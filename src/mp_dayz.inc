// MP 3.58 - Project Day Z Legacy (gamemode "Proper DAYZ") - the phone side.
// Included from mp.cpp after the other featured modes (it reuses the Mega Vil text helpers, the
// Google radar, the PUBG compass and the BCW overlay buttons).
//
//   connection screen  : a cabin interior (fireplace, sofa, lamps, bottles, rifles on the wall),
//                        PLAY / RULES / CREDITS / SETTINGS, the title logo (human print + zombie hand)
//   playing            : random 1 of 15 spawns, real loot objects on the ground (PICK UP + 2-hand anim),
//                        the inventory (two panels, KG bars), 5 armour slots, ear / eye / temperature,
//                        the scoreboard, weapon + ammo, FPS | PING, compass, damage numbers + blood drops,
//                        blood screen, infection, GPS-gated square radar (COORDS | MAP), drive HUD + fuel,
//                        local zombies, ruins debris, real fog (90 m), ANTI-RELOG, F2 control panel.

#include "mp_dayz_atlas.h"   // 3.59: the MTA DayZ textures (dz_hud / dz_status / dz_items / dz_map)

#define DZ_YEL   0xFACC15FFu
#define DZ_GRN   0x4ADE80FFu
#define DZ_RED   0xEF4444FFu
#define DZ_ORG   0xF97316FFu
#define DZ_GRY   0xA0A0A0FFu
#define DZ_LOOT_RANGE   110.0f     // loot objects exist inside this range
#define DZ_LOOT_OBJ_MAX 70
#define DZ_PICK_RANGE   2.6f
#define DZ_ZED_MAX      7
#define DZ_CABIN_AREA   3          // CJ's house interior (the cabin)

static const Vec3 kDzCabinCam  = { 2493.20f, -1700.60f, 1015.75f };
static const Vec3 kDzCabinLook = { 2497.40f, -1696.20f, 1014.95f };
static const Vec3 kDzCabinOutCam  = { -1634.0f, -2249.0f, 33.8f };   // fallback: the hunter's cabin, Back o Beyond
static const Vec3 kDzCabinOutLook = { -1630.5f, -2243.5f, 31.6f };

struct DzLootC { uint16_t idx; uint8_t item, count; float x, y, z; int obj; bool zDone; uint64_t tryAt = 0; uint8_t tries = 0; };
struct DzZed   { int ped; uint64_t born, deadAt; int lastHp; bool reported; bool hunting = false; uint64_t wanderAt = 0, huntAt = 0; int model = 0;
                  uint64_t atkAt = 0, hitDue = 0, idleAt = 0; int style = 0; bool styled = false; int dmg = 0;
                  Vec3 lastP{}; uint64_t stillSince = 0, goAt = 0; int unstuck = 0; };
struct DzNum   { Vec3 at; bool world; float sx, sy; int val; uint64_t t; bool green; };
struct DzDrop  { float x, y, vy; uint64_t t; bool green; };
struct DzProp  { int model, alt; Vec3 p; float rz, rx; int obj; };
struct DzOpts  { bool fps = true, dmg = true, compass = true, blood = true, logo = true; int radar = 1; };

struct DzClientState
{
    bool        engaged = false;
    int         view = 0;               // 1 cabin (connection screen), 2 playing, 3 dead (waiting for the game respawn)
    int         page = 0;               // cabin: 0 menu, 1 rules, 2 credits, 3 settings
    uint64_t    connectedAt = 0, lastHudOff = 0, spawnedAt = 0, playAsked = 0, diedAt = 0, lastUnstick = 0, rulesAt = 0;
    bool        rulesShown = false;   // 3.60: the 10 rules window after the first spawn of this connection
    bool        haveMe = false, haveInv = false;
    PktDzMe     me{};
    PktDzInv    inv{};
    uint64_t    combatUntil = 0;
    std::unordered_map<uint16_t, DzLootC> loot;
    int         nLootObj = 0;
    // cabin
    DzProp      props[24]; int nProps = 0; int fire = 0; bool propsMade = false, cabinOutdoor = false; uint64_t cabinAt = 0;
    // gear on the body
    int         bagObj = 0, bagLvl = 0; uint64_t bagAt = 0;
    int         armourGiven = 0;
    // pick up
    int         pickIdx = -1; uint64_t pickAt = 0; bool pickAnim = false;
    // weapons waiting for the model to stream in
    struct PendW { int w, ammo; bool ammoOnly; uint64_t t; }; std::vector<PendW> pendW;
    // zombies
    std::vector<DzZed> zeds; uint64_t lastZedSpawn = 0;
    // feedback
    std::deque<DzNum> nums; std::deque<DzDrop> drops; uint64_t hurtAt = 0; float hurtK = 0; int prevHp = -1;
    std::unordered_map<int, int> otherHp;   // ped -> last health (green numbers when I hit them)
    // fuel
    std::unordered_map<int, float> fuel; int lastCar = 0; uint64_t lastFuelTick = 0; bool refueling = false; uint64_t refuelAsk = 0;
    // UI
    bool        invOpen = false; int invSel = -1; int nearSel = -1;
    bool        panelOpen = false; int panelTab = 0;
    int         uiWin = 0;              // a window the server opened: 1 help 2 rules 3 stats 4 party 5 credits
    bool        coords = false;         // radar tab: COORDS | MAP
    char        notice[96] = ""; uint32_t noticeCol = DZ_YEL; uint64_t noticeAt = 0;
    char        spawnName[64] = ""; uint64_t spawnNameAt = 0;
    bool        fog = false;
    std::vector<int> ruinObjs; int ruinSite = -1;
    uint8_t     ruinState[26] = {}, ruinTries[26] = {}; uint64_t ruinAt = 0, lootAt = 0;   // 3.60: built a few per tick (no freeze)
    uint64_t    lastStatus = 0, lastNeedsHurt = 0, lastInfHurt = 0, lastLootAsk = 0;
    bool        radarHidden = false;
    DzOpts      opt;
};
static DzClientState s_Dz;
// 3.59 (mp_dayz_mta.inc): the MTA DayZ systems
struct PktDzEvent;
static void DzXEvent(const PktDzEvent& e);
static void DzXList(const uint8_t* d, int len);
static void DzXProcess(int ped, int hp, int prevHp, uint64_t now);
static void DzXReset();
static float DzZedDetectRange(int ped);
static int  DzSkillRank(int k);
static int  DzZedAlarmTake();
static bool DzXDowned();
static bool DzXProne();   // 4.00
static void DzKnockDmg(int id, int dmg, uint64_t now);   // 4.00
static void DzKnockShow(const char* top, const char* name, const char* sub);   // 4.00
static bool DzXSpawnSelect();
static void DzXJournal(int tab);
static void DzXToggleList();
static void DzXGpsToggle();
static bool DzXCompassToggle();
static void DzXSpawned();
static void DzRulesTick(uint64_t now);
static void DzShowRules();   // 3.60: the 10 rules window after the spawn

// 4.08: base building (mp_dzbase.inc)
static void DzBaseOnPacket(const uint8_t* d, int len);
static void DzBaseTick(const Vec3& me, uint64_t now);
static void DzBaseClear();
static void DzBaseToggle();
static void DzBaseDraw(ImDrawList* bg, ImVec2 ds, int ped);
static void DzBaseDoorButton(ImVec2 ds, int ped);
static bool DzBaseOpen();
static bool DzOnServer()
{
    return client.GetState() == NET_CONNECTED && MP_IsDzServerName(client.GetServerName()[0] ? client.GetServerName() : s_szCurServer);
}
static bool DzHideCorner() { return DzOnServer() && s_Dz.view == 2 && !Game::IsPaused(); }   // 3.60: in game on Day Z (GPS bottom-left)
bool MP::IsOnDz() { return DzOnServer(); }
static bool s_DzDlSkip = false;   // 3.62: PLAY NOW pressed in the download window (this connection)
static bool DzHidden() { return s_Dz.engaged && (s_Dz.view == 1 || s_Dz.view == 3); }
static bool DzConnScreen() { return s_Dz.engaged && s_Dz.view == 1; }   // 4.02: no MP top bar / chat over it
static ImU32 DzCol(uint32_t rgba, float a = 1.0f) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }

static void DzSend(uint8_t cmd, int a = 0, int b = 0, int32_t val = 0, const char* text = nullptr)
{
    PktDzCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_DZ_CMD; c.cmd = cmd; c.a = (uint8_t)a; c.b = (uint8_t)b; c.val = val;
    int ped = Game::LocalPed(); Vec3 p; if(ped > 0 && Game::GetCharPos(ped, p)) { c.pos[0] = p.x; c.pos[1] = p.y; c.pos[2] = p.z; }
    if(text) MP_CopyStr(c.text, text, sizeof(c.text));
    client.Send(&c, sizeof(c));
}
static void DzNotice(uint32_t col, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Dz.notice, sizeof(s_Dz.notice), fmt, ap); va_end(ap);
    s_Dz.noticeCol = col; s_Dz.noticeAt = MP_NowMs();
}
// 3.61: the MTA DayZ model of an item (Host/DayZ/models on GitHub, src/game/dzmodels.cpp). Used only
// once that model is really in the game; until then (or with the setting off) the GTA model as before.
static const struct { uint8_t item; uint16_t mta; } kDzMtaItem[] = {
    { DZI_AMMO_PISTOL, 3013 }, { DZI_AMMO_SHELLS, 2033 }, { DZI_AMMO_SMG, 2041 }, { DZI_AMMO_RIFLE, 2035 }, { DZI_AMMO_SNIPER, 2034 },
    { DZI_BEANS, 2601 }, { DZI_BURGER, 2404 }, { DZI_PIZZA, 2770 }, { DZI_SODA, 2647 }, { DZI_WATER, 2683 }, { DZI_BEER, 2405 },
    { DZI_MEDKIT, 2891 }, { DZI_BANDAGE, 1578 }, { DZI_ANTIBIOTICS, 330 }, { DZI_PAINKILLERS, 2709 },
    { DZI_BAG1, 3026 }, { DZI_BAG2, 1644 }, { DZI_BAG3, 2382 },
    { DZI_GPS, 2976 }, { DZI_FUELCAN, 1650 }, { DZI_TOOLBOX, 2969 }, { DZI_MAP, 1277 }, { DZI_WATCH, 2710 },
    { DZI_TRASH_CAN, 2601 }, { DZI_TRASH_BOTTLE, 2683 },
    { DZI_MORPHINE, 958 }, { DZI_EPINEPHRINE, 2782 }, { DZI_BLOODBAG, 1580 }, { DZI_COMPASS, 1579 }, { DZI_HEATPACK, 1576 },
    { DZI_EMPTY_CAN_FUEL, 1650 }, { DZI_CLOTHES_CIV, 1247 }, { DZI_CLOTHES_CAMO, 1247 }, { DZI_CLOTHES_GHILLIE, 1247 }, { DZI_CLOTHES_SURV, 1247 },
    { DZI_MRE, 2740 }, { DZI_STEW, 2107 },
};
static int DzMtaModel(int item)
{
    for(const auto& m : kDzMtaItem) if(m.item == item) return DzModels::IsReplaced(m.mta) ? m.mta : 0;
    return 0;
}
static void DzModelsTick()
{
    static int all[sizeof(kDzMtaItem) / sizeof(kDzMtaItem[0]) + 8]; static int n = 0;
    if(!n) { for(const auto& m : kDzMtaItem) all[n++] = m.mta; for(int b : { 1253, 1254, 1272, 1273, 1274, 1275, 1313 }) all[n++] = b; }   // 4.08: the base building pieces (samp.img)
    const bool on = settings.dzModels && DzOnServer();
    static bool logSet = false;
    if(!logSet) { logSet = true; char lp[512]; snprintf(lp, sizeof(lp), "%s/dzmodels_log.txt", MP::DataDir()); DzModels::SetLogPath(lp); }   // 4.00
    DzModels::SetWorld(settings.dzWorldTex);   // 4.05.5
    DzModels::Tick(on, all, n);
    // a line when the download starts / ends (the files stay cached, next time it is instant)
    static int shown = 0; int done, total, mbD, mbT; bool busy; DzModels::Progress(done, total, mbD, mbT, busy);
    if(on && busy && total > 0 && done < total && shown == 0) shown = 1;   // 3.62: the download window / bar shows it
    if(on && !busy && total > 0 && shown == 1) { shown = 2; if(done == total) DzNotice(DZ_YEL, "MTA DayZ models ready (%d).", done); else DzNotice(DZ_RED, "MTA DayZ models: %d of %d downloaded.", done, total); }
}
// 4.00: /dzmodels - what the MTA model loader did (also in files/MP/dzmodels_log.txt)
static void DzModelsStatusLine(char* o, size_t n)
{
    DzModels::Status st; DzModels::GetStatus(st);
    const char* eng = !st.hooked ? "streamer hook missing" : !st.cache ? "no cache folder" : st.engine > 0 ? "engine ok" : st.engine == 0 ? "engine check waiting" : "engine check FAILED";
    if(st.engine < 0) snprintf(o, n, "MTA models: %s (%d) - models stay GTA. See files/MP/dzmodels_log.txt", eng, st.engine);
    else snprintf(o, n, "MTA models: %s | %d/%d downloaded | %d in game (%d loaded) | %d waiting (in use) | %d failed%s",
                  eng, st.files, st.wanted, st.swapped, st.loaded, st.waiting, st.failed, st.noTex ? " | some without textures" : "");
    int wm = 0, wh = 0; bool wr = false; DzModels::World(wm, wh, wr);   // 4.05.5
    size_t l = strlen(o); if(l < n) snprintf(o + l, n - l, " | world textures %s (%d GTA textures replaced)", !settings.dzWorldTex ? "off" : wr ? "ready" : "not downloaded", wm); (void)wh;
}
static void DzModelsReport(bool reset)
{
    if(reset) { DzModels::ResetBad(); SysMsg(COL_SYSTEM, "[Day Z] MTA models: skipped models reset - they are tried again."); }
    char l[256]; DzModelsStatusLine(l, sizeof(l));
    SysMsg(COL_SYSTEM, "[Day Z] %s", l);
    if(!settings.dzModels) SysMsg(COL_SYSTEM, "[Day Z] The MTA models setting is OFF (Day Z panel / MP settings).");
}
static int DzModel(const DzItem& d)
{
    if(&d >= g_DzItems && &d < g_DzItems + DZI_COUNT) { int m = DzMtaModel((int)(&d - g_DzItems)); if(m) return m; }   // 3.61
    if(d.model > 0 && Game::IsKnownModel(d.model)) return d.model;
    if(d.model2 > 0 && Game::IsKnownModel(d.model2)) return d.model2;
    return 0;
}

// ---- the logo: a badge with a human hand print and a zombie hand, then the title ----------------
static void DzDrawSymbol(ImDrawList* dl, ImVec2 c, float r, float t)
{
    dl->AddCircleFilled(c, r, IM_COL32(14, 12, 6, 235), 40);
    dl->AddCircle(c, r, DzCol(DZ_YEL, 0.95f), 40, r * 0.09f);
    dl->AddCircle(c, r * 0.84f, DzCol(DZ_YEL, 0.35f), 40, r * 0.03f);
    // human print (left, blood red): palm + 5 fingers
    auto hand = [&](ImVec2 o, float s, ImU32 col, float lean, bool claws)
    {
        dl->AddCircleFilled(ImVec2(o.x, o.y + s * 0.18f), s * 0.36f, col, 20);
        dl->AddRectFilled(ImVec2(o.x - s * 0.33f, o.y - s * 0.05f), ImVec2(o.x + s * 0.33f, o.y + s * 0.30f), col, s * 0.12f);
        const float fx[5] = { -0.30f, -0.12f, 0.06f, 0.24f, 0.44f }, fl[5] = { 0.42f, 0.55f, 0.58f, 0.50f, 0.30f };
        for(int i = 0; i < 5; ++i)
        {
            float bx = o.x + fx[i] * s, by = o.y - s * 0.05f, len = fl[i] * s;
            float ang = (i == 4 ? 0.9f : (i - 1.5f) * 0.16f) + lean;
            if(i == 4) by += s * 0.18f;
            ImVec2 tip(bx + sinf(ang) * len, by - cosf(ang) * len);
            dl->AddLine(ImVec2(bx, by), tip, col, s * 0.15f);
            dl->AddCircleFilled(tip, s * 0.075f, col, 10);
            if(claws) dl->AddTriangleFilled(ImVec2(tip.x - s * 0.05f, tip.y), ImVec2(tip.x + s * 0.05f, tip.y), ImVec2(tip.x + sinf(ang) * s * 0.16f, tip.y - cosf(ang) * s * 0.16f), IM_COL32(230, 230, 210, 255));
        }
    };
    float pulse = 0.85f + 0.15f * sinf(t * 2.2f);
    hand(ImVec2(c.x - r * 0.24f, c.y + r * 0.08f), r * 0.78f, IM_COL32((int)(170 * pulse), 18, 18, 245), -0.15f, false);
    hand(ImVec2(c.x + r * 0.26f, c.y + r * 0.14f), r * 0.70f, IM_COL32(74, (int)(150 * pulse), 52, 245), 0.22f, true);
    // drips under the print
    for(int i = 0; i < 3; ++i)
    {
        float dx = c.x - r * (0.42f - i * 0.16f), dy = c.y + r * 0.45f + fmodf(t * 0.25f + i * 0.31f, 1.0f) * r * 0.25f;
        dl->AddCircleFilled(ImVec2(dx, dy), r * 0.045f, IM_COL32(170, 18, 18, 220), 8);
    }
}
static void DzDrawLogo(ImDrawList* dl, float cx, float y, float scale, float t, float alpha = 1.0f)
{
    ImFont* pd = UI::PricedownFont(); ImFont* f = pd ? pd : UI::Font();
    const float k = pd ? 1.0f : 0.8f;
    const char* t1 = "Project Day Z"; const char* t3 = "LEGACY";
    float fs1 = UI::S(44) * scale * k, th = UI::S(2.6f) * scale, sym = fs1 * 0.62f;
    float w1 = f->CalcTextSizeA(fs1, FLT_MAX, 0, t1).x;
    float gap = UI::S(12) * scale, tw = sym * 2 + gap + w1;
    float x = cx - tw * 0.5f;
    DzDrawSymbol(dl, ImVec2(x + sym, y + fs1 * 0.55f), sym, t);
    float tx = x + sym * 2 + gap;
    RpGtaText(dl, f, fs1, ImVec2(tx, y), DzCol(DZ_YEL, alpha), t1, th);
    float fs3 = UI::S(15) * scale; float w3 = RpTextW(fs3, t3);
    float rx = tx + w1 - w3 - UI::S(4) * scale, ry = y + fs1 * 0.98f, pad = UI::S(8) * scale, rh = fs3 + UI::S(4) * scale;
    dl->AddQuadFilled(ImVec2(rx - pad, ry), ImVec2(rx + w3 + pad, ry - UI::S(1) * scale), ImVec2(rx + w3 + pad - UI::S(4) * scale, ry + rh), ImVec2(rx - pad + UI::S(3) * scale, ry + rh + UI::S(1) * scale), IM_COL32(255, 255, 255, (int)(235 * alpha)));
    dl->AddText(UI::Font(), fs3, ImVec2(rx, ry + UI::S(2) * scale), IM_COL32(60, 44, 4, (int)(255 * alpha)), t3);
}

// ---- cabin (connection screen) -----------------------------------------------------------------
static void DzCabinBase(Vec3& cam, Vec3& look) { cam = s_Dz.cabinOutdoor ? kDzCabinOutCam : kDzCabinCam; look = s_Dz.cabinOutdoor ? kDzCabinOutLook : kDzCabinLook; }
static void DzCabinPropsSetup()
{
    // relative to the look point: fireplace wall ahead, sofa between, lamps at the sides, rifles on the wall
    Vec3 cam, L; DzCabinBase(cam, L);
    float fx = L.x - cam.x, fy = L.y - cam.y, fl = sqrtf(fx * fx + fy * fy); if(fl < 0.01f) fl = 1; fx /= fl; fy /= fl;
    float rx = fy, ry = -fx;   // right
    float heading = atan2f(-fx, fy) * 57.29578f;
    auto at = [&](float fwd, float right, float up) { return Vec3{ L.x + fx * fwd + rx * right, L.y + fy * fwd + ry * right, L.z + up }; };
    int n = 0;
    auto add = [&](int m, int alt, Vec3 p, float rz, float rxr = 0) { if(n < 24) { s_Dz.props[n] = { m, alt, p, rz, rxr, 0 }; ++n; } };
    add(1463, 1458, at(1.10f,  0.00f, -0.55f), heading);            // the fireplace logs
    add(3929, 1463, at(1.55f,  0.00f, -0.55f), heading);            // stone hearth (fallback logs)
    add(1702, 1703, at(-0.40f, 0.00f, -0.95f), heading + 180.0f);   // the sofa, back to the camera
    add(1433, 1516, at(0.35f,  0.00f, -0.95f), heading);            // small table
    add(1486, 1543, at(0.30f, -0.20f, -0.45f), 0);                  // bottles on the table
    add(1543, 1486, at(0.38f,  0.15f, -0.45f), 0);
    add(1544, 1486, at(0.26f,  0.30f, -0.45f), 0);
    add(2108, 2069, at(0.20f, -1.90f, -0.95f), 0);                  // floor lamps
    add(2069, 2108, at(0.20f,  1.90f, -0.95f), 0);
    add(357,  355,  at(1.95f, -0.55f,  0.75f), heading + 90.0f, 75.0f);   // rifles on the wall, crossed
    add(355,  357,  at(1.95f,  0.55f,  0.75f), heading - 90.0f, -75.0f);
    add(1768, 1763, at(-0.10f, -1.40f, -0.95f), heading + 120.0f);  // armchair
    add(2357, 1433, at(0.95f,  1.35f, -0.95f), heading);            // side table
    add(1271, 1220, at(0.80f, -1.45f, -0.95f), heading + 20.0f);    // ammo box
    add(2061, 2037, at(0.85f, -1.40f, -0.45f), 0);
    s_Dz.nProps = n;
}
static void DzCabinTick()
{
    if(s_Dz.propsMade) return;
    if(!s_Dz.nProps) DzCabinPropsSetup();
    bool all = true;
    for(int i = 0; i < s_Dz.nProps; ++i)
    {
        DzProp& p = s_Dz.props[i]; if(p.obj) continue;
        int m = Game::IsKnownModel(p.model) ? p.model : (Game::IsKnownModel(p.alt) ? p.alt : 0);
        if(!m) { p.obj = -1; continue; }
        int o = Game::CreateMapObject(m, p.p, p.rx, 0, p.rz);
        if(o == 0) { all = false; continue; }
        p.obj = o;
    }
    if(!s_Dz.fire) { Vec3 cam, L; DzCabinBase(cam, L); Vec3 fp = L; float fx = L.x - cam.x, fy = L.y - cam.y, fl = sqrtf(fx * fx + fy * fy); if(fl > 0.01f) { fp.x += fx / fl * 1.15f; fp.y += fy / fl * 1.15f; } fp.z -= 0.45f; s_Dz.fire = Game::StartFireNet(fp, 0); }
    s_Dz.propsMade = all;
}
static void DzCabinClear()
{
    for(int i = 0; i < s_Dz.nProps; ++i) if(s_Dz.props[i].obj > 0) Game::DeleteWorldObject(s_Dz.props[i].obj);
    s_Dz.nProps = 0; s_Dz.propsMade = false;
    if(s_Dz.fire > 0) Game::RemoveFireNet(s_Dz.fire);
    s_Dz.fire = 0;
}
static void DzHudOff(uint64_t now, bool force)
{
    if(force || now - s_Dz.lastHudOff > 1000) { Game::ShowGameHud(false); Game::SetPlayerControl(false); s_Dz.lastHudOff = now; }
}
static void DzEnterCabin(int ped)
{
    logger->Info("DZ: cabin"); fflush(nullptr);
    if(ped <= 0) return;
    if(Game::IsCharInAnyCar(ped)) { Vec3 o; Game::GetCharPos(ped, o); o.z += 1.0f; Game::WarpOutOfCar(ped, o); }
    Vec3 cam, L; DzCabinBase(cam, L);
    Game::EnterArea(s_Dz.cabinOutdoor ? 0 : DZ_CABIN_AREA);
    Game::SetMaxWanted(0); Game::ClearWanted();
    Game::PreloadArea(L, true);
    Game::FreezeChar(ped, false);
    Game::PlaceChar(ped, { cam.x, cam.y, -100.0f });
    Game::FreezeChar(ped, true);
    Game::SetCharVisible(ped, false);
    Game::SetCharGodMode(ped, true);
    Game::SetFixedCamera(cam, L);
    if(!s_Dz.cabinOutdoor) { int h = 0, m = 0; Game::GetTimeOfDay(h, m); (void)h; }
    DzHudOff(MP_NowMs(), true);
    s_Dz.view = 1; s_Dz.page = 0; s_Dz.cabinAt = MP_NowMs();
}
static bool DzJoinTakeover(int ped)
{
    logger->Info("DZ: join takeover"); fflush(nullptr);
    if(!s_bDzJoin && !DzOnServer()) return false;
    s_bDzJoin = false;
    s_Dz.engaged = true; s_Dz.connectedAt = s_Dz.connectedAt ? s_Dz.connectedAt : MP_NowMs();
    DzEnterCabin(ped);
    return true;
}
static void DzJoinCam(Vec3& pos, Vec3& look) { pos = kDzCabinOutCam; look = kDzCabinOutLook; }

