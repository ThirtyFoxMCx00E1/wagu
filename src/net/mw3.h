// MP 3.23 - Call Of Duty: Modern Warfare 3 (MW3), a 24/7 featured gamemode server
// Port of the SA-MP gamemode MW3SAMP (CODMW3.pwn) to MP: 8 teams with their bases, 34 capture
// zones, 15 classes, 20 ranks, killing sprees + multi kill badges, the hourly tournament, 6 DM
// arenas, duels and the events (TDM, DM, Zombie, Last Man Standing) in the LS Docks container
// arena. It never runs on a phone: only the VPS hosts it (mp-mw3.service), nobody becomes host.
//
// Shared by the client (libMP32/64) and the server (net/server_mw3.inc, mp_mw3.inc).
#pragma once
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include "mw3_data.h"

#define MP_MW3_SERVER_NAME   "Call Of Duty: Modern Warfare 3"
#define MP_MW3_SHORT         "MW3"
#define MP_MW3_PORT          1931
#define MP_MW3_MAX_PLAYERS   64
#define MW3_TEAMS            8
#define MW3_RANKS            20
#define MW3_CLASSES          15
#define MW3_MIN_BUILD        32300

#define MW3_CAP_R            4.0f      // stand in the zone checkpoint (on foot)
#define MW3_CAP_MS           25000     // capture time
#define MW3_CAP_SPECIAL_MS   45000     // Big Ear, Mount Chiliad, Area 51, Oil Platform...
#define MW3_RESPAWN_MS       5000
#define MW3_SPAWN_PROTECT_MS 4000
#define MW3_MULTI_MS         6000      // kills within 6 s = double / triple / ... kill
#define MW3_TOURNEY_MS       (60ull * 60ull * 1000ull)   // hourly tournament
#define MW3_EVENT_EVERY_MS   (15ull * 60ull * 1000ull)   // a new event every 15 min
#define MW3_EVENT_SIGNUP_MS  60000     // /join window
#define MW3_EVENT_MAX_MS     (5ull * 60ull * 1000ull)
#define MW3_EVENT_MIN        2         // players needed for an event

// ---- teams (index 1..8; 0 = none) ----
struct Mw3Team { const char* name; const char* tag; uint32_t rgba; int skin; float base[4]; };
static const Mw3Team g_Mw3Teams[MW3_TEAMS + 1] =
{
    { "None",      "",     0xFFFFFFFF, 0,   { 0, 0, 0, 0 } },
    { "Eurasia",   "[EU]", 0x6AFF6AFF, 73,  {  994.2f, 1817.5f,  1185.5f, 2049.6f } },
    { "Arabia",    "[AR]", 0xFF8000FF, 179, { -875.8f, 1389.7f,  -607.2f, 1623.2f } },
    { "Soviet",    "[SO]", 0xF93939FF, 285, { -309.4f, 1024.2f,   103.1f, 1211.7f } },
    { "USA",       "[US]", 0x0080FFFF, 287, { -353.5f, 2574.2f,  -113.3f, 2796.9f } },
    { "Australia", "[AU]", 0x9B37FFFF, 206, {   89.1f, 2418.8f,   466.4f, 2618.0f } },
    { "Brazil",    "[BR]", 0x03FCFCFF, 249, { -769.5f,  808.6f,  -633.5f, 1056.6f } },
    { "Taliban",   "[TB]", 0x008040FF, 161, {  929.9f, 2536.1f,  1241.9f, 2720.1f } },
    { "France",    "[FR]", 0x923E34FF, 108, {-1658.2f, 2519.5f, -1341.8f, 2759.8f } },
};
inline bool Mw3InBase(int team, float x, float y)
{
    if(team < 1 || team > MW3_TEAMS) return false;
    const float* b = g_Mw3Teams[team].base;
    return x >= b[0] && x <= b[2] && y >= b[1] && y <= b[3];
}
inline int Mw3BaseAt(float x, float y) { for(int t = 1; t <= MW3_TEAMS; ++t) if(Mw3InBase(t, x, y)) return t; return 0; }

