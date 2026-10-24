// Fixed in-memory reports: no filesystem or process observations in this fixture driver.
#include "cli.h"
#include <iostream>
#include <stdexcept>

using namespace dllinjector;
namespace {
cli::Report Sample(const std::string& name) {
    cli::Report report;
    report.command = "doctor";
    report.accepted = true;
    report.path = L"C:\\fixtures\\中文 plugin.dll";
    report.duration_ms = 17;
    PeInfo pe{};
    pe.file_size = 1024;
    pe.machine = 0x14c;
    pe.characteristics = 0x2002;
    pe.subsystem = 3;
    pe.size_of_image = 8192;
    pe.size_of_headers = pe.file_alignment = 512;
    pe.section_alignment = 4096;
    pe.image_base = 0x400000;
    pe.sections.push_back({".text", "2e74657874000000", 4096, 512, 512, 512, 0x60000020});
    auto diagnosis = DiagnosePe(pe, name == "mismatch" ? Architecture::X64 : Architecture::X86);
    if (!diagnosis) throw std::runtime_error("fixture diagnosis");
    report.data = diagnosis.value();
    auto& doctor = std::get<DoctorReport>(report.data);
    if (name == "normal" || name == "mismatch") return report;
    if (name == "missing" || name == "api-set" || name == "candidate-error") {
        doctor.dependencies = DependencyReport{{L"C:\\fixtures\\plugins"}, {}};
        auto& libraries = doctor.dependencies->libraries;
        if (name == "missing") {
            libraries.push_back({"missing.dll", true, DependencyStatus::NotFound,
                {{0, CandidateStatus::NotFound, {}}}});
            doctor.findings.push_back({"POSSIBLE_MISSING_DEPENDENCY", FindingSeverity::Warning,
                "No candidate in explicit directories.", {{"filesystem_observation", "One explicit directory checked."}},
                "low", "Collect evidence from the target environment.", {"https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order"}});
        } else if (name == "api-set") {
            libraries.push_back({"api-ms-win-core-file-l1-1-0.dll", false, DependencyStatus::ApiSet, {}});
        } else {
            libraries.push_back({"locked.dll", false, DependencyStatus::Inconclusive,
                {{0, CandidateStatus::Unavailable, Error{ErrorCategory::AccessDenied, "GetFileAttributesW", "Access denied.", 5, {}}}}});
        }
    } else if (name == "malformed") {
        auto failed = DiagnosePe(Error{ErrorCategory::MalformedPe, "pe.headers", "Truncated PE header.", {}, {}});
        report.data = failed.value();
    } else if (name == "denied" || name == "timeout") {
        report.data = std::monostate{};
        report.errors.push_back(name == "denied"
            ? Error{ErrorCategory::AccessDenied, "CreateFileW", "Access denied.", 5, {}}
            : Error{ErrorCategory::Timeout, "pe.deadline", "Query deadline expired.", {}, {}});
    } else if (name == "process") {
        report.command = "process.list";
        report.path.reset();
        report.data = std::vector<ProcessSummary>{{7, L"中文.exe", {},
            {{ErrorCategory::AccessDenied, "OpenProcess", "Access denied.", 5, {}}}},
            {42, L"worker.exe", 134370000000000001ull, {}}};
    } else if (name == "modules") {
        report.command = "process.modules";
        report.path.reset();
        report.pid = 42;
        report.data = ModuleSnapshot{42, 134370000000000001ull, {Architecture::X64, "IsWow64Process2"},
            {{L"plugin?.dll", L"C:\\plugin?.dll", 0x7ff000000000ull, 8192, false, true, true}}};
    } else if (name == "pe") {
        report.command = "pe.inspect";
        pe.imports.push_back({"KERNEL32.dll", "original_first_thunk", 0, {{std::string("Sleep"), {}, 0}}});
        report.data = pe;
    } else if (name == "hostile") {
        report.path = L"C:\\fixtures\\` [x](javascript:evil) & <tag> 🚀.dll";
        const std::string payload = "```\n</pre><script>alert(1)</script><img src=https://invalid.test/x>\n"
            "``````\n# FORGED HEADING\n[x](javascript:evil) ![img](https://invalid.test/x)\n"
            "&lt;b&gt; | \\\" ' \t\r\n" + std::string("\0\x1b\x7f", 3);
        doctor.findings.front().message = payload;
        doctor.findings.front().confidence = payload;
        doctor.findings.front().recommended_next_step = payload;
        doctor.findings.front().evidence.push_back({"user_supplied_log", payload});
        doctor.findings.front().documentation = {payload};
        doctor.dependencies = DependencyReport{{L"C:\\fixtures\\` <img src=x>"},
            {{payload, false, DependencyStatus::UnsupportedName, {}}}};
    } else throw std::runtime_error("unknown fixture");
    return report;
}
}

int main(int argc, char** argv) {
    try {
        if (argc != 3) throw std::runtime_error("expected case and format");
        auto report = Sample(argv[1]);
        const std::string format = argv[2];
        const auto result = format == "json" ? cli::Render(report, true)
            : cli::RenderDocument(report, format == "html" ? cli::DocumentFormat::Html : cli::DocumentFormat::Markdown);
        if (!result) throw std::runtime_error(result.error().message);
        std::cout << result.value();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