// ---- loot objects on the ground --------------------------------------------------------------
static void DzLootDelObj(DzLootC& l) { if(l.obj > 0) { Game::DeleteWorldObject(l.obj); --s_Dz.nLootObj; } l.obj = 0; }
static void DzLootClear() { for(auto& kv : s_Dz.loot) DzLootDelObj(kv.second); s_Dz.loot.clear(); s_Dz.nLootObj = 0; }
static void DzLootTick(const Vec3& me)
{
    // 3.60: freeze fix (Grove Street, the country side - places with a lot of loot). Every frame each loot
    // spot in 110 m without an object used to probe the ground (REQUEST_COLLISION + GET_GROUND_Z) and ask
    // for its model again - when the model / collision wasn't streamed yet that was dozens of streaming
    // calls per frame, again and again. Now: 10 times a second, max 4 tries per pass, the ground only for
    // spots within 70 m (collision is loaded there) and a failed spot waits 1.5 s before the next try.
    const uint64_t now = MP_NowMs();
    if(now < s_Dz.lootAt) return;
    s_Dz.lootAt = now + 100;
    int made = 0, tries = 0;
    for(auto& kv : s_Dz.loot)
    {
        DzLootC& l = kv.second;
        float dx = l.x - me.x, dy = l.y - me.y, d2 = dx * dx + dy * dy;
        if(d2 > DZ_LOOT_RANGE * DZ_LOOT_RANGE) { if(l.obj) DzLootDelObj(l); continue; }
        if(l.obj || !l.item) continue;
        if(s_Dz.nLootObj >= DZ_LOOT_OBJ_MAX || made >= 3 || tries >= 4) continue;
        if(now < l.tryAt) continue;
        if(!l.zDone)
        {
            if(d2 > 70.0f * 70.0f) continue;   // the collision there is not loaded yet - wait until closer
            ++tries;
            float z = Game::GroundZBelow(l.x, l.y, me.z + 30.0f);
            if(z < -500.0f) { l.tryAt = now + 1500; if(++l.tries < 6) continue; z = Game::GroundZ(l.x, l.y, me.z); }
            l.z = z; l.zDone = true;
        }
        int m = DzModel(DzItemInfo(l.item)); if(!m) { l.obj = -1; continue; }
        ++tries;
        float bottom = Game::ModelBottom(m); if(bottom > 0.5f || bottom < -2.0f) bottom = -0.05f;
        int o = Game::CreateMapObject(m, { l.x, l.y, l.z - bottom + 0.02f }, DzItemInfo(l.item).kind == DZK_WEAPON ? 90.0f : 0.0f, 0, (float)((l.idx * 47) % 360));
        if(o > 0) { l.obj = o; ++s_Dz.nLootObj; ++made; Game::SetObjectCollision(o, false); }
        else if(o < 0) l.obj = -1;
        else l.tryAt = now + 400;   // model asked for, not streamed yet - try again a bit later
    }
}
static int DzNearestLoot(const Vec3& me, float r, float* outD = nullptr)
{
    int best = -1; float bd = r * r;
    for(auto& kv : s_Dz.loot)
    {
        const DzLootC& l = kv.second; if(!l.item) continue;
        float dx = l.x - me.x, dy = l.y - me.y, dz = l.zDone ? l.z - me.z + 1.0f : 0.0f, d2 = dx * dx + dy * dy + dz * dz;
        if(d2 < bd) { bd = d2; best = l.idx; }
    }
    if(outD) *outD = sqrtf(bd);
    return best;
}
static void DzStartPick(int idx)
{
    int ped = Game::LocalPed(); if(ped <= 0 || idx < 0 || s_Dz.pickIdx >= 0) return;
    if(Game::IsCharInAnyCar(ped)) { DzNotice(DZ_GRY, "Get out of the vehicle to pick it up."); return; }
    s_Dz.pickIdx = idx; s_Dz.pickAt = MP_NowMs(); s_Dz.pickAnim = Game::PlayAnimOnce(ped, "liftup", "CARRY", 4.0f, false, 1100);   // both hands
}
static void DzPickTick(int ped, uint64_t now)
{
    if(s_Dz.pickIdx < 0) return;
    if(!s_Dz.pickAnim && now - s_Dz.pickAt < 600) s_Dz.pickAnim = Game::PlayAnimOnce(ped, "liftup", "CARRY", 4.0f, false, 1100);
    if(now - s_Dz.pickAt >= 850)
    {
        DzSend(DZCMD_PICK, 0, 0, s_Dz.pickIdx);
        s_Dz.pickIdx = -1;
    }
}

// ---- weapons -----------------------------------------------------------------------------------
static void DzGiveWeapon(int w, int ammo, bool ammoOnly)
{
    int ped = Game::LocalPed(); if(ped <= 0 || w <= 0) return;
    if(ammoOnly && Game::GetAmmo(ped, w) <= 0) { DzNotice(DZ_GRY, "You need the gun for that ammo."); return; }
    if(!Game::GiveWeaponAmmo(ped, w, ammo)) { s_Dz.pendW.push_back({ w, ammo, ammoOnly, MP_NowMs() }); return; }
    if(!ammoOnly) Game::SetCurrentWeapon(ped, w);
}
static void DzWeaponsTick()
{
    if(s_Dz.pendW.empty()) return;
    int ped = Game::LocalPed(); if(ped <= 0) return;
    for(size_t i = 0; i < s_Dz.pendW.size();)
    {
        auto& p = s_Dz.pendW[i];
        if(Game::GiveWeaponAmmo(ped, p.w, p.ammo) || MP_NowMs() - p.t > 10000) { if(!p.ammoOnly) Game::SetCurrentWeapon(ped, p.w); s_Dz.pendW.erase(s_Dz.pendW.begin() + i); }
        else ++i;
    }
}
static void DzTakeWeapon(int w)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    int keepW[13], keepA[13], n = 0;
    for(int s = 1; s <= 12; ++s) { int ww = 0, aa = 0; if(Game::GetWeaponInSlot(ped, s, ww, aa) && ww > 0 && ww != w && aa > 0) { keepW[n] = ww; keepA[n] = aa; ++n; } }
    Game::RemoveAllWeapons(ped);
    for(int i = 0; i < n; ++i) DzGiveWeapon(keepW[i], keepA[i], false);
    Game::SetCurrentWeapon(ped, 0);
}
static int DzMagSize(int w)
{
    switch(w) { case 22: return 17; case 23: return 17; case 24: return 7; case 25: return 1; case 26: return 2; case 27: return 7;
                case 28: case 32: return 50; case 29: return 30; case 30: case 31: return 30; case 33: case 34: return 1; default: return 0; }
}

// ---- 3.63: Project Day Z ambient music ---------------------------------------------------------
// "No Man's Land | Metro / S.T.A.L.K.E.R Atmospheric Playlist" by Lemuria
// (https://www.youtube.com/@lemurian.archives - https://youtu.be/aVTcVx1570w). Starts after you press
// Play and tap a location on the map, fades in, plays to 1:00:02, fades out, waits 17 s + 20 ticks,
// then starts again (endless). The track is not shipped with MP (it is Lemuria's): put your own copy
// at files/MP/dz_ambient.mp3. /music turns it on / off.
#define DZ_AMB_FILE      "dz_ambient.mp3"
#define DZ_AMB_LEN_S     3602.0f              // 1:00:02
#define DZ_AMB_FADEIN_S  4.0f
#define DZ_AMB_FADEOUT_S 10.0f
#define DZ_AMB_GAP_MS    (17000 + 20 * 50)     // 17 s + 20 ticks (50 ms)
#define DZ_AMB_VOLUME    0.40f
static const char* kDzAmbTitle  = "No Man's Land | Metro / S.T.A.L.K.E.R Atmospheric Playlist";
static const char* kDzAmbArtist = "Lemuria";
static const char* kDzAmbLink   = "youtu.be/aVTcVx1570w";
struct DzAmbient
{
    int      phase = 0;        // 0 off, 1 playing, 2 fading out, 3 waiting for the next loop
    bool     armed = false, muted = false, missingSaid = false, creditSaid = false;
    uint64_t phaseAt = 0, cardAt = 0, retryAt = 0; bool logged = false;
    float    fadeFrom = 0.0f;
};
static DzAmbient s_DzAmb;
// 4.00.1: find the music file even with another name ("dz_ambient.mp3.mp3", "DZ_Ambient.MP3",
// "dz_ambient (1).mp3", ".m4a" ...), in files/MP, files/MP/music or files/. Everything goes to
// files/MP/dz_music_log.txt so you can see why it does not play.
static void DzAmbLog(const char* fmt, ...)
{
    char path[512]; snprintf(path, sizeof(path), "%s/dz_music_log.txt", MP::DataDir());
    static bool fresh = true; FILE* f = fopen(path, fresh ? "w" : "a"); fresh = false; if(!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap); fputc('\n', f); fclose(f);
    va_start(ap, fmt); char line[400]; vsnprintf(line, sizeof(line), fmt, ap); va_end(ap); logger->Info("DZ music: %s", line);
}
static bool DzAmbAudioExt(const char* n)
{
    const char* d = strrchr(n, '.'); if(!d) return false;
    return !strcasecmp(d, ".mp3") || !strcasecmp(d, ".m4a") || !strcasecmp(d, ".aac") || !strcasecmp(d, ".ogg") || !strcasecmp(d, ".opus") || !strcasecmp(d, ".webm") || !strcasecmp(d, ".wav") || !strcasecmp(d, ".flac") || !strcasecmp(d, ".mp4");
}
static bool DzAmbFind(char* out, size_t n)
{
    char dirs[3][512];
    snprintf(dirs[0], 512, "%s", MP::DataDir());
    snprintf(dirs[1], 512, "%s/music", MP::DataDir());
    snprintf(dirs[2], 512, "%s", MP::DataDir()); { char* sl = strrchr(dirs[2], '/'); if(sl) *sl = 0; }   // files/
    snprintf(out, n, "%s/" DZ_AMB_FILE, dirs[0]);
    { FILE* f = fopen(out, "rb"); if(f) { fclose(f); return true; } }
    for(int pass = 0; pass < 2; ++pass)   // 1st: names starting with "dz_ambient", 2nd: "*ambient*" / "*no man*" audio
        for(int d = 0; d < 3; ++d)
        {
            DIR* dir = opendir(dirs[d]); if(!dir) continue;
            struct dirent* e; bool hit = false;
            while((e = readdir(dir)) != nullptr)
            {
                const char* nm = e->d_name; if(nm[0] == '.') continue;
                char lo[256]; size_t i = 0; for(; nm[i] && i < sizeof(lo) - 1; ++i) lo[i] = (char)tolower((unsigned char)nm[i]); lo[i] = 0;
                bool ok = pass == 0 ? (!strncmp(lo, "dz_ambient", 10) || !strncmp(lo, "dz ambient", 10) || !strncmp(lo, "dzambient", 9))
                                    : (DzAmbAudioExt(lo) && (strstr(lo, "ambient") || strstr(lo, "no man") || strstr(lo, "no_man") || strstr(lo, "lemuria")));
                if(ok && (pass == 1 || DzAmbAudioExt(lo) || strstr(lo, ".mp3"))) { snprintf(out, n, "%s/%s", dirs[d], nm); hit = true; break; }
            }
            closedir(dir);
            if(hit) return true;
        }
    snprintf(out, n, "%s/" DZ_AMB_FILE, dirs[0]);
    return false;
}
static bool DzAmbStart(uint64_t now)
{
    char path[512]; bool found = DzAmbFind(path, sizeof(path));
    s_DzAmb.retryAt = 0;
    if(!found)
    {   // 4.07: not in files/MP -> the media player streams it from the repo (Host/Media) over Wi-Fi / mobile data
        if(Sound::StreamPlayUrl(MP_MEDIA_URL DZ_AMB_FILE, 0.0f))
        {
            DzAmbLog("not in files/MP - streaming %s", MP_MEDIA_URL DZ_AMB_FILE);
            s_DzAmb.phase = 1; s_DzAmb.phaseAt = now; s_DzAmb.cardAt = now; s_DzAmb.logged = false;
            return true;
        }
        s_DzAmb.phase = 0; s_DzAmb.retryAt = now + 20000;   // put the file in while playing: it starts by itself
        if(!s_DzAmb.missingSaid)
        {
            s_DzAmb.missingSaid = true;
            DzAmbLog("not found: %s (also looked in files/MP/music and files/)", path);
            SysMsg(COL_SYSTEM, "[Day Z] Ambient music: no " DZ_AMB_FILE " in %s", MP::DataDir());
        }
        return false;
    }
    if(!Sound::StreamPlay(path, 0.0f))
    {
        s_DzAmb.phase = 0; s_DzAmb.retryAt = now + 60000;
        if(!s_DzAmb.missingSaid)
        {
            s_DzAmb.missingSaid = true;
            DzAmbLog("can't play %s: %s", path, Sound::StreamInfo());
            SysMsg(COL_SYSTEM, "[Day Z] Ambient music can't play: %s (see files/MP/dz_music_log.txt)", Sound::StreamInfo());
        }
        return false;
    }
    DzAmbLog("start %s: %s", path, Sound::StreamInfo());
    s_DzAmb.phase = 1; s_DzAmb.phaseAt = now; s_DzAmb.cardAt = now; s_DzAmb.logged = false;
    // 4.00.1: no "now playing" card / chat line anymore (the credit is in /music and /credits)
    return true;
}
static void DzAmbStop()
{
    if(s_DzAmb.phase) Sound::StreamStop();
    s_DzAmb = DzAmbient();
}
static void DzAmbArm()   // after the spawn (Play + a location tapped)
{
    if(s_DzAmb.armed) return;
    s_DzAmb.armed = true;
    if(!s_DzAmb.muted) DzAmbStart(MP_NowMs());
}
static void DzAmbToggle()
{
    s_DzAmb.muted = !s_DzAmb.muted;
    if(s_DzAmb.muted) { if(s_DzAmb.phase) Sound::StreamStop(); s_DzAmb.phase = 0; DzNotice(DZ_GRY, "Ambient music off."); }
    else { s_DzAmb.missingSaid = false; if(s_DzAmb.armed) DzAmbStart(MP_NowMs()); DzNotice(DZ_GRY, "Ambient music on."); }
}
static void DzAmbTick(uint64_t now)
{
    if(!s_DzAmb.armed || s_DzAmb.muted) return;
    if(s_DzAmb.phase == 0) { if(s_DzAmb.retryAt && now >= s_DzAmb.retryAt) DzAmbStart(now); return; }
    Sound::StreamUpdate();
    const float sec = (float)(now - s_DzAmb.phaseAt) / 1000.0f;
    if(s_DzAmb.phase == 1)
    {
        if(!s_DzAmb.logged && sec > 3.0f)
        {   // 4.00.1: what really happened after 3 s
            s_DzAmb.logged = true;
            DzAmbLog("after 3 s: %s, active %d, at %.1f s of %.0f s", Sound::StreamInfo(), Sound::StreamActive() ? 1 : 0, Sound::StreamPosition(), Sound::StreamDuration());
            if(!Sound::StreamActive() && !Sound::StreamEnded())
                SysMsg(COL_SYSTEM, "[Day Z] Ambient music stopped: %s (see files/MP/dz_music_log.txt)", Sound::StreamInfo());
        }
        float pos = Sound::StreamPosition(), dur = Sound::StreamDuration();
        float end = DZ_AMB_LEN_S; if(dur > 60.0f && dur < end) end = dur;
        float k = sec / DZ_AMB_FADEIN_S; if(k > 1) k = 1;
        Sound::StreamSetVolume(DZ_AMB_VOLUME * k);
        if(pos >= end - DZ_AMB_FADEOUT_S || Sound::StreamEnded() || (!Sound::StreamActive() && sec > 5.0f))
        { s_DzAmb.phase = 2; s_DzAmb.phaseAt = now; s_DzAmb.fadeFrom = DZ_AMB_VOLUME * k; }
    }
    else if(s_DzAmb.phase == 2)
    {
        float k = 1.0f - sec / DZ_AMB_FADEOUT_S;
        if(k <= 0 || !Sound::StreamActive()) { Sound::StreamStop(); s_DzAmb.phase = 3; s_DzAmb.phaseAt = now; }
        else Sound::StreamSetVolume(s_DzAmb.fadeFrom * k);
    }
    else if(s_DzAmb.phase == 3)
    {
        if(now - s_DzAmb.phaseAt >= DZ_AMB_GAP_MS) DzAmbStart(now);
    }
}
static void DzAmbFmt(char* o, size_t n, float s) { int t = (int)s; if(t < 0) t = 0; if(t >= 3600) snprintf(o, n, "%d:%02d:%02d", t / 3600, t / 60 % 60, t % 60); else snprintf(o, n, "%d:%02d", t / 60, t % 60); }
static void DzAmbDraw(ImDrawList* dl, ImVec2 ds, uint64_t now)
{   // "now playing" card for 9 s after every (re)start, top center under the spawn name
    if(s_DzAmb.phase != 1 || !s_DzAmb.cardAt || now - s_DzAmb.cardAt > 9000) return;
    float age = (float)(now - s_DzAmb.cardAt) / 1000.0f;
    float a = age < 0.4f ? age / 0.4f : (age > 8.2f ? (9.0f - age) / 0.8f : 1.0f); if(a < 0) a = 0;
    const float w = UI::S(470), h = UI::S(96), x = (ds.x - w) * 0.5f, y = ds.y * 0.29f, c = UI::S(12);
    ImVec2 pts[6] = { ImVec2(x + c, y), ImVec2(x + w, y), ImVec2(x + w, y + h - c), ImVec2(x + w - c, y + h), ImVec2(x, y + h), ImVec2(x, y + c) };
    dl->AddConvexPolyFilled(pts, 6, IM_COL32(10, 20, 30, (int)(190 * a)));
    dl->AddPolyline(pts, 6, IM_COL32(236, 232, 225, (int)(60 * a)), ImDrawFlags_Closed, UI::S(1));
    dl->AddRectFilled(ImVec2(x, y + c), ImVec2(x + UI::S(4), y + h), IM_COL32(255, 70, 85, (int)(230 * a)));
    // equalizer bars
    for(int i = 0; i < 4; ++i)
    {
        float bh = UI::S(10) + UI::S(18) * (0.5f + 0.5f * sinf(age * (5.0f + i * 1.7f) + i));
        float bx = x + UI::S(18) + i * UI::S(9);
        dl->AddRectFilled(ImVec2(bx, y + UI::S(44) - bh), ImVec2(bx + UI::S(6), y + UI::S(44)), IM_COL32(64, 230, 190, (int)(230 * a)));
    }
    const float tx = x + UI::S(66);
    UI::OutlinedText(dl, UI::S(13), ImVec2(tx, y + UI::S(8)), IM_COL32(64, 230, 190, (int)(255 * a)), "NOW PLAYING  -  DAY Z AMBIENT");
    UI::OutlinedText(dl, UI::S(17), ImVec2(tx, y + UI::S(26)), IM_COL32(236, 232, 225, (int)(255 * a)), "No Man's Land | Metro / S.T.A.L.K.E.R");
    char by[160]; snprintf(by, sizeof(by), "by %s  -  %s", kDzAmbArtist, kDzAmbLink);
    UI::OutlinedText(dl, UI::S(13), ImVec2(tx, y + UI::S(48)), IM_COL32(190, 196, 204, (int)(230 * a)), by);
    float pos = Sound::StreamPosition(), len = DZ_AMB_LEN_S, dur = Sound::StreamDuration(); if(dur > 60.0f && dur < len) len = dur;
    float k = pos / len; if(k > 1) k = 1;
    const float bx0 = tx, bx1 = x + w - UI::S(90), by0 = y + UI::S(76);
    dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx1, by0 + UI::S(3)), IM_COL32(236, 232, 225, (int)(50 * a)));
    dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(bx0 + (bx1 - bx0) * k, by0 + UI::S(3)), IM_COL32(255, 70, 85, (int)(255 * a)));
    char t0[16], t1[16], tt[40]; DzAmbFmt(t0, sizeof(t0), pos); DzAmbFmt(t1, sizeof(t1), len); snprintf(tt, sizeof(tt), "%s / %s", t0, t1);
    UI::OutlinedText(dl, UI::S(12), ImVec2(bx1 + UI::S(8), by0 - UI::S(6)), IM_COL32(190, 196, 204, (int)(230 * a)), tt);
}

