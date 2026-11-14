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
//   - 4.13.37: every San Andreas sync in Vice City too: the other players are script chars in their own skin that walk /
//     run / sprint / crouch with the game's animations, hold their weapon, die and come back; bullet sync (tracers +
//     muzzle light from CWeapon::DoBulletImpact), projectiles (grenades / molotovs / rockets from CProjectileInfo),
//     explosions + fires (CExplosion / CFireManager, never shared in a safe zone), shooter-authoritative damage with
//     armour and kill credit, the full car rotation, car colours + health (PKT_VEH_STATE), passengers in a player's car,
//     carjacking, interiors (CGame::currArea), dropped weapon pickups, entity radar blips in the player's colour,
//     health / armour bars, AFK tags, the scoreboard ([ Players ]), the [ MP ] menu, SA-MP textdraws
// Not ported (docs/VICE_CITY_PLAN.md): voice chat, NPC traffic sync, SA clothes / gamemodes / co-op missions.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <cmath>
#include <sys/stat.h>
#include <sys/mman.h>
#include <string>
#include <vector>
#include "../net/client.h"
#include "../net/server.h"     // MP_NowMs
#include "../net/safezone.h"
#include "vc_data.h"
#include "../ui/filepick.h"   // the phone keyboard dialog (game independent)
#include "../../imgui/stb_image.h"   // 4.13.38: title2.png / title_logo_connected2.png (implementation in hud.cpp)

namespace VC
{
// ---- game types (reVC layouts) ------------------------------------------------------------------------------------
struct CVector { float x, y, z; };
struct RwV3d   { float x, y, z; };
struct CRGBA   { uint8_t r, g, b, a; };
struct CVector2D { float x, y; };
struct CRect   { float left, bottom, right, top; };   // reVC / VC order (Sprite2d DrawRect)
// CPlaceable: vtable, then CMatrix (RwMatrix: right, flags, up(= forward), pad, at, pad, pos, pad)
static inline float* MatRight(void* e) { return (float*)((uint8_t*)e + sizeof(void*) + 0x00); }
static inline float* MatFwd(void* e)   { return (float*)((uint8_t*)e + sizeof(void*) + 0x10); }
static inline float* MatUp(void* e)    { return (float*)((uint8_t*)e + sizeof(void*) + 0x20); }
static inline float* MatPos(void* e)   { return (float*)((uint8_t*)e + sizeof(void*) + 0x30); }
// 4.13.37: read from the real libGTAVC.so (both ABIs): CEntity::m_modelIndex (int16), CWeaponInfo (0x64 bytes, model +0x60)
#ifdef AML32
  enum { ENT_MODEL = 0x60 };
#else
  enum { ENT_MODEL = 0x70 };
#endif
enum { WINFO_MODEL = 0x60 };

enum { PEDTYPE_CIVMALE = 4, MI_MALE01 = 7 };

// ---- libGTAVC.so functions (by name) ------------------------------------------------------------------------------
static void* (*FindPlayerPed)() = nullptr;
static void* (*FindPlayerVehicle)() = nullptr;
// 4.13.38: RenderWare + CSprite2d for the Vice City title images
struct RwImage { int32_t flags, width, height, depth, stride; uint8_t* cpPixels; void* palette; };
static RwImage* (*RwImageCreate)(int, int, int) = nullptr;
static RwImage* (*RwImageAllocatePixels)(RwImage*) = nullptr;
static int (*RwImageDestroy)(RwImage*) = nullptr;
static RwImage* (*RwImageFindRasterFormat)(RwImage*, int, int*, int*, int*, int*) = nullptr;
static void* (*RwRasterCreate)(int, int, int, int) = nullptr;
static void* (*RwRasterSetFromImage)(void*, RwImage*) = nullptr;
static void* (*RwTextureCreate)(void*) = nullptr;
static int (*RwRenderStateSet)(int, void*) = nullptr;
struct CSprite2d { void* tex; };
static char s_szDataDir[512] = "";               // files/TYFMC/MP (gtavc)
static uint64_t s_nLogoAt = 0, s_nFirstFrame = 0;  // title_logo_connected2.png shown since / first game frame
static void (*SpriteDraw)(CSprite2d*, const struct CRect&, const struct CRGBA&) = nullptr;
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
static void* (*GetWeaponInfo)(int type) = nullptr;                           // CWeaponInfo::GetWeaponInfo
static void  (*AddTrace)(CVector* a, CVector* b, int weapon, void* shooter) = nullptr;   // CBulletTraces::AddTrace
static int   (*SetEntityBlip)(int type, int handle, unsigned int color, int display) = nullptr;
static void  (*ChangeBlipScale)(int idx, int scale) = nullptr;
static void  (*DrawRect)(const CRect& r, const CRGBA& c) = nullptr;
static void  (*FontSetCentreOn)() = nullptr;
static void  (*FontSetCentreOff)() = nullptr;
static void  (*FontSetRightOn)() = nullptr;
static void  (*FontSetRightOff)() = nullptr;
static void  (*FontSetCentreSize)(float) = nullptr;
static void  (*FontSetBgOff)() = nullptr;
static void  (*AddLight)(uint8_t type, CVector pos, CVector dir, float radius, float r, float g, float b, uint8_t fog, bool extra) = nullptr;
static int*  pCurrArea = nullptr;            // CGame::currArea (interior)
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
    // 4.13.37 (checked against the Sanny Builder Vice City library: numbers, inputs / outputs, condition)
    OP_CREATE_CHAR = 0x009A, OP_DELETE_CHAR = 0x009B, OP_SET_CHAR_COORDINATES = 0x00A1, OP_SET_CHAR_HEADING = 0x0173,
    OP_SET_CAR_HEADING = 0x0175, OP_GIVE_WEAPON_TO_CHAR = 0x01B2, OP_SET_CURRENT_CHAR_WEAPON = 0x01B9,
    OP_SET_CHAR_OBJ_NO_OBJ = 0x011C, OP_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT = 0x0211, OP_SET_CHAR_OBJ_RUN_TO_COORD = 0x0239,
    OP_SET_CHAR_OBJ_SPRINT_TO_COORD = 0x0502, OP_CREATE_CHAR_AS_PASSENGER = 0x01C8, OP_IS_CAR_PASSENGER_SEAT_FREE = 0x0431,
    OP_GET_PLAYER_CHAR = 0x01F5, OP_GET_DRIVER_OF_CAR = 0x046C, OP_HAS_CHAR_BEEN_DAMAGED_BY_CHAR = 0x051A,
    OP_CLEAR_CHAR_LAST_DAMAGE_ENTITY = 0x054E, OP_SET_CHAR_HEALTH = 0x0223, OP_GET_CHAR_HEALTH = 0x0226,
    OP_GET_CHAR_ARMOUR = 0x04DD, OP_IS_CHAR_DEAD = 0x0118, OP_SET_CHAR_CROUCH = 0x04EB, OP_IS_CHAR_DUCKING = 0x0597,
    OP_IS_CHAR_SHOOTING = 0x02E0, OP_SET_CHAR_CANT_BE_DRAGGED_OUT = 0x039E, OP_CLEAR_CHAR_THREAT_SEARCH = 0x01ED,
    OP_ADD_EXPLOSION = 0x020C, OP_START_SCRIPT_FIRE = 0x02CF, OP_REMOVE_SCRIPT_FIRE = 0x02D1,
    OP_CREATE_PICKUP_WITH_AMMO = 0x032B, OP_HAS_PICKUP_BEEN_COLLECTED = 0x0214, OP_REMOVE_PICKUP = 0x0215,
    OP_GET_CAR_COLOURS = 0x03F3, OP_SET_CAR_HEALTH = 0x0224, OP_GET_AMMO_IN_PLAYER_WEAPON = 0x0419,
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
    int  nameTags = 1;     // 4.13.37
} cfg;
static char s_szIni[600] = "";

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
    MP_CopyStr(s_szDataDir, dir, sizeof(s_szDataDir));
    MP_CopyStr(s_szIni, path, sizeof(s_szIni));
    FILE* f = fopen(path, "r");
    if(!f)
    {
        f = fopen(path, "w");
        if(f)
        {
            fputs("; MP for GTA Vice City (4.13.37). Only Vice City servers (mp_server --game vc) take a Vice City phone.\n"
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
                  "Blips=1\n"
                  "; name tags + health bars over the other players\n"
                  "NameTags=1\n", f);
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
        else if(!strcasecmp(k, "NameTags")) cfg.nameTags = atoi(v);
    }
    fclose(f);
    logger->Info("Vice City: vc.ini host '%s' port %d name '%s'", cfg.host, cfg.port, cfg.name);
}
static void SaveSettings()   // 4.13.37: the MP menu changes host / name / markers / tags
{
    if(!s_szIni[0]) return;
    FILE* f = fopen(s_szIni, "w");
    if(!f) return;
    fprintf(f, "; MP for GTA Vice City (4.13.37). Only Vice City servers (mp_server --game vc) take a Vice City phone.\n[VC]\n"
               "Host=%s\nPort=%d\nName=%s\nPassword=%s\nRemoteSkin=%d\nTextScale=%.2f\nBlips=%d\nNameTags=%d\n",
            cfg.host, cfg.port, cfg.name, cfg.password, cfg.remoteSkin, cfg.textScale, cfg.blips, cfg.nameTags);
    fclose(f);
}

