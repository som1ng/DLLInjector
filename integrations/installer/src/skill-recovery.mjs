import * as fs from 'node:fs/promises';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { InstallError, OWNED, safePath, stat, readSkillTree, validateSkillTree, replaceSkillFile } from './installer.mjs';
import { acquireRecoveryGuard } from './recovery-lock.mjs';

const hash = data => createHash('sha256').update(data).digest('hex');
const MAX_JOURNAL = 4 * 1024 * 1024;
const fail = (code, message) => { throw new InstallError(code, message); };
const lockFor = target => path.join(path.dirname(target), '.dll-doctor-installer.lock');
const temporaryFor = (target, nonce, file) => path.join(path.dirname(path.join(target, file)),
  '.dll-doctor-upgrade-' + nonce + '-' + OWNED.indexOf(file) + '.tmp');
const aliasFor = (target, nonce) => path.join(path.dirname(target), '.dll-doctor-recovery-' + nonce + '.tmp');
const encode = tree => Object.fromEntries(OWNED.map(file => [file, tree[file].toString('base64')]));

export async function acquireSkillUpgrade(target, before, after) {
  const payload = { owner: 'rondo-skill-upgrade', format: 1, pid: process.pid, nonce: randomUUID(), target,
    before: encode(before), after: encode(after) };
  const bytes = Buffer.from(JSON.stringify({ payload, sha256: hash(JSON.stringify(payload)) }) + '\n');
  if (bytes.length > MAX_JOURNAL) fail('RECOVERY_LIMIT', 'Managed Skill content is too large for a recovery record.');
  const alias = aliasFor(target, payload.nonce), lock = lockFor(target);
  await safePath(alias); await safePath(lock);
  const handle = await fs.open(alias, 'wx', 0o600);
  let claimed = false;
  try {
    await handle.writeFile(bytes); await handle.sync();
    await fs.link(alias, lock); // Exclusive, atomic publication of a complete journal.
    claimed = true;
    await fs.unlink(alias);
    return { handle, temporaryFor: file => temporaryFor(target, payload.nonce, file),
      start: async () => { await handle.write('started\n'); await handle.sync(); },
      complete: async () => { await handle.write('committed\n'); await handle.sync(); } };
  } catch (error) {
    await handle.close();
    if (!claimed) await fs.unlink(alias);
    if (error.code === 'EEXIST') fail('LOCKED', 'Another installer owns the Skill lock.');
    throw error;
  }
}

async function bounded(file, maximum = MAX_JOURNAL) {
  await safePath(file);
  const info = await stat(file);
  if (!info?.isFile() || info.size > maximum) fail('RECOVERY_CONFLICT', 'Recovery requires bounded regular files; preserve the local state.');
  const bytes = await fs.readFile(file);
  if (bytes.length > maximum) fail('RECOVERY_CONFLICT', 'Recovery file exceeds its limit.');
  return bytes;
}
function decode(tree) {
  if (!tree || Object.keys(tree).sort().join('|') !== [...OWNED].sort().join('|')) throw new Error();
  const decoded = Object.fromEntries(OWNED.map(file => {
    if (typeof tree[file] !== 'string') throw new Error();
    const bytes = Buffer.from(tree[file], 'base64');
    if (bytes.toString('base64') !== tree[file]) throw new Error();
    return [file, bytes];
  }));
  validateSkillTree(decoded);
  return decoded;
}
async function journal(target) {
  const raw = await bounded(lockFor(target));
  try {
    const text = new TextDecoder('utf-8', { fatal: true }).decode(raw), end = text.indexOf('\n');
    if (end < 0) throw new Error();
    const { payload, sha256 } = JSON.parse(text.slice(0, end)), marker = text.slice(end + 1);
    if (!payload || payload.owner !== 'rondo-skill-upgrade' || payload.format !== 1 || payload.target !== target
        || !Number.isInteger(payload.pid) || payload.pid <= 0 || payload.pid > 0x7fffffff
        || typeof payload.nonce !== 'string' || !/^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(payload.nonce)
        || sha256 !== hash(JSON.stringify(payload))
        || !['started\ncommitted\n', 'committed\n'].some(sequence => sequence.startsWith(marker))) throw new Error();
    return { raw, pid: payload.pid, nonce: payload.nonce, before: decode(payload.before), after: decode(payload.after),
      started: marker.startsWith('started\n'), committed: marker.endsWith('committed\n') };
  } catch { fail('RECOVERY_JOURNAL', 'Skill recovery record is missing, legacy, damaged or for another target; preserve it for inspection.'); }
}
function requireExited(pid) {
  try { process.kill(pid, 0); }
  catch (error) { if (error.code === 'ESRCH') return; }
  fail('RECOVERY_ACTIVE', 'The recorded owner is still running or cannot be checked; recovery will not take its lock.');
}

