#include "process_detail.h"
#include "common/win32.h"
#include <algorithm>

namespace dllinjector {
namespace {
class Deadline {
public:
    Deadline(QueryOptions options, const detail::ProcessApi& api)
        : api_(api), start_(api.tick()), budget_(options.timeout_ms) {}
    bool expired() const { return api_.tick() - start_ >= budget_; }
    Error error() const {
        return {ErrorCategory::Timeout, "query.deadline", "The query deadline was exceeded.", {}, {}};
    }
private:
    const detail::ProcessApi& api_;
    ULONGLONG start_;
    std::uint32_t budget_;
};
Error InvalidPid() {
    return {ErrorCategory::InvalidArgument, "process.pid", "PID must be between 1 and 4294967295.", {}, {}};
}
Result<std::uint64_t> CreationTime(HANDLE handle, const detail::ProcessApi& api) {
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!api.times(handle, &creation, &exit, &kernel, &user))
        return detail::Win32Error(GetLastError(), "GetProcessTimes");
    return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
}
Result<ArchitectureInfo> ArchitectureForHandle(HANDLE handle, const detail::ProcessApi& api) {
    if (api.isWow64Process2) {
        USHORT process = IMAGE_FILE_MACHINE_UNKNOWN, native = IMAGE_FILE_MACHINE_UNKNOWN;
        if (!api.isWow64Process2(handle, &process, &native))
            return detail::Win32Error(GetLastError(), "IsWow64Process2");
        const USHORT machine = process == IMAGE_FILE_MACHINE_UNKNOWN ? native : process;
        switch (machine) {
        case IMAGE_FILE_MACHINE_I386: return ArchitectureInfo{Architecture::X86, "IsWow64Process2"};
        case IMAGE_FILE_MACHINE_AMD64: return ArchitectureInfo{Architecture::X64, "IsWow64Process2"};
        case IMAGE_FILE_MACHINE_ARM64: return ArchitectureInfo{Architecture::Arm64, "IsWow64Process2"};
        default: return detail::Win32Error(ERROR_NOT_SUPPORTED, "IsWow64Process2");
        }
    }
    SYSTEM_INFO native{};
    api.getNativeSystemInfo(&native);
    if (native.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_INTEL &&
        native.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
        return detail::Win32Error(ERROR_NOT_SUPPORTED, "GetNativeSystemInfo");
    BOOL wow64 = FALSE;
    if (!api.isWow64Process(handle, &wow64))
        return detail::Win32Error(GetLastError(), "IsWow64Process");
    return ArchitectureInfo{native.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL || wow64
        ? Architecture::X86 : Architecture::X64, "IsWow64Process"};
}
}

detail::ProcessApi detail::NativeProcessApi() {
    ProcessApi api;
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    if (kernel32) api.isWow64Process2 = reinterpret_cast<ProcessApi::IsWow64Process2Fn>(
        GetProcAddress(kernel32, "IsWow64Process2"));
    return api;
}

Result<ArchitectureInfo> detail::QueryProcessArchitecture(std::uint32_t pid, QueryOptions options,
                                                         const ProcessApi& api) {
    if (!pid) return InvalidPid();
    const Deadline deadline(options, api);
    if (deadline.expired()) return deadline.error();
    UniqueHandle process(api.openProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), api.closeHandle);
    if (!process) return Win32Error(GetLastError(), "OpenProcess");
    if (deadline.expired()) return deadline.error();
    auto result = ArchitectureForHandle(process.get(), api);
    if (deadline.expired()) return deadline.error();
    return result;
}

