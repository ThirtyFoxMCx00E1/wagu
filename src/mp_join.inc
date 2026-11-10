// 2.19: joining a server (included by mp.cpp)
//  Connect / Join  -> a full-screen black panel slides in from the left (1.0 s = 20 ticks of 50 ms),
//                     the game HUD, radar and touch controls go away (chat + title logo stay on top)
//  behind the black -> camera at the connection view (4.12.1: default / Mega Vil: a moving camera tour after connected)
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
static Vec3     s_JoinFinalP, s_JoinFinalL;   // 4.12: the server's own connection view

static float EaseInOut(float t) { if(t <= 0) return 0; if(t >= 1) return 1; return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2; }

static void StartCamFlight(const Vec3& toP, const Vec3& toL);
// 4.12: cinematic connection camera. 4.12.1: starts once you are connected (the black stays until then),
// no cinema bars, no fades: one continuous camera move. Always on the default server and Mega Vil; a custom
// server turns it on in + Add server (PktWelcome3 flag).
struct CineShot { Vec3 p0, l0, p1, l1; };
static const CineShot kCineLS[] =
{
    { { 1290.0f, -1060.0f, 120.0f }, { 1415.0f, -805.0f, 85.0f }, { 1520.0f, -1030.0f, 110.0f }, { 1415.0f, -805.0f, 85.0f } },     // the Vinewood sign
    { { 1330.0f, -1420.0f, 170.0f }, { 1560.0f, -1250.0f, 70.0f }, { 1420.0f, -1520.0f, 150.0f }, { 1560.0f, -1260.0f, 60.0f } },    // downtown skyline
    { { 2420.0f, -1720.0f, 45.0f },  { 2495.0f, -1668.0f, 14.0f }, { 2520.0f, -1735.0f, 32.0f },  { 2490.0f, -1665.0f, 14.0f } },     // Grove Street
    { { 260.0f, -1880.0f, 40.0f },   { 380.0f, -2045.0f, 12.0f },  { 470.0f, -1880.0f, 30.0f },   { 380.0f, -2045.0f, 12.0f } },      // Santa Maria pier
    { { 1850.0f, -2540.0f, 70.0f },  { 1640.0f, -2450.0f, 14.0f }, { 1700.0f, -2650.0f, 45.0f },  { 1560.0f, -2440.0f, 14.0f } },     // LS airport
};
static const CineShot kCineRp[] =
{
    { { 1380.0f, -1780.0f, 95.0f },  { 1481.0f, -1692.0f, 15.0f }, { 1580.0f, -1780.0f, 80.0f },  { 1481.0f, -1692.0f, 15.0f } },     // over Mega Vil Plaza
    { { 1440.0f, -1590.0f, 40.0f },  { 1481.0f, -1772.0f, 18.0f }, { 1520.0f, -1600.0f, 34.0f },  { 1481.0f, -1772.0f, 18.0f } },     // City Hall
    { { 1250.0f, -1500.0f, 130.0f }, { 1480.0f, -1690.0f, 30.0f }, { 1300.0f, -1700.0f, 110.0f }, { 1480.0f, -1690.0f, 25.0f } },    // the city around it
};
#define CINE_SHOT_MS 6500
#define CINE_RP_MIN_MS 9000   // 4.12.1: Mega Vil: how long the tour runs before it eases down to the Lobby Hub
// 4.12.1: one continuous camera path (no cinema bars, no dip to black, no jump cuts between shots).
// The shots become waypoints of a closed loop: p0 -> p1 of each shot (the slow dolly), then a high
// transit point over the city to the next shot. A non-uniform Catmull-Rom (Barry-Goldman) curve with
// knot times = segment durations keeps position AND speed smooth through every waypoint.
#define CINE_MAX_PTS 32
static bool     s_bCine = false;          // this join uses the cinematic camera
static bool     s_bCineFinal = false;     // the tour has ended (final flight / spawn picker flight)
static uint64_t s_nCineT0 = 0, s_nCineStartMs = 0;
static int      s_nCinePts = 0, s_nCineSeg = -1, s_nCinePlayed = 0;
static Vec3     s_CineP[CINE_MAX_PTS], s_CineL[CINE_MAX_PTS];
static float    s_CineT[CINE_MAX_PTS + 1];   // knot times (ms), s_CineT[n] = loop length
static bool     s_bCamEaseOut = false;    // 4.12.1: a flight that leaves the moving tour starts at speed (no stop)
static bool CineWanted()
{
    if(s_bRpJoin || MP_IsRpServerName(client.GetServerName())) return true;
    if(s_bBcwJoin || s_bDzJoin || s_bMw3Join) return false;
    if(s_bConnectingDefault || (client.GetState() == NET_CONNECTED && MP::IsOnDefault())) return true;
    return client.GetState() == NET_CONNECTED && (client.GetServerFlags() & MP_SRVF_CINECAM);
}
static float SmoothStep(float t) { t = t < 0 ? 0 : (t > 1 ? 1 : t); return t * t * (3 - 2 * t); }
static float CineDist(const Vec3& a, const Vec3& b) { float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z; return sqrtf(dx * dx + dy * dy + dz * dz); }
static void CineAddPt(const Vec3& p, const Vec3& l, float ms)
{
    if(s_nCinePts >= CINE_MAX_PTS) return;
    s_CineP[s_nCinePts] = p; s_CineL[s_nCinePts] = l; s_CineT[s_nCinePts + 1] = s_CineT[s_nCinePts] + ms; ++s_nCinePts;
}
static Vec3 CineLerp(const Vec3& a, const Vec3& b, float ta, float tb, float t)
{
    const float d = tb - ta; if(d <= 0.001f) return b;
    const float u = (t - ta) / d;
    return { a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u, a.z + (b.z - a.z) * u };
}
// Barry-Goldman pyramid: point on segment i (P[i] -> P[i+1]) at loop time t
static Vec3 CineCurve(const Vec3* P, int i, float t)
{
    const int n = s_nCinePts;
    const int i0 = (i - 1 + n) % n, i1 = i, i2 = (i + 1) % n, i3 = (i + 2) % n;
    const float t1 = s_CineT[i], t2 = s_CineT[i + 1];
    const float t0 = t1 - (s_CineT[i0 + 1] - s_CineT[i0]);
    const float t3 = t2 + (s_CineT[i2 + 1] - s_CineT[i2]);
    const Vec3 a1 = CineLerp(P[i0], P[i1], t0, t1, t), a2 = CineLerp(P[i1], P[i2], t1, t2, t), a3 = CineLerp(P[i2], P[i3], t2, t3, t);
    const Vec3 b1 = CineLerp(a1, a2, t0, t2, t), b2 = CineLerp(a2, a3, t1, t3, t);
    return CineLerp(b1, b2, t1, t2, t);
}
static void CineBuild(const CineShot* shots, int count, int first)
{
    s_nCinePts = 0; s_CineT[0] = 0;
    for(int k = 0; k < count; ++k)
    {
        const CineShot& c = shots[(first + k) % count];
        const CineShot& nx = shots[(first + k + 1) % count];
        CineAddPt(c.p0, c.l0, CINE_SHOT_MS);                    // the slow dolly of this shot
        const float d = CineDist(c.p1, nx.p0);
        const float ms = std::max(4500.0f, std::min(11000.0f, d / 0.11f));   // ~110 m/s over the city
        if(d > 400.0f)
        {   // a long hop: go through a point high over the city between the two shots
            const Vec3 mp = { (c.p1.x + nx.p0.x) * 0.5f, (c.p1.y + nx.p0.y) * 0.5f, std::max(c.p1.z, nx.p0.z) + std::min(140.0f, d * 0.08f) };
            const Vec3 ml = { (c.l1.x + nx.l0.x) * 0.5f, (c.l1.y + nx.l0.y) * 0.5f, (c.l1.z + nx.l0.z) * 0.5f };
            CineAddPt(c.p1, c.l1, ms * 0.5f);
            CineAddPt(mp, ml, ms * 0.5f);
        }
        else CineAddPt(c.p1, c.l1, ms);
    }
}
static void CineStreamAhead(int ped, int seg)
{   // stream the next waypoints in before the camera gets there (the hidden player waits above them)
    const int n = s_nCinePts; if(n <= 0) return;
    const int a = (seg + 1) % n, b = (seg + 2) % n;
    Game::PreloadArea(s_CineL[a], false);
    Game::PreloadArea(s_CineL[b], false);
    if(ped > 0)
    {
        Game::FreezeChar(ped, false);
        Game::PlaceChar(ped, { s_CineL[a].x, s_CineL[a].y, s_CineL[a].z + 40.0f });
        Game::FreezeChar(ped, true);
    }
}
static void CineStart(int ped, uint64_t now)
{
    const bool rp = s_bRpJoin || MP_IsRpServerName(client.GetServerName());
    if(rp) CineBuild(kCineRp, (int)(sizeof(kCineRp) / sizeof(kCineRp[0])), 0);
    else   CineBuild(kCineLS, (int)(sizeof(kCineLS) / sizeof(kCineLS[0])), 3);   // starts at the Santa Maria pier, then flies on
    s_nCineT0 = now; s_nCineStartMs = now; s_nCineSeg = -1; s_nCinePlayed = 0;
    s_bCine = s_nCinePts >= 4; s_bCineFinal = false; s_bCamEaseOut = false; s_nCamMs = 0;
    if(!s_bCine) return;
    Game::PreloadArea(s_CineL[0], true);   // still under the black: the first view is streamed in
    CineStreamAhead(ped, 0);
}
// every tick while the tour runs: move the camera along the loop
static void CineTick(int ped, uint64_t now)
{
    if(!s_bCine || s_bCineFinal || s_nCinePts < 4) return;
    const float L = s_CineT[s_nCinePts];
    float t = fmodf((float)(now - s_nCineT0), L);
    int seg = 0; while(seg < s_nCinePts - 1 && t >= s_CineT[seg + 1]) ++seg;
    if(seg != s_nCineSeg) { if(s_nCineSeg >= 0) ++s_nCinePlayed; s_nCineSeg = seg; CineStreamAhead(ped, seg); }
    s_CamNowP = CineCurve(s_CineP, seg, t);
    s_CamNowL = CineCurve(s_CineL, seg, t);
    Game::SetFixedCamera(s_CamNowP, s_CamNowL);
}
// leave the tour: one flight from where the camera is now (it keeps its speed at the start)
static void CineLeave(const Vec3& toP, const Vec3& toL, int ms)
{
    s_bCineFinal = true;
    StartCamFlight(toP, toL);
    s_bCamEaseOut = true; s_fCamArc = 0.0f;
    if(ms > 0) s_nCamMs = ms;
}


