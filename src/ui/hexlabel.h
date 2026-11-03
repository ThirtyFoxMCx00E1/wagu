#pragma once
// 3.55: hexagon HUD labels (like the SA-MP roleplay HUDs): a pointy-top hexagon with a coloured fill
// and a lighter border, the icon goes in the middle
#include "imgui.h"
#include <math.h>
static inline void HexPoints(ImVec2 c, float r, ImVec2* out)
{
    for(int i = 0; i < 6; ++i)
    {
        float a = (60.0f * i - 90.0f) * 3.14159265f / 180.0f;   // pointy top
        out[i] = ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r);
    }
}
static inline void HexLabel(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border, float borderW)
{
    ImVec2 p[6]; HexPoints(ImVec2(c.x + r * 0.06f, c.y + r * 0.08f), r, p);
    dl->AddConvexPolyFilled(p, 6, IM_COL32(0, 0, 0, 110));   // soft shadow
    HexPoints(c, r, p);
    dl->AddConvexPolyFilled(p, 6, border);
    HexPoints(c, r - borderW, p);
    dl->AddConvexPolyFilled(p, 6, fill);
}

// 3.58: see-through hexagon: the fill is drawn ONCE (black 50 % by default) and the border is only a
// stroked outline, so the value colour (green / yellow / red) never bleeds through the background.
static inline void HexLabelGlass(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border, float borderW)
{
    ImVec2 p[6];
    HexPoints(c, r, p);
    dl->AddConvexPolyFilled(p, 6, fill);
    HexPoints(c, r - borderW * 0.5f, p);
    dl->AddPolyline(p, 6, border, ImDrawFlags_Closed, borderW);
}
