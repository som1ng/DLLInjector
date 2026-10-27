"""Guard against false discovery passes; no client installation or model calls."""
from pathlib import Path
import tempfile
import unittest
import subprocess
import sys

from agent_discovery import DiscoveryError, native_executable, verify_observation


class DiscoveryChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name).resolve() / '中文 project/.agents/skills/windows-dll-doctor/SKILL.md'
        self.description = 'Diagnose DLL 加载失败'
        self.good = {'name': 'windows-dll-doctor', 'description': self.description,
                     'path': str(self.path), 'enabled': True}

    def check(self, rows, expected=True, diagnostics=()):
        return verify_observation(rows, self.path, self.description, expected, diagnostics)

    def test_exact_project_skill(self):
        self.check([self.good])

    def test_global_name_collision_cannot_pass(self):
        other = {**self.good, 'path': str(self.path.parent.parent / 'global/SKILL.md')}
        with self.assertRaises(DiscoveryError):
            self.check([other])
        self.check([other], expected=False)

    def test_negative_case_cannot_contain_project_skill(self):
        self.check([], expected=False)
        with self.assertRaises(DiscoveryError):
            self.check([self.good], expected=False)

    def test_virtual_or_relative_skill_cannot_substitute_for_fixture(self):
        virtual = {**self.good, 'path': 'skill://plugin/windows-dll-doctor/SKILL.md'}
        self.check([virtual, self.good])
        with self.assertRaises(DiscoveryError):
            self.check([virtual])
        self.check([virtual], expected=False)

    def test_disabled_or_damaged_metadata_fails(self):
        for changes in ({'enabled': False}, {'name': 'wrong'}, {'description': 'stale'},
                        {'description': ''}, {'path': ''}):
            with self.subTest(changes=changes), self.assertRaises(DiscoveryError):
                self.check([{**self.good, **changes}])

    def test_duplicate_project_results_fail(self):
        with self.assertRaises(DiscoveryError):
            self.check([self.good, self.good])

    def test_batch_discovery_requires_exactly_one_of_the_installed_paths(self):
        other_path = self.path.parents[3] / '.claude/skills/windows-dll-doctor/SKILL.md'
        other = dict(self.good, path=str(other_path))
        for row in [self.good, other]:
            self.assertEqual(verify_observation([row], self.path, self.description, True,
                alternative_paths=[other_path]), Path(row['path']))
        for rows in [[], [self.good, other], [dict(self.good, path=str(self.path.parent / 'global.md'))]]:
            with self.assertRaises(DiscoveryError):
                verify_observation(rows, self.path, self.description, True, alternative_paths=[other_path])

    def test_batch_absence_and_diagnostics_cover_both_installed_paths(self):
        other_path = self.path.parents[3] / '.claude/skills/windows-dll-doctor/SKILL.md'
        with self.assertRaises(DiscoveryError):
            verify_observation([dict(self.good, path=str(other_path))], self.path, self.description, False,
                alternative_paths=[other_path])
        with self.assertRaises(DiscoveryError):
            verify_observation([self.good], self.path, self.description, True,
                diagnostics=[{'path': str(other_path)}], alternative_paths=[other_path])

    def test_target_diagnostic_is_not_silently_ignored(self):
        with self.assertRaises(DiscoveryError):
            self.check([self.good], diagnostics=[{'path': str(self.path), 'message': 'invalid'}])
        self.check([self.good], diagnostics=[{'path': str(self.path.parent.parent / 'other/SKILL.md')}])
        self.check([self.good], diagnostics=[{'path': 'skill://unrelated/plugin/SKILL.md'}])

    def test_malformed_response_fails_without_echoing_payload(self):
        for rows in (None, {}, [None], [{'path': 123}], [dict(self.good, description=None)]):
            with self.subTest(rows=rows), self.assertRaises(DiscoveryError):
                self.check(rows)

    def test_missing_native_client_is_an_error(self):
        with self.assertRaises(DiscoveryError):
            native_executable(str(self.path.parent / 'missing.exe'))

    def test_script_shim_is_not_executed(self):
        shim = Path(self.temp.name) / 'client.ps1'
        shim.write_text('throw "must not execute"', encoding='utf-8')
        with self.assertRaises(DiscoveryError):
            native_executable(str(shim))

    def test_cli_requires_explicit_client_and_bounded_timeout(self):
        script = Path(__file__).with_name('agent_discovery.py')
        for args in ([], ['--codex', 'missing.exe', '--timeout', '0'],
                     ['--codex', 'missing.exe', '--timeout', '121']):
            result = subprocess.run([sys.executable, str(script), *args], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 2)

    def test_existing_summary_is_preserved(self):
        summary = Path(self.temp.name) / 'summary.json'
        summary.write_bytes(b'existing report')
        result = subprocess.run([sys.executable, str(Path(__file__).with_name('agent_discovery.py')),
            '--codex', 'missing.exe', '--output', str(summary)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 2)
        self.assertEqual(summary.read_bytes(), b'existing report')


if __name__ == '__main__':
    unittest.main()
