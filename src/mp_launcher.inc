// =============================================================================================
// 2.25 MP Launcher: the Server page as an almost full-screen window (95% of the screen, 40%
// transparent), laid out like a desktop SA-MP launcher:
//   title bar  : MP Launcher  v3.04.6 ........................................... [X]
//   toolbar    : [Search server] [> Connect] [+] [trash] [refresh] [gear] ..... [Nickname] [sliders] [i]
//   sidebar    : SERVERS Internet / Favorites / Add server   (2.37: "Hosted" merged into Add server)
//                ADMIN   Players (kick + ban) / Bans
//                SETTINGS Connection / Display / Chat / Player / (sections from other mods)
//   centre     : HostName | Players | Ping | Mode | Region   (tap = select, tap again = join)
//   right      : Player | Score   and   Rule | Value of the selected server
// Plus the kick / ban dialog: "You are banned from the server!" Ban date / Administrator / Reason.
// The administrator is the one who hosts the server (this phone's player name).
// =============================================================================================

enum { LT_INTERNET = 0, LT_FAVORITES, LT_HOSTED, LT_ADD, LT_PLAYERS, LT_BANS, LT_SECTIONS };
static int   s_nLTab = LT_INTERNET;
static int   s_nLSel = -1;                         // selected row
static char  s_szLSelKey[48] = "";                 // name|port of the selected row (rows move)
static char  s_szSearch[32] = "";
static int   s_nSortCol = -1;                      // -1 = list order, 0 name, 1 players, 2 ping, 3 mode, 4 region
static bool  s_bSortAsc = true;
static char  s_szKickReason[MP_REASON_LEN] = "";
static int   s_nMgmtSel = -1;
static int   s_nBanSel = -1;
static char  s_szBanName[MP_NAME_LEN] = "";

struct KickInfo { bool show; bool banned; uint32_t when; char admin[MP_NAME_LEN]; char reason[MP_REASON_LEN]; char server[32]; };
static KickInfo s_Kick = {};

static void ShowKicked(const PktKicked& k)
{
    s_Kick.show = true; s_Kick.banned = k.banned != 0; s_Kick.when = k.when;
    MP_CopyStr(s_Kick.admin, k.admin, sizeof(s_Kick.admin));
    MP_CopyStr(s_Kick.reason, k.reason, sizeof(s_Kick.reason));
    MP_CopyStr(s_Kick.server, client.GetServerName()[0] ? client.GetServerName() : (s_szJobName[0] ? s_szJobName : "the server"), sizeof(s_Kick.server));
}

// ---- small drawing helpers -------------------------------------------------------------------
enum { IC_PLAY, IC_PLUS, IC_TRASH, IC_REFRESH, IC_GEAR, IC_SLIDERS, IC_INFO, IC_CLOSE, IC_GLOBE, IC_HEART,
       IC_BOX, IC_USERS, IC_BAN, IC_LIST, IC_SEARCH, IC_CHAT, IC_USER, IC_STOP, IC_REGION };

static void LIcon(ImDrawList* dl, int k, ImVec2 c, float r, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    switch(k)
    {
    case IC_PLAY:  dl->AddTriangle(ImVec2(c.x - r * 0.45f, c.y - r * 0.6f), ImVec2(c.x + r * 0.6f, c.y), ImVec2(c.x - r * 0.45f, c.y + r * 0.6f), col, t); break;
    case IC_STOP:  dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, r * 0.12f, 0, t); break;
    case IC_PLUS:  dl->AddLine(ImVec2(c.x - r * 0.65f, c.y), ImVec2(c.x + r * 0.65f, c.y), col, t); dl->AddLine(ImVec2(c.x, c.y - r * 0.65f), ImVec2(c.x, c.y + r * 0.65f), col, t); break;
    case IC_TRASH:
        dl->AddRect(ImVec2(c.x - r * 0.42f, c.y - r * 0.38f), ImVec2(c.x + r * 0.42f, c.y + r * 0.7f), col, r * 0.1f, 0, t);
        dl->AddLine(ImVec2(c.x - r * 0.62f, c.y - r * 0.4f), ImVec2(c.x + r * 0.62f, c.y - r * 0.4f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.18f, c.y - r * 0.62f), ImVec2(c.x + r * 0.18f, c.y - r * 0.62f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.14f, c.y - r * 0.15f), ImVec2(c.x - r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        dl->AddLine(ImVec2(c.x + r * 0.14f, c.y - r * 0.15f), ImVec2(c.x + r * 0.14f, c.y + r * 0.45f), col, t * 0.8f);
        break;
    case IC_REFRESH:
        dl->PathArcTo(c, r * 0.6f, -IM_PI * 0.35f, IM_PI * 1.45f, 24); dl->PathStroke(col, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.30f, c.y - r * 0.92f), ImVec2(c.x + r * 0.70f, c.y - r * 0.40f), ImVec2(c.x + r * 0.12f, c.y - r * 0.30f), col);
        break;
    case IC_GEAR:
        for(int i = 0; i < 8; ++i) { float a = i * IM_PI / 4; dl->AddLine(ImVec2(c.x + cosf(a) * r * 0.45f, c.y + sinf(a) * r * 0.45f), ImVec2(c.x + cosf(a) * r * 0.72f, c.y + sinf(a) * r * 0.72f), col, t * 1.6f); }
        dl->AddCircle(c, r * 0.48f, col, 20, t); dl->AddCircle(c, r * 0.18f, col, 12, t); break;
    case IC_SLIDERS:
        for(int i = 0; i < 2; ++i)
        {
            float y = c.y + (i ? r * 0.3f : -r * 0.3f), kx = c.x + (i ? -r * 0.25f : r * 0.25f);
            dl->AddLine(ImVec2(c.x - r * 0.7f, y), ImVec2(c.x + r * 0.7f, y), col, t);
            dl->AddCircleFilled(ImVec2(kx, y), r * 0.2f, IM_COL32(20, 20, 22, 255)); dl->AddCircle(ImVec2(kx, y), r * 0.2f, col, 12, t);
        }
        break;
    case IC_INFO:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.32f), t * 0.75f, col);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.08f), ImVec2(c.x, c.y + r * 0.38f), col, t); break;
    case IC_CLOSE: dl->AddLine(ImVec2(c.x - r * 0.5f, c.y - r * 0.5f), ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), col, t); dl->AddLine(ImVec2(c.x + r * 0.5f, c.y - r * 0.5f), ImVec2(c.x - r * 0.5f, c.y + r * 0.5f), col, t); break;
    case IC_GLOBE:
        dl->AddCircle(c, r * 0.7f, col, 24, t);
        for(int i = 0; i <= 20; ++i) { float a = i * IM_PI * 2 / 20; dl->PathLineTo(ImVec2(c.x + cosf(a) * r * 0.3f, c.y + sinf(a) * r * 0.7f)); }
        dl->PathStroke(col, 0, t * 0.8f);
        dl->AddLine(ImVec2(c.x - r * 0.7f, c.y), ImVec2(c.x + r * 0.7f, c.y), col, t * 0.8f); break;
    case IC_HEART:
    {
        ImVec2 p0(c.x, c.y + r * 0.62f);
        dl->PathLineTo(p0);
        dl->PathBezierCubicCurveTo(ImVec2(c.x - r * 0.95f, c.y), ImVec2(c.x - r * 0.6f, c.y - r * 0.85f), ImVec2(c.x, c.y - r * 0.3f), 12);
        dl->PathBezierCubicCurveTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.85f), ImVec2(c.x + r * 0.95f, c.y), p0, 12);
        dl->PathStroke(col, ImDrawFlags_Closed, t); break;
    }
    case IC_BOX:
        dl->AddQuad(ImVec2(c.x, c.y - r * 0.7f), ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x, c.y), ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y - r * 0.35f), ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y - r * 0.35f), ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), col, t);
        dl->AddLine(ImVec2(c.x, c.y), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x - r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t);
        dl->AddLine(ImVec2(c.x + r * 0.65f, c.y + r * 0.35f), ImVec2(c.x, c.y + r * 0.7f), col, t); break;
    case IC_USERS:
        dl->AddCircle(ImVec2(c.x - r * 0.22f, c.y - r * 0.3f), r * 0.24f, col, 14, t);
        dl->PathArcTo(ImVec2(c.x - r * 0.22f, c.y + r * 0.55f), r * 0.45f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
        dl->AddCircle(ImVec2(c.x + r * 0.35f, c.y - r * 0.22f), r * 0.18f, col, 12, t * 0.8f);
        dl->PathArcTo(ImVec2(c.x + r * 0.38f, c.y + r * 0.5f), r * 0.32f, IM_PI * 1.15f, IM_PI * 2, 10); dl->PathStroke(col, 0, t * 0.8f); break;
    case IC_USER:
        dl->AddCircle(ImVec2(c.x, c.y - r * 0.28f), r * 0.28f, col, 16, t);
        dl->PathArcTo(ImVec2(c.x, c.y + r * 0.62f), r * 0.55f, IM_PI, IM_PI * 2, 14); dl->PathStroke(col, 0, t); break;
    case IC_BAN:
        dl->AddCircle(c, r * 0.65f, col, 24, t);
        dl->AddLine(ImVec2(c.x - r * 0.46f, c.y + r * 0.46f), ImVec2(c.x + r * 0.46f, c.y - r * 0.46f), col, t); break;
    case IC_LIST:
        for(int i = -1; i <= 1; ++i) { float y = c.y + i * r * 0.4f; dl->AddCircleFilled(ImVec2(c.x - r * 0.55f, y), t * 0.7f, col); dl->AddLine(ImVec2(c.x - r * 0.3f, y), ImVec2(c.x + r * 0.65f, y), col, t); }
        break;
    case IC_SEARCH:
        dl->AddCircle(ImVec2(c.x - r * 0.12f, c.y - r * 0.12f), r * 0.42f, col, 18, t);
        dl->AddLine(ImVec2(c.x + r * 0.2f, c.y + r * 0.2f), ImVec2(c.x + r * 0.62f, c.y + r * 0.62f), col, t * 1.2f); break;
    case IC_CHAT:
        dl->AddRect(ImVec2(c.x - r * 0.65f, c.y - r * 0.5f), ImVec2(c.x + r * 0.65f, c.y + r * 0.3f), col, r * 0.2f, 0, t);
        dl->AddTriangleFilled(ImVec2(c.x - r * 0.3f, c.y + r * 0.28f), ImVec2(c.x - r * 0.05f, c.y + r * 0.28f), ImVec2(c.x - r * 0.38f, c.y + r * 0.62f), col); break;
    case IC_REGION:
    {   // 3.25.1: map pin = the region switch (round head, point at the bottom, a hole in the middle)
        const ImVec2 hc(c.x, c.y - r * 0.2f); const float pr = r * 0.46f, tipY = c.y + r * 0.75f;
        const float ta = acosf(pr / (tipY - hc.y));   // where the two straight sides leave the head
        dl->PathArcTo(hc, pr, IM_PI * 0.5f + ta, IM_PI * 2.5f - ta, 24);
        dl->PathLineTo(ImVec2(c.x, tipY));
        dl->PathStroke(col, ImDrawFlags_Closed, t);
        dl->AddCircle(hc, pr * 0.38f, col, 12, t);
        break;
    }
    }
}

