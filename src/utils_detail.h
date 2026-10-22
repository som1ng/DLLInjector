#pragma once

#include "utils.h"
#include <commdlg.h>

// Internal Win32 boundary, shared by production wrappers and deterministic tests.
// Not a public Core/CLI API. Tests substitute functions instead of changing tokens
// or opening interactive file dialogs.
namespace utils::detail {
    struct Win32Api {
        using IsWow64Process2Fn = BOOL (WINAPI*)(HANDLE, USHORT*, USHORT*);
        decltype(&::OpenProcess) openProcess = ::OpenProcess;
        decltype(&::CloseHandle) closeHandle = ::CloseHandle;
        IsWow64Process2Fn isWow64Process2 = nullptr;
        decltype(&::IsWow64Process) isWow64Process = ::IsWow64Process;
        decltype(&::GetNativeSystemInfo) getNativeSystemInfo = ::GetNativeSystemInfo;
        decltype(&::OpenProcessToken) openProcessToken = ::OpenProcessToken;
        decltype(&::LookupPrivilegeValueW) lookupPrivilegeValue = ::LookupPrivilegeValueW;
        decltype(&::AdjustTokenPrivileges) adjustTokenPrivileges = ::AdjustTokenPrivileges;
        decltype(&::GetOpenFileNameW) getOpenFileName = ::GetOpenFileNameW;
    };

    ArchitectureResult QueryProcessArchitecture(DWORD pid, const Win32Api& api);
    bool EnableDebugPrivilege(const Win32Api& api);
    std::wstring SelectFile(const wchar_t* filter, const Win32Api& api);
}
