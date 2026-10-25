// MP 3.25 - Mega Vil Futuristic: the phone (included by mp.cpp after mp_rp.inc)
// A real phone UI: status bar, wallpaper themes, app grid, home bar. Apps: Contacts, Messages,
// Phone, Bank, Jobs, GPS, Shop, Garage, Houses, Pets, Emotes, Vil Feed, Games (Snake, 2048,
// Tic-Tac-Toe, Sky Hop), News & Events, 911, Taxi, Settings. Calls ring here (Answer / Decline);
// a connected call carries your voice chat to the other phone anywhere in the city.

enum eRpApp { RPA_HOME = 0, RPA_CONTACTS, RPA_SMS, RPA_DIAL, RPA_BANK, RPA_JOBS, RPA_GPS, RPA_SHOP, RPA_GARAGE, RPA_HOUSES, RPA_PETS,
              RPA_EMOTES, RPA_FEED, RPA_GAMES, RPA_NEWS, RPA_911, RPA_TAXI, RPA_SETTINGS, RPA_COUNT, RPA_CALL = -2 };
struct RpAppDef { const char* name; const char* glyph; uint32_t col; };
static const RpAppDef g_RpApps[RPA_COUNT] =
{
    { "Home", "", 0 },
    { "Contacts", "C", 0x22C55EFF }, { "Messages", "M", 0x10B981FF }, { "Phone", "P", 0x16A34AFF }, { "Bank", "$", 0x0EA5E9FF },
    { "Jobs", "J", 0xF59E0BFF },     { "GPS", "G", 0x3B82F6FF },      { "Shop", "S", 0xEC4899FF }, { "Garage", "V", 0x64748BFF },
    { "Houses", "H", 0xA855F7FF },   { "Pets", "*", 0xF472B6FF },     { "Emotes", "E", 0xFB923CFF }, { "Vil Feed", "#", 0x38BDF8FF },
    { "Games", ">", 0x8B5CF6FF },    { "News", "N", 0xEF4444FF },     { "911", "!", 0xDC2626FF },  { "Taxi", "T", 0xEAB308FF },
    { "Settings", "o", 0x6B7280FF },
};
static int  s_nRpTheme = 0;   // 0 sky, 1 neon, 2 sunset, 3 matrix
static const uint32_t kRpThemeTop[4] = { 0x0EA5E9FF, 0x7C3AEDFF, 0xF97316FF, 0x052E16FF };
static const uint32_t kRpThemeBot[4] = { 0x0B1E3AFF, 0x1E0B3AFF, 0x3B0A2AFF, 0x000000FF };

// ---- keyboard plumbing -----------------------------------------------------------------------
enum { RPK_NONE = 0, RPKB_SMS_TO, RPKB_SMS_TEXT, RPKB_DIAL, RPKB_DEPOSIT, RPKB_WITHDRAW, RPKB_TR_NUM, RPKB_TR_AMT, RPKB_FEED, RPKB_AD, RPKB_911, RPKB_TAXI, RPKB_LOTTO };
static int32_t s_nRpKbNum = 0;
static void RpKbSubmit(const char* t, void* data)
{
    int mode = (int)(intptr_t)data;
    if(!t) return;
    switch(mode)
    {
        case RPKB_SMS_TO:   s_nRpKbNum = atoi(t); if(s_nRpKbNum > 0) { s_Rp.smsThread = s_nRpKbNum; s_Rp.app = RPA_SMS; } break;
        case RPKB_SMS_TEXT: if(*t && s_Rp.smsThread > 0) RpSend(RPC_SMS, 0, 0, s_Rp.smsThread, t); break;
        case RPKB_DIAL:     if(atoi(t) > 0) RpSend(RPC_CALL, 0, 0, atoi(t)); break;
        case RPKB_DEPOSIT:  if(atoi(t) > 0) RpSend(RPC_DEPOSIT, 0, 0, atoi(t)); break;
        case RPKB_WITHDRAW: if(atoi(t) > 0) RpSend(RPC_WITHDRAW, 0, 0, atoi(t)); break;
        case RPKB_TR_NUM:   s_nRpKbNum = atoi(t); if(s_nRpKbNum > 0) VKeyboard::Open("Amount to send", "", 9, RpKbSubmit, (void*)(intptr_t)RPKB_TR_AMT, true); break;
        case RPKB_TR_AMT:   { char n[16]; snprintf(n, sizeof(n), "%d", s_nRpKbNum); if(atoi(t) > 0) RpSend(RPC_TRANSFER, 1, 0, atoi(t), n); break; }
        case RPKB_FEED:     if(*t) RpSend(RPC_FEED, 0, 0, 0, t); break;
        case RPKB_AD:       if(*t) RpSend(RPC_AD, 0, 0, 0, t); break;
        case RPKB_911:      RpSend(RPC_911, 0, 0, 0, *t ? t : "Help needed!"); break;
        case RPKB_TAXI:     RpSend(RPC_TAXI_CALL, 0, 0, 0, t); break;
        case RPKB_LOTTO:    { int n = atoi(t); if(n >= 1 && n <= 60) RpSend(RPC_LOTTO, 0, (uint8_t)n); break; }
    }
}
static void RpKb(const char* title, int mode, bool numeric, int maxLen = 99) { VKeyboard::Open(title, "", maxLen, RpKbSubmit, (void*)(intptr_t)mode, numeric); }

static void RpPhoneOpen(int app)
{
    if(!RpOnServer()) return;
    s_Rp.phone = true; s_Rp.appSub = 0; s_Rp.appSel = -1;
    if(app == -2) s_Rp.app = RPA_CALL;
    else s_Rp.app = app >= 0 && app < RPA_COUNT ? app : RPA_HOME;
    if(s_Rp.app == RPA_SMS) s_Rp.unreadSms = 0;
}
static int s_nRpShopCat = -1;
static void RpPhoneOpenShop(int cat) { RpPhoneOpen(RPA_SHOP); s_nRpShopCat = cat; }

// ---- small widgets ---------------------------------------------------------------------------
static void RpDragScroll()
{
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, UI::S(14)))
    {
        float dy = ImGui::GetIO().MouseDelta.y;
        if(dy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - dy);
    }
}
static bool RpPBtn(const char* label, float w, float h, uint32_t col = RP_SKY, bool disabled = false)
{
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(col));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x, c.y, c.z, disabled ? 0.3f : 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x, c.y, c.z, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.x * 0.8f, c.y * 0.8f, c.z * 0.8f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(12));
    if(disabled) ImGui::BeginDisabled();
    bool r = ImGui::Button(label, ImVec2(w, h));
    if(disabled) ImGui::EndDisabled();
    ImGui::PopStyleVar(); ImGui::PopStyleColor(3);
    return r;
}
static void RpPText(uint32_t col, const char* fmt, ...)
{
    char b[256]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(col)));
    ImGui::TextWrapped("%s", b);
    ImGui::PopStyleColor();
}
static void RpPTitle(const char* t)
{
    ImGui::SetWindowFontScale(1.15f); RpPText(0xFFFFFFFF, "%s", t); ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();
}
static void RpPSep() { ImGui::Dummy(ImVec2(1, UI::S(6))); }

