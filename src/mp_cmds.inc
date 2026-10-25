// 2.19 (included by mp.cpp): chat commands + cheats, scoreboard (player list), Mission sync
// request popup, chat box with /command suggestions, spawn picker buttons, carjacking players.

// ---- commands -----------------------------------------------------------------------------
struct ChatCmd { const char* cmd; const char* args; const char* help; };
static const ChatCmd kCmds[] =
{
    { "/tp",         ":place",   "teleport, e.g. /tp :grove_street" },
    { "/wallhack",   "",         "name tags through walls, any distance (on foot + in a car)" },
    { "/nofall",     "",         "no fall damage, soft landings (on foot + in a car)" },
    { "/invincible", "",         "no damage at all (on foot + in a car)" },
    { "/freeze",     "",         "freeze in place (on foot)" },
    { "/invisible",  "",         "nobody sees you (on foot)" },
    { "/fly",        "",         "fly where the camera looks, joystick = move (on foot + in a car)" },
    { "/flash",      "",         "run super fast (on foot)" },
    { "/behind",     "[id|stop]", "go behind the nearest player / player id after 3 2 1 (30 s cooldown)" },
    { "/gravity",    "[0.1-3]",  "gravity, 1 = normal (on foot + in a car)" },
    { "/pagesize",   "[0-20]",   "chat lines shown (0 = only while typing)" },
    { "/fontsize",   "[50-150]", "chat text size in %" },
    { "/chatscale",  "[50-200]", "scale the whole chat (lines + chat box) in %" },
    { "/chatwidth",  "[20-90]",  "chat width in % of the screen" },
    { "/chatfade",   "[0-300]",  "seconds before chat lines fade (0 = never)" },
    { "/chatbg",     "",         "chat background on / off" },
    { "/clearchat",  "",         "clear your chat" },
    { "/stats",      "",         "your account (user, Discord, gender, age, logins)" },
    { "/char",       "",         "same as /stats" },
    { "/help",       "",         "list the commands" },
};
#define MP_CMD_COUNT ((int)(sizeof(kCmds) / sizeof(kCmds[0])))

#define s_nMyScore s_nMyScoreFwd
static bool AuthCommand(const char* cmd);   // 3.21.6 (mp_auth.inc)
static uint64_t s_nLastInfo = 0;
static uint64_t s_nCheatLast = 0;
static uint64_t s_nCheatRefresh = 0;

static const char* OnOff(bool b) { return b ? "ON" : "OFF"; }

static int NearestPlayer(const Vec3& me)
{
    int best = -1; float bd = 1e12f;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active || i == myId || !p.hasSync || p.invisible) continue;
        float dx = p.sync.pos[0] - me.x, dy = p.sync.pos[1] - me.y, dz = p.sync.pos[2] - me.z;
        float d = dx * dx + dy * dy + dz * dz;
        if(d < bd) { bd = d; best = i; }
    }
    return best;
}

// ---- 2.28 Go behind: never instant ---------------------------------------------------------
// 2.27 teleported the moment "Go behind" / /behind ran, straight to the other player's last sync
// position: into their car's collision (physics threw you up = "flying"), into another interior's
// coordinates (interiors float high above the map), or to a stale spot of a paused player. Other
// players saw you warp and fly around like a cheater. Now:
//   - tapping a name in the Player list ONLY selects it (nothing else, ever)
//   - Go behind = 3 2 1 countdown on screen, Cancel any time, cancelled if the target becomes invalid
//   - time limit: the target must still be valid when the countdown ends (fresh sync, same interior,
//     not paused, not on a mission / cutscene)
//   - cooldown between uses, so it can't be spammed like a teleport hack
//   - safe landing: ground found, speed zeroed and the ped held still for a moment (no fling)
static int      s_nBehindId = -1;          // countdown target (-1 = none)
static uint64_t s_nBehindStart = 0;        // countdown start (ms)
static uint64_t s_nBehindNextOk = 0;       // cooldown end (ms)
static uint64_t s_nBehindHoldUntil = 0;    // landing hold end (ms)
static const uint64_t kBehindCountMs  = 3000;    // 3 2 1
static const uint64_t kBehindCooldown = 30000;   // time limit between two Go behinds
static const uint64_t kBehindFreshMs  = 3000;    // target's sync must be newer than this
static const uint64_t kBehindHoldMs   = 700;     // landing hold

static bool BehindTargetOk(int id, const char** why)
{
    static const char* dummy; if(!why) why = &dummy;
    *why = nullptr;
    if(id < 0 || id >= MP_MAX_PLAYERS || id == myId || !players[id].active) { *why = "player left"; return false; }
    RemotePlayer& p = players[id];
    if(!p.hasSync) { *why = "no position from them yet"; return false; }
    if(p.paused) { *why = "they are paused"; return false; }
    if(p.status & (STATUS_MISSION | STATUS_CUTSCENE)) { *why = "they are on a mission / in a cutscene"; return false; }
    if(MP_NowMs() - p.lastRecv > kBehindFreshMs) { *why = "their position is too old"; return false; }
    int theirArea = p.hasX2 ? (int)p.interior : 0;
    if(theirArea != Game::GetAreaVisible()) { *why = "they are in another interior"; return false; }
    return true;
}

static void BehindCancel(const char* why)
{
    if(s_nBehindId < 0) return;
    SysMsg(COL_RED, "Go behind cancelled%s%s.", why ? ": " : "", why ? why : "");
    s_nBehindId = -1;
}

static void BehindStart(int id)
{
    uint64_t now = MP_NowMs();
    if(s_nBehindId >= 0) { BehindCancel("tapped again"); return; }
    if(now < s_nBehindNextOk) { SysMsg(COL_RED, "Go behind again in %d s.", (int)((s_nBehindNextOk - now + 999) / 1000)); return; }
    const char* why = nullptr;
    if(!BehindTargetOk(id, &why)) { SysMsg(COL_RED, "Can't go behind %s: %s.", (id >= 0 && id < MP_MAX_PLAYERS) ? players[id].name : "?", why); return; }
    int ped = Game::LocalPed();
    if(Game::IsCharInAnyCar(ped))
    {
        int car = Game::GetCarCharIsIn(ped);
        if(Game::GetDriverOfCar(car) != ped) { SysMsg(COL_RED, "Can't go behind as a passenger."); return; }
    }
    s_nBehindId = id; s_nBehindStart = now;
    SysMsg(COL_SYSTEM, "Going behind %s in 3... (tap Cancel to stop)", players[id].name);
}

static void BehindDo(int id)
{
    int ped = Game::LocalPed();
    RemotePlayer& p = players[id];
    float h = p.sync.heading * 0.0174533f;
    bool theyDrive = (p.sync.flags & SYNC_IN_CAR) != 0;
    bool iDrive = Game::IsCharInAnyCar(ped);
    // far enough to be clear of their ped / car collision (a car is ~5 m long, a bus 12 m)
    float back = theyDrive ? 9.0f : (iDrive ? 7.0f : 2.5f);
    Vec3 at = { p.sync.pos[0] + sinf(h) * back, p.sync.pos[1] - cosf(h) * back, p.sync.pos[2] };
    Game::TeleportGround(ped, at, p.sync.heading);   // loads collision, finds the ground, car or foot
    if(iDrive) { int car = Game::GetCarCharIsIn(ped); if(car > 0) Game::SetMoveSpeed(car, true, { 0, 0, 0 }); }
    else { Game::SetMoveSpeed(ped, false, { 0, 0, 0 }); Game::FreezeChar(ped, true); }
    s_nBehindHoldUntil = MP_NowMs() + kBehindHoldMs;
    s_nBehindNextOk = MP_NowMs() + kBehindCooldown;
    SysMsg(COL_SYSTEM, "Behind %s.", p.name);
}

