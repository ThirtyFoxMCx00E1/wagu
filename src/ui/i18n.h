// MP 3.52: languages. Every text MP draws (launcher, menus, HUD, chat / server messages) is looked up in
// the chosen language's table (files/TYFMC/MP/lang/<code>.lng (DataDir()/lang), made by tools/i18n/translate.py) as it is drawn,
// so no screen needs its own translation code. English = the texts as they are in the source.
#pragma once
#include <string>
#include <cstdint>

struct ImFont;
namespace I18n
{
    struct LangInfo { const char* code; const char* native; const char* english; const char* font; bool rtl; };
    int  Count();                          // all languages, English first
    const LangInfo& Info(int i);
    int  Current();                        // index into Info()
    const char* CurrentCode();
    bool Set(const char* code);            // loads files/TYFMC/MP/lang/<code>.lng (DataDir()/lang) ("en" = no table). false: file missing
    bool FileExists(const char* code);
    int  Entries();                        // strings in the loaded table
    const char* LastError();
    void SetDataDir(const char* dir);

    // a text as it is drawn: [b, e) -> the translation (or b itself). Exact texts, and texts that only
    // differ in their numbers ("Players: 12" from "Players: %d"). f = the font it is drawn with: a
    // translation that font can't show (no glyphs) stays English.
    const char* Text(const ImFont* f, const char* b, const char* e, const char** outEnd);
    const char* Fmt(const char* fmt);      // a printf format -> its translated format (same specs, same order)
    bool Message(const std::string& in, std::string& out);   // a finished chat / server line ("%s joined", ...)

    // the font: every character the loaded table uses (to build the glyph ranges) + which phone font it needs
    const std::string& Glyphs();
    const char* FontKind();                // "", "cjk-sc", "cjk-tc", "cjk-jp", "cjk-kr", "thai"
    uint32_t Generation();                 // +1 every Set() (the HUD rebuilds its font atlas when it changes)
    void ResetFontCache();                 // 3.53: a new font atlas is in use (render thread)
}
