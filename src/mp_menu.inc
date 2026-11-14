// MP 4.13.4: [Multiplayer] on the game's main menu (next to Resume / Start Game / Settings).
//
//  tap Multiplayer -> the newest save loads (no save = a new game) behind an MP loading screen that
//  looks like the MP launcher panel (not the game's loading splashes) -> a black screen ("the hologram"):
//  the player waits invisible + frozen at Grove Street, no game sound -> after 5 s + 20 ticks (6 s) the
//  MP launcher (server UI) opens on top of the black -> pick a server and join. Leaving / losing the
//  server = the black screen again and the launcher comes back after 6 s.
//
// The tile is added through SAUtils when it is installed; without SAUtils MP draws its own tile on the
// main menu (bottom left, where the menu has no buttons).

static std::atomic<bool> s_bMenuMp{false};       // Multiplayer mode (the tile was tapped)
static std::atomic<bool> s_bMenuMpBlack{false};  // the black hologram screen is up (game thread -> HUD)
static std::atomic<int>  s_nMenuMpPct{-1};       // black-screen countdown 0..100 (-1 = no loading panel)
static std::atomic<bool> s_bMenuMpCard{false};   // launcher closed on the black screen: [Servers] / [Single player]
static bool     s_bMenuMpTile = false;           // SAUtils has our tile
static bool     s_bMenuMpOn = true;              // MainMenuMultiplayer (MP.ini)
static uint64_t s_nMenuMpTapAt = 0;
static uint64_t s_nMenuMpBlackAt = 0;
static bool     s_bMenuMpHeld = false;           // player frozen + invisible by us
static bool     s_bMenuMpMuted = false;
static bool     s_bMenuMpLaunched = false;       // the launcher was opened for this black screen
static std::atomic<bool> s_bMenuMpExit{false};
static std::atomic<bool> s_bMenuMpLoaded{false};  // the Multiplayer load is done (no more MP loading screen)   // [Single player] tapped (HUD -> game thread)
static const uint64_t kMenuMpDelayMs = 5000 + 20 * 50;   // 5 s + 20 game ticks (50 ms) = 6 s
// 4.13.9: one percent for the whole Multiplayer load (it went 0..99 for the game's load, then 0..100 again
// for the black-screen countdown): game load = 0..90, countdown = 90..100, never goes back, 100 is held
// 0.5 s before the server select opens. After the load the game's menu (pause menu) is closed for you.
static const uint64_t kMenuMpHold100Ms = 500;
static std::atomic<float>    s_fMenuMpShown{0.0f};    // the percent on screen (only goes up)
static std::atomic<bool>     s_bMenuMpViaLoad{false}; // this countdown follows a save load (90..100)
static std::atomic<uint64_t> s_nMenuMpSplashMs{0};    // last frame of the game's loading screen
static std::atomic<bool>     s_bMenuMpCloseMenu{false};   // close the game's menu once the load is done
// 4.13.10: after a save load the 6 s countdown is skipped (the custom screen stayed up for seconds after the game had
// loaded): 100 % is held 0.4 s, the screen fades to black in 0.5 s, then the server select opens. Coming back from a
// server keeps the countdown.
static const uint64_t kMenuMpQuickHoldMs = 400, kMenuMpFadeMs = 500;
static bool              s_bMenuMpQuick = false;      // this black screen follows a save load
static std::atomic<int>  s_nMenuMpFade{0};            // 0..255 black over the loading screen

