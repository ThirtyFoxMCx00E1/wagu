// MP 4.13.4 (included by mp.cpp): San Andreas Online League - the phone side of Rocket Soccer.
//
// The HUD is the one of the original SA:O / MTA Neon "GTA Rocket League" (DryxioGTA,
// github.com/ThirtyFoxMCx00E1/san-andreas-online):
//   lobby (Grove Street at night): the cars parked with their RL name in white caps over them, cyan
//     "SA:O / ROCKET SOCCER" at the top, a small "Choose a car. Settings: ..." line top left and
//     "Choose a car, then your game mode and arena" at the bottom
//   match: a small white status line at the top centre with "0 : 0" under it, the boost gauge at the
//     bottom right (a thick dark ring, 12 cream ticks, a big squared white number, a small orange
//     BOOST), navy name boxes over the cars, a small control hint at the bottom, the build bottom right
// The server owns the ball (server_league.inc); the phone predicts it between the 10 Hz states
// and draws it (a big dark RL ball with teal lights) - a GTA object can't be scaled that big.

static const Vec3  kLgLobby = { 2495.20f, -1672.60f, 13.34f };    // Grove Street, between the houses
static const float kLgLobbyHeading = 0.0f;
static const Vec3  kLgLobbyCars[LG_CAR_COUNT] = {
    { 2486.0f, -1664.0f, 13.34f }, { 2491.5f, -1660.5f, 13.34f }, { 2498.0f, -1659.5f, 13.34f },
    { 2504.0f, -1661.5f, 13.34f }, { 2509.0f, -1665.5f, 13.34f }, { 2480.5f, -1668.5f, 13.34f },
};
static const float kLgLobbyCarHead[LG_CAR_COUNT] = { 210, 190, 180, 165, 140, 235 };
// boost pads (relative): 6 big (100) like RL Soccar + small ones (+12)
struct LgPad { float x, y; bool big; uint64_t backAt; };
static LgPad s_LgPads[] = {
    { -46, -30, true, 0 }, { -46, 30, true, 0 }, { 46, -30, true, 0 }, { 46, 30, true, 0 }, { 0, -32, true, 0 }, { 0, 32, true, 0 },
    { -24, 0, false, 0 }, { 24, 0, false, 0 }, { -12, -18, false, 0 }, { -12, 18, false, 0 }, { 12, -18, false, 0 }, { 12, 18, false, 0 },
    { -36, -10, false, 0 }, { -36, 10, false, 0 }, { 36, -10, false, 0 }, { 36, 10, false, 0 }, { 0, -10, false, 0 }, { 0, 10, false, 0 },
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
bool MP::LgOwnTags() { return s_Lg.engaged && LgOnServer() && s_Lg.view >= 1; }

static Vec3 LgWorld(float x, float y, float z) { return { kLgCenter[0] + x, kLgCenter[1] + y, kLgCenter[2] + z }; }
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
                    if(s_Lg.view != 3 || s_Lg.freePlay) break;
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
    if(now >= s_Lg.keepAt) { s_Lg.keepAt = now + 2000; LgSetupCar(s_Lg.myCar, s_Lg.freePlay ? 255 : s_Lg.me.team); Game::FixCar(s_Lg.myCar); }
    // kickoff: the cars wait for GO
    const bool kick = !s_Lg.freePlay && s_Lg.hasState && s_Lg.st.phase == LGP_KICKOFF && s_Lg.me.team <= 1;
    if(kick != s_Lg.frozen) { Game::FreezeCar(s_Lg.myCar, kick); s_Lg.frozen = kick; }

    Vec3 cp; if(!Game::GetCarPos(s_Lg.myCar, cp)) return;
    Vec3 v = { 0, 0, 0 }; Game::GetMoveSpeed(s_Lg.myCar, true, v);
    float rel[3] = { cp.x - kLgCenter[0], cp.y - kLgCenter[1], cp.z - kLgCenter[2] };
    // far away (fell through, drove off): back on the pitch
    if(fabsf(rel[0]) > LG_HALF_L + 25 || fabsf(rel[1]) > LG_HALF_W + 25 || rel[2] < -15 || rel[2] > 80)
    {
        Game::SetCarPos(s_Lg.myCar, LgWorld(s_Lg.me.team == 1 ? 40.0f : -40.0f, 0, 1.0f), 0.0f); Game::SetMoveSpeed(s_Lg.myCar, true, { 0, 0, 0 });
        return;
    }
    // the arena walls (invisible): bounce back like the RL cage
    bool hit = false;
    const bool inMouth = fabsf(rel[1]) < LG_GOAL_HALF_W && rel[2] < LG_GOAL_H;
    const float lx = inMouth ? LG_HALF_L + LG_GOAL_DEPTH - 1.5f : LG_HALF_L - 1.2f;
    if(rel[0] > lx && v.x > 0) { v.x = -v.x * 0.4f; hit = true; } else if(rel[0] < -lx && v.x < 0) { v.x = -v.x * 0.4f; hit = true; }
    const float ly = fabsf(rel[0]) > LG_HALF_L ? LG_GOAL_HALF_W - 1.2f : LG_HALF_W - 1.2f;
    if(rel[1] > ly && v.y > 0) { v.y = -v.y * 0.4f; hit = true; } else if(rel[1] < -ly && v.y < 0) { v.y = -v.y * 0.4f; hit = true; }

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
        if(g && s_Lg.freePlay) { s_Lg.fpScore[g - 1]++; LgBanner("GOAL!", g == 1 ? "into the orange net" : "into the blue net", g == 1 ? 0x3FA0FFFF : 0xFF9A2EFF, 2500); LgBallReset(s_Lg.ball); }
    }
    if(!s_Lg.freePlay && s_Lg.hasState && s_Lg.st.phase == LGP_KICKOFF) LgBallReset(s_Lg.ball);
    const float bx = s_Lg.ball.p[0] - rel[0], by = s_Lg.ball.p[1] - rel[1], bz = s_Lg.ball.p[2] - (rel[2] + 0.4f);
    const float reach = LG_BALL_R + 2.1f;
    if(bx * bx + by * by + bz * bz < reach * reach && now - s_Lg.touchAt > 120 && !kick)
    {
        s_Lg.touchAt = now;
        float cv[3] = { v.x, v.y, v.z };
        LgBallHit(s_Lg.ball, rel, cv);   // instant feedback; the server answers with its own ball
        if(!s_Lg.freePlay) { float wp[3] = { cp.x, cp.y, cp.z }; LgSend(LGC_TOUCH, 0, wp, cv); }
    }
}

