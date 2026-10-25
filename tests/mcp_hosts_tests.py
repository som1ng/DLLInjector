"""Focused guards against false positive host validation; no installed Agents needed."""
import copy
from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from urllib.request import ProxyHandler, Request, build_opener
from unittest.mock import patch

from mcp_hosts import JsonProcess, clean_environment, main, toml
from mcp_model_fixture import TOOLS, HostCheckError, ModelFixture, decode_content, verify_catalog, verify_result

ROOT = Path(__file__).resolve().parents[1]
REPORT = json.loads((ROOT / 'tests/fixtures/json/access-denied.json').read_text(encoding='utf-8'))


def catalog(style='codex'):
    prefix = {'codex': '', 'claude': 'mcp__dll_doctor__', 'opencode': 'dll_doctor_'}[style]
    rows = []
    for name in sorted(TOOLS):
        schema = {'type': 'object', 'additionalProperties': False,
                  'properties': {'timeout_ms': {'type': 'integer', 'minimum': 0, 'maximum': 30000}}}
        row = {'name': prefix + name, 'annotations': {'readOnlyHint': True},
               {'codex': 'inputSchema', 'claude': 'input_schema', 'opencode': 'parameters'}[style]: schema}
        rows.append({'type': 'function', 'function': row} if style == 'opencode' else row)
    return rows


def cases():
    return [{'label': 'read_report', 'tool': 'read_report', 'arguments': {'path': 'C:\\fixture.json'},
             'expected': {'source': 'saved_report', 'report': copy.deepcopy(REPORT)}}]


def envelope():
    return {'source': 'saved_report', 'cli_exit_code': 0, 'report': copy.deepcopy(REPORT), 'error': None}


