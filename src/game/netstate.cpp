// 2.13: deeper player state (animations, smooth motion) and environment hooks
// (explosions, fires, weapon pickups). Struct offsets come from aml-psdk (CHECKSIZE-verified
// for GTA:SA Android 2.00 armv7 / 2.10 arm64) and were measured with offsetof() on both ABIs:
//
//                                       armv7   arm64
//   CPlaceable::m_matrix                   20      24
//   CEntity::m_pRwObject (RpClump*)        24      32
//   CPhysical::m_vecMoveSpeed              72     104
//   CPed::m_ePedState                    1100    1356
//   CPed::m_nMoveState                   1104    1360
//   CMatrix right / up(fwd) / at(up) / pos   0 / 16 / 32 / 48 (both)
//   CAnimBlendAssociation group/hier/blend/delta/time/speed/id/flags
//                                14/20/24/28/32/36/44/46   26/40/48/52/56/60/68/70
//   CAnimBlendHierarchy hash / blockId      0 / 12       0 / 20
//   CAnimBlendAssocGroup size, assocs, num, idOffset, groupId
//                                0x14, 4, 8, 12, 16      0x20, 8, 16, 20, 24
//   CAnimBlock: char name[16]; bool loaded @16 (0x20 bytes, both)
//
// Everything is looked up by symbol and null-checked: a missing symbol only turns the matching
// feature off. Memory we didn't allocate is read through SafeRead() first.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include "game.h"
#include "scripting.h"
#include "net/protocol.h"
#include <stdlib.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>

DEFOPCODE(020C, ADD_EXPLOSION, fffi);
DEFOPCODE(02CF, START_SCRIPT_FIRE, fffiiv);
DEFOPCODE(02D1, REMOVE_SCRIPT_FIRE, i);
DEFOPCODE(032B, CREATE_PICKUP_WITH_AMMO, iiifffv);
DEFOPCODE(0214, HAS_PICKUP_BEEN_COLLECTED, i);
DEFOPCODE(0215, REMOVE_PICKUP, i);
DEFOPCODE(04ED, REQUEST_ANIMATION, s);
DEFOPCODE(04EE, HAS_ANIMATION_LOADED, s);
DEFOPCODE(041A, GET_AMMO_IN_CHAR_WEAPON, iiv);
// 2.15 passengers
DEFOPCODE(046C, GET_DRIVER_OF_CAR, iv);
DEFOPCODE(01EA, GET_MAXIMUM_NUMBER_OF_PASSENGERS, iv);
DEFOPCODE(0432, GET_CHAR_IN_CAR_PASSENGER_SEAT, iiv);
DEFOPCODE(0431, IS_CAR_PASSENGER_SEAT_FREE, ii);
DEFOPCODE(0430, WARP_CHAR_INTO_CAR_AS_PASSENGER, iii);
DEFOPCODE(05CA, TASK_ENTER_CAR_AS_PASSENGER, iiii);
DEFOPCODE(05CB, TASK_ENTER_CAR_AS_DRIVER, iii);   // 2.35
DEFOPCODE(0622, TASK_LEAVE_CAR, ii);              // 2.35
DEFOPCODE(09DE, IS_CHAR_GETTING_IN_TO_A_CAR, i);
DEFOPCODE(0811, GET_CAR_CHAR_IS_USING, iv);
DEFOPCODE(00DB, IS_CHAR_IN_CAR, ii);
DEFOPCODE(01C3, MARK_CAR_AS_NO_LONGER_NEEDED, i);

#ifdef AML32
  #define OFS_MATRIX     20
  #define OFS_RWOBJ      24
  #define OFS_MOVESPEED  72
  #define OFS_PEDSTATE   1100
  #define OFS_MOVESTATE  1104
  #define AS_GROUP 14
  #define AS_HIER  20
  #define AS_BLEND 24
  #define AS_DELTA 28
  #define AS_TIME  32
  #define AS_SPEED 36
  #define AS_ID    44
  #define AS_FLAGS 46
  #define AH_BLOCK 12
  #define AG_SIZE  0x14
  #define AG_ASSOC 4
  #define AG_NUM   8
  #define AG_IDOFS 12
  #define AG_ID    16
#else
  #define OFS_MATRIX     24
  #define OFS_RWOBJ      32
  #define OFS_MOVESPEED  104
  #define OFS_PEDSTATE   1356
  #define OFS_MOVESTATE  1360
  #define AS_GROUP 26
  #define AS_HIER  40
  #define AS_BLEND 48
  #define AS_DELTA 52
  #define AS_TIME  56
  #define AS_SPEED 60
  #define AS_ID    68
  #define AS_FLAGS 70
  #define AH_BLOCK 20
  #define AG_SIZE  0x20
  #define AG_ASSOC 8
  #define AG_NUM   16
  #define AG_IDOFS 20
  #define AG_ID    24
#endif
#define AH_HASH        0
#define AB_SIZE        0x20
#define AB_LOADED      16
#define MAT_RIGHT      0
#define MAT_FWD        16
#define MAT_UP         32
#define MAT_POS        48
#define NUM_GROUPS     118   // ANIM_NUM_ASSOCGROUPS
#define MAX_BLOCKS     180

// anim flags (gta-reversed names): 0x2 looped, 0x4 remove when faded out, 0x8 remove when
// finished, 0x10 partial, 0x40/0x80 extract velocity. Callbacks / block refs / "indestructible"
// are never copied from the network.
#define ANIMF_SAFE_MASK   (0x2 | 0x4 | 0x8 | 0x10 | 0x40 | 0x80)
#define ANIMF_AUTO_REMOVE 0x4

typedef uint8_t u8;
static void* (*PoolsGetPed)(int) = nullptr;
static void* (*PoolsGetVehicle)(int) = nullptr;
static void* (*ClumpFirstAssoc)(void* clump) = nullptr;
static void* (*NextAssoc)(void* assoc) = nullptr;
static void* (*BlendAnimGroup)(void* clump, int group, int id, float delta) = nullptr;
static void* (*BlendAnimHier)(void* clump, void* hier, int flags, float delta) = nullptr;
static const char* (*GetAnimBlockName)(int group) = nullptr;
static void* (*GetAnimationBlock)(const char* name) = nullptr;
static int   (*GetAnimationBlockIndex)(const char* name) = nullptr;
static void* (*GetAnimationByHash)(uint32_t hash, const void* block) = nullptr;
static void  (*AddAnimBlockRef)(int index) = nullptr;
static void  (*AssocSetCurrentTime)(void* assoc, float t) = nullptr;
static u8*   s_pAnimBlocks = nullptr;      // CAnimBlock[]
static u8*   s_pAssocGroups = nullptr;     // CAnimBlendAssocGroup[]
static bool  s_bAnimReady = false;
static bool  s_BlockRef[MAX_BLOCKS];
static uint64_t s_BlockReqTime[MAX_BLOCKS];

// explosion / fire hooks
struct Vec3Arg { float x, y, z; };   // CVector passed by value
static bool (*AddExplosion_orig)(void*, void*, int, Vec3Arg, uint32_t, u8, float, u8) = nullptr;
static void* (*StartFire_orig)(void*, Vec3Arg, float, u8, void*, uint32_t, int8_t, u8) = nullptr;
static Game::ExplosionCb s_OnExplosion = nullptr;
static Game::FireCb      s_OnFire = nullptr;
static Game::ForeignCb   s_OnForeign = nullptr;
static void* s_pLocalPed = nullptr;
static void* s_pLocalCar = nullptr;
static int   s_nApplyingRemote = 0;

static uint64_t NowMs();

// Read memory that might not be mapped (version mismatch, freed object) without crashing.
static bool SafeRead(const void* addr, void* out, size_t len)
{
    if(!addr) return false;
    struct iovec local = { out, len }, remote = { (void*)addr, len };
    return syscall(__NR_process_vm_readv, (long)getpid(), &local, 1L, &remote, 1L, 0L) == (long)len;   // API 21 safe
}
template<typename T> static inline T At(const void* base, int ofs) { T v; memcpy(&v, (const u8*)base + ofs, sizeof(T)); return v; }
template<typename T> static inline void Put(void* base, int ofs, T v) { memcpy((u8*)base + ofs, &v, sizeof(T)); }

// 3.34: no camera shake. Players saw the camera shaking (on a server, after leaving one, or with MP
// alone): explosions of other players' copies, script SHAKE_CAM calls and replayed blasts all go to
// CCamera::CamShake / CamShakeNoPos. With "No camera shake" (default on) both do nothing.
static bool s_bNoCamShake = true;
void Game::SetNoCamShake(bool on) { s_bNoCamShake = on; }
static void (*CamShake_orig)(void*, float, float, float, float) = nullptr;
static void HookCamShake(void* cam, float strength, float x, float y, float z) { if(!s_bNoCamShake) CamShake_orig(cam, strength, x, y, z); }
static void (*CamShakeNoPos_orig)(void*, float) = nullptr;
static void HookCamShakeNoPos(void* cam, float strength) { if(!s_bNoCamShake) CamShakeNoPos_orig(cam, strength); }

static bool HookExplosion(void* victim, void* creator, int type, Vec3Arg pos, uint32_t time, u8 sound, float shake, u8 vis)
{
    if(s_nApplyingRemote || s_bNoCamShake) shake = 0.0f;   // 3.34
    if(!s_nApplyingRemote)
    {
        Vec3 p = { pos.x, pos.y, pos.z };
        bool mine = s_pLocalPed && (creator == s_pLocalPed || (s_pLocalCar && (creator == s_pLocalCar || victim == s_pLocalCar)));
        if(mine) { if(s_OnExplosion) s_OnExplosion(type, p); }
        else if(s_OnForeign) s_OnForeign(p, true);   // e.g. an avatar's cosmetic rocket
    }
    return AddExplosion_orig(victim, creator, type, pos, time, sound, shake, vis);
}

static void* HookStartFire(void* self, Vec3Arg pos, float size, u8 ext, void* creator, uint32_t burn, int8_t gens, u8 unk)
{
    if(!s_nApplyingRemote)
    {
        Vec3 p = { pos.x, pos.y, pos.z };
        if(s_pLocalPed && creator == s_pLocalPed) { if(s_OnFire) s_OnFire(p, size); }
        else if(creator && s_OnForeign) s_OnForeign(p, false);
    }
    return StartFire_orig(self, pos, size, ext, creator, burn, gens, unk);
}

