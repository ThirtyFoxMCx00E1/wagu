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

static void MenuMpTap(void*)
{
    if(!s_bMenuMpOn) return;
    const bool again = s_bMenuMp.load();
    s_bMenuMp = true; s_bMenuMpExit = false;
    s_nMenuMpTapAt = MP_NowMs();
    if(again && Game::LocalPed() > 0) { logger->Info("Main menu: Multiplayer (already on)"); return; }
    s_bMenuMpLoaded = false;
    int slot = Game::NewestSaveSlot();
    bool ok = slot >= 0 ? Game::RequestLoadSlot(slot) : Game::RequestNewGame();
    logger->Info("Main menu: Multiplayer -> %s %d (%s)", slot >= 0 ? "load slot" : "new game", slot + 1, ok ? "ok" : "failed");
    if(!ok) s_bMenuMp = false;
}

void MP::InitMainMenu()
{
    s_bMenuMpOn = ::cfg->GetBool("MainMenuMultiplayer", true, "MP");
    if(!s_bMenuMpOn) { logger->Info("Main menu: Multiplayer tile off (MainMenuMultiplayer = 0)"); return; }
    std::string full = std::string(MP::DataDir()) + "/mp_menu.png";
    // SAUtils reads the picture relative to the game's files folder: files/TYFMC/MP/mp_menu.png
    s_bMenuMpTile = Game::AddMainMenuTile("Multiplayer", "TYFMC/MP/mp_menu.png", full.c_str(), MenuMpTap);
    if(!s_bMenuMpTile) logger->Info("Main menu: no SAUtils - MP draws its own Multiplayer tile");
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
        if(st == NET_DISCONNECTED && t < kMenuMpDelayMs) { s_nMenuMpPct = (int)(t * 100 / kMenuMpDelayMs); s_bMenuMpCard = false; return; }
        s_nMenuMpPct = -1;
        s_bMenuMpLaunched = true;
        if(st == NET_DISCONNECTED) FloatMenu::OpenPage(FloatMenu::PAGE_SERVER);
    }
    s_bMenuMpCard = st == NET_DISCONNECTED && !FloatMenu::IsPageOpen(FloatMenu::PAGE_SERVER);
}

bool MP::MenuMpBlack() { return s_bMenuMp.load() && s_bMenuMpBlack.load(); }
bool MP::MenuMpLoading() { return s_bMenuMpOn && s_bMenuMp.load() && !s_bMenuMpLoaded.load(); }