// ---- spawn / death -----------------------------------------------------------------------------
static void DzMakeBag(int ped)
{
    static int s_BagModel = 0;
    int lvl = s_Dz.haveMe ? s_Dz.me.bag : 0;
    int m = Game::IsKnownModel(3026) ? 3026 : (Game::IsKnownModel(2919) ? 2919 : 0);
    // 3.61: MTA DayZ packs (Patrol 3026, Assault 1644, ALICE 2382)
    const int mta = lvl > 0 ? DzMtaModel(lvl == 3 ? DZI_BAG3 : (lvl == 2 ? DZI_BAG2 : DZI_BAG1)) : 0;
    if(mta) m = mta;
    if(s_Dz.bagObj && (lvl != s_Dz.bagLvl || m != s_BagModel || !Game::WorldObjectExists(s_Dz.bagObj))) { BoneAttach::Stop(s_Dz.bagObj); Game::DeleteWorldObject(s_Dz.bagObj); s_Dz.bagObj = 0; }
    if(s_Dz.bagObj && !BoneAttach::Calibrated(s_Dz.bagObj) && ped > 0 && MP_NowMs() - s_Dz.bagAt > 700 && !Game::IsCharInAnyCar(ped) && Game::GetCharSpeed(ped) < 0.2f && !Game::IsCharDucking(ped) && !DzXProne() && !DzXDowned())
        BoneAttach::Calibrate(s_Dz.bagObj);   // 4.00: standing still: from now on it follows the spine bone
    s_Dz.bagLvl = lvl;
    if(lvl <= 0 || s_Dz.bagObj || ped <= 0 || s_Dz.view != 2 || !m) return;
    Vec3 p; Game::GetCharPos(ped, p);
    int o = Game::CreateWorldObject(m, p); if(o <= 0) return;
    s_BagModel = m;
    Game::SetObjectCollision(o, false);
    float back = lvl == 3 ? -0.20f : (lvl == 2 ? -0.17f : -0.14f);
    // 3.60: the parachute pack (3026) is made for the spine bone (its X axis points up the back), so it
    // lay sideways at the hip - turn it -90 around Y so it stands upright, straps to the body
    // 3.62: placed like MTA DayZ does it (backpack_player.lua: spine bone, 0 / -0.225 / 0.05, rot 90 0 0).
    // Those models lie on their back - the Y axis runs up the spine, Z points away from the body - so
    // they are turned +90 around X to stand upright; the spine bone sits ~0.17 above the ped origin.
    if(mta) Game::AttachObjectToCharRot(o, ped, { 0.0f, -0.225f, 0.22f }, { 90.0f, 0.0f, 0.0f });
    else Game::AttachObjectToCharRot(o, ped, { 0.0f, back, 0.12f + 0.03f * lvl }, { 0.0f, -90.0f, 0.0f });   // on the back, bigger bag sits higher
    s_Dz.bagObj = o; s_Dz.bagAt = MP_NowMs();
    // 4.05: the calibration is kept per model / level (dz_bagcal.bin) - a bag made in a car or prone follows at once
    { static bool calSet = false; if(!calSet) { calSet = true; char cp[512]; snprintf(cp, sizeof(cp), "%s/dz_bagcal.bin", MP::DataDir()); BoneAttach::SetCalFile(cp); } }
    BoneAttach::Follow(o, ped, 3, m * 10 + lvl);   // 4.00: spine1 - moves with the torso, no lag in cars
}
static void DzSpawnAt(const float* p, const char* name)
{
    int ped = Game::LocalPed(); if(ped <= 0) return;
    logger->Info("DZ: spawn %s", name); fflush(nullptr);
    DzCabinClear();
    DzXSpawned();
    Vec3 at = { p[0], p[1], p[2] };
    // 3.60: never wake up on a roof / under the map from a bad saved spot (the old cabin position)
    if(at.z < -20.0f || at.z > 700.0f) { float g = Game::GroundZ(at.x, at.y, 60.0f); at.z = g > -50.0f && g < 700.0f ? g + 1.0f : 30.0f; }
    Game::EnterArea(0);
    Game::PreloadArea(at, true);
    Game::FreezeChar(ped, false);
    Game::SetCharVisible(ped, !s_Cheat.invisible);
    Game::SetCharGodMode(ped, false);
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::ShowRadar(false); s_Dz.radarHidden = true;   // the DayZ radar replaces it (GPS)
    Game::TeleportGround(ped, at, p[3]);
    Game::SetMaxWanted(0); Game::ClearWanted();
    s_Dz.view = 2; s_Dz.spawnedAt = MP_NowMs(); s_Dz.prevHp = -1; s_Dz.armourGiven = 0;
    MP_CopyStr(s_Dz.spawnName, name ? name : "", sizeof(s_Dz.spawnName)); s_Dz.spawnNameAt = MP_NowMs();
    if(s_Dz.fog) { s_Dz.fog = false; Game::SetFogRadius(0); Game::ReleaseWeather(); }   // 4.00.1: no fog
    DzAmbArm();   // 3.63: the ambient music starts with the first spawn
}
static void DzShutdown()
{
    logger->Info("DZ: left"); fflush(nullptr);
    DzAmbStop();   // 3.63
    DzCabinClear();
    DzLootClear();
    DzBaseClear();   // 4.08
    for(auto& z : s_Dz.zeds) if(z.ped > 0) Game::DeleteChar(z.ped);
    for(int o : s_Dz.ruinObjs) if(o > 0) Game::DeleteWorldObject(o);
    if(s_Dz.bagObj) { BoneAttach::Stop(s_Dz.bagObj); Game::DeleteWorldObject(s_Dz.bagObj); }
    if(s_Dz.fog) { Game::SetFogRadius(0); Game::ReleaseWeather(); }
    SaRouteClear();
    DzXReset();
    if(!s_Dz.engaged) { s_Dz = DzClientState(); s_bDzJoin = false; return; }
    int ped = Game::LocalPed();
    if(ped > 0 && Game::IsReady())
    {
        Game::EnterArea(0);
        Game::FreezeChar(ped, false);
        Game::SetCharVisible(ped, !s_Cheat.invisible);
        Game::SetCharGodMode(ped, false);
        if(s_Dz.view == 1 || s_Dz.view == 3) Game::Teleport(ped, kGroveStreet, kGroveHeading);
    }
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true); Game::ShowRadar(true);
    Game::SetMaxWanted(6);
    s_Dz = DzClientState(); s_DzDlSkip = false;
    s_bDzJoin = false;
}

// ---- packets -----------------------------------------------------------------------------------
static void DzLocalCmd(const char* cmd);
static void DzOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    const uint64_t now = MP_NowMs();
    switch(d[0])
    {
        case PKT_DZ_ME:
            if(len < DZ_ME_OLD_SIZE) return;   // 3.59: older servers send the short packet (the rest stays 0)
            memset(&s_Dz.me, 0, sizeof(PktDzMe)); memcpy(&s_Dz.me, d, std::min<int>(len, sizeof(PktDzMe))); s_Dz.haveMe = true;
            if(s_Dz.me.combat) s_Dz.combatUntil = std::max<uint64_t>(s_Dz.combatUntil, now + s_Dz.me.combat * 1000ULL);
            break;
        case PKT_DZ_INV:
            if(len < 4) return;
            memset(&s_Dz.inv, 0, sizeof(s_Dz.inv)); memcpy(&s_Dz.inv, d, std::min<int>(len, sizeof(PktDzInv))); s_Dz.haveInv = true;
            if(s_Dz.inv.n > DZ_INV_MAX) s_Dz.inv.n = DZ_INV_MAX;
            if(s_Dz.invSel >= s_Dz.inv.n) s_Dz.invSel = -1;
            break;
        case PKT_DZ_LOOT:
        {
            if(len < 4) return;
            const PktDzLoot* L = (const PktDzLoot*)d; int n = std::min<int>(L->n, DZ_LOOT_PAGE);
            if(len < 4 + n * (int)sizeof(DzLootRow)) n = (len - 4) / (int)sizeof(DzLootRow);
            for(int i = 0; i < n; ++i)
            {
                DzLootRow r; memcpy(&r, &L->r[i], sizeof(r));
                auto it = s_Dz.loot.find(r.idx);
                if(!r.item) { if(it != s_Dz.loot.end()) { DzLootDelObj(it->second); s_Dz.loot.erase(it); } continue; }
                if(it != s_Dz.loot.end())
                {
                    DzLootC& l = it->second;
                    if(l.item != r.item || fabsf(l.x - r.x) > 0.3f || fabsf(l.y - r.y) > 0.3f) { DzLootDelObj(l); l.zDone = false; }
                    l.item = r.item; l.count = r.count; l.x = r.x; l.y = r.y;
                }
                else s_Dz.loot[r.idx] = { r.idx, r.item, r.count, r.x, r.y, 0.0f, 0, false };
            }
            break;
        }
        case PKT_DZ_LIST: DzXList(d, len); break;
        case PKT_DZ_BASE: DzBaseOnPacket(d, len); break;   // 4.08
        case PKT_DZ_EVENT:
        {
            if(len < (int)offsetof(PktDzEvent, text)) return;
            PktDzEvent e; memset(&e, 0, sizeof(e)); memcpy(&e, d, std::min<int>(len, sizeof(e))); e.text[sizeof(e.text) - 1] = 0;
            int ped = Game::LocalPed();
            switch(e.ev)
            {
                case DZEV_SPAWN: DzSpawnAt(e.pos, e.text); break;
                case DZEV_FOG:
                    s_Dz.fog = false;   // 4.00.1: no fog, even if an older server sends one
                    if(s_Dz.fog) { Game::SetFogRadius((float)(e.val > 0 ? e.val : 90)); Game::ForceWeatherNow(9); if(e.text[0]) DzNotice(DZ_ORG, "%s", e.text); }
                    else { Game::SetFogRadius(0); Game::ReleaseWeather(); }
                    break;
                case DZEV_WEAPON: DzGiveWeapon(e.a, e.val, e.b == 1); break;
                case DZEV_TAKEWEAPON: DzTakeWeapon(e.a); break;
                case DZEV_HEAL:
                    if(ped <= 0) break;
                    if(e.val < 0) { Game::SetCharHealth(ped, 0); break; }
                    { int hp = Game::GetCharHealth(ped) + e.val / 120; if(hp > 100) hp = 100; Game::SetCharHealth(ped, hp); s_Dz.prevHp = hp;
                      if(e.val >= 120) s_Dz.nums.push_back({ {0,0,0}, false, 0, 0, e.val / 120, now, true }); }
                    break;
                case DZEV_NOTICE: DzNotice(e.a ? (uint32_t)e.val : DZ_YEL, "%s", e.text); break;
                case DZEV_RELOG: s_Dz.combatUntil = now + (uint64_t)std::max(1, e.val) * 1000ULL; break;
                case DZEV_PICKED:
                {
                    auto it = s_Dz.loot.find((uint16_t)e.val);
                    if(it != s_Dz.loot.end()) { DzLootDelObj(it->second); s_Dz.loot.erase(it); }
                    DzNotice(DZ_GRN, "Picked up: %s%s", e.text, e.b > 1 ? " (x several)" : "");
                    break;
                }
                case DZEV_UI:
                    if(e.a == 6) DzLocalCmd(e.text);
                    else if(e.a == 7) DzXJournal(e.b);
                    else if(e.a == 2) DzShowRules();   // 3.60: /rules = the 10 rules window
                    else if(e.a == 5 && s_Dz.view == 1) s_Dz.page = 2;
                    else s_Dz.uiWin = e.a;
                    break;
                case DZEV_FUEL:
                {
                    int car = 0; Vec3 p;
                    if(ped > 0 && Game::IsCharInAnyCar(ped)) car = Game::GetCarCharIsIn(ped);
                    else if(ped > 0 && Game::GetCharPos(ped, p)) car = Game::FindNearestCar(p, 6.0f);
                    if(car <= 0) { DzNotice(DZ_GRY, "No vehicle near you - the canister is wasted."); break; }
                    float& f = s_Dz.fuel[car]; f = std::min(100.0f, f + (float)e.val);
                    DzNotice(DZ_GRN, "Fuel canister poured: tank %d%%", (int)f);
                    break;
                }
                case DZEV_DIED:
                    { DzXReset(); if(ped > 0) { Game::FreezeChar(ped, false); Game::SetPlayerControl(true); } }
                    s_Dz.view = 3; s_Dz.diedAt = now; s_Dz.invOpen = false; s_Dz.panelOpen = false;
                    DzNotice(DZ_RED, "%s", e.text[0] ? e.text : "YOU ARE DEAD");
                    break;
                default: DzXEvent(e); break;
            }
            break;
        }
    }
}

// ---- zombies (local to this phone: they hunt you, kills are reported to the server) -------------
static const int kDzZedSkins[] = { 200, 78, 79, 134, 135, 137, 160, 213 };   // 4.00: not 162 / 212 / 230 (MTA zombie skins: a GTA zombie in use kept them GTA)
// 3.61: MTA DayZ zombie skins (models/zombies, loadskins.lua)
static const int kDzMtaZeds[] = { 56, 67, 68, 69, 75, 92, 97, 105, 107, 108, 126, 127, 128, 152, 162, 167, 188, 195, 206, 209, 212, 229, 230, 258, 264, 277, 280 };
static bool DzZedSafeHere(const Vec3& me)
{
    for(int i = 0; i < 15; ++i) { float dx = g_DzSpawns[i].x - me.x, dy = g_DzSpawns[i].y - me.y; if(dx * dx + dy * dy < 30.0f * 30.0f && MP_NowMs() - s_Dz.spawnedAt < 60000) return true; }
    return false;
}
// 4.00: zombie animations (instead of the GTA fist fight). A zombie set needs: shamble walk, lurch run,
// idle sway, alert / turn, claw swipes left + right, an overhead smash, a lunge / grab, hit stagger, death.
// GTA SA has no zombie .ifp, so these are the closest stock anims (all of them exist on mobile too):
// walk styles from animgrp.dat (drunkman = staggering, oldman / oldfatman = slow limp, shuffle = dragging feet)
static const char* const kDzZedWalk[] = { "drunkman", "oldman", "shuffle", "drunkman", "oldman" };   // 4.00.2: names of max 8 letters (the game reads 8)
struct DzZedAnim { const char* file; const char* anim; int ms; int hitMs; };
static const DzZedAnim kDzZedAtk[] = {
    { "ped",      "BIKE_elbowL", 900, 380 },   // claw swipe, left arm
    { "ped",      "BIKE_elbowR", 900, 380 },   // claw swipe, right arm
    { "BASEBALL", "Bat_4",      1100, 520 },   // both arms smash down
    { "KNIFE",    "knife_3",    1000, 450 },   // lunge forward, grab
    { "KNIFE",    "KILL_Knife_Ped_Damage", 1200, 600 },   // grab + bite (the victim side of a knife kill, a lurch)
};
static const DzZedAnim kDzZedIdle[] = {
    { "RYDER", "RYD_Die_PT1", 2600, 0 },   // MTA DayZ zombie stance: clutching, swaying
    { "CRACK", "crckidle1",   2400, 0 },   // head hanging, wobbling
    { "ped",   "roadcross",   1800, 0 },   // looks around (sniffing / tracking)
    { "CRACK", "crckidle4",   2400, 0 },
};

static void DzZedTick(int ped, const Vec3& me, uint64_t now)
{   // 3.59 (MTA DayZ): zombies wander until they hear / see you (sound + visibility levels), sprinters
    // on veteran / hardcore, spawn around the loot spots, damage by the difficulty + skills
    const int myHp = Game::GetCharHealth(ped);
    const int diff = s_Dz.haveMe ? s_Dz.me.difficulty : 0;
    const float detect = DzZedDetectRange(ped);
    for(size_t i = 0; i < s_Dz.zeds.size();)
    {
        DzZed& z = s_Dz.zeds[i];
        Vec3 zp; bool ok = z.ped > 0 && Game::GetCharPos(z.ped, zp);
        if(!ok) { s_Dz.zeds.erase(s_Dz.zeds.begin() + i); continue; }
        float dx = zp.x - me.x, dy = zp.y - me.y, d = sqrtf(dx * dx + dy * dy);
        bool dead = Game::IsCharDead(z.ped);
        if(dead && !z.reported)
        {
            z.reported = true; z.deadAt = now;
            if(Game::HasCharBeenDamagedByChar(z.ped, ped))
            {
                int w = Game::GetCharWeapon(ped);
                DzSend(DZCMD_ZKILL, (w == 33 || w == 34) ? 1 : 0);
                { char sub[48]; snprintf(sub, sizeof(sub), "%d DAMAGE INFLICTED", z.dmg + z.lastHp); DzKnockShow("ZOMBIE KILLED", (w == 33 || w == 34) ? "HEADSHOT" : "", sub); }   // 4.00
                s_Dz.nums.push_back({ { zp.x, zp.y, zp.z + 1.1f }, true, 0, 0, 0, now, true });   // "0" = KILL marker
            }
        }
        if(!dead)
        {
            int hp = Game::GetCharHealth(z.ped);
            if(z.lastHp > 0 && hp < z.lastHp)
            {
                z.dmg += z.lastHp - hp;
                if(s_Dz.opt.dmg) s_Dz.nums.push_back({ { zp.x, zp.y, zp.z + 1.0f }, true, 0, 0, z.lastHp - hp, now, true });
                if(rand() % 100 < 4 * DzSkillRank(DZSK_CRIT)) { Game::SetCharHealth(z.ped, 0); hp = 0; }   // Critical Hit
                z.hunting = true;   // hurt: it knows where you are
            }
            z.lastHp = hp;
            if(!z.styled && now - z.born > 300) { z.styled = true; if(z.style >= 0) Game::SetAnimGroup(z.ped, kDzZedWalk[z.style % 5]); }
            if(z.hitDue && now >= z.hitDue)
            {   // 4.00: the claw / bite lands (our own hit, the zombies don't punch with GTA fists anymore)
                z.hitDue = 0;
                float dz = zp.z - me.z;
                if(d < 2.1f && fabsf(dz) < 1.6f && !DzXDowned() && !Game::IsCharInAnyCar(ped) && myHp > 0)
                {
                    int want = (int)((s_Dz.haveMe && s_Dz.me.zombieDmg ? s_Dz.me.zombieDmg : 650) / 120.0f * (1.0f + 0.5f * diff) * (1.0f - 0.05f * DzSkillRank(DZSK_TOUGH)) + 0.5f);
                    if(rand() % 100 < 3 * DzSkillRank(DZSK_DODGE)) want = 0;
                    if(want > 0)
                    {
                        int nh = myHp - DzAdjustDamage(myHp, want, false); if(nh > 100) nh = 100; if(nh < 0) nh = 0;
                        Game::SetCharHealth(ped, nh); s_Dz.prevHp = nh;
                        DzSend(DZCMD_HIT, 1, 0, nh); s_Dz.combatUntil = now + 15000;
                    }
                }
            }
            {   // 4.00.2: a zombie that should walk but stands still for 3 s: first back to the normal walk
                // style (a walk style without its anims = can't walk), then its task is cleared and given again
                bool wantMove = (z.hunting && d > 2.2f) || (!z.hunting && z.goAt && now - z.goAt < 5000);
                float mx = zp.x - z.lastP.x, my = zp.y - z.lastP.y;
                if(!wantMove || mx * mx + my * my > 0.35f * 0.35f) { z.lastP = zp; z.stillSince = now; }
                else if(now - z.stillSince > 3000 && now > z.atkAt + 1500)
                {
                    z.stillSince = now; ++z.unstuck;
                    if(z.styled && z.style >= 0) { Game::SetAnimGroup(z.ped, "man"); z.style = -1; logger->Info("DZ: zombie %d did not walk with its walk style: normal walk", z.ped); }
                    else Game::ClearTasksNow(z.ped);
                    z.huntAt = 0; z.wanderAt = 0;
                }
            }
            if(!z.hunting && (d < detect || d < 3.0f)) { z.hunting = true; z.huntAt = 0; }
            if(z.hunting && d > detect * 1.8f + 15.0f) z.hunting = false;   // lost you
            if(z.hunting)
            {
                if(d < 1.6f && fabsf(zp.z - me.z) < 1.6f && !Game::IsCharInAnyCar(ped))
                {   // close: turn to you, then a claw swipe / smash / lunge / bite
                    if(now - z.atkAt > 1400 + (uint64_t)(rand() % 500))
                    {
                        const DzZedAnim& a = kDzZedAtk[rand() % (int)(sizeof(kDzZedAtk) / sizeof(kDzZedAtk[0]))];
                        float h = atan2f(-(me.x - zp.x), me.y - zp.y) * 57.29578f; if(h < 0) h += 360.0f;
                        Game::SetCharHeading(z.ped, h);
                        if(Game::PlayAnimOnce(z.ped, a.anim, a.file, 8.0f, false, a.ms) ||
                           Game::PlayAnimOnce(z.ped, "BIKE_elbowL", "ped", 8.0f, false, 900))   // anim file still loading
                        { z.atkAt = now; z.hitDue = now + a.hitMs; z.huntAt = now + a.ms - 1200; }
                    }
                }
                else if(now > z.atkAt + 900 && now - z.huntAt > 1200)
                {   // chase: shamble (walk style) or, on veteran / hardcore and far away, a lurching run
                    z.huntAt = now;
                    Game::NpcGoTo(z.ped, me, (diff >= 1 && d > 7.0f) || d > 18.0f);
                }
            }
            else if(now > z.wanderAt)
            {   // wander around, sometimes stand and sway / look around
                z.wanderAt = now + 6000 + rand() % 6000;
                if(rand() % 3 == 0)
                {
                    const DzZedAnim& a = kDzZedIdle[rand() % (int)(sizeof(kDzZedIdle) / sizeof(kDzZedIdle[0]))];
                    Game::PlayAnimOnce(z.ped, a.anim, a.file, 4.0f, false, a.ms);
                }
                else
                {
                    float a = (float)(rand() % 628) / 100.0f, r = 4.0f + rand() % 10;
                    Game::NpcGoTo(z.ped, { zp.x + cosf(a) * r, zp.y + sinf(a) * r, zp.z }, false); z.goAt = now;
                }
            }
        }
        if((dead && now - z.deadAt > 9000) || d > 140.0f) { Game::DeleteChar(z.ped); s_Dz.zeds.erase(s_Dz.zeds.begin() + i); continue; }
        ++i;
    }
    // more of them at night / in the fog / in towns / on hardcore; none in the first minute at a spawn point
    int want = DZ_ZED_MAX + diff * 2; int hr = Game::GetHour(); if(hr >= 21 || hr < 5) want += 2; if(s_Dz.fog) want += 2;
    int alarm = DzZedAlarmTake(); want += alarm;
    if(Game::IsCharInAnyCar(ped)) want = 3;
    if((int)s_Dz.zeds.size() >= want || (now - s_Dz.lastZedSpawn < 3500 && !alarm) || DzZedSafeHere(me)) return;
    s_Dz.lastZedSpawn = now;
    Vec3 p;
    // the new zombie spawn system: around a loot spot near you (zombiespawnradius, maxzombiesperloot)
    const DzLootC* spot = nullptr;
    if(s_Dz.haveMe && s_Dz.me.zombieSys && !s_Dz.loot.empty() && !alarm)
    {
        int n = 0;
        for(auto& kv : s_Dz.loot)
        {
            const DzLootC& l = kv.second; float ddx = l.x - me.x, ddy = l.y - me.y, dd = ddx * ddx + ddy * ddy;
            if(dd < 35.0f * 35.0f || dd > 95.0f * 95.0f) continue;
            if(rand() % (++n) == 0) spot = &l;
        }
        if(spot)
        {
            int near = 0; for(auto& z : s_Dz.zeds) { Vec3 q; if(Game::GetCharPos(z.ped, q) && (q.x - spot->x) * (q.x - spot->x) + (q.y - spot->y) * (q.y - spot->y) < 900.0f) ++near; }
            if(near >= std::max<int>(1, s_Dz.me.zombiesPerLoot)) spot = nullptr;
        }
    }
    if(spot)
    {
        float a = (float)(rand() % 628) / 100.0f, r = (float)(rand() % std::max(5, (int)s_Dz.me.zombieRadius));
        p = { spot->x + cosf(a) * r, spot->y + sinf(a) * r, me.z + 30.0f };
    }
    else
    {
        float a = (float)(rand() % 628) / 100.0f, r = (alarm ? 25.0f : 45.0f) + (float)(rand() % 30);
        p = { me.x + cosf(a) * r, me.y + sinf(a) * r, me.z + 30.0f };
    }
    float gz = Game::GroundZBelow(p.x, p.y, p.z); if(gz < -500.0f || fabsf(gz - me.z) > 25.0f) return;
    p.z = gz + 1.0f;
    int skin = kDzZedSkins[rand() % (int)(sizeof(kDzZedSkins) / sizeof(kDzZedSkins[0]))];
    for(int t = 0; t < 8 && DzModels::Pending(skin); ++t) skin = kDzZedSkins[rand() % (int)(sizeof(kDzZedSkins) / sizeof(kDzZedSkins[0]))];   // 4.00.2: let a waiting model get free
    {   // 3.61: the MTA DayZ zombies, once they are in the game
        int mz[sizeof(kDzMtaZeds) / sizeof(kDzMtaZeds[0])], n = 0;
        for(int z : kDzMtaZeds) if(DzModels::IsReplaced(z)) mz[n++] = z;
        if(n) skin = mz[rand() % n];
    }
    if(!Game::IsSafePedModel(skin) || !Game::ModelReady(skin)) return;
    int zp = Game::CreateNpcPed(skin, p, (float)(rand() % 360)); if(zp <= 0) return;
    Game::SetNpcHealth(zp, 45 + rand() % 30 + diff * 15);
    Game::SetDropsWeapons(zp, false);
    DzZed z{}; z.ped = zp; z.model = skin; z.born = now; z.lastHp = Game::GetCharHealth(zp); z.hunting = alarm > 0;
    z.style = rand() % 5;   // 4.00: each zombie its own walk style
    if(alarm) Game::NpcGoTo(zp, me, true);
    s_Dz.zeds.push_back(z);
}