// ---- state --------------------------------------------------------------------------------------------------------
struct Remote
{
    bool    used = false;
    char    name[MP_NAME_LEN] = "";
    uint32_t color = 0xFFFFFFFF;  // ABGR (font); rgba keeps the server's RGBA for the radar
    uint32_t rgba = 0xFFFFFFFF;
    bool    hasSync = false;
    float   target[3] = {0, 0, 0}, cur[3] = {0, 0, 0}, vel[3] = {0, 0, 0};
    uint64_t at = 0;              // when the last sync came
    float   heading = 0;          // degrees
    bool    inCar = false;
    int     carModel = 0;         // the car they drive (Vice City model 130..236)
    int     carRef = -1;          // our copy of it
    int     pedRef = -1;          // script char handle of the avatar (the game may remove it: the handle says so)
    int     pedModel = -1;
    int     blip = -1, blipRef = -1;
    uint8_t status = 0;
    char    mission[9] = "";
    bool    named = false;
    // 4.13.37: full sync
    uint8_t health = 100, armour = 0, weapon = 0, flags = 0;
    uint16_t skin = 0;
    float   speed = 0;
    int     appliedWeapon = -1;
    bool    ducking = false, deadApplied = false;
    int     interior = 0;
    bool    hasVeh = false; int vehOwner = -1; int vehSeat = 0; int passCar = -1;
    bool    hasRot = false; float carFwd[3] = {0, 0, 0}, carUp[3] = {0, 0, 0};
    uint8_t paused = 0; int ping = 0; int score = 0;
    int     vsCol[2] = {-1, -1}, vsAppliedCol[2] = {-1, -1}; int vsHealth = -1; uint16_t vsModel = 0;
    float   lastGoto[2] = {0, 0}; uint64_t nextGoto = 0; int gotoMode = -1;
    uint64_t lastShot = 0, jackedUntil = 0; float shotDir[2] = {0, 0};
};
static Remote s_Players[256];
static void* s_AvPed[256];   // this frame's avatar entities (hooks: "was this explosion / fire made by an avatar")
static void* s_AvCar[256];
static MPClient s_Client;
static int s_nMyId = -1;
static bool s_bJoinTried = false, s_bManualOff = false;
static uint64_t s_nNextJoin = 0, s_nNextSync = 0, s_nNextBlips = 0, s_nLastFrame = 0;
static uint32_t s_nSeq = 0;
static int s_nSafeZone = -1;
static uint8_t s_nMyStatus = 0;
static uint64_t s_nNextStatus = 0;
static bool s_bDead = false;
static int  s_nLastHour = -1, s_nLastWeather = -1;
static bool s_bTyping = false;
enum { ASK_CHAT = 0, ASK_MENU, ASK_HOST, ASK_NAME };
static int  s_nAsk = ASK_CHAT;
struct PendingDialog { bool open = false; uint16_t id = 0; uint8_t style = 0; int items = 0; };
static PendingDialog s_Dialog;
enum { BTN_CHAT = 0, BTN_PLAYERS, BTN_MP, BTN_COUNT };
static int s_Btn[BTN_COUNT][4];   // x, y, w, h (pixels); w 0 = hidden
static bool s_bScoreboard = false;
// my state
static int s_nMyChar = -1, s_nMyWeapon = 0, s_nKills = 0;
static float s_LastPos[3] = {0, 0, 0}; static uint64_t s_nLastPosAt = 0;
static int s_nLastHitBy = -1, s_nLastHitWeapon = 255; static uint64_t s_nLastHitAt = 0;
static uint64_t s_nLastProjectile = 0, s_nNextInfo = 0, s_nNextVehState = 0, s_nLastExpSent = 0, s_nLastFireSent = 0;
static PktVehState s_LastVs; static bool s_bHaveVs = false;
// shots made by me this frame (hook -> PKT_SHOT)
static PktShot s_Shot; static float s_LastAim[3] = {0, 0, 0};
static bool s_bApplyingNet = false;   // our own ADD_EXPLOSION / START_SCRIPT_FIRE / replayed projectiles: never sent back
struct Foreign { float pos[3]; uint64_t at; };
static Foreign s_Foreign[16]; static int s_nForeign = 0;
struct NetFire { int handle; uint64_t until; };
static std::vector<NetFire> s_Fires;
struct NetPickup { uint16_t uid; int handle; };
static std::vector<NetPickup> s_Pickups;
static std::vector<PktTextDraw> s_TextDraws;

static uint32_t RgbaToAbgr(uint32_t c) { return 0xFF000000u | ((c >> 24) & 0xFF) | (((c >> 16) & 0xFF) << 8) | (((c >> 8) & 0xFF) << 16); }
static int MapWeather(int sa)
{   // San Andreas weather ids -> Vice City (0 sunny, 1 cloudy, 2 rainy, 3 foggy, 4 extra sunny, 5 hurricane)
    switch(sa) { case 8: case 16: return 2; case 9: case 19: return 3; case 4: case 7: case 12: case 15: return 1;
                 case 11: case 17: case 18: return 4; default: return 0; }
}
// Vice City weapons: 12 grenade, 13 detonator grenade, 14 teargas, 15 molotov, 16 rocket, 30 rocket launcher, 31 flamethrower
static bool IsBlastWeapon(int w) { return (w >= 12 && w <= 16) || w == 30 || w == 31; }
static bool ValidVcWeapon(int w) { return w >= 0 && w <= 33; }

struct ChatLine { std::string text; uint32_t color; uint64_t at; };
static std::vector<ChatLine> s_Chat;
static void Chat(uint32_t color, const char* fmt, ...)
{
    char buf[256]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    s_Chat.push_back({ buf, color, MP_NowMs() });
    if(s_Chat.size() > 10) s_Chat.erase(s_Chat.begin());
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
static void* RemoteCar(Remote& r) { return (r.carRef >= 0 && GetVehicle) ? GetVehicle(r.carRef) : nullptr; }
static bool IsAvatar(void* e)
{
    if(!e) return false;
    for(int i = 0; i < 256; ++i) if(s_AvPed[i] == e || s_AvCar[i] == e) return true;
    return false;
}
static int WeaponModel(int w)
{
    if(w <= 0 || w > 36 || !GetWeaponInfo) return -1;
    uint8_t* wi = (uint8_t*)GetWeaponInfo(w);
    if(!wi) return -1;
    int m = *(int*)(wi + WINFO_MODEL);
    return m > 0 && m < 6500 ? m : -1;
}
static bool LoadModel(int m)
{
    if(m < 0) return false;
    if(Cmd(OP_HAS_MODEL_LOADED, "i", m)) return true;
    Cmd(OP_REQUEST_MODEL, "i", m); Cmd(OP_LOAD_ALL_MODELS_NOW, "");
    return Cmd(OP_HAS_MODEL_LOADED, "i", m);
}
static int SkinModel(const Remote& r) { return (r.skin >= 1 && r.skin <= 106) ? r.skin : cfg.remoteSkin; }
static int MyChar()
{
    uint32_t h = 0;
    if(!s_pCmd) return -1;
    Cmd(OP_GET_PLAYER_CHAR, "iv", 0, &h);
    return (GetPed && GetPed((int)h)) ? (int)h : -1;
}
static void ClearRemoteBlip(Remote& r) { if(r.blip >= 0 && ClearBlip) ClearBlip(r.blip); r.blip = -1; r.blipRef = -1; }
static void DestroyAvatar(Remote& r)
{
    ClearRemoteBlip(r);
    if(RemotePed(r)) Cmd(OP_DELETE_CHAR, "i", r.pedRef);
    r.pedRef = -1; r.pedModel = -1; r.passCar = -1; r.appliedWeapon = -1; r.ducking = false; r.gotoMode = -1; r.deadApplied = false;
}
static void DestroyCar(Remote& r)
{
    void* car = RemoteCar(r);
    if(car)
    {
        float* q = MatPos(car); float* rt = MatRight(car);
        // never delete a car the local player sits in (the game would take the player with it)
        if(FindPlayerVehicle && FindPlayerVehicle() == car && s_nMyChar >= 0)
            Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", s_nMyChar, q[0] + rt[0] * 2.5f, q[1] + rt[1] * 2.5f, q[2]);
        if(RemotePed(r)) Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", r.pedRef, q[0] - rt[0] * 2.5f, q[1] - rt[1] * 2.5f, q[2]);
        Cmd(OP_DELETE_CAR, "i", r.carRef);
    }
    r.carRef = -1; r.vsAppliedCol[0] = r.vsAppliedCol[1] = -1;
}
static void DropRemote(Remote& r)
{
    DestroyCar(r);
    DestroyAvatar(r);
    r = Remote();
}
static void DropPickups() { for(auto& p : s_Pickups) Cmd(OP_REMOVE_PICKUP, "i", p.handle); s_Pickups.clear(); }
static void DropAll()
{
    for(auto& r : s_Players) if(r.used) DropRemote(r);
    for(auto& f : s_Fires) Cmd(OP_REMOVE_SCRIPT_FIRE, "i", f.handle);
    s_Fires.clear(); DropPickups(); s_TextDraws.clear();
    memset(s_AvPed, 0, sizeof(s_AvPed)); memset(s_AvCar, 0, sizeof(s_AvCar));
    s_nMyId = -1; s_bHaveVs = false; s_nKills = 0;
}
static float Dist2D(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1]; return sqrtf(x * x + y * y); }
static float Dist3(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrtf(x * x + y * y + z * z); }
static bool NearForeign(const float* p)
{
    const uint64_t now = MP_NowMs();
    for(int i = 0; i < 16; ++i) if(s_Foreign[i].at && now - s_Foreign[i].at < 2500 && Dist3(s_Foreign[i].pos, p) < 8.f) return true;
    return false;
}
static void MarkForeign(const float* p) { Foreign& f = s_Foreign[s_nForeign++ % 16]; memcpy(f.pos, p, 12); f.at = MP_NowMs(); }

