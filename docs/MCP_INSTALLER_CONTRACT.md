# Project MCP installer contract

Date: 2026-10-26. P4 slice: `--integration mcp` in the source installer, for project
configuration in Codex 0.164.0, Claude Code 2.1.310 and OpenCode 1.19.4 (V1).
Skill mode remains the default. User-level MCP, auto/all selection, client/server
downloads, trust/approval changes and Pi MCP are outside this slice.

- Explicit inputs: project, agent, installed native client EXE, trusted CLI,
  built local MCP entry and at least one allowed directory. Process queries remain
  disabled unless `--allow-processes` is explicitly set; no persistent PID grants.
- Check installed client version, CLI dev.9/Schema 1.6 and MCP initialization/tool
  catalog before changing configuration. Optional CLI `--manifest` is checked first.
  `--dry-run` only validates paths/configuration and previews the managed entry;
  it neither starts executables nor writes files.
- Targets: `.codex/config.toml`, `.mcp.json`, or existing `opencode.jsonc` /
  `opencode.json` (create the latter if absent). Reject two OpenCode config files,
  malformed/duplicate configuration keys, links and incompatible container shapes.
- Manage only `dll_doctor`. Refuse an existing unowned name even if identical.
  JSON/JSONC edits preserve unrelated fields/comments; TOML appends a marked table
  and validates the complete result. TOML layouts that cannot accept the appended
  table are rejected without rewriting the user's file.
  Refuse a JSONC edit when the parser's edit would discard an adjacent comment.
- A local receipt stores only our entry, paths, versions/hashes and ownership
  metadata, never a copy of unrelated configuration or credentials. A sibling lock
  serializes this installer's changes. Logs/previews never include unrelated settings.
- Repeated identical setup is unchanged. Changed paths, permissions, binary bytes,
  managed values or managed-entry text are conflicts; uninstall/reinstall explicitly.
- Uninstall removes only our verified entry and receipt; preserve unrelated edits
  made after setup. Remove a newly created config only if nothing else remains.
  Missing receipt plus existing entry is not ownership and cannot authorize deletion.
- Keep the original configuration/receipt in memory while changing files; atomic
  same-directory replacement and caught-error rollback compare current bytes before
  restoration, preserving concurrent edits. Report incomplete rollback and keep the
  local state for inspection. Forced termination/whole-machine recovery is not promised.
- `verify` checks recorded binary paths/hashes, supported client/CLI version and direct
  MCP handshake/tool catalog. It is not a claim that a host has trusted/discovered the
  project configuration or that a model has selected a tool. Never modify trust or
  permission settings to bypass a host's normal approval process.
- Test real filesystem preservation, JSONC/TOML handling, ownership conflicts,
  corruption, repeat/removal, preflight failures, transaction faults and concurrent
  edits. Verify generated settings with installed clients using isolated fixtures;
  distinguish explicit configuration loading from automatic project discovery.

Sources: [Codex MCP](https://learn.chatgpt.com/docs/extend/mcp),
[Codex project config](https://learn.chatgpt.com/docs/config-file/config-basic),
[Claude MCP](https://code.claude.com/docs/en/mcp),
[OpenCode MCP](https://opencode.ai/docs/mcp-servers/),
[OpenCode config](https://opencode.ai/docs/config/).
