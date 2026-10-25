// 2.19: joining a server (included by mp.cpp)
//  Connect / Join  -> a full-screen black panel slides in from the left (1.0 s = 20 ticks of 50 ms),
//                     the game HUD, radar and touch controls go away (chat + title logo stay on top)
//  behind the black -> camera at the SA-MP connection view (Santa Maria Beach)
//  "Connected successfully!" -> the black slides out again, starting at the left
//  3 s later        -> ( <  Spawn  > ): 34 spawn spots, the camera eases from one to the next
//  Spawn            -> you are put there, camera / HUD / controls come back

struct SpawnSpot { const char* name; const char* tag; float x, y, z, heading; bool findGround; };
static const SpawnSpot kSpawns[] =
{
    // ---- Los Santos (14) ----
    { "Grove Street",            "grove_street",   2487.60f, -1667.40f,  13.40f, 180.0f, false },
    { "Ganton",                  "ganton",         2306.83f, -1675.43f,  13.92f,   2.6f, false },
    { "Idlewood",                "idlewood",       2015.36f, -1717.25f,  13.55f,  93.4f, false },
    { "Willowfield",             "willowfield",    2489.52f, -1957.93f,  13.59f,   2.3f, false },
    { "East Beach",              "east_beach",     2770.54f, -1628.31f,  12.18f,   5.0f, false },
    { "Pershing Square (LSPD)",  "lspd",           1654.71f, -1656.85f,  22.52f, 178.0f, false },
    { "Conference Center",       "conference",     1219.09f, -1812.81f,  16.59f, 190.0f, false },
    { "Market",                  "market",         1133.82f, -1272.16f,  13.55f, 192.4f, false },
    { "Downtown Los Santos",     "downtown_ls",    1508.68f, -1059.08f,  25.06f,   1.8f, false },
    { "Vinewood",                "vinewood",       1421.08f,  -885.34f,  50.65f,   3.7f, false },
    { "Rodeo",                   "rodeo",           477.60f, -1496.62f,  20.43f, 267.0f, false },
    { "Richman",                 "richman",         255.46f, -1366.33f,  53.11f, 312.1f, false },
    { "El Corona",               "el_corona",      1751.11f, -2106.45f,  13.55f, 183.2f, false },
    { "Ocean Docks",             "ocean_docks",    2215.52f, -2627.82f,  13.55f, 273.8f, false },
    // ---- countryside (5) ----
    { "Dillimore",               "dillimore",       661.40f,  -573.50f,  16.34f,  90.0f, true  },
    { "Palomino Creek",          "palomino_creek", 2269.50f,   -75.00f,  26.80f,   0.0f, true  },
    { "Angel Pine",              "angel_pine",    -2161.00f, -2385.00f,  30.63f, 140.0f, true  },
    { "Mount Chiliad",           "mount_chiliad", -2318.00f, -1636.00f, 483.70f,   0.0f, true  },
    { "Fort Carson",             "fort_carson",    -200.00f,  1125.00f,  19.74f,   0.0f, true  },
    // ---- San Fierro (7) ----
    { "Doherty",                 "doherty",       -2039.36f,   -97.72f,  35.16f,   7.5f, false },
    { "Downtown San Fierro",     "downtown_sf",   -1956.14f,   287.11f,  35.47f,  90.4f, false },
    { "Juniper Hill",            "juniper_hill",  -1983.35f,  1117.06f,  53.12f, 271.2f, false },
    { "Pier 69",                 "pier_69",       -1660.23f,  1382.67f,   9.80f, 136.3f, false },
    { "Avispa Country Club",     "golf_sf",       -2723.46f,  -314.81f,   7.18f,  43.6f, false },
    { "Garcia",                  "garcia",        -2320.53f,  -180.39f,  35.31f, 179.7f, false },
    { "Easter Bay Airport",      "sf_airport",    -1427.09f,  -288.94f,  14.15f, 137.1f, false },
    // ---- Las Venturas (8) ----
    { "The Strip (Pyramid)",     "the_strip",      2299.54f,  1451.42f,  10.82f, 269.1f, false },
    { "Royal Casino",            "royal_casino",   2087.99f,  1516.53f,  10.82f,  48.9f, false },
    { "The Visage",              "the_visage",     2025.68f,  1916.44f,  12.34f, 272.6f, false },
    { "Come-A-Lot",              "come_a_lot",     2139.18f,   987.80f,  10.82f,   0.2f, false },
    { "Las Venturas Hospital",   "lv_hospital",    1609.59f,  1820.96f,  10.83f, 348.2f, false },
    { "LV Airport",              "lv_airport",     1673.23f,  1316.11f,  10.82f, 177.7f, false },
    { "Emerald Isle",            "emerald_isle",   2097.69f,  2491.33f,  14.84f, 181.8f, false },
    { "V-Rock Hotel",            "v_rock",         2633.32f,  2349.71f,  10.67f, 178.7f, false },
};
#define MP_SPAWN_COUNT ((int)(sizeof(kSpawns) / sizeof(kSpawns[0])))
static_assert(sizeof(kSpawns) / sizeof(kSpawns[0]) == 34, "34 spawn spots");

