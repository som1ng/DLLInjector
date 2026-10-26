# Rondo (DLL Injector) UI 与交互缺陷清单与修复报告 (Audit & Resolution Report)

> **2026-10-26 复查**：界面回归 14/14、MSVC x64 Release GUI 构建、`gui.preflight` / `smoke.gui` 2/2 和真实 WebView2 只读诊断检查通过。下表保留原审计记录；BUG-03 仍有未完成部分，不能视为全部关闭。此前报告的其他架构结果不代表本轮复查结果。

本轮真实窗口检查只枚举进程并静态检查自有 `noop_fixture.dll`，得到 3 项依赖诊断；没有执行注入。浏览器测试使用模拟宿主消息，补充了 880×600 双主题按钮边界、最大化状态、长弹窗、折叠侧栏及边缘提示框检查，并修正最大化按钮的无障碍名称。

尚未关闭的宿主问题：BUG-03 的执行函数仍同步运行，原子标志不等于异步任务或排队请求去重；本轮未验证注入执行路径。BUG-19 已在后端分支补齐严格 JSON 解析、Unicode 转义和重复字段校验，见 [消息契约](GUI_MESSAGE_CONTRACT.md)。多显示器 DPI 切换尚未实机验证。

本文档汇总了针对 UI 布局、C++ 宿主交互、CC Switch v7 设计系统规范的全量缺陷扫描及代码级修复详情。

---

## 缺陷总览与修复状态