static bool s_bMenuMpViaApi = false;   // 4.13.11: started by another mod (PCMenu), works even with MainMenuMultiplayer = 0
// 4.13.22: ServerBrowserMode = neon / mp / default (mp_neon.inc)
enum { NEON_MODE_DEFAULT = 0, NEON_MODE_MP = 1, NEON_MODE_NEON = 2 };
static int  s_nNeonMode = NEON_MODE_DEFAULT;   // 4.13.30: default until InitMainMenu reads the ini
static bool s_bNeonPendingJoin = false;   // a server picked on the neon title menu: joined after the Multiplayer load
static bool NeonOn() { return s_nNeonMode == NEON_MODE_NEON; }   // 4.13.23: ServerBrowserMode alone (an old MainMenuMultiplayer = 0 no longer hides it)
static bool NeonDraw();          // mp_neon.inc
static bool NeonPendingJoin();   // mp_neon.inc
static void MenuMpTap(void*)
{
    if(!s_bMenuMpOn && !s_bMenuMpViaApi) return;
    const bool again = s_bMenuMp.load();
    s_bMenuMp = true; s_bMenuMpExit = false;
    s_nMenuMpTapAt = MP_NowMs();
    if(again && Game::LocalPed() > 0) { logger->Info("Main menu: Multiplayer (already on)"); return; }
    s_bMenuMpLoaded = false;
    s_fMenuMpShown = 0.0f; s_bMenuMpViaLoad = true; s_nMenuMpSplashMs = 0; s_bMenuMpCloseMenu = true;
    int slot = Game::NewestSaveSlot();
    bool ok = slot >= 0 ? Game::RequestLoadSlot(slot) : Game::RequestNewGame();
    logger->Info("Main menu: Multiplayer -> %s %d (%s)", slot >= 0 ? "load slot" : "new game", slot + 1, ok ? "ok" : "failed");
    if(!ok) s_bMenuMp = false;
}

// 4.13.10: the "MPMultiplayer" AML interface: other mods (PCMenu's MULTIPLAYER button) start the same Multiplayer flow
// as the main menu tile.  struct { int version; void (*Multiplayer)(); int (*Available)(); }  (version 1)
// 4.13.11: ready as soon as MP's OnModLoad got through (MP::SetMenuApiReady). 4.13.10 waited for OnAllModsLoaded and
// MainMenuMultiplayer = 1, so PCMenu said "MULTIPLAYER NOT AVAILABLE" with MP installed when that call never came
// (older AML) or the tile was switched off.
static std::atomic<bool> s_bMenuMpReady{false};
static void LookUpPCMenuLoading();   // 4.13.12 (below)
static void MpApiMultiplayer()
{
    if(!s_bMenuMpReady.load()) { logger->Info("MPMultiplayer: called before MP finished loading - ignored"); return; }
    logger->Info("MPMultiplayer: Multiplayer requested by another mod (PCMenu)");
    s_bMenuMpViaApi = true; LookUpPCMenuLoading(); MenuMpTap(nullptr);
}
static int  MpApiAvailable() { return s_bMenuMpReady.load() ? 1 : 0; }
static int  MpApiLoading() { return MP::MenuMpLoading() ? 1 : 0; }   // 4.13.12: PCMenu shows "CONNECTING TO MULTIPLAYER"
static std::atomic<uint64_t> s_nNeonUpMs{0};   // 4.13.25: the last game-thread frame the neon title menu was up
static int  MpApiNeonMenu() { const uint64_t t = s_nNeonUpMs.load(); return t && MP_NowMs() - t < 500 ? 1 : 0; }   // PCMenu 1.2.9 stands aside
static MP::MultiplayerApi s_MpApi = { 3, MpApiMultiplayer, MpApiAvailable, MpApiLoading, MpApiNeonMenu };
// 4.13.12: PCMenu 1.2.6+ registers "PCMenuLoading". While its PC loading screen is on, MP leaves the loading screen to
// it (before, MP's panel covered it whenever MULTIPLAYER loaded the game). Looked up on the main thread only.
struct PCMenuLoadingApi { int version; int (*Active)(); };
static std::atomic<PCMenuLoadingApi*> s_pPcLoading{nullptr};
static void LookUpPCMenuLoading()
{
    if(s_pPcLoading.load()) return;
    PCMenuLoadingApi* a = (PCMenuLoadingApi*)GetInterface("PCMenuLoading");
    if(a && a->version >= 1 && a->Active) { s_pPcLoading = a; logger->Info("PCMenu loading screen found: MP uses it for Multiplayer loads"); }
}
bool MP::PCMenuLoadingActive() { PCMenuLoadingApi* a = s_pPcLoading.load(); return a && a->Active() != 0; }
MP::MultiplayerApi* MP::GetMultiplayerApi() { return &s_MpApi; }
void MP::SetMenuApiReady() { s_bMenuMpReady = true; }
// the same API as a plain export, for mods that look MP up with dlsym (no AML interface list needed)
extern "C" __attribute__((visibility("default"))) void* MP_GetMultiplayerApi() { return &s_MpApi; }

