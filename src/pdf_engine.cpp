#include "pdf_engine.hpp"
#include "runtime_trace.hpp"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <windows.h>
#if INKHORN_WITH_PDFIUM
#include <fpdf_annot.h>
#include <fpdf_edit.h>
#include <fpdf_progressive.h>
#include <fpdf_save.h>
#include <fpdf_signature.h>
#include <fpdfview.h>
#endif

namespace pb {
namespace {
std::string utf8(const std::filesystem::path &p) {
    auto s = p.u8string();
    return {s.begin(), s.end()};
}
std::wstring widen(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring r(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}
struct Finally {
    std::function<void()> f;
    ~Finally() {
        if (f)
            f();
    }
};
} // namespace
struct PdfEngine::Impl {
    Callback callback;
    std::mutex mutex;
    std::condition_variable cv;
    struct Task {
        int type{};
        uint64_t id{};
        std::function<void()> run;
        uint64_t ticket{};
    };
    std::deque<Task> queue;
    bool stopping{};
    std::thread worker;
    std::map<uint64_t, std::shared_ptr<std::atomic_bool>> renderCancellation;
#if INKHORN_WITH_PDFIUM
#define PB_API_LIST(X)                                                                                       \
    X(FPDF_InitLibraryWithConfig)                                                                            \
    X(FPDF_DestroyLibrary)                                                                                   \
    X(FPDF_LoadDocument)                                                                                     \
    X(FPDF_CloseDocument)                                                                                    \
    X(FPDF_GetPageCount)                                                                                     \
    X(FPDF_LoadPage) X(FPDF_ClosePage) X(FPDF_GetPageWidthF) X(FPDF_GetPageHeightF) X(FPDFBitmap_Create)     \
        X(FPDFBitmap_Destroy) X(FPDFBitmap_FillRect) X(FPDFBitmap_GetBuffer) X(FPDFBitmap_GetStride)         \
            X(FPDF_RenderPageBitmap) X(FPDFPage_GetAnnotCount) X(FPDFPage_GetAnnot) X(FPDFPage_CloseAnnot)   \
                X(FPDFAnnot_GetStringValue) X(FPDFAnnot_SetStringValue) X(FPDFAnnot_SetFlags)                \
                    X(FPDFAnnot_GetFlags) X(FPDFPage_RemoveAnnot) X(FPDFPage_CreateAnnot)                    \
                        X(FPDFAnnot_SetRect) X(FPDFAnnot_AddInkStroke) X(FPDFPageObj_CreateNewPath)          \
                            X(FPDFPath_MoveTo) X(FPDFPath_LineTo) X(FPDFPath_Close) X(FPDFPath_SetDrawMode)  \
                                X(FPDFPageObj_SetFillColor) X(FPDFPageObj_SetBlendMode)                      \
                                    X(FPDFAnnot_AppendObject) X(FPDFPageObj_Destroy) X(FPDF_SaveAsCopy)      \
                                        X(FPDF_DeviceToPage) X(FPDFAnnot_SetAP) X(FPDF_GetDocPermissions)    \
                                            X(FPDF_GetSignatureCount) X(FPDF_RenderPageBitmap_Start)         \
                                                X(FPDF_RenderPage_Continue) X(FPDF_RenderPage_Close)
    struct Api {
#define PB_DECLARE(name) decltype(&::name) name{};
        PB_API_LIST(PB_DECLARE)
#undef PB_DECLARE
        HMODULE dll{};
        void load() {
#ifdef INKHORN_DYNAMIC_PDFIUM
            wchar_t explicitPath[32768]{};
            GetEnvironmentVariableW(L"INKHORN_PDFIUM_DLL", explicitPath, 32768);
            dll = LoadLibraryExW(explicitPath[0] ? explicitPath : L"pdfium.dll", nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!dll)
                throw std::runtime_error(
                    "PDFium DLL could not be loaded. Set INKHORN_PDFIUM_DLL to its full path.");
#define PB_LOAD(name)                                                                                        \
    name = reinterpret_cast<decltype(name)>(GetProcAddress(dll, #name));                                     \
    if (!name)                                                                                               \
        throw std::runtime_error("Missing PDFium API: " #name);
#else
#define PB_LOAD(name) name = &::name;
#endif
            PB_API_LIST(PB_LOAD)
#undef PB_LOAD
            FPDF_LIBRARY_CONFIG config{};
            config.version = 2;
            FPDF_InitLibraryWithConfig(&config);
        }
    } api;
    struct Doc {
        FPDF_DOCUMENT handle{};
        DocumentInfo info;
        std::filesystem::file_time_type time;
        uintmax_t bytes{};
    };
    std::map<uint64_t, Doc> docs;
    bool ready{};
    std::string ownData(FPDF_ANNOTATION a) {
        auto n = api.FPDFAnnot_GetStringValue(a, "PdfBoardInkData", nullptr, 0);
        if (n < 4 || n > 32 * 1024 * 1024)
            return {};
        std::vector<unsigned short> data((n + 1) / 2);
        if (api.FPDFAnnot_GetStringValue(a, "PdfBoardInkData", data.data(), n) != n)
            return {};
        std::string s;
        s.reserve(data.size());
        for (auto c : data) {
            if (!c)
                break;
            if (c > 127)
                return {};
            s.push_back(char(c));
        }
        return s;
    }
    void read(Doc &d) {
        auto count = api.FPDF_GetPageCount(d.handle);
        if (count <= 0)
            throw std::runtime_error("PDF 没有可读取的页面。");
        d.info.pages.clear();
        d.info.strokes.clear();
        for (int i = 0; i < count; ++i) {
            auto p = api.FPDF_LoadPage(d.handle, i);
            if (!p)
                throw std::runtime_error("无法读取 PDF 页面。");
            Finally end{[&] { api.FPDF_ClosePage(p); }};
            d.info.pages.push_back({api.FPDF_GetPageWidthF(p), api.FPDF_GetPageHeightF(p)});
            for (int j = 0; j < api.FPDFPage_GetAnnotCount(p); ++j) {
                auto a = api.FPDFPage_GetAnnot(p, j);
                if (!a)
                    continue;
                Stroke s;
                if (decode(ownData(a), s)) {
                    s.page = i;
                    d.info.strokes.push_back(std::move(s));
                }
                api.FPDFPage_CloseAnnot(a);
            }
        }
        d.time = std::filesystem::last_write_time(d.info.path);
        d.bytes = std::filesystem::file_size(d.info.path);
        d.info.sourceStamp = d.time.time_since_epoch().count();
        d.info.sourceSize = d.bytes;
    }
    void openDoc(uint64_t id, const std::filesystem::path &path) {
        if (!ready)
            throw std::runtime_error("PDF 引擎初始化失败。");
        Doc d;
        d.info.id = id;
        d.info.path = std::filesystem::absolute(path);
        d.handle = api.FPDF_LoadDocument(utf8(d.info.path).c_str(), nullptr);
        if (!d.handle)
            throw std::runtime_error("无法打开 PDF，请检查文件是否损坏、受密码保护或没有访问权限。");
        try {
            read(d);
        } catch (...) {
            api.FPDF_CloseDocument(d.handle);
            throw;
        }
        if (docs.contains(id))
            api.FPDF_CloseDocument(docs.at(id).handle);
        docs[id] = std::move(d);
        auto r = std::make_shared<Result>();
        r->type = Result::Type::Opened;
        r->document = id;
        r->info = docs.at(id).info;
        callback(r);
    }
    void renderPage(uint64_t id, int index, double scale, bool includeSavedAnnotations, uint64_t ticket,
                    const std::shared_ptr<std::atomic_bool> &cancel) {
        if (cancel->load())
            return;
        auto &d = docs.at(id);
        if (index < 0 || index >= int(d.info.pages.size()))
            return;
        scale = std::clamp(scale, .1, 4.0);
        auto size = d.info.pages[index];
        int w = std::max(1, int(std::ceil(size.w * scale))), h = std::max(1, int(std::ceil(size.h * scale)));
        if (int64_t(w) * h > 32000000)
            throw std::runtime_error("该页面在当前倍率下超出安全位图大小，请降低缩放比例。");
        auto p = api.FPDF_LoadPage(d.handle, index);
        if (!p)
            throw std::runtime_error("无法渲染该页面。");
        Finally pageEnd{[&] { api.FPDF_ClosePage(p); }};
        if (cancel->load())
            return;
        // This document is a rendering mirror only. Saving always opens a fresh source.
        for (int j = 0; j < api.FPDFPage_GetAnnotCount(p); ++j) {
            auto a = api.FPDFPage_GetAnnot(p, j);
            if (!a)
                continue;
            Stroke s;
            if (decode(ownData(a), s))
                api.FPDFAnnot_SetFlags(a, includeSavedAnnotations
                                              ? api.FPDFAnnot_GetFlags(a) & ~FPDF_ANNOT_FLAG_HIDDEN
                                              : api.FPDFAnnot_GetFlags(a) | FPDF_ANNOT_FLAG_HIDDEN);
            api.FPDFPage_CloseAnnot(a);
        }
        auto b = api.FPDFBitmap_Create(w, h, 0);
        if (!b)
            throw std::runtime_error("页面位图内存分配失败。");
        Finally bitmapEnd{[&] { api.FPDFBitmap_Destroy(b); }};
        api.FPDFBitmap_FillRect(b, 0, 0, w, h, 0xffffffff);
        struct PauseState {
            std::atomic_bool *cancel;
            ULONGLONG until;
        } state{cancel.get(), GetTickCount64() + 8};
        IFSDK_PAUSE pause{};
        pause.version = 1;
        pause.user = &state;
        pause.NeedToPauseNow = [](IFSDK_PAUSE *self) -> FPDF_BOOL {
            auto *state = static_cast<PauseState *>(self->user);
            return state->cancel->load() || GetTickCount64() >= state->until;
        };
        int status = api.FPDF_RenderPageBitmap_Start(b, p, 0, 0, w, h, 0, FPDF_ANNOT, &pause);
        Finally renderEnd{[&] { api.FPDF_RenderPage_Close(p); }};
        while (status == FPDF_RENDER_TOBECONTINUED && !cancel->load()) {
            state.until = GetTickCount64() + 8;
            status = api.FPDF_RenderPage_Continue(p, &pause);
        }
        if (cancel->load())
            return;
        if (status != FPDF_RENDER_DONE)
            throw std::runtime_error("页面渐进渲染失败。");
        auto r = std::make_shared<Result>();
        r->type = Result::Type::Rendered;
        r->document = id;
        r->renderTicket = ticket;
        r->bitmap = {id, index, w, h, scale, {}};
        r->bitmap.pixels.resize(size_t(w) * h * 4);
        auto *src = static_cast<uint8_t *>(api.FPDFBitmap_GetBuffer(b));
        int stride = api.FPDFBitmap_GetStride(b);
        for (int y = 0; y < h; ++y)
            std::copy_n(src + size_t(y) * stride, size_t(w) * 4, r->bitmap.pixels.data() + size_t(y) * w * 4);
        if (!cancel->load())
            callback(r);
    }
    Point toPdf(FPDF_PAGE page, Size size, Point q) {
        // Use the engine's page mapping, including CropBox, Rotate and UserUnit.
        double x = 0, y = 0;
        api.FPDF_DeviceToPage(page, 0, 0, std::max(1, int(size.w * 100)), std::max(1, int(size.h * 100)), 0,
                              int(std::round(q.x * 100)), int(std::round(q.y * 100)), &x, &y);
        return {x, y, q.width};
    }
    void writeStroke(FPDF_PAGE page, Size size, const Stroke &s) {
        if (s.points.empty())
            return;
        int subtype = s.kind == Kind::Rectangle
                          ? FPDF_ANNOT_SQUARE
                          : (s.kind == Kind::Ellipse ? FPDF_ANNOT_CIRCLE : FPDF_ANNOT_INK);
        auto a = api.FPDFPage_CreateAnnot(page, subtype);
        if (!a)
            throw std::runtime_error("无法创建 PDF 批注。");
        Finally closeA{[&] { api.FPDFPage_CloseAnnot(a); }};
        auto g = geometry(s);
        Bounds b{1e30, 1e30, -1e30, -1e30};
        for (auto &c : g)
            for (auto &p : c) {
                p = toPdf(page, size, p);
                b.l = std::min(b.l, p.x);
                b.t = std::min(b.t, p.y);
                b.r = std::max(b.r, p.x);
                b.b = std::max(b.b, p.y);
            }
        FS_RECTF rect{float(b.l - .02), float(b.b + .02), float(b.r + .02), float(b.t - .02)};
        if (!api.FPDFAnnot_SetRect(a, &rect))
            throw std::runtime_error("无法设置批注边界。");
        api.FPDFAnnot_SetFlags(a, FPDF_ANNOT_FLAG_PRINT);
        if (subtype == FPDF_ANNOT_INK) {
            std::vector<FS_POINTF> points;
            for (auto p : s.points) {
                p = toPdf(page, size, p);
                points.push_back({float(p.x), float(p.y)});
            }
            if (api.FPDFAnnot_AddInkStroke(a, points.data(), points.size()) < 0)
                throw std::runtime_error("无法写入笔迹轨迹。");
            auto obj = api.FPDFPageObj_CreateNewPath(float(g.front().front().x), float(g.front().front().y));
            if (!obj)
                throw std::runtime_error("无法生成笔迹矢量外观。");
            bool first = true;
            for (const auto &c : g) {
                if (!first)
                    api.FPDFPath_MoveTo(obj, float(c.front().x), float(c.front().y));
                first = false;
                for (size_t i = 1; i < c.size(); ++i)
                    api.FPDFPath_LineTo(obj, float(c[i].x), float(c[i].y));
                api.FPDFPath_Close(obj);
            }
            api.FPDFPath_SetDrawMode(obj, FPDF_FILLMODE_WINDING, 0);
            api.FPDFPageObj_SetFillColor(obj, s.color.r, s.color.g, s.color.b, s.color.a);
            // Same single-fill alpha semantics as the live Direct2D layer.
            if (s.kind == Kind::Marker)
                api.FPDFPageObj_SetBlendMode(obj, "Normal");
            if (!api.FPDFAnnot_AppendObject(a, obj)) {
                api.FPDFPageObj_Destroy(obj);
                throw std::runtime_error("无法写入批注外观。");
            }
        } else {
            std::ostringstream stream;
            stream.imbue(std::locale::classic());
            stream << std::setprecision(10) << "q " << s.color.r / 255.0 << ' ' << s.color.g / 255.0 << ' '
                   << s.color.b / 255.0 << " rg\n";
            for (const auto &c : g) {
                stream << c[0].x << ' ' << c[0].y << " m\n";
                for (size_t i = 1; i < c.size(); ++i)
                    stream << c[i].x << ' ' << c[i].y << " l\n";
                stream << "h\n";
            }
            stream << "f Q";
            auto ap = widen(stream.str());
            if (!api.FPDFAnnot_SetAP(a, FPDF_ANNOT_APPEARANCEMODE_NORMAL,
                                     reinterpret_cast<const unsigned short *>(ap.c_str())))
                throw std::runtime_error("无法设置形状外观。");
        }
        auto data = widen(encode(s)), name = std::to_wstring(s.id);
        if (!api.FPDFAnnot_SetStringValue(a, "PdfBoardInkData",
                                          reinterpret_cast<const unsigned short *>(data.c_str())) ||
            !api.FPDFAnnot_SetStringValue(a, "NM", reinterpret_cast<const unsigned short *>(name.c_str())))
            throw std::runtime_error("无法写入笔迹数据。");
    }
    void saveDoc(uint64_t id, const std::vector<Stroke> &strokes, const std::string &fingerprint,
                 const std::filesystem::path &requested) {
        auto &d = docs.at(id);
        if (std::filesystem::last_write_time(d.info.path) != d.time ||
            std::filesystem::file_size(d.info.path) != d.bytes)
            throw std::runtime_error(
                "原 PDF 已被外部修改，当前笔记仍保留。请先核对原文件与恢复记录，避免覆盖外部修改。");
        auto target = requested.empty() ? d.info.path : std::filesystem::absolute(requested);
        auto source = api.FPDF_LoadDocument(utf8(d.info.path).c_str(), nullptr);
        if (!source)
            throw std::runtime_error("保存时无法重新打开原 PDF。");
        Finally closeSource{[&] {
            if (source)
                api.FPDF_CloseDocument(source);
        }};
        if (api.FPDF_GetSignatureCount(source) > 0)
            throw std::runtime_error("此 PDF 包含数字签名，本版本保护该文件，不直接写入可能影响签名的批注。");
        if (!(api.FPDF_GetDocPermissions(source) & (1u << 5)))
            throw std::runtime_error("该 PDF 的文档权限不允许添加批注。");
        for (int i = 0; i < int(d.info.pages.size()); ++i) {
            auto p = api.FPDF_LoadPage(source, i);
            if (!p)
                throw std::runtime_error("保存时无法读取页面。");
            Finally end{[&] { api.FPDF_ClosePage(p); }};
            for (int j = api.FPDFPage_GetAnnotCount(p) - 1; j >= 0; --j) {
                auto a = api.FPDFPage_GetAnnot(p, j);
                Stroke old;
                bool own = a && decode(ownData(a), old);
                if (a)
                    api.FPDFPage_CloseAnnot(a);
                if (own && !api.FPDFPage_RemoveAnnot(p, j))
                    throw std::runtime_error("无法更新原有笔记批注。");
            }
            for (const auto &s : strokes)
                if (s.page == i)
                    writeStroke(p, d.info.pages[i], s);
        }
        auto temp = target;
        temp += L".inkhorn-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L".tmp";
        HANDLE file =
            CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            throw std::runtime_error("无法创建保存临时文件，请检查目录权限和文件占用。");
        bool keep = false;
        Finally cleanup{[&] {
            if (file != INVALID_HANDLE_VALUE)
                CloseHandle(file);
            if (!keep)
                DeleteFileW(temp.c_str());
        }};
        struct Writer : FPDF_FILEWRITE {
            HANDLE file;
        };
        Writer writer{};
        writer.version = 1;
        writer.file = file;
        writer.WriteBlock = [](FPDF_FILEWRITE *self, const void *data, unsigned long size) -> int {
            auto *w = static_cast<Writer *>(self);
            DWORD written = 0;
            return WriteFile(w->file, data, size, &written, nullptr) && written == size;
        };
        if (!api.FPDF_SaveAsCopy(source, &writer, FPDF_NO_INCREMENTAL) || !FlushFileBuffers(file))
            throw std::runtime_error("保存失败，可能是磁盘已满或读写错误；原文件尚未替换。");
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
        api.FPDF_CloseDocument(source);
        source = nullptr;
        auto check = api.FPDF_LoadDocument(utf8(temp).c_str(), nullptr);
        if (!check)
            throw std::runtime_error("保存的 PDF 未通过重新打开校验，原文件尚未替换。");
        bool valid = api.FPDF_GetPageCount(check) == int(d.info.pages.size());
        api.FPDF_CloseDocument(check);
        if (!valid)
            throw std::runtime_error("保存校验发现页数不一致，原文件尚未替换。");
        if (std::filesystem::last_write_time(d.info.path) != d.time ||
            std::filesystem::file_size(d.info.path) != d.bytes)
            throw std::runtime_error("保存期间原 PDF 发生外部变化，已停止替换原文件。");
        api.FPDF_CloseDocument(d.handle);
        d.handle = nullptr;
        const bool exists = std::filesystem::exists(target);
        BOOL replaced = exists ? ReplaceFileW(target.c_str(), temp.c_str(), nullptr, 0, nullptr, nullptr)
                               : MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        if (!replaced) {
            const auto error = GetLastError();
            d.handle = api.FPDF_LoadDocument(utf8(d.info.path).c_str(), nullptr);
            throw std::runtime_error("无法替换目标文件（Windows 错误 " + std::to_string(error) +
                                     "），请检查占用或另存到有写入权限的位置。");
        }
        keep = true;
        d.info.path = target;
        d.handle = api.FPDF_LoadDocument(utf8(target).c_str(), nullptr);
        if (!d.handle)
            throw std::runtime_error("PDF 已写入，但重新打开失败，请保留恢复记录并检查目标文件。");
        d.info.strokes = strokes;
        d.time = std::filesystem::last_write_time(target);
        d.bytes = std::filesystem::file_size(target);
        auto r = std::make_shared<Result>();
        r->type = Result::Type::Saved;
        r->document = id;
        r->saveFingerprint = fingerprint;
        r->path = target;
        r->sourceStamp = d.time.time_since_epoch().count();
        r->sourceSize = d.bytes;
        callback(r);
    }
#endif
    explicit Impl(Callback cb) : callback(std::move(cb)) {
        worker = std::thread([this] {
#if INKHORN_WITH_PDFIUM
            try {
                api.load();
                ready = true;
            } catch (const std::exception &e) {
                failure(0, e.what());
            }
#endif
            for (;;) {
                Task t;
                {
                    std::unique_lock lock(mutex);
                    cv.wait(lock, [&] { return stopping || !queue.empty(); });
                    if (stopping && queue.empty())
                        break;
                    t = std::move(queue.front());
                    queue.pop_front();
                }
                try {
                    RuntimeStage stage(RuntimeThread::Pdf, t.type == 0   ? "pdf.open"
                                                           : t.type == 1 ? "pdf.render"
                                                           : t.type == 2 ? "pdf.save"
                                                                         : "pdf.close");
                    t.run();
                } catch (const std::exception &e) {
                    failure(t.id, e.what(), t.ticket);
                }
            }
#if INKHORN_WITH_PDFIUM
            for (auto &[id, d] : docs) {
                (void)id;
                if (d.handle)
                    api.FPDF_CloseDocument(d.handle);
            }
            if (ready)
                api.FPDF_DestroyLibrary();
            if (api.dll)
                FreeLibrary(api.dll);
#endif
        });
    }
    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stopping = true;
            for (auto &[id, cancel] : renderCancellation) {
                (void)id;
                cancel->store(true);
            }
            std::erase_if(queue, [](const Task &task) { return task.type == 1; });
        }
        cv.notify_one();
        worker.join();
    }
    void failure(uint64_t id, const std::string &message, uint64_t ticket = 0) {
        auto r = std::make_shared<Result>();
        r->document = id;
        r->renderTicket = ticket;
        r->error = L"操作未完成：" + widen(message);
        callback(r);
    }
    void post(int type, uint64_t id, std::function<void()> task, uint64_t ticket = 0,
              std::shared_ptr<std::atomic_bool> cancel = {}) {
        std::lock_guard lock(mutex);
        if (stopping)
            return;
        if (type == 1 || type == 3) {
            auto previous = renderCancellation.find(id);
            if (previous != renderCancellation.end())
                previous->second->store(true);
            if (cancel)
                renderCancellation[id] = std::move(cancel);
            else
                renderCancellation.erase(id);
            std::erase_if(queue, [&](const Task &t) { return t.type == 1 && t.id == id; });
        }
        queue.push_back({type, id, std::move(task), ticket});
        cv.notify_one();
    }
};
PdfEngine::PdfEngine(Callback cb) : impl_(std::make_unique<Impl>(std::move(cb))) {}
PdfEngine::~PdfEngine() = default;
void PdfEngine::open(uint64_t id, const std::filesystem::path &path) {
    impl_->post(0, id, [this, id, path] {
#if INKHORN_WITH_PDFIUM
        impl_->openDoc(id, path);
#else
    (void)path;impl_->failure(id,"This is UI preview only. Build the release preset with PDFium to read PDFs.");
#endif
    });
}
void PdfEngine::render(uint64_t id, int page, double scale, bool includeSavedAnnotations, uint64_t ticket) {
    auto cancel = std::make_shared<std::atomic_bool>(false);
    impl_->post(
        1, id,
        [this, id, page, scale, includeSavedAnnotations, ticket, cancel] {
#if INKHORN_WITH_PDFIUM
            impl_->renderPage(id, page, scale, includeSavedAnnotations, ticket, cancel);
#else
            (void)id;
            (void)page;
            (void)scale;
#endif
        },
        ticket, cancel);
}
void PdfEngine::cancelRender(uint64_t id) {
    std::lock_guard lock(impl_->mutex);
    if (auto found = impl_->renderCancellation.find(id); found != impl_->renderCancellation.end())
        found->second->store(true);
    std::erase_if(impl_->queue, [&](const auto &task) { return task.type == 1 && task.id == id; });
}
void PdfEngine::save(uint64_t id, std::vector<Stroke> strokes, std::string fp,
                     const std::filesystem::path &target) {
    impl_->post(2, id, [this, id, strokes = std::move(strokes), fp = std::move(fp), target] {
#if INKHORN_WITH_PDFIUM
        impl_->saveDoc(id, strokes, fp, target);
#else
    (void)strokes;(void)fp;(void)target;impl_->failure(id,"PDF support is disabled.");
#endif
    });
}
void PdfEngine::close(uint64_t id) {
    impl_->post(3, id, [this, id] {
#if INKHORN_WITH_PDFIUM
        auto i = impl_->docs.find(id);
        if (i != impl_->docs.end()) {
            if (i->second.handle)
                impl_->api.FPDF_CloseDocument(i->second.handle);
            impl_->docs.erase(i);
        }
#else
    (void)id;
#endif
    });
}
} // namespace pb
