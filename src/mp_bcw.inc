// MP 3.04 - Brief Case War, client side (included by mp.cpp after mp_world.inc).
//
// Joining "Brief Case War" never shows the Santa Maria beach: the connection screen looks at the
// war map (Area 69 on Bone County) and the < Spawn > picker is replaced by two team buttons -
// Gustav Team (red) and Schelapz Team (blue). After a pick you wait, frozen, until the queue holds
// 20 players; then a slot machine picks one leader per team and everybody spawns in their safe
// zone with weapons. The server (net/server_bcw.inc) is the referee; this file draws and plays:
//   - red / blue glowing safe-zone circles (9 m) with flowing particles + big floating base names
//   - 3 vehicle spawner circles per base (4 m): military cars, tank, helicopter (+ the pick list)
//   - the animated yellow-orange world barrier (240 m around the war area) that you can't cross
//   - the white steal bar, x-ray outlines of briefcase carriers (seen through walls), the dropped
//     briefcase highlight, respawn on the connection screen (10 s + 20 ticks), then in your safe zone (never at the hospital / your save)
//   - [GTV] / [SLZ] name tags, chat names and kill feed in team colours (admins keep their look)
//   - war_ambient.mp3 on loop at 45 % (starts 4 s + 20 ticks after connecting)
//   - victory -> back to the connection screen, scoreboard (top 3 + both teams), map vote


struct BcwClientState
{
    bool        engaged = false;      // connected to Brief Case War and this module drives the screen
    int         view = 0;             // 0 off, 1 lobby (connection screen), 2 playing, 3 dead (respawn wait)
    PktBcwState st{};  bool haveState = false;
    PktBcwRoll  roll{}; bool haveRoll = false; uint64_t rollAt = 0;
    PktBcwScore score[2]{}; bool haveScore[2] = { false, false }; bool scoreClosed = false; uint64_t scoreAt = 0;
    uint64_t    connectedAt = 0; bool musicTried = false, musicMissingSaid = false;
    uint64_t    deathAt = 0, heldAt = 0; bool held = false;   // dead: game respawned us (hospital) and we're parked hidden
    uint64_t    protectUntil = 0, lastVeh = 0, lastHudOff = 0, lastWanted = 0, lastNoWanted = 0;   // 3.21: lastNoWanted
    int         lobbyMap = -1;
    int         homeObj[2] = { 0, 0 }, carryObj[2] = { 0, 0 }, carryPed[2] = { 0, 0 };
    float       groundZ[BCW_MAP_COUNT][2] = {};
    bool        groundOk[BCW_MAP_COUNT][2] = {};
    int         spawner = -1;         // spawner circle I'm standing in (0..2), -1 none
    char        banner[96] = ""; uint32_t bannerCol = 0xFFFFFFFF; uint64_t bannerAt = 0;
    int         prevState = -1;
    float       barrierDist = 9999.0f;
    uint64_t    coolAt = 0;
    uint8_t     lobbyTeam = 0;        // team whose base the connection camera shows (0 = map overview)           // cooldown started (local clock)
};
static BcwClientState s_Bcw;

static bool BcwOnServer()
{
    return client.GetState() == NET_CONNECTED && MP_IsBcwServerName(client.GetServerName()[0] ? client.GetServerName() : s_szCurServer);
}
static int  BcwCurMap()      { int m = s_Bcw.haveState ? s_Bcw.st.map : 0; return m < 0 || m >= BCW_MAP_COUNT ? 0 : m; }
static uint8_t BcwTeamOf(int id) { return (s_Bcw.haveState && id >= 0 && id < MP_MAX_PLAYERS) ? s_Bcw.st.team[id] : 0; }
static uint8_t BcwMyTeam() { return BcwTeamOf(myId); }
static bool BcwSameTeam(int a, int b) { if(!BcwOnServer()) return false; uint8_t x = BcwTeamOf(a); return x && x == BcwTeamOf(b); }
static bool BcwHidden()   { return s_Bcw.engaged && (s_Bcw.view == 1 || (s_Bcw.view == 3 && s_Bcw.held)); }
static const char* BcwName(int id)
{
    if(id == myId) return settings.name;
    if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active) return players[id].name;
    return "?";
}
static void BcwBanner(uint32_t col, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Bcw.banner, sizeof(s_Bcw.banner), fmt, ap); va_end(ap);
    s_Bcw.bannerCol = col; s_Bcw.bannerAt = MP_NowMs();
}
static void BcwSend(uint8_t cmd, uint8_t arg) { PktBcwCmd c; c.type = PKT_BCW_CMD; c.cmd = cmd; c.arg = arg; client.Send(&c, sizeof(c)); }

static float BcwGround(int map, int base)
{
    if(!s_Bcw.groundOk[map][base])
    {
        const float* p = g_BcwMaps[map].spawn[base];
        float g = Game::GroundZ(p[0], p[1], p[2] + 3.0f);
        if(g > -50.0f && fabsf(g - p[2]) < 12.0f) { s_Bcw.groundZ[map][base] = g + 0.05f; s_Bcw.groundOk[map][base] = true; }
        else return p[2] - 1.0f;
    }
    return s_Bcw.groundZ[map][base];
}
static void BcwHeadingTo(const float* from, const float* to, float& h) { h = atan2f(-(to[0] - from[0]), to[1] - from[1]) * 57.29578f; }

// ---- name tag / chat / kill feed decoration (MP::BcwDecorate is used by the HUD too) -------------
uint32_t MP::BcwDecorate(int id, const char* name, uint32_t color, char* out, size_t n)
{
    if(!name) name = "";
    uint8_t t = BcwOnServer() ? BcwTeamOf(id) : 0;
    bool admin = !strncasecmp(name, "[ADMIN]", 7) || (id == myId && MP::IsLocalAdmin());
    if(!t || admin) { snprintf(out, n, "%s", name); return color; }   // admins keep their own tag and colour
    int ti = BcwTeamIndex(t);
    bool leader = s_Bcw.st.state == BCW_LIVE && ti >= 0 && s_Bcw.st.leader[ti] == id;
    snprintf(out, n, "%s %s%s", g_BcwTeamTag[t], name, leader ? " [Leader]" : "");
    return g_BcwTeamRGBA[t];
}
static void BcwDecorateStr(int id, std::string& name, uint32_t& color)
{
    char b[64]; color = MP::BcwDecorate(id, name.c_str(), color, b, sizeof(b)); name = b;
}

// ---- briefcase objects (model 1210) -----------------------------------------------------------
static void BcwDeleteObjects()
{
    for(int c = 0; c < 2; ++c)
    {
        Game::DeleteWorldObject(s_Bcw.homeObj[c]); s_Bcw.homeObj[c] = 0;
        Game::DeleteWorldObject(s_Bcw.carryObj[c]); s_Bcw.carryObj[c] = 0; s_Bcw.carryPed[c] = 0;
    }
}
static int BcwPedOf(int id)
{
    if(id == myId) return Game::LocalPed();
    if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active) return players[id].ped;
    return 0;
}
static void BcwUpdateObjects(uint64_t now)
{
    if(!s_Bcw.haveState || s_Bcw.st.state != BCW_LIVE) { BcwDeleteObjects(); return; }
    float spin = (float)((now / 8) % 360);
    for(int c = 0; c < 2; ++c)
    {
        const BcwCase& k = s_Bcw.st.cases[c];
        if(k.state == CASE_CARRIED)
        {
            Game::DeleteWorldObject(s_Bcw.homeObj[c]); s_Bcw.homeObj[c] = 0;
            int ped = BcwPedOf(k.carrier);
            if(ped != s_Bcw.carryPed[c] || !Game::WorldObjectExists(s_Bcw.carryObj[c]))
            {
                Game::DeleteWorldObject(s_Bcw.carryObj[c]); s_Bcw.carryObj[c] = 0; s_Bcw.carryPed[c] = 0;
                if(ped > 0)
                {
                    Vec3 p; Game::GetCharPos(ped, p);
                    int o = Game::CreateWorldObject(1210, p);
                    if(o) { Game::AttachObjectToChar(o, ped, { 0.32f, 0.05f, -0.15f }); s_Bcw.carryObj[c] = o; s_Bcw.carryPed[c] = ped; }
                }
            }
        }
        else
        {
            Game::DeleteWorldObject(s_Bcw.carryObj[c]); s_Bcw.carryObj[c] = 0; s_Bcw.carryPed[c] = 0;
            Vec3 at = { k.pos[0], k.pos[1], k.pos[2] };
            if(k.state == CASE_HOME) at.z = BcwGround(BcwCurMap(), c) + 1.0f;
            else at.z = Game::GroundZ(at.x, at.y, at.z + 2.0f) + 0.35f;
            if(!Game::WorldObjectExists(s_Bcw.homeObj[c])) s_Bcw.homeObj[c] = Game::CreateWorldObject(1210, at);
            if(s_Bcw.homeObj[c]) Game::MoveWorldObject(s_Bcw.homeObj[c], { at.x, at.y, at.z + 0.15f * sinf(now * 0.004f) }, spin);
        }
    }
}