bool Game::InitNetState(void* h)
{
    SET_TO(PoolsGetPed,          aml->GetSym(h, "_ZN6CPools6GetPedEi"));
    SET_TO(PoolsGetVehicle,      aml->GetSym(h, "_ZN6CPools10GetVehicleEi"));
    SET_TO(ClumpFirstAssoc,      aml->GetSym(h, "_Z35RpAnimBlendClumpGetFirstAssociationP7RpClump"));
    SET_TO(NextAssoc,            aml->GetSym(h, "_Z29RpAnimBlendGetNextAssociationP21CAnimBlendAssociation"));
    SET_TO(BlendAnimGroup,       aml->GetSym(h, "_ZN12CAnimManager14BlendAnimationEP7RpClump12AssocGroupId11AnimationIdf"));
    SET_TO(BlendAnimHier,        aml->GetSym(h, "_ZN12CAnimManager14BlendAnimationEP7RpClumpP19CAnimBlendHierarchyif"));
    SET_TO(GetAnimBlockName,     aml->GetSym(h, "_ZN12CAnimManager16GetAnimBlockNameE12AssocGroupId"));
    SET_TO(GetAnimationBlock,    aml->GetSym(h, "_ZN12CAnimManager17GetAnimationBlockEPKc"));
    SET_TO(GetAnimationBlockIndex, aml->GetSym(h, "_ZN12CAnimManager22GetAnimationBlockIndexEPKc"));
    SET_TO(GetAnimationByHash,   aml->GetSym(h, "_ZN12CAnimManager12GetAnimationEjPK10CAnimBlock"));
    SET_TO(AddAnimBlockRef,      aml->GetSym(h, "_ZN12CAnimManager15AddAnimBlockRefEi"));
    SET_TO(AssocSetCurrentTime,  aml->GetSym(h, "_ZN21CAnimBlendAssociation14SetCurrentTimeEf"));

    // CAnimManager::ms_aAnimBlocks is the array itself; ms_aAnimAssocGroups is a pointer to one
    SET_TO(s_pAnimBlocks, aml->GetSym(h, "_ZN12CAnimManager14ms_aAnimBlocksE"));
    void* groupsVar = (void*)aml->GetSym(h, "_ZN12CAnimManager19ms_aAnimAssocGroupsE");
    if(groupsVar) SafeRead(groupsVar, &s_pAssocGroups, sizeof(s_pAssocGroups));

    // sanity: group 0 / 1 must say so, block 0 must be "ped"
    bool groupsOk = false;
    if(s_pAssocGroups)
    {
        int g0 = -1, g1 = -1;
        groupsOk = SafeRead(s_pAssocGroups + AG_ID, &g0, 4) && SafeRead(s_pAssocGroups + AG_SIZE + AG_ID, &g1, 4) && g0 == 0 && g1 == 1;
    }
    if(!groupsOk) s_pAssocGroups = nullptr;
    if(s_pAnimBlocks)
    {
        char n[16] = {0};
        if(!SafeRead(s_pAnimBlocks, n, 15) || strncasecmp(n, "ped", 3) != 0) s_pAnimBlocks = nullptr;
    }
    s_bAnimReady = PoolsGetPed && ClumpFirstAssoc && NextAssoc && BlendAnimGroup && GetAnimBlockName && GetAnimationBlock && s_pAssocGroups;
    logger->Info("Anim sync: %s (groups %s, blocks %s, by-hash %s)", s_bAnimReady ? "on" : "off",
                 s_pAssocGroups ? "ok" : "-", s_pAnimBlocks ? "ok" : "-", (BlendAnimHier && GetAnimationByHash) ? "ok" : "-");

    void* exp = (void*)aml->GetSym(h, "_ZN10CExplosion12AddExplosionEP7CEntityS1_14eExplosionType7CVectorjhfh");
    if(exp) aml->Hook(exp, (void*)&HookExplosion, (void**)&AddExplosion_orig);
    void* fire = (void*)aml->GetSym(h, "_ZN12CFireManager9StartFireE7CVectorfhP7CEntityjah");
    if(fire) aml->Hook(fire, (void*)&HookStartFire, (void**)&StartFire_orig);
    logger->Info("Env sync: explosions %s, fires %s", AddExplosion_orig ? "on" : "off", StartFire_orig ? "on" : "off");
    void* cs = (void*)aml->GetSym(h, "_ZN7CCamera8CamShakeEffff");      // 3.34
    if(cs) aml->Hook(cs, (void*)&HookCamShake, (void**)&CamShake_orig);
    void* cs2 = (void*)aml->GetSym(h, "_Z13CamShakeNoPosP7CCameraf");
    if(cs2) aml->Hook(cs2, (void*)&HookCamShakeNoPos, (void**)&CamShakeNoPos_orig);
    logger->Info("No camera shake: %s / %s", CamShake_orig ? "on" : "-", CamShakeNoPos_orig ? "on" : "-");
    return s_bAnimReady;
}

bool Game::AnimSyncAvailable() { return s_bAnimReady; }

// ---- entities ------------------------------------------------------------------------------
void* Game::PedPtr(int ped)  { return (PoolsGetPed && ped > 0) ? PoolsGetPed(ped) : nullptr; }
void* Game::CarPtr(int car)  { return (PoolsGetVehicle && car > 0) ? PoolsGetVehicle(car) : nullptr; }

static u8* MatrixOf(void* ent)
{
    if(!ent) return nullptr;
    return (u8*)At<void*>(ent, OFS_MATRIX);
}

int Game::GetPedMoveState(int ped) { void* p = PedPtr(ped); return p ? At<int32_t>(p, OFS_MOVESTATE) : 0; }
int Game::GetPedState(int ped)     { void* p = PedPtr(ped); return p ? At<int32_t>(p, OFS_PEDSTATE) : 0; }

bool Game::GetCarRotation(int car, float fwd[3], float up[3])
{
    u8* m = MatrixOf(CarPtr(car));
    if(!m) return false;
    memcpy(fwd, m + MAT_FWD, 12); memcpy(up, m + MAT_UP, 12);
    return true;
}

static inline void Norm(float v[3]) { float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]); if(l > 1e-5f) { v[0] /= l; v[1] /= l; v[2] /= l; } }

void Game::SetCarRotation(int car, const float fwdIn[3], const float upIn[3])
{
    u8* m = MatrixOf(CarPtr(car));
    if(!m) return;
    float f[3] = { fwdIn[0], fwdIn[1], fwdIn[2] }, u[3] = { upIn[0], upIn[1], upIn[2] };
    Norm(f); Norm(u);
    // right = fwd x up, then re-orthogonalise up = right x fwd
    float r[3] = { f[1]*u[2] - f[2]*u[1], f[2]*u[0] - f[0]*u[2], f[0]*u[1] - f[1]*u[0] };
    Norm(r);
    float u2[3] = { r[1]*f[2] - r[2]*f[1], r[2]*f[0] - r[0]*f[2], r[0]*f[1] - r[1]*f[0] };
    if(!(r[0] == r[0]) || !(u2[0] == u2[0])) return;   // NaN guard
    memcpy(m + MAT_RIGHT, r, 12); memcpy(m + MAT_FWD, f, 12); memcpy(m + MAT_UP, u2, 12);
}

void Game::NudgeEntity(int handle, bool isCar, const Vec3& d)
{
    u8* m = MatrixOf(isCar ? CarPtr(handle) : PedPtr(handle));
    if(!m || !MP_Finite(d.x) || !MP_Finite(d.y) || !MP_Finite(d.z)) return;
    float p[3]; memcpy(p, m + MAT_POS, 12);
    Vec3 v = { p[0] + d.x, p[1] + d.y, p[2] + d.z };
    if(!MP_WorldPos(v)) return;   // 2.33: never outside the world grid
    p[0] = v.x; p[1] = v.y; p[2] = v.z;
    memcpy(m + MAT_POS, p, 12);
}

void Game::SetMoveSpeed(int handle, bool isCar, const Vec3& metresPerSec)
{
    void* e = isCar ? CarPtr(handle) : PedPtr(handle);
    if(!e || !MP_Finite(metresPerSec.x) || !MP_Finite(metresPerSec.y) || !MP_Finite(metresPerSec.z)) return;
    float v[3] = { metresPerSec.x / 50.0f, metresPerSec.y / 50.0f, metresPerSec.z / 50.0f };   // game: units per 1/50 s
    // 2.33: a wild speed from the network threw the entity out of the world in one frame (crash in
    // CPhysical::Add): max 150 m/s, and never further out past the edge of the map
    { float sp2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2], mx = 150.0f / 50.0f;
      if(sp2 > mx * mx) { float k = mx / sqrtf(sp2); v[0] *= k; v[1] *= k; v[2] *= k; } }
    { u8* m = MatrixOf(e); float p[3];
      if(m) { memcpy(p, m + MAT_POS, 12);
        if((p[0] > 2950.0f && v[0] > 0) || (p[0] < -2950.0f && v[0] < 0)) v[0] = 0;
        if((p[1] > 2950.0f && v[1] > 0) || (p[1] < -2950.0f && v[1] < 0)) v[1] = 0; } }
    memcpy((u8*)e + OFS_MOVESPEED, v, 12);
}

// ---- animations ----------------------------------------------------------------------------
static inline bool IsLocomotion(int group, int id)
{
    // walk / run / sprint / idle / start / stop of the default group and every walk style
    return id >= 0 && id <= 11 && (group == 0 || (group >= 54 && group <= 87));
}

static void* ClumpOf(int ped)
{
    void* p = Game::PedPtr(ped);
    return p ? At<void*>(p, OFS_RWOBJ) : nullptr;
}

static const u8* BlockByName(const char* name)
{
    if(!name || !name[0] || !GetAnimationBlock) return nullptr;
    return (const u8*)GetAnimationBlock(name);
}

// Ask the game to stream an IFP block in; keep a reference so it stays while we use it.
static bool EnsureBlock(const char* name)
{
    const u8* b = BlockByName(name);
    if(!b) return false;
    if(At<bool>(b, AB_LOADED))
    {
        int idx = GetAnimationBlockIndex ? GetAnimationBlockIndex(name) : -1;
        if(idx >= 0 && idx < MAX_BLOCKS && !s_BlockRef[idx] && AddAnimBlockRef) { AddAnimBlockRef(idx); s_BlockRef[idx] = true; }
        return true;
    }
    int idx = GetAnimationBlockIndex ? GetAnimationBlockIndex(name) : -1;
    uint64_t now = NowMs();
    if(idx >= 0 && idx < MAX_BLOCKS)
    {
        if(now - s_BlockReqTime[idx] < 2000) return false;
        s_BlockReqTime[idx] = now;
    }
    CALLSCM(REQUEST_ANIMATION, name);
    return false;
}

int Game::GetPedAnims(int ped, PedAnim* out, int maxOut, bool full)
{
    if(!s_bAnimReady || maxOut <= 0) return 0;
    void* clump = ClumpOf(ped);
    if(!clump) return 0;
    PedAnim cand[16]; int n = 0;
    void* a = ClumpFirstAssoc(clump);
    for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
    {
        float blend = At<float>(a, AS_BLEND);
        if(blend < (full ? 0.05f : 0.25f)) continue;
        int group = At<int16_t>(a, AS_GROUP), id = At<int16_t>(a, AS_ID);
        if(!full && IsLocomotion(group, id)) continue;   // 2.15 full: walk / run / sprint / idle too
        void* hier = At<void*>(a, AS_HIER);
        if(!hier) continue;
        PedAnim pa; memset(&pa, 0, sizeof(pa));
        pa.hash = At<uint32_t>(hier, AH_HASH);
        pa.group = (int16_t)group; pa.id = (int16_t)id;
        pa.time = At<float>(a, AS_TIME); pa.speed = At<float>(a, AS_SPEED); pa.blend = blend;
        pa.flags = (uint16_t)(At<uint16_t>(a, AS_FLAGS) & ANIMF_SAFE_MASK);
        int blockId = At<int32_t>(hier, AH_BLOCK);
        if(s_pAnimBlocks && blockId >= 0 && blockId < MAX_BLOCKS) memcpy(pa.block, s_pAnimBlocks + blockId * AB_SIZE, 15);
        else if(group >= 0 && group < NUM_GROUPS) { const char* bn = GetAnimBlockName(group); if(bn) strncpy(pa.block, bn, 15); }
        pa.block[15] = 0;
        // anim straight from its group: the receiver knows the block, don't send the name
        if(group >= 0 && group < NUM_GROUPS) { const char* bn = GetAnimBlockName(group); if(bn && !strncasecmp(bn, pa.block, 15)) pa.block[0] = 0; }
        if(n < 16) cand[n++] = pa;
    }
    // strongest first
    for(int i = 1; i < n; ++i) for(int k = i; k > 0 && cand[k].blend > cand[k - 1].blend; --k) { PedAnim t = cand[k]; cand[k] = cand[k - 1]; cand[k - 1] = t; }
    if(n > maxOut) n = maxOut;
    for(int i = 0; i < n; ++i) out[i] = cand[i];
    return n;
}

// Is this (group, id) safe to ask the anim manager for? (block streamed in, id inside the group)
static bool GroupAnimReady(int group, int id)
{
    if(group < 0 || group >= NUM_GROUPS || (group >= 2 && group <= 10) || group >= 88) return false;   // no bike / car anims on foot
    const char* bn = GetAnimBlockName(group);
    if(!bn || !EnsureBlock(bn)) return false;
    const u8* g = s_pAssocGroups + group * AG_SIZE;
    void* assocs = nullptr; int num = 0, ofs = 0;
    if(!SafeRead(g + AG_ASSOC, &assocs, sizeof(assocs)) || !SafeRead(g + AG_NUM, &num, 4) || !SafeRead(g + AG_IDOFS, &ofs, 4)) return false;
    int idx = id - ofs;
    return assocs && idx >= 0 && idx < num;
}

static void* FindAssoc(void* clump, int group, int id, void* hier)
{
    void* a = ClumpFirstAssoc(clump);
    for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
    {
        if(hier) { if(At<void*>(a, AS_HIER) == hier) return a; }
        else if(At<int16_t>(a, AS_GROUP) == group && At<int16_t>(a, AS_ID) == id) return a;
    }
    return nullptr;
}