static void LPadlock(ImDrawList* dl, ImVec2 c, float r, bool locked, ImU32 col)
{
    float t = ImMax(1.5f, r * 0.14f);
    dl->AddRect(ImVec2(c.x - r * 0.5f, c.y - r * 0.05f), ImVec2(c.x + r * 0.5f, c.y + r * 0.62f), col, r * 0.12f, 0, t);
    if(locked) { dl->PathArcTo(ImVec2(c.x, c.y - r * 0.12f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.32f, c.y - r * 0.12f), ImVec2(c.x - r * 0.32f, c.y - r * 0.05f), col, t); dl->AddLine(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), ImVec2(c.x + r * 0.32f, c.y - r * 0.05f), col, t); }
    else       { dl->PathArcTo(ImVec2(c.x + r * 0.3f, c.y - r * 0.2f), r * 0.32f, IM_PI, IM_PI * 2, 12); dl->PathStroke(col, 0, t);
                 dl->AddLine(ImVec2(c.x - r * 0.02f, c.y - r * 0.2f), ImVec2(c.x - r * 0.02f, c.y - r * 0.05f), col, t); }
}

// drag a child window with the finger to scroll it
static void LDragScroll()
{
    if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseDragging(0, UI::S(14)))
    {
        float dy = ImGui::GetIO().MouseDelta.y;
        if(dy != 0) ImGui::SetScrollY(ImGui::GetScrollY() - dy);
    }
}

// toolbar button: icon + optional label, flat, white highlight while pressed
static bool LToolButton(const char* id, int icon, const char* label, float h, bool enabled = true, bool on = false)
{
    float isz = h * 0.42f;
    float w = label ? (h * 0.55f + isz + ImGui::CalcTextSize(label).x + h * 0.35f) : h;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    if(!enabled) ImGui::BeginDisabled();
    bool hit = ImGui::InvisibleButton("##tb", ImVec2(w, h));
    if(!enabled) ImGui::EndDisabled();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(on || (enabled && ImGui::IsItemActive())) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), on ? IM_COL32(255, 255, 255, 40) : IM_COL32(255, 255, 255, 28), UI::S(10));
    ImU32 col = enabled ? IM_COL32(235, 235, 235, 255) : IM_COL32(120, 120, 120, 255);
    float cx = label ? p.x + h * 0.32f + isz * 0.5f : p.x + w * 0.5f;
    LIcon(dl, icon, ImVec2(cx, p.y + h * 0.5f), isz * 0.75f, col);
    if(label) dl->AddText(ImVec2(cx + isz * 0.5f + h * 0.22f, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    ImGui::PopID();
    return hit && enabled;
}

// framed "input" box that opens the game keyboard
static bool LInputBox(const char* id, const char* value, const char* placeholder, float w, float h, int icon = -1)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##ib", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(0, 0, 0, 110), UI::S(10));
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::IsItemActive() ? IM_COL32(255, 255, 255, 120) : IM_COL32(255, 255, 255, 38), UI::S(10), 0, UI::S(2));
    float tx = p.x + UI::S(18);
    if(icon >= 0) { LIcon(dl, icon, ImVec2(p.x + UI::S(30), p.y + h * 0.5f), h * 0.26f, IM_COL32(160, 160, 160, 255)); tx = p.x + UI::S(56); }
    dl->PushClipRect(p, ImVec2(p.x + w - UI::S(8), p.y + h), true);
    bool empty = !value || !value[0];
    dl->AddText(ImVec2(tx, p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), empty ? IM_COL32(140, 140, 140, 255) : IM_COL32_WHITE, empty ? placeholder : value);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}

static void OnSearch(const char* t, void*) { MP_CopyStr(s_szSearch, t ? t : "", sizeof(s_szSearch)); s_nLSel = -1; }
static void OnKickReason(const char* t, void*) { MP_CopyStr(s_szKickReason, t ? t : "", sizeof(s_szKickReason)); }
static void OnBanName(const char* t, void*) { MP_CopyStr(s_szBanName, t ? t : "", sizeof(s_szBanName)); }

// ---- server rows -----------------------------------------------------------------------------
struct LRow
{
    int  kind;            // 0 default, 1 live (internet / Wi-Fi), 2 saved (favourite), 3 Brief Case War (3.04), 4 MW3 (3.23), 5 Mega Vil Futuristic (3.25)
    char name[40];
    int  players, maxp, ping, region, icon;
    bool locked, live, on, mine, internet;
    char ip[64]; int port; int saved;
    char lang[MP_LANG_LEN];   // 2.25
};

static bool LMatch(const char* name, const char* lang = nullptr)
{
    if(!s_szSearch[0]) return true;
    char a[64], b[32]; MP_CopyStr(a, name, sizeof(a)); MP_CopyStr(b, s_szSearch, sizeof(b));
    for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    for(char* c = b; *c; ++c) *c = (char)tolower((unsigned char)*c);
    if(strstr(a, b)) return true;
    if(!lang || !lang[0]) return false;   // 2.25: search also finds the language ("filipino")
    MP_CopyStr(a, lang, sizeof(a)); for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return strstr(a, b) != nullptr;
}

static void LBuildRows(int tab, const std::vector<LanServer>& nearby, std::vector<LRow>& rows)
{
    rows.clear();
    if(tab == LT_INTERNET)
    {
        LRow r; memset(&r, 0, sizeof(r));
        r.kind = 0; MP_CopyStr(r.name, MP_DEFAULT_SERVER_NAME, sizeof(r.name)); r.port = MP_DEFAULT_PORT;
        r.players = -1; r.maxp = -1; r.ping = -1; r.region = -1; r.icon = 0; r.saved = -1;
        r.on = IsOnDefault();
        int found = -1;
        for(int i = 0; i < (int)nearby.size(); ++i)   // 2.29: the 24/7 copy first
            if(IsDefaultServer(nearby[i].name, nearby[i].port) && (found < 0 || (nearby[i].dedicated && !nearby[found].dedicated))) found = i;
        if(r.on && IsConnected())  { r.players = OnlineCount(); r.maxp = OnlineMax(); r.ping = MP::DisplayPing(); r.live = true; }
        else if(IsHostingDefault()) { r.players = s_Server.GetPlayerCount(); r.maxp = s_Server.GetMaxPlayers(); r.ping = 0; r.live = true; r.mine = true; }
        else if(found >= 0)        { r.players = nearby[found].players; r.maxp = nearby[found].maxPlayers; r.ping = nearby[found].ping; r.live = true; r.internet = nearby[found].token != 0; }
        else                       { r.players = 0; r.maxp = MP_STD_MAX_PLAYERS; r.live = true; }   // 2.29: always there, 0/100 - Join starts it
        MP_CopyStr(r.lang, (found >= 0 && nearby[found].lang[0]) ? nearby[found].lang : "All", sizeof(r.lang));
        if(LMatch(r.name)) rows.push_back(r);
        {   // 3.04: Brief Case War - pinned under the default server, always there (0/20) like it
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 3; MP_CopyStr(w.name, MP_BCW_SERVER_NAME, sizeof(w.name)); w.port = MP_BCW_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnBcw();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(nearby[i].port == MP_BCW_PORT && MP_IsBcwServerName(nearby[i].name) && (bf < 0 || (nearby[i].dedicated && !nearby[bf].dedicated))) bf = i;
            bool hosting = s_Server.IsRunning() && s_Server.IsBcwServer();
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_BCW_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_BCW_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            if(LMatch(w.name) || LMatch("briefcase war")) rows.push_back(w);
        }
        {   // 3.23: Call Of Duty: Modern Warfare 3 - 24/7 featured, pinned under Brief Case War, the VPS hosts it, or a phone when the VPS is down (3.23.1)
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 4; MP_CopyStr(w.name, MP_MW3_SERVER_NAME, sizeof(w.name)); w.port = MP_MW3_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnMw3();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(MP_IsMw3ServerName(nearby[i].name) && (bf < 0 || (nearby[i].dedicated && !nearby[bf].dedicated))) bf = i;
            bool hosting = s_Server.IsRunning() && s_Server.IsMw3Server();   // 3.23.1: a phone keeps it up when the VPS is down
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_MW3_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_MW3_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            if(LMatch(w.name) || LMatch("mw3") || LMatch("modern warfare")) rows.push_back(w);
        }
        {   // 3.25: Mega Vil Futuristic - 24/7 featured Roleplay, pinned under MW3 (VPS, or the Broker standby phones)
            LRow w; memset(&w, 0, sizeof(w));
            w.kind = 5; MP_CopyStr(w.name, MP_RP_SERVER_NAME, sizeof(w.name)); w.port = MP_RP_PORT;
            w.region = -1; w.icon = 0; w.saved = -1; w.live = true;
            w.on = MP::IsOnRp();
            int bf = -1;
            for(int i = 0; i < (int)nearby.size(); ++i)
                if(MP_IsRpServerName(nearby[i].name) && (bf < 0 || (nearby[i].dedicated && !nearby[bf].dedicated))) bf = i;
            bool hosting = s_Server.IsRunning() && s_Server.IsRpServer();
            if(w.on && IsConnected())  { w.players = OnlineCount(); w.maxp = OnlineMax(); w.ping = MP::DisplayPing(); }
            else if(hosting)           { w.players = s_Server.GetPlayerCount(); w.maxp = MP_RP_MAX_PLAYERS; w.ping = 0; w.mine = true; }
            else if(bf >= 0)           { w.players = nearby[bf].players; w.maxp = nearby[bf].maxPlayers; w.ping = nearby[bf].ping; w.internet = nearby[bf].token != 0; }
            else                       { w.players = 0; w.maxp = MP_RP_MAX_PLAYERS; w.ping = -1; }
            MP_CopyStr(w.lang, "All", sizeof(w.lang));
            if(LMatch(w.name) || LMatch("roleplay") || LMatch("rp") || LMatch("mega vil")) rows.push_back(w);
        }
        for(auto& sv : nearby)
        {
            if(IsDefaultServer(sv.name, sv.port)) continue;
            if(MP_IsBcwServerName(sv.name)) continue;   // 3.04: the pinned row
            if(MP_IsMw3ServerName(sv.name)) continue;   // 3.23: the pinned row
            if(MP_IsRpServerName(sv.name)) continue;    // 3.25: the pinned row
            if(!settings.lobbyAuto && sv.token != 0 && sv.region != settings.region) continue;   // 3.25: region switch (Wi-Fi servers always show)
            LRow q; memset(&q, 0, sizeof(q));
            q.kind = 1; MP_CopyStr(q.name, sv.name, sizeof(q.name)); MP_CopyStr(q.ip, sv.ip, sizeof(q.ip)); q.port = sv.port;
            q.players = sv.players; q.maxp = sv.maxPlayers; q.ping = sv.ping; q.region = sv.region; q.icon = sv.icon;
            q.locked = sv.locked != 0; q.live = true; q.internet = sv.token != 0; q.saved = -1;
            q.on = IsConnectedTo(sv.ip, sv.port);
            MP_CopyStr(q.lang, sv.lang, sizeof(q.lang));
            if(LMatch(q.name, q.lang)) rows.push_back(q);
        }
    }
    else if(tab == LT_FAVORITES)
    {
        for(int i = 0; i < ServerList::Count(); ++i)
        {
            ServerEntry* e = ServerList::Get(i);
            if(!e || !LMatch(e->name, e->liveLang[0] ? e->liveLang : e->lang)) continue;
            LRow q; memset(&q, 0, sizeof(q));
            q.kind = 2; q.saved = i; MP_CopyStr(q.name, e->name, sizeof(q.name)); q.port = e->port; q.region = e->region;
            const LanServer* lan = nullptr;
            for(auto& sv : nearby) if(sv.port == e->port && !strcasecmp(sv.name, e->name)) { lan = &sv; break; }
            q.mine = s_Server.IsRunning() && s_Server.GetPort() == e->port && !strcasecmp(s_Server.GetName(), e->name);
            q.on = client.GetState() != NET_DISCONNECTED && client.GetPort() == e->port &&
                   (IsConnectedTo(e->host, e->port) || (lan && !strcmp(client.GetHost(), lan->ip)) ||
                    (q.mine && IsLocalHost(client.GetHost())) || (client.GetServerName()[0] && !strcasecmp(client.GetServerName(), e->name)));
            q.locked = e->password[0] || e->locked;
            q.players = q.maxp = q.ping = -1;
            if(q.on && IsConnected()) { q.players = OnlineCount(); q.maxp = OnlineMax(); q.ping = MP::DisplayPing(); q.live = true; }
            else if(q.mine)           { q.players = s_Server.GetPlayerCount(); q.maxp = s_Server.GetMaxPlayers(); q.ping = 0; q.live = true; }
            else if(e->online)        { q.players = e->players; q.maxp = e->maxPlayers; q.ping = e->ping; q.live = true; }
            else if(lan)              { q.players = lan->players; q.maxp = lan->maxPlayers; q.ping = lan->ping; q.live = true; q.internet = lan->token != 0; }
            q.icon = q.mine ? settings.serverIcon : (e->online ? e->icon : (lan ? lan->icon : 0));
            const char* lg = e->online && e->liveLang[0] ? e->liveLang : (lan && lan->lang[0] ? lan->lang : e->lang);   // live, else saved
            MP_CopyStr(q.lang, lg, sizeof(q.lang));
            rows.push_back(q);
        }
    }
    if(s_nSortCol >= 0 && rows.size() > 1)
    {
        size_t from = 0;   // the default server + Brief Case War stay on top
        while(tab == LT_INTERNET && from < rows.size() && (rows[from].kind == 0 || rows[from].kind == 3 || rows[from].kind == 4 || rows[from].kind == 5)) ++from;
        auto key = [](const LRow& r, int c) -> int { return c == 1 ? r.players : (c == 2 ? (r.ping < 0 ? 99999 : r.ping) : (c == 3 ? r.region : 0)); };
        std::stable_sort(rows.begin() + from, rows.end(), [&](const LRow& a, const LRow& b)
        {
            int c = s_nSortCol; bool lt;
            const char* sa = c == 4 ? a.lang : a.name; const char* sb = c == 4 ? b.lang : b.name;
            if(c == 0 || c == 4) lt = strcasecmp(sa, sb) < 0;
            else lt = key(a, c) < key(b, c);
            bool gt = (c == 0 || c == 4) ? strcasecmp(sb, sa) < 0 : key(b, c) < key(a, c);
            return s_bSortAsc ? lt : gt;
        });
    }
}

