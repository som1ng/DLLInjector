import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { installMcp } from '../src/mcp.mjs';
import { setupAllMcp } from '../src/batch-mcp.mjs';
import { hash, inspectConfig } from '../src/mcp-config.mjs';

const agents = ['codex', 'claude', 'opencode'];
const secret = 'fixture-only-private-oauth-value';
const initial = {
  codex: '# keep comment\nmodel = "keep-model"\n[mcp_servers.other]\ncommand = "other.exe"\n',
  claude: JSON.stringify({ oauthAccount: { privateFixture: secret }, projects: { '/private-project': { custom: 'keep' } },
    mcpServers: { other: { command: 'other.exe' } } }) + '\n',
  opencode: '{\n // keep comment\n "custom":"keep",\n "mcp":{"other":{"type":"local","command":["other.exe"]}},\n}\n',
};
async function fixture(t, existing = true) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-user-mcp-')));
  const home = path.join(base, '用户 space 🧪'), project = path.join(base, 'project');
  await fs.mkdir(home); await fs.mkdir(project);
  const cli = path.join(base, 'rondo.exe'), server = path.join(base, 'index.js'), clients = {};
  for (const agent of agents) clients[agent] = path.join(base, agent + '.exe');
  for (const file of [cli, server, ...Object.values(clients)]) await fs.writeFile(file, 'fixture; never execute');
  const configs = { codex: path.join(home, '.codex/config.toml'), claude: path.join(home, '.claude.json'),
    opencode: path.join(home, '.config/opencode', existing ? 'opencode.jsonc' : 'opencode.json') };
  if (existing) for (const agent of agents) {
    await fs.mkdir(path.dirname(configs[agent]), { recursive: true });
    await fs.writeFile(configs[agent], initial[agent]);
  }
  let probes = 0;
  const context = { home, env: {},
    probeCli: async file => { probes++; return { path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) }; },
    probeClient: async (agent, file) => { probes++; return { path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }; },
    checkServer: async () => { probes++; },
  };
  const options = { integration: 'mcp', agent: 'all', scope: 'user', cli, server, clients, allowRoot: [project] };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('rondo-user-mcp-'));
    await fs.rm(base, { recursive: true });
  });
  return { base, home, project, configs, options, context, probes: () => probes,
    single: agent => ({ ...options, agent, client: clients[agent] }), query: agent => ({ agent, scope: 'user' }) };
}

for (const agent of agents) {
  test(agent + ' user setup preserves existing fields and keeps private values out of receipts/results', async t => {
    const f = await fixture(t), options = f.single(agent), query = f.query(agent);
    const before = await fs.readFile(f.configs[agent], 'utf8');
    const preview = await installMcp('setup', { ...options, dryRun: true }, f.context);
    assert.equal(preview.scope, 'user'); assert.equal(preview.config, f.configs[agent]);
    assert.equal(f.probes(), 0);
    assert.equal(await fs.readFile(preview.config, 'utf8'), before);
    assert.equal(await fs.stat(preview.receipt).catch(() => null), null);
    const installed = await installMcp('setup', { ...options, verify: true }, f.context);
    assert.equal(installed.action, 'installed');
    assert.equal((await installMcp('setup', options, f.context)).action, 'unchanged');
    assert.equal((await installMcp('verify', query, f.context)).action, 'verified');
    const receipt = await fs.readFile(installed.receipt, 'utf8');
    assert.equal(JSON.parse(receipt).scope, 'user');
    assert.ok(!receipt.includes(secret)); assert.ok(!JSON.stringify([preview, installed]).includes(secret));
    const changed = (await fs.readFile(installed.config, 'utf8')).replace('keep', 'later');
    await fs.writeFile(installed.config, changed);
    assert.equal((await installMcp('uninstall', query, f.context)).action, 'removed');
    const after = await fs.readFile(installed.config, 'utf8');
    assert.equal(inspectConfig(agent, after).entry, undefined);
    assert.ok(after.includes('later')); assert.ok(after.includes('other.exe'));
    if (agent === 'claude') assert.ok(after.includes(secret) && after.includes('/private-project'));
    assert.deepEqual(await fs.readdir(f.project), []);
  });
  test(agent + ' user setup creates absent config and removes only owned files', async t => {
    const f = await fixture(t, false);
    const installed = await installMcp('setup', f.single(agent), f.context);
    await installMcp('uninstall', f.query(agent), f.context);
    assert.equal(await fs.stat(installed.config).catch(() => null), null);
    assert.equal(await fs.stat(installed.receipt).catch(() => null), null);
  });
}