static void EnsureBasePose(void* clump, bool moving);
void Game::ApplyPedAnims(int ped, const PedAnim* in, int n, AppliedAnims& st, bool moving)
{
    if(!s_bAnimReady) return;
    void* clump = ClumpOf(ped);
    if(!clump) { st.n = 0; return; }
    AppliedAnims now; now.n = 0;
    for(int i = 0; i < n && now.n < 4; ++i)
    {
        const PedAnim& pa = in[i];
        if(IsLocomotion(pa.group, pa.id)) continue;
        bool byGroup = GroupAnimReady(pa.group, pa.id);
        void* hier = nullptr;
        if(!byGroup)
        {
            if(!BlendAnimHier || !GetAnimationByHash || !pa.block[0]) continue;
            char name[16]; memcpy(name, pa.block, 16); name[15] = 0;
            if(!EnsureBlock(name)) continue;
            const u8* blk = BlockByName(name);
            hier = blk ? GetAnimationByHash(pa.hash, blk) : nullptr;
            if(!hier) continue;
        }
        void* a = FindAssoc(clump, pa.group, pa.id, hier);
        if(!a)
        {
            a = byGroup ? BlendAnimGroup(clump, pa.group, pa.id, 6.0f)
                        : BlendAnimHier(clump, hier, (pa.flags & ANIMF_SAFE_MASK) | ANIMF_AUTO_REMOVE, 6.0f);
            if(a && AssocSetCurrentTime && pa.time > 0.0f) AssocSetCurrentTime(a, pa.time);
        }
        else
        {
            if(At<float>(a, AS_DELTA) < 0.0f) Put<float>(a, AS_DELTA, 6.0f);   // was fading out: bring it back
            float t = At<float>(a, AS_TIME);
            if(AssocSetCurrentTime && fabsf(t - pa.time) > 0.35f) AssocSetCurrentTime(a, pa.time);
        }
        if(a && pa.speed > 0.05f && pa.speed < 5.0f) Put<float>(a, AS_SPEED, pa.speed);
        now.group[now.n] = byGroup ? pa.group : -1; now.id[now.n] = pa.id; now.hier[now.n] = hier; ++now.n;
    }
    // anything we started earlier that the player isn't doing any more: fade it out
    for(int i = 0; i < st.n; ++i)
    {
        bool still = false;
        for(int k = 0; k < now.n; ++k)
            if(now.hier[k] == st.hier[i] && now.group[k] == st.group[i] && now.id[k] == st.id[i]) { still = true; break; }
        if(still) continue;
        void* a = FindAssoc(clump, st.group[i], st.id[i], st.hier[i]);
        if(a)
        {
            Put<float>(a, AS_DELTA, -6.0f);
            Put<uint16_t>(a, AS_FLAGS, (uint16_t)(At<uint16_t>(a, AS_FLAGS) | ANIMF_AUTO_REMOVE));
        }
    }
    st = now;
    // 2.15 T-pose fix: starting a full-body anim fades the walk / idle out, and the game doesn't
    // put it back by itself. If nothing solid is left, blend the base pose back in.
    EnsureBasePose(clump, moving);
}

// Blend weight of everything full-body that isn't fading out.
static float SolidBlend(void* clump)
{
    float sum = 0;
    void* a = ClumpFirstAssoc(clump);
    for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
    {
        if(At<uint16_t>(a, AS_FLAGS) & 0x10) continue;   // partial (upper body etc.)
        if(At<float>(a, AS_DELTA) < 0.0f) continue;
        sum += At<float>(a, AS_BLEND);
    }
    return sum;
}

static void EnsureBasePose(void* clump, bool moving)
{
    if(!clump || !BlendAnimGroup || SolidBlend(clump) > 0.3f) return;
    // group 0 = default ped anims (always loaded): 0 walk, 3 idle
    BlendAnimGroup(clump, 0, moving ? 0 : 3, 4.0f);
}

// 2.15: copy the sender's whole blend tree (walk/run/sprint/sneak/idle/fight/jump/... with
// their weights, times and speeds) onto the avatar. Anything else on the avatar fades out.
// Blocks still streaming in: keep what's there (never a T-pose).
//
// 2.17 slow-walk / T-pose fix:
//  - The game blends the bind pose (T-pose) into whatever weight is missing below 100 %. A slow
//    walk on the phone's stick is e.g. walk 35 % + idle 65 %; when one of those couldn't be
//    played here (block still loading, other skin's idle) the avatar showed 65 % T-pose. The
//    full-body weights we keep are now scaled back up to 100 %.
//  - If the avatar really moves (moveSpeed, from the drawn position) but the pose says "standing"
//    (late join, lag, a tiny push on the stick), the idle becomes a real walk at a matching
//    cadence: the feet always walk - never run, never slide like a snail.
static bool IsMoveGroupAnim(const Game::PedAnim& a, int idLo, int idHi)
{
    return a.group >= 0 && a.group < NUM_GROUPS && a.id >= idLo && a.id <= idHi && !(a.flags & 0x10);
}

void Game::MirrorPedAnims(int ped, const PedAnim* inRaw, int nRaw, AppliedAnims& st, float dt, float moveSpeed, float stick, int gait)
{
    if(!s_bAnimReady) return;
    void* clump = ClumpOf(ped);
    if(!clump) { st.n = 0; return; }
    PedAnim in[MP_MIRROR_MAX + 1]; int n = 0;
    for(int i = 0; i < nRaw && n < MP_MIRROR_MAX; ++i) in[n++] = inRaw[i];
    // 2.19 joystick tracking: the sender's stick (0..1) says "the legs are moving" even when the
    // drawn speed is still ~0 (start, turning on the spot, sharp left / right while running)
    const bool pushed = stick > 0.22f;
    const bool moving = moveSpeed >= 0.12f || pushed;
    if(moving)
    {
        float loco = 0, other = 0; int moveGroup = -1;
        for(int i = 0; i < n; ++i)
        {
            if(in[i].flags & 0x10) continue;                       // partial: arms only
            if(IsMoveGroupAnim(in[i], 0, 2)) { loco += in[i].blend; if(moveGroup < 0) moveGroup = in[i].group; }
            else if(IsMoveGroupAnim(in[i], 3, 3)) { if(moveGroup < 0) moveGroup = in[i].group; }
            else other += in[i].blend;
        }
        // which gait the legs need: sprint / run / walk (stick deflection + sender's move state + speed)
        int fid = 0;
        if(gait >= 7 || moveSpeed > 5.5f) fid = 2;
        else if(gait == 6 || gait == 5 || moveSpeed > 2.6f || (pushed && stick > 0.62f && moveSpeed > 1.2f)) fid = 1;
        if((loco < 0.6f && other < 0.4f) || (pushed && loco + other < 0.85f))
        {
            int g = (moveGroup >= 0 && GroupAnimReady(moveGroup, fid)) ? moveGroup : 0;
            if(!GroupAnimReady(g, fid)) fid = 0;
            int m = 0;
            for(int i = 0; i < n; ++i) if(!IsMoveGroupAnim(in[i], 3, 3)) in[m++] = in[i];   // idle out
            n = m;
            int w = -1; float others = 0;
            for(int i = 0; i < n; ++i)
            {
                if(IsMoveGroupAnim(in[i], fid, fid) && in[i].group == g) w = i;
                else if(IsMoveGroupAnim(in[i], 0, 2)) others += in[i].blend;
            }
            float need = 1.0f - other - others; if(need < 0.3f) need = 0.3f;
            float base = fid == 2 ? 7.0f : (fid == 1 ? 4.2f : 1.45f);   // anim speed at 1.0 (m/s)
            float sp = moveSpeed > 0.2f ? moveSpeed : (pushed ? base * (fid ? 0.9f : stick) : base * 0.6f);
            float spd = sp / base;
            if(spd < 0.45f) spd = 0.45f; if(spd > 1.2f) spd = 1.2f;
            if(w < 0 && n < MP_MIRROR_MAX + 1)
            {
                PedAnim wa; memset(&wa, 0, sizeof(wa));
                wa.group = (int16_t)g; wa.id = (int16_t)fid; wa.blend = need; wa.speed = spd; wa.time = -1.0f;   // -1: keep our own cycle
                wa.flags = 0x2;                                     // looped
                in[n++] = wa;
            }
            else if(w >= 0)
            {
                if(in[w].blend < need) in[w].blend = need;
                if(in[w].speed < spd) in[w].speed = spd;
            }
        }
    }
    void* keep[MP_MIRROR_MAX + 1]; const PedAnim* src[MP_MIRROR_MAX + 1]; int nk = 0;
    float solid = 0; int missing = 0;
    float k = dt * 20.0f; if(k > 1.0f) k = 1.0f;
    for(int i = 0; i < n && nk < MP_MIRROR_MAX + 1; ++i)
    {
        const PedAnim& pa = in[i];
        if(pa.blend < 0.02f) continue;
        bool byGroup = pa.group >= 0 && GroupAnimReady(pa.group, pa.id);
        void* hier = nullptr;
        if(!byGroup)
        {
            if(pa.group >= 0) { ++missing; continue; }   // group block still loading
            if(!BlendAnimHier || !GetAnimationByHash || !pa.block[0]) { ++missing; continue; }
            char name[16]; memcpy(name, pa.block, 16); name[15] = 0;
            if(!EnsureBlock(name)) { ++missing; continue; }
            const u8* blk = BlockByName(name);
            hier = blk ? GetAnimationByHash(pa.hash, blk) : nullptr;
            if(!hier) { ++missing; continue; }
        }
        void* a = FindAssoc(clump, byGroup ? pa.group : -9999, pa.id, hier);
        if(!a)
        {
            a = byGroup ? BlendAnimGroup(clump, pa.group, pa.id, 8.0f)
                        : BlendAnimHier(clump, hier, (pa.flags & ANIMF_SAFE_MASK & ~ANIMF_AUTO_REMOVE), 8.0f);
            if(!a) { ++missing; continue; }
            Put<float>(a, AS_BLEND, pa.blend * 0.6f);
            if(AssocSetCurrentTime && pa.time >= 0.0f) AssocSetCurrentTime(a, pa.time);
        }
        src[nk] = &pa;
        keep[nk++] = a;
    }
    // 2.17: what we can show has to add up to 100 % full-body, or the rest is T-pose
    float want = 0;
    for(int i = 0; i < nk; ++i) if(!(src[i]->flags & 0x10)) want += src[i]->blend;
    float scale = (want > 0.05f && want < 0.98f) ? 1.0f / want : 1.0f;
    if(scale > 5.0f) scale = 5.0f;
    // second pass: creating an anim fades the others, so set the weights only now
    for(int i = 0; i < nk; ++i)
    {
        void* a = keep[i]; const PedAnim& pa = *src[i];
        bool partial = (pa.flags & 0x10) != 0;
        float target = partial ? pa.blend : pa.blend * scale;
        if(target > 1.0f) target = 1.0f;
        float b = At<float>(a, AS_BLEND);
        b += (target - b) * k;
        Put<float>(a, AS_BLEND, b);
        Put<float>(a, AS_DELTA, 0.0f);
        uint16_t fl = At<uint16_t>(a, AS_FLAGS);
        Put<uint16_t>(a, AS_FLAGS, (uint16_t)(fl & ~(ANIMF_AUTO_REMOVE | 0x8)));   // hold the last frame instead of vanishing
        if(pa.speed >= 0.0f && pa.speed < 5.0f) Put<float>(a, AS_SPEED, pa.speed);
        if(pa.time >= 0.0f)
        {
            float t = At<float>(a, AS_TIME);
            if(AssocSetCurrentTime && fabsf(t - pa.time) > 0.15f) AssocSetCurrentTime(a, pa.time);
        }
        if(!partial) solid += target;
    }
    // fade out everything the sender isn't doing (the avatar's own AI anims too), but only
    // when we have a full-body pose to show instead
    if(solid > 0.3f)
    {
        void* a = ClumpFirstAssoc(clump);
        for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
        {
            bool mine = false;
            for(int i = 0; i < nk; ++i) if(keep[i] == a) { mine = true; break; }
            if(mine) continue;
            if(At<float>(a, AS_DELTA) > -8.0f) Put<float>(a, AS_DELTA, -8.0f);
            Put<uint16_t>(a, AS_FLAGS, (uint16_t)(At<uint16_t>(a, AS_FLAGS) | ANIMF_AUTO_REMOVE));
        }
    }
    else EnsureBasePose(clump, moving);
    // 2.19 T-pose fix (running + turning left / right): kept anims ramp up while the old ones fade,
    // so for a few frames the full-body weights added up to less than 100 % and the game filled the
    // gap with the bind pose (legs straight = T-pose). Top the strongest kept anim up so the total
    // never drops below 100 %.
    {
        float total = 0; void* best = nullptr; float bestB = -1.0f;
        void* a = ClumpFirstAssoc(clump);
        for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
        {
            if(At<uint16_t>(a, AS_FLAGS) & 0x10) continue;   // partial
            float b = At<float>(a, AS_BLEND), d = At<float>(a, AS_DELTA);
            float next = b + (d < 0.0f ? d * dt : 0.0f);      // weight after this frame's fade
            if(next < 0.0f) next = 0.0f;
            total += next;
            bool mine = false;
            for(int i = 0; i < nk; ++i) if(keep[i] == a) { mine = true; break; }
            if(mine && b > bestB) { bestB = b; best = a; }
        }
        if(total < 0.995f)
        {
            if(best)
            {
                float nb = bestB + (1.0f - total); if(nb > 1.0f) nb = 1.0f;
                Put<float>(best, AS_BLEND, nb);
            }
            else if(BlendAnimGroup)
            {   // nothing of ours could be shown: walking legs (stick pushed / moving) or idle
                void* b = BlendAnimGroup(clump, 0, moving ? 0 : 3, 8.0f);
                if(b) { float nb = At<float>(b, AS_BLEND) + (1.0f - total); Put<float>(b, AS_BLEND, nb > 1.0f ? 1.0f : nb); }
            }
        }
    }
    (void)missing;
    st.n = 0;
}

