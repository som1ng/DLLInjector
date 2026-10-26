#include "gui_theme.h"
#include "imgui_internal.h"
#include <string_view>

namespace gui {

namespace theme {
    ImVec4 Surface0;
    ImVec4 Surface1;
    ImVec4 Surface2;
    ImVec4 SurfaceHover;
    ImVec4 SurfaceActive;
    ImVec4 BorderSubtle;
    ImVec4 BorderStrong;

    ImVec4 Accent;
    ImVec4 AccentHover;
    ImVec4 AccentActive;

    ImVec4 TextPrimary;
    ImVec4 TextSecondary;
    ImVec4 TextMuted;
    ImVec4 TextDisabled;

    ImVec4 StatusSuccess;
    ImVec4 StatusWarning;
    ImVec4 StatusDanger;
    ImVec4 StatusSpecial;
}

static ThemeMode g_ActiveTheme = ThemeMode::Dark;
static AccentPreset g_ActiveAccent = AccentPreset::Violet;

ThemeMode GetCurrentThemeMode() {
    return g_ActiveTheme;
}

AccentPreset GetCurrentAccent() {
    return g_ActiveAccent;
}

void ApplyTheme(ThemeMode mode, AccentPreset accent) {
    g_ActiveTheme = mode;
    g_ActiveAccent = accent;

    const bool isDark = (mode == ThemeMode::Dark);

    // 1. Accent colors (Arc Presets from docs.tkcode.si - Strictly NO BLUE)
    switch (accent) {
        case AccentPreset::Violet:
            // Seed #7747FF
            theme::Accent       = isDark ? ImVec4(0.655f, 0.545f, 1.000f, 1.0f) : ImVec4(0.467f, 0.278f, 1.000f, 1.0f); // #A78BFF / #7747FF
            theme::AccentHover  = isDark ? ImVec4(0.740f, 0.640f, 1.000f, 1.0f) : ImVec4(0.400f, 0.220f, 0.900f, 1.0f);
            theme::AccentActive = isDark ? ImVec4(0.580f, 0.450f, 0.950f, 1.0f) : ImVec4(0.333f, 0.157f, 0.808f, 1.0f); // #5528CE
            break;
        case AccentPreset::Green:
            // Seed #0DB879
            theme::Accent       = isDark ? ImVec4(0.325f, 0.863f, 0.651f, 1.0f) : ImVec4(0.051f, 0.722f, 0.475f, 1.0f); // #53DCA6 / #0DB879
            theme::AccentHover  = isDark ? ImVec4(0.420f, 0.920f, 0.720f, 1.0f) : ImVec4(0.040f, 0.630f, 0.410f, 1.0f);
            theme::AccentActive = isDark ? ImVec4(0.240f, 0.780f, 0.560f, 1.0f) : ImVec4(0.031f, 0.486f, 0.329f, 1.0f); // #087C54
            break;
        case AccentPreset::Amber:
            // CC Switch v7 Action Orange (#F97316 light / #EA580C dark)
            theme::Accent       = isDark ? ImVec4(0.918f, 0.345f, 0.047f, 1.0f) : ImVec4(0.976f, 0.451f, 0.086f, 1.0f);
            theme::AccentHover  = isDark ? ImVec4(0.941f, 0.427f, 0.122f, 1.0f) : ImVec4(0.918f, 0.384f, 0.039f, 1.0f);
            theme::AccentActive = isDark ? ImVec4(0.761f, 0.255f, 0.047f, 1.0f) : ImVec4(0.761f, 0.255f, 0.047f, 1.0f);
            break;
        case AccentPreset::Neutral:
        default:
            // Seed #353535
            theme::Accent       = isDark ? ImVec4(0.745f, 0.745f, 0.745f, 1.0f) : ImVec4(0.208f, 0.208f, 0.208f, 1.0f); // #BEBEBE / #353535
            theme::AccentHover  = isDark ? ImVec4(0.850f, 0.850f, 0.850f, 1.0f) : ImVec4(0.120f, 0.120f, 0.120f, 1.0f);
            theme::AccentActive = isDark ? ImVec4(0.650f, 0.650f, 0.650f, 1.0f) : ImVec4(0.122f, 0.122f, 0.122f, 1.0f); // #1F1F1F
            break;
    }

    // 2. Status colors (Arc Tokens from docs.tkcode.si)
    theme::StatusSuccess = isDark ? ImVec4(0.325f, 0.863f, 0.651f, 1.0f) : ImVec4(0.051f, 0.722f, 0.475f, 1.0f); // #53DCA6 / #0DB879
    theme::StatusWarning = isDark ? ImVec4(1.000f, 0.796f, 0.384f, 1.0f) : ImVec4(0.953f, 0.678f, 0.125f, 1.0f); // #FFCB62 / #F3AD20
    theme::StatusDanger  = isDark ? ImVec4(1.000f, 0.576f, 0.525f, 1.0f) : ImVec4(0.945f, 0.373f, 0.333f, 1.0f); // #FF9386 / #F15F55
    theme::StatusSpecial = isDark ? ImVec4(0.655f, 0.545f, 1.000f, 1.0f) : ImVec4(0.467f, 0.278f, 1.000f, 1.0f);

    // 3. Surface & Text Tokens (CC Switch v7 tokens)
    if (isDark) {
        // Dark Mode: #161618 sidebar, #1C1C1E app, #26262A subtle, #2E2E33 card
        theme::Surface0       = ImVec4(0.110f, 0.110f, 0.118f, 1.00f); // #1C1C1E - --bg-app
        theme::Surface1       = ImVec4(0.180f, 0.180f, 0.200f, 1.00f); // #2E2E33 - --bg-card
        theme::Surface2       = ImVec4(0.149f, 0.149f, 0.165f, 1.00f); // #26262A - --bg-subtle
        theme::SurfaceHover   = ImVec4(0.192f, 0.192f, 0.212f, 1.00f); // #313136 - --bg-selected
        theme::SurfaceActive  = ImVec4(0.243f, 0.243f, 0.271f, 1.00f); // #3E3E45
        theme::BorderSubtle   = ImVec4(0.243f, 0.243f, 0.271f, 1.00f); // #3E3E45 - --border
        theme::BorderStrong   = ImVec4(0.290f, 0.290f, 0.322f, 1.00f); // #4A4A52 - --border-strong

        theme::TextPrimary    = ImVec4(0.894f, 0.894f, 0.906f, 1.00f); // #E4E4E7 - --text-1
        theme::TextSecondary  = ImVec4(0.671f, 0.671f, 0.702f, 1.00f); // #ABABB3 - --text-2
        theme::TextMuted      = ImVec4(0.604f, 0.604f, 0.635f, 1.00f); // #9A9AA2 - --text-3
        theme::TextDisabled   = ImVec4(0.435f, 0.435f, 0.467f, 1.00f); // #6F6F77
    } else {
        // Light Mode: #FFFFFF app/card, #F5F5F7 sidebar, #EFEFF2 subtle
        theme::Surface0       = ImVec4(0.961f, 0.961f, 0.969f, 1.00f); // #F5F5F7 - --bg-sidebar
        theme::Surface1       = ImVec4(1.000f, 1.000f, 1.000f, 1.00f); // #FFFFFF - --bg-card
        theme::Surface2       = ImVec4(0.937f, 0.937f, 0.949f, 1.00f); // #EFEFF2 - --bg-subtle
        theme::SurfaceHover   = ImVec4(0.894f, 0.894f, 0.918f, 1.00f); // #E4E4EA - --bg-selected
        theme::SurfaceActive  = ImVec4(0.812f, 0.812f, 0.839f, 1.00f); // #CFCFD6
        theme::BorderSubtle   = ImVec4(0.894f, 0.894f, 0.914f, 1.00f); // #E4E4E9 - --border
        theme::BorderStrong   = ImVec4(0.812f, 0.812f, 0.839f, 1.00f); // #CFCFD6 - --border-strong

        theme::TextPrimary    = ImVec4(0.094f, 0.094f, 0.106f, 1.00f); // #18181B - --text-1
        theme::TextSecondary  = ImVec4(0.322f, 0.322f, 0.357f, 1.00f); // #52525B - --text-2
        theme::TextMuted      = ImVec4(0.396f, 0.396f, 0.431f, 1.00f); // #65656E - --text-3
        theme::TextDisabled   = ImVec4(0.533f, 0.533f, 0.565f, 1.00f); // #888890
    }

    // 4. ImGui Geometry & Style
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 5.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 4.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 5.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.PopupBorderSize   = 1.0f;
    style.FrameBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowPadding     = ImVec2(16.0f, 16.0f);
    style.FramePadding      = ImVec2(10.0f, 7.0f);
    style.ItemSpacing       = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    style.IndentSpacing     = 18.0f;
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 12.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]             = theme::Surface0;
    colors[ImGuiCol_ChildBg]              = theme::Surface1;
    colors[ImGuiCol_PopupBg]              = theme::Surface1;
    colors[ImGuiCol_Border]               = theme::BorderSubtle;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_Text]                 = theme::TextPrimary;
    colors[ImGuiCol_TextDisabled]         = theme::TextDisabled;

    colors[ImGuiCol_FrameBg]              = theme::Surface2;
    colors[ImGuiCol_FrameBgHovered]       = theme::SurfaceHover;
    colors[ImGuiCol_FrameBgActive]        = theme::SurfaceActive;

    colors[ImGuiCol_TitleBg]              = theme::Surface0;
    colors[ImGuiCol_TitleBgActive]        = theme::Surface1;
    colors[ImGuiCol_TitleBgCollapsed]     = theme::Surface0;
    colors[ImGuiCol_MenuBarBg]            = theme::Surface1;

    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab]        = theme::SurfaceHover;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme::SurfaceActive;
    colors[ImGuiCol_ScrollbarGrabActive]  = theme::Accent;

    colors[ImGuiCol_Button]               = theme::Surface2;
    colors[ImGuiCol_ButtonHovered]        = theme::SurfaceHover;
    colors[ImGuiCol_ButtonActive]         = theme::SurfaceActive;

    colors[ImGuiCol_Header]               = theme::Surface2;
    colors[ImGuiCol_HeaderHovered]        = theme::SurfaceHover;
    colors[ImGuiCol_HeaderActive]         = theme::SurfaceActive;

    colors[ImGuiCol_Separator]            = theme::BorderSubtle;
    colors[ImGuiCol_SeparatorHovered]     = theme::AccentHover;
    colors[ImGuiCol_SeparatorActive]      = theme::Accent;

    colors[ImGuiCol_Tab]                  = theme::Surface1;
    colors[ImGuiCol_TabHovered]           = theme::SurfaceHover;
    colors[ImGuiCol_TabActive]            = theme::Surface2;
    colors[ImGuiCol_TabUnfocused]         = theme::Surface1;
    colors[ImGuiCol_TabUnfocusedActive]   = theme::Surface2;

    colors[ImGuiCol_CheckMark]            = theme::Accent;
    colors[ImGuiCol_SliderGrab]           = theme::Accent;
    colors[ImGuiCol_SliderGrabActive]     = theme::AccentActive;

    colors[ImGuiCol_TableHeaderBg]        = theme::Surface2;
    colors[ImGuiCol_TableBorderStrong]    = theme::BorderSubtle;
    colors[ImGuiCol_TableBorderLight]     = theme::BorderSubtle;
    colors[ImGuiCol_TableRowBg]           = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TableRowBgAlt]        = isDark ? ImVec4(0.12f, 0.12f, 0.14f, 0.35f) : ImVec4(0.96f, 0.96f, 0.97f, 0.60f);

    colors[ImGuiCol_ModalWindowDimBg]     = isDark ? ImVec4(0.0f, 0.0f, 0.0f, 0.65f) : ImVec4(0.0f, 0.0f, 0.0f, 0.35f);
}

