// MP 3.26 - Mega Vil Futuristic: PANEL, player info + inventory (the bag), documents and the
// "Received 10x" item cards (included by mp.cpp after mp_rp_phone.inc). Look taken from the
// screenshots in Screenshots/ (Screenshot_20261026-084456 / -084503 / -084514):
//   - PANEL: a row of 5 icons in the middle of the screen (Vehicle, Documents, General, Bag, Phone),
//     the one under your finger on a dark hexagon, a sky blue hexagon with X under them closes it
//   - PLAYER INFO (left) + INVENTORY (right): cyan header bars, dark see-through body, your body with
//     the condition of 6 body parts, HEALTH / ARMOUR bars, CLOSE; a 5 x 3 bag grid with the icon, the
//     name, "1x (0.5)" and the weight "0.5 /50.0Kg"; AMOUNT / USE / GIVE / DROP
//   - "Received 10x" cards above the bottom right: the item icon and its name, a pink bar that runs
//     out in 15 s, then the card fades off in 20 ticks (1 tick = 50 ms)
// The bag itself lives on the server (net/server_rp.inc, PKT_RP_INV); this file only draws and asks.

#define RP_GOT_SHOW_MS  15000   // a "Received" card stays 15 s...
#define RP_GOT_FADE_MS  (20 * 50)   // ...then fades off in 20 ticks
#define RP_GOT_MAX      5

struct RpGotCard { uint8_t id; int count; uint64_t at; };
struct RpInvClient
{
    PktRpInv inv{}; bool have = false; uint64_t askedAt = 0;
    bool     panel = false; uint64_t panelAt = 0; int hot = 2;   // the PANEL row (2 = General, like the screenshot)
    int      win = 0;                                             // 0 none, 1 player info + inventory, 2 documents
    uint64_t winAt = 0;
    int      sel = -1, amount = 1;
    RpGotCard got[RP_GOT_MAX]{}; int nGot = 0;
    // body condition (head, torso, left arm, right arm, left leg, right leg), from the hits you take
    float    part[6] = { 100, 100, 100, 100, 100, 100 }; int lastHp = -1; uint64_t lastRegen = 0;
};
static RpInvClient s_Inv;
enum { RPW_NONE = 0, RPW_INV, RPW_DOCS };
static const char* const kRpParts[6] = { "HEAD", "TORSO", "L ARM", "R ARM", "L LEG", "R LEG" };

static void RpInvReset() { s_Inv = RpInvClient(); }
static bool RpInvOpen() { return s_Inv.panel || s_Inv.win != RPW_NONE; }

// ---- packets -----------------------------------------------------------------------------------
static void RpInvOnPacket(const uint8_t* d, int len)
{
    if(len < (int)sizeof(PktRpInv)) return;
    memcpy(&s_Inv.inv, d, sizeof(PktRpInv)); s_Inv.have = true;
    for(int i = 0; i < RP_INV_SLOTS; ++i) if(s_Inv.inv.slot[i].id >= RPX_COUNT) s_Inv.inv.slot[i].id = 0;
    if(s_Inv.sel >= 0 && !s_Inv.inv.slot[s_Inv.sel].id) s_Inv.sel = -1;
    if(s_Inv.sel >= 0 && s_Inv.amount > s_Inv.inv.slot[s_Inv.sel].count) s_Inv.amount = s_Inv.inv.slot[s_Inv.sel].count;
    if(s_Inv.amount < 1) s_Inv.amount = 1;
}
// a "Received 10x" card. The same item again while its card is still up adds to it (and restarts it).
static void RpInvGot(uint8_t id, int n, const char* why)
{
    if(!id || id >= RPX_COUNT || n <= 0) return;
    const uint64_t now = MP_NowMs();
    for(int i = 0; i < s_Inv.nGot; ++i)
        if(s_Inv.got[i].id == id && now - s_Inv.got[i].at < RP_GOT_SHOW_MS) { s_Inv.got[i].count += n; s_Inv.got[i].at = now; return; }
    if(s_Inv.nGot == RP_GOT_MAX) { memmove(s_Inv.got, s_Inv.got + 1, sizeof(RpGotCard) * (RP_GOT_MAX - 1)); --s_Inv.nGot; }
    RpGotCard& c = s_Inv.got[s_Inv.nGot++]; c.id = id; c.count = n; c.at = now;
    if(why && *why && strcmp(why, "Received")) SysMsg(RP_SKY, "[BAG] %s: %dx %s", why, n, g_RpInv[id].name);
}

// ---- body condition ----------------------------------------------------------------------------
static void RpInvBodyTick(int ped, uint64_t now)
{
    if(ped <= 0) return;
    int hp = Game::GetCharHealth(ped); if(hp > 100) hp = 100;
    if(s_Inv.lastHp < 0 || hp >= 100 || hp <= 0) { if(hp >= 100 || s_Inv.lastHp < 0) for(float& p : s_Inv.part) p = 100.0f; s_Inv.lastHp = hp; return; }
    if(hp < s_Inv.lastHp)
    {   // a hit: falls hurt the legs, the rest lands like real hits (most on the torso)
        int dmg = s_Inv.lastHp - hp;
        bool falling = Game::IsCharInAnyCar(ped) ? false : (Game::GetCharSpeed(ped) > 6.0f);
        int k;
        if(falling) k = 4 + rand() % 2;
        else { int r = rand() % 100; k = r < 10 ? 0 : r < 50 ? 1 : r < 63 ? 2 : r < 76 ? 3 : r < 88 ? 4 : 5; }
        s_Inv.part[k] -= dmg * 1.6f; if(s_Inv.part[k] < 0) s_Inv.part[k] = 0;
    }
    else if(hp > s_Inv.lastHp)
    {   // healed: every part gets the same back
        int up = hp - s_Inv.lastHp;
        for(float& p : s_Inv.part) { p += up * 1.6f; if(p > 100) p = 100; }
    }
    s_Inv.lastHp = hp;
    if(now - s_Inv.lastRegen > 4000) { s_Inv.lastRegen = now; for(float& p : s_Inv.part) if(p < hp) p += 1.0f; }
}

