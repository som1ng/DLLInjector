import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { installMcp, checkServer } from '../src/mcp.mjs';
import { inspectConfig, hash } from '../src/mcp-config.mjs';

const initial = {
  codex: '# keep comment\nmodel = "keep-model"\n[mcp_servers.other]\ncommand = "other.exe"\n',
  claude: '{"custom":"keep","mcpServers":{"other":{"command":"other.exe"}}}\n',
  opencode: '{\n // keep comment\n "custom":"keep",\n "mcp":{"other":{"type":"local","command":["other.exe"]}},\n}\n',
};

async function fixture(t, agent = 'codex', existing = true) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'dll-mcp-install-')));
  const project = path.join(base, '中文 project space 🚀');
  await fs.mkdir(project);
  const cli = path.join(base, 'dllinjector.exe'), server = path.join(base, 'index.js'), client = path.join(base, 'client.exe');
  for (const file of [cli, server, client]) await fs.writeFile(file, 'fixture bytes; never executed');
  const config = path.join(project, agent === 'codex' ? '.codex/config.toml' : agent === 'claude' ? '.mcp.json' : 'opencode.jsonc');
  if (existing) {
    await fs.mkdir(path.dirname(config), { recursive: true });
    await fs.writeFile(config, initial[agent]);
  }
  const options = { agent, scope: 'project', project, cli, server, client, allowRoot: [project] };
  const query = { agent, scope: 'project', project };
  let probes = 0;
  const context = {
    probeCli: async file => { probes++; return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) }; },
    probeClient: async (name, file) => { probes++; return { path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[name] }; },
    checkServer: async () => { probes++; },
  };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('dll-mcp-install-'));
    await fs.rm(base, { recursive: true });
  });
  return { base, project, config, options, query, context, probes: () => probes };
}

for (const agent of ['codex', 'claude', 'opencode']) {
  test(agent + ': preview, repeat, verify, later edits and removal preserve unrelated settings', async t => {
    const f = await fixture(t, agent);
    const before = await fs.readFile(f.config, 'utf8');
    const preview = await installMcp('setup', { ...f.options, dryRun: true }, f.context);
    assert.equal(preview.action, 'preview');
    assert.equal(preview.executable_probes, 'not_run');
    assert.equal(f.probes(), 0);
    assert.equal(await fs.readFile(f.config, 'utf8'), before);
    assert.equal(await fs.stat(path.join(f.project, '.dll-doctor')).catch(() => null), null);
    const installed = await installMcp('setup', { ...f.options, verify: true }, f.context);
    assert.equal(installed.action, 'installed');
    const content = await fs.readFile(f.config, 'utf8');
    assert.equal((await installMcp('setup', f.options, f.context)).action, 'unchanged');
    assert.equal(await fs.readFile(f.config, 'utf8'), content);
    assert.equal((await installMcp('verify', f.query, f.context)).action, 'verified');
    const changed = agent === 'codex' ? content.replace('keep-model', 'later-model') : content.replace('"keep"', '"later"');
    await fs.writeFile(f.config, changed);
    const probes = f.probes();
    assert.equal((await installMcp('uninstall', { ...f.query, dryRun: true }, f.context)).action, 'preview');
    assert.equal(await fs.readFile(f.config, 'utf8'), changed);
    assert.equal((await installMcp('uninstall', f.query, f.context)).action, 'removed');
    assert.equal(f.probes(), probes);
    const remaining = await fs.readFile(f.config, 'utf8');
    const parsed = inspectConfig(agent, remaining);
    assert.equal(parsed.entry, undefined);
    assert.ok(remaining.includes('other.exe'));
    assert.ok(remaining.includes(agent === 'codex' ? 'later-model' : 'later'));
    if (agent !== 'claude') assert.ok(remaining.includes('keep comment'));
    assert.equal(await fs.stat(installed.receipt).catch(() => null), null);
  });
  test(agent + ': a newly created unchanged config is removed on uninstall', async t => {
    const f = await fixture(t, agent, false);
    const result = await installMcp('setup', f.options, f.context);
    await installMcp('uninstall', f.query, f.context);
    assert.equal(await fs.stat(result.config).catch(() => null), null);
  });
}

test('unowned and edited entries are protected; malformed config is never rewritten', async t => {
  const f = await fixture(t, 'claude');
  await fs.writeFile(f.config, '{"mcpServers":{"dll_doctor":{"command":"user.exe"}}}');
  for (const command of ['setup', 'uninstall']) await assert.rejects(installMcp(command, command === 'setup' ? f.options : f.query, f.context), { code: 'MCP_CONFLICT' });
  for (const content of ['{invalid', '{"mcpServers":{},"mcpServers":{}}', '{"mcpServers":[]}']) {
    await fs.writeFile(f.config, content);
    await assert.rejects(installMcp('setup', f.options, f.context), { code: 'MCP_CONFIG' });
    assert.equal(await fs.readFile(f.config, 'utf8'), content);
  }
  await fs.writeFile(f.config, initial.claude);
  await installMcp('setup', f.options, f.context);
  const content = (await fs.readFile(f.config, 'utf8')).replace('"--cli"', '"--changed"');
  await fs.writeFile(f.config, content);
  await assert.rejects(installMcp('uninstall', f.query, f.context), { code: 'MCP_CONFIG' });
  assert.equal(await fs.readFile(f.config, 'utf8'), content);
});

