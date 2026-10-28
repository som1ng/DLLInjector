import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { install } from '../src/installer.mjs';
import { installMcp } from '../src/mcp.mjs';
import { hash, inspectConfig } from '../src/mcp-config.mjs';

const agents = ['codex', 'claude', 'opencode'];
const initial = {
  codex: '# keep comment\nmodel = "keep"\n',
  claude: '{"privateFixture":"not-a-real-secret","custom":"keep"}\n',
  opencode: '{\n // keep comment\n "custom": "keep"\n}\n',
};

async function fixture(t, agent = 'codex', scope = 'project', existingConfig = true) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-upgrade-')));
  const project = path.join(base, '项目 space 🚀'), home = path.join(base, '用户 home');
  await fs.mkdir(project); await fs.mkdir(home);
  const cli = path.join(base, 'old.exe'), server = path.join(base, 'old.js'), client = path.join(base, 'client.exe');
  const nextCli = path.join(base, 'new.exe'), nextServer = path.join(base, 'new.js');
  for (const file of [cli, server, client, nextCli, nextServer]) await fs.writeFile(file, 'fixture only: ' + path.basename(file));
  const root = scope === 'project' ? project : home;
  const config = path.join(root, agent === 'codex' ? '.codex/config.toml'
    : agent === 'claude' ? (scope === 'user' ? '.claude.json' : '.mcp.json')
      : scope === 'user' ? '.config/opencode/opencode.json' : 'opencode.json');
  if (existingConfig) {
    await fs.mkdir(path.dirname(config), { recursive: true });
    await fs.writeFile(config, initial[agent]);
  }
  const query = { agent, scope, ...(scope === 'project' ? { project } : {}) };
  const skill = { ...query, cli }, mcp = { ...skill, server, client, allowRoot: [project] };
  let probes = 0;
  const probe = async file => { probes++; return { path: file, tool_version: '2.0.0-dev.9',
    schema_version: '1.6', sha256: hash(await fs.readFile(file)) }; };
  const context = { home, env: {}, probe, probeCli: probe,
    probeClient: async (name, file) => { probes++; return { path: file,
      version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[name] }; },
    checkServer: async () => { probes++; },
  };
  t.after(async () => {
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('rondo-upgrade-'));
    await fs.rm(base, { recursive: true });
  });
  return { base, project, config, cli, server, nextCli, nextServer, query, skill, mcp, context, probes: () => probes };
}

async function files(paths) {
  return Promise.all(paths.map(async file => [file, await fs.readFile(file)]));
}
async function unchanged(snapshot) {
  for (const [file, bytes] of snapshot) assert.deepEqual(await fs.readFile(file), bytes, file);
}

for (const agent of agents) for (const scope of ['project', 'user']) {
  test(`Skill ${agent}/${scope}: preview, upgrade missing old runtime, verify, repeat and uninstall`, async t => {
    const f = await fixture(t, agent, scope);
    const installed = await install('setup', f.skill, f.context);
    const before = await files(installed.files), options = { ...f.skill, cli: f.nextCli };
    const probes = f.probes();
    assert.equal((await install('upgrade', { ...options, dryRun: true }, f.context)).action, 'preview');
    assert.equal(f.probes(), probes); await unchanged(before);
    await assert.rejects(install('setup', options, f.context), { code: 'CONFLICT' });
    await fs.unlink(f.cli);
    assert.equal((await install('upgrade', options, f.context)).action, 'upgraded');
    const verified = await install('verify', f.query, f.context);
    assert.equal(verified.cli.path, f.nextCli);
    assert.equal((await install('upgrade', options, f.context)).action, 'unchanged');
    await install('uninstall', f.query, f.context);
    assert.equal(await fs.stat(installed.target).catch(() => null), null);
    assert.equal(await fs.readFile(f.config, 'utf8'), initial[agent]);
  });

  test(`MCP ${agent}/${scope}: upgrade preserves settings and binds new runtime and explicit policy`, async t => {
    const f = await fixture(t, agent, scope);
    const installed = await installMcp('setup', f.mcp, f.context);
    const paths = [installed.config, installed.receipt], before = await files(paths);
    const options = { ...f.mcp, cli: f.nextCli, server: f.nextServer, allowProcesses: true };
    const probes = f.probes();
    assert.equal((await installMcp('upgrade', { ...options, dryRun: true }, f.context)).action, 'preview');
    assert.equal(f.probes(), probes); await unchanged(before);
    await assert.rejects(installMcp('setup', options, f.context), { code: 'MCP_CONFLICT' });
    await fs.unlink(f.cli); await fs.unlink(f.server);
    const result = await installMcp('upgrade', options, f.context);
    assert.equal(result.action, 'upgraded'); assert.ok(!JSON.stringify(result).includes('not-a-real-secret'));
    const receipt = JSON.parse(await fs.readFile(installed.receipt));
    assert.equal(receipt.runtime.cli.path, f.nextCli); assert.equal(receipt.runtime.allowProcesses, true);
    assert.ok(!JSON.stringify(receipt).includes('not-a-real-secret'));
    assert.equal((await installMcp('verify', f.query, f.context)).action, 'verified');
    assert.equal((await installMcp('upgrade', options, f.context)).action, 'unchanged');
    const upgraded = await fs.readFile(f.config, 'utf8');
    if (agent !== 'claude') assert.ok(upgraded.includes('keep comment'));
    await installMcp('uninstall', f.query, f.context);
    const remaining = await fs.readFile(f.config, 'utf8');
    assert.equal(inspectConfig(agent, remaining).entry, undefined); assert.ok(remaining.includes('keep'));
  });
}

