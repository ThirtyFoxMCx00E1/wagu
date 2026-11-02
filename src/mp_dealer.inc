// 3.55: the car dealership receptionist (default server "Broker MQQ" + Mega Vil Futuristic).
// GTA SA has no receptionist ped, so she stands at the real dealerships: Coutt and Schutz (Jefferson,
// Los Santos - also Mega Vil's "Budget Car Dealership") and Otto's Autos (Doherty, San Fierro, default
// server only). Walk up to her: "Access Showroom" -> a "Cardealer" list (categories + a red "Sell
// Vehicle"), Select -> the showroom: the car on the lot under a fixed camera, "06/20", "Name - $price",
// "N Kg Trunk Capacity", colour circles, Cancel | PURCHASE | < | >. Mega Vil style on both servers.
//   Mega Vil: RPC_BUY a = 3 (server checks price + place, money from cash + bank, into the garage).
//   Default server: your game money, the car goes into My Vehicles (/mv) when there is room.

struct DealerColour { uint8_t id; uint32_t rgb; const char* name; };
static const DealerColour kDealerCol[] =
{
    { 0, 0x101010, "Black" }, { 1, 0xF5F5F5, "White" }, { 8, 0x9CA3AF, "Silver" }, { 3, 0x9B1020, "Red" }, { 2, 0x2A77A1, "Blue" },
    { 79, 0x203A6C, "Navy" }, { 16, 0x2E6B2A, "Green" }, { 6, 0xF5A600, "Yellow" }, { 126, 0xE24BA1, "Pink" },
};
#define DEALER_COL_COUNT ((int)(sizeof(kDealerCol) / sizeof(kDealerCol[0])))

static struct
{
    int stage = 0;            // 0 closed, 1 the list, 2 the showroom, 3 sell list
    int site = -1;            // g_DealerSites index (the one you stand at)
    int ped = 0, pedSite = -1; uint64_t pedTry = 0;
    int cat = 0, sel = 0;     // list: highlighted row
    std::vector<int> rows;    // showroom: g_DealerVeh indices of the category
    int idx = 0, col = 0;
    int car = 0, carModel = 0; float spin = 0;
    bool cam = false;
} s_Dl;

// ---- remembered colours (model -> colour index), so a garage / My Vehicles spawn keeps it ----------
static std::map<int, int> s_DlColours; static bool s_DlColLoaded = false;
static std::string DealerColPath() { return std::string(s_szDataDir) + "/dealer_colours.txt"; }
static void DealerColLoad()
{
    if(s_DlColLoaded) return; s_DlColLoaded = true;
    if(FILE* f = fopen(DealerColPath().c_str(), "r")) { int m, c; while(fscanf(f, "%d %d", &m, &c) == 2) if(c >= 0 && c < DEALER_COL_COUNT) s_DlColours[m] = c; fclose(f); }
}
static void DealerRememberColour(int model, int ci)
{
    DealerColLoad(); if(ci < 0 || ci >= DEALER_COL_COUNT) return;
    s_DlColours[model] = ci;
    if(FILE* f = fopen(DealerColPath().c_str(), "w")) { for(auto& kv : s_DlColours) fprintf(f, "%d %d\n", kv.first, kv.second); fclose(f); }
}
static void DealerApplyColour(int car, int model)
{
    if(car <= 0) return; DealerColLoad();
    auto it = s_DlColours.find(model); if(it == s_DlColours.end()) return;
    Game::SetCarColours(car, kDealerCol[it->second].id, kDealerCol[it->second].id);
}


