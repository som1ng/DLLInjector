# Codex npm 安装与真实模型验收

本轮关注项目级 MCP：本地 npm 归档安装 → 自检 → 安装器写配置 → 客户端自动发现 → 模型选择工具并回答。
脚本为 [tests/codex_acceptance.py](../tests/codex_acceptance.py)，与使用直接 RPC/固定工具调用的
[既有宿主测试](MCP_HOST_VALIDATION.md) 分开记录。

## 运行方式

需要 Windows、已安装的 Node/npm、Python、Codex 原生 `.exe`、可信的本地 Rondo npm 归档，以及本仓库构建的
`noop_fixture.dll`。脚本不安装或升级 Codex，不加载/执行 DLL，也不下载 registry 包。
归档构建方法见 [npm 交付记录](NPM_DELIVERY.md)。从仓库根目录运行，替换客户端路径：

```powershell
python tests/codex_acceptance.py `
  --archive .\build\npm-self-check\som1ng-rondo-0.1.0-dev.8.tgz `
  --codex 'C:\tools\codex\codex.exe' `
  --dll .\build\msvc-x64\Release\noop_fixture.dll `
  --output .\build\codex-discovery-new.json
```

默认只连接本地模型协议 fixture，不做真实模型推理。它返回固定文本，不请求工具，
只观察客户端发送的工具清单。检查四种状态：未安装配置、安装后未信任、安装后已信任、卸载后。
只有已信任的已安装项目应向模型展示全部 7 个 Rondo 工具。
配置由 `rondo setup --integration mcp --agent codex --scope project ... --verify` 生成，
**没有把 `mcp_servers` 解析后再通过命令行覆盖传给客户端**。

真实模型验收需显式添加 `--live-model gpt-6-astra`，使用现有 Codex 登录和 OpenAI provider。
这会产生网络请求并消耗账户额度；脚本不接受或复制密钥，不改用其他付费 provider。
只运行一个新回合，脚本不自动重试整个验收；客户端内部沿用内置 provider 的重试策略。
该回合仍可能包含多次模型请求，超时不等于费用硬上限。
`--timeout` 限制每次 Codex 进程的整个事件流，默认 180 秒，可设 30–300 秒。
不带 `--live-model` 时，结果中始终明确标记真实模型 `not_run`。

真实场景用自然语言询问 DLL 架构、DLL 标志、映像大小、导出数量和是否验证可加载，
不给出工具名或预写工具调用。模型使用 JSON 输出格式，以便逐字段核对答案。
验收同时要求实际 `inspect_pe` 调用、正确的绝对文件路径、成功结果、文本与结构化结果一致，
以及与直接 CLI 输出一致的完整报告（仅忽略观测耗时）。仅答对数字而未调用工具不能通过。
答案必须保留静态检查的限制，不能把 PE 查询解释成已验证可加载。

## 隔离与结果边界

- npm 使用临时全局 prefix、空缓存、`--offline --ignore-scripts`；测试后卸载并清理自有临时目录。
- 真实客户端使用 `exec --ignore-user-config --ephemeral`，只通过进程参数信任本次临时项目。
  不修改用户配置或持久信任记录，不替换 HOME/CODEX_HOME；已有 CODEX_HOME 原样继承以定位登录。
- 关闭 shell、hooks、插件、apps、联网搜索、子代理和本机 Skill 发现，使用只读沙箱；
  MCP 仅允许 fixture 所在项目目录，不授权进程查询。
- JSONL 读取复用既有字节/期限限制；只终止本次创建的进程，失败不输出原始客户端日志或提供方错误。
  摘要保留归档摘要、客户端版本、已通过步骤和模型计数/核验事实，不保留认证、完整聊天和用户路径。
- 客户端自身可能访问服务端配置、刷新登录或写日志/缓存；这不是全系统断网或零落盘测试。
- Codex 0.164.0 在模型侧 Schema 中省略数值上下限。本脚本检查 7 个名称、对象参数结构及
  `additionalProperties: false`；服务端原始 Schema 和约束另由 MCP 协议/宿主测试覆盖，不能声称模型侧完整保留了上下限。