// ---- ruins: debris of destroyed buildings (rocks, broken walls, chairs, wrecks) ----------------
static void DzRuinsTick(const Vec3& me)
{
    // 3.60: freeze fix - the 26 pieces of a ruin were all made in ONE frame, and when the ground there was
    // not loaded yet (seen from 260 m) none was made, so it tried all 26 again the next frame, every frame.
    // Now 3 pieces per tick (every 150 ms), each piece max 5 tries, only within 160 m.
    const uint64_t now = MP_NowMs();
    int near = -1;
    for(int i = 0; i < DZ_SITES; ++i) if(g_DzSites[i].kind == DZS_RUINS) { float dx = g_DzSites[i].x - me.x, dy = g_DzSites[i].y - me.y; if(dx * dx + dy * dy < 160.0f * 160.0f) { near = i; break; } }
    if(near != s_Dz.ruinSite)
    {
        for(int o : s_Dz.ruinObjs) if(o > 0) Game::DeleteWorldObject(o);
        s_Dz.ruinObjs.clear(); s_Dz.ruinSite = near;
        memset(s_Dz.ruinState, 0, sizeof(s_Dz.ruinState)); memset(s_Dz.ruinTries, 0, sizeof(s_Dz.ruinTries)); s_Dz.ruinAt = 0;
    }
    if(near < 0 || now < s_Dz.ruinAt) return;
    s_Dz.ruinAt = now + 150;
    static const int kDebris[] = { 3594, 3593, 12957, 1438, 1265, 1349, 1811, 1721, 3866, 3887, 905, 906, 899, 1218, 1225, 2672, 1450, 1428 };
    const DzSite& s = g_DzSites[near];
    int budget = 3;
    for(int i = 0; i < 26 && budget > 0; ++i)
    {
        if(s_Dz.ruinState[i]) continue;
        uint32_t seed = (uint32_t)(near * 2654435761u) ^ (uint32_t)((i + 1) * 40503u);
        seed = seed * 1103515245u + 12345u; int m = kDebris[(seed >> 8) % (sizeof(kDebris) / sizeof(kDebris[0]))];
        if(!Game::IsKnownModel(m)) { s_Dz.ruinState[i] = 1; continue; }
        --budget;
        seed = seed * 1103515245u + 12345u; float a = (float)((seed >> 8) % 628) / 100.0f;
        seed = seed * 1103515245u + 12345u; float r = (float)((seed >> 8) % 1000) / 1000.0f * s.r;
        float x = s.x + cosf(a) * r, y = s.y + sinf(a) * r;
        if(++s_Dz.ruinTries[i] > 5) { s_Dz.ruinState[i] = 1; continue; }
        float z = Game::GroundZBelow(x, y, me.z + 60.0f); if(z < -500.0f) continue;
        int o = Game::CreateMapObject(m, { x, y, z + 0.05f }, (float)((seed >> 4) % 30) - 15.0f, (float)((seed >> 12) % 24) - 12.0f, (float)((seed >> 16) % 360));
        if(o > 0) { s_Dz.ruinObjs.push_back(o); s_Dz.ruinState[i] = 1; }
    }
}

// ---- fuel --------------------------------------------------------------------------------------
static bool DzAtGasStation(const Vec3& p)
{
    for(int i = 0; i < DZ_SITES; ++i) if(g_DzSites[i].kind == DZS_GAS) { float dx = g_DzSites[i].x - p.x, dy = g_DzSites[i].y - p.y; if(dx * dx + dy * dy < g_DzSites[i].r * g_DzSites[i].r) return true; }
    return false;
}
static float DzCarSpeed(int car) { Vec3 v; if(car > 0 && Game::GetMoveSpeed(car, true, v)) return sqrtf(v.x * v.x + v.y * v.y + v.z * v.z); return 0.0f; }
static void DzFuelTick(int ped, uint64_t now)
{
    float dt = s_Dz.lastFuelTick ? (now - s_Dz.lastFuelTick) / 1000.0f : 0.0f; if(dt > 0.5f) dt = 0.5f; s_Dz.lastFuelTick = now;
    if(!Game::IsCharInAnyCar(ped)) { s_Dz.refueling = false; return; }
    int car = Game::GetCarCharIsIn(ped); if(car <= 0 || Game::GetDriverOfCar(car) != ped) return;
    auto it = s_Dz.fuel.find(car);
    if(it == s_Dz.fuel.end()) it = s_Dz.fuel.emplace(car, 8.0f + (float)(rand() % 40)).first;   // found cars are nearly empty
    float& f = it->second;
    float sp = DzCarSpeed(car);
    bool on = Game::IsCarEngineOn(car);
    if(on) f -= dt * (0.006f + sp * 0.0022f);   // slow: ~25 min of driving per tank at 100 km/h
    if(s_Dz.refueling) { Vec3 p; Game::GetCarPos(car, p); if(sp < 1.0f && DzAtGasStation(p)) { if(now - s_Dz.refuelAsk > 1500) { s_Dz.refuelAsk = now; DzSend(DZCMD_STATION, 0, 0, 10); } }   // 3.59: limited petrol at the station (server)
                         else s_Dz.refueling = false; if(f >= 100.0f) { f = 100.0f; s_Dz.refueling = false; DzNotice(DZ_GRN, "The tank is full."); } }
    if(f <= 0.0f) { f = 0.0f; if(on && now % 1000 < 50) { Game::SetCarEngineOn(car, false); DzNotice(DZ_RED, "Out of fuel - find a fuel canister or a gas station."); } }
}

// ---- process -----------------------------------------------------------------------------------
static void DzProcess(int ped, uint64_t now)
{
    if(!DzOnServer())
    {
        if(s_Dz.engaged || s_Dz.nProps || !s_Dz.loot.empty()) DzShutdown();
        return;
    }
    if(!s_Dz.engaged)
    {
        if(JoinFlowActive()) return;
        s_Dz.engaged = true; s_Dz.connectedAt = now;
        DzEnterCabin(ped);
    }
    DzAmbTick(now);   // 3.63: ambient music (fade / loop)
    if(ped <= 0) return;
    if(s_Dz.view == 1)
    {
        DzHudOff(now, false);
        DzCabinTick();
        if(now % 2000 < 40) { Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false); }
        // the interior did not load (other game version / mod) -> the outdoor cabin
        if(!s_Dz.cabinOutdoor && now - s_Dz.cabinAt > 2500 && now - s_Dz.cabinAt < 2600)
        {
            float g = Game::GroundZBelow(kDzCabinLook.x, kDzCabinLook.y, kDzCabinLook.z + 2.0f);
            if(g < kDzCabinLook.z - 3.0f || g > kDzCabinLook.z + 1.0f) { DzCabinClear(); s_Dz.cabinOutdoor = true; DzEnterCabin(ped); }
        }
        if(s_Dz.playAsked && now - s_Dz.playAsked > 6000) s_Dz.playAsked = 0;
        return;
    }
    if(s_Dz.view == 3)
    {   // dead: wait for the game respawn, then back to the cabin
        if(!s_bLocalDead && now - s_Dz.diedAt > 4000) { s_Dz.playAsked = 0; DzEnterCabin(ped); }
        return;
    }
    // ---- playing ----
    if(s_bLocalDead) { if(s_Dz.view == 2 && now - s_Dz.spawnedAt > 2000) { s_Dz.view = 3; s_Dz.diedAt = now; } return; }
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    // 3.60: "I spawn and can't move" - the cabin froze the ped and took the controls every second; one of
    // those could land after the spawn. For the first 3 s the controls + position are given back again.
    if(s_Dz.view == 2 && now - s_Dz.spawnedAt < 10000 && now - s_Dz.lastUnstick > 350 && !DzXDowned() && !DzXProne())   // 4.00.2: 10 s
    { s_Dz.lastUnstick = now; Game::FreezeChar(ped, false); Game::SetPlayerControl(true); Game::ShowGameHud(true); }
    // 4.00.1: stuck after a spawn (Bone County: in a rock / a house): the stick is pushed but you don't move
    if(s_Dz.view == 2 && now - s_Dz.spawnedAt < 180000 && !DzXDowned() && !DzXProne() && !Game::IsCharInAnyCar(ped))
    {
        static Vec3 sPos; static uint64_t sSince = 0; static int sTries = 0; static uint64_t sSpawn = 0;
        if(sSpawn != s_Dz.spawnedAt) { sSpawn = s_Dz.spawnedAt; sSince = 0; sTries = 0; }
        int sx = 0, sy = 0; bool push = Game::GetStick(sx, sy) && sx * sx + sy * sy > 70 * 70;
        if(!push) sSince = 0;
        else if(!sSince) { sSince = now; sPos = me; }
        else
        {
            float dx = me.x - sPos.x, dy = me.y - sPos.y;
            if(dx * dx + dy * dy > 0.5f * 0.5f) sSince = 0;
            else if(now - sSince > 2500)
            {
                sSince = 0; ++sTries;
                logger->Info("DZ: stuck at %.1f %.1f %.1f (try %d)", me.x, me.y, me.z, sTries);
                Game::FreezeChar(ped, false); Game::SetPlayerControl(true); Game::ClearTasksNow(ped);
                if(sTries >= 2)
                {   // still stuck: a few metres away, on the ground
                    float a = (float)(rand() % 628) / 100.0f, r = 4.0f + 2.0f * sTries;
                    Game::TeleportGround(ped, { me.x + cosf(a) * r, me.y + sinf(a) * r, me.z + 2.0f }, Game::GetCharHeading(ped));
                    DzNotice(DZ_GRY, "You were stuck - moved a bit.");
                }
            }
        }
    }
    DzRulesTick(now);
    if(s_Dz.radarHidden == false || now % 3000 < 40) { Game::ShowRadar(false); s_Dz.radarHidden = true; }
    DzWeaponsTick();
    DzPickTick(ped, now);
    DzLootTick(me);
    DzBaseTick(me, now);   // 4.08
    DzMakeBag(ped);
    DzZedTick(ped, me, now);
    DzRuinsTick(me);
    DzFuelTick(ped, now);
    // armour from the 5 slots
    if(s_Dz.haveMe)
    {
        int want = 0; for(int i = 0; i < 5; ++i) if(s_Dz.me.armor[i]) want += DzItemInfo(s_Dz.me.armor[i]).ammo;
        if(want > 100) want = 100;
        if(want != s_Dz.armourGiven) { int cur = Game::GetCharArmour(ped); if(want > s_Dz.armourGiven && cur < want) Game::AddArmour(ped, want - cur); s_Dz.armourGiven = want; }
    }
    // own damage: red numbers + blood drops + blood screen
    int hp = Game::GetCharHealth(ped);
    const int oldPrevHp = s_Dz.prevHp;
    if(s_Dz.prevHp > 0 && hp < s_Dz.prevHp)
    {
        int dmg = s_Dz.prevHp - hp; s_Dz.hurtAt = now; s_Dz.hurtK = std::min(1.0f, s_Dz.hurtK + dmg / 30.0f);
        s_Dz.combatUntil = now + 15000;
        if(s_Dz.opt.dmg) s_Dz.nums.push_back({ {0,0,0}, false, 0, 0, dmg * 120, now, false });
        if(s_Dz.opt.blood) for(int k = 0; k < std::min(8, 1 + dmg / 6); ++k) s_Dz.drops.push_back({ 0.25f + (rand() % 500) / 1000.0f, 0.2f + (rand() % 300) / 1000.0f, 0.0f, now, false });
    }
    if(s_Dz.prevHp > 0 && hp > s_Dz.prevHp + 1 && s_Dz.opt.blood) for(int k = 0; k < 3; ++k) s_Dz.drops.push_back({ 0.4f + (rand() % 200) / 1000.0f, 0.3f, 0.0f, now, true });
    s_Dz.prevHp = hp;
    DzXProcess(ped, hp, oldPrevHp, now);   // 3.59: DOWN / revive, conditions, temperature, keys, line of sight
    // shooting = combat (ANTI-RELOG)
    if(Game::IsCharShooting(ped)) s_Dz.combatUntil = std::max<uint64_t>(s_Dz.combatUntil, now + 15000);
    // other players I hurt: green numbers
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == myId || !players[i].active || players[i].ped <= 0) continue;
        int ph = Game::GetCharHealth(players[i].ped); auto it = s_Dz.otherHp.find(players[i].ped);
        if(it != s_Dz.otherHp.end() && ph < it->second && Game::HasCharBeenDamagedByChar(players[i].ped, ped))
        {
            DzKnockDmg(i, it->second - ph, now);   // 4.00: damage for the knockdown banner
            Vec3 pp; if(s_Dz.opt.dmg && Game::GetCharPos(players[i].ped, pp)) s_Dz.nums.push_back({ { pp.x, pp.y, pp.z + 1.1f }, true, 0, 0, (it->second - ph) * 120, now, true });
        }
        s_Dz.otherHp[players[i].ped] = ph;
    }
    // hunger / thirst at 0 and the infection cost blood
    if(s_Dz.haveMe)
    {
        if((s_Dz.me.hunger == 0 || s_Dz.me.thirst == 0) && now - s_Dz.lastNeedsHurt > 6000) { s_Dz.lastNeedsHurt = now; if(hp > 1) Game::SetCharHealth(ped, hp - 1); }
        if(s_Dz.me.infected && now - s_Dz.lastInfHurt > 9000) { s_Dz.lastInfHurt = now; if(hp > 1) Game::SetCharHealth(ped, hp - 1); }
    }
    if(now - s_Dz.lastStatus > 5000) { s_Dz.lastStatus = now; DzSend(DZCMD_HIT, 0, 0, Game::GetCharHealth(ped)); }
    if(ImGui::IsKeyPressed(ImGuiKey_F2, false)) s_Dz.panelOpen = !s_Dz.panelOpen;
    if(ImGui::IsKeyPressed(ImGuiKey_I, false) && !ImGui::GetIO().WantTextInput && !DzXDowned()) s_Dz.invOpen = !s_Dz.invOpen;
    if(DzXDowned()) s_Dz.invOpen = false;
    while(!s_Dz.nums.empty() && now - s_Dz.nums.front().t > 1600) s_Dz.nums.pop_front();
    while(!s_Dz.drops.empty() && now - s_Dz.drops.front().t > 1400) s_Dz.drops.pop_front();
    if(s_Dz.nums.size() > 40) s_Dz.nums.pop_front();
    if(s_Dz.drops.size() > 60) s_Dz.drops.pop_front();
}

// ---- commands ----------------------------------------------------------------------------------
static void DzUseFirst(uint8_t kind, uint8_t cmd)
{
    for(int i = 0; i < s_Dz.inv.n; ++i) if(DzItemInfo(s_Dz.inv.s[i].item).kind == kind) { DzSend(cmd, i); return; }
    DzNotice(DZ_GRY, "You don't have that.");
}
static void DzUseNamed(const char* name)
{
    for(int i = 0; i < s_Dz.inv.n; ++i) if(!strcasecmp(DzItemInfo(s_Dz.inv.s[i].item).name, name)) { DzSend(DZCMD_USE, i); return; }
    DzNotice(DZ_GRY, "You don't have %s.", name);
}
static void DzLocalCmd(const char* cmd)
{
    int ped = Game::LocalPed();
    if(!strcmp(cmd, "inv") || !strcmp(cmd, "bag")) { s_Dz.invOpen = !s_Dz.invOpen; return; }
    if(!strcmp(cmd, "map")) { s_Dz.coords = !s_Dz.coords; return; }   // 4.00: the map is always unlocked
    if(!strcmp(cmd, "gps")) { DzXGpsToggle(); return; }   // 4.00: no GPS item needed   // 3.59: bind 7
    if(!strcmp(cmd, "compass")) { if(!DzXCompassToggle()) s_Dz.opt.compass = !s_Dz.opt.compass; return; }                          // 3.59: bind 8
    if(!strcmp(cmd, "list") || !strcmp(cmd, "tab") || !strcmp(cmd, "online")) { DzXToggleList(); return; }
    if(!strcmp(cmd, "journal") || !strcmp(cmd, "debug")) { DzXJournal(0); return; }
    if(!strcmp(cmd, "achievements") || !strcmp(cmd, "ach")) { DzXJournal(1); return; }
    if(!strcmp(cmd, "skills")) { DzXJournal(2); return; }
    if(!strcmp(cmd, "checkbody")) { DzSend(DZCMD_CHECKBODY, 0, 0, -1); return; }
    if(!strcmp(cmd, "music") || !strcmp(cmd, "ambient")) { DzAmbToggle(); return; }   // 3.63
    if(!strcmp(cmd, "stuck") || !strcmp(cmd, "unstuck"))
    {   // 4.00.2: can't move: controls back, tasks cleared, a few metres away on the ground
        int ped = Game::LocalPed(); Vec3 me;
        if(ped > 0 && !Game::IsCharInAnyCar(ped) && !DzXDowned() && Game::GetCharPos(ped, me))
        {
            Game::FreezeChar(ped, false); Game::SetPlayerControl(true); Game::ClearTasksNow(ped);
            float a = (float)(rand() % 628) / 100.0f;
            Game::TeleportGround(ped, { me.x + cosf(a) * 5.0f, me.y + sinf(a) * 5.0f, me.z + 2.0f }, Game::GetCharHeading(ped));
            DzNotice(DZ_GRY, "Unstuck.");
        }
        return;
    }
    if(!strcmp(cmd, "dzmodels")) { DzModelsReport(false); return; }   // 4.00
    if(!strcmp(cmd, "build") || !strcmp(cmd, "base")) { DzBaseToggle(); return; }   // 4.08
    if(!strcmp(cmd, "hud")) { s_Dz.opt.logo = !s_Dz.opt.logo; return; }
    if(!strcmp(cmd, "fps") || !strcmp(cmd, "ping")) { s_Dz.opt.fps = !s_Dz.opt.fps; return; }
    if(!strcmp(cmd, "armor") || !strcmp(cmd, "weapon")) { s_Dz.invOpen = true; return; }
    if(!strcmp(cmd, "eat")) { DzUseFirst(DZK_FOOD, DZCMD_USE); return; }
    if(!strcmp(cmd, "drink")) { DzUseFirst(DZK_DRINK, DZCMD_USE); return; }
    if(!strcmp(cmd, "heal")) { DzUseNamed("Medkit"); return; }
    if(!strcmp(cmd, "bandage")) { DzUseNamed("Bandage"); return; }
    if(!strcmp(cmd, "cure")) { DzUseFirst(DZK_CURE, DZCMD_USE); return; }
    if(!strcmp(cmd, "painkillers")) { DzUseNamed("Painkillers"); return; }
    if(!strcmp(cmd, "drop")) { if(s_Dz.invSel >= 0) DzSend(DZCMD_DROP, s_Dz.invSel); else s_Dz.invOpen = true; return; }
    if(!strcmp(cmd, "pickup")) { Vec3 p; if(ped > 0 && Game::GetCharPos(ped, p)) { int i = DzNearestLoot(p, DZ_PICK_RANGE); if(i >= 0) DzStartPick(i); else DzNotice(DZ_GRY, "Nothing to pick up here."); } return; }
    if(!strcmp(cmd, "refuel") || !strcmp(cmd, "fuel"))
    {
        Vec3 p; if(ped <= 0 || !Game::GetCharPos(ped, p)) return;
        if(Game::IsCharInAnyCar(ped) && DzAtGasStation(p)) { s_Dz.refueling = true; DzNotice(DZ_YEL, "Refueling... stay still."); }
        else DzUseFirst(DZK_FUEL, DZCMD_FUEL);
        return;
    }
}
// MP-wide commands that stay allowed on Project Day Z
static bool DzPassCmd(const char* c)
{
    static const char* const ok[] = { "leave", "quit", "q", "disconnect", "mp", "menu", "tde", "report", "settings", "players", "help2", "mute", "unmute",
                                      "kick", "ban", "unban", "warn", "tban", "goto", "gethere", "spec", "freeze", "unfreeze", "slap", "a", "adm", "mod" };
    for(const char* s : ok) if(!strcasecmp(c, s)) return true;
    return false;
}
static bool DzCommand(const char* cmd, const char* arg)
{
    if(!DzOnServer() || !cmd || !cmd[0]) return false;
    char c[32]; int n = 0; const char* s = cmd; while(*s == '/') ++s;
    while(*s && n < 31) c[n++] = (char)tolower((unsigned char)*s++); c[n] = 0;
    if(DzPassCmd(c)) return false;
    static const char* const local[] = { "inv", "bag", "map", "gps", "compass", "hud", "armor", "weapon", "fps", "ping", "eat", "drink", "heal",
                                         "bandage", "cure", "painkillers", "drop", "pickup", "refuel", "fuel", "list", "tab", "online", "journal", "debug",
                                         "achievements", "ach", "skills", "checkbody", "music", "ambient", "dzmodels", "stuck", "unstuck", "build", "base" };
    if(!strcmp(c, "dzmodels")) { DzModelsReport(arg && strstr(arg, "reset")); return true; }   // 4.00: /dzmodels [reset]
    for(const char* l : local) if(!strcmp(c, l)) { DzLocalCmd(c); return true; }
    if(!strcmp(c, "panel") || !strcmp(c, "f2")) { s_Dz.panelOpen = !s_Dz.panelOpen; return true; }
    char t[100]; snprintf(t, sizeof(t), "/%s%s%s", c, arg && *arg ? " " : "", arg ? arg : "");
    DzSend(DZCMD_TEXT, 0, 0, 0, t);
    return true;
}

