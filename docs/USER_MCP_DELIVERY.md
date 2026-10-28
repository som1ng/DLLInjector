# 用户级 MCP 交付记录

日期：2026-10-28。安装器 `0.1.0-dev.8`，Rondo npm 包 `0.1.0-dev.2`。
原生 CLI 仍为 `2.0.0-dev.9`，本轮没有修改原生分析能力或 GUI。

## 行为

`rondo setup --integration mcp --scope user` 支持 `codex`、`claude`、`opencode`、
`auto` 和 `all`。单客户端支持安装、预览、校验和卸载；auto 沿用确认选择，
all 沿用全部预检、明确指定三个原生客户端路径和失败回滚。verify/uninstall 仍须指定单个客户端。
用户级操作不接受 `--project`，文件读取根和进程查询权限仍须明确授权。

```powershell
# 已安装本地 Rondo npm 归档后，选择客户端并验证服务：
rondo setup --integration mcp --agent auto --scope user --allow-root 'D:\work' --verify
# 后续操作使用实际选择的客户端名：
rondo verify --integration mcp --agent codex --scope user
rondo uninstall --integration mcp --agent codex --scope user --dry-run
```

配置位置及完整参数见 [安装器说明](../integrations/installer/README.md#用户级-mcp-配置)。
Codex 使用 `~/.codex/config.toml`，也支持已存在的绝对 `CODEX_HOME`；
Claude Code 使用 `~/.claude.json` 顶层 `mcpServers`；
OpenCode V1 使用 `~/.config/opencode/opencode.jsonc` 或 `.json`，两者同时存在会报冲突。

## 所有权与保留行为

- 用户级收据为 `mcp-user-<agent>.json`，带 `scope: user`；无 scope 的旧收据只按项目级解释。
- 仅增删自有 `dll_doctor` 条目，保留其他服务、Claude 账户字段、项目记录和后来追加的无关字段。
- 项目和用户操作指向相同 Codex 配置时共用锁；另一 scope 的收据不能授权卸载。
- 配置原文只作内存中的并发比较和回滚快照，不复制到日志、结果或安装收据。
- 批量失败撤回本批新增项；此前已安装且未变化的条目继续保留。发生并发编辑时保留外部改动并报告未完成的回滚。

## 验证

在 Windows 本地执行：

- `npm test --prefix integrations/installer`：93 项通过，0 跳过；通过
  `DLL_INSTALLER_TEST_CLI` / `DLL_INSTALLER_TEST_MCP` 使用现有 x64 Release CLI 和已构建 MCP。
  其中 17 项用户级测试覆盖默认/自定义目录、已有和空配置、私有字段保留、scope 隔离、
  旧收据兼容、链接及覆盖配置拒绝、批量回滚、CLI 预览和真实 MCP 握手。
- `npm test --prefix integrations/npm`：7 项通过。
- `python -X utf8 tests/npm_assembly_tests.py`：4 项通过。
- `python -X utf8 tests/npm_package_tests.py --archive <dev.2.tgz> --codex <原生 Codex.exe>`：
  2 项通过。归档在空缓存、离线 registry 配置下全局安装并卸载；覆盖打包后的用户级预览、
  Codex 项目/用户级 MCP 安装—校验—卸载，以及已有 CLI、Skill、MCP 工具和篡改检测路径。
  原生客户端仅执行版本探测，MCP 服务由测试直接启动，未验证 Codex 自动加载。

文件系统测试使用独立临时 home；CLI/npm 测试只给子进程传入隔离的 Windows 用户目录，
并先断言 Node 实际解析到该目录。没有修改当前用户的客户端配置、信任、凭据或全局环境变量。
真实 MCP 握手只运行本机工具，没有调用外部模型 API。

## 边界

- verify 验证配置、版本、原生 CLI 和 MCP 服务握手，不证明原生客户端已自动发现用户配置或模型调用成功。
- 尚未验收 Claude `CLAUDE_CONFIG_DIR` 和 OpenCode `XDG_CONFIG_HOME`、`OPENCODE_CONFIG`、
  `OPENCODE_CONFIG_DIR`、`OPENCODE_CONFIG_CONTENT`；检测到非空值时明确拒绝用户级操作，不输出其值。
- 其他配置层可能覆盖用户设置；不修改客户端的信任和批准设置。
- 不包含原地升级、断电恢复或跨文件原子可见性保证。
- npm 仍是本地可安装归档，尚未公开发布；许可证和发布配置待确定。

## 配置依据

2026-10-28 核对官方文档：

- [Codex 环境变量](https://learn.chatgpt.com/docs/config-file/environment-variables)：`CODEX_HOME` 的默认值及目录已存在要求。
- [Codex MCP](https://learn.chatgpt.com/docs/extend/mcp)：用户和项目配置位置，以及项目可信要求。
- [Claude Code MCP](https://code.claude.com/docs/en/mcp)：用户级 `.claude.json` 和不同作用域。
- [OpenCode 配置](https://opencode.ai/docs/config/)：全局配置和覆盖方式。

这些来源用于确定文件位置与限制；实际客户端自动加载仍是独立的后续验收项。
