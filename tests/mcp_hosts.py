"""Opt-in real-host MCP checks. Uses direct RPC or a local model fixture; no paid inference."""
import argparse
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import time

from mcp_model_fixture import HostCheckError, ModelFixture, decode_content, same_json, verify_catalog, verify_result

ROOT = Path(__file__).resolve().parents[1]
NAME = 'dll_doctor'
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


def toml(value):
    if isinstance(value, dict):
        return '{' + ','.join(json.dumps(k) + '=' + toml(v) for k, v in value.items()) + '}'
    if isinstance(value, list):
        return '[' + ','.join(map(toml, value)) + ']'
    return json.dumps(value, ensure_ascii=False)


def clean_environment():
    # Preserve Windows runtime locations, not provider keys, hooks or injected Node options.
    allowed = {'PATH', 'SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'TEMP', 'TMP', 'USERPROFILE',
        'APPDATA', 'LOCALAPPDATA', 'PROGRAMDATA', 'PROGRAMFILES', 'PROGRAMFILES(X86)',
        'SYSTEMDRIVE', 'HOMEDRIVE', 'HOMEPATH'}
    return {key: value for key, value in os.environ.items() if key.upper() in allowed}


def native_executable(value):
    path = Path(value).resolve()
    if not path.is_file() or path.suffix.lower() != '.exe':
        raise HostCheckError('provide an installed native .exe, not a shell shim')
    return str(path)


def run(args, cwd, timeout):
    result = subprocess.run(args, cwd=cwd, capture_output=True, timeout=timeout,
                            creationflags=NO_WINDOW)
    if result.returncode != 0 or len(result.stdout) > 4 * 1024 * 1024:
        raise HostCheckError('native fixture/version command failed or exceeded output limit')
    return result.stdout.decode('utf-8')


class JsonProcess:
    def __init__(self, command, cwd, timeout, env=None):
        self.timeout = timeout
        self.process = subprocess.Popen(command, cwd=cwd, env=env,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            creationflags=NO_WINDOW)
        self.inbox = queue.Queue()
        self.worker = threading.Thread(target=self.read, daemon=True)
        self.worker.start()

    def read(self):
        total = 0
        try:
            while line := self.process.stdout.readline(4 * 1024 * 1024 + 1):
                total += len(line)
                if len(line) > 4 * 1024 * 1024 or total > 8 * 1024 * 1024:
                    raise ValueError('output limit')
                value = json.loads(line.decode('utf-8'))
                if not isinstance(value, dict):
                    raise ValueError('invalid message')
                self.inbox.put(value)
        except (ValueError, UnicodeError, OSError):
            self.inbox.put(HostCheckError('host returned invalid or excessive protocol output'))
        finally:
            self.inbox.put(None)

    def send(self, value):
        self.process.stdin.write((json.dumps(value, ensure_ascii=False) + '\n').encode('utf-8'))
        self.process.stdin.flush()

    def wait(self, match, allow_eof=False):
        deadline = time.monotonic() + self.timeout
        while True:
            try:
                value = self.inbox.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise HostCheckError('host protocol deadline exceeded') from None
            if isinstance(value, HostCheckError):
                raise value
            if value is None:
                if allow_eof:
                    return None
                raise HostCheckError('host closed before returning the expected response')
            if match(value):
                return value

    def rpc(self, request_id, method, params):
        self.send({'id': request_id, 'method': method, 'params': params})
        response = self.wait(lambda value: value.get('id') == request_id)
        if 'error' in response or not isinstance(response.get('result'), dict):
            raise HostCheckError('host rejected the requested app-server operation')
        return response['result']

    def close(self):
        try:
            self.process.stdin.close()
        except (BrokenPipeError, OSError):
            pass
        try:
            self.process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            # The handle belongs to our child. Never kill by executable name or
            # terminate a user's independently running Codex/Claude/OpenCode.
            subprocess.run(['taskkill', '/PID', str(self.process.pid), '/T', '/F'],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5,
                creationflags=NO_WINDOW)
            self.process.wait(timeout=5)
        self.worker.join(timeout=2)
        self.process.stdout.close()


def make_cases(cli, dll, directory, timeout):
    sample = directory / '插件 space 🚀.dll'
    shutil.copyfile(dll, sample)
    live = json.loads(run([cli, 'pe', 'inspect', str(sample), '--json'], directory, timeout))
    saved = json.loads((ROOT / 'tests/fixtures/json/access-denied.json').read_text(encoding='utf-8'))
    report = directory / '历史 report.json'
    report.write_text(json.dumps(saved, ensure_ascii=False), encoding='utf-8')
    expected = {'source': 'live_cli', 'report': live}
    return [
        {'label': 'inspect_pe', 'tool': 'inspect_pe', 'arguments': {'path': str(sample)}, 'expected': expected},
        {'label': 'read_report', 'tool': 'read_report', 'arguments': {'path': str(report)},
            'expected': {'source': 'saved_report', 'report': saved}},
        {'label': 'policy_error', 'tool': 'inspect_pe', 'arguments': {'path': 'relative.dll'}},
        {'label': 'recovery', 'tool': 'inspect_pe', 'arguments': {'path': str(sample)}, 'expected': expected},
    ]


