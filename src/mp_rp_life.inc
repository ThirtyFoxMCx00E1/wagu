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
#include "mp_rp_atm.inc"   // 3.30: the ATM screen looks like a real ATM (steel, blue screen, side keys, keypad)
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
static void RpAtmOpen(int place) { s_Atm.open = true; s_Atm.tab = ATS_MAIN; s_Atm.at = MP_NowMs(); s_Atm.place = place; s_Atm.entry[0] = 0; s_Atm.msgAt = 0; RpClick(RPS_CLICK); }

static void RpDrawAtm(uint64_t nowMs)
{
    int ped = Game::LocalPed();
    int here = RpAtmHere(ped);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(!s_Atm.open)
    {   // walk up to an ATM: an outlined "ATM" button right of centre
        if(here < 0 || RpInvOpen() || s_Rp.phone || s_Rp.talkStage) return;
        char l[64]; snprintf(l, sizeof(l), "%s##atmb", g_RpPlaces[here].kind == RPK_BANK ? "Mega Bank" : "Use ATM");
        const char* L[1] = { l };
        if(RpOutlineColumn("##RpAtmBtn", L, 1, UI::S(280), UI::S(62), 0, ds.y * 0.42f) == 0) RpAtmOpen(here);
        return;
    }
    if(here < 0) { s_Atm.open = false; return; }   // walked away
    RpDrawAtmMachine(nowMs, g_RpPlaces[s_Atm.place >= 0 ? s_Atm.place : here].name);
}

