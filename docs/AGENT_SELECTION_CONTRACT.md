# Installer client detection and selection contract

Date: 2026-10-26. Source installer `0.1.0-dev.4`; Windows only.

- `list-agents` is read-only and dependency-free. Inspect absolute local PATH entries,
  user `.local/bin` and `%APPDATA%/npm`. Ignore empty, relative and UNC PATH entries;
  do not search the current directory implicitly or recursively scan installations.
- Report native EXE candidates for Codex, Claude Code and OpenCode, without running
  them, interpreting shell shims, importing client code, reading user configuration
  or contacting the network. File existence is not trust or version verification.
- A direct `<agent>.exe` wins within each inspected directory. If absent, a `.cmd`
  or `.ps1` launcher permits checking fixed known npm binary locations. Resolve
  candidate paths and deduplicate them case-insensitively; keep distinct installs.
  Report filesystem access errors as warnings instead of claiming a complete scan.
- The current layouts are Codex's nested/hoisted Windows platform package or vendor
  fallback, and Claude/OpenCode's package `bin/*.exe`. Unsupported layouts require
  the existing explicit agent/client path. Pi keeps its native package flow.
- `setup --agent auto` selects exactly one candidate. Display all candidates in an
  interactive terminal and require a numbered choice even when only one exists.
  Empty/invalid input cancels before any installer execution or writes.
- Noninteractive use requires `--select codex|claude|opencode`; scripts should first
  inspect `list-agents`. More than one path for the name requires an exact `--client`.
  No candidate, unavailable selection and ambiguous selection fail with a detection
  report; never fall back silently. Piped input does not count as terminal confirmation.
- `--dry-run` never prompts or starts executables. Without a selection it returns
  candidates and `selection_required`; with a selection it uses the existing preview.
- Dispatch to the existing Skill or project MCP installer after selection. Preserve
  their scopes, runtime/manifest validation, repeat/conflict handling and rollback.
  MCP receives the selected native EXE and performs its existing version check.
  Skill installation still does not start the client. `agent_detection` describes
  the selection phase, separately from host discovery of an installed Skill/MCP.
- Verify/uninstall require explicit agent names and do not redetect the target.
  No persistent selection file, PATH modification, elevation, download or publication.
  Batch `all`, general package-manager discovery, operating-system architecture
  detection and new-machine acceptance remain outside this slice.

Validation: filesystem/shim fixtures without native execution, duplicate/ambiguous
paths, single/multiple/no candidates, cancellation, noninteractive rejection,
selected previews, existing installation behavior, actual terminal selection and
actual client host checks with the new auto path. Client layouts were inspected in
the locally installed packages used for those checks; unsupported versions/layouts
are not claimed as verified.