// every frame (from ProcessCheats)
static void TickBehind(int ped, uint64_t now)
{
    if(s_nBehindHoldUntil)
    {
        if(now >= s_nBehindHoldUntil) { s_nBehindHoldUntil = 0; Game::FreezeChar(ped, false); }
        else if(!Game::IsCharInAnyCar(ped)) Game::SetMoveSpeed(ped, false, { 0, 0, 0 });
    }
    if(s_nBehindId < 0) return;
    const char* why = nullptr;
    if(!IsConnected()) { BehindCancel("disconnected"); return; }
    if(!BehindTargetOk(s_nBehindId, &why)) { BehindCancel(why); return; }
    if(Game::IsCharDead(ped)) { BehindCancel("you died"); return; }
    if(now - s_nBehindStart >= kBehindCountMs) { int id = s_nBehindId; s_nBehindId = -1; BehindDo(id); }
}

// big 3 / 2 / 1 in the middle of the screen + Cancel
static void RenderBehindCountdown()
{
    if(s_nBehindId < 0) return;
    uint64_t age = MP_NowMs() - s_nBehindStart;
    int left = (int)((kBehindCountMs - (age < kBehindCountMs ? age : kBehindCountMs) + 999) / 1000);
    if(left < 1) left = 1;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    char num[4]; snprintf(num, sizeof(num), "%d", left);
    float frac = (float)(age % 1000) / 1000.0f;            // each digit pops in and shrinks
    float scale = 7.0f - 2.0f * frac;
    ImFont* f = ImGui::GetFont();
    float fs = ImGui::GetFontSize() * scale;
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, num);
    ImVec2 c(ds.x * 0.5f, ds.y * 0.40f);
    fg->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f + UI::S(4), c.y - ts.y * 0.5f + UI::S(4)), IM_COL32(0, 0, 0, 200), num);
    fg->AddText(f, fs, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), IM_COL32(255, 210, 60, (int)(255 * (1.0f - 0.4f * frac))), num);
    const float w = UI::S(560);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, c.y + ts.y * 0.5f + UI::S(10)));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.72f));
    if(ImGui::Begin("##MPBehindCd", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::Text("Going behind %s", players[s_nBehindId].name);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if(ImGui::Button("Cancel##bcd", ImVec2(-1, UI::S(66)))) BehindCancel(nullptr);
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

static void CmdBehind(const char* arg)
{
    if(!Game::IsReady()) return;
    while(arg && *arg == ' ') ++arg;
    if(arg && (!strcasecmp(arg, "stop") || !strcasecmp(arg, "cancel"))) { if(s_nBehindId >= 0) BehindCancel(nullptr); return; }
    int ped = Game::LocalPed();
    Vec3 me; Game::GetCharPos(ped, me);
    int id = -1;
    if(arg && *arg) { id = atoi(arg); if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active || id == myId) id = -1; }
    else id = NearestPlayer(me);
    if(id < 0) { SysMsg(COL_RED, "No player to go behind%s.", (arg && *arg) ? " (wrong id)" : ""); return; }
    BehindStart(id);
}

static void CmdTp(const char* arg)
{
    while(arg && *arg == ' ') ++arg;
    if(!arg || !*arg)
    {
        std::string all;
        for(int i = 0; i < MP_SPAWN_COUNT; ++i) { if(i) all += ", "; all += ":"; all += kSpawns[i].tag; }
        SysMsg(COL_SYSTEM, "Usage: /tp :place  -  %s", all.c_str());
        return;
    }
    if(arg[0] != ':' && isdigit((unsigned char)arg[0]))   // /tp <id> = go to that player
    {
        int id = atoi(arg);
        if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active && players[id].hasSync && id != myId)
        {
            Vec3 at = { players[id].sync.pos[0] + 1.5f, players[id].sync.pos[1], players[id].sync.pos[2] };
            Game::Teleport(Game::LocalPed(), at, players[id].sync.heading);
            SysMsg(COL_SYSTEM, "Teleported to %s.", players[id].name);
        }
        else SysMsg(COL_RED, "No player with id %d.", id);
        return;
    }
    int k = FindSpawnTag(arg);
    if(k < 0) { SysMsg(COL_RED, "Unknown place \"%s\". Type /tp to list them.", arg); return; }
    const SpawnSpot& s = kSpawns[k];
    int ped = Game::LocalPed();
    Vec3 at = { s.x, s.y, s.z };
    if(Game::IsCharInAnyCar(ped) && Game::GetDriverOfCar(Game::GetCarCharIsIn(ped)) == ped)
    {   // take the car along
        int car = Game::GetCarCharIsIn(ped);
        Game::PreloadArea(at, true);
        Game::SetCarPos(car, { s.x, s.y, s.z + 1.0f }, 0.0f);
        Game::SetCarHeading(car, s.heading);
        Game::SetMoveSpeed(car, true, { 0, 0, 0 });
    }
    else
    {
        Game::Teleport(ped, at, s.heading);
        if(s.findGround) Game::PlaceChar(ped, { s.x, s.y, -100.0f });
    }
    SysMsg(COL_SYSTEM, "Teleported to %s.", s.name);
}

