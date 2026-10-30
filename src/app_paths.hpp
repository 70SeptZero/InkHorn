#pragma once
#include <filesystem>

namespace pb {
inline std::filesystem::path dataDirectoryForExecutable(const std::filesystem::path &executable) {
    return executable.parent_path() / L"InkHorn-data";
}
std::filesystem::path portableDataDirectory();
} // namespace pb
