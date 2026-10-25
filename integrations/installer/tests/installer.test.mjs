import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { install, destination, NAME } from '../src/installer.mjs';

const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');

async function fixture(t) {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dll-installer-test-'));
  const root = path.join(temporary, '中文 project space 🚀');
  await fs.mkdir(root);
  const cli = path.join(temporary, 'trusted cli.exe');
  await fs.writeFile(cli, 'local test fixture, never executed');
  const options = { agent: 'codex', scope: 'project', project: root, cli };
  let probes = 0;
  const context = { home: root, env: {}, probe: async file => {
    probes++;
    return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) };
  } };
  t.after(async () => {
    // Only remove this test's resolved temporary workspace, never a computed user path.
    const resolved = await fs.realpath(temporary);
    const tempRoot = await fs.realpath(os.tmpdir());
    assert.equal(path.dirname(resolved), tempRoot);
    assert.ok(path.basename(resolved).startsWith('dll-installer-test-'));
    await fs.rm(resolved, { recursive: true });
  });
  return { root, cli, options, context, probes: () => probes };
}

async function contents(directory) {
  const result = {};
  async function walk(base, relative = '') {
    for (const entry of await fs.readdir(base, { withFileTypes: true })) {
      const name = relative + entry.name;
      if (entry.isDirectory()) await walk(path.join(base, entry.name), name + '/');
      else result[name] = hash(await fs.readFile(path.join(base, entry.name)));
    }
  }
  await walk(directory);
  return result;
}

for (const agent of ['codex', 'claude', 'opencode']) {
  for (const scope of ['project', 'user']) {
    test(agent + ' ' + scope + ': preview, setup, repeat, verify and uninstall preserve other files', async t => {
      const f = await fixture(t);
      const options = { ...f.options, agent, scope, project: scope === 'project' ? f.root : undefined };
      const clientFolder = path.join(f.root, agent === 'claude' ? '.claude' : '.agents');
      await fs.mkdir(path.join(clientFolder, 'skills/other'), { recursive: true });
      await fs.writeFile(path.join(clientFolder, 'settings.json'), '{ "custom": true }\n');
      await fs.writeFile(path.join(clientFolder, 'skills/other/SKILL.md'), 'user content');
      const before = await contents(f.root);
      assert.equal((await install('setup', { ...options, dryRun: true }, f.context)).action, 'preview');
      assert.deepEqual(await contents(f.root), before);
      assert.equal(f.probes(), 0);
      const installed = await install('setup', { ...options, verify: true }, f.context);
      assert.equal(installed.action, 'installed');
      const first = await contents(f.root);
      assert.equal((await install('setup', options, f.context)).action, 'unchanged');
      assert.deepEqual(await contents(f.root), first);
      assert.equal((await install('verify', options, f.context)).action, 'verified');
      const probes = f.probes();
      await install('uninstall', { ...options, dryRun: true }, f.context);
      assert.deepEqual(await contents(f.root), first);
      assert.equal((await install('uninstall', options, f.context)).action, 'removed');
      assert.equal(f.probes(), probes);
      assert.deepEqual(await contents(f.root), before);
      assert.equal((await install('uninstall', options, f.context)).action, 'absent');
    });
  }
}

test('Codex/OpenCode use a single shared installation', async t => {
  const f = await fixture(t);
  const first = await install('setup', f.options, f.context);
  const second = await install('setup', { ...f.options, agent: 'opencode' }, f.context);
  assert.equal(first.target, second.target);
  assert.equal(second.action, 'unchanged');
  assert.deepEqual(second.shared_with, ['codex', 'opencode']);
});

test('unmanaged, edited, extra and malformed content are never overwritten or removed', async t => {
  const f = await fixture(t);
  const target = destination(f.options).target;
  await fs.mkdir(target, { recursive: true });
  await fs.writeFile(path.join(target, 'SKILL.md'), 'existing user skill');
  const unmanaged = await contents(f.root);
  await assert.rejects(install('setup', f.options, f.context), { code: 'CONFLICT' });
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'CONFLICT' });
  assert.deepEqual(await contents(f.root), unmanaged);
  await fs.unlink(path.join(target, 'SKILL.md'));
  await fs.rmdir(target);
  await install('setup', f.options, f.context);
  const skill = path.join(target, 'SKILL.md');
  const original = await fs.readFile(skill);
  await fs.appendFile(skill, '\nuser modification');
  const edited = await contents(f.root);
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'CONFLICT' });
  await assert.rejects(install('setup', f.options, f.context), { code: 'CONFLICT' });
  assert.deepEqual(await contents(f.root), edited);
  await fs.writeFile(skill, original);
  const extra = path.join(target, 'personal-note.txt');
  await fs.writeFile(extra, 'keep');
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'CONFLICT' });
  assert.equal(await fs.readFile(extra, 'utf8'), 'keep');
  await fs.unlink(extra);
  await fs.writeFile(path.join(target, '.dll-doctor-install.json'), '{ invalid');
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'CONFLICT' });
});

