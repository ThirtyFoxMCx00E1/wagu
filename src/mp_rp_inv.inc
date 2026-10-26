// MP 3.26 - Mega Vil Futuristic: PANEL, status + bag (inventory), documents and the "Received"
// toasts (included by mp.cpp after mp_rp_phone.inc). Own layout, sky blue on dark glass:
//   - PANEL: a radial wheel of 5 segments (Vehicle, Documents, Status, Bag, Phone) around a close button
//   - BAG (3.30): CITIZEN (body condition figure + chips, 4 vitals) next to BACKPACK (weight meter,
//     5 x 3 tile grid over the 16 slots, amount stepper, USE / GIVE / DROP pills, round X close)
//   - DOCUMENTS: a Mega Vil citizen ID card (photo, fields, licence pills, barcode), SHOW ID / CLOSE
//   - "Received" toasts stacked on the right ("+10 Iced Tea - Added to your bag"); each stays 15 s,
//     its time line running out, then fades + slides off in 20 ticks (1 tick = 50 ms)
// The bag itself lives on the server (net/server_rp.inc, PKT_RP_INV); this file only draws and asks.

#define RP_GOT_SHOW_MS  15000   // a "Received" card stays 15 s...
#define RP_GOT_FADE_MS  (20 * 50)   // ...then fades off in 20 ticks
#define RP_GOT_MAX      5

struct RpGotCard { uint8_t id; int count; uint64_t at; };
struct RpInvClient
{
    PktRpInv inv{}; bool have = false; uint64_t askedAt = 0;
    bool     panel = false; uint64_t panelAt = 0; int hot = -1;   // the PANEL wheel segment under the finger
    int      win = 0;                                             // 0 none, 1 player info + inventory, 2 documents
    uint64_t winAt = 0;
    int      sel = -1, amount = 1; float scroll = 0;   // 3.30: the grid scroll (px)
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
    const int minLen = (int)offsetof(PktRpInv, slot) + 15 * (int)sizeof(RpInvSlot);   // 3.29: a 3.26 - 3.28 server sends 15 slots
    if(len < minLen) return;
    memset(&s_Inv.inv, 0, sizeof(PktRpInv));
    memcpy(&s_Inv.inv, d, len < (int)sizeof(PktRpInv) ? len : sizeof(PktRpInv)); s_Inv.have = true;
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
    RpClick(RPS_CARD);   // 3.29: click3.mp3 for a new "Received" card
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
// the PANEL glyphs (white line art)
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
    s_Inv.panel = false; s_Inv.win = win; s_Inv.winAt = MP_NowMs(); s_Inv.scroll = 0;
    if(win == RPW_INV && MP_NowMs() - s_Inv.askedAt > 1500) { s_Inv.askedAt = MP_NowMs(); RpSend(RPC_INV_REQ); }
    s_Rp.phone = false;
}
static void RpPanelToggle()
{
    if(!RpOnServer()) return;
    if(s_Inv.win) { s_Inv.win = RPW_NONE; return; }
    s_Inv.panel = !s_Inv.panel; s_Inv.panelAt = MP_NowMs(); s_Inv.hot = -1;
    if(s_Inv.panel) s_Rp.phone = false;
}

// ---- shared look (3.26 own layout: rounded dark glass, sky blue accents) ------------------------
static const uint32_t kUiSky = 0x38BDF8FF, kUiSkyHi = 0x7DD3FCFF, kUiRed = 0xF87171FF, kUiGreen = 0x4ADE80FF, kUiAmber = 0xFBBF24FF;
static const ImU32 kUiGlass = IM_COL32(12, 18, 32, 236), kUiTile = IM_COL32(28, 38, 58, 230), kUiDim = IM_COL32(148, 170, 196, 255);
static uint32_t RpCondCol(float q) { return q > 0.75f ? kUiGreen : q > 0.40f ? kUiAmber : kUiRed; }
static bool RpUiButton(const char* id, ImVec2 a, ImVec2 sz, ImU32 fill, ImU32 line, const char* label, ImU32 tc, float fs = 0)
{
    ImGui::SetCursorScreenPos(a);
    bool r = ImGui::InvisibleButton(id, sz), down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rr = sz.y * 0.5f;
    if(fill) dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), down ? (fill & 0x00FFFFFF) | (((fill >> 24) * 3 / 4) << 24) : fill, rr);
    if(line) dl->AddRect(a, ImVec2(a.x + sz.x, a.y + sz.y), line, rr, 0, UI::S(2.5f));
    RpCenterIn(dl, fs > 0 ? fs : UI::S(19), a, ImVec2(a.x + sz.x, a.y + sz.y), tc, label);
    if(r) RpClick(RPS_CLICK);   // 3.29: click.mp3
    return r;
}
static void RpRing(ImDrawList* dl, ImVec2 c, float r, float th, float v, uint32_t col, const char* label)
{   // a round gauge: dark track, coloured arc from the top, value in the middle, label under it
    dl->AddCircle(c, r, IM_COL32(40, 52, 74, 255), 48, th);
    if(v > 0) { dl->PathArcTo(c, r, -IM_PI * 0.5f, -IM_PI * 0.5f + IM_PI * 2.0f * (v > 100 ? 1.0f : v / 100.0f), 48); dl->PathStroke(RpCol(col, 1), 0, th); }
    char t[8]; snprintf(t, sizeof(t), "%d", (int)(v + 0.5f));
    RpCenterIn(dl, UI::S(22), ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32_WHITE, t);
    float w = RpTextW(UI::S(13), label);
    RpPlainText(dl, UI::S(13), ImVec2(c.x - w * 0.5f, c.y + r + UI::S(8)), kUiDim, label);
}
static const char* RpItemDesc(const RpInvDef& d, char* b, int n)
{
    switch(d.use)
    {
        case RPU_FOOD:    snprintf(b, n, "Food  -  fills %d hunger", d.hunger); break;
        case RPU_DRINK:   snprintf(b, n, "Drink  -  fills %d thirst", d.thirst); break;
        case RPU_MEDKIT:  snprintf(b, n, "Heals %d health", d.hunger); break;
        case RPU_REPAIR:  snprintf(b, n, "Repairs the car you sit in"); break;
        case RPU_PHONE:   snprintf(b, n, "Opens your phone"); break;
        case RPU_STARTER: snprintf(b, n, "Starter pack  -  open it to unpack"); break;
        default:          snprintf(b, n, "Item"); break;
    }
    return b;
}

