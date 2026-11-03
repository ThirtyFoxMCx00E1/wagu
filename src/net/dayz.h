// 3.58: Project Day Z Remastered - the 5th featured server (gamemode "Proper DAYZ").
// Shared by the server (server_dayz.inc) and the client (mp_dayz.inc): names, the port, the item
// table (every item is a REAL GTA:SA object model on the ground), the 15 spawns and the loot sites
// (every GTA:SA gas station, the military bases / camps, towns with houses, ruins).
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>

#define MP_DZ_SERVER_NAME   "Project Day Z Remastered"
#define MP_DZ_SHORT_NAME    "DAYZ"
#define MP_DZ_GAMEMODE      "Proper DAYZ"
#define MP_DZ_PORT          1933
#define MP_DZ_DISCORD       "discord.gg/projectdayz"
// The user asked for 400 slots. Player ids are one byte on the wire (MP_MAX_PLAYERS = 250, 255 =
// "nobody"), so 250 is the real maximum of every MP server; the DayZ server asks for all of them.
#define MP_DZ_MAX_PLAYERS   250
#define MP_DZ_PARTY_MAX     4

static inline bool MP_IsDzServerName(const char* n)
{
    if(!n || !n[0]) return false;
    if(!strcasecmp(n, MP_DZ_SERVER_NAME)) return true;
    return strcasestr(n, "Project Day Z") != nullptr || strcasestr(n, "Project DayZ") != nullptr;
}

// ---- items ----
enum eDzKind : uint8_t { DZK_NONE = 0, DZK_WEAPON, DZK_AMMO, DZK_FOOD, DZK_DRINK, DZK_MED, DZK_CURE, DZK_BAG, DZK_ARMOR,
                         DZK_GPS, DZK_FUEL, DZK_TOOL, DZK_TRASH };
enum eDzArmorSlot : uint8_t { DZA_HELMET = 0, DZA_CHEST, DZA_VEST, DZA_GLOVES, DZA_BOOTS, DZA_COUNT };
struct DzItem
{
    const char* name;
    int16_t     model;      // the object on the ground (and in the hands)
    int16_t     model2;     // fallback if the first one isn't in this game
    uint8_t     kind;
    int16_t     val;        // weapon id / food+drink amount / blood / bag kg / armor slot / ammo count
    int16_t     ammo;       // weapons: rounds with it; ammo: the weapon group it feeds (weapon id)
    float       kg;
};
enum eDzItemId : uint8_t {
    DZI_NONE = 0,
    // weapons (the real gun models)
    DZI_KNIFE, DZI_KATANA, DZI_COLT, DZI_DEAGLE, DZI_SHOTGUN, DZI_SPAS, DZI_UZI, DZI_TEC9, DZI_MP5, DZI_AK47, DZI_M4,
    DZI_RIFLE, DZI_SNIPER, DZI_AK_GOLD,
    // ammo
    DZI_AMMO_PISTOL, DZI_AMMO_SHELLS, DZI_AMMO_SMG, DZI_AMMO_RIFLE, DZI_AMMO_SNIPER,
    // food / drink / medical
    DZI_BEANS, DZI_BURGER, DZI_PIZZA, DZI_SODA, DZI_WATER, DZI_BEER, DZI_MEDKIT, DZI_BANDAGE, DZI_ANTIBIOTICS, DZI_PAINKILLERS,
    // gear
    DZI_BAG1, DZI_BAG2, DZI_BAG3, DZI_HELMET, DZI_CHEST, DZI_VEST, DZI_GLOVES, DZI_BOOTS, DZI_GPS, DZI_FUELCAN, DZI_TOOLBOX,
    DZI_MAP, DZI_WATCH,
    // trash (houses are full of it)
    DZI_TRASH_CAN, DZI_TRASH_BOTTLE, DZI_TRASH_BOX,
    DZI_COUNT };
