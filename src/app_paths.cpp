#include "app_paths.hpp"
#include <stdexcept>
#include <windows.h>

namespace pb {
std::filesystem::path portableDataDirectory() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
    if (!length || length >= std::size(executable))
        throw std::runtime_error("Cannot locate InkHorn executable directory");
    return dataDirectoryForExecutable(std::filesystem::path(executable));
}
} // namespace pb