// ---- PANEL: a radial wheel (5 segments around a close button) ------------------------------------
static void RpDrawPanel(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float k = (nowMs - s_Inv.panelAt) / 160.0f; if(k > 1) k = 1;
    float ease = 1.0f - (1.0f - k) * (1.0f - k);
    BcwBeginOverlay("##RpPanel", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(4, 8, 16, (int)(110 * k)));   // dim the world a little
    const ImVec2 c(ds.x * 0.5f, ds.y * 0.5f);
    const float R = UI::S(250) * (0.82f + 0.18f * ease), r = UI::S(92) * (0.82f + 0.18f * ease), mid = (R + r) * 0.5f, th = R - r;
    static const char* const kLabels[5] = { "VEHICLE", "DOCUMENTS", "STATUS", "BAG", "PHONE" };
    static const int kGlyph[5] = { 0, 1, 2, 3, 4 };
    const float seg = IM_PI * 2.0f / 5.0f, gapA = 0.035f;
    // input: one button over the whole wheel; the angle picks the segment
    ImGui::SetCursorScreenPos(ImVec2(c.x - R, c.y - R));
    bool clicked = ImGui::InvisibleButton("##wheel", ImVec2(R * 2, R * 2));
    bool over = ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImVec2 mp = ImGui::GetIO().MousePos; float dx = mp.x - c.x, dy = mp.y - c.y, d = sqrtf(dx * dx + dy * dy);
    int at = -1; bool atCenter = d < r;
    if(d >= r && d <= R) { float a = atan2f(dy, dx) + IM_PI * 0.5f; if(a < 0) a += IM_PI * 2.0f; at = ((int)floorf(a / seg + 0.5f)) % 5; }
    s_Inv.hot = over ? at : -1;
    // title
    {
        float fs = UI::S(30); const char* t = "PANEL"; float w = RpTextW(fs, t);
        RpText(dl, fs, ImVec2(c.x - w * 0.5f, c.y - R - UI::S(64)), RpCol(0xFFFFFFFF, k), t);
        const char* sub = "MEGA VIL QUICK MENU"; float sw = RpTextW(UI::S(14), sub);
        RpPlainText(dl, UI::S(14), ImVec2(c.x - sw * 0.5f, c.y - R - UI::S(28)), RpCol(kUiSkyHi, k), sub);
    }
    for(int i = 0; i < 5; ++i)
    {
        float a0 = -IM_PI * 0.5f + (i - 0.5f) * seg + gapA, a1 = -IM_PI * 0.5f + (i + 0.5f) * seg - gapA;
        bool hot = s_Inv.hot == i;
        dl->PathArcTo(c, mid, a0, a1, 32); dl->PathStroke(hot ? RpCol(kUiSky, 0.95f * k) : IM_COL32(16, 24, 42, (int)(228 * k)), 0, th);
        dl->PathArcTo(c, R - UI::S(3), a0, a1, 32); dl->PathStroke(RpCol(kUiSky, (hot ? 1.0f : 0.55f) * k), 0, UI::S(3));
        float am = -IM_PI * 0.5f + i * seg;
        ImVec2 ic(c.x + cosf(am) * mid, c.y + sinf(am) * mid);
        RpPanelGlyph(dl, kGlyph[i], ImVec2(ic.x, ic.y - UI::S(12)), UI::S(46), RpCol(0xFFFFFFFF, k));
        float fs = UI::S(15), w = RpTextW(fs, kLabels[i]);
        RpPlainText(dl, fs, ImVec2(ic.x - w * 0.5f, ic.y + UI::S(22)), hot ? IM_COL32(8, 20, 34, (int)(255 * k)) : RpCol(kUiSkyHi, k), kLabels[i]);
    }
    // centre: close
    bool hotC = over && atCenter;
    dl->AddCircleFilled(c, r - UI::S(10), hotC ? RpCol(kUiRed, 0.95f * k) : IM_COL32(10, 14, 24, (int)(240 * k)), 48);
    dl->AddCircle(c, r - UI::S(10), RpCol(kUiSky, 0.8f * k), 48, UI::S(2.5f));
    RpCenterIn(dl, UI::S(34), ImVec2(c.x - r, c.y - r - UI::S(12)), ImVec2(c.x + r, c.y + r - UI::S(12)), RpCol(0xFFFFFFFF, k), "X");
    { const char* t = "CLOSE"; float w = RpTextW(UI::S(13), t); RpPlainText(dl, UI::S(13), ImVec2(c.x - w * 0.5f, c.y + UI::S(16)), RpCol(0xCBD5E1FF, k), t); }
    bool outside = ImGui::IsMouseClicked(0) && d > R;
    BcwEndOverlay();
    if(clicked) RpClick(RPS_CLICK);
    if(outside || (clicked && atCenter)) { s_Inv.panel = false; return; }
    if(!clicked || at < 0) return;
    switch(at)
    {
        case 0: s_Inv.panel = false; RpPhoneOpen(RPA_GARAGE); break;
        case 1: RpInvShow(RPW_DOCS); break;
        case 2: case 3: RpInvShow(RPW_INV); break;
        case 4: s_Inv.panel = false; RpPhoneOpen(s_Rp.ringFrom >= 0 || s_Rp.callWith >= 0 ? -2 : RPA_HOME); break;
    }
}

