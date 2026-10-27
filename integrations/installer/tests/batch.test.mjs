import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { setupAll } from '../src/batch.mjs';
import { install, destination, InstallError } from '../src/installer.mjs';

const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');

async function fixture(t) {
  const temporary = await fs.mkdtemp(path.join(os.tmpdir(), 'dll-batch-test-'));
  const root = path.join(temporary, '批量 project space 🧪');
  await fs.mkdir(root);
  const cli = path.join(temporary, 'fixture cli.exe');
  await fs.writeFile(cli, 'fixture, never executed');
  const options = { agent: 'all', scope: 'project', project: root, cli };
  let probes = 0;
  const context = { home: root, env: {}, probe: async file => {
    probes++;
    return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) };
  } };
  const target = agent => destination({ ...options, agent }).target;
  t.after(async () => {
    const resolved = await fs.realpath(temporary);
    assert.equal(path.dirname(resolved), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(resolved).startsWith('dll-batch-test-'));
    await fs.rm(resolved, { recursive: true });
  });
  return { root, cli, options, context, target, probes: () => probes };
}

async function files(directory) {
  const result = {};
  async function walk(base, prefix = '') {
    for (const item of await fs.readdir(base, { withFileTypes: true })) {
      const name = prefix + item.name, full = path.join(base, item.name);
      if (item.isDirectory()) await walk(full, name + '/');
      else result[name] = hash(await fs.readFile(full));
    }
  }
  await walk(directory);
  return result;
}

for (const scope of ['project', 'user']) {
  test('batch ' + scope + ': preview, deduplication, repeat, verify and explicit removal', async t => {
    const f = await fixture(t);
    const options = { ...f.options, scope, project: scope === 'project' ? f.root : undefined };
    await fs.mkdir(path.join(f.root, '.claude/skills/personal'), { recursive: true });
    await fs.writeFile(path.join(f.root, '.claude/skills/personal/SKILL.md'), 'keep existing skill');
    const before = await files(f.root), directories = await fs.readdir(f.root);
    const preview = await setupAll({ ...options, dryRun: true }, f.context);
    assert.equal(preview.action, 'preview');
    assert.equal(preview.targets.length, 2);
    assert.deepEqual(preview.targets.map(x => x.agents), [['codex', 'opencode'], ['claude']]);
    assert.equal(f.probes(), 0);
    assert.deepEqual(await files(f.root), before);
    assert.deepEqual(await fs.readdir(f.root), directories);
    const installed = await setupAll({ ...options, verify: true }, f.context);
    assert.equal(installed.action, 'installed');
    assert.equal(f.probes(), 4, 'only two installations, each probed and verified');
    assert.ok(installed.targets.every(x => x.action === 'installed'));
    const first = await files(f.root);
    assert.equal((await setupAll(options, f.context)).action, 'unchanged');
    assert.deepEqual(await files(f.root), first);
    for (const agent of ['opencode', 'claude']) {
      assert.equal((await install('verify', { ...options, agent }, f.context)).action, 'verified');
      assert.equal((await install('uninstall', { ...options, agent }, f.context)).action, 'removed');
    }
    assert.deepEqual(await files(f.root), before);
  });
}

test('a conflict in the later destination is found before the first write or CLI probe', async t => {
  const f = await fixture(t);
  await fs.mkdir(f.target('claude'), { recursive: true });
  await fs.writeFile(path.join(f.target('claude'), 'SKILL.md'), 'unmanaged content');
  const before = await files(f.root);
  await assert.rejects(setupAll(f.options, f.context), error => {
    assert.equal(error.code, 'CONFLICT');
    assert.equal(error.details.failed_agent, 'claude');
    assert.deepEqual(error.details.completed_targets, []);
    return true;
  });
  assert.equal(f.probes(), 0);
  assert.deepEqual(await files(f.root), before);
  await assert.rejects(fs.stat(f.target('codex')), { code: 'ENOENT' });
});

