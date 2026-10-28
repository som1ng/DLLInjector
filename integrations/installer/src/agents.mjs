import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { createInterface } from 'node:readline/promises';
import { InstallError, install } from './installer.mjs';

const names = ['codex', 'claude', 'opencode'];
const localAbsolute = value => typeof value === 'string' && /^[a-z]:[\\/]/i.test(value);
const fail = (code, message, discovery) => {
  const error = new InstallError(code, message);
  if (discovery) error.details = { discovery };
  throw error;
};

async function file(candidate, warnings) {
  try {
    const resolved = await fs.realpath(candidate);
    return (await fs.stat(resolved)).isFile() ? resolved : null;
  } catch (error) {
    if (!['ENOENT', 'ENOTDIR'].includes(error.code)) warnings.push({ path: candidate, code: error.code });
    return null;
  }
}

function npmExecutables(agent, arch) {
  if (agent === 'claude') return ['node_modules/@anthropic-ai/claude-code/bin/claude.exe'];
  if (agent === 'opencode') return ['node_modules/opencode-ai/bin/opencode.exe'];
  if (!['x64', 'arm64'].includes(arch)) return [];
  const target = arch === 'x64' ? 'x86_64-pc-windows-msvc' : 'aarch64-pc-windows-msvc';
  const binary = `vendor/${target}/bin/codex.exe`;
  return [
    `node_modules/@openai/codex/node_modules/@openai/codex-win32-${arch}/${binary}`,
    `node_modules/@openai/codex-win32-${arch}/${binary}`,
    `node_modules/@openai/codex/${binary}`,
  ];
}

// Inspect only known locations. Never parse/execute a shell shim or load a package.
export async function discoverAgents(context = {}) {
  const env = context.env ?? process.env, home = context.home ?? os.homedir();
  const arch = context.arch ?? process.arch;
  const environment = name => Object.entries(env).find(([key]) => key.toUpperCase() === name)?.[1];
  const directories = new Map();
  function add(value, source) {
    if (!localAbsolute(value)) return;
    const directory = path.normalize(value), key = directory.toLowerCase();
    if (!directories.has(key)) directories.set(key, { directory, source });
  }
  for (const entry of (environment('PATH') ?? '').split(';')) {
    const value = entry.startsWith('"') && entry.endsWith('"') ? entry.slice(1, -1) : entry;
    add(value, 'path');
  }
  if (localAbsolute(home)) add(path.join(home, '.local/bin'), 'user_bin');
  if (localAbsolute(environment('APPDATA'))) add(path.join(environment('APPDATA'), 'npm'), 'user_npm');
  const agents = names.map(agent => ({ agent, candidates: [] })), warnings = [];
  for (const { directory, source } of directories.values()) {
    for (const record of agents) {
      const direct = await file(path.join(directory, record.agent + '.exe'), warnings);
      let candidate = direct, kind = source;
      if (!candidate && (await file(path.join(directory, record.agent + '.cmd'), warnings)
          || await file(path.join(directory, record.agent + '.ps1'), warnings))) {
        for (const relative of npmExecutables(record.agent, arch)) {
          candidate = await file(path.join(directory, relative), warnings);
          if (candidate) { kind = source === 'user_npm' ? source : source + '_npm'; break; }
        }
      }
      if (candidate && !record.candidates.some(item => item.client.toLowerCase() === candidate.toLowerCase())) {
        record.candidates.push({ client: candidate, source: kind });
      }
    }
  }
  return { status: 'success', command: 'list-agents', platform: process.platform, node_architecture: arch,
    node_version: process.version, method: 'filesystem_candidates', executable_probes: 'not_run', agents, warnings };
}

async function promptCandidate(candidates) {
  process.stderr.write('Detected client candidates (versions not checked):\n');
  candidates.forEach((item, index) => process.stderr.write(`${index + 1}. ${item.agent}: ${item.client}\n`));
  const terminal = createInterface({ input: process.stdin, output: process.stderr });
  try {
    const answer = (await terminal.question('Choose one number to install, or Enter to cancel: ')).trim();
    return /^[1-9][0-9]*$/.test(answer) ? Number(answer) - 1 : undefined;
  } catch { return undefined; }
  finally { terminal.close(); }
}

export async function setupAuto(options, context = {}) {
  const integration = options.integration ?? 'skill';
  if (!['skill', 'mcp'].includes(integration) || !['user', 'project'].includes(options.scope)
      || (options.scope === 'user' && options.project !== undefined)) {
    fail('ARGUMENT', 'Choose project/user scope; --project is not valid for user scope.');
  }
  if (options.select !== undefined && !names.includes(options.select)) fail('ARGUMENT', '--select must name codex, claude or opencode.');
  if (options.client !== undefined && options.select === undefined) fail('ARGUMENT', 'Use --select with --client to disambiguate a detected client.');
  const discovery = await (context.discover ?? discoverAgents)();
  const candidates = discovery.agents.flatMap(item => item.candidates.map(candidate => ({ agent: item.agent, ...candidate })));
  if (!candidates.length) fail('NO_AGENTS', 'No supported native client candidate found. Use explicit --agent and, for MCP, --client.', discovery);
  let selected;
  if (options.select !== undefined) {
    let matches = candidates.filter(item => item.agent === options.select);
    if (options.client !== undefined) {
      if (!localAbsolute(options.client) || path.extname(options.client).toLowerCase() !== '.exe') fail('ARGUMENT', '--client must be an absolute local EXE path.');
      const client = await file(options.client, []);
      matches = matches.filter(item => item.client.toLowerCase() === client?.toLowerCase());
    }
    if (matches.length !== 1) fail('AGENT_SELECTION', 'Selected client is absent or ambiguous. Run list-agents; select an exact --client path or use explicit --agent.', discovery);
    selected = matches[0];
  } else if (options.dryRun) {
    return { status: 'success', command: 'setup', action: 'selection_required', integration, discovery,
      message: 'Choose --select <agent> to preview its installation. No programs started or files written.' };
  } else {
    if (!context.choose && (!process.stdin.isTTY || !process.stderr.isTTY)) {
      fail('AGENT_SELECTION_REQUIRED', 'Run list-agents, then setup --agent auto --select <agent>; noninteractive setup never chooses a client automatically.', discovery);
    }
    const index = await (context.choose ?? promptCandidate)(candidates);
    if (!Number.isInteger(index) || !candidates[index]) fail('AGENT_SELECTION_CANCELLED', 'No client selected; no installation was started.');
    selected = candidates[index];
  }
  const run = context.run ?? (integration === 'mcp' ? (await import('./mcp.mjs')).installMcp : install);
  const result = await run('setup', { ...options, agent: selected.agent,
    client: integration === 'mcp' ? selected.client : undefined });
  return { ...result, agent_detection: { ...selected, method: discovery.method, executable_probes: 'not_run' } };
}
