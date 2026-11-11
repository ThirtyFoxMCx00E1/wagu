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

static bool MenuMpActive() { return s_bMenuMp.load(); }
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
// 4.13.5: the loading screen of a main menu [Multiplayer] tap (Screenshots/animated-fivem-loading-screen-
// template-main-view.png layout) in the MP launcher's look: ui_bg.jpg (the launcher backdrop) over the whole
// screen, slate glass, square chips, the launcher's blue accent. Bottom left: title, "Welcome, <name>", two
// info chips. Middle: status, progress bar, percent. Right: sound chip. Centre right: the faded MP mark.
// pct 0..100. loadingScreen = the game's loading screen (save / new game), else the 6 s countdown over the
// black hologram before the launcher opens. Only the draw list is used (no ImGui windows): it never takes a touch.
static inline float MlHash(int i, int k) { uint32_t h = (uint32_t)i * 2654435761u ^ (uint32_t)k * 2246822519u; h ^= h >> 13; h *= 3266489917u; h ^= h >> 16; return (h & 0xFFFFFF) / 16777216.0f; }
static void MlChip(ImDrawList* dl, ImVec2 a, ImVec2 b, const char* label, ImU32 txt)
{
    VChamfer(dl, a, b, 0, IM_COL32(255, 255, 255, 22), IM_COL32(255, 255, 255, 46), UI::S(1.5f));
    const float fs = ImGui::GetFontSize() * 0.86f;
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, label);
    dl->AddText(UI::Font(), fs, ImVec2(floorf((a.x + b.x - ts.x) * 0.5f), floorf((a.y + b.y - ts.y) * 0.5f)), txt, label);
}
static float MlChipW(const char* label) { return UI::Font()->CalcTextSizeA(ImGui::GetFontSize() * 0.86f, FLT_MAX, 0, label).x + UI::S(56); }
static void MlBoldText(ImDrawList* dl, ImFont* f, float fs, ImVec2 p, ImU32 col, const char* t)
{
    dl->AddText(f, fs, ImVec2(p.x + UI::S(2), p.y + UI::S(3)), IM_COL32(0, 0, 0, 120), t);   // soft drop shadow
    dl->AddText(f, fs, p, col, t);
    dl->AddText(f, fs, ImVec2(p.x + UI::S(1.2f), p.y), col, t);                               // a little heavier
}
void MP::DrawMenuLoading(ImDrawList* dl, float pct, bool loadingScreen)
{
    if(!dl) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(pct < 0) pct = 0; else if(pct > 100) pct = 100;
    const uint64_t nowMs = MP_NowMs();
    const float tsec = (float)(nowMs % 600000) / 1000.0f;
    ImFont* f = UI::Font(); const float fs = ImGui::GetFontSize();

    // ---- background: the launcher's blurred night city over the whole screen
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(8, 11, 20, 255));
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("ui_bg.jpg", &uv)) LImageCover(dl, t, uv, ImVec2(0, 0), ds, IM_COL32(255, 255, 255, 255));
    // navy tint (the picture stays visible), the launcher-blue glow bottom left, dark floor for the text
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(10, 16, 34, 120));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x * 0.62f, ds.y), IM_COL32(40, 70, 110, 70), IM_COL32(40, 70, 110, 0), IM_COL32(40, 70, 110, 0), IM_COL32(70, 120, 180, 120));
    dl->AddRectFilledMultiColor(ImVec2(0, ds.y * 0.48f), ds, IM_COL32(4, 6, 12, 0), IM_COL32(4, 6, 12, 0), IM_COL32(4, 6, 12, 200), IM_COL32(4, 6, 12, 200));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, ds.y * 0.16f), IM_COL32(4, 6, 12, 150), IM_COL32(4, 6, 12, 150), IM_COL32(4, 6, 12, 0), IM_COL32(4, 6, 12, 0));

    // ---- floating sparks (slowly rising, launcher blue + a few white)
    for(int i = 0; i < 46; ++i)
    {
        const float sp = 0.018f + MlHash(i, 3) * 0.035f;                     // screen heights per second
        float yy = fmodf(MlHash(i, 2) + tsec * sp, 1.0f);
        const float x = (MlHash(i, 1) + 0.03f * sinf(tsec * (0.4f + MlHash(i, 5)) + i)) * ds.x;
        const float y = ds.y * (1.04f - yy * 1.08f);
        const float r = UI::S(1.6f + MlHash(i, 4) * 2.6f);
        int al = (int)(200 * (yy < 0.12f ? yy / 0.12f : yy > 0.85f ? (1.0f - yy) / 0.15f : 1.0f));
        if(al <= 0) continue;
        dl->AddCircleFilled(ImVec2(x, y), r, (i % 5 == 0) ? IM_COL32(236, 240, 245, al) : VAlpha(kVTeal, al / 255.0f), 10);
    }

    // ---- top bar: the launcher's title (PLAY | MP Launcher vX) + a turning ring (it is alive)
    const float pad = UI::S(54);
    {
        const float ty = UI::S(30);
        MlBoldText(dl, f, fs * 1.1f, ImVec2(pad, ty), IM_COL32_WHITE, "PLAY");
        dl->AddText(f, fs * 0.9f, ImVec2(pad + f->CalcTextSizeA(fs * 1.1f, FLT_MAX, 0, "PLAY").x + UI::S(20), ty + fs * 0.12f), kVDim, "MP Launcher  v" MP_VERSION_STR);
        dl->AddLine(ImVec2(pad, ty + fs * 1.1f + UI::S(14)), ImVec2(ds.x - pad, ty + fs * 1.1f + UI::S(14)), kVLine, UI::S(1));
        const float r = UI::S(12), t = (float)(nowMs % 1200) / 1200.0f * 6.2831853f;
        ImVec2 c(ds.x - pad - r, ty + fs * 0.55f);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, 30), 24, UI::S(3));
        dl->PathArcTo(c, r, t, t + 4.4f, 24); dl->PathStroke(kVTeal, 0, UI::S(3));
    }

    // ---- centre right: the faded MP mark (like the template's logo)
    {
        const float lh = ds.y * 0.20f;
        ImVec2 c(ds.x * 0.66f, ds.y * 0.42f);
        ImVec2 iuv(1, 1);
        if(void* ic = HUD::Texture("mp_menu.png", &iuv))
            dl->AddImage((ImTextureID)ic, ImVec2(c.x - lh * 0.5f, c.y - lh * 1.05f), ImVec2(c.x + lh * 0.5f, c.y - lh * 0.05f), ImVec2(0, 0), iuv, IM_COL32(150, 175, 215, 80));
        ImFont* big = UI::PricedownFont() ? UI::PricedownFont() : f;
        const char* l1 = "MULTIPLAYER";
        const float bfs = ds.y * 0.13f;
        ImVec2 s1 = big->CalcTextSizeA(bfs, FLT_MAX, 0, l1);
        dl->AddText(big, bfs, ImVec2(c.x - s1.x * 0.5f, c.y), IM_COL32(112, 132, 170, 96), l1);
        const char* l2 = "SAN ANDREAS";
        const float sfs = bfs * 0.34f;
        ImVec2 s2 = f->CalcTextSizeA(sfs, FLT_MAX, 0, l2);
        dl->AddText(f, sfs, ImVec2(c.x - s2.x * 0.5f, c.y + s1.y + UI::S(4)), IM_COL32(150, 170, 205, 90), l2);
    }

    // ---- bottom row geometry: chips + bar share one baseline
    const float chipH = UI::S(62), rowY = ds.y - UI::S(70) - chipH;
    // ---- bottom left: title, welcome, two chips
    {
        const char* title = loadingScreen ? "SAN ANDREAS MULTIPLAYER" : "MP MULTIPLAYER";
        float tfs = fs * 2.5f;
        const float maxW = ds.x * 0.44f;
        ImVec2 tsz = f->CalcTextSizeA(tfs, FLT_MAX, 0, title);
        if(tsz.x > maxW) { tfs *= maxW / tsz.x; tsz = f->CalcTextSizeA(tfs, FLT_MAX, 0, title); }
        const float wfs = fs * 1.05f;
        const float wy = rowY - UI::S(34) - wfs;
        const float ty = wy - UI::S(8) - tsz.y;
        MlBoldText(dl, f, tfs, ImVec2(pad, ty), IM_COL32_WHITE, title);
        const char* nm = settings.name[0] ? settings.name : "Player";
        dl->AddText(f, wfs, ImVec2(pad, wy), kVText, "Welcome,");
        const float wx = pad + f->CalcTextSizeA(wfs, FLT_MAX, 0, "Welcome, ").x;
        MlBoldText(dl, f, wfs, ImVec2(wx, wy), kVTeal, nm);
        const char* c1 = "FEATURED SERVERS";
        const char* c2 = "ONLINE 24/7";
        const float w1 = MlChipW(c1), w2 = MlChipW(c2);
        MlChip(dl, ImVec2(pad, rowY), ImVec2(pad + w1, rowY + chipH), c1, kVText);
        MlChip(dl, ImVec2(pad + w1 + UI::S(16), rowY), ImVec2(pad + w1 + UI::S(16) + w2, rowY + chipH), c2, kVText);
    }
    // ---- bottom right: sound chip (the black server select is silent)
    float rightX0;
    {
        const char* sc = loadingScreen ? "SOUND: ON" : "SOUND: OFF";
        const float w = MlChipW("SOUND: OFF");
        rightX0 = ds.x - pad - w;
        MlChip(dl, ImVec2(rightX0, rowY), ImVec2(ds.x - pad, rowY + chipH), sc, kVText);
    }
    // ---- middle: status, bar, percent, tip
    {
        const float bx0 = ds.x * 0.265f, bx1 = ImMin(ds.x * 0.735f, rightX0 - UI::S(40));
        const float bh = UI::S(12), by0 = rowY + chipH * 0.5f - bh * 0.5f, by1 = by0 + bh;
        const char* st = loadingScreen ? "LOADING SAN ANDREAS..." : "ESTABLISHING CONNECTION...";
        const float sfs = fs * 0.8f;
        ImVec2 ss = f->CalcTextSizeA(sfs, FLT_MAX, 0, st);
        dl->AddText(f, sfs, ImVec2(floorf((bx0 + bx1 - ss.x) * 0.5f), by0 - UI::S(16) - ss.y), kVDim, st);
        dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(255, 255, 255, 28));
        const float fx = bx0 + (bx1 - bx0) * pct / 100.0f;
        if(fx > bx0 + 1)
        {
            dl->AddRectFilledMultiColor(ImVec2(bx0, by0), ImVec2(fx, by1), IM_COL32(86, 140, 200, 255), kVTeal, kVTeal, IM_COL32(86, 140, 200, 255));
            dl->AddRectFilled(ImVec2(ImMax(bx0, fx - UI::S(26)), by0), ImVec2(fx, by1), IM_COL32(255, 255, 255, 60));   // bright head
        }
        dl->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(0, 0, 0, 120), 0, 0, UI::S(1));
        char pt[16]; snprintf(pt, sizeof(pt), "%d%%", (int)pct);
        const float pfs = fs * 0.95f;
        ImVec2 ps = f->CalcTextSizeA(pfs, FLT_MAX, 0, pt);
        MlBoldText(dl, f, pfs, ImVec2(floorf((bx0 + bx1 - ps.x) * 0.5f), by1 + UI::S(14)), IM_COL32_WHITE, pt);
        const int tip = (int)((nowMs / 3000) % (sizeof(kMenuMpTips) / sizeof(kMenuMpTips[0])));
        const float tfs = fs * 0.78f;
        ImVec2 tz = f->CalcTextSizeA(tfs, FLT_MAX, 0, kMenuMpTips[tip]);
        dl->AddText(f, tfs, ImVec2(floorf((bx0 + bx1 - tz.x) * 0.5f), by1 + UI::S(22) + ps.y), IM_COL32(150, 157, 166, 200), kMenuMpTips[tip]);
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
