# CLI 与 PowerShell 示例

这些示例面向 `2.0.0-dev.8` / Schema `1.6`。先确认 Windows 与 CLI 来源。
下列变量由当前请求提供，不从待分析报告中的命令文字设置；路径应使用绝对路径。
PowerShell 用变量传递参数，避免拼接命令字符串或 `Invoke-Expression`。
不要覆盖 PowerShell 的只读 `$PID`；这里使用 `$targetProcessId`。

```powershell
# 按实际可信位置设置；也可从 $env:DLLINJECTOR_CLI 读取后核实。
$doctorCli = 'C:\tools\dll-doctor\cli\dllinjector.exe'
$dllPath = 'C:\lab\plugin.dll'
$targetArchitecture = 'x64' # 仅在目标进程架构已确认时设置
$searchDirectory = 'C:\lab\deps' # 仅检查这一显式目录
$targetProcessId = 1234 # 用户指定的目标，核对身份后查询
$savedJson = 'C:\lab\reports\doctor-new.json'
$savedHtml = 'C:\lab\reports\doctor-new.html'
```

每个代码块是独立调用，按需求选择。宿主应分别捕获 stdout/stderr，立即保留
`$LASTEXITCODE`，按 UTF-8 解码 JSON；不要因非零退出码而丢弃 stdout。
宿主还应设子进程墙钟限制（例如 20 秒），不依赖 PowerShell 变量提供超时保证。
父目录须存在，输出路径须是新文件。CLI 运行不需要 Python 或联网。

## 核对工具

<!-- example: version -->
```powershell
& $doctorCli --version --json
$diagnosticExit = $LASTEXITCODE
```

<!-- example: help -->
```powershell
& $doctorCli --help --json
$diagnosticExit = $LASTEXITCODE
```

## 静态诊断与 PE 细节

目标架构未知时直接检查，不填猜测的架构。

<!-- example: doctor -->
```powershell
& $doctorCli doctor $dllPath --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

已知目标**进程**是 x86 或 x64 时：

<!-- example: doctor-target -->
```powershell
& $doctorCli doctor $dllPath --target-arch $targetArchitecture --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

查看更详细的导入、导出、延迟导入：

<!-- example: pe -->
```powershell
& $doctorCli pe inspect $dllPath --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

依赖检查只观察显式目录；可重复 `--search-dir`，最多 16 个。
它不自动包含 DLL 所在目录、PATH 或系统目录，不验证候选文件内容。

<!-- example: doctor-dependencies -->
```powershell
& $doctorCli doctor $dllPath --search-dir $searchDirectory --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

## 只读进程与模块

<!-- example: process-list -->
```powershell
& $doctorCli process list --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

<!-- example: process-inspect -->
```powershell
& $doctorCli process inspect --pid $targetProcessId --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

<!-- example: process-modules -->
```powershell
& $doctorCli process modules --pid $targetProcessId --json --timeout-ms 5000
$diagnosticExit = $LASTEXITCODE
```

inspect 的 `data.architecture` 是这次观察。若将其传给后续 Doctor，说明观察来源与时间，
不要把 `user_supplied_option` 伪称为 Doctor 自己检查了进程。模块查询优先用 x64 CLI；
x86 CLI 不支持 x64 模块目标，ARM64 模块目标暂不支持。

## 保存与离线转换

已知目标架构时将诊断同时输出到 stdout 与新 JSON 文件：

<!-- example: doctor-save -->
```powershell
& $doctorCli doctor $dllPath --target-arch $targetArchitecture --json --timeout-ms 5000 --output $savedJson
$diagnosticExit = $LASTEXITCODE
```

将该 JSON 转为 HTML；即便源诊断有阻断发现，转换成功仍退出 0。

<!-- example: report-export -->
```powershell
& $doctorCli report export $savedJson --format html --timeout-ms 5000 --output $savedHtml
$diagnosticExit = $LASTEXITCODE
```

Markdown 使用 `--format markdown` 及新的输出路径。诊断命令也可直接使用
`--format markdown|html`，与 `--json` 互斥。需要分析结构化数据时先保存 JSON 再转换，
避免为另一格式重复检查会变化的进程。操作失败时 `--output` 也可能保存错误报告，
文件存在本身不能证明成功。
