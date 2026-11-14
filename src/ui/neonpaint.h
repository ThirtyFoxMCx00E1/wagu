#pragma once
// MP 4.13.28: the neon title menu is drawn like PCMenu: at the end of the game's own menu pass (after
// MobileMenu::Render), with the game's MobileMenu::DrawSprite / DrawQuad, out of PNG textures read with the game's
// RtPNGImageRead from files/TYFMC/MP/neon/. No ImGui and nothing painted at runtime on the title screen.
#include <stdint.h>

namespace NeonPaint
{
    struct Col { uint8_t r, g, b, a; };
    struct Tex { void* t; int w, h; };
    enum Font { F_GOTHIC = 0, F_CAPS, F_UI, F_COUNT };

    bool Init(void* gameHandle);           // resolve the game functions (OnModLoad)
    bool Load(const char* mpDir);          // textures + font atlases (first title frame). false = not usable
    bool Ready();                          // Init + Load went through
    bool Failed();                         // Load was tried and failed (the stock menu stays)
    float W();                             // screen size (RsGlobal)
    float H();

    const Tex* T(const char* name);        // a loaded texture of neon/ by file name ("art.png"), null = missing
    void Begin();                          // render states for 2D (call once per paint)
    void End();
    void Quad(float x0, float y0, float x1, float y1, Col c);
    void Img(const Tex* t, float x0, float y0, float x1, float y1, Col c = {255, 255, 255, 255});
    void ImgUV(const Tex* t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Col top, Col bottom);
    // text: px = the font size (line height like ImGui's), y = the top of the line; track = extra space between letters
    float TextW(int font, const char* s, float px, float track = 0);
    void Text(int font, const char* s, float x, float y, float px, Col top, Col bottom, float track = 0);
    inline void Text(int font, const char* s, float x, float y, float px, Col c, float track = 0) { Text(font, s, x, y, px, c, c, track); }

    // touches (fed from MP's touch handler, read on the render thread)
    void OnTouch(int kind, int finger, float x, float y);   // kind: 1 = down, 2 = move, 3 = up
    void FrameInput();                     // call once per paint before the hit tests
    bool Tap(float x0, float y0, float x1, float y1);       // released inside, pressed inside
    bool Held(float x0, float y0, float x1, float y1);      // a finger is down inside
    void EatTap();                         // the tap of this frame is used (dialogs above the menu)
}
