#pragma once
#include "imgui.h"

namespace gui {
    namespace theme {
        // Arc Modern Geek Dark Color Palette
        constexpr ImVec4 Surface0       = ImVec4(0.051f, 0.059f, 0.075f, 1.00f); // #0d0f13 - Deep background
        constexpr ImVec4 Surface1       = ImVec4(0.078f, 0.094f, 0.129f, 1.00f); // #141821 - Card / panel
        constexpr ImVec4 Surface2       = ImVec4(0.094f, 0.110f, 0.149f, 1.00f); // #181c26 - Input / secondary
        constexpr ImVec4 SurfaceHover   = ImVec4(0.125f, 0.149f, 0.200f, 1.00f); // #202633 - Hover item
        constexpr ImVec4 SurfaceActive  = ImVec4(0.157f, 0.188f, 0.251f, 1.00f); // #283040 - Active item
        constexpr ImVec4 BorderSubtle   = ImVec4(0.180f, 0.212f, 0.278f, 0.75f); // #2e3647 - 1px border
        constexpr ImVec4 BorderBright   = ImVec4(0.000f, 0.824f, 1.000f, 0.60f); // #00d2ff - Accent border

        constexpr ImVec4 AccentCyan     = ImVec4(0.000f, 0.824f, 1.000f, 1.00f); // #00d2ff - Primary Cyan
        constexpr ImVec4 AccentBlue     = ImVec4(0.055f, 0.647f, 0.914f, 1.00f); // #0ea5e9 - Sky blue
        constexpr ImVec4 AccentHover    = ImVec4(0.150f, 0.720f, 0.980f, 1.00f);
        constexpr ImVec4 AccentActive   = ImVec4(0.012f, 0.520f, 0.780f, 1.00f);

        constexpr ImVec4 TextPrimary    = ImVec4(0.945f, 0.961f, 0.976f, 1.00f); // #f1f5f9 - Bright text
        constexpr ImVec4 TextMuted      = ImVec4(0.580f, 0.639f, 0.722f, 1.00f); // #94a3b8 - Muted label
        constexpr ImVec4 TextDark       = ImVec4(0.392f, 0.455f, 0.545f, 1.00f); // #64748b - Subtle note

        constexpr ImVec4 StatusGreen    = ImVec4(0.063f, 0.725f, 0.506f, 1.00f); // #10b981 - Success
        constexpr ImVec4 StatusAmber    = ImVec4(0.961f, 0.620f, 0.043f, 1.00f); // #f59e0b - Warning
        constexpr ImVec4 StatusRed      = ImVec4(0.937f, 0.267f, 0.267f, 1.00f); // #ef4444 - Error
        constexpr ImVec4 StatusPurple   = ImVec4(0.753f, 0.518f, 0.988f, 1.00f); // #c084fc - Special / Info
    }

    void ApplyArcDarkGeekTheme();

    // Custom badge capsule helper
    void RenderBadge(const char* text, ImVec4 textColor, ImVec4 bgColor, ImVec4 borderColor);
    void RenderStatusBadge(const char* text, const char* style); // "green", "amber", "red", "cyan", "purple", "muted"
    
    // Custom styled button helper
    bool RenderSegmentButton(const char* label, bool selected, ImVec2 size = ImVec2(0, 0));
}
