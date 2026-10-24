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
    "  dllinjector doctor <path> [--target-arch x86|x64] [options]\n"
    "Options (before or after command):\n"
    "  --json                   One versioned JSON result on stdout\n"
    "  --timeout-ms <0..600000>  Query deadline; default 5000, zero expires immediately\n"
    "  --output <path>          Also save result as UTF-8; existing files are refused\n"
    "  --verbose                Debug diagnostics on stderr\n"
    "  --target-arch <x86|x64>   Doctor only: declared target process architecture\n"
    "  --search-dir <path>      Doctor only: inspect dependency candidates here; repeat up to 16 times\n"
    "Exit codes: 0 no blocking findings, 1 malformed PE/Doctor blocking finding,\n"
    "            2 usage, 3 unavailable/access denied,\n"
    "            4 internal/unsupported/output failure, 5 query timeout.\n"
    "Timeout is cooperative; it cannot interrupt an in-flight Win32 call or output sink.\n"
    "Modules: use the x64 CLI for x64 targets. Loader snapshots can change and exclude data-file loads.\n"
    "PE: inspect headers, sections, imports, exports and delay imports; never load the input. Limit: 256 MiB.\n"
    "PE inspection does not resolve dependencies or validate signatures and loadability.\n"
    "Doctor: static rules with evidence and next steps; outcome_verified is always false.\n"
    "Doctor status=success means diagnosis completed; error findings still exit 1.\n"
    "Search directories are explicit observations in the caller's filesystem view, not loader resolution.\n";

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
std::string ImportLibraries(const std::vector<PeImportLibrary>& libraries) {
    std::string out = "[";
    for (const auto& lib : libraries) {
        if (out.size() > 1) out += ',';
        out += "{\"name\":" + Quote(lib.name) + ",\"lookup_source\":" + Quote(lib.lookup_source) +
            ",\"timestamp\":" + std::to_string(lib.timestamp) + ",\"symbols\":[";
        bool first = true;
        for (const auto& symbol : lib.symbols) {
            if (!first) out += ',';
            first = false;
            out += "{\"name\":" + (symbol.name ? Quote(*symbol.name) : "null") +
                ",\"ordinal\":" + Number(symbol.ordinal) + ",\"hint\":" + Number(symbol.hint) + "}";
        }
        out += "]}";
    }
    return out + ']';
}
std::string ExportTable(const std::optional<PeExports>& table) {
    if (!table) return "null";
    std::string out = "{\"dll_name\":" + Quote(table->dll_name) +
        ",\"ordinal_base\":" + std::to_string(table->ordinal_base) +
        ",\"address_table_entries\":" + std::to_string(table->address_table_entries) + ",\"symbols\":[";
    bool first = true;
    for (const auto& symbol : table->symbols) {
        if (!first) out += ',';
        first = false;
        out += "{\"ordinal\":" + std::to_string(symbol.ordinal) + ",\"rva\":" + Quote(Address(symbol.rva)) + ",\"names\":[";
        bool firstName = true;
        for (const auto& name : symbol.names) {
            if (!firstName) out += ',';
            firstName = false; out += Quote(name);
        }
        out += "],\"forwarder\":" + (symbol.forwarder ? Quote(*symbol.forwarder) : "null") + "}";
    }
    return out + "]}";
}
std::string ErrorJson(const Error& error) {
    return "{\"category\":" + Quote(CategoryName(error.category)) +
        ",\"stage\":" + Quote(error.stage) + ",\"message\":" + Quote(error.message) +
        ",\"win32\":" + Number(error.win32) + ",\"ntstatus\":" + Number(error.ntstatus) +
        ",\"verified\":" + Bool(error.verified) + "}";
}
std::string Errors(const std::vector<Error>& errors) {
    std::string out = "[";
    for (const auto& error : errors) {
        if (out.size() > 1) out += ',';
        out += ErrorJson(error);
    }
    return out + ']';
}
std::string Findings(const Report& report) {
    const auto* doctor = std::get_if<DoctorReport>(&report.data);
    if (!report.errors.empty() || !doctor) return "[]";
    std::string out = "[";
    for (const auto& f : doctor->findings) {
        if (out.size() > 1) out += ',';
        out += "{\"rule_id\":" + Quote(f.rule_id) + ",\"severity\":" + Quote(SeverityName(f.severity)) +
            ",\"message\":" + Quote(f.message) + ",\"confidence\":" + Quote(f.confidence) +
            ",\"recommended_next_step\":" + Quote(f.recommended_next_step) + ",\"evidence\":[";
        bool first = true;
        for (const auto& evidence : f.evidence) {
            if (!first) out += ',';
            first = false;
            out += "{\"source\":" + Quote(evidence.source) + ",\"description\":" + Quote(evidence.description) + "}";
        }
        out += "],\"documentation\":[";
        first = true;
        for (const auto& url : f.documentation) {
            if (!first) out += ',';
            first = false; out += Quote(url);
        }
        out += "]}";
    }
    return out + ']';
}
std::string Dependencies(const std::optional<DependencyReport>& report) {
    if (!report) return "null";
    std::string out = "{\"scope\":\"explicit_directories\",\"filesystem_view\":\"caller_process\",\"search_directories\":[";
    bool first = true;
    for (const auto& directory : report->search_directories) {
        if (!first) out += ',';
        first = false; out += Quote(Utf8(directory));
    }
    out += "],\"libraries\":[";
    first = true;
    for (const auto& library : report->libraries) {
        if (!first) out += ',';
        first = false;
        out += "{\"name\":" + Quote(library.name) + ",\"kind\":" + Quote(library.delay_import ? "delay_import" : "import") +
            ",\"status\":" + Quote(DependencyStatusName(library.status)) + ",\"candidates\":[";
        bool firstCandidate = true;
        for (const auto& candidate : library.candidates) {
            if (!firstCandidate) out += ',';
            firstCandidate = false;
            out += "{\"directory_index\":" + std::to_string(candidate.directory_index) +
                ",\"status\":" + Quote(CandidateStatusName(candidate.status)) +
                ",\"error\":" + (candidate.error ? ErrorJson(*candidate.error) : "null") + "}";
        }
        out += "]}";
    }
    return out + "]}";
}
std::string Data(const Report& report) {
    if (!report.errors.empty()) return "null";
    if (const auto* info = std::get_if<std::string>(&report.data))
        return "{\"text\":" + Quote(*info) + "}";
    if (const auto* doctor = std::get_if<DoctorReport>(&report.data)) {
        std::string out = "{\"scope\":\"static_dll_preflight\",\"target_architecture\":" +
            (doctor->target_architecture ? Quote(ArchitectureName(*doctor->target_architecture)) : "null") + ",\"image\":";
        if (!doctor->image) out += "null";
        else {
            const auto& p = *doctor->image;
            out += "{\"machine\":" + std::to_string(p.machine) + ",\"characteristics\":" + std::to_string(p.characteristics) +
                ",\"format\":" + Quote(p.pe32_plus ? "PE32+" : "PE32") + ",\"has_clr_header\":" + Bool(p.has_clr_header) +
                ",\"import_library_count\":" + std::to_string(p.import_library_count) +
                ",\"delay_import_library_count\":" + std::to_string(p.delay_import_library_count) + "}";
        }
        return out + ",\"inspection_error\":" + (doctor->inspection_error ? ErrorJson(*doctor->inspection_error) : "null") +
            ",\"dependencies\":" + Dependencies(doctor->dependencies) + "}";
    }
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
        std::string out = "{\"scope\":\"headers_sections_imports_exports_delay_imports\",\"file_size\":" + std::to_string(pe->file_size) +
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
        return out + "],\"imports\":" + ImportLibraries(pe->imports) + ",\"delay_imports\":" +
            ImportLibraries(pe->delay_imports) + ",\"exports\":" + ExportTable(pe->exports) + "}";
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
    case Command::Doctor: {
        report.command = "doctor";
        auto result = InspectDoctorFile(*options.path, options.target_architecture, options.search_directories, options.query.timeout_ms);
        if (result) report.data = std::move(result.value());
        else report.errors.push_back(result.error());
        break;
    }
    }
    return report;
}

