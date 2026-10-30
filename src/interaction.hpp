#pragma once
#include "core.hpp"
#include <chrono>
#include <deque>

namespace pb {
struct Hsv {
    double hue{}, saturation{}, value{};
};
inline Color hsvColor(Hsv hsv) {
    const double hue = std::fmod(std::max(0.0, hsv.hue), 360.0) / 60;
    const double chroma = hsv.value * hsv.saturation;
    const double x = chroma * (1 - std::abs(std::fmod(hue, 2) - 1)), m = hsv.value - chroma;
    double r{}, g{}, b{};
    if (hue < 1) {
        r = chroma;
        g = x;
    } else if (hue < 2) {
        r = x;
        g = chroma;
    } else if (hue < 3) {
        g = chroma;
        b = x;
    } else if (hue < 4) {
        g = x;
        b = chroma;
    } else if (hue < 5) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    return {uint8_t(std::round((r + m) * 255)), uint8_t(std::round((g + m) * 255)),
            uint8_t(std::round((b + m) * 255)), 255};
}
inline Hsv colorHsv(Color color) {
    const double r = color.r / 255.0, g = color.g / 255.0, b = color.b / 255.0;
    const double high = std::max({r, g, b}), low = std::min({r, g, b}), delta = high - low;
    double hue = 0;
    if (delta > 0) {
        if (high == r)
            hue = 60 * std::fmod((g - b) / delta, 6.0);
        else if (high == g)
            hue = 60 * ((b - r) / delta + 2);
        else
            hue = 60 * ((r - g) / delta + 4);
        if (hue < 0)
            hue += 360;
    }
    return {hue, high == 0 ? 0 : delta / high, high};
}
inline std::wstring colorHex(Color color) {
    constexpr wchar_t digits[] = L"0123456789ABCDEF";
    std::wstring result = L"#000000";
    const uint8_t channels[] = {color.r, color.g, color.b};
    for (int i = 0; i < 3; ++i) {
        result[1 + i * 2] = digits[channels[i] >> 4];
        result[2 + i * 2] = digits[channels[i] & 15];
    }
    return result;
}
inline bool parseColorHex(const std::wstring &text, Color &color) {
    const size_t start = !text.empty() && text.front() == L'#' ? 1 : 0;
    if (text.size() - start != 6)
        return false;
    unsigned value = 0;
    for (size_t i = start; i < text.size(); ++i) {
        const wchar_t c = text[i];
        const int digit =
            c >= L'0' && c <= L'9'
                ? c - L'0'
                : (c >= L'A' && c <= L'F' ? c - L'A' + 10 : (c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1));
        if (digit < 0)
            return false;
        value = (value << 4) | unsigned(digit);
    }
    color = {uint8_t(value >> 16), uint8_t(value >> 8), uint8_t(value), 255};
    return true;
}
inline double inputTime() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Wheel input adds a small bounded distance, consumed by the existing animation timer.
// No thread or document work is needed here; reversing discards the old direction's tail.
class WheelScroll {
    double remaining_{}, lastTick_{}, lastInput_{};

  public:
    bool active() const {
        return remaining_ != 0;
    }
    void stop() {
        remaining_ = 0;
    }
    void add(double distance, double time) {
        if (distance == 0)
            return;
        if (!active() || time - lastTick_ > .25) {
            stop();
            lastTick_ = time;
        }
        if (remaining_ * distance < 0)
            remaining_ = 0;
        remaining_ = std::clamp(remaining_ + distance, -720.0, 720.0);
        lastInput_ = time;
    }
    double tick(double time) {
        if (!active())
            return 0;
        const double dt = time - lastTick_;
        if (dt <= 0)
            return 0;
        if (dt > .25) {
            stop(); // Do not jump after the UI has been delayed.
            return 0;
        }
        lastTick_ = time;
        const double distance = remaining_ * -std::expm1(-dt / .045);
        remaining_ -= distance;
        if (std::abs(remaining_) < .15 || time - lastInput_ >= .28) {
            const double finalDistance = distance + remaining_;
            stop();
            return finalDistance;
        }
        return distance;
    }
};

// Track recent *clamped document movement*, not pointer displacement against a page edge.
class ScrollMomentum {
    struct Sample {
        double time, position;
    };
    std::deque<Sample> samples_;
    double velocity_{}, lastTick_{}, started_{}, friction_{5.4};

