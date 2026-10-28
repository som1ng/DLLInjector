# Agent 配置原地升级

日期：2026-10-28。安装器 `0.1.0-dev.9`，Rondo npm 包 `0.1.0-dev.3`。
原生 CLI 仍为 `2.0.0-dev.9`，本轮不修改 GUI、原生分析功能或客户端信任设置。

## 命令与契约

`rondo upgrade` 更新一个已受管的 Skill 或 MCP 安装，支持
`--agent codex|claude|opencode` 和 `--scope project|user`。
普通 setup 继续对不同绑定/内容报冲突；upgrade 对缺失、手动安装、用户修改或损坏的记录报错。
原有 auto/all 仅用于 setup。Codex/OpenCode 共享的 Skill 由任一客户端升级一次即可。

```powershell
# 更新 npm 包后，先预览已有项目 Skill 的更新目标：
rondo upgrade --agent codex --scope project --project 'D:\work\my-app' --dry-run
rondo upgrade --agent codex --scope project --project 'D:\work\my-app'
# 用户 MCP 使用当前 npm 包内的 CLI/服务；明确客户端和完整权限范围：
rondo upgrade --integration mcp --agent codex --scope user --client 'D:\tools\codex.exe' --allow-root 'D:\work'
rondo verify --integration mcp --agent codex --scope user
```

源码入口需显式提供 `--cli`，MCP 还需 `--server`；可选 `--manifest` 仍在执行前检查。
MCP 要求重新提供客户端和至少一个 `--allow-root`，只在明确传入 `--allow-processes` 时启用进程查询。
升级不隐式继承旧权限，也不更改系统 PATH、环境变量或客户端信任/批准设置。

`--dry-run` 不写文件或执行程序，不承诺新运行文件可执行。
实际升级探测新 CLI，MCP 还检查原生客户端版本和服务握手；写入后必须再次校验，无需 `--verify`。
旧 CLI/服务已删除或原路径字节已变化不阻止重新绑定，只要原 Skill/配置与所有权收据仍一致。
相同内容与运行文件返回 `unchanged`，成功更新返回 `upgraded`。

## 修改与恢复边界

- Skill 仅替换原来受管的文件，收据最后写入；保留其他 Skill、报告和客户端设置。
  新版本的权威 Skill 内容和绑定一起更新。
- MCP 原位替换自有 TOML 块或 JSON/JSONC 值，保留无关字段和注释。
  继承合法所有权，但不会将后来添加的用户字段重新归为“本工具新建文件”；升级后的卸载仍保留这些字段。
- 沿用现有锁和 scope 隔离。每次替换和回滚比较当前字节，捕获写入/校验失败时恢复旧配置及收据。
  原始配置只保存在进程内存中，不把账户字段或配置原文复制到结果、日志、收据或备份文件。
- 并发编辑或权限问题可能阻止完整回滚，此时返回 `ROLLBACK_INCOMPLETE`，保留外部修改并要求检查现场。
- 回滚的是 Agent 配置和安装记录，不恢复已被 npm 替换/删除的旧 CLI、服务或整个 npm 包。
  若旧运行文件已不存在，恢复后的旧配置也可能暂时无法运行；需提供可信运行文件再显式升级。
- 不保证跨文件原子可见性，不包含批量升级、进程强制结束/断电恢复或自动清理残留锁。
  客户端自动加载与模型行为仍不属于 installer verify 的证明范围。

## 验证记录

Windows 本地执行：

- `npm test --prefix integrations/installer`：125 项通过，0 跳过。
  设置 `DLL_INSTALLER_TEST_CLI` / `DLL_INSTALLER_TEST_MCP`，复用已有 x64 Release CLI 与 MCP 构建。
  新增 32 项升级测试覆盖三个客户端、两种 scope、预览、旧运行文件消失/覆盖、权威 Skill 更新、
  共享目录、重复升级、私有字段保留、卸载所有权、无效清单、写入/校验失败恢复、并发编辑和真实服务握手。
- `npm test --prefix integrations/npm`：8 项通过，含 upgrade 转发、包内默认路径和显式路径保留。
- `python -X utf8 tests/npm_package_tests.py --archive <dev.3.tgz> --codex <原生 Codex.exe>`：
  2 项通过。空缓存及离线 registry 配置下安装/卸载完整归档，验证打包后 Skill 与用户 MCP
  迁到另一 CLI 路径、删除旧 CLI 后再升级回包内路径，以及后续 verify/uninstall。
  Codex 只执行版本探测；MCP 服务由测试直接握手，不声称客户端自动加载已验证。
- 本地 Markdown 链接/锚点 87 项和 `git diff --check` 通过。

测试只使用临时项目/用户目录；真实 MCP 验证直接启动本地服务，不调用模型 API，也不修改真实客户端配置。
配置位置与此前的路径支持范围继续遵循 [用户级 MCP 记录](USER_MCP_DELIVERY.md) 和
[安装器使用说明](../integrations/installer/README.md)。
公开 npm 发布仍待许可证和发布配置确定。
