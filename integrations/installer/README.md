# 本地 Skill / MCP 安装器

Windows 源码运行版本 `0.1.0-dev.6`，支持 Codex、Claude Code、OpenCode。
准备 Node.js 22.19+ 和可信的 DLLInjector CLI `2.0.0-dev.8` 或 `dev.9`（Schema 1.6）。
默认 Skill 模式无需安装 npm 依赖。包尚未发布；当前从本仓库运行。
项目级 MCP 配置使用 `--integration mcp`，需要安装本目录依赖，见下文。

## 安装

在仓库根目录运行，替换实际项目和 CLI 绝对路径：

```powershell
node integrations/installer/src/index.mjs setup --agent codex --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --dry-run
node integrations/installer/src/index.mjs setup --agent codex --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --verify
```

`--dry-run` 预览目标与文件清单，不写文件、不启动 CLI。正式安装检查 CLI 版本，
复制完整 Skill，并保存本机 CLI 路径和文件校验信息。CLI 仍位于原位置，请保留它。
诊断时，Skill 优先使用用户明确提供的路径、`DLLINJECTOR_CLI`，然后读取安装记录中的路径。
安装记录是数据，仍须确认 CLI 来源；安装成功不表示模型已发现或执行 Skill。

若使用带 `manifest.json` 的 [CLI 分发归档](../../docs/CLI_DISTRIBUTION.md)，正式安装时附加
`--manifest 'D:\tools\manifest.json'`。安装器在启动 EXE 前检查文件大小、SHA-256 和 PE 架构，
启动后核对实际版本与清单一致。清单不符会失败，不能自动降级为无清单安装。
`--dry-run` 也会执行文件校验，但不启动 CLI。普通源码构建仍可省略此选项。
哈希用于比对内容，不能替代可信来源或数字签名。

| 选择 | 项目安装目录 | 用户安装目录 |
| --- | --- | --- |
| `--agent codex` / `opencode` | `<project>/.agents/skills/windows-dll-doctor` | `<home>/.agents/skills/windows-dll-doctor` |
| `--agent claude` | `<project>/.claude/skills/windows-dll-doctor` | `<home>/.claude/skills/windows-dll-doctor` |

用户级改用 `--scope user`，同时省略 `--project`。项目目录必须已存在；省略 `--project` 时使用当前目录。
客户端可以交叉读取兼容目录，**Codex/OpenCode 共用一份安装，卸载会影响读取它的所有客户端**。
同一目录不应反复安装多份同名 Skill。Claude 自定义 `CLAUDE_CONFIG_DIR` 的用户级安装暂不支持。
Pi 请使用 [原生 Pi 包](../pi/README.md) 的 `pi install` / `pi remove`。

在目标项目中启动或重新加载客户端，按客户端自己的信任和权限规则使用 Skill。
`--verify` 检查安装文件与 CLI 的版本、内容，**不等于客户端发现或模型触发验证**。
Codex/OpenCode 的真实项目发现另有可复现测试；Claude 和用户级发现尚未实测。

## 探测与选择客户端

可以先查看本机客户端候选路径，再选择一个安装目标：

```powershell
node integrations/installer/src/index.mjs list-agents
# 终端中展示候选路径，输入编号确认一个目标；回车取消。
node integrations/installer/src/index.mjs setup --agent auto --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --verify
# 脚本或 CI 必须显式选择。多个同名候选时还需 --client <list-agents 中的绝对 EXE 路径>。
node integrations/installer/src/index.mjs setup --agent auto --select claude --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --dry-run
```

`list-agents` 只读 PATH、用户 `.local/bin` 与 `%APPDATA%/npm` 中的已知原生 EXE 位置，
支持当前 npm 包的固定 Windows 布局，不执行 `.cmd` / `.ps1`，不导入包代码或扫描整个磁盘。
结果是**候选文件**，不是来源可信、版本兼容或客户端成功启动的证明。
同名但不同路径会分别列出；实际来源和版本应由用户确认，MCP 正式安装还会执行已有版本校验。
Node 架构随检测结果输出，不等同于操作系统 CPU 架构或二进制兼容性验证。
未发现不代表未安装：自定义包装器、未覆盖的包管理器布局可以继续用显式 `--agent` 入口。
仅有一个候选也不会自动确认；非终端输入不接受管道传入编号，缺少 `--select` 时明确失败。