test('Codex honors existing absolute CODEX_HOME and preserves scope identity when it names the default directory', async t => {
  const f = await fixture(t), options = f.single('codex');
  const custom = path.join(f.home, 'alternate-codex');
  await fs.mkdir(custom);
  const context = { ...f.context, env: { CODEX_HOME: custom } };
  const before = await fs.readFile(f.configs.codex);
  const installed = await installMcp('setup', options, context);
  assert.equal(installed.config, path.join(custom, 'config.toml'));
  assert.deepEqual(await fs.readFile(f.configs.codex), before);
  await installMcp('uninstall', f.query('codex'), context);
  await installMcp('setup', options, f.context);
  assert.equal((await installMcp('verify', f.query('codex'),
    { ...f.context, env: { CODEX_HOME: path.join(f.home, '.codex') } })).action, 'verified');
});

test('unsupported user config overrides fail before writes or probes without exposing their values', async t => {
  const f = await fixture(t);
  for (const [agent, key] of [['claude', 'CLAUDE_CONFIG_DIR'],
    ...['XDG_CONFIG_HOME', 'OPENCODE_CONFIG', 'OPENCODE_CONFIG_DIR', 'OPENCODE_CONFIG_CONTENT'].map(key => ['opencode', key])]) {
    await assert.rejects(installMcp('setup', f.single(agent), { ...f.context, env: { [key]: secret } }), error => {
      assert.equal(error.code, 'MCP_CUSTOM_CONFIG'); assert.ok(!error.message.includes(secret)); return true;
    });
    assert.equal(await fs.readFile(f.configs[agent], 'utf8'), initial[agent]);
  }
  for (const custom of ['relative', path.join(f.home, 'missing')]) {
    await assert.rejects(installMcp('setup', f.single('codex'), { ...f.context, env: { CODEX_HOME: custom } }), { code: 'MCP_PATH' });
  }
  await assert.rejects(installMcp('setup', { ...f.single('claude'), project: f.project }, f.context), { code: 'MCP_ARGUMENT' });
  assert.equal(f.probes(), 0);
});

test('OpenCode user ambiguity and linked config paths are preserved', async t => {
  const f = await fixture(t);
  const other = path.join(path.dirname(f.configs.opencode), 'opencode.json');
  await fs.writeFile(other, '{}');
  await assert.rejects(installMcp('setup', f.single('opencode'), f.context), { code: 'MCP_CONFIG' });
  const redirected = path.join(f.home, 'redirected');
  await fs.symlink(f.project, redirected, 'junction');
  await assert.rejects(installMcp('setup', f.single('codex'), { ...f.context, env: { CODEX_HOME: redirected } }), { code: 'LINKED_PATH' });
  assert.deepEqual(await fs.readdir(f.project), []);
});

test('user and project receipts cannot authorize removal across scopes; overlapping Codex paths share the lock', async t => {
  const f = await fixture(t);
  const installed = await installMcp('setup', f.single('codex'), f.context);
  await assert.rejects(installMcp('uninstall', { agent: 'codex', scope: 'project', project: f.home }, f.context), { code: 'MCP_CONFLICT' });
  const receipt = JSON.parse(await fs.readFile(installed.receipt));
  receipt.scope = 'project';
  await fs.writeFile(installed.receipt, JSON.stringify(receipt));
  await assert.rejects(installMcp('uninstall', f.query('codex'), f.context), { code: 'MCP_RECEIPT' });
  receipt.scope = 'user'; await fs.writeFile(installed.receipt, JSON.stringify(receipt));
  const lock = path.join(f.home, '.dll-doctor/mcp-codex.lock');
  await fs.writeFile(lock, 'other operation');
  await assert.rejects(installMcp('uninstall', f.query('codex'), f.context), { code: 'MCP_LOCKED' });
  assert.equal(await fs.readFile(lock, 'utf8'), 'other operation');
});

