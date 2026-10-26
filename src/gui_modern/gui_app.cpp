#include "gui_app.h"
#include "gui_theme.h"
#include "gui_logger.h"
#include "preflight.h"
#include "webview_host.h"
#include "web_message.h"
#include "methods/injector_methods.h"
#include "dllinjector/text.h"
#include <commdlg.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <cctype>

namespace gui {

ImFont* g_MainFont = nullptr;
ImFont* g_MonoFont = nullptr;

namespace {

enum class PayloadMode {
    Dll,
    Shellcode
};

enum class MethodID {
    CRT = 0,
    APC = 1,
    HOOK = 2,
    REFLECTIVE = 3,
    HIJACK = 4,
    SYSCALL = 5
};

struct MethodCardInfo {
    MethodID id;
    const char* title;
    const char* tag;
    const char* tagStyle;
    const char* description;
    bool supportsDll;
    bool supportsShellcode;
};

const MethodCardInfo g_MethodCards[] = {
    { MethodID::CRT, "1. CreateRemoteThread", "通用 / 经典", "cyan",
      "通过 VirtualAllocEx 写入载荷，CreateRemoteThread 唤起远程线程执行。", true, true },
    { MethodID::APC, "2. QueueUserAPC", "通用 / 异步", "cyan",
      "投递异步过程调用至目标可警惕线程队列，无新线程特征注入。", true, true },
    { MethodID::HOOK, "3. SetWindowsHookEx", "仅 DLL / 钩子", "purple",
      "安装 WH_GETMESSAGE 钩子并在目标窗口触发消息以强制模块加载。", true, false },
    { MethodID::REFLECTIVE, "4. Reflective Injection", "仅 DLL / 内存", "purple",
      "内存自解析 PE 头与重定位表直接映射执行，规避磁盘文件检测。", true, false },
    { MethodID::HIJACK, "5. DLL Hijacking", "仅 DLL / 劫持", "purple",
      "部署同名代理 DLL 至目标程序目录，利用系统加载顺序自动接管。", true, false },
    { MethodID::SYSCALL, "6. Direct Syscall", "实验性 / 系统调用", "amber",
      "绕过 API Hook 的直接 NT 系统调用（当前基线中作为实验性保留）。", true, true }
};

struct ProcessItem {
    DWORD pid;
    std::wstring name;
    std::string name_utf8;
    utils::ProcessArchitecture arch;
    std::string archStr;
};

// Application State
std::vector<ProcessItem> g_Processes;
int g_SelectedPid = -1;
char g_SearchFilter[128] = "";
int g_ArchFilterTab = 0; // 0: 全部, 1: 64位, 2: 32位

PayloadMode g_CurrentPayloadMode = PayloadMode::Dll;
MethodID g_SelectedMethod = MethodID::CRT;

char g_DllPathBuf[4096] = "";
std::wstring g_CurrentDllPath;
PreflightResult g_Preflight;

char g_BinPathBuf[4096] = "";
char g_HexInputBuf[8192] = "";
std::vector<unsigned char> g_ParsedHexBytes;

bool g_DebugPrivilegeActive = false;
HWND g_MainHwnd = nullptr;
static std::atomic<bool> g_IsInjecting{false};

std::string JsonEscapeStr(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 16);
    for (unsigned char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned int>(c));
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

// Helpers
std::string ToLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return lower;
}

std::string TrimPathString(std::string str) {
    size_t first = str.find_first_not_of(" \t\r\n\"'");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

std::wstring TrimPathWString(std::wstring str) {
    size_t first = str.find_first_not_of(L" \t\r\n\"'");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\r\n\"'");
    return str.substr(first, (last - first + 1));
}

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

bool CopyUtf8ToClipboard(HWND owner, const std::string& utf8Text) {
    std::wstring wide = Utf8ToWide(utf8Text);
    if (!OpenClipboard(owner)) return false;
    EmptyClipboard();
    size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    void* ptr = GlobalLock(hMem);
    if (ptr) {
        memcpy(ptr, wide.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    } else {
        GlobalFree(hMem);
    }
    CloseClipboard();
    return true;
}

std::wstring GetProcessPath(DWORD pid) {
    std::wstring path;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t buffer[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
            path = buffer;
        }
        CloseHandle(hProcess);
    }
    return path;
}

std::vector<unsigned char> ParseHexInput(const std::string& input) {
    std::vector<unsigned char> bytes;
    std::string hexStr;
    for (size_t i = 0; i < input.length(); ++i) {
        char c = input[i];
        if (c == '0' && i + 1 < input.length() && (input[i+1] == 'x' || input[i+1] == 'X')) {
            i++; continue;
        }
        if (isxdigit(static_cast<unsigned char>(c))) hexStr += c;
    }
    if (hexStr.empty() || hexStr.length() % 2 != 0) return {};
    for (size_t i = 0; i < hexStr.length(); i += 2) {
        std::string byteStr = hexStr.substr(i, 2);
        unsigned long b = std::strtoul(byteStr.c_str(), nullptr, 16);
        bytes.push_back(static_cast<unsigned char>(b));
    }
    return bytes;
}

std::wstring OpenFileDialog(const wchar_t* filter, const wchar_t* title) {
    wchar_t szFile[1024] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_MainHwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrTitle = title;
    if (GetOpenFileNameW(&ofn)) {
        return szFile;
    }
    return L"";
}

std::wstring SaveFileDialog(const wchar_t* filter, const wchar_t* defaultExt, const wchar_t* title) {
    wchar_t szFile[1024] = L"";
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_MainHwnd;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(szFile[0]);
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = defaultExt;
    ofn.lpstrTitle = title;
    if (GetSaveFileNameW(&ofn)) {
        return szFile;
    }
    return L"";
}

std::optional<ProcessItem> GetSelectedProcessItem() {
    if (g_SelectedPid <= 0) return std::nullopt;
    for (const auto& p : g_Processes) {
        if (static_cast<int>(p.pid) == g_SelectedPid) return p;
    }
    return std::nullopt;
}

void TriggerPreflight() {
    if (g_CurrentDllPath.empty()) {
        g_Preflight = PreflightResult{};
        return;
    }
    auto selectedProc = GetSelectedProcessItem();
    std::optional<utils::ProcessArchitecture> targetArch;
    if (selectedProc) targetArch = selectedProc->arch;

    g_Preflight = PreflightDoctor::AnalyzeDll(g_CurrentDllPath, targetArch);

    if (g_Preflight.valid) {
        Logger::Instance().Doctor("DLL Doctor 完成分析: " + g_Preflight.dllArchStr +
            ", 导入库 " + std::to_string(g_Preflight.importCount) +
            " 个, Findings: " + std::to_string(g_Preflight.findings.size()) + " 条");
    } else {
        Logger::Instance().Warn("DLL Doctor 预检异常: " + g_Preflight.errorMessage);
    }
}

void RefreshProcesses() {
    g_Processes.clear();
    auto rawList = utils::GetAllProcesses();
    for (const auto& proc : rawList) {
        ProcessItem item;
        item.pid = proc.pid;
        item.name = proc.name;
        auto utf8Name = dllinjector::ToUtf8(proc.name);
        item.name_utf8 = utf8Name ? utf8Name.value() : "unknown";

        auto archRes = utils::QueryProcessArchitecture(proc.pid);
        item.arch = archRes.architecture;
        switch (archRes.architecture) {
            case utils::ProcessArchitecture::X64:   item.archStr = "x64"; break;
            case utils::ProcessArchitecture::X86:   item.archStr = "x86"; break;
            case utils::ProcessArchitecture::Arm64: item.archStr = "ARM64"; break;
            default:                                item.archStr = "受限"; break;
        }
        g_Processes.push_back(std::move(item));
    }
    Logger::Instance().Info("进程列表已刷新，共获取到 " + std::to_string(g_Processes.size()) + " 个活跃进程。");
    TriggerPreflight();
}

} // namespace

