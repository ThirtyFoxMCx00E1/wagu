// 2.18: NPC sync, game side.
//  - list the people / traffic the game made around a point (ped + vehicle pools, plain reads)
//  - switch this phone's own people / traffic off and back on (density multipliers)
//  - remove the ones already there, create copies of another phone's NPCs
// Offsets: offsetof() on aml-psdk (CPed / CVehicle / CEntity) for both ABIs, see ofs/n.cpp.
#include "game.h"
#include "scripting.h"
#include <mod/amlmod.h>
#include <string.h>
#include <math.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>

typedef uint8_t u8;

#ifdef __aarch64__
  #define N_MATRIX      24
  #define N_MOVESPEED   104
  #define N_MODEL       50
  #define N_P_CREATEDBY 1352
  #define N_P_HEALTH    1708
  #define N_P_VEHICLE   1800
  #define N_V_CREATEDBY 1552
  #define N_V_DRIVER    1440
  #define N_V_HEALTH    1588
  #define N_V_COL1      1396
#else
  #define N_MATRIX      20
  #define N_MOVESPEED   72
  #define N_MODEL       38
  #define N_P_CREATEDBY 1096
  #define N_P_HEALTH    1348
  #define N_P_VEHICLE   1424
  #define N_V_CREATEDBY 1192
  #define N_V_DRIVER    1124
  #define N_V_HEALTH    1228
  #define N_V_COL1      1080
#endif
#define N_MAT_POS 48

#ifndef F2U
#define F2U(f) ((uint32_t*)&(f))
#endif

DEFOPCODE(03DE, SET_PED_DENSITY_MULTIPLIER, f);
DEFOPCODE(060A, LOAD_CHAR_DECISION_MAKER, iv);      // 2.21
DEFOPCODE(060B, SET_CHAR_DECISION_MAKER, ii);
DEFOPCODE(01EB, SET_CAR_DENSITY_MULTIPLIER, f);
DEFOPCODE(0129, CREATE_CHAR_INSIDE_CAR, iiiv);
DEFOPCODE(01C2, MARK_CHAR_AS_NO_LONGER_NEEDED, i);
DEFOPCODE(01C3, MARK_CAR_AS_NO_LONGER_NEEDED, i);
DEFOPCODE(020B, EXPLODE_CAR, i);
DEFOPCODE(0224, SET_CAR_HEALTH, ii);
DEFOPCODE(0227, GET_CAR_HEALTH, iv);
DEFOPCODE(0229, CHANGE_CAR_COLOUR, iii);
DEFOPCODE(0119, IS_CAR_DEAD, i);
DEFOPCODE(01C0, STORE_WANTED_LEVEL, iv);
DEFOPCODE(0918, SET_CAR_ENGINE_ON, ib);
DEFOPCODE(051B, HAS_CHAR_BEEN_DAMAGED_BY_CAR_NPC, ii);
DEFOPCODE(051C, HAS_CAR_BEEN_DAMAGED_BY_CHAR_NPC, ii);
DEFOPCODE(051D, HAS_CAR_BEEN_DAMAGED_BY_CAR_NPC, ii);
DEFOPCODE(054F, CLEAR_CAR_LAST_DAMAGE_ENTITY_NPC, i);
DEFOPCODE(0587, SET_LOAD_COLLISION_FOR_CAR_FLAG, ib);
DEFOPCODE(0588, SET_LOAD_COLLISION_FOR_CHAR_FLAG, ib);

// pool layout (aml-psdk CPool): objects*, byteMap*, size
struct PoolHdr { void* objs; u8* map; int size; };

static void** s_ppPedPool = nullptr;      // CPools::ms_pPedPool (CPool*)
static void** s_ppVehPool = nullptr;      // CPools::ms_pVehiclePool
static float* s_pPedDensity = nullptr;    // CPopulation::PedDensityMultiplier
static float* s_pCarDensity = nullptr;    // CCarCtrl::CarDensityMultiplier
static bool   s_bInit = false;

static bool SafeRead(const void* addr, void* out, size_t len)
{
    if(!addr) return false;
    struct iovec local = { out, len }, remote = { (void*)addr, len };
    return syscall(__NR_process_vm_readv, (long)getpid(), &local, 1L, &remote, 1L, 0L) == (long)len;
}

