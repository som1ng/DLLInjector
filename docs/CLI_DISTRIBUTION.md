# Rondo CLI 下载归档与校验

Windows CI 的 Release 作业提供 `rondo-cli-msvc-x64` 和 `rondo-cli-msvc-x86` 下载产物。
选择与你的诊断任务匹配的架构，并确认整个工作流成功。它们是 CI 产物，尚非正式 GitHub Release，
没有数字签名；项目级许可仍待确定。源码库不提交生成的 EXE、ZIP 或本机安装记录。

每个产物包含 `rondo-cli-<version>-windows-<arch>.zip` 及同名 `.zip.sha256` 文件。
ZIP 内只有四个文件：CLI、`manifest.json`、`README.txt`、`THIRD_PARTY_NOTICES.md`。
清单记录版本、Schema、架构、EXE 字节数/哈希，以及 CI 构建检出的提交 SHA。
ZIP 不包含安装器、GUI、演示程序、运行库或用户报告。

需要包含 Agent 安装器和 MCP 的完整包时，使用 [Rondo npm 归档](../integrations/npm/README.md)。
新 ZIP 使用 `rondo.exe` 和 `product: rondo-cli`；安装器仍兼容原 `dll-doctor-cli` / `dllinjector.exe` 清单，
但两种产品标识与文件名必须对应，不能混用。

## 下载后

1. 从可信的成功 CI 运行取得产物。GitHub 的下载容器解压后，先保留其中的 CLI ZIP 与校验文件。
2. 使用 PowerShell 检查内部 CLI ZIP 的 SHA-256，与同名 `.zip.sha256` 首列比较；一致后解压到稳定目录。
3. 使用本地源码安装器的 `--manifest` 校验解压后的 CLI，再安装 Skill。

```powershell
# 请替换实际版本、架构和路径；这里只读取文件。
Get-FileHash -LiteralPath 'D:\downloads\rondo-cli-2.0.0-dev.9-windows-x64.zip' -Algorithm SHA256
Get-Content -LiteralPath 'D:\downloads\rondo-cli-2.0.0-dev.9-windows-x64.zip.sha256'

# 在源码仓库根目录运行；项目目录需要已存在。
node integrations/installer/src/index.mjs setup --agent codex --scope project --project 'D:\work\my-app' --cli 'D:\tools\Rondo\rondo.exe' --manifest 'D:\tools\Rondo\manifest.json' --dry-run
node integrations/installer/src/index.mjs setup --agent codex --scope project --project 'D:\work\my-app' --cli 'D:\tools\Rondo\rondo.exe' --manifest 'D:\tools\Rondo\manifest.json' --verify
```

`distribution_checksum: verified` 表示 EXE 与所给清单匹配。安装器随后仍会检查实际 CLI 版本；
文件校验、CLI 启动、Agent 发现与模型调用是不同层次的验证。没有自动下载或执行清单中的路径/URL。
`--dry-run` 校验清单但不启动 EXE、不写安装文件。CLI 更新后需按安装器说明显式重新安装。

哈希和清单都来自同一分发渠道，因此只证明内容一致，不能独立证明发布者身份。
不要用旧版 GUI EXE 或来源不明的文件替换归档中的 CLI。
源码构建用户仍可使用原有 `--cli` 入口，省略清单。

## 运行要求

当前 MSVC CLI 使用动态 VC++ 运行库。目标机器需要与 CLI 架构匹配、且不早于构建工具版本的
Microsoft Visual C++ v14 Redistributable；见 [Microsoft 官方说明](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist)。
此归档不自动安装运行库；缺少运行库时，版本探测可能失败，不能据此宣称哈希校验失效。
全新 Windows、无运行库机器和 ARM64 的执行兼容性尚未验收。

## 维护者打包

先构建并验证可信的 Release CLI，再执行：

```powershell
python integrations/installer/scripts/package_cli.py --cli build/msvc-x64/Release/cli/rondo.exe --architecture x64 --output build/distribution/local-x64
```

打包会执行所选 CLI 的版本查询，不应用于未知 EXE。`--architecture x86` 对应 x86 构建。
输出目录内已有同名归档或校验文件时拒绝覆盖。打包失败会清理本次创建的输出文件。
可用 `--revision <40位提交SHA>` 记录确定的构建来源；未提供时为 `null`，不会猜测本地旧产物的来源。

测试命令：`python tests/cli_distribution_tests.py --cli <CLI路径> --architecture x64`；
包含解压后的真实 CLI 安装/校验/PE 查询/卸载。省略两个参数时，该原生用例明确跳过。
安装器目录中的 `npm test` 覆盖清单拒绝路径及“校验失败不启动候选 EXE”。
详细约定见 [分发契约](CLI_DISTRIBUTION_CONTRACT.md)，实测结果见 [交付记录](CLI_DISTRIBUTION_DELIVERY.md)。
