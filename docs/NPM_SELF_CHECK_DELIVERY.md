# npm 安装后自检

日期：2026-10-29。npm 包 `0.1.0-dev.8`；原生 CLI `2.0.0-dev.9`、安装器 `0.1.0-dev.13` 不变。

安装后可运行 `rondo self-check` 或 `rondo self-check --json`，在配置 Agent 之前检查本地包。
源码安装器和原生 CLI 不新增此命令；它属于 npm 启动器。

## 检查与边界

| 检查 | 通过代表什么 |
| --- | --- |
| platform | 宿主为 Windows x86/x64，沿用启动器的架构选择；不支持 ARM64 |
| node | 当前 Node.js 版本满足 22.19.0 最低要求 |
| package | 本地 package.json 的包名、版本和文件类型符合预期 |
| cli_distribution | 宿主架构 CLI 的 PE、大小、SHA-256 与清单一致；哈希不等于来源签名 |
| cli_runtime | 已校验的 CLI 实际完成 `--version --json`，返回与清单一致的版本和协议字段 |
| installer | Node 能启动安装器 `--help`；不代表所有安装操作已执行 |
| mcp_modules | MCP 入口通过 Node 语法检查，server 与安装器 MCP 模块及其导入依赖、报告 Schema 能加载；不执行服务入口或建立 MCP 会话 |
| skill | 四个必需资源为非空普通文件；不评估 Skill 内容语义 |

宿主/Node/包元数据失败后跳过后续探测。CLI 校验失败后禁止执行该二进制，但独立的安装器、MCP、Skill 检查继续。
CLI 无法启动、超时、异常退出或输出契约错误，都只能说明版本探测失败；不能据此认定 VC++ 运行库缺失。
恢复或重装的操作需要用户显式执行，自检不自动修复文件。

四个子进程使用固定入口及参数、`shell: false`、隐藏窗口，各限制 10 秒，stdout/stderr 各限 1 MiB。
失败报告使用固定错误码/提示，不回显子进程 stdout/stderr、异常、环境变量、配置内容或用户目录。
不扫描/执行 Agent 客户端、不修改 Agent 配置、不调用模型 API；CLI 只执行版本查询。

## 输出

- `--json`：stdout 输出单个对象，独立 `format: 1`，`command: self-check`。
- 顶层 `status` 为 `success` 或 `error`；`checks` 项的状态为 `passed`、`failed`、`skipped`。
- 包含包版本、宿主/Node 信息；`runtime_verification` 为 `passed`、`failed` 或 `not_run`。
- `client_discovery` 始终为 `not_run`，不能把模块加载解释为客户端发现、工具调用或模型行为验证。
- 退出码 `0`：全部通过；`1`：检查失败；`2`：参数错误。`--help` 不需要可用的原生运行文件。

## 验证记录

本地验证使用临时目录和既有可信 x64/x86 构建，运行文件未变化；打包不填伪造的 source revision。
npm 归档版本升级不意味着发布到公开 registry。

- `npm test --prefix integrations/npm`：28 项通过、0 跳过（原有 8 项启动器回归及 20 项自检测试）。
- `python tests/npm_package_tests.py --archive build/npm-self-check/som1ng-rondo-0.1.0-dev.8.tgz`：2 项通过。
  本轮未提供原生客户端路径；MCP 中断 fixture 使用本地客户端版本探测替身，包内 CLI/服务和 SDK 握手实际执行。
- 从 staging 实际运行 `rondo self-check --json`：8 项检查通过，`client_discovery: not_run`。
- 本轮修改文档的 78 个相对链接可解析；归档中 16 个相关源码文件与工作目录一致；`git diff --check` 通过。

验证覆盖不支持的平台/Node、错误元数据、CLI/清单损坏、宿主架构不匹配、启动/输出错误、模块缺失、
空 Skill 文件、实际超时终止、输出上限、Unicode 路径、隐私输出，以及不依赖完整包的帮助和参数校验。
归档回归在临时 npm 全局 prefix 和空缓存中执行离线安装，验证完整自检，并临时移除 Schema、
移除分发校验模块、破坏 MCP 入口语法及损坏 CLI，确认诊断仍可输出；随后执行原有 Skill/MCP 升级、恢复、卸载路径。

全新 Windows、真实 VC++ 运行库缺失、完全断网系统、真实 Agent 发现和模型行为尚未验收。
本功能提供验收入口，不把这些遗留项视为已关闭。公开 npm 发布、项目许可及来源核实仍待完成。
