#include "app.hpp"
#include "app_paths.hpp"
#include "core.hpp"
#include "input_diagnostics.hpp"
#include "ink_tiles.hpp"
#include "interaction.hpp"
#include "pdf_engine.hpp"
#include "pen_input.hpp"
#include "runtime_trace.hpp"
#include "storage.hpp"
#include "ui.hpp"
#include <array>
#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <dwrite.h>
#include <fstream>
#include <optional>
#include <set>
#include <shellapi.h>
#include <span>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <windowsx.h>
#include <wrl/client.h>
#include <wincodec.h>

namespace pb {
using Microsoft::WRL::ComPtr;
constexpr UINT resultMessage = WM_APP + 12;
constexpr int pageId = 101, zoomId = 102;
struct CachedPage {
    Bitmap cpu;
    ComPtr<ID2D1Bitmap> gpu;
};
struct Session {
    uint64_t id{};
    std::filesystem::path path;
    std::vector<Size> pages;
    History history;
    double scale{1}, scroll{}, horizontal{};
    bool fit{true}, hidden{}, saving{}, loading{true}, closing{}, preserveRecovery{}, fromRecent{};
    int page{}, pending{-1};
    uint64_t pendingTicket{};
    std::map<int, CachedPage> cache;
    int64_t sourceStamp{};
    uintmax_t sourceSize{};
};
struct App {
    HINSTANCE instance{};
    HWND window{}, pageEdit{}, zoomEdit{}, panel{};
    HFONT font{};
    UINT fontDpi{};
    ComPtr<ID2D1Factory> factory;
    ComPtr<IDWriteFactory> textFactory;
    ComPtr<IDWriteTextFormat> format, smallFormat;
    ComPtr<IDWriteTextFormat> homeTitle;
    ComPtr<ID2D1HwndRenderTarget> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> roundStyle;
    ComPtr<ID2D1BitmapRenderTarget> inkTarget;
    ComPtr<ID2D1SolidColorBrush> inkBrush;
    ComPtr<ID2D1BitmapRenderTarget> liveTarget;
    ComPtr<ID2D1SolidColorBrush> liveBrush;
    struct InkKey {
        uint64_t document{}, revision{}, erasing{}, moving{};
        double scale{}, scroll{}, horizontal{}, width{}, height{}, dpi{}, header{}, paperLeft{}, paperRight{};
        bool operator==(const InkKey &) const = default;
    } inkKey{}, liveKey{};
    bool inkValid{}, liveValid{};
    uint64_t liveStroke{}, eraseRevision{};
    size_t livePoints{};
    Bounds liveBounds{};
    size_t geometryPointsBuilt{}, inkRebuilds{}, liveBoundsPointsVisited{};
    double lastPaintMilliseconds{};
    double nextInkPresent{};
    size_t paints{};
    D2D1_SIZE_U inkPixels{};
    bool inkUnavailable{};
    ToolIcons icons;
    RangeCursor rangeCursor;
    InputDiagnostics diagnostics;
    bool penTemporaryEraser{}, mouseContact{}, penPositionKnown{};
    POINT penScreen{};
    PenContact penContact;
    PenCoordinates penCoordinates;
    InputToolMode inputToolMode;
    size_t fieldTextWrites{};
    ComPtr<ID2D1DCRenderTarget> popupTarget;
    ComPtr<ID2D1SolidColorBrush> popupBrush;
    HDC popupDC{};
    HBITMAP popupBitmap{};
    HGDIOBJ popupOldBitmap{};
    HBRUSH fieldBackground{};
    HWND tooltips{};
    bool popupDragging{}, toolbarDraggable{}, toolbarMoved{};
    enum class PanelKind { Tool, Functions, Files, Color, About } panelKind{PanelKind::Tool};
    struct MenuRow {
        int command{};
        std::wstring label, detail;
        bool enabled{true}, checked{}, heading{};
    };
    std::vector<MenuRow> menuRows;
    double menuScroll{};
    int menuHover{-1}, menuFocus{-1};
    int customColorIndex{7}, colorDrag{};
    Hsv draftColor;
    std::wstring draftHex;
    bool hexFocused{}, hexSelected{};
    std::vector<std::filesystem::path> recent;
    double toolbarAnchorX{1}, toolbarAnchorY{.5}, toolbarGrabX{}, toolbarGrabY{};
    int hoverControl{-1};
    UINT activePointer{};
    ScrollMomentum momentum;
    WheelScroll wheel;
    InkStabilizer stabilizer;
    struct StrokeGeometry {
        std::vector<ComPtr<ID2D1PathGeometry>> chunks;
        std::vector<Bounds> boxes;
        ComPtr<ID2D1PathGeometry> tail;
        Bounds tailBox;
        size_t points{};
    };
    std::unordered_map<uint64_t, StrokeGeometry> shapes;
    InkTiles inkTiles;
    std::unique_ptr<RuntimeMonitor> runtimeMonitor;
    std::unique_ptr<PdfEngine> engine;
    std::unique_ptr<Storage> storage;
    std::vector<Session> sessions;
    int active{-1}, tool{};
    uint64_t nextDocument{1}, nextStroke{uint64_t(GetTickCount64()) << 16};
    uint64_t nextRenderTicket{1};
    std::array<Color, 8> palette{{{25, 25, 28, 255},
                                  {21, 101, 192, 255},
                                  {211, 47, 47, 255},
                                  {46, 125, 50, 255},
                                  {239, 108, 0, 255},
                                  {123, 31, 162, 255},
                                  {110, 110, 120, 255},
                                  {255, 235, 59, 255}}};
    std::array<Color, 6> colors{{{0, 0, 0, 255},
                                 {21, 101, 192, 255},
                                 {211, 47, 47, 255},
                                 {0, 0, 0, 255},
                                 {255, 235, 59, 72},
                                 {21, 101, 192, 255}}};
    static constexpr double eraserWidth = 4;
    std::array<double, 6> widths{{1.5, 1.5, 1.5, eraserWidth, 12, 1.5}};
    Kind shapeKind{Kind::Rectangle};
    enum class Gesture { None, Draw, Pan, Erase, Move, Toolbar } gesture{Gesture::None};
    Stroke workingStroke;
    std::vector<Stroke> erased;
    std::optional<uint64_t> selected;
    Point startPoint{}, lastScreen{};
    double startScroll{}, startHorizontal{}, panelScroll{};
    int gesturePage{-1};
    bool penNear{}, fullscreen{}, showHeader{true}, dark{}, leftHand{}, fitGuard{}, quitting{}, smoke{},
        horizontalPan{};
    WINDOWPLACEMENT placement{};
    std::wstring notice;
    std::wstring toast;
    bool toastWarning{};
    double toastUntil{};
    void showToast(const std::wstring &message, bool warning = false) {
        toast = message;
        toastWarning = warning;
        toastUntil = inputTime() + 3;
        invalidate();
    }
    void expireToast(double now) {
        if (!toast.empty() && now >= toastUntil) {
            toast.clear();
            invalidate();
        }
    }
    std::filesystem::path diagnosticDirectory() const {
        return portableDataDirectory() / L"diagnostics";
    }
    void toggleDiagnostics() {
        if (diagnostics.active()) {
            diagnostics.stop();
            showToast(L"输入诊断已结束，日志已保存");
        } else {
            const auto directory = diagnosticDirectory();
            if (!directory.empty() && diagnostics.start(directory))
                showToast(L"输入诊断已开启，请测试侧键");
            else
                showToast(L"无法创建输入诊断日志");
        }
    }
    void openDiagnosticDirectory() {
        const auto directory = diagnosticDirectory();
        std::error_code ec;
        if (directory.empty() || !std::filesystem::is_directory(directory, ec)) {
            showToast(L"请先开启一次笔输入诊断");
            return;
        }
        ShellExecuteW(window, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    uint64_t closePrompt{};
    int closePromptFocus{2}, closePromptHover{-1};
    D2D1_RECT_F closeCard() const {
        const float x = float((width() - 500) / 2), y = float((height() - 224) / 2);
        return D2D1::RectF(x, y, x + 500, y + 224);
    }
    D2D1_RECT_F closeButton(int index) const {
        const auto card = closeCard();
        const float x = card.left + 24 + index * 152;
        return D2D1::RectF(x, card.bottom - 66, x + 140, card.bottom - 24);
    }
    int closeButtonHit(double x, double y) const {
        for (int i = 0; i < 3; ++i) {
            auto r = closeButton(i);
            if (x >= r.left && x <= r.right && y >= r.top && y <= r.bottom)
                return i;
        }
        return -1;
    }
    void showClosePrompt(uint64_t id) {
        closePanel();
        stopInertia();
        closePrompt = id;
        closePromptFocus = 2;
        closePromptHover = -1;
        if (tooltips)
            SendMessageW(tooltips, TTM_POP, 0, 0);
        ShowWindow(pageEdit, SW_HIDE);
        ShowWindow(zoomEdit, SW_HIDE);
        SetFocus(window);
        invalidate();
    }
    void answerClosePrompt(int answer) {
        const uint64_t id = closePrompt;
        closePrompt = 0;
        auto found =
            std::find_if(sessions.begin(), sessions.end(), [&](const Session &s) { return s.id == id; });
        if (found == sessions.end()) {
            quitting = false;
            layout();
            return;
        }
        const int index = int(found - sessions.begin());
        if (answer == 0) {
            quitting = false;
            layout();
            return;
        }
        if (answer == 1) {
            closeDocument(index, true);
            if (quitting)
                closeWindow();
        } else {
            found->closing = true;
            layout();
            save();
        }
        invalidate();
    }
    void paintClosePrompt() {
        if (!closePrompt)
            return;
        rect(D2D1::RectF(0, 0, float(width()), float(height())), {33, 43, 62, 75});
        const auto card = closeCard();
        for (int spread = 7; spread >= 1; --spread)
            rect(D2D1::RectF(card.left - spread, card.top - spread + 4, card.right + spread,
                             card.bottom + spread + 4),
                 {20, 30, 50, 5}, 24.f + spread);
        rect(card, {255, 255, 255, 255}, 24);
        text(L"保存笔记后关闭？", D2D1::RectF(card.left + 26, card.top + 25, card.right - 26, card.top + 54),
             {49, 60, 80, 255});
        auto s = std::find_if(sessions.begin(), sessions.end(),
                              [&](const Session &item) { return item.id == closePrompt; });
        text(s == sessions.end() ? L"当前文档" : s->path.filename().wstring(),
             D2D1::RectF(card.left + 26, card.top + 64, card.right - 26, card.top + 90), {91, 110, 145, 255});
        text(L"保存后关闭文件；选择“不保存”将放弃本次未保存的修改。",
             D2D1::RectF(card.left + 26, card.top + 104, card.right - 22, card.top + 130),
             {122, 132, 150, 255}, true);
        const wchar_t *names[] = {L"取消", L"不保存", L"保存并关闭"};
        for (int i = 0; i < 3; ++i) {
            const auto button = closeButton(i);
            const bool hovered = i == closePromptHover;
            rect(button,
                 i == 2 ? (hovered ? Color{62, 107, 199, 255} : Color{73, 119, 213, 255})
                        : (hovered ? Color{231, 236, 245, 255} : Color{243, 246, 250, 255}),
                 13);
            if (i == closePromptFocus) {
                color(i == 2 ? Color{38, 83, 172, 255} : Color{156, 176, 212, 255});
                target->DrawRoundedRectangle(D2D1::RoundedRect(button, 13, 13), brush.Get(), 1.5f);
            }
            const float textWidth = i == 2 ? 70.f : (i == 1 ? 42.f : 28.f);
            const float x = (button.left + button.right - textWidth) / 2;
            text(names[i], D2D1::RectF(x, button.top + 11, button.right - 8, button.bottom - 5),
                 i == 2 ? Color{255, 255, 255, 255} : Color{86, 99, 122, 255});
        }
    }
    double dpi() const {
        return GetDpiForWindow(window) / 96.0;
    }
    Session *doc() {
        return active >= 0 && active < int(sessions.size()) ? &sessions[active] : nullptr;
    }
    double width() const {
        RECT r{};
        GetClientRect(window, &r);
        return (r.right - r.left) / dpi();
    }
    double height() const {
        RECT r{};
        GetClientRect(window, &r);
        return (r.bottom - r.top) / dpi();
    }
    double header() const {
        return !fullscreen || showHeader ? 40 : 0;
    }
    double bodyHeight() const {
        return height() - header();
    }
    double fileEdge() const {
        return std::min(284.0, navigation().left - 16.0);
    }
    double fileLeft() const {
        return 50;
    }
    size_t homeRecentCount() const {
        const auto card = homeCard();
        return std::min(recent.size(), size_t(std::clamp(int((card.bottom - card.top - 70) / 58), 1, 5)));
    }
    D2D1_RECT_F homeCard() const {
        const double w = std::min(860.0, width() - 156);
        const double h = std::min(376.0, std::max(280.0, bodyHeight() - 64));
        return D2D1::RectF(float((width() - w) / 2), float(header() + (bodyHeight() - h) / 2),
                           float((width() + w) / 2), float(header() + (bodyHeight() + h) / 2));
    }
    D2D1_RECT_F homeLeft() const {
        auto card = homeCard();
        card.right = card.left + float(std::clamp((card.right - card.left) * .34, 168.0, 256.0));
        return card;
    }
    D2D1_RECT_F homeRight() const {
        auto card = homeCard();
        card.left = homeLeft().right + 48;
        return card;
    }
    D2D1_RECT_F homeOpenButton() const {
        const auto card = homeLeft();
        const float center = (card.left + card.right) / 2, y = (card.top + card.bottom) / 2 + 14;
        return D2D1::RectF(center - 84, y, center + 84, y + 44);
    }
    D2D1_RECT_F homeRecentRow(size_t index) const {
        const auto card = homeRight();
        const float top = (card.top + card.bottom - float(70 + homeRecentCount() * 58)) / 2;
        const float y = top + 70 + float(index * 58);
        return D2D1::RectF(card.left, y, card.right, y + 52);
    }
    int homeHit(double x, double y) const {
        auto inside = [&](D2D1_RECT_F r) {
            return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
        };
        if (inside(homeOpenButton()))
            return 30;
        for (size_t i = 0; i < homeRecentCount(); ++i)
            if (inside(homeRecentRow(i)))
                return 40 + int(i);
        return -1;
    }
    void rememberRecent(const std::filesystem::path &path) {
        if (smoke)
            return;
        auto key = [](const std::filesystem::path &p) {
            auto value = p.lexically_normal().wstring();
            std::transform(value.begin(), value.end(), value.begin(),
                           [](wchar_t c) { return wchar_t(towlower(c)); });
            return value;
        };
        const auto identity = key(path);
        std::erase_if(recent, [&](const auto &item) { return key(item) == identity; });
        recent.insert(recent.begin(), path);
        if (recent.size() > 20)
            recent.resize(20);
        if (storage)
            storage->writeRecent(recent);
    }
    void forgetRecent(const std::filesystem::path &path) {
        auto key = [](const std::filesystem::path &p) {
            auto value = p.lexically_normal().wstring();
            std::transform(value.begin(), value.end(), value.begin(),
                           [](wchar_t c) { return wchar_t(towlower(c)); });
            return value;
        };
        const auto identity = key(path);
        std::erase_if(recent, [&](const auto &item) { return key(item) == identity; });
        if (storage)
            storage->writeRecent(recent);
        hoverControl = -1;
        showToast(L"文件不存在", true);
    }
    static bool missingFile(const std::filesystem::path &path) {
        std::error_code ec;
        const auto status = std::filesystem::status(path, ec);
        return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory ||
               (!ec && !std::filesystem::is_regular_file(status));
    }
    void openRecent(size_t index) {
        if (index >= recent.size())
            return;
        const auto path = recent[index];
        if (missingFile(path)) {
            forgetRecent(path);
            return;
        }
        open(path, true);
    }
    void centeredText(const std::wstring &value, D2D1_RECT_F box, Color c, bool title = false,
                      bool vertically = false) {
        auto *textFormat = title ? homeTitle.Get() : format.Get();
        const auto alignment = textFormat->GetTextAlignment();
        const auto paragraph = textFormat->GetParagraphAlignment();
        textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        if (vertically)
            textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        color(c);
        target->DrawTextW(value.c_str(), UINT32(value.size()), textFormat, box, brush.Get(),
                          D2D1_DRAW_TEXT_OPTIONS_CLIP);
        textFormat->SetTextAlignment(alignment);
        textFormat->SetParagraphAlignment(paragraph);
    }
    void paintHome() {
        const auto card = homeCard();
        const auto left = homeLeft(), right = homeRight();
        const float center = (left.left + left.right) / 2, cy = (card.top + card.bottom) / 2;
        icons.logo(target.Get(), D2D1::RectF(center - 38, cy - 108, center + 38, cy - 32));
        centeredText(L"InkHorn", D2D1::RectF(left.left, cy - 23, left.right, cy + 7), Theme::text, true);
        rect(D2D1::RectF(left.right + 23, cy - 96, left.right + 24, cy + 96), {225, 230, 240, 255}, .5f);
        if (recent.empty()) {
            text(L"开始阅读与书写", D2D1::RectF(right.left, cy - 70, right.right, cy - 38), Theme::text);
            text(L"点击左侧按钮打开 PDF", D2D1::RectF(right.left, cy - 22, right.right, cy + 4),
                 Theme::muted);
            text(L"也可以将 PDF 拖入窗口", D2D1::RectF(right.left, cy + 13, right.right, cy + 39),
                 Theme::muted);
            text(L"页面上书写，两侧空白处滑动", D2D1::RectF(right.left, cy + 52, right.right, cy + 78),
                 Theme::muted, true);
        } else {
            const float top = (right.top + right.bottom - float(70 + homeRecentCount() * 58)) / 2;
            text(L"最近打开", D2D1::RectF(right.left + 10, top + 20, right.right, top + 50), Theme::text);
            for (size_t i = 0; i < homeRecentCount(); ++i) {
                const auto row = homeRecentRow(i);
                rect(row, hoverControl == 40 + int(i) ? Color{231, 237, 247, 255} : Theme::surface, 14);
                rect(D2D1::RectF(row.left + 15, row.top + 14, row.left + 32, row.top + 37),
                     {232, 115, 126, 255}, 4);
                text(recent[i].filename().wstring(),
                     D2D1::RectF(row.left + 46, row.top + 8, row.right - 18, row.top + 29), Theme::text);
                text(recent[i].parent_path().wstring(),
                     D2D1::RectF(row.left + 46, row.top + 29, row.right - 18, row.bottom - 3), Theme::muted,
                     true);
            }
        }
        const auto button = homeOpenButton();
        if (hoverControl == 30)
            rect(button, Theme::accent, 22);
        else {
            color(Theme::accent);
            target->DrawRoundedRectangle(D2D1::RoundedRect(button, 22, 22), brush.Get(), 1.5f);
        }
        centeredText(L"打开 PDF", button, hoverControl == 30 ? Color{255, 255, 255, 255} : Theme::accent,
                     false, true);
    }
    D2D1_RECT_F navigation() const {
        const float x = float((width() - 236) / 2);
        return D2D1::RectF(x, 5, x + 236, 35);
    }
    void paintTransientUI() {
        if (!toast.empty() && inputTime() < toastUntil) {
            const float w = float(std::min(width() - 80, std::max(240.0, toast.size() * 14.0 + 36)));
            const float x = float((width() - w) / 2), y = float(header() + 14);
            rect(D2D1::RectF(x - 3, y + 2, x + w + 3, y + 38), {45, 61, 90, 18}, 16);
            const auto box = D2D1::RectF(x, y, x + w, y + 36);
            const Color tone = toastWarning ? Color{194, 133, 143, 255} : Theme::text;
            rect(box, toastWarning ? Color{255, 251, 252, 255} : Theme::surface, 14);
            if (toastWarning) {
                color(tone);
                target->DrawRoundedRectangle(D2D1::RoundedRect(box, 14, 14), brush.Get(), 1);
            }
            centeredText(toast, D2D1::RectF(x + 14, y, x + w - 14, y + 36), tone, false, true);
        }
        if (diagnostics.active()) {
            const float w = float(std::min(860.0, width() - 160)), x = float((width() - w) / 2);
            const float y = float(height() - 90);
            rect(D2D1::RectF(x, y, x + w, y + 58), {238, 243, 253, 245}, 14);
            text(L"笔输入诊断 · Ctrl+Shift+D 结束 · Ctrl+Shift+L 日志目录",
                 D2D1::RectF(x + 16, y + 8, x + w - 16, y + 29), Theme::accent, true);
            text(diagnostics.last(), D2D1::RectF(x + 16, y + 31, x + w - 16, y + 53), Theme::text, true);
        }
    }
    double toolbarContentHeight() const {
        return toolbarDraggable ? 464 : 448;
    }
    int toolbarGlyph(int button) const {
        if (button != 5)
            return button;
        switch (shapeKind) {
        case Kind::Ellipse:
            return 9;
        case Kind::Line:
            return 10;
        case Kind::Arrow:
            return 11;
        default:
            return 5;
        }
    }
    static bool configurableTool(int button) {
        return button > 0 && button < 6 && button != 3;
    }
    double toolsTop() const {
        return toolbarDraggable ? 24 : 8;
    }
    double paperLeft() const {
        return leftHand ? 116 : 32;
    }
    double paperRight() const {
        return width() - (leftHand ? 32 : 116);
    }
    D2D1_RECT_F toolbar() const {
        float w = 60, h = float(std::min(toolbarContentHeight(), std::max(140.0, bodyHeight() - 32)));
        float x = leftHand ? 16 : float(width() - 72), y = float(header() + (bodyHeight() - h) / 2);
        if (toolbarMoved) {
            x = float(12 + toolbarAnchorX * std::max(0.0, width() - w - 24));
            y = float(header() + 12 + toolbarAnchorY * std::max(0.0, bodyHeight() - h - 24));
        }
        return D2D1::RectF(x, y, x + w, y + h);
    }
    int toolHit(double x, double y) const {
        auto r = toolbar();
        if (x < r.left || x > r.right || y < r.top || y > r.bottom)
            return -1;
        double yy = y - r.top - toolsTop() + panelScroll;
        for (int i = 0; i < 9; ++i) {
            double top = i * 46 + (i >= 6 ? 12 : 0);
            if (yy >= top && yy < top + 42)
                return i;
        }
        return -2;
    }
    bool toolbarContains(double x, double y) const {
        auto r = toolbar();
        return x >= r.left && x <= r.right && y >= r.top && y <= r.bottom;
    }
    bool toolbarHandle(double x, double y) const {
        auto r = toolbar();
        return toolbarDraggable && toolbarContains(x, y) && y < r.top + 22;
    }
    D2D1::Matrix3x2F toolbarIconTransform() const {
        const auto bar = toolbar();
        return leftHand ? D2D1::Matrix3x2F(-1, 0, 0, 1, bar.left + bar.right, 0)
                        : D2D1::Matrix3x2F::Identity();
    }
    int cursorTool(double x, double y) {
        auto *s = doc();
        if (!s || s->loading || s->hidden || s->closing || closePrompt || y < header() ||
            toolbarContains(x, y) || gesture == Gesture::Pan || gesture == Gesture::Toolbar ||
            pageAt(x, y) < 0)
            return -1;
        const int effective = gesture == Gesture::Erase || (penNear && penTemporaryEraser) ? 3 : tool;
        return effective >= 1 && effective <= 4 ? effective : -1;
    }
    double cursorDiameter(int effective) {
        if (effective == 3)
            return eraserWidth;
        const double width =
            effective == 1 || effective == 2 ? pressureWidth(1, widths[effective], true) : widths[effective];
        return width * doc()->scale;
    }
    void updateCursorAt(double x, double y) {
        if (smoke)
            return;
        if (toolbarHandle(x, y) || gesture == Gesture::Toolbar) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return;
        }
        const int effective = cursorTool(x, y);
        if (effective >= 0) {
            SetCursor(effective == 3 ? rangeCursor.eraser(dpi(), factory.Get(), icons)
                                     : rangeCursor.get(cursorDiameter(effective), dpi(), colors[effective]));
        } else
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    void refreshCursor() {
        POINT p{};
        if (penPositionKnown && (penNear || penContact.active()))
            p = penScreen;
        else if (!GetCursorPos(&p))
            return;
        ScreenToClient(window, &p);
        const double x = p.x / dpi(), y = p.y / dpi();
        if (x >= 0 && x < width() && y >= 0 && y < height())
            updateCursorAt(x, y);
    }
    void inputToolChanged() {
        selected.reset();
        if (panel && (panelKind == PanelKind::Tool || panelKind == PanelKind::Color))
            closePanel();
        invalidate();
    }
    void usePenInput() {
        if (inputToolMode.pen(tool))
            inputToolChanged();
    }
    bool mouseInput(UINT message) {
        if (promotedPointerMouse(GetMessageExtraInfo()) || penContact.active())
            return false;
        if (inputToolMode.penActive()) {
            INPUT_MESSAGE_SOURCE source{};
            const bool known = GetCurrentInputMessageSource(&source) != FALSE;
            if (known && (source.deviceType == IMDT_PEN || source.deviceType == IMDT_TOUCH))
                return false;
            // Layout/capture changes can synthesize mouse moves without a mouse
            // actually moving. Do not let those undo a pen-hover tool switch.
            if ((message == WM_MOUSEMOVE || message == WM_NCMOUSEMOVE) &&
                (!known || source.deviceType == IMDT_UNAVAILABLE))
                return false;
        }
        const bool changed = inputToolMode.mouse(tool);
        penNear = penTemporaryEraser = penPositionKnown = false;
        if (changed)
            inputToolChanged();
        return true;
    }
    void stopInertia() {
        momentum.stop();
        wheel.stop();
        if (window)
            KillTimer(window, 2);
    }
    void beginPan(double x, double y, bool horizontal) {
        stopInertia();
        if (closePrompt)
            return;
        auto *s = doc();
        if (!s || s->loading)
            return;
        gesture = Gesture::Pan;
        lastScreen = {x, y, 0};
        startScroll = s->scroll;
        startHorizontal = s->horizontal;
        horizontalPan = horizontal;
        momentum.begin(s->scroll, inputTime());
    }
    void animateScroll(double time = inputTime()) {
        auto *s = doc();
        if (!s || s->loading || gesture != Gesture::None || (!momentum.active() && !wheel.active())) {
            stopInertia();
            return;
        }
        const double delta = wheel.active() ? wheel.tick(time) : momentum.tick(time);
        const double previous = s->scroll;
        s->scroll += delta;
        clampScroll();
        if (std::abs(s->scroll - previous - delta) > .01 || (!momentum.active() && !wheel.active())) {
            stopInertia();
            rememberView();
        }
        if (s->scroll != previous) {
            requestPage();
            invalidate();
        }
    }
    void updateTooltips() {
        if (!tooltips)
            return;
        const wchar_t *labels[] = {L"指针选择 · 移动 / 删除笔记",
                                   L"笔 1 · 再次点击设置",
                                   L"笔 2 · 再次点击设置",
                                   L"整笔橡皮 · 擦除整条笔迹",
                                   L"荧光笔 · 再次点击设置",
                                   L"形状 · 再次点击设置",
                                   L"撤销 · Ctrl+Z",
                                   L"重做 · Ctrl+Y",
                                   L"保存 · Ctrl+S"};
        const auto tb = toolbar();
        for (int i = 0; i < 9; ++i) {
            const double y = tb.top + toolsTop() + i * 46 + (i >= 6 ? 12 : 0) - panelScroll;
            const double top = std::max(double(tb.top) + toolsTop() - 2, y);
            const double bottom = std::min(double(tb.bottom) - 6, y + 42);
            TOOLINFOW info{};
            info.cbSize = sizeof(info);
            info.hwnd = window;
            info.uId = UINT_PTR(i + 1);
            info.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
            if (bottom > top)
                info.rect = {LONG(tb.left * dpi()), LONG(top * dpi()), LONG(tb.right * dpi()),
                             LONG(bottom * dpi())};
            info.lpszText = const_cast<wchar_t *>(labels[i]);
            if (!SendMessageW(tooltips, TTM_GETTOOLINFOW, 0, reinterpret_cast<LPARAM>(&info))) {
                info.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
                info.lpszText = const_cast<wchar_t *>(labels[i]);
                SendMessageW(tooltips, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
            } else {
                if (bottom > top)
                    info.rect = {LONG(tb.left * dpi()), LONG(top * dpi()), LONG(tb.right * dpi()),
                                 LONG(bottom * dpi())};
                else
                    info.rect = {};
                SendMessageW(tooltips, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&info));
            }
        }
    }
    void hover(double x, double y) {
        int next = toolHit(x, y);
        if (!doc() && next < 0)
            next = homeHit(x, y);
        if (y < header()) {
            if (x < 44)
                next = 24;
            else if (x >= fileLeft() && x < fileEdge())
                next = 20;
            else if (x > width() - 138)
                next = 21 + std::min(2, int((x - width() + 138) / 46));
        }
        if (next != hoverControl) {
            hoverControl = next;
            invalidate();
        }
    }
    static double pointerSampleTime(const POINTER_INFO &info) {
        LARGE_INTEGER frequency{}, now{};
        if (info.PerformanceCount && QueryPerformanceFrequency(&frequency) && QueryPerformanceCounter(&now))
            return inputTime() -
                   double(now.QuadPart - LONGLONG(info.PerformanceCount)) / double(frequency.QuadPart);
        return info.dwTime ? inputTime() - double(DWORD(GetTickCount() - info.dwTime)) / 1000 : inputTime();
    }
    void invalidate() {
        InvalidateRect(window, nullptr, FALSE);
    }
    void presentInkIfDue(double now = inputTime()) {
        // WM_PAINT has lower priority than pointer input. A long input queue must
        // not hide wet ink until the queue drains. Render at a bounded frame rate.
        if (now >= nextInkPresent && (smoke || GetUpdateRect(window, nullptr, FALSE))) {
            if (smoke)
                paint(); // Hidden test windows have no visible update region.
            else
                UpdateWindow(window);
        }
    }
    void recoverLater(Session &session) {
        RuntimeStage stage(RuntimeThread::Ui, "ui.recovery-queue");
        if (!storage || session.loading || session.preserveRecovery || smoke)
            return;
        if (session.history.dirty())
            storage->write(
                {session.path, session.sourceStamp, session.sourceSize, session.history.current()});
        else
            storage->erase(session.path);
    }
    void rememberView() {
        auto *s = doc();
        if (storage && s && !s->loading && !smoke)
            storage->writeView(s->path, {s->page, s->scale, s->scroll, s->horizontal, s->fit, s->hidden});
    }
    void persistSettings() {
        if (!storage || smoke)
            return;
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "PBSET1 " << dark << ' ' << leftHand << " 0 " << int(shapeKind);
        for (double w : widths)
            out << ' ' << w;
        for (auto c : colors)
            out << ' ' << int(c.r) << ' ' << int(c.g) << ' ' << int(c.b) << ' ' << int(c.a);
        for (auto c : palette)
            out << ' ' << int(c.r) << ' ' << int(c.g) << ' ' << int(c.b) << ' ' << int(c.a);
        out << " PBUI2 " << toolbarDraggable << ' ' << toolbarMoved << ' ' << toolbarAnchorX << ' '
            << toolbarAnchorY;
        storage->writeSettings(out.str());
    }
    void loadSettings() {
        if (!storage)
            return;
        std::istringstream in(storage->readSettings());
        in.imbue(std::locale::classic());
        std::string magic;
        bool d, l;
        int t, k;
        auto sizes = widths;
        auto cs = colors;
        auto ps = palette;
        if (!(in >> magic >> d >> l >> t >> k) || magic != "PBSET1" || t < 0 || t > 5 || k < 2 || k > 5)
            return;
        for (auto &w : sizes)
            if (!(in >> w) || !std::isfinite(w) || w <= 0 || w > 80)
                return;
        for (auto *group : {&cs[0], &ps[0]}) {
            int count = group == &cs[0] ? 6 : 8;
            for (int i = 0; i < count; ++i) {
                int r, g, b, a;
                if (!(in >> r >> g >> b >> a) || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ||
                    a < 0 || a > 255)
                    return;
                group[i] = {uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)};
            }
        }
        dark = d;
        leftHand = l;
        tool = 0; // Every launch starts with the pointer, including legacy saved settings.
        shapeKind = static_cast<Kind>(k);
        widths = sizes;
        widths[3] = eraserWidth; // Old saved sizes do not enlarge whole-stroke hit testing.
        colors = cs;
        palette = ps;
        std::string extension;
        bool drag, moved;
        double x, y;
        if (in >> extension >> drag >> moved >> x >> y; extension == "PBUI2" && in && std::isfinite(x) &&
                                                        std::isfinite(y) && x >= 0 && x <= 1 && y >= 0 &&
                                                        y <= 1) {
            toolbarDraggable = drag;
            toolbarMoved = moved;
            toolbarAnchorX = x;
            toolbarAnchorY = y;
        }
    }
    void color(Color c) {
        brush->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
    }
    void rect(D2D1_RECT_F r, Color c, float radius = 0) {
        color(c);
        if (radius)
            target->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush.Get());
        else
            target->FillRectangle(r, brush.Get());
    }
    void text(const std::wstring &t, D2D1_RECT_F r, Color c = {45, 48, 55, 255}, bool tiny = false) {
        color(c);
        target->DrawTextW(t.c_str(), UINT32(t.size()), tiny ? smallFormat.Get() : format.Get(), r,
                          brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void releaseGraphics() {
        liveBrush.Reset();
        liveTarget.Reset();
        liveValid = inkValid = false;
        inkBrush.Reset();
        inkTarget.Reset();
        inkPixels = {};
        inkUnavailable = false;
        icons.release();
        target.Reset();
        brush.Reset();
        shapes.clear();
        inkTiles.clear();
        for (auto &s : sessions)
            for (auto &[i, c] : s.cache) {
                (void)i;
                c.gpu.Reset();
            }
    }
    bool graphics() {
        if (target)
            return true;
        RECT r{};
        GetClientRect(window, &r);
        auto hr = factory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(r.right, r.bottom),
                                             D2D1_PRESENT_OPTIONS_IMMEDIATELY),
            &target);
        if (FAILED(hr))
            return false;
        target->SetDpi(float(GetDpiForWindow(window)), float(GetDpiForWindow(window)));
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
        icons.load(instance);
        if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &brush))) {
            releaseGraphics();
            return false;
        }
        return true;
    }
    bool inkGraphics() {
        if (inkUnavailable)
            return false;
        // Supersample only the visible transparent ink overlay, never every PDF page.
        const double physical = width() * height() * dpi() * dpi();
        if (physical <= 0 || physical > 16 * 1024 * 1024)
            return false;
        const double quality = std::min(2.0, std::sqrt((16.0 * 1024 * 1024) / physical));
        const auto pixels = D2D1::SizeU(UINT32(std::ceil(width() * dpi() * quality)),
                                        UINT32(std::ceil(height() * dpi() * quality)));
        if (inkTarget && inkPixels.width == pixels.width && inkPixels.height == pixels.height)
            return true;
        inkBrush.Reset();
        inkTarget.Reset();
        const auto size = D2D1::SizeF(float(width()), float(height()));
        inkValid = liveValid = false;
        liveBrush.Reset();
        liveTarget.Reset();
        const auto pixelFormat = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
        if (FAILED(target->CreateCompatibleRenderTarget(
                &size, &pixels, &pixelFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &inkTarget))) {
            inkUnavailable = true;
            return false;
        }
        inkPixels = pixels;
        inkTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (FAILED(inkTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &inkBrush))) {
            inkTarget.Reset();
            inkUnavailable = true;
            return false;
        }
        return true;
    }
    ComPtr<ID2D1PathGeometry> pathGeometry(const Stroke &s, size_t begin, size_t end) {
        geometryPointsBuilt += end - begin;
        ComPtr<ID2D1PathGeometry> g;
        factory->CreatePathGeometry(&g);
        ComPtr<ID2D1GeometrySink> sink;
        if (!g || FAILED(g->Open(&sink)))
            return {};
        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        // Analytic round nibs avoid both polygonal caps at high zoom and dozens
        // of allocated contour vertices per input point. One winding fill unions
        // the nibs and their exterior tangents before applying antialiasing.
        for (size_t i = begin; i < end; ++i) {
            const auto p = s.points[i];
            const float x = float(p.x), y = float(p.y), r = float(std::max(.03, p.width / 2));
            const float k = r * .5522847498307936f;
            sink->BeginFigure(D2D1::Point2F(x + r, y), D2D1_FIGURE_BEGIN_FILLED);
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x + r, y + k),
                                               D2D1::Point2F(x + k, y + r), D2D1::Point2F(x, y + r)));
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x - k, y + r),
                                               D2D1::Point2F(x - r, y + k), D2D1::Point2F(x - r, y)));
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x - r, y - k),
                                               D2D1::Point2F(x - k, y - r), D2D1::Point2F(x, y - r)));
            sink->AddBezier(D2D1::BezierSegment(D2D1::Point2F(x + k, y - r),
                                               D2D1::Point2F(x + r, y - k), D2D1::Point2F(x + r, y)));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            std::array<Point, 4> corners;
            if (i > begin && nibTangents(s.points[i - 1], p, corners)) {
                sink->BeginFigure(D2D1::Point2F(float(corners[0].x), float(corners[0].y)),
                                  D2D1_FIGURE_BEGIN_FILLED);
                for (size_t j = 1; j < corners.size(); ++j)
                    sink->AddLine(D2D1::Point2F(float(corners[j].x), float(corners[j].y)));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
        }
        if (FAILED(sink->Close()))
            return {};
        return g;
    }
    Bounds partBounds(const Stroke &s, size_t begin, size_t end) {
        Bounds box{1e30, 1e30, -1e30, -1e30};
        for (size_t i = begin; i < end; ++i) {
            const auto p = s.points[i];
            const double r = std::max(.03, p.width / 2);
            box.l = std::min(box.l, p.x - r);
            box.r = std::max(box.r, p.x + r);
            box.t = std::min(box.t, p.y - r);
            box.b = std::max(box.b, p.y + r);
        }
        return box;
    }
    void inkPath(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b, const Stroke &s, const Bounds *clip = nullptr) {
        // Fixed-size geometry avoids tessellating thousands of intersecting disc contours
        // as one ever-growing path. Immutable chunks retain their Direct2D tessellation.
        constexpr size_t step = 24;
        auto &cache = shapes[s.id];
        const size_t complete = (s.points.size() - 1) / step;
        if (cache.points > s.points.size())
            cache = {};
        while (cache.chunks.size() < complete) {
            const size_t start = cache.chunks.size() * step;
            cache.chunks.push_back(pathGeometry(s, start, start + step + 1));
            cache.boxes.push_back(partBounds(s, start, start + step + 1));
        }
        if (cache.points != s.points.size()) {
            const size_t start = complete * step;
            cache.tail =
                start + 1 < s.points.size() || !complete ? pathGeometry(s, start, s.points.size()) : nullptr;
            cache.tailBox = partBounds(s, start, s.points.size());
            cache.points = s.points.size();
        }
        auto visible = [&](Bounds box) {
            return !clip || (box.l <= clip->r && box.r >= clip->l && box.t <= clip->b && box.b >= clip->t);
        };
        for (size_t i = 0; i < cache.chunks.size(); ++i)
            if (cache.chunks[i] && visible(cache.boxes[i]))
                rt->FillGeometry(cache.chunks[i].Get(), b);
        if (cache.tail && visible(cache.tailBox))
            rt->FillGeometry(cache.tail.Get(), b);
    }
    void strokeShape(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b, const Stroke &s) {
        const auto a = s.points.front(), z = s.points.back();
        const auto box = D2D1::RectF(float(std::min(a.x, z.x)), float(std::min(a.y, z.y)),
                                     float(std::max(a.x, z.x)), float(std::max(a.y, z.y)));
        if (s.kind == Kind::Rectangle)
            rt->DrawRectangle(box, b, float(s.width), roundStyle.Get());
        else if (s.kind == Kind::Ellipse)
            rt->DrawEllipse(
                D2D1::Ellipse(D2D1::Point2F((box.left + box.right) / 2, (box.top + box.bottom) / 2),
                              (box.right - box.left) / 2, (box.bottom - box.top) / 2),
                b, float(s.width), roundStyle.Get());
        else {
            rt->DrawLine(D2D1::Point2F(float(a.x), float(a.y)), D2D1::Point2F(float(z.x), float(z.y)), b,
                         float(s.width), roundStyle.Get());
            if (s.kind == Kind::Arrow) {
                const double angle = std::atan2(z.y - a.y, z.x - a.x), length = std::max(7.0, s.width * 5);
                for (double offset : {-.45, .45})
                    rt->DrawLine(D2D1::Point2F(float(z.x - length * std::cos(angle + offset)),
                                               float(z.y - length * std::sin(angle + offset))),
                                 D2D1::Point2F(float(z.x), float(z.y)), b, float(s.width), roundStyle.Get());
            }
        }
    }
    InkKey currentInkKey() {
        auto *s = doc();
        return {s->id,
                s->history.revision,
                gesture == Gesture::Erase ? eraseRevision : 0,
                gesture == Gesture::Move && selected ? *selected : 0,
                s->scale,
                s->scroll,
                s->horizontal,
                width(),
                height(),
                dpi(),
                header(),
                paperLeft(),
                paperRight()};
    }
    double topOf(int index) const {
        auto *self = const_cast<App *>(this);
        auto *s = self->doc();
        double y = 20;
        if (s)
            for (int i = 0; i < index && i < int(s->pages.size()); ++i)
                y += s->pages[i].h * s->scale + 18;
        return y;
    }
    double pageLeft(int index) const {
        auto *self = const_cast<App *>(this);
        auto *s = self->doc();
        if (!s)
            return 32;
        double available = width() - 148;
        return paperLeft() + std::max(0.0, (available - s->pages[index].w * s->scale) / 2) - s->horizontal;
    }
    double totalHeight() const {
        auto *s = const_cast<App *>(this)->doc();
        return s ? topOf(int(s->pages.size())) : 0;
    }
    void clampScroll() {
        if (auto *s = doc()) {
            s->scroll = std::clamp(s->scroll, 0.0, std::max(0.0, totalHeight() - bodyHeight() + 20));
            double widest = 0;
            for (auto size : s->pages)
                widest = std::max(widest, size.w * s->scale);
            s->horizontal = std::clamp(s->horizontal, 0.0, std::max(0.0, widest - (width() - 148)));
        }
    }
    int pageAt(double x, double y, bool requireInside = true) {
        auto *s = doc();
        if (!s || s->loading)
            return -1;
        if (requireInside && (x < paperLeft() || x > paperRight()))
            return -1;
        double py = y - header() + s->scroll;
        for (int i = 0; i < int(s->pages.size()); ++i) {
            double top = topOf(i), left = pageLeft(i);
            if (py >= top && py <= top + s->pages[i].h * s->scale &&
                (!requireInside || (x >= left && x <= left + s->pages[i].w * s->scale)))
                return i;
        }
        return -1;
    }
    Point pagePoint(double x, double y, int page, double w = 1.5) {
        auto *s = doc();
        return {(x - pageLeft(page)) / s->scale, (y - header() + s->scroll - topOf(page)) / s->scale, w};
    }
    void updateFields() {
        auto *s = doc();
        const bool enabled = s && !s->loading;
        auto update = [&](HWND edit, const std::wstring &value) {
            if (GetFocus() != edit) {
                wchar_t previous[128]{};
                GetWindowTextW(edit, previous, 128);
                if (value != previous)
                    SetWindowTextW(edit, value.c_str());
            }
            if (bool(IsWindowEnabled(edit)) != enabled)
                EnableWindow(edit, enabled);
        };
        update(pageEdit, enabled ? std::to_wstring(s->page + 1) : L"—");
        update(zoomEdit, enabled ? std::to_wstring(int(std::round(s->scale * 100))) + L"%" : L"—");
    }
    void layout() {
        if (!window)
            return;
        stopInertia();
        if (gesture != Gesture::None)
            finishGesture();
        const UINT currentDpi = GetDpiForWindow(window);
        if (currentDpi != fontDpi) {
            HFONT next = CreateFontW(-MulDiv(14, currentDpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
            for (HWND edit : {pageEdit, zoomEdit})
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(next), TRUE);
            if (font)
                DeleteObject(font);
            font = next;
            fontDpi = currentDpi;
        }
        if (auto *s = doc(); s && !s->loading && s->fit && !fitGuard) {
            fitGuard = true;
            double best = s->pages.empty() ? 595 : s->pages[s->page].w;
            const double scale = std::clamp((width() - 148) / best, .1, 4.0);
            if (std::abs(scale - s->scale) > .001) {
                s->scroll *= scale / s->scale;
                s->scale = scale;
                s->horizontal = 0;
                engine->cancelRender(s->id);
                s->pending = -1;
                s->pendingTicket = 0;
            }
            fitGuard = false;
        }
        auto factor = dpi();
        const auto navigationBox = navigation();
        MoveWindow(pageEdit, int((navigationBox.left + 10) * factor), int(9 * factor), int(48 * factor),
                   int(22 * factor), TRUE);
        MoveWindow(zoomEdit, int((navigationBox.left + 146) * factor), int(9 * factor), int(76 * factor),
                   int(22 * factor), TRUE);
        ShowWindow(pageEdit, header() && !closePrompt ? SW_SHOW : SW_HIDE);
        ShowWindow(zoomEdit, header() && !closePrompt ? SW_SHOW : SW_HIDE);
        clampScroll();
        updateFields();
        updateTooltips();
        invalidate();
        requestPage();
    }
    void requestPage() {
        RuntimeStage stage(RuntimeThread::Ui, "ui.page-request");
        if (smoke)
            return;
        auto *s = doc();
        if (!s || s->loading)
            return;
        int nearest = pageAt(width() / 2, header() + bodyHeight() / 2, false);
        if (nearest >= 0)
            s->page = nearest;
        const auto visible = [&](int page) {
            double y = header() + topOf(page) - s->scroll;
            return y + s->pages[page].h * s->scale >= header() && y <= height();
        };
        if (s->pending >= 0) {
            if (visible(s->pending)) {
                updateFields();
                return;
            }
            engine->cancelRender(s->id);
            s->pending = -1;
            s->pendingTicket = 0;
        }
        for (int i = 0; i < int(s->pages.size()); ++i) {
            double y = header() + topOf(i) - s->scroll;
            if (y + s->pages[i].h * s->scale < header() || y > height())
                continue;
            auto found = s->cache.find(i);
            const double renderScale = std::clamp(s->scale * dpi(), .1, 4.0);
            if (found == s->cache.end() || std::abs(found->second.cpu.scale - renderScale) > .005) {
                s->pending = i;
                s->pendingTicket = nextRenderTicket++;
                engine->render(s->id, i, renderScale, false, s->pendingTicket);
                break;
            }
        }
        updateFields();
    }
    void evict() {
        size_t bytes = 0;
        for (auto &s : sessions)
            for (auto &[i, c] : s.cache) {
                (void)i;
                bytes += c.cpu.pixels.size() * 2;
            }
        constexpr size_t limit = 256 * 1024 * 1024;
        for (auto &s : sessions) {
            if (bytes <= limit)
                break;
            for (auto i = s.cache.begin(); i != s.cache.end() && bytes > limit;) {
                if (s.id != doc()->id || std::abs(i->first - s.page) > 2) {
                    bytes -= i->second.cpu.pixels.size() * 2;
                    i = s.cache.erase(i);
                } else
                    ++i;
            }
        }
    }
    void vectorStroke(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b, const Stroke &stroke,
                      double left, double top, double scale, const Bounds *clip = nullptr) {
        if (stroke.points.empty())
            return;
        if (!b)
            b = inkBrush.Get();
        ComPtr<ID2D1Layer> layer;
        const bool translucent = stroke.color.a < 255 && SUCCEEDED(rt->CreateLayer(nullptr, &layer));
        if (translucent) {
            auto boundsBox = bounds(stroke);
            if (clip) {
                boundsBox.l = std::max(boundsBox.l, clip->l);
                boundsBox.t = std::max(boundsBox.t, clip->t);
                boundsBox.r = std::min(boundsBox.r, clip->r);
                boundsBox.b = std::min(boundsBox.b, clip->b);
            }
            auto box =
                D2D1::RectF(float(left + boundsBox.l * scale - 2), float(top + boundsBox.t * scale - 2),
                            float(left + boundsBox.r * scale + 2), float(top + boundsBox.b * scale + 2));
            rt->PushLayer(D2D1::LayerParameters(box, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                D2D1::Matrix3x2F::Identity(), stroke.color.a / 255.f),
                          layer.Get());
        }
        rt->SetTransform(D2D1::Matrix3x2F(float(scale), 0, 0, float(scale), float(left), float(top)));
        const auto c = stroke.color;
        b->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, translucent ? 1.f : c.a / 255.f));
        if (stroke.kind == Kind::Ink || stroke.kind == Kind::Marker)
            inkPath(rt, b, stroke, clip);
        else
            strokeShape(rt, b, stroke);
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        if (translucent)
            rt->PopLayer();
    }
    void drawStroke(const Stroke &stroke, double left, double top, double scale, bool supersample) {
        vectorStroke(supersample ? static_cast<ID2D1RenderTarget *>(inkTarget.Get()) : target.Get(),
                     supersample ? inkBrush.Get() : brush.Get(), stroke, left, top, scale);
    }
    void paintWorkingInk(const InkKey &key) {
        const auto &s = workingStroke;
        auto *session = doc();
        if (!session || s.points.empty())
            return;
        const int page = s.page;
        const double left = pageLeft(page), top = header() + topOf(page) - session->scroll,
                     scale = session->scale;
        if (!inkTarget) {
            drawStroke(s, left, top, scale, false);
            return;
        }
        if (!liveTarget) {
            const auto size = D2D1::SizeF(float(width()), float(height()));
            const auto livePixelFormat =
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
            if (FAILED(target->CreateCompatibleRenderTarget(&size, &inkPixels, &livePixelFormat,
                                                            D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                                            &liveTarget)) ||
                FAILED(liveTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &liveBrush))) {
                liveTarget.Reset();
                liveBrush.Reset();
                drawStroke(s, left, top, scale, false);
                return;
            }
            liveValid = false;
        }
        const bool incremental = gesture == Gesture::Draw && (s.kind == Kind::Ink || s.kind == Kind::Marker);
        const bool reset = !liveValid || liveStroke != s.id || liveKey != key ||
                           livePoints > s.points.size() || !incremental;
        liveTarget->BeginDraw();
        liveTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        if (reset) {
            liveTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
            livePoints = 0;
            liveBounds = {1e30, 1e30, -1e30, -1e30};
        }
        // Extend bounds with new samples only; a long active stroke must not
        // rescan its entire point array on every display frame.
        if (s.points.size() > livePoints) {
            liveBoundsPointsVisited += s.points.size() - livePoints;
            const auto added = partBounds(s, livePoints, s.points.size());
            liveBounds = {std::min(liveBounds.l, added.l), std::min(liveBounds.t, added.t),
                          std::max(liveBounds.r, added.r), std::max(liveBounds.b, added.b)};
        }
        const auto area = D2D1::RectF(float(std::max(paperLeft(), left)), float(std::max(header(), top)),
                                      float(std::min(paperRight(), left + session->pages[page].w * scale)),
                                      float(std::min(height(), top + session->pages[page].h * scale)));
        liveTarget->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        liveTarget->SetTransform(D2D1::Matrix3x2F(float(scale), 0, 0, float(scale), float(left), float(top)));
        const auto c = s.color;
        liveBrush->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f));
        if (incremental) {
            if (s.points.size() > livePoints) {
                size_t begin = livePoints ? livePoints - 1 : 0;
                while (begin < s.points.size()) {
                    const size_t end = std::min(begin + 25, s.points.size());
                    auto geometry = pathGeometry(s, begin, end);
                    if (geometry)
                        liveTarget->FillGeometry(geometry.Get(), liveBrush.Get());
                    if (end == s.points.size())
                        break;
                    begin = end - 1;
                }
            }
        } else if (s.kind == Kind::Ink || s.kind == Kind::Marker)
            inkPath(liveTarget.Get(), liveBrush.Get(), s);
        else
            strokeShape(liveTarget.Get(), liveBrush.Get(), s);
        liveTarget->SetTransform(D2D1::Matrix3x2F::Identity());
        liveTarget->PopAxisAlignedClip();
        ComPtr<ID2D1Bitmap> bitmap;
        if (SUCCEEDED(liveTarget->EndDraw()) && SUCCEEDED(liveTarget->GetBitmap(&bitmap))) {
            const auto b = incremental ? liveBounds : bounds(s);
            const auto visible = D2D1::RectF(float(std::max(double(area.left), left + b.l * scale - 2)),
                                             float(std::max(double(area.top), top + b.t * scale - 2)),
                                             float(std::min(double(area.right), left + b.r * scale + 2)),
                                             float(std::min(double(area.bottom), top + b.b * scale + 2)));
            if (visible.right > visible.left && visible.bottom > visible.top)
                target->DrawBitmap(bitmap.Get(), visible, c.a / 255.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                                   visible);
            liveValid = true;
            liveStroke = s.id;
            livePoints = s.points.size();
            liveKey = key;
        } else {
            liveBrush.Reset();
            liveTarget.Reset();
            liveValid = false;
            drawStroke(s, left, top, scale, false);
        }
    }
    void paint() {
        RuntimeStage stage(RuntimeThread::Ui, "ui.paint");
        const double paintStart = inputTime();
        ++paints;
        geometryPointsBuilt = inkRebuilds = liveBoundsPointsVisited = 0;
        inkTiles.beginFrame();
        PAINTSTRUCT ps{};
        BeginPaint(window, &ps);
        if (!graphics()) {
            EndPaint(window, &ps);
            return;
        }
        target->BeginDraw();
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->Clear(D2D1::ColorF(dark ? .13f : .945f, dark ? .14f : .95f, dark ? .16f : .96f));
        auto *s = doc();
        const bool supersample =
            s && !s->loading && !s->hidden &&
            (!s->history.current().empty() || gesture == Gesture::Draw || gesture == Gesture::Move) &&
            inkGraphics();
        const InkKey key = s && !s->loading ? currentInkKey() : InkKey{};
        const bool repaintInk = supersample && (!inkValid || inkKey != key);

        if (repaintInk) {
            ++inkRebuilds;
            inkTarget->BeginDraw();
            inkTarget->SetTransform(D2D1::Matrix3x2F::Identity());
            inkTarget->Clear(D2D1::ColorF(0, 0, 0, 0));
            inkTarget->PushAxisAlignedClip(
                D2D1::RectF(float(paperLeft()), float(header()), float(paperRight()), float(height())),
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        target->PushAxisAlignedClip(D2D1::RectF(0, float(header()), float(width()), float(height())),
                                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        if (s && !s->loading) {
            target->PushAxisAlignedClip(
                D2D1::RectF(float(paperLeft()), float(header()), float(paperRight()), float(height())),
                D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const auto &strokes = gesture == Gesture::Erase ? erased : s->history.current();
            for (int i = 0; i < int(s->pages.size()); ++i) {
                double top = header() + topOf(i) - s->scroll, left = pageLeft(i),
                       w = s->pages[i].w * s->scale, h = s->pages[i].h * s->scale;
                if (top + h < header() || top > height())
                    continue;
                rect(D2D1::RectF(float(left + 2), float(top + 3), float(left + w + 2), float(top + h + 3)),
                     {0, 0, 0, 18}, 1);
                auto area = D2D1::RectF(float(left), float(top), float(left + w), float(top + h));
                rect(area, {255, 255, 255, 255});
                auto found = s->cache.find(i);
                if (found != s->cache.end()) {
                    auto &c = found->second;
                    if (!c.gpu) {
                        auto props = D2D1::BitmapProperties(
                            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
                        target->CreateBitmap(D2D1::SizeU(c.cpu.width, c.cpu.height), c.cpu.pixels.data(),
                                             c.cpu.width * 4, props, &c.gpu);
                    }
                    if (c.gpu)
                        target->DrawBitmap(c.gpu.Get(), area);
                } else
                    text(
                        L"正在渲染…",
                        D2D1::RectF(float(left + 24), float(top + 24), float(left + w - 24), float(top + 55)),
                        {150, 155, 165, 255});
                target->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                if (repaintInk)
                    inkTarget->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                if (!s->hidden) {
                    if (supersample && repaintInk)
                        inkTiles.paint(inkTarget.Get(), s->id, i, key.revision, key.erasing, key.moving,
                                       strokes, left, top, s->scale, s->pages[i],
                                       D2D1::RectF(float(paperLeft()), float(header()), float(paperRight()),
                                                    float(height())),
                                       [&](ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b, const Stroke &stroke, double x, double y,
                                           double scale, const Bounds *clip) {
                                           vectorStroke(rt, b, stroke, x, y, scale, clip);
                                       });
                    else if (!supersample || repaintInk)
                        for (size_t index = 0; index < strokes.size(); ++index) {
                            const auto &st = strokes[index];
                            if (st.page == i) {
                                if (gesture == Gesture::Move && selected && st.id == *selected)
                                    continue;
                                drawStroke(st, left, top, s->scale, supersample);
                            }
                        }
                    if (!supersample && (gesture == Gesture::Draw || gesture == Gesture::Move) &&
                        workingStroke.page == i) {
                        drawStroke(workingStroke, left, top, s->scale, supersample);
                    }
                }
                if (selected && !s->hidden) {
                    const Stroke *chosen = nullptr;
                    if (gesture == Gesture::Move)
                        chosen = &workingStroke;
                    else
                        for (const auto &st : strokes)
                            if (st.id == *selected)
                                chosen = &st;
                    if (chosen && chosen->page == i) {
                        auto b = bounds(*chosen);
                        color({21, 101, 192, 255});
                        target->DrawRectangle(
                            D2D1::RectF(float(left + b.l * s->scale - 4), float(top + b.t * s->scale - 4),
                                        float(left + b.r * s->scale + 4), float(top + b.b * s->scale + 4)),
                            brush.Get(), 1);
                    }
                }
                if (repaintInk)
                    inkTarget->PopAxisAlignedClip();
                target->PopAxisAlignedClip();
                if (dark)
                    rect(area, {15, 20, 27, 55});
            }
            target->PopAxisAlignedClip();
        } else if (s)
            centeredText(
                L"正在打开 PDF…",
                D2D1::RectF(40, float(height() / 2 - 14), float(width() - 40), float(height() / 2 + 24)),
                Theme::muted);
        else
            paintHome();
        if (supersample) {
            HRESULT inkResult = S_OK;
            if (repaintInk) {
                inkTarget->PopAxisAlignedClip();
                inkResult = inkTarget->EndDraw();
                if (SUCCEEDED(inkResult)) {
                    inkKey = key;
                    inkValid = true;
                }
            }
            ComPtr<ID2D1Bitmap> bitmap;
            if (SUCCEEDED(inkResult) && SUCCEEDED(inkTarget->GetBitmap(&bitmap))) {
                target->DrawBitmap(bitmap.Get(), D2D1::RectF(0, 0, float(width()), float(height())), 1,
                                   D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            } else {
                inkBrush.Reset();
                inkTarget.Reset();
                inkUnavailable = true;
                inkValid = false;
                invalidate();
            }
            if ((gesture == Gesture::Draw || gesture == Gesture::Move) && !s->hidden)
                paintWorkingInk(key);
        }
        auto tb = toolbar();
        const int shadow = gesture == Gesture::Toolbar ? 24 : 12;
        for (int spread = 5; spread >= 1; --spread)
            rect(
                D2D1::RectF(tb.left - spread, tb.top - spread + 3, tb.right + spread, tb.bottom + spread + 3),
                {40, 52, 75, uint8_t(shadow / 5)}, 22 + float(spread));
        rect(tb, {255, 255, 255, 255}, 22);
        if (toolbarDraggable) {
            rect(D2D1::RectF(tb.left + 21, tb.top + 10, tb.right - 21, tb.top + 13),
                 gesture == Gesture::Toolbar ? Color{110, 130, 160, 255} : Color{200, 207, 218, 255}, 1.5f);
        }
        target->PushAxisAlignedClip(
            D2D1::RectF(tb.left, tb.top + float(toolsTop() - 2), tb.right, tb.bottom - 6),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (int i = 0; i < 9; ++i) {
            float y = tb.top + float(toolsTop()) + float(i * 46 + (i >= 6 ? 12 : 0) - panelScroll);
            auto r = D2D1::RectF(tb.left + 6, y, tb.right - 6, y + 42);
            const Color glyphColor =
                (i == 1 || i == 2 || i == 4 || i == 5) ? colors[i] : Color{85, 94, 113, 255};
            if (i == tool)
                rect(r, tintSurface(glyphColor), 15);
            else if (i == hoverControl)
                rect(r, {244, 246, 250, 255}, 15);
            if (i == 6) {
                color({225, 228, 233, 255});
                target->DrawLine(D2D1::Point2F(tb.left + 10, y - 7), D2D1::Point2F(tb.right - 10, y - 7),
                                 brush.Get());
            }
            const auto glyphBox = D2D1::RectF(tb.left + 18, y + 9, tb.left + 42, y + 33);
            D2D1_MATRIX_3X2_F previousTransform;
            target->GetTransform(&previousTransform);
            target->SetTransform(toolbarIconTransform() * previousTransform);
            const int glyph = toolbarGlyph(i);
            if (!icons.draw(target.Get(), glyph, glyphBox, glyphColor))
                toolGlyph(target.Get(), brush.Get(), roundStyle.Get(), glyph, glyphBox, glyphColor);
            if (i == tool && configurableTool(i)) {
                icons.draw(target.Get(), 12, D2D1::RectF(r.left - 2, y + 26, r.left + 14, y + 42),
                           glyphColor);
            }
            target->SetTransform(previousTransform);
        }
        target->PopAxisAlignedClip();
        target->PopAxisAlignedClip();
        if (header()) {
            rect(D2D1::RectF(0, 0, float(width()), 40), {252, 253, 255, 255});
            rect(D2D1::RectF(8, 5, 42, 35),
                 hoverControl == 24 || (panel && panelKind == PanelKind::Functions)
                     ? Color{230, 235, 245, 255}
                     : Theme::inset,
                 10);
            color(Theme::text);
            for (float y : {15.f, 20.f, 25.f})
                target->DrawLine(D2D1::Point2F(18, y), D2D1::Point2F(32, y), brush.Get(), 1.5f,
                                 roundStyle.Get());
            rect(D2D1::RectF(float(fileLeft()), 5, float(fileEdge()), 35),
                 hoverControl == 20 || (panel && panelKind == PanelKind::Files) ? Color{230, 235, 245, 255}
                                                                                : Theme::inset,
                 11);
            icons.logo(target.Get(), D2D1::RectF(float(fileLeft() + 8), 10, float(fileLeft() + 28), 30));
            text(s ? s->path.filename().wstring() : L"未打开文档",
                 D2D1::RectF(float(fileLeft() + 36), 10, float(fileEdge() - 38), 32));
            color({130, 140, 158, 255});
            target->DrawLine(D2D1::Point2F(float(fileEdge() - 23), 18),
                             D2D1::Point2F(float(fileEdge() - 19), 22), brush.Get(), 1.4f, roundStyle.Get());
            target->DrawLine(D2D1::Point2F(float(fileEdge() - 19), 22),
                             D2D1::Point2F(float(fileEdge() - 15), 18), brush.Get(), 1.4f, roundStyle.Get());
            if (s && s->history.dirty()) {
                color({103, 139, 205, 255});
                target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(float(fileEdge() - 30), 20), 2.5f, 2.5f),
                                    brush.Get());
            }
            const auto nav = navigation();
            // Child EDIT backgrounds use exactly the same color as these rounded capsules.
            rect(nav, {244, 246, 250, 255}, 12);
            rect(D2D1::RectF(nav.left + 131, 12, nav.left + 132, 28), {217, 224, 233, 255}, .5f);
            text(s && !s->loading ? L"/ " + std::to_wstring(s->pages.size()) : L"/ —",
                 D2D1::RectF(nav.left + 64, 11, nav.left + 126, 33), {113, 123, 142, 255}, true);
            if (hoverControl >= 21 && hoverControl <= 23)
                rect(D2D1::RectF(float(width() - (24 - hoverControl) * 46), 5,
                                 float(width() - (23 - hoverControl) * 46), 35),
                     hoverControl == 23 ? Color{255, 231, 233, 255} : Color{239, 242, 248, 255}, 10);
            const WindowGlyph windowGlyphs[] = {
                WindowGlyph::Minimize, IsZoomed(window) ? WindowGlyph::Restore : WindowGlyph::Maximize,
                WindowGlyph::Close};
            for (int i = 0; i < 3; ++i) {
                const float center = float(width() - 115 + i * 46);
                windowGlyph(target.Get(), brush.Get(), roundStyle.Get(), windowGlyphs[i],
                            D2D1::RectF(center - 8, 12, center + 8, 28),
                            i == 2 && hoverControl == 23 ? Color{203, 67, 82, 255} : Theme::text);
            }
        }
        if (!notice.empty())
            text(notice, D2D1::RectF(40, float(height() - 29), float(width() - 130), float(height() - 5)),
                 {95, 105, 120, 255}, true);
        paintTransientUI();
        paintClosePrompt();
        HRESULT hr = target->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET)
            releaseGraphics();
        EndPaint(window, &ps);
        lastPaintMilliseconds = (inputTime() - paintStart) * 1000;
        nextInkPresent = inputTime() + std::clamp(lastPaintMilliseconds / 2000, .016, .05);
    }
    std::filesystem::path dialog(bool save) {
        wchar_t filename[32768]{};
        OPENFILENAMEW o{};
        o.lStructSize = sizeof(o);
        o.hwndOwner = window;
        o.lpstrFilter = L"PDF 文档 (*.pdf)\0*.pdf\0\0";
        o.lpstrFile = filename;
        o.nMaxFile = 32768;
        o.lpstrDefExt = L"pdf";
        o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST |
                  (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        return (save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o)) ? std::filesystem::path(filename)
                                                                    : std::filesystem::path{};
    }
    void open(const std::filesystem::path &file, bool fromRecent = false) {
        stopInertia();
        if (file.empty())
            return;
        auto full = std::filesystem::absolute(file);
        for (size_t i = 0; i < sessions.size(); ++i) {
            std::error_code identityError;
            if (sessions[i].path == full ||
                std::filesystem::equivalent(sessions[i].path, full, identityError)) {
                activate(int(i));
                return;
            }
        }
        finishGesture();
        Session s;
        s.id = nextDocument++;
        s.path = full;
        s.fromRecent = fromRecent;
        s.history.reset({});
        sessions.push_back(std::move(s));
        active = int(sessions.size() - 1);
        notice = L"正在打开…";
        engine->open(doc()->id, full);
        layout();
    }
    void activate(int i) {
        if (i < 0 || i >= int(sessions.size()))
            return;
        finishGesture();
        stopInertia();
        rememberView();
        closePanel();
        if (auto *previous = doc(); previous && active != i) {
            engine->cancelRender(previous->id);
            previous->pending = -1;
            previous->pendingTicket = 0;
        }
        active = i;
        if (!sessions[i].loading)
            rememberRecent(sessions[i].path);
        selected.reset();
        notice = L"";
        layout();
    }
    void save(bool as = false) {
        auto *s = doc();
        if (!s || s->loading || s->saving)
            return;
        finishGesture();
        auto out = as ? dialog(true) : std::filesystem::path{};
        if (as && out.empty())
            return;
        if (as)
            for (const auto &other : sessions) {
                std::error_code ec;
                if (other.id != s->id && std::filesystem::equivalent(other.path, out, ec)) {
                    MessageBoxW(window, L"目标 PDF 已在另一个文件会话中打开，请先关闭该文件或选择其他路径。",
                                L"另存为", MB_OK | MB_ICONINFORMATION);
                    return;
                }
            }
        s->saving = true;
        notice = L"正在安全保存…";
        engine->save(s->id, s->history.current(), s->history.fingerprint(), out);
        invalidate();
    }
    bool closeDocument(int index, bool discard = false) {
        if (index < 0 || index >= int(sessions.size()))
            return true;
        activate(index);
        auto *s = doc();
        if (s->saving) {
            notice = L"正在保存，完成后即可关闭。";
            invalidate();
            return false;
        }
        if (s->history.dirty() && !discard) {
            showClosePrompt(s->id);
            return false;
        }
        auto id = s->id;
        rememberView();
        if (storage && !s->preserveRecovery)
            storage->erase(s->path);
        engine->close(id);
        inkTiles.forget(id);
        sessions.erase(sessions.begin() + index);
        active = sessions.empty() ? -1 : std::min(index, int(sessions.size() - 1));
        selected.reset();
        shapes.clear();
        layout();
        return true;
    }
    void closeWindow() {
        if (closePrompt)
            return;
        finishGesture();
        quitting = true;
        while (!sessions.empty())
            if (!closeDocument(int(sessions.size() - 1))) {
                if (!closePrompt && doc() && !doc()->closing)
                    quitting = false;
                return;
            }
        DestroyWindow(window);
    }
    void fileMenu() {
        openPopup(PanelKind::Files);
    }
    void functionsMenu() {
        openPopup(PanelKind::Functions);
    }
    double menuRowHeight(const MenuRow &row) const {
        return row.heading ? 28 : (!row.detail.empty() ? 58 : 36);
    }
    double menuHeight() const {
        double height = 0;
        for (const auto &row : menuRows)
            height += menuRowHeight(row);
        return height;
    }
    int menuHit(double x, double y) const {
        if (x < 16 || x > popupWidth() - 16 || y < 16 || y > popupHeight() - 16)
            return -1;
        double top = 16 - menuScroll;
        for (size_t i = 0; i < menuRows.size(); ++i) {
            const auto &row = menuRows[i];
            if (y >= top && y < top + menuRowHeight(row))
                return !row.heading && row.enabled ? int(i) : -1;
            top += menuRowHeight(row);
        }
        return -1;
    }
    void buildMenu() {
        menuRows.clear();
        menuScroll = 0;
        menuHover = menuFocus = -1;
        auto heading = [&](const wchar_t *name) { menuRows.push_back({0, name, L"", false, false, true}); };
        auto item = [&](int command, const wchar_t *label, bool enabled = true, bool checked = false) {
            menuRows.push_back({command, label, L"", enabled, checked, false});
        };
        if (panelKind == PanelKind::Files) {
            heading(L"已打开文件");
            if (sessions.empty())
                item(0, L"尚未打开文档", false);
            for (size_t i = 0; i < sessions.size(); ++i) {
                const auto &s = sessions[i];
                menuRows.push_back({1000 + int(i),
                                    s.path.filename().wstring() + (s.history.dirty() ? L"  •" : L""),
                                    s.path.parent_path().wstring(), true, int(i) == active, false});
            }
        } else {
            const bool ready = doc() && !doc()->loading;
            heading(L"文件");
            item(1, L"打开 PDF…\tCtrl+O");
            item(14, L"保存\tCtrl+S", ready);
            item(3, L"另存为…\tCtrl+Shift+S", ready);
            item(2, L"关闭文件\tCtrl+W", ready);
            heading(L"编辑");
            item(15, L"撤销\tCtrl+Z", ready && doc()->history.cursor > 0);
            item(16, L"重做\tCtrl+Y", ready && doc()->history.cursor + 1 < doc()->history.states.size());
            item(11, L"删除所选笔记\tDelete", ready && selected.has_value());
            heading(L"阅读与显示");
            item(4, L"适合宽度", ready);
            item(5, L"隐藏笔记", ready, ready && doc()->hidden);
            item(6, L"全屏\tF11", true, fullscreen);
            item(8, L"阅读调暗", true, dark);
            heading(L"工具栏");
            item(7, L"左手模式", true, leftHand);
            item(12, L"允许拖动", true, toolbarDraggable);
            item(13, L"重置位置");
            heading(L"帮助");
            item(9, L"设置与说明");
            item(10, L"第三方许可");
            item(17, L"笔输入诊断\tCtrl+Shift+D", true, diagnostics.active());
            item(18, L"打开诊断目录\tCtrl+Shift+L");
        }
        for (size_t i = 0; i < menuRows.size(); ++i)
            if (menuRows[i].enabled && !menuRows[i].heading) {
                menuFocus = int(i);
                break;
            }
    }
    void paintMenu(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b) {
        const float w = float(popupWidth()), h = float(popupHeight());
        auto set = [&](Color c) {
            b->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
        };
        auto text = [&](const std::wstring &label, D2D1_RECT_F box, Color c, bool compact = false) {
            set(c);
            rt->DrawTextW(label.c_str(), UINT32(label.size()), compact ? smallFormat.Get() : format.Get(),
                          box, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        rt->PushAxisAlignedClip(D2D1::RectF(16, 16, w - 16, h - 16), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        float y = float(16 - menuScroll);
        for (size_t i = 0; i < menuRows.size(); ++i) {
            const auto &row = menuRows[i];
            const float rowHeight = float(menuRowHeight(row));
            if (y + rowHeight >= 16 && y < h - 16) {
                if (!row.heading && (int(i) == menuHover || row.checked)) {
                    set(row.checked ? Color{230, 237, 250, 255} : Theme::inset);
                    rt->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(17, y + 2, w - 17, y + rowHeight - 2), 10, 10), b);
                }
                const auto tone = row.heading || !row.enabled ? Theme::muted : Theme::text;
                if (row.heading)
                    text(row.label, D2D1::RectF(28, y + 7, w - 28, y + rowHeight), tone, true);
                else {
                    const size_t tab = row.label.find(L'\t');
                    text(row.label.substr(0, tab),
                         D2D1::RectF(30, y + 9, w - (tab == std::wstring::npos ? 30 : 112), y + 33), tone);
                    if (tab != std::wstring::npos)
                        text(row.label.substr(tab + 1), D2D1::RectF(w - 110, y + 10, w - 26, y + 33),
                             Theme::muted, true);
                    if (!row.detail.empty())
                        text(row.detail, D2D1::RectF(30, y + 32, w - 30, y + 54), Theme::muted, true);
                    if (row.checked) {
                        set(Theme::accent);
                        rt->FillRoundedRectangle(
                            D2D1::RoundedRect(D2D1::RectF(21, y + 12, 24, y + rowHeight - 12), 1.5f, 1.5f),
                            b);
                    }
                }
            }
            y += rowHeight;
        }
        rt->PopAxisAlignedClip();
        const double overflow = menuHeight() - (popupHeight() - 32);
        if (overflow > 0) {
            const float track = h - 40, thumb = std::max(24.f, float(track * (h - 32) / menuHeight()));
            const float top = 20 + float(menuScroll / overflow) * (track - thumb);
            set({195, 205, 222, 255});
            rt->FillRoundedRectangle(
                D2D1::RoundedRect(D2D1::RectF(w - 14, top, w - 11, top + thumb), 1.5f, 1.5f), b);
        }
    }
    void popupMenuScroll(double delta) {
        menuScroll = std::clamp(menuScroll + delta, 0.0, std::max(0.0, menuHeight() - (popupHeight() - 32)));
        menuHover = -1;
        if (panel)
            InvalidateRect(panel, nullptr, FALSE);
    }
    void runCommand(int command) {
        closePanel();
        if (command >= 1000) {
            activate(command - 1000);
            return;
        }
        switch (command) {
        case 1:
            open(dialog(false));
            break;
        case 2:
            closeDocument(active);
            break;
        case 3:
            save(true);
            break;
        case 4:
            if (doc())
                doc()->fit = true;
            layout();
            break;
        case 5:
            if (doc())
                doc()->hidden = !doc()->hidden;
            selected.reset();
            invalidate();
            break;
        case 6:
            toggleFullscreen();
            break;
        case 7:
            leftHand = !leftHand;
            toolbarMoved = false;
            layout();
            break;
        case 8:
            dark = !dark;
            invalidate();
            break;
        case 9:
            openPopup(PanelKind::About);
            break;
        case 10: {
            auto resource = FindResourceW(instance, MAKEINTRESOURCEW(200), RT_RCDATA);
            auto data = resource ? LoadResource(instance, resource) : nullptr;
            const char *bytes = data ? static_cast<const char *>(LockResource(data)) : nullptr;
            int length = resource ? int(SizeofResource(instance, resource)) : 0;
            if (bytes && length) {
                int count = MultiByteToWideChar(CP_UTF8, 0, bytes, length, nullptr, 0);
                std::wstring content(count, L'\0');
                MultiByteToWideChar(CP_UTF8, 0, bytes, length, content.data(), count);
                auto view = CreateWindowExW(WS_EX_APPWINDOW, L"EDIT", L"第三方许可 — InkHorn",
                                            WS_OVERLAPPEDWINDOW | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                                                ES_AUTOVSCROLL | WS_VSCROLL | WS_HSCROLL,
                                            CW_USEDEFAULT, CW_USEDEFAULT, 760, 640, window, nullptr, instance,
                                            nullptr);
                SendMessageW(view, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
                SendMessageW(view, EM_SETLIMITTEXT, 0x7ffffffe, 0);
                SetWindowTextW(view, content.c_str());
            }
            break;
        }
        case 11:
            deleteSelected();
            break;
        case 12:
            toolbarDraggable = !toolbarDraggable;
            panelScroll = 0;
            persistSettings();
            layout();
            break;
        case 13:
            toolbarMoved = false;
            persistSettings();
            layout();
            break;
        case 14:
            save();
            break;
        case 15:
            undo(false);
            break;
        case 16:
            undo(true);
            break;
        case 17:
            toggleDiagnostics();
            break;
        case 18:
            openDiagnosticDirectory();
            break;
        }
    }
    void toggleFullscreen() {
        finishGesture();
        fullscreen = !fullscreen;
        if (fullscreen) {
            placement.length = sizeof(placement);
            GetWindowPlacement(window, &placement);
            SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN);
            MONITORINFO m{};
            m.cbSize = sizeof(m);
            GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &m);
            SetWindowPos(window, nullptr, m.rcMonitor.left, m.rcMonitor.top,
                         m.rcMonitor.right - m.rcMonitor.left, m.rcMonitor.bottom - m.rcMonitor.top,
                         SWP_FRAMECHANGED | SWP_NOZORDER);
            showHeader = false;
        } else {
            SetWindowLongPtrW(window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
            SetWindowPlacement(window, &placement);
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
            showHeader = true;
        }
        layout();
    }
    void undo(bool redo) {
        if (auto *s = doc(); s && !s->loading && !s->saving) {
            finishGesture();
            if (redo)
                s->history.redo();
            else
                s->history.undo();
            recoverLater(*s);
            selected.reset();
            shapes.clear();
            invalidate();
        }
    }
    void setScale(double scale) {
        RuntimeStage stage(RuntimeThread::Ui, "ui.zoom");
        stopInertia();
        if (auto *s = doc(); s && !s->loading) {
            scale = std::clamp(scale, .1, 4.0);
            if (std::abs(scale - s->scale) < .00001)
                return;
            finishGesture();
            double ratio = std::clamp(scale, .1, 4.0) / s->scale;
            s->scroll = (s->scroll + bodyHeight() / 2) * ratio - bodyHeight() / 2;
            s->scale = std::clamp(scale, .1, 4.0);
            s->fit = false;
            engine->cancelRender(s->id);
            s->pending = -1;
            s->pendingTicket = 0;
            clampScroll();
            updateFields();
            invalidate();
            requestPage();
        }
    }
    void submitEdit(HWND edit, bool commit) {
        stopInertia();
        if (!commit) {
            SetFocus(window);
            updateFields();
            return;
        }
        wchar_t buf[64]{};
        GetWindowTextW(edit, buf, 64);
        wchar_t *end = nullptr;
        double value = wcstod(buf, &end);
        bool valid = end != buf && std::isfinite(value);
        while (end && (*end == L' ' || *end == L'%'))
            ++end;
        valid = valid && end && !*end;
        auto *s = doc();
        if (!s || s->loading)
            return;
        if (edit == pageEdit) {
            valid = valid && std::floor(value) == value && value >= 1 && value <= s->pages.size();
            if (valid) {
                s->page = int(value) - 1;
                s->scroll = std::max(0.0, topOf(s->page) - 20);
                clampScroll();
                requestPage();
            }
        } else {
            valid = valid && value >= 10 && value <= 400;
            if (valid)
                setScale(value / 100);
        }
        if (!valid) {
            MessageBeep(MB_ICONWARNING);
            SendMessageW(edit, EM_SETSEL, 0, -1);
            notice = edit == pageEdit ? L"请输入有效页码（1 至总页数）。" : L"缩放请输入 10% 至 400%。";
            invalidate();
            return;
        }
        SetFocus(window);
        updateFields();
        invalidate();
    }
    void eraseAt(Point p) {
        auto *s = doc();
        if (!s)
            return;
        const auto before = erased.size();
        std::erase_if(erased, [&](const Stroke &st) {
            return st.page == gesturePage && hitSweep(st, startPoint, p, eraserWidth / s->scale / 2);
        });
        startPoint = p;
        if (before != erased.size())
            ++eraseRevision;
    }
    void down(double x, double y, bool barrel = false, double pressure = .5, bool pressureValid = false,
              double sampleTime = inputTime()) {
        RuntimeStage stage(RuntimeThread::Ui, "ui.ink-down");
        stopInertia();
        if (closePrompt) {
            const int button = closeButtonHit(x, y);
            if (button >= 0)
                answerClosePrompt(button);
            return;
        }
        const bool wasPanelOpen = panel != nullptr;
        const auto previousPanel = panelKind;
        if (panel) {
            closePanel();
        }
        if (fullscreen && !showHeader && y < 8) {
            showHeader = true;
            layout();
            return;
        }
        if (y < header()) {
            if (x > width() - 46) {
                closeWindow();
                return;
            }
            if (x > width() - 92) {
                ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
                return;
            }
            if (x > width() - 138) {
                ShowWindow(window, SW_MINIMIZE);
                return;
            }
            if (x < fileEdge()) {
                if (x < 44) {
                    if (!wasPanelOpen || previousPanel != PanelKind::Functions)
                        functionsMenu();
                } else if (x >= fileLeft()) {
                    if (!wasPanelOpen || previousPanel != PanelKind::Files)
                        fileMenu();
                }
                return;
            }
            return;
        }
        if (toolbarHandle(x, y)) {
            closePanel();
            const auto r = toolbar();
            toolbarGrabX = x - r.left;
            toolbarGrabY = y - r.top;
            gesture = Gesture::Toolbar;
            invalidate();
            return;
        }
        int button = toolHit(x, y);
        if (button >= 0) {
            if (button >= 6) {
                if (button == 6)
                    undo(false);
                if (button == 7)
                    undo(true);
                if (button == 8)
                    save();
                return;
            }
            if (tool == button && button > 0) {
                if (!wasPanelOpen && configurableTool(button))
                    openPanel();
            } else {
                tool = button;
                selected.reset();
                invalidate();
            }
            return;
        }
        if (button == -2)
            return;
        auto *s = doc();
        if (!s) {
            const int hit = homeHit(x, y);
            if (hit == 30)
                open(dialog(false));
            else if (hit >= 40 && size_t(hit - 40) < homeRecentCount())
                openRecent(size_t(hit - 40));
            return;
        }
        if (s->loading)
            return;
        if (s->closing)
            return;
        int page = pageAt(x, y);
        lastScreen = {x, y, 0};
        startScroll = s->scroll;
        startHorizontal = s->horizontal;
        horizontalPan = false;
        if (page < 0) {
            beginPan(x, y, false);
            return;
        }
        if (s->hidden)
            return;
        gesturePage = page;
        startPoint = pagePoint(x, y, page);
        int effective = barrel ? 3 : tool;
        if (effective == 3) {
            ++eraseRevision;
            gesture = Gesture::Erase;
            erased = s->history.current();
            eraseAt(startPoint);
            invalidate();
            return;
        }
        if (effective == 0) {
            selected.reset();
            for (auto i = s->history.current().rbegin(); i != s->history.current().rend(); ++i)
                if (i->page == page && hit(*i, startPoint.x, startPoint.y, 5 / s->scale)) {
                    selected = i->id;
                    workingStroke = *i;
                    gesture = Gesture::Move;
                    break;
                }
            invalidate();
            return;
        }
        gesture = Gesture::Draw;
        Kind kind = effective == 4 ? Kind::Marker : (effective == 5 ? shapeKind : Kind::Ink);
        double w = widths[effective];
        workingStroke = {++nextStroke, page, kind, colors[effective], w, {}};
        startPoint.width = kind == Kind::Ink ? pressureWidth(pressure, w, pressureValid) : w;
        workingStroke.points.push_back(startPoint);
        if (kind != Kind::Ink && kind != Kind::Marker)
            workingStroke.points.push_back(startPoint);
        stabilizer.reset(startPoint, sampleTime);
        invalidate();
    }
    void move(double x, double y, double pressure = .5, bool pressureValid = false,
              double sampleTime = inputTime()) {
        if (gesture == Gesture::Toolbar) {
            const auto r = toolbar();
            toolbarMoved = true;
            toolbarAnchorX = std::clamp(
                (x - toolbarGrabX - 12) / std::max(1.0, width() - (r.right - r.left) - 24), 0.0, 1.0);
            toolbarAnchorY = std::clamp((y - toolbarGrabY - header() - 12) /
                                            std::max(1.0, bodyHeight() - (r.bottom - r.top) - 24),
                                        0.0, 1.0);
            updateTooltips();
            invalidate();
            return;
        }
        auto *s = doc();
        if (!s)
            return;
        if (gesture == Gesture::Pan) {
            s->scroll = startScroll - (y - lastScreen.y);
            if (horizontalPan)
                s->horizontal = startHorizontal - (x - lastScreen.x);
            clampScroll();
            momentum.sample(s->scroll, sampleTime);
            requestPage();
            invalidate();
            return;
        }
        if (gesturePage < 0 || gesture == Gesture::None)
            return;
        auto p = pagePoint(x, y, gesturePage);
        if (gesture == Gesture::Erase) {
            eraseAt(p);
            invalidate();
            return;
        }
        if (gesture == Gesture::Move) {
            double dx = p.x - startPoint.x, dy = p.y - startPoint.y;
            translate(workingStroke, dx, dy);
            startPoint = p;
            shapes.erase(workingStroke.id);
            invalidate();
            return;
        }
        p.x = std::clamp(p.x, 0.0, s->pages[gesturePage].w);
        p.y = std::clamp(p.y, 0.0, s->pages[gesturePage].h);
        p.width = workingStroke.kind == Kind::Ink
                      ? pressureWidth(pressure, workingStroke.width, pressureValid)
                      : workingStroke.width;
        if (workingStroke.kind == Kind::Ink || workingStroke.kind == Kind::Marker) {
            p = stabilizer.filter(p, sampleTime, s->scale);
            auto q = workingStroke.points.back();
            if (std::hypot(p.x - q.x, p.y - q.y) * s->scale >= .12 ||
                std::abs(p.width - q.width) * s->scale >= .04)
                workingStroke.points.push_back(p);
        } else
            workingStroke.points.back() = p;
        invalidate();
    }
    void finishGesture(bool release = false) {
        RuntimeStage stage(RuntimeThread::Ui, "ui.ink-commit");
        auto *s = doc();
        const auto completed = gesture;
        if (completed == Gesture::Pan && release && momentum.release(inputTime()))
            SetTimer(window, 2, 16, nullptr);
        else if (completed == Gesture::Pan)
            stopInertia();
        if (completed == Gesture::Toolbar) {
            persistSettings();
            updateTooltips();
        }
        if (s) {
            if (gesture == Gesture::Draw) {
                auto next = s->history.current();
                if (!workingStroke.points.empty()) {
                    next.push_back(workingStroke);
                    s->history.commit(std::move(next));
                }
            } else if (gesture == Gesture::Move) {
                auto next = s->history.current();
                for (auto &st : next)
                    if (selected && st.id == *selected)
                        st = workingStroke;
                s->history.commit(std::move(next));
            } else if (gesture == Gesture::Erase && erased.size() != s->history.current().size())
                s->history.commit(std::move(erased));
        }
        gesture = Gesture::None;
        gesturePage = -1;
        if (s && completed != Gesture::None && completed != Gesture::Pan && completed != Gesture::Toolbar)
            recoverLater(*s);
        liveValid = false;
        invalidate();
    }
    void releaseAt(double x, double y) {
        // End at the real tip location without feeding pen-up's zero pressure into the filter.
        auto *s = doc();
        if (s && gesture == Gesture::Draw && gesturePage >= 0 &&
            (workingStroke.kind == Kind::Ink || workingStroke.kind == Kind::Marker)) {
            auto p = pagePoint(x, y, gesturePage, workingStroke.points.back().width);
            p.x = std::clamp(p.x, 0.0, s->pages[gesturePage].w);
            p.y = std::clamp(p.y, 0.0, s->pages[gesturePage].h);
            const auto last = workingStroke.points.back();
            if (std::hypot(p.x - last.x, p.y - last.y) > .01)
                workingStroke.points.push_back(p);
        } else if (gesture != Gesture::None)
            move(x, y);
        finishGesture(true);
    }
    void penInput(UINT message, const POINTER_PEN_INFO &info,
                  std::span<const POINTER_PEN_INFO> history = {}) {
        if (mouseContact && message != WM_POINTERDOWN)
            return;
        if (message != WM_POINTERLEAVE && !(info.pointerInfo.pointerFlags & POINTER_FLAG_CANCELED))
            usePenInput();
        const auto &pointer = info.pointerInfo;
        const UINT id = pointer.pointerId;
        const bool contact = (pointer.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        const bool owned = penContact.owns(id);
        penScreen = pointer.ptPixelLocation;
        penPositionKnown = true;
        penTemporaryEraser = penEraser(info.penFlags);
        penNear = (pointer.pointerFlags & POINTER_FLAG_INRANGE) != 0;
        penCoordinates.refresh(pointer.sourceDevice, message == WM_POINTERDOWN || message == WM_POINTERENTER);
        POINT origin{};
        ScreenToClient(window, &origin);
        const auto position = penCoordinates.screen(pointer);
        const double x = (position.x + origin.x) / dpi(), y = (position.y + origin.y) / dpi();
        const bool pressureValid = (info.penMask & PEN_MASK_PRESSURE) != 0;
        if (message == WM_POINTERLEAVE) {
            if (penContact.owns(id) && !contact) {
                finishGesture(); // Lost UP: end at the last contact point, never at a hover position.
                penContact.end();
                activePointer = 0;
            }
            if (penContact.active())
                penNear = true;
            if (!penNear)
                penTemporaryEraser = false;
            refreshCursor();
            return;
        }
        if (message == WM_POINTERENTER) {
            penNear = true;
            updateCursorAt(x, y);
            return;
        }
        if (pointer.pointerFlags & POINTER_FLAG_CANCELED) {
            cancelGesture();
            updateCursorAt(x, y);
            return;
        }
        if (message == WM_POINTERDOWN) {
            if (gesture != Gesture::None)
                finishGesture();
            penContact.end();
            activePointer = 0;
            mouseContact = false;
            if (!contact)
                return;
            SetFocus(window);
            down(x, y, penTemporaryEraser, info.pressure / 1024.0, pressureValid, pointerSampleTime(pointer));
            // Closing a popup can dispatch capture changes synchronously. Arm only AFTER it closes.
            if (gesture != Gesture::None) {
                activePointer = id;
                penContact.begin(info);
            }
        } else if (message == WM_POINTERUP) {
            if (penContact.owns(id))
                releaseAt(x, y);
            if (penContact.owns(id))
                penContact.end();
            if (activePointer == id)
                activePointer = 0;
        } else if (message == WM_POINTERUPDATE) {
            if (!penContact.owns(id)) {
                if (!penContact.active())
                    hover(x, y);
            } else if (!contact) {
                finishGesture();
                penContact.end();
                activePointer = 0;
            } else {
                const bool shouldErase = tool == 3 || penTemporaryEraser;
                if ((gesture == Gesture::Draw || gesture == Gesture::Erase) &&
                    shouldErase != (gesture == Gesture::Erase)) {
                    // A driver can change pen end/device while already touching the page.
                    finishGesture();
                    down(x, y, penTemporaryEraser, info.pressure / 1024.0, pressureValid, pointerSampleTime(pointer));
                    if (gesture != Gesture::None)
                        penContact.begin(info);
                    else {
                        penContact.end();
                        activePointer = 0;
                    }
                } else if (gesture == Gesture::Pan || gesture == Gesture::Toolbar) {
                    if (penContact.accept(info))
                        move(x, y);
                } else {
                    auto sample = [&](const POINTER_PEN_INFO &sampleInfo) {
                        if (penEraser(sampleInfo.penFlags) != penTemporaryEraser ||
                            !penContact.accept(sampleInfo))
                            return;
                        const auto point = penCoordinates.screen(sampleInfo.pointerInfo);
                        move((point.x + origin.x) / dpi(), (point.y + origin.y) / dpi(), sampleInfo.pressure / 1024.0,
                             (sampleInfo.penMask & PEN_MASK_PRESSURE) != 0,
                             pointerSampleTime(sampleInfo.pointerInfo));
                    };
                    for (auto it = history.rbegin(); it != history.rend(); ++it)
                        sample(*it);
                    sample(info); // History may be absent or incomplete; repeated timestamps are ignored.
                }
            }
        }
        updateCursorAt(x, y);
        if (owned || (message == WM_POINTERDOWN && penContact.active()))
            presentInkIfDue();
    }
    bool inkQualityCheck() {
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICBitmap> pixels;
        ComPtr<ID2D1RenderTarget> output;
        ComPtr<ID2D1BitmapRenderTarget> layer;
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&wic))) ||
            FAILED(wic->CreateBitmap(512, 512, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &pixels)))
            return false;
        auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                                 D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                  D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(factory->CreateWicBitmapRenderTarget(pixels.Get(), props, &output)))
            return false;
        const auto size = D2D1::SizeF(512, 512);
        const auto resolution = D2D1::SizeU(1024, 1024);
        if (FAILED(output->CreateCompatibleRenderTarget(&size, &resolution, nullptr,
                                                        D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &layer)))
            return false;
        std::vector<Stroke> strokes{
            {++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 2,
             {{10, 100.2, 2}, {500, 100.2, 2}}},
            {++nextStroke, 0, Kind::Marker, {211, 47, 47, 72}, 12,
             {{10, 200, 12}, {500, 200, 12}, {10, 200, 12}}},
            {++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 2,
             {{350.2, 10, 2}, {350.2, 500, 2}}},
            {++nextStroke, 0, Kind::Marker, {211, 47, 47, 72}, 12,
             {{450, 10, 12}, {450, 500, 12}, {450, 10, 12}}}};
        // Thin oblique ink exposes pixel stair-steps; a steep pressure ramp
        // exposes non-tangent shoulders. Keep these inside an empty test area.
        strokes.push_back({++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, .65,
                           {{20.25, 280.35, .65}, {300.25, 350.35, .65}}});
        strokes.push_back({++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 4,
                           {{40, 390, 2}, {52, 390, 16}, {76, 390, 3}, {110, 390, 3}}});
        strokes.push_back({++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 20,
                           {{170.25, 410.35, 20}}});
        InkTiles tiles;
        size_t edgePixels = 0, diagonalCoverage = 0;
        bool ok = true;
        size_t previousUpdates = 0;
        std::vector<BYTE> data(512 * 512 * 4);
        auto saveSample = [&](const wchar_t *path) {
            ComPtr<IWICStream> stream;
            ComPtr<IWICBitmapEncoder> encoder;
            ComPtr<IWICBitmapFrameEncode> frame;
            if (SUCCEEDED(wic->CreateStream(&stream)) &&
                SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)) &&
                SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
                SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
                SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr))) {
                frame->SetSize(512, 512);
                WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat32bppBGRA;
                frame->SetPixelFormat(&pixelFormat);
                frame->WritePixels(512, 512 * 4, UINT(data.size()), data.data());
                frame->Commit();
                encoder->Commit();
            }
        };
        std::ofstream samples("ink-quality-samples.txt");
        for (int pass = 0; pass < 4; ++pass) {
            if (pass == 2) {
                layer.Reset();
                const auto denseResolution = D2D1::SizeU(1536, 1536);
                if (FAILED(output->CreateCompatibleRenderTarget(&size, &denseResolution, nullptr,
                                                                D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE, &layer)))
                    return false;
            }
            const bool shifted = (pass % 2) != 0;
            tiles.beginFrame();
            layer->BeginDraw();
            layer->Clear(D2D1::ColorF(0, 0, 0, 0));
            tiles.paint(layer.Get(), 1, 0, 1, 0, 0, strokes, shifted ? .23 : 0, shifted ? -.37 : 0,
                        1, {512, 512}, D2D1::RectF(0, 0, 512, 512),
                        [&](ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b, const Stroke &stroke,
                            double x, double y, double scale, const Bounds *clip) {
                            vectorStroke(rt, b, stroke, x, y, scale, clip);
                        });
            ComPtr<ID2D1Bitmap> image;
            if (FAILED(layer->EndDraw()) || FAILED(layer->GetBitmap(&image)))
                return false;
            output->BeginDraw();
            output->Clear(D2D1::ColorF(0, 0, 0, 0));
            output->DrawBitmap(image.Get(), D2D1::RectF(0, 0, 512, 512), 1,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            if (FAILED(output->EndDraw()) || FAILED(pixels->CopyPixels(nullptr, 512 * 4, UINT(data.size()), data.data())))
                return false;
            auto alpha = [&](int x, int y) { return data[(y * 512 + x) * 4 + 3]; };
            for (int x : {100, 169, 170, 171, 254, 255, 256, 257, 340, 341, 342, 400}) {
                samples << "pass=" << pass << " x=" << x << " ink=" << int(alpha(x, 100))
                        << " marker=" << int(alpha(x, 200)) << '\n';
                ok = ok && alpha(x, 100) > 128 &&
                     std::abs(int(alpha(x, 100)) - int(alpha(100, 100))) <= 1 &&
                     alpha(x, 200) >= 71 && alpha(x, 200) <= 73;
                for (int y = 97; y <= 103; ++y)
                    if (alpha(x, y) > 0 && alpha(x, y) < 250)
                        ++edgePixels;
            }
            for (int y : {150, 169, 170, 171, 254, 255, 256, 257, 340, 341, 342, 400})
                ok = ok && alpha(350, y) > 128 &&
                     std::abs(int(alpha(350, y)) - int(alpha(350, 150))) <= 1 &&
                     alpha(450, y) >= 71 && alpha(450, y) <= 73;
            int minCoverage = 10000, maxCoverage = 0;
            const bool axesOk = ok;
            for (int x = 30; x < 290; ++x) {
                const double lineY = 280.35 + (x + .5 - (shifted ? .23 : 0) - 20.25) * .25 +
                                     (shifted ? -.37 : 0);
                int coverage = 0;
                for (int y = int(lineY) - 2; y <= int(lineY) + 2; ++y)
                    coverage += alpha(x, y);
                minCoverage = std::min(minCoverage, coverage);
                maxCoverage = std::max(maxCoverage, coverage);
                // At 2x, bilinear resolve averages all four samples. The 3x
                // fractional-DPI stress pass has a wider coverage range.
                ok = ok && coverage > (pass < 2 ? 120 : 35) && coverage <= (pass < 2 ? 220 : 260);
                if (coverage > (pass < 2 ? 120 : 35))
                    ++diagonalCoverage;
            }
            samples << "pass=" << pass << " axes_ok=" << axesOk << " diagonal_min=" << minCoverage
                    << " diagonal_max=" << maxCoverage << '\n';
            if (shifted)
                ok = ok && tiles.updates == previousUpdates;
            previousUpdates = tiles.updates;
            if (pass == 1)
                saveSample(L"ink-quality-2x.png");
        }
        ok = ok && edgePixels > 0;
        std::ofstream report("ink-quality-results.json");
        report << "{\n  \"passed\": " << (ok ? "true" : "false")
               << ",\n  \"antialiased_edge_samples\": " << edgePixels
               << ",\n  \"thin_diagonal_covered_columns\": " << diagonalCoverage
               << ",\n  \"tile_updates\": " << tiles.updates << "\n}\n";
        saveSample(L"ink-quality.png");
        return ok;
    }
    bool profileScroll(const std::filesystem::path &path, double scale = 1) {
        open(path);
        if (!doc())
            return false;
        doc()->fit = false;
        doc()->scale = std::clamp(scale, .1, 4.0);
        auto pumpUntil = [&](auto ready) {
            const auto deadline = GetTickCount64() + 30000;
            while (!ready() && GetTickCount64() < deadline) {
                MsgWaitForMultipleObjectsEx(0, nullptr, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
            return ready();
        };
        if (!pumpUntil([&] { return doc() && !doc()->loading; }))
            return false;
        std::vector<size_t> pagePoints(doc()->pages.size());
        for (const auto &stroke : doc()->history.current())
            if (stroke.page >= 0 && size_t(stroke.page) < pagePoints.size())
                pagePoints[stroke.page] += stroke.points.size();
        doc()->page = int(std::max_element(pagePoints.begin(), pagePoints.end()) - pagePoints.begin());
        const double origin = topOf(doc()->page);
        doc()->scroll = origin;
        layout();
        for (int page = doc()->page; page < std::min(doc()->page + 2, int(doc()->pages.size())); ++page) {
            engine->render(doc()->id, page, std::clamp(doc()->scale * dpi(), .1, 4.0), false);
            if (!pumpUntil([&] { return doc()->cache.contains(page); }))
                return false;
        }
        size_t points = 0, maxPoints = 0;
        double widest = 0;
        for (const auto &stroke : doc()->history.current()) {
            points += stroke.points.size();
            maxPoints = std::max(maxPoints, stroke.points.size());
            const auto b = bounds(stroke);
            widest = std::max(widest, b.r - b.l);
        }
        invalidate();
        paint();
        const double initialMs = lastPaintMilliseconds;
        const auto before = inkTiles.updates;
        std::vector<double> frames;
        for (int i = 0; i < 12; ++i) {
            doc()->scroll = origin + 180 * (1 - std::cos((i + 1) * .23));
            clampScroll();
            invalidate();
            paint();
            frames.push_back(lastPaintMilliseconds);
        }
        std::sort(frames.begin(), frames.end());
        std::ofstream report("document-scroll-profile.json");
        report << "{\n  \"strokes\": " << doc()->history.current().size()
               << ",\n  \"tested_page\": " << doc()->page
               << ",\n  \"points\": " << points << ",\n  \"max_points_per_stroke\": " << maxPoints
               << ",\n  \"widest_stroke\": " << widest << ",\n  \"scale\": " << doc()->scale
               << ",\n  \"dpi\": " << dpi() << ",\n  \"initial_ms\": " << initialMs
               << ",\n  \"scroll_p95_ms\": " << frames[10] << ",\n  \"scroll_max_ms\": " << frames.back()
               << ",\n  \"scroll_raster_builds\": " << inkTiles.updates - before
               << ",\n  \"raster_bytes\": " << inkTiles.bytes << "\n}\n";
        return true;
    }
    bool shortStrokeCheck() {
        Session demo;
        demo.id = 997;
        demo.path = L"short-stroke-benchmark.pdf";
        demo.loading = demo.fit = false;
        demo.pages.resize(120);
        std::vector<Stroke> seeded;
        for (int i = 0; i < 1600; ++i) {
            Stroke s{++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 1.5, {}};
            for (int j = 0; j < 12; ++j)
                s.points.push_back({40 + (i % 40) * 12.0 + j * .6,
                                    40 + (i / 40) * 15.0 + std::sin(j * .4) * 2, 1.5});
            seeded.push_back(std::move(s));
        }
        demo.history.reset(std::move(seeded));
        sessions.push_back(std::move(demo));
        active = 0;
        layout();
        invalidate();
        paint();
        const auto root = std::filesystem::current_path() /
                          ("short-stroke-test-" + std::to_string(GetCurrentProcessId()));
        storage = std::make_unique<Storage>(root / "recovery");
        for (int i = 0; i < 256; ++i)
            PostMessageW(window, WM_NULL, 0, 0);
        nextInkPresent = 0;
        const auto beforeRefresh = paints;
        invalidate();
        presentInkIfDue();
        MSG queued{};
        const bool refreshWithInputPending = paints > beforeRefresh &&
            PeekMessageW(&queued, window, WM_NULL, WM_NULL, PM_NOREMOVE);
        const auto afterRefresh = paints;
        invalidate();
        presentInkIfDue(nextInkPresent - .001);
        const bool boundedRefresh = paints == afterRefresh;
        while (PeekMessageW(&queued, window, WM_NULL, WM_NULL, PM_REMOVE)) {}
        std::vector<double> contacts, commits;
        double worst = 0;
        int worstStroke = -1;
        tool = 1;
        for (int i = 0; i < 400; ++i) {
            const double x = pageLeft(0) + (60 + (i % 30) * 15.0) * doc()->scale;
            const double y = header() + topOf(0) + (60 + (i / 30) * 20.0) * doc()->scale;
            const auto start = inputTime();
            down(x, y, false, .02, true);
            for (int j = 1; j <= 8; ++j)
                move(x + j * 1.2, y + std::sin(j * .4) * 3, .02 + j * .01, true);
            paint();
            contacts.push_back((inputTime() - start) * 1000);
            const auto up = inputTime();
            smoke = false; // Exercise the production recovery queue as well as rendering.
            finishGesture();
            smoke = true;
            paint();
            const double elapsed = (inputTime() - up) * 1000;
            commits.push_back(elapsed);
            if (elapsed > worst) {
                worst = elapsed;
                worstStroke = i;
            }
        }
        auto percentile = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            return values[size_t((values.size() - 1) * .95)];
        };
        const bool complete = doc()->history.current().size() == 2000;
        storage->flush();
        const auto writes = storage->recoveryWrites();
        const auto errors = storage->lastError();
        storage.reset();
        for (const auto &file : std::filesystem::directory_iterator(root / "recovery"))
            std::filesystem::remove(file.path());
        std::filesystem::remove(root / "recovery");
        std::filesystem::remove(root);
        const double contactP95 = percentile(contacts), commitP95 = percentile(commits);
        const bool ok = complete && errors.empty() && refreshWithInputPending && boundedRefresh &&
                        contactP95 < 33 && commitP95 < 33 && worst < 100;
        std::ofstream report("short-stroke-results.json");
        report << "{\n  \"passed\": " << (ok ? "true" : "false")
               << ",\n  \"seeded_strokes\": 1600,\n  \"new_strokes\": 400,\n  \"contact_p95_ms\": " << contactP95
               << ",\n  \"commit_p95_ms\": " << commitP95 << ",\n  \"commit_max_ms\": " << worst
               << ",\n  \"worst_stroke\": " << worstStroke
               << ",\n  \"refresh_with_input_pending\": " << (refreshWithInputPending ? "true" : "false")
               << ",\n  \"bounded_refresh\": " << (boundedRefresh ? "true" : "false")
               << ",\n  \"recovery_writes\": " << writes << "\n}\n";
        sessions.clear();
        active = -1;
        return ok;
    }
    bool performanceCheck() {
        Session demo;
        demo.id = 999;
        demo.path = L"性能测试.pdf";
        demo.loading = false;
        demo.fit = false;
        demo.pages.resize(120);
        std::vector<Stroke> initial;
        for (int i = 0; i < 80; ++i) {
            Stroke s{++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 1.5, {}};
            for (int j = 0; j < 160; ++j)
                s.points.push_back({45 + (i % 10) * 48 + std::sin(j * .15) * 14, 45 + (i / 10) * 76 + j * .22,
                                    1.2 + .6 * std::sin(j * .08)});
            initial.push_back(std::move(s));
        }
        demo.history.reset(std::move(initial));
        sessions.push_back(std::move(demo));
        active = 0;
        layout();
        invalidate();
        paint();
        const double initialMs = lastPaintMilliseconds;
        std::vector<double> idle, writing;
        size_t rebuiltDuringWriting = 0, maxLiveGeometry = 0, idleGeometry = 0, maxBoundsPoints = 0;
        for (int i = 0; i < 8; ++i) {
            invalidate();
            paint();
            idle.push_back(lastPaintMilliseconds);
            idleGeometry += geometryPointsBuilt + inkRebuilds;
        }
        tool = 1;
        const double left = pageLeft(0), top = header() + topOf(0) - doc()->scroll;
        down(left + 280, top + 250, false, .5, true);
        for (int frame = 0; frame < 150; ++frame) {
            for (int j = 0; j < 8; ++j) {
                const double t = (frame * 8 + j) * .025;
                move(left + 200 + std::cos(t) * 80, top + 250 + std::sin(t) * 60, .5 + .3 * std::sin(t),
                     true);
            }
            invalidate();
            paint();
            writing.push_back(lastPaintMilliseconds);
            maxLiveGeometry = std::max(maxLiveGeometry, geometryPointsBuilt);
            maxBoundsPoints = std::max(maxBoundsPoints, liveBoundsPointsVisited);
            rebuiltDuringWriting += inkRebuilds;
        }
        finishGesture();
        invalidate();
        paint();
        const double commitMs = lastPaintMilliseconds;
        const size_t commitGeometry = geometryPointsBuilt;
        inkValid = false;
        invalidate();
        paint(); // Synchronize tiles after the newly committed stroke before measuring reuse.
        // Exercise the same viewport changes used by both scrolling animations.
        // Existing stroke geometry must be reused even though the visible layer moves.
        std::vector<double> scrolling;
        size_t scrollGeometry = 0;
        const size_t rastersBeforeScroll = inkTiles.updates;
        for (int frame = 0; frame < 60; ++frame) {
            doc()->scroll = 120 * (1 - std::cos(frame * .1));
            clampScroll();
            invalidate();
            paint();
            scrolling.push_back(lastPaintMilliseconds);
            scrollGeometry += geometryPointsBuilt;
        }
        auto percentile = [](std::vector<double> values) {
            std::sort(values.begin(), values.end());
            return values[size_t((values.size() - 1) * .95)];
        };
        const double idleP95 = percentile(idle), writingP95 = percentile(writing),
                     scrollP95 = percentile(scrolling);
        const size_t scrollRasters = inkTiles.updates - rastersBeforeScroll;
        // More than the old 512-entry limit, using half-page-wide mouse strokes.
        std::vector<Stroke> dense;
        for (int i = 0; i < 768; ++i) {
            Stroke stroke{++nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 1.5, {}};
            for (int j = 0; j < 24; ++j)
                stroke.points.push_back({45 + j * 14.0, 50 + (i % 48) * 13.0 + std::sin(j * .5 + i) * 6, 1.5});
            dense.push_back(std::move(stroke));
        }
        doc()->history.reset(std::move(dense));
        doc()->scroll = 0;
        inkValid = false;
        invalidate();
        paint();
        doc()->scroll = 160;
        invalidate();
        paint();
        const auto denseBefore = inkTiles.updates;
        std::vector<double> denseFrames;
        for (int frame = 0; frame < 24; ++frame) {
            doc()->scroll = 80 * (1 - std::cos(frame * .3));
            invalidate();
            paint();
            denseFrames.push_back(lastPaintMilliseconds);
        }
        const double denseP95 = percentile(denseFrames);
        const auto denseUpdates = inkTiles.updates - denseBefore;
        const bool ok = inkTarget && liveTarget && idleGeometry == 0 && rebuiltDuringWriting == 0 &&
                        maxLiveGeometry <= 32 && maxBoundsPoints <= 8 && commitGeometry < 1600 && writingP95 < 100 && commitMs < 500 &&
                        scrollGeometry == 0 && scrollRasters == 0 && scrollP95 < 100 &&
                        inkTiles.bytes <= InkTiles::budget && denseUpdates == 0 && denseP95 < 33;
        std::ofstream report("performance-results.json");
        report << "{\n  \"passed\": " << (ok ? "true" : "false") << ",\n  \"seeded_points\": 12800,\n"
               << "  \"new_samples\": 1200,\n  \"initial_ms\": " << initialMs
               << ",\n  \"idle_p95_ms\": " << idleP95 << ",\n  \"writing_p95_ms\": " << writingP95
               << ",\n  \"commit_ms\": " << commitMs << ",\n  \"idle_geometry_work\": " << idleGeometry
               << ",\n  \"completed_layer_rebuilds_during_writing\": " << rebuiltDuringWriting
               << ",\n  \"max_new_geometry_points_per_frame\": " << maxLiveGeometry
               << ",\n  \"max_live_bounds_points_per_frame\": " << maxBoundsPoints
               << ",\n  \"commit_geometry_points\": " << commitGeometry
               << ",\n  \"scrolling_p95_ms\": " << scrollP95
               << ",\n  \"scrolling_geometry_points\": " << scrollGeometry
               << ",\n  \"scrolling_raster_builds\": " << scrollRasters
               << ",\n  \"ink_tile_bytes\": " << inkTiles.bytes
               << ",\n  \"dense_strokes\": 768,\n  \"dense_scroll_p95_ms\": " << denseP95
               << ",\n  \"dense_scroll_tile_updates\": " << denseUpdates << "\n}\n";
        sessions.clear();
        active = -1;
        return ok;
    }
    void cancelGesture() {
        stopInertia();
        penContact.end();
        mouseContact = false;
        activePointer = 0;
        gesture = Gesture::None;
        gesturePage = -1;
        erased.clear();
        shapes.clear();
        invalidate();
    }
    void deleteSelected() {
        if (auto *s = doc(); s && selected && !s->hidden) {
            auto next = s->history.current();
            std::erase_if(next, [&](const Stroke &st) { return st.id == *selected; });
            s->history.commit(std::move(next));
            recoverLater(*s);
            selected.reset();
            shapes.clear();
            invalidate();
        }
    }
    void scroll(double delta, double x, double y, double time = inputTime()) {
        if (closePrompt) {
            stopInertia();
            return;
        }
        if (toolbarContains(x, y)) {
            stopInertia();
            auto r = toolbar();
            panelScroll = std::clamp(panelScroll - delta * 36, 0.0,
                                     std::max(0.0, toolbarContentHeight() - double(r.bottom - r.top)));
            updateTooltips();
            invalidate();
            return;
        }
        if (GetKeyState(VK_CONTROL) < 0) {
            stopInertia();
            if (doc())
                setScale(doc()->scale * (delta > 0 ? 1.1 : 1 / 1.1));
            return;
        }
        if (auto *s = doc(); s && !s->loading && gesture == Gesture::None && delta != 0) {
            const bool animating = momentum.active() || wheel.active();
            momentum.stop();
            wheel.add(-delta * 90, time);
            // Repeated wheel messages must not reset the running timer and starve frames.
            if (!animating)
                SetTimer(window, 2, 16, nullptr);
        }
    }
    void result(std::shared_ptr<Result> r) {
        RuntimeStage stage(RuntimeThread::Ui, "ui.pdf-result");
        auto i = std::find_if(sessions.begin(), sessions.end(),
                              [&](const Session &s) { return s.id == r->document; });
        if (i == sessions.end()) {
            if (!r->document && !r->error.empty())
                MessageBoxW(window, r->error.c_str(), L"引擎初始化", MB_OK | MB_ICONERROR);
            return;
        }
        if (r->renderTicket && r->renderTicket != i->pendingTicket)
            return;
        if (r->type == Result::Type::Opened) {
            i->loading = false;
            i->pages = std::move(r->info.pages);
            i->history.reset(std::move(r->info.strokes));
            i->sourceStamp = r->info.sourceStamp;
            i->sourceSize = r->info.sourceSize;
            rememberRecent(i->path);
            if (storage)
                if (auto view = storage->readView(i->path)) {
                    i->page = std::min(view->page, int(i->pages.size()) - 1);
                    i->scale = view->scale;
                    i->scroll = view->scroll;
                    i->horizontal = view->horizontal;
                    i->fit = view->fit;
                    i->hidden = view->hidden;
                }
            if (storage) {
                if (auto recovered = storage->read(i->path)) {
                    if (recovered->sourceStamp == i->sourceStamp && recovered->sourceSize == i->sourceSize) {
                        History previous;
                        previous.reset(recovered->strokes);
                        if (previous.fingerprint() != i->history.saved &&
                            MessageBoxW(window, L"发现此 PDF 上次未保存的笔记，是否恢复？", L"恢复笔记",
                                        MB_YESNO | MB_ICONQUESTION) == IDYES)
                            i->history.commit(std::move(recovered->strokes));
                    } else {
                        i->preserveRecovery = true;
                        MessageBoxW(window,
                                    L"发现恢复记录，但原 PDF "
                                    L"已发生变化。旧记录将保留；请先核对文件。当前新笔记请手动保存。",
                                    L"恢复记录", MB_OK | MB_ICONINFORMATION);
                    }
                }
            }
            for (const auto &st : i->history.current())
                nextStroke = std::max(nextStroke, st.id);
            notice = L"";
            layout();
        } else if (r->type == Result::Type::Rendered) {
            i->pending = -1;
            i->pendingTicket = 0;
            if (std::abs(std::clamp(i->scale * dpi(), .1, 4.0) - r->bitmap.scale) < .005) {
                int page = r->bitmap.page;
                i->cache[page] = {std::move(r->bitmap), {}};
            }
            evict();
            requestPage();
            invalidate();
        } else if (r->type == Result::Type::Saved) {
            i->saving = false;
            i->history.saved = r->saveFingerprint;
            if (storage && i->path != r->path)
                storage->erase(i->path);
            i->path = r->path;
            rememberRecent(i->path);
            i->sourceStamp = r->sourceStamp;
            i->sourceSize = r->sourceSize;
            i->preserveRecovery = false;
            recoverLater(*i);
            i->cache.clear();
            i->pending = -1;
            i->pendingTicket = 0;
            notice = i->history.dirty() ? L"已保存，仍有新的修改。" : L"已保存到 PDF";
            bool closing = i->closing;
            i->closing = false;
            if (closing) {
                int index = int(i - sessions.begin());
                closeDocument(index);
                if (quitting)
                    closeWindow();
            } else {
                requestPage();
                invalidate();
            }
        } else {
            if (i->loading && i->fromRecent && missingFile(i->path)) {
                const auto path = i->path;
                const int index = int(i - sessions.begin());
                sessions.erase(i);
                if (active == index)
                    active = sessions.empty() ? -1 : std::min(index, int(sessions.size() - 1));
                else if (active > index)
                    --active;
                notice.clear();
                forgetRecent(path);
                layout();
                return;
            }
            i->saving = false;
            i->closing = false;
            i->pending = -1;
            i->pendingTicket = 0;
            quitting = false;
            notice = L"操作失败，笔记仍保留在内存中。";
            MessageBoxW(window, r->error.c_str(), L"InkHorn", MB_OK | MB_ICONERROR);
            if (i->loading) {
                int n = int(i - sessions.begin());
                sessions.erase(i);
                active = sessions.empty() ? -1 : std::min(n, int(sessions.size() - 1));
                layout();
            }
            invalidate();
        }
    }
    void closePanel() {
        if (panel) {
            auto closing = panel;
            panel = nullptr;
            popupDragging = false;
            colorDrag = 0;
            DestroyWindow(closing);
            popupBrush.Reset();
            popupTarget.Reset();
            if (popupDC && popupOldBitmap)
                SelectObject(popupDC, popupOldBitmap);
            if (popupBitmap)
                DeleteObject(popupBitmap);
            if (popupDC)
                DeleteDC(popupDC);
            popupDC = nullptr;
            popupBitmap = nullptr;
            popupOldBitmap = nullptr;
        }
        persistSettings();
        invalidate();
    }
    void chooseColor(int index) {
        customColorIndex = index;
        draftColor = colorHsv(palette[index]);
        draftHex = colorHex(palette[index]);
        hexFocused = hexSelected = false;
        openPopup(PanelKind::Color);
    }
    double popupWidth() const {
        return panelKind == PanelKind::Tool
                   ? 232
                   : (panelKind == PanelKind::Files ? std::min(420.0, width() - 24) : 328);
    }
    double popupHeight() const {
        if (panelKind == PanelKind::About)
            return 284;
        if (panelKind == PanelKind::Color)
            return 320;
        if (panelKind == PanelKind::Functions || panelKind == PanelKind::Files)
            return std::min(menuHeight() + 32, std::max(160.0, bodyHeight() - 16));
        return tool == 5 ? 376 : (tool == 3 ? 220 : 280);
    }
    void commitCustomColor() {
        Color c;
        if (hexFocused && !parseColorHex(draftHex, c)) {
            MessageBeep(MB_ICONWARNING);
            return;
        }
        if (hexFocused)
            draftColor = colorHsv(c);
        palette[customColorIndex] = hsvColor(draftColor);
        colors[tool] = palette[customColorIndex];
        if (tool == 4)
            colors[tool].a = 72;
        openPanel();
        persistSettings();
        invalidate();
    }
    void colorPointer(double x, double y, bool down) {
        if (down) {
            colorDrag = 0;
            if (x >= 24 && x <= 260 && y >= 64 && y <= 214)
                colorDrag = 1;
            else if (x >= 270 && x <= 312 && y >= 58 && y <= 220)
                colorDrag = 2;
            else if (x >= 24 && x <= 176 && y >= 246 && y <= 286) {
                hexFocused = true;
                hexSelected = true;
                InvalidateRect(panel, nullptr, FALSE);
                return;
            } else if (x >= 228 && x <= 304 && y >= 246 && y <= 286) {
                commitCustomColor();
                return;
            } else if (x >= 24 && x <= 48 && y >= 18 && y <= 46) {
                openPanel();
                return;
            }
        }
        if (colorDrag == 1) {
            draftColor.saturation = std::clamp((x - 24) / 236, 0.0, 1.0);
            draftColor.value = 1 - std::clamp((y - 64) / 150, 0.0, 1.0);
        } else if (colorDrag == 2)
            draftColor.hue = std::clamp((y - 64) / 150, 0.0, 1.0) * 359.999;
        else
            return;
        hexFocused = hexSelected = false;
        draftHex = colorHex(hsvColor(draftColor));
        if (panel)
            InvalidateRect(panel, nullptr, FALSE);
    }
    void paintColor(ID2D1RenderTarget *rt, ID2D1SolidColorBrush *b) {
        auto set = [&](Color c) { b->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f)); };
        auto text = [&](const std::wstring &label, D2D1_RECT_F box, Color c, bool compact = false) {
            set(c);
            rt->DrawTextW(label.c_str(), UINT32(label.size()), compact ? smallFormat.Get() : format.Get(),
                          box, b, D2D1_DRAW_TEXT_OPTIONS_CLIP);
        };
        icons.draw(rt, 12, D2D1::RectF(22, 22, 44, 44), Theme::text);
        text(customColorIndex == 7 ? L"自定义颜色" : L"修改预设颜色", D2D1::RectF(48, 25, 300, 49),
             Theme::text);
        const auto square = D2D1::RoundedRect(D2D1::RectF(24, 64, 260, 214), 12, 12);
        const auto hue = hsvColor({draftColor.hue, 1, 1});
        set(hue);
        rt->FillRoundedRectangle(square, b);
        auto gradient = [&](const std::vector<D2D1_GRADIENT_STOP> &stops, D2D1_POINT_2F start,
                            D2D1_POINT_2F end, D2D1_ROUNDED_RECT area) {
            ComPtr<ID2D1GradientStopCollection> collection;
            ComPtr<ID2D1LinearGradientBrush> brush;
            if (SUCCEEDED(rt->CreateGradientStopCollection(stops.data(), UINT32(stops.size()), D2D1_GAMMA_2_2,
                                                           D2D1_EXTEND_MODE_CLAMP, &collection)) &&
                SUCCEEDED(rt->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(start, end),
                                                        collection.Get(), &brush)))
                rt->FillRoundedRectangle(area, brush.Get());
        };
        gradient({{0, D2D1::ColorF(1, 1, 1, 1)}, {1, D2D1::ColorF(1, 1, 1, 0)}}, D2D1::Point2F(24, 64),
                 D2D1::Point2F(260, 64), square);
        gradient({{0, D2D1::ColorF(0, 0, 0, 0)}, {1, D2D1::ColorF(0, 0, 0, 1)}}, D2D1::Point2F(24, 64),
                 D2D1::Point2F(24, 214), square);
        std::vector<D2D1_GRADIENT_STOP> spectrum;
        for (int i = 0; i <= 6; ++i) {
            const auto c = hsvColor({double(i) * 60, 1, 1});
            spectrum.push_back({i / 6.f, D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f)});
        }
        gradient(spectrum, D2D1::Point2F(290, 64), D2D1::Point2F(290, 214),
                 D2D1::RoundedRect(D2D1::RectF(284, 64, 296, 214), 6, 6));
        const auto location =
            D2D1::Point2F(float(24 + draftColor.saturation * 236), float(64 + (1 - draftColor.value) * 150));
        set({255, 255, 255, 255});
        rt->DrawEllipse(D2D1::Ellipse(location, 6, 6), b, 2);
        set({55, 65, 85, 255});
        rt->DrawEllipse(D2D1::Ellipse(location, 7.5f, 7.5f), b, .8f);
        const float yy = float(64 + draftColor.hue / 360 * 150);
        set({255, 255, 255, 255});
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(290, yy), 10, 10), b);
        set(hue);
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(290, yy), 7, 7), b);
        text(L"HEX", D2D1::RectF(26, 226, 100, 244), Theme::muted, true);
        set(Theme::inset);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24, 246, 176, 286), 12, 12), b);
        if (hexFocused) {
            set(hexSelected ? Color{225, 233, 249, 255} : Theme::accent);
            if (hexSelected)
                rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(36, 254, 132, 279), 5, 5), b);
            else
                rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(24, 246, 176, 286), 12, 12), b, 1);
        }
        text(draftHex, D2D1::RectF(38, 256, 166, 283), Theme::text);
        set(hsvColor(draftColor));
        rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(200, 266), 13, 13), b);
        set(Theme::accent);
        rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(228, 246, 304, 286), 13, 13), b);
        text(L"完成", D2D1::RectF(252, 256, 298, 284), {255, 255, 255, 255});
    }
    bool popupKey(WPARAM key) {
        if (!panel || panelKind == PanelKind::Tool)
            return false;
        if (panelKind == PanelKind::About) {
            if (key == VK_ESCAPE || key == VK_RETURN)
                closePanel();
            return true;
        }
        if (panelKind == PanelKind::Color) {
            if (key == VK_ESCAPE)
                openPanel();
            else if (key == VK_RETURN)
                commitCustomColor();
            else if (key == VK_BACK && hexFocused) {
                if (hexSelected)
                    draftHex.clear();
                else if (!draftHex.empty())
                    draftHex.pop_back();
                hexSelected = false;
                InvalidateRect(panel, nullptr, FALSE);
            } else if (key == 'A' && GetKeyState(VK_CONTROL) < 0 && hexFocused) {
                hexSelected = true;
                InvalidateRect(panel, nullptr, FALSE);
            } else if (key == 'V' && GetKeyState(VK_CONTROL) < 0 && hexFocused && OpenClipboard(window)) {
                auto data = GetClipboardData(CF_UNICODETEXT);
                const auto *value = data ? static_cast<const wchar_t *>(GlobalLock(data)) : nullptr;
                if (value) {
                    const size_t limit = std::min<size_t>(9, GlobalSize(data) / sizeof(wchar_t));
                    size_t length = 0;
                    while (length < limit && value[length])
                        ++length;
                    Color parsed;
                    if (parseColorHex(std::wstring(value, length), parsed)) {
                        draftColor = colorHsv(parsed);
                        draftHex = colorHex(parsed);
                        hexSelected = false;
                    } else
                        MessageBeep(MB_ICONWARNING);
                    GlobalUnlock(data);
                }
                CloseClipboard();
                InvalidateRect(panel, nullptr, FALSE);
            }
            return true;
        }
        if (key == VK_ESCAPE) {
            closePanel();
            return true;
        }
        if (key == VK_RETURN) {
            if (menuFocus >= 0 && size_t(menuFocus) < menuRows.size() &&
                menuRows[size_t(menuFocus)].enabled && !menuRows[size_t(menuFocus)].heading)
                runCommand(menuRows[size_t(menuFocus)].command);
            return true;
        }
        if (key == VK_UP || key == VK_DOWN) {
            const int count = int(menuRows.size()), direction = key == VK_DOWN ? 1 : -1;
            for (int i = 0; i < count; ++i) {
                menuFocus = (menuFocus + count + direction) % count;
                if (menuRows[size_t(menuFocus)].enabled && !menuRows[size_t(menuFocus)].heading)
                    break;
            }
            menuHover = menuFocus;
            double top = 0;
            for (int i = 0; i < menuFocus; ++i)
                top += menuRowHeight(menuRows[size_t(i)]);
            const double bottom = top + menuRowHeight(menuRows[size_t(menuFocus)]);
            if (top < menuScroll)
                menuScroll = top;
            if (bottom > menuScroll + popupHeight() - 32)
                menuScroll = bottom - (popupHeight() - 32);
            InvalidateRect(panel, nullptr, FALSE);
            return true;
        }
        return false;
    }
    D2D1_RECT_F popupTrack() const {
        return D2D1::RectF(176, 52, 188, tool == 5 ? 326.f : (tool == 3 ? 164.f : 230.f));
    }
    std::pair<double, double> widthRange() const {
        return tool == 3 ? std::pair{4.0, 80.0} : (tool == 4 ? std::pair{2.0, 32.0} : std::pair{.25, 8.0});
    }
    void movePanelSlider(double y) {
        const auto r = popupTrack();
        const auto [lo, hi] = widthRange();
        const double value = std::clamp((r.bottom - y) / (r.bottom - r.top), 0.0, 1.0);
        // Logarithmic travel gives thin pens more usable adjustment space.
        widths[tool] = lo * std::pow(hi / lo, value);
        if (panel)
            InvalidateRect(panel, nullptr, FALSE);
        invalidate();
    }
    void panelInput(double x, double y, bool right = false) {
        if (panelKind == PanelKind::About) {
            if (x >= 24 && x <= 304 && y >= 220 && y <= 260)
                closePanel();
            return;
        }
        if (panelKind == PanelKind::Functions || panelKind == PanelKind::Files) {
            const int index = menuHit(x, y);
            if (index >= 0)
                runCommand(menuRows[size_t(index)].command);
            return;
        }
        if (panelKind == PanelKind::Color) {
            colorPointer(x, y, true);
            return;
        }
        const auto track = popupTrack();
        if (!right && x >= 158 && x <= 208 && y >= track.top - 14 && y <= track.bottom + 14) {
            popupDragging = true;
            movePanelSlider(y);
            return;
        }
        if (tool != 3)
            for (int i = 0; i < 8; ++i) {
                const double cx = 46 + (i % 2) * 54, cy = 62 + (i / 2) * 42;
                if (std::hypot(x - cx, y - cy) > 19)
                    continue;
                if (i == 7 || right)
                    chooseColor(i);
                else {
                    colors[tool] = palette[i];
                    if (tool == 4)
                        colors[tool].a = 72;
                }
                if (panel)
                    InvalidateRect(panel, nullptr, FALSE);
                persistSettings();
                invalidate();
                return;
            }
        if (tool == 5 && !right)
            for (int i = 0; i < 4; ++i) {
                const double xx = 24 + (i % 2) * 54, yy = 266 + (i / 2) * 42;
                if (x < xx || x > xx + 44 || y < yy || y > yy + 36)
                    continue;
                const Kind choices[] = {Kind::Rectangle, Kind::Ellipse, Kind::Line, Kind::Arrow};
                shapeKind = choices[i];
                InvalidateRect(panel, nullptr, FALSE);
                persistSettings();
                invalidate();
                return;
            }
    }
    void paintPanel() {
        if (!panel || !popupTarget || !popupDC)
            return;
        auto *rt = popupTarget.Get();
        auto *b = popupBrush.Get();
        const float h = float(popupHeight());
        const float w = float(popupWidth());
        auto set = [&](Color c) {
            b->SetColor(D2D1::ColorF(c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f));
        };
        auto rounded = [&](D2D1_RECT_F r, Color c, float radius) {
            set(c);
            rt->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), b);
        };
        auto dot = [&](float x, float y, float radius, Color c) {
            set(c);
            rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), radius, radius), b);
        };
        rt->BeginDraw();
        rt->SetTransform(D2D1::Matrix3x2F::Identity());
        rt->Clear(D2D1::ColorF(0, 0, 0, 0));
        for (int spread = 7; spread >= 1; --spread)
            rounded(D2D1::RectF(8.f - spread, 10.f - spread, w - 8 + spread, h - 6 + spread), {43, 55, 79, 4},
                    20.f + spread);
        rounded(D2D1::RectF(8, 8, w - 8, h - 8), Theme::surface, 20);
        if (panelKind == PanelKind::Functions || panelKind == PanelKind::Files)
            paintMenu(rt, b);
        else if (panelKind == PanelKind::Color)
            paintColor(rt, b);
        else if (panelKind == PanelKind::About) {
            set(Theme::text);
            rt->DrawTextW(L"InkHorn", 7, format.Get(), D2D1::RectF(26, 26, w - 26, 54), b);
            const wchar_t *lines[] = {L"轻量 PDF 阅读与批注", L"在纸面书写，在两侧空白拖动翻阅。",
                                      L"再次点击已选工具可以调整参数。", L"笔迹写入 PDF，可直接分享文件。",
                                      L"Ctrl+O 打开 · Ctrl+S 保存 · F11 全屏"};
            set(Theme::muted);
            for (int i = 0; i < 5; ++i)
                rt->DrawTextW(lines[i], UINT32(wcslen(lines[i])), smallFormat.Get(),
                              D2D1::RectF(26, 66.f + i * 26, w - 26, 92.f + i * 26), b);
            rounded(D2D1::RectF(24, 220, 304, 260), Theme::inset, 13);
            set(Theme::text);
            rt->DrawTextW(L"关闭", 2, format.Get(), D2D1::RectF(150, 230, 196, 255), b);
        } else {
            const wchar_t *titles[] = {L"指针", L"笔 1", L"笔 2", L"橡皮", L"荧光笔", L"形状"};
            set({96, 107, 128, 255});
            rt->DrawTextW(titles[tool], UINT32(wcslen(titles[tool])), smallFormat.Get(),
                          D2D1::RectF(26, 20, 180, 42), b);
            Color current = tool == 3 ? Color{113, 124, 145, 255} : colors[tool];
            current.a = 255;
            if (tool != 3)
                for (int i = 0; i < 8; ++i) {
                    const float x = 46.f + (i % 2) * 54, y = 62.f + (i / 2) * 42;
                    const auto c = palette[i];
                    const bool selectedColor = current.r == c.r && current.g == c.g && current.b == c.b;
                    if (selectedColor) {
                        dot(x, y, 19, tintSurface(c, .24));
                        dot(x, y, 16.5f, {255, 255, 255, 255});
                    }
                    dot(x, y, 14, c);
                    if (i == 7) {
                        set(int(c.r) * 299 + int(c.g) * 587 + int(c.b) * 114 > 145000
                                ? Color{55, 64, 80, 255}
                                : Color{255, 255, 255, 255});
                        rt->DrawLine(D2D1::Point2F(x - 4, y), D2D1::Point2F(x + 4, y), b, 1.5f,
                                     roundStyle.Get());
                        rt->DrawLine(D2D1::Point2F(x, y - 4), D2D1::Point2F(x, y + 4), b, 1.5f,
                                     roundStyle.Get());
                    }
                }
            const auto track = popupTrack();
            const auto [lo, hi] = widthRange();
            const float position =
                float(std::clamp(std::log(widths[tool] / lo) / std::log(hi / lo), 0.0, 1.0));
            const float yy = track.bottom - position * (track.bottom - track.top);
            rounded(D2D1::RectF(track.left, track.top - 6, track.right, track.bottom + 6),
                    {237, 241, 247, 255}, 6);
            rounded(D2D1::RectF(track.left, yy - 6, track.right, track.bottom + 6), tintSurface(current, .36),
                    6);
            dot(182, yy + 2, 15, {51, 70, 100, 12});
            dot(182, yy, 14, {255, 255, 255, 255});
            dot(182, yy, 10.5f, current);
            dot(179, yy - 3, 2, {255, 255, 255, 75});
            if (tool == 5)
                for (int i = 0; i < 4; ++i) {
                    const float x = 24.f + (i % 2) * 54, y = 266.f + (i / 2) * 42;
                    const Kind choices[] = {Kind::Rectangle, Kind::Ellipse, Kind::Line, Kind::Arrow};
                    const int glyphs[] = {5, 9, 10, 11};
                    rounded(D2D1::RectF(x, y, x + 44, y + 36),
                            shapeKind == choices[i] ? tintSurface(current) : Color{247, 249, 252, 255}, 12);
                    if (!icons.draw(rt, glyphs[i], D2D1::RectF(x + 11, y + 7, x + 33, y + 29), current))
                        toolGlyph(rt, b, roundStyle.Get(), glyphs[i],
                                  D2D1::RectF(x + 11, y + 7, x + 33, y + 29), current);
                }
            // Row five belongs to the two-column color area, alongside the slider.
            const float previewY = tool == 3 ? 112.f : 230.f;
            rounded(D2D1::RectF(26, previewY - 20, 120, previewY + 20), {247, 249, 252, 255}, 18);
            const double previewScale = (tool == 1 || tool == 2 || tool == 5) ? 3.0 : 1.0;
            const float radius = float(std::clamp(widths[tool] * previewScale / 2, .5, 20.0));
            if (tool == 3) {
                set(current);
                rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(73, previewY), radius, radius), b, 1.7f);
            } else {
                auto preview = current;
                if (tool == 4)
                    preview = tintSurface(current, .5);
                dot(73, previewY, radius, preview);
            }
        }
        if (FAILED(rt->EndDraw()))
            return;
        RECT rect{};
        GetWindowRect(panel, &rect);
        POINT destination{rect.left, rect.top}, source{};
        SIZE size{rect.right - rect.left, rect.bottom - rect.top};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(panel, nullptr, &destination, &size, popupDC, &source, 0, &blend, ULW_ALPHA);
    }
    static LRESULT CALLBACK panelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
        auto *a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        }
        if (!a)
            return DefWindowProcW(h, m, w, l);
        if (mouseInputMessage(m) && (!a->mouseInput(m) || a->panel != h))
            return 0;
        if (a->diagnostics.message(m, w, l, "panel"))
            a->invalidate();
        switch (m) {
        case WM_SETCURSOR:
            if (!a->smoke)
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            BeginPaint(h, &ps);
            a->paintPanel();
            EndPaint(h, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN: {
            a->panelInput(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi(), m == WM_RBUTTONDOWN);
            if (a->popupDragging || a->colorDrag)
                SetCapture(h);
            return 0;
        }
        case WM_MOUSEMOVE:
            if (a->panelKind == PanelKind::Functions || a->panelKind == PanelKind::Files) {
                const int index = a->menuHit(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
                if (index != a->menuHover) {
                    a->menuHover = index;
                    InvalidateRect(h, nullptr, FALSE);
                }
            } else if (a->panelKind == PanelKind::Color && a->colorDrag)
                a->colorPointer(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi(), false);
            else if (a->popupDragging)
                a->movePanelSlider(GET_Y_LPARAM(l) / a->dpi());
            return 0;
        case WM_LBUTTONUP:
            if (a->popupDragging)
                a->movePanelSlider(GET_Y_LPARAM(l) / a->dpi());
            if (a->colorDrag)
                a->colorPointer(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi(), false);
            a->popupDragging = false;
            a->colorDrag = 0;
            ReleaseCapture();
            a->persistSettings();
            return 0;
        case WM_CAPTURECHANGED:
            a->popupDragging = false;
            a->colorDrag = 0;
            return 0;
        case WM_MOUSEWHEEL:
            if (a->panelKind == PanelKind::Functions || a->panelKind == PanelKind::Files)
                a->popupMenuScroll(-GET_WHEEL_DELTA_WPARAM(w) / 120.0 * 72);
            return 0;
        case WM_POINTERENTER: {
            POINTER_INPUT_TYPE type{};
            if (GetPointerType(GET_POINTERID_WPARAM(w), &type) && type == PT_PEN && !a->mouseContact)
                a->usePenInput();
            return 0;
        }
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            POINTER_INFO info{};
            if (!GetPointerInfo(GET_POINTERID_WPARAM(w), &info))
                return 0;
            if (info.pointerType == PT_PEN) {
                if (!a->mouseContact || m == WM_POINTERDOWN)
                    a->usePenInput();
                POINTER_PEN_INFO pen{};
                if (GetPointerPenInfo(GET_POINTERID_WPARAM(w), &pen) && a->diagnostics.pen(m, pen, "panel"))
                    a->invalidate();
            }
            POINT p = info.ptPixelLocation;
            ScreenToClient(h, &p);
            if (m == WM_POINTERDOWN) {
                if (a->gesture != Gesture::None)
                    a->finishGesture();
                a->penContact.end();
                a->mouseContact = false;
                a->activePointer = 0;
                POINTER_PEN_INFO pen{};
                const bool right = info.pointerType == PT_PEN &&
                                   GetPointerPenInfo(GET_POINTERID_WPARAM(w), &pen) &&
                                   (pen.penFlags & PEN_FLAG_BARREL) != 0;
                a->panelInput(p.x / a->dpi(), p.y / a->dpi(), right);
            } else if ((info.pointerFlags & POINTER_FLAG_INCONTACT) && a->panelKind == PanelKind::Color &&
                       a->colorDrag)
                a->colorPointer(p.x / a->dpi(), p.y / a->dpi(), false);
            else if ((info.pointerFlags & POINTER_FLAG_INCONTACT) && a->popupDragging)
                a->movePanelSlider(p.y / a->dpi());
            if (m == WM_POINTERUP) {
                a->popupDragging = false;
                a->colorDrag = 0;
                a->persistSettings();
            }
            return 0;
        }
        case WM_POINTERCAPTURECHANGED:
            a->popupDragging = false;
            a->colorDrag = 0;
            return 0;
        case WM_KEYDOWN:
            if (a->popupKey(w))
                return 0;
            if (w == VK_ESCAPE) {
                a->closePanel();
                return 0;
            }
            break;
        case WM_CLOSE:
            a->closePanel();
            return 0;
        }
        return DefWindowProcW(h, m, w, l);
    }
    void openPanel() {
        openPopup(PanelKind::Tool);
    }
    void openPopup(PanelKind kind) {
        closePanel();
        if (kind == PanelKind::Tool && !configurableTool(tool))
            return;
        panelKind = kind;
        if (kind == PanelKind::Functions || kind == PanelKind::Files)
            buildMenu();
        auto r = toolbar();
        const double w = popupWidth();
        const bool fromHeader = kind != PanelKind::Tool && kind != PanelKind::Color;
        const double x = fromHeader ? (kind == PanelKind::Files ? fileLeft() - 8 : 2)
                                    : (r.left - w - 8 >= 8 ? r.left - w - 8 : r.right + 8);
        POINT p{int(x * dpi()), int((fromHeader ? header() - 2 : r.top - 8) * dpi())};
        ClientToScreen(window, &p);
        const SIZE pixels{LONG(std::ceil(w * dpi())), LONG(std::ceil(popupHeight() * dpi()))};
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor)) {
            p.x = std::clamp(p.x, monitor.rcWork.left,
                             std::max(monitor.rcWork.left, monitor.rcWork.right - pixels.cx));
            p.y = std::clamp(p.y, monitor.rcWork.top,
                             std::max(monitor.rcWork.top, monitor.rcWork.bottom - pixels.cy));
        }
        panel =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE, L"InkHornPanel", L"工具设置",
                            WS_POPUP, p.x, p.y, pixels.cx, pixels.cy, window, nullptr, instance, this);
        if (!panel)
            return;
        popupDC = CreateCompatibleDC(nullptr);
        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = pixels.cx;
        bitmapInfo.bmiHeader.biHeight = -pixels.cy;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;
        void *bits = nullptr;
        popupBitmap = CreateDIBSection(popupDC, &bitmapInfo, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (popupDC && popupBitmap)
            popupOldBitmap = SelectObject(popupDC, popupBitmap);
        const auto properties = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_SOFTWARE,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), float(96 * dpi()),
            float(96 * dpi()));
        const RECT bounds{0, 0, pixels.cx, pixels.cy};
        if (!popupDC || !popupBitmap || FAILED(factory->CreateDCRenderTarget(&properties, &popupTarget)) ||
            FAILED(popupTarget->BindDC(popupDC, &bounds)) ||
            FAILED(popupTarget->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0), &popupBrush))) {
            closePanel();
            notice = L"无法创建菜单面板。";
            return;
        }
        popupTarget->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        popupTarget->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        paintPanel();
        ShowWindow(panel, smoke ? SW_HIDE : SW_SHOWNOACTIVATE);
        invalidate();
    }
    static LRESULT CALLBACK editProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR data) {
        auto *a = reinterpret_cast<App *>(data);
        if (mouseInputMessage(m) && !a->mouseInput(m))
            return 0;
        if ((m == WM_POINTERENTER || m == WM_POINTERUPDATE || m == WM_POINTERDOWN || m == WM_POINTERUP) &&
            !a->mouseContact) {
            POINTER_INPUT_TYPE type{};
            if (GetPointerType(GET_POINTERID_WPARAM(w), &type) && type == PT_PEN)
                a->usePenInput();
        }
        if (m == WM_SETTEXT)
            ++a->fieldTextWrites;
        if (a->diagnostics.message(m, w, l, "edit"))
            a->invalidate();
        if (m == WM_LBUTTONDOWN || m == WM_POINTERDOWN || m == WM_SETFOCUS) {
            if (a->panel)
                a->closePanel();
            a->stopInertia();
        }
        if (m == WM_KEYDOWN && (w == VK_RETURN || w == VK_ESCAPE)) {
            a->submitEdit(h, w == VK_RETURN);
            return 0;
        }
        if (m == WM_GETDLGCODE)
            return DLGC_WANTALLKEYS;
        return DefSubclassProc(h, m, w, l);
    }
    static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
        runtimeHeartbeat(m);
        auto *a = reinterpret_cast<App *>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (m == WM_NCCREATE) {
            a = static_cast<App *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
            a->window = h;
            SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(a));
        }
        if (!a)
            return DefWindowProcW(h, m, w, l);
        if (mouseInputMessage(m) && !a->mouseInput(m))
            return 0;
        if (a->diagnostics.message(m, w, l))
            a->invalidate();
        switch (m) {
        case WM_NCCALCSIZE:
            if (w) {
                if (IsZoomed(h) && !a->fullscreen) {
                    MONITORINFO monitor{};
                    monitor.cbSize = sizeof(monitor);
                    if (GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &monitor))
                        reinterpret_cast<NCCALCSIZE_PARAMS *>(l)->rgrc[0] = monitor.rcWork;
                }
            }
            return 0;
        case WM_NCHITTEST: {
            if (a->fullscreen || a->closePrompt)
                return HTCLIENT;
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            double x = p.x / a->dpi(), y = p.y / a->dpi(), ww = a->width(), hh = a->height();
            if (!IsZoomed(h)) {
                bool left = x<6, right = x> ww - 6, top = y<6, bottom = y> hh - 6;
                if (top && left)
                    return HTTOPLEFT;
                if (top && right)
                    return HTTOPRIGHT;
                if (bottom && left)
                    return HTBOTTOMLEFT;
                if (bottom && right)
                    return HTBOTTOMRIGHT;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (bottom)
                    return HTBOTTOM;
                if (top)
                    return HTTOP;
            }
            const auto navigationBox = a->navigation();
            if (y < a->header() && x > a->fileEdge() && x < ww - 138 &&
                (x < navigationBox.left || x > navigationBox.right))
                return HTCAPTION;
            return HTCLIENT;
        }
        case WM_CREATE: {
            const BOOL feedback = FALSE;
            for (const auto kind : {FEEDBACK_PEN_BARRELVISUALIZATION, FEEDBACK_PEN_TAP,
                                    FEEDBACK_PEN_DOUBLETAP, FEEDBACK_PEN_PRESSANDHOLD, FEEDBACK_PEN_RIGHTTAP})
                SetWindowFeedbackSetting(h, kind, 0, sizeof(feedback), &feedback);
            a->fieldBackground = CreateSolidBrush(RGB(244, 246, 250));
            a->pageEdit = CreateWindowExW(
                0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 42, 23, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(pageId)), a->instance, nullptr);
            a->zoomEdit = CreateWindowExW(
                0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL, 0, 0, 72, 23, h,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(zoomId)), a->instance, nullptr);
            for (HWND edit : {a->pageEdit, a->zoomEdit}) {
                SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(a->font), TRUE);
                SetWindowSubclass(edit, editProc, 1, reinterpret_cast<DWORD_PTR>(a));
            }
            DragAcceptFiles(h, TRUE);
            a->tooltips = CreateWindowExW(
                WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, h, nullptr, a->instance, nullptr);
            if (a->tooltips)
                SendMessageW(a->tooltips, TTM_SETMAXTIPWIDTH, 0, 300);
            if (!a->smoke) {
                try {
                    a->storage = std::make_unique<Storage>();
                    a->loadSettings();
                    a->recent = a->storage->readRecent();
                } catch (...) {
                    a->notice = L"无法写入 EXE 旁的 InkHorn-data 文件夹，请检查目录权限并及时保存 PDF。";
                }
                try {
                    a->runtimeMonitor = std::make_unique<RuntimeMonitor>(a->diagnosticDirectory());
                } catch (...) {
                    a->showToast(L"无法创建运行诊断日志，请检查 InkHorn-data 目录权限", true);
                }
            }
            a->engine = std::make_unique<PdfEngine>([h](std::shared_ptr<Result> r) {
                auto *p = new std::shared_ptr<Result>(std::move(r));
                if (!PostMessageW(h, resultMessage, 0, reinterpret_cast<LPARAM>(p)))
                    delete p;
            });
            SetTimer(h, 1, 150, nullptr);
            return 0;
        }
        case WM_SIZE:
            a->stopInertia();
            a->inkBrush.Reset();
            a->inkTarget.Reset();
            a->liveBrush.Reset();
            a->liveTarget.Reset();
            a->inkValid = a->liveValid = false;
            a->inkUnavailable = false;
            if (a->panel)
                a->closePanel();
            if (w == SIZE_MINIMIZED)
                return 0;
            if (a->target)
                a->target->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
            if (a->pageEdit)
                a->layout();
            return 0;
        case WM_DPICHANGED: {
            a->closePanel();
            a->releaseGraphics();
            auto *r = reinterpret_cast<RECT *>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            a->layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto *info = reinterpret_cast<MINMAXINFO *>(l);
            info->ptMinTrackSize = {LONG(680 * a->dpi()), LONG(420 * a->dpi())};
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
            if (reinterpret_cast<HWND>(l) == a->pageEdit || reinterpret_cast<HWND>(l) == a->zoomEdit) {
                auto dc = reinterpret_cast<HDC>(w);
                SetBkColor(dc, RGB(244, 246, 250));
                SetTextColor(dc, RGB(67, 78, 98));
                return reinterpret_cast<LRESULT>(a->fieldBackground);
            }
            break;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) {
                if (a->penPositionKnown && a->penContact.active()) {
                    a->refreshCursor();
                    return TRUE;
                }
                if (reinterpret_cast<HWND>(w) == a->pageEdit || reinterpret_cast<HWND>(w) == a->zoomEdit) {
                    if (!a->smoke)
                        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
                    return TRUE;
                }
                if (a->penPositionKnown && (a->penNear || a->penContact.active())) {
                    a->refreshCursor();
                    return TRUE;
                }
                POINT p{};
                GetCursorPos(&p);
                ScreenToClient(h, &p);
                a->updateCursorAt(p.x / a->dpi(), p.y / a->dpi());
                return TRUE;
            }
            break;
        case WM_PAINT:
            a->paint();
            a->refreshCursor();
            return 0;
        case WM_TIMER:
            if (w == 2) {
                a->animateScroll();
                return 0;
            }
            if (w == 1) {
                a->expireToast(inputTime());
                a->requestPage();
                if (a->storage && !a->storage->lastError().empty()) {
                    a->notice = a->storage->lastError();
                    a->invalidate();
                }
            }
            return 0;
        case resultMessage: {
            auto *p = reinterpret_cast<std::shared_ptr<Result> *>(l);
            a->result(std::move(*p));
            delete p;
            return 0;
        }
        case WM_DROPFILES: {
            auto drop = reinterpret_cast<HDROP>(w);
            UINT n = DragQueryFileW(drop, 0xffffffff, nullptr, 0);
            for (UINT i = 0; i < n; ++i) {
                wchar_t path[32768]{};
                DragQueryFileW(drop, i, path, 32768);
                a->open(path);
            }
            DragFinish(drop);
            return 0;
        }
        case WM_LBUTTONDOWN:
            if (a->penContact.active())
                return 0;
            SetFocus(h);
            a->penNear = a->penTemporaryEraser = false;
            if (a->gesture != Gesture::None)
                a->finishGesture();
            a->down(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
            a->mouseContact = a->gesture != Gesture::None;
            if (a->mouseContact)
                SetCapture(h);
            if (a->mouseContact)
                a->presentInkIfDue();
            return 0;
        case WM_MOUSEMOVE:
            if (a->penContact.active()) {
                a->refreshCursor();
                return 0;
            }
            a->penTemporaryEraser = false;
            a->updateCursorAt(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
            if (a->closePrompt) {
                const int hit = a->closeButtonHit(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
                if (hit != a->closePromptHover) {
                    a->closePromptHover = hit;
                    a->invalidate();
                }
                return 0;
            }
            if (a->mouseContact && (w & MK_LBUTTON) && a->gesture != Gesture::None) {
                a->move(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
                a->presentInkIfDue();
            } else if (!a->mouseContact)
                a->hover(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
            {
                TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, h, 0};
                TrackMouseEvent(&tracking);
            }
            return 0;
        case WM_MOUSELEAVE:
            a->hoverControl = -1;
            a->invalidate();
            return 0;
        case WM_LBUTTONUP:
            if (!a->mouseContact)
                return 0;
            a->releaseAt(GET_X_LPARAM(l) / a->dpi(), GET_Y_LPARAM(l) / a->dpi());
            a->mouseContact = false;
            ReleaseCapture();
            a->presentInkIfDue();
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            a->scroll(GET_WHEEL_DELTA_WPARAM(w) / 120.0, p.x / a->dpi(), p.y / a->dpi());
            return 0;
        }
        case WM_POINTERENTER:
        case WM_POINTERLEAVE: {
            POINTER_INPUT_TYPE type{};
            if (GetPointerType(GET_POINTERID_WPARAM(w), &type) && type == PT_PEN) {
                POINTER_PEN_INFO info{};
                if (GetPointerPenInfo(GET_POINTERID_WPARAM(w), &info)) {
                    if (a->diagnostics.pen(m, info))
                        a->invalidate();
                    a->penInput(m, info);
                }
            }
            return 0;
        }
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP: {
            UINT id = GET_POINTERID_WPARAM(w);
            POINTER_INPUT_TYPE type{};
            if (!GetPointerType(id, &type))
                break;
            if (type == PT_PEN) {
                POINTER_PEN_INFO info{};
                if (!GetPointerPenInfo(id, &info))
                    return 0;
                if (a->diagnostics.pen(m, info))
                    a->invalidate();
                std::vector<POINTER_PEN_INFO> history;
                // Fetch history before focus/popup operations can retrieve another window message.
                if (m == WM_POINTERUPDATE && a->penContact.owns(id) && a->gesture != Gesture::Pan &&
                    a->gesture != Gesture::Toolbar) {
                    UINT32 count = 0;
                    GetPointerPenInfoHistory(id, &count, nullptr);
                    count = std::min<UINT32>(count, 2048);
                    history.resize(count);
                    if (count && GetPointerPenInfoHistory(id, &count, history.data())) {
                        history.resize(std::min<size_t>(count, history.size()));
                    } else
                        history.clear();
                }
                a->penInput(m, info, history);
                return 0;
            }
            if (type == PT_TOUCH && !a->penNear) {
                POINTER_INFO info{};
                if (GetPointerInfo(id, &info)) {
                    auto p = info.ptPixelLocation;
                    ScreenToClient(h, &p);
                    if (m == WM_POINTERDOWN) {
                        if (a->activePointer)
                            return 0;
                        a->activePointer = id;
                        a->stopInertia();
                        if (a->panel) {
                            a->closePanel();
                        }
                        if (!a->doc() || a->closePrompt || p.y / a->dpi() < a->header() ||
                            a->toolbarContains(p.x / a->dpi(), p.y / a->dpi()))
                            a->down(p.x / a->dpi(), p.y / a->dpi());
                        else
                            a->beginPan(p.x / a->dpi(), p.y / a->dpi(), true);
                    } else if (a->activePointer != id)
                        return 0;
                    else if (m == WM_POINTERUP) {
                        a->releaseAt(p.x / a->dpi(), p.y / a->dpi());
                        a->activePointer = 0;
                    } else if (a->gesture == Gesture::Pan || a->gesture == Gesture::Toolbar)
                        a->move(p.x / a->dpi(), p.y / a->dpi());
                }
                return 0;
            }
            break;
        }
        case WM_POINTERCAPTURECHANGED:
            // Windows also sends this after a normal pen-up. Do not cancel its new inertia.
            if (a->gesture != Gesture::None)
                a->cancelGesture();
            a->activePointer = 0;
            return 0;
        case WM_CAPTURECHANGED:
            if (a->mouseContact && a->gesture != Gesture::None)
                a->cancelGesture();
            return 0;
        case WM_CANCELMODE:
            a->cancelGesture();
            a->activePointer = 0;
            return 0;
        case WM_ACTIVATEAPP:
            if (!w) {
                a->penNear = a->penTemporaryEraser = false;
                a->closePanel();
                a->cancelGesture();
            }
            return 0;
        case WM_KEYDOWN: {
            if (a->closePrompt) {
                if (w == VK_ESCAPE)
                    a->answerClosePrompt(0);
                else if (w == VK_RETURN || w == VK_SPACE)
                    a->answerClosePrompt(a->closePromptFocus);
                else if (w == VK_TAB || w == VK_LEFT || w == VK_RIGHT) {
                    const int direction = w == VK_LEFT || (w == VK_TAB && GetKeyState(VK_SHIFT) < 0) ? -1 : 1;
                    a->closePromptFocus = (a->closePromptFocus + 3 + direction) % 3;
                    a->invalidate();
                }
                return 0;
            }
            if (GetKeyState(VK_CONTROL) < 0 && GetKeyState(VK_SHIFT) < 0 && (w == 'D' || w == 'L')) {
                a->closePanel();
                if (w == 'D')
                    a->toggleDiagnostics();
                else
                    a->openDiagnosticDirectory();
                return 0;
            }
            if (a->popupKey(w))
                return 0;
            bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
            if (w == VK_F11) {
                a->toggleFullscreen();
                return 0;
            }
            if (w == VK_ESCAPE) {
                a->stopInertia();
                if (a->panel)
                    a->closePanel();
                else if (a->gesture != Gesture::None)
                    a->cancelGesture();
                else if (a->fullscreen)
                    a->toggleFullscreen();
                return 0;
            }
            if (ctrl) {
                switch (w) {
                case 'O':
                    a->open(a->dialog(false));
                    return 0;
                case 'S':
                    a->save(shift);
                    return 0;
                case 'W':
                    a->closeDocument(a->active);
                    return 0;
                case VK_TAB:
                    if (!a->sessions.empty())
                        a->activate((a->active + int(a->sessions.size()) + (shift ? -1 : 1)) %
                                    int(a->sessions.size()));
                    return 0;
                case 'Z':
                    a->undo(shift);
                    return 0;
                case 'Y':
                    a->undo(true);
                    return 0;
                case 'G':
                    SetFocus(a->pageEdit);
                    SendMessageW(a->pageEdit, EM_SETSEL, 0, -1);
                    return 0;
                }
            }
            if (w == VK_DELETE) {
                a->deleteSelected();
                return 0;
            }
            int t = -1;
            if (w == '1')
                t = 0;
            if (w == '2')
                t = 1;
            if (w == '3')
                t = 2;
            if (w == 'E')
                t = 3;
            if (w == 'H')
                t = 4;
            if (w == 'S')
                t = 5;
            if (t >= 0) {
                a->finishGesture();
                a->tool = t;
                a->selected.reset();
                a->closePanel();
                a->invalidate();
            }
            return 0;
        }
        case WM_CLOSE:
            a->closeWindow();
            return 0;
        case WM_CHAR:
            if (a->panel && a->panelKind == PanelKind::Color && a->hexFocused) {
                const wchar_t character = wchar_t(w);
                if ((character >= L'0' && character <= L'9') || (character >= L'a' && character <= L'f') ||
                    (character >= L'A' && character <= L'F') || character == L'#') {
                    if (a->hexSelected)
                        a->draftHex.clear();
                    a->hexSelected = false;
                    if (a->draftHex.size() < 7)
                        a->draftHex.push_back(character);
                    Color parsed;
                    if (parseColorHex(a->draftHex, parsed))
                        a->draftColor = colorHsv(parsed);
                    InvalidateRect(a->panel, nullptr, FALSE);
                }
                return 0;
            }
            break;
        case WM_DESTROY: {
            RuntimeStage stage(RuntimeThread::Ui, "ui.shutdown");
            a->diagnostics.stop();
            KillTimer(h, 1);
            a->stopInertia();
            a->closePanel();
            a->persistSettings();
            a->engine.reset();
            a->storage.reset();
            {
                MSG pending{};
                while (PeekMessageW(&pending, h, resultMessage, resultMessage, PM_REMOVE))
                    delete reinterpret_cast<std::shared_ptr<Result> *>(pending.lParam);
            }
            if (a->font)
                DeleteObject(a->font);
            if (a->fieldBackground)
                DeleteObject(a->fieldBackground);
            PostQuitMessage(0);
            return 0;
        }
        }
        return DefWindowProcW(h, m, w, l);
    }
};
int run(HINSTANCE instance, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_BAR_CLASSES};
    InitCommonControlsEx(&controls);
    App a;
    a.instance = instance;
    const bool performance = wcsstr(GetCommandLineW(), L"--performance-test") != nullptr;
    const bool profile = wcsstr(GetCommandLineW(), L"--profile-scroll") != nullptr;
    const bool quality = wcsstr(GetCommandLineW(), L"--ink-quality-test") != nullptr;
    const bool shortStrokes = wcsstr(GetCommandLineW(), L"--short-stroke-test") != nullptr;
    a.smoke = shortStrokes || quality || profile || performance || wcsstr(GetCommandLineW(), L"--smoke-test") != nullptr;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, a.factory.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown **>(a.textFactory.GetAddressOf())))) {
        MessageBoxW(nullptr, L"无法初始化 Windows 图形组件。", L"InkHorn", MB_OK | MB_ICONERROR);
        return 1;
    }
    a.textFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14, L"zh-CN",
                                    &a.format);
    a.textFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12, L"zh-CN",
                                    &a.smallFormat);
    a.textFactory->CreateTextFormat(L"Microsoft YaHei UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20, L"zh-CN",
                                    &a.homeTitle);
    for (auto *textFormat : {a.format.Get(), a.smallFormat.Get()}) {
        if (!textFormat)
            continue;
        textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(a.textFactory->CreateEllipsisTrimmingSign(textFormat, &ellipsis)))
            textFormat->SetTrimming(&trimming, ellipsis.Get());
    }
    auto style = D2D1::StrokeStyleProperties();
    style.startCap = style.endCap = style.dashCap = D2D1_CAP_STYLE_ROUND;
    style.lineJoin = D2D1_LINE_JOIN_ROUND;
    a.factory->CreateStrokeStyle(style, nullptr, 0, &a.roundStyle);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = App::proc;
    wc.hInstance = instance;
    wc.hCursor = nullptr; // All client cursors are explicitly selected; no class-arrow reset mid-pen input.
    wc.lpszClassName = L"InkHornWindow";
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(100));
    wc.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(100), IMAGE_ICON,
                                               GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                               LR_SHARED));
    if (!wc.hIcon)
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);
    wc.lpfnWndProc = App::panelProc;
    wc.lpszClassName = L"InkHornPanel";
    wc.hIcon = nullptr;
    RegisterClassExW(&wc);
    auto h = CreateWindowExW(0, L"InkHornWindow", L"InkHorn", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1160, 860, nullptr, nullptr, instance, &a);
    if (!h)
        return 2;
    MARGINS margins{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(h, &margins);
    SetWindowPos(h, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ShowWindow(h, a.smoke ? SW_HIDE : show);
    UpdateWindow(h);
    a.layout();
    if (shortStrokes) {
        const bool ok = a.shortStrokeCheck();
        DestroyWindow(h);
        if (SUCCEEDED(com))
            CoUninitialize();
        return ok ? 0 : 7;
    }
    if (quality) {
        const bool ok = a.inkQualityCheck();
        DestroyWindow(h);
        if (SUCCEEDED(com))
            CoUninitialize();
        return ok ? 0 : 6;
    }
    if (profile) {
        int count{};
        auto args = CommandLineToArgvW(GetCommandLineW(), &count);
        bool ok = false;
        for (int i = 1; args && i + 1 < count; ++i)
            if (std::wstring_view(args[i]) == L"--profile-scroll")
                ok = a.profileScroll(args[i + 1], i + 2 < count ? _wtof(args[i + 2]) : 1);
        if (args)
            LocalFree(args);
        DestroyWindow(h);
        if (SUCCEEDED(com))
            CoUninitialize();
        return ok ? 0 : 5;
    }
    if (performance) {
        const bool ok = a.performanceCheck();
        DestroyWindow(h);
        if (SUCCEEDED(com))
            CoUninitialize();
        return ok ? 0 : 4;
    }
    if (a.smoke) {
        a.paint();
        const auto emptyButton = a.homeOpenButton();
        bool homeOk = a.tool == 0 && emptyButton.right < a.homeRight().left &&
                      std::abs((a.homeCard().left + a.homeCard().right) / 2 - a.width() / 2) < .01 &&
                      a.homeHit((emptyButton.left + emptyButton.right) / 2, emptyButton.top + 20) == 30;
        a.recent = {L"F:/测试资料/最近阅读.pdf", L"F:/测试资料/第二份文件.pdf"};
        a.paint();
        homeOk = homeOk && a.homeRecentCount() == 2 && a.homeRecentRow(0).left > emptyButton.right;
        a.recent = {std::filesystem::current_path() /
                    ("missing-recent-" + std::to_string(GetCurrentProcessId()) + ".pdf")};
        a.openRecent(0);
        homeOk = homeOk && a.recent.empty() && a.sessions.empty() && !a.toast.empty();
        a.paint();
        a.expireToast(a.toastUntil + 1);
        homeOk = homeOk && a.toast.empty();
        Session missing;
        missing.id = 998;
        missing.path = std::filesystem::current_path() /
                       ("missing-raced-recent-" + std::to_string(GetCurrentProcessId()) + ".pdf");
        missing.fromRecent = true;
        a.recent = {missing.path};
        a.sessions.push_back(std::move(missing));
        a.active = 0;
        auto failedOpen = std::make_shared<Result>();
        failedOpen->document = 998;
        a.result(failedOpen);
        homeOk = homeOk && a.sessions.empty() && a.recent.empty();
        a.expireToast(a.toastUntil + 1);
        a.recent.clear();
        Session demo;
        demo.id = 999;
        demo.path = L"界面测试.pdf";
        demo.pages = {{595, 842}, {595, 842}};
        demo.loading = false;
        demo.history.reset({});
        a.sessions.push_back(std::move(demo));
        a.active = 0;
        a.layout();
        const double x = a.pageLeft(0) + 100, y = a.header() + 80;
        const double beforeWheel = a.doc()->scroll;
        a.scroll(-1, x, y, 1);
        homeOk = homeOk && a.wheel.active() && a.doc()->scroll == beforeWheel;
        a.animateScroll(1.016);
        homeOk = homeOk && a.doc()->scroll > beforeWheel && a.doc()->scroll < beforeWheel + 90;
        for (int i = 2; i <= 20; ++i)
            a.animateScroll(1 + i * .016);
        homeOk = homeOk && !a.wheel.active() && std::abs(a.doc()->scroll - beforeWheel - 90) < .01;
        a.scroll(-1, x, y, 2);
        a.beginPan(4, y, false);
        homeOk = homeOk && !a.wheel.active() && !a.momentum.active();
        a.finishGesture();
        a.doc()->scroll = 0;
        a.scroll(1, x, y, 3);
        a.animateScroll(3.016);
        homeOk = homeOk && a.doc()->scroll == 0 && !a.wheel.active() && !a.momentum.active();
        a.updateFields();
        a.tool = 1;
        // Keep synthetic timestamps independent of popup/cursor creation cost.
        // Otherwise a deliberately old history sample can become newer than DOWN.
        const DWORD sampleEpoch = GetTickCount() - 2000;
        auto penSample = [&](DWORD time, double xx, double yy, bool contact, UINT flags = 0) {
            POINTER_PEN_INFO p{};
            p.pointerInfo.pointerId = 77;
            p.pointerInfo.pointerType = PT_PEN;
            p.pointerInfo.dwTime = sampleEpoch + time;
            p.pointerInfo.pointerFlags = POINTER_FLAG_INRANGE | (contact ? POINTER_FLAG_INCONTACT : 0);
            p.pointerInfo.hwndTarget = h;
            p.pointerInfo.ptPixelLocation = {LONG(std::round(xx * a.dpi())), LONG(std::round(yy * a.dpi()))};
            ClientToScreen(h, &p.pointerInfo.ptPixelLocation);
            p.penMask = PEN_MASK_PRESSURE;
            p.pressure = contact ? 512 : 0;
            p.penFlags = flags;
            return p;
        };
        // Exercise source changes through native event dispatch, including
        // hover, promoted mouse events, stale system moves, and explicit tools.
        a.tool = 0;
        a.inputToolMode = {};
        const auto routingHistory = a.doc()->history.current().size();
        a.penInput(WM_POINTERENTER, penSample(1, x, y, false));
        homeOk = homeOk && a.tool == 1 && a.gesture == App::Gesture::None;
        SendMessageW(h, WM_MOUSEMOVE, 0, MAKELPARAM(int(x * a.dpi()), int(y * a.dpi())));
        homeOk = homeOk && a.tool == 1; // SendMessage has no physical mouse source.
        const auto oldExtra = SetMessageExtraInfo(0xff515710);
        const bool promotedAccepted = a.mouseInput(WM_LBUTTONDOWN);
        SetMessageExtraInfo(oldExtra);
        homeOk = homeOk && !promotedAccepted && a.tool == 1;
        homeOk = a.mouseInput(WM_LBUTTONDOWN) && homeOk && a.tool == 0;
        for (int chosen = 1; chosen <= 5; ++chosen) {
            const auto bar = a.toolbar();
            const LPARAM click = MAKELPARAM(int((bar.left + 30) * a.dpi()),
                int((bar.top + a.toolsTop() + chosen * 46 + 20) * a.dpi()));
            SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, click);
            SendMessageW(h, WM_LBUTTONUP, 0, click);
            homeOk = homeOk && a.tool == chosen;
            a.penInput(WM_POINTERENTER, penSample(2, x, y, false));
            a.penInput(WM_POINTERUPDATE, penSample(3, x + 2, y, false));
            homeOk = homeOk && a.tool == chosen && a.gesture == App::Gesture::None;
            homeOk = a.mouseInput(WM_LBUTTONDOWN) && homeOk && a.tool == 0;
        }
        const double transitionsStarted = inputTime();
        for (int i = 0; i < 10000; ++i) {
            a.usePenInput();
            if (!a.mouseInput(WM_LBUTTONDOWN))
                homeOk = false;
        }
        const double transitionPairUs = (inputTime() - transitionsStarted) * 100;
        const auto repeatedHover = penSample(4, x, y, false);
        a.penInput(WM_POINTERUPDATE, repeatedHover);
        ValidateRect(h, nullptr);
        const double hoverStarted = inputTime();
        for (int i = 0; i < 10000; ++i)
            a.penInput(WM_POINTERUPDATE, repeatedHover);
        const double hoverUs = (inputTime() - hoverStarted) * 100;
        const bool idleHover = !GetUpdateRect(h, nullptr, FALSE) && a.tool == 1 &&
                              a.doc()->history.current().size() == routingHistory;
        homeOk = homeOk && idleHover;
        std::ofstream routingReport("tool-source-results.json");
        routingReport << "{\n  \"passed\": " << (homeOk ? "true" : "false")
                      << ",\n  \"transition_pairs\": 10000,\n  \"transition_pair_avg_us\": " << transitionPairUs
                      << ",\n  \"hover_messages\": 10000,\n  \"hover_message_avg_us\": " << hoverUs
                      << ",\n  \"repeated_hover_does_not_invalidate\": " << (idleHover ? "true" : "false") << "\n}\n";
        a.inputToolMode = {};
        a.penNear = a.penPositionKnown = false;
        a.tool = 1;
        a.updateFields();
        const auto fieldsBefore = a.fieldTextWrites;
        for (int i = 0; i < 20; ++i)
            a.updateFields();
        const auto penBar = a.toolbar();
        const double uiX = (penBar.left + penBar.right) / 2, uiY = penBar.top + a.toolsTop() + 46 + 20;
        a.penInput(WM_POINTERDOWN, penSample(10, uiX, uiY, true));
        a.penInput(WM_POINTERUP, penSample(20, uiX, uiY, false));
        homeOk = homeOk && a.panel && !a.penContact.active();
        homeOk = homeOk && a.cursorTool(x, y) == 1;
        a.penInput(WM_POINTERUPDATE, penSample(30, x, y, false));
        auto lightContact = penSample(100, x, y, true);
        lightContact.pressure = 1;
        a.penInput(WM_POINTERDOWN, lightContact);
        homeOk = homeOk && !a.workingStroke.points.empty() &&
                 a.workingStroke.points.front().width >= .35 * a.widths[1];
        homeOk = homeOk && !a.panel && a.gesture == App::Gesture::Draw && a.penContact.active();
        const auto currentSample = penSample(110, x + 10, y + 4, true);
        const POINTER_PEN_INFO oldUiHistory[] = {currentSample, penSample(90, uiX, uiY, true),
                                                 penSample(80, uiX, uiY, false)};
        a.penInput(WM_POINTERUPDATE, currentSample, oldUiHistory);
        // Unmarked mouse moves/UP must not affect a currently owned pen stroke.
        SendMessageW(h, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(int(uiX * a.dpi()), int(uiY * a.dpi())));
        SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(int(uiX * a.dpi()), int(uiY * a.dpi())));
        homeOk = homeOk && a.penContact.active() && a.gesture == App::Gesture::Draw;
        a.penInput(WM_POINTERUP, penSample(120, x + 10, y + 4, false));
        homeOk = homeOk && a.doc()->history.current().size() == 1 && !a.penContact.active();
        if (!a.doc()->history.current().empty()) {
            const auto b = bounds(a.doc()->history.current().front());
            homeOk = homeOk && b.r - b.l < 40;
        }
        a.penInput(WM_POINTERUPDATE, penSample(130, x + 300, y + 150, false));
        homeOk = homeOk && a.doc()->history.current().size() == 1 && a.gesture == App::Gesture::None;
        a.penInput(WM_POINTERDOWN, penSample(200, x, y + 35, true));
        a.penInput(WM_POINTERUPDATE, penSample(210, x + 10, y + 39, true));
        a.penInput(WM_POINTERUPDATE,
                   penSample(220, x + 10, y + 39, true, PEN_FLAG_ERASER | PEN_FLAG_INVERTED));
        homeOk = homeOk && a.gesture == App::Gesture::Erase && a.tool == 1;
        a.penInput(WM_POINTERUP, penSample(230, x + 10, y + 39, false, PEN_FLAG_INVERTED));
        const auto beforeEraser = a.doc()->history.current().size();
        a.penInput(WM_POINTERDOWN, penSample(250, x, y, true, PEN_FLAG_ERASER | PEN_FLAG_INVERTED));
        homeOk = homeOk && a.gesture == App::Gesture::Erase && a.tool == 1;
        a.penInput(WM_POINTERUP, penSample(260, x, y, false, PEN_FLAG_INVERTED));
        homeOk = homeOk && a.doc()->history.current().size() < beforeEraser;
        a.updateFields();
        homeOk = homeOk && a.fieldTextWrites == fieldsBefore;
        a.penContact.end();
        a.activePointer = 0;
        a.penNear = a.penTemporaryEraser = a.penPositionKnown = false;
        a.inputToolMode = {};
        a.doc()->history.reset({});
        a.shapes.clear();
        a.inkValid = false;
        // A pressure change at a stationary nib must still become visible.
        a.down(x, y, false, .02, true, 10);
        const double initialNib = a.workingStroke.points.front().width;
        a.move(x, y, .9, true, 10.02);
        homeOk = homeOk && a.workingStroke.points.size() == 2 &&
                 a.workingStroke.points.back().width > initialNib;
        a.paint();
        const auto stationaryBounds = bounds(a.workingStroke);
        homeOk = homeOk && std::abs(a.liveBounds.l - stationaryBounds.l) < 1e-8 &&
                 std::abs(a.liveBounds.r - stationaryBounds.r) < 1e-8;
        a.finishGesture();
        a.doc()->history.reset({});
        a.shapes.clear();
        a.inkValid = false;
        std::vector<Stroke> erasedRows;
        for (int row = 0; row < 5; ++row)
            erasedRows.push_back({++a.nextStroke, 0, Kind::Ink, {21, 101, 192, 255}, 1.5,
                                  {{60, 100 + row * 20.0, 1.5}, {220, 100 + row * 20.0, 1.5}}});
        a.doc()->history.reset(std::move(erasedRows));
        a.tool = 3;
        const double eraseX = a.pageLeft(0) + 120 * a.doc()->scale;
        const double eraseTop = a.header() + a.topOf(0) - a.doc()->scroll;
        a.down(eraseX, eraseTop + 220 * a.doc()->scale);
        a.move(eraseX, eraseTop + 70 * a.doc()->scale); // Only two mouse samples cross all five rows.
        a.finishGesture();
        homeOk = homeOk && a.doc()->history.current().empty();
        a.undo(false);
        homeOk = homeOk && a.doc()->history.current().size() == 5;
        a.doc()->history.reset({});
        a.shapes.clear();
        a.inkValid = false;
        a.doc()->pending = 1;
        a.doc()->pendingTicket = 42;
        auto staleRender = std::make_shared<Result>();
        staleRender->document = a.doc()->id;
        staleRender->type = Result::Type::Rendered;
        staleRender->renderTicket = 41;
        a.result(staleRender);
        homeOk = homeOk && a.doc()->pending == 1 && a.doc()->pendingTicket == 42 && a.doc()->cache.empty();
        a.doc()->pending = -1;
        a.doc()->pendingTicket = 0;
        a.tool = 1;
        homeOk = homeOk && a.cursorTool(x, y) == 1 && a.cursorTool(4, y) == -1;
        a.tool = 3;
        homeOk = homeOk && a.cursorTool(x, y) == 3 && a.cursorDiameter(3) == a.widths[3];
        a.openPanel();
        homeOk = homeOk && !a.panel && !App::configurableTool(3) && a.cursorDiameter(3) == 4;
        const auto range = a.rangeCursor.get(20, 2, a.colors[1]);
        ICONINFO rangeInfo{};
        homeOk = homeOk && range && GetIconInfo(range, &rangeInfo) && !rangeInfo.fIcon;
        if (rangeInfo.hbmColor) {
            BITMAP bitmap{};
            GetObjectW(rangeInfo.hbmColor, sizeof(bitmap), &bitmap);
            BITMAPINFO info{};
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth = bitmap.bmWidth;
            info.bmiHeader.biHeight = -bitmap.bmHeight;
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biBitCount = 32;
            info.bmiHeader.biCompression = BI_RGB;
            std::vector<uint32_t> pixels(size_t(bitmap.bmWidth) * bitmap.bmHeight);
            HDC dc = CreateCompatibleDC(nullptr);
            homeOk = homeOk && GetDIBits(dc, rangeInfo.hbmColor, 0, bitmap.bmHeight, pixels.data(), &info,
                                         DIB_RGB_COLORS);
            if (dc)
                DeleteDC(dc);
            const auto center = pixels[size_t(rangeInfo.yHotspot) * bitmap.bmWidth + rangeInfo.xHotspot];
            const auto nearCenter =
                pixels[size_t(rangeInfo.yHotspot) * bitmap.bmWidth + rangeInfo.xHotspot + 8];
            homeOk =
                homeOk && (center >> 24) >= 100 && (center >> 24) <= 104 &&
                (nearCenter >> 24) == (center >> 24);
        }
        if (rangeInfo.hbmColor)
            DeleteObject(rangeInfo.hbmColor);
        if (rangeInfo.hbmMask)
            DeleteObject(rangeInfo.hbmMask);
        const auto eraserCursor = a.rangeCursor.eraser(2, a.factory.Get(), a.icons);
        ICONINFO eraserInfo{};
        homeOk = homeOk && eraserCursor && GetIconInfo(eraserCursor, &eraserInfo) &&
                 eraserInfo.xHotspot == 8 && eraserInfo.yHotspot == 8;
        if (eraserInfo.hbmColor)
            DeleteObject(eraserInfo.hbmColor);
        if (eraserInfo.hbmMask)
            DeleteObject(eraserInfo.hbmMask);
        a.leftHand = true;
        const auto mirroredBar = a.toolbar();
        const auto mirrored =
            a.toolbarIconTransform().TransformPoint(D2D1::Point2F(mirroredBar.left + 6, mirroredBar.top));
        homeOk = homeOk && std::abs(mirrored.x - (mirroredBar.right - 6)) < .01;
        a.paint();
        a.leftHand = false;
        a.tool = 1;
        a.down(x, y, false, .2, true);
        a.move(x + 40, y + 20, .8, true);
        a.finishGesture();
        bool ok = homeOk && a.doc()->history.current().size() == 1 && a.graphics();
        a.functionsMenu();
        ok = ok && a.panelKind == App::PanelKind::Functions &&
             std::none_of(a.menuRows.begin(), a.menuRows.end(),
                          [](const auto &row) { return row.command >= 1000; });
        a.paintPanel();
        a.closePanel();
        a.fileMenu();
        ok = ok && a.panelKind == App::PanelKind::Files && a.menuRows.size() == 2 &&
             a.menuRows[1].command == 1000;
        a.paintPanel();
        a.menuFocus = 1;
        a.popupKey(VK_RETURN);
        ok = ok && !a.panel && a.active == 0;
        a.tool = 1;
        a.chooseColor(7);
        ok = ok && a.panelKind == App::PanelKind::Color;
        a.paintPanel();
        a.draftHex = L"#12ABEF";
        a.hexFocused = true;
        a.commitCustomColor();
        ok = ok && a.palette[7].r == 0x12 && a.palette[7].g == 0xAB && a.palette[7].b == 0xEF;
        a.chooseColor(7);
        a.draftColor = {120, 1, 1};
        a.popupKey(VK_ESCAPE);
        ok = ok && a.panelKind == App::PanelKind::Tool && a.palette[7].r == 0x12;
        a.closePanel();
        a.closeDocument(a.active);
        ok = ok && a.closePrompt == 999;
        a.paint();
        a.answerClosePrompt(0);
        ok = ok && !a.closePrompt && a.doc() && a.doc()->history.current().size() == 1;
        a.openPanel();
        auto bar = a.toolbar();
        a.down((bar.left + bar.right) / 2, bar.top + a.toolsTop() + 3 * 46 + 20);
        ok = ok && a.tool == 3 && !a.panel;
        a.tool = 1;
        a.openPanel();
        a.down(x + 100, y + 100);
        ok = ok && !a.panel && a.gesture == App::Gesture::Draw;
        a.cancelGesture();
        a.paint();
        const auto cachedStroke = a.doc()->history.current().front();
        ok = ok && a.inkTiles.matches(a.doc()->id, cachedStroke.page, cachedStroke);
        a.tool = 0;
        a.down(x, y);
        a.move(x + 15, y + 5);
        a.finishGesture();
        a.paint();
        auto rasterMatches = [&] {
            const auto &stroke = a.doc()->history.current().front();
            return a.inkTiles.matches(a.doc()->id, stroke.page, stroke);
        };
        ok = ok && rasterMatches() &&
             a.doc()->history.current().front().points.identity() != cachedStroke.points.identity();
        a.undo(false);
        a.paint();
        ok = ok && rasterMatches() &&
             a.doc()->history.current().front().points.identity() == cachedStroke.points.identity();
        a.undo(true);
        a.paint();
        ok = ok && rasterMatches();
        a.selected = cachedStroke.id;
        a.deleteSelected();
        ok = ok && a.doc()->history.current().empty();
        a.tool = 5;
        a.openPanel();
        SendMessageW(a.panel, WM_PAINT, 0, 0);
        const Kind smokeShapes[] = {Kind::Rectangle, Kind::Ellipse, Kind::Line, Kind::Arrow};
        const int smokeGlyphs[] = {5, 9, 10, 11};
        for (int i = 0; i < 4; ++i) {
            a.panelInput(46 + (i % 2) * 54, 284 + (i / 2) * 42);
            ok = ok && a.shapeKind == smokeShapes[i] && a.toolbarGlyph(5) == smokeGlyphs[i];
            a.paint();
        }
        a.closePanel();
        SetWindowTextW(a.pageEdit, L"2");
        a.submitEdit(a.pageEdit, true);
        ok = ok && a.doc()->page == 1;
        a.setScale(1.25);
        a.paint();
        a.tool = 1;
        a.down(a.pageLeft(a.doc()->page) + 100, a.header() + 80);
        a.move(a.pageLeft(a.doc()->page) + 120, a.header() + 90);
        a.finishGesture();
        a.closeDocument(a.active);
        ok = ok && a.closePrompt == 999;
        a.answerClosePrompt(1);
        ok = ok && a.sessions.empty();
        a.sessions.clear();
        a.active = -1;
        DestroyWindow(h);
        if (SUCCEEDED(com))
            CoUninitialize();
        return ok ? 0 : 3;
    }
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 1; i < argc; ++i)
            a.open(argv[i]);
        LocalFree(argv);
    }
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return int(msg.wParam);
}
} // namespace pb