// 3.10: tapping the server you are already on (its row, Join / Connect) used to leave it and go
// through the whole connection again. Now it only says so; leaving is the Leave button.
static bool LAlreadyOn(const LRow& r)
{
    if(!r.on || client.GetState() == NET_DISCONNECTED) return false;
    SysMsg(COL_SYSTEM, client.GetState() == NET_CONNECTED ? "You are already connected to %s." : "Already connecting to %s...", r.name[0] ? r.name : "this server");
    return true;
}
static void LLeave(const LRow& r) { if(r.kind == 0) { if(IsOnDefault()) LeaveServer("Disconnected from the server."); } else LeaveServer("Left the server."); }
static void LJoin(const LRow& r)
{
    if(LAlreadyOn(r)) return;
    if(r.kind == 0) { if(IsOnDefault()) MP::ToggleConnect(); else { LeaveServer(nullptr); MP::ToggleConnect(); } return; }
    if(r.kind == 3) { if(!MP::IsOnBcw()) LeaveServer(nullptr); MP::JoinBcw(); return; }   // 3.04
    if(r.kind == 4) { if(!MP::IsOnMw3()) LeaveServer(nullptr); MP::JoinMw3(); return; }   // 3.23
    if(r.kind == 5) { if(!MP::IsOnRp()) LeaveServer(nullptr); MP::JoinRp(); return; }     // 3.25
    if(r.on) { LeaveServer("Left the server."); return; }
    if(r.kind == 1) MP::JoinServer(r.ip, r.port, r.name);
    else if(ServerEntry* e = ServerList::Get(r.saved)) MP::JoinServer(e->host, e->port, e->name);
}

static void LRowKey(const LRow& r, char* out, size_t n) { snprintf(out, n, "%d|%s|%d", r.kind, r.name, r.port); }

// ---- 3.25: region switch: Auto | Asia | Europe | NA | Oceania --------------------------------------
// 3.25.1: it is an icon button (map pin) in the toolbar, next to the gear. Tapping it opens this small list under
// the icon (it used to be a row of tabs above the server table).
// Auto = the lobby uses the lowest ping region and the list shows every region. Picking a region moves
// the lobby to that region's broker, makes it your hosting region and shows only its servers (the
// featured 24/7 servers and Wi-Fi servers always stay in the list).
static bool   s_bRegionPick = false;            // the region list is open
static ImVec2 s_vRegionBtn(0, 0);               // top-left of the toolbar icon (set every frame by the toolbar)
static float  s_fRegionBtn = 0;                 // its size

static void LRegionChoose(int i)                // i < 0 = Auto
{
    if(i < 0)
    {
        settings.lobbyAuto = true;
        int best = Online::BestRegion();
        if(best >= 0) { settings.region = best; s_Server.SetRegion(best); }
        SysMsg(COL_SYSTEM, "Region: Auto (lowest ping)%s%s. All regions are listed.", best >= 0 ? " - " : "", best >= 0 ? g_MPRegionNames[best] : "");
    }
    else
    {
        settings.lobbyAuto = false; settings.region = i; settings.regionPicked = true;
        s_Server.SetRegion(i);
        int ms = Online::RegionPingMs(i);
        if(ms >= 0) SysMsg(COL_SYSTEM, "Region: %s (%d ms). The lobby moves to the %s broker; only %s servers are listed.", g_MPRegionNames[i], ms, g_MPRegionNames[i], g_MPRegionNames[i]);
        else SysMsg(COL_SYSTEM, "Region: %s. Only %s servers are listed.", g_MPRegionNames[i], g_MPRegionNames[i]);
    }
    Online::SetRegionPrefs(settings.region, settings.lobbyAuto);
    s_nLSel = -1; s_szLSelKey[0] = 0;
    SaveConfig();
}

static void LRegionPicker(const std::vector<LanServer>& nearby)
{
    if(!s_bRegionPick) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float rowH = UI::S(62), gap = UI::S(6), pw = UI::S(12);
    const float w = ImMin(ImMax(UI::S(470), ds.x * 0.26f), ds.x - UI::S(20));
    const float h = pw * 2 + (REGION_COUNT + 1) * rowH + REGION_COUNT * gap;
    float x = s_vRegionBtn.x, y = s_vRegionBtn.y + s_fRegionBtn + UI::S(8);
    if(x + w > ds.x - UI::S(10)) x = ds.x - UI::S(10) - w;
    if(y + h > ds.y - UI::S(10)) y = ds.y - UI::S(10) - h;
    int cnt[REGION_COUNT] = { 0 }, all = 0;
    for(auto& sv : nearby) if(sv.token != 0 && sv.region < REGION_COUNT) { ++cnt[sv.region]; ++all; }
    const int cur = Online::CurrentRegion();

    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pw, pw));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(14));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(2));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 0.97f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.20f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPRegionPick", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetWindowPos();
        for(int i = -1; i < REGION_COUNT; ++i)
        {
            ImVec2 a(p.x + pw, p.y + pw + (i + 1) * (rowH + gap)), b(a.x + w - pw * 2, a.y + rowH);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i + 100);
            bool tap = ImGui::InvisibleButton("##reg", ImVec2(b.x - a.x, rowH));
            bool held = ImGui::IsItemActive();
            ImGui::PopID();
            const bool sel = i < 0 ? settings.lobbyAuto : (!settings.lobbyAuto && settings.region == i);
            dl->AddRectFilled(a, b, sel ? IM_COL32(255, 255, 255, held ? 200 : 235) : IM_COL32(255, 255, 255, held ? 45 : 22), UI::S(10));
            if(!sel) dl->AddRect(a, b, IM_COL32(255, 255, 255, 40), UI::S(10), 0, UI::S(1.5f));
            const ImU32 tc = sel ? IM_COL32(15, 15, 15, 255) : IM_COL32(235, 235, 235, 255), sc = sel ? IM_COL32(60, 60, 60, 255) : IM_COL32(160, 160, 160, 255);
            char l1[32], l2[48];
            if(i < 0)
            {
                int best = Online::BestRegion();
                snprintf(l1, sizeof(l1), "Auto");
                if(best >= 0) snprintf(l2, sizeof(l2), "%s  |  %d servers", g_MPRegionNames[best], all); else snprintf(l2, sizeof(l2), "lowest ping  |  %d", all);
            }
            else
            {
                snprintf(l1, sizeof(l1), "%s", g_MPRegionNames[i]);
                int ms = Online::RegionPingMs(i);
                if(ms >= 0) snprintf(l2, sizeof(l2), "%d ms  |  %d servers", ms, cnt[i]);
                else snprintf(l2, sizeof(l2), "%s  |  %d servers", ms == -2 ? "offline" : "-- ms", cnt[i]);
            }
            const float fs1 = UI::S(26), fs2 = UI::S(19);
            const ImVec2 s2 = UI::Font()->CalcTextSizeA(fs2, FLT_MAX, 0, l2);
            dl->AddText(UI::Font(), fs1, ImVec2(a.x + UI::S(34), a.y + (rowH - fs1) * 0.5f), tc, l1);
            dl->AddText(UI::Font(), fs2, ImVec2(b.x - UI::S(16) - s2.x, a.y + (rowH - fs2) * 0.5f), sc, l2);
            if(i >= 0 && cur == i) dl->AddCircleFilled(ImVec2(a.x + UI::S(18), a.y + rowH * 0.5f), UI::S(6), IM_COL32(34, 197, 94, 255));   // lobby broker in use
            if(tap)
            {
                if(!sel) LRegionChoose(i);
                s_bRegionPick = false;
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    // a tap anywhere else (not on the picker, not on the icon that toggles it) closes the list
    if(s_bRegionPick && ImGui::IsMouseClicked(0)
       && !ImGui::IsMouseHoveringRect(ImVec2(x, y), ImVec2(x + w, y + h), false)
       && !ImGui::IsMouseHoveringRect(s_vRegionBtn, ImVec2(s_vRegionBtn.x + s_fRegionBtn, s_vRegionBtn.y + s_fRegionBtn), false))
        s_bRegionPick = false;
}