// ---- lobby (connection screen) / spawn / respawn ------------------------------------------------
static void BcwHudOff(uint64_t now, bool force)
{
    if(force || now - s_Bcw.lastHudOff > 1000) { Game::ShowGameHud(false); Game::SetPlayerControl(false); s_Bcw.lastHudOff = now; }
}
// connection screen camera: the map overview, or - after you picked a team - your own base
static void BcwLobbyCam(int m, uint8_t team, Vec3& cam, Vec3& look)
{
    const BcwMap& M = g_BcwMaps[m];
    int ti = BcwTeamIndex(team);
    if(ti < 0) { cam = { M.cam[0], M.cam[1], M.cam[2] }; look = { M.look[0], M.look[1], M.look[2] }; return; }
    const float* b = M.spawn[ti]; const float* e = M.spawn[1 - ti];
    float dx = b[0] - e[0], dy = b[1] - e[1], l = sqrtf(dx * dx + dy * dy); if(l < 1) l = 1;
    dx /= l; dy /= l;   // behind the base, looking at it (and towards the enemy)
    cam  = { b[0] + dx * 55.0f - dy * 20.0f, b[1] + dy * 55.0f + dx * 20.0f, b[2] + 32.0f };
    look = { b[0] - dx * 10.0f, b[1] - dy * 10.0f, b[2] + 2.0f };
}
static void BcwEnterLobby(int ped)
{
    // 3.04 cooldown / reset: the war vehicle I spawned goes away with the match
    if(s_nSpawnedCar > 0)
    {
        if(ped > 0 && Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_nSpawnedCar) { Vec3 o; Game::GetCharPos(ped, o); o.z += 1.0f; Game::WarpOutOfCar(ped, o); }
        if(Game::GetDriverOfCar(s_nSpawnedCar) <= 0 || Game::GetDriverOfCar(s_nSpawnedCar) == ped) Game::DeleteCar(s_nSpawnedCar); else Game::ReleaseCar(s_nSpawnedCar);
        s_nSpawnedCar = 0;
    }
    BcwDeleteObjects();
    s_Bcw.protectUntil = 0;
    Game::SetMaxWanted(0); Game::ClearWanted();   // 3.21: the lobby camera / parking spot is in Bone County too
    int m = BcwCurMap();
    const BcwMap& M = g_BcwMaps[m];
    Vec3 cam = { M.cam[0], M.cam[1], M.cam[2] }, look = { M.look[0], M.look[1], M.look[2] };
    Vec3 tcam, tlook; uint8_t lt = s_Bcw.st.state == BCW_QUEUE || s_Bcw.st.state == BCW_ROLL ? BcwMyTeam() : 0;
    BcwLobbyCam(m, lt, tcam, tlook);
    if(ped > 0)
    {
        if(Game::IsCharInAnyCar(ped)) { Vec3 o = { look.x, look.y, look.z + 1.0f }; Game::WarpOutOfCar(ped, o); }
        Game::PreloadArea(look, false);
        Game::PreloadArea({ cam.x, cam.y, 15.0f }, true);
        Game::FreezeChar(ped, false);
        Game::PlaceChar(ped, { cam.x, cam.y, -100.0f });   // parked on the ground under the camera, hidden (far from both bases)
        Game::FreezeChar(ped, true);
        Game::SetCharVisible(ped, false);
        Game::SetCharGodMode(ped, true);
    }
    if(lt) Game::PreloadArea(tlook, true);
    Game::SetFixedCamera(tcam, tlook);
    BcwHudOff(MP_NowMs(), true);
    s_Bcw.lobbyTeam = lt;
    s_Bcw.view = 1; s_Bcw.lobbyMap = m; s_Bcw.held = false; s_Bcw.spawner = -1;
}
static void BcwGiveLoadout(int ped)
{
    Game::RemoveAllWeapons(ped);
    static const int kLoadout[] = { 16, 34, 25, 29, 24, 31 };   // grenades, sniper, shotgun, MP5, Desert Eagle, M4 (selected)
    for(int w : kLoadout) Game::GiveAndSelectWeapon(ped, w);
    Game::SetCharHealth(ped, 100);
    Game::AddArmour(ped, 100);
}
static void BcwSpawnInBase(int ped, uint64_t now)
{
    uint8_t t = BcwMyTeam(); int ti = BcwTeamIndex(t);
    if(ped <= 0 || ti < 0) return;
    const BcwMap& M = g_BcwMaps[BcwCurMap()];
    const float* base = M.spawn[ti]; const float* enemy = M.spawn[1 - ti];
    float a = (float)(rand() % 628) / 100.0f, r = 1.0f + (float)(rand() % 250) / 100.0f;   // spread inside the circle
    Vec3 at = { base[0] + cosf(a) * r, base[1] + sinf(a) * r, base[2] };
    float h; BcwHeadingTo(base, enemy, h);
    Game::SetMaxWanted(0); Game::ClearWanted();   // 3.21: BEFORE the teleport (Bone County / Area 69 = 4 stars at once)
    Game::PreloadArea(at, true);
    Game::FreezeChar(ped, false);
    Game::SetCharVisible(ped, !s_Cheat.invisible);
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::TeleportGround(ped, at, h);
    BcwGiveLoadout(ped);
    Game::SetCharGodMode(ped, true);   // spawn protection (the server also drops damage for 5 s)
    s_Bcw.protectUntil = now + BCW_SPAWN_PROTECT_MS;
    s_Bcw.view = 2; s_Bcw.held = false;
    Game::SetMaxWanted(0); Game::ClearWanted();
}
static void BcwHoldDead(int ped)
{   // 3.04.4: the game respawned us (hospital / save point): back to the Brief Case War connection
    // screen (looking at our base) with the respawn countdown button, parked hidden
    uint8_t t = BcwMyTeam(); int ti = BcwTeamIndex(t); if(ti < 0) ti = 0;
    const float* b = g_BcwMaps[BcwCurMap()].spawn[ti];
    Vec3 cam, look; BcwLobbyCam(BcwCurMap(), t, cam, look);
    Game::SetMaxWanted(0); Game::ClearWanted();   // 3.21: parked under Area 69 = restricted zone, no stars
    Game::PreloadArea(look, true);
    Game::FreezeChar(ped, false);
    Game::PlaceChar(ped, { b[0], b[1], -100.0f });
    Game::FreezeChar(ped, true);
    Game::SetCharVisible(ped, false);
    Game::SetCharGodMode(ped, true);
    Game::SetFixedCamera(cam, look);
    BcwHudOff(MP_NowMs(), true);
    s_Bcw.held = true; s_Bcw.heldAt = MP_NowMs();
}
static void BcwShutdown()
{
    if(!s_Bcw.engaged) return;
    Sound::StopMusic();
    BcwDeleteObjects();
    int ped = Game::LocalPed();
    if(ped > 0 && Game::IsReady())
    {
        Game::FreezeChar(ped, false);
        Game::SetCharVisible(ped, !s_Cheat.invisible);
        Game::SetCharGodMode(ped, false);
        if(s_Bcw.view == 1 || s_Bcw.view == 3) Game::Teleport(ped, kGroveStreet, kGroveHeading);   // never left hidden under Area 69
    }
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::SetMaxWanted(6);
    s_Bcw = BcwClientState();
    s_bBcwJoin = false;
}
// the join overlay reached the spawn picker: Brief Case War takes the screen over instead
static bool BcwJoinTakeover(int ped)
{
    if(!s_bBcwJoin && !BcwOnServer()) return false;
    s_bBcwJoin = false;
    s_Bcw.engaged = true; s_Bcw.connectedAt = s_Bcw.connectedAt ? s_Bcw.connectedAt : MP_NowMs();
    BcwEnterLobby(ped);
    return true;
}
static void BcwJoinCam(Vec3& pos, Vec3& look)
{
    const BcwMap& M = g_BcwMaps[BcwCurMap()];
    pos = { M.cam[0], M.cam[1], M.cam[2] }; look = { M.look[0], M.look[1], M.look[2] };
}

// ---- packets --------------------------------------------------------------------------------------
static void BcwOnPacket(const uint8_t* d, int len)
{
    if(len < 1) return;
    switch(d[0])
    {
    case PKT_BCW_STATE:
    {
        if(len < (int)sizeof(PktBcwState)) return;
        PktBcwState old = s_Bcw.st; bool had = s_Bcw.haveState;
        memcpy(&s_Bcw.st, d, sizeof(PktBcwState));
        s_Bcw.haveState = true;
        if(!had || old.state != s_Bcw.st.state)
        {
            if(s_Bcw.st.state == BCW_RESULTS && !s_Bcw.scoreAt) s_Bcw.scoreAt = MP_NowMs();
            if(s_Bcw.st.state == BCW_QUEUE) { s_Bcw.haveScore[0] = s_Bcw.haveScore[1] = false; s_Bcw.scoreClosed = false; s_Bcw.scoreAt = 0; s_Bcw.haveRoll = false; }
        }
        break;
    }
    case PKT_BCW_ROLL:
        if(len < (int)sizeof(PktBcwRoll)) return;
        memcpy(&s_Bcw.roll, d, sizeof(PktBcwRoll)); s_Bcw.haveRoll = true; s_Bcw.rollAt = MP_NowMs();
        break;
    case PKT_BCW_SCORE:
    {
        PktBcwScore s; memset(&s, 0, sizeof(s)); memcpy(&s, d, len < (int)sizeof(s) ? len : (int)sizeof(s));
        if(s.part > 1) return;
        if(s.n > BCW_TEAM_SIZE) s.n = BCW_TEAM_SIZE;
        s_Bcw.score[s.part] = s; s_Bcw.haveScore[s.part] = true; s_Bcw.scoreClosed = false; s_Bcw.scoreAt = MP_NowMs();
        break;
    }
    case PKT_BCW_EVENT:
    {
        if(len < (int)sizeof(PktBcwEvent)) return;
        const PktBcwEvent& e = *(const PktBcwEvent*)d;
        uint8_t mine = BcwMyTeam();
        const char* who = e.who < MP_MAX_PLAYERS ? BcwName(e.who) : "";
        uint32_t wc = e.who < MP_MAX_PLAYERS ? g_BcwTeamRGBA[BcwTeamOf(e.who)] : COL_WHITE;
        switch(e.ev)
        {
        case BCWE_STOLEN:   BcwBanner(wc, "%s STOLE THE %s BRIEFCASE", who, e.team == BCW_T_GUSTAV ? "GUSTAV" : "SCHELAPZ");
                            SysMsg(wc, "%s stole the %s briefcase!", who, g_BcwTeamName[e.team]); break;
        case BCWE_DROPPED:  BcwBanner(g_BcwTeamRGBA[e.team], "THE %s BRIEFCASE WAS DROPPED", e.team == BCW_T_GUSTAV ? "GUSTAV" : "SCHELAPZ");
                            SysMsg(COL_YELLOW, "The %s briefcase was dropped%s%s.", g_BcwTeamName[e.team], *who ? " by " : "", who); break;
        case BCWE_PICKED:   BcwBanner(wc, "%s PICKED UP THE %s BRIEFCASE", who, e.team == BCW_T_GUSTAV ? "GUSTAV" : "SCHELAPZ");
                            SysMsg(wc, "%s picked up the %s briefcase.", who, g_BcwTeamName[e.team]); break;
        case BCWE_RETURNED: BcwBanner(g_BcwTeamRGBA[e.team], "THE %s BRIEFCASE IS BACK HOME", e.team == BCW_T_GUSTAV ? "GUSTAV" : "SCHELAPZ");
                            SysMsg(g_BcwTeamRGBA[e.team], "The %s briefcase is back in its base%s%s.", g_BcwTeamName[e.team], *who ? " - returned by " : "", who); break;
        case BCWE_VICTORY:  BcwBanner(g_BcwTeamRGBA[e.team], "%s VICTORY", e.team == BCW_T_GUSTAV ? "GUSTAV TEAM" : "SCHELAPZ TEAM");
                            SysMsg(g_BcwTeamRGBA[e.team], "%s wins the Brief Case War!%s", g_BcwTeamName[e.team], mine ? (mine == e.team ? " (Victory)" : " (Defeat)") : ""); break;
        case BCWE_MATCH_START: BcwBanner(0xFFFFFFFF, "WAR! STEAL THE ENEMY BRIEFCASE"); SysMsg(COL_YELLOW, "Brief Case War started on %s. Steal the enemy briefcase and bring it to your base.", g_BcwMaps[BcwCurMap()].name); break;
        case BCWE_MAP:      if(e.team < BCW_MAP_COUNT) { BcwBanner(0xFFFFFFFF, "NEXT MAP: %s", g_BcwMaps[e.team].name); SysMsg(COL_YELLOW, "Next map: %s. Pick your team.", g_BcwMaps[e.team].name); } break;
        }
        break;
    }
    }
}

