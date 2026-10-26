// MP 3.29 - Mega Vil Futuristic: a more alive city (included by mp.cpp after mp_rp_inv.inc)
//   - real ATM machines (GTA model 2942) at every ATM, and an ATM screen: cash, bank, deposit,
//     withdraw, transfer, and investments (one city market: Bond, Holo-Tech Stocks, VilCoin)
//   - info people at the Mega Vil Plaza (the lobby / spawn) with floating text: the server, rules,
//     jobs, money, phone, commands
//   - a welcome message in the chat when you join
//   - commands from Diverse Roleplay: /invest /sellinvest /market, /propose /divorce, /seatbelt
//     /checkseatbelt, /pawn

// ---- ATM machines ----------------------------------------------------------------------------
// rz = which way the screen faces (0 = the machine stands 0.9 m north of the ring, screen to the
// south, so you stand in the ring and look at it)
#define RP_ATM_MODEL 2942
struct RpAtmObj { int obj; };
static RpAtmObj s_RpAtm[64];
static uint64_t s_nRpAtmRetry = 0;
static float RpAtmYaw(int place)
{
    const char* n = g_RpPlaces[place].name;
    if(strstr(n, "Pershing")) return 90.0f;    // against the Pershing Square wall (west side)
    if(strstr(n, "Market"))   return 180.0f;
    return 0.0f;
}
static Vec3 RpAtmObjPos(int i, float& rz)
{
    rz = RpAtmYaw(i);
    float r = rz * 3.14159265f / 180.0f;
    // the screen faces local -Y; the machine stands behind the ring (opposite way)
    float bx = -sinf(r), by = cosf(r);
    return { g_RpPlaces[i].x + bx * 0.9f, g_RpPlaces[i].y + by * 0.9f, g_RpPlaces[i].z - 0.42f };
}
static void RpAtmClear()
{
    for(int i = 0; i < 64; ++i) { if(s_RpAtm[i].obj > 0) Game::DeleteWorldObject(s_RpAtm[i].obj); s_RpAtm[i].obj = 0; }
}
static void RpAtmStream(uint64_t now, int ped)
{
    if(now < s_nRpAtmRetry || !Game::IsReady() || Game::IsPaused() || ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    if(!(me.x == me.x) || me.z < -50.0f) return;
    int made = 0;
    for(int i = 0; i < RP_PLACE_COUNT && i < 64; ++i)
    {
        if(g_RpPlaces[i].kind != RPK_ATM) continue;
        float dx = g_RpPlaces[i].x - me.x, dy = g_RpPlaces[i].y - me.y, d2 = dx * dx + dy * dy;
        if(s_RpAtm[i].obj > 0)
        {
            if(d2 > 260.0f * 260.0f || !Game::WorldObjectExists(s_RpAtm[i].obj)) { if(Game::WorldObjectExists(s_RpAtm[i].obj)) Game::DeleteWorldObject(s_RpAtm[i].obj); s_RpAtm[i].obj = 0; }
            continue;
        }
        if(d2 > 200.0f * 200.0f || made >= 2) continue;
        float rz; Vec3 p = RpAtmObjPos(i, rz);
        int h = Game::CreateMapObject(RP_ATM_MODEL, p, 0.0f, 0.0f, rz);
        if(h == 0) { s_nRpAtmRetry = now + 150; return; }   // model loading
        if(h < 0) { s_nRpAtmRetry = now + 3000; return; }   // object pool nearly full
        s_RpAtm[i].obj = h; ++made;
    }
}

// ---- the ATM screen --------------------------------------------------------------------------
static struct { bool open = false; int tab = 0; uint64_t at = 0; int place = -1; } s_Atm;
enum { RPKB_ATM_DEP = 200, RPKB_ATM_WD, RPKB_ATM_INV0, RPKB_ATM_INV1, RPKB_ATM_INV2, RPKB_ATM_SELL0, RPKB_ATM_SELL1, RPKB_ATM_SELL2 };
static void RpAtmKb(const char* t, void* data)
{
    int mode = (int)(intptr_t)data, v = t ? atoi(t) : 0;
    if(v <= 0) return;
    if(mode == RPKB_ATM_DEP) RpSend(RPC_DEPOSIT, 0, 0, v);
    else if(mode == RPKB_ATM_WD) RpSend(RPC_WITHDRAW, 0, 0, v);
    else if(mode >= RPKB_ATM_INV0 && mode <= RPKB_ATM_INV2) RpSend(RPC_INVEST, (uint8_t)(mode - RPKB_ATM_INV0), 0, v);
    else if(mode >= RPKB_ATM_SELL0 && mode <= RPKB_ATM_SELL2) RpSend(RPC_INVEST, (uint8_t)(mode - RPKB_ATM_SELL0), 1, v);
}
// nearest ATM / bank you stand at (ATM: 2.6 m, bank: 7 m), -1 = none
static int RpAtmHere(int ped)
{
    if(ped <= 0 || Game::IsCharInAnyCar(ped)) return -1;
    Vec3 p; Game::GetCharPos(ped, p);
    int best = -1; float bd = 1e9f;
    for(int i = 0; i < RP_PLACE_COUNT; ++i)
    {
        uint8_t k = g_RpPlaces[i].kind; if(k != RPK_ATM && k != RPK_BANK) continue;
        float dx = g_RpPlaces[i].x - p.x, dy = g_RpPlaces[i].y - p.y, dz = g_RpPlaces[i].z - p.z, d2 = dx * dx + dy * dy;
        float r = k == RPK_ATM ? 2.6f : 7.0f;
        if(d2 < r * r && fabsf(dz) < 3.0f && d2 < bd) { bd = d2; best = i; }
    }
    return best;
}
static void RpAtmOpen(int place) { s_Atm.open = true; s_Atm.tab = 0; s_Atm.at = MP_NowMs(); s_Atm.place = place; RpClick(RPS_CLICK); }

static void RpDrawAtm(uint64_t nowMs)
{
    int ped = Game::LocalPed();
    int here = RpAtmHere(ped);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(!s_Atm.open)
    {   // walk up to an ATM: an outlined "ATM" button right of centre
        if(here < 0 || RpInvOpen() || s_Rp.phone || s_Rp.talkStage) return;
        BcwBeginOverlay("##RpAtmBtn", ImVec2(ds.x * 0.62f, ds.y * 0.42f), ImVec2(UI::S(300), UI::S(90)), 0.0f);
        char l[64]; snprintf(l, sizeof(l), "%s##atmb", g_RpPlaces[here].kind == RPK_BANK ? "Mega Bank" : "Use ATM");
        if(RpOutlineButton(l, ImVec2(UI::S(260), UI::S(62)))) RpAtmOpen(here);
        BcwEndOverlay();
        return;
    }
    if(here < 0) { s_Atm.open = false; return; }   // walked away
    const PktRpMe& m = s_Rp.me;
    const float W = UI::S(760), H = UI::S(560);
    float k = (nowMs - s_Atm.at) / 160.0f; if(k > 1) k = 1;
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H) * 0.5f + (1.0f - k) * UI::S(30)), b(a.x + W, a.y + H);
    BcwBeginOverlay("##RpAtm", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 white = IM_COL32_WHITE, green = IM_COL32(74, 222, 128, 255);
    char t[128];
    dl->AddRectFilled(a, b, IM_COL32(6, 16, 12, 242), UI::S(18));
    dl->AddRect(a, b, IM_COL32(22, 163, 74, 255), UI::S(18), 0, UI::S(3));
    dl->AddRectFilled(a, ImVec2(b.x, a.y + UI::S(70)), IM_COL32(22, 163, 74, 255), UI::S(18), ImDrawFlags_RoundCornersTop);
    RpPlainText(dl, UI::S(28), ImVec2(a.x + UI::S(24), a.y + UI::S(10)), white, "MEGA BANK");
    RpPlainText(dl, UI::S(14), ImVec2(a.x + UI::S(26), a.y + UI::S(44)), IM_COL32(220, 252, 231, 255), g_RpPlaces[s_Atm.place >= 0 ? s_Atm.place : here].name);
    char mb1[24], mb2[24];
    snprintf(mb1, sizeof(mb1), "$%d", m.cash); snprintf(mb2, sizeof(mb2), "$%d", m.bank);
    snprintf(t, sizeof(t), "CASH %s    BANK %s", mb1, mb2);
    float tw = RpTextW(UI::S(19), t); RpPlainText(dl, UI::S(19), ImVec2(b.x - UI::S(24) - tw, a.y + UI::S(24)), white, t);
    // tabs
    static const char* const kTabs[3] = { "CASH", "INVEST", "TRANSFER" };
    float tbw = (W - UI::S(48) - UI::S(24)) / 3.0f;
    for(int i = 0; i < 3; ++i)
    {
        char id[16]; snprintf(id, sizeof(id), "##atmt%d", i);
        if(RpUiButton(id, ImVec2(a.x + UI::S(24) + i * (tbw + UI::S(12)), a.y + UI::S(86)), ImVec2(tbw, UI::S(46)),
                      s_Atm.tab == i ? IM_COL32(22, 163, 74, 255) : IM_COL32(20, 40, 30, 255), 0, kTabs[i], white)) s_Atm.tab = i;
    }
    float y = a.y + UI::S(150), x0 = a.x + UI::S(24), cw = W - UI::S(48);
    if(s_Atm.tab == 0)
    {
        static const int kAmt[5] = { 100, 500, 1000, 5000, 0 };
        float bw = (cw - UI::S(4 * 10)) / 5.0f;
        RpPlainText(dl, UI::S(18), ImVec2(x0, y), green, "WITHDRAW (bank -> cash)");
        for(int i = 0; i < 5; ++i)
        {
            char id[16], lb[24]; snprintf(id, sizeof(id), "##aw%d", i);
            if(kAmt[i]) snprintf(lb, sizeof(lb), "$%d", kAmt[i]); else snprintf(lb, sizeof(lb), "Other");
            if(RpUiButton(id, ImVec2(x0 + i * (bw + UI::S(10)), y + UI::S(30)), ImVec2(bw, UI::S(56)), IM_COL32(20, 40, 30, 255), IM_COL32(22, 163, 74, 255), lb, white))
            { if(kAmt[i]) RpSend(RPC_WITHDRAW, 0, 0, kAmt[i]); else VKeyboard::Open("Withdraw amount", "", 9, RpAtmKb, (void*)(intptr_t)RPKB_ATM_WD, true); }
        }
        y += UI::S(110);
        RpPlainText(dl, UI::S(18), ImVec2(x0, y), green, "DEPOSIT (cash -> bank)");
        for(int i = 0; i < 5; ++i)
        {
            char id[16], lb[24]; snprintf(id, sizeof(id), "##ad%d", i);
            int v = kAmt[i];
            if(i == 4) snprintf(lb, sizeof(lb), "All cash"); else snprintf(lb, sizeof(lb), "$%d", v);
            if(RpUiButton(id, ImVec2(x0 + i * (bw + UI::S(10)), y + UI::S(30)), ImVec2(bw, UI::S(56)), IM_COL32(20, 40, 30, 255), IM_COL32(22, 163, 74, 255), lb, white))
            { if(i == 4) { if(m.cash > 0) RpSend(RPC_DEPOSIT, 0, 0, m.cash); } else RpSend(RPC_DEPOSIT, 0, 0, v); }
        }
        y += UI::S(110);
        if(RpUiButton("##adk", ImVec2(x0, y), ImVec2(cw, UI::S(52)), 0, IM_COL32(22, 163, 74, 255), "Deposit another amount", white))
            VKeyboard::Open("Deposit amount", "", 9, RpAtmKb, (void*)(intptr_t)RPKB_ATM_DEP, true);
    }
    else if(s_Atm.tab == 1)
    {
        RpPlainText(dl, UI::S(15), ImVec2(x0, y - UI::S(6)), IM_COL32(187, 247, 208, 255), "One market for the whole city. Prices move every 5 minutes. Buying uses your bank money.");
        y += UI::S(22);
        for(int i = 0; i < RPN_COUNT; ++i)
        {
            ImVec2 ra(x0, y), rb(x0 + cw, y + UI::S(96));
            dl->AddRectFilled(ra, rb, IM_COL32(16, 32, 24, 255), UI::S(12));
            int price = s_Rp.haveState ? s_Rp.st.market[i] : 0; int chg = s_Rp.haveState ? s_Rp.st.marketChg[i] : 0;
            RpPlainText(dl, UI::S(19), ImVec2(ra.x + UI::S(14), ra.y + UI::S(8)), white, g_RpPlans[i].name);
            snprintf(t, sizeof(t), "price %d  (%+.1f%%)   %s", price, chg / 10.0f, g_RpPlans[i].risk);
            RpPlainText(dl, UI::S(14), ImVec2(ra.x + UI::S(14), ra.y + UI::S(36)), chg >= 0 ? green : IM_COL32(248, 113, 113, 255), t);
            snprintf(t, sizeof(t), "yours: $%d", m.invest[i]);
            RpPlainText(dl, UI::S(17), ImVec2(ra.x + UI::S(14), ra.y + UI::S(62)), IM_COL32(253, 230, 138, 255), t);
            float bx = rb.x - UI::S(14) - UI::S(130) * 3 - UI::S(16);
            char id[16];
            snprintf(id, sizeof(id), "##ib5%d", i);
            if(RpUiButton(id, ImVec2(bx, ra.y + UI::S(24)), ImVec2(UI::S(130), UI::S(48)), IM_COL32(22, 163, 74, 255), 0, "Buy $500", white)) RpSend(RPC_INVEST, (uint8_t)i, 0, 500);
            snprintf(id, sizeof(id), "##ibk%d", i);
            if(RpUiButton(id, ImVec2(bx + UI::S(138), ra.y + UI::S(24)), ImVec2(UI::S(130), UI::S(48)), 0, IM_COL32(22, 163, 74, 255), "Buy ...", white))
                VKeyboard::Open("Invest amount", "", 9, RpAtmKb, (void*)(intptr_t)(RPKB_ATM_INV0 + i), true);
            snprintf(id, sizeof(id), "##isa%d", i);
            if(RpUiButton(id, ImVec2(bx + UI::S(276), ra.y + UI::S(24)), ImVec2(UI::S(130), UI::S(48)), 0, IM_COL32(248, 113, 113, 255), "Sell all", white)) RpSend(RPC_INVEST, (uint8_t)i, 1, 0);
            y += UI::S(106);
        }
    }
    else
    {
        RpPlainText(dl, UI::S(17), ImVec2(x0, y), white, "Send bank money to a phone number (works anywhere: /transfer [number] [amount]).");
        y += UI::S(40);
        if(RpUiButton("##atr", ImVec2(x0, y), ImVec2(cw, UI::S(60)), IM_COL32(22, 163, 74, 255), 0, "Transfer to a phone number", white))
            VKeyboard::Open("Phone number", "", 9, RpKbSubmit, (void*)(intptr_t)RPKB_TR_NUM, true);
        y += UI::S(80);
        snprintf(t, sizeof(t), "Your number: %d   Next payday in %d:%02d", m.number, m.paydayS / 60, m.paydayS % 60);
        RpPlainText(dl, UI::S(17), ImVec2(x0, y), IM_COL32(187, 247, 208, 255), t);
    }
    if(RpUiButton("##atx", ImVec2(b.x - UI::S(24) - UI::S(180), b.y - UI::S(24) - UI::S(52)), ImVec2(UI::S(180), UI::S(52)), IM_COL32(127, 29, 29, 255), 0, "Close", white)) s_Atm.open = false;
    BcwEndOverlay();
}