// ---- centre table ------------------------------------------------------------------------------
static void LTable(std::vector<LRow>& rows, float w, float h)
{
    ImGui::BeginChild("##ltable", ImVec2(w, h), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hdrH = UI::S(66), rowH = UI::S(70);
    float W = ImGui::GetContentRegionAvail().x;
    // columns
    float cLock = UI::S(16), cIcon = UI::S(64), cName = UI::S(134);
    float cPl = W * 0.42f, cPing = W * 0.53f, cMode = W * 0.62f, cReg = W * 0.82f;
    static const char* hdr[5] = { "HostName", "Players", "Ping", "Mode", "Language" };
    const float cx[5] = { cName, cPl, cPing, cMode, cReg };
    const float cw[5] = { cPl - cName, cPing - cPl, cMode - cPing, cReg - cMode, W - cReg };
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        for(int c = 0; c < 5; ++c)
        {
            ImGui::SetCursorScreenPos(ImVec2(p.x + cx[c], p.y));
            ImGui::PushID(c);
            if(ImGui::InvisibleButton("##hdr", ImVec2(cw[c], hdrH)))
            {
                if(s_nSortCol == c) { if(!s_bSortAsc) s_nSortCol = -1; s_bSortAsc = !s_bSortAsc; }
                else { s_nSortCol = c; s_bSortAsc = true; }
            }
            ImGui::PopID();
            ImU32 tc = IM_COL32(205, 205, 205, 255);
            float ty = p.y + (hdrH - ImGui::GetTextLineHeight()) * 0.5f;
            dl->AddText(ImVec2(p.x + cx[c], ty), tc, hdr[c]);
            float ax = p.x + cx[c] + ImGui::CalcTextSize(hdr[c]).x + UI::S(16), ay = p.y + hdrH * 0.5f, s = UI::S(6);
            ImU32 up = (s_nSortCol == c && s_bSortAsc) ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 255);
            ImU32 dn = (s_nSortCol == c && !s_bSortAsc) ? IM_COL32_WHITE : IM_COL32(120, 120, 120, 255);
            dl->AddTriangleFilled(ImVec2(ax - s, ay - UI::S(3)), ImVec2(ax + s, ay - UI::S(3)), ImVec2(ax, ay - UI::S(3) - s * 1.2f), up);
            dl->AddTriangleFilled(ImVec2(ax - s, ay + UI::S(3)), ImVec2(ax + s, ay + UI::S(3)), ImVec2(ax, ay + UI::S(3) + s * 1.2f), dn);
        }
        dl->AddLine(ImVec2(p.x, p.y + hdrH), ImVec2(p.x + W, p.y + hdrH), IM_COL32(255, 255, 255, 30), UI::S(2));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + hdrH + UI::S(2)));
    }
    ImGui::BeginChild("##lrows", ImVec2(0, 0), false);
    dl = ImGui::GetWindowDrawList();
    W = ImGui::GetContentRegionAvail().x;
    char key[48];
    s_nLSel = -1;
    for(int i = 0; i < (int)rows.size(); ++i) { LRowKey(rows[i], key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) s_nLSel = i; }
    for(int i = 0; i < (int)rows.size(); ++i)
    {
        LRow& r = rows[i];
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        bool tap = ImGui::InvisibleButton("##r", ImVec2(W, rowH));
        bool sel = s_nLSel == i;
        if(r.kind == 3)
        {   // 3.04: Brief Case War banner: red highlight + red outline + briefcase texture
            float pulse = 0.75f + 0.25f * sinf(MP_NowMs() * 0.004f);
            dl->AddRectFilledMultiColor(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(200, 20, 30, (int)(150 * pulse)), IM_COL32(120, 10, 18, 70), IM_COL32(120, 10, 18, 70), IM_COL32(200, 20, 30, (int)(150 * pulse)));
            dl->AddRect(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 50, 60, 255), UI::S(6), 0, UI::S(sel ? 4 : 2.5f));
            ImVec2 buv(1, 1);
            if(void* bt = HUD::Texture("bcw_briefcase.png", &buv))
            {   // big faded briefcase on the right of the banner
                float bs = rowH * 1.25f;
                ImVec2 ba(a.x + W - bs - UI::S(10), a.y + rowH * 0.5f - bs * 0.5f);
                dl->PushClipRect(a, ImVec2(a.x + W, a.y + rowH), true);
                dl->AddImage((ImTextureID)bt, ba, ImVec2(ba.x + bs, ba.y + bs), ImVec2(0, 0), buv, IM_COL32(255, 255, 255, 70));
                dl->PopClipRect();
            }
        }
        if(r.kind == 4)
        {   // 3.23: MW3 banner: green highlight + green outline + two guns
            float pulse = 0.75f + 0.25f * sinf(MP_NowMs() * 0.004f);
            dl->AddRectFilledMultiColor(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(25, 170, 60, (int)(150 * pulse)), IM_COL32(10, 90, 30, 70), IM_COL32(10, 90, 30, 70), IM_COL32(25, 170, 60, (int)(150 * pulse)));
            dl->AddRect(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(60, 255, 106, 255), UI::S(6), 0, UI::S(sel ? 4 : 2.5f));
            ImVec2 buv(1, 1);
            if(void* bt = HUD::Texture("mw3_guns.png", &buv))
            {
                float bs = rowH * 1.25f;
                ImVec2 ba(a.x + W - bs - UI::S(10), a.y + rowH * 0.5f - bs * 0.5f);
                dl->PushClipRect(a, ImVec2(a.x + W, a.y + rowH), true);
                dl->AddImage((ImTextureID)bt, ba, ImVec2(ba.x + bs, ba.y + bs), ImVec2(0, 0), buv, IM_COL32(255, 255, 255, 70));
                dl->PopClipRect();
            }
        }
        if(r.kind == 5)
        {   // 3.25: Mega Vil Futuristic banner: sky blue highlight + sky blue outline + the RP phone faded on the right
            float pulse = 0.75f + 0.25f * sinf(MP_NowMs() * 0.004f);
            dl->AddRectFilledMultiColor(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(56, 189, 248, (int)(150 * pulse)), IM_COL32(12, 74, 110, 70), IM_COL32(12, 74, 110, 70), IM_COL32(56, 189, 248, (int)(150 * pulse)));
            dl->AddRect(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(56, 189, 248, 255), UI::S(6), 0, UI::S(sel ? 4 : 2.5f));
            ImVec2 buv(1, 1);
            if(void* bt = HUD::Texture("rp_phone.png", &buv))
            {
                float bs = rowH * 1.25f;
                ImVec2 ba(a.x + W - bs - UI::S(10), a.y + rowH * 0.5f - bs * 0.5f);
                dl->PushClipRect(a, ImVec2(a.x + W, a.y + rowH), true);
                dl->AddImage((ImTextureID)bt, ba, ImVec2(ba.x + bs, ba.y + bs), ImVec2(0, 0), buv, IM_COL32(255, 255, 255, 70));
                dl->PopClipRect();
            }
        }
        if(sel) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 30), UI::S(6));
        else if(ImGui::IsItemActive()) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 16), UI::S(6));
        dl->AddLine(ImVec2(a.x, a.y + rowH), ImVec2(a.x + W, a.y + rowH), IM_COL32(255, 255, 255, 14));
        if(tap)
        {
            LRowKey(r, key, sizeof(key));
            if(sel) LJoin(r);                       // tap the selected row again = join / leave
            else { MP_CopyStr(s_szLSelKey, key, sizeof(s_szLSelKey)); s_nLSel = i; }
        }
        float ty = a.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        LPadlock(dl, ImVec2(a.x + cLock + UI::S(18), a.y + rowH * 0.5f - UI::S(4)), UI::S(24), r.locked, r.locked ? IM_COL32(240, 200, 60, 255) : IM_COL32(70, 200, 110, 255));
        ImVec2 uv(1, 1);
        void* rtex = r.kind == 3 ? HUD::Texture("bcw_icon.png", &uv) : (r.kind == 4 ? HUD::Texture("mw3_icon.png", &uv) : (r.kind == 5 ? HUD::Texture("rp_icon.png", &uv) : nullptr));   // 3.04: Brief Case War icon (briefcase)
        if(void* tex = rtex ? rtex : HUD::ServerIcon(r.icon, &uv))
        {
            float is = rowH - UI::S(18);
            ImVec2 ia(a.x + cIcon, a.y + UI::S(9));
            dl->AddImageRounded((ImTextureID)tex, ia, ImVec2(ia.x + is, ia.y + is), ImVec2(0, 0), uv, IM_COL32_WHITE, UI::S(8));
        }
        ImU32 nameCol = r.kind == 3 ? IM_COL32(255, 235, 235, 255) : r.kind == 4 ? IM_COL32(230, 255, 235, 255) : r.kind == 5 ? IM_COL32(224, 242, 254, 255) : ((r.live || sel) ? IM_COL32_WHITE : IM_COL32(175, 175, 175, 255));
        dl->PushClipRect(ImVec2(a.x + cName, a.y), ImVec2(a.x + cPl - UI::S(12), a.y + rowH), true);
        float nx = a.x + cName;
        if(r.on) { dl->AddCircleFilled(ImVec2(nx + UI::S(8), a.y + rowH * 0.5f), UI::S(7), IM_COL32(70, 200, 110, 255)); nx += UI::S(26); }
        char nm[80]; snprintf(nm, sizeof(nm), "%s%s", r.name, r.kind == 0 ? "  (default)" : (r.kind == 3 ? "  (gamemode)" : r.kind == 4 ? "  (24/7)" : r.kind == 5 ? "  (Roleplay 24/7)" : (r.mine ? "  (yours)" : "")));
        dl->AddText(ImVec2(nx, ty), nameCol, nm);
        dl->PopClipRect();
        char b[48];
        ImU32 dim = IM_COL32(175, 175, 175, 255);
        if(r.players >= 0) snprintf(b, sizeof(b), "%d/%d", r.players, r.maxp); else snprintf(b, sizeof(b), "-");
        dl->AddText(ImVec2(a.x + cPl, ty), r.live ? IM_COL32_WHITE : dim, b);
        if(r.ping >= 0) snprintf(b, sizeof(b), "%d", r.ping); else snprintf(b, sizeof(b), "-");
        dl->AddText(ImVec2(a.x + cPing, ty), r.live ? IM_COL32_WHITE : dim, b);
        dl->PushClipRect(ImVec2(a.x + cMode, a.y), ImVec2(a.x + cReg - UI::S(10), a.y + rowH), true);
        {   // Mode = game mode + region ("Freeroam Asia"), Wi-Fi servers say so
            char md[48]; snprintf(md, sizeof(md), "Freeroam %s%s", r.region >= 0 && r.region < REGION_COUNT ? g_MPRegionNames[r.region] : "(all)", (r.kind == 1 && !r.internet) ? " (LAN)" : "");
            if(r.kind == 3) snprintf(md, sizeof(md), "Brief Case War");   // 3.04
            if(r.kind == 4) snprintf(md, sizeof(md), "MW3 Team DM");      // 3.23
            if(r.kind == 5) snprintf(md, sizeof(md), MP_RP_GAMEMODE);     // 3.25: Roleplay RP
            dl->AddText(ImVec2(a.x + cMode, ty), r.live ? IM_COL32_WHITE : dim, md);
        }
        dl->PopClipRect();
        dl->PushClipRect(ImVec2(a.x + cReg, a.y), ImVec2(a.x + W, a.y + rowH), true);
        dl->AddText(ImVec2(a.x + cReg, ty), r.live ? IM_COL32_WHITE : dim, r.lang[0] ? r.lang : "-");
        dl->PopClipRect();
        ImGui::PopID();
    }
    if(rows.empty())
        ImGui::TextDisabled(s_szSearch[0] ? "No server matches \"%s\"." : (s_nLTab == LT_FAVORITES ? "No favourite servers yet. Tap + to add one." : "Searching..."), s_szSearch);
    LDragScroll();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ---- right panel: players + rules ----------------------------------------------------------------
