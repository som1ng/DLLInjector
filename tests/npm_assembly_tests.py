"""Assembly failures preserve existing files; only explicit runtime resources enter staging."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'integrations/npm/scripts'))
import package_npm


class AssemblyTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='rondo-assembly-')
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name).resolve()
        self.source = self.base / 'source'
        self.source.mkdir()
        for name, value in {
            'integrations/npm/package.json': {'name': '@som1ng/rondo', 'version': '1', 'scripts': {'postinstall': 'never'}, 'dependencies': {}},
            'integrations/installer/package.json': {'dependencies': {}},
            'integrations/mcp/package.json': {'dependencies': {}},
        }.items():
            file = self.source / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(json.dumps(value), encoding='utf-8')
        names = ['integrations/npm/README.md', 'integrations/npm/package-lock.json', 'docs/THIRD_PARTY_NOTICES.md',
                 'integrations/installer/src/index.mjs', 'integrations/npm/bin/rondo.mjs', 'integrations/npm/src/launcher.mjs']
        names += [f'integrations/mcp/dist/{name}.js' for name in ('index', 'errors', 'policy', 'reports', 'runner', 'server')]
        names += [f'integrations/mcp/dist/schemas/cli-v{v}.json' for v in ('1', '1.1', '1.2', '1.3', '1.4', '1.5', '1.6')]
        names += ['integrations/skills/windows-dll-doctor/' + name for name in
                  ('SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml')]
        for name in names:
            file = self.source / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text('fixture', encoding='utf-8')

    def assemble(self, output):
        with patch.object(package_npm, 'ROOT', self.source), patch.object(package_npm, 'metadata',
                side_effect=lambda cli, arch, revision: (b'fixture', {'tool_version': 'test', 'architecture': arch})):
            return package_npm.assemble(Path('x64'), Path('x86'), output)

    def test_allowlist_and_no_lifecycle_scripts(self):
        (self.source / 'integrations/mcp/dist/private.env').write_text('must not be packaged')
        output = self.base / 'new'
        self.assemble(output)
        self.assertFalse((output / 'integrations/mcp/dist/private.env').exists())
        self.assertNotIn('scripts', json.loads((output / 'package.json').read_text()))
        self.assertTrue((output / 'runtime/x86/rondo.exe').is_file())

    def test_existing_output_is_untouched(self):
        output = self.base / 'existing'
        output.mkdir()
        marker = output / 'keep.txt'
        marker.write_text('keep')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            self.assemble(output)
        self.assertEqual([p.name for p in output.iterdir()], ['keep.txt'])
        self.assertEqual(marker.read_text(), 'keep')

    def test_missing_runtime_prevents_any_output(self):
        (self.source / 'integrations/mcp/dist/index.js').unlink()
        output = self.base / 'new'
        with self.assertRaises(OSError):
            self.assemble(output)
        self.assertFalse(output.exists())

    def test_dependency_drift_prevents_any_output(self):
        file = self.source / 'integrations/mcp/package.json'
        file.write_text(json.dumps({'dependencies': {'missing': '1.0.0'}}))
        with self.assertRaisesRegex(ValueError, 'dependencies differ'):
            self.assemble(self.base / 'new')
        self.assertFalse((self.base / 'new').exists())


if __name__ == '__main__':
    unittest.main()