bool MP::TitleMenuHookWanted() { return s_nNeonMode != NEON_MODE_DEFAULT; }   // 4.13.30
// 4.13.33: ServerBrowserMode straight from files/TYFMC/TYFMC.MP.ini on the disk. AML keeps the ini in memory and
// rewrites the whole file on every save, so a value typed in by hand while the game ran was put back to the old
// one at the next save (the mode "stayed default"). The file is read before every save now and the hand-edited
// value wins.
static char s_szModeKnown[24] = "";
static bool ReadIniKey(const char* section, const char* key, char* out, size_t n)
{
    const std::string path = TYFMC::IniPath("MP");
    FILE* f = fopen(path.c_str(), "rb");
    if(!f) return false;
    char line[512]; bool inSec = false, found = false;
    while(fgets(line, sizeof(line), f))
    {
        char* p = line; while(*p == ' ' || *p == '\t') ++p;
        if(*p == '[') { char* e = strchr(p, ']'); inSec = e && (size_t)(e - p - 1) == strlen(section) && !strncasecmp(p + 1, section, strlen(section)); continue; }
        if(!inSec || *p == ';' || *p == '#') continue;
        char* eq = strchr(p, '='); if(!eq) continue;
        char* ke = eq; while(ke > p && (ke[-1] == ' ' || ke[-1] == '\t')) --ke;
        if((size_t)(ke - p) != strlen(key) || strncasecmp(p, key, ke - p)) continue;
        char* v = eq + 1; while(*v == ' ' || *v == '\t' || *v == '"') ++v;
        size_t l = strlen(v); while(l && (v[l - 1] == '\r' || v[l - 1] == '\n' || v[l - 1] == ' ' || v[l - 1] == '\t' || v[l - 1] == '"' || v[l - 1] == ';')) v[--l] = 0;
        snprintf(out, n, "%s", v); found = true; break;
    }
    fclose(f);
    return found;
}
static int ModeFromString(char* m)
{
    for(char* c = m; *c; ++c) *c = (char)tolower((unsigned char)*c);
    if(!strcmp(m, "mp") || !strcmp(m, "launcher") || !strcmp(m, "1") || !strcmp(m, "multiplayer")) { MP_CopyStr(m, "mp", 24); return NEON_MODE_MP; }
    if(!strcmp(m, "neon") || !strcmp(m, "2") || !strcmp(m, "new")) { MP_CopyStr(m, "neon", 24); return NEON_MODE_NEON; }
    MP_CopyStr(m, "default", 24); return NEON_MODE_DEFAULT;
}
static bool s_bTitleHookOn = false;
static void ApplyMode(int mode)
{
    s_nNeonMode = mode;
    if(mode == NEON_MODE_DEFAULT) s_bMenuMpOn = false;
    else s_bMenuMpOn = true;   // 4.13.33: mp / neon always show their menu (MainMenuMultiplayer = 0 hid the mp tile)
}
static void CfgBeforeSave(Config* c)
{
    if(c != ::cfg) return;
    char m[24];
    if(!ReadIniKey("MP", "ServerBrowserMode", m, sizeof(m))) return;
    char low[24]; MP_CopyStr(low, m, sizeof(low));
    const int mode = ModeFromString(low);
    if(!strcmp(low, s_szModeKnown)) return;
    logger->Info("Main menu: ServerBrowserMode changed in TYFMC.MP.ini by hand: %s -> %s%s", s_szModeKnown, low,
        (mode != NEON_MODE_DEFAULT && !s_bTitleHookOn) ? " (restart the game to see it)" : "");
    MP_CopyStr(s_szModeKnown, low, sizeof(s_szModeKnown));
    ConfigEntry* e = ::cfg->Bind("ServerBrowserMode", low, "MP"); e->SetString(low); delete e;
    if(mode == NEON_MODE_DEFAULT || s_bTitleHookOn) ApplyMode(mode);
}
void MP::SetTitleHookInstalled(bool on) { s_bTitleHookOn = on; }