// ---- games -----------------------------------------------------------------------------------
struct RpGames
{
    int best[4] = { 0, 0, 0, 0 };     // snake, 2048, sky hop, tic-tac-toe wins
    bool loaded = false;
    // snake
    int  snX[400], snY[400], snLen = 0, snDir = 0, snNext = 0, snFx = 0, snFy = 0; bool snOver = true; uint64_t snT = 0; int snScore = 0;
    // 2048
    int  g[4][4] = {}; int gScore = 0; bool gOver = true;
    // tic tac toe
    int  ttt[9] = {}; int tttRes = 0;   // 0 playing, 1 you, 2 phone, 3 draw
    // sky hop
    float hy = 0.5f, hv = 0; float px[3] = { 1.2f, 1.75f, 2.3f }, pg[3] = { 0.5f, 0.4f, 0.6f }; int hScore = 0; bool hOver = true; uint64_t hT = 0; bool hPassed[3] = {};
};
static RpGames s_RpG;
static void RpGamesPath(char* o, size_t n) { snprintf(o, n, "%s/rp_games.txt", s_szDataDir); }
static void RpGamesLoad() { if(s_RpG.loaded) return; s_RpG.loaded = true; char p[512]; RpGamesPath(p, sizeof(p)); FILE* f = fopen(p, "r"); if(!f) return; if(fscanf(f, "%d %d %d %d", &s_RpG.best[0], &s_RpG.best[1], &s_RpG.best[2], &s_RpG.best[3]) != 4) {} fclose(f); }
static void RpGamesSave() { char p[512]; RpGamesPath(p, sizeof(p)); FILE* f = fopen(p, "w"); if(!f) return; fprintf(f, "%d %d %d %d\n", s_RpG.best[0], s_RpG.best[1], s_RpG.best[2], s_RpG.best[3]); fclose(f); }
static void RpBest(int k, int v) { if(v > s_RpG.best[k]) { s_RpG.best[k] = v; RpGamesSave(); } }
#define SN 15
static void SnakeFood() { for(int t = 0; t < 200; ++t) { int x = rand() % SN, y = rand() % SN; bool hit = false; for(int i = 0; i < s_RpG.snLen; ++i) if(s_RpG.snX[i] == x && s_RpG.snY[i] == y) hit = true; if(!hit) { s_RpG.snFx = x; s_RpG.snFy = y; return; } } }
static void SnakeStart() { s_RpG.snLen = 3; for(int i = 0; i < 3; ++i) { s_RpG.snX[i] = 7 - i; s_RpG.snY[i] = 7; } s_RpG.snDir = s_RpG.snNext = 0; s_RpG.snOver = false; s_RpG.snScore = 0; s_RpG.snT = MP_NowMs(); SnakeFood(); }
static void SnakeStep()
{
    static const int dx[4] = { 1, 0, -1, 0 }, dy[4] = { 0, 1, 0, -1 };
    if((s_RpG.snNext + 2) % 4 != s_RpG.snDir) s_RpG.snDir = s_RpG.snNext;
    int nx = s_RpG.snX[0] + dx[s_RpG.snDir], ny = s_RpG.snY[0] + dy[s_RpG.snDir];
    if(nx < 0 || ny < 0 || nx >= SN || ny >= SN) { s_RpG.snOver = true; RpBest(0, s_RpG.snScore); return; }
    for(int i = 0; i < s_RpG.snLen - 1; ++i) if(s_RpG.snX[i] == nx && s_RpG.snY[i] == ny) { s_RpG.snOver = true; RpBest(0, s_RpG.snScore); return; }
    bool eat = nx == s_RpG.snFx && ny == s_RpG.snFy;
    if(eat && s_RpG.snLen < 399) ++s_RpG.snLen;
    for(int i = s_RpG.snLen - 1; i > 0; --i) { s_RpG.snX[i] = s_RpG.snX[i - 1]; s_RpG.snY[i] = s_RpG.snY[i - 1]; }
    s_RpG.snX[0] = nx; s_RpG.snY[0] = ny;
    if(eat) { s_RpG.snScore += 10; SnakeFood(); }
}
static void G2048Add() { int fr[16], n = 0; for(int i = 0; i < 16; ++i) if(!s_RpG.g[i / 4][i % 4]) fr[n++] = i; if(n) { int k = fr[rand() % n]; s_RpG.g[k / 4][k % 4] = rand() % 10 ? 2 : 4; } }
static void G2048Start() { memset(s_RpG.g, 0, sizeof(s_RpG.g)); s_RpG.gScore = 0; s_RpG.gOver = false; G2048Add(); G2048Add(); }
static void G2048Move(int dir)   // 0 left 1 right 2 up 3 down
{
    bool moved = false;
    for(int line = 0; line < 4; ++line)
    {
        int v[4], o[4] = { 0, 0, 0, 0 }, n = 0;
        for(int k = 0; k < 4; ++k) { int r = dir < 2 ? line : (dir == 2 ? k : 3 - k), c = dir < 2 ? (dir == 0 ? k : 3 - k) : line; v[k] = s_RpG.g[r][c]; }
        for(int k = 0; k < 4; ++k) if(v[k]) { if(n && o[n - 1] == v[k] && o[n - 1] > 0) { o[n - 1] = -o[n - 1] * 2; s_RpG.gScore += -o[n - 1]; } else o[n++] = v[k]; }
        for(int k = 0; k < 4; ++k) { if(o[k] < 0) o[k] = -o[k]; int r = dir < 2 ? line : (dir == 2 ? k : 3 - k), c = dir < 2 ? (dir == 0 ? k : 3 - k) : line; if(s_RpG.g[r][c] != o[k]) moved = true; s_RpG.g[r][c] = o[k]; }
    }
    if(moved) G2048Add();
    bool can = false; for(int r = 0; r < 4; ++r) for(int c = 0; c < 4; ++c) { if(!s_RpG.g[r][c]) can = true; if(c < 3 && s_RpG.g[r][c] == s_RpG.g[r][c + 1]) can = true; if(r < 3 && s_RpG.g[r][c] == s_RpG.g[r + 1][c]) can = true; }
    if(!can) { s_RpG.gOver = true; RpBest(1, s_RpG.gScore); }
}
static int TttWin(const int* b) { static const int L[8][3] = { {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6} }; for(auto& l : L) if(b[l[0]] && b[l[0]] == b[l[1]] && b[l[1]] == b[l[2]]) return b[l[0]]; for(int i = 0; i < 9; ++i) if(!b[i]) return 0; return 3; }
static void TttCpu()
{
    int* b = s_RpG.ttt;
    for(int who = 2; who >= 1; --who) for(int i = 0; i < 9; ++i) if(!b[i]) { b[i] = who; if(TttWin(b) == who) { b[i] = 2; return; } b[i] = 0; }   // win, else block
    if(!b[4]) { b[4] = 2; return; }
    static const int pref[8] = { 0, 2, 6, 8, 1, 3, 5, 7 }; int fr[8], n = 0; for(int i : pref) if(!b[i]) fr[n++] = i;
    if(n) b[fr[rand() % (n < 4 ? n : 4)]] = 2;
}
static void HopStart() { s_RpG.hy = 0.5f; s_RpG.hv = 0; s_RpG.hScore = 0; s_RpG.hOver = false; s_RpG.hT = MP_NowMs(); for(int i = 0; i < 3; ++i) { s_RpG.px[i] = 1.2f + i * 0.55f; s_RpG.pg[i] = 0.3f + (rand() % 40) / 100.0f; s_RpG.hPassed[i] = false; } }

