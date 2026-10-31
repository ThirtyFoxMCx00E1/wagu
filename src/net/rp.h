// MP 3.25 - Mega Vil Futuristic, a 24/7 Roleplay (RP) featured server
// Los Santos is "Mega Vil", a city of the future. Roleplay only: no teams, no other gamemodes, no
// cheats. A peaceful city: nobody can hurt anybody except on-duty Police Officers and Military
// Soldiers (and nobody inside a greenzone). Money, bank, jobs, shops, houses, pets, vehicles, a real
// phone (contacts, SMS, calls, bank, GPS, games...), people in the street you can talk to, city
// events, a lottery, emotes / dances (names from rpemotes) and 100+ RP commands.
// The house entrances come from open.RP (open_rp.sql), the places and street points from SP-RP.
//
// Shared by the client (libMP32/64, mp_rp.inc) and the server (net/server_rp.inc).
#pragma once
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>

#define MP_RP_SERVER_NAME   "Mega Vil Futuristic"
#define MP_RP_SHORT         "Mega Vil"
#define MP_RP_GAMEMODE      "Roleplay RP"
#define MP_RP_PORT          1932
#define MP_RP_MAX_PLAYERS   250
#define MP_RP_BUILD         32500
#define RP_SKY              0x38BDF8FF   // sky blue (RGBA)
#define RP_SKY_DARK         0x0C4A6EFF
#define RP_WHITE            0xFFFFFFFF
#define RP_GREEN            0x4ADE80FF
#define RP_RED              0xF87171FF
#define RP_YELLOW           0xFDE047FF
#define RP_PURPLE           0xC084FCFF
#define RP_ME_COL           0xC2A2DAFF   // /me
#define RP_DO_COL           0x9EC5FEFF   // /do
#define RP_OOC_COL          0xB0B0B0FF
#define RP_NEWBIE_MIN       120          // new citizen for the first 2 hours played
#define RP_PAYDAY_S         1800         // a payday every 30 minutes played
#define RP_NEED_TICK_S      54           // hunger / thirst -1 every 54 s (100 -> 0 in 1.5 h)
#define RP_TALK_RANGE       20.0f        // normal chat / /me / /do
#define RP_SHOUT_RANGE      45.0f
#define RP_LOW_RANGE        6.0f
#define RP_START_CASH       5000
#define RP_START_BANK       20000
#define RP_JAIL_S           180

inline bool MP_IsRpServerName(const char* n)
{
    return n && (!strcasecmp(n, MP_RP_SERVER_NAME) || !strcasecmp(n, MP_RP_SHORT) || !strcasecmp(n, "MegaVil") || !strcasecmp(n, "Mega Vil RP") || !strcasecmp(n, "Mega Vil Futuristic RP"));
}

// ---- places (SP-RP gps/data.pwn + open.RP businesses) ----------------------------------------
struct RpPlace { const char* name; float x, y, z; uint8_t kind; };
enum eRpPlaceKind : uint8_t { RPK_SPAWN = 0, RPK_JOBS, RPK_BANK, RPK_SHOP, RPK_FOOD, RPK_CLOTHES, RPK_CARS, RPK_TECH, RPK_GUNS, RPK_HOSPITAL, RPK_POLICE, RPK_ATM, RPK_PETS, RPK_OTHER, RPK_ARMY, RPK_AIRPORT };
static const RpPlace g_RpPlaces[] =
{
    { "Mega Vil Plaza (spawn)",         1479.60f, -1690.00f, 14.05f, RPK_SPAWN },
    { "City Hall - Job Center",         1481.00f, -1771.80f, 18.80f, RPK_JOBS },
    { "Mega Bank",                       595.33f, -1250.23f, 18.27f, RPK_BANK },
    { "Mega Vil Police Dept.",          1544.99f, -1675.20f, 13.56f, RPK_POLICE },
    { "County General Hospital",        2036.32f, -1403.90f, 16.92f, RPK_HOSPITAL },
    { "All Saints Hospital",            1172.08f, -1323.35f, 15.40f, RPK_HOSPITAL },
    { "Commerce 24/7",                  1352.36f, -1758.64f, 13.51f, RPK_SHOP },
    { "24/7 Idlewood",                  2068.66f, -1773.72f, 13.56f, RPK_SHOP },
    { "Stop Shop Pershing",             1578.64f, -1450.50f, 13.54f, RPK_SHOP },
    { "McCormick 24/7",                 1315.54f,  -898.39f, 39.58f, RPK_SHOP },
    { "Burger Shot Marina",              810.98f, -1616.20f, 13.55f, RPK_FOOD },
    { "Cluckin' Bell Jefferson",        2420.47f, -1509.05f, 24.00f, RPK_FOOD },
    { "Pizza Stack Co.",                2104.92f, -1806.42f, 13.55f, RPK_FOOD },
    { "Jim's Doughnuts",                1038.15f, -1340.15f, 13.74f, RPK_FOOD },
    { "SuB-URBAN Clothes",              2112.77f, -1212.01f, 23.96f, RPK_CLOTHES },
    { "Lina's J&C Clothes",             1234.86f, -1157.80f, 23.54f, RPK_CLOTHES },
    { "Mega Motors (sports cars)",      1359.74f, -1854.95f, 13.56f, RPK_CARS },
    { "Budget Car Dealership",          2131.91f, -1150.75f, 23.70f, RPK_CARS },
    { "Bike Dealership",                2352.93f, -1485.19f, 23.58f, RPK_CARS },
    { "Aircraft Dealership",            1957.58f, -2183.79f, 13.55f, RPK_CARS },
    { "Neo Tech Store",                 1464.53f, -1051.96f, 24.02f, RPK_TECH },
    { "Holo Pet Shop",                  1988.32f, -1761.59f, 13.55f, RPK_PETS },
    { "Licensed Ammu-Nation",           1368.35f, -1279.81f, 13.55f, RPK_GUNS },
    { "ATM Pershing",                   1497.00f, -1749.00f, 15.45f, RPK_ATM },
    { "ATM Idlewood",                   1928.70f, -1767.00f, 13.55f, RPK_ATM },
    { "ATM Market",                     1155.60f, -1453.30f, 15.80f, RPK_ATM },
    { "ATM Ganton",                     2225.60f, -1721.40f, 13.55f, RPK_ATM },   // 3.29: was on the road (2233, -1730); now on the walk by the gym door
    { "ATM Rodeo",                       494.00f, -1517.00f, 20.10f, RPK_ATM },
    { "DMV (licences)",                 1838.35f, -1443.88f, 13.57f, RPK_OTHER },
    { "Realty Listings",                1742.99f, -1456.21f, 13.52f, RPK_OTHER },
    { "Advert Agency",                  1721.94f, -1608.79f, 13.55f, RPK_OTHER },
    { "Idlewood Gas Station",           1939.02f, -1773.46f, 13.38f, RPK_OTHER },
    { "Pay 'N' Spray Pershing",         1296.89f, -1865.54f, 13.60f, RPK_OTHER },
    { "Butch's Gym",                    2229.50f, -1721.64f, 13.56f, RPK_OTHER },
    { "Ocean Docks - Dock Job",         2753.43f, -2453.79f, 13.64f, RPK_OTHER },
    { "Garbage Depot",                  2247.11f, -2672.97f, 13.56f, RPK_OTHER },
    { "Mega Vil Military Base (docks)", 2734.00f, -2384.00f, 13.63f, RPK_ARMY },
    { "Mega Vil Spaceport (LS Airport)",1682.50f, -2244.00f, 13.54f, RPK_AIRPORT },
};
#define RP_PLACE_COUNT ((int)(sizeof(g_RpPlaces) / sizeof(g_RpPlaces[0])))
#define RP_PLAZA 0
#define RP_JOBCENTER 1
// greenzones: nobody (not even police) can be hurt here
struct RpZone { float x, y, r; };
static const RpZone g_RpGreen[] = { { 1479.6f, -1690.0f, 70.0f }, { 1481.0f, -1771.8f, 30.0f }, { 2036.3f, -1403.9f, 40.0f }, { 1172.1f, -1323.4f, 40.0f }, { 595.3f, -1250.2f, 25.0f } };
inline bool RpInGreen(float x, float y)
{
    for(const RpZone& z : g_RpGreen) { float dx = x - z.x, dy = y - z.y; if(dx * dx + dy * dy < z.r * z.r) return true; }
    return false;
}