// ---- HUD pieces --------------------------------------------------------------------------------
static void DzText(ImDrawList* dl, float fs, ImVec2 p, ImU32 c, const char* t) { UI::OutlinedText(dl, fs, p, c, t); }
static void DzTextR(ImDrawList* dl, float fs, float xr, float y, ImU32 c, const char* t) { DzText(dl, fs, ImVec2(xr - RpTextW(fs, t), y), c, t); }

// armour slots (right side, green when worn) - helmet, chest plate, armour vest, gloves, boots
static void DzArmorIcon(ImDrawList* dl, int slot, ImVec2 c, float s, ImU32 col)
{
    switch(slot)
    {
        case DZA_HELMET: dl->PathArcTo(ImVec2(c.x, c.y + s * 0.15f), s * 0.42f, IM_PI, IM_PI * 2, 16); dl->PathFillConvex(col);
                         dl->AddRectFilled(ImVec2(c.x - s * 0.5f, c.y + s * 0.12f), ImVec2(c.x + s * 0.5f, c.y + s * 0.24f), col); break;
        case DZA_CHEST:  dl->AddQuadFilled(ImVec2(c.x - s * 0.4f, c.y - s * 0.35f), ImVec2(c.x + s * 0.4f, c.y - s * 0.35f), ImVec2(c.x + s * 0.3f, c.y + s * 0.4f), ImVec2(c.x - s * 0.3f, c.y + s * 0.4f), col);
                         dl->AddLine(ImVec2(c.x, c.y - s * 0.35f), ImVec2(c.x, c.y + s * 0.4f), IM_COL32(0, 0, 0, 160), s * 0.06f); break;
        case DZA_VEST:   dl->AddRectFilled(ImVec2(c.x - s * 0.38f, c.y - s * 0.2f), ImVec2(c.x + s * 0.38f, c.y + s * 0.42f), col, s * 0.08f);
                         dl->AddRectFilled(ImVec2(c.x - s * 0.38f, c.y - s * 0.42f), ImVec2(c.x - s * 0.18f, c.y - s * 0.2f), col);
                         dl->AddRectFilled(ImVec2(c.x + s * 0.18f, c.y - s * 0.42f), ImVec2(c.x + s * 0.38f, c.y - s * 0.2f), col); break;
        case DZA_GLOVES: dl->AddRectFilled(ImVec2(c.x - s * 0.25f, c.y - s * 0.1f), ImVec2(c.x + s * 0.25f, c.y + s * 0.42f), col, s * 0.1f);
                         for(int i = 0; i < 4; ++i) dl->AddRectFilled(ImVec2(c.x - s * 0.25f + i * s * 0.13f, c.y - s * 0.42f), ImVec2(c.x - s * 0.15f + i * s * 0.13f, c.y - s * 0.08f), col, s * 0.05f); break;
        default:         dl->AddRectFilled(ImVec2(c.x - s * 0.2f, c.y - s * 0.42f), ImVec2(c.x + s * 0.1f, c.y + s * 0.2f), col);
                         dl->AddRectFilled(ImVec2(c.x - s * 0.2f, c.y + s * 0.15f), ImVec2(c.x + s * 0.42f, c.y + s * 0.42f), col, s * 0.08f); break;
    }
}
static void DzDrawArmor(ImDrawList* dl, ImVec2 ds)
{
    const float sz = UI::S(52), gap = UI::S(8), x = ds.x - sz - UI::S(14);   // 3.60: a bit bigger
    float y = ds.y * 0.56f;
    static const char* const names[5] = { "HELMET", "CHEST", "VEST", "GLOVES", "BOOTS" };
    for(int i = 0; i < 5; ++i)
    {
        bool on = s_Dz.haveMe && s_Dz.me.armor[i];
        ImVec2 a(x, y), b(x + sz, y + sz), c(x + sz * 0.5f, y + sz * 0.5f);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), UI::S(6));
        dl->AddRect(a, b, on ? DzCol(DZ_GRN, 0.9f) : IM_COL32(98, 190, 70, 70), UI::S(6), 0, UI::S(1.5f));
        DzArmorIcon(dl, i, c, sz * 0.7f, on ? DzCol(DZ_GRN) : IM_COL32(98, 190, 70, 120));   // 4.00: MTA DayZ green, dim when empty
        if(on) dl->AddText(UI::Font(), UI::S(14), ImVec2(x - RpTextW(UI::S(14), names[i]) - UI::S(4), y + sz * 0.36f), DzCol(DZ_GRN, 0.85f), names[i]);
        y += sz + gap;
    }
}
// ear (noise) / eye (visibility) / temperature - top right
[[maybe_unused]] static void DzDrawSenses(ImDrawList* dl, ImVec2 ds, int ped, float t)
{
    float sp = Game::GetCharSpeed(ped); bool inCar = Game::IsCharInAnyCar(ped), duck = Game::IsCharDucking(ped), shoot = Game::IsCharShooting(ped);
    if(DzXProne()) duck = true;   // 4.00: prone = the quietest, hardest to see
    int noise = shoot ? 5 : inCar ? 4 : sp > 5.0f ? 3 : sp > 1.5f ? 2 : duck ? 0 : 1;
    int hr = Game::GetHour(); bool night = hr >= 21 || hr < 5;
    int vis = inCar ? 5 : duck ? 1 : sp > 5.0f ? 4 : 3; if(night) vis = std::max(0, vis - 1); if(s_Dz.fog) vis = std::max(0, vis - 2);
    const float x = ds.x - UI::S(30), y0 = ds.y * 0.30f, s = UI::S(30);
    // ear
    ImVec2 e(x, y0);
    dl->PathArcTo(ImVec2(e.x, e.y - s * 0.05f), s * 0.32f, -IM_PI * 1.05f, IM_PI * 0.15f, 16); dl->PathStroke(IM_COL32_WHITE, 0, UI::S(3));
    dl->AddLine(ImVec2(e.x + s * 0.3f, e.y + s * 0.07f), ImVec2(e.x, e.y + s * 0.42f), IM_COL32_WHITE, UI::S(3));
    for(int i = 0; i < 5; ++i)
    {
        ImU32 c = i < noise ? DzCol(i < 3 ? DZ_GRN : (i < 4 ? DZ_YEL : DZ_RED), 0.9f) : IM_COL32(255, 255, 255, 35);
        dl->PathArcTo(ImVec2(e.x + s * 0.2f, e.y), s * (0.55f + i * 0.17f), IM_PI * 0.75f, IM_PI * 1.25f, 8); dl->PathStroke(c, 0, UI::S(2.6f));
    }
    // eye
    ImVec2 c(x, y0 + s * 1.9f);
    dl->PathArcTo(ImVec2(c.x, c.y + s * 0.35f), s * 0.5f, -IM_PI * 0.80f, -IM_PI * 0.20f, 12); dl->PathStroke(IM_COL32_WHITE, 0, UI::S(2.6f));
    dl->PathArcTo(ImVec2(c.x, c.y - s * 0.35f), s * 0.5f, IM_PI * 0.20f, IM_PI * 0.80f, 12); dl->PathStroke(IM_COL32_WHITE, 0, UI::S(2.6f));
    dl->AddCircleFilled(c, s * 0.15f, IM_COL32_WHITE, 12);
    for(int i = 0; i < 5; ++i)
    {
        ImU32 cc = i < vis ? DzCol(i < 3 ? DZ_GRN : (i < 4 ? DZ_YEL : DZ_RED), 0.9f) : IM_COL32(255, 255, 255, 35);
        dl->PathArcTo(ImVec2(c.x + s * 0.2f, c.y), s * (0.55f + i * 0.17f), IM_PI * 0.75f, IM_PI * 1.25f, 8); dl->PathStroke(cc, 0, UI::S(2.6f));
    }
    // temperature (cold at night / in the fog)
    float temp = 37.0f - (night ? 1.6f : 0.0f) - (s_Dz.fog ? 1.0f : 0.0f) + (Game::IsCharInAnyCar(ped) ? 0.6f : 0.0f);
    ImVec2 th(x, y0 + s * 3.7f);
    ImU32 tc = temp < 35.5f ? IM_COL32(96, 165, 250, 255) : DzCol(DZ_GRN);
    dl->AddRectFilled(ImVec2(th.x - s * 0.1f, th.y - s * 0.5f), ImVec2(th.x + s * 0.1f, th.y + s * 0.25f), IM_COL32(255, 255, 255, 200), s * 0.1f);
    dl->AddRectFilled(ImVec2(th.x - s * 0.06f, th.y - s * 0.5f + s * 0.75f * (1.0f - (temp - 33.0f) / 5.0f)), ImVec2(th.x + s * 0.06f, th.y + s * 0.25f), tc);
    dl->AddCircleFilled(ImVec2(th.x, th.y + s * 0.35f), s * 0.18f, tc, 12);
    // 4.05.2: icon only (no temperature number)
    (void)t;
}
// scoreboard (right, under FPS)
static void DzDrawStats(ImDrawList* dl, ImVec2 ds)
{
    if(!s_Dz.haveMe) return;
    const PktDzMe& m = s_Dz.me;
    char l[8][48], v[8][24];
    snprintf(l[0], 48, "Points (level %d):", DzLevel(m.points)); snprintf(v[0], 24, "%d", m.points);
    snprintf(l[1], 48, "To next level:");      snprintf(v[1], 24, "%d", DzToNext(m.points));
    snprintf(l[2], 48, "Murders:");            snprintf(v[2], 24, "%d", m.murders);
    snprintf(l[3], 48, "Zombies killed:");     snprintf(v[3], 24, "%d", m.zkills);
    snprintf(l[4], 48, "Bandits killed:");     snprintf(v[4], 24, "%d", m.bkills);
    snprintf(l[5], 48, "Headshots:");          snprintf(v[5], 24, "%d", m.headshots);
    snprintf(l[6], 48, "Humanity:");           snprintf(v[6], 24, "%d", m.humanity);
    snprintf(l[7], 48, "Blood:");              snprintf(v[7], 24, "%d", m.blood);
    // 3.60: bigger (like the MTA screenshots) and under the GTA clock / money / weapon icon, not over it
    const float fs = UI::S(18), lh = UI::S(21), xr = ds.x - UI::S(12), xl = ds.x - UI::S(318);
    float y = ds.y * 0.235f;
    dl->AddRectFilledMultiColor(ImVec2(xl - UI::S(10), y - UI::S(4)), ImVec2(ds.x, y + lh * 8 + UI::S(4)), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 140), IM_COL32(0, 0, 0, 0));
    for(int i = 0; i < 8; ++i)
    {
        dl->AddText(UI::Font(), fs, ImVec2(xl, y), IM_COL32(255, 255, 255, 230), l[i]);
        ImU32 vc = i == 7 ? (m.blood < 4000 ? DzCol(DZ_RED) : IM_COL32(255, 120, 120, 255)) : i == 6 ? (m.humanity < 0 ? DzCol(DZ_RED) : DzCol(DZ_GRN)) : IM_COL32(255, 255, 255, 230);
        dl->AddText(UI::Font(), fs, ImVec2(xr - RpTextW(fs, v[i]), y), vc, v[i]);
        y += lh;
    }
}
// blood / hunger / thirst / infection (bottom right, above the armour)
[[maybe_unused]] static void DzDrawNeeds(ImDrawList* dl, ImVec2 ds, float t)
{
    if(!s_Dz.haveMe) return;
    const PktDzMe& m = s_Dz.me;
    const float r = UI::S(19), x = ds.x - UI::S(120), y = ds.y * 0.56f - UI::S(30);
    struct N { const char* g; float k; uint32_t col; bool warn; } n[4] = {
        { "B", m.blood / 12000.0f, 0xDC2626FFu, m.blood < 4000 },
        { "F", m.hunger / 100.0f, 0xF59E0BFFu, m.hunger < 20 },
        { "W", m.thirst / 100.0f, 0x38BDF8FFu, m.thirst < 20 },
        { "X", m.infected ? 1.0f : 0.0f, 0x22C55EFFu, m.infected != 0 } };
    for(int i = 0; i < 4; ++i)
    {
        ImVec2 c(x - i * (r * 2 + UI::S(8)), y);
        float a = n[i].warn ? 0.55f + 0.45f * sinf(t * 6.0f) : 1.0f;
        dl->AddCircleFilled(c, r, IM_COL32(0, 0, 0, 150), 24);
        if(i == 3)
        {   // green blood drop with an X (infection)
            ImU32 gc = m.infected ? DzCol(0x22C55EFFu, a) : IM_COL32(120, 120, 120, 120);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.18f), r * 0.45f, gc, 16);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.40f, c.y + r * 0.05f), ImVec2(c.x + r * 0.40f, c.y + r * 0.05f), ImVec2(c.x, c.y - r * 0.7f), gc);
            if(m.infected) { dl->AddLine(ImVec2(c.x - r * 0.2f, c.y - r * 0.02f), ImVec2(c.x + r * 0.2f, c.y + r * 0.38f), IM_COL32(0, 0, 0, 230), UI::S(2.4f)); dl->AddLine(ImVec2(c.x + r * 0.2f, c.y - r * 0.02f), ImVec2(c.x - r * 0.2f, c.y + r * 0.38f), IM_COL32(0, 0, 0, 230), UI::S(2.4f)); }
            continue;
        }
        float k = n[i].k < 0 ? 0 : (n[i].k > 1 ? 1 : n[i].k);
        dl->AddCircle(c, r, IM_COL32(255, 255, 255, 40), 24, UI::S(3));
        if(k > 0) { dl->PathArcTo(c, r, -IM_PI * 0.5f, -IM_PI * 0.5f + IM_PI * 2 * k, 32); dl->PathStroke(DzCol(n[i].col, a), 0, UI::S(3.5f)); }
        if(i == 0)
        {   // red blood drop
            ImU32 bc = DzCol(n[i].col, a);
            dl->AddCircleFilled(ImVec2(c.x, c.y + r * 0.18f), r * 0.38f, bc, 16);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.34f, c.y + r * 0.06f), ImVec2(c.x + r * 0.34f, c.y + r * 0.06f), ImVec2(c.x, c.y - r * 0.6f), bc);
        }
        else { float fs = r * 0.9f; DzText(dl, fs, ImVec2(c.x - RpTextW(fs, n[i].g) * 0.5f, c.y - fs * 0.55f), DzCol(n[i].col, a), n[i].g); }
    }
}
// weapon name + ammo, FPS | PING (top right corner)
static void DzDrawTopRight(ImDrawList* dl, ImVec2 ds, int ped)
{
    char t[64];
    if(s_Dz.opt.fps) { snprintf(t, sizeof(t), "FPS: %d | PING: %d | LOSS: %d%%", (int)ImGui::GetIO().Framerate, client.GetPing(), client.GetLossPct()); DzTextR(dl, UI::S(18), ds.x - UI::S(8), UI::S(4), IM_COL32(255, 255, 255, 220), t); }
    int w = Game::GetCharWeapon(ped);
    if(w > 0)
    {
        int it = DzWeaponItem(w); const char* nm = it ? DzItemInfo(it).name : "Weapon";
        if(s_Dz.haveInv && s_Dz.inv.hands && DzItemInfo(s_Dz.inv.hands).kind == DZK_WEAPON && DzItemInfo(s_Dz.inv.hands).val == w) nm = DzItemInfo(s_Dz.inv.hands).name;   // "AK-74 Gold"
        int total = Game::GetAmmo(ped, w), mag = DzMagSize(w);
        if(mag > 1 && total > 0) { int clip = ((total - 1) % mag) + 1; snprintf(t, sizeof(t), "%s   %d | %d", nm, clip, total - clip); }
        else if(mag == 1) snprintf(t, sizeof(t), "%s   %d", nm, total);
        else snprintf(t, sizeof(t), "%s", nm);
        DzTextR(dl, UI::S(26), ds.x - UI::S(8), UI::S(28), IM_COL32(255, 255, 255, 240), t);
    }
}
// damage numbers (floating) + animated blood drops (red = lost, green = healed)
static void DzDrawFeedback(ImDrawList* dl, ImVec2 ds, uint64_t now)
{
    float stack = 0;
    for(const DzNum& n : s_Dz.nums)
    {
        float k = (now - n.t) / 1600.0f; if(k > 1) continue;
        float a = k < 0.7f ? 1.0f : 1.0f - (k - 0.7f) / 0.3f;
        char t[24]; if(n.val == 0) snprintf(t, sizeof(t), "KILL"); else snprintf(t, sizeof(t), "%s%d", n.green ? "+" : "-", n.val);
        float x, y, dpt;
        if(n.world)
        {
            if(!Game::WorldToScreen(n.at, x, y, dpt) || dpt <= 0) continue;
            y -= k * UI::S(60); t[0] = n.val == 0 ? 'K' : '-';
            DzText(dl, UI::S(24), ImVec2(x - RpTextW(UI::S(24), t) * 0.5f, y), DzCol(n.val == 0 ? DZ_YEL : DZ_GRN, a), t);
        }
        else
        {   // my own blood: a column next to the crosshair, like the original
            x = ds.x * 0.44f; y = ds.y * 0.36f + stack - k * UI::S(30); stack += UI::S(20);
            DzText(dl, UI::S(18), ImVec2(x - RpTextW(UI::S(18), t), y), DzCol(n.green ? DZ_GRN : DZ_RED, a), t);
            ImVec2 c(x + UI::S(10), y + UI::S(10)); ImU32 bc = DzCol(n.green ? 0x22C55EFFu : 0xDC2626FFu, a);
            dl->AddCircleFilled(ImVec2(c.x, c.y + UI::S(2)), UI::S(5), bc, 12); dl->AddTriangleFilled(ImVec2(c.x - UI::S(4.5f), c.y), ImVec2(c.x + UI::S(4.5f), c.y), ImVec2(c.x, c.y - UI::S(9)), bc);
        }
    }
    for(const DzDrop& d : s_Dz.drops)
    {
        float k = (now - d.t) / 1400.0f; if(k > 1) continue;
        float a = 1.0f - k, x = d.x * ds.x, y = d.y * ds.y + k * k * ds.y * 0.35f, s = UI::S(9) * (1.0f + (1.0f - k) * 0.4f);
        ImU32 bc = DzCol(d.green ? 0x22C55EFFu : 0xB91C1CFFu, a);
        dl->AddCircleFilled(ImVec2(x, y + s * 0.35f), s * 0.6f, bc, 12);
        dl->AddTriangleFilled(ImVec2(x - s * 0.55f, y + s * 0.15f), ImVec2(x + s * 0.55f, y + s * 0.15f), ImVec2(x, y - s), bc);
    }
    // blood screen: red at the edges (hit, or low blood)
    float lowK = s_Dz.haveMe && s_Dz.me.blood < 5000 ? (5000 - s_Dz.me.blood) / 5000.0f * (0.55f + 0.45f * sinf(now / 300.0f)) : 0.0f;
    float hitK = s_Dz.hurtAt ? s_Dz.hurtK * std::max(0.0f, 1.0f - (now - s_Dz.hurtAt) / 1500.0f) : 0.0f;
    float k = std::max(lowK * 0.7f, hitK); if(!s_Dz.opt.blood) k = 0;
    if(k > 0.01f)
    {
        int a = (int)(190 * k); ImU32 r = IM_COL32(150, 0, 0, a), z = IM_COL32(150, 0, 0, 0);
        float w = ds.x * 0.16f, h = ds.y * 0.20f;
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(w, ds.y), r, z, z, r);
        dl->AddRectFilledMultiColor(ImVec2(ds.x - w, 0), ImVec2(ds.x, ds.y), z, r, r, z);
        dl->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(ds.x, h), r, r, z, z);
        dl->AddRectFilledMultiColor(ImVec2(0, ds.y - h), ImVec2(ds.x, ds.y), z, z, r, r);
    }
    if(hitK <= 0.01f) s_Dz.hurtK = 0;
}
// the square radar (4.00: always unlocked, no GPS item needed); COORDS | MAP tabs
static float s_DzRadarRange = 220.0f;
static void DzDrawRadar(ImDrawList* dl, ImVec2 ds, int ped, uint64_t nowMs, ImVec2& outP0, float& outSize)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const float size = ds.y * 0.24f; const ImVec2 p0(ds.x * 0.012f, ds.y * 0.90f - size);
    outP0 = p0; outSize = size;
    const float tabH = UI::S(24);
    if(false)   // 4.00: was "USE GPS TO UNLOCK MAP" without a GPS item
    {
        ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
        dl->AddCircleFilled(c, size * 0.5f, IM_COL32(0, 0, 0, 235), 48);
        dl->AddCircle(c, size * 0.5f, IM_COL32(255, 255, 255, 50), 48, UI::S(2));
        const char* l1 = "USE GPS"; const char* l2 = "TO UNLOCK MAP"; float fs = UI::S(16);
        DzText(dl, fs, ImVec2(c.x - RpTextW(fs, l1) * 0.5f, c.y - fs), IM_COL32(220, 220, 220, 255), l1);
        DzText(dl, fs, ImVec2(c.x - RpTextW(fs, l2) * 0.5f, c.y + UI::S(2)), IM_COL32(220, 220, 220, 255), l2);
        return;
    }
    SaRadarView v{};
    v.meX = me.x; v.meY = me.y; v.meZ = me.z; v.meHeading = Game::GetCharHeading(ped); v.mode = s_Dz.opt.radar == 2 ? 2 : 1;
    Vec3 cp, cl; if(Game::GetCameraPos(cp, cl)) { v.camFx = cl.x - cp.x; v.camFy = cl.y - cp.y; }
    float sp = 0; if(Game::IsCharInAnyCar(ped)) sp = DzCarSpeed(Game::GetCarCharIsIn(ped));
    float want = 200.0f + SarClamp(sp, 0, 45) * 4.0f; s_DzRadarRange += (want - s_DzRadarRange) * 0.04f; v.range = s_DzRadarRange;
    SaPin pins[80]; int n = 0;
    for(int i = 0; i < DZ_SITES && n < 50; ++i)
    {
        const DzSite& s = g_DzSites[i]; if(s.kind == DZS_HOUSE) continue;
        float dx = s.x - me.x, dy = s.y - me.y; if(dx * dx + dy * dy > 900.0f * 900.0f) continue;
        SaPin& q = pins[n++]; q.x = s.x; q.y = s.y; q.z = me.z; q.style = 0;
        q.rgba = s.kind == DZS_GAS ? 0xF97316FFu : s.kind == DZS_MILITARY ? 0x65A30DFFu : 0x78716CFFu;
        MP_CopyStr(q.glyph, s.kind == DZS_GAS ? "F" : s.kind == DZS_MILITARY ? "M" : "R", sizeof(q.glyph));
    }
    for(int i = 0; i < MP_MAX_PLAYERS && n < 78; ++i)
    {   // only my party shows on the map
        if(i == myId || !players[i].active || players[i].ped <= 0 || !s_Dz.haveMe || s_Dz.me.partyCol == 0) continue;
        if((players[i].color | 0xFF) != (s_Dz.me.partyCol | 0xFF)) continue;
        Vec3 pp; if(!Game::GetCharPos(players[i].ped, pp)) continue;
        SaPin& q = pins[n++]; q.x = pp.x; q.y = pp.y; q.z = pp.z; q.rgba = players[i].color | 0xFF; q.style = 1; q.glyph[0] = 0;
    }
    SaRadarDraw(dl, p0, size, v, pins, n, nullptr, 0, (float)(nowMs % 100000) / 1000.0f);
    if(s_Dz.coords)
    {
        char t[64]; snprintf(t, sizeof(t), "X %.0f  Y %.0f  Z %.0f", me.x, me.y, me.z);
        dl->AddRectFilled(ImVec2(p0.x, p0.y), ImVec2(p0.x + size, p0.y + UI::S(24)), IM_COL32(0, 0, 0, 170));
        dl->AddText(UI::Font(), UI::S(15), ImVec2(p0.x + UI::S(6), p0.y + UI::S(4)), DzCol(DZ_YEL), t);
    }
    // COORDS | MAP tabs under the frame (tap)
    ImVec2 a(p0.x, p0.y + size + UI::S(2));
    ImGui::SetNextWindowPos(a); ImGui::SetNextWindowSize(ImVec2(size, tabH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##DzRadarTabs", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    for(int i = 0; i < 2; ++i)
    {
        ImVec2 q = ImGui::GetCursorScreenPos(); float w = size * 0.5f;
        if(ImGui::InvisibleButton(i ? "##dzmap" : "##dzcoords", ImVec2(w, tabH))) s_Dz.coords = (i == 0);
        bool sel = (i == 0) == s_Dz.coords;
        wl->AddRectFilled(q, ImVec2(q.x + w, q.y + tabH), sel ? IM_COL32(60, 60, 60, 230) : IM_COL32(20, 20, 20, 210));
        const char* l = i ? "MAP" : "COORDS"; float fs = UI::S(14);
        wl->AddText(UI::Font(), fs, ImVec2(q.x + w * 0.5f - RpTextW(fs, l) * 0.5f, q.y + UI::S(4)), sel ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255), l);
        if(!i) ImGui::SameLine();
    }
    ImGui::End(); ImGui::PopStyleVar(2);
}
// drive HUD next to the radar: SPD, part boxes, bars (vehicle health / fuel / speed)
static void DzDrawDrive(ImDrawList* dl, ImVec2 p0, float size, int ped)
{
    if(!Game::IsCharInAnyCar(ped)) return;
    int car = Game::GetCarCharIsIn(ped); if(car <= 0) return;
    float sp = DzCarSpeed(car) * 3.6f;
    int hp = Game::GetCarHealth(car); float hk = (hp - 250) / 750.0f; if(hk < 0) hk = 0; if(hk > 1) hk = 1;
    auto fi = s_Dz.fuel.find(car); float fuel = fi != s_Dz.fuel.end() ? fi->second : 0.0f;
    Game::VehState vs{}; bool haveVs = Game::ReadVehState(car, vs);
    int wheels = 4, doors = 0, lights = 4;
    if(haveVs) { wheels = 0; for(int i = 0; i < 4; ++i) if(vs.wheels[i] == 0) ++wheels; for(int i = 0; i < 6; ++i) if(vs.doors[i] >= 3) ++doors; lights = 4 - __builtin_popcount(vs.lights & 0xFF) / 2; }
    // 4.00: same place as before, about 15 % bigger
    const float sc = 1.15f;
    const float bw = UI::S(40) * sc, bh = UI::S(17) * sc, bg = UI::S(4) * sc;
    const float bH = 6 * (bh + bg) - bg;
    float x = p0.x + size + UI::S(12), y = p0.y + size * 0.05f;
    char t[32]; snprintf(t, sizeof(t), "SPD: %d", (int)sp);
    DzText(dl, UI::S(20) * sc, ImVec2(x, y - UI::S(28) * sc), DzCol(DZ_GRN), t);
    // part boxes: engine, fuel tank, wheels, doors, lights, armour
    struct P { const char* n; float k; } parts[6] = { { "ENG", hk }, { "TNK", fuel / 100.0f }, { "WHL", wheels / 4.0f }, { "DRS", 1.0f - doors / 6.0f }, { "LGT", lights / 4.0f }, { "BDY", hk * 0.5f + 0.5f * (wheels / 4.0f) } };
    for(int i = 0; i < 6; ++i)
    {
        float k = parts[i].k; uint32_t c = k > 0.66f ? DZ_GRN : k > 0.33f ? DZ_YEL : DZ_RED;
        ImVec2 a(x, y + i * (bh + bg)), b(x + bw, y + i * (bh + bg) + bh);
        dl->AddRectFilled(a, b, DzCol(c, 0.30f)); dl->AddRect(a, b, DzCol(c, 0.95f), 0, 0, UI::S(1.5f));
        dl->AddText(UI::Font(), UI::S(12) * sc, ImVec2(a.x + UI::S(5), a.y + UI::S(2)), IM_COL32_WHITE, parts[i].n);
    }
    // the bars: vehicle health, fuel, speed
    float bx = x + bw + UI::S(10) * sc, barW = UI::S(9) * sc;
    float ks[3] = { hk, fuel / 100.0f, sp / 220.0f }; uint32_t cs[3] = { DZ_RED, DZ_ORG, DZ_YEL }; const char* ls[3] = { "HP", "FUEL", "SPD" };
    for(int i = 0; i < 3; ++i)
    {
        float k = ks[i] < 0 ? 0 : (ks[i] > 1 ? 1 : ks[i]); float xx = bx + i * (barW + UI::S(10) * sc);
        dl->AddRectFilled(ImVec2(xx, y), ImVec2(xx + barW, y + bH), IM_COL32(0, 0, 0, 150));
        dl->AddRectFilled(ImVec2(xx, y + bH * (1 - k)), ImVec2(xx + barW, y + bH), DzCol(cs[i], 0.95f));
        dl->AddRect(ImVec2(xx, y), ImVec2(xx + barW, y + bH), IM_COL32(255, 255, 255, 60));
        dl->AddText(UI::Font(), UI::S(10) * sc, ImVec2(xx - UI::S(2), y + bH + UI::S(3)), IM_COL32(230, 230, 230, 255), ls[i]);
    }
    snprintf(t, sizeof(t), "FUEL %d%%", (int)fuel);
    DzText(dl, UI::S(14) * sc, ImVec2(x, y + bH + UI::S(18) * sc), fuel < 15 ? DzCol(DZ_RED) : IM_COL32_WHITE, t);
}
static void DzDrawCompass(ImDrawList* dl, ImVec2 ds)
{
    if(!s_Dz.opt.compass) return;
    Vec3 cp, cl; if(!Game::GetCameraPos(cp, cl)) return;
    CompassMark mk[8]; int n = 0;
    int ped = Game::LocalPed(); Vec3 me; Game::GetCharPos(ped, me);
    for(int i = 0; i < MP_MAX_PLAYERS && n < 8; ++i)
    {
        if(i == myId || !players[i].active || players[i].ped <= 0 || !s_Dz.haveMe || !s_Dz.me.partyCol) continue;
        if((players[i].color | 0xFF) != (s_Dz.me.partyCol | 0xFF)) continue;
        Vec3 pp; if(!Game::GetCharPos(players[i].ped, pp)) continue;
        mk[n++] = { CompassBearing(pp.x - me.x, pp.y - me.y), players[i].color | 0xFF, sqrtf((pp.x - me.x) * (pp.x - me.x) + (pp.y - me.y) * (pp.y - me.y)), 0 };
    }
    PubgCompassDraw(dl, ds.x * 0.5f, UI::S(10), ds.x * 0.32f, CompassBearing(cl.x - cp.x, cl.y - cp.y), mk, n);
}
// loot labels + PICK UP button
[[maybe_unused]] static void DzDrawLoot(ImDrawList* dl, ImVec2 ds, int ped)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    for(auto& kv : s_Dz.loot)
    {
        const DzLootC& l = kv.second; if(!l.item || !l.zDone) continue;
        float dx = l.x - me.x, dy = l.y - me.y, d2 = dx * dx + dy * dy; if(d2 > 12.0f * 12.0f) continue;
        float sx, sy, dp; if(!Game::WorldToScreen({ l.x, l.y, l.z + 0.45f }, sx, sy, dp) || dp <= 0) continue;
        const DzItem& it = DzItemInfo(l.item);
        char t[48]; if(l.count > 1 && it.kind != DZK_WEAPON) snprintf(t, sizeof(t), "%s x%d", it.name, l.count); else snprintf(t, sizeof(t), "%s", it.name);
        float fs = UI::S(15); uint32_t c = it.kind == DZK_WEAPON || it.kind == DZK_AMMO ? DZ_ORG : it.kind == DZK_TRASH ? DZ_GRY : 0xFFFFFFFFu;
        DzText(dl, fs, ImVec2(sx - RpTextW(fs, t) * 0.5f, sy), DzCol(c, 0.95f), t);
    }
    if(Game::IsCharInAnyCar(ped) || s_Dz.invOpen || s_Dz.panelOpen) return;
    float d; int idx = DzNearestLoot(me, DZ_PICK_RANGE, &d); if(idx < 0) return;
    const DzItem& it = DzItemInfo(s_Dz.loot[idx].item);
    const float bw = UI::S(260), bh = UI::S(62);
    BcwBeginOverlay("##DzPick", ImVec2(ds.x * 0.5f - bw * 0.5f - UI::S(18), ds.y * 0.64f), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    char lb[64]; snprintf(lb, sizeof(lb), "PICK UP  %s##dzpk", it.name);
    if(BcwTeamButton(lb, s_Dz.pickIdx >= 0 ? 0x57534EFFu : 0xCA8A04FFu, ImVec2(bw, bh), false, s_Dz.pickIdx >= 0)) DzStartPick(idx);
    BcwEndOverlay();
}