// ---- BAG (3.30): our own two-panel layout, centred on the screen
//   - left  "CITIZEN": round badge + name, a body figure whose parts are tinted by their condition,
//     6 condition chips joined to the body by thin lines, and 4 vitals (health, armour, food, water)
//   - right "BACKPACK": weight meter, a 5 x 3 grid of rounded tiles over the 16 slots (drag the grid
//     for the last row), the picked item's line, an amount stepper (- n +) and USE / GIVE / DROP pills,
//     and a round X in the corner to close
static const ImU32 kInvAcc = IM_COL32(56, 189, 248, 255), kInvAccDn = IM_COL32(14, 140, 200, 255), kInvInk = IM_COL32(6, 22, 38, 255);
static const ImU32 kInvGlass = IM_COL32(11, 17, 30, 234), kInvTile = IM_COL32(26, 36, 56, 236), kInvTileSel = IM_COL32(18, 58, 92, 250);
static const ImU32 kInvRed = IM_COL32(239, 68, 68, 255), kInvDim = IM_COL32(148, 166, 190, 255);
enum { RPIB_FILL = 0, RPIB_LINE, RPIB_RED };
static bool RpInvBtn(const char* id, ImVec2 a, ImVec2 sz, int style, const char* label, bool enabled = true, float rr = -1)
{
    ImGui::SetCursorScreenPos(a);
    bool r = ImGui::InvisibleButton(id, sz), down = ImGui::IsItemActive() && enabled;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(a.x + sz.x, a.y + sz.y);
    if(rr < 0) rr = sz.y * 0.5f;
    const float al = enabled ? 1.0f : 0.40f;
    ImU32 tc;
    if(style == RPIB_FILL) { dl->AddRectFilled(a, b, RpCol(down ? 0x0E8CC8FF : 0x38BDF8FF, al), rr); tc = RpCol(0x061626FF, al); }
    else
    {
        uint32_t c = style == RPIB_RED ? 0xEF4444FF : 0x38BDF8FF;
        dl->AddRectFilled(a, b, down ? RpCol(c, 0.30f) : IM_COL32(16, 26, 44, (int)(230 * al)), rr);
        dl->AddRect(a, b, RpCol(c, al), rr, 0, UI::S(2));
        tc = RpCol(style == RPIB_RED ? 0xFCA5A5FF : 0xE0F2FEFF, al);
    }
    RpCenterIn(dl, UI::S(17), a, b, tc, label);
    if(r && enabled) RpClick(RPS_CLICK);
    return r && enabled;
}
// a panel: rounded dark glass, a faint accent edge, a round glyph + title + subtitle, a line under it
static void RpInvPanel(ImDrawList* dl, ImVec2 a, ImVec2 b, int glyph, const char* title, const char* sub, float k)
{
    dl->AddRectFilled(a, b, (kInvGlass & 0x00FFFFFF) | ((ImU32)(234 * k) << 24), UI::S(18));
    dl->AddRect(a, b, RpCol(0x38BDF8FF, 0.30f * k), UI::S(18), 0, UI::S(1.5f));
    ImVec2 gc(a.x + UI::S(36), a.y + UI::S(32));
    dl->AddCircleFilled(gc, UI::S(19), RpCol(0x38BDF8FF, 0.18f), 28);
    dl->AddCircle(gc, UI::S(19), kInvAcc, 28, UI::S(2));
    RpPanelGlyph(dl, glyph, gc, UI::S(22), IM_COL32(224, 242, 254, 255));
    RpPlainText(dl, UI::S(20), ImVec2(a.x + UI::S(66), a.y + UI::S(11)), IM_COL32_WHITE, title);
    RpPlainText(dl, UI::S(13), ImVec2(a.x + UI::S(67), a.y + UI::S(36)), kInvDim, sub);
    dl->AddRectFilledMultiColor(ImVec2(a.x + UI::S(16), a.y + UI::S(62)), ImVec2(b.x - UI::S(16), a.y + UI::S(64)),
                                kInvAcc, IM_COL32(56, 189, 248, 0), IM_COL32(56, 189, 248, 0), kInvAcc);
}
// the body figure: every part tinted by its condition (ice white -> amber -> red), with a dark edge
static ImU32 RpInvPartCol(float q)
{
    if(q > 0.75f) return IM_COL32(186, 230, 253, 255);
    if(q > 0.40f) return IM_COL32(251, 191, 36, 255);
    return IM_COL32(248, 96, 96, 255);
}
static void RpInvFigure(ImDrawList* dl, float cx, float top, float s)
{
    const float* p = s_Inv.part;   // head, torso, left arm, right arm, left leg, right leg
    const ImU32 edge = IM_COL32(2, 8, 18, 200);
    const float arm = UI::S(15) * s, leg = UI::S(20) * s, e = UI::S(4) * s;
    auto Limb = [&](ImVec2 a, ImVec2 m, ImVec2 b, float th, ImU32 c)
    {
        dl->AddLine(a, m, edge, th + e); dl->AddLine(m, b, edge, th + e);
        dl->AddCircleFilled(m, (th + e) * 0.5f, edge, 12); dl->AddCircleFilled(b, (th + e) * 0.5f, edge, 12);
        dl->AddLine(a, m, c, th); dl->AddLine(m, b, c, th);
        dl->AddCircleFilled(m, th * 0.5f, c, 12); dl->AddCircleFilled(b, th * 0.5f, c, 12);
    };
    auto P = [&](float x, float y) { return ImVec2(cx + UI::S(x) * s, top + UI::S(y) * s); };
    // a soft floor shadow, legs, arms, then the torso and the head on top
    dl->AddRectFilled(P(-46, 250), P(46, 262), IM_COL32(56, 189, 248, 40), UI::S(6) * s);
    Limb(P(-15, 140), P(-18, 194), P(-20, 246), leg, RpInvPartCol(p[4] / 100.0f));
    Limb(P( 15, 140), P( 18, 194), P( 20, 246), leg, RpInvPartCol(p[5] / 100.0f));
    Limb(P(-40,  60), P(-52, 110), P(-56, 156), arm, RpInvPartCol(p[2] / 100.0f));
    Limb(P( 40,  60), P( 52, 110), P( 56, 156), arm, RpInvPartCol(p[3] / 100.0f));
    ImU32 tc = RpInvPartCol(p[1] / 100.0f);
    dl->AddQuadFilled(P(-44, 50), P(44, 50), P(32, 146), P(-32, 146), edge);
    dl->AddQuadFilled(P(-41, 52), P(41, 52), P(30, 143), P(-30, 143), tc);
    dl->AddRectFilled(P(-9, 36), P(9, 54), edge);
    dl->AddRectFilled(P(-7, 36), P(7, 54), RpInvPartCol(p[0] / 100.0f));
    dl->AddCircleFilled(P(0, 20), UI::S(22) * s, edge, 28);
    dl->AddCircleFilled(P(0, 20), UI::S(19) * s, RpInvPartCol(p[0] / 100.0f), 28);
}
// a vital: label + value on one line, a rounded bar under it
static void RpInvVital(ImDrawList* dl, ImVec2 a, float w, const char* label, float v, ImU32 col)
{
    char t[16]; snprintf(t, sizeof(t), "%d", (int)(v + 0.5f));
    RpPlainText(dl, UI::S(13), a, kInvDim, label);
    float tw = RpTextW(UI::S(14), t);
    RpPlainText(dl, UI::S(14), ImVec2(a.x + w - tw, a.y - UI::S(1)), IM_COL32_WHITE, t);
    ImVec2 ba(a.x, a.y + UI::S(21)), bb(a.x + w, a.y + UI::S(30));
    dl->AddRectFilled(ba, bb, IM_COL32(34, 46, 68, 255), UI::S(5));
    float q = v / 100.0f; if(q > 1) q = 1; if(q < 0) q = 0;
    if(q > 0.02f) dl->AddRectFilled(ba, ImVec2(ba.x + (bb.x - ba.x) * q, bb.y), col, UI::S(5));
}
static void RpDrawInvWindow(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGuiIO& io = ImGui::GetIO();
    const PktRpInv& inv = s_Inv.inv;
    float k = (nowMs - s_Inv.winAt) / 160.0f; if(k > 1) k = 1;
    // sizes: the backpack sets the height, the citizen panel sits on its left
    const int COLS = 5, VROWS = 3;
    const float rw = UI::S(660), lw = UI::S(420), gapP = UI::S(20), pad = UI::S(18), gap = UI::S(10);
    const float tw = (rw - pad * 2 - gap * (COLS - 1)) / COLS, th = tw * 0.86f;
    const float viewH = th * VROWS + gap * (VROWS - 1);
    const float top0 = UI::S(64) + UI::S(52);   // header + weight meter
    const float H = top0 + viewH + UI::S(48) + UI::S(54) + pad;
    const float totalW = lw + gapP + rw;
    float x0 = (ds.x - totalW) * 0.5f, y0 = (ds.y - H) * 0.5f + (1.0f - k) * UI::S(30);
    if(y0 < UI::S(8)) y0 = UI::S(8);
    ImVec2 la(x0, y0), lb(x0 + lw, y0 + H), ra(lb.x + gapP, y0), rb(ra.x + rw, y0 + H);
    BcwBeginOverlay("##RpInv", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 white = IM_COL32_WHITE;
    char t[96];
    // ---------------- CITIZEN ----------------
    snprintf(t, sizeof(t), "%s  -  ID %d", settings.name, myId);
    RpInvPanel(dl, la, lb, 2, "CITIZEN", t, k);
    {
        const float vy0 = lb.y - pad - UI::S(84) - UI::S(30);   // where the vitals start (with room)
        float fs = (vy0 - (la.y + UI::S(84))) / UI::S(268); if(fs > 1.25f) fs = 1.25f; if(fs < 0.7f) fs = 0.7f;
        const float cx = (la.x + lb.x) * 0.5f, top = la.y + UI::S(84) + (vy0 - (la.y + UI::S(84)) - UI::S(268) * fs) * 0.5f;
        RpInvFigure(dl, cx, top, fs);
        // 6 chips: head / left arm / left leg on the left, torso / right arm / right leg on the right
        struct Tag { int part; bool right; float ty; float ax, ay; };
        static const Tag kTags[6] = {
            { 0, false,   4, -18,  20 }, { 2, false,  92, -50, 108 }, { 4, false, 180, -18, 196 },
            { 1, true,    4,  30,  78 }, { 3, true,   92,  50, 108 }, { 5, true,  180,  18, 196 } };
        const float bw = UI::S(114), bh = UI::S(32);
        for(const Tag& g : kTags)
        {
            float q = s_Inv.part[g.part];
            float bx = g.right ? lb.x - pad - bw : la.x + pad, by = top + UI::S(g.ty) * fs;
            ImVec2 ba(bx, by), bb(bx + bw, by + bh);
            ImU32 qc = RpCol(RpCondCol(q / 100.0f), 1);
            dl->AddRectFilled(ba, bb, IM_COL32(20, 30, 50, 245), bh * 0.5f);
            dl->AddRect(ba, bb, RpCol(RpCondCol(q / 100.0f), 0.55f), bh * 0.5f, 0, UI::S(1.5f));
            dl->AddCircleFilled(ImVec2(ba.x + UI::S(14), (ba.y + bb.y) * 0.5f), UI::S(4.5f), qc, 12);
            RpPlainText(dl, UI::S(12), ImVec2(ba.x + UI::S(24), ba.y + UI::S(8)), kInvDim, kRpParts[g.part]);
            snprintf(t, sizeof(t), "%d%%", (int)(q + 0.5f));
            float pw = RpTextW(UI::S(14), t);
            RpPlainText(dl, UI::S(14), ImVec2(bb.x - UI::S(12) - pw, ba.y + UI::S(7)), white, t);
            // a thin line from the chip to the body part
            ImVec2 from(g.right ? ba.x : bb.x, (ba.y + bb.y) * 0.5f), to(cx + UI::S(g.ax) * fs, top + UI::S(g.ay) * fs);
            dl->AddLine(from, to, IM_COL32(186, 230, 253, 120), UI::S(1.5f));
            dl->AddCircle(to, UI::S(4), kInvAcc, 12, UI::S(1.5f));
        }
        // 4 vitals in 2 x 2
        int ped = Game::LocalPed();
        float hp = ped > 0 ? (float)Game::GetCharHealth(ped) : 0, ar = ped > 0 ? (float)Game::GetCharArmour(ped) : 0;
        const PktRpMe& m = s_Rp.me;
        const float vw = (lw - pad * 3) * 0.5f, vy = lb.y - pad - UI::S(84);
        dl->AddLine(ImVec2(la.x + pad, vy - UI::S(14)), ImVec2(lb.x - pad, vy - UI::S(14)), IM_COL32(255, 255, 255, 26), UI::S(1));
        RpInvVital(dl, ImVec2(la.x + pad, vy), vw, "HEALTH", hp, kInvRed);
        RpInvVital(dl, ImVec2(la.x + pad * 2 + vw, vy), vw, "ARMOUR", ar, IM_COL32(203, 213, 225, 255));
        RpInvVital(dl, ImVec2(la.x + pad, vy + UI::S(46)), vw, "FOOD", s_Rp.haveMe ? m.hunger : 0, IM_COL32(251, 146, 60, 255));
        RpInvVital(dl, ImVec2(la.x + pad * 2 + vw, vy + UI::S(46)), vw, "WATER", s_Rp.haveMe ? m.thirst : 0, kInvAcc);
    }
    // ---------------- BACKPACK ----------------
    int used = 0; if(s_Inv.have) for(int i = 0; i < RP_INV_SLOTS; ++i) if(inv.slot[i].id && inv.slot[i].id < RPX_COUNT) ++used;
    snprintf(t, sizeof(t), "%d of %d slots used", used, RP_INV_SLOTS);
    RpInvPanel(dl, ra, rb, 3, "BACKPACK", t, k);
    // close: a round X in the corner
    {
        ImVec2 xc(rb.x - UI::S(34), ra.y + UI::S(32));
        ImGui::SetCursorScreenPos(ImVec2(xc.x - UI::S(24), xc.y - UI::S(24)));
        bool r = ImGui::InvisibleButton("##ivx", ImVec2(UI::S(48), UI::S(48)));
        bool dn = ImGui::IsItemActive();
        dl->AddCircleFilled(xc, UI::S(19), dn ? kInvRed : IM_COL32(40, 20, 28, 240), 28);
        dl->AddCircle(xc, UI::S(19), kInvRed, 28, UI::S(2));
        const float q = UI::S(7);
        dl->AddLine(ImVec2(xc.x - q, xc.y - q), ImVec2(xc.x + q, xc.y + q), white, UI::S(2.5f));
        dl->AddLine(ImVec2(xc.x + q, xc.y - q), ImVec2(xc.x - q, xc.y + q), white, UI::S(2.5f));
        if(r) { RpClick(RPS_CLICK); s_Inv.win = RPW_NONE; }
    }
    bool have = s_Inv.sel >= 0 && s_Inv.sel < RP_INV_SLOTS && s_Inv.have && inv.slot[s_Inv.sel].id && inv.slot[s_Inv.sel].id < RPX_COUNT;
    {   // weight meter
        int w = s_Inv.have ? inv.weight : 0, mw = s_Inv.have && inv.maxWeight ? inv.maxWeight : RP_INV_MAX_W;
        float y = ra.y + UI::S(76);
        RpPlainText(dl, UI::S(13), ImVec2(ra.x + pad, y), kInvDim, "WEIGHT");
        snprintf(t, sizeof(t), "%d.%d / %d.%d kg", w / 10, w % 10, mw / 10, mw % 10);
        bool heavy = w * 10 >= mw * 9;
        float ww = RpTextW(UI::S(14), t);
        RpPlainText(dl, UI::S(14), ImVec2(rb.x - pad - ww, y - UI::S(1)), heavy ? kInvRed : white, t);
        ImVec2 ma(ra.x + pad + UI::S(70), y + UI::S(5)), mb(rb.x - pad - ww - UI::S(14), y + UI::S(13));
        dl->AddRectFilled(ma, mb, IM_COL32(34, 46, 68, 255), UI::S(4));
        float q = mw > 0 ? (float)w / mw : 0; if(q > 1) q = 1;
        if(q > 0.01f) dl->AddRectFilled(ma, ImVec2(ma.x + (mb.x - ma.x) * q, mb.y), heavy ? kInvRed : kInvAcc, UI::S(4));
    }
    // the grid: a 5 x 3 window over the 16 slots; drag it (or the wheel) for the last row
    ImVec2 va(ra.x + pad, ra.y + top0), vb(rb.x - pad, va.y + viewH);
    const int rows = (RP_INV_SLOTS + COLS - 1) / COLS;
    const float contentH = rows * th + (rows - 1) * gap, maxS = contentH > viewH ? contentH - viewH : 0;
    ImGui::SetCursorScreenPos(va);
    bool gridTap = ImGui::InvisibleButton("##ivgrid", ImVec2(vb.x - va.x, vb.y - va.y));
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0, UI::S(6))) s_Inv.scroll -= io.MouseDelta.y;
    if(ImGui::IsItemHovered() && io.MouseWheel != 0) s_Inv.scroll -= io.MouseWheel * (th + gap);
    if(s_Inv.scroll < 0) s_Inv.scroll = 0;
    if(s_Inv.scroll > maxS) s_Inv.scroll = maxS;
    if(gridTap && io.MouseDragMaxDistanceSqr[0] < UI::S(12) * UI::S(12))
    {
        float lx = io.MousePos.x - va.x, ly = io.MousePos.y - va.y + s_Inv.scroll;
        int c = (int)(lx / (tw + gap)), r = (int)(ly / (th + gap)), i = r * COLS + c;
        bool onTile = lx >= 0 && ly >= 0 && c < COLS && lx - c * (tw + gap) <= tw && ly - r * (th + gap) <= th;
        if(onTile && i < RP_INV_SLOTS && s_Inv.have && inv.slot[i].id && inv.slot[i].id < RPX_COUNT)
        { RpClick(RPS_CLICK); if(s_Inv.sel != i) { s_Inv.sel = i; s_Inv.amount = 1; } else s_Inv.sel = -1; }
    }
    dl->PushClipRect(ImVec2(va.x - UI::S(3), va.y), ImVec2(vb.x + UI::S(3), vb.y), true);
    for(int i = 0; i < rows * COLS; ++i)
    {
        int c = i % COLS, r = i / COLS;
        ImVec2 ca(va.x + c * (tw + gap), va.y + r * (th + gap) - s_Inv.scroll), cb(ca.x + tw, ca.y + th);
        if(cb.y < va.y || ca.y > vb.y) continue;
        if(i >= RP_INV_SLOTS) { dl->AddRect(ca, cb, IM_COL32(255, 255, 255, 18), UI::S(12), 0, UI::S(1.5f)); continue; }   // no slot here
        bool full = s_Inv.have && inv.slot[i].id && inv.slot[i].id < RPX_COUNT, sel = s_Inv.sel == i;
        dl->AddRectFilled(ca, cb, sel ? kInvTileSel : kInvTile, UI::S(12));
        if(sel) dl->AddRect(ca, cb, kInvAcc, UI::S(12), 0, UI::S(3));
        else dl->AddRect(ca, cb, IM_COL32(255, 255, 255, 16), UI::S(12), 0, UI::S(1.5f));
        if(!full) continue;
        const RpInvSlot& sl = inv.slot[i];
        RpItemIcon(dl, sl.id, ImVec2((ca.x + cb.x) * 0.5f, ca.y + th * 0.44f), th * 0.50f);
        // the count in a small pill top-right, the name along the bottom
        snprintf(t, sizeof(t), "%u", sl.count);
        float cw2 = RpTextW(UI::S(13), t) + UI::S(14);
        ImVec2 pa(cb.x - cw2 - UI::S(6), ca.y + UI::S(6)), pb(cb.x - UI::S(6), ca.y + UI::S(26));
        dl->AddRectFilled(pa, pb, sel ? kInvAcc : IM_COL32(56, 189, 248, 70), UI::S(10));
        RpCenterIn(dl, UI::S(13), pa, pb, sel ? kInvInk : white, t);
        dl->PushClipRect(ImVec2(ca.x + UI::S(6), ca.y), ImVec2(cb.x - UI::S(6), cb.y), true);
        float nw = RpTextW(UI::S(13), g_RpInv[sl.id].name);
        RpPlainText(dl, UI::S(13), ImVec2(nw < tw - UI::S(12) ? (ca.x + cb.x - nw) * 0.5f : ca.x + UI::S(6), cb.y - UI::S(22)), IM_COL32(224, 242, 254, 255), g_RpInv[sl.id].name);
        dl->PopClipRect();
    }
    dl->PopClipRect();
    if(maxS > 0)
    {   // a thin scroll bar right of the grid
        float bx = vb.x + UI::S(6), bh2 = viewH * viewH / contentH, by = va.y + (viewH - bh2) * (s_Inv.scroll / maxS);
        dl->AddRectFilled(ImVec2(bx, va.y), ImVec2(bx + UI::S(4), vb.y), IM_COL32(255, 255, 255, 26), UI::S(2));
        dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + UI::S(4), by + bh2), kInvAcc, UI::S(2));
    }
    if(!s_Inv.have) RpCenterIn(dl, UI::S(18), va, vb, IM_COL32(230, 230, 230, 220), "Opening the bag...");
    // the picked item's line
    {
        float y = vb.y + UI::S(13);
        if(have)
        {
            const RpInvDef& d = g_RpInv[inv.slot[s_Inv.sel].id];
            char desc[64]; RpItemDesc(d, desc, sizeof(desc));
            dl->PushClipRect(ImVec2(ra.x + pad, y - UI::S(4)), ImVec2(rb.x - pad, y + UI::S(30)), true);
            RpPlainText(dl, UI::S(16), ImVec2(ra.x + pad, y), white, d.name);
            RpPlainText(dl, UI::S(15), ImVec2(ra.x + pad + RpTextW(UI::S(16), d.name) + UI::S(14), y + UI::S(1)), kInvAcc, desc);
            dl->PopClipRect();
        }
        else RpPlainText(dl, UI::S(15), ImVec2(ra.x + pad, y), kInvDim, "Tap an item to pick it.");
    }
    // amount stepper (- n +) and the USE / GIVE / DROP pills
    {
        const float bh = UI::S(50), bg = UI::S(10), by = rb.y - pad - bh;
        const float sw = UI::S(178), pw = (rw - pad * 2 - sw - bg * 3) / 3.0f;
        float x = ra.x + pad;
        int mx = have ? inv.slot[s_Inv.sel].count : 1;
        dl->AddRectFilled(ImVec2(x, by), ImVec2(x + sw, by + bh), IM_COL32(16, 26, 44, 240), bh * 0.5f);
        dl->AddRect(ImVec2(x, by), ImVec2(x + sw, by + bh), IM_COL32(56, 189, 248, 110), bh * 0.5f, 0, UI::S(1.5f));
        if(RpInvBtn("##ivm", ImVec2(x + UI::S(4), by + UI::S(4)), ImVec2(bh - UI::S(8), bh - UI::S(8)), RPIB_LINE, "-", have && s_Inv.amount > 1)) --s_Inv.amount;
        if(RpInvBtn("##ivp", ImVec2(x + sw - bh + UI::S(4), by + UI::S(4)), ImVec2(bh - UI::S(8), bh - UI::S(8)), RPIB_LINE, "+", have && s_Inv.amount < mx)) ++s_Inv.amount;
        {   // the number in the middle: tap it to type
            ImVec2 na(x + bh, by), nsz(sw - bh * 2, bh);
            ImGui::SetCursorScreenPos(na);
            if(ImGui::InvisibleButton("##iva", nsz) && have)
            { RpClick(RPS_CLICK); char init[8]; snprintf(init, sizeof(init), "%d", s_Inv.amount); VKeyboard::Open("Amount", init, 2, RpInvKbSubmit, (void*)(intptr_t)RPKI_AMOUNT, true); }
            snprintf(t, sizeof(t), "%d", have ? s_Inv.amount : 0);
            RpCenterIn(dl, UI::S(19), ImVec2(na.x, na.y - UI::S(6)), ImVec2(na.x + nsz.x, na.y + nsz.y - UI::S(6)), have ? white : kInvDim, t);
            RpCenterIn(dl, UI::S(11), ImVec2(na.x, na.y + UI::S(26)), ImVec2(na.x + nsz.x, na.y + nsz.y), kInvDim, "AMOUNT");
        }
        x += sw + bg;
        if(RpInvBtn("##ivu", ImVec2(x, by), ImVec2(pw, bh), RPIB_FILL, "USE", have)) RpInvUse();
        x += pw + bg;
        if(RpInvBtn("##ivg", ImVec2(x, by), ImVec2(pw, bh), RPIB_LINE, "GIVE", have)) RpInvGive();
        x += pw + bg;
        if(RpInvBtn("##ivd", ImVec2(x, by), ImVec2(pw, bh), RPIB_RED, "DROP", have)) RpInvDrop();
    }
    ImVec2 mp = io.MousePos;
    bool inside = (mp.x >= la.x && mp.x <= lb.x && mp.y >= la.y && mp.y <= lb.y) || (mp.x >= ra.x && mp.x <= rb.x + UI::S(12) && mp.y >= ra.y && mp.y <= rb.y);
    bool outside = ImGui::IsMouseClicked(0) && !inside && nowMs - s_Inv.winAt > 300;
    BcwEndOverlay();
    if(outside) s_Inv.win = RPW_NONE;
}

