#pragma once
#include <string>
#include <vector>
#include <mutex>
#include "imgui.h"

namespace gui {

enum class LogLevel {
    Info,
    Success,
    Warn,
    Error,
    Doctor
};

struct LogEntry {
    std::string timestamp;
    LogLevel level;
    std::string message;
};

class Logger {
public:
    static Logger& Instance();

    void Log(LogLevel level, const std::string& message);
    void Info(const std::string& message)    { Log(LogLevel::Info, message); }
    void Success(const std::string& message) { Log(LogLevel::Success, message); }
    void Warn(const std::string& message)    { Log(LogLevel::Warn, message); }
    void Error(const std::string& message)   { Log(LogLevel::Error, message); }
    void Doctor(const std::string& message)  { Log(LogLevel::Doctor, message); }

    void Clear();
    std::string GetAllAsText() const;
    std::vector<LogEntry> GetEntriesSnapshot() const;

    void RenderConsole(const char* title, float height = 180.0f, ImFont* monoFont = nullptr);

private:
    Logger() = default;
    mutable std::mutex mutex_;
    std::vector<LogEntry> entries_;
    bool autoScroll_ = true;
    bool scrollToBottom_ = false;
    int filterLevel_ = 0; // 0: All, 1: Info+, 2: Success+, 3: Warn+, 4: Error only
    char searchFilter_[64] = "";
};

} // namespace gui
