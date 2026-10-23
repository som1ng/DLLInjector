#include "cli.h"
#include "dllinjector/text.h"
#include "dllinjector/version.h"
#include <sstream>

namespace dllinjector::cli {
namespace {
constexpr char Help[] =
    "DLLInjector read-only process and PE CLI\n"
    "  dllinjector --help | --version\n"
    "  dllinjector process list [options]\n"
    "  dllinjector process inspect --pid <PID> [options]\n"
    "  dllinjector process modules --pid <PID> [options]\n"
    "  dllinjector pe inspect <path> [options]\n"
    "Options (before or after command):\n"
    "  --json                   One versioned JSON result on stdout\n"
    "  --timeout-ms <0..600000>  Query deadline; default 5000, zero expires immediately\n"
    "  --output <path>          Also save result as UTF-8; existing files are refused\n"
    "  --verbose                Debug diagnostics on stderr\n"
    "Exit codes: 0 success, 1 malformed PE, 2 usage, 3 unavailable/access denied,\n"
    "            4 internal/unsupported/output failure, 5 query timeout.\n"
    "Timeout is cooperative; it cannot interrupt an in-flight Win32 call or output sink.\n"
    "Modules: use the x64 CLI for x64 targets. Loader snapshots can change and exclude data-file loads.\n"
    "PE: inspect headers, sections and directory locations only; never load the input. Limit: 256 MiB.\n"
    "PE inspection does not validate directory contents, dependencies, signatures or loadability.\n";

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
std::string Address(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
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
    if (const auto* snapshot = std::get_if<ModuleSnapshot>(&report.data)) {
        std::string out = "{\"pid\":" + std::to_string(snapshot->pid) +
            ",\"creation_time_filetime\":" + Time(snapshot->creation_time) +
            ",\"architecture\":" + Quote(ArchitectureName(snapshot->architecture.architecture)) +
            ",\"architecture_source\":" + Quote(snapshot->architecture.source) +
            ",\"scope\":\"toolhelp_snapshot\",\"modules\":[";
        bool first = true;
        for (const auto& module : snapshot->modules) {
            if (!first) out += ',';
            first = false;
            out += "{\"name\":" + Quote(Utf8(module.name)) + ",\"path\":" + Quote(Utf8(module.path)) +
                ",\"base_address\":" + Quote(Address(module.base_address)) +
                ",\"size_bytes\":" + std::to_string(module.size_bytes) +
                ",\"name_may_be_truncated\":" + Bool(module.name_may_be_truncated) +
                ",\"path_may_be_truncated\":" + Bool(module.path_may_be_truncated) +
                ",\"text_may_be_lossy\":" + Bool(module.text_may_be_lossy) + "}";
        }
        return out + "]}";
    }
    if (const auto* pe = std::get_if<PeInfo>(&report.data)) {
        std::string out = "{\"scope\":\"headers_sections_directories\",\"file_size\":" + std::to_string(pe->file_size) +
            ",\"format\":" + Quote(pe->pe32_plus ? "PE32+" : "PE32") +
            ",\"machine\":" + std::to_string(pe->machine) + ",\"characteristics\":" + std::to_string(pe->characteristics) +
            ",\"subsystem\":" + std::to_string(pe->subsystem) + ",\"dll_characteristics\":" + std::to_string(pe->dll_characteristics) +
            ",\"entry_point_rva\":" + Quote(Address(pe->entry_point_rva)) + ",\"image_base\":" + Quote(Address(pe->image_base)) +
            ",\"size_of_image\":" + std::to_string(pe->size_of_image) + ",\"size_of_headers\":" + std::to_string(pe->size_of_headers) +
            ",\"section_alignment\":" + std::to_string(pe->section_alignment) + ",\"file_alignment\":" + std::to_string(pe->file_alignment) +
            ",\"sections\":[";
        bool first = true;
        for (const auto& s : pe->sections) {
            if (!first) out += ',';
            first = false;
            out += "{\"name\":" + Quote(s.name) + ",\"name_bytes\":" + Quote(s.name_bytes) +
                ",\"virtual_address\":" + Quote(Address(s.virtual_address)) + ",\"virtual_size\":" + std::to_string(s.virtual_size) +
                ",\"raw_offset\":" + std::to_string(s.raw_offset) + ",\"raw_size\":" + std::to_string(s.raw_size) +
                ",\"characteristics\":" + std::to_string(s.characteristics) + "}";
        }
        out += "],\"directories\":[";
        first = true;
        for (const auto& d : pe->directories) {
            if (!first) out += ',';
            first = false;
            out += "{\"index\":" + std::to_string(d.index) + ",\"address\":" + Quote(Address(d.address)) +
                ",\"size_bytes\":" + std::to_string(d.size_bytes) +
                ",\"address_kind\":" + Quote(d.is_file_offset ? "file_offset" : "rva") +
                ",\"file_offset\":" + Number(d.file_offset) + "}";
        }
        return out + "]}";
    }
    return "null";
}
}

Report Execute(const Options& options) {
    Report report;
    report.accepted = true;
    report.pid = options.pid;
    report.path = options.path;
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
    case Command::Modules: {
        report.command = "process.modules";
        auto result = ListModules(*options.pid, options.query);
        if (result) report.data = std::move(result.value());
        else report.errors.push_back(result.error());
        break;
    }
    case Command::PeInspect: {
        report.command = "pe.inspect";
        auto result = InspectPeFile(*options.path, options.query.timeout_ms);
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
            const bool observed = success && (report.command == "process.list" || report.command == "process.inspect" || report.command == "process.modules");
            const bool staticPe = success && report.command == "pe.inspect";
            const auto description = staticPe
                ? "Static headers, sections and directory ranges only; no loading, directory-content, dependency or signature validation."
                : report.command == "process.modules"
                ? "Toolhelp loader snapshot; may change, excludes data-file loads, may contain truncated/lossy text, and does not verify DLL initialization."
                : "Read-only Windows metadata; not a guarantee of continued process lifetime.";
            return "{\"schema_version\":" + Quote(SchemaVersion) + ",\"tool_version\":" + Quote(ToolVersion) +
                ",\"command\":" + Quote(report.command) + ",\"status\":" + Quote(success ? "success" : "error") +
                ",\"target\":{\"pid\":" + Number(report.pid) + ",\"path\":" +
                (report.path ? Quote(Utf8(*report.path)) : "null") + "},\"execution\":{\"command_accepted\":" +
                Bool(report.accepted) + ",\"operation_completed\":" + Bool(success) +
                ",\"outcome_verified\":" + Bool(success) + "},\"data\":" + Data(report) +
                ",\"findings\":[],\"evidence\":" + (observed || staticPe ?
                    "[{\"source\":" + Quote(staticPe ? "static_analysis" : "runtime_observation") +
                    ",\"description\":" + Quote(description) + "}]" : "[]") +
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
        } else if (const auto* snapshot = std::get_if<ModuleSnapshot>(&report.data)) {
            out << "PID: " << snapshot->pid << " (Toolhelp loader snapshot; excludes data-file loads)\n"
                << "BASE_ADDRESS\tSIZE_BYTES\tNAME\tPATH\tTEXT_STATUS\n";
            for (const auto& module : snapshot->modules)
                out << Address(module.base_address) << '\t' << module.size_bytes << '\t'
                    << Utf8(module.name) << '\t' << Utf8(module.path) << '\t'
                    << (module.text_may_be_lossy ? "possibly lossy" :
                        (module.name_may_be_truncated || module.path_may_be_truncated ? "possibly truncated" : "within capacity")) << '\n';
        } else if (const auto* pe = std::get_if<PeInfo>(&report.data)) {
            out << "PE static inspection: headers, sections and directory locations only\n"
                << "File: " << (report.path ? Utf8(*report.path) : "(bytes)") << '\n'
                << "Format: " << (pe->pe32_plus ? "PE32+" : "PE32") << "  Machine: " << Address(pe->machine)
                << "  Subsystem: " << pe->subsystem << "\nImageBase: " << Address(pe->image_base)
                << "  EntryPoint RVA: " << Address(pe->entry_point_rva) << "\nNAME\tRVA\tVIRTUAL_SIZE\tRAW_OFFSET\tRAW_SIZE\n";
            for (const auto& s : pe->sections)
                out << s.name << '\t' << Address(s.virtual_address) << '\t' << s.virtual_size << '\t'
                    << s.raw_offset << '\t' << s.raw_size << '\n';
            out << "DIRECTORY_INDEX\tADDRESS_KIND\tADDRESS\tSIZE\tFILE_OFFSET\n";
            for (const auto& d : pe->directories)
                out << d.index << '\t' << (d.is_file_offset ? "file_offset" : "rva") << '\t'
                    << Address(d.address) << '\t' << d.size_bytes << '\t' << Number(d.file_offset) << '\n';
            out << "No directory-content, dependency, signature or loadability validation.\n";
        }
        return out.str();
    } catch (const Error& error) { return error; }
}
}
