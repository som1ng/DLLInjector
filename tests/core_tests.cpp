#include "process/process_detail.h"
#include "dllinjector/text.h"
#include "cli.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dllinjector;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
HANDLE Process() { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(10)); }
HANDLE Snapshot() { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(11)); }
int opens = 0, closes = 0, position = 0;
DWORD openError = 0, snapshotError = 0, firstError = 0, nextError = ERROR_NO_MORE_FILES;
DWORD timeError = 0, pathError = 0, architectureError = 0;
bool denyOne = false;
ULONGLONG ticks = 0, imageDelay = 0;
std::wstring imagePath = L"C:\\测试\\" + std::wstring(280, L'长') + L"\\example.exe";
ULONGLONG WINAPI Tick() { return ticks; }
HANDLE WINAPI Open(DWORD rights, BOOL inherit, DWORD pid) {
    Check(rights == PROCESS_QUERY_LIMITED_INFORMATION && !inherit, "minimum non-inheritable access");
    ++opens;
    const DWORD code = denyOne && pid == 9 ? ERROR_ACCESS_DENIED : openError;
    SetLastError(code);
    return code ? nullptr : Process();
}
BOOL WINAPI Close(HANDLE handle) {
    Check(handle == Process() || handle == Snapshot(), "only valid owned handles close");
    ++closes;
    SetLastError(ERROR_INVALID_HANDLE);
    return TRUE;
}
HANDLE WINAPI TakeSnapshot(DWORD flags, DWORD pid) {
    Check(flags == TH32CS_SNAPPROCESS && pid == 0, "process-only snapshot");
    SetLastError(snapshotError);
    return snapshotError ? INVALID_HANDLE_VALUE : Snapshot();
}
void Fill(LPPROCESSENTRY32W entry, DWORD pid) {
    Check(entry->dwSize == sizeof(*entry), "initialize snapshot entry size");
    entry->th32ProcessID = pid;
    wcscpy_s(entry->szExeFile, L"测试.exe");
}
BOOL WINAPI First(HANDLE handle, LPPROCESSENTRY32W entry) {
    Check(handle == Snapshot(), "snapshot handle for first");
    position = 0;
    SetLastError(firstError);
    if (firstError) return FALSE;
    Fill(entry, 9);
    return TRUE;
}
BOOL WINAPI Next(HANDLE handle, LPPROCESSENTRY32W entry) {
    Check(handle == Snapshot(), "snapshot handle for next");
    ++position;
    if (position < 3) { Fill(entry, position == 1 ? 0 : 7); return TRUE; }
    SetLastError(nextError);
    return FALSE;
}
BOOL WINAPI Times(HANDLE handle, LPFILETIME creation, LPFILETIME, LPFILETIME, LPFILETIME) {
    Check(handle == Process(), "creation time uses the opened handle");
    creation->dwHighDateTime = 0x01d00000;
    creation->dwLowDateTime = 1;
    SetLastError(timeError);
    return timeError == 0;
}
BOOL WINAPI ImageName(HANDLE handle, DWORD flags, LPWSTR buffer, PDWORD size) {
    Check(handle == Process() && flags == 0, "path uses same handle, Win32 format");
    Check(*size > imagePath.size(), "path capacity is WCHAR count and supports long paths");
    ticks += imageDelay;
    if (pathError) { SetLastError(pathError); return FALSE; }
    std::copy(imagePath.begin(), imagePath.end(), buffer);
    buffer[imagePath.size()] = 0;
    *size = static_cast<DWORD>(imagePath.size());
    return TRUE;
}
BOOL WINAPI ArchitectureQuery(HANDLE handle, USHORT* process, USHORT* native) {
    Check(handle == Process(), "architecture reuses same handle");
    *process = IMAGE_FILE_MACHINE_UNKNOWN;
    *native = IMAGE_FILE_MACHINE_AMD64;
    SetLastError(architectureError);
    return architectureError == 0;
}
detail::ProcessApi Api() {
    detail::ProcessApi api;
    api.openProcess = Open; api.closeHandle = Close;
    api.snapshot = TakeSnapshot; api.first = First; api.next = Next;
    api.times = Times; api.imageName = ImageName;
    api.isWow64Process2 = ArchitectureQuery; api.tick = Tick;
    return api;
}
void Errors() {
    Check(ExitCode(ErrorCategory::InvalidArgument) == 2, "usage exit");
    Check(ExitCode(ErrorCategory::AccessDenied) == 3 && ExitCode(ErrorCategory::NotFound) == 3, "inaccessible exit");
    Check(ExitCode(ErrorCategory::Unsupported) == 4 && ExitCode(ErrorCategory::System) == 4 &&
          ExitCode(ErrorCategory::Io) == 4, "internal/output exit");
    Check(ExitCode(ErrorCategory::Timeout) == 5, "timeout exit");
    const auto api = Api();
    const auto zero = detail::InspectProcess(0, {}, api);
    Check(!zero && zero.error().category == ErrorCategory::InvalidArgument && opens == 0, "reject zero before open");
    openError = ERROR_ACCESS_DENIED;
    const auto denied = detail::InspectProcess(42, {}, api);
    Check(!denied && denied.error().category == ErrorCategory::AccessDenied && denied.error().win32 == 5u && closes == 0,
          "denied is not unknown success and no invalid handle is closed");
    openError = ERROR_INVALID_PARAMETER;
    const auto missing = detail::InspectProcess(42, {}, api);
    Check(!missing && missing.error().category == ErrorCategory::NotFound, "unavailable PID");
    openError = 0; pathError = ERROR_PARTIAL_COPY;
    const auto failedPath = detail::InspectProcess(42, {}, api);
    Check(!failedPath && failedPath.error().stage == "QueryFullProcessImageNameW" &&
          failedPath.error().win32 == std::uint32_t{ERROR_PARTIAL_COPY} && closes == 1, "path failure survives cleanup");
    pathError = 0; timeError = ERROR_ACCESS_DENIED;
    const auto failedTime = detail::InspectProcess(42, {}, api);
    Check(!failedTime && failedTime.error().stage == "GetProcessTimes" && closes == 2, "time failure is explicit");
    timeError = 0; architectureError = ERROR_BAD_LENGTH;
    const auto failedArch = detail::InspectProcess(42, {}, api);
    Check(!failedArch && failedArch.error().stage == "IsWow64Process2" &&
          failedArch.error().win32 == std::uint32_t{ERROR_BAD_LENGTH} && closes == 3, "architecture failure survives cleanup");
}
void Enumeration() {
    const auto api = Api();
    snapshotError = ERROR_NOT_ENOUGH_MEMORY;
    const auto noSnapshot = detail::ListProcesses({}, api);
    Check(!noSnapshot && noSnapshot.error().stage == "CreateToolhelp32Snapshot" && closes == 0, "snapshot creation failure");
    snapshotError = 0; firstError = ERROR_BAD_LENGTH;
    const auto noFirst = detail::ListProcesses({}, api);
    Check(!noFirst && noFirst.error().win32 == std::uint32_t{ERROR_BAD_LENGTH} && closes == 1, "first-entry failure, cleanup");
    firstError = ERROR_NO_MORE_FILES;
    const auto empty = detail::ListProcesses({}, api);
    Check(empty && empty.value().empty() && closes == 2, "empty snapshot differs from error");
    firstError = 0; denyOne = true;
    const auto rows = detail::ListProcesses({}, api);
    Check(rows && rows.value().size() == 2 && rows.value()[0].pid == 7 && rows.value()[1].pid == 9,
          "omit PID 0, preserve and sort other entries");
    Check(rows.value()[0].creation_time && rows.value()[0].errors.empty(), "available creation metadata");
    Check(!rows.value()[1].creation_time && rows.value()[1].errors.size() == 1 &&
          rows.value()[1].errors[0].category == ErrorCategory::AccessDenied, "retain inaccessible snapshot row");
    Check(closes == 4, "close snapshot and each opened process");
    nextError = ERROR_PARTIAL_COPY;
    const auto partial = detail::ListProcesses({}, api);
    Check(!partial && partial.error().stage == "Process32NextW" && partial.error().win32 == std::uint32_t{ERROR_PARTIAL_COPY},
          "failed enumeration cannot be reported as successful partial list");
    nextError = ERROR_NO_MORE_FILES; denyOne = false; timeError = ERROR_INVALID_PARAMETER;
    const auto noTimes = detail::ListProcesses({}, api);
    Check(noTimes && !noTimes.value()[0].creation_time && noTimes.value()[0].errors[0].stage == "GetProcessTimes",
          "time query failure is retained per row");
}
void Inspect() {
    const auto result = detail::InspectProcess(42, {}, Api());
    Check(result && result.value().pid == 42 && result.value().image_path == imagePath &&
          result.value().name == L"example.exe", "full Unicode path and basename");
    Check(result.value().architecture.architecture == Architecture::X64 &&
          result.value().creation_time == (std::uint64_t{0x01d00000} << 32) + 1, "typed observed metadata");
    Check(opens == 1 && closes == 1, "inspect owns exactly one process handle");
}
void Timeout() {
    const auto api = Api();
    const auto immediate = detail::InspectProcess(42, {0}, api);
    Check(!immediate && immediate.error().category == ErrorCategory::Timeout && opens == 0, "zero timeout performs no query");
    const auto list = detail::ListProcesses({0}, api);
    Check(!list && closes == 0, "expired list does not create a snapshot");
    imageDelay = 10;
    const auto expired = detail::InspectProcess(42, {5}, api);
    Check(!expired && expired.error().category == ErrorCategory::Timeout && opens == 1 && closes == 1,
          "deadline after API call discards result and closes handle");
}
void TextJson() {
    const auto text = ToUtf8(L"中文 🚀");
    Check(text && text.value() == u8"中文 🚀", "UTF-16 supplementary characters become UTF-8");
    const auto invalid = ToUtf8(std::wstring(1, wchar_t{0xd800}));
    Check(!invalid && invalid.error().win32 == std::uint32_t{ERROR_NO_UNICODE_TRANSLATION}, "reject unpaired surrogate");
    cli::Report report;
    report.command = "process.list"; report.accepted = true;
    report.data = std::vector<ProcessSummary>{{42, std::wstring(L"中\"\\\n\t\0", 6), {}, {}}};
    const auto json = cli::Render(report, true);
    Check(json && json.value().find(u8"中\\\"\\\\\\u000a\\u0009\\u0000") != std::string::npos,
          "escape quotes, backslash, control characters and embedded NUL");
    report.data = std::vector<ProcessSummary>{{42, std::wstring(1, wchar_t{0xdc00}), {}, {}}};
    Check(!cli::Render(report, true), "invalid Unicode is a structured encoding failure");
}
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "test case required");
        const std::string name = argv[1];
        if (name == "errors") Errors();
        else if (name == "enumeration") Enumeration();
        else if (name == "inspect") Inspect();
        else if (name == "timeout") Timeout();
        else if (name == "text-json") TextJson();
        else throw std::runtime_error("unknown test case");
        std::cout << "PASS core " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
