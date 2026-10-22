#pragma once
#include "result.h"
#include <string_view>

namespace dllinjector {
Result<std::string> ToUtf8(std::wstring_view text);
}