// ---- ranks: score needed for rank r (1..20) ----
static const int32_t g_Mw3RankScore[MW3_RANKS + 1] = { 0, 0, 100, 150, 500, 1000, 1900, 2500, 3400, 5000, 7500, 10000, 12000, 15000, 17000, 19000, 22000, 26000, 30000, 100000, 500000 };
static const char* const g_Mw3RankName[MW3_RANKS + 1] = { "", "Private", "Corporal", "Seargent", "Lieutenant", "Captain", "Major", "Colonel", "Brigadier", "General", "Field Marshall",
    "Commander", "Head Commander", "King's Advisor", "High Sentinel", "Knight Slayer", "Legend Warrior", "Major Veteran", "Veteran", "God Of War", "Terminator" };
inline int Mw3RankOf(int32_t score) { int r = 1; for(int i = 2; i <= MW3_RANKS; ++i) if(score >= g_Mw3RankScore[i]) r = i; return r; }
// spawn bonus of a rank: health, armour (rank 9: + medkit = full health again on spawn)
inline void Mw3RankBonus(int rank, int& hp, int& armour)
{
    static const int kHp[4] = { 0, 80, 95, 100 };
    static const int kAr[MW3_RANKS + 1] = { 0, 0, 0, 0, 5, 15, 30, 40, 50, 75, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100 };
    hp = rank <= 3 ? kHp[rank < 1 ? 1 : rank] : 100;
    armour = kAr[rank < 0 ? 0 : (rank > MW3_RANKS ? MW3_RANKS : rank)];
}

// ---- classes ----
enum eMw3ClassFlag : uint8_t { MW3CF_NONE = 0, MW3CF_JETPACK = 1, MW3CF_AIR = 2, MW3CF_TANK = 4, MW3CF_MEDIC = 8, MW3CF_SPY = 16 };
struct Mw3Class { const char* name; int rank; uint8_t flags; int weapons[6]; const char* info; };
static const Mw3Class g_Mw3Classes[MW3_CLASSES] =
{
    { "Assault",    1,  0,             { 31, 24, 25, 16, 0 },     "M4, Desert Eagle, Shotgun, Grenades" },
    { "Sniper",     2,  0,             { 34, 23, 4, 17, 0 },      "Sniper Rifle, Silenced Pistol, Knife, Tear Gas" },
    { "Suicider",   3,  0,             { 24, 27, 16, 30, 0 },     "Desert Eagle, Combat Shotgun, Grenades, AK-47" },
    { "Medic",      3,  MW3CF_MEDIC,   { 24, 30, 33, 18, 0 },     "Heals team mates nearby (/heal)" },
    { "Demolisher", 4,  0,             { 24, 27, 18, 33, 35, 0 }, "Rocket Launcher, Molotovs, Combat Shotgun" },
    { "Engineer",   5,  MW3CF_TANK,    { 24, 31, 25, 0 },         "Can drive the Rhino tank (/tank in your base)" },
    { "Scout",      5,  0,             { 17, 29, 26, 24, 0 },     "MP5, Sawn-off, Desert Eagle" },
    { "JetTrooper", 7,  MW3CF_JETPACK, { 28, 8, 0 },              "Jetpack (/jp), Micro Uzi, Katana" },
    { "Spotter",    7,  0,             { 17, 30, 22, 27, 0 },     "AK-47, Pistol, Combat Shotgun" },
    { "Support",    8,  0,             { 27, 16, 24, 34, 31, 0 }, "Combat Shotgun, Grenades, Sniper, M4" },
    { "Spy",        9,  MW3CF_SPY,     { 29, 4, 23, 34, 0 },      "Hidden on the enemy radar, Knife, Sniper" },
    { "Pilot",      10, MW3CF_AIR,     { 24, 29, 46, 0 },         "Hunter, Hydra, Seasparrow (/air in your base)" },
    { "Rifleman",   10, 0,             { 24, 25, 16, 33, 0 },     "Country Rifle, Shotgun, Grenades" },
    { "Recon",      16, 0,             { 17, 31, 34, 0 },         "M4, Sniper Rifle, Tear Gas" },
    { "Bomber",     18, 0,             { 33, 31, 35, 39, 40, 0 }, "Rocket Launcher, Satchel Charges, M4" },
};

