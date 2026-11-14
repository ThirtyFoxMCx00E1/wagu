// 4.13.36: MP for GTA Vice City (libGTAVC.so). The same libMP32.so / libMP64.so runs in both games: in San Andreas
// the SA code starts (main.cpp OnModLoad), in Vice City only this file runs. Nothing here touches SA code or SA
// addresses: every game function is found by its exported name in libGTAVC.so (vc-android-sdk), and the class
// layouts come from reVC (ThirtyFoxMCx00E1/GTAviceCity: CPlaceable = vtable + CMatrix, position at +0x30 of the matrix).
// The network, the server and the protocol are MP's own (net/client.cpp); the phone tells the server it is a Vice City
// phone (PktJoin6.game = MP_GAME_VC), so it only joins Vice City servers (mp_server --game vc) and never meets SA players.
//
// What works in this first Vice City version:
//   - join a Vice City server from TYFMC/MP/vc.ini (Host / Port / Name / Password), auto re-join
//   - your position / heading / car sent 10 times a second; every other player is a ped in your game
//     (smoothly moved, re-made when the game removes it), with name tag and radar blip
//   - server and chat messages on the screen, join / leave / kill messages
//   - hospital safe zones of Vice City (net/safezone.h, the server drops hits there)
//   - mission / cutscene status of every player ("is on The Party", "watches a cutscene"), mission names from main.scm
//   - 4.13.36 b: Vice City script engine (opcodes, CRunningScript layout read from the real libGTAVC.so 32 / 64-bit),
//     real health / weapon / car model in the sync, the other players' cars (same model, locked, proof), time + weather
//     of the server, chat typing (Chat button -> phone keyboard, /commands go to the server), server dialogs
//     (message / input / list / password), damage + death, admin health / armour / money / teleport, name colours
// Not yet (next phases, docs/VICE_CITY_PLAN.md): the other players' weapons in their hands, bullet sync, MP menus.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string>
#include <vector>
#include "../net/client.h"
#include "../net/server.h"     // MP_NowMs
#include "../net/safezone.h"
#include "vc_data.h"
#include "../ui/filepick.h"   // the phone keyboard dialog (game independent)

