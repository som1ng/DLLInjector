import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createHash, randomUUID } from 'node:crypto';
import { isDeepStrictEqual } from 'node:util';
import { destination, install, InstallError, safePath, stat, makeParents } from './installer.mjs';
import { acquireRecoveryGuard } from './recovery-lock.mjs';

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const fail = (code, message) => { throw new InstallError(code, message); };
const uuid = value => typeof value === 'string' && /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(value);
// Keep each hard link beside its target lock, including custom user destinations on another volume.
const archive = (layout, id, agent) => path.join(path.dirname(layout.targets.find(target => target.agent === agent).lock),
  '.dll-doctor-batch-' + id + '-' + agent + '.journal');
const alias = (layout, id) => path.join(path.dirname(layout.journal), '.dll-doctor-recovery-' + id + '.tmp');
async function bytes(file, limit = 4 * 1024 * 1024 + 32) {
  await safePath(file);
  const info = await stat(file);
  if (!info) return null;
  if (!info.isFile() || info.size > limit) fail('RECOVERY_CONFLICT', 'Batch recovery requires bounded regular records.');
  const data = await fs.readFile(file);
  if (data.length > limit) fail('RECOVERY_CONFLICT', 'Batch recovery record exceeds its size limit.');
  return data;
}
function envelope(raw) {
  const text = new TextDecoder('utf-8', { fatal: true }).decode(raw), end = text.indexOf('\n');
  if (end < 0) throw new Error();
  const { payload, sha256 } = JSON.parse(text.slice(0, end));
  if (!payload || hash(JSON.stringify(payload)) !== sha256) throw new Error();
  return { payload, marker: text.slice(end + 1), headerLength: Buffer.byteLength(text.slice(0, end + 1)) };
}
async function sameFile(left, right) {
  await safePath(left); await safePath(right);
  const a = await fs.stat(left, { bigint: true }), b = await fs.stat(right, { bigint: true });
  if (a.ino !== b.ino || a.dev !== b.dev) fail('RECOVERY_CONFLICT', 'A batch target lock was replaced; preserving it.');
}

// Only the coordinator supplies this internal context; CLI users must recover the batch as a whole.
export function batchDecision(state, batch) {
  if (!state.batch && !batch) return false;
  if (!batch) fail('BATCH_RECOVERY_REQUIRED', 'This target belongs to a batch; use recover --agent all with the same integration and scope.');
  if (!state.batch || state.batch.id !== batch.id || state.batch.journal !== batch.journal || state.pid !== batch.pid
      || !['rollback', 'committed', 'rolledback'].includes(batch.decision)) fail('RECOVERY_JOURNAL', 'Target record does not belong to this batch.');
  if (batch.decision === 'committed' && !state.committed) fail('RECOVERY_JOURNAL', 'A committed batch has an uncommitted target record.');
  state.committed = batch.decision === 'committed';
  state.expectBefore = batch.decision === 'rolledback';
  return batch.owner === true && batch.pid === process.pid;
}

async function layoutFor(options, context) {
  const integration = options.integration ?? 'skill';
  if (options.agent !== 'all' || !['skill', 'mcp'].includes(integration) || !['project', 'user'].includes(options.scope)
      || (options.scope === 'user' && options.project !== undefined)) fail('ARGUMENT', 'Batch recovery requires all, a supported integration and project/user scope.');
  const root = path.resolve(options.scope === 'user' ? context.home ?? os.homedir() : options.project ?? process.cwd());
  await safePath(root);
  if (!(await stat(root))?.isDirectory()) fail('ROOT', 'The batch project/user root must exist.');
  const targets = [];
  for (const agent of integration === 'mcp' ? ['codex', 'claude', 'opencode'] : ['codex', 'claude']) {
    if (integration === 'skill') {
      const { target } = destination({ ...options, agent }, context.home, context.env);
      targets.push({ agent, target, lock: path.join(path.dirname(target), '.dll-doctor-installer.lock') });
    } else {
      const { root: configRoot, relative } = await (await import('./mcp.mjs')).location({ ...options, agent }, context);
      const state = path.join(configRoot, '.dll-doctor');
      targets.push({ agent, config: path.join(configRoot, relative),
        receipt: path.join(state, 'mcp-' + (options.scope === 'user' ? 'user-' : '') + agent + '.json'),
        lock: path.join(state, 'mcp-' + agent + '.lock') });
    }
  }
  return { integration, scope: options.scope, targets,
    journal: path.join(root, '.dll-doctor', 'batch-upgrade-' + integration + '-' + options.scope + '.lock') };
}

