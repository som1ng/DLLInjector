# GUI 宿主解析修复交付

2026-10-26，分支 `codex/gui-message-parser`，实现前从 main 建立并推送同名远端。

复查 Gemini 的 GUI 修复时，真实 WebView2 仍无法读取包含 `\uXXXX` 中文与 emoji
转义的有效 DLL 路径。旧实现按子串搜索字段，还可能把字符串值或嵌套字段当成命令。
此次复用已有 jsoncons，替换为分派前的一次性严格解析，并单独验证其孤立代理转义边界。
未修改界面样式或增加命令。详细行为见 [消息契约](GUI_MESSAGE_CONTRACT.md)。

实际本地验证：

- `cmake --build --preset msvc-x64-release --parallel 4`：通过。
- `ctest --preset msvc-x64-release --output-on-failure`：59/59 通过。
- 新增 `gui.messages`：43 个无效输入 fixture，以及有效 Unicode、转义、字段类型、
  整数/长度边界、字段值碰撞和错误恢复检查通过。
- 通过临时 CDP 驱动启动实际 Rondo.exe 与隔离 WebView2 配置目录：将自有
  `noop_fixture.dll` 复制到中文/emoji 路径，再以转义消息设置路径。修复前路径还原失败，
  修复后路径完整还原、Doctor 有效并返回 3 项依赖观察。
- 实际宿主拒绝嵌套命令、重复命令键和字符串 PID；正常 init 消息随后可用，
  当前 DLL 路径与选定进程保持不变。没有执行注入或调用外部模型 API。

构建使用已有 jsoncons 缓存，没有重新下载或升级依赖。探针、DLL、日志和可执行文件
保留在忽略的 build 目录；远端矩阵结果以对应 PR 为准。
BUG-03 的同步执行限制仍保留在 [UI 审计](UI_BUGS.md)，本次不将其标为已解决。
