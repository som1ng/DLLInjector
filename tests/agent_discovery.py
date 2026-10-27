"""Opt-in discovery checks against installed clients. Never starts a model turn."""
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

NAME = 'windows-dll-doctor'
ROOT = Path(__file__).resolve().parents[1]


class DiscoveryError(RuntimeError):
    pass


def verify_observation(rows, expected_path, description, present, diagnostics=(), alternative_paths=()):
    """A global skill with the same name must never substitute for our fixture."""
    if not isinstance(rows, list):
        raise DiscoveryError('client returned an invalid skills collection')
    expected_path = expected_path.resolve()
    expected_paths = {expected_path, *(Path(value).resolve() for value in alternative_paths)}
    matches = []
    for row in rows:
        if not isinstance(row, dict) or not isinstance(row.get('path'), str) or not row['path']:
            raise DiscoveryError('client returned an invalid skill path')
        path = Path(row['path'])
        if not path.is_absolute():
            # Some clients also list virtual/plugin resource URIs. They cannot
            # prove discovery of the filesystem fixture and must not be resolved against cwd.
            continue
        if path.resolve() in expected_paths:
            matches.append(row)
    if len(matches) != int(present):
        raise DiscoveryError('project skill presence/count does not match the fixture')
    if present:
        row = matches[0]
        if (row.get('name') != NAME or row.get('description') != description
                or row.get('enabled') is not True):
            raise DiscoveryError('project skill metadata or enabled state differs')
    if not isinstance(diagnostics, (list, tuple)):
        raise DiscoveryError('client returned invalid discovery diagnostics')
    for error in diagnostics:
        if not isinstance(error, dict) or not isinstance(error.get('path'), str):
            raise DiscoveryError('client returned invalid discovery diagnostics')
        path = Path(error['path'])
        if not path.is_absolute():
            continue
        path = path.resolve()
        if any(path == expected or expected.parent in path.parents for expected in expected_paths):
            raise DiscoveryError('client reported a diagnostic for the project skill')
    return Path(matches[0]['path']).resolve() if present else None


def run(args, cwd, timeout, env=None):
    result = subprocess.run(args, cwd=cwd, env=env, capture_output=True, timeout=timeout,
                            creationflags=subprocess.CREATE_NO_WINDOW)
    if result.returncode:
        # Client logs can contain unrelated user configuration; do not echo them.
        raise DiscoveryError(f'client process exited with code {result.returncode}')
    return result.stdout.decode('utf-8')


def native_executable(value):
    path = Path(value).resolve()
    if not path.is_file() or path.suffix.lower() != '.exe':
        raise DiscoveryError('provide the installed native .exe path, not a .ps1/.cmd shim')
    return str(path)