// true = it was a command (handled here, not sent as chat)
static bool GamesCommand(const char* cmd, const char* arg);   // 2.21 mp_games.inc
static void GamesHelp();
static bool HandleCommand(const char* text)
{
    if(!text || text[0] != '/') return false;
    char cmd[32] = ""; const char* arg = text + 1;
    int n = 0; while(*arg && *arg != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*arg++);
    cmd[n] = 0;
    while(*arg == ' ') ++arg;
    if(GatesCommand(cmd, arg)) return true;   // 3.04.2: /gatepos, /gates
    if(Mw3Command(cmd, arg)) return true;   // 3.23: MW3 commands (others blocked)
    if(AuthCommand(cmd)) return true;       // 3.21.6: /stats  /char (also on Brief Case War)
    if(BcwBlockCommand(cmd)) return true;   // 3.04: Brief Case War: no gamemodes / cheats / teleports
    if(GamesCommand(cmd, arg)) return true;
    // 2.35: the host disabled cheats
    static const char* const kCheatCmds[] = { "tp", "wallhack", "wh", "nofall", "invincible", "god", "freeze", "invisible", "invis", "fly", "flash", "behind", "gravity" };
    if(MP::CheatsBlocked())
        for(const char* c : kCheatCmds) if(!strcmp(cmd, c)) { SysMsg(COL_RED, "Cheats are disabled on this server by the host."); return true; }
    if(!strcmp(cmd, "tp"))              CmdTp(arg);
    else if(!strcmp(cmd, "wallhack") || !strcmp(cmd, "wh")) { s_Cheat.wallhack = !s_Cheat.wallhack; SysMsg(COL_SYSTEM, "Wall hack: %s", OnOff(s_Cheat.wallhack)); }
    else if(!strcmp(cmd, "nofall"))     { s_Cheat.nofall = !s_Cheat.nofall; s_Cheat.applied = false; SysMsg(COL_SYSTEM, "No fall: %s", OnOff(s_Cheat.nofall)); }
    else if(!strcmp(cmd, "invincible") || !strcmp(cmd, "god")) { s_Cheat.invincible = !s_Cheat.invincible; s_Cheat.applied = false; SysMsg(COL_SYSTEM, "Invincible: %s", OnOff(s_Cheat.invincible)); }
    else if(!strcmp(cmd, "freeze"))     { s_Cheat.freeze = !s_Cheat.freeze; SysMsg(COL_SYSTEM, "Freeze: %s", OnOff(s_Cheat.freeze)); }
    else if(!strcmp(cmd, "invisible") || !strcmp(cmd, "invis")) { s_Cheat.invisible = !s_Cheat.invisible; SysMsg(COL_SYSTEM, "Invisible: %s", OnOff(s_Cheat.invisible)); }
    else if(!strcmp(cmd, "fly"))        { s_Cheat.fly = !s_Cheat.fly; SysMsg(COL_SYSTEM, "Fly hack: %s%s", OnOff(s_Cheat.fly), s_Cheat.fly ? "  (joystick = fly where the camera looks)" : ""); }
    else if(!strcmp(cmd, "flash"))      { s_Cheat.flash = !s_Cheat.flash; SysMsg(COL_SYSTEM, "Flash: %s", OnOff(s_Cheat.flash)); }
    else if(!strcmp(cmd, "behind"))     CmdBehind(arg);
    // 2.19.6: chat layout
    else if(!strcmp(cmd, "pagesize"))  { if(*arg) settings.chatLines = atoi(arg); ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat lines: %d", settings.chatLines); }
    else if(!strcmp(cmd, "chatscale"))  { if(*arg) settings.chatScale = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat scale: %d%%", (int)(settings.chatScale * 100 + 0.5f)); }
    else if(!strcmp(cmd, "fontsize"))  { if(*arg) settings.chatSize = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat text size: %d%%", (int)(settings.chatSize * 100 + 0.5f)); }
    else if(!strcmp(cmd, "chatwidth")) { if(*arg) settings.chatWidth = (float)atof(arg) / 100.0f; ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat width: %d%%", (int)(settings.chatWidth * 100 + 0.5f)); }
    else if(!strcmp(cmd, "chatfade"))  { if(*arg) settings.chatFade = atoi(arg); ClampChatSettings(); SaveConfig(); SysMsg(COL_SYSTEM, "Chat fade: %s", settings.chatFade ? (std::to_string(settings.chatFade) + " s").c_str() : "never"); }
    else if(!strcmp(cmd, "chatbg"))    { settings.chatBg = !settings.chatBg; SaveConfig(); SysMsg(COL_SYSTEM, "Chat background: %s", OnOff(settings.chatBg)); }
    else if(!strcmp(cmd, "clearchat")) { chat.clear(); }
    else if(!strcmp(cmd, "gravity"))
    {
        float g = *arg ? (float)atof(arg) : (s_Cheat.gravity != 1.0f ? 1.0f : 0.3f);
        if(g < 0.1f) g = 0.1f; if(g > 3.0f) g = 3.0f;
        s_Cheat.gravity = g;
        SysMsg(COL_SYSTEM, "Gravity: %.2f%s", g, g == 1.0f ? " (normal)" : "");
    }
    else if(!strcmp(cmd, "help") || !strcmp(cmd, "cmds") || !strcmp(cmd, "commands"))
    {
        for(int i = 0; i < MP_CMD_COUNT; ++i) SysMsg(COL_SYSTEM, "%s %s  -  %s", kCmds[i].cmd, kCmds[i].args, kCmds[i].help);
        GamesHelp();
    }
    else SysMsg(COL_RED, "Unknown command /%s. Type /help.", cmd);
    return true;
}

// ---- cheats, every game tick --------------------------------------------------------------
static void ProcessCheats(int ped, uint64_t now)
{
    if(MP::CheatsBlocked())
    {   // 2.35: everything off (the code below takes the proofs / freeze / invisible off again)
        s_Cheat.wallhack = s_Cheat.nofall = s_Cheat.invincible = s_Cheat.freeze = s_Cheat.invisible = false;
        s_Cheat.fly = s_Cheat.flash = false; s_Cheat.gravity = 1.0f;
    }
    TickBehind(ped, now);   // 2.28: Go behind countdown / landing hold
    float dt = s_nCheatLast ? (now - s_nCheatLast) / 1000.0f : 0.0f;
    s_nCheatLast = now;
    if(dt > 0.1f) dt = 0.1f;
    if(ped <= 0) return;
    bool inCar = Game::IsCharInAnyCar(ped);
    int car = inCar ? Game::GetCarCharIsIn(ped) : 0;
    bool refresh = now - s_nCheatRefresh > 1000;   // missions / the game reset these sometimes
    if(refresh) s_nCheatRefresh = now;

    // proofs: invincible = everything, nofall = collision / fall damage
    bool want = s_Cheat.invincible || s_Cheat.nofall;
    if(want && (!s_Cheat.applied || refresh))
    {
        bool all = s_Cheat.invincible;
        Game::SetCharProofs(ped, all, all, all, true, all);
        if(car) { Game::SetCarProofs(car, all, all, all, true, all); s_Cheat.provedCar = car; }
        s_Cheat.applied = true;
    }
    else if(!want && s_Cheat.applied)
    {
        Game::SetCharProofs(ped, false, false, false, false, false);
        if(s_Cheat.provedCar) Game::SetCarProofs(s_Cheat.provedCar, false, false, false, false, false);
        s_Cheat.provedCar = 0; s_Cheat.applied = false;
    }
    if(s_Cheat.provedCar && s_Cheat.provedCar != car && Game::CarPtr(s_Cheat.provedCar))
    { Game::SetCarProofs(s_Cheat.provedCar, false, false, false, false, false); s_Cheat.provedCar = car; if(car && want) Game::SetCarProofs(car, s_Cheat.invincible, s_Cheat.invincible, s_Cheat.invincible, true, s_Cheat.invincible); }
    if(s_Cheat.invincible && refresh)
    {
        if(Game::GetCharHealth(ped) < 100) Game::SetCharHealth(ped, 100);
        if(car && Game::GetCarHealth(car) < 1000) Game::SetCarHealth(car, 1000);
    }

    // freeze (on foot)
    bool fz = s_Cheat.freeze && !inCar && !JoinFlowActive();
    if(fz && (s_Cheat.frozenPed != ped || refresh)) { Game::FreezeChar(ped, true); s_Cheat.frozenPed = ped; }
    else if(!fz && s_Cheat.frozenPed && !JoinFlowActive()) { Game::FreezeChar(s_Cheat.frozenPed, false); s_Cheat.frozenPed = 0; }

    // invisible (on foot)
    bool inv = s_Cheat.invisible && !inCar && !JoinFlowActive();
    if(inv && (s_Cheat.hiddenPed != ped || refresh)) { Game::SetCharVisible(ped, false); s_Cheat.hiddenPed = ped; }
    else if(!inv && s_Cheat.hiddenPed && !JoinFlowActive()) { Game::SetCharVisible(s_Cheat.hiddenPed, true); s_Cheat.hiddenPed = 0; }

    if(JoinFlowActive() || dt <= 0) return;
    int h = car ? car : ped; bool isCar = car != 0;
    if(isCar && Game::GetDriverOfCar(car) != ped) return;   // passengers don't steer
    Vec3 v; if(!Game::GetMoveSpeed(h, isCar, v)) return;

    if(s_Cheat.fly)
    {
        Vec3 cp, cl; int sx = 0, sy = 0; Game::GetStick(sx, sy);
        if(Game::GetCameraPos(cp, cl))
        {
            float fx = cl.x - cp.x, fy = cl.y - cp.y, fzz = cl.z - cp.z;
            float l = sqrtf(fx * fx + fy * fy + fzz * fzz); if(l < 0.001f) l = 1;
            fx /= l; fy /= l; fzz /= l;
            float rx = fy, ry = -fx;   // right of the camera (flat)
            float fwd = -sy / 128.0f, side = sx / 128.0f;
            if(isCar && fabsf(fwd) < 0.1f && fabsf(side) < 0.1f)
            {   // cars: keep the car's own speed direction, just no falling
                v.z = 0; v.x *= 0.995f; v.y *= 0.995f;
                if(sqrtf(v.x * v.x + v.y * v.y) > 2.0f) { float sp = sqrtf(v.x * v.x + v.y * v.y); v = { fx * sp, fy * sp, fzz * sp }; }
            }
            else
            {
                float sp = isCar ? 45.0f : 16.0f;
                v = { (fx * fwd + rx * side) * sp, (fy * fwd + ry * side) * sp, fzz * fwd * sp };
            }
            Game::SetMoveSpeed(h, isCar, v);
            if(!isCar && (fabsf(fwd) > 0.1f || fabsf(side) > 0.1f)) Game::SetCharHeading(ped, atan2f(-fx, fy) * 57.2958f);
        }
        return;
    }
    bool changed = false;
    if(s_Cheat.flash && !isCar)
    {
        float hs = sqrtf(v.x * v.x + v.y * v.y);
        if(hs > 1.5f && hs < 30.0f) { float k = 26.0f / hs; v.x *= k; v.y *= k; changed = true; }
    }
    if(s_Cheat.gravity != 1.0f)
    {
        v.z += (1.0f - s_Cheat.gravity) * 20.0f * dt;   // game gravity ~ 20 m/s^2 (0.008 per step)
        changed = true;
    }
    if(s_Cheat.nofall && v.z < -14.0f) { v.z = -14.0f; changed = true; }   // soft landing
    if(changed) Game::SetMoveSpeed(h, isCar, v);
}

// ---- ping + score for the scoreboard ---------------------------------------------------------
static void SendPlayerInfo(uint64_t now)
{
    if(now - s_nLastInfo < 3000) return;
    s_nLastInfo = now;
    PktPlayerInfo pi; pi.type = PKT_PLAYER_INFO; pi.id = myId; pi.ping = (uint16_t)MP::DisplayPing(); pi.score = (int16_t)s_nMyScore;
    client.Send(&pi, sizeof(pi));
    if(myId < MP_MAX_PLAYERS) { players[myId].ping = MP::DisplayPing(); players[myId].score = s_nMyScore; }
}

// name tags: hidden behind walls (unless /wallhack), checked a few times a second
static void UpdateTagOcclusion(int id, uint64_t now)
{
    RemotePlayer& p = players[id];
    if(!p.drawValid || !myDrawValid) { p.tagBlocked = false; return; }
    if(now - p.losAt < 250) return;
    p.losAt = now;
    Vec3 cp, cl;
    Vec3 from = Game::GetCameraPos(cp, cl) ? cp : myDrawPos;
    p.tagBlocked = !Game::LineOfSightClear(from, p.drawPos);
}

// ---- carjacking players ------------------------------------------------------------------
// "enter car" at a player's car: driver door = pull them out (if the server is 2.19), passenger = ride
static uint64_t s_nJackSent = 0;
static void TryJack(int ped, int id, uint64_t now)
{
    if(!(s_nServerPerms & PERM_V219) || now - s_nJackSent < 2500) return;
    s_nJackSent = now;
    PktJack j; j.type = PKT_JACK; j.from = myId; j.victim = (uint8_t)id; j.seat = 0;
    client.Send(&j, sizeof(j));
    players[id].jackUntil = now + 4000;
}

// ---- Mission sync request popup ----------------------------------------------------------
static int      s_nPairPopup = -1;
static uint64_t s_nPairPopupMs = 0;
#define PAIR_POPUP_MS 30000

static void ShowPairPopup(int from) { s_nPairPopup = from; s_nPairPopupMs = MP_NowMs(); }

static void RenderPairPopup()
{
    if(s_nPairPopup < 0) return;
    int id = s_nPairPopup;
    if(id >= MP_MAX_PLAYERS || !players[id].active || !IsConnected() || IsPartner(id)) { s_nPairPopup = -1; return; }
    uint64_t age = MP_NowMs() - s_nPairPopupMs;
    if(age > PAIR_POPUP_MS) { SendPair(PAIR_DECLINE, (uint8_t)id); s_bPairAsked[id] = false; s_nPairPopup = -1; return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float w = UI::S(660);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, ds.y * 0.17f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.72f));
    if(ImGui::Begin("##MPPairPopup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::TextUnformatted("Mission sync request");
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(players[id].color | 0xFF)), "%s", players[id].name);
        ImGui::SameLine(0, UI::S(8));
        ImGui::TextWrapped("wants to Mission sync with you: you see each other on missions and share the mission.");
        ImGui::TextDisabled("Closes in %d s", (int)((PAIR_POPUP_MS - age) / 1000));
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if(ImGui::Button("Accept##pp", ImVec2(half, UI::S(70))))
        {
            SendPair(PAIR_ACCEPT, (uint8_t)id);
            s_bPairAsked[id] = false; s_nPairPopup = -1;
            SysMsg(COL_SYSTEM, "*** Mission sync with %s...", players[id].name);
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if(ImGui::Button("Decline##pp", ImVec2(half, UI::S(70))))
        {
            SendPair(PAIR_DECLINE, (uint8_t)id);
            s_bPairAsked[id] = false; s_nPairPopup = -1;
            SysMsg(COL_SYSTEM, "*** You declined Mission sync with %s.", players[id].name);
        }
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

// ---- player list (SA-MP scoreboard look) --------------------------------------------------
static int s_nListSel = -1;
static uint64_t s_nListSelMs = 0;   // 2.28: when the row was picked (arms Go behind a moment later)
static void MissionSyncButton(int i, float w)
{
    RemotePlayer& p = players[i];
    const char* lbl; uint8_t act = 0; ImVec4 c(1, 1, 1, 0.94f);
    // 2.31: Mission sync is a group of up to 16. "Unsync" takes only THIS player out of your group.
    bool full = GroupCount() + 1 >= MP_MISSION_GROUP_MAX;
    if(IsPartner(i))             { lbl = "Unsync mission##ms"; c = ImVec4(0.85f, 0.25f, 0.25f, 0.95f); act = PAIR_END; }
    else if(s_bPairAsked[i])     { lbl = "Accept Mission sync##ms"; act = PAIR_ACCEPT; }
    else if(full)                { lbl = "Group full (16)##ms"; c = ImVec4(0.5f, 0.5f, 0.5f, 0.9f); }
    else if(i == s_nPairAskedTo) { lbl = "Asked... (waiting)##ms"; c = ImVec4(0.95f, 0.80f, 0.35f, 0.95f); }
    else                         { lbl = "Mission sync##ms"; act = PAIR_REQUEST; }
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    if(ImGui::Button(lbl, ImVec2(w, UI::S(68))) && act)
    {
        SendPair(act, (uint8_t)i);
        if(act == PAIR_REQUEST) { s_nPairAskedTo = (uint8_t)i; SysMsg(COL_SYSTEM, "*** Asked %s for Mission sync. Waiting for them to accept...", p.name); }
        if(act == PAIR_ACCEPT) { s_bPairAsked[i] = false; if(s_nPairPopup == i) s_nPairPopup = -1; }
        if(act == PAIR_END)
        {
            if(GroupCount() <= 1) { memset(s_bGroup, 0, sizeof(s_bGroup)); }   // the last other one: the group ends
            else s_bGroup[i] = false;
            GroupFixFirst();
            SysMsg(COL_SYSTEM, "*** %s is no longer in your Mission sync.", p.name);
        }
    }
    ImGui::PopStyleColor();
}

static void PagePlayers(void* openPtr)
{
    bool* open = (bool*)openPtr;
    if(Mw3OnServer()) { Mw3OpenList(); if(open) *open = false; return; }   // 3.23: the MW3 player list
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Player list##MPPlayers", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    // header: server name ........ Players: N
    {
        const char* sn = IsConnected() ? (client.GetServerName()[0] ? client.GetServerName() : s_szCurServer) : "Not connected";
        ImGui::TextUnformatted(sn);
        char pl[48]; snprintf(pl, sizeof(pl), "Players: %d", IsConnected() ? MP::OnlineCount() : 0);
        ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(pl).x);
        ImGui::TextUnformatted(pl);
    }
    ImGui::Separator();
    float bottom = UI::S(236);
    ImGui::BeginChild("##sb", ImVec2(0, -bottom), false);
    const float rowH = UI::S(54);
    float W = ImGui::GetContentRegionAvail().x;
    float cId = 0, cName = W * 0.12f, cScore = W * 0.66f, cPing = W * 0.83f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    {   // column titles
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImU32 tc = IM_COL32(200, 200, 200, 255);
        dl->AddText(ImVec2(p.x + cId + UI::S(10), p.y), tc, "id");
        dl->AddText(ImVec2(p.x + cName, p.y), tc, "name");
        dl->AddText(ImVec2(p.x + cScore, p.y), tc, "score");
        dl->AddText(ImVec2(p.x + cPing, p.y), tc, "ping");
        ImGui::Dummy(ImVec2(W, ImGui::GetTextLineHeight() + UI::S(6)));
    }
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active) continue;
        ++n;
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##row", ImVec2(W, rowH))) { s_nListSel = (s_nListSel == i) ? -1 : i; s_nListSelMs = MP_NowMs(); }   // 2.28: select only
        bool sel = s_nListSel == i;
        if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(190, 25, 25, 215), UI::S(8));
        else if(n % 2 == 0) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 12), UI::S(8));
        float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        char buf[64];
        snprintf(buf, sizeof(buf), "%d", i);
        dl->AddText(ImVec2(a.x + cId + UI::S(10), ty), IM_COL32_WHITE, buf);
        std::string nm = p.name; if(i == myId) nm += " (you)"; if(IsPartner(i)) nm += "  [sync]"; if(p.paused) nm += "  (paused)";
        ImU32 nc = UI::FromRGBA((i == myId ? myColor : p.color) | 0xFF);
        if(sel) nc = IM_COL32_WHITE;
        UI::NameText(dl, ImGui::GetFontSize(), ImVec2(a.x + cName, ty), nc, nm.c_str(), 1.0f, false);   // 2.37: [ADMIN] SENJI animated
        {   // 3.11: voice icon left of the score: speaker = talking now, speaker + red X = muted by you
            ImVec2 vc(a.x + cScore - UI::S(34), a.y + rowH * 0.5f);
            if(i != myId && MP::VoiceMuted(i)) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32(230, 230, 230, 255), 0, true);
            else if(i == myId ? MP::MeTalking() : MP::VoiceSpeaking(i)) UI::DrawSpeaker(dl, vc, UI::S(13), IM_COL32_WHITE, 2, false);
        }
        snprintf(buf, sizeof(buf), "%d", i == myId ? s_nMyScore : p.score);
        dl->AddText(ImVec2(a.x + cScore, ty), IM_COL32_WHITE, buf);
        int ping = i == myId ? MP::DisplayPing() : p.ping;
        if(ping >= 0) snprintf(buf, sizeof(buf), "%d", ping); else snprintf(buf, sizeof(buf), "-");
        dl->AddText(ImVec2(a.x + cPing, ty), IM_COL32_WHITE, buf);
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("Not connected. Server > Connect, or join a custom server.");
    ImGui::EndChild();
    ImGui::Separator();
    // selected player: status + Mission sync / behind
    int i = s_nListSel;
    if(i >= 0 && i < MP_MAX_PLAYERS && players[i].active && i != myId)
    {
        RemotePlayer& p = players[i];
        if(p.status & STATUS_MISSION) ImGui::TextColored(kYellowTxt, "%s: on mission %s", p.name, MP::MissionTitle(p.mission));
        else if(p.status & STATUS_CUTSCENE) ImGui::TextColored(kYellowTxt, "%s: in a cutscene", p.name);
        else if(p.hasSync) ImGui::Text("%s  |  HP %d  |  armour %d", p.name, p.sync.health, p.sync.armour);
        else ImGui::Text("%s", p.name);
        float w3 = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if((s_nServerPerms & PERM_MISSION_PAIR) && IsConnected()) MissionSyncButton(i, w3);
        else { ImGui::BeginDisabled(); ImGui::Button("Mission sync is off on this server##ms0", ImVec2(w3, UI::S(68))); ImGui::EndDisabled(); }
        ImGui::SameLine();
        // 2.28: the button only works a moment after the row was picked (a tap on a name can never
        // land on it), starts the 3 2 1 countdown instead of teleporting, and shows the cooldown.
        uint64_t nowMs = MP_NowMs();
        bool armed = nowMs - s_nListSelMs > 600;
        char bl[48];
        if(s_nBehindId == i) snprintf(bl, sizeof(bl), "Cancel##bh");
        else if(nowMs < s_nBehindNextOk) snprintf(bl, sizeof(bl), "Go behind (%d s)##bh", (int)((s_nBehindNextOk - nowMs + 999) / 1000));
        else snprintf(bl, sizeof(bl), "Go behind (3 2 1)##bh");
        if(!armed) ImGui::BeginDisabled();
        if(ImGui::Button(bl, ImVec2(w3, UI::S(68))) && armed) { if(s_nBehindId == i) BehindCancel(nullptr); else BehindStart(i); }
        if(!armed) ImGui::EndDisabled();
        {   // 3.11: mute / unmute this player's voice (only for you), red X icon while muted
            bool muted = MP::VoiceMuted(i);
            ImGui::PushStyleColor(ImGuiCol_Button, muted ? ImVec4(0.70f, 0.12f, 0.12f, 0.95f) : ImVec4(0.18f, 0.18f, 0.18f, 0.95f));
            ImVec2 bp = ImGui::GetCursorScreenPos();
            float bw = ImGui::GetContentRegionAvail().x, bh = UI::S(64);
            if(ImGui::Button(muted ? "      Unmute voice##vm" : "      Mute voice##vm", ImVec2(bw, bh))) VoiceToggleMute(i);
            ImGui::PopStyleColor();
            ImVec2 tsz = ImGui::CalcTextSize(muted ? "      Unmute voice" : "      Mute voice");
            ImVec2 ic(bp.x + (bw - tsz.x) * 0.5f + UI::S(20), bp.y + bh * 0.5f);
            UI::DrawSpeaker(ImGui::GetWindowDrawList(), ic, UI::S(15), IM_COL32_WHITE, muted ? 0 : 2, muted);
        }
    }
    else if(IsConnected())
    {
        if(AnyGroupActive())
        {   // 2.31: the whole group + leave it
            std::string g;
            for(int k = 0; k < MP_MAX_PLAYERS; ++k) if(IsPartner(k) && players[k].active) { if(!g.empty()) g += ", "; g += players[k].name; }
            ImGui::TextColored(kGreenTxt, "Mission sync (%d/%d): you, %s", GroupCount() + 1, MP_MISSION_GROUP_MAX, g.c_str());
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
            if(ImGui::Button("Leave Mission sync##lg", ImVec2(-1, UI::S(60))))
            {
                SendPair(PAIR_END, myId);
                ResetPairs();
                SysMsg(COL_SYSTEM, "*** You left Mission sync.");
            }
            ImGui::PopStyleColor();
        }
        else ImGui::TextDisabled("Tap a player, then Mission sync (up to 16 together). They get Accept / Decline.");
    }
    ImGui::End();
}

