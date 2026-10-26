// MP 3.27 - PUBG-style compass strip (top centre), first used by the MW3 server.
//
//   195   210   SW   240   255   W   285        <- bearings every 15 deg, letters every 45 (N E S W bigger)
//   | ' ' | ' ' | ' ' | ' ' | ' ' | ' ' |        <- ticks every 5 deg, fading out to the sides
//                  ^                             <- centre pointer
//                [ 228 ]                         <- current bearing
//
// Marks (teammates, objectives) ride on the tick line in their colour; off-strip ones stick to the
// ends. Self-contained (ImGui + UI::S/UI::Font) so tools/preview can render it.
struct CompassMark { float bearing; uint32_t rgba; float dist; uint8_t shape; };   // shape 0 teammate triangle, 1 objective diamond

static float CompassBearing(float fx, float fy)   // ground forward vector -> 0 = north, 90 = east
{
    float b = atan2f(fx, fy) * 57.29578f;
    return b < 0 ? b + 360.0f : b;
}
static float CompassDelta(float a, float b)       // signed b - a in -180..180
{
    float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    return d;
}

static void PubgCompassDraw(ImDrawList* dl, float cx, float y, float width, float bearing, const CompassMark* marks, int nMarks)
{
    const float span = 150.0f, half = span * 0.5f, ppd = width / span;
    const float fsNum = UI::S(17), fsCard = UI::S(23);
    const float rowY = y + UI::S(2), tickY = y + UI::S(28), tickBig = UI::S(12), tickSmall = UI::S(6);
    const float x0 = cx - width * 0.5f, x1 = cx + width * 0.5f;
    // soft dark band, transparent at the ends (keeps the white readable over the bright SA sky)
    {
        float b0 = y - UI::S(4), b1 = tickY + tickBig + UI::S(4);
        ImU32 cIn = IM_COL32(0, 0, 0, 85), cOut = IM_COL32(0, 0, 0, 0);
        dl->AddRectFilledMultiColor(ImVec2(x0, b0), ImVec2(cx, b1), cOut, cIn, cIn, cOut);
        dl->AddRectFilledMultiColor(ImVec2(cx, b0), ImVec2(x1, b1), cIn, cOut, cOut, cIn);
    }
    static const char* kCard[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int first = (int)floorf((bearing - half) / 5.0f) * 5;
    for(int d = first; d <= bearing + half + 5; d += 5)
    {
        float off = (float)d - bearing;
        if(off < -half || off > half) continue;
        float x = cx + off * ppd;
        float t = fabsf(off) / half, a = 1.0f - t * t * t;           // fade to the ends
        int deg = ((d % 360) + 360) % 360;
        ImU32 col = IM_COL32(255, 255, 255, (int)(235 * a)), sh = IM_COL32(0, 0, 0, (int)(150 * a));
        bool big = deg % 15 == 0;
        float th = big ? tickBig : tickSmall, tw = big ? UI::S(2.6f) : UI::S(1.8f);
        dl->AddLine(ImVec2(x + 1, tickY + 1), ImVec2(x + 1, tickY + th + 1), sh, tw);
        dl->AddLine(ImVec2(x, tickY), ImVec2(x, tickY + th), col, tw);
        if(!big) continue;
        char txt[8]; float fs = fsNum; bool card = deg % 45 == 0;
        if(card) { snprintf(txt, sizeof(txt), "%s", kCard[deg / 45]); fs = (deg % 90 == 0) ? fsCard : fsNum + UI::S(1); }
        else snprintf(txt, sizeof(txt), "%d", deg);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, txt);
        float ty = rowY + (fsCard - ts.y) * 0.5f;
        ImU32 tc = (deg == 0) ? IM_COL32(255, 214, 64, (int)(255 * a)) : col;   // north in PUBG yellow
        dl->AddText(UI::Font(), fs, ImVec2(x - ts.x * 0.5f + 1, ty + 1), sh, txt);
        dl->AddText(UI::Font(), fs, ImVec2(x - ts.x * 0.5f, ty), tc, txt);
    }
    // marks on the tick line
    for(int i = 0; i < nMarks; ++i)
    {
        float off = CompassDelta(bearing, marks[i].bearing);
        bool clampd = off < -half || off > half;
        if(clampd) off = off < 0 ? -half : half;
        float x = cx + off * ppd, s = UI::S(7);
        float a = clampd ? 0.55f : 1.0f;
        uint32_t c = marks[i].rgba;
        ImU32 col = IM_COL32((c >> 24) & 255, (c >> 16) & 255, (c >> 8) & 255, (int)(255 * a));
        float my = tickY - UI::S(3);
        if(marks[i].shape == 1)
        {   // objective: a diamond with the distance under the ticks
            float d = s * 0.95f, cy = tickY + tickBig * 0.5f;
            dl->AddQuadFilled(ImVec2(x, cy - d - 1.5f), ImVec2(x + d + 1.5f, cy), ImVec2(x, cy + d + 1.5f), ImVec2(x - d - 1.5f, cy), IM_COL32(0, 0, 0, (int)(150 * a)));
            dl->AddQuadFilled(ImVec2(x, cy - d), ImVec2(x + d, cy), ImVec2(x, cy + d), ImVec2(x - d, cy), col);
            if(!clampd && marks[i].dist > 0)
            {
                char m[16]; snprintf(m, sizeof(m), "%dm", (int)marks[i].dist);
                ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(14), FLT_MAX, 0, m);
                dl->AddText(UI::Font(), UI::S(14), ImVec2(x - ts.x * 0.5f + 1, cy + d + UI::S(2) + 1), IM_COL32(0, 0, 0, (int)(170 * a)), m);
                dl->AddText(UI::Font(), UI::S(14), ImVec2(x - ts.x * 0.5f, cy + d + UI::S(2)), col, m);
            }
            continue;
        }
        dl->AddTriangleFilled(ImVec2(x - s - 1, my - s - 1), ImVec2(x + s + 1, my - s - 1), ImVec2(x, my + 2), IM_COL32(0, 0, 0, (int)(140 * a)));
        dl->AddTriangleFilled(ImVec2(x - s, my - s), ImVec2(x + s, my - s), ImVec2(x, my), col);
    }
    // centre pointer + bearing readout
    {
        float py = tickY + tickBig + UI::S(4);
        float s = UI::S(6);
        dl->AddTriangleFilled(ImVec2(cx, py), ImVec2(cx - s, py + s * 1.2f), ImVec2(cx + s, py + s * 1.2f), IM_COL32(255, 255, 255, 255));
        char b[8]; snprintf(b, sizeof(b), "%d", ((int)(bearing + 0.5f)) % 360);
        float fs = UI::S(19);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, b);
        float bw = ts.x + UI::S(18), by = py + s * 1.2f + UI::S(2);
        ImVec2 a(cx - bw * 0.5f, by), e(cx + bw * 0.5f, by + ts.y + UI::S(6));
        dl->AddRectFilled(a, e, IM_COL32(0, 0, 0, 150), UI::S(4));
        dl->AddRect(a, e, IM_COL32(255, 255, 255, 200), UI::S(4), 0, UI::S(1.5f));
        dl->AddText(UI::Font(), fs, ImVec2(cx - ts.x * 0.5f, by + UI::S(3)), IM_COL32(255, 255, 255, 255), b);
    }
}
