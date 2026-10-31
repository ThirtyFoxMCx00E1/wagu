// 2.21 (included by mp.cpp after mp_cmds.inc): games for the default + every custom server, like the
// big SA-MP freeroam servers: /spawn any vehicle, /w weapons, races with checkpoints + times, jobs
// (trucking, quarry, pizza, burger, taxi...), deathmatch zones with kill streaks, teleport events
// that everybody sees in the chat ("(/ls) Name teleported to Los Santos!"), a score bar, and the
// whole world opened up (all cities, shops, food, gyms, barbers, tattoo, clothes, Ammu-Nation,
// Pay 'n' Spray, mod garages, schools, safe houses, Emmet; girlfriends optional).

// ---- announce (everybody on the server sees it) ---------------------------------------------
#define COL_RACE   0xFFE14DFF
#define COL_DM     0xD25BFFFF
#define COL_TELE   0xFF6B6BFF
#define COL_JOB    0x4DD2FFFF
#define COL_EVT    0x7CFF6BFF

static void Announce(uint32_t col, const char* fmt, ...)
{
    char buf[MP_CHAT_LEN];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if(client.GetState() == NET_CONNECTED && myId != MP_INVALID_ID)
    {
        PktAnnounce a; memset(&a, 0, sizeof(a));
        a.type = PKT_ANNOUNCE; a.id = myId; a.color = col;
        MP_CopyStr(a.text, buf, sizeof(a.text));
        client.Send(&a, sizeof(a));   // the server sends it back to everyone (us too)
    }
    else MP::AddSystemMessage(col, buf);
}
static const char* MyName() { return settings.name[0] ? settings.name : "Player"; }

// ---- vehicles -------------------------------------------------------------------------------
static const char* const kVehNames[212] =
{
    "Landstalker","Bravura","Buffalo","Linerunner","Perennial","Sentinel","Dumper","Firetruck","Trashmaster","Stretch",
    "Manana","Infernus","Voodoo","Pony","Mule","Cheetah","Ambulance","Leviathan","Moonbeam","Esperanto",
    "Taxi","Washington","Bobcat","Mr Whoopee","BF Injection","Hunter","Premier","Enforcer","Securicar","Banshee",
    "Predator","Bus","Rhino","Barracks","Hotknife","Trailer 1","Previon","Coach","Cabbie","Stallion",
    "Rumpo","RC Bandit","Romero","Packer","Monster","Admiral","Squalo","Seasparrow","Pizzaboy","Tram",
    "Trailer 2","Turismo","Speeder","Reefer","Tropic","Flatbed","Yankee","Caddy","Solair","Berkley RC Van",
    "Skimmer","PCJ-600","Faggio","Freeway","RC Baron","RC Raider","Glendale","Oceanic","Sanchez","Sparrow",
    "Patriot","Quad","Coastguard","Dinghy","Hermes","Sabre","Rustler","ZR-350","Walton","Regina",
    "Comet","BMX","Burrito","Camper","Marquis","Baggage","Dozer","Maverick","News Chopper","Rancher",
    "FBI Rancher","Virgo","Greenwood","Jetmax","Hotring","Sandking","Blista Compact","Police Maverick","Boxville","Benson",
    "Mesa","RC Goblin","Hotring Racer A","Hotring Racer B","Bloodring Banger","Rancher Lure","Super GT","Elegant","Journey","Bike",
    "Mountain Bike","Beagle","Cropduster","Stunt","Tanker","Roadtrain","Nebula","Majestic","Buccaneer","Shamal",
    "Hydra","FCR-900","NRG-500","HPV1000","Cement Truck","Tow Truck","Fortune","Cadrona","FBI Truck","Willard",
    "Forklift","Tractor","Combine","Feltzer","Remington","Slamvan","Blade","Freight","Streak","Vortex",
    "Vincent","Bullet","Clover","Sadler","Firetruck LA","Hustler","Intruder","Primo","Cargobob","Tampa",
    "Sunrise","Merit","Utility","Nevada","Yosemite","Windsor","Monster A","Monster B","Uranus","Jester",
    "Sultan","Stratum","Elegy","Raindance","RC Tiger","Flash","Tahoma","Savanna","Bandito","Freight Flat",
    "Streak Carriage","Kart","Mower","Duneride","Sweeper","Broadway","Tornado","AT-400","DFT-30","Huntley",
    "Stafford","BF-400","Newsvan","Tug","Trailer 3","Emperor","Wayfarer","Euros","Hotdog","Club",
    "Freight Box","Article Trailer 3","Andromada","Dodo","RC Cam","Launch","Police LSPD","Police SFPD","Police LVPD","Police Ranger",
    "Picador","SWAT","Alpha","Phoenix","Glendale Shit","Sadler Shit","Baggage Trailer A","Baggage Trailer B","Tug Stairs","Boxville Black",
    "Farm Trailer","Utility Trailer",
};
static bool IsTrainOrBad(int m) { return m == 449 || m == 537 || m == 538 || m == 569 || m == 570 || m == 590 || m == 594 || MP_IsTrailerModel(m); }   // 2.37: + trailers (Tug Stairs flew)
static void Squash(const char* in, char* out, int n)   // lower case, letters + digits only
{
    int k = 0;
    for(; *in && k < n - 1; ++in) { char c = (char)tolower((unsigned char)*in); if(isalnum((unsigned char)c)) out[k++] = c; }
    out[k] = 0;
}
static int FindVehicle(const char* arg)
{
    if(!arg || !*arg) return -1;
    while(*arg == ' ' || *arg == ':') ++arg;
    if(isdigit((unsigned char)*arg)) { int id = atoi(arg); return (id >= 400 && id <= 611) ? id : -1; }
    char want[32]; Squash(arg, want, sizeof(want));
    if(!want[0]) return -1;
    int prefix = -1, sub = -1;
    for(int i = 0; i < 212; ++i)
    {
        char nm[32]; Squash(kVehNames[i], nm, sizeof(nm));
        if(!strcmp(nm, want)) return 400 + i;
        if(prefix < 0 && !strncmp(nm, want, strlen(want))) prefix = 400 + i;
        if(sub < 0 && strstr(nm, want)) sub = 400 + i;
    }
    return prefix >= 0 ? prefix : sub;
}

static int s_nSpawnedCar = 0;
static int SpawnVehicleForMe(int model, bool announce)
{
    int ped = Game::LocalPed();
    if(ped <= 0) return 0;
    Vec3 me; Game::GetCharPos(ped, me);
    float h = Game::GetCharHeading(ped);
    int old = s_nSpawnedCar; s_nSpawnedCar = 0;
    float side = 0.0f;
    if(Game::IsCharInAnyCar(ped))
    {
        int cur = Game::GetCarCharIsIn(ped);
        Vec3 out = me; out.z += 0.5f;
        Game::WarpOutOfCar(ped, out);
        if(cur > 0 && cur == old) { Game::DeleteCar(old); old = 0; }
        else side = 5.0f;   // someone else's / a street car stays: put the new one next to it
    }
    if(old > 0) { if(Game::GetDriverOfCar(old) <= 0) Game::DeleteCar(old); else Game::ReleaseCar(old); }
    float r = h * 0.0174533f;
    Vec3 at = { me.x - sinf(r) * 1.0f + cosf(r) * side, me.y + cosf(r) * 1.0f + sinf(r) * side, me.z + 0.6f };
    int car = Game::CreateCar(model, at, true);   // 3.32: your own /spawn: load it right now
    if(car <= 0) { SysMsg(COL_RED, "Could not spawn that vehicle here."); return 0; }
    Game::SetCarHeading(car, h);
    Game::WarpIntoCar(ped, car);
    s_nSpawnedCar = car;
    if(announce) SysMsg(COL_SYSTEM, "Spawned %s (%d).", kVehNames[model - 400], model);
    return car;
}
static void CmdSpawn(const char* arg)
{
    if(!Game::IsReady()) return;
    if(!arg || !*arg)
    {
        SysMsg(COL_SYSTEM, "/spawn [id or name]  e.g. /spawn 411  or  /spawn infernus  (400 - 611)");
        SysMsg(COL_SYSTEM, "Popular: 411 Infernus, 522 NRG-500, 560 Sultan, 541 Bullet, 451 Turismo, 487 Maverick, 520 Hydra, 432 Rhino, 556 Monster, 468 Sanchez");
        return;
    }
    int m = FindVehicle(arg);
    if(m < 0) { SysMsg(COL_RED, "No vehicle \"%s\". Use an id 400 - 611 or a name (/spawn infernus).", arg); return; }
    if(IsTrainOrBad(m)) { SysMsg(COL_RED, "%s (%d) can't be spawned (rail / camera vehicle or trailer).", kVehNames[m - 400], m); return; }
    SpawnVehicleForMe(m, true);
}

