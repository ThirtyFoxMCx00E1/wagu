// MP 3.04 - Brief Case War
// A private 24/7 gamemode server ("Brief Case War", port 1930, 20 players = 10 per team).
// It is pinned in the Internet list like the default server (red banner + briefcase texture),
// is never offered in "+ Add server", and runs its gamemode inside the shared server core
// (MPServer), so the VPS copy (mp_server) and a phone that hosts it behave the same.
//
// Shared by the client (libMP32/64) and the server: maps, bases, safe zones, vehicle spawners,
// the world barrier and the match timings. Edit the tables here to move a base or a spawner.
#pragma once
#include <math.h>
#include <string.h>
#include <strings.h>
#include "protocol.h"

#define MP_BCW_SERVER_NAME   "Brief Case War"
#define MP_BCW_PORT          1930
#define MP_BCW_MAX_PLAYERS   20        // 10 per team
#define BCW_TEAM_SIZE        BCW_TEAM_SIZE_WIRE
#define BCW_MAP_COUNT        BCW_MAP_COUNT_WIRE
#define BCW_MIN_PLAYERS      20        // a match starts when the queue holds this many (mp_server --bcw-min N)

#define BCW_SAFE_R           9.0f      // safe zone circle (red / blue) at each base
#define BCW_SPAWNER_R        4.0f      // vehicle spawner circles
#define BCW_BARRIER_R        240.0f    // world barrier: 240 m around the line base 1 <-> base 2
#define BCW_PICK_R           2.0f      // touch a dropped briefcase
#define BCW_STEAL_MS         5000      // stand in the enemy circle this long to take their briefcase
#define BCW_DROP_RETURN_MS   60000     // a dropped briefcase nobody touches goes home after 60 s
#define BCW_RESPAWN_MS       11000     // 3.04.4: 10 s + 20 ticks on the connection screen (20-tick steps)
#define BCW_WASTED_MS         5000     // the game's own "wasted" + hospital respawn before that screen
#define BCW_ROLL_MS          7000      // leader slot machine
#define BCW_COOLDOWN_MS      25000     // after a victory: 24 s + 20 ticks (1 s) cooldown - briefcases / objects / war vehicles cleared, then the scoreboard
#define BCW_RESULTS_MS       25000     // scoreboard (Close skips it on your phone)
#define BCW_VOTE_MS          20000     // map vote
#define BCW_SPAWN_PROTECT_MS 5000      // no damage right after a (re)spawn
#define BCW_MUSIC_DELAY_MS   5000      // war_ambient.mp3: 4 s + 20 ticks (1 s) after connecting
#define BCW_MUSIC_VOLUME     0.45f     // 45 %

enum eBcwTeam  : uint8_t { BCW_T_NONE = 0, BCW_T_GUSTAV = 1, BCW_T_SCHELAPZ = 2 };
enum eBcwState : uint8_t { BCW_QUEUE = 0, BCW_ROLL, BCW_LIVE, BCW_RESULTS, BCW_VOTE, BCW_COOLDOWN };   // LIVE -> COOLDOWN -> RESULTS -> VOTE -> QUEUE
enum eBcwCase  : uint8_t { CASE_HOME = 0, CASE_CARRIED, CASE_DROPPED };

static const char* const g_BcwTeamName[3] = { "", "Gustav Team", "Schelapz Team" };
static const char* const g_BcwTeamTag[3]  = { "", "[GTV]", "[SLZ]" };
static const uint32_t    g_BcwTeamRGBA[3] = { 0xFFFFFFFF, 0xE8262CFF, 0x2D7BF0FF };   // red / blue

struct BcwSpawner { float pos[3]; uint8_t kind; };   // kind: 0 military cars, 1 tank, 2 helicopter
struct BcwMap
{
    const char* name;           // vote card title
    const char* area;           // "Bone County"
    bool        open;           // false = locked in the vote
    const char* base[2];        // base names (Gustav = base 1, Schelapz = base 2)
    float       spawn[2][3];    // team spawn = safe zone centre
    float       cam[3], look[3];// connection screen camera
    BcwSpawner  spawners[2][3]; // per team: military car, tank, helicopter
};

