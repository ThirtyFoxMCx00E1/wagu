// MP 3.23 - Call Of Duty: Modern Warfare 3 (MW3), client side (included by mp.cpp after mp_bcw.inc).
//
// The 24/7 featured server "Call Of Duty: Modern Warfare 3" (port 1931, only on the VPS - a phone
// never hosts it). Port of the SA-MP gamemode MW3SAMP. The server (net/server_mw3.inc) is the
// referee; this file draws and plays:
//   - connection screen at the LS Docks & Warehouses (no beach, no < Spawn >): 8 team buttons
//     (Eurasia, Arabia, Soviet, USA, Australia, Brazil, Taliban, France), then the class grid
//     (15 classes, locked ones need a rank), then DEPLOY
//   - your team base spawn with the team skin, class weapons and the rank bonus (health / armour)
//   - the MW3SAMP map objects (container yard at the docks, bases), 34 capture zones with green
//     rings + names, radar zones in the owner colours, the capture bar
//   - [TAG] name tags with a second line "Rank - Class", chat + kill feed in the team colours
//   - kill badges (DOUBLE KILL ... SAVAGE) in a green box (black outline top + bottom only),
//     killing sprees, rewards, rank ups, the hourly tournament, the events, DM arenas, duels
//   - the MW3 player list (id / name / score / ping, every row in the team colour)

static const uint32_t MW3_UI_GREEN = 0x3CFF6AFF;
static const uint32_t MW3_UI_DARK  = 0x0B2A12FF;
static const Vec3 kMw3LobbyCam  = { 2770.0f, -2690.0f, 52.0f };
static const Vec3 kMw3LobbyLook = { 2560.0f, -2480.0f, 14.0f };

struct Mw3ClientState
{
    bool        engaged = false;
    int         view = 0;             // 0 off, 1 lobby, 2 playing, 3 dead (respawn wait)
    int         lobbyStep = 0;        // 0 team, 1 class
    PktMw3State st{};  bool haveState = false;
    Mw3Row      row[MP_MAX_PLAYERS]{}; bool haveRow[MP_MAX_PLAYERS]{};
    PktMw3Me    me{};  bool haveMe = false;
    uint8_t     arena = 0, arenaSpot = 0, evKind = 0, evSide = 0, duelW = 24, duelSpot = 0;
    bool        zombie = false;
    uint64_t    connectedAt = 0, heldAt = 0, protectUntil = 0, lastNoWanted = 0, lastHudOff = 0, lastJp = 0, lastVeh = 0;
    bool        held = false, needSpawn = false;
    int         obj[MW3_OBJECT_COUNT]{}; int objNext = 0; uint64_t objRetryAt = 0; bool objDone = false;
    char        banner[96] = ""; uint32_t bannerCol = 0xFFFFFFFF; uint64_t bannerAt = 0;
    char        badge[32] = ""; uint64_t badgeAt = 0; int badgeLv = 0;
    int         rwScore = 0, rwCash = 0, rwXp = 0; uint64_t rwAt = 0;
    bool        listOpen = false; float listScroll = 0;
    uint8_t     pendTeam = 0; uint64_t pendAt = 0;   // tapped a team: deploy as soon as the server has it
    int         interior = 0;
};
static Mw3ClientState s_Mw3;

static bool Mw3OnServer()
{
    return client.GetState() == NET_CONNECTED && MP_IsMw3ServerName(client.GetServerName()[0] ? client.GetServerName() : s_szCurServer);
}
static uint8_t Mw3TeamOf(int id) { return (id >= 0 && id < MP_MAX_PLAYERS && s_Mw3.haveRow[id]) ? s_Mw3.row[id].team : 0; }
static uint8_t Mw3MyTeam() { return Mw3TeamOf(myId); }
static int     Mw3MyRank() { return s_Mw3.haveMe ? (s_Mw3.me.rank ? s_Mw3.me.rank : 1) : 1; }
static bool Mw3SameTeam(int a, int b)
{
    if(!Mw3OnServer()) return false;
    if(s_Mw3.arena == MW3A_EVENT) return (s_Mw3.evKind == MW3E_TDM || s_Mw3.evKind == MW3E_ZOMBIE) && a >= 0 && b >= 0 && a < MP_MAX_PLAYERS && b < MP_MAX_PLAYERS && s_Mw3.row[a].sub == s_Mw3.row[b].sub;
    if(s_Mw3.arena != MW3A_NONE) return false;
    uint8_t x = Mw3TeamOf(a); return x && x == Mw3TeamOf(b);
}
static bool Mw3Hidden() { return s_Mw3.engaged && (s_Mw3.view == 1 || (s_Mw3.view == 3 && s_Mw3.held)); }
static uint32_t Mw3TeamCol(int t) { return t >= 1 && t <= MW3_TEAMS ? g_Mw3Teams[t].rgba : 0xFFFFFFFF; }
static const char* Mw3Name(int id)
{
    if(id == myId) return settings.name;
    if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active) return players[id].name;
    return "?";
}
static void Mw3Banner(uint32_t col, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Mw3.banner, sizeof(s_Mw3.banner), fmt, ap); va_end(ap);
    s_Mw3.bannerCol = col; s_Mw3.bannerAt = MP_NowMs();
}
static void Mw3Send(uint8_t cmd, uint8_t arg, uint8_t arg2 = 0) { PktMw3Cmd c; c.type = PKT_MW3_CMD; c.cmd = cmd; c.arg = arg; c.arg2 = arg2; client.Send(&c, sizeof(c)); }

// [EU] name in the team colour (chat, kill feed, name tags). Admins keep their look.
static bool Mw3Decorate(int id, const char* name, uint32_t& color, char* out, size_t n)
{
    if(!Mw3OnServer()) return false;
    uint8_t t = Mw3TeamOf(id);
    bool admin = !strncasecmp(name, "[ADMIN]", 7) || (id == myId && MP::IsLocalAdmin());
    if(!t || admin) { snprintf(out, n, "%s", name); return true; }
    snprintf(out, n, "%s %s", g_Mw3Teams[t].tag, name);
    color = g_Mw3Teams[t].rgba;
    return true;
}
// second name tag line (above the name): "Rank - Class"
bool MP::Mw3SubTag(int id, char* out, size_t n)
{
    if(!Mw3OnServer() || id < 0 || id >= MP_MAX_PLAYERS || !s_Mw3.haveRow[id] || !s_Mw3.row[id].team) return false;
    const Mw3Row& r = s_Mw3.row[id];
    int rank = r.rank < 1 ? 1 : (r.rank > MW3_RANKS ? MW3_RANKS : r.rank);
    if(s_Mw3.arena == MW3A_EVENT && s_Mw3.evKind == MW3E_ZOMBIE && r.arena == MW3A_EVENT) { snprintf(out, n, "%s", r.sub == 2 ? "ZOMBIE" : "Human"); return true; }
    snprintf(out, n, "%s - %s", g_Mw3RankName[rank], g_Mw3Classes[r.cls < MW3_CLASSES ? r.cls : 0].name);
    return true;
}
bool MP::IsOnMw3() { return Mw3OnServer(); }
bool MP::Mw3OwnTagHidden() { return (Mw3OnServer() || s_bMw3Join) && !(s_Mw3.engaged && s_Mw3.view == 2); }