// ---- weapons -------------------------------------------------------------------------------
struct WeapName { int id; const char* name; };
static const WeapName kWeaps[] =
{
    { 1, "brassknuckles" }, { 2, "golfclub" }, { 3, "nightstick" }, { 4, "knife" }, { 5, "bat" }, { 6, "shovel" },
    { 7, "poolcue" }, { 8, "katana" }, { 9, "chainsaw" }, { 14, "flowers" }, { 15, "cane" }, { 16, "grenade" },
    { 17, "teargas" }, { 18, "molotov" }, { 22, "pistol" }, { 23, "silenced" }, { 24, "deagle" }, { 25, "shotgun" },
    { 26, "sawnoff" }, { 27, "spas" }, { 28, "uzi" }, { 29, "mp5" }, { 30, "ak47" }, { 31, "m4" }, { 32, "tec9" },
    { 33, "rifle" }, { 34, "sniper" }, { 35, "rpg" }, { 36, "heatseeker" }, { 37, "flamethrower" }, { 38, "minigun" },
    { 39, "satchel" }, { 41, "spraycan" }, { 42, "extinguisher" }, { 43, "camera" }, { 46, "parachute" },
};
#define MP_WEAP_COUNT ((int)(sizeof(kWeaps) / sizeof(kWeaps[0])))
static void CmdWeapon(const char* arg)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(!arg || !*arg)
    {
        std::string all;
        for(int i = 0; i < MP_WEAP_COUNT; ++i) { if(i) all += ", "; all += std::to_string(kWeaps[i].id) + " " + kWeaps[i].name; }
        SysMsg(COL_SYSTEM, "/w [id or name]  or  /w all  (pack: deagle, m4, spas, sniper, grenades)");
        SysMsg(COL_SYSTEM, "%s", all.substr(0, MP_CHAT_LEN - 8).c_str());
        return;
    }
    char w[24]; Squash(arg, w, sizeof(w));
    if(!strcmp(w, "all") || !strcmp(w, "pack"))
    {
        static const int pack[] = { 4, 16, 24, 27, 29, 31, 34 };
        for(int id : pack) Game::GiveAndSelectWeapon(ped, id);
        Game::GiveAndSelectWeapon(ped, 31);
        SysMsg(COL_SYSTEM, "Weapon pack given.");
        return;
    }
    int id = -1;
    if(isdigit((unsigned char)arg[0])) id = atoi(arg);
    else for(int i = 0; i < MP_WEAP_COUNT; ++i) if(!strncmp(kWeaps[i].name, w, strlen(w))) { id = kWeaps[i].id; break; }
    bool ok = false; for(int i = 0; i < MP_WEAP_COUNT; ++i) if(kWeaps[i].id == id) ok = true;
    if(!ok) { SysMsg(COL_RED, "No weapon \"%s\". Type /w for the list.", arg); return; }
    Game::GiveAndSelectWeapon(ped, id);
    for(int i = 0; i < MP_WEAP_COUNT; ++i) if(kWeaps[i].id == id) SysMsg(COL_SYSTEM, "Weapon: %s (%d).", kWeaps[i].name, id);
}

// ---- places: teleports everybody sees ("(/ls) Name teleported to Los Santos!") ------------------
struct GPlace { const char* cmd; const char* name; float x, y, z, h; };
static const GPlace kPlaces[] =
{
    // cities / big spots
    { "ls",          "Los Santos",                2495.0f, -1687.0f,  13.5f, 180.0f },
    { "sf",          "San Fierro",               -1956.1f,   287.1f,  35.5f,  90.0f },
    { "lv",          "Las Venturas",              2087.9f,  1516.5f,  10.8f,  48.9f },
    { "chiliad",     "Mount Chiliad",            -2318.0f, -1636.0f, 483.7f,   0.0f },
    { "lsairport",   "Los Santos Airport",        1682.0f, -2245.0f,  13.5f, 180.0f },
    { "sfairport",   "Easter Bay Airport",       -1427.1f,  -288.9f,  14.2f, 137.0f },
    { "lvairport",   "Las Venturas Airport",      1673.2f,  1316.1f,  10.8f, 177.7f },
    { "desert",      "Verdant Meadows Airstrip",   414.0f,  2533.0f,  16.6f,  90.0f },
    { "beach",       "Santa Maria Beach",          370.0f, -2030.0f,   7.7f,  90.0f },
    { "strip",       "The Strip",                 2040.0f,  1300.0f,  10.8f,   0.0f },
    { "dam",         "Sherman Dam",               -900.0f,  2000.0f,  60.9f,   0.0f },
    { "area51",      "Area 69 gate",               215.0f,  1860.0f,  17.6f,   0.0f },
    // services + shops (doors open with the open world)
    { "pizza",       "Well Stacked Pizza Co.",    2105.5f, -1806.6f,  13.6f,  90.0f },
    { "burger",      "Burger Shot",                810.5f, -1616.1f,  13.6f, 270.0f },
    { "cluckin",     "Cluckin' Bell",              928.9f, -1353.0f,  13.3f,  90.0f },
    { "barber",      "Reece's Barber Shop",       2070.6f, -1793.8f,  13.6f, 270.0f },
    { "tattoo",      "Idlewood Tattoo",           2068.6f, -1779.8f,  13.6f, 270.0f },
    { "gym",         "Ganton Gym",                2229.8f, -1721.2f,  13.6f, 135.0f },
    { "gymsf",       "Cobra Martial Arts",       -2270.0f,  -155.9f,  35.3f, 270.0f },
    { "gymlv",       "Below the Belt Gym",        1968.8f,  2295.9f,  16.5f, 180.0f },
    { "clothes",     "Binco",                     2244.3f, -1665.6f,  15.5f, 345.0f },
    { "prolaps",     "Pro-Laps",                   499.5f, -1360.6f,  16.4f, 340.0f },
    { "victim",      "Victim",                     461.7f, -1500.8f,  31.0f, 100.0f },
    { "ammu",        "Ammu-Nation",               1368.4f, -1279.1f,  13.5f,  90.0f },
    { "spray",       "Pay 'n' Spray",             2071.5f, -1831.4f,  13.5f,  90.0f },
    { "transfender", "TransFender",               1041.4f, -1036.0f,  32.0f,   0.0f },
    { "locolow",     "Loco Low Co.",              2644.6f, -2044.3f,  13.6f, 180.0f },
    { "waa",         "Wheel Arch Angels",        -2720.0f,   217.6f,   4.5f,  90.0f },
    { "school",      "Driving School",           -2047.0f,   -95.0f,  35.2f,   0.0f },
    { "flightschool","Flight School",              414.0f,  2533.0f,  16.6f,  90.0f },
    { "boatschool",  "Boat School",              -2187.0f,  2416.0f,   5.2f, 315.0f },
    { "truck",       "RS Haul (trucking)",         -76.5f, -1136.0f,   1.1f,  70.0f },
    { "quarry",      "Hunter Quarry",              600.0f,   860.0f, -40.0f,   0.0f },
    { "emmet",       "Emmet's",                   2447.0f, -1975.3f,  13.6f,   0.0f },
    { "home",        "CJ's safe house",           2495.3f, -1690.0f,  14.8f,   0.0f },
    { "gf",          "Denise's house",            2402.5f, -1714.8f,  13.6f,   0.0f },
    { "casino",      "Caligula's Casino",         2196.0f,  1677.0f,  12.4f,  90.0f },
};
#define MP_GPLACE_COUNT ((int)(sizeof(kPlaces) / sizeof(kPlaces[0])))

static bool InEvent();
static void GotoPlace(const GPlace& p, bool announce)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(InEvent()) { SysMsg(COL_YELLOW, "Leave the race / job / DM first (/leave)."); return; }
    Game::TeleportGround(ped, { p.x, p.y, p.z }, p.h);
    if(announce) Announce(COL_TELE, "(/%s) %s teleported to %s!", p.cmd, MyName(), p.name);
}

// ---- jobs (checkpoint routes) ---------------------------------------------------------------
struct GRoute
{
    const char* cmd; const char* name; int kind;   // RK_JOB
    int model; int reward; int n; Vec3 cp[10];
};
enum { RK_RACE = 0, RK_JOB = 1 };
static const GRoute kRoutes[] =
{
    { "trucking", "Trucking: RS Haul -> Las Venturas", RK_JOB, 515, 9000, 3, { {-76.5f,-1136.0f,1.1f}, {661.4f,-573.5f,16.3f}, {1673.2f,1316.1f,10.8f} } },
    { "quarry",   "Quarry: haul the rocks",            RK_JOB, 406, 6000, 4, { {600.0f,860.0f,-40.0f}, {700.0f,800.0f,-30.0f}, {500.0f,900.0f,-35.0f}, {-200.0f,1125.0f,19.7f} } },
    { "pizza",    "Pizza Delivery: Idlewood",          RK_JOB, 448, 2500, 4, { {2105.5f,-1806.6f,13.6f}, {2306.8f,-1675.4f,13.9f}, {2489.5f,-1957.9f,13.6f}, {2105.5f,-1806.6f,13.6f} } },
    { "burger",   "Burger Shot Delivery",              RK_JOB, 448, 2500, 4, { {810.5f,-1616.1f,13.6f}, {477.6f,-1496.6f,20.4f}, {255.5f,-1366.3f,53.1f}, {810.5f,-1616.1f,13.6f} } },
    { "cluckin",  "Cluckin' Bell Delivery",            RK_JOB, 448, 2500, 4, { {928.9f,-1353.0f,13.3f}, {1133.8f,-1272.2f,13.6f}, {1421.1f,-885.3f,50.7f}, {928.9f,-1353.0f,13.3f} } },
    { "taxi",     "Taxi: downtown fares",              RK_JOB, 420, 3500, 4, { {1508.7f,-1059.1f,25.1f}, {1219.1f,-1812.8f,16.6f}, {2215.5f,-2627.8f,13.6f}, {1654.7f,-1656.9f,13.5f} } },
    { "medic",    "Paramedic: hospital run",           RK_JOB, 416, 3500, 3, { {2015.4f,-1717.3f,13.6f}, {2770.5f,-1628.3f,12.2f}, {1172.0f,-1323.0f,15.4f} } },
    { "firetruck","Firefighter: Ganton call",          RK_JOB, 407, 3500, 3, { {1751.1f,-2106.5f,13.6f}, {2306.8f,-1675.4f,13.9f}, {2489.5f,-1957.9f,13.6f} } },
};
#define MP_ROUTE_COUNT ((int)(sizeof(kRoutes) / sizeof(kRoutes[0])))

// ---- 3.01: the GTA San Andreas races (real starts + checkpoints) -----------------------------
#include "mp_races_sa.inc"

// ---- deathmatch zones ------------------------------------------------------------------------
struct GDm { const char* cmd; const char* name; float x, y, z, r; int w[4]; };
static const GDm kDms[] =
{
    { "war",    "War Zone DM",          400.0f, 2510.0f, 16.5f, 160.0f, { 24, 25, 31, 34 } },
    { "grove",  "Grove Street DM",     2487.6f, -1667.4f, 13.4f, 110.0f, { 22, 26, 28, 30 } },
    { "airport","LV Airport DM",       1600.0f,  1450.0f, 10.8f, 220.0f, { 24, 27, 29, 31 } },
    { "minigun","Minigun Madness",     -200.0f,  1125.0f, 19.7f, 140.0f, { 38, 24,  0,  0 } },
    { "sniper", "Chiliad Sniper DM",  -2318.0f, -1636.0f, 483.7f, 150.0f, { 34, 24,  0,  0 } },
};
#define MP_DM_COUNT ((int)(sizeof(kDms) / sizeof(kDms[0])))