// SA-MP's connection camera (SetPlayerCameraPos / LookAt): Santa Maria Beach from the airport side
static const Vec3 kJoinCamPos  = { 1093.0f, -2036.0f, 90.0f };
static const Vec3 kJoinCamLook = {  384.0f, -1557.0f, 20.0f };
static const Vec3 kJoinPedPark = {  836.0f, -2048.0f, -100.0f };   // Santa Maria pier: keeps the beach streamed in

enum { JP_NONE, JP_COVER, JP_BLACK, JP_REVEAL, JP_WAIT, JP_PICK };
#define JOIN_SLIDE_MS   1000   // 20 ticks x 50 ms
#define JOIN_BUTTONS_MS 3000
static int      s_nJoinPhase = JP_NONE;
static uint64_t s_nJoinT0 = 0;
static bool     s_bJoinConnected = false;   // FinishJoin happened (maybe while still sliding in)
static bool     s_bJoinCamSet = false, s_bJoinHudOff = false;
static int      s_nSpawnSel = 0;
static bool     s_bSpawnNow = false;
static int      s_nSpawnStep = 0;            // -1 / +1 pressed (done on the game tick)
// camera flight
static Vec3     s_CamFromP, s_CamFromL, s_CamToP, s_CamToL, s_CamNowP, s_CamNowL;
static uint64_t s_nCamT0 = 0; static int s_nCamMs = 0; static float s_fCamArc = 0;