// ---- game tick ------------------------------------------------------------------------------------
static void BcwProcess(int ped, uint64_t now)
{
    Sound::MusicUpdate();
    if(!BcwOnServer())
    {
        if(s_Bcw.engaged) BcwShutdown();
        return;
    }
    if(!s_Bcw.engaged)
    {
        if(JoinFlowActive()) return;   // the join overlay hands over at the spawn picker (BcwJoinTakeover)
        s_Bcw.engaged = true; s_Bcw.connectedAt = now;   // plain join (game was still loading)
        BcwEnterLobby(ped);
    }
    if(!s_Bcw.connectedAt) s_Bcw.connectedAt = now;
    // 3.21: never wanted in Brief Case War, in EVERY view (lobby, playing, dead / respawning). Before,
    // the stars were only cleared while playing (view 2, every 0.5 s): teleporting into Bone County
    // (Area 69 / Verdant Meadows = restricted area) gave 4 stars at once and the police / SAM sites
    // came before the next clear. Now: max wanted 0 every 250 ms and any star is removed the same frame.
    if(Game::IsReady() && !Game::IsPaused())
    {
        if(now - s_Bcw.lastNoWanted >= 250) { s_Bcw.lastNoWanted = now; Game::SetMaxWanted(0); }
        if(Game::GetWantedLevel() > 0) Game::ClearWanted();
    }
    // music: 4 s + 20 ticks after connecting, fades in over 20 ticks, 45 %, endless
    if(now - s_Bcw.connectedAt >= BCW_MUSIC_DELAY_MS)
    {
        char path[512]; snprintf(path, sizeof(path), "%s/war_ambient.mp3", MP::DataDir());
        if(!s_Bcw.musicTried)
        {
            s_Bcw.musicTried = true;
            if(!Sound::PlayMusic(path, 0.0f, true) && !s_Bcw.musicMissingSaid)
            { s_Bcw.musicMissingSaid = true; SysMsg(COL_SYSTEM, "Put war_ambient.mp3 in files/MP to hear the war music."); }
        }
        float k = (float)(now - s_Bcw.connectedAt - BCW_MUSIC_DELAY_MS) / 1000.0f; if(k > 1) k = 1;
        Sound::SetMusicVolume(BCW_MUSIC_VOLUME * k);
    }
    if(ped <= 0 || !s_Bcw.haveState) return;
    const uint8_t team = BcwMyTeam();
    const bool live = s_Bcw.st.state == BCW_LIVE;
    // lobby <-> match
    if(s_Bcw.view == 1)
    {
        if(live && team) { BcwSpawnInBase(ped, now); SysMsg(g_BcwTeamRGBA[team], "You are in %s. Steal the %s briefcase!", g_BcwTeamName[team], team == BCW_T_GUSTAV ? "Schelapz" : "Gustav"); }
        else
        {
            uint8_t lt = s_Bcw.st.state == BCW_QUEUE || s_Bcw.st.state == BCW_ROLL ? team : 0;
            if(s_Bcw.lobbyMap != BcwCurMap()) BcwEnterLobby(ped);   // the vote picked another map
            else if(lt != s_Bcw.lobbyTeam)
            {   // picked Gustav -> Verdant Meadows, Schelapz -> Area 69
                Vec3 c, l; BcwLobbyCam(BcwCurMap(), lt, c, l);
                Game::PreloadArea(l, true);
                Game::SetFixedCamera(c, l);
                s_Bcw.lobbyTeam = lt;
            }
            BcwHudOff(now, false);
            if(now % 2000 < 40) { Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false); }
        }
    }
    else if(!live || !team)
    {   // victory / reset: everybody back to the connection screen
        if(s_bLocalDead) return;   // wait for the game to respawn us first
        BcwEnterLobby(ped);
    }
    else if(s_Bcw.view == 2)
    {
        if(s_bLocalDead) { s_Bcw.view = 3; s_Bcw.deathAt = now; s_Bcw.held = false; s_Bcw.spawner = -1; }
    }
    if(s_Bcw.view == 3)
    {
        if(!s_bLocalDead && !s_Bcw.held) BcwHoldDead(ped);
        if(s_Bcw.held) BcwHudOff(now, false);
        if(s_Bcw.held && now - s_Bcw.heldAt >= BCW_RESPAWN_MS) BcwSpawnInBase(ped, now);
    }
    if(s_Bcw.view == 2)
    {
        if(s_Bcw.protectUntil && now >= s_Bcw.protectUntil) { Game::SetCharGodMode(ped, false); s_Bcw.protectUntil = 0; }
        if(now - s_Bcw.lastWanted > 500) { s_Bcw.lastWanted = now; Game::ClearWanted(); Game::SetMaxWanted(0); }   // Area 69: no stars, no SAM sites
        // world barrier: you can't leave the war area (on foot, in a car, tank or helicopter)
        bool inCar = Game::IsCharInAnyCar(ped);
        int car = inCar ? Game::GetCarCharIsIn(ped) : 0;
        Vec3 me; if(car > 0) Game::GetCarPos(car, me); else Game::GetCharPos(ped, me);
        float cx, cy, d = BcwSegDist(BcwCurMap(), me.x, me.y, &cx, &cy);
        s_Bcw.barrierDist = BCW_BARRIER_R - d;
        if(d > BCW_BARRIER_R - 1.0f && d > 0.01f)
        {
            float nx = (me.x - cx) / d, ny = (me.y - cy) / d, want = BCW_BARRIER_R - 2.5f;
            Vec3 delta = { cx + nx * want - me.x, cy + ny * want - me.y, 0.0f };
            if(car > 0) { Game::NudgeEntity(car, true, delta); Vec3 v; if(Game::GetMoveSpeed(car, true, v)) { float out = v.x * nx + v.y * ny; if(out > 0) { v.x -= nx * out * 1.6f; v.y -= ny * out * 1.6f; Game::SetMoveSpeed(car, true, v); } } }
            else    { Game::NudgeEntity(ped, false, delta); Vec3 v; if(Game::GetMoveSpeed(ped, false, v)) { float out = v.x * nx + v.y * ny; if(out > 0) { v.x -= nx * out * 1.6f; v.y -= ny * out * 1.6f; Game::SetMoveSpeed(ped, false, v); } } }
        }
        // vehicle spawner circles (own base only, on foot)
        s_Bcw.spawner = -1;
        int ti = BcwTeamIndex(team);
        if(!inCar && ti >= 0)
            for(int k = 0; k < 3; ++k)
            {
                const float* sp = g_BcwMaps[BcwCurMap()].spawners[ti][k].pos;
                if(BcwDist2D(sp, me.x, me.y) <= BCW_SPAWNER_R && fabsf(me.z - sp[2]) < 5.0f) { s_Bcw.spawner = k; break; }
            }
    }
    BcwUpdateObjects(now);
}

static void BcwSpawnVehicle(int model)
{
    uint64_t now = MP_NowMs();
    if(now - s_Bcw.lastVeh < 8000) { SysMsg(COL_YELLOW, "Wait %d s for the next vehicle.", (int)((8000 - (now - s_Bcw.lastVeh)) / 1000) + 1); return; }
    int ped = Game::LocalPed(); if(ped <= 0) return;
    int ti = BcwTeamIndex(BcwMyTeam()); if(ti < 0) return;
    const BcwMap& M = g_BcwMaps[BcwCurMap()];
    float h; BcwHeadingTo(M.spawn[ti], M.spawn[1 - ti], h);
    Game::SetCharHeading(ped, h);
    if(SpawnVehicleForMe(model, false)) { s_Bcw.lastVeh = now; s_Bcw.spawner = -1; }   // shared with everybody like /spawn
}

// commands that make no sense (or would cheat) in a war
static bool BcwBlockCommand(const char* cmd)
{
    if(!BcwOnServer()) return false;
    static const char* const kOk[] = { "help", "q", "quit", "pm", "me", "players", "list", "score", "stats", "ping", "time", "clear", "id", "bcw", "team", "gatepos", "gates" };
    for(const char* c : kOk) if(!strcmp(cmd, c)) return false;
    if(!strcmp(cmd, "kill")) return false;
    SysMsg(COL_RED, "/%s is not available in Brief Case War.", cmd);
    return true;
}

