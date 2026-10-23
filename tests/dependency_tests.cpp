#include "doctor/dependencies_detail.h"
#include <cstdlib>
#include <iostream>
#include <map>

using namespace dllinjector;
namespace {
std::map<std::wstring, DWORD> attributes;
std::map<std::wstring, DWORD> failures;
std::vector<std::wstring> probes;
ULONGLONG now = 0, step = 0;
void Check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
DWORD WINAPI Attributes(LPCWSTR path) {
    probes.push_back(path);
    now += step;
    const auto error = failures.find(path);
    if (error != failures.end()) { SetLastError(error->second); return INVALID_FILE_ATTRIBUTES; }
    const auto found = attributes.find(path);
    if (found != attributes.end()) return found->second;
    SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES;
}
UINT WINAPI DriveType(LPCWSTR path) { return path[0] == L'Z' ? DRIVE_REMOTE : DRIVE_FIXED; }
ULONGLONG WINAPI Tick() { return now; }
detail::DependencyApi Api() {
    attributes = {{L"C:\\deps", FILE_ATTRIBUTE_DIRECTORY}, {L"D:\\more", FILE_ATTRIBUTE_DIRECTORY}};
    failures.clear(); probes.clear(); now = step = 0;
    detail::DependencyApi api;
    api.attributes = Attributes; api.driveType = DriveType; api.tick = Tick;
    return api;
}
PeInfo Image(std::initializer_list<const char*> names) {
    PeInfo p{};
    for (const auto* name : names) p.imports.push_back({name, "import_lookup_table", 0, {}});
    return p;
}
void Candidates() {
    auto api = Api();
    auto pe = Image({"present.dll", "missing.dll", "folder.dll"});
    pe.delay_imports.push_back({"present.dll", "delay_import_name_table", 0, {}});
    attributes[L"D:\\more\\present.dll"] = FILE_ATTRIBUTE_NORMAL;
    attributes[L"C:\\deps\\folder.dll"] = FILE_ATTRIBUTE_DIRECTORY;
    const auto r = detail::CheckDependencyCandidates(pe, {L"C:\\deps", L"D:\\more"}, 5000, api);
    Check(r && r.value().libraries.size() == 4 && r.value().search_directories.size() == 2, "preserve direct and delay declarations");
    const auto& rows = r.value().libraries;
    Check(rows[0].status == DependencyStatus::CandidateFound && rows[0].candidates[1].status == CandidateStatus::File, "second directory can contain candidate");
    Check(rows[1].status == DependencyStatus::NotFound && rows[2].status == DependencyStatus::NotFound, "absence and non-file do not supply candidates");
    Check(rows[2].candidates[0].status == CandidateStatus::NotFile && !rows[2].candidates[0].error, "directory is an observed non-file");
    Check(rows[3].delay_import && rows[3].name == rows[0].name, "delay import remains separately labelled");
    Check(probes.size() == 10 && rows[0].candidates[1].directory_index == 1, "inspect every explicit directory");
}
void Unknown() {
    auto api = Api();
    failures[L"C:\\deps\\denied.dll"] = ERROR_ACCESS_DENIED;
    attributes[L"C:\\deps\\link.dll"] = FILE_ATTRIBUTE_REPARSE_POINT;
    auto r = detail::CheckDependencyCandidates(Image({"denied.dll", "link.dll"}), {L"C:\\deps", L"D:\\more"}, 5000, api);
    Check(r && r.value().libraries[0].status == DependencyStatus::Inconclusive, "denied is not absence");
    const auto& error = r.value().libraries[0].candidates[0].error;
    Check(error && error->win32 == ERROR_ACCESS_DENIED && error->category == ErrorCategory::AccessDenied, "preserve real permission error");
    Check(r.value().libraries[1].status == DependencyStatus::Inconclusive && !r.value().libraries[1].candidates[0].error->win32,
          "reparse point is not a proven regular candidate");
    attributes[L"D:\\more\\denied.dll"] = FILE_ATTRIBUTE_NORMAL;
    r = detail::CheckDependencyCandidates(Image({"denied.dll"}), {L"C:\\deps", L"D:\\more"}, 5000, api);
    Check(r && r.value().libraries[0].status == DependencyStatus::CandidateFound && r.value().libraries[0].candidates[0].error,
          "found elsewhere does not erase denied evidence");
}
void Names() {
    const auto api = Api();
    auto pe = Image({"API-MS-WIN-CORE-FILE-L1-1-0.DLL", "ext-ms-example.dll", "api-example.dll", "..\\escape.dll", "/evil.dll",
                    "C:evil.dll", "stream.dll:secret", "wild*.dll", "NUL.dll", "com1.dll", "LPT9.bin", "CONIN$.dll",
                    "plain", "trailing.dll.", "space name.dll", "", "weird\x01.dll"});
    const auto r = detail::CheckDependencyCandidates(pe, {L"C:\\deps"}, 5000, api);
    Check(r && probes.size() == 1, "only the explicitly supplied directory is queried for skipped names");
    for (std::size_t i = 0; i < r.value().libraries.size(); ++i) {
        const auto& lib = r.value().libraries[i];
        Check(lib.status == (i < 3 ? DependencyStatus::ApiSet : DependencyStatus::UnsupportedName) && lib.candidates.empty(),
              "special or unsafe name cannot escape into filesystem probing");
    }
}
void Directories() {
    auto api = Api();
    for (const auto& dir : {L"", L"\\\\server\\share", L"\\\\?\\C:\\deps", L"Z:\\remote", L"C:\\absent", L"C:\\file.dll"}) {
        attributes[L"C:\\file.dll"] = FILE_ATTRIBUTE_NORMAL;
        const auto r = detail::CheckDependencyCandidates(Image({"present.dll"}), {dir}, 5000, api);
        Check(!r, "invalid or unavailable search scope fails the operation");
    }
    failures[L"C:\\deps"] = ERROR_ACCESS_DENIED;
    const auto r = detail::CheckDependencyCandidates(Image({}), {L"C:\\deps"}, 5000, api);
    Check(!r && r.error().category == ErrorCategory::AccessDenied && r.error().win32 == ERROR_ACCESS_DENIED, "directory permission error is not missing dependency");
}
void Limits() {
    auto api = Api();
    auto r = detail::CheckDependencyCandidates(Image({"one.dll"}), std::vector<std::wstring>(17, L"C:\\deps"), 5000, api);
    Check(!r && probes.empty(), "directory cap checked before probing");
    r = detail::CheckDependencyCandidates(Image({}), {}, 5000, api);
    Check(!r && probes.empty(), "explicit scan needs directories");
    auto pe = Image({}); pe.imports.resize(1025);
    r = detail::CheckDependencyCandidates(pe, {L"C:\\deps"}, 5000, api);
    Check(!r && r.error().category == ErrorCategory::Unsupported && probes.empty(), "library resource cap");
    pe = Image({}); pe.delay_imports.resize(1024, {"safe.dll", "delay_import_name_table", 0, {}});
    r = detail::CheckDependencyCandidates(pe, {L"C:\\deps"}, 5000, api);
    Check(r && r.value().libraries.size() == 1024, "library limit includes delay imports and accepts boundary");
}
void Timeout() {
    auto api = Api();
    auto r = detail::CheckDependencyCandidates(Image({"one.dll"}), {L"C:\\deps"}, 0, api);
    Check(!r && r.error().category == ErrorCategory::Timeout && probes.empty(), "zero budget does no IO");
    step = 10;
    r = detail::CheckDependencyCandidates(Image({"one.dll"}), {L"C:\\deps"}, 15, api);
    Check(!r && r.error().category == ErrorCategory::Timeout && probes.size() == 2, "deadline checked after final system call");
}
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string name = argv[1];
    if (name == "candidates") Candidates();
    else if (name == "unknown") Unknown();
    else if (name == "names") Names();
    else if (name == "directories") Directories();
    else if (name == "limits") Limits();
    else if (name == "timeout") Timeout();
    else return 2;
    return 0;
}