void MP::InitMainMenu()
{
    s_bMenuMpOn = ::cfg->GetBool("MainMenuMultiplayer", true, "MP");
    {   // 4.13.22: ServerBrowserMode = neon (the neon title menu + server browser) / mp (the [Multiplayer] tile + the
        // launcher panel) / default (the game's own menu only)
        // 4.13.33: the value on the disk wins (see CfgBeforeSave); no more "set back to default" on start.
        char m[24] = "default";
        ::cfg->GetString(m, sizeof(m), "ServerBrowserMode", "default", "MP");
        char disk[24];
        const std::string ini = TYFMC::IniPath("MP");
        if(ReadIniKey("MP", "ServerBrowserMode", disk, sizeof(disk))) { if(strcasecmp(disk, m)) logger->Info("Main menu: ServerBrowserMode %s in %s (AML had %s)", disk, ini.c_str(), m); MP_CopyStr(m, disk, sizeof(m)); }
        else logger->Info("Main menu: no ServerBrowserMode in %s yet - written as %s", ini.c_str(), m);
        if(::cfg->GetInt("ServerBrowserModeReset", 0, "MP") < 41330) { ConfigEntry* r = ::cfg->Bind("ServerBrowserModeReset", 0, "MP"); r->SetInt(41330); delete r; }
        if(!m[0]) MP_CopyStr(m, "default", sizeof(m));
        ApplyMode(ModeFromString(m));
        MP_CopyStr(s_szModeKnown, m, sizeof(s_szModeKnown));
        { ConfigEntry* e = ::cfg->Bind("ServerBrowserMode", m, "MP"); e->SetString(m); delete e; ::cfg->Save(); }   // written to TYFMC.MP.ini
        Config::s_fnBeforeSave = CfgBeforeSave;
        logger->Info("Main menu: ServerBrowserMode = %s (neon = new main menu, mp = [Multiplayer] button on the game's menu, default = nothing) - %s", m, ini.c_str());
    }
    LookUpPCMenuLoading();
    if(!s_bMenuMpOn) { logger->Info("Main menu: Multiplayer tile off (MainMenuMultiplayer = 0 / ServerBrowserMode = default)"); return; }
    // 4.13.13: no SAUtils tile any more - SAUtils puts it on the pause menu too. MP draws its own tile, on the
    // title menu only (no game loaded yet).
    s_bMenuMpTile = false;
    logger->Info("Main menu: Multiplayer tile on the title menu only");
}

static void MenuMpRelease(int ped)
{
    if(s_bMenuMpMuted) { Game::SetGameSoundFader(1.0f); s_bMenuMpMuted = false; }
    if(s_bMenuMpHeld)
    {
        s_bMenuMpHeld = false;
        if(ped > 0) { Game::FreezeChar(ped, false); Game::SetCharVisible(ped, true); }
        if(!FloatMenu::IsModal()) { Game::ShowGameHud(true); Game::SetPlayerControl(true); }
    }
    s_bMenuMpBlack = false; s_nMenuMpPct = -1; s_bMenuMpCard = false;
}