void Game::InitNpcGame(void* h)
{
    s_ppPedPool   = (void**)aml->GetSym(h, "_ZN6CPools11ms_pPedPoolE");
    s_ppVehPool   = (void**)aml->GetSym(h, "_ZN6CPools15ms_pVehiclePoolE");
    s_pPedDensity = (float*)aml->GetSym(h, "_ZN11CPopulation20PedDensityMultiplierE");
    s_pCarDensity = (float*)aml->GetSym(h, "_ZN8CCarCtrl20CarDensityMultiplierE");
    s_bInit = s_ppPedPool && s_ppVehPool;
}
bool Game::NpcSyncAvailable() { return s_bInit; }

static bool ReadPool(void** pp, PoolHdr& h)
{
    void* pool = nullptr;
    if(!pp || !SafeRead(pp, &pool, sizeof(pool)) || !pool) return false;
    if(!SafeRead(pool, &h, sizeof(h)) || !h.map || h.size <= 0 || h.size > 20000) return false;
    return true;
}

static bool EntPos(void* e, Vec3& o)
{
    void* m = nullptr;
    if(!SafeRead((u8*)e + N_MATRIX, &m, sizeof(m)) || !m) return false;
    float p[3]; if(!SafeRead((u8*)m + N_MAT_POS, p, 12)) return false;
    o.x = p[0]; o.y = p[1]; o.z = p[2];
    return o.x == o.x;
}
static Vec3 EntVel(void* e)
{
    float v[3] = { 0, 0, 0 }; SafeRead((u8*)e + N_MOVESPEED, v, 12);
    Vec3 r = { v[0] * 50.0f, v[1] * 50.0f, v[2] * 50.0f };   // game: units per 1/50 s
    return r;
}
static int EntModel(void* e) { int16_t m = -1; SafeRead((u8*)e + N_MODEL, &m, 2); return m; }

// every pool slot that is in use: handle = (index << 8) | slot flag byte (CPool::GetRef)
template<class F> static void ForPool(void** pp, F fn)
{
    PoolHdr h; if(!ReadPool(pp, h)) return;
    static u8 map[20000];
    if(!SafeRead(h.map, map, (size_t)h.size)) return;
    for(int i = 0; i < h.size; ++i)
    {
        if(map[i] & 0x80) continue;                  // empty
        fn((i << 8) | map[i]);
    }
}

static float D2(const Vec3& a, const Vec3& b) { float x = a.x - b.x, y = a.y - b.y; return x * x + y * y; }

// 2.28: createdMask = which CPed::m_nCreatedBy values to list (1 << 1 = the game's people,
// 1 << 2 = script / CLEO made); skip(handle) = true for handles MP must never send (avatars, copies)
static int ListPedsImpl(Game::NpcPedInfo* out, int maxOut, const Vec3& c, float r, unsigned createdMask, bool (*skip)(int));
int Game::ListAmbientPeds(NpcPedInfo* out, int maxOut, const Vec3& c, float r, bool (*skip)(int)) { return ListPedsImpl(out, maxOut, c, r, 1u << 1, skip); }
int Game::ListSpawnedPeds(NpcPedInfo* out, int maxOut, const Vec3& c, float r, bool (*skip)(int)) { return ListPedsImpl(out, maxOut, c, r, 1u << 2, skip); }
static int ListPedsImpl(Game::NpcPedInfo* out, int maxOut, const Vec3& c, float r, unsigned createdMask, bool (*skip)(int))
{
    using namespace Game;
    if(!s_bInit || maxOut <= 0) return 0;
    int n = 0; const float r2 = r * r;
    struct Cand { int h; float d; } cand[256]; int nc = 0;
    void* local = PedPtr(LocalPed());
    ForPool(s_ppPedPool, [&](int handle)
    {
        if(nc >= 256) return;
        void* p = PedPtr(handle);
        if(!p || p == local) return;
        u8 by = 0; SafeRead((u8*)p + N_P_CREATEDBY, &by, 1);
        if(by > 7 || !(createdMask & (1u << by))) return;   // ambient: PED_GAME only (no mission / player / our copies)
        if(skip && skip(handle)) return;
        void* veh = nullptr; SafeRead((u8*)p + N_P_VEHICLE, &veh, sizeof(veh));
        if(veh && IsCharInAnyCar(handle)) return;    // drivers come with their car
        Vec3 pos; if(!EntPos(p, pos)) return;
        float d = D2(pos, c); if(d > r2) return;
        cand[nc].h = handle; cand[nc].d = d; ++nc;
    });
    // nearest first
    for(int i = 1; i < nc; ++i) for(int k = i; k > 0 && cand[k - 1].d > cand[k].d; --k) { Cand t = cand[k]; cand[k] = cand[k - 1]; cand[k - 1] = t; }
    for(int i = 0; i < nc && n < maxOut; ++i)
    {
        int handle = cand[i].h; void* p = PedPtr(handle);
        if(!p) continue;
        Game::NpcPedInfo& o = out[n];
        o.handle = handle; o.model = EntModel(p);
        if(!IsSafePedModel(o.model)) continue;
        if(!EntPos(p, o.pos)) continue;
        o.vel = EntVel(p);
        o.heading = GetCharHeading(handle);
        o.move = GetPedMoveState(handle);
        float hp = 0; SafeRead((u8*)p + N_P_HEALTH, &hp, 4);
        o.health = hp < 0 ? 0 : (hp > 255 ? 255 : (int)hp);
        o.dead = IsCharDead(handle) || hp <= 0.0f;
        o.duck = !o.dead && IsCharDucking(handle);
        o.weapon = o.dead ? 0 : GetCharWeapon(handle);
        ++n;
    }
    return n;
}

