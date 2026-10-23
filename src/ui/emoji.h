// 2.19: chat emojis (see emoji.cpp)
#pragma once
#include <string>
#include <stdint.h>
struct ImFontAtlas; struct ImFont;
namespace UI
{
    void        EmojiAddGlyphs(ImFontAtlas* atlas, ImFont* font, float fontSize);   // before the atlas is built
    void        EmojiBake(ImFontAtlas* atlas, ImFont* font);                         // after Build()
    int         EmojiCount();
    const char* EmojiUtf8(int i);     // the real emoji (what you type / send)
    const char* EmojiGlyph(int i);    // drawable form
    std::string EmojiToPUA(const char* text);
    int         Utf8Decode(const char* s, uint32_t* cp);
}