// ---- job task points ---------------------------------------------------------------------------
// street points (SP-RP jobs/garbage/data.pwn: the bins of real LS streets)
static const float g_RpStreet[][3] =
{
    { 2499.1399f, -1309.5740f, 34.9449f },
    { 2504.9185f, -1428.9695f, 28.6606f },
    { 2558.8032f, -1368.7695f, 32.5821f },
    { 2763.5620f, -1176.8756f, 69.4887f },
    { 2535.6404f, -942.6977f, 83.5571f },
    { 1972.3395f, -1739.5676f, 13.5629f },
    { 1972.5923f, -1629.7810f, 13.5537f },
    { 1910.0345f, -1873.0114f, 13.6282f },
    { 1680.8942f, -1848.7842f, 13.6152f },
    { 1465.2118f, -1845.3528f, 13.6315f },
    { 1514.5980f, -1847.5392f, 13.6330f },
    { 1338.5458f, -1768.1509f, 13.6248f },
    { 1339.3312f, -1840.9078f, 13.6348f },
    { 856.8906f, -1380.9453f, 14.0349f },
    { 2183.6184f, -1000.2727f, 62.8892f },
    { 2183.5400f, -1186.4955f, 24.3442f },
    { 2180.0369f, -1246.0662f, 23.9817f },
    { 2137.2246f, -1262.1564f, 23.9922f },
    { 1908.4852f, -1801.6218f, 13.5784f },
    { 1988.5538f, -1805.0980f, 13.6316f },
    { 2041.4531f, -1804.8492f, 13.6342f },
    { 2162.9460f, -1980.2291f, 13.6386f },
    { 2325.4863f, -1947.5992f, 13.6665f },
    { 2381.6362f, -1939.1394f, 13.6288f },
    { 2444.6646f, -1977.8380f, 13.6331f },
    { 2679.2390f, -1970.5261f, 13.4420f },
    { 2653.1655f, -2038.6115f, 13.6385f },
    { 2789.1208f, -1945.2130f, 13.6311f },
    { 2233.5996f, -1424.4718f, 24.0032f },
    { 2088.7227f, -1631.9696f, 13.6279f },
    { 2213.0371f, -1682.8466f, 14.1158f },
    { 2356.6240f, -1695.5206f, 13.3556f },
    { 2481.7639f, -1694.8453f, 13.5181f },
    { 2484.7112f, -1768.9296f, 13.5469f },
    { 2295.3838f, -1695.1926f, 13.5405f },
    { 2298.1736f, -1630.8212f, 14.6735f },
    { 2109.4106f, -1824.1066f, 13.6422f },
    { 2040.1188f, -1641.9617f, 13.6316f },
    { 1867.4149f, -1647.6122f, 13.4468f },
    { 1910.0383f, -1687.4792f, 13.4476f },
};
#define RP_STREET_COUNT ((int)(sizeof(g_RpStreet) / sizeof(g_RpStreet[0])))
// truck stores (SP-RP jobs/trucker)
static const float g_RpTruck[][3] =
{
    { 1630.09f, -1903.83f, 13.07f }, { 2281.33f, -2364.85f, 13.55f }, { 2549.67f, -2220.25f, 13.55f }, { 2197.05f, -2663.29f, 13.55f },
    { 1702.82f, -1470.54f, 13.55f }, {  573.14f, -1570.91f, 16.18f }, { 1921.11f, -1864.09f, 13.56f }, { 2045.63f, -2153.55f, 13.19f },
    {  793.35f, -1625.52f, 13.38f }, { 2516.49f, -2428.10f, 13.62f }, { 2508.68f, -2120.69f, 13.55f }, { 2195.80f, -1972.58f, 13.70f },
};
#define RP_TRUCK_COUNT ((int)(sizeof(g_RpTruck) / sizeof(g_RpTruck[0])))
static const float g_RpAir[][3] =   // pilot: Mega Vil Spaceport (LS), San Fierro, Las Venturas, Verdant Meadows
{
    { 1950.0f, -2400.0f, 13.55f }, { -1400.0f, -200.0f, 14.15f }, { 1600.0f, 1600.0f, 10.82f }, { 400.0f, 2500.0f, 16.48f },
};
#define RP_AIR_COUNT 4
static const float g_RpDock[][3] =
{
    { 2753.4f, -2453.8f, 13.64f }, { 2790.0f, -2420.0f, 13.63f }, { 2700.0f, -2470.0f, 13.63f }, { 2650.0f, -2420.0f, 13.63f },
    { 2770.0f, -2510.0f, 13.63f }, { 2720.0f, -2380.0f, 13.63f }, { 2620.0f, -2480.0f, 13.63f }, { 2800.0f, -2370.0f, 13.63f },
};
#define RP_DOCK_COUNT 8

// ---- houses (open.RP open_rp.sql: real LS entrances) -------------------------------------------
struct RpHouse { const char* addr; float x, y, z; int price; };
static const RpHouse g_RpHouses[] =
{
    { "Mulholland 22", 939.65f, -1717.39f, 13.06f, 2500000 },
    { "Richman 14", 300.12f, -1154.46f, 80.55f, 500000 },
    { "Rodeo", 690.50f, -1276.00f, 12.76f, 450000 },
    { "Gleen Park 1", 1788.95f, -1298.65f, 22.21f, 350000 },
    { "Richman 11", 345.05f, -1298.03f, 50.76f, 300000 },
    { "Mulholland 345", 1298.46f, -797.99f, 83.34f, 250000 },
    { "Richman 1", 724.73f, -999.40f, 51.93f, 250000 },
    { "Vinewood 80", 2481.77f, -1331.86f, 27.51f, 250000 },
    { "Richman 22", 189.81f, -1308.35f, 69.45f, 250000 },
    { "Vinewood Boulevard 1", 953.22f, -909.78f, 44.97f, 250000 },
    { "Idlewood Square 3", 1445.34f, -1303.42f, 12.75f, 250000 },
    { "TheBrothersSyndicate", 1270.66f, -1668.22f, 18.93f, 200000 },
    { "Rogue Music Studio", 725.49f, -1440.00f, 12.73f, 200000 },
    { "Santa Maria 606", 315.87f, -1769.78f, 3.83f, 150000 },
    { "Mulholland 5", 1540.47f, -851.39f, 64.34f, 120000 },
    { "Mulholland 30", 1326.07f, -1067.69f, 30.75f, 120000 },
    { "Long Beach 87", 2775.47f, -1404.58f, 43.85f, 120000 },
    { "Temple 3", 1068.43f, -1081.38f, 27.53f, 100000 },
    { "Mulholland 1", 1409.93f, -920.69f, 38.42f, 100000 },
    { "Richman 35", 558.73f, -1075.94f, 72.12f, 100000 },
    { "Temple 14", 1196.50f, -1017.10f, 35.43f, 80000 },
    { "Santa Maria", 168.11f, -1768.40f, 3.69f, 80000 },
    { "Mulholland 20", 785.95f, -828.59f, 70.29f, 80000 },
    { "Richman 4", 1845.43f, -1067.53f, 23.87f, 80000 },
    { "Richman 7", 553.05f, -1200.23f, 44.83f, 80000 },
    { "Richman", 161.51f, -1455.88f, 32.85f, 80000 },
    { "East Beach 1", 2808.05f, -1190.85f, 25.34f, 80000 },
    { "Los Flores 1", 2625.94f, -1112.52f, 68.00f, 70000 },
    { "Marina 5", 692.86f, -1602.78f, 14.25f, 70000 },
    { "East Los Santos 18", 2467.72f, -1200.25f, 36.81f, 70000 },
    { "Ganton 14", 2495.32f, -1690.97f, 14.77f, 65000 },
    { "Idlewood 59", 1930.29f, -1716.53f, 12.85f, 60000 },
    { "Verona Beach 1", 983.90f, -1829.57f, 13.33f, 60000 },
    { "Marina 11", 2066.58f, -1717.11f, 13.01f, 60000 },
    { "Marina 12", 769.22f, -1745.82f, 13.08f, 60000 },
    { "East Beach 5", 2249.35f, -1060.33f, 55.17f, 60000 },
    { "Idlewood 52", 2247.52f, -1627.59f, 14.95f, 50000 },
    { "El Corona 2", 1928.64f, -1915.90f, 15.26f, 50000 },
    { "El Corona 8", 1873.57f, -2070.76f, 15.50f, 50000 },
    { "Jefferson 15", 2263.71f, -1467.99f, 23.17f, 50000 },
    { "El Corona 16", 1734.14f, -2098.23f, 14.04f, 50000 },
    { "El Corona 20", 2357.22f, -1363.75f, 23.29f, 50000 },
    { "Willowfield 52", 2465.37f, -2020.79f, 13.32f, 50000 },
    { "Ganton 10", 2368.23f, -1675.21f, 14.17f, 50000 },
    { "Playa del Seville 1", 2696.37f, -1990.36f, 14.22f, 50000 },
    { "Jefferson 5", 2150.92f, -1419.09f, 25.92f, 50000 },
    { "Jefferson 26", 2091.06f, -1277.83f, 26.18f, 50000 },
    { "Jefferson 32", 2230.06f, -1280.06f, 25.63f, 50000 },
};
#define RP_HOUSE_COUNT ((int)(sizeof(g_RpHouses) / sizeof(g_RpHouses[0])))
static_assert(RP_HOUSE_COUNT <= RP_HOUSES_WIRE, "houses");

