"""Human reports preserve one query's JSON and cannot turn input into active markup."""
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator
from markdown_it import MarkdownIt

cli, fixtures, dll, root = map(Path, sys.argv[1:5])
validator = Draft202012Validator(json.loads((root / 'schemas/cli-v1.6.json').read_text(encoding='utf-8')))
markdown = MarkdownIt('commonmark')  # Raw HTML enabled: escaping must be done by the exporter.


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.codes, self.headings, self.tags, self.text = [], [], [], []
        self.code = self.heading = None
        self.feed(text)
        self.close()

    def handle_starttag(self, tag, attrs):
        self.tags.append(tag)
        assert tag not in ('script', 'img', 'iframe', 'object', 'embed', 'a', 'base', 'form', 'link'), tag
        assert not any(k.lower().startswith('on') or k in ('href', 'src', 'srcdoc') for k, _ in attrs), attrs
        if tag == 'code':
            self.code = []
        if tag in ('h1', 'h2', 'h3'):
            self.heading = []

    def handle_endtag(self, tag):
        if tag == 'code':
            self.codes.append(''.join(self.code))
            self.code = None
        if tag in ('h1', 'h2', 'h3'):
            self.headings.append(''.join(self.heading))
            self.heading = None

    def handle_data(self, data):
        self.text.append(data)
        if self.code is not None:
            self.code.append(data)
        if self.heading is not None:
            self.heading.append(data)


def extract(text, fmt):
    assert not text.startswith('\ufeff')
    if fmt == 'html':
        assert text.startswith('<!doctype html>') and text.endswith('</html>\n')
        assert '<meta charset="utf-8">' in text
        assert 'Content-Security-Policy' in text
    else:
        assert text.startswith('# DLLInjector report\n')
    parsed = Document(text if fmt == 'html' else markdown.render(text))
    blocks = [c for c in parsed.codes if c.startswith('{"schema_version":')]
    assert len(blocks) == 1, 'exactly one complete JSON appendix'
    doc = json.loads(blocks[0])
    return doc, parsed


cases = {'normal': 0, 'mismatch': 1, 'missing': 0, 'api-set': 0,
         'candidate-error': 0, 'malformed': 1, 'denied': 3, 'timeout': 5,
         'process': 0, 'modules': 0, 'pe': 0, 'hostile': 0}
for case, exit_code in cases.items():
    def sample(fmt):
        result = subprocess.run([str(fixtures), case, fmt], capture_output=True, timeout=15)
        assert result.returncode == 0, result.stderr
        return result.stdout.decode('utf-8').replace('\r\n', '\n')
    original = json.loads(sample('json'))
    if case != 'hostile':  # Deliberate arbitrary hostile strings exceed diagnostic field enums.
        validator.validate(original)
    for fmt in ('markdown', 'html'):
        document, parsed = extract(sample(fmt), fmt)
        assert document == original, (case, fmt, 'report data changed during presentation')
        visible = ''.join(parsed.text)
        assert 'Exit code' in visible and str(exit_code) in visible
        assert 'Outcome verified' in visible
        assert 'FORGED HEADING' not in parsed.headings
        for evidence in original['evidence']:
            assert evidence['source'] in visible and evidence['description'] in visible
        if case == 'hostile':
            assert '</pre><script>alert(1)</script>' in visible
            assert '\\x1b' in visible and '\\x00' in visible
        if case == 'missing':
            assert 'POSSIBLE_MISSING_DEPENDENCY' in visible and 'low' in visible
        if case == 'candidate-error':
            assert 'GetFileAttributesW' in visible


def invoke(args, fmt, code=0):
    result = subprocess.run([str(cli), *map(str, args), '--format', fmt], capture_output=True, timeout=15)
    assert result.returncode == code, (args, fmt, result.returncode, result.stderr)
    text = result.stdout.decode('utf-8')
    assert '\r' not in text and not text.startswith('\ufeff')
    doc, _ = extract(text, fmt)
    validator.validate(doc)
    assert bool(result.stderr) == (code not in (0, 1)), result.stderr
    return text, doc


with tempfile.TemporaryDirectory(prefix='dllinjector-reports-') as tmp:
    folder = Path(tmp) / '中文 space 🚀'
    folder.mkdir()
    broken = folder / 'broken & ` [x].dll'
    broken.write_bytes(b'MZ truncated')
    for fmt, extension in (('markdown', 'md'), ('html', 'html')):
        output = folder / f'doctor.{extension}'
        text, doc = invoke(['doctor', dll, '--output', output], fmt)
        assert output.read_bytes() == text.encode('utf-8')
        assert not doc['execution']['outcome_verified']
        before = output.read_bytes()
        _, failed = invoke(['doctor', dll, '--output', output], fmt, 4)
        assert failed['data'] is None and not failed['findings'] and not failed['evidence']
        assert output.read_bytes() == before
        _, failed = invoke(['doctor', dll, '--output', folder / 'absent' / extension], fmt, 4)
        assert failed['errors'][0]['stage'] == 'output.open'
        _, malformed = invoke(['doctor', broken], fmt, 1)
        assert malformed['status'] == 'success' and malformed['findings'][0]['rule_id'] == 'MALFORMED_PE'
        invoke(['doctor', broken, '--output', broken], fmt, 4)
        assert broken.read_bytes() == b'MZ truncated'
        invoke(['doctor', folder / 'absent.dll'], fmt, 3)
        invoke(['doctor', dll, '--timeout-ms', '0'], fmt, 5)
        invoke(['doctor', dll, '--search-dir', folder], fmt)
        invoke(['pe', 'inspect', dll], fmt)
        invoke(['process', 'list'], fmt)
        invoke(['process', 'inspect', '--pid', os.getpid()], fmt)
        # Same-bitness module success is covered by the fixed snapshot above and cli.modules.
        invoke(['process', 'modules', '--pid', '4294967295'], fmt, 3)

    invalid = [['--format'], ['--format', ''], ['--format', 'json'], ['--format', 'HTML'],
               ['--format', 'html', '--format', 'html'], ['--format', 'html', '--json'],
               ['--json', '--format', 'markdown']]
    for args in invalid:
        result = subprocess.run([str(cli), 'doctor', str(dll), *args],
                                capture_output=True, timeout=15)
        assert result.returncode == 2
        if '--json' in args:
            validator.validate(json.loads(result.stdout))
    result = subprocess.run([str(cli), '--format', 'markdown', 'doctor', str(dll)],
                            capture_output=True, timeout=15)
    assert result.returncode == 0
    extract(result.stdout.decode('utf-8'), 'markdown')
    for option in ('--help', '--version'):
        target = folder / 'invalid.html'
        result = subprocess.run([str(cli), option, '--format', 'html', '--output', str(target)],
                                capture_output=True, timeout=15)
        assert result.returncode == 2 and not target.exists()

print('PASS 12 report fixtures, CommonMark/HTML escaping, JSON identity and CLI export contract')