// ---- DOCUMENTS: a citizen ID card ----------------------------------------------------------------
static void RpDrawDocsWindow(uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktRpMe& m = s_Rp.me;
    const float W = UI::S(680), H = UI::S(400);
    float k = (nowMs - s_Inv.winAt) / 160.0f; if(k > 1) k = 1;
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H - UI::S(80)) * 0.5f + (1.0f - k) * UI::S(30)), b(a.x + W, a.y + H);
    BcwBeginOverlay("##RpDocs", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 white = IM_COL32_WHITE, ink = IM_COL32(8, 20, 34, 255), inkDim = IM_COL32(20, 60, 90, 255);
    char t[96];
    // the card: sky blue, a lighter band on top, rounded
    dl->AddRectFilled(a, b, IM_COL32(186, 230, 253, 252), UI::S(26));
    dl->AddRectFilled(a, ImVec2(b.x, a.y + UI::S(74)), RpCol(kUiSky, 1), UI::S(26), ImDrawFlags_RoundCornersTop);
    RpPlainText(dl, UI::S(30), ImVec2(a.x + UI::S(28), a.y + UI::S(10)), white, "MEGA VIL");
    RpPlainText(dl, UI::S(14), ImVec2(a.x + UI::S(30), a.y + UI::S(46)), IM_COL32(224, 242, 254, 255), "CITIZEN IDENTITY CARD");
    snprintf(t, sizeof(t), "No. %06d", m.number ? m.number : myId); float nw = RpTextW(UI::S(17), t);
    RpPlainText(dl, UI::S(17), ImVec2(b.x - UI::S(28) - nw, a.y + UI::S(27)), white, t);
    // photo
    ImVec2 pa(a.x + UI::S(28), a.y + UI::S(98)), pb(pa.x + UI::S(170), pa.y + UI::S(210));
    dl->AddRectFilled(pa, pb, IM_COL32(125, 211, 252, 255), UI::S(16));
    ImVec2 pc((pa.x + pb.x) * 0.5f, pa.y + UI::S(82));
    dl->AddCircleFilled(pc, UI::S(36), IM_COL32(14, 116, 144, 255), 40);
    dl->AddRectFilled(ImVec2(pc.x - UI::S(62), pc.y + UI::S(46)), ImVec2(pc.x + UI::S(62), pb.y), IM_COL32(14, 116, 144, 255), UI::S(40), ImDrawFlags_RoundCornersTop);
    // fields, 2 columns
    struct F { const char* k; char v[48]; } f[6];
    snprintf(f[0].v, 48, "%s", settings.name); f[0].k = "NAME";
    snprintf(f[1].v, 48, "%d", myId); f[1].k = "PLAYER ID";
    snprintf(f[2].v, 48, "%d", m.number); f[2].k = "PHONE";
    snprintf(f[3].v, 48, "%s%s (lv %d)", g_RpJobs[m.job < RPJ_COUNT ? m.job : 0].name, m.duty ? ", on duty" : "", m.level); f[3].k = "JOB";
    snprintf(f[4].v, 48, "%s", m.house < RP_HOUSE_COUNT ? g_RpHouses[m.house].addr : "Lobby Hub"); f[4].k = "HOME";
    snprintf(f[5].v, 48, "%u h %02u min", m.playMin / 60, m.playMin % 60); f[5].k = "TIME IN CITY";
    const float fx = pb.x + UI::S(26), colW = (b.x - UI::S(24) - fx) * 0.5f;
    for(int i = 0; i < 6; ++i)
    {
        float x = fx + (i % 2) * colW, y = pa.y + (i / 2) * UI::S(66);
        RpPlainText(dl, UI::S(13), ImVec2(x, y), inkDim, f[i].k);
        dl->PushClipRect(ImVec2(x, y), ImVec2(x + colW - UI::S(10), y + UI::S(50)), true);
        RpPlainText(dl, UI::S(19), ImVec2(x, y + UI::S(18)), ink, f[i].v);
        dl->PopClipRect();
    }
    // licences as pills
    struct L { const char* n; bool on; } lic[3] = { { "DRIVING", (m.items & RPIT_DRIVING) != 0 }, { "WEAPONS", (m.items & RPIT_GUNLIC) != 0 }, { "JETPACK", (m.items & RPIT_JETPACK) != 0 } };
    float lx = fx, ly = pa.y + UI::S(204);
    for(int i = 0; i < 3; ++i)
    {
        snprintf(t, sizeof(t), "%s %s", lic[i].n, lic[i].on ? "OK" : "--");
        float w = RpTextW(UI::S(14), t) + UI::S(26);
        dl->AddRectFilled(ImVec2(lx, ly - UI::S(18)), ImVec2(lx + w, ly + UI::S(14)), lic[i].on ? IM_COL32(22, 163, 74, 255) : IM_COL32(148, 163, 184, 255), UI::S(16));
        RpCenterIn(dl, UI::S(14), ImVec2(lx, ly - UI::S(18)), ImVec2(lx + w, ly + UI::S(14)), white, t);
        lx += w + UI::S(10);
    }
    // barcode from the name
    {
        unsigned hsh = 2166136261u; for(const char* p = settings.name; *p; ++p) hsh = (hsh ^ (uint8_t)*p) * 16777619u;
        float x = a.x + UI::S(28), y0 = b.y - UI::S(62), y1 = b.y - UI::S(24);
        while(x < b.x - UI::S(28))
        {
            hsh = hsh * 1103515245u + 12345u; float w = UI::S(2 + (hsh >> 16) % 4);
            dl->AddRectFilled(ImVec2(x, y0), ImVec2(x + w, y1), ink); x += w + UI::S(2 + (hsh >> 20) % 4);
        }
    }
    // buttons under the card
    const float bw = (W - UI::S(16)) * 0.5f, bh = UI::S(54), by = b.y + UI::S(18);
    bool hit = false;
    if(RpUiButton("##dshow", ImVec2(a.x, by), ImVec2(bw, bh), RpCol(kUiSky, 1), 0, "SHOW ID", ink))
    {
        snprintf(t, sizeof(t), "shows a Mega Vil Citizen ID: %s, phone %d, %s.", settings.name, m.number, g_RpJobs[m.job < RPJ_COUNT ? m.job : 0].name);
        RpChat(RPH_ME, t);
    }
    if(ImGui::IsItemHovered()) hit = true;
    if(RpUiButton("##dclose", ImVec2(a.x + bw + UI::S(16), by), ImVec2(bw, bh), kUiGlass, RpCol(kUiSky, 0.6f), "CLOSE", white)) s_Inv.win = RPW_NONE;
    if(ImGui::IsItemHovered()) hit = true;
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool outside = ImGui::IsMouseClicked(0) && !hit && !(mp.x >= a.x && mp.x <= b.x && mp.y >= a.y && mp.y <= by + bh) && nowMs - s_Inv.winAt > 300;
    BcwEndOverlay();
    if(outside) s_Inv.win = RPW_NONE;
}