// ---- network events -----------------------------------------------------------------------------------------------
static void ReplayShot(Remote& r, const ShotEntry& e);
class Events : public INetEvents
{
public:
    void OnConnected(const PktWelcome& w) override
    {
        s_nMyId = w.yourId;
        char sn[33]; memcpy(sn, w.serverName, 32); sn[32] = 0;
        Chat(0xFF80FF80, "Connected to %s (Vice City)", sn);
        Help("MP Vice City: connected");
        s_nLogoAt = MP_NowMs();   // 4.13.38: title_logo_connected2.png
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
        r.used = true; memcpy(r.name, p.name, MP_NAME_LEN); r.name[MP_NAME_LEN - 1] = 0;
        r.color = RgbaToAbgr(p.color); r.rgba = p.color ? p.color : 0xFFFFFFFF; r.named = true;
        Chat(0xFFC0C0C0, "%s joined", r.name);
    }
    void OnPlayerLeave(const PktPlayerLeave& p) override
    {
        Remote& r = s_Players[p.id];
        if(!r.used) return;
        Chat(0xFFC0C0C0, "%s left", r.name);
        DropRemote(r);
    }
    void OnSync(const PktSync& p, const SyncExt* ext) override
    {
        if(p.id == s_nMyId) return;
        Remote& r = s_Players[p.id];
        if(!r.used) { r.used = true; snprintf(r.name, sizeof(r.name), "Player %d", p.id); }
        const uint64_t now = MP_NowMs();
        r.inCar = (p.d.flags & SYNC_IN_CAR) != 0;
        r.carModel = r.inCar ? p.d.carModel : 0;
        float at[3]; memcpy(at, r.inCar ? p.d.carPos : p.d.pos, 12);
        float old[3]; memcpy(old, r.target, 12); const uint64_t oldAt = r.at;
        memcpy(r.target, at, 12);
        r.heading = r.inCar ? p.d.carHeading : p.d.heading;
        r.health = p.d.health; r.armour = p.d.armour; r.weapon = p.d.weapon; r.flags = p.d.flags; r.skin = p.d.skin; r.speed = p.d.speed;
        r.at = now;
        if(ext)
        {
            memcpy(r.vel, ext->vel, 12);
            r.hasRot = ext->carUp[0] != 0 || ext->carUp[1] != 0 || ext->carUp[2] != 0;
            memcpy(r.carFwd, ext->carFwd, 12); memcpy(r.carUp, ext->carUp, 12);
            r.hasVeh = ext->hasVeh != 0; r.vehOwner = ext->hasVeh ? ext->vehOwner : -1; r.vehSeat = ext->vehSeat;
            r.interior = ext->hasX2 ? ext->interior : 0;
        }
        else if(r.hasSync && oldAt && now > oldAt)
        {   // an old phone: velocity from the position change
            const float s = 1000.f / (float)(now - oldAt);
            for(int i = 0; i < 3; ++i) r.vel[i] = (at[i] - old[i]) * s;
            r.hasRot = false; r.hasVeh = false;
        }
        if(!r.hasSync || Dist3(r.cur, r.target) > 30.f) { memcpy(r.cur, r.target, sizeof(r.cur)); }
        r.hasSync = true;
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
        if(p.killer == s_nMyId && p.victim != s_nMyId) ++s_nKills;
    }
    void OnDamage(const PktDamage& d) override
    {   // the server only sends hits it allowed (not in a safe zone, not a team mate)
        if(d.target != s_nMyId || s_bDead || d.amount <= 0) return;
        s_nLastHitBy = d.from; s_nLastHitWeapon = d.weapon; s_nLastHitAt = MP_NowMs();
        uint32_t h = 0, a = 0; Cmd(OP_GET_PLAYER_HEALTH, "iv", 0, &h);
        int amount = (int)(d.amount + 0.5f);
        if(s_nMyChar >= 0) Cmd(OP_GET_CHAR_ARMOUR, "iv", s_nMyChar, &a);
        if(a > 0 && a < 1000)
        {   // armour takes it first (as in the game); ADD_ARMOUR_TO_PLAYER clamps, so a negative amount lowers it
            const int fromArmour = amount < (int)a ? amount : (int)a;
            Cmd(OP_ADD_ARMOUR_TO_PLAYER, "ii", 0, -fromArmour);
            amount -= fromArmour;
        }
        int nh = (int)h - amount; if(nh < 0) nh = 0;
        if(amount > 0) Cmd(OP_SET_PLAYER_HEALTH, "ii", 0, nh);
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
        if(s_bTyping) FilePick::TextReset();
        FilePick::AskText(title, "", list ? 3 : 63, list);
        s_bTyping = true; s_nAsk = ASK_CHAT;
    }
    void OnServerMessage(const PktServerMsg& p) override
    {
        char t[MP_CHAT_LEN + 1]; memcpy(t, p.text, MP_CHAT_LEN); t[MP_CHAT_LEN] = 0;
        Chat(p.color ? p.color : 0xFFFFFF80, "%s", t);
    }
    void OnSetColor(const PktSetColor& c) override
    {
        Remote& r = s_Players[c.id];
        if(!r.used) return;
        r.color = RgbaToAbgr(c.color); r.rgba = c.color;
        ClearRemoteBlip(r);   // made again in the new colour
    }
    void OnPause(const PktPause& p) override { if(p.id != s_nMyId && s_Players[p.id].used) s_Players[p.id].paused = p.paused; }
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
    // ---- 4.13.37: the rest of the SA syncs ----
    void OnShot(const PktShot& m) override
    {
        if(m.id == s_nMyId || !s_Players[m.id].used) return;
        const int n = m.count > MP_SHOT_MAX ? MP_SHOT_MAX : m.count;
        for(int i = 0; i < n; ++i) { ShotEntry e; memcpy(&e, &m.e[i], sizeof(e)); ReplayShot(s_Players[m.id], e); }
    }
    void OnVehState(const PktVehState& v) override
    {
        if(v.id == s_nMyId || !s_Players[v.id].used) return;
        Remote& r = s_Players[v.id];
        r.vsModel = v.model; r.vsCol[0] = v.col[0]; r.vsCol[1] = v.col[1]; r.vsHealth = v.health;
    }
    void OnExplosion(const PktExplosion& e) override
    {
        if(e.id == s_nMyId || e.expType > 11) return;
        float p[3]; memcpy(p, e.pos, 12);
        if(NearForeign(p)) return;   // the avatar's replayed rocket / grenade already blew up here
        if(MP_SafeZoneAt(p[0], p[1], p[2], MP_GAME_VC) >= 0) return;
        s_bApplyingNet = true;
        Cmd(OP_ADD_EXPLOSION, "fffi", p[0], p[1], p[2], (int)e.expType);
        s_bApplyingNet = false;
    }
    void OnFire(const PktFire& f) override
    {
        if(f.id == s_nMyId || s_Fires.size() >= 16) return;
        float p[3]; memcpy(p, f.pos, 12);
        if(MP_SafeZoneAt(p[0], p[1], p[2], MP_GAME_VC) >= 0) return;
        uint32_t h = 0;
        s_bApplyingNet = true;
        Cmd(OP_START_SCRIPT_FIRE, "fffv", p[0], p[1], p[2], &h);
        s_bApplyingNet = false;
        if(h) s_Fires.push_back({ (int)h, MP_NowMs() + 12000 });
    }
    void OnPickupAdd(const PktPickupAdd& a) override
    {
        for(auto& p : s_Pickups) if(p.uid == a.uid) return;
        if(!ValidVcWeapon(a.weapon) || s_Pickups.size() >= 64) return;
        const int model = WeaponModel(a.weapon);
        if(!LoadModel(model)) return;
        float p[3]; memcpy(p, a.pos, 12);
        uint32_t h = 0;
        Cmd(OP_CREATE_PICKUP_WITH_AMMO, "iiifffv", model, 3, (int)a.ammo, p[0], p[1], p[2], &h);
        if(h) s_Pickups.push_back({ a.uid, (int)h });
    }
    void OnPickupRemove(const PktPickupRemove& rm) override
    {
        for(size_t i = 0; i < s_Pickups.size(); ++i)
            if(s_Pickups[i].uid == rm.uid) { Cmd(OP_REMOVE_PICKUP, "i", s_Pickups[i].handle); s_Pickups.erase(s_Pickups.begin() + i); return; }
    }
    void OnPlayerInfo(const PktPlayerInfo& p) override
    {
        if(p.id == s_nMyId || !s_Players[p.id].used) return;
        s_Players[p.id].ping = p.ping; s_Players[p.id].score = p.score;
    }
    void OnJack(const PktJack& j) override
    {
        if(j.victim != s_nMyId) return;
        void* car = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
        if(!car || s_nMyChar < 0 || s_nSafeZone >= 0) return;
        float* q = MatPos(car); float* rt = MatRight(car);
        Cmd(OP_WARP_CHAR_FROM_CAR_TO_COORD, "ifff", s_nMyChar, q[0] - rt[0] * 2.2f, q[1] - rt[1] * 2.2f, q[2]);
        Chat(0xFF6060FF, "%s took your car", s_Players[j.from].used ? s_Players[j.from].name : "someone");
    }
    void OnTextDraw(const PktTextDraw& t) override
    {
        for(size_t i = 0; i < s_TextDraws.size(); ++i)
            if(s_TextDraws[i].tdId == t.tdId)
            {
                if(t.show) { memcpy(&s_TextDraws[i], &t, sizeof(t)); s_TextDraws[i].text[255] = 0; }
                else s_TextDraws.erase(s_TextDraws.begin() + i);
                return;
            }
        if(t.show && s_TextDraws.size() < 64) { PktTextDraw c; memcpy(&c, &t, sizeof(c)); c.text[255] = 0; s_TextDraws.push_back(c); }
    }
} s_Events;

// ---- my shots, projectiles, explosions, fires (hooks) -------------------------------------------------------------
static bool Online() { return s_Client.GetState() == NET_CONNECTED && s_nMyId >= 0; }
static void QueueShot(int kind, int weapon, const float* a, const float* b, float force)
{
    if(s_Shot.count >= MP_SHOT_MAX || !ValidVcWeapon(weapon)) return;
    ShotEntry e; e.kind = (uint8_t)kind; e.weapon = (uint8_t)weapon; memcpy(e.a, a, 12); memcpy(e.b, b, 12); e.force = force;
    memcpy(&s_Shot.e[s_Shot.count++], &e, sizeof(e));
    if(kind == 0) memcpy(s_LastAim, b, 12);
}
static void FlushShots()
{
    if(!s_Shot.count) return;
    s_Shot.type = PKT_SHOT; s_Shot.id = (uint8_t)s_nMyId;
    s_Client.Send(&s_Shot, MP_PKT_SHOT_HDR + s_Shot.count * (int)sizeof(ShotEntry));
    s_Shot.count = 0;
}
// the exact C++ types (CVector / CVector2D by value) so both ABIs pass them like the game does
DECL_HOOKv(VC_DoBulletImpact, void* self, void* shooter, void* victim, CVector* src, CVector* target, void* colpoint, CVector2D ahead)
{
    VC_DoBulletImpact(self, shooter, victim, src, target, colpoint, ahead);
    if(!Online() || !shooter || !src || !FindPlayerPed || shooter != FindPlayerPed()) return;
    const int weapon = self ? *(int*)self : 0;   // CWeapon::m_eWeaponType
    float a[3] = { src->x, src->y, src->z }, b[3] = { 0, 0, 0 };
    if(colpoint) memcpy(b, colpoint, 12);        // CColPoint::point (where it hit)
    if((!std::isfinite(b[0]) || (b[0] == 0 && b[1] == 0 && b[2] == 0)) && target) { b[0] = target->x; b[1] = target->y; b[2] = target->z; }
    if(!std::isfinite(b[0]) || (b[0] == 0 && b[1] == 0)) return;
    QueueShot(0, weapon, a, b, 0.f);
}
DECL_HOOK(bool, VC_AddProjectile, void* owner, int weapon, CVector pos, float force)
{
    if(!s_bApplyingNet && Online() && owner)
    {
        void* me = FindPlayerPed ? FindPlayerPed() : nullptr;
        void* mc = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
        if(owner == me || (mc && owner == mc))
        {
            const float* f = MatFwd(owner);
            float a[3] = { pos.x, pos.y, pos.z }, b[3] = { f[0], f[1], f[2] };
            QueueShot(owner == me ? 1 : 2, weapon, a, b, force);
            s_nLastProjectile = MP_NowMs();
        }
    }
    return VC_AddProjectile(owner, weapon, pos, force);
}
DECL_HOOK(bool, VC_AddExplosion, void* ent, void* culprit, int type, const CVector& pos, uint32_t lifetime, uint8_t sound, float radius)
{
    if(!s_bApplyingNet && Online() && culprit)
    {
        void* me = FindPlayerPed ? FindPlayerPed() : nullptr;
        void* mc = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
        float p[3] = { pos.x, pos.y, pos.z };
        if(culprit == me || (mc && culprit == mc))
        {
            const uint64_t now = MP_NowMs();
            if(type >= 0 && type <= 11 && now - s_nLastExpSent > 80)
            {
                PktExplosion e; e.type = PKT_EXPLOSION; e.id = (uint8_t)s_nMyId; e.expType = (uint8_t)type; memcpy(e.pos, p, 12);
                s_Client.Send(&e, sizeof(e)); s_nLastExpSent = now;
            }
        }
        else if(IsAvatar(culprit)) MarkForeign(p);
    }
    return VC_AddExplosion(ent, culprit, type, pos, lifetime, sound, radius);
}
static void SendFire(const float* p, float size)
{
    const uint64_t now = MP_NowMs();
    if(now - s_nLastFireSent < 300) return;
    PktFire f; f.type = PKT_FIRE; f.id = (uint8_t)s_nMyId;
    const int sz = (int)(size + 0.5f); f.size = (uint8_t)(sz < 1 ? 1 : (sz > 4 ? 4 : sz));
    memcpy(f.pos, p, 12);
    s_Client.Send(&f, sizeof(f)); s_nLastFireSent = now;
}
DECL_HOOK(void*, VC_StartFirePos, void* self, CVector pos, float size, uint8_t propagation)
{
    void* ret = VC_StartFirePos(self, pos, size, propagation);
    if(!s_bApplyingNet && Online() && FindPlayerPed)
    {   // a fire on the ground: mine when it is near me and I hold the molotov / flamethrower or just threw something
        void* me = FindPlayerPed();
        float p[3] = { pos.x, pos.y, pos.z };
        if(me && Dist3(MatPos(me), p) < 25.f && (s_nMyWeapon == 15 || s_nMyWeapon == 31 || MP_NowMs() - s_nLastProjectile < 4000)) SendFire(p, size);
    }
    return ret;
}
DECL_HOOK(void*, VC_StartFireEnt, void* self, void* burning, void* culprit, float strength, uint8_t propagation)
{
    void* ret = VC_StartFireEnt(self, burning, culprit, strength, propagation);
    if(!s_bApplyingNet && Online() && burning && culprit && FindPlayerPed)
    {
        void* me = FindPlayerPed(); void* mc = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
        if((culprit == me || (mc && culprit == mc)) && !IsAvatar(burning)) SendFire(MatPos(burning), strength);
    }
    return ret;
}

