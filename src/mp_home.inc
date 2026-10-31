// 3.39: Home Kit gamemode (GM_HOME) - the phone side. The server keeps the houses (server_home.inc),
// this file keeps a copy, streams the real GTA SA objects / buildings near the player, draws the
// door markers + labels (SA-MP style: "[H:id]" cyan, owner, name, red glowing column + arrow), the
// /setup slot squares on the ground (green = fine, red = over the 16x16 limit / overlaps) and the
// /edithouse + /edithouseinteriors editors.
//
// On by itself on the default server, on custom servers when the host turned on "Home Kit"
// (Add server > Gamemodes), and on Mega Vil Futuristic (the RP server reports GM_HOME).

struct HkHomeC
{
    PktHomeHead h;
    bool haveHead = false;
    std::vector<HkObj> obj;         // the current objects
    std::vector<HkObj> rx;          // objects coming in (rev rxRev)
    uint16_t rxRev = 0; int rxGot = 0;
    std::vector<uint8_t> rxHave;     // 3.39.1: which object slots came (a lost UDP page = ask again)
    uint64_t headAt = 0; int retries = 0;
    float doorGz = 0; uint64_t doorGzAt = 0; float doorGzFor[3] = {0, 0, 0};   // 3.39.1: the ground under the door
    std::vector<int> inst;          // spawned object handle per obj (0 = none, -1 = failed)
    std::vector<HkObj> instObj;     // what each handle shows
    int prefabInst = 0; uint16_t prefabModel = 0; float prefabAt[4] = {0, 0, 0, 0};
    uint64_t reqAt = 0;
    bool seen = true;
};
static std::map<uint16_t, HkHomeC> s_Hk;
static std::vector<uint16_t> s_HkWant;     // ids to ask for (HC_GET)
static uint64_t s_nHkWantAt = 0, s_nHkStreamAt = 0;
static uint16_t s_nHkInside = 0;           // the house whose interior I am in
static Vec3     s_HkOutPos = { 0, 0, 0 };
static uint16_t s_nHkDoorShown = 0;        // door dialog shown for this house (until I walk away)
static bool     s_bHkExitArmed = false;    // inside: walked away from the exit marker once
static uint16_t s_nHkPendingSetup = 0;     // /create house -> server ok -> hint
static uint64_t s_nHkIndexAt = 0;

struct HkSetup
{
    bool open = false; uint16_t id = 0;
    int w = 8, h = 8; float ox = 0, oy = 0, oz = 0, hdg = 0;
    int selX = -1, selY = -1; int prefabIdx = 0;   // 0 = none, else index into the prefab list + 1
    std::vector<float> gz; int gzW = 0, gzH = 0; float gzKey[4] = {0, 0, 0, 0};
};
static HkSetup s_HkSet;
struct HkEdit
{
    bool open = false; bool inside = false; uint16_t id = 0;
    int cat = HKC_LIVING, model = 0; int sel = -1; int step = 1; uint64_t sendAt = 0; bool dirty = false; int dirtyIdx = -1;
};
static HkEdit s_HkEd;

static bool HkActive()
{
    if(client.GetState() != NET_CONNECTED || BcwOnServer() || Mw3OnServer() || !Game::IsReady()) return false;
    return RpOnServer() || MP::ModeOn(GM_HOME);
}
static HkHomeC* HkGet(uint16_t id) { auto it = s_Hk.find(id); return (it != s_Hk.end() && it->second.haveHead) ? &it->second : nullptr; }
static void HkSendCmd(PktHomeCmd& c) { c.type = PKT_HOME_CMD; client.Send(&c, sizeof(c)); }
static PktHomeCmd HkCmd(uint8_t cmd, uint16_t id = 0, int32_t val = 0) { PktHomeCmd c; memset(&c, 0, sizeof(c)); c.cmd = cmd; c.id = id; c.val = val; return c; }
static const char* HkBareName(const char* n)   // 3.39.1: "[MOD] Juan" / "[ADMIN] SENJI" -> the plain name
{
    if(!n) return "";
    for(int k = 0; k < 3; ++k) { if(!strncmp(n, "[MOD] ", 6)) n += 6; else if(!strncmp(n, "[ADMIN] ", 8)) n += 8; else break; }
    return n;
}
static bool HkMine(const PktHomeHead& h) { return h.ownerName[0] && !strcasecmp(HkBareName(h.ownerName), HkBareName(settings.name)); }
static bool HkHasKey(const PktHomeHead& h) { for(int k = 0; k < HK_KEYS; ++k) if(h.keys[k][0] && !strcasecmp(HkBareName(h.keys[k]), HkBareName(settings.name))) return true; return false; }
static bool HkStaff() { return MP::IsLocalAdmin() || MP::IsLocalMod() || (s_Server.IsRunning() && !MP::IsHostingDefault()); }
static bool HkCanEdit(const PktHomeHead& h) { return HkMine(h) || HkStaff(); }
static Vec3 HkDoor(const PktHomeHead& h)
{
    Vec3 p; HkLocalToWorld(h.x, h.y, h.heading, h.ex / 100.0f, h.ey / 100.0f, p.x, p.y); p.z = h.z + h.ez / 100.0f; return p;
}
// 3.39.1: the door marker / text stand on the ground. The door point is where the player stood
// (body centre, ~1 m up, or on the stairs): find the ground under it once in a while (5 s) and cache it.
static Vec3 HkDoorGround(HkHomeC& c, uint64_t now, bool compute = false)   // compute: game thread only (HkProcess)
{
    Vec3 d = HkDoor(c.h);
    const bool moved = fabsf(d.x - c.doorGzFor[0]) > 0.05f || fabsf(d.y - c.doorGzFor[1]) > 0.05f || fabsf(d.z - c.doorGzFor[2]) > 0.05f;
    if(!compute) { if(!c.doorGzAt || moved) d.z -= 0.95f; else d.z = c.doorGz; return d; }
    if(!c.doorGzAt || moved || now - c.doorGzAt > 5000)
    {
        float g = Game::GroundZBelow(d.x, d.y, d.z + 0.4f);
        if(g > -999.0f && g > d.z - 3.5f) c.doorGz = g + 0.03f;
        else c.doorGz = d.z - 0.95f;   // no collision loaded yet: feet height
        c.doorGzAt = now; c.doorGzFor[0] = d.x; c.doorGzFor[1] = d.y; c.doorGzFor[2] = d.z;
    }
    d.z = c.doorGz;
    return d;
}
static Vec3 HkCentre(const PktHomeHead& h)
{
    Vec3 p; HkLocalToWorld(h.x, h.y, h.heading, h.w * HK_TILE * 0.5f, h.h * HK_TILE * 0.5f, p.x, p.y); p.z = h.z; return p;
}
static bool HkObjWorld(const PktHomeHead& h, const HkObj& o, Vec3& p, float& rz)
{
    if(o.flags & HKO_INTERIOR)
    {
        if(h.interior <= 0 || h.interior >= HK_INTERIOR_COUNT) return false;
        const HkInterior& in = g_HkInteriors[h.interior];
        p = { in.x + o.x / 100.0f, in.y + o.y / 100.0f, in.z + o.z / 100.0f }; rz = o.rz / 10.0f;
        return true;
    }
    HkLocalToWorld(h.x, h.y, h.heading, o.x / 100.0f, o.y / 100.0f, p.x, p.y); p.z = h.z + o.z / 100.0f; rz = h.heading + o.rz / 10.0f;
    return true;
}
static int HkNearest(float maxDist, bool doorOnly = false)
{
    int best = -1; float bd = maxDist;
    for(auto& kv : s_Hk)
    {
        if(!kv.second.haveHead) continue;
        const PktHomeHead& h = kv.second.h;
        Vec3 d = HkDoor(h); float dist = sqrtf((d.x - myDrawPos.x) * (d.x - myDrawPos.x) + (d.y - myDrawPos.y) * (d.y - myDrawPos.y));
        if(!doorOnly)
        {
            float lx, ly; HkWorldToLocal(h.x, h.y, h.heading, myDrawPos.x, myDrawPos.y, lx, ly);
            if(lx >= -2 && ly >= -2 && lx <= h.w * HK_TILE + 2 && ly <= h.h * HK_TILE + 2) dist = fminf(dist, 0.5f);
        }
        if(dist < bd) { bd = dist; best = kv.first; }
    }
    if(s_nHkInside && HkGet(s_nHkInside)) return s_nHkInside;
    return best;
}
static int HkByName(const char* n)
{
    if(!n || !*n) return -1;
    for(auto& kv : s_Hk) if(kv.second.haveHead && !strcasecmp(kv.second.h.name, n)) return kv.first;
    if(n[0] == '#' || isdigit((unsigned char)n[0])) { int id = atoi(n[0] == '#' ? n + 1 : n); if(HkGet((uint16_t)id)) return id; }
    return -1;
}
// the house named in arg, else the one I stand at
static int HkPick(const char* arg, bool quiet = false)
{
    int id = (arg && *arg) ? HkByName(arg) : HkNearest(25.0f);
    if(id < 0 && !quiet) SysMsg(COL_RED, (arg && *arg) ? "No house called \"%s\" (/houses lists them)." : "Stand at a house (or type its name).", arg);
    return id;
}