// ---- jobs / classes ----------------------------------------------------------------------------
enum eRpJob : uint8_t
{
    RPJ_NONE = 0, RPJ_TAXI, RPJ_TRUCKER, RPJ_PIZZA, RPJ_BUS, RPJ_GARBAGE, RPJ_MECHANIC, RPJ_MEDIC, RPJ_POLICE, RPJ_MILITARY,
    RPJ_FIRE, RPJ_COURIER, RPJ_SWEEPER, RPJ_NEWS, RPJ_PILOT, RPJ_DOCK, RPJ_LAWYER, RPJ_ICECREAM, RPJ_HACKER, RPJ_HOLO, RPJ_COUNT
};
enum eRpTaskSet : uint8_t { RPT_STREET = 0, RPT_TRUCK, RPT_HOUSE, RPT_AIR, RPT_DOCK, RPT_PLACE };
struct RpJob { const char* name; const char* desc; uint8_t minLevel; uint8_t set; uint16_t vehicle; int pay; uint16_t skin; uint32_t col; };
static const RpJob g_RpJobs[RPJ_COUNT] =
{
    { "Citizen",          "No job. Go to City Hall (Job Center) or open Jobs on your phone.",          1, RPT_STREET,   0,    0,   0, 0xE5E7EBFF },
    { "Taxi Driver",      "Drive people across Mega Vil. Fares from street to street.",                1, RPT_STREET, 420,  320,   0, 0xFACC15FF },
    { "Trucker",          "Deliver cargo to the stores and the docks.",                                 1, RPT_TRUCK,  456,  620,  15, 0xF97316FF },
    { "Pizza Boy",        "Hot pizza to the houses of Mega Vil.",                                       1, RPT_HOUSE,  448,  240, 155, 0xEF4444FF },
    { "Bus Driver",       "City bus line through the streets.",                                         1, RPT_STREET, 431,  280,   0, 0x22C55EFF },
    { "Garbage Collector","Empty the bins of Mega Vil (real SP-RP bin route).",                         1, RPT_STREET, 408,  260,  16, 0x84CC16FF },
    { "Mechanic",         "Breakdown calls; /repair other players' cars.",                              1, RPT_STREET, 525,  300,  50, 0xA3A3A3FF },
    { "Paramedic",        "Emergency calls; /heal other players.",                                      1, RPT_STREET, 416,  380, 274, 0xF472B6FF },
    { "Police Officer",   "Patrol, /cuff /arrest /fine. On duty you can use force (level 3+).",         3, RPT_STREET, 596,  420, 280, 0x3B82F6FF },
    { "Military Soldier", "Guard the base and the city. On duty you can use force (level 5+).",         5, RPT_DOCK,   470,  460, 287, 0x166534FF },
    { "Firefighter",      "Fire calls across the city.",                                                1, RPT_STREET, 407,  360, 277, 0xDC2626FF },
    { "Drone Courier",    "Hover-bike parcel delivery to the houses.",                                  1, RPT_HOUSE,  522,  290,   0, 0x38BDF8FF },
    { "Street Sweeper",   "Keep the streets of the future clean.",                                      1, RPT_STREET, 574,  190,  16, 0x94A3B8FF },
    { "News Reporter",    "Fly the SAN News heli to the stories.",                                      2, RPT_STREET, 488,  350,   0, 0xE879F9FF },
    { "Pilot",            "Spaceport flights: Mega Vil, San Fierro, Las Venturas, Verdant Meadows.",    2, RPT_AIR,    519,  950,  61, 0x60A5FAFF },
    { "Dock Worker",      "Move containers at the Ocean Docks (forklift).",                             1, RPT_DOCK,   530,  230,  27, 0xFB923CFF },
    { "Lawyer",           "Visit clients at City Hall, the police and the bank. /defend.",              2, RPT_PLACE,    0,  260,  17, 0xFDE68AFF },
    { "Ice Cream Vendor", "Sell ice cream in the neighbourhoods.",                                       1, RPT_HOUSE,  423,  200,   0, 0xF9A8D4FF },
    { "Cyber Hacker",     "Fix the city's holo terminals (businesses) on foot.",                        2, RPT_PLACE,    0,  330,   0, 0x22D3EEFF },
    { "Holo Engineer",    "Repair the holo towers along the streets.",                                  1, RPT_STREET, 552,  330,  27, 0x67E8F9FF },
};
inline bool RpJobArmed(int job) { return job == RPJ_POLICE || job == RPJ_MILITARY; }

// ---- levels (respect points) -------------------------------------------------------------------
inline int RpLevelFromXp(int xp) { int lv = 1, need = 8; while(xp >= need && lv < 99) { xp -= need; ++lv; need = 8 + lv * 4; } return lv; }
inline int RpXpForNext(int xp, int* into) { int lv = 1, need = 8; while(xp >= need && lv < 99) { xp -= need; ++lv; need = 8 + lv * 4; } if(into) *into = xp; return need; }

// ---- pets: holo drones that float next to you --------------------------------------------------
struct RpPet { const char* name; uint16_t model; int price; };
static const RpPet g_RpPets[] =
{
    { "Holo Heart",   1240,  8000 }, { "Neon Star",    1247, 12000 }, { "Info Drone",  1239, 15000 }, { "Shield Drone", 1242, 22000 },
    { "Cash Bot",     1274, 30000 }, { "Money Cube",   1212, 35000 }, { "Skull Spirit",1254, 45000 }, { "Arrow Wisp",   1318, 50000 },
};
#define RP_PET_COUNT ((int)(sizeof(g_RpPets) / sizeof(g_RpPets[0])))

