"""Real offline npm global install/run/remove in an isolated prefix; no user config or model API."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ARCHIVE = None
CODEX = None
CLAUDE = None
OPENCODE = None
NPM = shutil.which('npm.cmd')
NODE = shutil.which('node')


class PackageTests(unittest.TestCase):
    def run_command(self, args, *, code=0, cwd=None, timeout=120, env=None):
        result = subprocess.run([str(arg) for arg in args], cwd=cwd, capture_output=True,
                                encoding='utf-8', errors='strict', timeout=timeout, env=env)
        self.assertEqual(result.returncode, code, result.stderr[-3000:] + result.stdout[-1000:])
        return result

    def test_archive_contract(self):
        with tarfile.open(ARCHIVE, 'r:gz') as archive:
            files = {entry.name: entry for entry in archive.getmembers()}
            package = json.load(archive.extractfile('package/package.json'))
            self.assertEqual(package['name'], '@som1ng/rondo')
            self.assertEqual(package['bin'], {'rondo': 'integrations/npm/bin/rondo.mjs'})
            self.assertNotIn('scripts', package)
            self.assertTrue(package['bundleDependencies'])
            for entry in files.values():
                self.assertTrue(entry.isfile(), entry.name)
                self.assertNotIn('..', Path(entry.name).parts)
                self.assertNotIn('.env', Path(entry.name).parts)
                self.assertNotIn('.git', Path(entry.name).parts)
            for arch in ('x64', 'x86'):
                data = archive.extractfile(f'package/runtime/{arch}/rondo.exe').read()
                manifest = json.load(archive.extractfile(f'package/runtime/{arch}/manifest.json'))
                self.assertEqual(manifest['product'], 'rondo-cli')
                self.assertEqual(manifest['architecture'], arch)
                self.assertEqual(manifest['files']['rondo.exe'], {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
            for package_name in package['dependencies']:
                self.assertIn('package/node_modules/' + package_name + '/package.json', files)
            self.assertIn('package/integrations/mcp/dist/schemas/cli-v1.6.json', files)
            self.assertIn('package/integrations/skills/windows-dll-doctor/SKILL.md', files)
            self.assertFalse(any('/tests/' in name for name in files if not name.startswith('package/node_modules/')))

    def test_offline_global_install_native_skill_mcp_and_uninstall(self):
        self.assertTrue(NPM and NODE, 'Node/npm are required')
        with tempfile.TemporaryDirectory(prefix='rondo-npm-') as temporary:
            base = Path(temporary).resolve()
            prefix = base / '中文 global 🚀'
            project = base / '项目 space'
            project.mkdir()
            marker = project / 'keep.txt'
            marker.write_text('unrelated user file', encoding='utf-8')
            npm = [NPM, '--prefix', prefix, '--cache', base / 'empty-cache', '--offline',
                   '--registry=http://127.0.0.1:9', '--ignore-scripts', '--no-audit', '--no-fund']
            self.run_command([*npm, 'install', '-g', ARCHIVE])
            package = prefix / 'node_modules/@som1ng/rondo'
            shim = prefix / 'rondo.cmd'
            self.assertTrue(shim.is_file())
            readiness = json.loads(self.run_command([shim, 'self-check', '--json']).stdout)
            self.assertEqual(readiness['status'], 'success')
            self.assertEqual(readiness['runtime_verification'], 'passed')
            self.assertEqual(readiness['client_discovery'], 'not_run')
            self.assertTrue(all(check['status'] == 'passed' for check in readiness['checks']))
            self.assertEqual(list(project.iterdir()), [marker])
            self.assertIn('Rondo npm self-check: success', self.run_command([shim, 'self-check']).stdout)
            for relative, check_id, replacement in [('integrations/mcp/dist/schemas/cli-v1.6.json', 'mcp_modules', None),
                                                    ('integrations/installer/src/distribution.mjs', 'cli_distribution', None),
                                                    ('integrations/mcp/dist/index.js', 'mcp_modules', b'import {')]:
                missing = package / relative
                original_file = missing.read_bytes()
                if replacement is None:
                    missing.unlink()
                else:
                    missing.write_bytes(replacement)
                try:
                    diagnosis = json.loads(self.run_command([shim, 'self-check', '--json'], code=1).stdout)
                    self.assertEqual(next(check for check in diagnosis['checks'] if check['id'] == check_id)['status'], 'failed')
                finally:
                    missing.write_bytes(original_file)
            version = json.loads(self.run_command([shim, '--version', '--json']).stdout)
            self.assertEqual(version['tool_version'], '2.0.0-dev.9')
            help_text = json.loads(self.run_command([shim, '--help', '--json']).stdout)['data']['text']
            self.assertIn('rondo doctor', help_text)
            self.assertNotIn('  dllinjector ', help_text)
            self.assertIn('rondo setup', self.run_command([shim, '--help']).stdout)
            self.run_command([shim, 'bad-command', '--json'], code=2)
            # Both bundled architectures must actually run, independent of launch selection.
            for arch in ('x64', 'x86'):
                self.run_command([package / f'runtime/{arch}/rondo.exe', '--version', '--json'])
            sample = project / '中文 & dollar$.dll'
            sample.write_bytes((package / 'runtime/x64/rondo.exe').read_bytes())
            saved = project / 'report.json'
            report = json.loads(self.run_command([shim, 'pe', 'inspect', sample, '--json', '--output', saved]).stdout)
            self.assertEqual(report['status'], 'success')
            bad = project / 'broken.dll'
            bad.write_bytes(b'not a PE')
            self.run_command([shim, 'doctor', bad, '--json'], code=1)
            setup = [shim, 'setup', '--agent', 'all', '--scope', 'project', '--project', project]
            preview = json.loads(self.run_command([*setup, '--dry-run']).stdout)
            self.assertEqual(preview['action'], 'preview')
            self.assertFalse((project / '.agents').exists())
            self.assertEqual(json.loads(self.run_command([*setup, '--verify']).stdout)['action'], 'installed')
            self.assertEqual(json.loads(self.run_command(setup).stdout)['action'], 'unchanged')
            binding = json.loads((project / '.agents/skills/windows-dll-doctor/references/installation.json').read_text(encoding='utf-8'))
            self.assertTrue(Path(binding['cli']['path']).is_relative_to(package))
            # Migrate a managed binding back to the npm runtime after the old executable disappears.
            previous_cli = project / 'previous rondo.exe'
            previous_cli.write_bytes(Path(binding['cli']['path']).read_bytes())
            # An abrupt child exit bypasses finally blocks, leaving a durable Skill journal.
            interrupted = r'''
import { pathToFileURL } from 'node:url';
const [entry, project, cli] = process.argv.slice(1);
const { install } = await import(pathToFileURL(entry));
await install('upgrade', { agent: 'codex', scope: 'project', project, cli }, {
  checkpoint: stage => { if (stage === 'upgraded-file') process.exit(73); },
});
'''
            self.run_command([NODE, '--input-type=module', '-e', interrupted,
                              package / 'integrations/installer/src/installer.mjs', project, previous_cli], code=73)
            recover = [shim, 'recover', '--agent', 'codex', '--scope', 'project', '--project', project]
            self.assertEqual(json.loads(self.run_command([*recover, '--dry-run']).stdout)['action'], 'preview')
            self.assertEqual(json.loads(self.run_command(recover).stdout)['action'], 'restored')
            self.assertEqual(json.loads(self.run_command(recover).stdout)['action'], 'absent')
            restored_binding = json.loads((project / '.agents/skills/windows-dll-doctor/references/installation.json').read_text(encoding='utf-8'))
            self.assertEqual(restored_binding, binding)
            interrupted_batch = r'''
import { pathToFileURL } from 'node:url';
const [entry, project, cli] = process.argv.slice(1);
const { upgradeAll } = await import(pathToFileURL(entry));
await upgradeAll({ agent: 'all', scope: 'project', project, cli }, {
  batchCheckpoint: (stage, data) => { if (stage === 'target-completed' && data.agent === 'codex') process.exit(75); },
});
'''
            self.run_command([NODE, '--input-type=module', '-e', interrupted_batch,
                              package / 'integrations/installer/src/batch-upgrade.mjs', project, previous_cli], code=75)
            recover_all = [shim, 'recover', '--agent', 'all', '--scope', 'project', '--project', project]
            self.assertEqual(json.loads(self.run_command([*recover_all, '--dry-run']).stdout)['action'], 'preview')
            self.assertEqual(json.loads(self.run_command(recover_all).stdout)['action'], 'restored')
            self.assertEqual(json.loads(self.run_command(recover_all).stdout)['action'], 'absent')
            self.assertEqual(json.loads((project / '.agents/skills/windows-dll-doctor/references/installation.json').read_text(encoding='utf-8')), binding)
            upgrade = [shim, 'upgrade', '--agent', 'all', '--scope', 'project', '--project', project]
            batch = json.loads(self.run_command([*upgrade, '--cli', previous_cli]).stdout)
            self.assertEqual(batch['action'], 'upgraded')
            self.assertEqual(len(batch['targets']), 2)
            previous_cli.unlink()
            self.assertEqual(json.loads(self.run_command([*upgrade, '--dry-run']).stdout)['action'], 'preview')
            self.assertEqual(json.loads(self.run_command(upgrade).stdout)['action'], 'upgraded')
            for agent in ('codex', 'claude'):
                self.run_command([shim, 'verify', '--agent', agent, '--scope', 'project', '--project', project])
            isolated_profile = base / '临时 user profile'
            isolated_profile.mkdir()
            env = {key: value for key, value in os.environ.items()
                   if key.upper() in ('PATH', 'SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'TEMP', 'TMP')}
            env.update(USERPROFILE=str(isolated_profile), APPDATA=str(isolated_profile / 'AppData/Roaming'),
                       LOCALAPPDATA=str(isolated_profile / 'AppData/Local'))
            observed = self.run_command([NODE, '-p', 'JSON.stringify(require("node:os").homedir())'], env=env)
            self.assertEqual(Path(json.loads(observed.stdout)), isolated_profile)
            # Preview never executes this placeholder client and runs in CI without Codex installed.
            preview = json.loads(self.run_command([shim, 'setup', '--integration', 'mcp', '--agent', 'codex',
                '--scope', 'user', '--client', package / 'runtime/x64/rondo.exe', '--allow-root', project,
                '--dry-run'], env=env).stdout)
            self.assertEqual(preview['scope'], 'user')
            self.assertEqual(Path(preview['config']), isolated_profile / '.codex/config.toml')
            self.assertFalse((isolated_profile / '.codex').exists())
            # Exercise packaged MCP recovery in CI too; only the absent native client probe is a fixture.
            mcp_recovery_project = base / 'MCP recovery'
            mcp_recovery_project.mkdir()
            previous_cli.write_bytes(Path(binding['cli']['path']).read_bytes())
            interrupted_mcp = r'''
import { pathToFileURL } from 'node:url';
const [entry, project, cli, nextCli, server, client, realClient] = process.argv.slice(1);
const { installMcp } = await import(pathToFileURL(entry));
const options = { agent: 'codex', scope: 'project', project, cli, server, client, allowRoot: [project] };
const context = realClient === 'yes' ? {} : { probeClient: async (agent, file) => ({ path: file, version: '0.164.0' }) };
await installMcp('setup', options, context);
await installMcp('upgrade', { ...options, cli: nextCli }, { ...context,
  checkpoint: stage => { if (stage === 'config-written') process.exit(74); },
});
'''
            packaged_cli = Path(binding['cli']['path'])
            self.run_command([NODE, '--input-type=module', '-e', interrupted_mcp,
                              package / 'integrations/installer/src/mcp.mjs', mcp_recovery_project,
                              packaged_cli, previous_cli, package / 'integrations/mcp/dist/index.js',
                              CODEX or packaged_cli, 'yes' if CODEX else 'no'], code=74)
            mcp_receipt = mcp_recovery_project / '.dll-doctor/mcp-codex.json'
            receipt_before = mcp_receipt.read_bytes()
            recover_mcp = [shim, 'recover', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project',
                           '--project', mcp_recovery_project]
            self.assertEqual(json.loads(self.run_command([*recover_mcp, '--dry-run']).stdout)['action'], 'preview')
            self.assertEqual(json.loads(self.run_command(recover_mcp).stdout)['action'], 'restored')
            self.assertEqual(mcp_receipt.read_bytes(), receipt_before)
            recovered_config = (mcp_recovery_project / '.codex/config.toml').read_text(encoding='utf-8')
            self.assertIn(json.dumps(str(packaged_cli), ensure_ascii=False), recovered_config)
            self.assertEqual(json.loads(self.run_command(recover_mcp).stdout)['action'], 'absent')
            self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project',
                              '--project', mcp_recovery_project])
            self.assertFalse((mcp_recovery_project / '.codex/config.toml').exists())
            interrupted_mcp_batch = r'''
import { pathToFileURL } from 'node:url';
const [directory, project, cli, nextCli, server] = process.argv.slice(1);
const { setupAllMcp } = await import(pathToFileURL(directory + '/batch-mcp.mjs'));
const { upgradeAll } = await import(pathToFileURL(directory + '/batch-upgrade.mjs'));
const agents = ['codex', 'claude', 'opencode'];
const options = { agent: 'all', integration: 'mcp', scope: 'project', project, cli, server,
  clients: Object.fromEntries(agents.map(agent => [agent, cli])), allowRoot: [project] };
const context = { probeClient: async (agent, file) => ({ path: file,
  version: { codex: '0.164.0', claude: '2.1.310', opencode: '1.19.4' }[agent] }) };
await setupAllMcp(options, context);
await upgradeAll({ ...options, cli: nextCli }, { ...context,
  batchCheckpoint: (stage, data) => { if (stage === 'target-completed' && data.agent === 'codex') process.exit(76); },
});
'''
            self.run_command([NODE, '--input-type=module', '-e', interrupted_mcp_batch,
                              package / 'integrations/installer/src', mcp_recovery_project,
                              packaged_cli, previous_cli, package / 'integrations/mcp/dist/index.js'], code=76)
            recover_mcp_all = [shim, 'recover', '--integration', 'mcp', '--agent', 'all', '--scope', 'project',
                               '--project', mcp_recovery_project]
            self.assertEqual(json.loads(self.run_command([*recover_mcp_all, '--dry-run']).stdout)['action'], 'preview')
            self.assertEqual(json.loads(self.run_command(recover_mcp_all).stdout)['action'], 'restored')
            for agent in ('codex', 'claude', 'opencode'):
                receipt = json.loads((mcp_recovery_project / f'.dll-doctor/mcp-{agent}.json').read_text(encoding='utf-8'))
                self.assertEqual(Path(receipt['runtime']['cli']['path']), packaged_cli)
                self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', agent, '--scope', 'project',
                                  '--project', mcp_recovery_project])
            previous_cli.unlink()
            if CODEX:
                self.run_command([shim, 'setup', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project',
                                  '--project', project, '--client', CODEX, '--allow-root', project, '--verify'])
                self.run_command([shim, 'verify', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project', '--project', project])
                self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project', '--project', project])
                self.assertFalse((project / '.codex/config.toml').exists())
                self.run_command([shim, 'setup', '--integration', 'mcp', '--agent', 'codex', '--scope', 'user',
                                  '--client', CODEX, '--allow-root', project, '--verify'], env=env)
                self.assertTrue((isolated_profile / '.codex/config.toml').is_file())
                previous_cli.write_bytes(Path(binding['cli']['path']).read_bytes())
                upgrade_mcp = [shim, 'upgrade', '--integration', 'mcp', '--agent', 'codex', '--scope', 'user',
                               '--client', CODEX, '--allow-root', project]
                self.assertEqual(json.loads(self.run_command([*upgrade_mcp, '--cli', previous_cli], env=env).stdout)['action'], 'upgraded')
                previous_cli.unlink()
                self.assertEqual(json.loads(self.run_command(upgrade_mcp, env=env).stdout)['action'], 'upgraded')
                if CLAUDE and OPENCODE:
                    for agent, client in [('claude', CLAUDE), ('opencode', OPENCODE)]:
                        self.run_command([shim, 'setup', '--integration', 'mcp', '--agent', agent, '--scope', 'user',
                                          '--client', client, '--allow-root', project], env=env)
                    upgrade_all = [shim, 'upgrade', '--integration', 'mcp', '--agent', 'all', '--scope', 'user',
                                   '--codex-client', CODEX, '--claude-client', CLAUDE, '--opencode-client', OPENCODE,
                                   '--allow-root', project]
                    previous_cli.write_bytes(Path(binding['cli']['path']).read_bytes())
                    batch = json.loads(self.run_command([*upgrade_all, '--cli', previous_cli], env=env).stdout)
                    self.assertEqual(batch['action'], 'upgraded')
                    self.assertEqual(len(batch['targets']), 3)
                    previous_cli.unlink()
                    self.assertEqual(json.loads(self.run_command(upgrade_all, env=env).stdout)['action'], 'upgraded')
                    for agent in ('claude', 'opencode'):
                        self.run_command([shim, 'verify', '--integration', 'mcp', '--agent', agent, '--scope', 'user'], env=env)
                        self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', agent, '--scope', 'user'], env=env)
                self.run_command([shim, 'verify', '--integration', 'mcp', '--agent', 'codex', '--scope', 'user'], env=env)
                self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', 'codex', '--scope', 'user'], env=env)
                self.assertFalse((isolated_profile / '.codex/config.toml').exists())
            # Use the SDK installed inside the archive; there is no repository dependency fallback.
            script = r'''
import { Client } from '@modelcontextprotocol/client';
import { StdioClientTransport } from '@modelcontextprotocol/client/stdio';
import assert from 'node:assert/strict';
const [entry, root, sample, report, outside] = process.argv.slice(1);
const client = new Client({ name: 'rondo-package-test', version: '1' });
try {
  await client.connect(new StdioClientTransport({ command: process.execPath,
    args: [entry, 'mcp', '--allow-root', root], stderr: 'pipe' }));
  assert.equal((await client.listTools()).tools.length, 7);
  assert.notEqual((await client.callTool({name:'inspect_pe',arguments:{path:sample}})).isError, true);
  assert.notEqual((await client.callTool({name:'read_report',arguments:{path:report}})).isError, true);
  assert.equal((await client.callTool({name:'inspect_pe',arguments:{path:outside}})).isError, true);
  assert.notEqual((await client.callTool({name:'inspect_pe',arguments:{path:sample}})).isError, true);
} finally { await client.close(); }
'''
            self.run_command([NODE, '--input-type=module', '-e', script, package / 'integrations/npm/bin/rondo.mjs',
                              project, sample, saved, package / 'runtime/x64/rondo.exe'], cwd=package, timeout=30)
            cli = Path(binding['cli']['path'])
            original = cli.read_bytes()
            cli.write_bytes(original + b'tampered')
            diagnosis = json.loads(self.run_command([shim, 'self-check', '--json'], code=1).stdout)
            self.assertEqual(diagnosis['runtime_verification'], 'not_run')
            self.assertEqual(next(check for check in diagnosis['checks'] if check['id'] == 'cli_runtime')['status'], 'skipped')
            failure = self.run_command([shim, '--version', '--json'], code=1)
            self.assertEqual(failure.stdout, '')
            self.assertEqual(json.loads(failure.stderr)['code'], 'DISTRIBUTION_INVALID')
            cli.write_bytes(original)
            for agent in ('codex', 'claude'):
                self.run_command([shim, 'uninstall', '--agent', agent, '--scope', 'project', '--project', project])
            self.run_command([*npm, 'uninstall', '-g', '@som1ng/rondo'])
            self.assertFalse(shim.exists())
            self.assertFalse(package.exists())
            self.assertEqual(marker.read_text(encoding='utf-8'), 'unrelated user file')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--codex', type=Path, help='Optional installed native Codex EXE for packaged MCP setup/verify/uninstall')
    parser.add_argument('--claude', type=Path, help='Optional native Claude EXE; with --codex/--opencode tests batch user MCP upgrade')
    parser.add_argument('--opencode', type=Path, help='Optional native OpenCode EXE for the same batch test')
    args = parser.parse_args()
    ARCHIVE = args.archive.resolve(strict=True)
    CODEX = args.codex.resolve(strict=True) if args.codex else None
    if (args.claude or args.opencode) and not (args.codex and args.claude and args.opencode):
        parser.error('Batch MCP validation requires all three native clients.')
    CLAUDE = args.claude.resolve(strict=True) if args.claude else None
    OPENCODE = args.opencode.resolve(strict=True) if args.opencode else None
    unittest.main(argv=['npm_package_tests.py'])