test('Skill upgrade refreshes older managed content and preserves the shared Codex/OpenCode directory', async t => {
  const f = await fixture(t), installed = await install('setup', f.skill, f.context);
  const skillFile = path.join(installed.target, 'SKILL.md'), receiptFile = path.join(installed.target, '.dll-doctor-install.json');
  const canonical = await fs.readFile(skillFile);
  await fs.writeFile(skillFile, 'older managed release');
  const receipt = JSON.parse(await fs.readFile(receiptFile));
  receipt.files['SKILL.md'] = hash('older managed release');
  await fs.writeFile(receiptFile, JSON.stringify(receipt));
  const result = await install('upgrade', { ...f.skill, agent: 'opencode' }, f.context);
  assert.equal(result.target, installed.target); assert.equal(result.action, 'upgraded');
  assert.deepEqual(await fs.readFile(skillFile), canonical);
});

for (const agent of agents) for (const extra of [false, true]) {
  test(`MCP ${agent}: upgrade ${extra ? 'does not adopt later user content' : 'retains ownership of new empty configuration'}`, async t => {
    const f = await fixture(t, agent, 'user', false);
    const installed = await installMcp('setup', f.mcp, f.context);
    if (extra) {
      const source = await fs.readFile(f.config, 'utf8');
      await fs.writeFile(f.config, agent === 'codex' ? 'model = "later"\n' + source : source.replace('{', '{"custom":"later",'));
    }
    await installMcp('upgrade', { ...f.mcp, cli: f.nextCli, server: f.nextServer }, f.context);
    await installMcp('uninstall', f.query, f.context);
    if (extra) assert.ok((await fs.readFile(installed.config, 'utf8')).includes('later'));
    else assert.equal(await fs.stat(installed.config).catch(() => null), null);
  });
}

