# 项目级 MCP 安装器交付记录

日期：2026-10-26。源码安装器 `0.1.0-dev.3`，Windows / Node.js 22.22.3。

## 已交付

`--integration mcp` 支持 Codex 0.164.0、Claude Code 2.1.310、OpenCode 1.19.4 (V1)
的项目级 setup / verify / uninstall；默认仍为独立的 Skill 安装模式。
支持写入前预览、显式可信运行路径和授权目录、可选进程查询权限及 CLI 分发清单。
版本不匹配、同名手动配置、托管条目修改、配置损坏或链接路径会明确失败。

JSON/JSONC 只编辑托管条目，TOML 追加可验证的标记表；保留其他设置，
遇到 JSONC 编辑会丢失相邻注释的布局则拒绝修改。记录不复制无关配置或凭据。
同目录文件替换、本地锁和写入前字节比较用于处理可捕获的失败及并发修改。
强制终止/断电恢复不在保证范围内。运行方式见 [安装器说明](../integrations/installer/README.md)，
完整边界见 [契约](MCP_INSTALLER_CONTRACT.md)。

## 本地验证

| 检查 | 结果 |
| --- | --- |
| `npm.cmd test`（`integrations/installer`） | 35 项通过，0 跳过；同时设置真实 CLI 和 MCP 入口环境变量 |
| `python tests/mcp_hosts_tests.py` | 14 项通过 |
| `python tests/mcp_installer_hosts.py`，指定三个原生客户端及 CLI/MCP/DLL fixture | 三个客户端全部通过；每个发现 7 个工具，执行 4 项工具检查 |

安装器测试覆盖原有 Skill/分发校验，以及三种配置格式、中文/空格/Unicode 路径、
预览无执行、重复安装、卸载、后续用户修改、收据篡改、相邻注释、链接拒绝、
预检失败、配置/记录写入故障和并发编辑回滚。真实入口测试使用可信的
CLI `2.0.0-dev.9 / Schema 1.6` 产物；本轮没有修改原生 C++ 代码。

客户端检查在临时隔离项目中运行 setup、工具发现/调用、重复 setup、verify 和 uninstall。
四项工具检查为 `inspect_pe`、`read_report`、越界策略错误、错误后恢复。

| 客户端 | 实际验证方式 | 限制 |
| --- | --- | --- |
| Codex 0.164.0 | 解析生成的 TOML，通过隔离的 app-server 配置覆盖传入，直接调用 MCP RPC | 未验证 `.codex/config.toml` 自动发现和项目信任 |
| Claude Code 2.1.310 | 原生客户端显式读取生成的 `.mcp.json`，本地固定模型响应驱动工具调用 | 未验证默认项目发现或真实模型自主选择 |
| OpenCode 1.19.4 | 原生客户端通过 `OPENCODE_CONFIG` 显式读取生成配置，本地固定模型响应驱动工具调用 | 未验证默认项目发现或真实模型自主选择 |

外部模型 API 调用为 **0**；Claude/OpenCode 各收到 5 次回环地址 fixture 请求，
Codex 不发模型请求。未修改真实用户的配置/信任设置。
OpenCode 首次读取会添加 `$schema`：测试确认卸载删除 `dll_doctor` 并保留该字段。

复现前按 MCP README 构建服务，并安装安装器依赖；替换下面的绝对路径：

```powershell
$env:DLL_INSTALLER_TEST_CLI = 'D:\tools\dllinjector.exe'
$env:DLL_INSTALLER_TEST_MCP = 'D:\src\DLLInjector\integrations\mcp\dist\index.js'
npm.cmd test --prefix integrations/installer
python tests/mcp_hosts_tests.py
python tests/mcp_installer_hosts.py --cli 'D:\tools\dllinjector.exe' --server 'D:\src\DLLInjector\integrations\mcp\dist\index.js' --dll 'D:\fixtures\noop_fixture.dll' --codex 'D:\tools\codex.exe' --claude 'D:\tools\claude.exe' --opencode 'D:\tools\opencode.exe'
```

客户端必须为已安装的原生 EXE；可以只指定需要检查的客户端。
`noop_fixture.dll` 使用本仓库测试构建生成的无副作用 DLL。
日志、临时配置、依赖和二进制留在本机，不进入源码仓库。

## CI 与后续范围

Windows x64 Release CI 安装锁定的安装器依赖，并运行完整安装器测试及真实 MCP 握手；
四个原生构建/测试组合和跨位数测试保留。安装了特定版本客户端的宿主实测是显式本地测试，
没有将这些客户端的下载/登录或模型调用引入 CI。

用户级 MCP、客户端自动发现/选择、运行组件自动下载、公开 npm 发布、Pi 的统一入口、
升级恢复和真实模型任务效果仍待推进。`verify` 仅证明本机运行文件、协议握手和工具清单符合预期，
不代替客户端批准、自动项目发现或模型效果验证。
