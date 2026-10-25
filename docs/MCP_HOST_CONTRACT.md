# MCP host validation contract

Date: 2026-10-25. Scope: installed Windows Codex, Claude Code and OpenCode hosts
connecting to the existing local DLL Doctor stdio server. This complements the
SDK protocol tests; it does not replace them or evaluate model reasoning.

## Evidence levels

Record each host version and distinguish configuration parsing, connection,
seven-tool discovery, and actual tool execution. A healthy server listing alone
does not establish tool execution. An unsupported interface must fail or be
reported as unverified, never silently substituted with a direct SDK invocation.

Prefer host APIs that call tools without inference. Where a host requires a
model turn, a deterministic loopback-only model fixture may request a known tool
and collect its result. Such checks test the real host's MCP execution path,
not a real model's choice or accuracy. Never call a paid model endpoint or use
real API credentials for these checks. No client installation or user settings
changes are part of the check.

## Inputs and isolation

Accept explicit installed native executable paths, Node, the built MCP entry,
and same-version native CLI/fixtures. Use temporary Unicode/space paths and
per-run configuration. Do not repurpose HOME or CODEX_HOME, overwrite user
configuration, or use an existing Agent conversation. Temporary protocol
contexts must be ephemeral and must not send model turns to real providers.
Client process logs may contain unrelated configuration: emit only bounded,
sanitized result summaries, not full logs, prompts, environment or credentials.

Bound startup, protocol waits and process cleanup. Close only children created
by the check. Do not terminate a user's independently running Agent session.
No analysis report or generated client configuration enters Git.

## Acceptance

- Verify discovery contains exactly the seven advertised tools from our named
  server, with expected input schemas and read-only annotations where exposed.
- Invoke a PE inspection and saved-report read through each supported host path;
  compare the report to direct native output, excluding observation duration for
  live queries and preserving the entire supplied snapshot for saved reports.
- Verify an adapter-policy error and successful subsequent call to avoid treating
  startup/discovery alone as success.
- Reject missing executables, unavailable tools, wrong server identity, malformed
  responses, protocol errors and timeout. Write summaries only to new files.
- Keep installed-host checks opt-in. CI runs focused fixtures that reject false
  passes and test local protocol handling without downloading Agent clients.
- Document tested versions and configuration examples. Mark real-model behavior,
  untested host paths and Inspector GUI checks separately.

Sources: [Codex app-server](https://learn.chatgpt.com/docs/app-server),
[Codex MCP](https://learn.chatgpt.com/docs/extend/mcp),
[Claude Code MCP](https://code.claude.com/docs/en/mcp),
[OpenCode MCP](https://opencode.ai/docs/mcp-servers/), and the installed clients'
help/schema output. Client interfaces are version-sensitive.
