# Skill 升级进程中断恢复

日期：2026-10-28。安装器 `0.1.0-dev.11`，Rondo npm 包 `0.1.0-dev.5`。
本轮只覆盖受管 Skill 升级和内部回滚的单目标步骤，不修改 GUI、原生 CLI 或 MCP 配置。

## 命令

```powershell
rondo recover --agent codex --scope project --project 'D:\work\my-app' --dry-run
rondo recover --agent codex --scope project --project 'D:\work\my-app'
```

支持 `codex` / `claude` / `opencode` 及 project/user。Codex/OpenCode 共用一个 Skill 目标。
不接受 auto/all、MCP、CLI/manifest 等运行参数；恢复不执行 CLI、客户端或 MCP 服务，也不验证旧运行文件可用。
预览只检查记录和文件，不写入文件、不启动辅助进程。
之后可按需执行 verify 或 upgrade 重新绑定可信运行文件。

| action | 含义 |
| --- | --- |
| `absent` | 没有当前目标的锁/恢复记录，不扫描删除其他残留 |
| `preview` | 显示 `restore_previous` 或 `keep_committed` 计划、路径和已识别的临时文件 |
| `restored` | 文件及收据恢复到中断步骤前状态，清理本次恢复记录 |
| `completed` | 完整提交标记与目标文件一致，保留已提交结果并清理记录 |

所有结果声明 `runtime_verification: not_run`；配置恢复不等于旧 CLI、旧 npm 包或客户端发现已恢复。

## 记录及恢复规则

- 在首次修改文件前，将六个受管文件的前后字节、目标、进程 ID、随机标识和校验信息写入本地记录并同步。
  使用同目录硬链接排他发布完整的 `.dll-doctor-installer.lock`，避免半写入记录占据目标锁。
  准备阶段未发布成功的孤立临时记录不会自动扫描删除，也不会授权改动目标。
- 取得锁并完成现有内容检查后，同步“开始写入”标记。未开始写入的记录不能撤回后来到达的替换内容。
  所有文件写入并校验后同步“提交”标记。标记缺失/不完整时保守恢复步骤前状态，完整时要求文件与提交内容一致。
- 恢复先确认记录中的 PID 不存在。PID 仍在运行、被复用或查询权限不足时保守拒绝，没有强制接管选项。
- 受管文件只能与已记录的前/后内容匹配；未知、删除、链接或额外文件会阻止恢复。
  写入和清理前再次比较当前内容；发现并发变化就停止并保留记录。
- 只清理记录对应的临时文件：替换临时文件须是已知内容/前缀，记录别名须仍是同一文件的硬链接。
  不递归删除目标，不按宽泛模式删除用户文件。
- 恢复由 Windows PowerShell 的 `FileShare.None` 句柄互斥，父进程退出关闭管道后辅助进程释放句柄。
  guard 文件可保留为空文件；不靠删除“陈旧 guard”来争抢锁。恢复过程再次被强制终止后可重新运行 recover。

恢复记录仅保存受管 Skill 文本、CLI 绑定和收据，不保存整份 Agent/MCP 配置或凭据。
这些文件含本机路径，应保留在可信本地目录；锁、临时记录和 guard 已加入 gitignore。
记录上限 4 MiB；超限在写入目标之前拒绝。摘要用于一致性校验，不是来源签名。

## 范围及环境限制

- 需要支持硬链接的本地文件系统；本机测试临时目录为 NTFS。实际恢复需要 Windows 自带 PowerShell 可用，
  使用无配置文件的隐藏辅助进程，不修改执行策略、系统 PATH 或全局环境变量。
- 当前不支持 MCP、安装/卸载、旧版空锁/损坏记录的自动恢复。
  也不提供整批事务的中断恢复：已经完成的其他目标保留各自状态，当前步骤单独恢复。
  若中断发生在内部回滚，恢复的是该回滚步骤开始前状态，不承诺整个批次已回退。
- 不恢复旧 CLI/服务/npm 二进制，不承诺跨文件原子可见性。
  测试证明的是进程强制终止后的行为，未进行真实断电、系统崩溃或存储损坏验收。
- `RECOVERY_ACTIVE` 表示原 PID 存在/无法确认退出；`RECOVERY_BUSY` 表示恢复互斥锁或 PowerShell 不可用；
  `RECOVERY_JOURNAL` / `RECOVERY_CONFLICT` 表示不能证明记录与当前文件匹配。保留现场，不自动强制修复。

## 验证

- `npm test --prefix integrations/installer`：173 项通过、0 跳过；提供本地 x64 CLI 和构建后的 MCP 入口验证实际运行路径。
- 新增 18 项恢复测试已通过：实际终止子进程，覆盖三客户端/两种 scope、准备/临时写入/提交阶段，
  活进程拒绝、并发恢复、恢复再次中断、临时文件与别名、提交标记截断、记录校验、外部修改及 CLI 入口。
  截断临时文件/标记的测试使用受控 fixture；不把它们表述为物理断电实测。
- `npm test --prefix integrations/npm`：8 项启动器测试通过。
- 组装 `@som1ng/rondo@0.1.0-dev.5` 归档后，`tests/npm_package_tests.py` 的 2 项测试通过：
  空缓存、不可达 registry 的离线全局安装，打包后升级中断与 recover，原生 x86/x64 CLI、Skill、MCP 和卸载。
  同次测试传入 Codex 0.164.0、Claude Code 2.1.310、OpenCode 1.19.4 的原生路径，完成版本探测及配置路径回归；
  MCP 协议握手由测试 SDK 直连完成，不代表真实宿主自动发现或模型调用验收。
- 所有测试使用隔离临时目录，不修改真实客户端配置，不调用模型 API。

依据：[Node 22 的进程存在性检查及 Windows 终止行为](https://nodejs.org/docs/latest-v22.x/api/process.html#processkillpid-signal)，
[Windows/.NET 文件共享模式](https://learn.microsoft.com/en-us/dotnet/api/system.io.fileshare)。
