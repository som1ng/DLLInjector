# DLLInjector（二次开发中）

Windows C++ DLL 实验项目，正在向 **DLL / PE 诊断、只读进程分析与 Agent 本地工具**演进。

已具备 **只读进程/模块查询、PE 解析、DLL Doctor 静态规则与依赖候选检查、JSON / Markdown / HTML 报告及离线转换和 Windows 回归测试**，现有 Win32 GUI 保留。CLI 当前版本为 `2.0.0-dev.8`，尚未发布正式 V2.0；已提供只读诊断 Agent Skill，MCP 和一行安装器继续按路线推进。参见 [CLI 基础契约](docs/P1_CONTRACT.md)、[模块契约](docs/MODULES_CONTRACT.md)、[PE 头部契约](docs/PE_CONTRACT.md)、[PE 表解析契约](docs/PE_TABLES_CONTRACT.md)、[Doctor 契约](docs/DOCTOR_CONTRACT.md)、[依赖候选契约](docs/DEPENDENCIES_CONTRACT.md)、[直接报告](docs/REPORTS_CONTRACT.md)、[离线转换](docs/SAVED_REPORTS_CONTRACT.md)、[Skill 接入与兼容性](docs/AGENT_COMPATIBILITY.md) 和 [原始基线](docs/BASELINE.md)。

## 当前能力与验证边界

| 项目 | 状态 |
| --- | --- |
| Win32 GUI、进程列表、文件选择 | 可从源码构建；具备隐藏窗口启动/关闭测试 |
| 架构查询 | x86/x64/ARM64/Unknown 明确区分；查询失败携带错误与阶段；GUI 当前仅接受匹配的 x86/x64 目标 |
| 原有远程线程、APC、消息钩子、反射式与 DLL 部署方法 | 保留源代码和 GUI 入口；本轮仅验证构建，没有验证这些方法的运行效果 |
| Direct Syscall | GNU x64 实验源码保留；未纳入当前 CMake 构建；GUI 标注不可用 |
| CMake / CTest | MSVC x64/x86 Debug、Release；可选 MinGW-w64 x64 |
| Windows CI | 已配置构建、测试和审查产物上传；是否通过以实际 workflow 运行结果为准 |
| CLI / Core / JSON | `process list`、`process inspect`、`process modules`、帮助/版本、JSON、超时、输出文件、stderr 日志；GUI 与 CLI 复用进程查询 |
| 模块枚举 | 模块名、报告路径、基址、大小及文本限制标记；x64 CLI 可查询 x86/x64，x86 CLI 仅查询 x86，ARM64 目标暂不支持 |
| PE 静态检查 | `pe inspect <path>`：PE32/PE32+ 头部、节表、目录范围、导入/导出/延迟导入；支持名称/序号、导出别名与转发；不加载文件，不判断依赖是否存在、签名或可加载性 |
| DLL Doctor | `doctor <path> [--target-arch x86\|x64]`：损坏 PE、DLL 标记、原生 x86/x64 不匹配与证据不足；每条诊断含证据、置信度、下一步和官方资料 |
| 依赖候选检查 | Doctor 的 `--search-dir <path>` 可重复指定最多 16 个目录；报告普通/延迟导入的候选与观察错误；仅检查文件属性，不模拟加载器 |
| 可分享报告 | 诊断命令支持 `--format markdown` / `--format html`，展示证据、发现与完整 JSON 附录；HTML 单文件离线阅读 |
| 已保存报告转换 | `report export <saved.json> --format markdown|html` 按源版本校验并保留原报告，不重新查询其中的目标 |
| Agent 集成 | 单一权威 Windows DLL Doctor Skill 与手动接入说明；命令示例已回归验证，客户端自动触发尚未实测；MCP / 安装器待实现 |

GUI 启动不再自动申请 Debug Privilege。现有方法返回的 `true` 只用于显示“请求已提交，预期效果尚未验证”，不能证明 DLL 已完成初始化。旧反射式及部署代码的 PE 边界检查等已知问题记录在基线报告中，不能直接作为未来 Doctor 解析器使用。

## 构建与测试

需要 Windows、CMake 3.21+、Visual Studio 2022 的“使用 C++ 的桌面开发”工具与 Windows SDK。
测试还需要 Python 3.9+、`jsonschema` 和 `markdown-it-py`（由下方 requirements 安装）。首次 CMake 配置会下载并校验固定版本的 jsoncons，放在 build 缓存中；CLI 运行时不依赖 Python，也不下载依赖。

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
离线构建可预先解压经校验的 jsoncons 1.10.0 归档，并在配置时增加
`-DFETCHCONTENT_SOURCE_DIR_JSONCONS=C:/deps/jsoncons-1.10.0`；本地覆盖目录由调用方负责核对版本与完整性。
Schema 在构建时嵌入二进制，部署 CLI 无需旁置 Schema 或依赖源码。

