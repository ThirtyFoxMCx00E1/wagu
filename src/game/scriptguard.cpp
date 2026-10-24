// 2.16.2: fixes this crash (GTA SA 2.00 armv7, found in a user's AML crash log):
//
//   Exception Signal 11 - SIGSEGV   libGTASA.so + 0x34F022 (CRunningScript::ProcessCommands1000To1099 + 3421)
//   called from CLEOPlus / CLEOMod HookOf_ProcessOneCommand <- CRunningScript::Process, script "_carfix.csa"
//
// The instruction is `ldr r1, [r5, #0x14]` (CPlaceable::m_matrix) with r5 = 0. It belongs to opcode
// 0407 (GET_OFFSET_FROM_CAR_IN_WORLD_COORDS); 0400 (..._FROM_OBJECT_...) shares the same code. The game
// turns the car / object handle into a pointer, and when the handle is dead (negative, or its pool
// slot was freed / reused) it keeps going with a NULL pointer. A script that keeps a handle for a
// while (CLEO scripts do, for example in a loop with `wait 0`) crashes the whole game when that car
// is deleted, despawns, or the game was loaded in between. The 64-bit game has the same code.
//
// The guard sits in front of ProcessCommands1000To1099: for these two opcodes it asks the game's own
// pool whether the handle is alive; if not, the opcode is skipped (see scriptguard_core.h). Everything
// else - and every valid handle - goes straight to the game's original code.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdint.h>
#include "scriptguard.h"
#include "scriptguard_core.h"

// CRunningScript::m_szName (char[8], not always 0-terminated): after the two list pointers
#ifdef AML32
  #define OFS_SCRIPT_NAME 8
#else
  #define OFS_SCRIPT_NAME 16
#endif

static ScriptGuard::Env s_Env = {};
static int s_nSkipped = 0;

SDECL_HOOK(int8_t, ProcessCommands1000To1099, void* script, int opcode)
{
    int handle = 0;
    if(ScriptGuard::ShouldSkip(s_Env, script, opcode, handle))
    {
        ++s_nSkipped;
        if(s_nSkipped <= 5 || s_nSkipped % 1000 == 0)   // a script that loops every frame must not flood the log
        {
            char name[9];
            memcpy(name, (const uint8_t*)script + OFS_SCRIPT_NAME, 8);
            name[8] = 0;
            for(int i = 0; i < 8; ++i) if(name[i] && (name[i] < 32 || name[i] > 126)) name[i] = '?';
            logger->Info("ScriptGuard: %04X skipped, %s 0x%X doesn't exist (script \"%s\", %d so far)", opcode,
                         opcode == ScriptGuard::OP_GET_OFFSET_FROM_OBJECT ? "object" : "car", (unsigned)handle, name, s_nSkipped);
        }
        return 0;   // the game's "carry on with the next command"
    }
    return ProcessCommands1000To1099(script, opcode);
}

// Only hook code we have seen: the first instructions of the function in GTA SA 2.00 (armv7, Thumb-2)
// and 2.10 (arm64). Anything else (another build, or something already hooked this function) is left alone.
static bool LooksLikeKnownBuild(void* fn)
{
#ifdef AML32
    uintptr_t a = (uintptr_t)fn & ~(uintptr_t)1;
    return *(volatile uint16_t*)a == 0xB5F0 && *(volatile uint16_t*)(a + 2) == 0xAF03;   // push {r4-r7, lr}; add r7, sp, #12
#else
    return *(volatile uint32_t*)fn == 0xD10283FF;                                        // sub sp, sp, #0xa0
#endif
}

// ------------------------------------------------------------------ 2.37: CPhysical::Add guard
// Crash log (GTA SA 2.00 armv7, 68 mods): SIGSEGV, fault address 0x751D, libGTASA.so + 0x3FCF6C
// (_ZN9CPhysical3AddEv). See scriptguard_core.h CheckAdd() for why Add can go wild. Every car / ped /
// object added to the world - by MP, a CLEO script or any other mod - passes this check first:
//   position NaN / inf          -> not added (log)       position past the map edge -> pulled back to the edge, stopped
//   rect NaN / huge / off-map   -> not added (log)       node pools nearly empty    -> not added (log)
// "Not added" = the entity exists but is not in the sector lists (no collision, not drawn) until
// the game adds it again; CPhysical::Remove on it is safe (its entry list is empty).
#ifdef AML32
  #define OFS_PLACEMENT_POS 4
  #define OFS_MATRIX        20
  #define OFS_MOVE_SPEED    72
