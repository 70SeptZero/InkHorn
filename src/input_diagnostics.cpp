#include "input_diagnostics.hpp"
#include <sstream>

namespace pb {
namespace {
const char *penMessage(UINT message) {
    switch (message) {
    case WM_POINTERDOWN:
        return "POINTER_DOWN";
    case WM_POINTERUP:
        return "POINTER_UP";
    case WM_POINTERENTER:
        return "POINTER_ENTER";
    case WM_POINTERLEAVE:
        return "POINTER_LEAVE";
    default:
        return "POINTER_UPDATE";
    }
}
} // namespace
bool InputDiagnostics::start(const std::filesystem::path &directory) {
    stop();
    try {
        std::filesystem::create_directories(directory);
        SYSTEMTIME time{};
        GetLocalTime(&time);
        wchar_t name[120]{};
        swprintf_s(name, L"pen-input-%04u%02u%02u-%02u%02u%02u-%03u-%lu.log", time.wYear, time.wMonth,
                   time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                   GetCurrentProcessId());
        file_ = directory / name;
        stream_.open(file_, std::ios::binary | std::ios::trunc);
        if (!stream_) {
            stream_.close();
            return false;
        }
        started_ = GetTickCount64();
        lastUpdate_ = 0;
        lastMouse_ = 0;
        lastSource_ = ~0u;
        lastId_ = 0;
        lastFlags_ = lastMask_ = lastButtons_ = ~0u;
        lines_ = 0;
        stream_ << "InkHorn pen input diagnostic v5\n"
                   "Application window messages only; no global hooks; no PDF contents/paths.\n"
                   "Pen flags: BARREL=1 INVERTED=2 ERASER=4. Unchanged UPDATE sampled at 100 ms.\n"
                   "Current mapping: BARREL/ERASER/INVERTED => temporary whole-stroke eraser.\n"
                   "Canvas contacts are isolated from UI taps, mouse events and old history.\n";
        last_ = L"等待笔或侧键输入…";
        write("START");
        return true;
    } catch (...) {
        stop();
        return false;
    }
}
void InputDiagnostics::write(const std::string &line) {
    if (!active())
        return;
    stream_ << (GetTickCount64() - started_) << " ms " << line << '\n';
    if (++lines_ >= 20000 || !stream_)
        stop();
}
void InputDiagnostics::stop() {
    if (active()) {
        stream_ << "STOP (manual/window close/line limit/write failure)\n";
        stream_.close();
    }
}
bool InputDiagnostics::pen(UINT message, const POINTER_PEN_INFO &p, const char *surface) {
    if (!active())
        return false;
    const auto now = GetTickCount64();
    const auto buttons = p.pointerInfo.pointerFlags &
                         (POINTER_FLAG_INCONTACT | POINTER_FLAG_FIRSTBUTTON | POINTER_FLAG_SECONDBUTTON |
                          POINTER_FLAG_THIRDBUTTON | POINTER_FLAG_FOURTHBUTTON | POINTER_FLAG_FIFTHBUTTON);
    if (message == WM_POINTERUPDATE && lastId_ == p.pointerInfo.pointerId && lastFlags_ == p.penFlags &&
        lastMask_ == p.penMask && lastButtons_ == buttons && now - lastUpdate_ < 100)
        return false;
    lastId_ = p.pointerInfo.pointerId;
    lastFlags_ = p.penFlags;
    lastMask_ = p.penMask;
    lastButtons_ = buttons;
    lastUpdate_ = now;
    std::ostringstream out;
    out << surface << ' ' << penMessage(message) << " id=" << p.pointerInfo.pointerId << " source=0x"
        << std::hex << reinterpret_cast<uintptr_t>(p.pointerInfo.sourceDevice) << " pointerFlags=0x"
        << p.pointerInfo.pointerFlags << " buttonChange=" << std::dec << p.pointerInfo.ButtonChangeType
        << " penFlags=0x" << std::hex << p.penFlags << std::dec
        << " BARREL=" << bool(p.penFlags & PEN_FLAG_BARREL)
        << " INVERTED=" << bool(p.penFlags & PEN_FLAG_INVERTED)
        << " ERASER=" << bool(p.penFlags & PEN_FLAG_ERASER)
        << " CONTACT=" << bool(p.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) << " mask=0x" << std::hex
        << p.penMask << std::dec << " pressure=" << p.pressure << " tilt=" << p.tiltX << ',' << p.tiltY;
    write(out.str());
    std::wostringstream display;
    display << L"笔：BARREL=" << bool(p.penFlags & PEN_FLAG_BARREL) << L"  ERASER="
            << bool(p.penFlags & PEN_FLAG_ERASER) << L"  INVERTED=" << bool(p.penFlags & PEN_FLAG_INVERTED)
            << L"  接触=" << bool(p.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT) << L"  压力="
            << p.pressure;
    last_ = display.str();
    return true;
}
void InputDiagnostics::mouseSource(UINT message, bool known, const INPUT_MESSAGE_SOURCE &source, bool accepted) {
    if (!active())
        return;
    const auto now = GetTickCount64();
    const DWORD device = known ? DWORD(source.deviceType) : 0;
    if ((message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE) && device == lastSource_ &&
        accepted == lastAccepted_ && now - lastMouse_ < 100)
        return;
    lastMouse_ = now;
    lastSource_ = device;
    lastAccepted_ = accepted;
    const char *name = device == IMDT_TOUCHPAD ? "TOUCHPAD" : device == IMDT_MOUSE ? "MOUSE" :
                       device == IMDT_PEN ? "PEN" : device == IMDT_TOUCH ? "TOUCH" : "UNAVAILABLE";
    std::ostringstream out;
    out << "MOUSE_SOURCE=" << name << " known=" << known << " accepted=" << accepted
        << " origin=" << int(source.originId) << " message=0x" << std::hex << message
        << " extraInfo=0x" << GetMessageExtraInfo();
    write(out.str());
}
bool InputDiagnostics::message(UINT message, WPARAM w, LPARAM l, const char *surface) {
    if (!active())
        return false;
    const char *name = nullptr;
    switch (message) {
    case WM_KEYDOWN:
        name = "KEY_DOWN";
        break;
    case WM_KEYUP:
        name = "KEY_UP";
        break;
    case WM_SYSKEYDOWN:
        name = "SYSKEY_DOWN";
        break;
    case WM_SYSKEYUP:
        name = "SYSKEY_UP";
        break;
    case WM_LBUTTONDOWN:
        name = "MOUSE_LEFT_DOWN";
        break;
    case WM_LBUTTONUP:
        name = "MOUSE_LEFT_UP";
        break;
    case WM_RBUTTONDOWN:
        name = "MOUSE_RIGHT_DOWN";
        break;
    case WM_RBUTTONUP:
        name = "MOUSE_RIGHT_UP";
        break;
    case WM_MBUTTONDOWN:
        name = "MOUSE_MIDDLE_DOWN";
        break;
    case WM_MBUTTONUP:
        name = "MOUSE_MIDDLE_UP";
        break;
    case WM_MOUSEWHEEL:
        name = "MOUSE_WHEEL";
        break;
    case WM_MOUSEHWHEEL:
        name = "MOUSE_HWHEEL";
        break;
    case WM_POINTERWHEEL:
        name = "POINTER_WHEEL";
        break;
    case WM_POINTERHWHEEL:
        name = "POINTER_HWHEEL";
        break;
    case WM_HSCROLL:
        name = "HSCROLL";
        break;
    case WM_VSCROLL:
        name = "VSCROLL";
        break;
    case WM_GESTURE:
        name = "GESTURE";
        break;
    case WM_XBUTTONDOWN:
        name = "MOUSE_X_DOWN";
        break;
    case WM_XBUTTONUP:
        name = "MOUSE_X_UP";
        break;
    case WM_SETFOCUS:
        name = "FOCUS_GAIN";
        break;
    case WM_KILLFOCUS:
        name = "FOCUS_LOST";
        break;
    default:
        return false;
    }
    std::ostringstream out;
    out << surface << ' ' << name << " wParam=0x" << std::hex << w << " lParam=0x" << l << " extraInfo=0x"
        << GetMessageExtraInfo() << std::dec << " Ctrl=" << (GetKeyState(VK_CONTROL) < 0)
        << " Shift=" << (GetKeyState(VK_SHIFT) < 0) << " Alt=" << (GetKeyState(VK_MENU) < 0)
        << " asyncCtrl=" << (GetAsyncKeyState(VK_CONTROL) < 0);
    std::wstring detail;
    if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL ||
        message == WM_POINTERWHEEL || message == WM_POINTERHWHEEL) {
        out << " delta=" << short(HIWORD(w));
        detail = L"  增量=" + std::to_wstring(short(HIWORD(w)));
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
            out << " wheelCtrl=" << bool(LOWORD(w) & MK_CONTROL);
    }
    if (message == WM_HSCROLL || message == WM_VSCROLL) {
        static const char *commands[]{"LINE_BACK", "LINE_FORWARD", "PAGE_BACK", "PAGE_FORWARD",
                                      "THUMB_POSITION", "THUMB_TRACK", "START", "END", "END_SCROLL"};
        const unsigned command = LOWORD(w);
        out << " command=" << command << "(" << (command < 9 ? commands[command] : "UNKNOWN")
            << ") position=" << HIWORD(w) << " childControl=" << bool(l);
        detail = L"  命令=" + std::to_wstring(command);
    }
    if (message == WM_GESTURE) {
        GESTUREINFO info{sizeof(info)};
        if (GetGestureInfo(reinterpret_cast<HGESTUREINFO>(l), &info)) {
            out << " id=" << info.dwID << " flags=" << info.dwFlags << " distance=" << info.ullArguments
                << " instance=" << info.dwInstanceID;
            detail = L"  手势=" + std::to_wstring(info.dwID);
        }
    }
    write(out.str());
    const std::string label(name);
    last_ = std::wstring(label.begin(), label.end()) +
            (detail.empty() ? L"  输入码=" + std::to_wstring(w) : detail);
    return true;
}
void InputDiagnostics::navigation(bool precise, const char *action, int axis) {
    if (!active()) return;
    std::ostringstream out;
    out << "NAVIGATION precise=" << precise << " axis=" << axis << " action=" << action;
    write(out.str());
}
void InputDiagnostics::scrollRange(const SCROLLINFO &info) {
    if (!active()) return;
    std::ostringstream out;
    out << "HORIZONTAL_RANGE min=" << info.nMin << " max=" << info.nMax
        << " page=" << info.nPage << " position=" << info.nPos;
    write(out.str());
}
} // namespace pb
