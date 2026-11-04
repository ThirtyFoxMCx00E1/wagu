// 4.03: CS:GO style server list - shared by the launcher (mp_launcher.inc) and + Add server (mp.cpp)
#pragma once
#include "../../imgui/imgui.h"
#include "flags_data.h"
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
static int LCountryIndex(const char* cc)
{
    if(!cc || !cc[0]) return -1;
    for(int i = 0; i < kCountryCount; ++i) if(!strcasecmp(kCountries[i].code, cc)) return i;
    return -1;
}
static const char* LCountryName(const char* cc) { int i = LCountryIndex(cc); return i >= 0 ? kCountries[i].name : nullptr; }
static void LDrawFlag(ImDrawList* dl, int idx, ImVec2 a, ImVec2 b)   // idx: kCountries index, -1 = unknown
{
    ImVec2 uv(1, 1); ImTextureID t = (ImTextureID)HUD::Texture("flags.png", &uv);
    if(!t) { dl->AddRectFilled(a, b, IM_COL32(90, 95, 102, 255)); return; }
    int slot = idx + 1, cx = slot % MP_FLAG_COLS, cy = slot / MP_FLAG_COLS;
    float fx = (float)MP_FLAG_CELL_W / MP_FLAG_ATLAS, fy = (float)MP_FLAG_CELL_H / MP_FLAG_ATLAS;
    dl->AddImage(t, a, b, ImVec2(cx * fx * uv.x, cy * fy * uv.y), ImVec2((cx + 1) * fx * uv.x, (cy + 1) * fy * uv.y));
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 120));
}

