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
    ImFont* PricedownFont();
    // 3.34: Settings > Display: 9 fonts, text effect (0 none, 1 outline, 2 bold outline, 3 shadow), font + UI scale
    #define UI_FONT_COUNT 9
    const char* FontName(int i);
    bool  FontAvailable(int i);
    void  SetLook(int font, int effect, float fontScale, float uiScale);   // 3.00: pricedown.ttf found on the phone (else nullptr)
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
    bool IsModName(const char* name);   // 3.35: "[MOD] name"
    float AdminTagLift(float fontSize); // 3.00: 0 (no dragon to clear the health bar any more)

    // 3.11: red X with a dark outline (muted voice / mic off)
    inline void DrawRedX(ImDrawList* dl, ImVec2 c, float h)
    {
        float t = h * 0.42f > 1.5f ? h * 0.42f : 1.5f;
        ImU32 red = IM_COL32(235, 40, 40, 255), ol = IM_COL32(0, 0, 0, 210);
        dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), ol, t + 2.0f);
        dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), ol, t + 2.0f);
        dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), red, t);
        dl->AddLine(ImVec2(c.x - h, c.y + h), ImVec2(c.x + h, c.y - h), red, t);
    }
    // 3.11: voice chat icons (white, drawn - no texture). c = centre, r = half height.
    inline void DrawMic(ImDrawList* dl, ImVec2 c, float r, ImU32 col, bool slashed = false)
    {
        float t = r * 0.16f > 1.2f ? r * 0.16f : 1.2f;
        float w = r * 0.36f, top = c.y - r * 0.92f, bot = c.y + r * 0.12f;
        dl->AddRectFilled(ImVec2(c.x - w, top), ImVec2(c.x + w, bot), col, w);                      // capsule
        dl->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), r * 0.62f, 0.15f, 3.14159f - 0.15f, 12);           // holder
        dl->PathStroke(col, 0, t);
        dl->AddLine(ImVec2(c.x, c.y + r * 0.5f), ImVec2(c.x, c.y + r * 0.82f), col, t);               // stem
        dl->AddLine(ImVec2(c.x - r * 0.34f, c.y + r * 0.86f), ImVec2(c.x + r * 0.34f, c.y + r * 0.86f), col, t);   // base
        if(slashed) DrawRedX(dl, ImVec2(c.x + r * 0.62f, c.y + r * 0.55f), r * 0.42f);   // mic off / muted: red X
    }
    inline void DrawSpeaker(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int waves = 2, bool muted = false)
    {
        float t = r * 0.15f > 1.2f ? r * 0.15f : 1.2f;
        ImVec2 p[6] = { ImVec2(c.x - r * 0.9f, c.y - r * 0.32f), ImVec2(c.x - r * 0.45f, c.y - r * 0.32f), ImVec2(c.x + r * 0.05f, c.y - r * 0.8f),
                        ImVec2(c.x + r * 0.05f, c.y + r * 0.8f), ImVec2(c.x - r * 0.45f, c.y + r * 0.32f), ImVec2(c.x - r * 0.9f, c.y + r * 0.32f) };
        dl->AddConvexPolyFilled(p, 6, col);
        if(muted) { DrawRedX(dl, ImVec2(c.x + r * 0.62f, c.y), r * 0.42f); return; }   // muted voice: red X
        for(int k = 0; k < waves; ++k)
        {
            dl->PathArcTo(ImVec2(c.x + r * 0.05f, c.y), r * (0.42f + 0.32f * k), -0.85f, 0.85f, 10);
            dl->PathStroke(col, 0, t);
        }
    }
}
