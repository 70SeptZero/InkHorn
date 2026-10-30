#include "core.hpp"
#include "interaction.hpp"
#include <iostream>
#include <stdexcept>
using namespace pb;
void require(bool v, const char *message) {
    if (!v)
        throw std::runtime_error(message);
}
int main() {
    try {
        require(pressureWidth(.1, 2, true) < pressureWidth(.9, 2, true), "pressure must change width");
        require(pressureWidth(0, 2, false) == 2, "missing pressure must remain visible");
        require(pressureWidth(1.0 / 1024, 2, true) >= .7 && std::abs(pressureWidth(0, 2, true) - .7) < 1e-12,
                "the lightest Windows contact must leave a visible nib");
        require(std::abs(pressureWidth(.5, 2, true) - 2.075) < 1e-12 &&
                    std::abs(pressureWidth(1, 2, true) - 3.2) < 1e-12,
                "normal pressure stays near nominal and heavy pressure is restrained");
        for (int i = 1; i <= 1024; ++i)
            require(pressureWidth(i / 1024.0, 2, true) >= pressureWidth((i - 1) / 1024.0, 2, true),
                    "pressure curve must stay monotonic");
        const double slopeBefore = (pressureWidth(.16, 1, true) - pressureWidth(.1599, 1, true)) / .0001;
        const double slopeAfter = (pressureWidth(.1601, 1, true) - pressureWidth(.16, 1, true)) / .0001;
        require(std::abs(slopeAfter - slopeBefore) < .001, "light pressure must have no response kink");
        for (int i = 0; i <= 1024; ++i) {
            const double p = i / 1024.0, oldWidth = 2 * (.55 + p * (.9 + .15 * p));
            require(p < .3 ? pressureWidth(p, 2, true) < oldWidth : pressureWidth(p, 2, true) == oldWidth,
                    "only pressure below 30 percent may become thinner");
        }
        const double joinLeft = (pressureWidth(.3, 1, true) - pressureWidth(.29999, 1, true)) / .00001;
        const double joinRight = (pressureWidth(.30001, 1, true) - pressureWidth(.3, 1, true)) / .00001;
        require(std::abs(joinLeft - joinRight) < .001, "low-pressure reduction must join smoothly");
        std::array<Point, 4> tangent;
        require(nibTangents({0, 0, 2}, {4, 0, 6}, tangent), "variable nib tangents exist");
        const double tx = tangent[1].x - tangent[0].x, ty = tangent[1].y - tangent[0].y;
        require(std::abs(tangent[0].x * tx + tangent[0].y * ty) < 1e-10 &&
                    std::abs((tangent[1].x - 4) * tx + tangent[1].y * ty) < 1e-10,
                "pressure ramp sides must meet round nibs tangentially");
        require(!nibTangents({0, 0, 2}, {.1, 0, 6}, tangent) &&
                    !nibTangents({0, 0, 2}, {0, 0, 2}, tangent),
                "stationary or enclosed nibs must not create degenerate connectors");
        for (const auto c : {Color{0, 0, 0, 255}, Color{255, 255, 255, 255}, Color{128, 128, 128, 255},
                             Color{18, 171, 239, 255}, Color{255, 0, 180, 255}, Color{0, 255, 0, 255}}) {
            const auto roundtrip = hsvColor(colorHsv(c));
            require(std::abs(int(c.r) - roundtrip.r) <= 1 && std::abs(int(c.g) - roundtrip.g) <= 1 &&
                        std::abs(int(c.b) - roundtrip.b) <= 1,
                    "RGB/HSV roundtrip");
            Color decoded;
            require(parseColorHex(colorHex(c), decoded) && decoded.r == c.r && decoded.g == c.g &&
                        decoded.b == c.b,
                    "HEX color roundtrip");
        }
        Color rejected;
        require(!parseColorHex(L"#ZZ00FF", rejected) && !parseColorHex(L"#FFFFF", rejected),
                "reject invalid color input");
        WheelScroll wheel;
        wheel.add(90, 0);
        const double wheelFirst = wheel.tick(.016), wheelSecond = wheel.tick(.032);
        require(wheelFirst > wheelSecond && wheelSecond > 0 && wheelFirst < 45,
                "a wheel notch must move over several decaying frames");
        double wheelDistance = wheelFirst + wheelSecond;
        for (int i = 3; i <= 20; ++i)
            wheelDistance += wheel.tick(i * .016);
        require(!wheel.active() && std::abs(wheelDistance - 90) < 1e-8,
                "one notch must settle promptly at exactly the requested distance");
        wheel.add(90, 1);
        wheel.add(45, 1.005);
        wheelDistance = 0;
        for (int i = 1; i <= 20; ++i)
            wheelDistance += wheel.tick(1 + i * .016);
        require(std::abs(wheelDistance - 135) < 1e-8, "rapid wheel input must accumulate");
        wheel.add(90, 2);
        wheel.tick(2.016);
        wheel.add(-90, 2.02);
        require(wheel.tick(2.032) < 0, "reversing the wheel must cancel the old direction immediately");
        wheel.stop();
        wheel.add(9000, 3);
        wheelDistance = 0;
        for (int i = 1; i <= 20; ++i)
            wheelDistance += wheel.tick(3 + i * .016);
        require(!wheel.active() && std::abs(wheelDistance - 720) < 1e-8,
                "wheel backlog must stay bounded rather than coast through many pages");
        wheel.add(90, 4);
        require(wheel.tick(4.5) == 0 && !wheel.active(), "a delayed UI must not resume with a wheel jump");
        wheel.add(90, 5);
        wheel.stop();
        require(wheel.tick(5.016) == 0, "new contact must cancel pending wheel movement");
        ScrollMomentum slow;
        slow.begin(0, 0);
        slow.sample(1, .02);
        slow.sample(2, .04);
        require(!slow.release(.041), "slow release must stop without coasting");
        ScrollMomentum flick;
        flick.begin(0, 0);
        flick.sample(30, .02);
        flick.sample(70, .04);
        require(flick.release(.041), "fast release must start coasting");
        const double firstStep = flick.tick(.057), secondStep = flick.tick(.073);
        require(firstStep > secondStep && secondStep > 0, "coasting must decay smoothly");
        flick.stop();
        require(!flick.active() && flick.tick(.1) == 0, "new contact must be able to stop inertia");
        ScrollMomentum paused;
        paused.begin(0, 0);
        paused.sample(80, .03);
        require(!paused.release(.2), "holding still before release must cancel old velocity");
        ScrollMomentum slowed;
        slowed.begin(0, 0);
        slowed.sample(60, .02);
        slowed.sample(65, .12);
        require(!slowed.release(.121), "recent slow motion must replace the initial fast motion");
        ScrollMomentum precise;
        precise.begin(0, 0);
        precise.sample(60, .02);
        precise.sample(120, .04);
        precise.sample(122, .06);
        precise.sample(124, .08);
        require(!precise.release(.081), "a slow final 40 ms must stop a previously fast drag precisely");
        auto fastFlick = [] {
            ScrollMomentum result;
            result.begin(0, 0);
            result.sample(50, .02);
            result.sample(100, .04);
            require(result.release(.041), "fast flick setup");
            return result;
        };
        auto longFlick = fastFlick();
        double coastDistance = 0, coastTime = 0;
        for (int frame = 1; frame <= 200 && longFlick.active(); ++frame) {
            coastTime = frame * .016;
            coastDistance += longFlick.tick(.041 + coastTime);
            if (frame == 100)
                require(longFlick.active(), "fast flick must coast beyond the former 1.5 second limit");
        }
        require(!longFlick.active() && coastTime > 2 && coastTime <= 3.016 && coastDistance > 1000 &&
                    coastDistance < 1500,
                "fast inertia must travel farther but end within three seconds");
        auto sixtyHz = fastFlick(), oneTwentyHz = fastFlick();
        double coarse = 0, fine = 0;
        for (int i = 1; i <= 60; ++i)
            coarse += sixtyHz.tick(.041 + i / 60.0);
        for (int i = 1; i <= 120; ++i)
            fine += oneTwentyHz.tick(.041 + i / 120.0);
        require(std::abs(coarse - fine) < 1e-6, "inertia distance must not depend on timer frequency");
        auto interrupted = fastFlick();
        require(interrupted.tick(.5) == 0 && !interrupted.active(), "stall must cancel inertia without jumping");
        InkStabilizer stable;
        stable.reset({0, 0, 1}, 0);
        const auto filtered = stable.filter({.2, -.1, 3}, .005, 1);
        require(filtered.x > 0 && filtered.x < .2 && filtered.y < 0 && filtered.y > -.1,
                "stabilizer must damp small jitter without overshooting");
        require(filtered.width > 1 && filtered.width < 3, "pressure transitions must remain continuous");
        InkStabilizer jitter;
        jitter.reset({0, 0, 1}, 0);
        double rawEnergy = 0, filteredEnergy = 0;
        for (int i = 1; i <= 240; ++i) {
            const double noise = (i % 2 ? 1 : -1) * .15;
            const auto out = jitter.filter({noise, -noise, 1}, i / 240.0, 1);
            if (i > 24) {
                rawEnergy += 2 * noise * noise;
                filteredEnergy += out.x * out.x + out.y * out.y;
            }
        }
        require(filteredEnergy < rawEnergy * .4, "slow writing must suppress fine jitter");
        InkStabilizer quick;
        quick.reset({0, 0, 1}, 0);
        Point tip;
        for (int i = 1; i <= 240; ++i) {
            const Point raw{i * 800 / 240.0, 0, 1};
            tip = quick.filter(raw, i / 240.0, 1);
            require(tip.x <= raw.x && raw.x - tip.x <= 2.001, "fast ink must have bounded lag and no prediction overshoot");
        }
        const auto duplicate = quick.filter({9000, 9000, 1}, 1, 1);
        require(duplicate.x == tip.x && duplicate.y == tip.y, "old timestamps must not distort the next point");
        for (double zoom : {.25, 1.0, 4.0}) {
            for (int hz : {60, 120, 240}) {
                InkStabilizer replay;
                replay.reset({0, 0, 1}, 0);
                for (int i = 1; i <= hz; ++i) {
                    const double t = double(i) / hz;
                    const Point raw{600 * t / zoom, 30 * std::sin(t * 8) / zoom, 1 + .4 * std::sin(t * 10)};
                    const auto out = replay.filter(raw, t, zoom);
                    require(std::hypot(out.x - raw.x, out.y - raw.y) * zoom <= 1.351,
                            "follow distance stays bounded at different sample rates and zoom levels");
                }
            }
        }
        Stroke a{42, 0, Kind::Ink, {21, 101, 192, 255}, 2, {{10, 20, .7}, {30, 40, 3.6}}};
        Stroke b;
        require(decode(encode(a), b), "stroke roundtrip");
        require(encode(a) == encode(b), "exact model roundtrip");
        require(!decode("PB1 1 0 0 0 0 0 255 1 999999999", b), "reject unbounded allocation");
        require(!decode(encode(a) + " trailing", b), "reject corrupt suffix");
        auto g = geometry(a);
        require(!g.empty(), "ink must have vector geometry");
        require(hit(a, 20, 30, 1), "centerline hit");
        require(!hit(a, 200, 300, 1), "far point miss");
        Stroke crossing{99, 0, Kind::Ink, {0, 0, 0, 255}, 2, {{0, 50, 2}, {100, 50, 2}}};
        require(!hit(crossing, 40, 90, 2) && !hit(crossing, 40, 10, 2) &&
                    hitSweep(crossing, {40, 90}, {40, 10}, 2),
                "fast upward erasing must include the gap between mouse samples");
        require(hitSweep(crossing, {40, 10}, {40, 90}, 2) &&
                    !hitSweep(crossing, {110, 10}, {110, 90}, 2),
                "swept eraser must work both ways without widening the tip");
        require(!hitSweep(crossing, {-20, 40}, {20, 0}, 2),
                "overlapping broad-phase boxes alone must not erase a nearby stroke");
        require(hitSweep(crossing, {40, 50}, {40, 50}, 2), "stationary eraser must still hit");
        const auto sharedOriginal = a;
        auto before = bounds(a);
        translate(a, 13, -7);
        auto after = bounds(a);
        require(std::abs(after.l - before.l - 13) < 1e-6 && std::abs(after.t - before.t + 7) < 1e-6,
                "translation must preserve shape");
        require(a.points[1].width == 3.6, "moving must not reapply pressure");
        require(sharedOriginal.points[0].x == 10, "copy-on-write move must preserve undo snapshots");
        History h;
        h.reset({a});
        require(!h.dirty(), "initial clean");
        h.commit({});
        require(h.dirty(), "delete dirty");
        h.undo();
        require(!h.dirty(), "undo to saved state");
        h.redo();
        require(h.current().empty(), "redo delete");
        h.undo();
        h.commit({a, b});
        require(h.cursor + 1 == h.states.size(), "new edits drop redo branch");
        for (auto kind : {Kind::Rectangle, Kind::Ellipse, Kind::Line, Kind::Arrow}) {
            a.kind = kind;
            require(!geometry(a).empty(), "shape geometry");
        }
        std::cout << "PASS: pressure, bounded parsing, geometry, move, delete, savepoint, redo\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