static float EaseInOut(float t) { if(t <= 0) return 0; if(t >= 1) return 1; return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

static void SpotCamera(int i, Vec3& cam, Vec3& look)
{
    const SpawnSpot& s = kSpawns[i];
    float h = s.heading * 0.0174533f;
    float fx = -sinf(h), fy = cosf(h);           // where the player will look
    float rx = cosf(h), ry = sinf(h);
    cam  = { s.x + fx * 7.5f + rx * 4.0f, s.y + fy * 7.5f + ry * 4.0f, s.z + 3.2f };
    look = { s.x, s.y, s.z + 0.6f };
}

static void JoinOverlayStart()
{
    if(s_nJoinPhase != JP_NONE) return;
    if(!Game::IsReady()) return;               // still on the loading screen: plain join
    s_nJoinPhase = JP_COVER; s_nJoinT0 = MP_NowMs();
    s_bJoinConnected = false; s_bJoinCamSet = false; s_bSpawnNow = false; s_nSpawnStep = 0;
    s_nSpawnSel = 0;
    FloatMenu::CloseAll();
}

static void JoinRestoreGame(bool teleportGrove)
{
    int ped = Game::LocalPed();
    if(s_bJoinCamSet && ped > 0)
    {
        Game::FreezeChar(ped, false);
        Game::SetCharVisible(ped, !s_Cheat.invisible);
        if(teleportGrove) Game::Teleport(ped, kGroveStreet, kGroveHeading);
    }
    if(s_bJoinCamSet) Game::RestoreCamera();
    if(s_bJoinHudOff) { Game::ShowGameHud(true); Game::SetPlayerControl(true); }
    s_bJoinCamSet = false; s_bJoinHudOff = false;
}

static void JoinOverlayCancel()
{
    if(s_nJoinPhase == JP_NONE) return;
    JoinRestoreGame(false);
    s_nJoinPhase = JP_NONE;
}

static bool AuthHoldsJoin(uint64_t now);   // 3.21.6 (mp_auth.inc)
static bool JoinFlowActive() { return s_nJoinPhase != JP_NONE; }
static void JoinMarkConnected() { s_bJoinConnected = true; }

static void StartCamFlight(const Vec3& toP, const Vec3& toL)
{
    s_CamFromP = s_CamNowP; s_CamFromL = s_CamNowL; s_CamToP = toP; s_CamToL = toL;
    float dx = toP.x - s_CamFromP.x, dy = toP.y - s_CamFromP.y, d = sqrtf(dx * dx + dy * dy);
    s_nCamMs = d < 400 ? 1600 : (d < 1500 ? 2200 : 3000);
    s_fCamArc = d < 150 ? 4.0f : (d * 0.12f > 260.0f ? 260.0f : d * 0.12f);   // fly up a little over long hops
    s_nCamT0 = MP_NowMs();
}

// game tick
static void JoinFlowProcess(int ped, uint64_t now)
{
    if(s_nJoinPhase == JP_NONE) return;
    uint64_t t = now - s_nJoinT0;
    if(!s_bJoinHudOff) { Game::ShowGameHud(false); Game::SetPlayerControl(false); s_bJoinHudOff = true; }
    else if(now % 1000 < 40) { Game::ShowGameHud(false); Game::SetPlayerControl(false); }   // scripts may switch them back
    if(client.GetState() == NET_DISCONNECTED && !MP::IsSearching() && s_nFeedStep < 0 && s_nFeedRes < 0 && !s_bTakeoverPending)
    { JoinOverlayCancel(); return; }   // join failed / left: give the game back

    switch(s_nJoinPhase)
    {
        case JP_COVER:
            if(t >= JOIN_SLIDE_MS) { s_nJoinPhase = JP_BLACK; s_nJoinT0 = now; }
            break;
        case JP_BLACK:
            if(!s_bJoinCamSet)
            {   // all black now: park the player out of sight and point the camera at the beach
                Vec3 jcP = kJoinCamPos, jcL = kJoinCamLook, park = kJoinPedPark;
                if(s_bBcwJoin) { BcwJoinCam(jcP, jcL); park = { jcP.x, jcP.y, -100.0f }; }
                else if(s_bRpJoin) { RpJoinCam(jcP, jcL); park = { jcP.x, jcP.y, -100.0f }; }   // 3.25: Mega Vil: over the Lobby Hub
                else if(s_bMw3Join) { Mw3JoinCam(jcP, jcL); park = { jcP.x, jcP.y, -100.0f }; }   // 3.23: MW3: the LS Docks & Warehouses   // 3.04: Brief Case War: no beach, the war map (Area 69)
                Game::PreloadArea(jcL, false);
                Game::PreloadArea({ park.x, park.y, 15.0f }, true);   // collision first: then "on the ground" works
                Game::PlaceChar(ped, park);
                Game::FreezeChar(ped, true);
                Game::SetCharVisible(ped, false);
                s_CamNowP = jcP; s_CamNowL = jcL; s_nCamMs = 0;
                Game::SetFixedCamera(jcP, jcL);
                s_bJoinCamSet = true;
            }
            if(s_bJoinConnected && t >= 200) { s_nJoinPhase = JP_REVEAL; s_nJoinT0 = now; }
            break;
        case JP_REVEAL:
            if(t >= JOIN_SLIDE_MS) { s_nJoinPhase = JP_WAIT; s_nJoinT0 = now; }
            break;
        case JP_WAIT:
            if(t >= JOIN_BUTTONS_MS && !AuthHoldsJoin(now))   // 3.21.6: login first
            {
                if(s_bRpJoin || MP_IsRpServerName(client.GetServerName()))
                {   // 3.25: Mega Vil: the Lobby Hub < SPAWN > (or straight home) replaces the spawn picker
                    s_bJoinCamSet = false; s_bJoinHudOff = false; s_nJoinPhase = JP_NONE;
                    RpJoinTakeover(ped);
                    return;
                }
                if(s_bMw3Join || MP_IsMw3ServerName(client.GetServerName()))
                {   // 3.23: MW3: team / class buttons replace < Spawn >
                    s_bJoinCamSet = false; s_bJoinHudOff = false; s_nJoinPhase = JP_NONE;
                    Mw3JoinTakeover(ped);
                    return;
                }
                if(s_bBcwJoin || MP_IsBcwServerName(client.GetServerName()))
                {   // 3.04: Brief Case War: the team buttons replace < Spawn > (the war module keeps the camera)
                    s_bJoinCamSet = false; s_bJoinHudOff = false; s_nJoinPhase = JP_NONE;
                    BcwJoinTakeover(ped);
                    return;
                }
                s_nJoinPhase = JP_PICK; s_nJoinT0 = now;
            }
            break;
        case JP_PICK:
            if(s_nSpawnStep)
            {
                s_nSpawnSel = (s_nSpawnSel + s_nSpawnStep + MP_SPAWN_COUNT) % MP_SPAWN_COUNT;
                s_nSpawnStep = 0;
                const SpawnSpot& sp = kSpawns[s_nSpawnSel];
                Vec3 cp, cl; SpotCamera(s_nSpawnSel, cp, cl);
                StartCamFlight(cp, cl);
                // stream the new place in while the camera travels (the hidden player waits there)
                Game::PreloadArea({ sp.x, sp.y, sp.z }, false);
                Game::FreezeChar(ped, false);
                Game::PlaceChar(ped, { sp.x, sp.y, sp.z + 40.0f });
                Game::FreezeChar(ped, true);
            }
            if(s_bSpawnNow)
            {
                s_bSpawnNow = false;
                const SpawnSpot& sp = kSpawns[s_nSpawnSel];
                Game::FreezeChar(ped, false);
                Game::SetCharVisible(ped, !s_Cheat.invisible);
                Vec3 at = { sp.x, sp.y, sp.findGround ? -100.0f : sp.z };
                Game::PreloadArea({ sp.x, sp.y, sp.z }, true);
                Game::Teleport(ped, sp.findGround ? Vec3{ sp.x, sp.y, sp.z } : at, sp.heading);
                if(sp.findGround) Game::PlaceChar(ped, at);
                s_bJoinCamSet = false;
                Game::RestoreCamera();
                JoinRestoreGame(false);
                s_nJoinPhase = JP_NONE;
                SysMsg(COL_SYSTEM, "Spawned at %s.", sp.name);
                return;
            }
            break;
    }
    // eased camera flight between spots
    if(s_bJoinCamSet && s_nCamMs > 0)
    {
        float k = (float)(now - s_nCamT0) / (float)s_nCamMs;
        if(k >= 1.0f) { k = 1.0f; s_nCamMs = 0; }
        float e = EaseInOut(k), arc = sinf(k * 3.14159f) * s_fCamArc;
        s_CamNowP = { s_CamFromP.x + (s_CamToP.x - s_CamFromP.x) * e, s_CamFromP.y + (s_CamToP.y - s_CamFromP.y) * e,
                      s_CamFromP.z + (s_CamToP.z - s_CamFromP.z) * e + arc };
        s_CamNowL = { s_CamFromL.x + (s_CamToL.x - s_CamFromL.x) * e, s_CamFromL.y + (s_CamToL.y - s_CamFromL.y) * e,
                      s_CamFromL.z + (s_CamToL.z - s_CamFromL.z) * e };
        Game::SetFixedCamera(s_CamNowP, s_CamNowL);
    }
}

// ---- drawing (HUD) --------------------------------------------------------------------------
// black panel: [x0, x1] in 0..1 of the screen width, false = nothing to draw
bool MP::JoinOverlay(float& x0, float& x1)
{
    uint64_t t = MP_NowMs() - s_nJoinT0;
    float k = (float)t / (float)JOIN_SLIDE_MS; if(k > 1) k = 1;
    float e = EaseInOut(k);
    switch(s_nJoinPhase)
    {
        case JP_COVER:  x0 = 0; x1 = e; return true;
        case JP_BLACK:  x0 = 0; x1 = 1; return true;
        case JP_REVEAL: x0 = e; x1 = 1; return k < 1.0f;
        default: return false;
    }
}

const char* MP::SpawnSpotName(int i) { return (i >= 0 && i < MP_SPAWN_COUNT) ? kSpawns[i].name : ""; }
int  MP::SpawnSpotCount() { return MP_SPAWN_COUNT; }
bool MP::SpawnPickerShown() { return s_nJoinPhase == JP_PICK; }
int  MP::SpawnSelected() { return s_nSpawnSel; }
void MP::SpawnStep(int d) { if(s_nJoinPhase == JP_PICK) s_nSpawnStep = d < 0 ? -1 : 1; }
void MP::SpawnConfirm() { if(s_nJoinPhase == JP_PICK) s_bSpawnNow = true; }
float MP::SpawnPickerAlpha() { if(s_nJoinPhase != JP_PICK) return 0; float a = (MP_NowMs() - s_nJoinT0) / 400.0f; return a > 1 ? 1 : a; }

// /tp :place
static int FindSpawnTag(const char* t)
{
    if(!t) return -1;
    while(*t == ' ' || *t == ':') ++t;
    for(int i = 0; i < MP_SPAWN_COUNT; ++i) if(!strcasecmp(kSpawns[i].tag, t)) return i;
    for(int i = 0; i < MP_SPAWN_COUNT; ++i) if(!strncasecmp(kSpawns[i].tag, t, strlen(t)) && *t) return i;
    return -1;
}
