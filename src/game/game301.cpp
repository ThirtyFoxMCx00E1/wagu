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
    if(model != 0 && !LoadModel(model)) return false;
    if(IsCharInAnyCar(ped)) return false;   // the game swaps the model only on foot
    CALLSCM(SET_PLAYER_MODEL, 0, model);
    if(model == 0) CALLSCM(BUILD_PLAYER_MODEL, 0);   // CJ: put his clothes back on
    return true;
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
