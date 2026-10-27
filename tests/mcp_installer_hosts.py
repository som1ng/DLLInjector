"""Opt-in generated MCP configuration checks with installed clients and a local model fixture."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import tomllib

from mcp_hosts import codex_check, model_host_check, make_cases, native_executable, NAME, NO_WINDOW

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for agent in ('codex', 'claude', 'opencode'):
        parser.add_argument('--' + agent)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--server', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--auto', action='store_true', help='exercise installer candidate discovery and explicit auto selection')
    selection.add_argument('--all', action='store_true', help='install all three project configurations before checking each host')
    args = parser.parse_args()
    selected = {agent: getattr(args, agent) for agent in ('codex', 'claude', 'opencode') if getattr(args, agent)}
    if not selected:
        parser.error('select at least one installed native client')
    if args.all and len(selected) != 3:
        parser.error('--all requires --codex, --claude and --opencode native paths')
    stage = 'inputs'
    try:
        node = shutil.which('node')
        if not node:
            raise RuntimeError('Node is required')
        cli = native_executable(args.cli)
        server = str(args.server.resolve(strict=True))
        checks = []
        with tempfile.TemporaryDirectory(prefix='dll-mcp-installer-host-') as temporary:
            base = Path(temporary).resolve()
            for agent, client in selected.items():
                stage = agent
                client = native_executable(client)
                project = base / (agent + ' 中文 project 🚀')
                project.mkdir()
                cases = make_cases(cli, args.dll, project, 60)

                def installer(command, target_agent=agent):
                    argv = [node, str(ROOT / 'integrations/installer/src/index.mjs'), command,
                        '--integration', 'mcp', '--agent', target_agent, '--scope', 'project', '--project', str(project)]
                    if command == 'setup':
                        if args.all:
                            argv[argv.index('--agent') + 1] = 'all'
                            for name, native in selected.items():
                                argv += ['--' + name + '-client', native_executable(native)]
                        elif args.auto:
                            argv[argv.index('--agent') + 1] = 'auto'
                            argv += ['--select', agent]
                        if not args.all:
                            argv += ['--client', client]
                        argv += ['--cli', cli, '--server', server, '--allow-root', str(project), '--verify']
                    result = subprocess.run(argv, capture_output=True, timeout=60, creationflags=NO_WINDOW)
                    if result.returncode:
                        code = json.loads(result.stderr).get('code', 'UNKNOWN')
                        raise RuntimeError('MCP installer failed: ' + code)
                    data = json.loads(result.stdout)
                    if command == 'setup' and args.auto and data['agent_detection']['agent'] != agent:
                        raise RuntimeError('auto selection chose the wrong client')
                    return data

                setup_result = installer('setup')
                installed = next(row for row in setup_result['targets'] if row['agent'] == agent) if args.all else setup_result
                config_file = Path(installed['config'])
                if agent == 'codex':
                    config = tomllib.loads(config_file.read_text(encoding='utf-8'))['mcp_servers'][NAME]
                    # Explicit runtime override avoids changing user/project trust or starting unrelated servers.
                    details = codex_check(client, config['command'], config['args'], project, cases, 60)
                    details['configuration'] = 'generated TOML parsed and supplied as isolated Codex overrides'
                else:
                    config = json.loads(config_file.read_text(encoding='utf-8'))
                    command = config['mcpServers'][NAME]['command'] if agent == 'claude' else config['mcp'][NAME]['command'][0]
                    server_args = config['mcpServers'][NAME]['args'] if agent == 'claude' else config['mcp'][NAME]['command'][1:]
                    details = model_host_check(agent, client, command, server_args, project, cases, 60, config_file)
                    details['configuration'] = 'generated file selected explicitly by the host'
                if installer('setup')['action'] != 'unchanged' or installer('verify')['action'] != 'verified':
                    raise RuntimeError('repeat/verify failed')
                def unrelated_config():
                    if not config_file.exists():
                        return {}
                    content = config_file.read_text(encoding='utf-8')
                    data = tomllib.loads(content) if agent == 'codex' else json.loads(content)
                    key = {'codex': 'mcp_servers', 'claude': 'mcpServers', 'opencode': 'mcp'}[agent]
                    if key in data:
                        data[key].pop(NAME, None)
                        if not data[key]:
                            del data[key]
                    return data

                unrelated = unrelated_config()
                if installer('uninstall')['action'] != 'removed' or Path(installed['receipt']).exists():
                    raise RuntimeError('uninstall did not remove owned configuration')
                if config_file.exists() and NAME in config_file.read_text(encoding='utf-8'):
                    raise RuntimeError('managed entry remains after uninstall')
                if unrelated_config() != unrelated:
                    raise RuntimeError('uninstall changed unrelated host configuration')
                if args.all:
                    for target in setup_result['targets']:
                        if target['agent'] != agent and installer('uninstall', target['agent'])['action'] != 'removed':
                            raise RuntimeError('another batch target was not removed')
                        if Path(target['receipt']).exists():
                            raise RuntimeError('batch ownership receipt remains after removal')
                details['preserved_host_fields'] = sorted(unrelated)
                checks.append({'client': agent, 'tools_discovered': 7, 'tool_checks': [case['label'] for case in cases], **details})
        print(json.dumps({'status': 'passed', 'installer_auto_selection': args.auto, 'installer_batch_selection': args.all,
            'external_model_calls': 0, 'project_auto_discovery': 'not_tested', 'checks': checks}, indent=2))
        return 0
    except (RuntimeError, OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        message = str(error) if isinstance(error, RuntimeError) else 'Installer/host check failed; verify installed versions, paths and generated configuration.'
        print(json.dumps({'status': 'failed', 'stage': stage, 'message': message}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