#else
  #define OFS_PLACEMENT_POS 8
  #define OFS_MATRIX        24
  #define OFS_MOVE_SPEED    104
#endif
#define OFS_MATRIX_POS 48

struct GuardRect { float a, b, c, d; };
struct GuardPool { void* objects; uint8_t* byteMap; int32_t size; int32_t firstFree; };   // CPool<T> (both ABIs)

static GuardRect (*s_GetBoundRect)(void* ent) = nullptr;   // CEntity::GetBoundRect (POD return: same ABI as a plain function)
static GuardPool** s_PtrNodeDoublePool = nullptr;          // CPools::ms_pPtrNodeDoubleLinkPool
static GuardPool** s_EntryInfoPool = nullptr;              // CPools::ms_pEntryInfoNodePool
static int s_nAddFixed = 0, s_nPoolRefusals = 0;
static bool s_bPoolCheck = true;

static int FreeSlots(GuardPool** pp)
{
    if(!s_bPoolCheck || !pp || !*pp) return -1;
    GuardPool* p = *pp;
    if(!p->byteMap || p->size <= 0 || p->size > 2000000) return -1;
    int n = 0;
    for(int i = 0; i < p->size; ++i) if(p->byteMap[i] & 0x80) ++n;   // tPoolObjectFlags::bEmpty = top bit (aml-psdk Pools.h)
    return n;
}

static float* PosOf(void* ent)
{
    uint8_t* m = *(uint8_t**)((uint8_t*)ent + OFS_MATRIX);
    return (float*)(m ? m + OFS_MATRIX_POS : (uint8_t*)ent + OFS_PLACEMENT_POS);
}

static void LogAdd(const char* what, void* ent, const float* p, int need)
{
    ++s_nAddFixed;
    if(s_nAddFixed <= 10 || s_nAddFixed % 500 == 0)
        logger->Info("ScriptGuard: CPhysical::Add %s (entity %p at %.1f %.1f %.1f, %d sectors, %d so far)", what, ent, p[0], p[1], p[2], need, s_nAddFixed);
}

SDECL_HOOK(int, PhysicalAdd, void* ent)   // mobile: virtual int CEntity::Add()
{
    if(!ent) return 0;
    float* pos = PosOf(ent);
    float p[3] = { pos[0], pos[1], pos[2] };
    GuardRect r = s_GetBoundRect(ent);
    float rc[4] = { r.a, r.b, r.c, r.d };
    int need = 0;
    ScriptGuard::AddVerdict v = ScriptGuard::CheckAdd(p, rc, FreeSlots(s_PtrNodeDoublePool), FreeSlots(s_EntryInfoPool), &need);
    if(v == ScriptGuard::ADD_CLAMP_POS)
    {
        pos[0] = ScriptGuard::ClampWorld(pos[0]); pos[1] = ScriptGuard::ClampWorld(pos[1]);
        r = s_GetBoundRect(ent);
        float rr[4] = { r.a, r.b, r.c, r.d }, dx = 0, dy = 0;
        if(ScriptGuard::Fin(rr[0]) && ScriptGuard::Fin(rr[1]) && ScriptGuard::Fin(rr[2]) && ScriptGuard::Fin(rr[3]))
        { ScriptGuard::ShiftInside(rr, dx, dy); pos[0] += dx; pos[1] += dy; }
        float* mv = (float*)((uint8_t*)ent + OFS_MOVE_SPEED); mv[0] = mv[1] = mv[2] = 0.0f;
        LogAdd("past the map edge, moved back inside", ent, p, 0);
        p[0] = pos[0]; p[1] = pos[1];
        r = s_GetBoundRect(ent);
        float rc2[4] = { r.a, r.b, r.c, r.d };
        v = ScriptGuard::CheckAdd(p, rc2, FreeSlots(s_PtrNodeDoublePool), FreeSlots(s_EntryInfoPool), &need);
    }
    switch(v)
    {
        case ScriptGuard::ADD_OK:              return PhysicalAdd(ent);
        case ScriptGuard::ADD_REFUSE_BAD_POS:  LogAdd("skipped: position is NaN / inf", ent, p, 0); return 0;
        case ScriptGuard::ADD_REFUSE_BAD_RECT: LogAdd("skipped: broken bounding box", ent, p, 0); return 0;
        case ScriptGuard::ADD_REFUSE_POOL:
            // a wrong pool reading must never stop everything from spawning: after 300 refusals the pool check turns itself off
            if(++s_nPoolRefusals > 300) { s_bPoolCheck = false; logger->Info("ScriptGuard: pool check off (300 refusals - pools look different in this game)"); return PhysicalAdd(ent); }
            LogAdd("skipped: world node pools are full", ent, p, need); return 0;
        default:                               LogAdd("skipped", ent, p, 0); return 0;
    }
}

