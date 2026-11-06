// 4.11: SA-MP gamemodes on MP servers - included in server.cpp (needs MPServer members).
// A SA-MP server folder (server.cfg + gamemodes/ + filterscripts/ + scriptfiles/) is loaded on a
// custom server: the compiled .amx scripts run in the Pawn abstract machine (third_party/pawn-amx,
// from https://github.com/pawn-lang/compiler, zLib licence) and the SA-MP natives they call are made
// here on top of the MP server (messages, dialogs, textdraws, player state, objects, timers, PVars,
// files, Pawn.CMD, sscanf). MySQL R41 natives run on SQLite (server_samp_sql.inc). Natives that
// MP can't do yet return 0 and are listed in <folder>/mp_samp_report.txt.
#include "samp.h"
#include <string>
#include <map>
#include <set>
#include <memory>
#include <unordered_map>
#include <sys/stat.h>
#include <dirent.h>
#include <array>
#ifndef HAVE_STDINT_H
  #define HAVE_STDINT_H 1
#endif
#ifndef AMX_ANSIONLY
  #define AMX_ANSIONLY 1
#endif
#ifndef AMX_NODYNALOAD
  #define AMX_NODYNALOAD 1
#endif
#ifndef FLOATPOINT
  #define FLOATPOINT 1
#endif
extern "C" {
#include "../../third_party/pawn-amx/amx.h"
#include "../../third_party/pawn-amx/amxaux.h"
int AMXEXPORT AMXAPI amx_CoreInit(AMX* amx);
int AMXEXPORT AMXAPI amx_FloatInit(AMX* amx);
int AMXEXPORT AMXAPI amx_StringInit(AMX* amx);
}