// game thread, every frame (before the join flow, so the join screen's own HUD / camera calls win)
static void MenuMpProcess(int ped, uint64_t now)
{
    if(!s_bMenuMp.load()) return;
    if(s_bMenuMpExit.exchange(false))
    {   // [Single player]: the normal game, right where the save put you... at Grove Street
        if(FloatMenu::IsPageOpen(FloatMenu::PAGE_SERVER)) FloatMenu::ClosePage(FloatMenu::PAGE_SERVER);
        MenuMpRelease(ped);
        s_bMenuMp = false;
        logger->Info("Main menu: Multiplayer -> single player");
        return;
    }
    if(ped <= 0 || !Game::IsReady() || Game::IsFrontendMenu()) return;   // still loading / in the menu
    const int st = client.GetState();
    const bool black = st != NET_CONNECTED && !JoinScreenUp();
    if(!black)
    {
        if(s_bMenuMpBlack.load() || s_bMenuMpHeld || s_bMenuMpMuted) { MenuMpRelease(ped); logger->Info("Main menu: Multiplayer -> on the server"); }
        s_bMenuMpLaunched = false;
        return;
    }
    if(!s_bMenuMpBlack.load())
    {   // the black hologram: Grove Street, invisible, frozen, no HUD, no sound
        s_bMenuMpBlack = true; s_bMenuMpLoaded = true;
        s_nMenuMpBlackAt = now;
        s_bMenuMpQuick = s_bMenuMpViaLoad.load(); s_nMenuMpFade = 0;
        if(!s_bMenuMpViaLoad.load()) s_fMenuMpShown = 0.0f;   // back from a server: the countdown alone, 0..100
        s_bMenuMpLaunched = false; s_bMenuMpCard = false;
        if(st == NET_DISCONNECTED) SpawnLight(ped, kGroveStreet, kGroveHeading, false);
        Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false);
        Game::ShowGameHud(false); Game::SetPlayerControl(false);
        s_bMenuMpHeld = true;
        logger->Info("Main menu: Multiplayer -> black screen (server select in %u ms)", (unsigned)kMenuMpDelayMs);
    }
    Game::SetGameSoundFader(0.0f); s_bMenuMpMuted = true;   // every frame: the game sets its faders back
    if(now % 1000 < 40)
    {   // scripts / the launcher switch these back: again every second
        Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false);
        Game::ShowGameHud(false); Game::SetPlayerControl(false);
    }
    const uint64_t t = now - s_nMenuMpBlackAt;
    if(!s_bMenuMpLaunched)
    {
        const uint64_t delay = s_bMenuMpQuick ? 0 : kMenuMpDelayMs, hold = s_bMenuMpQuick ? kMenuMpQuickHoldMs : kMenuMpHold100Ms;
        const uint64_t fade = s_bMenuMpQuick ? kMenuMpFadeMs : 0;
        if(st == NET_DISCONNECTED && t < delay + hold + fade)
        {
            s_nMenuMpPct = t < delay ? (int)(t * 100 / delay) : 100;
            s_nMenuMpFade = t > delay + hold ? (int)((t - delay - hold) * 255 / (fade ? fade : 1)) : 0;
            s_bMenuMpCard = false; return;
        }
        s_nMenuMpPct = -1; s_nMenuMpFade = 0;
        s_bMenuMpViaLoad = false;
        s_bMenuMpLaunched = true;
        if(st == NET_DISCONNECTED)
        {   // 4.13.22: a server picked on the neon title menu is joined now; the neon menu is the server select
            if(NeonPendingJoin()) {}
            else if(!NeonOn()) FloatMenu::OpenPage(FloatMenu::PAGE_SERVER);
        }
    }
    s_bMenuMpCard = !NeonOn() && st == NET_DISCONNECTED && !FloatMenu::IsPageOpen(FloatMenu::PAGE_SERVER);
}

static bool MenuMpActive() { return s_bMenuMp.load(); }

// 4.13.9: the game's menu, every frame it is up (MobileMenu::Update, after the game's own update): a
// Multiplayer load puts you back into the pause menu when it is done - it is closed like [Resume]
void MP::MenuMpMenuTick()
{
    if(!s_bMenuMp.load() || !s_bMenuMpCloseMenu.load()) return;
    const uint64_t sp = s_nMenuMpSplashMs.load();
    if(!sp || Game::IsLoadPending() || !Game::IsFrontendMenu()) return;   // the load has not run yet
    if(MP_NowMs() - sp < 400) return;                                      // still on the loading screen
    s_bMenuMpCloseMenu = false;
    const bool ok = Game::ExitFrontendMenu();
    logger->Info("Main menu: Multiplayer load done -> game menu closed (%s)", ok ? "ok" : "no MobileMenu::Exit");
}
bool MP::MenuMpBlack() { return s_bMenuMp.load() && s_bMenuMpBlack.load(); }
bool MP::MenuMpLoading() { return (s_bMenuMpOn || s_bMenuMpViaApi) && s_bMenuMp.load() && !s_bMenuMpLoaded.load(); }