test('unsupported user configuration is rejected before any target is written', async t => {
  const f = await fixture(t);
  await assert.rejects(setupAll({ ...f.options, scope: 'user', project: undefined },
    { ...f.context, env: { CLAUDE_CONFIG_DIR: path.join(f.root, 'custom') } }), { code: 'CUSTOM_CONFIG' });
  assert.deepEqual(await fs.readdir(f.root), []);
  assert.equal(f.probes(), 0);
});

for (const stage of ['staged', 'committed']) {
  test('failure at the second ' + stage + ' rolls back both new installations', async t => {
    const f = await fixture(t);
    let hits = 0;
    await assert.rejects(setupAll(f.options, { ...f.context, checkpoint: async current => {
      if (current === stage && ++hits === 2) throw new Error('fixture failure');
    } }), error => {
      assert.equal(error.code, 'BATCH_SETUP_FAILED');
      assert.equal(error.details.rollback_status, 'complete');
      assert.equal(error.details.rollback[0].action, 'removed');
      return true;
    });
    assert.deepEqual(await files(f.root), {});
    for (const agent of ['codex', 'claude']) await assert.rejects(fs.stat(f.target(agent)), { code: 'ENOENT' });
  });
}

test('later verification failure rolls back new targets without extra CLI probes', async t => {
  const f = await fixture(t);
  let probes = 0;
  await assert.rejects(setupAll({ ...f.options, verify: true }, { ...f.context, probe: async file => {
    if (++probes === 4) throw new InstallError('CLI_PROBE', 'fixture verification failure');
    return f.context.probe(file);
  } }), { code: 'CLI_PROBE' });
  assert.equal(probes, 4);
  assert.deepEqual(await files(f.root), {});
});

test('a pre-existing shared installation survives failure of the later target', async t => {
  const f = await fixture(t);
  await install('setup', { ...f.options, agent: 'opencode' }, f.context);
  const before = await files(f.root);
  await assert.rejects(setupAll(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'staged') throw new Error('fixture failure');
  } }), error => {
    assert.equal(error.details.completed_targets[0].action, 'unchanged');
    assert.deepEqual(error.details.rollback, []);
    return true;
  });
  assert.deepEqual(await files(f.root), before);
});

test('an unchanged installation from an earlier installer version is reused without rewriting its receipt', async t => {
  const f = await fixture(t);
  await install('setup', { ...f.options, agent: 'codex' }, f.context);
  const receiptPath = path.join(f.target('codex'), '.dll-doctor-install.json');
  const receipt = JSON.parse(await fs.readFile(receiptPath));
  receipt.installer_version = '0.1.0-dev.4';
  await fs.writeFile(receiptPath, JSON.stringify(receipt, null, 2) + '\n');
  const before = await files(f.target('codex'));
  const result = await setupAll(f.options, f.context);
  assert.deepEqual(result.targets.map(item => item.action), ['unchanged', 'installed']);
  assert.deepEqual(await files(f.target('codex')), before);
});

test('a different CLI binding at the later target preserves it and rolls back the new shared target', async t => {
  const f = await fixture(t);
  await install('setup', { ...f.options, agent: 'claude' }, f.context);
  const before = await files(f.root);
  const other = path.join(path.dirname(f.cli), 'other.exe');
  await fs.writeFile(other, 'another fixture');
  await assert.rejects(setupAll({ ...f.options, cli: other }, f.context), { code: 'CONFLICT' });
  assert.deepEqual(await files(f.root), before);
  await assert.rejects(fs.stat(f.target('codex')), { code: 'ENOENT' });
});

test('concurrent user edits stop batch rollback and remain available for recovery', async t => {
  const f = await fixture(t);
  let stages = 0;
  await assert.rejects(setupAll(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'staged' && ++stages === 2) {
      await fs.appendFile(path.join(f.target('codex'), 'SKILL.md'), '\nkeep user edit');
      throw new Error('fixture failure');
    }
  } }), error => {
    assert.equal(error.code, 'ROLLBACK_INCOMPLETE');
    assert.equal(error.details.rollback_status, 'incomplete');
    assert.equal(error.details.rollback[0].action, 'failed');
    assert.equal(error.details.rollback[0].target, f.target('codex'));
    return true;
  });
  assert.match(await fs.readFile(path.join(f.target('codex'), 'SKILL.md'), 'utf8'), /keep user edit$/);
  await assert.rejects(fs.stat(f.target('claude')), { code: 'ENOENT' });
});