static const DzItem g_DzItems[DZI_COUNT] = {
    { "",                 0,    0,    DZK_NONE,   0,    0,  0.0f },
    { "Knife",            335,  335,  DZK_WEAPON, 4,    1,  0.35f },
    { "Katana",           339,  339,  DZK_WEAPON, 8,    1,  1.20f },
    { "Colt 45",          346,  346,  DZK_WEAPON, 22,   17, 1.10f },
    { "Desert Eagle",     348,  348,  DZK_WEAPON, 24,   7,  1.80f },
    { "Shotgun",          349,  349,  DZK_WEAPON, 25,   8,  3.40f },
    { "Combat Shotgun",   351,  351,  DZK_WEAPON, 27,   7,  4.20f },
    { "Micro Uzi",        352,  352,  DZK_WEAPON, 28,   50, 2.10f },
    { "Tec-9",            372,  372,  DZK_WEAPON, 32,   50, 1.90f },
    { "MP5",              353,  353,  DZK_WEAPON, 29,   30, 2.90f },
    { "AK-74",            355,  355,  DZK_WEAPON, 30,   30, 3.60f },
    { "M4A1",             356,  356,  DZK_WEAPON, 31,   30, 3.40f },
    { "Hunting rifle",    357,  357,  DZK_WEAPON, 33,   5,  4.00f },
    { "Sniper rifle",     358,  358,  DZK_WEAPON, 34,   5,  6.50f },
    { "AK-74 Gold",       355,  355,  DZK_WEAPON, 30,   81, 3.60f },
    { "Pistol magazine",  2037, 2061, DZK_AMMO,   17,   22, 0.25f },
    { "Shotgun shells",   2041, 2061, DZK_AMMO,   8,    25, 0.40f },
    { "SMG magazine",     2042, 2061, DZK_AMMO,   30,   28, 0.35f },
    { "Rifle magazine",   2040, 2061, DZK_AMMO,   30,   30, 0.50f },
    { "Sniper rounds",    2043, 2061, DZK_AMMO,   5,    34, 0.30f },
    { "Baked beans",      2769, 2768, DZK_FOOD,   60,   0,  0.45f },
    { "Burger",           2768, 2769, DZK_FOOD,   45,   0,  0.30f },
    { "Pizza slice",      2702, 2768, DZK_FOOD,   35,   0,  0.20f },
    { "Soda can",         2601, 1543, DZK_DRINK,  40,   0,  0.35f },
    { "Water bottle",     1484, 1543, DZK_DRINK,  70,   0,  0.60f },
    { "Beer",             1543, 1484, DZK_DRINK,  25,   0,  0.50f },
    { "Medkit",           1240, 1240, DZK_MED,    6000, 0,  0.80f },
    { "Bandage",          1241, 1240, DZK_MED,    1500, 0,  0.10f },
    { "Antibiotics",      1241, 1240, DZK_CURE,   0,    0,  0.10f },
    { "Painkillers",      1241, 1240, DZK_MED,    800,  0,  0.10f },
    { "Small backpack",   3026, 2919, DZK_BAG,    20,   1,  0.80f },
    { "Assault pack",     3026, 2919, DZK_BAG,    40,   2,  1.20f },
    { "Military rucksack",3026, 2919, DZK_BAG,    70,   3,  1.80f },
    { "Helmet",           1242, 1242, DZK_ARMOR,  DZA_HELMET, 20, 1.50f },
    { "Chest plate",      1242, 1242, DZK_ARMOR,  DZA_CHEST,  35, 4.00f },
    { "Armor vest",       1242, 1242, DZK_ARMOR,  DZA_VEST,   30, 3.00f },
    { "Tactical gloves",  1242, 1242, DZK_ARMOR,  DZA_GLOVES, 5,  0.30f },
    { "Combat boots",     1242, 1242, DZK_ARMOR,  DZA_BOOTS,  10, 1.20f },
    { "GPS",              330,  330,  DZK_GPS,    0,    0,  0.20f },
    { "Fuel canister",    1650, 1650, DZK_FUEL,   30,   0,  5.00f },
    { "Toolbox",          1279, 1650, DZK_TOOL,   0,    0,  3.00f },
    { "Map",              1239, 1239, DZK_TOOL,   0,    0,  0.05f },
    { "Watch",            1239, 1239, DZK_TOOL,   0,    0,  0.05f },
    { "Empty can",        2601, 1543, DZK_TRASH,  0,    0,  0.05f },
    { "Empty bottle",     1543, 1484, DZK_TRASH,  0,    0,  0.10f },
    { "Cardboard box",    1220, 2900, DZK_TRASH,  0,    0,  0.30f },
};
static inline const DzItem& DzItemInfo(int id) { return g_DzItems[(id > 0 && id < DZI_COUNT) ? id : 0]; }
static inline int DzWeaponItem(int weapon)   // the item that is this GTA weapon (for drops / the HUD name)
{
    for(int i = 1; i < DZI_COUNT; ++i) if(g_DzItems[i].kind == DZK_WEAPON && g_DzItems[i].val == weapon) return i;
    return 0;
}