// ---- the launcher-styled loading panel ------------------------------------------------------
static const char* const kMenuMpTips[] = {
    "Tip: tap a server twice to join it",
    "Tip: featured servers are online 24/7",
    "Tip: hold the chat bubble to talk (voice chat)",
    "Tip: Settings > Servers keeps your favourite servers",
    "Tip: /help lists the commands of the server you are on",
};
// 4.13.7: the loading screen of a main menu [Multiplayer] tap: ui_bg.jpg (the launcher backdrop), title.png
// in the centre, the tip bottom left (blue heading + white text, the Rust / Facepunch style) and only the
// percent bottom right. Only the draw list is used (no ImGui windows): it never takes a touch.
static void MlBoldText(ImDrawList* dl, ImFont* f, float fs, ImVec2 p, ImU32 col, const char* t)
{
    dl->AddText(f, fs, ImVec2(p.x + UI::S(2), p.y + UI::S(3)), IM_COL32(0, 0, 0, 120), t);   // soft drop shadow
    dl->AddText(f, fs, p, col, t);
    dl->AddText(f, fs, ImVec2(p.x + UI::S(1.2f), p.y), col, t);                               // a little heavier
}
void MP::DrawMenuLoading(ImDrawList* dl, float pct, bool loadingScreen)
{
    (void)loadingScreen;
    if(!dl) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(pct < 0) pct = 0; else if(pct > 100) pct = 100;
    const uint64_t nowMs = MP_NowMs();
    {   // 4.13.9: one percent for the whole load, only going up
        float v = loadingScreen ? pct * 0.9f : (s_bMenuMpViaLoad.load() ? 90.0f + pct * 0.1f : pct);
        if(loadingScreen) s_nMenuMpSplashMs = nowMs;
        if(v < s_fMenuMpShown.load()) v = s_fMenuMpShown.load();
        s_fMenuMpShown = v;
        pct = v;
    }
    ImFont* f = UI::Font(); const float fs = ImGui::GetFontSize();
    const ImU32 kBlue = IM_COL32(72, 160, 255, 255);

    // ---- background: the launcher's blurred night city over the whole screen, darker at the edges
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(8, 11, 20, 255));
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("ui_bg.jpg", &uv)) LImageCover(dl, t, uv, ImVec2(0, 0), ds, IM_COL32(255, 255, 255, 255));
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(6, 8, 12, 130));   // 4.13.10: darker, like the Rust screen
    {   // 4.13.9: a soft round vignette all around (clear in the middle, a see-through dark at the edges)
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f), uvw = dl->_Data->TexUvWhitePixel;
        const int seg = 48;
        const float r0 = 0.30f, r1 = 0.72f;   // inner (clear) / outer (dark) ellipse, x half-size relative
        const ImU32 in = IM_COL32(0, 0, 0, 0), out = IM_COL32(0, 0, 0, 165);
        dl->PrimReserve(seg * 6, seg * 2);
        const ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        for(int i = 0; i < seg; ++i)
        {
            const float a = 6.2831853f * i / seg, cx = cosf(a), sy = sinf(a);
            dl->PrimWriteVtx(ImVec2(c.x + cx * ds.x * r0, c.y + sy * ds.y * r0), uvw, in);
            dl->PrimWriteVtx(ImVec2(c.x + cx * ds.x * r1, c.y + sy * ds.y * r1), uvw, out);
        }
        for(int i = 0; i < seg; ++i)
        {
            const ImDrawIdx a0 = base + i * 2, a1 = a0 + 1, b0 = base + ((i + 1) % seg) * 2, b1 = b0 + 1;
            dl->PrimWriteIdx(a0); dl->PrimWriteIdx(a1); dl->PrimWriteIdx(b1);
            dl->PrimWriteIdx(a0); dl->PrimWriteIdx(b1); dl->PrimWriteIdx(b0);
        }
    }
    // a little darker at the bottom for the tip / percent
    dl->AddRectFilledMultiColor(ImVec2(0, ds.y * 0.70f), ds, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 120), IM_COL32(0, 0, 0, 120));

    // ---- centre: the title
    ImVec2 tuv(1, 1);
    if(void* t = HUD::Texture("title.png", &tuv))
    {
        const float w = ImMin(ds.x * 0.46f, ds.y * 1.25f), h = w * (601.0f / 1920.0f);
        const ImVec2 c(ds.x * 0.5f, ds.y * 0.46f);
        dl->AddImage((ImTextureID)t, ImVec2(c.x - w * 0.5f, c.y - h * 0.5f), ImVec2(c.x + w * 0.5f, c.y + h * 0.5f), ImVec2(0, 0), tuv);
    }

    // ---- 4.13.10: the Rust (Facepunch) loading screen: bottom left a red (!) + red caps heading with a small grey
    // text under it, bottom right the loading step in light grey caps with the percent
    const ImU32 kRed = IM_COL32(205, 65, 43, 255);
    const float m = UI::S(44), hfs = fs * 0.62f, bfs = fs * 0.50f;
    const int nt = (int)(sizeof(kMenuMpTips) / sizeof(kMenuMpTips[0]));
    const char* tip = kMenuMpTips[(nowMs / 3000) % nt];
    if(!strncmp(tip, "Tip: ", 5)) tip += 5;
    const float by = ds.y - m - bfs, hy = by - UI::S(8) - hfs;
    const float r = hfs * 0.62f; const ImVec2 ic(m + r, hy + hfs * 0.5f);
    dl->AddCircleFilled(ic, r, kRed, 24);
    dl->AddRectFilled(ImVec2(ic.x - r * 0.13f, ic.y - r * 0.60f), ImVec2(ic.x + r * 0.13f, ic.y + r * 0.18f), IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(ImVec2(ic.x, ic.y + r * 0.45f), r * 0.15f, IM_COL32(255, 255, 255, 255), 10);
    MlBoldText(dl, f, hfs, ImVec2(m + r * 2 + UI::S(10), hy), kRed, "GAMEPLAY TIP");
    dl->AddText(f, bfs, ImVec2(m, by), IM_COL32(170, 170, 170, 235), tip);
    char ps[64];
    const char* step = loadingScreen ? "LOADING WORLD" : (pct >= 100 ? "READY" : "CONNECTING TO MULTIPLAYER");
    snprintf(ps, sizeof(ps), "%s  %d%%", step, (int)pct);
    const float pfs = fs * 0.62f;
    ImVec2 pz = f->CalcTextSizeA(pfs, FLT_MAX, 0, ps);
    MlBoldText(dl, f, pfs, ImVec2(ds.x - m - pz.x, ds.y - m - pz.y), IM_COL32(215, 215, 215, 240), ps);
}

