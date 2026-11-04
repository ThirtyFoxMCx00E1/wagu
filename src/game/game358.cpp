// 3.58: game helpers for the Timber log carry (CARRY liftup / crry_prtial / putdwn), the Project DayZ
// loot pickups (two-hand pickup animation, objects held in the hands / worn on the back), real fog
// (the camera far clip + fog distance are capped by the RenderWare hooks in main.cpp) and the DayZ
// cabin login scene (a fixed camera looking at the props).
#include "game.h"
#include "scripting.h"
#include <string.h>
#include <math.h>
#include <mod/amlmod.h>
#include <mod/logger.h>

DEFOPCODE(04ED, REQUEST_ANIMATION_358, s);
DEFOPCODE(04EE, HAS_ANIMATION_LOADED_358, s);
DEFOPCODE(0605, TASK_PLAY_ANIM_PED_358, issfbbbbi);
DEFOPCODE(0812, TASK_PLAY_ANIM_SECONDARY_358, issfbbbbi);   // upper body only: you can walk while it plays
DEFOPCODE(069B, ATTACH_OBJECT_TO_CHAR_358, iiffffff);
DEFOPCODE(0682, DETACH_OBJECT_358, ifffb);
DEFOPCODE(03CA, DOES_OBJECT_EXIST_358, i);
DEFOPCODE(015F, SET_FIXED_CAMERA_POSITION_358, ffffff);
DEFOPCODE(0160, POINT_CAMERA_AT_POINT_358, fffi);
DEFOPCODE(015A, RESTORE_CAMERA_JUMPCUT_358, );

static bool AnimReady(const char* file)
{
    if(!file || !file[0]) return false;
    if(!CALLSCM(HAS_ANIMATION_LOADED_358, file)) { CALLSCM(REQUEST_ANIMATION_358, file); return false; }
    return true;
}
bool Game::PlayAnimOnce(int ped, const char* anim, const char* file, float delta, bool lockF, int timeMs)
{
    if(ped <= 0 || !anim || !PedPtr(ped) || !AnimReady(file)) return false;
    CALLSCM(TASK_PLAY_ANIM_PED_358, ped, anim, file, delta, 0, 0, 0, lockF ? 1 : 0, timeMs);
    return true;
}
bool Game::PlayAnimUpper(int ped, const char* anim, const char* file, bool loop)
{
    if(ped <= 0 || !anim || !PedPtr(ped) || !AnimReady(file)) return false;
    CALLSCM(TASK_PLAY_ANIM_SECONDARY_358, ped, anim, file, 4.0f, loop ? 1 : 0, 0, 0, 1, -1);
    return true;
}
void Game::AttachObjectToCharRot(int obj, int ped, const Vec3& off, const Vec3& rot)
{
    if(obj <= 0 || !CALLSCM(DOES_OBJECT_EXIST_358, obj) || ped <= 0 || !PedPtr(ped)) return;
    CALLSCM(ATTACH_OBJECT_TO_CHAR_358, obj, ped, off.x, off.y, off.z, rot.x, rot.y, rot.z);
}
void Game::DetachObject(int obj)
{
    if(obj <= 0 || !CALLSCM(DOES_OBJECT_EXIST_358, obj)) return;
    CALLSCM(DETACH_OBJECT_358, obj, 0.0f, 0.0f, 0.0f, 0);
}

// ---- the fixed scene camera (Project DayZ: the login cabin) -------------------------------------
void Game::SceneCamera(const Vec3& eye, const Vec3& look)
{
    CALLSCM(SET_FIXED_CAMERA_POSITION_358, eye.x, eye.y, eye.z, 0.0f, 0.0f, 0.0f);
    CALLSCM(POINT_CAMERA_AT_POINT_358, look.x, look.y, look.z, 2);
}
void Game::SceneCameraOff() { CALLSCM(RESTORE_CAMERA_JUMPCUT_358); }

// ---- real fog ---------------------------------------------------------------------------------------
// The game draws its fog between the camera's fog distance and its far clip plane (GLES shader fog).
// While a fog event is on, the hooks on RwCameraSetFarClipPlane / RwCameraSetFogDistance (main.cpp)
// cap both: everything past the radius is inside the fog and then not drawn at all - no screen overlay.
static float s_fFogRadius = 0.0f;
void  Game::SetFogRadius(float metres) { s_fFogRadius = metres > 0.0f ? (metres < 25.0f ? 25.0f : metres) : 0.0f; }
float Game::FogRadius() { return s_fFogRadius; }

// ---- 3.58: Project DayZ zombies (local peds that hunt you) ----
DEFOPCODE(05E2, TASK_KILL_CHAR_ON_FOOT_358, ii);
DEFOPCODE(05D3, TASK_GO_STRAIGHT_TO_COORD_358, ifffii);   // 4.02: mode is an int (was "iffffi": the time slot got the mode -> warp to the player)
DEFOPCODE(0223, SET_CHAR_HEALTH_358, ii);
void Game::ZombieHunt(int ped, int target)
{
    if(ped <= 0 || target <= 0 || !PedPtr(ped) || !PedPtr(target)) return;
    CALLSCM(TASK_KILL_CHAR_ON_FOOT_358, ped, target);
}
void Game::NpcGoTo(int ped, const Vec3& p, bool run)
{
    if(ped <= 0 || !PedPtr(ped)) return;
    Vec3 q = p; if(!MP_WorldPos(q)) return;
    // 4.02: time -1 = the ped only walks / runs there by itself (any other value warps it to the point)
    CALLSCM(TASK_GO_STRAIGHT_TO_COORD_358, ped, q.x, q.y, q.z, (int)(run ? 6 : 4), (int)-1);
}
void Game::SetNpcHealth(int ped, int hp)
{
    if(ped <= 0 || !PedPtr(ped)) return;
    CALLSCM(SET_CHAR_HEALTH_358, ped, hp);
}

// ---- 4.00: zombie walk styles (animgrp.dat: drunkman / oldman / oldfatman / shuffle ...) ----
DEFOPCODE(0245, SET_ANIM_GROUP_FOR_CHAR_400, is);
void Game::SetAnimGroup(int ped, const char* group)
{
    if(ped <= 0 || !group || !group[0] || !PedPtr(ped)) return;
    CALLSCM(SET_ANIM_GROUP_FOR_CHAR_400, ped, group);
}
