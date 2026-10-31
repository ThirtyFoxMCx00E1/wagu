// 3.39: Home Kit gamemode (GM_HOME) - shared by the server (server_home.inc) and the phone (mp_home.inc).
//
// A house = a slot (W x H tiles of HK_TILE metres, 1x1 .. 16x16) somewhere on the map, an owner, a price,
// an optional prefab building (a real GTA SA building .dff), objects placed in the slot (walls, doors,
// furniture - real GTA SA models) and an optional real GTA SA interior with its own furniture.
// The server keeps every house (homes file) and sends them to the phones; the phones stream the objects.
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "protocol.h"

#define HK_TILE          2.0f    // one slot square = 2 x 2 m (16x16 = 32 x 32 m)
#define HK_SLOT_MAX      16      // biggest slot: 16 x 16 squares
#define HK_HEIGHT_MAX    16.0f   // objects up to 16 m above the slot ground
#define HK_MAX_HOMES     256
#define HK_MAX_OBJ       150     // objects per house (outside + inside)
#define HK_PER_PLAYER    3       // houses one player can own (admin / moderator / host: no limit)
#define HK_OBJ_PAGE      40
#define HK_INDEX_PAGE    120
#define HK_NAME_LEN      24
#define HK_OWNER_LEN     40
#define HK_KEYS          4       // players with a key (enter / edit furniture)

enum eHomeFlags : uint8_t { HKF_SALE = 1, HKF_LOCKED = 2, HKF_SETUP = 4, HKF_RP = 8 };
enum eHomeObjFlags : uint8_t { HKO_INTERIOR = 1 };

// C->S commands (PktHomeCmd.cmd)
enum eHomeCmd : uint8_t
{
    HC_INDEX = 1,     // send me the index again
    HC_GET,           // id: send me this house
    HC_CREATE,        // text = name, pos = where (x, y, z, heading)
    HC_SETUP,         // id, w, h, pos = slot origin corner + heading, val = prefab model (0 = none)
    HC_BUY,           // id, val = price the phone paid (custom servers: GTA cash; Mega Vil: RP cash on the server)
    HC_SELL,          // id, val = price (0 = not for sale any more)
    HC_DELETE,        // id
    HC_RENAME,        // id, text
    HC_LOCK,          // id, val 0/1
    HC_OBJ_ADD,       // id, obj
    HC_OBJ_SET,       // id, objIdx, obj
    HC_OBJ_DEL,       // id, objIdx
    HC_OBJ_CLEAR,     // id, val = 0 outside, 1 inside, 2 all
    HC_ENTRANCE,      // id, pos = the door marker
    HC_INTERIOR,      // id, val = interior preset index (0 = none)
    HC_GIVE,          // id, val = player id (new owner)
    HC_KEY,           // id, val = player id (give / take a key)
    HC_PREFAB,        // id, val = prefab model (0 = none)
};