def codex_discovery(exe, fixtures, timeout):
    version = run([exe, '--version'], fixtures[0]['cwd'], timeout).strip()
    proc = subprocess.Popen([exe, 'app-server', '--stdio'], cwd=fixtures[0]['cwd'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
        encoding='utf-8', text=True, creationflags=subprocess.CREATE_NO_WINDOW)
    inbox = queue.Queue()

    def reader():
        try:
            for line in proc.stdout:
                inbox.put(json.loads(line))
        except (ValueError, UnicodeError):
            pass
        finally:
            inbox.put(None)

    worker = threading.Thread(target=reader, daemon=True)
    worker.start()

    def send(value):
        proc.stdin.write(json.dumps(value, ensure_ascii=False) + '\n')
        proc.stdin.flush()

    def response(request_id):
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                raise DiscoveryError('Codex discovery response timed out')
            try:
                value = inbox.get(timeout=left)
            except queue.Empty:
                raise DiscoveryError('Codex discovery response timed out') from None
            if not isinstance(value, dict):
                raise DiscoveryError('Codex closed stdout or returned invalid protocol data')
            if value.get('id') == request_id:
                if 'error' in value or 'result' not in value:
                    raise DiscoveryError('Codex rejected the discovery protocol request')
                return value['result']

    try:
        send({'id': 1, 'method': 'initialize', 'params': {
            'clientInfo': {'name': 'dll_doctor_discovery', 'version': '0.1.0'}}})
        response(1)
        send({'method': 'initialized', 'params': {}})
        for request_id, fixture in enumerate(fixtures, 2):
            send({'id': request_id, 'method': 'skills/list', 'params': {
                'cwds': [str(fixture['cwd'])], 'forceReload': True}})
            entries = response(request_id)['data']
            if len(entries) != 1 or Path(entries[0]['cwd']).resolve() != fixture['cwd']:
                raise DiscoveryError('Codex returned a different workspace')
            entry = entries[0]
            observed_path = verify_observation(entry['skills'], fixture['path'], fixture['description'],
                               fixture['present'], entry['errors'], fixture.get('alternative_paths', ()))
            if fixture['present']:
                match = next(s for s in entry['skills'] if Path(s['path']).resolve() == observed_path)
                if match.get('scope') != 'repo':
                    raise DiscoveryError('Codex did not discover the skill in repository scope')
        return {'client': 'codex', 'version': version, 'checks': ['project', 'absent'],
                'method': 'app-server skills/list'}
    finally:
        try:
            proc.stdin.close()
        except BrokenPipeError:
            pass
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
        worker.join(timeout=2)
        proc.stdout.close()


def opencode_discovery(exe, fixtures, base, timeout):
    env = dict(os.environ)
    env.update(OPENCODE_CONFIG_DIR=str(base / 'opencode-config'),
        OPENCODE_DB=str(base / 'opencode.sqlite'), OPENCODE_CONFIG_CONTENT='{}',
        OPENCODE_DISABLE_AUTOUPDATE='true', OPENCODE_DISABLE_MODELS_FETCH='true',
        OPENCODE_DISABLE_PRUNE='true', OPENCODE_DISABLE_DEFAULT_PLUGINS='true')
    version = run([exe, '--version'], base, timeout, env).strip()
    for fixture in fixtures:
        raw = json.loads(run([exe, 'debug', 'skill', '--pure'], fixture['cwd'], timeout, env))
        if not isinstance(raw, list):
            raise DiscoveryError('OpenCode returned an invalid skills collection')
        # Listing confirms discovery; OpenCode does not expose an enabled field here.
        rows = [dict(s, path=s['location'], enabled=True) for s in raw]
        verify_observation(rows, fixture['path'], fixture['description'], fixture['present'],
                           alternative_paths=fixture.get('alternative_paths', ()))
    return {'client': 'opencode', 'version': version, 'checks': ['project', 'absent'],
            'method': 'debug skill --pure (discovery only; permissions not tested)'}


def pi_discovery(package, node, fixtures, base, timeout):
    package = Path(package).resolve()
    manifest = json.loads((package / 'package.json').read_text(encoding='utf-8'))
    if manifest['name'] not in ('@earendil-works/pi-coding-agent', '@mariozechner/pi-coding-agent'):
        raise DiscoveryError('pi-package must be an installed Pi coding-agent package')
    helper = Path(__file__).with_name('pi_skill_discovery.mjs')
    for fixture in fixtures:
        for trusted in ([True, False] if fixture['present'] else [True]):
            data = json.loads(run([node, str(helper), str(package), str(fixture['cwd']),
                str(base / 'pi-config'), str(trusted).lower()], fixture['cwd'], timeout))
            verify_observation(data['skills'], fixture['path'], fixture['description'],
                               fixture['present'] and trusted, data['diagnostics'])
            if fixture['present'] and trusted:
                match = next(s for s in data['skills'] if Path(s['path']).resolve() == fixture['path'])
                if match.get('scope') != 'project' or not match.get('advertised'):
                    raise DiscoveryError('Pi did not advertise the trusted project skill')
    return {'client': 'pi', 'version': manifest['version'],
            'checks': ['trusted-project', 'untrusted-project', 'absent'],
            'method': 'installed DefaultResourceLoader and formatSkillsForPrompt; no AgentSession'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--codex', help='absolute path to the installed Codex native .exe')
    parser.add_argument('--opencode', help='absolute path to the installed OpenCode native .exe')
    parser.add_argument('--pi-package', help='installed Pi coding-agent package directory')
    parser.add_argument('--node', default='node', help='Node executable used for Pi')
    parser.add_argument('--timeout', type=int, default=30, help='per client operation deadline, seconds (1-120)')
    parser.add_argument('--output', type=Path, help='optional new JSON summary file; never overwrites')
    args = parser.parse_args()
    if not any((args.codex, args.opencode, args.pi_package)):
        parser.error('select at least one installed client')
    if not 1 <= args.timeout <= 120:
        parser.error('--timeout must be 1-120 seconds')
    if os.name != 'nt':
        parser.error('this Windows integration check requires Windows')
    if args.output and args.output.exists():
        parser.error('--output already exists')

    stage = 'setup'
    try:
        codex = native_executable(args.codex) if args.codex else None
        opencode = native_executable(args.opencode) if args.opencode else None
        node = shutil.which(args.node) if args.pi_package else None
        if args.pi_package and not node:
            raise DiscoveryError('Node executable was not found')
        source = ROOT / 'integrations/skills' / NAME
        # This canonical fixture uses a plain, single-line description. Do not parse arbitrary YAML.
        text = (source / 'SKILL.md').read_text(encoding='utf-8')
        frontmatter = text.split('---', 2)[1]
        description = next(line[len('description: '):] for line in frontmatter.splitlines()
                           if line.startswith('description: '))
        with tempfile.TemporaryDirectory(prefix='dll-doctor-discovery-') as tmp:
            base = Path(tmp).resolve()
            fixtures = []
            for present, label in ((True, '已安装 project'), (False, '未安装 project')):
                project = base / label
                project.mkdir()
                run(['git', 'init', '-q', str(project)], base, args.timeout)
                path = project / '.agents/skills' / NAME / 'SKILL.md'
                if present:
                    shutil.copytree(source, path.parent)
                cwd = project / 'packages' / 'nested'
                cwd.mkdir(parents=True)
                fixtures.append(dict(path=path, cwd=cwd, present=present, description=description))
            results = []
            if codex:
                stage = 'codex discovery'
                results.append(codex_discovery(codex, fixtures, args.timeout))
            if opencode:
                stage = 'opencode discovery'
                results.append(opencode_discovery(opencode, fixtures, base, args.timeout))
            if args.pi_package:
                stage = 'pi discovery'
                results.append(pi_discovery(args.pi_package, node, fixtures, base, args.timeout))
        summary = {'status': 'passed', 'skill': NAME, 'results': results, 'model_evaluation': 'not_run',
                   'claude_discovery': 'not_tested'}
        output = json.dumps(summary, ensure_ascii=False, indent=2) + '\n'
        stage = 'write summary'
        if args.output:
            with args.output.open('x', encoding='utf-8') as file:
                file.write(output)
        print(output, end='')
        return 0
    except (DiscoveryError, OSError, ValueError, KeyError, TypeError, StopIteration,
            subprocess.TimeoutExpired) as error:
        # Never include raw client logs or unrelated installed skill contents.
        detail = str(error) if isinstance(error, DiscoveryError) else type(error).__name__
        print(json.dumps({'status': 'failed', 'stage': stage, 'reason': detail, 'model_evaluation': 'not_run'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
