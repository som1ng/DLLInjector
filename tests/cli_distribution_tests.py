"""Distribution archive and real extracted-CLI installer checks; no model calls."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'integrations/installer/scripts'))
import package_cli

NATIVE_CLI = None
NATIVE_ARCH = None
VERSION_JSON = {'command': 'version', 'status': 'success', 'tool_version': '2.0.0-dev.9',
    'schema_version': '1.6', 'data': {'text': '2.0.0-dev.9'}, 'execution': {'operation_completed': True}}
MEMBERS = {'dllinjector.exe', 'manifest.json', 'README.txt', 'THIRD_PARTY_NOTICES.md'}


def pe(arch='x64'):
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, 0x8664 if arch == 'x64' else 0x14c)
    struct.pack_into('<H', data, 152, 0x20b if arch == 'x64' else 0x10b)
    return bytes(data)


class DistributionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='dll-distribution-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.cli = self.base / 'dllinjector.exe'
        self.cli.write_bytes(pe())
        self.version = subprocess.CompletedProcess([], 0, json.dumps(VERSION_JSON).encode(), b'')

    def test_architecture_and_truncated_headers(self):
        self.assertEqual(package_cli.architecture(pe()), 'x64')
        self.assertEqual(package_cli.architecture(pe('x86')), 'x86')
        wrong_magic = bytearray(pe())
        struct.pack_into('<H', wrong_magic, 152, 0x10b)
        overflow = bytearray(pe())
        struct.pack_into('<I', overflow, 0x3c, 0xfffffff0)
        for data in (b'', pe()[:63], b'NO' + pe()[2:], wrong_magic, overflow, pe()[:150]):
            with self.assertRaises(ValueError):
                package_cli.architecture(data)

    def test_metadata_rejects_wrong_architecture_revision_version_and_changed_binary(self):
        with patch.object(package_cli.subprocess, 'run', return_value=self.version) as run:
            with self.assertRaises(ValueError):
                package_cli.metadata(self.cli, 'x86')
            with self.assertRaises(ValueError):
                package_cli.metadata(self.cli, 'x64', 'not-a-commit')
            run.assert_not_called()
            run.return_value = subprocess.CompletedProcess([], 0, b'{}', b'')
            with self.assertRaises(ValueError):
                package_cli.metadata(self.cli, 'x64')

            def changed(*_args, **_kwargs):
                self.cli.write_bytes(pe() + b'changed')
                return self.version

            run.side_effect = changed
            with self.assertRaisesRegex(ValueError, 'changed'):
                package_cli.metadata(self.cli, 'x64')

    def test_archive_allowlist_content_checksum_and_revision(self):
        with patch.object(package_cli.subprocess, 'run', return_value=self.version):
            for revision in (None, 'a' * 40):
                result = package_cli.create_bundle(self.cli, self.base / ('中文 space 🚀' + str(revision)), 'x64', revision)
                archive = Path(result['archive'])
                self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), result['sha256'])
                self.assertEqual(Path(result['checksum_file']).read_text(), result['sha256'] + '  ' + archive.name + '\n')
                with zipfile.ZipFile(archive) as bundle:
                    self.assertEqual(set(bundle.namelist()), MEMBERS)
                    self.assertEqual(len(bundle.infolist()), 4)
                    self.assertTrue(all(not entry.is_dir() for entry in bundle.infolist()))
                    self.assertEqual(bundle.read('dllinjector.exe'), pe())
                    manifest = json.loads(bundle.read('manifest.json'))
                    self.assertEqual(manifest['source_revision'], revision)
                    self.assertEqual(manifest['architecture'], 'x64')
                    self.assertEqual(manifest['files']['dllinjector.exe'], {'size': 256, 'sha256': hashlib.sha256(pe()).hexdigest()})
                    self.assertEqual(bundle.read('THIRD_PARTY_NOTICES.md'), (ROOT / 'docs/THIRD_PARTY_NOTICES.md').read_bytes())

    def test_existing_outputs_are_preserved(self):
        with patch.object(package_cli.subprocess, 'run', return_value=self.version):
            output = self.base / 'output'
            result = package_cli.create_bundle(self.cli, output, 'x64')
            before = {file.name: file.read_bytes() for file in output.iterdir()}
            with self.assertRaises(ValueError):
                package_cli.create_bundle(self.cli, output, 'x64')
            self.assertEqual(before, {file.name: file.read_bytes() for file in output.iterdir()})
            Path(result['archive']).unlink()
            with self.assertRaises(ValueError):
                package_cli.create_bundle(self.cli, output, 'x64')
            self.assertFalse(Path(result['archive']).exists())

    def test_checksum_write_failure_cleans_only_new_archive(self):
        output = self.base / 'output'
        output.mkdir()
        note = output / 'user-note.txt'
        note.write_text('keep')
        original_open = Path.open

        def fail_checksum(file, *args, **kwargs):
            if file.suffix == '.sha256':
                raise OSError('injected checksum write failure')
            return original_open(file, *args, **kwargs)

        with patch.object(package_cli.subprocess, 'run', return_value=self.version), patch.object(Path, 'open', fail_checksum):
            with self.assertRaises(OSError):
                package_cli.create_bundle(self.cli, output, 'x64')
        self.assertEqual(list(output.iterdir()), [note])
        self.assertEqual(note.read_text(), 'keep')

    def test_actual_native_bundle_installs_outside_source_tree(self):
        if NATIVE_CLI is None:
            self.skipTest('pass --cli and --architecture for native packaging/installer validation')
        node = shutil.which('node')
        self.assertTrue(node, 'Node is required for the native installer check')
        result = package_cli.create_bundle(NATIVE_CLI, self.base / 'native archive', NATIVE_ARCH)
        extracted = self.base / '独立 CLI space 🚀'
        extracted.mkdir()
        with zipfile.ZipFile(result['archive']) as bundle:
            self.assertEqual(set(bundle.namelist()), MEMBERS)
            self.assertEqual(len(bundle.infolist()), len(MEMBERS))
            # Extract only the known regular file names, never arbitrary archive paths.
            for name in MEMBERS:
                (extracted / name).write_bytes(bundle.read(name))
        project = self.base / '独立 project 🚀'
        project.mkdir()
        cli = extracted / 'dllinjector.exe'

        def installer(command, code=0, dry_run=False):
            argv = [node, str(ROOT / 'integrations/installer/src/index.mjs'), command,
                '--agent', 'codex', '--scope', 'project', '--project', str(project)]
            if command == 'setup':
                argv += ['--cli', str(cli), '--manifest', str(extracted / 'manifest.json')]
                argv += ['--dry-run'] if dry_run else ['--verify']
            run = subprocess.run(argv, capture_output=True, timeout=30,
                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
            self.assertEqual(run.returncode, code, 'native installer exit code differs')
            return json.loads(run.stdout if code == 0 else run.stderr)

        self.assertEqual(installer('setup', dry_run=True)['distribution_checksum'], 'verified')
        self.assertEqual(list(project.iterdir()), [])
        installed = installer('setup')
        self.assertEqual(installed['action'], 'installed')
        self.assertEqual(installer('setup')['action'], 'unchanged')
        self.assertEqual(installer('verify')['action'], 'verified')
        binding = json.loads((Path(installed['target']) / 'references/installation.json').read_text(encoding='utf-8'))
        self.assertEqual(binding['cli']['distribution']['architecture'], NATIVE_ARCH)
        response = subprocess.run([str(cli), 'pe', 'inspect', str(cli), '--json'], capture_output=True,
            timeout=10, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        self.assertEqual(response.returncode, 0)
        self.assertEqual(json.loads(response.stdout)['status'], 'success')
        # Corruption is rejected before startup; uninstall can still remove the Skill.
        cli.write_bytes(cli.read_bytes() + b'corrupt')
        self.assertEqual(installer('setup', code=1)['code'], 'DISTRIBUTION_INVALID')
        self.assertEqual(installer('uninstall')['action'], 'removed')
        self.assertFalse(Path(installed['target']).exists())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--cli', type=Path)
    parser.add_argument('--architecture', choices=('x86', 'x64'))
    arguments, remaining = parser.parse_known_args()
    if bool(arguments.cli) != bool(arguments.architecture):
        parser.error('--cli and --architecture must be supplied together')
    NATIVE_CLI, NATIVE_ARCH = arguments.cli, arguments.architecture
    unittest.main(argv=[sys.argv[0], *remaining])
