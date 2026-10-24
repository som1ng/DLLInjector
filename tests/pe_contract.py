"""Inspect inert data and built binaries without loading or executing the input."""
import copy
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

from jsonschema import Draft202012Validator

cli, dll, root = map(Path, sys.argv[1:])
schema = json.loads((root / 'schemas/cli-v1.6.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)


def run(*args, code=0, human=False):
    result = subprocess.run([str(cli), *map(str, args), *([] if human else ['--json'])],
                            capture_output=True, timeout=15)
    out, err = result.stdout.decode('utf-8'), result.stderr.decode('utf-8')
    assert result.returncode == code, (args, result.returncode, err)
    assert bool(err) == (code != 0 or '--verbose' in args), err
    if human:
        return out
    doc = json.loads(out)
    validator.validate(doc)
    assert doc['status'] == ('success' if code == 0 else 'error')
    assert doc['execution']['outcome_verified'] == (code == 0)
    if code:
        assert doc['data'] is None
    return doc


def fixture(plus):
    b = bytearray(1024)
    b[:2] = b'MZ'
    struct.pack_into('<I', b, 60, 128)
    b[128:132] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', b, 132, 0x8664 if plus else 0x14c, 1, 0, 0, 0,
                     240 if plus else 224, 0x2002)
    o = 152
    struct.pack_into('<H', b, o, 0x20b if plus else 0x10b)
    struct.pack_into('<I', b, o + 16, 4096)
    struct.pack_into('<Q' if plus else '<I', b, o + (24 if plus else 28),
                     0x20000000010000 if plus else 0x400000)
    struct.pack_into('<II', b, o + 32, 4096, 512)
    struct.pack_into('<II', b, o + 56, 8192, 512)
    struct.pack_into('<HH', b, o + 68, 3, 320)
    # No directories in the synthetic fixture: contents are deliberately inert.
    s = o + (240 if plus else 224)
    struct.pack_into('<8sIIIIIIHHI', b, s, b'.text', 384, 4096, 512, 512, 0, 0, 0, 0, 0x60000020)
    return b


fixed = json.loads((root / 'tests/fixtures/json/pe-tables-empty.json').read_text(encoding='utf-8'))
historical = json.loads((root / 'schemas/cli-v1.3.json').read_text(encoding='utf-8'))
Draft202012Validator(historical).validate(fixed)
fixed['schema_version'] = '1.6'
validator.validate(fixed)
for key, value in [('image_base', 0x20000000010000), ('entry_point_rva', '0x100000000'),
                   ('scope', 'loadability_verified'), ('sections', [])]:
    bad = copy.deepcopy(fixed)
    bad['data'][key] = value
    assert not validator.is_valid(bad), key
bad = copy.deepcopy(fixed)
bad['evidence'][0]['source'] = 'runtime_observation'
assert not validator.is_valid(bad)
bad = copy.deepcopy(fixed)
bad['target']['path'] = None
assert not validator.is_valid(bad)

assert 'pe inspect' in run('--help')['data']['text']
for args in [('pe',), ('pe', 'inspect'), ('pe', 'inspect', ''), ('pe', 'inspect', 'a', 'b'),
             ('pe', 'inspect', 'x.dll', '--pid', '1'), ('pe', 'inspect', '--bad.dll')]:
    assert not run(*args, code=2)['execution']['command_accepted']

with tempfile.TemporaryDirectory(prefix='dllinjector-pe-') as tmp:
    folder = Path(tmp) / '文件 space 🚀'
    folder.mkdir()
    missing = folder / 'missing.dll'
    doc = run('pe', 'inspect', missing, code=3)
    assert doc['errors'][0]['category'] == 'not_found' and doc['errors'][0]['win32'] == 2
    doc = run('pe', 'inspect', missing, '--timeout-ms', 0, code=5)
    assert doc['errors'][0]['stage'] == 'pe.timeout'
    assert run('pe', 'inspect', r'\\.\pipe\not-a-pe', code=4)['errors'][0]['category'] == 'unsupported'

    # Compare output against independently read header fields in real compiler products.
    for original in (dll, cli):
        path = folder / ('插件 🚀.dll' if original == dll else '程序 🚀.exe')
        shutil.copyfile(original, path)
        before = hashlib.sha256(path.read_bytes()).digest()
        output = folder / (path.name + '.json')
        doc = run('--verbose', 'pe', 'inspect', path, '--output', output)
        assert json.loads(output.read_text(encoding='utf-8')) == doc
        assert doc['target'] == {'pid': None, 'path': str(path)}
        assert doc['evidence'][0]['source'] == 'static_analysis'
        data = doc['data']
        raw = path.read_bytes()
        nt, = struct.unpack_from('<I', raw, 60)
        machine, sections = struct.unpack_from('<HH', raw, nt + 4)
        assert data['machine'] == machine and len(data['sections']) == sections
        assert data['file_size'] == len(raw)
        assert data['scope'] == 'headers_sections_imports_exports_delay_imports'
        assert any(s['raw_size'] > 0 for s in data['sections'])
        assert any(d['index'] == 1 and d['address'] != '0x0' for d in data['directories'])
        text = run('pe', 'inspect', path, human=True)
        assert 'ImageBase:' in text and 'RAW_OFFSET' in text and 'DIRECTORY_INDEX' in text
        run('pe', 'inspect', path, '--output', path, code=4)
        assert hashlib.sha256(path.read_bytes()).digest() == before, 'input must remain unchanged'
        # Successful and failing calls must release the read handle.
        renamed = path.with_suffix('.moved')
        path.rename(renamed)
        renamed.rename(path)

    for plus in (False, True):
        path = folder / ('pe64.bin' if plus else 'pe32.bin')
        path.write_bytes(fixture(plus))
        doc = run('pe', 'inspect', path)
        assert doc['data']['format'] == ('PE32+' if plus else 'PE32')
        assert int(doc['data']['image_base'], 16) == (0x20000000010000 if plus else 0x400000)
        if plus:
            assert doc['data'] == fixed['data'], 'synthetic PE64 matches the frozen JSON golden data'

    bad_inputs = [b'', b'MZ', bytes(64), fixture(False)[:152], fixture(True)[:511]]
    for offset, value, width in [(60, 0x7ffffff0, 4), (128, 0, 4), (152, 0x999, 2),
                                  (152 + 60, 0, 4), (152 + 224 + 20, 0xfffffe00, 4)]:
        damaged = fixture(False)
        struct.pack_into('<H' if width == 2 else '<I', damaged, offset, value)
        bad_inputs.append(damaged)
    path = folder / '损坏.dll'
    for damaged in bad_inputs:
        path.write_bytes(damaged)
        doc = run('pe', 'inspect', path, code=1)
        error = doc['errors'][0]
        assert error['category'] == 'malformed_pe' and error['win32'] is None
        assert error['ntstatus'] is None and error['stage'].startswith('pe.')
        assert doc['execution']['command_accepted'] and not doc['evidence']

    # A real sharing violation, without modifying permissions or elevating.
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                  wintypes.LPVOID, wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)
    assert handle != ctypes.c_void_p(-1).value
    try:
        doc = run('pe', 'inspect', path, code=4)
        assert doc['errors'][0]['stage'] == 'pe.open' and doc['errors'][0]['win32'] == 32
    finally:
        assert kernel.CloseHandle(handle)

    oversized = folder / 'oversized.bin'
    with oversized.open('wb') as file:
        file.truncate(256 * 1024 * 1024 + 1)
    doc = run('pe', 'inspect', oversized, code=4)
    assert doc['errors'][0]['category'] == 'unsupported' and doc['errors'][0]['stage'] == 'pe.size'

print('PASS PE CLI: real binaries, inert PE32/PE32+, malformed data, IO and schema')
