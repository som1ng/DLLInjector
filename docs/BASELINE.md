# P0 baseline audit

Audit date: 2026-10-22. Starting commit: `8e182f19d769953d08eda25be5ee10a910a2b242`.
Work branch: `codex/p0-build-baseline`.

This round implements the first-round scope of the local upgrade plan: P0 and
the executable parts of PR-01. CLI, PE Doctor, Skill, MCP, installers and the
V3 Lab are future work. See [P0 contract](P0_CONTRACT.md).

## Observed source and call flow

The repository was read directly, including all source under `src/` and
`dll_test/`, and the VS Code configurations. Existing binary files are not
evidence that the current source builds or behaves correctly.

- `src/main_gui.cpp`: Win32 `wWinMain` -> `WindowProc`. Startup enables debug
  privilege, lists processes, and initializes the method selector. A button
  selects a PID and file/hex input, checks architecture, then dispatches to
  `src/methods/`. There is no command-line argument parser.
- `src/utils.*`: process enumeration, architecture checks, file dialog, file
  reads, console logging and token privilege helper.
- `src/methods/`: remote-thread, APC, message-hook, reflective loading,
  DLL-copy deployment, and direct-syscall experiments. Public method results
  are `bool`; they do not form a verified DLL-initialization protocol.
- `dll_test/demo.c`: a continuously running console process; it does not
  implement an alertable wait or a GUI message-loop fixture.
- `dll_test/dll_demo1.cpp`: DLL with a dummy export and a message-box thread
  started during process attach. The dummy export is not a typed hook callback.
- `dll_test/ReflectiveLoader/`: separate reflective sample and third-party
  loader sources carrying Stephen Fewer / Harmony Security copyright and
  redistribution notices. The sample also contains a GNU-specific builtin.
- `.vscode/`: machine-specific MinGW paths, stale source lists and a debugger
  pre-launch task that does not exist. No CMake, tests or CI were present.

## Baseline build reproduction

Host: Windows, Visual Studio 2022 Community / MSVC 14.42.34433 available;
CMake 4.3.2; MinGW-w64 GCC 16.1.0 (x64) on PATH. No tools were installed.

Both commands below were actually run against the starting source and failed
at link time. Local output is retained in `build/baseline/` (not versioned).

1. README command (PowerShell explicitly expands `src/methods/*.cpp`):
   `g++ src/main_gui.cpp src/utils.cpp src/methods/*.cpp -o README-build.exe -lgdi32 -lshlwapi -static`
   Missing `GetOpenFileNameW`, `sys_*_wrapper`, and `WinMain` symbols. The
   instructions omit libraries, Unicode entry-point options and assembly.
2. VS Code x64 task's flags and source list, using the installed `g++` rather
   than the nonexistent hard-coded `C:\vscode environment\...` executable:
   missing `Inject_Syscall_DLL` and `Inject_Syscall_Shellcode`. The GUI calls
   functions excluded from that task's source list.

## Confirmed defects and disposition

| ID | Priority | Observation | First-round treatment |
| --- | --- | --- | --- |
| B01 | High | `OPENFILENAMEW.nMaxFile = sizeof(wchar_t[260])` advertises 520 characters for a 260-character buffer | Fix and test character capacity/boundaries |
| B02 | High | Failed architecture queries return `false`, indistinguishable from x86; native 32-bit Windows is also misclassified | Explicit Unknown/error/stage result, modern API and conservative fallback |
| B03 | Medium | `AdjustTokenPrivileges` TRUE is accepted even with `ERROR_NOT_ALL_ASSIGNED` | Preserve API error and report failure; no automatic startup privilege request |
| B04 | High | Documented builds cannot link; no reproducible project configuration | CMake, MSVC x86/x64 presets, CTest, Windows workflow |
| B05 | Medium | README advertises nonexistent CLI flags, a missing MIT file and inconsistent feature status | Correct documentation; do not invent a project license |
| B06 | High | Reflective and deployment PE readers dereference file offsets without complete bounds checks | Record; do not reuse as the future untrusted-input Doctor parser |
| B07 | High | Several methods accept scheduling/API success as `true` without checking initialization; waits and remote buffers lack a full lifecycle contract | Record; keep GUI wording explicitly about request submission |
| B08 | High | Direct-syscall source has only GNU x64 assembly and unvalidated ABI/status assumptions; no x86 implementation | Keep source; exclude from validated CMake targets and clearly mark unavailable in that build |
| B09 | Medium | Hook and reflective samples are not general compatibility fixtures; DLL-copy deployment can alter a selected application's directory | No automatic execution in tests; document unverified behavior |
| B10 | Medium | GUI prints many details to a console which the GUI build does not expose | Record for Result/report work in P1 |

## Plan adjustments

1. P1 can define the PE/Doctor command contract, but those commands must not be
   advertised as working until the P2 implementation exists.
2. Retain the legacy GUI and all method sources. The validated MSVC build
   cannot claim support for GNU x64 syscall assembly. Do not port or repair
   that experimental backend during diagnostic groundwork.
3. Legacy code is not a license/provenance-clean starting point for a blanket
   MIT declaration. Preserve existing notices and record what remains unknown.
4. A workflow file is not proof of a GitHub-hosted run. Local Windows results
   and remote CI results must be recorded separately.
5. P0's specific architecture-result fix precedes the general P1 `Result<T>`
   contract. Do not prematurely rewrite every injection method or create
   placeholder Agent integrations.

## Verification record

Initial state: the two existing build recipes failed as described above.

After the P0 changes, all of these local Windows configurations built and
passed all 10 registered CTest cases on 2026-10-22:

| Configuration | Configure/build | CTest |
| --- | --- | --- |
| MSVC x64 Debug | PASS | 10/10 PASS |
| MSVC x64 Release | PASS | 10/10 PASS |
| MSVC Win32 Debug | PASS | 10/10 PASS |
| MSVC Win32 Release | PASS | 10/10 PASS |
| MinGW-w64 x64 Release | PASS | 10/10 PASS |

The same 10 tests were run in five configurations (50 case executions, not
50 independent test cases). Commands and limitations are recorded in
[P0 delivery](P0_DELIVERY.md). The legacy GUI startup was actually exercised
hidden, including its process list and inherited debug-privilege state. The
original console demo was started and stopped by its owning smoke test. A
new inert fixture was loaded and unloaded in the smoke-test process; the
old message-box and reflective DLLs were not executed.

B01-B05 have been addressed within this scope. B08 is isolated from the
validated build, not repaired. B06-B07 and B09-B10 remain documented legacy
limitations. Repository-wide licensing/provenance is not resolved by this audit.

Injection into another process, DLL-copy deployment, reflective payload
execution, direct syscalls, ARM64 execution, actual legacy 32-bit Windows
execution, manual VS Code F5 operation and remote GitHub Actions are
`NOT_RUN` in this baseline. No existing binary has been treated as trusted
verification evidence.

## API references checked

- [OPENFILENAMEW: nMaxFile is a character count](https://learn.microsoft.com/en-us/windows/win32/api/commdlg/ns-commdlg-openfilenamew)
- [AdjustTokenPrivileges: inspect ERROR_NOT_ALL_ASSIGNED](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-adjusttokenprivileges)
- [IsWow64Process2: process and native machine types](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2)
- [IsWow64Process: limitations of the legacy result](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process)
