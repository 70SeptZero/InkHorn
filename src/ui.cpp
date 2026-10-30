#include "ui.hpp"
#include "generated_icons.hpp"
#include <wincodec.h>

namespace pb {
using Microsoft::WRL::ComPtr;
Color tintSurface(Color c, double amount) {
    auto mix = [amount](uint8_t value) { return uint8_t(std::round(255 * (1 - amount) + value * amount)); };
    return {mix(c.r), mix(c.g), mix(c.b), 255};
}
RangeCursor::~RangeCursor() {
    if (cursor_) {
        if (GetCursor() == cursor_)
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        DestroyCursor(cursor_);
    }
}
namespace {
HCURSOR cursorFromBitmap(HBITMAP bitmap, int size, DWORD hotspotX, DWORD hotspotY) {
    const size_t maskStride = ((size + 15) / 16) * 2;
    std::vector<uint8_t> maskBits(maskStride * size);
    HBITMAP mask = CreateBitmap(size, size, 1, 1, maskBits.data());
    ICONINFO icon{};
    icon.xHotspot = hotspotX;
    icon.yHotspot = hotspotY;
    icon.hbmColor = bitmap;
    icon.hbmMask = mask;
    HCURSOR next = mask ? static_cast<HCURSOR>(CreateIconIndirect(&icon)) : nullptr;
    if (mask)
        DeleteObject(mask);
    return next;
}
} // namespace
HCURSOR RangeCursor::get(double diameterDip, double dpi, Color tint) {
    if (cursor_ && !eraser_ && diameter_ == diameterDip && dpi_ == dpi && color_.r == tint.r &&
        color_.g == tint.g && color_.b == tint.b)
        return cursor_;
    const Color c{uint8_t(tint.r * .45 + 128 * .55), uint8_t(tint.g * .45 + 128 * .55),
                  uint8_t(tint.b * .45 + 128 * .55), 255};
    const double radius = std::max(.5, diameterDip * dpi / 2);
    const int size = std::clamp(int(std::ceil(std::max(24 * dpi, radius * 2 + 12 * dpi))) | 1, 25, 513);
    const int center = size / 2;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *data = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (!bitmap || !data) {
        if (bitmap)
            DeleteObject(bitmap);
        return LoadCursorW(nullptr, IDC_ARROW);
    }
    auto *pixels = static_cast<uint32_t *>(data);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            const double dx = x - center, dy = y - center;
            const double alpha = .40 * std::clamp(radius + .5 - std::hypot(dx, dy), 0.0, 1.0);
            auto channel = [&](uint8_t v) { return uint32_t(std::round(v * alpha)); };
            pixels[y * size + x] = channel(c.b) | (channel(c.g) << 8) | (channel(c.r) << 16) |
                                   (uint32_t(std::round(alpha * 255)) << 24);
        }
    HCURSOR next = cursorFromBitmap(bitmap, size, DWORD(center), DWORD(center));
    DeleteObject(bitmap);
    if (next) {
        diameter_ = diameterDip;
        dpi_ = dpi;
        color_ = tint;
        eraser_ = false;
    }
    return replace(next);
}
HCURSOR RangeCursor::replace(HCURSOR next) {
    if (!next)
        return cursor_ ? cursor_ : LoadCursorW(nullptr, IDC_ARROW);
    if (cursor_) {
        if (GetCursor() == cursor_)
            SetCursor(next);
        DestroyCursor(cursor_);
    }
    cursor_ = next;
    return cursor_;
}
HCURSOR RangeCursor::eraser(double dpi, ID2D1Factory *factory, ToolIcons &icons) {
    if (cursor_ && eraser_ && dpi_ == dpi)
        return cursor_;
    const int size = int(std::ceil(32 * dpi));
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void *pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HDC dc = CreateCompatibleDC(nullptr);
    HGDIOBJ previous = dc && bitmap ? SelectObject(dc, bitmap) : nullptr;
    ComPtr<ID2D1DCRenderTarget> target;
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(96 * dpi),
        float(96 * dpi));
    HCURSOR next = nullptr;
    bool rendered = false;
    const RECT bounds{0, 0, size, size};
    if (bitmap && pixels && dc && SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) &&
        SUCCEEDED(target->BindDC(dc, &bounds))) {
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0, 0, 0, 0));
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const bool drawn = icons.draw(target.Get(), 13, D2D1::RectF(0, 0, 32, 32), Theme::text);
        rendered = SUCCEEDED(target->EndDraw()) && drawn;
    }
    target.Reset();
    if (previous)
        SelectObject(dc, previous);
    if (rendered)
        next = cursorFromBitmap(bitmap, size, DWORD(std::round(4 * dpi)), DWORD(std::round(4 * dpi)));
    if (dc)
        DeleteDC(dc);
    if (bitmap)
        DeleteObject(bitmap);
    if (next) {
        eraser_ = true;
        dpi_ = dpi;
        return replace(next);
    }
    return get(4, dpi, Theme::muted);
}
void windowGlyph(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, ID2D1StrokeStyle *style,
                 WindowGlyph glyph, D2D1_RECT_F box, Color c) {
    brush->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f));
    const float unit = (box.right - box.left) / 16.f;
    auto p = [&](float x, float y) { return D2D1::Point2F(box.left + x * unit, box.top + y * unit); };
    auto line = [&](float x, float y, float xx, float yy) {
        rt->DrawLine(p(x, y), p(xx, yy), brush, 1.35f * unit, style);
    };
    auto rectangle = [&](float x, float y, float xx, float yy) {
        rt->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(p(x, y).x, p(x, y).y, p(xx, yy).x, p(xx, yy).y), 1.5f * unit,
                              1.5f * unit),
            brush, 1.35f * unit, style);
    };
    switch (glyph) {
    case WindowGlyph::Minimize:
        line(3, 8, 13, 8);
        break;
    case WindowGlyph::Maximize:
        rectangle(3, 3, 13, 13);
        break;
    case WindowGlyph::Restore:
        // Only the exposed part of the rear window is visible behind the front one.
        line(6, 3, 12, 3);
        line(12, 3, 13, 4);
        line(13, 4, 13, 10);
        rectangle(3, 6, 10, 13);
        break;
    case WindowGlyph::Close:
        line(4, 4, 12, 12);
        line(12, 4, 4, 12);
        break;
    }
}
void toolGlyph(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *brush, ID2D1StrokeStyle *style, int tool,
               D2D1_RECT_F box, Color c) {
    brush->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f));
    const float unit = (box.right - box.left) / 24.f;
    auto p = [&](float x, float y) { return D2D1::Point2F(box.left + x * unit, box.top + y * unit); };
    auto line = [&](float x, float y, float xx, float yy, float w = 1.65f) {
        rt->DrawLine(p(x, y), p(xx, yy), brush, w * unit, style);
    };
    auto rect = [&](float x, float y, float xx, float yy, float radius = 2) {
        rt->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(p(x, y).x, p(x, y).y, p(xx, yy).x, p(xx, yy).y), radius * unit,
                              radius * unit),
            brush, 1.65f * unit, style);
    };
    switch (tool) {
    case 0:
        line(5, 3, 5, 20);
        line(5, 3, 18, 14);
        line(5, 20, 10, 15);
        line(10, 15, 18, 14);
        line(11, 16, 14, 22);
        break;
    case 1:
    case 2:
    case 4:
        line(6, 17, 16, 4, tool == 4 ? 5.f : 3.2f);
        line(5, 19, 3, 22);
        line(3, 22, 7, 21);
        line(5, 19, 7, 21);
        line(15, 5, 19, 8);
        line(10, 22, 21, 22, tool == 4 ? 3.f : 1.7f);
        break;
    case 3:
        line(4, 15, 13, 4);
        line(13, 4, 21, 11);
        line(21, 11, 13, 20);
        line(13, 20, 8, 20);
        line(8, 20, 4, 15);
        line(8, 10, 16, 17);
        line(13, 20, 22, 20);
        break;
    case 5:
        rect(4, 4, 20, 20, 3);
        break;
    case 6:
    case 7: {
        const bool redo = tool == 7;
        auto mirror = [&](float x) { return redo ? 24 - x : x; };
        line(mirror(9), 5, mirror(4), 10);
        line(mirror(4), 10, mirror(9), 15);
        line(mirror(4), 10, mirror(15), 10);
        line(mirror(15), 10, mirror(20), 14);
        line(mirror(20), 14, mirror(20), 18);
        line(mirror(20), 18, mirror(16), 21);
        break;
    }
    case 8:
        rect(4, 3, 20, 21, 2);
        rect(8, 3, 16, 9, 1);
        rect(8, 15, 16, 21, 1);
        break;
    // Shape choices in the popup.
    case 9:
        rt->DrawEllipse(D2D1::Ellipse(p(12, 12), 8 * unit, 8 * unit), brush, 1.65f * unit, style);
        break;
    case 10:
        line(4, 20, 20, 4);
        break;
    case 11:
        line(4, 20, 20, 4);
        line(11, 4, 20, 4);
        line(20, 4, 20, 13);
        break;
    }
}
void ToolIcons::load(HINSTANCE instance, int resource, Image &image, int size) {
    HICON icon =
        static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(resource), IMAGE_ICON, size, size, 0));
    if (!icon)
        return;
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr =
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr))
        hr = factory->CreateBitmapFromHICON(icon, &source);
    if (SUCCEEDED(hr))
        hr = factory->CreateFormatConverter(&converter);
    if (SUCCEEDED(hr))
        hr = converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                   nullptr, 0, WICBitmapPaletteTypeCustom);
    if (SUCCEEDED(hr))
        hr = converter->GetSize(&image.width, &image.height);
    if (SUCCEEDED(hr) && image.width <= 256 && image.height <= 256) {
        image.pixels.resize(size_t(image.width) * image.height * 4);
        hr = converter->CopyPixels(nullptr, image.width * 4, UINT(image.pixels.size()), image.pixels.data());
    } else
        hr = E_FAIL;
    if (FAILED(hr))
        image = {};
    DestroyIcon(icon);
}
void ToolIcons::load(HINSTANCE instance) {
    if (loaded_)
        return;
    loaded_ = true;
    load(instance, 100, logo_, 256);
    for (int i = 0; i < 14; ++i) {
        load(instance, i < 9 ? 300 + i : 321 + i, base_[i]);
        if (i == 1 || i == 2 || i == 4)
            load(instance, 310 + i, masks_[i]);
    }
}
void ToolIcons::release() {
    logo_.bitmap.Reset();
    for (auto *group : {&base_, &masks_})
        for (auto &image : *group)
            image.bitmap.Reset();
}
bool ToolIcons::drawImage(ID2D1RenderTarget *target, Image &image, D2D1_RECT_F box, const Color *color) {
    if (image.pixels.empty())
        return false;
    uint32_t key =
        color ? uint32_t(color->r) | (uint32_t(color->g) << 8) | (uint32_t(color->b) << 16) : 0xffffffff;
    if (!image.bitmap || image.tint != key) {
        auto pixels = image.pixels;
        if (color)
            for (size_t i = 0; i < pixels.size(); i += 4) {
                const auto a = pixels[i + 3];
                pixels[i] = uint8_t(unsigned(color->b) * a / 255);
                pixels[i + 1] = uint8_t(unsigned(color->g) * a / 255);
                pixels[i + 2] = uint8_t(unsigned(color->r) * a / 255);
            }
        image.bitmap.Reset();
        const auto props = D2D1::BitmapProperties(
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(target->CreateBitmap(D2D1::SizeU(image.width, image.height), pixels.data(),
                                        image.width * 4, props, &image.bitmap)))
            return false;
        image.tint = key;
    }
    // Preserve non-square artwork, centered in its logical icon cell.
    const float scale = std::min((box.right - box.left) / image.width, (box.bottom - box.top) / image.height);
    const float w = image.width * scale, h = image.height * scale;
    const float x = (box.left + box.right - w) / 2, y = (box.top + box.bottom - h) / 2;
    target->DrawBitmap(image.bitmap.Get(), D2D1::RectF(x, y, x + w, y + h), 1,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return true;
}
bool ToolIcons::draw(ID2D1RenderTarget *target, int tool, D2D1_RECT_F box, Color color) {
    if (tool < 0 || tool >= 14)
        return false;
    if (drawVector(target, tool, box, color))
        return true;
    const bool tinted = tool == 1 || tool == 2 || tool == 4;
    auto &mask = masks_[tool];
    if (tinted && !mask.pixels.empty()) {
        if (!drawImage(target, base_[tool], box, nullptr))
            return false;
        return drawImage(target, mask, box, &color);
    }
    return drawImage(target, base_[tool], box, tinted ? &color : nullptr);
}
bool ToolIcons::logo(ID2D1RenderTarget *target, D2D1_RECT_F box) {
    return drawImage(target, logo_, box, nullptr);
}
void ToolIcons::loadVectors(ID2D1RenderTarget *target) {
    if (vectorsLoaded_)
        return;
    vectorsLoaded_ = true;
    ComPtr<ID2D1Factory> factory;
    target->GetFactory(&factory);
    for (size_t icon = 0; icon < std::size(svgAssets); ++icon) {
        const auto &asset = svgAssets[icon];
        for (size_t part = 0; part < asset.count; ++part) {
            const auto &element = asset.elements[part];
            VectorPart result;
            factory->CreatePathGeometry(&result.path);
            ComPtr<ID2D1GeometrySink> sink;
            if (!result.path || FAILED(result.path->Open(&sink))) {
                vectors_[icon].clear();
                break;
            }
            sink->SetFillMode(D2D1_FILL_MODE_WINDING);
            bool figure = false;
            for (size_t i = 0; i < element.count; ++i) {
                const auto &command = element.commands[i];
                const auto *v = command.values;
                switch (command.op) {
                case 'M':
                    if (figure)
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                    sink->BeginFigure(D2D1::Point2F(v[0], v[1]), D2D1_FIGURE_BEGIN_FILLED);
                    figure = true;
                    break;
                case 'L':
                    sink->AddLine(D2D1::Point2F(v[0], v[1]));
                    break;
                case 'C':
                    sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(v[0], v[1]), D2D1::Point2F(v[2], v[3]),
                                                        D2D1::Point2F(v[4], v[5])));
                    break;
                case 'A':
                    if (v[0] <= 0 || v[1] <= 0)
                        sink->AddLine(D2D1::Point2F(v[5], v[6]));
                    else
                        sink->AddArc(D2D1::ArcSegment(
                            D2D1::Point2F(v[5], v[6]), D2D1::SizeF(v[0], v[1]), v[2],
                            v[4] ? D2D1_SWEEP_DIRECTION_CLOCKWISE : D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE,
                            v[3] ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                    break;
                case 'Z':
                    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
                    figure = false;
                    break;
                }
            }
            if (figure)
                sink->EndFigure(D2D1_FIGURE_END_OPEN);
            if (FAILED(sink->Close())) {
                vectors_[icon].clear();
                break;
            }
            auto properties = D2D1::StrokeStyleProperties();
            properties.startCap = properties.endCap =
                element.cap == 1 ? D2D1_CAP_STYLE_ROUND
                                 : (element.cap == 2 ? D2D1_CAP_STYLE_SQUARE : D2D1_CAP_STYLE_FLAT);
            properties.lineJoin = element.join == 1
                                      ? D2D1_LINE_JOIN_ROUND
                                      : (element.join == 2 ? D2D1_LINE_JOIN_BEVEL : D2D1_LINE_JOIN_MITER);
            factory->CreateStrokeStyle(properties, nullptr, 0, &result.style);
            vectors_[icon].push_back(std::move(result));
        }
    }
}
bool ToolIcons::drawVector(ID2D1RenderTarget *target, int icon, D2D1_RECT_F box, Color tint) {
    loadVectors(target);
    const auto &asset = svgAssets[icon];
    if (!asset.count || vectors_[icon].size() != asset.count)
        return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush)))
        return false;
    const float scale =
        std::min((box.right - box.left) / asset.view[2], (box.bottom - box.top) / asset.view[3]);
    const float x = (box.left + box.right - asset.view[2] * scale) / 2 - asset.view[0] * scale;
    const float y = (box.top + box.bottom - asset.view[3] * scale) / 2 - asset.view[1] * scale;
    D2D1_MATRIX_3X2_F previous;
    target->GetTransform(&previous);
    auto color = [](uint32_t value) {
        return D2D1::ColorF(float((value >> 24) & 255) / 255, float((value >> 16) & 255) / 255,
                            float((value >> 8) & 255) / 255, float(value & 255) / 255);
    };
    for (size_t i = 0; i < asset.count; ++i) {
        const auto &e = asset.elements[i];
        const auto &v = vectors_[icon][i];
        const auto matrix = D2D1::Matrix3x2F(e.transform[0], e.transform[1], e.transform[2], e.transform[3],
                                             e.transform[4], e.transform[5]);
        target->SetTransform(matrix * D2D1::Matrix3x2F(scale, 0, 0, scale, x, y) * previous);
        if (e.fill & 255) {
            brush->SetColor(e.tint ? D2D1::ColorF(tint.r / 255.f, tint.g / 255.f, tint.b / 255.f)
                                   : color(e.fill));
            target->FillGeometry(v.path.Get(), brush.Get());
        }
        if (e.stroke & 255) {
            brush->SetColor(color(e.stroke));
            target->DrawGeometry(v.path.Get(), brush.Get(), e.width, v.style.Get());
        }
    }
    target->SetTransform(previous);
    return true;
}
} // namespace pb
