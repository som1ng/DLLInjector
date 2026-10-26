#include "preflight.h"
#include "dllinjector/text.h"
#include <windows.h>
#include <sstream>
#include <iomanip>
#include <filesystem>

namespace gui {

namespace {
std::string ToUtf8String(const std::wstring& wstr) {
    auto res = dllinjector::ToUtf8(wstr);
    return res ? res.value() : "";
}

std::string EscapeJson(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\b') out += "\\b";
        else if (c == '\f') out += "\\f";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

std::optional<dllinjector::Architecture> MapArch(std::optional<utils::ProcessArchitecture> arch) {
    if (!arch) return std::nullopt;
    switch (*arch) {
        case utils::ProcessArchitecture::X86:   return dllinjector::Architecture::X86;
        case utils::ProcessArchitecture::X64:   return dllinjector::Architecture::X64;
        case utils::ProcessArchitecture::Arm64: return dllinjector::Architecture::Arm64;
        default: return std::nullopt;
    }
}
} // namespace

PreflightResult PreflightDoctor::AnalyzeDll(const std::wstring& dllPath,
                                           std::optional<utils::ProcessArchitecture> targetArch) {
    PreflightResult res;
    res.filePath = dllPath;
    res.targetArch = targetArch;

    if (dllPath.empty()) {
        res.errorMessage = "未选择 DLL 文件";
        return res;
    }

    DWORD attr = GetFileAttributesW(dllPath.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        res.errorMessage = "DLL 文件不存在或路径无效";
        return res;
    }

    auto coreTargetArch = MapArch(targetArch);
    std::error_code pathError;
    const auto directory = std::filesystem::absolute(dllPath, pathError).parent_path();
    if (pathError) {
        res.errorMessage = "无法确定 DLL 所在目录";
        return res;
    }
    auto docResult = dllinjector::InspectDoctorFile(dllPath, coreTargetArch, {directory.wstring()}, 5000);

    if (!docResult) {
        res.valid = false;
        res.errorMessage = docResult.error().message;
        return res;
    }

    res.valid = true;
    const auto& doc = docResult.value();

    if (doc.image) {
        res.machine = doc.image->machine;
        res.isPe32Plus = doc.image->pe32_plus;
        res.importCount = doc.image->import_library_count;
        res.delayImportCount = doc.image->delay_import_library_count;

        switch (doc.image->machine) {
            case 0x8664: // IMAGE_FILE_MACHINE_AMD64
                res.dllArch = utils::ProcessArchitecture::X64;
                res.dllArchStr = "x64 (AMD64)";
                break;
            case 0x014c: // IMAGE_FILE_MACHINE_I386
                res.dllArch = utils::ProcessArchitecture::X86;
                res.dllArchStr = "x86 (I386)";
                break;
            case 0xaa64: // IMAGE_FILE_MACHINE_ARM64
                res.dllArch = utils::ProcessArchitecture::Arm64;
                res.dllArchStr = "ARM64";
                break;
            default:
                res.dllArch = utils::ProcessArchitecture::Unknown;
                res.dllArchStr = "Unknown (0x" + std::to_string(doc.image->machine) + ")";
                break;
        }
    }

    // Match status calculation
    if (!targetArch.has_value()) {
        res.matchStatus = ArchMatchStatus::TargetNotSelected;
    } else if (*targetArch == utils::ProcessArchitecture::Unknown) {
        res.matchStatus = ArchMatchStatus::TargetUnknown;
    } else if (*targetArch == res.dllArch) {
        res.matchStatus = ArchMatchStatus::Match;
    } else {
        res.matchStatus = ArchMatchStatus::Mismatch;
    }

    res.findings = doc.findings;
    if (doc.dependencies) {
        res.dependencies = doc.dependencies->libraries;
    }
    res.hasBlockingErrors = dllinjector::HasBlockingFindings(doc);

    res.jsonReport = GenerateJsonReport(res, 0);
    res.textReport = GenerateTextReport(res, 0);

    return res;
}

std::string PreflightDoctor::GenerateJsonReport(const PreflightResult& res, DWORD targetPid) {
    std::ostringstream json;
    json << "{\n";
    json << "  \"tool\": \"DLLInjector Modern GUI\",\n";
    json << "  \"schema_version\": \"1.0.0\",\n";
    json << "  \"dll_path\": \"" << EscapeJson(ToUtf8String(res.filePath)) << "\",\n";
    json << "  \"valid\": " << (res.valid ? "true" : "false") << ",\n";
    json << "  \"dll_architecture\": \"" << res.dllArchStr << "\",\n";
    json << "  \"pe32_plus\": " << (res.isPe32Plus ? "true" : "false") << ",\n";
    json << "  \"import_libraries_count\": " << res.importCount << ",\n";
    json << "  \"delay_imports_count\": " << res.delayImportCount << ",\n";
    json << "  \"target_pid\": " << targetPid << ",\n";
    std::string targetArchStr = "None";
    if (res.targetArch) {
        auto mappedTarget = MapArch(res.targetArch);
        targetArchStr = mappedTarget ? std::string(dllinjector::ArchitectureName(*mappedTarget)) : "Unknown";
    }
    json << "  \"target_architecture\": \"" << targetArchStr << "\",\n";
    
    std::string matchStr = "UNKNOWN";
    if (res.matchStatus == ArchMatchStatus::Match) matchStr = "MATCH";
    else if (res.matchStatus == ArchMatchStatus::Mismatch) matchStr = "MISMATCH";
    else if (res.matchStatus == ArchMatchStatus::TargetNotSelected) matchStr = "TARGET_NOT_SELECTED";
    json << "  \"compatibility_status\": \"" << matchStr << "\",\n";
    json << "  \"blocking_findings\": " << (res.hasBlockingErrors ? "true" : "false") << ",\n";

    // Findings array
    json << "  \"findings\": [\n";
    for (size_t i = 0; i < res.findings.size(); ++i) {
        const auto& f = res.findings[i];
        json << "    {\n";
        json << "      \"rule_id\": \"" << EscapeJson(f.rule_id) << "\",\n";
        json << "      \"severity\": \"" << dllinjector::SeverityName(f.severity) << "\",\n";
        json << "      \"message\": \"" << EscapeJson(f.message) << "\",\n";
        json << "      \"confidence\": \"" << EscapeJson(f.confidence) << "\",\n";
        json << "      \"recommended_next_step\": \"" << EscapeJson(f.recommended_next_step) << "\"\n";
        json << "    }" << (i + 1 < res.findings.size() ? "," : "") << "\n";
    }
    json << "  ],\n";

    // Dependencies array
    json << "  \"dependencies\": [\n";
    for (size_t i = 0; i < res.dependencies.size(); ++i) {
        const auto& d = res.dependencies[i];
        json << "    {\n";
        json << "      \"name\": \"" << EscapeJson(d.name) << "\",\n";
        json << "      \"delay_import\": " << (d.delay_import ? "true" : "false") << ",\n";
        json << "      \"status\": \"" << dllinjector::DependencyStatusName(d.status) << "\"\n";
        json << "    }" << (i + 1 < res.dependencies.size() ? "," : "") << "\n";
    }
    json << "  ]\n";
    json << "}\n";

    return json.str();
}

std::string PreflightDoctor::GenerateTextReport(const PreflightResult& res, DWORD targetPid) {
    std::ostringstream txt;
    txt << "============================================================\r\n";
    txt << "         DLL INJECTOR - PREFLIGHT DOCTOR DIAGNOSIS          \r\n";
    txt << "============================================================\r\n";
    txt << "File: " << ToUtf8String(res.filePath) << "\r\n";
    txt << "DLL Architecture: " << res.dllArchStr << " (" << (res.isPe32Plus ? "PE32+" : "PE32") << ")\r\n";
    txt << "Imported Libraries: " << res.importCount << " | Delay Imports: " << res.delayImportCount << "\r\n";
    txt << "Target Process PID: " << targetPid << "\r\n";
    std::string textTargetArch = "Not Selected";
    if (res.targetArch) {
        auto mappedTarget = MapArch(res.targetArch);
        textTargetArch = mappedTarget ? std::string(dllinjector::ArchitectureName(*mappedTarget)) : "Unknown";
    }
    txt << "Target Architecture: " << textTargetArch << "\r\n";
    
    txt << "Architecture Compatibility: ";
    if (res.matchStatus == ArchMatchStatus::Match) txt << "[PASS] Architecture match confirmed.\r\n";
    else if (res.matchStatus == ArchMatchStatus::Mismatch) txt << "[FAIL] Architecture mismatch!\r\n";
    else txt << "[WAIT] Target not yet selected.\r\n";

    txt << "\r\n--- DOCTOR FINDINGS (" << res.findings.size() << ") ---\r\n";
    for (const auto& f : res.findings) {
        txt << "[" << dllinjector::SeverityName(f.severity) << "] " << f.rule_id << ": " << f.message << "\r\n";
        if (!f.recommended_next_step.empty()) {
            txt << "  -> Next step: " << f.recommended_next_step << "\r\n";
        }
    }

    txt << "\r\n--- DEPENDENCIES (" << res.dependencies.size() << ") ---\r\n";
    for (const auto& d : res.dependencies) {
        txt << "  " << std::left << std::setw(28) << d.name 
            << " [" << dllinjector::DependencyStatusName(d.status) << "]"
            << (d.delay_import ? " (Delay-loaded)" : "") << "\r\n";
    }
    txt << "============================================================\r\n";
    return txt.str();
}

} // namespace gui
