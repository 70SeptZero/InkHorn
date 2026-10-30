#pragma once
#include "core.hpp"
#include <d2d1.h>
#include <map>
#include <tuple>
#include <wrl/client.h>

namespace pb {
// Page-local raster tiles avoid allocating a mostly empty bounding image for every
// long stroke. Neither viewport position nor stroke count is a cache key.
class InkTiles {
    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    struct Model {
        uint64_t token{}, revision{}, erasing{}, moving{};
        std::vector<Stroke> strokes;
        std::vector<Bounds> boxes;
    };
    struct Tile {
        uint64_t modelToken{};
        std::vector<Stroke> strokes;
        ComPtr<ID2D1BitmapRenderTarget> target;
        ComPtr<ID2D1SolidColorBrush> brush;
        ComPtr<ID2D1Bitmap> bitmap;
        uint64_t used{};
        size_t bytes{};
    };
    using Key = std::tuple<uint64_t, int, double, float, float, int, int>;
    std::map<std::pair<uint64_t, int>, std::shared_ptr<Model>> models_;
    std::map<Key, Tile> tiles_;
    uint64_t frame_{}, nextModel_{1};
    static bool overlaps(Bounds a, Bounds b) {
        return a.l <= b.r && a.r >= b.l && a.t <= b.b && a.b >= b.t;
    }
    static bool same(const Stroke &a, const Stroke &b) {
        return a.id == b.id && a.points.identity() == b.points.identity() && a.kind == b.kind &&
               a.width == b.width && a.color.r == b.color.r && a.color.g == b.color.g &&
               a.color.b == b.color.b && a.color.a == b.color.a;
    }

