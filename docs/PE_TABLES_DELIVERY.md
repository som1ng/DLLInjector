# PE 导入/导出/延迟导入交付

日期：2026-10-23。分支：`codex/p2-pe-tables`。
本地与同名远端分支均在实现前创建；先提交公共类型、解析契约及测试，再实现。

## 已实现

- `pe inspect` 输出依赖 DLL 名称、按名称/序号导入的符号及 hint，记录查找表来源。
- 导出保留 DLL 名称、ordinal base、EAT 槽位数、具名别名、按序号导出和转发字符串；空槽不冒充导出。
- 支持现代 RVA 延迟导入；旧 VA 格式、未知标志以及缺少原始查找表的绑定 IAT 明确返回 unsupported。
- 元数据读取支持跨相邻 RVA 区域、非相邻原始文件区域；不使用虚拟零填充代替文件中的结构或字符串。
- 缺失终止项、非法索引、越界指针返回受控错误，失败不会返回半份成功结果。
- 设置库数、条目数、单字符串和累计文本上限，重复指向同一字符串也消耗输出预算。
- CLI `2.0.0-dev.4` / Schema 1.3；所有历史 Schema 和样例保持不变。

主要变更在 `include/dllinjector/pe.h`、`core/pe/pe.cpp`、`apps/cli/report.cpp`、
`schemas/cli-v1.3.json`、`tests/pe_tables_tests.cpp`、`tests/pe_tables_contract.py`，
以及 CMake、真实延迟加载 fixture、既有契约测试和用户文档。

## 验证

执行三组 `cmake --preset msvc-x64 / msvc-x86 / mingw-x64` 配置命令，以及各配置的
`cmake --build --preset <配置> --parallel 4` 和 `ctest --preset <配置>`：

| 配置 | 构建 | CTest |
| --- | --- | --- |
| msvc-x64-debug | 通过 | 38/38 |
| msvc-x64-release | 通过 | 38/38 |
| msvc-x86-debug | 通过 | 38/38 |
| msvc-x86-release | 通过 | 38/38 |
| mingw-x64 | 通过 | 38/38 |

最终矩阵共 190 次测试执行。新增七组表解析单元测试和一组 CLI 测试；原 30 组继续通过。
MSVC 的四组配置实际编译 `/DELAYLOAD:noop_fixture.dll` fixture 并静态核对其依赖/符号；
MinGW 的延迟导入验证使用合成样本，未宣称验证了 MinGW 编译器的延迟加载产物。
原模块两项跨位数脚本也以新的 CLI / Schema 1.3 实测通过。

MSVC ASan 构建与检查：

```powershell
cmake -S . -B build/pe-asan -G "Visual Studio 17 2022" -A x64 "-DCMAKE_CXX_FLAGS=/EHsc /Zi /fsanitize=address" -DBUILD_TESTING=ON
cmake --build build/pe-asan --config Release --target pe_tests pe_tables_tests dllinjector_cli noop_fixture delay_fixture --parallel 4
# PATH 包含已安装 MSVC 的 bin/Hostx64/x64，以加载 ASan runtime DLL。
ctest --test-dir build/pe-asan -C Release -R '^(pe|cli.pe)' --output-on-failure
```

结果 **15/15 通过**，未报告内存访问错误。包含原 2048 个截断前缀及 5000 个变异输入，
另外增加每次 6000 个表内容变异输入、1024 库和 65536 符号边界、累计文本限制及确定性超时。
ASan 首次配置只覆盖了 sanitizer 标志，产生缺失异常展开/调试信息的编译警告；补齐 `/EHsc /Zi`
后重新构建和测试通过。最终五种普通构建与 ASan 日志均未检出编译器 warning/error。
这些用例不是持续模糊测试或代码覆盖率百分比证明。

本地日志为 `build/tables-*.log`，不提交。合并前须通过
[分支 Windows CI](https://github.com/som1ng/DLLInjector/actions/workflows/windows.yml?query=branch%3Acodex%2Fp2-pe-tables)
的四个构建/测试作业和跨位数作业。

## 边界与下一步

本轮解析静态声明，不查找或加载依赖，不跟随转发目标，不验证签名和可加载性。
其他目录内容、API Set / Known DLL / SxS / 动态加载的解析仍未实现；名称只支持非空可打印 ASCII。
资源限制与错误规则见 [契约](PE_TABLES_CONTRACT.md)。旧 GUI 行为保留，未发布 Release 或修改 Agent 配置。

下一独立分支：DLL Doctor 的静态诊断规则与证据输出，再加入有明确搜索范围和“不足以判断”状态的依赖分析。