// ---- map objects (MW3SAMP CreateObject: the docks container yard, bases, ramps) ----------------
static void Mw3DeleteObjects()
{
    for(int i = 0; i < MW3_OBJECT_COUNT; ++i) { if(s_Mw3.obj[i]) Game::DeleteWorldObject(s_Mw3.obj[i]); s_Mw3.obj[i] = 0; }
    s_Mw3.objNext = 0; s_Mw3.objDone = false;
}
// 3.23.2: objects stream by distance like an SA-MP streamer: only the ones within 220 m of the
// player exist (removed past 280 m), at most 3 new ones a frame, none in the lobby. 3.23 made all
// 162 at once on login (99 of them at the LS Docks, 26 models pinned in memory), which could close
// the game on phones with many mods without a crash log.
#define MW3_OBJ_IN  (220.0f * 220.0f)
#define MW3_OBJ_OUT (280.0f * 280.0f)
static void Mw3UpdateObjects(uint64_t now, int ped)
{
    if(!Game::IsReady() || Game::IsPaused() || now < s_Mw3.objRetryAt) return;
    if(s_Mw3.view != 2 || ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    if(!(me.x == me.x) || me.z < -50.0f) return;
    int made = 0, removed = 0;
    for(int i = 0; i < MW3_OBJECT_COUNT; ++i)
    {
        const Mw3Obj& o = g_Mw3Objects[i];
        float dx = o.x - me.x, dy = o.y - me.y, d2 = dx * dx + dy * dy;
        if(s_Mw3.obj[i] > 0)
        {
            if(d2 > MW3_OBJ_OUT && removed < 6) { Game::DeleteWorldObject(s_Mw3.obj[i]); s_Mw3.obj[i] = 0; ++removed; }
            continue;
        }
        if(d2 > MW3_OBJ_IN || made >= 3) continue;
        int h = Game::CreateMapObject(o.model, { o.x, o.y, o.z }, o.rx, o.ry, o.rz);
        if(h == 0) { s_Mw3.objRetryAt = now + 100; return; }      // model still loading
        if(h < 0)                                                  // object pool nearly full
        {
            if(!s_Mw3.objDone) { s_Mw3.objDone = true; logger->Info("MW3: object pool nearly full, map objects paused"); }
            s_Mw3.objRetryAt = now + 3000; return;
        }
        s_Mw3.obj[i] = h; ++made;
    }
    if(made || removed)
    {
        int n = 0; for(int i = 0; i < MW3_OBJECT_COUNT; ++i) if(s_Mw3.obj[i] > 0) ++n;
        if(made && s_Mw3.objNext == 0) logger->Info("MW3: map objects streaming in near you");
        s_Mw3.objNext = n;
    }
    if(made) s_Mw3.objDone = false;
}

// ---- lobby / spawn ----------------------------------------------------------------------------
static void Mw3NoWanted(uint64_t now)
{
    if(!Game::IsReady() || Game::IsPaused()) return;
    if(now - s_Mw3.lastNoWanted >= 250) { s_Mw3.lastNoWanted = now; Game::SetMaxWanted(0); }
    if(Game::GetWantedLevel() > 0) Game::ClearWanted();
}
static void Mw3HudOff(uint64_t now, bool force)
{
    if(force || now - s_Mw3.lastHudOff > 1000) { Game::ShowGameHud(false); Game::SetPlayerControl(false); s_Mw3.lastHudOff = now; }
}
static void Mw3SetInterior(int area)
{
    if(s_Mw3.interior == area) return;
    Game::EnterArea(area);
    s_Mw3.interior = area;
}
static void Mw3Park(int ped)
{
    if(ped <= 0) return;
    if(Game::IsCharInAnyCar(ped)) { Vec3 o = { kMw3LobbyLook.x, kMw3LobbyLook.y, kMw3LobbyLook.z + 1.0f }; Game::WarpOutOfCar(ped, o); }
    Mw3SetInterior(0);
    Game::SetMaxWanted(0); Game::ClearWanted();
    Game::PreloadArea(kMw3LobbyLook, true);
    Game::FreezeChar(ped, false);
    Game::PlaceChar(ped, { kMw3LobbyCam.x, kMw3LobbyCam.y, -100.0f });
    Game::FreezeChar(ped, true);
    Game::SetCharVisible(ped, false);
    Game::SetCharGodMode(ped, true);
    Game::SetFixedCamera(kMw3LobbyCam, kMw3LobbyLook);
    Mw3HudOff(MP_NowMs(), true);
}
static void Mw3EnterLobby(int ped)
{
    logger->Info("MW3: lobby (LS Docks)"); fflush(nullptr);   // 3.23.2: the last step is in the log if the game closes
    if(s_nSpawnedCar > 0)
    {
        if(ped > 0 && Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_nSpawnedCar) { Vec3 o; Game::GetCharPos(ped, o); o.z += 1.0f; Game::WarpOutOfCar(ped, o); }
        if(Game::GetDriverOfCar(s_nSpawnedCar) <= 0 || Game::GetDriverOfCar(s_nSpawnedCar) == ped) Game::DeleteCar(s_nSpawnedCar); else Game::ReleaseCar(s_nSpawnedCar);
        s_nSpawnedCar = 0;
    }
    Mw3Park(ped);
    s_Mw3.view = 1; s_Mw3.held = false; s_Mw3.protectUntil = 0;
    s_Mw3.lobbyStep = 0; s_Mw3.pendTeam = 0;   // tap a team = deploy
}
static void Mw3GiveWeapons(int ped, const int* w, int n)
{
    Game::RemoveAllWeapons(ped);
    for(int i = n - 1; i >= 0; --i) if(w[i] > 0) Game::GiveAndSelectWeapon(ped, w[i]);   // the first one ends up selected
}
// spawn where the server says I am: my base, a DM arena, the event (docks) or a duel
static void Mw3SpawnNow(int ped, uint64_t now)
{
    logger->Info("MW3: deploy"); fflush(nullptr);   // 3.23.2: the last step is in the log if the game closes
    uint8_t team = Mw3MyTeam();
    if(ped <= 0 || !team) return;
    Vec3 at; float h = 0; int area = 0;
    int skin = g_Mw3Teams[team].skin;
    const Mw3Class& C = g_Mw3Classes[s_Mw3.haveRow[myId] && s_Mw3.row[myId].cls < MW3_CLASSES ? s_Mw3.row[myId].cls : 0];
    int weapons[6] = { 0 }; int nw = 0;
    if(s_Mw3.arena >= MW3A_DM && s_Mw3.arena <= MW3A_HSDM)
    {
        const Mw3ArenaDef& A = g_Mw3Arenas[s_Mw3.arena];
        int k = A.nsp ? rand() % A.nsp : 0;
        at = { A.sp[k][0], A.sp[k][1], A.sp[k][2] }; h = (float)(rand() % 360);
        area = A.interior;
        for(int i = 0; i < 4 && A.weapons[i]; ++i) weapons[nw++] = A.weapons[i];
    }
    else if(s_Mw3.arena == MW3A_EVENT)
    {
        int k = s_Mw3.needSpawn ? s_Mw3.arenaSpot % 8 : rand() % 8;
        at = { g_Mw3DockSpawns[k][0], g_Mw3DockSpawns[k][1], g_Mw3DockSpawns[k][2] }; h = (float)(rand() % 360);
        if(s_Mw3.zombie) { skin = 162; weapons[nw++] = 9; }
        else { weapons[nw++] = 31; weapons[nw++] = 24; weapons[nw++] = 25; }
    }
    else if(s_Mw3.arena == MW3A_DUEL)
    {
        int k = s_Mw3.duelSpot ? 3 : 0;
        at = { g_Mw3Ar_CBDMS[k][0], g_Mw3Ar_CBDMS[k][1], g_Mw3Ar_CBDMS[k][2] }; h = s_Mw3.duelSpot ? 270.0f : 90.0f;
        area = g_Mw3Arenas[MW3A_CBDM].interior;
        weapons[nw++] = s_Mw3.duelW ? s_Mw3.duelW : 24;
    }
    else
    {
        const Mw3SpawnSet& S = g_Mw3TeamSpawns[team - 1];
        int k = S.n ? rand() % S.n : 0;
        at = { S.p[k][0], S.p[k][1], S.p[k][2] }; h = S.p[k][3];
        for(int i = 0; i < 6 && C.weapons[i]; ++i) weapons[nw++] = C.weapons[i];
    }
    s_Mw3.needSpawn = false;
    Game::SetMaxWanted(0); Game::ClearWanted();
    if(Game::IsCharInAnyCar(ped)) { Vec3 o = { at.x, at.y, at.z + 1.0f }; Game::WarpOutOfCar(ped, o); }
    Mw3SetInterior(area);
    Game::PreloadArea(at, true);
    Game::FreezeChar(ped, false);
    Game::SetLocalModel(skin);
    ped = Game::LocalPed(); if(ped <= 0) return;
    Game::SetCharVisible(ped, !s_Cheat.invisible);
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    if(area) Game::Teleport(ped, at, h); else Game::TeleportGround(ped, at, h);
    Mw3GiveWeapons(ped, weapons, nw);
    int hp = 100, ar = 0; Mw3RankBonus(Mw3MyRank(), hp, ar);
    if(s_Mw3.arena != MW3A_NONE) { hp = 100; ar = s_Mw3.arena == MW3A_DUEL ? 0 : 50; }
    Game::SetCharHealth(ped, hp);
    if(ar > 0) Game::AddArmour(ped, ar);
    Game::SetCharGodMode(ped, true);   // spawn protection (the server drops damage for 4 s too)
    s_Mw3.protectUntil = now + MW3_SPAWN_PROTECT_MS;
    s_Mw3.view = 2; s_Mw3.held = false;
    Mw3Send(MW3C_SPAWNED, 0);
}
static void Mw3HoldDead(int ped)
{   // the game respawned us (hospital): back to the docks camera with the respawn countdown
    Mw3Park(ped);
    s_Mw3.held = true; s_Mw3.heldAt = MP_NowMs();
}
static void Mw3Shutdown()
{
    logger->Info("MW3: left"); fflush(nullptr);   // 3.23.2: the last step is in the log if the game closes
    if(!s_Mw3.engaged) { s_bMw3Join = false; return; }
    Mw3DeleteObjects();
    int ped = Game::LocalPed();
    if(ped > 0 && Game::IsReady())
    {
        Game::EnterArea(0);
        Game::FreezeChar(ped, false);
        Game::SetCharVisible(ped, !s_Cheat.invisible);
        Game::SetCharGodMode(ped, false);
        if(s_Mw3.view == 1 || s_Mw3.view == 3 || s_Mw3.interior) Game::Teleport(ped, kGroveStreet, kGroveHeading);
    }
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::SetMaxWanted(6);
    s_Mw3 = Mw3ClientState();
    s_bMw3Join = false;
}
static bool Mw3JoinTakeover(int ped)
{
    logger->Info("MW3: join takeover"); fflush(nullptr);   // 3.23.2: the last step is in the log if the game closes
    if(!s_bMw3Join && !Mw3OnServer()) return false;
    s_bMw3Join = false;
    s_Mw3.engaged = true; s_Mw3.connectedAt = s_Mw3.connectedAt ? s_Mw3.connectedAt : MP_NowMs();
    Mw3EnterLobby(ped);
    return true;
}
static void Mw3JoinCam(Vec3& pos, Vec3& look) { pos = kMw3LobbyCam; look = kMw3LobbyLook; }

// ---- packets --------------------------------------------------------------------------------------
static void Mw3OnPacket(const uint8_t* d, int len)
{
    if(len < 1) return;
    switch(d[0])
    {
    case PKT_MW3_STATE:
        if(len < (int)sizeof(PktMw3State)) return;
        memcpy(&s_Mw3.st, d, sizeof(PktMw3State)); s_Mw3.haveState = true;
        break;
    case PKT_MW3_PLAYERS:
    {
        if(len < 2) return;
        int n = d[1]; if(n > MW3_ROWS_WIRE) n = MW3_ROWS_WIRE;
        if(len < 2 + n * (int)sizeof(Mw3Row)) return;
        bool seen[MP_MAX_PLAYERS] = { false };
        const Mw3Row* rows = (const Mw3Row*)(d + 2);
        for(int i = 0; i < n; ++i) { Mw3Row r; memcpy(&r, &rows[i], sizeof(r)); if(r.id < MP_MAX_PLAYERS) { s_Mw3.row[r.id] = r; s_Mw3.haveRow[r.id] = true; seen[r.id] = true; } }
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(!seen[i]) s_Mw3.haveRow[i] = false;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(seen[i] && players[i].active) players[i].score = s_Mw3.row[i].score;   // the player list / scoreboard
        break;
    }
    case PKT_MW3_ME:
        if(len < (int)sizeof(PktMw3Me)) return;
        memcpy(&s_Mw3.me, d, sizeof(PktMw3Me)); s_Mw3.haveMe = true;
        s_nMyScore = s_Mw3.me.score;
        if(s_Mw3.view == 2 && Game::IsReady() && !Game::IsPaused())
        {   // the server keeps the MW3 cash: the phone shows exactly that
            int diff = s_Mw3.me.cash - Game::GetMoney();
            if(diff) Game::AddMoney(diff);
        }
        break;
    case PKT_MW3_EVENT:
    {
        if(len < (int)sizeof(PktMw3Event)) return;
        PktMw3Event e; memcpy(&e, d, sizeof(e)); e.text[sizeof(e.text) - 1] = 0;
        const char* who = e.who < MP_MAX_PLAYERS ? Mw3Name(e.who) : "";
        uint64_t now = MP_NowMs();
        int ped = Game::LocalPed();
        switch(e.ev)
        {
        case MW3V_BADGE:
            s_Mw3.badgeLv = e.a; MP_CopyStr(s_Mw3.badge, e.a < 8 ? g_Mw3Badge[e.a] : "SAVAGE", sizeof(s_Mw3.badge)); s_Mw3.badgeAt = now;
            break;
        case MW3V_REWARD:
            if(now - s_Mw3.rwAt > 2500) { s_Mw3.rwScore = s_Mw3.rwCash = s_Mw3.rwXp = 0; }
            s_Mw3.rwScore += e.a; s_Mw3.rwXp += e.b; s_Mw3.rwCash += e.val; s_Mw3.rwAt = now;
            break;
        case MW3V_CAP_START:
            if(e.a < MW3_ZONE_COUNT && e.b == Mw3MyTeam() && e.who == myId) Mw3Banner(MW3_UI_GREEN, "CAPTURING %s", g_Mw3Zones[e.a].name);
            break;
        case MW3V_CAP_FAIL:
            if(e.who == myId && e.a < MW3_ZONE_COUNT) Mw3Banner(0xFF5050FF, "CAPTURE FAILED");
            break;
        case MW3V_CAPTURED:
            if(e.a < MW3_ZONE_COUNT && e.b == Mw3MyTeam()) Mw3Banner(Mw3TeamCol(e.b), "%s CAPTURED", g_Mw3Zones[e.a].name);
            if(e.a < MW3_ZONE_COUNT && e.b <= MW3_TEAMS) s_Mw3.st.zoneOwner[e.a] = e.b, s_Mw3.st.zoneAtk[e.a] = 0;
            break;
        case MW3V_EVENT_SPAWN:
            s_Mw3.arena = MW3A_EVENT; s_Mw3.evKind = e.a; s_Mw3.evSide = e.b; s_Mw3.arenaSpot = e.c;
            s_Mw3.zombie = e.a == MW3E_ZOMBIE && e.b == 2;
            Mw3Banner(MW3_UI_GREEN, "%s", e.text[0] ? e.text : "EVENT");
            if(e.a == MW3E_TDM) SysMsg(e.b == 1 ? 0xFF4040FF : 0x4080FFFF, "[EVENT] You are in the %s team.", e.b == 1 ? "red" : "blue");
            if(s_Mw3.zombie) SysMsg(COL_RED, "[EVENT] You are the first zombie. Infect the humans with the chainsaw.");
            s_Mw3.needSpawn = true;
            if(s_Mw3.view == 2 && !s_bLocalDead) Mw3SpawnNow(ped, now);
            break;
        case MW3V_EVENT_OUT:
            s_Mw3.arena = MW3A_NONE; s_Mw3.zombie = false; s_Mw3.evSide = 0;
            if(s_Mw3.view == 2 && !s_bLocalDead) Mw3SpawnNow(ped, now);
            break;
        case MW3V_DUEL_START:
            s_Mw3.arena = MW3A_DUEL; s_Mw3.duelW = e.a ? e.a : 24; s_Mw3.duelSpot = e.c;
            Mw3Banner(0xFFE14AFF, "DUEL VS %s", who);
            if(s_Mw3.view == 2 && !s_bLocalDead) Mw3SpawnNow(ped, now);
            break;
        case MW3V_ARENA:
            s_Mw3.arena = e.a <= MW3A_HSDM ? e.a : 0; s_Mw3.arenaSpot = e.c;
            if(s_Mw3.arena) Mw3Banner(MW3_UI_GREEN, "%s", g_Mw3Arenas[s_Mw3.arena].name);
            if(s_Mw3.view == 2 && !s_bLocalDead) Mw3SpawnNow(ped, now);
            break;
        case MW3V_RANKUP:
            Mw3Banner(MW3_UI_GREEN, "PROMOTED: %s", e.a <= MW3_RANKS ? g_Mw3RankName[e.a] : "");
            break;
        case MW3V_ZOMBIE:
            s_Mw3.zombie = true; s_Mw3.evSide = 2;
            Mw3Banner(0xFF4040FF, "YOU ARE A ZOMBIE");
            break;
        case MW3V_HEAL:
            if(ped > 0 && s_Mw3.view == 2) Game::SetCharHealth(ped, e.val > 0 ? e.val : 100);
            break;
        case MW3V_TEXT:
            Mw3Banner((uint32_t)e.val ? (uint32_t)e.val : 0xFFFFFFFF, "%s", e.text);
            break;
        }
        break;
    }
    }
}

// ---- radar zones (owner colours, the zone being captured flashes) --------------------------------
static int Mw3RadarZones(Game::RadarZone* out, int maxOut)
{
    if(!Mw3OnServer() || !s_Mw3.haveState || s_Mw3.view != 2 || s_Mw3.arena != MW3A_NONE) return 0;
    const bool flash = (MP_NowMs() / 500) % 2 == 0;
    int n = 0;
    for(int t = 1; t <= MW3_TEAMS && n < maxOut; ++t)
    {
        const float* b = g_Mw3Teams[t].base;
        out[n++] = { b[0], b[1], b[2], b[3], (g_Mw3Teams[t].rgba & 0xFFFFFF00) | 0x70 };
    }
    for(int i = 0; i < MW3_ZONE_COUNT && n < maxOut; ++i)
    {
        const Mw3Zone& z = g_Mw3Zones[i];
        int team = (s_Mw3.st.zoneAtk[i] && flash) ? s_Mw3.st.zoneAtk[i] : s_Mw3.st.zoneOwner[i];
        uint32_t c = team ? Mw3TeamCol(team) : 0xFFFFFFFF;
        out[n++] = { z.minX, z.minY, z.maxX, z.maxY, (c & 0xFFFFFF00) | (team ? 0x80 : 0x40) };
    }
    return n;
}

// ---- game tick ------------------------------------------------------------------------------------
static void Mw3Process(int ped, uint64_t now)
{
    if(!Mw3OnServer())
    {
        if(s_Mw3.engaged) Mw3Shutdown();
        return;
    }
    if(!s_Mw3.engaged)
    {
        if(JoinFlowActive()) return;   // the join overlay hands over (Mw3JoinTakeover)
        s_Mw3.engaged = true; s_Mw3.connectedAt = now;
        Mw3EnterLobby(ped);
    }
    Mw3NoWanted(now);   // never wanted (bases are in Bone County / restricted areas)
    Mw3UpdateObjects(now, ped);
    if(ped <= 0) return;
    if(s_Mw3.view == 1)
    {
        Mw3HudOff(now, false);
        if(now % 2000 < 40) { Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false); }
        if(s_Mw3.lobbyStep == 1 && !Mw3MyTeam()) s_Mw3.lobbyStep = 0;
        if(s_Mw3.pendTeam)
        {   // no waiting for other players: tap a team and you are in
            if(Mw3MyTeam() == s_Mw3.pendTeam) { s_Mw3.pendTeam = 0; Mw3SpawnNow(ped, now); }
            else if(now - s_Mw3.pendAt > 4000) s_Mw3.pendTeam = 0;
        }
    }
    else if(s_Mw3.view == 2)
    {
        if(s_bLocalDead)
        {
            s_Mw3.view = 3; s_Mw3.held = false;
            if(s_Mw3.arena == MW3A_DUEL) s_Mw3.arena = MW3A_NONE;   // a duel ends with the first death
        }
        else
        {
            if(s_Mw3.protectUntil && now >= s_Mw3.protectUntil) { Game::SetCharGodMode(ped, false); s_Mw3.protectUntil = 0; }
            if(s_Mw3.needSpawn) Mw3SpawnNow(ped, now);
        }
    }
    if(s_Mw3.view == 3)
    {
        if(!s_bLocalDead && !s_Mw3.held) Mw3HoldDead(ped);
        if(s_Mw3.held) Mw3HudOff(now, false);
        if(s_Mw3.held && now - s_Mw3.heldAt >= MW3_RESPAWN_MS) Mw3SpawnNow(ped, now);
    }
}

