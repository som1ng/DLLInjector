# PE inspection contract — schema 1.2

This document records the dev.3 milestone. Current dev.5 / Schema 1.4 also parses
import/export/delay tables; see [table contract](PE_TABLES_CONTRACT.md). Its stricter
content checks can reject files whose headers alone were accepted by dev.3.
Doctor is a separate command; see [Doctor contract](DOCTOR_CONTRACT.md).

Development version: `2.0.0-dev.3`. Command: `pe inspect <path> [options]`.
This is the first PE vertical slice: headers, sections and directory locations.
Import/export/delay-import contents, dependency resolution and Doctor rules are
subsequent features. A successful parse is not a Windows loader or trust verdict.

## Input and scope

- One local regular disk file, opened for reading only; no LoadLibrary, process
  launch, network upload or privilege change. Relative and Unicode paths work.
- PE32 and PE32+ are parsed independently of CLI bitness. Machine is reported as
  its numeric header value, including unrecognized values; no host compatibility
  or ARM64 runtime support is implied.
- DOS/PE signatures, signed e_lfanew, COFF/optional-header lengths, section table,
  alignments, raw/virtual ranges and overlapping sections are checked before use.
  Zero-raw-size sections and an entry point of zero are allowed.
- At most 256 MiB, 96 sections and 16 data directories. Exceeding these supported
  limits is `unsupported`, not a claim of corruption. Declared structures extending
  beyond available bytes, inconsistent ranges and malformed headers are errors.
- Directories preserve their index, address and size. Index 4 (certificate table)
  is a file offset, all others use RVA. A nullable `file_offset` is provided only
  when the entire range fits in one file-backed header/section region. Zero-fill,
  image gaps and ranges crossing regions yield null; null is not a content verdict.
  Directory contents and certificate signatures are **not** validated.
- Section names have eight raw bytes (`name_bytes`, lowercase hex); `name` is a
  display string with non-ASCII/control/backslash bytes escaped as `\xNN`.
  This preserves unusual names without corrupting JSON or terminal output.
- Files are opened with read sharing only, so incompatible writers cause an IO
  error. Parsing describes bytes read through that handle, not permanent path
  identity or a guarantee against writes through pre-existing memory mappings.
  Explicit UNC/device/extended paths and mapped remote drives are unsupported;
  normal filesystem links are resolved by Windows, not treated as a sandbox.
- The cooperative deadline covers open/read/parse. Zero expires before file IO;
  synchronous OS calls and result output cannot be interrupted. Input allocation
  is bounded and the read loop checks its deadline between 1 MiB chunks.

## CLI and errors

Common `--json`, `--timeout-ms`, `--output`, `--verbose` options apply. `--pid` is
invalid for PE inspection. Prefix an option-like filename with `./` or use its
absolute path. Output creation retains the existing no-overwrite contract.

| Exit | Meaning |
| --- | --- |
| 0 | Header/section/directory-range inspection completed |
| 1 | `malformed_pe`: inspected bytes violate the supported layout checks |
| 2 | Invalid CLI use or invalid path argument |
| 3 | File missing or access denied |
| 4 | Unsupported format/size, IO failure or internal/output failure |
| 5 | Deadline expired; no partial success |

Malformed errors identify a `pe.*` stage and have null Win32/NTSTATUS codes.
File IO errors preserve actual Win32 codes. Failed results have `data: null`.
`execution.outcome_verified` means only the stated inspection scope completed.

## JSON evolution

All dev.3 commands emit Schema 1.2; Schema 1.0/1.1 and their fixtures stay frozen.
`target` gains nullable `path` (the supplied path, not a canonical identity).
PE success carries `scope: headers_sections_directories` and `static_analysis`
evidence explicitly excluding loading, directory contents and signature checks.
`findings` stays empty until Doctor. ImageBase and RVA/address fields use hex
strings so 64-bit values survive JavaScript parsing; sizes/counts are integers.

## Verification

Tests cover PE32/PE32+, all truncated prefixes, bad offsets/counts/signatures,
alignment and range overflow, raw/virtual overlaps, BSS and eight-byte names,
certificate offsets, virtual-only directory ranges, deterministic timeout,
and a bounded seeded mutation corpus. Real built DLL/EXE files exercise CLI
JSON/human output, Unicode paths, missing/locked files, size limits and no
overwrite. The synthetic fixtures are inert data; inspection never executes them.

Specification checked 2026-10-23:
[Microsoft PE/COFF](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
