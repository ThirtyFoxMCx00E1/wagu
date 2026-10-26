// MP 3.30 - the ATM screen (included by mp_rp_life.inc). It looks like a real street ATM now
// instead of the green 3.29 window:
//   - brushed steel body, a blue bank topper with the MEGA BANK logo
//   - a dark bezel with the classic blue ATM screen and 4 grey side keys left + 4 right
//     (tap the side key or the option on the screen)
//   - a steel keypad: 1-9, 00, 0, back, and the coloured keys CANCEL (red), CLEAR (yellow),
//     ENTER (green); type an amount and press ENTER on the withdraw / deposit screen
//   - card reader with a blinking green light, receipt slot and a cash slot that glows on a withdrawal
// Screens: main menu, withdrawal, deposit, investments, transfer.

enum { ATS_MAIN = 0, ATS_WD, ATS_DEP, ATS_INV, ATS_TR };
static struct { bool open = false; int tab = 0; uint64_t at = 0; int place = -1; char entry[12] = ""; char msg[72] = ""; uint64_t msgAt = 0, cashAt = 0; } s_Atm;
enum { RPKB_ATM_DEP = 200, RPKB_ATM_WD, RPKB_ATM_INV0, RPKB_ATM_INV1, RPKB_ATM_INV2, RPKB_ATM_SELL0, RPKB_ATM_SELL1, RPKB_ATM_SELL2 };
static const char* AtmMoney(int v, char* b, int n)
{   // $12,345
    char d[16]; snprintf(d, sizeof(d), "%d", v < 0 ? -v : v);
    int len = (int)strlen(d), o = 0;
    if(o < n - 1) b[o++] = '$';
    if(v < 0 && o < n - 1) b[o++] = '-';
    for(int i = 0; i < len && o < n - 1; ++i) { b[o++] = d[i]; if((len - 1 - i) % 3 == 0 && i < len - 1 && o < n - 1) b[o++] = ','; }
    b[o] = 0; return b;
}
static void AtmSay(const char* m) { snprintf(s_Atm.msg, sizeof(s_Atm.msg), "%s", m); s_Atm.msgAt = MP_NowMs(); }
static void AtmWithdraw(int v)
{
    if(v <= 0) { AtmSay("Enter an amount first."); return; }
    RpSend(RPC_WITHDRAW, 0, 0, v); s_Atm.cashAt = MP_NowMs();
    char m[24], t[72]; snprintf(t, sizeof(t), "Withdrawing %s - please take your cash.", AtmMoney(v, m, sizeof(m))); AtmSay(t);
}
static void AtmDeposit(int v)
{
    if(v <= 0) { AtmSay("Enter an amount first."); return; }
    RpSend(RPC_DEPOSIT, 0, 0, v);
    char m[24], t[72]; snprintf(t, sizeof(t), "Depositing %s into your account.", AtmMoney(v, m, sizeof(m))); AtmSay(t);
}
static void RpAtmKb(const char* t, void* data)
{
    if(!t || !*t) return;
    int mode = (int)(intptr_t)data, v = atoi(t);
    if(v <= 0 && !(mode >= RPKB_ATM_SELL0 && mode <= RPKB_ATM_SELL2)) return;
    if(mode == RPKB_ATM_DEP) AtmDeposit(v);
    else if(mode == RPKB_ATM_WD) AtmWithdraw(v);
    else if(mode >= RPKB_ATM_INV0 && mode <= RPKB_ATM_INV2) RpSend(RPC_INVEST, (uint8_t)(mode - RPKB_ATM_INV0), 0, v);
    else if(mode >= RPKB_ATM_SELL0 && mode <= RPKB_ATM_SELL2) RpSend(RPC_INVEST, (uint8_t)(mode - RPKB_ATM_SELL0), 1, v);
}
static void AtmGo(int scr) { s_Atm.tab = scr; s_Atm.entry[0] = 0; }
static int AtmEntry() { return s_Atm.entry[0] ? atoi(s_Atm.entry) : 0; }