  public:
    bool active() const {
        return velocity_ != 0;
    }
    void stop() {
        velocity_ = 0;
        samples_.clear();
    }
    void begin(double position, double time) {
        stop();
        samples_.push_back({time, position});
    }
    void sample(double position, double time) {
        if (!samples_.empty() && time <= samples_.back().time)
            return;
        samples_.push_back({time, position});
        while (samples_.size() > 2 && samples_.front().time < time - .075)
            samples_.pop_front();
        while (samples_.size() > 32)
            samples_.pop_front();
    }
    bool release(double time) {
        velocity_ = 0;
        if (samples_.size() < 2 || time - samples_.back().time > .075)
            return false;
        sample(samples_.back().position, time);
        const auto first = samples_.front(), last = samples_.back();
        const double duration = last.time - first.time;
        if (duration < .012 || duration > .18)
            return false;
        // Use the last 40 ms, interpolating at the window boundary. A fast movement
        // earlier in the gesture must not override a deliberate slow finish.
        const double from = std::max(first.time, last.time - .04);
        double position = first.position;
        for (size_t i = 1; i < samples_.size(); ++i) {
            const auto a = samples_[i - 1], b = samples_[i];
            if (b.time >= from) {
                position = a.position + (b.position - a.position) * (from - a.time) / (b.time - a.time);
                break;
            }
        }
        const double speed = (last.position - position) / (last.time - from);
        // Slow releases stop immediately. Only fast flicks get the longer 2–3 s coast.
        if (std::abs(speed) < 180)
            return false;
        velocity_ = std::clamp(speed, -5500.0, 5500.0);
        friction_ = 5.4 - 3.5 * std::clamp((std::abs(velocity_) - 180) / 1620, 0.0, 1.0);
        started_ = lastTick_ = time;
        return true;
    }
    double tick(double time) {
        if (!active())
            return 0;
        const double dt = time - lastTick_;
        if (dt <= 0)
            return 0;
        if (dt > .25 || time - started_ > 3.0) {
            stop();
            return 0;
        }
        lastTick_ = time;
        const double decay = std::exp(-friction_ * dt);
        const double distance = velocity_ * (1 - decay) / friction_;
        velocity_ *= decay;
        if (std::abs(velocity_) < 24)
            velocity_ = 0;
        return distance;
    }
};

// A short, speed-adaptive low-pass filter; only new input is filtered. Saved points already
// contain the final geometry, so reopening/moving a stroke never runs this filter again.
class InkStabilizer {
    Point previous_{}, raw_{};
    double vx_{}, vy_{};
    double lastTime_{};
    bool ready_{};

  public:
    void reset(Point p, double time) {
        previous_ = p;
        raw_ = p;
        vx_ = vy_ = 0;
        lastTime_ = time;
        ready_ = true;
    }
    Point filter(Point p, double time, double viewScale) {
        if (!ready_ || time - lastTime_ > .12) {
            reset(p, time);
            return p;
        }
        if (time <= lastTime_)
            return previous_;
        const double dt = std::clamp(time - lastTime_, .0001, .04);
        const double derivativeAlpha = -std::expm1(-6.283185307179586 * 18 * dt);
        vx_ += derivativeAlpha * ((p.x - raw_.x) * viewScale / dt - vx_);
        vy_ += derivativeAlpha * ((p.y - raw_.y) * viewScale / dt - vy_);
        raw_ = p;
        const double speed = std::hypot(vx_, vy_);
        // Speed-adaptive filtering: suppress slow jitter without buffering input.
        const double cutoff = 12 + std::min(228.0, speed * .32);
        const double alpha = 1 - std::exp(-6.283185307179586 * cutoff * dt);
        const double widthAlpha = -std::expm1(-6.283185307179586 * (p.width > previous_.width ? 32 : 24) * dt);
        const Point current = p;
        p.x = previous_.x + alpha * (p.x - previous_.x);
        p.y = previous_.y + alpha * (p.y - previous_.y);
        p.width = previous_.width + widthAlpha * (p.width - previous_.width);
        const double lag = std::hypot(current.x - p.x, current.y - p.y) * viewScale;
        const double maxLag = .55 + std::min(.8, speed * .0007);
        if (lag > maxLag) {
            p.x = current.x + (p.x - current.x) * maxLag / lag;
            p.y = current.y + (p.y - current.y) * maxLag / lag;
        }
        previous_ = p;
        lastTime_ = time;
        return p;
    }
};
} // namespace pb
