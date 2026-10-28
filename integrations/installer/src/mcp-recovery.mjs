import * as fs from 'node:fs/promises';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { InstallError, safePath, stat } from './installer.mjs';
import { read, text, replace, parseReceipt } from './mcp.mjs';
import { hash, ownedSlice } from './mcp-config.mjs';
import { acquireRecoveryGuard } from './recovery-lock.mjs';

const MAX_JOURNAL = 512 * 1024;
const fail = (code, message) => { throw new InstallError(code, message); };
const temporaryFor = (location, nonce, file) => path.join(path.dirname(file),
  '.dll-doctor-config-' + nonce + '-' + (file === location.config ? 'config' : 'receipt') + '.tmp');
const aliasFor = (location, nonce) => path.join(path.dirname(location.lock), '.dll-doctor-recovery-' + nonce + '.tmp');

function receipt(bytes, options, relative) {
  if (!bytes || bytes.length > 65536) fail('MCP_RECOVERY_RECEIPT', 'Recovery requires a bounded managed receipt.');
  const record = parseReceipt(bytes, options.agent, options.scope, relative), runtime = record.runtime;
  // Do not persist unknown receipt extensions (such as user credentials).
  const allowed = (object, keys) => object && typeof object === 'object' && !Array.isArray(object)
    && Object.keys(object).every(key => keys.includes(key));
  for (const [object, keys] of [
    [record, ['owner', 'format', 'agent', 'scope', 'config', 'entry', 'runtime', 'ownership']],
    [runtime, ['node', 'server', 'cli', 'client', 'roots', 'allowProcesses']],
    [runtime.server, ['path', 'sha256']], [runtime.client, ['path', 'version']],
    [runtime.cli, ['path', 'sha256', 'tool_version', 'schema_version', 'distribution']],
    [record.ownership, ['block', 'entryTextHash', 'configCreated', 'containerCreated', 'installedHash']],
  ]) if (!allowed(object, keys)) fail('MCP_RECOVERY_RECEIPT', 'Receipt has unsupported fields; preserving it without creating a recovery record.');
  if (runtime.cli.distribution !== undefined && !allowed(runtime.cli.distribution,
    ['format', 'manifest_sha256', 'sha256', 'tool_version', 'schema_version', 'architecture', 'source_revision'])) {
    fail('MCP_RECOVERY_RECEIPT', 'Distribution receipt has unsupported fields.');
  }
  if (![runtime.node, runtime.server.path, runtime.cli.path, runtime.client.path].every(value => typeof value === 'string')
      || !Array.isArray(runtime.roots) || !runtime.roots.every(value => typeof value === 'string')
      || typeof runtime.allowProcesses !== 'boolean') fail('MCP_RECOVERY_RECEIPT', 'Receipt runtime values are invalid.');
  for (const metadata of [runtime.server, runtime.cli, runtime.client, runtime.cli.distribution ?? {}]) {
    if (Object.entries(metadata).some(([key, value]) => key !== 'distribution' && value !== null
        && !['string', 'number'].includes(typeof value))) fail('MCP_RECOVERY_RECEIPT', 'Receipt metadata contains unsupported values.');
  }
  return record;
}

