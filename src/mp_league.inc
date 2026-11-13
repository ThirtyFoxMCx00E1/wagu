// MP 4.13.4 (included by mp.cpp): Rocket League (4.13.10 name; was San Andreas Online League) - the phone side of Rocket Soccer.
//
// The HUD is the one of the original SA:O / MTA Neon "GTA Rocket League" (DryxioGTA,
// github.com/ThirtyFoxMCx00E1/san-andreas-online):
//   lobby (Grove Street at night): the cars parked with their RL name in white caps over them, cyan
//     "ROCKET LEAGUE" at the top, a small "Choose a car. Settings: ..." line top left and
//     "Choose a car, then your game mode and arena" at the bottom
//   match: a small white status line at the top centre with "0 : 0" under it, the boost gauge at the
//     bottom right (a thick dark ring, 12 cream ticks, a big squared white number, a small orange
//     BOOST), navy name boxes over the cars, a small control hint at the bottom, the build bottom right
// The server owns the ball (server_league.inc); the phone predicts it between the 10 Hz states
// and shows it as GTA object 1946 with the real Rocket League ball mesh + texture (4.13.13, RlBall); the old
// drawn ball (a dark sphere with teal lights) is only the fallback.

static const Vec3  kLgLobby = { 2495.20f, -1672.60f, 13.34f };    // Grove Street, between the houses
static const float kLgLobbyHeading = 0.0f;
static const Vec3  kLgLobbyCars[LG_CAR_COUNT] = {
    { 2486.0f, -1664.0f, 13.34f }, { 2491.5f, -1660.5f, 13.34f }, { 2498.0f, -1659.5f, 13.34f },
    { 2504.0f, -1661.5f, 13.34f }, { 2509.0f, -1665.5f, 13.34f }, { 2480.5f, -1668.5f, 13.34f },
};
static const float kLgLobbyCarHead[LG_CAR_COUNT] = { 210, 190, 180, 165, 140, 235 };
// boost pads (relative): 6 big (100) like RL Soccar + small ones (+12)
struct LgPad { float x, y; bool big; uint64_t backAt; };
static LgPad s_LgPads[] = {   // 4.13.10: the real Soccar spots (uu / 100, x along the length)
    { -40.96f * LG_SCALE, -30.72f * LG_SCALE, true, 0 }, { -40.96f * LG_SCALE, 30.72f * LG_SCALE, true, 0 }, { 40.96f * LG_SCALE, -30.72f * LG_SCALE, true, 0 }, { 40.96f * LG_SCALE, 30.72f * LG_SCALE, true, 0 }, { 0 * LG_SCALE, -35.84f * LG_SCALE, true, 0 }, { 0 * LG_SCALE, 35.84f * LG_SCALE, true, 0 },
    { -10.24f * LG_SCALE, 0 * LG_SCALE, false, 0 }, { 10.24f * LG_SCALE, 0 * LG_SCALE, false, 0 }, { -23.04f * LG_SCALE, 0 * LG_SCALE, false, 0 }, { 23.04f * LG_SCALE, 0 * LG_SCALE, false, 0 },
    { -10.24f * LG_SCALE, -17.92f * LG_SCALE, false, 0 }, { -10.24f * LG_SCALE, 17.92f * LG_SCALE, false, 0 }, { 10.24f * LG_SCALE, -17.92f * LG_SCALE, false, 0 }, { 10.24f * LG_SCALE, 17.92f * LG_SCALE, false, 0 },
    { -28.16f * LG_SCALE, -9.4f * LG_SCALE, false, 0 }, { -28.16f * LG_SCALE, 9.4f * LG_SCALE, false, 0 }, { 28.16f * LG_SCALE, -9.4f * LG_SCALE, false, 0 }, { 28.16f * LG_SCALE, 9.4f * LG_SCALE, false, 0 },
    { -40.96f * LG_SCALE, -11.0f * LG_SCALE, false, 0 }, { -40.96f * LG_SCALE, 11.0f * LG_SCALE, false, 0 }, { 40.96f * LG_SCALE, -11.0f * LG_SCALE, false, 0 }, { 40.96f * LG_SCALE, 11.0f * LG_SCALE, false, 0 },
    { 0 * LG_SCALE, -24.0f * LG_SCALE, false, 0 }, { 0 * LG_SCALE, 24.0f * LG_SCALE, false, 0 }, { -17.92f * LG_SCALE, -30.72f * LG_SCALE, false, 0 }, { -17.92f * LG_SCALE, 30.72f * LG_SCALE, false, 0 }, { 17.92f * LG_SCALE, -30.72f * LG_SCALE, false, 0 }, { 17.92f * LG_SCALE, 30.72f * LG_SCALE, false, 0 },
};

struct LgClient
{
    bool     engaged = false;
    uint8_t  view = 0;            // 1 lobby, 2 mode card, 3 arena
    bool     freePlay = false;
    int      lobbyCar[LG_CAR_COUNT] = { 0 };
    int      myCar = 0; int carIdx = -1;
    uint64_t engagedAt = 0, nightAt = 0, keepAt = 0, touchAt = 0, lastFrame = 0, viewAt = 0;
    // server state
    PktLgState st; bool hasState = false; uint64_t stateAt = 0;
    LgBall   ball;                // predicted (server) or local (free play)
    PktLgMe  me; bool hasMe = false;
    LgRow    rows[MP_LG_MAX_PLAYERS]; int nRows = 0;
    uint8_t  teamOf[MP_MAX_PLAYERS];
    // car
    float    boost = 33; bool boostHeld = false, jumpTap = false; uint64_t jumpAt = 0; bool dodged = false; bool airborne = false;
    bool     frozen = false; uint64_t spawnAt = 0;
    bool     arenaWait = false;   // 4.13.10: the car waits (frozen) until the sky floor exists
    // banners
    char     banner[72] = ""; char bannerSub[72] = ""; uint32_t bannerCol = 0xFFFFFFFF; uint64_t bannerUntil = 0;
    char     feed[4][72] = {}; uint64_t feedAt[4] = {}; int feedN = 0;
    bool     board = false;   // scoreboard open
    int      fpScore[2] = { 0, 0 };
};
static LgClient s_Lg;

static bool LgOnServer()
{
    return client.GetState() == NET_CONNECTED && MP_IsLgServerName(client.GetServerName()[0] ? client.GetServerName() : s_szCurServer);
}
bool MP::IsOnLg() { return LgOnServer(); }
// 4.13.14: the League night is the clock the server-world lock holds (ApplyWorld, every frame). Before, the lock put
// the server's time back every frame and the lobby set 23:30 every 4 s - the sky jumped between the two
// (a black flicker at Grove Street that single player never has).
int MP::LgClockMinute() { return (s_Lg.engaged && LgOnServer() && s_Lg.view >= 1) ? (s_Lg.view == 3 ? 22 * 60 : 23 * 60 + 30) : -1; }
bool MP::LgOwnTags() { return s_Lg.engaged && LgOnServer() && s_Lg.view >= 1; }

// 4.13.10: the active arena centre: kLgCenter (the sky pitch over downtown LS), or the old LV airport apron when the
// SA-MP model pack (grass plane 19529) is missing - there is no floor in the sky without it. The server always works
// in kLgCenter coordinates (it subtracts kLgCenter from touches), so LgToCanon() converts before sending.
static const float kLgFallbackCenter[3] = { 1477.0f, 1500.0f, 10.82f };
static float s_LgC[3] = { kLgCenter[0], kLgCenter[1], kLgCenter[2] };
static bool  s_bLgSky = true;
static int   s_nLgFloor = 19529;   // 4.13.20: the floor model (19529, or the game's own 6959 court without the SA-MP pack)
static Vec3 LgWorld(float x, float y, float z) { return { s_LgC[0] + x, s_LgC[1] + y, s_LgC[2] + z }; }
static void LgToCanon(float* p) { for(int k = 0; k < 3; ++k) p[k] = p[k] - s_LgC[k] + kLgCenter[k]; }

// ---- 4.13.10: the arena objects (built progressively: CreateMapObject gives 0 while a model streams in) ----
// floor: SA-MP 19529 Plane125x125Grass1 (125 x 125 m, solid).
// 4.13.13 goals: a Rocket League goal tunnel instead of the 946/947 basketball hoops:
//   tunnel SA-MP 18776 TunnelJoinSection4 (10.42 wide x 5.62 long x 6.39 high, collision off) turned to run into
//          the end wall and scaled to the 71.4 x 35.2 x 25.7 m goal; its bottom is put on the floor with the model's
//          own bounding box (GET_MODEL_DIMENSIONS). Without 18776: wall005 (19357) panels for the back, sides, roof;
//   frame  SA-MP neon tubes (2 m): 18648 blue for the blue goal, 18647 red + 18650 yellow (= orange) for the orange
//          goal, along the posts, the crossbar and the goal line;
//   blast  SA-MP particles 18688 (fire) + 18693 (flame) inside the net for 2.5 s when a goal is scored.
//   Without the SA-MP models the goal is just the barrier box (3578) and the HUD frame.
// floor: 19529 tiles, NOT scaled (the collision of a scaled object stays 125 x 125 m): 4 x 3 tiles = 500 x 375 m.
// barriers: 3578 DockBarr1_LA (10.3 m long, solid) on the side walls, the 45 deg corners and the end walls beside
// the goal mouths, drawn 2.5x taller. The scale is visual (RwFrame), so it is applied again every 250 ms (the game
// rebuilds the matrix when an object streams back in).
struct LgObj { int model; float x, y, z, rz, sx, sy, sz; bool solid; int h; float rx = 0, ry = 0; bool onFloor = false;
               bool fit = false; float fe[3] = { 0, 0, 0 }; float fd[3] = { 0, 0, 0 }; };   // 4.13.15: fit = sized from the model's own box
