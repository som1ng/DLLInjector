# Local read-only MCP contract

Status: P3 implementation contract, 2026-10-25. The private package lives in
`integrations/mcp`; it is not published to npm. Node.js 22+ and Windows are required.
The adapter uses the [official TypeScript SDK v2](https://ts.sdk.modelcontextprotocol.io/v2/serving/stdio)
over stdio. It opens no listening port and makes no model or network requests.

## Startup and permissions

`node dist/index.js --cli <absolute native CLI.exe> [--allow-root <directory>]...
[--allow-processes | --allow-pid <PID>...]`

The CLI is explicitly trusted executable code. Startup requires tool version
`2.0.0-dev.9`, schema `1.6`, and a successful JSON version response. No automatic
download, elevation, shell command, injection or write tool is exposed.
File tools require at least one explicit allowed root. Process listing requires
`--allow-processes`; process details/modules require that switch or a matching PID.
PID permissions are session-local and do not protect against OS PID reuse.

Paths must be absolute local Windows drive paths. Reject UNC/device paths, ADS,
control characters and wildcards. Resolve roots, files and dependency directories
through `realpath`, then check containment and type. Limit PE inputs to 256 MiB
and saved reports to 16 MiB. This is an access policy, not an OS sandbox: another
local writer can change files between validation and the CLI opening them.
Filesystem metadata calls cannot be interrupted by the CLI deadline. Avoid roots
on network-mapped drives; the native CLI rejects unsupported file locations.

## Tools

All inputs are strict objects. Common optional fields are `timeout_ms` (integer
0–30000, default 5000) and `max_output_bytes` (1024–4194304, default 1048576).
Zero timeout retains the native immediate-timeout behavior.

| Tool | Required inputs | Optional inputs | Native command |
| --- | --- | --- | --- |
| `inspect_pe` | `path` | common | `pe inspect` |
| `analyze_dependencies` | `path` | `search_directories` (at most 16), common | `doctor` |
| `list_processes` | none | common | `process list` |
| `inspect_process` | `pid` (1–4294967295) | common | `process inspect` |
| `list_modules` | `pid` | common | `process modules` |
| `diagnose_dll` | `path` | `target_arch` (`x86`/`x64`), `search_directories`, common | `doctor` |
| `read_report` | `path` | common | `report export --json` |

`analyze_dependencies` returns the full Doctor report, including PE findings and
dependency limitations. It searches only explicitly supplied directories; a
candidate is not proof of loader resolution. All commands receive `--json` and
`--timeout-ms`; arguments are arrays passed with `shell: false`.

## Results and failure semantics

Both `structuredContent` and the text content contain the same envelope:

```json
{"source":"live_cli","cli_exit_code":0,"report":{},"error":null}
```

`source` is `live_cli`, `saved_report`, or `adapter`. Native reports retain every
field, execution flag and error; adapter failures use `report: null`,
`cli_exit_code: null` and `error: {code, message}`. No invented Win32 code.
Live native error reports set MCP `isError`; completed Doctor diagnoses with
blocking findings preserve exit 1 without treating the completed tool as broken.
Successful saved-report reads have exit 0 even if the snapshot records a failure.
They are supplied snapshots, never fresh observations or proof of success.

Native `report export --json` validates the saved report using the same strict
offline reader as Markdown/HTML export, then emits the original JSON. It supports
schemas 1.0–1.6 and never revisits the recorded targets. JSON and document formats
remain mutually exclusive. Export errors use the live `report.export` envelope.

The adapter validates responses using bundled canonical schemas. JavaScript
cannot transport every native uint64 exactly: reject any integer outside its
safe range with `UNSAFE_INTEGER`, rather than silently round saved evidence.
Use native report export for such snapshots.

## Resource limits and audit

At most two active calls; excess calls fail with `BUSY` without queuing. Enforce
the native timeout plus a 1000 ms process deadline, including startup; cancellation,
output overflow and timeout kill and reap the direct child. The trusted native
CLI does not launch subprocesses. Bound stdout by the requested limit and stderr
by 64 KiB. Reject truncated, invalid UTF-8/JSON, incompatible schema/command,
abnormal exits and inconsistent success envelopes. Never return partial output.
Audit one JSON record per tool execution on stderr (tool, duration, exit/error
code), without report contents or paths. Stdout is exclusively MCP protocol.

## Acceptance fixtures

- Official SDK stdio client: initialize, enumerate exactly seven read-only tools,
  invoke every tool against native fixtures, compare complete native reports
  excluding observation duration, close cleanly.
- Unicode/spaces, missing paths, disallowed roots/PIDs, junction escape, strict
  arguments, malformed saved JSON, historical snapshots and unsafe integers.
- Controlled subprocesses: launch failure, timeout, cancellation, oversized
  output/stderr, crash, invalid JSON, and subsequent-call recovery.
- Native regression: JSON export preserves historical/current failure snapshots,
  handles output protection and rejects invalid inputs with versioned JSON.
- CI runs native tests on the existing matrix and MCP tests on x64 Release.
  Inspector and real Agent-host checks must be recorded separately; protocol
  tests alone do not establish every host's compatibility.