// ---- shots of the other players (PKT_SHOT) ------------------------------------------------------------------------
static void ReplayShot(Remote& r, const ShotEntry& e)
{
    void* ped = RemotePed(r);
    if(!ped || !ValidVcWeapon(e.weapon)) return;
    CVector a = { e.a[0], e.a[1], e.a[2] }, b = { e.b[0], e.b[1], e.b[2] };
    if(!std::isfinite(a.x) || !std::isfinite(b.x) || fabsf(a.x) > 6000.f || fabsf(b.x) > 6000.f) return;
    const uint64_t now = MP_NowMs();
    if(e.kind == 0)
    {   // a bullet: the tracer from their gun to where it hit, a muzzle light, and they face that way
        if(AddTrace) AddTrace(&a, &b, e.weapon, ped);
        if(AddLight) { CVector d = { 0, 0, 0 }; AddLight(0, a, d, 3.0f, 1.0f, 0.8f, 0.4f, 0, false); }
        r.shotDir[0] = b.x - a.x; r.shotDir[1] = b.y - a.y; r.lastShot = now;
    }
    else if(VC_AddProjectile)
    {   // a thrown / fired projectile (grenade, molotov, rocket): made again by the avatar, never sent back
        void* owner = ped;
        if(e.kind == 2) { owner = RemoteCar(r); if(!owner) return; }
        else { r.shotDir[0] = b.x; r.shotDir[1] = b.y; r.lastShot = now; if(SetHeading) SetHeading(ped, atan2f(-b.x, b.y)); }
        s_bApplyingNet = true;
        VC_AddProjectile(owner, e.weapon, a, e.force);
        s_bApplyingNet = false;
    }
}

// ---- per frame: the other players ---------------------------------------------------------------------------------
static void SetupAvatar(Remote& r, int ref, int model)
{
    r.pedRef = ref; r.pedModel = model; r.appliedWeapon = -1; r.ducking = false; r.gotoMode = -1; r.deadApplied = false;
    // no proofs: the hits have to register (they go to the server, the avatar itself never loses health)
    Cmd(OP_SET_CHAR_HEALTH, "ii", ref, 1000);
    Cmd(OP_SET_CHAR_STAY_IN_SAME_PLACE, "ii", ref, 1);   // not pushed away / no fleeing when shot
    Cmd(OP_CLEAR_CHAR_THREAT_SEARCH, "i", ref);
    Cmd(OP_SET_CHAR_CANT_BE_DRAGGED_OUT, "ii", ref, 0);
    Cmd(OP_CLEAR_CHAR_LAST_DAMAGE_ENTITY, "i", ref);
}
static bool CheckAvatarHit(Remote& r, int id)
{   // shooter-authoritative damage like San Andreas: my hit on the avatar -> PKT_DAMAGE (the server checks + forwards)
    if(s_nMyChar < 0 || r.pedRef < 0) return false;
    if(!Cmd(OP_HAS_CHAR_BEEN_DAMAGED_BY_CHAR, "ii", r.pedRef, s_nMyChar)) return false;
    uint32_t hp = 1000; Cmd(OP_GET_CHAR_HEALTH, "iv", r.pedRef, &hp);
    const bool dead = Cmd(OP_IS_CHAR_DEAD, "i", r.pedRef);
    const float amount = dead ? 1000.f : (float)(1000 - (int)hp);
    Cmd(OP_CLEAR_CHAR_LAST_DAMAGE_ENTITY, "i", r.pedRef);
    if(amount > 0.5f && !IsBlastWeapon(s_nMyWeapon) && !r.paused && !(r.flags & SYNC_DEAD))
    {   // explosions / fire count on the victim's phone (its own copy of the explosion), never twice
        PktDamage d; d.type = PKT_DAMAGE; d.from = (uint8_t)s_nMyId; d.target = (uint8_t)id; d.weapon = (uint8_t)s_nMyWeapon;
        d.amount = amount > 200.f ? 200.f : amount;
        s_Client.Send(&d, sizeof(d));
    }
    return dead;
}
static void UpdateRemotes(uint64_t now, float dt)
{
    memset(s_AvPed, 0, sizeof(s_AvPed)); memset(s_AvCar, 0, sizeof(s_AvCar));
    if(!GetPed || !s_pCmd) return;
    void* me = FindPlayerPed ? FindPlayerPed() : nullptr;
    if(!me) return;
    const float* myPos = MatPos(me);
    const int myArea = pCurrArea ? *pCurrArea : 0;
    void* myCar = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
    const int myCarRef = (myCar && GetVehicleRef) ? GetVehicleRef(myCar) : -1;
    const float k = dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f;
    for(int id = 0; id < 256; ++id)
    {
        Remote& r = s_Players[id];
        if(!r.used || !r.hasSync) continue;
        // where they are now: the last sync moved on by their velocity (max 0.5 s), smoothed
        float age = (float)(now - r.at) / 1000.f; if(age > 0.5f) age = 0.5f;
        float tgt[3]; for(int i = 0; i < 3; ++i) tgt[i] = r.target[i] + r.vel[i] * age;
        for(int i = 0; i < 3; ++i) r.cur[i] += (tgt[i] - r.cur[i]) * k;
        const bool hidden = (r.flags & (SYNC_MISSION | SYNC_CUTSCENE)) || (r.status & (STATUS_MISSION | STATUS_CUTSCENE));
        const bool want = !hidden && r.interior == myArea && Dist2D(myPos, tgt) < 300.f;
        if(!want) { DestroyCar(r); DestroyAvatar(r); continue; }
        void* ped = RemotePed(r);
        // dead: the avatar dies once, a new one comes when they are alive again
        if(r.flags & SYNC_DEAD)
        {
            if(ped && !r.deadApplied) { DestroyCar(r); ped = RemotePed(r); if(ped) Cmd(OP_SET_CHAR_HEALTH, "ii", r.pedRef, 0); r.deadApplied = true; }
            if(ped) s_AvPed[id] = ped;
            continue;
        }
        if(r.deadApplied) { DestroyAvatar(r); ped = nullptr; }
        const int model = SkinModel(r);
        // passenger in somebody's car (mine or another player's)
        int passCar = -1;
        if(r.inCar && r.hasVeh && r.vehOwner >= 0)
        {
            if(r.vehOwner == s_nMyId) passCar = myCarRef;
            else if(r.vehOwner < 256 && s_Players[r.vehOwner].used && RemoteCar(s_Players[r.vehOwner])) passCar = s_Players[r.vehOwner].carRef;
        }
        if(passCar >= 0)
        {
            if(r.carRef >= 0) DestroyCar(r);
            ped = RemotePed(r);
            if(!ped || r.passCar != passCar || r.pedModel != model)
            {
                DestroyAvatar(r);
                if(!LoadModel(model)) continue;
                int seat = -1;
                const int want0 = r.vehSeat >= 0 && r.vehSeat <= 2 ? r.vehSeat : 0;
                if(Cmd(OP_IS_CAR_PASSENGER_SEAT_FREE, "ii", passCar, want0)) seat = want0;
                for(int s = 0; s < 3 && seat < 0; ++s) if(Cmd(OP_IS_CAR_PASSENGER_SEAT_FREE, "ii", passCar, s)) seat = s;
                if(seat < 0) continue;
                uint32_t ref = 0;
                Cmd(OP_CREATE_CHAR_AS_PASSENGER, "iiiiv", passCar, PEDTYPE_CIVMALE, model, seat, &ref);
                if(!GetPed((int)ref)) continue;
                SetupAvatar(r, (int)ref, model); r.passCar = passCar;
                ped = RemotePed(r);
            }
            if(ped) { s_AvPed[id] = ped; Cmd(OP_SET_CHAR_HEALTH, "ii", r.pedRef, 1000); }
            continue;
        }
        if(r.passCar >= 0) { DestroyAvatar(r); ped = nullptr; }
        // the avatar: a script char in their skin
        if(ped && r.pedModel != model && !RemoteCar(r)) { DestroyAvatar(r); ped = nullptr; }
        if(!ped)
        {
            if(!LoadModel(model)) continue;
            uint32_t ref = 0;
            Cmd(OP_CREATE_CHAR, "iifffv", PEDTYPE_CIVMALE, model, tgt[0], tgt[1], tgt[2] - 1.0f, &ref);
            if(!GetPed((int)ref)) continue;
            DestroyCar(r);
            SetupAvatar(r, (int)ref, model);
            ped = RemotePed(r);
            if(!ped) continue;
        }
        s_AvPed[id] = ped;
        // my hits on them
        if(CheckAvatarHit(r, id) || Cmd(OP_IS_CHAR_DEAD, "i", r.pedRef)) { DestroyCar(r); DestroyAvatar(r); continue; }
        Cmd(OP_SET_CHAR_HEALTH, "ii", r.pedRef, 1000);
        // the weapon in their hands
        const int w = ValidVcWeapon(r.weapon) ? r.weapon : 0;
        if(w != r.appliedWeapon)
        {
            if(w == 0 || LoadModel(WeaponModel(w)))
            {
                if(w) Cmd(OP_GIVE_WEAPON_TO_CHAR, "iii", r.pedRef, w, 9999);
                Cmd(OP_SET_CURRENT_CHAR_WEAPON, "ii", r.pedRef, w);
                r.appliedWeapon = w;
            }
            else r.appliedWeapon = w;   // no model for it: don't try every frame
        }
        const float rad = r.heading * 3.14159265f / 180.0f;
        // their car (driver): same model, their colours + health, unlocked (carjack), proof (only the server breaks it)
        void* car = RemoteCar(r);
        if(car && myCar == car)
        {   // I took it: tell them, the car is mine now
            uint32_t drv = 0; Cmd(OP_GET_DRIVER_OF_CAR, "iv", r.carRef, &drv);
            if((int)drv == s_nMyChar)
            {
                PktJack j; j.type = PKT_JACK; j.from = (uint8_t)s_nMyId; j.victim = (uint8_t)id; j.seat = 0;
                s_Client.Send(&j, sizeof(j));
                Cmd(OP_SET_CAR_PROOFS, "iiiiii", r.carRef, 0, 0, 0, 0, 0);
                r.carRef = -1; r.jackedUntil = now + 4000;
                continue;
            }
        }
        if(r.inCar && IsVcCarModel(r.carModel) && GetVehicle && now >= r.jackedUntil)
        {
            uint32_t cm = 0;
            if(car) Cmd(OP_GET_CAR_MODEL, "iv", r.carRef, &cm);
            if(car && ((int)cm != r.carModel || Cmd(OP_IS_CAR_DEAD, "i", r.carRef))) { DestroyCar(r); car = nullptr; }
            if(!RemotePed(r)) continue;
            if(!car && LoadModel(r.carModel))
            {
                uint32_t ref = 0;
                Cmd(OP_CREATE_CAR, "ifffv", r.carModel, r.cur[0], r.cur[1], r.cur[2] + 0.5f, &ref);
                car = GetVehicle((int)ref);
                if(car)
                {
                    r.carRef = (int)ref;
                    Cmd(OP_SET_CAR_PROOFS, "iiiiii", r.carRef, 1, 1, 1, 1, 1);
                    Cmd(OP_LOCK_CAR_DOORS, "ii", r.carRef, 1);
                    Cmd(OP_CAR_SET_IDLE, "i", r.carRef);
                    Cmd(OP_WARP_CHAR_INTO_CAR, "ii", r.pedRef, r.carRef);
                    r.vsAppliedCol[0] = r.vsAppliedCol[1] = -1;
                }
            }
            if(car)
            {
                s_AvCar[id] = car;
                if(r.hasRot)
                {   // the whole rotation (slopes, flips, bikes leaning): right = forward x up
                    float* f = MatFwd(car); float* u = MatUp(car); float* rt = MatRight(car);
                    memcpy(f, r.carFwd, 12); memcpy(u, r.carUp, 12);
                    rt[0] = f[1] * u[2] - f[2] * u[1]; rt[1] = f[2] * u[0] - f[0] * u[2]; rt[2] = f[0] * u[1] - f[1] * u[0];
                }
                else if(SetHeading) SetHeading(car, rad);
                CVector at = { r.cur[0], r.cur[1], r.cur[2] };
                if(s_nTeleportSlot >= 0) ((void(*)(void*, CVector))((*(void***)car)[s_nTeleportSlot]))(car, at);
                else { float* q = MatPos(car); q[0] = at.x; q[1] = at.y; q[2] = at.z; }
                // their colours + damage (PKT_VEH_STATE)
                if(r.vsModel == r.carModel && r.vsCol[0] >= 0 && (r.vsCol[0] != r.vsAppliedCol[0] || r.vsCol[1] != r.vsAppliedCol[1]))
                { Cmd(OP_CHANGE_CAR_COLOUR, "iii", r.carRef, r.vsCol[0], r.vsCol[1]); r.vsAppliedCol[0] = r.vsCol[0]; r.vsAppliedCol[1] = r.vsCol[1]; }
                if(r.vsHealth >= 0) Cmd(OP_SET_CAR_HEALTH, "ii", r.carRef, r.vsHealth < 300 ? 300 : (r.vsHealth > 1000 ? 1000 : r.vsHealth));
            }
            continue;
        }
        else if(car) { DestroyCar(r); ped = RemotePed(r); if(!ped) continue; }
        if(r.inCar) continue;   // jacked a moment ago: wait for their on-foot sync
        // crouch
        const bool duck = (r.flags & SYNC_DUCKING) != 0;
        if(duck != r.ducking) { Cmd(OP_SET_CHAR_CROUCH, "iii", r.pedRef, duck ? 1 : 0, duck ? 0x7FFFFFFF : 0); r.ducking = duck; }
        // on foot: the game walks / runs / sprints them (real animations); far off = teleport
        const float* pp = MatPos(ped);
        const float d = Dist3(pp, tgt);
        const float spd = sqrtf(r.vel[0] * r.vel[0] + r.vel[1] * r.vel[1]);
        if(d > 8.f)
        {
            CVector at = { tgt[0], tgt[1], tgt[2] };
            if(PedTeleport) PedTeleport(ped, at); else { float* p = MatPos(ped); p[0] = at.x; p[1] = at.y; p[2] = at.z; }
            Cmd(OP_SET_CHAR_OBJ_NO_OBJ, "i", r.pedRef); r.gotoMode = 0;
        }
        else if(spd > 0.4f || d > 1.2f)
        {
            const float aim[2] = { tgt[0] + r.vel[0] * 0.5f, tgt[1] + r.vel[1] * 0.5f };
            const int mode = spd > 5.5f ? 3 : (spd > 2.5f || d > 4.f) ? 2 : 1;
            if(mode != r.gotoMode || Dist2D(aim, r.lastGoto) > 0.75f || now >= r.nextGoto)
            {
                const uint16_t op = mode == 3 ? OP_SET_CHAR_OBJ_SPRINT_TO_COORD : mode == 2 ? OP_SET_CHAR_OBJ_RUN_TO_COORD : OP_SET_CHAR_OBJ_GOTO_COORD_ON_FOOT;
                Cmd(op, "iff", r.pedRef, aim[0], aim[1]);
                r.gotoMode = mode; r.lastGoto[0] = aim[0]; r.lastGoto[1] = aim[1]; r.nextGoto = now + 1000;
            }
        }
        else
        {
            if(r.gotoMode != 0) { Cmd(OP_SET_CHAR_OBJ_NO_OBJ, "i", r.pedRef); r.gotoMode = 0; }
            if(SetHeading) SetHeading(ped, rad);
        }
        if(now - r.lastShot < 600 && SetHeading && (r.shotDir[0] != 0 || r.shotDir[1] != 0)) SetHeading(ped, atan2f(-r.shotDir[0], r.shotDir[1]));
    }
}

