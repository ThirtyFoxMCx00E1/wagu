// 3.58: Project Day Z Legacy - the 5th featured server (gamemode "Proper DAYZ").
// Shared by the server (server_dayz.inc) and the client (mp_dayz.inc): names, the port, the item
// table (every item is a REAL GTA:SA object model on the ground), the 15 spawns and the loot sites
// (every GTA:SA gas station, the military bases / camps, towns with houses, ruins).
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#define MP_DZ_SERVER_NAME   "Project Day Z Legacy"
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
                         DZK_GPS, DZK_FUEL, DZK_TOOL, DZK_TRASH, DZK_REVIVE,
                         DZK_SPECIAL, DZK_CLOTHES };   // 3.59 (MTA DayZ): special = val is a DZSP_ code, clothes = val is the skin   // 3.58b: revive = medicine that gets a downed party member up
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
    // 3.58b: medicines that revive a downed party member (+50% HP); appended = old saves keep their ids
    DZI_MORPHINE, DZI_EPINEPHRINE, DZI_BLOODBAG, DZI_COMPASS,
    // 3.59: from MTA DayZ (heat pack, vehicle parts, keycard, the old clothes / skins system, more food, optics)
    DZI_HEATPACK, DZI_TIRE, DZI_ENGINE, DZI_SCRAP, DZI_EMPTY_CAN_FUEL, DZI_KEYCARD,
    DZI_CLOTHES_CIV, DZI_CLOTHES_CAMO, DZI_CLOTHES_GHILLIE, DZI_CLOTHES_SURV,
    DZI_MRE, DZI_STEW, DZI_BINOCULARS, DZI_RANGEFINDER,
    DZI_COUNT };
enum eDzSpecial : uint8_t { DZSP_HEATPACK = 1, DZSP_TIRE, DZSP_ENGINE, DZSP_SCRAP, DZSP_EMPTYCAN, DZSP_KEYCARD, DZSP_BINOCULARS, DZSP_RANGEFINDER };
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
    { "Morphine",         1579, 1241, DZK_REVIVE, 50,   0,  0.10f },   // val = HP % given on revive
    { "Epinephrine",      1580, 1241, DZK_REVIVE, 50,   0,  0.10f },
    { "Blood bag",        1577, 1240, DZK_REVIVE, 50,   0,  0.40f },
    { "Compass",          1239, 1239, DZK_TOOL,   0,    0,  0.10f },
    { "Heat pack",        1241, 1240, DZK_SPECIAL, DZSP_HEATPACK, 0, 0.10f },     // +temperature, cures the cold
    { "Tire",             1098, 1025, DZK_SPECIAL, DZSP_TIRE,     0, 8.00f },     // fixes the tyres of the car next to you
    { "Engine",           1008, 1650, DZK_SPECIAL, DZSP_ENGINE,   0, 12.0f },     // +engine health (a toolbox needed)
    { "Scrap metal",      2040, 1650, DZK_SPECIAL, DZSP_SCRAP,    0, 2.00f },     // +200 vehicle health (a toolbox needed)
    { "Empty gas canister",1650,1650, DZK_SPECIAL, DZSP_EMPTYCAN, 0, 1.00f },     // fill it at a petrol station / siphon a car
    { "Keycard",          1581, 1239, DZK_SPECIAL, DZSP_KEYCARD,  0, 0.05f },     // Area 69 + the San Fierro carrier
    { "Civilian clothes", 2386, 1242, DZK_CLOTHES, 60,  0, 1.00f },
    { "Camo clothes",     2386, 1242, DZK_CLOTHES, 287, 0, 1.00f },   // harder to see
    { "Ghillie suit",     2386, 1242, DZK_CLOTHES, 285, 0, 1.50f },   // almost invisible when still
    { "Survivor clothes", 2386, 1242, DZK_CLOTHES, 73,  0, 1.00f },
    { "MRE",              2768, 2769, DZK_FOOD,   80,   0,  0.40f },
    { "Can (stew)",       2769, 2768, DZK_FOOD,   55,   0,  0.40f },
    { "Binoculars",       367,  1239, DZK_SPECIAL, DZSP_BINOCULARS, 0, 0.60f },
    { "Range finder",     367,  1239, DZK_SPECIAL, DZSP_RANGEFINDER, 0, 0.50f },
};
static inline bool DzKindStacks(int kind) { return kind == DZK_AMMO || kind == DZK_FOOD || kind == DZK_DRINK || kind == DZK_MED || kind == DZK_CURE || kind == DZK_TRASH || kind == DZK_REVIVE; }
static inline bool DzItemStacks(int item) { return DzKindStacks(g_DzItems[(item > 0 && item < DZI_COUNT) ? item : 0].kind) || item == DZI_HEATPACK || item == DZI_SCRAP; }
// 3.58b: inventory sections like the real DayZ inventory (top: weapons, then GEAR, FOOD / DRINKS, MEDICINES, the tool belt)
enum eDzSection : uint8_t { DZSEC_WEAPON = 0, DZSEC_GEAR, DZSEC_FOOD, DZSEC_MED, DZSEC_TOOLBELT };
static inline int DzSection(int kind)
{
    switch(kind)
    {
        case DZK_WEAPON: return DZSEC_WEAPON;
        case DZK_FOOD: case DZK_DRINK: return DZSEC_FOOD;
        case DZK_MED: case DZK_CURE: case DZK_REVIVE: return DZSEC_MED;
        case DZK_GPS: case DZK_TOOL: case DZK_FUEL: return DZSEC_TOOLBELT;
        case DZK_SPECIAL: return DZSEC_TOOLBELT;
        default: return DZSEC_GEAR;
    }
}
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
    { "Fort Carson Desert",        70.7f,  1219.1f, 18.4f,  90.0f },   // 4.00.1: the gas station (was inside the town houses)
    { "El Quebrados Ridge",     -1420.0f,  2620.0f, 55.8f, 180.0f },
    { "Bayside Marina",         -2460.0f,  2310.0f,  4.9f, 270.0f },
    { "Las Barrancas",           -828.3f,  1504.2f, 19.8f,  90.0f },   // 4.00.1: the town road (was in the cliff pueblo: stuck)
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


