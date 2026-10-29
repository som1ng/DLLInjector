import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { installMcp } from '../src/mcp.mjs';
import { hash } from '../src/mcp-config.mjs';

const childFile = fileURLToPath(new URL('./fixtures/mcp-recovery-child.mjs', import.meta.url));
const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
const privateFixture = 'external-credential-fixture-never-copy';
const initial = {
  codex: '# keep comment\nmodel = "keep"\n[mcp_servers.other.env]\nTOKEN = "' + privateFixture + '"\n',
  claude: '{"custom":"keep","mcpServers":{"other":{"env":{"TOKEN":"' + privateFixture + '"}}}}\n',
  opencode: '{\n // keep comment\n "custom":"keep", "mcp":{"other":{"environment":{"TOKEN":"' + privateFixture + '"}}},\n}\n',
};
async function files(paths) { return Promise.all(paths.map(async file => [file, await fs.readFile(file)])); }
async function unchanged(snapshot) { for (const [file, bytes] of snapshot) assert.deepEqual(await fs.readFile(file), bytes, file); }
const envelope = bytes => JSON.parse(bytes.toString('utf8').split('\n')[0]);
const serialized = (data, marker = '') => JSON.stringify({ payload: data.payload, sha256: hash(JSON.stringify(data.payload)) }) + '\n' + marker;

async function fixture(t, agent = 'codex', scope = 'project', customCodex = false) {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-mcp-recovery-')));
  const root = path.join(base, '用户 project 🚀'); await fs.mkdir(root);
  const custom = path.join(base, 'custom config'); if (customCodex) await fs.mkdir(custom);
  const cli = path.join(base, 'old.exe'), server = path.join(base, 'old.js'), client = path.join(base, 'client.exe');
  const nextCli = path.join(base, 'new.exe'), nextServer = path.join(base, 'new.js');
  for (const file of [cli, server, client, nextCli, nextServer]) await fs.writeFile(file, 'fixture only ' + path.basename(file));
  const config = customCodex ? path.join(custom, 'config.toml') : path.join(root, agent === 'codex' ? '.codex/config.toml'
    : agent === 'claude' ? (scope === 'user' ? '.claude.json' : '.mcp.json')
      : scope === 'user' ? '.config/opencode/opencode.jsonc' : 'opencode.jsonc');
  await fs.mkdir(path.dirname(config), { recursive: true }); await fs.writeFile(config, initial[agent]);
  const query = { agent, scope, ...(scope === 'project' ? { project: root } : {}) };
  const options = { ...query, cli, server, client, allowRoot: [root] };
  const context = { home: root, env: customCodex ? { CODEX_HOME: custom } : {},
    probeCli: async file => ({ path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) }),
    probeClient: async (name, file) => ({ path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[name] }),
    checkServer: async () => {},
  };
  const installed = await installMcp('setup', options, context), paths = [installed.config, installed.receipt];
  const before = await files(paths), lock = path.join(path.dirname(installed.receipt), 'mcp-' + agent + '.lock');
  const next = { ...options, cli: nextCli, server: nextServer, allowProcesses: true }, children = [];
  t.after(async () => {
    for (const stop of children) await stop();
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('rondo-mcp-recovery-'));
    await fs.rm(base, { recursive: true, maxRetries: 20, retryDelay: 50 });
  });
  const start = async (command, pause, extra = {}) => {
    const child = spawn(process.execPath, [childFile, JSON.stringify({ command, pause, home: root, env: context.env,
      options: command === 'upgrade' ? next : query, ...extra })], { windowsHide: true, stdio: ['ignore', 'ignore', 'ignore', 'ipc'] });
    let ended = false, timer;
    const closed = new Promise(resolve => child.once('close', () => { ended = true; resolve(); }));
    const stop = async () => { if (!ended) child.kill('SIGKILL'); await closed; };
    children.push(stop);
    try {
      await Promise.race([new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('message', message => message.ready ? resolve() : reject(Object.assign(new Error('Fixture failed'), { code: message.error })));
      }), closed.then(() => { throw new Error('Fixture exited before checkpoint'); }),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('Fixture timed out')), 20000); })]);
    } finally { clearTimeout(timer); }
    return { stop };
  };
  return { base, root, cli, server, nextCli, nextServer, installed, paths, before, lock, options, next, query, context, start };
}
async function retry(operation) {
  for (let attempt = 0; ; attempt++) {
    try { return await operation(); }
    catch (error) {
      if (error.code !== 'RECOVERY_BUSY' || attempt >= 20) throw error;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
}

for (const agent of ['codex', 'claude', 'opencode']) for (const scope of ['project', 'user']) {
  test(`MCP ${agent}/${scope}: killed upgrade restores exact bytes without backing up unrelated credentials or running obsolete files`, async t => {
    const f = await fixture(t, agent, scope), child = await f.start('upgrade', 'config-written');
    await child.stop();
    await fs.unlink(f.cli); await fs.unlink(f.server);
    const raw = await fs.readFile(f.lock), { payload } = envelope(raw);
    assert.ok(!raw.toString().includes(privateFixture));
    for (const state of [payload.before, payload.after]) {
      assert.ok(!Buffer.from(state.receipt, 'base64').toString().includes(privateFixture));
      assert.ok(!state.entry.includes(privateFixture));
      assert.deepEqual(Object.keys(state).sort(), ['config_sha256', 'entry', 'receipt']);
    }
    const partial = await files([...f.paths, f.lock]);
    const context = { ...f.context, probeCli: () => { throw new Error('Recovery executed CLI'); },
      probeClient: () => { throw new Error('Recovery executed client'); }, checkServer: () => { throw new Error('Recovery executed server'); } };
    const preview = await installMcp('recover', { ...f.query, dryRun: true }, context);
    assert.equal(preview.action, 'preview'); assert.equal(preview.runtime_verification, 'not_run');
    assert.equal(await fs.stat(f.lock + '.recover.guard').catch(() => null), null); await unchanged(partial);
    assert.equal((await installMcp('recover', f.query, context)).action, 'restored'); await unchanged(f.before);
    assert.equal((await installMcp('recover', f.query, context)).action, 'absent');
    assert.equal((await installMcp('upgrade', f.next, f.context)).action, 'upgraded');
    await installMcp('verify', f.query, f.context);
  });
}

for (const phase of ['prepared', 'temporary-written', 'receipt-written', 'committed']) {
  test(`MCP termination at ${phase} recovers the recorded write phase`, async t => {
    const f = await fixture(t), child = await f.start('upgrade', phase); await child.stop();
    assert.equal((await installMcp('recover', f.query, f.context)).action, phase === 'committed' ? 'completed' : 'restored');
    if (phase === 'committed') assert.equal(JSON.parse(await fs.readFile(f.installed.receipt)).runtime.cli.path, f.nextCli);
    else await unchanged(f.before);
  });
}

test('MCP partial temporary content and a same-inode journal alias can be recovered', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'temporary-written'); await child.stop();
  const { payload } = envelope(await fs.readFile(f.lock));
  const temp = path.join(path.dirname(f.installed.config), '.dll-doctor-config-' + payload.nonce + '-config.tmp');
  const bytes = await fs.readFile(temp); await fs.writeFile(temp, bytes.subarray(0, 12));
  const alias = path.join(path.dirname(f.lock), '.dll-doctor-recovery-' + payload.nonce + '.tmp'); await fs.link(f.lock, alias);
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(f.before);
  for (const file of [temp, alias, f.lock]) assert.equal(await fs.stat(file).catch(() => null), null);
});