namespace {

#define SAMP_MAX_TD      2048
#define SAMP_MAX_PTD     2048
#define SAMP_TD_BASE     0       // client textdraw slots the gamemode may use: 0..199 (200..255 stay MP's)
#define SAMP_TD_SLOTS    200
#define SAMP_INVALID_TD  0xFFFF

struct SampScript;
struct SampVar { int type = 0; cell i = 0; float f = 0; std::string s; };   // 1 int, 2 string, 3 float
struct SampArg { int kind = 0; cell v = 0; std::string s; std::vector<cell> a; };   // 0 cell, 1 string, 2 array
struct SampTimer { int id; SampScript* sc; std::string fn; uint32_t interval; bool repeat; uint64_t next; std::vector<SampArg> args; bool dead = false; };
struct SampTdDef { bool used = false; PktTextDraw t; bool selectable = false; };
struct SampPl
{
    bool on = false, spawned = false, spectating = false, controllable = true;
    int money = 0, score = 0, skin = 0, interior = 0, world = 0, team = 255, wanted = 0, weapon = 0, ammo = 0, drunk = 0;
    int state = 0, lastState = 0, dialog = -1, vehicle = 0, special = 0, fightStyle = 4, keys = 0;
    float hp = 100, ar = 0, a = 0, pos[3] = {0, 0, 0};
    uint32_t color = 0xFFFFFFFF;
    std::string name;
    std::map<std::string, SampVar> pvars;
    std::vector<SampTdDef> ptd = std::vector<SampTdDef>(SAMP_MAX_PTD);
    int tdSlot[SAMP_TD_SLOTS];   // client slot -> 0x4000 | global id, 0x6000 | player id, -1 free
    bool cp = false, rcp = false, inCp = false, inRcp = false; float cpPos[4] = {0}, rcpPos[4] = {0};
    struct Spawn { int team = 255, skin = 0; float x = 0, y = 0, z = 3, a = 0; int w[3] = {0}, am[3] = {0}; bool set = false; } spawn;
    uint64_t lastUpdate = 0, spawnAt = 0;
    std::vector<std::array<float, 5>> removed;   // RemoveBuildingForPlayer (model, x, y, z, r)
};
struct SampVeh { bool used = false; int model = 0; float x = 0, y = 0, z = 0, a = 0; int c1 = -1, c2 = -1, world = 0, interior = 0; float hp = 1000; };
struct SampObj { uint32_t row = 0; int model = 0; float p[3] = {0}, r[3] = {0}; int world = -1, interior = -1; };
struct SampLabel { bool used = false; std::string text; uint32_t color = 0xFFFFFFFF; float p[3] = {0}, dist = 30; int world = -1; };

struct SampScript
{
    AMX amx; std::vector<uint8_t> mem; std::string name, path; bool gm = false, ok = false;
    std::vector<std::string> natives; std::vector<bool> missing;
    int fOnPlayerCommandText = -1, fOnPlayerText = -1, fOnDialogResponse = -1;
};

struct SampState
{
    std::string root;
    std::vector<std::unique_ptr<SampScript>> scripts;   // filterscripts first, the gamemode last (SA-MP order)
    std::map<std::string, std::string> cfg;              // server.cfg
    std::vector<SampTimer> timers; int nextTimer = 1;
    std::map<std::string, SampVar> svars;
    SampTdDef td[SAMP_MAX_TD];
    std::vector<SampVeh> veh = std::vector<SampVeh>(2001);
    std::map<int, SampObj> obj; int nextObj = 1;
    std::vector<SampLabel> labels = std::vector<SampLabel>(1025);
    std::map<std::string, int> unknown;                  // missing native -> calls
    std::map<std::string, std::string> pcAlias;          // Pawn.CMD alias -> command
    std::map<std::string, int> pcFlags;
    bool pawnCmd = false;
    std::string modeText = "SA-MP gamemode", hostName;
    std::vector<FILE*> files;
    int areaNext = 1, pickupNext = 1, cpNext = 1, iconNext = 1, actorNext = 1, zoneNext = 1, menuNext = 1, plabelNext = 1;
    uint64_t reportAt = 0; bool reportDirty = false;
    int maxPlayers = 50;
    bool test = false;                                    // mp_server --samp-test: no network, prints what it would send
};

MPServer*   g_SampSrv = nullptr;
SampState*  g_Samp = nullptr;
SampPl      g_SampPl[MP_MAX_PLAYERS];

// ---- AMX helpers ------------------------------------------------------------------------------
static inline SampScript* ScOf(AMX* amx) { return (SampScript*)amx->userdata[0]; }
static cell* Addr(AMX* amx, cell a) { cell* p = nullptr; if(amx_GetAddr(amx, a, &p) != AMX_ERR_NONE) return nullptr; return p; }
static std::string Str(AMX* amx, cell a)
{
    cell* p = Addr(amx, a); if(!p) return std::string();
    int len = 0; amx_StrLen(p, &len); if(len <= 0) return std::string();
    std::string s; s.resize((size_t)len + 1);
    amx_GetString(&s[0], p, 0, (size_t)len + 1); s.resize(strlen(s.c_str()));
    return s;
}
static void SetStr(AMX* amx, cell a, const std::string& s, cell size)
{
    cell* p = Addr(amx, a); if(!p || size <= 0) return;
    amx_SetString(p, s.c_str(), 0, 0, (size_t)size);
}
static inline bool SetRef(AMX* amx, cell a, cell v) { cell* p = Addr(amx, a); if(!p) return false; *p = v; return true; }
static inline bool SetRefF(AMX* amx, cell a, float f) { return SetRef(amx, a, amx_ftoc(f)); }
static inline float PF(const cell* params, int i) { cell c = params[i]; return amx_ctof(c); }
static inline cell RF(float f) { return amx_ftoc(f); }
static inline int NArgs(const cell* params) { return (int)(params[0] / (cell)sizeof(cell)); }
static inline cell RefVal(AMX* amx, cell a) { cell* p = Addr(amx, a); return p ? *p : 0; }

static std::string Lower(std::string s) { for(auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool PlOk(cell id) { return id >= 0 && id < MP_MAX_PLAYERS && g_SampPl[id].on; }

// ---- calling publics --------------------------------------------------------------------------
struct SampPush
{   // args are pushed in reverse; heap blocks are freed after the call (lowest address = first block)
    AMX* amx; cell heapLow = -1;
    explicit SampPush(AMX* a) : amx(a) {}
    void C(cell v) { amx_Push(amx, v); }
    void S(const std::string& s) { cell a = 0; if(amx_PushString(amx, &a, nullptr, s.c_str(), 0, 0) == AMX_ERR_NONE && (heapLow < 0 || a < heapLow)) heapLow = a; }
    void A(const std::vector<cell>& v) { cell a = 0; std::vector<cell> t = v; if(t.empty()) t.push_back(0); if(amx_PushArray(amx, &a, nullptr, t.data(), (int)t.size()) == AMX_ERR_NONE && (heapLow < 0 || a < heapLow)) heapLow = a; }
    void Arg(const SampArg& g) { if(g.kind == 1) S(g.s); else if(g.kind == 2) A(g.a); else C(g.v); }
    void Done() { if(heapLow >= 0) amx_Release(amx, heapLow); heapLow = -1; }
};
static int g_SampErrs = 0;
static cell SampExec(SampScript* sc, int idx, bool* ran = nullptr)
{
    cell ret = 0; int e = amx_Exec(&sc->amx, &ret, idx);
    if(ran) *ran = e == AMX_ERR_NONE;
    if(e != AMX_ERR_NONE && e != AMX_ERR_SLEEP && g_SampErrs < 200)
    {
        ++g_SampErrs; char fn[64] = "?"; amx_GetPublic(&sc->amx, idx, fn);
        if(g_SampSrv) g_SampSrv->SampLog("[samp] %s: %s() stopped with error %d (%s)", sc->name.c_str(), fn, e, aux_StrError(e));
    }
    return ret;
}
static int FindPub(SampScript* sc, const char* name) { int idx = -1; if(amx_FindPublic(&sc->amx, name, &idx) != AMX_ERR_NONE) return -1; return idx; }

// call one public with typed args ("i"=cell, "f"=float, "s"=string). Returns false when it isn't there.
static bool CallPub(SampScript* sc, const char* name, cell* ret, const std::vector<SampArg>& args)
{
    if(!sc || !sc->ok) return false;
    int idx = FindPub(sc, name); if(idx < 0) return false;
    SampPush p(&sc->amx);
    for(int i = (int)args.size() - 1; i >= 0; --i) p.Arg(args[i]);
    bool ran = false; cell r = SampExec(sc, idx, &ran);
    p.Done();
    if(ret) *ret = r;
    return true;
}
static SampArg AI(cell v) { SampArg a; a.v = v; return a; }
static SampArg AF(float f) { SampArg a; a.v = amx_ftoc(f); return a; }
static SampArg AS(const std::string& s) { SampArg a; a.kind = 1; a.s = s; return a; }

// SA-MP: every script gets the callback; "stop" = the first script that returns stopOn ends it.
static cell CallAll(const char* name, const std::vector<SampArg>& args, int stopOn = -1, cell def = 1)
{
    if(!g_Samp) return def;
    cell last = def;
    for(auto& sc : g_Samp->scripts)
    {
        cell r = 0;
        if(!CallPub(sc.get(), name, &r, args)) continue;
        last = r;
        if(stopOn >= 0 && r == stopOn && !sc->gm) return r;
    }
    return last;
}

// read "format, args..." (CallLocalFunction, SetTimerEx, mysql_tquery) into SampArgs
static std::vector<SampArg> ReadArgs(AMX* amx, const cell* params, const std::string& fmt, int first)
{
    std::vector<SampArg> out; int n = NArgs(params), pi = first;
    for(size_t i = 0; i < fmt.size() && pi <= n; ++i)
    {
        char c = fmt[i]; SampArg g;
        switch(c)
        {
            case 's': case 'S': g.kind = 1; g.s = Str(amx, params[pi++]); break;
            case 'a': case 'A':
            {   // array + the next arg is its length
                cell* p = Addr(amx, params[pi++]);
                cell len = (i + 1 < fmt.size() && pi <= n) ? RefVal(amx, params[pi]) : 1;
                if(len < 0) len = 0; if(len > 65536) len = 65536;
                g.kind = 2; if(p) g.a.assign(p, p + len);
                break;
            }
            case 'p': case 'P': case '{': case '}': continue;
            default: g.v = RefVal(amx, params[pi++]); break;   // i d b c f x h l
        }
        out.push_back(std::move(g));
    }
    return out;
}

// ---- format() ---------------------------------------------------------------------------------
// %d %i %u %x %X %h %o %b %c %s %f %q %e (mysql: escaped string) %%, flags - 0 +, width, .precision
static std::string SampFormat(AMX* amx, const cell* params, const std::string& f, int first, bool sql = false)
{
    std::string out; int n = NArgs(params), pi = first;
    auto next = [&]() -> cell* { return pi <= n ? Addr(amx, params[pi++]) : nullptr; };
    for(size_t i = 0; i < f.size(); ++i)
    {
        char c = f[i];
        if(c != '%') { out += c; continue; }
        if(i + 1 >= f.size()) break;
        if(f[i + 1] == '%') { out += '%'; ++i; continue; }
        size_t j = i + 1; std::string spec = "%";
        while(j < f.size() && strchr("-+ 0#", f[j])) spec += f[j++];
        bool starW = false;
        if(j < f.size() && f[j] == '*') { starW = true; ++j; }
        while(j < f.size() && isdigit((unsigned char)f[j])) spec += f[j++];
        int prec = -1; bool starP = false;
        if(j < f.size() && f[j] == '.')
        {
            ++j; if(j < f.size() && f[j] == '*') { starP = true; ++j; }
            std::string pd; while(j < f.size() && isdigit((unsigned char)f[j])) pd += f[j++];
            if(!pd.empty()) prec = atoi(pd.c_str());
        }
        if(starW) { cell* p = next(); spec += std::to_string(p ? *p : 0); }
        if(starP) { cell* p = next(); prec = p ? (int)*p : 0; }
        if(j >= f.size()) break;
        char t = f[j]; i = j; char buf[512];
        switch(t)
        {
            case 'd': case 'i': { cell* p = next(); snprintf(buf, sizeof(buf), (spec + "d").c_str(), p ? (int)*p : 0); out += buf; break; }
            case 'u': { cell* p = next(); snprintf(buf, sizeof(buf), (spec + "u").c_str(), p ? (unsigned)*p : 0u); out += buf; break; }
            case 'x': case 'h': case 'X': case 'H': { cell* p = next(); snprintf(buf, sizeof(buf), (spec + (t == 'x' ? "x" : "X")).c_str(), p ? (unsigned)*p : 0u); out += buf; break; }
            case 'o': { cell* p = next(); snprintf(buf, sizeof(buf), (spec + "o").c_str(), p ? (unsigned)*p : 0u); out += buf; break; }
            case 'b':
            {
                cell* p = next(); unsigned v = p ? (unsigned)*p : 0u; std::string b;
                if(!v) b = "0"; while(v) { b.insert(b.begin(), (char)('0' + (v & 1))); v >>= 1; }
                snprintf(buf, sizeof(buf), (spec + "s").c_str(), b.c_str()); out += buf; break;
            }
            case 'c': { cell* p = next(); out += (char)(p ? *p : ' '); break; }
            case 'f': case 'F':
            {
                cell* p = next(); float v = 0; if(p) { cell c2 = *p; v = amx_ctof(c2); }
                std::string s2 = spec; if(prec >= 0) s2 += "." + std::to_string(prec); else s2 += ".6";
                snprintf(buf, sizeof(buf), (s2 + "f").c_str(), (double)v); out += buf; break;
            }
            case 's': case 'q': case 'e': case 'S':
            {
                cell* p = next(); std::string s;
                if(p) { int len = 0; amx_StrLen(p, &len); s.resize((size_t)len + 1); amx_GetString(&s[0], p, 0, (size_t)len + 1); s.resize(strlen(s.c_str())); }
                if(prec >= 0 && (int)s.size() > prec) s.resize((size_t)prec);
                if(t == 'e' || (sql && t == 'q')) { std::string e; for(char ch : s) { if(ch == '\'') e += "''"; else e += ch; } s = e; }
                snprintf(buf, sizeof(buf), (spec + "s").c_str(), "");
                if(spec.size() > 1) { char b2[4096]; snprintf(b2, sizeof(b2), (spec + "s").c_str(), s.c_str()); out += b2; }
                else out += s;
                break;
            }
            default: out += '%'; out += t; break;
        }
    }
    return out;
}

// ---- report of missing natives -----------------------------------------------------------------
static cell AMX_NATIVE_CALL n_Unknown(AMX* amx, const cell* params);
static int AMXAPI SampCallback(AMX* amx, cell index, cell* result, const cell* params)
{
    AMX_HEADER* hdr = (AMX_HEADER*)amx->base;
    AMX_FUNCSTUB* f = (AMX_FUNCSTUB*)(amx->base + (int)hdr->natives + (int)index * hdr->defsize);
    amx->error = AMX_ERR_NONE;
    if(f->address == 0)
    {
        SampScript* sc = ScOf(amx);
        if(sc && index >= 0 && index < (cell)sc->natives.size())
        {
            int& c = g_Samp->unknown[sc->natives[(size_t)index]];
            if(!c && g_SampSrv) g_SampSrv->SampLog("[samp] %s uses %s() - not in MP yet (returns 0)", sc->name.c_str(), sc->natives[(size_t)index].c_str());
            ++c; g_Samp->reportDirty = true;
        }
        *result = 0; return AMX_ERR_NONE;
    }
    AMX_NATIVE fn = amx_NtvGet((ucell)f->address);
    if(!fn) { *result = 0; return AMX_ERR_NONE; }
    *result = fn(amx, params);
    return amx->error;
}
static cell AMX_NATIVE_CALL n_Unknown(AMX*, const cell*) { return 0; }

} // namespace

// =================================================================================================
// natives
// =================================================================================================
#define N(name) static cell AMX_NATIVE_CALL n_##name(AMX* amx, const cell* params)
#define S_ (g_SampSrv)
#define UNUSED_AMX (void)amx; (void)params

namespace {
// ---- console / misc -----------------------------------------------------------------------------
N(print) { S_->SampLog("%s", Str(amx, params[1]).c_str()); return 0; }
N(printf) { S_->SampLog("%s", SampFormat(amx, params, Str(amx, params[1]), 2).c_str()); return 0; }
N(format)
{
    std::string s = SampFormat(amx, params, Str(amx, params[3]), 4);
    SetStr(amx, params[1], s, params[2]); return 1;
}
N(GetTickCount) { UNUSED_AMX; return (cell)(MP_NowMs() & 0x7FFFFFFF); }
N(tickcount) { if(NArgs(params) >= 1) SetRef(amx, params[1], 1000); return (cell)(MP_NowMs() & 0x7FFFFFFF); }
N(gettime)
{
    time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
    int n = NArgs(params);
    if(n >= 1) SetRef(amx, params[1], tm.tm_hour);
    if(n >= 2) SetRef(amx, params[2], tm.tm_min);
    if(n >= 3) SetRef(amx, params[3], tm.tm_sec);
    return (cell)t;
}
N(getdate)
{
    time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
    int n = NArgs(params);
    if(n >= 1) SetRef(amx, params[1], tm.tm_year + 1900);
    if(n >= 2) SetRef(amx, params[2], tm.tm_mon + 1);
    if(n >= 3) SetRef(amx, params[3], tm.tm_mday);
    return tm.tm_yday + 1;
}
N(GetMaxPlayers) { UNUSED_AMX; return g_Samp->maxPlayers; }
N(GetPlayerPoolSize) { UNUSED_AMX; int m = -1; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) m = i; return m; }
N(GetVehiclePoolSize) { UNUSED_AMX; int m = 0; for(size_t i = 1; i < g_Samp->veh.size(); ++i) if(g_Samp->veh[i].used) m = (int)i; return m; }
N(SetGameModeText) { g_Samp->modeText = Str(amx, params[1]); return 1; }
N(Ret1) { UNUSED_AMX; return 1; }
N(Ret0) { UNUSED_AMX; return 0; }
N(RetM1) { UNUSED_AMX; return -1; }
N(RetInvalid) { UNUSED_AMX; return 0xFFFF; }
N(RetF0) { UNUSED_AMX; return RF(0.0f); }

static std::string Cfg(const std::string& k) { auto it = g_Samp->cfg.find(Lower(k)); return it == g_Samp->cfg.end() ? std::string() : it->second; }
N(GetConsoleVarAsString) { std::string v = Cfg(Str(amx, params[1])); SetStr(amx, params[2], v, params[3]); return (cell)v.size(); }
N(GetConsoleVarAsInt) { return (cell)atoi(Cfg(Str(amx, params[1])).c_str()); }
N(GetConsoleVarAsBool) { return atoi(Cfg(Str(amx, params[1])).c_str()) != 0; }
N(SendRconCommand)
{
    std::string c = Str(amx, params[1]);
    S_->SampLog("[samp] rcon: %s", c.c_str());
    size_t sp = c.find(' '); std::string k = Lower(c.substr(0, sp)), v = sp == std::string::npos ? "" : c.substr(sp + 1);
    if(!v.empty()) g_Samp->cfg[k] = v;
    if(k == "hostname") g_Samp->hostName = v;
    return 1;
}
N(CallLocalFunction)
{
    SampScript* sc = ScOf(amx); std::string fn = Str(amx, params[1]), f = Str(amx, params[2]);
    std::vector<SampArg> a = ReadArgs(amx, params, f, 3);
    cell r = 0; if(!CallPub(sc, fn.c_str(), &r, a)) return 0;
    return r;
}
N(CallRemoteFunction)
{
    std::string fn = Str(amx, params[1]), f = Str(amx, params[2]);
    std::vector<SampArg> a = ReadArgs(amx, params, f, 3);
    cell last = 0;
    for(auto& sc : g_Samp->scripts) { cell r = 0; if(CallPub(sc.get(), fn.c_str(), &r, a)) last = r; }
    return last;
}
N(VectorSize) { float x = PF(params, 1), y = PF(params, 2), z = PF(params, 3); return RF(sqrtf(x * x + y * y + z * z)); }
N(asin) { return RF(asinf(PF(params, 1)) * 57.29578f); }
N(acos) { return RF(acosf(PF(params, 1)) * 57.29578f); }
N(atan) { return RF(atanf(PF(params, 1)) * 57.29578f); }
N(atan2) { return RF(atan2f(PF(params, 1), PF(params, 2)) * 57.29578f); }
N(GetPlayerVersion) { SetStr(amx, params[2], "0.3.7-MP", params[3]); return 1; }
N(gpci) { if(!PlOk(params[1])) return 0; SetStr(amx, params[2], S_->SampUid((int)params[1]), params[3]); return 1; }
N(GetPlayerIp) { if(!PlOk(params[1])) return 0; SetStr(amx, params[2], S_->SampIp((int)params[1]), params[3]); return 1; }
N(GetServerTickRate) { UNUSED_AMX; return 50; }
N(GetWeaponName)
{
    static const char* w[] = { "Fist","Brass Knuckles","Golf Club","Nite Stick","Knife","Baseball Bat","Shovel","Pool Cue","Katana","Chainsaw","Purple Dildo","Dildo","Vibrator","Silver Vibrator","Flowers","Cane","Grenade","Tear Gas","Molotov Cocktail","","","","Colt 45","Silenced 9mm","Desert Eagle","Shotgun","Sawnoff Shotgun","Combat Shotgun","Micro SMG/Uzi","MP5","AK-47","M4","Tec-9","Country Rifle","Sniper Rifle","RPG","HS Rocket","Flamethrower","Minigun","Satchel Charge","Detonator","Spraycan","Fire Extinguisher","Camera","Night Vis Goggles","Thermal Goggles","Parachute" };
    cell id = params[1]; SetStr(amx, params[2], id >= 0 && id < (cell)(sizeof(w) / sizeof(w[0])) ? w[id] : "", params[3]); return 1;
}

// ---- timers -------------------------------------------------------------------------------------
N(SetTimer)
{
    SampTimer t; t.id = g_Samp->nextTimer++; t.sc = ScOf(amx); t.fn = Str(amx, params[1]);
    t.interval = (uint32_t)std::max<cell>(1, params[2]); t.repeat = params[3] != 0; t.next = MP_NowMs() + t.interval;
    g_Samp->timers.push_back(std::move(t)); return g_Samp->nextTimer - 1;
}
N(SetTimerEx)
{
    SampTimer t; t.id = g_Samp->nextTimer++; t.sc = ScOf(amx); t.fn = Str(amx, params[1]);
    t.interval = (uint32_t)std::max<cell>(1, params[2]); t.repeat = params[3] != 0; t.next = MP_NowMs() + t.interval;
    t.args = ReadArgs(amx, params, Str(amx, params[4]), 5);
    g_Samp->timers.push_back(std::move(t)); return g_Samp->nextTimer - 1;
}
N(KillTimer) { for(auto& t : g_Samp->timers) if(t.id == params[1]) t.dead = true; return 1; }
N(IsValidTimer) { for(auto& t : g_Samp->timers) if(t.id == params[1] && !t.dead) return 1; return 0; }

// ---- PVars / SVars ------------------------------------------------------------------------------
static std::map<std::string, SampVar>* PV(cell id) { return PlOk(id) ? &g_SampPl[id].pvars : nullptr; }
N(SetPVarInt) { auto* m = PV(params[1]); if(!m) return 0; SampVar& v = (*m)[Lower(Str(amx, params[2]))]; v = SampVar(); v.type = 1; v.i = params[3]; return 1; }
N(GetPVarInt) { auto* m = PV(params[1]); if(!m) return 0; auto it = m->find(Lower(Str(amx, params[2]))); return it != m->end() && it->second.type == 1 ? it->second.i : 0; }
N(SetPVarFloat) { auto* m = PV(params[1]); if(!m) return 0; SampVar& v = (*m)[Lower(Str(amx, params[2]))]; v = SampVar(); v.type = 3; v.f = PF(params, 3); return 1; }
N(GetPVarFloat) { auto* m = PV(params[1]); if(!m) return RF(0); auto it = m->find(Lower(Str(amx, params[2]))); return RF(it != m->end() && it->second.type == 3 ? it->second.f : 0.0f); }
N(SetPVarString) { auto* m = PV(params[1]); if(!m) return 0; SampVar& v = (*m)[Lower(Str(amx, params[2]))]; v = SampVar(); v.type = 2; v.s = Str(amx, params[3]); return 1; }
N(GetPVarString) { auto* m = PV(params[1]); if(!m) return 0; auto it = m->find(Lower(Str(amx, params[2]))); std::string s = it != m->end() && it->second.type == 2 ? it->second.s : ""; SetStr(amx, params[3], s, params[4]); return (cell)s.size(); }
N(DeletePVar) { auto* m = PV(params[1]); if(!m) return 0; return m->erase(Lower(Str(amx, params[2]))) ? 1 : 0; }
N(GetPVarType) { auto* m = PV(params[1]); if(!m) return 0; auto it = m->find(Lower(Str(amx, params[2]))); return it == m->end() ? 0 : it->second.type; }
N(GetPVarsUpperIndex) { auto* m = PV(params[1]); return m ? (cell)m->size() : 0; }
N(GetPVarNameAtIndex) { auto* m = PV(params[1]); if(!m || params[2] < 0 || params[2] >= (cell)m->size()) return 0; auto it = m->begin(); std::advance(it, params[2]); SetStr(amx, params[3], it->first, params[4]); return 1; }
N(SetSVarInt) { SampVar& v = g_Samp->svars[Lower(Str(amx, params[1]))]; v = SampVar(); v.type = 1; v.i = params[2]; return 1; }
N(GetSVarInt) { auto it = g_Samp->svars.find(Lower(Str(amx, params[1]))); return it != g_Samp->svars.end() && it->second.type == 1 ? it->second.i : 0; }
N(SetSVarFloat) { SampVar& v = g_Samp->svars[Lower(Str(amx, params[1]))]; v = SampVar(); v.type = 3; v.f = PF(params, 2); return 1; }
N(GetSVarFloat) { auto it = g_Samp->svars.find(Lower(Str(amx, params[1]))); return RF(it != g_Samp->svars.end() && it->second.type == 3 ? it->second.f : 0.0f); }
N(SetSVarString) { SampVar& v = g_Samp->svars[Lower(Str(amx, params[1]))]; v = SampVar(); v.type = 2; v.s = Str(amx, params[2]); return 1; }
N(GetSVarString) { auto it = g_Samp->svars.find(Lower(Str(amx, params[1]))); std::string s = it != g_Samp->svars.end() && it->second.type == 2 ? it->second.s : ""; SetStr(amx, params[2], s, params[3]); return (cell)s.size(); }
N(DeleteSVar) { return g_Samp->svars.erase(Lower(Str(amx, params[1]))) ? 1 : 0; }
N(GetSVarType) { auto it = g_Samp->svars.find(Lower(Str(amx, params[1]))); return it == g_Samp->svars.end() ? 0 : it->second.type; }

// ---- files (sandboxed to scriptfiles/) ----------------------------------------------------------
static std::string SfPath(const std::string& name)
{
    std::string n = name; for(auto& c : n) if(c == '\\') c = '/';
    while(!n.empty() && n[0] == '/') n.erase(0, 1);
    if(n.find("..") != std::string::npos) return std::string();
    return g_Samp->root + "/scriptfiles/" + n;
}
static void MkDirs(const std::string& path) { for(size_t i = 1; i < path.size(); ++i) if(path[i] == '/') { std::string d = path.substr(0, i); mkdir(d.c_str(), 0755); } }
static FILE* FH(cell h) { return h > 0 && h <= (cell)g_Samp->files.size() ? g_Samp->files[(size_t)h - 1] : nullptr; }
N(fopen)
{
    std::string p = SfPath(Str(amx, params[1])); if(p.empty()) return 0;
    int mode = NArgs(params) >= 2 ? (int)params[2] : 2;
    const char* m = mode == 0 ? "rb" : mode == 1 ? "wb" : mode == 3 ? "ab" : "r+b";
    if(mode != 0) MkDirs(p);
    FILE* f = fopen(p.c_str(), m);
    if(!f && mode == 2) f = fopen(p.c_str(), "w+b");
    if(!f) return 0;
    for(size_t i = 0; i < g_Samp->files.size(); ++i) if(!g_Samp->files[i]) { g_Samp->files[i] = f; return (cell)i + 1; }
    g_Samp->files.push_back(f); return (cell)g_Samp->files.size();
}
N(fclose) { FILE* f = FH(params[1]); if(!f) return 0; fclose(f); g_Samp->files[(size_t)params[1] - 1] = nullptr; return 1; }
N(fwrite) { FILE* f = FH(params[1]); if(!f) return 0; std::string s = Str(amx, params[2]); fwrite(s.data(), 1, s.size(), f); return (cell)s.size(); }
N(fread)
{
    FILE* f = FH(params[1]); if(!f) return 0;
    cell size = NArgs(params) >= 3 ? params[3] : 256; if(size <= 0) return 0;
    std::string s; int c;
    while((int)s.size() < size - 1 && (c = fgetc(f)) != EOF) { s += (char)c; if(c == '\n') break; }
    SetStr(amx, params[2], s, size); return (cell)s.size();
}
N(fputchar) { FILE* f = FH(params[1]); if(!f) return 0; fputc((int)params[2], f); return 1; }
N(fgetchar) { FILE* f = FH(params[1]); if(!f) return -1; return fgetc(f); }
N(fblockwrite) { FILE* f = FH(params[1]); cell* p = Addr(amx, params[2]); if(!f || !p) return 0; return (cell)fwrite(p, sizeof(cell), (size_t)params[3], f); }
N(fblockread) { FILE* f = FH(params[1]); cell* p = Addr(amx, params[2]); if(!f || !p) return 0; return (cell)fread(p, sizeof(cell), (size_t)params[3], f); }
N(fseek) { FILE* f = FH(params[1]); if(!f) return 0; int wh = NArgs(params) >= 3 ? (int)params[3] : 0; fseek(f, (long)params[2], wh == 1 ? SEEK_CUR : wh == 2 ? SEEK_END : SEEK_SET); return (cell)ftell(f); }
N(flength) { FILE* f = FH(params[1]); if(!f) return 0; long c = ftell(f); fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, c, SEEK_SET); return (cell)n; }
N(fexist) { std::string p = SfPath(Str(amx, params[1])); struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0; }
N(fremove) { std::string p = SfPath(Str(amx, params[1])); return !p.empty() && remove(p.c_str()) == 0; }
N(frename) { std::string a = SfPath(Str(amx, params[1])), b = SfPath(Str(amx, params[2])); return !a.empty() && !b.empty() && rename(a.c_str(), b.c_str()) == 0; }
N(ftemp) { UNUSED_AMX; FILE* f = tmpfile(); if(!f) return 0; g_Samp->files.push_back(f); return (cell)g_Samp->files.size(); }
N(dir_exists) { std::string p = SfPath(Str(amx, params[1])); struct stat st; return !p.empty() && stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
N(dir_create) { std::string p = SfPath(Str(amx, params[1])); if(p.empty()) return 0; MkDirs(p + "/"); return 1; }

// ---- players --------------------------------------------------------------------------------------
N(IsPlayerConnected) { return PlOk(params[1]); }
N(IsPlayerNPC) { UNUSED_AMX; return 0; }
N(IsPlayerAdmin) { return PlOk(params[1]) && S_->SampIsAdmin((int)params[1]); }
N(GetPlayerName) { if(!PlOk(params[1])) { SetStr(amx, params[2], "", params[3]); return 0; } const std::string& n = g_SampPl[params[1]].name; SetStr(amx, params[2], n, params[3]); return (cell)n.size(); }
N(SetPlayerName) { if(!PlOk(params[1])) return 0; std::string n = Str(amx, params[2]); if(n.empty() || n.size() >= MP_NAME_LEN) return -1; g_SampPl[params[1]].name = n; return 1; }
N(GetPlayerPing) { return PlOk(params[1]) ? 60 : 0; }
N(GetPlayerState) { return PlOk(params[1]) ? g_SampPl[params[1]].state : 0; }
N(GetPlayerPos)
{
    if(!PlOk(params[1])) return 0; float p[3]; S_->SampGetPos((int)params[1], p);
    SetRefF(amx, params[2], p[0]); SetRefF(amx, params[3], p[1]); SetRefF(amx, params[4], p[2]); return 1;
}
N(SetPlayerPos)
{
    if(!PlOk(params[1])) return 0; float p[3] = { PF(params, 2), PF(params, 3), PF(params, 4) };
    S_->SampTele((int)params[1], p, g_SampPl[params[1]].a); return 1;
}
N(SetPlayerPosFindZ) { return n_SetPlayerPos(amx, params); }
N(GetPlayerFacingAngle) { if(!PlOk(params[1])) return 0; SetRefF(amx, params[2], g_SampPl[params[1]].a); return 1; }
N(SetPlayerFacingAngle) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].a = PF(params, 2); float p[3]; S_->SampGetPos((int)params[1], p); S_->SampTele((int)params[1], p, PF(params, 2)); return 1; }
N(IsPlayerInRangeOfPoint)
{
    if(!PlOk(params[1])) return 0; float p[3]; S_->SampGetPos((int)params[1], p);
    float r = PF(params, 2), dx = p[0] - PF(params, 3), dy = p[1] - PF(params, 4), dz = p[2] - PF(params, 5);
    return dx * dx + dy * dy + dz * dz <= r * r;
}
N(GetPlayerDistanceFromPoint)
{
    if(!PlOk(params[1])) return RF(0); float p[3]; S_->SampGetPos((int)params[1], p);
    float dx = p[0] - PF(params, 2), dy = p[1] - PF(params, 3), dz = p[2] - PF(params, 4);
    return RF(sqrtf(dx * dx + dy * dy + dz * dz));
}
N(GetPlayerHealth) { if(!PlOk(params[1])) return 0; SetRefF(amx, params[2], g_SampPl[params[1]].hp); return 1; }
N(SetPlayerHealth) { if(!PlOk(params[1])) return 0; float h = PF(params, 2); g_SampPl[params[1]].hp = h; S_->SampAct((int)params[1], CGA_HEALTH, 0, 0, (int32_t)h); return 1; }
N(GetPlayerArmour) { if(!PlOk(params[1])) return 0; SetRefF(amx, params[2], g_SampPl[params[1]].ar); return 1; }
N(SetPlayerArmour) { if(!PlOk(params[1])) return 0; float h = PF(params, 2); g_SampPl[params[1]].ar = h; S_->SampAct((int)params[1], CGA_ARMOUR, 0, 0, (int32_t)h); return 1; }
N(GetPlayerMoney) { return PlOk(params[1]) ? g_SampPl[params[1]].money : 0; }
N(GivePlayerMoney) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].money += params[2]; S_->SampAct((int)params[1], CGA_SETMONEY, 0, 0, g_SampPl[params[1]].money); return 1; }
N(ResetPlayerMoney) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].money = 0; S_->SampAct((int)params[1], CGA_SETMONEY, 0, 0, 0); return 1; }
N(GetPlayerScore) { return PlOk(params[1]) ? g_SampPl[params[1]].score : 0; }
N(SetPlayerScore) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].score = params[2]; return 1; }
N(GetPlayerSkin) { return PlOk(params[1]) ? g_SampPl[params[1]].skin : 0; }
N(SetPlayerSkin) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].skin = params[2]; S_->SampAct((int)params[1], CGA_SKIN, 0, 0, params[2]); return 1; }
N(GetPlayerInterior) { return PlOk(params[1]) ? g_SampPl[params[1]].interior : 0; }
N(SetPlayerInterior) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].interior = params[2]; S_->SampWorld((int)params[1]); return 1; }
N(GetPlayerVirtualWorld) { return PlOk(params[1]) ? g_SampPl[params[1]].world : 0; }
N(SetPlayerVirtualWorld) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].world = params[2]; S_->SampWorld((int)params[1]); return 1; }
N(GetPlayerTeam) { return PlOk(params[1]) ? g_SampPl[params[1]].team : 255; }
N(SetPlayerTeam) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].team = params[2]; return 1; }
N(GetPlayerColor) { return PlOk(params[1]) ? (cell)g_SampPl[params[1]].color : 0; }
N(SetPlayerColor) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].color = (uint32_t)params[2]; return 1; }
N(GetPlayerWantedLevel) { return PlOk(params[1]) ? g_SampPl[params[1]].wanted : 0; }
N(SetPlayerWantedLevel) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].wanted = params[2]; S_->SampAct((int)params[1], CGA_WANTED, 0, 0, params[2]); return 1; }
N(GetPlayerWeapon) { return PlOk(params[1]) ? g_SampPl[params[1]].weapon : 0; }
N(GetPlayerAmmo) { return PlOk(params[1]) ? g_SampPl[params[1]].ammo : 0; }
N(GivePlayerWeapon) { if(!PlOk(params[1])) return 0; S_->SampAct((int)params[1], CGA_GUN, (int)params[2], 0, params[3]); return 1; }
N(ResetPlayerWeapons) { if(!PlOk(params[1])) return 0; S_->SampAct((int)params[1], CGA_DISARM); return 1; }
N(GetPlayerWeaponData) { if(!PlOk(params[1])) return 0; SetRef(amx, params[3], 0); SetRef(amx, params[4], 0); return 1; }
N(GetPlayerDrunkLevel) { return PlOk(params[1]) ? g_SampPl[params[1]].drunk : 0; }
N(SetPlayerDrunkLevel) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].drunk = params[2]; return 1; }
N(GetPlayerSpecialAction) { return PlOk(params[1]) ? g_SampPl[params[1]].special : 0; }
N(SetPlayerSpecialAction) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].special = params[2]; return 1; }
N(GetPlayerFightingStyle) { return PlOk(params[1]) ? g_SampPl[params[1]].fightStyle : 4; }
N(SetPlayerFightingStyle) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].fightStyle = params[2]; S_->SampAct((int)params[1], CGA_FIGHT, 0, 0, params[2]); return 1; }
N(SetPlayerWeather) { if(!PlOk(params[1])) return 0; S_->SampAct((int)params[1], CGA_WEATHER, 0, 0, params[2]); return 1; }
N(SetWeather) { for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampAct(i, CGA_WEATHER, 0, 0, params[1]); return 1; }
N(SetPlayerTime) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_TIME, (uint8_t)params[2], (uint8_t)params[3], -1); return 1; }
N(SetWorldTime) { for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampOp(i, SPO_TIME, (uint8_t)params[1], 0, -1); return 1; }
N(TogglePlayerControllable) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].controllable = params[2] != 0; S_->SampAct((int)params[1], CGA_FREEZE, params[2] ? 0 : 1); return 1; }
N(TogglePlayerSpectating)
{
    if(!PlOk(params[1])) return 0; SampPl& p = g_SampPl[params[1]];
    p.spectating = params[2] != 0;
    S_->SampAct((int)params[1], CGA_FREEZE, p.spectating ? 1 : 0);
    if(!p.spectating) S_->SampSpawn((int)params[1]);   // SA-MP: spectating off = the player spawns
    else p.state = 9;
    return 1;
}
N(SpawnPlayer) { if(!PlOk(params[1])) return 0; S_->SampSpawn((int)params[1]); return 1; }
N(SetSpawnInfo)
{
    if(!PlOk(params[1])) return 0; auto& s = g_SampPl[params[1]].spawn;
    s.team = params[2]; s.skin = params[3]; s.x = PF(params, 4); s.y = PF(params, 5); s.z = PF(params, 6); s.a = PF(params, 7);
    for(int i = 0; i < 3 && 8 + i * 2 + 1 <= NArgs(params); ++i) { s.w[i] = params[8 + i * 2]; s.am[i] = params[9 + i * 2]; }
    s.set = true; return 1;
}
N(Kick) { if(!PlOk(params[1])) return 0; S_->SampKick((int)params[1], false); return 1; }
N(Ban) { if(!PlOk(params[1])) return 0; S_->SampKick((int)params[1], true); return 1; }
N(BanEx) { if(!PlOk(params[1])) return 0; S_->SampKick((int)params[1], true); return 1; }
N(SendClientMessage) { if(!PlOk(params[1])) return 0; S_->SampMsg((int)params[1], (uint32_t)params[2], Str(amx, params[3])); return 1; }
N(SendClientMessageToAll) { std::string s = Str(amx, params[2]); for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampMsg(i, (uint32_t)params[1], s); return 1; }
N(SendPlayerMessageToPlayer) { if(!PlOk(params[1]) || !PlOk(params[2])) return 0; S_->SampMsg((int)params[1], g_SampPl[params[2]].color, g_SampPl[params[2]].name + ": " + Str(amx, params[3])); return 1; }
N(SendPlayerMessageToAll) { if(!PlOk(params[1])) return 0; std::string s = g_SampPl[params[1]].name + ": " + Str(amx, params[2]); for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampMsg(i, g_SampPl[params[1]].color, s); return 1; }
N(SendDeathMessage) { UNUSED_AMX; return 1; }
N(GameTextForPlayer) { if(!PlOk(params[1])) return 0; S_->SampGameText((int)params[1], Str(amx, params[2]), (int)params[3], (int)params[4]); return 1; }
N(GameTextForAll) { std::string s = Str(amx, params[1]); for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampGameText(i, s, (int)params[2], (int)params[3]); return 1; }
N(ShowPlayerDialog)
{
    if(!PlOk(params[1])) return 0;
    S_->SampDialog((int)params[1], (int)params[2], (int)params[3], Str(amx, params[4]), Str(amx, params[5]), Str(amx, params[6]), Str(amx, params[7]));
    return 1;
}
N(GetPlayerDialogID) { return PlOk(params[1]) ? g_SampPl[params[1]].dialog : -1; }
N(ApplyAnimation)
{
    if(!PlOk(params[1])) return 0;
    std::string t = Str(amx, params[3]) + " " + Str(amx, params[2]);
    S_->SampAct((int)params[1], CGA_ANIM, 0, 0, 0, nullptr, 0, t.c_str()); return 1;
}
N(ClearAnimations) { if(!PlOk(params[1])) return 0; S_->SampAct((int)params[1], CGA_ANIM, 0, 0, 0, nullptr, 0, ""); return 1; }
N(GetPlayerAnimationIndex) { UNUSED_AMX; return 0; }
N(PlayerPlaySound) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 3), PF(params, 4), PF(params, 5) }; S_->SampOp((int)params[1], SPO_SOUND, 0, 0, params[2], f); return 1; }
N(CreateExplosion) { float f[4] = { PF(params, 1), PF(params, 2), PF(params, 3) }; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampAct(i, CGA_EXPLODE, (int)params[4], 0, 0, f); return 1; }
N(CreateExplosionForPlayer) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 2), PF(params, 3), PF(params, 4) }; S_->SampAct((int)params[1], CGA_EXPLODE, (int)params[5], 0, 0, f); return 1; }
N(SetPlayerCheckpoint)
{
    if(!PlOk(params[1])) return 0; SampPl& p = g_SampPl[params[1]];
    p.cp = true; p.inCp = false; p.cpPos[0] = PF(params, 2); p.cpPos[1] = PF(params, 3); p.cpPos[2] = PF(params, 4); p.cpPos[3] = PF(params, 5);
    S_->SampOp((int)params[1], SPO_CHECKPOINT, 1, 0, 0, p.cpPos); S_->SampAct((int)params[1], CGA_GPS, 0, 0, 0, p.cpPos); return 1;
}
N(DisablePlayerCheckpoint) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].cp = false; float z[4] = {0}; S_->SampOp((int)params[1], SPO_CHECKPOINT, 0, 0, 0, z); S_->SampAct((int)params[1], CGA_GPS, 0, 0, 0, z); return 1; }
N(SetPlayerRaceCheckpoint)
{
    if(!PlOk(params[1])) return 0; SampPl& p = g_SampPl[params[1]];
    p.rcp = true; p.inRcp = false; p.rcpPos[0] = PF(params, 3); p.rcpPos[1] = PF(params, 4); p.rcpPos[2] = PF(params, 5); p.rcpPos[3] = PF(params, 9);
    S_->SampOp((int)params[1], SPO_CHECKPOINT, 1, 1, 0, p.rcpPos); return 1;
}
N(DisablePlayerRaceCheckpoint) { if(!PlOk(params[1])) return 0; g_SampPl[params[1]].rcp = false; float z[4] = {0}; S_->SampOp((int)params[1], SPO_CHECKPOINT, 0, 1, 0, z); return 1; }
N(IsPlayerInCheckpoint) { return PlOk(params[1]) && g_SampPl[params[1]].inCp; }
N(IsPlayerInRaceCheckpoint) { return PlOk(params[1]) && g_SampPl[params[1]].inRcp; }
N(SetPlayerMapIcon)
{
    if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 3), PF(params, 4), PF(params, 5) };
    S_->SampOp((int)params[1], SPO_MAPICON, (uint8_t)params[2], (uint8_t)params[6], params[7], f); return 1;
}
N(RemovePlayerMapIcon) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_MAPICON, (uint8_t)params[2], 0, 0); return 1; }
N(RemoveBuildingForPlayer)
{
    if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6) };
    g_SampPl[params[1]].removed.push_back({ (float)params[2], f[0], f[1], f[2], f[3] });
    S_->SampOp((int)params[1], SPO_REMOVEBLD, 0, 0, params[2], f); return 1;
}
N(SetPlayerCameraPos) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 2), PF(params, 3), PF(params, 4) }; S_->SampOp((int)params[1], SPO_CAMERA, 1, 0, 0, f); return 1; }
N(SetPlayerCameraLookAt) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 2), PF(params, 3), PF(params, 4) }; S_->SampOp((int)params[1], SPO_CAMERA, 2, 0, 0, f); return 1; }
N(SetCameraBehindPlayer) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_CAMERA, 0, 0, 0); return 1; }
N(InterpolateCameraPos) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 5), PF(params, 6), PF(params, 7) }; S_->SampOp((int)params[1], SPO_CAMERA, 1, 0, 0, f); return 1; }
N(InterpolateCameraLookAt) { if(!PlOk(params[1])) return 0; float f[4] = { PF(params, 5), PF(params, 6), PF(params, 7) }; S_->SampOp((int)params[1], SPO_CAMERA, 2, 0, 0, f); return 1; }
N(GetPlayerCameraPos) { if(!PlOk(params[1])) return 0; float p[3]; S_->SampGetPos((int)params[1], p); SetRefF(amx, params[2], p[0]); SetRefF(amx, params[3], p[1]); SetRefF(amx, params[4], p[2] + 1.0f); return 1; }
N(GetPlayerCameraFrontVector) { if(!PlOk(params[1])) return 0; float a = g_SampPl[params[1]].a * 0.0174533f; SetRefF(amx, params[2], -sinf(a)); SetRefF(amx, params[3], cosf(a)); SetRefF(amx, params[4], 0); return 1; }
N(GetPlayerVelocity) { if(!PlOk(params[1])) return 0; SetRefF(amx, params[2], 0); SetRefF(amx, params[3], 0); SetRefF(amx, params[4], 0); return 1; }
N(GetPlayerKeys) { if(!PlOk(params[1])) return 0; SetRef(amx, params[2], g_SampPl[params[1]].keys); SetRef(amx, params[3], 0); SetRef(amx, params[4], 0); return 1; }
N(IsPlayerInAnyVehicle) { return PlOk(params[1]) && (g_SampPl[params[1]].state == 2 || g_SampPl[params[1]].state == 3); }
N(GetPlayerVehicleID) { return PlOk(params[1]) ? g_SampPl[params[1]].vehicle : 0; }
N(GetPlayerVehicleSeat) { return PlOk(params[1]) && g_SampPl[params[1]].state == 2 ? 0 : -1; }
N(IsPlayerInVehicle) { return PlOk(params[1]) && g_SampPl[params[1]].vehicle == params[2] && params[2]; }
N(RemovePlayerFromVehicle) { if(!PlOk(params[1])) return 0; S_->SampAct((int)params[1], CGA_EJECT); return 1; }
N(PutPlayerInVehicle)
{
    if(!PlOk(params[1]) || params[2] <= 0 || params[2] >= (cell)g_Samp->veh.size() || !g_Samp->veh[params[2]].used) return 0;
    const SampVeh& v = g_Samp->veh[params[2]]; float p[3] = { v.x, v.y, v.z };
    S_->SampTele((int)params[1], p, v.a); S_->SampAct((int)params[1], CGA_SPAWNCAR, 0, 0, v.model);
    g_SampPl[params[1]].vehicle = (int)params[2]; return 1;
}
N(SetPlayerChatBubble) { UNUSED_AMX; return 1; }
N(SetPlayerAttachedObject)
{
    if(!PlOk(params[1])) return 0; float f[4] = { NArgs(params) >= 5 ? PF(params, 5) : 0, NArgs(params) >= 6 ? PF(params, 6) : 0, NArgs(params) >= 7 ? PF(params, 7) : 0, 0 };
    char t[200]; snprintf(t, sizeof(t), "%.2f %.2f %.2f %.2f %.2f %.2f", NArgs(params) >= 8 ? PF(params, 8) : 0.f, NArgs(params) >= 9 ? PF(params, 9) : 0.f, NArgs(params) >= 10 ? PF(params, 10) : 0.f, NArgs(params) >= 11 ? PF(params, 11) : 1.f, NArgs(params) >= 12 ? PF(params, 12) : 1.f, NArgs(params) >= 13 ? PF(params, 13) : 1.f);
    S_->SampOp((int)params[1], SPO_ATTACH, (uint8_t)params[2], (uint8_t)(NArgs(params) >= 4 ? params[4] : 1), params[3], f, t); return 1;
}
N(RemovePlayerAttachedObject) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_ATTACH, (uint8_t)params[2], 0, -1); return 1; }
N(IsPlayerAttachedObjectSlotUsed) { UNUSED_AMX; return 0; }
N(SetPlayerSkillLevel) { UNUSED_AMX; return 1; }
N(GetPlayerTargetPlayer) { UNUSED_AMX; return 0xFFFF; }
N(GetPlayerSurfingVehicleID) { UNUSED_AMX; return 0xFFFF; }
N(NetStats_GetConnectedTime) { if(!PlOk(params[1])) return 0; return (cell)S_->SampOnlineMs((int)params[1]); }
N(SelectTextDraw) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_TDSELECT, 1, 0, params[2]); return 1; }
N(CancelSelectTextDraw) { if(!PlOk(params[1])) return 0; S_->SampOp((int)params[1], SPO_TDSELECT, 0, 0, 0); return 1; }

