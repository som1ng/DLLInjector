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

Result<std::vector<ProcessSummary>> ListProcesses(QueryOptions options) {
    return detail::ListProcesses(options, detail::NativeProcessApi());
}
Result<ProcessDetails> InspectProcess(std::uint32_t pid, QueryOptions options) {
    return detail::InspectProcess(pid, options, detail::NativeProcessApi());
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