test('MCP partial commit markers restore the old state, while inactive records cannot undo external replacements', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'committed'); await child.stop();
  const data = envelope(await fs.readFile(f.lock));
  await fs.writeFile(f.lock, serialized(data));
  const committed = await files([...f.paths, f.lock]);
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' }); await unchanged(committed);
  await fs.writeFile(f.lock, serialized(data, 'started\ncomm'));
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(f.before);
});

test('MCP live owner is preserved and recovery can be killed twice then retried', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'receipt-written');
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_ACTIVE' }); await child.stop();
  const first = await f.start('recover', 'guard-held');
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_BUSY' }); await first.stop();
  const second = await retry(() => f.start('recover', 'restored-file')); await second.stop();
  assert.equal((await retry(() => installMcp('recover', f.query, f.context))).action, 'restored'); await unchanged(f.before);
});

for (const agent of ['codex', 'claude', 'opencode']) {
  test(`MCP ${agent}: later unrelated settings are preserved and block recovery`, async t => {
    const f = await fixture(t, agent), child = await f.start('upgrade', 'config-written'); await child.stop();
    await fs.writeFile(f.installed.config, (await fs.readFile(f.installed.config, 'utf8')).replace('keep', 'later'));
    const changed = await files([...f.paths, f.lock]);
    await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' }); await unchanged(changed);
  });
}