// ---- 3.55.1: never inside a wall ---------------------------------------------------------------
// The table spots are only a first guess. When you come near a dealership (its collision is loaded
// then) the receptionist spot, the lot spot and the showroom camera are tested against the world:
//   receptionist: ground within 2.5 m of the guess, nothing solid from 0.3 m to 2.3 m over it, and a
//                 clear line 0.6 m out in 8 directions at chest height (not in / against a wall);
//   car:          flat ground in a 3 m circle (it turns in the showroom), 2.4 m of headroom, clear
//                 lines from the middle to 12 points on that circle, 4 m+ away from her;
//   camera:       a clear line to the car, not inside anything.
// Failing spots move to the nearest place that passes (rings of 1.5 m out to 20 m). Retried while
// the collision streams in; /dealerspot shows where they ended up.
struct DealerSpot { bool done = false; int tries = 0; uint64_t nextTry = 0; bool movedPed = false, movedCar = false;
                    Vec3 ped = { 0, 0, 0 }; float pedH = 0; Vec3 car = { 0, 0, 0 }; float carH = 0; Vec3 cam = { 0, 0, 0 }; };
static DealerSpot s_DlSpot[DEALER_SITE_COUNT];

static bool DlClear(const Vec3& a, const Vec3& b) { return Game::LineOfSightClear(a, b); }
static bool DlPedOk(float x, float y, float zHint, float& gz)
{
    float g = Game::GroundBelow(x, y, zHint + 1.5f);
    if(g < -999.0f || fabsf(g - zHint) > 2.5f) return false;
    float over = Game::GroundBelow(x, y, g + 2.3f);
    if(over > g + 0.3f) return false;                              // something solid at body height
    const Vec3 c = { x, y, g + 1.0f };
    if(!DlClear(c, { x, y, g + 2.0f })) return false;
    for(int k = 0; k < 8; ++k)
    {
        float a = k * 0.785398f;
        if(!DlClear(c, { x + cosf(a) * 0.6f, y + sinf(a) * 0.6f, g + 1.0f })) return false;
    }
    gz = g; return true;
}
static bool DlCarOk(float x, float y, float zHint, float& gz)
{
    float g = Game::GroundBelow(x, y, zHint + 2.0f);
    if(g < -999.0f || fabsf(g - zHint) > 3.0f) return false;
    if(Game::GroundBelow(x, y, g + 2.4f) > g + 0.3f) return false;   // roof / beam too low
    const Vec3 c = { x, y, g + 0.8f };
    if(!DlClear(c, { x, y, g + 2.2f })) return false;
    for(int k = 0; k < 12; ++k)
    {
        float a = k * 0.523599f, px = x + cosf(a) * 3.0f, py = y + sinf(a) * 3.0f;
        float pg = Game::GroundBelow(px, py, g + 1.5f);
        if(pg < -999.0f || fabsf(pg - g) > 0.7f) return false;    // a kerb / slope / drop
        if(Game::GroundBelow(px, py, g + 2.0f) > g + 0.8f) return false;
        if(!DlClear(c, { px, py, g + 0.8f }) || !DlClear({ x, y, g + 0.4f }, { px, py, pg + 0.4f })) return false;
    }
    gz = g; return true;
}
// nearest passing spot to (x, y) on rings of 1.5 m; minAway: keep that far from 'away'
template<class F> static bool DlSearch(float x, float y, float z, float maxR, F ok, Vec3& out, const Vec3* away = nullptr, float minAway = 0)
{
    for(float r = 0; r <= maxR; r += 1.5f)
    {
        const int n = r < 0.1f ? 1 : (int)(r * 4.2f) + 6;
        for(int k = 0; k < n; ++k)
        {
            float a = k * 6.2831853f / n, px = x + cosf(a) * r, py = y + sinf(a) * r, g;
            if(away) { float dx = px - away->x, dy = py - away->y; if(dx * dx + dy * dy < minAway * minAway) continue; }
            if(ok(px, py, z, g)) { out = { px, py, g }; return true; }
        }
    }
    return false;
}
static float DlFace(const Vec3& from, const Vec3& to) { float h = atan2f(-(to.x - from.x), to.y - from.y) * 57.29578f; return h < 0 ? h + 360.0f : h; }
static void DealerFixSpot(int i, const Vec3& me, uint64_t now)
{
    DealerSpot& S = s_DlSpot[i]; const DealerSite& d = g_DealerSites[i];
    if(S.done || now < S.nextTry) return;
    if(!S.tries) { S.ped = { d.x, d.y, d.z }; S.pedH = d.h; S.car = { d.cx, d.cy, d.cz }; S.carH = d.ch; }
    float dx = me.x - d.x, dy = me.y - d.y;
    if(dx * dx + dy * dy > 70.0f * 70.0f) return;                  // collision is only there close by
    S.nextTry = now + 1500; ++S.tries;
    float tz; Game::OpenGround(d.x, d.y, d.z, tz); Game::OpenGround(d.cx, d.cy, d.cz, tz);   // request the collision
    Vec3 p, c;
    const bool pOk = DlSearch(d.x, d.y, d.z, 20.0f, DlPedOk, p);
    const Vec3 pp = pOk ? p : Vec3{ d.x, d.y, d.z };
    const bool cOk = DlSearch(d.cx, d.cy, d.cz, 20.0f, DlCarOk, c, &pp, 4.5f);
    if((!pOk || !cOk) && S.tries < 6) return;                       // still streaming in: again soon
    S.ped = pp; S.movedPed = pOk && (fabsf(p.x - d.x) > 0.1f || fabsf(p.y - d.y) > 0.1f);
    S.car = cOk ? c : Vec3{ d.cx, d.cy, d.cz }; S.movedCar = cOk && (fabsf(c.x - d.cx) > 0.1f || fabsf(c.y - d.cy) > 0.1f);
    S.pedH = DlFace(S.ped, S.car);                                    // she looks at the lot
    S.carH = d.ch;
    // the camera: around the car, 7.5 m out (then closer), 2.6 m up, with a clear view
    const Vec3 look = { S.car.x, S.car.y, S.car.z + 0.8f };
    bool camOk = false;
    static const float kAng[] = { 35, 0, 70, -35, 105, -70, 140, -105, 180, -140 };
    for(float dist : { 7.5f, 6.0f, 4.8f })
    {
        for(float off : kAng)
        {
            float r = (S.carH + off) * 0.0174533f;
            Vec3 cp = { S.car.x - sinf(r) * dist, S.car.y + cosf(r) * dist, S.car.z + 2.6f };
            float under = Game::GroundBelow(cp.x, cp.y, cp.z + 0.6f);
            if(under > cp.z - 0.8f) continue;                          // inside / on top of something
            if(!DlClear(cp, look) || !DlClear(cp, { look.x, look.y, look.z + 1.2f })) continue;
            S.cam = cp; camOk = true; break;
        }
        if(camOk) break;
    }
    if(!camOk) S.cam = { S.car.x + 0.5f, S.car.y - 0.5f, S.car.z + 9.0f };   // from above
    S.done = true;
}
static const DealerSpot& DlSpot(int i) { return s_DlSpot[i]; }