static void LTwoCol(ImDrawList* dl, ImVec2 p, float w, float h, const char* a, const char* b, bool header, float split = 0.42f)
{
    float ty = p.y + (h - ImGui::GetTextLineHeight()) * 0.5f;
    ImU32 c = header ? IM_COL32(205, 205, 205, 255) : IM_COL32_WHITE;
    dl->PushClipRect(p, ImVec2(p.x + w * split - UI::S(6), p.y + h), true);
    dl->AddText(ImVec2(p.x + UI::S(14), ty), c, a);
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(p.x + w * split, p.y), ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImVec2(p.x + w * split, ty), header ? c : IM_COL32(230, 230, 230, 255), b);
    dl->PopClipRect();
    dl->AddLine(ImVec2(p.x, p.y + h), ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, header ? 30 : 14));
}

static void LRightPanel(const LRow* r, float w, float h)
{
    ImGui::BeginChild("##lright", ImVec2(w, h), false, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float rh = UI::S(56);
    float W = ImGui::GetContentRegionAvail().x;
    float half = h * 0.5f;
    // players
    ImGui::BeginChild("##lpl", ImVec2(W, half - UI::S(4)), false);
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, "Player", "Score", true, 0.70f); ImGui::Dummy(ImVec2(W, rh + UI::S(2))); }
    if(r && r->on && IsConnected())
    {
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(i != myId && !players[i].active) continue;
            ImVec2 p = ImGui::GetCursorScreenPos();
            char sc[16]; snprintf(sc, sizeof(sc), "%d", i == myId ? s_nMyScore : players[i].score);
            const char* nm = i == myId ? settings.name : players[i].name;
            LTwoCol(dl, p, W, rh, nm, sc, false, 0.70f);
            ImGui::Dummy(ImVec2(W, rh));
        }
    }
    else if(r && r->mine)
    {
        std::vector<MPServer::PlayerView> pv; s_Server.GetPlayers(pv);
        for(auto& v : pv) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, v.name, "-", false, 0.70f); ImGui::Dummy(ImVec2(W, rh)); }
        if(pv.empty()) ImGui::TextDisabled("  Nobody on your server yet.");
    }
    else ImGui::TextDisabled(r ? "  Join to see who is playing." : "  Select a server.");
    LDragScroll();
    ImGui::EndChild();
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); dl->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + W, p.y), IM_COL32(255, 255, 255, 40), UI::S(2)); ImGui::Dummy(ImVec2(W, UI::S(4))); }
    // rules
    ImGui::BeginChild("##lrules", ImVec2(W, 0), false);
    dl = ImGui::GetWindowDrawList();
    { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, "Rule", "Value", true); ImGui::Dummy(ImVec2(W, rh + UI::S(2))); }
    if(r)
    {
        uint32_t wm = MPServer::WorldMinute();
        char t[32]; snprintf(t, sizeof(t), "%02u:%02u", (unsigned)((wm / 60) % 24), (unsigned)(wm % 60));
        char pl[32]; if(r->players >= 0) snprintf(pl, sizeof(pl), "%d/%d", r->players, r->maxp); else snprintf(pl, sizeof(pl), "-");
        char ic[24]; snprintf(ic, sizeof(ic), "%s", r->icon > 0 ? "custom" : "default");   // 2.32: no icon numbers
        char pt[16]; snprintf(pt, sizeof(pt), "%d", r->port);
        const char* rules[][2] = {
            { "address",   r->kind == 0 ? "default (always on)" : (r->internet ? "internet lobby" : "hidden") },
            { "port",      pt },
            { "status",    r->on ? "connected" : (r->live ? "online" : "always active") },
            { "players",   pl },
            { "password",  r->locked ? "yes" : "no" },
            { "region",    r->region >= 0 && r->region < REGION_COUNT ? g_MPRegionNames[r->region] : "All" },
            { "language",  r->lang[0] ? r->lang : "-" },
            { "mapname",   "San Andreas" },
            { "version",   "MP 3.04" },
            { "worldtime", t },
            { "weather",   "server synced" },
            { "icon",      ic },
        };
        if(r->kind == 5)
        {   // 3.25
            const char* rp[][2] = {
                { "address",   "24/7 featured (always on, VPS + Broker)" }, { "gamemode", "Roleplay RP (only)" }, { "port", pt },
                { "players",   pl }, { "city", "Mega Vil (Los Santos of the future)" }, { "jobs", "20 jobs, payday every 30 min" },
                { "phone",     "calls, SMS, bank, GPS, shop, games..." }, { "rules", "peaceful: only police / military on duty" },
                { "cheats",    "off (MP + CLEO menu)" }, { "login", "required" }, { "version", "MP 3.25" },
            };
            for(auto& kv : rp) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else if(r->kind == 4)
        {   // 3.23
            const char* mw[][2] = {
                { "address",   "24/7 featured (always on, VPS)" }, { "gamemode", "Call Of Duty: Modern Warfare 3" }, { "port", pt },
                { "players",   pl }, { "teams", "Eurasia, Arabia, Soviet, USA, Australia, Brazil, Taliban, France" },
                { "zones",     "34 capture zones + 8 bases" }, { "extras", "6 DM arenas, duels, events, hourly tournament" },
                { "cheats",    "off (MP + CLEO menu)" }, { "login", "required" }, { "version", "MP 3.23" },
            };
            for(auto& kv : mw) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else if(r->kind == 3)
        {   // 3.04
            const char* bcw[][2] = {
                { "address",   "private gamemode (always on)" }, { "gamemode", "Brief Case War" }, { "port", pt },
                { "players",   pl }, { "teams", "Gustav 10 vs Schelapz 10" }, { "starts at", "20 players" },
                { "maps",      "Bone County, San Fierro Bay" }, { "cheats", "off (MP + CLEO menu)" }, { "CheatMenu", "not allowed" },
                { "music",     "war_ambient.mp3 (45%)" }, { "version", "MP 3.04.6" },
            };
            for(auto& kv : bcw) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
        }
        else
        for(auto& kv : rules) { ImVec2 p = ImGui::GetCursorScreenPos(); LTwoCol(dl, p, W, rh, kv[0], kv[1], false); ImGui::Dummy(ImVec2(W, rh)); }
    }
    LDragScroll();
    ImGui::EndChild();
    ImGui::EndChild();
}

// ---- admin pages: players (kick / ban) + ban list -------------------------------------------------
// 2.37: [ADMIN] SENJI manages the default server from his phone (the server checks the admin key)
static void LPagePlayersAdmin()
{
    ImGui::Text(MP::IsLocalMod() ? "This server   |   Moderator: " : "Default server   |   Administrator: ");
    ImGui::SameLine(0, 0);
    { ImVec2 p = ImGui::GetCursorScreenPos(); const char* me = players[myId].name; UI::NameText(ImGui::GetWindowDrawList(), ImGui::GetFontSize(), p, IM_COL32_WHITE, me, 1.0f, false);
      ImGui::Dummy(ImVec2(ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, 0, me).x, ImGui::GetTextLineHeight())); }
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.48f;
    ImGui::BeginChild("##mga", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    int n = 0; bool selOk = false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active || i == myId) continue;
        ++n;
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##p", ImVec2(W, rh))) s_nMgmtSel = (s_nMgmtSel == i) ? -1 : i;
        bool sel = s_nMgmtSel == i;
        if(sel) { dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(190, 25, 25, 200), UI::S(6)); selOk = true; }
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char b[24]; snprintf(b, sizeof(b), "%d", i);
        dl->AddText(ImVec2(a.x + UI::S(14), ty), IM_COL32_WHITE, b);
        UI::NameText(dl, ImGui::GetFontSize(), ImVec2(a.x + W * 0.1f, ty), sel ? IM_COL32_WHITE : UI::FromRGBA(p.color | 0xFF), p.name, 1.0f, false);
        snprintf(b, sizeof(b), p.ping >= 0 ? "%d ms" : "-", p.ping);
        dl->AddText(ImVec2(a.x + W * 0.8f, ty), IM_COL32(200, 200, 200, 255), b);
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("  Nobody else on the default server right now.");
    LDragScroll();
    ImGui::EndChild();
    if(!selOk) s_nMgmtSel = -1;
    ImGui::Separator();
    FloatMenu::TextField("Reason", s_szKickReason, "Reason (kick / ban)", MP_REASON_LEN - 1, OnKickReason);
    static const char* quick[] = { "Cheating", "Spamming", "Insulting", "Griefing", "Bug abuse", "AFK" };
    for(int i = 0; i < 6; ++i)
    {
        if(i) ImGui::SameLine();
        if(ImGui::Button(quick[i], ImVec2(0, UI::S(54)))) MP_CopyStr(s_szKickReason, quick[i], sizeof(s_szKickReason));
    }
    int t = s_nMgmtSel;
    bool can = t >= 0 && t < MP_MAX_PLAYERS && players[t].active;
    float q = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    if(!can) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.10f, 0.95f));
    if(ImGui::Button("Kick##ak", ImVec2(q, UI::S(72))) && can) { SendAdminCmd(ADMIN_KICK, t, 0, s_szKickReason); s_nMgmtSel = -1; }
    ImGui::PopStyleColor(); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.08f, 0.08f, 0.95f));
    if(ImGui::Button("Ban##ab", ImVec2(q, UI::S(72))) && can) { SendAdminCmd(ADMIN_BAN, t, 0, s_szKickReason); s_nMgmtSel = -1; }
    ImGui::PopStyleColor(); ImGui::SameLine();
    if(ImGui::Button("Mute##am", ImVec2(q * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, UI::S(72))) && can) SendAdminCmd(ADMIN_MUTE, t, 0, "");
    ImGui::SameLine();
    if(ImGui::Button("Unmute##au", ImVec2(q * 0.5f - ImGui::GetStyle().ItemSpacing.x * 0.5f, UI::S(72))) && can) SendAdminCmd(ADMIN_UNMUTE, t, 0, "");
    if(!can) ImGui::EndDisabled();
    ImGui::TextDisabled("Tap a player, pick a reason, then Kick / Ban / Mute. The 24/7 server keeps its ban list.");
}