// ---- inventory (two dark panels, orange divider, KG bars) --------------------------------------
static void DzItemTile(ImDrawList* wl, ImVec2 a, float s, const DzItem& it, int count, bool sel)
{
    ImVec2 b(a.x + s, a.y + s);
    wl->AddRectFilled(a, b, sel ? IM_COL32(70, 54, 20, 255) : IM_COL32(38, 38, 40, 255), UI::S(3));
    wl->AddRect(a, b, sel ? DzCol(DZ_ORG) : IM_COL32(60, 60, 64, 255), UI::S(3), 0, UI::S(sel ? 2.0f : 1.0f));
    // a glyph for the kind (the model name is under it)
    ImVec2 c(a.x + s * 0.5f, a.y + s * 0.40f); float r = s * 0.22f;
    switch(it.kind)
    {
        case DZK_WEAPON: wl->AddRectFilled(ImVec2(c.x - r * 1.4f, c.y - r * 0.2f), ImVec2(c.x + r * 1.4f, c.y + r * 0.2f), IM_COL32(200, 200, 200, 255)); wl->AddRectFilled(ImVec2(c.x - r * 0.6f, c.y), ImVec2(c.x - r * 0.2f, c.y + r * 0.9f), IM_COL32(160, 120, 70, 255)); break;
        case DZK_AMMO:   for(int i = 0; i < 3; ++i) wl->AddRectFilled(ImVec2(c.x - r * 0.8f + i * r * 0.6f, c.y - r * 0.7f), ImVec2(c.x - r * 0.45f + i * r * 0.6f, c.y + r * 0.7f), IM_COL32(212, 175, 55, 255), r * 0.15f); break;
        case DZK_FOOD:   wl->AddRectFilled(ImVec2(c.x - r * 0.7f, c.y - r * 0.8f), ImVec2(c.x + r * 0.7f, c.y + r * 0.8f), IM_COL32(245, 158, 11, 255), r * 0.2f); break;
        case DZK_DRINK:  wl->AddRectFilled(ImVec2(c.x - r * 0.45f, c.y - r * 0.6f), ImVec2(c.x + r * 0.45f, c.y + r * 0.9f), IM_COL32(56, 189, 248, 255), r * 0.2f); wl->AddRectFilled(ImVec2(c.x - r * 0.2f, c.y - r), ImVec2(c.x + r * 0.2f, c.y - r * 0.6f), IM_COL32(56, 189, 248, 255)); break;
        case DZK_MED: case DZK_CURE: wl->AddRectFilled(ImVec2(c.x - r * 0.9f, c.y - r * 0.7f), ImVec2(c.x + r * 0.9f, c.y + r * 0.7f), IM_COL32(240, 240, 240, 255), r * 0.2f); wl->AddRectFilled(ImVec2(c.x - r * 0.15f, c.y - r * 0.5f), ImVec2(c.x + r * 0.15f, c.y + r * 0.5f), IM_COL32(220, 38, 38, 255)); wl->AddRectFilled(ImVec2(c.x - r * 0.5f, c.y - r * 0.15f), ImVec2(c.x + r * 0.5f, c.y + r * 0.15f), IM_COL32(220, 38, 38, 255)); break;
        case DZK_BAG:    wl->AddRectFilled(ImVec2(c.x - r * 0.75f, c.y - r * 0.6f), ImVec2(c.x + r * 0.75f, c.y + r), IM_COL32(101, 115, 60, 255), r * 0.3f); wl->AddRect(ImVec2(c.x - r * 0.4f, c.y - r), ImVec2(c.x + r * 0.4f, c.y - r * 0.5f), IM_COL32(101, 115, 60, 255), r * 0.2f, 0, UI::S(2)); break;
        case DZK_ARMOR:  DzArmorIcon(wl, it.val, c, r * 2.2f, DzCol(DZ_GRN)); break;
        case DZK_GPS:    wl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.85f), ImVec2(c.x + r * 0.55f, c.y + r * 0.85f), IM_COL32(30, 30, 30, 255), r * 0.2f); wl->AddRectFilled(ImVec2(c.x - r * 0.4f, c.y - r * 0.65f), ImVec2(c.x + r * 0.4f, c.y + r * 0.2f), IM_COL32(74, 222, 128, 255)); break;
        case DZK_FUEL:   wl->AddRectFilled(ImVec2(c.x - r * 0.7f, c.y - r * 0.6f), ImVec2(c.x + r * 0.7f, c.y + r * 0.9f), IM_COL32(220, 38, 38, 255), r * 0.15f); wl->AddRectFilled(ImVec2(c.x + r * 0.2f, c.y - r), ImVec2(c.x + r * 0.5f, c.y - r * 0.6f), IM_COL32(220, 38, 38, 255)); break;
        default:         wl->AddCircleFilled(c, r * 0.8f, IM_COL32(140, 140, 140, 255), 12); break;
    }
    char nm[40]; snprintf(nm, sizeof(nm), "%s", it.name);
    float fs = UI::S(11); float w = RpTextW(fs, nm); if(w > s - UI::S(4)) { fs *= (s - UI::S(4)) / w; w = RpTextW(fs, nm); }
    wl->AddText(UI::Font(), fs, ImVec2(a.x + s * 0.5f - w * 0.5f, a.y + s * 0.74f), IM_COL32(220, 220, 220, 255), nm);
    if(count > 1) { char cn[12]; snprintf(cn, sizeof(cn), "%d", count); wl->AddText(UI::Font(), UI::S(11), ImVec2(a.x + UI::S(4), a.y + UI::S(2)), DzCol(DZ_YEL), cn); }
}
static void DzKgBar(ImDrawList* wl, ImVec2 a, float w, float cur, float max)
{
    float h = UI::S(26);
    wl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), IM_COL32(30, 30, 32, 245), UI::S(3));
    float bx = a.x + w * 0.25f, bw = w * 0.42f, by = a.y + h * 0.5f - UI::S(3);
    wl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + UI::S(6)), IM_COL32(60, 60, 64, 255));
    float k = max > 0 ? cur / max : 0; if(k > 1) k = 1;
    wl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * k, by + UI::S(6)), k > 0.9f ? DzCol(DZ_RED) : DzCol(DZ_ORG));
    char t[40]; snprintf(t, sizeof(t), "%.2f / %.2f KG", cur, max);
    wl->AddText(UI::Font(), UI::S(13), ImVec2(bx + bw + UI::S(10), a.y + h * 0.5f - UI::S(7)), IM_COL32(220, 220, 220, 255), t);
}
[[maybe_unused]] static void DzDrawInventory(ImVec2 ds, int ped)
{
    if(!s_Dz.invOpen) return;
    const float W = ds.x * 0.74f, H = ds.y * 0.70f, x0 = ds.x * 0.5f - W * 0.5f, y0 = ds.y * 0.12f;
    ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(W, H + UI::S(40)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##DzInv", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    wl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + H), IM_COL32(24, 24, 26, 238), UI::S(4));
    wl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + UI::S(26)), IM_COL32(32, 32, 35, 255), UI::S(4));
    wl->AddText(UI::Font(), UI::S(14), ImVec2(x0 + UI::S(10), y0 + UI::S(5)), IM_COL32(200, 200, 200, 255), "INVENTORY");
    const char* rt = "NEARBY / GROUND"; wl->AddText(UI::Font(), UI::S(14), ImVec2(x0 + W * 0.5f + UI::S(16), y0 + UI::S(5)), IM_COL32(200, 200, 200, 255), rt);
    // close
    ImGui::SetCursorScreenPos(ImVec2(x0 + W - UI::S(30), y0 + UI::S(1)));
    if(ImGui::InvisibleButton("##dzinvx", ImVec2(UI::S(28), UI::S(24)))) s_Dz.invOpen = false;
    wl->AddText(UI::Font(), UI::S(18), ImVec2(x0 + W - UI::S(22), y0 + UI::S(2)), IM_COL32(230, 230, 230, 255), "X");
    // orange divider
    float dx = x0 + W * 0.5f;
    wl->AddRectFilled(ImVec2(dx - UI::S(1.5f), y0 + UI::S(46)), ImVec2(dx + UI::S(1.5f), y0 + H - UI::S(70)), DzCol(DZ_ORG));
    const float tile = UI::S(76), gap = UI::S(6);
    // left: my items
    {
        float px = x0 + UI::S(10), py = y0 + UI::S(36), pw = W * 0.5f - UI::S(22);
        int cols = std::max(1, (int)((pw + gap) / (tile + gap)));
        for(int i = 0; i < s_Dz.inv.n; ++i)
        {
            ImVec2 a(px + (i % cols) * (tile + gap), py + (i / cols) * (tile + gap + UI::S(6)));
            if(a.y + tile > y0 + H - UI::S(76)) break;
            ImGui::SetCursorScreenPos(a); char id[16]; snprintf(id, sizeof(id), "##dzi%d", i);
            if(ImGui::InvisibleButton(id, ImVec2(tile, tile))) { s_Dz.invSel = s_Dz.invSel == i ? -1 : i; s_Dz.nearSel = -1; MpUiClick(); }
            DzItemTile(wl, a, tile, DzItemInfo(s_Dz.inv.s[i].item), s_Dz.inv.s[i].count, s_Dz.invSel == i);
        }
        if(!s_Dz.inv.n) wl->AddText(UI::Font(), UI::S(15), ImVec2(px, py + UI::S(8)), IM_COL32(150, 150, 150, 255), "Empty. Loot houses, gas stations and military bases.");
    }
    // right: loot near me
    std::vector<int> near;
    Vec3 me; if(Game::GetCharPos(ped, me)) for(auto& kv : s_Dz.loot) { const DzLootC& l = kv.second; float ddx = l.x - me.x, ddy = l.y - me.y; if(l.item && ddx * ddx + ddy * ddy < 4.0f * 4.0f) near.push_back(l.idx); }
    {
        float px = dx + UI::S(14), py = y0 + UI::S(36), pw = W * 0.5f - UI::S(24);
        int cols = std::max(1, (int)((pw + gap) / (tile + gap)));
        for(size_t i = 0; i < near.size(); ++i)
        {
            ImVec2 a(px + (i % cols) * (tile + gap), py + (i / cols) * (tile + gap + UI::S(6)));
            if(a.y + tile > y0 + H - UI::S(76)) break;
            ImGui::SetCursorScreenPos(a); char id[16]; snprintf(id, sizeof(id), "##dzn%d", (int)i);
            const DzLootC& l = s_Dz.loot[near[i]];
            if(ImGui::InvisibleButton(id, ImVec2(tile, tile))) { s_Dz.nearSel = l.idx; s_Dz.invSel = -1; MpUiClick(); }
            DzItemTile(wl, a, tile, DzItemInfo(l.item), l.count, s_Dz.nearSel == l.idx);
        }
        if(near.empty()) wl->AddText(UI::Font(), UI::S(15), ImVec2(px, py + UI::S(8)), IM_COL32(150, 150, 150, 255), "Nothing on the ground here.");
    }
    // actions
    float ay = y0 + H - UI::S(62);
    ImGui::SetCursorScreenPos(ImVec2(x0 + UI::S(10), ay));
    ImGui::SetWindowFontScale(0.9f);
    if(s_Dz.invSel >= 0 && s_Dz.invSel < s_Dz.inv.n)
    {
        const DzItem& it = DzItemInfo(s_Dz.inv.s[s_Dz.invSel].item);
        bool equip = it.kind == DZK_ARMOR || it.kind == DZK_BAG || it.kind == DZK_GPS || it.kind == DZK_WEAPON;
        if(BcwTeamButton(equip ? "EQUIP##dzeq" : "USE##dzuse", 0xCA8A04FFu, ImVec2(UI::S(150), UI::S(48)), false, false))
            DzSend(it.kind == DZK_FUEL ? DZCMD_FUEL : (equip && it.kind != DZK_WEAPON ? DZCMD_EQUIP : DZCMD_USE), s_Dz.invSel);
        ImGui::SameLine(0, UI::S(10));
        if(BcwTeamButton("DROP##dzdrop", 0x57534EFFu, ImVec2(UI::S(150), UI::S(48)), false, false)) { DzSend(DZCMD_DROP, s_Dz.invSel); s_Dz.invSel = -1; }
    }
    else if(s_Dz.nearSel >= 0 && s_Dz.loot.count((uint16_t)s_Dz.nearSel))
    {
        if(BcwTeamButton("PICK UP##dzpk2", 0xCA8A04FFu, ImVec2(UI::S(200), UI::S(48)), false, s_Dz.pickIdx >= 0)) { DzStartPick(s_Dz.nearSel); s_Dz.nearSel = -1; }
    }
    ImGui::SetWindowFontScale(1.0f);
    // KG bars under the panels
    float kgCur = s_Dz.haveMe ? s_Dz.me.kg100 / 100.0f : 0, kgMax = s_Dz.haveMe ? s_Dz.me.maxKg100 / 100.0f : 10;
    float nearKg = 0; for(int i : near) { const DzLootC& l = s_Dz.loot[i]; nearKg += DzItemInfo(l.item).kg * std::max<int>(1, l.count); }
    DzKgBar(wl, ImVec2(x0, y0 + H + UI::S(6)), W * 0.5f - UI::S(4), kgCur, kgMax);
    DzKgBar(wl, ImVec2(x0 + W * 0.5f + UI::S(4), y0 + H + UI::S(6)), W * 0.5f - UI::S(4), nearKg, 50.0f);
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}

