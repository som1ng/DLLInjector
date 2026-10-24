#include "dependencies_detail.h"
#include "common/win32.h"
#include <new>

namespace dllinjector {
namespace {
constexpr char SearchDocs[] = "https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order";
Error Failure(ErrorCategory category, const char* stage, const char* message) {
    return {category, stage, message, {}, {}};
}
Error FileError(DWORD code, const char* stage) {
    auto error = detail::Win32Error(code, stage);
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) {
        error.category = ErrorCategory::NotFound;
        error.message = "The specified filesystem entry was not found.";
    } else if (error.category == ErrorCategory::System) error.category = ErrorCategory::Io;
    return error;
}
Error TimeoutError() {
    return Failure(ErrorCategory::Timeout, "doctor.dependencies.timeout", "Doctor dependency inspection deadline expired.");
}
class Deadline {
public:
    Deadline(std::uint32_t ms, const detail::DependencyApi& api) : api_(api), start_(api.tick()), ms_(ms) {}
    void Check() const { if (api_.tick() - start_ >= ms_) throw TimeoutError(); }
private:
    const detail::DependencyApi& api_;
    ULONGLONG start_;
    std::uint32_t ms_;
};
std::string Lower(std::string text) {
    for (auto& ch : text) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
    return text;
}
bool PlainFileName(const std::string& name) {
    if (name.empty() || name.size() > 255 || name.back() == '.') return false;
    for (const auto ch : name)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              ch == '_' || ch == '.' || ch == '+' || ch == '-')) return false;
    const auto dot = name.find('.');
    if (dot == std::string::npos || dot == 0) return false;
    const auto stem = Lower(name.substr(0, dot));
    if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul") return false;
    if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) &&
        stem[3] >= '1' && stem[3] <= '9') return false;
    return true;
}
std::wstring Directory(const std::wstring& path, const detail::DependencyApi& api, const Deadline& deadline) {
    deadline.Check();
    if (path.empty() || path.find(L'\0') != std::wstring::npos)
        throw Failure(ErrorCategory::InvalidArgument, "doctor.dependencies.directory", "A nonempty directory path without embedded NUL is required.");
    if (path.size() > 32767)
        throw Failure(ErrorCategory::Unsupported, "doctor.dependencies.directory", "The directory exceeds the supported Windows path length.");
    const DWORD needed = api.fullPath(path.c_str(), 0, nullptr, nullptr);
    const auto firstError = needed ? ERROR_SUCCESS : GetLastError();
    deadline.Check();
    if (!needed) throw FileError(firstError, "doctor.dependencies.directory");
    if (needed > 32768)
        throw Failure(ErrorCategory::Unsupported, "doctor.dependencies.directory", "The absolute directory exceeds the supported Windows path length.");
    std::wstring full(needed, L'\0');
    const DWORD length = api.fullPath(path.c_str(), needed, full.data(), nullptr);
    const auto secondError = length ? ERROR_SUCCESS : GetLastError();
    deadline.Check();
    if (!length) throw FileError(secondError, "doctor.dependencies.directory");
    if (length >= needed)
        throw Failure(ErrorCategory::Io, "doctor.dependencies.directory", "The path changed during normalization; retry with an absolute path.");
    full.resize(length);
    if (full.size() < 3 || full[1] != L':' || full[2] != L'\\')
        throw Failure(ErrorCategory::Unsupported, "doctor.dependencies.directory", "Use local drive directories; UNC and device paths are unsupported.");
    const auto drive = api.driveType(full.substr(0, 3).c_str());
    deadline.Check();
    if (drive == DRIVE_REMOTE)
        throw Failure(ErrorCategory::Unsupported, "doctor.dependencies.directory", "Remote mapped drive directories are unsupported.");
    while (full.size() > 3 && full.back() == L'\\') full.pop_back();
    const auto attributes = api.attributes(full.c_str());
    const auto code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
    deadline.Check();
    if (attributes == INVALID_FILE_ATTRIBUTES) throw FileError(code, "doctor.dependencies.directory");
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
        throw Failure(ErrorCategory::InvalidArgument, "doctor.dependencies.directory", "Each search path must be a directory.");
    return full;
}
void AppendFindings(DoctorReport& report) {
    std::size_t missing = 0, delayed = 0, skipped = 0, unavailable = 0;
    for (const auto& library : report.dependencies->libraries) {
        if (library.status == DependencyStatus::NotFound) (library.delay_import ? delayed : missing)++;
        if (library.status == DependencyStatus::ApiSet || library.status == DependencyStatus::UnsupportedName) ++skipped;
        for (const auto& candidate : library.candidates)
            if (candidate.status == CandidateStatus::Unavailable) ++unavailable;
    }
    if (missing || delayed)
        report.findings.push_back({"POSSIBLE_MISSING_DEPENDENCY", FindingSeverity::Warning,
            "Some direct dependencies have no regular file candidate in the supplied directories.",
            {{"filesystem_observation", std::to_string(missing) + " import libraries and " + std::to_string(delayed) +
                " delay-import libraries were not found as regular candidates; see dependencies.libraries for each directory observation."},
             {"user_supplied_option", "Only the explicit --search-dir directories were checked; this is not the target process's complete search environment."}},
            "low", "Check the application's deployment and actual loader search configuration. Known DLLs, SxS and other sources may satisfy these names; delay imports may never be used.", {SearchDocs}});
    if (skipped || unavailable)
        report.findings.push_back({"INSUFFICIENT_EVIDENCE", FindingSeverity::Warning,
            "Some dependency names or candidate paths could not be evaluated as ordinary files.",
            {{"static_analysis", std::to_string(skipped) + " API-set or unsupported names were skipped; inspect dependencies.libraries."},
             {"filesystem_observation", std::to_string(unavailable) + " candidate attribute observations were unavailable; they were not treated as absent."}},
            "high", "Inspect API-set mapping, unsupported names and the reported filesystem errors in the intended application's environment. Candidate presence does not establish loadability.", {SearchDocs}});
}
}