static void RpDrawGames(float w)
{
    RpGamesLoad();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bh = UI::S(54);
    if(s_Rp.appSub == 0)
    {
        RpPTitle("Games");
        const char* names[4] = { "Snake", "2048", "Sky Hop", "Tic-Tac-Toe" };
        for(int i = 0; i < 4; ++i)
        {
            char l[64]; snprintf(l, sizeof(l), "%s   (best %d%s)##g%d", names[i], s_RpG.best[i], i == 3 ? " wins" : "", i);
            if(RpPBtn(l, w, bh, 0x8B5CF6FF)) { s_Rp.appSub = i + 1; if(i == 0) SnakeStart(); if(i == 1) G2048Start(); if(i == 2) HopStart(); if(i == 3) { memset(s_RpG.ttt, 0, sizeof(s_RpG.ttt)); s_RpG.tttRes = 0; } }
        }
        return;
    }
    uint64_t now = MP_NowMs();
    float side = w; ImVec2 o = ImGui::GetCursorScreenPos();
    if(s_Rp.appSub == 1)
    {   // ---- snake ----
        if(!s_RpG.snOver) while(now - s_RpG.snT >= 150) { s_RpG.snT += 150; SnakeStep(); if(s_RpG.snOver) break; }
        float cs = side / SN;
        dl->AddRectFilled(o, ImVec2(o.x + side, o.y + side), IM_COL32(5, 20, 12, 255), UI::S(8));
        dl->AddRectFilled(ImVec2(o.x + s_RpG.snFx * cs + 2, o.y + s_RpG.snFy * cs + 2), ImVec2(o.x + (s_RpG.snFx + 1) * cs - 2, o.y + (s_RpG.snFy + 1) * cs - 2), IM_COL32(248, 113, 113, 255), cs * 0.4f);
        for(int i = 0; i < s_RpG.snLen; ++i) dl->AddRectFilled(ImVec2(o.x + s_RpG.snX[i] * cs + 1, o.y + s_RpG.snY[i] * cs + 1), ImVec2(o.x + (s_RpG.snX[i] + 1) * cs - 1, o.y + (s_RpG.snY[i] + 1) * cs - 1), i ? IM_COL32(74, 222, 128, 255) : IM_COL32(56, 189, 248, 255), UI::S(3));
        ImGui::Dummy(ImVec2(side, side));
        RpPText(0xFFFFFFFF, "Score %d   Best %d%s", s_RpG.snScore, s_RpG.best[0], s_RpG.snOver ? "   GAME OVER" : "");
        float bw = (w - UI::S(16)) / 3;
        ImGui::Dummy(ImVec2(bw, 1)); ImGui::SameLine(); if(RpPBtn("^##su", bw, bh)) s_RpG.snNext = 3;
        if(RpPBtn("<##sl", bw, bh)) s_RpG.snNext = 2; ImGui::SameLine(); if(RpPBtn(s_RpG.snOver ? "Play##sp" : "v##sd", bw, bh)) { if(s_RpG.snOver) SnakeStart(); else s_RpG.snNext = 1; } ImGui::SameLine(); if(RpPBtn(">##sr", bw, bh)) s_RpG.snNext = 0;
    }
    else if(s_Rp.appSub == 2)
    {   // ---- 2048 ----
        float cs = side / 4;
        dl->AddRectFilled(o, ImVec2(o.x + side, o.y + side), IM_COL32(30, 41, 59, 255), UI::S(10));
        for(int r = 0; r < 4; ++r) for(int c = 0; c < 4; ++c)
        {
            int v = s_RpG.g[r][c]; int lv = 0; for(int t = v; t > 1; t >>= 1) ++lv;
            ImU32 col = v ? ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f + lv * 0.05f, 0.55f + (lv % 4) * 0.08f, 0.95f - lv * 0.05f, 1)) : IM_COL32(51, 65, 85, 255);
            ImVec2 a(o.x + c * cs + UI::S(4), o.y + r * cs + UI::S(4)), b(o.x + (c + 1) * cs - UI::S(4), o.y + (r + 1) * cs - UI::S(4));
            dl->AddRectFilled(a, b, col, UI::S(8));
            if(v) { char t[12]; snprintf(t, sizeof(t), "%d", v); float fs = cs * (v < 100 ? 0.45f : (v < 1000 ? 0.36f : 0.28f)); ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t); UI::OutlinedText(dl, fs, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32_WHITE, t); }
        }
        ImGui::Dummy(ImVec2(side, side));
        RpPText(0xFFFFFFFF, "Score %d   Best %d%s", s_RpG.gScore, s_RpG.best[1], s_RpG.gOver ? "   GAME OVER" : "");
        float bw = (w - UI::S(16)) / 3;
        ImGui::Dummy(ImVec2(bw, 1)); ImGui::SameLine(); if(RpPBtn("^##gu", bw, bh) && !s_RpG.gOver) G2048Move(2);
        if(RpPBtn("<##gl", bw, bh) && !s_RpG.gOver) G2048Move(0); ImGui::SameLine(); if(RpPBtn(s_RpG.gOver ? "Play##gp" : "v##gd", bw, bh)) { if(s_RpG.gOver) G2048Start(); else G2048Move(3); } ImGui::SameLine(); if(RpPBtn(">##gr", bw, bh) && !s_RpG.gOver) G2048Move(1);
    }
    else if(s_Rp.appSub == 3)
    {   // ---- sky hop (tap to fly through the neon gates) ----
        float hgt = side * 1.15f;
        ImGui::InvisibleButton("##hop", ImVec2(side, hgt));
        bool tap = ImGui::IsItemActivated();
        if(tap) { if(s_RpG.hOver) HopStart(); else s_RpG.hv = -0.85f; }
        if(!s_RpG.hOver)
        {
            float dt = (now - s_RpG.hT) / 1000.0f; if(dt > 0.05f) dt = 0.05f; s_RpG.hT = now;
            s_RpG.hv += 2.2f * dt; s_RpG.hy += s_RpG.hv * dt;
            for(int i = 0; i < 3; ++i)
            {
                s_RpG.px[i] -= 0.38f * dt;
                if(s_RpG.px[i] < -0.15f) { s_RpG.px[i] += 1.65f; s_RpG.pg[i] = 0.25f + (rand() % 50) / 100.0f; s_RpG.hPassed[i] = false; }
                if(!s_RpG.hPassed[i] && s_RpG.px[i] < 0.25f) { s_RpG.hPassed[i] = true; ++s_RpG.hScore; }
                if(fabsf(s_RpG.px[i] - 0.3f) < 0.09f && fabsf(s_RpG.hy - s_RpG.pg[i]) > 0.14f) s_RpG.hOver = true;
            }
            if(s_RpG.hy > 1.0f || s_RpG.hy < 0.0f) s_RpG.hOver = true;
            if(s_RpG.hOver) RpBest(2, s_RpG.hScore);
        }
        dl->AddRectFilledMultiColor(o, ImVec2(o.x + side, o.y + hgt), IM_COL32(14, 165, 233, 255), IM_COL32(14, 165, 233, 255), IM_COL32(11, 30, 58, 255), IM_COL32(11, 30, 58, 255));
        for(int i = 0; i < 3; ++i)
        {
            float x = o.x + s_RpG.px[i] * side, gw = side * 0.12f, gy0 = o.y + (s_RpG.pg[i] - 0.15f) * hgt, gy1 = o.y + (s_RpG.pg[i] + 0.15f) * hgt;
            if(x + gw < o.x || x > o.x + side) continue;
            float xa = x < o.x ? o.x : x, xb = x + gw > o.x + side ? o.x + side : x + gw;
            dl->AddRectFilled(ImVec2(xa, o.y), ImVec2(xb, gy0), IM_COL32(168, 85, 247, 255));
            dl->AddRectFilled(ImVec2(xa, gy1), ImVec2(xb, o.y + hgt), IM_COL32(168, 85, 247, 255));
        }
        ImVec2 bp(o.x + side * 0.3f, o.y + s_RpG.hy * hgt);
        dl->AddCircleFilled(bp, side * 0.04f, IM_COL32(250, 250, 250, 255), 16); dl->AddCircle(bp, side * 0.04f + UI::S(2), IM_COL32(56, 189, 248, 255), 16, UI::S(3));
        char t[64]; snprintf(t, sizeof(t), s_RpG.hOver ? "Tap to play  -  best %d" : "%d", s_RpG.hOver ? s_RpG.best[2] : s_RpG.hScore);
        float fs = UI::S(30); UI::OutlinedText(dl, fs, ImVec2(o.x + side * 0.5f - UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t).x * 0.5f, o.y + UI::S(12)), IM_COL32_WHITE, t);
    }
    else if(s_Rp.appSub == 4)
    {   // ---- tic-tac-toe vs the phone ----
        float cs = (w - UI::S(16)) / 3;
        for(int i = 0; i < 9; ++i)
        {
            if(i % 3) ImGui::SameLine();
            char l[16]; snprintf(l, sizeof(l), "%s##t%d", s_RpG.ttt[i] == 1 ? "X" : (s_RpG.ttt[i] == 2 ? "O" : " "), i);
            ImGui::SetWindowFontScale(1.8f);
            if(RpPBtn(l, cs, cs, s_RpG.ttt[i] == 1 ? 0x38BDF8FF : (s_RpG.ttt[i] == 2 ? 0xF472B6FF : 0x334155FF)) && !s_RpG.ttt[i] && !s_RpG.tttRes)
            {
                s_RpG.ttt[i] = 1; s_RpG.tttRes = TttWin(s_RpG.ttt);
                if(!s_RpG.tttRes) { TttCpu(); s_RpG.tttRes = TttWin(s_RpG.ttt); }
                if(s_RpG.tttRes == 1) RpBest(3, s_RpG.best[3] + 1);
            }
            ImGui::SetWindowFontScale(1.0f);
        }
        RpPText(0xFFFFFFFF, "%s   Wins %d", s_RpG.tttRes == 1 ? "You win!" : s_RpG.tttRes == 2 ? "The phone wins." : s_RpG.tttRes == 3 ? "Draw." : "You are X.", s_RpG.best[3]);
        if(s_RpG.tttRes && RpPBtn("Again##ta", w, bh)) { memset(s_RpG.ttt, 0, sizeof(s_RpG.ttt)); s_RpG.tttRes = 0; }
    }
    if(RpPBtn("< Games##gb", w, UI::S(46), 0x334155FF)) s_Rp.appSub = 0;
}

