# 批量 Skill 安装交付

日期：2026-10-27。安装器 `0.1.0-dev.5`；分支 `codex/p4-batch-skill-setup`，
实现前已创建本地和同名远端分支。开发中按用户要求 fetch 并同步 main `7a10b65`，
包含 Gemini 的 `apps/gui` 目录调整与纯 Win32/WebView2 架构。

## 已交付

`setup --agent all` 一次配置 Codex、Claude Code 和 OpenCode 的 Skill 目录，支持项目/用户范围。
Codex/OpenCode 共用的目录只写一份；输出两个实际目标及其覆盖的客户端。
全量预检后依次安装，支持预览、重复安装和安装后校验；失败时撤回本次新增目标，
保留已有安装和并发用户修改，无法恢复时准确报告 `ROLLBACK_INCOMPLETE`。

修复了三个与批量行为有关的问题：旧收据的安装器版本号不会再让内容相同的安装误报冲突；
提交后的锁清理失败留下目标目录时，不会将回滚误报为完成；其他安装操作替换了目标内容后，
回滚会通过锁内收据比对保留替换后的有效安装。三项均先复现失败再修复。
运行方式见 [安装器说明](../integrations/installer/README.md#批量安装-skill)，
完整边界见 [契约](BATCH_SKILL_CONTRACT.md)。

## 实际验证

| 命令/路径 | 结果 |
| --- | --- |
| `npm.cmd test --prefix integrations/installer`，提供真实 CLI 与 MCP 环境变量 | 60 项通过，0 跳过；新增 15 项批量相关测试 |
| `python -X utf8 tests/agent_discovery_tests.py` | 14 项通过，包括两份安装路径的重复发现拒绝与卸载负例 |
| `python -X utf8 tests/installer_hosts.py --all --cli <cli.exe> --codex <codex.exe> --opencode <opencode.exe>` | Codex 0.164.0 / OpenCode 1.19.4 的项目发现、无重复、重复安装及两个目录卸载后不可见均通过 |
| `cmake --build --preset msvc-x64-release --parallel 4` | 基于最新目录结构构建通过 |
| `ctest --preset msvc-x64-release --output-on-failure` | 59/59 通过 |
| `node --test tests/gui/ui.test.mjs`，复用已安装的 Playwright | 16/16 通过 |

安装测试使用临时目录、中文/空格/emoji 路径与隔离配置；真实命令行入口执行可信 CLI 的
版本查询与只读 PE 检查，没有修改真实用户的 Skill、MCP 或信任设置。
两个客户端的发现检查不启动模型回合，外部模型 API 调用为 0。
已复用现有构建、Node 依赖和 jsoncons 缓存，没有安装或更新依赖。
远端构建、评审与合并状态以对应 PR 为准。

## 验证边界

Claude Code 的文件安装和 CLI 校验已覆盖，但未新增其真实 Skill 发现验证；
用户级目录使用隔离 home fixture 验证，未宣称真实客户端用户级发现通过。
批量入口仅支持 Skill setup；verify/uninstall 仍显式指定客户端，MCP 和 Pi 沿用各自入口。
不保证跨目录原子可见性或强制终止/断电恢复；空父目录可能保留。
自动下载 CLI、MCP 批量配置、用户级 MCP、公开 npm 分发仍待后续交付。