// ---- drawing helpers ---------------------------------------------------------------------------
static void RpHex(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{   // pointy-top hexagon (the PANEL buttons)
    for(int i = 0; i < 6; ++i) { float a = -IM_PI * 0.5f + i * (IM_PI / 3.0f); dl->PathLineTo(ImVec2(c.x + cosf(a) * r, c.y + sinf(a) * r)); }
    dl->PathFillConvex(col);
}
static void RpPlainText(ImDrawList* dl, float fs, ImVec2 p, ImU32 c, const char* t) { dl->AddText(UI::Font(), fs, p, c, t); }
static void RpCenterIn(ImDrawList* dl, float fs, ImVec2 a, ImVec2 b, ImU32 c, const char* t)
{
    ImVec2 sz = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    dl->AddText(UI::Font(), fs, ImVec2((a.x + b.x - sz.x) * 0.5f, (a.y + b.y - sz.y) * 0.5f), c, t);
}
// an item icon from rp_items.png (4 x 4 cells of 128 px); a letter tile when the file is missing
static void RpItemIcon(ImDrawList* dl, uint8_t id, ImVec2 c, float s, float alpha = 1.0f)
{
    if(!id || id >= RPX_COUNT) return;
    ImVec2 uv(1, 1);
    if(void* tex = HUD::Texture("rp_items.png", &uv))
    {
        int cell = g_RpInv[id].icon; float cw = uv.x / 4.0f, ch = uv.y / 4.0f;
        ImVec2 u0((cell % 4) * cw, (cell / 4) * ch), u1(u0.x + cw, u0.y + ch);
        dl->AddImage((ImTextureID)tex, ImVec2(c.x - s * 0.5f, c.y - s * 0.5f), ImVec2(c.x + s * 0.5f, c.y + s * 0.5f), u0, u1, IM_COL32(255, 255, 255, (int)(255 * alpha)));
        return;
    }
    dl->AddRectFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), RpCol(RP_SKY, 0.8f * alpha), UI::S(10));
    char l[2] = { g_RpInv[id].name[0], 0 };
    RpCenterIn(dl, s * 0.5f, ImVec2(c.x - s * 0.4f, c.y - s * 0.4f), ImVec2(c.x + s * 0.4f, c.y + s * 0.4f), IM_COL32(255, 255, 255, (int)(255 * alpha)), l);
}
// the PANEL glyphs (white line art like the screenshot)
static void RpPanelGlyph(ImDrawList* dl, int k, ImVec2 c, float s, ImU32 col)
{
    const float t = UI::S(3.2f);
    switch(k)
    {
    case 0:   // vehicle
        dl->AddRectFilled(ImVec2(c.x - s * 0.50f, c.y - s * 0.05f), ImVec2(c.x + s * 0.50f, c.y + s * 0.28f), col, UI::S(4));
        dl->AddQuadFilled(ImVec2(c.x - s * 0.30f, c.y - s * 0.05f), ImVec2(c.x - s * 0.16f, c.y - s * 0.34f), ImVec2(c.x + s * 0.20f, c.y - s * 0.34f), ImVec2(c.x + s * 0.34f, c.y - s * 0.05f), col);
        dl->AddRectFilled(ImVec2(c.x - s * 0.10f, c.y - s * 0.27f), ImVec2(c.x + s * 0.14f, c.y - s * 0.09f), IM_COL32(20, 26, 40, 255), UI::S(2));
        dl->AddCircleFilled(ImVec2(c.x - s * 0.28f, c.y + s * 0.30f), s * 0.13f, col); dl->AddCircleFilled(ImVec2(c.x + s * 0.28f, c.y + s * 0.30f), s * 0.13f, col);
        dl->AddCircleFilled(ImVec2(c.x - s * 0.28f, c.y + s * 0.30f), s * 0.06f, IM_COL32(20, 26, 40, 255)); dl->AddCircleFilled(ImVec2(c.x + s * 0.28f, c.y + s * 0.30f), s * 0.06f, IM_COL32(20, 26, 40, 255));
        break;
    case 1:   // documents
        dl->AddRectFilled(ImVec2(c.x - s * 0.32f, c.y - s * 0.42f), ImVec2(c.x + s * 0.32f, c.y + s * 0.42f), col, UI::S(2));
        for(int i = 0; i < 5; ++i) dl->AddLine(ImVec2(c.x - s * 0.20f, c.y - s * 0.26f + i * s * 0.13f), ImVec2(c.x + s * 0.20f, c.y - s * 0.26f + i * s * 0.13f), IM_COL32(20, 26, 40, 255), UI::S(2));
        break;
    case 2:   // general (menu)
        for(int i = 0; i < 3; ++i) dl->AddRectFilled(ImVec2(c.x - s * 0.42f, c.y - s * 0.30f + i * s * 0.25f), ImVec2(c.x + s * 0.42f, c.y - s * 0.16f + i * s * 0.25f), col, UI::S(2));
        break;
    case 3:   // bag
        dl->AddRect(ImVec2(c.x - s * 0.14f, c.y - s * 0.40f), ImVec2(c.x + s * 0.14f, c.y - s * 0.22f), col, UI::S(3), 0, t);
        dl->AddRectFilled(ImVec2(c.x - s * 0.40f, c.y - s * 0.24f), ImVec2(c.x + s * 0.40f, c.y + s * 0.38f), col, UI::S(5));
        for(int i = 0; i < 3; ++i) dl->AddLine(ImVec2(c.x - s * 0.28f, c.y - s * 0.09f + i * s * 0.13f), ImVec2(c.x + s * 0.28f, c.y - s * 0.09f + i * s * 0.13f), IM_COL32(20, 26, 40, 255), UI::S(2));
        break;
    case 4:   // phone
        dl->AddRectFilled(ImVec2(c.x - s * 0.22f, c.y - s * 0.44f), ImVec2(c.x + s * 0.22f, c.y + s * 0.44f), col, UI::S(4));
        dl->AddRectFilled(ImVec2(c.x - s * 0.16f, c.y - s * 0.34f), ImVec2(c.x + s * 0.16f, c.y + s * 0.30f), IM_COL32(20, 26, 40, 255), UI::S(2));
        break;
    }
}

