"""Assemble a complete Rondo npm staging directory from trusted local builds."""
import argparse
import json
from pathlib import Path
import sys
import subprocess

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'integrations/installer/scripts'))
from package_cli import metadata


def assemble(x64, x86, output, revision=None):
    package = json.loads((ROOT / 'integrations/npm/package.json').read_text(encoding='utf-8'))
    expected = {}
    for name in ('installer', 'mcp'):
        dependencies = json.loads((ROOT / f'integrations/{name}/package.json').read_text(encoding='utf-8'))['dependencies']
        for key, version in dependencies.items():
            if key in expected and expected[key] != version:
                raise ValueError('Adapter dependencies disagree')
            expected[key] = version
    if package['dependencies'] != expected:
        raise ValueError('npm runtime dependencies differ from adapters')
    package.pop('scripts', None)  # No lifecycle scripts or downloads at installation time.
    files = {'package.json': (json.dumps(package, indent=2) + '\n').encode('utf-8')}
    versions = set()
    for arch, cli in (('x64', x64), ('x86', x86)):
        data, manifest = metadata(cli, arch, revision)
        versions.add(manifest['tool_version'])
        files[f'runtime/{arch}/rondo.exe'] = data
        files[f'runtime/{arch}/manifest.json'] = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
    if len(versions) != 1:
        raise ValueError('Both CLI architectures must use the same version')
    sources = {
        'README.md': ROOT / 'integrations/npm/README.md',
        'package-lock.json': ROOT / 'integrations/npm/package-lock.json',
        'THIRD_PARTY_NOTICES.md': ROOT / 'docs/THIRD_PARTY_NOTICES.md',
    }
    for directory in ('integrations/installer/src', 'integrations/npm/bin', 'integrations/npm/src'):
        for source in (ROOT / directory).glob('*.mjs'):
            sources[source.relative_to(ROOT).as_posix()] = source
    for name in ('index', 'errors', 'policy', 'reports', 'runner', 'server'):
        relative = f'integrations/mcp/dist/{name}.js'
        sources[relative] = ROOT / relative
    for version in ('1', '1.1', '1.2', '1.3', '1.4', '1.5', '1.6'):
        relative = f'integrations/mcp/dist/schemas/cli-v{version}.json'
        sources[relative] = ROOT / relative
    for name in ('SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml'):
        relative = 'integrations/skills/windows-dll-doctor/' + name
        sources[relative] = ROOT / relative
    for relative, source in sources.items():
        if source.is_symlink() or not source.resolve(strict=True).is_relative_to(ROOT):
            raise ValueError('Package sources must be regular repository files')
        files[relative] = source.read_bytes()
    # Refuse an existing output, including a junction, before any package writes.
    if output.exists() or output.is_symlink():
        raise ValueError('Package output already exists')
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    created = []
    try:
        for relative, content in files.items():
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as stream:
                created.append(target)
                stream.write(content)
    except OSError:
        # Only remove files this invocation created, then empty directories.
        for target in reversed(created):
            target.unlink(missing_ok=True)
        for directory in sorted((p for p in output.rglob('*') if p.is_dir()), key=lambda p: len(p.parts), reverse=True):
            directory.rmdir()
        output.rmdir()
        raise
    return {'status': 'success', 'package': package['name'], 'version': package['version'],
            'cli_version': versions.pop(), 'output': str(output), 'files': len(files)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--x64', type=Path, required=True)
    parser.add_argument('--x86', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--revision')
    args = parser.parse_args()
    try:
        print(json.dumps(assemble(args.x64, args.x86, args.output, args.revision)))
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError):
        print(json.dumps({'status': 'error', 'message': 'Rondo package assembly failed; check builds, dependencies and unused output path.'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
