// 2.19: game helpers for the join camera / spawn picker, chat commands (cheats), jetpack sync,
// joystick tracking (remote walk / run blend) and name-tag occlusion.
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
  #define G_MOVESPEED 104
#else
  #define G_MOVESPEED 72
#endif

#ifndef F2U
#define F2U(f) ((uint32_t*)&(f))
#endif

DEFOPCODE(015F, SET_FIXED_CAMERA_POSITION, ffffff);
DEFOPCODE(0160, POINT_CAMERA_AT_POINT, fffi);
DEFOPCODE(02EB, RESTORE_CAMERA_JUMPCUT, );
DEFOPCODE(0373, SET_CAMERA_BEHIND_PLAYER, );
DEFOPCODE(01B4, SET_PLAYER_CONTROL, ib);
DEFOPCODE(0581, DISPLAY_RADAR, b);
DEFOPCODE(0826, DISPLAY_HUD, b);
DEFOPCODE(0337, SET_CHAR_VISIBLE, ib);
DEFOPCODE(04D7, FREEZE_CHAR_POSITION, ib);
DEFOPCODE(0519, FREEZE_CAR_POSITION, ib);
DEFOPCODE(02AB, SET_CHAR_PROOFS_219, ibbbbb);
DEFOPCODE(02AC, SET_CAR_PROOFS, ibbbbb);
DEFOPCODE(07A7, TASK_JETPACK, i);
DEFOPCODE(0A0C, IS_PLAYER_USING_JETPACK, i);
DEFOPCODE(03CB, LOAD_SCENE_219, fff);
DEFOPCODE(04E4, REQUEST_COLLISION_219, ff);
DEFOPCODE(068D, GET_ACTIVE_CAMERA_COORDINATES_219, vvv);
DEFOPCODE(068E, GET_ACTIVE_CAMERA_POINT_AT_219, vvv);
DEFOPCODE(0792, CLEAR_CHAR_TASKS_IMMEDIATELY_219, i);
DEFOPCODE(0247, REQUEST_MODEL_219, i);
DEFOPCODE(00A1, SET_CHAR_COORDINATES_219, ifff);
DEFOPCODE(01B9, SET_CURRENT_CHAR_WEAPON_219, ii);
DEFOPCODE(0248, HAS_MODEL_LOADED_219, i);
DEFOPCODE(0107, CREATE_OBJECT_219, ifffv);
DEFOPCODE(0108, DELETE_OBJECT_219, i);
DEFOPCODE(0382, SET_OBJECT_COLLISION_219, ib);
DEFOPCODE(069B, ATTACH_OBJECT_TO_CHAR_219, iiffffff);
DEFOPCODE(03CA, DOES_OBJECT_EXIST_219, i);

static void* (*GetPad_fn)(int) = nullptr;
static int16_t (*GetWalkLR_fn)(void*) = nullptr;
static int16_t (*GetWalkUD_fn)(void*) = nullptr;
static bool (*LineOfSight_fn)(const float*, const float*, bool, bool, bool, bool, bool, bool, bool) = nullptr;

static bool SafeRead(const void* addr, void* out, size_t len)
{
    if(!addr) return false;
    struct iovec local = { out, len }, remote = { (void*)addr, len };
    return syscall(__NR_process_vm_readv, (long)getpid(), &local, 1L, &remote, 1L, 0L) == (long)len;
}

void Game::InitGame219(void* h)
{
    SET_TO(GetPad_fn, aml->GetSym(h, "_ZN4CPad6GetPadEi"));
    SET_TO(GetWalkLR_fn, aml->GetSym(h, "_ZN4CPad19GetPedWalkLeftRightEv"));
    SET_TO(GetWalkUD_fn, aml->GetSym(h, "_ZN4CPad16GetPedWalkUpDownEv"));
    SET_TO(LineOfSight_fn, aml->GetSym(h, "_ZN6CWorld21GetIsLineOfSightClearERK7CVectorS2_bbbbbbb"));
}

bool Game::GetStick(int& x, int& y)
{
    x = y = 0;
    if(!GetPad_fn || !GetWalkLR_fn || !GetWalkUD_fn) return false;
    void* pad = GetPad_fn(0);
    if(!pad) return false;
    x = GetWalkLR_fn(pad); y = GetWalkUD_fn(pad);   // -128..128
    return true;
}

bool Game::LineOfSightClear(const Vec3& a, const Vec3& b)
{
    if(!LineOfSight_fn) return true;
    float p0[3] = { a.x, a.y, a.z }, p1[3] = { b.x, b.y, b.z };
    // buildings + objects only (cars / peds don't hide a name tag)
    return LineOfSight_fn(p0, p1, true, false, false, true, false, false, false);
}

void Game::SetFixedCamera(const Vec3& pos, const Vec3& look)
{
    if(!IsReady()) return;
    CALLSCM(SET_FIXED_CAMERA_POSITION, pos.x, pos.y, pos.z, 0.0f, 0.0f, 0.0f);
    CALLSCM(POINT_CAMERA_AT_POINT, look.x, look.y, look.z, 2);   // 2 = jump cut (we ease ourselves)
}

void Game::RestoreCamera()
{
    if(!IsReady()) return;
    CALLSCM(RESTORE_CAMERA_JUMPCUT);
    CALLSCM(SET_CAMERA_BEHIND_PLAYER);
}

