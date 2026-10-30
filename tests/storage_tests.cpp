#include "app_paths.hpp"
#include "storage.hpp"
#include <iostream>
#include <stdexcept>
#include <windows.h>
using namespace pb;
int main() {
    try {
        const auto portable = portableDataDirectory();
        if (portable.filename() != "InkHorn-data" || dataDirectoryForExecutable(L"F:/应用/InkHorn.exe") !=
                                                         std::filesystem::path(L"F:/应用/InkHorn-data"))
            throw std::runtime_error("portable data must follow the executable, not the working directory");
        auto root = std::filesystem::current_path() /
                    ("recovery-test-" + std::to_string(GetCurrentProcessId())) / "recovery";
        {
            Storage store(root);
            Recovery data;
            data.path = std::filesystem::current_path() / L"未保存笔记.pdf";
            data.sourceStamp = 123456;
            data.sourceSize = 789;
            data.strokes = {{42, 0, Kind::Ink, {21, 101, 192, 255}, 2, {{10, 20, .7}, {30, 40, 3.6}}}};
            store.write(data);
            store.flush();
            auto recovered = store.read(data.path);
            if (!store.lastError().empty() || !recovered || recovered->sourceStamp != 123456 ||
                recovered->strokes.size() != 1 || encode(recovered->strokes[0]) != encode(data.strokes[0]))
                throw std::runtime_error("recovery roundtrip failed");
            data.strokes.clear();
            store.write(data);
            store.flush();
            recovered = store.read(data.path);
            if (!recovered || !recovered->strokes.empty())
                throw std::runtime_error("recover deletion of all strokes");
            const auto beforeBurst = store.recoveryWrites();
            for (int i = 0; i < 100; ++i) {
                data.strokes = {{42, 0, Kind::Ink, {21, 101, 192, 255}, 2, {{double(i), 20, 1}}}};
                store.write(data);
            }
            store.flush();
            recovered = store.read(data.path);
            if (!recovered || recovered->strokes[0].points[0].x != 99 ||
                store.recoveryWrites() - beforeBurst >= 20)
                throw std::runtime_error("rapid strokes must coalesce and flush the newest recovery snapshot");
            store.erase(data.path);
            store.flush();
            if (store.read(data.path))
                throw std::runtime_error("saved journal must be removed");
            const std::vector<std::filesystem::path> recent = {
                data.path, std::filesystem::current_path() / L"含空格路径" / L"阅读 笔记.pdf"};
            store.writeRecent(recent);
            store.flush();
            if (store.readRecent() != recent)
                throw std::runtime_error("recent files Unicode roundtrip failed");
            store.writeRecent({});
            store.flush();
            if (!store.readRecent().empty())
                throw std::runtime_error("removed recent files must stay removed after persistence");
        }
        std::filesystem::remove(root);
        std::filesystem::remove(root.parent_path() / "recent.txt");
        std::filesystem::remove(root.parent_path());
        std::cout << "PASS: async recovery, stroke widths, full deletion, saved cleanup\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