// ---- spawned objects --------------------------------------------------------------------------
static void HkDespawn(HkHomeC& c)
{
    for(int& i : c.inst) { if(i > 0) Game::DeleteWorldObject(i); i = 0; }
    if(c.prefabInst > 0) Game::DeleteWorldObject(c.prefabInst);
    c.prefabInst = 0; c.prefabModel = 0;
}
static void HkDespawnAll() { for(auto& kv : s_Hk) HkDespawn(kv.second); }
static void HkReset()
{
    HkDespawnAll(); s_Hk.clear(); s_HkWant.clear();
    s_nHkInside = 0; s_nHkDoorShown = 0; s_HkSet.open = false; s_HkEd.open = false; s_nHkPendingSetup = 0;
}
// make the spawned objects match c.obj (in range: outside ones; inside ones only in that interior)
static void HkReconcile(HkHomeC& c, bool near, bool inside, int& budget)
{
    if(c.inst.size() > c.obj.size()) { for(size_t i = c.obj.size(); i < c.inst.size(); ++i) if(c.inst[i] > 0) Game::DeleteWorldObject(c.inst[i]); }
    c.inst.resize(c.obj.size(), 0); c.instObj.resize(c.obj.size());
    for(size_t i = 0; i < c.obj.size(); ++i)
    {
        const HkObj& o = c.obj[i];
        const bool want = (o.flags & HKO_INTERIOR) ? inside : near;
        int& h = c.inst[i];
        if(!want) { if(h > 0) Game::DeleteWorldObject(h); h = 0; continue; }
        const bool same = !memcmp(&c.instObj[i], &o, sizeof(HkObj));
        if(h > 0 && !Game::WorldObjectExists(h)) h = 0;
        if(h > 0 && !same)
        {
            if(c.instObj[i].model == o.model) { Vec3 p; float rz; if(HkObjWorld(c.h, o, p, rz)) Game::PlaceMapObject(h, p, o.rx / 10.0f, o.ry / 10.0f, rz); c.instObj[i] = o; continue; }
            Game::DeleteWorldObject(h); h = 0;
        }
        if(h == -1 && same) continue;   // no room / unknown model: not again until it changes
        if(h <= 0)
        {
            if(budget <= 0) continue;
            c.instObj[i] = o;
            if(!Game::IsKnownModel(o.model)) { h = -1; continue; }
            Vec3 p; float rz; if(!HkObjWorld(c.h, o, p, rz)) { h = -1; continue; }
            --budget;
            h = Game::CreateMapObject(o.model, p, o.rx / 10.0f, o.ry / 10.0f, rz);
            if(h == 0) memset(&c.instObj[i], 0, sizeof(HkObj));   // still loading: again next time
        }
    }
    // the building (prefab) in the middle of the slot
    const float at[4] = { c.h.x, c.h.y, c.h.z, c.h.heading };
    const bool wantPrefab = near && c.h.prefab && Game::IsKnownModel(c.h.prefab);
    if(c.prefabInst > 0 && (!wantPrefab || c.prefabModel != c.h.prefab || memcmp(at, c.prefabAt, sizeof(at)) || !Game::WorldObjectExists(c.prefabInst)))
    { if(Game::WorldObjectExists(c.prefabInst)) Game::DeleteWorldObject(c.prefabInst); c.prefabInst = 0; }
    if(wantPrefab && c.prefabInst == 0 && budget > 0)
    {
        --budget;
        Vec3 p = HkCentre(c.h);
        int h = Game::CreateMapObject(c.h.prefab, p, 0, 0, c.h.heading);
        if(h > 0) { c.prefabInst = h; c.prefabModel = c.h.prefab; memcpy(c.prefabAt, at, sizeof(at)); }
    }
}

// ---- packets ----------------------------------------------------------------------------------
static void HkOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    switch(d[0])
    {
    case PKT_HOME_INDEX:
    {
        if(len < (int)offsetof(PktHomeIndex, e)) return;
        PktHomeIndex p; memset(&p, 0, sizeof(p)); memcpy(&p, d, std::min(len, (int)sizeof(p)));
        if(p.page == 0) for(auto& kv : s_Hk) kv.second.seen = false;
        const int n = std::min((int)p.count, HK_INDEX_PAGE);
        for(int k = 0; k < n; ++k)
        {
            uint16_t id = p.e[k * 2], rev = p.e[k * 2 + 1];
            HkHomeC& c = s_Hk[id]; c.seen = true;
            if(!c.haveHead || c.h.rev != rev) if(std::find(s_HkWant.begin(), s_HkWant.end(), id) == s_HkWant.end()) s_HkWant.push_back(id);
        }
        if((int)(p.page + 1) * HK_INDEX_PAGE >= p.total)   // the last page: houses that are gone
            for(auto it = s_Hk.begin(); it != s_Hk.end();) { if(!it->second.seen) { HkDespawn(it->second); it = s_Hk.erase(it); } else ++it; }
        return;
    }
    case PKT_HOME_HEAD:
    {
        if(len < (int)sizeof(PktHomeHead)) return;
        PktHomeHead h; memcpy(&h, d, sizeof(h));
        h.name[HK_NAME_LEN - 1] = 0; h.ownerName[MP_NAME_LEN - 1] = 0; h.owner[HK_OWNER_LEN - 1] = 0;
        for(int k = 0; k < HK_KEYS; ++k) h.keys[k][MP_NAME_LEN - 1] = 0;
        HkHomeC& c = s_Hk[h.id];
        const bool newRev = !c.haveHead || c.h.rev != h.rev;
        c.h = h; c.haveHead = true; c.seen = true;
        if(newRev || (c.rxGot < (int)c.rx.size()))
        {
            if(newRev || c.rxRev != h.rev) { c.rxRev = h.rev; c.rx.assign(std::min((int)h.nObj, HK_MAX_OBJ), HkObj{}); c.rxHave.assign(c.rx.size(), 0); c.rxGot = 0; c.retries = 0; }
            if(c.rx.empty()) c.obj.clear();
        }
        c.headAt = MP_NowMs();
        s_HkWant.erase(std::remove(s_HkWant.begin(), s_HkWant.end(), h.id), s_HkWant.end());
        return;
    }
    case PKT_HOME_OBJS:
    {
        if(len < (int)offsetof(PktHomeObjs, o)) return;
        PktHomeObjs p; memset(&p, 0, sizeof(p)); memcpy(&p, d, std::min(len, (int)sizeof(p)));
        auto it = s_Hk.find(p.id); if(it == s_Hk.end()) return;
        HkHomeC& c = it->second;
        if(p.rev != c.rxRev) return;
        if(c.rxHave.size() != c.rx.size()) c.rxHave.assign(c.rx.size(), 0);
        for(int k = 0; k < p.count && k < HK_OBJ_PAGE; ++k) { int i = p.start + k; if(i < (int)c.rx.size()) { c.rx[i] = p.o[k]; if(!c.rxHave[i]) { c.rxHave[i] = 1; ++c.rxGot; } } }
        if(c.rxGot >= (int)c.rx.size()) { c.obj = c.rx; }
        return;
    }
    case PKT_HOME_DEL:
    {
        if(len < (int)sizeof(PktHomeDel)) return;
        PktHomeDel p; memcpy(&p, d, sizeof(p));
        auto it = s_Hk.find(p.id);
        if(it != s_Hk.end()) { HkDespawn(it->second); s_Hk.erase(it); }
        if(s_HkSet.id == p.id) s_HkSet.open = false;
        if(s_HkEd.id == p.id) s_HkEd.open = false;
        return;
    }
    case PKT_HOME_MSG:
    {
        if(len < (int)sizeof(PktHomeMsg)) return;
        PktHomeMsg m; memcpy(&m, d, sizeof(m)); m.text[sizeof(m.text) - 1] = 0;
        SysMsg(m.ok ? COL_GREEN : COL_RED, "[HOUSE] %s", m.text);
        if(m.cash) Game::AddMoney(m.cash);
        if(m.ok && s_nHkPendingSetup == 0xFFFF && m.id) s_nHkPendingSetup = m.id;
        return;
    }
    }
}