`--agent auto --dry-run` 不提问、不执行程序、不写文件；未选择时只列出候选，
加 `--select` 后才预览该客户端的实际安装目标。`--select` 仅适用于 auto setup，
verify/uninstall 仍须显式指定原客户端，避免客户端安装状态变化导致误选卸载目标。
输出中的 `agent_detection` 描述安装器选中了哪个客户端；
`client_discovery` 描述客户端是否发现了安装资源，二者是不同检查。

MCP 也支持 auto；安装器把选中的原生路径用于版本校验，其他 MCP 参数仍需明确提供：

```powershell
node integrations/installer/src/index.mjs setup --integration mcp --agent auto --select claude --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --server 'D:\src\DLLInjector\integrations\mcp\dist\index.js' --allow-root 'D:\work\my-app' --dry-run
```

需要 MCP 依赖时按“项目级 MCP 配置”说明准备。auto 一次只选择一个客户端；
Pi 仍通过自己的原生包安装。探测范围与实际验证见 [客户端选择交付记录](../../docs/AGENT_SELECTION_DELIVERY.md)。

## 批量安装 Skill

`setup --agent all` 明确选择 Codex、Claude Code 和 OpenCode 的 Skill 目录，不要求这些客户端
已被探测到。支持项目和用户两种范围；Codex/OpenCode 共用的目录只安装一次，结果列出两个
实际目标及其覆盖的客户端。Pi 与 MCP 配置继续使用各自入口。

```powershell
node integrations/installer/src/index.mjs setup --agent all --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --dry-run
node integrations/installer/src/index.mjs setup --agent all --scope project --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' --verify
# 校验和卸载仍明确指定客户端；Codex/OpenCode 共用目录只选其中一个。
node integrations/installer/src/index.mjs verify --agent codex --scope project --project 'D:\work\my-app'
node integrations/installer/src/index.mjs verify --agent claude --scope project --project 'D:\work\my-app'
```

先预检全部目标，再依次安装。后续目标失败会撤回本次新增安装，原有相同安装保持不变；
相同 Skill 和 CLI 绑定可以复用早期安装器的有效收据，不因安装器版本号变化而重写文件。
用户并发修改或锁/权限异常导致回滚无法完成时，会返回 `ROLLBACK_INCOMPLETE` 和目标路径，
请保留现场检查。批量操作不保证跨目录原子可见性或强制结束后的自动恢复。
卸载使用上述两个显式 agent，将 `verify` 换为 `uninstall`；不支持 `uninstall --agent all`。
契约及实测边界见 [批量安装契约](../../docs/BATCH_SKILL_CONTRACT.md) 和 [交付记录](../../docs/BATCH_SKILL_DELIVERY.md)。

## 校验、更新与卸载

```powershell
node integrations/installer/src/index.mjs verify --agent codex --scope project --project 'D:\work\my-app'
node integrations/installer/src/index.mjs uninstall --agent codex --scope project --project 'D:\work\my-app' --dry-run
node integrations/installer/src/index.mjs uninstall --agent codex --scope project --project 'D:\work\my-app'
```

重复安装相同内容和 CLI 时返回 `unchanged`。CLI 字节、路径或 Skill 版本变化时，需要先卸载再安装；
不会静默覆盖。卸载不执行或删除 CLI，保留其他 Skill、客户端配置和报告。
已存在的同名手动安装、用户修改、额外文件或损坏的安装记录会阻止覆盖/卸载，请先自行备份和比较。
卸载后空的 `.agents/skills` / `.claude/skills` 父目录可能保留。