// ---- commands -------------------------------------------------------------------------------------
static void Mw3ShowHelp()
{
    ShowDialog(9301, 0, "{3CFF6A}MW3 - Help",
        "{FFFFFF}Capture zones: stand in a zone checkpoint (on foot) for 25 s.\n"
        "Kills: +2 score, $1500. Captures: +5 score, $5000. Owned zones pay every 10 min.\n"
        "Kill 2+ players within 6 s for a badge. Every 5 kills = a killing spree bonus.\n\n"
        "{3CFF6A}/dm /sdm /mdm /chdm /cbdm /hsdm{FFFFFF} - deathmatch arenas, {3CFF6A}/qdm{FFFFFF} - leave\n"
        "{3CFF6A}/duel <id> [weapon]{FFFFFF}, {3CFF6A}/accept{FFFFFF} - 1 vs 1 duel\n"
        "{3CFF6A}/join /leave{FFFFFF} - events (every 15 min at the LS Docks)\n"
        "{3CFF6A}/team /class{FFFFFF} - change (back to the docks screen)\n"
        "{3CFF6A}/jp{FFFFFF} JetTrooper  {3CFF6A}/heal{FFFFFF} Medic  {3CFF6A}/tank{FFFFFF} Engineer  {3CFF6A}/air{FFFFFF} Pilot\n"
        "{3CFF6A}/ranks /classes /stats /players /kill",
        "Close", "", nullptr);
}
static void Mw3ShowRanks()
{
    std::string s = "Rank\tName\tScore\n";
    char b[96];
    for(int r = 1; r <= MW3_RANKS; ++r) { snprintf(b, sizeof(b), "%s%d\t%s\t%d\n", r == Mw3MyRank() ? "{3CFF6A}" : "", r, g_Mw3RankName[r], g_Mw3RankScore[r]); s += b; }
    ShowDialog(9302, 5, "{3CFF6A}MW3 - Ranks", s.c_str(), "Close", "", nullptr);
}
static void Mw3ShowClasses()
{
    std::string s = "Class\tRank\tWeapons\n";
    char b[160];
    for(int c = 0; c < MW3_CLASSES; ++c)
    {
        const Mw3Class& C = g_Mw3Classes[c];
        snprintf(b, sizeof(b), "%s%s\t%d\t%s\n", C.rank <= Mw3MyRank() ? "{FFFFFF}" : "{808080}", C.name, C.rank, C.info);
        s += b;
    }
    ShowDialog(9303, 5, "{3CFF6A}MW3 - Classes", s.c_str(), "Close", "", nullptr);
}
static void Mw3ShowStats()
{
    const PktMw3Me& m = s_Mw3.me;
    char b[512];
    int r = Mw3MyRank();
    snprintf(b, sizeof(b),
        "{FFFFFF}Rank: {3CFF6A}%s (%d){FFFFFF}\nScore: %d   Next rank: %s\nXP: %d\nCash: $%d\n\nKills: %d\nDeaths: %d\nRatio: %.2f\nZone captures: %d\nDeagle kills: %d\nBest killing spree: %d\nKilling spree now: %d",
        g_Mw3RankName[r], r, m.score, r < MW3_RANKS ? std::to_string(g_Mw3RankScore[r + 1]).c_str() : "max", m.xp, m.cash,
        m.kills, m.deaths, m.deaths ? (float)m.kills / m.deaths : (float)m.kills, m.caps, m.deagle, m.best, m.spree);
    ShowDialog(9304, 0, "{3CFF6A}MW3 - Stats", b, "Close", "", nullptr);
}
static void Mw3AirPick(int response, int item, const char*, int)
{
    if(!response) return;
    static const int kAir[3] = { 425, 520, 447 };
    if(item >= 0 && item < 3 && SpawnVehicleForMe(kAir[item], false)) s_Mw3.lastVeh = MP_NowMs();
}
static bool Mw3InOwnBase()
{
    int ped = Game::LocalPed(); Vec3 p;
    return ped > 0 && Game::GetCharPos(ped, p) && Mw3InBase(Mw3MyTeam(), p.x, p.y);
}
static bool Mw3Command(const char* cmd, const char* arg)
{
    if(!Mw3OnServer()) return false;
    const uint8_t cls = s_Mw3.haveRow[myId] ? s_Mw3.row[myId].cls : 0;
    const uint8_t fl = g_Mw3Classes[cls < MW3_CLASSES ? cls : 0].flags;
    uint64_t now = MP_NowMs();
    for(int a = 1; a <= MW3A_HSDM; ++a)
        if(!strcmp(cmd, g_Mw3Arenas[a].cmd)) { Mw3Send(MW3C_ARENA, (uint8_t)a); return true; }
    if(!strcmp(cmd, "qdm") || !strcmp(cmd, "exitdm") || !strcmp(cmd, "leavedm")) { if(s_Mw3.arena >= MW3A_DM && s_Mw3.arena <= MW3A_HSDM) Mw3Send(MW3C_ARENA, 0); else SysMsg(COL_RED, "You are not in a DM arena."); return true; }
    if(!strcmp(cmd, "duel"))
    {
        int id = -1, w = 24;
        if(sscanf(arg, "%d %d", &id, &w) < 1 || id < 0 || id >= MP_MAX_PLAYERS) { SysMsg(COL_YELLOW, "Use: /duel <id> [weapon id, 24 = Desert Eagle]"); return true; }
        Mw3Send(MW3C_DUEL, (uint8_t)id, (uint8_t)w);
        return true;
    }
    if(!strcmp(cmd, "accept")) { Mw3Send(MW3C_ACCEPT, 0); return true; }
    if(!strcmp(cmd, "join"))   { Mw3Send(MW3C_JOIN_EVENT, 0); return true; }
    if(!strcmp(cmd, "leave"))  { Mw3Send(MW3C_LEAVE_EVENT, 0); return true; }
    if(!strcmp(cmd, "startevent") || !strcmp(cmd, "event"))
    {
        int k = atoi(arg);
        Mw3Send(MW3C_START_EVENT, (uint8_t)(k >= MW3E_TDM && k < MW3E_KINDS ? k : 0));
        if(!*arg) SysMsg(COL_SYSTEM, "/startevent 1 TDM, 2 DM, 3 Zombie, 4 Last Man Standing");
        return true;
    }
    if(!strcmp(cmd, "heal")) { Mw3Send(MW3C_HEAL, 0); return true; }
    if(!strcmp(cmd, "jp") || !strcmp(cmd, "jetpack"))
    {
        if(!(fl & MW3CF_JETPACK)) { SysMsg(COL_RED, "Only the JetTrooper class has a jetpack (rank %d).", g_Mw3Classes[7].rank); return true; }
        if(s_Mw3.view != 2 || s_Mw3.arena != MW3A_NONE) return true;
        if(now - s_Mw3.lastJp < 15000) { SysMsg(COL_YELLOW, "Jetpack ready in %d s.", (int)((15000 - (now - s_Mw3.lastJp)) / 1000) + 1); return true; }
        s_Mw3.lastJp = now; Game::GiveLocalJetpack();
        return true;
    }
    if(!strcmp(cmd, "tank") || !strcmp(cmd, "air"))
    {
        bool tank = cmd[0] == 't';
        if(!(fl & (tank ? MW3CF_TANK : MW3CF_AIR))) { SysMsg(COL_RED, tank ? "Only the Engineer class can drive the tank." : "Only the Pilot class can fly."); return true; }
        if(s_Mw3.view != 2 || s_Mw3.arena != MW3A_NONE || !Mw3InOwnBase()) { SysMsg(COL_RED, "Only in your own base."); return true; }
        if(now - s_Mw3.lastVeh < 60000) { SysMsg(COL_YELLOW, "Next vehicle in %d s.", (int)((60000 - (now - s_Mw3.lastVeh)) / 1000) + 1); return true; }
        if(tank) { if(SpawnVehicleForMe(432, false)) s_Mw3.lastVeh = now; }
        else ShowDialog(9305, 2, "{3CFF6A}MW3 - Aircraft", "Hunter\nHydra\nSeasparrow", "Spawn", "Cancel", Mw3AirPick);
        return true;
    }
    if(!strcmp(cmd, "team") || !strcmp(cmd, "class") || !strcmp(cmd, "changeteam") || !strcmp(cmd, "changeclass"))
    {
        if(s_Mw3.arena != MW3A_NONE) { SysMsg(COL_RED, "Leave the arena / event / duel first."); return true; }
        Mw3EnterLobby(Game::LocalPed());
        s_Mw3.lobbyStep = (!strcmp(cmd, "team") || !strcmp(cmd, "changeteam")) ? 0 : 1;
        if(s_Mw3.lobbyStep == 1 && !Mw3MyTeam()) s_Mw3.lobbyStep = 0;
        return true;
    }
    if(!strcmp(cmd, "help") || !strcmp(cmd, "cmds") || !strcmp(cmd, "commands")) { Mw3ShowHelp(); return true; }
    if(!strcmp(cmd, "ranks"))   { Mw3ShowRanks(); return true; }
    if(!strcmp(cmd, "classes")) { Mw3ShowClasses(); return true; }
    if(!strcmp(cmd, "stats"))   { Mw3ShowStats(); return true; }
    if(!strcmp(cmd, "players") || !strcmp(cmd, "list") || !strcmp(cmd, "score")) { s_Mw3.listOpen = !s_Mw3.listOpen; return true; }
    static const char* const kOk[] = { "q", "quit", "pm", "me", "ping", "time", "clear", "id", "kill", "char" };
    for(const char* c : kOk) if(!strcmp(cmd, c)) return false;
    SysMsg(COL_RED, "/%s is not available on Modern Warfare 3. Type /help.", cmd);
    return true;
}