// 2.19.5: mirrored anims are kept alive (no auto-remove, delta 0). When the game's own walk task
// takes the legs over, let every full-body one fade out so they don't fight the walk.
void Game::ReleaseMirroredAnims(int ped)
{
    if(!s_bAnimReady) return;
    void* clump = ClumpOf(ped);
    if(!clump) return;
    void* a = ClumpFirstAssoc(clump);
    for(int guard = 0; a && guard < 48; ++guard, a = NextAssoc(a))
    {
        if(At<uint16_t>(a, AS_FLAGS) & 0x10) continue;   // partial
        if(At<float>(a, AS_DELTA) > -4.0f) Put<float>(a, AS_DELTA, -4.0f);
        Put<uint16_t>(a, AS_FLAGS, (uint16_t)(At<uint16_t>(a, AS_FLAGS) | ANIMF_AUTO_REMOVE));
    }
}

// ---- 2.15 passengers -------------------------------------------------------------------------
// 2.28: every car query checks the car is still alive first. The crash in 2.27
// (SIGSEGV libGTASA + 0x34EB76, CRunningScript::ProcessCommands1000To1099, from
// Game::GetCharTagPos -> GetPassengerSeat) was opcode 0432 GET_CHAR_IN_CAR_PASSENGER_SEAT:
// the game reads the seat's CPed* and turns it into a handle without checking it, so asking
// about an EMPTY seat (or a car that was just deleted / despawned) crashes the whole game.
// Name tags asked every seat of every player's car every frame. Now an empty seat is never asked.
int  Game::GetDriverOfCar(int car) { if(car <= 0 || !CarPtr(car)) return 0; uint32_t d = 0; CALLSCM(GET_DRIVER_OF_CAR, car, &d); return (int)d; }
int  Game::GetMaxPassengers(int car) { if(car <= 0 || !CarPtr(car)) return 0; int n = 0; CALLSCM(GET_MAXIMUM_NUMBER_OF_PASSENGERS, car, &n); return n < 0 ? 0 : (n > 8 ? 8 : n); }
int  Game::GetPassengerSeat(int car, int ped)
{
    if(car <= 0 || ped <= 0 || !CarPtr(car) || !PedPtr(ped)) return -1;
    int n = GetMaxPassengers(car);
    for(int seat = 0; seat < n; ++seat)
    {
        if(CALLSCM(IS_CAR_PASSENGER_SEAT_FREE, car, seat) != 0) continue;   // empty seat: 0432 would crash
        uint32_t c = 0; CALLSCM(GET_CHAR_IN_CAR_PASSENGER_SEAT, car, seat, &c);
        if((int)c == ped) return seat;
    }
    return -1;
}
bool Game::IsPassengerSeatFree(int car, int seat) { return car > 0 && seat >= 0 && seat < 8 && CarPtr(car) && CALLSCM(IS_CAR_PASSENGER_SEAT_FREE, car, seat) != 0; }
int  Game::FreePassengerSeat(int car, int prefer)
{
    int n = GetMaxPassengers(car);
    if(prefer >= 0 && prefer < n && IsPassengerSeatFree(car, prefer)) return prefer;
    for(int seat = 0; seat < n; ++seat) if(IsPassengerSeatFree(car, seat)) return seat;
    return -1;
}
void Game::WarpIntoCarAsPassenger(int ped, int car, int seat) { if(ped > 0 && car > 0 && seat >= 0) CALLSCM(WARP_CHAR_INTO_CAR_AS_PASSENGER, ped, car, seat); }
void Game::TaskEnterCarAsDriver(int ped, int car, int ms) { if(ped > 0 && car > 0) CALLSCM(TASK_ENTER_CAR_AS_DRIVER, ped, car, ms); }
void Game::TaskLeaveCar(int ped, int car) { if(ped > 0 && car > 0) CALLSCM(TASK_LEAVE_CAR, ped, car); }
void Game::TaskEnterCarAsPassenger(int ped, int car, int seat) { if(ped > 0 && car > 0 && seat >= 0) CALLSCM(TASK_ENTER_CAR_AS_PASSENGER, ped, car, 8000, seat); }
bool Game::IsCharGettingInToACar(int ped) { return ped > 0 && CALLSCM(IS_CHAR_GETTING_IN_TO_A_CAR, ped) != 0; }
int  Game::GetCarCharIsUsing(int ped) { if(ped <= 0) return 0; uint32_t c = 0; CALLSCM(GET_CAR_CHAR_IS_USING, ped, &c); return (int)c; }
bool Game::IsCharInCar(int ped, int car) { return ped > 0 && car > 0 && CALLSCM(IS_CHAR_IN_CAR, ped, car) != 0; }
void Game::ReleaseCar(int car) { if(car > 0) CALLSCM(MARK_CAR_AS_NO_LONGER_NEEDED, car); }
void Game::ClearTasksNow(int ped) { if(ped > 0) CALLSCM(CLEAR_CHAR_TASKS_IMMEDIATELY, ped); }

// ---- environment -----------------------------------------------------------------------------
void Game::SetEnvCallbacks(ExplosionCb onExp, FireCb onFire, ForeignCb onForeign)
{
    s_OnExplosion = onExp; s_OnFire = onFire; s_OnForeign = onForeign;
}

void Game::SetLocalEntities(int ped, int car)
{
    s_pLocalPed = PedPtr(ped);
    s_pLocalCar = car > 0 ? CarPtr(car) : nullptr;
}

void Game::AddExplosionNet(int type, const Vec3& p0)
{
    Vec3 p = p0; if(!MP_WorldPos(p)) return;   // 2.33
    if(type < 0 || type > 12) return;
    ++s_nApplyingRemote;
    CALLSCM(ADD_EXPLOSION, p.x, p.y, p.z, type);
    --s_nApplyingRemote;
}

int Game::StartFireNet(const Vec3& p0, int size)
{
    Vec3 p = p0; if(!MP_WorldPos(p)) return 0;   // 2.33
    if(size < 1) size = 1; if(size > 4) size = 4;
    uint32_t h = 0;
    ++s_nApplyingRemote;
    CALLSCM(START_SCRIPT_FIRE, p.x, p.y, p.z, 1, size, &h);
    --s_nApplyingRemote;
    return (int)h;
}

void Game::RemoveFireNet(int fire) { if(fire > 0) CALLSCM(REMOVE_SCRIPT_FIRE, fire); }

int Game::CreateWeaponPickup(int weapon, int ammo, const Vec3& p0)
{
    Vec3 p = p0; if(!MP_WorldPos(p)) return 0;   // 2.33
    if(weapon < 1 || weapon > 46) return 0;
    uint32_t model = 0;
    CALLSCM(GET_WEAPONTYPE_MODEL, weapon, &model);
    if((int)model <= 0 || !LoadModel((int)model)) return 0;
    uint32_t h = 0;
    CALLSCM(CREATE_PICKUP_WITH_AMMO, (int)model, 3, ammo > 0 ? ammo : 1, p.x, p.y, p.z, &h);   // 3 = once
    return (int)h;
}

bool Game::PickupCollected(int pickup) { return pickup > 0 && CALLSCM(HAS_PICKUP_BEEN_COLLECTED, pickup); }
void Game::RemovePickup(int pickup)    { if(pickup > 0) CALLSCM(REMOVE_PICKUP, pickup); }

void Game::SetDropsWeapons(int ped, bool on) { if(ped > 0) CALLSCM(SET_CHAR_DROPS_WEAPONS_WHEN_DEAD, ped, on ? 1 : 0); }

int Game::GetAmmo(int ped, int weapon)
{
    uint32_t a = 0;
    CALLSCM(GET_AMMO_IN_CHAR_WEAPON, ped, weapon, &a);
    return (int)a;
}

#include <time.h>
static uint64_t NowMs()
{
    timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + ts.tv_nsec / 1000000;
}

// =============================================================================================
// 2.17: vehicle state, every-bullet shot sync, aim, interiors, map markers, mission markers
// Offsets measured with offsetof() on aml-psdk (CHECKSIZE-verified), both ABIs:
//                                         armv7   arm64
//   CVehicle  flags (bEngineOn...)         1068    1384
//             m_nPrimaryColor              1080    1396
//             m_fSteerAngle / Gas / Brake  1176/1184/1188   1536/1544/1548
//             m_fHealth                    1228    1588
//             m_nHornCounter               1316    1700
//             m_nVehicleClass / SubClass   1440/1444        1844/1848
//   CAutomobile m_damageManager            1460    1880   (CDamageManager: 24 bytes both)
//             m_wMiscComponentAngle        2176    2696   (Hydra nozzles)
//             m_fDoomVertical/Horizontal   2400/2404        2944/2948   (tank / firetruck turret)
//   CPed      flag word (bIsAimingGun = bit 4)  1156  1412
//   CCamera   m_vecAimingTargetCoors       2008    2096
//   tRadarTrace                            0x28    0x30
// =============================================================================================
#ifndef F2U
#define F2U(f) ((uint32_t*)&(f))
#endif
DEFOPCODE(03F3, GET_CAR_COLOURS, ivv);
DEFOPCODE(0229, CHANGE_CAR_COLOUR, iii);
DEFOPCODE(0A12, GET_EXTRA_CAR_COLOURS, ivv);
DEFOPCODE(0A11, SET_EXTRA_CAR_COLOURS, iii);
DEFOPCODE(0988, GET_CURRENT_VEHICLE_PAINTJOB, iv);
DEFOPCODE(06ED, GIVE_VEHICLE_PAINTJOB, ii);
// 096D GET_CURRENT_CAR_MOD: NOT used (crashes in CVehicle::GetReplacementUpgrade on some cars)
DEFOPCODE(06E9, REQUEST_VEHICLE_MOD, i);
DEFOPCODE(06EA, HAS_VEHICLE_MOD_LOADED, i);
// 06E7 ADD_VEHICLE_MOD / 06E8 REMOVE_VEHICLE_MOD: NOT used (2.18.1, see FitUpgrade)
DEFOPCODE(0227, GET_CAR_HEALTH, iv);
DEFOPCODE(0224, SET_CAR_HEALTH, ii);
DEFOPCODE(09B3, GET_CAR_DOOR_LOCK_STATUS, iv);
DEFOPCODE(020A, LOCK_CAR_DOORS, ii);
DEFOPCODE(0918, SET_CAR_ENGINE_ON, ib);
DEFOPCODE(067F, FORCE_CAR_LIGHTS, ii);
DEFOPCODE(0397, SWITCH_CAR_SIREN, ib);
DEFOPCODE(091F, GET_PLANE_UNDERCARRIAGE_POSITION, iv);
DEFOPCODE(08E6, SET_PLANE_UNDERCARRIAGE_UP, ib);
DEFOPCODE(0689, POP_CAR_DOOR, iib);
DEFOPCODE(068A, FIX_CAR_DOOR, ii);
DEFOPCODE(073C, DAMAGE_CAR_DOOR, ii);
DEFOPCODE(0697, POP_CAR_PANEL, iib);
DEFOPCODE(0698, FIX_CAR_PANEL, ii);
DEFOPCODE(0730, DAMAGE_CAR_PANEL, ii);
DEFOPCODE(04FE, BURST_CAR_TYRE, ii);
DEFOPCODE(0699, FIX_CAR_TYRE, ii);
DEFOPCODE(0A30, FIX_CAR, i);
DEFOPCODE(095F, GET_DOOR_ANGLE_RATIO, iiv);
DEFOPCODE(095E, CONTROL_CAR_DOOR, iiif);
DEFOPCODE(09E8, GET_CHAR_AREA_VISIBLE, iv);
DEFOPCODE(0840, SET_VEHICLE_AREA_VISIBLE, ii);
DEFOPCODE(077E, GET_AREA_VISIBLE, v);
DEFOPCODE(0122, IS_PLAYER_PRESSING_HORN, i);
DEFOPCODE(068D, GET_ACTIVE_CAMERA_COORDINATES, vvv);
DEFOPCODE(068E, GET_ACTIVE_CAMERA_POINT_AT, vvv);
DEFOPCODE(018A, ADD_BLIP_FOR_COORD, fffv);
DEFOPCODE(0168, CHANGE_BLIP_SCALE_217, ii);
DEFOPCODE(0165, CHANGE_BLIP_COLOUR_217, ii);
DEFOPCODE(0164, REMOVE_BLIP_217, i);

