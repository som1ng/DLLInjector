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
  数值只接受 int64/uint64 范围的整数字面量，浮点/指数写法拒绝，避免精度变化。限制错误退出 4（浮点写法为 2）。
- JSON 附录保留原输入（去除 BOM、将 JSON 文法空白中的 CRLF/CR 统一为 LF）；不升级源版本、不修改源工具版本、耗时或字段值。
  转换器版本另外标注。展示的数据区以格式化 JSON 保留历史字段，诊断发现和证据单独展示。
- 相对/Unicode 本地磁盘路径可用；不接受 UNC、设备路径和映射网络盘。文件读取与解析/校验共用协作式预算，
  timeout=0 立即超时；不可中断正在执行的系统调用/单次校验，不包含格式化和输出。
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
