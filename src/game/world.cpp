#include <math.h>
// 2.21: open world for multiplayer: all cities unlocked, shops / restaurants / gyms / barbers /
// tattoo / clothes / Ammu-Nation doors open, Pay 'n' Spray + mod garages active, race checkpoints,
// ground height, money. Everything here is plain game script commands (same as main.scm uses).
#include "game.h"
#include "scripting.h"
#include <mod/amlmod.h>
#include <string.h>

#define F2U(f) ((uint32_t*)&(f))

DEFOPCODE(0629, SET_INT_STAT, ii);
DEFOPCODE(07FB, SWITCH_ENTRY_EXIT, si);
DEFOPCODE(0299, ACTIVATE_GARAGE, s);
DEFOPCODE(02CE, GET_GROUND_Z_FOR_3D_COORD, fffv);
DEFOPCODE(06D5, CREATE_CHECKPOINT, ifffffffv);
DEFOPCODE(06D6, DELETE_CHECKPOINT, i);
DEFOPCODE(0109, ADD_SCORE, ii);
DEFOPCODE(04E4, REQUEST_COLLISION_W, ff);
DEFOPCODE(03CB, LOAD_SCENE_W, fff);

// enex (door) names from the game's IPL files: everything a player can walk into
// interior (door) names: docs.sannybuilder.com/scm-documentation/sa/interiors
static const char* const kDoors[] =
{
    // food: Well Stacked Pizza, Burger Shot, Cluckin' Bell, Rusty Brown's, World of Coq, diners
    "FDPIZA", "FDBURG", "FDCHICK", "FDDONUT", "FDREST1", "DINER1", "DINER2", "TSDINER",
    // barbers / tattoo parlors
    "BARBERS", "BARBER2", "BARBER3", "TATTOO", "TATTO2", "TATTO3",
    // gyms
    "GYM1", "GYM2", "GYM3",
    // clothes: Binco, Pro-Laps, Sub Urban, Zip, Victim, Didier Sachs
    "CSCHP", "CSSPRT", "LACS1", "CLOTHGP", "CSDESGN", "CSEXL",
    // Ammu-Nation
    "AMMUN1", "AMMUN2", "AMMUN3", "AMMUN4", "AMMUN5",
    // schools, mod garages (TransFender, Loco Low Co, Wheel Arch Angels), race stadiums
    "DRIVES", "DRIVES2", "BIKESCH", "CARMOD1", "CARMOD2", "CARMOD3", "8TRACK", "DIRBIKE",
    // safe houses
    "SVSFSM", "SVLASM", "SVLAMD", "SVLABIG", "SVVGMD", "SVSFMD", "SVCUNT",
    // 24-7, bars, casinos, police
    "X711S2", "X711S3", "X7_11S", "X7_11B", "X7_11C", "X7_11D", "BAR1", "BAR2", "UFOBAR",
    "MAFCAS", "TRICAS", "POLICE1", "POLICE2", "POLICE3",
};
static const char* const kGfDoors[] = { "GF1", "GF2", "GF3", "GF4", "GF5", "GF6" };

// garages (gtamods.com/wiki/Garage): Pay 'n' Spray, TransFender, Loco Low Co, Wheel Arch Angels, bomb shops
static const char* const kGarages[] =
{
    "sprLAe", "spLAw2", "CEspray", "CN2spry", "CNspray", "sprsfse", "sprsfw", "blob1",
    "bodLAwN", "mdsSFSe", "vEcmod", "modlast", "mds1SFS", "lasbomb", "blob2",
};

void Game::UnlockWorld(bool girlfriends)
{
    if(!IsReady()) return;
    CALLSCM(SET_INT_STAT, 181, 4);   // ISLANDS_UNLOCKED: Los Santos + San Fierro + Las Venturas + desert
    for(auto d : kDoors) CALLSCM(SWITCH_ENTRY_EXIT, d, 1);
    if(girlfriends) for(auto d : kGfDoors) CALLSCM(SWITCH_ENTRY_EXIT, d, 1);
    static bool s_Garages = false;   // once per game run: activating is permanent
    if(!s_Garages) { s_Garages = true; for(auto g : kGarages) CALLSCM(ACTIVATE_GARAGE, g); }
}

