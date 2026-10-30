"""Opt-in npm -> project discovery -> Codex acceptance; live inference requires --live-model."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from mcp_hosts import JsonProcess, NAME, NO_WINDOW, clean_environment, native_executable, toml
from mcp_model_fixture import HostCheckError, ModelFixture, TOOLS, decode_content, same_json, verify_result
from codex_diagnostics import answer_matches, diagnostic_call, prepare_diagnostic

ANSWER_SCHEMA = {
    'type': 'object', 'additionalProperties': False,
    'properties': {'architecture': {'type': 'string', 'enum': ['x86', 'x64']},
        'is_dll': {'type': 'boolean'}, 'image_size_bytes': {'type': 'integer'},
        'export_count': {'type': 'integer'}, 'loadability_verified': {'type': 'boolean'}},
    'required': ['architecture', 'is_dll', 'image_size_bytes', 'export_count', 'loadability_verified'],
}


def expected_answer(report):
    data = report['data']
    return {'architecture': {0x14c: 'x86', 0x8664: 'x64'}[data['machine']],
        'is_dll': bool(data['characteristics'] & 0x2000),
        'image_size_bytes': data['size_of_image'], 'export_count': len(data['exports']['symbols']),
        'loadability_verified': False}


def verify_model_catalog(tools, present):
    """Accept the installed client's flat or namespace wire format, never another MCP."""
    if not isinstance(tools, list) or any(not isinstance(row, dict) for row in tools):
        raise HostCheckError('invalid discovery tool catalog')
    own = []
    for row in tools:
        name = row.get('name', '')
        if not isinstance(name, str):
            raise HostCheckError('invalid discovery tool name')
        if name == 'mcp__dll_doctor' and row.get('type') == 'namespace':
            own.extend(row.get('tools', []))
        elif name.startswith('mcp__dll_doctor__'):
            own.append(dict(row, name=name.removeprefix('mcp__dll_doctor__')))
        elif name.startswith('mcp__'):
            raise HostCheckError('an unrelated MCP server reached the model')
    if present:
        if len(own) != len(TOOLS) or {row.get('name') for row in own} != TOOLS:
            raise HostCheckError('project discovery did not advertise exactly seven tools')
        # Codex 0.164.0 strips numeric bounds in its model-facing schema. Native
        # MCP schema/constraint preservation is checked separately by mcp_hosts.py.
        for row in own:
            schema = row.get('parameters', {})
            if (schema.get('type') != 'object' or schema.get('additionalProperties') is not False
                    or schema.get('properties', {}).get('timeout_ms', {}).get('type') != 'integer'):
                raise HostCheckError('model-facing tool parameter structure differs')
    elif own:
        raise HostCheckError('MCP tools appeared without a trusted installed project configuration')


class DiscoveryModel(ModelFixture):
    """Loopback Responses endpoint: observe discovery, return text, never request a tool."""
    def __init__(self, present):
        super().__init__('codex', [])
        self.present = present

    def respond(self, path, body):
        if path != '/v1/responses' or not isinstance(body, dict) or body.get('stream') is not True:
            raise HostCheckError('unexpected discovery model request')
        self.requests += 1
        if self.requests != 1:
            raise HostCheckError('discovery exceeded its single request budget')
        verify_model_catalog(body.get('tools'), self.present)
        self.catalog_verified = True
        message = {'type': 'message', 'id': 'msg_discovery', 'role': 'assistant', 'status': 'completed',
                   'content': [{'type': 'output_text', 'text': 'Discovery checked.', 'annotations': []}]}
        response = {'id': 'resp_discovery', 'object': 'response', 'model': 'fixture', 'status': 'completed',
                    'output': [message], 'usage': {'input_tokens': 1, 'output_tokens': 1, 'total_tokens': 2}}
        events = [
            {'type': 'response.created', 'response': dict(response, status='in_progress', output=[])},
            {'type': 'response.output_item.added', 'output_index': 0,
             'item': dict(message, status='in_progress', content=[])},
            {'type': 'response.output_text.delta', 'item_id': message['id'], 'output_index': 0,
             'content_index': 0, 'delta': 'Discovery checked.'},
            {'type': 'response.output_item.done', 'output_index': 0, 'item': message},
            {'type': 'response.completed', 'response': response},
        ]
        return ''.join('event: ' + row['type'] + '\ndata: ' + json.dumps(row) + '\n\n' for row in events)