// ================================ drawing =====================================================
static bool BcwW2S(const Vec3& w, ImVec2& out)
{
    float sx, sy, dep;
    if(!Game::WorldToScreen(w, sx, sy, dep) || dep < 0.5f) return false;
    out = ImVec2(sx, sy); return true;
}
static ImU32 BcwCol(uint32_t rgba, float a) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }
static float BcwCamDist(const Vec3& p)
{
    Vec3 c, l; if(!Game::GetCameraPos(c, l)) return 0;
    return sqrtf((c.x - p.x) * (c.x - p.x) + (c.y - p.y) * (c.y - p.y) + (c.z - p.z) * (c.z - p.z));
}
// glowing ground circle + a soft "mission marker" wall + particles flowing up (mission / cutscene style)
static void BcwDrawZone(ImDrawList* dl, const Vec3& c, float r, uint32_t rgba, float now, bool particles, float wallH)
{
    float d = BcwCamDist(c);
    if(d > 420.0f) return;
    float fade = d < 300 ? 1.0f : 1.0f - (d - 300) / 120.0f;
    const int N = 64;
    ImVec2 pts[N + 1]; bool ok[N + 1];
    ImVec2 top[N + 1]; bool okT[N + 1];
    for(int i = 0; i <= N; ++i)
    {
        float a = (float)i / N * 6.2831853f;
        Vec3 g = { c.x + cosf(a) * r, c.y + sinf(a) * r, c.z };
        ok[i] = BcwW2S(g, pts[i]);
        Vec3 t = { g.x, g.y, g.z + wallH };
        okT[i] = BcwW2S(t, top[i]);
    }
    float pulse = 0.75f + 0.25f * sinf(now * 0.004f);
    for(int i = 0; i < N; ++i)
    {
        if(ok[i] && ok[i + 1] && okT[i] && okT[i + 1])
        {   // wall: bright at the bottom, gone at the top
            ImU32 cb = BcwCol(rgba, 0.42f * fade * pulse), ct = BcwCol(rgba, 0.0f);
            dl->PrimReserve(6, 4);
            ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
            ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(pts[i], uv, cb); dl->PrimWriteVtx(pts[i + 1], uv, cb);
            dl->PrimWriteVtx(top[i + 1], uv, ct); dl->PrimWriteVtx(top[i], uv, ct);
            dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 1); dl->PrimWriteIdx(idx + 2);
            dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 2); dl->PrimWriteIdx(idx + 3);
        }
        if(ok[i] && ok[i + 1])
        {
            dl->AddLine(pts[i], pts[i + 1], BcwCol(rgba, 0.25f * fade), UI::S(14));   // glow
            dl->AddLine(pts[i], pts[i + 1], BcwCol(rgba, 0.55f * fade), UI::S(6));
            dl->AddLine(pts[i], pts[i + 1], BcwCol(0xFFFFFFFF, 0.65f * fade * pulse), UI::S(2));
        }
    }
    if(!particles || d > 160.0f) return;
    for(int i = 0; i < 48; ++i)
    {   // particles flow around the ring and rise
        float ph = (float)((i * 7919) % 1000) / 1000.0f;
        float a = (float)i / 48 * 6.2831853f + now * 0.00035f * (i % 2 ? 1 : -1);
        float rise = fmodf(now * 0.00045f + ph, 1.0f);
        float rr = r * (0.92f + 0.12f * sinf(ph * 40.0f + now * 0.002f));
        Vec3 p = { c.x + cosf(a) * rr, c.y + sinf(a) * rr, c.z + rise * (wallH + 1.2f) };
        ImVec2 s; if(!BcwW2S(p, s)) continue;
        float sz = UI::S(7) * (1.0f - rise * 0.6f) * (d < 25 ? 1.3f : 25.0f / d + 0.3f);
        float al = (1.0f - rise) * fade;
        dl->AddCircleFilled(s, sz * 1.9f, BcwCol(rgba, 0.22f * al));
        dl->AddCircleFilled(s, sz, BcwCol(rgba, 0.85f * al));
        dl->AddCircleFilled(s, sz * 0.45f, BcwCol(0xFFFFFFFF, 0.8f * al));
    }
}
static void BcwLabel3D(ImDrawList* dl, const Vec3& p, const char* text, uint32_t rgba, float baseSize, float refDist)
{
    float d = BcwCamDist(p);
    if(d > 500.0f) return;
    ImVec2 s; if(!BcwW2S(p, s)) return;
    float k = refDist / (d < 1 ? 1 : d); if(k > 1.0f) k = 1.0f; if(k < 0.38f) k = 0.38f;
    float fs = UI::S(baseSize) * k;
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, text);
    UI::OutlinedText(dl, fs, ImVec2(s.x - ts.x * 0.5f, s.y - ts.y * 0.5f), BcwCol(rgba, 1.0f), text);
}
// x-ray outline of a player model (drawn over everything = visible through walls)
static void BcwOutline(ImDrawList* dl, const Vec3& pos, uint32_t rgba, float now, const char* label)
{
    ImVec2 head, feet;
    if(!BcwW2S({ pos.x, pos.y, pos.z + 0.85f }, head) || !BcwW2S({ pos.x, pos.y, pos.z - 1.0f }, feet)) return;
    float h = feet.y - head.y; if(h < UI::S(14)) h = UI::S(14);
    float w = h * 0.36f, cx = (head.x + feet.x) * 0.5f;
    float hr = w * 0.36f;
    ImVec2 hc(head.x, head.y + hr * 0.4f);
    ImVec2 b0(cx - w * 0.5f, hc.y + hr * 1.1f), b1(cx + w * 0.5f, feet.y);
    float pulse = 0.7f + 0.3f * sinf(now * 0.008f);
    for(int g = 3; g >= 1; --g)
    {
        float th = UI::S(3.0f * g + 1.0f);
        ImU32 c = BcwCol(rgba, (g == 1 ? 0.95f : 0.22f) * pulse);
        dl->AddCircle(hc, hr, c, 20, th);
        dl->AddRect(b0, b1, c, w * 0.35f, 0, th);
    }
    if(label && *label)
    {
        float fs = UI::S(24);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, label);
        UI::OutlinedText(dl, fs, ImVec2(cx - ts.x * 0.5f, hc.y - hr - ts.y - UI::S(30)), BcwCol(rgba, 1), label);
    }
    ImVec2 uv(1, 1);
    if(void* tex = HUD::Texture("bcw_briefcase.png", &uv))
    {
        float is = UI::S(30);
        dl->AddImage((ImTextureID)tex, ImVec2(cx - is * 0.5f, hc.y - hr - is - UI::S(2)), ImVec2(cx + is * 0.5f, hc.y - hr - UI::S(2)), ImVec2(0, 0), uv);
    }
}
static void BcwBriefcaseMarker(ImDrawList* dl, const Vec3& p, uint32_t rgba, float now, const char* text, bool beam)
{
    ImVec2 g, t;
    if(beam && BcwW2S(p, g) && BcwW2S({ p.x, p.y, p.z + 30.0f }, t))
    {
        dl->AddLine(g, t, BcwCol(rgba, 0.25f), UI::S(16));
        dl->AddLine(g, t, BcwCol(rgba, 0.7f), UI::S(5));
        dl->AddLine(g, t, BcwCol(0xFFFFFFFF, 0.8f), UI::S(1.5f));
    }
    if(beam) BcwDrawZone(dl, { p.x, p.y, p.z - 0.3f }, 1.3f, rgba, now, false, 0.8f);
    ImVec2 s; if(!BcwW2S({ p.x, p.y, p.z + 1.4f }, s)) return;
    float bob = sinf(now * 0.005f) * UI::S(5);
    ImVec2 uv(1, 1); float is = UI::S(40);
    if(void* tex = HUD::Texture("bcw_briefcase.png", &uv))
    {
        dl->AddCircleFilled(ImVec2(s.x, s.y + bob), is * 0.75f, BcwCol(rgba, 0.35f));
        dl->AddImage((ImTextureID)tex, ImVec2(s.x - is * 0.5f, s.y - is * 0.5f + bob), ImVec2(s.x + is * 0.5f, s.y + is * 0.5f + bob), ImVec2(0, 0), uv);
    }
    else dl->AddRectFilled(ImVec2(s.x - is * 0.4f, s.y - is * 0.3f + bob), ImVec2(s.x + is * 0.4f, s.y + is * 0.3f + bob), BcwCol(rgba, 0.9f), UI::S(4));
    if(text && *text)
    {
        float fs = UI::S(22);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, text);
        UI::OutlinedText(dl, fs, ImVec2(s.x - ts.x * 0.5f, s.y + is * 0.55f + bob), BcwCol(0xFFFFFFFF, 1), text);
    }
}
// Minecraft-style world border: an animated yellow / orange striped wall near you
static void BcwDrawBarrier(ImDrawList* dl, float now)
{
    if(s_Bcw.barrierDist > 70.0f) return;
    int m = BcwCurMap();
    int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    float L = BcwBarrierLength(m), best = 1e9f, s0 = 0;
    for(float s = 0; s < L; s += 4.0f) { float x, y; BcwBarrierPoint(m, s, x, y); float d = (x - me.x) * (x - me.x) + (y - me.y) * (y - me.y); if(d < best) { best = d; s0 = s; } }
    const float span = 80.0f, step = 2.5f, z0 = me.z - 18.0f, z1 = me.z + 40.0f;
    const int bands = 12;
    float a0 = 1.0f - s_Bcw.barrierDist / 70.0f; if(a0 < 0) a0 = 0;
    for(float s = s0 - span; s < s0 + span; s += step)
    {
        float xa, ya, xb, yb; BcwBarrierPoint(m, s, xa, ya); BcwBarrierPoint(m, s + step, xb, yb);
        float dd = sqrtf((xa - me.x) * (xa - me.x) + (ya - me.y) * (ya - me.y));
        float fade = a0 * (1.0f - dd / (span + 10.0f)); if(fade <= 0.02f) continue;
        for(int b = 0; b < bands; ++b)
        {
            float za = z0 + (z1 - z0) * b / bands, zb = z0 + (z1 - z0) * (b + 1) / bands;
            ImVec2 p1, p2, p3, p4;
            if(!BcwW2S({ xa, ya, za }, p1) || !BcwW2S({ xb, yb, za }, p2) || !BcwW2S({ xb, yb, zb }, p3) || !BcwW2S({ xa, ya, zb }, p4)) continue;
            float stripe = fmodf((s + za) * 0.33f - now * 0.0025f, 2.0f); if(stripe < 0) stripe += 2.0f;
            float mix = 0.5f + 0.5f * sinf((s * 0.05f) + za * 0.08f - now * 0.0015f);
            int r = 255, gg = (int)(215 - 95 * mix), bb = (int)(40 - 20 * mix);   // yellow -> orange
            float al = (stripe < 1.0f ? 0.42f : 0.16f) * fade;
            dl->AddQuadFilled(p1, p2, p3, p4, IM_COL32(r, gg, bb, (int)(255 * al)));
        }
        ImVec2 g1, g2;
        if(BcwW2S({ xa, ya, me.z - 0.9f }, g1) && BcwW2S({ xb, yb, me.z - 0.9f }, g2)) dl->AddLine(g1, g2, IM_COL32(255, 170, 40, (int)(230 * fade)), UI::S(4));
    }
}

