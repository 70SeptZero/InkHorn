#pragma once
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <windows.h>

namespace pb {
inline constexpr wchar_t brokerClass[] = L"InkHornDocumentBroker";
inline constexpr ULONG_PTR openProtocol = 0x49484f31;
inline std::wstring instanceKey(const std::filesystem::path &executable) {
    auto value = std::filesystem::absolute(executable).lexically_normal().wstring();
    uint64_t hash = 14695981039346656037ull;
    for (auto c : value) {
        hash ^= uint64_t(std::towlower(c == L'/' ? L'\\' : c));
        hash *= 1099511628211ull;
    }
    return L"InkHorn-" + std::to_wstring(hash);
}
inline std::wstring openPayload(const std::vector<std::filesystem::path> &paths) {
    std::wstring payload;
    for (const auto &path : paths) {
        payload += std::filesystem::absolute(path).wstring();
        payload += L'\0';
    }
    if (payload.empty())
        payload += L'\0';
    payload += L'\0';
    return payload;
}
inline std::optional<std::vector<std::filesystem::path>> readOpenPayload(const COPYDATASTRUCT *data) {
    if (!data || data->dwData != openProtocol || !data->lpData ||
        data->cbData < 2 * sizeof(wchar_t) || data->cbData > 1024 * 1024 || data->cbData % sizeof(wchar_t))
        return {};
    const auto *text = static_cast<const wchar_t *>(data->lpData);
    const size_t count = data->cbData / sizeof(wchar_t);
    if (text[count - 1] || text[count - 2])
        return {};
    std::vector<std::filesystem::path> paths;
    if (!text[0] && count != 2)
        return {};
    for (size_t i = 0; i < count - 1 && text[i];) {
        size_t end = i;
        while (end < count && text[end])
            ++end;
        if (end - i > 32767 || paths.size() >= 256)
            return {};
        std::filesystem::path path(std::wstring(text + i, end - i));
        if (!path.is_absolute())
            return {};
        paths.push_back(std::move(path));
        i = end + 1;
        if (i < count - 1 && !text[i])
            return {};
    }
    return paths;
}
class InstanceLock {
    HANDLE handle_{};
    bool owner_{};
  public:
    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;
    explicit InstanceLock(const std::wstring &key) {
        handle_ = CreateMutexW(nullptr, FALSE, (L"Local\\" + key).c_str());
        if (handle_) {
            const auto wait = WaitForSingleObject(handle_, 0);
            owner_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
        }
    }
    ~InstanceLock() {
        if (handle_) {
            if (owner_)
                ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }
    bool valid() const { return handle_ != nullptr; }
    bool primary() const { return owner_; }
    bool recover() {
        if (owner_ || !handle_)
            return owner_;
        const auto wait = WaitForSingleObject(handle_, 0);
        return owner_ = wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED;
    }
};
inline bool forwardOpen(HWND receiver, const std::vector<std::filesystem::path> &paths) {
    const auto payload = openPayload(paths);
    if (payload.size() * sizeof(wchar_t) > 1024 * 1024)
        return false;
    COPYDATASTRUCT data{openProtocol, DWORD(payload.size() * sizeof(wchar_t)),
                        const_cast<wchar_t *>(payload.data())};
    DWORD pid{};
    GetWindowThreadProcessId(receiver, &pid);
    AllowSetForegroundWindow(pid);
    DWORD_PTR accepted{};
    return SendMessageTimeoutW(receiver, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 2000, &accepted) && accepted == 1;
}
} // namespace pb
