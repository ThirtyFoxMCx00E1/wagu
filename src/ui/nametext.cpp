// Admin name look: "[ADMIN]" rank in flowing RGB (red -> blue -> violet -> yellow -> green) and
// "SENJI" with white <-> black light flowing across it. Same look on the name tag, in chat, in the
// kill feed and in player lists (3.00: the dragons are gone).
// 3.01: the fire flash + "ADMIN KILL" kill popup is gone too - only the RGB rank + name is left.
// Only imgui + ui_common, everything is animated from ImGui::GetTime() (= every frame).
#include <math.h>
#include <float.h>
#include <string.h>
#include "ui_common.h"

bool UI::IsAdminName(const char* name) { return name && !strncmp(name, "[ADMIN] SENJI", 13); }

namespace
{
    // ---------------------------------------------------------------- helpers
    inline float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
    inline ImU32 DCol(int r, int g, int b, float a) { return IM_COL32(r > 255 ? 255 : r < 0 ? 0 : r, g > 255 ? 255 : g < 0 ? 0 : g, b > 255 ? 255 : b < 0 ? 0 : b, (int)(255.0f * Clamp01(a))); }

    // smooth 1D value noise: organic, never-repeating wobble instead of a perfect sine
    inline float Hash(float n) { float s = sinf(n * 127.1f) * 43758.5453f; return s - floorf(s); }
    inline float Noise(float x)
    {
        float i = floorf(x), f = x - i; f = f * f * (3.0f - 2.0f * f);
        return (Hash(i) * (1 - f) + Hash(i + 1) * f) * 2.0f - 1.0f;
    }

}

void UI::NameText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* name, float alphaMul, bool outline, int style)
{
    if(!IsAdminName(name))
    {
        if(outline) OutlinedText(dl, size, p, col, name, alphaMul);
        else dl->AddText(ImGui::GetFont(), size, p, (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 0xFF) * alphaMul) << 24), name);
        return;
    }
    (void)style;   // 3.00: same look everywhere (name tag, chat, kill feed, lists): RGB rank + name, no dragon
    ImFont* f = outline ? Font() : ImGui::GetFont();
    const float t = (float)ImGui::GetTime();
    const float o = fmaxf(1.0f, size * 0.06f);
    const int a = (int)(255 * alphaMul);
    const char* tagEnd = name + 7;          // "[ADMIN]"
    const char* nameBeg = name + 8;         // "SENJI..."
    const char* nameEnd = name + 13;
    float x = p.x;
    int idx = 0;
    for(const char* c = name; *c; ++c, ++idx)
    {
        char ch[2] = { *c, 0 };
        float w = f->CalcTextSizeA(size, FLT_MAX, 0, ch).x;
        if(*c == ' ') { x += w; continue; }
        ImU32 fill, edge;
        if(c < tagEnd)
        {   // [ADMIN]: RGB rank - red -> blue -> violet -> yellow -> green, flowing through the letters
            static const int kPal[5][3] = { {255, 40, 40}, {40, 90, 255}, {170, 60, 255}, {255, 225, 30}, {40, 220, 70} };
            float ph = fmodf(t * 1.6f - idx * 0.35f, 5.0f); if(ph < 0) ph += 5.0f;
            int i0 = (int)ph, i1 = (i0 + 1) % 5; float k = ph - (float)i0;
            k = k * k * (3.0f - 2.0f * k);
            int r = (int)(kPal[i0][0] + (kPal[i1][0] - kPal[i0][0]) * k);
            int g = (int)(kPal[i0][1] + (kPal[i1][1] - kPal[i0][1]) * k);
            int b = (int)(kPal[i0][2] + (kPal[i1][2] - kPal[i0][2]) * k);
            fill = IM_COL32(r, g, b, a);
            edge = IM_COL32(0, 0, 0, a);
        }
        else if(c >= nameBeg && c < nameEnd)
        {   // SENJI: white <-> black light flowing across the name, outline is the opposite shade
            float k = 0.5f + 0.5f * sinf(t * 3.2f - (idx - 8) * 0.8f + Noise(t * 0.7f) * 0.6f);
            int v = (int)(28 + 227 * k), e = v > 140 ? 0 : 245;   // dark letters get a light rim, light ones a dark rim
            fill = IM_COL32(v, v, v, a);
            edge = IM_COL32(e, e, e, (int)(a * 0.95f));
        }
        else { fill = (col & 0x00FFFFFF) | ((ImU32)a << 24); edge = IM_COL32(0, 0, 0, a); }   // ": ", " (you)"...
        if(outline || (c >= nameBeg && c < nameEnd))
        {
            dl->AddText(f, size, ImVec2(x - o, p.y), edge, ch); dl->AddText(f, size, ImVec2(x + o, p.y), edge, ch);
            dl->AddText(f, size, ImVec2(x, p.y - o), edge, ch); dl->AddText(f, size, ImVec2(x, p.y + o), edge, ch);
        }
        dl->AddText(f, size, ImVec2(x, p.y), fill, ch);
        x += w;
    }
}

float UI::AdminTagLift(float size) { (void)size; return 0.0f; }   // 3.00: no dragon -> the tag sits at the normal place