// 4.13.15: fe = the size wanted along world x / y / z (0 = keep the model's own size on that axis), fd = the model's
// box (length along local x / y / z) when the game can't tell it. The centre (x, y, z) is the centre of that box.
static LgObj LgFitObj(int model, float cx, float cy, float cz, float ex, float ey, float ez, float rx, float ry, float rz, float dx, float dy, float dz)
{
    LgObj o = { model, cx, cy, cz, rz, 1.0f, 1.0f, 1.0f, false, 0 };
    o.rx = rx; o.ry = ry; o.fit = true; o.fe[0] = ex; o.fe[1] = ey; o.fe[2] = ez; o.fd[0] = dx; o.fd[1] = dy; o.fd[2] = dz;
    return o;
}
// the object's rotation (one axis at a time here: 0 / 90 deg, right-hand rule like CMatrix::SetRotate)
static void LgRotMat(float rx, float ry, float rz, float R[3][3])
{
    const float d = 0.01745329f, a = rx * d, b = ry * d, c = rz * d;
    const float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), cc = cosf(c), sc = sinf(c);
    // R = Rz * Ry * Rx
    R[0][0] = cc * cb; R[0][1] = cc * sb * sa - sc * ca; R[0][2] = cc * sb * ca + sc * sa;
    R[1][0] = sc * cb; R[1][1] = sc * sb * sa + cc * ca; R[1][2] = sc * sb * ca - cc * sa;
    R[2][0] = -sb;     R[2][1] = cb * sa;                R[2][2] = cb * ca;
}
static void LgApplyFit(LgObj& o)
{
    float mn[3], mx[3];
    if(!Game::ModelDims(o.model, mn, mx)) { for(int k = 0; k < 3; ++k) { mn[k] = -o.fd[k] * 0.5f; mx[k] = o.fd[k] * 0.5f; } }
    float R[3][3]; LgRotMat(o.rx, o.ry, o.rz, R);
    float sc[3], lc[3];
    for(int i = 0; i < 3; ++i)
    {
        const float ext = mx[i] - mn[i]; lc[i] = (mn[i] + mx[i]) * 0.5f;
        int j = 0; for(int k = 1; k < 3; ++k) if(fabsf(R[k][i]) > fabsf(R[j][i])) j = k;   // local axis i lies along world axis j
        sc[i] = (o.fe[j] > 0.0f && ext > 0.01f) ? o.fe[j] / ext : 1.0f;
    }
    o.sx = sc[0]; o.sy = sc[1]; o.sz = sc[2];
    // the model's origin is not always the middle of its box: move it so the box centre is where we want it
    for(int j = 0; j < 3; ++j)
    {
        float off = 0; for(int i = 0; i < 3; ++i) off += R[j][i] * sc[i] * lc[i];
        (&o.x)[j] -= off;
    }
}
static std::vector<LgObj> s_LgObjs;
static bool     s_bLgArenaPlanned = false;
static uint64_t s_nLgScaleAt = 0, s_nLgBuildAt = 0;
static bool LgFloorReady()   // 4.13.13: every floor tile is there
{
    if(!s_bLgSky) return true;
    int n = 0; for(const auto& o : s_LgObjs) if(o.model == s_nLgFloor) { if(o.h <= 0) return false; ++n; }
    return n > 0;
}
static void LgPlanArena()
{
    s_LgObjs.clear(); s_bLgArenaPlanned = true;
    // 4.13.20: without the SA-MP pack the sky floor is built from the game's own flat court 6959 (vegasNbball1,
    // 41.36 x 40.0 m, solid). Before, those phones got the LV airport apron, but the x4 arena (4.13.13) is far bigger
    // than the apron: it ran through the airport buildings and fences and players spawned stuck inside them.
    s_nLgFloor = Game::IsKnownModel(19529) ? 19529 : Game::IsKnownModel(6959) ? 6959 : 0;
    s_bLgSky = s_nLgFloor != 0;
    const float* c = s_bLgSky ? kLgCenter : kLgFallbackCenter;
    for(int k = 0; k < 3; ++k) s_LgC[k] = c[k];
    logger->Info("LEAGUE: arena %s (%.0f %.0f %.0f), floor %d", s_bLgSky ? "Sky Pitch above Los Santos" : "LV airport fallback (no floor model)", s_LgC[0], s_LgC[1], s_LgC[2], s_nLgFloor);
    if(s_bLgSky)   // the floor: the field + both goal boxes, unscaled tiles (solid where they look solid)
    {
        const float TL = 2 * (LG_HALF_L + LG_GOAL_DEPTH), TW = 2 * LG_HALF_W;
        // 6959: 40 m steps both ways whatever way the model lies (41.36 x 40.0: no gaps, a 1.36 m overlap); every other
        // tile 1 cm higher so the overlaps don't flicker
        const float T = s_nLgFloor == 19529 ? 125.0f : 40.0f;
        const int nx = (int)ceilf(TL / T), ny = (int)ceilf(TW / T);
        for(int ix = 0; ix < nx; ++ix) for(int iy = 0; iy < ny; ++iy)
        {
            const float z = (s_nLgFloor != 19529 && ((ix + iy) & 1)) ? 0.01f : 0.0f;
            s_LgObjs.push_back({ s_nLgFloor, (ix - (nx - 1) * 0.5f) * T, (iy - (ny - 1) * 0.5f) * T, z, 0, 1.0f, 1.0f, 1.0f, true, 0 });
        }
    }
    for(int sd = -1; sd <= 1; sd += 2)   // sd < 0: the blue goal (x-), sd > 0: the orange goal (x+)
    {
        const float mx = sd * LG_HALF_L, bx = sd * (LG_HALF_L + LG_GOAL_DEPTH), cxg = sd * (LG_HALF_L + LG_GOAL_DEPTH * 0.5f);
        const float W = 2 * LG_GOAL_HALF_W, H = LG_GOAL_H, D = LG_GOAL_DEPTH;
        // 4.13.15: the goal is built from the game's OWN models (no SA-MP model pack needed): the overhead
        // road sign frame gen_roadsign2 (1312: two posts + the beam, 26.3 x 0.7 x 8.4 m) sized to the goal
        // mouth = the posts + the crossbar, and chain-link fence panels (983 fenceshit3, 0.09 x 6.4 x 1.4 m)
        // as the net on the back, the sides and the roof. The old SA-MP tunnel (18776) is gone: without the
        // pack that id was some other model and showed up as a giant block inside the goal (screenshot).
        {
            const float post = 2.2f;   // the frame's beam thickness after sizing (~3x the model)
            s_LgObjs.push_back(LgFitObj(1312, mx, 0, H * 0.5f, post, W + post, H + post * 0.5f, 0, 0, 90.0f, 26.26731f, 0.684406f, 8.42215f));
            const float ND = 0.12f;    // the net's thickness
            const int cw = (int)ceilf(W / 36.0f), ch = (int)ceilf(H / 9.0f), cd = (int)ceilf(D / 36.0f);
            const float gw = W / cw, gh = H / ch, gd = D / cd;
            for(int a = 0; a < cw; ++a) for(int b = 0; b < ch; ++b)           // back net (across y, up z)
                s_LgObjs.push_back(LgFitObj(983, bx, -LG_GOAL_HALF_W + gw * (a + 0.5f), gh * (b + 0.5f), ND, gw, gh, 0, 0, 0, 0.0891054f, 6.4471f, 1.367302f));
            for(int sy = -1; sy <= 1; sy += 2)                                 // side nets (along x, up z)
                for(int a = 0; a < cd; ++a) for(int b = 0; b < ch; ++b)
                    s_LgObjs.push_back(LgFitObj(983, mx + sd * gd * (a + 0.5f), sy * LG_GOAL_HALF_W, gh * (b + 0.5f), gd, ND, gh, 0, 0, 90.0f, 0.0891054f, 6.4471f, 1.367302f));
            for(int a = 0; a < cw; ++a) for(int b = 0; b < cd; ++b)           // roof net (lying flat: the fence turned 90 deg on y)
                s_LgObjs.push_back(LgFitObj(983, mx + sd * gd * (b + 0.5f), -LG_GOAL_HALF_W + gw * (a + 0.5f), H, gd, gw, ND, 0, 90.0f, 0, 0.0891054f, 6.4471f, 1.367302f));
        }
        // the coloured neon tubes are SA-MP models: only with the pack (the sky pitch), never a guess
        const int neons[2] = { sd < 0 ? 18648 : 18647, sd < 0 ? 0 : 18650 };   // blue, or red + yellow = orange
        for(int k = 0; k < 2; ++k)
        {
            const int m = neons[k]; if(!m || !s_bLgSky || !Game::IsKnownModel(m)) continue;
            const float in = sd * (0.4f + 0.35f * k);    // just in front of the goal mouth (the two orange ones side by side)
            s_LgObjs.push_back({ m, mx - in, 0, H, 0, 6.0f, W / 2.0f, 6.0f, false, 0 });                             // crossbar
            s_LgObjs.push_back({ m, mx - in, 0, 0.2f, 0, 6.0f, W / 2.0f, 6.0f, false, 0 });                          // goal line
            for(int sy = -1; sy <= 1; sy += 2)
            {
                LgObj post = { m, mx - in, sy * LG_GOAL_HALF_W, H * 0.5f, 0, 6.0f, H / 2.0f, 6.0f, false, 0 }; post.rx = 90.0f;
                s_LgObjs.push_back(post);                                                                          // posts
                s_LgObjs.push_back({ m, cxg, sy * (LG_GOAL_HALF_W - 0.3f), H - 0.3f, 90.0f, 6.0f, D / 2.0f, 6.0f, false, 0 });   // roof edges
            }
        }
    }
    if(Game::IsKnownModel(3578))
    {
        const float L = 10.31f * 2.0f, sz = 6.0f;   // 4.13.13: drawn 2x long (half the objects on the x4 arena) and 6x tall
        auto Line = [&](float x0, float y0, float x1, float y1)
        {
            const float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy);
            const int n = (int)ceilf(len / L - 0.05f); if(n <= 0) return;
            const float rz = atan2f(dy, dx) * 57.29578f, sx = len / (n * 10.31f);
            for(int i = 0; i < n; ++i)
            {
                const float t = (i + 0.5f) / n;
                s_LgObjs.push_back({ 3578, x0 + dx * t, y0 + dy * t, 0, rz, sx, 1.0f, sz, true, 0 });
            }
        };
        const float cx = LG_CORNER - LG_HALF_W, cy = LG_CORNER - LG_HALF_L;   // where the corner cut meets the end / side walls
        for(int sy = -1; sy <= 1; sy += 2)
        {
            Line(-cx, sy * LG_HALF_W, cx, sy * LG_HALF_W);                                   // side walls
            for(int sx = -1; sx <= 1; sx += 2)
            {
                Line(sx * cx, sy * LG_HALF_W, sx * LG_HALF_L, sy * cy);                     // the 45 deg corners
                Line(sx * LG_HALF_L, sy * cy, sx * LG_HALF_L, sy * LG_GOAL_HALF_W);          // end walls beside the goal
            }
        }
        for(int sx = -1; sx <= 1; sx += 2)
        {   // the goal box sides + back (the cars can't fall off behind the goal)
            const float x0 = sx * LG_HALF_L, x1 = sx * (LG_HALF_L + LG_GOAL_DEPTH);
            Line(x0, -LG_GOAL_HALF_W, x1, -LG_GOAL_HALF_W); Line(x0, LG_GOAL_HALF_W, x1, LG_GOAL_HALF_W);
            Line(x1, -LG_GOAL_HALF_W, x1, LG_GOAL_HALF_W);
        }
    }
}
static void LgArenaBuild(uint64_t now)
{
    if(!s_bLgArenaPlanned) LgPlanArena();
    if(now >= s_nLgBuildAt)
    {
        s_nLgBuildAt = now + 100;
        int made = 0;
        for(auto& o : s_LgObjs)
        {
            if(o.h > 0 || o.h < 0 || made >= 6) continue;
            const int h = Game::CreateMapObject(o.model, LgWorld(o.x, o.y, o.z), o.rx, o.ry, o.rz);
            if(h == 0) break;                         // streaming in: try again next round
            o.h = h > 0 ? h : -1; ++made;
            if(h > 0)
            {
                if(!o.solid) Game::SetObjectCollision(h, false);
                if(o.fit) { LgApplyFit(o); Game::PlaceMapObject(h, LgWorld(o.x, o.y, o.z), o.rx, o.ry, o.rz); }   // 4.13.15
                BoneAttach::ScaleObject(h, o.sx, o.sy, o.sz);
                if(o.solid && o.model == s_nLgFloor && o.model != 19529)
                {   // 4.13.20: the 6959 court's top on the pitch level (the model is loaded now)
                    float mn[3], mx[3];
                    if(Game::ModelDims(o.model, mn, mx) && fabsf(mx[2]) < 5.0f) { o.z -= mx[2]; Game::PlaceMapObject(h, LgWorld(o.x, o.y, o.z), o.rx, o.ry, o.rz); }
                }
                if(o.onFloor)
                {   // the model is loaded now: lift it so its (scaled) bottom sits on the floor
                    const float b = Game::ModelBottom(o.model);
                    if(b <= 0.0f) { o.z = -b * o.sz; Game::PlaceMapObject(h, LgWorld(o.x, o.y, o.z), o.rx, o.ry, o.rz); }
                }
            }
        }
    }
    if(now >= s_nLgScaleAt)
    {
        s_nLgScaleAt = now + 250;
        for(auto& o : s_LgObjs) if(o.h > 0) BoneAttach::ScaleObject(o.h, o.sx, o.sy, o.sz);
    }
}
// 4.13.13: the goal blast (SA-MP particle objects inside the net that was scored in)
struct LgFx { int h; uint64_t until; };
static std::vector<LgFx> s_LgFx;
static void LgGoalBlast(int side)   // side: +1 the orange net (x+), -1 the blue net (x-)
{
    if(s_Lg.view != 3) return;
    static const int kFx[2] = { 18688, 18693 };
    const uint64_t until = MP_NowMs() + 2500;
    for(int i = 0; i < 6; ++i)
    {
        const int m = kFx[i & 1]; if(!Game::IsKnownModel(m)) continue;
        const float x = side * (LG_HALF_L + LG_GOAL_DEPTH * (0.25f + 0.25f * (i % 3)));
        const float y = (i < 3 ? -0.45f : 0.45f) * LG_GOAL_HALF_W;
        const int h = Game::CreateMapObject(m, LgWorld(x, y, 1.0f + 6.0f * (i & 1)), 0, 0, 0);
        if(h > 0) { Game::SetObjectCollision(h, false); BoneAttach::ScaleObject(h, 6.0f, 6.0f, 6.0f); s_LgFx.push_back({ h, until }); }
    }
}
static void LgFxTick(uint64_t now)
{
    for(size_t i = 0; i < s_LgFx.size();)
        if(now >= s_LgFx[i].until) { Game::DeleteWorldObject(s_LgFx[i].h); s_LgFx.erase(s_LgFx.begin() + i); } else ++i;
}
// 4.13.13: the ball is a real object now - GTA object 1946 (baskt_ball_hi) with the Rocket League ball mesh and
// texture put in by RlBall (src/game/lgball.cpp, from assets/MP/ball_-_rocket_league.glb). It follows the
// predicted ball every frame and rolls with it. The drawn ball is only used when that swap is not possible.
static const int kLgBallModel = 1946;
static int   s_LgBallObj = 0;
static float s_LgBallRot[2] = { 0, 0 }, s_LgBallLast[2] = { 0, 0 };
static void LgBallObjTick()
{
    const float* b = s_Lg.ball.p;
    if(s_LgBallObj > 0 && !Game::WorldObjectExists(s_LgBallObj)) s_LgBallObj = 0;
    if(s_LgBallObj <= 0)
    {
        if(RlBall::Failed() || !LgFloorReady() || !RlBall::Ready(kLgBallModel)) return;
        s_LgBallObj = Game::CreateWorldObject(kLgBallModel, LgWorld(b[0], b[1], b[2]));
        if(s_LgBallObj > 0) { Game::SetObjectCollision(s_LgBallObj, false); s_LgBallLast[0] = b[0]; s_LgBallLast[1] = b[1]; }
        return;
    }
    const float dx = b[0] - s_LgBallLast[0], dy = b[1] - s_LgBallLast[1];
    s_LgBallLast[0] = b[0]; s_LgBallLast[1] = b[1];
    if(fabsf(dx) < 60.0f && fabsf(dy) < 60.0f)
    {   // rolling: turn by the distance over the radius, around the axis across the motion
        s_LgBallRot[0] = fmodf(s_LgBallRot[0] + dy / LG_BALL_R * 57.29578f, 360.0f);
        s_LgBallRot[1] = fmodf(s_LgBallRot[1] - dx / LG_BALL_R * 57.29578f, 360.0f);
    }
    Game::PlaceMapObject(s_LgBallObj, LgWorld(b[0], b[1], b[2]), s_LgBallRot[0], s_LgBallRot[1], 0.0f);
}
static void LgBallObjFree()
{
    if(s_LgBallObj > 0) Game::DeleteWorldObject(s_LgBallObj);
    s_LgBallObj = 0;
    RlBall::Restore(kLgBallModel);   // the GTA basketball is back for everything else
}
static void LgArenaFree()
{
    LgBallObjFree();
    for(auto& f : s_LgFx) Game::DeleteWorldObject(f.h);
    s_LgFx.clear();
    for(auto& o : s_LgObjs) if(o.h > 0) Game::DeleteWorldObject(o.h);
    s_LgObjs.clear(); s_bLgArenaPlanned = false;
}
static bool LgCarOk(int car) { return car > 0 && Game::CarExists(car) && !Game::IsCarDead(car); }
static void LgSend(uint8_t cmd, int a, const float* pos = nullptr, const float* vel = nullptr)
{
    PktLgCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_LG; c.op = LGO_CMD; c.cmd = cmd; c.a = (uint8_t)a;
    if(pos) memcpy(c.pos, pos, sizeof(c.pos));
    if(vel) memcpy(c.vel, vel, sizeof(c.vel));
    client.Send(&c, sizeof(c));
}
static void LgBanner(const char* t, const char* sub, uint32_t col, int ms)
{
    MP_CopyStr(s_Lg.banner, t ? t : "", sizeof(s_Lg.banner)); MP_CopyStr(s_Lg.bannerSub, sub ? sub : "", sizeof(s_Lg.bannerSub));
    s_Lg.bannerCol = col; s_Lg.bannerUntil = MP_NowMs() + ms;
}
static void LgFeed(const char* t)
{
    for(int i = 3; i > 0; --i) { memcpy(s_Lg.feed[i], s_Lg.feed[i - 1], sizeof(s_Lg.feed[0])); s_Lg.feedAt[i] = s_Lg.feedAt[i - 1]; }
    MP_CopyStr(s_Lg.feed[0], t, sizeof(s_Lg.feed[0])); s_Lg.feedAt[0] = MP_NowMs();
    if(s_Lg.feedN < 4) ++s_Lg.feedN;
}