// 2.35: is there a car / person of this model within r of `at` on this phone already (not ours to skip)?
bool Game::HasLocalTwin(bool car, int model, const Vec3& at, float r, bool (*skip)(int))
{
    if(!s_bInit) return false;
    bool found = false; const float r2 = r * r;
    void* local = PedPtr(LocalPed());
    ForPool(car ? s_ppVehPool : s_ppPedPool, [&](int h)
    {
        if(found) return;
        void* e = car ? CarPtr(h) : PedPtr(h);
        if(!e || e == local || EntModel(e) != model) return;
        Vec3 p; if(!EntPos(e, p)) return;
        float dz = p.z - at.z; if(D2(p, at) > r2 || dz * dz > r2) return;
        if(skip && skip(h)) return;
        found = true;
    });
    return found;
}

// 2.28: createdMask as for peds (CVehicle::m_nCreatedBy: 1 traffic, 2 script / CLEO / MP spawned)
static int ListCarsImpl(Game::NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, unsigned createdMask, bool (*skip)(int));
int Game::ListAmbientCars(NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, bool (*skip)(int)) { return ListCarsImpl(out, maxOut, c, r, skipCar, 1u << 1, skip); }
int Game::ListSpawnedCars(NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, bool includeTraffic, bool (*skip)(int))
{ return ListCarsImpl(out, maxOut, c, r, skipCar, (1u << 2) | (includeTraffic ? (1u << 1) : 0u), skip); }
static int ListCarsImpl(Game::NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, unsigned createdMask, bool (*skip)(int))
{
    using namespace Game;
    if(!s_bInit || maxOut <= 0) return 0;
    int n = 0; const float r2 = r * r;
    struct Cand { int h; float d; } cand[128]; int nc = 0;
    void* skipPtr = skipCar > 0 ? CarPtr(skipCar) : nullptr;
    void* local = PedPtr(LocalPed());
    ForPool(s_ppVehPool, [&](int handle)
    {
        if(nc >= 128) return;
        void* v = CarPtr(handle);
        if(!v || v == skipPtr) return;
        u8 by = 0; SafeRead((u8*)v + N_V_CREATEDBY, &by, 1);
        if(by > 7 || !(createdMask & (1u << by))) return;   // ambient: RANDOM_VEHICLE only (traffic)
        if(skip && skip(handle)) return;
        void* drv = nullptr; SafeRead((u8*)v + N_V_DRIVER, &drv, sizeof(drv));
        if(drv && drv == local) return;              // the player stole it: player sync shows it
        if(drv)
        {
            u8 dby = 0; SafeRead((u8*)drv + N_P_CREATEDBY, &dby, 1);
            unsigned drvMask = (createdMask & (1u << 2)) ? ((1u << 1) | (1u << 2)) : (1u << 1);   // spawned car: a script driver is fine
            if(dby > 7 || !(drvMask & (1u << dby))) return;            // a player (avatar / local) drives it: player sync
        }
        int model = EntModel(v);
        if(model < 400 || model > 611) return;
        Vec3 pos; if(!EntPos(v, pos)) return;
        float d = D2(pos, c); if(d > r2) return;
        cand[nc].h = handle; cand[nc].d = d; ++nc;
    });
    for(int i = 1; i < nc; ++i) for(int k = i; k > 0 && cand[k - 1].d > cand[k].d; --k) { Cand t = cand[k]; cand[k] = cand[k - 1]; cand[k - 1] = t; }
    for(int i = 0; i < nc && n < maxOut; ++i)
    {
        int handle = cand[i].h; void* v = CarPtr(handle);
        if(!v) continue;
        Game::NpcCarInfo& o = out[n];
        o.handle = handle; o.model = EntModel(v);
        if(!EntPos(v, o.pos) || !GetCarRotation(handle, o.fwd, o.up)) continue;
        o.vel = EntVel(v);
        void* drv = nullptr; SafeRead((u8*)v + N_V_DRIVER, &drv, sizeof(drv));
        o.driverModel = drv ? EntModel(drv) : 0;
        if(o.driverModel && !IsSafePedModel(o.driverModel)) o.driverModel = 0;
        float hp = 0; SafeRead((u8*)v + N_V_HEALTH, &hp, 4);
        o.health = hp < 0 ? 0 : (hp > 65000 ? 65000 : (int)hp);
        u8 col[2] = { 0, 0 }; SafeRead((u8*)v + N_V_COL1, col, 2);
        o.col[0] = col[0]; o.col[1] = col[1];
        o.dead = CALLSCM(IS_CAR_DEAD, handle) != 0;
        VehState vs; memset(&vs, 0, sizeof(vs));
        o.siren = o.lights = false; o.engine = true;
        if(ReadVehState(handle, vs)) { o.siren = (vs.flags & 4) != 0; o.lights = (vs.flags & 2) != 0; o.engine = (vs.flags & 1) != 0; }
        ++n;
    }
    return n;
}