// ---- 3.01: derbies (last car standing) -------------------------------------------------------
// Blood Bowl = the stadium interior 15 (floor -1394.2, 987.6, 1024.0, the SA-MP interior list);
// the others are open-air arenas. You are out when your car is wrecked, you are out of it for
// 15 s (3.21, was 5), you leave the arena for 15 s or (sumo) fall off the top.
#define MP_DERBY_GRACE_MS 15000
struct GDerby { const char* cmd; const char* name; int model; int area; float x, y, z, arena, ring, fallZ; int reward; };
static const GDerby kDerbies[] =
{
    { "bloodbowl", "Blood Bowl Derby",        504, 15, -1394.2f,  987.6f, 1024.0f,  60.0f, 22.0f, 1010.0f, 8000 },
    { "monster",   "Monster Truck Derby",     556, 15, -1394.2f,  987.6f, 1024.0f,  60.0f, 22.0f, 1010.0f, 8000 },
    { "docks",     "Ocean Docks Derby",       504,  0,  2750.0f, -2450.0f,  13.6f,  70.0f, 28.0f, -20.0f, 6000 },
    { "desert",    "Verdant Meadows Derby",   495,  0,   370.0f,  2500.0f,  16.5f,  90.0f, 30.0f, -20.0f, 6000 },
    { "sumo",      "Chiliad Sumo",            444,  0, -2318.0f, -1636.0f, 483.7f,  60.0f, 10.0f, 470.0f, 9000 },
};
#define MP_DERBY_COUNT ((int)(sizeof(kDerbies) / sizeof(kDerbies[0])))

// ---- state ----------------------------------------------------------------------------------
enum { EV_NONE = 0, EV_ROUTE, EV_DM, EV_DERBY, EV_RACE };   // EV_ROUTE = job, EV_RACE = GTA SA race (3.01)
struct GEvState
{
    int      kind = EV_NONE;
    int      idx = -1;          // job / dm / derby / race index
    int      cp = 0;            // next checkpoint
    int      cpHandle = 0, blip = 0;
    int      car = 0;
    uint64_t goAt = 0, startMs = 0, outOfCarSince = 0, outOfZoneSince = 0;
    int      countdown = 0;     // last countdown number shown
    bool     wasDead = false;
    int      streak = 0;
    // 3.01
    int      slot = 0;          // start grid place
    int      rivals = 0;        // others at GO
    uint64_t lastSend = 0;
    float    dist = 0;          // to the next checkpoint
    Vec3     ret = { 0, 0, 0 }; float retH = 0; int retArea = 0;   // derby / stadium race: where I came from
};
static GEvState s_Ev;
static int s_nDmScore = 0, s_nRaceScore = 0, s_nJobScore = 0;
static uint32_t s_nBestMs[MP_ROUTE_COUNT] = {};
static uint32_t s_nRaceBestMs[MP_SA_RACES] = {};
static uint64_t s_nNextTip = 0, s_nNextUnlock = 0;
static int s_nTip = 0;
// 3.01: race queue (/race): the server starts a race when MP_RACE_PLAYERS players are waiting
struct RaceQueue { bool on = false; int count = 0, need = MP_RACE_PLAYERS; uint64_t lastSend = 0; };
static RaceQueue s_RQ;

static bool InEvent() { return s_Ev.kind != EV_NONE; }
static float GDist2D(const Vec3& a, const Vec3& b) { float x = a.x - b.x, y = a.y - b.y; return sqrtf(x * x + y * y); }
static float HeadingTo(const Vec3& a, const Vec3& b) { return atan2f(-(b.x - a.x), b.y - a.y) * 57.29578f; }
static void FmtTime(uint32_t ms, char* out, int n) { snprintf(out, n, "%u:%02u.%03u", ms / 60000, (ms / 1000) % 60, ms % 1000); }
static int EvKind() { return s_Ev.kind == EV_DERBY ? EVK_DERBY : s_Ev.kind == EV_DM ? EVK_DM : s_Ev.kind == EV_ROUTE ? EVK_JOB : EVK_RACE; }
// the checkpoint route of the job / race I'm in
static int  EvN()        { return s_Ev.kind == EV_RACE ? kSaRaces[s_Ev.idx].n : kRoutes[s_Ev.idx].n; }
static Vec3 EvCp(int i)  { return s_Ev.kind == EV_RACE ? kSaRaces[s_Ev.idx].cp[i] : kRoutes[s_Ev.idx].cp[i]; }
static bool EvAir()      { return s_Ev.kind == EV_RACE && kSaRaces[s_Ev.idx].air; }

static void ClearMarkers()
{
    if(s_Ev.cpHandle) { Game::DeleteRaceCheckpoint(s_Ev.cpHandle); s_Ev.cpHandle = 0; }
    if(s_Ev.blip) { Game::RemoveBlip(s_Ev.blip); s_Ev.blip = 0; }
}
static void ShowCheckpoint()
{
    ClearMarkers();
    const int n = EvN();
    if(s_Ev.cp >= n) return;
    const bool last = s_Ev.cp == n - 1, air = EvAir();
    Vec3 p = EvCp(s_Ev.cp);
    if(s_Ev.kind == EV_ROUTE) p.z = Game::GroundZ(p.x, p.y, p.z);   // job points are rough; race points are the game's own
    Vec3 nx = last ? p : EvCp(s_Ev.cp + 1);
    s_Ev.cpHandle = Game::CreateRaceCheckpoint(air ? (last ? 4 : 3) : (last ? 1 : 0), p, nx, air ? 12.0f : 8.0f);
    s_Ev.blip = Game::AddCoordBlip(p, s_Ev.kind == EV_ROUTE ? 0x4DD2FFFF : 0xFFE14DFF, 2);
}
static void EndEvent(const char* why)
{
    ClearMarkers();
    if(s_Ev.kind != EV_NONE && s_Ev.kind != EV_DM) SendEvent(EvKind(), s_Ev.idx, EVS_LEAVE, 0, 0, 0, 0);
    int ped = Game::LocalPed();
    const bool inside = s_Ev.kind == EV_DERBY || (s_Ev.kind == EV_RACE && kSaRaces[s_Ev.idx].area);
    if(inside && ped > 0 && !Game::IsCharDead(ped))
    {   // back out of the stadium to where I was
        Game::EnterArea(s_Ev.retArea);
        if(Game::IsCharInAnyCar(ped)) { Vec3 o = s_Ev.ret; Game::WarpOutOfCar(ped, o); }
        Game::TeleportGround(ped, s_Ev.ret, s_Ev.retH);
    }
    if(inside && ped > 0 && Game::IsCharDead(ped)) Game::EnterArea(0);   // the hospital is outside
    if(s_Ev.car > 0) { Game::FreezeCar(s_Ev.car, false); if(inside && s_Ev.car == s_nSpawnedCar) { Game::DeleteCar(s_Ev.car); s_nSpawnedCar = 0; } else if(s_Ev.car != s_nSpawnedCar) Game::ReleaseCar(s_Ev.car); }
    TdHide(TD_EVENT_BOX); TdHide(TD_EVENT_TOP); TdHide(TD_GAMETEXT_3);
    if(why && *why) SysMsg(COL_YELLOW, "%s", why);
    s_Ev = GEvState();
}

// grid: 2 columns behind the start line (planes and helicopters get more room)
static Vec3 GridPos(const Vec3& st, float headingDeg, int slot, bool wide = false)
{
    float r = headingDeg * 0.0174533f;
    float fx = -sinf(r), fy = cosf(r), rx = cosf(r), ry = sinf(r);
    float back = (wide ? 16.0f : 7.0f) * (float)(slot / 2), side = (slot % 2) ? (wide ? 9.0f : 2.6f) : (wide ? -9.0f : -2.6f);
    return { st.x - fx * back + rx * side, st.y - fy * back + ry * side, st.z };
}

// jobs: alone, 3 s countdown
static void StartRoute(int i)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(InEvent()) EndEvent("");
    const GRoute& r = kRoutes[i];
    s_Ev.kind = EV_ROUTE; s_Ev.idx = i; s_Ev.cp = 1;
    s_Ev.goAt = MP_NowMs() + 5000; s_Ev.countdown = 99;   // 3.21: 5 s to get ready (was 3.5)
    Vec3 st = r.cp[0];
    float h = HeadingTo(st, r.cp[1]);
    Game::TeleportGround(ped, st, h);
    int car = SpawnVehicleForMe(r.model, false);
    if(car <= 0) { s_Ev = GEvState(); return; }
    Game::SetCarHeading(car, h);
    Game::FreezeCar(car, true);
    s_Ev.car = car;
    ShowCheckpoint();
    Announce(COL_JOB, "[Job] %s started %s (/job %s)", MyName(), r.name, r.cmd);
}

