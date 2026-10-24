# 依赖候选检查交付

日期：2026-10-24。分支：`codex/p2-dependency-candidates`。版本：`2.0.0-dev.6` / Schema `1.5`。

## 本轮完成

- Doctor 新增可重复的 `--search-dir`（最多 16 个）；只在显式目录观察主 PE 直接普通/延迟导入的文件候选，不自动扩大搜索范围。
- Core 增加 `InspectDoctorFile` 与候选结果类型，复用 PE 解析；CLI 仅解析参数和格式化文本/JSON。候选文件只查属性，不读取内容或加载执行。
- 保留每个目录的 file/not_found/not_file/unavailable 观察。权限错误与重解析点不会被当作不存在；其他目录已找到候选也不丢失错误证据。
- API Set 形式名称和不支持的路径/设备/流等名称跳过文件探测。普通文件名的支持范围见 [依赖契约](DEPENDENCIES_CONTRACT.md)。
- `POSSIBLE_MISSING_DEPENDENCY` 为 warning / low，仅表示指定目录范围内没有候选；不宣称 Known DLL、SxS、延迟加载或运行时环境中的真实缺失。
- 报告新增 `filesystem_observation` 与 `filesystem_view=caller_process`，明确 WOW64 影响；所有 Doctor 结果仍不验证加载效果。
- 主文件与依赖阶段共用超时预算；Schema 1.5、历史兼容验证、固定 JSON、README 与 CI 审查产物同步更新。

核心文件为 `include/dllinjector/doctor.h`、`core/doctor/dependencies.cpp`、`apps/cli/`、
`schemas/cli-v1.5.json`、`tests/dependency_tests.cpp`、`tests/dependencies_contract.py` 和 `tests/fixtures/json/doctor-dependencies.json`。
按约定先提交了接口、契约和未注册的规则测试，再实现并运行检查；开发前已建立同名本地/远端分支。

## 已执行的本地验证

沿用现有依赖与构建缓存，无重新安装。

| preset | 完整构建与 CTest |
| --- | --- |
| msvc-x64-debug | PASS，52/52 |
| msvc-x64-release | PASS，52/52 |
| msvc-x86-debug | PASS，52/52 |
| msvc-x86-release | PASS，52/52 |
| mingw-x64 | PASS，52/52 |

每种配置执行：`cmake --build --preset <preset> --parallel 4` 与 `ctest --preset <preset> --output-on-failure`。
完整矩阵通过后仅移除新增文件的无用 include、统一新增 else 的括号格式；五种配置再次以
`cmake --build --preset <preset> --target dllinjector_cli dependency_tests --parallel 4` 增量构建通过。
最终构建日志未发现 warning/error，`git diff --check` 通过。

两项跨位数回归实际通过：

```powershell
python tests/modules_contract.py build/msvc-x86/Release/cli/dllinjector.exe build/msvc-x86/Release/module_host.exe build/msvc-x86/Release/noop_fixture.dll . build/msvc-x64/Release/cli/dllinjector.exe 0
python tests/modules_contract.py build/msvc-x64/Release/cli/dllinjector.exe build/msvc-x64/Release/module_host.exe build/msvc-x64/Release/noop_fixture.dll . build/msvc-x86/Release/cli/dllinjector.exe 4
```

新增 7 组 Core 测试和 1 组 CLI 契约测试，验证：候选与缺失、目录冒充文件、不可访问结果、同名多候选、API Set 与受限名称零探测、普通/延迟导入分组、目录/库上限、重解析点、跨阶段预算、Unicode/相对路径、无效选项和输出不覆盖。
权限失败与时钟由内部 Win32 API seam 稳定注入，没有修改 ACL 或依赖管理员权限。
端到端测试使用无害 PE 字节和实际编译产物；候选故意包含非 PE 文本，证明只做属性观察。完整规范化 JSON 与固定 golden 一致，历史样例按原 Schema 验证。

首轮编译发现新测试函数与 Windows SDK 的 `Unknown` 枚举重名，以及错误码比较的有符号警告；已重命名测试函数并修正比较类型，之后完整矩阵通过。
本轮未更改 PE 字节解析逻辑，没有重跑 ASan 或测量代码覆盖率百分比。
远端 Windows workflow 执行四种 MSVC 配置与跨位数检查；以本功能 PR 的 Checks 为准，全部通过后合并。

## 保留的边界与下一步

这不是 Windows 加载器或递归依赖解析器。候选的内容、导出符号、架构和签名均未验证；Known DLL/SxS/API Set 映射、动态加载及实际搜索顺序仍需目标环境证据。
显式目录使用调用方的 Win32 文件系统视图，目录链接和文件系统变化不会被隔离成沙箱。Win32 调用为协作式超时，调用方仍可设置子进程墙钟超时。
下一功能分支补 Markdown/HTML 报告导出，随后推进权威 Agent Skill；这些尚未在本轮实现。