// ---- DM arenas (/dm /sdm /mdm /chdm /cbdm /hsdm, /qdm leaves) ----
enum eMw3Arena : uint8_t { MW3A_NONE = 0, MW3A_DM, MW3A_SDM, MW3A_MDM, MW3A_CHDM, MW3A_CBDM, MW3A_HSDM, MW3A_EVENT, MW3A_DUEL };
struct Mw3ArenaDef { const char* cmd; const char* name; int interior; const float (*sp)[3]; int nsp; int weapons[4]; };
static const Mw3ArenaDef g_Mw3Arenas[7] =
{
    { "",     "",                      0,  nullptr, 0, { 0 } },
    { "dm",   "Deathmatch",            15, g_Mw3Ar_DMS,   (int)(sizeof(g_Mw3Ar_DMS)   / sizeof(g_Mw3Ar_DMS[0])),   { 24, 25, 31, 0 } },
    { "sdm",  "Sniper Deathmatch",     3,  g_Mw3Ar_SDMS,  (int)(sizeof(g_Mw3Ar_SDMS)  / sizeof(g_Mw3Ar_SDMS[0])),  { 34, 0 } },
    { "mdm",  "Minigun Deathmatch",    2,  g_Mw3Ar_MGDMS, (int)(sizeof(g_Mw3Ar_MGDMS) / sizeof(g_Mw3Ar_MGDMS[0])), { 38, 0 } },
    { "chdm", "Chainsaw Deathmatch",   15, g_Mw3Ar_CHDMS, (int)(sizeof(g_Mw3Ar_CHDMS) / sizeof(g_Mw3Ar_CHDMS[0])), { 9, 0 } },
    { "cbdm", "Close Combat DM",       18, g_Mw3Ar_CBDMS, (int)(sizeof(g_Mw3Ar_CBDMS) / sizeof(g_Mw3Ar_CBDMS[0])), { 24, 0 } },
    { "hsdm", "Headshot Deathmatch",   1,  g_Mw3Ar_HSDMS, (int)(sizeof(g_Mw3Ar_HSDMS) / sizeof(g_Mw3Ar_HSDMS[0])), { 24, 0 } },
};

// ---- events (LS Docks container arena) ----
enum eMw3EventKind  : uint8_t { MW3E_NONE = 0, MW3E_TDM, MW3E_DM, MW3E_ZOMBIE, MW3E_LMS, MW3E_KINDS };
enum eMw3EventState : uint8_t { MW3ES_IDLE = 0, MW3ES_SIGNUP, MW3ES_LIVE };
static const char* const g_Mw3EventName[MW3E_KINDS] = { "", "Team Deathmatch", "Deathmatch", "Zombie Survival", "Last Man Standing" };
// ---- hourly tournament ----
enum eMw3Tourney : uint8_t { MW3T_KILLS = 0, MW3T_CAPS, MW3T_DEAGLE, MW3T_SPREE, MW3T_KINDS };
static const char* const g_Mw3TourneyName[MW3T_KINDS] = { "Top Kills", "Top Zone Captures", "Top Deagle Kills", "Best Killing Spree" };
// multi kill badges (2..7+ kills within 6 s)
static const char* const g_Mw3Badge[8] = { "", "", "DOUBLE KILL", "TRIPLE KILL", "ULTRA KILL", "MULTI KILL", "MEGA KILL", "SAVAGE" };

inline bool MP_IsMw3ServerName(const char* n) { return n && (!strcasecmp(n, MP_MW3_SERVER_NAME) || !strcasecmp(n, MP_MW3_SHORT) || !strcasecmp(n, "Modern Warfare 3")); }
inline float Mw3Dist2D(const float* a, float x, float y) { return sqrtf((a[0] - x) * (a[0] - x) + (a[1] - y) * (a[1] - y)); }
inline float Mw3CapMs(int zone) { return zone >= 0 && zone < MW3_ZONE_COUNT && g_Mw3Zones[zone].special ? (float)MW3_CAP_SPECIAL_MS : (float)MW3_CAP_MS; }
inline int Mw3ZoneAt(float x, float y)
{
    for(int i = 0; i < MW3_ZONE_COUNT; ++i) { const Mw3Zone& z = g_Mw3Zones[i]; if(x >= z.minX && x <= z.maxX && y >= z.minY && y <= z.maxY) return i; }
    return -1;
}
