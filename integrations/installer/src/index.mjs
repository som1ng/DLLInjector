import { parseArgs } from 'node:util';
import { install, InstallError, VERSION } from './installer.mjs';

try {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: {
    agent: { type: 'string' }, scope: { type: 'string' }, project: { type: 'string' },
    cli: { type: 'string' }, 'dry-run': { type: 'boolean' }, verify: { type: 'boolean' },
    help: { type: 'boolean' }, manifest: { type: 'string' },
    integration: { type: 'string' }, server: { type: 'string' }, client: { type: 'string' },
    'allow-root': { type: 'string', multiple: true }, 'allow-processes': { type: 'boolean' },
    select: { type: 'string' },
    'codex-client': { type: 'string' }, 'claude-client': { type: 'string' }, 'opencode-client': { type: 'string' },
  } });
  if (values.help) {
    console.log('Rondo Agent installer ' + VERSION + '\n'
      + 'setup --agent codex|claude|opencode --scope project|user --cli <absolute.exe> [--manifest <absolute.json>] [--project <dir>] [--dry-run] [--verify]\n'
      + 'upgrade: use setup options with an explicit agent or all to update owned installations; verifies new runtimes and rolls back caught failures.\n'
      + 'verify --agent <agent> --scope <scope> [--project <dir>]\n'
      + 'recover --agent <agent>|all --scope <scope> [--integration mcp] [--project <dir>] [--dry-run]: recover an interrupted upgrade after its owner exits.\n'
      + 'uninstall --agent <agent> --scope <scope> [--project <dir>] [--dry-run]\n'
      + 'MCP: add --integration mcp, --server <built.js> and --allow-root <dir> (repeatable); explicit-agent setup also requires --client <native.exe>.\n'
      + 'MCP supports project/user scope; --allow-processes is optional. Skill is the default integration.\n'
      + 'list-agents: find native client candidates without starting them.\n'
      + 'setup --agent auto: choose one detected client interactively; scripts require --select <agent>.\n'
      + 'setup --agent all: install Skills for all three supported clients, deduplicating the shared directory.\n'
      + 'MCP all: add --integration mcp --codex-client <exe> --claude-client <exe> --opencode-client <exe> and MCP options.\n'
      + 'upgrade --agent all requires every target to be managed already. Use explicit agents for verify/uninstall.\n'
      + 'Use --client <native.exe> with --select if several paths match. --dry-run never prompts.\n'
      + 'version\nPi: use integrations/pi. Local source installer; no downloads.');
  } else if (positionals.length === 1 && positionals[0] === 'version' && Object.keys(values).length === 0) {
    console.log(JSON.stringify({ version: VERSION }));
  } else if (positionals.length === 1 && positionals[0] === 'list-agents' && Object.keys(values).length === 0) {
    if (process.platform !== 'win32') throw new InstallError('PLATFORM', 'This installer supports Windows only.');
    console.log(JSON.stringify(await (await import('./agents.mjs')).discoverAgents(), null, 2));
  } else {
    const command = positionals[0];
    if (positionals.length !== 1 || !['setup', 'upgrade', 'recover', 'verify', 'uninstall'].includes(command)
        || (!['setup', 'upgrade'].includes(command) && (values.cli || values.verify || values.manifest !== undefined)) || (command === 'verify' && values['dry-run'])) {
      throw new InstallError('ARGUMENT', 'Invalid command/options; see --help.');
    }
    if (process.platform !== 'win32') throw new InstallError('PLATFORM', 'This installer supports Windows only.');
    const integration = values.integration ?? 'skill';
    const clients = Object.fromEntries(['codex', 'claude', 'opencode'].filter(agent => values[agent + '-client'] !== undefined)
      .map(agent => [agent, values[agent + '-client']]));
    if (Object.keys(clients).length && (values.agent !== 'all' || integration !== 'mcp' || !['setup', 'upgrade'].includes(command))) {
      throw new InstallError('ARGUMENT', 'Per-agent client paths are only valid for setup/upgrade --integration mcp --agent all.');
    }
    if ((values.agent === 'auto' && command !== 'setup') || (values.agent === 'all' && !['setup', 'upgrade', 'recover'].includes(command))
        || (values.select !== undefined && values.agent !== 'auto')) {
      throw new InstallError('ARGUMENT', 'Auto selection is for setup only; all supports setup/upgrade/recover; verify/uninstall require an explicit agent.');
    }
    if (!['skill', 'mcp'].includes(integration) || (integration === 'skill'
        && ((values.client !== undefined && values.agent !== 'auto') || values.server !== undefined || values['allow-root'] !== undefined || values['allow-processes']))) {
      throw new InstallError('ARGUMENT', 'MCP options require --integration mcp.');
    }
    // Existing Skill-only usage does not need the optional MCP parser/SDK installation.
    const options = { ...values, clients, dryRun: values['dry-run'], allowRoot: values['allow-root'], allowProcesses: values['allow-processes'] };
    const result = values.agent === 'auto'
      ? await (await import('./agents.mjs')).setupAuto(options)
      : values.agent === 'all' && command === 'recover' ? await (await import('./batch-recovery.mjs')).recoverAll(options)
      : values.agent === 'all' && command === 'upgrade' ? await (await import('./batch-upgrade.mjs')).upgradeAll(options)
      : values.agent === 'all' ? await (integration === 'mcp'
        ? (await import('./batch-mcp.mjs')).setupAllMcp : (await import('./batch.mjs')).setupAll)(options)
      : await (integration === 'mcp' ? (await import('./mcp.mjs')).installMcp : install)(command, options);
    console.log(JSON.stringify(result, null, 2));
  }
} catch (error) {
  const known = error instanceof InstallError;
  console.error(JSON.stringify({ status: 'error', code: error.code || 'INSTALL_FAILED',
    message: known ? error.message : 'Installation failed; check local paths, permissions and arguments.',
    ...(known && error.details ? { details: error.details } : {}) }));
  process.exitCode = 1;
}
