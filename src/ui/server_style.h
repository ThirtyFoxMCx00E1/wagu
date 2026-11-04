// 4.03: CS:GO style server list - shared by the launcher (mp_launcher.inc) and + Add server (mp.cpp)
#pragma once
#include "../../imgui/imgui.h"
#include "flags_data.h"
#include <math.h>
// ---- 4.03: gamemode chip colours (+ Add server "Gamemode colour") ----------------------------
struct LModeCol { const char* name; ImU32 col; };
static const LModeCol kModeCols[] = {
    { "Auto",   IM_COL32( 66, 133, 190, 255) }, { "Blue",   IM_COL32( 52, 120, 210, 255) }, { "Red",    IM_COL32(196,  52,  52, 255) },
    { "Green",  IM_COL32( 64, 160,  64, 255) }, { "Sky",    IM_COL32( 40, 170, 220, 255) }, { "Yellow", IM_COL32(214, 168,  28, 255) },
    { "Purple", IM_COL32(128,  80, 196, 255) }, { "Orange", IM_COL32(222, 112,  32, 255) }, { "Pink",   IM_COL32(214,  72, 144, 255) },
    { "Teal",   IM_COL32( 30, 160, 150, 255) }, { "Grey",   IM_COL32(112, 118, 128, 255) },
};
static const int kModeColCount = (int)(sizeof(kModeCols) / sizeof(kModeCols[0]));
// ---- 4.03: country flags (flags.png atlas in bg_server.jar, src/ui/flags_data.h) ------------------
// 4.05.3: "global" = no single country (the featured 24/7 servers). It has no cell in flags.png, so its
//         flag (a white globe on blue) is drawn here and works with any bg_server.jar
#define MP_CC_GLOBAL "global"
static const int kCountryGlobal = kCountryCount;   // virtual index after the real countries
static int LCountryIndex(const char* cc)
{
    if(!cc || !cc[0]) return -1;
    if(!strcasecmp(cc, MP_CC_GLOBAL)) return kCountryGlobal;
    for(int i = 0; i < kCountryCount; ++i) if(!strcasecmp(kCountries[i].code, cc)) return i;
    return -1;
}
static const char* LCountryNameAt(int i) { return i == kCountryGlobal ? "Global" : (i >= 0 && i < kCountryCount ? kCountries[i].name : nullptr); }
static const char* LCountryCodeAt(int i) { return i == kCountryGlobal ? MP_CC_GLOBAL : (i >= 0 && i < kCountryCount ? kCountries[i].code : ""); }
static const char* LCountryName(const char* cc) { return LCountryNameAt(LCountryIndex(cc)); }
static void LDrawGlobalFlag(ImDrawList* dl, ImVec2 a, ImVec2 b)
{
    dl->AddRectFilled(a, b, IM_COL32(26, 86, 168, 255));
    const float w = b.x - a.x, h = b.y - a.y, r = h * 0.36f, th = h * 0.06f < 1.0f ? 1.0f : h * 0.06f;
    const ImVec2 c(a.x + w * 0.5f, a.y + h * 0.5f);
    const ImU32 wc = IM_COL32(255, 255, 255, 235);
    const int n = 24;
    dl->AddCircle(c, r, wc, n, th);
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), wc, th);   // equator
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), wc, th);   // prime meridian
    for(int k = 0; k < 2; ++k)   // two meridians (ellipses) + two parallels
    {
        const float ex = r * (k ? 0.75f : 0.4f);
        for(int i = 0; i <= n; ++i) { float t = 6.2831853f * i / n; dl->PathLineTo(ImVec2(c.x + cosf(t) * ex, c.y + sinf(t) * r)); }
        dl->PathStroke(wc, 0, th * 0.8f);
        const float py = r * 0.5f * (k ? 1.0f : -1.0f), px = sqrtf(r * r - py * py);
        dl->AddLine(ImVec2(c.x - px, c.y + py), ImVec2(c.x + px, c.y + py), wc, th * 0.8f);
    }
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 120));
}
static void LDrawFlag(ImDrawList* dl, int idx, ImVec2 a, ImVec2 b)   // idx: kCountries index, -1 = unknown, kCountryGlobal = Global
{
    if(idx == kCountryGlobal) { LDrawGlobalFlag(dl, a, b); return; }
    ImVec2 uv(1, 1); ImTextureID t = (ImTextureID)HUD::Texture("flags.png", &uv);
    if(!t) { dl->AddRectFilled(a, b, IM_COL32(90, 95, 102, 255)); return; }
    int slot = idx + 1, cx = slot % MP_FLAG_COLS, cy = slot / MP_FLAG_COLS;
    float fx = (float)MP_FLAG_CELL_W / MP_FLAG_ATLAS, fy = (float)MP_FLAG_CELL_H / MP_FLAG_ATLAS;
    dl->AddImage(t, a, b, ImVec2(cx * fx * uv.x, cy * fy * uv.y), ImVec2((cx + 1) * fx * uv.x, (cy + 1) * fy * uv.y));
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 120));
}

