import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash, randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
import { fileURLToPath } from 'node:url';
import { verifyDistribution } from './distribution.mjs';

export const VERSION = '0.1.0-dev.6';
export const NAME = 'windows-dll-doctor';
export const FILES = ['SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml'];
const BINDING = 'references/installation.json';
const RECEIPT = '.dll-doctor-install.json';
const OWNED = [...FILES, BINDING, RECEIPT];
const SOURCE = fileURLToPath(new URL('../../skills/windows-dll-doctor/', import.meta.url));
const execute = promisify(execFile);
const digest = data => createHash('sha256').update(data).digest('hex');
const json = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');

export class InstallError extends Error {
  constructor(code, message) { super(message); this.code = code; }
}
function fail(code, message) { throw new InstallError(code, message); }
export async function stat(file) {
  try { return await fs.lstat(file); }
  catch (error) { if (error.code === 'ENOENT') return undefined; throw error; }
}

// Check existing components before any writes; do not follow junctions into user data.
export async function safePath(file) {
  const absolute = path.resolve(file);
  let current = path.parse(absolute).root;
  for (const part of absolute.slice(current.length).split(path.sep).filter(Boolean)) {
    current = path.join(current, part);
    const entry = await stat(current);
    if (entry?.isSymbolicLink()) fail('LINKED_PATH', 'Linked installation paths are unsupported: ' + current);
    if (entry && current !== absolute && !entry.isDirectory()) fail('PATH_CONFLICT', 'A parent path is not a directory.');
  }
  return absolute;
}

export function destination(options, home = os.homedir(), env = process.env) {
  if (!['codex', 'claude', 'opencode'].includes(options.agent)) fail('ARGUMENT', 'Choose --agent codex, claude or opencode; Pi uses its native package.');
  if (!['user', 'project'].includes(options.scope)) fail('ARGUMENT', 'Choose --scope user or project.');
  if (options.scope === 'user' && options.project) fail('ARGUMENT', '--project is only valid for project scope.');
  if (options.agent === 'claude' && options.scope === 'user' && env.CLAUDE_CONFIG_DIR) {
    fail('CUSTOM_CONFIG', 'Custom CLAUDE_CONFIG_DIR is not supported for user scope; use project scope.');
  }
  const root = path.resolve(options.scope === 'user' ? home : options.project || process.cwd());
  return { root, target: path.join(root, options.agent === 'claude' ? '.claude' : '.agents', 'skills', NAME) };
}

export async function cliPath(file) {
  if (!file || !path.isAbsolute(file) || path.extname(file).toLowerCase() !== '.exe' || file.startsWith('\\\\')) {
    fail('CLI_PATH', 'Provide an absolute local .exe path with --cli.');
  }
  const resolved = await fs.realpath(file);
  if (!(await fs.stat(resolved)).isFile()) fail('CLI_PATH', 'CLI path must be a regular file.');
  return resolved;
}

export async function probeCli(file) {
  let data;
  try {
    const result = await execute(file, ['--version', '--json'], {
      windowsHide: true, timeout: 10000, maxBuffer: 1024 * 1024, encoding: 'utf8',
    });
    data = JSON.parse(result.stdout);
  } catch { fail('CLI_PROBE', 'CLI version probe failed, timed out or returned invalid JSON.'); }
  if (!data || data.command !== 'version' || data.status !== 'success' || data.schema_version !== '1.6'
      || !['2.0.0-dev.8', '2.0.0-dev.9'].includes(data.tool_version)
      || data.data?.text !== data.tool_version || data.execution?.operation_completed !== true) {
    fail('CLI_VERSION', 'Expected tested DLLInjector CLI dev.8/dev.9 with Schema 1.6.');
  }
  return { path: file, tool_version: data.tool_version, schema_version: data.schema_version,
    sha256: digest(await fs.readFile(file)) };
}

async function snapshot(target) {
  const tree = {};
  async function walk(directory, prefix = '') {
    for (const entry of await fs.readdir(directory, { withFileTypes: true })) {
      const relative = prefix + entry.name;
      const file = path.join(directory, entry.name);
      if (entry.isSymbolicLink()) fail('CONFLICT', 'Installation contains linked content; nothing was removed.');
      if (entry.isDirectory() && ['references', 'agents'].includes(relative)) await walk(file, relative + '/');
      else if (entry.isFile() && OWNED.includes(relative)) tree[relative] = await fs.readFile(file);
      else fail('CONFLICT', 'Installation contains extra content; nothing was removed.');
    }
  }
  await walk(target);
  if (Object.keys(tree).length !== OWNED.length) fail('CONFLICT', 'Installation is incomplete or unmanaged.');
  let receipt;
  try { receipt = JSON.parse(tree[RECEIPT]); }
  catch { fail('CONFLICT', 'Installation receipt is invalid.'); }
  if (!receipt || receipt.owner !== 'dll-doctor-installer' || receipt.format !== 1
      || Object.keys(receipt.files || {}).sort().join('|') !== [...FILES, BINDING].sort().join('|')) {
    fail('CONFLICT', 'Installation receipt is invalid or unsupported.');
  }
  for (const file of [...FILES, BINDING]) {
    if (digest(tree[file]) !== receipt.files[file]) fail('CONFLICT', 'Installed files were modified; preserve them before uninstalling.');
  }
  return { tree, receipt };
}

