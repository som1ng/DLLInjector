# 已保存 JSON 报告离线转换

目标：CLI `2.0.0-dev.8` / Schema `1.6`。

```powershell
dllinjector report export .\reports\doctor.json --format html --output .\reports\doctor.html
dllinjector report export .\reports\doctor.json --format markdown
```

- 必须提供一个输入文件和显式 `--format markdown|html`；拒绝 --json、--pid、--target-arch、--search-dir。
  --output、--verbose、--timeout-ms 沿用现有约定。只指定 .html 扩展名不能代替 --format。
- 成功转换退出 0，即使源报告记录了诊断失败、阻断发现或超时。源状态/错误/执行标志展示为“记录的结果”，
  不重算诊断、不确认来源真实性。成功转换不把源 outcome_verified 从 false 改为 true。
- 只读取指定 JSON，不访问源报告里的路径、PID、资料地址或 Schema URI，不执行任何进程/PE/加载诊断。
- 支持本项目 1.0–1.6 的报告；按版本使用随二进制嵌入的原 JSON Schema 完整验证，包含 data、findings、errors、条件约束。
  未知版本退出 4；JSON/已知 Schema 不合法退出 2。Schema 校验不代表报告内容真实。
- 严格 UTF-8（可含开头 BOM），拒绝 UTF-16、非法编码、注释、尾逗号、多个根值、重复键（含转义后相同的键）。
  另有资源/数值限制：文件最多 16 MiB，嵌套最多 64，最多 250000 个容器/键/标量事件；
  数值只接受 int64/uint64 范围的整数字面量，浮点/指数写法拒绝，避免精度变化。
  tool_version 最多 128 UTF-8 字节，资料 URL 最多 2048 字节；键/字符串中的 NUL 拒绝，避免正则校验截断。
  限制错误退出 4（浮点写法和 NUL 为 2）。
- JSON 附录保留原输入（去除 BOM、将 JSON 文法空白中的 CRLF/CR 统一为 LF）；不升级源版本、不修改源工具版本、耗时或字段值。
  转换器版本另外标注。展示的数据区以格式化 JSON 保留历史字段，诊断发现和证据单独展示。
- 相对/Unicode 本地磁盘路径可用；不接受 UNC、设备路径和映射网络盘。文件读取与解析/校验共用协作式预算，
  timeout=0 立即超时；不可中断正在执行的系统调用/单次校验，不包含格式化和输出。
  路径约束不构成文件系统沙箱；目录链接仍按 Windows 的文件系统规则解析。
- 导出错误使用当前 Schema 1.6 的 `command=report.export` 错误报告。此命令的成功文档附的是源 JSON，
  没有伪造新的“诊断成功” JSON。--output 也可能保存错误报告，沿用其他诊断命令的行为。
- stdout 与保存内容相同，均为无 BOM UTF-8；已存在输出（包括输入自身）拒绝覆盖。
- 沿用已验证的 HTML 文本转义和 CommonMark 自适应围栏。没有脚本、活动链接、网络资源、自动上传或自动脱敏。

## 依赖和目录

- 使用 jsoncons 1.10.0 的 JSON 与 Draft 2020-12 Schema 支持；源码归档固定 SHA-256。
  只在首次配置时下载至 build 缓存，CLI 运行时不依赖 Python、网络或外部 Schema 文件。
- 构建可显式提供已解压依赖目录用于离线配置，遵循 CMake FetchContent 的本地源码覆盖选项。
- 本地报告放 `/reports/`；构建、预览、下载缓存放 `/build/`；其他临时产物放 `/artifacts/`。
  它们及 Python 缓存不提交。源码、契约、版本化 Schema、固定测试样例和第三方许可说明继续跟踪。

## 验证计划

历史固定报告 1.0–1.5、当前 1.6 报告、保留失败状态、中文/emoji/大 FILETIME、重复键/非法编码/结构/版本/大小/嵌套限制、
Schema 深层错误、HTML/Markdown 注入文本、文件锁/缺失/超时、输出保护和忽略规则；本地五种构建配置与远端 CI。

## 本轮交付与实际验证（2026-10-24）

分支 `codex/p2-saved-report-export`，先提交接口/契约/未注册测试及忽略规则（`ba73517`），再实现。
核心改动在 `apps/cli/saved_report.*`，排版复用 `document.cpp`；构建配置在 `cmake/ReportSchemas.cmake`，
固定历史样例继续复用，未增加依赖源码或生成物到仓库。

| 配置 | 完整构建 / CTest |
| --- | --- |
| msvc-x64-debug | PASS，54/54 |
| msvc-x64-release | PASS，54/54 |
| msvc-x86-debug | PASS，54/54 |
| msvc-x86-release | PASS，54/54 |
| mingw-x64 | PASS，54/54 |

每种配置实际执行 `cmake --build --preset <preset> --parallel 4` 和
`ctest --preset <preset> --output-on-failure`。最终构建日志没有 warning/error。
完整矩阵后，对包含“复制 CLI 单独运行”和“转换错误报告再次导出”的最终测试文件，
五种配置均再次执行 `ctest --preset <preset> -R '^cli.saved-reports$' --output-on-failure`，全部通过。

配置方面，`cmake --preset msvc-x86` 实际验证远端依赖下载和 hash 校验；x64 / MinGW 复用
已解压到 build 的相同归档，使用 `-DFETCHCONTENT_SOURCE_DIR_JSONCONS=<本地绝对目录>`。
本轮没有重新安装 Python 测试依赖。运行时单文件部署测试在临时目录执行复制后的 CLI，没有旁置 Schema。

首次编译发现 MSVC 的单个字符串常量和对象节数限制，分别用构建时分段拼接和仅针对验证器源文件的 /bigobj 解决；
初始读取测试还发现 jsoncons 的普通未转义字符串携带 noesc 标签，已按真实 API 区分普通字符串和越界数字。
审查确认正则校验使用 C 字符串，因此在 Schema 校验前拒绝 NUL，并限制无界重复正则的版本号/资料 URL 长度。
相关非法输入、历史兼容与完整矩阵均已回归通过；错误输出不会复制解析器异常中的原始输入。

实际生成 `build/saved-preview.html`：将历史 doctor-mismatch 1.4 报告转换为 HTML（退出 0），
在隔离 profile 的隐藏 Edge headless 中截取 1200×1600 图片并目视检查，源版本、未验证标志和阻断发现均正确展示。
缓存、截图和下载源码都在忽略目录；`git diff --check` 通过。
没有重跑 ASan、测覆盖率或开展最大规模性能、多浏览器和打印分页验证。

远端 Windows CI 仍须通过四种 MSVC 配置及跨位数检查后合并；结果以本功能 PR Checks 为准。
下一阶段为权威 Agent Skill。当前报告的格式合法性不等于内容真实性，且不提供签名或脱敏功能。
