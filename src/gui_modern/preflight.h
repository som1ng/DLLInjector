#pragma once
#include <string>
#include <vector>
#include <optional>
#include "dllinjector/doctor.h"
#include "utils.h"

namespace gui {

enum class ArchMatchStatus {
    TargetNotSelected,
    TargetUnknown,
    Match,
    Mismatch
};

struct PreflightResult {
    bool valid = false;
    std::wstring filePath;
    std::string errorMessage;
    
    // Architectures
    utils::ProcessArchitecture dllArch = utils::ProcessArchitecture::Unknown;
    std::string dllArchStr = "Unknown";
    std::optional<utils::ProcessArchitecture> targetArch;
    ArchMatchStatus matchStatus = ArchMatchStatus::TargetNotSelected;

    // PE info
    uint16_t machine = 0;
    bool isPe32Plus = false;
    size_t importCount = 0;
    size_t delayImportCount = 0;

    // Findings and dependencies
    std::vector<dllinjector::DoctorFinding> findings;
    std::vector<dllinjector::DependencyCheck> dependencies;
    bool hasBlockingErrors = false;

    // Reports
    std::string jsonReport;
    std::string textReport;
};

class PreflightDoctor {
public:
    static PreflightResult AnalyzeDll(const std::wstring& dllPath,
                                      std::optional<utils::ProcessArchitecture> targetArch);

    static std::string GenerateJsonReport(const PreflightResult& res, DWORD targetPid);
    static std::string GenerateTextReport(const PreflightResult& res, DWORD targetPid);
};

} // namespace gui
