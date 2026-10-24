# Agent Skill 接入与验证记录

核验日期：**2026-10-24**。权威源只有
[`integrations/skills/windows-dll-doctor/`](../integrations/skills/windows-dll-doctor/SKILL.md)。
完整复制该目录即可保留相对引用；此轮没有 MCP、自动安装器、已发布 npm 包或原生 Pi Package。

## 官方发现规则与本机版本

| 客户端 | 本机 `--version` 结果 | 官方支持的项目目录 | 本轮验证范围 |
| --- | --- | --- | --- |
| Codex CLI | `0.163.0-alpha.11.2` | `.agents/skills/windows-dll-doctor/` | 版本、帮助和文档核对；未运行发现/模型触发测试 |
| Claude Code | `2.1.309` | `.claude/skills/windows-dll-doctor/` | 同上 |
| OpenCode | `1.19.4` | `.opencode/skills/` 或 `.agents/skills/` 下的同名目录 | 同上 |
| Pi Coding Agent | `1.4.0` | `.agents/skills/windows-dll-doctor/`；也可显式 `--skill <path>` | 同上；未验证包安装或扩展 |

来源：[Agent Skills 规范](https://agentskills.io/specification)、
[Codex 官方 Skills 文档](https://learn.chatgpt.com/docs/build-skills)、
[Claude Code Skills](https://code.claude.com/docs/en/skills)、
[OpenCode Skills](https://docs.opencode.ai/docs/skills/)、
[Pi 官方 Skills 文档](https://github.com/badlogic/pi-mono/blob/main/packages/coding-agent/docs/skills.md)。
Codex 原文档地址 `developers.openai.com/codex/skills` 当前重定向到上述官方页面。
本机 CLI 的 `--help` 已运行；Pi 帮助确认 `--skill` 参数。

这些路径来自当前官方文档；版本号、包格式或文档核对**不代表**某客户端已成功发现、
自动选择或执行了此 Skill。客户端信任配置、权限、上下文预算与模型可能影响行为。
此包保留默认自动选择策略，不声明客户端专属工具权限，不要求 MCP。

## 手动接入

先按仓库 README 构建当前 CLI，或取得来源可验证的同版 CI 产物。
x64 Windows 推荐 x64 CLI；当前源码构建位置为
`build/msvc-x64/Release/cli/dllinjector.exe`（Debug 对应改名）。
上层 `DLLInjector.exe` 为 GUI，旧 `bin/` 不用于此 Skill。

在仓库根目录运行下列 PowerShell，将目标项目路径替换为你的项目。
Codex / OpenCode / Pi 可选 `.agents/skills`，Claude Code 改用 `.claude/skills`。

```powershell
$skillSource = (Resolve-Path -LiteralPath '.\integrations\skills\windows-dll-doctor').Path
$projectDirectory = 'C:\work\my-app'
$skillDestination = Join-Path $projectDirectory '.agents\skills\windows-dll-doctor'
if (-not (Test-Path -LiteralPath $projectDirectory -PathType Container)) {
    throw '目标项目目录不存在'
}
if (Test-Path -LiteralPath $skillDestination) {
    throw '已有同名 Skill；先比较内容，不覆盖'
}
New-Item -ItemType Directory -Path (Split-Path -Parent $skillDestination) -Force | Out-Null
Copy-Item -LiteralPath $skillSource -Destination $skillDestination -Recurse
# 按实际当前构建位置修改；在同一终端中启动客户端以继承这个环境变量。
$env:DLLINJECTOR_CLI = (Resolve-Path -LiteralPath '.\build\msvc-x64\Release\cli\dllinjector.exe').Path
```

从目标项目启动/重新加载客户端；桌面应用不一定继承另一个终端设置的环境变量，
此时在请求里直接提供可信 CLI 的绝对路径。只复制 `SKILL.md` 会丢失参考文件。
安装副本需要更新时从权威源重新比对；不要分别维护四份不同内容的 Skill。

可用请求示例：

> 检查 C:\lab\plugin.dll 为什么无法加载到我的 x64 程序。
> 依赖目录是 C:\lab\deps，请仅做静态检查，并说明证据和不能确定的部分。

Codex 可显式写 `$windows-dll-doctor`，Claude Code 可使用 `/windows-dll-doctor`，
Pi 官方提供 `/skill:windows-dll-doctor`。这些是官方调用方式，不是本轮实测的触发成功记录。

## 已做验证与未做验证

- 包内 Markdown 相对链接检查；包复制到源码树之外后执行引用中的命令。
- 11 组 PowerShell 示例直接调用真实 CLI；验证 JSON Schema、Unicode/空格/`&` 路径、
  静态输入保持不变、受支持进程查询、架构不匹配、损坏/缺失文件、报告拒绝覆盖、
  失败诊断转换为 HTML 后源 JSON 保留。
- 22 条中英文评测规格见 [`tests/skill_scenarios.json`](../tests/skill_scenarios.json)，
  包含正例、负例、错误语义和不可信报告。只验证了用例结构，**没有运行模型行为评测**。
- 本轮没有向外部模型发送评测输入或消耗模型 API 额度；没有修改本机各客户端配置。

Skill Creator 随附的 `quick_validate.py` 使用较旧的字段白名单，拒绝规范允许的
`compatibility`。保留该字段，以官方规范及 YAML 解析/长度检查核验；不把旧校验器失败
记录为通过，也不修改安装在用户机器上的校验器。

具体构建/测试与评审结果见 [本轮交付记录](SKILL_DELIVERY.md)。
