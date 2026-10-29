import * as fs from 'node:fs/promises';
import path from 'node:path';
import os from 'node:os';
import { execFile } from 'node:child_process';
import { promisify, parseArgs } from 'node:util';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { architecture } from './launcher.mjs';

const execute = promisify(execFile);
const packageRoot = fileURLToPath(new URL('../../../', import.meta.url));
const repair = 'Reinstall a complete trusted Rondo npm archive.';
const help = 'rondo self-check [--json]\nCheck this npm package and its host CLI without changing Agent configuration.\nRuns bounded local probes; does not discover clients or start an MCP session.';

async function regular(file) {
  const entry = await fs.lstat(file);
  if (!entry.isFile() || entry.isSymbolicLink() || !entry.size) throw new Error('Missing regular file');
  return entry;
}

export async function selfCheck(context = {}) {
  const root = context.root ?? packageRoot, run = context.execute ?? execute;
  const platform = context.platform ?? process.platform, machine = context.machine ?? os.machine();
  const node = context.nodeVersion ?? process.versions.node;
  const checks = [];
  const result = { format: 1, command: 'self-check', status: 'error', package_version: null,
    platform, machine, architecture: null, node_version: node, runtime_verification: 'not_run', client_discovery: 'not_run', checks };
  const check = async (id, operation, code, message, remedy) => {
    try { const details = await operation(); checks.push({ id, status: 'passed', ...(details ? { details } : {}) }); return true; }
    catch { checks.push({ id, status: 'failed', code, message, remedy }); return false; }
  };
  const skip = (id, reason) => checks.push({ id, status: 'skipped', reason });
  const probe = (file, args) => run(file, args, { windowsHide: true, shell: false,
    timeout: 10000, maxBuffer: 1024 * 1024, encoding: 'utf8', cwd: root });
  const supported = await check('platform', () => { result.architecture = architecture(platform, machine); },
    'RONDO_PLATFORM', 'The host platform or architecture is unsupported.', 'Use Windows x86/x64; ARM64 is not supported.');
  const nodeReady = await check('node', () => {
    const parts = /^(\d+)\.(\d+)\.(\d+)$/.exec(node);
    if (!parts || !(Number(parts[1]) > 22 || (Number(parts[1]) === 22 && Number(parts[2]) >= 19))) throw new Error('Unsupported Node');
  }, 'RONDO_NODE', 'Node.js 22.19.0 or newer is required.', 'Run the npm command with a supported Node.js version.');
  const metadata = await check('package', async () => {
    const file = path.join(root, 'package.json');
    if ((await regular(file)).size > 65536) throw new Error('Package metadata too large');
    const bytes = await fs.readFile(file);
    if (bytes.length > 65536) throw new Error('Package metadata too large');
    const pkg = JSON.parse(bytes);
    if (pkg.name !== '@som1ng/rondo' || typeof pkg.version !== 'string'
        || !/^\d+\.\d+\.\d+(?:-[\da-zA-Z.-]+)?$/.test(pkg.version)) throw new Error('Invalid package');
    result.package_version = pkg.version;
  }, 'RONDO_PACKAGE', 'Rondo package metadata is missing or invalid.', repair);
  if (!supported || !nodeReady || !metadata) {
    for (const id of ['cli_distribution', 'cli_runtime', 'installer', 'mcp_modules', 'skill']) skip(id, 'Host or package prerequisites failed.');
    return result;
  }
  const cli = path.join(root, 'runtime', result.architecture, 'rondo.exe');
  let distribution;
  const verified = await check('cli_distribution', async () => {
    const { verifyDistribution } = await import('../../installer/src/distribution.mjs');
    distribution = await verifyDistribution(cli, path.join(path.dirname(cli), 'manifest.json'));
    if (distribution.architecture !== result.architecture) throw new Error('Architecture mismatch');
    return { tool_version: distribution.tool_version, schema_version: distribution.schema_version };
  }, 'DISTRIBUTION_INVALID', 'The host CLI does not match its packaged manifest.', repair);
  if (verified) {
    const ran = await check('cli_runtime', async () => {
      const report = JSON.parse((await probe(cli, ['--version', '--json'])).stdout);
      if (report?.command !== 'version' || report.status !== 'success'
          || report.tool_version !== distribution.tool_version || report.schema_version !== distribution.schema_version
          || report.data?.text !== report.tool_version || report.execution?.operation_completed !== true) throw new Error('CLI contract mismatch');
    }, 'RONDO_CLI_PROBE', 'The verified CLI could not complete its version probe or returned an unexpected result.',
    'Check the matching Microsoft Visual C++ v14 runtime, OS compatibility and application-control diagnostics; this result alone does not identify the cause.');
    result.runtime_verification = ran ? 'passed' : 'failed';
  } else skip('cli_runtime', 'CLI distribution validation failed; the executable was not started.');
  await check('installer', async () => {
    const entry = path.join(root, 'integrations/installer/src/index.mjs'); await regular(entry);
    if (!(await probe(process.execPath, [entry, '--help'])).stdout.startsWith('Rondo Agent installer ')) throw new Error('Installer help mismatch');
  }, 'RONDO_INSTALLER', 'The installer help entry could not start.', repair);
  await check('mcp_modules', async () => {
    await regular(path.join(root, 'integrations/mcp/dist/index.js'));
    const server = pathToFileURL(path.join(root, 'integrations/mcp/dist/server.js')).href;
    const adapter = pathToFileURL(path.join(root, 'integrations/installer/src/mcp.mjs')).href;
    const script = 'const a = await import(process.argv[1]); const b = await import(process.argv[2]); '
      + 'if (typeof a.createServer !== "function" || typeof b.installMcp !== "function") process.exit(1); '
      + 'console.log("rondo-mcp-modules-ok");';
    if ((await probe(process.execPath, ['--input-type=module', '-e', script, server, adapter])).stdout.trim() !== 'rondo-mcp-modules-ok') throw new Error('Module probe mismatch');
  }, 'RONDO_MCP_MODULES', 'MCP modules, bundled dependencies or report schemas could not load.', repair);
  await check('skill', async () => {
    for (const file of ['SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml']) {
      await regular(path.join(root, 'integrations/skills/windows-dll-doctor', file));
    }
  }, 'RONDO_SKILL', 'A required bundled Skill file is missing, empty or linked.', repair);
  result.status = checks.every(item => item.status === 'passed') ? 'success' : 'error';
  return result;
}

export async function runSelfCheck(args) {
  let values;
  try { ({ values } = parseArgs({ args, allowPositionals: false, options: { json: { type: 'boolean' }, help: { type: 'boolean' } } })); }
  catch {
    const error = { format: 1, command: 'self-check', status: 'error', code: 'ARGUMENT', message: 'Use rondo self-check [--json] or --help.' };
    if (args.includes('--json')) console.log(JSON.stringify(error)); else console.error(error.message);
    return 2;
  }
  if (values.help) { console.log(values.json ? JSON.stringify({ command: 'self-check', help }) : help); return 0; }
  const report = await selfCheck();
  if (values.json) console.log(JSON.stringify(report, null, 2));
  else {
    console.log('Rondo npm self-check: ' + report.status);
    for (const item of report.checks) console.log(`[${item.status}] ${item.id}`
      + (item.message ? `: ${item.message} ${item.remedy}` : item.reason ? `: ${item.reason}` : ''));
    console.log('Client discovery and MCP session verification were not run.');
  }
  return report.status === 'success' ? 0 : 1;
}
