#include "input_diagnostics.hpp"
#include "pen_input.hpp"
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace pb;
int main() {
    try {
        for (int selected = 0; selected <= 5; ++selected) {
            InputToolMode mode;
            int tool = 0;
            if (mode.mouse(tool) || tool != 0)
                throw std::runtime_error("initial mouse mode must keep the pointer");
            tool = selected;
            const bool changed = mode.pen(tool);
            if (tool != (selected ? selected : 1) || changed != (selected == 0) || !mode.penActive())
                throw std::runtime_error("pen hover must preserve the mouse choice or default to pen 1");
            for (int i = 0; i < 1000; ++i)
                if (mode.pen(tool) || tool != (selected ? selected : 1))
                    throw std::runtime_error("repeated pen input must not switch or repaint tools");
            if (!mode.mouse(tool) || tool != 0 || mode.penActive() || mode.mouse(tool))
                throw std::runtime_error("returning to mouse must select the pointer exactly once");
            mode.pen(tool);
            tool = 0; // The pen can explicitly select the pointer too.
            if (mode.pen(tool) || tool != 0)
                throw std::runtime_error("repeated hover must respect an explicit pen selection");
        }
        if (!promotedPointerMouse(0xff515710) || !promotedPointerMouse(0xff515780) ||
            promotedPointerMouse(0) || !mouseInputMessage(WM_MOUSEMOVE) ||
            !mouseInputMessage(WM_MOUSEWHEEL) || mouseInputMessage(WM_POINTERUPDATE) ||
            mouseInputMessage(WM_PAINT))
            throw std::runtime_error("pen/touch mouse promotion and genuine mouse messages must be distinguishable");
        PenCoordinates coordinates;
        POINTER_INFO position{};
        position.ptPixelLocation = {-1800, 100};
        position.ptHimetricLocation = {1604, 2506};
        coordinates.mapping({100, 1000, 24100, 14500}, {-1920, 0, 0, 1080});
        auto precise = coordinates.screen(position);
        if (precise.x != -1800 || precise.y != 100)
            throw std::runtime_error("mismatched coordinate mapping must fall back to pixel input");
        position.ptPixelLocation = {-1800, 120};
        precise = coordinates.screen(position);
        if (std::abs(precise.x + 1799.68) > 1e-8 || std::abs(precise.y - 120.48) > 1e-8)
            throw std::runtime_error("digitizer fractions and negative monitor origins must be preserved");
        position.ptHimetricLocation = {0, 0};
        precise = coordinates.screen(position);
        if (precise.x != -1800 || precise.y != 120)
            throw std::runtime_error("missing HIMETRIC input must not jump the pen");
        coordinates.mapping({}, {});
        if (coordinates.screen(position).x != -1800)
            throw std::runtime_error("invalid device extents must fall back without division by zero");
        if (!penEraser(PEN_FLAG_INVERTED) || !penEraser(PEN_FLAG_ERASER) || !penEraser(PEN_FLAG_BARREL) ||
            penEraser(0))
            throw std::runtime_error("Wacom eraser flags must map to the temporary eraser");
        PenContact contact;
        POINTER_PEN_INFO sample{};
        sample.pointerInfo.pointerId = 9;
        sample.pointerInfo.PerformanceCount = 100;
        sample.pointerInfo.pointerFlags = POINTER_FLAG_INCONTACT;
        contact.begin(sample);
        sample.pointerInfo.PerformanceCount = 90;
        if (contact.accept(sample))
            throw std::runtime_error("pre-DOWN history must be rejected");
        sample.pointerInfo.PerformanceCount = 110;
        sample.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE;
        if (contact.accept(sample))
            throw std::runtime_error("hover must not extend a stroke");
        sample.pointerInfo.pointerFlags = POINTER_FLAG_INCONTACT;
        if (!contact.accept(sample) || contact.accept(sample))
            throw std::runtime_error("contact must be accepted exactly once");
        sample.pointerInfo.pointerId = 10;
        sample.pointerInfo.PerformanceCount = 120;
        if (contact.accept(sample))
            throw std::runtime_error("another pointer must not extend a stroke");
        contact.end();
        if (contact.accept(sample))
            throw std::runtime_error("UI-only/ended contacts cannot draw");
        const auto directory =
            std::filesystem::current_path() / ("input-test-" + std::to_string(GetCurrentProcessId()));
        InputDiagnostics diagnostics;
        POINTER_PEN_INFO pen{};
        pen.pointerInfo.pointerType = PT_PEN;
        pen.pointerInfo.pointerId = 9;
        pen.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE;
        pen.penMask = PEN_MASK_PRESSURE;
        pen.pressure = 512;
        if (diagnostics.pen(WM_POINTERDOWN, pen) || !diagnostics.start(directory))
            throw std::runtime_error("diagnostics must be opt-in and start successfully");
        diagnostics.pen(WM_POINTERENTER, pen);
        pen.penFlags = PEN_FLAG_ERASER;
        if (!diagnostics.pen(WM_POINTERUPDATE, pen))
            throw std::runtime_error("eraser transition missing");
        pen.penFlags = PEN_FLAG_BARREL | PEN_FLAG_INVERTED;
        diagnostics.pen(WM_POINTERDOWN, pen);
        diagnostics.message(WM_KEYDOWN, 'Z', 0);
        diagnostics.message(WM_KEYUP, 'Z', 0, "edit");
        diagnostics.message(WM_RBUTTONDOWN, 0, 0);
        const auto file = diagnostics.file();
        diagnostics.stop();
        std::ifstream input(file, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(input)), {});
        input.close();
        for (const auto *value : {"ERASER=1", "BARREL=1", "INVERTED=1", "pressure=512", "KEY_DOWN",
                                  "edit KEY_UP", "MOUSE_RIGHT_DOWN", "STOP"})
            if (text.find(value) == std::string::npos)
                throw std::runtime_error("diagnostic log lost input evidence");
        std::filesystem::remove(file);
        std::filesystem::remove(directory);
        std::cout << "PASS: opt-in local pen/key/mouse diagnostic roundtrip\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