test('legacy project receipts without scope remain supported', async t => {
  const f = await fixture(t);
  const options = { ...f.single('codex'), scope: 'project', project: f.project };
  const installed = await installMcp('setup', options, f.context);
  const receipt = JSON.parse(await fs.readFile(installed.receipt));
  delete receipt.scope; await fs.writeFile(installed.receipt, JSON.stringify(receipt));
  await installMcp('uninstall', { agent: 'codex', scope: 'project', project: f.project }, f.context);
  assert.equal(await fs.readFile(f.configs.codex, 'utf8'), initial.codex);
});

test('batch user setup previews all targets, repeats safely and permits explicit removal', async t => {
  const f = await fixture(t);
  const preview = await setupAllMcp({ ...f.options, dryRun: true }, f.context);
  assert.equal(preview.scope, 'user'); assert.equal(preview.targets.length, 3); assert.equal(f.probes(), 0);
  const installed = await setupAllMcp({ ...f.options, verify: true }, f.context);
  assert.equal(installed.action, 'installed');
  assert.equal((await setupAllMcp(f.options, f.context)).action, 'unchanged');
  for (const agent of agents) await installMcp('uninstall', f.query(agent), f.context);
});
for (const existing of [true, false]) {
  test('batch user failure rolls back ' + (existing ? 'existing files byte for byte' : 'new configs to absence'), async t => {
    const f = await fixture(t, existing);
    let writes = 0;
    await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: stage => {
      if (stage === 'config-written' && ++writes === 3) throw new Error(secret);
    } }), error => {
      assert.equal(error.details.rollback_status, 'complete'); assert.ok(!JSON.stringify(error).includes(secret));
      assert.ok(!error.message.includes(secret)); return true;
    });
    for (const agent of agents) {
      if (existing) assert.equal(await fs.readFile(f.configs[agent], 'utf8'), initial[agent]);
      else assert.equal(await fs.stat(f.configs[agent]).catch(() => null), null);
    }
  });
}

test('a prior user installation survives a later batch failure', async t => {
  const f = await fixture(t);
  const installed = await installMcp('setup', f.single('codex'), f.context);
  const config = await fs.readFile(installed.config), receipt = await fs.readFile(installed.receipt);
  let writes = 0;
  await assert.rejects(setupAllMcp(f.options, { ...f.context, checkpoint: stage => {
    if (stage === 'config-written' && ++writes === 2) throw new Error('fixture failure');
  } }));
  assert.deepEqual(await fs.readFile(installed.config), config);
  assert.deepEqual(await fs.readFile(installed.receipt), receipt);
  assert.equal(await fs.readFile(f.configs.claude, 'utf8'), initial.claude);
});

test('user CLI preview uses an isolated Windows profile without writing config or executing clients', async t => {
  const f = await fixture(t);
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    ['PATH', 'SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'TEMP', 'TMP'].includes(key.toUpperCase())));
  Object.assign(env, { USERPROFILE: f.home, APPDATA: path.join(f.home, 'AppData/Roaming'), LOCALAPPDATA: path.join(f.home, 'AppData/Local') });
  const exec = args => execFileSync(process.execPath, args, { env, windowsHide: true, encoding: 'utf8', stdio: 'pipe' });
  assert.equal(JSON.parse(exec(['-p', 'JSON.stringify(require("node:os").homedir())'])), f.home);
  const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
  const args = [entry, 'setup', '--integration', 'mcp', '--agent', 'all', '--scope', 'user', '--cli', f.options.cli,
    '--server', f.options.server, '--allow-root', f.project, '--dry-run'];
  for (const agent of agents) args.push('--' + agent + '-client', f.options.clients[agent]);
  const result = JSON.parse(exec(args));
  assert.equal(result.scope, 'user'); assert.equal(result.action, 'preview');
  for (const target of result.targets) {
    assert.equal(target.config, f.configs[target.agent]);
    assert.equal(await fs.stat(target.receipt).catch(() => null), null);
  }
});

test('user setup verifies a real CLI and MCP server in the isolated home', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI || !process.env.DLL_INSTALLER_TEST_MCP,
}, async t => {
  const f = await fixture(t, false);
  const { probeCli, checkServer, ...context } = f.context; // Only the native client version is a fixture.
  const result = await installMcp('setup', { ...f.single('codex'), cli: process.env.DLL_INSTALLER_TEST_CLI,
    server: process.env.DLL_INSTALLER_TEST_MCP, verify: true }, context);
  assert.equal(result.server_verification, 'handshake_and_tools');
  await installMcp('verify', f.query('codex'), context);
  await installMcp('uninstall', f.query('codex'), context);
});