static void LPagePlayers()
{
    if(MP::IsDefaultAdmin() || MP::IsLocalMod()) { LPagePlayersAdmin(); return; }   // 3.35: [MOD] kicks / mutes too   // 2.37
    if(MP::IsHostingDefault())
    {
        ImGui::TextUnformatted("Player management");
        ImGui::TextDisabled("The default server has one administrator: [ADMIN] SENJI. Joining first (or hosting it) doesn't make you the host.");
        ImGui::TextDisabled("Want your own rules? Add a server with your own name (Add server -> Host this server).");
        if(FloatMenu::BigButton("Go to Add server")) s_nLTab = LT_ADD;
        return;
    }
    if(!s_Server.IsRunning())
    {
        ImGui::TextUnformatted("Player management");
        ImGui::TextDisabled("Only the administrator can kick and ban: the phone that hosts the server.");
        ImGui::TextDisabled("Host a server first (Add server -> Host this server), then your players show up here.");
        if(FloatMenu::BigButton("Go to Add server")) s_nLTab = LT_ADD;
        return;
    }
    std::vector<MPServer::PlayerView> pv; s_Server.GetPlayers(pv);
    ImGui::Text("Server: %s   |   Administrator: %s   |   %d/%d players", s_Server.GetName(), settings.name, (int)pv.size(), s_Server.GetMaxPlayers());
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.48f;
    ImGui::BeginChild("##mgl", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    {
        ImVec2 p = ImGui::GetCursorScreenPos(); ImU32 tc = IM_COL32(205, 205, 205, 255);
        float ty = p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x + UI::S(14), ty), tc, "id"); dl->AddText(ImVec2(p.x + W * 0.1f, ty), tc, "Player");
        dl->AddText(ImVec2(p.x + W * 0.52f, ty), tc, "Phone id"); dl->AddText(ImVec2(p.x + W * 0.8f, ty), tc, "Online");
        dl->AddLine(ImVec2(p.x, p.y + rh), ImVec2(p.x + W, p.y + rh), IM_COL32(255, 255, 255, 30), UI::S(2));
        ImGui::Dummy(ImVec2(W, rh + UI::S(2)));
    }
    bool selOk = false;
    const MPServer::PlayerView* selP = nullptr;
    for(auto& v : pv)
    {
        ImGui::PushID(v.id);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##p", ImVec2(W, rh))) s_nMgmtSel = (s_nMgmtSel == v.id) ? -1 : v.id;
        bool sel = s_nMgmtSel == v.id;
        if(sel) { dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(190, 25, 25, 200), UI::S(6)); selOk = true; selP = &v; }
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char b[64]; snprintf(b, sizeof(b), "%d", v.id);
        dl->AddText(ImVec2(a.x + UI::S(14), ty), IM_COL32_WHITE, b);
        bool me = v.uid[0] && !strcmp(v.uid, settings.deviceId);
        snprintf(b, sizeof(b), "%s%s", v.name, me ? "  (you, administrator)" : "");
        dl->AddText(ImVec2(a.x + W * 0.1f, ty), sel ? IM_COL32_WHITE : UI::FromRGBA(v.color | 0xFF), b);
        dl->AddText(ImVec2(a.x + W * 0.52f, ty), IM_COL32(200, 200, 200, 255), v.uid[0] ? v.uid : "old version");
        uint64_t mins = (MP_NowMs() - v.joinedMs) / 60000;
        snprintf(b, sizeof(b), "%llu min", (unsigned long long)mins);
        dl->AddText(ImVec2(a.x + W * 0.8f, ty), IM_COL32(200, 200, 200, 255), b);
        dl->AddLine(ImVec2(a.x, a.y + rh), ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 14));
        ImGui::PopID();
    }
    if(pv.empty()) ImGui::TextDisabled("  Nobody on your server yet.");
    LDragScroll();
    ImGui::EndChild();
    if(!selOk) s_nMgmtSel = -1;
    ImGui::Separator();
    FloatMenu::TextField("Reason", s_szKickReason, "Reason (kick / ban)", MP_REASON_LEN - 1, OnKickReason);
    static const char* quick[] = { "Cheating", "Spamming", "Insulting", "Griefing", "Bug abuse", "AFK" };
    for(int i = 0; i < 6; ++i)
    {
        if(i) ImGui::SameLine();
        if(ImGui::Button(quick[i], ImVec2(0, UI::S(54)))) MP_CopyStr(s_szKickReason, quick[i], sizeof(s_szKickReason));
    }
    bool me = selP && selP->uid[0] && !strcmp(selP->uid, settings.deviceId);
    bool can = selP && !me;
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if(!can) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.10f, 0.95f));
    char lbl[96]; snprintf(lbl, sizeof(lbl), "Kick %s", selP ? selP->name : "");
    if(ImGui::Button(lbl, ImVec2(half, UI::S(72))) && selP)
    {
        int id = selP->id; char nm[MP_NAME_LEN]; MP_CopyStr(nm, selP->name, sizeof(nm));
        if(s_Server.Kick(id, settings.name, s_szKickReason, false)) SysMsg(COL_YELLOW, "Kicked %s.", nm);
        s_nMgmtSel = -1;
    }
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.65f, 0.08f, 0.08f, 0.95f));
    snprintf(lbl, sizeof(lbl), "Ban %s", selP ? selP->name : "");
    if(ImGui::Button(lbl, ImVec2(half, UI::S(72))) && selP)
    {
        int id = selP->id; char nm[MP_NAME_LEN]; MP_CopyStr(nm, selP->name, sizeof(nm));
        if(s_Server.Kick(id, settings.name, s_szKickReason, true)) SysMsg(COL_RED, "Banned %s.", nm);
        s_nMgmtSel = -1;
    }
    ImGui::PopStyleColor();
    if(!can) ImGui::EndDisabled();
    if(me) ImGui::TextDisabled("That is you (the administrator).");
    else ImGui::TextDisabled("Tap a player, type or pick a reason, then Kick or Ban. Banned players see the date, you as administrator and the reason.");
}