static void PageHelp(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(ds.x * 0.50f, ds.y * 0.72f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Help##MPHelp", open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) { ImGui::End(); return; }
    ImGui::TextUnformatted("MP 2.37 - multiplayer for GTA SA");
    ImGui::Separator();
    ImGui::TextWrapped("Top bar: the dots open Help, Server and Player list. Chat opens the chat box (type / for the commands). Drag the bar to move it.");
    ImGui::TextWrapped("Server: connect to the default server, join or add custom servers (with an optional password, max 16 characters), host your own.");
    ImGui::TextWrapped("Player list: tap a player, then Mission sync. They get a popup with Accept / Decline. Synced players see each other on every mission (also in mission scenes) and share markers.");
    ImGui::Separator();
    ImGui::TextUnformatted("Chat commands");
    for(int k = 0; k < MP_CMD_COUNT; ++k)
    {
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "%s %s", kCmds[k].cmd, kCmds[k].args);
        ImGui::SameLine(0, UI::S(12));
        ImGui::TextWrapped("%s", kCmds[k].help);
    }
    ImGui::Separator();
    ImGui::TextUnformatted("Games (2.21)");
    ImGui::TextWrapped("/spawn [id|name] (or /v) any vehicle 400-611, /w [id|name|all] weapons, /fix, /flip, /heal, /armour");
    ImGui::TextWrapped("/race 1-8 races with checkpoints and times, /job trucking|quarry|pizza|burger|cluckin|taxi|medic|firetruck, /dm war|grove|airport|minigun|sniper, /leave, /scores");
    ImGui::TextWrapped("Teleports everybody sees: /ls /sf /lv /chiliad /lsairport /sfairport /lvairport /desert /beach /strip /dam /area51 /pizza /burger /cluckin /barber /tattoo /gym /gymsf /gymlv /clothes /prolaps /victim /ammu /spray /transfender /locolow /waa /school /flightschool /boatschool /truck /quarry /emmet /home /gf /casino");
    ImGui::TextWrapped("Open world: /unlock (all cities, shops, food, gyms, barbers, tattoo, clothes, Ammu-Nation, Pay 'n' Spray, mod garages, schools, safe houses), /girlfriends");
    ImGui::Separator();
    ImGui::TextUnformatted("Places for /tp :place");
    std::string all;
    for(int k = 0; k < MP_SPAWN_COUNT; ++k) { if(k) all += "  "; all += ":"; all += kSpawns[k].tag; }
    ImGui::TextWrapped("%s", all.c_str());
    ImGui::End();
}