#pragma pack(push, 1)
struct HkObj { uint16_t model; int16_t x, y, z; int16_t rx, ry, rz; uint8_t flags; };   // cm from the house origin (inside: from the interior anchor), degrees * 10
struct PktHomeCmd
{
    uint8_t  type, cmd; uint16_t id;
    int32_t  val; uint16_t objIdx; uint8_t w, h;
    float    pos[4];
    HkObj    obj;
    char     text[HK_NAME_LEN];
};
struct PktHomeHead
{
    uint8_t  type, op; uint16_t id, rev;
    uint8_t  w, h, flags, interior;
    float    x, y, z, heading;          // slot origin corner (squares grow along +x / +y turned by heading)
    int32_t  price;
    uint16_t prefab, nObj;
    int16_t  ex, ey, ez;                // the door marker, cm from the origin
    char     name[HK_NAME_LEN];
    char     ownerName[MP_NAME_LEN];
    char     owner[HK_OWNER_LEN];       // "a:<account>" or "u:<phone id>" ("" = nobody / the server)
    char     keys[HK_KEYS][MP_NAME_LEN];
};
struct PktHomeObjs  { uint8_t type, op; uint16_t id, rev, start; uint8_t count; HkObj o[HK_OBJ_PAGE]; };
struct PktHomeIndex { uint8_t type, op; uint16_t page, count, total; uint16_t e[HK_INDEX_PAGE * 2]; };   // id, rev pairs
struct PktHomeDel   { uint8_t type, op; uint16_t id; };
struct PktHomeMsg   { uint8_t type, ok; uint16_t id; int32_t cash; char text[120]; };   // cash: GTA money to add (house sold)
// 3.39: live server settings: S->C what is on (+ can = may change it), C->S a change (owner / admin / moderator)
// 3.39.1: saved progress. op 1 = C->S "save this", op 0 = S->C "here is yours", op 2 = S->C "nothing saved yet"
struct PktProgress
{
    uint8_t  type, op; uint16_t ver;
    float    x, y, z, heading;
    int32_t  money;
    uint8_t  interior, health, armour, hunger, energy, wanted; int16_t skin;
    uint8_t  wep[13]; uint8_t pad2[3]; uint16_t ammo[13]; uint16_t pad3;
};
struct PktSrvSettings { uint8_t type, op, perms, can; uint16_t modes; uint8_t rulesOn, pad; };
// 3.39: rules: idx 0 = title, 1..10 = the lines; on = 0 -> no rules window
struct PktRules     { uint8_t type, idx, on, count; char text[100]; };
// 3.39: the new gamemodes (CnR / A-D / survival / stunt / drift): one small relayed event.
// C->S: to = a player id or 0xFF (everyone); the server fills in from and passes it on.
enum eGmOp : uint8_t { GMO_TEAM = 1, GMO_WANTED, GMO_ARREST, GMO_ROB, GMO_HIT, GMO_AD_ROUND, GMO_AD_CAP, GMO_SCORE, GMO_NOTICE, GMO_CUFF, GMO_VOTE };
struct PktGmEvent   { uint8_t type, op, from, to; uint16_t mode; int16_t a, b; int32_t val; char text[48]; };
#pragma pack(pop)

