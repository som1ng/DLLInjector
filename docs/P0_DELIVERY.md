# P0 / PR-01 首轮交付

日期：2026-10-22。分支：`codex/p0-build-baseline`。

本轮遵循本地《重构升级计划》第 12 节，只完成 P0 与 PR-01 的可执行部分，
在 P1 前保留人工 Review 节点。没有发布 Release。

按用户补充的分支约定，本轮使用本地及远端同名分支 `codex/p0-build-baseline`，
通过 PR 合入 `main`。后续每个新功能均先创建本地及远端分支，再开始实现。
这项约定已记录到本地项目 `AGENTS.md`；该文件仍遵循用户现有的忽略规则。

## 方案审查结论

总体方向可执行，采用以下调整：

1. P1 中的 PE/Doctor 命令依赖 P2，不提前提供假实现或宣传可用。
2. 将 `IsProcess64Bit` 的歧义修复提前到 P0；通用 Core `Result<T>` 留给 P1。
3. GNU x64 Direct Syscall 代码不能直接编进 MSVC x86/x64 矩阵；保留全部源码与 GUI
   条目，但当前构建明确显示不可用。没有移植、修复或执行这一实验后端。
4. 将 CI Runner 固定为 `windows-2022`，匹配 Visual Studio 2022 Presets。
   配置 CI 与实际远端运行是两个独立状态。
5. 仓库没有项目级 LICENSE，不根据旧 README 自动补一份 MIT 或覆盖第三方声明。

## 修改文件与目的

| 文件 | 修改 |
| --- | --- |
| `CMakeLists.txt`、`CMakePresets.json` | 可重建 GUI、原 Demo、utility 库与测试；MSVC x86/x64、可选 MinGW x64 |
| `.github/workflows/windows.yml` | Debug/Release 矩阵、CTest、审查产物与日志上传 |
| `src/utils.h`、`src/utils.cpp` | 显式架构结果；保留失败原因；文件对话框字符容量；正确处理权限部分分配 |
| `src/utils_detail.h` | 小型内部 Win32 替身接口，避免测试打开对话框或修改真实权限 |
| `src/main_gui.cpp` | 未知/ARM64 架构显式提示；启动不申请 Debug Privilege；不把请求提交称为初始化成功；标记未验证后端 |
| `src/methods/injector_methods.h`、`src/methods/method_syscall.cpp` | 仅纠正实验后端的过度宣传注释，未改其执行逻辑 |
| `tests/utils_tests.cpp` | 7 个测试分组，覆盖边界、错误、权限替身、现代/兼容查询、真实只读查询及 Unicode 文件 |
| `tests/smoke_tests.cpp`、`tests/fixtures/noop_plugin.cpp` | 3 项启动/加载测试；所有子进程属于测试本身 |
| `.vscode/tasks.json`、`launch.json`、`c_cpp_properties.json` | 去除失效的个人编译路径与不存在的任务，构建/调试对齐 CMake |
| `README.md`、`docs/*` | 已有能力与未来路线分开；基线、契约、第三方声明及结果记录 |
| `.gitignore` | 只新增 CMake 构建目录和个人 Presets 忽略规则；用户原有未提交条目保留 |

## 实际执行的命令与结果

原 README 构建及原 VS Code 源码清单构建均已执行并复现失败，详见
[基线报告](BASELINE.md)。修复后实际执行：

```powershell
cmake --preset msvc-x64
cmake --build --preset msvc-x64-debug --parallel
ctest --preset msvc-x64-debug
cmake --build --preset msvc-x64-release --parallel
ctest --preset msvc-x64-release

cmake --preset msvc-x86
cmake --build --preset msvc-x86-debug --parallel
ctest --preset msvc-x86-debug
cmake --build --preset msvc-x86-release --parallel
ctest --preset msvc-x86-release

cmake --preset mingw-x64
cmake --build --preset mingw-x64 --parallel 4
ctest --preset mingw-x64
```

全部构建成功；每个配置 10/10 测试通过，合计 50 次测试执行。
这些是本机 Windows 的结果。远端分支已推送，GitHub Runner 的实际结果和日志见
[Windows build and tests](https://github.com/som1ng/DLLInjector/actions/workflows/windows.yml?query=branch%3Acodex%2Fp0-build-baseline)。
合入前须确认远端 MSVC x86/x64 的 Debug/Release 四组构建和测试均通过。

测试确认：文件框实际容量是 260 个宽字符；259 字符 Unicode 路径与取消/失败分支；
Unknown 与 x86 分离；现代 API 失败不静默回退；旧 API 在模拟的原生 x86 系统上
不误判 x64；查询句柄正确关闭；权限 API TRUE + `ERROR_NOT_ALL_ASSIGNED` 仍失败；
清理不丢失错误；GUI 启动没有额外启用调试权限。

## 可查看的产物

- `build/msvc-x64/Debug/DLLInjector.exe`
- `build/msvc-x64/Release/DLLInjector.exe`
- `build/msvc-x86/Debug/DLLInjector.exe`
- `build/msvc-x86/Release/DLLInjector.exe`
- `build/mingw-x64/DLLInjector.exe`

本机构建日志在 `build/baseline/`，CTest 日志在各构建目录的
`Testing/Temporary/LastTest.log`。构建目录不纳入版本控制。

## NOT_RUN 与仍存限制

- ARM64 实机、真正的旧版/32 位 Windows：没有硬件/系统验证，相关分支仅替身测试。
- 第三方进程注入、DLL 部署、反射式和 Direct Syscall：未运行，不宣称有效。
- 原始反射式及部署 PE 读取器仍欠缺严格长度检查；后续 Doctor 必须使用新解析器。
- 旧方法的 `bool`、等待和远程资源生命周期仍非可验证的完整执行协议。
- 原始弹窗 DLL 仅编译；无副作用 fixture 的加载通过不代表原样例所有方法通过。
- VS Code JSON/任务对应关系已检查，编辑器内 F5 未人工运行。
- 项目级许可与剩余第三方来源仍待核实；新增声明没有重新许可原有代码。

## 下一 PR 建议（待 Review 后实施）

先交付一个完整的只读纵切片：

1. 定义 `Result<T>/Error` 与版本化 JSON Schema，分开命令阶段、诊断结论和原始 API 错误。
2. 把进程查询从 Win32 UI utility 中抽到 Core；GUI 通过兼容适配层使用 Core。
3. 实现 `--help`、`--version`、`process list/inspect`；不要让尚未落地的命令返回成功。
4. 验证非法参数、不可访问 PID、Unicode、stdout/stderr 分离、退出码和超时契约。
5. 新 PE 解析器必须先有截断/越界测试，再逐步加入 `pe inspect/doctor`。

Skill、MCP、安装器及 V3 Lab 继续按依赖关系推进，不在本轮创建空壳。
