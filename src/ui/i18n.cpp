// MP 3.52: languages - see i18n.h. The tables are read-only once loaded: a new language builds a new table
// and swaps the pointer (the old one is kept, so a line being drawn on another thread never reads freed memory).
#include "i18n.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <atomic>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace
{
const I18n::LangInfo kLangs[] = {
    { "en",    "English",            "English",               "",       false },
    { "es",    "Español",            "Spanish",               "",       false },
    { "fil",   "Filipino",           "Filipino",              "",       false },
    { "id",    "Bahasa Indonesia",   "Indonesian",            "",       false },
    { "pt-BR", "Português (Brasil)", "Portuguese (Brazil)",   "",       false },
    { "ar",    "\xEF\xBA\x94\xEF\xBB\xB4\xEF\xBA\x91\xEF\xBA\xAE\xEF\xBB\x8C\xEF\xBB\x9F\xEF\xBA\x8D", "Arabic", "", true },   // shaped, visual order
    { "ko",    "한국어",              "Korean",                "cjk-kr", false },
    { "ru",    "Русский",            "Russian",               "",       false },
    { "zh-CN", "简体中文",            "Chinese (Simplified)",  "cjk-sc", false },
    { "vi",    "Tiếng Việt",         "Vietnamese",            "",       false },
    { "ja",    "日本語",              "Japanese",              "cjk-jp", false },
    { "fr",    "Français",           "French",                "",       false },
    { "de",    "Deutsch",            "German",                "",       false },
    { "it",    "Italiano",           "Italian",               "",       false },
    { "tr",    "Türkçe",             "Turkish",               "",       false },
    { "pl",    "Polski",             "Polish",                "",       false },
    { "th",    "ไทย",                "Thai",                  "thai",   false },
    { "ms",    "Bahasa Melayu",      "Malay",                 "",       false },
    { "uk",    "Українська",         "Ukrainian",             "",       false },
    { "nl",    "Nederlands",         "Dutch",                 "",       false },
    { "zh-TW", "繁體中文",            "Chinese (Traditional)", "cjk-tc", false },
};
const int kLangCount = (int)(sizeof(kLangs) / sizeof(kLangs[0]));

inline uint64_t Hash(const char* b, const char* e)
{
    uint64_t h = 1469598103934665603ULL;
    for(; b < e; ++b) { h ^= (uint8_t)*b; h *= 1099511628211ULL; }
    return h ? h : 1;
}

// a printf spec at p? -> its length (0 = none). kind: 'n' number, 's' text, 'c' char, '%' a literal %
int Spec(const char* p, const char* e, char* kind)
{
    if(p >= e || *p != '%') return 0;
    const char* q = p + 1;
    if(q < e && *q == '%') { *kind = '%'; return 2; }
    while(q < e && strchr("-+ #0", *q)) ++q;
    while(q < e && (*q >= '0' && *q <= '9')) ++q;
    if(q < e && *q == '.') { ++q; while(q < e && (*q >= '0' && *q <= '9')) ++q; }
    while(q < e && strchr("hlzjt", *q)) ++q;
    if(q >= e) return 0;
    if(strchr("diuxXfFeEgG", *q)) *kind = 'n';
    else if(*q == 's') *kind = 's';
    else if(*q == 'c') *kind = 'c';
    else if(*q == 'p') *kind = 'p';
    else return 0;
    return (int)(q - p) + 1;
}
// the specs of a format in order ("%d %s" -> "ns"), literal %% left out
std::string Specs(const std::string& s)
{
    std::string out; const char* p = s.c_str(); const char* e = p + s.size();
    while(p < e) { char k; int n = Spec(p, e, &k); if(n) { if(k != '%') out += k; p += n; } else ++p; }
    return out;
}

std::string Unescape(const char* b, const char* e)
{
    std::string o; o.reserve(e - b);
    for(const char* p = b; p < e; ++p)
    {
        if(*p != '\\' || p + 1 >= e) { o += *p; continue; }
        char c = *++p;
        switch(c)
        {
        case 'n': o += '\n'; break;
        case 't': o += '\t'; break;
        case 'r': o += '\r'; break;
        case '"': o += '"'; break;
        case '\'': o += '\''; break;
        case '\\': o += '\\'; break;
        case '0': o += '\0'; break;
        case 'x':
        {
            int v = 0, k = 0;
            while(k < 2 && p + 1 < e && isxdigit((unsigned char)p[1])) { char d = *++p; v = v * 16 + (d <= '9' ? d - '0' : (d | 32) - 'a' + 10); ++k; }
            o += (char)v; break;
        }
        default: o += '\\'; o += c; break;
        }
    }
    return o;
}

struct Tpl { std::vector<std::string> lits; std::string kinds; uint32_t val; };
struct Table
{
    std::string code, font;
    std::vector<std::string> keys, vals;
    std::vector<uint32_t> slots; uint32_t mask = 0;            // exact texts (index + 1, 0 = empty)
    std::vector<uint32_t> nslots; uint32_t nmask = 0;          // number templates: key with \x01 for every number spec
    std::vector<std::string> nkeys;
    std::vector<uint32_t> nent;                                // nkeys index -> entry index
    std::vector<Tpl> tpls;                                     // %s templates (chat / server messages)
    std::unordered_multimap<uint32_t, uint32_t> tplByPrefix;   // first 4 bytes of the first literal
    std::vector<uint32_t> tplNoPrefix;
    std::string glyphs;
    mutable std::vector<std::pair<const ImFont*, int8_t>> fontOk;   // per entry: can this font draw it (render thread)
};
std::atomic<Table*> g_Table{ nullptr };
std::vector<Table*> g_Old;
int g_nCur = 0;
std::atomic<uint32_t> g_nGen{ 0 };
char g_szDir[400] = "";
char g_szErr[160] = "";
std::string g_Empty;

void PutSlot(std::vector<uint32_t>& slots, uint32_t mask, const std::vector<std::string>& keys, const std::string& k, uint32_t idx)
{
    uint64_t h = Hash(k.data(), k.data() + k.size());
    for(uint32_t i = (uint32_t)h & mask;; i = (i + 1) & mask)
    {
        if(!slots[i]) { slots[i] = idx + 1; return; }
        if(keys[slots[i] - 1] == k) return;   // first one wins
    }
}
int FindSlot(const std::vector<uint32_t>& slots, uint32_t mask, const std::vector<std::string>& keys, const char* b, const char* e)
{
    if(slots.empty()) return -1;
    const size_t n = e - b;
    uint64_t h = Hash(b, e);
    for(uint32_t i = (uint32_t)h & mask;; i = (i + 1) & mask)
    {
        uint32_t s = slots[i];
        if(!s) return -1;
        const std::string& k = keys[s - 1];
        if(k.size() == n && !memcmp(k.data(), b, n)) return (int)s - 1;
    }
}
std::string NumKey(const std::string& k, bool* onlyNums)
{
    std::string o; const char* p = k.c_str(); const char* e = p + k.size(); bool any = false; *onlyNums = true;
    while(p < e)
    {
        char kind; int n = Spec(p, e, &kind);
        if(!n) { o += *p++; continue; }
        if(kind == 'n') { o += '\x01'; any = true; }
        else if(kind == '%') o += '%';
        else *onlyNums = false;
        p += n;
    }
    if(!any) *onlyNums = false;
    return o;
}
bool IsNumStart(const char* p, const char* e) { return p < e && *p >= '0' && *p <= '9'; }
const char* NumEnd(const char* p, const char* e)
{
    while(p < e && *p >= '0' && *p <= '9') ++p;
    if(p + 1 < e && *p == '.' && p[1] >= '0' && p[1] <= '9') { ++p; while(p < e && *p >= '0' && *p <= '9') ++p; }
    return p;
}
// a format with the captured pieces put in for its specs (in order)
void FillFmt(const std::string& fmt, const std::vector<std::string>& caps, std::string& out)
{
    out.clear(); const char* p = fmt.c_str(); const char* e = p + fmt.size(); size_t ci = 0;
    while(p < e)
    {
        char kind; int n = Spec(p, e, &kind);
        if(!n) { out += *p++; continue; }
        if(kind == '%') out += '%'; else if(ci < caps.size()) out += caps[ci++];
        p += n;
    }
}

const char* RingStore(const std::string& s, const char** outEnd)
{
    static char ring[24][1024]; static int at = 0;
    char* b = ring[at]; at = (at + 1) % 24;
    size_t n = s.size() < sizeof(ring[0]) - 1 ? s.size() : sizeof(ring[0]) - 1;
    memcpy(b, s.data(), n); b[n] = 0;
    if(outEnd) *outEnd = b + n;
    return b;
}

bool FontHas(const ImFont* f, const std::string& s)
{
    if(!f) return true;
    const char* p = s.c_str(); const char* e = p + s.size();
    while(p < e)
    {
        unsigned int c = (unsigned char)*p;
        if(c < 0x80) { ++p; continue; }
        int n = ImTextCharFromUtf8(&c, p, e);
        if(n <= 0) return false;
        p += n;
        if(c == '\n' || c == 0xFEFF || (c >= 0xE000 && c < 0xF900)) continue;   // emoji glyphs live in the private area
        if(!f->FindGlyphNoFallback((ImWchar)c)) return false;
    }
    return true;
}

bool MatchTpl(const Tpl& t, const char* p, const char* e, size_t i, std::vector<std::string>& caps)
{
    // lits[i] is already matched; now spec i, then lits[i+1]
    if(i >= t.kinds.size()) return p == e;
    const std::string& next = t.lits[i + 1];
    const bool last = i + 1 == t.kinds.size();
    if(t.kinds[i] == 'n')
    {
        const char* q = p; if(q < e && *q == '-') ++q;
        if(!IsNumStart(q, e)) return false;
        q = NumEnd(q, e);
        if((size_t)(e - q) < next.size() || memcmp(q, next.data(), next.size())) return false;
        caps.emplace_back(p, q);
        if(MatchTpl(t, q + next.size(), e, i + 1, caps)) return true;
        caps.pop_back(); return false;
    }
    // %s / %c: shortest piece that lets the rest match (a %c is one byte)
    if(next.empty() && last) { if(t.kinds[i] == 'c' && e - p != 1) return false; caps.emplace_back(p, e); return true; }
    if(next.empty()) return false;
    for(const char* q = p; q + next.size() <= e; ++q)
    {
        if(t.kinds[i] == 'c' && q - p > 1) return false;
        if(memcmp(q, next.data(), next.size())) continue;
        if(last && q + next.size() != e) continue;
        caps.emplace_back(p, q);
        if(MatchTpl(t, q + next.size(), e, i + 1, caps)) return true;
        caps.pop_back();
    }
    return false;
}

Table* Build(const char* path, const char* code, char* err, size_t errN)
{
    FILE* f = fopen(path, "rb");
    if(!f) { snprintf(err, errN, "missing %s", path); return nullptr; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if(len <= 0 || len > 16 * 1024 * 1024) { fclose(f); snprintf(err, errN, "bad file %s", path); return nullptr; }
    std::string data((size_t)len, '\0');
    if(fread(&data[0], 1, (size_t)len, f) != (size_t)len) { fclose(f); snprintf(err, errN, "can't read %s", path); return nullptr; }
    fclose(f);
    Table* t = new Table; t->code = code;
    const char* p = data.c_str(); const char* end = p + data.size();
    while(p < end)
    {
        const char* nl = (const char*)memchr(p, '\n', end - p); if(!nl) nl = end;
        const char* le = nl; if(le > p && le[-1] == '\r') --le;
        if(p < le && *p == '#')
        {
            if(!strncmp(p, "#font=", 6)) t->font.assign(p + 6, le);
        }
        else if(const char* tab = (const char*)memchr(p, '\t', le - p))
        {
            std::string k = Unescape(p, tab), v = Unescape(tab + 1, le);
            // the translation must keep the format's specs, in the same order (or printf would read the wrong args)
            if(!k.empty() && !v.empty() && k != v && Specs(k) == Specs(v)) { t->keys.push_back(std::move(k)); t->vals.push_back(std::move(v)); }
        }
        p = nl + 1;
    }
    const uint32_t n = (uint32_t)t->keys.size();
    uint32_t cap = 16; while(cap < n * 2 + 2) cap <<= 1;
    t->slots.assign(cap, 0); t->mask = cap - 1;
    for(uint32_t i = 0; i < n; ++i) PutSlot(t->slots, t->mask, t->keys, t->keys[i], i);
    // number templates
    for(uint32_t i = 0; i < n; ++i) { bool only; std::string nk = NumKey(t->keys[i], &only); if(only) { t->nkeys.push_back(nk); t->nent.push_back(i); } }
    cap = 16; while(cap < t->nkeys.size() * 2 + 2) cap <<= 1;
    t->nslots.assign(cap, 0); t->nmask = cap - 1;
    for(uint32_t i = 0; i < t->nkeys.size(); ++i) PutSlot(t->nslots, t->nmask, t->nkeys, t->nkeys[i], i);
    // %s / mixed templates for chat lines
    for(uint32_t i = 0; i < n; ++i)
    {
        const std::string& k = t->keys[i];
        Tpl tp; tp.val = i; std::string lit; const char* a = k.c_str(); const char* e = a + k.size(); bool ok = true;
        while(a < e)
        {
            char kind; int m = Spec(a, e, &kind);
            if(!m) { lit += *a++; continue; }
            if(kind == '%') { lit += '%'; a += m; continue; }
            if(kind == 'p') { ok = false; break; }
            tp.lits.push_back(lit); lit.clear(); tp.kinds += kind; a += m;
        }
        tp.lits.push_back(lit);
        if(!ok || tp.kinds.empty()) continue;
        size_t fixed = 0; for(auto& l : tp.lits) fixed += l.size();
        if(fixed < 4) continue;   // "%s: %s" would match almost anything
        const uint32_t ti = (uint32_t)t->tpls.size();
        if(tp.lits[0].size() >= 4) { uint32_t key; memcpy(&key, tp.lits[0].data(), 4); t->tplByPrefix.emplace(key, ti); }
        else if(!tp.lits.back().empty()) t->tplNoPrefix.push_back(ti);
        else continue;
        t->tpls.push_back(std::move(tp));
    }
    t->fontOk.assign(n, { nullptr, -1 });
    for(auto& v : t->vals) t->glyphs += v;
    return t;
}
}   // namespace

namespace I18n
{
int Count() { return kLangCount; }
const LangInfo& Info(int i) { return kLangs[(i >= 0 && i < kLangCount) ? i : 0]; }
int Current() { return g_nCur; }
const char* CurrentCode() { return kLangs[g_nCur].code; }
const char* LastError() { return g_szErr; }
void SetDataDir(const char* dir) { snprintf(g_szDir, sizeof(g_szDir), "%s", dir ? dir : ""); }
uint32_t Generation() { return g_nGen.load(); }
int Entries() { Table* t = g_Table.load(); return t ? (int)t->keys.size() : 0; }
const std::string& Glyphs() { Table* t = g_Table.load(); return t ? t->glyphs : g_Empty; }
const char* FontKind() { Table* t = g_Table.load(); return t ? t->font.c_str() : ""; }
void ResetFontCache() { Table* t = g_Table.load(); if(t) t->fontOk.assign(t->fontOk.size(), { nullptr, -1 }); }   // 3.53

static void PathOf(const char* code, char* out, size_t n) { snprintf(out, n, "%s/lang/%s.lng", g_szDir, code); }
bool FileExists(const char* code)
{
    if(!code || !strcmp(code, "en")) return true;
    char p[512]; PathOf(code, p, sizeof(p));
    FILE* f = fopen(p, "rb"); if(!f) return false; fclose(f); return true;
}
bool Set(const char* code)
{
    int idx = -1;
    for(int i = 0; i < kLangCount; ++i) if(code && !strcmp(kLangs[i].code, code)) idx = i;
    if(idx < 0) { snprintf(g_szErr, sizeof(g_szErr), "unknown language %s", code ? code : "?"); return false; }
    if(idx == g_nCur && (idx == 0 || g_Table.load())) return true;
    Table* t = nullptr;
    if(idx)
    {
        char p[512]; PathOf(code, p, sizeof(p));
        t = Build(p, code, g_szErr, sizeof(g_szErr));
        if(!t) return false;
    }
    Table* old = g_Table.exchange(t);
    if(old) g_Old.push_back(old);   // kept: another thread may still be reading it
    g_nCur = idx; g_szErr[0] = 0;
    g_nGen.fetch_add(1);
    return true;
}

const char* Text(const ImFont* f, const char* b, const char* e, const char** outEnd)
{
    Table* t = g_Table.load();
    if(!e) e = b + strlen(b);
    if(outEnd) *outEnd = e;
    if(!t || e - b < 2 || e - b > 900) return b;
    int i = FindSlot(t->slots, t->mask, t->keys, b, e);
    if(i >= 0)
    {
        auto& c = t->fontOk[i];
        if(c.first != f || c.second < 0) { c.first = f; c.second = FontHas(f, t->vals[i]) ? 1 : 0; }
        if(!c.second) return b;
        if(outEnd) *outEnd = t->vals[i].data() + t->vals[i].size();
        return t->vals[i].c_str();
    }
    // numbers: "Players: 12" -> "Players: \x01"
    if(t->nkeys.empty()) return b;
    bool digit = false; for(const char* p = b; p < e; ++p) if(*p >= '0' && *p <= '9') { digit = true; break; }
    if(!digit) return b;
    static thread_local std::string key, out; static thread_local std::vector<std::string> caps;
    key.clear(); caps.clear();
    for(const char* p = b; p < e;)
    {
        const bool start = IsNumStart(p, e) && (p == b || !((p[-1] | 32) >= 'a' && (p[-1] | 32) <= 'z'));
        if(start) { const char* q = NumEnd(p, e); caps.emplace_back(p, q); key += '\x01'; p = q; }
        else key += *p++;
    }
    int n = FindSlot(t->nslots, t->nmask, t->nkeys, key.data(), key.data() + key.size());
    if(n < 0) return b;
    i = (int)t->nent[n];
    auto& c = t->fontOk[i];
    if(c.first != f || c.second < 0) { c.first = f; c.second = FontHas(f, t->vals[i]) ? 1 : 0; }
    if(!c.second) return b;
    FillFmt(t->vals[i], caps, out);
    return RingStore(out, outEnd);
}

const char* Fmt(const char* fmt)
{
    Table* t = g_Table.load();
    if(!t || !fmt) return fmt;
    int i = FindSlot(t->slots, t->mask, t->keys, fmt, fmt + strlen(fmt));
    return i >= 0 ? t->vals[i].c_str() : fmt;
}

bool Message(const std::string& in, std::string& out)
{
    Table* t = g_Table.load();
    if(!t || in.size() < 2) return false;
    const char* b = in.data(); const char* e = b + in.size();
    int i = FindSlot(t->slots, t->mask, t->keys, b, e);
    if(i >= 0) { out = t->vals[i]; return true; }
    std::vector<std::string> caps;
    auto tryT = [&](uint32_t ti) -> bool
    {
        const Tpl& tp = t->tpls[ti];
        const std::string& l0 = tp.lits[0]; const std::string& ln = tp.lits.back();
        if(in.size() < l0.size() + ln.size() || memcmp(b, l0.data(), l0.size())) return false;
        if(memcmp(e - ln.size(), ln.data(), ln.size())) return false;
        caps.clear();
        if(!MatchTpl(tp, b + l0.size(), e, 0, caps)) return false;
        FillFmt(t->vals[tp.val], caps, out);
        return true;
    };
    if(in.size() >= 4)
    {
        uint32_t k; memcpy(&k, b, 4);
        auto r = t->tplByPrefix.equal_range(k);
        for(auto it = r.first; it != r.second; ++it) if(tryT(it->second)) return true;
    }
    for(uint32_t ti : t->tplNoPrefix) if(tryT(ti)) return true;
    // a line that only differs in its numbers
    const char* oe; const char* r = Text(nullptr, b, e, &oe);
    if(r != b) { out.assign(r, oe); return true; }
    return false;
}
}   // namespace I18n
