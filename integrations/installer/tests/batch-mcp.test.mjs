import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { setupAllMcp } from '../src/batch-mcp.mjs';
import { installMcp } from '../src/mcp.mjs';
import { hash, inspectConfig } from '../src/mcp-config.mjs';

const agents = ['codex', 'claude', 'opencode'];
const versions = { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' };
const secret = 'fixture-only-private-value';
const initial = {
  codex: '# keep comment\nmodel = "keep-model"\n[mcp_servers.other]\ncommand = "other.exe"\n',
  claude: JSON.stringify({ custom: 'keep', fixture_secret: secret, mcpServers: { other: { command: 'other.exe' } } }) + '\n',
  opencode: '{\n // keep comment\n "custom":"keep",\n "mcp":{"other":{"type":"local","command":["other.exe"]}},\n}\n',
};
const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));

async function fixture(t, existing = true) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'dll-batch-mcp-')));
  const project = path.join(base, '项目 space 🧪');
  await fs.mkdir(project);
  const cli = path.join(base, 'dllinjector.exe'), server = path.join(base, 'index.js'), clients = {};
  for (const agent of agents) clients[agent] = path.join(base, agent + '.exe');
  for (const file of [cli, server, ...Object.values(clients)]) await fs.writeFile(file, 'fixture bytes; never executed');
  const configs = { codex: path.join(project, '.codex/config.toml'), claude: path.join(project, '.mcp.json'),
    opencode: path.join(project, existing ? 'opencode.jsonc' : 'opencode.json') };
  if (existing) for (const agent of agents) {
    await fs.mkdir(path.dirname(configs[agent]), { recursive: true });
    await fs.writeFile(configs[agent], initial[agent]);
  }
  const options = { integration: 'mcp', agent: 'all', scope: 'project', project, cli, server, clients, allowRoot: [project] };
  let probes = 0;
  const context = {
    probeCli: async file => { probes++; return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) }; },
    probeClient: async (agent, file) => { probes++; return { path: file, version: versions[agent] }; },
    checkServer: async () => { probes++; },
  };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('dll-batch-mcp-'));
    await fs.rm(base, { recursive: true });
  });
  return { base, project, configs, options, context, probes: () => probes,
    single: agent => ({ ...options, agent, client: clients[agent] }),
    query: agent => ({ agent, scope: 'project', project }) };
}

async function files(root) {
  const result = {};
  async function walk(base, prefix = '') {
    for (const item of await fs.readdir(base, { withFileTypes: true })) {
      const name = prefix + item.name, file = path.join(base, item.name);
      if (item.isDirectory()) await walk(file, name + '/');
      else result[name] = hash(await fs.readFile(file));
    }
  }
  await walk(root);
  return result;
}

for (const existing of [true, false]) {
  test('batch MCP ' + (existing ? 'existing' : 'new') + ': preview, setup, repeat, verify and explicit removal', async t => {
    const f = await fixture(t, existing);
    const before = await files(f.project), directories = await fs.readdir(f.project);
    const preview = await setupAllMcp({ ...f.options, dryRun: true }, f.context);
    assert.equal(preview.action, 'preview');
    assert.deepEqual(preview.targets.map(row => row.agent), agents);
    assert.ok(preview.targets.every(row => row.executable_probes === 'not_run'));
    assert.equal(f.probes(), 0);
    assert.deepEqual(await files(f.project), before);
    assert.deepEqual(await fs.readdir(f.project), directories);
    const result = await setupAllMcp({ ...f.options, verify: true }, f.context);
    assert.equal(result.action, 'installed');
    assert.equal(f.probes(), 18);
    assert.ok(result.targets.every(row => row.server_verification === 'handshake_and_tools'));
    assert.ok(!JSON.stringify(result).includes(secret));
    for (const row of result.targets) assert.ok(!(await fs.readFile(row.receipt, 'utf8')).includes(secret));
    const installed = await files(f.project);
    assert.equal((await setupAllMcp(f.options, f.context)).action, 'unchanged');
    assert.deepEqual(await files(f.project), installed);
    for (const agent of agents) {
      assert.equal((await installMcp('verify', f.query(agent), f.context)).action, 'verified');
      assert.equal((await installMcp('uninstall', f.query(agent), f.context)).action, 'removed');
      const text = await fs.readFile(f.configs[agent], 'utf8').catch(() => null);
      if (existing) {
        assert.equal(inspectConfig(agent, text).entry, undefined);
        assert.ok(text.includes('other.exe'));
      } else assert.equal(text, null);
    }
  });
}

