#pragma once
#include "core.hpp"
#include <windows.h>

namespace pb {
inline bool promotedPointerMouse(LPARAM extra) {
    return (extra & 0xffffff00) == 0xff515700;
}
inline bool mouseInputMessage(UINT message) {
    return message == WM_MOUSEMOVE || message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
           (message >= WM_LBUTTONDOWN && message <= WM_MBUTTONDBLCLK) ||
           (message >= WM_XBUTTONDOWN && message <= WM_XBUTTONDBLCLK) ||
           message == WM_NCMOUSEMOVE ||
           (message >= WM_NCLBUTTONDOWN && message <= WM_NCMBUTTONDBLCLK) ||
           (message >= WM_NCXBUTTONDOWN && message <= WM_NCXBUTTONDBLCLK);
}
// Tool selection is shared. Only a device transition supplies a default; repeated
// hover/move messages must not overwrite an explicit selection or repaint UI.
class InputToolMode {
    bool pen_{};

  public:
    bool penActive() const { return pen_; }
    bool pen(int &tool) {
        if (pen_)
            return false;
        pen_ = true;
        if (tool != 0)
            return false;
        tool = 1;
        return true;
    }
    bool mouse(int &tool) {
        if (!pen_)
            return false;
        pen_ = false;
        const bool changed = tool != 0;
        tool = 0;
        return changed;
    }
};
// Retain the digitizer's fractional pixels. Query the device mapping once at
// entry/contact (or a device change), never for every coalesced input sample.
class PenCoordinates {
    HANDLE device_{};
    RECT deviceRect_{}, displayRect_{};
    bool valid_{};

  public:
    void mapping(RECT device, RECT display) {
        deviceRect_ = device;
        displayRect_ = display;
        valid_ = device.right > device.left && device.bottom > device.top &&
                 display.right > display.left && display.bottom > display.top;
    }
    void refresh(HANDLE device, bool contactBoundary) {
        if (device == device_ && !contactBoundary)
            return;
        device_ = device;
        RECT physical{}, display{};
        valid_ = false;
        if (device && GetPointerDeviceRects(device, &physical, &display))
            mapping(physical, display);
    }
    Point screen(const POINTER_INFO &p) const {
        Point fallback{double(p.ptPixelLocation.x), double(p.ptPixelLocation.y)};
        if (!valid_)
            return fallback;
        const double x = displayRect_.left + (double(p.ptHimetricLocation.x) - deviceRect_.left) *
                         (double(displayRect_.right) - displayRect_.left) /
                         (double(deviceRect_.right) - deviceRect_.left);
        const double y = displayRect_.top + (double(p.ptHimetricLocation.y) - deviceRect_.top) *
                         (double(displayRect_.bottom) - displayRect_.top) /
                         (double(deviceRect_.bottom) - deviceRect_.top);
        // Some drivers expose missing or differently calibrated HIMETRIC data.
        // Keep Windows' pixel position if the two mappings do not agree.
        if (std::abs(x - fallback.x) > 2 || std::abs(y - fallback.y) > 2)
            return fallback;
        return {x, y};
    }
};
inline bool penEraser(UINT flags) {
    return (flags & (PEN_FLAG_BARREL | PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
}
// One stream belongs to one physical contact begun on the canvas. UI taps/hover
// and coalesced samples from before that DOWN must never extend a new stroke.
class PenContact {
    UINT id_{};
    HANDLE device_{};
    UINT64 last_{};

  public:
    static UINT64 stamp(const POINTER_PEN_INFO &p) {
        return p.pointerInfo.PerformanceCount ? p.pointerInfo.PerformanceCount : p.pointerInfo.dwTime;
    }
    void begin(const POINTER_PEN_INFO &p) {
        id_ = p.pointerInfo.pointerId;
        device_ = p.pointerInfo.sourceDevice;
        last_ = stamp(p);
    }
    void end() {
        id_ = 0;
        last_ = 0;
        device_ = nullptr;
    }
    bool active() const {
        return id_ != 0;
    }
    bool owns(UINT id) const {
        return id_ && id_ == id;
    }
    bool accept(const POINTER_PEN_INFO &p) {
        const auto time = stamp(p);
        if (!owns(p.pointerInfo.pointerId) || p.pointerInfo.sourceDevice != device_ ||
            !(p.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) || !time || time <= last_)
            return false;
        last_ = time;
        return true;
    }
};
} // namespace pb
