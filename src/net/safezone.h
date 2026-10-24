#pragma once
// 3.21: hospital SAFE ZONES + 5 s spawn protection. Shared by the phone (mp_safezone.inc) and the
// server (server.cpp), so both sides agree on where nobody can be hurt.
//
// Inside a safe zone (every GTA SA hospital = every place you respawn after being wasted):
//   - you can't be hurt: no bullets, melee, rockets, explosions, fire, car ramming, being pulled
//     out of your car - from players, NPCs or the game (god mode + proofs on you and your car)
//   - you can't hurt anyone: your weapon is put away (fists), drive-by is off, and the server drops
//     every damage packet a player inside a zone sends (and every one aimed at a player inside)
//   - shooting INTO a zone from outside does nothing: the server and the target phone both drop it
// After every respawn: 5 s spawn protection (same rules) wherever you spawn.
// Not used on Brief Case War servers (they have their own base protection) or in /race, /derby,
// /dm and /job (event rules decide there).
#include <math.h>

#define MP_SAFEZONE_RADIUS     32.0f    // metres around the hospital door
#define MP_SAFEZONE_HEIGHT     30.0f    // +- metres up/down (roofs, helicopters just above)
#define MP_SPAWN_PROTECT_MS    5000     // after a respawn

struct MPSafeZone { float x, y, z; const char* name; };

// the 8 hospital restart points of GTA SA (main.scm restart_if_wasted_at, door positions)
static const MPSafeZone g_MPSafeZones[] =
{
    {  1178.0f, -1323.5f, 14.1f, "All Saints General" },          // Los Santos, Market
    {  2034.0f, -1405.0f, 17.2f, "County General" },               // Los Santos, Jefferson
    {  1242.5f,   327.5f, 19.7f, "Crippen Memorial" },             // Montgomery
    { -2200.5f, -2311.5f, 30.6f, "Angel Pine Medical" },           // Angel Pine
    { -2655.0f,   635.5f, 14.4f, "San Fierro Medical" },           // Santa Flora
    { -1514.8f,  2524.0f, 55.8f, "El Quebrados Medical" },         // El Quebrados
    {  -320.0f,  1049.0f, 20.3f, "Fort Carson Medical" },          // Fort Carson
    {  1607.5f,  1820.0f, 10.8f, "Las Venturas Hospital" },        // Las Venturas
};
static const int g_MPSafeZoneCount = (int)(sizeof(g_MPSafeZones) / sizeof(g_MPSafeZones[0]));

// index of the zone the point is in, or -1
inline int MP_SafeZoneAt(float x, float y, float z)
{
    for(int i = 0; i < g_MPSafeZoneCount; ++i)
    {
        const MPSafeZone& s = g_MPSafeZones[i];
        float dx = x - s.x, dy = y - s.y;
        if(dx * dx + dy * dy <= MP_SAFEZONE_RADIUS * MP_SAFEZONE_RADIUS && fabsf(z - s.z) <= MP_SAFEZONE_HEIGHT) return i;
    }
    return -1;
}
inline bool MP_InSafeZone(float x, float y, float z) { return MP_SafeZoneAt(x, y, z) >= 0; }