- 失败退出码为 1，参数错误为 2；提前校验输出父目录，输出文件使用独占新建，拒绝覆盖。
  运行结束先输出 JSON 到 stdout，随后文件写入即使失败，摘要和用量仍保留在 stdout，退出码为 1。
  stdout 写入/flush 失败也会继续尝试文件输出，并以非零状态退出。
  一次通过只证明该客户端版本和该场景，
  不代表全部 22 条 Skill 场景、其他客户端、用户级自动发现或全新 Windows 已验收。

Codex 项目配置与信任行为依据 [官方配置说明](https://learn.chatgpt.com/docs/config-file/config-basic)，
实际 CLI 选项以本机 `codex exec --help` 和 0.164.0 的观测为准。
CI 只运行 [离线证据检查](../tests/codex_acceptance_tests.py)，不运行真实模型。

## 本轮验证记录

验证日期：2026-10-29。npm 归档 `0.1.0-dev.8`，CLI `2.0.0-dev.9` / Schema `1.6`，Codex `0.164.0`。
产品版本不因新增验收脚本而升级。

- `python tests/codex_acceptance_tests.py`：10 项通过；错误目标/工具、缺少或伪造证据、错误答案/类型、
  错称可加载、额外活动、缺失/重复工具目录都不能被误判通过；输出失败不丢失摘要、不覆盖已有文件。
- `python tests/mcp_hosts_tests.py`：既有 14 项通过。
- 上述归档验收命令添加 `--live-model gpt-6-astra`：通过。实际执行离线 npm 全局安装、8 项自检、
  四种项目配置发现状态、一个真实模型回合以及 MCP/npm 卸载。
- 模型自主调用 `inspect_pe` 一次；完整报告与原生 CLI 一致，最终答案为 x64、DLL、28,672 字节、
  1 个导出、`loadability_verified: false`。该成功回合客户端报告输入 78,449 tokens（其中缓存 57,984），
  输出 206 tokens；这是最终成功回合的计数，不代表调试全过程总量或费用估算。
- 受测归档 SHA-256：`4f789103e291e66c84e0e65bc0c7694a757963fd7eda999701a1fea6822947d5`。

调试时发现并修正两项宿主兼容假设：模型侧 Schema 不保留数值上下限，内置 `openai` provider
不能重新定义。另通过进程参数抑制已知实验选项启动警告，验收仍拒绝真实诊断错误项。
首轮失败记录没有计为通过；本地摘要与探针位于已忽略的 `build/`，不上传完整事件、归档和 EXE。

此轮只关闭 Codex 0.164.0 的项目级安装/发现和单个真实 PE 问答场景。
Claude/OpenCode 的真实模型行为、用户级自动发现、全部 Skill 场景、新机器验收和公开 npm 发布仍待完成。

2026-10-30 CI 补充：既有批量恢复并发测试在原升级进程已关闭、前两次恢复已启动后，
第三次恢复仍偶发返回 `RECOVERY_ACTIVE`。新增失败阶段/PID 诊断，并仅在确认原进程已退出的测试步骤中，
对 `RECOVERY_BUSY` / `RECOVERY_ACTIVE` 最多重试 20 次、间隔 100 ms。
原进程活跃及并发恢复必须被拒绝的断言仍保留，运行时保护未修改。
这避免测试依赖“旧 PID 此后始终空闲”的假设，不证明已确定哪一个进程复用了 PID。
[Windows 的进程标识保证限于进程生命周期](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getcurrentprocessid)，
[Node 的 signal 0 仅检查当前存在性](https://nodejs.org/docs/latest-v22.x/api/process.html#processkillpid-signal)。
修改后运行 `npm test --prefix integrations/installer`：224 项通过、7 项跳过、0 失败；跳过项未计为通过。