void InitGuiApp(HWND mainHwnd) {
    g_MainHwnd = mainHwnd;
    Logger::Instance().Info("DLL Injector (CC Switch v7 WebView2 / Modern UI) 初始化完成。");
    #if defined(_WIN64)
    Logger::Instance().Info("当前宿主构建架构: 64-bit (x64)");
    #else
    Logger::Instance().Info("当前宿主构建架构: 32-bit (x86)");
    #endif
    RefreshProcesses();
}

void OnDropFile(const wchar_t* path) {
    if (!path) return;
    std::wstring p = TrimPathWString(path);
    if (p.empty()) return;
    std::wstring ext = PathFindExtensionW(p.c_str());
    std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

    if (ext == L".dll") {
        g_CurrentPayloadMode = PayloadMode::Dll;
        g_CurrentDllPath = p;
        auto u8 = dllinjector::ToUtf8(p);
        if (u8) {
            std::snprintf(g_DllPathBuf, sizeof(g_DllPathBuf), "%s", u8.value().c_str());
            Logger::Instance().Info("已通过拖拽载入 DLL: " + u8.value());
        }
        TriggerPreflight();
        if (webview_host::IsActive()) {
            PushWebViewState("已通过拖放载入 DLL 并完成静态预检", "ok");
        }
    } else {
        g_CurrentPayloadMode = PayloadMode::Shellcode;
        if (g_SelectedMethod == MethodID::HOOK ||
            g_SelectedMethod == MethodID::REFLECTIVE ||
            g_SelectedMethod == MethodID::HIJACK) {
            g_SelectedMethod = MethodID::CRT;
        }
        auto u8 = dllinjector::ToUtf8(p);
        if (u8) {
            std::snprintf(g_BinPathBuf, sizeof(g_BinPathBuf), "%s", u8.value().c_str());
            auto data = utils::ReadFileToBuffer(p);
            g_ParsedHexBytes = data;
            Logger::Instance().Info("已通过拖拽载入二进制载荷 (" + std::to_string(data.size()) + " 字节)");
        }
        if (webview_host::IsActive()) {
            PushWebViewState("已通过拖放载入二进制载荷 (" + std::to_string(g_ParsedHexBytes.size()) + " 字节)", "ok");
        }
    }
}

// -------------------------------------------------------------
// UI Rendering Functions
// -------------------------------------------------------------