// 3.04.3: a very high team flag on top of each base (60 m pole, 18 x 10 m waving cloth) - you can
// see where the enemy base is from anywhere on the war map
static const float BCW_FLAG_POLE = 60.0f, BCW_FLAG_LEN = 18.0f, BCW_FLAG_H = 10.0f;
static void BcwDrawFlag(ImDrawList* dl, const Vec3& base, uint32_t rgba, const char* tag, float now)
{
    Vec3 top = { base.x, base.y, base.z + BCW_FLAG_POLE };
    ImVec2 g, t;
    bool hg = BcwW2S(base, g), ht = BcwW2S(top, t);
    float d = BcwCamDist(top);
    float th = UI::S(9.0f) * (60.0f / (d < 30 ? 30 : d)); if(th < UI::S(2.2f)) th = UI::S(2.2f);
    if(hg && ht)
    {   // pole: dark outline + light metal
        dl->AddLine(g, t, IM_COL32(20, 20, 20, 230), th + UI::S(2));
        dl->AddLine(g, t, IM_COL32(215, 215, 220, 255), th);
    }
    if(ht) dl->AddCircleFilled(t, th * 1.3f, IM_COL32(255, 210, 60, 255));   // gold ball on top
    // cloth: streams in the wind, waves along its length. Far away it grows, so the enemy flag
    // keeps the size of a flag ~220 m away on screen
    float big = d > 220.0f ? d / 220.0f : 1.0f; if(big > 4.0f) big = 4.0f;
    const float LEN = BCW_FLAG_LEN * big, FH = BCW_FLAG_H * big;
    const int N = 14;
    float wa = 0.6f + 0.25f * sinf(now * 0.00031f);      // slowly turning wind
    float wx = cosf(wa), wy = sinf(wa), px = -wy, py = wx;
    ImVec2 up[N + 1], dn[N + 1]; bool ok[N + 1];
    float phase = now * 0.0045f;
    for(int i = 0; i <= N; ++i)
    {
        float u = (float)i / N;
        float wave = sinf(phase - u * 6.5f) * 1.6f * u;   // fixed at the pole, free at the end
        float sag  = u * u * 1.2f;
        Vec3 a = { top.x + wx * LEN * u + px * wave * big, top.y + wy * LEN * u + py * wave * big, top.z - 0.6f - sag * big };
        Vec3 b = { a.x, a.y, a.z - FH };
        ok[i] = BcwW2S(a, up[i]) && BcwW2S(b, dn[i]);
    }
    uint8_t r = (rgba >> 24) & 255, gg = (rgba >> 16) & 255, bb = (rgba >> 8) & 255;
    for(int i = 0; i < N; ++i)
    {
        if(!ok[i] || !ok[i + 1]) continue;
        float u = (i + 0.5f) / N;
        float sh = 0.72f + 0.28f * cosf(phase - u * 6.5f);   // light / shadow folds
        ImU32 c = IM_COL32((int)(r * sh), (int)(gg * sh), (int)(bb * sh), 245);
        dl->AddQuadFilled(up[i], up[i + 1], dn[i + 1], dn[i], c);
    }
    for(int i = 0; i < N; ++i)
        if(ok[i] && ok[i + 1])
        {
            dl->AddLine(up[i], up[i + 1], IM_COL32(15, 15, 15, 200), UI::S(1.5f));
            dl->AddLine(dn[i], dn[i + 1], IM_COL32(15, 15, 15, 200), UI::S(1.5f));
        }
    if(ok[N]) dl->AddLine(up[N], dn[N], IM_COL32(15, 15, 15, 200), UI::S(1.5f));
    // team tag in the middle of the cloth
    int mi = N / 2;
    if(ok[mi])
    {
        ImVec2 c = ImVec2((up[mi].x + dn[mi].x) * 0.5f, (up[mi].y + dn[mi].y) * 0.5f);
        float hpx = fabsf(dn[mi].y - up[mi].y);
        float fs = hpx * 0.55f; if(fs < UI::S(14)) fs = UI::S(14); if(fs > UI::S(90)) fs = UI::S(90);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, tag);
        UI::OutlinedText(dl, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), tag);
    }
}
static void BcwDrawWorld(ImDrawList* dl, float now)
{
    const int m = BcwCurMap();
    const BcwMap& M = g_BcwMaps[m];
    const bool live = s_Bcw.st.state == BCW_LIVE;
    for(int b = 0; b < 2; ++b)
    {
        uint8_t t = (uint8_t)(b + 1);
        Vec3 c = { M.spawn[b][0], M.spawn[b][1], BcwGround(m, b) };
        BcwDrawZone(dl, c, BCW_SAFE_R, g_BcwTeamRGBA[t], now, true, 1.8f);
        char lbl[80]; snprintf(lbl, sizeof(lbl), "%s BASE", t == BCW_T_GUSTAV ? "GUSTAV TEAM" : "SCHELAPZ TEAM");
        BcwLabel3D(dl, { c.x, c.y, c.z + 9.0f }, lbl, g_BcwTeamRGBA[t], 64, 70.0f);
        BcwLabel3D(dl, { c.x, c.y, c.z + 6.6f }, M.base[b], 0xFFFFFFFF, 30, 60.0f);
        BcwDrawFlag(dl, c, g_BcwTeamRGBA[t], t == BCW_T_GUSTAV ? "GTV" : "SLZ", now);   // very high team flag
        for(int k = 0; k < 3; ++k)
        {
            const BcwSpawner& sp = M.spawners[b][k];
            float gz = Game::GroundZ(sp.pos[0], sp.pos[1], sp.pos[2] + 3.0f); if(gz < -50 || fabsf(gz - sp.pos[2]) > 12) gz = sp.pos[2] - 1.0f;
            Vec3 sc = { sp.pos[0], sp.pos[1], gz + 0.05f };
            BcwDrawZone(dl, sc, BCW_SPAWNER_R, g_BcwTeamRGBA[t], now, true, 1.0f);
            BcwLabel3D(dl, { sc.x, sc.y, sc.z + 3.4f }, g_BcwSpawnerTitle[sp.kind], g_BcwTeamRGBA[t], 34, 35.0f);
        }
        if(live)
        {
            const BcwCase& k = s_Bcw.st.cases[b];
            if(k.state == CASE_HOME) BcwBriefcaseMarker(dl, { c.x, c.y, c.z + 1.0f }, g_BcwTeamRGBA[t], now, "", false);
        }
    }
    if(!live) return;
    ImDrawList* fg = ImGui::GetForegroundDrawList();   // outlines + dropped briefcases are seen through walls
    for(int b = 0; b < 2; ++b)
    {
        const BcwCase& k = s_Bcw.st.cases[b];
        if(k.state == CASE_DROPPED)
        {
            char t[64]; snprintf(t, sizeof(t), "%s BRIEFCASE  %ds", b == 0 ? "GUSTAV" : "SCHELAPZ", (int)k.dropLeftS);
            float gz = Game::GroundZ(k.pos[0], k.pos[1], k.pos[2] + 2.0f); if(gz < -50) gz = k.pos[2] - 1.0f;
            BcwBriefcaseMarker(fg, { k.pos[0], k.pos[1], gz + 0.2f }, g_BcwTeamRGBA[b + 1], now, t, true);
        }
        else if(k.state == CASE_CARRIED && k.carrier < MP_MAX_PLAYERS)
        {
            uint8_t ct = BcwTeamOf(k.carrier);
            Vec3 p;
            if(k.carrier == myId) { int ped = Game::LocalPed(); if(ped <= 0) continue; Game::GetCharPos(ped, p); }
            else if(players[k.carrier].active && players[k.carrier].ped > 0) Game::GetCharPos(players[k.carrier].ped, p);
            else p = { k.pos[0], k.pos[1], k.pos[2] };
            char lbl[64]; snprintf(lbl, sizeof(lbl), "%s", BcwName(k.carrier));
            if(k.carrier != myId) BcwOutline(fg, p, g_BcwTeamRGBA[ct], now, lbl);
        }
    }
    if(s_Bcw.view == 2) BcwDrawBarrier(dl, now);
}

// ---- HUD pieces -------------------------------------------------------------------------------
static void BcwCenterText(ImDrawList* dl, float fs, float y, ImU32 col, const char* t)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    UI::OutlinedText(dl, fs, ImVec2(ds.x * 0.5f - ts.x * 0.5f, y), col, t);
}
static void BcwCaseIcon(ImDrawList* dl, ImVec2 c, float sz, const BcwCase& k, uint32_t rgba)
{
    ImVec2 uv(1, 1);
    float a = k.state == CASE_HOME ? 1.0f : 0.45f + 0.35f * sinf(MP_NowMs() * 0.01f);
    dl->AddCircleFilled(c, sz * 0.62f, BcwCol(rgba, 0.85f));
    dl->AddCircle(c, sz * 0.62f, IM_COL32(0, 0, 0, 220), 24, UI::S(3));
    if(void* tex = HUD::Texture("bcw_briefcase.png", &uv))
        dl->AddImage((ImTextureID)tex, ImVec2(c.x - sz * 0.42f, c.y - sz * 0.42f), ImVec2(c.x + sz * 0.42f, c.y + sz * 0.42f), ImVec2(0, 0), uv, IM_COL32(255, 255, 255, (int)(255 * a)));
    const char* s = k.state == CASE_HOME ? "HOME" : (k.state == CASE_CARRIED ? "STOLEN" : "DROPPED");
    float fs = UI::S(20); ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, s);
    UI::OutlinedText(dl, fs, ImVec2(c.x - ts.x * 0.5f, c.y + sz * 0.66f), IM_COL32_WHITE, s);
}
static void BcwDrawMatchHud(ImDrawList* dl, float now)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktBcwState& st = s_Bcw.st;
    // top bar: Gustav [case] mm:ss [case] Schelapz
    float cy = UI::S(70), w = UI::S(560), h = UI::S(64);
    ImVec2 a(ds.x * 0.5f - w * 0.5f, cy - h * 0.5f), b(ds.x * 0.5f + w * 0.5f, cy + h * 0.5f);
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150), UI::S(14));
    dl->AddRectFilled(a, ImVec2(a.x + w * 0.38f, b.y), BcwCol(g_BcwTeamRGBA[1], 0.55f), UI::S(14), ImDrawFlags_RoundCornersLeft);
    dl->AddRectFilled(ImVec2(b.x - w * 0.38f, a.y), b, BcwCol(g_BcwTeamRGBA[2], 0.55f), UI::S(14), ImDrawFlags_RoundCornersRight);
    char t[48]; float fs = UI::S(26);
    snprintf(t, sizeof(t), "GUSTAV %d", st.count[0]);
    UI::OutlinedText(dl, fs, ImVec2(a.x + UI::S(16), cy - fs * 0.5f), IM_COL32_WHITE, t);
    snprintf(t, sizeof(t), "%d SCHELAPZ", st.count[1]);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    UI::OutlinedText(dl, fs, ImVec2(b.x - UI::S(16) - ts.x, cy - fs * 0.5f), IM_COL32_WHITE, t);
    snprintf(t, sizeof(t), "%02d:%02d", st.timerS / 60, st.timerS % 60);
    ts = UI::Font()->CalcTextSizeA(UI::S(28), FLT_MAX, 0, t);
    UI::OutlinedText(dl, UI::S(28), ImVec2(ds.x * 0.5f - ts.x * 0.5f, cy - UI::S(14)), IM_COL32_WHITE, t);
    BcwCaseIcon(dl, ImVec2(a.x + w * 0.38f - UI::S(4), cy + h * 0.5f + UI::S(26)), UI::S(40), st.cases[0], g_BcwTeamRGBA[1]);
    BcwCaseIcon(dl, ImVec2(b.x - w * 0.38f + UI::S(4), cy + h * 0.5f + UI::S(26)), UI::S(40), st.cases[1], g_BcwTeamRGBA[2]);

    const uint8_t mine = BcwMyTeam(); const int mi = BcwTeamIndex(mine);
    if(mi < 0) return;
    // steal bar: white fill, thick black outline, a bit above the bottom
    const BcwCase& enemy = st.cases[1 - mi];
    if(enemy.state == CASE_HOME && enemy.stealer == myId && s_Bcw.view == 2)
    {
        float bw = ds.x * 0.36f, bh = UI::S(30), y = ds.y * 0.80f, x = ds.x * 0.5f - bw * 0.5f;
        float k = enemy.progress / 255.0f;
        float ol = UI::S(5);   // thick black outline
        dl->AddRectFilled(ImVec2(x - ol, y - ol), ImVec2(x + bw + ol, y + bh + ol), IM_COL32(0, 0, 0, 255), UI::S(8));
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), IM_COL32(40, 40, 40, 220), UI::S(5));
        if(k > 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw * k, y + bh), IM_COL32(255, 255, 255, 255), UI::S(5));
        float shine = fmodf(now * 0.0012f, 1.0f) * bw * k;   // moving shine on the fill
        if(k > 0.05f) dl->AddRectFilled(ImVec2(x + shine - UI::S(18) < x ? x : x + shine - UI::S(18), y), ImVec2(x + shine, y + bh), IM_COL32(210, 210, 210, 200));
        char p[64]; snprintf(p, sizeof(p), "STEALING THE %s BRIEFCASE  %d%%", mi == 0 ? "SCHELAPZ" : "GUSTAV", (int)(k * 100));
        BcwCenterText(dl, UI::S(28), y - UI::S(44), IM_COL32_WHITE, p);
    }
    const BcwCase& own = st.cases[mi];
    if(own.state == CASE_HOME && own.stealer != MP_INVALID_ID && fmodf(now, 700.0f) < 450.0f)
        BcwCenterText(dl, UI::S(30), UI::S(190), BcwCol(g_BcwTeamRGBA[mine], 1), "YOUR BRIEFCASE IS BEING STOLEN!");
    if(enemy.state == CASE_CARRIED && enemy.carrier == myId)
        BcwCenterText(dl, UI::S(28), ds.y * 0.72f, IM_COL32_WHITE, own.state == CASE_HOME ? "YOU HAVE THE BRIEFCASE - BRING IT TO YOUR BASE!" : "YOUR BRIEFCASE IS STOLEN TOO - KILL THEIR CARRIER FIRST!");
    if(s_Bcw.view == 2 && s_Bcw.barrierDist < 25.0f && fmodf(now, 900.0f) < 600.0f)
        BcwCenterText(dl, UI::S(30), ds.y * 0.30f, IM_COL32(255, 170, 40, 255), "WORLD BARRIER");
    if(s_Bcw.protectUntil && now < s_Bcw.protectUntil)
        BcwCenterText(dl, UI::S(22), ds.y * 0.5f + UI::S(70), IM_COL32(200, 230, 255, 255), "SPAWN PROTECTION");
}