static void LPageBans()
{
    std::vector<MPServer::BanEntry> bans; s_Server.GetBans(bans);
    ImGui::Text("Ban list  (%d)   |   Administrator: %s", (int)bans.size(), settings.name);
    ImGui::TextDisabled("Saved in files/MP/bans.txt, used by every server this phone hosts.");
    ImGui::Separator();
    float listH = ImGui::GetContentRegionAvail().y * 0.55f;
    ImGui::BeginChild("##bl", ImVec2(0, listH), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float W = ImGui::GetContentRegionAvail().x, rh = UI::S(60);
    {
        ImVec2 p = ImGui::GetCursorScreenPos(); ImU32 tc = IM_COL32(205, 205, 205, 255);
        float ty = p.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(p.x + UI::S(14), ty), tc, "Player"); dl->AddText(ImVec2(p.x + W * 0.28f, ty), tc, "Ban date");
        dl->AddText(ImVec2(p.x + W * 0.5f, ty), tc, "Administrator"); dl->AddText(ImVec2(p.x + W * 0.7f, ty), tc, "Reason");
        dl->AddLine(ImVec2(p.x, p.y + rh), ImVec2(p.x + W, p.y + rh), IM_COL32(255, 255, 255, 30), UI::S(2));
        ImGui::Dummy(ImVec2(W, rh + UI::S(2)));
    }
    if(s_nBanSel >= (int)bans.size()) s_nBanSel = -1;
    for(int i = 0; i < (int)bans.size(); ++i)
    {
        const MPServer::BanEntry& b = bans[i];
        ImGui::PushID(i);
        ImVec2 a = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##b", ImVec2(W, rh))) s_nBanSel = (s_nBanSel == i) ? -1 : i;
        if(s_nBanSel == i) dl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 30), UI::S(6));
        float ty = a.y + (rh - ImGui::GetTextLineHeight()) * 0.5f;
        char d[32]; time_t tt = (time_t)b.when; struct tm lt; localtime_r(&tt, &lt); strftime(d, sizeof(d), "%Y-%m-%d %H:%M", &lt);
        auto col = [&](float x0, float x1, const char* s, ImU32 c) { dl->PushClipRect(ImVec2(a.x + x0, a.y), ImVec2(a.x + x1, a.y + rh), true); dl->AddText(ImVec2(a.x + x0, ty), c, s); dl->PopClipRect(); };
        col(UI::S(14), W * 0.27f, b.name[0] ? b.name : b.uid, IM_COL32_WHITE);
        col(W * 0.28f, W * 0.49f, d, IM_COL32(210, 210, 210, 255));
        col(W * 0.5f, W * 0.69f, b.admin, IM_COL32(210, 210, 210, 255));
        col(W * 0.7f, W, b.reason[0] ? b.reason : "-", IM_COL32(210, 210, 210, 255));
        dl->AddLine(ImVec2(a.x, a.y + rh), ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 14));
        ImGui::PopID();
    }
    if(bans.empty()) ImGui::TextDisabled("  Nobody is banned.");
    LDragScroll();
    ImGui::EndChild();
    if(s_nBanSel < 0) ImGui::BeginDisabled();
    if(FloatMenu::BigButton("Unban selected")) { s_Server.Unban(s_nBanSel); s_nBanSel = -1; }
    if(s_nBanSel < 0) ImGui::EndDisabled();
    ImGui::Separator();
    ImGui::TextUnformatted("Ban a name (also when that player is offline)");
    FloatMenu::TextField("Player name", s_szBanName, "Player name", MP_NAME_LEN - 1, OnBanName);
    FloatMenu::TextField("Reason##bn", s_szKickReason, "Reason", MP_REASON_LEN - 1, OnKickReason);
    if(!s_szBanName[0]) ImGui::BeginDisabled();
    if(FloatMenu::BigButton("Ban name", IM_COL32(150, 25, 25, 240)))
    {
        if(s_Server.AddBan(s_szBanName, "", settings.name, s_szKickReason)) { SysMsg(COL_RED, "Banned the name %s.", s_szBanName); s_szBanName[0] = 0; }
        else SysMsg(COL_YELLOW, "%s is already banned.", s_szBanName);
    }
    if(!s_szBanName[0]) ImGui::EndDisabled();
}

// ---- the window -----------------------------------------------------------------------------------
static int LSectionIcon(const char* t)
{
    if(!strcasecmp(t, "Connection")) return IC_GLOBE;
    if(!strcasecmp(t, "Display"))    return IC_SLIDERS;
    if(!strcasecmp(t, "Chat"))       return IC_CHAT;
    if(!strcasecmp(t, "Player"))     return IC_USER;
    if(!strcasecmp(t, "Cheats"))     return IC_BAN;
    if(!strcasecmp(t, "Sound"))      return IC_PLAY;   // 3.00
    return IC_LIST;
}

static bool LSideItem(const char* id, int icon, const char* label, bool on, float w, float h)
{
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool hit = ImGui::InvisibleButton("##si", ImVec2(w, h));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if(on) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 34), UI::S(10));
    else if(ImGui::IsItemActive()) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, 18), UI::S(10));
    ImU32 col = on ? IM_COL32_WHITE : IM_COL32(215, 215, 215, 255);
    LIcon(dl, icon, ImVec2(p.x + UI::S(36), p.y + h * 0.5f), h * 0.24f, col);
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    dl->AddText(ImVec2(p.x + UI::S(74), p.y + (h - ImGui::GetTextLineHeight()) * 0.5f), col, label);
    dl->PopClipRect();
    ImGui::PopID();
    return hit;
}

static void LGroupLabel(const char* t)
{
    ImGui::Dummy(ImVec2(1, UI::S(6)));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.55f, 0.55f, 1));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + UI::S(14));
    ImGui::TextUnformatted(t);
    ImGui::PopStyleColor();
}

