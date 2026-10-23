#pragma once
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include <stdio.h>
#include <stdint.h>

namespace UI {
    float S(float px);
    ImFont* Font();
    static const ImU32 kAccent = IM_COL32(46, 180, 70, 255);
    static const ImU32 kAccentDark = IM_COL32(24, 110, 40, 255);
    inline ImU32 FromRGBA(uint32_t c, float alphaMul = 1.0f) {
        int a = (int)((c & 0xFF) * alphaMul);
        return IM_COL32((c >> 24) & 0xFF, (c >> 16) & 0xFF, (c >> 8) & 0xFF, a);
    }
    void OutlinedText(ImDrawList* dl, float size, ImVec2 pos, ImU32 col, const char* text, float alphaMul = 1.0f);
}