static bool DealerServerOn(bool& rp)
{
    rp = RpOnServer();
    if(client.GetState() != NET_CONNECTED || !Game::IsReady()) return false;
    if(rp) return s_Rp.engaged && s_Rp.view == 2;
    return MP::IsOnDefault();
}
static bool DealerSiteOk(int i, bool rp) { return i >= 0 && i < DEALER_SITE_COUNT && (rp ? g_DealerSites[i].rp : g_DealerSites[i].def); }
static float DealerDist2(const Vec3& me, int i) { const Vec3& p = s_DlSpot[i].done ? s_DlSpot[i].ped : Vec3{ g_DealerSites[i].x, g_DealerSites[i].y, g_DealerSites[i].z }; float dx = me.x - p.x, dy = me.y - p.y; return dx * dx + dy * dy; }

static void DealerDropCar() { if(s_Dl.car > 0) Game::DeleteCar(s_Dl.car); s_Dl.car = 0; s_Dl.carModel = 0; }
static void DealerEndShowroom()
{
    DealerDropCar();
    if(s_Dl.cam) { Game::RestoreCamera(); Game::SetPlayerControl(true); s_Dl.cam = false; }
}
static void DealerClose() { DealerEndShowroom(); s_Dl.stage = 0; }
bool MP::DealerShown() { return s_Dl.stage != 0; }
bool MP::DealerBack()
{
    if(!s_Dl.stage) return false;
    if(s_Dl.stage == 2 || s_Dl.stage == 3) { DealerEndShowroom(); s_Dl.stage = 1; } else DealerClose();
    return true;
}