// ---- tick -------------------------------------------------------------------------------------
static void HkEnter(uint16_t id)
{
    HkHomeC* c = HkGet(id); if(!c) return;
    const PktHomeHead& h = c->h;
    const int ped = Game::LocalPed(); if(ped <= 0) return;
    if((h.flags & HKF_LOCKED) && !HkCanEdit(h) && !HkHasKey(h)) { GameText("~r~Locked", 1500, 3); return; }
    if(h.interior <= 0 || h.interior >= HK_INTERIOR_COUNT) { SysMsg(COL_YELLOW, "%s has no interior yet (owner: /edithouseinteriors).", h.name); return; }
    if(Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Get out of the vehicle first."); return; }
    const HkInterior& in = g_HkInteriors[h.interior];
    Game::GetCharPos(ped, s_HkOutPos);
    Game::EnterArea(in.area);
    Game::Teleport(ped, { in.x, in.y, in.z }, in.a);
    s_nHkInside = id; s_bHkExitArmed = false;
    GameText(h.name, 2000, 1);
}
static void HkExit()
{
    if(!s_nHkInside) return;
    const int ped = Game::LocalPed();
    HkHomeC* c = HkGet(s_nHkInside);
    Vec3 out = s_HkOutPos; float hd = 0;
    if(c) { out = HkDoor(c->h); hd = c->h.heading + 180.0f; float r = hd * 0.0174533f; out.x += -sinf(r) * 1.6f; out.y += cosf(r) * 1.6f; out.z += 0.3f; }
    s_nHkInside = 0;
    Game::EnterArea(0);
    if(ped > 0) Game::Teleport(ped, out, hd);
    s_nHkDoorShown = c ? c->h.id : 0;
}
static void HkDoorDlg(int response, int, const char*, int user)
{
    HkHomeC* c = HkGet((uint16_t)user); if(!c || !response) return;
    const PktHomeHead& h = c->h;
    if((h.flags & HKF_SALE) && !HkMine(h))
    {
        if(!RpOnServer() && Game::GetMoney() < h.price) { SysMsg(COL_RED, "You need $%d (you have $%d).", h.price, Game::GetMoney()); return; }
        PktHomeCmd b = HkCmd(HC_BUY, h.id, h.price); HkSendCmd(b);
        return;
    }
    HkEnter(h.id);
}
static void HkShowDoor(const PktHomeHead& h)
{
    char body[600];
    const bool sale = (h.flags & HKF_SALE) && !HkMine(h);
    snprintf(body, sizeof(body),
        "{FFFFFF}House: {00BFFF}%s {FFFFFF}[H:%u]\n"
        "{FFFFFF}Owner: {FFFF00}%s\n"
        "{FFFFFF}Slot: {33CC55}%dx%d {FFFFFF}squares   Objects: {33CC55}%u\n"
        "{FFFFFF}Status: %s\n%s",
        h.name, h.id, h.ownerName[0] ? h.ownerName : "Nobody (the server)", h.w, h.h, h.nObj,
        sale ? "{33CC55}For sale" : ((h.flags & HKF_LOCKED) ? "{FF4444}Locked" : "{33CC55}Open"),
        sale ? (std::string("{FFFFFF}Price: {33CC55}$") + std::to_string(h.price) + "\n{A9C4E4}Tap Buy or type /buyhouse").c_str()
             : (h.interior ? "{A9C4E4}Tap Enter to go in (/exithouse to leave)" : "{A9C4E4}No interior yet"));
    char cap[64]; snprintf(cap, sizeof(cap), "{FFFFFF}House {00BFFF}[H:%u]", h.id);
    ShowDialog(0, DIALOG_STYLE_MSGBOX, cap, body, sale ? "Buy" : (h.interior ? "Enter" : "OK"), (sale || h.interior) ? "Close" : "", HkDoorDlg, h.id);
}
static void HkProcess(int ped, uint64_t now)
{
    static bool s_bWasActive = false;
    const bool act = HkActive();
    if(!act) { if(s_bWasActive) { if(s_nHkInside) { s_nHkInside = 0; } HkReset(); } s_bWasActive = false; return; }
    if(!s_bWasActive) { s_nHkIndexAt = now + 1500; }
    s_bWasActive = true;
    if(s_nHkIndexAt && now >= s_nHkIndexAt && s_Hk.empty()) { PktHomeCmd c = HkCmd(HC_INDEX); HkSendCmd(c); s_nHkIndexAt = now + 4000; }   // 3.39.1: again until the list comes
    {   // 3.39.1: the house list again every 30 s (a lost index page = houses missing after a rejoin)
        static uint64_t s_nIdxAgain = 0;
        if(!s_nIdxAgain || now >= s_nIdxAgain) { if(s_nIdxAgain) { PktHomeCmd c = HkCmd(HC_INDEX); HkSendCmd(c); } s_nIdxAgain = now + 30000; }
    }
    // 3.39.1: a house whose furniture / buildings didn't all arrive (lost UDP page): ask for it again
    for(auto& kv : s_Hk)
    {
        HkHomeC& c = kv.second;
        if(!c.haveHead || c.rxGot >= (int)c.rx.size() || c.retries > 20) continue;
        if(now - c.headAt > 2500 && now - c.reqAt > 2500)
        {
            c.reqAt = now; ++c.retries;
            PktHomeCmd q = HkCmd(HC_GET, kv.first); HkSendCmd(q);
        }
    }
    // ask for the houses I don't have (about 10 a second)
    if(!s_HkWant.empty() && now >= s_nHkWantAt)
    {
        s_nHkWantAt = now + 100;
        uint16_t id = s_HkWant.front(); s_HkWant.erase(s_HkWant.begin()); s_HkWant.push_back(id);   // round robin until the head comes
        auto it = s_Hk.find(id);
        if(it == s_Hk.end() || now - it->second.reqAt > 3000) { s_Hk[id].reqAt = now; PktHomeCmd c = HkCmd(HC_GET, id); HkSendCmd(c); }
    }
    if(ped <= 0 || !myDrawValid) return;
    if(s_nHkPendingSetup && s_nHkPendingSetup != 0xFFFF)
    {
        HkHomeC* c = HkGet(s_nHkPendingSetup);
        if(c) { GameText("~g~House created", 2500, 3); s_nHkPendingSetup = 0; }
    }
    // stream
    if(now >= s_nHkStreamAt)
    {
        s_nHkStreamAt = now + 250;
        int budget = 8;
        for(auto& kv : s_Hk)
        {
            HkHomeC& c = kv.second; if(!c.haveHead) continue;
            { Vec3 dd = HkDoor(c.h); if(fabsf(dd.x - myDrawPos.x) < 80.0f && fabsf(dd.y - myDrawPos.y) < 80.0f) HkDoorGround(c, now, true); }   // 3.39.1
            Vec3 m = HkCentre(c.h);
            const float dx = m.x - myDrawPos.x, dy = m.y - myDrawPos.y, d = sqrtf(dx * dx + dy * dy);
            const bool inside = s_nHkInside == kv.first;
            const bool near = !s_nHkInside && d < (c.inst.empty() && !c.prefabInst ? 180.0f : 220.0f);
            if(!near && !inside && c.prefabInst == 0) { bool any = false; for(int i : c.inst) if(i > 0) { any = true; break; } if(!any) continue; }
            HkReconcile(c, near, inside, budget);
        }
    }
    // the door markers: walk in -> the house dialog
    if(!s_nHkInside)
    {
        int nearId = -1; float nd = 1.4f;
        for(auto& kv : s_Hk)
        {
            if(!kv.second.haveHead) continue;
            Vec3 d = HkDoor(kv.second.h);
            float dist = sqrtf((d.x - myDrawPos.x) * (d.x - myDrawPos.x) + (d.y - myDrawPos.y) * (d.y - myDrawPos.y));
            if(fabsf(d.z - myDrawPos.z) < 2.5f && dist < nd) { nd = dist; nearId = kv.first; }
        }
        if(nearId < 0) s_nHkDoorShown = 0;
        else if(nearId != s_nHkDoorShown && !SampDialogOpen() && !Game::IsCharInAnyCar(ped) && !s_HkSet.open && !s_HkEd.open)
        {
            s_nHkDoorShown = (uint16_t)nearId;
            HkShowDoor(s_Hk[(uint16_t)nearId].h);
        }
    }
    else
    {
        HkHomeC* c = HkGet(s_nHkInside);
        if(!c || !c->h.interior) { HkExit(); return; }
        const HkInterior& in = g_HkInteriors[c->h.interior];
        float dist = sqrtf((in.x - myDrawPos.x) * (in.x - myDrawPos.x) + (in.y - myDrawPos.y) * (in.y - myDrawPos.y));
        if(dist > 2.2f) s_bHkExitArmed = true;
        else if(s_bHkExitArmed && dist < 1.0f && !SampDialogOpen()) { s_bHkExitArmed = false; HkExit(); }
        if(fabsf(myDrawPos.z - in.z) > 60.0f) { s_nHkInside = 0; }   // teleported away / died
    }
    // the editor: send the moved object (a held arrow: ~7 a second)
    if(s_HkEd.open && s_HkEd.dirty && now >= s_HkEd.sendAt)
    {
        HkHomeC* c = HkGet(s_HkEd.id);
        if(c && s_HkEd.dirtyIdx >= 0 && s_HkEd.dirtyIdx < (int)c->obj.size())
        {
            PktHomeCmd m = HkCmd(HC_OBJ_SET, c->h.id); m.objIdx = (uint16_t)s_HkEd.dirtyIdx; m.obj = c->obj[s_HkEd.dirtyIdx]; HkSendCmd(m);
        }
        s_HkEd.dirty = false; s_HkEd.sendAt = now + 140;
    }
}

// ---- drawing ----------------------------------------------------------------------------------
static void HkText3D(ImDrawList* dl, const Vec3& at, const char* text, float d)
{
    float sx, sy, depth;
    if(!Game::WorldToScreen(at, sx, sy, depth)) return;
    ImFont* f = UI::Font();
    float fs = UI::S(28.0f) * fmaxf(0.55f, 1.0f - d / 45.0f);
    std::vector<std::string> lines; std::vector<SampSeg> segs;
    SampSplit(text, '\n', lines);
    float y = sy - fs * lines.size();
    for(auto& l : lines)
    {
        SampSplitColors(l.c_str(), IM_COL32_WHITE, segs);
        float w = SampSegsWidth(f, fs, segs);
        std::vector<SampSeg> sh = segs; for(auto& g : sh) g.col = IM_COL32(0, 0, 0, 255);
        const float o = fmaxf(1.0f, fs * 0.05f);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f + ox * o, y + oy * o), sh);
        SampDrawSegs(dl, f, fs, ImVec2(sx - w * 0.5f, y), segs);
        y += fs;
    }
}
static void HkArrow(ImDrawList* dl, const Vec3& p, float now, ImU32 col)
{
    ImVec2 s; Vec3 a = p; a.z += 1.25f + 0.12f * sinf(now * 3.0f);
    if(!BcwW2S(a, s)) return;
    float d = BcwCamDist(p); float r = UI::S(26.0f) * fmaxf(0.35f, 1.0f - d / 60.0f);
    dl->AddTriangleFilled(ImVec2(s.x - r, s.y - r * 0.6f), ImVec2(s.x + r, s.y - r * 0.6f), ImVec2(s.x, s.y + r * 0.7f), col);
    dl->AddTriangle(ImVec2(s.x - r, s.y - r * 0.6f), ImVec2(s.x + r, s.y - r * 0.6f), ImVec2(s.x, s.y + r * 0.7f), IM_COL32(0, 0, 0, 160), UI::S(1.5f));
}
static void HkSetupGround(ImDrawList* dl, float now);
static void HkDrawWorld(ImDrawList* dl, float now)
{
    if(!HkActive() || !myDrawValid || Game::IsPaused()) return;
    for(auto& kv : s_Hk)
    {
        if(!kv.second.haveHead) continue;
        const PktHomeHead& h = kv.second.h;
        if(s_nHkInside) break;
        Vec3 d = HkDoor(h);
        float dist = sqrtf((d.x - myDrawPos.x) * (d.x - myDrawPos.x) + (d.y - myDrawPos.y) * (d.y - myDrawPos.y) + (d.z - myDrawPos.z) * (d.z - myDrawPos.z));
        if(dist > 60.0f) continue;
        d = HkDoorGround(kv.second, (uint64_t)MP_NowMs());   // 3.39.1: on the ground, not floating (the door point was the body centre)
        MkBeam(dl, d, 0.55f, 1.5f, 0xFF1A1AFF, 1.0f);   // the red glowing column
        HkArrow(dl, d, now, IM_COL32(255, 30, 30, 230));
        if(dist > 30.0f || !OccPoint({ d.x, d.y, d.z + 1.6f })) continue;
        char t[300];
        if((h.flags & HKF_SALE) && !HkMine(h))
            snprintf(t, sizeof(t), "{00BFFF}[H:%u]\n{FFFF00}Owner: {FFFFFF}%s\n{FFFFFF}%s\n{33CC55}For sale: ${FFFFFF}%d {A9C4E4}/buyhouse", h.id, h.ownerName[0] ? h.ownerName : "Server", h.name, h.price);
        else
            snprintf(t, sizeof(t), "{00BFFF}[H:%u]\n{FFFF00}Owner: {FFFFFF}%s\n{FFFFFF}%s%s", h.id, h.ownerName[0] ? h.ownerName : "Server", h.name, (h.flags & HKF_LOCKED) ? "\n{FF4444}Locked" : "");
        HkText3D(dl, { d.x, d.y, d.z + 2.3f }, t, dist);
    }
    if(s_nHkInside)
    {
        HkHomeC* c = HkGet(s_nHkInside);
        if(c && c->h.interior)
        {
            const HkInterior& in = g_HkInteriors[c->h.interior];
            Vec3 e = { in.x, in.y, in.z - 0.5f };
            MkBeam(dl, e, 0.5f, 1.3f, 0xFF1A1AFF, 0.8f);
            HkText3D(dl, { e.x, e.y, e.z + 2.0f }, "{FFFFFF}Exit", 3.0f);
        }
    }
    HkSetupGround(dl, now);
    if(s_HkEd.open)
    {
        HkHomeC* c = HkGet(s_HkEd.id);
        if(c && s_HkEd.sel >= 0 && s_HkEd.sel < (int)c->obj.size())
        {
            Vec3 p; float rz; if(HkObjWorld(c->h, c->obj[s_HkEd.sel], p, rz)) { MkBeam(dl, p, 0.35f, 2.5f, 0xFFDD33FF, 0.9f); HkArrow(dl, { p.x, p.y, p.z + 1.4f }, now, IM_COL32(255, 221, 51, 230)); }
        }
    }
}