// the side key labels per screen (nullptr = no option on that key)
static const char* AtmLabel(int scr, bool right, int i, char* b, int n)
{
    static const int kAmt[4] = { 100, 500, 1000, 5000 };
    char m[24];
    switch(scr)
    {
    case ATS_MAIN:
        if(!right) { static const char* const L[4] = { "WITHDRAWAL", "DEPOSIT", "FAST CASH $100", "BALANCE" }; return L[i]; }
        { static const char* const R[4] = { "INVESTMENTS", "TRANSFER", nullptr, "EXIT" }; return R[i]; }
    case ATS_WD: case ATS_DEP:
        if(!right) { snprintf(b, n, "%s", AtmMoney(kAmt[i], m, sizeof(m))); return b; }
        if(scr == ATS_WD) { static const char* const R[4] = { "OTHER AMOUNT", "$10,000", nullptr, "BACK" }; return R[i]; }
        { static const char* const R[4] = { "ALL CASH", "OTHER AMOUNT", nullptr, "BACK" }; return R[i]; }
    case ATS_INV:
        if(i == 3) return right ? "BACK" : nullptr;
        if(!right) { int v = AtmEntry() > 0 ? AtmEntry() : 500; snprintf(b, n, "BUY %s", AtmMoney(v, m, sizeof(m))); return b; }
        return "SELL ALL";
    case ATS_TR:
        if(right) { static const char* const R[4] = { "SEND MONEY", nullptr, nullptr, "BACK" }; return R[i]; }
        return nullptr;
    }
    return nullptr;
}
static void AtmAction(int scr, bool right, int i)
{
    static const int kAmt[4] = { 100, 500, 1000, 5000 };
    const PktRpMe& m = s_Rp.me;
    switch(scr)
    {
    case ATS_MAIN:
        if(!right) { if(i == 0) AtmGo(ATS_WD); else if(i == 1) AtmGo(ATS_DEP); else if(i == 2) AtmWithdraw(100);
                     else { char a[24], c[24], t[72]; snprintf(t, sizeof(t), "Bank %s  -  cash %s", AtmMoney(m.bank, a, sizeof(a)), AtmMoney(m.cash, c, sizeof(c))); AtmSay(t); } }
        else { if(i == 0) AtmGo(ATS_INV); else if(i == 1) AtmGo(ATS_TR); else if(i == 3) s_Atm.open = false; }
        break;
    case ATS_WD:
        if(!right) AtmWithdraw(kAmt[i]);
        else if(i == 0) VKeyboard::Open("Withdraw amount", "", 9, RpAtmKb, (void*)(intptr_t)RPKB_ATM_WD, true);
        else if(i == 1) AtmWithdraw(10000);
        else if(i == 3) AtmGo(ATS_MAIN);
        break;
    case ATS_DEP:
        if(!right) AtmDeposit(kAmt[i]);
        else if(i == 0) { if(m.cash > 0) AtmDeposit(m.cash); else AtmSay("You have no cash on you."); }
        else if(i == 1) VKeyboard::Open("Deposit amount", "", 9, RpAtmKb, (void*)(intptr_t)RPKB_ATM_DEP, true);
        else if(i == 3) AtmGo(ATS_MAIN);
        break;
    case ATS_INV:
        if(i == 3) { if(right) AtmGo(ATS_MAIN); break; }
        if(!right) { int v = AtmEntry() > 0 ? AtmEntry() : 500; RpSend(RPC_INVEST, (uint8_t)i, 0, v); s_Atm.entry[0] = 0;
                     char a[24], t[72]; snprintf(t, sizeof(t), "Buying %s of %s...", AtmMoney(v, a, sizeof(a)), g_RpPlans[i].name); AtmSay(t); }
        else { RpSend(RPC_INVEST, (uint8_t)i, 1, 0); char t[72]; snprintf(t, sizeof(t), "Selling all of %s...", g_RpPlans[i].name); AtmSay(t); }
        break;
    case ATS_TR:
        if(right && i == 0) VKeyboard::Open("Phone number", "", 9, RpKbSubmit, (void*)(intptr_t)RPKB_TR_NUM, true);
        else if(right && i == 3) AtmGo(ATS_MAIN);
        break;
    }
}
static void AtmKey(const char* k)
{
    if(!strcmp(k, "CANCEL")) { if(s_Atm.entry[0]) s_Atm.entry[0] = 0; else if(s_Atm.tab == ATS_MAIN) s_Atm.open = false; else AtmGo(ATS_MAIN); return; }
    if(!strcmp(k, "CLEAR")) { s_Atm.entry[0] = 0; return; }
    if(!strcmp(k, "<")) { size_t n = strlen(s_Atm.entry); if(n) s_Atm.entry[n - 1] = 0; return; }
    if(!strcmp(k, "ENTER"))
    {
        int v = AtmEntry();
        if(s_Atm.tab == ATS_WD) { AtmWithdraw(v); s_Atm.entry[0] = 0; }
        else if(s_Atm.tab == ATS_DEP) { AtmDeposit(v); s_Atm.entry[0] = 0; }
        else if(s_Atm.tab == ATS_INV) AtmSay(v > 0 ? "Now press BUY next to a plan." : "Type an amount, then press BUY.");
        else if(s_Atm.tab == ATS_MAIN) AtmSay(v > 0 ? "Pick WITHDRAWAL or DEPOSIT first." : "Please select a transaction.");
        return;
    }
    if(!*k) return;
    if(s_Atm.tab == ATS_TR || s_Atm.tab == ATS_MAIN) { if(s_Atm.tab == ATS_MAIN) { AtmGo(ATS_WD); } else return; }
    size_t n = strlen(s_Atm.entry);
    for(const char* p = k; *p; ++p)
    {
        if(n == 0 && *p == '0') continue;   // no leading zeros
        if(n >= 9) break;
        s_Atm.entry[n++] = *p; s_Atm.entry[n] = 0;
    }
}