static void LgDeleteLobbyCars(int keep)
{
    for(int i = 0; i < LG_CAR_COUNT; ++i)
    {
        int& c = s_Lg.lobbyCar[i];
        if(c > 0 && c != keep && Game::CarExists(c)) { int ped = Game::LocalPed(); if(!(ped > 0 && Game::IsCharInCar(ped, c))) Game::DeleteCar(c); }
        if(c != keep) c = 0;
    }
}
static void LgDeleteMyCar()
{
    if(s_Lg.myCar > 0 && Game::CarExists(s_Lg.myCar))
    {
        int ped = Game::LocalPed();
        if(ped > 0 && Game::IsCharInCar(ped, s_Lg.myCar)) { Vec3 p; Game::GetCarPos(s_Lg.myCar, p); p.x += 2.5f; Game::WarpOutOfCar(ped, p); }
        Game::DeleteCar(s_Lg.myCar);
    }
    for(int i = 0; i < LG_CAR_COUNT; ++i) if(s_Lg.lobbyCar[i] == s_Lg.myCar) s_Lg.lobbyCar[i] = 0;
    s_Lg.myCar = 0;
}
static void LgSetupCar(int car, int team)
{
    if(!LgCarOk(car)) return;
    Game::SetCarGodMode(car, true);
    Game::SetCarProofs(car, true, true, true, true, true);
    Game::KeepMissionCar(car);
    const int col = team <= 1 ? g_LgTeamCarCol[team] : 79;
    Game::SetCarColours(car, col, 1);
}

static void LgToLobby(int ped)
{
    LgDeleteMyCar();
    s_Lg.view = 1; s_Lg.viewAt = MP_NowMs(); s_Lg.freePlay = false; s_Lg.carIdx = -1; s_Lg.frozen = false;
    if(ped > 0)
    {
        Game::SetCharVisible(ped, true); Game::FreezeChar(ped, false); Game::SetCharGodMode(ped, true);
        Game::Teleport(ped, kLgLobby, kLgLobbyHeading);
    }
    Game::RestoreCamera();
    Game::SetPlayerControl(true); Game::ShowGameHud(true);
    Game::SetTimeOfDay(23, 30);
}

