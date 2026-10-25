#include "gui_logger.h"
#include "gui_theme.h"
#include <windows.h>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace gui {

namespace {
std::string GetCurrentTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    return std::string(buf);
}

const char* LevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return "INFO";
        case LogLevel::Success: return "SUCCESS";
        case LogLevel::Warn:    return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Doctor:  return "DOCTOR";
    }
    return "LOG";
}

ImVec4 LevelToColor(LogLevel level) {
    switch (level) {
        case LogLevel::Info:    return theme::Accent;
        case LogLevel::Success: return theme::StatusSuccess;
        case LogLevel::Warn:    return theme::StatusWarning;
        case LogLevel::Error:   return theme::StatusDanger;
        case LogLevel::Doctor:  return theme::StatusSpecial;
    }
    return theme::TextMuted;
}
} // namespace

Logger& Logger::Instance() {
    static Logger s_instance;
    return s_instance;
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    LogEntry entry;
    entry.timestamp = GetCurrentTimestamp();
    entry.level = level;
    entry.message = message;
    entries_.push_back(std::move(entry));
    scrollToBottom_ = true;
}

void Logger::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    entries_.clear();
    scrollToBottom_ = true;
}

std::string Logger::GetAllAsText() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    for (const auto& e : entries_) {
        oss << "[" << e.timestamp << "] [" << LevelToString(e.level) << "] " << e.message << "\r\n";
    }
    return oss.str();
}

void Logger::RenderConsole(const char* title, float height, ImFont* monoFont) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface1);
    ImGui::BeginChild(title, ImVec2(0, height), true, ImGuiWindowFlags_NoScrollbar);

    // Toolbar Header
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::Accent, ">_ ");
    ImGui::SameLine(0, 2);
    ImGui::TextColored(theme::TextPrimary, "终端日志控制台 (TERMINAL CONSOLE)");
    
    ImGui::SameLine();
    std::string countStr = std::to_string(entries_.size()) + " 条";
    RenderStatusBadge(countStr.c_str(), "accent");

    ImGui::SameLine(0, 10);
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputTextWithHint("##LogSearch", "🔍 搜索日志...", searchFilter_, sizeof(searchFilter_));

    // Right-aligned toolbar buttons
    float rightControlsWidth = 270.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    if (availWidth > rightControlsWidth) {
        ImGui::SameLine(ImGui::GetWindowWidth() - rightControlsWidth - 16.0f);
    } else {
        ImGui::SameLine();
    }

    if (ImGui::SmallButton("清空")) {
        Clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("复制全部")) {
        std::string all = GetAllAsText();
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    ImGui::Checkbox("自动滚动", &autoScroll_);

    ImGui::Separator();

    // Log lines container
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface0);
    ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    if (monoFont) ImGui::PushFont(monoFont);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string filterLower = searchFilter_;
        if (!filterLower.empty()) {
            std::transform(filterLower.begin(), filterLower.end(), filterLower.begin(),
                           [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
        }

        for (const auto& entry : entries_) {
            if (!filterLower.empty()) {
                std::string msgLower = entry.message;
                std::transform(msgLower.begin(), msgLower.end(), msgLower.begin(),
                               [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                if (msgLower.find(filterLower) == std::string::npos &&
                    entry.timestamp.find(filterLower) == std::string::npos) {
                    continue;
                }
            }

            // Timestamp
            ImGui::TextColored(theme::TextDark, "[%s]", entry.timestamp.c_str());
            ImGui::SameLine(0, 4);

            // Level Tag
            ImVec4 tagCol = LevelToColor(entry.level);
            ImGui::TextColored(tagCol, "[%-7s]", LevelToString(entry.level));
            ImGui::SameLine(0, 6);

            // Message
            if (entry.level == LogLevel::Error) {
                ImGui::TextColored(theme::StatusRed, "%s", entry.message.c_str());
            } else if (entry.level == LogLevel::Success) {
                ImGui::TextColored(theme::StatusGreen, "%s", entry.message.c_str());
            } else if (entry.level == LogLevel::Warn) {
                ImGui::TextColored(theme::StatusAmber, "%s", entry.message.c_str());
            } else {
                ImGui::TextColored(theme::TextPrimary, "%s", entry.message.c_str());
            }
        }
    }

    if (scrollToBottom_ || (autoScroll_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
        ImGui::SetScrollHereY(1.0f);
        scrollToBottom_ = false;
    }

    if (monoFont) ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleColor(); // Surface0

    ImGui::EndChild();
    ImGui::PopStyleColor(); // Surface1
}

} // namespace gui
