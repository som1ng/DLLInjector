import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash } from 'node:crypto';
import { spawn, execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { install } from '../src/installer.mjs';
import { installMcp } from '../src/mcp.mjs';
import { setupAll } from '../src/batch.mjs';
import { setupAllMcp } from '../src/batch-mcp.mjs';
import { upgradeAll } from '../src/batch-upgrade.mjs';
import { recoverAll } from '../src/batch-recovery.mjs';

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const childFile = fileURLToPath(new URL('./fixtures/batch-recovery-child.mjs', import.meta.url));
const entry = fileURLToPath(new URL('../src/index.mjs', import.meta.url));
const privateValue = 'unrelated-private-fixture-never-copy';
const snapshot = paths => Promise.all(paths.map(async file => [file, await fs.readFile(file)]));
async function unchanged(saved) { for (const [file, bytes] of saved) assert.deepEqual(await fs.readFile(file), bytes, file); }
const envelope = bytes => JSON.parse(bytes.toString().split('\n')[0]);

async function fixture(t, integration = 'skill', scope = 'project') {
  const base = await fs.realpath(await fs.mkdtemp(path.join(os.tmpdir(), 'rondo-batch-recovery-')));
  const root = path.join(base, '用户 project 🚀'); await fs.mkdir(root);
  const cli = path.join(base, 'old.exe'), nextCli = path.join(base, 'new.exe'), server = path.join(base, 'old.js'), nextServer = path.join(base, 'new.js');
  const clients = Object.fromEntries(['codex', 'claude', 'opencode'].map(agent => [agent, path.join(base, agent + '.exe')]));
  for (const file of [cli, nextCli, server, nextServer, ...Object.values(clients)]) await fs.writeFile(file, 'fixture ' + path.basename(file));
  const probe = async file => ({ path: file, tool_version: '2.0.0-dev.9', schema_version: '1.6', sha256: hash(await fs.readFile(file)) });
  const context = { home: root, env: {}, probe, probeCli: probe,
    probeClient: async (agent, file) => ({ path: file, version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }),
    checkServer: async () => {},
  };
  const query = { agent: 'all', integration, scope, ...(scope === 'project' ? { project: root } : {}) };
  const options = { ...query, cli, ...(integration === 'mcp' ? { server, clients, allowRoot: [root] } : {}) };
  if (integration === 'mcp') await fs.writeFile(path.join(root, scope === 'project' ? '.mcp.json' : '.claude.json'),
    JSON.stringify({ privateFixture: privateValue, mcpServers: { other: { command: 'other.exe' } } }));
  const targets = (await (integration === 'skill' ? setupAll : setupAllMcp)(options, context)).targets;
  const paths = targets.flatMap(target => target.files ?? [target.config, target.receipt]);
  const before = await snapshot(paths), next = { ...options, cli: nextCli, ...(integration === 'mcp' ? { server: nextServer } : {}) };
  const journal = path.join(root, '.dll-doctor', 'batch-upgrade-' + integration + '-' + scope + '.lock'), children = [];
  const lock = target => target.target ? path.join(path.dirname(target.target), '.dll-doctor-installer.lock')
    : path.join(path.dirname(target.receipt), 'mcp-' + target.agent + '.lock');
  const start = async (command, pause, extra = {}) => {
    const child = spawn(process.execPath, [childFile, JSON.stringify({ command, pause, home: root,
      options: command === 'upgrade' ? next : query, ...extra })], { windowsHide: true, stdio: ['ignore', 'ignore', 'ignore', 'ipc'] });
    let ended = false, timer;
    const closed = new Promise(resolve => child.once('close', () => { ended = true; resolve(); }));
    const stop = async () => { if (!ended) child.kill('SIGKILL'); await closed; }; children.push(stop);
    try {
      await Promise.race([new Promise((resolve, reject) => {
        child.once('error', reject);
        child.once('message', message => message.ready ? resolve() : reject(Object.assign(new Error('Fixture failed'), { code: message.error })));
      }), closed.then(() => { throw new Error('Fixture ended before checkpoint'); }),
      new Promise((_, reject) => { timer = setTimeout(() => reject(new Error('Fixture timeout')), 20000); })]);
    } catch (error) { await stop(); throw error; }
    finally { clearTimeout(timer); }
    return { stop };
  };
  t.after(async () => {
    for (const stop of children) await stop();
    assert.equal(path.dirname(base), await fs.realpath(os.tmpdir())); assert.ok(path.basename(base).startsWith('rondo-batch-recovery-'));
    await fs.rm(base, { recursive: true, maxRetries: 30, retryDelay: 50 });
  });
  return { root, cli, server, query, next, options, context, targets, paths, before, journal, start, lock,
    run: integration === 'skill' ? install : installMcp };
}
async function retry(operation) {
  for (let n = 0; ; n++) try { return await operation(); }
  catch (error) { if (error.code !== 'RECOVERY_BUSY' || n >= 20) throw error; await new Promise(resolve => setTimeout(resolve, 100)); }
}

for (const integration of ['skill', 'mcp']) {
  for (const scope of ['project', 'user']) {
    test(`${integration}/${scope}: killed second target restores the whole batch without old runtimes or private config backups`, async t => {
      const f = await fixture(t, integration, scope);
      const child = await f.start('upgrade', integration === 'skill' ? 'upgraded-file' : 'config-written', { target: 2 }); await child.stop();
      await fs.unlink(f.cli); await fs.unlink(f.server);
      const { payload } = envelope(await fs.readFile(f.journal));
      for (const target of f.targets.slice(0, 2)) {
        const archive = path.join(path.dirname(f.lock(target)), '.dll-doctor-batch-' + payload.id + '-' + target.agent + '.journal');
        const raw = await fs.readFile(archive), record = envelope(raw).payload;
        assert.equal(record.format, 2); // Older format-1 installers must reject batch participants.
        assert.ok(!raw.toString().includes(privateValue));
        if (integration === 'mcp') for (const side of [record.before, record.after]) assert.ok(!Buffer.from(side.receipt, 'base64').toString().includes(privateValue));
        assert.equal((await fs.stat(archive, { bigint: true })).ino, (await fs.stat(f.lock(target), { bigint: true })).ino);
      }
      const partial = await snapshot([...f.paths, f.journal]);
      const noRuntime = { ...f.context, probe: () => { throw new Error('runtime'); }, probeCli: () => { throw new Error('runtime'); },
        probeClient: () => { throw new Error('runtime'); }, checkServer: () => { throw new Error('runtime'); } };
      assert.equal((await recoverAll({ ...f.query, dryRun: true }, noRuntime)).action, 'preview'); await unchanged(partial);
      await assert.rejects(f.run('recover', { ...f.query, agent: 'codex' }, noRuntime), { code: 'BATCH_RECOVERY_REQUIRED' });
      assert.equal((await recoverAll(f.query, noRuntime)).action, 'restored'); await unchanged(f.before);
      assert.equal((await recoverAll(f.query, noRuntime)).action, 'absent');
      assert.equal((await upgradeAll(f.next, f.context)).action, 'upgraded');
    });
  }

  for (const phase of ['batch-prepared', 'prepared', 'temporary-written', 'batch-committed', 'target-cleaned']) {
    test(`${integration}: interruption at ${phase} has a deterministic batch outcome`, async t => {
      const f = await fixture(t, integration), child = await f.start('upgrade', phase); await child.stop();
      const committed = ['batch-committed', 'target-cleaned'].includes(phase), current = await snapshot(f.paths);
      assert.equal((await recoverAll(f.query, f.context)).action, committed ? 'completed' : 'restored');
      await unchanged(committed ? current : f.before);
    });
  }

  test(`${integration}: active owner, concurrent recovery and repeated termination preserve retryability`, async t => {
    const f = await fixture(t, integration), child = await f.start('upgrade', 'target-completed', { agent: 'codex' });
    await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_ACTIVE' });
    await assert.rejects(f.run('upgrade', { ...f.options, agent: 'codex', client: f.options.clients?.codex }, f.context));
    await child.stop();
    const first = await f.start('recover', 'recovery-guard-held');
    await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_BUSY' }); await first.stop();
    const second = await retry(() => f.start('recover', 'target-restored')); await second.stop();
    const third = await retry(() => f.start('recover', 'target-cleaned')); await third.stop();
    assert.equal((await retry(() => recoverAll(f.query, f.context))).action, 'restored'); await unchanged(f.before);
  });

  test(`${integration}: all-target conflict preflight preserves even otherwise recoverable targets`, async t => {
    const f = await fixture(t, integration), child = await f.start('upgrade', 'target-completed', { agent: 'claude' }); await child.stop();
    const edited = f.targets[0].target ? path.join(f.targets[0].target, 'SKILL.md') : f.targets[0].config;
    await fs.appendFile(edited, '\n# later user edit');
    const modified = await snapshot([...f.paths, f.journal]);
    await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_CONFLICT' }); await unchanged(modified);
  });

  test(`${integration}: an unchanged first target is preserved during interrupted batch recovery`, async t => {
    const f = await fixture(t, integration);
    await f.run('upgrade', { ...f.next, agent: 'codex', client: f.options.clients?.codex }, f.context);
    const before = await snapshot(f.paths);
    const child = await f.start('upgrade', integration === 'skill' ? 'upgraded-file' : 'config-written'); await child.stop();
    const partial = await snapshot(f.paths);
    await assert.rejects(upgradeAll(f.next, f.context), { code: 'BATCH_RECOVERY_REQUIRED' }); await unchanged(partial);
    const restored = await recoverAll(f.query, f.context);
    assert.ok(restored.targets.every(target => target.agent !== 'codex')); await unchanged(before);
  });

  test(`${integration}: failure during committed cleanup keeps the upgrade and reports a recoverable cleanup error`, async t => {
    const f = await fixture(t, integration);
    await assert.rejects(f.start('upgrade', 'never', { fail: 'target-cleaned' }), { code: 'BATCH_CLEANUP_REQUIRED' });
    const upgraded = await snapshot(f.paths);
    assert.equal((await recoverAll(f.query, f.context)).action, 'completed'); await unchanged(upgraded);
  });

  for (const phase of ['target-completed', 'batch-committed']) {
    test(`${integration}: edits at ${phase} cannot be reported as a successful batch`, async t => {
      const f = await fixture(t, integration);
      const edited = f.targets[0].target ? path.join(f.targets[0].target, 'SKILL.md') : f.targets[0].config;
      await assert.rejects(upgradeAll(f.next, { ...f.context, batchCheckpoint: async (stage, data) => {
        if (stage === phase && (phase === 'batch-committed' || data.agent === f.targets.at(-1).agent)) await fs.appendFile(edited, '\n# late edit');
      } }), error => {
        assert.equal(error.code, phase === 'batch-committed' ? 'BATCH_CLEANUP_REQUIRED' : 'ROLLBACK_INCOMPLETE'); return true;
      });
      assert.ok((await fs.readFile(edited, 'utf8')).endsWith('# late edit'));
      assert.equal((await fs.readFile(f.journal, 'utf8')).endsWith('committed\n'), phase === 'batch-committed');
    });
  }

  test(`${integration}: interrupted caught-failure rollback restores the batch on retry`, async t => {
    const f = await fixture(t, integration), child = await f.start('upgrade', 'target-restored', { fail: 'target-completed' }); await child.stop();
    assert.equal((await recoverAll(f.query, f.context)).action, 'restored'); await unchanged(f.before);
  });

  test(`${integration}: missing enrolled records, replaced locks and damaged coordinator records are preserved`, async t => {
    const f = await fixture(t, integration), child = await f.start('upgrade', 'target-completed', { agent: 'codex' }); await child.stop();
    const raw = await fs.readFile(f.journal), { payload } = envelope(raw);
    const archive = path.join(path.dirname(f.lock(f.targets[0])), '.dll-doctor-batch-' + payload.id + '-codex.journal');
    const lock = f.lock(f.targets[0]), record = await fs.readFile(archive), partial = await snapshot(f.paths);
    await fs.unlink(archive); await fs.unlink(lock);
    await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_CONFLICT' }); await unchanged(partial);
    await fs.writeFile(archive, record); await fs.writeFile(lock, record); // Same bytes do not prove lock ownership.
    await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_CONFLICT' }); await unchanged(partial);
    await fs.unlink(lock); await fs.link(archive, lock);
    for (const invalid of ['', 'corrupt', raw.toString().replace(payload.id, 'invalid-id')]) {
      await fs.writeFile(f.journal, invalid);
      await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_JOURNAL' }); await unchanged(partial);
    }
    await fs.writeFile(f.journal, raw); await recoverAll(f.query, f.context); await unchanged(f.before);
  });
}

test('partial batch commit marker rolls back; incomplete enrollment never authorizes started writes', async t => {
  const f = await fixture(t, 'mcp'), child = await f.start('upgrade', 'batch-committed'); await child.stop();
  const raw = (await fs.readFile(f.journal)).toString(), partial = await snapshot(f.paths);
  await fs.writeFile(f.journal, raw.replace('codex\nclaude\nopencode\ncommitted\n', 'codex\nclaude\nopen'));
  await assert.rejects(recoverAll(f.query, f.context), { code: 'RECOVERY_JOURNAL' }); await unchanged(partial);
  await fs.writeFile(f.journal, raw.replace('committed\n', 'comm'));
  await recoverAll(f.query, f.context); await unchanged(f.before);
});

test('lost coordinator records are reported as incomplete instead of claiming rollback succeeded', async t => {
  const f = await fixture(t);
  await assert.rejects(upgradeAll(f.next, { ...f.context, batchCheckpoint: async (stage, data) => {
    if (stage === 'target-completed' && data.agent === 'codex') await fs.unlink(f.journal);
  } }), error => { assert.equal(error.code, 'ROLLBACK_INCOMPLETE'); assert.equal(error.details.rollback_status, 'incomplete'); return true; });
  assert.ok(await fs.stat(f.lock(f.targets[0])));
});

test('batch CLI supports Skill and MCP recovery without runtime flags and keeps integrations separate', async t => {
  for (const integration of ['skill', 'mcp']) {
    const f = await fixture(t, integration), child = await f.start('upgrade', 'target-completed', { agent: 'codex' }); await child.stop();
    const args = ['recover', '--agent', 'all', '--scope', 'project', '--project', f.root, '--integration', integration];
    const run = values => execFileSync(process.execPath, [entry, ...values], { windowsHide: true, encoding: 'utf8', stdio: 'pipe' });
    assert.equal(JSON.parse(run([...args, '--dry-run'])).action, 'preview');
    assert.equal((await recoverAll({ ...f.query, integration: integration === 'skill' ? 'mcp' : 'skill' }, f.context)).action, 'absent');
    for (const flags of [['--cli', f.cli], ['--allow-root', f.root], ['--codex-client', f.cli]]) {
      assert.throws(() => run([...args, ...flags]), error => JSON.parse(error.stderr).code === 'ARGUMENT');
    }
    assert.equal(JSON.parse(run(args)).action, 'restored'); await unchanged(f.before);
  }
});
