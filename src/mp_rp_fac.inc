// MP 3.34 - Mega Vil Futuristic: FACTIONS (included by mp.cpp after mp_rp_cos.inc).
// /faction (or /factions, /f) opens the list: up to 25 factions, each with its colour, [TAG], title,
// members / limit, description, rules and owner. JOIN / LEAVE; CREATE ($25000 cash) asks for the
// tag, title, colour, player limit and description; the owner can set the rules or close it.
// Members get "[TAG] name" in name tags, chat and the kill feed, and their radar pin has the
// faction colour (no faction: green).

struct RpFacC { bool used; uint8_t limit, members, mine; uint32_t col; char tag[8], title[28], desc[96], rules[96], owner[24]; };
static struct
{
    RpFacC  f[RP_FAC_MAX] = {}; int n = 0;
    uint8_t pfac[MP_MAX_PLAYERS]; bool havePf = false;
    bool    open = false; int sel = -1; uint64_t openAt = 0; float scroll = 0;
    // create form
    char    cTag[8] = "", cTitle[28] = "", cDesc[96] = "", cRules[96] = ""; int cCol = 0, cLimit = RP_FAC_LIMIT;
    bool    form = false;
} s_Fac;
static const uint32_t kRpFacCols[] = { 0x38BDF8FF, 0xF87171FF, 0x4ADE80FF, 0xFACC15FF, 0xC084FCFF, 0xFB923CFF, 0xF472B6FF, 0x2DD4BFFF, 0xFFFFFFFF, 0x94A3B8FF };
static const int RP_FAC_NCOL = (int)(sizeof(kRpFacCols) / sizeof(kRpFacCols[0]));

static void RpFacReset() { memset(s_Fac.f, 0, sizeof(s_Fac.f)); s_Fac.n = 0; memset(s_Fac.pfac, 0xFF, sizeof(s_Fac.pfac)); s_Fac.havePf = false; s_Fac.open = false; s_Fac.form = false; }
static void RpFacOnPacket(const uint8_t* d, int len)
{
    if(d[0] == PKT_RP_FAC && len >= (int)sizeof(PktRpFac))
    {
        const PktRpFac& p = *(const PktRpFac*)d;
        s_Fac.n = p.count > RP_FAC_MAX ? RP_FAC_MAX : p.count;
        for(int i = s_Fac.n; i < RP_FAC_MAX; ++i) s_Fac.f[i].used = false;
        if(p.idx >= RP_FAC_MAX) return;
        RpFacC& f = s_Fac.f[p.idx];
        f.used = true; f.limit = p.limit; f.members = p.members; f.mine = p.mine; f.col = p.col;
        MP_CopyStr(f.tag, p.tag, sizeof(f.tag)); MP_CopyStr(f.title, p.title, sizeof(f.title));
        MP_CopyStr(f.desc, p.desc, sizeof(f.desc)); MP_CopyStr(f.rules, p.rules, sizeof(f.rules)); MP_CopyStr(f.owner, p.owner, sizeof(f.owner));
    }
    else if(d[0] == PKT_RP_PFAC && len >= (int)sizeof(PktRpPFac))
    {
        const PktRpPFac& p = *(const PktRpPFac*)d;
        memcpy(s_Fac.pfac, p.fac, sizeof(s_Fac.pfac)); s_Fac.havePf = true;
    }
}
static const RpFacC* RpFacOf(int id)
{
    if(!s_Fac.havePf || !RpOnServer() || id < 0 || id >= MP_MAX_PLAYERS) return nullptr;
    uint8_t f = s_Fac.pfac[id];
    if(f >= RP_FAC_MAX || !s_Fac.f[f].used) return nullptr;
    return &s_Fac.f[f];
}
// name tags, kill feed, chat: "[TAG] name" in the faction colour
static bool RpFacDecorate(int id, const char* name, uint32_t& color, char* out, size_t n)
{
    const RpFacC* f = RpFacOf(id);
    if(!f || !name || name[0] == '[') return false;
    snprintf(out, n, "[%s] %s", f->tag, name);
    color = f->col;
    return true;
}
static uint32_t RpFacPinColor(int id) { const RpFacC* f = RpFacOf(id); return f ? f->col : 0x4ADE80FF; }

static void RpFacSend(int a, int b, const char* text) { RpSend(RPC_FACTION, (uint8_t)a, (uint8_t)b, 0, text); }
static void RpFacOpen() { s_Fac.open = true; s_Fac.form = false; s_Fac.openAt = MP_NowMs(); RpFacSend(0, 0, nullptr); }