static void DealerOpenCat(int cat)
{
    s_Dl.rows.clear();
    for(int i = 0; i < DEALER_VEH_COUNT; ++i) if(g_DealerVeh[i].cat == cat && Game::IsKnownCarModel(g_DealerVeh[i].model)) s_Dl.rows.push_back(i);
    if(s_Dl.rows.empty()) return;
    s_Dl.cat = cat; s_Dl.idx = 0; s_Dl.col = 0; s_Dl.spin = DlSpot(s_Dl.site).carH; s_Dl.stage = 2;
}

static void DealerReceptionist(bool want, int site, uint64_t now)
{
    if(s_Dl.ped && (!want || site != s_Dl.pedSite)) { Game::DeleteChar(s_Dl.ped); s_Dl.ped = 0; s_Dl.pedSite = -1; }
    if(!want || s_Dl.ped || now < s_Dl.pedTry) return;
    s_Dl.pedTry = now + 500;
    const DealerSpot& S = DlSpot(site);
    if(!S.done) return;                                   // 3.55.1: only once her spot was checked
    int skin = Game::IsSafePedModel(150) ? 150 : 141;   // office worker
    s_Dl.ped = Game::CreateNpcPed(skin, { S.ped.x, S.ped.y, S.ped.z + 1.0f }, S.pedH);
    if(s_Dl.ped) { Game::SetCharGodMode(s_Dl.ped, true); Game::FreezeChar(s_Dl.ped, true); s_Dl.pedSite = site; }
}

static void DealerProcess(int ped, uint64_t now)
{
    bool rp = false;
    const bool on = ped > 0 && DealerServerOn(rp);
    int site = -1;
    Vec3 me = { 0, 0, 0 };
    if(on)
    {
        Game::GetCharPos(ped, me);
        float best = 120.0f * 120.0f;
        for(int i = 0; i < DEALER_SITE_COUNT; ++i) if(DealerSiteOk(i, rp)) { float d2 = DealerDist2(me, i); if(d2 < best) { best = d2; site = i; } }
    }
    if(site >= 0) DealerFixSpot(site, me, now);
    DealerReceptionist(site >= 0, site, now);
    if(s_Dl.stage && (site < 0 || site != s_Dl.site || (!s_Dl.cam && DealerDist2(me, site) > 6.0f * 6.0f))) DealerClose();
    if(s_Dl.stage != 2) { if(s_Dl.cam) DealerEndShowroom(); return; }
    // the showroom: the car on the lot, turning slowly, the camera on it
    const DealerSpot& S = DlSpot(s_Dl.site);
    const DealerVeh& v = g_DealerVeh[s_Dl.rows[s_Dl.idx]];
    if(s_Dl.car > 0 && (s_Dl.carModel != v.model || !Game::CarExists(s_Dl.car))) DealerDropCar();
    const float gz = S.car.z;
    if(!s_Dl.car)
    {
        int car = Game::CreateCar(v.model, { S.car.x, S.car.y, gz + 1.0f }, false);   // 0 while it streams in
        if(car > 0)
        {
            s_Dl.car = car; s_Dl.carModel = v.model;
            Game::SetCarProofs(car, true, true, true, true, true); Game::FreezeCar(car, true);
            Game::SetCarColours(car, kDealerCol[s_Dl.col].id, kDealerCol[s_Dl.col].id);
        }
    }
    if(s_Dl.car > 0) { s_Dl.spin += 0.35f; if(s_Dl.spin >= 360.0f) s_Dl.spin -= 360.0f; Game::SetCarHeading(s_Dl.car, s_Dl.spin); }
    if(!s_Dl.cam)
    {
        Game::SetFixedCamera(S.cam, { S.car.x, S.car.y, gz + 0.6f });
        Game::SetPlayerControl(false); s_Dl.cam = true;
    }
}