// the join flow hands over after the login (mp_join.inc)
static bool LgJoinTakeover(int ped)
{
    if(!s_bLgJoin && !LgOnServer()) return false;
    s_bLgJoin = false;
    logger->Info("LEAGUE: join takeover");
    s_Lg = LgClient();
    memset(s_Lg.teamOf, 255, sizeof(s_Lg.teamOf));
    s_Lg.engaged = true; s_Lg.engagedAt = MP_NowMs();
    LgBallReset(s_Lg.ball);
    for(auto& p : s_LgPads) p.backAt = 0;
    LgToLobby(ped);
    return true;
}

static void LgRelease()
{
    if(!s_Lg.engaged) return;
    int ped = Game::LocalPed();
    LgDeleteMyCar();
    LgDeleteLobbyCars(0);
    LgArenaFree();
    if(ped > 0) { Game::SetCharGodMode(ped, false); Game::FreezeChar(ped, false); Game::SetCharVisible(ped, true); }
    s_Lg = LgClient();
    memset(s_Lg.teamOf, 255, sizeof(s_Lg.teamOf));
}

static void LgOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    switch(d[1])
    {
        case LGO_STATE:
        {
            if(len < (int)sizeof(PktLgState)) return;
            PktLgState s; memcpy(&s, d, sizeof(s));
            if(s_Lg.hasState && s.seq <= s_Lg.st.seq && s_Lg.st.seq - s.seq < 1000) return;   // late
            for(int k = 0; k < 3; ++k) if(!std::isfinite(s.ball[k]) || !std::isfinite(s.vel[k])) return;
            const uint8_t oldPhase = s_Lg.hasState ? s_Lg.st.phase : 255;
            s_Lg.st = s; s_Lg.hasState = true; s_Lg.stateAt = MP_NowMs();
            if(!s_Lg.freePlay) { memcpy(s_Lg.ball.p, s.ball, sizeof(s.ball)); memcpy(s_Lg.ball.v, s.vel, sizeof(s.vel)); }
            if(oldPhase != s.phase && s.phase == LGP_KICKOFF) { s_Lg.boost = 33; for(auto& p : s_LgPads) p.backAt = 0; }
            break;
        }
        case LGO_PLAYERS:
        {
            if(len < (int)offsetof(PktLgPlayers, r)) return;
            PktLgPlayers pk; memset(&pk, 0, sizeof(pk)); memcpy(&pk, d, len > (int)sizeof(pk) ? (int)sizeof(pk) : len);
            int n = pk.n; const int fit = (len - (int)offsetof(PktLgPlayers, r)) / (int)sizeof(LgRow); if(n > fit) n = fit; if(n > MP_LG_MAX_PLAYERS) n = MP_LG_MAX_PLAYERS;
            memset(s_Lg.teamOf, 255, sizeof(s_Lg.teamOf));
            for(int i = 0; i < n; ++i) { s_Lg.rows[i] = pk.r[i]; if(pk.r[i].id < MP_MAX_PLAYERS) s_Lg.teamOf[pk.r[i].id] = pk.r[i].team; }
            s_Lg.nRows = n;
            break;
        }
        case LGO_ME:
        {
            if(len < (int)sizeof(PktLgMe)) return;
            const uint8_t oldTeam = s_Lg.hasMe ? s_Lg.me.team : 255;
            memcpy(&s_Lg.me, d, sizeof(PktLgMe)); s_Lg.hasMe = true;
            if(oldTeam != s_Lg.me.team && LgCarOk(s_Lg.myCar)) LgSetupCar(s_Lg.myCar, s_Lg.me.team);
            break;
        }
        case LGO_EVENT:
        {
            if(len < (int)sizeof(PktLgEvent)) return;
            PktLgEvent e; memcpy(&e, d, sizeof(e)); e.text[sizeof(e.text) - 1] = 0;
            switch(e.ev)
            {
                case LGE_SPAWN:
                {
                    if(s_Lg.view != 3 || s_Lg.freePlay || s_Lg.arenaWait) break;   // (4.13.10: after the floor exists the phone asks again)
                    for(int k = 0; k < 4; ++k) if(!std::isfinite(e.pos[k])) return;
                    int ped = Game::LocalPed();
                    Vec3 at = LgWorld(e.pos[0], e.pos[1], e.pos[2]);
                    if(LgCarOk(s_Lg.myCar)) { Game::SetCarPos(s_Lg.myCar, at, 0.0f); Game::SetCarHeading(s_Lg.myCar, e.pos[3]); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 }); }
                    else if(ped > 0) Game::TeleportLight(ped, at, e.pos[3]);
                    s_Lg.spawnAt = MP_NowMs(); s_Lg.boost = 33;
                    break;
                }
                case LGE_GOAL:
                {
                    char t[72]; snprintf(t, sizeof(t), "%s SCORED", e.a ? "ORANGE" : "BLUE");
                    char sub[72]; snprintf(sub, sizeof(sub), "%s  -  %d km/h", e.text, (int)e.val);
                    LgBanner(t, sub, e.a ? 0xFF9A2EFF : 0x3FA0FFFF, 4500);
                    LgGoalBlast(e.a ? -1 : 1);   // 4.13.13: blue scores into the orange net (x+)
                    break;
                }
                case LGE_SAVE: { char t[72]; snprintf(t, sizeof(t), "SAVE!  %s", e.text); LgFeed(t); break; }
                case LGE_SHOT: LgFeed("Shot on Goal  +20"); break;
                case LGE_QCHAT: LgFeed(e.text); break;
                case LGE_NOTICE: LgBanner(e.text, "", 0xFFFFFFFF, 1500); break;
                case LGE_END:
                {
                    char sub[72] = "";
                    if(e.b != MP_INVALID_ID && e.b < MP_MAX_PLAYERS && MP::players[e.b].active) snprintf(sub, sizeof(sub), "MVP  %s  (%d)", MP::players[e.b].name, (int)e.val);
                    LgBanner(e.text, sub, e.a ? 0xFF9A2EFF : 0x3FA0FFFF, 9000);
                    s_Lg.board = true;
                    break;
                }
                case LGE_RANK:
                {
                    char t[72]; const int d = (int)e.val - 1000;
                    snprintf(t, sizeof(t), "%s  %s%d MMR", e.c ? "WIN" : "LOSS", d >= 0 ? "+" : "", d);
                    LgFeed(t);
                    break;
                }
            }
            break;
        }
    }
}