// keyboard fields of the create form
enum { RFK_TAG = 1, RFK_TITLE, RFK_DESC, RFK_RULES, RFK_LIMIT, RFK_SETRULES };
static void RpFacKb(const char* t, void* ud)
{
    int k = (int)(intptr_t)ud; if(!t) return;
    switch(k)
    {
    case RFK_TAG:   MP_CopyStr(s_Fac.cTag, t, sizeof(s_Fac.cTag)); break;
    case RFK_TITLE: MP_CopyStr(s_Fac.cTitle, t, sizeof(s_Fac.cTitle)); break;
    case RFK_DESC:  MP_CopyStr(s_Fac.cDesc, t, sizeof(s_Fac.cDesc)); break;
    case RFK_RULES: MP_CopyStr(s_Fac.cRules, t, sizeof(s_Fac.cRules)); break;
    case RFK_LIMIT: { int v = atoi(t); s_Fac.cLimit = v < 2 ? 2 : (v > RP_FAC_LIMIT ? RP_FAC_LIMIT : v); break; }
    case RFK_SETRULES: RpFacSend(4, 0, t); break;
    }
}
static bool RpFacBox(const char* id, ImVec2 a, ImVec2 sz, ImU32 fill, ImU32 bar, const char* label, float fs = 0)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(a);
    bool hit = ImGui::InvisibleButton(id, sz); bool down = ImGui::IsItemActive();
    ImVec2 b(a.x + sz.x, a.y + sz.y);
    dl->AddRectFilled(a, b, down ? IM_COL32(56, 189, 248, 140) : fill);
    if(bar) dl->AddRectFilled(ImVec2(a.x, b.y - UI::S(4)), b, bar);
    if(label) RpCenterIn(dl, fs > 0 ? fs : UI::S(18), a, b, IM_COL32_WHITE, label);
    return hit;
}
static void RpFacDraw()
{
    if(!s_Fac.open) return;
    if(!RpOnServer()) { s_Fac.open = false; return; }
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float W = fminf(UI::S(1180), ds.x - UI::S(40)), H = fminf(UI::S(760), ds.y - UI::S(60));
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H) * 0.5f), b(a.x + W, a.y + H);
    BcwBeginOverlay("##RpFac", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 glass = IM_COL32(10, 10, 12, 165), cell = IM_COL32(40, 40, 44, 102), sky = RpCol(RP_SKY, 1), orange = IM_COL32(245, 130, 32, 255);
    dl->AddRectFilled(a, b, glass);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + UI::S(50)), IM_COL32(8, 16, 26, 200));
    dl->AddRectFilled(ImVec2(a.x, a.y + UI::S(50)), ImVec2(b.x, a.y + UI::S(53)), sky);
    RpHudIcon(dl, RPH_FACTION, ImVec2(a.x + UI::S(30), a.y + UI::S(25)), UI::S(32), IM_COL32_WHITE);
    char t[160]; snprintf(t, sizeof(t), "FACTIONS  %d / %d", s_Fac.n, RP_FAC_MAX);
    RpPlainText(dl, UI::S(22), ImVec2(a.x + UI::S(56), a.y + UI::S(13)), IM_COL32_WHITE, t);
    bool close = RpFacBox("##fcx", ImVec2(b.x - UI::S(110), a.y + UI::S(6)), ImVec2(UI::S(100), UI::S(38)), cell, 0, "CLOSE");
    const float pad = UI::S(14), top = a.y + UI::S(66);
    if(!s_Fac.form)
    {
        // left: the list
        const float lw = W * 0.46f, rowH = UI::S(70), gap = UI::S(6);
        ImGui::SetCursorScreenPos(ImVec2(a.x + pad, top));
        ImGui::BeginChild("##faclist", ImVec2(lw, H - (top - a.y) - UI::S(90)), false, ImGuiWindowFlags_NoScrollbar);
        ImDrawList* cl = ImGui::GetWindowDrawList();
        ImVec2 c0 = ImGui::GetCursorScreenPos();
        if(!s_Fac.n) RpPlainText(cl, UI::S(18), ImVec2(c0.x + UI::S(6), c0.y + UI::S(10)), IM_COL32(200, 200, 200, 255), "No factions yet. Create the first one.");
        for(int i = 0; i < s_Fac.n; ++i)
        {
            const RpFacC& f = s_Fac.f[i]; if(!f.used) continue;
            ImVec2 ra(c0.x, c0.y + i * (rowH + gap)), rb(ra.x + lw - UI::S(4), ra.y + rowH);
            ImGui::SetCursorScreenPos(ra);
            char id[16]; snprintf(id, sizeof(id), "##fr%d", i);
            if(ImGui::InvisibleButton(id, ImVec2(lw - UI::S(4), rowH))) { s_Fac.sel = i; RpClick(RPS_CLICK); }
            bool on = s_Fac.sel == i;
            cl->AddRectFilled(ra, rb, on ? IM_COL32(56, 189, 248, 90) : cell);
            cl->AddRectFilled(ra, ImVec2(ra.x + UI::S(6), rb.y), RpCol(f.col, 1));
            snprintf(t, sizeof(t), "[%s] %s%s", f.tag, f.title, f.mine == 2 ? "  (yours)" : (f.mine == 1 ? "  (member)" : ""));
            RpPlainText(cl, UI::S(19), ImVec2(ra.x + UI::S(16), ra.y + UI::S(8)), RpCol(f.col, 1), t);
            snprintf(t, sizeof(t), "%d / %d players   -   owner %s", f.members, f.limit, f.owner);
            RpPlainText(cl, UI::S(15), ImVec2(ra.x + UI::S(16), ra.y + UI::S(40)), IM_COL32(200, 204, 210, 255), t);
        }
        ImGui::SetCursorScreenPos(ImVec2(c0.x, c0.y + s_Fac.n * (rowH + gap)));
        ImGui::Dummy(ImVec2(1, 1));
        ImGui::EndChild();
        // right: the picked one
        ImVec2 da(a.x + pad * 2 + lw, top), db(b.x - pad, b.y - UI::S(90));
        dl->AddRectFilled(da, db, cell);
        if(s_Fac.sel >= 0 && s_Fac.sel < s_Fac.n && s_Fac.f[s_Fac.sel].used)
        {
            const RpFacC& f = s_Fac.f[s_Fac.sel];
            dl->AddRectFilled(da, ImVec2(db.x, da.y + UI::S(6)), RpCol(f.col, 1));
            float y = da.y + UI::S(16), x = da.x + UI::S(14), ww = db.x - da.x - UI::S(28);
            snprintf(t, sizeof(t), "[%s] %s", f.tag, f.title); RpPlainText(dl, UI::S(24), ImVec2(x, y), RpCol(f.col, 1), t); y += UI::S(40);
            snprintf(t, sizeof(t), "Players %d / %d   -   Owner %s", f.members, f.limit, f.owner); RpPlainText(dl, UI::S(16), ImVec2(x, y), IM_COL32(210, 214, 220, 255), t); y += UI::S(34);
            RpPlainText(dl, UI::S(16), ImVec2(x, y), sky, "DESCRIPTION"); y += UI::S(24);
            dl->AddText(UI::Font(), UI::S(17), ImVec2(x, y), IM_COL32_WHITE, f.desc[0] ? f.desc : "-", nullptr, ww); y += UI::S(80);
            RpPlainText(dl, UI::S(16), ImVec2(x, y), orange, "RULES"); y += UI::S(24);
            dl->AddText(UI::Font(), UI::S(17), ImVec2(x, y), IM_COL32_WHITE, f.rules[0] ? f.rules : "-", nullptr, ww);
            const float bw = UI::S(180), bh = UI::S(52), by = db.y - bh - UI::S(12);
            if(f.mine == 0) { if(RpFacBox("##fj", ImVec2(x, by), ImVec2(bw, bh), sky, 0, f.members >= f.limit ? "FULL" : "JOIN")) RpFacSend(1, s_Fac.sel, nullptr); }
            else if(RpFacBox("##fl", ImVec2(x, by), ImVec2(bw, bh), cell, orange, "LEAVE")) RpFacSend(2, 0, nullptr);
            if(f.mine == 2)
            {
                if(RpFacBox("##fr", ImVec2(x + bw + UI::S(10), by), ImVec2(bw, bh), cell, sky, "SET RULES")) VKeyboard::Open("Faction rules", f.rules, 94, RpFacKb, (void*)(intptr_t)RFK_SETRULES, false);
                if(RpFacBox("##fd", ImVec2(x + (bw + UI::S(10)) * 2, by), ImVec2(bw, bh), IM_COL32(120, 30, 30, 200), 0, "CLOSE IT")) RpFacSend(5, 0, nullptr);
            }
        }
        else RpPlainText(dl, UI::S(17), ImVec2(da.x + UI::S(14), da.y + UI::S(16)), IM_COL32(200, 200, 200, 255), "Pick a faction on the left.");
        snprintf(t, sizeof(t), "CREATE FACTION  ($%d)", RP_FAC_PRICE);
        if(RpFacBox("##fc", ImVec2(a.x + pad, b.y - UI::S(74)), ImVec2(UI::S(360), UI::S(60)), sky, 0, t, UI::S(19)))
        { s_Fac.form = true; s_Fac.cTag[0] = s_Fac.cTitle[0] = s_Fac.cDesc[0] = 0; s_Fac.cCol = 0; s_Fac.cLimit = RP_FAC_LIMIT; }
    }
    else
    {   // the create form
        float x = a.x + pad * 2, y = top + UI::S(6), fw = W - pad * 4, fh = UI::S(56), gap = UI::S(10);
        struct { const char* lb; char* v; int key; } rows[] = { { "TAG (2-5 letters)", s_Fac.cTag, RFK_TAG }, { "TITLE", s_Fac.cTitle, RFK_TITLE }, { "DESCRIPTION", s_Fac.cDesc, RFK_DESC } };
        for(int r = 0; r < 3; ++r)
        {
            RpPlainText(dl, UI::S(15), ImVec2(x, y), sky, rows[r].lb); y += UI::S(22);
            char id[12]; snprintf(id, sizeof(id), "##ff%d", r);
            if(RpFacBox(id, ImVec2(x, y), ImVec2(fw, fh), cell, 0, nullptr))
                VKeyboard::Open(rows[r].lb, rows[r].v, r == 0 ? 5 : (r == 1 ? 26 : 94), RpFacKb, (void*)(intptr_t)rows[r].key, false);
            RpPlainText(dl, UI::S(19), ImVec2(x + UI::S(12), y + UI::S(16)), IM_COL32_WHITE, rows[r].v[0] ? rows[r].v : "(tap to type)");
            y += fh + gap;
        }
        RpPlainText(dl, UI::S(15), ImVec2(x, y), sky, "COLOUR"); y += UI::S(22);
        for(int c = 0; c < RP_FAC_NCOL; ++c)
        {
            char id[12]; snprintf(id, sizeof(id), "##fcol%d", c);
            ImVec2 ca(x + c * (fh + gap), y);
            if(RpFacBox(id, ca, ImVec2(fh, fh), RpCol(kRpFacCols[c], 1), 0, nullptr)) s_Fac.cCol = c;
            if(s_Fac.cCol == c) dl->AddRect(ImVec2(ca.x - UI::S(3), ca.y - UI::S(3)), ImVec2(ca.x + fh + UI::S(3), ca.y + fh + UI::S(3)), IM_COL32_WHITE, 0, 0, UI::S(3));
        }
        y += fh + gap;
        RpPlainText(dl, UI::S(15), ImVec2(x, y), sky, "PLAYER LIMIT"); y += UI::S(22);
        if(RpFacBox("##fl-", ImVec2(x, y), ImVec2(fh, fh), cell, 0, "-", UI::S(26)) && s_Fac.cLimit > 2) --s_Fac.cLimit;
        snprintf(t, sizeof(t), "%d", s_Fac.cLimit);
        RpCenterIn(dl, UI::S(22), ImVec2(x + fh, y), ImVec2(x + fh * 2.6f, y + fh), IM_COL32_WHITE, t);
        if(RpFacBox("##fl+", ImVec2(x + fh * 2.6f, y), ImVec2(fh, fh), cell, 0, "+", UI::S(26)) && s_Fac.cLimit < RP_FAC_LIMIT) ++s_Fac.cLimit;
        y += fh + gap * 2;
        snprintf(t, sizeof(t), "CREATE  ($%d)", RP_FAC_PRICE);
        if(RpFacBox("##fgo", ImVec2(x, b.y - UI::S(80)), ImVec2(UI::S(300), UI::S(60)), sky, 0, t, UI::S(19)))
        {
            char cmd[100];
            snprintf(cmd, sizeof(cmd), "%s|%s|%06X|%d|%s", s_Fac.cTag, s_Fac.cTitle, (unsigned)(kRpFacCols[s_Fac.cCol] >> 8), s_Fac.cLimit, s_Fac.cDesc);
            RpFacSend(3, 0, cmd); s_Fac.form = false;
        }
        if(RpFacBox("##fback", ImVec2(x + UI::S(316), b.y - UI::S(80)), ImVec2(UI::S(200), UI::S(60)), cell, orange, "BACK")) s_Fac.form = false;
    }
    BcwEndOverlay();
    if(close) s_Fac.open = false;
}