// ---- actions -----------------------------------------------------------------------------------
static int RpNearestPlayer(float maxD)
{
    int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return -1;
    int best = -1; float bd = maxD * maxD;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == myId || !players[i].active || players[i].ped <= 0) continue;
        Vec3 p; if(!Game::GetCharPos(players[i].ped, p)) continue;
        float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z, d = dx * dx + dy * dy + dz * dz;
        if(d < bd) { bd = d; best = i; }
    }
    return best;
}
enum { RPKI_AMOUNT = 1, RPKI_GIVE };
static void RpInvKbSubmit(const char* t, void* data)
{
    if(!t || !*t) return;
    int mode = (int)(intptr_t)data, v = atoi(t);
    if(mode == RPKI_AMOUNT) { s_Inv.amount = v < 1 ? 1 : (v > RP_INV_STACK ? RP_INV_STACK : v); if(s_Inv.sel >= 0 && s_Inv.amount > s_Inv.inv.slot[s_Inv.sel].count) s_Inv.amount = s_Inv.inv.slot[s_Inv.sel].count; }
    else if(mode == RPKI_GIVE && s_Inv.sel >= 0 && v >= 0 && v < MP_MAX_PLAYERS) RpSend(RPC_INV_GIVE, (uint8_t)s_Inv.sel, (uint8_t)v, s_Inv.amount);
}
static void RpInvUse()
{
    if(s_Inv.sel < 0 || !s_Inv.inv.slot[s_Inv.sel].id) { SysMsg(COL_YELLOW, "[BAG] Tap an item first."); return; }
    const RpInvDef& d = g_RpInv[s_Inv.inv.slot[s_Inv.sel].id];
    if(d.use == RPU_PHONE) { s_Inv.win = RPW_NONE; RpPhoneOpen(RPA_HOME); return; }
    RpSend(RPC_INV_USE, (uint8_t)s_Inv.sel, 0, s_Inv.amount);
    if(d.use == RPU_FOOD || d.use == RPU_DRINK || d.use == RPU_MEDKIT || d.use == RPU_REPAIR) s_Inv.win = RPW_NONE;   // close, so you see yourself eat
}
static void RpInvGive()
{
    if(s_Inv.sel < 0 || !s_Inv.inv.slot[s_Inv.sel].id) { SysMsg(COL_YELLOW, "[BAG] Tap an item first."); return; }
    int n = RpNearestPlayer(4.0f);
    char init[8] = ""; if(n >= 0) snprintf(init, sizeof(init), "%d", n);
    char title[64]; snprintf(title, sizeof(title), n >= 0 ? "Give to player id (nearest: %s)" : "Give to player id (stand next to them)", n >= 0 ? RpName(n) : "");
    VKeyboard::Open(title, init, 3, RpInvKbSubmit, (void*)(intptr_t)RPKI_GIVE, true);
}
static void RpInvDrop()
{
    if(s_Inv.sel < 0 || !s_Inv.inv.slot[s_Inv.sel].id) { SysMsg(COL_YELLOW, "[BAG] Tap an item first."); return; }
    RpSend(RPC_INV_DROP, (uint8_t)s_Inv.sel, 0, s_Inv.amount);
}
static void RpInvShow(int win)
{
    if(!RpOnServer()) return;
    s_Inv.panel = false; s_Inv.win = win; s_Inv.winAt = MP_NowMs();
    if(win == RPW_INV && MP_NowMs() - s_Inv.askedAt > 1500) { s_Inv.askedAt = MP_NowMs(); RpSend(RPC_INV_REQ); }
    s_Rp.phone = false;
}
static void RpPanelToggle()
{
    if(!RpOnServer()) return;
    if(s_Inv.win) { s_Inv.win = RPW_NONE; return; }
    s_Inv.panel = !s_Inv.panel; s_Inv.panelAt = MP_NowMs(); s_Inv.hot = 2;
    if(s_Inv.panel) s_Rp.phone = false;
}