// ---- F2 control panel (stats / party / commands / rules / settings) ----------------------------
// 3.60: 10 rules (same look as the Default server's 10 rules window), shown after you pick a location
static const char* const kDzRules10[10] = {
    "{FF4444}No cheating{FFFFFF}: no CLEO, mod menus, aimbots, speed / fly hacks (libCheatMenu is blocked).",
    "{FF4444}ANTI-RELOG{FFFFFF}: leaving the game during combat = {FF4444}your survivor dies{FFFFFF}.",
    "{FFFF00}No bug abuse{FFFFFF}: no glitching into walls or under the map - report it to an {33CC55}admin{FFFFFF}.",
    "{FFFF00}No spawn killing{FFFFFF} at the 15 spawn points or on fresh survivors.",
    "{FFFF00}Parties{FFFFFF}: max 4 survivors. Don't pretend to be another party.",
    "{FFFF00}Respect everyone{FFFFFF}: no insults, racism, threats or harassment.",
    "{FFFF00}No spam{FFFFFF}: no flooding, caps lock or advertising other servers.",
    "{FFFF00}Don't pretend to be staff{FFFFFF} (admin, moderator or the bot {00BFFF}Broker{FFFFFF}).",
    "{FFFF00}Listen to the {33CC55}admins{FFFFFF}: a kick waits {FF9900}4:29{FFFFFF}, 3 fly / teleport kicks = {FF4444}permanent ban{FFFFFF}.",
    "{33CC55}Survive, loot, trust no one{FFFFFF} - and have fun.",
};
// the same lines without the {RRGGBB} colour codes (F2 panel / old builds)
static void DzRulePlain(int i, char* out, size_t n)
{
    size_t o = 0; const char* r = kDzRules10[i];
    o += (size_t)snprintf(out, n, "%d. ", i + 1);
    for(; *r && o + 1 < n; ++r) { if(*r == '{') { const char* e = strchr(r, '}'); if(e && e - r == 7) { r = e; continue; } } out[o++] = *r; }
    out[o < n ? o : n - 1] = 0;
}
static void DzRulesDlgCb(int, int, const char*, int) {}
static void DzShowRules()
{
    std::string body;
    for(int i = 0; i < 10; ++i) { char l[240]; snprintf(l, sizeof(l), "{FFFF00}%d ) {FFFFFF}%s\n", i + 1, kDzRules10[i]); body += l; }
    ShowDialog(0, DIALOG_STYLE_MSGBOX, "{FFFFFF}PROJECT DAY Z LEGACY - RULES", body.c_str(), "Close", "", DzRulesDlgCb, 0x445A);
}
static void DzRulesTick(uint64_t now)
{   // once per connection, 1.5 s after the first spawn (after you tapped a location on the map)
    if(s_Dz.rulesShown) return;
    if(!s_Dz.rulesAt) { s_Dz.rulesAt = now + 1500; return; }
    if(now < s_Dz.rulesAt || SampDialogOpen() || VKeyboard::IsOpen() || s_Dz.invOpen) return;
    s_Dz.rulesShown = true; DzShowRules();
}
static const char* const kDzCmdList[] = {
    "/help", "/rules", "/credits", "/stats", "/level", "/humanity", "/score", "/top", "/time", "/fog", "/spawns", "/supplies", "/loot",
    "/inv", "/bag", "/map", "/gps", "/compass", "/hud", "/armor", "/weapon", "/fps", "/ping", "/eat", "/drink", "/heal", "/bandage",
    "/cure", "/painkillers", "/drop", "/pickup", "/refuel", "/fuel", "/party", "/plist", "/pcreate", "/paccept", "/pdecline", "/pleave",
    "/admins", "/kill", "/me", "/do", "/shout", "/whisper", "/pm", "/pinvite", "/pkick", "/p", "/report", "/music", "/dzmodels" };
static void DzRunCmd(const char* full)
{
    char c[32] = ""; const char* s = full; while(*s == '/') ++s; int n = 0; while(*s && *s != ' ' && n < 31) c[n++] = *s++; c[n] = 0; while(*s == ' ') ++s;
    DzCommand(c, s);
}
static void DzDrawPanel(ImVec2 ds)
{
    int win = s_Dz.panelOpen ? 1 : 0;
    if(!win && s_Dz.uiWin) { s_Dz.panelOpen = true; s_Dz.panelTab = s_Dz.uiWin == 2 ? 3 : s_Dz.uiWin == 3 ? 0 : s_Dz.uiWin == 4 ? 1 : s_Dz.uiWin == 5 ? 4 : 2; s_Dz.uiWin = 0; }
    if(!s_Dz.panelOpen) return;
    const float W = std::min(ds.x * 0.62f, UI::S(900)), H = ds.y * 0.72f;
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - W * 0.5f, ds.y * 0.12f)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.06f, 0.03f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(DzCol(DZ_YEL, 0.8f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(10));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, UI::S(2));
    ImGui::Begin("##DzPanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(DzCol(DZ_YEL)), "PROJECT DAY Z  -  CONTROL PANEL");
    ImGui::SameLine(W - UI::S(110));
    if(BcwTeamButton("CLOSE##dzpc", 0x57534EFFu, ImVec2(UI::S(90), UI::S(40)), false, false)) s_Dz.panelOpen = false;
    static const char* const tabs[5] = { "STATS", "PARTY", "COMMANDS", "RULES", "SETTINGS" };
    for(int i = 0; i < 5; ++i) { if(i) ImGui::SameLine(0, UI::S(6)); char l[32]; snprintf(l, sizeof(l), "%s##dzt%d", tabs[i], i); if(BcwTeamButton(l, s_Dz.panelTab == i ? 0xCA8A04FFu : 0x3F3F46FFu, ImVec2((W - UI::S(60)) / 5, UI::S(44)), s_Dz.panelTab == i, false)) s_Dz.panelTab = i; }
    ImGui::Separator();
    ImGui::BeginChild("##dzpbody", ImVec2(0, 0), false);
    const PktDzMe& m = s_Dz.me;
    switch(s_Dz.panelTab)
    {
        case 0:
            ImGui::Text("Level %d  -  %d points  (%d to the next level)", DzLevel(m.points), m.points, DzToNext(m.points));
            ImGui::Text("Alive: %u min   Blood: %d / 12000   Hunger: %d%%   Thirst: %d%%", m.aliveMin, m.blood, m.hunger, m.thirst);
            ImGui::Text("Zombies killed: %d   Bandits killed: %d   Murders: %d   Headshots: %d", m.zkills, m.bkills, m.murders, m.headshots);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(DzCol(m.humanity < 0 ? DZ_RED : DZ_GRN)), "Humanity: %d (%s)", m.humanity, m.humanity < 0 ? "BANDIT" : "SURVIVOR");
            if(m.infected) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(DzCol(DZ_RED)), "INFECTED - find Antibiotics (/cure).");
            ImGui::Text("Carry: %.2f / %.2f KG   Bag level %d   GPS: %s", m.kg100 / 100.0f, m.maxKg100 / 100.0f, m.bag, m.gps ? "yes" : "no");
            if(BcwTeamButton("TOP SURVIVORS##dztop", 0xCA8A04FFu, ImVec2(UI::S(240), UI::S(46)), false, false)) DzRunCmd("/top");
            break;
        case 1:
        {
            if(m.partyN) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(m.partyCol | 0xFF)), "Your party: %d / 4 members", m.partyN); else ImGui::Text("You are solo.");
            if(BcwTeamButton("CREATE##dzpcr", 0xCA8A04FFu, ImVec2(UI::S(150), UI::S(44)), false, m.partyN != 0)) DzRunCmd("/pcreate");
            ImGui::SameLine(); if(BcwTeamButton("ACCEPT##dzpac", 0x16A34AFFu, ImVec2(UI::S(150), UI::S(44)), false, false)) DzRunCmd("/paccept");
            ImGui::SameLine(); if(BcwTeamButton("DECLINE##dzpdc", 0x57534EFFu, ImVec2(UI::S(150), UI::S(44)), false, false)) DzRunCmd("/pdecline");
            ImGui::SameLine(); if(BcwTeamButton("LEAVE##dzplv", 0xB91C1CFFu, ImVec2(UI::S(150), UI::S(44)), false, m.partyN == 0)) DzRunCmd("/pleave");
            ImGui::Text("Invite a survivor:");
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            {
                if(i == myId || !players[i].active) continue;
                char l[64]; snprintf(l, sizeof(l), "%s (%d)##dzinv%d", players[i].name, i, i);
                if(ImGui::Button(l)) { char c[24]; snprintf(c, sizeof(c), "/pinvite %d", i); DzRunCmd(c); }
                if((i % 4) != 3) ImGui::SameLine();
            }
            ImGui::NewLine();
            break;
        }
        case 2:
        {
            ImGui::TextWrapped("Tap a command to run it (44+). Commands that need a name / text: type them in the chat.");
            int col = 0;
            for(const char* c : kDzCmdList)
            {
                if(col) ImGui::SameLine(0, UI::S(6));
                if(ImGui::Button(c, ImVec2((W - UI::S(80)) / 6, UI::S(40)))) { DzRunCmd(c); }
                col = (col + 1) % 6;
            }
            break;
        }
        case 3: for(int i = 0; i < 10; ++i) { char r[200]; DzRulePlain(i, r, sizeof(r)); ImGui::TextWrapped("%s", r); } break;
        default:
            ImGui::Checkbox("FPS | PING", &s_Dz.opt.fps);
            ImGui::Checkbox("Damage numbers", &s_Dz.opt.dmg);
            ImGui::Checkbox("Compass", &s_Dz.opt.compass);
            ImGui::Checkbox("Blood screen + drops", &s_Dz.opt.blood);
            ImGui::Checkbox("Title logo", &s_Dz.opt.logo);
            { bool r3 = s_Dz.opt.radar == 2; if(ImGui::Checkbox("3D radar (GPS)", &r3)) s_Dz.opt.radar = r3 ? 2 : 1; }
            {   // 3.63: ambient music
                bool on = !s_DzAmb.muted;
                if(ImGui::Checkbox("Ambient music (No Man's Land - Lemuria)", &on)) DzAmbToggle();
            }
            {   // 4.00: MTA model loader status
                ImGui::Checkbox("MTA DayZ models", &settings.dzModels);
                char l[256]; DzModelsStatusLine(l, sizeof(l)); ImGui::TextWrapped("%s", l);
                if(ImGui::Button("Retry skipped models")) DzModelsReport(true);
            }
            ImGui::TextWrapped("Project Day Z Legacy - gamemode Proper DAYZ. Based on Project DayZ SA-MP, made for MP by ThirtyFox.");
            ImGui::TextWrapped("Ambient music: No Man's Land | Metro / S.T.A.L.K.E.R Atmospheric Playlist by Lemuria - youtu.be/aVTcVx1570w (streamed from the MP media host over Wi-Fi / mobile data; a copy at files/MP/dz_ambient.mp3 is used instead if you have one).");
            ImGui::TextWrapped("Global network (4.05): libks, signalwire-c, FreeSWITCH (SignalWire), Eclipse Mosquitto, EMQX, MQTTX, Mainflux, awesome-mqtt, vps-hosting-providers, Sidekick, Aeon-MLTB, whmcs-vps-reseller.");
            break;
    }
    ImGui::EndChild();
    ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
}

// ---- the connection screen ---------------------------------------------------------------------
static bool DzMenuText(const char* id, const char* label, float cx, float y, float fs, bool sel)
{
    ImFont* pd = UI::PricedownFont(); ImFont* f = pd ? pd : UI::Font();
    float w = f->CalcTextSizeA(fs, FLT_MAX, 0, label).x;
    ImVec2 a(cx - w * 0.5f - UI::S(18), y);
    ImGui::SetCursorScreenPos(a);
    bool hit = ImGui::InvisibleButton(id, ImVec2(w + UI::S(36), fs * 1.2f));
    bool hot = ImGui::IsItemActive() || sel;
    ImDrawList* wl = ImGui::GetWindowDrawList();
    if(hot) wl->AddRectFilled(a, ImVec2(a.x + w + UI::S(36), a.y + fs * 1.2f), DzCol(DZ_YEL, 0.18f), UI::S(4));
    RpGtaText(wl, f, fs, ImVec2(cx - w * 0.5f, y), hot ? DzCol(DZ_YEL) : IM_COL32_WHITE, label, UI::S(2.2f));
    if(hit) MpUiClick();
    return hit;
}
// 3.62: MTA-style "Download progress" window while the MTA DayZ models come from GitHub
static bool DzDlActive(DzModels::Download& d)
{
    if(!settings.dzModels) return false;
    DzModels::GetDownload(d);
    return d.started && (d.busy || !d.listReady);
}
static void DzDlFmtBytes(char* o, size_t n, unsigned long long b) { snprintf(o, n, "%.1f MB", b / 1048576.0); }
static float DzDlSpeed(const DzModels::Download& d)   // bytes per second, smoothed
{
    static uint64_t t0 = 0; static unsigned long long b0 = 0; static float sp = 0;
    uint64_t now = MP_NowMs();
    if(!t0 || d.bytesDone < b0) { t0 = now; b0 = d.bytesDone; return sp; }
    if(now - t0 >= 700) { float s = (float)(d.bytesDone - b0) * 1000.0f / (float)(now - t0); sp = sp > 0 ? sp * 0.6f + s * 0.4f : s; t0 = now; b0 = d.bytesDone; }
    return sp;
}
static void DzDrawDownload(ImVec2 ds, float x0, float t)
{
    DzModels::Download d; if(!DzDlActive(d) || s_DzDlSkip) return;
    const float w = std::min(ds.x - x0 - UI::S(40), UI::S(700)), h = UI::S(300);
    const float x = x0 + (ds.x - x0 - w) * 0.5f, y = ds.y * 0.5f - h * 0.5f, th = UI::S(40);
    // 4.05.2: the panel and its button are ONE window now. Before, the panel was painted on the foreground
    // layer (over every window), so it covered its own button - and the two did not go away together.
    // Now the button sits on the panel, and when the download is done the panel + button are gone at once
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##DzDlWin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* fg = ImGui::GetWindowDrawList();
    fg->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(12, 12, 12, 235), UI::S(8));
    fg->AddRectFilledMultiColor(ImVec2(x + 1, y + 1), ImVec2(x + w - 1, y + th), IM_COL32(70, 70, 70, 255), IM_COL32(70, 70, 70, 255), IM_COL32(28, 28, 28, 255), IM_COL32(28, 28, 28, 255));
    fg->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(120, 120, 120, 200), UI::S(8), 0, UI::S(1.5f));
    fg->AddText(UI::Font(), UI::S(20), ImVec2(x + UI::S(16), y + UI::S(9)), IM_COL32_WHITE, "Download progress");
    float ly = y + th + UI::S(16);
    const float pct = d.bytesTotal ? (float)((double)d.bytesDone / (double)d.bytesTotal) : 0.0f;
    char l[200];
    if(!d.listReady) snprintf(l, sizeof(l), "Connecting to GitHub - getting the resource list...");
    else snprintf(l, sizeof(l), "Downloading resource 'dayz' (Project Day Z Legacy) - %d files", d.filesTotal);
    fg->AddText(UI::Font(), UI::S(18), ImVec2(x + UI::S(18), ly), DzCol(DZ_YEL), l); ly += UI::S(30);
    snprintf(l, sizeof(l), "From: %s", d.host);
    fg->AddText(UI::Font(), UI::S(15), ImVec2(x + UI::S(18), ly), IM_COL32(190, 190, 190, 255), l); ly += UI::S(24);
    if(d.listReady)
    {
        snprintf(l, sizeof(l), "File %d / %d:  %s", std::min(d.filesDone + 1, d.filesTotal), d.filesTotal, d.file);
        fg->AddText(UI::Font(), UI::S(15), ImVec2(x + UI::S(18), ly), IM_COL32(230, 230, 230, 255), l, nullptr, w - UI::S(36));
    }
    ly += UI::S(30);
    // the bar
    const float bx = x + UI::S(18), bw = w - UI::S(36), bh = UI::S(28);
    fg->AddRectFilled(ImVec2(bx, ly), ImVec2(bx + bw, ly + bh), IM_COL32(40, 40, 40, 255), UI::S(4));
    if(d.listReady) fg->AddRectFilled(ImVec2(bx, ly), ImVec2(bx + bw * pct, ly + bh), DzCol(DZ_YEL), UI::S(4));
    else { float p = fmodf(t * 0.6f, 1.0f); fg->AddRectFilled(ImVec2(bx + bw * std::max(0.0f, p - 0.2f), ly), ImVec2(bx + bw * std::min(1.0f, p + 0.05f), ly + bh), DzCol(DZ_YEL, 0.8f), UI::S(4)); }
    fg->AddRect(ImVec2(bx, ly), ImVec2(bx + bw, ly + bh), IM_COL32(150, 150, 150, 220), UI::S(4));
    snprintf(l, sizeof(l), "%d%%", (int)(pct * 100.0f + 0.5f));
    { ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(17), FLT_MAX, 0, l); UI::OutlinedText(fg, UI::S(17), ImVec2(bx + bw * 0.5f - ts.x * 0.5f, ly + bh * 0.5f - ts.y * 0.5f), IM_COL32_WHITE, l); }
    ly += bh + UI::S(12);
    char a[24], b[24]; DzDlFmtBytes(a, sizeof(a), d.bytesDone); DzDlFmtBytes(b, sizeof(b), d.bytesTotal);
    const float sp = DzDlSpeed(d);
    if(d.listReady)
    {
        int eta = sp > 1024.0f ? (int)((double)(d.bytesTotal - d.bytesDone) / sp) : -1;
        if(eta >= 0) snprintf(l, sizeof(l), "%s / %s   -   %.0f KB/s   -   %d:%02d left", a, b, sp / 1024.0f, eta / 60, eta % 60);
        else snprintf(l, sizeof(l), "%s / %s", a, b);
        fg->AddText(UI::Font(), UI::S(15), ImVec2(bx, ly), IM_COL32(220, 220, 220, 255), l);
        if(d.filesFailed) { snprintf(l, sizeof(l), "%d failed", d.filesFailed); fg->AddText(UI::Font(), UI::S(15), ImVec2(bx + bw - UI::S(90), ly), DzCol(DZ_RED), l); }
    }
    // the button: close this window and play now - the rest keeps downloading in the background
    // (the thin bar on top shows it; Play on the left works right away)
    const float bw2 = std::min(UI::S(330), w * 0.5f), bh2 = UI::S(46);
    ImGui::SetCursorScreenPos(ImVec2(x + w - bw2 - UI::S(14), y + h - bh2 - UI::S(12)));
    if(BcwTeamButton("DOWNLOAD IN BACKGROUND##dzdlskip", 0x57534EFFu, ImVec2(bw2, bh2), false, false)) s_DzDlSkip = true;
    fg->AddText(UI::Font(), UI::S(13), ImVec2(x + UI::S(18), y + h - UI::S(40)), IM_COL32(160, 160, 160, 255), "Saved on the phone - next time it is instant.", nullptr, w - bw2 - UI::S(50));
    ImGui::End(); ImGui::PopStyleVar(2);
}
// in game: a thin bar on top while the rest is still coming
static void DzDrawDownloadMini(ImDrawList* fg, ImVec2 ds)
{
    DzModels::Download d; if(!DzDlActive(d) || !d.listReady || !d.bytesTotal) return;
    const float pct = (float)((double)d.bytesDone / (double)d.bytesTotal);
    const float w = UI::S(300), h = UI::S(8), x = ds.x * 0.5f - w * 0.5f, y = UI::S(6);
    fg->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 140), h * 0.5f);
    fg->AddRectFilled(ImVec2(x, y), ImVec2(x + w * pct, y + h), DzCol(DZ_YEL, 0.9f), h * 0.5f);
    char l[96]; snprintf(l, sizeof(l), "MTA DayZ models %d%%", (int)(pct * 100.0f + 0.5f));
    UI::OutlinedText(fg, UI::S(13), ImVec2(x + w + UI::S(8), y - UI::S(4)), IM_COL32(230, 230, 230, 230), l);
}
static void DzDrawCabin(float t)
{
    if(MP::AuthScreenShown() || SampDialogOpen()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float colW = ds.x * 0.30f;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(colW * 1.25f, ds.y), IM_COL32(0, 0, 0, 215), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 215));
    bg->AddRectFilled(ImVec2(0, 0), ImVec2(colW, ds.y), IM_COL32(0, 0, 0, 90));
    // vignette + warm fire light
    float fl = 0.5f + 0.5f * sinf(t * 7.1f) * sinf(t * 2.3f);
    bg->AddRectFilledMultiColor(ImVec2(colW, ds.y * 0.55f), ImVec2(ds.x, ds.y), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(40, 16, 0, (int)(70 + 30 * fl)), IM_COL32(40, 16, 0, (int)(70 + 30 * fl)));
    DzDrawLogo(bg, colW * 0.5f, ds.y * 0.06f, 0.95f, t);
    bg->AddText(UI::Font(), UI::S(14), ImVec2(UI::S(18), ds.y * 0.24f), IM_COL32(220, 220, 220, 230), "Survive. Loot. Trust no one.");
    bg->AddText(UI::Font(), UI::S(14), ImVec2(UI::S(18), ds.y * 0.24f + UI::S(18)), IM_COL32(170, 170, 170, 230), "Gamemode: Proper DAYZ  |  Broker 24/7");
    ImGui::SetNextWindowPos(ImVec2(0, ds.y * 0.30f)); ImGui::SetNextWindowSize(ImVec2(colW, ds.y * 0.70f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DzCabin", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
    const float fs = UI::S(36), step = UI::S(58), cx = colW * 0.5f; float y = ds.y * 0.33f;
    if(s_Dz.page == 0)
    {
        DzModels::Download dl; const bool dlWait = DzDlActive(dl) && !s_DzDlSkip;   // 3.62: like MTA - play after the download (or PLAY NOW)
        if(DzMenuText("##dzplay", dlWait ? "Downloading..." : (s_Dz.playAsked ? "waiting..." : "Play"), cx, y, fs, false) && !s_Dz.playAsked && !dlWait) { s_Dz.playAsked = MP_NowMs(); if(s_Dz.haveMe && s_Dz.me.spawnSel) { DzXSpawnSelect(); s_Dz.playAsked = 0; } else DzSend(DZCMD_PLAY); }
        // 3.60: no Rules button here any more - the 10 rules window comes up after you pick a location
        if(DzMenuText("##dzcred", "Credits", cx, y + step, fs, false)) s_Dz.page = 2;
        if(DzMenuText("##dzset", "Settings", cx, y + step * 2, fs, false)) s_Dz.page = 3;
        if(DzMenuText("##dzleave", "Leave", cx, y + step * 3.4f, fs * 0.7f, false)) LeaveServer("Left Project Day Z Legacy.");
    }
    else
    {
        if(DzMenuText("##dzback", "Back", cx, ds.y * 0.88f, fs * 0.8f, false)) s_Dz.page = 0;
    }
    ImGui::End(); ImGui::PopStyleVar();
    // page content on the right side, over the cabin
    if(s_Dz.page)
    {
        float px = colW + UI::S(30), py = ds.y * 0.14f, pw = ds.x - px - UI::S(30), ph = ds.y * 0.72f;
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        fg->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + ph), IM_COL32(8, 6, 2, 215), UI::S(10));
        // 4.02: no yellow outline around the Credits / Settings / Rules panel (plain dark glass)
        float ly = py + UI::S(20); const float lf = UI::S(19);
        if(s_Dz.page == 1)
        {
            DzText(fg, UI::S(26), ImVec2(px + UI::S(20), ly), DzCol(DZ_YEL), "RULES"); ly += UI::S(44);
            for(int i = 0; i < 10; ++i) { char r[200]; DzRulePlain(i, r, sizeof(r)); fg->AddText(UI::Font(), lf, ImVec2(px + UI::S(20), ly), IM_COL32_WHITE, r, nullptr, pw - UI::S(40)); ly += lf * 1.6f; }
        }
        else if(s_Dz.page == 2)
        {
            DzText(fg, UI::S(26), ImVec2(px + UI::S(20), ly), DzCol(DZ_YEL), "CREDITS"); ly += UI::S(44);
            static const char* const cr[] = { "Project Day Z Legacy (Proper DAYZ) for MP", "Original Project DayZ SA-MP gamemode - github.com/ThirtyFoxMCx00E1/Project-DayZ-SAMP",
                                              "MP, the Broker 24/7 servers, the HUD and the cabin - ThirtyFox", "Psdk / AML mod loader - the AndroidModLoader team",
                                              "Ambient music: No Man's Land | Metro / S.T.A.L.K.E.R Atmospheric Playlist - Lemuria (youtube.com/@lemurian.archives, youtu.be/aVTcVx1570w)",
                                              "Global network (4.05): signalwire/libks, signalwire/signalwire-c, signalwire/freeswitch, eclipse-mosquitto/mosquitto, emqx/emqx, emqx/MQTTX, mainflux/mainflux, awesome-mqtt/awesome-mqtt, qfft560/vps-hosting-providers, MightyMoud/sidekick, AeonOrg/Aeon-MLTB, Rad-Web-Hosting/whmcs-vps-reseller (github.com)",
                                              "Thanks to every survivor testing the server." };
            for(const char* r : cr) { fg->AddText(UI::Font(), lf, ImVec2(px + UI::S(20), ly), IM_COL32_WHITE, r, nullptr, pw - UI::S(40)); ly += lf * 1.9f; }
        }
        else
        {
            DzText(fg, UI::S(26), ImVec2(px + UI::S(20), ly), DzCol(DZ_YEL), "SETTINGS");
            ImGui::SetNextWindowPos(ImVec2(px + UI::S(20), ly + UI::S(50))); ImGui::SetNextWindowSize(ImVec2(pw - UI::S(40), ph - UI::S(90)));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);   // 4.02: no outlines
            ImGui::Begin("##DzCabinSet", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
            ImGui::SetWindowFontScale(1.2f);
            ImGui::Checkbox("FPS | PING", &s_Dz.opt.fps); ImGui::Checkbox("Damage numbers", &s_Dz.opt.dmg); ImGui::Checkbox("Compass", &s_Dz.opt.compass);
            ImGui::Checkbox("Blood screen + drops", &s_Dz.opt.blood); ImGui::Checkbox("Title logo in game", &s_Dz.opt.logo);
            { bool r3 = s_Dz.opt.radar == 2; if(ImGui::Checkbox("3D radar (GPS)", &r3)) s_Dz.opt.radar = r3 ? 2 : 1; }
            ImGui::End(); ImGui::PopStyleVar(2);
        }
    }
    BcwCenterText(bg, UI::S(15), ds.y - UI::S(28), IM_COL32(200, 200, 200, 200), "Press PLAY: you wake up at 1 of 15 random places (or where you logged out alive).");
    if(!s_Dz.page) DzDrawDownload(ds, colW, t);   // 3.62
}

