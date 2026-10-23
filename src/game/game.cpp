#include <mod/amlmod.h>
#include <mod/logger.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "scripting.h"
#include "../../imgui/RW/RenderWare.h"

extern RsGlobalType* RsGlobal;

struct RwV3dLocal { float x, y, z; };
static bool (*CalcScreenCoors)(const RwV3dLocal&, RwV3dLocal*, float*, float*, bool, bool) = nullptr;
static bool* m_UserPause = nullptr;
static bool* m_CodePause = nullptr;
static int16_t* m_OldWeather = nullptr;
static int16_t* m_NewWeather = nullptr;
static int16_t* m_ForcedWeather = nullptr;
static int32_t* m_WeatherTypeInList = nullptr;
static const int8_t* (*FindWeatherTypesList)() = nullptr;
static uint8_t*  m_ClockHours = nullptr;
static uint8_t*  m_ClockMinutes = nullptr;
static uint32_t* m_MsPerGameMinute = nullptr;
static uint32_t* m_LastClockTick = nullptr;
static uint32_t* m_TimeInMs = nullptr;
static uint32_t  s_nOrigMsPerMinute = 0;
static uint32_t* m_OnAMissionFlag = nullptr;   // byte offset of $ONMISSION inside ScriptSpace
static uint8_t*  m_ScriptSpace = nullptr;
static void**    m_pActiveScripts = nullptr;   // CRunningScript* list head
static bool*     m_CutsceneRunning = nullptr;
static uint8_t*  m_MobileMenu = nullptr;        // gMobileMenu
static void*     m_PcSaveHelper = nullptr;
static void (*GenerateGameFilename)(void*, int, char*) = nullptr;
static bool (*CheckSlotDataValid)(int, bool) = nullptr;
static float*    m_StatFloat = nullptr;
static void**    m_ModelInfoPtrs = nullptr;    // CModelInfo::ms_modelInfoPtrs (2.12: skin safety check)
static uint8_t   s_BadModel[320];               // ped models that failed to load once: never request again
static char      s_szAndroidData[300] = "";
// MobileMenu layout (aml-psdk, CHECKSIZE 0xB0 / 0xD0): m_bWantToLoad, then int m_nSaveSlot
#ifdef AML32
  #define MM_WANTLOAD_OFS 77
  #define MM_SLOT_OFS     80
#else
  #define MM_WANTLOAD_OFS 109
  #define MM_SLOT_OFS     112
#endif

#define F2U(f) ((uint32_t*)&(f))

