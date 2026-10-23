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
inline constexpr std::size_t MaxDependencySearchDirectories = 16;
enum class CandidateStatus { File, NotFound, NotFile, Unavailable };
enum class DependencyStatus { CandidateFound, NotFound, Inconclusive, ApiSet, UnsupportedName };
struct DependencyCandidate {
    std::size_t directory_index;
    CandidateStatus status;
    std::optional<Error> error;
};
struct DependencyCheck {
    std::string name;
    bool delay_import;
    DependencyStatus status;
    std::vector<DependencyCandidate> candidates;
};
struct DependencyReport {
    // Absolute lexical paths, in caller-supplied order; not loader search order.
    std::vector<std::wstring> search_directories;
    std::vector<DependencyCheck> libraries;
};
struct DoctorReport {
    std::optional<Architecture> target_architecture;
    std::optional<DoctorImage> image;
    std::optional<Error> inspection_error;
    std::vector<DoctorFinding> findings;
    std::optional<DependencyReport> dependencies;
};

// Pure rules over an existing static inspection. Only explicit x86/x64 targets
// are supported. MalformedPe becomes a finding; operational errors propagate.
Result<DoctorReport> DiagnosePe(const Result<PeInfo>& inspection,
                              std::optional<Architecture> target_architecture = {});
bool HasBlockingFindings(const DoctorReport& report);
const char* SeverityName(FindingSeverity severity);
const char* CandidateStatusName(CandidateStatus status);
const char* DependencyStatusName(DependencyStatus status);
// Reads the input once. Optional dependency checks observe file attributes only,
// in explicitly supplied directories and the caller's Windows filesystem view.
Result<DoctorReport> InspectDoctorFile(const std::wstring& path,
    std::optional<Architecture> target_architecture = {},
    const std::vector<std::wstring>& search_directories = {}, std::uint32_t timeout_ms = 5000);
}
