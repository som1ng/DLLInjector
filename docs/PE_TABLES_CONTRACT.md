# PE import/export tables — schema 1.3

This records dev.4. Current dev.6 emits Schema 1.5 with unchanged PE table data;
the separate `doctor` command is described in [Doctor contract](DOCTOR_CONTRACT.md).

Development version: `2.0.0-dev.4`. Existing `pe inspect <path>` now inspects
headers, sections, directory ranges, imports, exports and delay imports. The
scope is `headers_sections_imports_exports_delay_imports`. Other directory
contents, dependency search, signature verification and loadability remain
outside scope. Nothing from the inspected file is loaded or executed.

## Interpretation

- `imports` and `delay_imports` contain DLL names and ordered symbols. Each
  symbol has either a name with hint, or an ordinal with name/hint null.
- Normal imports prefer OriginalFirstThunk. With no original lookup table,
  FirstThunk is accepted only when TimeDateStamp is zero (unbound IAT).
  A bound IAT without its original table is unsupported, not decoded as names.
- Each descriptor array must terminate with an all-zero descriptor within its
  directory size; each lookup table must terminate in file-backed bytes.
  IAT storage is checked for the corresponding number of entries plus terminator;
  its pointer values are not resolved or treated as runtime observations.
- Modern RVA-based delay descriptors are supported. Legacy VA-based descriptors
  and unknown attribute flags return unsupported. Missing INT cannot fall back
  to a delay IAT containing loader thunks. Optional bound/unload table storage
  is checked; the module-handle slot only needs an image range, allowing BSS.
- `exports` is null when the directory is absent; otherwise it includes DLL name,
  ordinal base, address-table entry count and symbols. Zero EAT slots are omitted,
  named aliases share a symbol, ordinal-only exports have an empty names array.
  Forwarder strings remain literal references; the inspector never follows them.
- Forwarder strings must terminate inside the export-directory range. Export
  name ordinals index the EAT and are checked before use; biased ordinals cannot
  overflow. Normal export RVAs must fit the image, but are not executed.
- All consumed metadata must have file backing. RVA reads may cross adjacent
  virtual regions even when their raw file locations are nonadjacent. Gaps and
  zero-filled tails cannot supply descriptor/thunk/string bytes.

## Limits and errors

Existing 256 MiB / 96-section / 16-directory limits remain. A parse permits at
most 1,024 combined import/delay libraries, 65,536 combined import symbols,
export slots and aliases, and 1 MiB total name/forwarder text. Strings are nonempty
printable ASCII, at most 4,096 bytes excluding NUL. Other encodings/control bytes
and resource-limit excesses return unsupported; no silent truncation is used.

Malformed tables return `malformed_pe` / exit 1 at `pe.imports`,
`pe.delay_imports` or `pe.exports`; unsupported cases return exit 4. Errors have
null fabricated OS codes. Deadline expiry returns exit 5. Failures discard the
whole result instead of returning a partial success. Bound import metadata,
API sets, Known DLLs, SxS and runtime dynamic loading are not resolved.

## JSON and compatibility

All dev.4 commands emit Schema 1.3; historical schemas and fixtures stay frozen.
PE success adds required imports/delay_imports/exports fields. Empty tables are
explicit; findings remain empty until Doctor. Static-analysis evidence explains
that reported names do not establish dependency availability or DLL loadability.
No new CLI flag or separate parser path is introduced.

## Verification

Test PE32 and PE32+ names/ordinals, lookup fallback, bound-IAT rejection, exports
with aliases/holes/forwarders, delay attributes, invalid pointers/counts/indexes,
unterminated data, file-mapping boundaries, output limits and deterministic
timeout. Exercise a seeded mutation corpus under ASan. Verify a real compiled
DLL's imports and export; on MSVC also inspect a compiled delay-load fixture
without running it. Preserve existing process/module regressions.

Reference checked 2026-10-23:
[Microsoft PE/COFF import, export and delay-load tables](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format).
