"""Guard against false discovery passes; no client installation or model calls."""
from pathlib import Path
import tempfile
import unittest

from agent_discovery import DiscoveryError, verify_observation


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

    def test_disabled_or_damaged_metadata_fails(self):
        for changes in ({'enabled': False}, {'name': 'wrong'}, {'description': 'stale'},
                        {'description': ''}, {'path': ''}):
            with self.subTest(changes=changes), self.assertRaises(DiscoveryError):
                self.check([{**self.good, **changes}])

    def test_duplicate_project_results_fail(self):
        with self.assertRaises(DiscoveryError):
            self.check([self.good, self.good])

    def test_target_diagnostic_is_not_silently_ignored(self):
        with self.assertRaises(DiscoveryError):
            self.check([self.good], diagnostics=[{'path': str(self.path), 'message': 'invalid'}])
        self.check([self.good], diagnostics=[{'path': str(self.path.parent.parent / 'other/SKILL.md')}])

    def test_malformed_response_fails_without_echoing_payload(self):
        for rows in (None, {}, [None], [{'path': 123}], [dict(self.good, description=None)]):
            with self.subTest(rows=rows), self.assertRaises(DiscoveryError):
                self.check(rows)


if __name__ == '__main__':
    unittest.main()
