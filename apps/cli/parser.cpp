#include "cli.h"
#include <limits>
#include <set>

namespace dllinjector::cli {
namespace {
Error Usage(const char* message) {
    return {ErrorCategory::InvalidArgument, "cli.parse", message, {}, {}};
}
Result<std::uint32_t> Number(const std::wstring& text, std::uint32_t minimum, std::uint32_t maximum) {
    if (text.empty()) return Usage("Expected an unsigned decimal integer.");
    std::uint32_t value = 0;
    for (const wchar_t ch : text) {
        if (ch < L'0' || ch > L'9') return Usage("Expected an unsigned decimal integer.");
        const auto digit = static_cast<std::uint32_t>(ch - L'0');
        if (value > (maximum - digit) / 10) return Usage("Numeric option is out of range.");
        value = value * 10 + digit;
    }
    if (value < minimum || value > maximum) return Usage("Numeric option is out of range.");
    return value;
}
}

Result<Options> Parse(const std::vector<std::wstring>& args) {
    Options options;
    std::set<std::wstring> seen;
    std::vector<std::wstring> words;
    bool help = false, version = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& arg = args[i];
        if (arg.empty() || arg[0] != L'-') { words.push_back(arg); continue; }
        if (!seen.insert(arg).second && arg != L"--search-dir") return Usage("Duplicate option; only --search-dir may be repeated.");
        if (arg == L"--json") options.json = true;
        else if (arg == L"--verbose") options.verbose = true;
        else if (arg == L"--help") help = true;
        else if (arg == L"--version") version = true;
        else if (arg == L"--pid" || arg == L"--timeout-ms" || arg == L"--output" || arg == L"--target-arch" || arg == L"--search-dir" || arg == L"--format") {
            if (++i == args.size() || args[i].empty() || args[i].rfind(L"--", 0) == 0)
                return Usage("Missing option value; consult --help.");
            if (arg == L"--output") options.output = args[i];
            else if (arg == L"--format") {
                if (args[i] == L"markdown") options.document_format = DocumentFormat::Markdown;
                else if (args[i] == L"html") options.document_format = DocumentFormat::Html;
                else return Usage("--format requires markdown or html.");
            }
            else if (arg == L"--search-dir") {
                if (options.search_directories.size() == MaxDependencySearchDirectories)
                    return Usage("doctor supports at most 16 --search-dir options.");
                options.search_directories.push_back(args[i]);
            } else if (arg == L"--target-arch") {
                if (args[i] == L"x86") options.target_architecture = Architecture::X86;
                else if (args[i] == L"x64") options.target_architecture = Architecture::X64;
                else return Usage("--target-arch requires x86 or x64.");
            } else {
                auto number = Number(args[i], arg == L"--pid" ? 1u : 0u,
                    arg == L"--pid" ? (std::numeric_limits<std::uint32_t>::max)() : 600000u);
                if (!number) return number.error();
                if (arg == L"--pid") options.pid = number.value();
                else options.query.timeout_ms = number.value();
            }
        } else return Usage("Unknown option; consult --help.");
    }
    if (help || version) {
        if ((help && version) || !words.empty() || options.pid)
            return Usage("Use --help or --version without a command, path or PID.");
        options.command = help ? Command::Help : Command::Version;
    } else if (words == std::vector<std::wstring>{L"process", L"list"}) {
        if (options.pid) return Usage("--pid is only supported by process inspect/modules.");
        options.command = Command::List;
    } else if (words == std::vector<std::wstring>{L"process", L"inspect"}) {
        if (!options.pid) return Usage("process inspect requires --pid.");
        options.command = Command::Inspect;
    } else if (words == std::vector<std::wstring>{L"process", L"modules"}) {
        if (!options.pid) return Usage("process modules requires --pid.");
        options.command = Command::Modules;
    } else if (words.size() == 3 && words[0] == L"pe" && words[1] == L"inspect") {
        if (options.pid || words[2].empty()) return Usage("pe inspect requires one nonempty file path and does not accept --pid.");
        options.command = Command::PeInspect;
        options.path = words[2];
    } else if (words.size() == 2 && words[0] == L"doctor") {
        if (options.pid || words[1].empty()) return Usage("doctor requires one nonempty file path and does not accept --pid.");
        options.command = Command::Doctor;
        options.path = words[1];
    } else return Usage("Expected --help, --version, process list/inspect/modules, pe inspect <path> or doctor <path>.");
    if (options.target_architecture && options.command != Command::Doctor)
        return Usage("--target-arch is only supported by doctor.");
    if (!options.search_directories.empty() && options.command != Command::Doctor)
        return Usage("--search-dir is only supported by doctor.");
    if (options.document_format && (options.json || options.command == Command::Help || options.command == Command::Version))
        return Usage("--format requires a diagnostic command and cannot be combined with --json.");
    return options;
}
}