  public:
    static constexpr size_t budget = 96 * 1024 * 1024;
    size_t bytes{}, updates{}, hits{};
    void beginFrame() {
        ++frame_;
    }
    void clear() {
        tiles_.clear();
        models_.clear();
        bytes = 0;
    }
    void forget(uint64_t document) {
        std::erase_if(models_, [&](const auto &entry) { return entry.first.first == document; });
        for (auto i = tiles_.begin(); i != tiles_.end();)
            if (std::get<0>(i->first) == document) {
                bytes -= i->second.bytes;
                i = tiles_.erase(i);
            } else
                ++i;
    }
    bool matches(uint64_t document, int page, const Stroke &stroke) const {
        const auto found = models_.find({document, page});
        if (found == models_.end())
            return false;
        for (const auto &s : found->second->strokes)
            if (same(s, stroke))
                return true;
        return false;
    }
    template <class Draw>
    void paint(ID2D1BitmapRenderTarget *destination, uint64_t document, int page, uint64_t revision,
               uint64_t erasing, uint64_t moving, const std::vector<Stroke> &strokes, double left,
               double top, double scale, Size pageSize, D2D1_RECT_F viewport, Draw draw) {
        auto &model = models_[{document, page}];
        if (!model || model->revision != revision || model->erasing != erasing || model->moving != moving) {
            auto next = std::make_shared<Model>();
            next->token = nextModel_++;
            next->revision = revision;
            next->erasing = erasing;
            next->moving = moving;
            for (const auto &s : strokes)
                if (s.page == page && (!moving || s.id != moving)) {
                    next->strokes.push_back(s);
                    next->boxes.push_back(bounds(s));
                }
            model = std::move(next);
        }
        float dx{}, dy{};
        destination->GetDpi(&dx, &dy);
        constexpr int edge = 512, gutter = 2, pixels = edge + gutter * 2;
        const double tileW = edge * 96.0 / dx, tileH = edge * 96.0 / dy;
        const double gx = gutter * 96.0 / dx, gy = gutter * 96.0 / dy;
        const int x0 = std::max(0, int(std::floor((viewport.left - left) / tileW)));
        const int y0 = std::max(0, int(std::floor((viewport.top - top) / tileH)));
        const int x1 = int(std::ceil(std::min(pageSize.w * scale, viewport.right - left) / tileW));
        const int y1 = int(std::ceil(std::min(pageSize.h * scale, viewport.bottom - top) / tileH));
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const Key key{document, page, scale, dx, dy, x, y};
                if (!tiles_.contains(key) && tiles_.size() >= 512) {
                    auto oldest = tiles_.end();
                    for (auto i = tiles_.begin(); i != tiles_.end(); ++i)
                        if (i->second.used != frame_ &&
                            (oldest == tiles_.end() || i->second.used < oldest->second.used))
                            oldest = i;
                    if (oldest != tiles_.end()) {
                        bytes -= oldest->second.bytes;
                        tiles_.erase(oldest);
                    }
                }
                auto &tile = tiles_[key];
                tile.used = frame_; // Visible tiles cannot evict each other during this frame.
                const Bounds clip{(x * tileW - gx) / scale, (y * tileH - gy) / scale,
                                  ((x + 1) * tileW + gx) / scale, ((y + 1) * tileH + gy) / scale};
                if (tile.modelToken != model->token) {
                    std::vector<Stroke> needed;
                    for (size_t i = 0; i < model->strokes.size(); ++i)
                        if (overlaps(model->boxes[i], clip))
                            needed.push_back(model->strokes[i]);
                    size_t common = 0;
                    while (common < needed.size() && common < tile.strokes.size() &&
                           same(needed[common], tile.strokes[common]))
                        ++common;
                    if (needed.empty()) {
                        bytes -= tile.bytes;
                        tile = {};
                        tile.used = frame_;
                    } else if (common != needed.size() || common != tile.strokes.size()) {
                        if (!tile.target) {
                            constexpr size_t cost = size_t(pixels) * pixels * 4;
                            while (bytes + cost > budget || tiles_.size() > 512) {
                                auto oldest = tiles_.end();
                                for (auto i = tiles_.begin(); i != tiles_.end(); ++i)
                                    if (i->second.used != frame_ &&
                                        (oldest == tiles_.end() || i->second.used < oldest->second.used))
                                        oldest = i;
                                if (oldest == tiles_.end())
                                    break;
                                bytes -= oldest->second.bytes;
                                tiles_.erase(oldest);
                            }
                            const auto size = D2D1::SizeF(float(tileW + 2 * gx), float(tileH + 2 * gy));
                            const auto pixelSize = D2D1::SizeU(pixels, pixels);
                            const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                 D2D1_ALPHA_MODE_PREMULTIPLIED);
                            if (bytes + cost <= budget && SUCCEEDED(destination->CreateCompatibleRenderTarget(
                                    &size, &pixelSize, &format, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                    &tile.target)) &&
                                SUCCEEDED(tile.target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &tile.brush))) {
                                tile.bytes = cost;
                                bytes += cost;
                            } else {
                                tile.target.Reset();
                                tile.brush.Reset();
                            }
                        }
                        if (tile.target) {
                            const bool append = tile.bitmap && common == tile.strokes.size();
                            tile.target->BeginDraw();
                            tile.target->SetTransform(D2D1::Matrix3x2F::Identity());
                            if (!append)
                                tile.target->Clear(D2D1::ColorF(0, 0, 0, 0));
                            for (size_t i = append ? common : 0; i < needed.size(); ++i)
                                draw(tile.target.Get(), tile.brush.Get(), needed[i], gx - x * tileW,
                                     gy - y * tileH, scale, &clip);
                            if (SUCCEEDED(tile.target->EndDraw()) && SUCCEEDED(tile.target->GetBitmap(&tile.bitmap))) {
                                tile.strokes = std::move(needed);
                                ++updates;
                            } else {
                                bytes -= tile.bytes;
                                tile = {};
                                tile.used = frame_;
                            }
                        }
                    }
                    if (needed.empty() || tile.bitmap)
                        tile.modelToken = model->token;
                } else
                    ++hits;
                const auto box = D2D1::RectF(float(left + x * tileW), float(top + y * tileH),
                                             float(left + (x + 1) * tileW), float(top + (y + 1) * tileH));
                // Split overlapping gutters at the same physical pixel boundary. Drawing
                // abutting bitmap quads at fractional coordinates causes alpha seams.
                const auto snapped = D2D1::RectF(float(std::round(box.left * dx / 96) * 96 / dx),
                                                 float(std::round(box.top * dy / 96) * 96 / dy),
                                                 float(std::round(box.right * dx / 96) * 96 / dx),
                                                 float(std::round(box.bottom * dy / 96) * 96 / dy));
                if (tile.bitmap) {
                    destination->PushAxisAlignedClip(snapped, D2D1_ANTIALIAS_MODE_ALIASED);
                    destination->DrawBitmap(tile.bitmap.Get(),
                                             D2D1::RectF(float(box.left - gx), float(box.top - gy),
                                                         float(box.right + gx), float(box.bottom + gy)),
                                             1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
                    destination->PopAxisAlignedClip();
                } else if (!tile.modelToken) {
                    // Allocation failure retains the same anti-aliased vector rendering.
                    destination->PushAxisAlignedClip(snapped, D2D1_ANTIALIAS_MODE_ALIASED);
                    for (size_t i = 0; i < model->strokes.size(); ++i)
                        if (overlaps(model->boxes[i], clip))
                            draw(destination, nullptr, model->strokes[i], left, top, scale, &clip);
                    destination->PopAxisAlignedClip();
                }
            }
    }
};
} // namespace pb
