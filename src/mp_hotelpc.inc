// MP 3.36 - hotel rooms with a computer (included by mp.cpp after mp_rp_life.inc). On EVERY server
// (free roam, races, Mega Vil...):
//   - at a hotel door (Jefferson Motel, Four Dragons, Caligula's + files/MP/hotels.txt) "Hotel room"
//     takes you into a motel room (interior 10) with a desk + computer in it
//   - walk up to the computer: a round computer button shows; tap it = the eyefind.info browser
//     (like the GTA one): Southern San Andreas Super Autos, Legendary Motorsport, Pedal and Metal,
//     Elitas Travel, Docktease, and a search box over all of them
//   - a site = a grid of white cards (picture from files/MP/Vehicles.jar, name, red price box), scroll
//     with a drag; tap a card = BUY. You walk out of the hotel and the vehicle is waiting for you.
//     Mega Vil: paid from cash + bank, kept in your garage (RPC_BUY a = 2). Other servers: your $.
// files/MP/hotels.txt (optional, one per line): name x y z   (the door outside, more hotels)

#define HPC_AREA 10
static const Vec3 kHpcRoom = { 2262.83f, -1137.71f, 1050.63f };   // Red Bed Motel room (interior 10), ped centre
static const int  kHpcDeskModel = 2165;   // office desk with a computer on it
struct HpcHotel { char name[40]; Vec3 door; };
static std::vector<HpcHotel> s_HpcHotels;
static const char* const kHpcSites[5]    = { "Southern San Andreas Super Autos", "Legendary Motorsport", "Pedal and Metal", "Elitas Travel", "Docktease" };
static const char* const kHpcSiteUrl[5]  = { "www.southernsanandreassuperautos.com", "www.legendarymotorsport.net", "www.pedalandmetal.com", "www.elitastravel.com", "www.docktease.com" };
static const char* const kHpcSiteSub[5]  = { "Cars, vans and trucks for every budget", "Sports and luxury cars", "Motorbikes, quads and bicycles", "Planes and helicopters", "Boats and yachts" };
static const ImU32 kHpcSiteCol[5] = { IM_COL32(18, 92, 170, 255), IM_COL32(12, 12, 12, 255), IM_COL32(200, 110, 20, 255), IM_COL32(20, 120, 110, 255), IM_COL32(16, 70, 120, 255) };

struct HpcState
{
    int      phase = 0;          // 0 outside, 1 going in (waiting for the floor), 2 in the room
    int      hotel = -1;         // the door I came in through
    uint64_t at = 0, leftAreaAt = 0;
    float    floorZ = -1000.0f;
    int      desk = 0; bool deskDone = false; Vec3 deskPos{}; float deskRz = 0;
    bool     open = false; int page = -1;   // -1 eyefind home, 0..4 a site, 5 search results
    float    scroll = 0; int pick = -1;     // index into g_HotelVeh (confirm box)
    char     search[32] = "";
    uint64_t openAt = 0;
    int      spawnModel = 0; uint64_t spawnAt = 0;   // other servers: the vehicle when I am out
    char     msg[96] = ""; uint64_t msgAt = 0;
};
static HpcState s_Hpc;