// ---- chat box (only while the Chat pill is on) ------------------------------------------------
static bool s_bChatBox = false;
bool  MP::IsChatBoxOpen() { return s_bChatBox; }
float MP::chatDrawnH = 0;
void MP::ClampChatSettings()
{
    if(settings.chatLines < 0) settings.chatLines = 0;
    if(settings.chatLines > 20) settings.chatLines = 20;
    if(!(settings.chatSize >= 0.5f && settings.chatSize <= 1.5f)) settings.chatSize = 0.80f;
    if(!(settings.chatScale >= 0.5f && settings.chatScale <= 2.0f)) settings.chatScale = 1.00f;
    if(!(settings.chatWidth >= 0.2f && settings.chatWidth <= 0.9f)) settings.chatWidth = 0.42f;
    if(settings.chatFade < 0) settings.chatFade = 0;
    if(settings.chatFade > 300) settings.chatFade = 300;
}
static void OnChatBoxSubmit(const char* t, void*) { MP::SendChat(t); }
static void OpenChatKeyboard(const char* initial)
{
    VKeyboard::Open("Say", initial ? initial : "", MP_CHAT_LEN, OnChatBoxSubmit);
    VKeyboard::SetChat(true);
}

struct Suggest { std::string text, hint; };
static void GamesSuggest(const char* t, std::vector<Suggest>& out);   // 2.21
static void BuildSuggestions(const char* t, std::vector<Suggest>& out)
{
    out.clear();
    if(!t || t[0] != '/') return;
    const char* sp = strchr(t, ' ');
    if(!sp)
    {   // command names
        size_t n = strlen(t);
        for(int k = 0; k < MP_CMD_COUNT; ++k)
            if(!strncasecmp(kCmds[k].cmd, t, n)) out.push_back({ std::string(kCmds[k].cmd) + (kCmds[k].args[0] ? " " : ""), kCmds[k].args[0] ? std::string(kCmds[k].args) + "  -  " + kCmds[k].help : kCmds[k].help });
        GamesSuggest(t, out);
        return;
    }
    if(!strncasecmp(t, "/tp ", 4))
    {   // places
        const char* a = t + 4; while(*a == ' ') ++a;
        if(*a == ':') ++a;
        size_t n = strlen(a);
        for(int k = 0; k < MP_SPAWN_COUNT && out.size() < 12; ++k)
            if(!strncasecmp(kSpawns[k].tag, a, n)) out.push_back({ std::string("/tp :") + kSpawns[k].tag, kSpawns[k].name });
    }
    else if(!strncasecmp(t, "/spawn ", 7) || !strncasecmp(t, "/v ", 3) || !strncasecmp(t, "/w ", 3) || !strncasecmp(t, "/race ", 6) || !strncasecmp(t, "/dm ", 4) || !strncasecmp(t, "/job ", 5) || !strncasecmp(t, "/goto ", 6))
        GamesSuggest(t, out);
    else if(!strncasecmp(t, "/behind ", 8))
    {
        for(int i = 0; i < MP_MAX_PLAYERS && out.size() < 12; ++i)
            if(players[i].active && i != myId) out.push_back({ "/behind " + std::to_string(i), players[i].name });
    }
    else if(!strncasecmp(t, "/gravity ", 9))
    {
        static const char* g[] = { "0.1", "0.3", "0.5", "1", "2", "3" };
        for(auto v : g) out.push_back({ std::string("/gravity ") + v, !strcmp(v, "1") ? "normal" : "" });
    }
}