#ifdef AML32
  #define V_FLAGS    1068
  #define V_COL1     1080
  #define V_UPGR     1086   // short m_anUpgrades[15] (model ids, -1 = empty)
  #define V_STEER    1176
  #define V_GAS      1184
  #define V_BRAKE    1188
  #define V_HEALTH   1228
  #define V_HORN     1316
  #define V_CLASS    1440
  #define V_SUBCLASS 1444
  #define A_DMG      1460
  #define A_MISC     2176
  #define A_DOOMV    2400
  #define A_DOOMH    2404
  #define P_FLAGS    1156
  #define CAM_AIM    2008
  #define RT_SIZE    0x28
  #define RT_SPRITE  0x24
#else
  #define V_FLAGS    1384
  #define V_COL1     1396
  #define V_UPGR     1402
  #define V_STEER    1536
  #define V_GAS      1544
  #define V_BRAKE    1548
  #define V_HEALTH   1588
  #define V_HORN     1700
  #define V_CLASS    1844
  #define V_SUBCLASS 1848
  #define A_DMG      1880
  #define A_MISC     2696
  #define A_DOOMV    2944
  #define A_DOOMH    2948
  #define P_FLAGS    1412
  #define CAM_AIM    2096
  #define RT_SIZE    0x30
  #define RT_SPRITE  0x28
#endif
#define RT_COUNT  175     // CRadar::ms_RadarTrace[MAX_RADAR_TRACES]
#define DM_DOORS    9
#define DM_WHEELS   5
#define DM_LIGHTS  16
#define DM_PANELS  20

static bool IsAutomobileClass(void* v)
{
    int32_t c = -1, sc = -1;
    if(!SafeRead((u8*)v + V_CLASS, &c, 4) || !SafeRead((u8*)v + V_SUBCLASS, &sc, 4)) return false;
    auto aut = [](int x) { return x == 0 || x == 1 || x == 2 || x == 3 || x == 4 || x == 11; };
    return aut(c) && aut(sc);
}
static bool IsPlaneClass(void* v)
{
    int32_t c = -1, sc = -1;
    SafeRead((u8*)v + V_CLASS, &c, 4); SafeRead((u8*)v + V_SUBCLASS, &sc, 4);
    return c == 4 || sc == 4;
}

// ---- vehicle state ----------------------------------------------------------------------------
// fitted tuning parts (sorted list of model ids 1000..1193, 0 = empty); plain memory read only
static int ReadCarUpgrades(void* v, uint16_t* out)
{
    memset(out, 0, sizeof(uint16_t) * MP_VEH_MOD_SLOTS);
    int16_t up[15];
    if(!v || !SafeRead((u8*)v + V_UPGR, up, sizeof(up))) return 0;
    int n = 0;
    for(int i = 0; i < 15 && n < MP_VEH_MOD_SLOTS; ++i)
    {
        int m = up[i];
        if(m < 1000 || m > 1193) continue;
        bool dup = false; for(int k = 0; k < n; ++k) if(out[k] == m) dup = true;
        if(!dup) out[n++] = (uint16_t)m;
    }
    for(int i = 1; i < n; ++i) for(int k = i; k > 0 && out[k - 1] > out[k]; --k) { uint16_t t = out[k]; out[k] = out[k - 1]; out[k - 1] = t; }
    return n;
}
// 2.18.1: fitting / removing a tuning part. CVehicle::AddVehicleUpgrade / RemoveVehicleUpgrade
// look the part's slot up in the car's 3D model (CVehicle::GetReplacementUpgrade ->
// CClumpModelInfo::GetFrameFromId -> RwFrameForAllObjects). When that car model has no frame
// for the slot - a modded / replaced car .dff, or a part that doesn't belong on that car - the
// frame is NULL and the game crashes (SIGSEGV in RwFrameForAllObjects, R0 = 0). There's no
// game function that tells beforehand, so the call is made directly (not through the script
// engine / CLEO) inside a fault guard: if it faults, we jump back out, that part is never tried
// again on that car model, and the game keeps running.
static int  (*VehAddUpgrade_fn)(void*, int) = nullptr;
static void (*VehRemoveUpgrade_fn)(void*, int) = nullptr;
static sigjmp_buf s_UpJmp;
static volatile sig_atomic_t s_bInUpgrade = 0;
static pthread_t s_UpThread;
static struct sigaction s_OldSegv, s_OldBus;
static uint8_t s_BadUpgrade[(212 * 194 + 7) / 8];   // (car model 400..611) x (part 1000..1193)
static int  s_nUpgradeFaults = 0;
static bool IsBadUpgrade(int model, int part)
{
    if(model < 400 || model > 611 || part < 1000 || part > 1193) return true;
    int b = (model - 400) * 194 + (part - 1000); return (s_BadUpgrade[b >> 3] >> (b & 7)) & 1;
}
static void SetBadUpgrade(int model, int part)
{
    if(model < 400 || model > 611 || part < 1000 || part > 1193) return;
    int b = (model - 400) * 194 + (part - 1000); s_BadUpgrade[b >> 3] |= (uint8_t)(1 << (b & 7));
}
static void UpgradeFault(int sig, siginfo_t* si, void* uc)
{
    if(s_bInUpgrade && pthread_equal(pthread_self(), s_UpThread)) { s_bInUpgrade = 0; siglongjmp(s_UpJmp, 1); }
    // not ours: hand it to whoever was there before (AML's crash log, the system)
    struct sigaction* old = sig == SIGBUS ? &s_OldBus : &s_OldSegv;
    if(old->sa_flags & SA_SIGINFO) { if(old->sa_sigaction) { old->sa_sigaction(sig, si, uc); return; } }
    else if(old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) { old->sa_handler(sig); return; }
    signal(sig, SIG_DFL); raise(sig);
}
static bool GuardedUpgrade(void* v, int part, bool add)
{
    if(add ? !VehAddUpgrade_fn : !VehRemoveUpgrade_fn) return false;
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = UpgradeFault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_OldSegv); sigaction(SIGBUS, &sa, &s_OldBus);
    bool ok = true;
    s_UpThread = pthread_self();
    if(sigsetjmp(s_UpJmp, 1) == 0)
    {
        s_bInUpgrade = 1;
        if(add) VehAddUpgrade_fn(v, part); else VehRemoveUpgrade_fn(v, part);
        s_bInUpgrade = 0;
    }
    else ok = false;
    sigaction(SIGSEGV, &s_OldSegv, nullptr); sigaction(SIGBUS, &s_OldBus, nullptr);
    return ok;
}
static void FitUpgrade(void* v, int carModel, int part, bool add)
{
    if(IsBadUpgrade(carModel, part)) return;
    if(!GuardedUpgrade(v, part, add))
    {
        SetBadUpgrade(carModel, part);
        if(++s_nUpgradeFaults <= 20) logger->Info("MP: tuning part %d doesn't fit car model %d on this game (its 3D model has no place for it) - skipped", part, carModel);
    }
}

static bool HasMod(const uint16_t* l, uint16_t m) { for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i) if(l[i] == m) return true; return false; }

bool Game::ReadVehState(int car, VehState& s)
{
    memset(&s, 0, sizeof(s));
    void* v = CarPtr(car);
    if(!v) return false;
    uint32_t c1 = 0, c2 = 0, c3 = 0, c4 = 0, pj = (uint32_t)-1, hp = 1000, lock = 1;
    CALLSCM(GET_CAR_COLOURS, car, &c1, &c2);
    CALLSCM(GET_EXTRA_CAR_COLOURS, car, &c3, &c4);
    CALLSCM(GET_CURRENT_VEHICLE_PAINTJOB, car, &pj);
    CALLSCM(GET_CAR_HEALTH, car, &hp);
    CALLSCM(GET_CAR_DOOR_LOCK_STATUS, car, &lock);
    s.col[0] = (uint8_t)c1; s.col[1] = (uint8_t)c2; s.col[2] = (uint8_t)c3; s.col[3] = (uint8_t)c4;
    s.paintjob = ((int)pj >= 0 && (int)pj < 8) ? (int8_t)pj : (int8_t)-1;
    s.health = (uint16_t)((int)hp < 0 ? 0 : ((int)hp > 65000 ? 65000 : (int)hp));
    s.lock = (uint8_t)lock;
    // 2.17.1: tuning parts are read straight from the car's upgrade list. GET_CURRENT_CAR_MOD
    // (CVehicle::GetReplacementUpgrade) walks the car model's frames and crashed with a SIGSEGV
    // in RwFrameForAllObjects on cars that don't have the frame for that slot.
    ReadCarUpgrades(v, s.mods);
    u8 fl[8] = {0};
    if(SafeRead((u8*)v + V_FLAGS, fl, sizeof(fl)))
    {
        if(fl[0] & (1 << 4)) s.flags |= VEHF_ENGINE;
        if(fl[0] & (1 << 6)) s.flags |= VEHF_LIGHTS;
        if(fl[5] & (1 << 7)) s.flags |= VEHF_SIREN;
    }
    s.isAuto = IsAutomobileClass(v);
    if(s.isAuto)
    {
        u8 dm[24];
        if(SafeRead((u8*)v + A_DMG, dm, sizeof(dm)))
        {
            memcpy(s.wheels, dm + DM_WHEELS, 4); memcpy(s.doors, dm + DM_DOORS, 6);
            memcpy(&s.lights, dm + DM_LIGHTS, 4); memcpy(&s.panels, dm + DM_PANELS, 4);
        }
        for(int d = 0; d < 6; ++d)
        {
            float r = 0; CALLSCM(GET_DOOR_ANGLE_RATIO, car, d, F2U(r));
            if(!(r >= 0)) r = 0; if(r > 1) r = 1;
            s.doorOpen[d] = (uint8_t)(r * 255.0f + 0.5f);
        }
        if(IsPlaneClass(v))
        {
            float g = 0; CALLSCM(GET_PLANE_UNDERCARRIAGE_POSITION, car, F2U(g));
            if(g > 0.5f) s.flags |= VEHF_GEAR_UP;     // 0 = down, 1 = up
        }
    }
    s.valid = true;
    return true;
}

// Apply only what changed since the last call (state = what this car already shows).
bool Game::GetCarColours(int car, uint8_t out[4])
{
    void* v = CarPtr(car);
    return v && SafeRead((u8*)v + V_COL1, out, 4);
}