// ---- the launcher-styled loading panel ------------------------------------------------------
static const char* const kMenuMpTips[] = {
    "Tip: tap a server twice to join it",
    "Tip: featured servers are online 24/7",
    "Tip: hold the chat bubble to talk (voice chat)",
    "Tip: Settings > Servers keeps your favourite servers",
    "Tip: /help lists the commands of the server you are on",
};
static const char* const kMenuMpServers[] = {
    "MP Default Server", "Brief Case War 24/7", "Modern Warfare 3 24/7", "Mega Vil Futuristic Roleplay",
    "Project Day Z Legacy", "San Andreas Online League",
};
// pct 0..100. full = the whole screen is ours (game loading screen); else over the black hologram.
// Only the draw list is used (no ImGui windows): it never takes a touch.
void MP::DrawMenuLoading(ImDrawList* dl, float pct, bool loadingScreen)
{
    if(!dl) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(pct < 0) pct = 0; else if(pct > 100) pct = 100;
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 255));
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;
    ImVec2 a(mx, my), b(ds.x - mx, ds.y - my);
    if(loadingScreen) dl->AddRectFilledMultiColor(a, b, IM_COL32(30, 36, 46, 255), IM_COL32(30, 36, 46, 255), IM_COL32(10, 12, 16, 255), IM_COL32(10, 12, 16, 255));
    else VBackdrop(dl, a, b);   // the launcher's blurred city (already loaded in game)
    VGlass(dl, a, b);
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 150), 0, 0, UI::S(1));
    ImFont* f = UI::Font(); const float fs = ImGui::GetFontSize();
    // title bar, same as the launcher
    const float titleH = UI::S(70), pad = UI::S(22);
    dl->AddText(f, fs * 1.25f, ImVec2(a.x + pad, a.y + (titleH - fs * 1.25f) * 0.5f), IM_COL32_WHITE, "PLAY");
    dl->AddText(f, fs, ImVec2(a.x + pad + ImGui::CalcTextSize("PLAY").x * 1.25f + UI::S(22), a.y + (titleH - fs) * 0.5f), kVDim, "MP Launcher  v" MP_VERSION_STR);
    dl->AddLine(ImVec2(a.x, a.y + titleH), ImVec2(b.x, a.y + titleH), kVLine);
    // sidebar
    const float sideW = UI::S(210);
    dl->AddRectFilled(ImVec2(a.x, a.y + titleH), ImVec2(a.x + sideW, b.y), IM_COL32(0, 0, 0, 60));
    static const char* const side[] = { "Internet", "Featured", "Favorites", "History", "LAN" };
    for(int i = 0; i < 5; ++i)
    {
        float y = a.y + titleH + UI::S(16) + i * UI::S(52);
        if(i == 1) dl->AddRectFilled(ImVec2(a.x, y - UI::S(8)), ImVec2(a.x + sideW, y + fs + UI::S(8)), kVTealDim);
        if(i == 1) dl->AddRectFilled(ImVec2(a.x, y - UI::S(8)), ImVec2(a.x + UI::S(4), y + fs + UI::S(8)), kVTeal);
        dl->AddText(ImVec2(a.x + pad, y), i == 1 ? kVText : kVDim, side[i]);
    }
    // main: the featured servers (greyed while loading) + the progress
    const float cx0 = a.x + sideW + pad, cx1 = b.x - pad;
    float y = a.y + titleH + UI::S(18);
    dl->AddText(f, fs * 1.1f, ImVec2(cx0, y), kVText, loadingScreen ? "Loading San Andreas" : "Opening the server list"); y += fs * 1.1f + UI::S(14);
    for(int i = 0; i < (int)(sizeof(kMenuMpServers) / sizeof(kMenuMpServers[0])); ++i)
    {
        ImVec2 ra(cx0, y), rb(cx1, y + UI::S(44));
        if(rb.y > b.y - UI::S(120)) break;
        dl->AddRectFilled(ra, rb, (i & 1) ? IM_COL32(255, 255, 255, 10) : IM_COL32(255, 255, 255, 18));
        dl->AddText(ImVec2(ra.x + UI::S(12), ra.y + (UI::S(44) - fs) * 0.5f), kVDim, kMenuMpServers[i]);
        const char* w = "24/7"; float tw = ImGui::CalcTextSize(w).x;
        dl->AddText(ImVec2(rb.x - tw - UI::S(12), ra.y + (UI::S(44) - fs) * 0.5f), IM_COL32(126, 178, 226, 160), w);
        y += UI::S(48);
    }
    // progress bar
    const float by0 = b.y - UI::S(70), by1 = by0 + UI::S(10);
    dl->AddRectFilled(ImVec2(cx0, by0), ImVec2(cx1, by1), IM_COL32(255, 255, 255, 30));
    dl->AddRectFilled(ImVec2(cx0, by0), ImVec2(cx0 + (cx1 - cx0) * pct / 100.0f, by1), kVTeal);
    char pt[24]; snprintf(pt, sizeof(pt), "%d%%", (int)pct);
    dl->AddText(ImVec2(cx1 - ImGui::CalcTextSize(pt).x, by0 - fs - UI::S(8)), kVText, pt);
    const int tip = (int)((MP_NowMs() / 3000) % (sizeof(kMenuMpTips) / sizeof(kMenuMpTips[0])));
    dl->AddText(ImVec2(cx0, by1 + UI::S(14)), kVDim, kMenuMpTips[tip]);
    // a turning ring next to the title (it is alive)
    {
        const float r = UI::S(11), t = (float)(MP_NowMs() % 1200) / 1200.0f * 6.2831853f;
        ImVec2 c(cx1 - r, a.y + titleH + UI::S(18) + fs * 0.55f);
        dl->PathArcTo(c, r, t, t + 4.4f, 24); dl->PathStroke(kVTeal, 0, UI::S(3));
    }
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
    if(MenuMpBlack())
    {
        int pct = s_nMenuMpPct.load();
        if(pct >= 0) { DrawMenuLoading(ImGui::GetForegroundDrawList(), (float)pct, false); return; }
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
    // no SAUtils: our own tile on the game's main menu, bottom left (the menu's tiles are in the middle)
    if(!s_bMenuMpOn || s_bMenuMpTile || s_bMenuMp.load() || !Game::IsMainMenuShown()) return;
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
