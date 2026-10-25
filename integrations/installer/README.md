# 本地 Skill 安装器

Windows 源码运行版本 `0.1.0-dev.2`，支持 Codex、Claude Code、OpenCode。
准备 Node.js 22.19+ 和可信的 DLLInjector CLI `2.0.0-dev.8` 或 `dev.9`（Schema 1.6）。
没有 npm 依赖，不需要 `npm install`。包尚未发布；当前从本仓库运行。

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
不应为同一个项目反复安装多份同名 Skill。Claude 自定义 `CLAUDE_CONFIG_DIR` 的用户级安装暂不支持。
Pi 请使用 [原生 Pi 包](../pi/README.md) 的 `pi install` / `pi remove`。

在目标项目中启动或重新加载客户端，按客户端自己的信任和权限规则使用 Skill。
`--verify` 检查安装文件与 CLI 的版本、内容，**不等于客户端发现或模型触发验证**。
Codex/OpenCode 的真实项目发现另有可复现测试；Claude 和用户级发现尚未实测。

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

## 开发验证与范围

在本目录执行 `npm test`。设置 `DLL_INSTALLER_TEST_CLI` 为现有 CLI 的绝对路径，会额外执行真实入口安装、
校验及 PE 查询；未设置时该项明确标为跳过。真实客户端发现可运行仓库的
`python tests/installer_hosts.py --cli <cli.exe> --codex <codex.exe> --opencode <opencode.exe>`。
后者使用临时项目，客户端必须已安装且参数为原生 EXE，不调用模型 API。

本轮不修改 MCP/客户端主配置，不探测并选择所有客户端，不下载 CLI，也不提供公开 npm 命令。
CI 已提供 CLI 下载归档、校验文件和安装前清单验证；自动下载、`auto/all` 交互选择、
统一 Pi/MCP 配置和升级恢复将分批推进。