bool Game::Init(void* h)
{
    bool ok = Scripting::Init(h);
    InitNetState(h);   // 2.13: anims, smooth motion, explosions / fires / pickups
    InitNetState217(h);   // 2.17: shots, car state, aim, interiors, map + mission markers
    InitNpcGame(h);       // 2.18: NPC sync
    InitGame219(h);       // 2.19: camera, cheats, jetpack, joystick
    InitSkins(h);         // 2.19.6: player model sync + story characters
    SET_TO(CalcScreenCoors, aml->GetSym(h, "_ZN7CSprite15CalcScreenCoorsERK5RwV3dPS0_PfS4_bb"));
    SET_TO(m_UserPause, aml->GetSym(h, "_ZN6CTimer11m_UserPauseE"));
    SET_TO(m_CodePause, aml->GetSym(h, "_ZN6CTimer11m_CodePauseE"));
    SET_TO(m_OldWeather, aml->GetSym(h, "_ZN8CWeather14OldWeatherTypeE"));
    SET_TO(m_NewWeather, aml->GetSym(h, "_ZN8CWeather14NewWeatherTypeE"));
    SET_TO(m_ForcedWeather, aml->GetSym(h, "_ZN8CWeather17ForcedWeatherTypeE"));
    SET_TO(m_WeatherTypeInList, aml->GetSym(h, "_ZN8CWeather17WeatherTypeInListE"));
    SET_TO(FindWeatherTypesList, aml->GetSym(h, "_ZN8CWeather20FindWeatherTypesListEv"));
    SET_TO(m_ClockHours, aml->GetSym(h, "_ZN6CClock18ms_nGameClockHoursE"));
    SET_TO(m_ClockMinutes, aml->GetSym(h, "_ZN6CClock20ms_nGameClockMinutesE"));
    SET_TO(m_MsPerGameMinute, aml->GetSym(h, "_ZN6CClock29ms_nMillisecondsPerGameMinuteE"));
    SET_TO(m_LastClockTick, aml->GetSym(h, "_ZN6CClock17ms_nLastClockTickE"));
    SET_TO(m_TimeInMs, aml->GetSym(h, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    SET_TO(m_OnAMissionFlag, aml->GetSym(h, "_ZN11CTheScripts14OnAMissionFlagE"));
    SET_TO(m_ScriptSpace, aml->GetSym(h, "_ZN11CTheScripts11ScriptSpaceE"));
    SET_TO(m_pActiveScripts, aml->GetSym(h, "_ZN11CTheScripts14pActiveScriptsE"));
    SET_TO(m_CutsceneRunning, aml->GetSym(h, "_ZN12CCutsceneMgr10ms_runningE"));
    SET_TO(m_MobileMenu, aml->GetSym(h, "gMobileMenu"));
    SET_TO(m_PcSaveHelper, aml->GetSym(h, "PcSaveHelper"));
    SET_TO(GenerateGameFilename, aml->GetSym(h, "_ZN8C_PcSave20GenerateGameFilenameEiPc"));
    SET_TO(CheckSlotDataValid, aml->GetSym(h, "_ZN19CGenericGameStorage18CheckSlotDataValidEib"));
    SET_TO(m_StatFloat, aml->GetSym(h, "_ZN6CStats14StatTypesFloatE"));
    SET_TO(m_ModelInfoPtrs, aml->GetSym(h, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
    snprintf(s_szAndroidData, sizeof(s_szAndroidData), "%s", aml->GetAndroidDataPath());
    logger->Info("Save slots: menu %s, filename %s, check %s, stats %s", m_MobileMenu ? "yes" : "no",
        GenerateGameFilename ? "yes" : "no", CheckSlotDataValid ? "yes" : "no", m_StatFloat ? "yes" : "no");
    logger->Info("Mission detect: flag %s, scripts %s, cutscene %s",
        (m_OnAMissionFlag && m_ScriptSpace) ? "yes" : "no", m_pActiveScripts ? "yes" : "no", m_CutsceneRunning ? "yes" : "no");
    logger->Info("World lock: clock %s, weather %s, weather cycle %s",
        (m_ClockHours && m_ClockMinutes) ? "yes" : "no (script fallback)",
        (m_OldWeather && m_NewWeather && m_ForcedWeather) ? "yes" : "no (script fallback)",
        (FindWeatherTypesList && m_WeatherTypeInList) ? "yes" : "no");
    if(!CalcScreenCoors) logger->Error("CSprite::CalcScreenCoors not found - name tags disabled");
    return ok;
}

bool Game::IsPaused()
{
    return (m_UserPause && *m_UserPause) || (m_CodePause && *m_CodePause);
}

// CRunningScript layout (aml-psdk, CHECKSIZE 0x100 / 0x130): m_pNext at 0, m_szName[8] after the
// two list pointers, m_bIsMission is the last byte.
#ifdef AML32
  #define RS_NAME_OFS    8
  #define RS_MISSION_OFS 252
#else
  #define RS_NAME_OFS    16
  #define RS_MISSION_OFS 300
#endif
bool Game::IsRealCutscene() { return m_CutsceneRunning && *m_CutsceneRunning; }

void Game::GetMissionState(bool& onMission, bool& cutscene, char missionName[8])
{
    onMission = false; cutscene = false; missionName[0] = 0;
    if(m_OnAMissionFlag && m_ScriptSpace)
    {
        uint32_t ofs = *m_OnAMissionFlag;
        if(ofs > 0 && ofs < 0x20000 && (ofs & 3) == 0) onMission = *(int32_t*)(m_ScriptSpace + ofs) != 0;
    }
    if(m_pActiveScripts)
    {
        uint8_t* s = (uint8_t*)*m_pActiveScripts;
        for(int n = 0; s && n < 256; ++n, s = *(uint8_t**)s)
        {
            if(s[RS_MISSION_OFS] == 0) continue;
            const char* nm = (const char*)(s + RS_NAME_OFS);
            int k = 0;
            for(; k < 7 && nm[k]; ++k)
            {
                char c = nm[k];
                if(c >= 'a' && c <= 'z') c -= 32;
                if(!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) { k = 0; break; }
                missionName[k] = c;
            }
            missionName[k] = 0;
            if(k > 0) { onMission = true; break; }
        }
    }
    if(m_CutsceneRunning && *m_CutsceneRunning) cutscene = true;
    // scripted scenes (black bars, camera moves) take the player's control away during a mission
    else if(onMission && Scripting::IsReady() && !CALLSCM(IS_PLAYER_CONTROL_ON, 0)) cutscene = true;
}

bool Game::SaveSlotPath(int slot, char* out, int outSize)
{
    if(slot < 0 || slot > 9 || outSize < 16) return false;
    char name[400] = "";
    if(GenerateGameFilename && m_PcSaveHelper) GenerateGameFilename(m_PcSaveHelper, slot, name);
    if(!name[0] || strlen(name) > 300) snprintf(name, sizeof(name), "GTASAsf%d.b", slot + 1);
    if(name[0] == '/') snprintf(out, outSize, "%s", name);
    else snprintf(out, outSize, "%s/%s", s_szAndroidData, name);
    return true;
}

bool Game::RequestLoadSlot(int slot)
{
    if(!m_MobileMenu || slot < 0 || slot > 9) return false;
    if(CheckSlotDataValid && !CheckSlotDataValid(slot, false)) { logger->Error("Save slot %d is not valid", slot + 1); return false; }
    *(int32_t*)(m_MobileMenu + MM_SLOT_OFS) = slot;
    m_MobileMenu[MM_WANTLOAD_OFS] = 1;
    logger->Info("Requested loading save slot %d", slot + 1);
    return true;
}

bool Game::IsLoadPending() { return m_MobileMenu && m_MobileMenu[MM_WANTLOAD_OFS] != 0; }
void Game::CancelLoad()    { if(m_MobileMenu) m_MobileMenu[MM_WANTLOAD_OFS] = 0; }

float Game::ProgressPercent()
{
    if(!m_StatFloat) return -1.0f;
    float made = m_StatFloat[0], total = m_StatFloat[1];
    if(!(total > 1.0f) || total > 10000.0f || made < 0) return -1.0f;
    return made / total * 100.0f;
}

int Game::LocalPed()
{
    uint32_t ped = 0;
    CALLSCM(GET_PLAYER_CHAR, 0, &ped);
    return (int)ped;
}

bool Game::IsReady() { return Scripting::IsReady() && LocalPed() > 0; }

bool Game::GetCharPos(int ped, Vec3& o)
{
    if(ped <= 0) return false;
    o.x = o.y = o.z = 0.0f;
    CALLSCM(GET_CHAR_COORDINATES, ped, F2U(o.x), F2U(o.y), F2U(o.z));
    return !(o.x == 0.0f && o.y == 0.0f && o.z == 0.0f);
}

// 00A1 expects a "foot" z. baseOffset = centre-to-foot distance measured for this ped.
void Game::SetCharPos(int ped, const Vec3& c0, float baseOffset) { Vec3 c = c0; if(!MP_WorldPos(c) || !MP_Finite(baseOffset)) return; CALLSCM(SET_CHAR_COORDINATES, ped, c.x, c.y, c.z - baseOffset); }
float Game::GetCharHeading(int ped) { float h = 0; CALLSCM(GET_CHAR_HEADING, ped, F2U(h)); return h; }
void  Game::SetCharHeading(int ped, float h) { CALLSCM(SET_CHAR_HEADING, ped, h); }
int   Game::GetCharHealth(int ped) { uint32_t v = 0; CALLSCM(GET_CHAR_HEALTH, ped, &v); return (int)v; }
void  Game::SetCharHealth(int ped, int hp) { CALLSCM(SET_CHAR_HEALTH, ped, hp); }
int   Game::GetCharArmour(int ped) { uint32_t v = 0; CALLSCM(GET_CHAR_ARMOUR, ped, &v); return (int)v; }
int   Game::GetCharWeapon(int ped) { uint32_t v = 0; CALLSCM(GET_CURRENT_CHAR_WEAPON, ped, &v); return (int)v; }
float Game::GetCharSpeed(int ped) { float s = 0; CALLSCM(GET_CHAR_SPEED, ped, F2U(s)); return s; }
bool  Game::IsCharDead(int ped) { return CALLSCM(IS_CHAR_DEAD, ped) != 0; }
bool  Game::IsCharShooting(int ped) { return CALLSCM(IS_CHAR_SHOOTING, ped) != 0; }
bool  Game::IsCharDucking(int ped) { return CALLSCM(IS_CHAR_DUCKING, ped) != 0; }
bool  Game::IsCharInAnyCar(int ped) { return ped > 0 && PedPtr(ped) && CALLSCM(IS_CHAR_IN_ANY_CAR, ped) != 0; }   // 2.28: dead handle = not in a car
int   Game::GetCarCharIsIn(int ped) { if(ped <= 0 || !PedPtr(ped)) return 0; uint32_t c = 0; CALLSCM(STORE_CAR_CHAR_IS_IN_NO_SAVE, ped, &c); return (int)c; }
bool  Game::HasCharBeenDamagedByChar(int ped, int by) { return CALLSCM(HAS_CHAR_BEEN_DAMAGED_BY_CHAR, ped, by) != 0; }
void  Game::ClearCharLastDamageEntity(int ped) { CALLSCM(CLEAR_CHAR_LAST_DAMAGE_ENTITY, ped); }

void Game::GetTimeOfDay(int& h, int& m)
{
    uint32_t hh = 12, mm = 0;
    CALLSCM(GET_TIME_OF_DAY, &hh, &mm);
    h = (int)hh; m = (int)mm;
}
void Game::SetTimeOfDay(int h, int m) { CALLSCM(SET_TIME_OF_DAY, h, m); }
int  Game::GetWeather() { return (m_OldWeather && *m_OldWeather >= 0 && *m_OldWeather < 256) ? *m_OldWeather : 1; }
void Game::ForceWeatherNow(int w) { CALLSCM(FORCE_WEATHER_NOW, w); }
void Game::ReleaseWeather() { CALLSCM(RELEASE_WEATHER); }

bool Game::CanLockWorld() { return m_ClockHours && m_ClockMinutes && m_OldWeather && m_NewWeather && m_ForcedWeather; }

int Game::EnforceWorld(uint32_t absMinute, float secFrac, int forcedWeather)
{
    int fixed = 0;
    const int wantH = (int)((absMinute / 60) % 24), wantM = (int)(absMinute % 60);

    // ---- clock ----
    int curH, curM;
    if(m_ClockHours && m_ClockMinutes) { curH = *m_ClockHours; curM = *m_ClockMinutes; }
    else GetTimeOfDay(curH, curM);
    int d = (wantH * 60 + wantM) - (curH * 60 + curM);
    if(d < 0) d = -d;
    if(d > 720) d = 1440 - d;
    if(d > 1)
    {
        if(m_ClockHours && m_ClockMinutes)
        {
            *m_ClockHours = (uint8_t)wantH; *m_ClockMinutes = (uint8_t)wantM;
            if(m_LastClockTick && m_TimeInMs)
            {
                uint32_t per = (m_MsPerGameMinute && *m_MsPerGameMinute) ? *m_MsPerGameMinute : 1000;
                *m_LastClockTick = *m_TimeInMs - (uint32_t)(secFrac * per);
            }
        }
        else SetTimeOfDay(wantH, wantM);
        curH = wantH; curM = wantM;
        fixed |= WORLD_FIX_TIME;
    }
    if(m_MsPerGameMinute)   // time speed (some scripts make days pass faster)
    {
        if(!s_nOrigMsPerMinute) s_nOrigMsPerMinute = *m_MsPerGameMinute ? *m_MsPerGameMinute : 1000;
        if(*m_MsPerGameMinute != 1000) { *m_MsPerGameMinute = 1000; fixed |= WORLD_FIX_SPEED; }
    }

    // ---- weather ----
    if(!(m_OldWeather && m_NewWeather && m_ForcedWeather))
    {
        int w = forcedWeather >= 0 ? forcedWeather : 1;
        if(GetWeather() != w) { ForceWeatherNow(w); fixed |= WORLD_FIX_WEATHER; }
        return fixed;
    }
    if(forcedWeather >= 0)
    {
        int16_t w = (int16_t)forcedWeather;
        if(*m_ForcedWeather != w || *m_OldWeather != w || *m_NewWeather != w)
        {
            if(*m_ForcedWeather != w && *m_ForcedWeather >= 0) fixed |= WORLD_FIX_WEATHER;
            *m_ForcedWeather = w; *m_OldWeather = w; *m_NewWeather = w;
        }
        return fixed;
    }
    // the game's normal weather cycle: one list step per game hour, 64 steps.
    // The hour count comes from the server clock, aligned to our (already locked) game hour so
    // the game's own hour change and ours never fight.
    if(*m_ForcedWeather >= 0) { *m_ForcedWeather = -1; fixed |= WORLD_FIX_WEATHER; }
    if(FindWeatherTypesList && m_WeatherTypeInList)
    {
        int64_t hours = (int64_t)(absMinute / 60);
        int dh = curH - (int)(hours % 24);
        if(dh > 12) dh -= 24;
        if(dh < -12) dh += 24;
        hours += dh;
        int idx = (int)(hours % 64);
        const int8_t* list = FindWeatherTypesList();
        if(list)
        {
            int16_t wantNew = list[idx], wantOld = list[(idx + 63) % 64];
            *m_WeatherTypeInList = idx;
            if(*m_NewWeather != wantNew || *m_OldWeather != wantOld)
            {
                // a different weather than the cycle = something forced it (weather only
                // follows the list by itself, apart from crossing into another map area)
                *m_OldWeather = wantOld; *m_NewWeather = wantNew;
            }
        }
    }
    return fixed;
}

void Game::ReleaseWorld()
{
    if(m_MsPerGameMinute && s_nOrigMsPerMinute) *m_MsPerGameMinute = s_nOrigMsPerMinute;
    s_nOrigMsPerMinute = 0;
    if(m_ForcedWeather) *m_ForcedWeather = -1;
    else ReleaseWeather();
}

// 2.24: any model id we are about to REQUEST_MODEL must really exist in THIS game. Ids from the
// network (NPC sync, other phones with limit adjusters / modded IDE files) can be unknown here,
// and CStreaming::RequestModel on such an id crashes (SIGSEGV in _ZN10CStreaming12RequestModelEii).
bool Game::IsKnownModel(int model)
{
    if(model <= 0 || model >= 20000) return false;   // stock MODELINFOSIZE; above it the table may be elsewhere
    if(model < 320 && s_BadModel[model]) return false;
    if(m_ModelInfoPtrs)
    {
        bool tableLive = m_ModelInfoPtrs[105] && m_ModelInfoPtrs[106] && m_ModelInfoPtrs[400];
        if(tableLive && !m_ModelInfoPtrs[model]) return false;
    }
    return true;
}
bool Game::IsKnownCarModel(int model) { return model >= 400 && model <= 611 && IsKnownModel(model); }

bool Game::LoadModel(int model)
{
    if(!IsKnownModel(model)) return false;   // 2.24
    if(!CALLSCM(HAS_MODEL_LOADED, model))
    {
        CALLSCM(REQUEST_MODEL, model);
        CALLSCM(LOAD_ALL_MODELS_NOW);
    }
    return CALLSCM(HAS_MODEL_LOADED, model) != 0;
}

// 2.12: a remote player's skin comes from the network, and some mod setups (model limit
// adjusters, model variations, replaced peds.ide) don't have every ped. REQUEST_MODEL on a
// model without a model info crashes inside CStreaming::RequestModel (seen with 269 Big Smoke
// on 64-bit + ZyZ mods). Only request ped models that really exist; otherwise use GSF 2.
bool Game::IsSafePedModel(int model)
{
    if(model <= 0 || model > 311 || model == 74) return false;   // 0 = CJ (player only), 74 = unused
    if(model >= 290 && model <= 299) return false;               // SPECIAL01..10 slots (mission scripts)
    if(s_BadModel[model]) return false;
    if(m_ModelInfoPtrs)
    {
        // a limit adjuster may have moved the table: if even the stock GSF / cop models are
        // empty here, this copy isn't the live one, so don't judge by it
        bool tableLive = m_ModelInfoPtrs[105] && m_ModelInfoPtrs[106] && m_ModelInfoPtrs[280];
        if(tableLive && !m_ModelInfoPtrs[model]) return false;
    }
    return true;
}

int Game::CreateRemotePed(int model, const Vec3& p0)
{
    Vec3 p = p0; if(!MP_WorldPos(p)) return 0;   // 2.33
    int specialSlot = 0;
    if(IsSpecialSkin(model))   // 2.19.6: story character -> one of MP's special slots
    {
        int m = ModelForSkin(model, specialSlot);
        if(m)
        {
            uint32_t ped = 0;
            CALLSCM(CREATE_CHAR, 24 /* PEDTYPE_MISSION1 */, m, p.x, p.y, p.z - 1.0f, &ped);
            if(ped)
            {
                NoteSpecialPed(specialSlot, (int)ped);
                CALLSCM(SET_CHAR_DROPS_WEAPONS_WHEN_DEAD, ped, 0);
                CALLSCM(SET_CHAR_CANT_BE_DRAGGED_OUT, ped, 1);
                CALLSCM(SET_CHAR_SUFFERS_CRITICAL_HITS, ped, 0);
                CALLSCM(SET_CHAR_ACCURACY, ped, 0);
                CALLSCM(SET_CHAR_KEEP_TASK, ped, 1);
                CALLSCM(SET_CHAR_HEALTH, ped, 1000);
                MakePuppet((int)ped);   // 2.21
                return (int)ped;
            }
        }
        model = 106;   // no free slot right now / not in this game's files
    }
    if(!IsSafePedModel(model)) model = 106;
    if(!LoadModel(model))
    {
        if(model > 0 && model < 320) s_BadModel[model] = 1;
        model = 106;
        if(!LoadModel(model)) { model = 105; if(!LoadModel(model)) return 0; }   // fallback: Grove Street Families member
    }
    uint32_t ped = 0;
    CALLSCM(CREATE_CHAR, 24 /* PEDTYPE_MISSION1 */, model, p.x, p.y, p.z - 1.0f, &ped);
    if(ped)
    {
        CALLSCM(SET_CHAR_DROPS_WEAPONS_WHEN_DEAD, ped, 0);
        CALLSCM(SET_CHAR_CANT_BE_DRAGGED_OUT, ped, 1);
        CALLSCM(SET_CHAR_SUFFERS_CRITICAL_HITS, ped, 0);
        CALLSCM(SET_CHAR_ACCURACY, ped, 0);   // remote bullets are cosmetic; damage is sent over the network
        CALLSCM(SET_CHAR_KEEP_TASK, ped, 1);
        CALLSCM(SET_CHAR_HEALTH, ped, 1000);
        MakePuppet((int)ped);   // 2.21: never attacks the local player by itself
    }
    CALLSCM(MARK_MODEL_AS_NO_LONGER_NEEDED, model);
    return (int)ped;
}

void Game::DeleteChar(int ped) { if(ped > 0) CALLSCM(DELETE_CHAR, ped); }

void Game::GiveAndSelectWeapon(int ped, int weapon)
{
    if(ped <= 0 || !PedPtr(ped)) return;   // 2.19.1: never on a ped that is gone (crash in CPed::GiveWeapon)
    if(weapon < 0 || weapon > 46) return;   // 2.19.1: only real weapon ids
    // 2.26: crash in CPed::GiveWeapon (libGTASA + 0x49F61E). 19-21 are empty weapon slots in SA
    // (no weapon info); 44/45 goggles and 46 parachute need player data, which another
    // player's avatar (a normal ped) doesn't have. A weapon whose model isn't loaded (weapon
    // mods / replaced .dff, streaming full) is never given either.
    if(weapon >= 19 && weapon <= 21) return;
    if(weapon >= 44 && ped != LocalPed()) weapon = 0;
    static uint8_t s_BadWeapon[47] = {0};
    if(weapon > 0)
    {
        if(s_BadWeapon[weapon] >= 3) weapon = 0;   // failed 3 times: show fists, stop asking
        else
        {
            uint32_t model = 0;
            CALLSCM(GET_WEAPONTYPE_MODEL, weapon, &model);
            if((int)model <= 0 || !LoadModel((int)model) || !PedPtr(ped)) { ++s_BadWeapon[weapon]; return; }
            CALLSCM(GIVE_WEAPON_TO_CHAR, ped, weapon, 9999);
        }
    }
    CALLSCM(SET_CURRENT_CHAR_WEAPON, ped, weapon);
}

void Game::TaskGoTo(int ped, const Vec3& p0, int moveState) { Vec3 p = p0; if(!MP_WorldPos(p)) return; CALLSCM(TASK_GO_STRAIGHT_TO_COORD, ped, p.x, p.y, p.z, moveState, 2000); }
void Game::TaskStandStill(int ped) { CALLSCM(TASK_STAND_STILL, ped, 1000); }
void Game::TaskShootAt(int ped, const Vec3& p0, int t) { Vec3 p = p0; if(!MP_WorldPos(p)) return; CALLSCM(TASK_SHOOT_AT_COORD, ped, p.x, p.y, p.z, t); }
void Game::TaskAimAt(int ped, const Vec3& p0, int t) { Vec3 p = p0; if(!MP_WorldPos(p)) return; CALLSCM(TASK_AIM_GUN_AT_COORD, ped, p.x, p.y, p.z, t); }
void Game::TaskDuck(int ped, bool d) { CALLSCM(TASK_TOGGLE_DUCK, ped, d ? 1 : 0); }
void Game::TaskDie(int ped) { CALLSCM(TASK_DIE, ped); }

int Game::CreateCar(int model, const Vec3& p0)
{
    Vec3 p = p0; if(!MP_WorldPos(p)) return 0;   // 2.33
    if(model < 400 || model > 611 || !LoadModel(model)) return 0;
    uint32_t car = 0;
    CALLSCM(CREATE_CAR, model, p.x, p.y, p.z, &car);
    CALLSCM(MARK_MODEL_AS_NO_LONGER_NEEDED, model);
    return (int)car;
}
void  Game::DeleteCar(int car) { if(car > 0) CALLSCM(DELETE_CAR, car); }
bool  Game::GetCarPos(int car, Vec3& o) { o.x = o.y = o.z = 0; if(car <= 0 || !CarPtr(car)) return false; CALLSCM(GET_CAR_COORDINATES, car, F2U(o.x), F2U(o.y), F2U(o.z)); return true; }
void  Game::SetCarPos(int car, const Vec3& c0, float off) { Vec3 c = c0; if(!MP_WorldPos(c) || !MP_Finite(off)) return; CALLSCM(SET_CAR_COORDINATES, car, c.x, c.y, c.z - off); }
float Game::GetCarHeading(int car) { float h = 0; CALLSCM(GET_CAR_HEADING, car, F2U(h)); return h; }
void  Game::SetCarHeading(int car, float h) { CALLSCM(SET_CAR_HEADING, car, h); }
int   Game::GetCarModel(int car) { uint32_t m = 0; CALLSCM(GET_CAR_MODEL, car, &m); return (int)m; }
void  Game::WarpIntoCar(int ped, int car) { CALLSCM(WARP_CHAR_INTO_CAR, ped, car); }
void  Game::WarpOutOfCar(int ped, const Vec3& p0) { Vec3 p = p0; if(!MP_WorldPos(p)) return; CALLSCM(WARP_CHAR_FROM_CAR_TO_COORD, ped, p.x, p.y, p.z); }

int Game::AddBlipForChar(int ped, uint32_t rgba)
{
    uint32_t blip = 0;
    CALLSCM(ADD_BLIP_FOR_CHAR, ped, &blip);
    if(blip)
    {
        CALLSCM(CHANGE_BLIP_COLOUR, blip, (int)rgba); // values > 8 are treated as RGBA
        CALLSCM(CHANGE_BLIP_DISPLAY, blip, 2);        // radar only (no 3D marker)
        CALLSCM(CHANGE_BLIP_SCALE, blip, 2);
    }
    return (int)blip;
}
void Game::SetBlipColour(int blip, uint32_t rgba) { if(blip > 0) CALLSCM(CHANGE_BLIP_COLOUR, blip, (int)rgba); }
void Game::RemoveBlip(int blip) { if(blip > 0) CALLSCM(REMOVE_BLIP, blip); }

void Game::Teleport(int ped, const Vec3& foot, float heading)
{
    if(ped <= 0) return;
    CALLSCM(SET_AREA_VISIBLE, 0);
    CALLSCM(SET_CHAR_AREA_VISIBLE, ped, 0);
    CALLSCM(REQUEST_COLLISION, foot.x, foot.y);
    CALLSCM(LOAD_SCENE, foot.x, foot.y, foot.z);
    if(IsCharInAnyCar(ped)) WarpOutOfCar(ped, foot);
    else CALLSCM(SET_CHAR_COORDINATES, ped, foot.x, foot.y, foot.z);
    SetCharHeading(ped, heading);
}

bool Game::WorldToScreen(const Vec3& w, float& sx, float& sy, float& depth)
{
    if(!CalcScreenCoors) return false;
    RwV3dLocal in = { w.x, w.y, w.z }, out = { 0, 0, 0 };
    float ww = 0, hh = 0;
    if(!CalcScreenCoors(in, &out, &ww, &hh, true, true)) return false;
    if(out.z <= 1.0f) return false;
    sx = out.x; sy = out.y; depth = out.z;
    return true;
}

int Game::ScreenWidth()  { return RsGlobal ? RsGlobal->maximumWidth : 1280; }
int Game::ScreenHeight() { return RsGlobal ? RsGlobal->maximumHeight : 720; }