// ---- 3.01: GTA SA races ---------------------------------------------------------------------
// /race = join the queue (stay where you are). When MP_RACE_PLAYERS players are in it, the server
// picks a race and gives everybody a grid place: all are teleported to the race at the same time,
// frozen in their car, 3-2-1-GO together.
static void RaceQueueJoin()
{
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "Connect to a server first."); return; }
    if(!s_bServer301) { SysMsg(COL_YELLOW, "This server can't run races yet: it needs the 3.01 server (HiveMQ console option 7, or mp-server update)."); return; }
    if(InEvent()) { SysMsg(COL_YELLOW, "Finish (or /leave) your race / derby / job / DM first."); return; }
    if(s_RQ.on) { SysMsg(COL_RACE, "You are in the race queue: %d/%d players. The race starts at %d. /leave to leave.", s_RQ.count, s_RQ.need, s_RQ.need); return; }
    s_RQ = RaceQueue(); s_RQ.on = true; s_RQ.count = 1; s_RQ.lastSend = MP_NowMs();
    SendEvent(EVK_RACE, MP_EV_QUEUE, EVS_LOBBY, 0, 0, 0, 0);
    Announce(COL_RACE, "[Race] %s is waiting for a race - type /race to join (starts with %d players, or %d s after 2 players join).", MyName(), MP_RACE_PLAYERS, MP_RACE_WAIT_MS / 1000);
}
static void RaceQueueLeave(bool quiet)
{
    if(!s_RQ.on) return;
    SendEvent(EVK_RACE, MP_EV_QUEUE, EVS_LEAVE, 0, 0, 0, 0);
    s_RQ = RaceQueue();
    TdHide(TD_EVENT_TOP);
    if(!quiet) SysMsg(COL_YELLOW, "You left the race queue.");
}
static void StartSaRace(int i, int slot, int racers, uint32_t goMs)
{
    int ped = Game::LocalPed(); if(ped <= 0 || i < 0 || i >= MP_SA_RACES) return;
    if(InEvent()) EndEvent("");
    const GSaRace& r = kSaRaces[i];
    Vec3 me; Game::GetCharPos(ped, me);
    s_Ev.ret = me; s_Ev.retH = Game::GetCharHeading(ped); s_Ev.retArea = Game::GetCharArea(ped);
    if(Game::IsCharInAnyCar(ped)) Game::WarpOutOfCar(ped, me);
    s_Ev.kind = EV_RACE; s_Ev.idx = i; s_Ev.cp = 1; s_Ev.slot = slot; s_Ev.rivals = racers - 1;
    s_Ev.goAt = MP_NowMs() + (goMs ? goMs : MP_RACE_GO_MS); s_Ev.countdown = 99;
    Vec3 st = r.gh || r.gx ? Vec3{ r.gx, r.gy, r.gz } : r.cp[0];
    float h = r.gx ? r.gh : HeadingTo(r.cp[0], r.cp[1]);
    Vec3 at = GridPos(st, h, slot, r.air);
    if(r.area) Game::EnterArea(r.area);
    Game::TeleportGround(ped, at, h);
    int car = SpawnVehicleForMe(r.model, false);
    if(car <= 0) { s_Ev.kind = EV_NONE; s_Ev = GEvState(); SysMsg(COL_RED, "Could not spawn the race vehicle."); return; }
    if(r.area) Game::SetCarArea(car, r.area);
    Game::SetCarHeading(car, h);
    Game::FreezeCar(car, true);
    s_Ev.car = car;
    ShowCheckpoint();
    SendEvent(EVK_RACE, i, EVS_LOBBY, 0, slot, (uint32_t)(s_Ev.goAt - MP_NowMs()), 0);
    s_Ev.lastSend = MP_NowMs();
    char gt[96]; snprintf(gt, sizeof(gt), "~y~%s~n~~w~%d racers", r.name, racers);
    GameText(gt, 4000, 6);
    SysMsg(COL_RACE, "[Race] %s (%s, %s, %d checkpoints) - grid place %d of %d. /leave to quit.", r.name, r.city, kVehNames[r.model - 400], r.n - 1, slot + 1, racers);
}
static void GamesOnServerEvent(const PktEvent& e)
{
    if(e.kind != EVK_RACE) return;
    if(e.idx == MP_EV_QUEUE) { if(s_RQ.on) { s_RQ.count = e.cp; s_RQ.need = e.slot ? e.slot : MP_RACE_PLAYERS; } return; }
    if(!s_RQ.on) return;   // not mine any more (left the queue)
    s_RQ = RaceQueue();
    TdHide(TD_EVENT_TOP);
    StartSaRace(e.idx, e.slot, e.cp ? e.cp : MP_RACE_PLAYERS, e.ms);
}
// my race position: finished players first, then by checkpoint, then by distance to it
static int RacePosition(int* total)
{
    int ahead = 0, n = 1; uint64_t now = MP_NowMs();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const SampPart& p = s_Part[i];
        if(!p.on || p.kind != EVK_RACE || p.idx != s_Ev.idx || !players[i].active) continue;
        if(p.state == EVS_FINISH) { if(p.finishedAt >= s_Ev.startMs) { ++n; ++ahead; } continue; }
        if(p.state != EVS_PROGRESS || now - p.seen > 6000) continue;
        ++n;
        if(p.cp > s_Ev.cp || (p.cp == s_Ev.cp && p.dist < s_Ev.dist)) ++ahead;
    }
    if(total) *total = n;
    return ahead + 1;
}
static void FinishRoute()
{
    uint32_t ms = (uint32_t)(MP_NowMs() - s_Ev.startMs);
    char t[24]; FmtTime(ms, t, sizeof(t));
    if(s_Ev.kind == EV_RACE)
    {
        const GSaRace& r = kSaRaces[s_Ev.idx];
        bool pb = !s_nRaceBestMs[s_Ev.idx] || ms < s_nRaceBestMs[s_Ev.idx];
        if(pb) s_nRaceBestMs[s_Ev.idx] = ms;
        int total = 1; s_Ev.cp = r.n; s_Ev.dist = 0;
        int place = RacePosition(&total);
        int reward = total > 1 ? r.reward * (total - place + 1) / total + (place == 1 ? r.reward / 2 : 0) : r.reward;
        Game::AddMoney(reward);
        ++s_nRaceScore;
        SendEvent(EVK_RACE, s_Ev.idx, EVS_FINISH, r.n > 255 ? 255 : r.n, s_Ev.slot, ms, 0);
        Announce(COL_RACE, "RACE: %s finished %s in %d%s place! (%s%s)", MyName(), r.name, place, Ordinal(place), t, pb ? " - best" : "");
        char gt[96]; snprintf(gt, sizeof(gt), "~y~%d%s place!~n~~w~%s  ~g~+$%d", place, Ordinal(place), t, reward);
        GameText(gt, 5000, 1);
        if(!r.area) { ClearMarkers(); if(s_Ev.car > 0) Game::FreezeCar(s_Ev.car, false); s_Ev.car = 0; }   // outside: you keep the car where you finished
    }
    else
    {
        const GRoute& r = kRoutes[s_Ev.idx];
        if(!s_nBestMs[s_Ev.idx] || ms < s_nBestMs[s_Ev.idx]) s_nBestMs[s_Ev.idx] = ms;
        Game::AddMoney(r.reward);
        ++s_nJobScore;
        Announce(COL_JOB, "JOB: %s completed %s! (+$%d, %s)", MyName(), r.name, r.reward, t);
        char gt[64]; snprintf(gt, sizeof(gt), "~g~Job complete!~n~~w~+$%d", r.reward);
        GameText(gt, 4000, 1);
    }
    EndEvent("");
}

// 3.21.4: a random DM point in the street, not on a roof (up to 12 tries, else the zone centre)
static Vec3 DmSpot(const GDm& d, float spread)
{
    for(int t = 0; t < 12; ++t)
    {
        float a = (float)(rand() % 360) * 0.0174533f, rr = d.r * spread * (float)(rand() % 100) / 100.0f;
        float x = d.x + cosf(a) * rr, y = d.y + sinf(a) * rr, z;
        if(Game::OpenGround(x, y, d.z, z)) return { x, y, z + 1.0f };
    }
    return { d.x, d.y, d.z };
}

static void StartDm(int i)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(InEvent()) EndEvent("");
    const GDm& d = kDms[i];
    s_Ev.kind = EV_DM; s_Ev.idx = i; s_Ev.streak = 0;
    Game::TeleportGround(ped, DmSpot(d, 0.4f), (float)(rand() % 360));
    Game::SetCharHealth(ped, 100);
    Game::AddArmour(ped, 100);
    for(int k = 3; k >= 0; --k) if(d.w[k] > 0) Game::GiveAndSelectWeapon(ped, d.w[k]);
    s_Ev.blip = Game::AddCoordBlip({ d.x, d.y, d.z }, 0xD25BFFFF, 2);
    Announce(COL_DM, "(/dm %s) %s teleported to %s!", d.cmd, MyName(), d.name);
}

// derby lobby: 15 s when other players are online (they can type the same /derby), else 3 s
static void JoinLobby(int kind, int idx)
{
    uint64_t now = MP_NowMs();
    int used = 0;
    uint64_t go = EventLobbyGo(kind, idx, &used);
    if(go) s_Ev.slot = used;
    else { go = now + (OthersOnline() > 0 && s_bServer301 ? 30000 : 8000); s_Ev.slot = 0; }   // 3.21: 30 s for the others to join (was 15), 8 s alone (was 3.5)
    s_Ev.goAt = go; s_Ev.countdown = 99;
    SendEvent(kind, idx, EVS_LOBBY, 0, s_Ev.slot, (uint32_t)(go - now), 0);
    s_Ev.lastSend = now;
}