static void HpcLoadHotels()
{
    if(!s_HpcHotels.empty()) return;
    static const HpcHotel kBuiltIn[] = {
        { "Jefferson Motel",        { 2233.08f, -1159.83f, 25.89f } },
        { "Four Dragons Casino",    { 2019.32f,  1007.81f, 10.82f } },
        { "Caligula's Palace",      { 2196.96f,  1677.18f, 12.37f } },
    };
    for(const HpcHotel& h : kBuiltIn) s_HpcHotels.push_back(h);
    char path[512]; snprintf(path, sizeof(path), "%s/hotels.txt", MP::DataDir());
    FILE* f = fopen(path, "r");
    if(!f) return;
    char line[160];
    while(fgets(line, sizeof(line), f))
    {
        if(line[0] == '#' || line[0] == '\n') continue;
        // the name may have spaces: the last 3 numbers are x y z
        float x, y, z; char* e = line + strlen(line);
        while(e > line && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ')) *--e = 0;
        char* p3 = strrchr(line, ' '); if(!p3) continue; *p3 = 0;
        char* p2 = strrchr(line, ' '); if(!p2) continue; *p2 = 0;
        char* p1 = strrchr(line, ' '); if(!p1) continue; *p1 = 0;
        x = (float)atof(p1 + 1); y = (float)atof(p2 + 1); z = (float)atof(p3 + 1);
        if(!line[0] || (x == 0 && y == 0)) continue;
        HpcHotel h{}; MP_CopyStr(h.name, line, sizeof(h.name)); h.door = { x, y, z };
        s_HpcHotels.push_back(h);
    }
    fclose(f);
    logger->Info("3.36: %d hotels (files/MP/hotels.txt)", (int)s_HpcHotels.size());
}
static void HpcSay(const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Hpc.msg, sizeof(s_Hpc.msg), fmt, ap); va_end(ap);
    s_Hpc.msgAt = MP_NowMs();
}
static int HpcMyMoney()
{
    if(RpOnServer() && s_Rp.haveMe) return s_Rp.me.cash + s_Rp.me.bank;
    return Game::GetMoney();
}
// the hotel UI only where nothing else owns the screen
static bool HpcAllowed()
{
    if(JoinFlowActive() || MP::AuthScreenShown() || FloatMenu::IsModal() || Game::IsPaused()) return false;
    if(RpOnServer() && !(s_Rp.engaged && s_Rp.view == 2)) return false;   // Mega Vil lobby
    if(RpCosActive()) return false;
    return true;
}
static void HpcFreeDesk()
{
    if(s_Hpc.desk > 0) Game::DeleteWorldObject(s_Hpc.desk);
    s_Hpc.desk = 0; s_Hpc.deskDone = false;
}
static void HpcClosePc()
{
    if(!s_Hpc.open) return;
    s_Hpc.open = false; s_Hpc.pick = -1;
    HUD::FreeVehiclePics();
}
static void HpcLeave(bool teleport)
{
    int ped = Game::LocalPed();
    HpcClosePc();
    HpcFreeDesk();
    if(teleport && ped > 0)
    {
        Game::FreezeChar(ped, false);
        Game::EnterArea(0);
        Vec3 d = s_Hpc.hotel >= 0 && s_Hpc.hotel < (int)s_HpcHotels.size() ? s_HpcHotels[s_Hpc.hotel].door : Vec3{ 2233.08f, -1159.83f, 25.89f };
        Game::PreloadArea(d, true);
        Game::TeleportGround(ped, d, Game::GetCharHeading(ped));
    }
    s_Hpc.phase = 0; s_Hpc.at = MP_NowMs(); s_Hpc.floorZ = -1000.0f;
}
static void HpcEnter(int hotel)
{
    int ped = Game::LocalPed();
    if(ped <= 0 || Game::IsCharInAnyCar(ped)) return;
    RpClick(RPS_CLICK);
    s_Hpc.hotel = hotel; s_Hpc.phase = 1; s_Hpc.at = MP_NowMs(); s_Hpc.floorZ = -1000.0f; s_Hpc.leftAreaAt = 0;
    Game::EnterArea(HPC_AREA);
    Game::PreloadArea(kHpcRoom, true);
    Game::PlaceChar(ped, { kHpcRoom.x, kHpcRoom.y, kHpcRoom.z - 1.0f });
    Game::FreezeChar(ped, true);   // held until the floor collision is in (no fall through it)
}
// the desk: in a free direction from where I stand (the room walls are not known here)
static void HpcMakeDesk()
{
    if(s_Hpc.deskDone) return;
    if(s_Hpc.desk == 0)
    {
        Vec3 chest{ kHpcRoom.x, kHpcRoom.y, s_Hpc.floorZ + 1.2f };
        float best = 0; bool found = false;
        for(int i = 0; i < 8 && !found; ++i)
        {
            float a = i * 0.785398f, dx = cosf(a), dy = sinf(a);
            Vec3 far{ chest.x + dx * 2.0f, chest.y + dy * 2.0f, chest.z };
            if(Game::LineOfSightClear(chest, far)) { best = a; found = true; }
        }
        s_Hpc.deskPos = { kHpcRoom.x + cosf(best) * 1.35f, kHpcRoom.y + sinf(best) * 1.35f, s_Hpc.floorZ };
        s_Hpc.deskRz = best * 57.29578f + 90.0f;   // the long side faces me
    }
    int o = Game::CreateMapObject(kHpcDeskModel, s_Hpc.deskPos, 0, 0, s_Hpc.deskRz);
    if(o == 0) return;   // model loading: next tick
    s_Hpc.desk = o > 0 ? o : 0; s_Hpc.deskDone = true;
}