static void BcwBeginOverlay(const char* id, ImVec2 pos, ImVec2 size, float bgAlpha)
{
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, bgAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(18), UI::S(14)));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
static void BcwEndOverlay() { ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(); }
static bool BcwTeamButton(const char* label, uint32_t rgba, ImVec2 size, bool selected, bool disabled)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(rgba));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x, c.y, c.z, disabled ? 0.35f : (selected ? 1.0f : 0.82f)));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x, c.y, c.z, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.x * 0.8f, c.y * 0.8f, c.z * 0.8f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, disabled ? 0.5f : 1));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(16));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, selected ? UI::S(4) : 0.0f);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1));
    if(disabled) ImGui::BeginDisabled();
    bool r = ImGui::Button(label, size);
    if(disabled) ImGui::EndDisabled();
    ImGui::PopStyleColor(5); ImGui::PopStyleVar(2);
    return r;
}

// team picker (replaces < Spawn >) + queue
// 3.04.4: dead = back on the connection screen; a team-coloured label button counts the respawn
// down (10 s + 20 ticks, in 20-tick steps), then you spawn in your safe zone
static void BcwDrawRespawnScreen(float now)
{
    if(s_Bcw.view != 3 || !s_Bcw.held) return;
    const uint8_t mine = BcwMyTeam();
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    BcwCenterText(bg, UI::S(56), ds.y * 0.10f, IM_COL32(255, 255, 255, 255), "BRIEF CASE WAR");
    char t[96];
    snprintf(t, sizeof(t), "%s  -  %s", g_BcwMaps[BcwCurMap()].name, g_BcwMaps[BcwCurMap()].area);
    BcwCenterText(bg, UI::S(28), ds.y * 0.10f + UI::S(66), IM_COL32(230, 230, 230, 255), t);
    BcwCenterText(bg, UI::S(34), ds.y * 0.62f, IM_COL32(255, 80, 80, 255), "YOU DIED");
    if(mine) { snprintf(t, sizeof(t), "%s - you will spawn in your safe zone", g_BcwTeamName[mine]); BcwCenterText(bg, UI::S(24), ds.y * 0.62f + UI::S(44), BcwCol(g_BcwTeamRGBA[mine], 1), t); }
    uint64_t e = MP_NowMs() - s_Bcw.heldAt; if(e > BCW_RESPAWN_MS) e = BCW_RESPAWN_MS;
    int ticks = (int)((BCW_RESPAWN_MS - e) / 50);
    snprintf(t, sizeof(t), "Respawn in %d.%02d##rsp", ticks / 20, (ticks % 20) * 5);
    const float bw = UI::S(440), bh = UI::S(84);
    BcwBeginOverlay("##BcwRespawn", ImVec2(ds.x * 0.5f - bw * 0.5f - UI::S(18), ds.y * 0.80f - UI::S(30)), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    ImGui::SetWindowFontScale(0.95f);
    BcwTeamButton(t, mine ? g_BcwTeamRGBA[mine] : 0x555555FF, ImVec2(bw, bh), true, false);   // a label: tapping does nothing
    ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
    // cooldown bar along the bottom of the button
    float k = (float)e / BCW_RESPAWN_MS;
    ImVec2 p0(ds.x * 0.5f - bw * 0.5f, ds.y * 0.80f - UI::S(16) + bh + UI::S(10));
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p0, ImVec2(p0.x + bw, p0.y + UI::S(10)), IM_COL32(0, 0, 0, 200), UI::S(5));
    fg->AddRectFilled(p0, ImVec2(p0.x + bw * k, p0.y + UI::S(10)), IM_COL32(255, 255, 255, 255), UI::S(5));
}
static void BcwDrawTeamPicker(float now)
{
    const PktBcwState& st = s_Bcw.st;
    const uint8_t mine = BcwMyTeam();
    if(st.state == BCW_RESULTS || st.state == BCW_VOTE || st.state == BCW_ROLL || st.state == BCW_COOLDOWN) return;
    if(st.state == BCW_LIVE && mine) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    BcwCenterText(bg, UI::S(56), ds.y * 0.10f, IM_COL32(255, 255, 255, 255), "BRIEF CASE WAR");
    char t[96];
    snprintf(t, sizeof(t), "%s  -  %s", g_BcwMaps[BcwCurMap()].name, g_BcwMaps[BcwCurMap()].area);
    BcwCenterText(bg, UI::S(28), ds.y * 0.10f + UI::S(66), IM_COL32(230, 230, 230, 255), t);
    int total = st.count[0] + st.count[1];
    if(st.state == BCW_QUEUE)
    {
        snprintf(t, sizeof(t), mine ? "WAITING FOR PLAYERS  %d/%d" : "PICK YOUR TEAM  -  %d/%d IN THE QUEUE", total, st.minPlayers);
        BcwCenterText(bg, UI::S(34), ds.y * 0.62f, IM_COL32_WHITE, t);
        if(mine)
        {
            snprintf(t, sizeof(t), "You are in %s - the war starts at %d/%d players", g_BcwTeamName[mine], st.minPlayers, st.minPlayers);
            BcwCenterText(bg, UI::S(24), ds.y * 0.62f + UI::S(44), BcwCol(g_BcwTeamRGBA[mine], 1), t);
            int dots = (int)(now / 400) % 4; char d[8] = "   "; for(int i = 0; i < dots; ++i) d[i] = '.';
            BcwCenterText(bg, UI::S(34), ds.y * 0.62f + UI::S(76), IM_COL32_WHITE, d);
        }
    }
    else BcwCenterText(bg, UI::S(30), ds.y * 0.62f, IM_COL32_WHITE, "A WAR IS RUNNING - JOIN A TEAM WITH A FREE SLOT");
    const float bw = UI::S(440), bh = UI::S(84), gap = UI::S(36);   // wide enough for "Schelapz Team  10/10"
    float total_w = bw * 2 + gap;
    BcwBeginOverlay("##BcwTeams", ImVec2(ds.x * 0.5f - total_w * 0.5f - UI::S(18), ds.y * 0.80f - UI::S(30)), ImVec2(total_w + UI::S(36), bh + UI::S(28)), 0.0f);
    ImGui::SetWindowFontScale(0.95f);
    snprintf(t, sizeof(t), "Gustav Team  %d/%d##g", st.count[0], BCW_TEAM_SIZE);
    if(BcwTeamButton(t, g_BcwTeamRGBA[1], ImVec2(bw, bh), mine == BCW_T_GUSTAV, st.count[0] >= BCW_TEAM_SIZE && mine != BCW_T_GUSTAV)) BcwSend(BCWC_TEAM, BCW_T_GUSTAV);
    ImGui::SameLine(0, gap);
    snprintf(t, sizeof(t), "Schelapz Team  %d/%d##s", st.count[1], BCW_TEAM_SIZE);
    if(BcwTeamButton(t, g_BcwTeamRGBA[2], ImVec2(bw, bh), mine == BCW_T_SCHELAPZ, st.count[1] >= BCW_TEAM_SIZE && mine != BCW_T_SCHELAPZ)) BcwSend(BCWC_TEAM, BCW_T_SCHELAPZ);
    ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
}

// slot-machine leader pick: names roll "user > user" in each team and slow down on the leader
static void BcwDrawRoll(float now)
{
    if(!s_Bcw.haveRoll || (s_Bcw.st.state != BCW_ROLL && now - s_Bcw.rollAt > BCW_ROLL_MS + 1500)) return;
    if(s_Bcw.st.state == BCW_LIVE && s_Bcw.view == 2 && now - s_Bcw.rollAt > BCW_ROLL_MS + 1500) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, ds.y * 0.28f), ImVec2(ds.x, ds.y * 0.68f), IM_COL32(0, 0, 0, 170));
    BcwCenterText(dl, UI::S(44), ds.y * 0.30f, IM_COL32_WHITE, "PICKING TEAM LEADERS");
    const float T = BCW_ROLL_MS - 1000.0f;
    float k = (float)(now - s_Bcw.rollAt) / T; if(k > 1) k = 1;
    float F = 1.0f - (1.0f - k) * (1.0f - k) * (1.0f - k);
    for(int t = 0; t < 2; ++t)
    {
        int n = s_Bcw.roll.n[t]; if(n <= 0) continue;
        int li = 0; for(int i = 0; i < n; ++i) if(s_Bcw.roll.ids[t][i] == s_Bcw.roll.leader[t]) li = i;
        int steps = n > 1 ? n * 4 + li : li;
        int step = (int)(F * steps + 0.0001f); if(k >= 1) step = steps;
        int cur = s_Bcw.roll.ids[t][step % n], prev = s_Bcw.roll.ids[t][(step - 1 + n * 8) % n];
        float cx = ds.x * (t == 0 ? 0.28f : 0.72f), y = ds.y * 0.40f;
        uint32_t col = g_BcwTeamRGBA[t + 1];
        const char* title = t == 0 ? "GUSTAV TEAM LEADER" : "SCHELAPZ TEAM LEADER";
        ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(30), FLT_MAX, 0, title);
        UI::OutlinedText(dl, UI::S(30), ImVec2(cx - ts.x * 0.5f, y), BcwCol(col, 1), title);
        char line[96]; snprintf(line, sizeof(line), "%s  >  %s", n > 1 ? BcwName(prev) : "", BcwName(cur));
        float fs = UI::S(k >= 1 ? 46 : 40);
        ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, line);
        ImVec2 bp(cx - ts.x * 0.5f - UI::S(20), y + UI::S(54));
        float glow = k >= 1 ? 0.6f + 0.4f * sinf(now * 0.012f) : 0.35f;
        dl->AddRectFilled(bp, ImVec2(cx + ts.x * 0.5f + UI::S(20), bp.y + fs + UI::S(20)), BcwCol(col, glow), UI::S(12));
        dl->AddRect(bp, ImVec2(cx + ts.x * 0.5f + UI::S(20), bp.y + fs + UI::S(20)), IM_COL32(255, 255, 255, 200), UI::S(12), 0, UI::S(3));
        UI::OutlinedText(dl, fs, ImVec2(cx - ts.x * 0.5f, bp.y + UI::S(10)), IM_COL32_WHITE, line);
        if(k >= 1) { ts = UI::Font()->CalcTextSizeA(UI::S(26), FLT_MAX, 0, "LEADER!"); UI::OutlinedText(dl, UI::S(26), ImVec2(cx - ts.x * 0.5f, bp.y + fs + UI::S(30)), IM_COL32(255, 230, 90, 255), "LEADER!"); }
    }
}

