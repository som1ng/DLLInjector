import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { discoverAgents, setupAuto } from '../src/agents.mjs';
import { install, NAME } from '../src/installer.mjs';

const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));

async function fixture(t) {
  const root = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'dll-agent-selection-')));
  const bin = path.join(root, '中文 bin 🚀'), home = path.join(root, 'home');
  await fs.mkdir(bin); await fs.mkdir(home);
  const context = { env: { PATH: bin }, home, arch: 'x64' };
  const write = async relative => {
    const file = path.join(bin, relative);
    await fs.mkdir(path.dirname(file), { recursive: true });
    await fs.writeFile(file, 'fixture: this file must never be executed');
    return fs.realpath(file);
  };
  const discover = () => discoverAgents(context);
  t.after(async () => {
    assert.equal(path.dirname(root), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(root).startsWith('dll-agent-selection-'));
    await fs.rm(root, { recursive: true });
  });
  return { root, bin, home, context, write, discover };
}

test('discovery reads native paths, ignores relative/empty paths and deduplicates without execution', async t => {
  const f = await fixture(t);
  const native = await f.write('codex.exe');
  f.context.env = { Path: `;.;relative;"${f.bin}";${f.bin.toUpperCase()};\\\\server\\bin` };
  const found = await f.discover();
  assert.equal(found.executable_probes, 'not_run');
  assert.deepEqual(found.agents[0].candidates, [{ client: native, source: 'path' }]);
  assert.equal(found.agents[1].candidates.length, 0);
  assert.deepEqual(found.warnings, []);
});

test('npm discovery uses known native layouts and ignores arbitrary shim commands', async t => {
  const f = await fixture(t);
  const binaries = {
    codex: 'node_modules/@openai/codex/node_modules/@openai/codex-win32-x64/vendor/x86_64-pc-windows-msvc/bin/codex.exe',
    claude: 'node_modules/@anthropic-ai/claude-code/bin/claude.exe',
    opencode: 'node_modules/opencode-ai/bin/opencode.exe',
  };
  for (const [agent, relative] of Object.entries(binaries)) {
    await f.write(agent + '.cmd');
    binaries[agent] = await f.write(relative);
  }
  const found = await f.discover();
  for (const item of found.agents) assert.deepEqual(item.candidates, [{ client: binaries[item.agent], source: 'path_npm' }]);
  await fs.unlink(binaries.opencode);
  assert.equal((await f.discover()).agents[2].candidates.length, 0);
});

test('native PATH entries take priority within a directory and user locations are inspected', async t => {
  const f = await fixture(t);
  const native = await f.write('claude.exe');
  await f.write('claude.cmd');
  await f.write('node_modules/@anthropic-ai/claude-code/bin/claude.exe');
  await fs.mkdir(path.join(f.home, '.local/bin'), { recursive: true });
  const user = path.join(f.home, '.local/bin/opencode.exe');
  await fs.writeFile(user, 'never execute');
  const found = await f.discover();
  assert.deepEqual(found.agents[1].candidates, [{ client: native, source: 'path' }]);
  assert.deepEqual(found.agents[2].candidates, [{ client: await fs.realpath(user), source: 'user_bin' }]);
});

test('Codex hoisted/vendor layouts and user npm prefix are discovered without scanning packages', async t => {
  const f = await fixture(t);
  f.context.env = { APPDATA: f.home };
  const prefix = path.join(f.home, 'npm');
  await fs.mkdir(prefix);
  await fs.writeFile(path.join(prefix, 'codex.ps1'), 'never execute');
  for (const relative of ['node_modules/@openai/codex-win32-x64/vendor/x86_64-pc-windows-msvc/bin/codex.exe',
    'node_modules/@openai/codex/vendor/x86_64-pc-windows-msvc/bin/codex.exe']) {
    const candidate = path.join(prefix, relative);
    await fs.mkdir(path.dirname(candidate), { recursive: true });
    await fs.writeFile(candidate, 'never execute');
    assert.deepEqual((await f.discover()).agents[0].candidates,
      [{ client: await fs.realpath(candidate), source: 'user_npm' }]);
    await fs.unlink(candidate);
  }
});

test('auto dry-run only lists choices until selected, even with a single candidate', async t => {
  const f = await fixture(t);
  await f.write('codex.exe');
  let calls = 0;
  const result = await setupAuto({ scope: 'project', dryRun: true }, { discover: f.discover,
    choose: async () => { calls++; }, run: async () => { calls++; } });
  assert.equal(result.action, 'selection_required');
  assert.equal(calls, 0);
  assert.deepEqual(await fs.readdir(f.root), [path.basename(f.bin), 'home'].sort());
});

