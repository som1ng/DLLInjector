# P1 模块枚举交付

日期：2026-10-23。分支：`codex/p1-process-modules`。
开发前已从最新 main 创建本地及远端同名分支，先提交接口、契约与端到端测试，再实现功能。

## 已实现

- `process modules --pid <PID>`：返回模块名、Windows 报告的路径、基址和镜像大小，复用 JSON、超时、文件输出和 stderr 日志契约。
- Core `ListModules`：持有一个最小查询权限进程句柄，获取架构、创建时间及只读 Toolhelp 快照。
- x64 CLI 可查询 x86/x64 目标；x86 → x64 明确返回 unsupported / 退出码 4，并提示使用 x64 CLI。ARM64 目标暂不支持。
- 创建快照遇到 ERROR_BAD_LENGTH 最多尝试 8 次，重试等待计入查询期限；其他错误及遍历失败原样报告，不输出成功的半份列表。
- 模块基址使用十六进制字符串，避免 JavaScript 精度丢失；名称/路径提供可能截断及可能失真的标记。
- CLI 版本升级到 `2.0.0-dev.2`，所有命令输出 Schema 1.1；Schema 1.0 及旧固定样例保留不变。
- Windows CI 增加独立的跨位数作业，复用矩阵构建的 x86/x64 CLI 和自有测试宿主。

## 修改位置

| 文件 | 作用 |
| --- | --- |
| `include/dllinjector/process.h`、`core/process/` | 模块类型、只读查询、重试、错误及字段边界 |
| `apps/cli/`、`include/dllinjector/version.h` | 命令解析、JSON/文本输出、版本 |
| `schemas/cli-v1.1.json`、`tests/fixtures/json/modules.json` | 新版 Schema 与固定模块样例 |
| `tests/module_tests.cpp` | 成功、错误、架构、重试、超时及边界六组替身测试 |
| `tests/fixtures/module_host.cpp`、`tests/modules_contract.py` | 自有宿主加载原无副作用 DLL，真实 CLI 和跨位数验证 |
| `tests/cli_contract.py` | 新版本断言，继续验证旧 Schema/样例兼容性 |
| CMake、Windows workflow、README、CHANGELOG、契约文档 | 构建、CI 和用户说明 |

## 实测发现与处理

第一轮真实 Unicode 测试发现：本机 `Module32FirstW` 返回的模块路径和名称已将 emoji 替换为 `?`，
导致按原文件名匹配 DLL 失败。Core 的 UTF-16 → UTF-8 测试本身通过，不能把系统提供的失真文本当作准确路径。

因此保留系统报告值，并在检测到 `?` 时设置 `text_may_be_lossy`；固定缓冲区达到上限时设置
`name_may_be_truncated` / `path_may_be_truncated`。这些是保守提示，不能检测所有字符替换，也不验证文件身份。
调用方不应把带标记的路径直接用于文件操作。

真实测试宿主通过独立的 READY 握手报告已加载 fixture 的基址，测试据此匹配模块，
再检查路径完全一致或明确标记系统文本失真。这样既验证观察到的是目标 DLL，也不会通过修改期望字符串掩盖 API 限制。
模块出现本身不证明 DLL 初始化成功；测试宿主的显式 fixture 导出检查属于测试协议，不是诊断命令的行为。

## 本机验证结果

执行各配置的 `cmake --preset`、`cmake --build --preset ... --parallel 4` 和 `ctest --preset ...`：

| 配置 | 构建 | CTest |
| --- | --- | --- |
| MSVC x64 Debug | 通过 | 23/23 |
| MSVC x64 Release | 通过 | 23/23 |
| MSVC x86 Debug | 通过 | 23/23 |
| MSVC x86 Release | 通过 | 23/23 |
| MinGW x64 Release | 通过 | 23/23 |

共 115 次 CTest 执行：保留原 16 组，加上 6 组模块单元测试与 1 组模块 CLI 测试。
另外实际运行 `tests/modules_contract.py` 的两个跨位数用例，均通过：

- x64 CLI → 自有 x86 宿主：成功枚举，并匹配宿主报告的 DLL 基址。
- x86 CLI → 自有 x64 宿主：按契约返回退出码 4，category=unsupported，stage=modules.architecture。

最终五组构建日志未检出编译器 warning/error。日志在 `build/modules-*.log`，不提交到仓库。
远端验证以 [本分支的 GitHub Actions](https://github.com/som1ng/DLLInjector/actions/workflows/windows.yml?query=branch%3Acodex%2Fp1-process-modules) 为准；合并前须通过四组矩阵任务和跨位数任务。

## 使用与后续

```powershell
& .\build\msvc-x64\Release\cli\dllinjector.exe process modules --pid $PID --json
```

GUI 路径与既有行为保留。当前 P1 的 process list / inspect / modules 三个命令已具备可运行实现。
下一阶段是独立分支上的 PE 解析与损坏文件测试，再接 DLL Doctor；Skill/MCP/安装器继续按依赖顺序推进。

限制：这是会变化的加载器快照，不包含按数据文件方式加载的模块；超时不能中断正在执行的系统调用或阻塞输出。
ARM64、旧 Windows 实机仍未验证；未执行第三方进程注入，未发布 Release，也未修改 Agent 配置。
项目级许可证确认仍是正式发布前的待办。