安装失败会清理本次新增内容，卸载中途失败会恢复已删除文件。
并发编辑或权限问题导致恢复不完整时返回 `ROLLBACK_INCOMPLETE`，需要保留现场并检查输出路径。
进程被强制终止或机器断电不在自动回滚保证范围内；残留的 `.dll-doctor-installer.lock` 会阻止后续写入。
此时先确认没有安装器运行，再检查同目录下 `.dll-doctor-stage-*` 和目标目录，备份后处理残留。
不支持通过符号链接/junction 写入安装目录；不自动删除锁或未知目录。

输出为 JSON，成功退出 `0`，失败退出 `1`。文件清单包含本机路径；安装目录与安装记录应保留在本机，
请在目标项目的 Git 忽略规则中排除对应安装目录。本仓库已配置这些忽略规则。

## 项目级 MCP 配置

支持 Codex **0.164.0**、Claude Code **2.1.310**、OpenCode **1.19.4 (V1)**；
其他版本会明确拒绝，尚未验证其配置兼容性。准备 CLI **dev.9 / Schema 1.6**、
已构建的 [MCP 服务](../mcp/README.md) 和客户端原生 `.exe`（不接受 `.cmd` / `.ps1` 包装器）。
安装器不下载或复制这些运行文件，移动/更新后需明确卸载再安装。

在仓库根目录运行，替换绝对路径；`--allow-root` 可重复指定：

```powershell
npm.cmd ci --prefix integrations/installer --ignore-scripts --no-audit --no-fund
node integrations/installer/src/index.mjs setup --integration mcp --agent codex --scope project --project 'D:\work\my-app' --client 'D:\tools\codex.exe' --cli 'D:\tools\dllinjector.exe' --server 'D:\src\DLLInjector\integrations\mcp\dist\index.js' --allow-root 'D:\work\my-app' --dry-run
# 确认预览后，将 --dry-run 换成 --verify 执行安装。
node integrations/installer/src/index.mjs verify --integration mcp --agent codex --scope project --project 'D:\work\my-app'
node integrations/installer/src/index.mjs uninstall --integration mcp --agent codex --scope project --project 'D:\work\my-app' --dry-run
node integrations/installer/src/index.mjs uninstall --integration mcp --agent codex --scope project --project 'D:\work\my-app'
```

| 客户端 | 项目配置 |
| --- | --- |
| Codex | `.codex/config.toml` |
| Claude Code | `.mcp.json` |
| OpenCode | 已有 `opencode.jsonc` 或 `opencode.json`；无配置时创建 `.json`；同时存在则拒绝 |

仅管理 `dll_doctor` 条目，保留其他设置和注释。已有同名手动配置、被修改的托管条目、
重复键、损坏配置或链接路径会阻止写入。相同安装返回 `unchanged`；更换路径或授权目录需先卸载。
若 JSONC 编辑会删除相邻注释，也会保留原文件并报错，请先调整注释位置。
默认只开放显式授权的目录；需要查询进程时在 setup 中明确加 `--allow-processes`。
支持可选的 CLI `--manifest` 校验。`--dry-run` 不启动客户端、CLI 或 MCP 服务。

正式 setup 在写入前检查客户端/CLI 版本，并启动 MCP 服务验证握手和七个只读工具；
`--verify` 额外在写入后复查。独立 verify 也检查记录的 CLI/入口脚本哈希和 Node 路径。
**验证成功不代表客户端自动发现、信任该项目，或模型自主选择了工具。**
在目标客户端中按其正常流程信任项目、批准 MCP，再使用工具；安装器不修改信任或批准设置。
实际客户端验证的加载方式和范围见 [交付记录](../../docs/MCP_INSTALLER_DELIVERY.md)。

本机记录位于 `.dll-doctor/mcp-<agent>.json`，只保存本安装的设置、路径和校验信息，
不复制其他配置或凭据。卸载保留安装后的其他配置修改；例如 OpenCode 自动新增的 `$schema`。
未被修改的新建配置才会整体删除。请在目标项目忽略 `.dll-doctor/`，
并根据项目需要忽略含本机绝对路径的客户端配置；安装器不会改动目标项目的 `.gitignore`。