// ---- vehicles (kept on the server: model / position for the script; MP phones spawn their own) ----
static int VehNew(int model, float x, float y, float z, float a, int c1, int c2)
{
    for(size_t i = 1; i < g_Samp->veh.size(); ++i) if(!g_Samp->veh[i].used)
    {
        SampVeh& v = g_Samp->veh[i]; v = SampVeh(); v.used = true; v.model = model; v.x = x; v.y = y; v.z = z; v.a = a; v.c1 = c1; v.c2 = c2;
        return (int)i;
    }
    return 0xFFFF;
}
static SampVeh* VH(cell id) { return id > 0 && id < (cell)g_Samp->veh.size() && g_Samp->veh[id].used ? &g_Samp->veh[id] : nullptr; }
N(CreateVehicle) { return VehNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), (int)params[6], (int)params[7]); }
N(DestroyVehicle) { SampVeh* v = VH(params[1]); if(!v) return 0; v->used = false; return 1; }
N(IsValidVehicle) { return VH(params[1]) != nullptr; }
N(GetVehicleModel) { SampVeh* v = VH(params[1]); return v ? v->model : 0; }
N(GetVehiclePos) { SampVeh* v = VH(params[1]); if(!v) return 0; SetRefF(amx, params[2], v->x); SetRefF(amx, params[3], v->y); SetRefF(amx, params[4], v->z); return 1; }
N(SetVehiclePos) { SampVeh* v = VH(params[1]); if(!v) return 0; v->x = PF(params, 2); v->y = PF(params, 3); v->z = PF(params, 4); return 1; }
N(GetVehicleZAngle) { SampVeh* v = VH(params[1]); if(!v) return 0; SetRefF(amx, params[2], v->a); return 1; }
N(SetVehicleZAngle) { SampVeh* v = VH(params[1]); if(!v) return 0; v->a = PF(params, 2); return 1; }
N(GetVehicleHealth) { SampVeh* v = VH(params[1]); if(!v) return 0; SetRefF(amx, params[2], v->hp); return 1; }
N(SetVehicleHealth) { SampVeh* v = VH(params[1]); if(!v) return 0; v->hp = PF(params, 2); return 1; }
N(GetVehicleVirtualWorld) { SampVeh* v = VH(params[1]); return v ? v->world : 0; }
N(SetVehicleVirtualWorld) { SampVeh* v = VH(params[1]); if(!v) return 0; v->world = params[2]; return 1; }
N(GetVehicleDistanceFromPoint) { SampVeh* v = VH(params[1]); if(!v) return RF(0); float dx = v->x - PF(params, 2), dy = v->y - PF(params, 3), dz = v->z - PF(params, 4); return RF(sqrtf(dx * dx + dy * dy + dz * dz)); }
N(GetVehicleParamsEx) { for(int i = 2; i <= NArgs(params); ++i) SetRef(amx, params[i], 0); return VH(params[1]) != nullptr; }
N(GetVehicleVelocity) { for(int i = 2; i <= 4; ++i) SetRefF(amx, params[i], 0); return VH(params[1]) != nullptr; }

