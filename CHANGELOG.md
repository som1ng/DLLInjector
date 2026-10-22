# Changelog

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