// ======================================================================================
// 3.59: the MTA DayZ systems (github.com/ThirtyFoxMCx00E1/mtadayz, 0.9.9.5a) on Project Day Z Legacy
// ======================================================================================
// conditions (PktDzMe::cond bits)
enum eDzCond : uint16_t { DZCO_BLEEDING = 1, DZCO_PAIN = 2, DZCO_LEGS = 4, DZCO_ARMS = 8, DZCO_COLD = 16, DZCO_UNCONSCIOUS = 32, DZCO_HELMET = 64, DZCO_KEYCARD = 128 };
// gameplay variables (/dzset <name> <value> by an admin, saved in dayz_vars.txt) - same names as cfgServer.lua
enum eDzVar { DZV_DIFFICULTY = 0, DZV_ZOMBIEDAMAGE, DZV_SPAWNSELECTION, DZV_NEWZOMBIESPAWN, DZV_ZOMBIERADIUS, DZV_ZOMBIESPERLOOT, DZV_PETROLMAX,
              DZV_ITEMRESPAWN, DZV_COMBATLOG, DZV_NOADVERT, DZV_PINGKICK, DZV_PING, DZV_CLOTHES, DZV_HEADSHOTMUL, DZV_TEMPWATER, DZV_TEMPSPRINT,
              DZV_RAIN, DZV_CAMSWAY, DZV_DEBUGMONITOR, DZV_RESPAWNCOUNTDOWN, DZV_COUNT };
