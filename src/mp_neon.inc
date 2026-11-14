// MP 4.13.22: the "neon" main menu + server browser (Screenshots/neon-main-menu.jpg, neon-server-browser.jpg).
//
// ServerBrowserMode= in files/TYFMC/TYFMC.MP.ini ([MP]):
//   neon     (default) the neon title menu: Resume / New game / Browse servers / Quick connect / Settings / About /
//            Quit game, the Neon Identity card (Discord), NEON FEATURED card, the version label; Browse servers is
//            the neon server browser (its own layout - not the launcher panel). Also shown on the black screen
//            after leaving a server.
//   mp       the old [Multiplayer] tile on the game's menu + the MP launcher panel (4.13.4 - 4.13.21)
//   default  the game's own main menu only (no Multiplayer tile, = MainMenuMultiplayer = 0)
// In the game (floating 3-dots menu > Server) the launcher panel stays as it was.
// Background: neon_bg.jpg (the San Andreas fan art), logo: title.png. Fonts: imgui/neonfonts.h (OFL).

enum { NP_MAIN = 0, NP_BROWSER, NP_SETTINGS, NP_ABOUT, NP_QUIT };
static int  s_nNeonPage = NP_MAIN, s_nNeonSel = 2;   // Browse servers
static bool s_bNeonHidden = false;                   // "Game settings": the game's own menu for now
static LRow s_NeonPending;
static char s_szNeonSearch[64] = "";
static int  s_nNeonList = 0;                         // 0 all, 1 featured, 2 favourites, 3 Wi-Fi
static bool s_bNeonListOpen = false, s_bNeonFilterOpen = false;
static bool s_bNeonHideFull = false, s_bNeonHideEmpty = false, s_bNeonHideLocked = false;
static char s_szNeonSelKey[64] = "";
static int  s_nNeonSetSec = -1;
static uint64_t s_nNeonCopiedAt = 0, s_nNeonTapAt = 0;

bool MP::NeonMenuMode() { return NeonOn(); }

// ---- small drawing helpers ------------------------------------------------------------------
static const ImU32 kNGold = IM_COL32(201, 169, 92, 255), kNGoldDim = IM_COL32(150, 126, 70, 255);
static const ImU32 kNGrey = IM_COL32(141, 143, 149, 255), kNText = IM_COL32(226, 228, 232, 255);
static const ImU32 kNPanel = IM_COL32(14, 15, 18, 236), kNLine = IM_COL32(38, 40, 45, 255);
static const ImU32 kNBlue = IM_COL32(159, 194, 255, 255);
// letter-spaced text (the tracked small caps labels of the reference)
static float NeonTextW(ImFont* f, float fs, const char* t, float sp)
{
    float w = 0; const char* p = t;
    while(*p) { unsigned int c = 0; int k = ImTextCharFromUtf8(&c, p, nullptr); if(k <= 0) break;
                w += f->CalcTextSizeA(fs, FLT_MAX, 0, p, p + k).x + sp; p += k; }
    return w > 0 ? w - sp : 0;
}
static void NeonText(ImDrawList* dl, ImFont* f, float fs, ImVec2 at, ImU32 col, const char* t, float sp = 0, bool shadow = false)
{
    if(!t || !*t) return;
    if(sp <= 0.01f)
    {
        if(shadow) dl->AddText(f, fs, ImVec2(at.x + fs * 0.04f, at.y + fs * 0.06f), IM_COL32(0, 0, 0, 200), t);
        dl->AddText(f, fs, at, col, t); return;
    }
    const char* p = t; float x = at.x;
    while(*p)
    {
        unsigned int c = 0; int k = ImTextCharFromUtf8(&c, p, nullptr); if(k <= 0) break;
        if(shadow) dl->AddText(f, fs, ImVec2(x + fs * 0.04f, at.y + fs * 0.06f), IM_COL32(0, 0, 0, 200), p, p + k);
        dl->AddText(f, fs, ImVec2(x, at.y), col, p, p + k);
        x += f->CalcTextSizeA(fs, FLT_MAX, 0, p, p + k).x + sp; p += k;
    }
}
static void NeonTextR(ImDrawList* dl, ImFont* f, float fs, ImVec2 rightTop, ImU32 col, const char* t, float sp = 0)
{ NeonText(dl, f, fs, ImVec2(rightTop.x - NeonTextW(f, fs, t, sp), rightTop.y), col, t, sp); }
static void NeonUpper(char* o, size_t n, const char* s) { size_t i = 0; for(; s && s[i] && i + 1 < n; ++i) o[i] = (char)toupper((unsigned char)s[i]); o[i] = 0; }
static void NeonTri(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int dir = 0)   // 0 right, 1 down, 2 up, 3 left
{
    if(dir == 0) dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x - r * 0.6f, c.y + r), ImVec2(c.x + r * 0.9f, c.y), col);
    else if(dir == 1) dl->AddTriangleFilled(ImVec2(c.x - r, c.y - r * 0.6f), ImVec2(c.x + r, c.y - r * 0.6f), ImVec2(c.x, c.y + r * 0.9f), col);
    else if(dir == 2) dl->AddTriangleFilled(ImVec2(c.x - r, c.y + r * 0.6f), ImVec2(c.x + r, c.y + r * 0.6f), ImVec2(c.x, c.y - r * 0.9f), col);
    else dl->AddTriangleFilled(ImVec2(c.x + r * 0.6f, c.y - r), ImVec2(c.x + r * 0.6f, c.y + r), ImVec2(c.x - r * 0.9f, c.y), col);
}
static void NeonChevron(ImDrawList* dl, ImVec2 c, float r, ImU32 col, float th, bool down = false)
{
    if(down) { ImVec2 p[3] = { ImVec2(c.x - r, c.y - r * 0.5f), ImVec2(c.x, c.y + r * 0.5f), ImVec2(c.x + r, c.y - r * 0.5f) }; dl->AddPolyline(p, 3, col, 0, th); }
    else     { ImVec2 p[3] = { ImVec2(c.x - r * 0.5f, c.y - r), ImVec2(c.x + r * 0.5f, c.y), ImVec2(c.x - r * 0.5f, c.y + r) }; dl->AddPolyline(p, 3, col, 0, th); }
}
// a tap target (no look): true on release inside
static bool NeonHit(const char* id, ImVec2 a, ImVec2 b)
{
    ImGui::SetCursorScreenPos(a);
    return ImGui::InvisibleButton(id, ImVec2(ImMax(1.0f, b.x - a.x), ImMax(1.0f, b.y - a.y)));
}
static void NeonBackdrop(ImDrawList* dl, ImVec2 ds, int dark)
{
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(6, 6, 8, 255));
    ImVec2 uv(1, 1);
    if(void* t = HUD::Texture("neon_bg.jpg", &uv))
    {   // the art on the right (like the reference: the character right, the menu over the dark left)
        const float ih = ds.y, iw = ih * (1131.0f / 707.0f), x0 = ImMax(ds.x - iw, ds.x * 0.30f);
        dl->AddImage((ImTextureID)t, ImVec2(x0, 0), ImVec2(x0 + iw, ih), ImVec2(0, 0), uv, IM_COL32(255, 255, 255, 255));
        dl->AddRectFilledMultiColor(ImVec2(x0, 0), ImVec2(x0 + iw * 0.55f, ds.y), IM_COL32(6, 6, 8, 255), IM_COL32(6, 6, 8, 0), IM_COL32(6, 6, 8, 0), IM_COL32(6, 6, 8, 255));
    }
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(24, 6, 8, dark));   // the dark red night of the reference
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x * 0.62f, ds.y), IM_COL32(4, 4, 6, 235), IM_COL32(4, 4, 6, 60), IM_COL32(4, 4, 6, 60), IM_COL32(4, 4, 6, 235));
    dl->AddRectFilledMultiColor(ImVec2(0, ds.y * 0.78f), ds, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 190), IM_COL32(0, 0, 0, 190));
    dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, ds.y * 0.16f), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 150), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
}

