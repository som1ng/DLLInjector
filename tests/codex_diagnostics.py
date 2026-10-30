"""Evidence and answer checks for two static Doctor acceptance scenarios."""
from pathlib import Path

from mcp_model_fixture import HostCheckError, same_json


def schema(properties):
    return {'type': 'object', 'additionalProperties': False,
            'properties': properties, 'required': list(properties)}


MISMATCH_SCHEMA = schema({
    'image_architecture': {'type': 'string', 'enum': ['x86', 'x64', 'unknown']},
    'target_architecture': {'type': 'string', 'enum': ['x86', 'x64', 'unknown']},
    'target_architecture_source': {'type': 'string', 'enum': ['user_supplied_option', 'process_observation', 'unknown']},
    'architecture_matches': {'type': 'boolean'}, 'blocking_rule': {'type': 'string'},
    'diagnostic_exit_code': {'type': 'integer'}, 'diagnosis_completed': {'type': 'boolean'},
    'loadability_verified': {'type': 'boolean'},
})
DEPENDENCY_SCHEMA = schema({
    'reported_error_code': {'type': 'integer'},
    'error_code_source': {'type': 'string', 'enum': ['user_report', 'native_observation', 'unknown']},
    'search_scope': {'type': 'string', 'enum': ['explicit_directories', 'windows_loader', 'unknown']},
    **{key: {'type': 'array', 'items': {'type': 'string'}} for key in
       ('candidate_names', 'missing_in_search_dirs', 'api_set_names')},
    'possible_missing_confidence': {'type': 'string', 'enum': ['low', 'medium', 'high', 'unknown']},
    'root_cause_confirmed': {'type': 'boolean'}, 'candidate_loadability_verified': {'type': 'boolean'},
    'loadability_verified': {'type': 'boolean'}, 'diagnosis_completed': {'type': 'boolean'},
    'diagnostic_exit_code': {'type': 'integer'},
})


def diagnostic_answer(name, report):
    if (report['command'] != 'doctor' or report['status'] != 'success'
            or report['execution']['operation_completed'] is not True
            or report['execution']['outcome_verified'] is not False):
        raise HostCheckError('Doctor fixture did not complete static analysis')
    findings = {row['rule_id']: row for row in report['findings']}
    if name == 'mismatch':
        data = report['data']
        rule = findings.get('ARCH_MISMATCH', {})
        if (data['image']['machine'] != 0x14c or data['image']['format'] != 'PE32'
                or data['image']['has_clr_header'] is not False or data['target_architecture'] != 'x64'
                or rule.get('severity') != 'error' or rule.get('confidence') != 'high'
                or 'user_supplied_option' not in {row['source'] for row in rule.get('evidence', [])}):
            raise HostCheckError('mismatch scenario requires a native x86 DLL and declared x64 target')
        return {'image_architecture': 'x86', 'target_architecture': 'x64',
            'target_architecture_source': 'user_supplied_option', 'architecture_matches': False,
            'blocking_rule': 'ARCH_MISMATCH', 'diagnostic_exit_code': 1,
            'diagnosis_completed': True, 'loadability_verified': False}
    dependencies = report['data']['dependencies']
    rule = findings.get('POSSIBLE_MISSING_DEPENDENCY', {})
    if (dependencies['scope'] != 'explicit_directories' or rule.get('severity') != 'warning'
            or rule.get('confidence') != 'low'):
        raise HostCheckError('dependency fixture did not preserve scoped uncertainty')
    names = {key: sorted(row['name'] for row in dependencies['libraries'] if row['status'] == status)
             for key, status in [('candidate_names', 'candidate_found'),
                                 ('missing_in_search_dirs', 'not_found_in_search_dirs'),
                                 ('api_set_names', 'api_set_unresolved')]}
    if (set(name.casefold() for name in names['candidate_names']) != {'vcruntime140.dll'}
            or not names['missing_in_search_dirs'] or not names['api_set_names']):
        raise HostCheckError('dependency scenario needs candidate, absent and API Set observations')
    return {'reported_error_code': 126, 'error_code_source': 'user_report',
        'search_scope': 'explicit_directories', **names, 'possible_missing_confidence': 'low',
        'root_cause_confirmed': False, 'candidate_loadability_verified': False,
        'loadability_verified': False, 'diagnosis_completed': True, 'diagnostic_exit_code': 0}