// ================================ drawing =====================================================
static ImU32 Mw3Col(uint32_t rgba, float a) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }
static void Mw3GlowRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float now, float alpha = 1.0f)
{   // the green MW3 frame: soft glow + bright border + a light sweep
    float pulse = 0.6f + 0.4f * sinf(now * 0.004f);
    for(int i = 3; i >= 1; --i)
    {
        float g = UI::S(3.0f * i);
        dl->AddRect(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), Mw3Col(MW3_UI_GREEN, 0.10f * pulse * alpha * (4 - i)), r + g, 0, UI::S(3));
    }
    dl->AddRect(a, b, Mw3Col(MW3_UI_GREEN, 0.95f * alpha), r, 0, UI::S(2.5f));
    float w = b.x - a.x, sx = a.x + fmodf(now * 0.25f, w + UI::S(120)) - UI::S(60);
    float x0 = sx < a.x ? a.x : sx, x1 = sx + UI::S(60) > b.x ? b.x : sx + UI::S(60);
    if(x1 > x0) dl->AddRectFilled(ImVec2(x0, a.y), ImVec2(x1, a.y + UI::S(3)), Mw3Col(0xFFFFFFFF, 0.55f * alpha));
}

static void Mw3DrawWorld(ImDrawList* dl, float now)
{
    if(s_Mw3.view != 2 || s_Mw3.arena != MW3A_NONE || !s_Mw3.haveState) return;
    const uint8_t mine = Mw3MyTeam();
    for(int i = 0; i < MW3_ZONE_COUNT; ++i)
    {
        const Mw3Zone& z = g_Mw3Zones[i];
        Vec3 c = { z.cp[0], z.cp[1], z.cp[2] - 0.9f };
        if(BcwCamDist(c) > 420.0f) continue;
        uint8_t own = s_Mw3.st.zoneOwner[i], atk = s_Mw3.st.zoneAtk[i];
        uint32_t col = own ? Mw3TeamCol(own) : MW3_UI_GREEN;
        if(atk && fmodf(now, 600.0f) < 300.0f) col = Mw3TeamCol(atk);
        BcwDrawZone(dl, c, MW3_CAP_R, col, now, true, 1.4f, false);
        char t[96];
        snprintf(t, sizeof(t), "%s%s%s", z.name, own ? "  " : "", own ? g_Mw3Teams[own].tag : "");
        BcwLabel3D(dl, { c.x, c.y, c.z + 3.2f }, t, own == mine && own ? MW3_UI_GREEN : col, 30.0f, 40.0f);
        if(atk)
        {
            snprintf(t, sizeof(t), "%s capturing  %d%%", g_Mw3Teams[atk].name, s_Mw3.st.zoneProg[i] * 100 / 255);
            BcwLabel3D(dl, { c.x, c.y, c.z + 2.3f }, t, Mw3TeamCol(atk), 22.0f, 40.0f);
        }
    }
    for(int t = 1; t <= MW3_TEAMS; ++t)
    {
        Vec3 p = { g_Mw3CmdCP[t - 1][0], g_Mw3CmdCP[t - 1][1], g_Mw3CmdCP[t - 1][2] + 4.0f };
        char b[64]; snprintf(b, sizeof(b), "%s %s BASE", g_Mw3Teams[t].tag, g_Mw3Teams[t].name);
        BcwLabel3D(dl, p, b, g_Mw3Teams[t].rgba, 34.0f, 60.0f);
    }
}