// ---- objects -> MP dynamic mapping (the PKT_DZ_MAP rows every phone streams) ----------------------
N(CreateObject) { return S_->SampObjNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), PF(params, 7), -1, -1); }
N(CreatePlayerObject) { return S_->SampObjNew((int)params[2], PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), PF(params, 7), PF(params, 8), -1, -1); }
N(CreateDynamicObject)
{
    int n = NArgs(params);
    return S_->SampObjNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), PF(params, 7), n >= 8 ? (int)params[8] : -1, n >= 9 ? (int)params[9] : -1);
}
N(CreateDynamicObjectEx) { return S_->SampObjNew((int)params[1], PF(params, 2), PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), PF(params, 7), -1, -1); }
N(DestroyObject) { return S_->SampObjDel((int)params[1]); }
N(DestroyPlayerObject) { return S_->SampObjDel((int)params[2]); }
N(IsValidObject) { return g_Samp->obj.count((int)params[1]) != 0; }
N(IsValidPlayerObject) { return g_Samp->obj.count((int)params[2]) != 0; }
N(SetObjectPos) { auto it = g_Samp->obj.find((int)params[1]); if(it == g_Samp->obj.end()) return 0; float p[3] = { PF(params, 2), PF(params, 3), PF(params, 4) }; S_->SampObjMove((int)params[1], p, it->second.r); return 1; }
N(SetObjectRot) { auto it = g_Samp->obj.find((int)params[1]); if(it == g_Samp->obj.end()) return 0; float r[3] = { PF(params, 2), PF(params, 3), PF(params, 4) }; S_->SampObjMove((int)params[1], it->second.p, r); return 1; }
N(GetObjectPos) { auto it = g_Samp->obj.find((int)params[1]); if(it == g_Samp->obj.end()) return 0; SetRefF(amx, params[2], it->second.p[0]); SetRefF(amx, params[3], it->second.p[1]); SetRefF(amx, params[4], it->second.p[2]); return 1; }
N(GetObjectRot) { auto it = g_Samp->obj.find((int)params[1]); if(it == g_Samp->obj.end()) return 0; SetRefF(amx, params[2], it->second.r[0]); SetRefF(amx, params[3], it->second.r[1]); SetRefF(amx, params[4], it->second.r[2]); return 1; }
N(GetObjectModel) { auto it = g_Samp->obj.find((int)params[1]); return it == g_Samp->obj.end() ? -1 : it->second.model; }
N(MoveObject)
{   // the MP map rows don't animate: the object jumps to the end and OnObjectMoved / OnDynamicObjectMoved fires
    auto it = g_Samp->obj.find((int)params[1]); if(it == g_Samp->obj.end()) return 0;
    float p[3] = { PF(params, 2), PF(params, 3), PF(params, 4) };
    float r[3] = { it->second.r[0], it->second.r[1], it->second.r[2] };
    if(NArgs(params) >= 8) { float rx = PF(params, 6), ry = PF(params, 7), rz = PF(params, 8); if(rx > -999) r[0] = rx; if(ry > -999) r[1] = ry; if(rz > -999) r[2] = rz; }
    float dx = p[0] - it->second.p[0], dy = p[1] - it->second.p[1], dz = p[2] - it->second.p[2], sp = PF(params, 5);
    int ms = sp > 0.01f ? (int)(sqrtf(dx * dx + dy * dy + dz * dz) / sp * 1000.0f) : 0;
    S_->SampObjMove((int)params[1], p, r);
    SampTimer t; t.id = g_Samp->nextTimer++; t.sc = nullptr; t.fn = "\x01moved"; t.interval = (uint32_t)std::max(1, ms); t.repeat = false; t.next = MP_NowMs() + t.interval; t.args.push_back(AI(params[1]));
    g_Samp->timers.push_back(std::move(t));
    return ms;
}
N(StopObject) { UNUSED_AMX; return 1; }
N(CountObjects) { UNUSED_AMX; return (cell)g_Samp->obj.size(); }
N(IsObjectMoving) { UNUSED_AMX; return 0; }
N(GetDynamicObjectPos) { return n_GetObjectPos(amx, params); }
N(GetDynamicObjectRot) { return n_GetObjectRot(amx, params); }
N(SetDynamicObjectPos) { return n_SetObjectPos(amx, params); }
N(SetDynamicObjectRot) { return n_SetObjectRot(amx, params); }

// ---- 3D text labels ---------------------------------------------------------------------------------
static int LabelNew(const std::string& text, uint32_t col, float x, float y, float z, float dist, int world)
{
    for(size_t i = 1; i < g_Samp->labels.size(); ++i) if(!g_Samp->labels[i].used)
    {
        SampLabel& l = g_Samp->labels[i]; l.used = true; l.text = text; l.color = col; l.p[0] = x; l.p[1] = y; l.p[2] = z; l.dist = dist; l.world = world;
        S_->SampLabelSend(-1, (int)i); return (int)i;
    }
    return 0xFFFF;
}
N(Create3DTextLabel) { return LabelNew(Str(amx, params[1]), (uint32_t)params[2], PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), (int)params[7]); }
N(CreateDynamic3DTextLabel) { return LabelNew(Str(amx, params[1]), (uint32_t)params[2], PF(params, 3), PF(params, 4), PF(params, 5), PF(params, 6), NArgs(params) >= 10 ? (int)params[10] : -1); }
N(CreatePlayer3DTextLabel) { return LabelNew(Str(amx, params[2]), (uint32_t)params[3], PF(params, 4), PF(params, 5), PF(params, 6), PF(params, 7), -1); }
N(Delete3DTextLabel) { cell id = params[1]; if(id <= 0 || id >= (cell)g_Samp->labels.size() || !g_Samp->labels[id].used) return 0; g_Samp->labels[id].used = false; S_->SampLabelSend(-1, (int)id); return 1; }
N(DeletePlayer3DTextLabel) { cell p2[2] = { (cell)sizeof(cell), params[2] }; return n_Delete3DTextLabel(amx, p2); }
N(Update3DTextLabelText)
{
    cell id = params[1]; if(id <= 0 || id >= (cell)g_Samp->labels.size() || !g_Samp->labels[id].used) return 0;
    g_Samp->labels[id].color = (uint32_t)params[2]; g_Samp->labels[id].text = Str(amx, params[3]); S_->SampLabelSend(-1, (int)id); return 1;
}
N(UpdateDynamic3DTextLabelText) { return n_Update3DTextLabelText(amx, params); }
N(DestroyDynamic3DTextLabel) { return n_Delete3DTextLabel(amx, params); }
N(IsValidDynamic3DTextLabel) { cell id = params[1]; return id > 0 && id < (cell)g_Samp->labels.size() && g_Samp->labels[id].used; }
N(CreateDynamicMapIcon)
{
    int id = g_Samp->iconNext++ % 100; float f[4] = { PF(params, 1), PF(params, 2), PF(params, 3) };
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampOp(i, SPO_MAPICON, (uint8_t)id, (uint8_t)params[4], params[5], f);
    return g_Samp->iconNext - 1;
}
N(NewId) { UNUSED_AMX; return g_Samp->areaNext++; }