static void RenderChatBox()
{
    if(!s_bChatBox) return;
    if(FloatMenu::IsModal()) return;   // 2.26: hidden while the launcher is open
    if(!VKeyboard::IsOpen()) VKeyboard::SetChat(false);
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float cs = settings.chatScale;   // 3.04.5: chat scale
    auto CS = [cs](float v) { return UI::S(v) * cs; };
    const float fs = CS(27.0f);
    float x = ds.x * settings.chatX + CS(4);
    float y = ds.y * settings.chatY + chatDrawnH + CS(10);   // 2.19.6: under the lines really drawn
    if(settings.chatBoxX >= 0 && settings.chatBoxY >= 0)          // 2.22: own position (Server > Display)
    {
        x = ds.x * ImClamp(settings.chatBoxX, 0.0f, 0.9f) + CS(4);
        y = ds.y * ImClamp(settings.chatBoxY, 0.0f, 0.9f);
    }
    float w = ImMin(ImMax(ds.x * settings.chatWidth, ds.x * 0.34f), ds.x - x - CS(10));
    float kb = VKeyboard::Height();
    const bool typing = VKeyboard::IsOpen() && VKeyboard::IsChat();
    const char* txt = VKeyboard::IsChat() ? VKeyboard::GetText() : "";
    // 2.22: box hidden (no background + no label) and not typing -> nothing to draw or tap
    if(!typing && !settings.chatBoxBg && !settings.chatBoxLabel) return;
    // 2.22: "Tap the chat box to type" off -> the box never takes a touch until the Chat pill opens the keyboard
    const bool tappable = typing || settings.chatBoxTap;
    std::vector<Suggest> sug; BuildSuggestions(txt, sug);
    const float boxH = CS(60), rowH = CS(50);
    int maxRows = (int)((ds.y - kb - y - boxH - CS(20)) / rowH);
    if(maxRows < 0) maxRows = 0; if(maxRows > 6) maxRows = 6;
    int shown = (int)sug.size() < maxRows ? (int)sug.size() : maxRows;
    ImGui::SetNextWindowPos(ImVec2(x, y));
    ImGui::SetNextWindowSize(ImVec2(w, boxH + (shown ? shown * rowH + CS(12) : 0) + CS(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(CS(4), CS(4)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar;
    if(!tappable) wf |= ImGuiWindowFlags_NoInputs;
    if(ImGui::Begin("##MPChatBox", nullptr, wf))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float bw = ImGui::GetContentRegionAvail().x;
        if(tappable)
        {
            // 2.22: a real tap only - pressed AND released on the box, short, not a swipe / joystick drag
            static double s_PressT = 0;
            bool hit = ImGui::InvisibleButton("##cbin", ImVec2(bw, boxH));
            if(ImGui::IsItemActivated()) s_PressT = ImGui::GetTime();
            const ImGuiIO& io = ImGui::GetIO();
            bool shortTap = ImGui::GetTime() - s_PressT < 0.8 && io.MouseDragMaxDistanceSqr[0] < CS(28) * CS(28);
            if(hit && shortTap && !VKeyboard::IsOpen()) OpenChatKeyboard("");
        }
        else ImGui::Dummy(ImVec2(bw, boxH));
        if(settings.chatBoxBg || typing)
        {
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + boxH), IM_COL32(0, 0, 0, 150), CS(10));
            dl->AddRect(p, ImVec2(p.x + bw, p.y + boxH), IM_COL32(255, 255, 255, 235), CS(10), 0, CS(2.5f));
        }
        // ➤ arrow
        float ax = p.x + CS(16), ay = p.y + boxH * 0.5f, as = CS(11);
        dl->AddTriangleFilled(ImVec2(ax, ay - as), ImVec2(ax + as * 1.5f, ay), ImVec2(ax, ay + as), IM_COL32_WHITE);
        std::string shownTxt = UI::EmojiToPUA(txt);
        bool caret = VKeyboard::IsChat() && ((int)(ImGui::GetTime() * 2.0)) % 2 == 0;
        if(caret) shownTxt += "_";
        ImU32 tc = (txt[0] || caret) ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 200);
        dl->PushClipRect(ImVec2(p.x + CS(44), p.y), ImVec2(p.x + bw - CS(8), p.y + boxH), true);
        const char* hint = settings.chatBoxTap ? "Tap to chat  -  type / for commands" : "Chat button to type  -  / for commands";
        if(txt[0] || caret || settings.chatBoxLabel)   // 2.22: label can be hidden
            dl->AddText(UI::Font(), fs * 1.05f, ImVec2(p.x + CS(46), p.y + (boxH - fs * 1.05f) * 0.5f), tc,
                        (txt[0] || caret) ? shownTxt.c_str() : hint);
        dl->PopClipRect();
        if(shown)
        {
            ImGui::Dummy(ImVec2(1, CS(4)));
            ImVec2 q = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(q, ImVec2(q.x + bw, q.y + shown * rowH + CS(8)), IM_COL32(0, 0, 0, 165), CS(10));
            ImGui::Dummy(ImVec2(1, CS(4)));
            for(int k = 0; k < shown; ++k)
            {
                ImGui::PushID(k);
                ImVec2 r = ImGui::GetCursorScreenPos();
                bool hit = ImGui::InvisibleButton("##sg", ImVec2(bw, rowH));
                bool top = k == 0;   // best match: SA-MP style red highlight
                if(top) dl->AddRectFilled(ImVec2(r.x + CS(4), r.y), ImVec2(r.x + bw - CS(4), r.y + rowH), IM_COL32(190, 25, 25, 220), CS(8));
                float ty = r.y + (rowH - fs) * 0.5f;
                dl->AddText(UI::Font(), fs, ImVec2(r.x + CS(16), ty), IM_COL32_WHITE, sug[k].text.c_str());
                if(!sug[k].hint.empty())
                {
                    float tw = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, sug[k].text.c_str()).x;
                    dl->PushClipRect(r, ImVec2(r.x + bw - CS(8), r.y + rowH), true);
                    dl->AddText(UI::Font(), fs * 0.85f, ImVec2(r.x + CS(28) + tw, ty + fs * 0.08f), top ? IM_COL32(255, 220, 220, 230) : IM_COL32(190, 190, 190, 220), sug[k].hint.c_str());
                    dl->PopClipRect();
                }
                if(hit)
                {
                    std::string t = sug[k].text;
                    if(!strcmp(t.c_str(), "/tp ")) t = "/tp :";
                    if(VKeyboard::IsChat()) VKeyboard::SetText(t.c_str()); else OpenChatKeyboard(t.c_str());
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}

// ---- spawn picker: ( <  Spawn  > ) ----------------------------------------------------------
static void RenderSpawnPicker()
{
    if(!MP::SpawnPickerShown()) return;
    float a = MP::SpawnPickerAlpha();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bh = UI::S(84), side = UI::S(110), mid = UI::S(300), gap = UI::S(18);
    float total = side * 2 + mid + gap * 2;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - total * 0.5f - UI::S(10), ds.y * 0.80f - UI::S(60)));
    ImGui::SetNextWindowSize(ImVec2(total + UI::S(20), bh + UI::S(80)));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, a);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(10), UI::S(6)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPSpawn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar))
    {
        int sel = MP::SpawnSelected();
        char name[96]; snprintf(name, sizeof(name), "%s   (%d/%d)", MP::SpawnSpotName(sel), sel + 1, MP::SpawnSpotCount());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetCursorScreenPos();
        float fs = UI::S(32);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, name);
        float cx = wp.x + total * 0.5f;
        dl->AddRectFilled(ImVec2(cx - ts.x * 0.5f - UI::S(18), wp.y), ImVec2(cx + ts.x * 0.5f + UI::S(18), wp.y + fs + UI::S(16)), IM_COL32(0, 0, 0, 140), UI::S(10));
        UI::OutlinedText(dl, fs, ImVec2(cx - ts.x * 0.5f, wp.y + UI::S(8)), IM_COL32_WHITE, name);
        ImGui::Dummy(ImVec2(total, fs + UI::S(26)));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.75f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(16));
        ImGui::SetWindowFontScale(1.3f);
        if(ImGui::Button("<##sp", ImVec2(side, bh))) MP::SpawnStep(-1);
        ImGui::SameLine(0, gap);
        ImGui::PopStyleColor(3);
        if(ImGui::Button("Spawn##sp", ImVec2(mid, bh))) MP::SpawnConfirm();   // white, black text
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.55f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.15f, 0.15f, 0.15f, 0.65f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.75f));
        ImGui::SameLine(0, gap);
        if(ImGui::Button(">##sp", ImVec2(side, bh))) MP::SpawnStep(1);
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// ---- 2.19.1: Passenger button (the mobile game has no "enter as passenger" key) ---------------
static int      s_nPassCand = 0, s_nPassSeat = -1;
static uint64_t s_nPassScan = 0;
static void UpdatePassengerCandidate(int ped, uint64_t now)
{
    if(now - s_nPassScan < 250) return;
    s_nPassScan = now;
    s_nPassCand = 0; s_nPassSeat = -1;
    if(ped <= 0 || JoinFlowActive() || Game::IsCharInAnyCar(ped) || Game::IsCharGettingInToACar(ped) || !Game::IsReady()) return;
    if(Game::GetCharHealth(ped) <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    int car = 0; float best = 6.5f * 6.5f;
    // 2.19.4: only a car another player is DRIVING (they took it first). Empty cars, parked
    // cars and traffic: no icon (just get in normally as the driver).
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(i == myId || !p.active || !p.car || !p.ped || !Game::PedPtr(p.ped)) continue;
        if(!(p.sync.flags & SYNC_IN_CAR) || (p.sync.flags & SYNC_DEAD)) continue;
        if(Game::GetDriverOfCar(p.car) != p.ped) continue;   // they must be in the driver seat
        Vec3 cp; if(!Game::GetCarPos(p.car, cp)) continue;
        float dx = cp.x - me.x, dy = cp.y - me.y, dz = cp.z - me.z, d = dx * dx + dy * dy + dz * dz;
        if(d < best) { best = d; car = p.car; }
    }
    if(!car) return;
    Vec3 cp; if(!Game::GetCarPos(car, cp) || fabsf(cp.z - me.z) > 3.0f) return;
    int seat = Game::FreePassengerSeat(car, 0);
    if(seat < 0) return;
    s_nPassCand = car; s_nPassSeat = seat;
}

