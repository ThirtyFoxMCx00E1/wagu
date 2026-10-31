// 3.01: game helpers for the SA-MP features - map icons (short-range sprite blips), class
// selection (local player model) and actors (looping idle animations). The gang zones are drawn
// in netstate.cpp (the CRadar gang overlay hook).
#include "game.h"
#include "scripting.h"
#include <string.h>

DEFOPCODE(04CE, ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD, fffiv);
DEFOPCODE(09C7, SET_PLAYER_MODEL, ii);
DEFOPCODE(070D, BUILD_PLAYER_MODEL, i);
DEFOPCODE(0665, GET_CHAR_MODEL_301, iv);
DEFOPCODE(04ED, REQUEST_ANIMATION, s);
DEFOPCODE(04EE, HAS_ANIMATION_LOADED, s);
DEFOPCODE(0605, TASK_PLAY_ANIM_PED, issfbbbbi);
DEFOPCODE(087B, GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP, issi);
DEFOPCODE(0784, GIVE_PLAYER_CLOTHES, iiii);
DEFOPCODE(020A, LOCK_CAR_DOORS_301, ii);

void Game::LockCar(int car, bool locked) { if(car > 0 && CarPtr(car)) CALLSCM(LOCK_CAR_DOORS_301, car, locked ? 2 : 1); }   // 3.39
bool Game::CarExists(int car) { return car > 0 && CarPtr(car) != nullptr; }

int Game::AddSpriteBlip(const Vec3& p, int sprite)
{
    if(!IsReady() || sprite <= 0 || sprite > 63) return 0;
    if(!(p.x == p.x) || p.x < -3000.0f || p.x > 3000.0f || p.y < -3000.0f || p.y > 3000.0f) return 0;
    uint32_t blip = 0;
    CALLSCM(ADD_SHORT_RANGE_SPRITE_BLIP_FOR_COORD, p.x, p.y, p.z, sprite, &blip);
    return (int)blip;
}

int Game::LocalModel()
{
    int ped = LocalPed(); if(ped <= 0) return -1;
    int model = -1;
    CALLSCM(GET_CHAR_MODEL_301, ped, &model);
    return model;
}

bool Game::SetLocalModel(int model)
{
    int ped = LocalPed(); if(ped <= 0) return false;
    if(model != 0 && !IsSafePedModel(model)) return false;
    if(model != 0 && !LoadModelNow(model)) return false;   // your own skin change: one model, blocking is fine
    if(IsCharInAnyCar(ped)) return false;   // the game swaps the model only on foot
    CALLSCM(SET_PLAYER_MODEL, 0, model);
    if(model == 0) CALLSCM(BUILD_PLAYER_MODEL, 0);   // CJ: put his clothes back on
    return true;
}

// 3.37: real CJ clothes (player.img): texture + model name on a body part (0 torso, 1 head/hair, 2 legs,
// 3 feet, 13 chain, 14 watch, 15 glasses, 16 hat, 17 special). tex = null / "" takes the part off.
// The player must be CJ (model 0); RebuildPlayer() puts the new clothes on his body.
bool Game::SetLocalClothes(const char* tex, const char* model, int part)
{
    int ped = LocalPed(); if(ped <= 0) return false;
    if(part < 0 || part > 17) return false;
    if(LocalModel() != 0) return false;
    if(!tex || !tex[0] || !model || !model[0]) CALLSCM(GIVE_PLAYER_CLOTHES, 0, 0, 0, part);
    else CALLSCM(GIVE_PLAYER_CLOTHES_OUTSIDE_SHOP, 0, tex, model, part);
    return true;
}
void Game::RebuildPlayer()
{
    int ped = LocalPed(); if(ped <= 0) return;
    if(IsCharInAnyCar(ped) || LocalModel() != 0) return;
    CALLSCM(BUILD_PLAYER_MODEL, 0);
}

bool Game::PlayAnimLoop(int ped, const char* anim, const char* file)
{
    if(ped <= 0 || !anim || !file || !PedPtr(ped)) return false;
    if(!CALLSCM(HAS_ANIMATION_LOADED, file)) { CALLSCM(REQUEST_ANIMATION, file); return false; }
    CALLSCM(TASK_PLAY_ANIM_PED, ped, anim, file, 4.0f, 1, 0, 0, 0, -1);
    return true;
}

DEFOPCODE(04BB, SET_AREA_VISIBLE_301, i);
// derby arenas in the stadium interiors (Blood Bowl = 15): the world shows that area + me + my car
void Game::EnterArea(int area)
{
    int ped = LocalPed(); if(ped <= 0) return;
    CALLSCM(SET_AREA_VISIBLE_301, area);
    SetCharArea(ped, area);
    if(IsCharInAnyCar(ped)) SetCarArea(GetCarCharIsIn(ped), area);
}

// 3.25: Mega Vil Futuristic - people you talk to stop, turn to you and look at you; mechanics fix cars
DEFOPCODE(0639, TASK_TURN_CHAR_TO_FACE_CHAR_325, ii);
DEFOPCODE(05BF, TASK_LOOK_AT_CHAR_325, iii);
DEFOPCODE(0A30, FIX_CAR_325, i);
void Game::TaskFaceChar(int ped, int target)
{
    if(ped <= 0 || target <= 0 || !PedPtr(ped) || !PedPtr(target)) return;
    CALLSCM(TASK_TURN_CHAR_TO_FACE_CHAR_325, ped, target);
}
void Game::TaskLookAtChar(int ped, int target, int ms)
{
    if(ped <= 0 || target <= 0 || !PedPtr(ped) || !PedPtr(target)) return;
    CALLSCM(TASK_LOOK_AT_CHAR_325, ped, target, ms);
}
void Game::FixCar(int car)
{
    if(car <= 0) return;
    CALLSCM(FIX_CAR_325, car);
}