// the kill badge: green box, black outline on the top and bottom only (no left / right outline)
static void Mw3DrawBadge(ImDrawList* dl, uint64_t nowMs)
{
    if(!s_Mw3.badgeAt || nowMs - s_Mw3.badgeAt > 2600) return;
    float t = (nowMs - s_Mw3.badgeAt) / 1000.0f;
    float pop = t < 0.18f ? 0.6f + 0.4f * (t / 0.18f) * 1.15f : (t < 0.30f ? 1.07f - 0.07f * ((t - 0.18f) / 0.12f) : 1.0f);
    float a = t < 2.2f ? 1.0f : 1.0f - (t - 2.2f) / 0.4f;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float fs = UI::S(40) * pop;
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, s_Mw3.badge);
    float w = ts.x + UI::S(70) * pop, h = UI::S(66) * pop, cx = ds.x * 0.5f, y = UI::S(150);
    ImVec2 p0(cx - w * 0.5f, y), p1(cx + w * 0.5f, y + h);
    // VFX: green glow behind, light streaks
    for(int i = 4; i >= 1; --i) { float g = UI::S(5.0f * i); dl->AddRectFilled(ImVec2(p0.x - g, p0.y - g * 0.5f), ImVec2(p1.x + g, p1.y + g * 0.5f), Mw3Col(MW3_UI_GREEN, 0.06f * a * (5 - i))); }
    dl->AddRectFilled(p0, p1, Mw3Col(0x2BD94FFF, 0.92f * a));
    dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, p0.y + h * 0.45f), Mw3Col(0xFFFFFFFF, 0.25f * a), Mw3Col(0xFFFFFFFF, 0.25f * a), Mw3Col(0xFFFFFFFF, 0), Mw3Col(0xFFFFFFFF, 0));
    float ol = UI::S(5);
    dl->AddRectFilled(ImVec2(p0.x, p0.y - ol), ImVec2(p1.x, p0.y), IM_COL32(0, 0, 0, (int)(255 * a)));   // top
    dl->AddRectFilled(ImVec2(p0.x, p1.y), ImVec2(p1.x, p1.y + ol), IM_COL32(0, 0, 0, (int)(255 * a)));   // bottom
    float sweep = fmodf(t * 1.6f, 1.0f) * (w + UI::S(80)) - UI::S(40);
    float sx0 = p0.x + sweep, sx1 = sx0 + UI::S(26);
    if(sx0 < p0.x) sx0 = p0.x; if(sx1 > p1.x) sx1 = p1.x;
    if(sx1 > sx0) dl->AddRectFilled(ImVec2(sx0, p0.y), ImVec2(sx1, p1.y), Mw3Col(0xFFFFFFFF, 0.30f * a));
    UI::OutlinedText(dl, fs, ImVec2(cx - ts.x * 0.5f, y + (h - ts.y) * 0.5f), IM_COL32(255, 255, 255, (int)(255 * a)), s_Mw3.badge);
    if(s_Mw3.badgeLv >= 2)
    {   // small kill icons under it: one per kill
        int n = s_Mw3.badgeLv > 7 ? 7 : s_Mw3.badgeLv;
        float r = UI::S(7), gx = cx - (n - 1) * UI::S(10);
        for(int i = 0; i < n; ++i) dl->AddCircleFilled(ImVec2(gx + i * UI::S(20), p1.y + ol + UI::S(14)), r, Mw3Col(MW3_UI_GREEN, a), 12);
    }
}

