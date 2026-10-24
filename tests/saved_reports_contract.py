"""Export saved reports without observing their targets; validate all historical schemas."""
import copy
import ctypes
from ctypes import wintypes
from html.parser import HTMLParser
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator
from markdown_it import MarkdownIt

cli, dll, root = map(Path, sys.argv[1:4])
markdown = MarkdownIt('commonmark')
current = Draft202012Validator(json.loads((root / 'schemas/cli-v1.6.json').read_text(encoding='utf-8')))


class Document(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True)
        self.codes, self.labels, self.text = [], [], []
        self.code = self.heading = None
        self.feed(text)
        self.close()

    def handle_starttag(self, tag, attrs):
        assert tag not in ('script', 'img', 'iframe', 'object', 'a', 'embed', 'link', 'base', 'form'), tag
        assert not any(k.startswith('on') or k in ('src', 'href', 'srcdoc') for k, _ in attrs)
        if tag == 'code':
            self.code = []
        if tag in ('h1', 'h2', 'h3'):
            self.heading = []

    def handle_endtag(self, tag):
        if tag == 'code':
            self.codes.append(''.join(self.code))
            self.code = None
        if tag in ('h1', 'h2', 'h3'):
            self.labels.append(''.join(self.heading))
            self.heading = None

    def handle_data(self, data):
        self.text.append(data)
        if self.code is not None:
            self.code.append(data)
        if self.heading is not None:
            self.heading.append(data)


def invoke(path, fmt='html', code=0, *extra):
    result = subprocess.run([str(cli), 'report', 'export', str(path), '--format', fmt, *map(str, extra)],
                            capture_output=True, timeout=20)
    assert result.returncode == code, (path, result.returncode, result.stderr)
    text = result.stdout.decode('utf-8')
    assert not text.startswith('\ufeff') and '\r' not in text
    parsed = Document(text if fmt == 'html' else markdown.render(text))
    documents = []
    for block in parsed.codes:
        try:
            obj = json.loads(block)
        except (ValueError, TypeError):
            continue
        if isinstance(obj, dict) and 'schema_version' in obj:
            documents.append(obj)
    assert len(documents) == 1
    doc = documents[0]
    if code:
        current.validate(doc)
        assert doc['command'] == 'report.export' and doc['status'] == 'error'
        assert not doc['execution']['outcome_verified'] and doc['data'] is None
        assert result.stderr
    else:
        assert not result.stderr or '--verbose' in extra
        assert 'supplied snapshot' in ''.join(parsed.text)
    return text, doc, parsed