test('an invalid third configuration prevents all writes and probes', async t => {
  const f = await fixture(t);
  await fs.writeFile(f.configs.opencode, '{ invalid');
  const before = await files(f.project);
  await assert.rejects(setupAllMcp(f.options, f.context), error => {
    assert.equal(error.code, 'MCP_CONFIG');
    assert.equal(error.details.failed_agent, 'opencode');
    assert.deepEqual(error.details.completed_targets, []);
    return true;
  });
  assert.equal(f.probes(), 0);
  assert.deepEqual(await files(f.project), before);
});

test('an invalid last native client path prevents all writes and probes', async t => {
  const f = await fixture(t);
  const before = await files(f.project);
  await assert.rejects(setupAllMcp({ ...f.options, clients: { ...f.options.clients, opencode: 'wrapper.cmd' } }, f.context), { code: 'MCP_PATH' });
  assert.deepEqual(await files(f.project), before);
  assert.equal(f.probes(), 0);
});

for (const stage of ['config-written', 'receipt-written']) {
  test('third ' + stage + ' failure restores every original byte and excludes private configuration from errors', async t => {
    const f = await fixture(t);
    const before = await files(f.project);
    let hits = 0;
    await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async current => {
      if (current === stage && ++hits === 3) throw new Error(secret);
    } }), error => {
      assert.equal(error.code, 'BATCH_MCP_FAILED');
      assert.equal(error.details.rollback_status, 'complete');
      assert.deepEqual(error.details.rollback.map(row => row.agent), ['claude', 'codex']);
      assert.ok(!JSON.stringify(error).includes(secret));
      assert.ok(!error.message.includes(secret));
      return true;
    });
    assert.deepEqual(await files(f.project), before);
  });
}

test('final handshake failure restores new configuration files to absence', async t => {
  const f = await fixture(t, false);
  let checks = 0;
  await assert.rejects(setupAllMcp({ ...f.options, verify: true }, { ...f.context, checkServer: async () => {
    if (++checks === 6) throw new Error('final verification failure');
  } }), { code: 'BATCH_MCP_FAILED' });
  assert.equal(checks, 6, 'rollback never probes the server');
  assert.deepEqual(await files(f.project), {});
});

test('a previously installed entry survives failure of a later target', async t => {
  const f = await fixture(t);
  await installMcp('setup', f.single('codex'), f.context);
  const before = await files(f.project);
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async () => { throw new Error('fixture failure'); } }), error => {
    assert.equal(error.details.completed_targets[0].action, 'unchanged');
    assert.deepEqual(error.details.rollback, []);
    return true;
  });
  assert.deepEqual(await files(f.project), before);
});

test('batch rollback preserves later unrelated fields instead of restoring an old whole file', async t => {
  const f = await fixture(t);
  let writes = 0;
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'config-written' && ++writes === 3) {
      await fs.writeFile(f.configs.claude, (await fs.readFile(f.configs.claude, 'utf8')).replace('"keep"', '"later"'));
      throw new Error('fixture failure');
    }
  } }), { code: 'BATCH_MCP_FAILED' });
  const restored = inspectConfig('claude', await fs.readFile(f.configs.claude, 'utf8'));
  assert.equal(restored.entry, undefined);
  assert.equal(restored.data.custom, 'later');
  assert.equal(restored.data.fixture_secret, secret);
  assert.equal(await fs.readFile(f.configs.codex, 'utf8'), initial.codex);
});