// ---- drawing (Mega Vil look: deep navy glass, sky / purple accents) --------------------------------
static void DealerPanelStyle(bool begin)
{
    if(begin)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(8, 14, 32, 238));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(56, 189, 248, 200));
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(56, 189, 248, 70));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(56, 189, 248, 110));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(56, 189, 248, 150));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(30, 41, 79, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(56, 120, 200, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(56, 189, 248, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(14)); ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(2));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, UI::S(10)); ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(18), UI::S(14)));
    }
    else { ImGui::PopStyleVar(4); ImGui::PopStyleColor(8); }
}
static const char* DealerTitle(bool rp) { return rp ? "Mega Vil // Cardealer" : "Broker MQQ // Cardealer"; }

static void DealerDrawList(bool rp)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float w = UI::S(560);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(w, 0));
    DealerPanelStyle(true);
    ImGui::Begin("##dealerlist", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImColor(RP_SKY >> 24 & 255, RP_SKY >> 16 & 255, RP_SKY >> 8 & 255), "%s", DealerTitle(rp));
    ImGui::TextDisabled("%s - receptionist", g_DealerSites[s_Dl.site].name);
    ImGui::Separator();
    const float rowH = UI::S(46);
    for(int i = 0; i <= DLC_COUNT; ++i)
    {
        const bool sell = i == DLC_COUNT;
        if(sell) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(248, 80, 80, 255));
        char l[64]; snprintf(l, sizeof(l), "%s##dlc%d", sell ? "(Sell Vehicle)" : kDealerCat[i], i);
        if(ImGui::Selectable(l, s_Dl.sel == i, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, rowH)))
        {
            s_Dl.sel = i;
            if(ImGui::IsMouseDoubleClicked(0)) { if(sell) s_Dl.stage = 3; else DealerOpenCat(i); }
        }
        if(sell) ImGui::PopStyleColor();
    }
    ImGui::Separator();
    const float bw = (ImGui::GetContentRegionAvail().x - UI::S(12)) * 0.5f;
    if(ImGui::Button("Select##dls", ImVec2(bw, UI::S(54)))) { if(s_Dl.sel == DLC_COUNT) s_Dl.stage = 3; else DealerOpenCat(s_Dl.sel); }
    ImGui::SameLine(0, UI::S(12));
    if(ImGui::Button("Cancel##dlc", ImVec2(bw, UI::S(54)))) DealerClose();
    ImGui::End();
    DealerPanelStyle(false);
}