// ---- textdraws ---------------------------------------------------------------------------------
static SampTdDef* TD(AMX* amx, const cell* params, int& pl, int& base)
{   // global (Text:id) or player (playerid, PlayerText:id) by the native name's convention: base = 1st param index after the id
    (void)amx; if(pl < 0) { cell id = params[1]; base = 2; return id >= 0 && id < SAMP_MAX_TD && g_Samp->td[id].used ? &g_Samp->td[id] : nullptr; }
    cell id = params[2]; base = 3; if(!PlOk(params[1]) || id < 0 || id >= SAMP_MAX_PTD) return nullptr; pl = (int)params[1];
    return g_SampPl[pl].ptd[id].used ? &g_SampPl[pl].ptd[id] : nullptr;
}
static void TdDefault(PktTextDraw& t, float x, float y, const std::string& s)
{
    memset(&t, 0, sizeof(t)); t.type = PKT_TEXTDRAW; t.show = 1; t.x = x; t.y = y; t.letterX = 0.48f; t.letterY = 1.12f;
    t.color = 0xE1E1E1FF; t.boxColor = 0x80808080; t.bgColor = 0x000000FF; t.font = 1; t.align = 1; t.shadow = 2; t.proportional = 1;
    MP_CopyStr(t.text, s.c_str(), sizeof(t.text));
}
N(TextDrawCreate)
{
    for(int i = 0; i < SAMP_MAX_TD; ++i) if(!g_Samp->td[i].used) { g_Samp->td[i] = SampTdDef(); g_Samp->td[i].used = true; TdDefault(g_Samp->td[i].t, PF(params, 1), PF(params, 2), Str(amx, params[3])); return i; }
    return SAMP_INVALID_TD;
}
N(CreatePlayerTextDraw)
{
    if(!PlOk(params[1])) return SAMP_INVALID_TD; SampPl& p = g_SampPl[params[1]];
    for(int i = 0; i < SAMP_MAX_PTD; ++i) if(!p.ptd[i].used) { p.ptd[i] = SampTdDef(); p.ptd[i].used = true; TdDefault(p.ptd[i].t, PF(params, 2), PF(params, 3), Str(amx, params[4])); return i; }
    return SAMP_INVALID_TD;
}
#define TDN(nm, PL, body) N(nm) { int pl = PL; int b = 0; SampTdDef* d = TD(amx, params, pl, b); if(!d) return 0; PktTextDraw& t = d->t; (void)t; body; S_->SampTdChanged(pl, PL < 0 ? (int)params[1] : (int)params[2], PL >= 0); return 1; }
TDN(TextDrawLetterSize, -1, { t.letterX = PF(params, b); t.letterY = PF(params, b + 1); })
TDN(TextDrawTextSize, -1, { t.boxX = PF(params, b); })
TDN(TextDrawAlignment, -1, { t.align = (uint8_t)params[b]; })
TDN(TextDrawColor, -1, { t.color = (uint32_t)params[b]; })
TDN(TextDrawUseBox, -1, { t.useBox = params[b] != 0; })
TDN(TextDrawBoxColor, -1, { t.boxColor = (uint32_t)params[b]; })
TDN(TextDrawSetShadow, -1, { t.shadow = (uint8_t)params[b]; })
TDN(TextDrawSetOutline, -1, { t.outline = (uint8_t)params[b]; })
TDN(TextDrawBackgroundColor, -1, { t.bgColor = (uint32_t)params[b]; })
TDN(TextDrawFont, -1, { t.font = (uint8_t)params[b]; })
TDN(TextDrawSetProportional, -1, { t.proportional = params[b] != 0; })
TDN(TextDrawSetSelectable, -1, { d->selectable = params[b] != 0; })
TDN(TextDrawSetString, -1, { MP_CopyStr(t.text, Str(amx, params[b]).c_str(), sizeof(t.text)); })
TDN(TextDrawSetPreviewModel, -1, { (void)0; })
TDN(PlayerTextDrawLetterSize, 0, { t.letterX = PF(params, b); t.letterY = PF(params, b + 1); })
TDN(PlayerTextDrawTextSize, 0, { t.boxX = PF(params, b); })
TDN(PlayerTextDrawAlignment, 0, { t.align = (uint8_t)params[b]; })
TDN(PlayerTextDrawColor, 0, { t.color = (uint32_t)params[b]; })
TDN(PlayerTextDrawUseBox, 0, { t.useBox = params[b] != 0; })
TDN(PlayerTextDrawBoxColor, 0, { t.boxColor = (uint32_t)params[b]; })
TDN(PlayerTextDrawSetShadow, 0, { t.shadow = (uint8_t)params[b]; })
TDN(PlayerTextDrawSetOutline, 0, { t.outline = (uint8_t)params[b]; })
TDN(PlayerTextDrawBackgroundColor, 0, { t.bgColor = (uint32_t)params[b]; })
TDN(PlayerTextDrawFont, 0, { t.font = (uint8_t)params[b]; })
TDN(PlayerTextDrawSetProportional, 0, { t.proportional = params[b] != 0; })
TDN(PlayerTextDrawSetSelectable, 0, { d->selectable = params[b] != 0; })
TDN(PlayerTextDrawSetString, 0, { MP_CopyStr(t.text, Str(amx, params[b]).c_str(), sizeof(t.text)); })
TDN(PlayerTextDrawSetPreviewModel, 0, { (void)0; })
// textdraw-streamer (https://github.com/nexquery/samp-textdraw-streamer): same args + file, line
N(CreateDynamicTextDraw)
{
    std::string t = SampFormat(amx, params, Str(amx, params[3]), 4);
    for(int i = 0; i < SAMP_MAX_TD; ++i) if(!g_Samp->td[i].used) { g_Samp->td[i] = SampTdDef(); g_Samp->td[i].used = true; TdDefault(g_Samp->td[i].t, PF(params, 1), PF(params, 2), t); return i; }
    return SAMP_INVALID_TD;
}
N(CreateDynamicPlayerTextDraw)
{
    if(!PlOk(params[1])) return SAMP_INVALID_TD; SampPl& p = g_SampPl[params[1]];
    std::string t = SampFormat(amx, params, Str(amx, params[4]), 5);
    for(int i = 0; i < SAMP_MAX_PTD; ++i) if(!p.ptd[i].used) { p.ptd[i] = SampTdDef(); p.ptd[i].used = true; TdDefault(p.ptd[i].t, PF(params, 2), PF(params, 3), t); return i; }
    return SAMP_INVALID_TD;
}
TDN(DynamicTextDrawSetString, -1, { MP_CopyStr(t.text, SampFormat(amx, params, Str(amx, params[b]), b + 1).c_str(), sizeof(t.text)); })
TDN(DynamicPlayerTextDrawSetString, 0, { MP_CopyStr(t.text, SampFormat(amx, params, Str(amx, params[b]), b + 1).c_str(), sizeof(t.text)); })
TDN(DynamicTextDrawSetPos, -1, { t.x = PF(params, b); t.y = PF(params, b + 1); })
TDN(DynamicPlayerTextDrawSetPos, 0, { t.x = PF(params, b); t.y = PF(params, b + 1); })
N(DynamicTextDrawGetString) { if(params[1] < 0 || params[1] >= SAMP_MAX_TD || !g_Samp->td[params[1]].used) return 0; SetStr(amx, params[2], g_Samp->td[params[1]].t.text, params[3]); return 1; }
N(DynamicPlayerTextDrawGetString) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_PTD || !g_SampPl[params[1]].ptd[params[2]].used) return 0; SetStr(amx, params[3], g_SampPl[params[1]].ptd[params[2]].t.text, params[4]); return 1; }
N(IsValidPlayerTextDraw) { return PlOk(params[1]) && params[2] >= 0 && params[2] < SAMP_MAX_PTD && g_SampPl[params[1]].ptd[params[2]].used; }
N(IsTdVisible) { if(!PlOk(params[1])) return 0; return S_->SampTdSlot((int)params[1], 0x4000 | (int)params[2], false) >= 0; }
N(IsPtdVisible) { if(!PlOk(params[1])) return 0; return S_->SampTdSlot((int)params[1], 0x6000 | (int)params[2], false) >= 0; }
N(TextDrawDestroy) { cell id = params[1]; if(id < 0 || id >= SAMP_MAX_TD || !g_Samp->td[id].used) return 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampTdShow(i, 0x4000 | (int)id, false); g_Samp->td[id].used = false; return 1; }
N(PlayerTextDrawDestroy) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_PTD) return 0; S_->SampTdShow((int)params[1], 0x6000 | (int)params[2], false); g_SampPl[params[1]].ptd[params[2]].used = false; return 1; }
N(TextDrawShowForPlayer) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_TD || !g_Samp->td[params[2]].used) return 0; S_->SampTdShow((int)params[1], 0x4000 | (int)params[2], true); return 1; }
N(TextDrawHideForPlayer) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_TD) return 0; S_->SampTdShow((int)params[1], 0x4000 | (int)params[2], false); return 1; }
N(TextDrawShowForAll) { if(params[1] < 0 || params[1] >= SAMP_MAX_TD || !g_Samp->td[params[1]].used) return 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampTdShow(i, 0x4000 | (int)params[1], true); return 1; }
N(TextDrawHideForAll) { if(params[1] < 0 || params[1] >= SAMP_MAX_TD) return 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(g_SampPl[i].on) S_->SampTdShow(i, 0x4000 | (int)params[1], false); return 1; }
N(PlayerTextDrawShow) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_PTD || !g_SampPl[params[1]].ptd[params[2]].used) return 0; S_->SampTdShow((int)params[1], 0x6000 | (int)params[2], true); return 1; }
N(PlayerTextDrawHide) { if(!PlOk(params[1]) || params[2] < 0 || params[2] >= SAMP_MAX_PTD) return 0; S_->SampTdShow((int)params[1], 0x6000 | (int)params[2], false); return 1; }
N(TextDrawIsValid) { return params[1] >= 0 && params[1] < SAMP_MAX_TD && g_Samp->td[params[1]].used; }
N(TextDrawGetString) { if(params[1] < 0 || params[1] >= SAMP_MAX_TD || !g_Samp->td[params[1]].used) return 0; SetStr(amx, params[2], g_Samp->td[params[1]].t.text, params[3]); return 1; }

// ---- Pawn.CMD (https://github.com/katursis/Pawn.CMD): commands = publics pc_cmd_<name> -----------
N(PC_Init) { UNUSED_AMX; g_Samp->pawnCmd = true; return 1; }
N(PC_RegAlias)
{
    std::string cmd = Lower(Str(amx, params[1]));
    for(int i = 2; i <= NArgs(params); ++i) g_Samp->pcAlias[Lower(Str(amx, params[i]))] = cmd;
    return 1;
}
N(PC_SetFlags) { g_Samp->pcFlags[Lower(Str(amx, params[1]))] = params[2]; return 1; }
N(PC_GetFlags) { auto it = g_Samp->pcFlags.find(Lower(Str(amx, params[1]))); return it == g_Samp->pcFlags.end() ? 0 : it->second; }
N(PC_EmulateCommand) { if(!PlOk(params[1])) return 0; S_->SampCommand((int)params[1], Str(amx, params[2]).c_str()); return 1; }
N(PC_CommandExists) { std::string c = Lower(Str(amx, params[1])); for(auto& sc : g_Samp->scripts) if(FindPub(sc.get(), ("pc_cmd_" + c).c_str()) >= 0) return 1; return g_Samp->pcAlias.count(c) != 0; }

} // namespace

#define R(name) { #name, n_##name }
#define RA(name, fn) { name, n_##fn }
#include "server_samp_sscanf.inc"
#include "server_samp_sql.inc"

