#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pb {
struct Point {
    double x{}, y{}, width{1.5};
};
// Snapshots share immutable point arrays; editing one stroke detaches that array only.
class Points {
    std::shared_ptr<std::vector<Point>> data_ = std::make_shared<std::vector<Point>>();
    void detach() {
        if (data_.use_count() != 1)
            data_ = std::make_shared<std::vector<Point>>(*data_);
    }

  public:
    Points() = default;
    Points(std::initializer_list<Point> points) : data_(std::make_shared<std::vector<Point>>(points)) {}
    size_t size() const {
        return data_->size();
    }
    bool empty() const {
        return data_->empty();
    }
    const void *identity() const {
        return data_.get();
    }
    auto begin() const {
        return data_->cbegin();
    }
    auto end() const {
        return data_->cend();
    }
    auto begin() {
        detach();
        return data_->begin();
    }
    auto end() {
        detach();
        return data_->end();
    }
    const Point &front() const {
        return data_->front();
    }
    const Point &back() const {
        return data_->back();
    }
    Point &front() {
        detach();
        return data_->front();
    }
    Point &back() {
        detach();
        return data_->back();
    }
    const Point &operator[](size_t i) const {
        return (*data_)[i];
    }
    Point &operator[](size_t i) {
        detach();
        return (*data_)[i];
    }
    void reserve(size_t n) {
        detach();
        data_->reserve(n);
    }
    void push_back(Point point) {
        detach();
        data_->push_back(point);
    }
};
struct Size {
    double w{595}, h{842};
};
struct Color {
    uint8_t r{}, g{}, b{}, a{255};
};
enum class Kind { Ink, Marker, Rectangle, Ellipse, Line, Arrow };
struct Stroke {
    uint64_t id{};
    int page{};
    Kind kind{Kind::Ink};
    Color color{21, 101, 192, 255};
    double width{1.5};
    Points points;
};
using Contour = std::vector<Point>;
using Geometry = std::vector<Contour>;
struct Bounds {
    double l{}, t{}, r{}, b{};
};
double pressureWidth(double pressure, double base, bool valid);
// Exterior tangents of two round nibs. False when one nib contains the other.
bool nibTangents(Point a, Point b, std::array<Point, 4> &corners);
Geometry geometry(const Stroke &stroke);
Bounds bounds(const Stroke &stroke);
bool hit(const Stroke &stroke, double x, double y, double tolerance);
bool hitSweep(const Stroke &stroke, Point from, Point to, double tolerance);
void translate(Stroke &stroke, double dx, double dy);
std::string encode(const Stroke &stroke);
bool decode(const std::string &data, Stroke &stroke);
struct History {
    std::vector<std::vector<Stroke>> states{{}};
    size_t cursor{};
    std::string saved;
    // Repainting the title-bar dirty dot must not hash the whole notebook each frame.
    mutable std::string cachedFingerprint;
    uint64_t revision{};
    const std::vector<Stroke> &current() const {
        return states[cursor];
    }
    void reset(std::vector<Stroke> strokes);
    void commit(std::vector<Stroke> strokes);
    void undo();
    void redo();
    std::string fingerprint() const;
    bool dirty() const {
        return fingerprint() != saved;
    }
};
} // namespace pb