// car seen from above with its right door open: white with a dark outline (readable on any
// background), no button background
static void DrawCarOpenIcon(ImDrawList* dl, ImVec2 o, float S, float alpha)
{
    auto P = [&](float x, float y) { return ImVec2(o.x + x * S, o.y + y * S); };
    const ImU32 edge = IM_COL32(0, 0, 0, (int)(200 * alpha));
    const ImU32 white = IM_COL32(255, 255, 255, (int)(255 * alpha));
    const ImU32 dark = IM_COL32(25, 25, 25, (int)(255 * alpha));
    const float hx = 0.70f, hy = 0.36f, L = 0.26f, ang = 0.96f;   // door hinge, length, 55 deg
    const float ex = hx + L * sinf(ang), ey = hy + L * cosf(ang);
    for(int pass = 0; pass < 2; ++pass)
    {
        float g = pass == 0 ? 0.022f : 0.0f;
        ImU32 c = pass == 0 ? edge : white;
        dl->AddLine(P(hx, hy), P(ex, ey), c, (0.07f + 2 * g) * S);
        dl->AddCircleFilled(P(ex, ey), (0.035f + g) * S, c, 16);
        dl->AddRectFilled(P(0.30f - g, 0.06f - g), P(0.70f + g, 0.94f + g), c, (0.13f + g) * S);
    }
    dl->AddQuadFilled(P(0.38f, 0.26f), P(0.62f, 0.26f), P(0.66f, 0.36f), P(0.34f, 0.36f), dark);   // windscreen
    dl->AddQuadFilled(P(0.36f, 0.72f), P(0.64f, 0.72f), P(0.61f, 0.80f), P(0.39f, 0.80f), dark);   // rear window
    dl->AddQuadFilled(P(0.66f, 0.40f), P(0.66f, 0.70f), P(0.63f, 0.68f), P(0.63f, 0.42f), dark);   // side windows
    dl->AddQuadFilled(P(0.34f, 0.40f), P(0.37f, 0.42f), P(0.37f, 0.68f), P(0.34f, 0.70f), dark);
}