async function loadBatch(layout) {
  const raw = await bytes(layout.journal, 65536);
  if (!raw) return null;
  try {
    const { payload, marker, headerLength } = envelope(raw);
    if (payload.owner !== 'rondo-batch-upgrade' || payload.format !== 1 || !uuid(payload.id)
        || !Number.isInteger(payload.pid) || payload.pid <= 0 || payload.pid > 0x7fffffff
        || payload.integration !== layout.integration || payload.scope !== layout.scope
        || !isDeepStrictEqual(payload.targets, layout.targets)) throw new Error();
    const lines = marker.split('\n'), partial = lines.pop(), enrolled = [];
    let terminal;
    for (const line of lines) {
      if (terminal) throw new Error();
      if (['committed', 'rolledback'].includes(line)) terminal = line;
      else {
        const index = layout.targets.findIndex(target => target.agent === line);
        const previous = layout.targets.findIndex(target => target.agent === enrolled.at(-1));
        if (index < 0 || index <= previous) throw new Error();
        enrolled.push(line);
      }
    }
    const next = layout.targets.slice(layout.targets.findIndex(target => target.agent === enrolled.at(-1)) + 1).map(target => target.agent);
    if (partial && (terminal || ![...next, 'committed', 'rolledback'].some(token => (token + '\n').startsWith(partial)))) throw new Error();
    return { ...payload, raw, enrolled, terminal, stableLength: headerLength + Buffer.byteLength(marker.slice(0, marker.length - partial.length)) };
  } catch { fail('RECOVERY_JOURNAL', 'Batch record is damaged or its destinations changed; preserve it for inspection.'); }
}

async function targetRecords(layout, state) {
  const records = [];
  for (const target of layout.targets) {
    const saved = archive(layout, state.id, target.agent), savedBytes = await bytes(saved), lockBytes = await bytes(target.lock);
    const raw = savedBytes ?? lockBytes;
    let found;
    if (raw) {
      try { found = envelope(raw); }
      catch { if (savedBytes || state.enrolled.includes(target.agent)) fail('RECOVERY_JOURNAL', 'A batch target record is damaged.'); }
    }
    const owned = found?.payload.batch?.id === state.id && found.payload.batch.journal === layout.journal && found.payload.pid === state.pid;
    if (!owned) {
      if (savedBytes || (state.enrolled.includes(target.agent) && !state.terminal)) fail('RECOVERY_CONFLICT', 'An enrolled batch record is missing or replaced.');
      continue; // Never reached or already cleaned after a terminal marker; leave unrelated locks alone.
    }
    if ((!savedBytes || !state.enrolled.includes(target.agent)) && found.marker.startsWith('started\n')) {
      fail('RECOVERY_JOURNAL', 'A writing target has no complete batch enrollment record.');
    }
    if (savedBytes && lockBytes) await sameFile(saved, target.lock);
    records.push({ ...target, saved: savedBytes ? saved : undefined, recordPath: savedBytes ? saved : target.lock,
      raw, listed: state.enrolled.includes(target.agent) });
  }
  return records;
}

export async function assertNoPendingBatch(options, context) {
  const layout = await layoutFor(options, context);
  if (await stat(layout.journal)) fail('BATCH_RECOVERY_REQUIRED', 'A batch record already exists; use recover --agent all with the same integration and scope.');
  return layout;
}

export async function beginBatch(options, context) {
  const layout = await assertNoPendingBatch(options, context);
  for (const target of layout.targets) if (await stat(target.lock)) fail('BATCH_LOCKED', 'A target already has an installer lock; preserve and inspect it before upgrading all.');
  await makeParents(path.dirname(layout.journal), []);
  const payload = { owner: 'rondo-batch-upgrade', format: 1, id: randomUUID(), pid: process.pid,
    integration: layout.integration, scope: layout.scope, targets: layout.targets };
  const temporary = alias(layout, payload.id), raw = Buffer.from(JSON.stringify({ payload, sha256: hash(JSON.stringify(payload)) }) + '\n');
  if (raw.length > 65000) fail('RECOVERY_LIMIT', 'Batch destinations exceed the recovery record limit.');
  await safePath(temporary); await safePath(layout.journal);
  const handle = await fs.open(temporary, 'wx', 0o600);
  let claimed = false;
  try {
    await handle.writeFile(raw); await handle.sync(); await fs.link(temporary, layout.journal); claimed = true;
    await fs.unlink(temporary);
  } catch (error) { await handle.close(); if (!claimed) await fs.unlink(temporary); throw error; }
  const current = async () => {
    const state = await loadBatch(layout);
    if (!state || state.id !== payload.id || state.pid !== payload.pid) fail('RECOVERY_JOURNAL', 'The batch coordinator record disappeared or was replaced.');
    return state;
  };
  return {
    info: { id: payload.id, journal: layout.journal },
    capture: async agent => {
      await current();
      const target = layout.targets.find(item => item.agent === agent), saved = archive(layout, payload.id, agent);
      await safePath(target.lock); await safePath(saved);
      await fs.link(target.lock, saved);
      await handle.write(agent + '\n'); await handle.sync();
    },
    check: async () => {
      const state = await current(), records = await targetRecords(layout, state);
      for (const record of records) if (record.saved && !await stat(record.lock)) fail('BATCH_LOCK_LOST', 'A batch target lock disappeared before commit.');
    },
    commit: async () => { await handle.write('committed\n'); await handle.sync(); },
    close: () => handle.close(),
  };
}