// ---- apps ------------------------------------------------------------------------------------
static void RpAppContacts(float w)
{
    RpPTitle("Contacts");
    RpPText(0xBAE6FDFF, "My number: %d", s_Rp.me.number);
    if(RpPBtn("New message to a number##cn", w, UI::S(46), 0x10B981FF)) RpKb("Phone number", RPKB_SMS_TO, true, 6);
    RpPSep();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!s_Rp.haveRow[i] || i == myId) continue;
        const RpRow& r = s_Rp.row[i];
        ImGui::PushID(i);
        RpPText(0xFFFFFFFF, "%s  [%d]", RpName(i), i);
        RpPText(0x94A3B8FF, "%d  -  %s, Lv %d%s", r.number, g_RpJobs[r.job < RPJ_COUNT ? r.job : 0].name, r.level, (r.flags & 16) ? "  (busy)" : "");
        float bw = (w - UI::S(8)) / 2;
        if(RpPBtn("Call", bw, UI::S(42), 0x16A34AFF)) RpSend(RPC_CALL, 0, 0, r.number);
        ImGui::SameLine();
        if(RpPBtn("Message", bw, UI::S(42), 0x10B981FF)) { s_Rp.smsThread = r.number; s_Rp.app = RPA_SMS; }
        ImGui::PopID();
        ImGui::Separator();
    }
}
static void RpAppSms(float w)
{
    if(s_Rp.smsThread <= 0)
    {
        RpPTitle("Messages");
        if(RpPBtn("New message##sn", w, UI::S(46), 0x10B981FF)) RpKb("Phone number", RPKB_SMS_TO, true, 6);
        std::vector<int32_t> seen;
        for(int k = (int)s_Rp.sms.size() - 1; k >= 0; --k)
        {
            int32_t n = s_Rp.sms[k].number; bool dup = false; for(int32_t x : seen) if(x == n) dup = true; if(dup) continue; seen.push_back(n);
            int id = RpIdForNumber(n);
            char l[180]; snprintf(l, sizeof(l), "%s (%d)\n%.40s##th%d", id >= 0 ? RpName(id) : "Unknown", n, s_Rp.sms[k].text, n);
            ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0, 0.5f));
            if(RpPBtn(l, w, UI::S(66), 0x1E293BFF)) s_Rp.smsThread = n;
            ImGui::PopStyleVar();
        }
        if(seen.empty()) RpPText(0x94A3B8FF, "No messages yet.");
        return;
    }
    int id = RpIdForNumber(s_Rp.smsThread);
    char t[64]; snprintf(t, sizeof(t), "%s (%d)", id >= 0 ? RpName(id) : "Unknown", s_Rp.smsThread);
    RpPTitle(t);
    for(const RpSms& m : s_Rp.sms)
    {
        if(m.number != s_Rp.smsThread) continue;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float bw = w * 0.78f; ImVec2 ts = UI::Font()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, bw - UI::S(20), m.text);
        float x = m.mine ? w - ts.x - UI::S(20) : 0;
        ImVec2 p = ImGui::GetCursorScreenPos(); p.x += x;
        dl->AddRectFilled(p, ImVec2(p.x + ts.x + UI::S(20), p.y + ts.y + UI::S(14)), m.mine ? UI::FromRGBA(0x0EA5E9FF) : IM_COL32(51, 65, 85, 255), UI::S(14));
        dl->AddText(UI::Font(), ImGui::GetFontSize(), ImVec2(p.x + UI::S(10), p.y + UI::S(7)), IM_COL32_WHITE, m.text, nullptr, bw - UI::S(20));
        ImGui::Dummy(ImVec2(w, ts.y + UI::S(20)));
    }
    float bw = (w - UI::S(8)) / 2;
    if(RpPBtn("Reply##sr", bw, UI::S(48), 0x10B981FF)) RpKb("Message", RPKB_SMS_TEXT, false);
    ImGui::SameLine();
    if(RpPBtn("Call##sc", bw, UI::S(48), 0x16A34AFF)) RpSend(RPC_CALL, 0, 0, s_Rp.smsThread);
    if(RpPBtn("< All messages##sb", w, UI::S(42), 0x334155FF)) s_Rp.smsThread = 0;
}
static void RpAppDial(float w)
{
    RpPTitle("Phone");
    if(RpPBtn("Dial a number##dd", w, UI::S(56), 0x16A34AFF)) RpKb("Number to call", RPKB_DIAL, true, 6);
    RpPText(0xBAE6FDFF, "911 = emergency, the Taxi app calls a taxi.");
    RpPSep(); RpPText(0xFFFFFFFF, "Recent calls");
    for(int k = (int)s_Rp.calls.size() - 1, shown = 0; k >= 0 && shown < 20; --k, ++shown)
    {
        const RpCallLog& c = s_Rp.calls[k];
        char l[96]; snprintf(l, sizeof(l), "%s %s (%d)##cl%d", c.kind == 2 ? "Missed:" : (c.kind == 1 ? "Out:" : "In:"), c.name, c.number, k);
        if(RpPBtn(l, w, UI::S(44), c.kind == 2 ? 0x7F1D1DFF : 0x1E293BFF)) RpSend(RPC_CALL, 0, 0, c.number);
    }
}
static void RpAppBank(float w)
{
    RpPTitle("Mega Bank");
    ImGui::SetWindowFontScale(1.3f); RpPText(0x7DD3FCFF, "$%d", s_Rp.me.bank); ImGui::SetWindowFontScale(1.0f);
    RpPText(0x94A3B8FF, "Bank balance  -  cash $%d", s_Rp.me.cash);
    RpPText(0xBAE6FDFF, "Payday in %d:%02d (salary + 0.1%% interest)", s_Rp.me.paydayS / 60, s_Rp.me.paydayS % 60);
    RpPSep();
    float bw = (w - UI::S(8)) / 2;
    if(RpPBtn("Deposit##bd", bw, UI::S(52))) RpKb("Deposit amount (at an ATM)", RPKB_DEPOSIT, true, 9);
    ImGui::SameLine();
    if(RpPBtn("Withdraw##bw", bw, UI::S(52))) RpKb("Withdraw amount (at an ATM)", RPKB_WITHDRAW, true, 9);
    if(RpPBtn("Transfer to a phone number##bt", w, UI::S(52), 0x0284C7FF)) RpKb("Their phone number", RPKB_TR_NUM, true, 6);
    if(RpPBtn("Nearest ATM on GPS##ba", w, UI::S(46), 0x334155FF)) RpCommand("atm", "");
    RpPText(0x94A3B8FF, "Deposits and withdrawals work at ATMs and the Mega Bank. Transfers work anywhere.");
}
static void RpAppJobs(float w)
{
    RpPTitle("Job Center");
    const PktRpMe& m = s_Rp.me;
    RpPText(0xFFFFFFFF, "Your job: %s%s  -  tasks done %u", g_RpJobs[m.job].name, m.duty ? " (on duty)" : "", m.tasks);
    if(m.job)
    {
        float bw = (w - UI::S(8)) / 2;
        if(RpPBtn(m.jobOn ? "Stop shift##js" : "Start shift##jw", bw, UI::S(50), m.jobOn ? 0xB91C1CFF : 0x16A34AFF)) RpSend(m.jobOn ? RPC_STOP_WORK : RPC_WORK);
        ImGui::SameLine();
        bool dutyJob = RpJobArmed(m.job) || m.job == RPJ_MEDIC || m.job == RPJ_FIRE;
        if(RpPBtn(m.duty ? "Off duty##jd" : "On duty##jd", bw, UI::S(50), 0x2563EBFF, !dutyJob)) RpSend(RPC_DUTY);
        if(RpPBtn("Quit job##jq", w, UI::S(42), 0x334155FF)) RpSend(RPC_QUIT_JOB);
    }
    RpPSep();
    for(int j = 1; j < RPJ_COUNT; ++j)
    {
        const RpJob& J = g_RpJobs[j];
        ImGui::PushID(j);
        bool locked = m.level < J.minLevel || (RpJobArmed(j) && m.newbieS);
        RpPText(J.col, "%s%s", J.name, j == m.job ? "  (yours)" : "");
        RpPText(0x94A3B8FF, "%s  Pay ~$%d / task. Level %d.", J.desc, J.pay, J.minLevel);
        if(j != m.job && RpPBtn(locked ? "Locked" : "Take this job", w, UI::S(42), J.col, locked)) RpSend(RPC_JOIN_JOB, (uint8_t)j);
        ImGui::PopID(); ImGui::Separator();
    }
}
static void RpAppGps(float w)
{
    RpPTitle("GPS");
    if(s_Rp.gpsBlip && RpPBtn("Clear route##gc", w, UI::S(42), 0x334155FF)) { Game::RemoveBlip(s_Rp.gpsBlip); s_Rp.gpsBlip = 0; s_Rp.gpsName[0] = 0; }
    if(RpHasHome() && RpPBtn("My home##gh", w, UI::S(46), 0x16A34AFF)) RpCommand("myhouse", "");
    for(int i = 0; i < RP_PLACE_COUNT; ++i) { char l[80]; snprintf(l, sizeof(l), "%s##gp%d", g_RpPlaces[i].name, i); if(RpPBtn(l, w, UI::S(44), 0x1E3A8AFF)) RpPlacesPick(1, i, nullptr, 0); }
}
static void RpAppShop(float w)
{
    if(s_nRpShopCat < 0 || s_nRpShopCat >= RPS_CAT_COUNT)
    {
        RpPTitle("Shop");
        RpPText(0x94A3B8FF, "At the store counter: normal price. Anywhere else: drone delivery (+10%). Weapons only at the Licensed Ammu-Nation.");
        for(int c = 0; c < RPS_CAT_COUNT; ++c) { char l[64]; snprintf(l, sizeof(l), "%s##sc%d", g_RpShopCats[c], c); if(RpPBtn(l, w, UI::S(56), 0xEC4899FF)) s_nRpShopCat = c; }
        return;
    }
    RpPTitle(g_RpShopCats[s_nRpShopCat]);
    RpPText(0x86EFACFF, "Cash $%d  |  Bank $%d", s_Rp.me.cash, s_Rp.me.bank);
    for(int i = 0; i < RP_SHOP_COUNT; ++i)
    {
        const RpShopItem& it = g_RpShop[i]; if(it.cat != s_nRpShopCat) continue;
        char l[96]; snprintf(l, sizeof(l), "%s   $%d##it%d", it.name, it.price, i);
        bool owned = (it.kind == RPI_PET && (s_Rp.me.pets & (1u << it.a))) || (it.kind == RPI_ITEM && (s_Rp.me.items & (uint32_t)it.a));
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.05f, 0.5f));
        if(RpPBtn(owned ? (std::string(it.name) + "   (owned)##ow" + std::to_string(i)).c_str() : l, w, UI::S(48), owned ? 0x334155FF : 0x9D174DFF, owned)) RpSend(RPC_BUY, 0, 0, i);
        ImGui::PopStyleVar();
    }
    if(RpPBtn("< Categories##sb", w, UI::S(42), 0x334155FF)) s_nRpShopCat = -1;
}
static void RpAppGarage(float w)
{
    RpPTitle("Garage");
    if(!s_Rp.me.nveh) RpPText(0x94A3B8FF, "No vehicles yet. Shop - Vehicles, or the dealerships on the GPS.");
    for(int v = 0; v < s_Rp.me.nveh && v < RP_VEH_SLOTS; ++v)
    {
        int m = s_Rp.me.veh[v]; ImGui::PushID(v);
        RpPText(0xFFFFFFFF, "%d. %s", v + 1, m >= 400 && m < 612 ? kVehNames[m - 400] : "?");
        float bw = (w - UI::S(8)) / 2;
        if(RpPBtn("Deliver here", bw, UI::S(44), 0x0284C7FF)) RpSend(RPC_MY_VEH, (uint8_t)v);
        ImGui::SameLine();
        if(RpPBtn("Sell (50%)", bw, UI::S(44), 0x7F1D1DFF)) RpSend(RPC_SELL_VEH, (uint8_t)v);
        ImGui::PopID(); ImGui::Separator();
    }
}
static void RpAppHouses(float w)
{
    RpPTitle("Mega Realty");
    if(RpHasHome())
    {
        const RpHouse& H = g_RpHouses[s_Rp.me.house];
        RpPText(RP_GREEN, "Your home: %s", H.addr);
        RpPText(0x94A3B8FF, "You always spawn at home. /setspawn at your house sets the exact point.");
        float bw = (w - UI::S(8)) / 2;
        if(RpPBtn("GPS home##hg", bw, UI::S(46), 0x16A34AFF)) RpCommand("myhouse", "");
        ImGui::SameLine();
        if(RpPBtn("Sell (60%)##hs", bw, UI::S(46), 0x7F1D1DFF)) RpSend(RPC_SELL_HOUSE);
        RpPSep();
    }
    else RpPText(0xBAE6FDFF, "Buy a house: you will always spawn there instead of the Lobby Hub.");
    for(int h = 0; h < RP_HOUSE_COUNT; ++h)
    {
        bool sold = s_Rp.haveState && s_Rp.st.houseOwned[h], mine = s_Rp.me.house == h;
        char l[120]; snprintf(l, sizeof(l), "%s   %s##hh%d", g_RpHouses[h].addr, mine ? "YOURS" : (sold ? "SOLD" : ("$" + std::to_string(g_RpHouses[h].price)).c_str()), h);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.05f, 0.5f));
        if(RpPBtn(l, w, UI::S(46), mine ? 0x166534FF : (sold ? 0x334155FF : 0x6D28D9FF))) RpSetGps({ g_RpHouses[h].x, g_RpHouses[h].y, g_RpHouses[h].z }, g_RpHouses[h].addr);
        ImGui::PopStyleVar();
    }
    RpPText(0x94A3B8FF, "Tap a house for the GPS, then /buyhouse at the door (paid from the bank first).");
}
static void RpAppPets(float w)
{
    RpPTitle("Holo Pets");
    if(s_Rp.me.pet < RP_PET_COUNT && RpPBtn("Send my pet home##pd", w, UI::S(44), 0x334155FF)) RpSend(RPC_PET, 255);
    for(int p = 0; p < RP_PET_COUNT; ++p)
    {
        bool own = s_Rp.me.pets & (1u << p);
        char l[80]; snprintf(l, sizeof(l), "%s%s##pt%d", g_RpPets[p].name, own ? (s_Rp.me.pet == p ? "  (with you)" : "  - call") : ("  $" + std::to_string(g_RpPets[p].price) + " in the Shop").c_str(), p);
        if(RpPBtn(l, w, UI::S(46), own ? 0xDB2777FF : 0x334155FF, !own)) RpSend(RPC_PET, (uint8_t)p);
    }
}
static void RpAppEmotes(float w)
{
    RpPTitle("Emotes & Dances");
    if(RpPBtn("Stop (/e c)##ec", w, UI::S(44), 0x334155FF)) RpStopEmote();
    int cat = -1, col = 0;
    for(int i = 0; i < RP_EMOTE_COUNT; ++i)
    {
        const RpEmote& E = g_RpEmotes[i];
        if(E.cat != cat) { cat = E.cat; col = 0; RpPText(0xFDBA74FF, "%s", g_RpEmoteCats[cat]); }
        char l[64]; snprintf(l, sizeof(l), "%s##em%d", E.label, i);
        if(col++ % 2) ImGui::SameLine();
        if(RpPBtn(l, (w - UI::S(8)) / 2, UI::S(42), 0xC2410CFF))
        {
            if(E.cat == RPE_SHARED) SysMsg(COL_YELLOW, "Shared emote: /e %s [player id]", E.cmd);
            else { RpPlayEmote(i); s_Rp.phone = false; }
        }
    }
}
static void RpAppFeed(float w)
{
    RpPTitle("Vil Feed");
    float bw = (w - UI::S(8)) / 2;
    if(RpPBtn("New post##fp", bw, UI::S(48), 0x0EA5E9FF)) RpKb("Post to Vil Feed", RPKB_FEED, false);
    ImGui::SameLine();
    if(RpPBtn("Advert ($100)##fa", bw, UI::S(48), 0x059669FF)) RpKb("Advert text", RPKB_AD, false);
    for(int k = (int)s_Rp.feed.size() - 1; k >= 0; --k)
    {
        const RpPost& p = s_Rp.feed[k];
        RpPText(0x7DD3FCFF, "@%s", p.name); RpPText(0xFFFFFFFF, "%s", p.text);
        RpPText(0x64748BFF, "%d min ago", (int)((MP_NowMs() - p.at) / 60000)); ImGui::Separator();
    }
    if(s_Rp.feed.empty()) RpPText(0x94A3B8FF, "Nothing posted yet. Be the first.");
}
static void RpAppNews(float w)
{
    RpPTitle("Mega Vil News");
    const PktRpState& st = s_Rp.st;
    if(st.evKind) { RpPText(RP_SKY, "LIVE: %s", g_RpEventNames[st.evKind % RPEV_COUNT]); RpPText(0xFFFFFFFF, "%d:%02d left", st.evLeftS / 60, st.evLeftS % 60); if(RpPBtn("Go there (GPS)##nj", w, UI::S(48))) RpSend(RPC_EVENT_JOIN); }
    else RpPText(0xFFFFFFFF, "Next city event in %d:%02d", st.nextEvS / 60, st.nextEvS % 60);
    RpPSep();
    RpPText(0xFDE047FF, "Mega Lottery jackpot: $%d", st.lottoPot);
    RpPText(0x94A3B8FF, s_Rp.me.lotto ? "Your ticket: %d" : "No ticket yet ($500, numbers 1-60).", s_Rp.me.lotto);
    if(!s_Rp.me.lotto && RpPBtn("Buy a ticket##nl", w, UI::S(48), 0xCA8A04FF)) RpKb("Your number (1-60)", RPKB_LOTTO, true, 2);
    RpPSep();
    RpPText(0xFFFFFFFF, "City events (every 15 minutes):");
    for(int k = 1; k < RPEV_COUNT; ++k) RpPText(0xBAE6FDFF, "- %s", g_RpEventNames[k]);
}
static void RpAppSettings(float w)
{
    RpPTitle("Settings");
    static const char* names[4] = { "Sky Blue", "Neon", "Sunset", "Matrix" };
    static const uint32_t need[4] = { 0, RPIT_THEME_NEON, RPIT_THEME_SUNSET, RPIT_THEME_MATRIX };
    for(int i = 0; i < 4; ++i)
    {
        bool have = !need[i] || (s_Rp.me.items & need[i]);
        char l[64]; snprintf(l, sizeof(l), "%s%s##th%d", names[i], have ? (s_nRpTheme == i ? "  (on)" : "") : "  (Shop)", i);
        if(RpPBtn(l, w, UI::S(46), kRpThemeTop[i], !have)) s_nRpTheme = i;
    }
    RpPSep();
    RpPText(0x94A3B8FF, "Phone number %d  |  MegaVil OS %s", s_Rp.me.number, MP_VERSION_STR);
}

