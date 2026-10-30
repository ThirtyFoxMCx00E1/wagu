// MP 3.34 - Mega Vil Futuristic: the COSMETICS room (included by mp.cpp after mp_rp_inv.inc).
// In the lobby the second button (a T-shirt icon, next to "Spawn") flies the camera - eased, like the
// default server's < > - into the Didier Sachs store (area 14). There you stand between clothes rails,
// shelves and mirrors; a side bar on the left shows the wardrobe categories (net/rp.h g_RpCos), the
// outfits of the picked category are on the right. SAVE sends RPC_SKIN_SET (the server keeps it with
// your citizen record + card, so it is there after you leave), the camera flies back to the lobby and
// you press Spawn. CANCEL puts your old outfit back.

#define RP_COS_AREA 14
static const Vec3 kRpCosSpot = { 204.33f, -168.69f, 1000.52f };   // Didier Sachs, inside the door
struct RpCosState
{
    bool     on = false; int phase = 0;   // 0 flying in, 1 browsing, 2 flying out
    uint64_t t0 = 0; int cat = 0, sel = -1; int origSkin = 0, curSkin = -1, wantSkin = -1;
    Vec3     fromP{}, fromL{}, toP{}, toL{}, camP{}, camL{}; float yaw = 0;
    int      obj[16] = {}; int nObj = 0; bool objDone = false; float scroll = 0;
};
static RpCosState s_Cos;
static bool RpCosActive() { return s_Cos.on; }

