# MCP 客户端接入与实测

核验日期：2026-10-25。服务：`integrations/mcp`，版本 `0.1.0-dev.1`；
原生 CLI：`2.0.0-dev.9` / Schema `1.6`。

## 本机验证结果

| 宿主 | 实测版本 | 真实宿主路径 | 结果 |
| --- | --- | --- | --- |
| Codex CLI | 0.164.0 | app-server 临时上下文 → `mcpServerStatus/list` / `mcpServer/tool/call` | 7 工具发现；4 项调用检查通过；没有模型请求 |
| Claude Code | 2.1.310 | 原生 CLI `--bare --print` → stdio MCP | 7 工具出现在宿主工具清单；4 项调用检查通过；本地固定响应驱动 |
| OpenCode | 1.19.4 | 原生 CLI `run --pure` → stdio MCP | 7 工具出现在宿主工具清单；4 项调用检查通过；本地固定响应驱动 |

每个宿主的 4 项检查：PE 查询、读取保存的失败报告、拒绝相对路径、再次成功查询 PE。
实时报告与直接 CLI 输出逐字段比较，只忽略观测耗时；保存的报告连源耗时、错误与失败状态都必须完全保留。
Codex 还检查结构化返回与文本 JSON 一致、MCP `isError` 标志正确。
Claude/OpenCode 则验证宿主转交给本地模型端的工具结果 JSON。

Claude/OpenCode 的测试模型端点仅绑定 `127.0.0.1` 随机端口，发送预先写好的工具请求，
不执行模型推理、不转发请求、不使用真实密钥。每个宿主完成 5 次本地模型协议请求。
**这证明宿主可以发现并执行工具，不证明真实模型会自主选对工具或准确解释结果。**
Codex 使用 ephemeral 上下文和直接 RPC，不发起 `turn/start`，不创建可恢复的测试聊天。

## 重复运行

检查脚本不安装或升级客户端。使用已安装的原生 `.exe` 路径，不能用 npm 的 `.ps1` / `.cmd` 启动脚本。
先按 [MCP 接入说明](../integrations/mcp/README.md) 构建服务和原生测试 fixture。
从仓库根目录运行，下列客户端路径需替换成实际位置：

```powershell
python tests/mcp_hosts.py `
  --codex 'C:\tools\codex\codex.exe' `
  --claude 'C:\tools\claude\claude.exe' `
  --opencode 'C:\tools\opencode\opencode.exe' `
  --server .\integrations\mcp\dist\index.js `
  --cli .\build\msvc-x64\Release\cli\dllinjector.exe `
  --dll .\build\msvc-x64\Release\noop_fixture.dll `
  --output .\build\mcp-host-results-new.json
```

三个客户端选项可以只选一个或两个，未指定者不会记作通过。Node 默认从 PATH 查找，
可用 `--node` 指定；`--timeout` 为每次操作的 10–120 秒期限，默认 60 秒。
摘要只包含所选客户端版本、检查项目和验证方式，`--output` 拒绝覆盖。
缺少工具、严格输入标记或超时范围丢失、结果不符、进程错误或超时都会返回非零。

配置、DLL 副本、保存的报告和 OpenCode 数据库位于临时 Unicode/空格目录，结束后清理。
不会更改用户客户端配置，不替换 HOME/CODEX_HOME；Codex 使用进程级参数覆盖，
Claude 使用 `--bare`、独立配置目录与 `--strict-mcp-config`，OpenCode 使用临时配置、XDG 目录和数据库。
只终止本次检查启动的进程；超时清理不按进程名称查杀其他 Agent。
客户端自身仍可能写运行日志/缓存；该检查不是全系统网络抓包或防火墙隔离验收。

## 手动配置示例

以下均指向已构建的同一服务，目录需换成实际存在的绝对路径。
进程查询默认禁用；按需在 args 后增加 `--allow-pid` 或 `--allow-processes`。
不需要把本地验证用的模型 fixture 配置写进日常客户端。

Codex 的 MCP 配置段：

```toml
[mcp_servers.dll_doctor]
command = 'C:\Program Files\nodejs\node.exe'
args = ['D:\src\DLLInjector\integrations\mcp\dist\index.js', '--cli', 'D:\src\DLLInjector\build\msvc-x64\Release\cli\dllinjector.exe', '--allow-root', 'D:\samples']
```

Claude Code 的 MCP JSON：

```json
{
  "mcpServers": {
    "dll_doctor": {
      "command": "C:\\Program Files\\nodejs\\node.exe",
      "args": ["D:\\src\\DLLInjector\\integrations\\mcp\\dist\\index.js", "--cli", "D:\\src\\DLLInjector\\build\\msvc-x64\\Release\\cli\\dllinjector.exe", "--allow-root", "D:\\samples"]
    }
  }
}
```

OpenCode **1.19.4 / V1** 的配置段（不要直接当作 V2 配置）：

```json
{
  "mcp": {
    "dll_doctor": {
      "type": "local",
      "command": ["C:\\Program Files\\nodejs\\node.exe", "D:\\src\\DLLInjector\\integrations\\mcp\\dist\\index.js", "--cli", "D:\\src\\DLLInjector\\build\\msvc-x64\\Release\\cli\\dllinjector.exe", "--allow-root", "D:\\samples"],
      "enabled": true
    }
  }
}
```

请将对应配置段合并到已有配置中，保留其他服务。Claude 项目配置还受其工作区信任与 MCP 批准流程约束；
测试中的显式动态配置不代表项目 `.mcp.json` 能跳过该流程。

## 本轮交付与边界

功能分支：`codex/p3-mcp-host-validation`，开发前创建同名远端分支，使用独立 worktree。
新增 `tests/mcp_hosts.py` 和本地协议 fixture，14 项轻量测试进入 x64 Release CI。
原生 CLI、MCP 运行时代码、版本及依赖保持本轮验证的既有实现。

实际执行：隔离 worktree 中 `npm ci --no-audit --no-fund`、`npm run build`；
`python tests/mcp_hosts_tests.py`；上述三个已安装客户端的组合检查。
原生 CLI / DLL 从上轮已验证的 dev.9 构建复制到本轮忽略目录复用，没有声称本轮重新编译原生代码。

回归中发现 OpenCode 会等待管道 stdin 结束：早期完整用例超时，测试启动器显式关闭 argv 提示模式的 stdin 后通过。
此修正仅涉及测试启动方式，没有修改 MCP 服务来绕过宿主问题。

仍未覆盖：真实模型自主行为评测、其他客户端版本、OpenCode V2、Inspector 图形界面手动验收、
全系统断网验收、自动安装器与 npm 发布。因此本轮不把全部 P3/P4 条目统称为完成。

接口依据：[Codex app-server](https://learn.chatgpt.com/docs/app-server)、
[Codex MCP](https://learn.chatgpt.com/docs/extend/mcp)、
[Claude Code MCP](https://code.claude.com/docs/en/mcp)、
[Claude 流式消息](https://platform.claude.com/docs/en/build-with-claude/streaming)、
[OpenCode V1 MCP](https://opencode.ai/docs/mcp-servers/)、
[OpenCode V1 自定义 Provider](https://docs.opencode.ai/docs/providers/)，
以及本机 CLI 帮助与 Codex 生成的实验接口 Schema。测试驱动器须随实际宿主接口变化维护。
