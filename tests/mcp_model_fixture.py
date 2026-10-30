"""Deterministic loopback model fixture; never forwards requests or uses credentials."""
import json
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit

TOOLS = {'inspect_pe', 'analyze_dependencies', 'list_processes', 'inspect_process',
         'list_modules', 'diagnose_dll', 'read_report'}
MAX_BODY = 8 * 1024 * 1024


class HostCheckError(RuntimeError):
    pass


def same_json(left, right):
    # Python considers True == 1 and 1 == 1.0; evidence comparison must not.
    return json.dumps(left, sort_keys=True, allow_nan=False) == json.dumps(right, sort_keys=True, allow_nan=False)


def verify_catalog(tools, prefix='', model_wire=False):
    if not isinstance(tools, list):
        raise HostCheckError('invalid tool catalog')
    rows = [row.get('function', row) if isinstance(row, dict) else {} for row in tools]
    names = [row.get('name') for row in rows]
    if len(names) != len(TOOLS) or set(names) != {prefix + tool for tool in TOOLS}:
        raise HostCheckError('tool catalog must contain exactly our seven tools')
    for row in rows:
        schema = row.get('input_schema', row.get('parameters')) if model_wire else row.get('inputSchema')
        if not isinstance(schema, dict) or schema.get('additionalProperties') is not False:
            raise HostCheckError('host did not preserve strict input schemas')
        timeout = schema.get('properties', {}).get('timeout_ms', {})
        if timeout.get('minimum') != 0 or timeout.get('maximum') != 30000:
            raise HostCheckError('host changed timeout bounds')
        if not model_wire and row.get('annotations', {}).get('readOnlyHint') is not True:
            raise HostCheckError('host did not preserve read-only annotations')


def decode_content(content):
    if isinstance(content, list):
        if len(content) != 1 or content[0].get('type') != 'text':
            raise HostCheckError('expected one JSON text tool result')
        content = content[0].get('text')
    if not isinstance(content, str):
        raise HostCheckError('tool result is not JSON text')
    try:
        value = json.loads(content)
    except (ValueError, TypeError):
        raise HostCheckError('tool result contains invalid JSON') from None
    if not isinstance(value, dict):
        raise HostCheckError('tool result is not an object')
    return value


def verify_result(actual, case):
    if set(actual) != {'source', 'cli_exit_code', 'report', 'error'}:
        raise HostCheckError('invalid adapter envelope')
    if case['label'] == 'policy_error':
        if (actual['source'] != 'adapter' or actual['report'] is not None
                or actual['cli_exit_code'] is not None or not isinstance(actual['error'], dict)
                or actual['error'].get('code') != 'PATH_POLICY' or not actual['error'].get('message')):
            raise HostCheckError('policy failure was not preserved')
        return
    expected = case['expected']
    if (actual['source'] != expected['source'] or type(actual['cli_exit_code']) is not int
            or actual['cli_exit_code'] != expected.get('cli_exit_code', 0)
            or actual['error'] is not None or not isinstance(actual['report'], dict)):
        raise HostCheckError('successful report envelope differs from native result')
    report = dict(actual['report'])
    baseline = dict(expected['report'])
    if actual['source'] == 'live_cli':
        duration = report.pop('duration_ms', None)
        if isinstance(duration, bool) or not isinstance(duration, int) or duration < 0:
            raise HostCheckError('invalid live observation duration')
        baseline.pop('duration_ms')
    if not same_json(report, baseline):
        raise HostCheckError('host report differs from native CLI evidence')


