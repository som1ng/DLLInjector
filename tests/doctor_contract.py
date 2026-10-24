"""Doctor CLI rules, evidence and error semantics; files are never loaded."""
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

cli, dll, exe, root = map(Path, sys.argv[1:5])
schema = json.loads((root / 'schemas/cli-v1.5.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
golden = json.loads((root / 'tests/fixtures/json/doctor-mismatch.json').read_text(encoding='utf-8'))
historical = json.loads((root / 'schemas/cli-v1.4.json').read_text(encoding='utf-8'))
Draft202012Validator(historical).validate(golden)
golden['schema_version'] = '1.5'
golden['tool_version'] = '2.0.0-dev.7'
golden['data']['dependencies'] = None
golden['findings'][1]['evidence'][0]['description'] = (
    'Static tables contain 0 import libraries and 0 delay-import libraries. No DLL loading or initialization was performed.')
validator.validate(golden)

def invoke(args, code=0, completed=True, human=False):
    r = subprocess.run([str(cli), *map(str, args), *([] if human else ['--json'])], capture_output=True, timeout=15)
    out, err = r.stdout.decode('utf-8'), r.stderr.decode('utf-8')
    assert r.returncode == code, (args, r.returncode, out, err)
    assert bool(err) == (not completed), (args, err)
    if human:
        return out
    doc = json.loads(out)
    validator.validate(doc)
    assert doc['status'] == ('success' if completed else 'error')
    assert doc['execution']['operation_completed'] == completed
    assert not doc['execution']['outcome_verified'], 'Doctor never claims runtime verification'
    if completed:
        assert doc['execution']['command_accepted'] and not doc['errors']
        assert doc['data']['scope'] == 'static_dll_preflight'
        assert any(f['severity'] == 'error' for f in doc['findings']) == (code == 1)
    else:
        assert doc['data'] is None and not doc['findings'] and not doc['evidence']
    return doc

def run(path, *args, **kwargs):
    return invoke(['doctor', path, *args], **kwargs)

def rules(doc):
    return {f['rule_id'] for f in doc['findings']}

def fixture(plus=False):
    b = bytearray(1024)
    def put(at, value, width=4):
        struct.pack_into({2: '<H', 4: '<I', 8: '<Q'}[width], b, at, value)
    put(0, 0x5a4d, 2); put(60, 128); put(128, 0x4550)
    put(132, 0x8664 if plus else 0x14c, 2); put(134, 1, 2)
    optional_size = 240 if plus else 224
    put(148, optional_size, 2); put(150, 0x2002, 2); put(152, 0x20b if plus else 0x10b, 2)
    put(152 + (24 if plus else 28), 0x140000000 if plus else 0x400000, 8 if plus else 4)
    put(184, 4096); put(188, 512); put(208, 8192); put(212, 512); put(220, 3, 2)
    put(152 + (108 if plus else 92), 16)
    section = 152 + optional_size
    b[section:section + 5] = b'.text'
    put(section + 8, 512); put(section + 12, 4096); put(section + 16, 512); put(section + 20, 512)
    put(section + 36, 0x60000020)
    return b

with tempfile.TemporaryDirectory(prefix='dllinjector-doctor-') as tmp:
    folder = Path(tmp) / '诊断 样例 🚀'
    folder.mkdir()
    path = folder / 'sample.dll'
    for plus in (False, True):
        path.write_bytes(fixture(plus))
        arch, other = ('x64', 'x86') if plus else ('x86', 'x64')
        good = run(path, '--target-arch', arch)
        assert rules(good) == {'INSUFFICIENT_EVIDENCE'}
        assert good['data']['target_architecture'] == arch
        mismatch = run(path, '--target-arch', other, code=1)
        assert rules(mismatch) == {'ARCH_MISMATCH', 'INSUFFICIENT_EVIDENCE'}
        if not plus:
            normalized = copy.deepcopy(mismatch)
            normalized['target']['path'] = golden['target']['path']
            normalized['duration_ms'] = 0
            assert normalized == golden, 'fixed x86 DLL / x64 target report'
        no_target = run(path)
        assert no_target['data']['target_architecture'] is None
        assert rules(no_target) == {'INSUFFICIENT_EVIDENCE'}
        assert any(f['severity'] == 'warning' for f in no_target['findings'])

    raw = fixture()
    struct.pack_into('<H', raw, 150, 2)
    path.write_bytes(raw)
    assert 'NOT_A_DLL' in rules(run(path, '--target-arch', 'x86', code=1))
    for machine in (0xaa64, 0xa641, 0xa64e, 0xffff, 0x14c):
        raw = fixture(True)
        struct.pack_into('<H', raw, 132, machine)
        path.write_bytes(raw)
        assert rules(run(path, '--target-arch', 'x86')) == {'INSUFFICIENT_EVIDENCE'}
    raw = fixture()
    struct.pack_into('<II', raw, 152 + 96 + 14 * 8, 4096, 72)
    path.write_bytes(raw)
    managed = run(path, '--target-arch', 'x64')
    assert managed['data']['image']['has_clr_header']
    assert 'ARCH_MISMATCH' not in rules(managed)

    for content in (b'', b'MZ', fixture()[:511]):
        path.write_bytes(content)
        bad = run(path, code=1)
        assert rules(bad) == {'MALFORMED_PE'} and bad['data']['image'] is None
        error = bad['data']['inspection_error']
        assert error['category'] == 'malformed_pe' and error['stage'].startswith('pe.')
        assert error['win32'] is None and error['ntstatus'] is None

    path.write_bytes(fixture())
    saved = folder / 'diagnosis.json'
    report = run(path, '--target-arch', 'x64', '--output', saved, code=1)
    assert json.loads(saved.read_text(encoding='utf-8')) == report
    before = saved.read_bytes()
    failed = run(path, '--target-arch', 'x64', '--output', saved, code=4, completed=False)
    assert failed['errors'][0]['stage'] == 'output.open' and saved.read_bytes() == before
    original = hashlib.sha256(path.read_bytes()).digest()
    run(path, '--output', path, code=4, completed=False)
    assert hashlib.sha256(path.read_bytes()).digest() == original
    human = run(path, '--target-arch', 'x64', human=True, code=1)
    for text in ('ARCH_MISMATCH', 'Evidence (static_analysis)', 'Evidence (user_supplied_option)',
                 'Confidence: high', 'Next:', 'Docs: https://learn.microsoft.com/', 'unverified'):
        assert text in human

    run(path, '--timeout-ms', '0', code=5, completed=False)
    missing = run(folder / 'missing.dll', code=3, completed=False)
    assert missing['errors'][0]['category'] == 'not_found'
    # CreateFileW without directory backup semantics returns actual access denied.
    denied = run(folder, code=3, completed=False)
    assert denied['errors'][0]['category'] == 'access_denied' and denied['errors'][0]['win32'] == 5
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID,
                                  wintypes.DWORD, wintypes.DWORD, wintypes.HANDLE]
    kernel.CreateFileW.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    handle = kernel.CreateFileW(str(path), 0x80000000, 0, None, 3, 0x80, None)
    assert handle != ctypes.c_void_p(-1).value
    try:
        locked = run(path, code=4, completed=False)
        assert locked['errors'][0]['win32'] == 32 and locked['errors'][0]['stage'] == 'pe.open'
    finally:
        assert kernel.CloseHandle(handle)
    raw = fixture(); struct.pack_into('<H', raw, 152, 0x107)
    path.write_bytes(raw)
    unsupported = run(path, code=4, completed=False)
    assert unsupported['errors'][0]['category'] == 'unsupported'

    # Real compiler output is read as data, including a renamed EXE.
    for original, code, expected in [(dll, 0, None), (exe, 1, 'NOT_A_DLL')]:
        shutil.copyfile(original, path)
        before = path.read_bytes()
        doc = run(path, code=code)
        assert doc['data']['image']['import_library_count'] > 0
        if expected:
            assert expected in rules(doc)
        assert path.read_bytes() == before
        moved = path.with_suffix('.moved')
        path.rename(moved); moved.rename(path)

    for args in (['doctor'], ['doctor', ''], ['doctor', path, 'extra'],
                 ['doctor', path, '--pid', '1'], ['doctor', path, '--target-arch'],
                 ['doctor', path, '--target-arch', 'arm64'], ['doctor', path, '--target-arch', 'auto'],
                 ['doctor', path, '--target-arch', 'x64', '--target-arch', 'x86'],
                 ['pe', 'inspect', path, '--target-arch', 'x64'], ['--help', '--target-arch', 'x64'],
                 ['process', 'list', '--target-arch', 'x64']):
        invalid = invoke(args, code=2, completed=False)
        assert not invalid['execution']['command_accepted']
    raw = dll.read_bytes()
    nt, = struct.unpack_from('<I', raw, 60)
    machine, = struct.unpack_from('<H', raw, nt + 4)
    invoke(['--target-arch', 'x64' if machine == 0x8664 else 'x86', 'doctor', dll])

for modify in (
    lambda d: d['execution'].update(outcome_verified=True),
    lambda d: d.update(findings=[]),
    lambda d: d['data'].update(target_architecture=None),
    lambda d: d['data'].update(inspection_error={'category': 'malformed_pe', 'stage': 'pe.dos',
        'message': 'Malformed', 'win32': None, 'ntstatus': None, 'verified': True}),
    lambda d: d['findings'][0].update(evidence=[]),
    lambda d: d['findings'][0].update(severity='info'),
    lambda d: d['findings'][0].update(documentation=[]),
    lambda d: d['findings'][0].update(confidence='certain'),
    lambda d: d['findings'][0]['evidence'][0].update(source='runtime_observation'),
    lambda d: d['findings'][0].update(rule_id='POSSIBLE_MISSING_DEPENDENCY'),
):
    invalid = copy.deepcopy(golden)
    modify(invalid)
    assert not validator.is_valid(invalid), invalid
print('Doctor CLI contract: PASS')