static void InitAddGuard(void* h)
{
    void* fn = (void*)aml->GetSym(h, "_ZN9CPhysical3AddEv");
    SET_TO(s_GetBoundRect, aml->GetSym(h, "_ZNK7CEntity12GetBoundRectEv"));   // const in 2.00 / 2.10
    if(!s_GetBoundRect) SET_TO(s_GetBoundRect, aml->GetSym(h, "_ZN7CEntity12GetBoundRectEv"));
    SET_TO(s_PtrNodeDoublePool, aml->GetSym(h, "_ZN6CPools25ms_pPtrNodeDoubleLinkPoolE"));
    SET_TO(s_EntryInfoPool, aml->GetSym(h, "_ZN6CPools22ms_pEntryInfoNodePoolE"));
    if(!fn || !s_GetBoundRect)
    {
        logger->Info("ScriptGuard: world-add guard off (CPhysical::Add / CEntity::GetBoundRect not found)");
        return;
    }
    if(!aml->Hook(fn, (void*)&HookOf_PhysicalAdd, (void**)&PhysicalAdd) || !PhysicalAdd)
    {
        logger->Info("ScriptGuard: world-add guard off (hook failed)");
        return;
    }
    logger->Info("ScriptGuard: world-add guard on (pools: %s / %s)", s_PtrNodeDoublePool ? "ok" : "-", s_EntryInfoPool ? "ok" : "-");
}

// ---- 3.10: collision guard (see colguard_core.h for the crash it stops) -----------------------
#include "colguard_core.h"
static int (*ProcessColModels)(void*, void*, void*, void*, void*, void*, void*, bool) = nullptr;
static void (*ColCalcPlanes)(void*) = nullptr;       // CCollision::CalculateTrianglePlanes(CCollisionData*)
static void (*DataCalcPlanes)(void*) = nullptr;      // CCollisionData::CalculateTrianglePlanes()
static int  s_nColFixes = 0;
static void NoteColFix(const void* cd, int fix, const char* where)
{
    if(!fix) return;
    if(++s_nColFixes <= 20)
        logger->Info("ColGuard: broken collision data %p fixed (%s%s%s%s) in %s - this would have crashed the game (fix #%d)", cd,
                     fix & ColGuard::FIX_TRIS ? "triangles without vertices " : "", fix & ColGuard::FIX_SPH ? "spheres " : "",
                     fix & ColGuard::FIX_BOX ? "boxes " : "", fix & ColGuard::FIX_LINES ? "lines " : "", where, s_nColFixes);
}
static inline void FixColModel(void* cm, const char* where)
{
    if(!cm || ColGuard::BadPtr((uintptr_t)cm)) return;
    uint8_t* cd = *(uint8_t**)((uint8_t*)cm + ColGuard::ColDataOffset());
    if(!cd || ColGuard::BadPtr((uintptr_t)cd)) return;
    NoteColFix(cd, ColGuard::Sanitize(cd), where);
}
static int HookOf_ProcessColModels(void* m1, void* cm1, void* m2, void* cm2, void* p1, void* p2, void* d, bool b)
{
    FixColModel(cm1, "ProcessColModels");
    FixColModel(cm2, "ProcessColModels");
    return ProcessColModels(m1, cm1, m2, cm2, p1, p2, d, b);
}
static void HookOf_ColCalcPlanes(void* cd)
{
    if(!cd || ColGuard::BadPtr((uintptr_t)cd)) return;
    NoteColFix(cd, ColGuard::Sanitize((uint8_t*)cd), "CalculateTrianglePlanes");
    ColCalcPlanes(cd);
}
static void HookOf_DataCalcPlanes(void* cd)
{
    if(!cd || ColGuard::BadPtr((uintptr_t)cd)) return;
    NoteColFix(cd, ColGuard::Sanitize((uint8_t*)cd), "CCollisionData::CalculateTrianglePlanes");
    DataCalcPlanes(cd);
}
static void InitColGuard(void* h)
{
    int n = 0;
    void* f1 = (void*)aml->GetSym(h, "_ZN10CCollision16ProcessColModelsERK7CMatrixR9CColModelS2_S4_P9CColPointS6_Pfb");
    void* f2 = (void*)aml->GetSym(h, "_ZN10CCollision23CalculateTrianglePlanesEP14CCollisionData");
    void* f3 = (void*)aml->GetSym(h, "_ZN14CCollisionData23CalculateTrianglePlanesEv");
    if(f1 && aml->Hook(f1, (void*)&HookOf_ProcessColModels, (void**)&ProcessColModels) && ProcessColModels) ++n;
    if(f2 && aml->Hook(f2, (void*)&HookOf_ColCalcPlanes, (void**)&ColCalcPlanes) && ColCalcPlanes) ++n;
    if(f3 && aml->Hook(f3, (void*)&HookOf_DataCalcPlanes, (void**)&DataCalcPlanes) && DataCalcPlanes) ++n;
    logger->Info("ColGuard: %s (%d/3 hooks) - collision data without vertices no longer crashes the game", n ? "on" : "off", n);
}

