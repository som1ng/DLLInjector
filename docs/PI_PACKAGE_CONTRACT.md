# Pi native package contract

Date: 2026-10-25. Scope: a local-installable Pi package for the existing read-only
Windows DLL Doctor Skill. This implements the Pi packaging part of P4, not the
cross-Agent installer or a public npm release.

- Source: `integrations/skills/windows-dll-doctor` remains authoritative.
  `integrations/pi` generates byte-identical Skill files into an ignored directory.
  No separately maintained Pi Skill, extension code, MCP dependency or binary copy.
- Package: proposed name `@som1ng/dll-doctor-pi`, version `0.1.0-dev.1`, private,
  `UNLICENSED`, keyword `pi-package`, explicit `pi.skills` path inside the package.
  Only package metadata, README and the four canonical Skill files enter a pack.
- Build: no dependency installation or network access. Re-running updates only
  the declared generated files; never recursively removes user files. Generated
  output and `.tgz` archives are ignored. Fail on unsafe linked output paths.
- Install: use Pi's native local-path install, including project `--local` and
  user scope. Local package paths are references, not copied installations;
  the package directory must remain available. No system PATH or privilege changes.
- CLI prerequisite: the user supplies a trusted compatible Windows CLI, normally
  through `DLLINJECTOR_CLI`. The package does not download, bundle or silently
  choose an executable. The Skill checks actual version/help as already specified.
- Trust: native Pi project-trust rules remain applicable. Package registration
  does not imply trusted loading, model invocation or successful diagnostics.
- Removal: Pi removes only this package declaration. Its local source directory,
  CLI, user reports and unrelated settings/packages remain intact.

## Acceptance

1. Validate manifest and archive contents against the canonical source, including
   relative Markdown references, Unicode/space paths, repeatable build, and
   rejection of output links. No generated artifacts tracked by Git.
2. With an explicitly selected installed Pi version, use its actual CLI to install
   the standalone packaged directory in isolated user and project settings. Check
   repeat installation has one declaration and unrelated fields/packages survive.
3. Use the installed Pi resource loader to verify the exact packaged Skill path,
   metadata and prompt advertisement. Test trusted/untrusted project loading,
   enabled/filtered-out packages, and absence after uninstall.
4. Exercise both the source build directory and the extracted npm pack outside the
   repository. No model turn or API call; no user's real Pi settings are modified.
5. Keep real Pi checks opt-in; CI validates package structure/build/archive without
   downloading an Agent client. Existing Skill command tests continue to validate
   the same canonical content against the native CLI.

References: [Pi packages](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/packages.md)
and the installed Pi 1.4.0 package documentation/help. Public npm availability and
project licensing must be resolved before publishing; local checks do not claim
that an `npm:` install command already works.
