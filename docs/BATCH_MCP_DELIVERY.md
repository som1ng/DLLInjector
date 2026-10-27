# 批量项目 MCP 配置交付

日期：2026-10-27。安装器 `0.1.0-dev.6`；分支 `codex/p4-batch-mcp-setup`，
实现前从最新 main 创建并推送同名远端分支。

## 已交付

`setup --integration mcp --agent all --scope project` 为 Codex、Claude Code、OpenCode
一次配置三份项目 MCP 文件。三个原生客户端路径显式提供；共用已有 CLI、服务入口和目录授权。
支持全量预览、逐项版本/协议校验、重复安装和写入后 verify。

失败时逆序撤回新增条目，保留已有相同安装；配置未被再次编辑时恢复原始字节，
否则只移除本工具条目，保留后续无关字段。锁内核对托管收据，防止回滚删除后来替换的不同安装。
原始配置快照只在内存中使用，不写入输出、错误或收据。
详见 [使用说明](../integrations/installer/README.md#批量项目-mcp-配置) 和 [契约](BATCH_MCP_CONTRACT.md)。

## 本地验证

| 检查 | 结果 |
| --- | --- |
| `npm.cmd test --prefix integrations/installer`，设置真实 CLI/MCP 路径 | 74 项通过，0 跳过；新增 14 项批量 MCP 测试 |
| `python -X utf8 tests/mcp_hosts_tests.py` | 14 项通过 |
| `python -X utf8 tests/mcp_installer_hosts.py --all`，提供三个客户端与 CLI/MCP/DLL fixture 路径 | 三个真实客户端全部通过；每个发现 7 个工具并执行 4 项检查 |
| `node --check integrations/installer/src/batch-mcp.mjs` | 通过 |

安装器用例覆盖三种配置格式、中文/空格/emoji 路径、预览无写入/执行、客户端路径缺失、
重复安装、最后目标写入/验证失败、既有安装保留、原始字节恢复、后来无关字段保留、
并发有效安装保护、锁清理异常及私有配置字段不外泄。
单元套件中的原生协议检查使用真实 CLI/MCP 和模拟客户端版本；实际客户端检查单独运行。

真实宿主版本与加载方式沿用已经验证的适配器：

| 客户端 | 实测加载方式 |
| --- | --- |
| Codex 0.164.0 | 读取生成的 TOML，以隔离配置覆盖传入本地 app-server RPC |
| Claude Code 2.1.310 | 原生客户端显式读取生成文件，本地固定模型响应驱动工具调用 |
| OpenCode 1.19.4 | 原生客户端显式读取生成文件，本地固定模型响应驱动工具调用 |

每个客户端检查 `inspect_pe`、`read_report`、目录越界拒绝及错误后恢复，随后检查重复 setup、
verify 和逐个卸载三个批量目标。OpenCode 后来添加的 `$schema` 保持不变。
外部模型 API 调用为 **0**，没有修改用户真实客户端配置、凭据或信任设置。
本轮复用现有原生构建和 Node 依赖；未修改或重新编译 GUI/Core。
CI 继续执行全部 Windows 构建、测试与安装器测试；结果以对应 PR 为准。

## 边界

这里只验证显式配置加载，不宣称自动项目发现、项目信任批准或真实模型自主选工具已经通过。
批量入口限于项目级 setup；verify/uninstall 继续显式指定客户端。
不保证跨客户端原子可见性、强制终止/断电恢复或用户级 MCP；空父目录可能保留。
运行文件自动下载、用户级 MCP、公开 npm 发布和升级恢复仍待后续交付。
