"""Offline checks of acceptance evidence; never starts a real client or model."""
import copy
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from codex_acceptance import DiscoveryModel, command, emit_summary, expected_answer, main, verify_live, verify_model_catalog
from mcp_model_fixture import HostCheckError, TOOLS


SAMPLE = Path(__file__).resolve().parent / 'fixture.dll'
REPORT = {'duration_ms': 1, 'data': {'machine': 0x8664, 'characteristics': 0x2022,
    'size_of_image': 28672, 'exports': {'symbols': [{'names': ['FixtureValue']}]}}}


def catalog():
    return [{'type': 'namespace', 'name': 'mcp__dll_doctor', 'tools': [
        {'type': 'function', 'name': name, 'parameters': {'type': 'object', 'additionalProperties': False,
         'properties': {'timeout_ms': {'type': 'integer'}}}}
        for name in sorted(TOOLS)]}]


def events():
    content = {'source': 'live_cli', 'cli_exit_code': 0, 'report': copy.deepcopy(REPORT), 'error': None}
    return [
        {'type': 'item.completed', 'item': {'id': 'call', 'type': 'mcp_tool_call', 'server': 'dll_doctor',
            'tool': 'inspect_pe', 'arguments': {'path': str(SAMPLE)}, 'error': None, 'status': 'completed',
            'result': {'content': [{'type': 'text', 'text': json.dumps(content)}], 'structured_content': content}}},
        {'type': 'item.completed', 'item': {'type': 'agent_message', 'text': json.dumps(expected_answer(REPORT))}},
        {'type': 'turn.completed', 'usage': {'input_tokens': 10, 'output_tokens': 10}},
    ]


class AcceptanceTests(unittest.TestCase):
    def test_catalog_requires_trusted_project_and_exactly_seven_tools(self):
        verify_model_catalog(catalog(), True)
        verify_model_catalog([], False)
        flat = [dict(row, name='mcp__dll_doctor__' + row['name']) for row in catalog()[0]['tools']]
        verify_model_catalog(flat, True)
        broken = catalog()
        broken[0]['tools'].pop()
        for rows, present in [(catalog(), False), ([], True), (broken, True),
                (catalog() * 2, True), ([{'name': 'mcp__unrelated', 'type': 'namespace'}], False)]:
            with self.subTest(rows=rows), self.assertRaises(HostCheckError):
                verify_model_catalog(rows, present)

    def test_native_evidence_and_final_answer_both_required(self):
        result = verify_live(events(), SAMPLE, REPORT)
        self.assertEqual(result['answer']['architecture'], 'x64')
        mutations = []
        for field, value in [('server', 'other'), ('tool', 'read_report'), ('status', 'failed'),
                             ('error', {'message': 'private provider error'}), ('result', None),
                             ('arguments', {'path': str(SAMPLE.with_name('other.dll'))})]:
            changed = events()
            changed[0]['item'][field] = value
            mutations.append(changed)
        changed = events()
        changed[0]['item']['result']['structured_content']['report']['data']['machine'] = 0x14c
        mutations.append(changed)
        mutations.append(events()[1:])  # A correct guessed answer without evidence cannot pass.
        mutations.append(events()[:1] + events()[2:])
        for changed in mutations:
            with self.subTest(changed=changed), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT)

    def test_mutually_consistent_but_wrong_tool_evidence_cannot_pass(self):
        changed = events()
        result = changed[0]['item']['result']
        result['structured_content']['report']['data']['machine'] = 0x14c
        result['content'][0]['text'] = json.dumps(result['structured_content'])
        with self.assertRaises(HostCheckError):
            verify_live(changed, SAMPLE, REPORT)

    def test_answer_types_and_static_limit_are_checked(self):
        for key, value in [('architecture', 'x86'), ('export_count', True), ('image_size_bytes', 1),
                           ('is_dll', 1), ('loadability_verified', True)]:
            changed = events()
            answer = expected_answer(REPORT)
            answer[key] = value
            changed[1]['item']['text'] = json.dumps(answer)
            with self.subTest(key=key), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT)

    def test_unrelated_activity_and_excessive_tool_calls_fail(self):
        for kind in ['command_execution', 'web_search', 'file_change', 'collab_tool_call', 'error']:
            with self.subTest(kind=kind), self.assertRaises(HostCheckError):
                verify_live([*events(), {'type': 'item.completed', 'item': {'type': kind}}], SAMPLE, REPORT)
        with self.assertRaises(HostCheckError):
            verify_live([events()[0]] * 4 + events()[1:], SAMPLE, REPORT)

    def test_fixture_only_returns_text_and_bounds_requests(self):
        with DiscoveryModel(True) as fixture:
            response = fixture.respond('/v1/responses', {'stream': True, 'tools': catalog()})
            self.assertIn('response.completed', response)
            self.assertNotIn('function_call', response)
            fixture.verify_complete()
            with self.assertRaises(HostCheckError):
                fixture.respond('/v1/responses', {'stream': True, 'tools': catalog()})

    def test_command_does_not_supply_mcp_configuration_or_persist_trust(self):
        argv = command('codex.exe', SAMPLE.parent, True, 'model')
        self.assertIn('--ignore-user-config', argv)
        self.assertIn('--ephemeral', argv)
        self.assertIn('read-only', argv)
        self.assertFalse(any('mcp_servers' in arg for arg in argv))
        self.assertFalse(any('CODEX_HOME' in arg for arg in argv))
        self.assertFalse(any('model_providers.openai' in arg for arg in argv))
        with DiscoveryModel(False) as fixture:
            isolated = command('codex.exe', SAMPLE.parent, False, 'fixture', fixture)
            self.assertIn('model_provider="fixture"', isolated)
            self.assertTrue(any(fixture.url in arg for arg in isolated))

    def test_output_failure_retains_summary_and_usage_without_overwriting(self):
        summary = {'status': 'passed', 'live_model': {'usage': {'input_tokens': 123}}}
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'existing.json'
            output.write_text('keep', encoding='utf-8')
            for target in [output, Path(tmp) / 'missing-parent' / 'summary.json']:
                stdout, stderr = io.StringIO(), io.StringIO()
                with redirect_stdout(stdout), redirect_stderr(stderr):
                    self.assertEqual(emit_summary(summary, target), 1)
                self.assertEqual(json.loads(stdout.getvalue()), summary)
                self.assertNotIn(tmp, stderr.getvalue())
                self.assertNotIn('Traceback', stderr.getvalue())
            self.assertEqual(output.read_text(encoding='utf-8'), 'keep')

    def test_missing_output_parent_is_rejected_before_client_or_model(self):
        with tempfile.TemporaryDirectory() as tmp:
            argv = ['codex_acceptance.py', '--archive', 'archive.tgz', '--codex', 'codex.exe',
                    '--dll', 'fixture.dll', '--live-model', 'model', '--output', str(Path(tmp) / 'missing' / 'out.json')]
            with patch('sys.argv', argv), patch('codex_acceptance.native_executable') as native, redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    main()
                self.assertEqual(error.exception.code, 2)
                native.assert_not_called()


if __name__ == '__main__':
    unittest.main()