// ---- "Received" toasts: stacked on the right, newest at the bottom ------------------------------
static void RpDrawGotCards(uint64_t nowMs)
{
    int w = 0;
    for(int i = 0; i < s_Inv.nGot; ++i) if(nowMs - s_Inv.got[i].at < RP_GOT_SHOW_MS + RP_GOT_FADE_MS) s_Inv.got[w++] = s_Inv.got[i];
    s_Inv.nGot = w;
    if(!s_Inv.nGot) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float cw = UI::S(340), ch = UI::S(80), gap = UI::S(10);
    const float right = ds.x - UI::S(28), bottom = ds.y * 0.86f;
    char t[48];
    for(int j = 0; j < s_Inv.nGot; ++j)
    {
        int i = s_Inv.nGot - 1 - j;   // newest at the bottom
        const RpGotCard& c = s_Inv.got[i];
        uint64_t e = nowMs - c.at;
        float alpha = e < RP_GOT_SHOW_MS ? 1.0f : 1.0f - (float)(e - RP_GOT_SHOW_MS) / RP_GOT_FADE_MS;
        if(alpha <= 0) continue;
        float in = e < 200 ? e / 200.0f : 1.0f;
        float slide = (1.0f - in) * UI::S(60) + (1.0f - alpha) * UI::S(40);   // in from the right, out to the right
        ImVec2 a(right - cw + slide, bottom - ch - j * (ch + gap)), b(a.x + cw, a.y + ch);
        dl->AddRectFilled(a, b, IM_COL32(12, 18, 32, (int)(232 * alpha)), UI::S(18));
        dl->AddRectFilled(a, ImVec2(a.x + UI::S(7), b.y), RpCol(kUiSky, alpha), UI::S(18), ImDrawFlags_RoundCornersLeft);
        ImVec2 ic(a.x + UI::S(50), (a.y + b.y) * 0.5f);
        dl->AddCircleFilled(ic, UI::S(30), IM_COL32(30, 44, 70, (int)(255 * alpha)), 32);
        RpItemIcon(dl, c.id, ic, UI::S(50), alpha);
        snprintf(t, sizeof(t), "+%d", c.count);
        float pw = RpTextW(UI::S(21), t);
        RpPlainText(dl, UI::S(21), ImVec2(a.x + UI::S(94), a.y + UI::S(13)), RpCol(kUiSkyHi, alpha), t);
        RpPlainText(dl, UI::S(21), ImVec2(a.x + UI::S(102) + pw, a.y + UI::S(13)), RpCol(0xFFFFFFFF, alpha), g_RpInv[c.id].name);
        RpPlainText(dl, UI::S(14), ImVec2(a.x + UI::S(94), a.y + UI::S(43)), IM_COL32(148, 170, 196, (int)(255 * alpha)), "Added to your bag");
        // the time line runs out in 15 s
        float left = e < RP_GOT_SHOW_MS ? 1.0f - (float)e / RP_GOT_SHOW_MS : 0.0f;
        ImVec2 la(a.x + UI::S(94), b.y - UI::S(12)), lb(b.x - UI::S(18), b.y - UI::S(8));
        dl->AddRectFilled(la, lb, IM_COL32(40, 52, 74, (int)(255 * alpha)), UI::S(2));
        if(left > 0) dl->AddRectFilled(la, ImVec2(la.x + (lb.x - la.x) * left, lb.y), RpCol(kUiSky, alpha), UI::S(2));
    }
}