// the props: clothes rails / shirts / shoe shelves round the spot (SA clothes shop models 2371 - 2401)
struct RpCosProp { int model; float dx, dy, dz, rz; };
static const RpCosProp kRpCosProps[] = {
    { 2372, -2.2f,  1.2f, -1.0f,  90 }, { 2372,  2.2f,  1.2f, -1.0f, 270 }, { 2374, -2.2f, -1.0f, -1.0f,  90 },
    { 2374,  2.2f, -1.0f, -1.0f, 270 }, { 2381, -1.4f,  2.4f, -1.0f,   0 }, { 2389,  1.4f,  2.4f, -1.0f,   0 },
    { 2394, -2.6f,  0.0f, -1.0f,  90 }, { 2399,  2.6f,  0.0f, -1.0f, 270 }, { 2401,  0.0f,  2.8f, -1.0f,   0 },
};
static void RpCosFreeProps()
{
    for(int i = 0; i < s_Cos.nObj; ++i) if(s_Cos.obj[i] > 0) Game::DeleteWorldObject(s_Cos.obj[i]);
    s_Cos.nObj = 0; s_Cos.objDone = false;
}
static void RpCosMakeProps()
{
    if(s_Cos.objDone) return;
    const int n = (int)(sizeof(kRpCosProps) / sizeof(kRpCosProps[0]));
    while(s_Cos.nObj < n)
    {
        const RpCosProp& p = kRpCosProps[s_Cos.nObj];
        int o = Game::CreateMapObject(p.model, { kRpCosSpot.x + p.dx, kRpCosSpot.y + p.dy, kRpCosSpot.z + p.dz }, 0, 0, p.rz);
        if(o == 0) return;   // still loading: next tick
        s_Cos.obj[s_Cos.nObj++] = o;   // -1 = no room: skipped
    }
    s_Cos.objDone = true;
}
// the camera: 3.4 m from you towards the open side of the room (a clear line), a little above
static void RpCosPickCam()
{
    float best = 0; int bestA = 0;
    for(int a = 0; a < 8; ++a)
    {
        float r = a * 0.785398f, cx = kRpCosSpot.x + cosf(r) * 3.4f, cy = kRpCosSpot.y + sinf(r) * 3.4f;
        bool clear = Game::LineOfSightClear({ kRpCosSpot.x, kRpCosSpot.y, kRpCosSpot.z + 0.5f }, { cx, cy, kRpCosSpot.z + 0.6f });
        float score = clear ? 2.0f : 0.0f;
        if(score > best) { best = score; bestA = a; }
    }
    float r = bestA * 0.785398f;
    s_Cos.yaw = r;
    s_Cos.toP = { kRpCosSpot.x + cosf(r) * 3.4f, kRpCosSpot.y + sinf(r) * 3.4f, kRpCosSpot.z + 0.65f };
    s_Cos.toL = { kRpCosSpot.x, kRpCosSpot.y, kRpCosSpot.z + 0.15f };
}
static void RpCosPlacePed(int ped)
{
    if(ped <= 0) return;
    Game::FreezeChar(ped, false);
    Game::PlaceChar(ped, kRpCosSpot);
    // face the camera
    float h = atan2f(-(s_Cos.toP.x - kRpCosSpot.x), s_Cos.toP.y - kRpCosSpot.y) * 57.29578f;
    Game::SetCharHeading(ped, h);
    Game::FreezeChar(ped, true);
    Game::SetCharVisible(ped, true);
}
static void RpCosOpen()
{
    if(s_Cos.on || !s_Rp.haveMe) return;
    s_Cos = RpCosState(); s_Cos.on = true; s_Cos.phase = 0; s_Cos.t0 = MP_NowMs();
    s_Cos.origSkin = s_Rp.me.skin; s_Cos.curSkin = s_Rp.me.skin;
    for(int i = 0; i < RP_COS_COUNT; ++i) if(g_RpCos[i].skin == s_Rp.me.skin) { s_Cos.sel = i; s_Cos.cat = g_RpCos[i].cat; break; }
    s_Cos.fromP = kRpLobbyCam; s_Cos.fromL = kRpLobbyLook;
    Game::PreloadArea(kRpCosSpot, true);
    RpCosPickCam();
    RpClick(RPS_CLICK);
}
static void RpCosClose(bool save)
{
    if(!s_Cos.on || s_Cos.phase == 2) return;
    if(save && s_Cos.sel >= 0)
    {
        int sk = g_RpCos[s_Cos.sel].skin;
        RpSend(RPC_SKIN_SET, 0, 0, sk); s_Rp.me.skin = (uint16_t)sk;
        SysMsg(RP_SKY, "[MEGA VIL] Outfit saved: %s. Press Spawn.", g_RpCos[s_Cos.sel].name);
    }
    else if(s_Cos.curSkin != s_Cos.origSkin) s_Cos.wantSkin = s_Cos.origSkin;
    s_Cos.phase = 2; s_Cos.t0 = MP_NowMs();
    s_Cos.fromP = s_Cos.camP; s_Cos.fromL = s_Cos.camL; s_Cos.toP = kRpLobbyCam; s_Cos.toL = kRpLobbyLook;
    RpClick(RPS_CLICK);
}
static void RpCosAbort()   // left the server / the lobby while in the room
{
    if(!s_Cos.on) return;
    RpCosFreeProps(); s_Cos.on = false;
}
static float RpCosEase(float k) { return k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; }
// game tick (lobby only)
static void RpCosTick(int ped, uint64_t now)
{
    if(!s_Cos.on) return;
    const float flightMs = 2200.0f;
    float k = (now - s_Cos.t0) / flightMs; if(k > 1) k = 1;
    if(s_Cos.phase == 0 || s_Cos.phase == 2)
    {
        float e = RpCosEase(k), arc = sinf(k * 3.14159f) * 60.0f;
        s_Cos.camP = { s_Cos.fromP.x + (s_Cos.toP.x - s_Cos.fromP.x) * e, s_Cos.fromP.y + (s_Cos.toP.y - s_Cos.fromP.y) * e,
                       s_Cos.fromP.z + (s_Cos.toP.z - s_Cos.fromP.z) * e + arc };
        s_Cos.camL = { s_Cos.fromL.x + (s_Cos.toL.x - s_Cos.fromL.x) * e, s_Cos.fromL.y + (s_Cos.toL.y - s_Cos.fromL.y) * e,
                       s_Cos.fromL.z + (s_Cos.toL.z - s_Cos.fromL.z) * e };
        Game::SetFixedCamera(s_Cos.camP, s_Cos.camL);
        if(s_Cos.phase == 0 && k > 0.6f && s_Rp.interior != RP_COS_AREA) { RpSetInterior(RP_COS_AREA); RpCosPlacePed(ped); }
        if(s_Cos.phase == 2 && k > 0.4f && s_Rp.interior == RP_COS_AREA) { RpCosFreeProps(); RpSetInterior(0); }
        if(k >= 1)
        {
            if(s_Cos.phase == 0) s_Cos.phase = 1;
            else
            {
                if(s_Cos.wantSkin >= 0) { Game::SetLocalModel(s_Cos.wantSkin); ped = Game::LocalPed(); }
                s_Cos.on = false; RpPark(ped);
                return;
            }
        }
    }
    if(s_Rp.interior == RP_COS_AREA) RpCosMakeProps();
    if(s_Cos.phase == 1)
    {
        // a new outfit: change the model, put him back on the spot
        if(s_Cos.sel >= 0 && g_RpCos[s_Cos.sel].skin != s_Cos.curSkin)
        {
            if(Game::SetLocalModel(g_RpCos[s_Cos.sel].skin)) { s_Cos.curSkin = g_RpCos[s_Cos.sel].skin; RpCosPlacePed(Game::LocalPed()); }
        }
        if(now % 2000 < 40) RpCosPlacePed(Game::LocalPed());
        Game::SetFixedCamera(s_Cos.toP, s_Cos.toL);
        s_Cos.camP = s_Cos.toP; s_Cos.camL = s_Cos.toL;
    }
}
// the side bars: categories on the left, the outfits of the category on the right, SAVE / CANCEL
static void RpCosDraw(float now)
{
    (void)now;
    if(!s_Cos.on || s_Cos.phase != 1) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const uint64_t nowMs = MP_NowMs();
    BcwBeginOverlay("##RpCos", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 glass = IM_COL32(10, 10, 12, 153), sky = RpCol(RP_SKY, 1), orange = IM_COL32(245, 130, 32, 255);
    // title
    BcwCenterText(dl, UI::S(30), UI::S(26), IM_COL32_WHITE, "WARDROBE");
    // left: categories
    const float lw = UI::S(240), rowH = UI::S(56), gap = UI::S(6), x0 = UI::S(24), y0 = UI::S(90);
    for(int c = 0; c < RPCO_CAT_COUNT; ++c)
    {
        ImVec2 a(x0, y0 + c * (rowH + gap)), b(a.x + lw, a.y + rowH);
        ImGui::SetCursorScreenPos(a);
        char id[16]; snprintf(id, sizeof(id), "##cc%d", c);
        if(ImGui::InvisibleButton(id, ImVec2(lw, rowH))) { s_Cos.cat = c; s_Cos.scroll = 0; RpClick(RPS_CLICK); }
        bool on = s_Cos.cat == c;
        dl->AddRectFilled(a, b, on ? IM_COL32(56, 189, 248, 120) : glass);
        dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), on ? sky : IM_COL32(255, 255, 255, 40));
        RpHudIcon(dl, RPH_COSMETIC, ImVec2(a.x + UI::S(32), (a.y + b.y) * 0.5f), UI::S(30), on ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 200));
        RpPlainText(dl, UI::S(19), ImVec2(a.x + UI::S(60), a.y + (rowH - UI::S(19)) * 0.5f), IM_COL32_WHITE, g_RpCosCats[c]);
    }
    // right: outfits of the category (2 columns of boxes)
    const float rw = UI::S(330), cw = (rw - gap) * 0.5f, chh = UI::S(70), rx = ds.x - rw - UI::S(24);
    int k = 0;
    for(int i = 0; i < RP_COS_COUNT; ++i)
    {
        if(g_RpCos[i].cat != s_Cos.cat) continue;
        ImVec2 a(rx + (k % 2) * (cw + gap), y0 + (k / 2) * (chh + gap)), b(a.x + cw, a.y + chh);
        ++k;
        if(b.y > ds.y - UI::S(130)) break;
        ImGui::SetCursorScreenPos(a);
        char id[16]; snprintf(id, sizeof(id), "##co%d", i);
        if(ImGui::InvisibleButton(id, ImVec2(cw, chh))) { s_Cos.sel = i; RpClick(RPS_CLICK); }
        bool on = s_Cos.sel == i;
        dl->AddRectFilled(a, b, on ? IM_COL32(56, 189, 248, 110) : IM_COL32(40, 40, 44, 102));
        dl->AddRectFilled(ImVec2(a.x, b.y - UI::S(4)), b, on ? sky : orange);
        RpPlainText(dl, UI::S(17), ImVec2(a.x + UI::S(10), a.y + UI::S(10)), IM_COL32_WHITE, g_RpCos[i].name);
        char sk[16]; snprintf(sk, sizeof(sk), "skin %d", g_RpCos[i].skin);
        RpPlainText(dl, UI::S(13), ImVec2(a.x + UI::S(10), a.y + UI::S(36)), IM_COL32(190, 196, 204, 255), sk);
    }
    // bottom: SAVE / CANCEL
    const float bw = UI::S(260), bh = UI::S(64), by = ds.y - bh - UI::S(36);
    ImGui::SetCursorScreenPos(ImVec2(ds.x * 0.5f - bw - UI::S(8), by));
    bool save = ImGui::InvisibleButton("##cosS", ImVec2(bw, bh));
    ImGui::SetCursorScreenPos(ImVec2(ds.x * 0.5f + UI::S(8), by));
    bool cancel = ImGui::InvisibleButton("##cosC", ImVec2(bw, bh));
    {
        ImVec2 a(ds.x * 0.5f - bw - UI::S(8), by), b(a.x + bw, by + bh);
        dl->AddRectFilled(a, b, sky); RpCenterIn(dl, UI::S(22), a, b, IM_COL32(8, 16, 26, 255), "SAVE");
        ImVec2 c(ds.x * 0.5f + UI::S(8), by), d(c.x + bw, by + bh);
        dl->AddRectFilled(c, d, glass); dl->AddRect(c, d, IM_COL32(255, 255, 255, 60)); RpCenterIn(dl, UI::S(22), c, d, IM_COL32_WHITE, "CANCEL");
    }
    BcwEndOverlay();
    (void)nowMs;
    if(save) RpCosClose(true);
    else if(cancel) RpCosClose(false);
}