class ModelFixture:
    """Request one known tool at a time, and accept only verified host tool results."""
    def __init__(self, style, cases):
        self.style, self.cases = style, cases
        self.results = {}
        self.issued = set()
        self.requests = 0
        self.catalog_verified = False
        self.failure = None
        self.lock = threading.Lock()
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_args):
                pass  # Never log headers, prompts, credentials or report contents.

            def do_POST(self):
                try:
                    length = int(self.headers.get('Content-Length', '0'))
                    if not 0 < length <= MAX_BODY:
                        raise HostCheckError('model fixture request size limit')
                    self.connection.settimeout(10)
                    body = json.loads(self.rfile.read(length))
                    with fixture.lock:
                        events = fixture.respond(urlsplit(self.path).path, body)
                    encoded = events.encode('utf-8')
                    self.send_response(200)
                    self.send_header('Content-Type', 'text/event-stream')
                    self.send_header('Content-Length', str(len(encoded)))
                    self.end_headers()
                    self.wfile.write(encoded)
                except (HostCheckError, ValueError, KeyError, TypeError, OSError) as error:
                    fixture.failure = str(error) if isinstance(error, HostCheckError) else 'invalid local model protocol'
                    try:
                        self.send_error(400, 'Invalid fixture request')
                    except OSError:
                        pass

        self.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)

    @property
    def url(self):
        return f'http://127.0.0.1:{self.server.server_port}'

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *_args):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=5)

    def respond(self, path, body):
        expected_path = '/v1/messages' if self.style == 'claude' else '/v1/chat/completions'
        if path != expected_path or not isinstance(body, dict) or body.get('stream') is not True:
            raise HostCheckError('unexpected local model endpoint or non-streaming request')
        self.requests += 1
        if self.requests > len(self.cases) + 3:
            raise HostCheckError('host exceeded the fixed model request budget')
        prefix = 'mcp__dll_doctor__' if self.style == 'claude' else 'dll_doctor_'
        verify_catalog(body.get('tools'), prefix, model_wire=True)
        self.catalog_verified = True
        for message in body.get('messages', []):
            if message.get('role') == 'tool':
                self.accept(message.get('tool_call_id'), message.get('content'))
            if isinstance(message.get('content'), list):
                for block in message['content']:
                    if block.get('type') == 'tool_result':
                        self.accept(block.get('tool_use_id'), block.get('content'))
        index = len(self.results)
        call = self.cases[index] if index < len(self.cases) else None
        if call:
            self.issued.add(index)
        name = prefix + call['tool'] if call else None
        call_id = f'fixture_{index}'
        if self.style == 'claude':
            message = {'id': 'msg_fixture', 'type': 'message', 'role': 'assistant', 'content': [],
                'model': body['model'], 'stop_reason': None, 'stop_sequence': None,
                'usage': {'input_tokens': 1, 'output_tokens': 0}}
            block = {'type': 'tool_use', 'id': call_id, 'name': name, 'input': {}} if call else {'type': 'text', 'text': ''}
            delta = {'type': 'input_json_delta', 'partial_json': json.dumps(call['arguments'])} if call else {
                'type': 'text_delta', 'text': 'Local fixture complete.'}
            events = [{'type': 'message_start', 'message': message},
                {'type': 'content_block_start', 'index': 0, 'content_block': block},
                {'type': 'content_block_delta', 'index': 0, 'delta': delta},
                {'type': 'content_block_stop', 'index': 0},
                {'type': 'message_delta', 'delta': {'stop_reason': 'tool_use' if call else 'end_turn',
                    'stop_sequence': None}, 'usage': {'output_tokens': 1}}, {'type': 'message_stop'}]
            return ''.join(f'event: {event["type"]}\ndata: {json.dumps(event)}\n\n' for event in events)
        delta = {'role': 'assistant', 'tool_calls': [{'index': 0, 'id': call_id, 'type': 'function',
            'function': {'name': name, 'arguments': json.dumps(call['arguments'])}}]} if call else {
            'role': 'assistant', 'content': 'Local fixture complete.'}
        events = [{'id': 'chatcmpl_fixture', 'object': 'chat.completion.chunk', 'created': 1,
            'model': body['model'], 'choices': [{'index': 0, 'delta': value, 'finish_reason': finish}]}
            for value, finish in [(delta, None), ({}, 'tool_calls' if call else 'stop')]]
        return ''.join(f'data: {json.dumps(event)}\n\n' for event in events) + 'data: [DONE]\n\n'

    def accept(self, call_id, content):
        if not isinstance(call_id, str) or not call_id.startswith('fixture_'):
            raise HostCheckError('host returned an unrequested tool result')
        try:
            index = int(call_id.removeprefix('fixture_'))
        except ValueError:
            raise HostCheckError('invalid fixture call id') from None
        if call_id != f'fixture_{index}' or index not in self.issued or index > len(self.results):
            raise HostCheckError('host returned an out-of-order tool result')
        result = decode_content(content)
        verify_result(result, self.cases[index])
        if index in self.results and not same_json(self.results[index], result):
            raise HostCheckError('host changed an earlier tool result')
        self.results[index] = result

    def verify_complete(self):
        if self.failure:
            raise HostCheckError(self.failure)
        if not self.catalog_verified or len(self.results) != len(self.cases):
            raise HostCheckError('host did not execute and return every fixture tool result')