可捕获的写入失败会按原始字节回滚，遇到并发修改则保留现场并报告 `ROLLBACK_INCOMPLETE`。
强制结束/断电恢复不在本轮范围内；残留 `.dll-doctor/mcp-<agent>.lock` 时，
先确认没有安装器运行，再备份配置与记录并检查残留，不自动抢锁。
MCP 暂只支持 `--scope project`；详细边界见 [契约](../../docs/MCP_INSTALLER_CONTRACT.md)。

## 批量项目 MCP 配置

准备好上述 MCP 依赖与运行文件后，可用 `--agent all` 配置三个客户端。
三个客户端原生路径必须分别明确提供，不使用单客户端的 `--client` 或 auto 的 `--select`：

```powershell
node integrations/installer/src/index.mjs setup --integration mcp --agent all --scope project `
  --project 'D:\work\my-app' --cli 'D:\tools\dllinjector.exe' `
  --server 'D:\src\Rondo\integrations\mcp\dist\index.js' --allow-root 'D:\work\my-app' `
  --codex-client 'D:\tools\codex.exe' --claude-client 'D:\tools\claude.exe' `
  --opencode-client 'D:\tools\opencode.exe' --dry-run
# 确认三个目标后，把 --dry-run 换成 --verify 执行安装。
```

先预检三份配置，正式安装再逐个验证客户端版本与 MCP 服务。每项结果列出配置和收据路径。
失败时仅回滚本次新增的条目，保留已有相同配置；原文件未被后来编辑时恢复原始字节，
有后续无关设置时只删除本工具条目。回滚还会在锁内比对收据，保护其他操作替换的有效安装。
无法完整恢复会返回 `ROLLBACK_INCOMPLETE`，请检查输出路径并保留现场。
原始配置快照仅保存在操作内存中，不写入日志、输出或安装记录。

`verify` / `uninstall` 继续逐个指定 `--agent codex|claude|opencode`；不支持批量删除。
本功能只配置项目级 MCP，不修改客户端的信任或批准设置，也不保证强制结束后的恢复。
行为与实测边界见 [批量 MCP 契约](../../docs/BATCH_MCP_CONTRACT.md) 和 [交付记录](../../docs/BATCH_MCP_DELIVERY.md)。

## 开发验证与范围

在本目录完成 `npm.cmd ci --ignore-scripts --no-audit --no-fund` 后执行 `npm.cmd test`。
设置 `DLL_INSTALLER_TEST_CLI` 为现有 CLI 的绝对路径，会额外执行真实入口安装、
校验及 PE 查询；未设置时该项明确标为跳过。真实客户端发现可运行仓库的
`python tests/installer_hosts.py --cli <cli.exe> --codex <codex.exe> --opencode <opencode.exe>`。
后者使用临时项目，客户端必须已安装且参数为原生 EXE，不调用模型 API。

同时设置 `DLL_INSTALLER_TEST_MCP` 为构建后的 MCP `index.js` 绝对路径，可运行真实 MCP 握手测试。
生成配置的真实宿主测试见 `tests/mcp_installer_hosts.py --help`，使用隔离临时项目和本地固定模型响应，
不调用付费模型 API；这是显式配置加载验证，不是自动项目发现测试。

两个宿主脚本增加 `--auto` 可验证“候选探测 → 显式选择 → 安装 → 客户端检查”的完整路径。
测试所提供的客户端 EXE 必须也能被 `list-agents` 检出。
`tests/installer_hosts.py --all` 验证批量安装后的单份发现、重复安装和两个目录卸载后不可见，
与 `--auto` 互斥；不调用模型 API。
`tests/mcp_installer_hosts.py --all` 需要同时提供三个客户端路径，检查批量配置后的真实宿主
工具发现/调用、重复安装及显式卸载；使用本地固定模型响应，与 `--auto` 互斥。

CI 已提供 CLI 下载归档、校验文件和安装前清单验证；自动下载、用户级 MCP、
公开 npm 分发和升级恢复仍待实现。目前公开 Release 只有旧 GUI 版本，
不能作为现有 CLI 的自动下载来源；先继续使用可信 CI 归档或源码构建。