// ---- the PANEL button (2 x 2 squares) -------------------------------------------------------------
// 3.28: only the icon - no round background / ring / label - and it took the old phone button's spot.
// The unread SMS badge moved here (the phone is in the PANEL).
static void RpDrawPanelButton()
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float s = UI::S(96);
    ImVec2 c(ds.x - UI::S(560), ds.y * 0.60f);
    BcwBeginOverlay("##RpPanelBtn", ImVec2(c.x - s * 0.5f - UI::S(6), c.y - s * 0.5f - UI::S(6)), ImVec2(s + UI::S(12), s + UI::S(12)), 0.0f);
    ImGui::SetCursorScreenPos(ImVec2(c.x - s * 0.5f, c.y - s * 0.5f));
    if(ImGui::InvisibleButton("##pnb", ImVec2(s, s))) RpPanelToggle();
    bool down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const uint64_t nowMs = MP_NowMs();
    bool ring = s_Rp.ringFrom >= 0 && !s_Rp.ringOut && s_Rp.callWith < 0;
    ImU32 col = s_Inv.panel ? RpCol(kUiSkyHi, 1) : ((ring && nowMs % 600 < 300) ? IM_COL32(120, 255, 160, 255) : IM_COL32_WHITE);
    float k = down ? 0.9f : 1.0f;
    const float q = UI::S(26) * k, o = UI::S(17) * k, sh = UI::S(2.5f);
    for(int i = 0; i < 4; ++i)
    {
        float x = c.x + ((i % 2) ? o : -o), y = c.y + ((i / 2) ? o : -o);
        // a thin dark outline so the white squares read on any background
        dl->AddRectFilled(ImVec2(x - q * 0.5f - sh, y - q * 0.5f - sh), ImVec2(x + q * 0.5f + sh, y + q * 0.5f + sh), IM_COL32(0, 0, 0, 150), UI::S(6));
        dl->AddRectFilled(ImVec2(x - q * 0.5f, y - q * 0.5f), ImVec2(x + q * 0.5f, y + q * 0.5f), col, UI::S(5));
    }
    if(s_Rp.unreadSms)
    {
        const ImVec2 b(c.x + o + q * 0.5f, c.y - o - q * 0.5f);
        dl->AddCircleFilled(b, UI::S(14), IM_COL32(239, 68, 68, 255));
        char n[8]; snprintf(n, sizeof(n), "%d", s_Rp.unreadSms > 9 ? 9 : s_Rp.unreadSms);
        float w = RpTextW(UI::S(18), n);
        RpPlainText(dl, UI::S(18), ImVec2(b.x - w * 0.5f, b.y - UI::S(10)), IM_COL32_WHITE, n);
    }
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
    if(s_Inv.win == RPW_NONE) RpDrawPanelButton();   // hidden while the bag / documents are up (it sat on their edge)
    if(VKeyboard::IsOpen()) return;
    if(s_Inv.panel) RpDrawPanel(nowMs);
    else if(s_Inv.win == RPW_INV) RpDrawInvWindow(nowMs);
    else if(s_Inv.win == RPW_DOCS) RpDrawDocsWindow(nowMs);
}