// ---- PANEL (screenshot 084456) -----------------------------------------------------------------
static void RpDrawPanel(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float k = (nowMs - s_Inv.panelAt) / 150.0f; if(k > 1) k = 1;
    BcwBeginOverlay("##RpPanel", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cx = ds.x * 0.5f, rowY = ds.y * 0.44f, gap = UI::S(122), hexR = UI::S(50) * (0.85f + 0.15f * k), ico = UI::S(42);
    // title: the Futuristic "F" stripes + PANEL
    {
        float fs = UI::S(36), ty = ds.y * 0.325f - fs * 0.5f;
        const char* t = "PANEL"; float w = RpTextW(fs, t), gw = UI::S(40), x = cx - (w + gw + UI::S(8)) * 0.5f;
        ImU32 sky = RpCol(0x67E8F9FF, k);
        dl->AddQuadFilled(ImVec2(x + UI::S(10), ty + UI::S(4)), ImVec2(x + gw, ty + UI::S(4)), ImVec2(x + gw - UI::S(4), ty + UI::S(11)), ImVec2(x + UI::S(6), ty + UI::S(11)), sky);
        dl->AddQuadFilled(ImVec2(x + UI::S(6), ty + UI::S(15)), ImVec2(x + gw - UI::S(10), ty + UI::S(15)), ImVec2(x + gw - UI::S(14), ty + UI::S(22)), ImVec2(x + UI::S(2), ty + UI::S(22)), sky);
        dl->AddQuadFilled(ImVec2(x + UI::S(6), ty + UI::S(4)), ImVec2(x + UI::S(12), ty + UI::S(4)), ImVec2(x + UI::S(2), ty + UI::S(34)), ImVec2(x - UI::S(4), ty + UI::S(34)), sky);
        RpText(dl, fs, ImVec2(x + gw + UI::S(8), ty), RpCol(0xFFFFFFFF, k), t);
    }
    static const char* const kLabels[5] = { "VEHICLE", "DOCUMENTS", "GENERAL", "BAG", "PHONE" };
    bool hitAny = false; int clicked = -1;
    for(int i = 0; i < 5; ++i)
    {
        ImVec2 c(cx + (i - 2) * gap, rowY);
        ImGui::SetCursorScreenPos(ImVec2(c.x - gap * 0.5f, c.y - hexR));
        ImGui::PushID(i);
        if(ImGui::InvisibleButton("##pn", ImVec2(gap, hexR * 2.0f + UI::S(24)))) clicked = i;
        if(ImGui::IsItemHovered() || ImGui::IsItemActive()) { s_Inv.hot = i; hitAny = true; }
        ImGui::PopID();
        if(s_Inv.hot == i) RpHex(dl, c, hexR, IM_COL32(28, 34, 54, (int)(225 * k)));
        RpPanelGlyph(dl, i, ImVec2(c.x, c.y - UI::S(8)), ico, RpCol(0xFFFFFFFF, k));
        float fs = UI::S(15), w = RpTextW(fs, kLabels[i]);
        RpText(dl, fs, ImVec2(c.x - w * 0.5f, c.y + UI::S(20)), RpCol(0xFFFFFFFF, k), kLabels[i]);
    }
    // the X (sky blue hexagon) under the row
    ImVec2 xc(cx, ds.y * 0.585f);
    ImGui::SetCursorScreenPos(ImVec2(xc.x - hexR, xc.y - hexR));
    bool closeX = ImGui::InvisibleButton("##pnx", ImVec2(hexR * 2, hexR * 2));
    if(ImGui::IsItemHovered() || ImGui::IsItemActive()) hitAny = true;
    RpHex(dl, xc, hexR * (ImGui::IsItemActive() ? 0.9f : 1.0f), RpCol(0x5BCBEFFF, 0.92f * k));
    RpCenterIn(dl, UI::S(34), ImVec2(xc.x - hexR, xc.y - hexR), ImVec2(xc.x + hexR, xc.y + hexR), RpCol(0xFFFFFFFF, k), "X");
    // a tap anywhere else closes it too
    bool outside = ImGui::IsMouseClicked(0) && !hitAny && !ImGui::IsAnyItemHovered();
    BcwEndOverlay();
    if(closeX || outside) { s_Inv.panel = false; return; }
    switch(clicked)
    {
        case 0: s_Inv.panel = false; RpPhoneOpen(RPA_GARAGE); break;
        case 1: RpInvShow(RPW_DOCS); break;
        case 2: case 3: RpInvShow(RPW_INV); break;
        case 4: s_Inv.panel = false; RpPhoneOpen(s_Rp.ringFrom >= 0 || s_Rp.callWith >= 0 ? -2 : RPA_HOME); break;
    }
}

// ---- PLAYER INFO + INVENTORY (screenshot 084503) ------------------------------------------------
static const uint32_t kInvHead = 0x4DE1F5FF, kInvBtn = 0x2CC9DCFF, kInvRed = 0xD9534FFF;
static void RpInvHeader(ImDrawList* dl, ImVec2 a, float w, const char* t)
{
    float h = UI::S(34);
    dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), RpCol(kInvHead, 1));
    RpCenterIn(dl, UI::S(19), a, ImVec2(a.x + w, a.y + h), IM_COL32(12, 22, 34, 255), t);
}
static bool RpInvButton(const char* id, ImVec2 a, ImVec2 sz, uint32_t col, const char* label, ImU32 tc = IM_COL32(12, 22, 34, 255))
{
    ImGui::SetCursorScreenPos(a);
    bool r = ImGui::InvisibleButton(id, sz);
    bool down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), RpCol(col, down ? 0.75f : 0.95f));
    RpCenterIn(dl, UI::S(19), a, ImVec2(a.x + sz.x, a.y + sz.y), tc, label);
    return r;
}
static void RpDrawBody(ImDrawList* dl, ImVec2 c, float h)
{   // a standing citizen (sky blue shirt, jeans, white shoes); every body part tinted by its condition
    auto tint = [&](int k, uint32_t base) -> ImU32
    {
        float q = s_Inv.part[k] / 100.0f; if(q > 0.75f) return RpCol(base, 1);
        int r = 255, g = (int)(80 + 175 * q), b = (int)(80 * q); return IM_COL32(r, g, b, 255);
    };
    const float u = h / 10.0f;
    const ImU32 skin = IM_COL32(222, 184, 150, 255);
    // legs
    dl->AddRectFilled(ImVec2(c.x - u * 0.95f, c.y + u * 0.6f), ImVec2(c.x - u * 0.08f, c.y + u * 4.6f), tint(4, 0x93C5FDFF), u * 0.25f);
    dl->AddRectFilled(ImVec2(c.x + u * 0.08f, c.y + u * 0.6f), ImVec2(c.x + u * 0.95f, c.y + u * 4.6f), tint(5, 0x93C5FDFF), u * 0.25f);
    dl->AddRectFilled(ImVec2(c.x - u * 1.05f, c.y + u * 4.5f), ImVec2(c.x - u * 0.05f, c.y + u * 4.95f), IM_COL32(245, 245, 245, 255), u * 0.2f);
    dl->AddRectFilled(ImVec2(c.x + u * 0.05f, c.y + u * 4.5f), ImVec2(c.x + u * 1.05f, c.y + u * 4.95f), IM_COL32(245, 245, 245, 255), u * 0.2f);
    // arms
    dl->AddRectFilled(ImVec2(c.x - u * 1.75f, c.y - u * 2.7f), ImVec2(c.x - u * 1.12f, c.y + u * 0.5f), tint(2, 0xF1F5F9FF), u * 0.3f);
    dl->AddRectFilled(ImVec2(c.x + u * 1.12f, c.y - u * 2.7f), ImVec2(c.x + u * 1.75f, c.y + u * 0.5f), tint(3, 0xF1F5F9FF), u * 0.3f);
    dl->AddCircleFilled(ImVec2(c.x - u * 1.43f, c.y + u * 0.7f), u * 0.3f, skin); dl->AddCircleFilled(ImVec2(c.x + u * 1.43f, c.y + u * 0.7f), u * 0.3f, skin);
    // torso (white tee) + head
    dl->AddRectFilled(ImVec2(c.x - u * 1.15f, c.y - u * 2.9f), ImVec2(c.x + u * 1.15f, c.y + u * 0.75f), tint(1, 0xF8FAFCFF), u * 0.35f);
    dl->AddRectFilled(ImVec2(c.x - u * 0.28f, c.y - u * 3.35f), ImVec2(c.x + u * 0.28f, c.y - u * 2.85f), skin);
    dl->AddCircleFilled(ImVec2(c.x, c.y - u * 4.0f), u * 0.78f, s_Inv.part[0] > 75 ? skin : tint(0, 0));
    dl->AddRectFilled(ImVec2(c.x - u * 0.8f, c.y - u * 4.85f), ImVec2(c.x + u * 0.8f, c.y - u * 4.35f), IM_COL32(245, 245, 245, 255), u * 0.25f);   // cap
    dl->AddRectFilled(ImVec2(c.x - u * 0.55f, c.y - u * 4.15f), ImVec2(c.x + u * 0.55f, c.y - u * 3.9f), IM_COL32(20, 20, 24, 255), u * 0.1f);   // shades
}
static void RpDrawInvWindow(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktRpInv& inv = s_Inv.inv;
    const float lw = UI::S(380), rw = UI::S(598), gap = UI::S(21), H = UI::S(531);
    const float x0 = ds.x * 0.5f - (lw + gap + rw) * 0.5f, y0 = ds.y * 0.269f > UI::S(10) ? ds.y * 0.269f : UI::S(10);
    float yTop = (y0 + H > ds.y - UI::S(10)) ? ds.y - UI::S(10) - H : y0;
    BcwBeginOverlay("##RpInv", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 body = IM_COL32(22, 30, 46, 205), white = IM_COL32(255, 255, 255, 255);
    char t[96];
    bool hitAny = false;
    // ---------------- PLAYER INFO ----------------
    {
        ImVec2 a(x0, yTop), b(x0 + lw, yTop + H);
        dl->AddRectFilled(a, b, body);
        RpInvHeader(dl, a, lw, "PLAYER INFO");
        RpPlainText(dl, UI::S(17), ImVec2(a.x + UI::S(8), a.y + UI::S(40)), white, settings.name);
        // the body + 6 condition boxes with lines
        ImVec2 bc(a.x + lw * 0.5f, a.y + UI::S(196));
        RpDrawBody(dl, bc, UI::S(300) * 0.62f);
        const float u = UI::S(300) * 0.62f / 10.0f;
        const ImVec2 to[6] = { ImVec2(bc.x - u * 0.4f, bc.y - u * 4.0f), ImVec2(bc.x + u * 0.5f, bc.y - u * 1.6f), ImVec2(bc.x - u * 1.43f, bc.y - u * 0.8f),
                               ImVec2(bc.x + u * 1.43f, bc.y - u * 0.8f), ImVec2(bc.x - u * 0.5f, bc.y + u * 3.0f), ImVec2(bc.x + u * 0.5f, bc.y + u * 3.0f) };
        const float rows[3] = { UI::S(122), UI::S(199), UI::S(298) };
        const float bw = UI::S(92), bh = UI::S(25);
        for(int i = 0; i < 6; ++i)
        {
            bool left = (i % 2) == 0; float yy = a.y + rows[i / 2] - bh * 0.5f;
            ImVec2 ba(left ? a.x + UI::S(34) : a.x + lw - UI::S(34) - bw, yy), bb(ba.x + bw, yy + bh);
            dl->AddRectFilled(ba, bb, IM_COL32(8, 10, 14, 215));
            snprintf(t, sizeof(t), "%d%%", (int)(s_Inv.part[i] + 0.5f));
            float q = s_Inv.part[i] / 100.0f;
            RpCenterIn(dl, UI::S(16), ba, bb, q > 0.75f ? white : IM_COL32(255, (int)(80 + 175 * q), (int)(80 * q), 255), t);
            RpPlainText(dl, UI::S(11), ImVec2(ba.x, ba.y - UI::S(13)), IM_COL32(200, 214, 228, 170), kRpParts[i]);
            ImVec2 from(left ? bb.x : ba.x, yy + bh * 0.5f);
            ImVec2 elbow(left ? from.x + UI::S(22) : from.x - UI::S(22), from.y);
            dl->AddLine(from, elbow, white, UI::S(2)); dl->AddLine(elbow, to[i], white, UI::S(2));
            dl->AddCircleFilled(to[i], UI::S(3), white);
        }
        // HEALTH / ARMOUR
        int ped = Game::LocalPed();
        int hp = ped > 0 ? Game::GetCharHealth(ped) : 0, ar = ped > 0 ? Game::GetCharArmour(ped) : 0;
        if(hp > 100) hp = 100; if(hp < 0) hp = 0; if(ar > 100) ar = 100; if(ar < 0) ar = 0;
        auto Bar = [&](float y, const char* label, int v, uint32_t col)
        {
            RpPlainText(dl, UI::S(16), ImVec2(a.x + UI::S(18), y), white, label);
            ImVec2 ba(a.x + UI::S(18), y + UI::S(20)), bb(a.x + UI::S(314), y + UI::S(34));
            dl->AddRectFilled(ba, bb, IM_COL32(40, 44, 54, 230));
            if(v > 0) dl->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * v / 100.0f, bb.y), RpCol(col, 1));
            snprintf(t, sizeof(t), "%d%%", v); RpPlainText(dl, UI::S(16), ImVec2(bb.x + UI::S(8), ba.y - UI::S(2)), white, t);
        };
        Bar(a.y + UI::S(366), "HEALTH", hp, 0xE53935FF);
        Bar(a.y + UI::S(414), "ARMOUR", ar, 0x93C5FDFF);
        if(RpInvButton("##ivclose", ImVec2(a.x + UI::S(10), a.y + UI::S(464)), ImVec2(lw - UI::S(20), UI::S(56)), kInvRed, "CLOSE", IM_COL32(60, 12, 12, 255))) s_Inv.win = RPW_NONE;
        if(ImGui::IsItemHovered()) hitAny = true;
    }
    // ---------------- INVENTORY ----------------
    {
        ImVec2 a(x0 + lw + gap, yTop), b(a.x + rw, yTop + H);
        dl->AddRectFilled(a, b, body);
        RpInvHeader(dl, a, rw, "INVENTORY");
        int w = s_Inv.have ? inv.weight : 0, mw = s_Inv.have && inv.maxWeight ? inv.maxWeight : RP_INV_MAX_W;
        snprintf(t, sizeof(t), "/%d.%dKg", mw / 10, mw % 10);
        float fs = UI::S(17), tw = RpTextW(fs, t);
        RpPlainText(dl, fs, ImVec2(b.x - tw - UI::S(8), a.y + UI::S(40)), white, t);
        char wv[16]; snprintf(wv, sizeof(wv), "%d.%d", w / 10, w % 10);
        RpPlainText(dl, fs, ImVec2(b.x - tw - UI::S(28) - RpTextW(fs, wv), a.y + UI::S(40)), w * 10 >= mw * 9 ? IM_COL32(255, 120, 120, 255) : white, wv);
        const float cw = UI::S(110), chh = UI::S(110), gx = UI::S(9), gy = UI::S(12);
        const float gx0 = a.x + (rw - (cw * 5 + gx * 4)) * 0.5f, gy0 = a.y + UI::S(77);
        for(int i = 0; i < RP_INV_SLOTS; ++i)
        {
            ImVec2 ca(gx0 + (i % 5) * (cw + gx), gy0 + (i / 5) * (chh + gy)), cb(ca.x + cw, ca.y + chh);
            const RpInvSlot& sl = inv.slot[i];
            bool sel = s_Inv.sel == i;
            dl->AddRectFilled(ca, cb, sel ? IM_COL32(24, 36, 74, 240) : IM_COL32(44, 46, 54, 225));
            if(sel) dl->AddRect(ca, cb, RpCol(kInvHead, 0.9f), 0, 0, UI::S(2));
            ImGui::SetCursorScreenPos(ca); ImGui::PushID(i);
            if(ImGui::InvisibleButton("##ivs", ImVec2(cw, chh)) && sl.id) { if(s_Inv.sel != i) { s_Inv.sel = i; s_Inv.amount = 1; } else s_Inv.sel = -1; }
            if(ImGui::IsItemHovered()) hitAny = true;
            ImGui::PopID();
            if(!sl.id || sl.id >= RPX_COUNT || !s_Inv.have) continue;
            RpItemIcon(dl, sl.id, ImVec2((ca.x + cb.x) * 0.5f, ca.y + chh * 0.52f), UI::S(70));
            snprintf(t, sizeof(t), "%d. %s", i + 1, g_RpInv[sl.id].name);
            dl->PushClipRect(ca, cb, true);
            UI::OutlinedText(dl, UI::S(13), ImVec2(ca.x + UI::S(6), ca.y + UI::S(5)), white, t);
            int iw = g_RpInv[sl.id].weight * sl.count;
            snprintf(t, sizeof(t), "%ux (%d.%d)", sl.count, iw / 10, iw % 10);
            float w2 = RpTextW(UI::S(13), t);
            UI::OutlinedText(dl, UI::S(13), ImVec2((ca.x + cb.x - w2) * 0.5f, cb.y - UI::S(19)), white, t);
            dl->PopClipRect();
        }
        if(!s_Inv.have) RpCenterIn(dl, UI::S(18), ImVec2(a.x, gy0), ImVec2(b.x, gy0 + chh * 3), IM_COL32(220, 230, 240, 200), "Opening the bag...");
        // AMOUNT / USE / GIVE / DROP
        const float bw = UI::S(125), bh = UI::S(56), by = a.y + UI::S(464);
        const float bgap = (rw - bw * 4) / 5.0f;
        char am[24]; if(s_Inv.amount > 1) snprintf(am, sizeof(am), "AMOUNT: %d", s_Inv.amount); else snprintf(am, sizeof(am), "AMOUNT");
        if(RpInvButton("##iva", ImVec2(a.x + bgap, by), ImVec2(bw, bh), kInvBtn, am))
        {
            char init[8]; snprintf(init, sizeof(init), "%d", s_Inv.amount);
            VKeyboard::Open("Amount", init, 2, RpInvKbSubmit, (void*)(intptr_t)RPKI_AMOUNT, true);
        }
        if(ImGui::IsItemHovered()) hitAny = true;
        if(RpInvButton("##ivu", ImVec2(a.x + bgap * 2 + bw, by), ImVec2(bw, bh), kInvBtn, "USE")) RpInvUse();
        if(ImGui::IsItemHovered()) hitAny = true;
        if(RpInvButton("##ivg", ImVec2(a.x + bgap * 3 + bw * 2, by), ImVec2(bw, bh), kInvBtn, "GIVE")) RpInvGive();
        if(ImGui::IsItemHovered()) hitAny = true;
        if(RpInvButton("##ivd", ImVec2(a.x + bgap * 4 + bw * 3, by), ImVec2(bw, bh), kInvBtn, "DROP")) RpInvDrop();
        if(ImGui::IsItemHovered()) hitAny = true;
    }
    // tap outside both panels = close
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inside = mp.x >= x0 && mp.x <= x0 + lw + gap + rw && mp.y >= yTop && mp.y <= yTop + H;
    bool outside = ImGui::IsMouseClicked(0) && !inside && !hitAny && nowMs - s_Inv.winAt > 300;
    BcwEndOverlay();
    if(outside) s_Inv.win = RPW_NONE;
}