// ---- the server rows (the launcher's data, merged: featured first) --------------------------
static void NeonNearby(std::vector<LanServer>& nearby)
{
    ServerList::Poll();
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();
    { std::lock_guard<std::mutex> lk(s_LanMutex); nearby = s_Nearby; }
    static std::vector<LanServer> live; static uint64_t liveAt = 0;
    if(MP_NowMs() - liveAt > 1000) { liveAt = MP_NowMs(); live = Online::Snapshot(); }
    for(auto& n : nearby) if(n.token) for(auto& l : live) if(l.token == n.token) { n.players = l.players; n.maxPlayers = l.maxPlayers; break; }
    for(auto& l : live)
    {   // lobby servers the last scan has not seen yet
        bool have = false; for(auto& n : nearby) if(n.token == l.token && l.token) { have = true; break; }
        if(!have) nearby.push_back(l);
    }
    if(s_bHasNetwork && !IsSearching() && MP_NowMs() - s_nNearbyTime > 15000) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
}
static bool NeonFeatured(const LRow& r) { return r.kind == 0 || (r.kind >= 3 && r.kind <= 7); }
static bool NeonFind(const char* hay, const char* needle)
{
    if(!needle[0]) return true; if(!hay || !hay[0]) return false;
    char a[200], b[64]; MP_CopyStr(a, hay, sizeof(a)); MP_CopyStr(b, needle, sizeof(b));
    for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
    for(char* c = b; *c; ++c) *c = (char)tolower((unsigned char)*c);
    return strstr(a, b) != nullptr;
}
static bool NeonIsAddress(const char* s)
{   // "1.2.3.4", "1.2.3.4:7777", "host.name:7777", "mp://..."
    if(!s || !*s) return false;
    if(!strncasecmp(s, "mp://", 5)) return true;
    int dots = 0, digits = 0; for(const char* p = s; *p; ++p) { if(*p == ' ') return false; if(*p == '.') ++dots; if(isdigit((unsigned char)*p)) ++digits; }
    return dots >= 1 && (strchr(s, ':') || digits >= 4);
}
static void NeonBuild(std::vector<LRow>& out)
{
    out.clear();
    std::vector<LanServer> nearby; NeonNearby(nearby);
    char keep[32]; MP_CopyStr(keep, s_szSearch, sizeof(keep)); s_szSearch[0] = 0;
    const int sub = s_nInetSub, sc = s_nSortCol; s_nSortCol = -1;
    std::vector<LRow> tmp;
    auto add = [&](const LRow& r)
    {
        for(auto& q : out) if(q.port == r.port && !strcasecmp(q.name, r.name) && q.kind == r.kind) return;
        out.push_back(r);
    };
    if(s_nNeonList == 0 || s_nNeonList == 1) { s_nInetSub = 1; LBuildRows(LT_INTERNET, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 0) { s_nInetSub = 0; LBuildRows(LT_INTERNET, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 0 || s_nNeonList == 3) { LBuildRows(LT_LAN, nearby, tmp); for(auto& r : tmp) add(r); }
    if(s_nNeonList == 2) { LBuildRows(LT_FAVORITES, nearby, tmp); for(auto& r : tmp) add(r); }
    s_nInetSub = sub; s_nSortCol = sc; MP_CopyStr(s_szSearch, keep, sizeof(s_szSearch));
    const bool addr = NeonIsAddress(s_szNeonSearch);
    std::vector<LRow> f;
    for(auto& r : out)
    {
        if(!addr && s_szNeonSearch[0] && !NeonFind(r.name, s_szNeonSearch) && !NeonFind(r.desc, s_szNeonSearch) && !NeonFind(r.mode, s_szNeonSearch) && !NeonFind(r.lang, s_szNeonSearch)) continue;
        if(s_bNeonHideFull && r.maxp > 0 && r.players >= r.maxp) continue;
        if(s_bNeonHideEmpty && r.players <= 0) continue;
        if(s_bNeonHideLocked && r.locked) continue;
        f.push_back(r);
    }
    std::stable_sort(f.begin(), f.end(), [](const LRow& a, const LRow& b)
    {
        const bool fa = NeonFeatured(a), fb = NeonFeatured(b);
        if(fa != fb) return fa;
        if(fa) return false;   // the featured servers keep their own order (default, BCW, MW3, Mega Vil, Day Z, League)
        return a.players > b.players;
    });
    out.swap(f);
}
static void NeonRowKey(const LRow& r, char* o, size_t n) { snprintf(o, n, "%d|%s|%d", r.kind, r.name, r.port); }
static void NeonRowIcon(ImDrawList* dl, const LRow& r, ImVec2 a, float sz)
{
    const ImVec2 b(a.x + sz, a.y + sz);
    ImVec2 uv(1, 1); void* t = LRowPreview(r, &uv); if(!t) t = LRowBg(r, &uv);
    if(t)
    {
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 255), sz * 0.12f);
        LImageCover(dl, t, uv, a, b, IM_COL32(255, 255, 255, 255));
        dl->AddRect(a, b, IM_COL32(120, 64, 40, 255), sz * 0.12f, 0, UI::S(1.5f));
        return;
    }
    uint32_t h = 2166136261u; for(const char* p = r.name; *p; ++p) h = (h ^ (unsigned char)tolower((unsigned char)*p)) * 16777619u;
    static const ImU32 kc[5] = { IM_COL32(150, 52, 52, 255), IM_COL32(52, 140, 72, 255), IM_COL32(56, 92, 160, 255), IM_COL32(160, 120, 50, 255), IM_COL32(120, 70, 150, 255) };
    const ImU32 c = kc[h % 5];
    dl->AddRectFilled(a, b, IM_COL32(16, 16, 19, 255), sz * 0.12f);
    dl->AddRect(a, b, c, sz * 0.12f, 0, UI::S(1.5f));
    char ini[4] = { 0 }; int n = 0;
    for(const char* p = r.name; *p && n < 2; ++p) if(isalnum((unsigned char)*p) && (p == r.name || !isalnum((unsigned char)p[-1]))) ini[n++] = (char)toupper((unsigned char)*p);
    if(!n) { ini[0] = 'M'; ini[1] = 'P'; }
    ImFont* f = UI::CapsFont(); const float fs = sz * 0.30f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, ini);
    dl->AddText(f, fs, ImVec2(a.x + (sz - ts.x) * 0.5f, a.y + (sz - ts.y) * 0.5f), (c & 0x00FFFFFF) | 0xE0000000, ini);
}
static void NeonFlags(ImDrawList* dl, const LRow& r, ImVec2 rightTop, float fh)
{
    const float fw = fh * 1.45f;
    const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
    ImVec2 a(rightTop.x - fw, rightTop.y);
    LDrawFlag(dl, ci, a, ImVec2(a.x + fw, a.y + fh));
}
static void NeonJoinRow(const LRow& r);