// ---- info people at the plaza ------------------------------------------------------------------
// They stand in an arc in front of the lobby camera, facing it, each with a floating text.
struct RpInfoPed { int skin; float dx, dy; const char* anim; const char* file; const char* title; const char* text; uint32_t col; int ped; bool animOk; };
static RpInfoPed s_RpInfo[] =
{
    { 141,  -3.2f,  -7.4f, "Coplook_loop", "COP_AMBIENT", "WELCOME TO MEGA VIL",  "Futuristic Roleplay 24/7\nRoleplay only - no DM, no cheats\nBe a citizen: work, earn, live", 0x38BDF8FF, 0, false },
    { 150,  -1.1f,  -5.3f, "Coplook_loop", "COP_AMBIENT", "RULES",                "/rules - roleplay, don't fail it\n/b OOC  /me /do  /n newbie help\nGreenzones: nobody gets hurt", 0xFBBF24FF, 0, false },
    { 187,   1.0f,  -3.2f, "Coplook_loop", "COP_AMBIENT", "JOBS",                 "/jobs - pick one at the Job Center\n/duty  /work  then follow the GPS\nPayday every 30 min of play", 0x22C55EFF, 0, false },
    { 194,   3.2f,  -1.0f, "Coplook_loop", "COP_AMBIENT", "MONEY & INVEST",       "ATM: walk into the ring - Use ATM\n/invest  /sellinvest  /market\n/pawn at a 24/7 - /lotto 1-60", 0x4ADE80FF, 0, false },
    { 240,   5.3f,   1.1f, "Coplook_loop", "COP_AMBIENT", "PHONE & PANEL",        "PANEL button: bag, documents\n/phone  /sms  /call  /taxi  /911\n/gps - every place in the city", 0xA78BFAFF, 0, false },
    { 147,   7.4f,   3.2f, "Coplook_loop", "COP_AMBIENT", "LIFE",                 "/buyhouse  /home  /pet  /emotes\n/propose /divorce  /seatbelt\n/joinevent - city events", 0xF472B6FF, 0, false },
};
#define RP_INFO_COUNT ((int)(sizeof(s_RpInfo) / sizeof(s_RpInfo[0])))
// offsets from the plaza spawn: a line across the camera's view, 3 m behind the spawn point
static Vec3 RpInfoPos(const RpInfoPed& r) { return { g_RpPlaces[0].x + r.dx, g_RpPlaces[0].y + r.dy, g_RpPlaces[0].z }; }
static bool RpIsInfoPed(int h)
{
    if(h <= 0) return false;
    for(auto& r : s_RpInfo) if(r.ped == h) return true;
    return false;
}
static void RpInfoUpdate(bool want, uint64_t now)
{
    static uint64_t s_Next = 0;
    if(!want)
    {
        for(auto& r : s_RpInfo) if(r.ped) { Game::DeleteChar(r.ped); r.ped = 0; r.animOk = false; }
        return;
    }
    if(now < s_Next || !Game::IsReady() || Game::IsPaused()) return;
    s_Next = now + 250;
    for(auto& r : s_RpInfo)
    {
        if(!r.ped)
        {
            Vec3 p = RpInfoPos(r);
            // face the lobby camera
            float hx = kRpLobbyCam.x - p.x, hy = kRpLobbyCam.y - p.y;
            float h = atan2f(-hx, hy) * 180.0f / 3.14159265f; if(h < 0) h += 360.0f;
            r.ped = Game::CreateNpcPed(r.skin, p, h);
            if(r.ped) { Game::SetCharGodMode(r.ped, true); Game::FreezeChar(r.ped, true); }
            return;   // one a tick
        }
        if(!r.animOk) r.animOk = Game::PlayAnimLoop(r.ped, r.anim, r.file);
    }
}
static void RpInfoDraw(ImDrawList* dl)
{
    char t[256];
    for(auto& r : s_RpInfo)
    {
        Vec3 p = RpInfoPos(r); p.z += 1.25f;
        if(BcwCamDist(p) > 60.0f) continue;
        BcwLabel3D(dl, { p.x, p.y, p.z + 0.62f }, r.title, r.col, 32.0f, 22.0f);
        snprintf(t, sizeof(t), "%s", r.text);
        BcwLabel3D(dl, p, t, 0xFFFFFFFF, 24.0f, 22.0f);
    }
}

