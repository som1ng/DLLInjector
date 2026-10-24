# Markdown / HTML 报告交付

日期：2026-10-24。分支：`codex/p2-report-export`。CLI `2.0.0-dev.7`，JSON Schema 仍为 `1.5`。

## 完成内容

- process list/inspect/modules、pe inspect、doctor 新增 `--format markdown|html`，与 --json 互斥。
  stdout 与 --output 保存相同 UTF-8 字节，继续拒绝覆盖已有文件。
- 文档展示执行状态、退出码、目标、证据来源、Doctor 静态图像/发现/候选观察及错误，附完整 JSON。
  格式化只读取已有 Report，不重复执行查询，不增加 CLI 运行时依赖。
- 抽取公用 ReportEvidence，保持 JSON 与文档证据描述一致。Doctor 阻断发现仍退出 1，
  outcome_verified 仍为 false；保存失败清空原成功数据后输出所选格式的错误报告。
- HTML 带离线样式、打印样式和 CSP，不含脚本、外部资源或活动链接；动态值只进入文本节点。
  Markdown 字段/代码块使用长于内容反引号串的分隔符，避免名称、路径或描述成为活动标记。
- 12 个内存 fixture 和真实 CLI 导出测试覆盖数据完整性、错误语义、UTF-8、输出保护及恶意标记。
  CommonMark 解析器只用于测试，固定使用现有环境的 markdown-it-py 2.2.0；本地没有重新安装依赖。
- 新增报告契约及本交付文档，更新中英文 README、CHANGELOG 和 CI 审查产物。

主要文件：`apps/cli/document.cpp`、`cli.h`、`parser.cpp`、`main.cpp`、`report.cpp`，
`tests/report_fixtures.cpp`、`tests/reports_contract.py`、`tests/requirements.txt` 及 CMake 注册。
历史 JSON schema/磁盘 fixture 未改；现有契约测试只更新当前工具版本适配。
实现前已建立同名本地/远端分支，并先提交接口、契约与未注册测试（`5111dc6`）。

## 实际验证

| 配置 | 构建 | CTest |
| --- | --- | --- |
| msvc-x64-debug | PASS | 53/53 |
| msvc-x64-release | PASS | 53/53 |
| msvc-x86-debug | PASS | 53/53 |
| msvc-x86-release | PASS | 53/53 |
| mingw-x64 | PASS | 53/53 |

每种配置执行 `cmake --build --preset <preset> --parallel 4` 和
`ctest --preset <preset> --output-on-failure`。最终矩阵构建日志未发现 warning/error。
完整矩阵前还执行了 `python -m py_compile tests/reports_contract.py`、原 CLI 增量构建和
`ctest --preset msvc-x64-debug -R '^cli.reports$' --output-on-failure`。

新测试包含：正常诊断、架构不符、依赖缺失、API Set、候选访问错误、损坏 PE、主文件权限错误、超时、
进程列表、模块快照、PE 和恶意文本共 12 组固定报告。两种格式中的 JSON 附录与原 Report JSON 深比较，
CommonMark（允许 raw HTML）及 HTMLParser 检查没有注入的活动标签、资源属性或伪造标题。
CLI 测试覆盖真实编译 DLL、Unicode 输出路径、不覆盖、输入输出同路径、保存错误、格式选项隔离及退出码。

首轮新测试发现 PE fixture 的 lookup_source 使用了错误枚举值，已改为实际契约值，
同时修正该 fixture 的 optional<uint16_t> 整数类型警告。审查中修正 JSON 附录对 DEL 的重复可见化，
防止生成不属于 JSON 的转义；固定恶意文本回归包含 NUL、ESC、DEL、反引号和标签。
之后新增测试与完整矩阵均通过。

使用实际 x64 Debug CLI 对本次构建的 noop_fixture.dll 执行 doctor（声明 x86，退出 1），
生成 `build/reports-preview.html`；以独立临时 profile 启动隐藏的 Edge headless，截取 1200×1600 PNG 并目视检查，
页面正常显示状态、未验证边界、证据、静态图像及发现。预览及浏览器 profile 在忽略的 build 下，不进入提交。
未执行移动端、打印分页或多浏览器视觉矩阵；未重测 ASan、覆盖率或最大报告性能。

远端 Windows workflow 运行四种 MSVC 构建/测试和跨位数回归，以 PR Checks 的结果为准，全部通过后合并。

## 限制与下一步

- 当前为直接诊断导出；计划中的 `report export <saved.json>` 离线转换仍待独立实现，随后推进权威 Agent Skill。
- JSON 字段与退出语义保持 1.5；可读版排版不作为机器解析接口。
- 完整 JSON 附录保留原始内容，报告不自动脱敏；分享前需检查本地路径和进程信息。
- 查询 deadline 不包括格式化/输出，也不能打断阻塞输出。保存中途失败可能留下不完整的新文件，沿用原行为。
- 文档链接作为文字展示；Markdown 的测试以 CommonMark 为准，不保证每种第三方渲染器的扩展行为。