| 编号 | 级别 | 类别 | 简要描述 | 涉及核心文件与行号 | 修复状态 |
| :---: | :---: | :---: | :--- | :--- | :---: |
| **BUG-01** | **P0** | 布局崩塌 | 默认 1140x760 窗口下 `.page-header` 超宽，右上角窗口控制按钮与执行按钮被挤出屏幕被裁切 | `src/gui_modern/cc_switch_ui.h:161-175, 605-655` | **✅ 已修复** |
| **BUG-02** | **P0** | 业务安全 | `inject` 命令参数 `force` 被 C++ 宿主完全忽略，底层预检阻断防护缺失 | `src/gui_modern/gui_app.cpp:888-890, 1250-1253` | **✅ 已修复** |
| **BUG-03** | **P0** | 稳定性 | 注入与底层等待在 UI 主线程同步阻塞 1~2 秒，且前端缺少防连击锁导致多重注入风险 | `src/gui_modern/gui_app.cpp:895-968, cc_switch_ui.h` | **部分修复：执行仍同步** |
| **BUG-04** | **P0** | 窗体边界 | Win32 原生窗口未响应 `WM_GETMINMAXINFO`，可被随意缩小至破坏性尺寸坍塌 | `src/main_gui.cpp:39-160` | **✅ 已修复** |
| **BUG-05** | **P1** | 无障碍视觉 | 深色模式下危险按钮文字对比度仅 1.84:1，严重发虚不可读（WCAG AA 违规） | `src/gui_modern/cc_switch_ui.h:77, 242` | **✅ 已修复** |
| **BUG-06** | **P1** | 视觉缺失 | 状态芯片缺失 `.chip.neutral` 令牌样式导致 Doctor 建议项呈现为裸字 | `src/gui_modern/cc_switch_ui.h:270-279, 1253` | **✅ 已修复** |
| **BUG-07** | **P1** | 状态同步 | 拖拽载入 `.bin` 载荷时未同步重置不兼容的注入技术（如 HOOK/RFL/HJK） | `src/gui_modern/gui_app.cpp:377-387` | **✅ 已修复** |
| **BUG-08** | **P1** | 字符编码 | `char g_DllPathBuf[MAX_PATH]`（260 字节）在深层中文路径下发生 UTF-8 截断损坏与乱码 | `src/gui_modern/gui_app.cpp:79, 83, 370, 381` | **✅ 已修复** |
| **BUG-09** | **P1** | 交互死锁 | `.dialog` 模态弹窗缺少 `max-height` 与 `overflow-y: auto` 导致小屏下确认/取消按钮被裁切 | `src/gui_modern/cc_switch_ui.h:497-503` | **✅ 已修复** |
| **BUG-10** | **P1** | 性能瓶颈 | `WM_SIZE` 在拖拽缩放窗口边框时高频触发全量状态序列化与 DOM 重建，引发严重掉帧 | `src/main_gui.cpp:125-130` | **✅ 已修复** |
| **BUG-11** | **P2** | 视觉冲突 | 表格选中行在鼠标悬浮（hover）时背景被 `var(--bg-subtle)` 覆写冲刷消失 | `src/gui_modern/cc_switch_ui.h:469, 1222` | **✅ 已修复** |
| **BUG-12** | **P2** | 组件测量 | 分段滑块在切换至隐藏视图（`display: none`）时测量宽度为 0 导致滑块塌陷 | `src/gui_modern/cc_switch_ui.h:946-954` | **✅ 已修复** |
| **BUG-13** | **P2** | 视觉缺失 | 搜索输入框内的清空按钮 `.clear-btn` 样式空白，触发时脱离定位显示原生边框 | `src/gui_modern/cc_switch_ui.h:333-335` | **✅ 已修复** |
| **BUG-14** | **P2** | 语义规范 | `<p id="modalBody"></p>` 内嵌套复杂 `<div>` 违反 HTML5 规范 | `src/gui_modern/cc_switch_ui.h:884` | **✅ 已修复** |
| **BUG-15** | **P2** | 图标语义 | Doctor 建议项错误引用了警告三角标 `#alert` 而非 `#info` | `src/gui_modern/cc_switch_ui.h:1255` | **✅ 已修复** |
| **BUG-16** | **P2** | 滚动体验 | 数据表格向下滚动时表头滚出视口，缺少 `position: sticky; top: 0` | `src/gui_modern/cc_switch_ui.h:467` | **✅ 已修复** |
| **BUG-17** | **P2** | 侧栏边界 | 侧栏折叠至 68px 时所有主导航图标缺少 Tooltip 提示 | `src/gui_modern/cc_switch_ui.h:147, 568-588` | **✅ 已修复** |
| **BUG-18** | **P2** | DPI 适配 | 主程序开启了 Per-Monitor V2 但未处理 `WM_DPICHANGED`，跨屏移动尺寸失真 | `src/main_gui.cpp:45-160` | **✅ 已修复** |
| **BUG-19** | **P2** | 数据健壮性 | 手写 `ExtractJson*Field` 存在子串碰撞、跨字段误匹配与 Unicode `\uXXXX` 丢失 | `src/gui_modern/gui_app.cpp:116-170` | **✅ 已修复** |
| **BUG-20** | **P2** | 微交互规范 | 交互元素缺少统一贝塞尔曲线 `var(--ease)` 与键盘 Tab 聚焦轮廓 `:focus-visible` | `src/gui_modern/cc_switch_ui.h:228, 232` | **✅ 已修复** |

---

## 详细缺陷说明与修复指引

### 🔴 P0 致命缺陷

#### BUG-01: 默认 1140x760 窗口下 `.page-header` 超宽将右上角按钮挤出屏幕
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 161–175 行，第 605–655 行
- **复现场景**: 以默认 `1140x760` 尺寸启动程序，或在选中一个较长进程名后，顶部标题栏右上角的「最小化、最大化、关闭」系统按钮以及「执行注入」按钮被完全推到屏幕右侧视口之外被裁切。
- **根因分析**:
  1. 主窗口扣除侧栏 200px 后仅剩约 882px 净可用宽度。
  2. 右侧 `.header-actions` 自然宽度约 503px，设置了 `flex-shrink: 0`。
  3. 左侧 `.header-left` 内包含标题、长副标题（约 250px）、宿主架构芯片、提权芯片与选中目标进程芯片，总宽达 764px。
  4. `.header-sub` 缺少 `min-width: 0`，且各 `.chip` 强制声明了 `flex-shrink: 0`，导致左侧完全无法随窗口收缩，总宽度达 1283px，将右侧窗口控制按钮直接顶出屏幕。