// ---- the phone itself --------------------------------------------------------------------------
static void RpDrawCallScreen(float w, float h)
{
    uint64_t now = MP_NowMs();
    bool incoming = s_Rp.ringFrom >= 0 && !s_Rp.ringOut && s_Rp.callWith < 0;
    bool outgoing = s_Rp.ringFrom >= 0 && s_Rp.ringOut && s_Rp.callWith < 0;
    const char* who = s_Rp.callWith >= 0 ? s_Rp.callName : s_Rp.ringName;
    int32_t num = s_Rp.callWith >= 0 ? s_Rp.callNum : s_Rp.ringNum;
    ImGui::Dummy(ImVec2(1, h * 0.12f));
    ImGui::SetWindowFontScale(1.5f);
    float tw = ImGui::CalcTextSize(who).x; ImGui::SetCursorPosX((w - tw) * 0.5f + ImGui::GetStyle().WindowPadding.x); RpPText(0xFFFFFFFF, "%s", who);
    ImGui::SetWindowFontScale(1.0f);
    char t[64];
    if(s_Rp.callWith >= 0) { int s = (int)((now - s_Rp.callStart) / 1000); snprintf(t, sizeof(t), "%d   -   %d:%02d", num, s / 60, s % 60); }
    else snprintf(t, sizeof(t), "%d   -   %s", num, incoming ? "incoming call..." : "calling...");
    tw = ImGui::CalcTextSize(t).x; ImGui::SetCursorPosX((w - tw) * 0.5f + ImGui::GetStyle().WindowPadding.x); RpPText(0xBAE6FDFF, "%s", t);
    // pulsing rings
    ImDrawList* dl = ImGui::GetWindowDrawList(); ImVec2 c = ImGui::GetCursorScreenPos(); c.x += w * 0.5f; c.y += h * 0.18f;
    for(int i = 0; i < 3; ++i) { float k = fmodf(now / 1200.0f + i / 3.0f, 1.0f); dl->AddCircle(c, UI::S(40) + k * UI::S(70), RpCol(RP_SKY, 1.0f - k), 40, UI::S(3)); }
    dl->AddCircleFilled(c, UI::S(42), RpCol(RP_SKY, 1), 40);
    { char ini[4] = { who[0] ? (char)toupper((unsigned char)who[0]) : '?', 0 }; float fs = UI::S(44); UI::OutlinedText(dl, fs, ImVec2(c.x - RpTextW(fs, ini) * 0.5f, c.y - fs * 0.55f), IM_COL32_WHITE, ini); }
    ImGui::Dummy(ImVec2(1, h * 0.40f));
    if(s_Rp.callWith >= 0) RpPText(0x86EFACFF, "Voice chat goes to this call. Hold the mic button to talk.");
    float bw = (w - UI::S(12)) / 2;
    if(incoming)
    {
        if(RpPBtn("Answer##ca", bw, UI::S(64), 0x16A34AFF)) RpSend(RPC_ANSWER);
        ImGui::SameLine();
        if(RpPBtn("Decline##cd", bw, UI::S(64), 0xDC2626FF)) RpSend(RPC_HANGUP);
    }
    else if(outgoing || s_Rp.callWith >= 0) { if(RpPBtn("End call##ce", w, UI::S(64), 0xDC2626FF)) RpSend(RPC_HANGUP); }
    else { RpPText(0x94A3B8FF, "Call ended."); if(RpPBtn("Close##cc", w, UI::S(52), 0x334155FF)) s_Rp.app = RPA_HOME; }
}
static void RpDrawPhoneButton()
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    // 3.25.1: only the phone icon - no circle / ring behind it - big, and OUTSIDE the game's button column.
    // It used to sit on the right edge at 62 % of the height, right on top of the fist / attack button.
    // Now its centre is 560 (1080p units) from the right edge: left of the game's buttons, below the info panel.
    const float s = UI::S(160);
    ImVec2 pos(ds.x - UI::S(560) - s * 0.5f, ds.y * 0.60f - s * 0.5f);
    BcwBeginOverlay("##RpPhoneBtn", ImVec2(pos.x - UI::S(6), pos.y - UI::S(6)), ImVec2(s + UI::S(12), s + UI::S(12)), 0.0f);
    ImGui::SetCursorScreenPos(pos);
    if(ImGui::InvisibleButton("##ph", ImVec2(s, s))) { if(s_Rp.phone) s_Rp.phone = false; else RpPhoneOpen(s_Rp.ringFrom >= 0 || s_Rp.callWith >= 0 ? -2 : RPA_HOME); }
    const bool down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const uint64_t nowMs = MP_NowMs();
    bool ring = s_Rp.ringFrom >= 0 && !s_Rp.ringOut && s_Rp.callWith < 0;
    // an incoming call: the icon shakes and flashes green (there is no background left to flash)
    const float shake = ring ? sinf((float)(nowMs % 100000) * 0.03f) * UI::S(6) : 0.0f;
    const ImVec2 c(pos.x + s * 0.5f + shake, pos.y + s * 0.5f);
    const float is = down ? s * 0.9f : s;
    const ImU32 tint = (ring && nowMs % 600 < 300) ? IM_COL32(120, 255, 160, 255) : IM_COL32_WHITE;
    ImVec2 uv(1, 1);
    if(void* tex = HUD::Texture("rp_phone.png", &uv))
        dl->AddImage((ImTextureID)tex, ImVec2(c.x - is * 0.5f, c.y - is * 0.5f), ImVec2(c.x + is * 0.5f, c.y + is * 0.5f), ImVec2(0, 0), uv, tint);
    else
    {   // the art is missing: a drawn phone (black body, light screen)
        const float bw = is * 0.30f, bh = is * 0.46f;
        dl->AddRectFilled(ImVec2(c.x - bw, c.y - bh), ImVec2(c.x + bw, c.y + bh), IM_COL32(12, 14, 20, 255), UI::S(12));
        dl->AddRectFilled(ImVec2(c.x - bw + UI::S(6), c.y - bh + UI::S(8)), ImVec2(c.x + bw - UI::S(6), c.y + bh - UI::S(14)), RpCol(RP_SKY, 1), UI::S(8));
        dl->AddRect(ImVec2(c.x - bw, c.y - bh), ImVec2(c.x + bw, c.y + bh), tint, UI::S(12), 0, UI::S(4));
    }
    if(s_Rp.unreadSms)
    {
        const ImVec2 b(c.x + is * 0.30f, c.y - is * 0.36f);
        dl->AddCircleFilled(b, UI::S(15), IM_COL32(239, 68, 68, 255));
        char n[8]; snprintf(n, sizeof(n), "%d", s_Rp.unreadSms > 9 ? 9 : s_Rp.unreadSms);
        UI::OutlinedText(dl, UI::S(20), ImVec2(b.x - UI::S(6), b.y - UI::S(11)), IM_COL32_WHITE, n);
    }
    BcwEndOverlay();
}
static void RenderRpPhone(float nowF)
{
    if(s_Rp.view != 2) return;
    RpDrawPhoneButton();
    if(s_Rp.app == RPA_CALL && s_Rp.ringFrom < 0 && s_Rp.callWith < 0 && s_Rp.phone && MP_NowMs() - s_Rp.ringAt > 4000) s_Rp.app = RPA_HOME;
    if(!s_Rp.phone || SampDialogOpen()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float H = ds.y * 0.86f, W = H * 0.49f;
    float x = ds.x - W - UI::S(380), y = (ds.y - H) * 0.5f;
    if(x < UI::S(10)) x = UI::S(10);
    if(VKeyboard::IsOpen()) y = UI::S(8) - (VKeyboard::Height() > H * 0.5f ? H * 0.3f : 0);
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##RpPhone", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float bez = UI::S(12), rad = UI::S(42);
    ImVec2 a(x, y), b(x + W, y + H), sa(x + bez, y + bez), sb(x + W - bez, y + H - bez);
    dl->AddRectFilled(a, b, IM_COL32(10, 10, 14, 255), rad);
    dl->AddRect(a, b, RpCol(RP_SKY, 0.9f), rad, 0, UI::S(2.5f));
    int th = s_nRpTheme & 3;
    dl->AddRectFilledMultiColor(sa, sb, RpCol(kRpThemeTop[th], 1), RpCol(kRpThemeTop[th], 1), RpCol(kRpThemeBot[th], 1), RpCol(kRpThemeBot[th], 1));
    // round the screen corners: paint the bezel colour over the corners
    dl->AddRect(ImVec2(sa.x - UI::S(6), sa.y - UI::S(6)), ImVec2(sb.x + UI::S(6), sb.y + UI::S(6)), IM_COL32(10, 10, 14, 255), rad - UI::S(4), 0, UI::S(12));
    // notch + status bar
    dl->AddRectFilled(ImVec2(x + W * 0.38f, sa.y), ImVec2(x + W * 0.62f, sa.y + UI::S(22)), IM_COL32(10, 10, 14, 255), UI::S(11), ImDrawFlags_RoundCornersBottom);
    time_t tt = time(nullptr); char clock[16]; strftime(clock, sizeof(clock), "%H:%M", localtime(&tt));
    UI::OutlinedText(dl, UI::S(18), ImVec2(sa.x + UI::S(26), sa.y + UI::S(4)), IM_COL32_WHITE, clock);
    const char* net = "MegaVil 6G  100%"; UI::OutlinedText(dl, UI::S(16), ImVec2(sb.x - RpTextW(UI::S(16), net) - UI::S(26), sa.y + UI::S(5)), IM_COL32_WHITE, net);
    // content
    const float pad = UI::S(16), top = UI::S(40), bottom = UI::S(64);
    float cw = (sb.x - sa.x) - pad * 2;
    ImGui::SetCursorScreenPos(ImVec2(sa.x + pad, sa.y + top));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::BeginChild("##rpscr", ImVec2(cw, (sb.y - sa.y) - top - bottom), false, ImGuiWindowFlags_NoScrollbar);
    float w = cw - UI::S(4);
    if(s_Rp.app == RPA_CALL) RpDrawCallScreen(w, (sb.y - sa.y) - top - bottom);
    else if(s_Rp.app == RPA_HOME)
    {
        // big clock + date like a lock screen widget
        ImGui::SetWindowFontScale(2.2f); float tw = ImGui::CalcTextSize(clock).x; ImGui::SetCursorPosX((w - tw) * 0.5f); RpPText(0xFFFFFFFF, "%s", clock); ImGui::SetWindowFontScale(1.0f);
        char dt[48]; strftime(dt, sizeof(dt), "%A, %d %B", localtime(&tt)); tw = ImGui::CalcTextSize(dt).x; ImGui::SetCursorPosX((w - tw) * 0.5f); RpPText(0xE0F2FEFF, "%s", dt);
        ImGui::Dummy(ImVec2(1, UI::S(10)));
        const int cols = 4; float gap = UI::S(10), isz = (w - gap * (cols - 1)) / cols;
        ImDrawList* cd = ImGui::GetWindowDrawList();
        ImVec2 auv(1, 1); void* apps = HUD::Texture("rp_apps.png", &auv);
        for(int i = 1; i < RPA_COUNT; ++i)
        {
            int k = i - 1; if(k % cols) ImGui::SameLine(0, gap);
            ImGui::PushID(i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool hit = ImGui::InvisibleButton("##app", ImVec2(isz, isz + UI::S(22)));
            bool held = ImGui::IsItemActive();
            float ic = isz * 0.78f; ImVec2 ia(p.x + (isz - ic) * 0.5f, p.y), ib(ia.x + ic, ia.y + ic);
            if(apps)
            {
                // 3.25: drawn app icons from the rp_apps.png atlas (6 x 3 cells of 84 px, icon 80 px inside)
                int cell = i - 1, c = cell % 6, r = cell / 6;
                float u0 = (c * 84 + 2) / 504.0f * auv.x, v0 = (r * 84 + 2) / 252.0f * auv.y;
                float u1 = (c * 84 + 82) / 504.0f * auv.x, v1 = (r * 84 + 82) / 252.0f * auv.y;
                cd->AddImage((ImTextureID)apps, ia, ib, ImVec2(u0, v0), ImVec2(u1, v1), held ? IM_COL32(200, 200, 200, 255) : IM_COL32_WHITE);
            }
            else
            {
                cd->AddRectFilled(ia, ib, RpCol(g_RpApps[i].col, held ? 0.7f : 1.0f), ic * 0.26f);
                cd->AddRectFilledMultiColor(ia, ImVec2(ib.x, ia.y + ic * 0.45f), IM_COL32(255, 255, 255, 60), IM_COL32(255, 255, 255, 60), IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0));
                float gfs = ic * 0.52f; UI::OutlinedText(cd, gfs, ImVec2((ia.x + ib.x - RpTextW(gfs, g_RpApps[i].glyph)) * 0.5f, ia.y + ic * 0.2f), IM_COL32_WHITE, g_RpApps[i].glyph);
            }
            float lfs = UI::S(14); const char* nm = g_RpApps[i].name; UI::OutlinedText(cd, lfs, ImVec2(p.x + (isz - RpTextW(lfs, nm)) * 0.5f, ib.y + UI::S(3)), IM_COL32_WHITE, nm);
            if(i == RPA_SMS && s_Rp.unreadSms) cd->AddCircleFilled(ImVec2(ib.x - UI::S(4), ia.y + UI::S(4)), UI::S(9), IM_COL32(239, 68, 68, 255));
            if(hit) { s_Rp.app = i; s_Rp.appSub = 0; if(i == RPA_SMS) { s_Rp.unreadSms = 0; s_Rp.smsThread = 0; } if(i == RPA_SHOP) s_nRpShopCat = -1; if(i == RPA_911) RpKb("911 - what happened?", RPKB_911, false); if(i == RPA_TAXI) RpKb("Taxi - where to? (optional)", RPKB_TAXI, false); }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(1, UI::S(8)));
        RpPText(0xE0F2FEFF, "Cash $%d  |  Bank $%d  |  Lv %d", s_Rp.me.cash, s_Rp.me.bank, s_Rp.me.level);
    }
    else switch(s_Rp.app)
    {
        case RPA_CONTACTS: RpAppContacts(w); break;
        case RPA_SMS:      RpAppSms(w); break;
        case RPA_DIAL:     RpAppDial(w); break;
        case RPA_BANK:     RpAppBank(w); break;
        case RPA_JOBS:     RpAppJobs(w); break;
        case RPA_GPS:      RpAppGps(w); break;
        case RPA_SHOP:     RpAppShop(w); break;
        case RPA_GARAGE:   RpAppGarage(w); break;
        case RPA_HOUSES:   RpAppHouses(w); break;
        case RPA_PETS:     RpAppPets(w); break;
        case RPA_EMOTES:   RpAppEmotes(w); break;
        case RPA_FEED:     RpAppFeed(w); break;
        case RPA_GAMES:    RpDrawGames(w); break;
        case RPA_NEWS:     RpAppNews(w); break;
        case RPA_911:      RpPTitle("911 Emergency"); RpPText(0xFFFFFFFF, "Police, paramedics and firefighters on duty get your location."); if(RpPBtn("Call 911##9", w, UI::S(64), 0xDC2626FF)) RpKb("911 - what happened?", RPKB_911, false); break;
        case RPA_TAXI:     RpPTitle("Mega Taxi"); RpPText(0xFFFFFFFF, "Taxi drivers get your location."); if(RpPBtn("Call a taxi##tx", w, UI::S(64), 0xCA8A04FF)) RpKb("Taxi - where to? (optional)", RPKB_TAXI, false); break;
        case RPA_SETTINGS: RpAppSettings(w); break;
        default: break;
    }
    RpDragScroll();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    // home bar: back (to the app grid) + close
    float hy = sb.y - bottom + UI::S(10), hbw = (cw - UI::S(10)) / 2;
    ImGui::SetCursorScreenPos(ImVec2(sa.x + pad, hy));
    if(RpPBtn("Home##phh", hbw, UI::S(44), 0x1E293BFF)) { s_Rp.app = (s_Rp.ringFrom >= 0 || s_Rp.callWith >= 0) && s_Rp.app != RPA_CALL ? RPA_CALL : RPA_HOME; s_Rp.appSub = 0; }
    ImGui::SameLine(0, UI::S(10));
    if(RpPBtn("Close##phc", hbw, UI::S(44), 0x1E293BFF)) s_Rp.phone = false;
    dl->AddRectFilled(ImVec2(x + W * 0.36f, sb.y - UI::S(10)), ImVec2(x + W * 0.64f, sb.y - UI::S(5)), IM_COL32(255, 255, 255, 200), UI::S(3));
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor();
    (void)nowF;
}
