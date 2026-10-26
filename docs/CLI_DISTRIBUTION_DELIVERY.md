# CLI 分发校验交付

日期：2026-10-25。本轮完成 P4 的 CI 分发归档与本地安装前清单验证。
[使用说明](CLI_DISTRIBUTION.md)、[契约](CLI_DISTRIBUTION_CONTRACT.md)。
安装器升级至 `0.1.0-dev.2`，CLI 版本/原生接口与 GUI 均未修改。

## 实现

- 打包脚本读取指定 CLI 的 PE 架构并执行版本查询，拒绝位数错误、查询前后文件变化和输出覆盖。
  归档只含 EXE、清单、说明与第三方声明，并生成 ZIP SHA-256 文件。
- Manifest 记录实际字节哈希/大小、版本/Schema、x86/x64、运行库要求和显式来源 SHA。
  本地复用旧构建时不自动假定当前 HEAD 是其源码来源。
- 安装器新增可选 `--manifest`：启动 EXE 或写入安装文件前验证清单和二进制，启动后比对版本/hash。
  已验证的分发元数据写入本地 CLI 绑定；原有无清单源码安装入口继续可用。
- 两个 Windows Release CI 作业都测试并打包对应 CLI，新增 `cli-distribution-msvc-x64` /
  `cli-distribution-msvc-x86` 下载产物。归档、EXE、测试日志均位于现有忽略的 `build/`。

## 本地实际验证

Windows，Node.js `22.22.3`，Python 本地环境；复用已存在的 dev.9 / Schema 1.6 x64 与 x86 CLI。
未修改并行 worktree 的二进制；x86 fixture 复制到本轮独立忽略目录后验证。
本轮没有声称重新编译这些本地 fixture，也未调用模型或下载/安装运行库。

| 命令 | 结果 |
| --- | --- |
| `npm test`（`integrations/installer`，设置 `DLL_INSTALLER_TEST_CLI`） | 22 项通过，0 跳过；包括原有安装/回滚与新增清单拒绝路径 |
| `python tests/cli_distribution_tests.py --cli <x64-cli> --architecture x64` | 6 项通过，0 跳过 |
| `python tests/cli_distribution_tests.py --cli <x86-cli> --architecture x86` | 6 项通过，0 跳过 |
| `python integrations/installer/scripts/package_cli.py --cli <x64-cli> --architecture x64 --output build/distribution/local-x64` | 实际脚本入口生成 ZIP 与校验文件；本地复用构建的来源 SHA 为 `null` |

两种原生架构都经历真实打包、解压到源码树外的中文/空格/emoji 目录、安装预览、安装、
重复安装、校验、原生 PE 查询、破坏二进制后的拒绝以及卸载。
清单单测验证格式/版本/路径/哈希/大小/PE 架构错误在执行候选程序和写入项目之前被拒绝。
故障注入验证校验文件写入失败时清理本次新归档、保留原有文件。

## 边界

CI 下载归档不是正式 Release 或签名发行版。哈希只能与所给清单比对，不能证明来源真实性；
调用方仍须选择可信的成功 CI 运行。项目许可、自动下载、MCP 配置安装与公开 npm 发布仍待推进。
当前 MSVC CLI 动态依赖 VC++ v14 运行库，归档不附带运行库安装器。
没有验证全新系统、运行库缺失系统或 ARM64 执行兼容性。
