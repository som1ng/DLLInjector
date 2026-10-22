# DLLInjector

Windows C++ DLL 实验项目，正在向 **DLL / PE 诊断、只读进程分析与 Agent 本地工具**演进。

已具备 **只读进程 Core / CLI、版本化 JSON 和 Windows 回归测试**，现有 Win32 GUI 保留。CLI 当前版本为 `2.0.0-dev.1`，尚未发布正式 V2.0；PE Doctor、模块枚举、Skill、MCP 和一行安装器继续按路线推进。参见 [CLI 契约](docs/P1_CONTRACT.md)、[P1 交付](docs/P1_DELIVERY.md) 和 [原始基线](docs/BASELINE.md)。

## 当前能力与验证边界

| 项目 | 状态 |
| --- | --- |
| Win32 GUI、进程列表、文件选择 | 可从源码构建；具备隐藏窗口启动/关闭测试 |
| 架构查询 | x86/x64/ARM64/Unknown 明确区分；查询失败携带错误与阶段；GUI 当前仅接受匹配的 x86/x64 目标 |
| 原有远程线程、APC、消息钩子、反射式与 DLL 部署方法 | 保留源代码和 GUI 入口；本轮仅验证构建，没有验证这些方法的运行效果 |
| Direct Syscall | GNU x64 实验源码保留；未纳入当前 CMake 构建；GUI 标注不可用 |
| CMake / CTest | MSVC x64/x86 Debug、Release；可选 MinGW-w64 x64 |
| Windows CI | 已配置构建、测试和审查产物上传；是否通过以实际 workflow 运行结果为准 |
| CLI / Core / JSON | `process list`、`process inspect`、帮助/版本、JSON、超时、输出文件、stderr 日志；GUI 与 CLI 复用进程查询 |
| 模块枚举 / PE Doctor / Agent 集成 | 路线图，尚不可调用 |

GUI 启动不再自动申请 Debug Privilege。现有方法返回的 `true` 只用于显示“请求已提交，预期效果尚未验证”，不能证明 DLL 已完成初始化。旧反射式及部署代码的 PE 边界检查等已知问题记录在基线报告中，不能直接作为未来 Doctor 解析器使用。

## 构建与测试

需要 Windows、CMake 3.21+、Visual Studio 2022 的“使用 C++ 的桌面开发”工具与 Windows SDK。
测试还需要 Python 3.9+ 和 `jsonschema`；CLI 本身不依赖 Python，也不会下载依赖。

```powershell
git clone https://github.com/som1ng/DLLInjector.git
cd DLLInjector
python -m pip install -r tests/requirements.txt
cmake --preset msvc-x64
cmake --build --preset msvc-x64-debug --parallel
ctest --preset msvc-x64-debug
```

GUI 位于 `build/msvc-x64/Debug/DLLInjector.exe`；CLI 位于
`build/msvc-x64/Debug/cli/dllinjector.exe`。Windows 文件名不区分大小写，因此二者分目录存放。
只构建、不运行测试时，可在配置命令后加 `-DBUILD_TESTING=OFF`，无需 Python。

## CLI 快速开始

```powershell
& .\build\msvc-x64\Debug\cli\dllinjector.exe --help
& .\build\msvc-x64\Debug\cli\dllinjector.exe --version --json
& .\build\msvc-x64\Debug\cli\dllinjector.exe process list --json
# 查询当前 PowerShell 的 PID；不修改目标进程，不自动提权。
& .\build\msvc-x64\Debug\cli\dllinjector.exe process inspect --pid $PID --json
& .\build\msvc-x64\Debug\cli\dllinjector.exe process inspect --pid $PID --json --timeout-ms 5000 --output .\process-report.json
```

stdout 为结果，stderr 为错误与 `--verbose` 日志。重定向及输出文件使用无 BOM UTF-8；
`--output` 要求文件不存在，防止覆盖已有报告。JSON Schema 在 [schemas/cli-v1.json](schemas/cli-v1.json)。
创建时间以十进制 FILETIME **字符串**提供，避免 JavaScript 数字精度丢失；列表中无法读取的创建时间为 `null`，原因在该条目的 `errors` 中。

退出码：`0` 成功，`1` 保留给后续诊断发现，`2` 参数错误，`3` 目标不可访问，
`4` 内部/不支持/输出失败，`5` 查询超时。`--timeout-ms 0` 会立即返回查询超时。
超时在 Win32 调用之间检查，不能打断正在执行的系统调用或阻塞的输出；需要严格墙钟限制的调用方也应设置子进程超时。

