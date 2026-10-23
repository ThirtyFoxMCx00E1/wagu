// 2.19: emojis in the chat + virtual keyboard.
// ImWchar is 16 bit here (ImGui is shared with other AML mods, so it stays that way): every emoji
// is drawn as a coloured custom glyph on a private-use code point (U+E000 + index). Text keeps the
// real UTF-8 emoji (that's what goes over the network); EmojiToPUA() swaps them just for drawing.
#include "ui_common.h"
#include "emoji.h"
#include "emoji_sheet.h"
#include "../../imgui/stb_image.h"
#include <string>
#include <string.h>
#include <vector>
#include <math.h>

static std::string s_Utf8[kEmojiCount];
static bool s_bReady = false;
static int  s_nRect[kEmojiCount];
static int  s_nPx = 0;

static void PutUtf8(std::string& o, uint32_t c)
{
    if(c < 0x80) o.push_back((char)c);
    else if(c < 0x800) { o.push_back((char)(0xC0 | (c >> 6))); o.push_back((char)(0x80 | (c & 0x3F))); }
    else if(c < 0x10000) { o.push_back((char)(0xE0 | (c >> 12))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
    else { o.push_back((char)(0xF0 | (c >> 18))); o.push_back((char)(0x80 | ((c >> 12) & 0x3F))); o.push_back((char)(0x80 | ((c >> 6) & 0x3F))); o.push_back((char)(0x80 | (c & 0x3F))); }
}

// decode one UTF-8 char, returns its length (1 on bad bytes)
int UI::Utf8Decode(const char* s, uint32_t* cp)
{
    const unsigned char* u = (const unsigned char*)s;
    if(u[0] < 0x80) { *cp = u[0]; return 1; }
    if((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) { *cp = ((u[0] & 0x1F) << 6) | (u[1] & 0x3F); return 2; }
    if((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) { *cp = ((u[0] & 0x0F) << 12) | ((u[1] & 0x3F) << 6) | (u[2] & 0x3F); return 3; }
    if((u[0] & 0xF8) == 0xF0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80 && (u[3] & 0xC0) == 0x80)
    { *cp = ((u[0] & 0x07) << 18) | ((u[1] & 0x3F) << 12) | ((u[2] & 0x3F) << 6) | (u[3] & 0x3F); return 4; }
    *cp = u[0]; return 1;
}

void UI::EmojiAddGlyphs(ImFontAtlas* atlas, ImFont* font, float fontSize)
{
    for(int i = 0; i < kEmojiCount; ++i) { s_Utf8[i].clear(); PutUtf8(s_Utf8[i], kEmojiCodepoints[i]); }
    s_nPx = (int)(fontSize * 0.92f + 0.5f); if(s_nPx < 12) s_nPx = 12; if(s_nPx > 96) s_nPx = 96;
    for(int i = 0; i < kEmojiCount; ++i)
        s_nRect[i] = atlas->AddCustomRectFontGlyph(font, (ImWchar)(0xE000 + i), s_nPx, s_nPx, s_nPx + fontSize * 0.08f, ImVec2(fontSize * 0.04f, (fontSize - s_nPx) * 0.5f + fontSize * 0.04f));
}

void UI::EmojiBake(ImFontAtlas* atlas, ImFont* font)
{
    unsigned char* tex = nullptr; int tw = 0, th = 0, bpp = 0;
    atlas->GetTexDataAsRGBA32(&tex, &tw, &th, &bpp);
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load_from_memory(kEmojiSheetPng, (int)sizeof(kEmojiSheetPng), &w, &h, &n, 4);
    if(!tex || !px) { if(px) stbi_image_free(px); return; }
    for(int i = 0; i < kEmojiCount; ++i)
    {
        const ImFontAtlasCustomRect* r = atlas->GetCustomRectByIndex(s_nRect[i]);
        if(!r || !r->IsPacked()) continue;
        int cx = (i % kEmojiCols) * kEmojiCell, cy = (i / kEmojiCols) * kEmojiCell;
        for(int y = 0; y < r->Height; ++y)
            for(int x = 0; x < r->Width; ++x)
            {   // box filter from the 48 px cell (premultiplied, then back)
                float x0 = x * (float)kEmojiCell / r->Width, x1 = (x + 1) * (float)kEmojiCell / r->Width;
                float y0 = y * (float)kEmojiCell / r->Height, y1 = (y + 1) * (float)kEmojiCell / r->Height;
                float acc[4] = { 0, 0, 0, 0 }; float wsum = 0;
                for(int sy = (int)y0; sy < (int)ceilf(y1) && sy < kEmojiCell; ++sy)
                    for(int sx = (int)x0; sx < (int)ceilf(x1) && sx < kEmojiCell; ++sx)
                    {
                        const unsigned char* s = px + ((cy + sy) * w + (cx + sx)) * 4;
                        float a = s[3] / 255.0f;
                        acc[0] += s[0] * a; acc[1] += s[1] * a; acc[2] += s[2] * a; acc[3] += a; wsum += 1.0f;
                    }
                unsigned char* d = tex + ((r->Y + y) * tw + (r->X + x)) * 4;
                if(wsum <= 0 || acc[3] <= 0) { d[0] = d[1] = d[2] = 255; d[3] = 0; continue; }
                d[0] = (unsigned char)(acc[0] / acc[3]); d[1] = (unsigned char)(acc[1] / acc[3]); d[2] = (unsigned char)(acc[2] / acc[3]);
                d[3] = (unsigned char)(acc[3] / wsum * 255.0f);
            }
        if(font) if(ImFontGlyph* g = (ImFontGlyph*)font->FindGlyphNoFallback((ImWchar)(0xE000 + i))) g->Colored = 1;   // keep its colours
    }
    stbi_image_free(px);
    s_bReady = true;
}

int UI::EmojiCount() { return kEmojiCount; }
const char* UI::EmojiUtf8(int i) { return (i >= 0 && i < kEmojiCount) ? s_Utf8[i].c_str() : ""; }
const char* UI::EmojiGlyph(int i)
{
    static std::string g[kEmojiCount];
    if(i < 0 || i >= kEmojiCount) return "";
    if(g[i].empty()) PutUtf8(g[i], 0xE000 + i);
    return g[i].c_str();
}

std::string UI::EmojiToPUA(const char* s)
{
    std::string o;
    if(!s) return o;
    o.reserve(strlen(s));
    while(*s)
    {
        uint32_t cp; int n = Utf8Decode(s, &cp);
        if(cp == 0xFE0F || cp == 0x200D) { s += n; continue; }   // variation selector / joiner
        int k = -1;
        if(cp >= 0x2000) for(int i = 0; i < kEmojiCount; ++i) if(kEmojiCodepoints[i] == cp) { k = i; break; }
        if(k >= 0) PutUtf8(o, 0xE000 + k);
        else o.append(s, n);
        s += n;
    }
    return o;
}