static void Mw3DrawHud(ImDrawList* dl, float now, uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktMw3State& st = s_Mw3.st;
    const PktMw3Me& m = s_Mw3.me;
    const uint8_t mine = Mw3MyTeam();
    char t[160];
    // bottom right: stats (normal font)
    {
        int r = Mw3MyRank();
        float fs = UI::S(22), lh = UI::S(27), w = UI::S(330), h = lh * 4 + UI::S(18);
        ImVec2 a(ds.x - w - UI::S(16), ds.y - h - UI::S(16)), b(ds.x - UI::S(16), ds.y - UI::S(16));
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 150), UI::S(10));
        Mw3GlowRect(dl, a, b, UI::S(10), now, 0.8f);
        float y = a.y + UI::S(9), x = a.x + UI::S(12);
        snprintf(t, sizeof(t), "%s %s", mine ? g_Mw3Teams[mine].tag : "", g_Mw3RankName[r]);
        UI::OutlinedText(dl, fs, ImVec2(x, y), Mw3Col(mine ? Mw3TeamCol(mine) : MW3_UI_GREEN, 1), t); y += lh;
        snprintf(t, sizeof(t), "Score: %d   XP: %d", m.score, m.xp);
        UI::OutlinedText(dl, fs, ImVec2(x, y), IM_COL32_WHITE, t); y += lh;
        snprintf(t, sizeof(t), "Kills: %d   Deaths: %d   K/D: %.2f", m.kills, m.deaths, m.deaths ? (float)m.kills / m.deaths : (float)m.kills);
        UI::OutlinedText(dl, fs, ImVec2(x, y), IM_COL32_WHITE, t); y += lh;
        snprintf(t, sizeof(t), "Spree: %d   Caps: %d", m.spree, m.caps);
        UI::OutlinedText(dl, fs, ImVec2(x, y), IM_COL32_WHITE, t);
        if(r < MW3_RANKS)
        {   // rank progress line on the bottom of the box
            float k = (float)(m.score - g_Mw3RankScore[r]) / (float)(g_Mw3RankScore[r + 1] - g_Mw3RankScore[r]); if(k < 0) k = 0; if(k > 1) k = 1;
            dl->AddRectFilled(ImVec2(a.x + UI::S(4), b.y - UI::S(6)), ImVec2(a.x + UI::S(4) + (w - UI::S(8)) * k, b.y - UI::S(3)), Mw3Col(MW3_UI_GREEN, 0.9f));
        }
        if(s_Mw3.rwAt && nowMs - s_Mw3.rwAt < 2500)
        {   // reward popup, floats up
            float k = (nowMs - s_Mw3.rwAt) / 2500.0f;
            char rw[64]; int n = 0;
            if(s_Mw3.rwScore) n += snprintf(rw + n, sizeof(rw) - n, "+%d score  ", s_Mw3.rwScore);
            if(s_Mw3.rwCash)  n += snprintf(rw + n, sizeof(rw) - n, "+$%d", s_Mw3.rwCash);
            if(n) UI::OutlinedText(dl, UI::S(26), ImVec2(a.x, a.y - UI::S(36) - k * UI::S(30)), Mw3Col(MW3_UI_GREEN, 1.0f - k * k), rw);
        }
    }
    // bottom center: tournament + event
    {
        int tl = st.tLeftS;
        const char* leader = st.tLeader < MP_MAX_PLAYERS ? Mw3Name(st.tLeader) : "nobody";
        snprintf(t, sizeof(t), "TOURNAMENT: %s  %02d:%02d  -  Leader: %s (%d)", st.tKind < MW3T_KINDS ? g_Mw3TourneyName[st.tKind] : "", tl / 60, tl % 60, leader, st.tLeaderVal);
        BcwCenterText(dl, UI::S(22), ds.y - UI::S(64), Mw3Col(0xFFE14AFF, 1), t);
        int el = st.evLeftS;
        const char* ek = st.evKind < MW3E_KINDS ? g_Mw3EventName[st.evKind] : "";
        if(st.evState == MW3ES_SIGNUP) snprintf(t, sizeof(t), "EVENT: %s  -  sign-up %d s  -  /join  (%d)", ek, el, st.evCount);
        else if(st.evState == MW3ES_LIVE)
        {
            if(st.evKind == MW3E_TDM) snprintf(t, sizeof(t), "EVENT: %s  -  RED %d : %d BLUE  -  %02d:%02d", ek, st.evSide[0], st.evSide[1], el / 60, el % 60);
            else if(st.evKind == MW3E_ZOMBIE) snprintf(t, sizeof(t), "EVENT: %s  -  %d humans / %d zombies  -  %02d:%02d", ek, st.evSide[0], st.evSide[1], el / 60, el % 60);
            else snprintf(t, sizeof(t), "EVENT: %s  -  %d alive  -  %02d:%02d", ek, st.evCount, el / 60, el % 60);
        }
        else snprintf(t, sizeof(t), "Next event: %s in %02d:%02d", ek, el / 60, el % 60);
        BcwCenterText(dl, UI::S(22), ds.y - UI::S(36), Mw3Col(MW3_UI_GREEN, 1), t);
    }
    // arena / duel tag (top center)
    if(s_Mw3.arena)
    {
        const char* an = s_Mw3.arena <= MW3A_HSDM ? g_Mw3Arenas[s_Mw3.arena].name : (s_Mw3.arena == MW3A_EVENT ? (s_Mw3.evKind < MW3E_KINDS ? g_Mw3EventName[s_Mw3.evKind] : "Event") : "Duel");
        snprintf(t, sizeof(t), "%s%s", an, s_Mw3.arena <= MW3A_HSDM ? "  -  /qdm to leave" : "");
        BcwCenterText(dl, UI::S(24), UI::S(100), Mw3Col(MW3_UI_GREEN, 1), t);
    }
    // capture bar (I am capturing)
    for(int z = 0; z < MW3_ZONE_COUNT; ++z)
    {
        if(!st.zoneAtk[z] || st.zoneCapper[z] != myId) continue;
        float bw = ds.x * 0.36f, bh = UI::S(28), y = ds.y * 0.78f, x = ds.x * 0.5f - bw * 0.5f;
        float k = st.zoneProg[z] / 255.0f;
        float ol = UI::S(4);
        dl->AddRectFilled(ImVec2(x - ol, y - ol), ImVec2(x + bw + ol, y + bh + ol), IM_COL32(0, 0, 0, 255), UI::S(8));
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw, y + bh), Mw3Col(MW3_UI_DARK, 0.9f), UI::S(5));
        if(k > 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bw * k, y + bh), Mw3Col(MW3_UI_GREEN, 1), UI::S(5));
        float shine = fmodf(now * 0.0012f, 1.0f) * bw * k;
        if(k > 0.05f) dl->AddRectFilled(ImVec2(x + shine - UI::S(18) < x ? x : x + shine - UI::S(18), y), ImVec2(x + shine, y + bh), IM_COL32(220, 255, 225, 170));
        snprintf(t, sizeof(t), "CAPTURING %s  %d%%", g_Mw3Zones[z].name, (int)(k * 100));
        BcwCenterText(dl, UI::S(26), y - UI::S(40), IM_COL32_WHITE, t);
        break;
    }
    Mw3DrawBadge(dl, nowMs);
}