int ReportExitCode(const Report& report) {
    if (!report.errors.empty()) return ExitCode(report.errors.front().category);
    const auto* doctor = std::get_if<DoctorReport>(&report.data);
    return doctor && HasBlockingFindings(*doctor) ? 1 : 0;
}

Result<std::string> Render(const Report& report, bool json) {
    try {
        if (json) {
            const bool success = report.errors.empty();
            const bool observed = success && (report.command == "process.list" || report.command == "process.inspect" || report.command == "process.modules");
            const bool staticPe = success && report.command == "pe.inspect";
            const bool doctor = success && report.command == "doctor";
            const auto description = doctor
                ? "Static DLL preflight only; declared target architecture is not observed. Dependencies, runtime loading and initialization remain unverified."
                : staticPe
                ? "Static headers, sections, imports, exports and delay imports; no loading, dependency resolution, signature or loadability verification."
                : report.command == "process.modules"
                ? "Toolhelp loader snapshot; may change, excludes data-file loads, may contain truncated/lossy text, and does not verify DLL initialization."
                : "Read-only Windows metadata; not a guarantee of continued process lifetime.";
            std::string evidence = "[]";
            if (observed || staticPe || doctor) {
                evidence = "[{\"source\":" + Quote(staticPe || doctor ? "static_analysis" : "runtime_observation") +
                    ",\"description\":" + Quote(description) + "}";
                const auto* diagnosis = std::get_if<DoctorReport>(&report.data);
                if (doctor && diagnosis && diagnosis->dependencies)
                    evidence += ",{\"source\":\"filesystem_observation\",\"description\":\"File attributes in explicit directories, using the caller process's filesystem view (including WOW64 redirection). No loader resolution or candidate content verification.\"}";
                evidence += ']';
            }
            return "{\"schema_version\":" + Quote(SchemaVersion) + ",\"tool_version\":" + Quote(ToolVersion) +
                ",\"command\":" + Quote(report.command) + ",\"status\":" + Quote(success ? "success" : "error") +
                ",\"target\":{\"pid\":" + Number(report.pid) + ",\"path\":" +
                (report.path ? Quote(Utf8(*report.path)) : "null") + "},\"execution\":{\"command_accepted\":" +
                Bool(report.accepted) + ",\"operation_completed\":" + Bool(success) +
                ",\"outcome_verified\":" + Bool(success && !doctor) + "},\"data\":" + Data(report) +
                ",\"findings\":" + Findings(report) + ",\"evidence\":" + evidence +
                ",\"errors\":" + Errors(report.errors) + ",\"duration_ms\":" + std::to_string(report.duration_ms) + "}\n";
        }
        std::ostringstream out;
        if (!report.errors.empty()) {
            for (const auto& error : report.errors)
                out << CategoryName(error.category) << " at " << error.stage << ": " << error.message << '\n';
        } else if (const auto* info = std::get_if<std::string>(&report.data)) out << *info << '\n';
        else if (const auto* doctor = std::get_if<DoctorReport>(&report.data)) {
            out << "DLL Doctor: static diagnosis completed; runtime loading remains unverified.\n"
                << "File: " << (report.path ? Utf8(*report.path) : "(inspection)") << '\n'
                << "Declared target architecture: " << (doctor->target_architecture ? ArchitectureName(*doctor->target_architecture) : "not supplied") << '\n';
            for (const auto& f : doctor->findings) {
                out << '[' << SeverityName(f.severity) << "] " << f.rule_id << ": " << f.message << '\n';
                for (const auto& evidence : f.evidence)
                    out << "  Evidence (" << evidence.source << "): " << evidence.description << '\n';
                out << "  Confidence: " << f.confidence << "\n  Next: " << f.recommended_next_step << '\n';
                for (const auto& url : f.documentation) out << "  Docs: " << url << '\n';
            }
            if (doctor->dependencies) {
                const auto& dependencies = *doctor->dependencies;
                out << "Dependency candidates (caller filesystem view; not loader search order):\n";
                for (std::size_t i = 0; i < dependencies.search_directories.size(); ++i)
                    out << "  Directory " << i << ": " << Utf8(dependencies.search_directories[i]) << '\n';
                for (const auto& library : dependencies.libraries) {
                    out << (library.delay_import ? "delay_import" : "import") << '\t' << library.name << '\t'
                        << DependencyStatusName(library.status) << '\n';
                    for (const auto& candidate : library.candidates) {
                        out << "  Directory " << candidate.directory_index << ": " << CandidateStatusName(candidate.status);
                        if (candidate.error) out << " (" << CategoryName(candidate.error->category) << ", win32=" << Number(candidate.error->win32) << ')';
                        out << '\n';
                    }
                }
            }
        }
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
            out << "PE static inspection: headers, sections, imports, exports and delay imports\n"
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
            for (const auto& kind : {false, true}) {
                out << (kind ? "DELAY_IMPORTS" : "IMPORTS") << "\nDLL\tNAME_OR_ORDINAL\tHINT\n";
                for (const auto& lib : (kind ? pe->delay_imports : pe->imports)) {
                    if (lib.symbols.empty()) out << lib.name << "\t(no symbols)\n";
                    for (const auto& symbol : lib.symbols)
                        out << lib.name << '\t' << (symbol.name ? *symbol.name : "#" + Number(symbol.ordinal))
                            << '\t' << Number(symbol.hint) << '\n';
                }
            }
            out << "EXPORTS\nORDINAL\tRVA\tNAME\tFORWARDER\n";
            if (pe->exports) {
                out << "DLL: " << pe->exports->dll_name << '\n';
                for (const auto& symbol : pe->exports->symbols) {
                    out << symbol.ordinal << '\t' << Address(symbol.rva) << '\t';
                    for (std::size_t i = 0; i < symbol.names.size(); ++i) {
                        if (i) out << ", ";
                        out << symbol.names[i];
                    }
                    out << '\t' << symbol.forwarder.value_or("") << '\n';
                }
            }
            out << "No dependency resolution, signature or loadability verification.\n";
        }
        return out.str();
    } catch (const Error& error) { return error; }
}
}
