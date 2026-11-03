#include "window_routing.hpp"
#include <iostream>
#include <stdexcept>
using namespace pb;
namespace {
std::vector<std::vector<std::filesystem::path>> requests;
LRESULT CALLBACK receive(HWND h, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_COPYDATA) {
        auto paths = readOpenPayload(reinterpret_cast<COPYDATASTRUCT *>(l));
        if (!paths)
            return 0;
        requests.push_back(std::move(*paths));
        return 1;
    }
    return DefWindowProcW(h, message, w, l);
}
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
}
int wmain(int argc, wchar_t **argv) {
    if (argc >= 3 && std::wstring_view(argv[1]) == L"--client") {
        InstanceLock lock(argv[2]);
        if (!lock.valid() || lock.primary())
            return 2;
        std::vector<std::filesystem::path> paths;
        for (int i = 3; i < argc; ++i)
            paths.emplace_back(argv[i]);
        auto receiver = FindWindowExW(HWND_MESSAGE, nullptr, brokerClass, argv[2]);
        return receiver && forwardOpen(receiver, paths) ? 0 : 3;
    }
    try {
        wchar_t executable[32768]{};
        GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
        auto key = instanceKey(executable) + L"-routing-test-" + std::to_wstring(GetCurrentProcessId());
        InstanceLock lock(key);
        require(lock.valid() && lock.primary(), "first process owns the session");
        require(instanceKey(L"C:/InkHorn/APP.exe") == instanceKey(L"c:\\inkhorn\\app.exe"),
                "equivalent executable path casing and separators");
        require(instanceKey(L"C:/one/InkHorn.exe") != instanceKey(L"C:/two/InkHorn.exe"),
                "different portable builds keep independent sessions");
        WNDCLASSW wc{};
        wc.lpfnWndProc = receive;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = brokerClass;
        require(RegisterClassW(&wc) != 0, "register test broker");
        auto receiver = CreateWindowExW(0, brokerClass, key.c_str(), 0, 0, 0, 0, 0,
                                        HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        require(receiver != nullptr, "create message-only broker");
        auto payload = openPayload({std::filesystem::current_path() / L"中文 空格 A.pdf"});
        COPYDATASTRUCT data{openProtocol, DWORD(payload.size() * sizeof(wchar_t)), payload.data()};
        require(readOpenPayload(&data).has_value(), "Unicode payload roundtrip");
        data.cbData -= 1;
        require(!readOpenPayload(&data), "reject misaligned packet length");
        data.cbData = DWORD(payload.size() * sizeof(wchar_t));
        data.dwData = 0;
        require(!readOpenPayload(&data), "reject unknown protocol");
        std::wstring relative = L"relative.pdf";
        relative.append(2, L'\0');
        data = {openProtocol, DWORD(relative.size() * sizeof(wchar_t)), relative.data()};
        require(!readOpenPayload(&data), "only absolute paths cross processes");
        payload.back() = L'x';
        data = {openProtocol, DWORD(payload.size() * sizeof(wchar_t)), payload.data()};
        require(!readOpenPayload(&data), "reject missing terminator");
        const std::vector<std::filesystem::path> paths{
            std::filesystem::current_path() / L"中文 空格 A.pdf",
            std::filesystem::current_path() / L"另一本 B.pdf"};
        const auto launch = [&](bool withFiles) {
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --client \"" + key + L"\"";
            if (withFiles)
                for (const auto &path : paths)
                    command += L" \"" + path.wstring() + L"\"";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &startup, &child) != FALSE, "launch second process");
            const auto deadline = GetTickCount64() + 5000;
            while (WaitForSingleObject(child.hProcess, 0) == WAIT_TIMEOUT && GetTickCount64() < deadline) {
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                Sleep(5);
            }
            DWORD exitCode{};
            GetExitCodeProcess(child.hProcess, &exitCode);
            CloseHandle(child.hThread);
            CloseHandle(child.hProcess);
            require(exitCode == 0, "second process forwards to existing instance without making a window");
        };
        launch(true);
        require(requests.size() == 1 && requests.front() == paths, "both file paths reach the original process");
        launch(false);
        require(requests.size() == 2 && requests.back().empty(), "no-file launch activates existing session");
        DestroyWindow(receiver);
        std::cout << "PASS: cross-process session lock, Unicode/multiple-file forwarding, malformed packets\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