void Game::ApplyVehState(int car, const VehState& w, VehState& st)
{
    void* v = CarPtr(car);
    if(!v || !w.valid) return;
    ++s_nApplyingRemote;
    // 2.19.1 colour sync: compared with the colours the car REALLY has (not only with what we
    // set last time) - the game re-colours cars on its own (new avatar car, respawn, pay 'n'
    // spray, a mission script), and then the colours used to stay different on each phone
    uint8_t real[4] = { 0, 0, 0, 0 };
    bool haveReal = SafeRead((u8*)v + V_COL1, real, 4);
    if(!st.valid || memcmp(st.col, w.col, 2) || (haveReal && memcmp(real, w.col, 2))) CALLSCM(CHANGE_CAR_COLOUR, car, w.col[0], w.col[1]);
    if(!st.valid || memcmp(st.col + 2, w.col + 2, 2) || (haveReal && memcmp(real + 2, w.col + 2, 2))) CALLSCM(SET_EXTRA_CAR_COLOURS, car, w.col[2], w.col[3]);
    if((!st.valid || st.paintjob != w.paintjob) && w.paintjob >= 0) CALLSCM(GIVE_VEHICLE_PAINTJOB, car, (int)w.paintjob);
    // tuning parts: stream the part in, then fit it (pending ones are retried next time)
    // 2.17.1: compared with what the car really has fitted (its own upgrade list), so a part is
    // only removed if it is on the car and only added if it isn't. Only cars (not bikes, boats,
    // planes, helis, trains) can be tuned; anything else is never touched.
    int32_t vc = -1, vsc = -1;
    SafeRead((u8*)v + V_CLASS, &vc, 4); SafeRead((u8*)v + V_SUBCLASS, &vsc, 4);
    bool tunable = (vc == 0 || vc == 1 || vc == 2) && (vsc == 0 || vsc == 1 || vsc == 2);   // car, monster truck, quad
    if(tunable)
    {
        uint16_t fitted[MP_VEH_MOD_SLOTS];
        ReadCarUpgrades(v, fitted);
        int carModel = GetCarModel(car);
        for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i)
            if(fitted[i] && !HasMod(w.mods, fitted[i])) FitUpgrade(v, carModel, fitted[i], false);
        for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i)
        {
            uint16_t want = w.mods[i];
            if(!want || want < 1000 || want > 1193 || HasMod(fitted, want) || IsBadUpgrade(carModel, want)) continue;
            if(!CALLSCM(HAS_VEHICLE_MOD_LOADED, (int)want)) { CALLSCM(REQUEST_VEHICLE_MOD, (int)want); continue; }
            FitUpgrade(v, carModel, want, true);
        }
        ReadCarUpgrades(v, st.mods);
    }
    if(!st.valid || st.lock != w.lock) CALLSCM(LOCK_CAR_DOORS, car, (int)w.lock);
    if(!st.valid || ((st.flags ^ w.flags) & VEHF_ENGINE)) CALLSCM(SET_CAR_ENGINE_ON, car, (w.flags & VEHF_ENGINE) ? 1 : 0);
    if(!st.valid || ((st.flags ^ w.flags) & VEHF_LIGHTS)) CALLSCM(FORCE_CAR_LIGHTS, car, (w.flags & VEHF_LIGHTS) ? 2 : 1);
    if(!st.valid || ((st.flags ^ w.flags) & VEHF_SIREN)) CALLSCM(SWITCH_CAR_SIREN, car, (w.flags & VEHF_SIREN) ? 1 : 0);
    CALLSCM(SET_CAR_HEALTH, car, (int)w.health);
    bool isAuto = IsAutomobileClass(v);
    if(isAuto && w.isAuto)
    {
        bool allFixed = true;
        for(int d = 0; d < 6; ++d) if(w.doors[d]) allFixed = false;
        for(int i = 0; i < 4; ++i) if(w.wheels[i]) allFixed = false;
        if(w.panels || w.lights) allFixed = false;
        bool hadDamage = st.valid && (st.panels || st.lights || memcmp(st.doors, "\0\0\0\0\0\0", 6) || memcmp(st.wheels, "\0\0\0\0", 4));
        if(allFixed && hadDamage) CALLSCM(FIX_CAR, car);   // repaired (Pay 'n' Spray / cheat)
        else
        {
            for(int d = 0; d < 6; ++d)
            {
                uint8_t a = st.valid ? st.doors[d] : 0, b = w.doors[d];
                if(a == b) continue;
                if(b == 4) CALLSCM(POP_CAR_DOOR, car, d, 1);          // door missing
                else if(b == 0) CALLSCM(FIX_CAR_DOOR, car, d);
                else if(b >= 2 && a < 2) CALLSCM(DAMAGE_CAR_DOOR, car, d);
            }
            for(int pn = 0; pn < 7; ++pn)
            {
                int a = st.valid ? (int)((st.panels >> (pn * 4)) & 0xF) : 0, b = (int)((w.panels >> (pn * 4)) & 0xF);
                if(a == b) continue;
                if(b == 0) CALLSCM(FIX_CAR_PANEL, car, pn);
                else if(b >= 3) CALLSCM(POP_CAR_PANEL, car, pn, 0);
                else CALLSCM(DAMAGE_CAR_PANEL, car, pn);
            }
            for(int i = 0; i < 4; ++i)
            {
                uint8_t a = st.valid ? st.wheels[i] : 0, b = w.wheels[i];
                if(a == b) continue;
                if(b) CALLSCM(BURST_CAR_TYRE, car, i); else CALLSCM(FIX_CAR_TYRE, car, i);
            }
            Put<uint32_t>(v, A_DMG + DM_LIGHTS, w.lights);   // broken lamps: plain status bits
        }
        for(int d = 0; d < 6; ++d)
        {
            if(w.doors[d] == 4) continue;
            uint8_t a = st.valid ? st.doorOpen[d] : 0, b = w.doorOpen[d];
            if(abs((int)a - (int)b) < 8) continue;   // cars spawn closed: doors a model doesn't have are never touched
            // CONTROL_CAR_DOOR state 0 = fully open/close by ratio (angle 0..1)
            CALLSCM(CONTROL_CAR_DOOR, car, d, b > 4 ? 1 : 0, b / 255.0f);
        }
        if(IsPlaneClass(v) && (!st.valid || ((st.flags ^ w.flags) & VEHF_GEAR_UP)))
            CALLSCM(SET_PLANE_UNDERCARRIAGE_UP, car, (w.flags & VEHF_GEAR_UP) ? 1 : 0);
    }
    uint16_t keepMods[MP_VEH_MOD_SLOTS]; memcpy(keepMods, st.mods, sizeof(keepMods));
    st = w; memcpy(st.mods, keepMods, sizeof(keepMods));
    --s_nApplyingRemote;
}

// Fast-changing driver inputs: sent in every sync (steering, pedals, horn, nozzles, turret).
bool Game::ReadVehFast(int car, VehFast& f)
{
    memset(&f, 0, sizeof(f));
    void* v = CarPtr(car);
    if(!v) return false;
    f.steer = At<float>(v, V_STEER); f.gas = At<float>(v, V_GAS); f.brake = At<float>(v, V_BRAKE);
    if(!(f.steer == f.steer)) f.steer = 0; if(!(f.gas == f.gas)) f.gas = 0; if(!(f.brake == f.brake)) f.brake = 0;
    f.horn = CALLSCM(IS_PLAYER_PRESSING_HORN, 0) != 0;
    if(IsAutomobileClass(v))
    {
        f.misc = At<uint16_t>(v, A_MISC);
        f.doomV = At<float>(v, A_DOOMV); f.doomH = At<float>(v, A_DOOMH);
        if(!(f.doomV == f.doomV)) f.doomV = 0; if(!(f.doomH == f.doomH)) f.doomH = 0;
    }
    return true;
}

void Game::ApplyVehFast(int car, const VehFast& f)
{
    void* v = CarPtr(car);
    if(!v) return;
    auto clampf = [](float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); };
    Put<float>(v, V_STEER, clampf(f.steer, -1.2f, 1.2f));
    Put<float>(v, V_GAS, clampf(f.gas, -1.0f, 1.0f));
    Put<float>(v, V_BRAKE, clampf(f.brake, 0.0f, 1.0f));
    if(f.horn) { uint32_t hc = At<uint32_t>(v, V_HORN); if(hc < 6) Put<uint32_t>(v, V_HORN, 6); }
    if(IsAutomobileClass(v))
    {
        Put<uint16_t>(v, A_MISC, f.misc);
        Put<float>(v, A_DOOMV, f.doomV); Put<float>(v, A_DOOMH, f.doomH);
    }
}

// ---- interiors (EnEx) -------------------------------------------------------------------------
int  Game::GetAreaVisible() { uint32_t a = 0; CALLSCM(GET_AREA_VISIBLE, &a); return (int)a; }
int  Game::GetCharArea(int ped) { if(ped <= 0) return 0; uint32_t a = 0; CALLSCM(GET_CHAR_AREA_VISIBLE, ped, &a); return (int)a; }
void Game::SetCharArea(int ped, int area) { if(ped > 0) CALLSCM(SET_CHAR_AREA_VISIBLE, ped, area); }
void Game::SetCarArea(int car, int area) { if(car > 0) CALLSCM(SET_VEHICLE_AREA_VISIBLE, car, area); }

// ---- aim ----------------------------------------------------------------------------------------
static u8* s_pCamera = nullptr;   // TheCamera
bool Game::IsPedAimingGun(int ped)
{
    void* p = PedPtr(ped);
    uint32_t w = 0;
    return p && SafeRead((u8*)p + P_FLAGS, &w, 4) && (w & (1u << 4));
}
bool Game::GetAimPoint(Vec3& out)
{
    // 1) where the game itself says the player aims (updated while aiming a gun)
    if(s_pCamera)
    {
        float a[3];
        if(SafeRead(s_pCamera + CAM_AIM, a, 12) && a[0] == a[0] && (a[0] != 0 || a[1] != 0) &&
           fabsf(a[0]) < 6000 && fabsf(a[1]) < 6000 && fabsf(a[2]) < 3000)
        { out.x = a[0]; out.y = a[1]; out.z = a[2]; return true; }
    }
    // 2) the camera ray
    float cx = 0, cy = 0, cz = 0, px = 0, py = 0, pz = 0;
    CALLSCM(GET_ACTIVE_CAMERA_COORDINATES, F2U(cx), F2U(cy), F2U(cz));
    CALLSCM(GET_ACTIVE_CAMERA_POINT_AT, F2U(px), F2U(py), F2U(pz));
    float dx = px - cx, dy = py - cy, dz = pz - cz, l = sqrtf(dx * dx + dy * dy + dz * dz);
    if(!(l > 0.01f)) return false;
    out.x = cx + dx / l * 60.0f; out.y = cy + dy / l * 60.0f; out.z = cz + dz / l * 60.0f;
    return true;
}

// ---- shots: capture every local bullet / projectile, replay remote ones ---------------------
struct CVecPOD { float x, y, z; };
static void (*DoBulletImpact_orig)(void*, void*, void*, CVecPOD*, CVecPOD*, void*, int) = nullptr;
static bool (*AddProjectile_orig)(void*, int, CVecPOD, float, CVecPOD*, void*) = nullptr;
static bool (*FireInstantHit_fn)(void*, void*, CVecPOD*, CVecPOD*, void*, CVecPOD*, CVecPOD*, bool, bool) = nullptr;
static bool (*WeaponFire_fn)(void*, void*, CVecPOD*, CVecPOD*, void*, CVecPOD*, CVecPOD*) = nullptr;
static Game::ShotCb s_OnShot = nullptr;
static void* s_pReplayShooter = nullptr;
static uint64_t s_LastPelletMs = 0;

static bool IsShotgun(int w) { return w == 25 || w == 26 || w == 27; }

static void HookDoBulletImpact(void* self, void* owner, void* victim, CVecPOD* start, CVecPOD* end, void* colPoint, int inc)
{
    if(s_pReplayShooter && owner == s_pReplayShooter && victim && victim == s_pLocalPed)
        return;   // a replayed remote bullet: the real damage comes from the shooter (PKT_DAMAGE)
    if(!s_nApplyingRemote && s_OnShot && s_pLocalPed && owner && (owner == s_pLocalPed || (s_pLocalCar && owner == s_pLocalCar)) && start && end)
    {
        int w = 0; SafeRead(self, &w, 4);
        uint64_t now = NowMs();
        if(!IsShotgun(w) || now - s_LastPelletMs > 90)     // shotguns: one shot, the receiver makes the pellets
        {
            if(IsShotgun(w)) s_LastPelletMs = now;
            Vec3 a = { start->x, start->y, start->z }, b = { end->x, end->y, end->z };
            float hit[3];
            if(colPoint && SafeRead(colPoint, hit, 12) && hit[0] == hit[0])
            {
                float sx = hit[0] - a.x, sy = hit[1] - a.y, sz = hit[2] - a.z;
                float ex = b.x - a.x, ey = b.y - a.y, ez = b.z - a.z;
                float ds = sx * sx + sy * sy + sz * sz, de = ex * ex + ey * ey + ez * ez;
                if(ds > 0.01f && ds <= de + 1.0f) { b.x = hit[0]; b.y = hit[1]; b.z = hit[2]; }   // real impact point
            }
            s_OnShot(0, w, a, b, 0.0f);
        }
    }
    DoBulletImpact_orig(self, owner, victim, start, end, colPoint, inc);
}

static bool HookAddProjectile(void* creator, int weapon, CVecPOD pos, float force, CVecPOD* dir, void* victim)
{
    if(!s_nApplyingRemote && s_OnShot && s_pLocalPed && creator && (creator == s_pLocalPed || (s_pLocalCar && creator == s_pLocalCar)))
    {
        Vec3 a = { pos.x, pos.y, pos.z }, d = { 0, 0, 0 };
        if(dir) { d.x = dir->x; d.y = dir->y; d.z = dir->z; }
        s_OnShot(creator == s_pLocalPed ? 1 : 2, weapon, a, d, force);
    }
    return AddProjectile_orig(creator, weapon, pos, force, dir, victim);
}

