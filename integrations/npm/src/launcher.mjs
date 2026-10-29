import path from 'node:path';
import os from 'node:os';
import { fileURLToPath } from 'node:url';

const packageRoot = fileURLToPath(new URL('../../../', import.meta.url));
const installerCommands = new Set(['setup', 'upgrade', 'recover', 'verify', 'uninstall', 'list-agents']);
export const usage = `Rondo — Windows diagnostics and Agent setup
  rondo --version --json
  rondo doctor <file.dll> --json
  rondo self-check [--json]
  rondo setup --agent auto --scope project --verify
  rondo setup --integration mcp --agent auto --scope project --allow-root <directory> --verify
  rondo setup --integration mcp --agent auto --scope user --allow-root <directory> --verify
  rondo upgrade --agent <agent>|all --scope project [--dry-run]
  rondo recover --agent <agent>|all --scope project [--integration mcp] [--dry-run]
  rondo verify|uninstall --agent <agent> --scope project [--integration mcp]
  rondo list-agents
  rondo mcp --allow-root <directory>
  rondo setup --help
The npm package supplies CLI and MCP paths. Native CLI options follow below.`;

export function architecture(platform = process.platform, machine = os.machine()) {
  if (platform === 'win32') {
    if (['x86_64', 'AMD64'].includes(machine)) return 'x64';
    if (['i386', 'i686', 'x86'].includes(machine)) return 'x86';
  }
  throw Object.assign(new Error('Rondo supports Windows x86/x64; ARM64 and other platforms are not supported.'), { code: 'RONDO_PLATFORM' });
}

const has = (args, key) => args.some(arg => arg === key || arg.startsWith(key + '='));
const value = (args, key) => {
  const equal = args.find(arg => arg.startsWith(key + '='));
  return equal ? equal.slice(key.length + 1) : args[args.indexOf(key) + 1];
};

export async function command(args, context = {}) {
  const arch = architecture(context.platform, context.machine);
  const root = context.root || packageRoot;
  const cli = path.join(root, 'runtime', arch, 'rondo.exe');
  const manifest = path.join(root, 'runtime', arch, 'manifest.json');
  const installer = path.join(root, 'integrations/installer/src/index.mjs');
  const server = path.join(root, 'integrations/mcp/dist/index.js');
  const verifyBundled = async () => {
    const { verifyDistribution } = await import('../../installer/src/distribution.mjs');
    if ((await verifyDistribution(cli, manifest)).architecture !== arch) {
      throw Object.assign(new Error('Bundled CLI architecture does not match the selected runtime.'), { code: 'DISTRIBUTION_INVALID' });
    }
  };
  if (installerCommands.has(args[0])) {
    const forwarded = [...args];
    if (['setup', 'upgrade'].includes(args[0]) && !has(args, '--help')) {
      if (!has(args, '--cli')) {
        // Let the installer validate supplied options before it executes anything.
        forwarded.push('--cli', cli);
        if (!has(args, '--manifest')) forwarded.push('--manifest', manifest);
      }
      if (has(args, '--integration') && value(args, '--integration') === 'mcp' && !has(args, '--server')) {
        forwarded.push('--server', server);
      }
    }
    return { file: process.execPath, args: [installer, ...forwarded] };
  }
  if (args[0] === 'mcp') {
    const forwarded = args.slice(1);
    if (!has(forwarded, '--cli')) {
      await verifyBundled();
      forwarded.push('--cli', cli);
    }
    return { file: process.execPath, args: [server, ...forwarded] };
  }
  await verifyBundled();
  return { file: cli, args: [...args] };
}
