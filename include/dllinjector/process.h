#pragma once

#include "result.h"
#include <vector>

namespace dllinjector {
enum class Architecture { Unknown, X86, X64, Arm64 };
struct ArchitectureInfo {
    Architecture architecture;
    std::string source;
};
struct QueryOptions { std::uint32_t timeout_ms = 5000; };
struct ProcessSummary {
    std::uint32_t pid;
    std::wstring name;
    std::optional<std::uint64_t> creation_time;
    std::vector<Error> errors;
};
struct ProcessDetails {
    std::uint32_t pid;
    std::wstring name;
    std::wstring image_path;
    std::uint64_t creation_time;
    ArchitectureInfo architecture;
};
struct ModuleInfo {
    std::wstring name;
    std::wstring path;
    std::uint64_t base_address;
    std::uint32_t size_bytes;
    bool name_may_be_truncated;
    bool path_may_be_truncated;
};
struct ModuleSnapshot {
    std::uint32_t pid;
    std::uint64_t creation_time;
    ArchitectureInfo architecture;
    std::vector<ModuleInfo> modules;
};

Result<std::vector<ProcessSummary>> ListProcesses(QueryOptions options = {});
Result<ProcessDetails> InspectProcess(std::uint32_t pid, QueryOptions options = {});
Result<ModuleSnapshot> ListModules(std::uint32_t pid, QueryOptions options = {});
Result<ArchitectureInfo> QueryProcessArchitecture(std::uint32_t pid, QueryOptions options = {});
const char* ArchitectureName(Architecture architecture);
}
