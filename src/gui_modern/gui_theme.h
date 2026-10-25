#pragma once
#include "imgui.h"

namespace gui {

enum class ThemeMode {
    Dark,
    Light
};

enum class AccentPreset {
    Violet,  // Arc Violet (#7747FF / #A78BFF) - Default
    Green,   // Arc Green (#0DB879 / #53DCA6)
    Neutral, // Arc Neutral (#353535 / #BEBEBE)
    Amber    // Arc Amber (#F3AD20 / #FFCB62)
};

namespace theme {
    // Dynamically updated tokens according to Active ThemeMode & AccentPreset
    extern ImVec4 Surface0;       // Canvas background
    extern ImVec4 Surface1;       // Card / Panel background
    extern ImVec4 Surface2;       // Input / Well background
    extern ImVec4 SurfaceHover;   // Hover item
    extern ImVec4 SurfaceActive;  // Active item
    extern ImVec4 BorderSubtle;   // 1px subtle divider/border
    extern ImVec4 BorderStrong;   // Highlight/focus border

    extern ImVec4 Accent;         // Active Accent (Arc Violet / Green / etc. - NO BLUE)
    extern ImVec4 AccentHover;
    extern ImVec4 AccentActive;

    extern ImVec4 TextPrimary;    // Title and primary text
    extern ImVec4 TextSecondary;  // Description and labels
    extern ImVec4 TextMuted;      // Hints and captions
    extern ImVec4 TextDisabled;   // Inactive items

    extern ImVec4 StatusSuccess;  // Green
    extern ImVec4 StatusWarning;  // Amber
    extern ImVec4 StatusDanger;   // Coral / Red
    extern ImVec4 StatusSpecial;  // Violet / Special info

    // Compatibility aliases so existing code never displays blue/cyan
    #define AccentCyan  Accent
    #define AccentBlue  Accent
    #define StatusGreen StatusSuccess
    #define StatusAmber StatusWarning
    #define StatusRed   StatusDanger
    #define StatusPurple StatusSpecial
    #define TextDark    TextDisabled
}

ThemeMode GetCurrentThemeMode();
AccentPreset GetCurrentAccent();

void ApplyTheme(ThemeMode mode, AccentPreset accent = AccentPreset::Violet);
void ToggleTheme();
void SetAccent(AccentPreset accent);

// Custom UI badge & button helpers
void RenderBadge(const char* text, ImVec4 textColor, ImVec4 bgColor, ImVec4 borderColor);
void RenderStatusBadge(const char* text, const char* style); // "green", "amber", "red", "accent", "muted"
bool RenderSegmentButton(const char* label, bool selected, ImVec2 size = ImVec2(0, 0));

} // namespace gui