// 3.01: derby
static void StartDerby(int i)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(InEvent()) EndEvent("");
    const GDerby& d = kDerbies[i];
    Vec3 me; Game::GetCharPos(ped, me);
    s_Ev.ret = me; s_Ev.retH = Game::GetCharHeading(ped); s_Ev.retArea = Game::GetCharArea(ped);
    s_Ev.kind = EV_DERBY; s_Ev.idx = i;
    JoinLobby(EVK_DERBY, i);
    float a = (float)s_Ev.slot * (6.2831853f / 12.0f);
    Vec3 at = { d.x + cosf(a) * d.ring, d.y + sinf(a) * d.ring, d.z };
    float h = HeadingTo(at, { d.x, d.y, d.z });
    if(d.area) Game::EnterArea(d.area);
    Game::TeleportGround(ped, at, h);
    int car = SpawnVehicleForMe(d.model, false);
    if(car <= 0) { s_Ev.kind = EV_NONE; EndEvent(""); return; }
    if(d.area) Game::SetCarArea(car, d.area);
    Game::SetCarHeading(car, h);
    Game::FreezeCar(car, true);
    s_Ev.car = car;
    s_Ev.blip = Game::AddCoordBlip({ d.x, d.y, d.z }, 0xFF8C1EFF, 2);
    Announce(COL_RACE, "[Derby] %s joined %s (/derby %s)", MyName(), d.name, d.cmd);
}
static int DerbyAlive()   // others still in my derby
{
    int n = 0; uint64_t now = MP_NowMs();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const SampPart& p = s_Part[i];
        if(!p.on || p.kind != EVK_DERBY || p.idx != s_Ev.idx || !players[i].active) continue;
        if((p.state == EVS_PROGRESS || p.state == EVS_LOBBY) && now - p.seen < 6000) ++n;
    }
    return n;
}
static void DerbyOut(const char* how)
{
    const GDerby& d = kDerbies[s_Ev.idx];
    SendEvent(EVK_DERBY, s_Ev.idx, EVS_OUT, 0, s_Ev.slot, (uint32_t)(MP_NowMs() - s_Ev.startMs), 0);
    int left = DerbyAlive();
    if(s_Ev.rivals > 0) Announce(COL_RACE, "DERBY: %s %s in %s! (%d left)", MyName(), how, d.name, left);
    char gt[64]; snprintf(gt, sizeof(gt), "~r~%s!", how[0] == 'w' ? "WRECKED" : "OUT");
    GameText(gt, 3000, 3);
    EndEvent("");
}
static void DerbyWin(bool alone)
{
    const GDerby& d = kDerbies[s_Ev.idx];
    int reward = alone ? d.reward / 4 : d.reward * (1 + s_Ev.rivals) / 2;
    Game::AddMoney(reward);
    ++s_nRaceScore;
    SendEvent(EVK_DERBY, s_Ev.idx, EVS_FINISH, 0, s_Ev.slot, (uint32_t)(MP_NowMs() - s_Ev.startMs), 0);
    if(alone) Announce(COL_RACE, "DERBY: %s survived %s!", MyName(), d.name);
    else Announce(COL_RACE, "DERBY: %s won %s! (last car standing of %d)", MyName(), d.name, s_Ev.rivals + 1);
    char gt[64]; snprintf(gt, sizeof(gt), alone ? "~y~Survived!~n~~g~+$%d" : "~y~Winner!~n~~g~+$%d", reward);
    GameText(gt, 5000, 1);
    EndEvent("");
}

// ---- 3.01: dialog menus (SA-MP list dialogs) ---------------------------------------------------
static void OnRouteDialog(int resp, int item, const char*, int) { if(resp && item >= 0 && item < MP_ROUTE_COUNT) StartRoute(item); }
static void ShowRouteDialog(int)
{
    std::string body = "Job\tVehicle\tPay\tBest\n";
    for(int i = 0; i < MP_ROUTE_COUNT; ++i)
    {
        char best[24] = "-"; if(s_nBestMs[i]) FmtTime(s_nBestMs[i], best, sizeof(best));
        char row[200];
        snprintf(row, sizeof(row), "%s\t%s\t{33CC55}$%d\t{A9C4E4}%s\n", kRoutes[i].name, kVehNames[kRoutes[i].model - 400], kRoutes[i].reward, best);
        body += row;
    }
    ShowDialog(3011, DIALOG_STYLE_TABLIST_HEADERS, "{4DD2FF}Jobs", body.c_str(), "Start", "Close", OnRouteDialog);
}
// /races: the 27 tracks (where they are: Mark = blip on the start for 90 s)
static int s_nRaceMarkBlip = 0; static uint64_t s_nRaceMarkUntil = 0;
static void OnRacesDialog(int resp, int item, const char*, int)
{
    if(!resp || item < 0 || item >= MP_SA_RACES) return;
    const GSaRace& r = kSaRaces[item];
    if(r.area) { SysMsg(COL_RACE, "%s is inside the stadium (interior %d): the race takes you there.", r.name, r.area); return; }
    if(s_nRaceMarkBlip) Game::RemoveBlip(s_nRaceMarkBlip);
    s_nRaceMarkBlip = Game::AddCoordBlip(r.cp[0], 0xFFE14DFF, 2);
    s_nRaceMarkUntil = MP_NowMs() + 90000;
    SysMsg(COL_RACE, "%s start is marked on your radar for 90 seconds.", r.name);
}
static void ShowRacesDialog()
{
    std::string body = "Race\tWhere\tVehicle\tBest\n";
    for(int i = 0; i < MP_SA_RACES; ++i)
    {
        char best[24] = "-"; if(s_nRaceBestMs[i]) FmtTime(s_nRaceBestMs[i], best, sizeof(best));
        char row[200];
        snprintf(row, sizeof(row), "%s\t{A9C4E4}%s\t%s\t%s\n", kSaRaces[i].name, kSaRaces[i].city, kVehNames[kSaRaces[i].model - 400], best);
        body += row;
    }
    char cap[96]; snprintf(cap, sizeof(cap), "{FFE14D}GTA SA races - /race to queue (%d players)", MP_RACE_PLAYERS);
    ShowDialog(3010, DIALOG_STYLE_TABLIST_HEADERS, cap, body.c_str(), "Mark", "Close", OnRacesDialog);
}
static void OnDmDialog(int resp, int item, const char*, int) { if(resp && item >= 0 && item < MP_DM_COUNT) StartDm(item); }
static void OnDerbyDialog(int resp, int item, const char*, int) { if(resp && item >= 0 && item < MP_DERBY_COUNT) StartDerby(item); }
static void ShowDmDialog()
{
    std::string body = "Zone\tWeapons\n";
    for(int i = 0; i < MP_DM_COUNT; ++i)
    {
        std::string w;
        for(int k = 0; k < 4; ++k) if(kDms[i].w[k] > 0) for(int j = 0; j < MP_WEAP_COUNT; ++j) if(kWeaps[j].id == kDms[i].w[k]) { if(!w.empty()) w += ", "; w += kWeaps[j].name; }
        body += std::string(kDms[i].name) + "\t{A9C4E4}" + w + "\n";
    }
    ShowDialog(3012, DIALOG_STYLE_TABLIST_HEADERS, "{D25BFF}Deathmatch", body.c_str(), "Go", "Close", OnDmDialog);
}
static void ShowDerbyDialog()
{
    std::string body = "Derby\tVehicle\tPrize\n";
    for(int i = 0; i < MP_DERBY_COUNT; ++i)
    {
        int n = EventOthers(EVK_DERBY, i);
        char row[160];
        snprintf(row, sizeof(row), "%s%s\t%s\t{33CC55}$%d\n", kDerbies[i].name, n ? (std::string("  {FF8C1E}(") + std::to_string(n) + " in)").c_str() : "", kVehNames[kDerbies[i].model - 400], kDerbies[i].reward);
        body += row;
    }
    ShowDialog(3013, DIALOG_STYLE_TABLIST_HEADERS, "{FF8C1E}Derbies - last car standing", body.c_str(), "Join", "Close", OnDerbyDialog);
}

// ---- commands -------------------------------------------------------------------------------
static int FindRoute(const char* a, int)
{
    char w[24]; Squash(a, w, sizeof(w));
    for(int i = 0; i < MP_ROUTE_COUNT; ++i) if(!strcasecmp(kRoutes[i].cmd, w)) return i;
    return -1;
}
static void ListRoutes(int)
{
    for(int i = 0; i < MP_ROUTE_COUNT; ++i)
    {
        char best[32] = "";
        if(s_nBestMs[i]) { char t[24]; FmtTime(s_nBestMs[i], t, sizeof(t)); snprintf(best, sizeof(best), "  best %s", t); }
        SysMsg(COL_JOB, "/job %s  -  %s  (%s, $%d)%s", kRoutes[i].cmd, kRoutes[i].name, kVehNames[kRoutes[i].model - 400], kRoutes[i].reward, best);
    }
}

static void GamesHelp()
{
    SysMsg(COL_EVT, "/spawn [id|name] vehicle  /v = same  /w [id|name|all] weapons  /fix  /flip  /heal  /armour");
    SysMsg(COL_EVT, "/race (queue: %d players start a GTA SA race)  /races (the 27 tracks)", MP_RACE_PLAYERS);
    SysMsg(COL_EVT, "/derby [bloodbowl|monster|docks|desert|sumo]  /job [trucking|quarry|pizza|burger|cluckin|taxi|medic|firetruck]  /dm [war|grove|airport|minigun|sniper]  /leave");
    SysMsg(COL_EVT, "/class (gang / police / civilian)  /turf (gang zones)  /teams  /bubbles  /labels  /icons  /actors  /zones");
    std::string t = "Teleports:";
    for(int i = 0; i < MP_GPLACE_COUNT; ++i) { t += " /"; t += kPlaces[i].cmd; if(t.size() > 110) { SysMsg(COL_EVT, "%s", t.c_str()); t = " "; } }
    if(t.size() > 1) SysMsg(COL_EVT, "%s", t.c_str());
    SysMsg(COL_EVT, "/scores  /unlock (open world on/off)  /girlfriends  /tips");
}