async function plan(target) {
  const state = await journal(target);
  requireExited(state.pid);
  const temporary = OWNED.map(file => temporaryFor(target, state.nonce, file));
  const ignored = temporary.map(file => path.relative(target, file).split(path.sep).join('/'));
  for (const file of OWNED) await bounded(path.join(target, file), Math.max(state.before[file].length, state.after[file].length));
  const current = await readSkillTree(target, ignored), leftovers = [];
  for (const file of OWNED) {
    if (!current[file]?.equals(state.before[file]) && !current[file]?.equals(state.after[file])) {
      fail('RECOVERY_CONFLICT', 'Skill content changed outside the interrupted upgrade; preserving it.');
    }
    if (state.committed && !current[file].equals(state.after[file])) fail('RECOVERY_CONFLICT', 'Committed Skill content changed; preserving it.');
    if (!state.started && !state.committed && !current[file].equals(state.before[file])) {
      fail('RECOVERY_CONFLICT', 'Skill changed before this operation began writing; preserving it.');
    }
    const temp = temporaryFor(target, state.nonce, file);
    if (await stat(temp)) {
      if (!state.started) fail('RECOVERY_CONFLICT', 'A temporary file predates this operation; preserving it.');
      const bytes = await bounded(temp, Math.max(state.before[file].length, state.after[file].length));
      if (![state.before[file], state.after[file]].some(expected => expected.subarray(0, bytes.length).equals(bytes))) {
        fail('RECOVERY_CONFLICT', 'An upgrade temporary file has unknown content; preserving it.');
      }
      leftovers.push([temp, bytes]);
    }
  }
  const alias = aliasFor(target, state.nonce);
  if (await stat(alias)) {
    const bytes = await bounded(alias), a = await fs.stat(alias, { bigint: true }), b = await fs.stat(lockFor(target), { bigint: true });
    if (!bytes.equals(state.raw) || a.ino !== b.ino || a.dev !== b.dev) fail('RECOVERY_CONFLICT', 'The journal alias is not owned by this operation.');
    leftovers.push([alias, bytes]);
  }
  return { ...state, current, leftovers };
}

export async function recoverSkill(target, options, context = {}) {
  const lock = lockFor(target);
  const result = { status: 'success', command: 'recover', integration: 'skill', agent: options.agent, scope: options.scope,
    target, journal: lock, runtime_verification: 'not_run' };
  if (!await stat(lock)) return { ...result, action: 'absent' };
  const inspected = await plan(target);
  if (options.dryRun) return { ...result, action: 'preview', recovery_action: inspected.committed ? 'keep_committed' : 'restore_previous',
    files: OWNED.map(file => path.join(target, file)), temporary_files: inspected.leftovers.map(([file]) => file) };
  const guardPath = path.join(path.dirname(target), '.dll-doctor-recover.guard');
  await safePath(guardPath);
  let guard;
  try { guard = await acquireRecoveryGuard(guardPath); }
  catch { fail('RECOVERY_BUSY', 'Another recovery holds the guard, or Windows PowerShell/file locking is unavailable.'); }
  try {
    const state = await plan(target);
    if (!state.raw.equals(inspected.raw)) fail('RECOVERY_CONFLICT', 'Recovery record changed; preserving it.');
    const check = async () => {
      try { guard.check(); } catch { fail('RECOVERY_BUSY', 'Recovery guard was lost; the recovery record has been preserved.'); }
      if (!(await bounded(lock)).equals(state.raw)) fail('RECOVERY_CONFLICT', 'Recovery record changed during restoration.');
    };
    await context.recoveryCheckpoint?.('guard-held');
    for (const [file, bytes] of state.leftovers) {
      await check();
      if (!(await bounded(file)).equals(bytes)) fail('RECOVERY_CONFLICT', 'A temporary file changed during recovery.');
      await fs.unlink(file);
    }
    if (!state.committed) for (const file of OWNED) {
      await check();
      if (!state.current[file].equals(state.before[file])) {
        await replaceSkillFile(path.join(target, file), state.current[file], state.before[file], temporaryFor(target, state.nonce, file));
        await context.recoveryCheckpoint?.('restored-file', { file });
      }
    }
    const restored = validateSkillTree(await readSkillTree(target)).tree;
    const expected = state.committed ? state.after : state.before;
    if (!OWNED.every(file => restored[file].equals(expected[file]))) fail('RECOVERY_CONFLICT', 'Skill changed during recovery.');
    await check(); await fs.unlink(lock);
    return { ...result, action: state.committed ? 'completed' : 'restored' };
  } finally { await guard.release(); }
}