// 2.19.1: nearest car around a point (any car: traffic, parked, players'), 0 = none
int Game::FindNearestCar(const Vec3& c, float r, int skipCar)
{
    if(!s_bInit) return 0;
    int best = 0; float bd = r * r;
    void* skip = skipCar > 0 ? CarPtr(skipCar) : nullptr;
    ForPool(s_ppVehPool, [&](int handle)
    {
        void* v = CarPtr(handle);
        if(!v || v == skip) return;
        int model = EntModel(v);
        if(model < 400 || model > 611) return;
        Vec3 pos; if(!EntPos(v, pos)) return;
        float d = D2(pos, c);
        if(d < bd && CALLSCM(IS_CAR_DEAD, handle) == 0) { bd = d; best = handle; }
    });
    return best;
}

// own people / traffic off (another phone shows them) or back on
static bool  s_bPopOff = false;
static float s_fSavedPed = 1.0f, s_fSavedCar = 1.0f;
void Game::SetAmbientPopulation(bool on)
{
    if(!on)
    {
        if(!s_bPopOff)
        {
            float v;
            if(s_pPedDensity && SafeRead(s_pPedDensity, &v, 4) && v == v && v > 0.0f && v < 10.0f) s_fSavedPed = v; else s_fSavedPed = 1.0f;
            if(s_pCarDensity && SafeRead(s_pCarDensity, &v, 4) && v == v && v > 0.0f && v < 10.0f) s_fSavedCar = v; else s_fSavedCar = 1.0f;
            s_bPopOff = true;
        }
        CALLSCM(SET_PED_DENSITY_MULTIPLIER, 0.0f);
        CALLSCM(SET_CAR_DENSITY_MULTIPLIER, 0.0f);
    }
    else if(s_bPopOff)
    {
        s_bPopOff = false;
        CALLSCM(SET_PED_DENSITY_MULTIPLIER, s_fSavedPed);
        CALLSCM(SET_CAR_DENSITY_MULTIPLIER, s_fSavedCar);
    }
}
bool Game::IsAmbientPopulationOff() { return s_bPopOff; }

