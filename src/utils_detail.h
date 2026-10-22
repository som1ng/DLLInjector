#pragma once

#include "utils.h"
#include <commdlg.h>
#include "process/process_detail.h"

// Internal Win32 boundary, shared by production wrappers and deterministic tests.
// Not a public Core/CLI API. Tests substitute functions instead of changing tokens
// or opening interactive file dialogs.
namespace utils::detail {
    struct Win32Api : dllinjector::detail::ProcessApi {
        decltype(&::OpenProcessToken) openProcessToken = ::OpenProcessToken;
        decltype(&::LookupPrivilegeValueW) lookupPrivilegeValue = ::LookupPrivilegeValueW;
        decltype(&::AdjustTokenPrivileges) adjustTokenPrivileges = ::AdjustTokenPrivileges;
        decltype(&::GetOpenFileNameW) getOpenFileName = ::GetOpenFileNameW;
    };

    ArchitectureResult QueryProcessArchitecture(DWORD pid, const Win32Api& api);
    bool EnableDebugPrivilege(const Win32Api& api);
    std::wstring SelectFile(const wchar_t* filter, const Win32Api& api);
}
