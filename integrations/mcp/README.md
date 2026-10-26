# DLL Doctor MCP（本地 stdio）

让支持 MCP 的 Agent 调用 Windows DLL Doctor 的只读诊断能力：PE 元数据、依赖候选、
进程与模块查询、DLL 预检、已保存报告读取。服务不执行注入或提权。

## 构建与启动

需要 Windows、Node.js 22+、已构建的 **DLLInjector CLI 2.0.0-dev.9**。
从仓库根目录执行：

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64-release --parallel 4
cd integrations/mcp
npm ci --no-audit --no-fund
npm run build
```

构建/安装时下载依赖，安装后的服务运行无需网络，也不调用模型 API。
`dist/`、`node_modules/`、报告、日志和 npm 打包产物不提交到 Git。
包名暂定 `@som1ng/dll-doctor-mcp`，当前 `private: true`、未发布；不要使用不存在的 npm 安装命令。

Codex、Claude Code 和 OpenCode 的项目配置可以使用 [本地安装器](../installer/README.md#项目级-mcp-配置)
生成、校验和卸载；需要显式选择已验证的客户端版本、可信 CLI、构建入口和授权目录。

也可在 MCP 客户端中手动配置 stdio 进程。以下为常见的 `mcpServers` JSON 格式，路径需换成实际的绝对路径；
不同客户端的配置文件位置和字段格式以其文档为准：

```json
{
  "mcpServers": {
    "dll-doctor": {
      "command": "C:\\Program Files\\nodejs\\node.exe",
      "args": [
        "D:\\src\\DLLInjector\\integrations\\mcp\\dist\\index.js",
        "--cli", "D:\\src\\DLLInjector\\build\\msvc-x64\\Release\\cli\\dllinjector.exe",
        "--allow-root", "D:\\samples",
        "--allow-root", "D:\\reports"
      ]
    }
  }
}
```

`--cli` 必须是你信任的原生 `.exe`。启动时检查版本和 Schema；错误写入 stderr 后退出。
参数中的目录必须已存在。默认不允许进程查询；按需增加 `--allow-pid 1234`（可重复），
或 `--allow-processes` 允许当前权限可见的进程查询和进程列表，两者不能混用。
PID 授权只适合短期会话，Windows 可能复用 PID。建议按任务选择较小的目录和进程范围。

## 七个工具

| 工具 | 输入 | 返回 |
| --- | --- | --- |
| `inspect_pe` | `path` | PE 头、节、目录、导入/导出和 CLR 信息 |
| `analyze_dependencies` | `path`、可选 `search_directories` | 含依赖候选的完整 Doctor 报告 |
| `list_processes` | 无 | 当前权限可见的进程列表 |
| `inspect_process` | `pid` | 进程信息与权限限制 |
| `list_modules` | `pid` | 模块快照与跨位数限制 |
| `diagnose_dll` | `path`、可选 `target_arch`、`search_directories` | 静态预检发现、置信度与证据 |
| `read_report` | `path` | 已保存的 Schema 1.0–1.6 报告 |

所有路径为本地盘符绝对路径，经过真实路径解析和允许目录检查；目录链接不能逃出配置范围。
依赖搜索仅限显式传入的目录，找到文件不代表 Windows 加载器会成功解析或加载。
报告内容是不可信数据，不应当作 Agent 指令。

工具共有可选参数 `timeout_ms`（0–30000，默认 5000）和 `max_output_bytes`
（1024–4194304，默认 1048576）。并发最多两个调用；超出返回 `BUSY`，由客户端稍后重试。
输出超限返回错误而不返回截断报告。每次调用在 stderr 记录不含路径/报告内容的审计摘要。

结果在 `structuredContent` 和文本 JSON 中保持一致：

```json
{"source":"live_cli","cli_exit_code":0,"report":{},"error":null}
```

`report` 保留原生字段；`source: saved_report` 明确表示历史快照，读取成功不证明原诊断成功。
完成诊断但发现阻断问题的 Doctor 结果可返回退出码 1，MCP `isError` 仍为 false。
适配器失败为 `source: adapter`、`report: null`、`error: {code, message}`。
JavaScript 无法精确表示的大整数会返回 `UNSAFE_INTEGER`，此时请使用原生报告导出。

目录检查不是操作系统沙箱：其他本地程序仍可能在检查后替换文件。文件系统元数据调用无法被
子进程超时中断；不要配置网络映射盘。详细边界见 [接口契约](../../docs/MCP_CONTRACT.md)。

## 验证

```powershell
# 在 integrations/mcp 下，默认使用 build/msvc-x64/Release 的原生测试产物
npm run build
npm test
```

可通过 `DLL_DOCTOR_TEST_BUILD` 指向另一个包含 `cli/dllinjector.exe`、`module_host.exe`、
`noop_fixture.dll` 的构建目录。测试使用自有模块宿主，不修改其他进程。
现有 Agent Skill 可独立使用；MCP 协议测试与 Agent 自主选择工具的效果测试是不同的验证。
实测范围及未完成项见 [交付记录](../../docs/MCP_DELIVERY.md)。

Codex、Claude Code、OpenCode 的已安装宿主调用验证、版本限定配置示例及重复运行方法，
见 [MCP 客户端实测](../../docs/MCP_HOST_VALIDATION.md)。