// ---- the server browser (Browse servers) ------------------------------------------------------
static void OnNeonSearch(const char* t, void*) { MP_CopyStr(s_szNeonSearch, t ? t : "", sizeof(s_szNeonSearch)); }
static bool NeonJoinAddress(const char* s)
{
    if(!NeonIsAddress(s)) return false;
    char host[96]; MP_CopyStr(host, !strncasecmp(s, "mp://", 5) ? s + 5 : s, sizeof(host));
    int port = MP_DEFAULT_PORT; if(char* c = strrchr(host, ':')) { *c = 0; port = atoi(c + 1); if(port <= 0 || port > 65535) port = MP_DEFAULT_PORT; }
    LRow r; memset(&r, 0, sizeof(r)); r.kind = 1; MP_CopyStr(r.ip, host, sizeof(r.ip)); r.port = port; MP_CopyStr(r.name, host, sizeof(r.name));
    NeonJoinRow(r); return true;
}
static void NeonBrowser(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 773.0f;
    auto X = [&](float rx) { return ds.x * rx / 1487.0f; };
    ImFont* uf = UI::Font(); ImFont* cf = UI::CapsFont(); ImFont* pd = UI::PricedownFont() ? UI::PricedownFont() : UI::GothicFont();
    static std::vector<LRow> rows; static uint64_t builtAt = 0; static int builtList = -1; static char builtQ[64] = "\x01";
    if(MP_NowMs() - builtAt > 1000 || builtList != s_nNeonList || strcmp(builtQ, s_szNeonSearch))
    { builtAt = MP_NowMs(); builtList = s_nNeonList; MP_CopyStr(builtQ, s_szNeonSearch, sizeof(builtQ)); NeonBuild(rows); }
    int sel = -1; char key[64];
    for(int i = 0; i < (int)rows.size(); ++i) { NeonRowKey(rows[i], key, sizeof(key)); if(!strcmp(key, s_szNeonSelKey)) sel = i; }
    if(sel < 0 && !rows.empty()) { sel = 0; NeonRowKey(rows[0], s_szNeonSelKey, sizeof(s_szNeonSelKey)); }
    int total = 0; for(auto& r : rows) total += r.players > 0 ? r.players : 0;

    // header
    NeonText(dl, uf, 10.5f * u, ImVec2(X(57), 30 * u), kNGrey, "MP NEON NETWORK", 4.2f * u);
    NeonText(dl, pd, 62 * u, ImVec2(X(55), 42 * u), IM_COL32(190, 192, 196, 255), "choose your server", 0, true);
    char t1[48], t2[64]; snprintf(t1, sizeof(t1), "%d SERVERS", (int)rows.size()); snprintf(t2, sizeof(t2), "%d PLAYERS ONLINE", total);
    NeonTextR(dl, uf, 11.5f * u, ImVec2(X(1185), 33 * u), kNGrey, t1, 1.6f * u);
    NeonTextR(dl, uf, 11.5f * u, ImVec2(X(1185), 52 * u), kNGrey, t2, 1.6f * u);
    {   // BACK TO MAIN MENU
        const ImVec2 a(X(1212), 30 * u), b(X(1430), 70 * u);
        dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 240), 5 * u);
        dl->AddRect(a, b, IM_COL32(205, 182, 120, 255), 5 * u, 0, 1.6f * u);
        const float cy = (a.y + b.y) * 0.5f;
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 31 * u, cy), kNText, 1.6f * u);
        NeonChevron(dl, ImVec2(a.x + 21 * u, cy), 5 * u, kNText, 1.6f * u); // drawn as "<" below
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy - 6 * u), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy + 6 * u), kNText, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 44 * u, cy - 6.5f * u), kNText, "BACK TO MAIN MENU", 2.6f * u);
        if(NeonHit("##nbBack", a, b)) { s_nNeonPage = NP_MAIN; s_bNeonListOpen = s_bNeonFilterOpen = false; }
    }
    // toolbar
    const ImVec2 ta(X(57), 129 * u), tb(X(1430), 179 * u);
    dl->AddRectFilled(ta, tb, IM_COL32(13, 14, 17, 240), 5 * u);
    dl->AddRect(ta, tb, kNLine, 5 * u, 0, 1.2f * u);
    const float tcy = (ta.y + tb.y) * 0.5f;
    static const char* const kLists[4] = { "NEON SERVERS", "FEATURED", "FAVORITES", "WI-FI / LAN" };
    {
        NeonText(dl, uf, 11 * u, ImVec2(X(78), tcy - 7 * u), IM_COL32(180, 182, 188, 255), kLists[s_nNeonList], 3.2f * u);
        const float w = NeonTextW(uf, 11 * u, kLists[s_nNeonList], 3.2f * u);
        NeonChevron(dl, ImVec2(X(78) + w + 16 * u, tcy), 4.5f * u, IM_COL32(180, 182, 188, 255), 1.4f * u, true);
        if(NeonHit("##nbList", ImVec2(X(66), ta.y), ImVec2(X(245), tb.y))) { s_bNeonListOpen = !s_bNeonListOpen; s_bNeonFilterOpen = false; }
    }
    {   // refresh
        const ImVec2 c(X(268), tcy); const float r = 7.5f * u;
        dl->PathArcTo(c, r, 0.6f, 5.6f, 18); dl->PathStroke(IM_COL32(180, 182, 188, 255), 0, 1.6f * u);
        NeonTri(dl, ImVec2(c.x + r * 0.8f, c.y - r * 0.55f), 3 * u, IM_COL32(180, 182, 188, 255), 2);
        if(NeonHit("##nbRefresh", ImVec2(c.x - 16 * u, ta.y), ImVec2(c.x + 16 * u, tb.y))) { builtAt = 0; ServerList::Refresh(); s_nNearbyTime = 0; }
    }
    {   // search / address
        const ImVec2 a(X(293), 135 * u), b(X(1172), 172 * u);
        dl->AddRectFilled(a, b, IM_COL32(8, 9, 11, 255), 4 * u);
        dl->AddRect(a, b, IM_COL32(48, 50, 56, 255), 4 * u, 0, 1.2f * u);
        const ImVec2 mc(a.x + 22 * u, (a.y + b.y) * 0.5f - 1 * u);
        dl->AddCircle(mc, 5.5f * u, IM_COL32(190, 192, 198, 255), 16, 1.5f * u);
        dl->AddLine(ImVec2(mc.x + 4 * u, mc.y + 4 * u), ImVec2(mc.x + 8 * u, mc.y + 8 * u), IM_COL32(190, 192, 198, 255), 1.6f * u);
        char up[80]; NeonUpper(up, sizeof(up), s_szNeonSearch);
        NeonText(dl, uf, 14 * u, ImVec2(a.x + 38 * u, (a.y + b.y) * 0.5f - 8.5f * u), s_szNeonSearch[0] ? kNText : IM_COL32(170, 172, 178, 255),
                 s_szNeonSearch[0] ? s_szNeonSearch : "SEARCH SERVERS OR ENTER AN ADDRESS", s_szNeonSearch[0] ? 0.6f * u : 1.0f * u);
        if(NeonHit("##nbSearch", a, b)) VKeyboard::Open("Search servers or enter an address (ip:port)", s_szNeonSearch, 63, OnNeonSearch);
    }
    {   // CONNECT
        const ImVec2 a(X(1178), 135 * u), b(X(1301), 172 * u);
        dl->AddRectFilled(a, b, IM_COL32(18, 19, 23, 255), 4 * u);
        dl->AddRect(a, b, IM_COL32(56, 58, 64, 255), 4 * u, 0, 1.2f * u);
        NeonTri(dl, ImVec2(a.x + 19 * u, (a.y + b.y) * 0.5f), 3.2f * u, kNGold);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 32 * u, (a.y + b.y) * 0.5f - 6.5f * u), kNText, "CONNECT", 3.0f * u);
        if(NeonHit("##nbConnect", a, b)) { if(!NeonJoinAddress(s_szNeonSearch) && sel >= 0) NeonJoinRow(rows[sel]); }
    }
    {   // FILTERS
        NeonText(dl, uf, 11 * u, ImVec2(X(1322), tcy - 7 * u), IM_COL32(180, 182, 188, 255), "FILTERS", 3.2f * u);
        const float fx = X(1322) + NeonTextW(uf, 11 * u, "FILTERS", 3.2f * u) + 14 * u;
        dl->AddTriangleFilled(ImVec2(fx - 5 * u, tcy - 5 * u), ImVec2(fx + 5 * u, tcy - 5 * u), ImVec2(fx, tcy + 1 * u), IM_COL32(180, 182, 188, 255));
        dl->AddRectFilled(ImVec2(fx - 1 * u, tcy), ImVec2(fx + 1 * u, tcy + 5 * u), IM_COL32(180, 182, 188, 255));
        if(NeonHit("##nbFilter", ImVec2(X(1310), ta.y), ImVec2(X(1428), tb.y))) { s_bNeonFilterOpen = !s_bNeonFilterOpen; s_bNeonListOpen = false; }
    }
    // column heads
    NeonText(dl, uf, 10.5f * u, ImVec2(X(74), 191 * u), kNGrey, "SERVERS", 1.8f * u);
    NeonTextR(dl, uf, 10.5f * u, ImVec2(X(897), 191 * u), kNGrey, "PLAYERS", 1.8f * u);
    NeonTextR(dl, uf, 10.5f * u, ImVec2(X(944), 191 * u), kNGrey, "PING", 1.8f * u);

    // the list (scrolls; tap = select, tap the selected one again = join)
    const ImVec2 la(X(57), 219 * u), lb(X(966), 712 * u);
    ImGui::SetCursorScreenPos(la);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 4 * u);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.25f, 0.26f, 0.29f, 1));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    if(ImGui::BeginChild("##neonList", ImVec2(lb.x - la.x, lb.y - la.y), false, ImGuiWindowFlags_NoBackground))
    {
        ImDrawList* cl = ImGui::GetWindowDrawList();
        const float rw = X(954) - X(57), rh = 115 * u, gap = 5 * u;
        if(rows.empty())
        {
            const char* m = IsSearching() ? "SEARCHING FOR SERVERS..." : (s_szNeonSearch[0] ? "NO SERVER MATCHES THE SEARCH" : "NO SERVERS ONLINE RIGHT NOW");
            NeonText(cl, uf, 13 * u, ImVec2(la.x + 24 * u, la.y + 30 * u), kNGrey, m, 2.0f * u);
        }
        for(int i = 0; i < (int)rows.size(); ++i)
        {
            const LRow& r = rows[i];
            ImVec2 a = ImGui::GetCursorScreenPos(); a.x = la.x; const ImVec2 b(a.x + rw, a.y + rh);
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos(a);
            const bool tap = ImGui::InvisibleButton("##row", ImVec2(rw, rh));
            const bool press = ImGui::IsItemActive();
            ImGui::PopID();
            const bool on = i == sel;
            cl->AddRectFilled(a, b, on ? IM_COL32(24, 22, 18, 245) : (press ? IM_COL32(26, 27, 31, 245) : IM_COL32(17, 18, 21, 238)), 7 * u);
            cl->AddRect(a, b, on ? kNGold : kNLine, 7 * u, 0, on ? 1.6f * u : 1.1f * u);
            if(on) cl->AddRectFilled(ImVec2(a.x + 1 * u, a.y + 8 * u), ImVec2(a.x + 4 * u, b.y - 8 * u), kNGold, 2 * u);
            NeonTri(cl, ImVec2(a.x + 19 * u, a.y + rh * 0.5f), 2.6f * u, on ? kNGold : IM_COL32(110, 112, 118, 255));
            NeonRowIcon(cl, r, ImVec2(a.x + 41 * u, a.y + (rh - 66 * u) * 0.5f), 66 * u);
            char nm[64]; NeonUpper(nm, sizeof(nm), r.name);
            cl->PushClipRect(ImVec2(a.x + 120 * u, a.y), ImVec2(a.x + 500 * u * (ds.x / ds.y) / (1487.0f / 773.0f), b.y), true);
            NeonText(cl, cf, 19 * u, ImVec2(a.x + 127 * u, a.y + 31 * u), kNText, nm, 2.4f * u, true);
            cl->PopClipRect();
            char desc[MP_DESC_LEN + 40];
            if(r.desc[0]) MP_CopyStr(desc, r.desc, sizeof(desc));
            else snprintf(desc, sizeof(desc), "%s%s%s", r.mode[0] ? r.mode : "Freeroam", r.lang[0] ? " - " : "", r.lang[0] ? r.lang : "");
            const float dmaxX = X(560) - (NeonFeatured(r) ? 110 * u : 0);
            float dw = uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc).x;
            if(a.x + 127 * u + dw > dmaxX) { size_t n = strlen(desc); while(n > 3 && a.x + 127 * u + uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc, desc + n).x > dmaxX - 14 * u) --n; strcpy(desc + n, "..."); dw = uf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, desc).x; }
            NeonText(cl, uf, 14.5f * u, ImVec2(a.x + 127 * u, a.y + 64 * u), IM_COL32(192, 195, 201, 255), desc);
            if(NeonFeatured(r))
            {
                const ImVec2 ga(a.x + 127 * u + dw + 12 * u, a.y + 66 * u);
                const float gw = NeonTextW(uf, 9 * u, "NEON FEATURED", 1.2f * u) + 14 * u;
                cl->AddRect(ga, ImVec2(ga.x + gw, ga.y + 16 * u), kNGold, 1 * u, 0, 1.1f * u);
                NeonText(cl, uf, 9 * u, ImVec2(ga.x + 7 * u, ga.y + 2.8f * u), kNGold, "NEON FEATURED", 1.2f * u);
            }
            NeonFlags(cl, r, ImVec2(X(733), a.y + rh * 0.5f - 6 * u), 12 * u);
            {   // favourite star
                const ImVec2 sc(X(760), a.y + rh * 0.5f);
                const bool fav = LIsFav(r.name, r.port);
                LStar(cl, sc, 8 * u, fav ? kNGold : IM_COL32(150, 152, 158, 255), fav);
                ImGui::PushID(i + 100000);
                if(NeonHit("##star", ImVec2(sc.x - 18 * u, a.y + 20 * u), ImVec2(sc.x + 18 * u, b.y - 20 * u))) LSetFav(r.name, r.port, !fav);
                ImGui::PopID();
            }
            cl->AddLine(ImVec2(X(786), a.y + 26 * u), ImVec2(X(786), b.y - 26 * u), kNLine, 1.1f * u);
            char pl[32]; if(r.maxp > 0) snprintf(pl, sizeof(pl), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else MP_CopyStr(pl, "-", sizeof(pl));
            ImFont* bf = UI::Font();
            const ImVec2 ps = bf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, pl);
            cl->AddText(bf, 14.5f * u, ImVec2(X(869) - ps.x, a.y + rh * 0.5f - ps.y * 0.5f), kNText, pl);
            cl->AddText(bf, 14.5f * u, ImVec2(X(869) - ps.x + 0.8f * u, a.y + rh * 0.5f - ps.y * 0.5f), kNText, pl);
            char pg[24]; if(r.ping >= 0) snprintf(pg, sizeof(pg), "%d MS", r.ping); else MP_CopyStr(pg, "- MS", sizeof(pg));
            const ImVec2 gs = bf->CalcTextSizeA(14.5f * u, FLT_MAX, 0, pg);
            const ImU32 pc = r.ping < 0 ? kNGrey : (r.ping < 90 ? kNBlue : (r.ping < 180 ? IM_COL32(240, 200, 110, 255) : IM_COL32(240, 120, 110, 255)));
            cl->AddText(bf, 14.5f * u, ImVec2(X(938) - gs.x, a.y + rh * 0.5f - gs.y * 0.5f), pc, pg);
            cl->AddText(bf, 14.5f * u, ImVec2(X(938) - gs.x + 0.8f * u, a.y + rh * 0.5f - gs.y * 0.5f), pc, pg);
            if(tap && !s_bNeonListOpen && !s_bNeonFilterOpen)
            {
                const uint64_t now = MP_NowMs();
                if(on && now - s_nNeonTapAt < 600) NeonJoinRow(r);
                NeonRowKey(r, s_szNeonSelKey, sizeof(s_szNeonSelKey)); s_nNeonTapAt = now;
            }
            ImGui::SetCursorScreenPos(ImVec2(la.x, b.y + gap));
        }
        ImGui::Dummy(ImVec2(1, 10 * u));
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(3); ImGui::PopStyleVar();

    // the selected server
    const ImVec2 pa(X(997), 192 * u), pb(X(1430), 712 * u);
    dl->AddRectFilled(pa, pb, IM_COL32(13, 14, 17, 242), 7 * u);
    dl->AddRect(pa, pb, kNLine, 7 * u, 0, 1.1f * u);
    if(sel >= 0)
    {
        const LRow& r = rows[sel];
        const float px = pa.x + 24 * u, pw = pb.x - pa.x - 48 * u;
        NeonRowIcon(dl, r, ImVec2(px, pa.y + 18 * u), 66 * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(px + 81 * u, pa.y + 30 * u), kNGold, "SELECTED SERVER", 1.4f * u);
        char nm[64]; NeonUpper(nm, sizeof(nm), r.name);
        float ns = 21 * u; while(ns > 11 * u && NeonTextW(cf, ns, nm, 3.0f * u) > pw - 81 * u) ns -= 1 * u;
        NeonText(dl, cf, ns, ImVec2(px + 82 * u, pa.y + 52 * u), kNText, nm, 3.0f * u, true);
        // description (wrapped, 4 lines)
        char dsc[MP_DESC_LEN + 80];
        if(r.desc[0]) MP_CopyStr(dsc, r.desc, sizeof(dsc));
        else snprintf(dsc, sizeof(dsc), "%s on MP. %s%s Tap JOIN SERVER to play.", r.mode[0] ? r.mode : "Freeroam", r.lang[0] ? "Language: " : "", r.lang[0] ? r.lang : "");
        dl->PushClipRect(ImVec2(px, pa.y + 100 * u), ImVec2(pb.x - 20 * u, pa.y + 196 * u), true);
        dl->AddText(uf, 14.5f * u, ImVec2(px, pa.y + 104 * u), IM_COL32(205, 208, 214, 255), dsc, nullptr, pw);
        dl->PopClipRect();
        // server link
        const ImVec2 sa(px, pa.y + 206 * u), sb(pb.x - 24 * u, pa.y + 259 * u);
        dl->AddRectFilled(sa, sb, IM_COL32(9, 10, 12, 255), 4 * u); dl->AddRect(sa, sb, kNLine, 4 * u, 0, 1.1f * u);
        NeonText(dl, uf, 9.5f * u, ImVec2(sa.x + 11 * u, sa.y + 9 * u), kNGrey, "SERVER LINK", 1.4f * u);
        char link[128];
        if(r.ip[0]) snprintf(link, sizeof(link), "mp://%s:%d", r.ip, r.port); else snprintf(link, sizeof(link), "mp://%s", r.name);
        NeonText(dl, uf, 12 * u, ImVec2(sa.x + 11 * u, sa.y + 27 * u), kNText, link);
        {
            const ImVec2 ca(sb.x - 106 * u, sa.y + 10 * u), cb(sb.x - 10 * u, sb.y - 10 * u);
            dl->AddRect(ca, cb, kNGoldDim, 3 * u, 0, 1.2f * u);
            dl->AddRect(ImVec2(ca.x + 9 * u, ca.y + 9 * u), ImVec2(ca.x + 17 * u, ca.y + 19 * u), kNGold, 1 * u, 0, 1.1f * u);
            dl->AddRect(ImVec2(ca.x + 12 * u, ca.y + 6 * u), ImVec2(ca.x + 20 * u, ca.y + 16 * u), kNGold, 1 * u, 0, 1.1f * u);
            const bool copied = MP_NowMs() - s_nNeonCopiedAt < 1500;
            NeonText(dl, uf, 9.5f * u, ImVec2(ca.x + 28 * u, ca.y + 9 * u), kNGold, copied ? "COPIED" : "COPY LINK", 1.0f * u);
            if(NeonHit("##nbCopy", ca, cb)) { ImGui::SetClipboardText(link); s_nNeonCopiedAt = MP_NowMs(); }
        }
        // players / ping / mode boxes
        const float bw = (sb.x - sa.x - 2 * 8 * u) / 3.0f, by0 = pa.y + 275 * u, by1 = pa.y + 331 * u;
        char v0[32], v1[24], v2[MP_MODE_LEN + 4];
        if(r.maxp > 0) snprintf(v0, sizeof(v0), "%d / %d", r.players < 0 ? 0 : r.players, r.maxp); else MP_CopyStr(v0, "-", sizeof(v0));
        if(r.ping >= 0) snprintf(v1, sizeof(v1), "%d MS", r.ping); else MP_CopyStr(v1, "-", sizeof(v1));
        NeonUpper(v2, sizeof(v2), r.mode[0] ? r.mode : "Freeroam");
        const char* hl[3] = { "PLAYERS", "PING", "MODE" }; const char* vl[3] = { v0, v1, v2 };
        for(int k = 0; k < 3; ++k)
        {
            const ImVec2 ba(sa.x + k * (bw + 8 * u), by0), bb(ba.x + bw, by1);
            dl->AddRectFilled(ba, bb, IM_COL32(9, 10, 12, 255), 4 * u); dl->AddRect(ba, bb, kNLine, 4 * u, 0, 1.1f * u);
            NeonText(dl, uf, 9.5f * u, ImVec2(ba.x + 10 * u, ba.y + 9 * u), kNGrey, hl[k], 1.4f * u);
            dl->PushClipRect(ba, ImVec2(bb.x - 4 * u, bb.y), true);
            NeonText(dl, uf, 13.5f * u, ImVec2(ba.x + 10 * u, ba.y + 27 * u), kNText, vl[k]);
            NeonText(dl, uf, 13.5f * u, ImVec2(ba.x + 10.7f * u, ba.y + 27 * u), kNText, vl[k]);
            dl->PopClipRect();
        }
        // regions & languages
        {
            const float gy = pa.y + 360 * u; const ImVec2 gc(px + 6 * u, gy + 6 * u);
            dl->AddCircle(gc, 6 * u, kNGold, 16, 1.2f * u); dl->AddLine(ImVec2(gc.x - 6 * u, gc.y), ImVec2(gc.x + 6 * u, gc.y), kNGold, 1.0f * u);
            dl->AddLine(ImVec2(gc.x, gc.y - 6 * u), ImVec2(gc.x, gc.y + 6 * u), kNGold, 1.0f * u);
            NeonText(dl, uf, 10.5f * u, ImVec2(px + 20 * u, gy), IM_COL32(200, 202, 208, 255), "REGIONS & LANGUAGES", 1.4f * u);
            const int ci = LCountryIndex(r.cc[0] ? r.cc : MP_CC_GLOBAL);
            LDrawFlag(dl, ci, ImVec2(px, gy + 20 * u), ImVec2(px + 17 * u, gy + 32 * u));
            char rl[160]; const char* cn = LCountryNameAt(ci);
            static const char* const kReg[4] = { "ASIA", "EUROPE", "NORTH AMERICA", "OCEANIA" };
            char lu[MP_LANG_LEN + 4]; NeonUpper(lu, sizeof(lu), r.lang[0] ? r.lang : "All");
            char cu[64]; NeonUpper(cu, sizeof(cu), cn ? cn : "Global");
            snprintf(rl, sizeof(rl), "/ %s   / %s%s%s", cu, lu, r.region >= 0 && r.region < 4 ? "   / " : "", r.region >= 0 && r.region < 4 ? kReg[r.region] : "");
            dl->PushClipRect(ImVec2(px + 22 * u, gy + 16 * u), ImVec2(pb.x - 16 * u, gy + 60 * u), true);
            NeonText(dl, uf, 10.5f * u, ImVec2(px + 24 * u, gy + 21 * u), IM_COL32(165, 168, 175, 255), rl, 0.6f * u);
            std::vector<std::string> pls; LDetailPlayers(r, pls);
            if(!pls.empty())
            {
                std::string s = "PLAYING: "; for(size_t k = 0; k < pls.size() && k < 8; ++k) { if(k) s += ", "; s += pls[k]; }
                NeonText(dl, uf, 10.5f * u, ImVec2(px, gy + 40 * u), IM_COL32(140, 143, 150, 255), s.c_str());
            }
            dl->PopClipRect();
        }
        {   // JOIN SERVER
            const ImVec2 ja(pa.x + 18 * u, pb.y - 79 * u), jb(pb.x - 17 * u, pb.y - 17 * u);
            const bool connecting = client.GetState() != NET_DISCONNECTED;
            ImGui::SetCursorScreenPos(ja);
            const bool tap = ImGui::InvisibleButton("##nbJoin", ImVec2(jb.x - ja.x, jb.y - ja.y));
            const bool press = ImGui::IsItemActive();
            dl->AddRectFilledMultiColor(ja, jb, IM_COL32(40, 38, 32, 255), IM_COL32(20, 20, 22, 255), IM_COL32(16, 16, 18, 255), IM_COL32(34, 32, 28, 255));
            if(press) dl->AddRectFilled(ja, jb, IM_COL32(255, 230, 160, 30));
            dl->AddRect(ja, jb, kNGoldDim, 4 * u, 0, 1.3f * u);
            dl->AddRectFilled(ImVec2(ja.x, ja.y), ImVec2(ja.x + 3 * u, jb.y), kNGold);
            const float jy = (ja.y + jb.y) * 0.5f;
            NeonTri(dl, ImVec2(ja.x + 46 * u, jy), 5 * u, IM_COL32(220, 220, 224, 255));
            const char* jt = connecting ? "CONNECTING" : "JOIN SERVER";
            float js = 27 * u; while(js > 14 * u && NeonTextW(cf, js, jt, 6 * u) > jb.x - ja.x - 130 * u) js -= 1 * u;
            NeonText(dl, cf, js, ImVec2(ja.x + 70 * u, jy - js * 0.62f), kNText, jt, 6 * u, true);
            NeonChevron(dl, ImVec2(jb.x - 26 * u, jy), 5 * u, IM_COL32(200, 200, 204, 255), 1.5f * u);
            if(tap && !connecting) NeonJoinRow(r);
        }
    }
    // hints
    {
        const float hy = 734 * u;
        float hx = X(1430);
        const char* h2 = "JOIN SERVER"; hx -= NeonTextW(uf, 10.5f * u, h2, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(hx, hy), kNGrey, h2, 1.6f * u);
        dl->AddLine(ImVec2(hx - 17 * u, hy + 3 * u), ImVec2(hx - 17 * u, hy + 9 * u), kNGrey, 1.2f * u);
        dl->AddLine(ImVec2(hx - 17 * u, hy + 9 * u), ImVec2(hx - 25 * u, hy + 9 * u), kNGrey, 1.2f * u);
        NeonTri(dl, ImVec2(hx - 25 * u, hy + 9 * u), 2.5f * u, kNGrey, 3);
        hx -= 50 * u; const char* h1 = "NAVIGATE"; hx -= NeonTextW(uf, 10.5f * u, h1, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(hx, hy), kNGrey, h1, 1.6f * u);
        NeonTri(dl, ImVec2(hx - 13 * u, hy + 8 * u), 3 * u, kNGrey, 1); NeonTri(dl, ImVec2(hx - 24 * u, hy + 5 * u), 3 * u, kNGrey, 2);
    }
    // dropdowns (on top)
    if(s_bNeonListOpen)
    {
        const ImVec2 a(X(66), tb.y + 4 * u), b(X(300), tb.y + 4 * u + 4 * 40 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 13, 16, 252), 5 * u); dl->AddRect(a, b, kNLine, 5 * u, 0, 1.1f * u);
        for(int k = 0; k < 4; ++k)
        {
            const ImVec2 ia(a.x, a.y + k * 40 * u), ib(b.x, ia.y + 40 * u);
            if(k == s_nNeonList) dl->AddRectFilled(ImVec2(ia.x + 2 * u, ia.y + 2 * u), ImVec2(ib.x - 2 * u, ib.y - 2 * u), IM_COL32(40, 36, 26, 255), 4 * u);
            NeonText(dl, uf, 11 * u, ImVec2(ia.x + 16 * u, ia.y + 13 * u), k == s_nNeonList ? kNGold : kNText, kLists[k], 2.6f * u);
            char id[16]; snprintf(id, sizeof(id), "##nbL%d", k);
            if(NeonHit(id, ia, ib)) { s_nNeonList = k; s_bNeonListOpen = false; builtAt = 0; }
        }
    }
    if(s_bNeonFilterOpen)
    {
        const ImVec2 a(X(1190), tb.y + 4 * u), b(X(1430), tb.y + 4 * u + 3 * 40 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 13, 16, 252), 5 * u); dl->AddRect(a, b, kNLine, 5 * u, 0, 1.1f * u);
        bool* vs[3] = { &s_bNeonHideFull, &s_bNeonHideEmpty, &s_bNeonHideLocked };
        static const char* const kF[3] = { "HIDE FULL", "HIDE EMPTY", "HIDE LOCKED" };
        for(int k = 0; k < 3; ++k)
        {
            const ImVec2 ia(a.x, a.y + k * 40 * u), ib(b.x, ia.y + 40 * u);
            const ImVec2 ca(ia.x + 14 * u, ia.y + 12 * u), cb(ca.x + 16 * u, ca.y + 16 * u);
            dl->AddRect(ca, cb, kNGoldDim, 2 * u, 0, 1.2f * u);
            if(*vs[k]) dl->AddRectFilled(ImVec2(ca.x + 4 * u, ca.y + 4 * u), ImVec2(cb.x - 4 * u, cb.y - 4 * u), kNGold, 1 * u);
            NeonText(dl, uf, 11 * u, ImVec2(ia.x + 42 * u, ia.y + 13 * u), kNText, kF[k], 2.2f * u);
            char id[16]; snprintf(id, sizeof(id), "##nbF%d", k);
            if(NeonHit(id, ia, ib)) { *vs[k] = !*vs[k]; builtAt = 0; }
        }
    }
}

