import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { spawn, execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { install } from '../src/installer.mjs';

const childFile = fileURLToPath(new URL('./fixtures/recovery-child.mjs', import.meta.url));
const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
const hash = value => createHash('sha256').update(value).digest('hex');
async function fixture(t, agent = 'codex', scope = 'project') {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-recovery-')));
  const root = path.join(base, '用户 project 🚀');
  await fs.mkdir(root);
  const cli = path.join(base, 'old.exe'), nextCli = path.join(base, 'new.exe');
  await fs.writeFile(cli, 'old fixture'); await fs.writeFile(nextCli, 'new fixture');
  const options = { agent, scope, ...(scope === 'project' ? { project: root } : {}), cli };
  const query = { agent, scope, ...(scope === 'project' ? { project: root } : {}) };
  const context = { home: root, env: {}, probe: async file => ({ path: file, tool_version: '2.0.0-dev.9',
    schema_version: '1.6', sha256: hash(await fs.readFile(file)) }) };
  const installed = await install('setup', options, context);
  const before = await Promise.all(installed.files.map(async file => [file, await fs.readFile(file)]));
  const lock = path.join(path.dirname(installed.target), '.dll-doctor-installer.lock');
  const children = [];
  t.after(async () => {
    for (const child of children) await child.stop();
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir()));
    assert.ok(path.basename(base).startsWith('rondo-recovery-'));
    await fs.rm(base, { recursive: true, maxRetries: 20, retryDelay: 50 });
  });
  const start = async (command, pause, count) => {
    const child = spawn(process.execPath, [childFile, JSON.stringify({ command, pause, count, home: root,
      options: command === 'upgrade' ? { ...options, cli: nextCli } : query })],
      { windowsHide: true, stdio: ['ignore', 'ignore', 'ignore', 'ipc'] });
    let ended = false;
    const closed = new Promise(resolve => child.once('close', () => { ended = true; resolve(); }));
    const stop = async () => { if (!ended) child.kill('SIGKILL'); await closed; };
    children.push({ stop });
    let timer;
    try {
      await Promise.race([new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('message', message => message.ready ? resolve() : reject(new Error(JSON.stringify(message))));
      }), closed.then(() => { throw new Error('Recovery fixture exited before checkpoint'); }),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('Recovery fixture timed out')), 20000); })]);
    } finally { clearTimeout(timer); }
    return { stop, pid: child.pid };
  };
  return { base, root, cli, nextCli, options, query, context, installed, before, lock, start };
}
async function unchanged(before) {
  for (const [file, bytes] of before) assert.deepEqual(await fs.readFile(file), bytes, file);
}
async function retryRecover(f) {
  for (let attempt = 0; ; attempt++) {
    try { return await install('recover', f.query, f.context); }
    catch (error) {
      if (error.code !== 'RECOVERY_BUSY' || attempt >= 5) throw error;
      await new Promise(resolve => setTimeout(resolve, 100));
    }
  }
}
function envelope(bytes) { return JSON.parse(bytes.toString('utf8').split('\n')[0]); }
function serialized(data, marker = '') {
  data.sha256 = hash(JSON.stringify(data.payload));
  return JSON.stringify(data) + '\n' + marker;
}

for (const agent of ['codex', 'claude', 'opencode']) for (const scope of ['project', 'user']) {
  test(`${agent}/${scope}: a killed upgrade is previewed and restored without executing obsolete runtime`, async t => {
    const f = await fixture(t, agent, scope);
    await fs.unlink(f.cli);
    const child = await f.start('upgrade', 'upgraded-file');
    await child.stop();
    const raw = await fs.readFile(f.lock);
    const partial = await Promise.all(f.installed.files.map(async file => [file, await fs.readFile(file)]));
    const preview = await install('recover', { ...f.query, dryRun: true }, f.context);
    assert.equal(preview.action, 'preview'); assert.equal(preview.recovery_action, 'restore_previous');
    await unchanged(partial); assert.deepEqual(await fs.readFile(f.lock), raw);
    assert.equal((await install('recover', f.query, f.context)).action, 'restored');
    await unchanged(f.before);
    assert.equal(await fs.stat(f.lock).catch(() => null), null);
    assert.equal((await install('recover', f.query, f.context)).action, 'absent');
    assert.equal((await install('upgrade', { ...f.options, cli: f.nextCli }, f.context)).action, 'upgraded');
  });
}

for (const phase of ['prepared', 'temporary-written', 'committed']) {
  test(`termination at ${phase} recovers only the recorded transaction`, async t => {
    const f = await fixture(t);
    const child = await f.start('upgrade', phase);
    await child.stop();
    const result = await install('recover', f.query, f.context);
    assert.equal(result.action, phase === 'committed' ? 'completed' : 'restored');
    if (phase !== 'committed') await unchanged(f.before);
    else assert.equal((await install('verify', f.query, f.context)).cli.path, f.nextCli);
  });
}

test('a partial temporary write and an interrupted journal alias are cleaned only with ownership evidence', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'temporary-written');
  await child.stop();
  const raw = await fs.readFile(f.lock), { payload } = envelope(raw);
  const temp = path.join(f.installed.target, 'references', '.dll-doctor-upgrade-' + payload.nonce + '-4.tmp');
  const contents = await fs.readFile(temp);
  await fs.writeFile(temp, contents.subarray(0, Math.floor(contents.length / 2)));
  const alias = path.join(path.dirname(f.lock), '.dll-doctor-recovery-' + payload.nonce + '.tmp');
  await fs.link(f.lock, alias);
  assert.equal((await install('recover', f.query, f.context)).action, 'restored');
  await unchanged(f.before);
  assert.equal(await fs.stat(temp).catch(() => null), null); assert.equal(await fs.stat(alias).catch(() => null), null);
});