// cooldown after a victory: the winner, "resetting the war zone", a bar running out
static void BcwDrawCooldown(float now)
{
    if(s_Bcw.st.state != BCW_COOLDOWN) { s_Bcw.coolAt = 0; return; }
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(ImVec2(0, ds.y * 0.30f), ImVec2(ds.x, ds.y * 0.62f), IM_COL32(0, 0, 0, 170));
    uint8_t w = 0;
    for(int t = 1; t <= 2; ++t) if(!strncasecmp(s_Bcw.banner, t == 1 ? "GUSTAV TEAM VICTORY" : "SCHELAPZ TEAM VICTORY", 12)) w = (uint8_t)t;
    BcwCenterText(dl, UI::S(60), ds.y * 0.33f, BcwCol(w ? g_BcwTeamRGBA[w] : 0xFFFFFFFF, 1), w ? (w == 1 ? "GUSTAV TEAM VICTORY" : "SCHELAPZ TEAM VICTORY") : "VICTORY");
    if(!s_Bcw.coolAt) s_Bcw.coolAt = MP_NowMs() - (uint64_t)(BCW_COOLDOWN_MS / 1000 - s_Bcw.st.timerS) * 1000;
    uint64_t ce = MP_NowMs() - s_Bcw.coolAt; if(ce > BCW_COOLDOWN_MS) ce = BCW_COOLDOWN_MS;
    int ticks = (int)((BCW_COOLDOWN_MS - ce) / 50);   // 20 ticks a second
    char t[80]; snprintf(t, sizeof(t), "Cooldown - resetting the war zone  %d.%02d", ticks / 20, (ticks % 20) * 5);
    BcwCenterText(dl, UI::S(30), ds.y * 0.33f + UI::S(84), IM_COL32_WHITE, t);
    BcwCenterText(dl, UI::S(22), ds.y * 0.33f + UI::S(126), IM_COL32(210, 210, 210, 255), "briefcases, objects and war vehicles cleared  -  scoreboard and map vote next");
    float bw = ds.x * 0.40f, bh = UI::S(16), x = ds.x * 0.5f - bw * 0.5f, y = ds.y * 0.33f + UI::S(170);
    float k = (float)(BCW_COOLDOWN_MS - ce) / BCW_COOLDOWN_MS;
    dl->AddRectFilled(ImVec2(x - UI::S(4), y - UI::S(4)), ImVec2(x + bw + UI::S(4), y + bh + UI::S(4)), IM_COL32(0, 0, 0, 255), UI::S(6));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw * k, y + bh), IM_COL32(255, 255, 255, 255), UI::S(4));
    (void)now;
}

// scoreboard after a victory: top 3 most kills, Gustav Team [..] || Schelapz Team [..], all players
static void BcwDrawScoreboard(float now)
{
    if(s_Bcw.scoreClosed || !(s_Bcw.haveScore[0] || s_Bcw.haveScore[1])) return;
    if(s_Bcw.st.state != BCW_RESULTS && s_Bcw.st.state != BCW_VOTE) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 sz(ds.x * 0.90f, ds.y * 0.88f), pos(ds.x * 0.05f, ds.y * 0.06f);
    BcwBeginOverlay("##BcwScore", pos, sz, 0.82f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const PktBcwScore* S0 = s_Bcw.haveScore[0] ? &s_Bcw.score[0] : nullptr;
    const PktBcwScore* S1 = s_Bcw.haveScore[1] ? &s_Bcw.score[1] : nullptr;
    int winner = S0 ? S0->winner : (S1 ? S1->winner : 0);
    int dur = S0 ? S0->durationS : (S1 ? S1->durationS : 0);
    int mapi = S0 ? S0->map : (S1 ? S1->map : 0); if(mapi >= BCW_MAP_COUNT) mapi = 0;
    float x0 = pos.x + UI::S(24), W = sz.x - UI::S(48), y = pos.y + UI::S(18);
    char t[128];
    snprintf(t, sizeof(t), "BRIEF CASE WAR  -  %s", winner ? (winner == BCW_T_GUSTAV ? "GUSTAV TEAM VICTORY" : "SCHELAPZ TEAM VICTORY") : "DRAW");
    { ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(44), FLT_MAX, 0, t); UI::OutlinedText(dl, UI::S(44), ImVec2(pos.x + sz.x * 0.5f - ts.x * 0.5f, y), BcwCol(winner ? g_BcwTeamRGBA[winner] : 0xFFFFFFFF, 1), t); }
    y += UI::S(56);
    snprintf(t, sizeof(t), "%s  -  %d:%02d", g_BcwMaps[mapi].name, dur / 60, dur % 60);
    { ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(24), FLT_MAX, 0, t); dl->AddText(UI::Font(), UI::S(24), ImVec2(pos.x + sz.x * 0.5f - ts.x * 0.5f, y), IM_COL32(210, 210, 210, 255), t); }
    y += UI::S(40);
    // podium: top 3 kills over both teams
    const BcwScoreRow* all[BCW_TEAM_SIZE * 2]; int na = 0;
    for(int p = 0; p < 2; ++p) { const PktBcwScore* S = p ? S1 : S0; if(S) for(int i = 0; i < S->n; ++i) all[na++] = &S->rows[i]; }
    std::sort(all, all + na, [](const BcwScoreRow* a, const BcwScoreRow* b) { return a->kills != b->kills ? a->kills > b->kills : a->score > b->score; });
    {
        const float pw = UI::S(250), ph[3] = { UI::S(120), UI::S(96), UI::S(80) };
        const int order[3] = { 1, 0, 2 };   // 2nd | 1st | 3rd
        float cx = pos.x + sz.x * 0.5f, base = y + UI::S(124);
        for(int s = 0; s < 3; ++s)
        {
            int rk = order[s]; if(rk >= na) continue;
            const BcwScoreRow* r = all[rk];
            float px = cx + (s - 1) * (pw + UI::S(18)) - pw * 0.5f;
            ImVec2 a(px, base - ph[rk]), b(px + pw, base);
            dl->AddRectFilled(a, b, BcwCol(g_BcwTeamRGBA[r->team <= 2 ? r->team : 0], 0.55f), UI::S(10));
            dl->AddRect(a, b, rk == 0 ? IM_COL32(255, 215, 70, 255) : (rk == 1 ? IM_COL32(210, 210, 220, 255) : IM_COL32(205, 130, 70, 255)), UI::S(10), 0, UI::S(4));
            snprintf(t, sizeof(t), "%d", rk + 1);
            dl->AddText(UI::Font(), UI::S(34), ImVec2(a.x + UI::S(12), a.y + UI::S(6)), IM_COL32_WHITE, t);
            dl->PushClipRect(ImVec2(a.x + UI::S(44), a.y), b, true);
            dl->AddText(UI::Font(), UI::S(26), ImVec2(a.x + UI::S(48), a.y + UI::S(10)), IM_COL32_WHITE, r->name);
            dl->PopClipRect();
            snprintf(t, sizeof(t), "%d kills  -  %d score", r->kills, r->score);
            dl->AddText(UI::Font(), UI::S(20), ImVec2(a.x + UI::S(48), a.y + UI::S(44)), IM_COL32(235, 235, 235, 255), t);
        }
        y = base + UI::S(18);
    }
    // split header
    {
        char l[64], r[64];
        snprintf(l, sizeof(l), "Gustav Team [%s]", winner == BCW_T_GUSTAV ? "Victory" : "Defeat");
        snprintf(r, sizeof(r), "Schelapz Team [%s]", winner == BCW_T_SCHELAPZ ? "Victory" : "Defeat");
        float fs = UI::S(32);
        ImVec2 tl = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, l), tm = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, "  ||  ");
        float cx = pos.x + sz.x * 0.5f;
        UI::OutlinedText(dl, fs, ImVec2(cx - tm.x * 0.5f - tl.x, y), BcwCol(g_BcwTeamRGBA[1], 1), l);
        UI::OutlinedText(dl, fs, ImVec2(cx - tm.x * 0.5f, y), IM_COL32_WHITE, "  ||  ");
        UI::OutlinedText(dl, fs, ImVec2(cx + tm.x * 0.5f, y), BcwCol(g_BcwTeamRGBA[2], 1), r);
        y += UI::S(48);
    }
    // two tables
    static const char* hdr[] = { "Player", "K", "D", "K/D", "Steal", "Cap", "Ret", "Dmg", "Score" };
    const float colx[] = { 0.0f, 0.40f, 0.47f, 0.54f, 0.63f, 0.72f, 0.79f, 0.86f, 0.93f };
    float tw = (W - UI::S(30)) * 0.5f, rh = UI::S(34);
    for(int p = 0; p < 2; ++p)
    {
        const PktBcwScore* S = p ? S1 : S0;
        float tx = x0 + p * (tw + UI::S(30)), ty = y;
        dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + rh), BcwCol(g_BcwTeamRGBA[p + 1], 0.6f), UI::S(6));
        for(int c = 0; c < 9; ++c) dl->AddText(UI::Font(), UI::S(20), ImVec2(tx + UI::S(8) + colx[c] * tw, ty + UI::S(6)), IM_COL32_WHITE, hdr[c]);
        ty += rh + UI::S(4);
        if(!S) continue;
        for(int i = 0; i < S->n; ++i)
        {
            const BcwScoreRow& r = S->rows[i];
            if(i % 2) dl->AddRectFilled(ImVec2(tx, ty), ImVec2(tx + tw, ty + rh), IM_COL32(255, 255, 255, 14), UI::S(4));
            if(r.id == myId) dl->AddRect(ImVec2(tx, ty), ImVec2(tx + tw, ty + rh), IM_COL32(255, 255, 255, 120), UI::S(4));
            char nm[48]; snprintf(nm, sizeof(nm), "%s%s", r.name, r.leader ? " [L]" : "");
            dl->PushClipRect(ImVec2(tx, ty), ImVec2(tx + colx[1] * tw, ty + rh), true);
            dl->AddText(UI::Font(), UI::S(21), ImVec2(tx + UI::S(8), ty + UI::S(6)), IM_COL32_WHITE, nm);
            dl->PopClipRect();
            char v[9][16];
            snprintf(v[1], 16, "%d", r.kills); snprintf(v[2], 16, "%d", r.deaths);
            snprintf(v[3], 16, "%.2f", r.deaths ? (float)r.kills / r.deaths : (float)r.kills);
            snprintf(v[4], 16, "%d", r.steals); snprintf(v[5], 16, "%d", r.caps); snprintf(v[6], 16, "%d", r.returns);
            snprintf(v[7], 16, "%d", r.damage); snprintf(v[8], 16, "%d", r.score);
            for(int c = 1; c < 9; ++c) dl->AddText(UI::Font(), UI::S(21), ImVec2(tx + UI::S(8) + colx[c] * tw, ty + UI::S(6)), IM_COL32(235, 235, 235, 255), v[c]);
            ty += rh;
        }
    }
    // Close
    ImGui::SetCursorScreenPos(ImVec2(pos.x + sz.x - UI::S(230), pos.y + sz.y - UI::S(86)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(14));
    ImGui::SetWindowFontScale(1.25f);
    if(ImGui::Button("Close##bcwsc", ImVec2(UI::S(200), UI::S(68)))) s_Bcw.scoreClosed = true;
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar();
    if(s_Bcw.st.state == BCW_RESULTS)
    {
        snprintf(t, sizeof(t), "map vote in %d s", s_Bcw.st.timerS);
        dl->AddText(UI::Font(), UI::S(22), ImVec2(pos.x + UI::S(24), pos.y + sz.y - UI::S(60)), IM_COL32(200, 200, 200, 255), t);
    }
    BcwEndOverlay();
}

