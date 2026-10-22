# P0 interface and regression contract

This contract is recorded before the implementation. It covers utility fixes
and build infrastructure, not the future public CLI or general `Result<T>`.

## Architecture result

`QueryProcessArchitecture(pid)` returns a named architecture (`Unknown`,
`X86`, `X64`, `Arm64`), an original Win32 error, and an operation stage.
An inaccessible process is **Unknown**, never inferred to be x86. Unsupported
machine types also remain Unknown with `ERROR_NOT_SUPPORTED`.

Prefer dynamically resolved `IsWow64Process2`. Only absence of that API
selects the legacy fallback; an actual query failure must remain a failure.
Legacy `IsWow64Process` is combined with `GetNativeSystemInfo` on x86/x64.
On other hosts the fallback returns Unknown rather than guessing.

The GUI reports Unknown with its stage/error and does not dispatch a method.
Only a matching x86/x64 target passes the existing architecture check.

## File picker and privileges

The file-picker buffer capacity includes its terminating wide null. The
advertised character count must exactly match the actual buffer capacity.
Cancellation returns an empty string. Dialog UI is substituted in unit tests,
so the tests do not require an interactive desktop or user input.

The privilege helper succeeds only when the API succeeds **and** its immediate
last-error value is `ERROR_SUCCESS`. Error capture precedes handle cleanup.
The GUI must not call this helper at startup. Privilege regression tests
substitute API calls; they do not grant or remove privileges on the user's token.

## Required test cases

| Area | Expected coverage |
| --- | --- |
| File dialog | Exact character capacity; maximum in-buffer Unicode path; cancellation; undersized-buffer failure |
| Modern architecture | Native x64, WOW64 x86, native x86, ARM64, unsupported machine, inaccessible PID, query failure |
| Legacy architecture | Native x86, native x64, WOW64 x86, query failure, unsupported host |
| Resource/error handling | Handles closed once; original error survives a cleanup call that changes last-error |
| Privileges | TRUE/success; TRUE/not-all-assigned; FALSE/error; open-token failure; lookup failure |
| Live Windows | Current process architecture and process enumeration; invalid PID; Unicode file reads |
| Build/smoke | GUI compiles and opens/closes hidden; original console demo starts; harmless local fixture DLL loads and returns its explicit test value |

Use failures that remain failures in Release builds (`assert` alone is not
sufficient). CTest must fail on an empty test selection. Each process test has
a timeout and cleans up only children it created.

## Build and scope

- C++17, CMake/CTest without downloading a test framework.
- MSVC x64 and Win32, Debug and Release; optional installed MinGW x64 check.
- Preserve original injection method sources; direct-syscall code is not part
  of this validated build, and must not appear operational in the GUI.
- Do not run injection or deployment against third-party processes in CI.
- CI builds are artifacts for review, not a published Release.
- P1 proposal: one versioned JSON envelope and explicit command outcomes;
  introduce only implemented CLI commands, then the bounds-checked PE parser.
