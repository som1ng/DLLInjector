#pragma once

#include <windows.h>
#include "dllinjector/result.h"

namespace dllinjector::detail {
class UniqueHandle {
public:
    explicit UniqueHandle(HANDLE handle, decltype(&::CloseHandle) close = ::CloseHandle)
        : handle_(handle), close_(close) {}
    ~UniqueHandle() { if (*this) close_(handle_); }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return handle_; }
private:
    HANDLE handle_;
    decltype(&::CloseHandle) close_;
};

inline Error Win32Error(DWORD code, const char* stage) {
    ErrorCategory category = ErrorCategory::System;
    std::string message = "Windows query failed; inspect the raw Win32 code.";
    if (code == ERROR_ACCESS_DENIED) {
        category = ErrorCategory::AccessDenied;
        message = "Access denied; this query does not request elevated privileges.";
    } else if (std::string(stage) == "OpenProcess" && code == ERROR_INVALID_PARAMETER) {
        category = ErrorCategory::NotFound;
        message = "The process is unavailable; refresh its PID and retry.";
    } else if (code == ERROR_NOT_SUPPORTED) {
        category = ErrorCategory::Unsupported;
        message = "This query is not supported for the reported architecture or system.";
    }
    return {category, stage, std::move(message), code, std::nullopt};
}
}