export async function acquireMcpUpgrade(location, options, before, after) {
  const snapshots = [before, after].map(state => {
    if (!state.config || state.config.length > 1048576) fail('RECOVERY_LIMIT', 'MCP configuration exceeds the recoverable file limit.');
    const record = receipt(state.receipt, options, location.relative);
    const slice = ownedSlice(options.agent, text(state.config), record);
    return { slice, state: { receipt: state.receipt.toString('base64'), entry: slice.entry, config_sha256: hash(state.config) } };
  });
  if (snapshots[0].slice.prefix !== snapshots[1].slice.prefix || snapshots[0].slice.suffix !== snapshots[1].slice.suffix) {
    fail('MCP_RECOVERY_LAYOUT', 'This update changes text outside the managed entry; preserving configuration.');
  }
  const payload = { owner: 'rondo-mcp-upgrade', format: 1, pid: process.pid, nonce: randomUUID(),
    agent: options.agent, scope: options.scope, config: location.config, receipt: location.receipt,
    before: snapshots[0].state, after: snapshots[1].state };
  const bytes = Buffer.from(JSON.stringify({ payload, sha256: hash(JSON.stringify(payload)) }) + '\n');
  if (bytes.length > MAX_JOURNAL - 32) fail('RECOVERY_LIMIT', 'Managed MCP state exceeds the recovery record limit.');
  const alias = aliasFor(location, payload.nonce);
  await safePath(alias); await safePath(location.lock);
  const handle = await fs.open(alias, 'wx', 0o600);
  let claimed = false;
  try {
    await handle.writeFile(bytes); await handle.sync();
    await fs.link(alias, location.lock);
    claimed = true;
    await fs.unlink(alias);
    return { handle, temporaryFor: file => temporaryFor(location, payload.nonce, file),
      start: async () => { await handle.write('started\n'); await handle.sync(); },
      complete: async () => { await handle.write('committed\n'); await handle.sync(); } };
  } catch (error) {
    await handle.close();
    if (!claimed) await fs.unlink(alias);
    if (error.code === 'EEXIST') fail('MCP_LOCKED', 'Another or interrupted MCP installation owns the local lock.');
    throw error;
  }
}

async function journal(location, options) {
  const raw = await read(location.lock, MAX_JOURNAL);
  try {
    const source = text(raw), end = source.indexOf('\n');
    if (end < 0) throw new Error();
    const { payload, sha256 } = JSON.parse(source.slice(0, end)), marker = source.slice(end + 1);
    if (!payload || payload.owner !== 'rondo-mcp-upgrade' || payload.format !== 1
        || payload.agent !== options.agent || payload.scope !== options.scope
        || payload.config !== location.config || payload.receipt !== location.receipt
        || !Number.isInteger(payload.pid) || payload.pid <= 0 || payload.pid > 0x7fffffff
        || typeof payload.nonce !== 'string' || !/^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(payload.nonce)
        || sha256 !== hash(JSON.stringify(payload))
        || !'started\ncommitted\n'.startsWith(marker)) throw new Error();
    for (const state of [payload.before, payload.after]) {
      if (!state || typeof state.receipt !== 'string' || typeof state.entry !== 'string'
          || !/^[0-9a-f]{64}$/.test(state.config_sha256)) throw new Error();
      state.bytes = Buffer.from(state.receipt, 'base64');
      if (state.bytes.toString('base64') !== state.receipt) throw new Error();
      state.record = receipt(state.bytes, options, location.relative);
    }
    return { ...payload, raw, started: marker.startsWith('started\n'), committed: marker.endsWith('committed\n') };
  } catch { fail('RECOVERY_JOURNAL', 'MCP recovery record is legacy, damaged or for another target; preserve it for inspection.'); }
}