def command(exe, project, trusted, model, fixture=None, schema=None):
    config = {'projects': {str(project): {'trust_level': 'trusted' if trusted else 'untrusted'}},
        'features.apps': False, 'features.plugins': False, 'features.hooks': False,
        'features.shell_tool': False, 'features.multi_agent': False, 'features.goals': False,
        'features.skip_host_skill_discovery': True, 'project_doc_max_bytes': 0,
        'suppress_unstable_features_warning': True,
        'web_search': 'disabled', 'model': model, 'model_reasoning_effort': 'low'}
    if fixture:
        config.update({'model_provider': 'fixture', 'model_providers.fixture': {
            'name': 'Local discovery', 'base_url': fixture.url + '/v1', 'wire_api': 'responses',
            'supports_websockets': False, 'request_max_retries': 0, 'stream_max_retries': 0}})
    else:
        # Built-in provider IDs are reserved in Codex 0.164.0. Keep its normal
        # auth/transport/retry policy; never redefine it to customize retries.
        config['model_provider'] = 'openai'
    argv = [exe, 'exec', '--ignore-user-config', '--ephemeral', '--json', '-s', 'read-only', '-C', str(project)]
    for key, value in config.items():
        argv += ['-c', key + '=' + toml(value)]
    if schema:
        argv += ['--output-schema', str(schema)]
    return argv


def execute(argv, cwd, timeout):
    env = clean_environment()
    if 'CODEX_HOME' in os.environ:
        env['CODEX_HOME'] = os.environ['CODEX_HOME']  # Preserve existing auth location, never replace/copy it.
    proc = JsonProcess(argv, cwd, timeout, env)
    events = []
    try:
        proc.process.stdin.close()
        # One deadline for the entire stream, with the shared reader's byte limits.
        proc.wait(lambda event: events.append(event) or False, allow_eof=True)
        proc.process.wait(timeout=5)
        if proc.process.returncode:
            raise HostCheckError('Codex exited unsuccessfully; authentication, provider or client failure')
    finally:
        proc.close()
    if (sum(event.get('type') == 'turn.completed' for event in events) != 1
            or any(event.get('type') in ('turn.failed', 'error') for event in events)):
        raise HostCheckError('Codex did not complete exactly one successful turn')
    return events


def verify_live(events, sample, baseline, diagnostic=None):
    calls, messages, usage = [], [], None
    diagnosed = False
    for event in events:
        if event.get('type') == 'turn.completed':
            usage = event.get('usage')
        if event.get('type') != 'item.completed':
            continue
        item = event.get('item', {})
        kind = item.get('type')
        if kind == 'agent_message':
            messages.append(item.get('text'))
        elif kind == 'mcp_tool_call':
            args = item.get('arguments', {})
            if (item.get('server') != NAME
                    or item.get('status') != 'completed' or item.get('error') is not None
                    or not isinstance(args, dict)
                    or not isinstance(args.get('path'), str) or not Path(args['path']).is_absolute()
                    or Path(args['path']).resolve() != sample.resolve()):
                raise HostCheckError('model did not call a successful tool on the requested fixture')
            if diagnostic:
                expected, is_diagnostic = diagnostic_call(item.get('tool'), args, diagnostic, baseline)
                diagnosed = diagnosed or is_diagnostic
            else:
                if item.get('tool') != 'inspect_pe' or set(args) - {'path', 'timeout_ms', 'max_output_bytes'}:
                    raise HostCheckError('model did not call inspect_pe on the requested fixture')
                expected = {'source': 'live_cli', 'report': baseline}
            result = item.get('result')
            if not isinstance(result, dict):
                raise HostCheckError('model tool call has no result')
            content = decode_content(result.get('content'))
            if not same_json(content, result.get('structured_content')):
                raise HostCheckError('model tool result lost structured evidence')
            verify_result(content, {'label': 'diagnostic' if diagnostic else 'inspect_pe', 'expected': expected})
            calls.append(item)
        elif kind not in ('reasoning',):
            raise HostCheckError('client reported a diagnostic in the model turn' if kind == 'error'
                                 else 'unexpected tool or activity in the model turn')
    if not 1 <= len(calls) <= 3 or not messages:
        raise HostCheckError('model did not produce bounded tool evidence and an answer')
    if diagnostic and not diagnosed:
        raise HostCheckError('model answered without the required Doctor evidence')
    try:
        answer = json.loads(messages[-1])
    except (ValueError, TypeError):
        raise HostCheckError('model did not return the requested structured answer') from None
    expected = diagnostic['answer'] if diagnostic else expected_answer(baseline)
    if not answer_matches(answer, expected):
        raise HostCheckError('model answer disagrees with native static evidence')
    if not isinstance(usage, dict) or any(type(v) is not int or v < 0 for v in usage.values()):
        raise HostCheckError('invalid model usage counters')
    return {'status': 'passed', 'turns': 1, 'tool_calls': len(calls), 'tools': sorted({call['tool'] for call in calls}),
            **({'tool': 'inspect_pe'} if not diagnostic else {}),
            'native_evidence': 'matched', 'answer': expected, 'usage': usage}