static void BcwDrawPadlock(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
{
    dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y - s * 0.1f), ImVec2(c.x + s * 0.5f, c.y + s * 0.6f), col, s * 0.12f);
    dl->PathArcTo(ImVec2(c.x, c.y - s * 0.1f), s * 0.32f, 3.14159f, 6.28318f, 16);
    dl->PathStroke(col, 0, s * 0.13f);
    dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.22f), s * 0.08f, IM_COL32(0, 0, 0, 220));
}
// map vote: 6 maps, 2 open, 4 locked
static void BcwDrawVote(float now)
{
    if(s_Bcw.st.state != BCW_VOTE && !(s_Bcw.st.state == BCW_RESULTS && s_Bcw.scoreClosed)) return;
    if(s_Bcw.st.state == BCW_VOTE && !s_Bcw.scoreClosed && (s_Bcw.haveScore[0] || s_Bcw.haveScore[1]) && now - s_Bcw.scoreAt < BCW_RESULTS_MS) return;   // scoreboard still open: Close first
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImVec2 sz(ds.x * 0.86f, ds.y * 0.80f), pos(ds.x * 0.07f, ds.y * 0.10f);
    BcwBeginOverlay("##BcwVote", pos, sz, 0.80f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char t[96];
    snprintf(t, sizeof(t), s_Bcw.st.state == BCW_VOTE ? "VOTE THE NEXT WAR MAP  -  %d s" : "VOTE THE NEXT WAR MAP  -  opens in %d s", s_Bcw.st.timerS);
    { ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(40), FLT_MAX, 0, t); UI::OutlinedText(dl, UI::S(40), ImVec2(pos.x + sz.x * 0.5f - ts.x * 0.5f, pos.y + UI::S(18)), IM_COL32_WHITE, t); }
    const float gx = UI::S(22), top = pos.y + UI::S(84);
    float cw = (sz.x - UI::S(36) - gx * 2) / 3.0f, ch = (sz.y - UI::S(110) - gx) / 2.0f;
    static int s_nMyVote = -1;
    if(s_Bcw.st.state == BCW_QUEUE) s_nMyVote = -1;
    for(int i = 0; i < BCW_MAP_COUNT; ++i)
    {
        const BcwMap& M = g_BcwMaps[i];
        ImVec2 a(pos.x + UI::S(18) + (i % 3) * (cw + gx), top + (i / 3) * (ch + gx)), b(a.x + cw, a.y + ch);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID(i);
        bool tap = ImGui::InvisibleButton("##map", ImVec2(cw, ch));
        ImGui::PopID();
        bool open = M.open, sel = s_nMyVote == i;
        dl->AddRectFilled(a, b, open ? IM_COL32(40, 46, 58, 240) : IM_COL32(25, 25, 25, 240), UI::S(14));
        dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + ch * 0.55f), b, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 160), IM_COL32(0, 0, 0, 160));
        dl->AddRect(a, b, sel ? IM_COL32(255, 220, 80, 255) : (open ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 30)), UI::S(14), 0, UI::S(sel ? 5 : 2));
        dl->AddText(UI::Font(), UI::S(30), ImVec2(a.x + UI::S(16), a.y + UI::S(14)), open ? IM_COL32_WHITE : IM_COL32(140, 140, 140, 255), M.name);
        dl->AddText(UI::Font(), UI::S(20), ImVec2(a.x + UI::S(16), a.y + UI::S(52)), IM_COL32(180, 180, 180, 255), M.area);
        if(open)
        {
            dl->AddText(UI::Font(), UI::S(18), ImVec2(a.x + UI::S(16), a.y + UI::S(82)), BcwCol(g_BcwTeamRGBA[1], 1), M.base[0]);
            dl->AddText(UI::Font(), UI::S(18), ImVec2(a.x + UI::S(16), a.y + UI::S(106)), BcwCol(g_BcwTeamRGBA[2], 1), M.base[1]);
            snprintf(t, sizeof(t), "%d vote%s", s_Bcw.st.votes[i], s_Bcw.st.votes[i] == 1 ? "" : "s");
            dl->AddText(UI::Font(), UI::S(28), ImVec2(a.x + UI::S(16), b.y - UI::S(46)), IM_COL32(255, 230, 120, 255), t);
            if(i == BcwCurMap()) dl->AddText(UI::Font(), UI::S(18), ImVec2(b.x - UI::S(130), b.y - UI::S(36)), IM_COL32(200, 200, 200, 255), "last map");
        }
        else
        {
            BcwDrawPadlock(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f + UI::S(4)), UI::S(56), IM_COL32(200, 200, 200, 220));
            dl->AddText(UI::Font(), UI::S(24), ImVec2(a.x + UI::S(16), b.y - UI::S(42)), IM_COL32(170, 170, 170, 255), "LOCKED");
        }
        if(tap && open) { s_nMyVote = i; BcwSend(BCWC_VOTE, (uint8_t)i); }
    }
    BcwEndOverlay();
}

static void BcwDrawSpawnerMenu()
{
    if(s_Bcw.view != 2 || s_Bcw.spawner < 0) return;
    int ti = BcwTeamIndex(BcwMyTeam()); if(ti < 0) return;
    int kind = g_BcwMaps[BcwCurMap()].spawners[ti][s_Bcw.spawner].kind;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = UI::S(380);
    int n = 0; for(int i = 0; i < 3; ++i) if(g_BcwSpawnerModels[kind][i]) ++n;
    BcwBeginOverlay("##BcwVeh", ImVec2(ds.x - w - UI::S(40), ds.y * 0.28f), ImVec2(w, UI::S(90) + n * UI::S(84)), 0.72f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    UI::OutlinedText(dl, UI::S(28), p, BcwCol(g_BcwTeamRGBA[ti + 1], 1), g_BcwSpawnerTitle[kind]);
    ImGui::Dummy(ImVec2(w - UI::S(36), UI::S(44)));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(12));
    ImGui::SetWindowFontScale(1.2f);
    for(int i = 0; i < 3; ++i)
    {
        int model = g_BcwSpawnerModels[kind][i]; if(!model) continue;
        const char* nm = model == 470 ? "Patriot" : model == 433 ? "Barracks" : model == 432 ? "Rhino (tank)" : model == 425 ? "Hunter (fighter helicopter)" : "Vehicle";
        char l[64]; snprintf(l, sizeof(l), "%s##v%d", nm, model);
        if(ImGui::Button(l, ImVec2(w - UI::S(36), UI::S(72)))) BcwSpawnVehicle(model);
    }
    ImGui::SetWindowFontScale(1.0f);
    ImGui::PopStyleVar();
    BcwEndOverlay();
}

static void RenderBcw()
{
    if(!s_Bcw.engaged || !s_Bcw.haveState) return;
    float now = (float)(MP_NowMs() % 100000000ULL);
    uint64_t nowMs = MP_NowMs();
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if(Game::IsReady() && !Game::IsPaused()) BcwDrawWorld(bg, now);
    if(s_Bcw.st.state == BCW_LIVE && s_Bcw.view >= 2) BcwDrawMatchHud(ImGui::GetForegroundDrawList(), now);
    if(s_Bcw.view == 1) BcwDrawTeamPicker(now);
    BcwDrawRespawnScreen(now);   // 3.04.4
    BcwDrawRoll(now);
    BcwDrawCooldown(now);
    BcwDrawSpawnerMenu();
    BcwDrawScoreboard(now);
    BcwDrawVote(now);
    if(s_Bcw.bannerAt && nowMs - s_Bcw.bannerAt < 3500)
    {
        float a = nowMs - s_Bcw.bannerAt < 3000 ? 1.0f : 1.0f - (nowMs - s_Bcw.bannerAt - 3000) / 500.0f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(42), ds.y * 0.20f, BcwCol(s_Bcw.bannerCol, a), s_Bcw.banner);
    }
}

// launcher / menu entry: join the 24/7 Brief Case War (or host it on this phone if nobody does)
void MP::JoinBcw()
{
    if(BcwOnServer()) { LeaveServer("Left Brief Case War."); return; }
    s_bBcwJoin = true;
    MP::JoinServer("", MP_BCW_PORT, MP_BCW_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bBcwJoin = false;   // refused (cheat menu, offline...)
}
bool MP::IsOnBcw() { return BcwOnServer(); }
// 3.10: on the connection screen (picking Gustav moves the camera to Verdant Meadows, near where
// your hidden ped is parked) your own name tag was still drawn over the view. Your ped is hidden
// there, so is your tag - also while you wait to respawn and while joining.
bool MP::BcwOwnTagHidden() { return (BcwOnServer() || s_bBcwJoin) && !(s_Bcw.engaged && s_Bcw.view == 2); }