Result<std::vector<ProcessSummary>> detail::ListProcesses(QueryOptions options, const ProcessApi& api) {
    const Deadline deadline(options, api);
    if (deadline.expired()) return deadline.error();
    UniqueHandle snapshot(api.snapshot(TH32CS_SNAPPROCESS, 0), api.closeHandle);
    if (!snapshot) return Win32Error(GetLastError(), "CreateToolhelp32Snapshot");
    if (deadline.expired()) return deadline.error();
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    BOOL found = api.first(snapshot.get(), &entry);
    DWORD error = found ? ERROR_SUCCESS : GetLastError();
    const char* stage = "Process32FirstW";
    std::vector<ProcessSummary> result;
    while (found) {
        if (deadline.expired()) return deadline.error();
        if (entry.th32ProcessID != 0) {
            ProcessSummary row{entry.th32ProcessID, entry.szExeFile, {}, {}};
            UniqueHandle process(api.openProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, row.pid), api.closeHandle);
            if (!process) row.errors.push_back(Win32Error(GetLastError(), "OpenProcess"));
            if (deadline.expired()) return deadline.error();
            if (process) {
                const auto time = CreationTime(process.get(), api);
                if (time) row.creation_time = time.value();
                else row.errors.push_back(time.error());
            }
            result.push_back(std::move(row));
        }
        if (deadline.expired()) return deadline.error();
        found = api.next(snapshot.get(), &entry);
        error = found ? ERROR_SUCCESS : GetLastError();
        stage = "Process32NextW";
    }
    if (deadline.expired()) return deadline.error();
    if (error != ERROR_NO_MORE_FILES) return Win32Error(error, stage);
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.pid < b.pid; });
    if (deadline.expired()) return deadline.error();
    return result;
}

Result<ProcessDetails> detail::InspectProcess(std::uint32_t pid, QueryOptions options, const ProcessApi& api) {
    if (!pid) return InvalidPid();
    const Deadline deadline(options, api);
    if (deadline.expired()) return deadline.error();
    UniqueHandle process(api.openProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), api.closeHandle);
    if (!process) return Win32Error(GetLastError(), "OpenProcess");
    if (deadline.expired()) return deadline.error();
    // Full Win32 image-path capacity in WCHARs, not bytes; no MAX_PATH truncation.
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    if (!api.imageName(process.get(), 0, path.data(), &size))
        return Win32Error(GetLastError(), "QueryFullProcessImageNameW");
    if (size >= path.size()) return Win32Error(ERROR_INSUFFICIENT_BUFFER, "QueryFullProcessImageNameW");
    path.resize(size);
    if (deadline.expired()) return deadline.error();
    const auto time = CreationTime(process.get(), api);
    if (deadline.expired()) return deadline.error();
    if (!time) return time.error();
    const auto architecture = ArchitectureForHandle(process.get(), api);
    if (deadline.expired()) return deadline.error();
    if (!architecture) return architecture.error();
    const auto separator = path.find_last_of(L"\\/");
    auto name = path.substr(separator == std::wstring::npos ? 0 : separator + 1);
    return ProcessDetails{pid, std::move(name), std::move(path), time.value(), architecture.value()};
}

