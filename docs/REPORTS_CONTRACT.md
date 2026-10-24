# Markdown / HTML 报告契约

目标版本：`2.0.0-dev.7`，JSON Schema 保持 `1.5`。本轮实现 P2.2 的直接导出；
本文件记录 dev.7 的直接输出。dev.8 已实现 `report export <saved.json>`，见 [离线转换契约](SAVED_REPORTS_CONTRACT.md)。

## 命令

```powershell
dllinjector doctor plugin.dll --target-arch x64 --format markdown --output report.md
dllinjector doctor plugin.dll --search-dir .\plugins --format html --output report.html
dllinjector pe inspect plugin.dll --format html
dllinjector process modules --pid 1234 --format markdown
```

- `--format` 只接受小写 `markdown` / `html`，适用于 process list/inspect/modules、pe inspect、doctor。
- 与 `--json` 互斥；重复、缺值、无效格式或用于 --help/--version 均退出 2。参数顺序与现有 CLI 一致。
- 未指定时保留原文本/JSON 行为；不根据文件扩展名猜格式。stdout 输出完整文档；`--output` 另外保存相同字节。
- 无 BOM UTF-8，LF 换行；不覆盖已有文件、不自动建目录。参数错误不创建文件。
- 解析成功后的查询/编码/保存失败也使用选定文档格式；解析失败沿用原文本错误（显式 --json 时为 JSON）。
- 退出码继承原诊断：Doctor 阻断发现退出 1，完成状态仍为 success；操作错误保留 2/3/4/5。
  保存失败时 stdout 为错误报告，不能包含旧的成功 findings/data。部分写入失败可能留下不完整新文件，继承原保存语义。

## 内容与事实边界

- 文档概要列出工具/Schema 版本、命令、状态、退出码、目标、耗时和三个 execution 标志。
- 公用证据生成函数确保 JSON 与文档的来源/描述一致；Doctor 的 outcome_verified 始终为 false。
- Doctor 展示静态图像摘要、每条发现的规则/级别/置信度/说明/下一步/证据/资料地址，
  以及显式目录、普通/延迟依赖、各目录观察和错误。资料地址作为文字展示。
- process / PE 复用已有可读详情；完整字段（如逐项错误、精度敏感 FILETIME、截断标志）保存在文档的完整 JSON 附录中。
- 所有格式复用同一次 Execute 结果，不能重查进程、重读文件、加载 DLL、申请权限或访问网络。
- 报告状态不等于 DLL 可加载。明确 static_analysis / runtime_observation / filesystem_observation、
  显式架构声明与实际观察、文件候选与加载器解析的区别。不得把导出完成当成效果验证。
- JSON 附录直接使用现有 Render(report, true)，无数据删减、没有新 JSON 解析器或 CLI 运行时依赖。

## 转义与阅读

- HTML 为单文件，带 UTF-8、viewport、内联样式、打印样式及限制资源的 CSP；不包含 JavaScript、外部资源、可执行链接或表单。
  动态字段仅插入转义后的文本节点；转义 `& < > " '`，不得进入属性/样式/脚本上下文。
- Markdown 使用 CommonMark 标题、字段和代码块。字段用自适应反引号代码 span，块围栏长于内容中最长反引号串。
  路径、PE 名称、诊断及 JSON 中的 Markdown / HTML 片段只能呈现为文字，不能生成链接、图片或活动 HTML。
- 可读字段中的控制字符显示为可见转义；代码块保留换行与 tab，其他 C0/DEL 显示转义。
  原始语义以 JSON 附录为准（JSON 中控制字符已转义）。
- 无脱敏功能：分享前检查报告中的文件路径、进程信息及依赖名称。不会自动上传。
- 查询超时不包含格式化/输出阶段，不能打断阻塞输出；沿用现有契约，不承诺渲染时间或文件大小上限。

CommonMark 规则依据：[code spans](https://spec.commonmark.org/0.31.2/#code-spans)、
[fenced code blocks](https://spec.commonmark.org/0.31.2/#fenced-code-blocks)。

## 验证计划

- 至少 10 个固定内存报告覆盖正常、架构不符、候选缺失/API Set/不可访问、损坏 PE、权限错误、超时、进程、模块、PE 与恶意标记文本。
- 两种格式还原 JSON 附录并与同一 Report 的 JSON 深比较；用 CommonMark 解析器与 HTMLParser 检查危险元素、资源属性和伪造标题。
- CLI 子进程验证 UTF-8、中文/空格/emoji 路径、两格式保存、拒绝覆盖、输入输出同路径、保存失败、参数隔离和原退出码。
- 本地 MSVC x86/x64 debug/release 与 MinGW x64 构建/CTest；云端 Windows 矩阵与跨位数回归通过后合并。
