# Doctor 显式目录依赖候选检查

目标版本：`2.0.0-dev.6`，Schema `1.5`。在 Doctor 静态规则之上增加可选目录观察，不实现 Windows 加载器。

## 使用方式

`dllinjector doctor plugin.dll --search-dir <app-directory> --search-dir <other-directory> --json`

`--search-dir` 仅用于 Doctor，可重复，最多 16 项，按传入顺序报告。相对路径转换为绝对本地盘路径；不自动加入 DLL 所在目录、当前目录、PATH 或系统目录，不展开环境变量。
参数不重复去重，顺序不代表实际加载优先级。空参数、超过上限、目录不存在/无权限/不是目录、UNC/设备/远程映射盘分别明确报错。
不传目录时不执行候选检查，`data.dependencies=null`。主 PE 结构损坏时仍完成原有诊断，依赖检查不执行、该字段为 null。

## 检查与证据边界

- 仅取主文件的直接普通导入与延迟导入，分别标记 `kind=import/delay_import`；不递归读取候选文件、不验证符号、架构、签名或可读性。
- 候选检查使用只读文件属性查询。`file` 只代表观察到一个非目录、非重解析点的文件；不能称为已解析依赖或 DLL 可加载。
- 大小写不敏感识别 `api-` / `ext-` 前缀并标为 `api_set_unresolved`，不查同名磁盘文件，也不声称契约在目标系统存在。
- 只探测单个普通 ASCII 文件名：字符限字母、数字、下划线、点、加减号，长度不超过 255，必须有非空显式扩展名；拒绝 DOS 设备名、路径、流、通配符、空白与末尾点。其他名称标为 `unsupported_name`，不执行由导入名引发的文件系统查询。
- 检查所有指定目录，保留每个观察。不存在与不是文件的结果可以形成有限范围内的未找到；权限/其他查询失败、重解析点保留为 unavailable，不能当成不存在。
- 目录解析使用正常 Win32 路径语义，可能受父目录链接、文件系统变化影响，并非路径身份验证或文件系统沙箱。候选重解析点不跟随验证；显式目录及其父目录可能经过链接。
- `filesystem_view=caller_process`：结果来自 CLI 进程的文件系统视图，仍受 WOW64 重定向影响；`--target-arch` 不改变该视图。无需管理员权限，不修改重定向或目标进程。
- Known DLL、SxS、API Set 映射、加载模块、包依赖、应用自定义搜索路径和运行时动态加载未解析；绝不把指定目录集合当成目标进程的完整搜索环境。

## JSON

`data.dependencies` 为 null 或含以下字段的对象：

- `scope=explicit_directories`、`filesystem_view=caller_process`。
- `search_directories`：1–16 个规范化绝对路径，保留调用方顺序。
- `libraries`：最多 1024 项直接普通/延迟导入记录（保留声明顺序和重复项）。
- 每项有 `name`、`kind`、`status`、`candidates`。候选项记录 `directory_index`、`status`、`error`；索引对应目录数组，不重复存储长路径。

| 库 status | 含义 |
| --- | --- |
| `candidate_found` | 至少一个目录观察到普通文件；其他目录的错误仍保留 |
| `not_found_in_search_dirs` | 所有指定目录都已查询，只观察到 not_found/not_file |
| `inconclusive` | 未发现普通文件，至少一次查询 unavailable |
| `api_set_unresolved` | API Set 形式的名称，不做文件探测 |
| `unsupported_name` | 名称不在安全的单文件名支持范围，不做探测 |

候选 status 为 `file`、`not_found`、`not_file`、`unavailable`；只有 unavailable 有非空 `error`，保留真实系统错误码。重解析点是主动标记的 unsupported，无伪造 Win32 错误码。

## Findings、状态和限制

存在 `not_found_in_search_dirs` 时增加一条 `POSSIBLE_MISSING_DEPENDENCY`，severity=warning、confidence=low，汇总普通/延迟导入未找到数量；逐项证据在 dependencies 中。
该 finding 只能证明显式范围内没有普通候选，不能证明全局缺失或加载一定失败。Known DLL/SxS 等依然可能满足依赖，延迟导入可能根本没有被调用。
出现跳过名称、API Set 或不可观察项时补 `INSUFFICIENT_EVIDENCE`；这些诊断不生成新的阻断退出码。
新增 evidence 来源 `filesystem_observation`，与静态导入声明和用户参数分别标记；实际 DLL 加载始终未验证。

沿用 Doctor 已有完成状态和退出码；warning/info 不导致 exit 1。配置目录的失败、全局超时等丢弃整份结果，走顶层操作错误，不伪装成部分成功。
主 PE 读取、诊断和目录检查共用 `--timeout-ms` 预算；Win32 调用不能被强行中断。每项和系统调用前后检查预算，超时不能靠后面的阶段重新获得完整预算。
最多 1024 项库、16 个目录，候选探测最多 16384 次；绝对目录长度最多 32767 个 UTF-16 代码单元（Win32 实际能力仍取决于系统路径设置）。
历史 Schema/golden 保持不变。新 Schema 的不传目录结果也增加 `dependencies:null`。

## 验证

Core API seam 覆盖：找到/不存在/不是文件、权限失败不当成缺失、其他目录找到但保留失败、大小写 API Set、路径/设备名零探测、重复普通/延迟导入、目录和库上限、时间预算以及重解析点。
CLI 覆盖：中文目录、相对目录、多个目录及候选、真实无害 DLL 的直接/延迟导入、空目录、非法目录、选项隔离、输出不覆盖、历史结果/固定数据和 Schema 反例。

## 官方参考

- [Windows DLL 搜索顺序与特殊来源](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-search-order)
- [API Set 名称与契约](https://learn.microsoft.com/en-us/windows/win32/apiindex/windows-apisets)
- [GetFileAttributesW 的属性、错误与符号链接语义](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfileattributesw)
- [Windows 文件命名规则](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file)
- [WOW64 文件系统视图](https://learn.microsoft.com/en-us/windows/win32/winprog64/file-system-redirector)
