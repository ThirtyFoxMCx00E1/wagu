// 3.50: server anti-cheat (based on Nex-AC 1.9.69, https://github.com/ThirtyFoxMCx00E1/Nex-AC)
// Shared by the server (checks) and the phone (Add server page -> Gamemode -> Anti-cheat toggles).
// Always on (every check) on the default server, Mega Vil Futuristic, Brief Case War and MW3.
// Custom servers: the host turns it on and picks the checks.
#pragma once
#include <stdint.h>

enum eAcCheck : uint8_t
{
    AC_AIRBREAK = 0,     // on foot / in a vehicle
    AC_TELEPORT,         // on foot / into / in / between vehicles
    AC_TELEPORT_PICKUP,  // taking pickups from far away
    AC_FLY,              // on foot / in a vehicle
    AC_SPEED,            // on foot / in a vehicle
    AC_HEALTH,           // health + armour (on foot / in a vehicle)
    AC_WEAPON,           // invalid weapons, add / infinite ammo
    AC_SPECIAL_ACTION,   // jetpack and the other special actions
    AC_GODMODE,          // from bullets (on foot / in a vehicle)
    AC_INVISIBLE,
    AC_MONEY,
    AC_TUNING,
    AC_LAGCOMP_SPOOF,
    AC_PARKOUR,
    AC_QUICK_TURN,
    AC_RAPID_FIRE,
    AC_FAKE_SPAWN,
    AC_FAKE_KILL,
    AC_PRO_AIM,
    AC_CJ_RUN,
    AC_CARSHOT,
    AC_CARJACK,
    AC_UNFREEZE,
    AC_AFK_GHOST,
    AC_RECONNECT,
    AC_HIGH_PING,
    AC_FAKE_NPC,
    AC_DIALOG,
    AC_SANDBOX,          // protection from sandbox
    AC_INVALID_VERSION,  // protection from invalid version
    AC_SEAT_FLOOD,       // protection from flood by seat changing
    AC_SLOT_FLOOD,       // protection from connection flood in one slot
    AC_RCON,             // admin / moderator key brute force
    AC_CALLBACK_FLOOD,
    AC_CRASHERS,
    AC_NOPS,
    AC_DOS,
    AC_COUNT
};

// cheat = the check also catches the MP / CLEO menu cheats; it runs only when the server has them off
// (PERM_NO_CHEATS and no CheatMenu). The others always run while the anti-cheat is on.
struct AcInfo { const char* name; const char* desc; bool cheat; };
static const AcInfo g_AcInfo[AC_COUNT] =
{
    { "Anti-AirBreak",              "On foot / in vehicle",                          true  },
    { "Anti-Teleport hack",         "On foot / into / in / between vehicles",        true  },
    { "Anti-Teleport (pickups)",    "Taking pickups from far away",                  false },
    { "Anti-FlyHack",               "On foot / in vehicle",                          true  },
    { "Anti-SpeedHack",             "On foot / in vehicle",                          true  },
    { "Anti-Health / Armour hack",  "On foot / in vehicle",                          true  },
    { "Anti-Weapon / Ammo hack",    "Invalid weapons, add / infinite ammo",          true  },
    { "Anti-Special actions hack",  "Including jetpack",                             true  },
    { "Anti-GodMode",               "From bullets, on foot / in vehicle",            true  },
    { "Anti-Invisible hack",        "Hidden from the map and the other players",     true  },
    { "Anti-Money hack",            "Impossible money jumps",                        true  },
    { "Anti-Tuning hack",           "Tuning far from a mod garage",                  true  },
    { "Anti-lagcomp-spoof",         "Fake sender clock",                             false },
    { "Anti-Parkour mod",           "Parkour animations",                            false },
    { "Anti-Quick turn",            "Instant 180 in a fast vehicle",                 true  },
    { "Anti-Rapid fire",            "Shooting faster than the weapon",               false },
    { "Anti-FakeSpawn",             "Respawning without dying",                      false },
    { "Anti-FakeKill",              "Kills without damage",                          false },
    { "Anti-Pro Aim",               "Hits far from the aim / out of range",          false },
    { "Anti-CJ run",                "CJ running style on other skins",               false },
    { "Anti-CarShot",               "Cars launched like bullets",                    true  },
    { "Anti-CarJack",               "Jacking from far away",                         false },
    { "Anti-UnFreeze",              "Leaving jail / a freeze",                       false },
    { "Anti-AFK Ghost",             "Moving while marked as paused",                 false },
    { "Anti-Reconnect",             "Reconnect spam",                                false },
    { "Anti-High ping",             "Ping over 500 ms",                              false },
    { "Anti-Fake NPC",              "Fake bots / NPC names",                         false },
    { "Anti-Dialog hack",           "Answers to dialogs that were never shown",      false },
    { "Protection from sandbox",    "Joined but never playing",                      false },
    { "Protection from invalid version", "Forged builds and names",                  false },
    { "Protection from seat flood", "Seat changing flood",                           false },
    { "Protection from slot flood", "Connection flood in one slot",                  false },
    { "Anti-Rcon hack",             "Admin / moderator key brute force",             false },
    { "Anti-callback flood",        "Dialog, chat, pickup, vehicle... floods",       false },
    { "Anti-crashers",              "Invalid tuning, weapons, seats, dialog text",   false },
    { "Anti-NOPs",                  "Ignoring the server (heal, teleport)",          true  },
    { "Anti-DoS",                   "Packet floods from one address",                false },
};
#define AC_ALL_MASK   ((1ull << AC_COUNT) - 1ull)
#define AC_DEFAULT_MASK AC_ALL_MASK
static inline bool AcMaskHas(uint64_t mask, int c) { return c >= 0 && c < AC_COUNT && (mask >> c) & 1ull; }
