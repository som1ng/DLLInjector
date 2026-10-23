#pragma once

#include "dllinjector/process.h"
#include <windows.h>
#include <tlhelp32.h>

namespace dllinjector::detail {
// Internal injectable API boundary; public Core headers contain no Windows/UI types.
struct ProcessApi {
    using IsWow64Process2Fn = BOOL (WINAPI*)(HANDLE, USHORT*, USHORT*);
    decltype(&::OpenProcess) openProcess = ::OpenProcess;
    decltype(&::CloseHandle) closeHandle = ::CloseHandle;
    IsWow64Process2Fn isWow64Process2 = nullptr;
    decltype(&::IsWow64Process) isWow64Process = ::IsWow64Process;
    decltype(&::GetNativeSystemInfo) getNativeSystemInfo = ::GetNativeSystemInfo;
    decltype(&::CreateToolhelp32Snapshot) snapshot = ::CreateToolhelp32Snapshot;
    decltype(&::Process32FirstW) first = ::Process32FirstW;
    decltype(&::Process32NextW) next = ::Process32NextW;
    decltype(&::Module32FirstW) moduleFirst = ::Module32FirstW;
    decltype(&::Module32NextW) moduleNext = ::Module32NextW;
    decltype(&::Sleep) sleep = ::Sleep;
    decltype(&::GetProcessTimes) times = ::GetProcessTimes;
    decltype(&::QueryFullProcessImageNameW) imageName = ::QueryFullProcessImageNameW;
    decltype(&::GetTickCount64) tick = ::GetTickCount64;
};
ProcessApi NativeProcessApi();
Result<ArchitectureInfo> QueryProcessArchitecture(std::uint32_t pid, QueryOptions options, const ProcessApi& api);
Result<std::vector<ProcessSummary>> ListProcesses(QueryOptions options, const ProcessApi& api);
Result<ProcessDetails> InspectProcess(std::uint32_t pid, QueryOptions options, const ProcessApi& api);
Result<ModuleSnapshot> ListModules(std::uint32_t pid, QueryOptions options, const ProcessApi& api);
}