static void UpdateBlips()
{   // the radar blip sits on the avatar (moves with it, in the player's colour); a coordinate blip when there is no avatar
    for(int id = 0; id < 256; ++id)
    {
        Remote& r = s_Players[id];
        if(!r.used || !r.hasSync) continue;
        if(!cfg.blips) { ClearRemoteBlip(r); continue; }
        void* ped = RemotePed(r);
        const unsigned col = r.rgba ? r.rgba : 0xFFFFFFFF;
        if(ped && SetEntityBlip)
        {
            if(r.blip >= 0 && r.blipRef == r.pedRef) continue;
            ClearRemoteBlip(r);
            r.blip = SetEntityBlip(3 /*BLIP_CHAR*/, r.pedRef, col, 3 /*BLIP_DISPLAY_BOTH*/);
            r.blipRef = r.pedRef;
        }
        else if(SetCoordBlip)
        {
            ClearRemoteBlip(r);
            if((r.flags & (SYNC_MISSION | SYNC_CUTSCENE)) || (r.status & STATUS_MISSION)) continue;
            CVector at = { r.cur[0], r.cur[1], r.cur[2] };
            r.blip = SetCoordBlip(4 /*BLIP_COORD*/, at, col, 3 /*BLIP_DISPLAY_BOTH*/);
        }
        if(r.blip >= 0 && ChangeBlipScale) ChangeBlipScale(r.blip, 2);
    }
}