static void DealerSellList(bool rp)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(UI::S(620), 0));
    DealerPanelStyle(true);
    ImGui::Begin("##dealersell", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextColored(ImVec4(0.97f, 0.31f, 0.31f, 1), "Sell Vehicle");
    ImGui::TextDisabled("The dealer pays half of the price.");
    ImGui::Separator();
    char l[96]; int n = 0;
    if(rp)
    {
        for(int v = 0; v < s_Rp.me.nveh && v < RP_VEH_SLOTS; ++v)
        {
            const int m = s_Rp.me.veh[v]; if(m < 400 || m > 611) continue; ++n;
            snprintf(l, sizeof(l), "Sell %s##dsv%d", kVehNames[m - 400], v);
            if(ImGui::Button(l, ImVec2(-1, UI::S(50)))) { RpSend(RPC_SELL_VEH, (uint8_t)v); s_Dl.stage = 1; }
        }
    }
    else
    {
        MvLoad();
        for(int i = 0; i < s_nMv; ++i)
        {
            const int m = s_Mv[i].model; ++n;
            int back = DealerVehPrice(m) / 2; if(back <= 0) back = 2000;
            snprintf(l, sizeof(l), "Sell %s - $%d##dsm%d", kVehNames[m - 400], back, i);
            if(ImGui::Button(l, ImVec2(-1, UI::S(50))))
            {
                if(MvSpawned(s_Mv[i]) && Game::GetDriverOfCar(s_Mv[i].car) <= 0) Game::DeleteCar(s_Mv[i].car);
                else if(MvSpawned(s_Mv[i])) Game::ReleaseCar(s_Mv[i].car);
                if(s_Mv[i].blip) Game::RemoveBlip(s_Mv[i].blip);
                for(int k = i; k + 1 < s_nMv; ++k) s_Mv[k] = s_Mv[k + 1];
                --s_nMv; MvSave(); Game::AddMoney(back);
                SysMsg(COL_GREEN, "[DEALER] Sold your %s for $%d.", kVehNames[m - 400], back);
                s_Dl.stage = 1; break;
            }
        }
    }
    if(!n) ImGui::TextWrapped(rp ? "No vehicles in your garage." : "My Vehicles is empty (/savecar keeps a car, or buy one here).");
    ImGui::Separator();
    if(ImGui::Button("Back##dsb", ImVec2(-1, UI::S(54)))) s_Dl.stage = 1;
    ImGui::End();
    DealerPanelStyle(false);
}

static void DealerPurchase(bool rp)
{
    const DealerVeh& v = g_DealerVeh[s_Dl.rows[s_Dl.idx]];
    const DealerSpot& S = DlSpot(s_Dl.site);
    const int ci = s_Dl.col;
    if(rp)
    {
        if(s_Rp.me.cash + s_Rp.me.bank < v.price) { SysMsg(COL_RED, "[DEALER] %s costs $%d - not enough money (cash + bank).", kVehNames[v.model - 400], v.price); return; }
        if(s_Rp.me.nveh >= RP_VEH_SLOTS) { SysMsg(COL_RED, "[DEALER] Your garage is full - sell one first (Sell Vehicle)."); return; }
    }
    else if(Game::GetMoney() < v.price) { SysMsg(COL_RED, "[DEALER] %s costs $%d - you have $%d.", kVehNames[v.model - 400], v.price, Game::GetMoney()); return; }
    DealerRememberColour(v.model, ci);
    DealerClose();
    int ped = Game::LocalPed();
    if(ped > 0) Game::TeleportGround(ped, S.car, S.carH);   // the new car comes on the lot
    if(rp) { RpSend(RPC_BUY, 3, (uint8_t)ci, v.model); return; }
    Game::AddMoney(-v.price);
    int car = SpawnVehicleForMe(v.model, false);
    if(car > 0) Game::SetCarColours(car, kDealerCol[ci].id, kDealerCol[ci].id);
    MvLoad();
    if(car > 0 && s_nMv < MV_MAX)
    {
        MvCar c; c.model = v.model; Game::GetCarPos(car, c.p); c.h = Game::GetCarHeading(car); c.car = car;
        s_Mv[s_nMv++] = c; MvSave();
        SysMsg(COL_GREEN, "[DEALER] Paid $%d - your %s (%s) is in My Vehicles (%d/%d), /mv.", v.price, kVehNames[v.model - 400], kDealerCol[ci].name, s_nMv, MV_MAX);
    }
    else SysMsg(COL_GREEN, "[DEALER] Paid $%d - enjoy your %s. (My Vehicles is full: /mv to make room, then /savecar.)", v.price, kVehNames[v.model - 400]);
}