static void ProcessHotelPc(int ped, uint64_t now)
{
    HpcLoadHotels();
    if(ped <= 0) return;
    if(client.GetState() != NET_CONNECTED)
    {   // left the server: out of the room (the server is gone, so is the room)
        if(s_Hpc.phase != 0) HpcLeave(true);
        s_Hpc.spawnModel = 0;
        return;
    }
    if(s_Hpc.spawnModel && now >= s_Hpc.spawnAt && s_Hpc.phase == 0)
    {   // other servers: the vehicle I bought, now that I am outside
        int m = s_Hpc.spawnModel; s_Hpc.spawnModel = 0;
        if(SpawnVehicleForMe(m, false)) SysMsg(COL_SYSTEM, "[EYEFIND] Your new %s is here.", kVehNames[m - 400]);
    }
    if(s_Hpc.phase == 0) return;
    if(Game::IsCharDead(ped)) { HpcLeave(false); return; }
    if(s_Hpc.phase == 1)
    {
        Game::PreloadArea(kHpcRoom, false);
        float f = Game::GroundBelow(kHpcRoom.x, kHpcRoom.y, kHpcRoom.z + 0.5f);
        if(f > -999.0f && fabsf(f - (kHpcRoom.z - 1.0f)) < 2.5f)
        {
            s_Hpc.floorZ = f;
            Game::PlaceChar(ped, { kHpcRoom.x, kHpcRoom.y, f + 0.02f });   // feet on the floor
            Game::ClearTasksNow(ped);
            Game::FreezeChar(ped, false);
            s_Hpc.phase = 2; s_Hpc.at = now;
            HpcSay("Welcome to your room. The computer is on the desk.");
        }
        else if(now - s_Hpc.at > 6000) { HpcLeave(true); SysMsg(COL_YELLOW, "The hotel room did not load - try again."); }
        return;
    }
    // in the room
    HpcMakeDesk();
    if(Game::GetAreaVisible() != HPC_AREA)
    {   // somebody / something moved me out (spawn, teleport, a race): forget the room
        if(!s_Hpc.leftAreaAt) s_Hpc.leftAreaAt = now;
        else if(now - s_Hpc.leftAreaAt > 1500) { HpcLeave(false); return; }
    }
    else s_Hpc.leftAreaAt = 0;
    Vec3 p; if(!Game::GetCharPos(ped, p)) return;
    if(p.z < s_Hpc.floorZ - 3.0f) { Game::PlaceChar(ped, { kHpcRoom.x, kHpcRoom.y, s_Hpc.floorZ + 0.02f }); }   // fell through: back on the floor
}

// ---- drawing ------------------------------------------------------------------------------------
static bool HpcHit(const char* id, ImVec2 a, ImVec2 b)
{
    ImGui::SetCursorScreenPos(a);
    bool r = ImGui::InvisibleButton(id, ImVec2(b.x - a.x > 1 ? b.x - a.x : 1, b.y - a.y > 1 ? b.y - a.y : 1));
    if(r) RpClick(RPS_CLICK);
    return r;
}
// a round button with a little computer drawn in it
static bool HpcPcButton(ImVec2 c, float r)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
    bool hit = ImGui::InvisibleButton("##hpcbtn", ImVec2(r * 2, r * 2));
    bool down = ImGui::IsItemActive();
    dl->AddCircleFilled(c, r, down ? IM_COL32(30, 110, 200, 240) : IM_COL32(12, 20, 34, 225), 40);
    dl->AddCircle(c, r, IM_COL32(56, 189, 248, 255), 40, UI::S(3));
    float s = r * 0.5f;
    ImVec2 ma(c.x - s, c.y - s * 0.75f), mb(c.x + s, c.y + s * 0.45f);
    dl->AddRectFilled(ma, mb, IM_COL32(230, 240, 250, 255), UI::S(3));
    dl->AddRectFilled(ImVec2(ma.x + UI::S(3), ma.y + UI::S(3)), ImVec2(mb.x - UI::S(3), mb.y - UI::S(3)), IM_COL32(40, 120, 220, 255), UI::S(2));
    dl->AddRectFilled(ImVec2(c.x - s * 0.15f, mb.y), ImVec2(c.x + s * 0.15f, mb.y + s * 0.3f), IM_COL32(230, 240, 250, 255));
    dl->AddRectFilled(ImVec2(c.x - s * 0.55f, mb.y + s * 0.3f), ImVec2(c.x + s * 0.55f, mb.y + s * 0.42f), IM_COL32(230, 240, 250, 255), UI::S(2));
    if(hit) RpClick(RPS_CLICK);
    return hit;
}
static void HpcSearchKb(const char* t, void*)
{
    MP_CopyStr(s_Hpc.search, t ? t : "", sizeof(s_Hpc.search));
    s_Hpc.page = 5; s_Hpc.scroll = 0; s_Hpc.pick = -1;
}
static bool HpcMatch(int i)
{
    const HotelVeh& v = g_HotelVeh[i];
    if(s_Hpc.page >= 0 && s_Hpc.page < 5) return v.site == s_Hpc.page;
    if(s_Hpc.page != 5) return false;
    if(!s_Hpc.search[0]) return true;
    return strcasestr(kVehNames[v.model - 400], s_Hpc.search) != nullptr || strcasestr(kHpcSites[v.site], s_Hpc.search) != nullptr;
}
static void HpcBuy(int i)
{
    const HotelVeh& v = g_HotelVeh[i];
    char mb[24];
    if(HpcMyMoney() < v.price) { HpcSay("Not enough money: the %s costs %s.", kVehNames[v.model - 400], AtmMoney(v.price, mb, sizeof(mb))); return; }
    if(RpOnServer())
    {
        if(s_Rp.haveMe && s_Rp.me.nveh >= RP_VEH_SLOTS) { HpcSay("Your garage is full - sell one in the Garage app first."); return; }
        HpcLeave(true);                               // out of the hotel first: the car comes to the door
        RpSend(RPC_BUY, 2, 0, v.model);               // the server takes the money + spawns it (RPV_VEHICLE)
    }
    else
    {
        Game::AddMoney(-v.price);
        HpcLeave(true);
        s_Hpc.spawnModel = v.model; s_Hpc.spawnAt = MP_NowMs() + 900;   // when the street is loaded round me
        SysMsg(COL_SYSTEM, "[EYEFIND] You bought a %s for %s.", kVehNames[v.model - 400], AtmMoney(v.price, mb, sizeof(mb)));
    }
}