float Game::GroundZ(float x, float y, float zHint)
{
    CALLSCM(REQUEST_COLLISION_W, x, y);
    float z = 0;
    CALLSCM(GET_GROUND_Z_FOR_3D_COORD, x, y, zHint + 40.0f, F2U(z));
    if(z < -90.0f || z > 1500.0f || z == 0.0f) return zHint;
    return z;
}

// 3.21.4: the first surface below fromZ, or -1000 (nothing / not loaded)
float Game::GroundBelow(float x, float y, float fromZ)
{
    float z = 0;
    CALLSCM(GET_GROUND_Z_FOR_3D_COORD, x, y, fromZ, F2U(z));
    if(z < -90.0f || z > 1500.0f || z == 0.0f || z > fromZ + 0.5f) return -1000.0f;
    return z;
}
// 3.21.4: a place with open sky (no roof / bridge / tree over it) near the ground at zHint
bool Game::OpenGround(float x, float y, float zHint, float& out)
{
    CALLSCM(REQUEST_COLLISION_W, x, y);
    float low = GroundBelow(x, y, zHint + 3.0f);
    if(low < -999.0f || fabsf(low - zHint) > 6.0f) return false;
    float top = GroundBelow(x, y, zHint + 80.0f);
    if(top < -999.0f || top > low + 1.5f) return false;   // something above: a roof
    out = low; return true;
}

void Game::TeleportGround(int ped, Vec3 at, float heading)
{
    if(ped <= 0) return;
    CALLSCM(REQUEST_COLLISION_W, at.x, at.y);
    CALLSCM(LOAD_SCENE_W, at.x, at.y, at.z);
    // 3.21.4: look for the ground just above the point first (it used to start 40 m up and took
    // the roof of the house / the bridge over the spot), the old way only when nothing is there
    float g = GroundBelow(at.x, at.y, at.z + 3.0f);
    if(g > at.z - 15.0f) at.z = g + 1.0f;
    else { g = GroundZ(at.x, at.y, at.z); if(g > at.z - 15.0f && g < at.z + 40.0f) at.z = g + 1.0f; }
    if(IsCharInAnyCar(ped))
    {
        int car = GetCarCharIsIn(ped);
        if(car > 0 && GetDriverOfCar(car) == ped) { SetCarPos(car, at, -0.5f); SetCarHeading(car, heading); SetMoveSpeed(car, true, { 0, 0, 0 }); return; }
    }
    Teleport(ped, at, heading);
}

int Game::CreateRaceCheckpoint(int type, const Vec3& p, const Vec3& next, float radius)
{
    uint32_t h = 0;
    CALLSCM(CREATE_CHECKPOINT, type, p.x, p.y, p.z, next.x, next.y, next.z, radius, &h);
    return (int)h;
}
void Game::DeleteRaceCheckpoint(int h) { if(h > 0) CALLSCM(DELETE_CHECKPOINT, h); }
void Game::AddMoney(int amount) { CALLSCM(ADD_SCORE, 0, amount); }

DEFOPCODE(035F, ADD_ARMOUR_TO_CHAR_W, ii);
DEFOPCODE(048F, REMOVE_ALL_CHAR_WEAPONS, i);
void Game::AddArmour(int ped, int amount) { if(ped > 0 && PedPtr(ped)) CALLSCM(ADD_ARMOUR_TO_CHAR_W, ped, amount); }
void Game::RemoveAllWeapons(int ped) { if(ped > 0 && PedPtr(ped)) CALLSCM(REMOVE_ALL_CHAR_WEAPONS, ped); }