#include "mp_dayz_mta.inc"   // 3.59: the MTA DayZ systems + textures
#include "mp_dzbase.inc"     // 4.08: base building
static bool DzBaseOpen() { return s_DzB.open; }

// ---- main render -------------------------------------------------------------------------------
static void RenderDz()
{
    if(!s_Dz.engaged) return;
    if(FloatMenu::IsModal()) return;
    const uint64_t nowMs = MP_NowMs(); const float t = (float)(nowMs % 100000000ULL) / 1000.0f;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(s_Dz.view == 1) { DzDrawCabin(t); if(s_DzX.spawnSel) DzDrawSpawnSelect(ds, t); return; }
    if(MP::AuthScreenShown() || Game::IsPaused()) return;
    if(TdeIsOpen()) return;   // 4.00: a clean screen for the Textdraw Editor
    ImDrawList* fg = ImGui::GetForegroundDrawList(); ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if(s_Dz.view == 3)
    {
        bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(60, 0, 0, 110));
        BcwCenterText(fg, UI::S(46), ds.y * 0.40f, DzCol(DZ_RED), "YOU ARE DEAD");
        BcwCenterText(fg, UI::S(20), ds.y * 0.40f + UI::S(60), IM_COL32_WHITE, "Your gear stays on your body. A new survivor wakes up soon.");
        return;
    }
    int ped = Game::LocalPed(); if(ped <= 0) return;
    DzDrawDownloadMini(fg, ds);   // 3.62
    DzDrawFeedback(bg, ds, nowMs);
    DzDrawCompass(fg, ds);
    ImVec2 rp0(0, 0); float rs = 0;
    if(!s_DzX.gpsOff) { if(s_Dz.opt.radar == 2) DzDrawRadar(bg, ds, ped, nowMs, rp0, rs); else DzDrawGps(bg, ds, ped, rp0, rs); }   // 3.59: the MTA DayZ GPS (key 7)
    if(rs > 0) DzDrawDrive(bg, rp0, rs, ped);
    DzDrawTopRight(fg, ds, ped);
    if(!s_Dz.invOpen) DzDrawStats(bg, ds);   // 3.60: not behind the inventory
    DzDrawPain(bg, ds, t);
    if(!s_Dz.invOpen) DzDrawStatusMta(bg, ds, ped, t);   // blood / food / drink / temperature / sound / visibility (MTA DayZ icons)
    if(!s_Dz.invOpen) DzDrawArmor(bg, ds);
    DzDrawLootX(bg, ds, ped, t);       // beams (never through walls) + glow outline when aimed at
    DzDrawPartyMarkers(fg, ds, ped, t);
    DzDrawBigCompass(fg, ds);
    DzDrawOptics(fg, ds, ped);
    // spawn name floating, then fading
    if(s_Dz.spawnNameAt && nowMs - s_Dz.spawnNameAt < 6000)
    {
        float a = nowMs - s_Dz.spawnNameAt < 5000 ? 1.0f : 1.0f - (nowMs - s_Dz.spawnNameAt - 5000) / 1000.0f;
        BcwCenterText(fg, UI::S(34), ds.y * 0.22f, DzCol(DZ_YEL, a), s_Dz.spawnName);
    }
    // the floating names of the 15 spawn points (3D text) near me
    {
        Vec3 me; Game::GetCharPos(ped, me);
        for(int i = 0; i < 15; ++i)
        {
            const DzSpawnPt& s = g_DzSpawns[i]; float dx = s.x - me.x, dy = s.y - me.y; if(dx * dx + dy * dy > 60.0f * 60.0f) continue;
            float sx, sy, dp; if(!Game::WorldToScreen({ s.x, s.y, s.z + 2.2f }, sx, sy, dp) || dp <= 0) continue;
            char l[80]; snprintf(l, sizeof(l), "SPAWN: %s", s.name);
            DzText(bg, UI::S(16), ImVec2(sx - RpTextW(UI::S(16), l) * 0.5f, sy), DzCol(DZ_GRN, 0.9f), l);
        }
    }
    // bottom: the logo, the warning lines
    float by = ds.y - UI::S(30);
    if(s_Dz.opt.logo) { DzDrawLogo(bg, ds.x * 0.5f, ds.y - UI::S(122), 0.85f, t, 0.9f); by = ds.y - UI::S(148); }   // 4.00: a bit higher
    DzDrawAntiRelog(fg, ds, nowMs, t);   // 3.59: top-left like the screenshot
    (void)by;   // 3.60: no "Press F2" line - this is a phone, the gear button opens the panel
    if(s_Dz.noticeAt && nowMs - s_Dz.noticeAt < 4000)
    {
        float a = nowMs - s_Dz.noticeAt < 3400 ? 1.0f : 1.0f - (nowMs - s_Dz.noticeAt - 3400) / 600.0f;
        BcwCenterText(fg, UI::S(22), ds.y * 0.30f, DzCol(s_Dz.noticeCol, a), s_Dz.notice);
    }
    // 3.60: icon buttons right of the top bar (same height as its pills, black 50 % background):
    // control panel (gear), bag (backpack), journal (book) and FUEL at gas stations. The TAB button
    // is gone - the player list is in the 3-dots menu of the top bar.
    {
        float bx0, by0, bx1, by1;
        float h = UI::S(52), x = ds.x - UI::S(330), y = UI::S(16);
        if(FloatMenu::GetBarRect(&bx0, &by0, &bx1, &by1)) { h = by1 - by0; x = bx1 + UI::S(10); y = by0; }
        Vec3 p; Game::GetCharPos(ped, p);
        const bool fuel = Game::IsCharInAnyCar(ped) && DzAtGasStation(p);
        const int n = fuel ? 5 : 4; const float gap = UI::S(10), w = h * 1.08f;   // 4.08: + build (hammer)
        ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(n * w + (n - 1) * gap + 2, h + 2));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##DzBtns", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for(int i = 0; i < n; ++i)
        {
            ImGui::SetCursorScreenPos(ImVec2(x + i * (w + gap), y));
            char id[16]; snprintf(id, sizeof(id), "##dzib%d", i);
            const bool hit = ImGui::InvisibleButton(id, ImVec2(w, h));
            const bool on = i == 0 ? s_Dz.panelOpen : i == 1 ? s_Dz.invOpen : i == 2 ? s_DzX.journal : i == 3 ? DzBaseOpen() : s_Dz.refueling;
            const ImVec2 a0 = ImGui::GetItemRectMin(), a1 = ImGui::GetItemRectMax();
            dl->AddRectFilled(a0, a1, ImGui::IsItemActive() ? IM_COL32(40, 40, 40, 170) : IM_COL32(0, 0, 0, 128), h * 0.26f);
            if(on) dl->AddRect(a0, a1, IM_COL32(255, 255, 255, 150), h * 0.26f, 0, UI::S(2));
            const ImU32 c = IM_COL32(255, 255, 255, 235);
            const float cx = (a0.x + a1.x) * 0.5f, cy = (a0.y + a1.y) * 0.5f, r = h * 0.30f, lw = UI::S(2.6f);
            if(i == 0)
            {   // gear: 8 teeth, ring, hole
                for(int k = 0; k < 8; ++k) { float an = k * 0.785398f; ImVec2 d(cosf(an), sinf(an)); dl->AddLine(ImVec2(cx + d.x * r * 0.62f, cy + d.y * r * 0.62f), ImVec2(cx + d.x * r * 1.02f, cy + d.y * r * 1.02f), c, r * 0.36f); }
                dl->AddCircleFilled(ImVec2(cx, cy), r * 0.72f, c, 24);
                dl->AddCircleFilled(ImVec2(cx, cy), r * 0.30f, IM_COL32(20, 20, 20, 255), 16);
            }
            else if(i == 1)
            {   // backpack: body, top handle, front pocket, straps
                dl->AddRect(ImVec2(cx - r * 0.30f, cy - r * 1.08f), ImVec2(cx + r * 0.30f, cy - r * 0.70f), c, r * 0.18f, 0, lw);
                dl->AddRectFilled(ImVec2(cx - r * 0.78f, cy - r * 0.78f), ImVec2(cx + r * 0.78f, cy + r * 1.02f), c, r * 0.36f);
                dl->AddRectFilled(ImVec2(cx - r * 0.50f, cy + r * 0.05f), ImVec2(cx + r * 0.50f, cy + r * 0.72f), IM_COL32(25, 25, 25, 255), r * 0.14f);
                dl->AddLine(ImVec2(cx - r * 0.50f, cy + r * 0.28f), ImVec2(cx + r * 0.50f, cy + r * 0.28f), c, lw * 0.8f);
                dl->AddLine(ImVec2(cx - r * 0.78f, cy - r * 0.25f), ImVec2(cx + r * 0.78f, cy - r * 0.25f), IM_COL32(25, 25, 25, 255), lw * 0.8f);
            }
            else if(i == 2)
            {   // journal: closed book with spine + bookmark
                dl->AddRectFilled(ImVec2(cx - r * 0.75f, cy - r), ImVec2(cx + r * 0.75f, cy + r), c, r * 0.12f);
                dl->AddRectFilled(ImVec2(cx - r * 0.75f, cy - r), ImVec2(cx - r * 0.50f, cy + r), IM_COL32(120, 120, 120, 255), r * 0.12f);
                for(int k = 0; k < 3; ++k) { float ly = cy - r * 0.45f + k * r * 0.38f; dl->AddLine(ImVec2(cx - r * 0.30f, ly), ImVec2(cx + r * 0.52f, ly), IM_COL32(25, 25, 25, 255), lw * 0.8f); }
                dl->AddTriangleFilled(ImVec2(cx + r * 0.25f, cy + r), ImVec2(cx + r * 0.50f, cy + r), ImVec2(cx + r * 0.375f, cy + r * 0.70f), IM_COL32(220, 38, 38, 255));
            }
            else if(i == 3)
            {   // 4.08: build - a hammer
                dl->AddLine(ImVec2(cx - r * 0.55f, cy + r * 0.95f), ImVec2(cx + r * 0.25f, cy - r * 0.35f), c, lw * 1.6f);
                ImVec2 hc(cx + r * 0.32f, cy - r * 0.48f); const float an = -0.785f + 1.5708f, ca = cosf(an), sa = sinf(an);
                auto P = [&](float u, float v) { return ImVec2(hc.x + u * ca - v * sa, hc.y + u * sa + v * ca); };
                dl->AddQuadFilled(P(-r * 0.75f, -r * 0.22f), P(r * 0.55f, -r * 0.22f), P(r * 0.55f, r * 0.22f), P(-r * 0.75f, r * 0.22f), c);
            }
            else
            {   // fuel pump
                dl->AddRectFilled(ImVec2(cx - r * 0.70f, cy - r), ImVec2(cx + r * 0.25f, cy + r), c, r * 0.14f);
                dl->AddRectFilled(ImVec2(cx - r * 0.52f, cy - r * 0.78f), ImVec2(cx + r * 0.07f, cy - r * 0.25f), IM_COL32(25, 25, 25, 255), r * 0.08f);
                dl->AddBezierCubic(ImVec2(cx + r * 0.25f, cy - r * 0.45f), ImVec2(cx + r * 0.85f, cy - r * 0.45f), ImVec2(cx + r * 0.85f, cy + r * 0.60f), ImVec2(cx + r * 0.62f, cy + r * 0.60f), c, lw);
                dl->AddLine(ImVec2(cx + r * 0.70f, cy - r * 0.60f), ImVec2(cx + r * 0.70f, cy + r * 0.20f), c, lw);
            }
            if(hit)
            {
                if(i == 0) s_Dz.panelOpen = !s_Dz.panelOpen;
                else if(i == 1) { if(!s_DzX.downed) s_Dz.invOpen = !s_Dz.invOpen; }
                else if(i == 2) s_DzX.journal = !s_DzX.journal;
                else if(i == 3) DzBaseToggle();
                else s_Dz.refueling = !s_Dz.refueling;
            }
        }
        ImGui::End(); ImGui::PopStyleVar();
    }
    DzDrawKnock(fg, ds, nowMs);   // 4.00
    if(!s_DzX.downed && !Game::IsCharInAnyCar(ped))
    {   // 4.00: PRONE button - only a white icon (no background), right side
        // 4.02: the right side is full - stats board (DzDrawStats: ds.x - 328 .. ds.x, ds.y * 0.235 + 8 lines),
        // ear / eye icons (ds.x - 132, from ds.y * 0.40), status column (ds.x - 256, from ds.y * 0.42, with the
        // temperature arrows to its left) and the armour column. The button goes left of the status column
        // (and its arrows), just under the stats board, so it touches none of them.
        const float w = UI::S(70), h = UI::S(58);
        const float statsBottom = ds.y * 0.235f + UI::S(21) * 8 + UI::S(4);
        const float statusLeft = ds.x - UI::S(256) - UI::S(62) * 0.62f;
        const float x = statusLeft - UI::S(14) - w;
        const float y = std::max(statsBottom + UI::S(12), ds.y * 0.42f);
        ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##DzProne", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        const bool hit = ImGui::InvisibleButton("##dzprone", ImVec2(w, h));
        const int al = ImGui::IsItemActive() ? 255 : s_DzX.prone ? 250 : 200;
        const ImU32 c = IM_COL32(255, 255, 255, al), sh = IM_COL32(0, 0, 0, al / 3);
        const float cx = x + w * 0.5f, cy = y + h * 0.58f, u = w / 14.0f, lw = UI::S(3.2f);
        for(int pass = 0; pass < 2; ++pass)
        {   // a person lying on the belly: head, body, legs spread, arms forward (like the screenshot)
            const float o = pass == 0 ? UI::S(1.5f) : 0.0f; const ImU32 cc = pass == 0 ? sh : c;
            dl->AddCircleFilled(ImVec2(cx + 5.2f * u + o, cy - 1.6f * u + o), 1.35f * u, cc, 16);
            dl->AddLine(ImVec2(cx - 3.0f * u + o, cy - 0.2f * u + o), ImVec2(cx + 3.8f * u + o, cy - 1.0f * u + o), cc, lw * 1.5f);
            dl->AddLine(ImVec2(cx + 3.4f * u + o, cy - 1.0f * u + o), ImVec2(cx + 6.6f * u + o, cy + 0.6f * u + o), cc, lw);
            dl->AddLine(ImVec2(cx - 3.0f * u + o, cy - 0.2f * u + o), ImVec2(cx - 6.6f * u + o, cy - 1.4f * u + o), cc, lw);
            dl->AddLine(ImVec2(cx - 3.0f * u + o, cy - 0.2f * u + o), ImVec2(cx - 6.4f * u + o, cy + 1.2f * u + o), cc, lw);
            dl->AddLine(ImVec2(cx - 7.0f * u + o, cy + 2.4f * u + o), ImVec2(cx + 7.0f * u + o, cy + 2.4f * u + o), cc, UI::S(1.6f));
        }
        if(s_DzX.prone) dl->AddText(nullptr, UI::S(12), ImVec2(cx - RpTextW(UI::S(12), "PRONE") * 0.5f, y + h - UI::S(12)), c, "PRONE");
        if(hit && nowMs > s_DzX.getupUntil) DzProneSet(ped, !s_DzX.prone, nowMs);
        ImGui::End(); ImGui::PopStyleVar();
    }
    DzBaseDraw(bg, ds, ped);         // 4.08: base building
    DzBaseDoorButton(ds, ped);
    DzDrawActions(ds, ped);
    DzDrawInventoryMta(ds, ped);
    DzDrawDowned(fg, bg, ds, t);
    DzDrawAch(fg, ds, nowMs);
    DzDrawPlayerList(ds);
    DzDrawJournal(ds);
    DzDrawPanel(ds);
}

// launcher / menu entry: join the 24/7 Project Day Z (the Broker bot on GitHub Actions)
void MP::JoinDz()
{
    if(DzOnServer()) { LeaveServer("Left Project Day Z Legacy."); return; }
    s_bDzJoin = true;
    MP::JoinServer("", MP_DZ_PORT, MP_DZ_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bDzJoin = false;
}
