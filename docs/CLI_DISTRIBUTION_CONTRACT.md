# CLI distribution contract

Date: 2026-10-25. P4 distribution groundwork: CI download artifacts with integrity
metadata and an optional pre-execution manifest check in the local Skill installer.
This does not publish a GitHub Release, resolve project licensing, sign binaries,
download executables or configure MCP clients.

## Bundle

- Build each archive from the explicitly selected, trusted Release CLI. Read its
  PE machine/optional-header magic and run its bounded version query; the queried
  binary must not change while packaging. Require the requested x86/x64 architecture.
- ZIP contains exactly `dllinjector.exe`, `manifest.json`, `README.txt` and
  `THIRD_PARTY_NOTICES.md`. No GUI, demos, reports, credentials or build directories.
- Manifest format 1: product `dll-doctor-cli`, platform `windows`, architecture
  `x86`/`x64`, tool/schema versions, source revision (40 hex or null), runtime
  `msvc-v14`, and one `files.dllinjector.exe` entry with size and SHA-256.
- Omit inferred build provenance: CI supplies the checkout SHA explicitly; a
  locally reused binary has null source revision unless its builder supplies one.
- Archive is named `dll-doctor-cli-<version>-windows-<arch>.zip`. Its sibling
  `.zip.sha256` contains lowercase SHA-256, two spaces and the archive basename.
  Refuse existing output files. Clean up outputs created by a caught packaging failure.
- CI uploads bundles for both Release architectures as review/download artifacts.
  Build outputs remain ignored; no new EXE is tracked by Git.

## Installer verification

- `setup --cli <absolute.exe> --manifest <absolute manifest.json>` is optional for
  existing source-build users. A supplied manifest is never ignored or downgraded.
- Before executing the CLI or writing installation files, validate a bounded
  regular JSON manifest, fixed product/platform/format/runtime and file name,
  size (maximum 64 MiB), SHA-256 and PE x86/x64 machine/magic consistency.
  Do not resolve or execute file paths or URLs from manifest content.
- `--dry-run` may read/hash files but does not execute the CLI or write anything.
  Report the checksum result separately from CLI startup or Agent discovery.
- After the normal CLI probe, require the observed tool/schema/hash to match the
  manifest. Preserve manifest identity/provenance in the local install binding.
  Existing `verify` continues checking installed CLI bytes and its runtime version.
- Hashes prove agreement with the supplied manifest, not publisher authenticity.
  Obtain the archive/checksum/manifest from a trusted CI run or publisher. No
  signature or tamper-proof receipt guarantee is implied.
- MSVC builds require the matching Visual C++ v14 runtime, at least as recent as
  their build tools. Runtime installers are not bundled or run automatically.

## Acceptance

Validate archive allowlist, byte/hash/architecture/version metadata, checksum,
revision handling, no-overwrite and cleanup on failure. Test truncated/wrong PE,
malformed or oversized manifests, unsafe file names, incorrect size/hash/architecture,
and prove rejection occurs before any executable probe or installation writes.
Exercise an actual packaged native CLI through setup/verify/read-only query/uninstall
outside the source tree; CI repeats this for x86 and x64 Release.

Runtime source: [Microsoft Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