void ToggleTheme() {
    ThemeMode nextMode = (g_ActiveTheme == ThemeMode::Dark) ? ThemeMode::Light : ThemeMode::Dark;
    ApplyTheme(nextMode, g_ActiveAccent);
}

void SetAccent(AccentPreset accent) {
    ApplyTheme(g_ActiveTheme, accent);
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
    const bool isDark = (g_ActiveTheme == ThemeMode::Dark);
    const float bgAlpha = isDark ? 0.16f : 0.10f;
    const float borderAlpha = isDark ? 0.45f : 0.35f;

    if (s == "green" || s == "success") {
        RenderBadge(text, theme::StatusSuccess,
                    ImVec4(theme::StatusSuccess.x, theme::StatusSuccess.y, theme::StatusSuccess.z, bgAlpha),
                    ImVec4(theme::StatusSuccess.x, theme::StatusSuccess.y, theme::StatusSuccess.z, borderAlpha));
    } else if (s == "amber" || s == "warning") {
        RenderBadge(text, theme::StatusWarning,
                    ImVec4(theme::StatusWarning.x, theme::StatusWarning.y, theme::StatusWarning.z, bgAlpha),
                    ImVec4(theme::StatusWarning.x, theme::StatusWarning.y, theme::StatusWarning.z, borderAlpha));
    } else if (s == "red" || s == "danger") {
        RenderBadge(text, theme::StatusDanger,
                    ImVec4(theme::StatusDanger.x, theme::StatusDanger.y, theme::StatusDanger.z, bgAlpha),
                    ImVec4(theme::StatusDanger.x, theme::StatusDanger.y, theme::StatusDanger.z, borderAlpha));
    } else if (s == "accent" || s == "purple" || s == "violet") {
        RenderBadge(text, theme::Accent,
                    ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, bgAlpha),
                    ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, borderAlpha));
    } else {
        RenderBadge(text, theme::TextMuted,
                    isDark ? ImVec4(0.18f, 0.18f, 0.20f, 0.35f) : ImVec4(0.90f, 0.90f, 0.92f, 0.45f),
                    theme::BorderSubtle);
    }
}

bool RenderSegmentButton(const char* label, bool selected, ImVec2 size) {
    const bool isDark = (g_ActiveTheme == ThemeMode::Dark);
    if (selected) {
        ImVec4 selBg = isDark ? ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.25f)
                              : ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.12f);
        ImVec4 selHov = isDark ? ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.35f)
                               : ImVec4(theme::Accent.x, theme::Accent.y, theme::Accent.z, 0.18f);
        ImGui::PushStyleColor(ImGuiCol_Button, selBg);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, selHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, selBg);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::Accent);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::Accent);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, theme::Surface2);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::SurfaceHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::SurfaceActive);
        ImGui::PushStyleColor(ImGuiCol_Border, theme::BorderSubtle);
        ImGui::PushStyleColor(ImGuiCol_Text, theme::TextSecondary);
    }
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(5);
    return clicked;
}

} // namespace gui
