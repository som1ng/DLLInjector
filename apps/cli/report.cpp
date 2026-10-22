#include "cli.h"
#include "dllinjector/text.h"
#include "dllinjector/version.h"
#include <sstream>

namespace dllinjector::cli {
namespace {
constexpr char Help[] =
    "DLLInjector read-only process CLI\n"
    "  dllinjector --help | --version\n"
    "  dllinjector process list [options]\n"
    "  dllinjector process inspect --pid <PID> [options]\n"
    "Options (before or after command):\n"
    "  --json                   One versioned JSON result on stdout\n"
    "  --timeout-ms <0..600000>  Query deadline; default 5000, zero expires immediately\n"
    "  --output <path>          Also save result as UTF-8; existing files are refused\n"
    "  --verbose                Debug diagnostics on stderr\n"
    "Exit codes: 0 success, 1 reserved findings, 2 usage, 3 unavailable/access denied,\n"
    "            4 internal/unsupported/output failure, 5 query timeout.\n"
    "Timeout is cooperative; it cannot interrupt an in-flight Win32 call or output sink.\n";

std::string Utf8(const std::wstring& text) {
    auto result = ToUtf8(text);
    if (!result) throw result.error();
    return result.value();
}
std::string Quote(const std::string& text) {
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : text) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += static_cast<char>(ch); }
        else if (ch < 0x20) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += static_cast<char>(ch);
    }
    return out + '"';
}
std::string Bool(bool value) { return value ? "true" : "false"; }
template<class T> std::string Number(const std::optional<T>& value) {
    return value ? std::to_string(*value) : "null";
}
std::string Time(const std::optional<std::uint64_t>& value) {
    return value ? Quote(std::to_string(*value)) : "null";
}
std::string Errors(const std::vector<Error>& errors) {
    std::string out = "[";
    for (const auto& error : errors) {
        if (out.size() > 1) out += ',';
        out += "{\"category\":" + Quote(CategoryName(error.category)) +
            ",\"stage\":" + Quote(error.stage) + ",\"message\":" + Quote(error.message) +
            ",\"win32\":" + Number(error.win32) + ",\"ntstatus\":" + Number(error.ntstatus) +
            ",\"verified\":" + Bool(error.verified) + "}";
    }
    return out + ']';
}
std::string Data(const Report& report) {
    if (!report.errors.empty()) return "null";
    if (const auto* info = std::get_if<std::string>(&report.data))
        return "{\"text\":" + Quote(*info) + "}";
    if (const auto* rows = std::get_if<std::vector<ProcessSummary>>(&report.data)) {
        std::string out = "{\"processes\":[";
        bool first = true;
        for (const auto& row : *rows) {
            if (!first) out += ',';
            first = false;
            out += "{\"pid\":" + std::to_string(row.pid) + ",\"name\":" + Quote(Utf8(row.name)) +
                ",\"creation_time_filetime\":" + Time(row.creation_time) +
                ",\"errors\":" + Errors(row.errors) + "}";
        }
        return out + "]}";
    }
    if (const auto* process = std::get_if<ProcessDetails>(&report.data)) {
        return "{\"pid\":" + std::to_string(process->pid) + ",\"name\":" + Quote(Utf8(process->name)) +
            ",\"image_path\":" + Quote(Utf8(process->image_path)) +
            ",\"creation_time_filetime\":" + Time(process->creation_time) +
            ",\"architecture\":" + Quote(ArchitectureName(process->architecture.architecture)) +
            ",\"architecture_source\":" + Quote(process->architecture.source) + "}";
    }
    return "null";
}
}

Report Execute(const Options& options) {
    Report report;
    report.accepted = true;
    report.pid = options.pid;
    switch (options.command) {
    case Command::Help: report.command = "help"; report.data = std::string(Help); break;
    case Command::Version: report.command = "version"; report.data = std::string(ToolVersion); break;
    case Command::List: {
        report.command = "process.list";
        auto result = ListProcesses(options.query);
        if (result) report.data = std::move(result.value());
        else report.errors.push_back(result.error());
        break;
    }
    case Command::Inspect: {
        report.command = "process.inspect";
        auto result = InspectProcess(*options.pid, options.query);
        if (result) report.data = std::move(result.value());
        else report.errors.push_back(result.error());
        break;
    }
    }
    return report;
}

Result<std::string> Render(const Report& report, bool json) {
    try {
        if (json) {
            const bool success = report.errors.empty();
            const bool observed = success && (report.command == "process.list" || report.command == "process.inspect");
            return "{\"schema_version\":" + Quote(SchemaVersion) + ",\"tool_version\":" + Quote(ToolVersion) +
                ",\"command\":" + Quote(report.command) + ",\"status\":" + Quote(success ? "success" : "error") +
                ",\"target\":{\"pid\":" + Number(report.pid) + "},\"execution\":{\"command_accepted\":" +
                Bool(report.accepted) + ",\"operation_completed\":" + Bool(success) +
                ",\"outcome_verified\":" + Bool(success) + "},\"data\":" + Data(report) +
                ",\"findings\":[],\"evidence\":" + (observed ?
                    "[{\"source\":\"runtime_observation\",\"description\":\"Read-only Windows metadata; not a guarantee of continued process lifetime.\"}]" : "[]") +
                ",\"errors\":" + Errors(report.errors) + ",\"duration_ms\":" + std::to_string(report.duration_ms) + "}\n";
        }
        std::ostringstream out;
        if (!report.errors.empty()) {
            for (const auto& error : report.errors)
                out << CategoryName(error.category) << " at " << error.stage << ": " << error.message << '\n';
        } else if (const auto* info = std::get_if<std::string>(&report.data)) out << *info << '\n';
        else if (const auto* rows = std::get_if<std::vector<ProcessSummary>>(&report.data)) {
            out << "PID\tNAME\tCREATION_FILETIME\tMETADATA\n";
            for (const auto& row : *rows)
                out << row.pid << '\t' << Utf8(row.name) << '\t'
                    << (row.creation_time ? std::to_string(*row.creation_time) : "unknown") << '\t'
                    << (row.errors.empty() ? "observed" : row.errors[0].message) << '\n';
        } else if (const auto* process = std::get_if<ProcessDetails>(&report.data)) {
            out << "PID: " << process->pid << "\nName: " << Utf8(process->name)
                << "\nImage: " << Utf8(process->image_path)
                << "\nArchitecture: " << ArchitectureName(process->architecture.architecture)
                << "\nCreation FILETIME: " << process->creation_time << '\n';
        }
        return out.str();
    } catch (const Error& error) { return error; }
}
}