test('another valid installation cannot be removed by batch rollback', async t => {
  const f = await fixture(t);
  let writes = 0;
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'config-written' && ++writes === 2) {
      await installMcp('uninstall', f.query('codex'), f.context);
      await installMcp('setup', { ...f.single('codex'), allowProcesses: true }, f.context);
      throw new Error('fixture failure');
    }
  } }), error => {
    assert.equal(error.code, 'ROLLBACK_INCOMPLETE');
    assert.equal(error.details.rollback[0].code, 'MCP_CONCURRENT');
    return true;
  });
  assert.ok(inspectConfig('codex', await fs.readFile(f.configs.codex, 'utf8')).entry.args.includes('--allow-processes'));
  assert.equal((await installMcp('verify', f.query('codex'), f.context)).action, 'verified');
});

test('changed managed values are preserved and reported as incomplete rollback', async t => {
  const f = await fixture(t);
  let writes = 0;
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'config-written' && ++writes === 2) {
      await fs.writeFile(f.configs.codex, (await fs.readFile(f.configs.codex, 'utf8')).replace('--cli', '--user-change'));
      throw new Error('fixture failure');
    }
  } }), { code: 'ROLLBACK_INCOMPLETE' });
  assert.ok((await fs.readFile(f.configs.codex, 'utf8')).includes('--user-change'));
});

test('lock cleanup failure after commit is not reported as complete rollback', async t => {
  const f = await fixture(t);
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: async stage => {
    if (stage === 'receipt-written') await fs.unlink(path.join(f.project, '.dll-doctor/mcp-codex.lock'));
  } }), error => {
    assert.equal(error.code, 'ROLLBACK_INCOMPLETE');
    assert.equal(error.details.failed_target_state, 'present');
    return true;
  });
  assert.ok(inspectConfig('codex', await fs.readFile(f.configs.codex, 'utf8')).entry);
});

test('CLI batch preview is read-only and requires all three explicit native paths', async t => {
  const f = await fixture(t);
  const args = [entry, 'setup', '--integration', 'mcp', '--agent', 'all', '--scope', 'project', '--project', f.project,
    '--cli', f.options.cli, '--server', f.options.server, '--allow-root', f.project, '--dry-run'];
  const before = await files(f.project);
  assert.throws(() => execFileSync(process.execPath, args, { windowsHide: true, stdio: 'pipe' }), error => {
    assert.equal(JSON.parse(error.stderr).code, 'MCP_ARGUMENT'); return true;
  });
  for (const agent of agents) args.push('--' + agent + '-client', f.options.clients[agent]);
  const preview = JSON.parse(execFileSync(process.execPath, args, { windowsHide: true, encoding: 'utf8' }));
  assert.equal(preview.targets.length, 3);
  assert.ok(preview.targets.every(row => row.executable_probes === 'not_run'));
  assert.deepEqual(await files(f.project), before);
  for (const invalid of [
    ['setup', '--agent', 'codex', '--codex-client', f.options.clients.codex],
    ['setup', '--agent', 'all', '--codex-client', f.options.clients.codex],
    ['verify', '--integration', 'mcp', '--agent', 'all'],
  ]) assert.throws(() => execFileSync(process.execPath, [entry, ...invalid], { windowsHide: true, stdio: 'pipe' }), error => {
    assert.equal(JSON.parse(error.stderr).code, 'ARGUMENT'); return true;
  });
});

test('batch MCP reuses the actual CLI and server handshake with fixture client versions', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI || !process.env.DLL_INSTALLER_TEST_MCP,
}, async t => {
  const f = await fixture(t, false);
  const options = { ...f.options, cli: path.resolve(process.env.DLL_INSTALLER_TEST_CLI),
    server: path.resolve(process.env.DLL_INSTALLER_TEST_MCP), verify: true };
  const result = await setupAllMcp(options, { probeClient: f.context.probeClient });
  assert.equal(result.action, 'installed');
  for (const agent of agents) await installMcp('uninstall', f.query(agent));
  assert.deepEqual(await files(f.project), {});
});