test('batch entry rejects unsupported operations and MCP options without writing', async t => {
  const f = await fixture(t);
  for (const extra of [['verify'], ['uninstall'], ['setup', '--integration', 'mcp'], ['setup', '--select', 'codex']]) {
    assert.throws(() => execFileSync(process.execPath, [entry, ...extra, '--agent', 'all', '--scope', 'project', '--project', f.root],
      { windowsHide: true, stdio: 'pipe' }), error => {
      assert.equal(JSON.parse(error.stderr).code, 'ARGUMENT');
      return true;
    });
  }
  assert.deepEqual(await fs.readdir(f.root), []);
});

test('rollback preserves a valid replacement installed by another operation', async t => {
  const f = await fixture(t);
  const otherCli = path.join(path.dirname(f.cli), 'replacement.exe');
  await fs.writeFile(otherCli, 'replacement fixture');
  let stages = 0;
  await assert.rejects(setupAll(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'staged' && ++stages === 2) {
      await install('uninstall', { ...f.options, agent: 'codex' }, f.context);
      await install('setup', { ...f.options, agent: 'codex', cli: otherCli }, f.context);
      throw new Error('fixture failure after another installation');
    }
  } }), { code: 'ROLLBACK_INCOMPLETE' });
  const binding = JSON.parse(await fs.readFile(path.join(f.target('codex'), 'references/installation.json')));
  assert.equal(binding.cli.path, await fs.realpath(otherCli));
  assert.equal((await install('verify', { ...f.options, agent: 'codex' }, f.context)).action, 'verified');
});

test('failure while releasing a completed target lock cannot be reported as complete rollback', async t => {
  const f = await fixture(t);
  await assert.rejects(setupAll(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'committed') await fs.unlink(path.join(path.dirname(f.target('codex')), '.dll-doctor-installer.lock'));
  } }), error => {
    assert.equal(error.code, 'ROLLBACK_INCOMPLETE');
    assert.equal(error.details.failed_target, f.target('codex'));
    assert.equal(error.details.failed_target_state, 'present');
    return true;
  });
  assert.equal((await fs.stat(f.target('codex'))).isDirectory(), true);
});

test('actual all entry with native CLI: preview, setup, repeat and read-only diagnosis', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI,
}, async t => {
  const f = await fixture(t);
  const cli = path.resolve(process.env.DLL_INSTALLER_TEST_CLI);
  const invoke = (command, agent, extra = []) => JSON.parse(execFileSync(process.execPath,
    [entry, command, '--agent', agent, '--scope', 'project', '--project', f.root, ...extra], { windowsHide: true, encoding: 'utf8' }));
  assert.equal(invoke('setup', 'all', ['--cli', cli, '--dry-run']).action, 'preview');
  assert.deepEqual(await fs.readdir(f.root), []);
  assert.equal(invoke('setup', 'all', ['--cli', cli, '--verify']).action, 'installed');
  assert.equal(invoke('setup', 'all', ['--cli', cli]).action, 'unchanged');
  for (const agent of ['codex', 'claude']) {
    assert.equal(invoke('verify', agent).action, 'verified');
    const binding = JSON.parse(await fs.readFile(path.join(f.target(agent), 'references/installation.json')));
    const report = JSON.parse(execFileSync(binding.cli.path, ['pe', 'inspect', binding.cli.path, '--json'], { windowsHide: true, encoding: 'utf8' }));
    assert.equal(report.status, 'success');
    assert.equal(invoke('uninstall', agent).action, 'removed');
  }
  assert.deepEqual(await files(f.root), {});
});