static void SpotCamera(int i, Vec3& cam, Vec3& look)
{
    const SpawnSpot& s = kSpawns[i];
    float h = s.heading * 0.0174533f;
    float fx = -sinf(h), fy = cosf(h);           // where the player will look
    float rx = cosf(h), ry = sinf(h);
    cam  = { s.x + fx * 7.5f + rx * 4.0f, s.y + fy * 7.5f + ry * 4.0f, s.z + 3.2f };
    look = { s.x, s.y, s.z + 0.6f };
}

static uint64_t s_nJoinStartMs = 0;   // 3.53: watchdog of the black connecting screen
static void JoinOverlayStart()
{
    if(s_nJoinPhase != JP_NONE) return;
    if(!Game::IsReady()) return;               // still on the loading screen: plain join
    s_nJoinPhase = JP_COVER; s_nJoinT0 = MP_NowMs(); s_nJoinStartMs = s_nJoinT0;
    s_bJoinConnected = false; s_bJoinCamSet = false; s_bSpawnNow = false; s_nSpawnStep = 0;
    s_nSpawnSel = 0; s_bCine = false; s_bCineFinal = false;
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
    s_bCine = false;
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
    s_nCamT0 = MP_NowMs(); s_bCamEaseOut = false;
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
    // 3.53: never hang on the black "Connecting..." screen (a friend on a hotspot looked frozen): no answer
    // within 25 s = give the game back; the join keeps trying in the background and finishes without it
    // 4.10: black screen fix. A quiet rejoin (24/7 shift change, host hand-off, duplicate merge, "moving to the
    // 24/7 server") skips the loading feed, so FinishJoin() never ran and nothing lifted the black panel - and
    // the 25 s watchdog below was off once the client said CONNECTED. Connected + welcomed + no feed running =
    // the join is done: reveal the camera now.
    if((s_nJoinPhase == JP_COVER || s_nJoinPhase == JP_BLACK || (s_bCine && s_nJoinPhase <= JP_WAIT)) && !s_bJoinConnected && client.GetState() == NET_CONNECTED
       && s_nFeedStep < 0 && s_nFeedRes < 0 && now - s_nJoinStartMs > 1500)
    {
        static int s_nQuietLogs = 0;
        if(s_nQuietLogs < 4) { ++s_nQuietLogs; logger->Info("4.10: join screen lifted (connected, no loading feed)"); }
        s_bJoinConnected = true;
    }
    // 4.10: the hard cap: connected or not, the black panel never stays more than 25 s
    if((s_nJoinPhase == JP_COVER || s_nJoinPhase == JP_BLACK || (s_bCine && s_nJoinPhase <= JP_WAIT)) && !s_bJoinConnected && client.GetState() == NET_CONNECTED && now - s_nJoinStartMs > 25000)
        s_bJoinConnected = true;
    if((s_nJoinPhase == JP_COVER || s_nJoinPhase == JP_BLACK || (s_bCine && s_nJoinPhase <= JP_WAIT)) && !s_bJoinConnected && client.GetState() != NET_CONNECTED && now - s_nJoinStartMs > 25000)
    {
        JoinOverlayCancel();
        SysMsg(COL_YELLOW, "Still connecting - you can move meanwhile. On a hotspot: the host should keep the game open and the screen on.");
        return;
    }

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
                else if(s_bDzJoin) { DzJoinCam(jcP, jcL); park = { jcP.x, jcP.y, -100.0f }; }   // 3.58: Project Day Z: the cabin
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
                s_JoinFinalP = jcP; s_JoinFinalL = jcL;
            }
            // 4.12.1: the black stays until "Connected successfully!" - only then the camera tour starts (under the
            // black, so the first frame you see is already moving), and the black slides away from it
            if(s_bJoinConnected && t >= 200)
            {
                if(!s_bCine && CineWanted()) CineStart(ped, now);
                s_nJoinPhase = JP_REVEAL; s_nJoinT0 = now;
            }
            break;
        case JP_REVEAL:
            if(t >= JOIN_SLIDE_MS) { s_nJoinPhase = JP_WAIT; s_nJoinT0 = now; }
            break;
        case JP_WAIT:
            if(s_bCine && (s_bRpJoin || MP_IsRpServerName(client.GetServerName())))
            {   // 4.12.1: Mega Vil: the tour glides over the plaza, then eases (without stopping) down to the Lobby Hub
                if(!s_bCineFinal && s_nFeedRes < 0 && s_nFeedStep < 0 && now - s_nCineStartMs >= CINE_RP_MIN_MS)
                {
                    Vec3 fp, fl; RpJoinCam(fp, fl);
                    Game::PreloadArea(fl, false);
                    CineLeave(fp, fl, 3200);
                    s_nJoinT0 = now; t = 0;
                }
                if(!s_bCineFinal || s_nCamMs > 0 || t < 400) break;
                s_bCine = false;
                if(t < JOIN_BUTTONS_MS) s_nJoinT0 = now - JOIN_BUTTONS_MS;
                t = JOIN_BUTTONS_MS;
            }
            // 4.12.1: default server / custom servers with the cinematic camera: the tour keeps moving behind
            // the < Spawn > picker (it opens after JOIN_BUTTONS_MS as before) until you pick a place
            if(t >= JOIN_BUTTONS_MS && !AuthHoldsJoin(now))   // 3.21.6: login first
            {
                if(s_bDzJoin || MP_IsDzServerName(client.GetServerName()))
                {   // 3.58: Project Day Z: the cabin connection screen (PLAY / RULES / CREDITS / SETTINGS)
                    s_bJoinCamSet = false; s_bJoinHudOff = false; s_nJoinPhase = JP_NONE;
                    DzJoinTakeover(ped);
                    return;
                }
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
                if(s_bCine && !s_bCineFinal) { CineLeave(cp, cl, 0); s_bCine = false; }   // 4.12.1: off the moving tour, no stop
                else StartCamFlight(cp, cl);
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
                s_bJoinCamSet = false; s_bCine = false;
                Game::RestoreCamera();
                JoinRestoreGame(false);
                s_nJoinPhase = JP_NONE;
                SysMsg(COL_SYSTEM, "Spawned at %s.", sp.name);
                return;
            }
            break;
    }
    if(s_bJoinCamSet && s_bCine && !s_bCineFinal && s_nJoinPhase >= JP_REVEAL) CineTick(ped, now);   // 4.12.1: only after connected
    // eased camera flight between spots
    if(s_bJoinCamSet && s_nCamMs > 0)
    {
        float k = (float)(now - s_nCamT0) / (float)s_nCamMs;
        if(k >= 1.0f) { k = 1.0f; s_nCamMs = 0; }
        float e = s_bCamEaseOut ? 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k) : EaseInOut(k), arc = sinf(k * 3.14159f) * s_fCamArc;
        if(k >= 1.0f) s_bCamEaseOut = false;
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
        case JP_REVEAL: x0 = e; x1 = 1; return k < 1.0f;   // 4.12.1: opens (after connected) on the moving camera
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