struct DzVarDef { const char* name; int def, min, max; const char* help; };
static const DzVarDef g_DzVarDefs[DZV_COUNT] = {
    { "difficulty",          1, 1, 3,       "1 normal, 2 veteran, 3 hardcore (zombie damage, hunger, thirst, sprinting zombies)" },
    { "zombiedamage",        650, 0, 5000,  "blood a zombie hit takes (x difficulty)" },
    { "spawnselection",      1, 0, 1,       "players choose the spawn region" },
    { "newzombiespawnsystem",1, 0, 1,       "zombies spawn near the loot spots" },
    { "zombiespawnradius",   20, 5, 80,     "radius around a loot spot (m)" },
    { "maxzombiesperloot",   6, 1, 12,      "zombies per loot spot" },
    { "maxPetrolFuelAmount", 15, 1, 100,    "max fuel of a petrol station (canisters, random 1..max)" },
    { "itemrespawntimer",    30, 1, 240,    "minutes before taken loot comes back" },
    { "combatlog",           1, 0, 1,       "ANTI-RELOG: disconnect in combat = death" },
    { "noadvert",            1, 0, 1,       "block IPs / links / server adverts in the chat" },
    { "pingkick",            1, 0, 1,       "kick players with a high ping" },
    { "ping",                600, 100, 2000,"max ping for the ping kicker (ms)" },
    { "newclothingsystem",   1, 0, 1,       "clothes = skins, random clothes on spawn" },
    { "headshotdamage",      150, 100, 400, "headshot multiplier in % (helmets stop it)" },
    { "temperaturewater",    10, 0, 100,    "temperature lost per tick in water (x0.01 C)" },
    { "temperaturesprint",   1, 0, 20,      "temperature gained per tick sprinting (x0.005 C)" },
    { "rain",                1, 0, 1,       "occasional rain when the weather is cloudy" },
    { "camerasway",          1, 0, 1,       "camera sway when in pain" },
    { "debugmonitorenabled", 1, 0, 1,       "debug monitor in the journal (key 6)" },
    { "playerRespawnCountdown", 30, 0, 120, "seconds on the death screen" },
};
// achievements (the MTA DayZ table_achievements.lua list) - bit index in the save
struct DzAch { const char* name; const char* desc; uint8_t reward, rewardN; };
enum eDzAchId { DZACH_TASTE1 = 0, DZACH_TASTE2, DZACH_TASTE3, DZACH_FITTEST1, DZACH_FITTEST2, DZACH_ODDS1, DZACH_ODDS2, DZACH_DARK1, DZACH_DARK2,
                DZACH_MONSTER1, DZACH_MONSTER2, DZACH_MONSTER3, DZACH_LITTER, DZACH_HOUSE, DZACH_PULP, DZACH_COMMANDO, DZACH_ROSWELL, DZACH_CARRIER,
                DZACH_HEADHUNTER, DZACH_MEDIC, DZACH_HUNTED, DZACH_COUNT };