static const char* DealerMoney(int v, char* out, size_t n)
{
    char t[24]; snprintf(t, sizeof(t), "%d", v); const int l = (int)strlen(t); size_t o = 0;
    for(int i = 0; i < l && o + 2 < n; ++i) { if(i && (l - i) % 3 == 0) out[o++] = ','; out[o++] = t[i]; }
    out[o] = 0; return out;
}
static void DealerDrawShowroom(bool rp)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const int n = (int)s_Dl.rows.size(); if(!n) { s_Dl.stage = 1; return; }
    const DealerVeh& v = g_DealerVeh[s_Dl.rows[s_Dl.idx]];
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    // top right: index, name - price, trunk
    char a[48], b[96], c[48], mbuf[24];
    snprintf(a, sizeof(a), "%02d/%02d", s_Dl.idx + 1, n);
    snprintf(b, sizeof(b), "%s - $%s", kVehNames[v.model - 400], DealerMoney(v.price, mbuf, sizeof(mbuf)));
    snprintf(c, sizeof(c), "%u Kg Trunk Capacity", v.trunk);
    ImFont* f = ImGui::GetFont(); const float fs = UI::S(40), fs2 = UI::S(30);
    float y = ds.y * 0.06f; const float rx = ds.x - UI::S(40);
    auto rtext = [&](const char* t, float size, ImU32 col) {
        ImVec2 sz = f->CalcTextSizeA(size, FLT_MAX, 0, t);
        fg->AddText(f, size, ImVec2(rx - sz.x + UI::S(2), y + UI::S(2)), IM_COL32(0, 0, 0, 200), t);
        fg->AddText(f, size, ImVec2(rx - sz.x, y), col, t); y += sz.y + UI::S(4);
    };
    rtext(a, fs2, IM_COL32(56, 189, 248, 255)); rtext(b, fs, IM_COL32(255, 255, 255, 255)); rtext(c, fs2, IM_COL32(203, 213, 225, 255));
    rtext(kDealerCat[s_Dl.cat], fs2 * 0.85f, IM_COL32(192, 132, 252, 255));
    // bottom: colour circles, then Cancel | PURCHASE | < | >
    const float bh = UI::S(62), gap = UI::S(12);
    const float wC = UI::S(170), wP = UI::S(240), wA = UI::S(90);
    const float total = wC + wP + wA * 2 + gap * 3;
    const float circle = UI::S(22), cgap = UI::S(14);
    const float ctotal = DEALER_COL_COUNT * circle * 2 + (DEALER_COL_COUNT - 1) * cgap;
    const float winW = std::max(total, ctotal) + UI::S(36);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f, ds.y - UI::S(24)), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(winW, 0));
    DealerPanelStyle(true);
    ImGui::Begin("##dealershow", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::SetCursorPosX((winW - ctotal) * 0.5f);
    for(int i = 0; i < DEALER_COL_COUNT; ++i)
    {
        if(i) ImGui::SameLine(0, cgap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if(ImGui::InvisibleButton("##dcol", ImVec2(circle * 2, circle * 2)))
        { s_Dl.col = i; if(s_Dl.car > 0) Game::SetCarColours(s_Dl.car, kDealerCol[i].id, kDealerCol[i].id); }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const uint32_t rgb = kDealerCol[i].rgb;
        const ImVec2 cc(p.x + circle, p.y + circle);
        if(s_Dl.col == i) dl->AddCircle(cc, circle + UI::S(3), IM_COL32(56, 189, 248, 255), 32, UI::S(3));
        dl->AddCircleFilled(cc, circle - UI::S(1), IM_COL32(rgb >> 16 & 255, rgb >> 8 & 255, rgb & 255, 255), 32);
        dl->AddCircle(cc, circle - UI::S(1), IM_COL32(255, 255, 255, 120), 32, UI::S(1.5f));
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(0, UI::S(6)));
    ImGui::SetCursorPosX((winW - total) * 0.5f);
    if(ImGui::Button("Cancel##dsc", ImVec2(wC, bh))) { DealerEndShowroom(); s_Dl.stage = 1; }
    ImGui::SameLine(0, gap);
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(22, 163, 74, 255)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(34, 197, 94, 255));
    char pl[48]; snprintf(pl, sizeof(pl), "PURCHASE##dsp");
    const bool buy = ImGui::Button(pl, ImVec2(wP, bh));
    ImGui::PopStyleColor(2);
    ImGui::SameLine(0, gap);
    if(ImGui::Button("<##dsl", ImVec2(wA, bh))) { s_Dl.idx = (s_Dl.idx + n - 1) % n; DealerDropCar(); }
    ImGui::SameLine(0, gap);
    if(ImGui::Button(">##dsr", ImVec2(wA, bh))) { s_Dl.idx = (s_Dl.idx + 1) % n; DealerDropCar(); }
    // controller: LB / RB also browse, the D-pad moves between the buttons
    if(ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) { s_Dl.idx = (s_Dl.idx + n - 1) % n; DealerDropCar(); }
    if(ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) { s_Dl.idx = (s_Dl.idx + 1) % n; DealerDropCar(); }
    ImGui::End();
    DealerPanelStyle(false);
    if(buy) DealerPurchase(rp);
}