namespace {
static const AMX_NATIVE_INFO g_SampNatives[] =
{
    R(print), R(printf), R(format), R(GetTickCount), R(tickcount), R(gettime), R(getdate), R(GetMaxPlayers), R(GetPlayerPoolSize), R(GetVehiclePoolSize),
    R(SetGameModeText), R(GetConsoleVarAsString), R(GetConsoleVarAsInt), R(GetConsoleVarAsBool), RA("GetServerVarAsString", GetConsoleVarAsString),
    RA("GetServerVarAsInt", GetConsoleVarAsInt), RA("GetServerVarAsBool", GetConsoleVarAsBool), R(SendRconCommand), R(CallLocalFunction), R(CallRemoteFunction),
    R(VectorSize), R(asin), R(acos), R(atan), R(atan2), R(GetPlayerVersion), R(gpci), R(GetPlayerIp), R(GetServerTickRate), R(GetWeaponName),
    R(SetTimer), R(SetTimerEx), R(KillTimer), R(IsValidTimer),
    R(SetPVarInt), R(GetPVarInt), R(SetPVarFloat), R(GetPVarFloat), R(SetPVarString), R(GetPVarString), R(DeletePVar), R(GetPVarType), R(GetPVarsUpperIndex), R(GetPVarNameAtIndex),
    R(SetSVarInt), R(GetSVarInt), R(SetSVarFloat), R(GetSVarFloat), R(SetSVarString), R(GetSVarString), R(DeleteSVar), R(GetSVarType),
    R(fopen), R(fclose), R(fwrite), R(fread), R(fputchar), R(fgetchar), R(fblockwrite), R(fblockread), R(fseek), R(flength), R(fexist), R(fremove), R(frename), R(ftemp), R(dir_exists), R(dir_create),
    R(IsPlayerConnected), R(IsPlayerNPC), R(IsPlayerAdmin), R(GetPlayerName), R(SetPlayerName), R(GetPlayerPing), R(GetPlayerState), R(GetPlayerPos), R(SetPlayerPos), R(SetPlayerPosFindZ),
    R(GetPlayerFacingAngle), R(SetPlayerFacingAngle), R(IsPlayerInRangeOfPoint), R(GetPlayerDistanceFromPoint), R(GetPlayerHealth), R(SetPlayerHealth), R(GetPlayerArmour), R(SetPlayerArmour),
    R(GetPlayerMoney), R(GivePlayerMoney), R(ResetPlayerMoney), R(GetPlayerScore), R(SetPlayerScore), R(GetPlayerSkin), R(SetPlayerSkin), R(GetPlayerInterior), R(SetPlayerInterior),
    R(GetPlayerVirtualWorld), R(SetPlayerVirtualWorld), R(GetPlayerTeam), R(SetPlayerTeam), R(GetPlayerColor), R(SetPlayerColor), R(GetPlayerWantedLevel), R(SetPlayerWantedLevel),
    R(GetPlayerWeapon), R(GetPlayerAmmo), R(GivePlayerWeapon), R(ResetPlayerWeapons), R(GetPlayerWeaponData), R(GetPlayerDrunkLevel), R(SetPlayerDrunkLevel), R(GetPlayerSpecialAction),
    R(SetPlayerSpecialAction), R(GetPlayerFightingStyle), R(SetPlayerFightingStyle), R(SetPlayerWeather), R(SetWeather), R(SetPlayerTime), R(SetWorldTime), R(TogglePlayerControllable),
    R(TogglePlayerSpectating), R(SpawnPlayer), R(SetSpawnInfo), R(Kick), R(Ban), R(BanEx), R(SendClientMessage), R(SendClientMessageToAll), R(SendPlayerMessageToPlayer), R(SendPlayerMessageToAll),
    R(SendDeathMessage), R(GameTextForPlayer), R(GameTextForAll), R(ShowPlayerDialog), R(GetPlayerDialogID), RA("GetPlayerDialog", GetPlayerDialogID), R(ApplyAnimation), R(ClearAnimations), R(GetPlayerAnimationIndex),
    R(PlayerPlaySound), R(CreateExplosion), R(CreateExplosionForPlayer), R(SetPlayerCheckpoint), R(DisablePlayerCheckpoint), R(SetPlayerRaceCheckpoint), R(DisablePlayerRaceCheckpoint),
    R(IsPlayerInCheckpoint), R(IsPlayerInRaceCheckpoint), R(SetPlayerMapIcon), R(RemovePlayerMapIcon), R(RemoveBuildingForPlayer), R(SetPlayerCameraPos), R(SetPlayerCameraLookAt),
    R(SetCameraBehindPlayer), R(InterpolateCameraPos), R(InterpolateCameraLookAt), R(GetPlayerCameraPos), R(GetPlayerCameraFrontVector), R(GetPlayerVelocity), R(GetPlayerKeys),
    R(IsPlayerInAnyVehicle), R(GetPlayerVehicleID), R(GetPlayerVehicleSeat), R(IsPlayerInVehicle), R(RemovePlayerFromVehicle), R(PutPlayerInVehicle), R(SetPlayerChatBubble),
    R(SetPlayerAttachedObject), R(RemovePlayerAttachedObject), R(IsPlayerAttachedObjectSlotUsed), R(SetPlayerSkillLevel), R(GetPlayerTargetPlayer), R(GetPlayerSurfingVehicleID),
    R(NetStats_GetConnectedTime), R(SelectTextDraw), R(CancelSelectTextDraw),
    R(CreateVehicle), RA("AddStaticVehicle", CreateVehicle), RA("AddStaticVehicleEx", CreateVehicle), R(DestroyVehicle), R(IsValidVehicle), R(GetVehicleModel), R(GetVehiclePos), R(SetVehiclePos),
    R(GetVehicleZAngle), R(SetVehicleZAngle), R(GetVehicleHealth), R(SetVehicleHealth), R(GetVehicleVirtualWorld), R(SetVehicleVirtualWorld), R(GetVehicleDistanceFromPoint),
    R(GetVehicleParamsEx), R(GetVehicleVelocity),
    R(CreateObject), R(CreatePlayerObject), R(CreateDynamicObject), R(CreateDynamicObjectEx), R(DestroyObject), R(DestroyPlayerObject), RA("DestroyDynamicObject", DestroyObject),
    R(IsValidObject), R(IsValidPlayerObject), RA("IsValidDynamicObject", IsValidObject), R(SetObjectPos), R(SetObjectRot), R(GetObjectPos), R(GetObjectRot), R(GetObjectModel), R(MoveObject),
    RA("MoveDynamicObject", MoveObject), R(StopObject), RA("StopDynamicObject", StopObject), R(IsObjectMoving), RA("IsDynamicObjectMoving", IsObjectMoving),
    R(GetDynamicObjectPos), R(GetDynamicObjectRot), R(SetDynamicObjectPos), R(SetDynamicObjectRot),
    R(Create3DTextLabel), R(CreateDynamic3DTextLabel), R(CreatePlayer3DTextLabel), R(Delete3DTextLabel), R(DeletePlayer3DTextLabel), R(Update3DTextLabelText),
    R(UpdateDynamic3DTextLabelText), R(DestroyDynamic3DTextLabel), R(IsValidDynamic3DTextLabel), R(CreateDynamicMapIcon),
    RA("CreateDynamicPickup", NewId), RA("CreatePickup", NewId), RA("AddStaticPickup", NewId), RA("CreateDynamicCP", NewId), RA("CreateDynamicRaceCP", NewId),
    RA("CreateDynamicCircle", NewId), RA("CreateDynamicSphere", NewId), RA("CreateDynamicRectangle", NewId), RA("CreateDynamicCube", NewId), RA("CreateDynamicCylinder", NewId),
    RA("CreateDynamicPolygon", NewId), RA("CreateActor", NewId), RA("CreateDynamicActor", NewId), RA("GangZoneCreate", NewId), RA("CreateMenu", NewId),
    R(TextDrawCreate), R(CreatePlayerTextDraw), R(TextDrawLetterSize), R(TextDrawTextSize), R(TextDrawAlignment), R(TextDrawColor), R(TextDrawUseBox), R(TextDrawBoxColor),
    R(TextDrawSetShadow), R(TextDrawSetOutline), R(TextDrawBackgroundColor), R(TextDrawFont), R(TextDrawSetProportional), R(TextDrawSetSelectable), R(TextDrawSetString),
    R(TextDrawSetPreviewModel), R(PlayerTextDrawLetterSize), R(PlayerTextDrawTextSize), R(PlayerTextDrawAlignment), R(PlayerTextDrawColor), R(PlayerTextDrawUseBox),
    R(PlayerTextDrawBoxColor), R(PlayerTextDrawSetShadow), R(PlayerTextDrawSetOutline), R(PlayerTextDrawBackgroundColor), R(PlayerTextDrawFont), R(PlayerTextDrawSetProportional),
    R(PlayerTextDrawSetSelectable), R(PlayerTextDrawSetString), R(PlayerTextDrawSetPreviewModel), R(TextDrawDestroy), R(PlayerTextDrawDestroy), R(TextDrawShowForPlayer),
    R(TextDrawHideForPlayer), R(TextDrawShowForAll), R(TextDrawHideForAll), R(PlayerTextDrawShow), R(PlayerTextDrawHide), R(TextDrawIsValid), R(TextDrawGetString),
    RA("TextDrawColour", TextDrawColor), RA("TextDrawBoxColour", TextDrawBoxColor), RA("TextDrawBackgroundColour", TextDrawBackgroundColor),
    RA("PlayerTextDrawColour", PlayerTextDrawColor), RA("PlayerTextDrawBoxColour", PlayerTextDrawBoxColor), RA("PlayerTextDrawBackgroundColour", PlayerTextDrawBackgroundColor),
    R(CreateDynamicTextDraw), R(CreateDynamicPlayerTextDraw), R(DynamicTextDrawSetString), R(DynamicPlayerTextDrawSetString), R(DynamicTextDrawSetPos), R(DynamicPlayerTextDrawSetPos),
    R(DynamicTextDrawGetString), R(DynamicPlayerTextDrawGetString), RA("IsValidDynamicTextDraw", TextDrawIsValid), RA("IsValidDynamicPlayerTextDraw", IsValidPlayerTextDraw),
    RA("IsValidPlayerTextDraw", IsValidPlayerTextDraw), RA("IsDynTextDrawVisibleForPlayer", IsTdVisible), RA("IsDynamicPlayerTextDrawVisible", IsPtdVisible),
    RA("IsTextDrawVisibleForPlayer", IsTdVisible), RA("IsPlayerTextDrawVisible", IsPtdVisible),
    RA("DestroyDynamicTextDraw", TextDrawDestroy), RA("DynamicTextDrawLetterSize", TextDrawLetterSize), RA("DynamicTextDrawTextSize", TextDrawTextSize),
    RA("DynamicTextDrawAlignment", TextDrawAlignment), RA("DynamicTextDrawColour", TextDrawColor), RA("DynamicTextDrawColor", TextDrawColor), RA("DynamicTextDrawUseBox", TextDrawUseBox),
    RA("DynamicTextDrawBoxColour", TextDrawBoxColor), RA("DynamicTextDrawBoxColor", TextDrawBoxColor), RA("DynamicTextDrawSetShadow", TextDrawSetShadow), RA("DynamicTextDrawSetOutline", TextDrawSetOutline),
    RA("DynamicTextDrawBackgroundColour", TextDrawBackgroundColor), RA("DynamicTextDrawBackgroundColor", TextDrawBackgroundColor), RA("DynamicTextDrawFont", TextDrawFont),
    RA("DynamicTextDrawSetProportional", TextDrawSetProportional), RA("DynamicTextDrawSetSelectable", TextDrawSetSelectable), RA("DynamicTextDrawShowForPlayer", TextDrawShowForPlayer),
    RA("DynamicTextDrawHideForPlayer", TextDrawHideForPlayer), RA("DynamicTextDrawShowForAll", TextDrawShowForAll), RA("DynamicTextDrawHideForAll", TextDrawHideForAll),
    RA("DynamicTextDrawSetPreviewModel", TextDrawSetPreviewModel), RA("DynamicTextDrawSetPreviewRot", Ret1), RA("DynamicTextDrawSetPreviewVehCol", Ret1),
    RA("TextDrawSetPreviewRot", Ret1), RA("TextDrawSetPreviewVehCol", Ret1), RA("PlayerTextDrawSetPreviewRot", Ret1), RA("PlayerTextDrawSetPreviewVehCol", Ret1),
    RA("DestroyDynamicPlayerTextDraw", PlayerTextDrawDestroy), RA("DynamicPlayerTextDrawLetterSize", PlayerTextDrawLetterSize), RA("DynamicPlayerTextDrawTextSize", PlayerTextDrawTextSize),
    RA("DynamicPlayerTextDrawAlignment", PlayerTextDrawAlignment), RA("DynamicPlayerTextDrawColour", PlayerTextDrawColor), RA("DynamicPlayerTextDrawColor", PlayerTextDrawColor),
    RA("DynamicPlayerTextDrawUseBox", PlayerTextDrawUseBox), RA("DynamicPlayerTextDrawBoxColor", PlayerTextDrawBoxColor), RA("DynamicPlayerTextDrawBoxColour", PlayerTextDrawBoxColor),
    RA("DynamicPlayerTextDrawSetShadow", PlayerTextDrawSetShadow), RA("DynamicPlayerTextDrawSetOutline", PlayerTextDrawSetOutline), RA("DynamicPlayerTextDrawBGColour", PlayerTextDrawBackgroundColor),
    RA("DynamicPlayerTextDrawFont", PlayerTextDrawFont), RA("DynPlayerTextSetProportional", PlayerTextDrawSetProportional), RA("DynamicPlayerTextDrawSelectable", PlayerTextDrawSetSelectable),
    RA("DynamicPlayerTextDrawShow", PlayerTextDrawShow), RA("DynamicPlayerTextDrawHide", PlayerTextDrawHide), RA("DynamicPlayerTextDrawSetPrevMdl", PlayerTextDrawSetPreviewModel),
    RA("DynamicPlayerTextDrawSetPrevRot", Ret1), RA("DynamicPlayerTextDrawPrevVehCol", Ret1),
    RA("IsValidDynamicMapIcon", Ret1), RA("CountDynamicObjects", CountObjects), RA("AttachObjectToObject", Ret1), RA("AttachDynamicObjectToObject", Ret1), RA("BlockIpAddress", Ret1),
    RA("UnBlockIpAddress", Ret1), RA("EnableTirePopping", Ret1), RA("EnableZoneNames", Ret1), RA("EnableVehicleFriendlyFire", Ret1), RA("NetStats_MessagesRecvPerSecond", Ret0),
    RA("NetStats_GetIpPort", GetPlayerIp), RA("SvGetVersion", Ret0),
    R(PC_Init), R(PC_RegAlias), R(PC_SetFlags), R(PC_GetFlags), R(PC_EmulateCommand), R(PC_CommandExists),
    // plugin natives a SA-MP server answers with "ok" (the plugin's work isn't needed on MP)
    RA("Streamer_Update", Ret1), RA("Streamer_UpdateEx", Ret1), RA("Streamer_ToggleIdleUpdate", Ret1), RA("Streamer_SetVisibleItems", Ret1), RA("Streamer_TickRate", Ret1),
    RA("Streamer_SetTickRate", Ret1), RA("Streamer_MaxItems", Ret1), RA("Streamer_SetMaxItems", Ret1), RA("Streamer_ToggleErrorCallback", Ret1),
    RA("SetDynamicObjectMaterial", Ret1), RA("SetDynamicObjectMaterialText", Ret1), RA("SetObjectMaterial", Ret1), RA("SetObjectMaterialText", Ret1),
    RA("ShowPlayerNameTagForPlayer", Ret1), RA("ShowNameTags", Ret1), RA("ShowPlayerMarkers", Ret1), RA("UsePlayerPedAnims", Ret1), RA("DisableInteriorEnterExits", Ret1),
    RA("EnableStuntBonusForAll", Ret1), RA("EnableStuntBonusForPlayer", Ret1), RA("SetNameTagDrawDistance", Ret1), RA("LimitGlobalChatRadius", Ret1), RA("LimitPlayerMarkerRadius", Ret1),
    RA("ManualVehicleEngineAndLights", Ret1), RA("AllowInteriorWeapons", Ret1), RA("SetGravity", Ret1), RA("AddPlayerClass", Ret0), RA("AddPlayerClassEx", Ret0), RA("SetTeamCount", Ret1),
    RA("SetVehicleParamsEx", Ret1), RA("SetVehicleParamsForPlayer", Ret1), RA("ChangeVehicleColor", Ret1), RA("ChangeVehiclePaintjob", Ret1), RA("AddVehicleComponent", Ret1),
    RA("SetVehicleNumberPlate", Ret1), RA("RepairVehicle", Ret1), RA("LinkVehicleToInterior", Ret1), RA("SetVehicleToRespawn", Ret1), RA("SetVehicleVelocity", Ret1),
    RA("UpdateVehicleDamageStatus", Ret1), RA("GangZoneShowForPlayer", Ret1), RA("GangZoneShowForAll", Ret1), RA("GangZoneHideForPlayer", Ret1), RA("GangZoneHideForAll", Ret1),
    RA("GangZoneFlashForPlayer", Ret1), RA("GangZoneFlashForAll", Ret1), RA("GangZoneStopFlashForPlayer", Ret1), RA("GangZoneStopFlashForAll", Ret1), RA("GangZoneDestroy", Ret1),
    RA("DestroyPickup", Ret1), RA("DestroyDynamicPickup", Ret1), RA("DestroyDynamicCP", Ret1), RA("DestroyDynamicRaceCP", Ret1), RA("DestroyDynamicArea", Ret1), RA("DestroyDynamicMapIcon", Ret1),
    RA("DestroyActor", Ret1), RA("DestroyDynamicActor", Ret1), RA("SetActorPos", Ret1), RA("SetActorFacingAngle", Ret1), RA("ApplyActorAnimation", Ret1), RA("SetActorInvulnerable", Ret1),
    RA("SetActorVirtualWorld", Ret1), RA("TogglePlayerClock", Ret1), RA("PlayAudioStreamForPlayer", Ret1), RA("StopAudioStreamForPlayer", Ret1), RA("SetPlayerShopName", Ret1),
    RA("AttachObjectToVehicle", Ret1), RA("AttachObjectToPlayer", Ret1), RA("AttachDynamicObjectToVehicle", Ret1), RA("AttachDynamicObjectToPlayer", Ret1), RA("Attach3DTextLabelToPlayer", Ret1),
    RA("Attach3DTextLabelToVehicle", Ret1), RA("EditObject", Ret1), RA("EditDynamicObject", Ret1), RA("EditAttachedObject", Ret1), RA("CancelEdit", Ret1), RA("SelectObject", Ret1),
    RA("PlayerSpectatePlayer", Ret1), RA("PlayerSpectateVehicle", Ret1), RA("ForceClassSelection", Ret1), RA("AllowAdminTeleport", Ret1), RA("AllowPlayerTeleport", Ret1),
    RA("SetPlayerVelocity", Ret1), RA("SetPlayerArmedWeapon", Ret1), RA("SetPlayerAmmo", Ret1), RA("TogglePlayerDynamicCP", Ret1), RA("TogglePlayerAllDynamicCPs", Ret1),
    RA("IsPlayerInDynamicArea", Ret0), RA("IsPlayerInAnyDynamicArea", Ret0), RA("IsPlayerInDynamicCP", Ret0), RA("IsValidDynamicArea", Ret1), RA("IsValidDynamicPickup", Ret1),
    RA("IsValidDynamicCP", Ret1), RA("IsValidActor", Ret0), RA("IsValidDynamicActor", Ret0), RA("SetPlayerObjectMaterial", Ret1), RA("SetPlayerObjectMaterialText", Ret1),
    RA("Streamer_SetIntData", Ret1), RA("Streamer_SetFloatData", Ret1), RA("Streamer_SetArrayData", Ret1), RA("Streamer_AppendArrayData", Ret1), RA("Streamer_RemoveArrayData", Ret1),
    RA("Streamer_GetIntData", Ret0), RA("Streamer_GetArrayData", Ret0), RA("Streamer_IsInArrayData", Ret0), RA("Streamer_GetUpperBound", Ret0), RA("Streamer_ToggleItemStatic", Ret1),
    RA("GetPlayerNetworkStats", Ret0), RA("NetStats_PacketLossPercent", RetF0), RA("GetPlayerCameraMode", Ret0), RA("GetPlayerCameraTargetVehicle", RetInvalid),
    RA("GetPlayerCameraTargetPlayer", RetInvalid), RA("GetPlayerCameraTargetObject", RetInvalid), RA("SetDeathDropAmount", Ret1), RA("ConnectNPC", Ret0), RA("SendClientCheck", Ret1),
    RA("SetPlayerMarkerForPlayer", Ret1), RA("ShowMenuForPlayer", Ret1), RA("HideMenuForPlayer", Ret1), RA("AddMenuItem", Ret0), RA("SetMenuColumnHeader", Ret1), RA("DestroyMenu", Ret1),
    RA("GetPlayerMenu", RetInvalid), RA("crashdetect_set_error_handler", Ret1), RA("GetAmxBaseAddress", Ret0), RA("sampvoice_init", Ret1),
};
} // namespace

// =================================================================================================
// MPServer side
// =================================================================================================
void MPServer::SampLog(const char* fmt, ...)
{
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    if(g_Samp && g_Samp->test) { printf("%s\n", b); fflush(stdout); return; }
    Log("%s", b);
}
std::string MPServer::SampUid(int slot) { return m_Slots[slot].uid[0] ? m_Slots[slot].uid : "MP-PHONE"; }
std::string MPServer::SampIp(int slot) { char b[32]; inet_ntop(AF_INET, &m_Slots[slot].addr.sin_addr, b, sizeof(b)); return b; }
bool MPServer::SampIsAdmin(int slot) { return m_Slots[slot].admin || m_Slots[slot].owner; }
uint64_t MPServer::SampOnlineMs(int slot) { return MP_NowMs() - m_Slots[slot].joinedMs; }
void MPServer::SampGetPos(int slot, float* p)
{
    SampPl& s = g_SampPl[slot];
    if(m_Slots[slot].hasPos) memcpy(p, m_Slots[slot].pos, sizeof(float) * 3); else memcpy(p, s.pos, sizeof(float) * 3);
}
void MPServer::SampTele(int slot, const float* p, float h)
{
    SampPl& s = g_SampPl[slot]; memcpy(s.pos, p, sizeof(s.pos)); s.a = h;
    if(!g_Samp->test) { memcpy(m_Slots[slot].pos, p, sizeof(float) * 3); m_Slots[slot].hasPos = true; }
    SampAct(slot, CGA_TELE, 255, 0, 0, p, h);
}
void MPServer::SampAct(int slot, uint8_t act, int a, int b, int32_t val, const float* pos, float h, const char* text)
{
    if(g_Samp && g_Samp->test) { SampLog("  -> [%d] act %d a=%d b=%d val=%d %s", slot, act, a, b, val, text ? text : ""); return; }
    CgAct(slot, act, a, b, val, pos, h, text);
}
void MPServer::SampOp(int slot, uint8_t op, uint8_t a, uint8_t b, int32_t val, const float* f, const char* text)
{
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = op; p.a = a; p.b = b; p.val = val;
    if(f) memcpy(p.f, f, sizeof(p.f));
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    if(g_Samp && g_Samp->test) { SampLog("  -> [%d] samp op %d a=%d b=%d val=%d %s", slot, op, a, b, val, p.text); return; }
    if(slot >= 0) SendTo(slot, &p, (int)sizeof(p));
}
void MPServer::SampMsg(int slot, uint32_t color, const std::string& s)
{
    if(g_Samp->test) { SampLog("  -> [%d] msg %08X %s", slot, color, s.c_str()); return; }
    // SA-MP lines are 144 chars, MP chat lines 128: longer ones go in two lines
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = color | 0xFF;
    size_t at = 0;
    do { MP_CopyStr(m.text, s.c_str() + at, sizeof(m.text)); SendTo(slot, &m, sizeof(m)); at += sizeof(m.text) - 1; } while(at < s.size());
}
void MPServer::SampGameText(int slot, const std::string& s, int ms, int style)
{
    SampAct(slot, CGA_GAMETEXT, style, 0, ms, nullptr, 0, s.c_str());
}
void MPServer::SampDialog(int slot, int id, int style, const std::string& cap, const std::string& info, const std::string& b1, const std::string& b2)
{
    SampPl& p = g_SampPl[slot];
    if(id < 0) { p.dialog = -1; CgDlg(slot, 0xFFFF, 0, "", "", "", ""); return; }
    p.dialog = id;
    if(g_Samp->test) { SampLog("  -> [%d] dialog %d style %d \"%s\" [%s|%s]\n%s", slot, id, style, cap.c_str(), b1.c_str(), b2.c_str(), info.c_str()); return; }
    AcDialogShown(slot, (uint16_t)id);
    CgDlg(slot, (uint16_t)id, (uint8_t)style, cap.c_str(), info.c_str(), b1.c_str(), b2.c_str());
}
void MPServer::SampKick(int slot, bool ban)
{
    SampLog("[samp] %s %s by the gamemode", g_SampPl[slot].name.c_str(), ban ? "banned" : "kicked");
    SampTimer t; t.id = g_Samp->nextTimer++; t.sc = nullptr; t.fn = "\x01kick"; t.interval = 150; t.repeat = false; t.next = MP_NowMs() + 150; t.args.push_back(AI(slot));
    g_Samp->timers.push_back(std::move(t));   // SA-MP sends the messages before the kick: wait a moment
}
void MPServer::SampWorld(int slot)
{
    SampOp(slot, SPO_WORLD, (uint8_t)g_SampPl[slot].interior, 0, g_SampPl[slot].world);
}
void MPServer::SampSpawn(int slot)
{
    SampPl& p = g_SampPl[slot];
    p.spectating = false; p.spawned = true; p.state = 1; p.hp = 100;
    if(p.spawn.set)
    {
        float pos[3] = { p.spawn.x, p.spawn.y, p.spawn.z };
        if(p.spawn.skin > 0) { p.skin = p.spawn.skin; SampAct(slot, CGA_SKIN, 0, 0, p.skin); }
        p.team = p.spawn.team;
        SampTele(slot, pos, p.spawn.a);
        for(int i = 0; i < 3; ++i) if(p.spawn.w[i] > 0) SampAct(slot, CGA_GUN, p.spawn.w[i], 0, p.spawn.am[i]);
    }
    SampAct(slot, CGA_FREEZE, 0);
    CallAll("OnPlayerSpawn", { AI(slot) });
}