## CLI 快速开始

```powershell
& .\build\msvc-x64\Debug\cli\dllinjector.exe --help
& .\build\msvc-x64\Debug\cli\dllinjector.exe --version --json
& .\build\msvc-x64\Debug\cli\dllinjector.exe process list --json
# 查询当前 PowerShell 的 PID；不修改目标进程，不自动提权。
& .\build\msvc-x64\Debug\cli\dllinjector.exe process inspect --pid $PID --json
& .\build\msvc-x64\Debug\cli\dllinjector.exe process modules --pid $PID --json
& .\build\msvc-x64\Debug\cli\dllinjector.exe process inspect --pid $PID --json --timeout-ms 5000 --output .\process-report.json
# 读取本次构建的测试 DLL；不会加载或执行该文件。
& .\build\msvc-x64\Debug\cli\dllinjector.exe pe inspect .\build\msvc-x64\Debug\noop_fixture.dll --json
# 为打算加载到 x64 进程的 DLL 生成诊断；目标架构是显式声明，不是运行时观察。
& .\build\msvc-x64\Debug\cli\dllinjector.exe doctor .\build\msvc-x64\Debug\noop_fixture.dll --target-arch x64 --json
# 只在显式目录中检查依赖候选，可重复 --search-dir；结果不能证明实际加载成功。
& .\build\msvc-x64\Debug\cli\dllinjector.exe doctor .\build\msvc-x64\Debug\noop_fixture.dll --search-dir .\build\msvc-x64\Debug --json
# 导出可供人工阅读的报告；--format 与 --json 互斥，输出文件必须不存在。
& .\build\msvc-x64\Debug\cli\dllinjector.exe doctor .\build\msvc-x64\Debug\noop_fixture.dll --format html --output .\doctor-report.html
& .\build\msvc-x64\Debug\cli\dllinjector.exe process modules --pid $PID --format markdown --output .\modules-report.md
# 将已经保存的 JSON 转成 HTML；不访问报告中记录的文件或进程。
& .\build\msvc-x64\Debug\cli\dllinjector.exe report export .\process-report.json --format html --output .\process-report.html
```

stdout 为结果，stderr 为错误与 `--verbose` 日志。重定向及输出文件使用无 BOM UTF-8；
`--output` 要求文件不存在，防止覆盖已有报告。当前新诊断/错误 JSON Schema 为 [1.6](schemas/cli-v1.6.json)；
[1.0](schemas/cli-v1.json) / [1.1](schemas/cli-v1.1.json) / [1.2](schemas/cli-v1.2.json) / [1.3](schemas/cli-v1.3.json) / [1.4](schemas/cli-v1.4.json) / [1.5](schemas/cli-v1.5.json) 保留用于旧版结果。离线转换保留源版本，调用方按 `schema_version` 选择验证器。Doctor 的 `dependencies` 在未请求候选检查时为 null。
创建时间以十进制 FILETIME **字符串**提供，避免 JavaScript 数字精度丢失；列表中无法读取的创建时间为 `null`，原因在该条目的 `errors` 中。

模块基址使用十六进制字符串。模块列表是 Toolhelp 加载器快照，不包含按数据文件方式加载的模块，也不能证明 DLL 初始化成功。
Windows 的模块名称/路径有固定长度上限，且本机实测 W API 也可能把 emoji 替换为 `?`：
`name_may_be_truncated`、`path_may_be_truncated`、`text_may_be_lossy` 明确标记这些限制。
标记为可能失真的路径不应直接用于文件操作；未触发标记也不构成路径身份验证。

PE 检查报告 `scope: headers_sections_imports_exports_delay_imports`，证据来源为 `static_analysis`。Machine 是原始数值，
ImageBase / RVA 用十六进制字符串表示；证书目录按文件偏移处理。最大文件 256 MiB，最多 96 个节、16 个目录；
超过支持范围会明确报错。文件偏移为 `null` 表示未确定完整的单一区域文件映射，不能据此推断目录内容。
导入包含 DLL 名称和按名称/序号引用的符号；导出包含别名、序号及未跟随解析的转发字符串。
延迟导入支持现代 RVA 描述符。缺失原始查找表的绑定 IAT、旧 VA 延迟格式和不支持的名称编码会明确报错。
库数、符号数和文本量均设上限，详见表解析契约。解析成功不能证明 DLL 能加载、依赖存在或签名可信。

