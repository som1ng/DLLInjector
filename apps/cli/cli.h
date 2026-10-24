#pragma once

#include "dllinjector/process.h"
#include "dllinjector/pe.h"
#include "dllinjector/doctor.h"
#include <variant>

namespace dllinjector::cli {
enum class Command { Help, Version, List, Inspect, Modules, PeInspect, Doctor };
enum class DocumentFormat { Markdown, Html };
struct Options {
    Command command = Command::Help;
    bool json = false;
    bool verbose = false;
    std::optional<DocumentFormat> document_format;
    QueryOptions query;
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> output;
    std::optional<std::wstring> path;
    std::optional<Architecture> target_architecture;
    std::vector<std::wstring> search_directories;
};
struct Report {
    std::string command = "unknown";
    std::optional<std::uint32_t> pid;
    std::optional<std::wstring> path;
    bool accepted = false;
    std::variant<std::monostate, std::string, std::vector<ProcessSummary>, ProcessDetails, ModuleSnapshot, PeInfo, DoctorReport> data;
    std::vector<Error> errors;
    std::uint64_t duration_ms = 0;
};

Result<Options> Parse(const std::vector<std::wstring>& arguments);
Report Execute(const Options& options);
Result<std::string> Render(const Report& report, bool json);
// Presentation only: preserves the JSON report and never repeats a query.
Result<std::string> RenderDocument(const Report& report, DocumentFormat format);
std::vector<FindingEvidence> ReportEvidence(const Report& report);
int ReportExitCode(const Report& report);
}