async function expected(cli) {
  const tree = {};
  for (const file of FILES) {
    const input = path.join(SOURCE, file);
    await safePath(input);
    if (!(await fs.lstat(input)).isFile()) fail('SOURCE', 'Canonical Skill file is missing.');
    tree[file] = await fs.readFile(input);
  }
  tree[BINDING] = json({ format: 1, cli });
  tree[RECEIPT] = json({ owner: 'dll-doctor-installer', format: 1, installer_version: VERSION,
    files: Object.fromEntries(Object.entries(tree).map(([file, data]) => [file, digest(data)])) });
  return tree;
}

async function verify(target, probe) {
  const { tree } = await snapshot(target);
  let binding;
  try { binding = JSON.parse(tree[BINDING]); }
  catch { fail('CONFLICT', 'Installed CLI binding is invalid.'); }
  if (!binding || binding.format !== 1 || !binding.cli) fail('CONFLICT', 'Installed CLI binding is invalid.');
  const cli = binding.cli;
  const executable = await cliPath(cli?.path);
  if (digest(await fs.readFile(executable)) !== cli.sha256) {
    fail('CLI_CHANGED', 'Installed CLI bytes changed; review the binary and reinstall explicitly.');
  }
  const observed = await probe(executable);
  if (observed.tool_version !== cli.tool_version || observed.schema_version !== cli.schema_version
      || observed.sha256 !== cli.sha256) fail('CLI_CHANGED', 'CLI verification differs from installation.');
  return cli;
}

export async function makeParents(directory, made) {
  if (await stat(directory)) return;
  await makeParents(path.dirname(directory), made);
  try { await fs.mkdir(directory); made.push(directory); }
  catch (error) { if (error.code !== 'EEXIST') throw error; }
}

// Delete only explicit files and empty directories, never a recursive remove.
async function removeTree(directory, tree, checkpoint = async () => {}) {
  await safePath(directory);
  for (const [file, data] of Object.entries(tree)) {
    const full = path.join(directory, file);
    await safePath(full);
    if (!Buffer.from(await fs.readFile(full)).equals(data)) fail('CONCURRENT_EDIT', 'Content changed during removal; preserving it.');
    await fs.unlink(full);
    await checkpoint('removed-file');
  }
  for (const part of ['references', 'agents', '']) await fs.rmdir(path.join(directory, part));
}

async function restore(directory, tree) {
  await safePath(directory);
  await fs.mkdir(directory, { recursive: true });
  for (const [file, data] of Object.entries(tree)) {
    const full = path.join(directory, file);
    await safePath(full);
    await fs.mkdir(path.dirname(full), { recursive: true });
    if (await stat(full)) {
      if (!Buffer.from(await fs.readFile(full)).equals(data)) fail('CONCURRENT_EDIT', 'Rollback preserved concurrently edited content.');
    } else await fs.writeFile(full, data, { flag: 'wx' });
  }
}

