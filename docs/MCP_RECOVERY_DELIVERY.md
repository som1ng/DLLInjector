# MCP 升级进程中断恢复

日期：2026-10-28。安装器 `0.1.0-dev.12`，Rondo npm 包 `0.1.0-dev.6`。
延续 [Skill 恢复](SKILL_RECOVERY_DELIVERY.md)，补齐 MCP 单目标升级与内部回滚步骤的进程中断恢复。
本轮不改 GUI、原生 CLI 或 MCP 工具协议。

## 使用

```powershell
rondo recover --integration mcp --agent codex --scope project --project 'D:\work\my-app' --dry-run
rondo recover --integration mcp --agent codex --scope project --project 'D:\work\my-app'
rondo recover --integration mcp --agent claude --scope user
```

支持 Codex / Claude Code / OpenCode；user 范围沿用现有路径规则，包括自定义 CODEX_HOME。
要求显式 agent，不接受 auto/all、CLI、manifest、client、server、allow-root、allow-processes 或 verify。
恢复不运行旧 CLI、客户端或 MCP 服务。返回 `runtime_verification: not_run`、`client_discovery: not_run`。

| action | 行为 |
| --- | --- |
| `absent` | 当前目标没有锁/记录；不扫描删除其他文件 |
| `preview` | 检查文件和记录，输出恢复方向、路径及已识别的临时文件；不写文件、不启动辅助进程 |
| `restored` | 未提交步骤恢复到开始前的配置及收据字节 |
| `completed` | 完整提交标记且文件一致，保留已提交结果并清理记录 |

## 保存什么、何时拒绝

- 记录仅含受管 MCP 条目的前后文本、安装收据、整份配置的 SHA-256、PID 和操作标识。
  不保存配置中其他服务、模型设置或凭据；恢复前后条目外的字节必须相同，否则升级在首次写入前拒绝。
- 收据允许已知字段，未知扩展字段不会被复制进恢复记录。记录上限 512 KiB，单份收据上限 64 KiB。
  升级后的配置须在现有读取上限 1 MiB 内，超限在首次写入前拒绝，避免留下无法读回的状态。
  这些记录仍含本机路径，应保留在可信本地目录；摘要用于一致性检查，不是来源签名。
- 完整记录先写入并同步，再通过同目录硬链接排他发布到 `.dll-doctor/mcp-<agent>.lock`。
  取得锁并核对原文件后同步 started 标记；更新配置、收据并校验运行文件后同步 committed 标记。
  标记不完整时恢复该步骤前状态；未写 started 的记录不能撤回后来出现的替换配置。
- 恢复要求配置的完整摘要和收据字节均与已记录的前/后状态匹配；之后编辑的无关设置也会阻止恢复。
  不尝试合并用户的新设置，不覆盖缺失/未知内容或链接目标。
- PID 仍存在、被复用或无法确认退出时拒绝接管。实际恢复复用 Windows PowerShell 的排他文件句柄，
  防止两个恢复进程同时写入；父进程终止后管道 EOF 释放句柄，可再次运行恢复。
- 临时配置文件仍含完整配置，供原子替换使用，不作为持久恢复快照。
  只清理记录标识对应且内容与已知前/后字节（或其前缀）匹配的临时文件；记录别名还必须是同一 inode 的硬链接。
  不扫描或递归删除其他残留。`.dll-doctor/` 和配置临时文件已被 Git 忽略。
- 仅凭标准输出不能证明客户端发现已恢复；需要时另行执行 verify 或 upgrade 绑定可信运行文件。

## 限制

仅覆盖新版 MCP 升级及内部回滚的单目标步骤，不处理安装/卸载、旧空锁、损坏记录或整批事务恢复。
如果中断发生在批量回滚，恢复到的是该回滚步骤开始前的状态；其他已完成目标不一起回退。
不恢复旧运行文件，也不保证跨文件原子可见性。真实断电、系统崩溃及存储损坏未验收。

需要支持硬链接的本地文件系统，以及 Windows 自带 PowerShell；不会改变执行策略或系统环境变量。
`RECOVERY_ACTIVE` / `RECOVERY_BUSY` 表示原进程或互斥锁不可接管；
`RECOVERY_JOURNAL` / `RECOVERY_CONFLICT` 表示记录或现状无法确认；
`MCP_RECOVERY_LAYOUT` / `MCP_RECOVERY_RECEIPT` 表示不能安全生成受限记录。失败时保留现场。

## 验证

- `npm test --prefix integrations/installer`：196 项通过、0 跳过，提供本地 CLI/MCP 路径。
- 新增 23 项 MCP 恢复测试：真实终止子进程，覆盖三客户端/两种 scope，准备、临时写入、配置/收据更新和提交阶段；
  还包括记录不含无关凭据、活进程拒绝、并发及重复中断、配置改动保护、损坏记录、临时文件/链接、
  自定义用户目录与 scope 隔离、内部批量回滚、同路径运行文件升级、配置超限及 CLI 参数验证。
- `npm test --prefix integrations/npm`：8 项启动器测试通过，包括未提供打包运行文件时转交 MCP 恢复命令。
- 组装 `@som1ng/rondo@0.1.0-dev.6` 后，`tests/npm_package_tests.py` 的 2 项归档/离线全局安装测试通过。
  覆盖打包后的 MCP 升级中断、恢复、卸载，并回归 Skill、原生 x86/x64 CLI 和 MCP 协议调用。
  本地传入 Codex 0.164.0、Claude Code 2.1.310、OpenCode 1.19.4 原生路径，执行版本探测和配置流程；
  协议调用由测试 SDK 直接完成，不代表真实宿主自动发现或模型行为验收。
  CI 未安装原生客户端时仅替换客户端版本探测，仍使用实际打包的 CLI 和 MCP 服务。
- 98 个相对 Markdown 链接可解析，归档内 13 个相关源码文件与工作目录一致，`git diff --check` 通过。
- 截断记录/临时文件用受控 fixture 模拟；不宣称这是物理断电实测。
- 测试均使用隔离临时目录，不修改真实客户端配置，不调用模型 API。
