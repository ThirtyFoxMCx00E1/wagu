#pragma once
#include "../../imgui/imgui.h"
#include "../../imgui/imgui_internal.h"
#include <stdio.h>
#include <stdint.h>

namespace UI
{
    // Scale a 1080p-designed pixel value to the current screen height.
    float S(float px);
    ImFont* Font();
    ImFont* PricedownFont();   // 3.00: pricedown.ttf found on the phone (else nullptr)
    const char* PricedownPath();

    // 2.19 TopbarPlus look: white "on" / selected state (black text), dark translucent panels
    static const ImU32 kAccent     = IM_COL32(255, 255, 255, 240);
    static const ImU32 kAccentDark = IM_COL32(236, 236, 236, 235);
    static const ImU32 kPill       = IM_COL32(  0,   0,   0, 135);   // TopbarPlus icon / panel background
    static const ImU32 kPillActive = IM_COL32(255, 255, 255, 235);

    // RGBA (0xRRGGBBAA) -> ImGui ABGR
    inline ImU32 FromRGBA(uint32_t c, float alphaMul = 1.0f)
    {
        int a = (int)((c & 0xFF) * alphaMul);
        return IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, a);
    }

    // Text with a 1px black outline like SA-MP's name tags/chat
    void OutlinedText(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text, float alphaMul = 1.0f);

    // 2.37: a player name. "[ADMIN] SENJI" (only the server can give that tag) is drawn with an
    // animated RGB "[ADMIN]" rank (red, blue, violet, yellow, green) and a white+black flowing "SENJI"; any other name is
    // drawn like OutlinedText. outline = false for list rows (plain AddText look).
    enum { NAME_TAG = 0, NAME_CHAT = 1, NAME_FEED = 2, NAME_LIST = 3 };   // where the name is drawn (3.00: same admin look in every place)
    void NameText(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* name, float alphaMul = 1.0f, bool outline = true, int style = NAME_TAG);
    bool IsAdminName(const char* name);
    float AdminTagLift(float fontSize); // 3.00: 0 (no dragon to clear the health bar any more)
}