static void RenderHeaderBar() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface1);
    ImGui::BeginChild("HeaderBar", ImVec2(0, 52), false, ImGuiWindowFlags_NoScrollbar);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Accent, "DLL INJECTOR");
    ImGui::SameLine();
    RenderStatusBadge("v2.0-modern", "accent");
    ImGui::SameLine();
    #if defined(_WIN64)
    RenderStatusBadge("宿主: x64", "accent");
    #else
    RenderStatusBadge("宿主: x86", "amber");
    #endif

    ImGui::SameLine();
    if (g_DebugPrivilegeActive) {
        RenderStatusBadge("SeDebug: 已启用", "green");
    } else {
        RenderStatusBadge("SeDebug: 未提权", "amber");
        ImGui::SameLine();
        if (ImGui::SmallButton("启用提权")) {
            if (utils::EnableDebugPrivilege()) {
                g_DebugPrivilegeActive = true;
                Logger::Instance().Success("已成功提升进程令牌权限 (SeDebugPrivilege)。");
            } else {
                Logger::Instance().Error("提升 SeDebugPrivilege 权限失败 (需管理员权限运行)。");
            }
        }
    }

    // Theme Switcher & Arc Accent Presets (Strictly NO BLUE)
    ImGui::SameLine();
    const bool isDark = (gui::GetCurrentThemeMode() == gui::ThemeMode::Dark);
    if (ImGui::SmallButton(isDark ? "浅色模式" : "深色模式")) {
        gui::ToggleTheme();
    }

    ImGui::SameLine();
    const char* accentLabel = "重点色: 紫罗兰";
    switch (gui::GetCurrentAccent()) {
        case gui::AccentPreset::Violet:  accentLabel = "重点色: 紫罗兰"; break;
        case gui::AccentPreset::Green:   accentLabel = "重点色: 翡翠绿"; break;
        case gui::AccentPreset::Amber:   accentLabel = "重点色: 琥珀金"; break;
        case gui::AccentPreset::Neutral: accentLabel = "重点色: 极简灰"; break;
    }
    if (ImGui::SmallButton(accentLabel)) {
        switch (gui::GetCurrentAccent()) {
            case gui::AccentPreset::Violet:  gui::SetAccent(gui::AccentPreset::Green); break;
            case gui::AccentPreset::Green:   gui::SetAccent(gui::AccentPreset::Amber); break;
            case gui::AccentPreset::Amber:   gui::SetAccent(gui::AccentPreset::Neutral); break;
            case gui::AccentPreset::Neutral: gui::SetAccent(gui::AccentPreset::Violet); break;
        }
    }

    // Right-aligned Payload Mode Selector
    float modeWidth = 260.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > modeWidth) {
        ImGui::SameLine(ImGui::GetWindowWidth() - modeWidth - 16.0f);
    } else {
        ImGui::SameLine();
    }

    if (RenderSegmentButton("DLL 注入模式", g_CurrentPayloadMode == PayloadMode::Dll, ImVec2(125, 30))) {
        g_CurrentPayloadMode = PayloadMode::Dll;
        TriggerPreflight();
    }
    ImGui::SameLine(0, 6);
    if (RenderSegmentButton("Shellcode 模式", g_CurrentPayloadMode == PayloadMode::Shellcode, ImVec2(125, 30))) {
        g_CurrentPayloadMode = PayloadMode::Shellcode;
        // In Shellcode mode, if selected method only supports DLL, switch to CRT
        if (g_SelectedMethod == MethodID::HOOK ||
            g_SelectedMethod == MethodID::REFLECTIVE ||
            g_SelectedMethod == MethodID::HIJACK) {
            g_SelectedMethod = MethodID::CRT;
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void RenderProcessListPane(float width) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface1);
    ImGui::BeginChild("ProcessPane", ImVec2(width, 0), true);

    // Section Title & Refresh
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Accent, "目标进程");
    ImGui::SameLine();
    std::string countBadge = std::to_string(g_Processes.size()) + " 个";
    RenderStatusBadge(countBadge.c_str(), "muted");

    ImGui::SameLine(ImGui::GetWindowWidth() - 75.0f);
    if (ImGui::SmallButton("刷新")) {
        RefreshProcesses();
    }

    // Search filter with clear button
    if (g_SearchFilter[0] != '\0') {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 32.0f);
        ImGui::InputTextWithHint("##SearchFilter", "输入进程名或 PID 实时过滤...", g_SearchFilter, sizeof(g_SearchFilter));
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            g_SearchFilter[0] = '\0';
        }
    } else {
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##SearchFilter", "输入进程名或 PID 实时过滤...", g_SearchFilter, sizeof(g_SearchFilter));
    }

    // Arch filter tabs
    if (RenderSegmentButton("全部", g_ArchFilterTab == 0, ImVec2(80, 24))) g_ArchFilterTab = 0;
    ImGui::SameLine(0, 4);
    if (RenderSegmentButton("64位", g_ArchFilterTab == 1, ImVec2(80, 24))) g_ArchFilterTab = 1;
    ImGui::SameLine(0, 4);
    if (RenderSegmentButton("32位", g_ArchFilterTab == 2, ImVec2(80, 24))) g_ArchFilterTab = 2;

    ImGui::Separator();

    // Process Table
    float footerHeight = 65.0f;
    ImGui::BeginChild("ProcessTableChild", ImVec2(0, -footerHeight), false);

    if (ImGui::BeginTable("ProcessTable", 4,
                          ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("PID", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("进程名称", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("架构", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableSetupColumn("匹配", ImGuiTableColumnFlags_WidthFixed, 65.0f);
        ImGui::TableHeadersRow();

        std::string filterLower = ToLower(g_SearchFilter);

        for (const auto& proc : g_Processes) {
            // Apply Arch Tab
            if (g_ArchFilterTab == 1 && proc.arch != utils::ProcessArchitecture::X64) continue;
            if (g_ArchFilterTab == 2 && proc.arch != utils::ProcessArchitecture::X86) continue;

            // Apply Search Query
            if (!filterLower.empty()) {
                std::string pidStr = std::to_string(proc.pid);
                std::string nameLower = ToLower(proc.name_utf8);
                if (nameLower.find(filterLower) == std::string::npos &&
                    pidStr.find(filterLower) == std::string::npos) {
                    continue;
                }
            }

            ImGui::TableNextRow();
            bool isSelected = (g_SelectedPid == static_cast<int>(proc.pid));

            // Column 0: PID
            ImGui::TableSetColumnIndex(0);
            char pidBuf[32];
            std::snprintf(pidBuf, sizeof(pidBuf), "%u", proc.pid);
            if (ImGui::Selectable(pidBuf, isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                g_SelectedPid = static_cast<int>(proc.pid);
                Logger::Instance().Info("已选定目标进程: " + proc.name_utf8 + " (PID: " + std::to_string(proc.pid) + ", 架构: " + proc.archStr + ")");
                TriggerPreflight();
            }

            // Column 1: Name
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(proc.name_utf8.c_str());

            // Column 2: Arch
            ImGui::TableSetColumnIndex(2);
            if (proc.arch == utils::ProcessArchitecture::X64) {
                RenderStatusBadge("x64", "accent");
            } else if (proc.arch == utils::ProcessArchitecture::X86) {
                RenderStatusBadge("x86", "amber");
            } else {
                RenderStatusBadge("受限", "muted");
            }

            // Column 3: Status / Compatibility
            ImGui::TableSetColumnIndex(3);
            if (proc.arch == utils::ProcessArchitecture::Unknown) {
                RenderStatusBadge("受限", "muted");
            } else if (g_Preflight.valid) {
                if (proc.arch == g_Preflight.dllArch) {
                    RenderStatusBadge("兼容", "green");
                } else {
                    RenderStatusBadge("冲突", "red");
                }
            } else {
                RenderStatusBadge("待定", "muted");
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild(); // ProcessTableChild

    // Selected Process Footer Box
    ImGui::Separator();
    auto selected = GetSelectedProcessItem();
    if (selected) {
        ImGui::TextColored(theme::Accent, "当前目标:");
        ImGui::SameLine();
        ImGui::TextColored(theme::TextPrimary, "%s", selected->name_utf8.c_str());
        ImGui::SameLine();
        std::string pBadge = "PID: " + std::to_string(selected->pid);
        RenderStatusBadge(pBadge.c_str(), "accent");
        ImGui::SameLine();
        const char* archStyle = "muted";
        if (selected->arch == utils::ProcessArchitecture::X64) archStyle = "accent";
        else if (selected->arch == utils::ProcessArchitecture::X86) archStyle = "amber";
        RenderStatusBadge(selected->archStr.c_str(), archStyle);
    } else {
        ImGui::TextColored(theme::TextDark, "尚未选择目标进程 (请在上方表格中单击选择)");
    }

    ImGui::EndChild(); // ProcessPane
    ImGui::PopStyleColor();
}

static void RenderPayloadPane() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface1);
    ImGui::BeginChild("PayloadCard", ImVec2(0, 0), true);

    if (g_CurrentPayloadMode == PayloadMode::Dll) {
        // DLL Mode
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Accent, "DLL 载荷设置");
        ImGui::SameLine();
        RenderStatusBadge("PE 动态库", "accent");

        // File selection input
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
        if (ImGui::InputTextWithHint("##DllPath", "输入或选择目标 .dll 绝对路径...", g_DllPathBuf, sizeof(g_DllPathBuf))) {
            std::string clean = TrimPathString(g_DllPathBuf);
            g_CurrentDllPath = Utf8ToWide(clean);
            TriggerPreflight();
        }
        ImGui::SameLine();
        if (ImGui::Button("浏览...", ImVec2(90, 0))) {
            std::wstring file = OpenFileDialog(L"Dynamic Link Library (*.dll)\0*.dll\0All Files (*.*)\0*.*\0", L"选择注入 DLL");
            if (!file.empty()) {
                g_CurrentDllPath = file;
                auto u8 = dllinjector::ToUtf8(file);
                if (u8) std::snprintf(g_DllPathBuf, sizeof(g_DllPathBuf), "%s", u8.value().c_str());
                TriggerPreflight();
            }
        }

        // Drag & Drop hint zone
        ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface2);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
        ImGui::BeginChild("DropZone", ImVec2(0, 36), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextColored(theme::TextDark, "  [拖放支持] 支持将 .dll 文件直接拖入本窗口任意区域完成自动加载与分析");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        // Core Doctor Preflight Card
        if (!g_CurrentDllPath.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(theme::Accent, "Core 预检报告 (Preflight Diagnostics)");

            if (g_Preflight.valid) {
                // Match Status Banner
                if (g_Preflight.matchStatus == ArchMatchStatus::Match) {
                    RenderStatusBadge("[兼容] 架构完全匹配 (Compatible)", "green");
                } else if (g_Preflight.matchStatus == ArchMatchStatus::Mismatch) {
                    RenderStatusBadge("[冲突] 架构冲突: DLL 与目标进程架构不一致", "red");
                } else {
                    RenderStatusBadge("[待定] 目标进程待选定 (Select Target)", "amber");
                }

                ImGui::SameLine();
                std::string archDesc = "模块架构: " + g_Preflight.dllArchStr;
                RenderStatusBadge(archDesc.c_str(), "cyan");

                ImGui::SameLine();
                std::string impDesc = "导入表: " + std::to_string(g_Preflight.importCount) + " 库";
                RenderStatusBadge(impDesc.c_str(), "muted");

                // Findings & Dependencies summary
                if (!g_Preflight.findings.empty()) {
                    ImGui::Spacing();
                    for (const auto& f : g_Preflight.findings) {
                        const char* style = (f.severity == dllinjector::FindingSeverity::Error) ? "red" :
                                            (f.severity == dllinjector::FindingSeverity::Warning) ? "amber" : "cyan";
                        RenderStatusBadge(f.rule_id.c_str(), style);
                        ImGui::SameLine();
                        ImGui::TextColored(theme::TextPrimary, "%s", f.message.c_str());
                    }
                }

                // Action buttons: Copy JSON & Export Report
                ImGui::Spacing();
                if (ImGui::SmallButton("📋 复制 Doctor JSON")) {
                    auto proc = GetSelectedProcessItem();
                    std::string json = PreflightDoctor::GenerateJsonReport(g_Preflight, proc ? proc->pid : 0);
                    ImGui::SetClipboardText(json.c_str());
                    Logger::Instance().Success("已复制 Core Doctor 诊断 JSON 到系统剪贴板。");
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("💾 导出诊断报告...")) {
                    std::wstring savePath = SaveFileDialog(L"JSON Report (*.json)\0*.json\0Text Report (*.txt)\0*.txt\0", L"json", L"保存诊断报告");
                    if (!savePath.empty()) {
                        auto proc = GetSelectedProcessItem();
                        std::string content = (savePath.find(L".json") != std::wstring::npos) ?
                            PreflightDoctor::GenerateJsonReport(g_Preflight, proc ? proc->pid : 0) :
                            PreflightDoctor::GenerateTextReport(g_Preflight, proc ? proc->pid : 0);
                        std::ofstream ofs(savePath.c_str(), std::ios::binary);
                        if (ofs) {
                            ofs.write(content.data(), content.size());
                            auto u8Path = dllinjector::ToUtf8(savePath);
                            Logger::Instance().Success("已成功导出报告到: " + (u8Path ? u8Path.value() : ""));
                        } else {
                            Logger::Instance().Error("保存诊断报告失败，无法写入目标文件。");
                        }
                    }
                }
            } else {
                ImGui::TextColored(theme::StatusRed, "预检错误: %s", g_Preflight.errorMessage.c_str());
            }
        }
    } else {
        // Shellcode Mode
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::Accent, "Shellcode / 原始机器码载荷");
        ImGui::SameLine();
        RenderStatusBadge("Raw Bytes", "accent");

        // Optional .bin file picker
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 100.0f);
        if (ImGui::InputTextWithHint("##BinPath", "载入 .bin 文件路径 (可选)...", g_BinPathBuf, sizeof(g_BinPathBuf))) {
            std::wstring binPath = Utf8ToWide(TrimPathString(g_BinPathBuf));
            if (!binPath.empty() && GetFileAttributesW(binPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
                g_ParsedHexBytes = utils::ReadFileToBuffer(binPath);
                Logger::Instance().Info("已从二进制文件读取 " + std::to_string(g_ParsedHexBytes.size()) + " 字节。");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("浏览 .bin...", ImVec2(90, 0))) {
            std::wstring file = OpenFileDialog(L"Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0", L"选择载荷文件");
            if (!file.empty()) {
                auto u8 = dllinjector::ToUtf8(file);
                if (u8) std::snprintf(g_BinPathBuf, sizeof(g_BinPathBuf), "%s", u8.value().c_str());
                g_ParsedHexBytes = utils::ReadFileToBuffer(file);
                Logger::Instance().Info("已载入 " + std::to_string(g_ParsedHexBytes.size()) + " 字节 Shellcode。");
            }
        }

        ImGui::Spacing();
        ImGui::TextColored(theme::TextMuted, "或在此输入十六进制 Hex (如: 90 90 CC 48 31 C0):");
        if (ImGui::InputTextMultiline("##HexInput", g_HexInputBuf, sizeof(g_HexInputBuf), ImVec2(-1, 80))) {
            g_ParsedHexBytes = ParseHexInput(g_HexInputBuf);
        }

        std::string hexStatus = "已解析载荷: " + std::to_string(g_ParsedHexBytes.size()) + " 字节";
        RenderStatusBadge(hexStatus.c_str(), g_ParsedHexBytes.empty() ? "muted" : "green");
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void RenderMethodsGrid() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface1);
    ImGui::BeginChild("MethodsCard", ImVec2(0, 230), true);

    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Accent, "注入方法选择 (INJECTION METHODS)");
    ImGui::SameLine();
    RenderStatusBadge("6 种核心机制", "accent");

    ImGui::Columns(2, "MethodGridColumns", false);

    bool isDllMode = (g_CurrentPayloadMode == PayloadMode::Dll);

    for (int i = 0; i < 6; ++i) {
        const auto& card = g_MethodCards[i];
        bool isSupported = isDllMode ? card.supportsDll : card.supportsShellcode;
        bool isSelected = (g_SelectedMethod == card.id);

        ImGui::PushID(i);
        if (!isSupported) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(theme::Surface0.x, theme::Surface0.y, theme::Surface0.z, 0.40f));
            ImGui::PushStyleColor(ImGuiCol_Border, theme::BorderSubtle);
        } else if (isSelected) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_Border, theme::Accent);
        } else {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface2);
            ImGui::PushStyleColor(ImGuiCol_Border, theme::BorderSubtle);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 3.0f));
        ImGui::BeginChild("MethodSubCard", ImVec2(0, 54), true, ImGuiWindowFlags_NoScrollbar);
        
        // Card Title & Badge
        ImGui::TextColored(isSelected ? theme::Accent : (isSupported ? theme::TextPrimary : theme::TextDisabled),
                           "%s", card.title);
        ImGui::SameLine();
        if (!isSupported) {
            RenderStatusBadge("仅 DLL", "red");
        } else {
            RenderStatusBadge(card.tag, card.tagStyle);
        }

        // Subtitle / description
        ImGui::TextColored(isSupported ? theme::TextMuted : theme::TextDisabled, "%s", card.description);

        bool cardHovered = isSupported && ImGui::IsWindowHovered();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        // Click detection
        if (isSupported && (ImGui::IsItemClicked() || (cardHovered && ImGui::IsMouseClicked(0)))) {
            g_SelectedMethod = card.id;
            Logger::Instance().Info(std::string("已切换注入技术: ") + card.title);
        }

        ImGui::PopID();

        ImGui::NextColumn();
    }

    ImGui::Columns(1);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static bool ExecuteInjectionInternal(bool force, bool showModal, std::string& outToast, std::string& outTone) {
    auto proc = GetSelectedProcessItem();
    if (!proc) {
        Logger::Instance().Error("注入停止: 请先在左侧选择目标进程！");
        outToast = "请先选择一个目标进程";
        outTone = "err";
        if (showModal) MessageBoxW(NULL, L"请先在左侧列表中选择一个目标进程！", L"未选定目标", MB_ICONWARNING);
        return false;
    }

    // Architecture Check
    #if defined(_WIN64)
    bool amI64 = true;
    #else
    bool amI64 = false;
    #endif

    bool targetIs64 = (proc->arch == utils::ProcessArchitecture::X64);
    if (proc->arch == utils::ProcessArchitecture::Unknown) {
        Logger::Instance().Error("注入停止: 目标进程架构未知或权限不足。");
        outToast = "目标进程架构未知或权限受限，可先尝试启用 SeDebug 提权";
        outTone = "err";
        if (showModal) MessageBoxW(NULL, L"无法确认目标进程架构，已停止操作。", L"架构未知", MB_ICONERROR);
        return false;
    }

    if (amI64 != targetIs64) {
        std::string errMsg = "架构冲突: 注入器为 " + std::string(amI64 ? "64位" : "32位") +
            "，目标进程为 " + std::string(targetIs64 ? "64位" : "32位") + "。";
        Logger::Instance().Error(errMsg);
        outToast = errMsg;
        outTone = "err";
        if (showModal) MessageBoxW(NULL, L"架构不匹配！注入器与目标进程位数必须一致。", L"架构错误", MB_ICONERROR);
        return false;
    }

    // Syscall experimental guard
    #if !DLLINJECTOR_HAS_SYSCALL
    if (g_SelectedMethod == MethodID::SYSCALL) {
        Logger::Instance().Warn("Direct Syscall 源码已保留，但未纳入当前验证构建的测试范围。");
        outToast = "Direct Syscall 在当前构建中作为实验性源码保留";
        outTone = "warn";
        if (showModal) MessageBoxW(NULL, L"Direct Syscall 源码已保留，但未纳入本构建的验证范围。", L"功能不可用", MB_ICONINFORMATION);
        return false;
    }
    #endif

    DWORD pid = proc->pid;
    bool success = false;

    if (g_CurrentPayloadMode == PayloadMode::Dll) {
        if (g_CurrentDllPath.empty() || GetFileAttributesW(g_CurrentDllPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
            Logger::Instance().Error("注入停止: DLL 文件路径无效或文件不存在。");
            outToast = "请选择有效的目标 .dll 文件";
            outTone = "err";
            if (showModal) MessageBoxW(NULL, L"请选择有效的目标 .dll 文件！", L"文件错误", MB_ICONERROR);
            return false;
        }

        // Preflight blocking check
        if (g_Preflight.valid && g_Preflight.hasBlockingErrors) {
            if (!force) {
                Logger::Instance().Warn("注入被安全拦截: Core Doctor 检测到阻断性严重错误，请确认后强制执行。");
                outToast = "注入被安全拦截：存在阻断性错误，请确认后强制执行";
                outTone = "err";
                if (showModal) MessageBoxW(NULL, L"检测到阻断性错误 (如架构不匹配)，注入已拦截！", L"预检拦截", MB_ICONWARNING);
                return false;
            }
            Logger::Instance().Warn("Core Doctor 提示存在阻止性诊断项，已由用户授权强制执行。");
        }

        Logger::Instance().Info("开始对目标 PID " + std::to_string(pid) + " 执行注入...");

        switch (g_SelectedMethod) {
            case MethodID::CRT:
                Logger::Instance().Info("调用 CreateRemoteThread DLL 注入器...");
                success = methods::Inject_CRT_DLL(pid, g_CurrentDllPath);
                break;
            case MethodID::APC:
                Logger::Instance().Info("调用 QueueUserAPC DLL 注入器...");
                success = methods::Inject_APC_DLL(pid, g_CurrentDllPath);
                break;
            case MethodID::HOOK:
                Logger::Instance().Info("调用 SetWindowsHookEx DLL 注入器...");
                success = methods::Inject_Hook_DLL(pid, g_CurrentDllPath);
                break;
            case MethodID::REFLECTIVE: {
                Logger::Instance().Info("读取 DLL 内存并调用 Reflective 注入器...");
                auto raw = utils::ReadFileToBuffer(g_CurrentDllPath);
                if (raw.empty()) {
                    Logger::Instance().Error("读取 DLL 载荷数据失败！");
                    break;
                }
                success = methods::Inject_Reflective(pid, raw);
                break;
            }
            case MethodID::HIJACK: {
                Logger::Instance().Info("定位目标 EXE 路径并部署劫持 DLL...");
                std::wstring exePath = GetProcessPath(pid);
                if (exePath.empty()) {
                    Logger::Instance().Error("获取目标进程主模块路径失败 (可能权限不足)。");
                    break;
                }
                success = methods::Deploy_Hijack(exePath, g_CurrentDllPath);
                break;
            }
            #if DLLINJECTOR_HAS_SYSCALL
            case MethodID::SYSCALL:
                Logger::Instance().Info("调用 Direct Syscall DLL 注入器...");
                success = methods::Inject_Syscall_DLL(pid, g_CurrentDllPath);
                break;
            #endif
            default:
                break;
        }
    } else {
        // Shellcode Mode
        if (g_ParsedHexBytes.empty()) {
            Logger::Instance().Error("注入停止: Shellcode 载荷数据为空！请载入 .bin 文件或输入合法十六进制。");
            outToast = "Shellcode 载荷数据为空，请检查十六进制输入或 .bin 文件";
            outTone = "err";
            if (showModal) MessageBoxW(NULL, L"载荷数据为空！请检查 Hex 格式或 .bin 文件。", L"载荷错误", MB_ICONERROR);
            return false;
        }

        Logger::Instance().Info("开始对目标 PID " + std::to_string(pid) + " 执行 Shellcode 注入 (" +
                               std::to_string(g_ParsedHexBytes.size()) + " 字节)...");

        switch (g_SelectedMethod) {
            case MethodID::CRT:
                Logger::Instance().Info("调用 CreateRemoteThread Shellcode 注入器...");
                success = methods::Inject_CRT_Shellcode(pid, g_ParsedHexBytes);
                break;
            case MethodID::APC:
                Logger::Instance().Info("调用 QueueUserAPC Shellcode 注入器...");
                success = methods::Inject_APC_Shellcode(pid, g_ParsedHexBytes);
                break;
            #if DLLINJECTOR_HAS_SYSCALL
            case MethodID::SYSCALL:
                Logger::Instance().Info("调用 Direct Syscall Shellcode 注入器...");
                success = methods::Inject_Syscall_Shellcode(pid, g_ParsedHexBytes);
                break;
            #endif
            default:
                Logger::Instance().Error("选中的技术不支持 Shellcode 模式！");
                break;
        }
    }

    if (success) {
        Logger::Instance().Success("[成功] 注入请求已提交！(底层 API 成功返回，预期效果视目标保护机制而定)");
        outToast = "已向 PID " + std::to_string(pid) + " (" + proc->name_utf8 + ") 提交注入请求";
        outTone = "ok";
        if (showModal) MessageBoxW(NULL, L"请求已提交，底层 API 执行成功！", L"注入成功", MB_ICONINFORMATION);
    } else {
        DWORD err = GetLastError();
        Logger::Instance().Error("[失败] 注入执行失败！GetLastError: " + std::to_string(err));
        outToast = "注入执行失败 (Win32 Error " + std::to_string(err) + ")，详情见终端日志";
        outTone = "err";
        if (showModal) MessageBoxW(NULL, L"注入失败，详情请查看终端控制台日志。", L"执行失败", MB_ICONERROR);
    }
    return success;
}

static void ExecuteInjection() {
    std::string toast, tone;
    ExecuteInjectionInternal(true, true, toast, tone);
}

static void RenderActionBar() {
    auto proc = GetSelectedProcessItem();

    ImGui::PushStyleColor(ImGuiCol_Button, theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::AccentActive);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));

    std::string btnText = "开始执行注入 (EXECUTE INJECTION)";
    if (proc) {
        btnText += " -> [ PID: " + std::to_string(proc->pid) + " " + proc->name_utf8 + " ]";
    }

    if (ImGui::Button(btnText.c_str(), ImVec2(-1, 42))) {
        ExecuteInjection();
    }

    ImGui::PopStyleColor(4);
}