static void RenderPassengerButton()
{
    if(!s_nPassCand || JoinFlowActive() || VKeyboard::IsOpen() || Game::IsPaused()) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sz = UI::S(120);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.79f - sz * 0.5f, ds.y * 0.38f));
    ImGui::SetNextWindowSize(ImVec2(sz, sz));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPPassenger", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground))
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool tap = ImGui::InvisibleButton("##pb", ImVec2(sz, sz));   // "get in as passenger"
        bool held = ImGui::IsItemActive();
        float k = held ? 0.90f : 1.0f;   // a little smaller while pressed
        float s2 = sz * k;
        DrawCarOpenIcon(ImGui::GetWindowDrawList(), ImVec2(p.x + (sz - s2) * 0.5f, p.y + (sz - s2) * 0.5f), s2, held ? 0.75f : 0.95f);
        if(tap)
        {
            int ped = Game::LocalPed();
            int seat = Game::FreePassengerSeat(s_nPassCand, 0);
            if(Game::GetDriverOfCar(s_nPassCand) <= 0) seat = -2;   // the driver just got out
            if(seat >= 0)
            {
                Game::ClearTasksNow(ped);
                Game::TaskEnterCarAsPassenger(ped, s_nPassCand, seat);
                s_nPassBtnCar = s_nPassCand; s_nPassBtnT = MP_NowMs();
            }
            else if(seat == -1) SysMsg(COL_YELLOW, "That car is full.");
            s_nPassCand = 0;
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

// all 2.19 windows, every frame (FloatMenu floating window)
static void RenderGames();
static void RenderAuth();   // 3.21.6 (mp_auth.inc)
static void RenderStats();
static void RenderSamp();   // 3.01 (mp_world.inc): 3D labels, textdraws, class selection, dialogs
static void Render219(void*)
{
    FloatMenu::SetHidden(JoinFlowActive());   // join screen: no top bar
    RenderPairPopup();
    RenderBehindCountdown();   // 2.28
    if(!JoinFlowActive() || MP::SpawnPickerShown()) RenderChatBox();
    RenderSpawnPicker();
    RenderPassengerButton();   // 2.19.1
    RenderGames();             // 2.21: score bar + race / DM / job HUD
    RenderSamp();              // 3.01
    RenderBcw();               // 3.04: Brief Case War
    RenderMw3();               // 3.23: Modern Warfare 3
    RenderSafeZones();         // 3.21: hospital safe zones
    RenderMarkers();           // 3.21: red checkpoint beams
    RenderGates();             // 3.04.2: gate fade (over everything)
    RenderStats();             // 3.21.6: /stats  /char
    RenderAuth();              // 3.21.6: login screens (over the join view)
}

bool MP::JoinScreenActive() { return JoinFlowActive(); }