test('changed CLI binding requires explicit uninstall; missing CLI does not prevent uninstall', async t => {
  const f = await fixture(t);
  await install('setup', f.options, f.context);
  const before = await contents(f.root);
  await fs.writeFile(f.cli, 'different CLI bytes');
  await assert.rejects(install('verify', f.options, f.context), { code: 'CLI_CHANGED' });
  await assert.rejects(install('setup', f.options, f.context), { code: 'CONFLICT' });
  assert.deepEqual(await contents(f.root), before);
  await fs.unlink(f.cli);
  assert.equal((await install('uninstall', f.options, f.context)).action, 'removed');
});

test('pre-commit and post-commit failures roll back newly created directories', async t => {
  const f = await fixture(t);
  for (const stage of ['staged', 'committed']) {
    await assert.rejects(install('setup', f.options, { ...f.context, checkpoint: async current => {
      if (stage === current) throw new Error('injected transaction failure');
    } }), /injected transaction failure/);
    assert.deepEqual(await fs.readdir(f.root), []);
  }
});

test('failed final CLI verification rolls setup back', async t => {
  const f = await fixture(t);
  let count = 0;
  await assert.rejects(install('setup', { ...f.options, verify: true }, { ...f.context, probe: async file => {
    if (++count === 2) throw new Error('injected verification failure');
    return f.context.probe(file);
  } }), /injected verification failure/);
  assert.deepEqual(await fs.readdir(f.root), []);
});

test('partial uninstall failure restores exact bytes', async t => {
  const f = await fixture(t);
  await install('setup', f.options, f.context);
  const before = await contents(f.root);
  let count = 0;
  await assert.rejects(install('uninstall', f.options, { ...f.context, checkpoint: async () => {
    if (++count === 2) throw new Error('injected removal failure');
  } }), /injected removal failure/);
  assert.deepEqual(await contents(f.root), before);
});

test('active lock and linked paths are rejected without modifying their targets', async t => {
  const f = await fixture(t);
  const target = destination(f.options).target;
  const parent = path.dirname(target);
  await fs.mkdir(parent, { recursive: true });
  const lock = path.join(parent, '.dll-doctor-installer.lock');
  await fs.writeFile(lock, 'another operation');
  await assert.rejects(install('setup', f.options, f.context), { code: 'LOCKED' });
  assert.equal(await fs.readFile(lock, 'utf8'), 'another operation');
  await fs.unlink(lock);
  const outside = path.join(f.root, 'unrelated');
  await fs.mkdir(outside);
  await fs.symlink(outside, target, 'junction');
  await assert.rejects(install('setup', f.options, f.context), { code: 'LINKED_PATH' });
  await assert.rejects(install('uninstall', f.options, f.context), { code: 'LINKED_PATH' });
  assert.deepEqual(await fs.readdir(outside), []);
  if (process.platform === 'win32') await fs.rmdir(target); else await fs.unlink(target);
});

test('incomplete rollback preserves a concurrent user edit and reports it', async t => {
  const f = await fixture(t);
  const installed = await install('setup', f.options, f.context);
  const receipt = path.join(installed.target, '.dll-doctor-install.json');
  await assert.rejects(install('uninstall', f.options, { ...f.context, checkpoint: async () => {
    await fs.writeFile(receipt, 'concurrent user edit');
    throw new Error('injected concurrent edit');
  } }), { code: 'ROLLBACK_INCOMPLETE' });
  assert.equal(await fs.readFile(receipt, 'utf8'), 'concurrent user edit');
});

test('invalid selectors and source-only CLI options fail before writes', async t => {
  const f = await fixture(t);
  for (const agent of ['auto', 'all', 'pi']) {
    await assert.rejects(install('setup', { ...f.options, agent }, f.context), { code: 'ARGUMENT' });
  }
  assert.throws(() => destination({ agent: 'claude', scope: 'user' }, f.root, { CLAUDE_CONFIG_DIR: f.root }), { code: 'CUSTOM_CONFIG' });
  assert.throws(() => execFileSync(process.execPath, [entry, 'setup', '--unsupported'], { windowsHide: true, stdio: 'pipe' }));
  assert.deepEqual(await fs.readdir(f.root), []);
});

test('actual executable entry installs, verifies and uninstalls with native CLI', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI,
}, async t => {
  const f = await fixture(t);
  const invoke = command => JSON.parse(execFileSync(process.execPath, [entry, command,
    '--agent', 'codex', '--scope', 'project', '--project', f.root,
    ...(command === 'setup' ? ['--cli', path.resolve(process.env.DLL_INSTALLER_TEST_CLI), '--verify'] : [])],
  { windowsHide: true, encoding: 'utf8' }));
  const result = invoke('setup');
  assert.equal(result.action, 'installed');
  assert.equal(invoke('verify').action, 'verified');
  const binding = JSON.parse(await fs.readFile(path.join(result.target, 'references/installation.json')));
  const report = JSON.parse(execFileSync(binding.cli.path, ['pe', 'inspect', binding.cli.path, '--json'], { windowsHide: true, encoding: 'utf8' }));
  assert.equal(report.status, 'success');
  assert.equal(invoke('uninstall').action, 'removed');
  assert.equal((await fs.readdir(path.join(f.root, '.agents/skills'))).length, 0);
});