Doctor 的 `status=success` 表示诊断完成，有 error 级 finding 时仍返回 `1`；`outcome_verified` 始终为 false。
没有阻断发现也不能证明可加载。托管/混合模式、ARM 等未支持的架构判断会明确提示证据不足；
传入 `--search-dir` 后，只观察这些目录中的直接导入/延迟导入候选，不自动加入系统目录、PATH 或 DLL 所在目录。
未找到时给出低置信度的 `POSSIBLE_MISSING_DEPENDENCY` 警告；API Set、不可支持的名称及无法观察的候选明确区分，访问失败不会当成缺失。
Known DLL、SxS、动态加载等仍可能提供依赖；候选文件不做内容验证。结果来自 CLI 自身的文件系统视图，受 WOW64 影响。
详见 [Doctor 契约](docs/DOCTOR_CONTRACT.md) 和 [依赖候选契约](docs/DEPENDENCIES_CONTRACT.md)。

Markdown / HTML 与 JSON 使用同一次查询结果，保留诊断退出码，附录包含完整 JSON。
HTML 不需要联网或脚本；两种格式将名称和路径作为文字展示，避免输入被解释成活动标记。
报告包含本地路径、进程信息，分享前请自行检查。`report export` 支持 1.0–1.6 的已保存报告，最多 16 MiB，
按内嵌 Schema 校验并保留来源字段；未知版本、重复键、非法编码或超限输入会明确失败。
转换成功退出 0，即使源报告记录了失败；不会把源诊断的 outcome_verified 改成 true。更多限制见离线转换契约。

建议将本地诊断结果放在 `reports/`，预览/临时文件放 `artifacts/` 或 `build/`，这些目录均已忽略。
源码、文档、Schema 和 `tests/fixtures/` 的固定样例仍纳入版本控制；生成日志、依赖下载和 Python 缓存不提交。
`bin/` 与 `dll_test/` 下的旧 EXE / DLL 不再跟踪；本地已有文件可保留。
请使用上述 CMake 构建产物，或从成功的 Windows CI 运行下载 Artifacts；旧二进制不代表当前源码版本。

退出码：`0` 命令完成且无阻断发现，`1` PE 内容损坏或 Doctor 阻断发现，`2` 参数错误，`3` 目标不可访问，
`4` 内部/不支持/输出失败，`5` 查询超时。`--timeout-ms 0` 会立即返回查询超时。
超时在 Win32 调用之间检查，不能打断正在执行的系统调用或阻塞的输出；需要严格墙钟限制的调用方也应设置子进程超时。

## Agent Skill

[Windows DLL Doctor](integrations/skills/windows-dll-doctor/SKILL.md) 将现有 CLI 组织为只读诊断工作流，
用于 DLL 加载失败、架构/依赖排查、模块查询和报告解释。它保留证据与不确定性，不把静态检查当作加载成功。

构建 CLI 后，将整个 Skill 目录复制到 Agent 支持的项目目录，并提供 CLI 绝对路径或
`DLLINJECTOR_CLI` 环境变量。具体步骤、官方目录与版本核验见 [接入说明](docs/AGENT_COMPATIBILITY.md)。
此轮提供可移植指令包与 22 条评测规格，尚未验证各客户端的自动触发和模型回答质量。
已用 Codex、OpenCode 的实际发现接口和 Pi 的资源加载器验证项目 Skill 发现及负例；
范围和复现命令见接入说明。Claude Code 的发现尚未实测。

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
- Markdown / HTML 的 12 组固定报告、CommonMark / HTML 解析、恶意标记转义、完整 JSON 还原、诊断失败与保存失败语义。
- 模块快照的重试、超时、遍历错误、文本边界；自有宿主加载无副作用 DLL 后比对基址与模块元数据。
- 本机与 Windows CI 的 x64 → x86 枚举，以及 x86 → x64 的明确拒绝。
- PE32/PE32+、所有截断前缀、偏移溢出、节区重叠、零填充、证书目录和固定种子的变异语料。
- 真实编译产物的 PE 检查、损坏内容退出码、Unicode 路径、文件锁、大小上限及输入不被覆盖。
- 导入/导出/延迟导入内容、跨节区元数据、边界计数、超长字符串及变异输入；MSVC 构建真实延迟加载 DLL，仅静态读取。
- GUI 隐藏启动/关闭且不自动启用 Debug Privilege、原有控制台 Demo 启动。
- 测试程序在自身进程加载新建的无副作用 DLL，并检查显式导出返回值。