// lobby: team buttons (4 x 2), then the class grid, then DEPLOY
static void Mw3DrawTitle(ImDrawList* bg, ImVec2 ds, float now)
{
    float y = ds.y * 0.07f;
    ImVec2 uv(1, 1);
    if(void* tex = HUD::Texture("mw3_guns.png", &uv))
    {
        float s = UI::S(84);
        bg->AddImage((ImTextureID)tex, ImVec2(ds.x * 0.5f - s * 0.5f, y - s - UI::S(4)), ImVec2(ds.x * 0.5f + s * 0.5f, y - UI::S(4)), ImVec2(0, 0), uv);
    }
    float glow = 0.55f + 0.45f * sinf(now * 0.003f);
    BcwCenterText(bg, UI::S(58), y + UI::S(2), Mw3Col(MW3_UI_GREEN, 0.35f * glow), "MODERN WARFARE 3");
    BcwCenterText(bg, UI::S(56), y, IM_COL32_WHITE, "MODERN WARFARE 3");
    BcwCenterText(bg, UI::S(26), y + UI::S(64), Mw3Col(MW3_UI_GREEN, 1), "CALL OF DUTY  -  24/7  -  LOS SANTOS DOCKS & WAREHOUSES");
}
static void Mw3DrawLobby(float now)
{
    if(MP::AuthScreenShown() || SampDialogOpen()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    Mw3DrawTitle(bg, ds, now);
    const PktMw3State& st = s_Mw3.st;
    const uint8_t mine = Mw3MyTeam();
    char t[96];
    if(s_Mw3.lobbyStep == 0)
    {
        BcwCenterText(bg, UI::S(32), ds.y * 0.30f, IM_COL32_WHITE, "TAP A TEAM TO DEPLOY");
        const float bw = UI::S(330), bh = UI::S(84), gap = UI::S(22);
        float tw = bw * 4 + gap * 3;
        BcwBeginOverlay("##Mw3Teams", ImVec2(ds.x * 0.5f - tw * 0.5f - UI::S(18), ds.y * 0.38f), ImVec2(tw + UI::S(36), bh * 2 + gap + UI::S(30)), 0.0f);
        ImGui::SetWindowFontScale(0.95f);
        for(int k = 0; k < MW3_TEAMS; ++k)
        {
            if(k % 4) ImGui::SameLine(0, gap);
            else if(k) ImGui::Dummy(ImVec2(1, gap - ImGui::GetStyle().ItemSpacing.y * 2));
            int team = k + 1;
            snprintf(t, sizeof(t), "%s  %d##t%d", g_Mw3Teams[team].name, st.teamCount[k], team);
            if(BcwTeamButton(t, g_Mw3Teams[team].rgba, ImVec2(bw, bh), mine == team || s_Mw3.pendTeam == team, false) && !s_Mw3.pendTeam)
            {
                if(mine == team) Mw3SpawnNow(Game::LocalPed(), MP_NowMs());
                else { logger->Info("MW3: team %d tapped", team); Mw3Send(MW3C_TEAM, (uint8_t)team); s_Mw3.pendTeam = (uint8_t)team; s_Mw3.pendAt = MP_NowMs(); }
            }
        }
        ImGui::SetWindowFontScale(1.0f);
        BcwEndOverlay();
        {
            const float nb = UI::S(300);
            BcwBeginOverlay("##Mw3Next", ImVec2(ds.x * 0.5f - nb * 0.5f - UI::S(18), ds.y * 0.80f), ImVec2(nb + UI::S(36), UI::S(100)), 0.0f);
            if(BcwTeamButton("CLASS >##n", 0x2BD94FFF, ImVec2(nb, UI::S(72)), false, !mine)) s_Mw3.lobbyStep = 1;
            BcwEndOverlay();
        }
        BcwCenterText(bg, UI::S(22), ds.y * 0.93f, IM_COL32(220, 220, 220, 255), "No waiting: tap a team and you spawn in its base. /class changes your class.");
        return;
    }
    // class grid
    snprintf(t, sizeof(t), "%s %s  -  CHOOSE YOUR CLASS", mine ? g_Mw3Teams[mine].tag : "", mine ? g_Mw3Teams[mine].name : "");
    BcwCenterText(bg, UI::S(30), ds.y * 0.24f, Mw3Col(mine ? Mw3TeamCol(mine) : MW3_UI_GREEN, 1), t);
    const int rank = Mw3MyRank();
    const uint8_t cur = s_Mw3.haveRow[myId] ? s_Mw3.row[myId].cls : 0;
    const float bw = UI::S(258), bh = UI::S(72), gap = UI::S(14);
    float tw = bw * 5 + gap * 4;
    BcwBeginOverlay("##Mw3Classes", ImVec2(ds.x * 0.5f - tw * 0.5f - UI::S(18), ds.y * 0.30f), ImVec2(tw + UI::S(36), bh * 3 + gap * 2 + UI::S(30)), 0.0f);
    ImGui::SetWindowFontScale(0.85f);
    for(int c = 0; c < MW3_CLASSES; ++c)
    {
        if(c % 5) ImGui::SameLine(0, gap);
        else if(c) ImGui::Dummy(ImVec2(1, gap - ImGui::GetStyle().ItemSpacing.y * 2));
        const Mw3Class& C = g_Mw3Classes[c];
        bool locked = C.rank > rank;
        if(locked) snprintf(t, sizeof(t), "%s\nRank %d##c%d", C.name, C.rank, c);
        else snprintf(t, sizeof(t), "%s##c%d", C.name, c);
        if(BcwTeamButton(t, locked ? 0x404040FF : 0x1E8C3AFF, ImVec2(bw, bh), cur == c, locked)) Mw3Send(MW3C_CLASS, (uint8_t)c);
    }
    ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
    const Mw3Class& C = g_Mw3Classes[cur < MW3_CLASSES ? cur : 0];
    snprintf(t, sizeof(t), "%s: %s", C.name, C.info);
    BcwCenterText(bg, UI::S(24), ds.y * 0.30f + bh * 3 + gap * 2 + UI::S(40), IM_COL32_WHITE, t);
    snprintf(t, sizeof(t), "Your rank: %s (%d)  -  score %d", g_Mw3RankName[rank], rank, s_Mw3.me.score);
    BcwCenterText(bg, UI::S(22), ds.y * 0.30f + bh * 3 + gap * 2 + UI::S(74), Mw3Col(MW3_UI_GREEN, 1), t);
    const float nb = UI::S(300);
    BcwBeginOverlay("##Mw3Deploy", ImVec2(ds.x * 0.5f - nb - UI::S(30), ds.y * 0.80f), ImVec2(nb * 2 + UI::S(60), UI::S(100)), 0.0f);
    if(BcwTeamButton("< TEAM##b", 0x555555FF, ImVec2(nb, UI::S(72)), false, false)) s_Mw3.lobbyStep = 0;
    ImGui::SameLine(0, UI::S(24));
    bool can = mine != 0;
    if(BcwTeamButton("DEPLOY##d", 0x2BD94FFF, ImVec2(nb, UI::S(72)), true, !can)) Mw3SpawnNow(Game::LocalPed(), MP_NowMs());
    BcwEndOverlay();
}
static void Mw3DrawRespawn(float now)
{
    if(s_Mw3.view != 3 || !s_Mw3.held || MP::AuthScreenShown()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    Mw3DrawTitle(bg, ds, now);
    BcwCenterText(bg, UI::S(36), ds.y * 0.62f, IM_COL32(255, 80, 80, 255), "YOU DIED");
    const uint8_t mine = Mw3MyTeam();
    uint64_t e = MP_NowMs() - s_Mw3.heldAt; if(e > MW3_RESPAWN_MS) e = MW3_RESPAWN_MS;
    int ticks = (int)((MW3_RESPAWN_MS - e) / 50);
    char t[64]; snprintf(t, sizeof(t), "Respawn in %d.%02d##rsp", ticks / 20, (ticks % 20) * 5);
    const float bw = UI::S(440), bh = UI::S(84);
    BcwBeginOverlay("##Mw3Respawn", ImVec2(ds.x * 0.5f - bw * 0.5f - UI::S(18), ds.y * 0.80f - UI::S(30)), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    ImGui::SetWindowFontScale(0.95f);
    BcwTeamButton(t, mine ? Mw3TeamCol(mine) : 0x2BD94FFF, ImVec2(bw, bh), true, false);
    ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
    float k = (float)e / MW3_RESPAWN_MS;
    ImVec2 p0(ds.x * 0.5f - bw * 0.5f, ds.y * 0.80f - UI::S(16) + bh + UI::S(10));
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(p0, ImVec2(p0.x + bw, p0.y + UI::S(10)), IM_COL32(0, 0, 0, 200), UI::S(5));
    fg->AddRectFilled(p0, ImVec2(p0.x + bw * k, p0.y + UI::S(10)), Mw3Col(MW3_UI_GREEN, 1), UI::S(5));
}

// player list (SA-MP style): "Call Of Duty: Modern Warfare 3 (MW3 3.23)"  Players: N
// id | name | score | ping, each row in the team colour, my row on a red bar with white text
static void Mw3DrawPlayerList()
{
    if(!s_Mw3.listOpen) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ds.x * 0.56f, h = ds.y * 0.70f;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, ds.y * 0.5f - h * 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.05f, 0.03f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.24f, 1.0f, 0.42f, 0.7f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(6));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(16), UI::S(12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, UI::S(18));
    ImGui::Begin("##Mw3List", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos();
    Mw3GlowRect(dl, wp, ImVec2(wp.x + w, wp.y + h), UI::S(6), (float)(MP_NowMs() % 100000000ULL), 0.7f);
    int online = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active) ++online;
    char t[96];
    snprintf(t, sizeof(t), "%s (MW3 %s)", MP_MW3_SERVER_NAME, MP_VERSION_STR);
    ImGui::TextUnformatted(t);
    snprintf(t, sizeof(t), "Players: %d", online);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(t).x - UI::S(70));
    ImGui::TextUnformatted(t);
    ImGui::SameLine(ImGui::GetContentRegionMax().x - UI::S(56));
    if(ImGui::Button("X##mw3lc", ImVec2(UI::S(56), UI::S(44)))) s_Mw3.listOpen = false;
    ImGui::Separator();
    float W = ImGui::GetContentRegionAvail().x - UI::S(20);
    float cId = UI::S(10), cName = W * 0.12f, cScore = W * 0.66f, cPing = W * 0.84f;
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 tc = IM_COL32(200, 200, 200, 255);
        dl->AddText(ImVec2(p.x + cId, p.y), tc, "id");
        dl->AddText(ImVec2(p.x + cName, p.y), tc, "name");
        dl->AddText(ImVec2(p.x + cScore, p.y), tc, "score");
        dl->AddText(ImVec2(p.x + cPing, p.y), tc, "ping");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(6)));
    }
    ImGui::BeginChild("##mw3rows", ImVec2(0, 0), false);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    const float rowH = UI::S(46);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active) continue;
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(W, rowH));
        bool me = i == myId;
        if(me) cl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(190, 25, 25, 220));
        uint8_t team = Mw3TeamOf(i);
        ImU32 col = me ? IM_COL32_WHITE : UI::FromRGBA((team ? Mw3TeamCol(team) : (p.color | 0xFF)));
        float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        char b[64];
        snprintf(b, sizeof(b), "%d", i); cl->AddText(ImVec2(a.x + cId, ty), col, b);
        snprintf(b, sizeof(b), "%s%s%s", team ? g_Mw3Teams[team].tag : "", team ? " " : "", me ? settings.name : p.name);
        cl->AddText(ImVec2(a.x + cName, ty), col, b);
        snprintf(b, sizeof(b), "%d", me ? s_Mw3.me.score : (s_Mw3.haveRow[i] ? s_Mw3.row[i].score : p.score)); cl->AddText(ImVec2(a.x + cScore, ty), col, b);
        int ping = me ? MP::DisplayPing() : p.ping;
        if(ping >= 0) snprintf(b, sizeof(b), "%d", ping); else snprintf(b, sizeof(b), "-");
        cl->AddText(ImVec2(a.x + cPing, ty), col, b);
    }
    ImGui::EndChild();
    ImGui::End();
    ImGui::PopStyleVar(3); ImGui::PopStyleColor(2);
}
static void Mw3OpenList() { s_Mw3.listOpen = true; }
// HUD button that opens the player list
static void Mw3DrawListButton()
{
    if(s_Mw3.view != 2 || s_Mw3.listOpen || JoinFlowActive()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bw = UI::S(110), bh = UI::S(56);
    BcwBeginOverlay("##Mw3ListBtn", ImVec2(ds.x - bw - UI::S(34), ds.y * 0.44f), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    if(BcwTeamButton("LIST##mw3l", 0x1E8C3AFF, ImVec2(bw, bh), false, false)) s_Mw3.listOpen = true;
    BcwEndOverlay();
}

static void RenderMw3()
{
    if(!s_Mw3.engaged) return;
    float now = (float)(MP_NowMs() % 100000000ULL);
    uint64_t nowMs = MP_NowMs();
    if(Game::IsReady() && !Game::IsPaused()) Mw3DrawWorld(ImGui::GetBackgroundDrawList(), now);
    if(s_Mw3.view == 2 && s_Mw3.haveState && !MP::AuthScreenShown()) Mw3DrawHud(ImGui::GetForegroundDrawList(), now, nowMs);
    if(s_Mw3.view == 1) Mw3DrawLobby(now);
    Mw3DrawRespawn(now);
    Mw3DrawListButton();
    Mw3DrawPlayerList();
    if(s_Mw3.bannerAt && nowMs - s_Mw3.bannerAt < 3500)
    {
        float a = nowMs - s_Mw3.bannerAt < 3000 ? 1.0f : 1.0f - (nowMs - s_Mw3.bannerAt - 3000) / 500.0f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(40), ds.y * 0.24f, Mw3Col(s_Mw3.bannerCol, a), s_Mw3.banner);
    }
}

// launcher / menu entry: join the 24/7 MW3 server (the VPS, or a phone when the VPS is down)
void MP::JoinMw3()
{
    if(Mw3OnServer()) { LeaveServer("Left Modern Warfare 3."); return; }
    s_bMw3Join = true;
    MP::JoinServer("", MP_MW3_PORT, MP_MW3_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bMw3Join = false;
}