// ---- DOCUMENTS -------------------------------------------------------------------------------
static void RpDrawDocsWindow(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktRpMe& m = s_Rp.me;
    const float W = UI::S(560), H = UI::S(470);
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H) * 0.5f), b(a.x + W, a.y + H);
    BcwBeginOverlay("##RpDocs", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 white = IM_COL32(255, 255, 255, 255), dim = IM_COL32(170, 200, 220, 255);
    dl->AddRectFilled(a, b, IM_COL32(22, 30, 46, 215));
    RpInvHeader(dl, a, W, "DOCUMENTS - MEGA VIL CITIZEN ID");
    char t[96];
    struct Row { const char* k; char v[64]; uint32_t c; } rows[9];
    int n = 0;
    auto Add = [&](const char* k, uint32_t c, const char* fmt, ...) { rows[n].k = k; rows[n].c = c; va_list ap; va_start(ap, fmt); vsnprintf(rows[n].v, sizeof(rows[n].v), fmt, ap); va_end(ap); ++n; };
    Add("NAME", 0xFFFFFFFF, "%s", settings.name);
    Add("ID", 0xFFFFFFFF, "%d", myId);
    Add("PHONE", 0x7DD3FCFF, "%d", m.number);
    Add("JOB", 0xFFFFFFFF, "%s%s  (level %d)", g_RpJobs[m.job < RPJ_COUNT ? m.job : 0].name, m.duty ? " - on duty" : "", m.level);
    Add("HOME", 0xFFFFFFFF, "%s", m.house < RP_HOUSE_COUNT ? g_RpHouses[m.house].addr : "none (Lobby Hub)");
    Add("PLAYED", 0xFFFFFFFF, "%u h %02u min%s", m.playMin / 60, m.playMin % 60, m.newbieS ? "  - new citizen" : "");
    Add("DRIVING", (m.items & RPIT_DRIVING) ? 0x4ADE80FF : 0xF87171FF, "%s", (m.items & RPIT_DRIVING) ? "Licensed" : "No licence (DMV / Tech Store)");
    Add("WEAPONS", (m.items & RPIT_GUNLIC) ? 0x4ADE80FF : 0xF87171FF, "%s", (m.items & RPIT_GUNLIC) ? "Licensed (duty only)" : "No licence");
    Add("JETPACK", (m.items & RPIT_JETPACK) ? 0x4ADE80FF : 0xF87171FF, "%s", (m.items & RPIT_JETPACK) ? "Licensed" : "No licence");
    for(int i = 0; i < n; ++i)
    {
        float y = a.y + UI::S(50) + i * UI::S(34);
        RpPlainText(dl, UI::S(18), ImVec2(a.x + UI::S(22), y), dim, rows[i].k);
        RpPlainText(dl, UI::S(18), ImVec2(a.x + UI::S(150), y), RpCol(rows[i].c, 1), rows[i].v);
    }
    const float bw = (W - UI::S(36)) * 0.5f, bh = UI::S(56), by = b.y - bh - UI::S(12);
    bool hit = false;
    if(RpInvButton("##dshow", ImVec2(a.x + UI::S(12), by), ImVec2(bw, bh), kInvBtn, "SHOW ID"))
    {
        snprintf(t, sizeof(t), "shows a Mega Vil Citizen ID: %s, phone %d, %s.", settings.name, m.number, g_RpJobs[m.job < RPJ_COUNT ? m.job : 0].name);
        RpChat(RPH_ME, t);
    }
    if(ImGui::IsItemHovered()) hit = true;
    if(RpInvButton("##dclose", ImVec2(a.x + UI::S(24) + bw, by), ImVec2(bw, bh), kInvRed, "CLOSE", IM_COL32(60, 12, 12, 255))) s_Inv.win = RPW_NONE;
    if(ImGui::IsItemHovered()) hit = true;
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool outside = ImGui::IsMouseClicked(0) && !hit && !(mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= b.y) && nowMs - s_Inv.winAt > 300;
    BcwEndOverlay();
    if(outside) s_Inv.win = RPW_NONE;
}