CTest 不运行对第三方进程的注入或目录部署，也不运行仓库内已有的反射式二进制。`demo_dll` 是保留的旧弹窗样例，只参与构建；它与新建的无副作用 `noop_fixture` 不同。

## 目录

```text
src/                   原 Win32 GUI、工具函数与方法实现
include/dllinjector/    UI 无关的 Result/Error、进程/PE 类型与公共接口
core/                  进程查询、PE 静态解析、RAII、Unicode 转换
apps/cli/              参数解析、文本/JSON/Markdown/HTML 输出与命令行入口
schemas/               版本化 JSON Schema
dll_test/              原 Demo 与第三方反射式样例
tests/                 Win32 回归测试、启动测试与无副作用 fixture
integrations/skills/   权威 Agent Skill、相对引用与只读命令示例
docs/                  基线、契约、验证记录与第三方声明
CMakeLists.txt         构建入口
CMakePresets.json       MSVC / 可选 MinGW 配置
cmake/                 固定构建依赖与 Schema 嵌入配置
.github/workflows/     Windows 构建与测试
```

## 后续路线

1. P2：Doctor、报告和权威 Skill 指令包已实现；补充真实客户端触发与回答评测。
2. P3/P4：本地 stdio MCP、实测的 Agent 适配和可回滚安装器。
3. P5：自有测试程序的 DLL 生命周期实验与结果对比。

旧 README 中的 `-t`、`-m`、`-d`、`-s` 参数没有实现，已移除。未来命令在实现并通过测试前不会作为可用功能展示。

## English quick start

This Windows C++ project includes the legacy GUI and a read-only process/PE CLI
(`2.0.0-dev.8`). Install Visual Studio 2022 C++ tools and CMake 3.21+, plus Python
3.9+ and `python -m pip install -r tests/requirements.txt` for tests. Build with the
presets above, then run `build/msvc-x64/Debug/cli/dllinjector.exe process list --json`
or `process inspect --pid <PID> --json` / `process modules --pid <PID> --json`.
Use `pe inspect <file> --json` for PE32/PE32+ headers, sections, imports, exports
and delay imports. Names/ordinals, aliases and forwarders are reported without
loading the input. Dependencies are not resolved; signatures and Windows
loadability are not verified. The file limit is 256 MiB.
The CLI has no Python runtime dependency.
New diagnostic/error JSON uses schema 1.6, UTF-8 and explicit error/exit codes; `--output` refuses existing
files, `--verbose` logs to stderr, and `--timeout-ms` is a cooperative query deadline.
Module enumeration uses a loader snapshot, with bounded retries and explicit bitness
errors. Addresses are hex strings; paths/names may be truncated or lossy in the OS
snapshot and carry corresponding flags. Use `doctor <file> --target-arch x64 --json`
for static findings and `--search-dir <directory>` for explicit dependency candidate observations.
Candidates do not prove Windows loader resolution; Doctor never verifies runtime loading.
Diagnostic commands also accept `--format markdown` or `--format html` instead of `--json`.
For example: `doctor <file> --format html --output report.html`. Reports include a complete
JSON appendix; HTML works offline without scripts. `report export saved.json --format html`
validates schemas 1.0–1.6 and preserves the source snapshot without inspecting its targets.
Export success exits 0 independently of the source diagnosis. CMake fetches a pinned,
hash-checked jsoncons archive on first configuration; the CLI needs no external schemas
or network at runtime. A portable [Windows DLL Doctor Skill](integrations/skills/windows-dll-doctor/SKILL.md)
is available with tested CLI examples. Project discovery is checked with Codex, OpenCode and
Pi's resource loader in the documented versions; model behavior and Claude discovery remain unverified.
MCP and installers remain planned. CTest uses read-only queries,
hidden startup checks, mocks and self-owned fixtures.

## 来源与许可

仓库目前没有项目级 `LICENSE` 文件，因此不再使用 MIT 徽章或声明整个仓库采用 MIT。第三方 ReflectiveLoader 文件中的原始版权与许可头完整保留，见 [第三方声明](docs/THIRD_PARTY_NOTICES.md)。其他代码的来源核实及项目级许可是后续发布前的待办。

问题与建议可通过 [GitHub Issues](https://github.com/som1ng/DLLInjector/issues) 反馈。
