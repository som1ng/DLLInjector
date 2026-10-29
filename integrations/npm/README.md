# Rondo npm 全局安装包

目标公开包名为 **`@som1ng/rondo`**，命令为 **`rondo`**。当前尚未发布到 npm，不能从公共仓库下载本项目的新包。
以下是本地构建/CI 归档的可验证安装入口：

```powershell
npm install -g .\som1ng-rondo-0.1.0-dev.8.tgz
rondo self-check
rondo --version --json
rondo doctor 'D:\samples\example.dll' --json
rondo setup --agent auto --scope project --verify
```

公开发布后的目标命令为 `npm install -g @som1ng/rondo`。包名/发布权限和项目许可证尚待发布前确认。
Windows PowerShell 若限制 `.ps1` 执行，可使用 `npm.cmd` / `rondo.cmd`；无需改变执行策略。

需要 Windows x86/x64、Node.js 22.19+ 和匹配构建工具的 Microsoft Visual C++ v14 运行库。
包内含 x64/x86 `rondo.exe`、各自校验清单、MCP 服务/Schema、权威 Skill、安装器和 Node 运行依赖。
按宿主机器架构选择 CLI；ARM64 不支持。原生 CLI 版本仍为 `2.0.0-dev.9`，npm 包版本独立管理。

## 安装后自检

```powershell
rondo self-check
rondo self-check --json
```

检查宿主 Windows 架构、Node.js 最低版本、npm 包元数据、当前架构 CLI 的清单/哈希及实际版本启动，
再启动安装器帮助入口、加载 MCP 模块/依赖/报告 Schema，检查四个必需 Skill 文件存在且非空。
CLI 校验失败会跳过该二进制启动，其余独立资源检查继续；宿主或包元数据不满足前置条件则跳过全部运行探测。
每个子进程限制为 10 秒及 1 MiB 输出，使用本地固定参数，不经 shell；原始子进程输出和异常内容不会复制到报告。

退出码：`0` 全部通过，`1` 有检查失败，`2` 参数错误。JSON 输出到 stdout，独立使用 `format: 1`，
包括 `checks` 中的 `passed` / `failed` / `skipped`、错误码和处理提示；不使用原生 CLI 的 Schema 版本。
CLI 启动失败只能说明版本探测未完成或结果不匹配，不能仅凭它确认 VC++ 运行库缺失。