test('a partial commit marker conservatively restores the prior configuration', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'committed');
  await child.stop();
  const data = envelope(await fs.readFile(f.lock));
  await fs.writeFile(f.lock, serialized(data, 'started\ncomm'));
  assert.equal((await install('recover', f.query, f.context)).action, 'restored');
  await unchanged(f.before);
});

test('an inactive journal cannot roll back a replacement that arrived before the first write', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'prepared');
  await child.stop();
  const { payload } = envelope(await fs.readFile(f.lock));
  for (const [file, bytes] of Object.entries(payload.after)) await fs.writeFile(path.join(f.installed.target, file), Buffer.from(bytes, 'base64'));
  const before = await Promise.all([...f.installed.files, f.lock].map(async file => [file, await fs.readFile(file)]));
  await assert.rejects(install('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' });
  await unchanged(before);
});

test('a live owner cannot be recovered or have its lock removed', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'upgraded-file');
  const raw = await fs.readFile(f.lock);
  for (const dryRun of [true, false]) await assert.rejects(install('recover', { ...f.query, dryRun }, f.context), { code: 'RECOVERY_ACTIVE' });
  assert.deepEqual(await fs.readFile(f.lock), raw);
  await child.stop();
  await install('recover', f.query, f.context);
});

test('concurrent recoveries are excluded and killing recovery twice still permits safe retry', async t => {
  const f = await fixture(t), upgrade = await f.start('upgrade', 'upgraded-file', 2);
  await upgrade.stop();
  const first = await f.start('recover', 'guard-held');
  await assert.rejects(install('recover', f.query, f.context), { code: 'RECOVERY_BUSY' });
  await first.stop();
  // Waiting briefly allows the OS helper to observe EOF; no stale guard is deleted.
  await new Promise(resolve => setTimeout(resolve, 250));
  const second = await f.start('recover', 'restored-file');
  await second.stop();
  assert.equal((await retryRecover(f)).action, 'restored');
  await unchanged(f.before);
});

test('changed or extra user files prevent recovery without further writes', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'upgraded-file');
  await child.stop();
  const edited = path.join(f.installed.target, 'SKILL.md');
  await fs.appendFile(edited, 'user edit');
  const before = await Promise.all([...f.installed.files, f.lock].map(async file => [file, await fs.readFile(file)]));
  await assert.rejects(install('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' });
  await unchanged(before);
  await fs.writeFile(edited, f.before.find(([file]) => file === edited)[1]);
  const extra = path.join(f.installed.target, 'user.txt'); await fs.writeFile(extra, 'keep');
  await assert.rejects(install('recover', f.query, f.context), { code: 'CONFLICT' });
  assert.equal(await fs.readFile(extra, 'utf8'), 'keep');
});

test('legacy, damaged, transplanted and path-injecting journals never authorize restoration', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'prepared');
  await child.stop();
  const raw = await fs.readFile(f.lock), original = envelope(raw);
  const invalid = ['', 'legacy owner', raw.toString().replace(original.sha256, '0'.repeat(64))];
  for (const mutate of [data => { data.payload.target = f.base; }, data => { data.payload.before['../user.txt'] = 'eA=='; }]) {
    const data = structuredClone(original); mutate(data); invalid.push(serialized(data));
  }
  for (const bytes of invalid) {
    await fs.writeFile(f.lock, bytes);
    await assert.rejects(install('recover', f.query, f.context), { code: 'RECOVERY_JOURNAL' });
    assert.equal(await fs.readFile(f.lock, 'utf8'), bytes); await unchanged(f.before);
  }
});

test('unknown temporary contents and linked files are preserved', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'temporary-written');
  await child.stop();
  const { payload } = envelope(await fs.readFile(f.lock));
  const temp = path.join(f.installed.target, 'references', '.dll-doctor-upgrade-' + payload.nonce + '-4.tmp');
  await fs.writeFile(temp, 'unrelated content');
  await assert.rejects(install('recover', f.query, f.context), { code: 'RECOVERY_CONFLICT' });
  assert.equal(await fs.readFile(temp, 'utf8'), 'unrelated content');
  await fs.unlink(temp);
  const linked = path.join(f.installed.target, 'foreign'); await fs.symlink(f.base, linked, 'junction');
  await assert.rejects(install('recover', f.query, f.context), { code: 'CONFLICT' });
  assert.ok((await fs.lstat(linked)).isSymbolicLink());
});

test('CLI recovery works without native runtime arguments and rejects MCP/all/auto recovery', async t => {
  const f = await fixture(t), child = await f.start('upgrade', 'upgraded-file');
  await child.stop();
  const args = ['recover', '--agent', 'codex', '--scope', 'project', '--project', f.root];
  const run = values => execFileSync(process.execPath, [entry, ...values], { windowsHide: true, encoding: 'utf8', stdio: 'pipe' });
  assert.equal(JSON.parse(run([...args, '--dry-run'])).action, 'preview');
  assert.equal(JSON.parse(run(args)).action, 'restored'); await unchanged(f.before);
  for (const values of [['recover', '--agent', 'all', '--scope', 'project'], ['recover', '--agent', 'auto', '--scope', 'user'],
    [...args, '--cli', f.nextCli], [...args, '--integration', 'mcp']]) {
    assert.throws(() => run(values), error => ['ARGUMENT', 'RECOVERY_UNSUPPORTED'].includes(JSON.parse(error.stderr).code));
  }
});
