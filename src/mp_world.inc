// 3.01: SA-MP features, part 2 (included after mp_games.inc): 3D text labels, map icons, actors,
// gang zones + turf wars, class selection, the SA-MP chat commands and the network handlers.
//
//  - 3D text labels (Create3DTextLabel): over every race / job / derby / DM start and gang spawn
//  - map icons (SetPlayerMapIcon): short-range radar icons with the SA-MP icon ids (open.mp list)
//  - actors (CreateActor): people standing at the job starts, streamed in within 90 m
//  - gang zones (GangZoneShowForAll / GangZoneFlashForAll): drawn by the game on the radar + map
//  - turf wars: the server decides (MPServer::TurfTick), the phone shows the war (PKT_TURF)
//  - class selection (AddPlayerClass + the spawn screen): /class, << >> SPAWN

// ---- map icons + 3D labels: one list of spots ---------------------------------------------------
struct SampSpot { Vec3 p; int sprite; std::string label; uint16_t mode; };   // 3.02: mode = the gamemode it belongs to
static std::vector<SampSpot> s_Spots;
static int JobSprite(const char* cmd)
{
    static const struct { const char* c; int s; } k[] =
    {
        { "trucking", 51 }, { "quarry", 11 }, { "pizza", 29 }, { "burger", 10 }, { "cluckin", 14 },
        { "taxi", 55 }, { "medic", 22 }, { "firetruck", 20 },
    };
    for(auto& e : k) if(!strcmp(e.c, cmd)) return e.s;
    return 52;
}
static const int kTeamSprite[TEAM_COUNT] = { 0, 62, 59, 60, 58, 61, 43, 30 };
static void AddSpot(const Vec3& p, int sprite, const std::string& line, uint16_t mode)
{
    for(auto& s : s_Spots)   // same place + same gamemode: one label, more lines
        if(s.mode == mode && fabsf(s.p.x - p.x) < 6.0f && fabsf(s.p.y - p.y) < 6.0f && fabsf(s.p.z - p.z) < 6.0f)
        {
            if(s.label.size() < 400) s.label += "\n" + line;
            return;
        }
    s_Spots.push_back({ p, sprite, line, mode });
}
static void BuildSpots()
{
    if(!s_Spots.empty()) return;
    for(int i = 0; i < MP_ROUTE_COUNT; ++i)
    {
        const GRoute& r = kRoutes[i];
        char l[160];
        snprintf(l, sizeof(l), "{4DD2FF}%s\n{FFFFFF}/job %s  {33CC55}$%d", r.name, r.cmd, r.reward);
        AddSpot(r.cp[0], JobSprite(r.cmd), l, GM_JOBS);
    }
    for(int i = 0; i < MP_SA_RACES; ++i)   // 3.01: the GTA SA race starts
    {
        const GSaRace& r = kSaRaces[i];
        if(r.area) continue;
        char l[160]; snprintf(l, sizeof(l), "{FFE14D}Race: %s\n{FFFFFF}/race  {A9C4E4}(%s, %d players)", r.name, kVehNames[r.model - 400], MP_RACE_PLAYERS);
        AddSpot(r.cp[0], 53, l, GM_RACE);
    }
    for(int i = 0; i < MP_DM_COUNT; ++i)
    {
        char l[128]; snprintf(l, sizeof(l), "{D25BFF}%s\n{FFFFFF}/dm %s", kDms[i].name, kDms[i].cmd);
        AddSpot({ kDms[i].x, kDms[i].y, kDms[i].z }, 19, l, GM_DM);
    }
    for(int i = 0; i < MP_DERBY_COUNT; ++i)
    {
        if(kDerbies[i].area) continue;   // stadium interior: no icon on the outside map
        char l[128]; snprintf(l, sizeof(l), "{FF8C1E}%s\n{FFFFFF}/derby %s", kDerbies[i].name, kDerbies[i].cmd);
        AddSpot({ kDerbies[i].x, kDerbies[i].y, kDerbies[i].z }, 33, l, GM_DERBY);
    }
    for(int t = TEAM_GROVE; t < TEAM_COUNT; ++t)
    {
        const MPTeamInfo& ti = g_MPTeams[t];
        char l[128]; snprintf(l, sizeof(l), "{%06X}%s\n{FFFFFF}/class", (unsigned)(ti.rgba >> 8), ti.name);
        AddSpot({ ti.sx, ti.sy, ti.sz }, kTeamSprite[t], l, GM_CLASS);
    }
}
static std::vector<int> s_IconBlips;
static uint16_t s_nIconModes = 0;
static uint16_t SpotModesOn() { uint16_t m = 0; for(auto& g : kGameModes) if(MP::ModeOn(g.bit)) m |= g.bit; return m; }
static void UpdateMapIcons(bool want)
{
    uint16_t on = want ? SpotModesOn() : 0;
    if(!s_IconBlips.empty() && on != s_nIconModes) { for(int b : s_IconBlips) if(b) Game::RemoveBlip(b); s_IconBlips.clear(); }   // 3.02: gamemodes changed
    if(want && s_IconBlips.empty())
    {
        BuildSpots();
        s_nIconModes = on;
        for(auto& s : s_Spots) if(on & s.mode) { int b = Game::AddSpriteBlip(s.p, s.sprite); if(b) s_IconBlips.push_back(b); }
        if(s_IconBlips.empty()) s_IconBlips.push_back(0);   // tried: don't retry every frame
    }
    else if(!want && !s_IconBlips.empty())
    {
        for(int b : s_IconBlips) if(b) Game::RemoveBlip(b);
        s_IconBlips.clear();
    }
}
static void RenderLabels(ImDrawList* dl)
{
    if(!settings.labels3d || !myDrawValid || !MP::ModeOn(GM_LABELS)) return;
    BuildSpots();
    const uint16_t modesOn = SpotModesOn();
    static std::vector<bool> vis; static uint64_t losAt = 0;
    const uint64_t now = MP_NowMs();
    if(vis.size() != s_Spots.size()) vis.assign(s_Spots.size(), true);
    bool checkLos = now - losAt > 300; if(checkLos) losAt = now;
    Vec3 cp, cl; Vec3 from = Game::GetCameraPos(cp, cl) ? cp : myDrawPos;
    ImFont* f = UI::Font();
    std::vector<std::string> lines; std::vector<SampSeg> segs;
    for(size_t i = 0; i < s_Spots.size(); ++i)
    {
        const SampSpot& s = s_Spots[i];
        if(!(modesOn & s.mode)) continue;   // 3.02: that gamemode is off here
        float dx = s.p.x - myDrawPos.x, dy = s.p.y - myDrawPos.y, dz = s.p.z - myDrawPos.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if(d > 35.0f) continue;
        Vec3 at = s.p; at.z += 1.2f;
        if(checkLos) vis[i] = Game::LineOfSightClear(from, at);
        if(!vis[i]) continue;
        float sx, sy, depth;
        if(!Game::WorldToScreen(at, sx, sy, depth)) continue;
        float fs = UI::S(30.0f) * fmaxf(0.55f, 1.0f - d / 50.0f);
        SampSplit(s.label, '\n', lines);
        float y = sy - fs * lines.size();
        for(auto& l : lines)
        {
            SampSplitColors(l.c_str(), IM_COL32_WHITE, segs);
            float w = SampSegsWidth(f, fs, segs);
            std::vector<SampSeg> sh = segs; for(auto& g : sh) g.col = IM_COL32(0, 0, 0, 255);
            const float o = fmaxf(1.0f, fs * 0.05f);
            for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f + ox * o, y + oy * o), sh);
            SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f, y), segs);
            y += fs;
        }
    }
}