// remove this phone's own people / traffic around c (never mission ones, never the player's car)
// 2.34: a car I spawned / summoned (CLEO menu, vehicle spawner, cheat) is often made as a "random"
// car (m_nCreatedBy = 1) like traffic, and was deleted here every second while another player was
// near (it stayed only when I was far away from everybody). Now kept: every EMPTY car, and any car
// that first shows up close to me after my own traffic was switched off (the game makes no traffic
// then - something on this phone spawned it). Only traffic that drives in from far away is removed.
static int s_KeepCar[48], s_nKeepCar = 0;          // summoned cars: never cleared
static int s_SeenCar[64], s_nSeenCar = 0;          // ambient cars of the last call
static bool ListHas(const int* l, int n, int h) { for(int i = 0; i < n; ++i) if(l[i] == h) return true; return false; }
bool Game::IsKeptLocalCar(int car) { return car > 0 && ListHas(s_KeepCar, s_nKeepCar, car); }
void Game::ClearAmbient(const Vec3& c, float r, int keepCar, bool first)
{
    if(!s_bInit) return;
    NpcCarInfo cars[64]; int lp = LocalPed();
    int skip = (lp > 0 && IsCharInAnyCar(lp)) ? GetCarCharIsIn(lp) : 0;
    // 2.19 carjack fix: the car we're getting into / pulling someone out of is not "ambient" any more
    // (it used to vanish mid-jack and leave the player stuck at the door)
    int using_ = (lp > 0 && !skip) ? GetCarCharIsUsing(lp) : 0;
    Vec3 usingPos = { 0, 0, 0 }; bool hasUsing = using_ > 0 && GetCarPos(using_, usingPos);
    Vec3 me = c; if(lp > 0) GetCharPos(lp, me);
    { int k = 0; for(int i = 0; i < s_nKeepCar; ++i) if(CarPtr(s_KeepCar[i])) s_KeepCar[k++] = s_KeepCar[i]; s_nKeepCar = k; }
    if(skip > 0 && !IsKeptLocalCar(skip) && s_nKeepCar < 48) s_KeepCar[s_nKeepCar++] = skip;   // a car I drove: mine
    int nc = ListAmbientCars(cars, 64, c, r, skip);
    int seen[64], nSeen = 0;
    for(int i = 0; i < nc; ++i)
    {
        int h = cars[i].handle;
        if(nSeen < 64) seen[nSeen++] = h;
        if(h == keepCar || h == using_ || IsKeptLocalCar(h)) continue;
        void* v = CarPtr(h); if(!v) continue;
        void* drv = nullptr; SafeRead((u8*)v + N_V_DRIVER, &drv, sizeof(drv));
        bool summoned = !drv;                                   // empty: spawned / parked / left by someone
        if(!summoned && !first && !ListHas(s_SeenCar, s_nSeenCar, h) && D2(cars[i].pos, me) < 25.0f * 25.0f) summoned = true;   // 2.35: 60 -> 25 m   // new, right next to me
        if(summoned) { if(s_nKeepCar < 48) s_KeepCar[s_nKeepCar++] = h; continue; }
        DeleteCar(h);       // its random driver / passengers go with it
    }
    memcpy(s_SeenCar, seen, sizeof(int) * nSeen); s_nSeenCar = nSeen;
    NpcPedInfo peds[128];
    int np = ListAmbientPeds(peds, 128, c, r);
    for(int i = 0; i < np; ++i)
    {
        if(hasUsing)
        {
            float dx = peds[i].pos.x - usingPos.x, dy = peds[i].pos.y - usingPos.y, dz = peds[i].pos.z - usingPos.z;
            if(dx * dx + dy * dy + dz * dz < 64.0f) continue;   // the driver being pulled out
        }
        DeleteChar(peds[i].handle);
    }
}

bool Game::ModelReady(int model)
{
    // 2.24: NPC models come from the host's phone - only cars 400-611 and safe peds that exist here
    if(!IsKnownCarModel(model) && !IsSafePedModel(model)) return false;
    if(CALLSCM(HAS_MODEL_LOADED, model)) return true;
    CALLSCM(REQUEST_MODEL, model);
    return false;
}

int Game::CreateNpcPed(int model, const Vec3& p0, float heading)
{
    Vec3 p = p0; if(!MP_WorldPos(p) || !MP_Finite(heading)) return 0;   // 2.33
    if(!IsSafePedModel(model) || !CALLSCM(HAS_MODEL_LOADED, model)) return 0;
    uint32_t ped = 0;
    CALLSCM(CREATE_CHAR, 24 /* PEDTYPE_MISSION1 */, model, p.x, p.y, p.z - 1.0f, &ped);
    if(!ped) return 0;
    CALLSCM(SET_CHAR_DROPS_WEAPONS_WHEN_DEAD, ped, 0);
    CALLSCM(SET_CHAR_SUFFERS_CRITICAL_HITS, ped, 0);   // damage goes to the owner, who decides
    CALLSCM(SET_CHAR_KEEP_TASK, ped, 1);
    CALLSCM(SET_CHAR_HEALTH, ped, 1000);
    CALLSCM(SET_LOAD_COLLISION_FOR_CHAR_FLAG, ped, 1);
    MakePuppet((int)ped);   // 2.21: a copy never fights / shoots on its own (the owner's phone decides)
    SetCharHeading(ped, heading);
    return (int)ped;
}

