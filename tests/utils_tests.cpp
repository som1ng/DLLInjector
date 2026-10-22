#include "utils.h"
#include "utils_detail.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int closes = 0;
int legacyCalls = 0;
USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
USHORT nativeMachine = IMAGE_FILE_MACHINE_AMD64;
WORD nativeProcessor = PROCESSOR_ARCHITECTURE_AMD64;
BOOL wow64 = FALSE;
DWORD queryError = ERROR_SUCCESS;
DWORD openError = ERROR_SUCCESS;
DWORD lookupError = ERROR_SUCCESS;
BOOL adjustResult = TRUE;
DWORD adjustError = ERROR_SUCCESS;
HANDLE FakeHandle() { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(123)); }

HANDLE WINAPI OpenProcessStub(DWORD access, BOOL inherit, DWORD) {
    Check(access == PROCESS_QUERY_LIMITED_INFORMATION, "query must request minimum rights");
    Check(!inherit, "query handle must not be inheritable");
    SetLastError(openError);
    return openError == ERROR_SUCCESS ? FakeHandle() : nullptr;
}
BOOL WINAPI CloseStub(HANDLE handle) {
    Check(handle == FakeHandle(), "unexpected handle cleanup");
    ++closes;
    SetLastError(ERROR_INVALID_HANDLE); // Cleanup must not destroy the captured error.
    return TRUE;
}
BOOL WINAPI Query2Stub(HANDLE, USHORT* process, USHORT* native) {
    *process = processMachine;
    *native = nativeMachine;
    SetLastError(queryError);
    return queryError == ERROR_SUCCESS;
}
BOOL WINAPI QueryLegacyStub(HANDLE, PBOOL result) {
    ++legacyCalls;
    *result = wow64;
    SetLastError(queryError);
    return queryError == ERROR_SUCCESS;
}
void WINAPI SystemInfoStub(LPSYSTEM_INFO info) {
    *info = {};
    info->wProcessorArchitecture = nativeProcessor;
}
BOOL WINAPI OpenTokenStub(HANDLE, DWORD access, PHANDLE token) {
    Check(access == (TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY), "unexpected token rights");
    SetLastError(openError);
    *token = openError == ERROR_SUCCESS ? FakeHandle() : nullptr;
    return openError == ERROR_SUCCESS;
}
BOOL WINAPI LookupStub(LPCWSTR, LPCWSTR name, PLUID luid) {
    Check(std::wstring(name) == SE_DEBUG_NAME, "unexpected privilege");
    *luid = {};
    SetLastError(lookupError);
    return lookupError == ERROR_SUCCESS;
}
BOOL WINAPI AdjustStub(HANDLE, BOOL, PTOKEN_PRIVILEGES requested, DWORD,
                      PTOKEN_PRIVILEGES, PDWORD) {
    Check(requested->PrivilegeCount == 1, "unexpected privilege count");
    Check(requested->Privileges[0].Attributes == SE_PRIVILEGE_ENABLED, "unexpected attributes");
    SetLastError(adjustError);
    return adjustResult;
}
utils::detail::Win32Api FakeApi() {
    utils::detail::Win32Api api;
    api.openProcess = OpenProcessStub;
    api.closeHandle = CloseStub;
    api.isWow64Process2 = Query2Stub;
    api.isWow64Process = QueryLegacyStub;
    api.getNativeSystemInfo = SystemInfoStub;
    api.openProcessToken = OpenTokenStub;
    api.lookupPrivilegeValue = LookupStub;
    api.adjustTokenPrivileges = AdjustStub;
    return api;
}