// ---- process (every frame) -------------------------------------------------------------------
static void LgArenaProcess(int ped, uint64_t now, float dt)
{
    // always in the car (RL: you can't get out)
    if(!LgCarOk(s_Lg.myCar))
    {
        s_Lg.myCar = 0;
        const int idx = s_Lg.carIdx >= 0 && s_Lg.carIdx < LG_CAR_COUNT ? s_Lg.carIdx : 0;
        Vec3 at = LgWorld(s_Lg.me.team == 1 ? 40.0f : -40.0f, 0, 1.0f);
        int c = Game::CreateCar(g_LgCars[idx].model, at);
        if(c > 0) { s_Lg.myCar = c; LgSetupCar(c, s_Lg.me.team); Game::SetCarHeading(c, s_Lg.me.team == 1 ? 90.0f : 270.0f); Game::WarpIntoCar(ped, c); }
        return;
    }
    if(!Game::IsCharInCar(ped, s_Lg.myCar)) { if(!Game::IsCharGettingInToACar(ped)) Game::WarpIntoCar(ped, s_Lg.myCar); return; }
    if(s_Lg.arenaWait)
    {   // 4.13.10: the floating floor first
        const float side = s_Lg.freePlay ? -1.0f : (s_Lg.me.team == 1 ? 1.0f : -1.0f);
        const Vec3 at = LgWorld(side * 46.08f * LG_SCALE, 0, 1.0f);
        if(!LgFloorReady())
        {
            if(!s_Lg.frozen) { Game::FreezeCar(s_Lg.myCar, true); s_Lg.frozen = true; }
            Game::SetCarPos(s_Lg.myCar, at, 0.0f); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 });
            if(now - s_Lg.viewAt > 15000)
            {   // the floor never came (object pool full / model pack broken): the LV apron instead
                logger->Error("LEAGUE: the sky floor did not appear in 15 s - using the LV airport apron");
                LgArenaFree(); s_bLgArenaPlanned = true; s_bLgSky = false;
                for(int k = 0; k < 3; ++k) s_LgC[k] = kLgFallbackCenter[k];
                s_Lg.viewAt = now;
            }
            return;
        }
        s_Lg.arenaWait = false;
        Game::SetCarPos(s_Lg.myCar, at, 0.3f); Game::SetCarHeading(s_Lg.myCar, side < 0 ? 270.0f : 90.0f); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 });
        Game::FreezeCar(s_Lg.myCar, false); s_Lg.frozen = false;
        if(!s_Lg.freePlay) LgSend(LGC_SPAWN, 0);   // the server's kickoff spot again, now that there is a floor
    }
    if(now >= s_Lg.keepAt) { s_Lg.keepAt = now + 2000; LgSetupCar(s_Lg.myCar, s_Lg.freePlay ? 255 : s_Lg.me.team); Game::FixCar(s_Lg.myCar); }
    // kickoff: the cars wait for GO
    const bool kick = !s_Lg.freePlay && s_Lg.hasState && s_Lg.st.phase == LGP_KICKOFF && s_Lg.me.team <= 1;
    if(kick != s_Lg.frozen) { Game::FreezeCar(s_Lg.myCar, kick); s_Lg.frozen = kick; }

    Vec3 cp; if(!Game::GetCarPos(s_Lg.myCar, cp)) return;
    Vec3 v = { 0, 0, 0 }; Game::GetMoveSpeed(s_Lg.myCar, true, v);
    float rel[3] = { cp.x - s_LgC[0], cp.y - s_LgC[1], cp.z - s_LgC[2] };
    // far away (fell through, drove off): back on the pitch
    if(fabsf(rel[0]) > LG_HALF_L + LG_GOAL_DEPTH + 25 || fabsf(rel[1]) > LG_HALF_W + 25 || rel[2] < -15 || rel[2] > LG_CEIL + 20)
    {
        Game::SetCarPos(s_Lg.myCar, LgWorld(s_Lg.me.team == 1 ? LG_HALF_L * 0.8f : -LG_HALF_L * 0.8f, 0, 1.0f), 0.0f); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 });
        return;
    }
    // the arena walls (invisible): bounce back like the RL cage
    bool hit = false;
    const bool inMouth = fabsf(rel[1]) < LG_GOAL_HALF_W && rel[2] < LG_GOAL_H;
    const float lx = inMouth ? LG_HALF_L + LG_GOAL_DEPTH - 1.5f : LG_HALF_L - 1.2f;
    if(rel[0] > lx && v.x > 0) { v.x = -v.x * 0.4f; hit = true; } else if(rel[0] < -lx && v.x < 0) { v.x = -v.x * 0.4f; hit = true; }
    const float ly = fabsf(rel[0]) > LG_HALF_L ? LG_GOAL_HALF_W - 1.2f : LG_HALF_W - 1.2f;
    if(rel[1] > ly && v.y > 0) { v.y = -v.y * 0.4f; hit = true; } else if(rel[1] < -ly && v.y < 0) { v.y = -v.y * 0.4f; hit = true; }
    if(fabsf(rel[0]) < LG_HALF_L && fabsf(rel[0]) + fabsf(rel[1]) > LG_CORNER - 1.7f)
    {   // 4.13.10: the 45 deg corner walls
        const float nx = (rel[0] < 0 ? -1.0f : 1.0f) * 0.70711f, ny = (rel[1] < 0 ? -1.0f : 1.0f) * 0.70711f;
        const float vn = v.x * nx + v.y * ny;
        if(vn > 0) { v.x -= 1.4f * vn * nx; v.y -= 1.4f * vn * ny; hit = true; }
    }

    // boost / jump / dodge
    float fwd[3] = { 0, 1, 0 }, up[3] = { 0, 0, 1 };
    Game::GetCarRotation(s_Lg.myCar, fwd, up);
    s_Lg.airborne = rel[2] > 1.9f;
    if(!s_Lg.airborne && now - s_Lg.jumpAt > 300) s_Lg.dodged = false;
    if(s_Lg.boostHeld && s_Lg.boost > 0 && !kick)
    {
        const float acc = s_Lg.airborne ? 18.0f : 22.0f;
        v.x += fwd[0] * acc * dt; v.y += fwd[1] * acc * dt; v.z += fwd[2] * acc * dt + (s_Lg.airborne ? 4.0f * dt : 0);
        const float sp = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); if(sp > 46.0f) { const float k = 46.0f / sp; v.x *= k; v.y *= k; v.z *= k; }
        s_Lg.boost -= 33.3f * dt; if(s_Lg.boost < 0) s_Lg.boost = 0;
        hit = true;
    }
    if(s_Lg.jumpTap && !kick)
    {
        s_Lg.jumpTap = false;
        if(!s_Lg.airborne) { v.z += 7.5f; s_Lg.jumpAt = now; hit = true; }
        else if(!s_Lg.dodged && now - s_Lg.jumpAt < 1500) { v.x += fwd[0] * 11.0f; v.y += fwd[1] * 11.0f; v.z = v.z > 2 ? v.z : 2; s_Lg.dodged = true; hit = true; }
    }
    s_Lg.jumpTap = false;
    if(hit) Game::SetMoveSpeed(s_Lg.myCar, true, v);
    // boost pads
    for(auto& p : s_LgPads)
    {
        if(p.backAt && now < p.backAt) continue;
        p.backAt = 0;
        const float dx = rel[0] - p.x, dy = rel[1] - p.y;
        if(dx * dx + dy * dy < (p.big ? 3.2f * 3.2f : 2.2f * 2.2f) && rel[2] < 3 && s_Lg.boost < 100)
        { s_Lg.boost = p.big ? 100 : (s_Lg.boost + 12 > 100 ? 100 : s_Lg.boost + 12); p.backAt = now + (p.big ? 10000 : 4000); }
    }
    // the ball
    if(s_Lg.freePlay || !s_Lg.hasState || s_Lg.st.phase == LGP_PLAY || s_Lg.st.phase == LGP_WARMUP || s_Lg.st.phase == LGP_GOAL)
    {
        const int g = LgBallStep(s_Lg.ball, dt);
        if(g && s_Lg.freePlay) { s_Lg.fpScore[g - 1]++; LgGoalBlast(g == 1 ? 1 : -1); LgBanner("GOAL!", g == 1 ? "into the orange net" : "into the blue net", g == 1 ? 0x3FA0FFFF : 0xFF9A2EFF, 2500); LgBallReset(s_Lg.ball); }
    }
    if(!s_Lg.freePlay && s_Lg.hasState && s_Lg.st.phase == LGP_KICKOFF) LgBallReset(s_Lg.ball);
    const float bx = s_Lg.ball.p[0] - rel[0], by = s_Lg.ball.p[1] - rel[1], bz = s_Lg.ball.p[2] - (rel[2] + 0.4f);
    const float reach = LG_BALL_R + 2.1f;
    if(bx * bx + by * by + bz * bz < reach * reach && now - s_Lg.touchAt > 120 && !kick)
    {
        s_Lg.touchAt = now;
        float cv[3] = { v.x, v.y, v.z };
        LgBallHit(s_Lg.ball, rel, cv);   // instant feedback; the server answers with its own ball
        if(!s_Lg.freePlay) { float wp[3] = { cp.x, cp.y, cp.z }; LgToCanon(wp); LgSend(LGC_TOUCH, 0, wp, cv); }
    }
}

static void LgLobbyProcess(int ped, uint64_t now)
{
    // 4.13.14: the night is held by the world lock (MP::LgClockMinute) - no more SetTimeOfDay every 4 s
    // the parked cars (they stream in: CreateCar gives 0 until the model is loaded)
    for(int i = 0; i < LG_CAR_COUNT; ++i)
    {
        int& c = s_Lg.lobbyCar[i];
        if(c > 0 && !Game::CarExists(c)) c = 0;
        if(!c)
        {
            c = Game::CreateCar(g_LgCars[i].model, kLgLobbyCars[i]);
            if(c > 0) { Game::SetCarHeading(c, kLgLobbyCarHead[i]); LgSetupCar(c, 255); Game::SetCarEngineOn(c, false); }
        }
    }
    if(s_Lg.view == 1 && Game::IsCharInAnyCar(ped))
    {
        const int car = Game::GetCarCharIsIn(ped);
        for(int i = 0; i < LG_CAR_COUNT; ++i)
            if(car > 0 && car == s_Lg.lobbyCar[i])
            {
                s_Lg.carIdx = i; s_Lg.myCar = car; s_Lg.view = 2; s_Lg.viewAt = now;
                Game::SetCarEngineOn(car, true);
                LgSend(LGC_CAR, i);
                break;
            }
    }
    if(s_Lg.view == 2 && !Game::IsCharInAnyCar(ped) && now - s_Lg.viewAt > 800) { s_Lg.view = 1; s_Lg.myCar = 0; s_Lg.carIdx = -1; }
}

static void LgGoArena(bool freePlay)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    s_Lg.freePlay = freePlay; s_Lg.fpScore[0] = s_Lg.fpScore[1] = 0;
    LgDeleteLobbyCars(s_Lg.myCar);
    if(LgCarOk(s_Lg.myCar)) LgSetupCar(s_Lg.myCar, freePlay ? 255 : s_Lg.me.team);
    const float side = freePlay ? -1.0f : (s_Lg.me.team == 1 ? 1.0f : -1.0f);
    // 4.13.10: no TeleportGround (it would find the street 380 m below the sky pitch): the car waits frozen at its
    // kickoff spot until the floor object exists (LgArenaProcess), then it is put down on the grass
    if(!s_bLgArenaPlanned) LgPlanArena();
    const Vec3 at = LgWorld(side * 46.08f * LG_SCALE, 0, 1.0f);
    Game::PreloadArea(at, false);
    if(LgCarOk(s_Lg.myCar) && Game::IsCharInCar(ped, s_Lg.myCar))
    { Game::SetCarPos(s_Lg.myCar, at, 0.0f); Game::SetCarHeading(s_Lg.myCar, side < 0 ? 270.0f : 90.0f); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 }); Game::FreezeCar(s_Lg.myCar, true); s_Lg.frozen = true; }
    else Game::TeleportLight(ped, at, side < 0 ? 270.0f : 90.0f);
    s_Lg.arenaWait = true;
    if(!LgFloorReady()) LgBanner("BUILDING THE ARENA", "Sky Pitch above Los Santos", 0xFFFFFFFF, 2500);
    if(freePlay) LgBallReset(s_Lg.ball);
    else LgSend(LGC_TEAM, 2);   // auto team (balanced); the server sends the kickoff spot
    s_Lg.view = 3; s_Lg.viewAt = MP_NowMs(); s_Lg.boost = 33;
    Game::SetTimeOfDay(22, 0);
}

static void LgProcess(int ped, uint64_t now)
{
    if(!LgOnServer()) { if(s_Lg.engaged) LgRelease(); return; }
    if(!s_Lg.engaged)
    {   // the join flow didn't hand over (reconnect / an old join flow): take over after the login
        if(JoinScreenUp() || MP::AuthScreenShown() || ped <= 0) return;
        s_bLgJoin = true;
        LgJoinTakeover(ped);
    }
    if(ped <= 0) return;
    float dt = s_Lg.lastFrame ? (now - s_Lg.lastFrame) / 1000.0f : 0.0f; s_Lg.lastFrame = now;
    if(dt > 0.1f) dt = 0.1f;
    if(s_Lg.view == 1 || s_Lg.view == 2) LgLobbyProcess(ped, now);
    else if(s_Lg.view == 3) { LgArenaBuild(now); LgFxTick(now); LgArenaProcess(ped, now, dt); LgBallObjTick(); }
}