// ---- settings / about / quit -----------------------------------------------------------------
static void NeonSettings(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 804.0f;
    ImFont* uf = UI::Font(); ImFont* pd = UI::PricedownFont() ? UI::PricedownFont() : UI::GothicFont();
    NeonText(dl, uf, 10.5f * u, ImVec2(ds.x * 0.04f, 30 * u), kNGrey, "MP NEON NETWORK", 4.2f * u);
    NeonText(dl, pd, 60 * u, ImVec2(ds.x * 0.04f, 42 * u), IM_COL32(190, 192, 196, 255), "settings", 0, true);
    {
        const ImVec2 a(ds.x - 260 * u, 30 * u), b(ds.x - 40 * u, 70 * u);
        dl->AddRectFilled(a, b, IM_COL32(10, 10, 12, 240), 5 * u); dl->AddRect(a, b, IM_COL32(205, 182, 120, 255), 5 * u, 0, 1.6f * u);
        const float cy = (a.y + b.y) * 0.5f;
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 31 * u, cy), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy - 6 * u), kNText, 1.6f * u);
        dl->AddLine(ImVec2(a.x + 18 * u, cy), ImVec2(a.x + 24 * u, cy + 6 * u), kNText, 1.6f * u);
        NeonText(dl, uf, 10.5f * u, ImVec2(a.x + 44 * u, cy - 6.5f * u), kNText, "BACK TO MAIN MENU", 2.6f * u);
        if(NeonHit("##nsBack", a, b)) s_nNeonPage = NP_MAIN;
    }
    const int n = FloatMenu::SectionCount();
    if(s_nNeonSetSec < 0 || s_nNeonSetSec >= n)
    {   // Display first (the GTA HUD settings live there)
        s_nNeonSetSec = 0; for(int i = 0; i < n; ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Display")) s_nNeonSetSec = i;
    }
    const float lx = ds.x * 0.04f, ly = 125 * u, lw = 250 * u, ih = 46 * u;
    for(int i = 0; i <= n; ++i)
    {
        const ImVec2 a(lx, ly + i * (ih + 4 * u)), b(lx + lw, a.y + ih);
        const bool on = i == s_nNeonSetSec;
        dl->AddRectFilled(a, b, on ? IM_COL32(30, 27, 20, 245) : IM_COL32(15, 16, 19, 235), 5 * u);
        dl->AddRect(a, b, on ? kNGold : kNLine, 5 * u, 0, 1.2f * u);
        if(on) dl->AddRectFilled(ImVec2(a.x + 1 * u, a.y + 7 * u), ImVec2(a.x + 4 * u, b.y - 7 * u), kNGold, 2 * u);
        char t[48]; NeonUpper(t, sizeof(t), i < n ? FloatMenu::SectionTitle(i) : "Game settings");
        NeonText(dl, uf, 12 * u, ImVec2(a.x + 18 * u, a.y + 15 * u), on ? kNGold : kNText, t, 2.2f * u);
        char id[16]; snprintf(id, sizeof(id), "##nsS%d", i);
        if(NeonHit(id, a, b))
        {
            if(i < n) s_nNeonSetSec = i;
            else { s_bNeonHidden = true; logger->Info("Neon menu: the game's own menu (Game settings)"); }
        }
    }
    const ImVec2 pa(lx + lw + 20 * u, ly), pb(ds.x - 40 * u, ds.y - 40 * u);
    dl->AddRectFilled(pa, pb, IM_COL32(12, 13, 16, 240), 7 * u); dl->AddRect(pa, pb, kNLine, 7 * u, 0, 1.1f * u);
    ImGui::SetCursorScreenPos(ImVec2(pa.x + 16 * u, pa.y + 14 * u));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    if(ImGui::BeginChild("##neonSet", ImVec2(pb.x - pa.x - 32 * u, pb.y - pa.y - 28 * u), false, ImGuiWindowFlags_NoBackground))
    {
        VPushWidgetStyle();
        FloatMenu::DrawSection(s_nNeonSetSec);
        VPopWidgetStyle();
        LDragScroll();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}
static void NeonDialog(ImDrawList* dl, ImVec2 ds, const char* title, const char* body, const char* yes, const char* no, bool* outYes, bool* outNo)
{
    const float u = ds.y / 804.0f;
    dl->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 150));
    const float w = 640 * u, h = 300 * u; const ImVec2 a((ds.x - w) * 0.5f, (ds.y - h) * 0.5f), b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, IM_COL32(13, 14, 17, 250), 7 * u); dl->AddRect(a, b, kNGoldDim, 7 * u, 0, 1.4f * u);
    dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 3 * u, b.y), kNGold);
    NeonText(dl, UI::GothicFont(), 40 * u, ImVec2(a.x + 30 * u, a.y + 20 * u), kNText, title, 0, true);
    dl->AddText(UI::Font(), 15 * u, ImVec2(a.x + 30 * u, a.y + 88 * u), IM_COL32(200, 203, 210, 255), body, nullptr, w - 60 * u);
    const float bw = (w - 60 * u - 16 * u) * (no ? 0.5f : 1.0f);
    for(int k = 0; k < (no ? 2 : 1); ++k)
    {
        const ImVec2 ba(a.x + 30 * u + k * (bw + 16 * u), b.y - 74 * u), bb(ba.x + bw, b.y - 24 * u);
        dl->AddRectFilled(ba, bb, IM_COL32(24, 23, 20, 255), 4 * u); dl->AddRect(ba, bb, k == 0 ? kNGold : kNLine, 4 * u, 0, 1.3f * u);
        const char* t = k == 0 ? yes : no;
        const float tw = NeonTextW(UI::Font(), 12 * u, t, 2.6f * u);
        NeonText(dl, UI::Font(), 12 * u, ImVec2((ba.x + bb.x - tw) * 0.5f, (ba.y + bb.y) * 0.5f - 7.5f * u), kNText, t, 2.6f * u);
        char id[16]; snprintf(id, sizeof(id), "##ndB%d", k);
        if(NeonHit(id, ba, bb)) { if(k == 0) *outYes = true; else *outNo = true; }
    }
}