// ---- shop catalogue ----------------------------------------------------------------------------
// 3.34: the free wardrobe in the lobby (Cosmetics room): categories of ped skins anyone may wear.
// The server takes RPC_SKIN_SET only for a skin in this list (or one bought in the Clothes shop).
struct RpCosmetic { uint8_t cat; uint16_t skin; const char* name; };
enum { RPCO_STREET = 0, RPCO_GANG, RPCO_WORK, RPCO_SMART, RPCO_SPORT, RPCO_BEACH, RPCO_CAT_COUNT };
static const char* const g_RpCosCats[RPCO_CAT_COUNT] = { "STREET", "GANG", "WORK", "SMART", "SPORT", "BEACH" };
static const RpCosmetic g_RpCos[] = {
    { RPCO_STREET, 0, "CJ" }, { RPCO_STREET, 7, "Casual" }, { RPCO_STREET, 22, "Hoodie" }, { RPCO_STREET, 29, "Skater" }, { RPCO_STREET, 47, "Street dude" }, { RPCO_STREET, 56, "Street girl" }, { RPCO_STREET, 60, "Young" }, { RPCO_STREET, 101, "Urban" },
    { RPCO_GANG, 105, "Grove 1" }, { RPCO_GANG, 106, "Grove 2" }, { RPCO_GANG, 107, "Grove 3" }, { RPCO_GANG, 102, "Ballas 1" }, { RPCO_GANG, 103, "Ballas 2" }, { RPCO_GANG, 108, "Vagos" }, { RPCO_GANG, 114, "Aztecas" }, { RPCO_GANG, 117, "Triads" },
    { RPCO_WORK, 50, "Mechanic" }, { RPCO_WORK, 155, "Pizza" }, { RPCO_WORK, 167, "Cluckin" }, { RPCO_WORK, 260, "Builder" }, { RPCO_WORK, 274, "Medic" }, { RPCO_WORK, 277, "Fireman" }, { RPCO_WORK, 255, "Driver" }, { RPCO_WORK, 171, "Waiter" },
    { RPCO_SMART, 17, "Suit" }, { RPCO_SMART, 147, "Gentleman" }, { RPCO_SMART, 150, "Office lady" }, { RPCO_SMART, 187, "Business" }, { RPCO_SMART, 93, "Rich girl" }, { RPCO_SMART, 186, "Old money" }, { RPCO_SMART, 240, "Vinewood" }, { RPCO_SMART, 295, "Boss" },
    { RPCO_SPORT, 41, "Sport girl" }, { RPCO_SPORT, 81, "Runner" }, { RPCO_SPORT, 96, "Jogger" }, { RPCO_SPORT, 80, "Boxer" }, { RPCO_SPORT, 23, "Skate" }, { RPCO_SPORT, 21, "Rapper" }, { RPCO_SPORT, 250, "Cyber" }, { RPCO_SPORT, 100, "Biker" },
    { RPCO_BEACH, 18, "Beach" }, { RPCO_BEACH, 45, "Trunks" }, { RPCO_BEACH, 97, "Surfer" }, { RPCO_BEACH, 138, "Bikini 1" }, { RPCO_BEACH, 139, "Bikini 2" }, { RPCO_BEACH, 140, "Bikini 3" }, { RPCO_BEACH, 154, "Beach dude" }, { RPCO_BEACH, 251, "Lifeguard" },
};
static const int RP_COS_COUNT = (int)(sizeof(g_RpCos) / sizeof(g_RpCos[0]));
static inline bool RpCosAllowed(int skin) { for(int i = 0; i < RP_COS_COUNT; ++i) if(g_RpCos[i].skin == skin) return true; return false; }

enum eRpShopCat : uint8_t { RPS_FOOD = 0, RPS_CLOTHES, RPS_CARS, RPS_TECH, RPS_GUNS, RPS_CAT_COUNT };
static const char* const g_RpShopCats[RPS_CAT_COUNT] = { "Food & Drinks", "Clothes", "Vehicles", "Tech & Pets", "Ammu-Nation" };
enum eRpShopKind : uint8_t { RPI_FOOD = 0, RPI_MEDKIT, RPI_SKIN, RPI_VEH, RPI_PET, RPI_ITEM, RPI_WEAPON, RPI_ARMOUR, RPI_LOTTO, RPI_REPAIR };
enum eRpItem : uint32_t { RPIT_JETPACK = 1, RPIT_DRIVING = 2, RPIT_GUNLIC = 4, RPIT_TOOLKIT = 8, RPIT_THEME_NEON = 16, RPIT_THEME_SUNSET = 32, RPIT_THEME_MATRIX = 64, RPIT_RADIO = 128, RPIT_PARACHUTE = 256,
                          RPIT_STARTER = 512 };   // 3.26: the starter backpack was handed out (once per citizen)