static bool SampCommand(const char* cmd, const char* arg);   // mp_world.inc: /class /turf /teams ...
static bool GamesCommand(const char* cmd, const char* arg)
{
    if(SampCommand(cmd, arg)) return true;
    if(!strcmp(cmd, "spawn") || !strcmp(cmd, "v") || !strcmp(cmd, "veh") || !strcmp(cmd, "car") || !strcmp(cmd, "vehicle")) { if(s_Ev.kind == EV_DERBY) { SysMsg(COL_YELLOW, "Not during a derby."); return true; } CmdSpawn(arg); return true; }
    if(!strcmp(cmd, "w") || !strcmp(cmd, "weapon") || !strcmp(cmd, "weapons")) { CmdWeapon(arg); return true; }
    if(!strcmp(cmd, "c")) { HandleCommand("/help"); return true; }
    if(!strcmp(cmd, "t") || !strcmp(cmd, "teleports")) { GamesHelp(); return true; }
    if(!strcmp(cmd, "fix") || !strcmp(cmd, "repair"))
    {
        int ped = Game::LocalPed(); if(ped <= 0 || !Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Get in a car first."); return true; }
        if(s_Ev.kind == EV_DERBY) { SysMsg(COL_YELLOW, "No repairs in a derby."); return true; }
        int car = Game::GetCarCharIsIn(ped); Game::SetCarHealth(car, 1000); SysMsg(COL_SYSTEM, "Vehicle repaired.");
        return true;
    }
    if(!strcmp(cmd, "flip"))
    {
        int ped = Game::LocalPed(); if(ped <= 0 || !Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Get in a car first."); return true; }
        int car = Game::GetCarCharIsIn(ped); Vec3 p; Game::GetCarPos(car, p); p.z += 1.5f;
        Game::SetCarPos(car, p, 0.0f); Game::SetCarHeading(car, Game::GetCarHeading(car));
        return true;
    }
    if(!strcmp(cmd, "heal") || !strcmp(cmd, "hp")) { int ped = Game::LocalPed(); if(ped > 0 && !InEvent()) { Game::SetCharHealth(ped, 100); SysMsg(COL_SYSTEM, "Healed."); } else SysMsg(COL_YELLOW, "Not during an event."); return true; }
    if(!strcmp(cmd, "armour") || !strcmp(cmd, "armor")) { int ped = Game::LocalPed(); if(ped > 0 && !InEvent()) { Game::AddArmour(ped, 100); SysMsg(COL_SYSTEM, "Armour on."); } else SysMsg(COL_YELLOW, "Not during an event."); return true; }
    if(!strcmp(cmd, "race") && !NeedMode(GM_RACE)) return true;
    if((!strcmp(cmd, "races") || !strcmp(cmd, "tracks")) && !NeedMode(GM_RACE)) return true;
    if((!strcmp(cmd, "job") || !strcmp(cmd, "jobs") || !strcmp(cmd, "trucking") || !strcmp(cmd, "trucker")) && !NeedMode(GM_JOBS)) return true;
    if((!strcmp(cmd, "derby") || !strcmp(cmd, "derbies") || !strcmp(cmd, "dd") || !strcmp(cmd, "sumo")) && !NeedMode(GM_DERBY)) return true;
    if((!strcmp(cmd, "dm") || !strcmp(cmd, "war") || !strcmp(cmd, "deathmatch")) && !NeedMode(GM_DM)) return true;
    if(!strcmp(cmd, "race"))
    {
        if(!strcasecmp(arg, "list") || !strcasecmp(arg, "tracks")) { ShowRacesDialog(); return true; }
        RaceQueueJoin(); return true;
    }
    if(!strcmp(cmd, "races") || !strcmp(cmd, "tracks")) { ShowRacesDialog(); return true; }
    if(!strcmp(cmd, "job") || !strcmp(cmd, "jobs") || !strcmp(cmd, "trucking") || !strcmp(cmd, "trucker"))
    {
        const char* a = arg;
        if(!strcmp(cmd, "trucking") || !strcmp(cmd, "trucker")) a = "trucking";
        if(!*a) { ShowRouteDialog(RK_JOB); return true; }
        if(!strcasecmp(a, "list")) { ListRoutes(RK_JOB); return true; }
        int i = FindRoute(a, RK_JOB); if(i < 0) { SysMsg(COL_RED, "No job %s. Type /job.", a); return true; }
        StartRoute(i); return true;
    }
    if(!strcmp(cmd, "derby") || !strcmp(cmd, "derbies") || !strcmp(cmd, "dd") || !strcmp(cmd, "sumo"))
    {
        const char* a = !strcmp(cmd, "sumo") ? "sumo" : arg;
        if(!*a) { ShowDerbyDialog(); return true; }
        for(int i = 0; i < MP_DERBY_COUNT; ++i) if(!strcasecmp(kDerbies[i].cmd, a)) { StartDerby(i); return true; }
        SysMsg(COL_RED, "No derby %s. Type /derby.", a); return true;
    }
    if(!strcmp(cmd, "dm") || !strcmp(cmd, "war") || !strcmp(cmd, "deathmatch"))
    {
        const char* a = !strcmp(cmd, "war") ? "war" : arg;
        if(!*a) { ShowDmDialog(); return true; }
        for(int i = 0; i < MP_DM_COUNT; ++i) if(!strcasecmp(kDms[i].cmd, a)) { StartDm(i); return true; }
        SysMsg(COL_RED, "No DM %s. Type /dm.", a); return true;
    }
    if(!strcmp(cmd, "leave") || !strcmp(cmd, "quit") || !strcmp(cmd, "exit"))
    {
        if(!InEvent() && s_RQ.on) RaceQueueLeave(false);
        else if(!InEvent()) SysMsg(COL_YELLOW, "You are not in a race / derby / job / DM.");
        else
        {
            if(s_Ev.kind == EV_DM) Announce(COL_DM, "DEATHMATCH: %s left %s.", MyName(), kDms[s_Ev.idx].name);
            if(s_Ev.kind == EV_DERBY && s_Ev.startMs) { DerbyOut("left"); return true; }
            EndEvent("Left the event.");
        }
        return true;
    }
    if((!strcmp(cmd, "scores") || !strcmp(cmd, "score")) && !(MP::ModeOn(GM_DM) || MP::ModeOn(GM_RACE) || MP::ModeOn(GM_DERBY) || MP::ModeOn(GM_JOBS))) { SysMsg(COL_EVT, "This server has no DM / race / job gamemodes (Add server > Gamemodes)."); return true; }
    if(!strcmp(cmd, "scores") || !strcmp(cmd, "score")) { SysMsg(COL_EVT, "DM score %d  -  Race score %d  -  Job score %d", s_nDmScore, s_nRaceScore, s_nJobScore); return true; }
    if(!strcmp(cmd, "unlock")) { settings.unlockWorld = !settings.unlockWorld; SaveConfig(); s_nNextUnlock = 0; SysMsg(COL_SYSTEM, "Open world (cities, shops, gyms, food, garages): %s", OnOff(settings.unlockWorld)); return true; }
    if(!strcmp(cmd, "girlfriends") || !strcmp(cmd, "gfs")) { settings.girlfriends = !settings.girlfriends; SaveConfig(); s_nNextUnlock = 0; SysMsg(COL_SYSTEM, "Girlfriend houses: %s", OnOff(settings.girlfriends)); return true; }
    if(!strcmp(cmd, "tips")) { settings.eventTips = !settings.eventTips; SaveConfig(); SysMsg(COL_SYSTEM, "Event tips in the chat: %s", OnOff(settings.eventTips)); return true; }
    if(!strcmp(cmd, "goto")) { for(int i = 0; i < MP_GPLACE_COUNT; ++i) if(!strcasecmp(kPlaces[i].cmd, arg)) { GotoPlace(kPlaces[i], true); return true; } SysMsg(COL_RED, "No place %s. Type /t.", arg); return true; }
    for(int i = 0; i < MP_GPLACE_COUNT; ++i)
        if(!strcmp(cmd, kPlaces[i].cmd))
        {
            // "/pizza", "/quarry", "/trucking" etc: the job when there is one with that name and the
            // player typed "/pizza job"; plain = go there
            GotoPlace(kPlaces[i], true); return true;
        }
    return false;
}

static void GamesSuggest(const char* t, std::vector<Suggest>& out)
{
    if(!t || t[0] != '/') return;
    const char* sp = strchr(t, ' ');
    if(!sp)
    {
        static const struct { const char* c; const char* h; } kTop[] =
        {
            { "/spawn ", "[id|name]  -  spawn a vehicle (/spawn 411, /spawn nrg)" }, { "/w ", "[id|name|all]  -  weapons" },
            { "/race", "queue for a GTA SA race (14 players)" }, { "/races", "the 27 race tracks (mark on radar)" }, { "/derby ", "[arena]  -  last car standing wins" },
            { "/job ", "[name]  -  trucking, quarry, pizza, taxi..." },
            { "/dm ", "[zone]  -  deathmatch zones" }, { "/leave", "leave the race / derby / job / DM" }, { "/t", "teleports everybody sees" },
            { "/class", "pick a gang, police or civilian (SA-MP class selection)" }, { "/turf", "gang zones + who owns them" }, { "/teams", "turf count per gang" },
            { "/fix", "repair your vehicle" }, { "/flip", "flip your vehicle" }, { "/scores", "your DM / race / job score" },
            { "/bubbles", "chat bubbles on / off (off by default)" }, { "/labels", "3D text labels on / off" }, { "/icons", "map icons on / off" },
            { "/actors", "actors at the job starts on / off" }, { "/zones", "gang zones on the radar on / off" },
            { "/unlock", "open world on / off" }, { "/girlfriends", "girlfriend houses on / off" },
        };
        size_t n = strlen(t);
        for(auto& k : kTop) if(!strncasecmp(k.c, t, n)) out.push_back({ k.c, k.h });
        for(int i = 0; i < MP_GPLACE_COUNT && out.size() < 24; ++i)
        {
            std::string c = std::string("/") + kPlaces[i].cmd;
            if(n > 1 && !strncasecmp(c.c_str(), t, n)) out.push_back({ c, std::string("teleport: ") + kPlaces[i].name });
        }
        return;
    }
    std::string head(t, sp - t);
    const char* a = sp + 1; while(*a == ' ') ++a;
    size_t n = strlen(a);
    if(head == "/spawn" || head == "/v")
    {
        char w[32]; Squash(a, w, sizeof(w));
        for(int i = 0; i < 212 && out.size() < 12; ++i)
        {
            if(IsTrainOrBad(400 + i)) continue;
            char nm[32]; Squash(kVehNames[i], nm, sizeof(nm));
            std::string id = std::to_string(400 + i);
            if(!n || !strncmp(nm, w, strlen(w)) || !strncmp(id.c_str(), a, n))
                out.push_back({ head + " " + id, kVehNames[i] });
        }
    }
    else if(head == "/w")
    {
        for(int i = 0; i < MP_WEAP_COUNT && out.size() < 12; ++i)
            if(!n || !strncasecmp(kWeaps[i].name, a, n)) out.push_back({ std::string("/w ") + kWeaps[i].name, std::to_string(kWeaps[i].id) });
    }
    else if(head == "/job")
    {
        for(int i = 0; i < MP_ROUTE_COUNT; ++i) if(!n || !strncasecmp(kRoutes[i].cmd, a, n))
            out.push_back({ head + " " + kRoutes[i].cmd, kRoutes[i].name });
    }
    else if(head == "/derby")
    {
        for(int i = 0; i < MP_DERBY_COUNT; ++i) if(!n || !strncasecmp(kDerbies[i].cmd, a, n)) out.push_back({ std::string("/derby ") + kDerbies[i].cmd, kDerbies[i].name });
    }
    else if(head == "/dm")
    {
        for(int i = 0; i < MP_DM_COUNT; ++i) if(!n || !strncasecmp(kDms[i].cmd, a, n)) out.push_back({ std::string("/dm ") + kDms[i].cmd, kDms[i].name });
    }
    else if(head == "/goto")
    {
        for(int i = 0; i < MP_GPLACE_COUNT && out.size() < 12; ++i) if(!n || !strncasecmp(kPlaces[i].cmd, a, n)) out.push_back({ std::string("/goto ") + kPlaces[i].cmd, kPlaces[i].name });
    }
}

// ---- kills (DM score + streak announcements) -------------------------------------------------
static void GamesOnDeath(const PktDeath& d)
{
    if(s_Ev.kind != EV_DM) return;
    if(d.killer == myId && d.victim != myId)
    {
        ++s_nDmScore; ++s_Ev.streak;
        if(s_Ev.streak == 3 || s_Ev.streak == 5 || s_Ev.streak == 10 || (s_Ev.streak > 10 && s_Ev.streak % 5 == 0))
            Announce(COL_DM, "DEATHMATCH: %s has %d kills in a row in %s!", MyName(), s_Ev.streak, kDms[s_Ev.idx].name);
    }
}

// ---- every game tick ------------------------------------------------------------------------
struct GTip { uint16_t mode; const char* text; };
static const GTip kTips[] =
{
    { GM_RACE, "[Race] Type /race to queue for a GTA SA race. It waits for players: it starts with 14 players, or 45 s after the 2nd player is in (15 s more for every new player). Never alone." },
    { GM_DERBY, "[Derby] Type /derby for Blood Bowl, Monster, Docks, Desert or Chiliad Sumo. Last car standing wins." },
    { GM_DM, "[DM] Type /dm war for the War Zone DM. Kill streaks are announced to everyone." },
    { GM_JOBS, "[Job] Type /job trucking, /job quarry or /job pizza to earn money." },
    { GM_TURF, "[Turf] Type /class to join a gang, then stand in an enemy zone (/turf) for 60 s to take it." },
    { 0, "[Spawn] Type /spawn infernus (or any id 400 - 611) for a vehicle, /w all for weapons." },
    { 0, "[Teleport] /ls /sf /lv /chiliad /desert /beach /pizza /gym /ammu /emmet /home ... (/t for all)" },
    { 0, "[World] All cities, shops, gyms, barbers, tattoo, clothes, Pay 'n' Spray and mod garages are open." },
};
// lobby + countdown (3, 2, 1, GO) shared by races, jobs and derbies. true = still waiting
static bool EventCountdown(uint64_t now)
{
    if(!s_Ev.goAt) return false;
    if(now < s_Ev.goAt)
    {
        int left = (int)((s_Ev.goAt - now + 999) / 1000);
        if(s_Ev.kind != EV_ROUTE)
            if(now - s_Ev.lastSend >= 1000) { s_Ev.lastSend = now; SendEvent(EvKind(), s_Ev.idx, EVS_LOBBY, 0, s_Ev.slot, (uint32_t)(s_Ev.goAt - now), 0); }
        if(left <= 3 && left != s_Ev.countdown)
        {
            s_Ev.countdown = left;
            char t[16]; snprintf(t, sizeof(t), "~y~%d", left);
            GameText(t, 1100, 3);
        }
        return true;
    }
    s_Ev.goAt = 0; s_Ev.countdown = 0;
    GameText("~g~GO!", 1200, 3);
    Game::FreezeCar(s_Ev.car, false);
    s_Ev.startMs = now; s_Ev.lastSend = 0;
    if(s_Ev.kind == EV_DERBY) s_Ev.rivals = EventOthers(EVK_DERBY, s_Ev.idx);   // races: the server told us (racers - 1)
    return false;
}
static void ProcessGames(int ped, uint64_t now)
{
    if(ped <= 0) return;
    // open world, re-applied now and then (a mission script may close a door again), not on missions
    if(settings.unlockWorld && !s_bLocalMission && now >= s_nNextUnlock)
    {
        s_nNextUnlock = now + 20000;
        Game::UnlockWorld(settings.girlfriends);
    }
    if(settings.eventTips && client.GetState() == NET_CONNECTED && now >= s_nNextTip)
    {
        const int nt = (int)(sizeof(kTips) / sizeof(kTips[0]));
        for(int k = 0; k < nt && s_nNextTip; ++k)   // 3.02: only tips for gamemodes this server has
        {
            const GTip& tip = kTips[s_nTip++ % nt];
            if(!tip.mode || MP::ModeOn(tip.mode)) { SysMsg(0xA0E0FFFF, "%s", tip.text); break; }
        }
        s_nNextTip = now + 240000;
    }
    if(s_nRaceMarkBlip && now > s_nRaceMarkUntil) { Game::RemoveBlip(s_nRaceMarkBlip); s_nRaceMarkBlip = 0; }
    if(s_RQ.on)
    {   // still in the queue: tell the server now and then (it forgets us when it restarts)
        if(client.GetState() != NET_CONNECTED) s_RQ = RaceQueue();
        else if(now - s_RQ.lastSend >= 5000) { s_RQ.lastSend = now; SendEvent(EVK_RACE, MP_EV_QUEUE, EVS_LOBBY, 0, 0, 0, 0); }
    }
    if(s_Ev.kind == EV_NONE) return;
    bool dead = Game::IsCharDead(ped) || Game::GetCharHealth(ped) <= 0;

    if(s_Ev.kind == EV_RACE)
    {
        const GSaRace& r = kSaRaces[s_Ev.idx];
        if(dead) { EndEvent("Race over: you died."); return; }
        if(EventCountdown(now)) return;
        bool inMyCar = Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_Ev.car;
        if(!inMyCar)
        {
            if(!s_Ev.outOfCarSince) { s_Ev.outOfCarSince = now; SysMsg(COL_YELLOW, "Get back in your vehicle within 20 seconds."); }
            else if(now - s_Ev.outOfCarSince > 20000) { EndEvent("Left the vehicle: race over."); return; }
        }
        else s_Ev.outOfCarSince = 0;
        Vec3 me; Game::GetCharPos(ped, me);
        s_Ev.dist = GDist2D(me, r.cp[s_Ev.cp]);
        if(s_Ev.dist < (r.air ? 20.0f : 12.0f))
        {
            ++s_Ev.cp;
            if(s_Ev.cp >= r.n) { FinishRoute(); return; }
            ShowCheckpoint();
            s_Ev.lastSend = 0;
        }
        if(now - s_Ev.lastSend >= 1000) { s_Ev.lastSend = now; SendEvent(EVK_RACE, s_Ev.idx, EVS_PROGRESS, s_Ev.cp > 255 ? 255 : s_Ev.cp, s_Ev.slot, (uint32_t)(now - s_Ev.startMs), s_Ev.dist); }
        return;
    }

    if(s_Ev.kind == EV_ROUTE)
    {
        const GRoute& r = kRoutes[s_Ev.idx];
        if(dead) { EndEvent("Job failed: you died."); return; }
        if(EventCountdown(now)) return;
        bool inMyCar = Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_Ev.car;
        if(!inMyCar)
        {
            if(!s_Ev.outOfCarSince) { s_Ev.outOfCarSince = now; SysMsg(COL_YELLOW, "Get back in your vehicle within 20 seconds."); }
            else if(now - s_Ev.outOfCarSince > 20000) { EndEvent("Left the vehicle: event over."); return; }
        }
        else s_Ev.outOfCarSince = 0;
        Vec3 me; Game::GetCharPos(ped, me);
        const Vec3& c = r.cp[s_Ev.cp];
        s_Ev.dist = GDist2D(me, c);
        if(s_Ev.dist < 12.0f)   // flat distance: works for hills and guessed heights
        {
            ++s_Ev.cp;
            if(s_Ev.cp >= r.n) { FinishRoute(); return; }
            ShowCheckpoint();
            s_Ev.lastSend = 0;
        }
        return;
    }
    if(s_Ev.kind == EV_DERBY)
    {
        const GDerby& d = kDerbies[s_Ev.idx];
        if(dead) { DerbyOut("was wrecked"); return; }
        if(EventCountdown(now)) return;
        if(now - s_Ev.lastSend >= 2000) { s_Ev.lastSend = now; SendEvent(EVK_DERBY, s_Ev.idx, EVS_PROGRESS, 0, s_Ev.slot, (uint32_t)(now - s_Ev.startMs), 0); }
        bool inMyCar = Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_Ev.car;
        if(Game::IsCarDead(s_Ev.car) || (inMyCar && Game::GetCarHealth(s_Ev.car) < 250)) { DerbyOut("was wrecked"); return; }
        if(!inMyCar)
        {
            if(!s_Ev.outOfCarSince) { s_Ev.outOfCarSince = now; SysMsg(COL_YELLOW, "Get back in your car within %d seconds!", MP_DERBY_GRACE_MS / 1000); }
            else if(now - s_Ev.outOfCarSince > MP_DERBY_GRACE_MS) { DerbyOut("was out of the car"); return; }   // 3.21: 15 s (was 5)
        }
        else s_Ev.outOfCarSince = 0;
        Vec3 me; Game::GetCharPos(ped, me);
        if(me.z < d.fallZ) { DerbyOut("fell off"); return; }
        if(GDist2D(me, { d.x, d.y, d.z }) > d.arena)
        {
            if(!s_Ev.outOfZoneSince) { s_Ev.outOfZoneSince = now; SysMsg(COL_YELLOW, "Back into the arena within %d seconds! (follow the red ring)", MP_DERBY_GRACE_MS / 1000); }
            else if(now - s_Ev.outOfZoneSince > MP_DERBY_GRACE_MS) { DerbyOut("left the arena"); return; }   // 3.21: 15 s (was 5)
        }
        else s_Ev.outOfZoneSince = 0;
        // winner: everybody else is out. Alone: survive 60 s. Time limit 4 min (everybody left wins)
        if(s_Ev.rivals > 0 && DerbyAlive() == 0 && now - s_Ev.startMs > 3000) { DerbyWin(false); return; }
        if(s_Ev.rivals == 0 && now - s_Ev.startMs > 60000) { DerbyWin(true); return; }
        if(now - s_Ev.startMs > 240000) { DerbyWin(s_Ev.rivals == 0); return; }
        return;
    }
    if(s_Ev.kind == EV_DM)
    {
        const GDm& d = kDms[s_Ev.idx];
        if(dead) { if(!s_Ev.wasDead) { s_Ev.wasDead = true; s_Ev.streak = 0; } return; }
        if(s_Ev.wasDead)
        {   // respawned (hospital): straight back into the zone
            s_Ev.wasDead = false;
            Game::TeleportGround(ped, DmSpot(d, 0.5f), (float)(rand() % 360));
            Game::SetCharHealth(ped, 100); Game::AddArmour(ped, 100);
            for(int k = 3; k >= 0; --k) if(d.w[k] > 0) Game::GiveAndSelectWeapon(ped, d.w[k]);
            return;
        }
        Vec3 me; Game::GetCharPos(ped, me);
        if(GDist2D(me, { d.x, d.y, d.z }) > d.r)
        {
            if(!s_Ev.outOfZoneSince) { s_Ev.outOfZoneSince = now; SysMsg(COL_YELLOW, "You left the DM zone: go back within 10 seconds (/leave to quit)."); }
            else if(now - s_Ev.outOfZoneSince > 10000) { Announce(COL_DM, "DEATHMATCH: %s left %s.", MyName(), d.name); EndEvent("Left the DM zone."); }
        }
        else s_Ev.outOfZoneSince = 0;
    }
}

// ---- HUD: score bar (top right) + event textdraws ---------------------------------------------
// 3.00: top-right score bar - one colour per score (blue / yellow / red / green), drawn by the game
// in its Pricedown HUD font (Game::SetHudTextProvider). ImGui draws the same colours if the game
// font isn't available.
// 3.04: only the scores of gamemodes this server has (Add server > Gamemodes; the default server
// has all of them). Brief Case War and a custom server without DM / races / derbies / jobs show none.
static int ScoreSegsBuild(Game::HudSeg* seg)
{
    if(MP::IsOnBcw() || MP::IsOnMw3() || MP::IsOnRp()) return 0;
    int n = 0;
    if(MP::ModeOn(GM_DM))                       { snprintf(seg[n].text, sizeof(seg[n].text), "DM SCORE: %d", s_nDmScore);     seg[n++].rgba = 0x3C8CFFFF; }   // blue
    if(MP::ModeOn(GM_RACE) || MP::ModeOn(GM_DERBY)) { snprintf(seg[n].text, sizeof(seg[n].text), "RACE SCORE: %d", s_nRaceScore); seg[n++].rgba = 0xFFD21EFF; }   // yellow
    if(MP::ModeOn(GM_JOBS))                     { snprintf(seg[n].text, sizeof(seg[n].text), "JOB SCORE: %d", s_nJobScore);   seg[n++].rgba = 0xE6322DFF; }   // red
    if(n)                                       { snprintf(seg[n].text, sizeof(seg[n].text), "SCORE: %d", s_nMyScore);        seg[n++].rgba = 0x37C846FF; }   // green
    return n;
}
static int ScoreBarSegs(Game::HudSeg* out, int maxOut)
{
    if(maxOut < 4 || !Game::IsReady() || JoinFlowActive() || client.GetState() != NET_CONNECTED) return 0;
    if(UI::PricedownFont()) return 0;   // the Pricedown .ttf is drawn by MP below, not by the game
    return ScoreSegsBuild(out);
}

// 3.01: the race scoreboard - top centre, a little above the crosshair: POS / CP / TIME
// (derby: cars left / time, job: checkpoint / time). The lobby / queue line sits above it.
#define MP_EVBOX_Y 132.0f   // 640 x 448 textdraw space; the crosshair is at about y 180
static void UpdateEventTextDraws()
{
    const uint64_t now = MP_NowMs();
    if(s_Ev.kind == EV_NONE || s_Ev.kind == EV_DM)
    {
        TdHide(TD_EVENT_BOX);
        if(s_RQ.on && s_Ev.kind == EV_NONE)
        {
            char q[160]; snprintf(q, sizeof(q), s_RQ.count < 2 ? "~y~RACE QUEUE~n~~w~Waiting for players (%d/%d)  -  /leave" : "~y~RACE QUEUE~n~~w~%d/%d players - starts soon  -  /leave", s_RQ.count, s_RQ.need);   // 3.36: it never starts alone
            PktTextDraw d = TdMake(q, 320.0f, 86.0f, 1.15f, 1, 2); TdSet(TD_EVENT_TOP, d);
        }
        else TdHide(TD_EVENT_TOP);
        return;
    }
    char top[160] = "", box[200] = "";
    const char* name = s_Ev.kind == EV_DERBY ? kDerbies[s_Ev.idx].name : s_Ev.kind == EV_RACE ? kSaRaces[s_Ev.idx].name : kRoutes[s_Ev.idx].name;
    if(s_Ev.goAt && now < s_Ev.goAt)
    {
        int left = (int)((s_Ev.goAt - now + 999) / 1000);
        if(s_Ev.kind == EV_ROUTE) snprintf(top, sizeof(top), "~b~~h~%s", name);
        else if(s_Ev.kind == EV_RACE) snprintf(top, sizeof(top), "~y~%s~n~~w~GO in %d  -  %d racers", name, left, s_Ev.rivals + 1);
        else { int others = EventOthers(EVK_DERBY, s_Ev.idx); snprintf(top, sizeof(top), "~y~%s~n~~w~starts in %d  -  %d car%s", name, left, others + 1, others ? "s" : ""); }
    }
    else
    {
        char t[24]; FmtTime((uint32_t)(now - s_Ev.startMs), t, sizeof(t));
        if(s_Ev.kind == EV_RACE)
        {
            int total = 1, pos = RacePosition(&total);
            if(total < s_Ev.rivals + 1) total = s_Ev.rivals + 1;
            snprintf(box, sizeof(box), "POS ~y~%d~w~/%d   CP ~y~%d~w~/%d   TIME ~y~%s", pos, total, s_Ev.cp, kSaRaces[s_Ev.idx].n - 1, t);
        }
        else if(s_Ev.kind == EV_ROUTE) snprintf(box, sizeof(box), "~b~~h~%s~w~   CP ~y~%d~w~/%d   TIME ~y~%s", name, s_Ev.cp, kRoutes[s_Ev.idx].n - 1, t);
        else snprintf(box, sizeof(box), "~r~~h~DERBY~w~   CARS ~y~%d~w~/%d   TIME ~y~%s", DerbyAlive() + 1, s_Ev.rivals + 1, t);
    }
    if(top[0]) { PktTextDraw d = TdMake(top, 320.0f, 86.0f, 1.25f, 1, 2); TdSet(TD_EVENT_TOP, d); } else TdHide(TD_EVENT_TOP);
    if(box[0])
    {
        PktTextDraw d = TdMake(box, 320.0f, MP_EVBOX_Y, 1.2f, 2, 2);
        d.useBox = 1; d.boxColor = 0x00000088; d.boxX = 0;
        TdSet(TD_EVENT_BOX, d);
    }
    else TdHide(TD_EVENT_BOX);
}

static void RenderGames()
{
    static bool s_bProv = false;
    if(!s_bProv) { Game::SetHudTextProvider(ScoreBarSegs); s_bProv = true; }
    if(!Game::IsReady() || JoinFlowActive() || client.GetState() != NET_CONNECTED) return;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImFont* pd = UI::PricedownFont();
    if(pd || !Game::HudTextActive())   // Pricedown .ttf (or Arial if neither it nor the game font works)
    {
        ImFont* f = pd ? pd : UI::Font();
        const float fs = pd ? UI::S(34.0f) : UI::S(24.0f), gap = UI::S(16);
        Game::HudSeg seg[4];
        int n = ScoreSegsBuild(seg);   // 3.04: only enabled gamemodes
        float total = 0, w[4];
        for(int i = 0; i < n; ++i) { w[i] = f->CalcTextSizeA(fs, FLT_MAX, 0, seg[i].text).x; total += w[i] + (i ? gap : 0); }
        float x = ds.x - total - UI::S(24), y = UI::S(6);
        const float o = fmaxf(1.5f, fs * 0.07f);   // thick black edge like the HUD money
        for(int i = 0; i < n; ++i)
        {
            uint32_t c = seg[i].rgba;
            for(int dx = -1; dx <= 1; ++dx) for(int dy = -1; dy <= 1; ++dy)
                if(dx || dy) dl->AddText(f, fs, ImVec2(x + dx * o, y + dy * o), IM_COL32(0, 0, 0, 255), seg[i].text);
            dl->AddText(f, fs, ImVec2(x, y), IM_COL32(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, 255), seg[i].text);
            x += w[i] + gap;
        }
    }
    UpdateEventTextDraws();
    if(s_Ev.kind != EV_DM) return;
    char ev[160] = "";
    snprintf(ev, sizeof(ev), "%s  -  kills %d  -  streak %d  -  /leave to quit", kDms[s_Ev.idx].name, s_nDmScore, s_Ev.streak);
    float fe = UI::S(30.0f);
    ImVec2 esz = UI::Font()->CalcTextSizeA(fe, FLT_MAX, 0, ev);
    UI::OutlinedText(dl, fe, ImVec2((ds.x - esz.x) * 0.5f, ds.y * 0.14f), IM_COL32(210, 91, 255, 255), ev);
}