static void LgLobbyProcess(int ped, uint64_t now)
{
    if(now >= s_Lg.nightAt) { s_Lg.nightAt = now + 4000; Game::SetTimeOfDay(23, 30); }
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
    Game::TeleportGround(ped, LgWorld(side * 40.0f, 0, 1.0f), side < 0 ? 270.0f : 90.0f);   // the car comes along
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
    else if(s_Lg.view == 3) LgArenaProcess(ped, now, dt);
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
static void LgDrawBoost(ImDrawList* dl, ImVec2 ds)
{
    const float R = UI::S(92), th = UI::S(15);
    const ImVec2 c(ds.x - R - UI::S(46), ds.y - R - UI::S(40));
    dl->AddCircle(c, R, IM_COL32(8, 26, 26, 235), 64, th);
    // the boost amount as a light arc inside the ring (RL fills it clockwise from the bottom)
    const float f = s_Lg.boost / 100.0f;
    if(f > 0.005f)
    {
        const float a0 = IM_PI * 0.5f, a1 = a0 + f * IM_PI * 2.0f;
        dl->PathArcTo(c, R, a0, a1, 64); dl->PathStroke(IM_COL32(255, 150, 40, 120), 0, th * 0.35f);
    }
    for(int i = 0; i < 12; ++i)
    {   // 12 small cream ticks on the ring
        const float a = i * IM_PI * 2.0f / 12.0f;
        const ImVec2 dir(cosf(a), sinf(a));
        dl->AddLine(ImVec2(c.x + dir.x * (R - th * 0.32f), c.y + dir.y * (R - th * 0.32f)), ImVec2(c.x + dir.x * (R + th * 0.32f), c.y + dir.y * (R + th * 0.32f)), IM_COL32(236, 226, 196, 235), UI::S(2.2f));
    }
    char b[8]; snprintf(b, sizeof(b), "%d", (int)(s_Lg.boost + 0.5f));
    const int n = (int)strlen(b);
    const float dw = UI::S(36), dh = UI::S(46), st = UI::S(7.5f), gap = UI::S(14);
    const float tw = n * dw + (n - 1) * gap;
    float x = c.x - tw * 0.5f;
    for(int i = 0; i < n; ++i) { LgDigit(dl, ImVec2(x + 2, c.y - dh * 0.62f + 2), dw, dh, st, b[i] - '0', IM_COL32(0, 0, 0, 90)); LgDigit(dl, ImVec2(x, c.y - dh * 0.62f), dw, dh, st, b[i] - '0', IM_COL32(242, 240, 228, 255)); x += dw + gap; }
    LgText(dl, UI::S(15), ImVec2(c.x, c.y + R * 0.50f), IM_COL32(242, 168, 40, 255), "BOOST", false);
}
static void LgDrawTop(ImDrawList* dl, ImVec2 ds)
{
    char line[96];
    const PktLgState& s = s_Lg.st;
    if(s_Lg.freePlay) snprintf(line, sizeof(line), "FREE PLAY \xC2\xB7 LV Airport \xC2\xB7 ball %d km/h \xC2\xB7 boost %d %%",
                               (int)(sqrtf(s_Lg.ball.v[0] * s_Lg.ball.v[0] + s_Lg.ball.v[1] * s_Lg.ball.v[1] + s_Lg.ball.v[2] * s_Lg.ball.v[2]) * 3.6f), (int)s_Lg.boost);
    else
    {
        static const char* const ph[5] = { "WARMUP", "KICKOFF", "LIVE", "GOAL REPLAY", "MATCH OVER" };
        const char* p = s.phase < 5 ? ph[s.phase] : "";
        if(s.overtime) snprintf(line, sizeof(line), "RANKED SOCCAR \xC2\xB7 OVERTIME +%d:%02d \xC2\xB7 %dv%d \xC2\xB7 %s", s.clock / 60, s.clock % 60, s.blueN, s.orangeN, p);
        else snprintf(line, sizeof(line), "RANKED SOCCAR \xC2\xB7 %d:%02d \xC2\xB7 %dv%d \xC2\xB7 %s", s.clock / 60, s.clock % 60, s.blueN, s.orangeN, p);
    }
    LgText(dl, UI::S(19), ImVec2(ds.x * 0.5f, UI::S(30)), IM_COL32(238, 244, 244, 255), line);
    char sc[24];
    if(s_Lg.freePlay) snprintf(sc, sizeof(sc), "%d : %d", s_Lg.fpScore[0], s_Lg.fpScore[1]);
    else snprintf(sc, sizeof(sc), "%d : %d", s.score[0], s.score[1]);
    LgText(dl, UI::S(30), ImVec2(ds.x * 0.5f, UI::S(70)), IM_COL32(250, 250, 250, 255), sc);
    if(!s_Lg.freePlay && s.phase == LGP_KICKOFF && s.phaseMs > 0)
    {
        char k[8]; snprintf(k, sizeof(k), "%d", (s.phaseMs + 999) / 1000);
        LgText(dl, UI::S(96), ImVec2(ds.x * 0.5f, ds.y * 0.36f), IM_COL32(255, 255, 255, 240), k);
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
    Seg(-LG_HALF_L, -LG_HALF_W, z, LG_HALF_L, -LG_HALF_W, z, line, w); Seg(-LG_HALF_L, LG_HALF_W, z, LG_HALF_L, LG_HALF_W, z, line, w);
    Seg(-LG_HALF_L, -LG_HALF_W, z, -LG_HALF_L, LG_HALF_W, z, line, w); Seg(LG_HALF_L, -LG_HALF_W, z, LG_HALF_L, LG_HALF_W, z, line, w);
    Seg(0, -LG_HALF_W, z, 0, LG_HALF_W, z, line, w);
    for(int i = 0; i < 24; ++i)
    {   // centre circle
        const float a0 = i * IM_PI * 2 / 24, a1 = (i + 1) * IM_PI * 2 / 24, r = 9.0f;
        ImVec2 p0, p1; if(LgW2S(LgWorld(cosf(a0) * r, sinf(a0) * r, z), p0) && LgW2S(LgWorld(cosf(a1) * r, sinf(a1) * r, z), p1)) dl->AddLine(p0, p1, line, w);
    }
    // the cage walls (top rails) + the goal frames
    const float wh = 4.0f;
    Seg(-LG_HALF_L, -LG_HALF_W, wh, LG_HALF_L, -LG_HALF_W, wh, wall, w); Seg(-LG_HALF_L, LG_HALF_W, wh, LG_HALF_L, LG_HALF_W, wh, wall, w);
    for(int s = -1; s <= 1; s += 2)
    {
        const float x = s * LG_HALF_L;
        Seg(x, -LG_HALF_W, wh, x, -LG_GOAL_HALF_W, wh, wall, w); Seg(x, LG_GOAL_HALF_W, wh, x, LG_HALF_W, wh, wall, w);
        const ImU32 gc = s > 0 ? IM_COL32(255, 154, 46, 230) : IM_COL32(63, 160, 255, 230);
        const float gw = UI::S(4.0f);
        Seg(x, -LG_GOAL_HALF_W, 0, x, -LG_GOAL_HALF_W, LG_GOAL_H, gc, gw); Seg(x, LG_GOAL_HALF_W, 0, x, LG_GOAL_HALF_W, LG_GOAL_H, gc, gw);
        Seg(x, -LG_GOAL_HALF_W, LG_GOAL_H, x, LG_GOAL_HALF_W, LG_GOAL_H, gc, gw);
        const float bx = x + s * LG_GOAL_DEPTH;
        Seg(bx, -LG_GOAL_HALF_W, 0, bx, LG_GOAL_HALF_W, 0, gc, w); Seg(x, -LG_GOAL_HALF_W, LG_GOAL_H, bx, -LG_GOAL_HALF_W, LG_GOAL_H * 0.7f, gc, w); Seg(x, LG_GOAL_HALF_W, LG_GOAL_H, bx, LG_GOAL_HALF_W, LG_GOAL_H * 0.7f, gc, w);
    }
    // boost pads
    const uint64_t now = MP_NowMs();
    for(const auto& p : s_LgPads)
    {
        ImVec2 c; if(!LgW2S(LgWorld(p.x, p.y, 0.1f), c)) continue;
        ImVec2 e; if(!LgW2S(LgWorld(p.x + (p.big ? 1.6f : 0.9f), p.y, 0.1f), e)) continue;
        const float r = fabsf(e.x - c.x) + fabsf(e.y - c.y) * 0.3f; if(r < 1.5f || r > 300) continue;
        const bool on = !p.backAt || now >= p.backAt;
        dl->AddCircleFilled(c, r, on ? IM_COL32(255, 170, 40, p.big ? 150 : 110) : IM_COL32(90, 70, 40, 70), 20);
        if(on && p.big) dl->AddCircle(c, r * 1.25f, IM_COL32(255, 210, 120, 160), 20, UI::S(2));
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
    LgText(dl, UI::S(19), ImVec2(ds.x * 0.5f, UI::S(30)), IM_COL32(110, 220, 230, 255), "SA:O / ROCKET SOCCER");
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
        LgText(w, UI::S(14), ImVec2(o.x + sz.x * 0.5f, o.y + UI::S(50)), IM_COL32(200, 210, 210, 220), "Game mode  \xC2\xB7  Arena: LV Airport (Neon Pitch)");
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
    char build[64]; snprintf(build, sizeof(build), "MP %s - San Andreas Online League", MP_VERSION_STR);
    ImVec2 bs = UI::Font()->CalcTextSizeA(UI::S(11), FLT_MAX, 0, build);
    fg->AddText(UI::Font(), UI::S(11), ImVec2(ds.x - bs.x - UI::S(8), ds.y - bs.y - UI::S(4)), IM_COL32(230, 230, 230, 140), build);
}

// launcher / menu entry: join the 24/7 San Andreas Online League (the Broker bot on GitHub Actions)
void MP::JoinLg()
{
    if(LgOnServer()) { LeaveServer("Left San Andreas Online League."); return; }
    s_bLgJoin = true;
    MP::JoinServer("", MP_LG_PORT, MP_LG_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bLgJoin = false;
}