// ---- 3.30: dropped items on the ground ----------------------------------------------------------
// The host keeps the list (PKT_RP_DROPS); every phone puts a real object on the ground for each
// row (a real cellphone for the smartphone, a burger, a pizza slice, a can...), spinning slowly
// like a pickup, with its name over it. Walk close: "Pick up ..." (outlined, right of centre).
static struct { int n = 0; RpDropRow row[RP_DROPS_MAX]; } s_Drops;
struct RpDropObj { uint16_t uid; int obj; int model; uint64_t since; Vec3 p; };
static RpDropObj s_DropObj[RP_DROPS_MAX];
static uint64_t s_nDropPickAt = 0;
static void RpDropsOnPacket(const uint8_t* d, int len)
{
    const int head = (int)offsetof(PktRpDrops, row);
    if(len < head) return;
    int n = d[1]; if(n > RP_DROPS_MAX) n = RP_DROPS_MAX;
    if(len < head + n * (int)sizeof(RpDropRow)) n = (len - head) / (int)sizeof(RpDropRow);
    memcpy(s_Drops.row, d + head, n * sizeof(RpDropRow));
    s_Drops.n = n;
}
static const RpDropRow* RpDropFind(uint16_t uid)
{
    for(int i = 0; i < s_Drops.n; ++i) if(s_Drops.row[i].uid == uid) return &s_Drops.row[i];
    return nullptr;
}
static void RpDropsClear()
{
    for(auto& o : s_DropObj) { if(o.obj > 0 && Game::WorldObjectExists(o.obj)) Game::DeleteWorldObject(o.obj); o = RpDropObj{}; }
}
static void RpDropsStream(uint64_t now, int ped)
{
    if(!Game::IsReady() || Game::IsPaused() || ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    if(!(me.x == me.x)) return;
    // gone or far: remove the object
    for(auto& o : s_DropObj)
    {
        if(!o.uid) continue;
        const RpDropRow* r = RpDropFind(o.uid);
        float dx = o.p.x - me.x, dy = o.p.y - me.y;
        if(!r || dx * dx + dy * dy > 160.0f * 160.0f || (o.obj > 0 && !Game::WorldObjectExists(o.obj)))
        { if(o.obj > 0 && Game::WorldObjectExists(o.obj)) Game::DeleteWorldObject(o.obj); o = RpDropObj{}; }
    }
    // new and near: make one per tick
    for(int i = 0; i < s_Drops.n; ++i)
    {
        const RpDropRow& r = s_Drops.row[i];
        float dx = r.pos[0] - me.x, dy = r.pos[1] - me.y;
        if(dx * dx + dy * dy > 120.0f * 120.0f || !r.id || r.id >= RPX_COUNT) continue;
        RpDropObj* o = nullptr; RpDropObj* freeO = nullptr;
        for(auto& q : s_DropObj) { if(q.uid == r.uid) { o = &q; break; } if(!q.uid && !freeO) freeO = &q; }
        if(o && o->obj > 0) continue;
        if(!o) { if(!freeO) break; o = freeO; o->uid = r.uid; o->obj = 0; o->model = g_RpDropModel[r.id]; o->since = now;
                 float gz = Game::GroundZ(r.pos[0], r.pos[1], r.pos[2] + 1.0f);
                 o->p = { r.pos[0], r.pos[1], (gz > -100.0f && fabsf(gz - r.pos[2]) < 3.0f ? gz : r.pos[2] - 0.95f) + 0.15f }; }
        int h = Game::CreateWorldObject(o->model, o->p);
        if(h > 0) o->obj = h;
        else if(now - o->since > 4000 && o->model != 1210) { o->model = 1210; o->since = now; }   // the model never loaded: a briefcase
        break;
    }
    // spin + bob like a pickup
    float spin = (float)(now % 4000) * 0.09f;
    for(auto& o : s_DropObj)
        if(o.obj > 0) { Vec3 p = o.p; p.z += 0.06f * sinf((float)(now % 2000) * 0.00314f); Game::MoveWorldObject(o.obj, p, spin); }
}
// names over the drops, and the pick-up button when you are close
static void RpDrawDrops(uint64_t nowMs)
{
    if(!s_Drops.n) return;
    int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    int best = -1; float bd = RP_DROP_RANGE * RP_DROP_RANGE;
    char t[64];
    for(int i = 0; i < s_Drops.n; ++i)
    {
        const RpDropRow& r = s_Drops.row[i]; if(!r.id || r.id >= RPX_COUNT) continue;
        const RpDropObj* o = nullptr; for(const auto& q : s_DropObj) if(q.uid == r.uid) { o = &q; break; }
        Vec3 p = o && o->obj > 0 ? o->p : Vec3{ r.pos[0], r.pos[1], r.pos[2] - 0.9f };
        float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z, d2 = dx * dx + dy * dy;
        if(d2 > 22.0f * 22.0f) continue;
        snprintf(t, sizeof(t), r.count > 1 ? "%s x%u" : "%s", g_RpInv[r.id].name, r.count);
        BcwLabel3D(bg, { p.x, p.y, p.z + 0.55f }, t, RP_SKY, 26.0f, 8.0f);
        if(d2 < bd && fabsf(dz) < 2.5f) { bd = d2; best = i; }
    }
    if(best < 0 || Game::IsCharInAnyCar(ped) || RpInvOpen() || s_Rp.phone || s_Rp.talkStage || s_Atm.open) return;
    const RpDropRow& r = s_Drops.row[best];
    snprintf(t, sizeof(t), "Pick up %s##dpk", g_RpInv[r.id].name);
    const char* L[1] = { t };
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(RpOutlineColumn("##RpDropBtn", L, 1, UI::S(330), UI::S(62), 0, ds.y * 0.30f) == 0 && nowMs - s_nDropPickAt > 600)
    { s_nDropPickAt = nowMs; RpSend(RPC_INV_PICKUP, 0, 0, r.uid); }
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
    if(!engaged) { if(s_Welcomed) { RpAtmClear(); RpDropsClear(); RpInfoUpdate(false, now); s_Atm.open = false; } s_Welcomed = false; return; }
    if(!s_Welcomed) { s_Welcomed = true; RpWelcome(); }
    RpInfoUpdate(Game::GetCharArea(ped > 0 ? ped : Game::LocalPed()) == 0 || s_Rp.view == 1, now);
    if(s_Rp.view == 2) { RpAtmStream(now, ped); RpDropsStream(now, ped); } else { RpAtmClear(); RpDropsClear(); }
}
