# DLL Doctor 首批规则交付

日期：2026-10-23。分支：`codex/p2-doctor`。版本：`2.0.0-dev.5` / Schema `1.4`。

## 完成内容

- `include/dllinjector/doctor.h`、`core/doctor/doctor.cpp`：复用现有 PE 结果的纯规则 API。
- `apps/cli/`：`doctor <path> [--target-arch x86|x64]`、可读文本与 JSON、诊断发现对应退出码。
- `schemas/cli-v1.4.json`：诊断数据、finding 字段与执行状态约束；历史 Schema/fixture 不变。
- `tests/doctor_tests.cpp`、`tests/doctor_contract.py`、固定 `doctor-mismatch.json`：规则正反例与 CLI 端到端验证。
- CMake/Windows CI 注册新测试，并把新契约和 Schema 放入审查产物；README/Changelog 更新。

规则包括 `MALFORMED_PE`、`NOT_A_DLL`、`ARCH_MISMATCH`、`INSUFFICIENT_EVIDENCE`。
具体触发条件、证据来源和状态约定见 [契约](DOCTOR_CONTRACT.md)。
目标架构来自用户参数，CLI 不猜测宿主位数；CLR 声明、ARM/EC/X、未知或不一致的 Machine/format 均不套用原生 x86/x64 比较。

诊断完成即 `status=success`，阻断发现仍返回 exit 1；Doctor 的 `outcome_verified` 始终为 false。
文件读不到、超时、不支持、输出失败进入操作错误，不能被误报成损坏 PE，也不会保留伪成功 findings。

## 实际本地验证

沿用已有 CMake 缓存与 Python 环境，没有重新安装依赖。

| 配置 | 完整构建与 CTest | 最后定向回归 |
| --- | --- | --- |
| MSVC x64 Debug | PASS，44/44 | PASS，7/7 |
| MSVC x64 Release | PASS，44/44 | PASS，7/7 |
| MSVC x86 Debug | PASS，44/44 | PASS，7/7 |
| MSVC x86 Release | PASS，44/44 | PASS，7/7 |
| MinGW-w64 x64 Release | PASS，44/44 | PASS，7/7 |

完整检查使用 `cmake --build --preset <preset> --parallel 4` 和 `ctest --preset <preset> --output-on-failure`，
preset 为 `msvc-x64-debug`、`msvc-x64-release`、`msvc-x86-debug`、`msvc-x86-release`、`mingw-x64`。
完整通过后修正了测试中的无符号比较警告，并统一了新增代码的括号格式；
五种配置再次构建 `doctor_tests dllinjector_cli`，使用
`ctest --preset <preset> -R '^(doctor\.|cli.doctor$|cli.contract$)' --output-on-failure` 补跑。
最后构建日志未发现 warning/error，`git diff --check` 通过。

跨位数回归也实际通过：

```powershell
python tests/modules_contract.py build/msvc-x86/Release/cli/dllinjector.exe build/msvc-x86/Release/module_host.exe build/msvc-x86/Release/noop_fixture.dll . build/msvc-x64/Release/cli/dllinjector.exe 0
python tests/modules_contract.py build/msvc-x64/Release/cli/dllinjector.exe build/msvc-x64/Release/module_host.exe build/msvc-x64/Release/noop_fixture.dll . build/msvc-x86/Release/cli/dllinjector.exe 4
```

新增测试覆盖原生 x86/x64 双向匹配/不匹配、无目标、缺少 DLL 标记、未知/ARM64/ARM64EC/ARM64X、
CLR 声明、Machine/format 不一致、空/截断文件、实际编译 DLL/EXE、Unicode 路径、缺失文件、
真实 Win32 访问拒绝（目录按文件打开）、共享冲突、立即超时、Unsupported、输出不覆盖、参数错误及 Schema 反例。
Core 另用注入的错误对象验证 AccessDenied 等所有错误类别原样传播，不依赖管理员权限或修改 ACL。
静态样例从未被 Doctor 加载；旧 smoke.fixture 仍只执行项目自身的无害 fixture。

首轮发现旧 PE golden 被新 Schema 验证器拒绝：已改为先按历史 1.3 验证，再仅在内存中升级版本字段测试当前约束，磁盘样例未改写。
本轮没有新增字节解析逻辑，没有重跑 ASan，也没有测量覆盖率百分比。

远端 Windows workflow 执行四种 MSVC 配置和跨位数检查；具体结果见本功能 PR 的 Checks，全部通过后才允许合并。

## 限制与下一步

- 本批不搜索依赖目录，不生成缺失依赖结论。静态导入/延迟导入数量不代表实际加载是否发生；动态加载也未验证。
- 没有读取 CLR 头部/CorFlags、验证签名、模拟 Windows 加载器或执行初始化，不能由 exit 0 得出可加载结论。
- 保留 PE 文件大小、目录/表资源上限、本地路径与协作式超时限制，见 PE 契约。
- 后续独立分支实现有明确搜索范围的依赖候选检查，再补 Markdown/HTML 报告与权威 Agent Skill；这些能力尚未完成。