export async function install(command, options, context = {}) {
  if (!['setup', 'verify', 'uninstall'].includes(command)) fail('ARGUMENT', 'Unknown command.');
  if (command !== 'setup' && options.manifest !== undefined) fail('ARGUMENT', '--manifest is only valid for setup.');
  const { root, target } = destination(options, context.home, context.env);
  await safePath(target);
  if (!(await stat(root))?.isDirectory()) fail('ROOT', 'The project/user root must already exist.');
  const parent = path.dirname(target);
  const probe = context.probe || probeCli;
  const checkpoint = context.checkpoint || (async () => {});
  let executable, distribution;
  if (command === 'setup') {
    executable = await cliPath(options.cli);
    if (options.manifest !== undefined) {
      try { distribution = await verifyDistribution(executable, options.manifest); }
      catch (error) {
        if (error.code === 'DISTRIBUTION_INVALID') fail(error.code, error.message);
        throw error;
      }
    }
  }
  const existing = await stat(target);
  if (existing && !existing.isDirectory()) fail('CONFLICT', 'Skill destination is not a directory.');
  if (existing) await snapshot(target);
  if (command === 'verify' && !existing) fail('NOT_INSTALLED', 'No managed installation exists.');
  const result = { status: 'success', command, agent: options.agent, scope: options.scope, target,
    shared_with: options.agent === 'claude' ? [] : ['codex', 'opencode'],
    files: OWNED.map(file => path.join(target, file)), client_discovery: 'not_run',
    distribution_checksum: distribution ? 'verified' : 'not_supplied' };
  if (options.dryRun) return { ...result, action: 'preview', cli_probe: 'not_run', exists: !!existing };
  if (command === 'verify') return { ...result, action: 'verified', cli: await verify(target, probe) };
  if (command === 'uninstall' && !existing) return { ...result, action: 'absent' };

  const made = [];
  let lock, stage, committed = false, tree, removed;
  const lockPath = path.join(parent, '.dll-doctor-installer.lock');
  try {
    await makeParents(parent, made);
    await safePath(parent);
    try { lock = await fs.open(lockPath, 'wx'); }
    catch (error) { if (error.code === 'EEXIST') fail('LOCKED', 'Another installer or an interrupted operation owns the lock: ' + lockPath); throw error; }
    await safePath(target);
    if (command === 'setup') {
      const cli = await probe(executable);
      if (distribution) {
        if (cli.tool_version !== distribution.tool_version || cli.schema_version !== distribution.schema_version
            || cli.sha256 !== distribution.sha256) fail('DISTRIBUTION_MISMATCH', 'CLI probe differs from the verified distribution.');
        cli.distribution = distribution;
      }
      tree = await expected(cli);
      if (await stat(target)) {
        const current = await snapshot(target);
        // Receipt format/ownership and hashes were checked by snapshot. An
        // installer version change alone does not change the installed content.
        if (![...FILES, BINDING].every(file => current.tree[file].equals(tree[file]))) {
          fail('CONFLICT', 'A different managed version or CLI binding exists; uninstall explicitly before setup.');
        }
        if (options.verify) await verify(target, probe);
        return { ...result, action: 'unchanged' };
      }
      const stagingPath = path.join(parent, '.dll-doctor-stage-' + randomUUID());
      await fs.mkdir(stagingPath);
      stage = stagingPath;
      await fs.mkdir(path.join(stage, 'references'));
      await fs.mkdir(path.join(stage, 'agents'));
      for (const [file, data] of Object.entries(tree)) await fs.writeFile(path.join(stage, file), data, { flag: 'wx' });
      await checkpoint('staged');
      // Windows directory rename refuses an existing destination.
      if (await stat(target)) fail('CONFLICT', 'Skill destination appeared during setup.');
      await fs.rename(stage, target);
      stage = undefined;
      committed = true;
      await checkpoint('committed', { target, receipt: Buffer.from(tree[RECEIPT]) });
      await snapshot(target);
      if (options.verify) await verify(target, probe);
      return { ...result, action: 'installed' };
    }
    const current = await snapshot(target);
    if (context.expectedReceipt && !current.tree[RECEIPT].equals(context.expectedReceipt)) {
      fail('CONCURRENT_EDIT', 'Installation changed since this batch created it; preserving the replacement.');
    }
    removed = current.tree;
    await removeTree(target, removed, checkpoint);
    return { ...result, action: 'removed' };
  } catch (error) {
    try {
      if (removed) await restore(target, removed);
      if (committed) await removeTree(target, tree);
      if (stage) {
        // A failed write may have left only a subset of our private stage files.
        const partial = {};
        for (const file of OWNED) {
          const full = path.join(stage, file);
          if (await stat(full)) partial[file] = await fs.readFile(full);
        }
        for (const file of Object.keys(partial)) { await safePath(path.join(stage, file)); await fs.unlink(path.join(stage, file)); }
        for (const part of ['references', 'agents', '']) {
          const directory = path.join(stage, part);
          if (await stat(directory)) await fs.rmdir(directory);
        }
      }
    } catch { fail('ROLLBACK_INCOMPLETE', 'Operation failed and rollback could not complete; preserve and inspect: ' + target + (stage ? ' ; ' + stage : '')); }
    throw error;
  } finally {
    if (lock) { await lock.close(); await fs.unlink(lockPath); }
    for (const directory of made.reverse()) {
      try { await fs.rmdir(directory); }
      catch (error) { if (!['ENOTEMPTY', 'EEXIST', 'ENOENT'].includes(error.code)) throw error; }
    }
  }
}
