# Pi 原生包交付记录

日期：2026-10-25。范围：P4.2 的 Pi 原生 Skill 包本地安装路径。
契约见 [PI_PACKAGE_CONTRACT.md](PI_PACKAGE_CONTRACT.md)，使用步骤见
[包说明](../integrations/pi/README.md)。

## 实现

- `@som1ng/dll-doctor-pi` / `0.1.0-dev.1`，保留 `private: true` 和 `UNLICENSED`，未发布到 npm。
- 从唯一权威 Skill 生成四个字节一致的文件，Pi 清单显式指定 Skill 目录；无需运行时依赖或 Extension。
- `npm pack` 的文件清单只允许包元数据、README 与四个 Skill 文件进入归档。
  生成目录和归档已加入 `.gitignore`，不提交 CLI、依赖或验证日志。
- 使用 Pi 自带的 `install` / `remove` 管理本地目录引用，支持用户级和 `--local` 项目级配置。
  本地包目录必须保留；更新权威 Skill 后重新构建并在 Pi 中重新加载。

## 本地验证

Windows，Node.js `22.22.3`，npm `12.2.0`，Pi `1.4.0`；使用已有可信的
CLI `2.0.0-dev.9` / Schema `1.6` 与无副作用 fixture。没有重新安装依赖或调用模型 API。

| 命令 / 路径 | 实际结果 |
| --- | --- |
| `npm test`（`integrations/pi`） | 3 项通过：清单与字节一致性、重复构建与真实归档、拒绝输出目录链接；覆盖中文/空格/emoji 路径，未列入清单的本地文件保留且不进入归档 |
| `npm.cmd run verify-host -- --pi-package <installed-pi>` | 4 组通过：源码目录/独立解包目录 × 用户/项目范围；每组检查真实安装、重复安装单条引用、加载/提示清单、信任、过滤、卸载、保留其他配置 |
| `python tests/agent_discovery_tests.py` | 12 项通过 |
| `python tests/agent_discovery.py --pi-package <installed-pi>` | 原有普通 Skill 发现回归通过：信任项目、未信任项目、未安装负例 |
| `python tests/skill_contract.py <cli> <noop_fixture.dll> <demo_target.exe> .` | 11 个可移植 PowerShell 示例及失败路径通过；静态诊断、模块查询、依赖与报告输出均使用原生 CLI |

Pi 实测使用已有客户端的 CLI 和 `DefaultResourceLoader`，在临时目录中通过
`PI_CODING_AGENT_DIR` 隔离用户配置；不会修改真实用户的 Pi 设置。
项目级与用户级配置的其他字段/包引用在卸载后保持原值，另一个范围的配置文件在安装时保持字节不变。
卸载后 Skill 不再被发现，本地包文件仍保留。独立归档中的 Skill 与命令回归所用权威源字节一致。

Windows CI 的 x64 Release 作业新增 `npm test`，验证构建和真实归档；不下载 Pi 客户端。
真实宿主矩阵仍为显式选择已安装 Pi 的本地验证。

## 边界与下一步

这轮没有创建 AgentSession，也没有声称模型已主动选择 Skill 或完成诊断。
Skill 命令示例由测试执行，原生 Pi 发现由真实资源加载器验证，两者的证据范围分别记录。
本地目录安装验证不等于全新离线 Windows 验证；CLI 必须由用户单独提供。

公开 npm 发布、项目许可、CLI 分发与跨 Agent 自动安装/回滚尚未完成。
下一步继续通用安装器；GUI 与其他并行功能各自在独立分支开发。