## 其他构建组合

其他已配置组合：

```powershell
cmake --build --preset msvc-x64-release --parallel
ctest --preset msvc-x64-release

cmake --preset msvc-x86
cmake --build --preset msvc-x86-debug --parallel
ctest --preset msvc-x86-debug
cmake --build --preset msvc-x86-release --parallel
ctest --preset msvc-x86-release
```

可选 MinGW-w64 x64：确保 `gcc`、`g++`、`ninja` 在 PATH，然后运行 `cmake --preset mingw-x64`、`cmake --build --preset mingw-x64`、`ctest --preset mingw-x64`。不提供 MinGW x86 或 ARM64 构建的验证声明。

VS Code 的默认构建任务使用 CMake x64 Debug；F5 使用同一构建产物，需要 Microsoft C/C++ 扩展。

## 测试内容

- 文件对话框字符容量、边界、取消与中文路径。
- 现代/旧版架构查询、未知架构、访问失败、资源关闭与原始错误保留。
- 权限 API 部分分配与失败：使用替身，不改变测试者的真实权限。
- 实际当前进程的只读查询与枚举、中文路径文件读取。
- Core 的快照创建/遍历失败、权限不足、同一句柄查询、超时与错误保留。
- CLI 子进程契约、JSON Schema 与固定样例、中文/空格/emoji 路径、输出文件保护、stdout/stderr 分离。
- GUI 隐藏启动/关闭且不自动启用 Debug Privilege、原有控制台 Demo 启动。
- 测试程序在自身进程加载新建的无副作用 DLL，并检查显式导出返回值。

CTest 不运行对第三方进程的注入或目录部署，也不运行仓库内已有的反射式二进制。`demo_dll` 是保留的旧弹窗样例，只参与构建；它与新建的无副作用 `noop_fixture` 不同。

## 目录

```text
src/                   原 Win32 GUI、工具函数与方法实现
include/dllinjector/    UI 无关的 Result/Error、进程类型与公共接口
core/                  只读 Windows 进程查询、RAII、Unicode 转换
apps/cli/              参数解析、文本/JSON 输出与命令行入口
schemas/               版本化 JSON Schema
dll_test/              原 Demo 与第三方反射式样例
tests/                 Win32 回归测试、启动测试与无副作用 fixture
docs/                  基线、契约、验证记录与第三方声明
CMakeLists.txt         构建入口
CMakePresets.json       MSVC / 可选 MinGW 配置
.github/workflows/     Windows 构建与测试
```

## 后续路线

1. P1 后续：只读模块枚举；当前已完成 Result/Error、进程 list/inspect 与版本化 JSON 契约。
2. P2：具备边界检查的 PE 解析、DLL Doctor、报告与单一权威 Skill。
3. P3/P4：本地 stdio MCP、实测的 Agent 适配和可回滚安装器。
4. P5：自有测试程序的 DLL 生命周期实验与结果对比。

旧 README 中的 `-t`、`-m`、`-d`、`-s` 参数没有实现，已移除。未来命令在实现并通过测试前不会作为可用功能展示。

## English quick start

This Windows C++ project includes the legacy GUI and a read-only process CLI
(`2.0.0-dev.1`). Install Visual Studio 2022 C++ tools and CMake 3.21+, plus Python
3.9+ and `python -m pip install -r tests/requirements.txt` for tests. Build with the
presets above, then run `build/msvc-x64/Debug/cli/dllinjector.exe process list --json`
or `process inspect --pid <PID> --json`. The CLI has no Python runtime dependency.
JSON uses schema 1.0, UTF-8 and explicit error/exit codes; `--output` refuses existing
files, `--verbose` logs to stderr, and `--timeout-ms` is a cooperative query deadline.
Module enumeration, PE Doctor and Agent adapters are not available yet. CTest uses
read-only queries, hidden startup checks, mocks and self-owned fixtures.

## 来源与许可

仓库目前没有项目级 `LICENSE` 文件，因此不再使用 MIT 徽章或声明整个仓库采用 MIT。第三方 ReflectiveLoader 文件中的原始版权与许可头完整保留，见 [第三方声明](docs/THIRD_PARTY_NOTICES.md)。其他代码的来源核实及项目级许可是后续发布前的待办。

问题与建议可通过 [GitHub Issues](https://github.com/som1ng/DLLInjector/issues) 反馈。