static void DealerRender()
{
    bool rp = false;
    if(!DealerServerOn(rp) || Game::IsPaused()) return;
    int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    // the sign over her
    if(s_Dl.ped > 0 && s_Dl.pedSite >= 0 && !s_Dl.cam)
    {
        const Vec3& q = DlSpot(s_Dl.pedSite).ped;
        if(DealerDist2(me, s_Dl.pedSite) < 30.0f * 30.0f)
        {
            ImDrawList* bg = ImGui::GetBackgroundDrawList();
            BcwLabel3D(bg, { q.x, q.y, q.z + 2.35f }, "RECEPTIONIST", RP_SKY, 30.0f, 8.0f);
            BcwLabel3D(bg, { q.x, q.y, q.z + 2.05f }, "Cardealer", 0xFFFFFFFF, 22.0f, 8.0f);
        }
    }
    if(s_Dl.stage == 1) { DealerDrawList(rp); return; }
    if(s_Dl.stage == 2) { DealerDrawShowroom(rp); return; }
    if(s_Dl.stage == 3) { DealerSellList(rp); return; }
    // walk up to her: "Access Showroom" (outlined, right of centre, like the ATM)
    if(s_Dl.pedSite < 0 || Game::IsCharInAnyCar(ped) || DealerDist2(me, s_Dl.pedSite) > 3.2f * 3.2f || fabsf(me.z - DlSpot(s_Dl.pedSite).ped.z) > 3.0f) return;
    if(rp && (RpInvOpen() || s_Rp.phone || s_Rp.talkStage || s_Atm.open)) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const char* L[1] = { "Access Showroom##dlacc" };
    if(RpOutlineColumn("##DealerBtn", L, 1, UI::S(330), UI::S(62), 0, ds.y * 0.42f) == 0) { s_Dl.site = s_Dl.pedSite; s_Dl.stage = 1; s_Dl.sel = 0; }
}

// 3.55.1: /dealerspot - where the receptionist, the lot and the camera ended up (and if they moved)
static bool DealerCommand(const char* cmd, const char*)
{
    if(strcmp(cmd, "dealerspot")) return false;
    bool rp = false; if(!DealerServerOn(rp)) { SysMsg(COL_RED, "Only on the default server and Mega Vil."); return true; }
    for(int i = 0; i < DEALER_SITE_COUNT; ++i)
    {
        if(!DealerSiteOk(i, rp)) continue;
        const DealerSpot& S = s_DlSpot[i];
        if(!S.done) { SysMsg(COL_SYSTEM, "%s: not checked yet (go there).", g_DealerSites[i].name); continue; }
        SysMsg(COL_SYSTEM, "%s: receptionist %.1f %.1f %.1f%s, car %.1f %.1f %.1f%s", g_DealerSites[i].name,
               S.ped.x, S.ped.y, S.ped.z, S.movedPed ? " (moved off a wall)" : "", S.car.x, S.car.y, S.car.z, S.movedCar ? " (moved off a wall)" : "");
    }
    return true;
}