// HUD, after NewFrame (under everything): the black
void MP::DrawMenuMpBack(ImDrawList* bg)
{
    if(!bg || !MenuMpBlack()) return;
    bg->AddRectFilled(ImVec2(0, 0), ImGui::GetIO().DisplaySize, IM_COL32(0, 0, 0, 255));
}

// HUD, after the launcher: the 6 s loading panel, the "launcher closed" card, the fallback tile
void MP::DrawMenuMpFront()
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    HudLayout::SetScreen(ds.x, ds.y);   // 4.13.22: the GTA HUD offsets are % of the screen
    if(MenuMpBlack())
    {
        int pct = s_nMenuMpPct.load();
        if(pct >= 0)
        {
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            DrawMenuLoading(fg, (float)pct, false);
            const int a = s_nMenuMpFade.load();   // 4.13.10: the fade to black after a save load
            if(a > 0) fg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, a > 255 ? 255 : a));
            return;
        }
        if(NeonDraw()) return;   // 4.13.22: the neon menu on the black screen
        if(s_bMenuMpCard.load() && !FloatMenu::IsModal())
        {
            const ImVec2 sz(UI::S(460), UI::S(200));
            ImGui::SetNextWindowPos(ImVec2((ds.x - sz.x) * 0.5f, (ds.y - sz.y) * 0.5f));
            ImGui::SetNextWindowSize(sz);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.13f, 0.96f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            if(ImGui::Begin("##MPMenuCard", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
            {
                ImGui::TextUnformatted("Multiplayer");
                ImGui::TextColored(ImVec4(0.6f, 0.62f, 0.66f, 1), "Pick a server to play, or go back to single player.");
                ImGui::Dummy(ImVec2(0, UI::S(12)));
                const float bw = (sz.x - ImGui::GetStyle().WindowPadding.x * 2 - UI::S(12)) * 0.5f;
                VPushWidgetStyle();
                if(ImGui::Button("Servers", ImVec2(bw, UI::S(64)))) FloatMenu::OpenPage(FloatMenu::PAGE_SERVER);
                ImGui::SameLine(0, UI::S(12));
                if(ImGui::Button("Single player", ImVec2(bw, UI::S(64)))) s_bMenuMpExit = true;
                VPopWidgetStyle();
            }
            ImGui::End();
            ImGui::PopStyleVar(); ImGui::PopStyleColor();
        }
        return;
    }
    if(NeonDraw()) return;   // 4.13.22: the neon title menu (ServerBrowserMode = neon)
    // no SAUtils: our own tile on the game's main menu, bottom left (the menu's tiles are in the middle)
    if(s_nNeonMode != NEON_MODE_MP && !s_bMenuMpViaApi) return;
    if(!s_bMenuMpOn || s_bMenuMpTile || s_bMenuMp.load() || !Game::IsMainMenuShown()) return;
    if(Game::LocalPed() > 0) return;   // 4.13.13: a game is loaded = the pause menu: no Multiplayer tile there
    if(NeonPaint::Ready()) return;     // 4.13.28: painted natively in the game's menu pass (MP::NeonPaintMenu)
    const ImVec2 sz(ds.x * 0.11f, ds.y * 0.17f), at(ds.x * 0.13f, ds.y * 0.70f);
    ImGui::SetNextWindowPos(at); ImGui::SetNextWindowSize(sz);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    if(ImGui::Begin("##MPMenuTile", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool tap = ImGui::InvisibleButton("##mpTile", sz);
        const bool hot = ImGui::IsItemActive();
        // the game's tile look: a dark slab with a light, slightly slanted frame
        ImVec2 q[4] = { ImVec2(p.x + sz.x * 0.04f, p.y), ImVec2(p.x + sz.x, p.y + sz.y * 0.03f), ImVec2(p.x + sz.x * 0.97f, p.y + sz.y), ImVec2(p.x, p.y + sz.y) };
        dl->AddConvexPolyFilled(q, 4, hot ? IM_COL32(60, 60, 60, 230) : IM_COL32(12, 12, 12, 220));
        dl->AddPolyline(q, 4, IM_COL32(200, 200, 200, 230), ImDrawFlags_Closed, UI::S(3));
        // two people glyph
        const ImU32 w = IM_COL32(220, 220, 220, 255);
        ImVec2 c(p.x + sz.x * 0.5f, p.y + sz.y * 0.42f); const float r = sz.y * 0.10f;
        dl->AddCircleFilled(ImVec2(c.x + r * 0.9f, c.y - r * 0.6f), r * 0.8f, w);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.4f, c.y - r * 0.3f), r, w);
        dl->PathArcTo(ImVec2(c.x - r * 0.4f, c.y + r * 2.2f), r * 1.7f, 3.14159f, 6.28318f, 20); dl->PathFillConvex(w);
        const char* lbl = "Multiplayer"; ImVec2 ts = ImGui::CalcTextSize(lbl);
        dl->AddText(ImVec2(p.x + (sz.x - ts.x) * 0.5f, p.y + sz.y * 0.80f - ts.y * 0.5f), IM_COL32_WHITE, lbl);
        if(tap) MenuMpTap(nullptr);
    }
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar();
}
