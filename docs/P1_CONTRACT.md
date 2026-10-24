# P1 process CLI contract — schema 1.0

For current dev.6 / Schema 1.5 additions, see [Doctor contract](DOCTOR_CONTRACT.md).
Existing process queries retain their behavior; `target.path` is null for them.

This document records the dev.1 baseline. For dev.2, the process commands and common
options still apply; all responses now use schema 1.1 and `process modules` is added.
See [the module contract](MODULES_CONTRACT.md) for version migration and limits.

This increment delivers the shared read-only process Core and `process list/inspect`.
Module enumeration, PE/Doctor, reports and Agent adapters remain later increments.

## Commands and options

`dllinjector --help`, `--version`, `process list`, `process inspect --pid <1..4294967295>`.
Options may precede or follow the command: `--json`, `--verbose`,
`--timeout-ms <0..600000>` (default 5000), `--output <path>`.
Duplicate/unknown options, extra positional arguments, negative/overflowing numbers,
missing values and PID on any command other than inspect are usage errors.
Help/version cannot be combined with a process command. Unimplemented commands fail.

UTF-16 arguments and Windows paths are preserved. Redirected stdout and output files
are UTF-8 without BOM. `--json` emits exactly one JSON document plus a newline;
errors and optional `[debug]` messages use stderr. `--output` copies the result to a
new file, including failed-query reports; it never overwrites an existing file.
An output failure is itself an error, emitted to stdout in the requested format.
Invalid syntax never creates a file. Parent directories must already exist.

Timeout is a cooperative **query** deadline measured by a monotonic clock: checked
before/after Win32 queries and between snapshot entries. Zero expires immediately
for process commands. It cannot interrupt an in-flight Win32 call or a blocked
output sink; callers needing a hard wall-clock bound must also time out the process.
Help/version do not query processes. Timed-out queries do not emit partial success.

## Result and evidence

Core public operations return `Result<T>` (value or Error, never both). Errors carry
category, stage, stable English message, optional raw Win32/NTSTATUS, and `verified`
(the error condition was observed, not a guarantee of a target outcome).
Handles are owned by RAII; all process opens request only
`PROCESS_QUERY_LIMITED_INFORMATION`, non-inheritable. No privilege adjustment.

List contains snapshot PID/name, optional creation time and per-entry metadata
errors. Inaccessible/exited entries remain in the snapshot; this is not an overall
enumeration failure. An unexpected snapshot-walk failure fails the whole command.
PID 0 is omitted. Output is sorted by PID. The GUI adapter retains its previous
creation-time ordering. Inspect queries image path, creation time and architecture
using **one handle** so PID reuse cannot mix observations from different processes.
Observations are not atomic and do not guarantee that the process is still alive.

Architecture is x86/x64/arm64, or an explicit error. A failed modern API does not
silently fall back; fallback is only used if the API is absent. The old GUI keeps
its Unknown/error adapter. FILETIME values are decimal strings (100 ns since
1601-01-01 UTC), avoiding JavaScript integer precision loss; missing values are null.

The envelope includes schema_version, tool_version, command, status, target,
execution, data, findings, evidence, errors, duration_ms. Schema: `schemas/cli-v1.json`.
Version starts at `2.0.0-dev.1`; this is a development build, not a published release.
Execution tracks command_accepted, operation_completed and outcome_verified
separately. Read-only success verifies only the reported observations. Syntax
failures are not accepted; operational failures are accepted but not completed or
verified. Findings stay empty until diagnostic rules exist.

| Exit | Meaning |
| --- | --- |
| 0 | Read-only command completed |
| 1 | Reserved for future blocking diagnostic findings |
| 2 | Invalid usage / invalid argument |
| 3 | Access denied or target unavailable |
| 4 | Unsupported operation, system, encoding or output failure |
| 5 | Query deadline exceeded |

## Tests required before merge

- Retain the P0 GUI/architecture/privilege regression tests.
- Core mocks: access denied, captured errors after CloseHandle, failed snapshot
  creation/first/next, per-entry failures, one-handle inspect, timeout and cleanup.
- CLI subprocess tests: help/version, list, inspect owned Unicode executable,
  unavailable PID, invalid/duplicate arguments, all exit-code mappings, UTF-8,
  output creation/conflicts/failure, verbose stderr isolation and zero timeout.
- Validate every JSON result and fixed fixtures using Draft 2020-12 JSON Schema.
- Build/test MSVC x86/x64 Debug/Release and available MinGW x64; verify remote CI.

## API references checked 2026-10-22

- [QueryFullProcessImageNameW](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-queryfullprocessimagenamew): character capacity and limited query access.
- [IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2): machine identity and availability.
- [Process32NextW](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-process32nextw): distinguish ERROR_NO_MORE_FILES from enumeration failure.
