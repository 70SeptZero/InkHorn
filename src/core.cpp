#include "core.hpp"
#include <bit>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace pb {
namespace {
constexpr double pi = 3.14159265358979323846;
Contour disc(double x, double y, double r) {
    // Bound the contour error instead of showing a fixed 16-sided cap on thick pens.
    const int n = std::clamp(int(std::ceil(pi * std::sqrt(std::max(.03, r) / .012))), 24, 128);
    Contour c;
    c.reserve(n);
    for (int i = 0; i < n; ++i) {
        double a = 2 * pi * i / n;
        c.push_back({x + std::cos(a) * r, y + std::sin(a) * r, 0});
    }
    return c;
}
void segment(Geometry &g, Point a, Point b, double ra, double rb) {
    a.width = 2 * ra;
    b.width = 2 * rb;
    std::array<Point, 4> corners;
    if (nibTangents(a, b, corners))
        g.emplace_back(corners.begin(), corners.end());
}
double segmentDistance(double x, double y, Point a, Point b) {
    double dx = b.x - a.x, dy = b.y - a.y, d = dx * dx + dy * dy;
    double u = d > 0 ? std::clamp(((x - a.x) * dx + (y - a.y) * dy) / d, 0.0, 1.0) : 0;
    return std::hypot(x - a.x - u * dx, y - a.y - u * dy);
}
double segmentSeparation(Point a, Point b, Point c, Point d) {
    const double ax = b.x - a.x, ay = b.y - a.y, bx = d.x - c.x, by = d.y - c.y;
    const double cross = ax * by - ay * bx;
    if (std::abs(cross) > 1e-12) {
        const double x = c.x - a.x, y = c.y - a.y;
        const double t = (x * by - y * bx) / cross, u = (x * ay - y * ax) / cross;
        if (t >= 0 && t <= 1 && u >= 0 && u <= 1)
            return 0;
    }
    return std::min({segmentDistance(a.x, a.y, c, d), segmentDistance(b.x, b.y, c, d),
                     segmentDistance(c.x, c.y, a, b), segmentDistance(d.x, d.y, a, b)});
}
} // namespace
double pressureWidth(double p, double base, bool valid) {
    if (!valid)
        return base;
    p = std::clamp(p, 0.0, 1.0);
    // A continuous, restrained pen response: visible light contact, near nominal
    // width at normal pressure, and less ballooning under heavy pressure.
    double factor = .55 + p * (.9 + .15 * p);
    if (p < .3) {
        const double light = 1 - p / .3;
        factor -= .2 * light * light; // Joins the existing curve with zero slope change at 30%.
    }
    return base * factor;
}
bool nibTangents(Point a, Point b, std::array<Point, 4> &corners) {
    const double dx = b.x - a.x, dy = b.y - a.y, d = std::hypot(dx, dy);
    const double ra = std::max(.03, a.width / 2), rb = std::max(.03, b.width / 2);
    if (d <= std::abs(ra - rb) + 1e-8)
        return false;
    const double ux = dx / d, uy = dy / d, k = (ra - rb) / d;
    const double h = std::sqrt(std::max(0.0, 1 - k * k));
    const double nx = k * ux + h * uy, ny = k * uy - h * ux;
    const double mx = k * ux - h * uy, my = k * uy + h * ux;
    corners = {{{a.x + ra * nx, a.y + ra * ny, 0}, {b.x + rb * nx, b.y + rb * ny, 0},
                {b.x + rb * mx, b.y + rb * my, 0}, {a.x + ra * mx, a.y + ra * my, 0}}};
    return true;
}
Geometry geometry(const Stroke &s) {
    Geometry g;
    if (s.points.empty())
        return g;
    std::vector<Point> path(s.points.begin(), s.points.end());
    bool closed = false;
    if (s.kind == Kind::Rectangle && path.size() > 1) {
        auto a = path.front(), b = path.back();
        path = {{a.x, a.y, s.width}, {b.x, a.y, s.width}, {b.x, b.y, s.width}, {a.x, b.y, s.width}};
        closed = true;
    } else if (s.kind == Kind::Ellipse && path.size() > 1) {
        auto a = path.front(), b = path.back();
        double cx = (a.x + b.x) / 2, cy = (a.y + b.y) / 2;
        path.clear();
        const int segments = std::clamp(
            int(std::ceil(pi * std::sqrt(std::max(std::abs(b.x - a.x), std::abs(b.y - a.y)) / .024))), 96,
            512);
        for (int i = 0; i < segments; ++i) {
            double t = 2 * pi * i / segments;
            path.push_back({cx + std::abs(b.x - a.x) / 2 * std::cos(t),
                            cy + std::abs(b.y - a.y) / 2 * std::sin(t), s.width});
        }
        closed = true;
    }
    if (closed)
        path.push_back(path.front());
    for (size_t i = 0; i < path.size(); ++i) {
        double r = std::max(0.03, path[i].width / 2);
        g.push_back(disc(path[i].x, path[i].y, r));
        if (i)
            segment(g, path[i - 1], path[i], std::max(.03, path[i - 1].width / 2), r);
    }
    if (s.kind == Kind::Arrow && path.size() > 1) {
        auto a = path.front(), b = path.back();
        double t = std::atan2(b.y - a.y, b.x - a.x), n = std::max(7.0, s.width * 5);
        for (double d : {-.45, .45}) {
            Point c{b.x - n * std::cos(t + d), b.y - n * std::sin(t + d), s.width};
            segment(g, c, b, s.width / 2, s.width / 2);
            g.push_back(disc(c.x, c.y, s.width / 2));
        }
    }
    return g;
}
Bounds bounds(const Stroke &s) {
    Bounds b{1e30, 1e30, -1e30, -1e30};
    if (s.kind == Kind::Ink || s.kind == Kind::Marker || s.kind == Kind::Line) {
        for (const auto &p : s.points) {
            const double r = std::max(.03, p.width / 2);
            b.l = std::min(b.l, p.x - r);
            b.r = std::max(b.r, p.x + r);
            b.t = std::min(b.t, p.y - r);
            b.b = std::max(b.b, p.y + r);
        }
        return b;
    }
    for (const auto &c : geometry(s))
        for (auto p : c) {
            b.l = std::min(b.l, p.x);
            b.t = std::min(b.t, p.y);
            b.r = std::max(b.r, p.x);
            b.b = std::max(b.b, p.y);
        }
    return b;
}
bool hit(const Stroke &s, double x, double y, double tolerance) {
    if (s.kind == Kind::Rectangle || s.kind == Kind::Ellipse || s.kind == Kind::Arrow) {
        auto b = bounds(s);
        return x >= b.l - tolerance && x <= b.r + tolerance && y >= b.t - tolerance && y <= b.b + tolerance;
    }
    for (size_t i = 0; i < s.points.size(); ++i) {
        auto a = s.points[i], b = s.points[i ? i - 1 : 0];
        if (segmentDistance(x, y, a, b) <= tolerance + std::max(a.width, b.width) / 2)
            return true;
    }
    return false;
}
bool hitSweep(const Stroke &s, Point from, Point to, double tolerance) {
    if (s.points.empty())
        return false;
    const auto box = bounds(s);
    if (std::max(from.x, to.x) + tolerance < box.l || std::min(from.x, to.x) - tolerance > box.r ||
        std::max(from.y, to.y) + tolerance < box.t || std::min(from.y, to.y) - tolerance > box.b)
        return false;
    if (s.kind == Kind::Rectangle || s.kind == Kind::Ellipse || s.kind == Kind::Arrow) {
        if (hit(s, from.x, from.y, tolerance) || hit(s, to.x, to.y, tolerance))
            return true;
        const Point corners[] = {{box.l - tolerance, box.t - tolerance},
                                 {box.r + tolerance, box.t - tolerance},
                                 {box.r + tolerance, box.b + tolerance},
                                 {box.l - tolerance, box.b + tolerance}};
        for (int i = 0; i < 4; ++i)
            if (segmentSeparation(from, to, corners[i], corners[(i + 1) % 4]) < 1e-9)
                return true;
        return false;
    }
    for (size_t i = 0; i < s.points.size(); ++i) {
        const auto a = s.points[i ? i - 1 : 0], b = s.points[i];
        if (segmentSeparation(from, to, a, b) <= tolerance + std::max(a.width, b.width) / 2)
            return true;
    }
    return false;
}
void translate(Stroke &s, double dx, double dy) {
    for (auto &p : s.points) {
        p.x += dx;
        p.y += dy;
    }
}
std::string encode(const Stroke &s) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << std::setprecision(12);
    o << "PB1 " << s.id << ' ' << s.page << ' ' << static_cast<int>(s.kind) << ' ' << int(s.color.r) << ' '
      << int(s.color.g) << ' ' << int(s.color.b) << ' ' << int(s.color.a) << ' ' << s.width << ' '
      << s.points.size();
    for (auto p : s.points)
        o << ' ' << p.x << ' ' << p.y << ' ' << p.width;
    return o.str();
}
bool decode(const std::string &str, Stroke &result) {
    if (str.size() > 16 * 1024 * 1024)
        return false;
    std::istringstream in(str);
    in.imbue(std::locale::classic());
    std::string magic;
    Stroke s;
    int kind, r, g, b, a;
    size_t n;
    if (!(in >> magic >> s.id >> s.page >> kind >> r >> g >> b >> a >> s.width >> n) || magic != "PB1" ||
        kind < 0 || kind > 5 || n == 0 || n > 200000 || s.page < 0 || !std::isfinite(s.width) ||
        s.width <= 0 || s.width > 1000)
        return false;
    for (int c : {r, g, b, a})
        if (c < 0 || c > 255)
            return false;
    s.kind = static_cast<Kind>(kind);
    s.color = {uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)};
    s.points.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        Point p;
        if (!(in >> p.x >> p.y >> p.width) || !std::isfinite(p.x) || !std::isfinite(p.y) ||
            !std::isfinite(p.width) || std::abs(p.x) > 1e8 || std::abs(p.y) > 1e8 || p.width <= 0 ||
            p.width > 1000)
            return false;
        s.points.push_back(p);
    }
    in >> std::ws;
    if (!in.eof())
        return false;
    result = std::move(s);
    return true;
}
void History::reset(std::vector<Stroke> s) {
    ++revision;
    states.clear();
    states.push_back(std::move(s));
    cursor = 0;
    cachedFingerprint.clear();
    saved = fingerprint();
}
void History::commit(std::vector<Stroke> s) {
    ++revision;
    cachedFingerprint.clear();
    states.resize(cursor + 1);
    states.push_back(std::move(s));
    ++cursor;
    // Bounded first implementation. Persistent undo transactions replace snapshots later.
    if (states.size() > 81) {
        states.erase(states.begin());
        --cursor;
    }
}
void History::undo() {
    ++revision;
    cachedFingerprint.clear();
    if (cursor)
        --cursor;
}
void History::redo() {
    ++revision;
    cachedFingerprint.clear();
    if (cursor + 1 < states.size())
        ++cursor;
}
std::string History::fingerprint() const {
    if (!cachedFingerprint.empty())
        return cachedFingerprint;
    uint64_t h = 14695981039346656037ull;
    auto number = [&](uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            h ^= (value >> (i * 8)) & 255;
            h *= 1099511628211ull;
        }
    };
    for (const auto &s : current()) {
        number(s.id);
        number(uint64_t(s.page));
        number(uint64_t(s.kind));
        number(uint64_t(s.color.r) | (uint64_t(s.color.g) << 8) | (uint64_t(s.color.b) << 16) |
               (uint64_t(s.color.a) << 24));
        number(std::bit_cast<uint64_t>(s.width));
        number(s.points.size());
        for (const auto &p : s.points) {
            number(std::bit_cast<uint64_t>(p.x));
            number(std::bit_cast<uint64_t>(p.y));
            number(std::bit_cast<uint64_t>(p.width));
        }
    }
    cachedFingerprint = std::to_string(h);
    return cachedFingerprint;
}
} // namespace pb
