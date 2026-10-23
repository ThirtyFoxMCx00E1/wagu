// 3.04: game helpers for Brief Case War - briefcase objects (model 1210, the game's own
// briefcase), carried briefcases attached to the carrier's hand, and no wanted level inside the
// war area (Area 69 / Verdant Meadows would give everybody stars and SAM missiles).
#include "game.h"
#include "scripting.h"
#include <string.h>

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

int Game::CreateWorldObject(int model, const Vec3& p)
{
    if(!IsReady() || model <= 0) return 0;
    if(!(p.x == p.x) || p.x < -3000.0f || p.x > 3000.0f || p.y < -3000.0f || p.y > 3000.0f) return 0;
    if(!CALLSCM(HAS_MODEL_LOADED_304, model)) { CALLSCM(REQUEST_MODEL_304, model); return 0; }   // try again next tick
    uint32_t obj = 0;
    CALLSCM(CREATE_OBJECT_304, model, p.x, p.y, p.z, &obj);
    if(!obj) return 0;
    CALLSCM(SET_OBJECT_COLLISION_304, (int)obj, 0);
    return (int)obj;
}
void Game::DeleteWorldObject(int obj)
{
    if(obj > 0 && CALLSCM(DOES_OBJECT_EXIST_304, obj)) CALLSCM(DELETE_OBJECT_304, obj);
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
