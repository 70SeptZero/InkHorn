#include "pdf_engine.hpp"
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <windows.h>
using namespace pb;
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void fixture(const std::filesystem::path &path, int pageCount = 1, int rotate = 0, bool withText = false) {
    std::string kids;
    for (int i = 0; i < pageCount; ++i)
        kids += std::to_string(3 + i * 2) + " 0 R ";
    std::vector<std::string> objects = {"<< /Type /Catalog /Pages 2 0 R >>",
                                        "<< /Type /Pages /Kids [" + kids + "] /Count " +
                                            std::to_string(pageCount) + " >>"};
    for (int i = 0; i < pageCount; ++i) {
        objects.push_back("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Rotate " +
                          std::to_string(rotate) + " /Resources << /Font << /F1 << /Type /Font /Subtype /Type1 /BaseFont /Helvetica >> >> >> /Contents " +
                          std::to_string(4 + i * 2) + " 0 R >>");
        const std::string content = withText ? "BT /F1 12 Tf 20 360 Td (Hello wrapped) Tj 0 -16 Td (world) Tj 0 -40 Td (Next paragraph) Tj ET" : "";
        objects.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "\nendstream");
    }
    std::ostringstream pdf;
    pdf << "%PDF-1.7\n";
    std::vector<size_t> offsets{0};
    for (size_t i = 0; i < objects.size(); ++i) {
        offsets.push_back(size_t(pdf.tellp()));
        pdf << i + 1 << " 0 obj\n" << objects[i] << "\nendobj\n";
    }
    auto xref = pdf.tellp();
    pdf << "xref\n0 " << objects.size() + 1 << "\n0000000000 65535 f \n";
    for (size_t i = 1; i < offsets.size(); ++i) {
        pdf << std::setw(10) << std::setfill('0') << offsets[i] << " 00000 n \n";
    }
    pdf << "trailer\n<< /Size " << objects.size() + 1 << " /Root 1 0 R >>\nstartxref\n"
        << xref << "\n%%EOF\n";
    std::ofstream file(path, std::ios::binary);
    file << pdf.str();
}
int main() {
    try {
        auto dir =
            std::filesystem::current_path() / ("inkhorn-test-" + std::to_string(GetCurrentProcessId()) +
                                               "-" + std::to_string(GetTickCount64()));
        std::filesystem::create_directories(dir);
        auto source = dir / "input.pdf", saved = dir / "notes.pdf";
        auto book = dir / L"中文路径-120页.pdf", bookSaved = dir / "book-notes.pdf";
        auto textSource = dir / "text.pdf", rotatedTextSource = dir / "rotated-text.pdf";
        fixture(source);
        fixture(book, 120, 90);
        fixture(textSource, 120, 0, true);
        fixture(rotatedTextSource, 1, 90, true);
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::shared_ptr<Result>> results;
        {
            PdfEngine engine([&](std::shared_ptr<Result> r) {
                {
                    std::lock_guard lock(mutex);
                    results.push_back(std::move(r));
                }
                cv.notify_one();
            });
            auto await = [&](Result::Type type) {
                std::unique_lock lock(mutex);
                require(cv.wait_for(lock, std::chrono::seconds(30), [&] { return !results.empty(); }),
                        "engine timeout");
                auto r = results.front();
                results.pop_front();
                if (r->type == Result::Type::Failed) {
                    int n = WideCharToMultiByte(CP_UTF8, 0, r->error.data(), int(r->error.size()), nullptr, 0,
                                                nullptr, nullptr);
                    std::string error(n, '\0');
                    WideCharToMultiByte(CP_UTF8, 0, r->error.data(), int(r->error.size()), error.data(), n,
                                        nullptr, nullptr);
                    throw std::runtime_error(error);
                }
                require(r->type == type, "unexpected result type");
                return r;
            };
            engine.open(1, source);
            auto opened = await(Result::Type::Opened);
            require(opened->info.pages.size() == 1, "open page count");
            std::vector<Stroke> strokes = {
                {1, 0, Kind::Ink, {21, 101, 192, 255}, 2, {{20, 40, .7}, {60, 60, 3.6}, {90, 50, 1}}},
                {2, 0, Kind::Marker, {255, 235, 59, 72}, 12, {{20, 100, 12}, {90, 100, 12}}},
                {3, 0, Kind::Rectangle, {211, 47, 47, 255}, 2, {{110, 40, 2}, {170, 90, 2}}},
                {4, 0, Kind::Ellipse, {21, 101, 192, 255}, 2, {{190, 40, 2}, {250, 90, 2}}}};
            engine.save(1, strokes, "first", saved);
            std::cout << "Stage: initial save\n" << std::flush;
            require(await(Result::Type::Saved)->saveFingerprint == "first", "save point");
            engine.close(1);
            engine.open(2, saved);
            auto roundtrip = await(Result::Type::Opened);
            require(roundtrip->info.strokes.size() == strokes.size(), "stroke count preserved");
            for (size_t i = 0; i < strokes.size(); ++i)
                require(encode(strokes[i]) == encode(roundtrip->info.strokes[i]),
                        "stroke geometry preserved");
            engine.render(2, 0, 1);
            auto bitmap = await(Result::Type::Rendered);
            require(bitmap->bitmap.width == 300 && bitmap->bitmap.height == 400, "render size");
            // Rendering mirror must suppress own saved ink, which the UI overlays.
            size_t notWhite = 0;
            for (size_t i = 0; i < bitmap->bitmap.pixels.size(); i += 4)
                if (bitmap->bitmap.pixels[i] != 255 || bitmap->bitmap.pixels[i + 1] != 255 ||
                    bitmap->bitmap.pixels[i + 2] != 255)
                    ++notWhite;
            require(notWhite == 0, "saved ink must not be drawn twice by UI");
            engine.render(2, 0, 2, true);
            auto visible = await(Result::Type::Rendered);
            size_t blue = 0, yellow = 0, red = 0;
            const auto &pixels = visible->bitmap.pixels;
            for (size_t k = 0; k < pixels.size(); k += 4) {
                if (pixels[k] > 120 && pixels[k + 2] < 100)
                    ++blue;
                if (pixels[k] < 245 && pixels[k + 1] > 240 && pixels[k + 2] > 240)
                    ++yellow;
                if (pixels[k + 2] > 150 && pixels[k] < 100)
                    ++red;
            }
            require(blue > 150 && red > 100 && yellow > 100,
                    "saved vector appearances must actually render in PDFium");
            translate(strokes[0], 15, 7);
            strokes.erase(strokes.begin() + 1);
            engine.save(2, strokes, "second");
            std::cout << "Stage: overwrite after move/delete\n" << std::flush;
            await(Result::Type::Saved);
            engine.close(2);
            engine.open(3, saved);
            auto edited = await(Result::Type::Opened);
            require(edited->info.strokes.size() == 3, "delete persisted");
            require(encode(edited->info.strokes[0]) == encode(strokes[0]),
                    "move persisted without pressure replay");
            engine.close(1);
            engine.close(2);
            engine.close(3);
            engine.open(4, book);
            auto large = await(Result::Type::Opened);
            require(large->info.pages.size() == 120, "120-page document metadata");
            auto last = strokes.front();
            last.page = 119;
            engine.save(4, {last}, "last-page", bookSaved);
            await(Result::Type::Saved);
            engine.close(4);
            engine.open(5, bookSaved);
            auto finalPage = await(Result::Type::Opened);
            require(finalPage->info.strokes.size() == 1 && finalPage->info.strokes[0].page == 119,
                    "last page annotation survived");
            engine.render(5, 119, 1, true);
            auto rotated = await(Result::Type::Rendered);
            size_t inkPixels = 0;
            for (size_t i = 0; i < rotated->bitmap.pixels.size(); i += 4)
                if (rotated->bitmap.pixels[i] > 120 && rotated->bitmap.pixels[i + 2] < 100)
                    ++inkPixels;
            require(inkPixels > 20, "rotated page annotation appearance");
            // Rapid page/zoom changes must converge to the newest request, even when
            // older rendering is already running or has just delivered a callback.
            for (uint64_t ticket = 1; ticket <= 100; ++ticket)
                engine.render(5, int(ticket % 120), ticket % 2 ? 4.0 : .5, false, ticket);
            engine.cancelRender(5);
            engine.render(5, 119, 1.25, true, 101);
            bool newest = false;
            for (int n = 0; n < 101 && !newest; ++n) {
                auto rendered = await(Result::Type::Rendered);
                if (rendered->renderTicket == 101) {
                    newest = true;
                    require(rendered->bitmap.page == 119 && rendered->bitmap.scale == 1.25,
                            "newest page/zoom render must win");
                }
            }
            require(newest, "latest render completion");
            engine.close(5);
            engine.open(6, textSource);
            await(Result::Type::Opened);
            auto started = std::chrono::steady_clock::now();
            size_t totalMatches = 0;
            for (int page = 0; page < 120; ++page) {
                engine.text(6, page, uint64_t(page + 1), L"wrapped world");
                auto text = await(Result::Type::Text);
                require(text->error.empty() && text->text && text->text->copyAllowed,
                        "read actual PDF text on background worker");
                require(text->textTicket == uint64_t(page + 1) && text->textPage == page,
                        "text request identity survives asynchronous delivery");
                if (text->matches.size() != 1 || text->matches.front().boxes.size() != 2) {
                    std::cout << "Text diagnostic: matches=" << text->matches.size() << '\n';
                    for (const auto &c : text->text->characters)
                        std::cout << unsigned(c.unicode) << " [" << c.box.l << ',' << c.box.t << ','
                                  << c.box.r << ',' << c.box.b << "] visible=" << c.visible << '\n';
                }
                require(text->matches.size() == 1 && text->matches.front().boxes.size() == 2,
                        "PDFium generated line breaks must be searchable");
                auto &m = text->matches.front();
                require(selectedText(*text->text, m.first, m.last) == L"wrapped\r\nworld",
                        "PDF selection preserves its source text");
                require(findText(*text->text, L"world Next").empty(), "real PDF paragraph gap");
                totalMatches += text->matches.size();
            }
            std::cout << "Text: 120 pages, " << totalMatches << " matches in "
                      << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count()
                      << " ms (sequential background extraction/search)\n";
            engine.open(7, rotatedTextSource);
            auto rotatedInfo = await(Result::Type::Opened);
            engine.text(7, 0, 500, L"Hello");
            auto rotatedText = await(Result::Type::Text);
            require(rotatedText->text && rotatedText->matches.size() == 1, "rotated PDF text extraction");
            if (findText(*rotatedText->text, L"wrapped world").size() != 1 ||
                !findText(*rotatedText->text, L"world Next").empty()) {
                std::cout << "Rotated text diagnostic:\n";
                for (const auto &c : rotatedText->text->characters)
                    std::cout << unsigned(c.unicode) << " paragraph=" << c.paragraphBefore << '\n';
            }
            require(findText(*rotatedText->text, L"wrapped world").size() == 1 &&
                        findText(*rotatedText->text, L"world Next").empty(),
                    "paragraph inference is independent of display rotation");
            const auto &box = rotatedText->text->characters.front().box;
            require(box.l > 300 && box.l < 380 && box.t >= 19 && box.t < 30 &&
                        box.r < rotatedInfo->info.pages[0].w && box.b < rotatedInfo->info.pages[0].h,
                    "rotated glyph boxes use displayed page coordinates");
            require(rotatedText->text->hit({(box.l + box.r) / 2, (box.t + box.b) / 2}, 0) == 0,
                    "rotated glyph hit test agrees with PDFium page mapping");
            engine.render(7, 0, 1);
            auto afterText = await(Result::Type::Rendered);
            require(afterText->bitmap.width == 400 && afterText->bitmap.height == 300,
                    "text parsing restores the page rotation before rendering");
            engine.close(6);
            engine.close(7);
        }
        // Only delete the explicitly generated files, no recursive filesystem removal.
        std::filesystem::remove(source);
        std::filesystem::remove(saved);
        std::filesystem::remove(book);
        std::filesystem::remove(bookSaved);
        std::filesystem::remove(textSource);
        std::filesystem::remove(rotatedTextSource);
        std::filesystem::remove(dir);
        std::cout << "PASS: real PDF open/render/save/reopen, Ink/Marker/Square/Circle, move/delete\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