// ---- 3.21: LOCATE_CHAR_*_OBJECT guard ---------------------------------------------------------
// Crash log (realme RE6054, Android 16): SIGSEGV in LocateCharObjectCommand+31 <-
// ProcessCommands1100To1199 <- a CLEO script. Opcodes 0471..0476 (LOCATE_CHAR_ANY_MEANS /
// ON_FOOT / IN_CAR _OBJECT_2D / _3D) were run with a char or object that no longer exists. Now:
// a dead char or object -> the opcode is skipped, its condition is FALSE, the script carries on.
#ifdef AML32
  #define OFS_SCRIPT_BASEIP 16
  #define OFS_SCRIPT_IP     20
#else
  #define OFS_SCRIPT_BASEIP 24
  #define OFS_SCRIPT_IP     32
#endif
static void* (*s_GetPed)(int) = nullptr;
static void  (*s_UpdateCompareFlag)(void*, uint8_t) = nullptr;
static int   s_nLocateSkipped = 0;
static bool  s_bIpOk = true;
static uintptr_t LocGetIp(void* s)            { return (uintptr_t)*(uint8_t**)((uint8_t*)s + OFS_SCRIPT_IP); }
static void      LocSetIp(void* s, uintptr_t v) { *(uint8_t**)((uint8_t*)s + OFS_SCRIPT_IP) = (uint8_t*)v; }
static uintptr_t LocGetBase(void* s)          { return (uintptr_t)*(uint8_t**)((uint8_t*)s + OFS_SCRIPT_BASEIP); }
static ScriptGuard::LocateEnv s_Loc;

// true = handled (skipped), the game's code must not run
static bool LocateGuard(void* script, int opcode)
{
    int ch, obj;
    if(!ScriptGuard::LocateShouldSkip(s_Loc, script, opcode, s_bIpOk, ch, obj)) return false;
    s_Env.skipParams(script, ScriptGuard::LocateParams(opcode));
    if(s_UpdateCompareFlag) s_UpdateCompareFlag(script, 0);
    if(++s_nLocateSkipped <= 5 || s_nLocateSkipped % 1000 == 0)
    {
        char name[9] = {0}; memcpy(name, (const uint8_t*)script + OFS_SCRIPT_NAME, 8);
        logger->Info("ScriptGuard: script '%s' opcode %04X with a dead char (%d) / object (%d) skipped - this would have crashed the game (#%d)", name, opcode, ch, obj, s_nLocateSkipped);
    }
    return true;
}

// Preferred: the crashing function itself (every caller - the game's opcode table, CLEO, other mods).
static void (*LocateCharObjectCommand)(void*, int) = nullptr;
static void HookOf_LocateCharObjectCommand(void* script, int opcode)
{
    if(!script || (uintptr_t)script < 0x10000) return;   // log: R0 = 0 at the crash - nothing to run on
    if(LocateGuard(script, opcode)) return;
    LocateCharObjectCommand(script, opcode);
}
// Fallback when that symbol isn't exported: the opcode range that calls it.
static int8_t (*ProcessCommands1100To1199)(void*, int) = nullptr;
static int8_t HookOf_ProcessCommands1100To1199(void* script, int opcode)
{
    if(ScriptGuard::IsLocateObject(opcode) && script && LocateGuard(script, opcode)) return 0;
    return ProcessCommands1100To1199(script, opcode);
}
static void InitLocateGuard(void* h)
{
    SET_TO(s_GetPed, aml->GetSym(h, "_ZN6CPools6GetPedEi"));
    SET_TO(s_UpdateCompareFlag, aml->GetSym(h, "_ZN14CRunningScript17UpdateCompareFlagEh"));
    if(!s_UpdateCompareFlag) SET_TO(s_UpdateCompareFlag, aml->GetSym(h, "_ZN14CRunningScript17UpdateCompareFlagEb"));
    if(!s_GetPed || !s_Env.peekParam || !s_Env.skipParams || !s_Env.getObject)
    { logger->Info("ScriptGuard: locate guard off (game functions not found)"); return; }
    s_Loc.e = s_Env; s_Loc.getPed = s_GetPed; s_Loc.getIp = LocGetIp; s_Loc.setIp = LocSetIp; s_Loc.getBase = LocGetBase;
    void* fn = (void*)aml->GetSym(h, "_ZN14CRunningScript23LocateCharObjectCommandEi");
    if(fn && aml->Hook(fn, (void*)&HookOf_LocateCharObjectCommand, (void**)&LocateCharObjectCommand) && LocateCharObjectCommand)
    { logger->Info("ScriptGuard: locate guard on (LocateCharObjectCommand: 0471-0476 with a dead char / object are skipped, not crashed)"); return; }
    fn = (void*)aml->GetSym(h, "_ZN14CRunningScript25ProcessCommands1100To1199Ei");
    if(fn && aml->Hook(fn, (void*)&HookOf_ProcessCommands1100To1199, (void**)&ProcessCommands1100To1199) && ProcessCommands1100To1199)
    { logger->Info("ScriptGuard: locate guard on (ProcessCommands1100To1199)"); return; }
    logger->Info("ScriptGuard: locate guard off (hook failed)");
}

