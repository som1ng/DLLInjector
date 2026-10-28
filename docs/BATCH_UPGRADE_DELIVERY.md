# Agent 批量配置升级

后续补充：[Skill 中断恢复](SKILL_RECOVERY_DELIVERY.md) 可恢复当前中断的单目标步骤，尚不恢复整个批次。

日期：2026-10-28。安装器 `0.1.0-dev.10`，Rondo npm 包 `0.1.0-dev.4`。
原生 CLI 仍为 `2.0.0-dev.9`。本轮不修改 GUI 或原生诊断能力。

## 接口

`rondo upgrade --agent all` 支持 Skill/MCP、project/user 两种范围。
所有目标必须已有有效受管安装；不自动安装缺失项，不接管手动配置。
Skill 的 Codex/OpenCode 共享目录仅处理一次，实际两项目标；MCP 分别处理三个客户端。
`--dry-run` 不执行程序、不写入文件；auto 升级仍不支持，verify/uninstall 仍要求单个客户端。

```powershell
rondo upgrade --agent all --scope project --project 'D:\work\my-app' --dry-run
rondo upgrade --agent all --scope project --project 'D:\work\my-app'
rondo upgrade --integration mcp --agent all --scope user --codex-client 'D:\tools\codex.exe' --claude-client 'D:\tools\claude.exe' --opencode-client 'D:\tools\opencode.exe' --allow-root 'D:\work'
```

完整参数沿用 [单客户端升级](CONFIG_UPGRADE_DELIVERY.md)。源码入口还需显式 CLI/服务路径。
MCP 要求三个原生客户端路径及完整 `--allow-root` 列表，只在显式传入 `--allow-processes` 时开启进程查询。
不接受混用 `--client` 或 `--select`，不扩大文件范围或修改客户端信任设置。

## 事务与恢复

1. 先对所有目标执行文件和所有权预检，检查路径、受管内容、安装记录以及可选分发清单。
   这一步不执行程序；运行版本与 MCP 握手由随后各目标的升级阶段验证。
2. 按顺序复用单客户端升级与写入后校验；相同运行文件/配置返回 `unchanged`。
   原始及本次写入的配置/收据快照仅保存在本进程内存中，不输出或另存用户私有配置。
3. 后续目标失败时逆序恢复本批改动。恢复包含失败目标，因为锁清理也可能在成功写入后报错。
   未变化的目标不参与恢复；本工具未创建的锁不会被清理。
4. 恢复前检查当前内容是否仍是本批写入或已恢复的原始内容；并发编辑/替换/权限问题会保留现场，
   返回 `ROLLBACK_INCOMPLETE`。不会为了回滚而覆盖后来编辑的配置。

成功 JSON 提供 `command: upgrade`、`action: upgraded|unchanged|preview` 及各目标结果。
失败 JSON 的 `details` 提供 `failed_agent`、`completed_targets`、`rollback` 和 `rollback_status`，
不包含配置原文或任意底层错误消息。内部恢复只使用本次进程的快照，不提供用户可直接调用的恢复 CLI。

恢复只验证原受管配置/收据的完整性，不启动旧 CLI/服务，因此旧运行文件已删除也能恢复记录。
这不等于恢复旧二进制或整个 npm 包：旧文件已不存在时，恢复后的配置可能需要再次升级才能运行。
跨目标原子可见性、强制结束/断电恢复和客户端自动加载仍不在保证范围内。

## 验证

- `npm test --prefix integrations/installer`：155 项通过、0 跳过，包含 30 项新增批量升级测试。
  通过 `DLL_INSTALLER_TEST_CLI` / `DLL_INSTALLER_TEST_MCP` 复用已有本机 CLI 和 MCP 构建。
- 新测试覆盖两种集成/两种 scope、共享目录去重、预览、重复升级、逐目标失败、旧运行文件删除、
  缺失收据、未变化目标、写入后锁清理失败、运行验证失败、并发修改、已有锁和 CLI 参数分发。
  含真实 CLI/MCP 服务握手，原生客户端版本在这一层使用 fixture。
- `npm test --prefix integrations/npm`：8 项通过，包含 batch 参数与包内运行路径转发。
- `python -X utf8 tests/npm_package_tests.py --archive <dev.4.tgz> --codex <Codex.exe> --claude <Claude.exe> --opencode <OpenCode.exe>`：
  2 项通过。验证完整归档的离线全局安装/卸载、Skill 批量升级和用户 MCP 三客户端批量升级，
  包括删除旧 CLI 后迁回包内运行路径及后续 verify/uninstall。
  本地实际版本为 Codex 0.164.0、Claude Code 2.1.310、OpenCode 1.19.4；仅做原生客户端版本探测和直接 MCP 握手，
  不将其描述为客户端自动加载或模型行为验收。CI 无原生客户端时仍执行归档与 Skill 批量升级路径。
- 本地 Markdown 链接/锚点 91 项及 `git diff --check` 通过。

所有写入测试使用隔离项目/用户目录，不改动真实客户端配置或凭据，也不调用模型 API。
公开 npm 发布、许可证和断电恢复等后续工作仍见 [路线图状态](ROADMAP_STATUS.md)。
