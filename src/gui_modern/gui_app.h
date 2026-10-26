#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <optional>
#include "imgui.h"
#include "utils.h"
#include "preflight.h"

namespace gui {

extern ImFont* g_MainFont;
extern ImFont* g_MonoFont;

void InitGuiApp(HWND mainHwnd = nullptr);
void RenderGuiFrame();
void OnDropFile(const wchar_t* path);
void HandleWebViewMessage(const std::string& jsonMessage);
void PushWebViewState(const std::string& toastMsg = "", const std::string& toastTone = "ok");

} // namespace gui