test('all preflight and caught commit failures preserve original config and receipt state', async t => {
  const f = await fixture(t);
  const before = await fs.readFile(f.config);
  await assert.rejects(installMcp('setup', f.options, { ...f.context, checkServer: async () => { throw new Error('offline'); } }), /offline/);
  assert.deepEqual(await fs.readFile(f.config), before);
  for (const stage of ['config-written', 'receipt-written']) {
    await assert.rejects(installMcp('setup', f.options, { ...f.context, checkpoint: async event => {
      if (event === stage) throw new Error('injected failure');
    } }), /injected failure/);
    assert.deepEqual(await fs.readFile(f.config), before);
    assert.equal(await fs.stat(path.join(f.project, '.dll-doctor')).catch(() => null), null);
  }
  await installMcp('setup', f.options, f.context);
  const installed = await fs.readFile(f.config);
  const receipt = await fs.readFile(path.join(f.project, '.dll-doctor/mcp-codex.json'));
  await assert.rejects(installMcp('uninstall', f.query, { ...f.context, checkpoint: async () => { throw new Error('uninstall fault'); } }), /uninstall fault/);
  assert.deepEqual(await fs.readFile(f.config), installed);
  assert.deepEqual(await fs.readFile(path.join(f.project, '.dll-doctor/mcp-codex.json')), receipt);
});

test('concurrent edits survive failed rollback and locks are not stolen', async t => {
  const f = await fixture(t);
  await assert.rejects(installMcp('setup', f.options, { ...f.context, checkpoint: async () => {
    await fs.writeFile(f.config, '# concurrent user edit\n');
    throw new Error('fault');
  } }), { code: 'ROLLBACK_INCOMPLETE' });
  assert.equal(await fs.readFile(f.config, 'utf8'), '# concurrent user edit\n');
  await fs.mkdir(path.join(f.project, '.dll-doctor'), { recursive: true });
  const lock = path.join(f.project, '.dll-doctor/mcp-codex.lock');
  await fs.writeFile(lock, 'another operation');
  await assert.rejects(installMcp('setup', f.options, f.context), { code: 'MCP_LOCKED' });
  assert.equal(await fs.readFile(lock, 'utf8'), 'another operation');
});

test('ambiguous OpenCode, links, user scope and modified policy are rejected', async t => {
  const f = await fixture(t, 'opencode');
  await fs.writeFile(path.join(f.project, 'opencode.json'), '{}');
  await assert.rejects(installMcp('setup', f.options, f.context), { code: 'MCP_CONFIG' });
  await fs.unlink(path.join(f.project, 'opencode.json'));
  await assert.rejects(installMcp('setup', { ...f.options, scope: 'user' }, f.context), { code: 'MCP_ARGUMENT' });
  await installMcp('setup', f.options, f.context);
  await assert.rejects(installMcp('setup', { ...f.options, allowProcesses: true }, f.context), { code: 'MCP_CONFLICT' });
  const other = await fixture(t, 'claude', false);
  await fs.symlink(f.project, path.join(other.project, '.dll-doctor'), 'junction');
  await assert.rejects(installMcp('setup', other.options, other.context), { code: 'LINKED_PATH' });
  await fs.rmdir(path.join(other.project, '.dll-doctor'));
});

test('native CLI and built MCP server complete the actual handshake/catalog preflight', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI || !process.env.DLL_INSTALLER_TEST_MCP,
}, async () => {
  const cli = process.env.DLL_INSTALLER_TEST_CLI;
  await checkServer({ node: process.execPath, cli: { path: cli },
    server: { path: process.env.DLL_INSTALLER_TEST_MCP }, roots: [path.dirname(cli)], allowProcesses: false });
});

test('a changed receipt cannot authorize deletion of unrelated settings', async t => {
  const f = await fixture(t, 'claude');
  const installed = await installMcp('setup', f.options, f.context);
  const before = await fs.readFile(f.config, 'utf8');
  const receipt = JSON.parse(await fs.readFile(installed.receipt));
  receipt.ownership.configCreated = true;
  await fs.writeFile(installed.receipt, JSON.stringify(receipt));
  await assert.rejects(installMcp('uninstall', f.query, f.context), { code: 'MCP_CONFIG' });
  assert.equal(await fs.readFile(f.config, 'utf8'), before);
  receipt.ownership.configCreated = false;
  receipt.ownership.containerCreated = true;
  await fs.writeFile(installed.receipt, JSON.stringify(receipt));
  await installMcp('uninstall', f.query, f.context);
  const data = JSON.parse(await fs.readFile(f.config));
  assert.equal(data.custom, 'keep');
  assert.equal(data.mcpServers.other.command, 'other.exe');
});

test('uninstall preserves a later adjacent JSONC comment when the editor would remove it', async t => {
  const f = await fixture(t, 'opencode', false);
  const installed = await installMcp('setup', f.options, f.context);
  const changed = (await fs.readFile(installed.config, 'utf8'))
    .replace('"dll_doctor":', '// keep this project note\n    "dll_doctor":');
  await fs.writeFile(installed.config, changed);
  await assert.rejects(installMcp('uninstall', f.query, f.context), { code: 'MCP_CONFIG' });
  assert.equal(await fs.readFile(installed.config, 'utf8'), changed);
  assert.ok(await fs.stat(installed.receipt));
});
