#pragma once
#include "core.hpp"
#include <array>
#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

namespace pb {
namespace Theme {
inline constexpr Color surface{250, 251, 254, 255}, inset{242, 244, 249, 255}, text{56, 66, 85, 255},
    muted{125, 136, 154, 255}, accent{76, 120, 208, 255};
}
Color tintSurface(Color color, double amount = .12);
void toolGlyph(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush, ID2D1StrokeStyle *style, int tool,
               D2D1_RECT_F box, Color color);
enum class WindowGlyph { Minimize, Maximize, Restore, Close };
void windowGlyph(ID2D1RenderTarget *target, ID2D1SolidColorBrush *brush, ID2D1StrokeStyle *style,
                 WindowGlyph glyph, D2D1_RECT_F box, Color color);
class RangeCursor {
    HCURSOR cursor_{};
    double diameter_{-1}, dpi_{};
    Color color_{};
    bool eraser_{};
    HCURSOR replace(HCURSOR);

  public:
    ~RangeCursor();
    HCURSOR get(double diameterDip, double dpi, Color color);
    HCURSOR eraser(double dpi, ID2D1Factory *factory, class ToolIcons &icons);
};
class ToolIcons {
    struct Image {
        UINT width{}, height{};
        std::vector<uint8_t> pixels;
        Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
        uint32_t tint{0xffffffff};
    };
    struct VectorPart {
        Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
        Microsoft::WRL::ComPtr<ID2D1StrokeStyle> style;
    };
    std::array<Image, 14> base_;
    std::array<Image, 14> masks_;
    std::array<std::vector<VectorPart>, 14> vectors_;
    Image logo_;
    bool vectorsLoaded_{};
    void loadVectors(ID2D1RenderTarget *);
    bool drawVector(ID2D1RenderTarget *, int, D2D1_RECT_F, Color);
    bool loaded_{};
    static void load(HINSTANCE instance, int resource, Image &image, int size = 64);
    static bool drawImage(ID2D1RenderTarget *target, Image &image, D2D1_RECT_F box, const Color *tint);

  public:
    void load(HINSTANCE instance);
    void release();
    bool draw(ID2D1RenderTarget *target, int tool, D2D1_RECT_F box, Color color);
    bool logo(ID2D1RenderTarget *target, D2D1_RECT_F box);
};
} // namespace pb
