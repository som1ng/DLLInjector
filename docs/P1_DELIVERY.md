# P1 首个完整交付：只读进程 Core / CLI

日期：2026-10-22。分支：`codex/p1-process-cli`。
本轮在用户确认 P0 合并后实施；开发前已创建本地与远端同名分支。
先提交公共接口、[命令契约](P1_CONTRACT.md) 和端到端测试，再实现并验证。

## 已实现

- `include/dllinjector/`：不依赖 Win32 UI 类型的 Result/Error、进程数据结构、Unicode 转换、开发版本。
- `core/common/`、`core/process/`：只读查询、RAII 句柄、统一错误和超时；保留 P0 现代/旧版架构检测语义。
- `src/utils.*`、`src/utils_detail.h`：GUI 的枚举和架构查询适配到 Core，保留列表排序及原功能入口。
- `apps/cli/`：`--help`、`--version`、`process list`、`process inspect --pid`，以及 JSON、超时、输出文件和日志选项。
- `schemas/cli-v1.json`、`tests/fixtures/json/`：版本化 Schema、Unicode/访问失败/超时固定样例。
- `tests/core_tests.cpp`、`tests/cli_contract.py`：API 错误替身、资源与结果语义、真实 CLI 子进程及 Schema 验证。
- CMake / Windows CI：同时构建 GUI 和 CLI；测试安装固定版本 `jsonschema`；上传 CLI、Schema 与契约文档。
- README / CHANGELOG：中英文调用示例、退出码、实际能力与后续边界。

## 关键决策

1. Windows 文件名不区分大小写。保留 `DLLInjector.exe`，将 CLI 放在同配置目录的 `cli/dllinjector.exe`。
2. `inspect` 使用一个 `PROCESS_QUERY_LIMITED_INFORMATION` 句柄获取路径、创建时间及架构，避免重新按 PID 打开导致观察对象混杂。
3. 快照中的不可访问条目仍显示 PID/name，缺失创建时间为 null，保留条目错误。快照遍历本身失败则整个命令失败。
4. UTF-16 参数和路径通过 Windows 宽字符 API 处理；JSON/重定向/文件输出使用 UTF-8。FILETIME 使用字符串避免 JS 精度丢失。
5. `--output` 使用 CREATE_NEW，不覆盖已有文件；无效语法不创建文件。输出失败以独立错误及退出码报告。
6. `--timeout-ms` 是协作式查询期限；不能打断阻塞的系统调用或输出。上层 Agent 应另设子进程墙钟超时。
7. 不引入 CLI 运行时 Python/Node/JSON 库。Python `jsonschema==4.23.0` 仅用于测试，构建时不会隐式下载安装。

## 本机验证

已执行 `cmake --preset`、`cmake --build --preset ... --parallel 4` 与 `ctest --preset ...`：

| 配置 | 构建 | CTest |
| --- | --- | --- |
| MSVC x64 Debug | 通过 | 16/16 |
| MSVC x64 Release | 通过 | 16/16 |
| MSVC x86 Debug | 通过 | 16/16 |
| MSVC x86 Release | 通过 | 16/16 |
| MinGW x64 Release | 通过 | 16/16 |

合计 80 次测试执行，包含原有 10 组回归、5 组 Core 测试及 1 组 CLI 契约测试。
CLI 契约组内包含多个真实子进程案例；真实权限不足通过替身稳定验证，不依赖系统进程权限偶然性。
Unicode 端到端案例启动测试自己复制的中文/空格/emoji 路径 Demo，查询完成后只结束该自有子进程。
测试日志位于 `build/p1-*.log`，不纳入版本控制。

远端实际结果见 [GitHub Actions](https://github.com/som1ng/DLLInjector/actions/workflows/windows.yml?query=branch%3Acodex%2Fp1-process-cli)。
PR 合并前检查对应最新提交的四组 MSVC 配置、构建、测试和产物上传均成功。

## 当前产物

```text
build/msvc-x64/Release/cli/dllinjector.exe
build/msvc-x86/Release/cli/dllinjector.exe
build/mingw-x64/Release/cli/dllinjector.exe
```

GUI 路径不变。此版本为 `2.0.0-dev.1` 开发里程碑，不创建 Release，也不宣称 Agent 已完成集成。

## 限制与下一步

- 本轮交付 P1 的 list/inspect 纵切片；`process modules` 尚未实现，应在独立功能分支补充跨位数及不可访问错误测试。
- PE、Doctor 和报告仍按 P2 依赖推进；现有实验后端不用于诊断。
- 快照枚举及属性查询不是原子快照；PID 可能复用、目标也可能退出。结果仅表明查询时观察到的元数据。
- ARM64/旧系统实机、交互式控制台和编辑器内 F5 未人工验证；ARM64/旧 API 路径有替身测试。
- 输出磁盘写入中途失败可能留下不完整的新文件；不会把写入失败报告为成功。
- 未执行第三方进程注入、DLL 部署、反射式或 Direct Syscall。
- 未发布安装包、未修改 Agent 配置；项目级许可证仍需核实。
