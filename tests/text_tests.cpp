#include "pdf_text.hpp"
#include <iostream>
#include <stdexcept>
using namespace pb;
void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
int main() {
    try {
        PageText page;
        const auto line = [&](std::u32string value, double y) {
            double x = 20;
            for (auto c : value) {
                page.characters.push_back({c, {x, y, x + 6, y + 10}, !textSpace(c)});
                x += 6;
            }
        };
        line(U"Hello wrapped\r\n", 20);
        line(U"world\r\n", 34);
        line(U"Next paragraph", 70);
        require(findText(page, L"WRAPPED world").size() == 1, "ordinary line wraps and ASCII case folding");
        require(findText(page, L"world Next").empty(), "paragraph gaps must not create false phrase matches");
        require(findText(page, L"   ").empty(), "empty normalized query");
        require(findText(page, L"Hello\nwrapped").size() == 1, "query whitespace collapses");
        auto match = findText(page, L"wrapped world").front();
        require(match.boxes.size() == 2, "multiline results have separate line rectangles");
        require(selectedText(page, match.last, match.first) == L"wrapped\r\nworld", "reverse selection preserves source line breaks");
        require(page.hit({21, 22}) == 0 && page.hit({0, 0}) == -1 && page.nearest({-20, 22}) == 0,
                "glyph hit, blank area and selection extension");
        page.copyAllowed = false;
        require(selectedText(page, 0, 4).empty(), "copy permissions");
        page.characters.clear();
        line(U"中文换行\r\n", 20);
        line(U"继续阅读\r\n\r\n", 34);
        line(U"另一段", 48);
        require(findText(page, L"换行继续").size() == 1, "CJK wraps do not insert a word space");
        require(findText(page, L"换行\n继续").size() == 1, "CJK query wraps normalize too");
        require(findText(page, L"阅读另一").empty(), "explicit blank lines separate paragraphs");
        page.characters.clear();
        line(U"perfor-\r\n", 20);
        line(U"mance", 34);
        require(findText(page, L"performance").size() == 1, "wrapped Latin word dehyphenation");
        page.characters.clear();
        line(U"perfor\u00ad\r\n", 20);
        line(U"mance", 34);
        require(findText(page, L"performance").size() == 1, "discretionary soft hyphen at a wrap");
        page.characters.clear();
        line(U"aaaaa", 20);
        require(findText(page, L"aa").size() == 4 && findText(page, L"a", 2).size() == 2,
                "overlapping matches and bounded results");
        page.copyAllowed = true;
        page.characters.clear();
        line(U"A\U0001f600B", 20);
        require(findText(page, L"\U0001f600").size() == 1 && selectedText(page, 1, 1) == L"\U0001f600",
                "supplementary Unicode maps to one PDF character");
        std::cout << "PASS: text search normalization, paragraph boundaries, selection and Unicode\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