// ---- drawing ----------------------------------------------------------------------------------
static ImU32 LgCol(uint32_t rgba, float a = 1.0f) { return UI::FromRGBA(rgba, a); }
static bool LgW2S(const Vec3& w, ImVec2& out) { float x, y, d; if(!Game::WorldToScreen(w, x, y, d) || d <= 0.5f) return false; out = ImVec2(x, y); return true; }
static void LgText(ImDrawList* dl, float fs, ImVec2 c, ImU32 col, const char* t, bool shadow = true)
{   // centred, the soft MTA dxDrawText shadow
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    ImVec2 p(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
    if(shadow) dl->AddText(UI::Font(), fs, ImVec2(p.x + 1, p.y + 1), IM_COL32(0, 0, 0, 170), t);
    dl->AddText(UI::Font(), fs, p, col, t);
}
// squared "digital" digits like the SA:O boost number (wide spaced 7-segment, thick strokes)
static void LgDigit(ImDrawList* dl, ImVec2 o, float w, float h, float t, int d, ImU32 col)
{
    static const uint8_t seg[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };
    const uint8_t m = d >= 0 && d <= 9 ? seg[d] : 0x40;
    const float hh = h * 0.5f;
    auto H = [&](float y) { dl->AddRectFilled(ImVec2(o.x, y), ImVec2(o.x + w, y + t), col); };
    auto V = [&](float x, float y0, float y1) { dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + t, y1), col); };
    if(m & 0x01) H(o.y);
    if(m & 0x02) V(o.x + w - t, o.y, o.y + hh + t * 0.5f);
    if(m & 0x04) V(o.x + w - t, o.y + hh - t * 0.5f, o.y + h);
    if(m & 0x08) H(o.y + h - t);
    if(m & 0x10) V(o.x, o.y + hh - t * 0.5f, o.y + h);
    if(m & 0x20) V(o.x, o.y, o.y + hh + t * 0.5f);
    if(m & 0x40) dl->AddRectFilled(ImVec2(o.x + t * 0.6f, o.y + hh - t * 0.5f), ImVec2(o.x + w, o.y + hh + t * 0.5f), col);
}
// 4.13.10: the HUD of the reference screenshot (Screenshots/Screenshot_20261112-094939.png): a solid orange arc in a
// dark ring with white ticks, the big white boost number, a small orange BOOST
static void LgDrawBoost(ImDrawList* dl, ImVec2 ds)
{
    const float R = UI::S(78), th = UI::S(13);
    const ImVec2 c(ds.x - R - UI::S(40), ds.y - R - UI::S(34));
    dl->AddCircleFilled(c, R + th * 0.5f, IM_COL32(10, 14, 20, 120), 64);
    dl->AddCircle(c, R, IM_COL32(16, 18, 24, 235), 64, th);
    const float f = s_Lg.boost / 100.0f;
    if(f > 0.005f)
    {   // RL fills it clockwise from the bottom
        const float a0 = IM_PI * 0.5f, a1 = a0 + f * IM_PI * 2.0f;
        dl->PathArcTo(c, R, a0, a1, 64); dl->PathStroke(IM_COL32(255, 128, 20, 255), 0, th * 0.62f);
    }
    for(int i = 0; i < 12; ++i)
    {
        const float a = i * IM_PI * 2.0f / 12.0f; const ImVec2 dir(cosf(a), sinf(a));
        dl->AddLine(ImVec2(c.x + dir.x * (R - th * 0.5f), c.y + dir.y * (R - th * 0.5f)), ImVec2(c.x + dir.x * (R + th * 0.5f), c.y + dir.y * (R + th * 0.5f)), IM_COL32(245, 245, 245, 230), UI::S(2.0f));
    }
    char b[8]; snprintf(b, sizeof(b), "%d", (int)(s_Lg.boost + 0.5f));
    const int n = (int)strlen(b);
    const float dw = UI::S(28), dh = UI::S(36), st = UI::S(6.0f), gap = UI::S(10);
    const float tw = n * dw + (n - 1) * gap;
    float x = c.x - tw * 0.5f;
    for(int i = 0; i < n; ++i) { LgDigit(dl, ImVec2(x + 2, c.y - dh * 0.6f + 2), dw, dh, st, b[i] - '0', IM_COL32(0, 0, 0, 90)); LgDigit(dl, ImVec2(x, c.y - dh * 0.6f), dw, dh, st, b[i] - '0', IM_COL32(250, 250, 250, 255)); x += dw + gap; }
    LgText(dl, UI::S(12), ImVec2(c.x, c.y + R * 0.46f), IM_COL32(255, 140, 30, 255), "BOOST", false);
}
// the team name under the score box: one player = the name ("YOU" for this phone), more = "BLUE x3"
static void LgTeamLabel(int team, char* out, int n)
{
    int cnt = 0; const char* nm = nullptr; const uint8_t me = client.GetMyId();
    for(int i = 0; i < s_Lg.nRows; ++i)
    {
        const LgRow& r = s_Lg.rows[i]; if(r.team != team) continue;
        ++cnt;
        if(!nm) nm = r.id == me ? "YOU" : (r.id < MP_MAX_PLAYERS && MP::players[r.id].active ? MP::players[r.id].name : "?");
    }
    if(cnt == 0) snprintf(out, n, "%s", team ? "ORANGE" : "BLUE");
    else if(cnt == 1) snprintf(out, n, "%s (%s)", nm, team ? "ORANGE" : "BLUE");
    else snprintf(out, n, "%s x%d", team ? "ORANGE" : "BLUE", cnt);
    for(char* p = out; *p; ++p) if(*p >= 'a' && *p <= 'z') *p -= 32;
}
// the score box: blue score | dark navy clock | orange score, the small white caps line under it
static void LgDrawTop(ImDrawList* dl, ImVec2 ds)
{
    const PktLgState& s = s_Lg.st;
    const int sb = s_Lg.freePlay ? s_Lg.fpScore[0] : s.score[0], so = s_Lg.freePlay ? s_Lg.fpScore[1] : s.score[1];
    char clock[24];
    if(s_Lg.freePlay) snprintf(clock, sizeof(clock), "FREE PLAY");
    else if(s.overtime) snprintf(clock, sizeof(clock), "+%d:%02d", s.clock / 60, s.clock % 60);
    else snprintf(clock, sizeof(clock), "%d:%02d", s.clock / 60, s.clock % 60);
    const float h = UI::S(46), sw = UI::S(60), mw = UI::S(150), y0 = UI::S(10);
    const float x0 = ds.x * 0.5f - mw * 0.5f - sw;
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + sw, y0 + h), IM_COL32(26, 74, 168, 245));
    dl->AddRectFilled(ImVec2(x0 + sw, y0), ImVec2(x0 + sw + mw, y0 + h), IM_COL32(12, 20, 34, 240));
    dl->AddRectFilled(ImVec2(x0 + sw + mw, y0), ImVec2(x0 + 2 * sw + mw, y0 + h), IM_COL32(196, 90, 24, 245));
    char t[16];
    snprintf(t, sizeof(t), "%d", sb); LgText(dl, UI::S(24), ImVec2(x0 + sw * 0.5f, y0 + h * 0.5f), IM_COL32(150, 200, 255, 255), t, false);
    snprintf(t, sizeof(t), "%d", so); LgText(dl, UI::S(24), ImVec2(x0 + sw * 1.5f + mw, y0 + h * 0.5f), IM_COL32(255, 205, 130, 255), t, false);
    LgText(dl, UI::S(s_Lg.freePlay ? 17 : 22), ImVec2(ds.x * 0.5f, y0 + h * 0.5f), IM_COL32(250, 250, 250, 255), clock, false);
    char line[128];
    if(s_Lg.freePlay)
        snprintf(line, sizeof(line), "%s \xC2\xB7 BALL %d KM/H", s_bLgSky ? "LS SKY PITCH" : "LV AIRPORT",
                 (int)(sqrtf(s_Lg.ball.v[0] * s_Lg.ball.v[0] + s_Lg.ball.v[1] * s_Lg.ball.v[1] + s_Lg.ball.v[2] * s_Lg.ball.v[2]) * 3.6f));
    else { char bl[48], og[48]; LgTeamLabel(0, bl, sizeof(bl)); LgTeamLabel(1, og, sizeof(og)); snprintf(line, sizeof(line), "%s  vs  %s", bl, og); }
    LgText(dl, UI::S(14), ImVec2(ds.x * 0.5f, y0 + h + UI::S(14)), IM_COL32(240, 240, 240, 255), line);
    if(!s_Lg.freePlay && s.phase == LGP_KICKOFF && s.phaseMs > 0)
    {
        char k[8]; snprintf(k, sizeof(k), "%d", (s.phaseMs + 999) / 1000);
        LgText(dl, UI::S(96), ImVec2(ds.x * 0.5f, ds.y * 0.36f), IM_COL32(255, 255, 255, 240), k);
    }
    else if(!s_Lg.freePlay && (s.phase == LGP_GOAL || s.phase == LGP_END || s.phase == LGP_WARMUP))
    {
        static const char* const ph[5] = { "WARMUP", "", "", "GOAL REPLAY", "MATCH OVER" };
        LgText(dl, UI::S(13), ImVec2(ds.x * 0.5f, y0 + h + UI::S(32)), IM_COL32(200, 210, 220, 230), ph[s.phase]);
    }
}
static void LgDrawField(ImDrawList* dl)
{
    const ImU32 line = IM_COL32(255, 255, 255, 150), wall = IM_COL32(64, 224, 230, 190);
    auto Seg = [&](float x0, float y0, float z0, float x1, float y1, float z1, ImU32 col, float w)
    {   // split long lines so a point behind the camera doesn't drop the whole line
        const int n = 8;
        ImVec2 a; bool va = LgW2S(LgWorld(x0, y0, z0), a);
        for(int i = 1; i <= n; ++i)
        {
            const float t = (float)i / n; ImVec2 b;
            const bool vb = LgW2S(LgWorld(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, z0 + (z1 - z0) * t), b);
            if(va && vb) dl->AddLine(a, b, col, w);
            a = b; va = vb;
        }
    };
    const float z = 0.05f, w = UI::S(2.2f);
    const float cxw = LG_CORNER - LG_HALF_W, cyl = LG_CORNER - LG_HALF_L;   // 4.13.10: the corner cuts
    for(int sy = -1; sy <= 1; sy += 2)
    {
        Seg(-cxw, sy * LG_HALF_W, z, cxw, sy * LG_HALF_W, z, line, w);
        for(int sx = -1; sx <= 1; sx += 2) { Seg(sx * cxw, sy * LG_HALF_W, z, sx * LG_HALF_L, sy * cyl, z, line, w); Seg(sx * LG_HALF_L, sy * cyl, z, sx * LG_HALF_L, 0, z, line, w); }
    }
    Seg(0, -LG_HALF_W, z, 0, LG_HALF_W, z, line, w);
    for(int i = 0; i < 24; ++i)
    {   // centre circle
        const float a0 = i * IM_PI * 2 / 24, a1 = (i + 1) * IM_PI * 2 / 24, r = 9.0f;
        ImVec2 p0, p1; if(LgW2S(LgWorld(cosf(a0) * r, sinf(a0) * r, z), p0) && LgW2S(LgWorld(cosf(a1) * r, sinf(a1) * r, z), p1)) dl->AddLine(p0, p1, line, w);
    }
    // the cage walls (top rails) + the goal frames
    const float wh = 4.0f;
    for(int sy = -1; sy <= 1; sy += 2)
    {
        Seg(-cxw, sy * LG_HALF_W, wh, cxw, sy * LG_HALF_W, wh, wall, w);
        for(int sx = -1; sx <= 1; sx += 2) Seg(sx * cxw, sy * LG_HALF_W, wh, sx * LG_HALF_L, sy * cyl, wh, wall, w);
    }
    for(int s = -1; s <= 1; s += 2)
    {
        const float x = s * LG_HALF_L;
        Seg(x, -cyl, wh, x, -LG_GOAL_HALF_W, wh, wall, w); Seg(x, LG_GOAL_HALF_W, wh, x, cyl, wh, wall, w);
        const ImU32 gc = s > 0 ? IM_COL32(255, 154, 46, 230) : IM_COL32(63, 160, 255, 230);
        const float gw = UI::S(4.0f);
        Seg(x, -LG_GOAL_HALF_W, 0, x, -LG_GOAL_HALF_W, LG_GOAL_H, gc, gw); Seg(x, LG_GOAL_HALF_W, 0, x, LG_GOAL_HALF_W, LG_GOAL_H, gc, gw);
        Seg(x, -LG_GOAL_HALF_W, LG_GOAL_H, x, LG_GOAL_HALF_W, LG_GOAL_H, gc, gw);
        const float bx = x + s * LG_GOAL_DEPTH;
        Seg(bx, -LG_GOAL_HALF_W, 0, bx, LG_GOAL_HALF_W, 0, gc, w); Seg(x, -LG_GOAL_HALF_W, LG_GOAL_H, bx, -LG_GOAL_HALF_W, LG_GOAL_H * 0.7f, gc, w); Seg(x, LG_GOAL_HALF_W, LG_GOAL_H, bx, LG_GOAL_HALF_W, LG_GOAL_H * 0.7f, gc, w);
    }
    // boost pads: yellow rings lying on the ground (the screenshot), the big ones with a white outer ring
    const uint64_t now = MP_NowMs();
    for(const auto& p : s_LgPads)
    {
        const bool on = !p.backAt || now >= p.backAt;
        const float r = p.big ? 2.6f : 1.4f; const int n = 20;
        const ImU32 col = on ? IM_COL32(255, 214, 60, p.big ? 230 : 190) : IM_COL32(110, 100, 60, 90);
        for(int ring = 0; ring < (p.big ? 2 : 1); ++ring)
        {
            const float rr = ring ? r * 1.45f : r; const ImU32 cc = ring ? IM_COL32(235, 240, 245, on ? 170 : 60) : col;
            ImVec2 a; bool va = LgW2S(LgWorld(p.x + rr, p.y, 0.08f), a);
            for(int i = 1; i <= n; ++i)
            {
                const float t = i * IM_PI * 2 / n; ImVec2 b;
                const bool vb = LgW2S(LgWorld(p.x + cosf(t) * rr, p.y + sinf(t) * rr, 0.08f), b);
                if(va && vb && fabsf(a.x - b.x) < 400) dl->AddLine(a, b, cc, UI::S(p.big ? 2.6f : 1.8f));
                a = b; va = vb;
            }
        }
    }
}
static void LgDrawBall(ImDrawList* dl)
{
    const float* b = s_Lg.ball.p;
    ImVec2 c, e, sh, she;
    if(LgW2S(LgWorld(b[0], b[1], 0.06f), sh) && LgW2S(LgWorld(b[0] + LG_BALL_R, b[1], 0.06f), she))
    {   // ground shadow: smaller + lighter the higher the ball is
        const float k = 1.0f / (1.0f + b[2] * 0.08f);
        const float rx = (fabsf(she.x - sh.x) + fabsf(she.y - sh.y) * 0.5f) * k;
        if(rx > 1 && rx < 600)
        {   // an ellipse (this ImGui has no AddEllipseFilled)
            for(int i = 0; i < 24; ++i) { const float t = i * IM_PI * 2.0f / 24.0f; dl->PathLineTo(ImVec2(sh.x + cosf(t) * rx, sh.y + sinf(t) * rx * 0.42f)); }
            dl->PathFillConvex(IM_COL32(0, 0, 0, (int)(120 * k)));
        }
    }
    // 4.13.13: the real ball object is there (or still coming): no drawn ball. Only when the model swap can't
    // be done (or the object never came) is the old drawn ball used.
    if(s_LgBallObj > 0 || (!RlBall::Failed() && MP_NowMs() - s_Lg.viewAt < 8000)) return;
    if(!LgW2S(LgWorld(b[0], b[1], b[2]), c)) return;
    float x, y, d; if(!Game::WorldToScreen(LgWorld(b[0], b[1], b[2]), x, y, d)) return;
    // the screen radius from a point one radius "up" (keeps it round from any camera)
    if(!LgW2S(LgWorld(b[0], b[1], b[2] + LG_BALL_R), e)) return;
    float r = sqrtf((e.x - c.x) * (e.x - c.x) + (e.y - c.y) * (e.y - c.y));
    if(r < 2 || r > 900) return;
    // shaded dark-grey sphere (RL ball): a few circles toward the light
    dl->AddCircleFilled(c, r, IM_COL32(38, 42, 46, 255), 40);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.12f, c.y - r * 0.14f), r * 0.82f, IM_COL32(62, 68, 74, 255), 40);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.26f, c.y - r * 0.30f), r * 0.50f, IM_COL32(92, 98, 104, 255), 32);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.36f, c.y - r * 0.40f), r * 0.18f, IM_COL32(170, 176, 180, 200), 20);
    // the hex panel seams + teal lights, turning with the ball's roll
    const float spin = (b[0] + b[1]) / LG_BALL_R;
    for(int i = 0; i < 6; ++i)
    {
        const float a = spin + i * IM_PI / 3.0f;
        const float cs = cosf(a);
        if(cs < -0.2f) continue;   // the back side
        const ImVec2 p(c.x + sinf(a) * r * 0.70f, c.y + r * 0.10f * cs - r * 0.05f);
        dl->AddCircleFilled(p, r * 0.11f * (0.5f + 0.5f * cs), IM_COL32(64, 236, 224, (int)(150 + 100 * cs)), 12);
    }
    dl->AddCircle(c, r * 0.98f, IM_COL32(64, 236, 224, 140), 40, r * 0.06f > 1 ? r * 0.06f : 1);
    dl->AddCircle(c, r, IM_COL32(10, 12, 14, 220), 40, UI::S(1.5f));
}

