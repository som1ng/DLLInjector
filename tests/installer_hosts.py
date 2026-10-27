"""Opt-in project discovery of installer-created Skills; no model calls."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

from agent_discovery import codex_discovery, opencode_discovery, native_executable

ROOT = Path(__file__).resolve().parents[1]
ENTRY = ROOT / 'integrations/installer/src/index.mjs'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', required=True, type=Path)
    parser.add_argument('--codex')
    parser.add_argument('--opencode')
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--auto', action='store_true', help='exercise installer candidate discovery and explicit auto selection')
    selection.add_argument('--all', action='store_true', help='check batch setup, nonduplicate discovery and explicit removal of both directories')
    args = parser.parse_args()
    if not args.codex and not args.opencode:
        parser.error('select at least one installed client')
    try:
        node = shutil.which('node')
        if not node:
            raise RuntimeError('Node is required')
        description = next(line.removeprefix('description: ') for line in
            (ROOT / 'integrations/skills/windows-dll-doctor/SKILL.md').read_text(encoding='utf-8').splitlines()
            if line.startswith('description: '))
        results = []
        with tempfile.TemporaryDirectory(prefix='dll-installer-host-') as temporary:
            base = Path(temporary).resolve()
            for name, client in [('codex', args.codex), ('opencode', args.opencode)]:
                if not client:
                    continue
                project = base / (name + ' 中文 space 🚀')
                project.mkdir()
                subprocess.run(['git', 'init', '-q', str(project)], check=True, capture_output=True)

                def installer(command, agent=name):
                    arguments = [node, str(ENTRY), command, '--agent', agent, '--scope', 'project', '--project', str(project)]
                    if command == 'setup':
                        if args.all:
                            arguments[arguments.index('--agent') + 1] = 'all'
                        if args.auto:
                            arguments[arguments.index('--agent') + 1] = 'auto'
                            arguments += ['--select', name, '--client', native_executable(client)]
                        arguments += ['--cli', str(args.cli.resolve()), '--verify']
                    outcome = subprocess.run(arguments, capture_output=True, timeout=30,
                        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                    if outcome.returncode:
                        raise RuntimeError('installer failed during host check')
                    data = json.loads(outcome.stdout)
                    if command == 'setup' and args.auto and data['agent_detection']['agent'] != name:
                        raise RuntimeError('auto selection chose the wrong client')
                    return data

                installed = installer('setup')
                target = next(row['target'] for row in installed['targets'] if name in row['agents']) if args.all else installed['target']
                fixture = {'cwd': project, 'path': Path(target) / 'SKILL.md',
                    'description': description, 'present': True}
                if args.all:
                    fixture['alternative_paths'] = [Path(row['target']) / 'SKILL.md' for row in installed['targets']]
                executable = native_executable(client)

                def discover():
                    if name == 'codex':
                        return codex_discovery(executable, [fixture], 30)
                    return opencode_discovery(executable, [fixture], base, 30)

                observed = discover()
                if installer('setup')['action'] != 'unchanged':
                    raise RuntimeError('repeated setup was not a no-op')
                if installer('uninstall')['action'] != 'removed':
                    raise RuntimeError('uninstall did not remove the managed Skill')
                if args.all and installer('uninstall', 'claude')['action'] != 'removed':
                    raise RuntimeError('uninstall did not remove the second batch directory')
                fixture['present'] = False
                discover()
                results.append({'client': name, 'version': observed['version'],
                    'checks': ['installed-project-discovery', 'repeat', 'uninstalled-absence']})
        print(json.dumps({'status': 'passed', 'installer_auto_selection': args.auto, 'installer_batch_selection': args.all,
            'results': results, 'model_evaluation': 'not_run'}, indent=2))
        return 0
    except (RuntimeError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError):
        print(json.dumps({'status': 'failed', 'message': 'Installer/client discovery failed; check installed client compatibility and local paths.'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
