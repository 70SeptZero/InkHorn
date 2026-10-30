#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <windows.h>

namespace pb {
// Explicit, local, bounded diagnostics. No global hooks or document contents.
class InputDiagnostics {
    std::ofstream stream_;
    std::filesystem::path file_;
    std::wstring last_;
    ULONGLONG started_{}, lastUpdate_{};
    UINT lastId_{}, lastFlags_{~0u}, lastMask_{~0u}, lastButtons_{~0u};
    size_t lines_{};
    void write(const std::string &);

  public:
    bool start(const std::filesystem::path &directory);
    void stop();
    bool active() const {
        return stream_.is_open();
    }
    const std::filesystem::path &file() const {
        return file_;
    }
    const std::wstring &last() const {
        return last_;
    }
    bool pen(UINT message, const POINTER_PEN_INFO &, const char *surface = "main");
    bool message(UINT, WPARAM, LPARAM, const char *surface = "main");
};
} // namespace pb
