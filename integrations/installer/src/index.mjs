import { parseArgs } from 'node:util';
import { install, InstallError, VERSION } from './installer.mjs';

try {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: {
    agent: { type: 'string' }, scope: { type: 'string' }, project: { type: 'string' },
    cli: { type: 'string' }, 'dry-run': { type: 'boolean' }, verify: { type: 'boolean' },
    help: { type: 'boolean' }, manifest: { type: 'string' },
    integration: { type: 'string' }, server: { type: 'string' }, client: { type: 'string' },
    'allow-root': { type: 'string', multiple: true }, 'allow-processes': { type: 'boolean' },
  } });
  if (values.help) {
    console.log('DLL Doctor local installer ' + VERSION + '\n'
      + 'setup --agent codex|claude|opencode --scope project|user --cli <absolute.exe> [--manifest <absolute.json>] [--project <dir>] [--dry-run] [--verify]\n'
      + 'verify --agent <agent> --scope <scope> [--project <dir>]\n'
      + 'uninstall --agent <agent> --scope <scope> [--project <dir>] [--dry-run]\n'
      + 'MCP: add --integration mcp; setup also requires --client <native.exe> --server <built.js> --allow-root <dir> (repeatable).\n'
      + 'MCP supports project scope only; --allow-processes is optional. Skill is the default integration.\n'
      + 'version\nPi: use integrations/pi. Local source installer; no downloads.');
  } else if (positionals.length === 1 && positionals[0] === 'version' && Object.keys(values).length === 0) {
    console.log(JSON.stringify({ version: VERSION }));
  } else {
    const command = positionals[0];
    if (positionals.length !== 1 || !['setup', 'verify', 'uninstall'].includes(command)
        || (command !== 'setup' && (values.cli || values.verify || values.manifest !== undefined)) || (command === 'verify' && values['dry-run'])) {
      throw new InstallError('ARGUMENT', 'Invalid command/options; see --help.');
    }
    if (process.platform !== 'win32') throw new InstallError('PLATFORM', 'This installer supports Windows only.');
    const integration = values.integration ?? 'skill';
    if (!['skill', 'mcp'].includes(integration) || (integration === 'skill'
        && (values.client !== undefined || values.server !== undefined || values['allow-root'] !== undefined || values['allow-processes']))) {
      throw new InstallError('ARGUMENT', 'MCP options require --integration mcp.');
    }
    // Existing Skill-only usage does not need the optional MCP parser/SDK installation.
    const run = integration === 'mcp' ? (await import('./mcp.mjs')).installMcp : install;
    const result = await run(command, { ...values, dryRun: values['dry-run'],
      allowRoot: values['allow-root'], allowProcesses: values['allow-processes'] });
    console.log(JSON.stringify(result, null, 2));
  }
} catch (error) {
  const known = error instanceof InstallError;
  console.error(JSON.stringify({ status: 'error', code: error.code || 'INSTALL_FAILED',
    message: known ? error.message : 'Installation failed; check local paths, permissions and arguments.' }));
  process.exitCode = 1;
}