// ---- 3.21: model collision guard ---------------------------------------------------------------
// Crash log (realme RE6054): SIGSEGV in CBaseModelInfo::SetColModel+5, this = NULL, while the
// streamer read a clump's collision (CStreaming::LoadAllRequestedModels). A NULL model info has
// no collision to set: skip it (the col model stays unused) instead of crashing.
static void (*SetColModel)(void*, void*, bool) = nullptr;
static int s_nColNull = 0;
static void HookOf_SetColModel(void* mi, void* col, bool owns)
{
    if(!mi || (uintptr_t)mi < 0x10000)
    {
        if(++s_nColNull <= 5) logger->Info("ColGuard: collision for a model that has no model info skipped - this would have crashed the game (#%d)", s_nColNull);
        return;
    }
    SetColModel(mi, col, owns);
}
static void InitSetColGuard(void* h)
{
    void* fn = (void*)aml->GetSym(h, "_ZN14CBaseModelInfo11SetColModelEP9CColModelb");
    bool ok = fn && aml->Hook(fn, (void*)&HookOf_SetColModel, (void**)&SetColModel) && SetColModel;
    logger->Info("ColGuard: SetColModel guard %s", ok ? "on" : "off");
}

bool ScriptGuard::Init(void* h)
{
    InitAddGuard(h);   // 2.37: independent of the opcode guard below
    InitColGuard(h);   // 3.10: CColTrianglePlane::Set crash (aml_crashlog: bike vs. a model with no vertices)
    InitSetColGuard(h);   // 3.21: CBaseModelInfo::SetColModel with no model info
    void* fn = (void*)aml->GetSym(h, "_ZN14CRunningScript25ProcessCommands1000To1099Ei");
    SET_TO(s_Env.peekParam, aml->GetSym(h, "_ZN14CRunningScript39CollectNextParameterWithoutIncreasingPCEv"));
    SET_TO(s_Env.skipParams, aml->GetSym(h, "_ZN14CRunningScript17CollectParametersEs"));
    SET_TO(s_Env.getVehicle, aml->GetSym(h, "_ZN6CPools10GetVehicleEi"));
    SET_TO(s_Env.getObject, aml->GetSym(h, "_ZN6CPools9GetObjectEi"));
    InitLocateGuard(h);   // 3.21: 0471-0476 with a dead char / object (own hook, independent of the one below)

    if(!fn || !s_Env.peekParam || !s_Env.skipParams || !s_Env.getVehicle || !s_Env.getObject)
    {
        logger->Info("ScriptGuard: off (game functions not found)");
        return false;
    }
    if(!LooksLikeKnownBuild(fn))
    {
        logger->Info("ScriptGuard: off (ProcessCommands1000To1099 is not the GTA SA 2.00 / 2.10 code, or already hooked)");
        return false;
    }
    if(!aml->Hook(fn, (void*)&HookOf_ProcessCommands1000To1099, (void**)&ProcessCommands1000To1099) || !ProcessCommands1000To1099)
    {
        logger->Info("ScriptGuard: off (hook failed)");
        return false;
    }
    logger->Info("ScriptGuard: on (0400 / 0407 / 0432 with a dead car or object handle are skipped, not crashed)");
    return true;
}
