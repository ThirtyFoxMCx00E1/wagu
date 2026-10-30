// 3.04: game helpers for Brief Case War - briefcase objects (model 1210, the game's own
// briefcase), carried briefcases attached to the carrier's hand, and no wanted level inside the
// war area (Area 69 / Verdant Meadows would give everybody stars and SAM missiles).
#include "game.h"
#include "scripting.h"
#include <string.h>
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <set>

DEFOPCODE(0247, REQUEST_MODEL_304, i);
DEFOPCODE(0248, HAS_MODEL_LOADED_304, i);
DEFOPCODE(0107, CREATE_OBJECT_304, ifffv);
DEFOPCODE(0108, DELETE_OBJECT_304, i);
DEFOPCODE(03CA, DOES_OBJECT_EXIST_304, i);
DEFOPCODE(0382, SET_OBJECT_COLLISION_304, ib);
DEFOPCODE(069B, ATTACH_OBJECT_TO_CHAR_304, iiffffff);
DEFOPCODE(01BC, SET_OBJECT_COORDINATES_304, ifff);
DEFOPCODE(0453, SET_OBJECT_ROTATION_304, ifff);
DEFOPCODE(0110, CLEAR_WANTED_LEVEL_304, i);
DEFOPCODE(01F0, SET_MAX_WANTED_LEVEL_304, i);
DEFOPCODE(0249, MARK_MODEL_AS_NO_LONGER_NEEDED_304, i);

// 3.34: every object MP makes is remembered, so a save load can free them all first. The two
// crash logs in BugReports/ (CPools::LoadObjectPool / CFileLoader::LoadObjectInstance -> CPlaceable /
// CEntity::GetBoundCentre on a null object) are the game's object + dummy pools running out while
// the save loads: MP's ATMs, drops, MW3 props, briefcases were still in the pool.
static std::set<int> s_MpObjects;
static void TrackObj(int obj) { if(obj > 0) s_MpObjects.insert(obj); }
int Game::DeleteAllMpObjects()
{
    int n = 0;
    for(int o : s_MpObjects) if(o > 0 && CALLSCM(DOES_OBJECT_EXIST_304, o)) { CALLSCM(DELETE_OBJECT_304, o); ++n; }
    s_MpObjects.clear();
    return n;
}
int Game::CreateWorldObject(int model, const Vec3& p)
{
    if(!IsReady() || model <= 0) return 0;
    { int fs = FreeObjectSlots(); if(fs >= 0 && fs < 40) return 0; }   // 3.34: keep room for the game + other mods
    if(!(p.x == p.x) || p.x < -3000.0f || p.x > 3000.0f || p.y < -3000.0f || p.y > 3000.0f) return 0;
    if(!CALLSCM(HAS_MODEL_LOADED_304, model)) { CALLSCM(REQUEST_MODEL_304, model); return 0; }   // try again next tick
    uint32_t obj = 0;
    CALLSCM(CREATE_OBJECT_304, model, p.x, p.y, p.z, &obj);
    if(!obj) return 0;
    CALLSCM(SET_OBJECT_COLLISION_304, (int)obj, 0);
    TrackObj((int)obj);
    return (int)obj;
}
void Game::DeleteWorldObject(int obj)
{
    if(obj > 0 && CALLSCM(DOES_OBJECT_EXIST_304, obj)) CALLSCM(DELETE_OBJECT_304, obj);
    s_MpObjects.erase(obj);
}
bool Game::WorldObjectExists(int obj) { return obj > 0 && CALLSCM(DOES_OBJECT_EXIST_304, obj) != 0; }
void Game::MoveWorldObject(int obj, const Vec3& p, float spinDeg)
{
    if(!WorldObjectExists(obj)) return;
    CALLSCM(SET_OBJECT_COORDINATES_304, obj, p.x, p.y, p.z);
    CALLSCM(SET_OBJECT_ROTATION_304, obj, 0.0f, 0.0f, spinDeg);
}
void Game::AttachObjectToChar(int obj, int ped, const Vec3& off)
{
    if(!WorldObjectExists(obj) || ped <= 0 || !PedPtr(ped)) return;
    CALLSCM(ATTACH_OBJECT_TO_CHAR_304, obj, ped, off.x, off.y, off.z, 0.0f, 0.0f, 0.0f);
}
void Game::ClearWanted()
{
    if(!IsReady()) return;
    CALLSCM(CLEAR_WANTED_LEVEL_304, 0);
}
void Game::SetMaxWanted(int stars)
{
    if(!IsReady()) return;
    CALLSCM(SET_MAX_WANTED_LEVEL_304, stars < 0 ? 0 : (stars > 6 ? 6 : stars));
}

// 3.23.2: the game's object pool (CPools::ms_pObjectPool). MW3 has 162 map objects; on a game
// with many mods the pool can be nearly full, and a full pool crashes CREATE_OBJECT. MW3 keeps
// at least 60 slots free for the game and the other mods.
struct ObjPool304 { void* objects; uint8_t* byteMap; int32_t size; int32_t firstFree; };   // CPool<T>
static ObjPool304** s_ppObjectPool = nullptr;
void Game::InitMapObjects(void* h)
{
    s_ppObjectPool = (ObjPool304**)aml->GetSym(h, "_ZN6CPools14ms_pObjectPoolE");
    logger->Info("MW3: object pool reading %s", s_ppObjectPool ? "on" : "off");
}
int Game::FreeObjectSlots()
{
    if(!s_ppObjectPool || !*s_ppObjectPool) return -1;
    ObjPool304* p = *s_ppObjectPool;
    if(!p->byteMap || p->size <= 0 || p->size > 100000) return -1;
    int n = 0;
    for(int i = 0; i < p->size; ++i) if(p->byteMap[i] & 0x80) ++n;   // bEmpty = top bit
    return n;
}

// 3.23: MW3 map objects (bases, zones, the LS Docks container arena): solid (collision on), any
// rotation, anywhere on the map. 0 = the model is still loading (call again next tick).
// 3.23.2: -1 = no room in the object pool; the model is released right after (the object keeps
// it loaded), so 26 MW3 models are not pinned in memory for good.
int Game::CreateMapObject(int model, const Vec3& p, float rx, float ry, float rz)
{
    if(!IsReady() || model <= 0 || model >= 20000) return 0;
    if(!(p.x == p.x) || !(p.z == p.z) || p.x < -3500.0f || p.x > 3500.0f || p.y < -3500.0f || p.y > 3500.0f) return 0;
    int freeSlots = FreeObjectSlots();
    if(freeSlots >= 0 && freeSlots < 60) return -1;
    if(!CALLSCM(HAS_MODEL_LOADED_304, model)) { CALLSCM(REQUEST_MODEL_304, model); return 0; }
    uint32_t obj = 0;
    CALLSCM(CREATE_OBJECT_304, model, p.x, p.y, p.z, &obj);
    CALLSCM(MARK_MODEL_AS_NO_LONGER_NEEDED_304, model);
    if(!obj || !CALLSCM(DOES_OBJECT_EXIST_304, (int)obj)) return -1;
    CALLSCM(SET_OBJECT_COORDINATES_304, (int)obj, p.x, p.y, p.z);   // exact z (no ground snap)
    CALLSCM(SET_OBJECT_ROTATION_304, (int)obj, rx, ry, rz);
    CALLSCM(SET_OBJECT_COLLISION_304, (int)obj, 1);
    TrackObj((int)obj);
    return (int)obj;
}
