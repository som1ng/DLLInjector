# Rondo npm 全局安装与命名交付

2026-10-27，分支 `codex/rondo-npm-package`，从最新 main 建立并在实现前推送同名远端。
按用户要求，下载包和公开 CLI 命令统一为 Rondo。采用 npm 管理运行文件，替代另建用户运行库的优先方案。

## 已实现

- 完整归档 `som1ng-rondo-0.1.0-dev.1.tgz`，包名 `@som1ng/rondo`，全局入口 `rondo`。
  包含 x64/x86 CLI、清单、已构建 MCP/Schema、权威 Skill、安装器和固定依赖；无安装生命周期脚本。
- `rondo` 转发原生参数、标准流和退出码；`setup/verify/uninstall/list-agents` 使用已有安装器，
  setup 默认提供包内 CLI、清单和 MCP 路径；`rondo mcp` 可直接启动内置 stdio 服务。
  原生执行前校验 CLI 内容与架构；不从 manifest 执行任意路径。用户显式路径仍由原有安装器/服务验证。
- 原生主产物改为 `rondo.exe`，帮助与当前快速开始同步。保留 `dllinjector.exe` 镜像兼容旧脚本。
  新独立 ZIP 使用 `rondo-cli-*` 和 `rondo-cli` manifest；安装器同时兼容旧产品/文件名成对的 manifest。
- 未公开的适配包名同步为 `@som1ng/rondo-installer`、`@som1ng/rondo-mcp`、`@som1ng/rondo-pi`。
  现有 Skill ID、MCP 服务 ID、配置条目和收据标识保留，避免名称迁移破坏已有安装。
  Skill 优先识别 `RONDO_CLI`，仍兼容 `DLLINJECTOR_CLI`。
- 新 CI 作业组装完整包、真实打包、在隔离 prefix 中离线全局安装/运行/卸载，并提供 `rondo-npm` 产物。
  独立 CLI 产物更名为 `rondo-cli-msvc-x64` / `rondo-cli-msvc-x86`。

版本：npm 包 `0.1.0-dev.1`，安装器 `0.1.0-dev.7`；原生行为/Schema 版本仍为 CLI `2.0.0-dev.9` / `1.6`。
使用、打包和卸载顺序见 [npm 包说明](../integrations/npm/README.md)。

## 本地实际验证

| 检查 | 结果 |
| --- | --- |
| `cmake --build --preset msvc-x64-release --parallel 4` | 全部目标构建通过，主 CLI 和兼容镜像生成 |
| `cmake --preset msvc-x86`，使用既有 jsoncons 目录；`cmake --build --preset msvc-x86-release --target dllinjector_cli --parallel 4` | x86 CLI 构建通过；未声称本地跑过 x86 全套 CTest |
| `ctest --preset msvc-x64-release --output-on-failure` | 首次 58/59；唯一失败是旧帮助文本断言，改为 `rondo inject` 后定向复测 `cli.inject` 通过 |
| `npm.cmd test --prefix integrations/installer`，设置真实 CLI/MCP 路径 | 76 项通过，0 跳过；含新旧清单兼容 |
| `npm.cmd test --prefix integrations/npm` | 7 项通过：平台选择、缺失/损坏文件、命令路由、显式路径和参数保留 |
| `python -X utf8 tests/npm_assembly_tests.py` | 4 项通过：打包内容限制、无生命周期脚本、拒绝已有目标、缺失资源/依赖漂移预检 |
| `python -X utf8 tests/cli_distribution_tests.py --cli <rondo.exe> --architecture <arch>` | x64/x86 各 6 项通过，真实新名称 ZIP 解包及安装检查 |
| `npm.cmd run build --prefix integrations/mcp` | 通过 |
| `npm.cmd test --prefix integrations/pi` | 3 项通过，新包名和更新后的权威 Skill 归档一致 |
| `package_npm.py`、staging `npm ci`、`npm pack` | 实际生成完整归档，最终约 6.45 MB；staging 不携带源码测试、私有配置或日志 |
| `python -X utf8 tests/npm_package_tests.py --archive <tgz> --codex <原生 EXE>` | 2 项端到端测试通过，覆盖下述真实路径 |

全局安装实测在中文/空格/emoji 临时 prefix 中执行，使用空 npm 缓存、`--offline --ignore-scripts`
和不可用 registry 地址。验证 npm 命令 shim、两种原生 CLI 启动、帮助/错误码、含特殊字符路径的 PE 检查、
报告输出、Skill 批量预览/安装/重复安装/检查/卸载，最后 npm 卸载保留无关项目文件。
篡改包内 CLI 后，启动前拒绝且 stdout 不混入诊断错误。

通过包内 SDK 启动 `rondo mcp`，发现 7 个工具，检查 PE、读取报告、拒绝目录越界并在错误后恢复。
另使用本机 Codex 0.164.0 的版本探测，执行包装命令的 MCP setup/verify/uninstall，验证默认包内路径和服务握手。
这不是新的 Codex 模型行为或自动信任验收。CI 不安装 Codex；其余归档检查在 CI 执行。
所有 Agent 写入限于临时项目，没有修改真实用户配置、凭据或信任；外部模型 API 调用为 0。

## 剩余限制

- 本次未发布到 npm、未创建 Release、未选定许可证；包仍 `private: true` / `UNLICENSED`。
  `npm install -g @som1ng/rondo` 是公开发布后的目标命令，当前可用的是 `npm install -g <本地 tgz>`。
  发布前还需核实来源、许可证、npm 包名/权限及发布认证。
- npm 负责其全局文件，不扫描或卸载其他项目的 Agent 配置；应先显式卸载 Agent 配置再移除全局包。
  npm 更新可能让已有 CLI/服务/Skill 绑定失效，需显式重新安装；原地升级和断电恢复仍未实现。
- Node.js 与 VC++ v14 运行库必须已具备；ARM64、全新/缺运行库 Windows 和全系统断网尚未实测。
- CLI/MCP JSON 与诊断算法未改变；GUI 风格和原有实验方法未修改。本次没有执行第三方进程操作。

打包依据：[npm package.json](https://docs.npmjs.com/cli/v11/configuring-npm/package-json/)
的 bin / bundleDependencies 与 [npm install](https://docs.npmjs.com/cli/v11/commands/npm-install/) 的归档/全局安装。