// ---- the main menu -----------------------------------------------------------------------------
static bool NeonTitleCtx() { return Game::IsMainMenuShown() && Game::LocalPed() <= 0 && !s_bMenuMp.load(); }
// 4.13.23: the game draws its title menu (MobileMenu::Render) after MP's HUD, so in 4.13.22 its full-screen
// background covered the neon menu and only the game's menu showed. While the neon menu is up the game's menu
// is not drawn (main.cpp MobileMenu_Render) and gets no touches.
bool MP::NeonCoversGameMenu()
{
    if(!NeonOn()) return false;   // 4.13.25: ServerBrowserMode = mp / default: nothing is read
    const bool on = true, menu = Game::IsMainMenuShown(), ped = Game::LocalPed() > 0, mp = s_bMenuMp.load();
    const bool r = on && !s_bNeonHidden && menu && !ped && !mp;
    if(r) s_nNeonUpMs = MP_NowMs();
    static int last = -1; const int st = (on << 0) | (s_bNeonHidden << 1) | (menu << 2) | (ped << 3) | (mp << 4) | (r << 5);
    if(st != last)
    {   // the MP log says why the neon menu is (not) up
        last = st;
        logger->Info("Neon menu: %s (mode %d, tile %d, api %d, game settings %d, game menu %d, game loaded %d, MP load %d)",
            r ? "on screen" : "not shown", s_nNeonMode, (int)s_bMenuMpOn, (int)s_bMenuMpViaApi, (int)s_bNeonHidden, (int)menu, (int)ped, (int)mp);
    }
    return r;
}
static void NeonResume()
{
    if(!NeonTitleCtx()) { s_bMenuMpExit = true; return; }   // the black screen: the game is loaded - back to it
    const int slot = Game::NewestSaveSlot();
    const bool ok = slot >= 0 ? Game::RequestLoadSlot(slot) : Game::RequestNewGame();
    logger->Info("Neon menu: Resume -> %s %d (%s)", slot >= 0 ? "load slot" : "new game", slot + 1, ok ? "ok" : "failed");
}
static void NeonNewGame()
{
    if(!NeonTitleCtx()) s_bMenuMpExit = true;
    const bool ok = Game::RequestNewGame();
    logger->Info("Neon menu: New game (%s)", ok ? "ok" : "failed");
}
static void NeonJoinRow(const LRow& r)
{
    s_bNeonListOpen = s_bNeonFilterOpen = false;
    if(NeonTitleCtx())
    {   // no game yet: the Multiplayer load first (newest save / new game), the join runs when it is done
        s_NeonPending = r; s_bNeonPendingJoin = true;
        logger->Info("Neon menu: join %s after the Multiplayer load", r.name);
        MenuMpTap(nullptr);
        return;
    }
    s_bNeonPendingJoin = false;
    logger->Info("Neon menu: join %s", r.name);
    LJoin(r);
}
static bool NeonBestRow(LRow& out)
{   // Quick connect with no address: the featured server with the most players (the default server when all are empty)
    std::vector<LRow> rows; const int keep = s_nNeonList; char q[64]; MP_CopyStr(q, s_szNeonSearch, sizeof(q));
    s_nNeonList = 0; s_szNeonSearch[0] = 0; NeonBuild(rows); s_nNeonList = keep; MP_CopyStr(s_szNeonSearch, q, sizeof(s_szNeonSearch));
    int best = -1;
    for(int i = 0; i < (int)rows.size(); ++i)
    {
        const LRow& r = rows[i]; if(r.locked || (r.maxp > 0 && r.players >= r.maxp)) continue;
        if(best < 0 || (NeonFeatured(r) && !NeonFeatured(rows[best])) || (NeonFeatured(r) == NeonFeatured(rows[best]) && r.players > rows[best].players)) best = i;
    }
    if(best < 0) return false;
    out = rows[best]; return true;
}
static void OnNeonQuick(const char* t, void*)
{
    if(t && *t && NeonJoinAddress(t)) return;
    if(t && *t) { MP_CopyStr(s_szNeonSearch, t, sizeof(s_szNeonSearch)); s_nNeonPage = NP_BROWSER; return; }   // a name: search it
    LRow r; if(NeonBestRow(r)) NeonJoinRow(r); else s_nNeonPage = NP_BROWSER;
}
static const char* NeonLangName()
{
    static const struct { const char* c; const char* n; } k[] = { { "en", "ENGLISH" }, { "es", "ESPAÑOL" }, { "pt", "PORTUGUÊS" }, { "fr", "FRANÇAIS" }, { "de", "DEUTSCH" },
        { "ru", "РУССКИЙ" }, { "tr", "TÜRKÇE" }, { "id", "INDONESIA" }, { "fil", "FILIPINO" }, { "tl", "FILIPINO" }, { "vi", "TIẾNG VIỆT" }, { "it", "ITALIANO" }, { "pl", "POLSKI" } };
    for(auto& e : k) if(!strcasecmp(settings.uiLanguage, e.c)) return e.n;
    static char up[16]; NeonUpper(up, sizeof(up), settings.uiLanguage[0] ? settings.uiLanguage : "en"); return up;
}
static void NeonMain(ImDrawList* dl, ImVec2 ds)
{
    const float u = ds.y / 804.0f;
    const float L = ImMin(ds.x * 0.07f, 100 * u * (ds.x / ds.y) / (1430.0f / 804.0f));
    ImFont* uf = UI::Font(); ImFont* gf = UI::GothicFont(); ImFont* cf = UI::CapsFont();
    {   // the logo (title.png)
        ImVec2 tuv(1, 1);
        if(void* t = HUD::Texture("title.png", &tuv))
        {
            const float w = 490 * u, h = w * (601.0f / 1920.0f);
            dl->AddImage((ImTextureID)t, ImVec2(L, 52 * u), ImVec2(L + w, 52 * u + h), ImVec2(0, 0), tuv);
        }
    }
    static const char* const kItems[] = { "Resume", "New game", "Browse servers", "Quick connect", "Settings", "About", "Quit game" };
    const int ni = 7; const float fs = 38 * u, step = 43 * u, y0 = 205 * u, ix = L + 24 * u;
    for(int i = 0; i < ni; ++i)
    {
        const float y = y0 + i * step;
        const float w = gf->CalcTextSizeA(fs, FLT_MAX, 0, kItems[i]).x;
        ImGui::SetCursorScreenPos(ImVec2(ix - 30 * u, y));
        char id[16]; snprintf(id, sizeof(id), "##nm%d", i);
        const bool tap = ImGui::InvisibleButton(id, ImVec2(w + 60 * u, step));
        const bool press = ImGui::IsItemActive();
        const bool on = i == s_nNeonSel || press;
        if(on) NeonTri(dl, ImVec2(ix - 14 * u, y + step * 0.5f), 3.6f * u, IM_COL32(222, 200, 140, 255));
        // the silver blackletter with a dark edge (like the reference)
        const ImVec2 p(ix, y + (step - fs) * 0.5f - 2 * u);
        for(int k = 0; k < 4; ++k) dl->AddText(gf, fs, ImVec2(p.x + ((k & 1) ? 1.4f : -1.4f) * u, p.y + ((k & 2) ? 1.4f : -1.4f) * u), IM_COL32(10, 12, 18, 230), kItems[i]);
        dl->AddText(gf, fs, ImVec2(p.x + 2 * u, p.y + 3 * u), IM_COL32(0, 0, 0, 160), kItems[i]);
        const int vb = dl->VtxBuffer.Size;
        dl->AddText(gf, fs, p, IM_COL32_WHITE, kItems[i]);
        {   // vertical silver gradient on the glyphs
            const ImU32 top = on ? IM_COL32(240, 243, 250, 255) : IM_COL32(196, 204, 220, 255), bot = on ? IM_COL32(170, 180, 205, 255) : IM_COL32(112, 122, 145, 255);
            for(int v = vb; v < dl->VtxBuffer.Size; ++v)
            {
                ImDrawVert& vt = dl->VtxBuffer[v]; float t = (vt.pos.y - p.y) / fs; t = t < 0 ? 0 : (t > 1 ? 1 : t);
                const ImVec4 a = ImGui::ColorConvertU32ToFloat4(top), b = ImGui::ColorConvertU32ToFloat4(bot);
                vt.col = ImGui::ColorConvertFloat4ToU32(ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f));
            }
        }
        if(tap)
        {
            s_nNeonSel = i;
            switch(i)
            {
                case 0: NeonResume(); break;
                case 1: NeonNewGame(); break;
                case 2: s_nNeonPage = NP_BROWSER; break;
                case 3: VKeyboard::Open("Quick connect: ip:port (empty = the best featured server)", "", 63, OnNeonQuick); break;
                case 4: s_nNeonPage = NP_SETTINGS; break;
                case 5: s_nNeonPage = NP_ABOUT; break;
                case 6: s_nNeonPage = NP_QUIT; break;
            }
        }
    }
    {   // tagline
        const char* tg = "FIND A WORLD AND JOIN THE STREETS OF SAN\nANDREAS.";
        const float ty = y0 + ni * step + 22 * u;
        const char* nl = strchr(tg, '\n');
        std::string l1(tg, nl - tg);
        NeonText(dl, uf, 12.5f * u, ImVec2(ix + 10 * u, ty), IM_COL32(205, 208, 214, 255), l1.c_str(), 1.0f * u);
        NeonText(dl, uf, 12.5f * u, ImVec2(ix + 10 * u, ty + 16 * u), IM_COL32(205, 208, 214, 255), nl + 1, 1.0f * u);
    }
    {   // Neon Identity (top right)
        const ImVec2 a(ds.x - 445 * u, 40 * u), b(ds.x - 68 * u, 103 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 12, 14, 232), 2 * u);
        dl->AddRect(a, b, IM_COL32(40, 40, 44, 255), 2 * u, 0, 1.0f * u);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 2.5f * u, b.y), IM_COL32(184, 154, 90, 255));
        const ImVec2 ia(a.x + 11 * u, a.y + 13 * u), ib(ia.x + 37 * u, ia.y + 37 * u);
        dl->AddRectFilled(ia, ib, IM_COL32(26, 27, 31, 255), 2 * u); dl->AddRect(ia, ib, IM_COL32(70, 72, 78, 255), 2 * u, 0, 1.0f * u);
        {   // the Discord mark (simplified: a rounded controller face with two eyes)
            const ImVec2 c((ia.x + ib.x) * 0.5f, (ia.y + ib.y) * 0.5f);
            dl->AddRectFilled(ImVec2(c.x - 10 * u, c.y - 7 * u), ImVec2(c.x + 10 * u, c.y + 7 * u), IM_COL32(225, 228, 236, 255), 6 * u);
            dl->AddTriangleFilled(ImVec2(c.x - 10 * u, c.y + 2 * u), ImVec2(c.x - 6 * u, c.y + 7 * u), ImVec2(c.x - 12 * u, c.y + 9 * u), IM_COL32(225, 228, 236, 255));
            dl->AddTriangleFilled(ImVec2(c.x + 10 * u, c.y + 2 * u), ImVec2(c.x + 6 * u, c.y + 7 * u), ImVec2(c.x + 12 * u, c.y + 9 * u), IM_COL32(225, 228, 236, 255));
            dl->AddCircleFilled(ImVec2(c.x - 4 * u, c.y), 2.2f * u, IM_COL32(26, 27, 31, 255), 10);
            dl->AddCircleFilled(ImVec2(c.x + 4 * u, c.y), 2.2f * u, IM_COL32(26, 27, 31, 255), 10);
        }
        std::vector<std::string> acc = AuthAccounts();
        const std::string who = !s_AuthUser.empty() ? s_AuthUser : (!acc.empty() ? acc[0] : std::string());
        const bool linked = !who.empty() || !s_Auth.discordName.empty();
        NeonText(dl, cf, 8.5f * u, ImVec2(a.x + 59 * u, a.y + 11 * u), IM_COL32(150, 152, 158, 255), "Neon Identity", 1.2f * u);
        NeonText(dl, cf, 14 * u, ImVec2(a.x + 59 * u, a.y + 24 * u), kNText, linked ? "Discord connected" : "Discord not connected", 1.6f * u);
        char sub[96];
        if(linked) snprintf(sub, sizeof(sub), "Neon account connected%s%s", who.empty() ? "" : " - ", who.c_str());
        else snprintf(sub, sizeof(sub), "Log in on a server to link your Discord");
        dl->AddCircleFilled(ImVec2(a.x + 62 * u, a.y + 49 * u), 2.2f * u, linked ? IM_COL32(110, 200, 120, 255) : IM_COL32(120, 120, 126, 255), 8);
        NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 70 * u, a.y + 44 * u), IM_COL32(140, 142, 148, 255), sub, 1.0f * u);
        if(linked)
        {
            NeonTextR(dl, cf, 7.5f * u, ImVec2(b.x - 22 * u, a.y + 43 * u), IM_COL32(150, 152, 158, 255), "Sign out", 1.2f * u);
            dl->AddCircleFilled(ImVec2(b.x - 15 * u, a.y + 47 * u), 1.2f * u, IM_COL32(150, 152, 158, 255), 6);
            if(NeonHit("##niOut", ImVec2(b.x - 110 * u, a.y + 34 * u), b))
            {
                if(!who.empty()) AuthAccountsRemove(who);
                AuthLogout(nullptr, false);
                logger->Info("Neon menu: signed out");
            }
        }
    }
    {   // NEON FEATURED (bottom left)
        static LRow feat; static bool haveFeat = false; static uint64_t at = 0;
        if(MP_NowMs() - at > 3000) { at = MP_NowMs(); haveFeat = NeonBestRow(feat); }
        const ImVec2 a(L - 8 * u, 643 * u), b(L + 379 * u, 723 * u);
        dl->AddRectFilled(a, b, IM_COL32(12, 12, 14, 232), 2 * u);
        if(haveFeat)
        {   // the server's own art, faint, on the right
            ImVec2 uv(1, 1); void* t = LRowBg(feat, &uv);
            if(t) { dl->PushClipRect(ImVec2(a.x + 220 * u, a.y), b, true); LImageCover(dl, t, uv, ImVec2(a.x + 220 * u, a.y), b, IM_COL32(255, 255, 255, 40)); dl->PopClipRect(); }
        }
        dl->AddRect(a, b, IM_COL32(64, 60, 52, 255), 2 * u, 0, 1.0f * u);
        dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(a.x + 2.5f * u, b.y), IM_COL32(184, 154, 90, 255));
        if(haveFeat)
        {
            NeonRowIcon(dl, feat, ImVec2(a.x + 11 * u, a.y + 13 * u), 54 * u);
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 77 * u, a.y + 23 * u), IM_COL32(150, 152, 158, 255), "Neon featured", 1.2f * u);
            char nm[64]; MP_CopyStr(nm, feat.name, sizeof(nm));
            dl->PushClipRect(a, ImVec2(b.x - 6 * u, b.y), true);
            NeonText(dl, cf, 13.5f * u, ImVec2(a.x + 77 * u, a.y + 34 * u), kNText, nm, 1.8f * u, true);
            char d[96]; snprintf(d, sizeof(d), "%s - %d / %d players", feat.mode[0] ? feat.mode : "Freeroam", feat.players < 0 ? 0 : feat.players, feat.maxp > 0 ? feat.maxp : 0);
            NeonText(dl, uf, 9 * u, ImVec2(a.x + 77 * u, a.y + 52 * u), IM_COL32(170, 172, 178, 255), d);
            NeonText(dl, cf, 7.5f * u, ImVec2(a.x + 77 * u, a.y + 64 * u), IM_COL32(200, 202, 208, 255), "Play now", 1.2f * u);
            dl->AddCircleFilled(ImVec2(a.x + 77 * u + NeonTextW(cf, 7.5f * u, "Play now", 1.2f * u) + 7 * u, a.y + 69 * u), 1.2f * u, IM_COL32(200, 202, 208, 255), 6);
            dl->PopClipRect();
            if(NeonHit("##nfPlay", a, b)) NeonJoinRow(feat);
        }
        else NeonText(dl, cf, 9 * u, ImVec2(a.x + 20 * u, a.y + 34 * u), IM_COL32(150, 152, 158, 255), "Looking for featured servers...", 1.2f * u);
    }
    {   // version + hints + language
        char v[64]; snprintf(v, sizeof(v), "V%s \xE2\x80\x94 NEON PREVIEW", MP_VERSION_STR);
        NeonText(dl, uf, 9.5f * u, ImVec2(L - 8 * u, 761 * u), IM_COL32(100, 104, 112, 255), v, 2.6f * u);
        const float hy = 761 * u; float hx = ds.x * (998.0f / 1430.0f);
        NeonTri(dl, ImVec2(hx + 2 * u, hy + 4 * u), 2.4f * u, IM_COL32(120, 124, 132, 255), 2);
        NeonTri(dl, ImVec2(hx + 8 * u, hy + 8 * u), 2.4f * u, IM_COL32(120, 124, 132, 255), 1);
        NeonText(dl, uf, 9 * u, ImVec2(hx + 16 * u, hy), IM_COL32(120, 124, 132, 255), "SELECT", 2.4f * u);
        NeonText(dl, uf, 9 * u, ImVec2(hx + 80 * u, hy), IM_COL32(170, 172, 178, 255), "ENTER", 2.0f * u);
        NeonText(dl, uf, 9 * u, ImVec2(hx + 80 * u + NeonTextW(uf, 9 * u, "ENTER", 2.0f * u) + 7 * u, hy), IM_COL32(120, 124, 132, 255), "CONFIRM", 2.4f * u);
        const char* ln = NeonLangName();
        const float lx = ds.x - 160 * u;
        dl->AddRect(ImVec2(lx, hy - 3 * u), ImVec2(lx + 15 * u, hy + 13 * u), IM_COL32(200, 202, 208, 255), 2 * u, 0, 1.1f * u);
        NeonText(dl, uf, 8 * u, ImVec2(lx + 3 * u, hy - 1 * u), IM_COL32(200, 202, 208, 255), "A");
        NeonText(dl, uf, 9.5f * u, ImVec2(lx + 24 * u, hy), IM_COL32(220, 222, 228, 255), ln, 1.6f * u);
        NeonTri(dl, ImVec2(lx + 32 * u + NeonTextW(uf, 9.5f * u, ln, 1.6f * u), hy + 5 * u), 2.2f * u, IM_COL32(200, 202, 208, 255), 1);
        if(NeonHit("##nLang", ImVec2(lx - 6 * u, hy - 10 * u), ImVec2(ds.x - 10 * u, hy + 22 * u)))
        {
            s_nNeonPage = NP_SETTINGS;
            for(int i = 0; i < FloatMenu::SectionCount(); ++i) if(!strcasecmp(FloatMenu::SectionTitle(i), "Language")) s_nNeonSetSec = i;
        }
    }
}

