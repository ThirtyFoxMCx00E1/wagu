// 3.50: MP Textdraw Editor file (.mptd) - ONE file per textdraw project.
//
// Made by the in-game editor (/tde) or the MP Textdraw Editor app, imported in MP Launcher >
// Add server > Plugins (type Textdraw): the file is copied into files/MP/<server name>/ and every
// player that joins the server you host sees the textdraws.
//
// The file is plain JSON (UTF-8), so it can be read and written by anything:
//   { "format": "mp-textdraw", "version": 1, "name": "My HUD", "author": "...",
//     "created": "2026-11-01 10:00", "updated": "...",
//     "settings": { "globalVar": "Text_Global", "playerVar": "Text_Player" },
//     "textdraws": [ { "var": "", "type": "global", "text": "Hello", "x": 270, "y": 170,
//         "letterX": 0.3, "letterY": 1.5, "textX": 0, "textY": 0, "align": 1,
//         "color": "FFFFFFFF", "useBox": 0, "boxColor": "00000096", "shadow": 1, "outline": 1,
//         "bgColor": "00000096", "font": 1, "proportional": 1, "selectable": 0,
//         "model": 0, "rot": [0, 0, 0, 1], "vehCol": [-1, -1], "group": 0 } ],
//     "deleted": [ { ...same fields..., "deletedAt": "2026-11-01 10:05" } ] }
//
// Fields and defaults are the ones of the SA-MP Textdraw Editor (Nexor, samp-textdraw-editor):
// 640x448 screen, fonts 0..3 text, 4 = sprite (text = "LIB:name", textX/textY = size),
// 5 = preview model. Import / export of its Pawn .txt files (TextDrawCreate ... / open.mp names)
// is here too (MptdImportPawn / MptdExportPawn), parsed with sscanf like the original does.
#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <ctime>

struct MptdItem
{
    std::string var;           // variable name ("" = in the Text_Global[] / Text_Player[] array)
    int   player = 0;          // 0 = global, 1 = player textdraw
    std::string text = "New textdraw";
    float x = 270.0f, y = 170.0f;
    float letterX = 0.3f, letterY = 1.5f;
    float textX = 0.0f, textY = 0.0f;
    int   align = 1;           // 1 left, 2 centre, 3 right
    uint32_t color = 0xFFFFFFFF, boxColor = 0x00000096, bgColor = 0x00000096;   // RRGGBBAA
    int   useBox = 0, shadow = 1, outline = 1, font = 1, proportional = 1, selectable = 0;
    int   model = 0; float rot[4] = {0, 0, 0, 1}; int vehCol[2] = {-1, -1};
    int   group = 0;
    std::string deletedAt;     // only in "deleted"
};
struct MptdProject
{
    std::string name, author, created, updated;
    std::string globalVar = "Text_Global", playerVar = "Text_Player";
    std::vector<MptdItem> items, deleted;
};