// ---- "Received 10x" cards (screenshot 084514) ---------------------------------------------------
static void RpDrawGotCards(uint64_t nowMs)
{
    // drop the old ones (15 s + 20 ticks)
    int w = 0;
    for(int i = 0; i < s_Inv.nGot; ++i) if(nowMs - s_Inv.got[i].at < RP_GOT_SHOW_MS + RP_GOT_FADE_MS) s_Inv.got[w++] = s_Inv.got[i];
    s_Inv.nGot = w;
    if(!s_Inv.nGot) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float cw = UI::S(164), ch = UI::S(168), gap = UI::S(6), bar = UI::S(6);
    const float bottom = ds.y * 0.89f;
    float x = ds.x * 0.58f;
    if(x + s_Inv.nGot * (cw + gap) > ds.x - UI::S(12)) x = ds.x - UI::S(12) - s_Inv.nGot * (cw + gap);
    char t[48];
    for(int i = 0; i < s_Inv.nGot; ++i)
    {
        const RpGotCard& c = s_Inv.got[i];
        uint64_t e = nowMs - c.at;
        float alpha = e < RP_GOT_SHOW_MS ? 1.0f : 1.0f - (float)(e - RP_GOT_SHOW_MS) / RP_GOT_FADE_MS;
        if(alpha <= 0) continue;
        float in = e < 180 ? e / 180.0f : 1.0f;   // a short slide up when it comes in
        ImVec2 a(x + i * (cw + gap), bottom - ch + (1.0f - in) * UI::S(24)), b(a.x + cw, a.y + ch);
        dl->AddRectFilled(a, b, IM_COL32(78, 86, 106, (int)(215 * alpha)));
        snprintf(t, sizeof(t), "Received %dx", c.count);
        UI::OutlinedText(dl, UI::S(15), ImVec2(a.x + UI::S(8), a.y + UI::S(7)), IM_COL32(255, 255, 255, 255), t, alpha);
        RpItemIcon(dl, c.id, ImVec2((a.x + b.x) * 0.5f, a.y + ch * 0.48f), UI::S(78), alpha);
        const char* nm = g_RpInv[c.id].name; float nw = RpTextW(UI::S(16), nm);
        UI::OutlinedText(dl, UI::S(16), ImVec2((a.x + b.x - nw) * 0.5f, b.y - bar - UI::S(30)), IM_COL32(255, 255, 255, 255), nm, alpha);
        // the pink bar runs out in 15 s
        float left = e < RP_GOT_SHOW_MS ? 1.0f - (float)e / RP_GOT_SHOW_MS : 0.0f;
        dl->AddRectFilled(ImVec2(a.x, b.y - bar), b, IM_COL32(40, 40, 48, (int)(200 * alpha)));
        if(left > 0) dl->AddRectFilled(ImVec2(a.x, b.y - bar), ImVec2(a.x + cw * left, b.y), IM_COL32(240, 106, 122, (int)(255 * alpha)));
    }
}

