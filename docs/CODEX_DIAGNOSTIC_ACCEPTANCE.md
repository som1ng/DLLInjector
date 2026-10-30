# Codex 架构冲突与报错 126 诊断验收

沿用 [Codex 安装与发现验收](CODEX_ACCEPTANCE.md) 的临时 npm 全局安装、8 项自检、
项目配置发现和卸载流程，增加两个真实模型场景。实现位于
[codex_acceptance.py](../tests/codex_acceptance.py) 和 [codex_diagnostics.py](../tests/codex_diagnostics.py)。
每次调用只选一个场景；不指定 `--scenario` 时仍运行原有 `pe` 场景。

## 场景和证据

| 场景 | 输入 | 必须保留的结论 |
| --- | --- | --- |
| `mismatch` | 本仓库构建的 x86 DLL；用户明确声明目标为 x64 | `ARCH_MISMATCH` 阻断；目标架构来自用户声明；CLI 退出码为 1，但诊断已完成；未验证加载 |
| `error-126` | 用户报告加载错误 126；本仓库 MSVC DLL；单个明确的依赖搜索目录 | 区分候选、限定目录内未找到和 API Set；可能缺失的置信度为 low；根因与可加载性未确认 |

提示词描述用户问题，不指定工具名，也不提供预写工具调用。模型自行选择工具，
最后按给定 JSON Schema 回答。字段类型和枚举是评分约定，不包含具体 DLL 的预期答案。
答案由实际安装后的原生 CLI 报告生成，再与模型的最终 JSON 逐字段核对。

验收要求至少一次对应的 Doctor 调用：架构冲突使用 `diagnose_dll`，依赖场景允许
`diagnose_dll` 或 `analyze_dependencies`。可先对主 DLL 调用 `inspect_pe`，但仅有 PE 证据不能通过。
核对目标绝对路径、参数范围、CLI 退出码、完整报告及文本/结构化内容一致性；只忽略观测耗时。
本场景的 DLL 名称忽略大小写和顺序，重复项仍拒绝；其他字段保留严格类型检查。

126 是 Windows 的 `ERROR_MOD_NOT_FOUND`，单凭错误码无法在本场景中确定哪个依赖是根因。
参见 [Microsoft 错误码说明](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-)。
测试**不实际加载 DLL，也不复现加载器错误**：126 来自提示词中的用户报告。
临时目录内的 `VCRUNTIME140.dll` 是固定名称的无害文本文件，用来区分“候选存在”和“候选可用”。
提示词明确限定本轮只检查主 DLL 和候选存在情况，不读取候选内容；其余依赖未复制到该目录。
目录内没找到 `KERNEL32.dll` 不表示 Windows 系统缺失它；API Set 也不能当作普通缺失文件。

## 运行方式

前置条件与隔离方式见 [基础验收说明](CODEX_ACCEPTANCE.md#运行方式)。
需要相应架构的 `noop_fixture.dll`；未构建时先配置对应 CMake preset，再构建 `noop_fixture` 目标。
从仓库根目录运行，替换已安装的 Codex 原生 EXE 路径：

```powershell
python tests/codex_acceptance.py `
  --archive .\build\npm-self-check\som1ng-rondo-0.1.0-dev.8.tgz `
  --codex 'C:\tools\codex\codex.exe' `
  --dll .\build\msvc-x86\Release\noop_fixture.dll `
  --scenario mismatch --output .\build\codex-mismatch-new.json

python tests/codex_acceptance.py `
  --archive .\build\npm-self-check\som1ng-rondo-0.1.0-dev.8.tgz `
  --codex 'C:\tools\codex\codex.exe' `
  --dll .\build\msvc-x64\Release\noop_fixture.dll `
  --scenario error-126 --output .\build\codex-error-126-new.json
```

默认只检查本地安装/发现及原生诊断基线，真实模型仍标记 `not_run`。
`native_preflight: passed` 不等于模型已经调用或正确解释工具。
显式添加 `--live-model gpt-6-astra` 才会使用现有登录运行一个真实回合，产生网络请求和账户额度消耗。
脚本不自动重跑整轮；客户端内部可能重试或发出多次模型请求，超时不是费用硬上限。

## 本轮验证记录

验证日期：2026-10-30。Codex `0.164.0`、模型 `gpt-6-astra`、npm `0.1.0-dev.8`、
CLI `2.0.0-dev.9` / Schema `1.6`。两项均完成 npm 安装、自检、四种配置发现状态及 MCP/npm 卸载。

| 场景 | 真实模型结果 | 自主调用 | 输入 tokens（其中缓存） | 输出 tokens |
| --- | --- | --- | --- | --- |
| `mismatch` | 通过，完整证据和最终答案匹配 | `diagnose_dll` 一次 | 77,421（44,032） | 280 |
| `error-126` | 通过，完整证据和最终答案匹配 | `analyze_dependencies` 一次 | 75,249（57,344） | 335 |

依赖场景核对到候选 `VCRUNTIME140.dll`、指定目录内未找到的 `KERNEL32.dll`，
以及 `api-ms-win-crt-runtime-l1-1-0.dll` API Set。模型保留 low 置信度和未确认根因/可加载性的结论。
表中计数仅来自这两个成功回合，不是整个调试过程的费用估算。此前一次会话中断未留下完整结果，
未计为通过，其用量也未计入表内。

- `cmake --build --preset msvc-x86-release --target noop_fixture`：成功。
- `python tests/codex_acceptance_tests.py`：17 项通过，含新增 7 项诊断证据回归测试。
- `python tests/mcp_hosts_tests.py`：既有 14 项通过。
- 两个不带 `--live-model` 的命令：通过，真实模型均保持 `not_run`。
- 两个显式启用真实模型的命令：通过；没有用本地模型 fixture 的固定响应冒充真实诊断。
- 受测归档 SHA-256：`4f789103e291e66c84e0e65bc0c7694a757963fd7eda999701a1fea6822947d5`。

## 验收范围

本轮检查的是工具证据与最终结构化答案，不对所有自由文本、思考过程或开放式对话做语义评分。
进程架构观察、不可访问目录、加载器真实搜索顺序和依赖递归加载不在这两个场景的覆盖范围内。
本机 Skill 发现保持关闭；这些场景参考既有 Skill 评测要求，但不代表 Skill 自动触发已验收，
也不代表全部 22 条场景、其他客户端或用户级配置发现已完成。
产品版本不因增加测试而改变；本地摘要、二进制和归档保留在已忽略的 `build/`，不上传原始模型记录或认证信息。