// ---- spawns (random one of 15, each with its name floating over it) ----
struct DzSpawnPt { const char* name; float x, y, z, h; };
static const DzSpawnPt g_DzSpawns[15] = {
    { "Angel Pine Shore",       -2179.0f, -2427.0f, 30.6f, 140.0f },
    { "Flint County Farm",        -66.0f, -1585.0f,  2.6f, 200.0f },
    { "Blueberry Acres",          258.0f,  -158.0f,  1.6f,  90.0f },
    { "Dillimore Woods",          650.0f,  -500.0f, 16.3f, 180.0f },
    { "Palomino Creek",          2268.0f,    35.0f, 26.5f, 270.0f },
    { "Montgomery Fields",       1290.0f,   264.0f, 19.6f,   0.0f },
    { "Back o Beyond",          -1009.0f, -1606.0f, 76.4f,  45.0f },
    { "Fort Carson Desert",       -96.0f,  1080.0f, 19.7f,  90.0f },
    { "El Quebrados Ridge",     -1420.0f,  2620.0f, 55.8f, 180.0f },
    { "Bayside Marina",         -2460.0f,  2310.0f,  4.9f, 270.0f },
    { "Las Barrancas",           -770.0f,  1530.0f, 27.0f,  90.0f },
    { "Tierra Robada Hills",     -315.0f,  2650.0f, 63.0f, 180.0f },
    { "Whetstone Forest",       -1600.0f, -2700.0f, 48.5f,  60.0f },
    { "Ocean Flats Beach",      -2900.0f,   -60.0f,  4.5f, 270.0f },
    { "Verdant Bluffs",          1100.0f, -2050.0f, 69.0f, 180.0f },
};

