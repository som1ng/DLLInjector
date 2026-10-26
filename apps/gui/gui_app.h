#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include "utils.h"
#include "preflight.h"

namespace gui {

void InitGuiApp(HWND mainHwnd = nullptr);
void OnDropFile(const wchar_t* path);
void HandleWebViewMessage(const std::string& jsonMessage);
void PushWebViewState(const std::string& toastMsg = "", const std::string& toastTone = "ok");

} // namespace gui