- **修复方案**:
  1. 给 `.header-sub` 补充 `min-width: 0; flex-shrink: 1;`，并在紧凑宽度下自动截断或隐藏：
     ```css
     .header-sub { font-size: 13px; color: var(--text-3); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; min-width: 0; flex-shrink: 1; }
     @media (max-width: 1200px) { .header-sub { display: none; } }
     ```
  2. 移除 `.header-left .chip { flex-shrink: 0; }` 的硬性约束，空间不足时允许次要芯片隐藏或自适应缩小。
  3. 窗口控制按钮组 `.window-controls` 设置最高布局层级：`flex-shrink: 0; margin-left: auto;`。

---

#### BUG-02: `inject` 命令参数 `force` 被 C++ 宿主完全忽略，预检阻断防护失效
- **文件**: `src/gui_modern/gui_app.cpp`
- **代码行号**: 第 888–890 行，第 1250–1253 行
- **复现场景**: 前端发现目标进程与 DLL 架构严重冲突（如 x86 注入 x64），若前端未阻断直接发出了 `force: false`，C++ 宿主不会进行拦截，仍然直接调用底层注入 API 导致目标崩溃。
- **根因分析**:
  前端在遇到阻断项时弹出模态框，用户点击「仍然执行注入」发送 `force: true`，正常执行发送 `force: false`。但宿主端收到 `inject` 后，完全未提取 `force` 字段，且 `ExecuteInjectionInternal` 无论有无阻止性错误均向下执行。
- **修复方案**:
  1. 在 `HandleWebViewMessage` 的 `cmd == "inject"` 分支中提取 `force`：
     ```cpp
     bool force = ExtractJsonBoolField(jsonMessage, "force", false);
     ExecuteInjectionInternal(force, toast, tone);
     ```
  2. 在 `ExecuteInjectionInternal(bool force, ...)` 中加入安全拦截：
     ```cpp
     if (!force && g_Preflight.valid && g_Preflight.hasBlockingErrors) {
         Logger::Instance().Warn("注入已拦截: Core Doctor 检测到阻断性严重错误 (如架构不匹配)。");
         outToast = "注入被安全拦截：检测到阻断性错误，请在确认后强制执行";
         outTone = "err";
         return false;
     }
     ```

---

#### BUG-03: 注入主线程同步阻塞 1~2 秒，且前端缺少防连击锁导致多重注入风险
- **文件**: `src/gui_modern/gui_app.cpp` 与 `src/gui_modern/cc_switch_ui.h`
- **代码行号**: `gui_app.cpp:895-968`，`cc_switch_ui.h:1120-1135`
- **复现场景**: 点击「执行注入」后，窗口消息循环被底层 `WaitForSingleObject` 冻结 1~2 秒。此时快速多次点击按钮，导致多个注入指令排队执行。
- **修复方案**:
  1. 前端在 `triggerInject()` 触发后立即设置按钮禁用态并呈现 Loading：
     ```javascript
     const btn = document.getElementById("btnExecuteMain");
     btn.disabled = true;
     document.getElementById("btnExecuteText").textContent = "正在注入...";
     ```
     在 `applyState` 或收到 Toast 响应后恢复按钮状态。
  2. C++ 宿主增加原子互斥锁：
     ```cpp
     static std::atomic<bool> g_IsInjecting{false};
     if (g_IsInjecting.exchange(true)) {
         PushWebViewState("已有注入任务正在执行中，请稍候", "warn");
         return;
     }
     // 执行完毕后 g_IsInjecting = false;
     ```

---

#### BUG-04: Win32 原生窗口未响应 `WM_GETMINMAXINFO`，可被随意缩小至破坏性尺寸
- **文件**: `src/main_gui.cpp`
- **代码行号**: 第 39–160 行 (`WindowProc`)
- **复现场景**: 拖拽窗口边框将窗口缩小到 300x200 时，所有组件挤压成一团严重坍塌。
- **修复方案**:
  在 `WindowProc` 中拦截 `WM_GETMINMAXINFO` 设定最小窗体尺寸：
  ```cpp
  case WM_GETMINMAXINFO: {
      LPMINMAXINFO lpMMI = reinterpret_cast<LPMINMAXINFO>(lParam);
      lpMMI->ptMinTrackSize.x = 880;
      lpMMI->ptMinTrackSize.y = 600;
      return 0;
  }
  ```

---

### 🟡 P1 核心功能与交互缺陷

