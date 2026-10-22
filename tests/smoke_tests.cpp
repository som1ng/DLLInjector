#include <windows.h>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Child {
    PROCESS_INFORMATION process{};
    ~Child() {
        if (process.hProcess) {
            if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
                // Only this test's own CreateProcess handle is ever terminated.
                TerminateProcess(process.hProcess, 0);
                WaitForSingleObject(process.hProcess, 3000);
            }
            CloseHandle(process.hProcess);
        }
        if (process.hThread) CloseHandle(process.hThread);
    }
    void Start(const wchar_t* path) {
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESHOWWINDOW;
        startup.wShowWindow = SW_HIDE;
        std::wstring command = L"\"" + std::wstring(path) + L"\"";
        Check(CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &process) != FALSE, "launch smoke child");
    }
};
bool DebugPrivilegeEnabled(HANDLE process) {
    HANDLE token = nullptr;
    Check(OpenProcessToken(process, TOKEN_QUERY, &token) != FALSE, "query child token");
    DWORD needed = 0;
    GetTokenInformation(token, TokenPrivileges, nullptr, 0, &needed);
    std::vector<unsigned char> bytes(needed);
    const BOOL read = GetTokenInformation(token, TokenPrivileges, bytes.data(), needed, &needed);
    CloseHandle(token);
    Check(read != FALSE, "read token privileges");
    LUID debug{};
    Check(LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &debug) != FALSE, "lookup debug privilege");
    const auto* privileges = reinterpret_cast<const TOKEN_PRIVILEGES*>(bytes.data());
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i) {
        const auto& entry = privileges->Privileges[i];
        if (entry.Luid.LowPart == debug.LowPart && entry.Luid.HighPart == debug.HighPart) {
            return (entry.Attributes & SE_PRIVILEGE_ENABLED) != 0;
        }
    }
    return false;
}
struct WindowSearch { DWORD pid; HWND window; };
BOOL CALLBACK FindWindowForChild(HWND window, LPARAM parameter) {
    auto& search = *reinterpret_cast<WindowSearch*>(parameter);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    wchar_t className[64]{};
    GetClassNameW(window, className, 64);
    if (pid == search.pid && std::wstring(className) == L"InjectorWindowClass") {
        search.window = window;
        return FALSE;
    }
    return TRUE;
}
void Gui(const wchar_t* path) {
    const bool parentDebug = DebugPrivilegeEnabled(GetCurrentProcess());
    Child child;
    child.Start(path);
    Check(WaitForInputIdle(child.process.hProcess, 10000) == 0, "GUI must initialize");
    WindowSearch search{child.process.dwProcessId, nullptr};
    EnumWindows(FindWindowForChild, reinterpret_cast<LPARAM>(&search));
    Check(search.window != nullptr, "GUI window must exist");
    Check(!IsWindowVisible(search.window), "smoke GUI must remain hidden");
    Check(DebugPrivilegeEnabled(child.process.hProcess) == parentDebug,
          "GUI startup must not enable debug privilege");
    DWORD_PTR result = 0;
    Check(SendMessageTimeoutW(GetDlgItem(search.window, 101), LB_GETCOUNT, 0, 0,
                              SMTO_ABORTIFHUNG, 3000, &result) != 0 && result > 0,
          "GUI must list processes");
    Check(SendMessageTimeoutW(GetDlgItem(search.window, 108), CB_GETCOUNT, 0, 0,
                              SMTO_ABORTIFHUNG, 3000, &result) != 0 && result == 6,
          "legacy method entries must remain present");
    Check(PostMessageW(search.window, WM_CLOSE, 0, 0) != FALSE, "close smoke GUI");
    Check(WaitForSingleObject(child.process.hProcess, 5000) == WAIT_OBJECT_0, "GUI must exit cleanly");
    DWORD exitCode = 1;
    Check(GetExitCodeProcess(child.process.hProcess, &exitCode) && exitCode == 0, "GUI exit code");
}
void Demo(const wchar_t* path) {
    Child child;
    child.Start(path);
    Check(WaitForSingleObject(child.process.hProcess, 1200) == WAIT_TIMEOUT,
          "original demo must stay running until stopped");
}
void Fixture(const wchar_t* path) {
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    Check(module != nullptr, "load self-owned noop fixture");
    const auto value = reinterpret_cast<int (*)()>(GetProcAddress(module, "FixtureValue"));
    const bool valid = value && value() == 42;
    const BOOL unloaded = FreeLibrary(module);
    Check(valid, "fixture must return its explicit test value");
    Check(unloaded != FALSE, "unload noop fixture");
}
}

int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc == 3, "expected smoke mode and fixture path");
        const std::wstring mode = argv[1];
        if (mode == L"gui") Gui(argv[2]);
        else if (mode == L"demo") Demo(argv[2]);
        else if (mode == L"fixture") Fixture(argv[2]);
        else throw std::runtime_error("unknown smoke mode");
        std::cout << "PASS smoke test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << " (last Win32 error " << GetLastError() << ")\n";
        return 1;
    }
}