// ---- loot sites ----
enum eDzSiteKind : uint8_t { DZS_HOUSE = 0, DZS_GAS, DZS_MILITARY, DZS_RUINS };
struct DzSite { uint8_t kind; const char* name; float x, y, r; uint8_t spots; };
static const DzSite g_DzSites[] = {
    // every GTA:SA gas station = supplies (food, drinks, fuel)
    { DZS_GAS, "Idlewood Gas",         1940.0f, -1772.0f, 14.0f, 4 }, { DZS_GAS, "Mulholland Gas",       1004.0f,  -937.0f, 12.0f, 4 },
    { DZS_GAS, "Flint County Gas",      -90.0f, -1169.0f, 14.0f, 4 }, { DZS_GAS, "Dillimore Gas",         655.0f,  -565.0f, 12.0f, 4 },
    { DZS_GAS, "Whetstone Gas",       -1606.0f, -2714.0f, 12.0f, 4 }, { DZS_GAS, "Angel Pine Gas",      -2244.0f, -2560.0f, 12.0f, 3 },
    { DZS_GAS, "Easter Basin Gas",    -1676.0f,   414.0f, 14.0f, 4 }, { DZS_GAS, "Juniper Hollow Gas",  -2410.0f,   976.0f, 12.0f, 4 },
    { DZS_GAS, "Doherty Gas",         -2029.0f,   156.0f, 10.0f, 3 }, { DZS_GAS, "Montgomery Gas",       1381.0f,   459.0f, 12.0f, 4 },
    { DZS_GAS, "Fort Carson Gas",        70.0f,  1218.0f, 12.0f, 4 }, { DZS_GAS, "Bone County Gas",       614.0f,  1692.0f, 14.0f, 4 },
    { DZS_GAS, "Tierra Robada Gas",   -1328.0f,  2677.0f, 12.0f, 4 }, { DZS_GAS, "El Quebrados Gas",    -1471.0f,  1864.0f, 12.0f, 4 },
    { DZS_GAS, "Come-a-Lot Gas",       2115.0f,   920.0f, 14.0f, 4 }, { DZS_GAS, "Spinybed Gas",         2639.0f,  1106.0f, 14.0f, 4 },
    { DZS_GAS, "Pilgrim Gas",          2202.0f,  2475.0f, 14.0f, 4 }, { DZS_GAS, "Redsands Gas",         1596.0f,  2199.0f, 14.0f, 4 },
    { DZS_GAS, "Las Venturas Gas",     2147.0f,  2747.0f, 12.0f, 3 },
    // every military base / camp = gun supplies
    { DZS_MILITARY, "Area 69",                 213.0f,  1880.0f, 45.0f, 16 }, { DZS_MILITARY, "Easter Basin Naval",   -1460.0f,   480.0f, 45.0f, 14 },
    { DZS_MILITARY, "K.A.C.C. Military Fuels",2600.0f,  2800.0f, 40.0f, 10 }, { DZS_MILITARY, "Verdant Meadows Airfield", 400.0f, 2500.0f, 45.0f, 10 },
    { DZS_MILITARY, "LS Airport Hangars",     1900.0f, -2400.0f, 50.0f, 8 },  { DZS_MILITARY, "SF Airport Hangars",    -1300.0f,  -200.0f, 50.0f, 8 },
    { DZS_MILITARY, "LV Airport Hangars",     1600.0f,  1500.0f, 50.0f, 8 },  { DZS_MILITARY, "Mount Chiliad Camp",    -2300.0f, -1600.0f, 25.0f, 6 },
    { DZS_MILITARY, "Fern Ridge Camp",         890.0f,   -80.0f, 20.0f, 5 },  { DZS_MILITARY, "Hunter Quarry Camp",     700.0f,   860.0f, 30.0f, 6 },
    // towns: houses = food, trash and sometimes a gun
    { DZS_HOUSE, "Grove Street",          2495.0f, -1680.0f, 45.0f, 10 }, { DZS_HOUSE, "Ganton",              2380.0f, -1700.0f, 45.0f, 8 },
    { DZS_HOUSE, "Idlewood",              2070.0f, -1700.0f, 50.0f, 8 },  { DZS_HOUSE, "Jefferson",           2150.0f, -1300.0f, 50.0f, 8 },
    { DZS_HOUSE, "East Los Santos",       2440.0f, -1300.0f, 50.0f, 8 },  { DZS_HOUSE, "Willowfield",         2500.0f, -1960.0f, 45.0f, 8 },
    { DZS_HOUSE, "Glen Park",             2000.0f, -1150.0f, 45.0f, 6 },  { DZS_HOUSE, "Vinewood Hills",       1000.0f,  -800.0f, 60.0f, 8 },
    { DZS_HOUSE, "Santa Maria Beach",      330.0f, -1800.0f, 45.0f, 6 },  { DZS_HOUSE, "Marina",               800.0f, -1600.0f, 50.0f, 6 },
    { DZS_HOUSE, "Palomino Creek",        2300.0f,    50.0f, 50.0f, 10 }, { DZS_HOUSE, "Montgomery",          1300.0f,   250.0f, 55.0f, 10 },
    { DZS_HOUSE, "Dillimore",              700.0f,  -550.0f, 50.0f, 10 }, { DZS_HOUSE, "Blueberry",            250.0f,  -150.0f, 55.0f, 10 },
    { DZS_HOUSE, "Angel Pine",           -2150.0f, -2400.0f, 55.0f, 10 }, { DZS_HOUSE, "Fort Carson",          -200.0f,  1100.0f, 60.0f, 10 },
    { DZS_HOUSE, "El Quebrados",         -1450.0f,  2620.0f, 50.0f, 10 }, { DZS_HOUSE, "Las Barrancas",        -800.0f,  1500.0f, 45.0f, 8 },
    { DZS_HOUSE, "Bayside",              -2500.0f,  2350.0f, 55.0f, 10 }, { DZS_HOUSE, "Las Payasadas",        -250.0f,  2650.0f, 45.0f, 8 },
    { DZS_HOUSE, "Doherty",              -2050.0f,   150.0f, 50.0f, 6 },  { DZS_HOUSE, "Hashbury",            -2500.0f,  -100.0f, 50.0f, 8 },
    { DZS_HOUSE, "Juniper Hill",         -2400.0f,   900.0f, 50.0f, 8 },  { DZS_HOUSE, "Chinatown",           -2200.0f,   650.0f, 45.0f, 6 },
    { DZS_HOUSE, "Ocean Flats",          -2700.0f,     0.0f, 50.0f, 8 },  { DZS_HOUSE, "Redsands East",        1800.0f,  2100.0f, 55.0f, 8 },
    { DZS_HOUSE, "Prickle Pine",          1450.0f,  2600.0f, 55.0f, 8 },  { DZS_HOUSE, "Creek",                2800.0f,  2300.0f, 50.0f, 8 },
    { DZS_HOUSE, "Whitewood Estates",      1000.0f,  2000.0f, 50.0f, 6 }, { DZS_HOUSE, "The Farm (Flint)",      -380.0f, -1430.0f, 35.0f, 6 },
    // ruins: broken walls / doors, rocks, chairs - a few supplies in the rubble
    { DZS_RUINS, "Ruins of Los Flores",   2700.0f, -1300.0f, 40.0f, 6 },  { DZS_RUINS, "Ruins of Ocean Docks", 2700.0f, -2400.0f, 45.0f, 6 },
    { DZS_RUINS, "Ruins of Garcia",      -2300.0f,   100.0f, 40.0f, 5 },  { DZS_RUINS, "Ruins of The Strip",   2050.0f,  1500.0f, 45.0f, 6 },
    { DZS_RUINS, "Ruins of Aldea Malvada",-1300.0f, 2500.0f, 35.0f, 5 },  { DZS_RUINS, "Ruins of Hilltop Farm", 1050.0f, -300.0f, 35.0f, 4 },
};
#define DZ_SITES ((int)(sizeof(g_DzSites) / sizeof(g_DzSites[0])))

