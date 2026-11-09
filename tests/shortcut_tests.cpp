#include "shortcuts.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace pb;
static void require(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    try {
        Shortcuts shortcuts;
        for (size_t i = 0; i < shortcutDefinitions.size(); ++i) {
            const auto key = shortcuts.at(i);
            require(Shortcuts::valid(key) && shortcuts.conflict(i, key) == -1, "default bindings must be valid and unique");
            if (key.key) require(shortcuts.match(key) == shortcutDefinitions[i].command, "default command routing");
        }
        require(shortcuts.match({'S', 1}) == 14 && shortcuts.match({'S', 3}) == 3 &&
                shortcuts.match({'S', 0}) == 35 && !shortcuts.match({'S', 2}), "modifiers must match exactly");
        require(!shortcuts.assign(0, {'S', 1}) && shortcuts.forCommand(1) == KeyChord{'O', 1}, "conflict must preserve original binding");
        for (auto key : {KeyChord{VK_ESCAPE, 0}, {VK_F3, 1}, {VK_RETURN, 0}, {VK_BACK, 0},
                         {VK_TAB, 0}, {'A', 4}, {0, 1}, {VK_CONTROL, 0}})
            require(!shortcuts.assign(0, key), "reject reserved keys and unsupported modifiers");
        require(shortcuts.assign(0, {'Q', 3}) && !shortcuts.match({'O', 1}) && shortcuts.match({'Q', 3}) == 1,
                "reassign must stop the old key from triggering the command");
        require(shortcuts.assign(1, {}) && !shortcuts.match({'S', 1}), "disabled shortcut");
        std::ostringstream saved;
        shortcuts.write(saved);
        Shortcuts restored;
        std::istringstream input(saved.str());
        require(restored.read(input) && restored.match({'Q', 3}) == 1 && !restored.match({'S', 1}), "custom and disabled bindings must roundtrip");
        const auto checkDamaged = [&](std::string text) {
            std::istringstream bad(text);
            require(!restored.read(bad), "damaged extension must fail");
            std::ostringstream after;
            restored.write(after);
            require(after.str() == saved.str(), "invalid input must not partially overwrite bindings");
        };
        checkDamaged("");
        checkDamaged("PBKEY2 25");
        checkDamaged("PBKEY1 999999999");
        checkDamaged(saved.str().substr(0, saved.str().size() - 4));
        auto duplicate = saved.str();
        duplicate.replace(duplicate.find("14 0 0"), 6, "14 81 3");
        checkDamaged(duplicate);
        restored.reset();
        require(restored.match({'O', 1}) == 1 && restored.match({'S', 1}) == 14 && !restored.match({'Q', 3}), "restore defaults");
        require(chordLabel({VK_OEM_COMMA, 1}) == L"Ctrl+," && chordLabel({VK_F11, 0}) == L"F11" &&
                chordLabel({VK_TAB, 3}) == L"Ctrl+Shift+Tab", "display labels");
        std::cout << "Shortcut configuration checks passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
