// 4.11: sscanf 2 (https://github.com/pawn-lang/sscanf) natives for SA-MP gamemodes on MP.
// The specifiers gamemodes use: i d h x o b n c l f g s[n] z u r q (upper case = optional with a
// (default)), p<delimiter>, 'literal', {quiet}, a<type>[n] arrays, e<...> enums. 0 = all parsed.
namespace {
static int g_SscanfInvalid = 0xFFFF;

struct SscIn
{
    const char* s; char delim = 0;
    void Skip() { while(*s && (*s == ' ' || *s == '\t' || (delim && *s == delim))) { if(delim && *s == delim) { ++s; break; } ++s; } while(*s == ' ' || *s == '\t') ++s; }
    std::string Word()
    {
        while(*s == ' ' || *s == '\t') ++s;
        std::string w;
        while(*s && *s != ' ' && *s != '\t' && !(delim && *s == delim)) w += *s++;
        if(delim && *s == delim) ++s;
        return w;
    }
    std::string Rest() { while(*s == ' ' || *s == '\t') ++s; std::string w = s; s += w.size(); while(!w.empty() && (w.back() == ' ' || w.back() == '\r' || w.back() == '\n')) w.pop_back(); return w; }
};
static bool SscInt(const std::string& w, char t, cell& v)
{
    if(w.empty()) return false;
    char* e = nullptr; long long r = 0;
    switch(t)
    {
        case 'h': case 'x': r = strtoll(w.c_str(), &e, 16); break;
        case 'o': r = strtoll(w.c_str(), &e, 8); break;
        case 'b': r = strtoll(w.c_str() + ((w.size() > 2 && w[0] == '0' && (w[1] == 'b' || w[1] == 'B')) ? 2 : 0), &e, 2); break;
        case 'n': r = strtoll(w.c_str(), &e, 0); break;
        default: r = strtoll(w.c_str(), &e, 10); break;
    }
    if(!e || *e) return false;
    v = (cell)r; return true;
}
static cell SscPlayer(const std::string& w)
{
    if(w.empty()) return g_SscanfInvalid;
    bool num = true; for(char c : w) if(!isdigit((unsigned char)c)) { num = false; break; }
    if(num) { long id = atol(w.c_str()); return PlOk(id) ? (cell)id : g_SscanfInvalid; }
    std::string lw = Lower(w); int best = -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!g_SampPl[i].on) continue;
        std::string n = Lower(g_SampPl[i].name);
        if(n == lw) return i;
        if(best < 0 && n.find(lw) != std::string::npos) best = i;
    }
    return best < 0 ? g_SscanfInvalid : best;
}
// read "(default)" after an upper case specifier
static std::string SscDefault(const char*& f)
{
    std::string d; if(*f != '(') return d;
    ++f; int depth = 1;
    while(*f && depth) { if(*f == '(') ++depth; else if(*f == ')') { if(!--depth) { ++f; break; } } d += *f++; }
    return d;
}
static int SscLen(const char*& f, int def)
{
    if(*f != '[') return def;
    ++f; std::string n; while(*f && *f != ']') n += *f++;
    if(*f == ']') ++f;
    int v = atoi(n.c_str()); return v > 0 ? v : def;
}
static bool SscIsLast(const char* f) { while(*f == ' ') ++f; return !*f; }