static void RenderLauncher(void* openPtr)
{
    bool* open = (bool*)openPtr;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float mx = ds.x * 0.025f, my = ds.y * 0.03f;
    // 2.26: dim the game behind the launcher (touches never reach it while this is open)
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 110));
    ImGui::SetNextWindowPos(ImVec2(mx, my));
    ImGui::SetNextWindowSize(ImVec2(ds.x - mx * 2, ds.y - my * 2));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(16));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(2));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.03f, 0.03f, 0.035f, 0.60f));   // 40% transparent
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 0.10f));
    if(!ImGui::Begin("##MPLauncher", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse))
    { ImGui::End(); ImGui::PopStyleColor(3); ImGui::PopStyleVar(3); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    const ImU32 line = IM_COL32(255, 255, 255, 26);
    const float pad = UI::S(16);

    // ---- title bar ----
    const float titleH = UI::S(70);
    {
        // 3.24: profile at the top left: your Discord picture + account (blank default when no account)
        ImVec2 c(wp.x + pad + UI::S(26), wp.y + titleH * 0.5f);
        const float ar = UI::S(25);
        ProfileDrawAvatar(dl, c, ar);
        const std::string pu = ProfileUser();
        const bool online = s_Auth.LoggedIn() && !s_AuthUser.empty();
        const char* l1 = pu.empty() ? "Guest" : pu.c_str();
        char l2[96];
        if(pu.empty()) snprintf(l2, sizeof(l2), "Not logged in");
        else if(!s_ProfDiscord.empty() && !strcasecmp(pu.c_str(), s_ProfOwner.c_str())) snprintf(l2, sizeof(l2), "@%s%s", s_ProfDiscord.c_str(), online ? "" : "  (offline)");
        else snprintf(l2, sizeof(l2), online ? "Logged in" : "Log in on a server");
        const float lh = ImGui::GetTextLineHeight();
        float px = c.x + ar + UI::S(12);
        float sc = 0.78f;
        ImFont* fnt = ImGui::GetFont();
        dl->AddText(ImVec2(px, c.y - lh * 0.5f - lh * sc * 0.5f), IM_COL32_WHITE, l1);
        dl->AddText(fnt, ImGui::GetFontSize() * sc, ImVec2(px, c.y + lh * 0.5f - lh * sc * 0.45f), online ? IM_COL32(95, 211, 95, 255) : IM_COL32(150, 150, 150, 255), l2);
        float pw = std::max(ImGui::CalcTextSize(l1).x, ImGui::CalcTextSize(l2).x * sc);
        // a status dot on the picture: green = logged in this game session
        dl->AddCircleFilled(ImVec2(c.x + ar * 0.72f, c.y + ar * 0.72f), UI::S(6), online ? IM_COL32(67, 181, 129, 255) : IM_COL32(116, 127, 141, 255), 16);
        dl->AddCircle(ImVec2(c.x + ar * 0.72f, c.y + ar * 0.72f), UI::S(6), IM_COL32(15, 15, 18, 255), 16, UI::S(2));
        float sepX = px + pw + UI::S(20);
        dl->AddLine(ImVec2(sepX, wp.y + titleH * 0.22f), ImVec2(sepX, wp.y + titleH * 0.78f), line, UI::S(2));
        float tx = sepX + UI::S(20), ty = wp.y + (titleH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(tx, ty), IM_COL32_WHITE, "MP Launcher");
        dl->AddText(ImVec2(tx + ImGui::CalcTextSize("MP Launcher").x + UI::S(16), ty), IM_COL32(150, 150, 150, 255), "v" MP_VERSION_STR);   // 3.35: always the real version
        // 2.30: watermark, right side of the title bar (left of [X]), green at 50% transparency
        {
            const char* wm = "powered by HiveMQ/ThirtyFoxMC";
            ImVec2 wsz = ImGui::CalcTextSize(wm);
            float wx = wp.x + ws.x - titleH - UI::S(6) - UI::S(18) - wsz.x;
            float minX = tx + ImGui::CalcTextSize("MP Launcher  v" MP_VERSION_STR).x + UI::S(40);   // never over the name
            if(wx > minX) dl->AddText(ImVec2(wx, ty), IM_COL32(95, 211, 95, 128), wm);
        }
        ImGui::SetCursorScreenPos(ImVec2(wp.x + ws.x - titleH - UI::S(6), wp.y));
        if(LToolButton("close", IC_CLOSE, nullptr, titleH)) { *open = false; s_bRegionPick = false; }
        dl->AddLine(ImVec2(wp.x, wp.y + titleH), ImVec2(wp.x + ws.x, wp.y + titleH), line, UI::S(2));
    }

    // ---- data ----
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    std::vector<LanServer> nearby;
    { std::lock_guard<std::mutex> lk(s_LanMutex); nearby = s_Nearby; }
    if(s_bHasNetwork && !IsSearching() && MP_NowMs() - s_nNearbyTime > 15000) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
    bool listTab = s_nLTab == LT_INTERNET || s_nLTab == LT_FAVORITES;
    std::vector<LRow> rows;
    if(listTab) LBuildRows(s_nLTab, nearby, rows);
    // keep the selection valid for the toolbar (LTable re-resolves it by key)
    s_nLSel = -1;
    { char key[48]; for(int i = 0; i < (int)rows.size(); ++i) { LRowKey(rows[i], key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) s_nLSel = i; } }
    const LRow* sel = (s_nLSel >= 0 && s_nLSel < (int)rows.size()) ? &rows[s_nLSel] : nullptr;

    // ---- toolbar ----
    const float tbH = UI::S(96), btnH = UI::S(72);
    {
        float y = wp.y + titleH + (tbH - btnH) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(wp.x + pad, y));
        float sideW = ImMax(UI::S(300), ws.x * 0.17f);
        if(LInputBox("search", s_szSearch, "Search server", sideW - pad, btnH, IC_SEARCH)) VKeyboard::Open("Search server", s_szSearch, 31, OnSearch);
        ImGui::SameLine(0, UI::S(18));
        bool onSel = sel ? sel->on : IsOnDefault();
        const char* cl = onSel ? (client.GetState() == NET_CONNECTING ? "Cancel" : "Leave") : (IsBusy() ? "Searching..." : "Connect");
        if(LToolButton("connect", onSel ? IC_STOP : IC_PLAY, cl, btnH, s_bHasNetwork || onSel))
        {
            if(sel) { if(sel->on) LLeave(*sel); else LJoin(*sel); }   // 3.10: Leave really leaves, Connect never reconnects
            else { if(IsOnDefault()) MP::ToggleConnect(); else { LeaveServer(nullptr); MP::ToggleConnect(); } }
        }
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("add", IC_PLUS, nullptr, btnH, true, s_nLTab == LT_ADD)) s_nLTab = LT_ADD;
        ImGui::SameLine(0, UI::S(6));
        bool canDel = sel && sel->kind == 2;
        if(LToolButton("del", IC_TRASH, nullptr, btnH, canDel) && canDel) { ServerList::Remove(sel->saved); s_szLSelKey[0] = 0; sel = nullptr; }
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("refresh", IC_REFRESH, nullptr, btnH)) { ServerList::Refresh(); if(s_bHasNetwork) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); } }
        ImGui::SameLine(0, UI::S(6));
        int connTab = -1;
        for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Connection")) connTab = LT_SECTIONS + i;
        if(LToolButton("gear", IC_GEAR, nullptr, btnH, connTab >= 0, s_nLTab == connTab) && connTab >= 0) s_nLTab = connTab;
        ImGui::SameLine(0, UI::S(6));
        s_vRegionBtn = ImGui::GetCursorScreenPos(); s_fRegionBtn = btnH;   // 3.25.1: region switch icon, right after the gear
        if(LToolButton("region", IC_REGION, nullptr, btnH, true, s_bRegionPick)) s_bRegionPick = !s_bRegionPick;

        // right side: nickname, display settings, help
        float nickW = ImMax(UI::S(280), ws.x * 0.15f);
        float rx = wp.x + ws.x - pad - btnH * 2 - UI::S(12) - nickW;
        ImGui::SetCursorScreenPos(ImVec2(rx, y));
        bool idle = client.GetState() == NET_DISCONNECTED;
        if(LInputBox("nick", settings.name, "Nickname", nickW, btnH) && idle) VKeyboard::Open("Nickname", settings.name, MP_NAME_LEN - 1, OnName);
        ImGui::SameLine(0, UI::S(6));
        int dispTab = -1;
        for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Display")) dispTab = LT_SECTIONS + i;
        if(LToolButton("disp", IC_SLIDERS, nullptr, btnH, dispTab >= 0, s_nLTab == dispTab) && dispTab >= 0) s_nLTab = dispTab;
        ImGui::SameLine(0, UI::S(6));
        if(LToolButton("help", IC_INFO, nullptr, btnH)) FloatMenu::OpenPage(FloatMenu::PAGE_HELP);
        dl->AddLine(ImVec2(wp.x, wp.y + titleH + tbH), ImVec2(wp.x + ws.x, wp.y + titleH + tbH), line, UI::S(2));
    }

    // ---- body ----
    float bodyY = wp.y + titleH + tbH + UI::S(2);
    float bodyH = wp.y + ws.y - bodyY - UI::S(6);
    float sideW = ImMax(UI::S(300), ws.x * 0.17f);
    ImGui::SetCursorScreenPos(ImVec2(wp.x + UI::S(10), bodyY + UI::S(8)));
    ImGui::BeginChild("##lside", ImVec2(sideW, bodyH - UI::S(10)), false);
    {
        float iw = ImGui::GetContentRegionAvail().x, ih = UI::S(70);
        LGroupLabel("SERVERS");
        if(LSideItem("inet", IC_GLOBE, "Internet",  s_nLTab == LT_INTERNET,  iw, ih)) { s_nLTab = LT_INTERNET; }
        if(LSideItem("fav",  IC_HEART, "Favorites", s_nLTab == LT_FAVORITES, iw, ih)) { s_nLTab = LT_FAVORITES; }
        if(LSideItem("add",  IC_PLUS,  "Add server",s_nLTab == LT_ADD,       iw, ih)) { s_nLTab = LT_ADD; }
        LGroupLabel("ADMIN");
        if(LSideItem("pl",   IC_USERS, "Players",   s_nLTab == LT_PLAYERS,   iw, ih)) { s_nLTab = LT_PLAYERS; }
        if(LSideItem("bans", IC_BAN,   "Bans",      s_nLTab == LT_BANS,      iw, ih)) { s_nLTab = LT_BANS; }
        LGroupLabel("SETTINGS");
        for(int i = 0; i < FloatMenu::SectionCount(); ++i)
        {
            const char* t = FloatMenu::SectionTitle(i);
            if(!strcasecmp(t, "Servers")) continue;   // shown as Internet / Favorites / Add server
            char id[16]; snprintf(id, sizeof(id), "sec%d", i);
            if(LSideItem(id, LSectionIcon(t), t, s_nLTab == LT_SECTIONS + i, iw, ih)) s_nLTab = LT_SECTIONS + i;
        }
        LDragScroll();
    }
    ImGui::EndChild();
    float cx0 = wp.x + UI::S(10) + sideW + UI::S(8);
    dl->AddLine(ImVec2(cx0, bodyY), ImVec2(cx0, wp.y + ws.y), line, UI::S(2));
    float contentW = wp.x + ws.x - cx0 - UI::S(4);

    if(listTab)
    {
        float rightW = ImMax(UI::S(330), ws.x * 0.22f);
        float tableW = contentW - rightW - UI::S(6);
        ImGui::SetCursorScreenPos(ImVec2(cx0 + UI::S(4), bodyY + UI::S(6)));
        // 3.25.1: the region switch is an icon in the toolbar now, the table gets the whole height again
        ImGui::SetCursorScreenPos(ImVec2(cx0 + UI::S(4), bodyY));
        LTable(rows, tableW, bodyH);
        float rx0 = cx0 + UI::S(4) + tableW + UI::S(2);
        dl->AddLine(ImVec2(rx0, bodyY), ImVec2(rx0, wp.y + ws.y), line, UI::S(2));
        ImGui::SetCursorScreenPos(ImVec2(rx0 + UI::S(4), bodyY));
        // the table may have changed the selection
        sel = nullptr;
        { char key[48]; for(auto& r : rows) { LRowKey(r, key, sizeof(key)); if(s_szLSelKey[0] && !strcmp(key, s_szLSelKey)) { sel = &r; break; } } }
        LRightPanel(sel, rightW - UI::S(6), bodyH);
    }
    else
    {
        ImGui::SetCursorScreenPos(ImVec2(cx0 + pad, bodyY + pad));
        ImGui::BeginChild("##lpage", ImVec2(contentW - pad * 2, bodyH - pad), false);
        switch(s_nLTab)
        {
        case LT_HOSTED:  // 2.37: merged into Add server
        case LT_ADD:     SectionAddServer(nullptr); break;
        case LT_PLAYERS: LPagePlayers(); break;
        case LT_BANS:    LPageBans(); break;
        default:
            if(s_nLTab >= LT_SECTIONS && s_nLTab - LT_SECTIONS < FloatMenu::SectionCount()) FloatMenu::DrawSection(s_nLTab - LT_SECTIONS);
            else s_nLTab = LT_INTERNET;
        }
        LDragScroll();
        ImGui::EndChild();
    }
    // the Internet tab also shows the lobby state under the list title area when it is offline
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    LRegionPicker(nearby);   // 3.25.1: the region list opened by the toolbar icon (its own window, on top of the launcher)
}

// ---- kick / ban dialog (SA-MP style) -----------------------------------------------------------
static void RenderKickDialog(void*)
{
    if(!s_Kick.show) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ImMin(ds.x * 0.62f, UI::S(1100));
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(28), UI::S(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.025f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.15f, 0.15f, 0.65f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPKicked", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fs = UI::S(40);
        const char* title = s_Kick.banned ? "You are banned from the server!" : "You were kicked from the server!";
        dl->AddText(UI::Font(), fs, p, IM_COL32(255, 70, 70, 255), title);
        ImGui::Dummy(ImVec2(w - UI::S(56), fs + UI::S(10)));
        ImGui::Separator();
        char d[40]; time_t tt = (time_t)s_Kick.when; struct tm lt; localtime_r(&tt, &lt); strftime(d, sizeof(d), "%Y-%m-%d  %H:%M", &lt);
        auto rowf = [](const char* k, const char* v)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, 1)); ImGui::TextUnformatted(k); ImGui::PopStyleColor();
            ImGui::SameLine(UI::S(300)); ImGui::TextWrapped("%s", v);
        };
        rowf("Server:", s_Kick.server);
        rowf(s_Kick.banned ? "Ban date:" : "Kick date:", d);
        rowf("Administrator:", s_Kick.admin[0] ? s_Kick.admin : "Server");
        rowf("Reason:", s_Kick.reason[0] ? s_Kick.reason : "No reason given");
        ImGui::Separator();
        ImGui::TextDisabled(s_Kick.banned ? "If you think this is a mistake, ask the administrator (the host) to unban you."
                                          : "You can join again. Follow the server rules.");
        ImGui::Spacing();
        float bw = UI::S(260);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
        if(ImGui::Button("OK", ImVec2(bw, UI::S(72)))) s_Kick.show = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}

// 2.37: "This server doesn't allow cheat menus" - shown when CheatMenu is installed and you try to join / host
static void RenderCheatGateDialog(void*)
{
    if(!s_CheatGate.show) return;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    float w = ImMin(ds.x * 0.62f, UI::S(1100));
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(28), UI::S(24)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(14));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.025f, 0.92f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.95f, 0.55f, 0.10f, 0.70f));
    ImGui::SetNextWindowFocus();
    if(ImGui::Begin("##MPCheatGate", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fs = UI::S(40);
        dl->AddText(UI::Font(), fs, p, IM_COL32(255, 150, 40, 255), "This server doesn't allow cheat menus");
        ImGui::Dummy(ImVec2(w - UI::S(56), fs + UI::S(10)));
        ImGui::Separator();
        ImGui::TextWrapped("%s is loaded in your game right now. To keep it fair for everyone, "
                           "cheat menus aren't allowed on %s.", ModScan::What(), s_CheatGate.server);
        ImGui::Spacing();
        ImGui::TextWrapped("Cheat Menu works only on custom servers whose host turned on \"Cheat Menu\" (Add server > permissions). "
                           "For the default server, remove it from your AML mods folder and restart the game - once it is not loaded any more you can join right away.");
        ImGui::Spacing();
        ImGui::TextDisabled("We check this every time you join, so it can't be switched off while playing.");
        ImGui::Spacing();
        float bw = UI::S(260);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - bw) * 0.5f);
        if(ImGui::Button("Got it", ImVec2(bw, UI::S(72)))) s_CheatGate.show = false;
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
}
