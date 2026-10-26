# 客户端探测与选择交付记录

日期：2026-10-26。Windows / Node.js 22.22.3；源码安装器 `0.1.0-dev.4`。

## 已交付

- `list-agents`：检查本机已知位置，列出 Codex、Claude Code、OpenCode 原生 EXE 候选，
  不运行客户端或包装器，不读取用户配置，不联网。
- `setup --agent auto`：终端展示路径并要求选择一个目标；无终端时须明确 `--select`，
  同名多路径用 `--client` 消除歧义，不默认选择第一个或唯一候选。
- 选中后调用已有 Skill / 项目 MCP 安装流程；支持原有预览、校验、冲突保护和卸载。
  无选择的 dry-run 只返回候选，不提问、不启动程序、不写文件。

命令见 [安装器说明](../integrations/installer/README.md#探测与选择客户端)，
规则见 [选择契约](AGENT_SELECTION_CONTRACT.md)。候选文件存在不证明其来源或版本；
客户端自动发现安装资源、工具协议检查和模型效果是其他层次的验证。

## 本地实测

| 检查 | 实际结果 |
| --- | --- |
| `npm.cmd test`（安装器目录，设置真实 CLI/MCP 环境变量） | 45 项通过，0 跳过 |
| `node integrations/installer/src/index.mjs list-agents` | 检出两处 Codex、一处 Claude、一处 OpenCode；无探测执行、无扫描警告 |
| 真实终端 `setup --agent auto ... --verify` | 展示四个路径；选择 Claude 后在隔离项目安装成功 |
| 真实终端重复 auto setup，输入空行 | 返回 `AGENT_SELECTION_CANCELLED`；未开始安装 |
| 显式 `uninstall --agent claude` | 删除上述终端测试安装，托管目录已不存在 |
| `python tests/installer_hosts.py --auto ...` | Codex/OpenCode 项目 Skill 发现、重复安装、卸载后不可见均通过 |
| `python tests/mcp_installer_hosts.py --auto ...` | 三个客户端均通过 setup、七工具发现、四项工具检查、重复安装、verify 和 uninstall |

单元测试新增 10 项，覆盖原生 PATH、npm 固定布局、用户目录、忽略相对/空/UNC PATH、
去重与路径歧义、无候选、确认/取消、非交互输入拒绝、Skill 安装复用及 MCP 路径传递。
测试客户端文件为不可执行的本地 fixture；只有显式原生集成测试使用可信 CLI/MCP 产物。

真实客户端版本为 Codex **0.164.0**、Claude Code **2.1.310**、OpenCode **1.19.4**。
Skill 实测没有调用模型；MCP 实测使用本地固定响应，外部模型 API 调用为 **0**。
Codex MCP 使用生成 TOML 的隔离覆盖，Claude/OpenCode 显式加载生成文件；
此处没有新增对 MCP 自动项目发现、项目信任或模型自主选择的声明，
详见 [MCP 验证边界](MCP_INSTALLER_DELIVERY.md)。Claude 的 Skill 自动发现仍未实测。

复现前准备现有 CLI/MCP 构建和原生客户端。下列 `<...>` 为需要替换的路径占位符；
`list-agents` 必须能检出所指定客户端，测试会用精确路径区分多个安装：

```text
python tests/installer_hosts.py --auto --cli <cli.exe> --codex <codex.exe> --opencode <opencode.exe>
python tests/mcp_installer_hosts.py --auto --cli <cli.exe> --server <MCP-index.js> --dll <noop_fixture.dll> --codex <codex.exe> --claude <claude.exe> --opencode <opencode.exe>
```

Windows CI 现有安装器测试步骤自动收集新增测试；真实客户端和交互终端检查保留为显式本地验收。
没有新增依赖。测试日志、临时项目、配置和二进制沿用忽略规则，不进入源码仓库。

## 后续范围

自动下载尚未交付：本轮检查 [GitHub Releases](https://github.com/som1ng/DLLInjector/releases)
时，公开发行仍只有旧 `injector_1.0` GUI EXE，不满足 CLI dev.9 / Schema 1.6 的安装契约。
现有 CLI 应继续来自可信成功 CI 的分发归档或源码构建；未把旧 GUI 当成新 CLI 下载。

批量 `all`、用户级 MCP、Pi 统一前置引导、更广的包管理器/自定义布局、ARM64 实机验收、
全新 Windows 验收、公开 npm 分发和升级恢复仍待推进。手动显式入口保持可用。