// ---- drawing ----------------------------------------------------------------------------------
static bool AtmHit(const char* id, ImVec2 a, ImVec2 sz)
{
    ImGui::SetCursorScreenPos(a);
    bool r = ImGui::InvisibleButton(id, sz);
    if(r) RpClick(RPS_CLICK);
    return r;
}
// a raised steel key: lighter face, darker lip under it, pressed = pushed in
static void AtmKeyFace(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 face, ImU32 lip, bool down)
{
    const float d = UI::S(4);
    if(down) { dl->AddRectFilled(ImVec2(a.x, a.y + d), ImVec2(b.x, b.y), lip, UI::S(7)); return; }
    dl->AddRectFilled(ImVec2(a.x, a.y + d), b, lip, UI::S(7));
    dl->AddRectFilled(a, ImVec2(b.x, b.y - d), face, UI::S(7));
    dl->AddLine(ImVec2(a.x + UI::S(6), a.y + UI::S(2)), ImVec2(b.x - UI::S(6), a.y + UI::S(2)), IM_COL32(255, 255, 255, 110), UI::S(1.5f));
}
static void RpDrawAtmMachine(uint64_t nowMs, const char* placeName)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktRpMe& m = s_Rp.me;
    const float W = UI::S(1180), H = UI::S(736);
    float k = (nowMs - s_Atm.at) / 160.0f; if(k > 1) k = 1;
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H) * 0.5f + (1.0f - k) * UI::S(30)), b(a.x + W, a.y + H);
    if(a.y < UI::S(6)) { b.y += UI::S(6) - a.y; a.y = UI::S(6); }
    BcwBeginOverlay("##RpAtm", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 white = IM_COL32_WHITE;
    char t[128], mb[24];
    // dim the world a little
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(90 * k)));
    // ---- the body: brushed steel ----
    dl->AddRectFilled(ImVec2(a.x + UI::S(6), a.y + UI::S(10)), ImVec2(b.x + UI::S(6), b.y + UI::S(10)), IM_COL32(0, 0, 0, 90), UI::S(24));   // shadow
    dl->AddRectFilled(a, b, IM_COL32(120, 126, 136, 255), UI::S(24));
    dl->AddRectFilledMultiColor(ImVec2(a.x + UI::S(8), a.y + UI::S(80)), ImVec2(b.x - UI::S(8), b.y - UI::S(14)),
                                IM_COL32(204, 209, 216, 255), IM_COL32(186, 192, 200, 255), IM_COL32(140, 147, 157, 255), IM_COL32(160, 166, 175, 255));
    for(int i = 0; i < 26; ++i)   // fine brush lines
    {
        float y = a.y + UI::S(92) + i * (H - UI::S(110)) / 26.0f;
        dl->AddLine(ImVec2(a.x + UI::S(10), y), ImVec2(b.x - UI::S(10), y), IM_COL32(255, 255, 255, 14), UI::S(1));
    }
    dl->AddRect(a, b, IM_COL32(70, 76, 86, 255), UI::S(24), 0, UI::S(2.5f));
    // ---- the topper: the bank's blue sign ----
    {
        ImVec2 ta(a.x, a.y), tb(b.x, a.y + UI::S(78));
        dl->AddRectFilled(ta, tb, IM_COL32(0, 70, 165, 255), UI::S(24), ImDrawFlags_RoundCornersTop);
        dl->AddRectFilledMultiColor(ImVec2(ta.x + UI::S(18), ta.y + UI::S(6)), ImVec2(tb.x - UI::S(18), ta.y + UI::S(34)),
                                    IM_COL32(255, 255, 255, 46), IM_COL32(255, 255, 255, 46), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
        dl->AddRectFilled(ImVec2(ta.x, tb.y - UI::S(6)), tb, IM_COL32(255, 196, 0, 255));   // a thin yellow stripe under the sign
        ImVec2 lc(ta.x + UI::S(52), ta.y + UI::S(36));
        dl->AddCircleFilled(lc, UI::S(24), white, 32);
        RpCenterIn(dl, UI::S(26), ImVec2(lc.x - UI::S(24), lc.y - UI::S(24)), ImVec2(lc.x + UI::S(24), lc.y + UI::S(24)), IM_COL32(0, 70, 165, 255), "M");
        RpPlainText(dl, UI::S(30), ImVec2(ta.x + UI::S(88), ta.y + UI::S(10)), white, "MEGA BANK");
        RpPlainText(dl, UI::S(13), ImVec2(ta.x + UI::S(90), ta.y + UI::S(46)), IM_COL32(191, 219, 254, 255), placeName ? placeName : "ATM");
        const char* r = "24 HOUR BANKING"; float rw = RpTextW(UI::S(18), r);
        RpPlainText(dl, UI::S(18), ImVec2(tb.x - UI::S(30) - rw, ta.y + UI::S(16)), white, r);
        const char* r2 = "CASH  -  DEPOSIT  -  INVEST  -  TRANSFER"; float rw2 = RpTextW(UI::S(12), r2);
        RpPlainText(dl, UI::S(12), ImVec2(tb.x - UI::S(30) - rw2, ta.y + UI::S(44)), IM_COL32(191, 219, 254, 255), r2);
    }
    // ---- the bezel, side keys and the blue screen ----
    const ImVec2 za(a.x + UI::S(28), a.y + UI::S(100)), zb(za.x + UI::S(800), za.y + UI::S(500));
    dl->AddRectFilled(za, zb, IM_COL32(30, 33, 39, 255), UI::S(16));
    dl->AddRect(za, zb, IM_COL32(12, 14, 18, 255), UI::S(16), 0, UI::S(3));
    const ImVec2 sa(za.x + UI::S(84), za.y + UI::S(24)), sb(zb.x - UI::S(84), zb.y - UI::S(24));
    dl->AddRectFilledMultiColor(sa, sb, IM_COL32(18, 80, 170, 255), IM_COL32(18, 80, 170, 255), IM_COL32(6, 36, 102, 255), IM_COL32(6, 36, 102, 255));
    dl->AddRect(sa, sb, IM_COL32(0, 0, 0, 255), 0, 0, UI::S(3));
    dl->AddRectFilledMultiColor(sa, ImVec2(sb.x, sa.y + UI::S(60)), IM_COL32(255, 255, 255, 22), IM_COL32(255, 255, 255, 22), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
    const float sw = sb.x - sa.x;
    // header on the screen
    static const char* const kTitle[5] = { "WELCOME", "WITHDRAWAL", "DEPOSIT", "INVESTMENTS", "TRANSFER" };
    const int scr = s_Atm.tab < 0 || s_Atm.tab > ATS_TR ? ATS_MAIN : s_Atm.tab;
    if(scr == ATS_MAIN) snprintf(t, sizeof(t), "WELCOME, %s", settings.name); else snprintf(t, sizeof(t), "%s", kTitle[scr]);
    RpCenterIn(dl, UI::S(26), ImVec2(sa.x, sa.y + UI::S(12)), ImVec2(sb.x, sa.y + UI::S(46)), white, t);
    static const char* const kSub[5] = { "Please select a transaction", "Select an amount, or type it on the keypad and press ENTER",
                                         "Select an amount, or type it on the keypad and press ENTER", "One city market - prices move every 5 minutes",
                                         "Send bank money to a phone number" };
    RpCenterIn(dl, UI::S(15), ImVec2(sa.x, sa.y + UI::S(48)), ImVec2(sb.x, sa.y + UI::S(72)), IM_COL32(255, 214, 0, 255), kSub[scr]);
    dl->AddLine(ImVec2(sa.x + UI::S(24), sa.y + UI::S(84)), ImVec2(sb.x - UI::S(24), sa.y + UI::S(84)), IM_COL32(255, 255, 255, 60), UI::S(1.5f));
    // options + side keys (rows line up)
    const float rowY0 = sa.y + UI::S(104), rowStep = UI::S(74), rowH = UI::S(54);
    for(int side = 0; side < 2; ++side) for(int i = 0; i < 4; ++i)
    {
        bool right = side == 1;
        char lb[32]; const char* L = AtmLabel(scr, right, i, lb, sizeof(lb));
        float cy = rowY0 + i * rowStep + rowH * 0.5f;
        // the physical key in the bezel
        ImVec2 ka(right ? sb.x + UI::S(14) : za.x + UI::S(16), cy - UI::S(22)), kb(ka.x + UI::S(54), cy + UI::S(22));
        char id[16]; snprintf(id, sizeof(id), "##ak%d%d", side, i);
        bool hit = AtmHit(id, ka, ImVec2(kb.x - ka.x, kb.y - ka.y)); bool dn = ImGui::IsItemActive();
        AtmKeyFace(dl, ka, kb, IM_COL32(200, 205, 212, 255), IM_COL32(110, 116, 126, 255), dn);
        float ky = dn ? UI::S(2) : 0, kx = (ka.x + kb.x) * 0.5f;
        if(!right) dl->AddTriangleFilled(ImVec2(kx - UI::S(8), cy - UI::S(9) + ky), ImVec2(kx - UI::S(8), cy + UI::S(7) + ky), ImVec2(kx + UI::S(6), cy - UI::S(1) + ky), IM_COL32(60, 66, 76, 255));   // points at the screen
        else      dl->AddTriangleFilled(ImVec2(kx + UI::S(8), cy - UI::S(9) + ky), ImVec2(kx + UI::S(8), cy + UI::S(7) + ky), ImVec2(kx - UI::S(6), cy - UI::S(1) + ky), IM_COL32(60, 66, 76, 255));
        // the option on the screen (tap it too)
        bool hit2 = false;
        if(L)
        {
            float lw = RpTextW(UI::S(19), L), lx = right ? sb.x - UI::S(30) - lw : sa.x + UI::S(30);
            snprintf(id, sizeof(id), "##ao%d%d", side, i);
            ImVec2 oa(right ? sb.x - sw * 0.42f : sa.x, cy - rowH * 0.5f);
            hit2 = AtmHit(id, oa, ImVec2(sw * 0.42f, rowH));
            if(ImGui::IsItemActive() || dn) dl->AddRectFilled(ImVec2(right ? lx - UI::S(12) : sa.x + UI::S(8), cy - UI::S(20)), ImVec2(right ? sb.x - UI::S(8) : lx + lw + UI::S(12), cy + UI::S(20)), IM_COL32(255, 255, 255, 50), UI::S(4));
            // a little marker pointing at the key, then the text
            if(right) dl->AddTriangleFilled(ImVec2(sb.x - UI::S(20), cy - UI::S(7)), ImVec2(sb.x - UI::S(20), cy + UI::S(7)), ImVec2(sb.x - UI::S(10), cy), white);
            else      dl->AddTriangleFilled(ImVec2(sa.x + UI::S(20), cy - UI::S(7)), ImVec2(sa.x + UI::S(20), cy + UI::S(7)), ImVec2(sa.x + UI::S(10), cy), white);
            RpPlainText(dl, UI::S(19), ImVec2(lx, cy - UI::S(11)), white, L);
        }
        if((hit || hit2) && L) AtmAction(scr, right, i);
    }
    // the middle of the screen
    const float cxm = (sa.x + sb.x) * 0.5f;
    if(scr == ATS_WD || scr == ATS_DEP)
    {
        ImVec2 ea(cxm - UI::S(110), rowY0 + rowStep * 1.0f - UI::S(4)), eb(cxm + UI::S(110), ea.y + UI::S(58));
        RpCenterIn(dl, UI::S(13), ImVec2(ea.x, ea.y - UI::S(26)), ImVec2(eb.x, ea.y - UI::S(4)), IM_COL32(191, 219, 254, 255), "ENTER AMOUNT");
        dl->AddRectFilled(ea, eb, IM_COL32(240, 244, 250, 255), UI::S(4));
        dl->AddRect(ea, eb, IM_COL32(0, 0, 0, 120), UI::S(4), 0, UI::S(2));
        snprintf(t, sizeof(t), "%s", AtmMoney(AtmEntry(), mb, sizeof(mb)));
        bool blink = (nowMs / 500) % 2 == 0;
        float vw = RpTextW(UI::S(26), t);
        RpPlainText(dl, UI::S(26), ImVec2(eb.x - UI::S(16) - vw - UI::S(10), ea.y + UI::S(14)), IM_COL32(10, 20, 40, 255), t);
        if(blink) dl->AddRectFilled(ImVec2(eb.x - UI::S(20), ea.y + UI::S(14)), ImVec2(eb.x - UI::S(17), eb.y - UI::S(14)), IM_COL32(10, 20, 40, 255));
        snprintf(t, sizeof(t), scr == ATS_WD ? "Available: %s" : "Cash on you: %s", AtmMoney(scr == ATS_WD ? m.bank : m.cash, mb, sizeof(mb)));
        RpCenterIn(dl, UI::S(14), ImVec2(ea.x - UI::S(40), eb.y + UI::S(8)), ImVec2(eb.x + UI::S(40), eb.y + UI::S(30)), IM_COL32(191, 219, 254, 255), t);
    }
    else if(scr == ATS_INV)
    {
        for(int i = 0; i < RPN_COUNT; ++i)
        {
            float cy = rowY0 + i * rowStep + rowH * 0.5f;
            int price = s_Rp.haveState ? s_Rp.st.market[i] : 0, chg = s_Rp.haveState ? s_Rp.st.marketChg[i] : 0;
            RpCenterIn(dl, UI::S(15), ImVec2(cxm - UI::S(130), cy - UI::S(26)), ImVec2(cxm + UI::S(130), cy - UI::S(6)), white, g_RpPlans[i].name);
            snprintf(t, sizeof(t), "%d (%+.1f%%)  yours %s", price, chg / 10.0f, AtmMoney(m.invest[i], mb, sizeof(mb)));
            RpCenterIn(dl, UI::S(13), ImVec2(cxm - UI::S(130), cy - UI::S(2)), ImVec2(cxm + UI::S(130), cy + UI::S(18)), chg >= 0 ? IM_COL32(134, 239, 172, 255) : IM_COL32(252, 165, 165, 255), t);
        }
        if(s_Atm.entry[0])
        {
            snprintf(t, sizeof(t), "Typed: %s - BUY uses it", AtmMoney(AtmEntry(), mb, sizeof(mb)));
            RpCenterIn(dl, UI::S(14), ImVec2(sa.x, rowY0 + 3 * rowStep), ImVec2(sb.x, rowY0 + 3 * rowStep + rowH), IM_COL32(255, 214, 0, 255), t);
        }
    }
    else if(scr == ATS_TR)
    {
        snprintf(t, sizeof(t), "Your number: %d", m.number);
        RpCenterIn(dl, UI::S(19), ImVec2(sa.x, rowY0 + rowStep), ImVec2(sb.x - sw * 0.3f, rowY0 + rowStep + UI::S(30)), white, t);
        snprintf(t, sizeof(t), "Next payday in %d:%02d", m.paydayS / 60, m.paydayS % 60);
        RpCenterIn(dl, UI::S(16), ImVec2(sa.x, rowY0 + rowStep + UI::S(36)), ImVec2(sb.x - sw * 0.3f, rowY0 + rowStep + UI::S(62)), IM_COL32(191, 219, 254, 255), t);
        RpCenterIn(dl, UI::S(14), ImVec2(sa.x, rowY0 + rowStep * 2 + UI::S(10)), ImVec2(sb.x - sw * 0.3f, rowY0 + rowStep * 2 + UI::S(36)), IM_COL32(191, 219, 254, 255), "Works anywhere too: /transfer [number] [amount]");
    }
    // footer: balances, or the last message
    {
        float fy = sb.y - UI::S(40);
        dl->AddRectFilled(ImVec2(sa.x, fy), sb, IM_COL32(0, 20, 60, 200));
        if(s_Atm.msgAt && nowMs - s_Atm.msgAt < 3500)
            RpCenterIn(dl, UI::S(16), ImVec2(sa.x, fy), sb, IM_COL32(255, 214, 0, 255), s_Atm.msg);
        else
        {
            snprintf(t, sizeof(t), "CASH %s", AtmMoney(m.cash, mb, sizeof(mb)));
            RpPlainText(dl, UI::S(16), ImVec2(sa.x + UI::S(20), fy + UI::S(10)), white, t);
            snprintf(t, sizeof(t), "BANK %s", AtmMoney(m.bank, mb, sizeof(mb)));
            float bw2 = RpTextW(UI::S(16), t);
            RpPlainText(dl, UI::S(16), ImVec2(sb.x - UI::S(20) - bw2, fy + UI::S(10)), white, t);
        }
    }
    // ---- right column: card reader, keypad, receipt, exit ----
    const float rx = zb.x + UI::S(28), rw = b.x - UI::S(28) - rx;
    {   // card reader
        ImVec2 ca(rx, za.y), cb(rx + rw, za.y + UI::S(92));
        dl->AddRectFilled(ca, cb, IM_COL32(52, 56, 64, 255), UI::S(12));
        dl->AddRectFilled(ImVec2(ca.x + UI::S(30), ca.y + UI::S(40)), ImVec2(cb.x - UI::S(70), ca.y + UI::S(52)), IM_COL32(8, 9, 11, 255), UI::S(4));
        bool on = (nowMs / 400) % 2 == 0;
        dl->AddRectFilled(ImVec2(ca.x + UI::S(30), ca.y + UI::S(56)), ImVec2(cb.x - UI::S(70), ca.y + UI::S(60)), on ? IM_COL32(34, 197, 94, 255) : IM_COL32(20, 83, 45, 255), UI::S(2));
        RpPlainText(dl, UI::S(12), ImVec2(ca.x + UI::S(30), ca.y + UI::S(14)), IM_COL32(203, 213, 225, 255), "INSERT CARD");
        // a little card icon
        ImVec2 ia(cb.x - UI::S(56), ca.y + UI::S(28)), ib(cb.x - UI::S(16), ca.y + UI::S(56));
        dl->AddRectFilled(ia, ib, IM_COL32(0, 70, 165, 255), UI::S(4));
        dl->AddRectFilled(ImVec2(ia.x + UI::S(5), ia.y + UI::S(8)), ImVec2(ia.x + UI::S(15), ia.y + UI::S(16)), IM_COL32(255, 196, 0, 255), UI::S(1));
    }
    {   // keypad
        const float kpY = za.y + UI::S(112), gap = UI::S(10), kw = (rw - UI::S(24) - gap * 3) / 4.0f, kh = UI::S(52);
        ImVec2 pa(rx, kpY), pb(rx + rw, kpY + UI::S(24) + kh * 4 + gap * 3);
        dl->AddRectFilled(pa, pb, IM_COL32(70, 75, 84, 255), UI::S(12));
        dl->AddRect(pa, pb, IM_COL32(40, 44, 52, 255), UI::S(12), 0, UI::S(2));
        static const char* const kDig[12] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "00", "0", "<" };
        for(int i = 0; i < 12; ++i)
        {
            int c = i % 3, r = i / 3;
            ImVec2 ka(pa.x + UI::S(12) + c * (kw + gap), pa.y + UI::S(12) + r * (kh + gap)), kb(ka.x + kw, ka.y + kh);
            char id[12]; snprintf(id, sizeof(id), "##kd%d", i);
            bool hit = AtmHit(id, ka, ImVec2(kw, kh)), dn = ImGui::IsItemActive();
            AtmKeyFace(dl, ka, kb, IM_COL32(228, 231, 235, 255), IM_COL32(140, 146, 156, 255), dn);
            float o = dn ? UI::S(2) : -UI::S(2);
            RpCenterIn(dl, UI::S(i == 11 ? 20 : 24), ImVec2(ka.x, ka.y + o), ImVec2(kb.x, kb.y + o), IM_COL32(30, 34, 42, 255), i == 11 ? "<-" : kDig[i]);
            if(hit) AtmKey(kDig[i]);
        }
        struct FK { const char* k; const char* label; ImU32 face, lip, ink; };
        static const FK kFn[4] = {
            { "CANCEL", "CANCEL", IM_COL32(214, 40, 40, 255),  IM_COL32(140, 20, 24, 255), IM_COL32_WHITE },
            { "CLEAR",  "CLEAR",  IM_COL32(250, 200, 20, 255), IM_COL32(170, 128, 0, 255), IM_COL32(40, 30, 0, 255) },
            { "",       "",       IM_COL32(228, 231, 235, 255), IM_COL32(140, 146, 156, 255), IM_COL32(30, 34, 42, 255) },
            { "ENTER",  "ENTER",  IM_COL32(30, 150, 70, 255),  IM_COL32(16, 92, 42, 255),  IM_COL32_WHITE } };
        for(int r = 0; r < 4; ++r)
        {
            ImVec2 ka(pa.x + UI::S(12) + 3 * (kw + gap), pa.y + UI::S(12) + r * (kh + gap)), kb(ka.x + kw, ka.y + kh);
            char id[12]; snprintf(id, sizeof(id), "##kf%d", r);
            bool hit = AtmHit(id, ka, ImVec2(kw, kh)), dn = ImGui::IsItemActive();
            AtmKeyFace(dl, ka, kb, kFn[r].face, kFn[r].lip, dn);
            float o = dn ? UI::S(2) : -UI::S(2);
            if(kFn[r].label[0]) RpCenterIn(dl, UI::S(12), ImVec2(ka.x, ka.y + o), ImVec2(kb.x, kb.y + o), kFn[r].ink, kFn[r].label);
            if(hit) AtmKey(kFn[r].k);
        }
        // receipt slot under the keypad
        ImVec2 qa(rx, pb.y + UI::S(18)), qb(rx + rw, pb.y + UI::S(66));
        dl->AddRectFilled(qa, qb, IM_COL32(52, 56, 64, 255), UI::S(10));
        dl->AddRectFilled(ImVec2(qa.x + UI::S(40), qa.y + UI::S(28)), ImVec2(qb.x - UI::S(40), qa.y + UI::S(34)), IM_COL32(8, 9, 11, 255), UI::S(3));
        RpPlainText(dl, UI::S(11), ImVec2(qa.x + UI::S(40), qa.y + UI::S(8)), IM_COL32(203, 213, 225, 255), "RECEIPT");
        // exit: a red pill at the bottom
        ImVec2 xa(rx, b.y - UI::S(28) - UI::S(56)), xb(rx + rw, b.y - UI::S(28));
        bool hit = AtmHit("##atx", xa, ImVec2(rw, UI::S(56))), dn = ImGui::IsItemActive();
        AtmKeyFace(dl, xa, xb, IM_COL32(214, 40, 40, 255), IM_COL32(140, 20, 24, 255), dn);
        RpCenterIn(dl, UI::S(18), ImVec2(xa.x, xa.y + (dn ? UI::S(2) : -UI::S(2))), ImVec2(xb.x, xb.y + (dn ? UI::S(2) : -UI::S(2))), white, "EXIT");
        if(hit) s_Atm.open = false;
    }
    {   // cash slot under the screen: glows green for a moment after a withdrawal
        ImVec2 ca(za.x + UI::S(120), zb.y + UI::S(26)), cb(zb.x - UI::S(120), b.y - UI::S(34));
        dl->AddRectFilled(ca, cb, IM_COL32(52, 56, 64, 255), UI::S(12));
        bool glow = s_Atm.cashAt && nowMs - s_Atm.cashAt < 3000 && (nowMs / 250) % 2 == 0;
        ImVec2 la(ca.x + UI::S(40), (ca.y + cb.y) * 0.5f - UI::S(6)), lb(cb.x - UI::S(40), (ca.y + cb.y) * 0.5f + UI::S(6));
        dl->AddRectFilled(la, lb, IM_COL32(8, 9, 11, 255), UI::S(4));
        if(glow) dl->AddRect(ImVec2(la.x - UI::S(3), la.y - UI::S(3)), ImVec2(lb.x + UI::S(3), lb.y + UI::S(3)), IM_COL32(34, 197, 94, 255), UI::S(5), 0, UI::S(3));
        RpPlainText(dl, UI::S(11), ImVec2(la.x, ca.y + UI::S(8)), IM_COL32(203, 213, 225, 255), "CASH");
    }
    BcwEndOverlay();
}
