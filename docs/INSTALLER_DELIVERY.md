# 本地 Skill 安装器交付

日期：2026-10-25。P4 首个安装器切片；[契约](INSTALLER_CONTRACT.md)、
[使用说明](../integrations/installer/README.md)。版本 `0.1.0-dev.1`，私有且未发布。

## 本轮完成

- Windows / Node.js 源码入口，`setup`、`verify`、`uninstall`、`version`。
  选择 Codex/Claude/OpenCode、用户/项目范围；支持 `--dry-run` 与 `setup --verify`。
- 复制四个权威 Skill 文件，附加 CLI 路径绑定与所有权/哈希记录，不修改客户端主配置。
  Codex/OpenCode 共用兼容目录。用户范围的 Claude 自定义配置目录明确拒绝。
- CLI 使用用户提供的本地 EXE，核对 dev.8/dev.9、Schema 1.6，并记录字节哈希。
  不捆绑或下载二进制。Skill 的可选安装记录不改变显式路径与环境变量的优先级。
- 重复安装不写重复内容；不同版本、路径、用户改动、额外文件、链接与未知目录均阻止覆盖/删除。
  staging + Windows rename 提交新目录，独占锁串行化安装器写操作，逐文件卸载。
- 可捕获失败会清理新安装/恢复卸载前内容；并发改动导致回滚不完整时保留改动并报告。
  强制终止/断电恢复、自动升级与任意外部并发写入不在本轮保证范围。
- 本地安装目录、staging、锁与日志通过忽略规则留在本机。

## 实际验证

Windows，Node.js `22.22.3`，CLI `2.0.0-dev.9` / Schema `1.6`。
没有调用模型 API，没有修改日常客户端的 Skill 或配置。

| 命令/检查 | 结果 |
| --- | --- |
| `npm test`，设置 `DLL_INSTALLER_TEST_CLI`（`integrations/installer`） | 16 项通过，0 跳过：三客户端 × 两范围、Unicode/空格路径、预览、重复安装、未知/修改文件保护、链接/锁、安装前后失败、CLI 验证失败、部分卸载恢复、并发编辑导致的回滚不完整、原生 CLI 入口与 PE 查询 |
| `python tests/installer_hosts.py --cli <cli> --codex <codex.exe> --opencode <opencode.exe>` | Codex `0.164.0` app-server 和 OpenCode `1.19.4` debug skill 实测安装后的项目发现、重复安装与卸载后缺失；无模型回合 |
| `npm test`（`integrations/pi`） | 3 项打包检查通过；权威 Skill 的改动保留了 Pi 包一致性与可移植性 |
| `python tests/skill_contract.py <cli> <noop_fixture.dll> <demo_target.exe> .` | 11 个 PowerShell 示例及失败路径通过 |

Windows x64 Release CI 新增安装器测试，并明确设置本次构建的 CLI 路径，避免原生检查被跳过。
安装器的 `verify` 只确认文件/CLI，不等同于客户端发现。Claude 和用户级真实发现未验证；
用户级目录行为已在隔离 home fixture 中覆盖，不使用或改写真实 HOME/CODEX_HOME。

## 后续

新版 CLI 尚无可验证的 Release 分发清单，不能把现有旧 EXE 当成安装资源。
继续推进发行包/哈希验证、客户端探测与 `auto/all` 确认选择、MCP 配置和统一 Pi 引导。
Pi 现阶段使用已交付的原生包入口。项目许可和公开 npm 发布仍待处理。
GUI 由并行分支开发，本轮没有修改 GUI 或原生诊断接口。