// HUD, every frame (DrawMenuMpFront): true when the neon menu took the screen
static bool NeonDraw()
{
    if(!NeonOn()) return false;
    const bool title = NeonTitleCtx();
    const bool black = MenuMpBlack() && s_nMenuMpPct.load() < 0 && client.GetState() == NET_DISCONNECTED && !FloatMenu::IsModal() && !JoinScreenUp();
    if(!title && !black) { if(!Game::IsMainMenuShown()) s_bNeonHidden = false; return false; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(s_bNeonHidden && title)
    {   // the game's own menu (Game settings): a small way back to the neon menu
        const float u = ds.y / 804.0f;
        const ImVec2 sz(230 * u, 46 * u), at(ds.x * 0.02f, ds.y - sz.y - 20 * u);
        ImGui::SetNextWindowPos(at); ImGui::SetNextWindowSize(sz);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if(ImGui::Begin("##NeonBack", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(at, ImVec2(at.x + sz.x, at.y + sz.y), IM_COL32(10, 10, 12, 230), 5 * u);
            dl->AddRect(at, ImVec2(at.x + sz.x, at.y + sz.y), kNGold, 5 * u, 0, 1.4f * u);
            NeonText(dl, UI::Font(), 11 * u, ImVec2(at.x + 20 * u, at.y + 15 * u), kNText, "NEON MENU", 3.0f * u);
            if(NeonHit("##nbk", at, ImVec2(at.x + sz.x, at.y + sz.y))) s_bNeonHidden = false;
        }
        ImGui::End(); ImGui::PopStyleVar();
        return true;
    }
    if(black) s_bNeonHidden = false;
    ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    if(ImGui::Begin("##NeonMenu", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus))
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        NeonBackdrop(dl, ds, s_nNeonPage == NP_MAIN || s_nNeonPage >= NP_ABOUT ? 70 : 200);
        if(s_nNeonPage == NP_BROWSER) NeonBrowser(dl, ds);
        else if(s_nNeonPage == NP_SETTINGS) NeonSettings(dl, ds);
        else
        {
            NeonMain(dl, ds);
            if(s_nNeonPage == NP_ABOUT)
            {
                char body[400];
                snprintf(body, sizeof(body), "MP Multiplayer %s for GTA San Andreas (Android, AML).\nby ThirtyFoxMCx00E1 - github.com/ThirtyFoxMCx00E1/wagu\nDiscord: %s\n\n"
                         "Neon menu art: San Andreas fan art (GTA V style). Fonts: UnifrakturMaguntia, Cinzel (SIL OFL 1.1).", MP_VERSION_STR, MP_AUTH_DISCORD_URL);
                bool y = false, n = false; NeonDialog(dl, ds, "About", body, "CLOSE", nullptr, &y, &n);
                if(y) s_nNeonPage = NP_MAIN;
            }
            else if(s_nNeonPage == NP_QUIT)
            {
                bool y = false, n = false;
                NeonDialog(dl, ds, "Quit game", "Close GTA San Andreas now? Unsaved progress is lost.", "QUIT", "CANCEL", &y, &n);
                if(n) s_nNeonPage = NP_MAIN;
                if(y) { logger->Info("Neon menu: Quit game"); MP::SaveConfig(); exit(0); }
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(); ImGui::PopStyleVar(2);
    return true;
}

// game thread (MenuMpProcess): the black screen is up after a join from the title menu
static bool NeonPendingJoin()
{
    if(!s_bNeonPendingJoin) return false;
    s_bNeonPendingJoin = false;
    logger->Info("Neon menu: the Multiplayer load is done -> join %s", s_NeonPending.name);
    LJoin(s_NeonPending);
    return true;
}