#### BUG-05: 深色模式下危险按钮文字对比度仅 1.84:1，严重看不清（WCAG AA 违规）
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 77 行，第 242 行
- **复现场景**: 触发注入阻断模态弹窗时，深色模式下的「仍然执行注入」按钮，白色文字在浅珊瑚红 `#f87171` 背景上发白虚化，对比度只有 1.84:1。
- **修复方案**:
  1. 在 `:root` 中补充：`--danger-solid: #dc2626; --danger-on: #ffffff;`
  2. 在 `:root[data-theme="dark"]` 中补充：`--danger-solid: #b91c1c; --danger-on: #ffffff;`
  3. 修改样式：`.btn.danger { background: var(--danger-solid); color: var(--danger-on); font-weight: 600; }`

---

#### BUG-06: 状态芯片缺少 `.chip.neutral` 令牌样式导致展示破损
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 270–279 行，第 1253 行
- **复现场景**: DLL Doctor 诊断列表中的非致命项（`info` / `suggestion`）渲染为无背景边框的裸字。
- **修复方案**:
  在 CSS 芯片区域添加 `.chip.neutral`：
  ```css
  .chip.neutral { background: var(--bg-subtle); color: var(--text-2); border: 1px solid var(--border); }
  ```

---

#### BUG-07: 拖拽载入 `.bin` 载荷时未同步重置不兼容的注入技术
- **文件**: `src/gui_modern/gui_app.cpp`
- **代码行号**: 第 377–387 行
- **复现场景**: 原先停留在 Hook 或 Reflective 模式，直接把 `.bin` 文件拖入窗口并注入，直接报错“不支持 Shellcode 模式”。
- **修复方案**:
  在 `OnDropFile` 的 `.bin` 处理分支中加入重置判断：
  ```cpp
  if (g_SelectedMethod == MethodID::HOOK ||
      g_SelectedMethod == MethodID::REFLECTIVE ||
      g_SelectedMethod == MethodID::HIJACK) {
      g_SelectedMethod = MethodID::CRT;
  }
  ```

---

#### BUG-08: `char g_DllPathBuf[MAX_PATH]` 260 字节在深层中文路径下发生 UTF-8 截断损坏
- **文件**: `src/gui_modern/gui_app.cpp`
- **代码行号**: 第 79, 83, 370, 381, 1216, 1222 行
- **复现场景**: 选择或拖入较深中文目录下的 DLL 时，`snprintf` 将路径截断在多字节字符中间，导致乱码且文件无法打开。
- **修复方案**:
  将静态缓冲区由 `MAX_PATH`（260 字节）扩大至 `4096` 字节：
  ```cpp
  char g_DllPathBuf[4096] = "";
  char g_BinPathBuf[4096] = "";
  ```
  同时在 `OpenFileDialog` 与 `SaveFileDialog` 中将 `wchar_t szFile[MAX_PATH]` 扩大至 `1024` 宽字符。

---

#### BUG-09: `.dialog` 模态弹窗缺少 `max-height` 与 `overflow-y: auto` 导致小屏下模态死锁
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 497–503 行
- **复现场景**: 小分辨率或高 DPI 缩放下，弹窗高度超出屏幕，确认与取消按钮在视口外且无法滚动。
- **修复方案**:
  ```css
  .dialog { width: min(440px, 92vw); max-height: 85vh; display: flex; flex-direction: column; }
  #modalBody { overflow-y: auto; max-height: calc(85vh - 120px); }
  ```

---

#### BUG-10: `WM_SIZE` 在拖拽缩放时高频触发全量状态序列化与 DOM 重建
- **文件**: `src/main_gui.cpp`
- **代码行号**: 第 125–130 行
- **复现场景**: 拖动窗口边框调整大小时，鼠标出现严重粘滞感与掉帧。
- **修复方案**:
  在 `WM_SIZE` 中常规尺寸调整仅触发 WebView 重新布局，仅在最大化状态实际反转时才推送全量状态：
  ```cpp
  case WM_SIZE:
      if (wParam != SIZE_MINIMIZED && gui::webview_host::IsActive()) {
          gui::webview_host::OnResize(hwnd);
          static bool lastZoomed = false;
          bool currentZoomed = IsZoomed(hwnd) != FALSE;
          if (currentZoomed != lastZoomed) {
              lastZoomed = currentZoomed;
              gui::PushWebViewState();
          }
      }
      return 0;
  ```