自检不修改 Agent 配置、不发现/执行客户端、不启动 MCP 会话或调用模型 API。
通过只说明本机 npm 包的上述检查通过；不验证 Skill 语义、全部安装操作、MCP 宿主发现、真实缺运行库机器或物理断电。
这里只检查当前宿主选择的 CLI；另一位数的执行由发布 CI 验证。
详情与验证记录见 [自检交付说明](https://github.com/som1ng/Rondo/blob/main/docs/NPM_SELF_CHECK_DELIVERY.md)。

## Agent 与 MCP

安装 npm 包本身不改 Agent 配置。`setup` 默认使用包内 CLI、清单和 MCP 服务；
参数及权限范围沿用本地安装器。仍可显式传入 `--cli` / `--manifest` / `--server` 使用已有可信文件。

```powershell
rondo setup --agent all --scope project --dry-run
rondo setup --agent all --scope project --verify
rondo setup --integration mcp --agent auto --scope project --allow-root 'D:\samples' --verify
rondo setup --integration mcp --agent auto --scope user --allow-root 'D:\samples' --verify
rondo list-agents
rondo verify --agent codex --scope project
rondo uninstall --agent codex --scope project
# 直接启动本地 stdio MCP，诊断范围必须显式授权。
rondo mcp --allow-root 'D:\samples'
```

`rondo setup --help` 查看完整安装器选项。`rondo --help` 同时列出管理入口和原生 CLI 帮助；
`rondo --help --json` 保留原生 JSON 契约。原生子进程的参数、stdout/stderr 和退出码直接传递。
直接启动内置 CLI/MCP 前核对 CLI 清单；安装器还执行自己的版本和协议校验。
哈希是内容一致性校验，不是来源签名；使用可信构建的归档。

用户级 MCP 的配置位置、自定义目录支持范围和收据规则见
[安装器说明](https://github.com/som1ng/Rondo/blob/main/integrations/installer/README.md#用户级-mcp-配置)。`--scope user` 不能传 `--project`，也不会扩大授权目录。

npm 负责全局目录、命令 shim、包版本和运行文件的移除，不另复制一份用户运行库。
卸载 npm 包前，先对需要移除的各项目/用户 Agent 配置执行对应 `rondo uninstall`，再运行：

```powershell
npm uninstall -g @som1ng/rondo
```

npm 卸载不会扫描项目或删除 Agent 配置。更新 npm 包可能改变绑定的 CLI/服务/Skill；
已有安装的校验会报告不一致。对需要更新的每个安装显式执行 upgrade：

```powershell
rondo upgrade --agent codex --scope project --project 'D:\work\my-app' --dry-run
rondo upgrade --agent codex --scope project --project 'D:\work\my-app'
rondo upgrade --agent all --scope project --project 'D:\work\my-app'
rondo upgrade --integration mcp --agent codex --scope user --client 'D:\tools\codex.exe' --allow-root 'D:\work'
```

只更新已有的受管安装，保留无关配置；实际升级会校验新运行文件，捕获失败时恢复旧配置与收据。
MCP 需要重新明确完整允许目录和可选 `--allow-processes`；不隐式继承旧权限。
支持显式单客户端和 `--agent all`；批量升级要求所有目标已有受管安装，Codex/OpenCode 共享 Skill 自动去重。
批量 MCP 需分别传入 `--codex-client`、`--claude-client`、`--opencode-client`，并明确完整权限范围。
后续目标失败时恢复本批已改动的配置与收据；并发改动会保留并报告回滚不完整，见
[批量升级说明](https://github.com/som1ng/Rondo/blob/main/docs/BATCH_UPGRADE_DELIVERY.md)。
旧二进制回退和断电恢复仍未实现，详见
[升级边界](https://github.com/som1ng/Rondo/blob/main/docs/CONFIG_UPGRADE_DELIVERY.md)。

新版 Skill 升级若被强制终止，可先预览再恢复本机记录：

```powershell
rondo recover --agent codex --scope project --project 'D:\work\my-app' --dry-run
rondo recover --agent codex --scope project --project 'D:\work\my-app'
```

恢复不需要旧 CLI 仍存在，不执行 CLI/客户端；实际恢复需要 Windows PowerShell 获取排他文件锁。
上述入口处理新版 Skill 升级的单目标记录，不支持安装/卸载或整个批次，也不代表物理断电已验证，见
[恢复边界](https://github.com/som1ng/Rondo/blob/main/docs/SKILL_RECOVERY_DELIVERY.md)。
从 npm 包 `0.1.0-dev.6` 起，增加 `--integration mcp` 可恢复单客户端 MCP 升级，支持 project/user：

```powershell
rondo recover --integration mcp --agent codex --scope user --dry-run
rondo recover --integration mcp --agent codex --scope user
```

MCP 恢复记录只保存受管条目、收据和配置摘要；后来编辑的设置会阻止恢复并原样保留。
不运行 CLI、客户端或 MCP 服务；范围和限制见 [MCP 恢复说明](https://github.com/som1ng/Rondo/blob/main/docs/MCP_RECOVERY_DELIVERY.md)。

从 npm 包 `0.1.0-dev.7` 起，新版整批升级可使用 `recover --agent all`，同样支持 Skill/MCP 和 project/user：

```powershell
rondo recover --agent all --scope project --dry-run
rondo recover --agent all --scope project
rondo recover --integration mcp --agent all --scope user --dry-run
rondo recover --integration mcp --agent all --scope user
```

批次未提交时恢复参与目标，已提交时保留结果并完成清理；配置被后来修改时保留现场并报错。
不运行 CLI/客户端/MCP 服务，也不恢复旧二进制或旧版批次；见 [整批恢复说明](https://github.com/som1ng/Rondo/blob/main/docs/BATCH_RECOVERY_DELIVERY.md)。

没有 postinstall 下载或自动运行脚本；依赖已打入归档，可使用 `--offline --ignore-scripts` 安装此归档。
这不等于新机器已安装 Node/VC++ 运行库或已验证整个系统完全断网。

## 维护者打包

先构建 x64/x86 Release CLI 和 `integrations/mcp`。以下输出目录必须不存在：

```powershell
python integrations/npm/scripts/package_npm.py --x64 build/msvc-x64/Release/cli/rondo.exe --x86 build/msvc-x86/Release/cli/rondo.exe --output build/npm/stage
npm ci --prefix build/npm/stage --ignore-scripts --no-audit --no-fund
npm pack ./build/npm/stage --pack-destination build/npm --ignore-scripts
python tests/npm_package_tests.py --archive build/npm/som1ng-rondo-0.1.0-dev.8.tgz
```

CI 提供实际 checkout SHA 给 `--revision`；本地复用构建时省略，避免伪造来源。
只从完整 staging 打包，不对本源码目录直接执行 `npm pack`。
依据：[npm bin 与依赖打包](https://docs.npmjs.com/cli/v11/configuring-npm/package-json/)、
[npm 全局/归档安装](https://docs.npmjs.com/cli/v11/commands/npm-install/)。
