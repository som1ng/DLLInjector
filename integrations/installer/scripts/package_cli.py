"""Package an explicitly trusted, built Windows CLI into a bounded CI artifact."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[3]
MAX_CLI_SIZE = 64 * 1024 * 1024
VERSIONS = ('2.0.0-dev.8', '2.0.0-dev.9')


def architecture(data):
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('CLI is not a bounded PE image')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if offset < 64 or offset > len(data) - 26 or data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('CLI PE header is invalid')
    machine = struct.unpack_from('<H', data, offset + 4)[0]
    magic = struct.unpack_from('<H', data, offset + 24)[0]
    result = {(0x14c, 0x10b): 'x86', (0x8664, 0x20b): 'x64'}.get((machine, magic))
    if not result:
        raise ValueError('CLI must have matching x86/x64 machine and optional-header magic')
    return result


def metadata(cli, expected_architecture, revision=None):
    if revision is not None and not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('source revision must be 40 lowercase hex characters')
    cli = cli.resolve(strict=True)
    if not cli.is_file() or not 64 <= cli.stat().st_size <= MAX_CLI_SIZE:
        raise ValueError('CLI must be a regular file of at most 64 MiB')
    data = cli.read_bytes()
    arch = architecture(data)
    if arch != expected_architecture:
        raise ValueError('CLI architecture does not match the requested bundle')
    result = subprocess.run([str(cli), '--version', '--json'], capture_output=True,
        timeout=10, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode or len(result.stdout) > 1024 * 1024:
        raise ValueError('trusted CLI version query failed or exceeded its output limit')
    version = json.loads(result.stdout)
    if (not isinstance(version, dict) or version.get('command') != 'version'
            or version.get('status') != 'success' or version.get('schema_version') != '1.6'
            or version.get('tool_version') not in VERSIONS
            or version.get('data', {}).get('text') != version.get('tool_version')
            or version.get('execution', {}).get('operation_completed') is not True):
        raise ValueError('expected tested Rondo CLI dev.8/dev.9 with Schema 1.6')
    if cli.read_bytes() != data:
        raise ValueError('CLI changed during its version query')
    manifest = {'format': 1, 'product': 'rondo-cli', 'platform': 'windows',
        'architecture': arch, 'tool_version': version['tool_version'], 'schema_version': '1.6',
        'source_revision': revision, 'runtime': 'msvc-v14',
        'files': {'rondo.exe': {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}}}
    return data, manifest


def bundle_readme(manifest):
    return ('Rondo CLI ' + manifest['tool_version'] + ' / Windows ' + manifest['architecture'] + '\n\n'
        'CI download artifact; not a signed release. Project licensing remains unresolved.\n'
        'Get the archive and its .zip.sha256 file from a trusted build/run. Before extracting,\n'
        'compare Get-FileHash -Algorithm SHA256 <archive.zip> with the checksum file.\n'
        'After extraction, the source Skill installer can check the executable before launch:\n'
        'node integrations/installer/src/index.mjs setup --agent codex --scope project '
        '--project <project> --cli <absolute-rondo.exe> --manifest <absolute-manifest.json> --verify\n'
        'Keep the extracted CLI in a stable local directory. The installer is not in this archive.\n'
        'Checksums detect differences from the supplied manifest; they are not signatures.\n\n'
        'Requires Windows and the matching Microsoft Visual C++ v14 Redistributable,\n'
        'at least as recent as the MSVC tools used by the build. Runtime is not bundled.\n'
        'https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist\n\n'
        'Read-only examples: rondo.exe --version --json; rondo.exe doctor <dll> --json\n'
        'See --help --json for the actual executable capabilities. No DLL is loaded by doctor.\n'
        'Third-party notices are included separately.\n').encode('utf-8')


def create_bundle(cli, output, expected_architecture, revision=None):
    data, manifest = metadata(cli, expected_architecture, revision)
    files = {'rondo.exe': data,
        'manifest.json': (json.dumps(manifest, indent=2) + '\n').encode('utf-8'),
        'README.txt': bundle_readme(manifest),
        'THIRD_PARTY_NOTICES.md': (ROOT / 'docs/THIRD_PARTY_NOTICES.md').read_bytes()}
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, content in files.items():
            entry = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, content)
    packed = stream.getvalue()
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    basename = 'rondo-cli-' + manifest['tool_version'] + '-windows-' + manifest['architecture'] + '.zip'
    archive_path = output / basename
    checksum_path = output / (basename + '.sha256')
    if archive_path.exists() or checksum_path.exists():
        raise ValueError('bundle output already exists; select a new output directory')
    checksum = hashlib.sha256(packed).hexdigest()
    created = []
    try:
        for target, content in [(archive_path, packed), (checksum_path, (checksum + '  ' + basename + '\n').encode('ascii'))]:
            with target.open('xb') as handle:
                created.append(target)
                handle.write(content)
    except Exception:
        for target in reversed(created):
            # Only unlink exact files exclusively created in the resolved output directory.
            if target.parent != output:
                raise RuntimeError('unexpected output cleanup path')
            target.unlink()
        raise
    return {'archive': str(archive_path), 'checksum_file': str(checksum_path), 'sha256': checksum,
        'architecture': manifest['architecture'], 'tool_version': manifest['tool_version'],
        'source_revision': revision}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True, help='trusted built CLI; packaging runs its version query')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--architecture', choices=('x86', 'x64'), required=True)
    parser.add_argument('--revision', help='explicit source build revision; omitted for unknown provenance')
    args = parser.parse_args()
    try:
        print(json.dumps({'status': 'success', **create_bundle(args.cli, args.output, args.architecture, args.revision)}, indent=2))
        return 0
    except (OSError, ValueError, TypeError, AttributeError, subprocess.SubprocessError):
        print(json.dumps({'status': 'error', 'message': 'Packaging failed; check CLI version/architecture, revision and unused output paths.'}))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