namespace VC
{
// ---- game types (reVC layouts) ------------------------------------------------------------------------------------
struct CVector { float x, y, z; };
struct RwV3d   { float x, y, z; };
struct CRGBA   { uint8_t r, g, b, a; };
// CPlaceable: vtable, then CMatrix (RwMatrix: right, flags, up(= forward), pad, at, pad, pos, pad)
static inline float* MatRight(void* e) { return (float*)((uint8_t*)e + sizeof(void*) + 0x00); }
static inline float* MatFwd(void* e)   { return (float*)((uint8_t*)e + sizeof(void*) + 0x10); }
static inline float* MatPos(void* e)   { return (float*)((uint8_t*)e + sizeof(void*) + 0x30); }

enum { PEDTYPE_CIVMALE = 4, MI_MALE01 = 7 };

// ---- libGTAVC.so functions (by name) ------------------------------------------------------------------------------
static void* (*FindPlayerPed)() = nullptr;
static void* (*FindPlayerVehicle)() = nullptr;
static void* (*AddPed)(int type, unsigned int mi, const CVector& at, int unk) = nullptr;
static void  (*RemovePed)(void* ped) = nullptr;
static int   (*GetPedRef)(void* ped) = nullptr;
static void* (*GetPed)(int ref) = nullptr;
static void  (*PedTeleport)(void* ped, CVector at) = nullptr;
static void  (*SetHeading)(void* ent, float rad) = nullptr;
static void  (*RequestModel)(int mi, int flags) = nullptr;
static void  (*LoadAllRequestedModels)(bool onlyPriority) = nullptr;
static void  (*SetHelpMessage)(uint16_t* text, bool quick, bool showAlways, bool) = nullptr;
static void  (*FontSetScale)(float, float) = nullptr;
// CFont::SetColor(CRGBA) / SetDropColor(CRGBA) take the colour BY VALUE (symbol ...E5CRGBA, reVC Font.h): a 4-byte struct
// goes in r0 / w0. The vc-android-sdk examples pass a CRGBA* - the game then reads the pointer's bits as the colour.
static void  (*FontSetColor)(CRGBA) = nullptr;
static void  (*FontSetDropColor)(CRGBA) = nullptr;
static void  (*FontSetDropShadow)(short) = nullptr;
static void  (*FontSetStyle)(short) = nullptr;
static void  (*FontSetJustifyOn)() = nullptr;
static void  (*FontPrint)(float, float, uint16_t*) = nullptr;
static bool  (*CalcScreenCoors)(const RwV3d& in, RwV3d* out, float* w, float* h, bool farClip) = nullptr;
static int   (*ScreenW)() = nullptr;
static int   (*ScreenH)() = nullptr;
static int   (*SetCoordBlip)(int type, CVector at, unsigned int color, int display) = nullptr;
static void  (*ClearBlip)(int idx) = nullptr;
static int*     pOnMissionFlag = nullptr;   // CTheScripts::OnAMissionFlag (offset into ScriptSpace)
static uint8_t* pScriptSpace = nullptr;     // CTheScripts::ScriptSpace
static bool*    pCutsceneRunning = nullptr; // CCutsceneMgr::ms_running
static void  (*ProcessOneCommand)(void* script) = nullptr;   // CRunningScript::ProcessOneCommand
static int   (*GetVehicleRef)(void* veh) = nullptr;
static void* (*GetVehicle)(int ref) = nullptr;
static int   (*PointerGetButton)(int finger, int button) = nullptr;
static void  (*PointerGetCoords)(int finger, int* x, int* y, float* pressure) = nullptr;
static int   s_nTeleportSlot = -1;                            // CEntity::Teleport(CVector) vtable slot (virtual)

// ---- the Vice City script engine ----------------------------------------------------------------------------------
// CRunningScript of GTA VC Android, read from libGTAVC.so (ProcessOneCommand / UpdateCompareFlag / CollectParameters,
// both ABIs) - the same as reVC: m_nIp is a uint32 OFFSET into CTheScripts::ScriptSpace (not a pointer like SA),
//                        armv7   arm64
//   m_nIp                0x10    0x18
//   m_anLocalVariables   0x30    0x38   (int32[18])
//   m_bCondResult        0x79    0x81
//   m_nAndOrState        0x80    0x88   (uint16)
//   m_bNotFlag           0x82    0x8A
// Parameter types (CollectParameters jump table): 1 = int32, 3 = local var (uint16 index), 6 = float (4 bytes on mobile).
// So our command bytes must be reachable as ScriptSpace + uint32: on arm64 they live in a page mapped above ScriptSpace.
#ifdef AML32
  enum { RS_IP = 0x10, RS_LOCALS = 0x30, RS_COND = 0x79, RS_ANDOR = 0x80, RS_NOT = 0x82 };
#else
  enum { RS_IP = 0x18, RS_LOCALS = 0x38, RS_COND = 0x81, RS_ANDOR = 0x88, RS_NOT = 0x8A };
#endif
alignas(16) static uint8_t s_Thread[0x200];
static uint8_t* s_pCmd = nullptr;   // 256 bytes of command space
static bool InitScripts()
{
    if(!ProcessOneCommand || !pScriptSpace) return false;
#ifdef AML32
    static uint8_t buf[256]; s_pCmd = buf;   // 32-bit: ScriptSpace + ip wraps around 2^32 - any address works
#else
    const uintptr_t base = (uintptr_t)pScriptSpace;
    for(uintptr_t off = 0x10000000; off < 0xF0000000 && !s_pCmd; off += 0x10000000)
    {
        void* want = (void*)((base + off) & ~(uintptr_t)0xFFFF);
        void* m = mmap(want, 0x1000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(m == MAP_FAILED) continue;
        if((uintptr_t)m > base && (uintptr_t)m - base < 0xFFFFF000ull) s_pCmd = (uint8_t*)m;
        else munmap(m, 0x1000);
    }
    if(!s_pCmd) { logger->Info("Vice City: no command page within 4 GB of ScriptSpace - script commands off"); return false; }
#endif
    memset(s_Thread, 0, sizeof(s_Thread));
    return true;
}
// fmt: i = int, f = float, v = int* / float* out (local var)
static bool Cmd(uint16_t op, const char* fmt, ...)
{
    if(!s_pCmd) return false;
    va_list ap; va_start(ap, fmt);
    uint8_t* b = s_pCmd; int n = 0; int nv = 0; uint32_t* outs[16];
    int32_t* locals = (int32_t*)(s_Thread + RS_LOCALS);
    memset(locals, 0, 18 * 4);
    b[n++] = (uint8_t)op; b[n++] = (uint8_t)(op >> 8);
    for(const char* f = fmt; *f && n < 240; ++f)
    {
        if(*f == 'i') { int32_t v = va_arg(ap, int); b[n++] = 1; memcpy(b + n, &v, 4); n += 4; }
        else if(*f == 'f') { float v = (float)va_arg(ap, double); b[n++] = 6; memcpy(b + n, &v, 4); n += 4; }
        else if(*f == 'v' && nv < 16) { outs[nv] = va_arg(ap, uint32_t*); locals[nv] = (int32_t)*outs[nv]; b[n++] = 3; b[n++] = (uint8_t)nv; b[n++] = 0; ++nv; }
    }
    va_end(ap);
    *(uint32_t*)(s_Thread + RS_IP) = (uint32_t)((uintptr_t)s_pCmd - (uintptr_t)pScriptSpace);
    s_Thread[RS_COND] = 0; *(uint16_t*)(s_Thread + RS_ANDOR) = 0; s_Thread[RS_NOT] = 0;
    ProcessOneCommand(s_Thread);
    for(int i = 0; i < nv; ++i) *outs[i] = (uint32_t)locals[i];
    return s_Thread[RS_COND] != 0;
}
// opcodes: numbers and parameter counts from reVC (src/control/ScriptCommands.h + Script*.cpp)
enum : uint16_t
{
    OP_SET_PLAYER_COORDINATES = 0x0055, OP_CREATE_CAR = 0x00A5, OP_DELETE_CAR = 0x00A6, OP_CAR_SET_IDLE = 0x00A9,
    OP_SET_TIME_OF_DAY = 0x00C0, OP_ADD_SCORE = 0x0109, OP_IS_CAR_DEAD = 0x0119, OP_SET_PLAYER_HEADING = 0x0171,
    OP_FORCE_WEATHER_NOW = 0x01B6, OP_LOCK_CAR_DOORS = 0x020A, OP_SET_PLAYER_HEALTH = 0x0222, OP_GET_PLAYER_HEALTH = 0x0225,
    OP_GET_CAR_HEALTH = 0x0227, OP_CHANGE_CAR_COLOUR = 0x0229, OP_REQUEST_MODEL = 0x0247, OP_HAS_MODEL_LOADED = 0x0248,
    OP_SET_CHAR_PROOFS = 0x02AB, OP_SET_CAR_PROOFS = 0x02AC, OP_SET_CHAR_STAY_IN_SAME_PLACE = 0x0350,
    OP_ADD_ARMOUR_TO_PLAYER = 0x035E, OP_WARP_CHAR_FROM_CAR_TO_COORD = 0x0362, OP_WARP_CHAR_INTO_CAR = 0x036A,
    OP_LOAD_ALL_MODELS_NOW = 0x038B, OP_GET_CAR_MODEL = 0x0441, OP_GET_CURRENT_PLAYER_WEAPON = 0x046F,
};
static bool IsVcCarModel(int m) { return m >= 130 && m <= 236; }

static void* s_hGame = nullptr;
template<typename T> static bool Sym(T& fn, const char* name, int& missing)
{
    fn = (T)aml->GetSym(s_hGame, name);
    if(!fn) { ++missing; logger->Info("Vice City: %s not found (that part is off)", name); }
    return fn != nullptr;
}

// ---- settings: TYFMC/MP/vc.ini ------------------------------------------------------------------------------------
struct Settings
{
    char host[128] = "";
    int  port = 7777;
    char name[MP_NAME_LEN] = "Player";
    char password[MP_PASSWORD_LEN + 1] = "";
    int  remoteSkin = MI_MALE01;
    float textScale = 1.0f;
    int  blips = 1;
} cfg;

static void Trim(char* s)
{
    char* e = s + strlen(s);
    while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0;
    char* b = s; while(*b == ' ' || *b == '\t') ++b;
    if(b != s) memmove(s, b, strlen(b) + 1);
}
static void LoadSettings()
{
    const char* base = aml->GetAndroidDataPath();
    char dir[512], path[600];
    snprintf(dir, sizeof(dir), "%s/TYFMC", base ? base : ""); mkdir(dir, 0777);
    snprintf(dir, sizeof(dir), "%s/TYFMC/MP", base ? base : ""); mkdir(dir, 0777);
    snprintf(path, sizeof(path), "%s/vc.ini", dir);
    FILE* f = fopen(path, "r");
    if(!f)
    {
        f = fopen(path, "w");
        if(f)
        {
            fputs("; MP for GTA Vice City (4.13.36). Only Vice City servers (mp_server --game vc) take a Vice City phone.\n"
                  "[VC]\n"
                  "; the server to join when the game starts (empty = play alone)\n"
                  "Host=\n"
                  "Port=7777\n"
                  "Name=Player\n"
                  "Password=\n"
                  "; model of the other players' peds (7 = a Vice City man)\n"
                  "RemoteSkin=7\n"
                  "; text size on the screen\n"
                  "TextScale=1.0\n"
                  "; other players on the radar\n"
                  "Blips=1\n", f);
            fclose(f);
        }
        logger->Info("Vice City: wrote %s (set Host= to join a Vice City server)", path);
        return;
    }
    char line[256];
    while(fgets(line, sizeof(line), f))
    {
        char* eq = strchr(line, '=');
        if(line[0] == ';' || line[0] == '#' || line[0] == '[' || !eq) continue;
        *eq = 0; char* k = line; char* v = eq + 1; Trim(k); Trim(v);
        if(!strcasecmp(k, "Host")) MP_CopyStr(cfg.host, v, sizeof(cfg.host));
        else if(!strcasecmp(k, "Port")) cfg.port = atoi(v) > 0 ? atoi(v) : 7777;
        else if(!strcasecmp(k, "Name") && v[0]) MP_CopyStr(cfg.name, v, sizeof(cfg.name));
        else if(!strcasecmp(k, "Password")) MP_CopyStr(cfg.password, v, sizeof(cfg.password));
        else if(!strcasecmp(k, "RemoteSkin")) cfg.remoteSkin = atoi(v) > 0 && atoi(v) < 110 ? atoi(v) : MI_MALE01;
        else if(!strcasecmp(k, "TextScale")) cfg.textScale = atof(v) > 0.2f && atof(v) < 5.0f ? (float)atof(v) : 1.0f;
        else if(!strcasecmp(k, "Blips")) cfg.blips = atoi(v);
    }
    fclose(f);
    logger->Info("Vice City: vc.ini host '%s' port %d name '%s'", cfg.host, cfg.port, cfg.name);
}

// ---- state --------------------------------------------------------------------------------------------------------
struct Remote
{
    bool    used = false;
    char    name[MP_NAME_LEN] = "";
    uint32_t color = 0xFFFFFFFF;
    bool    hasSync = false;
    float   target[3] = {0, 0, 0}, cur[3] = {0, 0, 0};
    float   heading = 0;          // degrees
    bool    inCar = false;
    int     pedRef = -1;          // CPools handle (the game may remove the ped: the handle says so)
    int     blip = -1;
    uint8_t status = 0;
    char    mission[9] = "";
    int     carModel = 0;         // 4.13.36 b: the car they drive (Vice City model 130..236)
    int     carRef = -1;          // our copy of it
    bool    named = false;
};
static Remote s_Players[256];
static MPClient s_Client;
static int s_nMyId = -1;
static bool s_bJoinTried = false;
static uint64_t s_nNextJoin = 0, s_nNextSync = 0, s_nNextBlips = 0, s_nLastFrame = 0;
static uint32_t s_nSeq = 0;
static int s_nSafeZone = -1;
static uint8_t s_nMyStatus = 0;
static uint64_t s_nNextStatus = 0;
static bool s_bModelReq = false;
static bool s_bDead = false;
static int  s_nLastHour = -1, s_nLastWeather = -1;
static bool s_bTyping = false;
struct PendingDialog { bool open = false; uint16_t id = 0; uint8_t style = 0; int items = 0; };
static PendingDialog s_Dialog;
static int s_nChatBtn[4] = { 0, 0, 0, 0 };   // x, y, w, h of the Chat button (pixels)
static uint32_t RgbaToAbgr(uint32_t c) { return 0xFF000000u | ((c >> 24) & 0xFF) | (((c >> 16) & 0xFF) << 8) | (((c >> 8) & 0xFF) << 16); }
static int MapWeather(int sa)
{   // San Andreas weather ids -> Vice City (0 sunny, 1 cloudy, 2 rainy, 3 foggy, 4 extra sunny, 5 hurricane)
    switch(sa) { case 8: case 16: return 2; case 9: case 19: return 3; case 4: case 7: case 12: case 15: return 1;
                 case 11: case 17: case 18: return 4; default: return 0; }
}

struct ChatLine { std::string text; uint32_t color; uint64_t at; };
static std::vector<ChatLine> s_Chat;
static void Chat(uint32_t color, const char* fmt, ...)
{
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    s_Chat.push_back({ buf, color, MP_NowMs() });
    if(s_Chat.size() > 8) s_Chat.erase(s_Chat.begin());
    logger->Info("Vice City chat: %s", buf);
}
static void Help(const char* text)
{
    if(!SetHelpMessage) return;
    static uint16_t w[200]; int i = 0;
    for(; text[i] && i < 199; ++i) w[i] = (uint8_t)text[i];
    w[i] = 0;
    SetHelpMessage(w, false, false, false);
}

static void* RemotePed(Remote& r) { return (r.pedRef >= 0 && GetPed) ? GetPed(r.pedRef) : nullptr; }
static void DropRemote(Remote& r)
{
    void* p = RemotePed(r);
    if(r.carRef >= 0 && GetVehicle && GetVehicle(r.carRef))
    {
        if(p) { float* q = MatPos(p); Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", r.pedRef, q[0], q[1], q[2]); }
        Cmd(OP_DELETE_CAR, "i", r.carRef);
    }
    p = RemotePed(r);
    if(p && RemovePed) RemovePed(p);
    if(r.blip >= 0 && ClearBlip) ClearBlip(r.blip);
    r = Remote();
}
static void DropAll() { for(auto& r : s_Players) if(r.used) DropRemote(r); s_nMyId = -1; }

// ---- network events -----------------------------------------------------------------------------------------------
class Events : public INetEvents
{
public:
    void OnConnected(const PktWelcome& w) override
    {
        s_nMyId = w.yourId;
        char sn[33]; memcpy(sn, w.serverName, 32); sn[32] = 0;
        Chat(0xFF80FF80, "Connected to %s (Vice City)", sn);
        Help("MP Vice City: connected");
    }
    void OnRejected(uint8_t reason) override
    {
        const char* why = reason == REJECT_WRONG_GAME ? "this server is a GTA San Andreas server" :
                          reason == REJECT_PASSWORD ? "wrong password (vc.ini Password=)" :
                          reason == REJECT_FULL ? "the server is full" :
                          reason == REJECT_OUTDATED ? "update MP" : "refused";
        Chat(0xFF6060FF, "Join refused: %s", why);
        DropAll(); s_nNextJoin = MP_NowMs() + 30000;
    }
    void OnConnectFailed() override { Chat(0xFF6060FF, "No answer from %s:%d", cfg.host, cfg.port); s_nNextJoin = MP_NowMs() + 15000; }
    void OnLostConnection() override { Chat(0xFF6060FF, "Lost the connection - joining again"); DropAll(); s_nNextJoin = MP_NowMs() + 5000; }
    void OnPlayerJoin(const PktPlayerJoin& p) override
    {
        if(p.id == s_nMyId) return;
        Remote& r = s_Players[p.id];
        if(r.used) DropRemote(r);
        r.used = true; memcpy(r.name, p.name, MP_NAME_LEN); r.name[MP_NAME_LEN - 1] = 0; r.color = RgbaToAbgr(p.color); r.named = true;
        Chat(0xFFC0C0C0, "%s joined", r.name);
    }
    void OnPlayerLeave(const PktPlayerLeave& p) override
    {
        Remote& r = s_Players[p.id];
        if(!r.used) return;
        Chat(0xFFC0C0C0, "%s left", r.name);
        DropRemote(r);
    }
    void OnSync(const PktSync& p, const SyncExt*) override
    {
        if(p.id == s_nMyId) return;
        Remote& r = s_Players[p.id];
        if(!r.used) { r.used = true; snprintf(r.name, sizeof(r.name), "Player %d", p.id); }
        r.inCar = (p.d.flags & SYNC_IN_CAR) != 0;
        r.carModel = r.inCar ? p.d.carModel : 0;
        const float* at = r.inCar ? p.d.carPos : p.d.pos;
        r.target[0] = at[0]; r.target[1] = at[1]; r.target[2] = at[2];
        r.heading = r.inCar ? p.d.carHeading : p.d.heading;
        if(!r.hasSync) { memcpy(r.cur, r.target, sizeof(r.cur)); r.hasSync = true; }
    }
    void OnChat(const PktChat& p) override
    {
        char t[MP_CHAT_LEN + 1]; memcpy(t, p.text, MP_CHAT_LEN); t[MP_CHAT_LEN] = 0;
        const char* n = p.id == s_nMyId ? cfg.name : (s_Players[p.id].used ? s_Players[p.id].name : "?");
        Chat(0xFFFFFFFF, "%s: %s", n, t);
    }
    void OnDeath(const PktDeath& p) override
    {
        auto nm = [](int id) -> const char* { return id == s_nMyId ? cfg.name : (s_Players[id].used ? s_Players[id].name : "someone"); };
        if(p.killer != p.victim && p.killer < 255) Chat(0xFF8080FF, "%s killed %s", nm(p.killer), nm(p.victim));
        else Chat(0xFF8080FF, "%s died", nm(p.victim));
    }
    void OnDamage(const PktDamage& d) override
    {   // the server only sends hits it allowed (not in a safe zone, not a team mate)
        if(d.target != s_nMyId || s_bDead || d.amount <= 0) return;
        uint32_t h = 0; Cmd(OP_GET_PLAYER_HEALTH, "iv", 0, &h);
        int nh = (int)h - (int)(d.amount + 0.5f); if(nh < 0) nh = 0;
        Cmd(OP_SET_PLAYER_HEALTH, "ii", 0, nh);
    }
    void OnAdminAct(const PktAdminAct& a) override
    {
        char t[MP_CHAT_LEN + 1]; memcpy(t, a.text, MP_CHAT_LEN); t[MP_CHAT_LEN] = 0;
        switch(a.act)
        {
        case AACT_HEALTH:   Cmd(OP_SET_PLAYER_HEALTH, "ii", 0, a.amount > 0 ? a.amount : 100); Chat(0xFF80FF80, "Health set to %d", a.amount); break;
        case AACT_ARMOUR:   Cmd(OP_ADD_ARMOUR_TO_PLAYER, "ii", 0, a.amount > 0 ? a.amount : 100); Chat(0xFF80FF80, "Armour +%d", a.amount); break;
        case AACT_MONEY:    Cmd(OP_ADD_SCORE, "ii", 0, a.amount); Chat(0xFF80FF80, "Money %+d", a.amount); break;
        case AACT_TELEPORT: Cmd(OP_SET_PLAYER_COORDINATES, "ifff", 0, a.pos[0], a.pos[1], a.pos[2]); Cmd(OP_SET_PLAYER_HEADING, "if", 0, a.heading); break;
        case AACT_PAY:      if(a.amount > 0) Cmd(OP_ADD_SCORE, "ii", 0, a.amount); Chat(0xFF80FF80, "%s %d (%s)", a.amount > 0 ? "Got $" : "Sent $", a.amount > 0 ? a.amount : -a.amount, t); break;
        default: if(t[0]) Chat(0xFF60C0FF, "%s", t); break;
        }
    }
    void OnKicked(const PktKicked& k) override
    {
        char why[MP_REASON_LEN + 1]; memcpy(why, k.reason, MP_REASON_LEN); why[MP_REASON_LEN] = 0;
        Chat(0xFF6060FF, "%s: %s", k.banned ? "Banned" : "Kicked", why[0] ? why : "-");
    }
    void OnDialog(const PktDialog& d) override
    {   // SA-MP style dialogs from the server's gamemodes: shown in the chat, answered with the phone keyboard
        char cap[65]; memcpy(cap, d.caption, 64); cap[64] = 0;
        std::string info(d.info, strnlen(d.info, MP_DIALOG_INFO_LEN));
        Chat(0xFFFFE080, "[%s]", cap);
        int items = 0; size_t st = 0;
        while(st <= info.size())
        {
            size_t e = info.find('\n', st); if(e == std::string::npos) e = info.size();
            std::string line = info.substr(st, e - st);
            for(auto& c : line) if(c == '\t') c = ' ';
            std::string clean; for(size_t i = 0; i < line.size(); ++i)   // drop {RRGGBB} colour codes
            { if(line[i] == '{' && i + 7 < line.size() + 1 && line.find('}', i) == i + 7) { i += 7; continue; } clean += line[i]; }
            if(!clean.empty())
            {
                if(d.style == DIALOG_STYLE_LIST || d.style == DIALOG_STYLE_TABLIST || d.style == DIALOG_STYLE_TABLIST_HEADERS)
                {
                    if(d.style == DIALOG_STYLE_TABLIST_HEADERS && items == 0 && st == 0) Chat(0xFFC0C0C0, "  %s", clean.c_str());
                    else Chat(0xFFFFFFFF, "%d. %s", ++items, clean.c_str());
                }
                else Chat(0xFFFFFFFF, "%s", clean.c_str());
            }
            st = e + 1;
        }
        s_Dialog.open = true; s_Dialog.id = d.dialogId; s_Dialog.style = d.style; s_Dialog.items = items;
        const bool list = items > 0;
        char title[128]; snprintf(title, sizeof(title), "%s%s", cap, list ? " - type the number" : (d.style == DIALOG_STYLE_MSGBOX ? " - OK" : ""));
        FilePick::AskText(title, "", list ? 3 : 63, list);
        s_bTyping = true;
    }
    void OnServerMessage(const PktServerMsg& p) override
    {
        char t[MP_CHAT_LEN + 1]; memcpy(t, p.text, MP_CHAT_LEN); t[MP_CHAT_LEN] = 0;
        Chat(p.color ? p.color : 0xFFFFFF80, "%s", t);
    }
    void OnSetColor(const PktSetColor& c) override { if(s_Players[c.id].used) s_Players[c.id].color = RgbaToAbgr(c.color); }
    void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld& w) override
    {   // the server's clock + weather (1 game minute = 1 real second on every MP server)
        const int nowMin = w.hour * 60 + w.minute;
        if(s_nLastHour < 0 || abs(nowMin - s_nLastHour) > 3) { Cmd(OP_SET_TIME_OF_DAY, "ii", (int)w.hour, (int)w.minute); }
        s_nLastHour = nowMin;
        const int vw = MapWeather(w.weather);
        if(vw != s_nLastWeather) { Cmd(OP_FORCE_WEATHER_NOW, "i", vw); s_nLastWeather = vw; }
    }
    void OnStatus(const PktStatus& s) override
    {
        if(s.id == s_nMyId) return;
        Remote& r = s_Players[s.id];
        if(!r.used) return;
        char m[9]; memcpy(m, s.mission, 8); m[8] = 0;
        if((s.state & STATUS_MISSION) && !(r.status & STATUS_MISSION))
        {
            const char* t = m[0] ? VC_MissionTitle(m) : nullptr;
            Chat(0xFFFFC080, "%s started %s", r.name, t ? t : (m[0] ? m : "a mission"));
        }
        if((s.state & STATUS_CUTSCENE) && !(r.status & STATUS_CUTSCENE)) Chat(0xFFFFC080, "%s watches a cutscene", r.name);
        r.status = s.state; memcpy(r.mission, m, sizeof(r.mission));
    }
} s_Events;

// ---- per frame ----------------------------------------------------------------------------------------------------
static void UpdateRemotes(float dt)
{
    if(!AddPed || !GetPedRef || !GetPed) return;
    if(!s_bModelReq && RequestModel && LoadAllRequestedModels)
    {
        RequestModel(cfg.remoteSkin, 1); LoadAllRequestedModels(false); s_bModelReq = true;
    }
    const float k = dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f;
    for(auto& r : s_Players)
    {
        if(!r.used || !r.hasSync) continue;
        for(int i = 0; i < 3; ++i) r.cur[i] += (r.target[i] - r.cur[i]) * k;
        void* ped = RemotePed(r);
        if(!ped)
        {   // new, or the game removed it (population / streaming): make it again
            CVector at = { r.cur[0], r.cur[1], r.cur[2] };
            ped = AddPed(PEDTYPE_CIVMALE, (unsigned)cfg.remoteSkin, at, 0);
            r.pedRef = ped ? GetPedRef(ped) : -1;
            if(!ped) continue;
            Cmd(OP_SET_CHAR_PROOFS, "iiiiii", r.pedRef, 1, 1, 1, 1, 1);   // only the server hurts other players
            Cmd(OP_SET_CHAR_STAY_IN_SAME_PLACE, "ii", r.pedRef, 1);
            r.carRef = -1;
        }
        CVector at = { r.cur[0], r.cur[1], r.cur[2] };
        const float rad = r.heading * 3.14159265f / 180.0f;
        // their car: same model, locked, proof; moved with them
        void* car = (r.carRef >= 0 && GetVehicle) ? GetVehicle(r.carRef) : nullptr;
        if(r.inCar && IsVcCarModel(r.carModel) && s_pCmd && GetVehicle)
        {
            uint32_t cm = 0;
            if(car) Cmd(OP_GET_CAR_MODEL, "iv", r.carRef, &cm);
            if(car && ((int)cm != r.carModel || Cmd(OP_IS_CAR_DEAD, "i", r.carRef)))
            { Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", r.pedRef, at.x, at.y, at.z); Cmd(OP_DELETE_CAR, "i", r.carRef); car = nullptr; r.carRef = -1; }
            if(!car)
            {
                if(!Cmd(OP_HAS_MODEL_LOADED, "i", r.carModel)) { Cmd(OP_REQUEST_MODEL, "i", r.carModel); Cmd(OP_LOAD_ALL_MODELS_NOW, ""); }
                uint32_t ref = 0;
                if(Cmd(OP_HAS_MODEL_LOADED, "i", r.carModel))
                {
                    Cmd(OP_CREATE_CAR, "ifffv", r.carModel, at.x, at.y, at.z + 0.5f, &ref);
                    car = GetVehicle((int)ref);
                    if(car)
                    {
                        r.carRef = (int)ref;
                        Cmd(OP_SET_CAR_PROOFS, "iiiiii", r.carRef, 1, 1, 1, 1, 1);
                        Cmd(OP_LOCK_CAR_DOORS, "ii", r.carRef, 2);
                        Cmd(OP_CAR_SET_IDLE, "i", r.carRef);
                        Cmd(OP_WARP_CHAR_INTO_CAR, "ii", r.pedRef, r.carRef);
                    }
                }
            }
            if(car)
            {
                if(s_nTeleportSlot >= 0) ((void(*)(void*, CVector))((*(void***)car)[s_nTeleportSlot]))(car, at);
                else { float* q = MatPos(car); q[0] = at.x; q[1] = at.y; q[2] = at.z; }
                if(SetHeading) SetHeading(car, rad);
                continue;
            }
        }
        else if(car)
        {   // got out
            Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", r.pedRef, at.x, at.y, at.z);
            Cmd(OP_DELETE_CAR, "i", r.carRef); r.carRef = -1;
            ped = RemotePed(r); if(!ped) continue;
        }
        if(PedTeleport) PedTeleport(ped, at);
        else { float* p = MatPos(ped); p[0] = at.x; p[1] = at.y; p[2] = at.z; }
        if(SetHeading) SetHeading(ped, rad);
    }
}

static void UpdateBlips()
{
    if(!cfg.blips || !SetCoordBlip || !ClearBlip) return;
    for(auto& r : s_Players)
    {
        if(!r.used || !r.hasSync) continue;
        if(r.blip >= 0) ClearBlip(r.blip);
        CVector at = { r.cur[0], r.cur[1], r.cur[2] };
        r.blip = SetCoordBlip(4 /*BLIP_COORD*/, at, 1, 3 /*BLIP_DISPLAY_BOTH*/);
    }
}

static void SendMySync(uint64_t now)
{
    void* ped = FindPlayerPed ? FindPlayerPed() : nullptr;
    if(!ped) return;
    PktSync p; memset(&p, 0, sizeof(p));
    p.type = PKT_SYNC; p.id = (uint8_t)(s_nMyId < 0 ? 0 : s_nMyId); p.seq = ++s_nSeq;
    const float* pos = MatPos(ped); const float* fw = MatFwd(ped);
    memcpy(p.d.pos, pos, sizeof(p.d.pos));
    p.d.heading = atan2f(-fw[0], fw[1]) * 180.0f / 3.14159265f;
    if(p.d.heading < 0) p.d.heading += 360.0f;
    uint32_t hp = 100, wp = 0;
    if(s_pCmd) { Cmd(OP_GET_PLAYER_HEALTH, "iv", 0, &hp); Cmd(OP_GET_CURRENT_PLAYER_WEAPON, "iv", 0, &wp); }
    p.d.health = (uint8_t)(hp > 255 ? 255 : hp); p.d.armour = 0; p.d.weapon = (uint8_t)(wp < 256 ? wp : 0); p.d.skin = 0;
    // death -> PKT_DEATH once (the server tells everyone), alive again -> normal sync
    if(hp == 0 && !s_bDead)
    {
        s_bDead = true;
        PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)s_nMyId; d.killer = MP_INVALID_ID; d.weapon = 255;
        s_Client.Send(&d, sizeof(d));
    }
    else if(hp > 0) s_bDead = false;
    if(s_bDead) p.d.flags |= SYNC_DEAD;
    void* car = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
    if(car)
    {
        const float* cp = MatPos(car); const float* cf = MatFwd(car);
        memcpy(p.d.carPos, cp, sizeof(p.d.carPos));
        p.d.carHeading = atan2f(-cf[0], cf[1]) * 180.0f / 3.14159265f;
        if(p.d.carHeading < 0) p.d.carHeading += 360.0f;
        uint32_t cm = 0;
        if(GetVehicleRef && s_pCmd) Cmd(OP_GET_CAR_MODEL, "iv", GetVehicleRef(car), &cm);
        p.d.flags |= SYNC_IN_CAR | SYNC_DRIVER; p.d.carModel = (uint16_t)(IsVcCarModel((int)cm) ? cm : 0);
    }
    s_Client.SendSync(&p, sizeof(p));

    // safe zones (the server drops every hit in or into them)
    const float* at = car ? MatPos(car) : pos;
    int z = MP_SafeZoneAt(at[0], at[1], at[2], MP_GAME_VC);
    if(z != s_nSafeZone)
    {
        if(z >= 0) { char t[128]; snprintf(t, sizeof(t), "Safe zone: %s - nobody can hurt you here", g_MPSafeZonesVC[z].name); Help(t); }
        else if(s_nSafeZone >= 0) Help("You left the safe zone");
        s_nSafeZone = z;
    }

    // mission / cutscene status (main.scm OnAMissionFlag, CCutsceneMgr::ms_running)
    uint8_t st = 0;
    if(pOnMissionFlag && pScriptSpace && *pOnMissionFlag > 0 && *pOnMissionFlag < 0x100000 && *(int*)(pScriptSpace + *pOnMissionFlag)) st |= STATUS_MISSION;
    if(pCutsceneRunning && *pCutsceneRunning) st |= STATUS_CUTSCENE;
    if(st != s_nMyStatus || now >= s_nNextStatus)
    {
        PktStatus ps; memset(&ps, 0, sizeof(ps)); ps.type = PKT_STATUS; ps.id = p.id; ps.state = st;
        s_Client.Send(&ps, sizeof(ps));
        s_nMyStatus = st; s_nNextStatus = now + 5000;
    }
}

// ---- chat typing + dialog answers (phone keyboard) ----------------------------------------------------------------
static void SendChatText(const char* text)
{
    if(!text[0]) return;
    PktChat c; memset(&c, 0, sizeof(c));
    c.type = PKT_CHAT; c.id = (uint8_t)s_nMyId;
    MP_CopyStr(c.text, text, MP_CHAT_LEN);
    s_Client.Send(&c, sizeof(c));
    if(text[0] != '/') Chat(0xFFFFFFFF, "%s: %s", cfg.name, text);   // the server sends chat to the others only
}
static void PollTyping()
{
    if(!s_bTyping) return;
    const int st = FilePick::TextState();
    if(st == 1 || st == 0) return;
    std::string t = st == 3 ? FilePick::Text() : std::string();
    FilePick::TextReset();
    s_bTyping = false;
    if(s_Dialog.open)
    {
        PktDialogResp r; memset(&r, 0, sizeof(r));
        r.type = PKT_DIALOG_RESPONSE; r.dialogId = s_Dialog.id; r.listItem = -1;
        if(st == 3)
        {
            r.response = 1;
            if(s_Dialog.items > 0) { int n = atoi(t.c_str()); if(n < 1 || n > s_Dialog.items) r.response = 0; else r.listItem = (int16_t)(n - 1); }
            else MP_CopyStr(r.input, t.c_str(), sizeof(r.input));
        }
        s_Client.Send(&r, sizeof(r));
        s_Dialog = PendingDialog();
        return;
    }
    if(st == 3) SendChatText(t.c_str());
}
static bool s_bWasDown = false;
static void PollChatButton()
{
    if(!PointerGetButton || !PointerGetCoords || s_bTyping || s_nChatBtn[2] <= 0) return;
    int st = PointerGetButton(0, 0);
    bool down = st == 2 || st == 3;
    if(down && !s_bWasDown)
    {
        int x = 0, y = 0; float pr = 0; PointerGetCoords(0, &x, &y, &pr);
        if(x >= s_nChatBtn[0] && x <= s_nChatBtn[0] + s_nChatBtn[2] && y >= s_nChatBtn[1] && y <= s_nChatBtn[1] + s_nChatBtn[3])
        {
            if(FilePick::AskText("Chat - or a /command", "", MP_CHAT_LEN - 1, false)) s_bTyping = true;
        }
    }
    s_bWasDown = down;
}

static void Process()
{
    const uint64_t now = MP_NowMs();
    const float dt = s_nLastFrame ? (float)(now - s_nLastFrame) / 1000.0f : 0.0f;
    s_nLastFrame = now;
    if(!FindPlayerPed || !FindPlayerPed()) return;   // menu / loading

    if(cfg.host[0] && s_Client.GetState() == NET_DISCONNECTED && now >= s_nNextJoin)
    {
        if(!s_bJoinTried) Chat(0xFFFFFFFF, "MP Vice City: joining %s:%d as %s", cfg.host, cfg.port, cfg.name);
        s_bJoinTried = true;
        s_Client.SetGame(MP_GAME_VC);
        s_Client.SetJoinPassword(cfg.password);
        s_Client.Connect(cfg.host, (uint16_t)cfg.port, cfg.name, 0);
        s_nNextJoin = now + 20000;
    }
    s_Client.Poll(&s_Events);
    if(s_Client.GetState() != NET_CONNECTED) return;
    PollTyping();
    PollChatButton();
    if(now >= s_nNextSync) { s_nNextSync = now + 100; SendMySync(now); }
    UpdateRemotes(dt > 0.25f ? 0.25f : dt);
    if(now >= s_nNextBlips) { s_nNextBlips = now + 500; UpdateBlips(); }
}

static void Text(float x, float y, const char* s, uint32_t abgr)
{
    if(!FontPrint) return;
    static uint16_t w[256]; int i = 0;
    for(; s[i] && i < 255; ++i) w[i] = (uint8_t)s[i];
    w[i] = 0;
    CRGBA c = { (uint8_t)(abgr), (uint8_t)(abgr >> 8), (uint8_t)(abgr >> 16), (uint8_t)(abgr >> 24) };
    if(FontSetColor) FontSetColor(c);
    FontPrint(x, y, w);
}

static void Draw()
{
    if(!FontPrint || !ScreenH) return;
    const float sh = (float)ScreenH();
    const float sc = cfg.textScale * sh / 720.0f;
    if(FontSetStyle) FontSetStyle(1);
    if(FontSetScale) FontSetScale(0.8f * sc, 1.0f * sc);
    if(FontSetJustifyOn) FontSetJustifyOn();
    if(FontSetDropShadow) FontSetDropShadow(1);
    CRGBA black = { 0, 0, 0, 255 };
    if(FontSetDropColor) FontSetDropColor(black);

    const uint64_t now = MP_NowMs();
    if(s_Client.GetState() == NET_CONNECTED)
    {   // the Chat button (tap -> the phone keyboard)
        s_nChatBtn[0] = (int)(sh * 0.03f); s_nChatBtn[1] = (int)(sh * 0.22f);
        s_nChatBtn[2] = (int)(110 * sc); s_nChatBtn[3] = (int)(40 * sc);
        Text((float)s_nChatBtn[0], (float)s_nChatBtn[1] + 8 * sc, s_bTyping ? "[ typing... ]" : "[ Chat ]", 0xFF80E0FF);
    }
    else s_nChatBtn[2] = 0;
    float y = sh * 0.30f;
    for(auto& l : s_Chat)
    {
        if(now - l.at > 15000) continue;
        Text(sh * 0.03f, y, l.text.c_str(), l.color);
        y += 26.0f * sc;
    }
    if(s_Client.GetState() == NET_CONNECTED && CalcScreenCoors)
    {
        for(auto& r : s_Players)
        {
            if(!r.used || !r.hasSync) continue;
            RwV3d in = { r.cur[0], r.cur[1], r.cur[2] + 1.15f }, out; float w, h;
            if(!CalcScreenCoors(in, &out, &w, &h, true) || out.z <= 0.5f || out.z > 80.0f) continue;
            Text(out.x - strlen(r.name) * 6.0f * sc, out.y, r.name, r.color ? r.color : 0xFFFFFFFF);
        }
    }
}

// ---- hooks --------------------------------------------------------------------------------------------------------
DECL_HOOKv(VC_CGame_Process)
{
    VC_CGame_Process();
    Process();
}
DECL_HOOKv(VC_CHud_Draw)
{
    VC_CHud_Draw();
    Draw();
}

bool Start()
{
    s_hGame = aml->GetLibHandle("libGTAVC.so");
    if(!s_hGame) { logger->Error("Vice City: libGTAVC.so handle not found - MP stays off"); return false; }
    int missing = 0;
    if(!Sym(FindPlayerPed, "_Z13FindPlayerPedv", missing)) { logger->Error("Vice City: FindPlayerPed not found - this libGTAVC.so is not supported, MP stays off"); return false; }
    Sym(FindPlayerVehicle, "_Z17FindPlayerVehiclev", missing);
    Sym(AddPed, "_ZN11CPopulation6AddPedE8ePedTypejRK7CVectori", missing);
    Sym(RemovePed, "_ZN11CPopulation9RemovePedEP4CPed", missing);
    Sym(GetPedRef, "_ZN6CPools9GetPedRefEP4CPed", missing);
    Sym(GetPed, "_ZN6CPools6GetPedEi", missing);
    Sym(PedTeleport, "_ZN4CPed8TeleportE7CVector", missing);
    Sym(SetHeading, "_ZN10CPlaceable10SetHeadingEf", missing);
    Sym(RequestModel, "_ZN10CStreaming12RequestModelEii", missing);
    Sym(LoadAllRequestedModels, "_ZN10CStreaming22LoadAllRequestedModelsEb", missing);
    Sym(SetHelpMessage, "_ZN4CHud14SetHelpMessageEPtbbb", missing);
    Sym(FontSetScale, "_ZN5CFont8SetScaleEff", missing);
    Sym(FontSetColor, "_ZN5CFont8SetColorE5CRGBA", missing);
    Sym(FontSetDropColor, "_ZN5CFont12SetDropColorE5CRGBA", missing);
    Sym(FontSetDropShadow, "_ZN5CFont21SetDropShadowPositionEs", missing);
    Sym(FontSetStyle, "_ZN5CFont12SetFontStyleEs", missing);
    Sym(FontSetJustifyOn, "_ZN5CFont12SetJustifyOnEv", missing);
    Sym(FontPrint, "_ZN5CFont11PrintStringEffPt", missing);
    Sym(CalcScreenCoors, "_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_b", missing);
    Sym(ScreenW, "_Z17OS_ScreenGetWidthv", missing);
    Sym(ScreenH, "_Z18OS_ScreenGetHeightv", missing);
    Sym(SetCoordBlip, "_ZN6CRadar12SetCoordBlipE9eBlipType7CVectorj12eBlipDisplay", missing);
    Sym(ClearBlip, "_ZN6CRadar9ClearBlipEi", missing);
    Sym(pOnMissionFlag, "_ZN11CTheScripts14OnAMissionFlagE", missing);
    Sym(pScriptSpace, "_ZN11CTheScripts11ScriptSpaceE", missing);
    Sym(pCutsceneRunning, "_ZN12CCutsceneMgr10ms_runningE", missing);
    Sym(ProcessOneCommand, "_ZN14CRunningScript17ProcessOneCommandEv", missing);
    Sym(GetVehicleRef, "_ZN6CPools13GetVehicleRefEP8CVehicle", missing);
    Sym(GetVehicle, "_ZN6CPools10GetVehicleEi", missing);
    Sym(PointerGetButton, "_Z20LIB_PointerGetButtonii", missing);
    Sym(PointerGetCoords, "_Z25LIB_PointerGetCoordinatesiPiS_Pf", missing);
    {   // CEntity::Teleport is virtual (CAutomobile / CBike / CBoat each have their own): find its vtable slot
        void** vt = (void**)aml->GetSym(s_hGame, "_ZTV11CAutomobile");
        uintptr_t fn = (uintptr_t)aml->GetSym(s_hGame, "_ZN11CAutomobile8TeleportE7CVector");
        if(vt && fn) for(int i = 2; i < 80; ++i) if(((uintptr_t)vt[i] & ~(uintptr_t)1) == (fn & ~(uintptr_t)1)) { s_nTeleportSlot = i - 2; break; }
        logger->Info("Vice City: vehicle Teleport vtable slot %d", s_nTeleportSlot);
    }
    if(!aml->GetSym(s_hGame, "_ZN5CGame7ProcessEv")) { logger->Error("Vice City: CGame::Process not found - MP stays off"); return false; }

    LoadSettings();
    logger->Info("Vice City: script commands %s", InitScripts() ? "on" : "OFF (cars / health / weather off)");
    HOOKSYM(VC_CGame_Process, s_hGame, "_ZN5CGame7ProcessEv");
    if(aml->GetSym(s_hGame, "_ZN4CHud4DrawEv")) HOOKSYM(VC_CHud_Draw, s_hGame, "_ZN4CHud4DrawEv");
    logger->Info("Vice City: MP Vice City layer on (%d game functions missing), %d missions / %d cutscenes known, %d hospital safe zones",
                 missing, g_VCMissionCount, g_VCCutsceneCount, g_MPSafeZoneCountVC);
    return true;
}
} // namespace VC