bool Game::GetCameraPos(Vec3& pos, Vec3& look)
{
    if(!IsReady()) return false;
    float cx = 0, cy = 0, cz = 0, px = 0, py = 0, pz = 0;
    CALLSCM(GET_ACTIVE_CAMERA_COORDINATES_219, F2U(cx), F2U(cy), F2U(cz));
    CALLSCM(GET_ACTIVE_CAMERA_POINT_AT_219, F2U(px), F2U(py), F2U(pz));
    pos = { cx, cy, cz }; look = { px, py, pz };
    return cx == cx && (cx != 0 || cy != 0);
}

void Game::SetPlayerControl(bool on) { if(IsReady()) CALLSCM(SET_PLAYER_CONTROL, 0, on ? 1 : 0); }
void Game::ShowGameHud(bool on)
{
    if(!IsReady()) return;
    CALLSCM(DISPLAY_HUD, on ? 1 : 0);
    CALLSCM(DISPLAY_RADAR, on ? 1 : 0);
}
void Game::SetCharVisible(int ped, bool on) { if(ped > 0 && PedPtr(ped)) CALLSCM(SET_CHAR_VISIBLE, ped, on ? 1 : 0); }
void Game::FreezeChar(int ped, bool on)     { if(ped > 0 && PedPtr(ped)) CALLSCM(FREEZE_CHAR_POSITION, ped, on ? 1 : 0); }
void Game::FreezeCar(int car, bool on)      { if(car > 0 && CarPtr(car)) CALLSCM(FREEZE_CAR_POSITION, car, on ? 1 : 0); }
void Game::SetCharGodMode(int ped, bool on)
{
    if(ped > 0 && PedPtr(ped)) CALLSCM(SET_CHAR_PROOFS_219, ped, on, on, on, on, on);
}
void Game::SetCarGodMode(int car, bool on)
{
    if(car > 0 && CarPtr(car)) CALLSCM(SET_CAR_PROOFS, car, on, on, on, on, on);
}

bool Game::IsLocalOnJetpack()
{
    if(!IsReady()) return false;
    return CALLSCM(IS_PLAYER_USING_JETPACK, 0) != 0;
}

// 2.19.1: other players' avatars are normal peds, not player peds. The game's real jetpack
// (TASK_JETPACK) is player-only: on an avatar it ends up in CPed::GiveWeapon with no player
// data and crashes the game. So avatars get a jetpack OBJECT on their back instead (looks the
// same, the flight itself comes from the position sync).
int Game::GiveJetpack(int ped)
{
    if(ped <= 0 || !PedPtr(ped)) return 0;
    if(!CALLSCM(HAS_MODEL_LOADED_219, 370)) { CALLSCM(REQUEST_MODEL_219, 370); return 0; }   // jetpack model
    Vec3 p; GetCharPos(ped, p);
    uint32_t obj = 0;
    CALLSCM(CREATE_OBJECT_219, 370, p.x, p.y, p.z + 5.0f, &obj);
    if(!obj) return 0;
    CALLSCM(SET_OBJECT_COLLISION_219, (int)obj, 0);
    CALLSCM(ATTACH_OBJECT_TO_CHAR_219, (int)obj, ped, 0.0f, -0.18f, 0.12f, 0.0f, 0.0f, 0.0f);
    return (int)obj;
}

void Game::RemoveJetpack(int obj)
{
    if(obj > 0 && CALLSCM(DOES_OBJECT_EXIST_219, obj)) CALLSCM(DELETE_OBJECT_219, obj);
}

void Game::PreloadArea(const Vec3& p, bool blocking)
{
    if(!IsReady()) return;
    CALLSCM(REQUEST_COLLISION_219, p.x, p.y);
    if(blocking) CALLSCM(LOAD_SCENE_219, p.x, p.y, p.z);
}

bool Game::GetMoveSpeed(int handle, bool isCar, Vec3& mps)
{
    void* e = isCar ? CarPtr(handle) : PedPtr(handle);
    float v[3];
    if(!e || !SafeRead((u8*)e + G_MOVESPEED, v, 12)) return false;
    mps = { v[0] * 50.0f, v[1] * 50.0f, v[2] * 50.0f };
    return v[0] == v[0];
}

void Game::PlaceChar(int ped, const Vec3& p)
{
    if(ped <= 0 || !PedPtr(ped)) return;
    if(IsCharInAnyCar(ped)) { WarpOutOfCar(ped, p); return; }
    Vec3 q = p; if(!MP_WorldPos(q)) return;   // 2.33
    CALLSCM(SET_CHAR_COORDINATES_219, ped, q.x, q.y, q.z);
}

void Game::SetCharProofs(int ped, bool bullet, bool fire, bool explosion, bool collision, bool melee)
{
    if(ped > 0 && PedPtr(ped)) CALLSCM(SET_CHAR_PROOFS_219, ped, bullet, fire, explosion, collision, melee);
}
void Game::SetCarProofs(int car, bool bullet, bool fire, bool explosion, bool collision, bool melee)
{
    if(car > 0 && CarPtr(car)) CALLSCM(SET_CAR_PROOFS, car, bullet, fire, explosion, collision, melee);
}