// one value of a simple type: returns false = parse failed
static bool SscOne(char t, SscIn& in, bool last, cell* out, int strLen, AMX* amx, bool opt, const std::string& def)
{
    char lt = (char)tolower((unsigned char)t);
    std::string w;
    if(lt == 's' || lt == 'z') w = last ? in.Rest() : in.Word(); else w = in.Word();
    if(w.empty())
    {
        if(!opt) return false;
        w = def;
        if(lt == 's' || lt == 'z') { if(out) amx_SetString(out, w.c_str(), 0, 0, (size_t)strLen); return true; }
        if(w.empty()) { if(out) *out = (lt == 'u' || lt == 'r' || lt == 'q') ? g_SscanfInvalid : 0; return true; }
    }
    (void)amx;
    switch(lt)
    {
        case 'i': case 'd': case 'h': case 'x': case 'o': case 'b': case 'n': { cell v; if(!SscInt(w, lt, v)) return false; if(out) *out = v; return true; }
        case 'c': if(out) *out = (unsigned char)w[0]; return true;
        case 'l': if(out) *out = (w == "1" || Lower(w) == "true" || Lower(w) == "yes") ? 1 : 0; return true;
        case 'f': case 'g': { char* e = nullptr; float v = strtof(w.c_str(), &e); if(!e || *e) return false; if(out) *out = amx_ftoc(v); return true; }
        case 's': case 'z': if(out) amx_SetString(out, w.c_str(), 0, 0, (size_t)strLen); return true;
        case 'u': case 'r': case 'q': if(out) *out = SscPlayer(w); return true;
        default: return false;
    }
}
static int SscRun(AMX* amx, const cell* params, const std::string& data, const std::string& fmt, int first)
{
    SscIn in; in.s = data.c_str();
    const char* f = fmt.c_str(); int pi = first, n = NArgs(params), quiet = 0;
    auto nextArg = [&]() -> cell* { if(quiet) return nullptr; return pi <= n ? Addr(amx, params[pi++]) : nullptr; };
    while(*f)
    {
        char t = *f++;
        if(t == ' ') continue;
        if(t == '{') { ++quiet; continue; }
        if(t == '}') { if(quiet) --quiet; continue; }
        if(t == 'p' || t == 'P')
        {   // p<,>
            if(*f == '<') { ++f; in.delim = *f ? *f++ : 0; if(*f == '>') ++f; }
            continue;
        }
        if(t == '\'')
        {
            std::string lit; while(*f && *f != '\'') lit += *f++;
            if(*f == '\'') ++f;
            const char* at = strstr(in.s, lit.c_str()); if(!at) return 1;
            in.s = at + lit.size(); continue;
        }
        bool opt = isupper((unsigned char)t) != 0; char lt = (char)tolower((unsigned char)t);
        if(lt == 'a')
        {   // a<i>[10]
            char et = 'i'; if(*f == '<') { ++f; et = *f ? *f++ : 'i'; if(*f == '>') ++f; }
            std::string def = opt ? SscDefault(f) : ""; int len = SscLen(f, 1);
            cell* out = nextArg();
            for(int i = 0; i < len; ++i)
            {
                cell* o = out ? out + i : nullptr;
                bool ok = SscOne(et, in, false, o, 1, amx, opt || i > 0, def);
                if(!ok) return i == 0 && !opt ? 1 : 0;
            }
            continue;
        }
        if(lt == 'e')
        {   // e<ifs[24]> -> one enum array
            std::string inner; if(*f == '<') { ++f; int d = 1; while(*f && d) { if(*f == '<') ++d; else if(*f == '>' && !--d) { ++f; break; } inner += *f++; } }
            cell* out = nextArg(); int off = 0;
            const char* g = inner.c_str();
            while(*g)
            {
                char et = *g++; if(et == ' ') continue;
                int sl = 1; if(tolower((unsigned char)et) == 's') sl = SscLen(g, 32);
                bool lastIn = SscIsLast(g) && SscIsLast(f);
                if(!SscOne(et, in, lastIn, out ? out + off : nullptr, sl, amx, isupper((unsigned char)et) != 0, "")) return 1;
                off += (tolower((unsigned char)et) == 's') ? sl : 1;
            }
            continue;
        }
        if(lt == 'k')
        {   // k<name>: the script's own parser sscanf_<name>(string[])
            std::string kn; if(*f == '<') { ++f; while(*f && *f != '>') kn += *f++; if(*f == '>') ++f; }
            std::string def = opt ? SscDefault(f) : "";
            std::string w = in.Word(); if(w.empty()) { if(!opt) return 1; w = def; }
            cell* out = nextArg(); cell r = 0;
            CallPub(ScOf(amx), ("sscanf_" + kn).c_str(), &r, { AS(w) });
            if(out) *out = r; continue;
        }
        if(!strchr("idhxobnclfgszurq", lt)) continue;   // unknown: skip it
        std::string def = opt ? SscDefault(f) : "";
        int sl = 32; if(lt == 's' || lt == 'z') sl = SscLen(f, 32); else if(*f == '[') SscLen(f, 0);
        if(lt == 'z') opt = true;
        cell* out = nextArg();
        if(!SscOne(t, in, SscIsLast(f), out, sl, amx, opt, def)) return 1;
    }
    return 0;
}
N(SSCANF__) { return SscRun(amx, params, Str(amx, params[3]), Str(amx, params[4]), 5); }
N(sscanf) { return SscRun(amx, params, Str(amx, params[1]), Str(amx, params[2]), 3); }
N(unformat) { return SscRun(amx, params, Str(amx, params[1]), Str(amx, params[2]), 3); }
N(SSCANF_Init) { if(NArgs(params) >= 2) g_SscanfInvalid = (int)params[2]; return 1; }
N(SSCANF_Version) { if(NArgs(params) >= 2) SetStr(amx, params[1], "2.13.8", params[2]); return 0x021308; }
N(SSCANF_IsConnected) { return PlOk(params[1]); }
N(SSCANF_Option) { UNUSED_AMX; return 0; }
N(SSCANF_TextSimilarity) { std::string a = Lower(Str(amx, params[1])), b = Lower(Str(amx, params[2])); return RF(a == b ? 1.0f : (a.find(b) != std::string::npos || b.find(a) != std::string::npos) ? 0.5f : 0.0f); }

static const AMX_NATIVE_INFO g_SscNatives[] =
{
    R(SSCANF__), R(sscanf), R(unformat), R(SSCANF_Init), R(SSCANF_Version), R(SSCANF_IsConnected), R(SSCANF_Option), R(SSCANF_TextSimilarity),
    RA("SSCANF_Join", Ret1), RA("SSCANF_Leave", Ret1), RA("SSCANF_SetPlayerName", Ret1), RA("SSCANF_GetLastError", Ret0), RA("SSCANF_ClearLastError", Ret1),
    RA("SSCANF_GetErrorSpecifier", Ret0), RA("SSCANF_Levenshtein", Ret0), RA("SetSSCANFOption", Ret0), RA("GetSSCANFOption", Ret0),
};
static void SampRegisterSscanf(AMX* amx) { amx_Register(amx, g_SscNatives, (int)(sizeof(g_SscNatives) / sizeof(g_SscNatives[0]))); }
} // namespace
