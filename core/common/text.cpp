#include "dllinjector/text.h"
#include "win32.h"
#include <limits>

namespace dllinjector {
Result<std::string> ToUtf8(std::wstring_view text) {
    if (text.empty()) return std::string{};
    if (text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        return Error{ErrorCategory::InvalidArgument, "utf8", "Text exceeds the supported length.", {}, {}};
    const auto length = static_cast<int>(text.size());
    const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                                           nullptr, 0, nullptr, nullptr);
    if (!needed) return detail::Win32Error(GetLastError(), "WideCharToMultiByte");
    std::string result(static_cast<std::size_t>(needed), '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), length,
                             result.data(), needed, nullptr, nullptr))
        return detail::Win32Error(GetLastError(), "WideCharToMultiByte");
    return result;
}
}
