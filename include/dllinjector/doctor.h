#pragma once

#include "pe.h"
#include "process.h"

namespace dllinjector {
enum class FindingSeverity { Info, Warning, Error };
struct FindingEvidence {
    std::string source, description;
};
struct DoctorFinding {
    std::string rule_id;
    FindingSeverity severity;
    std::string message;
    std::vector<FindingEvidence> evidence;
    // Confidence in the stated finding, never a probability of successful loading.
    std::string confidence;
    std::string recommended_next_step;
    std::vector<std::string> documentation;
};
struct DoctorImage {
    std::uint16_t machine, characteristics;
    bool pe32_plus, has_clr_header;
    std::size_t import_library_count, delay_import_library_count;
};
struct DoctorReport {
    std::optional<Architecture> target_architecture;
    std::optional<DoctorImage> image;
    std::optional<Error> inspection_error;
    std::vector<DoctorFinding> findings;
};

// Pure rules over an existing static inspection. Only explicit x86/x64 targets
// are supported. MalformedPe becomes a finding; operational errors propagate.
Result<DoctorReport> DiagnosePe(const Result<PeInfo>& inspection,
                              std::optional<Architecture> target_architecture = {});
bool HasBlockingFindings(const DoctorReport& report);
const char* SeverityName(FindingSeverity severity);
}
