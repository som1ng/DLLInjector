"""Static inspection of real import/export/delay tables and inert synthetic data."""
import copy
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
from jsonschema import Draft202012Validator

cli, dll, root = map(Path, sys.argv[1:4])
delay_dll = Path(sys.argv[4]) if len(sys.argv) > 4 else None
schema = json.loads((root / 'schemas/cli-v1.3.json').read_text(encoding='utf-8'))
Draft202012Validator.check_schema(schema)
validator = Draft202012Validator(schema)

def run(path, code=0, human=False):
    result = subprocess.run([str(cli), 'pe', 'inspect', str(path), *([] if human else ['--json'])], capture_output=True, timeout=15)
    out, err = result.stdout.decode('utf-8'), result.stderr.decode('utf-8')
    assert result.returncode == code, (path, result.returncode, err)
    assert bool(err) == (code != 0)
    if human:
        return out
    doc = json.loads(out)
    validator.validate(doc)
    if code:
        assert doc['data'] is None and not doc['execution']['outcome_verified']
        assert doc['errors'][0]['win32'] is None and doc['errors'][0]['ntstatus'] is None
    else:
        assert doc['data']['scope'] == 'headers_sections_imports_exports_delay_imports'
        assert doc['evidence'][0]['source'] == 'static_analysis'
    return doc

def fixture(plus):
    b = bytearray(0x4200)
    def put(at, value, width=4):
        struct.pack_into({2: '<H', 4: '<I', 8: '<Q'}[width], b, at, value)
    def string(at, value):
        raw = value.encode('ascii') + b'\0'
        b[at:at + len(raw)] = raw
    put(0, 0x5a4d, 2); put(60, 128); put(128, 0x4550)
    put(132, 0x8664 if plus else 0x14c, 2); put(134, 1, 2)
    optional_size = 240 if plus else 224
    put(148, optional_size, 2); put(150, 0x2002, 2); put(152, 0x20b if plus else 0x10b, 2)
    put(152 + (24 if plus else 28), 0x140000000 if plus else 0x400000, 8 if plus else 4)
    put(184, 4096); put(188, 512); put(208, 0x5000); put(212, 512); put(220, 3, 2)
    directory = 152 + (112 if plus else 96)
    put(directory - 4, 16)
    section = 152 + optional_size
    string(section, '.rdata'); put(section + 8, 0x4000); put(section + 12, 0x1000)
    put(section + 16, 0x4000); put(section + 20, 512); put(section + 36, 0x40000040)
    put(directory + 8, 0x1000); put(directory + 12, 40)
    put(0x200, 0x1200); put(0x20c, 0x1100); put(0x210, 0x1280)
    string(0x300, 'sample.dll')
    width = 8 if plus else 4
    put(0x400, 0x1300, width); put(0x400 + width, (1 << (width * 8 - 1)) | 7, width)
    put(0x500, 9, 2); string(0x502, 'Function')
    put(directory + 13 * 8, 0x1400); put(directory + 13 * 8 + 4, 64)
    put(0x600, 1); put(0x604, 0x1500); put(0x608, 0x17c0); put(0x60c, 0x1680); put(0x610, 0x1600)
    string(0x700, 'delayed.dll'); put(0x800, 0x1700, width); put(0x900, 3, 2); string(0x902, 'LazyFunction')
    put(directory, 0x1800); put(directory + 4, 0x300)
    for offset, value in [(12, 0x1880), (16, 7), (20, 4), (24, 3), (28, 0x1900), (32, 0x1920), (36, 0x1930)]:
        put(0xa00 + offset, value)
    string(0xa80, 'exports.dll'); put(0xb00, 0x2000); put(0xb04, 0x1a40); put(0xb0c, 0x2010)
    for i, (name, rva, ordinal) in enumerate([('Alias', 0x1a00, 0), ('Alpha', 0x1a10, 0), ('Forward', 0x1a20, 1)]):
        put(0xb20 + 4 * i, rva); put(0xb30 + 2 * i, ordinal, 2); string(rva - 0xe00, name)
    string(0xc40, 'KERNEL32.Sleep')
    return b

expected = {
    'imports': [{'name': 'sample.dll', 'lookup_source': 'import_lookup_table', 'timestamp': 0, 'symbols': [
        {'name': 'Function', 'ordinal': None, 'hint': 9}, {'name': None, 'ordinal': 7, 'hint': None}]}],
    'delay_imports': [{'name': 'delayed.dll', 'lookup_source': 'delay_import_name_table', 'timestamp': 0,
                       'symbols': [{'name': 'LazyFunction', 'ordinal': None, 'hint': 3}]}],
    'exports': {'dll_name': 'exports.dll', 'ordinal_base': 7, 'address_table_entries': 4, 'symbols': [
        {'ordinal': 7, 'rva': '0x2000', 'names': ['Alias', 'Alpha'], 'forwarder': None},
        {'ordinal': 8, 'rva': '0x1a40', 'names': ['Forward'], 'forwarder': 'KERNEL32.Sleep'},
        {'ordinal': 10, 'rva': '0x2010', 'names': [], 'forwarder': None}]}
}
with tempfile.TemporaryDirectory(prefix='dllinjector-tables-') as tmp:
    folder = Path(tmp) / '导入 导出 🚀'
    folder.mkdir()
    for original in [dll, *([delay_dll] if delay_dll else [])]:
        path = folder / original.name
        shutil.copyfile(original, path)
        before = path.read_bytes()
        data = run(path)['data']
        assert data['imports'] and data['exports']
        names = [n for symbol in data['exports']['symbols'] for n in symbol['names']]
        assert ('DelayedFixtureValue' if original == delay_dll else 'FixtureValue') in names, names
        if original == delay_dll:
            lib = next(x for x in data['delay_imports'] if x['name'].lower() == 'noop_fixture.dll')
            assert any(s['name'] == 'FixtureValue' for s in lib['symbols'])
            assert all(x['name'].lower() != 'noop_fixture.dll' for x in data['imports'])
        else:
            assert not data['delay_imports']
        assert path.read_bytes() == before
        text = run(path, human=True)
        assert 'DELAY_IMPORTS' in text and 'EXPORTS' in text and 'No dependency resolution' in text
    for plus in (False, True):
        path = folder / ('synthetic64.bin' if plus else 'synthetic32.bin')
        path.write_bytes(fixture(plus))
        doc = run(path)
        assert {key: doc['data'][key] for key in expected} == expected
        for change in ['ordinal_and_name', 'missing_tables', 'invalid_name', 'wrong_delay_source']:
            invalid = copy.deepcopy(doc)
            if change == 'ordinal_and_name': invalid['data']['imports'][0]['symbols'][0]['ordinal'] = 7
            if change == 'missing_tables': del invalid['data']['exports']
            if change == 'invalid_name': invalid['data']['imports'][0]['name'] += '\n'
            if change == 'wrong_delay_source': invalid['data']['delay_imports'][0]['lookup_source'] = 'unbound_iat'
            assert not validator.is_valid(invalid), change
        broken = fixture(plus)
        struct.pack_into('<H', broken, 0xb30, 4)
        path.write_bytes(broken)
        assert run(path, code=1)['errors'][0]['stage'] == 'pe.exports'
        broken = fixture(plus)
        struct.pack_into('<I', broken, 0x600, 0)
        path.write_bytes(broken)
        assert run(path, code=4)['errors'][0]['stage'] == 'pe.delay_imports'

print('PASS PE import/export/delay CLI and schema' + ('; real MSVC delay fixture' if delay_dll else '; synthetic delay fixture'))