class HostValidationTests(unittest.TestCase):
    def test_catalog_rejects_missing_extra_duplicate_and_wrong_server(self):
        verify_catalog(catalog())
        for rows in (catalog()[:-1], catalog() + catalog()[:1], catalog()[:-1] + catalog()[:1], catalog('claude')):
            with self.subTest(rows=len(rows)), self.assertRaises(HostCheckError):
                verify_catalog(rows)

    def test_catalog_preserves_schema_and_annotations(self):
        for field, value in [('additionalProperties', True), ('properties', {})]:
            rows = catalog()
            rows[0]['inputSchema'][field] = value
            with self.assertRaises(HostCheckError):
                verify_catalog(rows)
        rows = catalog()
        rows[0]['annotations']['readOnlyHint'] = False
        with self.assertRaises(HostCheckError):
            verify_catalog(rows)

    def test_decode_requires_exact_json_text(self):
        self.assertEqual(decode_content([{'type': 'text', 'text': json.dumps(envelope())}]), envelope())
        for content in ('not JSON', '[]', None, [], [{'type': 'image'}], [{'type': 'text', 'text': '{}'}] * 2):
            with self.subTest(content=content), self.assertRaises(HostCheckError):
                decode_content(content)

    def test_snapshot_failure_and_duration_are_preserved(self):
        verify_result(envelope(), cases()[0])
        changed = envelope()
        changed['report']['duration_ms'] += 1
        with self.assertRaises(HostCheckError):
            verify_result(changed, cases()[0])
        changed = envelope()
        changed['report']['status'] = 'success'
        with self.assertRaises(HostCheckError):
            verify_result(changed, cases()[0])

    def test_live_allows_only_duration_to_change(self):
        case = cases()[0]
        case['expected']['source'] = 'live_cli'
        actual = envelope()
        actual['source'] = 'live_cli'
        actual['report']['duration_ms'] = 100
        verify_result(actual, case)
        for duration in (-1, True, 1.5, None):
            actual['report']['duration_ms'] = duration
            with self.assertRaises(HostCheckError):
                verify_result(actual, case)

    def test_json_evidence_types_are_not_coerced(self):
        for edit in ('boolean', 'number', 'exit'):
            actual = envelope()
            if edit == 'boolean':
                actual['report']['execution']['operation_completed'] = 0
            elif edit == 'number':
                actual['report']['duration_ms'] = float(actual['report']['duration_ms'])
            else:
                actual['cli_exit_code'] = False
            with self.subTest(edit=edit), self.assertRaises(HostCheckError):
                verify_result(actual, cases()[0])

    def test_policy_error_is_not_a_success_report(self):
        actual = {'source': 'adapter', 'cli_exit_code': None, 'report': None,
                  'error': {'code': 'PATH_POLICY', 'message': 'absolute local path required'}}
        verify_result(actual, {'label': 'policy_error'})
        actual['error']['code'] = 'INTERNAL'
        with self.assertRaises(HostCheckError):
            verify_result(actual, {'label': 'policy_error'})

    def test_fixture_requires_real_returned_results(self):
        fixture = ModelFixture('claude', cases())
        try:
            body = {'stream': True, 'model': 'fixture', 'tools': catalog('claude'), 'messages': []}
            self.assertIn('tool_use', fixture.respond('/v1/messages', body))
            with self.assertRaises(HostCheckError):
                fixture.verify_complete()
            body['messages'] = [{'role': 'user', 'content': [{'type': 'tool_result',
                'tool_use_id': 'fixture_0', 'content': json.dumps(envelope())}]}]
            self.assertIn('end_turn', fixture.respond('/v1/messages', body))
            fixture.verify_complete()
        finally:
            fixture.server.server_close()

    def test_fixture_rejects_unsolicited_reordered_and_changed_results(self):
        fixture = ModelFixture('claude', cases())
        try:
            with self.assertRaises(HostCheckError):
                fixture.accept('fixture_0', json.dumps(envelope()))
            fixture.issued.add(0)
            for call_id in ('wrong', 'fixture_-1', 'fixture_1', 'fixture_00'):
                with self.assertRaises(HostCheckError):
                    fixture.accept(call_id, json.dumps(envelope()))
            fixture.accept('fixture_0', json.dumps(envelope()))
            changed = envelope()
            changed['source'] = 'live_cli'
            with self.assertRaises(HostCheckError):
                fixture.accept('fixture_0', json.dumps(changed))
        finally:
            fixture.server.server_close()

    def test_loopback_http_openai_stream_roundtrip(self):
        with ModelFixture('opencode', cases()) as fixture:
            body = {'stream': True, 'model': 'fixture', 'tools': catalog('opencode'), 'messages': []}
            def post():
                request = Request(fixture.url + '/v1/chat/completions', json.dumps(body).encode(),
                                  {'Content-Type': 'application/json'})
                with build_opener(ProxyHandler({})).open(request, timeout=5) as response:
                    return response.read().decode()
            self.assertIn('dll_doctor_read_report', post())
            body['messages'] = [{'role': 'tool', 'tool_call_id': 'fixture_0', 'content': json.dumps(envelope())}]
            self.assertIn('[DONE]', post())
            fixture.verify_complete()

    def test_fixture_rejects_wrong_endpoints_and_unbounded_requests(self):
        fixture = ModelFixture('claude', cases())
        try:
            body = {'stream': True, 'model': 'fixture', 'tools': catalog('claude'), 'messages': []}
            with self.assertRaises(HostCheckError):
                fixture.respond('/v1/responses', body)
            fixture.requests = 4
            with self.assertRaises(HostCheckError):
                fixture.respond('/v1/messages', body)
        finally:
            fixture.server.server_close()

    def test_environment_and_toml_preserve_isolation(self):
        with patch.dict('os.environ', {'ANTHROPIC_API_KEY': 'do-not-copy', 'NODE_OPTIONS': 'do-not-copy'}):
            env = clean_environment()
        self.assertNotIn('ANTHROPIC_API_KEY', env)
        self.assertNotIn('NODE_OPTIONS', env)
        import tomllib
        value = {'command': 'C:\\Program Files\\node.exe', 'args': ['中文 path', 'a"b'], 'enabled': True}
        self.assertEqual(tomllib.loads('server=' + toml(value))['server'], value)

    def test_protocol_errors_and_silent_timeout_do_not_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            for script in ('print("not-json")', 'import time;time.sleep(10)'):
                proc = JsonProcess([sys.executable, '-u', '-c', script], directory, .1)
                try:
                    with self.assertRaises(HostCheckError):
                        proc.wait(lambda _value: True)
                finally:
                    proc.close()

    def test_existing_summary_is_never_overwritten(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'existing.json'
            output.write_text('keep', encoding='utf-8')
            with patch.object(sys, 'argv', ['mcp_hosts.py', '--codex', 'missing.exe', '--server', 'missing.js',
                     '--cli', 'missing.exe', '--dll', 'missing.dll', '--output', str(output)]), redirect_stdout(io.StringIO()):
                self.assertEqual(main(), 1)
            self.assertEqual(output.read_text(encoding='utf-8'), 'keep')


if __name__ == '__main__':
    unittest.main()
