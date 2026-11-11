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

static void MenuMpTap(void*)
{
    if(!s_bMenuMpOn) return;
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
        if(st == NET_DISCONNECTED && t < kMenuMpDelayMs + kMenuMpHold100Ms)
        {
            s_nMenuMpPct = t < kMenuMpDelayMs ? (int)(t * 100 / kMenuMpDelayMs) : 100;
            s_bMenuMpCard = false; return;
        }
        s_nMenuMpPct = -1;
        s_bMenuMpViaLoad = false;
        s_bMenuMpLaunched = true;
        if(st == NET_DISCONNECTED) FloatMenu::OpenPage(FloatMenu::PAGE_SERVER);
    }
    s_bMenuMpCard = st == NET_DISCONNECTED && !FloatMenu::IsPageOpen(FloatMenu::PAGE_SERVER);
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
bool MP::MenuMpLoading() { return s_bMenuMpOn && s_bMenuMp.load() && !s_bMenuMpLoaded.load(); }

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
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(6, 8, 16, 100));
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

    // ---- bottom left: (!) TIP heading in blue, the tip in white
    const float m = UI::S(44), hfs = fs * 0.80f, bfs = fs * 0.70f;
    const int nt = (int)(sizeof(kMenuMpTips) / sizeof(kMenuMpTips[0]));
    const char* tip = kMenuMpTips[(nowMs / 3000) % nt];
    if(!strncmp(tip, "Tip: ", 5)) tip += 5;
    const float by = ds.y - m - bfs, hy = by - UI::S(8) - hfs;
    const float r = hfs * 0.55f; const ImVec2 ic(m + r, hy + hfs * 0.5f);
    dl->AddCircleFilled(ic, r, kBlue, 24);
    dl->AddRectFilled(ImVec2(ic.x - r * 0.13f, ic.y - r * 0.62f), ImVec2(ic.x + r * 0.13f, ic.y + r * 0.18f), IM_COL32(255, 255, 255, 255));
    dl->AddCircleFilled(ImVec2(ic.x, ic.y + r * 0.45f), r * 0.15f, IM_COL32(255, 255, 255, 255), 10);
    MlBoldText(dl, f, hfs, ImVec2(m + r * 2 + UI::S(12), hy), kBlue, "GAMEPLAY TIP");
    MlBoldText(dl, f, bfs, ImVec2(m, by), IM_COL32(255, 255, 255, 235), tip);

    // ---- bottom right: only the percent
    char ps[8]; snprintf(ps, sizeof(ps), "%d%%", (int)pct);
    const float pfs = fs * 0.95f;
    ImVec2 pz = f->CalcTextSizeA(pfs, FLT_MAX, 0, ps);
    MlBoldText(dl, f, pfs, ImVec2(ds.x - m - pz.x, ds.y - m - pz.y), IM_COL32(255, 255, 255, 245), ps);
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
