#pragma once
#include "core.hpp"
#include <cwctype>
#include <string>

namespace pb {
// Coordinates use the same rotated/cropped, top-left page space as ink.
struct TextCharacter {
    char32_t unicode{};
    Bounds box{};
    bool visible{};
    bool paragraphBefore{};
};
struct PageText {
    int page{};
    bool copyAllowed{true};
    bool boundariesKnown{};
    std::vector<TextCharacter> characters;
    int hit(Point p, double tolerance = 2) const {
        for (size_t i = 0; i < characters.size(); ++i) {
            const auto &c = characters[i];
            if (c.visible && p.x >= c.box.l - tolerance && p.x <= c.box.r + tolerance &&
                p.y >= c.box.t - tolerance && p.y <= c.box.b + tolerance)
                return int(i);
        }
        return -1;
    }
    int nearest(Point p) const {
        double distance = 1e100;
        int found = -1;
        for (size_t i = 0; i < characters.size(); ++i) {
            const auto &c = characters[i];
            if (!c.visible)
                continue;
            double dx = std::max({c.box.l - p.x, 0.0, p.x - c.box.r});
            double dy = std::max({c.box.t - p.y, 0.0, p.y - c.box.b});
            double d = dx * dx + dy * dy;
            if (d < distance) {
                distance = d;
                found = int(i);
            }
        }
        return found;
    }
};
inline void appendUnicode(std::wstring &out, char32_t c) {
    if (!c || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        return;
    if constexpr (sizeof(wchar_t) == 2) {
        if (c > 0xffff) {
            c -= 0x10000;
            out += wchar_t(0xd800 + (c >> 10));
            out += wchar_t(0xdc00 + (c & 1023));
            return;
        }
    }
    out += wchar_t(c);
}
inline std::wstring selectedText(const PageText &page, int first, int last) {
    std::wstring out;
    if (!page.copyAllowed)
        return out;
    for (int i = std::max(0, std::min(first, last));
         i <= std::max(first, last) && i < int(page.characters.size()); ++i)
        appendUnicode(out, page.characters[i].unicode);
    return out;
}
inline std::vector<Bounds> textBoxes(const PageText &page, int first, int last) {
    std::vector<Bounds> boxes;
    for (int i = std::max(0, std::min(first, last));
         i <= std::max(first, last) && i < int(page.characters.size()); ++i) {
        const auto &c = page.characters[i];
        if (!c.visible)
            continue;
        // Join glyphs on one line; keep columns and rotated runs separate.
        if (!boxes.empty() &&
            std::min(boxes.back().b, c.box.b) - std::max(boxes.back().t, c.box.t) >
                .3 * std::min(boxes.back().b - boxes.back().t, c.box.b - c.box.t) &&
            c.box.l >= boxes.back().l && c.box.l <= boxes.back().r + 8)
        {
            boxes.back().r = std::max(boxes.back().r, c.box.r);
            boxes.back().t = std::min(boxes.back().t, c.box.t);
            boxes.back().b = std::max(boxes.back().b, c.box.b);
        }
        else
            boxes.push_back(c.box);
    }
    return boxes;
}
struct TextMatch {
    int page{}, first{}, last{};
    std::vector<Bounds> boxes;
};
inline bool textSpace(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\r' || c == U'\n' || c == 0xa0 || c == 0x3000;
}
inline char32_t foldText(char32_t c) {
    return c <= 0xffff ? char32_t(std::towlower(wchar_t(c))) : c;
}
inline bool cjkText(char32_t c) {
    return c >= 0x2e80 && c <= 0x9fff;
}
// PDF has no reliable paragraph tags. Keep explicit blank lines, large line
// gaps and column jumps as boundaries; collapse ordinary wrapped lines.
inline std::vector<TextMatch> findText(const PageText &page, const std::wstring &query,
                                       size_t limit = 2000) {
    std::u32string needle;
    for (size_t i = 0; i < query.size(); ++i) {
        char32_t c = query[i];
        if constexpr (sizeof(wchar_t) == 2) {
            if (c >= 0xd800 && c <= 0xdbff && i + 1 < query.size() &&
                query[i + 1] >= 0xdc00 && query[i + 1] <= 0xdfff)
                c = 0x10000 + ((c - 0xd800) << 10) + (query[++i] - 0xdc00);
        }
        if (textSpace(c)) {
            size_t next = i + 1;
            while (next < query.size() && textSpace(query[next]))
                ++next;
            if (!needle.empty() && needle.back() != U' ' &&
                !(cjkText(needle.back()) && next < query.size() && cjkText(query[next])))
                needle += U' ';
        } else
            needle += foldText(c);
    }
    if (!needle.empty() && needle.back() == U' ')
        needle.pop_back();
    if (needle.empty())
        return {};
    std::u32string text;
    std::vector<int> indices;
    const auto add = [&](char32_t c, int i) { text += c; indices.push_back(i); };
    const auto &chars = page.characters;
    int previousVisible = -1;
    for (int i = 0; i < int(chars.size()); ++i) {
        auto c = chars[i].unicode;
        if (textSpace(c)) {
            int start = i, lines = 0;
            while (i < int(chars.size()) && textSpace(chars[i].unicode)) {
                if (chars[i].unicode == U'\n' ||
                    (chars[i].unicode == U'\r' && (i + 1 == int(chars.size()) || chars[i + 1].unicode != U'\n')))
                    ++lines;
                ++i;
            }
            bool boundary = lines > 1;
            if (lines && page.boundariesKnown && i < int(chars.size()))
                boundary = boundary || chars[i].paragraphBefore;
            else if (lines && previousVisible >= 0 && i < int(chars.size()) && chars[i].visible) {
                const auto a = chars[previousVisible].box, b = chars[i].box;
                double h = std::max(1.0, std::max(a.b - a.t, b.b - b.t));
                boundary = boundary || b.t - a.b > h * 1.2 || b.t < a.t - h || b.l > a.r + h * 3;
            }
            if (boundary)
                add(U'\u2029', start);
            else if (lines && start > 0 && chars[start - 1].unicode == 0xad) {
                // The discretionary hyphen was already omitted above.
            }
            else if (lines && !text.empty() && text.back() == U'-' &&
                     text.size() >= 2 && text[text.size() - 2] >= U'a' && text[text.size() - 2] <= U'z' &&
                     i < int(chars.size()) && chars[i].unicode >= U'a' && chars[i].unicode <= U'z') {
                text.pop_back();
                indices.pop_back();
            }
            else if (!(lines && previousVisible >= 0 && i < int(chars.size()) &&
                       cjkText(chars[previousVisible].unicode) && cjkText(chars[i].unicode)))
                add(U' ', start);
            --i;
            continue;
        }
        if (chars[i].visible)
            previousVisible = i;
        if (c && c != 0xad) // discretionary soft hyphen
            add(foldText(c), i);
    }
    std::vector<TextMatch> matches;
    for (size_t at = 0; matches.size() < limit && (at = text.find(needle, at)) != std::u32string::npos; ++at) {
        int first = indices[at], last = indices[at + needle.size() - 1];
        auto boxes = textBoxes(page, first, last);
        if (!boxes.empty())
            matches.push_back({page.page, first, last, std::move(boxes)});
    }
    return matches;
}
} // namespace pb