// ---- real GTA SA models (all in the stock gta3.img - no SA-MP objects) ---------------------------
struct HkModel { uint16_t model; uint8_t cat; const char* name; };
enum eHomeCat : uint8_t { HKC_PREFAB = 0, HKC_BUILD, HKC_DOOR, HKC_LIVING, HKC_BED, HKC_KITCHEN, HKC_BATH, HKC_LIGHT, HKC_GARDEN, HKC_FENCE, HKC_COUNT };
static const char* const g_HkCatNames[HKC_COUNT] = { "Buildings (prefab)", "Walls / floors / stairs", "Doors", "Living room", "Bedroom", "Kitchen / dining", "Bathroom", "Lights / TV", "Plants / garden", "Fences / gates" };
static const HkModel g_HkModels[] =
{
    // buildings: whole houses of the GTA SA map (.dff), used as the house shell
    { 3639, HKC_PREFAB, "Glen Park house" },      { 3648, HKC_PREFAB, "Gang house 2" },       { 3655, HKC_PREFAB, "Gang house 3" },
    { 3314, HKC_PREFAB, "Suburb house A" },       { 3315, HKC_PREFAB, "Suburb house B" },     { 3316, HKC_PREFAB, "Suburb house C" },
    { 3317, HKC_PREFAB, "Suburb house D" },       { 5520, HKC_PREFAB, "Big Smoke style house" }, { 4886, HKC_PREFAB, "Gang spawn house" },
    { 5060, HKC_PREFAB, "Carl's safehouse" },     { 6284, HKC_PREFAB, "Santa Maria house 1" }, { 6285, HKC_PREFAB, "Santa Maria house 2" },
    { 6286, HKC_PREFAB, "Santa Maria house 3" },  { 9339, HKC_PREFAB, "San Fierro villa" },   { 18259, HKC_PREFAB, "Log cabin" },
    { 18267, HKC_PREFAB, "Log cabin (big)" },     { 12991, HKC_PREFAB, "Countryside shack" }, { 12932, HKC_PREFAB, "Trailer home" },
    { 12934, HKC_PREFAB, "Trailer home 2" },      { 12937, HKC_PREFAB, "Cat shack" },         { 1452, HKC_PREFAB, "Outhouse" },
    // building pieces
    { 3095, HKC_BUILD, "Big metal plate (floor / roof 9x9)" }, { 3280, HKC_BUILD, "Metal panel (wall)" },   { 3260, HKC_BUILD, "Old wood panel (wall)" },
    { 2937, HKC_BUILD, "Wooden plank" },          { 3406, HKC_BUILD, "Wooden jetty (deck)" }, { 3361, HKC_BUILD, "Wooden stairs" },
    { 3399, HKC_BUILD, "Metal stairs" },          { 2395, HKC_BUILD, "Sports wall panel" },   { 1697, HKC_BUILD, "Roof solar panel" },
    { 3934, HKC_BUILD, "Helipad" },               { 8171, HKC_BUILD, "Big concrete ground" }, { 1649, HKC_BUILD, "Glass window" },
    // doors
    { 1491, HKC_DOOR, "Inside door 1" },          { 1492, HKC_DOOR, "Inside door 2" },        { 1494, HKC_DOOR, "Inside door 3" },
    { 1495, HKC_DOOR, "Front door 1" },           { 1497, HKC_DOOR, "Front door 2" },         { 1498, HKC_DOOR, "Front door 3" },
    { 1500, HKC_DOOR, "Front door 5" },           { 1501, HKC_DOOR, "Front door 4" },         { 1502, HKC_DOOR, "Inside door 4" },
    { 1535, HKC_DOOR, "Front door 14" },          { 1557, HKC_DOOR, "Double door" },          { 1508, HKC_DOOR, "Garage door" },
    { 13187, HKC_DOOR, "Suburb garage door" },    { 17951, HKC_DOOR, "CJ garage door" },
    // living room
    { 1702, HKC_LIVING, "Couch 6" },              { 1703, HKC_LIVING, "Couch 2" },            { 1706, HKC_LIVING, "Couch 3" },
    { 1709, HKC_LIVING, "Corner couch 8" },       { 1712, HKC_LIVING, "Couch 5" },            { 1713, HKC_LIVING, "Couch 4" },
    { 1753, HKC_LIVING, "Swank couch" },          { 1761, HKC_LIVING, "Swank couch 2" },      { 1704, HKC_LIVING, "Armchair 3" },
    { 1705, HKC_LIVING, "Armchair 4" },           { 1708, HKC_LIVING, "Armchair 2" },         { 1711, HKC_LIVING, "Armchair 1" },
    { 1714, HKC_LIVING, "Office chair" },         { 1814, HKC_LIVING, "Coffee table" },       { 1815, HKC_LIVING, "Coffee table 2" },
    { 1817, HKC_LIVING, "Coffee table 3" },       { 1818, HKC_LIVING, "Swank coffee table" }, { 1742, HKC_LIVING, "Bookshelf" },
    { 1743, HKC_LIVING, "Cabinet" },              { 1740, HKC_LIVING, "Low cabinet" },        { 2001, HKC_LIVING, "Office plant" },
    { 1738, HKC_LIVING, "Radiator" },             { 2100, HKC_LIVING, "Hi-fi" },
    // bedroom
    { 1700, HKC_BED, "Double bed" },              { 1701, HKC_BED, "Double bed 2" },          { 1745, HKC_BED, "Bed 3" },
    { 1771, HKC_BED, "Bunk bed" },                { 1794, HKC_BED, "Low bed 3" },             { 1795, HKC_BED, "Swank bed 2" },
    { 1796, HKC_BED, "Low bed 1" },               { 1797, HKC_BED, "Low bed 2" },             { 1798, HKC_BED, "Swank bed 1" },
    { 1799, HKC_BED, "Bed 4" },                   { 1801, HKC_BED, "Swank bed 4" },           { 1803, HKC_BED, "Swank bed 3" },
    { 2087, HKC_BED, "Wardrobe" },                { 2088, HKC_BED, "Wardrobe 2" },            { 2094, HKC_BED, "Dresser" },
    // kitchen / dining
    { 2131, HKC_KITCHEN, "Fridge" },              { 2132, HKC_KITCHEN, "Kitchen sink" },      { 2133, HKC_KITCHEN, "Kitchen unit R" },
    { 2134, HKC_KITCHEN, "Kitchen unit M" },      { 2135, HKC_KITCHEN, "Cooker" },            { 2136, HKC_KITCHEN, "Sink 3" },
    { 2147, HKC_KITCHEN, "Fridge 2" },            { 2149, HKC_KITCHEN, "Microwave" },         { 2417, HKC_KITCHEN, "Fast food cooker" },
    { 2115, HKC_KITCHEN, "Dining table" },        { 2117, HKC_KITCHEN, "Dining table 3" },    { 2118, HKC_KITCHEN, "Swank dining table" },
    { 2120, HKC_KITCHEN, "Dining chair" },        { 2121, HKC_KITCHEN, "Dining chair 2" },    { 2124, HKC_KITCHEN, "Dining chair 4" },
    { 1739, HKC_KITCHEN, "Wooden chair" },
    // bathroom
    { 2514, HKC_BATH, "Toilet" },                 { 2521, HKC_BATH, "Toilet 2" },             { 2525, HKC_BATH, "Toilet 3" },
    { 2528, HKC_BATH, "Toilet 4" },               { 2516, HKC_BATH, "Bath" },                 { 2519, HKC_BATH, "Bath 2" },
    { 2522, HKC_BATH, "Bath 3" },                 { 2526, HKC_BATH, "Bath 4" },               { 2517, HKC_BATH, "Shower" },
    { 2520, HKC_BATH, "Shower 2" },               { 2527, HKC_BATH, "Shower 3" },             { 2515, HKC_BATH, "Sink" },
    { 2518, HKC_BATH, "Sink 2" },                 { 2523, HKC_BATH, "Sink 3" },               { 2524, HKC_BATH, "Sink 4" },
    // lights / TV
    { 1717, HKC_LIGHT, "TV (telly)" },            { 1747, HKC_LIGHT, "Low TV 2" },            { 1748, HKC_LIGHT, "Low TV 3" },
    { 1749, HKC_LIGHT, "TV 3" },                  { 1750, HKC_LIGHT, "TV 4" },                { 1752, HKC_LIGHT, "Swank TV" },
    { 1786, HKC_LIGHT, "Swank TV 4" },            { 1791, HKC_LIGHT, "Swank TV 1" },          { 1718, HKC_LIGHT, "Game console" },
    { 2069, HKC_LIGHT, "Floor lamp" },            { 2071, HKC_LIGHT, "Lamp 5" },              { 2072, HKC_LIGHT, "Lamp 4" },
    { 2073, HKC_LIGHT, "Ceiling lamp 3" },        { 2076, HKC_LIGHT, "Ceiling lamp" },
    // plants / garden
    { 948, HKC_GARDEN, "Plant pot 10" },          { 949, HKC_GARDEN, "Plant pot 4" },         { 950, HKC_GARDEN, "Plant pot 11" },
    { 2010, HKC_GARDEN, "Plant 3" },              { 2011, HKC_GARDEN, "Plant 2" },            { 2194, HKC_GARDEN, "Cactus" },
    { 2195, HKC_GARDEN, "Plant pot 3" },          { 2241, HKC_GARDEN, "Plant pot 5" },        { 2251, HKC_GARDEN, "Plant pot 20" },
    { 1255, HKC_GARDEN, "Sun lounger" },          { 1281, HKC_GARDEN, "Park table" },         { 1432, HKC_GARDEN, "Garden table" },
    { 1481, HKC_GARDEN, "Barbecue" },             { 1280, HKC_GARDEN, "Park bench" },
    // fences / gates
    { 1418, HKC_FENCE, "Wooden fence 3" },        { 1408, HKC_FENCE, "Wooden fence 2" },      { 1407, HKC_FENCE, "Rough wood fence" },
    { 1410, HKC_FENCE, "Rough wood fence 1b" },   { 1460, HKC_FENCE, "Rough wood fence 3" },  { 1411, HKC_FENCE, "Wire mesh fence" },
    { 1412, HKC_FENCE, "Wire mesh fence 2" },     { 1413, HKC_FENCE, "Wire mesh fence 3" },   { 970, HKC_FENCE, "Low fence" },
    { 983, HKC_FENCE, "Road fence" },             { 16501, HKC_FENCE, "Garden gate" },
};
#define HK_MODEL_COUNT ((int)(sizeof(g_HkModels) / sizeof(g_HkModels[0])))
static inline const HkModel* HkFindModel(int model) { for(int i = 0; i < HK_MODEL_COUNT; ++i) if(g_HkModels[i].model == model) return &g_HkModels[i]; return nullptr; }
static inline bool HkIsPrefab(int model) { const HkModel* m = HkFindModel(model); return m && m->cat == HKC_PREFAB; }

