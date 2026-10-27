"""Real offline npm global install/run/remove in an isolated prefix; no user config or model API."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ARCHIVE = None
CODEX = None
NPM = shutil.which('npm.cmd')
NODE = shutil.which('node')


class PackageTests(unittest.TestCase):
    def run_command(self, args, *, code=0, cwd=None, timeout=120):
        result = subprocess.run([str(arg) for arg in args], cwd=cwd, capture_output=True,
                                encoding='utf-8', errors='strict', timeout=timeout)
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
            for agent in ('codex', 'claude'):
                self.run_command([shim, 'verify', '--agent', agent, '--scope', 'project', '--project', project])
            if CODEX:
                self.run_command([shim, 'setup', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project',
                                  '--project', project, '--client', CODEX, '--allow-root', project, '--verify'])
                self.run_command([shim, 'verify', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project', '--project', project])
                self.run_command([shim, 'uninstall', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project', '--project', project])
                self.assertFalse((project / '.codex/config.toml').exists())
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
    args = parser.parse_args()
    ARCHIVE = args.archive.resolve(strict=True)
    CODEX = args.codex.resolve(strict=True) if args.codex else None
    unittest.main(argv=['npm_package_tests.py'])