// the browser: eyefind home, a site (card grid), the confirm box
static void HpcDrawBrowser(uint64_t now)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 ds = io.DisplaySize;
    float k = (now - s_Hpc.openAt) / 160.0f; if(k > 1) k = 1;
    float W = ds.x * 0.86f, H = ds.y * 0.86f;
    if(W > UI::S(1500)) W = UI::S(1500);
    ImVec2 a((ds.x - W) * 0.5f, (ds.y - H) * 0.5f + (1 - k) * UI::S(20)), b(a.x + W, a.y + H);
    BcwBeginOverlay("##HotelPc", ImVec2(0, 0), ds, 0.55f * k);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char t[128], mb[24];
    // the window + address bar
    dl->AddRectFilled(ImVec2(a.x - UI::S(6), a.y - UI::S(6)), ImVec2(b.x + UI::S(6), b.y + UI::S(6)), IM_COL32(30, 30, 34, 255), UI::S(10));
    dl->AddRectFilled(a, b, IM_COL32(236, 238, 241, 255), UI::S(6));
    const float barH = UI::S(56);
    dl->AddRectFilled(a, ImVec2(b.x, a.y + barH), IM_COL32(52, 54, 60, 255), UI::S(6), ImDrawFlags_RoundCornersTop);
    ImVec2 backA(a.x + UI::S(10), a.y + UI::S(9)), backB(backA.x + UI::S(56), a.y + barH - UI::S(9));
    bool canBack = s_Hpc.page != -1 || s_Hpc.pick >= 0;
    dl->AddRectFilled(backA, backB, canBack ? IM_COL32(80, 84, 92, 255) : IM_COL32(64, 66, 72, 255), UI::S(6));
    RpCenterIn(dl, UI::S(22), backA, backB, canBack ? IM_COL32_WHITE : IM_COL32(130, 130, 136, 255), "<");
    ImVec2 xA(b.x - UI::S(66), backA.y), xB(b.x - UI::S(10), backB.y);
    dl->AddRectFilled(xA, xB, IM_COL32(200, 50, 50, 255), UI::S(6));
    RpCenterIn(dl, UI::S(20), xA, xB, IM_COL32_WHITE, "X");
    ImVec2 urlA(backB.x + UI::S(12), backA.y), urlB(xA.x - UI::S(12), backB.y);
    dl->AddRectFilled(urlA, urlB, IM_COL32(250, 250, 252, 255), UI::S(14));
    const char* url = s_Hpc.page >= 0 && s_Hpc.page < 5 ? kHpcSiteUrl[s_Hpc.page] : "www.eyefind.info";
    if(s_Hpc.page == 5) { snprintf(t, sizeof(t), "www.eyefind.info/search?q=%s", s_Hpc.search); url = t; }
    dl->PushClipRect(urlA, urlB, true);
    RpPlainText(dl, UI::S(17), ImVec2(urlA.x + UI::S(16), (urlA.y + urlB.y) * 0.5f - UI::S(9)), IM_COL32(60, 64, 72, 255), url);
    dl->PopClipRect();
    {
        int money = HpcMyMoney();
        snprintf(t, sizeof(t), "%s", AtmMoney(money, mb, sizeof(mb)));
        float tw = RpTextW(UI::S(17), t);
        dl->AddRectFilled(ImVec2(urlB.x - tw - UI::S(24), urlA.y + UI::S(5)), ImVec2(urlB.x - UI::S(6), urlB.y - UI::S(5)), IM_COL32(30, 150, 70, 255), UI::S(10));
        RpPlainText(dl, UI::S(17), ImVec2(urlB.x - tw - UI::S(15), (urlA.y + urlB.y) * 0.5f - UI::S(9)), IM_COL32_WHITE, t);
    }
    bool close = HpcHit("##hpcx", xA, xB);
    if(HpcHit("##hpcback", backA, backB) && canBack)
    {
        if(s_Hpc.pick >= 0) s_Hpc.pick = -1;
        else { s_Hpc.page = -1; s_Hpc.scroll = 0; }
    }
    ImVec2 ca(a.x, a.y + barH), cb = b;   // page area
    if(s_Hpc.page == -1)
    {   // ---- eyefind.info home ----
        const float hh = UI::S(120);
        dl->AddRectFilled(ca, ImVec2(cb.x, ca.y + hh), IM_COL32(32, 120, 206, 255));
        float lw = RpTextW(UI::S(54), "eyefind") + RpTextW(UI::S(30), ".info");
        float lx = (ca.x + cb.x - lw) * 0.5f;
        RpPlainText(dl, UI::S(54), ImVec2(lx, ca.y + UI::S(14)), IM_COL32_WHITE, "eyefind");
        RpPlainText(dl, UI::S(30), ImVec2(lx + RpTextW(UI::S(54), "eyefind"), ca.y + UI::S(36)), IM_COL32(200, 228, 255, 255), ".info");
        // the search box
        float sw = W * 0.56f; ImVec2 sa((ca.x + cb.x - sw) * 0.5f, ca.y + hh - UI::S(26)), sb(sa.x + sw, sa.y + UI::S(52));
        dl->AddRectFilled(sa, sb, IM_COL32_WHITE, UI::S(26));
        dl->AddRect(sa, sb, IM_COL32(180, 190, 205, 255), UI::S(26), 0, UI::S(2));
        RpPlainText(dl, UI::S(19), ImVec2(sa.x + UI::S(24), sa.y + UI::S(15)), IM_COL32(130, 136, 146, 255), "Search vehicles (name or site)...");
        ImVec2 ga(sb.x - UI::S(110), sa.y + UI::S(6)), gb(sb.x - UI::S(8), sb.y - UI::S(6));
        dl->AddRectFilled(ga, gb, IM_COL32(32, 120, 206, 255), UI::S(20));
        RpCenterIn(dl, UI::S(18), ga, gb, IM_COL32_WHITE, "SEARCH");
        if(HpcHit("##hpcsearch", sa, sb)) VKeyboard::Open("eyefind.info search", s_Hpc.search, 24, HpcSearchKb, nullptr, false);
        // the sites
        float top = sb.y + UI::S(28), rowH = (cb.y - top - UI::S(20)) / 5.0f; if(rowH > UI::S(92)) rowH = UI::S(92);
        float rw = W * 0.74f, rx = (ca.x + cb.x - rw) * 0.5f;
        for(int i = 0; i < 5; ++i)
        {
            ImVec2 ra(rx, top + i * rowH), rb(rx + rw, top + (i + 1) * rowH - UI::S(10));
            dl->AddRectFilled(ra, rb, IM_COL32_WHITE, UI::S(8));
            dl->AddRectFilled(ra, ImVec2(ra.x + UI::S(150), rb.y), kHpcSiteCol[i], UI::S(8), ImDrawFlags_RoundCornersLeft);
            static const char* const kTag[5] = { "SSASA", "LEGENDARY", "P&M", "ELITAS", "DOCKTEASE" };
            RpCenterIn(dl, UI::S(18), ra, ImVec2(ra.x + UI::S(150), rb.y), i == 1 ? IM_COL32(230, 40, 40, 255) : IM_COL32_WHITE, kTag[i]);
            float ty = ra.y + (rb.y - ra.y) * 0.5f - UI::S(20);
            RpPlainText(dl, UI::S(21), ImVec2(ra.x + UI::S(170), ty), IM_COL32(26, 80, 160, 255), kHpcSites[i]);
            int n = 0; for(int j = 0; j < HOTEL_VEH_COUNT; ++j) if(g_HotelVeh[j].site == i) ++n;
            snprintf(t, sizeof(t), "%s  -  %s  (%d vehicles)", kHpcSiteUrl[i], kHpcSiteSub[i], n);
            dl->PushClipRect(ra, rb, true);
            RpPlainText(dl, UI::S(15), ImVec2(ra.x + UI::S(170), ty + UI::S(26)), IM_COL32(40, 140, 60, 255), t);
            dl->PopClipRect();
            snprintf(t, sizeof(t), "##hpcsite%d", i);
            if(HpcHit(t, ra, rb)) { s_Hpc.page = i; s_Hpc.scroll = 0; s_Hpc.pick = -1; }
        }
    }
    else
    {   // ---- a site / the search results: header + a grid of cards ----
        const float hh = UI::S(96);
        int site = s_Hpc.page < 5 ? s_Hpc.page : -1;
        ImU32 hc = site >= 0 ? kHpcSiteCol[site] : IM_COL32(32, 120, 206, 255);
        dl->AddRectFilled(ca, ImVec2(cb.x, ca.y + hh), hc);
        if(site == 1)
        {   // Legendary Motorsport: black with the red logo
            float lw = RpTextW(UI::S(40), "LEGENDARY"), lx = ca.x + UI::S(30);
            RpPlainText(dl, UI::S(40), ImVec2(lx, ca.y + UI::S(10)), IM_COL32(230, 40, 40, 255), "LEGENDARY");
            RpPlainText(dl, UI::S(22), ImVec2(lx + UI::S(4), ca.y + UI::S(56)), IM_COL32_WHITE, "MOTORSPORT");
            dl->AddLine(ImVec2(lx + lw + UI::S(20), ca.y + UI::S(20)), ImVec2(lx + lw + UI::S(20), ca.y + hh - UI::S(20)), IM_COL32(120, 120, 120, 255), UI::S(2));
            RpPlainText(dl, UI::S(18), ImVec2(lx + lw + UI::S(36), ca.y + UI::S(38)), IM_COL32(200, 200, 200, 255), "Exclusive cars for exclusive people");
        }
        else
        {
            const char* title = site >= 0 ? kHpcSites[site] : "Search results";
            RpPlainText(dl, UI::S(34), ImVec2(ca.x + UI::S(30), ca.y + UI::S(14)), IM_COL32_WHITE, title);
            if(site >= 0) RpPlainText(dl, UI::S(17), ImVec2(ca.x + UI::S(32), ca.y + UI::S(58)), IM_COL32(230, 236, 245, 255), kHpcSiteSub[site]);
            else { snprintf(t, sizeof(t), "\"%s\" - tap to search again", s_Hpc.search); RpPlainText(dl, UI::S(17), ImVec2(ca.x + UI::S(32), ca.y + UI::S(58)), IM_COL32(230, 236, 245, 255), t);
                   if(HpcHit("##hpcresearch", ca, ImVec2(cb.x, ca.y + hh))) VKeyboard::Open("eyefind.info search", s_Hpc.search, 24, HpcSearchKb, nullptr, false); }
        }
        // the grid
        ImVec2 va(ca.x + UI::S(20), ca.y + hh + UI::S(16)), vb(cb.x - UI::S(20), cb.y - UI::S(12));
        const int COLS = 3; const float gap = UI::S(18);
        const float cw = (vb.x - va.x - gap * (COLS - 1) - UI::S(10)) / COLS, ph = cw * 125.0f / 204.0f, chh = ph + UI::S(52);
        int list[HOTEL_VEH_COUNT]; int n = 0;
        for(int i = 0; i < HOTEL_VEH_COUNT; ++i) if(HpcMatch(i)) list[n++] = i;
        const int rows = (n + COLS - 1) / COLS;
        const float contentH = rows * chh + (rows > 0 ? (rows - 1) * gap : 0), viewH = vb.y - va.y, maxS = contentH > viewH ? contentH - viewH : 0;
        bool tap = false;
        if(s_Hpc.pick < 0)
        {
            ImGui::SetCursorScreenPos(va);
            tap = ImGui::InvisibleButton("##hpcgrid", ImVec2(vb.x - va.x, vb.y - va.y));
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0, UI::S(6))) s_Hpc.scroll -= io.MouseDelta.y;
            if(ImGui::IsItemHovered() && io.MouseWheel != 0) s_Hpc.scroll -= io.MouseWheel * (chh + gap);
        }
        if(s_Hpc.scroll < 0) s_Hpc.scroll = 0;
        if(s_Hpc.scroll > maxS) s_Hpc.scroll = maxS;
        if(tap && io.MouseDragMaxDistanceSqr[0] < UI::S(12) * UI::S(12))
        {
            float lx = io.MousePos.x - va.x, ly = io.MousePos.y - va.y + s_Hpc.scroll;
            int c = (int)(lx / (cw + gap)), r = (int)(ly / (chh + gap)), i = r * COLS + c;
            if(lx >= 0 && ly >= 0 && c < COLS && lx - c * (cw + gap) <= cw && ly - r * (chh + gap) <= chh && i < n) { s_Hpc.pick = list[i]; RpClick(RPS_CLICK); }
        }
        dl->PushClipRect(va, ImVec2(vb.x + UI::S(10), vb.y), true);
        for(int i = 0; i < n; ++i)
        {
            int c = i % COLS, r = i / COLS;
            ImVec2 xa(va.x + c * (cw + gap), va.y + r * (chh + gap) - s_Hpc.scroll), xb(xa.x + cw, xa.y + chh);
            if(xb.y < va.y || xa.y > vb.y) continue;
            const HotelVeh& v = g_HotelVeh[list[i]];
            dl->AddRectFilled(ImVec2(xa.x + UI::S(2), xa.y + UI::S(3)), ImVec2(xb.x + UI::S(2), xb.y + UI::S(3)), IM_COL32(0, 0, 0, 40), UI::S(4));
            dl->AddRectFilled(xa, xb, IM_COL32_WHITE, UI::S(4));
            ImVec2 pa(xa.x + UI::S(6), xa.y + UI::S(6)), pb(xb.x - UI::S(6), xa.y + ph);
            ImVec2 uv; void* tex = HUD::VehiclePic(v.model, &uv);
            if(tex) dl->AddImage((ImTextureID)tex, pa, pb, ImVec2(0, 0), uv);
            else { dl->AddRectFilled(pa, pb, IM_COL32(220, 224, 230, 255)); RpCenterIn(dl, UI::S(15), pa, pb, IM_COL32(140, 146, 156, 255), "loading..."); }
            const char* nm = kVehNames[v.model - 400];
            snprintf(t, sizeof(t), "%s", AtmMoney(v.price, mb, sizeof(mb)));
            float pw = RpTextW(UI::S(19), t) + UI::S(20);
            ImVec2 ra(xb.x - UI::S(8) - pw, xb.y - UI::S(42)), rb(xb.x - UI::S(8), xb.y - UI::S(8));
            dl->AddRectFilled(ra, rb, IM_COL32(200, 30, 30, 255), UI::S(2));
            RpCenterIn(dl, UI::S(19), ra, rb, IM_COL32_WHITE, t);
            dl->PushClipRect(ImVec2(xa.x, xa.y), ImVec2(ra.x - UI::S(6), xb.y), true);
            RpPlainText(dl, UI::S(20), ImVec2(xa.x + UI::S(12), xb.y - UI::S(36)), IM_COL32(20, 20, 24, 255), nm);
            dl->PopClipRect();
        }
        if(n == 0) RpCenterIn(dl, UI::S(20), va, vb, IM_COL32(90, 96, 106, 255), "Nothing found - try another word.");
        dl->PopClipRect();
        if(maxS > 0)
        {
            float bx = vb.x + UI::S(2), bh2 = viewH * viewH / contentH, by = va.y + (viewH - bh2) * (s_Hpc.scroll / maxS);
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + UI::S(5), by + bh2), IM_COL32(120, 126, 136, 200), UI::S(3));
        }
    }
    if(s_Hpc.pick >= 0 && s_Hpc.pick < HOTEL_VEH_COUNT)
    {   // ---- confirm ----
        const HotelVeh& v = g_HotelVeh[s_Hpc.pick];
        float bw = UI::S(560), bh = UI::S(470);
        ImVec2 qa((ds.x - bw) * 0.5f, (ds.y - bh) * 0.5f), qb(qa.x + bw, qa.y + bh);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), UI::S(6));
        dl->AddRectFilled(qa, qb, IM_COL32_WHITE, UI::S(8));
        dl->AddRectFilled(qa, ImVec2(qb.x, qa.y + UI::S(50)), kHpcSiteCol[v.site], UI::S(8), ImDrawFlags_RoundCornersTop);
        RpCenterIn(dl, UI::S(19), qa, ImVec2(qb.x, qa.y + UI::S(50)), v.site == 1 ? IM_COL32(230, 40, 40, 255) : IM_COL32_WHITE, kHpcSites[v.site]);
        float pw = bw - UI::S(80), ph = pw * 125.0f / 204.0f;
        ImVec2 pa(qa.x + UI::S(40), qa.y + UI::S(62)), pb(pa.x + pw, pa.y + ph);
        ImVec2 uv; void* tex = HUD::VehiclePic(v.model, &uv);
        if(tex) dl->AddImage((ImTextureID)tex, pa, pb, ImVec2(0, 0), uv); else dl->AddRectFilled(pa, pb, IM_COL32(220, 224, 230, 255));
        RpCenterIn(dl, UI::S(26), ImVec2(qa.x, pb.y + UI::S(6)), ImVec2(qb.x, pb.y + UI::S(40)), IM_COL32(20, 20, 24, 255), kVehNames[v.model - 400]);
        bool afford = HpcMyMoney() >= v.price;
        snprintf(t, sizeof(t), "%s%s", AtmMoney(v.price, mb, sizeof(mb)), afford ? "" : "  -  not enough money");
        RpCenterIn(dl, UI::S(20), ImVec2(qa.x, pb.y + UI::S(40)), ImVec2(qb.x, pb.y + UI::S(68)), afford ? IM_COL32(200, 30, 30, 255) : IM_COL32(150, 150, 150, 255), t);
        const float by = qb.y - UI::S(66), gw = (bw - UI::S(80) - UI::S(20)) * 0.5f;
        ImVec2 ba(qa.x + UI::S(40), by), bb(ba.x + gw, by + UI::S(50)), na(bb.x + UI::S(20), by), nb(na.x + gw, by + UI::S(50));
        dl->AddRectFilled(ba, bb, afford ? IM_COL32(200, 30, 30, 255) : IM_COL32(180, 180, 184, 255), UI::S(4));
        RpCenterIn(dl, UI::S(20), ba, bb, IM_COL32_WHITE, "BUY");
        dl->AddRectFilled(na, nb, IM_COL32(60, 64, 72, 255), UI::S(4));
        RpCenterIn(dl, UI::S(20), na, nb, IM_COL32_WHITE, "CANCEL");
        if(HpcHit("##hpcbuy", ba, bb)) { int i = s_Hpc.pick; s_Hpc.pick = -1; HpcBuy(i); }
        else if(HpcHit("##hpcno", na, nb)) s_Hpc.pick = -1;
    }
    if(s_Hpc.msg[0] && now - s_Hpc.msgAt < 4000)
    {
        float tw = RpTextW(UI::S(18), s_Hpc.msg);
        ImVec2 ma((ds.x - tw) * 0.5f - UI::S(16), b.y - UI::S(60)), mb2((ds.x + tw) * 0.5f + UI::S(16), b.y - UI::S(20));
        dl->AddRectFilled(ma, mb2, IM_COL32(20, 22, 28, 235), UI::S(8));
        RpCenterIn(dl, UI::S(18), ma, mb2, IM_COL32_WHITE, s_Hpc.msg);
    }
    BcwEndOverlay();
    if(close) HpcClosePc();
}

