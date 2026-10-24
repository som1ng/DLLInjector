# Skill 客户端发现交付记录

日期：2026-10-24。分支 `codex/p2-skill-client-discovery`，实现前已创建同名远端分支。
先提交 [验证契约](AGENT_DISCOVERY_CONTRACT.md) 与防误判测试，再实现客户端适配与文档。

## 结果与范围

| 实际版本 | 入口 | 通过的检查 |
| --- | --- | --- |
| Codex CLI `0.164.0` | stdio app-server `skills/list` | 项目 Skill 的精确路径、名称、描述、repo scope、enabled；未安装项目负例 |
| OpenCode `1.19.4` | `debug skill --pure` | `.agents/skills` 项目 Skill 精确路径及元数据；未安装项目负例 |
| Pi `1.4.0`（`@earendil-works/pi-coding-agent`） | 已安装包的 `DefaultResourceLoader` 与 `formatSkillsForPrompt` | 信任项目发现与提示清单；未信任项目和未安装项目负例 |

全部真实发现检查使用临时 Git 项目、中文/空格路径与嵌套子目录，不复制到用户真实项目。
没有发起模型回合或创建 Pi AgentSession；没有安装依赖、改写 HOME/CODEX_HOME 或修改用户已有配置。
第三方客户端可能按自身设计写日志/缓存，本轮没有证明其完全不访问网络。

OpenCode 的发现结果不包含工具权限状态；测试中的统一 enabled 标记只表示它被列出。
Pi 是资源加载层验证，不能当成完整交互客户端启动验证。Claude Code 发现仍未验证。
22 条自然语言场景尚未进行模型自动触发/回答评测，本轮不宣称该部分通过。

## 实际执行的验证

- `python -X utf8 tests/agent_discovery_tests.py`：12 项通过。
- `node --check tests/pi_skill_discovery.mjs`：通过。
- `python -X utf8 tests/agent_discovery.py --codex <本机原生 EXE> --opencode <本机原生 EXE> --pi-package <本机已安装包> --output build/agent-discovery-local.json`：
  使用上表安装实际执行，三个客户端全部检查通过；具体私人安装路径不纳入文档。
  最终适配器修订后以 `--output build/agent-discovery-final.json` 再次执行，全部通过。
- `cmake --build --preset msvc-x64-release --parallel 4`：通过。
- `ctest --preset msvc-x64-release --output-on-failure`：56/56 通过。

轻量检查已加入 CTest 为 `skill.discovery-checks`，CI 不需要安装 Agent 客户端。
它覆盖精确项目路径、全局同名碰撞、虚拟/相对资源地址、重复结果、失效/禁用元数据、
目标诊断错误、无效响应、缺失客户端、脚本启动器拒绝、参数范围和报告拒绝覆盖。
这些替身数据的单元检查与上面的真实发现记录分开，不混算为客户端测试。

本轮未改动原生 CLI 或权威 Skill 内容，版本仍为 `2.0.0-dev.8` / Schema `1.6`。
适配器显式运行、按已安装版本失败报告，不尝试自动安装、升级或绕过客户端规则。
`git diff --check` 与本轮文档相对链接检查通过。
日志、临时探针和 JSON 汇总位于忽略目录，不提交到 GitHub。
远端检查与评审状态以本 PR 的实际记录为准。

下一开发阶段是基于现有 CLI JSON 的本地 stdio MCP；真实模型行为评测仍保留为独立待办。