async function plan(location, options) {
  const state = await journal(location, options);
  let exited = false;
  try { process.kill(state.pid, 0); } catch (error) { exited = error.code === 'ESRCH'; }
  if (!exited) fail('RECOVERY_ACTIVE', 'The recorded owner is still running or cannot be checked; preserving its lock.');
  const config = await read(location.config), currentReceipt = await read(location.receipt, 65536);
  if (!config || !currentReceipt) fail('RECOVERY_CONFLICT', 'Configuration or receipt is missing; preserving the local state.');
  const current = [state.before, state.after].find(value => value.config_sha256 === hash(config));
  if (!current || ![state.before, state.after].some(value => value.bytes.equals(currentReceipt))) {
    fail('RECOVERY_CONFLICT', 'MCP configuration or receipt changed outside the interrupted step; preserving it.');
  }
  const slice = ownedSlice(options.agent, text(config), current.record);
  if (slice.entry !== current.entry) fail('RECOVERY_JOURNAL', 'Managed MCP text does not match the recovery record.');
  for (const value of [state.before, state.after]) {
    value.config = Buffer.from(slice.prefix + value.entry + slice.suffix);
    if (hash(value.config) !== value.config_sha256) fail('RECOVERY_JOURNAL', 'MCP recovery record cannot reconstruct the recorded configuration.');
    ownedSlice(options.agent, text(value.config), value.record);
  }
  const matches = value => config.equals(value.config) && currentReceipt.equals(value.bytes);
  if ((state.committed && !matches(state.after)) || (!state.started && !matches(state.before))) {
    fail('RECOVERY_CONFLICT', 'MCP files do not match the recorded write phase; preserving them.');
  }
  const leftovers = [];
  for (const [file, expected] of [[location.config, [state.before.config, state.after.config]],
    [location.receipt, [state.before.bytes, state.after.bytes]]]) {
    const temp = temporaryFor(location, state.nonce, file);
    const bytes = await read(temp, Math.max(...expected.map(value => value.length)));
    if (bytes !== null) {
      if (!state.started || !expected.some(value => value.subarray(0, bytes.length).equals(bytes))) {
        fail('RECOVERY_CONFLICT', 'An MCP temporary file has unknown content; preserving it.');
      }
      leftovers.push([temp, bytes]);
    }
  }
  const alias = aliasFor(location, state.nonce), aliasBytes = await read(alias, MAX_JOURNAL);
  if (aliasBytes !== null) {
    const a = await fs.stat(alias, { bigint: true }), b = await fs.stat(location.lock, { bigint: true });
    if (!aliasBytes.equals(state.raw) || a.ino !== b.ino || a.dev !== b.dev) fail('RECOVERY_CONFLICT', 'Journal alias is not owned by this operation.');
    leftovers.push([alias, aliasBytes]);
  }
  return { ...state, config, currentReceipt, leftovers };
}

export async function recoverMcp(location, options, context = {}) {
  const result = { status: 'success', command: 'recover', integration: 'mcp', agent: options.agent, scope: options.scope,
    config: location.config, receipt: location.receipt, journal: location.lock,
    runtime_verification: 'not_run', client_discovery: 'not_run' };
  if (!await stat(location.lock)) return { ...result, action: 'absent' };
  const inspected = await plan(location, options);
  if (options.dryRun) return { ...result, action: 'preview', recovery_action: inspected.committed ? 'keep_committed' : 'restore_previous',
    files: [location.config, location.receipt], temporary_files: inspected.leftovers.map(([file]) => file) };
  const guardPath = location.lock + '.recover.guard';
  await safePath(guardPath);
  let guard;
  try { guard = await acquireRecoveryGuard(guardPath); }
  catch { fail('RECOVERY_BUSY', 'Another recovery holds the guard, or Windows PowerShell/file locking is unavailable.'); }
  try {
    const state = await plan(location, options);
    if (!state.raw.equals(inspected.raw)) fail('RECOVERY_CONFLICT', 'Recovery record changed; preserving it.');
    const check = async () => {
      try { guard.check(); } catch { fail('RECOVERY_BUSY', 'Recovery guard was lost; the record has been preserved.'); }
      if (!(await read(location.lock, MAX_JOURNAL))?.equals(state.raw)) fail('RECOVERY_CONFLICT', 'MCP recovery record changed during restoration.');
    };
    await context.recoveryCheckpoint?.('guard-held');
    for (const [file, bytes] of state.leftovers) {
      await check();
      if (!(await read(file))?.equals(bytes)) fail('RECOVERY_CONFLICT', 'A temporary file changed during recovery.');
      await fs.unlink(file);
    }
    if (!state.committed) for (const [file, current, before] of [
      [location.config, state.config, state.before.config], [location.receipt, state.currentReceipt, state.before.bytes],
    ]) {
      await check();
      if (!current.equals(before)) {
        await replace(file, current, before, [], temporaryFor(location, state.nonce, file), context.recoveryCheckpoint);
        await context.recoveryCheckpoint?.('restored-file', { file });
      }
    }
    const expected = state.committed ? state.after : state.before;
    if (!(await read(location.config))?.equals(expected.config) || !(await read(location.receipt, 65536))?.equals(expected.bytes)) {
      fail('RECOVERY_CONFLICT', 'MCP files changed during recovery.');
    }
    await check(); await fs.unlink(location.lock);
    return { ...result, action: state.committed ? 'completed' : 'restored' };
  } finally { await guard.release(); }
}