void Game::SetShotCallback(ShotCb cb) { s_OnShot = cb; }
bool Game::ShotSyncAvailable() { return DoBulletImpact_orig && (FireInstantHit_fn || WeaponFire_fn); }

static int ProjectileModel(int w)
{
    switch(w) { case 16: return 342; case 17: return 343; case 18: return 344; case 19: case 20: case 35: case 36: return 345; default: return -1; }
}

void Game::ReplayBullet(int ped, int weapon, const Vec3& from, const Vec3& to)
{
    void* p = PedPtr(ped);
    // guns only: no flamethrower (needs the ped's own fx system), rockets come as projectiles
    if(!p || weapon < 22 || weapon > 38 || weapon == 35 || weapon == 36 || weapon == 37 || (!WeaponFire_fn && !FireInstantHit_fn)) return;
    // a throw-away CWeapon (0x1C / 0x20 bytes): type, state READY, ammo, no fx system
    alignas(8) u8 wbuf[64]; memset(wbuf, 0, sizeof(wbuf));
    int32_t* wi = (int32_t*)wbuf; wi[0] = weapon; wi[1] = 0; wi[2] = 50; wi[3] = 500;
    CVecPOD o = { from.x, from.y, from.z }, m = o;
    // aim a little past the impact so the ray really reaches it
    float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z, l = sqrtf(dx * dx + dy * dy + dz * dz);
    if(!(l > 0.05f)) return;
    CVecPOD t = { to.x + dx / l * 0.5f, to.y + dy / l * 0.5f, to.z + dz / l * 0.5f };
    CALLSCM(SET_CHAR_ACCURACY, ped, 100);
    ++s_nApplyingRemote; s_pReplayShooter = p;
    if(WeaponFire_fn) WeaponFire_fn(wbuf, p, &o, &m, nullptr, &t, nullptr);
    else FireInstantHit_fn(wbuf, p, &o, &m, nullptr, &t, nullptr, false, true);
    s_pReplayShooter = nullptr; --s_nApplyingRemote;
    CALLSCM(SET_CHAR_ACCURACY, ped, 0);
}

void Game::ReplayProjectile(int ped, int car, int weapon, const Vec3& pos, const Vec3& dir, float force)
{
    void* creator = car > 0 ? CarPtr(car) : PedPtr(ped);
    if(!creator || !AddProjectile_orig) return;
    int model = ProjectileModel(weapon);
    if(model < 0 || !LoadModel(model)) return;
    if(!(force == force) || force < 0) force = 0; if(force > 5) force = 5;
    CVecPOD p = { pos.x, pos.y, pos.z }, d = { dir.x, dir.y, dir.z };
    ++s_nApplyingRemote;
    AddProjectile_orig(creator, weapon, p, force, &d, nullptr);
    --s_nApplyingRemote;
}

// ---- main menu map: player markers + names --------------------------------------------------
struct CRGBA_ { uint8_t r, g, b, a; };
struct CVec2D_ { float x, y; };
static void (*DrawBlips_orig)(float) = nullptr;
static void (*TransformRealWorldPointToRadarSpace)(CVec2D_&, const CVec2D_&) = nullptr;
static void (*TransformRadarPointToScreenSpace)(CVec2D_&, const CVec2D_&) = nullptr;
static void (*ShowRadarTraceWithHeight)(float, float, uint32_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t) = nullptr;
static void (*FontSetScale)(float) = nullptr;
static void (*FontSetColor)(CRGBA_*) = nullptr;
static void (*FontSetDropColor)(CRGBA_*) = nullptr;
static void (*FontSetFontStyle)(int) = nullptr;
static void (*FontSetOrientation)(int) = nullptr;
static void (*FontSetEdge)(int8_t) = nullptr;
static void (*FontSetProportional)(bool) = nullptr;
static void (*FontSetBackground)(bool, bool) = nullptr;
static void (*FontPrintString)(float, float, uint16_t*) = nullptr;
static void (*FontRenderBuffer)() = nullptr;
static void (*FontSetCentreSize)(float) = nullptr;
static void (*FontSetWrapx)(float) = nullptr;
static u8* s_pMenu = nullptr;      // gMobileMenu
static float*   s_pRadarRange = nullptr;    // 2.19.1: CRadar::m_radarRange
static CVec2D_* s_pRadarOrigin = nullptr;   //         CRadar::vec2DRadarOrigin
static Game::MapMarkerProvider s_MapProvider = nullptr;
#ifdef AML32
  #define MM_DRAWMAP_OFS 108
#else
  #define MM_DRAWMAP_OFS 140
#endif

static Game::MenuMapLayerFn s_MenuMapLayer = nullptr;   // 3.34
static bool s_InMenuBlips = false;   // 3.35: inside the game's DrawBlips on the pause map: its radar state IS the map's
static bool MapRadarState()
{
    if(s_InMenuBlips) return TransformRealWorldPointToRadarSpace != nullptr;   // 3.35: the map's own range / origin (the +-3000 guess drew the map too small)
                                                                                // 3.39: even when CRadar::m_radarRange is not exported (the Google map was ~0.58x)
    float range = 0; CVec2D_ org = { 0, 0 };
    if(s_pRadarRange) SafeRead(s_pRadarRange, &range, 4);
    if(s_pRadarOrigin) SafeRead(s_pRadarOrigin, &org, sizeof(org));
    return s_pRadarRange && range >= 1500.0f && fabsf(org.x) < 1.0f && fabsf(org.y) < 1.0f;
}
bool Game::MenuMapWorldToScreen(float x, float y, float& sx, float& sy)
{
    if(!TransformRealWorldPointToRadarSpace || !TransformRadarPointToScreenSpace) return false;
    CVec2D_ world = { x, y }, rad, scr;
    if(MapRadarState()) TransformRealWorldPointToRadarSpace(rad, world);
    else { rad.x = world.x / 3000.0f; rad.y = world.y / 3000.0f; }
    TransformRadarPointToScreenSpace(scr, rad);
    if(!(scr.x == scr.x) || !(scr.y == scr.y)) return false;
    sx = scr.x; sy = scr.y; return true;
}
void Game::SetMenuMapLayer(MenuMapLayerFn f) { s_MenuMapLayer = f; }

static void DrawMapPlayers()
{
    if(!s_MapProvider || !TransformRealWorldPointToRadarSpace || !TransformRadarPointToScreenSpace) return;
    Game::MapMarker mk[MP_MAX_MAP_MARKERS];
    int n = s_MapProvider(mk, MP_MAX_MAP_MARKERS);
    if(n <= 0) return;
    float sw = (float)Game::ScreenWidth(); if(sw < 320) sw = 1280;
    bool canText = FontSetScale && FontSetColor && FontPrintString && FontSetOrientation && FontSetFontStyle;
    const bool mapRadarState = MapRadarState();
    // 3.34: names go through MP's own layer (fixed pixel size: the game's font grew / shrank with the map zoom)
    static Game::MapLabel labels[MP_MAX_MAP_MARKERS]; int nLabels = 0;
    for(int i = 0; i < n; ++i)
    {
        CVec2D_ world = { mk[i].x, mk[i].y }, rad, scr;
        // 2.19.1: our draw call can run while the radar still holds the small player-centred
        // HUD radar state (range ~100-300 m around you). Then the icons were placed relative
        // to you and flew away when zooming. The full map is the whole world (+-3000 m, no
        // rotation), so in that case work it out directly; the screen part (zoom / pan of the
        // map) always comes from the game, so the icons stay glued to the map.
        if(mapRadarState) TransformRealWorldPointToRadarSpace(rad, world);
        else { rad.x = world.x / 3000.0f; rad.y = world.y / 3000.0f; }
        TransformRadarPointToScreenSpace(scr, rad);
        if(!(scr.x == scr.x) || scr.x < -50 || scr.y < -50 || scr.x > sw * 2 || scr.y > sw * 2) continue;
        uint8_t r = (uint8_t)(mk[i].rgba >> 24), g = (uint8_t)(mk[i].rgba >> 16), b = (uint8_t)(mk[i].rgba >> 8);
        if(ShowRadarTraceWithHeight) ShowRadarTraceWithHeight(scr.x, scr.y, 2, r, g, b, 255, 2 /* RADAR_TRACE_NORMAL */);
        if(!mk[i].name[0]) continue;
        if(s_MenuMapLayer && nLabels < MP_MAX_MAP_MARKERS)
        {
            Game::MapLabel& L = labels[nLabels++];
            L.sx = scr.x; L.sy = scr.y; L.rgba = mk[i].rgba; memcpy(L.name, mk[i].name, sizeof(L.name)); L.name[sizeof(L.name) - 1] = 0;
            continue;
        }
        if(!canText) continue;
        uint16_t txt[MP_NAME_LEN + 1]; int k = 0;
        for(; k < MP_NAME_LEN && mk[i].name[k]; ++k) { unsigned char c = (unsigned char)mk[i].name[k]; txt[k] = (c >= 32 && c < 127) ? c : '?'; }
        txt[k] = 0;
        CRGBA_ col = { r, g, b, 255 }, drop = { 0, 0, 0, 255 };
        FontSetFontStyle(1);                          // FONT_SUBTITLES
        if(FontSetProportional) FontSetProportional(true);
        if(FontSetBackground) FontSetBackground(false, false);
        FontSetOrientation(0);                        // ALIGN_CENTER
        if(FontSetCentreSize) FontSetCentreSize(sw);
        if(FontSetWrapx) FontSetWrapx(sw * 2);
        if(FontSetEdge) FontSetEdge(1);
        if(FontSetDropColor) FontSetDropColor(&drop);
        FontSetScale(0.28f * sw / 640.0f);
        FontSetColor(&col);
        FontPrintString(scr.x, scr.y - 0.032f * sw, txt);
    }
    if(FontRenderBuffer) FontRenderBuffer();
    if(nLabels > 0 && s_MenuMapLayer) s_MenuMapLayer(1, labels, nLabels);
}

// ---- 3.00: score bar in the game's Pricedown font (top right) ------------------------------
// Drawn right after CHud::Draw with the game's own CFont (FONT_PRICEDOWN, the money / clock font),
// so it looks like the HUD. The font is the game's - nothing extra ships with MP.
static void (*HudDraw_orig)() = nullptr;
static void (*FontSetColorV)(uint32_t) = nullptr;           // CFont::SetColor(CRGBA) - CRGBA by value
static void (*FontSetDropColorV)(uint32_t) = nullptr;
static float (*FontGetStringWidth)(uint16_t*, bool, bool) = nullptr;
static Game::HudTextProvider s_HudProvider = nullptr;
static uint64_t s_nHudDrawnMs = 0;
static uint64_t HudNowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }
static inline uint32_t PackRGBA(uint32_t rgba)   // 0xRRGGBBAA -> CRGBA bytes r,g,b,a
{ return ((rgba >> 24) & 0xFF) | (((rgba >> 16) & 0xFF) << 8) | (((rgba >> 8) & 0xFF) << 16) | ((rgba & 0xFF) << 24); }

static void DrawHudText()
{
    if(!s_HudProvider || !FontPrintString || !FontSetScale || !FontSetColorV || !FontSetFontStyle || !FontSetOrientation || !FontGetStringWidth) return;
    Game::HudSeg seg[8];
    int n = s_HudProvider(seg, 8);
    if(n <= 0) return;
    float sw = (float)Game::ScreenWidth(), sh = (float)Game::ScreenHeight();
    if(sw < 320 || sh < 200) return;
    FontSetFontStyle(3);                              // FONT_PRICEDOWN
    if(FontSetProportional) FontSetProportional(true);
    if(FontSetBackground) FontSetBackground(false, false);
    FontSetOrientation(1);                            // ALIGN_LEFT: we place each piece ourselves
    if(FontSetWrapx) FontSetWrapx(sw * 2);
    if(FontSetEdge) FontSetEdge(2);                   // black outline like the money
    if(FontSetDropColorV) FontSetDropColorV(PackRGBA(0x000000FF));
    FontSetScale(0.45f * sh / 448.0f);
    uint16_t txt[8][48]; float w[8], gap = 0.012f * sw, total = 0;
    for(int i = 0; i < n; ++i)
    {
        int k = 0;
        for(; k < 47 && seg[i].text[k]; ++k) { unsigned char c = (unsigned char)seg[i].text[k]; txt[i][k] = (c >= 32 && c < 127) ? c : ' '; }
        txt[i][k] = 0;
        w[i] = FontGetStringWidth(txt[i], true, false);
        if(!(w[i] == w[i]) || w[i] < 0 || w[i] > sw) return;   // a broken width: leave it to ImGui
        total += w[i] + (i ? gap : 0);
    }
    float x = sw - 0.02f * sw - total, y = 0.008f * sh;
    for(int i = 0; i < n; ++i)
    {
        FontSetColorV(PackRGBA(seg[i].rgba));
        FontPrintString(x, y, txt[i]);
        x += w[i] + gap;
    }
    if(FontRenderBuffer) FontRenderBuffer();
    s_nHudDrawnMs = HudNowMs();
}
static void HookHudDraw() { HudDraw_orig(); DrawHudText(); }
void Game::SetHudTextProvider(HudTextProvider p) { s_HudProvider = p; }
bool Game::HudTextActive() { return s_nHudDrawnMs && HudNowMs() - s_nHudDrawnMs < 300; }

