# PE 头部检查交付

日期：2026-10-23。分支：`codex/p2-pe-inspect`。
开发前从最新 main 创建并推送同名分支；先提交公共接口、契约与损坏输入测试，再实现和接入 CTest。

## 本轮结果

- 新命令 `pe inspect <path>` 和 Core `ParsePe` / `InspectPeFile`。
- 解析 PE32/PE32+ 的 Machine、Subsystem、ImageBase、入口 RVA、特征位、节表和数据目录位置。
- 读取前检查结构长度；检查对齐、整数范围、节区重叠与文件截断。证书目录使用文件偏移，其他目录使用 RVA。
- 不依赖调用方位数：32 位 CLI 也可读取 PE32+ 数据。只报告 Machine 原始值，不据此声明支持该架构的运行时。
- 损坏输入返回 `malformed_pe` / 退出码 1；文件不存在、共享冲突、不支持的大小及超时分别报告。
- 文件以只读句柄读取，不执行输入；保留实际 IO 错误码，未执行的系统操作不虚构错误码。
- 所有 CLI 命令升级到 `2.0.0-dev.3` / Schema 1.2。新增 `target.path` 和 `static_analysis` 证据；旧 Schema 和固定样例保留。

使用示例（先完成对应构建）：

```powershell
& .\build\msvc-x64\Release\cli\dllinjector.exe pe inspect .\build\msvc-x64\Release\noop_fixture.dll --json
```

## 文件与设计

| 位置 | 变更 |
| --- | --- |
| `include/dllinjector/pe.h`、`core/pe/` | 公共类型、只读文件入口、有长度检查的字节解析与时钟测试接口 |
| `include/dllinjector/result.h`、`core/common/result.cpp` | 损坏 PE 错误类别和退出码 1 |
| `apps/cli/`、`include/dllinjector/version.h` | 命令、文本/JSON 输出、静态证据、版本 |
| `schemas/cli-v1.2.json`、`tests/fixtures/json/pe.json` | 新版本 Schema 和 PE 固定结果 |
| `tests/pe_tests.cpp`、`tests/pe_contract.py` | 字节级回归、真实 CLI、文件 IO 与格式验证 |
| 原 CLI/模块契约测试、CMake、Windows workflow | 版本迁移、旧样例验证、新测试及审查产物 |
| README、CHANGELOG、契约与本交付文件 | 使用方法、支持范围、验收记录 |

保持一次可交付的纵切片：此版本检查头部、节区和目录范围，不提前实现 Doctor。
数据目录可能跨多个区域；此时保留 RVA/大小，将无法确定的单一区域文件偏移标为 null，
避免因为简化的地址转换而把文件武断地判成损坏。节名原始八字节保留为 hex，展示名转义控制/非 ASCII 字节。

## 实际验证

运行 `cmake --preset msvc-x64`、`cmake --preset msvc-x86`、`cmake --preset mingw-x64`，
以及下列各配置的 `cmake --build --preset <配置> --parallel 4` 和 `ctest --preset <配置>`：

| 配置 | 构建 | CTest |
| --- | --- | --- |
| msvc-x64-debug | 通过 | 30/30 |
| msvc-x64-release | 通过 | 30/30 |
| msvc-x86-debug | 通过 | 30/30 |
| msvc-x86-release | 通过 | 30/30 |
| mingw-x64 | 通过 | 30/30 |

最终矩阵共 150 次测试执行，保留原 23 组，新增 6 组 PE 单元测试与 1 组 CLI 测试。
五份最终构建日志未检出编译器 warning/error。原模块契约的两个跨位数实测也通过：

```powershell
python tests/modules_contract.py build/msvc-x86/Release/cli/dllinjector.exe build/msvc-x86/Release/module_host.exe build/msvc-x86/Release/noop_fixture.dll . build/msvc-x64/Release/cli/dllinjector.exe 0
python tests/modules_contract.py build/msvc-x64/Release/cli/dllinjector.exe build/msvc-x64/Release/module_host.exe build/msvc-x64/Release/noop_fixture.dll . build/msvc-x86/Release/cli/dllinjector.exe 4
```

另外使用已安装的 MSVC AddressSanitizer 构建 PE 相关目标：

```powershell
cmake -S . -B build/pe-asan -G "Visual Studio 17 2022" -A x64 -DCMAKE_CXX_FLAGS=/fsanitize=address -DBUILD_TESTING=ON
cmake --build build/pe-asan --config Release --target pe_tests dllinjector_cli noop_fixture --parallel 4
# 运行环境 PATH 包含已安装 MSVC 的 bin/Hostx64/x64，以找到 ASan runtime DLL。
ctest --test-dir build/pe-asan -C Release -R '^(pe\.|cli\.pe)' --output-on-failure
```

结果 **7/7 通过**，未报告内存访问错误。其中包括两个 1024 字节样本的全部 2048 个截断前缀、
每次运行 5000 个固定种子变异输入，以及真实文件的 CLI 契约。该语料回归不等同于持续模糊测试或覆盖率证明。

首轮范围测试曾同时破坏入口点和节区范围，因此先触发了 optional-header 错误；现已让该用例只破坏待测节区范围，
并补充失败时的阶段输出。随后全部配置和 ASan 检查通过。首轮测试中的有符号/无符号比较警告也已修正。

本地日志位于 `build/pe-*.log`，不提交。远端验收以
[本分支 Windows CI](https://github.com/som1ng/DLLInjector/actions/workflows/windows.yml?query=branch%3Acodex%2Fp2-pe-inspect)
为准；合并前要求四个构建/测试作业与跨位数作业全部成功。

## 限制与下一步

- `scope: headers_sections_directories`：不解析导入/导出/延迟导入内容，不解析依赖，不验证签名或 DLL 可加载性。
- 上限为 256 MiB、96 节、16 目录；仅支持 PE32/PE32+，ROM/COFF 对象等不在本轮支持范围内。
- 使用本地普通驱动器路径；显式 UNC、设备、扩展路径和映射网络驱动器不支持。路径限制不是文件系统沙箱。
- 超时是协作式的，不能打断同步系统调用或输出；不保证文件路径永远对应同一个文件。
- 代码覆盖率百分比未测量，未宣称达到计划中的 80% 目标；未执行 Windows ARM64/旧系统实机检查或实际 DACL 拒绝测试。
- 未发布 Release，未配置 Agent，未更改旧 GUI 的操作行为。项目级许可证确认仍待发布前处理。

下一个独立功能分支：导入、导出及延迟导入内容解析和针对性损坏样例；随后接 DLL Doctor 规则与报告。
