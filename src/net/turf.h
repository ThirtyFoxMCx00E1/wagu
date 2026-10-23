// 3.01: SA-MP style teams (class selection) + gang zones (turf wars). One table for the server (it
// decides who owns a zone) and the phone (it draws the zones on the radar / map and shows the war).
// Zone boxes are real San Andreas districts (the zone table SA-MP scripts use, e.g. zones.inc).
#pragma once
#include <stdint.h>

enum eTeam : uint8_t
{
    TEAM_NONE = 0,     // civilian: no turf
    TEAM_GROVE,        // Grove Street Families
    TEAM_BALLAS,       // Front Yard Ballas
    TEAM_VAGOS,        // Los Santos Vagos
    TEAM_AZTECAS,      // Varrios Los Aztecas
    TEAM_RIFA,         // San Fierro Rifa
    TEAM_DANANG,       // Da Nang Boys
    TEAM_POLICE,       // LSPD / SFPD: can't take turf, can stop a war (counts as a defender everywhere)
    TEAM_COUNT
};

struct MPTeamInfo { const char* name; const char* tag; uint32_t rgba; float sx, sy, sz, sh; };   // spawn
static const MPTeamInfo g_MPTeams[TEAM_COUNT] =
{
    { "Civilian",               "CIV",  0xFFFFFFFF,  1682.0f, -2245.0f,  13.5f, 180.0f },   // LS airport
    { "Grove Street Families",  "GSF",  0x2FB52FFF,  2495.3f, -1687.5f,  13.5f, 180.0f },   // Grove Street
    { "Front Yard Ballas",      "FYB",  0xA040D0FF,  2000.0f, -1121.0f,  26.8f,  90.0f },   // Glen Park
    { "Los Santos Vagos",       "LSV",  0xF0D020FF,  2771.0f, -1259.0f,  56.8f,  90.0f },   // Los Flores
    { "Varrios Los Aztecas",    "VLA",  0x20C8E0FF,  1886.0f, -2019.0f,  13.5f,   0.0f },   // El Corona
    { "San Fierro Rifa",        "SFR",  0x3A6CF0FF, -2245.0f,    94.0f,  35.3f, 270.0f },   // Garcia
    { "Da Nang Boys",           "DNB",  0xE03030FF, -2160.0f,   650.0f,  52.4f,   0.0f },   // Chinatown
    { "Police",                 "COP",  0x3050FFFF,  1544.0f, -1675.0f,  13.6f,  90.0f },   // LSPD
};

struct MPTurfZone { const char* name; float minX, minY, maxX, maxY; uint8_t owner; };
static const MPTurfZone g_MPTurf[] =
{
    // Los Santos
    { "Ganton",                   2222.5f, -1852.8f, 2632.8f, -1722.3f, TEAM_GROVE   },
    { "Ganton North",             2222.5f, -1722.3f, 2632.8f, -1628.5f, TEAM_GROVE   },
    { "Idlewood South",           1971.6f, -1852.8f, 2222.5f, -1742.3f, TEAM_GROVE   },
    { "Playa del Seville",        2703.5f, -2126.9f, 2959.3f, -1852.8f, TEAM_GROVE   },
    { "Idlewood West",            1812.6f, -1742.3f, 1951.6f, -1602.3f, TEAM_BALLAS  },
    { "Idlewood East",            1951.6f, -1742.3f, 2124.6f, -1602.3f, TEAM_BALLAS  },
    { "Idlewood North",           1812.6f, -1602.3f, 2124.6f, -1449.6f, TEAM_BALLAS  },
    { "Glen Park",                1812.6f, -1350.7f, 2056.8f, -1100.8f, TEAM_BALLAS  },
    { "Jefferson",                2056.8f, -1449.6f, 2266.2f, -1372.0f, TEAM_BALLAS  },
    { "Jefferson North",          2056.8f, -1372.0f, 2281.4f, -1210.7f, TEAM_BALLAS  },
    { "East Los Santos",          2222.5f, -1628.5f, 2421.0f, -1494.0f, TEAM_VAGOS   },
    { "East Los Santos East",     2421.0f, -1628.5f, 2632.8f, -1454.3f, TEAM_VAGOS   },
    { "Los Flores",               2581.7f, -1393.4f, 2747.7f, -1135.0f, TEAM_VAGOS   },
    { "Las Colinas",              2281.4f, -1135.0f, 2632.7f,  -945.0f, TEAM_VAGOS   },
    { "East Beach",               2632.8f, -1852.8f, 2959.3f, -1668.1f, TEAM_VAGOS   },
    { "Willowfield",              2324.0f, -2059.2f, 2541.7f, -1852.8f, TEAM_AZTECAS },
    { "Willowfield West",         2089.0f, -1989.9f, 2324.0f, -1852.8f, TEAM_AZTECAS },
    { "El Corona",                1812.6f, -2179.2f, 1970.6f, -1852.8f, TEAM_AZTECAS },
    { "Little Mexico",            1701.9f, -1842.2f, 1812.6f, -1722.2f, TEAM_AZTECAS },
    { "Ocean Docks",              2324.0f, -2302.3f, 2703.5f, -2145.1f, TEAM_AZTECAS },
    // San Fierro
    { "Garcia",                  -2411.2f,  -222.5f,-2173.0f,   265.2f, TEAM_RIFA    },
    { "Doherty",                 -2173.0f,  -222.5f,-1794.9f,   265.2f, TEAM_RIFA    },
    { "Chinatown",               -2274.1f,   578.3f,-2078.6f,   744.1f, TEAM_DANANG  },
    { "Easter Basin",            -1794.9f,   -50.0f,-1499.8f,   249.9f, TEAM_DANANG  },
};
#define MP_TURF_COUNT ((int)(sizeof(g_MPTurf) / sizeof(g_MPTurf[0])))

#define MP_TURF_CAPTURE_SECS 60       // attackers alone in a zone for this long (1 player = 1 point / s, max 3)
#define MP_TURF_TICK_MS      1000

inline bool MP_TeamCanTakeTurf(int team) { return team >= TEAM_GROVE && team <= TEAM_DANANG; }
inline int  MP_TurfAt(float x, float y, float z)
{
    if(z > 400.0f) return -1;   // interiors live high up in the sky
    for(int i = 0; i < MP_TURF_COUNT; ++i)
        if(x >= g_MPTurf[i].minX && x <= g_MPTurf[i].maxX && y >= g_MPTurf[i].minY && y <= g_MPTurf[i].maxY) return i;
    return -1;
}