// ---- actors -----------------------------------------------------------------------------------
struct SampActor { int skin; Vec3 p; float h; const char* anim; const char* file; int ped; bool animOk; };
static SampActor s_Actors[] =
{
    { 155, {2103.0f,-1808.5f,13.55f},  90.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // Well Stacked Pizza
    { 205, { 807.5f,-1618.0f,13.55f}, 270.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // Burger Shot
    { 167, { 926.0f,-1355.5f,13.38f},  90.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // Cluckin' Bell
    { 133, { -73.5f,-1133.5f, 1.08f},  70.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // RS Haul trucker
    {  27, { 603.0f,  862.5f,-40.0f},   0.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // Hunter Quarry
    { 255, {1505.5f,-1061.0f,25.06f},   0.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // taxi rank
    { 274, {2018.0f,-1715.0f,13.55f},  90.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // paramedic
    { 277, {1754.0f,-2104.0f,13.55f},  90.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // firefighter
    { 105, {2490.5f,-1665.0f,13.34f}, 180.0f, "prtial_gngtlkA", "GANGS",       0, false },   // Grove Street (race 1 start)
    { 287, { 403.0f, 2512.0f,16.48f},  90.0f, "Coplook_loop",   "COP_AMBIENT", 0, false },   // War Zone DM guard
};
#define SAMP_ACTOR_COUNT ((int)(sizeof(s_Actors) / sizeof(s_Actors[0])))
static bool IsSampActor(int h)
{
    if(h <= 0) return false;
    for(auto& a : s_Actors) if(a.ped == h) return true;
    return false;
}
static void RemoveActors() { for(auto& a : s_Actors) if(a.ped) { if(Game::PedPtr(a.ped)) Game::DeleteChar(a.ped); a.ped = 0; a.animOk = false; } }
static void UpdateActors(bool want, uint64_t now)
{
    static uint64_t next = 0;
    if(!want) { RemoveActors(); return; }
    if(now < next) return;
    next = now + 500;
    for(auto& a : s_Actors)
    {
        float dx = a.p.x - myDrawPos.x, dy = a.p.y - myDrawPos.y;
        float d = sqrtf(dx * dx + dy * dy);
        if(a.ped && !Game::PedPtr(a.ped)) { a.ped = 0; a.animOk = false; }
        if(a.ped && d > 130.0f) { Game::DeleteChar(a.ped); a.ped = 0; a.animOk = false; continue; }
        if(!a.ped && d < 90.0f)
        {
            if(!Game::ModelReady(a.skin)) continue;
            a.ped = Game::CreateNpcPed(a.skin, a.p, a.h);
            if(a.ped) { Game::SetCharGodMode(a.ped, true); Game::FreezeChar(a.ped, true); }
        }
        if(a.ped && !a.animOk) a.animOk = Game::PlayAnimLoop(a.ped, a.anim, a.file);
    }
}

// ---- gang zones + turf wars ------------------------------------------------------------------
static uint8_t s_TurfOwner[MP_TURF_MAX], s_TurfAtk[MP_TURF_MAX], s_TurfProg[MP_TURF_MAX];
static bool    s_bTurfInit = false;
static void TurfInit()
{
    for(int i = 0; i < MP_TURF_MAX; ++i) { s_TurfOwner[i] = i < MP_TURF_COUNT ? g_MPTurf[i].owner : 0; s_TurfAtk[i] = 0; s_TurfProg[i] = 0; }
    s_bTurfInit = true;
}
static int TurfZoneProvider(Game::RadarZone* out, int maxOut)
{
    if(Mw3OnServer()) return Mw3RadarZones(out, maxOut);   // 3.23
    if(RpOnServer()) return RpRadarZones(out, maxOut);     // 3.25: greenzones
    if(!settings.gangZones || client.GetState() != NET_CONNECTED || JoinFlowActive() || !MP::ModeOn(GM_TURF)) return 0;
    if(!s_bTurfInit) TurfInit();
    const bool flash = (MP_NowMs() / 500) % 2 == 0;
    int n = 0;
    for(int i = 0; i < MP_TURF_COUNT && n < maxOut; ++i)
    {
        const MPTurfZone& z = g_MPTurf[i];
        int team = (s_TurfAtk[i] && flash) ? s_TurfAtk[i] : s_TurfOwner[i];
        uint32_t c = g_MPTeams[team < TEAM_COUNT ? team : 0].rgba;
        out[n++] = { z.minX, z.minY, z.maxX, z.maxY, (c & 0xFFFFFF00) | 0x80 };
    }
    return n;
}
static void SampOnTurf(const PktTurf& t)
{
    if(!s_bTurfInit) TurfInit();
    s_bServer301 = true;
    for(int i = 0; i < t.count && i < MP_TURF_COUNT; ++i)
    {
        s_TurfOwner[i] = t.owner[i] < TEAM_COUNT ? t.owner[i] : 0;
        s_TurfAtk[i] = t.attacker[i] < TEAM_COUNT ? t.attacker[i] : 0;
        s_TurfProg[i] = t.progress[i];
    }
}
static int TurfCount(int team) { int n = 0; for(int i = 0; i < MP_TURF_COUNT; ++i) if(s_TurfOwner[i] == team) ++n; return n; }
static void UpdateTurfHud(int ped)
{
    if(!settings.gangZones || ped <= 0 || client.GetState() != NET_CONNECTED || !MP::ModeOn(GM_TURF)) { TdHide(TD_TURF); return; }
    Vec3 me; if(!Game::GetCharPos(ped, me)) { TdHide(TD_TURF); return; }
    int z = MP_TurfAt(me.x, me.y, me.z);
    if(z < 0) { TdHide(TD_TURF); return; }
    int ow = s_TurfOwner[z], at = s_TurfAtk[z];
    char t[200];
    if(at) snprintf(t, sizeof(t), "%s~n~~r~TURF WAR: ~w~%s %d%%", g_MPTurf[z].name, g_MPTeams[at].tag, s_TurfProg[z]);
    else if(MP_TeamCanTakeTurf(settings.team) && settings.team != ow) snprintf(t, sizeof(t), "%s~n~~w~%s turf - stay to take it%s", g_MPTurf[z].name, g_MPTeams[ow].tag, s_bServer301 ? "" : " (3.01 server)");
    else snprintf(t, sizeof(t), "%s~n~~w~%s turf", g_MPTurf[z].name, g_MPTeams[ow].tag);
    // 3.02: centre of the left edge (bottom left was over the radar with a PC-style HUD radar)
    PktTextDraw d = TdMake(t, 24.0f, 205.0f, 1.0f, 2, 1, g_MPTeams[ow].rgba | 0xFF);
    TdSet(TD_TURF, d);
}
static int s_nTurfGpsBlip = 0; static uint64_t s_nTurfGpsUntil = 0;
static void OnTurfDialog(int resp, int item, const char*, int)
{
    if(!resp || item < 0 || item >= MP_TURF_COUNT) return;
    const MPTurfZone& z = g_MPTurf[item];
    if(s_nTurfGpsBlip) Game::RemoveBlip(s_nTurfGpsBlip);
    Vec3 c = { (z.minX + z.maxX) * 0.5f, (z.minY + z.maxY) * 0.5f, 20.0f };
    s_nTurfGpsBlip = Game::AddCoordBlip(c, g_MPTeams[s_TurfOwner[item]].rgba | 0xFF, 2);
    s_nTurfGpsUntil = MP_NowMs() + 90000;
    SysMsg(COL_SYSTEM, "%s is marked on your radar for 90 seconds.", z.name);
}
static void ShowTurfDialog()
{
    if(!s_bTurfInit) TurfInit();
    std::string body = "Zone\tOwner\tStatus\n";
    for(int i = 0; i < MP_TURF_COUNT; ++i)
    {
        const MPTeamInfo& o = g_MPTeams[s_TurfOwner[i]];
        char row[200];
        if(s_TurfAtk[i]) snprintf(row, sizeof(row), "%s\t{%06X}%s\t{FF4444}attacked by %s (%d%%)\n", g_MPTurf[i].name, (unsigned)(o.rgba >> 8), o.name, g_MPTeams[s_TurfAtk[i]].tag, s_TurfProg[i]);
        else snprintf(row, sizeof(row), "%s\t{%06X}%s\t{A9C4E4}-\n", g_MPTurf[i].name, (unsigned)(o.rgba >> 8), o.name);
        body += row;
    }
    ShowDialog(3020, DIALOG_STYLE_TABLIST_HEADERS, "{FFFFFF}Turf - gang zones", body.c_str(), "Mark", "Close", OnTurfDialog);
}
static void ShowTeamsDialog()
{
    if(!s_bTurfInit) TurfInit();
    std::string body = "Team\tTurf\tPlayers\n";
    for(int t = TEAM_GROVE; t < TEAM_COUNT; ++t)
    {
        int n = settings.team == t ? 1 : 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i != myId && players[i].active && s_Team[i] == t) ++n;
        char row[160]; snprintf(row, sizeof(row), "{%06X}%s\t{FFFFFF}%d\t%d\n", (unsigned)(g_MPTeams[t].rgba >> 8), g_MPTeams[t].name, MP_TeamCanTakeTurf(t) ? TurfCount(t) : 0, n);
        body += row;
    }
    ShowDialog(3021, DIALOG_STYLE_TABLIST_HEADERS, "{FFFFFF}Teams", body.c_str(), "OK", "", nullptr);
}

// ---- class selection (SA-MP spawn screen) ----------------------------------------------------
struct SampClass { uint8_t team; int16_t skin; };
static const SampClass kClasses[] =
{
    { TEAM_GROVE, 0 }, { TEAM_GROVE, 105 }, { TEAM_GROVE, 106 }, { TEAM_GROVE, 107 },
    { TEAM_BALLAS, 102 }, { TEAM_BALLAS, 103 }, { TEAM_BALLAS, 104 },
    { TEAM_VAGOS, 108 }, { TEAM_VAGOS, 109 }, { TEAM_VAGOS, 110 },
    { TEAM_AZTECAS, 114 }, { TEAM_AZTECAS, 115 }, { TEAM_AZTECAS, 116 },
    { TEAM_RIFA, 173 }, { TEAM_RIFA, 174 }, { TEAM_RIFA, 175 },
    { TEAM_DANANG, 121 }, { TEAM_DANANG, 122 }, { TEAM_DANANG, 123 },
    { TEAM_POLICE, 280 }, { TEAM_POLICE, 281 }, { TEAM_POLICE, 282 },
    { TEAM_NONE, -1 }, { TEAM_NONE, 7 }, { TEAM_NONE, 19 }, { TEAM_NONE, 29 }, { TEAM_NONE, 101 }, { TEAM_NONE, 170 }, { TEAM_NONE, 188 }, { TEAM_NONE, 250 },
};
#define SAMP_CLASS_COUNT ((int)(sizeof(kClasses) / sizeof(kClasses[0])))
struct ClassSel { bool active = false; int idx = 0, shownTeam = -1, prevModel = 0; Vec3 prevPos = { 0, 0, 0 }; float prevH = 0; uint64_t changedAt = 0; };
static ClassSel s_Class;
static uint64_t s_nNextTeamSend = 0;
static bool s_bModelApplied = false;

static void SendTeam()
{
    if(client.GetState() != NET_CONNECTED || !MP::ModeOn(GM_CLASS)) return;   // 3.02
    PktTeam t; memset(&t, 0, sizeof(t));
    t.type = PKT_TEAM; t.id = myId; t.team = (uint8_t)settings.team; t.skin = (int16_t)settings.classSkin;
    client.Send(&t, sizeof(t));
}
static void ClassShow(int idx)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    s_Class.idx = (idx % SAMP_CLASS_COUNT + SAMP_CLASS_COUNT) % SAMP_CLASS_COUNT;
    const SampClass& c = kClasses[s_Class.idx];
    const MPTeamInfo& t = g_MPTeams[c.team];
    if(s_Class.shownTeam != c.team)
    {   // showroom = the team's spawn, camera 4 m in front of the player
        s_Class.shownTeam = c.team;
        Game::FreezeChar(ped, false);
        Game::TeleportGround(ped, { t.sx, t.sy, t.sz }, t.sh);
        Game::FreezeChar(ped, true);
    }
    int model = c.skin < 0 ? s_Class.prevModel : c.skin;
    if(!Game::SetLocalModel(model)) SysMsg(COL_YELLOW, "Skin %d is not in this game.", model);
    Vec3 me; Game::GetCharPos(ped, me);
    float r = Game::GetCharHeading(ped) * 0.0174533f;
    Vec3 cam = { me.x - sinf(r) * 4.0f, me.y + cosf(r) * 4.0f, me.z + 0.6f };
    Game::SetFixedCamera(cam, { me.x, me.y, me.z + 0.2f });
    char txt[160];
    snprintf(txt, sizeof(txt), "%s~n~~w~class %d/%d  -  skin %d", t.name, s_Class.idx + 1, SAMP_CLASS_COUNT, model);
    PktTextDraw d = TdMake(txt, 320.0f, 70.0f, 1.5f, 3, 2, t.rgba | 0xFF);
    TdSet(TD_CLASS, d);
    s_Class.changedAt = MP_NowMs();
}
static void ClassOpen()
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    if(InEvent()) { SysMsg(COL_YELLOW, "Leave the race / derby / job / DM first (/leave)."); return; }
    if(s_bLocalMission) { SysMsg(COL_YELLOW, "Not during a mission."); return; }
    if(Game::IsCharInAnyCar(ped)) { SysMsg(COL_YELLOW, "Get out of the vehicle first."); return; }
    s_Class = ClassSel();
    s_Class.active = true; classSelect = true;
    s_Class.prevModel = Game::LocalModel(); if(s_Class.prevModel < 0) s_Class.prevModel = 0;
    Game::GetCharPos(ped, s_Class.prevPos); s_Class.prevH = Game::GetCharHeading(ped);
    Game::SetPlayerControl(false);
    int start = 0;
    for(int i = 0; i < SAMP_CLASS_COUNT; ++i) if(kClasses[i].team == settings.team && (kClasses[i].skin == settings.classSkin || settings.classSkin < 0)) { start = i; break; }
    ClassShow(start);
}
static void ClassClose(bool spawn)
{
    int ped = Game::LocalPed();
    s_Class.active = false; classSelect = false;
    TdHide(TD_CLASS);
    Game::RestoreCamera();
    if(ped > 0) Game::FreezeChar(ped, false);
    Game::SetPlayerControl(true);
    if(ped <= 0) return;
    if(!spawn)
    {
        Game::SetLocalModel(s_Class.prevModel);
        Game::TeleportGround(ped, s_Class.prevPos, s_Class.prevH);
        return;
    }
    const SampClass& c = kClasses[s_Class.idx];
    const MPTeamInfo& t = g_MPTeams[c.team];
    settings.team = c.team;
    settings.classSkin = c.skin;
    SaveConfig();
    s_bModelApplied = true;
    float a = (float)(rand() % 360) * 0.0174533f;
    Game::TeleportGround(ped, { t.sx + cosf(a) * 2.5f, t.sy + sinf(a) * 2.5f, t.sz }, t.sh);
    SendTeam(); s_nNextTeamSend = MP_NowMs() + 5000;
    if(c.team == TEAM_NONE) Announce(COL_SYSTEM, "(/class) %s is a civilian.", MyName());
    else Announce(t.rgba | 0xFF, "(/class) %s joined the %s!", MyName(), t.name);
    if(MP_TeamCanTakeTurf(c.team)) SysMsg(COL_SYSTEM, "%s hold %d zones. Stand in an enemy zone for 60 s to take it (/turf).", t.name, TurfCount(c.team));
}
static void RenderClassSelect()
{
    if(!s_Class.active) return;
    if(Game::LocalPed() <= 0 || client.GetState() != NET_CONNECTED) { ClassClose(false); return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bh = UI::S(110), bw = UI::S(170), sw = UI::S(300), gap = UI::S(30);
    float total = bw * 2 + sw + gap * 2 + UI::S(200) + gap;
    ImGui::SetNextWindowPos(ImVec2((ds.x - total) * 0.5f, ds.y - bh - UI::S(40)));
    ImGui::SetNextWindowSize(ImVec2(total, bh));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##SampClass", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* f = UI::PricedownFont() ? UI::PricedownFont() : UI::Font();
        const float fs = UI::S(UI::PricedownFont() ? 54.0f : 40.0f);
        struct B { const char* t; float w; int act; } bs[] = { { "<<", bw, -1 }, { "SPAWN", sw, 2 }, { ">>", bw, 1 }, { "CANCEL", UI::S(200), 0 } };
        float x = ImGui::GetCursorScreenPos().x, y = ImGui::GetCursorScreenPos().y;
        for(auto& b : bs)
        {
            ImGui::SetCursorScreenPos(ImVec2(x, y));
            ImGui::PushID(b.t);
            bool hit = ImGui::InvisibleButton("##cb", ImVec2(b.w, bh));
            bool held = ImGui::IsItemActive();
            ImGui::PopID();
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + b.w, y + bh), held ? IM_COL32(169, 196, 228, 150) : IM_COL32(0, 0, 0, 170), UI::S(8));
            float tfs = b.act == 0 ? fs * 0.6f : fs;
            ImVec2 ts = f->CalcTextSizeA(tfs, FLT_MAX, 0, b.t);
            ImU32 col = b.act == 2 ? IM_COL32(226, 192, 99, 255) : IM_COL32_WHITE;
            const float o = fmaxf(1.5f, tfs * 0.05f);
            for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) dl->AddText(f, tfs, ImVec2(x + (b.w - ts.x) * 0.5f + ox * o, y + (bh - ts.y) * 0.5f + oy * o), IM_COL32(0, 0, 0, 255), b.t);
            dl->AddText(f, tfs, ImVec2(x + (b.w - ts.x) * 0.5f, y + (bh - ts.y) * 0.5f), col, b.t);
            if(hit && MP_NowMs() - s_Class.changedAt > 150)
            {
                if(b.act == -1 || b.act == 1) ClassShow(s_Class.idx + b.act);
                else { ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar(2); ClassClose(b.act == 2); return; }
            }
            x += b.w + gap;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---- chat commands ----------------------------------------------------------------------------
static bool SampCommand(const char* cmd, const char* arg)
{
    if(!strcmp(cmd, "class") || !strcmp(cmd, "team") || !strcmp(cmd, "gang") || !strcmp(cmd, "skinselect")) { if(NeedMode(GM_CLASS)) ClassOpen(); return true; }
    if(!strcmp(cmd, "turf") || !strcmp(cmd, "turfs") || !strcmp(cmd, "gangzones")) { if(NeedMode(GM_TURF)) ShowTurfDialog(); return true; }
    if(!strcmp(cmd, "teams") || !strcmp(cmd, "gangs")) { if(NeedMode(GM_TURF)) ShowTeamsDialog(); return true; }
    auto toggle = [](bool& b, const char* what) { b = !b; SaveConfig(); SysMsg(COL_SYSTEM, "%s: %s", what, OnOff(b)); };
    if(!strcmp(cmd, "bubbles") || !strcmp(cmd, "bubble")) { toggle(settings.chatBubbles, "Chat bubbles"); return true; }
    if(!strcmp(cmd, "labels")) { toggle(settings.labels3d, "3D text labels"); return true; }
    if(!strcmp(cmd, "icons") || !strcmp(cmd, "mapicons")) { toggle(settings.mapIcons, "Map icons"); return true; }
    if(!strcmp(cmd, "actors")) { toggle(settings.actors, "Actors"); return true; }
    if(!strcmp(cmd, "zones")) { toggle(settings.gangZones, "Gang zones"); return true; }
    (void)arg;
    return false;
}

// ---- network ----------------------------------------------------------------------------------
static void SampOnTeam(const PktTeam& t) { if(t.id < MP_MAX_PLAYERS) s_Team[t.id] = t.team < TEAM_COUNT ? t.team : 0; }
static void SampOnDialog(const PktDialog& d)
{
    s_bServer301 = true;
    ShowDialog(d.dialogId, d.style <= DIALOG_STYLE_TABLIST_HEADERS ? d.style : DIALOG_STYLE_MSGBOX, d.caption, d.info, d.button1, d.button2, nullptr, 0, true);
}
static void SampOnTextDraw(const PktTextDraw& t)
{
    if(t.tdId >= SAMP_TD_LOCAL) return;
    if(t.show) TdSet(t.tdId, t); else TdHide(t.tdId);
}
static void SampOnPlayerLeave(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return;
    s_Part[id] = SampPart(); s_Team[id] = 0; s_Bubble[id].until = 0;
}
static void SampReset()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) SampOnPlayerLeave(i);
    for(int i = 0; i < SAMP_TD_LOCAL; ++i) TdHide(i);
    if(s_Dlg.open && s_Dlg.fromServer) s_Dlg.open = false;
    s_bServer301 = false; s_nNextTeamSend = 0;
    s_RQ = RaceQueue();   // the new server has its own queue
    TurfInit();
}

// ---- every frame ------------------------------------------------------------------------------
static void ProcessSamp(int ped, uint64_t now)
{
    static bool s_bProv = false;
    if(!s_bProv) { Game::SetZoneProvider(TurfZoneProvider); s_bProv = true; }
    const bool on = Game::IsReady() && client.GetState() == NET_CONNECTED && !JoinFlowActive() && ped > 0;
    UpdateMapIcons(on && settings.mapIcons);
    UpdateActors(on && settings.actors && MP::ModeOn(GM_ACTORS) && Game::GetCharArea(ped) == 0, now);
    if(!on) return;
    if(now >= s_nNextTeamSend) { s_nNextTeamSend = now + 5000; SendTeam(); }
    if(s_nTurfGpsBlip && now > s_nTurfGpsUntil) { Game::RemoveBlip(s_nTurfGpsBlip); s_nTurfGpsBlip = 0; }
    UpdateTurfHud(ped);
    // the class skin: CJ on story missions (cutscenes need him), the class skin again after
    if(!MP::ModeOn(GM_CLASS))
    {   // 3.02: no classes here: back to CJ once if I was in my class skin
        if(s_bModelApplied && settings.classSkin > 0 && !Game::IsCharInAnyCar(ped) && Game::LocalModel() == settings.classSkin) Game::SetLocalModel(0);
        s_bModelApplied = false;
    }
    else if(settings.classSkin > 0 && !s_Class.active && !Game::IsCharInAnyCar(ped) && !Game::IsCharDead(ped))
    {
        static uint64_t s_nNextModelCheck = 0;
        if(now >= s_nNextModelCheck)
        {
            s_nNextModelCheck = now + 3000;
            int m = Game::LocalModel();
            if(s_bLocalMission) { if(m > 0) Game::SetLocalModel(0); s_bModelApplied = false; }
            else if(!s_bModelApplied || m == 0) { if(m != settings.classSkin) Game::SetLocalModel(settings.classSkin); s_bModelApplied = true; }
        }
    }
}
static void RenderSamp()
{
    if(!Game::IsReady() || JoinFlowActive()) { if(s_Dlg.open && !s_Dlg.fromServer) s_Dlg.open = false; return; }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if(client.GetState() == NET_CONNECTED)
    {
        if(!s_Class.active) { RenderLabels(dl); RenderBubbles(dl); }   // class selection: only the skin
    }
    RenderTextDraws(dl);
    RenderClassSelect();
    RenderDialog();
}

// 3.02: the server told us its gamemodes (on join, and when the host changes them)
static void GamemodesChanged(uint16_t modes)
{
    const bool first = !s_bModesSeen;
    const uint16_t old = s_nServerModes;
    s_nServerModes = modes & GM_ALL;
    s_bModesSeen = true;
    if(modes) s_bServer301 = true;   // a 3.02 server
    SrvModesPut(s_szCurServer, client.GetPort(), s_nServerModes);   // 3.03: remember it (if this phone ever hosts it: the same)
    if(MP::IsOnDefault()) return;    // the default server: everything, nothing to say
    if(first)
    {
        if(!s_nServerModes) SysMsg(COL_SYSTEM, "*** No gamemodes on this server (its host turns them on: MP Launcher > Add server > Gamemodes).");
        else SysMsg(COL_SYSTEM, "*** Gamemodes here: %s.", GameModeList(s_nServerModes, ", ", false).c_str());
    }
    else if(old != s_nServerModes)
    {
        uint16_t on = s_nServerModes & ~old, off = old & ~s_nServerModes;
        if(on) SysMsg(COL_SYSTEM, "*** The host turned on: %s.", GameModeList(on, ", ", false).c_str());
        if(off) SysMsg(COL_YELLOW, "*** The host turned off: %s.", GameModeList(off, ", ", false).c_str());
    }
    // leave what was switched off
    if(s_RQ.on && !(s_nServerModes & GM_RACE)) RaceQueueLeave(true);
    if((s_Ev.kind == EV_RACE && !(s_nServerModes & GM_RACE)) || (s_Ev.kind == EV_DERBY && !(s_nServerModes & GM_DERBY))
    || (s_Ev.kind == EV_DM && !(s_nServerModes & GM_DM)) || (s_Ev.kind == EV_ROUTE && !(s_nServerModes & GM_JOBS)))
        EndEvent("The host turned this gamemode off.");
    if(s_Class.active && !(s_nServerModes & GM_CLASS)) ClassClose(false);
}
