# 整批升级进程中断恢复

日期：2026-10-29。安装器 `0.1.0-dev.13`，Rondo npm 包 `0.1.0-dev.7`。
为新版 `upgrade --agent all` 增加整批恢复，复用已有 Skill/MCP 单目标记录。
本轮不改 GUI、原生诊断 CLI 或 MCP 工具协议。

## 命令与结果

```powershell
rondo recover --agent all --scope project --project 'D:\work\my-app' --dry-run
rondo recover --agent all --scope project --project 'D:\work\my-app'
rondo recover --integration mcp --agent all --scope user --dry-run
rondo recover --integration mcp --agent all --scope user
```

支持 Skill/MCP 和 project/user。Skill 的 Codex/OpenCode 共享目标仍只处理一次。
不需要 CLI、manifest、服务、客户端路径或访问策略；不接受这些运行参数以及 auto/select。
恢复不执行 CLI、客户端或 MCP 服务，结果声明 `runtime_verification: not_run`、`client_discovery: not_run`。
`--dry-run` 不写入文件、不启动辅助进程。

| action | 含义 |
| --- | --- |
| `absent` | 当前集成/scope 没有批次记录；不扫描其他记录或推断旧批次 |
| `preview` | 列出可恢复目标；提交前计划回滚，提交后计划保留结果 |
| `restored` | 本批参与目标恢复到各步骤开始前，未变化/尚未执行的目标保持原状 |
| `completed` | 批次已提交，保留升级后的配置并完成记录清理 |

这里只恢复配置及收据，不恢复被 npm 替换或删除的旧运行文件。

## 写入与恢复顺序

1. 沿用全目标文件/所有权预检，随后排他创建项目或用户根目录的
   `.dll-doctor/batch-upgrade-<skill|mcp>-<project|user>.lock`。
   已有批次记录会阻止新的整批升级；已有目标锁不会被抢占。
2. 每个改变的目标在首次写入前，将单目标恢复记录硬链接为目标锁旁的
   `.dll-doctor-batch-<批次标识>-<agent>.journal`，再同步批次参与标记。
   硬链接位于各目标所在目录，不要求所有用户配置都在同一卷。
   MCP 记录仍只含受管条目、收据和配置摘要，不另存整份私有客户端配置。
3. 批次结束前保留参与目标的锁。全部目标完成并再次检查状态后，同步整批 committed 标记。
   没有完整提交标记的中断批次逆序恢复；已有完整标记的批次只保留结果并清理记录。
4. 实际恢复先检查 PID 已退出，再获取 Windows PowerShell 排他文件句柄。
   对全部参与记录、文件与所有权完成预检后才开始恢复写入；任一冲突阻止开始恢复。
   每次写入前继续检查当前字节，遇到后来改动则停止并保留记录。
5. 恢复完成后先同步 rolledback 标记，再逐个清理目标锁、对应记录和最后的批次记录。
   恢复中或清理中再次被终止，都可以运行相同命令重试；终态清理不回滚后来出现的新配置。

记录清理只涉及明确列出的文件，不递归删除目录或扫描宽泛模式。空 guard 文件可保留。
目标记录用硬链接身份及内容检查确认所有权；仅字节相同的另一个文件不能替代它。
新批次的目标记录使用格式 2，旧安装器会明确拒绝；独立单目标格式 1 仍受支持。
`recover --agent codex/claude/opencode` 不能绕过批次决定处理其中一个目标。

## 失败处理

- `BATCH_RECOVERY_REQUIRED`：已有批次或目标属于批次，使用相同 integration/scope 的 `recover --agent all`。
- `BATCH_CLEANUP_REQUIRED`：整批已提交，记录清理失败。结果不会伪称回滚；recover 会保留已提交结果并清理。
- `RECOVERY_ACTIVE` / `RECOVERY_BUSY`：原 PID 存在、不能确认退出，或恢复互斥锁不可用；不自动接管。
- `RECOVERY_JOURNAL` / `RECOVERY_CONFLICT`：记录损坏、参与记录丢失、目标路径/环境改变、文件被编辑、锁被替换等；保留现场。
- `ROLLBACK_INCOMPLETE`：可捕获错误的回滚或实际恢复未完成，保留全部剩余记录。

若用户/客户端后来修改了文件，恢复可能持续返回冲突，这是保留这些改动的预期行为。
手动处理前停止相关安装器和会写配置的客户端，备份批次记录、所有参与目标的记录/锁、配置、收据及临时文件。
核对每个目标的受管内容与收据一致后，才可把本批明确对应的记录/锁移到备份位置。
不能只删除批次锁或单个目标锁再假定状态已恢复；无法确认时继续保留现场，不清理其他操作的锁。

## 范围与限制

- 仅处理本版本创建的批量升级，不把旧版单目标记录拼成一个批次，也不提供安装/卸载中断恢复。
- 整批提交前，各目标仍按顺序写入，不保证跨文件或跨目标原子可见性。
  普通可捕获失败可以恢复无冲突的目标；显式整批恢复则先预检所有参与目标。
- 真实断电、系统崩溃、文件系统损坏、新机器及完全离线系统尚未验收。
  测试里的截断标记、删除记录和强制终止子进程不等同于物理断电。
- 沿用单目标文件大小限制；批次头记录上限 64 KiB。要求各目标所在文件系统支持硬链接，实际恢复需 Windows PowerShell。
- 批次定位依赖相同项目/用户根目录及配置环境，例如自定义 CODEX_HOME；不自动改写这些环境。
- 记录包含本机路径，仅用于本地恢复。批次、目标记录、guard、临时文件、构建和归档均由 Git 忽略。

## 验证

- `npm test --prefix integrations/installer`：229 项通过、0 跳过，提供本地 CLI 和 MCP 入口。
  包含 33 项新增整批恢复测试，以及已有 30 项批量升级回归。
- 新测试实际终止子进程，覆盖两种集成/两种 scope、第二目标部分写入、准备/临时写入/提交/清理阶段，
  活进程拒绝、并发与重复中断、未变化目标、全部目标冲突预检、内部失败回滚、损坏/丢失记录、
  硬链接身份、截断标记、提交前后编辑、协调记录丢失、CLI 参数及集成隔离。
- `npm test --prefix integrations/npm`：8 项启动器测试通过，覆盖 Skill/MCP 的 all 恢复参数转交。
- 组装 `@som1ng/rondo@0.1.0-dev.7` 后，`tests/npm_package_tests.py` 的 2 项归档/离线全局安装测试通过。
  覆盖包内 Skill/MCP 整批升级强制退出、all 恢复、后续升级/卸载及原有单目标恢复路径。
  整批 MCP 中断 fixture 只替换客户端版本探测，CLI 与 MCP 服务实际执行；同次本地回归还使用
  Codex 0.164.0、Claude Code 2.1.310、OpenCode 1.19.4 原生版本探测和直接 SDK 握手。
  这些结果不代表真实宿主自动发现或模型行为验收。
- 104 个相对 Markdown 链接可解析，归档中 14 个相关源码文件与工作目录一致；`git diff --check` 通过。

测试使用隔离临时目录，不改动真实客户端配置，不调用模型 API。
