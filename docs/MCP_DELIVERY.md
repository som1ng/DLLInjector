# P3 本地只读 MCP 交付记录

日期：2026-10-25。分支：`codex/p3-readonly-mcp`。

开发前已创建并推送功能分支。开发期间共享工作目录被切回 main，提交前未再次核验，
导致 `6ca2dea` 误推主分支；已用 `116a209` 回退，验证回退后的完整文件树与原主分支
`e390cb4` 一致。随后在独立 worktree 恢复相同功能代码，通过功能分支 PR/CI 合入；
保留全部历史，未强推。此记录不将误推描述为符合约定的流程。

## 本次实现

- 私有 TypeScript stdio 服务，官方 MCP SDK 2.4.0，7 个工具复用原生 CLI JSON。
- 原生 CLI 升至 `2.0.0-dev.9`，Schema 仍为 `1.6`；增加 `report export --json`，
  复用已验证的历史报告读取器并保留整数精度、源状态与全部字段。
- 明确的目录/PID 授权、路径规范化、文件与输出上限、两个并发槽位、子进程超时/取消、
  Schema 验证及 stderr 审计；没有注入、提权或网络监听接口。
- 新增源码、锁文件和测试；依赖、编译产物、缓存和报告不进入 Git。
  现有 Windows CI 的 x64 Release 增加 npm 安装、构建及协议测试。

## 本地实际验证

环境：Windows，Node.js `22.22.3`，npm `12.2.0`。

| 命令 / 检查 | 结果 |
| --- | --- |
| 原生 `saved_reports_contract.py` 在旧 CLI 上运行新用例 | 按预期失败：旧 parser 拒绝 `report export --json` |
| `cmake --build --preset msvc-x64-release --parallel 4` | PASS |
| `ctest --preset msvc-x64-release --output-on-failure` | PASS，56/56 |
| `npm run build`（`integrations/mcp`） | PASS |
| `npm test` | PASS，7/7，包含真实 stdio 和原生 CLI 调用 |
| Inspector `2.12.0` CLI `tools/list` | PASS，7 个工具 |
| Inspector CLI `tools/call inspect_pe` | PASS，原生 PE 报告、`isError=false` |
| `npm pack --dry-run --json` | 只包含包元数据、构建代码和随包 Schema，无依赖目录和测试产物；未发布 |
| `npm audit --omit=dev --json` | 当次返回 0 个已知运行时依赖漏洞 |
| `git diff --check` / `git check-ignore` | PASS；生成目录、依赖和本地 Inspector 配置均忽略 |

协议测试覆盖新版 SDK 与 `2025-11-25` 握手、七工具、原生完整报告对比（只去除观测耗时）、
自有模块宿主、Unicode/空格、依赖目录、历史报告、整数精度、目录链接越界、PID 授权、
无效参数、原生错误与阻断发现的差异、零超时、输出限制、并发拒绝、stderr 审计和失败恢复。
子进程测试覆盖启动失败、异常退出、stdout/stderr 超限、超时/取消及返回前进程已退出。

Inspector 使用本地临时配置和 stdio；配置与输出放在忽略的 `build/`，没有修改用户 MCP 配置。
没有调用模型 API。安装依赖及下载 Inspector 时访问了 npm；服务运行时无需网络。

## 尚未验证 / 尚未交付

后续补充：Codex / Claude Code / OpenCode 的实际宿主调用已由
[MCP 客户端验证](MCP_HOST_VALIDATION.md) 覆盖；以下保留首版交付时的验证边界。

这是 MCP 服务与协议集成的交付，不等于全部 P3/P4 验收完成。尚未进行 Inspector 图形界面手动验收、
Codex/Claude Code/OpenCode 实际 Agent 宿主的 MCP 工具调用、模型自主选择工具的行为评测，
也没有发布 npm 包或提供一行安装器。此前 Skill 发现测试不能代替这些 MCP 验证。
完整输入、权限与资源限制见 [MCP 契约](MCP_CONTRACT.md)。
