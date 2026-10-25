# Agent Skill 接入与验证记录

核验日期：**2026-10-24**。权威源只有
[`integrations/skills/windows-dll-doctor/`](../integrations/skills/windows-dll-doctor/SKILL.md)。
完整复制该目录即可保留相对引用。独立的 [本地 MCP 服务](../integrations/mcp/README.md) 已提供；
本页记录 Skill 接入，不代表 MCP 的 Agent 宿主兼容性实测。
2026-10-25 已补充可本地安装的 [原生 Pi Package](../integrations/pi/README.md)，
安装/卸载实测另见 [Pi 交付记录](PI_PACKAGE_DELIVERY.md)。
同日补充 [本地 Skill 安装器](../integrations/installer/README.md)，显式选择 Codex/Claude/OpenCode 和范围；
安装/回滚与真实项目发现记录见 [安装器交付](INSTALLER_DELIVERY.md)。自动下载和已发布 npm 包尚未提供。
MCP 的版本限定配置与真实宿主调用结果另见 [MCP 客户端实测](MCP_HOST_VALIDATION.md)。

## 官方发现规则与本机版本

| 客户端 | 本机 `--version` 结果 | 官方支持的项目目录 | 本轮验证范围 |
| --- | --- | --- | --- |
| Codex CLI | `0.164.0` | `.agents/skills/windows-dll-doctor/` | app-server `skills/list` 实测项目发现、repo scope、enabled 与未安装负例；未运行模型触发测试 |
| Claude Code | `2.1.310` | `.claude/skills/windows-dll-doctor/` | 版本、帮助和文档核对；发现与模型触发未验证 |
| OpenCode | `1.19.4` | `.opencode/skills/` 或 `.agents/skills/` 下的同名目录 | `debug skill --pure` 实测 `.agents/skills` 项目发现及未安装负例；未验证工具权限或模型选择 |
| Pi Coding Agent | `1.4.0` | `.agents/skills/windows-dll-doctor/`；也可显式 `--skill <path>` 或原生包安装 | 已安装包资源发现器实测项目发现、提示清单、未信任/未安装负例；原生包用户/项目安装另见 Pi 交付记录；未创建 AgentSession |

来源：[Agent Skills 规范](https://agentskills.io/specification)、
[Codex 官方 Skills 文档](https://learn.chatgpt.com/docs/build-skills)、
[Claude Code Skills](https://code.claude.com/docs/en/skills)、
[OpenCode Skills](https://docs.opencode.ai/docs/skills/)、
[Pi 官方 Skills 文档](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/skills.md)。
Codex 原文档地址 `developers.openai.com/codex/skills` 当前重定向到上述官方页面。
本机 CLI 的 `--help` 已运行；Pi 帮助确认 `--skill` 参数。

发现实测使用中文、空格路径下的临时 Git 项目，并从子目录查询；按规范化路径匹配
项目中的 Skill，避免用户级同名项造成误判。Codex 旧记录 `0.163.0-alpha.11.2`
只做过版本/帮助核验，本轮 `0.164.0` 是实际发现验证使用的版本。
**发现成功不代表模型已经自动选择或执行 Skill**。客户端信任配置、权限、上下文预算与模型可能影响行为。
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
Pi 的未信任项目测试不会加载项目 Skill；首次接入时按客户端正常流程确认你信任的项目，
验证脚本不会修改真实项目或用户配置中的信任状态。

可用请求示例：

> 检查 C:\lab\plugin.dll 为什么无法加载到我的 x64 程序。
> 依赖目录是 C:\lab\deps，请仅做静态检查，并说明证据和不能确定的部分。

Codex 可显式写 `$windows-dll-doctor`，Claude Code 可使用 `/windows-dll-doctor`，
Pi 官方提供 `/skill:windows-dll-doctor`。这些是官方调用方式，不是本轮实测的触发成功记录。

## 重复运行发现验证

维护者可显式运行 [`tests/agent_discovery.py`](../tests/agent_discovery.py)。它不安装客户端，
只测试指定的已有安装；未指定的客户端不计为通过。需要 Windows、Python、Git；Pi 检查另需 Node。
将下列路径替换为实际安装位置（Codex / OpenCode 必须使用原生 `.exe`，不能传 npm 的 `.ps1` / `.cmd` 启动脚本）。

```powershell
python tests/agent_discovery.py --codex 'C:\tools\codex\codex.exe' --output .\build\codex-discovery-new.json
python tests/agent_discovery.py --opencode 'C:\tools\opencode\opencode.exe'
python tests/agent_discovery.py --pi-package 'C:\tools\node_modules\@earendil-works\pi-coding-agent' --node 'C:\Program Files\nodejs\node.exe'
```

三个选项可合并到一次调用；默认每次操作超时 30 秒，可通过 `--timeout 1..120` 调整。
输出只含所选客户端版本、检查项目和 `model_evaluation: not_run`；`--output` 拒绝覆盖。
显式指定但缺失/不兼容的客户端、无效响应、发现失败或超时均返回非零。
临时项目在结束时清理；用户安装的其他 Skill 不会写入摘要。客户端可能自行写运行缓存/日志。
不改写 HOME 或 CODEX_HOME，不使用现有账户的凭证发起模型评测。

接口依据：[Codex app-server](https://learn.chatgpt.com/docs/app-server)、
[OpenCode CLI](https://docs.opencode.ai/docs/cli/)，以及本机 Pi `1.4.0` 包中的
`DefaultResourceLoader` / `SettingsManager.inMemory` / `formatSkillsForPrompt`。
Pi 检查用到已安装包的模块路径，版本变动可能需要调整适配；不能据此承诺所有 Pi 版本可用。

## 已做验证与未做验证

- 包内 Markdown 相对链接检查；包复制到源码树之外后执行引用中的命令。
- 三个客户端的上述发现检查通过；12 项轻量判定测试进入 CTest / CI。
  轻量测试不需要安装客户端，它们的通过不代替本机实际发现记录。
- 11 组 PowerShell 示例直接调用真实 CLI；验证 JSON Schema、Unicode/空格/`&` 路径、
  静态输入保持不变、受支持进程查询、架构不匹配、损坏/缺失文件、报告拒绝覆盖、
  失败诊断转换为 HTML 后源 JSON 保留。
- 22 条中英文评测规格见 [`tests/skill_scenarios.json`](../tests/skill_scenarios.json)，
  包含正例、负例、错误语义和不可信报告。只验证了用例结构，**没有运行模型行为评测**。
- 本轮没有向外部模型发送评测输入或消耗模型 API 额度；没有修改本机各客户端配置。

Skill Creator 随附的 `quick_validate.py` 使用较旧的字段白名单，拒绝规范允许的
`compatibility`。保留该字段，以官方规范及 YAML 解析/长度检查核验；不把旧校验器失败
记录为通过，也不修改安装在用户机器上的校验器。

具体构建/测试与评审结果见 [Skill 交付记录](SKILL_DELIVERY.md) 和
[客户端发现交付记录](AGENT_DISCOVERY_DELIVERY.md)。