// ---- textdraws: SA-MP ids -> the phone's 200 server textdraw slots ---------------------------------
int MPServer::SampTdSlot(int slot, int key, bool alloc)
{
    SampPl& p = g_SampPl[slot];
    for(int i = 0; i < SAMP_TD_SLOTS; ++i) if(p.tdSlot[i] == key) return i;
    if(!alloc) return -1;
    for(int i = 0; i < SAMP_TD_SLOTS; ++i) if(p.tdSlot[i] < 0) { p.tdSlot[i] = key; return i; }
    return -1;
}
void MPServer::SampTdShow(int slot, int key, bool show)
{
    int cs = SampTdSlot(slot, key, show); if(cs < 0) return;
    const SampTdDef* d = (key & 0x6000) == 0x6000 ? &g_SampPl[slot].ptd[key & 0xFFF] : &g_Samp->td[key & 0xFFF];
    PktTextDraw t = d->t; t.type = PKT_TEXTDRAW; t.tdId = (uint16_t)(SAMP_TD_BASE + cs); t.show = show ? 1 : 0; t.pad[0] = d->selectable ? 1 : 0;   // the phone makes it clickable while SelectTextDraw is on
    if(!show) g_SampPl[slot].tdSlot[cs] = -1;
    if(g_Samp->test) { SampLog("  -> [%d] textdraw %s #%d (%s) \"%s\"", slot, show ? "show" : "hide", cs, (key & 0x6000) == 0x6000 ? "player" : "global", t.text); return; }
    SendTo(slot, &t, sizeof(t));
}
void MPServer::SampTdChanged(int pl, int id, bool player)
{   // SA-MP: a change shows after the textdraw is shown again; TextDrawSetString updates at once
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!g_SampPl[i].on || (player && i != pl)) continue;
        int key = (player ? 0x6000 : 0x4000) | id;
        if(SampTdSlot(i, key, false) >= 0) SampTdShow(i, key, true);
    }
}

// ---- objects ------------------------------------------------------------------------------------
int MPServer::SampObjNew(int model, float x, float y, float z, float rx, float ry, float rz, int world, int interior)
{
    (void)world;
    DzMapObj o; memset(&o.r, 0, sizeof(o.r));
    o.r.id = m_nDzMapId++; o.r.model = (uint16_t)model; o.r.interior = (uint8_t)(interior < 0 ? 255 : interior); o.r.flags = 0;
    o.r.x = x; o.r.y = y; o.r.z = z; o.r.rx10 = (int16_t)(rx * 10); o.r.ry10 = (int16_t)(ry * 10); o.r.rz10 = (int16_t)(rz * 10);
    o.owner = "samp";
    m_DzMap.push_back(o);
    SampObj so; so.row = o.r.id; so.model = model; so.p[0] = x; so.p[1] = y; so.p[2] = z; so.r[0] = rx; so.r[1] = ry; so.r[2] = rz; so.world = world; so.interior = interior;
    int id = g_Samp->nextObj++; g_Samp->obj[id] = so;
    if(m_bSampLoaded && !g_Samp->test) DzMapSendOne(o.r, DZMO_SET);   // after the start: to everyone at once (the start list goes on join)
    return id;
}
int MPServer::SampObjDel(int id)
{
    auto it = g_Samp->obj.find(id); if(it == g_Samp->obj.end()) return 0;
    for(size_t i = 0; i < m_DzMap.size(); ++i) if(m_DzMap[i].r.id == it->second.row)
    {
        DzMapRow r = m_DzMap[i].r; m_DzMap.erase(m_DzMap.begin() + (long)i);
        if(!g_Samp->test) DzMapSendOne(r, DZMO_DEL);
        break;
    }
    g_Samp->obj.erase(it); return 1;
}
void MPServer::SampObjMove(int id, const float* p, const float* r)
{
    auto it = g_Samp->obj.find(id); if(it == g_Samp->obj.end()) return;
    memcpy(it->second.p, p, sizeof(float) * 3); memcpy(it->second.r, r, sizeof(float) * 3);
    for(auto& o : m_DzMap) if(o.r.id == it->second.row)
    {
        o.r.x = p[0]; o.r.y = p[1]; o.r.z = p[2]; o.r.rx10 = (int16_t)(r[0] * 10); o.r.ry10 = (int16_t)(r[1] * 10); o.r.rz10 = (int16_t)(r[2] * 10);
        if(!g_Samp->test) DzMapSendOne(o.r, DZMO_SET);
        break;
    }
}
void MPServer::SampLabelSend(int slot, int id)
{
    const SampLabel& l = g_Samp->labels[(size_t)id];
    float f[4] = { l.p[0], l.p[1], l.p[2], l.dist };
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!g_SampPl[i].on || (slot >= 0 && i != slot)) continue;
        PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = SPO_LABEL; p.a = l.used ? 1 : 0; p.val = id;
        memcpy(p.f, f, sizeof(f)); p.b = 0; MP_CopyStr(p.text, l.text.c_str(), sizeof(p.text));
        uint32_t col = l.color; memcpy(&p.text[sizeof(p.text) - 5], &col, 4); p.text[sizeof(p.text) - 6] = 0;
        if(g_Samp->test) continue;
        SendTo(i, &p, sizeof(p));
    }
}

// ---- loading ------------------------------------------------------------------------------------
static bool SampReadFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb"); if(!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n <= 0) { fclose(f); return false; }
    out.resize((size_t)n); bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n; fclose(f); return ok;
}
static std::string SampFindCi(const std::string& dir, const std::string& name)
{   // SA-MP folders come from Windows: the case of a name is anything
    DIR* d = opendir(dir.c_str()); if(!d) return std::string();
    std::string want = Lower(name), got; struct dirent* e;
    while((e = readdir(d))) if(Lower(e->d_name) == want) { got = dir + "/" + e->d_name; break; }
    closedir(d); return got;
}
bool MPServer::SampLoadScript(const std::string& path, const std::string& name, bool gm)
{
    std::unique_ptr<SampScript> sc(new SampScript());
    sc->name = name; sc->path = path; sc->gm = gm;
    std::vector<uint8_t> file;
    if(!SampReadFile(path, file) || file.size() < sizeof(AMX_HEADER)) { SampLog("[samp] can't read %s", path.c_str()); return false; }
    AMX_HEADER hdr; memcpy(&hdr, file.data(), sizeof(hdr));
    if(hdr.magic != AMX_MAGIC) { SampLog("[samp] %s is not a compiled Pawn script (.amx)", name.c_str()); return false; }
    if(hdr.stp <= 0 || hdr.stp > 256 * 1024 * 1024 || (size_t)hdr.size > file.size()) { SampLog("[samp] %s: bad header", name.c_str()); return false; }
    sc->mem.assign((size_t)hdr.stp, 0); memcpy(sc->mem.data(), file.data(), (size_t)hdr.size);
    memset(&sc->amx, 0, sizeof(sc->amx));
    int e = amx_Init(&sc->amx, sc->mem.data());
    if(e != AMX_ERR_NONE) { SampLog("[samp] %s: can't start the script (error %d %s)", name.c_str(), e, aux_StrError(e)); return false; }
    sc->amx.userdata[0] = sc.get();
    amx_SetCallback(&sc->amx, SampCallback);
    int nn = 0; amx_NumNatives(&sc->amx, &nn);
    for(int i = 0; i < nn; ++i) { char nm[64] = ""; amx_GetNative(&sc->amx, i, nm); sc->natives.push_back(nm); }
    amx_Register(&sc->amx, g_SampNatives, (int)(sizeof(g_SampNatives) / sizeof(g_SampNatives[0])));
    SampRegisterSscanf(&sc->amx); SampRegisterSql(&sc->amx);
    amx_CoreInit(&sc->amx); amx_FloatInit(&sc->amx); amx_StringInit(&sc->amx);
    sc->amx.flags |= AMX_FLAG_NTVREG;   // the rest are answered by SampCallback (0 + the report)
    sc->ok = true;
    int missing = 0;
    { AMX_HEADER* h = (AMX_HEADER*)sc->amx.base; for(int i = 0; i < nn; ++i) { AMX_FUNCSTUB* f = (AMX_FUNCSTUB*)(sc->amx.base + (int)h->natives + i * h->defsize); if(!f->address) ++missing; } }
    int np = 0; amx_NumPublics(&sc->amx, &np);
    SampLog("[samp] loaded %s %s: %d natives (%d not in MP yet), %d publics", gm ? "gamemode" : "filterscript", name.c_str(), nn, missing, np);
    if(gm) g_Samp->scripts.push_back(std::move(sc)); else g_Samp->scripts.insert(g_Samp->scripts.end() - (g_Samp->scripts.empty() || !g_Samp->scripts.back()->gm ? 0 : 1), std::move(sc));
    return true;
}
bool MPServer::SampLoad(const char* dir, bool test)
{
    SampUnload();
    g_SampSrv = this; g_Samp = new SampState(); g_Samp->root = dir; g_Samp->test = test;
    for(auto& p : g_SampPl) { p = SampPl(); for(int& t : p.tdSlot) t = -1; }
    // server.cfg (SA-MP) or config.json (open.mp: only the basic keys)
    std::string cfgPath = SampFindCi(dir, "server.cfg");
    FILE* f = cfgPath.empty() ? nullptr : fopen(cfgPath.c_str(), "rb");
    if(f)
    {
        char line[1024];
        while(fgets(line, sizeof(line), f))
        {
            char* p = line; while(*p == ' ' || *p == '\t') ++p;
            size_t n = strlen(p); while(n && (p[n - 1] == '\r' || p[n - 1] == '\n' || p[n - 1] == ' ')) p[--n] = 0;
            if(!*p || *p == '#' || (p[0] == '/' && p[1] == '/')) continue;
            char* sp = strpbrk(p, " \t"); std::string k = Lower(sp ? std::string(p, (size_t)(sp - p)) : std::string(p)), v;
            if(sp) { while(*sp == ' ' || *sp == '\t') ++sp; v = sp; }
            g_Samp->cfg[k] = v;
        }
        fclose(f);
    }
    g_Samp->hostName = Cfg("hostname");
    if(atoi(Cfg("maxplayers").c_str()) > 0) g_Samp->maxPlayers = std::min(MP_MAX_PLAYERS, atoi(Cfg("maxplayers").c_str()));
    // the gamemode: server.cfg gamemode0, else the only / first .amx in gamemodes/
    std::string gmDir = SampFindCi(dir, "gamemodes"), gm = Cfg("gamemode0");
    { size_t sp = gm.find(' '); if(sp != std::string::npos) gm = gm.substr(0, sp); }
    std::string gmPath;
    if(!gm.empty() && !gmDir.empty()) gmPath = SampFindCi(gmDir, gm + ".amx");
    if(gmPath.empty() && !gmDir.empty())
    {
        DIR* d = opendir(gmDir.c_str()); struct dirent* e;
        while(d && (e = readdir(d))) { std::string n = e->d_name; if(n.size() > 4 && Lower(n.substr(n.size() - 4)) == ".amx") { gmPath = gmDir + "/" + n; gm = n.substr(0, n.size() - 4); break; } }
        if(d) closedir(d);
    }
    if(gmPath.empty()) gmPath = SampFindCi(dir, gm.empty() ? "" : gm + ".amx");
    // filterscripts (before the gamemode, like SA-MP)
    std::string fsDir = SampFindCi(dir, "filterscripts");
    std::string fsList = Cfg("filterscripts");
    if(!fsDir.empty() && !fsList.empty())
    {
        size_t at = 0;
        while(at < fsList.size())
        {
            size_t e = fsList.find_first_of(" \t", at); std::string n = fsList.substr(at, e == std::string::npos ? std::string::npos : e - at);
            at = e == std::string::npos ? fsList.size() : e + 1;
            if(n.empty()) continue;
            std::string p = SampFindCi(fsDir, n + ".amx");
            if(p.empty()) { SampLog("[samp] filterscript %s.amx is not in filterscripts/ (only the .pwn? compile it with pawncc on a PC first)", n.c_str()); continue; }
            SampLoadScript(p, n, false);
        }
    }
    if(gmPath.empty()) SampLog("[samp] no gamemode .amx in %s/gamemodes (only .pwn? compile it on a PC with pawncc first)", dir);
    else SampLoadScript(gmPath, gm, true);
    if(g_Samp->scripts.empty()) { SampLog("[samp] nothing to run in %s", dir); SampUnload(); return false; }
    // Pawn.CMD: aliases / flags are publics pc_alias_* / pc_flags_* the plugin runs at the start
    for(auto& sc : g_Samp->scripts)
    {
        int np = 0; amx_NumPublics(&sc->amx, &np);
        for(int i = 0; i < np; ++i)
        {
            char nm[64] = ""; amx_GetPublic(&sc->amx, i, nm);
            if(!strncmp(nm, "pc_cmd_", 7)) g_Samp->pawnCmd = true;
        }
    }
    // the start: OnFilterScriptInit for each filterscript, then OnGameModeInit
    for(auto& sc : g_Samp->scripts) if(!sc->gm) CallPub(sc.get(), "OnFilterScriptInit", nullptr, {});
    for(auto& sc : g_Samp->scripts)
    {
        int np = 0; amx_NumPublics(&sc->amx, &np);
        for(int i = 0; i < np; ++i)
        {
            char nm[64] = ""; amx_GetPublic(&sc->amx, i, nm);
            if(!strncmp(nm, "pc_alias_", 9) || !strncmp(nm, "pc_flags_", 9)) CallPub(sc.get(), nm, nullptr, {});
        }
        CallPub(sc.get(), "PC_OnInit", nullptr, {});
    }
    for(auto& sc : g_Samp->scripts) if(sc->gm) CallPub(sc.get(), "OnGameModeInit", nullptr, {});
    m_bSampLoaded = true;
    SampLog("[samp] %s is running (%zu script(s), %zu objects, %d missing natives)", g_Samp->modeText.c_str(), g_Samp->scripts.size(), g_Samp->obj.size(), (int)g_Samp->unknown.size());
    SampReport();
    return true;
}
void MPServer::SampUnload()
{
    if(!g_Samp) return;
    if(m_bSampLoaded)
    {
        for(auto& sc : g_Samp->scripts) if(sc->gm) CallPub(sc.get(), "OnGameModeExit", nullptr, {});
        for(auto& sc : g_Samp->scripts) if(!sc->gm) CallPub(sc.get(), "OnFilterScriptExit", nullptr, {});
    }
    SampSqlClose();
    for(FILE* f : g_Samp->files) if(f) fclose(f);
    for(auto& o : g_Samp->obj) for(size_t i = 0; i < m_DzMap.size(); ++i) if(m_DzMap[i].r.id == o.second.row) { m_DzMap.erase(m_DzMap.begin() + (long)i); break; }
    for(auto& sc : g_Samp->scripts) amx_Cleanup(&sc->amx);
    delete g_Samp; g_Samp = nullptr; m_bSampLoaded = false;
}
void MPServer::SampReport()
{
    if(!g_Samp) return;
    g_Samp->reportDirty = false;
    std::string p = g_Samp->root + "/mp_samp_report.txt";
    FILE* f = fopen(p.c_str(), "wb"); if(!f) return;
    fprintf(f, "MP %s - SA-MP gamemode report\nGamemode: %s\nScripts:\n", MP_VERSION_STR, g_Samp->modeText.c_str());
    for(auto& sc : g_Samp->scripts) fprintf(f, "  %s %s (%zu natives)\n", sc->gm ? "gamemode" : "filterscript", sc->name.c_str(), sc->natives.size());
    fprintf(f, "Objects: %zu (SA-MP object models 11682-19999 need the SA-MP model pack on the phones)\n", g_Samp->obj.size());
    fprintf(f, "Natives the scripts called that MP doesn't do yet (they returned 0):\n");
    for(auto& u : g_Samp->unknown) fprintf(f, "  %-40s %d call(s)\n", u.first.c_str(), u.second);
    fclose(f);
}