static uint64_t s_LastBlipsMapDraw = 0;
static bool OnMenuMap()
{
    u8 drawMap = 0;
    return s_pMenu && Game::IsPaused() && SafeRead(s_pMenu + MM_DRAWMAP_OFS, &drawMap, 1) && drawMap;
}
// drawn right after the game's own blips, so our icons sit on top
static void HookDrawBlips(float scale)
{
    const bool onMap = OnMenuMap();
    s_InMenuBlips = onMap;
    if(s_MenuMapLayer && onMap) s_MenuMapLayer(0, nullptr, 0);   // 3.34: Mega Vil's Google map under the blips
    DrawBlips_orig(scale);
    if(onMap) { DrawMapPlayers(); s_LastBlipsMapDraw = NowMs(); }
    s_InMenuBlips = false;
}
// fallback: if this game build's map screen doesn't go through DrawBlips, the gang overlay
// (which the GPS mod also uses) is drawn on the map every frame
static void (*DrawGangOverlay_orig)(bool) = nullptr;
// 3.01: SA-MP gang zones - drawn by the game itself (CRadar::DrawAreaOnRadar) on the radar and
// the pause-menu map, exactly like the SA-MP client's ZoneOverlay hook. Rect = { minX, maxY, maxX, minY }.
struct GRect { float a, b, c, d; };
struct GRgba { uint8_t r, g, b, a; };
static void (*DrawAreaOnRadar)(const GRect&, const GRgba&, bool) = nullptr;
static Game::ZoneProvider s_ZoneProvider = nullptr;
void Game::SetZoneProvider(ZoneProvider p) { s_ZoneProvider = p; }
static void DrawZones(bool inMenu)
{
    if(!s_ZoneProvider || !DrawAreaOnRadar) return;
    static Game::RadarZone z[MP_MAX_RADAR_ZONES];
    int n = s_ZoneProvider(z, MP_MAX_RADAR_ZONES);
    for(int i = 0; i < n; ++i)
    {
        GRect r = { z[i].minX, z[i].maxY, z[i].maxX, z[i].minY };
        uint32_t c = z[i].rgba;
        GRgba col = { (uint8_t)(c >> 24), (uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c };
        DrawAreaOnRadar(r, col, inMenu);
    }
}
static void HookDrawGangOverlay(bool inMenu)
{
    DrawGangOverlay_orig(inMenu);
    DrawZones(inMenu);
    if(OnMenuMap() && NowMs() - s_LastBlipsMapDraw > 500) DrawMapPlayers();
}
void Game::SetMapMarkerProvider(MapMarkerProvider p) { s_MapProvider = p; }

// ---- mission markers (mission sync) -----------------------------------------------------------
static u8* s_pRadarTrace = nullptr;
int Game::ReadMissionBlips(MissionBlip* out, int maxOut, const int* skip, int nSkip)
{
    if(!s_pRadarTrace || maxOut <= 0) return 0;
    int n = 0;
    for(int i = 0; i < RT_COUNT && n < maxOut; ++i)
    {
        u8 t[0x30];
        if(!SafeRead(s_pRadarTrace + i * RT_SIZE, t, RT_SIZE)) break;
        u8 sprite = t[RT_SPRITE], f1 = t[RT_SPRITE + 1], f2 = t[RT_SPRITE + 2];
        bool inUse = (f1 >> 1) & 1, shortRange = (f1 >> 2) & 1;
        int display = f2 & 3, type = (f2 >> 2) & 0xF;
        if(!inUse || shortRange || sprite != 0 || display == 0) continue;
        if(type < 1 || type > 4) continue;            // car / char / object / coord only
        bool skipIt = false;
        for(int k = 0; k < nSkip; ++k) if((skip[k] & 0xFFFF) == i) skipIt = true;
        if(skipIt) continue;
        uint32_t colour; memcpy(&colour, t, 4);
        int32_t handle; memcpy(&handle, t + 4, 4);
        Vec3 pos = { 0, 0, 0 };
        if(type == 4) { memcpy(&pos, t + 8, 12); }
        else if(type == 1) { if(!CarPtr(handle)) continue; GetCarPos(handle, pos); }
        else if(type == 2) { if(!PedPtr(handle)) continue; GetCharPos(handle, pos); }
        else continue;                                 // objects: skip
        if(!(pos.x == pos.x) || (pos.x == 0 && pos.y == 0)) continue;
        out[n].x = pos.x; out[n].y = pos.y; out[n].z = pos.z;
        out[n].colour = colour; out[n].display = (uint8_t)display; out[n].kind = (uint8_t)type;
        ++n;
    }
    return n;
}

int Game::AddCoordBlip(const Vec3& p, uint32_t colour, int display)
{
    uint32_t b = 0;
    CALLSCM(ADD_BLIP_FOR_COORD, p.x, p.y, p.z, &b);
    if(b)
    {
        CALLSCM(CHANGE_BLIP_COLOUR_217, b, (int)colour);
        CALLSCM(CHANGE_BLIP_DISPLAY, b, display < 1 || display > 3 ? 2 : display);
    }
    return (int)b;
}

// ---- init ---------------------------------------------------------------------------------------
void Game::InitNetState217(void* h)
{
    void* f;
    VehAddUpgrade_fn    = (int (*)(void*, int))aml->GetSym(h, "_ZN8CVehicle17AddVehicleUpgradeEi");      // 2.18.1
    VehRemoveUpgrade_fn = (void (*)(void*, int))aml->GetSym(h, "_ZN8CVehicle20RemoveVehicleUpgradeEi");
    logger->Info("Tuning sync: %s", (VehAddUpgrade_fn && VehRemoveUpgrade_fn) ? "on (guarded)" : "off (game functions not found)");
    if((f = (void*)aml->GetSym(h, "_ZN7CWeapon14DoBulletImpactEP7CEntityS1_P7CVectorS3_P9CColPointi")))
        aml->Hook(f, (void*)&HookDoBulletImpact, (void**)&DoBulletImpact_orig);
    if((f = (void*)aml->GetSym(h, "_ZN15CProjectileInfo13AddProjectileEP7CEntity11eWeaponType7CVectorfPS3_S1_")))
        aml->Hook(f, (void*)&HookAddProjectile, (void**)&AddProjectile_orig);
    SET_TO(FireInstantHit_fn, aml->GetSym(h, "_ZN7CWeapon14FireInstantHitEP7CEntityP7CVectorS3_S1_S3_S3_bb"));
    SET_TO(WeaponFire_fn,     aml->GetSym(h, "_ZN7CWeapon4FireEP7CEntityP7CVectorS3_S1_S3_S3_"));
    SET_TO(s_pCamera,         aml->GetSym(h, "TheCamera"));
    SET_TO(s_pRadarTrace,     aml->GetSym(h, "_ZN6CRadar13ms_RadarTraceE"));
    SET_TO(s_pMenu,           aml->GetSym(h, "gMobileMenu"));
    SET_TO(s_pRadarRange,     aml->GetSym(h, "_ZN6CRadar12m_radarRangeE"));
    SET_TO(s_pRadarOrigin,    aml->GetSym(h, "_ZN6CRadar16vec2DRadarOriginE"));
    SET_TO(TransformRealWorldPointToRadarSpace, aml->GetSym(h, "_ZN6CRadar35TransformRealWorldPointToRadarSpaceER9CVector2DRKS0_"));
    SET_TO(TransformRadarPointToScreenSpace,    aml->GetSym(h, "_ZN6CRadar32TransformRadarPointToScreenSpaceER9CVector2DRKS0_"));
    SET_TO(ShowRadarTraceWithHeight,            aml->GetSym(h, "_ZN6CRadar24ShowRadarTraceWithHeightEffjhhhhh"));
    SET_TO(FontSetScale,        aml->GetSym(h, "_ZN5CFont8SetScaleEf"));
    SET_TO(FontSetColor,        aml->GetSym(h, "_ZN5CFont8SetColorE5CRGBA"));
    SET_TO(FontSetDropColor,    aml->GetSym(h, "_ZN5CFont12SetDropColorE5CRGBA"));
    SET_TO(FontSetFontStyle,    aml->GetSym(h, "_ZN5CFont12SetFontStyleEh"));
    SET_TO(FontSetOrientation,  aml->GetSym(h, "_ZN5CFont14SetOrientationEh"));
    SET_TO(FontSetEdge,         aml->GetSym(h, "_ZN5CFont7SetEdgeEa"));
    SET_TO(FontSetProportional, aml->GetSym(h, "_ZN5CFont15SetProportionalEh"));
    SET_TO(FontSetBackground,   aml->GetSym(h, "_ZN5CFont13SetBackgroundEhh"));
    SET_TO(FontPrintString,     aml->GetSym(h, "_ZN5CFont11PrintStringEffPt"));
    SET_TO(FontRenderBuffer,    aml->GetSym(h, "_ZN5CFont16RenderFontBufferEv"));
    SET_TO(FontSetCentreSize,   aml->GetSym(h, "_ZN5CFont13SetCentreSizeEf"));
    SET_TO(FontSetWrapx,        aml->GetSym(h, "_ZN5CFont8SetWrapxEf"));
    SET_TO(FontSetColorV,       aml->GetSym(h, "_ZN5CFont8SetColorE5CRGBA"));      // 3.00: same function, CRGBA by value
    SET_TO(FontSetDropColorV,   aml->GetSym(h, "_ZN5CFont12SetDropColorE5CRGBA"));
    SET_TO(FontGetStringWidth,  aml->GetSym(h, "_ZN5CFont14GetStringWidthEPtbb"));
    if(!FontGetStringWidth) SET_TO(FontGetStringWidth, aml->GetSym(h, "_ZN5CFont14GetStringWidthEPth"));
    if(!FontGetStringWidth) SET_TO(FontGetStringWidth, aml->GetSym(h, "_ZN5CFont14GetStringWidthEPt"));
    f = (void*)aml->GetSym(h, "_ZN4CHud4DrawEv");
    if(!f) f = (void*)aml->GetSym(h, "_ZN4CHud14DrawAfterFadeEv");
    if(f && FontGetStringWidth && FontSetColorV) aml->Hook(f, (void*)&HookHudDraw, (void**)&HudDraw_orig);
    logger->Info("3.00: game-font score bar %s (CHud::Draw %s, GetStringWidth %s, SetColor %s, PrintString %s)", HudDraw_orig ? "on" : "off",
                 f ? "ok" : "-", FontGetStringWidth ? "ok" : "-", FontSetColorV ? "ok" : "-", FontPrintString ? "ok" : "-");
    if((f = (void*)aml->GetSym(h, "_ZN6CRadar9DrawBlipsEf")) && TransformRealWorldPointToRadarSpace && TransformRadarPointToScreenSpace)
        aml->Hook(f, (void*)&HookDrawBlips, (void**)&DrawBlips_orig);
    SET_TO(DrawAreaOnRadar, aml->GetSym(h, "_ZN6CRadar15DrawAreaOnRadarERK5CRectRK5CRGBAb"));   // 3.01: gang zones
    if((f = (void*)aml->GetSym(h, "_ZN6CRadar20DrawRadarGangOverlayEb")) && TransformRealWorldPointToRadarSpace && TransformRadarPointToScreenSpace)
        aml->Hook(f, (void*)&HookDrawGangOverlay, (void**)&DrawGangOverlay_orig);
    logger->Info("2.17: shot sync %s (fire %s), projectiles %s, map markers %s (font %s), mission markers %s, aim %s",
                 DoBulletImpact_orig ? "on" : "off", WeaponFire_fn ? "Fire" : (FireInstantHit_fn ? "FireInstantHit" : "-"),
                 AddProjectile_orig ? "on" : "off", DrawBlips_orig ? "on" : "off", FontPrintString ? "ok" : "-",
                 s_pRadarTrace ? "on" : "off", s_pCamera ? "camera" : "ray");
}
