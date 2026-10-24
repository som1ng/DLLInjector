#include "dllinjector/doctor.h"
#include <algorithm>
#include <sstream>

namespace dllinjector {
namespace {
constexpr char PeDocs[] = "https://learn.microsoft.com/en-us/windows/win32/debug/pe-format";
constexpr char ArchDocs[] = "https://learn.microsoft.com/en-us/windows/win32/winprog64/process-interoperability";
constexpr char SearchDocs[] = "https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order";
constexpr char ClrDocs[] = "https://learn.microsoft.com/en-us/dotnet/framework/tools/corflags-exe-corflags-conversion-tool";
std::string Hex(std::uint16_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}
DoctorFinding Finding(const char* rule, FindingSeverity severity, const char* message,
                      std::string evidence, const char* next, const char* documentation) {
    return {rule, severity, message, {{"static_analysis", std::move(evidence)}}, "high", next, {documentation}};
}
}

const char* SeverityName(FindingSeverity severity) {
    switch (severity) {
    case FindingSeverity::Error: return "error";
    case FindingSeverity::Warning: return "warning";
    case FindingSeverity::Info: return "info";
    }
    return "info";
}
bool HasBlockingFindings(const DoctorReport& report) {
    return std::any_of(report.findings.begin(), report.findings.end(), [](const DoctorFinding& f) {
        return f.severity == FindingSeverity::Error;
    });
}
Result<DoctorReport> DiagnosePe(const Result<PeInfo>& inspection, std::optional<Architecture> target) {
    if (target && *target != Architecture::X86 && *target != Architecture::X64)
        return Error{ErrorCategory::InvalidArgument, "doctor.target", "Only explicit x86/x64 target architectures are supported.", {}, {}};
    DoctorReport report;
    report.target_architecture = target;
    if (!inspection) {
        const auto& error = inspection.error();
        if (error.category != ErrorCategory::MalformedPe) return error;
        report.inspection_error = error;
        report.findings.push_back(Finding("MALFORMED_PE", FindingSeverity::Error,
            "The file failed the supported PE structural checks.", error.stage + ": " + error.message,
            "Obtain or rebuild the intended DLL and inspect the reported parser stage. Loading was not attempted.", PeDocs));
        return report;
    }
    const auto& pe = inspection.value();
    const bool clr = std::any_of(pe.directories.begin(), pe.directories.end(), [](const PeDirectory& directory) {
        return directory.index == 14 && (directory.address != 0 || directory.size_bytes != 0);
    });
    report.image = DoctorImage{pe.machine, pe.characteristics, pe.pe32_plus, clr, pe.imports.size(), pe.delay_imports.size()};
    if ((pe.characteristics & 0x2000) == 0)
        report.findings.push_back(Finding("NOT_A_DLL", FindingSeverity::Error,
            "The PE header does not identify this image as a DLL.",
            "COFF Characteristics=" + Hex(pe.characteristics) + "; IMAGE_FILE_DLL (0x2000) is not set.",
            "Provide the DLL build artifact. Renaming an EXE to .dll does not change its PE header.", PeDocs));

    const auto architecture = pe.machine == 0x14c && !pe.pe32_plus ? Architecture::X86 :
        pe.machine == 0x8664 && pe.pe32_plus ? Architecture::X64 : Architecture::Unknown;
    const std::string header = "Machine=" + Hex(pe.machine) + ", format=" + (pe.pe32_plus ? "PE32+" : "PE32") + ".";
    if (clr) {
        report.findings.push_back(Finding("INSUFFICIENT_EVIDENCE", FindingSeverity::Warning,
            "Managed or mixed-mode compatibility was not evaluated.", header + " A nonempty CLR directory is declared; its contents and CorFlags were not inspected.",
            "Inspect CLR metadata and CorFlags for the intended runtime; Machine alone does not establish AnyCPU compatibility.", ClrDocs));
    } else if (architecture == Architecture::Unknown) {
        report.findings.push_back(Finding("INSUFFICIENT_EVIDENCE", FindingSeverity::Warning,
            "The Machine/format combination is outside the native x86/x64 compatibility rule.", header,
            "Check the image's platform-specific requirements and target runtime; do not infer compatibility from CLI or OS bitness.", PeDocs));
    } else if (target && architecture != *target) {
        auto finding = Finding("ARCH_MISMATCH", FindingSeverity::Error,
            "The native image architecture differs from the declared target process architecture.", header,
            "Use a DLL build matching the intended process architecture and confirm the actual process architecture.", ArchDocs);
        finding.evidence.push_back({"user_supplied_option", std::string("--target-arch ") + ArchitectureName(*target) + "; not a process observation."});
        report.findings.push_back(std::move(finding));
    }
    if (!target) {
        auto finding = Finding("INSUFFICIENT_EVIDENCE", FindingSeverity::Warning,
            "Target process architecture was not supplied.", "No --target-arch option was supplied; no process was queried.",
            "Confirm the intended process architecture and repeat with --target-arch x86 or --target-arch x64.", ArchDocs);
        finding.evidence[0].source = "user_supplied_option";
        report.findings.push_back(std::move(finding));
    }
    report.findings.push_back(Finding("INSUFFICIENT_EVIDENCE", FindingSeverity::Info,
        "Dependency availability and runtime loading remain unverified.",
        "Static tables contain " + std::to_string(pe.imports.size()) + " import libraries and " +
        std::to_string(pe.delay_imports.size()) + " delay-import libraries. No DLL loading or initialization was performed.",
        "Inspect the intended application's loader errors and search configuration, including API sets, Known DLLs, SxS and dynamic loads. Empty import tables do not prove independence.", SearchDocs));
    return report;
}
}
