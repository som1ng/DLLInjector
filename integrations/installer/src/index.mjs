import { parseArgs } from 'node:util';
import { install, InstallError, VERSION } from './installer.mjs';

try {
  const { values, positionals } = parseArgs({ allowPositionals: true, options: {
    agent: { type: 'string' }, scope: { type: 'string' }, project: { type: 'string' },
    cli: { type: 'string' }, 'dry-run': { type: 'boolean' }, verify: { type: 'boolean' },
    help: { type: 'boolean' },
  } });
  if (values.help) {
    console.log('DLL Doctor local Skill installer ' + VERSION + '\n'
      + 'setup --agent codex|claude|opencode --scope project|user --cli <absolute.exe> [--project <dir>] [--dry-run] [--verify]\n'
      + 'verify --agent <agent> --scope <scope> [--project <dir>]\n'
      + 'uninstall --agent <agent> --scope <scope> [--project <dir>] [--dry-run]\n'
      + 'version\nPi: use integrations/pi. Local source installer; no downloads or client configuration edits.');
  } else if (positionals.length === 1 && positionals[0] === 'version' && Object.keys(values).length === 0) {
    console.log(JSON.stringify({ version: VERSION }));
  } else {
    const command = positionals[0];
    if (positionals.length !== 1 || !['setup', 'verify', 'uninstall'].includes(command)
        || (command !== 'setup' && (values.cli || values.verify)) || (command === 'verify' && values['dry-run'])) {
      throw new InstallError('ARGUMENT', 'Invalid command/options; see --help.');
    }
    if (process.platform !== 'win32') throw new InstallError('PLATFORM', 'This installer supports Windows only.');
    const result = await install(command, { ...values, dryRun: values['dry-run'] });
    console.log(JSON.stringify(result, null, 2));
  }
} catch (error) {
  const known = error instanceof InstallError;
  console.error(JSON.stringify({ status: 'error', code: error.code || 'INSTALL_FAILED',
    message: known ? error.message : 'Installation failed; check local paths, permissions and arguments.' }));
  process.exitCode = 1;
}
