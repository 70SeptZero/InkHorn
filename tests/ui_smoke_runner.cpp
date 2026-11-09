#include <windows.h>
#include <iostream>
#include <string>

// Test real window occlusion without unrelated windows on the user's desktop.
// The private desktop is never made visible with SwitchDesktop.
int wmain(int argc, wchar_t **argv) {
    if (argc != 2) return 2;
    const auto name = L"InkHornUiSmoke-" + std::to_wstring(GetCurrentProcessId());
    HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0,
                                   DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS |
                                   DESKTOP_WRITEOBJECTS | DESKTOP_ENUMERATE, nullptr);
    if (!desktop) {
        std::cerr << "CreateDesktop failed: " << GetLastError() << '\n';
        return 2;
    }
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --smoke-test";
    STARTUPINFOW startup{sizeof(startup)};
    startup.lpDesktop = const_cast<wchar_t *>(name.c_str());
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &child)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << '\n';
        CloseDesktop(desktop);
        return 2;
    }
    CloseHandle(child.hThread);
    DWORD result = 2;
    if (WaitForSingleObject(child.hProcess, 25000) == WAIT_OBJECT_0) {
        GetExitCodeProcess(child.hProcess, &result);
    } else {
        std::cerr << "UI smoke test timed out\n";
        TerminateProcess(child.hProcess, 2);
        WaitForSingleObject(child.hProcess, 3000);
    }
    CloseHandle(child.hProcess);
    CloseDesktop(desktop);
    return int(result);
}
