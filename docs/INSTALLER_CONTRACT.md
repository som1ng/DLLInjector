# Local Skill installer contract

For the later explicit upgrade command, see [configuration upgrade](CONFIG_UPGRADE_DELIVERY.md).

Date: 2026-10-25. First P4 installer slice: a source-run Windows installer for
Codex, Claude Code and OpenCode Skills, using an explicitly supplied local CLI.
Pi uses its existing native package installation. No release download, npm
publication, MCP configuration, auto/all selection or client installation yet.

- Require explicit agent and scope (project/user). Project defaults to cwd; user
  uses the current OS home. Codex/OpenCode share `.agents/skills`; Claude uses
  `.claude/skills`. Removing a shared installation affects every client loading it.
  A custom `CLAUDE_CONFIG_DIR` is rejected for user scope in this first version.
- Copy the four canonical Skill files without edits. Add `references/installation.json`
  with the local CLI path/version and an ownership receipt with content hashes.
  Skill uses this optional binding after an explicit user path/environment setting.
  It still checks CLI trust, version and help before diagnostics.
- Require a local absolute `.exe` path. Probe `--version --json` without a shell,
  with timeout/output bounds; accept the tested dev.8/dev.9 Schema 1.6 contract.
  No CLI download, copy, system PATH edits or elevation. Uninstall never runs the CLI.
- `setup --dry-run` and `uninstall --dry-run` perform no writes or executable probes.
  Output JSON includes exact target and file paths. Unsupported options fail before writes.
- Setup uses a private staging directory, then renames into the absent target;
  identical managed content is a no-op. Unmanaged, changed or extra content is a
  conflict; upgrading/rebinding requires explicit uninstall followed by setup.
- A sibling exclusive lock serializes this installer's writes. Reject links in
  managed paths. Never recursively delete directories or replace another Skill.
- Setup failures clean up this operation's files; uninstall failures restore the
  removed files from an in-memory snapshot without overwriting concurrent edits.
  Report incomplete rollback honestly. This is caught-error rollback, not recovery
  from process termination, machine failure, or arbitrary concurrent filesystem edits.
- `verify` / `setup --verify` check receipt hashes, CLI bytes and native version
  response. This is installation/CLI verification, not Agent discovery/model execution.
  Real client discovery is separately tested using existing opt-in host helpers.
- Generated installation bindings contain local paths: keep them local. No user
  settings, tokens, reports, CLI binaries, staging output or logs enter the repository.

Acceptance: real filesystem tests of both scopes and all three targets, Unicode/
space paths, dry-run, repeat install, conflict/user-edit preservation, malformed
receipt, links, lock, verification failure rollback, uninstall failure restoration,
plus native CLI and installed-client project discovery checks. Windows CI runs
the fixture/native CLI checks without downloading Agent clients.

Sources: [Codex](https://learn.chatgpt.com/docs/build-skills),
[Claude Code](https://code.claude.com/docs/en/skills),
[OpenCode](https://opencode.ai/docs/skills/). The installed versions and evidence
are recorded separately in the delivery document.