int Game::CreateNpcCar(int model, int driverModel, const Vec3& p0, float heading, int* driverOut)
{
    Vec3 p = p0; if(!MP_WorldPos(p) || !MP_Finite(heading)) return 0;   // 2.33
    if(driverOut) *driverOut = 0;
    if(model < 400 || model > 611 || !CALLSCM(HAS_MODEL_LOADED, model)) return 0;
    if(driverModel && (!IsSafePedModel(driverModel) || !CALLSCM(HAS_MODEL_LOADED, driverModel))) return 0;
    uint32_t car = 0;
    CALLSCM(CREATE_CAR, model, p.x, p.y, p.z, &car);
    if(!car) return 0;
    SetCarHeading(car, heading);
    CALLSCM(SET_LOAD_COLLISION_FOR_CAR_FLAG, car, 1);
    CALLSCM(SET_CAR_HEALTH, car, 1000);
    if(driverModel)
    {
        uint32_t d = 0;
        CALLSCM(CREATE_CHAR_INSIDE_CAR, car, 24, driverModel, &d);
        if(d)
        {
            CALLSCM(SET_CHAR_KEEP_TASK, d, 1);
            CALLSCM(SET_CHAR_SUFFERS_CRITICAL_HITS, d, 0);
            CALLSCM(SET_CHAR_HEALTH, d, 1000);
            CALLSCM(SET_CHAR_DROPS_WEAPONS_WHEN_DEAD, d, 0);
            MakePuppet((int)d);   // 2.35: no own brain - it never gets out by itself
        }
        if(driverOut) *driverOut = (int)d;
    }
    CALLSCM(SET_CAR_ENGINE_ON, car, 1);
    return (int)car;
}

// did the local player (on foot or with their car) do the last damage? then clear it
bool Game::NpcDamagedByMe(int handle, bool car, int me, int myCar)
{
    bool by;
    if(car)
    {
        by = CALLSCM(HAS_CAR_BEEN_DAMAGED_BY_CHAR_NPC, handle, me) != 0 || (myCar > 0 && CALLSCM(HAS_CAR_BEEN_DAMAGED_BY_CAR_NPC, handle, myCar) != 0);
        CALLSCM(CLEAR_CAR_LAST_DAMAGE_ENTITY_NPC, handle);
    }
    else
    {
        by = HasCharBeenDamagedByChar(handle, me) || (myCar > 0 && CALLSCM(HAS_CHAR_BEEN_DAMAGED_BY_CAR_NPC, handle, myCar) != 0);
        ClearCharLastDamageEntity(handle);
    }
    return by;
}

int  Game::GetCarHealth(int car) { uint32_t v = 0; CALLSCM(GET_CAR_HEALTH, car, &v); return (int)v; }
void Game::SetCarHealth(int car, int hp) { CALLSCM(SET_CAR_HEALTH, car, hp); }
void Game::ExplodeCar(int car) { CALLSCM(EXPLODE_CAR, car); }
bool Game::IsCarDead(int car) { return car > 0 && CALLSCM(IS_CAR_DEAD, car) != 0; }
void Game::SetCarColours(int car, int c1, int c2) { CALLSCM(CHANGE_CAR_COLOUR, car, c1, c2); }
int  Game::GetWantedLevel() { uint32_t w = 0; CALLSCM(STORE_WANTED_LEVEL, 0, &w); return (int)w; }
void Game::ReleaseNpc(int handle, bool car)
{
    if(handle <= 0) return;
    if(car) CALLSCM(MARK_CAR_AS_NO_LONGER_NEEDED, handle); else CALLSCM(MARK_CHAR_AS_NO_LONGER_NEEDED, handle);
}