// ---- /setup: the slot ------------------------------------------------------------------------
static std::vector<int> s_HkPrefabs;   // indices into g_HkModels of the prefab buildings
static void HkPrefabList() { if(s_HkPrefabs.empty()) for(int i = 0; i < HK_MODEL_COUNT; ++i) if(g_HkModels[i].cat == HKC_PREFAB) s_HkPrefabs.push_back(i); }
#define HK_SETUP_MAX 24   // the editor lets you type up to 24 (red over 16: the limit)
static bool HkCellBad(int x, int y)
{
    if(x >= HK_SLOT_MAX || y >= HK_SLOT_MAX) return true;
    float wx, wy; HkLocalToWorld(s_HkSet.ox, s_HkSet.oy, s_HkSet.hdg, (x + 0.5f) * HK_TILE, (y + 0.5f) * HK_TILE, wx, wy);
    for(auto& kv : s_Hk)
    {
        if(!kv.second.haveHead || kv.first == s_HkSet.id) continue;
        const PktHomeHead& o = kv.second.h; float lx, ly; HkWorldToLocal(o.x, o.y, o.heading, wx, wy, lx, ly);
        if(lx >= 0 && ly >= 0 && lx <= o.w * HK_TILE && ly <= o.h * HK_TILE) return true;
    }
    return false;
}
static bool HkSetupValid()
{
    if(s_HkSet.w < 1 || s_HkSet.h < 1 || s_HkSet.w > HK_SLOT_MAX || s_HkSet.h > HK_SLOT_MAX) return false;
    for(int x = 0; x < s_HkSet.w; ++x) for(int y = 0; y < s_HkSet.h; ++y) if(HkCellBad(x, y)) return false;
    return true;
}
static void HkSetupCentreOnMe()
{
    const int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me);
    float ox, oy; HkLocalToWorld(0, 0, s_HkSet.hdg, -s_HkSet.w * HK_TILE * 0.5f, -s_HkSet.h * HK_TILE * 0.5f, ox, oy);
    s_HkSet.ox = me.x + ox; s_HkSet.oy = me.y + oy; s_HkSet.oz = Game::GroundZ(me.x, me.y, me.z + 2.0f);
    if(!(s_HkSet.oz > -100.0f)) s_HkSet.oz = me.z - 1.0f;
}
static void HkOpenSetup(uint16_t id)
{
    HkHomeC* c = HkGet(id); if(!c) return;
    HkPrefabList();
    s_HkSet = HkSetup(); s_HkSet.open = true; s_HkSet.id = id;
    s_HkSet.w = c->h.w; s_HkSet.h = c->h.h; s_HkSet.ox = c->h.x; s_HkSet.oy = c->h.y; s_HkSet.oz = c->h.z; s_HkSet.hdg = c->h.heading;
    for(size_t i = 0; i < s_HkPrefabs.size(); ++i) if(g_HkModels[s_HkPrefabs[i]].model == c->h.prefab) s_HkSet.prefabIdx = (int)i + 1;
    s_HkEd.open = false;
    SysMsg(COL_SYSTEM, "Setup %s: pick the slot size (1x1 - 16x16, each square 2x2 m). Tap a square to light it on the ground. Green = fine, red = over the limit / taken.", c->h.name);
}
static void HkSetupGround(ImDrawList* dl, float now)
{
    if(!s_HkSet.open) return;
    const int W = std::min(s_HkSet.w, HK_SETUP_MAX), H = std::min(s_HkSet.h, HK_SETUP_MAX);
    const float key[4] = { s_HkSet.ox, s_HkSet.oy, s_HkSet.hdg, (float)(W * 100 + H) };
    if(memcmp(key, s_HkSet.gzKey, sizeof(key)) || s_HkSet.gzW != W || s_HkSet.gzH != H)
    {
        memcpy(s_HkSet.gzKey, key, sizeof(key)); s_HkSet.gzW = W; s_HkSet.gzH = H;
        s_HkSet.gz.assign((W + 1) * (H + 1), s_HkSet.oz);
        for(int x = 0; x <= W; ++x) for(int y = 0; y <= H; ++y)
        {
            float wx, wy; HkLocalToWorld(s_HkSet.ox, s_HkSet.oy, s_HkSet.hdg, x * HK_TILE, y * HK_TILE, wx, wy);
            float g = Game::GroundZ(wx, wy, s_HkSet.oz + 4.0f);
            s_HkSet.gz[x * (H + 1) + y] = (g > -100.0f && fabsf(g - s_HkSet.oz) < 6.0f) ? g + 0.06f : s_HkSet.oz + 0.06f;
        }
    }
    auto corner = [&](int x, int y, ImVec2& s) -> bool
    {
        Vec3 p; HkLocalToWorld(s_HkSet.ox, s_HkSet.oy, s_HkSet.hdg, x * HK_TILE, y * HK_TILE, p.x, p.y); p.z = s_HkSet.gz[x * (H + 1) + y];
        return BcwW2S(p, s);
    };
    const float pulse = 0.5f + 0.5f * sinf(now * 5.0f);
    for(int x = 0; x < W; ++x) for(int y = 0; y < H; ++y)
    {
        ImVec2 a, b, c, d;
        if(!corner(x, y, a) || !corner(x + 1, y, b) || !corner(x + 1, y + 1, c) || !corner(x, y + 1, d)) continue;
        const bool bad = HkCellBad(x, y), sel = x == s_HkSet.selX && y == s_HkSet.selY;
        ImU32 fill = bad ? IM_COL32(230, 40, 40, 80) : IM_COL32(40, 220, 80, 60);
        if(sel) fill = bad ? IM_COL32(255, 60, 60, (int)(120 + 100 * pulse)) : IM_COL32(120, 255, 140, (int)(110 + 110 * pulse));
        dl->AddQuadFilled(a, b, c, d, fill);
        dl->AddQuad(a, b, c, d, bad ? IM_COL32(255, 70, 70, 220) : IM_COL32(60, 255, 110, 200), UI::S(sel ? 3.0f : 1.5f));
    }
}
static void HkSetupSave()
{
    if(!HkSetupValid()) { SysMsg(COL_RED, "Red squares: make the slot 16x16 or smaller and keep it off other houses."); return; }
    PktHomeCmd c = HkCmd(HC_SETUP, s_HkSet.id, s_HkSet.prefabIdx ? g_HkModels[s_HkPrefabs[s_HkSet.prefabIdx - 1]].model : -1);
    c.w = (uint8_t)s_HkSet.w; c.h = (uint8_t)s_HkSet.h;
    c.pos[0] = s_HkSet.ox; c.pos[1] = s_HkSet.oy; c.pos[2] = s_HkSet.oz; c.pos[3] = s_HkSet.hdg;
    HkSendCmd(c);
    s_HkSet.open = false;
}
static bool HkStepper(const char* label, int& v, int lo, int hi, int limit)
{
    bool ch = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label); ImGui::SameLine(UI::S(150));
    if(ImGui::Button("-", ImVec2(UI::S(70), UI::S(56))) && v > lo) { --v; ch = true; }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, v > limit ? ImVec4(1, 0.3f, 0.3f, 1) : ImVec4(0.4f, 1, 0.5f, 1));
    ImGui::Text("%2d", v); ImGui::PopStyleColor();
    ImGui::SameLine();
    if(ImGui::Button("+", ImVec2(UI::S(70), UI::S(56))) && v < hi) { ++v; ch = true; }
    ImGui::PopID();
    return ch;
}
static void HkRenderSetup()
{
    if(!s_HkSet.open) return;
    HkHomeC* hc = HkGet(s_HkSet.id); if(!hc) { s_HkSet.open = false; return; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float ww = fminf(UI::S(620), ds.x * 0.48f);
    ImGui::SetNextWindowPos(ImVec2(ds.x - ww - UI::S(16), UI::S(90)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ww, ds.y - UI::S(180)), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    if(ImGui::Begin("##hksetup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextColored(ImVec4(0, 0.75f, 1, 1), "SETUP  [H:%u] %s", hc->h.id, hc->h.name);
        ImGui::TextDisabled("Each square = %.0fx%.0f m. Limit %dx%d.", HK_TILE, HK_TILE, HK_SLOT_MAX, HK_SLOT_MAX);
        int ow = s_HkSet.w, oh = s_HkSet.h;
        HkStepper("Width", s_HkSet.w, 1, HK_SETUP_MAX, HK_SLOT_MAX);
        HkStepper("Height", s_HkSet.h, 1, HK_SETUP_MAX, HK_SLOT_MAX);
        if(ImGui::Button("1x1", ImVec2(UI::S(110), UI::S(52)))) { s_HkSet.w = s_HkSet.h = 1; } ImGui::SameLine();
        if(ImGui::Button("4x4", ImVec2(UI::S(110), UI::S(52)))) { s_HkSet.w = s_HkSet.h = 4; } ImGui::SameLine();
        if(ImGui::Button("8x8", ImVec2(UI::S(110), UI::S(52)))) { s_HkSet.w = s_HkSet.h = 8; } ImGui::SameLine();
        if(ImGui::Button("16x16", ImVec2(UI::S(130), UI::S(52)))) { s_HkSet.w = s_HkSet.h = 16; }
        if(ow != s_HkSet.w || oh != s_HkSet.h) { s_HkSet.selX = s_HkSet.selY = -1; }
        if(ImGui::Button("Turn -15", ImVec2(UI::S(170), UI::S(52)))) s_HkSet.hdg = fmodf(s_HkSet.hdg - 15.0f + 360.0f, 360.0f); ImGui::SameLine();
        if(ImGui::Button("Turn +15", ImVec2(UI::S(170), UI::S(52)))) s_HkSet.hdg = fmodf(s_HkSet.hdg + 15.0f, 360.0f); ImGui::SameLine();
        if(ImGui::Button("Move here", ImVec2(UI::S(190), UI::S(52)))) HkSetupCentreOnMe();
        // the slot squares: tap one -> it glows on the ground
        const int W = std::min(s_HkSet.w, HK_SETUP_MAX), H = std::min(s_HkSet.h, HK_SETUP_MAX);
        const float avail = ImGui::GetContentRegionAvail().x;
        const float cell = fminf(UI::S(36), (avail - UI::S(4)) / (float)std::max(W, H));
        ImVec2 g0 = ImGui::GetCursorScreenPos(); ImDrawList* wdl = ImGui::GetWindowDrawList();
        ImGui::InvisibleButton("##grid", ImVec2(cell * W, cell * H));
        if(ImGui::IsItemClicked())
        {
            ImVec2 m = ImGui::GetIO().MousePos;
            int cx = (int)((m.x - g0.x) / cell), cy = H - 1 - (int)((m.y - g0.y) / cell);
            if(cx >= 0 && cy >= 0 && cx < W && cy < H) { if(s_HkSet.selX == cx && s_HkSet.selY == cy) s_HkSet.selX = s_HkSet.selY = -1; else { s_HkSet.selX = cx; s_HkSet.selY = cy; } }
        }
        for(int x = 0; x < W; ++x) for(int y = 0; y < H; ++y)
        {
            ImVec2 a(g0.x + x * cell + 1, g0.y + (H - 1 - y) * cell + 1), b(a.x + cell - 2, a.y + cell - 2);
            const bool bad = HkCellBad(x, y), sel = x == s_HkSet.selX && y == s_HkSet.selY;
            wdl->AddRectFilled(a, b, sel ? IM_COL32(255, 221, 51, 230) : (bad ? IM_COL32(220, 50, 50, 200) : IM_COL32(40, 190, 80, 200)));
        }
        if(s_HkSet.selX >= 0) ImGui::Text("Square %d,%d %s", s_HkSet.selX + 1, s_HkSet.selY + 1, HkCellBad(s_HkSet.selX, s_HkSet.selY) ? "(red: not allowed)" : "(green: fine)");
        else ImGui::TextDisabled("Tap a square to light it up on the ground.");
        // the building
        HkPrefabList();
        const int np = (int)s_HkPrefabs.size();
        if(ImGui::Button("<##pf", ImVec2(UI::S(70), UI::S(52)))) s_HkSet.prefabIdx = (s_HkSet.prefabIdx + np) % (np + 1);
        ImGui::SameLine();
        ImGui::Text("Building: %s", s_HkSet.prefabIdx ? g_HkModels[s_HkPrefabs[s_HkSet.prefabIdx - 1]].name : "None (build your own)");
        ImGui::SameLine();
        if(ImGui::Button(">##pf", ImVec2(UI::S(70), UI::S(52)))) s_HkSet.prefabIdx = (s_HkSet.prefabIdx + 1) % (np + 1);
        ImGui::Spacing();
        const bool ok = HkSetupValid();
        if(!ok) ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "Red squares: 16x16 is the limit / another house is there.");
        ImGui::PushStyleColor(ImGuiCol_Button, ok ? ImVec4(0.1f, 0.5f, 0.2f, 1) : ImVec4(0.25f, 0.25f, 0.25f, 1));
        if(ImGui::Button("Save", ImVec2(UI::S(220), UI::S(64))) && ok) HkSetupSave();
        ImGui::PopStyleColor(); ImGui::SameLine();
        if(ImGui::Button("Close", ImVec2(UI::S(220), UI::S(64)))) s_HkSet.open = false;
    }
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
}

// ---- /edithouse + /edithouseinteriors ---------------------------------------------------------
static int HkCatFirst(int cat) { for(int i = 0; i < HK_MODEL_COUNT; ++i) if(g_HkModels[i].cat == cat) return i; return 0; }
static void HkOpenEdit(uint16_t id, bool inside)
{
    HkHomeC* c = HkGet(id); if(!c) return;
    s_HkEd = HkEdit(); s_HkEd.open = true; s_HkEd.id = id; s_HkEd.inside = inside;
    s_HkEd.cat = inside ? HKC_LIVING : HKC_BUILD; s_HkEd.model = HkCatFirst(s_HkEd.cat);
    s_HkSet.open = false;
    if(inside && !c->h.interior) SysMsg(COL_YELLOW, "Pick an interior first (top of the editor).");
    if(inside && c->h.interior && s_nHkInside != id) HkEnter(id);
}
static bool HkEdShows(const HkObj& o) { return ((o.flags & HKO_INTERIOR) != 0) == s_HkEd.inside; }
static void HkEdNextObj(HkHomeC& c, int dir)
{
    const int n = (int)c.obj.size(); if(!n) { s_HkEd.sel = -1; return; }
    int i = s_HkEd.sel;
    for(int k = 0; k < n; ++k) { i = (i + dir + n) % n; if(HkEdShows(c.obj[i])) { s_HkEd.sel = i; return; } }
    s_HkEd.sel = -1;
}
static void HkEdAdd(HkHomeC& c)
{
    const int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; Game::GetCharPos(ped, me); float hd = Game::GetCharHeading(ped), r = hd * 0.0174533f;
    Vec3 at = { me.x - sinf(r) * 2.0f, me.y + cosf(r) * 2.0f, me.z - 1.0f };
    HkObj o; memset(&o, 0, sizeof(o)); o.model = g_HkModels[s_HkEd.model].model;
    if(s_HkEd.inside)
    {
        if(!c.h.interior) { SysMsg(COL_RED, "Pick an interior first."); return; }
        const HkInterior& in = g_HkInteriors[c.h.interior];
        o.flags = HKO_INTERIOR; o.x = (int16_t)((at.x - in.x) * 100); o.y = (int16_t)((at.y - in.y) * 100); o.z = (int16_t)((at.z - in.z) * 100); o.rz = (int16_t)(hd * 10);
    }
    else
    {
        float lx, ly; HkWorldToLocal(c.h.x, c.h.y, c.h.heading, at.x, at.y, lx, ly);
        lx = fmaxf(0, fminf(lx, c.h.w * HK_TILE)); ly = fmaxf(0, fminf(ly, c.h.h * HK_TILE));
        o.x = (int16_t)(lx * 100); o.y = (int16_t)(ly * 100); o.z = (int16_t)((at.z - c.h.z) * 100); o.rz = (int16_t)(fmodf(hd - c.h.heading + 360.0f, 360.0f) * 10);
    }
    PktHomeCmd m = HkCmd(HC_OBJ_ADD, c.h.id); m.obj = o; HkSendCmd(m);
    s_HkEd.sel = (int)c.obj.size();   // the new one (when it comes back)
}
static void HkEdMove(HkHomeC& c, int axis, float amount)
{
    if(s_HkEd.sel < 0 || s_HkEd.sel >= (int)c.obj.size()) return;
    HkObj& o = c.obj[s_HkEd.sel];
    const int cm = (int)(amount * 100);
    if(axis == 0) o.x = (int16_t)std::max(-32000, std::min(32000, o.x + cm));
    else if(axis == 1) o.y = (int16_t)std::max(-32000, std::min(32000, o.y + cm));
    else if(axis == 2) o.z = (int16_t)std::max(-32000, std::min(32000, o.z + cm));
    else { int v = o.rz + (int)(amount * 10); while(v < 0) v += 3600; o.rz = (int16_t)(v % 3600); }
    s_HkEd.dirty = true; s_HkEd.dirtyIdx = s_HkEd.sel;
}
static void HkRenderEdit()
{
    if(!s_HkEd.open) return;
    HkHomeC* hc = HkGet(s_HkEd.id); if(!hc) { s_HkEd.open = false; return; }
    HkHomeC& c = *hc;
    if(s_HkEd.sel >= (int)c.obj.size() && s_HkEd.sel >= 0 && !c.obj.empty() && s_HkEd.sel > (int)c.obj.size()) s_HkEd.sel = -1;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float ww = fminf(UI::S(640), ds.x * 0.5f);
    ImGui::SetNextWindowPos(ImVec2(ds.x - ww - UI::S(16), UI::S(90)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ww, ds.y - UI::S(180)), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const ImVec2 bs(UI::S(80), UI::S(56));
    if(ImGui::Begin("##hkedit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextColored(ImVec4(0, 0.75f, 1, 1), "%s  [H:%u] %s", s_HkEd.inside ? "INTERIOR" : "EDIT HOUSE", c.h.id, c.h.name);
        int nIn = 0, nOut = 0; for(auto& o : c.obj) ((o.flags & HKO_INTERIOR) ? nIn : nOut)++;
        ImGui::TextDisabled("Objects %d/%d (outside %d, inside %d)", (int)c.obj.size(), HK_MAX_OBJ, nOut, nIn);
        if(s_HkEd.inside)
        {
            int ii = c.h.interior;
            if(ImGui::Button("<##in", bs)) { ii = (ii + HK_INTERIOR_COUNT - 1) % HK_INTERIOR_COUNT; PktHomeCmd m = HkCmd(HC_INTERIOR, c.h.id, ii); HkSendCmd(m); }
            ImGui::SameLine(); ImGui::Text("Interior: %s", g_HkInteriors[c.h.interior].name); ImGui::SameLine();
            if(ImGui::Button(">##in", bs)) { ii = (ii + 1) % HK_INTERIOR_COUNT; PktHomeCmd m = HkCmd(HC_INTERIOR, c.h.id, ii); HkSendCmd(m); }
            if(c.h.interior && s_nHkInside != c.h.id) { if(ImGui::Button("Go inside", ImVec2(UI::S(220), UI::S(52)))) HkEnter(c.h.id); }
        }
        ImGui::Separator();
        // catalogue
        if(ImGui::Button("<##cat", bs)) { s_HkEd.cat = (s_HkEd.cat + HKC_COUNT - 1) % HKC_COUNT; if(s_HkEd.cat == HKC_PREFAB) s_HkEd.cat = HKC_COUNT - 1; s_HkEd.model = HkCatFirst(s_HkEd.cat); }
        ImGui::SameLine(); ImGui::Text("%s", g_HkCatNames[s_HkEd.cat]); ImGui::SameLine();
        if(ImGui::Button(">##cat", bs)) { s_HkEd.cat = (s_HkEd.cat + 1) % HKC_COUNT; if(s_HkEd.cat == HKC_PREFAB) s_HkEd.cat = 1; s_HkEd.model = HkCatFirst(s_HkEd.cat); }
        if(ImGui::Button("<##mdl", bs)) { int i = s_HkEd.model; do { i = (i + HK_MODEL_COUNT - 1) % HK_MODEL_COUNT; } while(g_HkModels[i].cat != s_HkEd.cat); s_HkEd.model = i; }
        ImGui::SameLine(); ImGui::Text("%s (%u)", g_HkModels[s_HkEd.model].name, g_HkModels[s_HkEd.model].model); ImGui::SameLine();
        if(ImGui::Button(">##mdl", bs)) { int i = s_HkEd.model; do { i = (i + 1) % HK_MODEL_COUNT; } while(g_HkModels[i].cat != s_HkEd.cat); s_HkEd.model = i; }
        if(ImGui::Button("Add in front of me", ImVec2(UI::S(340), UI::S(58)))) HkEdAdd(c);
        ImGui::Separator();
        // the selected object
        if(ImGui::Button("<##obj", bs)) HkEdNextObj(c, -1);
        ImGui::SameLine();
        if(s_HkEd.sel >= 0 && s_HkEd.sel < (int)c.obj.size())
        {
            const HkModel* m = HkFindModel(c.obj[s_HkEd.sel].model);
            ImGui::Text("#%d %s", s_HkEd.sel + 1, m ? m->name : "object");
        }
        else ImGui::TextDisabled("No object picked");
        ImGui::SameLine();
        if(ImGui::Button(">##obj", bs)) HkEdNextObj(c, 1);
        static const float kSteps[3] = { 0.1f, 0.5f, 1.0f };
        for(int i = 0; i < 3; ++i)
        {
            if(i) ImGui::SameLine();
            char l[16]; snprintf(l, sizeof(l), "%.1f m", kSteps[i]);
            ImGui::PushStyleColor(ImGuiCol_Button, s_HkEd.step == i ? ImVec4(0.1f, 0.4f, 0.7f, 1) : ImVec4(0, 0, 0, 0.92f));
            if(ImGui::Button(l, ImVec2(UI::S(130), UI::S(52)))) s_HkEd.step = i;
            ImGui::PopStyleColor();
        }
        const float st = kSteps[s_HkEd.step];
        static const char* const kAx[4] = { "X", "Y", "Z", "Turn" };
        for(int a = 0; a < 4; ++a)
        {
            ImGui::PushID(a);
            ImGui::Text("%s", kAx[a]); ImGui::SameLine(UI::S(110));
            ImGui::Button("-", bs); if(ImGui::IsItemActive() && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDownDuration[0] > 0.35f)) HkEdMove(c, a, a == 3 ? -15.0f * (ImGui::IsItemActivated() ? 1 : 0.2f) : -st * (ImGui::IsItemActivated() ? 1 : 0.2f));
            ImGui::SameLine();
            ImGui::Button("+", bs); if(ImGui::IsItemActive() && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDownDuration[0] > 0.35f)) HkEdMove(c, a, a == 3 ? 15.0f * (ImGui::IsItemActivated() ? 1 : 0.2f) : st * (ImGui::IsItemActivated() ? 1 : 0.2f));
            ImGui::PopID();
        }
        if(ImGui::Button("Delete", ImVec2(UI::S(180), UI::S(58))) && s_HkEd.sel >= 0 && s_HkEd.sel < (int)c.obj.size())
        { PktHomeCmd m = HkCmd(HC_OBJ_DEL, c.h.id); m.objIdx = (uint16_t)s_HkEd.sel; HkSendCmd(m); s_HkEd.sel = -1; }
        ImGui::SameLine();
        if(ImGui::Button("Door here", ImVec2(UI::S(180), UI::S(58))) && !s_HkEd.inside)
        { PktHomeCmd m = HkCmd(HC_ENTRANCE, c.h.id); m.pos[0] = myDrawPos.x; m.pos[1] = myDrawPos.y; m.pos[2] = myDrawPos.z - 1.0f; HkSendCmd(m); }
        ImGui::SameLine();
        if(ImGui::Button("Close", ImVec2(UI::S(180), UI::S(58)))) s_HkEd.open = false;
    }
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
}
static void HkRender()
{
    if(!HkActive()) return;
    HkDrawWorld(ImGui::GetBackgroundDrawList(), (float)ImGui::GetTime());
    if(!SampDialogOpen()) { HkRenderSetup(); HkRenderEdit(); }
}

// ---- commands ---------------------------------------------------------------------------------
static void HkFurnish(HkHomeC& c, int level)
{
    if(!c.h.interior) { SysMsg(COL_RED, "Pick an interior first (/edithouseinteriors)."); return; }
    static const uint16_t kLow[] = { 1745, 1703, 1814, 1717, 2131, 2514, 2069 };
    static const uint16_t kMed[] = { 1701, 1702, 1704, 1815, 1749, 2147, 2135, 2115, 2120, 2516, 2071, 2001 };
    static const uint16_t kSwk[] = { 1798, 1753, 1761, 1818, 1786, 2118, 2124, 2149, 2519, 2523, 2073, 2241, 2100 };
    const uint16_t* set = level == 0 ? kLow : (level == 1 ? kMed : kSwk);
    const int n = level == 0 ? 7 : (level == 1 ? 12 : 13);
    for(int i = 0; i < n; ++i)
    {
        HkObj o; memset(&o, 0, sizeof(o)); o.model = set[i]; o.flags = HKO_INTERIOR;
        const float a = (float)i / n * 6.2831853f; o.x = (int16_t)(cosf(a) * 350); o.y = (int16_t)(sinf(a) * 350); o.z = -100; o.rz = (int16_t)(fmodf(a * 57.2958f + 180.0f, 360.0f) * 10);
        PktHomeCmd m = HkCmd(HC_OBJ_ADD, c.h.id); m.obj = o; HkSendCmd(m);
    }
    SysMsg(COL_GREEN, "Furniture on its way - move it with /edithouseinteriors.");
}
static void HkHelp()
{
    SysMsg(COL_YELLOW, "Home Kit: /create house <name>  /setup <name>  /buyhouse  /sellhouse <price>  /cancelsale  /edithouse  /edithouseinteriors");
    SysMsg(COL_YELLOW, "/enterhouse  /exithouse  /lockhouse  /renamehouse <new>  /deletehouse  /homedelete <name>  /myhouses  /houses  /houseinfo  /gotohouse <name>");
    SysMsg(COL_YELLOW, "/addobject <model>  /removeobject  /objects  /clearobjects  /furnish <low|med|swank>  /prefab <model|0>  /setentrance  /givehouse <id>  /housekey <id>");
}
static bool HkCommand(const char* cmd, const char* arg)
{
    if(!HkActive()) return false;
    // Mega Vil's own houses (/buyhouse at an RP house) go first
    if(RpOnServer() && (!strcmp(cmd, "buyhouse") || !strcmp(cmd, "sellhouse")) && HkNearest(4.0f, true) < 0) return false;
    char a2[64]; MP_CopyStr(a2, arg ? arg : "", sizeof(a2));
    std::string c = cmd;
    if(c == "create" && !strncasecmp(a2, "house", 5)) { c = "createhouse"; const char* p = a2 + 5; while(*p == ' ') ++p; memmove(a2, p, strlen(p) + 1); }
    auto pick = [&](bool needEdit) -> HkHomeC*
    {
        int id = HkPick(a2); if(id < 0) return nullptr;
        HkHomeC* h = HkGet((uint16_t)id);
        if(h && needEdit && !HkCanEdit(h->h)) { SysMsg(COL_RED, "%s is not your house.", h->h.name); return nullptr; }
        return h;
    };
    if(c == "createhouse")
    {
        if(!HkValidName(a2)) { SysMsg(COL_RED, "Use: /create house <name>  (2-23 letters, numbers, _ - .)"); return true; }
        PktHomeCmd m = HkCmd(HC_CREATE); MP_CopyStr(m.text, a2, sizeof(m.text));
        m.pos[0] = myDrawPos.x; m.pos[1] = myDrawPos.y; m.pos[2] = myDrawPos.z; m.pos[3] = Game::GetCharHeading(Game::LocalPed());
        HkSendCmd(m); s_nHkPendingSetup = 0xFFFF;
        return true;
    }
    if(c == "setup" || c == "setuphouse") { if(!a2[0] && s_nHkPendingSetup && s_nHkPendingSetup != 0xFFFF) snprintf(a2, sizeof(a2), "%u", s_nHkPendingSetup); HkHomeC* h = pick(true); if(h) HkOpenSetup(h->h.id); return true; }
    if(c == "edithouse") { HkHomeC* h = pick(true); if(h) HkOpenEdit(h->h.id, false); return true; }
    if(c == "edithouseinteriors" || c == "edithouseinterior" || c == "editinterior") { HkHomeC* h = pick(false); if(h) { if(!HkCanEdit(h->h) && !HkHasKey(h->h)) SysMsg(COL_RED, "You need the house key."); else HkOpenEdit(h->h.id, true); } return true; }
    if(c == "buyhouse")
    {
        int id = a2[0] ? HkByName(a2) : HkNearest(4.0f, true);
        HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h) { SysMsg(COL_RED, "Stand at the red marker of a house for sale."); return true; }
        if(!(h->h.flags & HKF_SALE)) { SysMsg(COL_RED, "%s is not for sale.", h->h.name); return true; }
        if(!RpOnServer() && Game::GetMoney() < h->h.price) { SysMsg(COL_RED, "You need $%d (you have $%d).", h->h.price, Game::GetMoney()); return true; }
        PktHomeCmd m = HkCmd(HC_BUY, h->h.id, h->h.price); HkSendCmd(m); return true;
    }
    if(c == "sellhouse")
    {
        const char* sp = strrchr(a2, ' '); int price = atoi(sp ? sp + 1 : a2); if(sp) *(char*)sp = 0; else a2[0] = 0;
        if(price <= 0) { SysMsg(COL_RED, "Use: /sellhouse <price>  (at your house)"); return true; }
        HkHomeC* h = pick(true); if(h) { PktHomeCmd m = HkCmd(HC_SELL, h->h.id, price); HkSendCmd(m); } return true;
    }
    if(c == "cancelsale") { HkHomeC* h = pick(true); if(h) { PktHomeCmd m = HkCmd(HC_SELL, h->h.id, 0); HkSendCmd(m); } return true; }
    if(c == "homedelete" || c == "deletehome" || c == "delhome" || c == "removehome")   // 3.39.1: /homedelete <home_name>
    {
        if(!a2[0]) { SysMsg(COL_RED, "Use: /homedelete <home name>  (your houses: /myhouses)"); return true; }
        int id = HkByName(a2); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h) { SysMsg(COL_RED, "No house called \"%s\" (your houses: /myhouses).", a2); return true; }
        if(!HkCanEdit(h->h)) { SysMsg(COL_RED, "%s is not your house.", h->h.name); return true; }
        static uint16_t s_DelId; s_DelId = h->h.id;
        char body[200]; snprintf(body, sizeof(body), "{FFFFFF}Delete the house {FFFF00}%s{FFFFFF} with all its buildings and furniture?\n{FF4444}This can't be undone.", h->h.name);
        ShowDialog(0, DIALOG_STYLE_MSGBOX, "{FFFFFF}Delete house", body, "Delete", "Cancel",
                   [](int resp, int, const char*, int) { if(resp) { PktHomeCmd m = HkCmd(HC_DELETE, s_DelId); HkSendCmd(m); } });
        return true;
    }
    if(c == "deletehouse") { HkHomeC* h = pick(true); if(h) { PktHomeCmd m = HkCmd(HC_DELETE, h->h.id); HkSendCmd(m); } return true; }
    if(c == "renamehouse")
    {
        int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h || !HkValidName(a2)) { SysMsg(COL_RED, "Use: /renamehouse <new name>  (at your house)"); return true; }
        PktHomeCmd m = HkCmd(HC_RENAME, h->h.id); MP_CopyStr(m.text, a2, sizeof(m.text)); HkSendCmd(m); return true;
    }
    if(c == "lockhouse" || c == "unlockhouse") { HkHomeC* h = pick(false); if(h) { PktHomeCmd m = HkCmd(HC_LOCK, h->h.id, c == "unlockhouse" ? 0 : !(h->h.flags & HKF_LOCKED)); HkSendCmd(m); } return true; }
    if(c == "enterhouse" || c == "enter") { int id = a2[0] ? HkByName(a2) : HkNearest(5.0f, true); if(id < 0) { if(c == "enter") return false; SysMsg(COL_RED, "Stand at a house door."); } else HkEnter((uint16_t)id); return true; }
    if(c == "exithouse") { if(!s_nHkInside) SysMsg(COL_RED, "You are not in a house."); else HkExit(); return true; }
    if(c == "myhouses")
    {
        int n = 0;
        for(auto& kv : s_Hk) if(kv.second.haveHead && HkMine(kv.second.h)) { const PktHomeHead& h = kv.second.h; SysMsg(COL_WHITE, "[H:%u] %s  %dx%d  %u objects%s", h.id, h.name, h.w, h.h, h.nObj, (h.flags & HKF_SALE) ? "  (for sale)" : ""); ++n; }
        if(!n) SysMsg(COL_SYSTEM, "You have no house. Go to an empty field and type /create house <name>.");
        return true;
    }
    if(c == "houses")
    {
        int n = 0;
        for(auto& kv : s_Hk) if(kv.second.haveHead && n < 12) { const PktHomeHead& h = kv.second.h; Vec3 d = HkDoor(h); float dist = sqrtf((d.x - myDrawPos.x) * (d.x - myDrawPos.x) + (d.y - myDrawPos.y) * (d.y - myDrawPos.y));
            SysMsg(COL_WHITE, "[H:%u] %s - %s - %.0f m%s", h.id, h.name, h.ownerName[0] ? h.ownerName : "Server", dist, (h.flags & HKF_SALE) ? " - for sale" : ""); ++n; }
        SysMsg(COL_SYSTEM, "%d houses on this server.", (int)s_Hk.size());
        return true;
    }
    if(c == "houseinfo") { HkHomeC* h = pick(false); if(h) HkShowDoor(h->h); return true; }
    if(c == "gotohouse")
    {
        HkHomeC* h = pick(false); if(!h) return true;
        if(!HkMine(h->h) && !HkHasKey(h->h) && !HkStaff()) { SysMsg(COL_RED, "Only to your own house (or one you have a key for)."); return true; }
        Vec3 d = HkDoor(h->h); float r = (h->h.heading + 180.0f) * 0.0174533f; d.x += -sinf(r) * 2.0f; d.y += cosf(r) * 2.0f; d.z += 0.5f;
        if(s_nHkInside) { s_nHkInside = 0; Game::EnterArea(0); }
        Game::Teleport(Game::LocalPed(), d, h->h.heading + 180.0f); s_nHkDoorShown = h->h.id; return true;
    }
    if(c == "addobject")
    {
        int model = atoi(a2); int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h || !HkCanEdit(h->h)) { SysMsg(COL_RED, "Stand at your house."); return true; }
        if(model <= 0 || !Game::IsKnownModel(model)) { SysMsg(COL_RED, "Use: /addobject <GTA SA model id>  (or pick from the /edithouse list)"); return true; }
        bool inside = s_nHkInside == h->h.id;
        s_HkEd.inside = inside; s_HkEd.id = h->h.id;
        int save = s_HkEd.model; int found = -1; for(int i = 0; i < HK_MODEL_COUNT; ++i) if(g_HkModels[i].model == model) found = i;
        if(found >= 0) { s_HkEd.model = found; HkEdAdd(*h); s_HkEd.model = save; }
        else
        {   // any real GTA SA model id
            const int ped = Game::LocalPed(); Vec3 me; Game::GetCharPos(ped, me); float hd = Game::GetCharHeading(ped), r = hd * 0.0174533f;
            Vec3 at = { me.x - sinf(r) * 2.0f, me.y + cosf(r) * 2.0f, me.z - 1.0f };
            HkObj o; memset(&o, 0, sizeof(o)); o.model = (uint16_t)model;
            if(inside && h->h.interior) { const HkInterior& in = g_HkInteriors[h->h.interior]; o.flags = HKO_INTERIOR; o.x = (int16_t)((at.x - in.x) * 100); o.y = (int16_t)((at.y - in.y) * 100); o.z = (int16_t)((at.z - in.z) * 100); }
            else { float lx, ly; HkWorldToLocal(h->h.x, h->h.y, h->h.heading, at.x, at.y, lx, ly); o.x = (int16_t)(lx * 100); o.y = (int16_t)(ly * 100); o.z = (int16_t)((at.z - h->h.z) * 100); }
            PktHomeCmd m = HkCmd(HC_OBJ_ADD, h->h.id); m.obj = o; HkSendCmd(m);
        }
        return true;
    }
    if(c == "removeobject")
    {
        int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h || !HkCanEdit(h->h) || h->obj.empty()) { SysMsg(COL_RED, "Stand next to an object of your house."); return true; }
        int best = -1; float bd = 6.0f;
        for(size_t i = 0; i < h->obj.size(); ++i) { Vec3 p; float rz; if(!HkObjWorld(h->h, h->obj[i], p, rz)) continue; float d = sqrtf((p.x - myDrawPos.x) * (p.x - myDrawPos.x) + (p.y - myDrawPos.y) * (p.y - myDrawPos.y) + (p.z - myDrawPos.z) * (p.z - myDrawPos.z)); if(d < bd) { bd = d; best = (int)i; } }
        if(best < 0) { SysMsg(COL_RED, "No object of your house within 6 m."); return true; }
        PktHomeCmd m = HkCmd(HC_OBJ_DEL, h->h.id); m.objIdx = (uint16_t)best; HkSendCmd(m); return true;
    }
    if(c == "objects") { HkHomeC* h = pick(false); if(h) { int k = 0; for(auto& o : h->obj) { if(k >= 15) break; const HkModel* m = HkFindModel(o.model); SysMsg(COL_WHITE, "#%d %s (%u) %s", ++k, m ? m->name : "object", o.model, (o.flags & HKO_INTERIOR) ? "inside" : "outside"); } SysMsg(COL_SYSTEM, "%d objects.", (int)h->obj.size()); } return true; }
    if(c == "clearobjects") { HkHomeC* h = pick(true); if(h) { PktHomeCmd m = HkCmd(HC_OBJ_CLEAR, h->h.id, 2); HkSendCmd(m); } return true; }
    if(c == "furnish")
    {
        int lvl = !strncasecmp(a2, "swank", 5) ? 2 : (!strncasecmp(a2, "med", 3) ? 1 : 0);
        int id = s_nHkInside ? s_nHkInside : HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h || !HkCanEdit(h->h)) { SysMsg(COL_RED, "Use it at (or in) your house: /furnish <low|med|swank>"); return true; }
        HkFurnish(*h, lvl); return true;
    }
    if(c == "prefab")
    {
        int model = atoi(a2); int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!h || !HkCanEdit(h->h)) { SysMsg(COL_RED, "Stand at your house."); return true; }
        if(model && !HkIsPrefab(model)) { HkPrefabList(); std::string l; for(int i : s_HkPrefabs) { l += std::to_string(g_HkModels[i].model) + " " + g_HkModels[i].name + ", "; } SysMsg(COL_SYSTEM, "Buildings: %s0 = none", l.c_str()); return true; }
        PktHomeCmd m = HkCmd(HC_PREFAB, h->h.id, model); HkSendCmd(m); return true;
    }
    if(c == "setentrance") { int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr; if(!h || !HkCanEdit(h->h)) { SysMsg(COL_RED, "Stand at your house."); return true; }
        PktHomeCmd m = HkCmd(HC_ENTRANCE, h->h.id); m.pos[0] = myDrawPos.x; m.pos[1] = myDrawPos.y; m.pos[2] = myDrawPos.z - 1.0f; HkSendCmd(m); return true; }
    if(c == "givehouse" || c == "housekey")
    {
        int pid = atoi(a2); int id = HkNearest(25.0f); HkHomeC* h = id >= 0 ? HkGet((uint16_t)id) : nullptr;
        if(!a2[0] || !h || !HkCanEdit(h->h)) { SysMsg(COL_RED, "Use it at your house: /%s <player id>", c.c_str()); return true; }
        PktHomeCmd m = HkCmd(c == "givehouse" ? HC_GIVE : HC_KEY, h->h.id, pid); HkSendCmd(m); return true;
    }
    if(c == "hkhelp" || c == "househelp" || c == "homekit") { HkHelp(); return true; }
    return false;
}
