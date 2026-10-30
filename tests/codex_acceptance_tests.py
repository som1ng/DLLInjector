"""Offline checks of acceptance evidence; never starts a real client or model."""
import copy
from contextlib import redirect_stderr, redirect_stdout
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from codex_acceptance import DiscoveryModel, command, emit_summary, expected_answer, main, verify_live, verify_model_catalog
from codex_diagnostics import diagnostic_answer, prepare_diagnostic
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

    def test_broken_stdout_write_or_flush_still_saves_summary_file(self):
        summary = {'status': 'passed', 'live_model': {'usage': {'input_tokens': 123}}}
        with tempfile.TemporaryDirectory() as tmp:
            for method in ['write', 'flush']:
                stream = io.StringIO()
                output = Path(tmp) / (method + '.json')
                with patch.object(stream, method, side_effect=BrokenPipeError), redirect_stdout(stream):
                    self.assertEqual(emit_summary(summary, output), 1)
                self.assertEqual(json.loads(output.read_text(encoding='utf-8')), summary)
            output = Path(tmp) / 'closed-pipe.json'
            code = ('import json,sys; from pathlib import Path; from codex_acceptance import emit_summary; '
                    'sys.stdin.buffer.read(1); sys.exit(emit_summary(json.loads(sys.argv[2]), Path(sys.argv[1])))')
            proc = subprocess.Popen([sys.executable, '-c', code, str(output), json.dumps(summary)],
                cwd=Path(__file__).resolve().parent, stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
            try:
                proc.stdout.close()
                # EOF releases the child only after its output pipe is closed.
                proc.stdin.close()
                self.assertNotEqual(proc.wait(timeout=10), 0)
                self.assertEqual(json.loads(output.read_text(encoding='utf-8')), summary)
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait(timeout=5)
                proc.stdout.close()
                proc.stdin.close()


def doctor_case(name):
    source = 'doctor-mismatch.json' if name == 'mismatch' else 'doctor-dependencies.json'
    report = json.loads((Path(__file__).parent / 'fixtures/json' / source).read_text(encoding='utf-8'))
    case = {'name': name, 'report': report, 'exit_code': 1 if name == 'mismatch' else 0}
    if name == 'error-126':
        case['search_dir'] = SAMPLE.parent / 'deps'
        dependencies = report['data']['dependencies']
        dependencies['libraries'] = dependencies['libraries'][:3]
        dependencies['libraries'][0]['name'] = 'VCRUNTIME140.dll'
        dependencies['search_directories'] = [str(case['search_dir'])]
    case['answer'] = diagnostic_answer(name, report)
    return case


def diagnostic_events(case, tool='diagnose_dll'):
    result = events()
    item = result[0]['item']
    item['tool'] = tool
    item['arguments'].update({'target_arch': 'x64'} if case['name'] == 'mismatch'
                             else {'search_directories': [str(case['search_dir'])]})
    envelope = {'source': 'live_cli', 'cli_exit_code': case['exit_code'],
                'report': copy.deepcopy(case['report']), 'error': None}
    item['result'] = {'content': [{'type': 'text', 'text': json.dumps(envelope)}], 'structured_content': envelope}
    result[1]['item']['text'] = json.dumps(case['answer'])
    return result


class DiagnosticAcceptanceTests(unittest.TestCase):
    def test_blocking_finding_is_completed_diagnosis_with_exit_one(self):
        case = doctor_case('mismatch')
        result = verify_live(diagnostic_events(case), SAMPLE, REPORT, case)
        self.assertEqual(result['answer']['diagnostic_exit_code'], 1)
        self.assertTrue(result['answer']['diagnosis_completed'])
        for code in [0, True, 2]:
            changed = diagnostic_events(case)
            envelope = changed[0]['item']['result']['structured_content']
            envelope['cli_exit_code'] = code
            changed[0]['item']['result']['content'][0]['text'] = json.dumps(envelope)
            with self.subTest(code=code), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)

    def test_declared_target_cannot_become_a_process_observation(self):
        case = doctor_case('mismatch')
        for key, value in [('target_architecture_source', 'process_observation'), ('architecture_matches', True),
                           ('loadability_verified', True), ('diagnosis_completed', False)]:
            changed = diagnostic_events(case)
            changed[1]['item']['text'] = json.dumps(dict(case['answer'], **{key: value}))
            with self.subTest(key=key), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)
        for target in ['x86', None]:
            changed = diagnostic_events(case)
            changed[0]['item']['arguments']['target_arch'] = target
            with self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)

    def test_both_dependency_tools_can_supply_evidence_and_names_ignore_case(self):
        case = doctor_case('error-126')
        for tool in ['diagnose_dll', 'analyze_dependencies']:
            changed = diagnostic_events(case, tool)
            answer = dict(case['answer'], candidate_names=['vcruntime140.DLL'])
            changed[1]['item']['text'] = json.dumps(answer)
            self.assertEqual(verify_live(changed, SAMPLE, REPORT, case)['tools'], [tool])

    def test_dependency_answer_preserves_scope_confidence_and_runtime_uncertainty(self):
        case = doctor_case('error-126')
        for key, value in [('root_cause_confirmed', True), ('candidate_loadability_verified', True),
                           ('loadability_verified', True), ('possible_missing_confidence', 'high'),
                           ('error_code_source', 'native_observation'), ('search_scope', 'windows_loader'),
                           ('api_set_names', []), ('missing_in_search_dirs', ['api-ms-win-core-file-l1-1-0.dll']),
                           ('candidate_names', ['VCRUNTIME140.dll', 'vcruntime140.dll'])]:
            changed = diagnostic_events(case)
            changed[1]['item']['text'] = json.dumps(dict(case['answer'], **{key: value}))
            with self.subTest(key=key), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)

    def test_search_scope_cannot_expand_or_guess_target_architecture(self):
        case = doctor_case('error-126')
        for args in [{'search_directories': []}, {'search_directories': ['relative']},
                     {'search_directories': [str(SAMPLE.parent)]},
                     {'search_directories': [str(case['search_dir'])] * 2}, {'target_arch': 'x64'}]:
            changed = diagnostic_events(case)
            changed[0]['item']['arguments'].update(args)
            with self.subTest(args=args), self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)

    def test_pe_preflight_is_allowed_but_not_a_substitute_for_doctor(self):
        for name in ['mismatch', 'error-126']:
            case = doctor_case(name)
            sequence = [events()[0], *diagnostic_events(case)]
            self.assertEqual(verify_live(sequence, SAMPLE, REPORT, case)['tool_calls'], 2)
            guessed = [events()[0], *diagnostic_events(case)[1:]]
            with self.assertRaises(HostCheckError):
                verify_live(guessed, SAMPLE, REPORT, case)
            changed = diagnostic_events(case)
            envelope = changed[0]['item']['result']['structured_content']
            envelope['report']['execution']['outcome_verified'] = True
            changed[0]['item']['result']['content'][0]['text'] = json.dumps(envelope)
            with self.assertRaises(HostCheckError):
                verify_live(changed, SAMPLE, REPORT, case)

    def test_incompatible_fixture_fails_before_invoking_doctor(self):
        invoke = unittest.mock.Mock()
        with self.assertRaises(HostCheckError):
            prepare_diagnostic('mismatch', SAMPLE, REPORT, invoke)
        no_imports = copy.deepcopy(REPORT)
        no_imports['data']['imports'] = []
        with self.assertRaises(HostCheckError):
            prepare_diagnostic('error-126', SAMPLE, no_imports, invoke)
        invoke.assert_not_called()


if __name__ == '__main__':
    unittest.main()
