"""Build and inspect the real Pi package archive without installing an Agent."""
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import uuid

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'integrations/pi'
SOURCE = ROOT / 'integrations/skills/windows-dll-doctor'
RESOURCE = Path('dist/skills/windows-dll-doctor')
FILES = ('SKILL.md', 'references/commands.md', 'references/evidence.md', 'agents/openai.yaml')


def run(command, cwd=PACKAGE, code=0):
    result = subprocess.run(command, cwd=cwd, capture_output=True, timeout=30,
        creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode != code:
        raise AssertionError(f'package command returned {result.returncode}, expected {code}')
    return result.stdout.decode('utf-8')


def node():
    result = shutil.which('node')
    if not result:
        raise AssertionError('Node is required')
    return result


def build(package=PACKAGE):
    run([node(), str(package / 'scripts/build.mjs')], cwd=package)


def pack(destination):
    npm_cli = os.environ.get('npm_execpath')
    if not npm_cli or not Path(npm_cli).is_file():
        raise AssertionError('Run through npm test or npm run verify-host to locate the installed npm CLI')
    # Run prepack, but keep lifecycle output separate from npm's JSON metadata.
    result = json.loads(run([node(), npm_cli, 'pack', '--json', '--foreground-scripts=false',
                            '--pack-destination', str(destination)]))
    # npm 10/11 returns an array; npm 12 keys the same metadata by package name.
    entries = result if isinstance(result, list) else list(result.values())
    if len(entries) != 1:
        raise AssertionError('expected one local npm archive')
    archive = destination / entries[0]['filename']
    if archive.parent.resolve() != destination.resolve() or not archive.is_file():
        raise AssertionError('invalid archive location')
    return archive


def extract_verified(archive, destination):
    expected = {'package/package.json', 'package/README.md'} | {
        'package/' + (RESOURCE / file).as_posix() for file in FILES}
    with tarfile.open(archive, 'r:gz') as bundle:
        members = bundle.getmembers()
        if len(members) != len(expected) or {member.name for member in members} != expected:
            raise AssertionError('archive includes missing or unexpected resources')
        for member in members:
            if not member.isfile() or member.size > 256 * 1024:
                raise AssertionError('archive member must be a bounded regular file')
            parts = PurePosixPath(member.name).parts
            if member.name.startswith('/') or '..' in parts:
                raise AssertionError('unsafe archive member')
            target = destination.joinpath(*parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            with target.open('xb') as output:
                output.write(bundle.extractfile(member).read())
    standalone = destination / 'package'
    for file in FILES:
        if (standalone / RESOURCE / file).read_bytes() != (SOURCE / file).read_bytes():
            raise AssertionError('packaged Skill differs from the canonical source')
    return standalone


class PiPackageContract(unittest.TestCase):
    def test_manifest_and_generated_content(self):
        manifest = json.loads((PACKAGE / 'package.json').read_text(encoding='utf-8'))
        self.assertEqual(manifest['name'], '@som1ng/dll-doctor-pi')
        self.assertIs(manifest['private'], True)
        self.assertEqual(manifest['license'], 'UNLICENSED')
        self.assertIn('pi-package', manifest['keywords'])
        self.assertFalse(manifest.get('dependencies'))
        self.assertEqual(manifest['pi'], {'skills': ['./' + RESOURCE.as_posix()],
            'extensions': [], 'prompts': [], 'themes': []})
        self.assertEqual(set(manifest['files']), {(RESOURCE / file).as_posix() for file in FILES})
        build()
        for file in FILES:
            self.assertEqual((PACKAGE / RESOURCE / file).read_bytes(), (SOURCE / file).read_bytes())

    def test_repeat_build_and_archive_remain_portable(self):
        build()
        sentinel = PACKAGE / RESOURCE / f'unlisted-{uuid.uuid4().hex}.txt'
        sentinel.write_text('keep this unlisted local file', encoding='utf-8')
        try:
            # Rebuild repairs generated content while leaving unlisted files alone.
            (PACKAGE / RESOURCE / 'SKILL.md').write_text('stale generated copy', encoding='utf-8')
            build()
            self.assertEqual(sentinel.read_text(encoding='utf-8'), 'keep this unlisted local file')
            with tempfile.TemporaryDirectory(prefix='pi-package-') as temporary:
                folder = Path(temporary).resolve() / '中文 archive space 🚀'
                folder.mkdir()
                (PACKAGE / RESOURCE / 'SKILL.md').write_text('prepack must rebuild this', encoding='utf-8')
                standalone = extract_verified(pack(folder), folder / 'extracted')
                manifest = json.loads((standalone / 'package.json').read_text(encoding='utf-8'))
                skill = standalone / manifest['pi']['skills'][0]
                self.assertTrue((skill / 'SKILL.md').is_file())
                for page in skill.rglob('*.md'):
                    for link in re.findall(r'\[[^\]]+\]\(([^)]+)\)', page.read_text(encoding='utf-8')):
                        if '://' in link or link.startswith('#'):
                            continue
                        target = (page.parent / link.split('#')[0]).resolve()
                        self.assertIn(skill.resolve(), target.parents)
                        self.assertTrue(target.is_file())
        finally:
            sentinel.unlink()

    def test_build_rejects_linked_output_before_writing_outside(self):
        with tempfile.TemporaryDirectory(prefix='pi-package-link-') as temporary:
            base = Path(temporary).resolve()
            package = base / 'workspace/integrations/pi'
            (package / 'scripts').mkdir(parents=True)
            shutil.copyfile(PACKAGE / 'package.json', package / 'package.json')
            shutil.copyfile(PACKAGE / 'scripts/build.mjs', package / 'scripts/build.mjs')
            shutil.copytree(SOURCE, package.parent / 'skills/windows-dll-doctor')
            outside = base / 'outside generated output'
            outside.mkdir()
            linked = package / 'dist'
            run([node(), '-e', "require('fs').symlinkSync(process.argv[1],process.argv[2],'junction')",
                 str(outside), str(linked)], cwd=base)
            try:
                run([node(), str(package / 'scripts/build.mjs')], cwd=base, code=1)
                self.assertEqual(list(outside.iterdir()), [])
            finally:
                # Remove only the junction created by this test, not its target.
                if os.name == 'nt':
                    linked.rmdir()
                else:
                    linked.unlink()


if __name__ == '__main__':
    unittest.main()