struct RpShopItem { uint8_t cat, kind; const char* name; int price; int a, b; };   // FOOD: a hunger, b thirst; SKIN/VEH/PET: a = id; ITEM: a = bit; WEAPON: a = weapon, b = ammo
static const RpShopItem g_RpShop[] =
{
    { RPS_FOOD, RPI_FOOD,   "Burger",              25,  30,   0 }, { RPS_FOOD, RPI_FOOD,   "Pizza slice",         35,  40,   0 },
    { RPS_FOOD, RPI_FOOD,   "Hot dog",             15,  20,   0 }, { RPS_FOOD, RPI_FOOD,   "Fried chicken",       45,  55,   0 },
    { RPS_FOOD, RPI_FOOD,   "Synth sushi box",     90,  80,  10 }, { RPS_FOOD, RPI_FOOD,   "Doughnut",            10,  12,   0 },
    { RPS_FOOD, RPI_FOOD,   "Water bottle",        10,   0,  40 }, { RPS_FOOD, RPI_FOOD,   "Sprunk soda",         15,   0,  50 },
    { RPS_FOOD, RPI_FOOD,   "Neon energy drink",   30,   5,  60 }, { RPS_FOOD, RPI_FOOD,   "Coffee",              20,   0,  35 },
    { RPS_FOOD, RPI_FOOD,   "Full meal combo",    120, 100, 100 }, { RPS_FOOD, RPI_MEDKIT, "Medkit (+60 health)",150,  60,   0 },
    { RPS_FOOD, RPI_LOTTO,  "Lottery ticket",     100,   0,   0 }, { RPS_FOOD, RPI_REPAIR, "Car repair kit",     250,   0,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Street casual",     500,   7,   0 }, { RPS_CLOTHES, RPI_SKIN, "Hoodie",          500,  23,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Beach style",       400,  18,   0 }, { RPS_CLOTHES, RPI_SKIN, "Business suit",  2500,  17,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Tech worker",      1200,  60,   0 }, { RPS_CLOTHES, RPI_SKIN, "Biker",          1500, 100,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Gentleman",        3000, 147,   0 }, { RPS_CLOTHES, RPI_SKIN, "Rich girl",      3000,  93,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Sport girl",        900,  41,   0 }, { RPS_CLOTHES, RPI_SKIN, "Party girl",     1100,  12,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Office lady",      1800, 150,   0 }, { RPS_CLOTHES, RPI_SKIN, "Skater",          700,  29,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Rapper",           1600,  21,   0 }, { RPS_CLOTHES, RPI_SKIN, "Old money",      5000, 186,   0 },
    { RPS_CLOTHES, RPI_SKIN, "Cyber punk",       4000, 250,   0 }, { RPS_CLOTHES, RPI_SKIN, "Vinewood star",  9000, 240,   0 },
    { RPS_CARS, RPI_VEH, "Faggio scooter",       2500, 462,   0 }, { RPS_CARS, RPI_VEH, "Sanchez",           6000, 468,   0 },
    { RPS_CARS, RPI_VEH, "Blista Compact",      12000, 496,   0 }, { RPS_CARS, RPI_VEH, "Premier",          18000, 426,   0 },
    { RPS_CARS, RPI_VEH, "Landstalker",         25000, 400,   0 }, { RPS_CARS, RPI_VEH, "Huntley",          42000, 579,   0 },
    { RPS_CARS, RPI_VEH, "Sultan",              48000, 560,   0 }, { RPS_CARS, RPI_VEH, "Elegy",            52000, 562,   0 },
    { RPS_CARS, RPI_VEH, "Jester",              56000, 559,   0 }, { RPS_CARS, RPI_VEH, "NRG-500 hover bike",65000, 522,   0 },
    { RPS_CARS, RPI_VEH, "Banshee",             80000, 429,   0 }, { RPS_CARS, RPI_VEH, "Super GT",         90000, 506,   0 },
    { RPS_CARS, RPI_VEH, "Cheetah",            105000, 415,   0 }, { RPS_CARS, RPI_VEH, "Bullet",          120000, 541,   0 },
    { RPS_CARS, RPI_VEH, "Turismo",            135000, 451,   0 }, { RPS_CARS, RPI_VEH, "Infernus",        150000, 411,   0 },
    { RPS_CARS, RPI_VEH, "Maverick heli",      300000, 487,   0 }, { RPS_CARS, RPI_VEH, "Shamal jet",      500000, 519,   0 },
    { RPS_TECH, RPI_ITEM, "Jetpack licence",   250000, RPIT_JETPACK, 0 }, { RPS_TECH, RPI_ITEM, "Driving licence",    1500, RPIT_DRIVING, 0 },
    { RPS_TECH, RPI_ITEM, "Toolkit",              800, RPIT_TOOLKIT, 0 }, { RPS_TECH, RPI_ITEM, "Phone theme: Neon",   600, RPIT_THEME_NEON, 0 },
    { RPS_TECH, RPI_ITEM, "Phone theme: Sunset",  600, RPIT_THEME_SUNSET, 0 }, { RPS_TECH, RPI_ITEM, "Phone theme: Matrix",  600, RPIT_THEME_MATRIX, 0 },
    { RPS_TECH, RPI_ITEM, "Holo radio",          1200, RPIT_RADIO, 0 }, { RPS_TECH, RPI_ITEM, "Parachute pack",       900, RPIT_PARACHUTE, 0 },
    { RPS_TECH, RPI_PET, "Pet: Holo Heart",      8000, 0, 0 }, { RPS_TECH, RPI_PET, "Pet: Neon Star",      12000, 1, 0 },
    { RPS_TECH, RPI_PET, "Pet: Info Drone",     15000, 2, 0 }, { RPS_TECH, RPI_PET, "Pet: Shield Drone",   22000, 3, 0 },
    { RPS_TECH, RPI_PET, "Pet: Cash Bot",       30000, 4, 0 }, { RPS_TECH, RPI_PET, "Pet: Money Cube",     35000, 5, 0 },
    { RPS_TECH, RPI_PET, "Pet: Skull Spirit",   45000, 6, 0 }, { RPS_TECH, RPI_PET, "Pet: Arrow Wisp",     50000, 7, 0 },
    { RPS_GUNS, RPI_ITEM,   "Gun licence (police / military)", 5000, RPIT_GUNLIC, 0 },
    { RPS_GUNS, RPI_WEAPON, "Camera",               300, 43, 50 }, { RPS_GUNS, RPI_WEAPON, "Flowers",            50, 14, 1 },
    { RPS_GUNS, RPI_WEAPON, "Cane",                 120, 15, 1 },  { RPS_GUNS, RPI_WEAPON, "Parachute",         500, 46, 1 },
    { RPS_GUNS, RPI_WEAPON, "Nightstick (duty)",    200,  3, 1 },  { RPS_GUNS, RPI_WEAPON, "Desert Eagle (duty)", 1500, 24, 70 },
    { RPS_GUNS, RPI_WEAPON, "Shotgun (duty)",      2500, 25, 50 }, { RPS_GUNS, RPI_WEAPON, "MP5 (duty)",         3500, 29, 300 },
    { RPS_GUNS, RPI_WEAPON, "M4 (duty)",           6000, 31, 300 },{ RPS_GUNS, RPI_WEAPON, "Sniper (duty)",      9000, 34, 30 },
    { RPS_GUNS, RPI_ARMOUR, "Body armour (duty)",  1000, 100, 0 },
    // 3.26: appended (the phone sends the index, so new rows only go at the end)
    { RPS_FOOD, RPI_FOOD,   "Iced Tea",            15,   0,  45 }, { RPS_FOOD, RPI_FOOD,   "Fried Rice",          40,  50,   0 },
};
#define RP_SHOP_COUNT ((int)(sizeof(g_RpShop) / sizeof(g_RpShop[0])))

// ---- 3.36: the hotel room computer (eyefind.info): vehicle web shops, on every server -----------
// site: 0 Southern San Andreas Super Autos (cars, vans, trucks), 1 Legendary Motorsport (sports / luxury),
// 2 Pedal and Metal (bikes), 3 Elitas Travel (aircraft), 4 Docktease (boats). Prices in $.
// Mega Vil: RPC_BUY with a = 2, val = model (the server checks the model + price here).
struct HotelVeh { uint16_t model; uint8_t site; int price; };
static const HotelVeh g_HotelVeh[] =
{
    // Legendary Motorsport
    { 411, 1, 150000 }, { 451, 1, 135000 }, { 541, 1, 120000 }, { 415, 1, 105000 }, { 494, 1,  95000 }, { 502, 1,  95000 },
    { 503, 1,  95000 }, { 506, 1,  90000 }, { 409, 1,  85000 }, { 429, 1,  80000 }, { 480, 1,  75000 }, { 477, 1,  70000 },
    { 402, 1,  69000 }, { 434, 1,  60000 }, { 559, 1,  56000 }, { 562, 1,  52000 }, { 555, 1,  50000 }, { 560, 1,  48000 },
    { 587, 1,  45000 }, { 603, 1,  44000 }, { 579, 1,  42000 }, { 602, 1,  40000 }, { 533, 1,  38000 }, { 558, 1,  35000 },
    { 545, 1,  32000 }, { 565, 1,  30000 }, { 535, 1,  30000 }, { 589, 1,  28000 }, { 534, 1,  27000 }, { 536, 1,  26000 },
    { 567, 1,  25000 }, { 575, 1,  24000 }, { 576, 1,  22000 }, { 412, 1,  20000 }, { 518, 1,  18000 },
    // Southern San Andreas Super Autos
    { 580, 0,  30000 }, { 400, 0,  25000 }, { 489, 0,  20000 }, { 500, 0,  19000 }, { 421, 0,  18000 }, { 426, 0,  18000 },
    { 554, 0,  18000 }, { 424, 0,  18000 }, { 507, 0,  17000 }, { 405, 0,  16000 }, { 482, 0,  16000 }, { 445, 0,  15000 },
    { 475, 0,  15000 }, { 526, 0,  15000 }, { 561, 0,  15000 }, { 568, 0,  15000 }, { 459, 0,  15000 }, { 420, 0,  15000 },
    { 439, 0,  14000 }, { 516, 0,  14000 }, { 551, 0,  14000 }, { 585, 0,  14000 }, { 413, 0,  14000 }, { 440, 0,  14000 },
    { 438, 0,  14000 }, { 458, 0,  13000 }, { 517, 0,  13000 }, { 566, 0,  13000 }, { 419, 0,  12000 }, { 474, 0,  12000 },
    { 496, 0,  12000 }, { 540, 0,  12000 }, { 546, 0,  12000 }, { 550, 0,  12000 }, { 483, 0,  12000 }, { 422, 0,  11000 },
    { 467, 0,  11000 }, { 491, 0,  11000 }, { 529, 0,  11000 }, { 547, 0,  11000 }, { 418, 0,  10000 }, { 466, 0,  10000 },
    { 492, 0,  10000 }, { 527, 0,  10000 }, { 600, 0,  10000 }, { 401, 0,   9000 }, { 479, 0,   9000 }, { 542, 0,   9000 },
    { 543, 0,   9000 }, { 442, 0,   9000 }, { 436, 0,   8000 }, { 549, 0,   8000 }, { 404, 0,   7000 }, { 410, 0,   6000 },
    { 478, 0,   6000 }, { 444, 0, 120000 }, { 556, 0, 125000 }, { 557, 0, 125000 }, { 470, 0,  60000 }, { 495, 0,  40000 },
    { 508, 0,  22000 }, { 431, 0,  45000 }, { 437, 0,  50000 }, { 403, 0,  55000 }, { 514, 0,  55000 }, { 515, 0,  60000 },
    { 455, 0,  50000 }, { 456, 0,  30000 }, { 414, 0,  25000 }, { 498, 0,  25000 }, { 499, 0,  25000 }, { 609, 0,  25000 },
    { 578, 0,  35000 }, { 573, 0,  45000 }, { 406, 0,  80000 }, { 486, 0,  30000 }, { 524, 0,  40000 }, { 525, 0,  22000 },
    { 552, 0,  18000 }, { 588, 0,  15000 }, { 423, 0,  16000 }, { 532, 0,  40000 }, { 531, 0,   7000 }, { 574, 0,   8000 },
    { 530, 0,   6000 }, { 571, 0,   5000 }, { 457, 0,   4000 }, { 572, 0,   3000 },
    // Pedal and Metal
    { 522, 2,  65000 }, { 521, 2,  18000 }, { 461, 2,  15000 }, { 463, 2,  14000 }, { 586, 2,  13000 }, { 581, 2,  12000 },
    { 471, 2,   7000 }, { 468, 2,   6000 }, { 462, 2,   2500 }, { 448, 2,   2500 }, { 510, 2,    800 }, { 481, 2,    500 },
    { 509, 2,    300 },
    // Elitas Travel
    { 592, 3, 1800000 }, { 577, 3, 1500000 }, { 553, 3, 600000 }, { 519, 3, 500000 }, { 548, 3, 500000 }, { 417, 3, 450000 },
    { 563, 3, 350000 }, { 487, 3, 300000 }, { 488, 3, 280000 }, { 447, 3, 250000 }, { 511, 3, 220000 }, { 513, 3, 180000 },
    { 460, 3, 160000 }, { 469, 3, 150000 }, { 512, 3, 130000 }, { 593, 3, 120000 },
    // Docktease
    { 484, 4, 150000 }, { 454, 4, 120000 }, { 493, 4,  90000 }, { 446, 4,  85000 }, { 452, 4,  70000 }, { 539, 4,  70000 },
    { 595, 4,  60000 }, { 453, 4,  40000 }, { 472, 4,  30000 }, { 473, 4,   8000 },
};
#define HOTEL_VEH_COUNT ((int)(sizeof(g_HotelVeh) / sizeof(g_HotelVeh[0])))
inline int HotelVehPrice(int model) { for(int i = 0; i < HOTEL_VEH_COUNT; ++i) if(g_HotelVeh[i].model == model) return g_HotelVeh[i].price; return 0; }
inline bool RpWeaponIsHarmless(int w) { return w == 0 || w == 14 || w == 15 || w == 43 || w == 46 || w == 10 || w == 11 || w == 12 || w == 13; }

// ---- emotes / dances (names from rpemotes, GTA SA animations) ----------------------------------
struct RpEmote { const char* cmd; const char* label; const char* file; const char* anim; uint8_t cat; uint16_t ms; };   // ms 0 = until /e c or you move
enum eRpEmoteCat : uint8_t { RPE_DANCE = 0, RPE_SOCIAL, RPE_POSE, RPE_WORK, RPE_SHARED };
static const char* const g_RpEmoteCats[] = { "Dances", "Social", "Poses", "Work", "Shared (with a player)" };
static const RpEmote g_RpEmotes[] =
{
    { "dance",    "Dance",            "DANCING",    "dnce_M_a",         RPE_DANCE, 0 }, { "dance2",  "Dance 2",          "DANCING",    "dnce_M_b",        RPE_DANCE, 0 },
    { "dance3",   "Dance 3",          "DANCING",    "dnce_M_c",         RPE_DANCE, 0 }, { "dance4",  "Dance 4",          "DANCING",    "dnce_M_d",        RPE_DANCE, 0 },
    { "dance5",   "Dance 5",          "DANCING",    "dnce_M_e",         RPE_DANCE, 0 }, { "dance6",  "Club loop",        "DANCING",    "DAN_Loop_A",      RPE_DANCE, 0 },
    { "dance7",   "Club left",        "DANCING",    "DAN_Left_A",       RPE_DANCE, 0 }, { "dance8",  "Club right",       "DANCING",    "DAN_Right_A",     RPE_DANCE, 0 },
    { "dance9",   "Club up",          "DANCING",    "DAN_Up_A",         RPE_DANCE, 0 }, { "dancef",  "Club down",        "DANCING",    "DAN_Down_A",      RPE_DANCE, 0 },
    { "danceclub","Dance club",       "DANCING",    "dance_loop",       RPE_DANCE, 0 }, { "boxdance","Clap dance",       "DANCING",    "bd_clap",         RPE_DANCE, 0 },
    { "dancesilly","Silly dance",     "DANCING",    "bd_clap1",         RPE_DANCE, 0 }, { "dancehiphop","Hip hop",       "RAPPING",    "RAP_A_Loop",      RPE_DANCE, 0 },
    { "dancehiphop2","Hip hop 2",     "RAPPING",    "RAP_B_Loop",       RPE_DANCE, 0 }, { "dancehiphop3","Hip hop 3",    "RAPPING",    "RAP_C_Loop",      RPE_DANCE, 0 },
    { "wave",     "Wave",             "ON_LOOKERS", "wave_loop",        RPE_SOCIAL, 0 }, { "wave2",  "Wave 2",           "KISSING",    "gfwave2",         RPE_SOCIAL, 2500 },
    { "point",    "Point",            "ON_LOOKERS", "point_loop",       RPE_SOCIAL, 0 }, { "cheer",  "Cheer",            "ON_LOOKERS", "shout_01",        RPE_SOCIAL, 0 },
    { "clap",     "Clap",             "DANCING",    "bd_clap",          RPE_SOCIAL, 0 }, { "laugh",  "Laugh",            "RAPPING",    "Laugh_01",        RPE_SOCIAL, 3000 },
    { "chat",     "Chat",             "PED",        "IDLE_CHAT",        RPE_SOCIAL, 0 }, { "facepalm","Shake head",      "MISC",       "plyr_shkhead",    RPE_SOCIAL, 2500 },
    { "angry",    "Angry",            "RIOT",       "RIOT_ANGRY",       RPE_SOCIAL, 0 }, { "chant",  "Chant",            "RIOT",       "RIOT_CHANT",      RPE_SOCIAL, 0 },
    { "gsign",    "Hand sign",        "GHANDS",     "gsign1",           RPE_SOCIAL, 2500 }, { "gsign2","Hand sign 2",    "GHANDS",     "gsign2",          RPE_SOCIAL, 2500 },
    { "win",      "Win",              "CASINO",     "manwinb",          RPE_SOCIAL, 3000 }, { "lose",  "Lose",           "OTB",        "wtchrace_lose",   RPE_SOCIAL, 3000 },
    { "handsup",  "Hands up",         "ROB_BANK",   "SHP_HandsUp_Scr",  RPE_POSE, 0 },  { "handsup2","Surrender",       "PED",        "handsup",         RPE_POSE, 0 },
    { "sit",      "Sit (ground)",     "BEACH",      "ParkSit_M_loop",   RPE_POSE, 0 },  { "sit2",   "Sit 2",            "BEACH",      "ParkSit_W_loop",  RPE_POSE, 0 },
    { "sit3",     "Sit (chair)",      "PED",        "SEAT_idle",        RPE_POSE, 0 },  { "sit4",   "Sit and wait",     "BEACH",      "SitnWait_loop_W", RPE_POSE, 0 },
    { "lay",      "Lay down",         "BEACH",      "bather",           RPE_POSE, 0 },  { "sunbathe","Sunbathe",        "BEACH",      "Lay_Bac_Loop",    RPE_POSE, 0 },
    { "sleep",    "Sleep",            "CRACK",      "crckidle2",        RPE_POSE, 0 },  { "lean",   "Lean",             "GANGS",      "leanIDLE",        RPE_POSE, 0 },
    { "lean2",    "Lean 2",           "MISC",       "Plyrlean_loop",    RPE_POSE, 0 },  { "smoke",  "Smoke",            "SMOKING",    "M_smklean_loop",  RPE_POSE, 0 },
    { "smoke2",   "Smoke 2",          "SMOKING",    "F_smklean_loop",   RPE_POSE, 0 },  { "cross",  "Cross arms",       "COP_AMBIENT","Coplook_loop",    RPE_POSE, 0 },
    { "think",    "Think",            "COP_AMBIENT","Coplook_think",    RPE_POSE, 0 },  { "guard",  "Guard",            "COP_AMBIENT","Coplook_loop",    RPE_POSE, 0 },
    { "yoga",     "Tai chi",          "PARK",       "Tai_Chi_Loop",     RPE_POSE, 0 },  { "cower",  "Cower",            "PED",        "cower",           RPE_POSE, 0 },
    { "lookout",  "Look out",         "SHOP",       "ROB_Shifty",       RPE_POSE, 3000 }, { "tired", "Tired",           "PED",        "IDLE_tired",      RPE_POSE, 0 },
    { "kneel",    "Kneel",            "CAMERA",     "camcrch_idleloop", RPE_POSE, 0 },  { "pray",   "Pray",             "GRAVEYARD",  "prst_loopa",      RPE_POSE, 0 },
    { "mourn",    "Mourn",            "GRAVEYARD",  "mrnF_loop",        RPE_POSE, 0 },  { "injured","Injured",          "SWEET",      "Sweet_injuredloop",RPE_POSE, 0 },
    { "drink",    "Drink",            "BAR",        "dnk_stndM_loop",   RPE_WORK, 0 },  { "eat",    "Eat",              "FOOD",       "EAT_Burger",      RPE_WORK, 4000 },
    { "cop",      "Traffic stop",     "POLICE",     "CopTraf_Stop",     RPE_WORK, 0 },  { "cop2",   "Traffic come",     "POLICE",     "CopTraf_Come",    RPE_WORK, 0 },
    { "cop3",     "Traffic left",     "POLICE",     "CopTraf_Left",     RPE_WORK, 0 },  { "notepad","Notepad",          "COP_AMBIENT","Copbrowse_loop",  RPE_WORK, 0 },
    { "cpr",      "CPR",              "MEDIC",      "CPR",              RPE_WORK, 0 },  { "mechanic","Fix car",         "CAR",        "Fixn_Car_Loop",   RPE_WORK, 0 },
    { "camera",   "Camera",           "CAMERA",     "camstnd_cmon",     RPE_WORK, 0 },  { "selfie", "Take photo",       "CAMERA",     "picstnd_take",    RPE_WORK, 2500 },
    { "bartender","Bartender",        "BAR",        "Barserve_loop",    RPE_WORK, 0 },  { "deal",   "Deal",             "DEALER",     "DEALER_IDLE",     RPE_WORK, 0 },
    { "carry",    "Carry box",        "CARRY",      "crry_prtial",      RPE_WORK, 0 },  { "bomb",   "Crouch work",      "BOMBER",     "BOM_Plant",       RPE_WORK, 3000 },
    { "handshake","Handshake",        "GANGS",      "hndshkfa",         RPE_SHARED, 3000 }, { "handshake2","Bro handshake","GANGS",   "hndshkba",        RPE_SHARED, 3000 },
    { "hug",      "Hug",              "GANGS",      "hndshkca",         RPE_SHARED, 3000 }, { "bro",   "Bro",              "GANGS",      "hndshkcb",        RPE_SHARED, 3000 },
    { "kiss",     "Kiss",             "KISSING",    "Playa_Kiss_02",    RPE_SHARED, 4000 }, { "give",  "Give",             "DEALER",     "DEALER_DEAL",     RPE_SHARED, 3000 },
};
#define RP_EMOTE_COUNT ((int)(sizeof(g_RpEmotes) / sizeof(g_RpEmotes[0])))
inline int RpFindEmote(const char* c) { if(!c || !c[0]) return -1; for(int i = 0; i < RP_EMOTE_COUNT; ++i) if(!strcasecmp(c, g_RpEmotes[i].cmd)) return i; return -1; }

// ---- commands (client -> server) ---------------------------------------------------------------
enum eRpCmd : uint8_t
{
    RPC_SPAWNED = 1, RPC_CHAT, RPC_PAY, RPC_DEPOSIT, RPC_WITHDRAW, RPC_TRANSFER, RPC_BUY, RPC_JOIN_JOB, RPC_QUIT_JOB, RPC_WORK,
    RPC_STOP_WORK, RPC_ARRIVE, RPC_DUTY, RPC_SMS, RPC_CALL, RPC_ANSWER, RPC_HANGUP, RPC_FEED, RPC_AD, RPC_BUY_HOUSE,
    RPC_SELL_HOUSE, RPC_HOME, RPC_MY_VEH, RPC_SELL_VEH, RPC_PET, RPC_CUFF, RPC_UNCUFF, RPC_ARREST, RPC_FINE, RPC_WANTED,
    RPC_HEAL, RPC_REPAIR, RPC_LOTTO, RPC_EAT_DONE, RPC_911, RPC_TAXI_CALL, RPC_SHARED_EMOTE, RPC_ACCEPT, RPC_STATS, RPC_RESPAWN,
    RPC_COINFLIP, RPC_DICE, RPC_EVENT_JOIN, RPC_PROXY, RPC_GIVE_ITEM, RPC_DEFEND, RPC_FRISK, RPC_JETPACK, RPC_SKIN_SET, RPC_FACTION,
    RPC_INV_REQ, RPC_INV_USE, RPC_INV_GIVE, RPC_INV_DROP,   // 3.26: the bag (a = bag slot, b = player id, val = amount)
    // 3.29 (Diverse Roleplay features): a = plan, b = 0 buy / 1 sell, val = $ (sell: 0 = all)
    RPC_INVEST, RPC_PROPOSE, RPC_DIVORCE, RPC_BELT, RPC_CHECK_BELT, RPC_PAWN, RPC_INVEST_INFO,
    RPC_INV_PICKUP,   // 3.30: pick up a dropped item (val = drop uid)
};
// 3.29: investments - one city market, prices move every RP_MARKET_S seconds
enum eRpPlan : uint8_t { RPN_BOND = 0, RPN_STOCKS, RPN_COIN, RPN_COUNT };
#define RP_MARKET_S 300
struct RpPlanDef { const char* name; const char* risk; int16_t lo, hi; uint8_t feePct; };   // per tick move in 0.1 %
static const RpPlanDef g_RpPlans[RPN_COUNT] =
{
    { "Mega Vil Bond",     "safe: +0.2% every 5 min",        2,   2, 0 },
    { "Holo-Tech Stocks",  "medium: -3% .. +4% every 5 min", -30, 40, 2 },
    { "VilCoin (crypto)",  "high: -9% .. +11% every 5 min",  -90, 110, 3 },
};

// chat kinds for RPC_CHAT / RPC_PROXY (a)
enum eRpChat : uint8_t { RPH_SAY = 0, RPH_ME, RPH_DO, RPH_AME, RPH_TRY, RPH_B, RPH_OOC, RPH_SHOUT, RPH_LOW, RPH_WHISPER, RPH_NEWBIE, RPH_MEGAPHONE, RPH_RADIO, RPH_DEPT, RPH_GOV };

// ---- city events -------------------------------------------------------------------------------
enum eRpEventKind : uint8_t { RPEV_NONE = 0, RPEV_DOUBLE_PAY, RPEV_CASH_DROP, RPEV_FESTIVAL, RPEV_LOTTERY, RPEV_HOLO_RUSH, RPEV_COUNT };
static const char* const g_RpEventNames[RPEV_COUNT] = { "-", "Double Pay Hour", "Holo Cash Drop", "Neon Night Festival", "Mega Lottery Draw", "Holo Tower Rush" };

// ---- 3.26: the bag (inventory) -----------------------------------------------------------------
// Server authoritative like the money: the phone only asks (RPC_INV_*), the host answers with
// PKT_RP_INV (the whole bag) and RPV_GOT ("Received 10x" cards). 5 x 3 slots, 50.0 kg.
// Icons: rp_items.png (4 x 4 cells of 128 px, built into the .so, written into files/MP when missing).
#define RP_INV_MAX_W   500          // 0.1 kg units: 50.0 kg
#define RP_INV_STACK   99
enum eRpInvId : uint8_t
{
    RPX_NONE = 0, RPX_BACKPACK, RPX_ESTEH, RPX_NASIGORENG, RPX_WATER, RPX_BURGER, RPX_PIZZA, RPX_HOTDOG, RPX_CHICKEN,
    RPX_SUSHI, RPX_DONUT, RPX_SODA, RPX_ENERGY, RPX_COFFEE, RPX_MEDKIT, RPX_REPAIRKIT, RPX_PHONE, RPX_COUNT
};
enum eRpInvUse : uint8_t { RPU_NONE = 0, RPU_FOOD, RPU_DRINK, RPU_MEDKIT, RPU_REPAIR, RPU_PHONE, RPU_STARTER };
struct RpInvDef { const char* name; uint8_t icon; uint8_t weight; uint8_t use; uint8_t hunger, thirst; };   // weight in 0.1 kg
static const RpInvDef g_RpInv[RPX_COUNT] =
{
    { "-",              0,  0, RPU_NONE,     0,   0 },
    { "Backpack",       0,  5, RPU_STARTER,  0,   0 },   // the starter pack: open it = the things inside
    { "Iced Tea",       1,  3, RPU_DRINK,    0,  45 },
    { "Fried Rice",     2,  4, RPU_FOOD,    50,   0 },
    { "Water",          3,  5, RPU_DRINK,    0,  40 },
    { "Burger",         4,  3, RPU_FOOD,    30,   0 },
    { "Pizza Slice",    5,  2, RPU_FOOD,    40,   0 },
    { "Hot Dog",        6,  2, RPU_FOOD,    20,   0 },
    { "Fried Chicken",  7,  3, RPU_FOOD,    55,   0 },
    { "Sushi Box",      8,  4, RPU_FOOD,    80,  10 },
    { "Doughnut",       9,  1, RPU_FOOD,    12,   0 },
    { "Sprunk",        10,  4, RPU_DRINK,    0,  50 },
    { "Energy Drink",  11,  4, RPU_DRINK,    5,  60 },
    { "Coffee",        12,  3, RPU_DRINK,    0,  35 },
    { "Medkit",        13, 10, RPU_MEDKIT,  60,   0 },   // hunger field = health
    { "Repair Kit",    14, 25, RPU_REPAIR,   0,   0 },
    { "Smartphone",    15,  2, RPU_PHONE,    0,   0 },
};
// 3.30: the world object for a dropped item (GTA SA models: a real phone, burger, pizza, can...)
#define RP_DROP_RANGE   2.2f       // walk this close to pick a drop up (m)
#define RP_DROP_LIFE_S  900        // a drop stays on the ground 15 minutes
static const uint16_t g_RpDropModel[RPX_COUNT] = {
    1210,   // -
    3026,   // backpack: para_pack
    2601,   // iced tea: a can (CJ_JUICE_CAN)
    2814,   // fried rice: takeaway box
    2601,   // water
    2703,   // burger (CJ_BURG_1)
    2702,   // pizza slice (CJ_PIZZA_1)
    2814,   // hot dog
    2814,   // fried chicken
    2814,   // sushi box
    2702,   // doughnut
    2601,   // sprunk
    2601,   // energy drink
    2601,   // coffee
    1240,   // medkit: the health heart
    1271,   // repair kit: a box
    330,    // smartphone: the real cellphone model
};
// 3.29: /pawn - what a pawn shop (24/7, tech shop) pays for one of each bag item
static const int16_t g_RpPawn[RPX_COUNT] = { 0, 0, 3, 6, 2, 5, 4, 3, 7, 12, 1, 3, 5, 3, 60, 90, 150 };
// the starter backpack (10x Iced Tea + 10x Fried Rice, and a smartphone; 3.30: English names)
struct RpInvPack { uint8_t id; uint16_t count; };
static const RpInvPack g_RpStarter[] = { { RPX_ESTEH, 10 }, { RPX_NASIGORENG, 10 }, { RPX_PHONE, 1 } };
// a shop row that goes into the bag instead of being used on the spot (RPX_NONE = used right away)
inline uint8_t RpInvForShop(const char* name)
{
    static const struct { const char* n; uint8_t id; } k[] =
    {
        { "Burger", RPX_BURGER }, { "Pizza slice", RPX_PIZZA }, { "Hot dog", RPX_HOTDOG }, { "Fried chicken", RPX_CHICKEN },
        { "Synth sushi box", RPX_SUSHI }, { "Doughnut", RPX_DONUT }, { "Water bottle", RPX_WATER }, { "Sprunk soda", RPX_SODA },
        { "Neon energy drink", RPX_ENERGY }, { "Coffee", RPX_COFFEE }, { "Medkit (+60 health)", RPX_MEDKIT }, { "Car repair kit", RPX_REPAIRKIT },
        { "Iced Tea", RPX_ESTEH }, { "Fried Rice", RPX_NASIGORENG },
    };
    if(!name) return RPX_NONE;
    for(const auto& e : k) if(!strcmp(e.n, name)) return e.id;
    return RPX_NONE;
}
inline int RpInvWeight(const RpInvSlot* s)
{
    int w = 0; for(int i = 0; i < RP_INV_SLOTS; ++i) if(s[i].id && s[i].id < RPX_COUNT) w += g_RpInv[s[i].id].weight * s[i].count;
    return w;
}
// how many of `id` still fit (weight + a free / same slot)
inline int RpInvRoom(const RpInvSlot* s, uint8_t id)
{
    if(!id || id >= RPX_COUNT) return 0;
    int w = g_RpInv[id].weight; int byW = w ? (RP_INV_MAX_W - RpInvWeight(s)) / w : 9999; if(byW < 0) byW = 0;
    int bySlot = 0;
    for(int i = 0; i < RP_INV_SLOTS; ++i) { if(s[i].id == id) bySlot += RP_INV_STACK - s[i].count; else if(!s[i].id) bySlot += RP_INV_STACK; }
    return byW < bySlot ? byW : bySlot;
}
// adds up to n, returns how many went in
inline int RpInvAdd(RpInvSlot* s, uint8_t id, int n)
{
    int room = RpInvRoom(s, id); if(n > room) n = room; if(n <= 0) return 0;
    int left = n;
    for(int i = 0; i < RP_INV_SLOTS && left; ++i) if(s[i].id == id && s[i].count < RP_INV_STACK) { int k = RP_INV_STACK - s[i].count; if(k > left) k = left; s[i].count = (uint16_t)(s[i].count + k); left -= k; }
    for(int i = 0; i < RP_INV_SLOTS && left; ++i) if(!s[i].id) { int k = left > RP_INV_STACK ? RP_INV_STACK : left; s[i].id = id; s[i].count = (uint16_t)k; left -= k; }
    return n - left;
}
inline int RpInvTake(RpInvSlot* s, int slot, int n)
{
    if(slot < 0 || slot >= RP_INV_SLOTS || !s[slot].id || n <= 0) return 0;
    if(n > s[slot].count) n = s[slot].count;
    s[slot].count = (uint16_t)(s[slot].count - n); if(!s[slot].count) s[slot].id = 0;
    return n;
}
