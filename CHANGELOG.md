# Changelog

## 2.0.0-dev.3 — PE header inspection milestone (2026-10-23)

- Add read-only `pe inspect <path>` and reusable Core byte/file APIs for PE32/PE32+
  headers, sections and directory locations, independent of CLI bitness.
- Check lengths, offsets, alignments and raw/virtual ranges before use; distinguish
  malformed bytes (exit 1), unsupported limits, file IO errors and timeouts.
- Add Schema 1.2 with `target.path`, static-analysis evidence and exact hexadecimal
  addresses; keep historical schemas and fixed fixtures unchanged.
- Add truncated/malformed fixtures, deterministic mutation/deadline tests and CLI
  checks against actual compiler-produced DLL/EXE files and Unicode paths.

Directory contents, dependency resolution, signatures and loadability are not
validated in this milestone. Doctor and Agent integration remain follow-up work.

## 2.0.0-dev.2 — module enumeration milestone (2026-10-23)

- Add read-only `process modules --pid`: module names, reported paths, base addresses
  and image sizes; bounded snapshot retries and explicit architecture/access errors.
- Add schema 1.1 for all current commands, keeping schema 1.0 and its fixtures intact.
  JSON base addresses are hex strings; Toolhelp text capacity and detectable `?`
  replacements are marked explicitly instead of claiming exact Unicode paths.
- Test an owned inert-DLL host, API failure cases and both x86/x64 caller directions;
  extend Windows CI with a cross-bitness job.

No published release or Agent integration is included in this milestone.

## 2.0.0-dev.1 — development milestone (2026-10-22)

- Shared UI-independent `Result<T>/Error` and read-only Windows process Core.
- New CLI: help/version, process list/inspect, schema 1.0 JSON, query deadlines,
  UTF-8 output files and stderr diagnostics. GUI remains available at its old path.
- Preserve inaccessible snapshot entries with explicit metadata errors; inspect
  uses one process handle for image path, creation time and architecture.
- Regression tests for Core failures, Unicode, CLI contracts and JSON Schema.

This is not a published release. Module enumeration, PE/Doctor and Agent
integrations are not included in this milestone.

## P0 baseline (2026-10-22)

- CMake/MSVC x86/x64 build matrix, optional MinGW x64 and Windows CI.
- Explicit architecture failures, file-dialog capacity and privilege-result fixes.
- GUI no longer enables Debug Privilege at startup; existing backends remain
  experimental and their runtime effects are not claimed as verified.