// ---- events from the MP server --------------------------------------------------------------------
void MPServer::SampOnJoin(int slot)
{
    if(!m_bSampLoaded) return;
    SampPl& p = g_SampPl[slot];
    p = SampPl(); for(int& t : p.tdSlot) t = -1;
    p.on = true; p.name = m_Slots[slot].name; p.skin = m_Slots[slot].skin; p.color = m_Slots[slot].color; p.state = 0;
    if(m_Slots[slot].hasPos) memcpy(p.pos, m_Slots[slot].pos, sizeof(p.pos));
    SampOp(slot, SPO_HELLO, g_Samp->pawnCmd ? 1 : 0, 0, 0, nullptr, g_Samp->modeText.c_str());
    if(!g_Samp->test) DzMapSend(slot);   // the gamemode's objects (+ the server's own mapping)
    for(size_t i = 1; i < g_Samp->labels.size(); ++i) if(g_Samp->labels[i].used) SampLabelSend(slot, (int)i);
    // SA-MP order: OnIncomingConnection -> OnPlayerConnect -> class selection (OnPlayerRequestClass)
    CallAll("OnIncomingConnection", { AI(slot), AS(SampIp(slot)), AI(7777) });
    CallAll("OnPlayerConnect", { AI(slot) });
    if(!g_SampPl[slot].on) return;   // kicked
    cell r = CallAll("OnPlayerRequestClass", { AI(slot), AI(0) });
    // MP players are already in the world: they "spawn" unless the gamemode holds them (login dialog / spectating)
    if(r && !p.spectating && p.dialog < 0 && !p.spawned) p.spawnAt = MP_NowMs() + 1500;
}
void MPServer::SampOnLeave(int slot, int reason)
{
    if(!m_bSampLoaded || !g_SampPl[slot].on) return;
    CallAll("OnPlayerDisconnect", { AI(slot), AI(reason) });
    SampSqlLeave(slot);
    g_SampPl[slot].on = false; g_SampPl[slot].pvars.clear();
}
bool MPServer::SampOnChat(int slot, const char* text)
{
    if(!m_bSampLoaded || !g_SampPl[slot].on) return true;
    if(text[0] == '/') { SampCommand(slot, text); return false; }
    return CallAll("OnPlayerText", { AI(slot), AS(text) }, 0) != 0;
}
void MPServer::SampCommand(int slot, const char* text)
{
    if(!m_bSampLoaded || !g_SampPl[slot].on || text[0] != '/') return;
    if(!strncasecmp(text, "/mpsamp", 7) && (text[7] == 0 || text[7] == ' '))
    {   // 4.11: MP's own tools for the host (the gamemode never sees these)
        if(!SampIsAdmin(slot)) { SampMsg(slot, 0xFF6060FF, "/mpsamp is for the server owner / MP admins."); return; }
        std::string a = text[7] ? text + 8 : "";
        if(!strncasecmp(a.c_str(), "sql ", 4)) { std::string r = SampSqlConsole(a.substr(4)); for(size_t i = 0; i < r.size(); i += 140) SampMsg(slot, 0xA0E0FFFF, r.substr(i, 140)); }
        else if(!strcasecmp(a.c_str(), "report")) { SampReport(); SampMsg(slot, 0xA0E0FFFF, "Wrote mp_samp_report.txt in the gamemode folder (natives MP doesn't have yet, errors)."); }
        else if(!strcasecmp(a.c_str(), "info")) { char b[160]; snprintf(b, sizeof(b), "Gamemode %s - %zu scripts, %zu objects, folder %s", g_Samp->modeText.c_str(), g_Samp->scripts.size(), g_Samp->obj.size(), m_szSampDir); SampMsg(slot, 0xA0E0FFFF, b); }
        else SampMsg(slot, 0xA0E0FFFF, "/mpsamp sql <query> | report | info  (MySQL of the gamemode runs on SQLite: mp_mysql_<db>.db)");
        return;
    }
    std::string t = text;
    if(g_Samp->pawnCmd)
    {   // Pawn.CMD: OnPlayerCommandReceived -> pc_cmd_<name>(playerid, params[]) -> OnPlayerCommandPerformed
        size_t sp = t.find(' ');
        std::string cmd = Lower(t.substr(1, sp == std::string::npos ? std::string::npos : sp - 1));
        std::string prm = sp == std::string::npos ? "" : t.substr(sp + 1);
        while(!prm.empty() && prm[0] == ' ') prm.erase(0, 1);
        auto al = g_Samp->pcAlias.find(cmd); if(al != g_Samp->pcAlias.end()) cmd = al->second;
        int flags = g_Samp->pcFlags.count(cmd) ? g_Samp->pcFlags[cmd] : 0;
        bool handled = false;
        for(auto& sc : g_Samp->scripts)
        {
            int idx = FindPub(sc.get(), ("pc_cmd_" + cmd).c_str());
            cell ok = 1; bool hasRecv = CallPub(sc.get(), "OnPlayerCommandReceived", &ok, { AI(slot), AS(cmd), AS(prm), AI(flags) });
            if(idx < 0 && !hasRecv) continue;
            if(hasRecv && !ok) { handled = true; break; }
            cell res = -1;
            if(idx >= 0) { if(!CallPub(sc.get(), ("pc_cmd_" + cmd).c_str(), &res, { AI(slot), AS(prm.empty() ? "\1" : prm) })) res = -1; }
            cell perf = res; bool hasPerf = CallPub(sc.get(), "OnPlayerCommandPerformed", &perf, { AI(slot), AS(cmd), AS(prm), AI(res), AI(flags) });
            if(idx >= 0 || (hasPerf && perf)) { handled = true; if(hasPerf && !perf && idx >= 0) {} break; }
        }
        if(handled) return;
    }
    if(CallAll("OnPlayerCommandText", { AI(slot), AS(t) }, 1, 0) == 0)
        SampMsg(slot, 0xFFFFFFFF, "SERVER: Unknown command.");
}
void MPServer::SampOnDialog(int slot, const PktDialogResp& r)
{
    if(!m_bSampLoaded || !g_SampPl[slot].on) return;
    SampPl& p = g_SampPl[slot];
    if(p.dialog < 0 || p.dialog != r.dialogId) return;   // SA-MP: an answer to a dialog that isn't shown is dropped
    p.dialog = -1;
    char in[65]; MP_CopyStr(in, r.input, sizeof(in));
    CallAll("OnDialogResponse", { AI(slot), AI(r.dialogId), AI(r.response ? 1 : 0), AI(r.listItem), AS(in) }, 1, 0);
    if(!p.spawned && !p.spectating && p.dialog < 0 && !p.spawnAt) {}   // the gamemode spawns them itself
}
void MPServer::SampOnSync(int slot, const SyncData& d)
{
    if(!m_bSampLoaded || !g_SampPl[slot].on) return;
    SampPl& p = g_SampPl[slot];
    memcpy(p.pos, (d.flags & SYNC_IN_CAR) ? d.carPos : d.pos, sizeof(p.pos));
    p.a = (d.flags & SYNC_IN_CAR) ? d.carHeading : d.heading;
    p.hp = d.health; p.ar = d.armour; p.weapon = d.weapon; p.skin = d.skin;
    int st = (d.flags & SYNC_DEAD) ? 7 : (d.flags & SYNC_IN_CAR) ? ((d.flags & SYNC_DRIVER) ? 2 : 3) : (p.spawned ? 1 : p.state);
    if(st != p.state)
    {
        int old = p.state; p.state = st;
        if(st == 2 || st == 3)
        {   // a car of the gamemode (CreateVehicle) at that place? -> GetPlayerVehicleID = it
            p.vehicle = 0;
            for(size_t i = 1; i < g_Samp->veh.size(); ++i) { const SampVeh& v = g_Samp->veh[i]; if(v.used && v.model == d.carModel) { float dx = v.x - d.carPos[0], dy = v.y - d.carPos[1]; if(dx * dx + dy * dy < 400) { p.vehicle = (int)i; break; } } }
            if(!p.vehicle) p.vehicle = VehNew(d.carModel, d.carPos[0], d.carPos[1], d.carPos[2], d.carHeading, -1, -1);
            CallAll("OnPlayerEnterVehicle", { AI(slot), AI(p.vehicle), AI(st == 3) });
        }
        else if(old == 2 || old == 3) CallAll("OnPlayerExitVehicle", { AI(slot), AI(p.vehicle) });
        CallAll("OnPlayerStateChange", { AI(slot), AI(st), AI(old) });
        if(st != 2 && st != 3) p.vehicle = 0;
    }
    if(p.vehicle) { SampVeh* v = VH(p.vehicle); if(v) { v->x = d.carPos[0]; v->y = d.carPos[1]; v->z = d.carPos[2]; v->a = d.carHeading; } }
    // checkpoints (the phone also tells, this is the fallback)
    auto inCp = [&](const float* c) { float dx = p.pos[0] - c[0], dy = p.pos[1] - c[1], dz = p.pos[2] - c[2]; float r = c[3] > 0.5f ? c[3] : 2.0f; return dx * dx + dy * dy < r * r && fabsf(dz) < 4.0f; };
    if(p.cp) { bool in = inCp(p.cpPos); if(in != p.inCp) { p.inCp = in; CallAll(in ? "OnPlayerEnterCheckpoint" : "OnPlayerLeaveCheckpoint", { AI(slot) }); } }
    if(p.rcp) { bool in = inCp(p.rcpPos); if(in != p.inRcp) { p.inRcp = in; CallAll(in ? "OnPlayerEnterRaceCheckpoint" : "OnPlayerLeaveRaceCheckpoint", { AI(slot) }); } }
    uint64_t now = MP_NowMs();
    if(now - p.lastUpdate >= 200) { p.lastUpdate = now; CallAll("OnPlayerUpdate", { AI(slot) }, 0); }
}
void MPServer::SampOnDeath(int victim, int killer, int weapon)
{
    if(!m_bSampLoaded || !g_SampPl[victim].on) return;
    g_SampPl[victim].state = 7; g_SampPl[victim].spawned = false;
    CallAll("OnPlayerDeath", { AI(victim), AI(killer < 0 ? 0xFFFF : killer), AI(weapon) });
    g_SampPl[victim].spawnAt = MP_NowMs() + 4000;   // MP respawns the ped itself; then OnPlayerSpawn
}
void MPServer::SampHandle(int slot, const uint8_t* data, int len)
{
    if(!m_bSampLoaded || slot < 0 || !g_SampPl[slot].on || len < (int)offsetof(PktSamp, text)) return;
    PktSamp p; memset(&p, 0, sizeof(p)); memcpy(&p, data, std::min<int>(len, (int)sizeof(p))); p.text[sizeof(p.text) - 1] = 0;
    switch(p.op)
    {
        case SPO_CMD: SampCommand(slot, p.text); break;
        case SPO_TDCLICK:
        {
            if(p.val < 0) { CallAll("OnPlayerClickTextDraw", { AI(slot), AI(SAMP_INVALID_TD) }, 1, 0); break; }
            int cs = p.val - SAMP_TD_BASE; if(cs < 0 || cs >= SAMP_TD_SLOTS) break;
            int key = g_SampPl[slot].tdSlot[cs]; if(key < 0) break;
            if((key & 0x6000) == 0x6000) CallAll("OnPlayerClickPlayerTextDraw", { AI(slot), AI(key & 0xFFF) }, 1, 0);
            else CallAll("OnPlayerClickTextDraw", { AI(slot), AI(key & 0xFFF) }, 1, 0);
            break;
        }
        case SPO_CPENTER:
        {
            SampPl& s = g_SampPl[slot]; bool in = p.a != 0;
            if(p.b) { if(s.rcp && in != s.inRcp) { s.inRcp = in; CallAll(in ? "OnPlayerEnterRaceCheckpoint" : "OnPlayerLeaveRaceCheckpoint", { AI(slot) }); } }
            else if(s.cp && in != s.inCp) { s.inCp = in; CallAll(in ? "OnPlayerEnterCheckpoint" : "OnPlayerLeaveCheckpoint", { AI(slot) }); }
            break;
        }
        case SPO_KEYS:
        {
            SampPl& s = g_SampPl[slot]; int old = s.keys; s.keys = p.val;
            if(old != s.keys) CallAll("OnPlayerKeyStateChange", { AI(slot), AI(s.keys), AI(old) });
            break;
        }
        default: break;
    }
}
void MPServer::SampTick(uint64_t now)
{
    if(!m_bSampLoaded) return;
    // timers (a timer may add / kill timers: index loop, new ones run next tick)
    size_t n = g_Samp->timers.size();
    for(size_t i = 0; i < n && i < g_Samp->timers.size(); ++i)
    {
        if(g_Samp->timers[i].dead || g_Samp->timers[i].next > now) continue;
        SampTimer t = g_Samp->timers[i];
        if(t.repeat) { g_Samp->timers[i].next = now + t.interval; } else g_Samp->timers[i].dead = true;
        if(t.fn[0] == '\x01')
        {
            if(t.fn == "\x01kick") { int s = (int)t.args[0].v; if(g_SampPl[s].on && !g_Samp->test) Drop(s, LEAVE_KICK); else if(g_Samp->test) SampOnLeave(s, 2); }
            else if(t.fn == "\x01moved") { CallAll("OnObjectMoved", { AI(t.args[0].v) }); CallAll("OnDynamicObjectMoved", { AI(t.args[0].v) }); }
            else if(t.fn == "\x01sql") SampSqlCallback(t);
            continue;
        }
        CallPub(t.sc, t.fn.c_str(), nullptr, t.args);
    }
    g_Samp->timers.erase(std::remove_if(g_Samp->timers.begin(), g_Samp->timers.end(), [](const SampTimer& t) { return t.dead; }), g_Samp->timers.end());
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        SampPl& p = g_SampPl[i];
        if(p.on && p.spawnAt && now >= p.spawnAt) { p.spawnAt = 0; if(!p.spectating && p.dialog < 0) SampSpawn(i); }
    }
    if(g_Samp->reportDirty && now - g_Samp->reportAt > 30000) { g_Samp->reportAt = now; SampReport(); }
}
std::string MPServer::SampModeName() const { return g_Samp ? g_Samp->modeText : std::string(); }

// ---- mp_server --samp-test <folder>: runs the gamemode with one pretend player (no network) ----------
int MPServer::SampSelfTest(const char* dir, const char* cmds)
{
    if(!SampLoad(dir, true)) return 1;
    int slot = 0;
    memset(&m_Slots[slot], 0, sizeof(m_Slots[slot]));
    m_Slots[slot].used = true; MP_CopyStr(m_Slots[slot].name, "Tester", sizeof(m_Slots[slot].name)); m_Slots[slot].joinedMs = MP_NowMs();
    m_Slots[slot].pos[0] = 1958.33f; m_Slots[slot].pos[1] = 1343.12f; m_Slots[slot].pos[2] = 15.36f; m_Slots[slot].hasPos = true;
    SampLog("---- player 0 (Tester) joins ----");
    SampOnJoin(slot);
    uint64_t t0 = MP_NowMs();
    std::string list = cmds ? cmds : "";
    size_t at = 0;
    while(MP_NowMs() - t0 < 6000)
    {
        SampTick(MP_NowMs());
        if(MP_NowMs() - t0 > 2500 && at < list.size())
        {
            size_t e = list.find(';', at); std::string c = list.substr(at, e == std::string::npos ? std::string::npos : e - at); at = e == std::string::npos ? list.size() : e + 1;
            if(c.rfind("dlg:", 0) == 0)
            {   // dlg:<response>:<listitem>:<text>
                PktDialogResp r; memset(&r, 0, sizeof(r)); r.dialogId = (uint16_t)std::max(0, g_SampPl[slot].dialog);
                int resp = 1, li = 0; char txt[64] = ""; sscanf(c.c_str() + 4, "%d:%d:%63[^\n]", &resp, &li, txt);
                r.response = (uint8_t)resp; r.listItem = (int16_t)li; MP_CopyStr(r.input, txt, sizeof(r.input));
                SampLog("---- dialog answer %d/%d \"%s\" ----", resp, li, txt); SampOnDialog(slot, r);
            }
            else if(!c.empty()) { SampLog("---- player types: %s ----", c.c_str()); if(!SampOnChat(slot, c.c_str())) {} }
        }
        usleep(20000);
    }
    SampLog("---- player 0 leaves ----");
    SampOnLeave(slot, 1);
    SampReport();
    SampLog("[samp] report: %s/mp_samp_report.txt", dir);
    SampUnload();
    return 0;
}
