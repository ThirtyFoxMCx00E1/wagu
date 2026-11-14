// 4.13.40: VC:MP-style gamemode for GTA Vice City servers (mp_server --game vc).
//
// What it brings (the "generic" gamemode of VC:MP 0.1c, https://github.com/ThirtyFoxMCx00E1/vc-mp,
// GPL v2, kyeman / SA:MP team): classes (team, skin, spawn point, 3 spawn weapons), teams without
// friendly fire, team colours, server cars that respawn, and the server.ini [GAME] file format, so a
// VC:MP server.ini works as it is:
//
//   GameName  = "Vanilla Vice"
//   Teams     = 3
//   TeamName  = 0 Law                    (MP addition, optional)
//   Class     = team skin x y z angle w1 a1 w2 a2 w3 a3
//   Vehicle   = model x y z angle col1 col2
//   VehicleRespawn = 45                  (MP addition: seconds an empty car waits before it goes back)
//
// The server sends INFO + every CLASS + every VEH after the join. Each phone streams the parked cars
// near it as its own script cars. The phone that gets into one says TAKE: the others delete their
// copy (they see the driver's car through the normal vehicle sync). FREE = the driver got out (or the car
// blew up); the server puts the car back on its spawn VehicleRespawn seconds later (VEH state PARKED).
#pragma once
#include <stdint.h>

#define VCMP_MAX_CLASSES   64
#define VCMP_MAX_VEHICLES  250
#define VCMP_MAX_TEAMS     8
#define VCMP_TEAM_NONE     255

enum eVcmpOp : uint8_t
{
    VCMP_INFO = 1,      // S->C  game name, counts, class selection camera
    VCMP_CLASS,         // S->C  one class
    VCMP_VEH,           // S->C  one server car (state)
    VCMP_PTEAM,         // S->C  player id is now in team (after a PICK)
    VCMP_PICK,          // C->S  I picked class idx (and spawned)
    VCMP_TAKE,          // C->S  I got into server car idx
    VCMP_FREE,          // C->S  I left server car idx / it blew up
};
enum eVcmpVehState : uint8_t { VCMP_PARKED = 0, VCMP_TAKEN = 1 };

#pragma pack(push, 1)
struct PktVcmpInfo  { uint8_t type, op; uint8_t classes, vehicles, teams, pad; float playerPos[3], camPos[3], camLook[3]; char gameName[32]; char teamName[VCMP_MAX_TEAMS][16]; };
struct PktVcmpClass { uint8_t type, op, idx, team; int16_t skin, pad; float pos[3]; float angle; uint8_t weapon[3], pad2; uint16_t ammo[3]; };
struct PktVcmpVeh   { uint8_t type, op, idx, state, holder, col1, col2, pad; int16_t model, pad2; float pos[3]; float angle; };
struct PktVcmpCmd   { uint8_t type, op, idx, team; };   // PICK / TAKE / FREE (idx), PTEAM (idx = player id)
#pragma pack(pop)

// team colours (RGBA), VC:MP style: Law blue, Sharks green, Bikers orange...
static const uint32_t g_VcmpTeamColor[VCMP_MAX_TEAMS] = {
    0x4F8CFFFF, 0x3CD25AFF, 0xFF9628FF, 0xE04BE0FF, 0xFFE040FF, 0x40E0E0FF, 0xFF4040FF, 0xC0C0C0FF };

// Vice City weapon names (eWeaponType 0..33), for the kill feed
static const char* const g_VcWeaponName[34] = {
    "Fist", "Brass Knuckles", "Screwdriver", "Golf Club", "Nightstick", "Knife", "Baseball Bat", "Hammer",
    "Meat Cleaver", "Machete", "Katana", "Chainsaw", "Grenade", "Remote Grenade", "Tear Gas", "Molotov",
    "Rocket", "Colt .45", "Python", "Shotgun", "SPAS-12", "Stubby", "Tec-9", "Uzi", "Silenced Ingram",
    "MP5", "M4", "Ruger", "Sniper Rifle", "Laser Sniper", "Rocket Launcher", "Flamethrower", "M60", "Minigun" };