static const DzAch g_DzAch[DZACH_COUNT] = {
    { "Tasting Blood I",              "Kill 10 zombies",                 DZI_BANDAGE, 2 },
    { "Tasting Blood II",             "Kill 100 zombies",                DZI_MEDKIT, 1 },
    { "Tasting Blood III",            "Kill 500 zombies",                DZI_AMMO_RIFLE, 3 },
    { "Survival of the Fittest I",    "Survive 1 hour",                  DZI_MRE, 1 },
    { "Survival of the Fittest II",   "Survive 24 hours",                DZI_BAG2, 1 },
    { "Against All Odds I",           "Get 10 headshots",                DZI_AMMO_SNIPER, 2 },
    { "Against All Odds II",          "Get 100 headshots",               DZI_SNIPER, 1 },
    { "What You Are In The Dark I",   "Murder 1 survivor",               DZI_AMMO_PISTOL, 2 },
    { "What You Are In The Dark II",  "Murder 10 survivors",             DZI_DEAGLE, 1 },
    { "He Who Fights Monsters I",     "Kill 1 bandit",                   DZI_BANDAGE, 2 },
    { "He Who Fights Monsters II",    "Kill 10 bandits",                 DZI_M4, 1 },
    { "He Who Fights Monsters III",   "Kill 50 bandits",                 DZI_SNIPER, 1 },
    { "Litterbug",                    "Collect 5 empty tin cans",        DZI_MRE, 1 },
    { "I Can See My House From Here!","Fly a plane / heli above 300 m",  DZI_STEW, 1 },
    { "Bloody Pulp",                  "Drop below 6000 blood",           DZI_BLOODBAG, 1 },
    { "Going Commando",               "Wear no clothes (civilian skin)", DZI_MRE, 0 },
    { "Roswell Conspiracies",         "Visit Area 69",                   DZI_KEYCARD, 0 },
    { "USS Werfukd",                  "Visit the San Fierro carrier",    DZI_MRE, 0 },
    { "Headhunter",                   "Confirm 5 player kills (Check Body)", DZI_RANGEFINDER, 1 },
    { "Field Medic",                  "Revive 5 party members",          DZI_EPINEPHRINE, 2 },
    { "Hunted",                       "Get your blood under 3000 and survive", DZI_HEATPACK, 2 },
};
// skills (0.9.9.5a RPG): 4 trees, 1 skill point every level
enum eDzSkill { DZSK_CRIT = 0, DZSK_TOUGH, DZSK_DODGE, DZSK_PROF, DZSK_SECOND, DZSK_WBOR, DZSK_GOOD, DZSK_SHADOW, DZSK_CAMO, DZSK_AMMO, DZSK_ANALYSIS, DZSK_DUCT, DZSK_COUNT };
struct DzSkill { const char* tree; const char* name; const char* desc; uint8_t maxRank; };
static const DzSkill g_DzSkills[DZSK_COUNT] = {
    { "Soldier",   "Critical Hit",   "+4% chance per rank: a zombie dies in one hit", 5 },
    { "Soldier",   "Toughness",      "-5% zombie damage per rank",                    5 },
    { "Soldier",   "Dodge",          "+3% chance per rank to dodge a zombie hit",     5 },
    { "Medic",     "Proficiency",    "+20% blood from medicine per rank",             5 },
    { "Medic",     "Second Chance",  "+15 s bleed-out time when DOWN per rank",        3 },
    { "Medic",     "Well Born",      "+1% blood regeneration per rank",               5 },
    { "Scavenger", "Good Eye",       "+10% chance per rank of a second item from loot",5 },
    { "Scavenger", "Shadow",         "-1 sound level per rank",                        3 },
    { "Scavenger", "Camouflage",     "-1 visibility level per rank",                   3 },
    { "Engineer",  "Ammo Crafter",   "+1 magazine per rank when you pick up ammo",     3 },
    { "Engineer",  "Analysis",       "+20% fuel from canisters per rank",              3 },
    { "Engineer",  "Duct Tape",      "+20% vehicle repair per rank",                   5 },
};
// spawn selection (gui/spawnselection.png: 8 regions + random), the g_DzSpawns indices of each region
struct DzRegion { const char* name; float u, v; int8_t spawns[3]; };
static const DzRegion g_DzRegions[9] = {
    { "Tierra Robada", 0.115f, 0.17f, { 11, 8, -1 } }, { "Whetstone", 0.115f, 0.62f, { 12, 0, -1 } }, { "San Fierro", 0.305f, 0.47f, { 13, 9, -1 } },
    { "Bone County",   0.475f, 0.17f, { 10, 7, -1 } }, { "Las Venturas", 0.805f, 0.20f, { 7, -1, -1 } }, { "Red County", 0.605f, 0.47f, { 4, 5, 2 } },
    { "Flint County",  0.485f, 0.78f, { 1, 6, 3 } },   { "Los Santos", 0.875f, 0.74f, { 14, -1, -1 } }, { "Random", 0.18f, 0.91f, { -1, -1, -1 } },
};
// keycard areas (Area 69 + the San Fierro carrier) - you need a Keycard to stay inside
struct DzArea { const char* name; float x, y, r; int ach; };
static const DzArea g_DzAreas[2] = { { "Area 69", 213.0f, 1880.0f, 120.0f, DZACH_ROSWELL }, { "the San Fierro carrier", -1380.0f, 500.0f, 70.0f, DZACH_CARRIER } };
// anti-advertising: IPs (1.2.3.4[:port]), links, invites
static inline bool DzIsAdvert(const char* t)
{
    if(!t) return false;
    char low[256]; int n = 0; for(; t[n] && n < 255; ++n) low[n] = (char)tolower((unsigned char)t[n]); low[n] = 0;
    static const char* const bad[] = { "http://", "https://", "www.", "discord.gg", "discord.com/invite", ".com", ".net", ".org", ".xyz", "samp://", "mtasa://", "join my server", "joinmyserver" };
    for(const char* b : bad) if(strstr(low, b)) { if(strstr(low, "discord.gg/projectdayz")) continue; return true; }
    int dots = 0, digits = 0;   // a.b.c.d
    for(int i = 0; low[i]; ++i) { if(isdigit((unsigned char)low[i])) digits++; else if(low[i] == '.' && digits > 0 && digits <= 3) { dots++; digits = 0; } else if(low[i] == ':' && dots >= 3) return true; else { if(dots >= 3 && digits > 0) return true; dots = 0; digits = 0; } }
    return dots >= 3 && digits > 0;
}