const char* CandidateStatusName(CandidateStatus status) {
    switch (status) {
    case CandidateStatus::File: return "file";
    case CandidateStatus::NotFound: return "not_found";
    case CandidateStatus::NotFile: return "not_file";
    case CandidateStatus::Unavailable: return "unavailable";
    }
    return "unavailable";
}
const char* DependencyStatusName(DependencyStatus status) {
    switch (status) {
    case DependencyStatus::CandidateFound: return "candidate_found";
    case DependencyStatus::NotFound: return "not_found_in_search_dirs";
    case DependencyStatus::Inconclusive: return "inconclusive";
    case DependencyStatus::ApiSet: return "api_set_unresolved";
    case DependencyStatus::UnsupportedName: return "unsupported_name";
    }
    return "inconclusive";
}

namespace detail {
Result<DependencyReport> CheckDependencyCandidates(const PeInfo& pe, const std::vector<std::wstring>& directories,
                                                   std::uint32_t timeout_ms, const DependencyApi& api) {
    try {
        const Deadline deadline(timeout_ms, api);
        deadline.Check();
        if (directories.empty() || directories.size() > MaxDependencySearchDirectories)
            return Failure(ErrorCategory::InvalidArgument, "doctor.dependencies.options", "Supply between 1 and 16 search directories.");
        if (pe.imports.size() > 1024 || pe.delay_imports.size() > 1024 - pe.imports.size())
            return Failure(ErrorCategory::Unsupported, "doctor.dependencies.limit", "Dependency inspection supports at most 1024 library declarations.");
        DependencyReport report;
        for (const auto& path : directories) report.search_directories.push_back(Directory(path, api, deadline));
        for (const auto delay : {false, true}) {
            for (const auto& library : (delay ? pe.delay_imports : pe.imports)) {
                deadline.Check();
                DependencyCheck row{library.name, delay, DependencyStatus::UnsupportedName, {}};
                if (PlainFileName(library.name)) {
                    const auto lower = Lower(library.name);
                    if (lower.rfind("api-", 0) == 0 || lower.rfind("ext-", 0) == 0) row.status = DependencyStatus::ApiSet;
                    else {
                        bool found = false, unknown = false;
                        const std::wstring name(library.name.begin(), library.name.end());
                        for (std::size_t i = 0; i < report.search_directories.size(); ++i) {
                            deadline.Check();
                            const auto& directory = report.search_directories[i];
                            const auto path = directory + (directory.back() == L'\\' ? L"" : L"\\") + name;
                            const DWORD attributes = api.attributes(path.c_str());
                            const DWORD code = attributes == INVALID_FILE_ATTRIBUTES ? GetLastError() : ERROR_SUCCESS;
                            deadline.Check();
                            DependencyCandidate candidate{i, CandidateStatus::Unavailable, {}};
                            if (attributes == INVALID_FILE_ATTRIBUTES) {
                                if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND) candidate.status = CandidateStatus::NotFound;
                                else candidate.error = FileError(code, "doctor.dependencies.attributes");
                            } else if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                                candidate.error = Failure(ErrorCategory::Unsupported, "doctor.dependencies.attributes", "Reparse-point candidate not followed or verified.");
                            } else candidate.status = (attributes & FILE_ATTRIBUTE_DIRECTORY) ? CandidateStatus::NotFile : CandidateStatus::File;
                            found = found || candidate.status == CandidateStatus::File;
                            unknown = unknown || candidate.status == CandidateStatus::Unavailable;
                            row.candidates.push_back(std::move(candidate));
                        }
                        row.status = found ? DependencyStatus::CandidateFound : unknown ? DependencyStatus::Inconclusive : DependencyStatus::NotFound;
                    }
                }
                report.libraries.push_back(std::move(row));
            }
        }
        deadline.Check();
        return report;
    } catch (const Error& error) { return error; }
    catch (const std::bad_alloc&) { return Failure(ErrorCategory::System, "doctor.dependencies.memory", "Not enough memory for dependency observations."); }
}
Result<DoctorReport> InspectDoctorFile(const std::wstring& path, std::optional<Architecture> target,
    const std::vector<std::wstring>& directories, std::uint32_t timeout_ms, const DependencyApi& api) {
    const auto start = api.tick();
    const auto inspection = api.inspectPe(path, timeout_ms);
    auto report = DiagnosePe(inspection, target);
    if (!report || !report.value().image || directories.empty()) return report;
    const auto elapsed = api.tick() - start;
    if (elapsed >= timeout_ms) return TimeoutError();
    auto dependencies = detail::CheckDependencyCandidates(inspection.value(), directories,
        timeout_ms - static_cast<std::uint32_t>(elapsed), api);
    if (!dependencies) return dependencies.error();
    if (api.tick() - start >= timeout_ms) return TimeoutError();
    report.value().dependencies = std::move(dependencies.value());
    AppendFindings(report.value());
    if (api.tick() - start >= timeout_ms) return TimeoutError();
    return report;
}
}
Result<DoctorReport> InspectDoctorFile(const std::wstring& path, std::optional<Architecture> target,
    const std::vector<std::wstring>& directories, std::uint32_t timeout_ms) {
    return detail::InspectDoctorFile(path, target, directories, timeout_ms, detail::DependencyApi{});
}
}