// Spawner circles sit 16 m from the safe zone centre (outside the 9 m circle).
static const BcwMap g_BcwMaps[BCW_MAP_COUNT] =
{
    { "Bone County", "Bone County", true,
      { "Verdant Meadows Airfield", "Area 69 Restricted Area" },
      { { 372.50f, 2500.50f, 16.50f }, { 210.50f, 1900.20f, 17.60f } },
      { 330.0f, 1760.0f, 95.0f }, { 210.5f, 1900.2f, 18.0f },
      { { { { 356.5f, 2500.5f, 16.5f }, 0 }, { { 372.5f, 2516.5f, 16.5f }, 1 }, { { 388.5f, 2500.5f, 16.5f }, 2 } },
        { { { 194.5f, 1900.2f, 17.6f }, 0 }, { { 210.5f, 1884.2f, 17.6f }, 1 }, { { 226.5f, 1900.2f, 17.6f }, 2 } } } },
    { "San Fierro Bay", "San Fierro / Tierra Robada", true,
      { "Pleasure Domes / Gant Bridge Base", "Bayside Boat Marina & Helipad" },
      { { -2680.00f, 1590.00f, 11.50f }, { -2330.00f, 2300.00f, 4.30f } },
      { -2420.0f, 1760.0f, 110.0f }, { -2505.0f, 1945.0f, 5.0f },
      { { { { -2696.0f, 1590.0f, 11.5f }, 0 }, { { -2680.0f, 1574.0f, 11.5f }, 1 }, { { -2664.0f, 1590.0f, 11.5f }, 2 } },
        { { { -2346.0f, 2300.0f, 4.3f }, 0 }, { { -2330.0f, 2316.0f, 4.3f }, 1 }, { { -2314.0f, 2300.0f, 4.3f }, 2 } } } },
    // locked maps (shown in the vote with a lock)
    { "Los Santos Docks",   "Los Santos",   false, { "", "" }, { { 2700, -2400, 13 }, { 2500, -2300, 13 } }, { 2600, -2200, 90 }, { 2600, -2350, 13 }, {} },
    { "Las Venturas Strip", "Las Venturas", false, { "", "" }, { { 2030, 1000, 10 }, { 2030, 1600, 10 } }, { 2200, 1300, 90 }, { 2030, 1300, 10 }, {} },
    { "Mount Chiliad",      "Whetstone",    false, { "", "" }, { { -2300, -1600, 480 }, { -2100, -2400, 30 } }, { -2000, -1800, 500 }, { -2300, -1700, 400 }, {} },
    { "Angel Pine Woods",   "Whetstone",    false, { "", "" }, { { -2150, -2400, 30 }, { -1650, -2250, 30 } }, { -1900, -2150, 90 }, { -1900, -2350, 30 }, {} },
};

static const int   g_BcwSpawnerModels[3][3] = { { 470, 433, 0 }, { 432, 0, 0 }, { 425, 0, 0 } };   // Patriot, Barracks | Rhino | Hunter
static const char* const g_BcwSpawnerTitle[3] = { "MILITARY VEHICLES", "TANK", "HELICOPTER" };

inline int BcwTeamIndex(uint8_t team) { return team == BCW_T_GUSTAV ? 0 : (team == BCW_T_SCHELAPZ ? 1 : -1); }
inline bool MP_IsBcwServerName(const char* n) { return n && !strcasecmp(n, MP_BCW_SERVER_NAME); }

// ---- world barrier: a capsule of radius BCW_BARRIER_R around the segment base 1 -> base 2 ----
// Returns the distance from (x,y) to the segment and the closest point on it.
inline float BcwSegDist(int map, float x, float y, float* cx = nullptr, float* cy = nullptr)
{
    const BcwMap& m = g_BcwMaps[map < 0 || map >= BCW_MAP_COUNT ? 0 : map];
    float ax = m.spawn[0][0], ay = m.spawn[0][1], bx = m.spawn[1][0], by = m.spawn[1][1];
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
    if(t < 0) t = 0; if(t > 1) t = 1;
    float px = ax + dx * t, py = ay + dy * t;
    if(cx) *cx = px; if(cy) *cy = py;
    return sqrtf((x - px) * (x - px) + (y - py) * (y - py));
}
inline bool BcwInsideBarrier(int map, float x, float y, float margin = 0) { return BcwSegDist(map, x, y) <= BCW_BARRIER_R - margin; }
// Point on the barrier (perimeter parameter 0..1) - used to draw the animated wall.
inline float BcwBarrierLength(int map)
{
    const BcwMap& m = g_BcwMaps[map];
    float dx = m.spawn[1][0] - m.spawn[0][0], dy = m.spawn[1][1] - m.spawn[0][1];
    return 2.0f * sqrtf(dx * dx + dy * dy) + 2.0f * 3.14159265f * BCW_BARRIER_R;
}
inline void BcwBarrierPoint(int map, float s, float& x, float& y)   // s = metres along the perimeter
{
    const BcwMap& m = g_BcwMaps[map];
    float ax = m.spawn[0][0], ay = m.spawn[0][1], bx = m.spawn[1][0], by = m.spawn[1][1];
    float dx = bx - ax, dy = by - ay, L = sqrtf(dx * dx + dy * dy);
    if(L < 1) L = 1;
    float ux = dx / L, uy = dy / L, nx = -uy, ny = ux, R = BCW_BARRIER_R, half = 3.14159265f * R;
    float total = 2 * L + 2 * half;
    s = fmodf(s, total); if(s < 0) s += total;
    if(s < L) { x = ax + ux * s + nx * R; y = ay + uy * s + ny * R; return; }
    s -= L;
    if(s < half) { float a = s / R; float c = cosf(a), sn = sinf(a); x = bx + (nx * c + ux * sn) * R; y = by + (ny * c + uy * sn) * R; return; }
    s -= half;
    if(s < L) { x = bx - ux * s - nx * R; y = by - uy * s - ny * R; return; }
    s -= L;
    { float a = s / R; float c = cosf(a), sn = sinf(a); x = ax + (-nx * c - ux * sn) * R; y = ay + (-ny * c - uy * sn) * R; }
}
inline float BcwDist2D(const float* a, float x, float y) { return sqrtf((a[0] - x) * (a[0] - x) + (a[1] - y) * (a[1] - y)); }