// ---- real GTA SA house interiors (the ones SA-MP house scripts use) ------------------------------
struct HkInterior { uint8_t area; float x, y, z, a; const char* name; };
static const HkInterior g_HkInteriors[] =
{
    { 0,  0.0f,     0.0f,     0.0f,    0.0f,   "None (furniture outside / in the prefab)" },
    { 1,  223.20f,  1287.08f, 1082.14f, 0.0f,  "Small house" },
    { 2,  2468.84f, -1698.24f, 1013.51f, 90.0f, "Ryder's house" },
    { 3,  2496.05f, -1692.08f, 1014.74f, 180.0f, "CJ's house (Grove Street)" },
    { 4,  -260.49f, 1456.75f, 1084.37f, 90.0f, "Country house" },
    { 5,  2233.64f, -1115.26f, 1050.88f, 0.0f, "Hotel room" },
    { 6,  2196.85f, -1204.25f, 1049.02f, 90.0f, "Big house" },
    { 7,  225.68f,  1021.45f, 1084.02f, 0.0f,  "Mansion" },
    { 8,  2365.31f, -1135.60f, 1050.88f, 0.0f, "Two floors house" },
    { 9,  2317.89f, -1026.76f, 1050.22f, 0.0f, "Modern house" },
    { 10, 2270.38f, -1210.35f, 1047.56f, 90.0f, "Cosy house" },
    { 12, 2324.53f, -1149.54f, 1050.71f, 0.0f, "Large house" },
    { 15, 295.04f,  1472.26f, 1080.26f, 0.0f,  "Penthouse" },
};
#define HK_INTERIOR_COUNT ((int)(sizeof(g_HkInteriors) / sizeof(g_HkInteriors[0])))

// local slot frame <-> world (origin corner + heading in degrees)
static inline void HkLocalToWorld(float ox, float oy, float hdg, float lx, float ly, float& wx, float& wy)
{
    const float a = hdg * 0.01745329f, c = cosf(a), s = sinf(a);
    wx = ox + lx * c - ly * s; wy = oy + lx * s + ly * c;
}
static inline void HkWorldToLocal(float ox, float oy, float hdg, float wx, float wy, float& lx, float& ly)
{
    const float a = hdg * 0.01745329f, c = cosf(a), s = sinf(a), dx = wx - ox, dy = wy - oy;
    lx = dx * c + dy * s; ly = -dx * s + dy * c;
}
static inline bool HkValidName(const char* n)
{
    size_t l = strlen(n); if(l < 2 || l >= HK_NAME_LEN) return false;
    for(size_t i = 0; i < l; ++i) { char c = n[i]; if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false; }
    return true;
}