Result<ModuleSnapshot> detail::ListModules(std::uint32_t pid, QueryOptions options, const ProcessApi& api) {
    if (!pid) return InvalidPid();
    const Deadline deadline(options, api);
    if (deadline.expired()) return deadline.error();
    UniqueHandle process(api.openProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid), api.closeHandle);
    const DWORD openError = process ? ERROR_SUCCESS : GetLastError();
    if (deadline.expired()) return deadline.error();
    if (!process) return Win32Error(openError, "OpenProcess");
    const auto architecture = ArchitectureForHandle(process.get(), api);
    if (deadline.expired()) return deadline.error();
    if (!architecture) return architecture.error();
    if (architecture.value().architecture == Architecture::Arm64)
        return Error{ErrorCategory::Unsupported, "modules.architecture",
                     "ARM64 module snapshots are not supported by this build.", {}, {}};
    if (sizeof(void*) == 4 && architecture.value().architecture == Architecture::X64)
        return Error{ErrorCategory::Unsupported, "modules.architecture",
                     "A 32-bit caller cannot enumerate a 64-bit target; use the x64 CLI.", {}, {}};
    const auto time = CreationTime(process.get(), api);
    if (deadline.expired()) return deadline.error();
    if (!time) return time.error();

    const DWORD flags = TH32CS_SNAPMODULE | (sizeof(void*) == 8 ? TH32CS_SNAPMODULE32 : 0);
    HANDLE rawSnapshot = INVALID_HANDLE_VALUE;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        if (deadline.expired()) return deadline.error();
        rawSnapshot = api.snapshot(flags, pid);
        if (rawSnapshot && rawSnapshot != INVALID_HANDLE_VALUE) break;
        const DWORD error = GetLastError();
        if (deadline.expired()) return deadline.error();
        if (error != ERROR_BAD_LENGTH || attempt == 7) {
            auto failure = Win32Error(error, "CreateToolhelp32Snapshot");
            if (error == ERROR_BAD_LENGTH)
                failure.message = "The module snapshot remained unstable after 8 attempts; retry when the target is stable.";
            return failure;
        }
        api.sleep(1);
    }
    UniqueHandle snapshot(rawSnapshot, api.closeHandle);
    if (deadline.expired()) return deadline.error();
    ModuleSnapshot result{pid, time.value(), architecture.value(), {}};
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    BOOL found = api.moduleFirst(snapshot.get(), &entry);
    DWORD error = found ? ERROR_SUCCESS : GetLastError();
    const char* stage = "Module32FirstW";
    while (found) {
        if (deadline.expired()) return deadline.error();
        const auto nameEnd = std::find(std::begin(entry.szModule), std::end(entry.szModule), L'\0');
        const auto pathEnd = std::find(std::begin(entry.szExePath), std::end(entry.szExePath), L'\0');
        if (entry.th32ProcessID != pid || nameEnd == std::end(entry.szModule) || pathEnd == std::end(entry.szExePath))
            return Error{ErrorCategory::System, "modules.entry",
                         "The module snapshot contains an invalid PID or unterminated text field.", {}, {}};
        result.modules.push_back({std::wstring(entry.szModule, nameEnd), std::wstring(entry.szExePath, pathEnd),
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(entry.modBaseAddr)), entry.modBaseSize,
            nameEnd == std::end(entry.szModule) - 1, pathEnd == std::end(entry.szExePath) - 1,
            std::find(entry.szModule, nameEnd, L'?') != nameEnd || std::find(entry.szExePath, pathEnd, L'?') != pathEnd});
        if (deadline.expired()) return deadline.error();
        found = api.moduleNext(snapshot.get(), &entry);
        error = found ? ERROR_SUCCESS : GetLastError();
        stage = "Module32NextW";
    }
    if (deadline.expired()) return deadline.error();
    if (error != ERROR_NO_MORE_FILES) return Win32Error(error, stage);
    std::sort(result.modules.begin(), result.modules.end(), [](const auto& a, const auto& b) {
        if (a.base_address != b.base_address) return a.base_address < b.base_address;
        return a.path < b.path;
    });
    if (deadline.expired()) return deadline.error();
    return result;
}

Result<std::vector<ProcessSummary>> ListProcesses(QueryOptions options) {
    return detail::ListProcesses(options, detail::NativeProcessApi());
}
Result<ProcessDetails> InspectProcess(std::uint32_t pid, QueryOptions options) {
    return detail::InspectProcess(pid, options, detail::NativeProcessApi());
}
Result<ModuleSnapshot> ListModules(std::uint32_t pid, QueryOptions options) {
    return detail::ListModules(pid, options, detail::NativeProcessApi());
}
Result<ArchitectureInfo> QueryProcessArchitecture(std::uint32_t pid, QueryOptions options) {
    return detail::QueryProcessArchitecture(pid, options, detail::NativeProcessApi());
}
const char* ArchitectureName(Architecture architecture) {
    switch (architecture) {
    case Architecture::X86: return "x86";
    case Architecture::X64: return "x64";
    case Architecture::Arm64: return "arm64";
    default: return "unknown";
    }
}
}
