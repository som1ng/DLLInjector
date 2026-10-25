#include "gui_theme.h"
#include "imgui_internal.h"
#include <string_view>

namespace gui {

void ApplyArcDarkGeekTheme() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding & Geometries (Micro-rounded modern Arc aesthetic)
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 5.0f;

    // Borders
    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    // Padding & Spacing
    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(10.0f, 7.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 12.0f;

    ImVec4* colors = style.Colors;

    // Canvas / Window
    colors[ImGuiCol_WindowBg]             = theme::Surface0;
    colors[ImGuiCol_ChildBg]              = theme::Surface1;
    colors[ImGuiCol_PopupBg]              = theme::Surface1;
    colors[ImGuiCol_Border]               = theme::BorderSubtle;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    // Text
    colors[ImGuiCol_Text]                 = theme::TextPrimary;
    colors[ImGuiCol_TextDisabled]         = theme::TextDark;

    // Frames (Inputs, checkboxes, radio, combo)
    colors[ImGuiCol_FrameBg]              = theme::Surface2;
    colors[ImGuiCol_FrameBgHovered]       = theme::SurfaceHover;
    colors[ImGuiCol_FrameBgActive]        = theme::SurfaceActive;

    // Titlebar
    colors[ImGuiCol_TitleBg]              = theme::Surface0;
    colors[ImGuiCol_TitleBgActive]        = theme::Surface1;
    colors[ImGuiCol_TitleBgCollapsed]     = theme::Surface0;
    colors[ImGuiCol_MenuBarBg]            = theme::Surface1;

    // Scrollbar
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]        = theme::SurfaceHover;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme::SurfaceActive;
    colors[ImGuiCol_ScrollbarGrabActive]  = theme::AccentBlue;

    // Buttons
    colors[ImGuiCol_Button]               = theme::Surface2;
    colors[ImGuiCol_ButtonHovered]        = theme::SurfaceHover;
    colors[ImGuiCol_ButtonActive]         = theme::SurfaceActive;

    // Header (CollapsingHeader, Selectables, TreeNodes)
    colors[ImGuiCol_Header]               = theme::Surface2;
    colors[ImGuiCol_HeaderHovered]        = theme::SurfaceHover;
    colors[ImGuiCol_HeaderActive]         = theme::SurfaceActive;

    // Separators
    colors[ImGuiCol_Separator]            = theme::BorderSubtle;
    colors[ImGuiCol_SeparatorHovered]     = theme::AccentBlue;
    colors[ImGuiCol_SeparatorActive]      = theme::AccentCyan;

    // Tabs
    colors[ImGuiCol_Tab]                  = theme::Surface1;
    colors[ImGuiCol_TabHovered]           = theme::SurfaceHover;
    colors[ImGuiCol_TabActive]            = theme::Surface2;
    colors[ImGuiCol_TabUnfocused]         = theme::Surface1;
    colors[ImGuiCol_TabUnfocusedActive]   = theme::Surface2;

    // Checkbox / Slider
    colors[ImGuiCol_CheckMark]            = theme::AccentCyan;
    colors[ImGuiCol_SliderGrab]           = theme::AccentBlue;
    colors[ImGuiCol_SliderGrabActive]     = theme::AccentCyan;

    // Tables
    colors[ImGuiCol_TableHeaderBg]        = theme::Surface1;
    colors[ImGuiCol_TableBorderStrong]    = theme::BorderSubtle;
    colors[ImGuiCol_TableBorderLight]     = ImVec4(0.18f, 0.21f, 0.28f, 0.40f);
    colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = ImVec4(0.09f, 0.11f, 0.15f, 0.35f);

    // Modal dim
    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.65f);
}

void RenderBadge(const char* text, ImVec4 textColor, ImVec4 bgColor, ImVec4 borderColor) {
    if (!text || text[0] == '\0') return;

    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pad = ImVec2(7.0f, 2.5f);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 rectMin = pos;
    ImVec2 rectMax = ImVec2(pos.x + textSize.x + pad.x * 2.0f, pos.y + textSize.y + pad.y * 2.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(bgColor), 4.0f);
    drawList->AddRect(rectMin, rectMax, ImGui::ColorConvertFloat4ToU32(borderColor), 4.0f, 0, 1.0f);
    drawList->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), ImGui::ColorConvertFloat4ToU32(textColor), text);

    ImGui::Dummy(ImVec2(textSize.x + pad.x * 2.0f, textSize.y + pad.y * 2.0f));
}

void RenderStatusBadge(const char* text, const char* style) {
    std::string_view s(style ? style : "muted");
    if (s == "green") {
        RenderBadge(text, theme::StatusGreen, ImVec4(0.063f, 0.725f, 0.506f, 0.15f), ImVec4(0.063f, 0.725f, 0.506f, 0.45f));
    } else if (s == "amber") {
        RenderBadge(text, theme::StatusAmber, ImVec4(0.961f, 0.620f, 0.043f, 0.15f), ImVec4(0.961f, 0.620f, 0.043f, 0.45f));
    } else if (s == "red") {
        RenderBadge(text, theme::StatusRed, ImVec4(0.937f, 0.267f, 0.267f, 0.15f), ImVec4(0.937f, 0.267f, 0.267f, 0.45f));
    } else if (s == "cyan") {
        RenderBadge(text, theme::AccentCyan, ImVec4(0.000f, 0.824f, 1.000f, 0.15f), ImVec4(0.000f, 0.824f, 1.000f, 0.45f));
    } else if (s == "purple") {
        RenderBadge(text, theme::StatusPurple, ImVec4(0.753f, 0.518f, 0.988f, 0.15f), ImVec4(0.753f, 0.518f, 0.988f, 0.45f));
    } else {
        RenderBadge(text, theme::TextMuted, ImVec4(0.18f, 0.21f, 0.28f, 0.35f), theme::BorderSubtle);
    }
}

bool RenderSegmentButton(const char* label, bool selected, ImVec2 size) {
    if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.055f, 0.450f, 0.650f, 0.35f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.055f, 0.500f, 0.720f, 0.45f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.055f, 0.400f, 0.600f, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_Border, theme::AccentCyan);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::AccentCyan);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::SurfaceHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::SurfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::BorderSubtle);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    }

    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return clicked;
}

} // namespace gui
