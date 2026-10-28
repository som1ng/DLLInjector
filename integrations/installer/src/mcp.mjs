import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { randomUUID } from 'node:crypto';
import { execFile } from 'node:child_process';
import { promisify, isDeepStrictEqual } from 'node:util';
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import { InstallError, safePath, stat, makeParents, cliPath, probeCli } from './installer.mjs';
import { verifyDistribution } from './distribution.mjs';
import { SERVER_NAME, hash, inspectConfig, addEntry, updateEntry, removeEntry, assertOwned, mcpEntry } from './mcp-config.mjs';

const execute = promisify(execFile);
const versions = { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' };
const toolNames = ['analyze_dependencies', 'diagnose_dll', 'inspect_pe', 'inspect_process', 'list_modules', 'list_processes', 'read_report'];
const json = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const same = (left, right) => left === null || right === null ? left === right : left.equals(right);
const fail = (code, message) => { throw new InstallError(code, message); };

async function location(options, context) {
  let root = path.resolve(options.project || process.cwd());
  let relative = options.agent === 'codex' ? '.codex/config.toml' : options.agent === 'claude' ? '.mcp.json' : 'opencode.json';
  if (options.scope === 'user') {
    if (options.project !== undefined) fail('MCP_ARGUMENT', '--project is not valid with --scope user.');
    const home = context.home ?? os.homedir(), env = context.env ?? process.env;
    if (!/^[a-z]:[\\/]/i.test(home)) fail('MCP_PATH', 'User home must be an absolute local directory.');
    root = path.resolve(home);
    await safePath(root);
    if (!(await stat(root))?.isDirectory()) fail('MCP_PATH', 'User home must exist.');
    if (options.agent === 'codex' && env.CODEX_HOME) {
      if (!/^[a-z]:[\\/]/i.test(env.CODEX_HOME)) fail('MCP_PATH', 'CODEX_HOME must be an absolute local directory.');
      const custom = path.resolve(env.CODEX_HOME);
      await safePath(custom);
      if (!(await stat(custom))?.isDirectory()) fail('MCP_PATH', 'CODEX_HOME must already exist.');
      // Share the project lock if both scopes can address the same .codex/config.toml.
      const standard = path.basename(custom).toLowerCase() === '.codex';
      root = standard ? path.dirname(custom) : custom;
      relative = standard ? '.codex/config.toml' : 'config.toml';
    } else if (options.agent === 'claude') {
      if (env.CLAUDE_CONFIG_DIR) fail('MCP_CUSTOM_CONFIG', 'Custom CLAUDE_CONFIG_DIR is not yet supported for user MCP.');
      relative = '.claude.json';
    } else if (options.agent === 'opencode') {
      if (['XDG_CONFIG_HOME', 'OPENCODE_CONFIG', 'OPENCODE_CONFIG_DIR', 'OPENCODE_CONFIG_CONTENT'].some(key => env[key])) {
        fail('MCP_CUSTOM_CONFIG', 'Custom OpenCode configuration overrides are not yet supported for user MCP.');
      }
      root = path.join(root, '.config', 'opencode');
    }
  }
  await safePath(root);
  if (options.scope === 'project' && !(await stat(root))?.isDirectory()) fail('MCP_PATH', 'Project directory must exist.');
  if (options.agent === 'opencode' && await stat(path.join(root, 'opencode.jsonc'))) {
    if (await stat(path.join(root, relative))) fail('MCP_CONFIG', 'Both OpenCode config files exist; resolve the ambiguity first.');
    relative = 'opencode.jsonc';
  }
  return { root, relative };
}

export async function read(file, limit = 1048576) {
  await safePath(file);
  const info = await stat(file);
  if (!info) return null;
  if (!info.isFile() || info.size > limit) fail('MCP_FILE', 'Expected a bounded regular configuration/receipt file.');
  const data = await fs.readFile(file);
  if (data.length > limit) fail('MCP_FILE', 'Configuration/receipt exceeds its size limit.');
  return data;
}
export function text(data) {
  if (data === null) return null;
  try { return new TextDecoder('utf-8', { fatal: true, ignoreBOM: true }).decode(data); }
  catch { fail('MCP_CONFIG', 'Configuration must use UTF-8.'); }
}

async function localFile(value, extension) {
  if (typeof value !== 'string' || !/^[a-z]:[\\/]/i.test(value)
      || path.extname(value).toLowerCase() !== extension) fail('MCP_PATH', 'Provide absolute local native client, CLI and built server paths.');
  const file = await fs.realpath(value);
  if (!(await fs.stat(file)).isFile()) fail('MCP_PATH', 'Required executable/server file is missing.');
  return file;
}

export async function probeClient(agent, file) {
  let value;
  try { value = (await execute(file, ['--version'], { windowsHide: true, timeout: 10000, maxBuffer: 4096 })).stdout.trim(); }
  catch { fail('MCP_CLIENT', 'Client version probe failed; provide its installed native EXE.'); }
  const match = /^(?:codex-cli\s+)?([0-9]+\.[0-9]+\.[0-9]+)(?:\s+\(Claude Code\))?$/.exec(value);
  if (!match || match[1] !== versions[agent]) fail('MCP_CLIENT_VERSION', 'This adapter is tested with ' + agent + ' ' + versions[agent] + '.');
  return { path: file, version: match[1] };
}

export async function checkServer(runtime) {
  const entry = mcpEntry('codex', runtime);
  const env = Object.fromEntries(Object.entries(process.env).filter(([key]) =>
    ['PATH', 'SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA'].includes(key.toUpperCase())));
  const transport = new StdioClientTransport({ command: entry.command, args: entry.args, env, stderr: 'ignore' });
  const client = new Client({ name: 'dll-doctor-installer', version: '0.1.0' });
  try {
    await client.connect(transport, { timeout: 10000 });
    const listed = await client.listTools({}, { timeout: 10000 });
    if (client.getServerVersion()?.name !== 'dll-doctor' || listed.nextCursor
        || !isDeepStrictEqual(listed.tools.map(tool => tool.name).sort(), toolNames)
        || listed.tools.some(tool => tool.annotations?.readOnlyHint !== true || tool.annotations?.destructiveHint !== false)) {
      fail('MCP_VERIFY', 'Unexpected MCP server identity or tool catalog.');
    }
  } catch { fail('MCP_VERIFY', 'MCP initialization/tool verification failed; check the built adapter and CLI.'); }
  finally { await transport.close(); }
}

async function runtimeInputs(options, context, dryRun) {
  const cli = await cliPath(options.cli);
  let distribution;
  if (options.manifest !== undefined) {
    try { distribution = await verifyDistribution(cli, options.manifest); }
    catch (error) {
      if (error.code === 'DISTRIBUTION_INVALID') fail(error.code, error.message);
      throw error;
    }
  }
  const server = await localFile(options.server, '.js');
  const client = await localFile(options.client, '.exe');
  if (!options.allowRoot?.length) fail('MCP_ROOTS', 'Supply at least one --allow-root directory.');
  const roots = [];
  for (const value of options.allowRoot) {
    if (!/^[a-z]:[\\/]/i.test(value)) fail('MCP_ROOTS', 'Allowed roots must be absolute local directories.');
    const root = await fs.realpath(value);
    if (!(await fs.stat(root)).isDirectory()) fail('MCP_ROOTS', 'Allowed root must be a directory.');
    if (!roots.includes(root)) roots.push(root);
  }
  const runtime = { node: process.execPath, server: { path: server, sha256: hash(await fs.readFile(server)) },
    cli: { path: cli }, client: { path: client }, roots, allowProcesses: options.allowProcesses === true };
  if (!dryRun) {
    runtime.cli = await (context.probeCli || probeCli)(cli);
    if (runtime.cli.tool_version !== '2.0.0-dev.9') fail('MCP_CLI_VERSION', 'The MCP adapter requires CLI dev.9.');
    if (distribution) {
      if (distribution.sha256 !== runtime.cli.sha256 || distribution.tool_version !== runtime.cli.tool_version
          || distribution.schema_version !== runtime.cli.schema_version) fail('DISTRIBUTION_MISMATCH', 'CLI differs from its distribution manifest.');
      runtime.cli.distribution = distribution;
    }
    runtime.client = await (context.probeClient || probeClient)(options.agent, client);
    await (context.checkServer || checkServer)(runtime);
  }
  return runtime;
}

async function verifyRuntime(agent, runtime, context) {
  if (runtime.node !== process.execPath || hash(await fs.readFile(runtime.server.path)) !== runtime.server.sha256
      || hash(await fs.readFile(runtime.cli.path)) !== runtime.cli.sha256) fail('MCP_RUNTIME_CHANGED', 'Recorded Node/server/CLI changed; review and reinstall explicitly.');
  const cli = await (context.probeCli || probeCli)(await cliPath(runtime.cli.path));
  const client = await (context.probeClient || probeClient)(agent, await localFile(runtime.client.path, '.exe'));
  if (cli.tool_version !== runtime.cli.tool_version || cli.schema_version !== runtime.cli.schema_version
      || cli.sha256 !== runtime.cli.sha256 || client.version !== runtime.client.version) fail('MCP_RUNTIME_CHANGED', 'Runtime version changed.');
  await (context.checkServer || checkServer)(runtime);
}

// Only our computed configuration paths reach this function; compare bytes before replacing.
export async function replace(file, before, after, made, temporary, checkpoint) {
  if (!same(await read(file), before)) fail('MCP_CONCURRENT', 'Configuration changed during the operation; preserving it.');
  if (after === null) { if (before !== null) await fs.unlink(file); return; }
  await makeParents(path.dirname(file), made);
  await safePath(file);
  const temp = temporary ?? path.join(path.dirname(file), '.dll-doctor-config-' + randomUUID() + '.tmp');
  await safePath(temp);
  const handle = await fs.open(temp, 'wx', before === null ? 0o600 : (await fs.stat(file)).mode & 0o777);
  try {
    await handle.writeFile(after);
    await checkpoint?.('temporary-written', { file });
    await handle.close();
    if (!same(await read(file), before)) fail('MCP_CONCURRENT', 'Configuration changed during the operation; preserving it.');
    if (before === null) await fs.link(temp, file); // exclusive creation; never replace a newly appeared file
    else await fs.rename(temp, file);
  } finally {
    await handle.close();
    if (await stat(temp)) await fs.unlink(temp);
  }
}

export function parseReceipt(bytes, agent, scope, relative) {
  try {
    const record = JSON.parse(text(bytes));
    if (!record || record.owner !== 'dll-doctor-installer' || record.format !== 1 || record.agent !== agent
        || (record.scope ?? 'project') !== scope || record.config !== relative || !record.runtime || !record.ownership
        || typeof record.ownership.configCreated !== 'boolean' || typeof record.ownership.containerCreated !== 'boolean'
        || !/^[0-9a-f]{64}$/.test(record.ownership.installedHash)
        || !isDeepStrictEqual(record.entry, mcpEntry(agent, record.runtime))) throw new Error();
    return record;
  } catch { fail('MCP_RECEIPT', 'MCP ownership receipt is invalid.'); }
}

export async function installMcp(command, options, context = {}) {
  if (!['setup', 'upgrade', 'recover', 'verify', 'uninstall', 'rollback-upgrade'].includes(command) || !Object.hasOwn(versions, options.agent)
      || !['project', 'user'].includes(options.scope)) fail('MCP_ARGUMENT', 'MCP mode requires a supported agent and --scope project|user.');
  const bindingCommand = ['setup', 'upgrade'].includes(command);
  if (!bindingCommand && (options.cli !== undefined || options.server !== undefined || options.client !== undefined
      || options.manifest !== undefined || options.allowRoot !== undefined || options.allowProcesses || options.verify)) {
    fail('MCP_ARGUMENT', 'Runtime arguments are only valid for setup/upgrade.');
  }
  if (command === 'verify' && options.dryRun) fail('MCP_ARGUMENT', 'verify does not accept --dry-run.');
  const { root, relative } = await location(options, context);
  const configPath = path.join(root, relative), stateDir = path.join(root, '.dll-doctor');
  const receiptPath = path.join(stateDir, 'mcp-' + (options.scope === 'user' ? 'user-' : '') + options.agent + '.json');
  const lockPath = path.join(stateDir, 'mcp-' + options.agent + '.lock');
  const recoveryLocation = { config: configPath, receipt: receiptPath, lock: lockPath, relative };
  const upgradeRollback = context.rollbackUpgrade;
  if ((command === 'rollback-upgrade') !== !!upgradeRollback || (upgradeRollback
      && (upgradeRollback.config !== configPath || upgradeRollback.receipt !== receiptPath))) {
    fail('MCP_ARGUMENT', 'Batch upgrade restoration requires its original in-memory configuration and receipt.');
  }
  const rollback = context.rollbackState;
  if (rollback && (command !== 'uninstall' || rollback.config !== configPath || rollback.receipt !== receiptPath)) {
    fail('MCP_ARGUMENT', 'Batch rollback must match the original configuration and receipt paths.');
  }
  if (command === 'recover') return (await import('./mcp-recovery.mjs')).recoverMcp(recoveryLocation, options, context);
  const configBefore = await read(configPath), receiptBefore = await read(receiptPath, 65536);
  const source = text(configBefore);
  const parsed = inspectConfig(options.agent, source ?? (options.agent === 'codex' ? '' : '{}'));
  let record;
  if (receiptBefore !== null) {
    record = parseReceipt(receiptBefore, options.agent, options.scope, relative);
    assertOwned(options.agent, source ?? '', record);
  } else if (parsed.entry !== undefined) fail('MCP_CONFLICT', 'The existing dll_doctor entry is not owned by this installer.');
  if (['upgrade', 'rollback-upgrade'].includes(command) && !record) fail('MCP_NOT_INSTALLED', 'No managed MCP installation exists.');
  const result = { status: 'success', integration: 'mcp', command, agent: options.agent, scope: options.scope,
    config: configPath, receipt: receiptPath, client_discovery: 'not_run' };
  if (command === 'verify') {
    if (!record) fail('MCP_NOT_INSTALLED', 'No managed MCP installation exists.');
    await verifyRuntime(options.agent, record.runtime, context);
    return { ...result, action: 'verified', server_verification: 'handshake_and_tools' };
  }
  if (command === 'uninstall' && !record) return { ...result, action: options.dryRun ? 'preview' : 'absent' };
  let configAfter, receiptAfter, runtime;
  if (upgradeRollback) {
    if (same(configBefore, upgradeRollback.configBefore) && same(receiptBefore, upgradeRollback.receiptBefore)) {
      return { ...result, action: 'restored' };
    }
    if (!same(configBefore, upgradeRollback.configAfter) || !same(receiptBefore, upgradeRollback.receiptAfter)) {
      fail('MCP_CONCURRENT', 'Configuration changed after this batch upgrade; preserving it.');
    }
    configAfter = upgradeRollback.configBefore;
    receiptAfter = upgradeRollback.receiptBefore;
  } else if (bindingCommand) {
    runtime = await runtimeInputs(options, context, options.dryRun);
    const entry = mcpEntry(options.agent, runtime);
    if (record && command === 'setup') {
      if (!isDeepStrictEqual(entry, record.entry) || (!options.dryRun && !isDeepStrictEqual(runtime, record.runtime))) {
        fail('MCP_CONFLICT', 'A different managed runtime/policy exists; use upgrade explicitly.');
      }
      return { ...result, action: options.dryRun ? 'preview' : 'unchanged', managed_entry: entry };
    }
    if (record && !options.dryRun && isDeepStrictEqual(runtime, record.runtime)) {
      return { ...result, action: 'unchanged', managed_entry: entry, server_verification: 'handshake_and_tools' };
    }
    const added = record ? updateEntry(options.agent, source, record, entry) : addEntry(options.agent, source, entry);
    configAfter = Buffer.from(added.text);
    receiptAfter = json({ owner: 'dll-doctor-installer', format: 1, agent: options.agent, scope: options.scope, config: relative,
      entry, runtime, ownership: added.ownership });
  } else {
    const removed = removeEntry(options.agent, source, record);
    configAfter = removed === null ? null : Buffer.from(removed);
    // Restore exact original bytes only if nobody has edited the configuration
    // since this batch installed it. Otherwise preserve later unrelated fields.
    if (rollback && same(configBefore, rollback.configAfter)) configAfter = rollback.configBefore;
    receiptAfter = null;
  }
  if (options.dryRun) return { ...result, action: 'preview', executable_probes: 'not_run',
    ...(runtime ? { managed_entry: mcpEntry(options.agent, runtime) } : {}) };
  const made = [], journal = [];
  let lock, recovery, keepRecovery = false;
  try {
    await makeParents(stateDir, made);
    await safePath(lockPath);
    if (command === 'upgrade' || upgradeRollback) {
      recovery = await (await import('./mcp-recovery.mjs')).acquireMcpUpgrade(recoveryLocation, options,
        { config: configBefore, receipt: receiptBefore }, { config: configAfter, receipt: receiptAfter });
      lock = recovery.handle;
      await context.recoveryCheckpoint?.('prepared');
    } else {
      try { lock = await fs.open(lockPath, 'wx'); }
      catch (error) { if (error.code === 'EEXIST') fail('MCP_LOCKED', 'Another or interrupted MCP installation owns the local lock.'); throw error; }
    }
    if (!same(await read(configPath), configBefore) || !same(await read(receiptPath), receiptBefore)) fail('MCP_CONCURRENT', 'Configuration changed during preflight.');
    if (rollback && !same(receiptBefore, rollback.receiptAfter)) fail('MCP_CONCURRENT', 'Another installation replaced the batch entry; preserving it.');
    if (command === 'upgrade') await context.captureUpgrade?.({ config: configPath, receipt: receiptPath,
      configBefore, receiptBefore, configAfter, receiptAfter });
    await recovery?.start();
    for (const [file, before, after] of [[configPath, configBefore, configAfter], [receiptPath, receiptBefore, receiptAfter]]) {
      journal.push({ file, before, after });
      await replace(file, before, after, made, recovery?.temporaryFor(file), context.recoveryCheckpoint);
      await context.checkpoint?.(file === configPath ? 'config-written' : 'receipt-written',
        command === 'setup' && file === receiptPath
          ? { config: configPath, receipt: receiptPath, configBefore, configAfter, receiptAfter } : undefined);
    }
    if (bindingCommand && (options.verify || command === 'upgrade')) await verifyRuntime(options.agent, runtime, context);
    await recovery?.complete();
    await context.recoveryCheckpoint?.('committed');
    return { ...result, action: upgradeRollback ? 'restored' : command === 'upgrade' ? 'upgraded' : command === 'setup' ? 'installed' : 'removed',
      ...(bindingCommand ? { server_verification: 'handshake_and_tools' } : {}) };
  } catch (error) {
    let failed = false;
    for (const step of journal.reverse()) {
      try {
        if (!same(await read(step.file), step.before)) await replace(step.file, step.after, step.before, made, recovery?.temporaryFor(step.file));
      } catch { failed = true; }
    }
    if (failed) {
      keepRecovery = !!recovery;
      fail('ROLLBACK_INCOMPLETE', 'MCP rollback preserved concurrent edits or encountered an access error; inspect the reported configuration and .dll-doctor state.');
    }
    throw error;
  } finally {
    if (lock) { await lock.close(); if (!keepRecovery) await fs.unlink(lockPath); }
    for (const directory of made.reverse()) {
      try { await fs.rmdir(directory); }
      catch (error) { if (!['ENOTEMPTY', 'EEXIST', 'ENOENT'].includes(error.code)) throw error; }
    }
  }
}
