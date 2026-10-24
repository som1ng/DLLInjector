"""Explicit-directory metadata checks over inert imports and real compiler output."""
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator

cli, dll, root = map(Path, sys.argv[1:4])
delay_dll = Path(sys.argv[4]) if len(sys.argv) > 4 else None
schema = json.loads((root / 'schemas/cli-v1.5.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)
golden = json.loads((root / 'tests/fixtures/json/doctor-dependencies.json').read_text(encoding='utf-8'))
validator.validate(golden)

def invoke(args, code=0, human=False, cwd=None):
    result = subprocess.run([str(cli.resolve()), *map(str, args), *([] if human else ['--json'])],
                            capture_output=True, timeout=15, cwd=cwd)
    out, err = result.stdout.decode('utf-8'), result.stderr.decode('utf-8')
    assert result.returncode == code, (args, result.returncode, out, err)
    assert bool(err) == (code not in (0, 1)), (args, err)
    if human:
        return out
    doc = json.loads(out)
    validator.validate(doc)
    assert not doc['execution']['outcome_verified']
    if code not in (0, 1):
        assert doc['status'] == 'error' and doc['data'] is None and not doc['findings'] and not doc['evidence']
    return doc

imports = ['present.dll', 'missing.dll', 'API-MS-WIN-CORE-FILE-L1-1-0.DLL', '../escape.dll', 'folder.dll', 'NUL.dll']
delayed = ['later.dll', 'missing-delay.dll']

def fixture(plus=True):
    b = bytearray(0x2200)
    def put(at, value, width=4):
        struct.pack_into({2: '<H', 4: '<I', 8: '<Q'}[width], b, at, value)
    def string(at, value):
        text = value.encode('ascii') + b'\0'
        b[at:at + len(text)] = text
    put(0, 0x5a4d, 2); put(60, 128); put(128, 0x4550)
    put(132, 0x8664 if plus else 0x14c, 2); put(134, 1, 2)
    optional = 240 if plus else 224
    put(148, optional, 2); put(150, 0x2002, 2); put(152, 0x20b if plus else 0x10b, 2)
    put(152 + (24 if plus else 28), 0x140000000 if plus else 0x400000, 8 if plus else 4)
    put(184, 4096); put(188, 512); put(208, 0x3000); put(212, 512); put(220, 3, 2)
    directories = 152 + (112 if plus else 96)
    put(directories - 4, 16)
    section = 152 + optional
    string(section, '.rdata'); put(section + 8, 0x2000); put(section + 12, 0x1000)
    put(section + 16, 0x2000); put(section + 20, 512); put(section + 36, 0x40000040)
    put(directories + 8, 0x1000); put(directories + 12, (len(imports) + 1) * 20)
    for i, name in enumerate(imports):
        put(0x200 + i * 20, 0x1600 + i * 16)
        put(0x20c + i * 20, 0x1200 + i * 64)
        put(0x210 + i * 20, 0x1700 + i * 16)
        string(0x400 + i * 64, name)
    put(directories + 13 * 8, 0x1800); put(directories + 13 * 8 + 4, (len(delayed) + 1) * 32)
    for i, name in enumerate(delayed):
        for offset, value in [(0, 1), (4, 0x1c00 + i * 64), (8, 0x1e00 + i * 16),
                              (12, 0x1b00 + i * 16), (16, 0x1a00 + i * 16)]:
            put(0xa00 + i * 32 + offset, value)
        string(0xe00 + i * 64, name)
    return b

with tempfile.TemporaryDirectory(prefix='dllinjector-dependencies-') as tmp:
    folder = Path(tmp) / '依赖 候选 🚀'
    first, second = folder / '应用', folder / '附加'
    first.mkdir(parents=True); second.mkdir()
    # Deliberately not PE: the command only observes metadata, never loads/parses candidates.
    for path in (first / 'present.dll', second / 'present.dll', second / 'later.dll',
                 first / imports[2], folder / 'escape.dll'):
        path.write_bytes(b'inert candidate, not a PE\n')
    (first / 'folder.dll').mkdir()
    primary = folder / 'plugin.dll'
    for plus in (False, True):
        primary.write_bytes(fixture(plus))
        args = ['doctor', primary, '--target-arch', 'x64' if plus else 'x86', '--search-dir', first, '--search-dir', second]
        doc = invoke(args)
        expected = copy.deepcopy(golden['data']['dependencies'])
        expected['search_directories'] = [str(first), str(second)]
        assert doc['data']['dependencies'] == expected
        assert doc['data']['image']['import_library_count'] == 6
        assert doc['data']['image']['delay_import_library_count'] == 2
        assert len([f for f in doc['findings'] if f['rule_id'] == 'POSSIBLE_MISSING_DEPENDENCY']) == 1
        possible = next(f for f in doc['findings'] if f['rule_id'] == 'POSSIBLE_MISSING_DEPENDENCY')
        assert possible['severity'] == 'warning' and possible['confidence'] == 'low'
        assert doc['status'] == 'success' and not doc['errors']
        assert {e['source'] for e in doc['evidence']} == {'static_analysis', 'filesystem_observation'}
        if plus:
            normalized = copy.deepcopy(doc)
            normalized['target']['path'] = golden['target']['path']
            normalized['data']['dependencies']['search_directories'] = golden['data']['dependencies']['search_directories']
            normalized['duration_ms'] = 0
            assert normalized == golden, 'complete dependency diagnostic report matches fixed golden'
    relative = invoke(['doctor', primary, '--search-dir', first.name], cwd=folder)
    assert relative['data']['dependencies']['search_directories'] == [str(first)]
    no_dirs = invoke(['doctor', primary])
    assert no_dirs['data']['dependencies'] is None
    assert all(f['rule_id'] != 'POSSIBLE_MISSING_DEPENDENCY' for f in no_dirs['findings'])
    human = invoke(args, human=True)
    for expected in ('POSSIBLE_MISSING_DEPENDENCY', 'Confidence: low', 'caller filesystem view', 'delay_import',
                     'not_found_in_search_dirs', 'api_set_unresolved', 'unsupported_name', str(first)):
        assert expected in human
    before = primary.read_bytes()
    invoke([*args, '--output', primary], code=4)
    assert primary.read_bytes() == before
    saved = folder / 'dependencies.json'
    doc = invoke([*args, '--output', saved])
    assert json.loads(saved.read_text(encoding='utf-8')) == doc
    invoke([*args, '--timeout-ms', '0'], code=5)
    invoke(['doctor', primary, '--search-dir', folder / 'absent'], code=3)
    invoke(['doctor', primary, '--search-dir', first / 'present.dll'], code=2)
    invoke(['doctor', primary, '--search-dir', r'\\server\share'], code=4)
    invoke(['doctor', primary, '--search-dir', '\\\\?\\' + str(first)], code=4)
    invoke(['doctor', primary, *(['--search-dir', first] * 16)])
    for invalid in (['doctor', primary, '--search-dir'], ['doctor', primary, '--search-dir', ''],
                    ['doctor', primary, *(['--search-dir', first] * 17)],
                    ['pe', 'inspect', primary, '--search-dir', first], ['process', 'list', '--search-dir', first],
                    ['--version', '--search-dir', first], ['doctor', primary, '--json', '--json']):
        assert not invoke(invalid, code=2)['execution']['command_accepted']
    # Malformed primary input never triggers probing from unparsed metadata.
    primary.write_bytes(b'MZ')
    malformed = invoke(['doctor', primary, '--search-dir', folder / 'absent'], code=1)
    assert malformed['data']['dependencies'] is None
    assert malformed['findings'][0]['rule_id'] == 'MALFORMED_PE'
    for real in [dll, *([delay_dll] if delay_dll else [])]:
        data = invoke(['doctor', real, '--search-dir', real.parent])['data']['dependencies']
        assert data['libraries']
        if real == delay_dll:
            row = next(r for r in data['libraries'] if r['name'].lower() == dll.name.lower() and r['kind'] == 'delay_import')
            assert row['status'] == 'candidate_found'

for change in (
    lambda d: d['data']['dependencies'].update(search_directories=[]),
    lambda d: d['data']['dependencies'].update(filesystem_view='target_process'),
    lambda d: d['data']['dependencies']['libraries'][0]['candidates'][0].update(directory_index=16),
    lambda d: d['data']['dependencies']['libraries'][0]['candidates'][0].update(status='unavailable'),
    lambda d: d['data']['dependencies']['libraries'][1].update(status='candidate_found'),
    lambda d: d['data']['dependencies']['libraries'][2].update(candidates=[{'directory_index': 0, 'status': 'file', 'error': None}]),
    lambda d: d['findings'][1].update(confidence='high'),
    lambda d: d['findings'][1].update(severity='error'),
    lambda d: d['findings'].pop(1),
    lambda d: d.update(evidence=[{'source': 'static_analysis', 'description': 'Not a filesystem observation'}]),
):
    invalid = copy.deepcopy(golden)
    change(invalid)
    assert not validator.is_valid(invalid), invalid
print('Dependency candidate CLI contract: PASS')