def codex_check(exe, node, server_args, directory, cases, timeout):
    # Direct MCP calls do not need a model. Route any unexpected inference to a
    # rejecting loopback fixture, with no real provider credentials.
    with ModelFixture('codex', []) as fixture:
        config = {'mcp_servers': {NAME: {'command': node, 'args': server_args}},
            'features.apps': False, 'features.plugins': False, 'analytics.enabled': False,
            'model_provider': 'fixture', 'model': 'fixture',
            'model_providers.fixture': {'name': 'fixture', 'base_url': fixture.url + '/v1', 'wire_api': 'responses'}}
        command = [exe, 'app-server', '--stdio']
        for key, value in config.items():
            command.extend(['-c', key + '=' + toml(value)])
        # Inherit CODEX_HOME unchanged if configured, for the installed binary's
        # normal settings resolution; never replace it with a fixture directory.
        env = clean_environment()
        if 'CODEX_HOME' in os.environ:
            env['CODEX_HOME'] = os.environ['CODEX_HOME']
        proc = JsonProcess(command, directory, timeout, env)
        try:
            proc.rpc(1, 'initialize', {'clientInfo': {'name': 'dll_doctor_host_test', 'version': '0.1.0'},
                'capabilities': {'experimentalApi': True}})
            proc.send({'method': 'initialized', 'params': {}})
            started = proc.rpc(2, 'thread/start', {'cwd': str(directory), 'ephemeral': True,
                'approvalPolicy': 'never', 'sandbox': 'read-only'})
            thread_id = started['thread']['id']
            status = proc.rpc(3, 'mcpServerStatus/list', {'threadId': thread_id,
                'serverName': NAME, 'detail': 'toolsAndAuthOnly'})
            rows = status.get('data')
            if (not isinstance(rows, list) or len(rows) != 1 or rows[0].get('name') != NAME
                    or rows[0].get('runtimeStatus') != 'connected'
                    or rows[0].get('serverInfo', {}).get('name') != 'dll-doctor'
                    or status.get('nextCursor') is not None or rows[0].get('toolsError')):
                raise HostCheckError('Codex did not connect to the expected MCP server')
            verify_catalog(list(rows[0]['tools'].values()))
            for request_id, case in enumerate(cases, 4):
                result = proc.rpc(request_id, 'mcpServer/tool/call', {'threadId': thread_id,
                    'server': NAME, 'tool': case['tool'], 'arguments': case['arguments']})
                content = decode_content(result.get('content'))
                if not same_json(content, result.get('structuredContent')):
                    raise HostCheckError('Codex changed the structured/text result relationship')
                verify_result(content, case)
                if result.get('isError') is not (case['label'] == 'policy_error'):
                    raise HostCheckError('Codex changed the MCP error flag')
            if fixture.failure or fixture.requests:
                raise HostCheckError('Codex unexpectedly requested model inference')
        finally:
            proc.close()
    return {'method': 'app-server direct MCP RPC', 'model_requests': 0}