test('MCP legacy/corrupt/transplanted records, missing files and unknown temporaries never authorize changes', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'temporary-written'); await child.stop();
  const raw = await fs.readFile(f.lock), original = envelope(raw);
  const bad = ['', 'legacy owner', raw.toString().replace(original.sha256, '0'.repeat(64))];
  for (const mutate of [data => { data.payload.config = f.base; }, data => { data.payload.scope = 'user'; }]) {
    const data = structuredClone(original); mutate(data); bad.push(serialized(data));
  }
  for (const bytes of bad) {
    await fs.writeFile(f.lock, bytes);
    await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_JOURNAL' }); await unchanged(f.before);
    assert.equal(await fs.readFile(f.lock, 'utf8'), bytes);
  }
  await fs.writeFile(f.lock, raw);
  const temp = path.join(path.dirname(f.installed.config), '.dll-doctor-config-' + original.payload.nonce + '-config.tmp');
  await fs.writeFile(temp, 'unknown user data');
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' });
  assert.equal(await fs.readFile(temp, 'utf8'), 'unknown user data');
  await fs.unlink(temp); await fs.symlink(f.base, temp, 'junction');
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'LINKED_PATH' });
  await fs.unlink(temp); await fs.unlink(f.installed.receipt);
  await assert.rejects(installMcp('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' });
});

test('MCP custom CODEX_HOME user recovery resolves its own target', async t => {
  const f = await fixture(t, 'codex', 'user', true), child = await f.start('upgrade', 'config-written'); await child.stop();
  const wrong = { agent: 'codex', scope: 'project', project: f.root };
  assert.equal((await installMcp('recover', wrong, f.context)).action, 'absent');
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(f.before);
});

test('MCP project/user aliases of the same config cannot recover each other', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'config-written'); await child.stop();
  const partial = await files([...f.paths, f.lock]);
  await assert.rejects(installMcp('recover', { agent: 'codex', scope: 'user' }, f.context), { code: 'RECOVERY_JOURNAL' });
  await unchanged(partial);
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(f.before);
});

test('MCP recovery of an interrupted internal batch rollback restores that step, not the entire batch', async t => {
  const f = await fixture(t); let captured;
  await installMcp('upgrade', f.next, { ...f.context, captureUpgrade: value => { captured = value; } });
  const upgraded = await files(f.paths);
  const rollback = Object.fromEntries(Object.entries(captured).map(([key, value]) => [key, Buffer.isBuffer(value) ? value.toString('base64') : value]));
  const child = await f.start('rollback-upgrade', 'config-written', { rollback }); await child.stop();
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(upgraded);
});

test('MCP refuses to persist unsupported receipt fields and restores receipt-only runtime upgrades', async t => {
  const f = await fixture(t), original = await fs.readFile(f.installed.receipt);
  const record = JSON.parse(original); record.credentials = privateFixture;
  await fs.writeFile(f.installed.receipt, JSON.stringify(record));
  await assert.rejects(installMcp('upgrade', f.next, f.context), { code: 'MCP_RECOVERY_RECEIPT' });
  assert.equal(await fs.stat(f.lock).catch(() => null), null);
  await fs.writeFile(f.installed.receipt, original); await fs.appendFile(f.cli, 'new runtime bytes');
  const child = await f.start('upgrade', 'receipt-written', { options: f.options }); await child.stop();
  assert.equal((await installMcp('recover', f.query, f.context)).action, 'restored'); await unchanged(f.before);
});

test('CLI MCP recovery requires no runtime and rejects runtime options and auto', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'config-written'); await child.stop();
  const args = ['recover', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project', '--project', f.root];
  const run = values => execFileSync(process.execPath, [entry, ...values], { windowsHide: true, encoding: 'utf8', stdio: 'pipe' });
  assert.equal(JSON.parse(run([...args, '--dry-run'])).action, 'preview');
  assert.equal(JSON.parse(run(args)).action, 'restored'); await unchanged(f.before);
  for (const values of [['recover', '--integration', 'mcp', '--agent', 'auto', '--scope', 'user'], [...args, '--client', f.options.client],
    [...args, '--allow-root', f.root], [...args, '--cli', f.cli]]) {
    assert.throws(() => run(values), error => ['ARGUMENT', 'MCP_ARGUMENT'].includes(JSON.parse(error.stderr).code));
  }
});

test('MCP upgrade rejects a configuration that would exceed the recovery read limit before writing', async t => {
  const f = await fixture(t), current = await fs.readFile(f.installed.config);
  await fs.writeFile(f.installed.config, Buffer.concat([current, Buffer.from('#' + 'x'.repeat(1048576 - current.length - 3) + '\n')]));
  const before = await files(f.paths);
  await assert.rejects(installMcp('upgrade', f.next, f.context), { code: 'RECOVERY_LIMIT' });
  await unchanged(before); assert.equal(await fs.stat(f.lock).catch(() => null), null);
});