static void RenderHotelPc()
{
    if(!HpcAllowed()) return;
    uint64_t now = MP_NowMs();
    int ped = Game::LocalPed();
    if(ped <= 0) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(s_Hpc.open && s_Hpc.phase == 2) { HpcDrawBrowser(now); return; }
    if(RpInvOpen() || s_Rp.phone || s_Atm.open) return;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    if(s_Hpc.phase == 0)
    {
        if(client.GetState() != NET_CONNECTED || Game::IsCharInAnyCar(ped) || Game::GetAreaVisible() != 0) return;
        int near = -1; float bd = 1e9f;
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        for(int i = 0; i < (int)s_HpcHotels.size(); ++i)
        {
            const Vec3& d = s_HpcHotels[i].door;
            float dx = d.x - me.x, dy = d.y - me.y, dz = d.z - me.z, d2 = dx * dx + dy * dy;
            if(d2 < 40.0f * 40.0f && fabsf(dz) < 15.0f)
            {   // a floating "HOTEL" sign over the door
                float sx, sy, dp;
                if(Game::WorldToScreen(Vec3{ d.x, d.y, d.z + 1.4f }, sx, sy, dp) && dp > 0)
                {
                    char l[64]; snprintf(l, sizeof(l), "HOTEL - %s", s_HpcHotels[i].name);
                    float tw = RpTextW(UI::S(18), l);
                    RpText(bg, UI::S(18), ImVec2(sx - tw * 0.5f, sy), IM_COL32(120, 200, 255, 255), l);
                }
            }
            if(d2 < 2.6f * 2.6f && fabsf(dz) < 2.5f && d2 < bd) { bd = d2; near = i; }
        }
        if(near >= 0)
        {
            const char* L[1] = { "Hotel room##hpcin" };
            if(RpOutlineColumn("##HpcIn", L, 1, UI::S(280), UI::S(62), 0, ds.y * 0.42f) == 0) HpcEnter(near);
        }
        return;
    }
    if(s_Hpc.phase == 1)
    {
        BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(26), ds.y * 0.45f, IM_COL32_WHITE, "Going to your room...");
        return;
    }
    // in the room: the computer button near the desk, "Leave room" always
    float dx = s_Hpc.deskPos.x - me.x, dy = s_Hpc.deskPos.y - me.y;
    bool atPc = s_Hpc.deskDone && dx * dx + dy * dy < 1.9f * 1.9f && !Game::IsCharInAnyCar(ped);
    if(!s_Hpc.deskDone) atPc = false;
    const char* L[1] = { "Leave room##hpcout" };
    if(RpOutlineColumn("##HpcOut", L, 1, UI::S(280), UI::S(62), 0, ds.y * 0.42f + (atPc ? UI::S(120) : 0)) == 0) { HpcLeave(true); return; }
    if(atPc)
    {
        float r = UI::S(46);
        ImVec2 c(RpSideColX(UI::S(280)) + UI::S(140), ds.y * 0.42f - UI::S(10));
        BcwBeginOverlay("##HpcBtn", ImVec2(c.x - r - UI::S(10), c.y - r - UI::S(10)), ImVec2(r * 2 + UI::S(20), r * 2 + UI::S(46)), 0.0f);
        if(HpcPcButton(c, r)) { s_Hpc.open = true; s_Hpc.page = -1; s_Hpc.scroll = 0; s_Hpc.pick = -1; s_Hpc.openAt = now; }
        const char* l = "Computer"; float tw = RpTextW(UI::S(16), l);
        RpText(ImGui::GetWindowDrawList(), UI::S(16), ImVec2(c.x - tw * 0.5f, c.y + r + UI::S(6)), IM_COL32_WHITE, l);
        BcwEndOverlay();
    }
    else if(s_Hpc.deskDone)
    {   // a little sign over the desk
        float sx, sy, dp;
        if(Game::WorldToScreen(Vec3{ s_Hpc.deskPos.x, s_Hpc.deskPos.y, s_Hpc.deskPos.z + 1.3f }, sx, sy, dp) && dp > 0)
        {
            const char* l = "Computer"; float tw = RpTextW(UI::S(17), l);
            RpText(ImGui::GetBackgroundDrawList(), UI::S(17), ImVec2(sx - tw * 0.5f, sy), IM_COL32(120, 200, 255, 255), l);
        }
    }
    if(s_Hpc.msg[0] && now - s_Hpc.msgAt < 4000) BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(22), ds.y * 0.20f, IM_COL32_WHITE, s_Hpc.msg);
}
// leaving the server / restarting: no room left behind
static void HotelPcReset()
{
    if(s_Hpc.phase != 0) HpcLeave(false);
    s_Hpc.spawnModel = 0; s_Hpc.hotel = -1;
}