void MP::LgDrawTags(ImDrawList* dl)
{
    if(!MP::settings.showNameTags) return;
    const bool arena = s_Lg.view == 3;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = MP::players[i];
        if(!p.active || i == MP::myId || !p.drawValid || !p.hasSync || p.invisible) continue;
        ImVec2 s; if(!LgW2S({ p.drawPos.x, p.drawPos.y, p.drawPos.z + (arena ? 1.9f : 1.15f) }, s)) continue;
        const float dx = p.drawPos.x - MP::myDrawPos.x, dy = p.drawPos.y - MP::myDrawPos.y;
        if(dx * dx + dy * dy > 160.0f * 160.0f) continue;
        if(!arena)
        {   // lobby: a small plain white name (SA:O)
            LgText(dl, UI::S(15), s, IM_COL32(245, 245, 245, 235), p.name);
            continue;
        }
        // match: a small navy box with the name in white; the team colour as a thin underline
        const float fs = UI::S(15);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, p.name);
        const float px = UI::S(6), py = UI::S(3);
        ImVec2 a(s.x - ts.x * 0.5f - px, s.y - ts.y * 0.5f - py), b(s.x + ts.x * 0.5f + px, s.y + ts.y * 0.5f + py);
        dl->AddRectFilled(a, b, IM_COL32(22, 38, 92, 225), UI::S(2));
        dl->AddRect(a, b, IM_COL32(150, 170, 220, 140), UI::S(2), 0, UI::S(1));
        const uint8_t t = s_Lg.teamOf[i];
        if(t <= 1) dl->AddRectFilled(ImVec2(a.x, b.y - UI::S(2)), ImVec2(b.x, b.y), t ? IM_COL32(255, 154, 46, 255) : IM_COL32(63, 160, 255, 255));
        dl->AddText(UI::Font(), fs, ImVec2(s.x - ts.x * 0.5f, s.y - ts.y * 0.5f), IM_COL32_WHITE, p.name);
        dl->AddLine(ImVec2(s.x, b.y), ImVec2(s.x, b.y + UI::S(6)), IM_COL32(22, 38, 92, 200), UI::S(1.5f));
    }
}

static void LgDrawBoard(ImDrawList* dl, ImVec2 ds)
{
    const float w = UI::S(760), rh = UI::S(30);
    float y = ds.y * 0.22f; const float x = (ds.x - w) * 0.5f;
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + rh), IM_COL32(6, 20, 24, 225));
    static const char* const hd[6] = { "NAME", "SCORE", "GOALS", "ASSISTS", "SAVES", "SHOTS" };
    const float cx[6] = { 0.04f, 0.50f, 0.61f, 0.72f, 0.83f, 0.94f };
    for(int k = 0; k < 6; ++k) dl->AddText(UI::Font(), UI::S(14), ImVec2(x + w * cx[k] - (k ? UI::S(20) : 0), y + UI::S(7)), IM_COL32(200, 214, 214, 255), hd[k]);
    y += rh;
    for(int team = 0; team < 2; ++team)
        for(int i = 0; i < s_Lg.nRows; ++i)
        {
            const LgRow& r = s_Lg.rows[i]; if(r.team != team) continue;
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + rh), team ? IM_COL32(120, 64, 18, 200) : IM_COL32(18, 58, 120, 200));
            const char* nm = r.id < MP_MAX_PLAYERS && MP::players[r.id].active ? MP::players[r.id].name : "?";
            char b[48]; snprintf(b, sizeof(b), "%s%s", r.flags & 1 ? "[MVP] " : "", nm);
            dl->AddText(UI::Font(), UI::S(16), ImVec2(x + w * cx[0], y + UI::S(6)), IM_COL32_WHITE, b);
            const int v[5] = { r.score, r.goals, r.assists, r.saves, r.shots };
            for(int k = 0; k < 5; ++k) { snprintf(b, sizeof(b), "%d", v[k]); dl->AddText(UI::Font(), UI::S(16), ImVec2(x + w * cx[k + 1], y + UI::S(6)), IM_COL32_WHITE, b); }
            y += rh;
        }
    if(s_Lg.hasMe)
    {
        static const char* const dv[4] = { "I", "II", "III", "IV" };
        char b[96]; const int t = s_Lg.me.tier < 8 ? s_Lg.me.tier : 0;
        snprintf(b, sizeof(b), "YOUR RANK  %s %s  \xC2\xB7  %d MMR  \xC2\xB7  %u W / %u games", g_LgTierName[t], t >= 7 ? "" : dv[s_Lg.me.div & 3], s_Lg.me.mmr, s_Lg.me.wins, s_Lg.me.games);
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + rh), IM_COL32(6, 20, 24, 225));
        dl->AddText(UI::Font(), UI::S(15), ImVec2(x + w * 0.04f, y + UI::S(7)), LgCol(g_LgTierCol[t]), b);
    }
}

