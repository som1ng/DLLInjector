"""Execute the portable Skill's published examples; this is not a model eval."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator

cli, dll, demo, root = (Path(p).resolve() for p in sys.argv[1:])
source = root / 'integrations/skills/windows-dll-doctor'
validator = Draft202012Validator(json.loads(
    (root / 'schemas/cli-v1.6.json').read_text(encoding='utf-8')))
shell = shutil.which('powershell.exe')
assert shell, 'Windows PowerShell is required for the published examples'

with tempfile.TemporaryDirectory(prefix='dll-doctor-skill-') as tmp:
    # CI may expose TEMP through an 8.3 alias or junction. Compare resolved paths.
    folder = Path(tmp).resolve()
    skill = folder / source.name
    shutil.copytree(source, skill)
    assert (skill / 'SKILL.md').is_file()
    for page in skill.rglob('*.md'):
        for link in re.findall(r'\[[^\]]+\]\(([^)]+)\)', page.read_text(encoding='utf-8')):
            if '://' in link or link.startswith('#'):
                continue
            target = (page.parent / link.split('#', 1)[0]).resolve()
            assert skill in target.parents and target.is_file(), (page, link)

    commands = (skill / 'references/commands.md').read_text(encoding='utf-8')
    encoding = re.search(r'<!-- setup: encoding -->\s*```powershell\n(.*?)\n```', commands, re.S).group(1)
    examples = dict(re.findall(
        r'<!-- example: ([a-z-]+) -->\s*```powershell\n(.*?)\n```',
        commands, re.S))
    assert set(examples) == {'version', 'help', 'doctor', 'doctor-target',
        'doctor-dependencies', 'pe', 'process-list', 'process-inspect',
        'process-modules', 'doctor-save', 'report-export'}
    sample = folder / '诊断 space & sample.dll'
    shutil.copyfile(dll, sample)
    original_hash = hashlib.sha256(sample.read_bytes()).digest()
    saved = folder / 'saved report.json'
    exported = folder / 'saved report.html'
    script = folder / 'example.ps1'
    preamble = '''param(
    [string]$doctorCli, [string]$dllPath, [string]$targetArchitecture,
    [string]$searchDirectory, [string]$targetProcessId,
    [string]$savedJson, [string]$savedHtml
)
$ErrorActionPreference = 'Continue'
'''
    # The target belongs to this test and matches the tested CLI's bitness.
    host = subprocess.Popen([str(demo)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        def run(name, *, code=0, document=False, path=sample, architecture='x64'):
            # Exercise the exact PowerShell source distributed inside the Skill.
            script.write_text(preamble + encoding + '\n' + examples[name] + '\nexit $diagnosticExit\n', encoding='utf-8-sig')
            result = subprocess.run([shell, '-NoLogo', '-NoProfile', '-NonInteractive',
                '-ExecutionPolicy', 'Bypass', '-File', str(script),
                '-doctorCli', str(cli), '-dllPath', str(path),
                '-targetArchitecture', architecture, '-searchDirectory', str(folder),
                '-targetProcessId', str(host.pid), '-savedJson', str(saved),
                '-savedHtml', str(exported)], cwd=folder, capture_output=True, timeout=20)
            assert result.returncode == code, (name, result.returncode,
                result.stdout.decode('utf-8'), result.stderr.decode('utf-8'))
            text = result.stdout.decode('utf-8')
            if document:
                return text
            report = json.loads(text)
            validator.validate(report)
            return report

        assert run('version')['tool_version'] == '2.0.0-dev.8'
        assert 'report export' in run('help')['data']['text']
        pe = run('pe')
        assert pe['command'] == 'pe.inspect'
        doctor = run('doctor')
        assert doctor['target']['path'] == str(sample), 'PowerShell must preserve UTF-8 paths'
        assert doctor['data']['target_architecture'] is None
        assert doctor['execution']['outcome_verified'] is False
        arch = 'x64' if doctor['data']['image']['machine'] == 0x8664 else 'x86'
        other = 'x86' if arch == 'x64' else 'x64'
        matched = run('doctor-target', architecture=arch)
        assert matched['data']['target_architecture'] == arch
        mismatched = run('doctor-target', architecture=other, code=1)
        assert mismatched['status'] == 'success'
        assert any(f['rule_id'] == 'ARCH_MISMATCH' for f in mismatched['findings'])
        dependencies = run('doctor-dependencies')
        assert dependencies['data']['dependencies']['search_directories'] == [str(folder)]
        listing = run('process-list')
        assert any(p['pid'] == host.pid for p in listing['data']['processes'])
        assert run('process-inspect')['data']['architecture'] == arch
        assert run('process-modules')['target']['pid'] == host.pid

        # The same save/export examples must preserve an unsuccessful diagnosis.
        diagnosis = run('doctor-save', architecture=other, code=1)
        assert json.loads(saved.read_text(encoding='utf-8')) == diagnosis
        saved_bytes = saved.read_bytes()
        html = run('report-export', document=True)
        assert '<html' in html.lower() and exported.is_file()
        assert 'ARCH_MISMATCH' in exported.read_text(encoding='utf-8')
        from html.parser import HTMLParser

        class PreBlocks(HTMLParser):
            def __init__(self):
                super().__init__(convert_charrefs=True)
                self.active = False
                self.blocks = []

            def handle_starttag(self, tag, attrs):
                if tag == 'pre':
                    self.active = True
                    self.blocks.append('')

            def handle_endtag(self, tag):
                if tag == 'pre':
                    self.active = False

            def handle_data(self, data):
                if self.active:
                    self.blocks[-1] += data

        parsed = PreBlocks()
        parsed.feed(html)
        assert any(json.loads(b) == diagnosis for b in parsed.blocks if b.lstrip().startswith('{'))
        run('doctor-save', architecture=arch, code=4)
        assert saved.read_bytes() == saved_bytes, 'existing report must survive'
        malformed = folder / 'malformed.dll'
        malformed.write_bytes(b'not a PE image')
        bad = run('doctor', path=malformed, code=1)
        assert bad['status'] == 'success'
        assert any(f['rule_id'] == 'MALFORMED_PE' for f in bad['findings'])
        missing = run('doctor', path=folder / 'missing.dll', code=3)
        assert missing['status'] == 'error' and missing['errors']
        assert not missing['execution']['operation_completed']
        assert hashlib.sha256(sample.read_bytes()).digest() == original_hash
    finally:
        host.terminate()
        host.wait(timeout=5)

print('11 portable PowerShell examples and failure paths passed; no model evaluation performed')