def prepare_diagnostic(name, sample, pe_report, invoke):
    """invoke reads the actual packaged CLI; expected answers never enter the project files."""
    if name == 'pe':
        return None
    if name == 'mismatch':
        if pe_report['data']['machine'] != 0x14c:
            raise HostCheckError('use the repository-built x86 noop_fixture.dll for --scenario mismatch')
        report = invoke(['doctor', sample, '--target-arch', 'x64'], expected_code=1)
        return {'name': name, 'report': report, 'exit_code': 1, 'schema': MISMATCH_SCHEMA,
            'answer': diagnostic_answer(name, report),
            'prompt': f'目标程序我确认是 x64。请检查这个 DLL 的架构是否匹配：{sample}。'
                      '同时说明目标架构的信息来源、阻断规则、诊断是否完成以及是否验证了可加载。'
                      '只做静态只读检查，不加载或执行 DLL，使用给定 JSON 格式回答。'}
    if name != 'error-126':
        raise HostCheckError('unknown diagnostic scenario')
    if 'vcruntime140.dll' not in {row['name'].casefold() for row in pe_report['data']['imports']}:
        raise HostCheckError('error-126 scenario needs the MSVC-built noop_fixture.dll')
    directory = sample.parent / '依赖 candidates'
    directory.mkdir()
    # A fixed filename in a freshly created directory: never turn an import name into a write path.
    (directory / 'VCRUNTIME140.dll').write_bytes(b'Inert candidate; deliberately not PE. Never load.\n')
    report = invoke(['doctor', sample, '--search-dir', directory])
    if report['data']['dependencies']['search_directories'] != [str(directory)]:
        raise HostCheckError('native report widened the requested dependency search scope')
    return {'name': name, 'report': report, 'exit_code': 0, 'schema': DEPENDENCY_SCHEMA,
        'search_dir': directory, 'answer': diagnostic_answer(name, report),
        'prompt': f'应用加载这个 DLL 报 126：{sample}。请只在这个目录检查依赖候选：{directory}。'
                  '先只检查主 DLL 和依赖候选的存在情况，不读取候选文件的内容。'
                  '区分已发现候选、目录中没找到的依赖和 API Set；能否确定根因、候选是否可用、DLL 是否能加载？'
                  '也说明错误码的来源、检查范围和诊断是否完成。只做静态只读检查，不加载或执行 DLL，使用给定 JSON 格式回答。'}


def diagnostic_call(tool, arguments, case, pe_report):
    common = {'path', 'timeout_ms', 'max_output_bytes'}
    if tool == 'inspect_pe' and not set(arguments) - common:
        return {'source': 'live_cli', 'report': pe_report, 'cli_exit_code': 0}, False
    if case['name'] == 'mismatch':
        valid = (tool == 'diagnose_dll' and not set(arguments) - (common | {'target_arch', 'search_directories'})
                 and arguments.get('target_arch') == 'x64' and arguments.get('search_directories', []) == [])
    else:
        directories = arguments.get('search_directories')
        valid = (tool in ('diagnose_dll', 'analyze_dependencies')
                 and not set(arguments) - (common | {'search_directories'})
                 and isinstance(directories, list) and len(directories) == 1
                 and isinstance(directories[0], str) and Path(directories[0]).is_absolute()
                 and Path(directories[0]).resolve() == case['search_dir'].resolve())
    if not valid:
        raise HostCheckError('model used a different diagnostic target, scope or tool')
    return {'source': 'live_cli', 'report': case['report'], 'cli_exit_code': case['exit_code']}, True


def answer_matches(answer, expected):
    # DLL names are case-insensitive; ordering is immaterial, duplicates are not accepted.
    if not isinstance(answer, dict):
        return False
    normalized, baseline = dict(answer), dict(expected)
    for key in ('candidate_names', 'missing_in_search_dirs', 'api_set_names'):
        if key not in baseline:
            continue
        names = normalized.get(key)
        if not isinstance(names, list) or any(not isinstance(name, str) for name in names):
            return False
        names = [name.casefold() for name in names]
        if len(set(names)) != len(names):
            return False
        normalized[key] = sorted(names)
        baseline[key] = sorted(name.casefold() for name in baseline[key])
    return same_json(normalized, baseline)