for (const kind of ['skill', 'mcp']) {
  const run = kind === 'skill' ? install : installMcp;
  test(`${kind} upgrade refuses absent/unowned/edited installs and invalid manifests before writes`, async t => {
    const f = await fixture(t);
    const options = { ...f[kind], cli: f.nextCli };
    await assert.rejects(run('upgrade', options, f.context), { code: kind === 'skill' ? 'NOT_INSTALLED' : 'MCP_NOT_INSTALLED' });
    const installed = await run('setup', f[kind], f.context);
    const paths = kind === 'skill' ? installed.files : [installed.config, installed.receipt];
    const before = await files(paths), probes = f.probes();
    const manifest = path.join(f.base, 'invalid-manifest.json');
    await fs.writeFile(manifest, '{}');
    await assert.rejects(run('upgrade', { ...options, manifest }, f.context), { code: 'DISTRIBUTION_INVALID' });
    assert.equal(f.probes(), probes); await unchanged(before);
    const receiptFile = kind === 'skill' ? path.join(installed.target, '.dll-doctor-install.json') : installed.receipt;
    const receipt = await fs.readFile(receiptFile);
    await fs.unlink(receiptFile);
    await assert.rejects(run('upgrade', options, f.context), { code: kind === 'skill' ? 'CONFLICT' : 'MCP_CONFLICT' });
    assert.equal(await fs.stat(receiptFile).catch(() => null), null);
    await fs.writeFile(receiptFile, receipt);
    await unchanged(before);
    const edited = kind === 'skill' ? path.join(installed.target, 'SKILL.md') : installed.config;
    await fs.appendFile(edited, kind === 'skill' ? 'user edit' : '\n# changed managed block');
    if (kind === 'mcp') {
      await fs.writeFile(edited, (await fs.readFile(edited, 'utf8')).replace('# BEGIN dll-doctor', '# changed dll-doctor'));
    }
    const modified = await files(paths);
    await assert.rejects(run('upgrade', options, f.context));
    await unchanged(modified);
  });

  for (const stage of ['first-write', 'last-write', 'verify']) {
    test(`${kind} upgrade restores exact old configuration and receipt after ${stage} failure`, async t => {
      const f = await fixture(t), installed = await run('setup', f[kind], f.context);
      const paths = kind === 'skill' ? installed.files : [installed.config, installed.receipt];
      const before = await files(paths);
      let writes = 0, probes = 0;
      const context = { ...f.context, checkpoint: () => {
        if (++writes === (stage === 'first-write' ? 1 : stage === 'last-write' ? 2 : -1)) throw new Error('fixture failure');
      } };
      if (stage === 'verify') {
        const key = kind === 'skill' ? 'probe' : 'checkServer', previous = context[key];
        context[key] = async (...args) => { if (++probes === 2) throw new Error('fixture failure'); return previous(...args); };
      }
      await assert.rejects(run('upgrade', { ...f[kind], cli: f.nextCli }, context));
      await unchanged(before);
      assert.equal((await run('verify', f.query, f.context)).action, 'verified');
      assert.equal((await run('upgrade', { ...f[kind], cli: f.nextCli }, f.context)).action, 'upgraded');
    });
  }

  test(`${kind} upgrade preserves concurrent edits and reports incomplete rollback`, async t => {
    const f = await fixture(t), installed = await run('setup', f[kind], f.context);
    let edited;
    const context = { ...f.context, checkpoint: async (stage, data) => {
      edited = kind === 'skill' ? path.join(installed.target, data.file) : installed.config;
      await fs.appendFile(edited, '\nconcurrent user edit');
      throw new Error('fixture failure');
    } };
    await assert.rejects(run('upgrade', { ...f[kind], cli: f.nextCli }, context), { code: 'ROLLBACK_INCOMPLETE' });
    assert.ok((await fs.readFile(edited, 'utf8')).includes('concurrent user edit'));
  });
}

test('MCP upgrade can rebind overwritten runtime bytes at the same paths', async t => {
  const f = await fixture(t);
  await installMcp('setup', f.mcp, f.context);
  await fs.appendFile(f.cli, 'new build'); await fs.appendFile(f.server, 'new build');
  await assert.rejects(installMcp('verify', f.query, f.context), { code: 'MCP_RUNTIME_CHANGED' });
  assert.equal((await installMcp('upgrade', f.mcp, f.context)).action, 'upgraded');
  assert.equal((await installMcp('verify', f.query, f.context)).action, 'verified');
});

test('upgrade CLI refuses auto/all and never dispatches batch writes', () => {
  const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
  for (const agent of ['auto', 'all']) {
    assert.throws(() => execFileSync(process.execPath, [entry, 'upgrade', '--agent', agent, '--scope', 'user'],
      { windowsHide: true, stdio: 'pipe' }), error => JSON.parse(error.stderr.toString()).code === 'ARGUMENT');
  }
});

test('Skill and MCP upgrades verify real CLI bytes and a real local MCP server', {
  skip: !process.env.DLL_INSTALLER_TEST_CLI || !process.env.DLL_INSTALLER_TEST_MCP,
}, async t => {
  const f = await fixture(t);
  const { probe, probeCli, checkServer, ...context } = f.context; // Only client version detection remains a fixture.
  const cli = process.env.DLL_INSTALLER_TEST_CLI, server = process.env.DLL_INSTALLER_TEST_MCP;
  await fs.copyFile(cli, f.nextCli);
  await install('setup', { ...f.skill, cli }, context);
  assert.equal((await install('upgrade', { ...f.skill, cli: f.nextCli }, context)).action, 'upgraded');
  await installMcp('setup', { ...f.mcp, cli, server }, context);
  assert.equal((await installMcp('upgrade', { ...f.mcp, cli: f.nextCli, server }, context)).action, 'upgraded');
  await install('verify', f.query, context);
  await installMcp('verify', f.query, context);
  await install('uninstall', f.query, context);
  await installMcp('uninstall', f.query, context);
});
