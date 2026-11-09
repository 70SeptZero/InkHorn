#pragma once
#include <array>
#include <istream>
#include <ostream>
#include <string>
#include <windows.h>

namespace pb {
struct KeyChord {
    unsigned key{}, modifiers{}; // 1 = Ctrl, 2 = Shift. Alt/Windows combinations stay with the OS.
    bool operator==(const KeyChord &) const = default;
};
struct ShortcutDefinition {
    int command;
    const wchar_t *label;
    KeyChord defaults;
};
inline constexpr std::array<ShortcutDefinition, 25> shortcutDefinitions{{
    ShortcutDefinition{1, L"打开 PDF", {'O', 1}},
    {14, L"保存", {'S', 1}}, {3, L"另存为", {'S', 3}}, {2, L"关闭当前文档", {'W', 1}},
    {22, L"下一个文档", {VK_TAB, 1}}, {23, L"上一个文档", {VK_TAB, 3}},
    {15, L"撤销", {'Z', 1}}, {16, L"重做", {'Y', 1}}, {24, L"重做（备用）", {'Z', 3}},
    {11, L"删除所选笔记", {VK_DELETE, 0}}, {20, L"复制所选文字", {'C', 1}},
    {21, L"跳转页码", {'G', 1}}, {19, L"在文档中查找", {'F', 1}},
    {4, L"适合宽度", {}}, {5, L"隐藏笔记", {}}, {6, L"切换全屏", {VK_F11, 0}},
    {30, L"指针 / 选择", {'1', 0}}, {31, L"笔 1", {'2', 0}}, {32, L"笔 2", {'3', 0}},
    {33, L"橡皮", {'E', 0}}, {34, L"荧光笔", {'H', 0}}, {35, L"形状", {'S', 0}},
    {9, L"打开设置", {VK_OEM_COMMA, 1}},
    {17, L"笔输入诊断", {'D', 3}}, {18, L"打开诊断目录", {'L', 3}}
}};
class Shortcuts {
    std::array<KeyChord, shortcutDefinitions.size()> keys_{};
  public:
    Shortcuts() { reset(); }
    void reset() {
        for (size_t i = 0; i < keys_.size(); ++i)
            keys_[i] = shortcutDefinitions[i].defaults;
    }
    KeyChord at(size_t i) const { return keys_.at(i); }
    static bool valid(KeyChord chord) {
        if (!chord.key)
            return chord.modifiers == 0;
        if (chord.modifiers > 3 || chord.key == VK_F3) // Contextual search navigation.
            return false;
        return (chord.key >= 'A' && chord.key <= 'Z') || (chord.key >= '0' && chord.key <= '9') ||
               (chord.key >= VK_F1 && chord.key <= VK_F12) || chord.key == VK_DELETE ||
               (chord.modifiers & 1 && (chord.key == VK_TAB || chord.key == VK_OEM_COMMA));
    }
    int conflict(size_t index, KeyChord chord) const {
        for (size_t i = 0; chord.key && i < keys_.size(); ++i)
            if (i != index && keys_[i] == chord)
                return int(i);
        return -1;
    }
    bool assign(size_t index, KeyChord chord) {
        if (index >= keys_.size() || !valid(chord) || conflict(index, chord) >= 0)
            return false;
        keys_[index] = chord;
        return true;
    }
    int match(KeyChord chord) const {
        for (size_t i = 0; chord.key && i < keys_.size(); ++i)
            if (keys_[i] == chord)
                return shortcutDefinitions[i].command;
        return 0;
    }
    KeyChord forCommand(int command) const {
        for (size_t i = 0; i < keys_.size(); ++i)
            if (shortcutDefinitions[i].command == command)
                return keys_[i];
        return {};
    }
    void write(std::ostream &out) const {
        out << " PBKEY1 " << keys_.size();
        for (size_t i = 0; i < keys_.size(); ++i)
            out << ' ' << shortcutDefinitions[i].command << ' ' << keys_[i].key << ' ' << keys_[i].modifiers;
    }
    bool read(std::istream &in) {
        std::string magic;
        size_t count{};
        if (!(in >> magic >> count) || magic != "PBKEY1" || count != keys_.size())
            return false;
        auto next = keys_;
        for (size_t i = 0; i < keys_.size(); ++i) {
            int command{};
            if (!(in >> command >> next[i].key >> next[i].modifiers) ||
                command != shortcutDefinitions[i].command || !valid(next[i]))
                return false;
            for (size_t j = 0; next[i].key && j < i; ++j)
                if (next[j] == next[i])
                    return false;
        }
        keys_ = next; // A damaged extension never partially overwrites working bindings.
        return true;
    }
};
inline std::wstring chordLabel(KeyChord chord) {
    if (!chord.key)
        return L"未设置";
    std::wstring label;
    if (chord.modifiers & 1) label += L"Ctrl+";
    if (chord.modifiers & 2) label += L"Shift+";
    if (chord.key >= VK_F1 && chord.key <= VK_F12)
        return label + L"F" + std::to_wstring(chord.key - VK_F1 + 1);
    if (chord.key == VK_TAB) return label + L"Tab";
    if (chord.key == VK_DELETE) return label + L"Delete";
    if (chord.key == VK_OEM_COMMA) return label + L",";
    return label + wchar_t(chord.key);
}
} // namespace pb
