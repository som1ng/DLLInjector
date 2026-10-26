"""Opt-in Pi native package lifecycle checks; no AgentSession or model calls."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

from agent_discovery import verify_observation
from pi_package_contract import ROOT, PACKAGE, RESOURCE, SOURCE, extract_verified, node, pack


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pi-package', type=Path, required=True, help='installed Pi coding-agent package directory')
    parser.add_argument('--output', type=Path, help='new sanitized summary file')
    args = parser.parse_args()
    try:
        if args.output and args.output.exists():
            raise AssertionError('output already exists; choose a new path')
        package = args.pi_package.resolve()
        manifest = json.loads((package / 'package.json').read_text(encoding='utf-8'))
        if manifest['name'] not in ('@earendil-works/pi-coding-agent', '@mariozechner/pi-coding-agent'):
            raise AssertionError('pi-package must be an installed Pi coding-agent package')
        cli = package / manifest['bin']['pi']
        if not cli.is_file() or package not in cli.resolve().parents:
            raise AssertionError('invalid installed Pi CLI entry')
        description = next(line.removeprefix('description: ') for line in
            (SOURCE / 'SKILL.md').read_text(encoding='utf-8').splitlines() if line.startswith('description: '))
        checks = []
        with tempfile.TemporaryDirectory(prefix='dll-doctor-pi-') as temporary:
            base = Path(temporary).resolve()
            standalone = extract_verified(pack(base), base / '独立 package space 🚀')
            for source_label, install_source in [('source', PACKAGE), ('archive', standalone)]:
                for scope in ('user', 'project'):
                    case = base / f'{source_label}-{scope} 中文 space 🚀'
                    project, agent = case / 'project', case / 'agent'
                    project.mkdir(parents=True)
                    other = case / 'other-package'
                    write_json(other / 'package.json', {'name': 'unrelated-local-package',
                        'pi': {'skills': [], 'extensions': [], 'prompts': [], 'themes': []}})
                    global_file, project_file = agent / 'settings.json', project / '.pi/settings.json'
                    initial_global = {'theme': 'dark', 'packages': [str(other)]}
                    initial_project = {'theme': 'light', 'packages': []}
                    write_json(global_file, initial_global)
                    write_json(project_file, initial_project)
                    env = {key: value for key, value in os.environ.items() if key.upper() in {
                        'PATH', 'SYSTEMROOT', 'WINDIR', 'COMSPEC', 'PATHEXT', 'TEMP', 'TMP',
                        'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'PROGRAMDATA', 'PROGRAMFILES',
                        'PROGRAMFILES(X86)', 'SYSTEMDRIVE', 'HOMEDRIVE', 'HOMEPATH'}}
                    env['PI_CODING_AGENT_DIR'] = str(agent)

                    def command(argv):
                        result = subprocess.run([node(), *map(str, argv)], cwd=project, env=env,
                            capture_output=True, timeout=30,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                        if result.returncode or len(result.stdout) > 1024 * 1024:
                            raise AssertionError('Pi command failed or exceeded output limit; inspect installed version compatibility')
                        return result.stdout.decode('utf-8')

                    def observe(present, trusted=True):
                        result = json.loads(command([ROOT / 'tests/pi_skill_discovery.mjs', package,
                            project, agent, str(trusted).lower(), 'packages']))
                        verify_observation(result['skills'], install_source / RESOURCE / 'SKILL.md',
                            description, present, result['diagnostics'])
                        if present:
                            if len(result['skills']) != 1 or not result['skills'][0]['advertised']:
                                raise AssertionError('Pi did not advertise the installed Skill exactly once')
                            if result['skills'][0]['scope'] != scope:
                                raise AssertionError('Pi loaded the package under an unexpected scope')

                    options = ['--local', '--approve'] if scope == 'project' else []
                    target = project_file if scope == 'project' else global_file
                    other_file = global_file if scope == 'project' else project_file
                    other_before = other_file.read_bytes()
                    observe(False)
                    for _ in range(2):
                        command([cli, 'install', install_source, *options])
                    settings = json.loads(target.read_text(encoding='utf-8'))
                    sources = [entry if isinstance(entry, str) else entry['source'] for entry in settings['packages']]
                    resolved = [(target.parent / value).resolve() for value in sources]
                    if resolved.count(install_source.resolve()) != 1:
                        raise AssertionError('repeat installation did not preserve a single package reference')
                    if other_file.read_bytes() != other_before:
                        raise AssertionError('installation changed settings in the other scope')
                    observe(True)
                    observe(scope == 'user', trusted=False)

                    # Native resource filtering must be honored, without deleting the package.
                    filtered = dict(settings)
                    filtered['packages'] = [{'source': entry, 'skills': []}
                        if isinstance(entry, str) and (target.parent / entry).resolve() == install_source.resolve()
                        else entry for entry in settings['packages']]
                    write_json(target, filtered)
                    observe(False)
                    write_json(target, settings)
                    command([cli, 'remove', install_source, *options])
                    observe(False)
                    if (json.loads(global_file.read_text(encoding='utf-8')) != initial_global
                            or json.loads(project_file.read_text(encoding='utf-8')) != initial_project):
                        raise AssertionError('uninstall did not preserve unrelated packages/settings')
                    if not (install_source / RESOURCE / 'SKILL.md').is_file():
                        raise AssertionError('uninstall removed local package resources')
                    checks.append({'source': source_label, 'scope': scope,
                        'checks': ['install', 'repeat', 'discovery', 'trust', 'filter', 'remove', 'preserve-settings']})
        summary = {'status': 'passed', 'pi_version': manifest['version'],
            'model_evaluation': 'not_run', 'checks': checks}
        text = json.dumps(summary, ensure_ascii=False, indent=2) + '\n'
        if args.output:
            with args.output.open('x', encoding='utf-8') as output:
                output.write(text)
        print(text, end='')
        return 0
    except (AssertionError, RuntimeError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        message = str(error) if isinstance(error, (AssertionError, RuntimeError)) else 'Pi package validation failed; verify the installed package and local paths'
        print(json.dumps({'status': 'failed', 'message': message}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