test('no matches, an absent choice, ambiguous paths and invalid options never start installation', async t => {
  const f = await fixture(t);
  let calls = 0;
  const context = { discover: f.discover, run: async () => { calls++; } };
  await assert.rejects(setupAuto({ scope: 'project', select: 'codex' }, context), { code: 'NO_AGENTS' });
  await f.write('codex.exe');
  await assert.rejects(setupAuto({ scope: 'project', select: 'claude' }, context), { code: 'AGENT_SELECTION' });
  const second = path.join(f.home, '.local/bin');
  await fs.mkdir(second, { recursive: true });
  await fs.writeFile(path.join(second, 'codex.exe'), 'never execute');
  await assert.rejects(setupAuto({ scope: 'project', select: 'codex' }, context), { code: 'AGENT_SELECTION' });
  for (const options of [{ scope: 'invalid' }, { scope: 'user', integration: 'mcp' },
    { scope: 'project', select: 'all' }, { scope: 'project', client: 'fake.exe' }]) {
    await assert.rejects(setupAuto(options, context), { code: 'ARGUMENT' });
  }
  assert.equal(calls, 0);
});

test('explicit selection and terminal choices dispatch exactly one client with the native MCP path', async t => {
  const f = await fixture(t);
  const codex = await f.write('codex.exe'), claude = await f.write('claude.exe');
  await fs.mkdir(path.join(f.home, '.local/bin'), { recursive: true });
  await fs.writeFile(path.join(f.home, '.local/bin/codex.exe'), 'another candidate, never execute');
  const calls = [];
  const context = { discover: f.discover, run: async (command, options) => {
    calls.push({ command, options }); return { action: options.dryRun ? 'preview' : 'installed' };
  } };
  const result = await setupAuto({ scope: 'project', integration: 'mcp', select: 'codex', client: codex, dryRun: true }, context);
  assert.equal(result.agent_detection.client, codex);
  assert.equal(calls[0].options.client, codex);
  assert.equal(calls[0].options.dryRun, true);
  await setupAuto({ scope: 'project', integration: 'mcp' }, { ...context, choose: async candidates => {
    const index = candidates.findIndex(candidate => candidate.client === claude);
    assert.ok(index >= 0); return index;
  } });
  assert.equal(calls.length, 2);
  assert.equal(calls[1].options.agent, 'claude');
  assert.equal(calls[1].options.client, claude);
});

test('cancelled or invalid terminal choices leave every client untouched', async t => {
  const f = await fixture(t);
  await f.write('codex.exe');
  let calls = 0;
  for (const index of [undefined, -1, 2, 0.5]) {
    await assert.rejects(setupAuto({ scope: 'project' }, { discover: f.discover,
      choose: async () => index, run: async () => { calls++; } }), { code: 'AGENT_SELECTION_CANCELLED' });
  }
  assert.equal(calls, 0);
});

test('selected Skill setup reuses preview, shared destination, repeat and uninstall behavior', async t => {
  const f = await fixture(t);
  const client = await f.write('opencode.exe'), cli = await f.write('dllinjector.exe');
  let probes = 0;
  const context = { discover: f.discover, run: (command, options) => install(command, options, {
    probe: async () => { probes++; return { path: cli, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: 'fixture' }; },
  }) };
  const options = { agent: 'auto', select: 'opencode', client, scope: 'project', project: f.home, cli };
  assert.equal((await setupAuto({ ...options, dryRun: true }, context)).action, 'preview');
  assert.equal(probes, 0);
  const installed = await setupAuto(options, context);
  assert.equal(installed.target, path.join(f.home, '.agents/skills', NAME));
  assert.equal((await setupAuto(options, context)).action, 'unchanged');
  assert.equal((await install('uninstall', { agent: 'opencode', scope: 'project', project: f.home })).action, 'removed');
});

test('public CLI lists candidates and rejects unconfirmed piped input before any native program starts', async t => {
  const f = await fixture(t);
  await f.write('codex.exe');
  const env = { ...process.env, PATH: f.bin, USERPROFILE: f.home, APPDATA: f.home };
  const found = JSON.parse(execFileSync(process.execPath, [entry, 'list-agents'], { env, windowsHide: true }));
  assert.equal(found.agents[0].candidates.length, 1);
  const result = spawnSync(process.execPath, [entry, 'setup', '--agent', 'auto', '--scope', 'project'],
    { env, input: '1\n', windowsHide: true, timeout: 10000 });
  assert.equal(result.status, 1);
  assert.equal(JSON.parse(result.stderr).code, 'AGENT_SELECTION_REQUIRED');
  assert.equal(JSON.parse(result.stderr).details.discovery.executable_probes, 'not_run');
  for (const args of [['uninstall', '--agent', 'auto'], ['verify', '--agent', 'codex', '--select', 'codex'], ['list-agents', '--scope', 'project']]) {
    const rejected = spawnSync(process.execPath, [entry, ...args], { env, windowsHide: true });
    assert.equal(rejected.status, 1);
    assert.equal(JSON.parse(rejected.stderr).code, 'ARGUMENT');
  }
  assert.deepEqual(await fs.readdir(f.home), []);
});