// the owner applies damage another player did to one of its NPCs
void Game::DamageNpc(int handle, bool car, int dmg)
{
    if(handle <= 0 || dmg <= 0) return;
    if(car)
    {
        if(!CarPtr(handle) || IsCarDead(handle)) return;
        int hp = GetCarHealth(handle) - dmg;
        SetCarHealth(handle, hp < 0 ? 0 : hp);       // at 0 the game sets it on fire / blows it up
        return;
    }
    if(!PedPtr(handle) || IsCharDead(handle)) return;
    int hp = GetCharHealth(handle) - dmg;
    if(hp <= 0) { SetCharHealth(handle, 0); TaskDie(handle); }
    else SetCharHealth(handle, hp);
}

// 2.21: "empty" decision maker (type 0, m_empty.ped): the ped ignores every event (being bumped,
// shot at, a gun pointed at it) and only does what the sync tells it. Before, other players'
// avatars and NPC copies used the default one and sometimes punched / shot the local player.
void Game::MakePuppet(int ped)
{
    static uint32_t s_Dm = 0; static bool s_Tried = false;
    if(ped <= 0 || !PedPtr(ped)) return;
    if(!s_Tried) { s_Tried = true; CALLSCM(LOAD_CHAR_DECISION_MAKER, 0, &s_Dm); }
    if(s_Dm) CALLSCM(SET_CHAR_DECISION_MAKER, ped, (int)s_Dm);
}

// 2.32: a mission ending / "clear area" / a CLEO script cleanup marks the script's peds as "no longer
// needed": CPed::m_nCreatedBy goes 2 (mission) -> 1 (game). For a player avatar that meant: own
// street-ped brain (wander, flee, walk away), the game could delete it, and the NPC sync shared it as
// a normal pedestrian. Put it back to a script ped (2) with the empty decision maker: only the player's
// sync moves it, it never moves like an NPC.
void Game::KeepPuppet(int ped)
{
    void* p = PedPtr(ped);
    if(!p) return;
    u8 by = 0;
    if(SafeRead((u8*)p + N_P_CREATEDBY, &by, 1) && by != 2)
    {
        *((volatile u8*)p + N_P_CREATEDBY) = 2;   // PED_MISSION
        MakePuppet(ped);                           // the cleanup may have reset its decision maker too
    }
}
void Game::KeepMissionCar(int car)
{
    void* v = CarPtr(car);
    if(!v) return;
    u8 by = 0;
    if(SafeRead((u8*)v + N_V_CREATEDBY, &by, 1) && by != 2) *((volatile u8*)v + N_V_CREATEDBY) = 2;   // MISSION_VEHICLE
}

// 2.21: where a name tag goes. In a car the script position is the CAR's, so the driver's and
// every passenger's tag sat on the same spot (and each phone saw them in a different place).
// The ped's own matrix is on its seat; fall back to a seat offset from the car's axes.
bool Game::GetCharTagPos(int ped, Vec3& o)
{
    void* p = PedPtr(ped);
    if(!p) return false;
    if(!IsCharInAnyCar(ped)) { if(!GetCharPos(ped, o)) return false; o.z += 1.0f; return true; }
    int car = GetCarCharIsIn(ped);
    Vec3 cp; if(car <= 0 || !GetCarPos(car, cp)) { if(!GetCharPos(ped, o)) return false; o.z += 1.2f; return true; }
    Vec3 pp;
    if(EntPos(p, pp))
    {
        float d = D2(pp, cp) + (pp.z - cp.z) * (pp.z - cp.z);
        if(d > 0.04f && d < 16.0f) { o = pp; o.z += 0.9f; return true; }
    }
    float fwd[3], up[3];
    if(!GetCarRotation(car, fwd, up)) { o = cp; o.z += 1.2f; return true; }
    float right[3] = { fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0] };
    int seat = GetDriverOfCar(car) == ped ? -1 : GetPassengerSeat(car, ped);
    bool bike = GetMaxPassengers(car) <= 1;
    float sx, sy;
    if(bike) { sx = 0.0f; sy = seat < 0 ? 0.0f : -0.55f; }
    else     { sx = (seat == -1 || seat == 1) ? -0.45f : 0.45f; sy = seat >= 1 ? -0.85f : 0.05f; }
    o.x = cp.x + right[0] * sx + fwd[0] * sy + up[0] * 1.15f;
    o.y = cp.y + right[1] * sx + fwd[1] * sy + up[1] * 1.15f;
    o.z = cp.z + right[2] * sx + fwd[2] * sy + up[2] * 1.15f;
    return true;
}
