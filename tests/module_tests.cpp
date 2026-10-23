#include "process/process_detail.h"
#include "cli.h"
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace dllinjector;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
HANDLE Process() { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(20)); }
HANDLE Snapshot() { return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(21)); }
struct State {
    unsigned opened = 0, closed = 0, snapshots = 0, sleeps = 0, next = 0;
    unsigned unstable = 0;
    DWORD openError = 0, snapshotError = 0, firstError = 0, nextError = ERROR_NO_MORE_FILES;
    DWORD timeError = 0, architectureError = 0;
    ULONGLONG ticks = 0, snapshotDelay = 0, nextDelay = 0;
    USHORT machine = IMAGE_FILE_MACHINE_I386;
    bool truncate = false, unterminated = false, wrongPid = false, lossy = false;
} state;
std::uintptr_t LargeAddress() {
    if constexpr (sizeof(void*) == 8) return static_cast<std::uintptr_t>(0x20000000000001ULL);
    else return 0x12345000;
}
HANDLE WINAPI Open(DWORD rights, BOOL inherit, DWORD pid) {
    Check(rights == PROCESS_QUERY_LIMITED_INFORMATION && !inherit && pid == 42, "minimal non-inheritable target access");
    ++state.opened;
    SetLastError(state.openError);
    return state.openError ? nullptr : Process();
}
BOOL WINAPI Close(HANDLE handle) {
    Check(handle == Process() || handle == Snapshot(), "close only valid handles");
    ++state.closed; SetLastError(ERROR_INVALID_HANDLE); return TRUE;
}
BOOL WINAPI ArchitectureQuery(HANDLE handle, USHORT* process, USHORT* native) {
    Check(handle == Process(), "architecture must use retained process handle");
    *process = state.machine; *native = IMAGE_FILE_MACHINE_AMD64;
    SetLastError(state.architectureError);
    return state.architectureError == 0;
}
BOOL WINAPI Times(HANDLE handle, LPFILETIME creation, LPFILETIME, LPFILETIME, LPFILETIME) {
    Check(handle == Process(), "time must use retained process handle");
    creation->dwHighDateTime = 1; creation->dwLowDateTime = 2;
    SetLastError(state.timeError); return state.timeError == 0;
}
ULONGLONG WINAPI Tick() { return state.ticks; }
void WINAPI SleepStub(DWORD ms) { Check(ms == 1, "bounded retry wait"); ++state.sleeps; state.ticks += ms; }
HANDLE WINAPI TakeSnapshot(DWORD flags, DWORD pid) {
    Check(pid == 42 && flags == (TH32CS_SNAPMODULE | (sizeof(void*) == 8 ? TH32CS_SNAPMODULE32 : 0)),
          "module-only snapshot with architecture-appropriate flags");
    ++state.snapshots; state.ticks += state.snapshotDelay;
    if (state.unstable) { --state.unstable; SetLastError(ERROR_BAD_LENGTH); return INVALID_HANDLE_VALUE; }
    SetLastError(state.snapshotError);
    return state.snapshotError ? INVALID_HANDLE_VALUE : Snapshot();
}
void Fill(LPMODULEENTRY32W entry, bool first) {
    Check(entry->dwSize == sizeof(*entry), "initialize module structure size");
    entry->th32ProcessID = state.wrongPid ? 99 : 42;
    entry->modBaseAddr = reinterpret_cast<BYTE*>(first ? LargeAddress() : std::uintptr_t{0x1000});
    entry->modBaseSize = first ? 8192 : 4096;
    std::fill(std::begin(entry->szModule), std::end(entry->szModule), L'\0');
    std::fill(std::begin(entry->szExePath), std::end(entry->szExePath), L'\0');
    const std::wstring name = first ? L"插件 🚀.dll" : L"host.exe";
    const std::wstring path = L"C:\\测试 space\\" + name;
    std::copy(name.begin(), name.end(), entry->szModule);
    std::copy(path.begin(), path.end(), entry->szExePath);
    if (state.lossy) entry->szExePath[3] = L'?';
    if (state.truncate) {
        std::fill(std::begin(entry->szModule), std::end(entry->szModule) - 1, L'名');
        std::fill(std::begin(entry->szExePath), std::end(entry->szExePath) - 1, L'路');
    }
    if (state.unterminated) std::fill(std::begin(entry->szExePath), std::end(entry->szExePath), L'路');
}
BOOL WINAPI First(HANDLE handle, LPMODULEENTRY32W entry) {
    Check(handle == Snapshot(), "valid snapshot for first");
    state.next = 0; SetLastError(state.firstError);
    if (state.firstError) return FALSE;
    Fill(entry, true); return TRUE;
}
BOOL WINAPI Next(HANDLE handle, LPMODULEENTRY32W entry) {
    Check(handle == Snapshot(), "valid snapshot for next");
    state.ticks += state.nextDelay;
    if (++state.next == 1) { Fill(entry, false); return TRUE; }
    SetLastError(state.nextError); return FALSE;
}
detail::ProcessApi Api() {
    detail::ProcessApi api;
    api.openProcess = Open; api.closeHandle = Close; api.isWow64Process2 = ArchitectureQuery;
    api.times = Times; api.snapshot = TakeSnapshot; api.moduleFirst = First; api.moduleNext = Next;
    api.sleep = SleepStub; api.tick = Tick; return api;
}
auto Query(QueryOptions options = {}) { return detail::ListModules(42, options, Api()); }
void Success() {
    const auto result = Query();
    Check(result && result.value().modules.size() == 2 && state.opened == 1 && state.closed == 2,
          "complete snapshot and owned handles");
    const auto& rows = result.value().modules;
    Check(rows[0].base_address == 0x1000 && rows[1].base_address == LargeAddress(), "sort without pointer narrowing");
    Check(rows[1].name == L"插件 🚀.dll" && rows[1].path == L"C:\\测试 space\\插件 🚀.dll" &&
          rows[1].size_bytes == 8192 && !rows[1].path_may_be_truncated && !rows[1].text_may_be_lossy,
          "Unicode and module metadata");
    cli::Report report; report.command = "process.modules"; report.pid = 42; report.accepted = true;
    report.data = result.value();
    const auto json = cli::Render(report, true);
    Check(json && json.value().find("\"base_address\":\"0x1000\"") != std::string::npos, "JSON address is a string");
    if constexpr (sizeof(void*) == 8)
        Check(json.value().find("0x20000000000001") != std::string::npos, "JSON preserves bits above 2^53");
    state = {}; state.firstError = ERROR_NO_MORE_FILES;
    const auto empty = Query();
    Check(empty && empty.value().modules.empty() && state.closed == 2, "empty snapshot is distinct from failure");
}
void Failures() {
    auto zero = detail::ListModules(0, {}, Api());
    Check(!zero && zero.error().category == ErrorCategory::InvalidArgument && state.opened == 0, "PID 0 not implicit self");
    state.openError = ERROR_ACCESS_DENIED;
    auto denied = Query();
    Check(!denied && denied.error().category == ErrorCategory::AccessDenied && state.closed == 0 && state.snapshots == 0,
          "do not snapshot inaccessible process");
    state = {}; state.architectureError = ERROR_INVALID_PARAMETER;
    auto architecture = Query();
    Check(!architecture && architecture.error().stage == "IsWow64Process2" && state.closed == 1, "architecture errors preserved");
    state = {}; state.timeError = ERROR_PARTIAL_COPY;
    auto time = Query();
    Check(!time && time.error().stage == "GetProcessTimes" && state.closed == 1, "time errors preserved");
    state = {}; state.snapshotError = ERROR_PARTIAL_COPY;
    auto snapshot = Query();
    Check(!snapshot && snapshot.error().win32 == std::uint32_t{ERROR_PARTIAL_COPY} && state.snapshots == 1 && state.closed == 1,
          "partial copy is not guessed as architecture mismatch and not retried");
    state = {}; state.firstError = ERROR_ACCESS_DENIED;
    auto first = Query();
    Check(!first && first.error().stage == "Module32FirstW" && first.error().win32 == std::uint32_t{ERROR_ACCESS_DENIED}
          && state.closed == 2, "first error survives cleanup");
    state = {}; state.nextError = ERROR_BAD_LENGTH;
    auto next = Query();
    Check(!next && next.error().stage == "Module32NextW" && next.error().win32 == std::uint32_t{ERROR_BAD_LENGTH}
          && state.closed == 2, "iteration failure never returns partial success");
}
void Architectures() {
    state.machine = IMAGE_FILE_MACHINE_AMD64;
    const auto x64 = Query();
    if constexpr (sizeof(void*) == 4) {
        Check(!x64 && x64.error().category == ErrorCategory::Unsupported && !x64.error().win32 &&
              x64.error().message.find("x64 CLI") != std::string::npos && state.snapshots == 0 && state.closed == 1,
              "x86 to x64 rejected with actionable error, not fabricated Win32 code");
    } else Check(x64 && x64.value().architecture.architecture == Architecture::X64, "native x64 snapshot");
    state = {}; state.machine = IMAGE_FILE_MACHINE_ARM64;
    auto arm = Query();
    Check(!arm && arm.error().category == ErrorCategory::Unsupported && state.snapshots == 0 && state.closed == 1,
          "ARM64 support is not assumed");
}
void Retries() {
    state.unstable = 2;
    Check(static_cast<bool>(Query()) && state.snapshots == 3 && state.sleeps == 2 && state.closed == 2,
          "retry bad length then succeed without closing invalid snapshots");
    state = {}; state.unstable = 99;
    auto exhausted = Query();
    Check(!exhausted && exhausted.error().win32 == std::uint32_t{ERROR_BAD_LENGTH} && state.snapshots == 8 &&
          state.sleeps == 7 && state.closed == 1, "retries bounded independently of clock");
    state = {}; state.unstable = 99;
    auto timed = Query({2});
    Check(!timed && timed.error().category == ErrorCategory::Timeout && state.snapshots < 8 && state.closed == 1,
          "retry delay consumes common deadline");
}
void Timeout() {
    auto zero = Query({0});
    Check(!zero && zero.error().category == ErrorCategory::Timeout && state.opened == 0, "expired query performs no work");
    state = {}; state.snapshotDelay = 10;
    auto slow = Query({5});
    Check(!slow && slow.error().category == ErrorCategory::Timeout && state.closed == 2, "close late successful snapshot");
    state = {}; state.nextDelay = 10;
    auto traversal = Query({5});
    Check(!traversal && traversal.error().category == ErrorCategory::Timeout && state.closed == 2, "timeout discards partial traversal");
}
void Bounds() {
    state.truncate = true;
    auto limited = Query();
    Check(limited && limited.value().modules[0].path_may_be_truncated && limited.value().modules[0].name_may_be_truncated,
          "capacity-length strings are not advertised as complete");
    state = {}; state.lossy = true;
    const auto lossy = Query();
    Check(lossy && lossy.value().modules[0].text_may_be_lossy, "flag replacement characters from Toolhelp text");
    state = {}; state.unterminated = true;
    auto invalid = Query();
    Check(!invalid && invalid.error().stage == "modules.entry" && !invalid.error().win32 && state.closed == 2,
          "unterminated strings fail without out-of-bounds reads");
    state = {}; state.wrongPid = true;
    auto wrong = Query();
    Check(!wrong && wrong.error().stage == "modules.entry" && state.closed == 2, "reject entries from different PID");
}
}
int main(int argc, char** argv) {
    try {
        Check(argc == 2, "test case required");
        const std::string name = argv[1];
        if (name == "success") Success();
        else if (name == "failures") Failures();
        else if (name == "architectures") Architectures();
        else if (name == "retries") Retries();
        else if (name == "timeout") Timeout();
        else if (name == "bounds") Bounds();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS modules " << name << '\n'; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n'; return 1;
    }
}