export async function recoverAll(options, context = {}) {
  if (['cli', 'manifest', 'server', 'client', 'allowRoot', 'select'].some(key => options[key] !== undefined)
      || options.allowProcesses || options.verify || Object.keys(options.clients ?? {}).length) fail('ARGUMENT', 'Batch recovery does not accept runtime or selection options.');
  const layout = await layoutFor(options, context), initial = await loadBatch(layout);
  const result = { status: 'success', command: 'recover', agent: 'all', integration: layout.integration, scope: layout.scope,
    journal: layout.journal, runtime_verification: 'not_run', client_discovery: 'not_run' };
  if (!initial) return { ...result, action: 'absent', targets: [] };
  const owner = context.batchOwner?.id === initial.id && initial.pid === process.pid;
  if (!owner) {
    let exited = false;
    try { process.kill(initial.pid, 0); } catch (error) { exited = error.code === 'ESRCH'; }
    if (!exited) fail('RECOVERY_ACTIVE', 'The batch owner is running or cannot be checked; preserving its records.');
  }
  const run = layout.integration === 'skill' ? install : (await import('./mcp.mjs')).installMcp;
  let guard;
  const targets = [], errors = [];
  try {
    if (!options.dryRun) {
      const guardPath = layout.journal + '.recover.guard'; await safePath(guardPath);
      guard = owner ? { check() {}, release: async () => {} } : await acquireRecoveryGuard(guardPath);
    }
    const state = await loadBatch(layout);
    if (!state || !state.raw.equals(initial.raw)) fail('RECOVERY_CONFLICT', 'Batch record changed during inspection.');
    const records = await targetRecords(layout, state), decision = state.terminal ?? (owner && context.batchExpected === 'committed' ? 'committed' : 'rollback');
    const query = record => ({ agent: record.agent, scope: options.scope, project: options.project });
    const recoveryContext = record => ({ ...context, batchRecovery: { id: state.id, journal: layout.journal, pid: state.pid,
      owner, decision, guard, recordPath: record.recordPath } });
    const check = async () => {
      guard?.check();
      if (!(await bytes(layout.journal, 65536))?.equals(state.raw)) fail('RECOVERY_CONFLICT', 'Batch record changed during recovery.');
    };
    // Validate every participant before recovery writes or lock claims.
    const ready = [];
    for (const record of records) {
      try { await run('recover', { ...query(record), dryRun: true }, recoveryContext(record)); ready.push(record); }
      catch (error) { errors.push({ agent: record.agent, action: 'failed', code: error.code || 'INSTALL_FAILED' }); }
    }
    if (errors.length) {
      const error = new InstallError('RECOVERY_CONFLICT', 'A batch target cannot be recovered; no target files were changed.');
      error.details = { targets: errors, journal: layout.journal }; throw error;
    }
    if (options.dryRun) return { ...result, action: 'preview', recovery_action: decision === 'committed' ? 'keep_committed' : 'restore_previous',
      targets: ready.map(record => ({ agent: record.agent, target: record.target, config: record.config, receipt: record.receipt })) };
    await context.batchCheckpoint?.('recovery-guard-held');
    for (const record of ready) {
      await check();
      if (!await stat(record.lock)) await fs.link(record.recordPath, record.lock);
      await sameFile(record.recordPath, record.lock);
    }
    for (const record of [...ready].reverse()) {
      try {
        await check();
        const restored = await run('recover', query(record), recoveryContext(record));
        if (record.listed) targets.push({ agent: record.agent, action: restored.action, target: record.target, config: record.config, receipt: record.receipt });
        await context.batchCheckpoint?.('target-restored', { agent: record.agent });
      } catch (error) { errors.push({ agent: record.agent, action: 'failed', code: error.code || 'INSTALL_FAILED' }); break; }
    }
    if (errors.length) {
      if (owner) return { ...result, action: 'incomplete', targets: [...targets, ...errors] };
      const error = new InstallError('ROLLBACK_INCOMPLETE', 'Batch recovery was interrupted by a conflict; preserve records and retry after inspection.');
      error.details = { targets: [...targets, ...errors], journal: layout.journal }; throw error;
    }
    await check();
    if (!state.terminal) {
      const handle = await fs.open(layout.journal, 'r+');
      try { await handle.truncate(state.stableLength); await handle.write('rolledback\n', state.stableLength); await handle.sync(); }
      finally { await handle.close(); }
      state.raw = await bytes(layout.journal, 65536);
    }
    await context.batchCheckpoint?.('batch-settled');
    for (const record of ready) {
      await check(); await sameFile(record.recordPath, record.lock);
      if (!(await bytes(record.recordPath))?.equals(record.raw)) fail('RECOVERY_CONFLICT', 'A target record changed during cleanup.');
      await fs.unlink(record.lock);
      if (record.saved) await fs.unlink(record.saved);
      await context.batchCheckpoint?.('target-cleaned', { agent: record.agent });
    }
    const pendingAlias = alias(layout, state.id);
    if (await stat(pendingAlias)) { await sameFile(pendingAlias, layout.journal); await fs.unlink(pendingAlias); }
    await check(); await fs.unlink(layout.journal);
    return { ...result, action: decision === 'committed' ? 'completed' : 'restored', targets };
  } finally { await guard?.release(); }
}