// ---- a small JSON reader (objects, arrays, strings, numbers, true/false/null) ----------------
struct MptdJson
{
    enum T { NUL, NUM, STR, ARR, OBJ, BOOL } t = NUL;
    double n = 0; std::string s; std::vector<MptdJson> a; std::vector<std::pair<std::string, MptdJson>> o;
    const MptdJson* get(const char* k) const { for(auto& p : o) if(p.first == k) return &p.second; return nullptr; }
    double num(const char* k, double d) const { auto* v = get(k); return v ? (v->t == NUM || v->t == BOOL ? v->n : v->t == STR ? atof(v->s.c_str()) : d) : d; }
    std::string str(const char* k, const char* d = "") const { auto* v = get(k); if(!v) return d; if(v->t == STR) return v->s; if(v->t == NUM) { char b[32]; snprintf(b, sizeof(b), "%g", v->n); return b; } return d; }
};
struct MptdJsonParser
{
    const char* p; const char* e; int depth = 0;
    void ws() { while(p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p; }
    static void utf8(std::string& o, unsigned c)
    {
        if(c < 0x80) o += (char)c;
        else if(c < 0x800) { o += (char)(0xC0 | (c >> 6)); o += (char)(0x80 | (c & 63)); }
        else { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 63)); o += (char)(0x80 | (c & 63)); }
    }
    bool str(std::string& o)
    {
        if(p >= e || *p != '"') return false;
        ++p;
        while(p < e && *p != '"')
        {
            if(*p == '\\' && p + 1 < e)
            {
                ++p;
                switch(*p)
                {
                    case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': break;
                    case 'b': case 'f': break;
                    case 'u': { if(p + 4 < e) { char h[5] = {p[1], p[2], p[3], p[4], 0}; utf8(o, (unsigned)strtoul(h, nullptr, 16)); p += 4; } break; }
                    default: o += *p;
                }
                ++p;
            }
            else o += *p++;
            if(o.size() > 65536) return false;
        }
        if(p >= e) return false;
        ++p; return true;
    }
    bool val(MptdJson& v)
    {
        if(++depth > 32) return false;
        ws(); if(p >= e) return false;
        bool ok = true;
        if(*p == '{')
        {
            v.t = MptdJson::OBJ; ++p; ws();
            if(p < e && *p == '}') { ++p; }
            else for(;;)
            {
                ws(); std::string k; if(!str(k)) { ok = false; break; }
                ws(); if(p >= e || *p != ':') { ok = false; break; } ++p;
                v.o.emplace_back(k, MptdJson()); if(!val(v.o.back().second)) { ok = false; break; }
                ws(); if(p < e && *p == ',') { ++p; continue; } if(p < e && *p == '}') { ++p; break; } ok = false; break;
            }
        }
        else if(*p == '[')
        {
            v.t = MptdJson::ARR; ++p; ws();
            if(p < e && *p == ']') { ++p; }
            else for(;;)
            {
                v.a.emplace_back(); if(!val(v.a.back())) { ok = false; break; }
                if(v.a.size() > 4096) { ok = false; break; }
                ws(); if(p < e && *p == ',') { ++p; continue; } if(p < e && *p == ']') { ++p; break; } ok = false; break;
            }
        }
        else if(*p == '"') { v.t = MptdJson::STR; ok = str(v.s); }
        else if(!strncmp(p, "true", 4)) { v.t = MptdJson::BOOL; v.n = 1; p += 4; }
        else if(!strncmp(p, "false", 5)) { v.t = MptdJson::BOOL; v.n = 0; p += 5; }
        else if(!strncmp(p, "null", 4)) { v.t = MptdJson::NUL; p += 4; }
        else { char* q = nullptr; v.t = MptdJson::NUM; v.n = strtod(p, &q); if(!q || q == p) ok = false; else p = q; }
        --depth; return ok;
    }
};
static inline uint32_t MptdColor(const MptdJson& o, const char* k, uint32_t d)
{
    const MptdJson* v = o.get(k); if(!v) return d;
    if(v->t == MptdJson::NUM) return (uint32_t)(int64_t)v->n;
    if(v->t != MptdJson::STR) return d;
    const char* s = v->s.c_str(); while(*s == '#' || *s == ' ') ++s;
    if(s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    size_t n = strlen(s); uint32_t c = (uint32_t)strtoul(s, nullptr, 16);
    if(n <= 6) c = (c << 8) | 0xFF;
    return c;
}
static inline void MptdReadItem(const MptdJson& o, MptdItem& it)
{
    it.var = o.str("var"); it.player = o.str("type") == "player" ? 1 : (int)o.num("player", 0) ? 1 : 0;
    it.text = o.str("text", "_"); if(it.text.size() > 255) it.text.resize(255);
    it.x = (float)o.num("x", 270); it.y = (float)o.num("y", 170);
    it.letterX = (float)o.num("letterX", 0.3); it.letterY = (float)o.num("letterY", 1.5);
    it.textX = (float)o.num("textX", 0); it.textY = (float)o.num("textY", 0);
    it.align = (int)o.num("align", 1); if(it.align < 1 || it.align > 3) it.align = 1;
    it.color = MptdColor(o, "color", 0xFFFFFFFF); it.boxColor = MptdColor(o, "boxColor", 0x00000096); it.bgColor = MptdColor(o, "bgColor", 0x00000096);
    it.useBox = (int)o.num("useBox", 0) ? 1 : 0; it.shadow = (int)o.num("shadow", 1); it.outline = (int)o.num("outline", 1);
    it.font = (int)o.num("font", 1); if(it.font < 0 || it.font > 5) it.font = 1;
    it.proportional = (int)o.num("proportional", 1) ? 1 : 0; it.selectable = (int)o.num("selectable", 0) ? 1 : 0;
    it.model = (int)o.num("model", 0);
    if(auto* r = o.get("rot")) for(size_t i = 0; i < 4 && i < r->a.size(); ++i) it.rot[i] = (float)r->a[i].n;
    if(auto* c = o.get("vehCol")) for(size_t i = 0; i < 2 && i < c->a.size(); ++i) it.vehCol[i] = (int)c->a[i].n;
    it.group = (int)o.num("group", 0) ? 1 : 0;
    it.deletedAt = o.str("deletedAt");
}
// false = not an MP textdraw file
static inline bool MptdParse(const std::string& txt, MptdProject& pr, std::string* err = nullptr)
{
    MptdJsonParser ps; ps.p = txt.data(); ps.e = txt.data() + txt.size();
    if(txt.size() >= 3 && (uint8_t)txt[0] == 0xEF) ps.p += 3;   // UTF-8 BOM
    MptdJson root;
    if(!ps.val(root) || root.t != MptdJson::OBJ) { if(err) *err = "not a JSON file"; return false; }
    if(root.str("format") != "mp-textdraw") { if(err) *err = "not an MP Textdraw Editor file (.mptd)"; return false; }
    pr = MptdProject();
    pr.name = root.str("name"); pr.author = root.str("author"); pr.created = root.str("created"); pr.updated = root.str("updated");
    if(auto* s = root.get("settings")) { pr.globalVar = s->str("globalVar", "Text_Global"); pr.playerVar = s->str("playerVar", "Text_Player"); }
    if(auto* a = root.get("textdraws")) for(auto& o : a->a) { MptdItem it; MptdReadItem(o, it); pr.items.push_back(it); }
    if(auto* a = root.get("deleted")) for(auto& o : a->a) { MptdItem it; MptdReadItem(o, it); pr.deleted.push_back(it); }
    return true;
}

// ---- writer --------------------------------------------------------------------------------
static inline std::string MptdEsc(const std::string& s)
{
    std::string o; o.reserve(s.size() + 8);
    for(unsigned char c : s)
    {
        if(c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if(c == '\n') o += "\\n";
        else if(c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
    return o;
}
static inline std::string MptdNum(double v) { char b[32]; snprintf(b, sizeof(b), "%.4f", v); std::string s = b; while(s.size() > 1 && s.back() == '0') s.pop_back(); if(s.back() == '.') s.pop_back(); if(s == "-0") s = "0"; return s; }
static inline std::string MptdItemJson(const MptdItem& t, bool del)
{
    char c1[12], c2[12], c3[12];
    snprintf(c1, sizeof(c1), "%08X", t.color); snprintf(c2, sizeof(c2), "%08X", t.boxColor); snprintf(c3, sizeof(c3), "%08X", t.bgColor);
    std::string o = "    { \"var\": \"" + MptdEsc(t.var) + "\", \"type\": \"" + (t.player ? "player" : "global") + "\", \"text\": \"" + MptdEsc(t.text) + "\",\n";
    o += "      \"x\": " + MptdNum(t.x) + ", \"y\": " + MptdNum(t.y) + ", \"letterX\": " + MptdNum(t.letterX) + ", \"letterY\": " + MptdNum(t.letterY) +
         ", \"textX\": " + MptdNum(t.textX) + ", \"textY\": " + MptdNum(t.textY) + ", \"align\": " + std::to_string(t.align) + ",\n";
    o += std::string("      \"color\": \"") + c1 + "\", \"useBox\": " + std::to_string(t.useBox) + ", \"boxColor\": \"" + c2 + "\", \"shadow\": " + std::to_string(t.shadow) +
         ", \"outline\": " + std::to_string(t.outline) + ", \"bgColor\": \"" + c3 + "\", \"font\": " + std::to_string(t.font) +
         ", \"proportional\": " + std::to_string(t.proportional) + ", \"selectable\": " + std::to_string(t.selectable) + ",\n";
    o += "      \"model\": " + std::to_string(t.model) + ", \"rot\": [" + MptdNum(t.rot[0]) + ", " + MptdNum(t.rot[1]) + ", " + MptdNum(t.rot[2]) + ", " + MptdNum(t.rot[3]) +
         "], \"vehCol\": [" + std::to_string(t.vehCol[0]) + ", " + std::to_string(t.vehCol[1]) + "], \"group\": " + std::to_string(t.group);
    if(del) o += ", \"deletedAt\": \"" + MptdEsc(t.deletedAt) + "\"";
    o += " }";
    return o;
}
static inline std::string MptdNow()
{
    time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
    char b[32]; strftime(b, sizeof(b), "%Y-%m-%d %H:%M", &tm); return b;
}
static inline std::string MptdWrite(const MptdProject& pr)
{
    std::string o = "{\n  \"format\": \"mp-textdraw\", \"version\": 1,\n";
    o += "  \"name\": \"" + MptdEsc(pr.name) + "\", \"author\": \"" + MptdEsc(pr.author) + "\",\n";
    o += "  \"created\": \"" + MptdEsc(pr.created) + "\", \"updated\": \"" + MptdEsc(pr.updated) + "\",\n";
    o += "  \"settings\": { \"globalVar\": \"" + MptdEsc(pr.globalVar) + "\", \"playerVar\": \"" + MptdEsc(pr.playerVar) + "\" },\n";
    o += "  \"textdraws\": [\n";
    for(size_t i = 0; i < pr.items.size(); ++i) o += MptdItemJson(pr.items[i], false) + (i + 1 < pr.items.size() ? ",\n" : "\n");
    o += "  ],\n  \"deleted\": [\n";
    for(size_t i = 0; i < pr.deleted.size(); ++i) o += MptdItemJson(pr.deleted[i], true) + (i + 1 < pr.deleted.size() ? ",\n" : "\n");
    o += "  ]\n}\n";
    return o;
}

// ---- SA-MP Pawn export (the original editor's output, plain and open.mp) ------------------------
static inline std::string MptdExportPawn(const MptdProject& pr, bool openmp)
{
    auto B = [&](int v) -> std::string { return openmp ? (v ? "true" : "false") : std::to_string(v); };
    auto AL = [&](int a) -> std::string { if(!openmp) return std::to_string(a); return a == 2 ? "TEXT_DRAW_ALIGN_CENTER" : a == 3 ? "TEXT_DRAW_ALIGN_RIGHT" : "TEXT_DRAW_ALIGN_LEFT"; };
    auto FO = [&](int f) -> std::string { if(!openmp) return std::to_string(f); static const char* n[] = {"TEXT_DRAW_FONT_0", "TEXT_DRAW_FONT_1", "TEXT_DRAW_FONT_2", "TEXT_DRAW_FONT_3", "TEXT_DRAW_FONT_SPRITE_DRAW", "TEXT_DRAW_FONT_MODEL_PREVIEW"}; return n[f < 0 || f > 5 ? 1 : f]; };
    auto F = [](float v) { char b[32]; snprintf(b, sizeof(b), "%.3f", v); return std::string(b); };
    auto C = [](uint32_t c) { return std::to_string((int32_t)c); };
    const char* colorFn = openmp ? "Colour" : "Color";
    std::string o;
    int counts[2][2] = {{0, 0}, {0, 0}};
    for(auto& t : pr.items) counts[t.player][t.var.empty() ? 0 : 1]++;
    for(int pass = 0; pass < 4; ++pass)
    {
        const int pl = pass & 1, named = pass >> 1;   // global, player, named global, named player
        if(!counts[pl][named]) continue;
        if(!o.empty()) o += "####################################################################################################\r\n\r\n";
        if(!named) o += std::string(pl ? "new PlayerText: " : "new Text: ") + (pl ? pr.playerVar : pr.globalVar) + (pl ? "[MAX_PLAYERS][" : "[") + std::to_string(counts[pl][0]) + "];\r\n\r\n";
        else { for(auto& t : pr.items) if(t.player == pl && !t.var.empty()) o += std::string(pl ? "new PlayerText: " : "new Text: ") + t.var + (pl ? "[MAX_PLAYERS]" : "") + ";\r\n"; o += "\r\n"; }
        int idx = 0;
        for(auto& t : pr.items)
        {
            if(t.player != pl || t.var.empty() != !named) continue;
            std::string v = named ? t.var + (pl ? "[playerid]" : "") : (pl ? pr.playerVar + "[playerid][" + std::to_string(idx) + "]" : pr.globalVar + "[" + std::to_string(idx) + "]");
            std::string P = pl ? "PlayerTextDraw" : "TextDraw", a = pl ? "playerid, " + v : v;
            o += v + " = " + (pl ? "CreatePlayerTextDraw(playerid, " : "TextDrawCreate(") + F(t.x) + ", " + F(t.y) + ", \"" + t.text + "\");\r\n";
            if(t.letterX != 0 || t.letterY != 0) o += P + "LetterSize(" + a + ", " + F(t.letterX) + ", " + F(t.letterY) + ");\r\n";
            if(t.textX != 0 || t.textY != 0) o += P + "TextSize(" + a + ", " + F(t.textX) + ", " + F(t.textY) + ");\r\n";
            o += P + "Alignment(" + a + ", " + AL(t.align) + ");\r\n";
            o += P + colorFn + "(" + a + ", " + C(t.color) + ");\r\n";
            if(t.useBox) { o += P + "UseBox(" + a + ", " + B(1) + ");\r\n"; o += P + "Box" + colorFn + "(" + a + ", " + C(t.boxColor) + ");\r\n"; }
            o += P + "SetShadow(" + a + ", " + std::to_string(t.shadow) + ");\r\n";
            o += P + "SetOutline(" + a + ", " + std::to_string(t.outline) + ");\r\n";
            o += P + "Background" + colorFn + "(" + a + ", " + C(t.bgColor) + ");\r\n";
            o += P + "Font(" + a + ", " + FO(t.font) + ");\r\n";
            o += P + "SetProportional(" + a + ", " + B(t.proportional) + ");\r\n";
            if(t.font == 5)
            {
                o += P + "SetPreviewModel(" + a + ", " + std::to_string(t.model) + ");\r\n";
                o += P + "SetPreviewRot(" + a + ", " + F(t.rot[0]) + ", " + F(t.rot[1]) + ", " + F(t.rot[2]) + ", " + F(t.rot[3]) + ");\r\n";
                o += P + (openmp ? "SetPreviewVehicleColours(" : "SetPreviewVehCol(") + a + ", " + std::to_string(t.vehCol[0]) + ", " + std::to_string(t.vehCol[1]) + ");\r\n";
            }
            if(t.selectable) o += P + "SetSelectable(" + a + ", " + B(1) + ");\r\n";
            o += "\r\n";
            ++idx;
        }
    }
    return o;
}

// ---- SA-MP Pawn import (any TextDrawCreate / CreatePlayerTextDraw script, plain or open.mp) -----
static inline int MptdPawnInt(const std::string& v, bool font)
{
    if(v == "true") return 1;
    if(v == "false") return 0;
    if(v.find("ALIGN_CENTER") != std::string::npos || v.find("ALIGN_CENTRE") != std::string::npos) return 2;
    if(v.find("ALIGN_RIGHT") != std::string::npos) return 3;
    if(v.find("ALIGN_LEFT") != std::string::npos) return 1;
    if(v.find("SPRITE_DRAW") != std::string::npos) return 4;
    if(v.find("MODEL_PREVIEW") != std::string::npos) return 5;
    if(font) { size_t p = v.find("FONT_"); if(p != std::string::npos) return atoi(v.c_str() + p + 5); }
    if(v.size() > 2 && v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) return (int)(uint32_t)strtoul(v.c_str() + 2, nullptr, 16);
    if(v.size() > 1 && v[0] == '-') return (int)strtol(v.c_str(), nullptr, 10);
    return (int)(uint32_t)strtoul(v.c_str(), nullptr, 10);
}
static inline void MptdSplitArgs(const std::string& in, std::vector<std::string>& out)
{
    out.clear(); std::string cur; bool q = false;
    for(size_t i = 0; i < in.size(); ++i)
    {
        char c = in[i];
        if(c == '"' && (i == 0 || in[i - 1] != '\\')) { q = !q; cur += c; continue; }
        if(c == ',' && !q) { out.push_back(cur); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(cur);
    for(auto& s : out) { size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
}
// returns the number of textdraws added
static inline int MptdImportPawn(const std::string& src, MptdProject& pr, int limit = 2048)
{
    std::vector<std::pair<std::string, size_t>> vars;   // variable expression -> item index
    auto find = [&](const std::string& v) -> MptdItem* { for(auto it = vars.rbegin(); it != vars.rend(); ++it) if(it->first == v) return &pr.items[it->second]; return nullptr; };
    int added = 0; size_t pos = 0;
    std::vector<std::string> a;
    while(pos < src.size())
    {
        size_t eol = src.find('\n', pos); if(eol == std::string::npos) eol = src.size();
        std::string ln = src.substr(pos, eol - pos); pos = eol + 1;
        size_t cm = ln.find("//"); if(cm != std::string::npos && ln.find('"') > cm) ln.resize(cm);
        size_t lp = ln.find('('), rp = ln.rfind(')');
        if(lp == std::string::npos || rp == std::string::npos || rp < lp) continue;
        std::string head = ln.substr(0, lp), args = ln.substr(lp + 1, rp - lp - 1);
        size_t eq = head.find('=');
        std::string fn = head.substr(eq == std::string::npos ? 0 : eq + 1);
        { size_t a0 = fn.find_first_not_of(" \t"), b0 = fn.find_last_not_of(" \t"); fn = a0 == std::string::npos ? "" : fn.substr(a0, b0 - a0 + 1); }
        MptdSplitArgs(args, a);
        if(fn == "TextDrawCreate" || fn == "CreatePlayerTextDraw")
        {
            if(eq == std::string::npos || (int)pr.items.size() >= limit) continue;
            const bool pl = fn == "CreatePlayerTextDraw"; const size_t o = pl ? 1 : 0;
            if(a.size() < o + 3) continue;
            std::string v = head.substr(0, eq); { size_t a0 = v.find_first_not_of(" \t"), b0 = v.find_last_not_of(" \t"); v = v.substr(a0, b0 - a0 + 1); }
            if(v.compare(0, 4, "new ") == 0) { v = v.substr(4); size_t c = v.find(':'); if(c != std::string::npos) v = v.substr(c + 1); size_t a0 = v.find_first_not_of(" \t"); v = v.substr(a0); }
            MptdItem it; it.player = pl ? 1 : 0;
            float fx = 0, fy = 0; sscanf(a[o].c_str(), "%f", &fx); sscanf(a[o + 1].c_str(), "%f", &fy); it.x = fx; it.y = fy;
            std::string tx = a[o + 2]; if(tx.size() >= 2 && tx.front() == '"') tx = tx.substr(1, tx.size() - 2); it.text = tx.substr(0, 255);
            // defaults SA-MP gives a fresh textdraw (not the editor's "new textdraw" ones)
            it.letterX = 0; it.letterY = 0; it.shadow = 2; it.outline = 0; it.font = 1; it.proportional = 0; it.bgColor = 0x000000FF; it.boxColor = 0x80808080;
            // named variable (not Text_Global[3] style arrays of the editor)
            std::string base = v; size_t br = base.find('[');
            std::string key = v; if(br != std::string::npos) base = base.substr(0, br);
            const bool arr = br != std::string::npos && (base == pr.globalVar || base == pr.playerVar || base == "Text_Global" || base == "Text_Player");
            if(!arr) { it.var = base; }
            pr.items.push_back(it); vars.emplace_back(key, pr.items.size() - 1); ++added;
            continue;
        }
        const bool pl = fn.compare(0, 14, "PlayerTextDraw") == 0;
        if(!pl && fn.compare(0, 8, "TextDraw") != 0) continue;
        const size_t o = pl ? 1 : 0;
        if(a.size() < o + 2) continue;
        MptdItem* t = find(a[o]); if(!t) continue;
        std::string f = fn.substr(pl ? 14 : 8);
        auto Fv = [&](size_t i) { float v = 0; if(o + i < a.size()) sscanf(a[o + i].c_str(), "%f", &v); return v; };
        auto Iv = [&](size_t i, bool font = false) { return o + i < a.size() ? MptdPawnInt(a[o + i], font) : 0; };
        if(f == "LetterSize") { t->letterX = Fv(1); t->letterY = Fv(2); }
        else if(f == "TextSize") { t->textX = Fv(1); t->textY = Fv(2); }
        else if(f == "Alignment") { t->align = Iv(1); if(t->align < 1 || t->align > 3) t->align = 1; }
        else if(f == "Color" || f == "Colour") t->color = (uint32_t)Iv(1);
        else if(f == "UseBox") t->useBox = Iv(1) ? 1 : 0;
        else if(f == "BoxColor" || f == "BoxColour") t->boxColor = (uint32_t)Iv(1);
        else if(f == "SetShadow") t->shadow = Iv(1);
        else if(f == "SetOutline") t->outline = Iv(1);
        else if(f == "BackgroundColor" || f == "BackgroundColour") t->bgColor = (uint32_t)Iv(1);
        else if(f == "Font") { t->font = Iv(1, true); if(t->font < 0 || t->font > 5) t->font = 1; }
        else if(f == "SetProportional") t->proportional = Iv(1) ? 1 : 0;
        else if(f == "SetSelectable") t->selectable = Iv(1) ? 1 : 0;
        else if(f == "SetPreviewModel") t->model = Iv(1);
        else if(f == "SetPreviewRot") { t->rot[0] = Fv(1); t->rot[1] = Fv(2); t->rot[2] = Fv(3); t->rot[3] = a.size() > o + 4 ? Fv(4) : 1.0f; }
        else if(f == "SetPreviewVehCol" || f == "SetPreviewVehicleColours" || f == "SetPreviewVehicleColors") { t->vehCol[0] = Iv(1); t->vehCol[1] = Iv(2); }
        else if(f == "SetString") { std::string tx = a[o + 1]; if(tx.size() >= 2 && tx.front() == '"') tx = tx.substr(1, tx.size() - 2); t->text = tx.substr(0, 255); }
    }
    return added;
}
