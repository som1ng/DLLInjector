#pragma once

#include "dllinjector/process.h"
#include "dllinjector/pe.h"
#include <variant>

namespace dllinjector::cli {
enum class Command { Help, Version, List, Inspect, Modules, PeInspect };
struct Options {
    Command command = Command::Help;
    bool json = false;
    bool verbose = false;
    QueryOptions query;
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> output;
    std::optional<std::wstring> path;
};
struct Report {
    std::string command = "unknown";
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> path;
    bool accepted = false;
    std::variant<std::monostate, std::string, std::vector<ProcessSummary>, ProcessDetails, ModuleSnapshot, PeInfo> data;
    std::vector<Error> errors;
    std::uint64_t duration_ms = 0;
};

Result<Options> Parse(const std::vector<std::wstring>& arguments);
Report Execute(const Options& options);
Result<std::string> Render(const Report& report, bool json);
}
