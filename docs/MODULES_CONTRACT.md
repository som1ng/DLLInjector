# Read-only module enumeration contract

Development version: `2.0.0-dev.2`. Command: `process modules --pid <PID>`.
The existing JSON, timeout, output-file and logging options apply unchanged.
No target DLL is loaded or executed by the diagnostic command.

## Observation and limits

- Public Core entry: `Result<ModuleSnapshot> ListModules(pid, options)`.
- Hold one non-inheritable `PROCESS_QUERY_LIMITED_INFORMATION` process handle while
  querying creation time, architecture and the Toolhelp module snapshot.
- x64 callers include native and WOW64 modules using SNAPMODULE | SNAPMODULE32;
  x86 callers use SNAPMODULE and support x86 targets. x86 → x64 is rejected before
  snapshot creation: category unsupported, stage modules.architecture, exit 4,
  message recommending the x64 CLI, no invented raw Win32 error.
- ARM64 targets are explicitly unsupported in this increment; no unverified
  architecture helper, memory scan or privilege elevation is attempted.
- Retry snapshot creation only on ERROR_BAD_LENGTH, at most 8 attempts with 1 ms
  between attempts, within the shared cooperative query deadline. Exhaustion is
  a system failure with the original error and a retry hint. Other errors return
  immediately. A real ERROR_PARTIAL_COPY is preserved without assuming its cause.
- Module32First/Next stop normally only on ERROR_NO_MORE_FILES. Other errors fail
  the entire command; no partial list is labelled successful. An empty snapshot
  is a successful observation of no entries, not proof that no code is loaded.
- Validate the returned PID and bounds of fixed-size UTF-16 fields. Reaching a
  field's character capacity is flagged as possible truncation, not a full path.
- Real local testing found that Toolhelp's W API can already contain `?`
  replacements for emoji before our UTF-8 conversion. Preserve the reported text
  and set text_may_be_lossy when either field contains `?`. This is a conservative
  signal, not a guarantee that other best-fit character conversions are detectable.
  Paths are observations, not canonical file identifiers; do not use flagged paths
  as file-operation inputs. Full Unicode provided by the API is preserved.
- Sort modules by base address, then path. JSON addresses are hexadecimal strings
  to preserve all pointer bits for JavaScript clients; sizes are integer bytes.
- Results are a loader snapshot: transient load/unload changes and modules loaded
  as data files are outside a completeness guarantee. Module presence does not
  prove DllMain initialization, plugin health, or successful injection.

## JSON compatibility

All commands in dev.2 emit schema_version `1.1`. `schemas/cli-v1.1.json` adds the
process.modules command/data shape without changing existing field meanings.
The published-in-repository `cli-v1.json` and 1.0 fixtures remain unchanged.
Consumers select a validator by schema_version; strict 1.0 validators are not
expected to accept 1.1. Tool version changes to `2.0.0-dev.2`.

Module data contains pid, creation_time_filetime, architecture, architecture_source,
scope (`toolhelp_snapshot`), and modules[]. Each module has name, path,
base_address (`0x` + lowercase hex), size_bytes, name_may_be_truncated and
path_may_be_truncated and text_may_be_lossy. Evidence describes the loader snapshot's limitations.

## Required tests

- Core mocks: both module flags, minimum rights, same process handle, ordering,
  Unicode, pointer width, empty snapshot, access denied, invalid PID, cross-bitness,
  ARM64, snapshot/iteration errors, bounded retries, timeout, malformed/truncated
  strings and cleanup without losing the original error.
- Real CLI: a self-owned host loads only the existing inert fixture from a copied
  Unicode/space/emoji path; match the independently reported base address and check
  path/name equality or explicit API text-loss markers, plus size. Test
  help, missing/invalid/duplicate PID, JSON Schema, text, output and zero timeout.
- Cross-bitness: use x86/x64 CLI and self-owned test hosts; x64 → x86 succeeds,
  x86 → x64 returns the documented error. Run these locally and in Windows CI.
- Existing 16 regression groups remain green; old 1.0 fixtures still validate.

## Official references checked 2026-10-22

- [CreateToolhelp32Snapshot](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot): flags, bitness, retries and loader visibility limits.
- [MODULEENTRY32W](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/ns-tlhelp32-moduleentry32w): fixed field capacities, target address and image size.
- [Module32FirstW](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-module32firstw) and [Module32NextW](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-module32nextw): initialization and end-of-enumeration errors.