// ---- welcome in the chat -----------------------------------------------------------------------
static void RpWelcome()
{
    SysMsg(RP_SKY,   "________ Welcome to Mega Vil Futuristic Roleplay ________");
    SysMsg(0xFFFFFFFF, "Hello %s, you are a citizen of Mega Vil now. This is a 24/7 roleplay city: stay in character.", settings.name[0] ? settings.name : "citizen");
    SysMsg(0xFFFFFFFF, "Start: /jobs for work, the PANEL button for your bag + documents, /phone, /gps for every place.");
    SysMsg(0xFFFFFFFF, "Money: walk into an ATM ring and tap Use ATM - deposit, withdraw, transfer, /invest.");
    SysMsg(0xFBBF24FF, "Rules: /rules  ·  help: /n [question] (newbie chat)  ·  all commands: /help");
}

// ---- commands ----------------------------------------------------------------------------------
static bool RpLifeCommand(const char* cmd, const char* arg)
{
    #define LIS(x) (!strcmp(cmd, x))
    if(LIS("market") || LIS("investments") || LIS("portfolio") || (LIS("invest") && !*arg)) { RpSend(RPC_INVEST_INFO); return true; }
    if(LIS("invest"))
    {
        int plan = 0, amt = 0;
        if(sscanf(arg, "%d %d", &plan, &amt) != 2 || plan < 1 || plan > RPN_COUNT || amt <= 0) { SysMsg(COL_YELLOW, "/invest [1 Bond / 2 Stocks / 3 VilCoin] [amount]  (/market for prices)"); return true; }
        RpSend(RPC_INVEST, (uint8_t)(plan - 1), 0, amt); return true;
    }
    if(LIS("sellinvest") || LIS("cashout"))
    {
        int plan = 0; char rest[32] = "";
        if(sscanf(arg, "%d %31s", &plan, rest) < 1 || plan < 1 || plan > RPN_COUNT) { SysMsg(COL_YELLOW, "/sellinvest [1-3] [amount or all]"); return true; }
        int amt = (!rest[0] || !strcasecmp(rest, "all")) ? 0 : atoi(rest);
        RpSend(RPC_INVEST, (uint8_t)(plan - 1), 1, amt); return true;
    }
    if(LIS("propose") || LIS("marry"))
    {
        if(!*arg) { SysMsg(COL_YELLOW, "/propose [id]  ($500 wedding, they /accept marry)"); return true; }
        RpSend(RPC_PROPOSE, 0, (uint8_t)atoi(arg)); return true;
    }
    if(LIS("divorce")) { RpSend(RPC_DIVORCE); return true; }
    if(LIS("seatbelt") || LIS("sb") || LIS("helmet")) { RpSend(RPC_BELT); return true; }
    if(LIS("checkseatbelt") || LIS("checksb") || LIS("csb"))
    {
        if(!*arg) { SysMsg(COL_YELLOW, "/checkseatbelt [id]"); return true; }
        RpSend(RPC_CHECK_BELT, 0, (uint8_t)atoi(arg)); return true;
    }
    if(LIS("pawn"))
    {
        int bs = 0, n = 1;
        if(sscanf(arg, "%d %d", &bs, &n) < 1 || bs < 1 || bs > RP_INV_SLOTS) { SysMsg(COL_YELLOW, "/pawn [bag slot 1-%d] [amount] - at a 24/7 or a tech shop", RP_INV_SLOTS); return true; }
        RpSend(RPC_PAWN, (uint8_t)(bs - 1), 0, n < 1 ? 1 : n); return true;
    }
    if(LIS("atm") || LIS("bank"))
    {
        int here = RpAtmHere(Game::LocalPed());
        if(here >= 0) { RpAtmOpen(here); return true; }
        return false;   // not at one: the old /atm (GPS to the nearest)
    }
    if(LIS("rules"))
    {
        SysMsg(RP_SKY, "______ MEGA VIL RULES ______");
        SysMsg(0xFFFFFFFF, "1. Roleplay: act like a real citizen. No deathmatch, no random killing.");
        SysMsg(0xFFFFFFFF, "2. No cheats, no hacks, no bug abuse. Greenzones: nobody can be hurt.");
        SysMsg(0xFFFFFFFF, "3. Police / military: force only on duty. Respect /fine and /arrest.");
        SysMsg(0xFFFFFFFF, "4. OOC talk in /b or /ooc only. Be nice in /n newbie chat.");
        return true;
    }
    #undef LIS
    return false;
}

// ---- per frame -----------------------------------------------------------------------------------
static void RpLifeProcess(int ped, uint64_t now, bool engaged)
{
    static bool s_Welcomed = false;
    if(!engaged) { if(s_Welcomed) { RpAtmClear(); RpInfoUpdate(false, now); s_Atm.open = false; } s_Welcomed = false; return; }
    if(!s_Welcomed) { s_Welcomed = true; RpWelcome(); }
    RpInfoUpdate(Game::GetCharArea(ped > 0 ? ped : Game::LocalPed()) == 0 || s_Rp.view == 1, now);
    if(s_Rp.view == 2) RpAtmStream(now, ped); else RpAtmClear();
}