---

### 🟢 P2 视觉与体验细节优化

#### BUG-11: 表格选中行在鼠标悬浮（hover）时背景被覆写冲刷消失
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 469 行，第 1222 行
- **修复方案**: 在 CSS 中定义专属 `.selected` 样式：
  ```css
  table.log tr.selected td { background: var(--direct-soft); }
  table.log tr.selected:hover td { background: color-mix(in srgb, var(--direct-soft) 80%, var(--bg-subtle)); }
  ```
  在 `renderProcesses()` 中将内联 `style="background:var(--direct-soft)"` 替换为 `class="${sel ? 'selected' : ''}"`。

#### BUG-12: 分段滑块在非活动视图中测量宽度为 0 导致滑块塌陷
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 946–954 行
- **修复方案**: 在 `moveSegThumb` 中加入保护：
  ```javascript
  if (!active || (active.offsetWidth === 0 && active.offsetHeight === 0)) return;
  ```

#### BUG-13: 搜索输入框内的清空按钮 `.clear-btn` 样式空白
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 333–335 行
- **修复方案**: 补充绝对定位与无边框圆钮样式：
  ```css
  .input-wrap > .clear-btn {
    position: absolute; right: 6px; top: 50%; transform: translateY(-50%);
    width: 20px; height: 20px; border: 0; border-radius: 50%;
    background: transparent; color: var(--text-3); cursor: pointer;
    display: inline-flex; align-items: center; justify-content: center; padding: 0;
  }
  ```

#### BUG-14: `<p id="modalBody"></p>` 内嵌套复杂 `<div>` 违反 HTML5 规范
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 884 行
- **修复方案**: 将 `<p id="modalBody"></p>` 改为 `<div id="modalBody"></div>`。

#### BUG-15: Doctor 建议项错误引用了警告三角标 `#alert` 而非 `#info`
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 1255 行
- **修复方案**:
  ```javascript
  const ic = f.severity === 'error' ? 'circle-alert' : (f.severity === 'warning' ? 'alert' : 'info');
  ```

#### BUG-16: 数据表格向下滚动时表头滚出视口
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 467 行
- **修复方案**:
  ```css
  table.log th { position: sticky; top: 0; background: var(--bg-card); z-index: 2; box-shadow: 0 1px 0 var(--border); }
  ```

#### BUG-17: 侧栏折叠至 68px 时所有主导航图标缺少 Tooltip 提示
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 568–588 行
- **修复方案**: 为各 `.nav-item` 增加 `data-tip="注入工作台"`、`data-tip="目标进程"` 等属性。

#### BUG-18: 主程序未响应 `WM_DPICHANGED`
- **文件**: `src/main_gui.cpp`
- **代码行号**: 第 45–160 行
- **修复方案**:
  ```cpp
  case WM_DPICHANGED: {
      RECT* const prcNewWindow = (RECT*)lParam;
      SetWindowPos(hwnd, NULL,
          prcNewWindow->left, prcNewWindow->top,
          prcNewWindow->right - prcNewWindow->left,
          prcNewWindow->bottom - prcNewWindow->top,
          SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
  }
  ```

#### BUG-19: 手写 `ExtractJson*Field` 解析器子串碰撞与 Unicode 转义缺失
- **文件**: `src/gui_modern/gui_app.cpp`
- **代码行号**: 第 116–170 行
- **实现**: `web_message.cpp` 使用 jsoncons 一次性解析完整消息，先验证字段与 Unicode，再交由宿主分派。契约和回归范围见 [GUI_MESSAGE_CONTRACT.md](GUI_MESSAGE_CONTRACT.md)。

#### BUG-20: 缺乏统一贝塞尔曲线 `var(--ease)` 与键盘 `:focus-visible` 焦点轮廓
- **文件**: `src/gui_modern/cc_switch_ui.h`
- **代码行号**: 第 228, 232 行
- **修复方案**: 统一所有 transition 补充 `var(--ease)`，并补充交互组件的 `:focus-visible` 外框。

