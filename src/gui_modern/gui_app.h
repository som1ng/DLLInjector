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

void InitGuiApp();
void RenderGuiFrame();
void OnDropFile(const wchar_t* path);

} // namespace gui