def model_host_check(host, exe, node, server_args, directory, cases, timeout):
    with ModelFixture(host, cases) as fixture:
        env = clean_environment()
        if host == 'claude':
            env.update(CLAUDE_CONFIG_DIR=str(directory / 'claude-state'),
                ANTHROPIC_API_KEY='fixture-not-a-real-key', ANTHROPIC_BASE_URL=fixture.url,
                CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC='1', DISABLE_AUTOUPDATER='1')
            command = [exe, '--bare', '--print', '--output-format', 'stream-json', '--verbose',
                '--no-session-persistence', '--setting-sources', '', '--strict-mcp-config',
                '--mcp-config', json.dumps({'mcpServers': {NAME: {'command': node, 'args': server_args}}}),
                '--tools', '', '--allowedTools', 'mcp__dll_doctor__*', '--permission-mode', 'dontAsk',
                '--model', 'claude-sonnet-4-6', 'Run the local MCP integration fixture.']
        else:
            config = {'model': 'fixture/fixture', 'small_model': 'fixture/fixture', 'enabled_providers': ['fixture'],
                'provider': {'fixture': {'npm': '@ai-sdk/openai-compatible', 'name': 'Local Fixture',
                    'options': {'baseURL': fixture.url + '/v1', 'apiKey': 'fixture-not-a-real-key'},
                    'models': {'fixture': {'name': 'Fixture', 'limit': {'context': 128000, 'output': 8192}}}}},
                'mcp': {NAME: {'type': 'local', 'command': [node, *server_args], 'enabled': True}},
                'permission': {'*': 'deny', 'dll_doctor_*': 'allow'}}
            env.update(OPENCODE_CONFIG_CONTENT=json.dumps(config), OPENCODE_CONFIG_DIR=str(directory / 'opencode-config'),
                OPENCODE_DB=str(directory / 'opencode.sqlite'), OPENCODE_DISABLE_AUTOUPDATE='true',
                OPENCODE_DISABLE_MODELS_FETCH='true', OPENCODE_DISABLE_PRUNE='true',
                OPENCODE_DISABLE_DEFAULT_PLUGINS='true', OPENCODE_DISABLE_PROJECT_CONFIG='true',
                XDG_CONFIG_HOME=str(directory / 'config'), XDG_DATA_HOME=str(directory / 'data'),
                XDG_CACHE_HOME=str(directory / 'cache'), XDG_STATE_HOME=str(directory / 'state'))
            command = [exe, 'run', '--pure', '--format', 'json', '--model', 'fixture/fixture',
                '--title', 'Local MCP fixture', 'Run the local MCP integration fixture.']
        proc = JsonProcess(command, directory, timeout, env)
        try:
            # These CLI runs receive their prompt in argv, unlike Codex RPC.
            # OpenCode also consumes piped stdin, so explicitly signal EOF.
            proc.process.stdin.close()
            if host == 'claude':
                result = proc.wait(lambda value: value.get('type') == 'result')
                if result.get('is_error') is not False or result.get('subtype') != 'success':
                    raise HostCheckError('Claude did not finish the fixture turn successfully')
            else:
                proc.wait(lambda _value: False, allow_eof=True)
            fixture.verify_complete()
        except HostCheckError:
            if fixture.failure:
                raise HostCheckError(fixture.failure) from None
            raise
        finally:
            proc.close()
        if proc.process.returncode != 0:
            raise HostCheckError('host exited unsuccessfully after the fixture')
        return {'method': 'real host with deterministic loopback model',
                'model_requests': fixture.requests, 'model_endpoint': 'loopback_fixture'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for host in ('codex', 'claude', 'opencode'):
        parser.add_argument('--' + host, help='installed native client .exe; omitted clients are not tested')
    parser.add_argument('--node', default='node', help='installed Node executable')
    parser.add_argument('--server', type=Path, required=True, help='built MCP dist/index.js')
    parser.add_argument('--cli', required=True, help='matching native DLLInjector CLI .exe')
    parser.add_argument('--dll', type=Path, required=True, help='inert noop_fixture.dll built by this repository')
    parser.add_argument('--timeout', type=int, default=60, help='operation deadline in seconds (10-120)')
    parser.add_argument('--output', type=Path, help='new sanitized JSON summary; refuses overwrite')
    args = parser.parse_args()
    selected = {host: getattr(args, host) for host in ('codex', 'claude', 'opencode') if getattr(args, host)}
    if not selected or not 10 <= args.timeout <= 120:
        parser.error('select a client and a timeout from 10 to 120 seconds')
    if os.name != 'nt':
        parser.error('these installed-host checks require Windows')
    stage = 'inputs'
    try:
        if args.output and args.output.exists():
            raise HostCheckError('output already exists; choose a new path')
        clients = {name: native_executable(value) for name, value in selected.items()}
        cli = native_executable(args.cli)
        node = shutil.which(args.node)
        if not node or not args.server.is_file() or not args.dll.is_file():
            raise HostCheckError('Node, built MCP entry or DLL fixture is missing')
        server = str(args.server.resolve())
        checks = []
        with tempfile.TemporaryDirectory(prefix='dll-doctor-hosts-') as raw:
            base = Path(raw).resolve()
            for name, exe in clients.items():
                stage = name
                directory = base / f'{name} 中文 space 🚀'
                directory.mkdir()
                version = run([exe, '--version'], directory, args.timeout).strip()
                if not version or len(version) > 160:
                    raise HostCheckError('invalid client version response')
                cases = make_cases(cli, args.dll, directory, args.timeout)
                server_args = [server, '--cli', cli, '--allow-root', str(directory)]
                if name == 'codex':
                    details = codex_check(exe, node, server_args, directory, cases, args.timeout)
                else:
                    details = model_host_check(name, exe, node, server_args, directory, cases, args.timeout)
                checks.append({'client': name, 'version': version, 'tools_discovered': 7,
                    'checks': [case['label'] for case in cases], **details})
        summary = {'status': 'passed', 'external_model_calls': 0, 'model_behavior_evaluation': 'not_run', 'clients': checks}
        text = json.dumps(summary, ensure_ascii=False, indent=2) + '\n'
        if args.output:
            with args.output.open('x', encoding='utf-8') as output:
                output.write(text)
        print(text, end='')
        return 0
    except (HostCheckError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        message = str(error) if isinstance(error, HostCheckError) else 'host validation failed; verify paths, versions and local runtime'
        print(json.dumps({'status': 'failed', 'stage': stage, 'message': message}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