int pickerMode = 0;
BOOL WINAPI DialogStub(LPOPENFILENAMEW dialog) {
    Check(dialog->lStructSize == sizeof(OPENFILENAMEW), "wrong dialog structure size");
    Check(dialog->nMaxFile == 260, "nMaxFile must be 260 WCHARs, not 520 bytes");
    Check(dialog->lpstrFile[0] == L'\0', "dialog path must initially be empty");
    Check((dialog->Flags & OFN_NOCHANGEDIR) != 0, "dialog must preserve working directory");
    if (pickerMode == 1) return FALSE; // User cancellation.
    if (pickerMode == 2) {
        // GetOpenFileName writes the required size into the first WORD on failure.
        *reinterpret_cast<WORD*>(dialog->lpstrFile) = 300;
        return FALSE;
    }
    std::fill_n(dialog->lpstrFile, dialog->nMaxFile - 1, L'测');
    dialog->lpstrFile[dialog->nMaxFile - 1] = L'\0';
    return TRUE;
}
void Picker() {
    auto api = FakeApi();
    api.getOpenFileName = DialogStub;
    const auto path = utils::detail::SelectFile(L"DLL\0*.dll\0\0", api);
    Check(path == std::wstring(259, L'测'), "maximum-length Unicode path must round trip");
    pickerMode = 1;
    Check(utils::detail::SelectFile(L"DLL\0*.dll\0\0", api).empty(), "cancel must be empty");
    pickerMode = 2;
    Check(utils::detail::SelectFile(L"DLL\0*.dll\0\0", api).empty(), "buffer failure must not return a path");
}
void ArchitectureModern() {
    auto api = FakeApi();
    using A = utils::ProcessArchitecture;
    auto query = [&] { return utils::detail::QueryProcessArchitecture(42, api); };
    Check(query().architecture == A::X64, "native x64");
    processMachine = IMAGE_FILE_MACHINE_I386;
    Check(query().architecture == A::X86, "WOW64 x86");
    processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    nativeMachine = IMAGE_FILE_MACHINE_I386;
    Check(query().architecture == A::X86, "native x86");
    nativeMachine = IMAGE_FILE_MACHINE_ARM64;
    Check(query().architecture == A::Arm64, "ARM64 must not be called x64");
    nativeMachine = 0xffff;
    const auto unknown = query();
    Check(unknown.architecture == A::Unknown && unknown.error == ERROR_NOT_SUPPORTED,
          "unsupported machine must stay unknown");
    Check(closes == 5 && legacyCalls == 0, "modern queries close handles without fallback");
}
void ArchitectureErrors() {
    auto api = FakeApi();
    openError = ERROR_ACCESS_DENIED;
    auto result = utils::detail::QueryProcessArchitecture(42, api);
    Check(result.architecture == utils::ProcessArchitecture::Unknown, "access denied is not x86");
    Check(result.error == ERROR_ACCESS_DENIED && std::wstring(result.stage) == L"OpenProcess",
          "open-process error and stage must survive");
    Check(closes == 0, "never close an invalid handle");
    openError = ERROR_SUCCESS;
    queryError = ERROR_INVALID_PARAMETER;
    result = utils::detail::QueryProcessArchitecture(42, api);
    Check(result.architecture == utils::ProcessArchitecture::Unknown &&
          result.error == ERROR_INVALID_PARAMETER && std::wstring(result.stage) == L"IsWow64Process2",
          "failed modern query must preserve the error");
    Check(closes == 1 && legacyCalls == 0, "do not silently fall back after query failure");
}
void ArchitectureLegacy() {
    auto api = FakeApi();
    api.isWow64Process2 = nullptr;
    using A = utils::ProcessArchitecture;
    auto query = [&] { return utils::detail::QueryProcessArchitecture(42, api); };
    Check(query().architecture == A::X64, "legacy native x64");
    wow64 = TRUE;
    Check(query().architecture == A::X86, "legacy WOW64 x86");
    wow64 = FALSE;
    nativeProcessor = PROCESSOR_ARCHITECTURE_INTEL;
    Check(query().architecture == A::X86, "32-bit Windows is not x64");
    nativeProcessor = PROCESSOR_ARCHITECTURE_ARM64;
    const auto unsupported = query();
    Check(unsupported.architecture == A::Unknown && unsupported.error == ERROR_NOT_SUPPORTED,
          "legacy ARM64 detection must not guess");
    nativeProcessor = PROCESSOR_ARCHITECTURE_AMD64;
    queryError = ERROR_ACCESS_DENIED;
    const auto failed = query();
    Check(failed.architecture == A::Unknown && failed.error == ERROR_ACCESS_DENIED &&
          std::wstring(failed.stage) == L"IsWow64Process", "legacy query error must survive");
    Check(closes == 5, "legacy queries close all opened handles");
}
void Privileges() {
    auto api = FakeApi();
    Check(utils::detail::EnableDebugPrivilege(api), "TRUE plus ERROR_SUCCESS should succeed");
    Check(GetLastError() == ERROR_SUCCESS, "cleanup must not replace success status");
    adjustError = ERROR_NOT_ALL_ASSIGNED;
    Check(!utils::detail::EnableDebugPrivilege(api), "TRUE plus NOT_ALL_ASSIGNED is failure");
    Check(GetLastError() == ERROR_NOT_ALL_ASSIGNED, "partial-assignment error must survive");
    adjustResult = FALSE;
    adjustError = ERROR_ACCESS_DENIED;
    Check(!utils::detail::EnableDebugPrivilege(api) && GetLastError() == ERROR_ACCESS_DENIED,
          "adjust failure must preserve original error");
    lookupError = ERROR_NO_SUCH_PRIVILEGE;
    Check(!utils::detail::EnableDebugPrivilege(api) && GetLastError() == ERROR_NO_SUCH_PRIVILEGE,
          "lookup failure must preserve original error");
    openError = ERROR_ACCESS_DENIED;
    Check(!utils::detail::EnableDebugPrivilege(api) && GetLastError() == ERROR_ACCESS_DENIED,
          "open-token failure must preserve original error");
    Check(closes == 4, "close only successfully opened tokens");
}
void LiveProcess() {
    const auto result = utils::QueryProcessArchitecture(GetCurrentProcessId());
    const auto expected = sizeof(void*) == 8 ? utils::ProcessArchitecture::X64 : utils::ProcessArchitecture::X86;
    Check(result.architecture == expected && result.error == ERROR_SUCCESS, "current-process architecture");
    const auto processes = utils::GetAllProcesses();
    Check(std::any_of(processes.begin(), processes.end(), [](const auto& p) {
        return p.pid == GetCurrentProcessId() && !p.name.empty();
    }), "process listing must contain this test process");
    const auto invalid = utils::QueryProcessArchitecture(MAXDWORD);
    Check(invalid.architecture == utils::ProcessArchitecture::Unknown && invalid.error != ERROR_SUCCESS,
          "invalid PID must return an explicit error");
}
void UnicodeFile() {
    const auto path = std::filesystem::current_path() /
        (L"P0 中文 path " + std::to_wstring(GetCurrentProcessId()) + L".dat");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove(path, ignored); }
    } cleanup{path};
    {
        std::ofstream file(path, std::ios::binary);
        Check(static_cast<bool>(file), "create Unicode fixture");
        file.write("\0\x7f\xff", 3);
    }
    Check(utils::ReadFileToBuffer(path.wstring()) == std::vector<unsigned char>({0, 127, 255}),
          "Unicode file contents must round trip");
    Check(utils::ReadFileToBuffer(path.wstring() + L".missing").empty(), "missing file");
}
}

int main(int argc, char** argv) {
    try {
        Check(argc == 2, "expected one test case");
        const std::string name = argv[1];
        if (name == "picker") Picker();
        else if (name == "architecture-modern") ArchitectureModern();
        else if (name == "architecture-errors") ArchitectureErrors();
        else if (name == "architecture-legacy") ArchitectureLegacy();
        else if (name == "privileges") Privileges();
        else if (name == "live-process") LiveProcess();
        else if (name == "unicode-file") UnicodeFile();
        else throw std::runtime_error("unknown test case");
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