// ---- my state -> the server ---------------------------------------------------------------------------------------
static void SendVehState(uint64_t now, void* car, int carRef, int model)
{
    PktVehState v; memset(&v, 0, sizeof(v));
    v.type = PKT_VEH_STATE; v.id = (uint8_t)s_nMyId; v.model = (uint16_t)model; v.paintjob = -1; v.isAuto = 1;
    uint32_t c1 = 0, c2 = 0, h = 1000;
    Cmd(OP_GET_CAR_COLOURS, "ivv", carRef, &c1, &c2);
    Cmd(OP_GET_CAR_HEALTH, "iv", carRef, &h);
    v.col[0] = (uint8_t)c1; v.col[1] = (uint8_t)c2; v.health = (uint16_t)(h > 1000 ? 1000 : h);
    memcpy(v.pos, MatPos(car), 12);
    const bool changed = !s_bHaveVs || s_LastVs.model != v.model || s_LastVs.col[0] != v.col[0] || s_LastVs.col[1] != v.col[1]
                         || abs((int)s_LastVs.health - (int)v.health) > 25;
    if(!changed && now < s_nNextVehState) return;
    s_Client.Send(&v, sizeof(v));
    memcpy(&s_LastVs, &v, sizeof(v)); s_bHaveVs = true; s_nNextVehState = now + 2000;
}
static void SendMySync(uint64_t now)
{
    void* ped = FindPlayerPed ? FindPlayerPed() : nullptr;
    if(!ped) return;
    PktSync p; memset(&p, 0, sizeof(p));
    SyncExt e; memset(&e, 0, sizeof(e));
    p.type = PKT_SYNC; p.id = (uint8_t)(s_nMyId < 0 ? 0 : s_nMyId); p.seq = ++s_nSeq;
    const float* pos = MatPos(ped); const float* fw = MatFwd(ped);
    memcpy(p.d.pos, pos, sizeof(p.d.pos));
    p.d.heading = atan2f(-fw[0], fw[1]) * 180.0f / 3.14159265f;
    if(p.d.heading < 0) p.d.heading += 360.0f;
    uint32_t hp = 100, wp = 0, ar = 0;
    Cmd(OP_GET_PLAYER_HEALTH, "iv", 0, &hp); Cmd(OP_GET_CURRENT_PLAYER_WEAPON, "iv", 0, &wp);
    if(s_nMyChar >= 0) Cmd(OP_GET_CHAR_ARMOUR, "iv", s_nMyChar, &ar);
    s_nMyWeapon = ValidVcWeapon((int)wp) ? (int)wp : 0;
    p.d.health = (uint8_t)(hp > 255 ? 255 : hp); p.d.armour = (uint8_t)(ar > 255 ? 255 : ar); p.d.weapon = (uint8_t)s_nMyWeapon;
    const int skin = *(int16_t*)((uint8_t*)ped + ENT_MODEL);   // CEntity::m_modelIndex
    p.d.skin = (uint16_t)(skin >= 0 && skin < 200 ? skin : 0);
    memcpy(p.d.aim, s_LastAim, 12);
    // death -> PKT_DEATH once (with the last player who hurt me), my weapon drops for everybody
    if(hp == 0 && !s_bDead)
    {
        s_bDead = true;
        PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)s_nMyId; d.killer = MP_INVALID_ID; d.weapon = 255;
        if(s_nLastHitBy >= 0 && now - s_nLastHitAt < 10000) { d.killer = (uint8_t)s_nLastHitBy; d.weapon = (uint8_t)s_nLastHitWeapon; }
        s_Client.Send(&d, sizeof(d));
        if(s_nMyWeapon > 0 && s_nSafeZone < 0)
        {
            uint32_t ammo = 0; Cmd(OP_GET_AMMO_IN_PLAYER_WEAPON, "iiv", 0, s_nMyWeapon, &ammo);
            if(ammo > 0 && ammo < 100000)
            {
                PktPickupDrop pd; pd.type = PKT_PICKUP_DROP; pd.weapon = (uint8_t)s_nMyWeapon; pd.ammo = (uint16_t)(ammo > 9999 ? 9999 : ammo);
                memcpy(pd.pos, pos, 12);
                s_Client.Send(&pd, sizeof(pd));
            }
        }
        s_nLastHitBy = -1;
    }
    else if(hp > 0) s_bDead = false;
    if(s_bDead) p.d.flags |= SYNC_DEAD;
    if(s_nMyChar >= 0 && Cmd(OP_IS_CHAR_DUCKING, "i", s_nMyChar)) p.d.flags |= SYNC_DUCKING;
    if(s_nMyChar >= 0 && Cmd(OP_IS_CHAR_SHOOTING, "i", s_nMyChar)) p.d.flags |= SYNC_SHOOTING;
    void* car = FindPlayerVehicle ? FindPlayerVehicle() : nullptr;
    const float* at = car ? MatPos(car) : pos;
    // velocity from the position change (m/s)
    if(s_nLastPosAt && now > s_nLastPosAt && now - s_nLastPosAt < 1000)
    {
        const float s = 1000.f / (float)(now - s_nLastPosAt);
        for(int i = 0; i < 3; ++i) e.vel[i] = (at[i] - s_LastPos[i]) * s;
        if(sqrtf(e.vel[0] * e.vel[0] + e.vel[1] * e.vel[1]) > 120.f) memset(e.vel, 0, sizeof(e.vel));   // a teleport, not speed
    }
    memcpy(s_LastPos, at, 12); s_nLastPosAt = now;
    p.d.speed = sqrtf(e.vel[0] * e.vel[0] + e.vel[1] * e.vel[1] + e.vel[2] * e.vel[2]);
    if(car)
    {
        const float* cf = MatFwd(car);
        memcpy(p.d.carPos, at, sizeof(p.d.carPos));
        p.d.carHeading = atan2f(-cf[0], cf[1]) * 180.0f / 3.14159265f;
        if(p.d.carHeading < 0) p.d.carHeading += 360.0f;
        const int carRef = GetVehicleRef ? GetVehicleRef(car) : -1;
        uint32_t cm = 0, drv = 0;
        if(carRef >= 0) { Cmd(OP_GET_CAR_MODEL, "iv", carRef, &cm); Cmd(OP_GET_DRIVER_OF_CAR, "iv", carRef, &drv); }
        p.d.carModel = (uint16_t)(IsVcCarModel((int)cm) ? cm : 0);
        p.d.flags |= SYNC_IN_CAR;
        memcpy(e.carFwd, cf, 12); memcpy(e.carUp, MatUp(car), 12);
        if((int)drv == s_nMyChar || carRef < 0)
        {
            p.d.flags |= SYNC_DRIVER;
            if(carRef >= 0 && p.d.carModel) SendVehState(now, car, carRef, p.d.carModel);
        }
        else
        {   // passenger: in another player's car?
            for(int i = 0; i < 256; ++i)
                if(s_Players[i].used && s_Players[i].carRef == carRef) { e.hasVeh = 1; e.vehOwner = (uint8_t)i; e.vehSeat = 0; break; }
        }
    }
    e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)now;
    e.hasX2 = 1; e.x2flags = X2_SHOTSYNC; e.interior = (uint8_t)(pCurrArea ? *pCurrArea : 0);
    uint8_t buf[MP_SYNC_MAX_LEN];
    memcpy(buf, &p, sizeof(p));
    const int n = MP_EncodeSyncExt(e, buf + sizeof(PktSync));
    s_Client.SendSync(buf, (int)sizeof(PktSync) + n);

    // safe zones (the server drops every hit in or into them)
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
    // scoreboard: my ping + kills
    if(now >= s_nNextInfo)
    {
        PktPlayerInfo pi; pi.type = PKT_PLAYER_INFO; pi.id = p.id; pi.ping = (uint16_t)s_Client.GetPing(); pi.score = (int16_t)s_nKills;
        s_Client.Send(&pi, sizeof(pi));
        s_nNextInfo = now + 3000;
    }
}
static void UpdatePickupsAndFires(uint64_t now)
{
    for(size_t i = 0; i < s_Pickups.size();)
    {
        if(Cmd(OP_HAS_PICKUP_BEEN_COLLECTED, "i", s_Pickups[i].handle))
        {
            PktPickupTake t; t.type = PKT_PICKUP_TAKE; t.uid = s_Pickups[i].uid;
            s_Client.Send(&t, sizeof(t));
            Cmd(OP_REMOVE_PICKUP, "i", s_Pickups[i].handle);
            s_Pickups.erase(s_Pickups.begin() + i);
        }
        else ++i;
    }
    for(size_t i = 0; i < s_Fires.size();)
    {
        if(now >= s_Fires[i].until) { Cmd(OP_REMOVE_SCRIPT_FIRE, "i", s_Fires[i].handle); s_Fires.erase(s_Fires.begin() + i); }
        else ++i;
    }
}