void RenderGuiFrame() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);

    ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration |
                                ImGuiWindowFlags_NoMove |
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::Begin("RootAppWindow", nullptr, rootFlags)) {
        // 1. Top Header
        RenderHeaderBar();

        ImGui::Spacing();

        // 2. Middle Body: Two Column Split
        float consoleHeight = 175.0f;
        float middleHeight = io.DisplaySize.y - 52.0f - consoleHeight - 38.0f;
        if (middleHeight < 310.0f) middleHeight = 310.0f;

        float leftPaneWidth = 380.0f;

        ImGui::BeginChild("MiddleSection", ImVec2(0, middleHeight), false);

        // Left Column: Process List
        RenderProcessListPane(leftPaneWidth);

        ImGui::SameLine();

        // Right Column: Payload + Methods + Inject Button
        ImGui::BeginChild("RightSection", ImVec2(0, 0), false);
        
        RenderPayloadPane();
        ImGui::Spacing();
        RenderMethodsGrid();
        ImGui::Spacing();
        RenderActionBar();

        ImGui::EndChild(); // RightSection

        ImGui::EndChild(); // MiddleSection

        ImGui::Spacing();

        // 3. Bottom Terminal Console
        Logger::Instance().RenderConsole("ConsolePanel", consoleHeight, g_MonoFont);
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void PushWebViewState(const std::string& toastMsg, const std::string& toastTone) {
    if (!webview_host::IsActive()) return;

    auto selProc = GetSelectedProcessItem();
    std::string doctorJson = g_Preflight.valid
        ? PreflightDoctor::GenerateJsonReport(g_Preflight, selProc ? selProc->pid : 0)
        : "";

    std::ostringstream oss;
    oss << "{\"type\":\"state\",\"state\":{";
    oss << "\"isMaximized\":" << (g_MainHwnd && IsZoomed(g_MainHwnd) ? "true" : "false") << ",";
#if defined(_WIN64)
    oss << "\"hostArch\":\"x64\",";
#else
    oss << "\"hostArch\":\"x86\",";
#endif
    oss << "\"debugPriv\":" << (g_DebugPrivilegeActive ? "true" : "false") << ",";
    oss << "\"hasSyscall\":" << (DLLINJECTOR_HAS_SYSCALL ? "true" : "false") << ",";
    oss << "\"payloadMode\":\"" << (g_CurrentPayloadMode == PayloadMode::Dll ? "dll" : "shellcode") << "\",";
    oss << "\"selectedMethod\":" << static_cast<int>(g_SelectedMethod) << ",";
    oss << "\"selectedPid\":" << g_SelectedPid << ",";
    oss << "\"dllPath\":\"" << JsonEscapeStr(g_DllPathBuf) << "\",";
    oss << "\"binPath\":\"" << JsonEscapeStr(g_BinPathBuf) << "\",";
    oss << "\"shellcodeSize\":" << g_ParsedHexBytes.size() << ",";

    // Preflight object
    std::string matchStr = "WAIT";
    if (g_Preflight.matchStatus == ArchMatchStatus::Match) matchStr = "MATCH";
    else if (g_Preflight.matchStatus == ArchMatchStatus::Mismatch) matchStr = "MISMATCH";

    oss << "\"preflight\":{";
    oss << "\"valid\":" << (g_Preflight.valid ? "true" : "false") << ",";
    oss << "\"dllArch\":\"" << JsonEscapeStr(g_Preflight.dllArchStr) << "\",";
    oss << "\"pe32Plus\":" << (g_Preflight.isPe32Plus ? "true" : "false") << ",";
    oss << "\"importCount\":" << g_Preflight.importCount << ",";
    oss << "\"delayCount\":" << g_Preflight.delayImportCount << ",";
    oss << "\"matchStatus\":\"" << matchStr << "\",";
    oss << "\"blocking\":" << (g_Preflight.hasBlockingErrors ? "true" : "false") << ",";
    oss << "\"error\":\"" << JsonEscapeStr(g_Preflight.errorMessage) << "\",";
    oss << "\"json\":\"" << JsonEscapeStr(doctorJson) << "\",";

    oss << "\"findings\":[";
    for (size_t i = 0; i < g_Preflight.findings.size(); ++i) {
        const auto& f = g_Preflight.findings[i];
        if (i > 0) oss << ",";
        oss << "{\"rule_id\":\"" << JsonEscapeStr(f.rule_id) << "\","
            << "\"severity\":\"" << JsonEscapeStr(std::string(dllinjector::SeverityName(f.severity))) << "\","
            << "\"confidence\":\"" << JsonEscapeStr(f.confidence) << "\","
            << "\"message\":\"" << JsonEscapeStr(f.message) << "\","
            << "\"next_step\":\"" << JsonEscapeStr(f.recommended_next_step) << "\"}";
    }
    oss << "],";

    oss << "\"dependencies\":[";
    for (size_t i = 0; i < g_Preflight.dependencies.size(); ++i) {
        const auto& d = g_Preflight.dependencies[i];
        if (i > 0) oss << ",";
        oss << "{\"name\":\"" << JsonEscapeStr(d.name) << "\","
            << "\"delay\":" << (d.delay_import ? "true" : "false") << ","
            << "\"status\":\"" << JsonEscapeStr(std::string(dllinjector::DependencyStatusName(d.status))) << "\"}";
    }
    oss << "]},";

    // Processes array
    oss << "\"processes\":[";
    for (size_t i = 0; i < g_Processes.size(); ++i) {
        const auto& p = g_Processes[i];
        if (i > 0) oss << ",";
        oss << "{\"pid\":" << p.pid << ","
            << "\"name\":\"" << JsonEscapeStr(p.name_utf8) << "\","
            << "\"arch\":\"" << JsonEscapeStr(p.archStr) << "\"}";
    }
    oss << "],";

    // Logs array
    auto logs = Logger::Instance().GetEntriesSnapshot();
    oss << "\"logs\":[";
    for (size_t i = 0; i < logs.size(); ++i) {
        const auto& l = logs[i];
        const char* lvl = "INFO";
        switch (l.level) {
            case LogLevel::Info:    lvl = "INFO"; break;
            case LogLevel::Success: lvl = "SUCCESS"; break;
            case LogLevel::Warn:    lvl = "WARN"; break;
            case LogLevel::Error:   lvl = "ERROR"; break;
            case LogLevel::Doctor:  lvl = "DOCTOR"; break;
        }
        if (i > 0) oss << ",";
        oss << "{\"time\":\"" << JsonEscapeStr(l.timestamp) << "\","
            << "\"level\":\"" << lvl << "\","
            << "\"msg\":\"" << JsonEscapeStr(l.message) << "\"}";
    }
    oss << "]}";

    if (!toastMsg.empty()) {
        oss << ",\"toast\":{\"msg\":\"" << JsonEscapeStr(toastMsg) << "\","
            << "\"tone\":\"" << JsonEscapeStr(toastTone) << "\"}";
    }
    oss << "}";

    webview_host::PostJsonMessage(oss.str());
}

void HandleWebViewMessage(const std::string& jsonMessage) {
    const auto message = ParseWebMessage(jsonMessage);
    if (!message) {
        Logger::Instance().Warn("已忽略格式无效的界面消息");
        return;
    }
    const auto& cmd = message->command;

    if (cmd == "init") {
        PushWebViewState();
    } else if (cmd == "refresh_processes") {
        RefreshProcesses();
        PushWebViewState("已刷新活跃进程列表 (" + std::to_string(g_Processes.size()) + " 个)", "ok");
    } else if (cmd == "select_process") {
        int pid = message->pid;
        g_SelectedPid = pid;
        auto sel = GetSelectedProcessItem();
        if (sel) {
            Logger::Instance().Info("已选定目标进程: " + sel->name_utf8 +
                " (PID: " + std::to_string(sel->pid) + ", 架构: " + sel->archStr + ")");
        }
        TriggerPreflight();
        PushWebViewState();
    } else if (cmd == "set_payload_mode") {
        std::string mode = message->mode;
        if (mode == "shellcode") {
            g_CurrentPayloadMode = PayloadMode::Shellcode;
            if (g_SelectedMethod == MethodID::HOOK ||
                g_SelectedMethod == MethodID::REFLECTIVE ||
                g_SelectedMethod == MethodID::HIJACK) {
                g_SelectedMethod = MethodID::CRT;
            }
            PushWebViewState("已切换到 Shellcode 载荷模式", "ok");
        } else {
            g_CurrentPayloadMode = PayloadMode::Dll;
            TriggerPreflight();
            PushWebViewState("已切换到 DLL 动态库注入模式", "ok");
        }
    } else if (cmd == "select_method") {
        int m = message->method;
        if (m >= 0 && m <= 5) {
            g_SelectedMethod = static_cast<MethodID>(m);
            Logger::Instance().Info(std::string("已切换注入技术: ") + g_MethodCards[m].title);
            PushWebViewState(std::string("已切换到 ") + g_MethodCards[m].title, "ok");
        }
    } else if (cmd == "browse_dll") {
        std::wstring file = OpenFileDialog(L"Dynamic Link Library (*.dll)\0*.dll\0All Files (*.*)\0*.*\0", L"选择注入 DLL");
        if (!file.empty()) {
            g_CurrentDllPath = file;
            auto u8 = dllinjector::ToUtf8(file);
            if (u8) std::snprintf(g_DllPathBuf, sizeof(g_DllPathBuf), "%s", u8.value().c_str());
            TriggerPreflight();
            PushWebViewState("已加载 DLL 载荷并完成静态预检", "ok");
        }
    } else if (cmd == "set_dll_path") {
        std::string clean = TrimPathString(message->path);
        std::snprintf(g_DllPathBuf, sizeof(g_DllPathBuf), "%s", clean.c_str());
        g_CurrentDllPath = Utf8ToWide(clean);
        TriggerPreflight();
        PushWebViewState();
    } else if (cmd == "browse_bin") {
        std::wstring file = OpenFileDialog(L"Binary Files (*.bin)\0*.bin\0All Files (*.*)\0*.*\0", L"选择载荷文件");
        if (!file.empty()) {
            auto u8 = dllinjector::ToUtf8(file);
            if (u8) std::snprintf(g_BinPathBuf, sizeof(g_BinPathBuf), "%s", u8.value().c_str());
            g_ParsedHexBytes = utils::ReadFileToBuffer(file);
            Logger::Instance().Info("已载入 " + std::to_string(g_ParsedHexBytes.size()) + " 字节 Shellcode。");
            PushWebViewState("已载入 " + std::to_string(g_ParsedHexBytes.size()) + " 字节二进制载荷", "ok");
        }
    } else if (cmd == "set_hex") {
        std::string hex = message->hex;
        std::snprintf(g_HexInputBuf, sizeof(g_HexInputBuf), "%s", hex.c_str());
        g_ParsedHexBytes = ParseHexInput(hex);
        PushWebViewState();
    } else if (cmd == "enable_debug_priv") {
        if (utils::EnableDebugPrivilege()) {
            g_DebugPrivilegeActive = true;
            Logger::Instance().Success("已成功提升进程令牌权限 (SeDebugPrivilege)。");
            RefreshProcesses();
            PushWebViewState("已启用 SeDebugPrivilege 调试权限", "ok");
        } else {
            Logger::Instance().Error("提升 SeDebugPrivilege 权限失败 (需以管理员身份运行)。");
            PushWebViewState("提权失败：请以管理员身份运行程序", "err");
        }
    } else if (cmd == "inject") {
        if (g_IsInjecting.exchange(true)) {
            PushWebViewState("已有注入任务正在执行中，请稍候...", "warn");
            return;
        }
        bool force = message->force;
        std::string toast, tone;
        ExecuteInjectionInternal(force, false, toast, tone);
        g_IsInjecting = false;
        PushWebViewState(toast, tone);
    } else if (cmd == "copy_doctor_json") {
        auto proc = GetSelectedProcessItem();
        std::string json = PreflightDoctor::GenerateJsonReport(g_Preflight, proc ? proc->pid : 0);
        CopyUtf8ToClipboard(g_MainHwnd, json);
        Logger::Instance().Success("已复制 Core Doctor 诊断 JSON 到系统剪贴板。");
        PushWebViewState("已复制 Core Doctor 诊断 JSON", "ok");
    } else if (cmd == "export_doctor_report") {
        std::wstring savePath = SaveFileDialog(L"JSON Report (*.json)\0*.json\0Text Report (*.txt)\0*.txt\0", L"json", L"保存诊断报告");
        if (!savePath.empty()) {
            auto proc = GetSelectedProcessItem();
            std::string content = (savePath.find(L".json") != std::wstring::npos) ?
                PreflightDoctor::GenerateJsonReport(g_Preflight, proc ? proc->pid : 0) :
                PreflightDoctor::GenerateTextReport(g_Preflight, proc ? proc->pid : 0);
            std::ofstream ofs(savePath.c_str(), std::ios::binary);
            if (ofs) {
                ofs.write(content.data(), content.size());
                Logger::Instance().Success("已成功导出诊断报告。");
                PushWebViewState("已成功导出诊断报告文件", "ok");
            } else {
                Logger::Instance().Error("保存诊断报告失败，无法写入目标文件。");
                PushWebViewState("导出报告失败：无法写入文件", "err");
            }
        }
    } else if (cmd == "clear_logs") {
        Logger::Instance().Clear();
        PushWebViewState("已清空终端日志", "ok");
    } else if (cmd == "copy_logs") {
        CopyUtf8ToClipboard(g_MainHwnd, Logger::Instance().GetAllAsText());
        PushWebViewState("已复制全部日志到剪贴板", "ok");
    } else if (cmd == "set_theme") {
        bool dark = message->dark;
        if (g_MainHwnd) {
            BOOL darkAttr = dark ? TRUE : FALSE;
            DwmSetWindowAttribute(g_MainHwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkAttr, sizeof(darkAttr));
            DwmSetWindowAttribute(g_MainHwnd, 19, &darkAttr, sizeof(darkAttr));
        }
    } else if (cmd == "window_min") {
        if (g_MainHwnd) ShowWindow(g_MainHwnd, SW_MINIMIZE);
    } else if (cmd == "window_max_toggle") {
        if (g_MainHwnd) {
            if (IsZoomed(g_MainHwnd)) {
                ShowWindow(g_MainHwnd, SW_RESTORE);
            } else {
                ShowWindow(g_MainHwnd, SW_MAXIMIZE);
            }
            PushWebViewState();
        }
    } else if (cmd == "window_close") {
        if (g_MainHwnd) PostMessageW(g_MainHwnd, WM_CLOSE, 0, 0);
    } else if (cmd == "window_drag") {
        if (g_MainHwnd) {
            ReleaseCapture();
            SendMessageW(g_MainHwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        }
    }
}

} // namespace gui