with tempfile.TemporaryDirectory(prefix='dllinjector-saved-') as tmp:
    folder = Path(tmp) / '中文 space 🚀'
    folder.mkdir()
    input_path = folder / 'report.json'
    for fixture in (root / 'tests/fixtures/json').glob('*.json'):
        expected = json.loads(fixture.read_text(encoding='utf-8'))
        for fmt in ('html', 'markdown'):
            _, actual, _ = invoke(fixture, fmt)
            assert actual == expected, fixture
    # Current report, including a blocking finding and a target that will be removed.
    sample = folder / 'removed.dll'
    sample.write_bytes(b'MZ truncated')
    made = subprocess.run([str(cli), 'doctor', str(sample), '--json', '--output', str(input_path)],
                          capture_output=True, timeout=15)
    assert made.returncode == 1
    expected = json.loads(made.stdout)
    sample.unlink()
    for fmt in ('html', 'markdown'):
        output = folder / f'export.{fmt}'
        text, actual, _ = invoke(input_path, fmt, 0, '--output', output)
        assert actual == expected and output.read_bytes() == text.encode('utf-8')
        before = output.read_bytes()
        invoke(input_path, fmt, 4, '--output', output)
        assert output.read_bytes() == before
        original = input_path.read_bytes()
        invoke(input_path, fmt, 4, '--output', input_path)
        assert input_path.read_bytes() == original
        invoke(input_path, fmt, 4, '--output', folder / 'missing' / 'out.html')
    baseline = json.loads((root / 'tests/fixtures/json/doctor-dependencies.json').read_text(encoding='utf-8'))
    poison = '</pre><script>alert(1)</script>\n```````\n# FORGED\n[x](javascript:x) ![x](https://invalid.test/x)\u001b\u007f'
    hostile = copy.deepcopy(baseline)
    hostile['findings'][0]['message'] = poison
    hostile['target']['path'] = 'C:\\absent\\中文 🚀 & <tag> `file.dll'
    input_path.write_bytes(b'\xef\xbb\xbf' + json.dumps(hostile, ensure_ascii=False, indent=2).replace('\n', '\r\n').encode('utf-8'))
    for fmt in ('html', 'markdown'):
        _, actual, parsed = invoke(input_path, fmt)
        assert actual == hostile and 'FORGED' not in parsed.labels
    # A valid PE source demonstrates 64-bit integers are not routed through double.
    precise = json.loads((root / 'tests/fixtures/json/pe.json').read_text(encoding='utf-8'))
    precise['duration_ms'] = 18446744073709551615
    input_path.write_text(json.dumps(precise), encoding='utf-8')
    assert invoke(input_path)[1] == precise

    def invalid(document, code=2):
        input_path.write_bytes(document if isinstance(document, bytes) else json.dumps(document).encode('utf-8'))
        return invoke(input_path, 'html', code)[1]

    for raw in (b'', b'[]', b'{}', b'null', b'{}{}', b'{"x":1,}', b'//comment\n{}',
                b'{"a":1,"a":2}', b'{"a":1,"\\u0061":2}', b'{"a":"\xff"}',
                b'{"a":"\\ud800"}', b'{"a":"\\u0000"}', b'{"a\\u0000":1}', b'\xff\xfe{\x00}\x00'):
        invalid(raw)
    for edit in ('flags', 'data', 'dependency', 'missing', 'unknown-key'):
        broken = copy.deepcopy(baseline)
        if edit == 'flags': broken['execution']['outcome_verified'] = True
        elif edit == 'data': broken['data']['image']['machine'] = 'x64'
        elif edit == 'dependency': broken['data']['dependencies']['libraries'][0]['candidates'][0]['status'] = 'impossible'
        elif edit == 'missing': del broken['findings'][0]['confidence']
        else: broken['unexpected'] = 'reject extra root keys'
        invalid(broken)
    # A new export error envelope is itself a valid saved 1.6 report.
    failed_export = invalid(b'{')
    input_path.write_text(json.dumps(failed_export), encoding='utf-8')
    assert invoke(input_path)[1] == failed_export
    unsupported = copy.deepcopy(baseline)
    unsupported['schema_version'] = '99.0'
    assert invalid(unsupported, 4)['errors'][0]['stage'] == 'report.version'
    oversized = copy.deepcopy(baseline)
    oversized['tool_version'] = '1' * 129
    invalid(oversized, 4)
    oversized = copy.deepcopy(baseline)
    oversized['findings'][0]['documentation'] = ['https://learn.microsoft.com/' + 'a' * 2048]
    invalid(oversized, 4)
    nul_version = copy.deepcopy(baseline)
    nul_version['tool_version'] = '2.0.0\u0000invalid'
    invalid(nul_version)
    invalid(b'{"x":1.0}')
    invalid(b'{"x":1e2}')
    invalid(b'{"x":18446744073709551616}', 4)
    invalid(b'[' * 70 + b'0' + b']' * 70, 4)
    invalid(b'[' + b'0,' * 250000 + b'0]', 4)
    invalid(b' ' * (16 * 1024 * 1024 + 1), 4)
    input_path.write_text(json.dumps(baseline), encoding='utf-8')
    # The copied executable has no adjacent schema files or repository working directory.
    standalone = folder / 'standalone.exe'
    shutil.copyfile(cli, standalone)
    offline = subprocess.run([str(standalone), 'report', 'export', str(input_path), '--format', 'html'],
                             cwd=folder, capture_output=True, timeout=20)
    assert offline.returncode == 0 and not offline.stderr
    invoke(input_path, 'html', 5, '--timeout-ms', '0')
    invoke(folder / 'missing.json', 'html', 3)
    invoke(r'\\server\share\report.json', 'html', 4)
    invoke(r'\\.\pipe\report', 'html', 4)
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = kernel.CreateFileW(str(input_path), 0x80000000, 0, None, 3, 0x80, None)
    assert handle != ctypes.c_void_p(-1).value
    try:
        assert invoke(input_path, 'html', 4)[1]['errors'][0]['win32'] == 32
    finally:
        kernel.CloseHandle(handle)
    for args in (['report', 'export'], ['report', 'export', str(input_path)],
                 ['report', 'export', str(input_path), '--format', 'html', '--json'],
                 ['report', 'export', str(input_path), '--format', 'html', '--pid', '1'],
                 ['report', 'export', str(input_path), '--format', 'html', '--target-arch', 'x64'],
                 ['report', 'export', str(input_path), '--format', 'html', '--search-dir', str(folder)]):
        result = subprocess.run([str(cli), *args], capture_output=True, timeout=15)
        assert result.returncode == 2, args

print('PASS saved reports: historical schemas, strict input, source preservation and output protection')
