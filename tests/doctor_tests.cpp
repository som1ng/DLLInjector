#include "dllinjector/doctor.h"
#include <cstdlib>
#include <iostream>

using namespace dllinjector;
namespace {
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
PeInfo Image(bool plus = false) {
    PeInfo p{};
    p.machine = plus ? 0x8664 : 0x14c;
    p.pe32_plus = plus;
    p.characteristics = 0x2002;
    return p;
}
const DoctorFinding* Find(const DoctorReport& r, const char* rule) {
    for (const auto& f : r.findings) if (f.rule_id == rule) return &f;
    return nullptr;
}
void ArchitectureRules() {
    for (const bool plus : {false, true}) {
        const auto match = DiagnosePe(Image(plus), plus ? Architecture::X64 : Architecture::X86);
        Check(match && !HasBlockingFindings(match.value()) && !Find(match.value(), "ARCH_MISMATCH"), "matching native architecture");
        const auto mismatch = DiagnosePe(Image(plus), plus ? Architecture::X86 : Architecture::X64);
        Check(mismatch && HasBlockingFindings(mismatch.value()), "both mismatch directions block");
        const auto* f = Find(mismatch.value(), "ARCH_MISMATCH");
        Check(f && f->severity == FindingSeverity::Error && f->evidence.size() == 2, "mismatch has two evidence sources");
        Check(f->evidence[0].source == "static_analysis" && f->evidence[1].source == "user_supplied_option", "target is a declaration");
    }
    for (const auto arch : {Architecture::Unknown, Architecture::Arm64}) {
        const auto r = DiagnosePe(Image(), arch);
        Check(!r && r.error().category == ErrorCategory::InvalidArgument, "unsupported target is rejected");
    }
}
void EvidenceGaps() {
    const auto noTarget = DiagnosePe(Image());
    Check(noTarget && !noTarget.value().target_architecture && !HasBlockingFindings(noTarget.value()), "no host inference");
    Check(!Find(noTarget.value(), "ARCH_MISMATCH") && Find(noTarget.value(), "INSUFFICIENT_EVIDENCE"), "missing target explicit");
    for (const auto machine : {0xaa64, 0xa641, 0xa64e, 0xffff}) {
        auto p = Image(true); p.machine = static_cast<std::uint16_t>(machine);
        const auto r = DiagnosePe(p, Architecture::X86);
        Check(r && !HasBlockingFindings(r.value()) && !Find(r.value(), "ARCH_MISMATCH"), "special architecture cannot be guessed");
    }
    auto p = Image(); p.pe32_plus = true;
    auto r = DiagnosePe(p, Architecture::X64);
    Check(r && !HasBlockingFindings(r.value()) && !Find(r.value(), "ARCH_MISMATCH"), "inconsistent format is outside compatibility rule");
    p = Image(); p.directories.push_back({14, 4096, 72, false, 512});
    r = DiagnosePe(p, Architecture::X64);
    Check(r && r.value().image->has_clr_header && !HasBlockingFindings(r.value()), "CLR requires separate evidence");
    Check(!Find(r.value(), "ARCH_MISMATCH"), "I386 alone does not establish AnyCPU runtime architecture");
    p.directories[0].address = 0; p.directories[0].size_bytes = 0;
    r = DiagnosePe(p, Architecture::X64);
    Check(r && !r.value().image->has_clr_header && Find(r.value(), "ARCH_MISMATCH"), "empty CLR directory is not managed metadata");
}
void FileRules() {
    auto p = Image(); p.characteristics = 2;
    auto r = DiagnosePe(p, Architecture::X86);
    Check(r && HasBlockingFindings(r.value()) && Find(r.value(), "NOT_A_DLL"), "EXE is not a DLL regardless of extension");
    r = DiagnosePe(Image(), Architecture::X86);
    Check(r && !Find(r.value(), "NOT_A_DLL"), "DLL flag accepted");
    const Error error{ErrorCategory::MalformedPe, "pe.imports", "Truncated descriptor.", {}, {}};
    r = DiagnosePe(error, Architecture::X64);
    Check(r && !r.value().image && r.value().inspection_error && HasBlockingFindings(r.value()), "malformed is completed diagnosis");
    Check(r.value().inspection_error->stage == error.stage && !r.value().inspection_error->win32, "preserve parser error");
    Check(r.value().findings.size() == 1 && Find(r.value(), "MALFORMED_PE"), "no conclusions from unavailable metadata");
}
void OperationalErrors() {
    for (const auto category : {ErrorCategory::AccessDenied, ErrorCategory::NotFound, ErrorCategory::Unsupported,
            ErrorCategory::Timeout, ErrorCategory::Io, ErrorCategory::System, ErrorCategory::InvalidArgument}) {
        const Error error{category, "fixture", "Operation failed.", 5, {}, false};
        const auto r = DiagnosePe(error);
        Check(!r && r.error().category == category && r.error().stage == "fixture" && r.error().win32 == 5 && !r.error().verified,
              "operational errors remain unchanged");
    }
}
void EvidenceContract() {
    auto p = Image();
    p.imports.push_back({"api-ms-win-fixture.dll", "import_lookup_table", 0, {}});
    p.delay_imports.push_back({"absent-fixture.dll", "delay_import_name_table", 0, {}});
    const auto r = DiagnosePe(p, Architecture::X86);
    Check(r && r.value().image->import_library_count == 1 && r.value().image->delay_import_library_count == 1, "counts preserved");
    Check(!Find(r.value(), "POSSIBLE_MISSING_DEPENDENCY") && !HasBlockingFindings(r.value()), "no invented dependency failures");
    Check(Find(r.value(), "INSUFFICIENT_EVIDENCE"), "runtime and dependency gap remains");
    for (const auto& f : r.value().findings) {
        Check(!f.message.empty() && !f.evidence.empty() && f.confidence == "high", "finding explains evidence confidence");
        Check(!f.recommended_next_step.empty() && !f.documentation.empty(), "action and official documentation");
        for (const auto& url : f.documentation)
            Check(url.rfind("https://learn.microsoft.com/", 0) == 0, "official links");
    }
    Check(std::string(SeverityName(FindingSeverity::Error)) == "error" &&
          std::string(SeverityName(FindingSeverity::Warning)) == "warning" &&
          std::string(SeverityName(FindingSeverity::Info)) == "info", "stable severity names");
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "architectures") ArchitectureRules();
    else if (name == "evidence-gaps") EvidenceGaps();
    else if (name == "file-rules") FileRules();
    else if (name == "errors") OperationalErrors();
    else if (name == "evidence-contract") EvidenceContract();
    else return 2;
    return 0;
}
