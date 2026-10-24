# Windows DLL Doctor Skill 交付记录

日期：2026-10-24。功能分支 `codex/p2-windows-dll-doctor-skill`，开始实现前已创建同名远端分支。
先提交 [契约](SKILL_CONTRACT.md)、22 条自然语言评测规格，再提交文档命令回归测试和实现。

## 本轮改动

- 单一权威 `integrations/skills/windows-dll-doctor/`：Skill、命令/证据参考与 Codex 展示元数据。
- 支持静态 Doctor、PE 细节、显式目录候选、只读进程/模块查询与离线报告转换的工作流。
  所有诊断复用 CLI；原生版本 `2.0.0-dev.8`、Schema `1.6` 保持不变。
- Windows/可信 CLI 检查、权限边界、非零退出码、证据来源、输出保护和不可信报告处理。
- 四类客户端的官方路径、本机版本核验及手动复制说明，见 [兼容性记录](AGENT_COMPATIBILITY.md)。
- `skill.contract` 加入 CTest，CI 的四个 MSVC 构建组合自动运行。
  测试将包移至源码树外，执行文档中的 11 组 PowerShell 示例；没有重新实现诊断逻辑。

## 本地实际执行

| 命令 | 结果 |
| --- | --- |
| `python -m py_compile tests/skill_contract.py` | 通过 |
| `python tests/skill_contract.py build/msvc-x64/Release/cli/dllinjector.exe build/msvc-x64/Release/noop_fixture.dll build/msvc-x64/Release/demo_target.exe .` | 11 组示例及失败路径通过 |
| `cmake --build --preset msvc-x64-release --parallel 4` | 通过 |
| `ctest --preset msvc-x64-release --output-on-failure` | 55/55 通过 |
| `cmake --build --preset msvc-x86-release --parallel 4` | 通过 |
| `ctest --preset msvc-x86-release -R '^skill.contract$' --output-on-failure` | 1/1 通过 |
| `cmake --build --preset mingw-x64 --parallel 4` | 通过 |
| `ctest --preset mingw-x64 -R '^skill.contract$' --output-on-failure` | 1/1 通过 |
| `git diff --check` | 通过 |

本机还运行了 Codex / Claude Code / OpenCode / Pi 的 `--version` 与 `--help`，
以及用例 JSON 结构检查、YAML 元数据/长度/命名和 Codex 展示字段检查。
接入文档的 PowerShell 复制示例已在临时项目中执行：全部文件逐字节一致，重复运行
拒绝覆盖已有同名 Skill；本轮修改文档的相对链接也已检查。
本轮未重复本地 Debug 全套测试或 sanitizer；没有改动原生解析/进程代码。
远端完整矩阵和评审状态以对应 PR 的实际 Checks / Reviews 为准。

测试只静态读取本次编译的 DLL，在自有 demo 进程上查询元数据；不加载旧二进制，
不向第三方进程注入。临时工作区会释放；构建/日志/本地报告继续位于忽略目录。

## 限制与后续

- 22 条场景是触发/回答评测输入和评分标准，**未运行真实客户端发现与模型评测**。
  文档示例测试不证明 Agent 能正确选择命令、抵御报告指令或给出正确答案。
- Skill Creator 的旧 `quick_validate.py` 实际返回失败：不认识官方规范允许的
  `compatibility`。保留规范字段并独立检查 YAML；未修改用户安装的工具，也未标为通过。
- 只提供手动复制接入，无自动更新或回滚安装器；未发布 Pi Package 或 MCP 服务。
- 下一步补真实客户端评测，再推进 CLI JSON 契约上的本地 stdio MCP。