// ---- chat colours on this server: ONLY grey, white, yellow, green, red, orange ----
#define DZC_GREY   0xA0A0A0FFu
#define DZC_WHITE  0xFFFFFFFFu
#define DZC_YELLOW 0xFACC15FFu
#define DZC_GREEN  0x4ADE80FFu
#define DZC_RED    0xEF4444FFu
#define DZC_ORANGE 0xF97316FFu

// party colours (name tag + chat + kill feed). Distinct, not one of the staff / chat colours, so
// nobody can "dress up" as another party; players with no party are plain light grey.
static const uint32_t g_DzPartyCols[12] = {
    0x38BDF8FFu, 0xA78BFAFFu, 0xF472B6FFu, 0x2DD4BFFFu, 0xFB923CFFu, 0xA3E635FFu,
    0xE879F9FFu, 0x60A5FAFFu, 0xFDE047FFu, 0x34D399FFu, 0xF87171FFu, 0xC4B5FDFFu };
#define DZ_SOLO_COL 0xD4D4D4FFu

// ---- level: points = 10 per zombie, 50 per bandit, 1 per survived minute ----
static inline int DzLevel(int points) { int l = 1, need = 500; while(points >= need && l < 99) { points -= need; ++l; need += 250; } return l; }
static inline int DzToNext(int points) { int need = 500; while(points >= need) { points -= need; need += 250; } return need - points; }
