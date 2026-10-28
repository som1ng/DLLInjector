# 路线图状态与遗留项

核对日期：2026-10-27；功能基线：main `31128b5`（[PR #25](https://github.com/som1ng/Rondo/pull/25)）。
对照原始 P0–P5 计划、当前代码、交付记录和远端发布情况整理。
**原计划尚未全部完成：只读诊断与 Agent 本地接入主链已交付，P4 分发安装尚未闭环，P5 未开始。**

本页汇总当前状态；各 `*_DELIVERY.md` 是对应提交时的历史记录，其中的“下一步”可能已由后续 PR 完成。
不要据旧记录重复开发，也不要把实现、客户端发现、模型行为评测和公开发布混为同一验收结果。

后续更新：按用户的 npm 全局安装要求，已实现 [Rondo npm 归档](NPM_DELIVERY.md)，由 npm 托管 CLI/MCP/Skill
和依赖，并提供 `rondo` 命令；不再优先另建一份用户目录运行库。下表保留 `31128b5` 时的基线，
其中“用户须另行准备 CLI/MCP”现在只适用于源码安装入口。公开 registry 发布、许可、升级恢复等缺口仍保留。

2026-10-28 更新：[用户级 MCP](USER_MCP_DELIVERY.md) 已实现单客户端安装、校验、卸载以及 auto/all 安装，
测试使用隔离用户目录。下表的用户级 MCP 是原基线待办；实际客户端用户级自动发现与模型行为仍未验收。

2026-10-28 更新：[配置升级](CONFIG_UPGRADE_DELIVERY.md) 已提供单客户端 Skill/MCP 的显式 `upgrade`，
包含新运行文件校验、配置/收据更新和捕获失败回滚。下表“必须卸载后重装”为历史基线；
批量升级、旧二进制回退和强制结束/断电恢复仍待实现。

2026-10-28 后续更新：[批量升级](BATCH_UPGRADE_DELIVERY.md) 已提供 Skill/MCP 的 `upgrade --agent all`，
包括全部目标的文件/所有权预检、共享 Skill 去重和后续目标失败时的配置恢复。
旧二进制回退和强制结束/断电恢复仍未实现；上段批量升级待办已关闭。

2026-10-28 后续更新：[Skill 升级中断恢复](SKILL_RECOVERY_DELIVERY.md) 已提供单目标 `recover`，
验证了升级/恢复进程被强制终止后的恢复与重试。MCP、安装/卸载、整批事务恢复和物理断电仍未验收；
不能把这个范围有限的恢复入口视为全部中断/断电恢复已完成。

## 阶段状态

| 阶段 | 已交付 | 尚未关闭 |
| --- | --- | --- |
| P0 基线与工程 | 源码审计、基础缺陷修复、CMake/CTest、Windows CI、宣传及许可声明校正 | 历史实验后端的解析/执行结果与资源生命周期限制仍在；项目级许可和部分来源待核实 |
| P1 Core + CLI | Result/Error、只读进程和模块查询、架构判断、JSON Schema、错误码、Unicode、超时与输出参数、x86/x64 构建和跨位数测试 | 不能把只读 Core 的契约视为历史实验方法已完成同等改造；ARM64/旧系统实机未验证 |
| P2 Doctor + 报告 + Skill | PE 头部/导入/导出/延迟导入、静态诊断、显式目录依赖候选、JSON/Markdown/HTML、保存报告离线转换、权威 Skill 和 22 条评测规格 | 真实模型自动触发与回答评测未做，阶段的 Agent 行为验收尚未完成 |
| P3 MCP | 本地 stdio、7 个只读工具、参数/路径/进程访问约束、超时与输出限制、真实客户端工具发现和调用 | Inspector 图形界面手验、全系统断网验收、公开分发未完成；真实宿主检查使用显式加载配置和本地模型 fixture |
| P4 跨 Agent 安装 | 本地 Skill 项目/用户安装；项目 MCP；确认选择 `auto`；批量 `all`；检查、卸载、捕获失败回滚；Pi 原生本地包；CLI 归档与清单校验 | 用户无需准备 CLI/服务文件的一行安装仍未实现，详见下表 |
| P5 Testing Lab | 已有构建/测试 fixture 可作为基础 | Lab 命令、受控生命周期协议、事件时间线、结果对比和 CI 报告尚未开发；已有 smoke 测试不等于 Lab 交付 |

主要证据：[基线](BASELINE.md)、[P0](P0_DELIVERY.md)、[P1](P1_DELIVERY.md)、
[模块](MODULES_DELIVERY.md)、[PE 表](PE_TABLES_DELIVERY.md)、[Doctor](DOCTOR_DELIVERY.md)、
[依赖](DEPENDENCIES_DELIVERY.md)、[报告](REPORTS_DELIVERY.md)、[离线报告契约](SAVED_REPORTS_CONTRACT.md)、
[Skill](SKILL_DELIVERY.md)、[MCP](MCP_DELIVERY.md)、[真实 MCP 宿主](MCP_HOST_VALIDATION.md)。

## P4 还缺什么

| 项目 | 当前情况 | 剩余工作 |
| --- | --- | --- |
| CLI 归档与校验 | x86/x64 CI ZIP、SHA-256、manifest；可在执行前检查本地 CLI | 托管运行文件到用户目录并记录所有权/版本；目前安装器只绑定用户提供的路径 |
| 自动下载与一条命令安装 | 必须自行准备 CLI、Node 和构建后的 MCP 服务 | 建立匹配当前 CLI 的发行来源，再接入架构选择、下载校验及失败处理；MCP 服务也需可分发产物 |
| 用户级 MCP | 只支持 `--scope project` | 单独实现用户配置适配、已有字段保留、检查和卸载，按真实客户端验证 |
| 更新与恢复 | 相同安装幂等；可捕获失败回滚；更换 CLI/路径/内容需要卸载后重装 | 原地升级与恢复流程；强制结束/断电恢复。批量安装没有跨目录原子性保证 |
| Pi | 原生包本地安装、资源发现、卸载及用户/项目范围验证 | 公开包、CLI 前置准备引导，以及完整 AgentSession 行为评测；可选 Extension 没有作为必需功能开发 |
| 新机器验收 | CI 和开发机测试通过 | 全新 Windows、VC++ 运行库缺失、离线首次安装等环境验收 |

截至核对日期，已发布 Release 仍是 [旧版 `injector_1.0`](https://github.com/som1ng/Rondo/releases/tag/injector_1.0)，
资产为 `Injector_x64.exe` / `Injector_x86.exe`，不能作为当前诊断 CLI 的下载源。
CI 归档可用于已知版本的测试分发，不等于正式 Release 或签名发行版。
安装器、MCP、Pi 包均仍为 `private` / `UNLICENSED`；仓库没有项目级 LICENSE。
应先核实来源并确定许可，再完成公开分发准备；本次没有选择许可证、创建 Release 或发布 npm 包。

证据：[安装器说明](../integrations/installer/README.md)、[CLI 分发](CLI_DISTRIBUTION_DELIVERY.md)、
[项目 MCP](MCP_INSTALLER_DELIVERY.md)、[客户端选择](AGENT_SELECTION_DELIVERY.md)、
[批量 Skill](BATCH_SKILL_DELIVERY.md)、[批量 MCP](BATCH_MCP_DELIVERY.md)、[Pi](PI_PACKAGE_DELIVERY.md)。

## 暂缓或未验证的项目

- **模型行为**：22 条自然语言场景目前是评测输入与标准；未验证模型自主选 Skill、执行正确命令、解释证据和处理不可信报告内容。
  三个真实 MCP 客户端的工具调用测试使用本地固定模型响应，不是这一项的替代证明。
- **资源发现**：Codex/OpenCode 项目 Skill 发现已实测；Claude Skill 发现、三者用户级 Skill 发现仍未验证。
  Pi 已验证本地包和资源加载，但未创建完整 AgentSession。MCP 显式配置加载通过，不代表自动项目发现和信任流程通过。
- **GUI 遗留**：[UI 审计](UI_BUGS.md) 的 BUG-03 仍只部分修复，宿主执行函数同步阻塞，原子标志未解决排队请求问题；多显示器 DPI 切换未实机验收。
  BUG-19 的严格消息解析已完成，不应再次列作缺失功能。
- **旧实验方法**：第三方进程注入、反射式、部署与 Direct Syscall 的有效性没有本轮验证；旧解析和执行协议限制仍在。
  Direct Syscall 未纳入当前 CMake，现有 GUI 返回请求已提交不证明目标效果。
- **质量证据**：已有固定种子变异、截断输入和 ASan 检查；持续模糊测试、计划中的解析器覆盖率 80% 目标仍无完成证据。
- **发布文档**：中英文快速开始、CHANGELOG、兼容说明已有；SECURITY.md、CONTRIBUTING.md 及系统整理的故障排查说明尚待补齐。
- **明确边界**：ARM64、旧系统实机以及完整 Windows 加载器行为未支持或未验证。
  当前依赖候选检查不递归模拟 Known DLL/SxS/API Set、动态加载与实际搜索顺序；这是公开的诊断边界，不把候选存在说成 DLL 可加载。
  SARIF/JUnit 在原计划中属于后续扩展，现有 JSON/Markdown/HTML 已可用。

补充证据：[Skill 兼容记录](AGENT_COMPATIBILITY.md)、[资源发现](AGENT_DISCOVERY_DELIVERY.md)、
[GUI 消息解析](GUI_MESSAGE_DELIVERY.md)、[PE 检查范围](PE_DELIVERY.md)、[第三方声明](THIRD_PARTY_NOTICES.md)。

## 后续优先顺序

1. **CLI 本地运行文件托管**：从用户明确提供的可信 CLI 分发文件开始，执行清单校验，安装到明确的用户目录，
   记录版本和所有权，支持检查及仅卸载本工具资源。复用现有安装器，再接 Skill/MCP。
   验收覆盖中文路径、重复安装、损坏清单/文件、失败回滚和保留用户文件。这一功能尚未实现。
2. **分发与下载**：来源/许可核实，准备当前 CLI 与 MCP 的版本化发行产物，再让下载与离线入口共用校验和托管流程。
   验收包括新机器、无网络、下载中断、哈希不符、架构/运行库不兼容及安装后诊断。
3. **P4 剩余能力**：用户级 MCP、升级恢复各自独立分支实现；不要用“项目批量配置完成”代替整阶段验收。
4. **补齐验收**：继续 Claude Skill/用户级发现、真实模型场景评测、GUI 遗留和发布文档。
   这些可在各自工作区推进；真实模型评测如需外部 API，先明确调用范围与费用，不使用本地 fixture 冒充。
5. **P5 Lab**：在诊断与分发主链稳定后，再做仅面向自有/明确授权测试程序的独立实验协议。

## 本次核对的验证范围

本次只核对和更新文档，不重新运行全部功能测试。`31128b5` 合入前的验证记录包括安装器 74 项、GUI 17 项、
三个真实 MCP 客户端检查，以及 [PR CI](https://github.com/som1ng/Rondo/actions/runs/40490097846) 的四组 Windows 构建和跨位数测试。
Copilot 因额度不足未完成 PR #25 评审；CI 通过不替代该评审，也不消除本页列出的未验收项。