// ---- chat typing, dialog answers, the MP menu (phone keyboard) ----------------------------------------------------
static void SendChatText(const char* text)
{
    if(!text[0]) return;
    PktChat c; memset(&c, 0, sizeof(c));
    c.type = PKT_CHAT; c.id = (uint8_t)s_nMyId;
    MP_CopyStr(c.text, text, MP_CHAT_LEN);
    s_Client.Send(&c, sizeof(c));
    if(text[0] != '/') Chat(0xFFFFFFFF, "%s: %s", cfg.name, text);   // the server sends chat to the others only
}
static void Ask(int what, const char* title, const char* init, int maxLen, bool numeric)
{
    if(s_bTyping) return;
    if(FilePick::AskText(title, init, maxLen, numeric)) { s_bTyping = true; s_nAsk = what; }
}
static void Reconnect()
{
    if(s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true);
    DropAll();
    s_bManualOff = false; s_bJoinTried = false; s_nNextJoin = 0;
}
static void ShowMenu()
{
    Chat(0xFFFFE080, "[MP Vice City] %s:%d %s", cfg.host[0] ? cfg.host : "(no server)", cfg.port, Online() ? "online" : "offline");
    Chat(0xFFFFFFFF, "1. Players   2. Server   3. Name (%s)   4. Reconnect", cfg.name);
    Chat(0xFFFFFFFF, "5. Map markers %s   6. Name tags %s   7. Disconnect", cfg.blips ? "on" : "off", cfg.nameTags ? "on" : "off");
    Ask(ASK_MENU, "MP menu - type the number", "", 2, true);
}
static void MenuPick(int n)
{
    switch(n)
    {
    case 1: s_bScoreboard = !s_bScoreboard; break;
    case 2: { char init[160]; snprintf(init, sizeof(init), "%s:%d", cfg.host, cfg.port); Ask(ASK_HOST, "Server - host:port", init, 120, false); break; }
    case 3: Ask(ASK_NAME, "Your name", cfg.name, MP_NAME_LEN - 1, false); break;
    case 4: Chat(0xFF80FF80, "Joining %s:%d again", cfg.host, cfg.port); Reconnect(); break;
    case 5: cfg.blips = !cfg.blips; SaveSettings(); Chat(0xFF80FF80, "Map markers %s", cfg.blips ? "on" : "off"); break;
    case 6: cfg.nameTags = !cfg.nameTags; SaveSettings(); Chat(0xFF80FF80, "Name tags %s", cfg.nameTags ? "on" : "off"); break;
    case 7: if(s_Client.GetState() != NET_DISCONNECTED) s_Client.Disconnect(true); DropAll(); s_bManualOff = true; Chat(0xFFC0C0C0, "Disconnected (MP menu, 4 joins again)"); break;
    default: break;
    }
}
static void PollTyping()
{
    if(!s_bTyping) return;
    const int st = FilePick::TextState();
    if(st == 1 || st == 0) return;
    std::string t = st == 3 ? FilePick::Text() : std::string();
    FilePick::TextReset();
    s_bTyping = false;
    const int what = s_nAsk; s_nAsk = ASK_CHAT;
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
    if(st != 3) return;
    switch(what)
    {
    case ASK_MENU: MenuPick(atoi(t.c_str())); break;
    case ASK_HOST:
    {
        std::string h = t; int port = cfg.port;
        size_t c = h.rfind(':');
        if(c != std::string::npos) { port = atoi(h.c_str() + c + 1); h = h.substr(0, c); }
        char hb[128]; MP_CopyStr(hb, h.c_str(), sizeof(hb)); Trim(hb);
        if(hb[0]) { MP_CopyStr(cfg.host, hb, sizeof(cfg.host)); cfg.port = port > 0 && port < 65536 ? port : 7777; SaveSettings(); Reconnect(); }
        break;
    }
    case ASK_NAME:
    {
        char nb[MP_NAME_LEN]; MP_CopyStr(nb, t.c_str(), sizeof(nb)); Trim(nb);
        if(nb[0]) { MP_CopyStr(cfg.name, nb, sizeof(cfg.name)); SaveSettings(); if(Online()) Reconnect(); }
        break;
    }
    default: if(Online()) SendChatText(t.c_str()); break;
    }
}
static bool s_bWasDown = false;
static void PollButtons()
{
    if(!PointerGetButton || !PointerGetCoords) return;
    int st = PointerGetButton(0, 0);
    bool down = st == 2 || st == 3;
    if(down && !s_bWasDown && !s_bTyping)
    {
        int x = 0, y = 0; float pr = 0; PointerGetCoords(0, &x, &y, &pr);
        for(int i = 0; i < BTN_COUNT; ++i)
        {
            const int* b = s_Btn[i];
            if(b[2] <= 0 || x < b[0] || x > b[0] + b[2] || y < b[1] || y > b[1] + b[3]) continue;
            if(i == BTN_CHAT) Ask(ASK_CHAT, "Chat - or a /command", "", MP_CHAT_LEN - 1, false);
            else if(i == BTN_PLAYERS) s_bScoreboard = !s_bScoreboard;
            else ShowMenu();
            break;
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
    s_nMyChar = MyChar();

    if(cfg.host[0] && !s_bManualOff && s_Client.GetState() == NET_DISCONNECTED && now >= s_nNextJoin)
    {
        if(!s_bJoinTried) Chat(0xFFFFFFFF, "MP Vice City: joining %s:%d as %s", cfg.host, cfg.port, cfg.name);
        s_bJoinTried = true;
        s_Client.SetGame(MP_GAME_VC);
        s_Client.SetJoinPassword(cfg.password);
        s_Client.Connect(cfg.host, (uint16_t)cfg.port, cfg.name, 0);
        s_nNextJoin = now + 20000;
    }
    s_Client.Poll(&s_Events);
    PollTyping();
    PollButtons();
    if(!Online()) { s_Shot.count = 0; return; }
    if(now >= s_nNextSync) { s_nNextSync = now + 100; SendMySync(now); }
    FlushShots();
    UpdateRemotes(now, dt > 0.25f ? 0.25f : dt);
    UpdatePickupsAndFires(now);
    if(now >= s_nNextBlips) { s_nNextBlips = now + 500; UpdateBlips(); }
}

// ---- drawing ------------------------------------------------------------------------------------------------------
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
static void Box(float l, float t, float r, float b, CRGBA c)
{
    if(!DrawRect) return;
    CRect rc = { l, b, r, t };
    DrawRect(rc, c);
}
static void DrawTextDraws(float sw, float sh)
{   // SA-MP textdraws from the server's gamemodes (640 x 448 canvas)
    const float kx = sw / 640.f, ky = sh / 448.f;
    for(auto& t : s_TextDraws)
    {
        const float x = t.x * kx, y = t.y * ky;
        const float lx = t.letterX > 0.05f ? t.letterX : 0.48f, ly = t.letterY > 0.05f ? t.letterY : 1.12f;
        if(t.useBox && DrawRect)
        {
            const uint32_t bc = t.boxColor;
            CRGBA c = { (uint8_t)(bc >> 24), (uint8_t)(bc >> 16), (uint8_t)(bc >> 8), (uint8_t)bc };
            int lines = 1; for(const char* p = t.text; *p; ++p) if(!strncmp(p, "~n~", 3)) ++lines;
            float l = t.align == 2 ? x - t.boxX * kx * 0.5f : x;
            float r = t.align == 2 ? x + t.boxX * kx * 0.5f : t.boxX * kx;
            if(r <= l) r = l + 120.f * kx;
            Box(l - 2, y - 2, r + 2, y + ly * 9.f * ky * lines + 2, c);
        }
        if(FontSetStyle) FontSetStyle(t.font == 2 ? 2 : (t.font == 3 ? 0 : 1));
        if(FontSetScale) FontSetScale(lx * kx, ly * ky * 0.5f);
        if(FontSetDropShadow) FontSetDropShadow((short)(t.outline ? t.outline : t.shadow));
        if(t.align == 2) { if(FontSetCentreOn) FontSetCentreOn(); if(FontSetCentreSize) FontSetCentreSize(sw); }
        else if(t.align == 3 && FontSetRightOn) FontSetRightOn();
        else if(FontSetJustifyOn) FontSetJustifyOn();
        const uint32_t c = t.color ? t.color : 0xFFFFFFFF;
        Text(x, y, t.text, ((uint32_t)(c & 0xFF) << 24) | ((c >> 24) & 0xFF) | (((c >> 16) & 0xFF) << 8) | (((c >> 8) & 0xFF) << 16));
        if(FontSetCentreOff) FontSetCentreOff();
        if(FontSetRightOff) FontSetRightOff();
    }
}
static void DrawScoreboard(float sw, float sh, float sc)
{
    int n = 1; for(auto& r : s_Players) if(r.used) ++n;
    const float lh = 26.f * sc, w = 560.f * sc;
    const float l = sw * 0.5f - w * 0.5f, t = sh * 0.18f;
    const float rows = n + 1.5f > 20.f ? 20.f : n + 1.5f;
    Box(l, t, l + w, t + lh * rows, CRGBA{ 0, 0, 0, 170 });
    char line[160];
    snprintf(line, sizeof(line), "Players: %d       id  name                kills  ping", n);
    Text(l + 10 * sc, t + 6 * sc, line, 0xFF80E0FF);
    float y = t + 6 * sc + lh;
    snprintf(line, sizeof(line), "%3d  %-20s %5d %5d  (you)", s_nMyId, cfg.name, s_nKills, s_Client.GetPing());
    Text(l + 10 * sc, y, line, 0xFFFFFFFF); y += lh;
    for(int id = 0; id < 256 && y < t + lh * (rows - 0.5f); ++id)
    {
        const Remote& r = s_Players[id];
        if(!r.used) continue;
        snprintf(line, sizeof(line), "%3d  %-20s %5d %5d%s", id, r.name, r.score, r.ping, r.paused ? "  (paused)" : "");
        Text(l + 10 * sc, y, line, r.color ? r.color : 0xFFFFFFFF); y += lh;
    }
}
// ---- 4.13.38: Vice City title images (files/TYFMC/MP in the gtavc folder) ----------------------------------------
// title2.png while MP joins a server, title_logo_connected2.png for 7 s after joining (San Andreas: title.png and
// title_logo_connected.png). The PNG is stretched to a 1024 x 512 texture (GL wants power-of-two sizes) and drawn
// in its own shape with CSprite2d::Draw.
struct VcImage { CSprite2d spr = { nullptr }; int w = 0, h = 0; bool tried = false; };
static VcImage s_ImgTitle, s_ImgConnected;
static bool LoadImage(VcImage& im, const char* file)
{
    if(im.tried) return im.spr.tex != nullptr;
    im.tried = true;
    if(!RwImageCreate || !RwImageAllocatePixels || !RwImageDestroy || !RwImageFindRasterFormat || !RwRasterCreate ||
       !RwRasterSetFromImage || !RwTextureCreate || !SpriteDraw) { logger->Info("Vice City: no RenderWare image functions - %s not shown", file); return false; }
    char path[640]; snprintf(path, sizeof(path), "%s/%s", s_szDataDir, file);
    int w = 0, h = 0, n = 0;
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);
    if(!px) { logger->Info("Vice City: %s not found - copy it into files/TYFMC/MP of GTA Vice City", path); return false; }
    const int TW = 1024, TH = 512;
    RwImage* img = RwImageCreate(TW, TH, 32);
    if(img) RwImageAllocatePixels(img);
    if(!img || !img->cpPixels || img->stride < TW * 4) { if(img) RwImageDestroy(img); stbi_image_free(px); return false; }
    for(int y = 0; y < TH; ++y)
    {   // bilinear stretch into the texture
        const float fy = (y + 0.5f) * h / TH - 0.5f; int y0 = (int)floorf(fy); const float ty = fy - y0;
        const int ya = y0 < 0 ? 0 : (y0 >= h ? h - 1 : y0), yb = y0 + 1 >= h ? h - 1 : (y0 + 1 < 0 ? 0 : y0 + 1);
        uint8_t* row = img->cpPixels + (size_t)y * img->stride;
        for(int x = 0; x < TW; ++x)
        {
            const float fx = (x + 0.5f) * w / TW - 0.5f; int x0 = (int)floorf(fx); const float tx = fx - x0;
            const int xa = x0 < 0 ? 0 : (x0 >= w ? w - 1 : x0), xb = x0 + 1 >= w ? w - 1 : (x0 + 1 < 0 ? 0 : x0 + 1);
            const uint8_t *p00 = px + ((size_t)ya * w + xa) * 4, *p01 = px + ((size_t)ya * w + xb) * 4,
                          *p10 = px + ((size_t)yb * w + xa) * 4, *p11 = px + ((size_t)yb * w + xb) * 4;
            for(int c = 0; c < 4; ++c)
                row[x * 4 + c] = (uint8_t)((p00[c] * (1 - tx) + p01[c] * tx) * (1 - ty) + (p10[c] * (1 - tx) + p11[c] * tx) * ty + 0.5f);
        }
    }
    stbi_image_free(px);
    int rw = 0, rh = 0, rd = 0, rf = 0;
    RwImageFindRasterFormat(img, 4 /* rwRASTERTYPETEXTURE */, &rw, &rh, &rd, &rf);
    void* ras = (rw == TW && rh == TH) ? RwRasterCreate(rw, rh, rd, rf) : nullptr;
    if(ras) ras = RwRasterSetFromImage(ras, img);
    RwImageDestroy(img);
    if(!ras) { logger->Error("Vice City: %s: no %dx%d texture (format %dx%d)", file, TW, TH, rw, rh); return false; }
    void* tex = RwTextureCreate(ras);
    if(!tex) return false;
    // RwTexture::filterAddressing (after raster, dict, link, name[32], mask[32]): linear filter, clamped edges
    *(uint32_t*)((uint8_t*)tex + 4 * sizeof(void*) + 64) = 0x3302;
    im.spr.tex = tex; im.w = w; im.h = h;
    logger->Info("Vice City: loaded %s (%dx%d)", path, w, h);
    return true;
}
static void DrawImage(VcImage& im, const char* file, float cx, float cy, float maxW, float maxH, float alpha)
{
    if(alpha <= 0.0f || !LoadImage(im, file)) return;
    const float k = fminf(maxW / im.w, maxH / im.h), w = im.w * k, h = im.h * k;
    if(RwRenderStateSet)
    {
        RwRenderStateSet(12 /* VERTEXALPHAENABLE */, (void*)(intptr_t)1);
        RwRenderStateSet(10 /* SRCBLEND */, (void*)(intptr_t)5 /* SRCALPHA */);
        RwRenderStateSet(11 /* DESTBLEND */, (void*)(intptr_t)6 /* INVSRCALPHA */);
    }
    CRect rc = { cx - w * 0.5f, cy + h * 0.5f, cx + w * 0.5f, cy - h * 0.5f };
    CRGBA c = { 255, 255, 255, (uint8_t)(255.0f * (alpha > 1.0f ? 1.0f : alpha)) };
    SpriteDraw(&im.spr, rc, c);
    if(RwRenderStateSet) RwRenderStateSet(1 /* TEXTURERASTER */, nullptr);
}
static void DrawTitles(float sw, float sh, bool online, uint64_t now)
{
    if(!s_nFirstFrame) s_nFirstFrame = now;
    if(s_nLogoAt)
    {   // like San Andreas: 7 s, the last second fades out
        const uint64_t t = now - s_nLogoAt;
        if(t >= 7000) s_nLogoAt = 0;
        else DrawImage(s_ImgConnected, "title_logo_connected2.png", sw * 0.5f, sh * 0.5f, sw * 0.60f, sh * 0.40f, t > 6000 ? (7000 - t) / 1000.0f : 1.0f);
        return;
    }
    // joining (or the first 6 s in the game): title2.png at the top
    const bool joining = !online && cfg.host[0] && !s_bManualOff;
    if(joining || now - s_nFirstFrame < 6000)
        DrawImage(s_ImgTitle, "title2.png", sw * 0.5f, sh * 0.13f, sw * 0.36f, sh * 0.20f, 1.0f);
}

static void Draw()
{
    if(!FontPrint || !ScreenH || !FindPlayerPed || !FindPlayerPed()) return;
    const float sh = (float)ScreenH(), sw = ScreenW ? (float)ScreenW() : sh * 16.f / 9.f;
    const float sc = cfg.textScale * sh / 720.0f;
    const bool online = Online();
    DrawTitles(sw, sh, online, MP_NowMs());
    if(FontSetBgOff) FontSetBgOff();
    if(FontSetDropColor) FontSetDropColor(CRGBA{ 0, 0, 0, 255 });
    if(online) DrawTextDraws(sw, sh);
    if(FontSetStyle) FontSetStyle(1);
    if(FontSetScale) FontSetScale(0.8f * sc, 1.0f * sc);
    if(FontSetJustifyOn) FontSetJustifyOn();
    if(FontSetDropShadow) FontSetDropShadow(1);

    const uint64_t now = MP_NowMs();
    {   // buttons: Chat / Players (online), MP (always: server, name, reconnect...)
        const char* label[BTN_COUNT] = { s_bTyping ? "[ typing... ]" : "[ Chat ]", "[ Players ]", "[ MP ]" };
        const float bw[BTN_COUNT] = { 120.f, 140.f, 80.f };
        float x = sh * 0.03f; const float y = sh * 0.22f;
        for(int i = 0; i < BTN_COUNT; ++i)
        {
            const bool show = i == BTN_MP || online;
            s_Btn[i][0] = (int)x; s_Btn[i][1] = (int)y; s_Btn[i][2] = show ? (int)(bw[i] * sc) : 0; s_Btn[i][3] = (int)(40 * sc);
            if(!show) continue;
            Text(x, y + 8 * sc, label[i], 0xFF80E0FF);
            x += (bw[i] + 14.f) * sc;
        }
    }
    float y = sh * 0.30f;
    for(auto& l : s_Chat)
    {
        if(now - l.at > 15000) continue;
        Text(sh * 0.03f, y, l.text.c_str(), l.color);
        y += 26.0f * sc;
    }
    if(online && CalcScreenCoors && cfg.nameTags)
    {   // name tags + health / armour bars over the avatars
        for(auto& r : s_Players)
        {
            if(!r.used || !r.hasSync || r.pedRef < 0) continue;
            void* ped = RemotePed(r);
            if(!ped) continue;
            const float* p = MatPos(ped);
            RwV3d in = { p[0], p[1], p[2] + 1.15f }, out; float w, h;
            if(!CalcScreenCoors(in, &out, &w, &h, true) || out.z <= 0.5f || out.z > 80.0f) continue;
            char tag[MP_NAME_LEN + 16]; snprintf(tag, sizeof(tag), "%s%s", r.name, r.paused ? " (paused)" : "");
            Text(out.x - strlen(tag) * 6.0f * sc, out.y, tag, r.color ? r.color : 0xFFFFFFFF);
            if(DrawRect)
            {
                const float bw = 80.f * sc, bh = 7.f * sc, bl = out.x - bw * 0.5f; float bt = out.y + 26.f * sc;
                const float hp = r.health > 100 ? 1.f : r.health / 100.f;
                Box(bl - 1, bt - 1, bl + bw + 1, bt + bh + 1, CRGBA{ 0, 0, 0, 200 });
                Box(bl, bt, bl + bw * hp, bt + bh, CRGBA{ 190, 30, 30, 230 });
                if(r.armour)
                {
                    bt += bh + 3.f * sc; const float ar = r.armour > 100 ? 1.f : r.armour / 100.f;
                    Box(bl - 1, bt - 1, bl + bw + 1, bt + bh + 1, CRGBA{ 0, 0, 0, 200 });
                    Box(bl, bt, bl + bw * ar, bt + bh, CRGBA{ 220, 220, 220, 230 });
                }
            }
        }
    }
    if(online && s_bScoreboard) DrawScoreboard(sw, sh, sc);
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
    Sym(GetPedRef, "_ZN6CPools9GetPedRefEP4CPed", missing);
    Sym(GetPed, "_ZN6CPools6GetPedEi", missing);
    Sym(PedTeleport, "_ZN4CPed8TeleportE7CVector", missing);
    Sym(SetHeading, "_ZN10CPlaceable10SetHeadingEf", missing);
    Sym(SetHelpMessage, "_ZN4CHud14SetHelpMessageEPtbbb", missing);
    Sym(FontSetScale, "_ZN5CFont8SetScaleEff", missing);
    Sym(FontSetColor, "_ZN5CFont8SetColorE5CRGBA", missing);
    Sym(FontSetDropColor, "_ZN5CFont12SetDropColorE5CRGBA", missing);
    Sym(FontSetDropShadow, "_ZN5CFont21SetDropShadowPositionEs", missing);
    Sym(FontSetStyle, "_ZN5CFont12SetFontStyleEs", missing);
    Sym(FontSetJustifyOn, "_ZN5CFont12SetJustifyOnEv", missing);
    Sym(FontSetCentreOn, "_ZN5CFont11SetCentreOnEv", missing);
    Sym(FontSetCentreOff, "_ZN5CFont12SetCentreOffEv", missing);
    Sym(FontSetRightOn, "_ZN5CFont17SetRightJustifyOnEv", missing);
    Sym(FontSetRightOff, "_ZN5CFont18SetRightJustifyOffEv", missing);
    Sym(FontSetCentreSize, "_ZN5CFont13SetCentreSizeEf", missing);
    Sym(FontSetBgOff, "_ZN5CFont15SetBackgroundOffEv", missing);
    Sym(FontPrint, "_ZN5CFont11PrintStringEffPt", missing);
    Sym(CalcScreenCoors, "_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_b", missing);
    Sym(ScreenW, "_Z17OS_ScreenGetWidthv", missing);
    Sym(ScreenH, "_Z18OS_ScreenGetHeightv", missing);
    Sym(SetCoordBlip, "_ZN6CRadar12SetCoordBlipE9eBlipType7CVectorj12eBlipDisplay", missing);
    Sym(SetEntityBlip, "_ZN6CRadar13SetEntityBlipE9eBlipTypeij12eBlipDisplay", missing);
    Sym(ChangeBlipScale, "_ZN6CRadar15ChangeBlipScaleEii", missing);
    Sym(ClearBlip, "_ZN6CRadar9ClearBlipEi", missing);
    Sym(DrawRect, "_ZN9CSprite2d8DrawRectERK5CRectRK5CRGBA", missing);
    Sym(SpriteDraw, "_ZN9CSprite2d4DrawERK5CRectRK5CRGBA", missing);
    Sym(RwImageCreate, "_Z13RwImageCreateiii", missing);
    Sym(RwImageAllocatePixels, "_Z21RwImageAllocatePixelsP7RwImage", missing);
    Sym(RwImageDestroy, "_Z14RwImageDestroyP7RwImage", missing);
    Sym(RwImageFindRasterFormat, "_Z23RwImageFindRasterFormatP7RwImageiPiS1_S1_S1_", missing);
    Sym(RwRasterCreate, "_Z14RwRasterCreateiiii", missing);
    Sym(RwRasterSetFromImage, "_Z20RwRasterSetFromImageP8RwRasterP7RwImage", missing);
    Sym(RwTextureCreate, "_Z15RwTextureCreateP8RwRaster", missing);
    Sym(RwRenderStateSet, "_Z16RwRenderStateSet13RwRenderStatePv", missing);
    Sym(AddLight, "_ZN12CPointLights8AddLightEh7CVectorS0_ffffhb", missing);
    Sym(GetWeaponInfo, "_ZN11CWeaponInfo13GetWeaponInfoE11eWeaponType", missing);
    Sym(AddTrace, "_ZN13CBulletTraces8AddTraceEP7CVectorS1_iP7CEntity", missing);
    Sym(pCurrArea, "_ZN5CGame8currAreaE", missing);
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
    if(!GetPed || !GetVehicle) { logger->Error("Vice City: CPools::GetPed / GetVehicle not found - MP stays off"); return false; }

    LoadSettings();
    const bool scripts = InitScripts();
    logger->Info("Vice City: script commands %s", scripts ? "on" : "OFF - MP Vice City needs them, multiplayer stays off");
    if(!scripts) return false;
    memset(&s_Shot, 0, sizeof(s_Shot));
    HOOKSYM(VC_CGame_Process, s_hGame, "_ZN5CGame7ProcessEv");
    if(aml->GetSym(s_hGame, "_ZN4CHud4DrawEv")) HOOKSYM(VC_CHud_Draw, s_hGame, "_ZN4CHud4DrawEv");
    // 4.13.37: bullet / projectile / explosion / fire sync (each one only when the game has the function)
    int hooks = 0;
    auto H = [&](const char* sym) { bool ok = aml->GetSym(s_hGame, sym) != 0; if(!ok) logger->Info("Vice City: %s not found (that sync is off)", sym); else ++hooks; return ok; };
    if(H("_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D")) HOOKSYM(VC_DoBulletImpact, s_hGame, "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPoint9CVector2D");
    if(H("_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorf")) HOOKSYM(VC_AddProjectile, s_hGame, "_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorf");
    if(H("_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionTypeRK7CVectorjhf")) HOOKSYM(VC_AddExplosion, s_hGame, "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionTypeRK7CVectorjhf");
    if(H("_ZN12CFireManager9StartFireE7CVectorfh")) HOOKSYM(VC_StartFirePos, s_hGame, "_ZN12CFireManager9StartFireE7CVectorfh");
    if(H("_ZN12CFireManager9StartFireEP7CEntityS1_fh")) HOOKSYM(VC_StartFireEnt, s_hGame, "_ZN12CFireManager9StartFireEP7CEntityS1_fh");
    logger->Info("Vice City: MP Vice City layer on (%d game functions missing, %d/5 sync hooks), %d missions / %d cutscenes known, %d hospital safe zones",
                 missing, hooks, g_VCMissionCount, g_VCCutsceneCount, g_MPSafeZoneCountVC);
    return true;
}
} // namespace VC
