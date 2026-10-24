# DLL Doctor 首批静态规则契约

目标版本 `2.0.0-dev.5`，JSON Schema `1.4`。旧 Schema 和固定样例保持不变。

本文记录 dev.5 基础规则。当前 dev.6 / Schema 1.5 增加显式目录候选检查；其可选参数、dependencies 字段与额外 finding 见 [依赖候选契约](DEPENDENCIES_CONTRACT.md)。

## 命令与边界

```powershell
dllinjector doctor <path> [--target-arch x86|x64] [--json] [--output <new-path>]
```

复用 `pe inspect` 的本地只读文件检查、资源上限和 `--timeout-ms`；不加载 DLL、修改进程、查询目标进程或自动提权。
`--target-arch` 仅用于 Doctor，表示用户声明的目标**进程**架构，并非操作系统或 CLI 自身的位数；不接受 `--pid`。
缺省时不猜目标架构。ARM/ARM64/ARM64EC/ARM64X、未知 Machine、Machine 与 PE 格式不一致以及声明了 CLR 目录的文件，不执行 x86/x64 不匹配规则。
CLR 目录存在只说明元数据有此声明，未验证 CLR 头部、CorFlags、AnyCPU 或托管加载行为。

Core 的 `DiagnosePe` 接收已有 `Result<PeInfo>`，不重新解析或访问文件。规则集中在 Core，CLI 负责格式化。

## 规则

| rule_id | severity | 触发与证据 | 下一步 |
| --- | --- | --- | --- |
| `MALFORMED_PE` | error | 解析器返回 `malformed_pe`；保留原始 stage/message/系统错误字段 | 重新获取或构建文件，检查解析失败位置 |
| `NOT_A_DLL` | error | COFF Characteristics 未设置 `IMAGE_FILE_DLL (0x2000)`；不能靠扩展名判断 | 提供正确 DLL 构建产物 |
| `ARCH_MISMATCH` | error | 普通原生 I386 + PE32 / AMD64 + PE32+ 与用户声明的目标 x86/x64 不同 | 对齐 DLL 与目标进程的构建架构 |
| `INSUFFICIENT_EVIDENCE` | warning / info | 缺少目标架构、架构不在规则范围，或依赖/运行时加载尚未验证 | 补充目标架构、检查 CLR/平台资料或目标环境加载日志 |

每条 finding 包含 `rule_id`、`severity`、`message`、非空 `evidence` 数组、`confidence`、`recommended_next_step`、非空 `documentation` 官方链接数组。
当前规则的 confidence 为 `high`，指对**已描述事实或证据缺口**的信心，不是对加载失败根因的确定性。
evidence 的来源是 `static_analysis` 或 `user_supplied_option`。架构比较必须同时包含头部证据与用户参数来源。
同一 `INSUFFICIENT_EVIDENCE` 可出现多次，分别说明不同证据缺口；消费者不能按 rule_id 去重丢弃证据。

成功解析时始终提示依赖与运行时行为未验证，包括导入库数量为零的情况：动态加载不必出现在静态导入表中。
本批不搜索依赖目录，不输出 `POSSIBLE_MISSING_DEPENDENCY`；不把 API Set 名称当作同名磁盘文件，也不猜测 Known DLL、SxS、应用搜索目录或延迟加载是否发生。

## 状态与退出码

| 情况 | status | operation_completed | outcome_verified | exit |
| --- | --- | --- | --- | --- |
| 诊断完成，无 error 级 finding（可有 warning/info） | success | true | false | 0 |
| 诊断完成，有 error 级 finding | success | true | false | 1 |
| 参数/文件读取/权限/超时/不支持/输出失败 | error | false | false | 沿用 2/3/4/5 |

`success` 仅表示诊断已完成；不得据此称 DLL 可加载。Doctor 的 `outcome_verified` 始终为 false。
调用者同时读取 status、findings 与退出码；`0` 只表示已实现规则未发现阻断问题。
`MALFORMED_PE` 是完成的诊断，`errors` 为空，`data.image` 为 null，`data.inspection_error` 保留解析错误。
文件不存在、AccessDenied、超时及 Unsupported 等属于操作失败，进入顶层 `errors`，`data=null`、`findings=[]`；保留真实 Win32/NTSTATUS，不编造权限根因。
输出失败优先于诊断退出码，不能留下一份伪成功 stdout 结果；输出文件继续要求不存在。
其他命令维持原有 status / execution / exit 语义，`pe inspect` 的 malformed 仍是 error / exit 1。

`data.scope=static_dll_preflight`，含 `target_architecture`（x86/x64/null）、`image`（原始 machine、characteristics、format、has_clr_header、普通/延迟导入库数量）及 `inspection_error`。
可读文本包含规则、严重程度、证据来源、置信度、下一步及文档链接；JSON 输出无 BOM UTF-8，stderr 只放操作错误或 verbose 日志。
Markdown/HTML 导出、依赖搜索和 Agent Skill 留待后续独立功能。

## 验证设计

- 原生 x86/x64 双向匹配与不匹配；无目标；未知/ARM64/EC/X Machine；CLR 声明；格式不一致；DLL 标记正反例。
- 损坏文件保留原始错误，不生成其他未经观察的规则；权限、超时、不支持等错误不伪装成文件损坏。
- 实际构建的 DLL/EXE、中文空格 emoji 路径、损坏样例、文件不存在、锁定文件、超时、输出不覆盖。
- 固定 JSON golden、Schema 正反例；新版本全部 CLI 命令验证，同时保留历史 Schema 验证。
- MSVC x64/x86 Debug/Release、MinGW x64；远端 Windows CI 与跨位数回归通过后合并。

## 官方依据

- [PE / COFF 格式、Machine、Characteristics、CLR 目录](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
- [32/64 位进程互操作](https://learn.microsoft.com/en-us/windows/win32/winprog64/process-interoperability)
- [Windows DLL 搜索顺序](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)
- [CorFlags 与托管程序集标志](https://learn.microsoft.com/en-us/dotnet/framework/tools/corflags-exe-corflags-conversion-tool)