def run(argv, cwd, timeout=120, *, expected_code=0):
    result = subprocess.run([str(arg) for arg in argv], cwd=cwd, stdin=subprocess.DEVNULL,
        capture_output=True, timeout=timeout, creationflags=NO_WINDOW)
    if result.returncode != expected_code or len(result.stdout) > 4 * 1024 * 1024:
        raise HostCheckError('acceptance prerequisite failed; subprocess output is withheld')
    return result.stdout.decode('utf-8')


def emit_summary(summary, output_path):
    text = json.dumps(summary, ensure_ascii=False, indent=2)
    failed = False
    try:
        print(text, flush=True)
    except OSError:
        failed = True
    if output_path:
        try:
            with output_path.open('x', encoding='utf-8') as output:
                output.write(text + '\n')
        except OSError:
            failed = True
            try:
                print('Could not write the summary file; check stdout for the JSON result.', file=sys.stderr)
            except OSError:
                pass
    return 0 if summary['status'] == 'passed' and not failed else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', required=True, type=Path, help='trusted local Rondo npm .tgz; no registry download')
    parser.add_argument('--codex', required=True, help='installed native Codex .exe, tested with 0.164.0')
    parser.add_argument('--dll', required=True, type=Path, help='repository-built inert noop_fixture.dll (never loaded)')
    parser.add_argument('--scenario', choices=['pe', 'mismatch', 'error-126'], default='pe',
                        help='one scenario per invocation; mismatch requires the x86 fixture')
    parser.add_argument('--live-model', help='explicit opt-in: ONE real OpenAI turn using existing Codex login; consumes quota')
    parser.add_argument('--timeout', type=int, default=180, help='per Codex run deadline, seconds (30-300)')
    parser.add_argument('--output', type=Path, help='new sanitized summary file; refuses overwrite')
    args = parser.parse_args()
    if os.name != 'nt' or not 30 <= args.timeout <= 300:
        parser.error('requires Windows and a timeout of 30-300 seconds')
    if args.live_model and not re.fullmatch(r'[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}', args.live_model):
        parser.error('provide a model identifier, not a provider URL or credentials')
    if args.output and args.output.exists():
        parser.error('output already exists; choose a new file')
    if args.output and not args.output.resolve().parent.is_dir():
        parser.error('output directory does not exist')
    stage = 'inputs'
    summary = {'status': 'failed', 'client': 'codex', 'scenario': args.scenario,
               'live_model': {'status': 'not_run'}, 'checks': []}
    try:
        exe = native_executable(args.codex)
        archive, dll = args.archive.resolve(strict=True), args.dll.resolve(strict=True)
        npm = shutil.which('npm.cmd')
        if not npm:
            raise HostCheckError('npm.cmd is required')
        summary['archive_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
        version = run([exe, '--version'], archive.parent).strip()
        if not re.fullmatch(r'codex-cli [0-9A-Za-z.+-]+', version):
            raise HostCheckError('invalid client version output')
        summary['client_version'] = version
        with tempfile.TemporaryDirectory(prefix='rondo-codex-acceptance-') as tmp:
            base = Path(tmp).resolve()
            prefix, project = base / '中文 global', base / 'project'
            project.mkdir()
            run(['git', 'init', '-q', project], base)
            stage = 'offline npm install'
            npm_args = [npm, '--prefix', prefix, '--cache', base / 'empty-cache', '--offline',
                '--registry=http://127.0.0.1:9', '--ignore-scripts', '--no-audit', '--no-fund']
            run([*npm_args, 'install', '-g', archive], base)
            shim = prefix / 'rondo.cmd'
            stage = 'self-check'
            check = json.loads(run([shim, 'self-check', '--json'], project))
            if (check['status'] != 'success' or check['runtime_verification'] != 'passed'
                    or len(check['checks']) != 8 or any(row['status'] != 'passed' for row in check['checks'])):
                raise HostCheckError('installed package self-check failed')
            summary['checks'] += ['offline_npm_install', 'self_check_8']
            sample = project / '插件 sample.dll'
            shutil.copyfile(dll, sample)
            baseline = json.loads(run([shim, 'pe', 'inspect', sample, '--json'], project))
            if baseline['status'] != 'success' or not expected_answer(baseline)['is_dll']:
                raise HostCheckError('provide a successfully inspectable x86/x64 DLL fixture')
            stage = 'native diagnostic preflight'
            def invoke(arguments, expected_code=0):
                return json.loads(run([shim, *arguments, '--json'], project, expected_code=expected_code))
            diagnostic = prepare_diagnostic(args.scenario, sample, baseline, invoke)
            if diagnostic:
                summary['native_preflight'] = {'status': 'passed', 'cli_exit_code': diagnostic['exit_code'],
                                               'answer': diagnostic['answer']}

            def discovery(label, present, trusted):
                nonlocal stage
                stage = label
                with DiscoveryModel(present) as fixture:
                    try:
                        execute(command(exe, project, trusted, 'fixture', fixture) + ['Reply briefly.'], project, args.timeout)
                    except HostCheckError:
                        if fixture.failure:
                            raise HostCheckError(fixture.failure) from None
                        raise
                    fixture.verify_complete()
                    if fixture.requests != 1:
                        raise HostCheckError('discovery did not observe a model request')
                summary['checks'].append(label)

            discovery('absent_config', False, True)
            stage = 'project setup'
            installer = [shim, 'setup', '--integration', 'mcp', '--agent', 'codex', '--scope', 'project',
                         '--project', project, '--client', exe, '--allow-root', project, '--verify']
            if json.loads(run(installer, project))['action'] != 'installed':
                raise HostCheckError('MCP setup failed')
            config_path = project / '.codex/config.toml'
            config_before = config_path.read_bytes()
            discovery('untrusted_project', False, False)
            discovery('trusted_project_seven_tools', True, True)
            if args.live_model:
                stage = 'live model'
                summary['live_model'] = {'status': 'attempted', 'model': args.live_model}
                schema = base / 'answer-schema.json'
                schema.write_text(json.dumps(diagnostic['schema'] if diagnostic else ANSWER_SCHEMA), encoding='utf-8')
                prompt = (f'请检查这个文件的架构、是否为 DLL、映像大小（字节）、导出函数数量，以及是否已验证可以加载：{sample}。'
                          '只做静态只读检查，不加载或执行 DLL，使用给定 JSON 格式回答。')
                if diagnostic:
                    prompt = diagnostic['prompt']
                events = execute(command(exe, project, True, args.live_model, schema=schema) + [prompt], project, args.timeout)
                # Keep bounded numeric usage even if evidence validation fails afterwards.
                usage = next(event.get('usage', {}) for event in events if event.get('type') == 'turn.completed')
                summary['live_model']['usage'] = {key: value for key, value in usage.items()
                    if key in ('input_tokens', 'cached_input_tokens', 'cache_write_input_tokens',
                               'output_tokens', 'reasoning_output_tokens') and type(value) is int and value >= 0}
                summary['live_model'] = dict(verify_live(events, sample, baseline, diagnostic), model=args.live_model)
            stage = 'uninstall'
            if config_path.read_bytes() != config_before:
                raise HostCheckError('client changed the managed project configuration')
            if json.loads(run([shim, 'uninstall', '--integration', 'mcp', '--agent', 'codex',
                              '--scope', 'project', '--project', project], project))['action'] != 'removed':
                raise HostCheckError('MCP uninstall failed')
            discovery('uninstalled_config', False, True)
            run([*npm_args, 'uninstall', '-g', '@som1ng/rondo'], base)
            if shim.exists():
                raise HostCheckError('npm uninstall left its launcher behind')
            summary['checks'].append('npm_uninstall')
        summary['status'] = 'passed'
    except (HostCheckError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        # Provider errors, transcripts, credentials and user paths never enter the report.
        summary.update(stage=stage, message=str(error) if isinstance(error, HostCheckError)
                       else 'Acceptance failed at the recorded stage; no success is inferred for later checks.')
    return emit_summary(summary, args.output)


if __name__ == '__main__':
    raise SystemExit(main())