// ---- the PANEL button next to the phone ---------------------------------------------------------
static void RpDrawPanelButton()
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float s = UI::S(92);
    // left of the phone icon (phone centre: 560 from the right edge, 60 % of the height)
    ImVec2 c(ds.x - UI::S(560) - UI::S(140), ds.y * 0.60f);
    BcwBeginOverlay("##RpPanelBtn", ImVec2(c.x - s * 0.5f - UI::S(6), c.y - s * 0.5f - UI::S(6)), ImVec2(s + UI::S(12), s + UI::S(30)), 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f));
    if(ImGui::InvisibleButton("##pnb", ImVec2(s, s))) RpPanelToggle();
    bool down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = s * 0.5f * (down ? 0.9f : 1.0f);
    RpHex(dl, c, r, IM_COL32(20, 26, 40, RpInvOpen() ? 235 : 170));
    RpPanelGlyph(dl, 2, ImVec2(c.x, c.y - UI::S(6)), s * 0.38f, IM_COL32_WHITE);
    float fs = UI::S(13), w = RpTextW(fs, "PANEL");
    RpText(dl, fs, ImVec2(c.x - w * 0.5f, c.y + s * 0.14f), RpCol(0x67E8F9FF, 1), "PANEL");
    BcwEndOverlay();
}

static void RenderRpInv()
{
    const uint64_t nowMs = MP_NowMs();
    if(s_Rp.view != 2) { s_Inv.panel = false; s_Inv.win = RPW_NONE; return; }
    RpInvBodyTick(Game::LocalPed(), nowMs);
    if(!s_Inv.have && nowMs - s_Inv.askedAt > 10000 && s_Rp.haveMe) { s_Inv.askedAt = nowMs; RpSend(RPC_INV_REQ); }
    RpDrawGotCards(nowMs);
    if(SampDialogOpen() && !VKeyboard::IsOpen()) return;
    if(s_Rp.phone) { s_Inv.panel = false; s_Inv.win = RPW_NONE; }
    RpDrawPanelButton();
    if(VKeyboard::IsOpen()) return;
    if(s_Inv.panel) RpDrawPanel(nowMs);
    else if(s_Inv.win == RPW_INV) RpDrawInvWindow(nowMs);
    else if(s_Inv.win == RPW_DOCS) RpDrawDocsWindow(nowMs);
}