static bool LgHoldButton(const char* id, const char* label, ImVec2 at, float r, ImU32 col, bool& tapped)
{
    ImGui::SetNextWindowPos(ImVec2(at.x - r, at.y - r)); ImGui::SetNextWindowSize(ImVec2(r * 2, r * 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    bool held = false; tapped = false;
    if(ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoFocusOnAppearing))
    {
        tapped = ImGui::InvisibleButton("##b", ImVec2(r * 2, r * 2));
        held = ImGui::IsItemActive();
        if(ImGui::IsItemActivated()) tapped = true;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCircleFilled(at, r - 2, held ? IM_COL32(20, 40, 44, 230) : IM_COL32(10, 26, 28, 170), 32);
        dl->AddCircle(at, r - 2, col, 32, UI::S(2.5f));
        LgText(dl, UI::S(17), at, col, label, false);
    }
    ImGui::End(); ImGui::PopStyleVar(2);
    return held;
}

static void LgDrawLobby(ImDrawList* dl, ImVec2 ds)
{
    // the RL car names over the parked cars (white caps, like FENNEC / OCTANE in SA:O)
    for(int i = 0; i < LG_CAR_COUNT; ++i)
    {
        const int c = s_Lg.lobbyCar[i]; if(!LgCarOk(c)) continue;
        Vec3 p; if(!Game::GetCarPos(c, p)) continue;
        ImVec2 s; if(!LgW2S({ p.x, p.y, p.z + 1.75f }, s)) continue;
        LgText(dl, UI::S(15), s, IM_COL32(250, 250, 250, 240), g_LgCars[i].name);
    }
    LgText(dl, UI::S(19), ImVec2(ds.x * 0.5f, UI::S(30)), IM_COL32(110, 220, 230, 255), "ROCKET LEAGUE");
    dl->AddText(UI::Font(), UI::S(14), ImVec2(UI::S(70), UI::S(96)), IM_COL32(220, 225, 225, 170), "Choose a car.  Settings: /team  /stats  /top  /help");
    if(s_Lg.view == 1) LgText(dl, UI::S(17), ImVec2(ds.x * 0.5f, ds.y - UI::S(56)), IM_COL32(250, 250, 250, 245), "Choose a car, then your game mode and arena");
    if(s_Lg.hasMe)
    {
        static const char* const dv[4] = { "I", "II", "III", "IV" };
        const int t = s_Lg.me.tier < 8 ? s_Lg.me.tier : 0;
        char b[64]; snprintf(b, sizeof(b), "%s %s  \xC2\xB7  %d MMR", g_LgTierName[t], t >= 7 ? "" : dv[s_Lg.me.div & 3], s_Lg.me.mmr);
        dl->AddText(UI::Font(), UI::S(14), ImVec2(UI::S(70), UI::S(118)), LgCol(g_LgTierCol[t], 0.9f), b);
    }
    if(s_Lg.view != 2) return;
    // the mode / arena card
    const ImVec2 sz(UI::S(440), UI::S(330));
    ImGui::SetNextWindowPos(ImVec2((ds.x - sz.x) * 0.5f, ds.y * 0.5f - sz.y * 0.45f)); ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(6, 22, 26, 232));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(20, 58, 64, 255)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(30, 86, 94, 255)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(48, 140, 150, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(6)); ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(4));
    if(ImGui::Begin("##LgMode", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImDrawList* w = ImGui::GetWindowDrawList(); ImVec2 o = ImGui::GetWindowPos();
        char t[48]; snprintf(t, sizeof(t), "%s", s_Lg.carIdx >= 0 ? g_LgCars[s_Lg.carIdx].name : "CAR");
        LgText(w, UI::S(22), ImVec2(o.x + sz.x * 0.5f, o.y + UI::S(26)), IM_COL32(110, 220, 230, 255), t);
        LgText(w, UI::S(14), ImVec2(o.x + sz.x * 0.5f, o.y + UI::S(50)), IM_COL32(200, 210, 210, 220), "Game mode  \xC2\xB7  Arena: Sky Pitch above Los Santos");
        ImGui::SetCursorPos(ImVec2(UI::S(20), UI::S(72)));
        const float bw = sz.x - UI::S(40), bh = UI::S(62);
        if(ImGui::Button("RANKED SOCCAR   (up to 8v8)", ImVec2(bw, bh))) LgGoArena(false);
        ImGui::SetCursorPosX(UI::S(20));
        if(ImGui::Button("FREE PLAY   (you and the ball)", ImVec2(bw, bh))) LgGoArena(true);
        ImGui::SetCursorPosX(UI::S(20));
        if(ImGui::Button("ANOTHER CAR", ImVec2(bw, UI::S(48))))
        {
            int ped = Game::LocalPed();
            if(ped > 0 && LgCarOk(s_Lg.myCar)) { Vec3 p; Game::GetCarPos(s_Lg.myCar, p); p.x += 2.6f; Game::WarpOutOfCar(ped, p); Game::SetCarEngineOn(s_Lg.myCar, false); }
            s_Lg.view = 1; s_Lg.myCar = 0; s_Lg.carIdx = -1;
        }
    }
    ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(4);
}

static void RenderLg()
{
    if(!s_Lg.engaged || !LgOnServer()) return;
    if(FloatMenu::IsModal() || MP::AuthScreenShown() || Game::IsPaused()) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList(); ImDrawList* fg = ImGui::GetForegroundDrawList();
    const uint64_t now = MP_NowMs();
    if(s_Lg.view == 1 || s_Lg.view == 2) LgDrawLobby(fg, ds);
    else if(s_Lg.view == 3)
    {
        LgDrawField(bg);
        LgDrawBall(bg);
        LgDrawTop(fg, ds);
        LgDrawBoost(fg, ds);
        // BOOST + JUMP (left of the gauge, over GTA's accelerate / brake pedals are on the right edge)
        bool tap = false;
        const float r = UI::S(46);
        s_Lg.boostHeld = LgHoldButton("##LgBoost", "BOOST", ImVec2(ds.x - UI::S(330), ds.y - UI::S(90)), r, IM_COL32(242, 168, 40, 255), tap);
        LgHoldButton("##LgJump", "JUMP", ImVec2(ds.x - UI::S(330), ds.y - UI::S(200)), r, IM_COL32(236, 226, 196, 255), tap);
        if(tap) s_Lg.jumpTap = true;
        // the top-left buttons: scoreboard + lobby
        ImGui::SetNextWindowPos(ImVec2(UI::S(70), UI::S(90))); ImGui::SetNextWindowSize(ImVec2(UI::S(250), UI::S(46)));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0)); ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(6, 22, 26, 200));
        if(ImGui::Begin("##LgTop", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            if(ImGui::Button("SCORES", ImVec2(UI::S(112), UI::S(36)))) s_Lg.board = !s_Lg.board;
            ImGui::SameLine();
            if(ImGui::Button("LOBBY", ImVec2(UI::S(112), UI::S(36)))) { if(!s_Lg.freePlay) LgSend(LGC_LEAVE_ARENA, 0); LgToLobby(Game::LocalPed()); }
        }
        ImGui::End(); ImGui::PopStyleColor(2);
        if(s_Lg.board) LgDrawBoard(fg, ds);
        LgText(fg, UI::S(14), ImVec2(ds.x * 0.5f, ds.y - UI::S(22)), IM_COL32(235, 240, 240, 200),
               "BOOST: hold  \xC2\xB7  JUMP: tap, tap again in the air = dodge  \xC2\xB7  SCORES  \xC2\xB7  /team blue|orange  \xC2\xB7  /ff");
    }
    // banners (goal / overtime / end) + the feed
    if(now < s_Lg.bannerUntil && s_Lg.banner[0])
    {
        const float a = (s_Lg.bannerUntil - now) < 400 ? (s_Lg.bannerUntil - now) / 400.0f : 1.0f;
        LgText(fg, UI::S(54), ImVec2(ds.x * 0.5f, ds.y * 0.30f), LgCol(s_Lg.bannerCol, a), s_Lg.banner);
        if(s_Lg.bannerSub[0]) LgText(fg, UI::S(20), ImVec2(ds.x * 0.5f, ds.y * 0.30f + UI::S(44)), IM_COL32(255, 255, 255, (int)(230 * a)), s_Lg.bannerSub);
    }
    for(int i = 0; i < s_Lg.feedN; ++i)
    {
        const uint64_t age = now - s_Lg.feedAt[i]; if(age > 6000) continue;
        const float a = age > 5000 ? 1.0f - (age - 5000) / 1000.0f : 1.0f;
        fg->AddText(UI::Font(), UI::S(15), ImVec2(ds.x - UI::S(330), UI::S(110) + i * UI::S(22)), IM_COL32(240, 245, 245, (int)(230 * a)), s_Lg.feed[i]);
    }
    char build[64]; snprintf(build, sizeof(build), "MP %s - Rocket League", MP_VERSION_STR);
    ImVec2 bs = UI::Font()->CalcTextSizeA(UI::S(11), FLT_MAX, 0, build);
    fg->AddText(UI::Font(), UI::S(11), ImVec2(ds.x - bs.x - UI::S(8), ds.y - bs.y - UI::S(4)), IM_COL32(230, 230, 230, 140), build);
}

// launcher / menu entry: join the 24/7 Rocket League (the Broker bot on GitHub Actions)
void MP::JoinLg()
{
    if(LgOnServer()) { LeaveServer("Left Rocket League."); return; }
    s_bLgJoin = true;
    MP::JoinServer("", MP_LG_PORT, MP_LG_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bLgJoin = false;
}
