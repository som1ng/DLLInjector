#pragma once

#include "dllinjector/doctor.h"

namespace dllinjector::cli {
inline constexpr std::size_t MaxSavedReportBytes = 16 * 1024 * 1024;
// Presentation data from a validated supplied snapshot, never a new observation.
struct SavedReport {
    std::string schema_version, tool_version, command, status;
    std::string target_pid, target_path, duration_ms;
    bool accepted = false, completed = false, verified = false;
    std::vector<DoctorFinding> findings;
    std::vector<FindingEvidence> evidence;
    std::vector<Error> errors;
    std::string data_json, original_json;
};
Result<SavedReport> ParseSavedReport(const std::string& bytes, std::uint32_t timeout_ms = 5000);
Result<SavedReport> ReadSavedReport(const std::wstring& path, std::uint32_t timeout_ms = 5000);
}
