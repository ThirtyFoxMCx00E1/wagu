// MP 3.51 - GM_TIMBER: the Timber (lumberjack) job (included by mp.cpp after mp_modes2.inc).
// After the Wild West Roleplay lumber job (github.com/ThirtyFoxMCx00E1/Wild-West-Roleplay,
// gamemodes/func/jobs/lumber + jobs/core.pwn): Birch / Oak / Yew trees with health, a chainsaw,
// a cutting progress bar that drops when you stop, stumps after the cut, a woodcutting skill
// (oak = level 2, yew = level 3), a "you feel tired" cooldown and /checkjobcooldown. Plus the
// screenshots' job (Screenshots/Screenshot_20261101-114544/114606.png): a red marker on a random tree
// (16 per mission), CUT, the tree falls (real hinge physics), "Loading timber" on your knees with the
// hands working on the log (BOMBER BOM_Plant_Loop), the log goes into the pickup's bed as real log
// objects (7 a load), the Timber Storage with the yellow "i" and its floating text, /unloadtimber, the
// paycheck and /mysalary.
// Spots: built-in (the Flint County Timber Storage barn + the pine forest around it, 3.51.1), or files/MP/timber.txt
// ("storage x y z", "start x y z h", "tree x y z" lines; /timberpos adds your position, staff only).
// 3.58: the Bobcat (422) is a job pickup too (/timbercar bobcat | sadler, or just drive your own pickup);
// the pickup spawns 0.12 m over the ground (no drop: wheel-detach mods broke it at +1 m); a cut tree
// breaks into 3 real log objects that scatter on the ground; PICK UP lifts one with both hands (CARRY
// liftup), you can carry 1 to 3 at once (crry_prtial while you walk); stand at the back of the pickup
// and CROUCH (or PUT IN) to lay them in the bed (CARRY putdwn).

#define TB_MISSION   16      // trees in one mission
#define TB_CAP       12      // 3.58: logs in one pickup load (a tree = 3 logs, was 7 timber = 7 trees)
#define TB_LOG_MODEL 841     // 3.58: a single fallen log (Prineside "trees" category); 1463 = woodpile fallback
#define TB_CARRY_MAX 3       // logs in your arms at once
#define TB_BED_SHOW  6       // log objects drawn in the bed (2 layers of 3)
#define TB_POOL      48
#define TB_TRUCK     543     // Sadler pickup
static const int kTbPickups[] = { 543, 422, 478, 554, 600, 605 };   // Sadler, Bobcat, Walton, Yosemite, Picador, Sadler (shit)
static const char* const kTbType[]  = { "Birch Tree", "Oak Tree", "Yew Tree" };
static const char* const kTbTypeC[] = { "{C9C9C9}", "{BA9F7B}", "{96744D}" };
static const uint32_t    kTbTypeRgba[] = { 0xC9C9C9FF, 0xBA9F7BFF, 0x96744DFF };
static const float kTbRound[] = { 50.0f, 34.0f, 25.0f };   // tree health per finished cut (WW-RP: 50 / 25 / 20)
static const float kTbRate[]  = { 30.0f, 22.0f, 16.0f };   // cut progress % a second while you hold CUT
static const float kTbPay[]   = { 8.50f, 10.50f, 14.00f }; // 3.58: paycheck per LOG (3 logs = the old 25.50 / 31.50 / 42.00 per tree)
// 3.58: the pickup bed per model (car space): bed centre y, rear tailgate y, bed floor z, half width
struct TbBed { int model; float bedY, rearY, floorZ, halfW; };
static const TbBed kTbBeds[] = { { 543, -1.55f, -2.75f, 0.05f, 0.55f }, { 422, -1.45f, -2.60f, 0.02f, 0.52f }, { 478, -1.55f, -2.70f, 0.10f, 0.55f },
                                 { 554, -1.75f, -3.00f, 0.15f, 0.60f }, { 600, -1.50f, -2.70f, 0.00f, 0.55f }, { 605, -1.55f, -2.75f, 0.05f, 0.55f } };
static const TbBed& TbBedOf(int model) { for(const TbBed& b : kTbBeds) if(b.model == model) return b; return kTbBeds[0]; }
static const int   kTbCool[]  = { 300, 210, 150 };          // tired cooldown (s) by skill level (WW-RP LVL1..3COOLDOWN)

enum { TB_OFF = 0, TB_GO, TB_CUT, TB_FALL, TB_LOAD, TB_SELL, TB_LOGS };   // 3.58: TB_LOGS = the logs lie on the ground (pick up / carry / load)
static struct
{
    bool     on = false;
    int      phase = TB_OFF;
    Vec3     storage = { -535.02f, -177.67f, 78.40f }, start = { -547.96f, -181.22f, 78.41f }; float startH = 270.0f;   // 3.51.1: the barn of the screenshot (samp-rvrp Timber Storage)
    uint8_t  anchorOf[TB_POOL] = {};
    Vec3     pool[TB_POOL]; int nPool = 0; bool spotsLoaded = false;
    int      mission[TB_MISSION]; uint8_t mType[TB_MISSION]; int idx = 0, done = 0;
    float    health = 100.0f, prog = 0.0f; bool holding = false; uint64_t lastHold = 0;
    int      tree = 0, stump = 0; uint64_t stumpUntil = 0; Vec3 treeAt = {}; float fallH = 0, ang = 0, vel = 0; int bounces = 0;
    float    loadProg = 0.0f; bool loadReq = false, cutReq = false;
    int      truck = 0, logs[TB_BED_SHOW] = {}, nLogs = 0, timber = 0;
    int      truckModel = TB_TRUCK; bool carLoaded = false;   // 3.58: 543 Sadler or 422 Bobcat (/timbercar)
    int      gLog[3] = {}; Vec3 gFrom[3] = {}, gPos[3] = {}; float gRot[3] = {}; bool gAlive[3] = {}; float scatT = 0; uint64_t settleAt = 0;
    int      carry[TB_CARRY_MAX] = {}; int nCarry = 0; int act = 0; uint64_t actUntil = 0; int actLog = -1; bool pickReq = false, putReq = false; uint64_t upperAt = 0;
    int      blip = 0; Vec3 blipAt = {}; bool arrived = false;
    float    paycheck = 0.0f, earned = 0.0f; int sold = 0, trees = 0;
    int      xp = 0; uint64_t tiredUntil = 0, sellAgainAt = 0; bool sellMsgDue = false;
    int      icon[2] = {}; uint64_t iconTry = 0;
    bool     builtIn = false, chainsawDue = false, truckDue = false, tpDue = false, loaded = false;
    uint64_t truckAt = 0; int truckTries = 0; uint64_t treeSnapAt = 0; int treeSnap = 0; bool sawing = false;   // 3.53: the pickup waits for the ground collision (no more mid-air)
} s_Tb;

static int  TbLevel() { return s_Tb.xp >= 60 ? 3 : (s_Tb.xp >= 20 ? 2 : 1); }
static bool TbOn() { return GmOn(GM_TIMBER); }
static float TbDist2D(const Vec3& a, const Vec3& b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y)); }
static float TbDist(const Vec3& a, const Vec3& b) { return sqrtf((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); }
static bool TbIsPickup(int m) { for(int k : kTbPickups) if(k == m) return true; return false; }
static Vec3 TbTarget() { return s_Tb.idx < TB_MISSION && s_Tb.mission[s_Tb.idx] >= 0 ? s_Tb.pool[s_Tb.mission[s_Tb.idx]] : s_Tb.storage; }

// ---- saved skill / salary (MP.ini, [MPTimber]) ----------------------------------------------------
static int TbCfgGet(const char* k, int def) { return ::cfg->GetInt(k, def, "MPTimber"); }
static void TbCfgPut(const char* k, int v) { ConfigEntry* e = ::cfg->Bind(k, v, "MPTimber"); e->SetInt(v); delete e; }
static void TbLoadStats()
{
    if(s_Tb.loaded) return; s_Tb.loaded = true;
    s_Tb.xp = TbCfgGet("Xp", 0); s_Tb.paycheck = TbCfgGet("PaycheckCents", 0) / 100.0f; s_Tb.earned = TbCfgGet("EarnedCents", 0) / 100.0f;
    s_Tb.sold = TbCfgGet("Sold", 0); s_Tb.trees = TbCfgGet("Trees", 0);
    s_Tb.truckModel = TbCfgGet("Car", TB_TRUCK) == 422 ? 422 : 543;   // 3.58
}
static void TbSaveStats()
{
    TbCfgPut("Xp", s_Tb.xp); TbCfgPut("PaycheckCents", (int)(s_Tb.paycheck * 100 + 0.5f)); TbCfgPut("EarnedCents", (int)(s_Tb.earned * 100 + 0.5f));
    TbCfgPut("Sold", s_Tb.sold); TbCfgPut("Trees", s_Tb.trees); ::cfg->Save();
}

// ---- spots ------------------------------------------------------------------------------------------
static void TbLoadSpots()
{
    if(s_Tb.spotsLoaded) return; s_Tb.spotsLoaded = true;
    s_Tb.nPool = 0;
    char path[512]; snprintf(path, sizeof(path), "%s/timber.txt", s_szDataDir);
    if(FILE* f = fopen(path, "r"))
    {
        char ln[160];
        while(fgets(ln, sizeof(ln), f))
        {
            char w[16] = ""; float x, y, z, h = 0;
            int n = sscanf(ln, "%15s %f %f %f %f", w, &x, &y, &z, &h);
            if(n < 4) continue;
            if(!strcasecmp(w, "storage")) s_Tb.storage = { x, y, z };
            else if(!strcasecmp(w, "start")) { s_Tb.start = { x, y, z }; if(n >= 5) s_Tb.startH = h; }
            else if(!strcasecmp(w, "tree") && s_Tb.nPool < TB_POOL) s_Tb.pool[s_Tb.nPool++] = { x, y, z };
        }
        fclose(f);
    }
    s_Tb.builtIn = false;
    if(s_Tb.nPool >= TB_MISSION) return;
    // built-in (3.51.1): the Flint County pine forest around the Timber Storage barn. The anchors are the
    // samp-rvrp lumber trees (revitali_rp.sql `tree`) near the barn; around each one 5 more spots, 18-42 m away.
    static const Vec3 kAnchor[] = {
        { -540.57f, -206.31f, 77.47f }, { -565.08f, -203.65f, 77.70f }, { -581.23f, -195.23f, 78.20f }, { -649.89f, -662.14f, 40.59f },
        { -529.42f, -623.96f, 12.75f }, { -506.71f, -1000.80f, 24.05f }, { -526.23f, -977.15f, 23.52f }, { -908.49f, -506.00f, 25.01f } };
    const int nA = (int)(sizeof(kAnchor) / sizeof(kAnchor[0]));
    s_Tb.nPool = 0;
    for(int a = 0; a < nA && s_Tb.nPool < TB_POOL; ++a)
    {
        s_Tb.anchorOf[s_Tb.nPool] = (uint8_t)a; s_Tb.pool[s_Tb.nPool++] = kAnchor[a];
        for(int k = 0; k < 5 && s_Tb.nPool < TB_POOL; ++k)
        {
            const float ang = (a * 61 + k * 72) * 0.0174533f, r = 18.0f + (float)((a * 7 + k * 11) % 25);
            s_Tb.anchorOf[s_Tb.nPool] = (uint8_t)a;
            const float sy = kAnchor[a].y > -250.0f ? -fabsf(sinf(ang)) : sinf(ang);   // next to the barn: only to the south, into the trees
            s_Tb.pool[s_Tb.nPool++] = { kAnchor[a].x + cosf(ang) * r, kAnchor[a].y + sy * r, kAnchor[a].z };
        }
    }
    s_Tb.builtIn = true;
}
static Vec3 TbGround(Vec3 p)
{
    // 3.53: the first ground just BELOW the point first (GroundZ starts 40 m up = tree tops / the barn roof)
    if(p.z > -500.0f)
    {
        float g = Game::GroundZBelow(p.x, p.y, p.z + 3.0f);
        if(g > -999.0f && p.z - g < 30.0f) { p.z = g; return p; }
        // 3.53.1: the old fallback probed from 30 m up and took the barn / canopy roof (the yellow "i" and the
        // /timber teleport ended up on the roof). Never go more than 1.5 m above the spot's own height.
        g = Game::GroundZ(p.x, p.y, p.z + 30.0f); if(g > -100.0f && g < p.z + 1.5f && p.z - g < 30.0f) p.z = g; return p;
    }
    float g = Game::GroundZ(p.x, p.y, 200.0f);
    p.z = g > -100.0f ? g : 30.0f;
    return p;
}
static Vec3 TbFloor(Vec3 p)
{   // 3.53.1: storage / job start under the barn and canopy roofs: the floor right under the saved spot
    float g = Game::GroundZBelow(p.x, p.y, p.z + 0.8f);
    p.z = (g > -999.0f && p.z - g < 4.0f) ? g : p.z - 1.0f;
    return p;
}
static void TbPickMission()
{
    TbLoadSpots();
    // random order, no spot twice, and each next tree in a different place: at least 55 m from the last one
    // (and from the one before) when the pool allows it, so the marker always moves to another part of the forest
    bool used[TB_POOL] = {}; int n = 0;
    Vec3 last = s_Tb.start, prev = s_Tb.start;
    for(int i = 0; i < TB_MISSION && i < s_Tb.nPool; ++i)
    {
        int cand[TB_POOL], nc = 0;
        for(float need : { 55.0f, 30.0f, 0.0f })
        {
            nc = 0;
            for(int j = 0; j < s_Tb.nPool; ++j)
                if(!used[j] && TbDist2D(s_Tb.pool[j], last) >= need && (i < 2 || TbDist2D(s_Tb.pool[j], prev) >= need * 0.5f)) cand[nc++] = j;
            if(nc) break;
        }
        if(!nc) break;
        const int pick = cand[rand() % nc];
        used[pick] = true; s_Tb.mission[n++] = pick; prev = last; last = s_Tb.pool[pick];
    }
    for(int i = n; i < TB_MISSION; ++i) s_Tb.mission[i] = n ? s_Tb.mission[i % n] : -1;
    const int lv = TbLevel();
    for(int i = 0; i < TB_MISSION; ++i)
    {
        int t = rand() % 100; s_Tb.mType[i] = (uint8_t)(t < 50 ? 0 : (t < 82 ? 1 : 2));
        if(s_Tb.mType[i] + 1 > lv) s_Tb.mType[i] = (uint8_t)(lv - 1);   // WW-RP: oak needs woodcutting level 2, yew level 3
    }
    s_Tb.idx = 0; s_Tb.done = 0;
}

// ---- world objects / markers (game thread) ------------------------------------------------------------
static void TbDelObj(int& o) { if(o > 0 && Game::WorldObjectExists(o)) Game::DeleteWorldObject(o); o = 0; }
static void TbSetBlip(const Vec3& p)
{
    if(s_Tb.blip) Game::RemoveBlip(s_Tb.blip);
    s_Tb.blip = Game::AddCoordBlip(p, 0xFF1A1AFF, 2); s_Tb.blipAt = p; s_Tb.arrived = false;
}
static void TbClearBlip() { if(s_Tb.blip) Game::RemoveBlip(s_Tb.blip); s_Tb.blip = 0; }
static int TbBedWant() { return (s_Tb.truck && Game::CarExists(s_Tb.truck)) ? std::min(s_Tb.timber, TB_BED_SHOW) : 0; }
static void TbLogsUpdate()
{
    // 3.58: real single logs lying lengthwise in the bed: 3 side by side, then a 2nd layer (counter = all of them)
    const int want = TbBedWant();
    const TbBed& bed = TbBedOf(s_Tb.truck ? Game::GetCarModel(s_Tb.truck) : s_Tb.truckModel);
    for(int i = 0; i < TB_BED_SHOW; ++i)
    {
        if(i >= want) { TbDelObj(s_Tb.logs[i]); continue; }
        if(s_Tb.logs[i] > 0 && Game::WorldObjectExists(s_Tb.logs[i])) continue;
        Vec3 cp; Game::GetCarPos(s_Tb.truck, cp);
        int o = Game::CreateWorldObject(TB_LOG_MODEL, cp);
        if(o <= 0) continue;
        s_Tb.logs[i] = o;
        const int col = i % 3, lay = i / 3;
        const Vec3 off = { (col - 1) * bed.halfW * 0.62f + (lay ? bed.halfW * 0.3f : 0.0f), bed.bedY, bed.floorZ + 0.12f + lay * 0.26f };
        Game::AttachObjectToCar(o, s_Tb.truck, off, { 0.0f, 0.0f, 0.0f });
    }
    s_Tb.nLogs = want;
}
// ---- 3.58: logs on the ground and in your arms ----------------------------------------------------------
static void TbDropCarried()
{
    for(int& o : s_Tb.carry) TbDelObj(o);
    s_Tb.nCarry = 0; s_Tb.act = 0; s_Tb.actLog = -1;
}
static void TbClearGround() { for(int i = 0; i < 3; ++i) { TbDelObj(s_Tb.gLog[i]); s_Tb.gAlive[i] = false; } }
static void TbScatter()
{
    // the trunk breaks into 3 logs where it lies (2.2 / 4.6 / 7.0 m from the base) and they roll off to the sides
    TbClearGround();
    const float r = s_Tb.fallH * 0.0174533f;
    const Vec3 dir = { -sinf(r), cosf(r), 0.0f }, side = { cosf(r), sinf(r), 0.0f };
    static const float kAlong[3] = { 2.2f, 4.6f, 7.0f };
    for(int i = 0; i < 3; ++i)
    {
        Vec3 from = { s_Tb.treeAt.x + dir.x * kAlong[i], s_Tb.treeAt.y + dir.y * kAlong[i], s_Tb.treeAt.z + 0.45f };
        const float lat = ((rand() % 2) ? 1.0f : -1.0f) * (0.6f + (rand() % 70) / 100.0f);
        Vec3 to = { from.x + side.x * lat + dir.x * (rand() % 50 - 25) / 100.0f, from.y + side.y * lat + dir.y * (rand() % 50 - 25) / 100.0f, from.z };
        const float g = Game::GroundZBelow(to.x, to.y, to.z + 2.0f);
        to.z = (g > -999.0f && fabsf(g - s_Tb.treeAt.z) < 6.0f) ? g + 0.14f : s_Tb.treeAt.z + 0.14f;
        s_Tb.gFrom[i] = from; s_Tb.gPos[i] = to; s_Tb.gRot[i] = s_Tb.fallH + 90.0f + (rand() % 60 - 30); s_Tb.gAlive[i] = true;
        s_Tb.gLog[i] = 0;
    }
    s_Tb.scatT = 0.0f;
}
static void TbGroundStream(float dt)
{
    // the logs are made one per tick (models stream in) and fly the short arc of the scatter
    if(s_Tb.scatT < 1.0f) s_Tb.scatT = std::min(1.0f, s_Tb.scatT + dt / 0.75f);
    const float t = s_Tb.scatT;
    for(int i = 0; i < 3; ++i)
    {
        if(!s_Tb.gAlive[i]) { TbDelObj(s_Tb.gLog[i]); continue; }
        if(s_Tb.gLog[i] <= 0 || !Game::WorldObjectExists(s_Tb.gLog[i])) { s_Tb.gLog[i] = Game::CreateWorldObject(TB_LOG_MODEL, s_Tb.gFrom[i]); continue; }
        if(t >= 1.0f && s_Tb.gLog[i] > 0) { Game::PlaceMapObject(s_Tb.gLog[i], s_Tb.gPos[i], 0.0f, 0.0f, s_Tb.gRot[i]); continue; }
        const float e = 1.0f - (1.0f - t) * (1.0f - t);   // ease out
        Vec3 p = { s_Tb.gFrom[i].x + (s_Tb.gPos[i].x - s_Tb.gFrom[i].x) * e, s_Tb.gFrom[i].y + (s_Tb.gPos[i].y - s_Tb.gFrom[i].y) * e,
                   s_Tb.gFrom[i].z + (s_Tb.gPos[i].z - s_Tb.gFrom[i].z) * e + sinf(t * 3.14159f) * 0.55f + (t > 0.8f ? sinf((t - 0.8f) * 15.7f) * 0.06f : 0.0f) };
        Game::PlaceMapObject(s_Tb.gLog[i], p, 0.0f, 0.0f, s_Tb.gRot[i] - (1.0f - t) * 140.0f);
    }
}
static int TbNearestLog(const Vec3& me, float maxD)
{
    int best = -1; float bd = maxD;
    for(int i = 0; i < 3; ++i) if(s_Tb.gAlive[i]) { const float d = TbDist2D(me, s_Tb.gPos[i]); if(d < bd && fabsf(me.z - s_Tb.gPos[i].z) < 2.5f) { bd = d; best = i; } }
    return best;
}
static int TbGroundLeft() { int n = 0; for(bool a : s_Tb.gAlive) n += a ? 1 : 0; return n; }
static bool TbAtTailgate(const Vec3& me)
{
    if(!s_Tb.truck || !Game::CarExists(s_Tb.truck)) return false;
    Vec3 cp; Game::GetCarPos(s_Tb.truck, cp);
    const float h = Game::GetCarHeading(s_Tb.truck) * 0.0174533f;
    const TbBed& bed = TbBedOf(Game::GetCarModel(s_Tb.truck));
    // car space y axis = (-sin h, cos h): the tailgate is bed.rearY (negative) along it
    const Vec3 tg = { cp.x + (-sinf(h)) * bed.rearY, cp.y + cosf(h) * bed.rearY, cp.z };
    return TbDist2D(me, tg) < 1.9f && fabsf(me.z - cp.z) < 2.2f;
}
static void TbIconsStream(uint64_t now)
{
    // the yellow spinning "i" (model 1239, the Mega Vil lobby one) at the storage and at the job start
    const Vec3 at[2] = { s_Tb.storage, s_Tb.start };
    bool made = false;
    for(int i = 0; i < 2; ++i)
    {
        int& o = s_Tb.icon[i];
        if(o > 0 && !Game::WorldObjectExists(o)) o = 0;
        if(BcwCamDist(at[i]) > 220.0f) { TbDelObj(o); continue; }
        if(o <= 0) { if(made || now - s_Tb.iconTry < 500) continue; s_Tb.iconTry = now; made = true; Vec3 p = TbFloor(at[i]); p.z += 0.9f; o = Game::CreateWorldObject(1239, p); continue; }
        Vec3 p = TbFloor(at[i]); p.z += 0.9f + 0.08f * sinf((float)(now % 2000) * 0.00314f);
        Game::MoveWorldObject(o, p, (float)(now % 3000) * 0.12f + i * 40.0f);
    }
}
static void TbAnim(int ped, const char* anim, const char* file) { for(int k = 0; k < 3 && !Game::PlayAnimLoop(ped, anim, file); ++k) {} }
static void TbStopAnim(int ped) { if(ped > 0) { Game::ClearTasksNow(ped); Game::FreezeChar(ped, false); } }

static void TbEnd(bool msg)
{
    const int ped = Game::LocalPed();
    if(s_Tb.phase == TB_CUT || s_Tb.phase == TB_LOAD) TbStopAnim(ped);
    TbClearBlip(); TbDelObj(s_Tb.tree); TbDelObj(s_Tb.stump);
    for(int& o : s_Tb.logs) TbDelObj(o);
    if(s_Tb.nCarry && ped > 0) TbStopAnim(ped);
    TbDropCarried(); TbClearGround();   // 3.58
    if(s_Tb.truck && Game::CarExists(s_Tb.truck)) { if(Game::GetDriverOfCar(s_Tb.truck) <= 0) Game::DeleteCar(s_Tb.truck); else Game::ReleaseCar(s_Tb.truck); }
    s_Tb.truck = 0; s_Tb.timber = 0; s_Tb.on = false; s_Tb.phase = TB_OFF; s_Tb.prog = s_Tb.loadProg = 0;
    if(msg) SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}You quit the Timber job. Your paycheck stays ($%.2f, /mysalary).", s_Tb.paycheck);
}
static void TbNextTarget()
{
    TbDelObj(s_Tb.tree);
    s_Tb.health = 100.0f; s_Tb.prog = 0; s_Tb.loadProg = 0; s_Tb.ang = s_Tb.vel = 0; s_Tb.bounces = 0;
    if(s_Tb.timber >= TB_CAP || s_Tb.idx >= TB_MISSION)
    {
        s_Tb.phase = TB_SELL; TbSetBlip(s_Tb.storage);
        SysMsg(COL_YELLOW, s_Tb.timber >= TB_CAP ? "{FFA500}JOBINFO: {FFFFFF}The pickup is full (%d/%d). Drive to the Timber Storage (red marker) and /unloadtimber." :
                                                   "{FFA500}JOBINFO: {FFFFFF}Mission trees done. Bring the %d timber to the Timber Storage and /unloadtimber.", s_Tb.timber, TB_CAP);
        return;
    }
    s_Tb.phase = TB_GO;
    s_Tb.treeAt = TbGround(s_Tb.pool[s_Tb.mission[s_Tb.idx]]);
    s_Tb.pool[s_Tb.mission[s_Tb.idx]] = s_Tb.treeAt;
    TbSetBlip(s_Tb.treeAt);
    SysMsg(COL_YELLOW, "{FFA500}GPSINFO: {FFFFFF}Tree %d/%d: a %s%s{FFFFFF} is marked on your radar.", s_Tb.idx + 1, TB_MISSION, kTbTypeC[s_Tb.mType[s_Tb.idx]], kTbType[s_Tb.mType[s_Tb.idx]]);
}

// ---- commands -------------------------------------------------------------------------------------------
static void TbHelp()
{
    SysMsg(COL_YELLOW, "{FFA500}Timber job: {FFFFFF}/timber (start, teleports you to the Timber Storage)  /quitjob  /fixjob  /timbergps");
    SysMsg(COL_YELLOW, "{FFFFFF}/cut (or hold CUT with the chainsaw)  /pickuplog (PICK UP, 1-3 logs in your arms)  /putlogs (or CROUCH at the back of the pickup)");
    SysMsg(COL_YELLOW, "{FFFFFF}/unloadtimber (Timber Storage)  /timbercar bobcat | sadler (your job pickup: 422 Bobcat or 543 Sadler)");
    SysMsg(COL_YELLOW, "{FFFFFF}/mysalary  /collectsalary (at the storage)  /timberskill  /checkjobcooldown  /timberhelp");
}
static void TbStart()
{
    TbLoadStats();
    const uint64_t now = MP_NowMs();
    if(s_Tb.tiredUntil > now) { SysMsg(COL_RED, "You cannot cut trees right now - you are on a cooldown (%d s, /checkjobcooldown).", (int)((s_Tb.tiredUntil - now) / 1000)); return; }
    if(s_Tb.on) { SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}Tree %d/%d, timber %d/%d, paycheck $%.2f. /quitjob to stop.", s_Tb.idx + 1, TB_MISSION, s_Tb.timber, TB_CAP, s_Tb.paycheck); return; }
    TbLoadSpots();
    s_Tb.on = true; s_Tb.tpDue = true; s_Tb.chainsawDue = true; s_Tb.truckDue = true; s_Tb.truckAt = MP_NowMs() + 900; s_Tb.truckTries = 0; s_Tb.timber = 0;
    TbPickMission();
    SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}Welcome to the Timber job (woodcutting level %d). Cut %d trees, load the logs in the pickup, sell them at the storage.", TbLevel(), TB_MISSION);
    TbHelp();
}
static bool TbAtStorage(const Vec3& me) { return TbDist2D(me, s_Tb.storage) < 9.0f && fabsf(me.z - s_Tb.storage.z) < 6.0f; }
static void TbUnload()
{
    const int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return;
    const uint64_t now = MP_NowMs();
    if(!TbAtStorage(me)) { SysMsg(COL_RED, "You are not at the Timber Storage (the yellow \"i\" at the barn, /timbergps)."); return; }
    if(now < s_Tb.sellAgainAt) { SysMsg(COL_RED, "You can sell lumber again in %d s.", (int)((s_Tb.sellAgainAt - now) / 1000)); return; }
    if(s_Tb.timber <= 0) { SysMsg(COL_RED, "Your pickup has no timber. Cut a tree and /loadtimber first."); return; }
    if(!s_Tb.truck || !Game::CarExists(s_Tb.truck)) { SysMsg(COL_RED, "Your pickup is gone - the timber went with it. /quitjob, then /timber."); s_Tb.timber = 0; return; }
    Vec3 cp; Game::GetCarPos(s_Tb.truck, cp);
    if(TbDist(cp, me) > 12.0f) { SysMsg(COL_RED, "Bring the pickup with the timber to the storage."); return; }
    float pay = 0; for(int i = 0; i < s_Tb.timber; ++i) pay += kTbPay[std::min(2, TbLevel() - 1)];
    SysMsg(COL_YELLOW, "{FFA500}STORAGE: {FFFFFF}You've unloaded {FFDD33}%d timber{FFFFFF}, {33CC55}$%.2f{FFFFFF} has been issued to your paycheck.", s_Tb.timber, pay);
    SysMsg(COL_YELLOW, "{FFA500}SALARY: {FFFFFF}Your salary statement has been updated, please check command '/mysalary'.");
    s_Tb.paycheck += pay; s_Tb.earned += pay; s_Tb.sold += s_Tb.timber; s_Tb.timber = 0; TbLogsUpdate();
    s_Tb.sellAgainAt = now + 60000; s_Tb.sellMsgDue = true;
    TbSaveStats();
    if(s_Tb.idx >= TB_MISSION)
    {
        s_Tb.paycheck += 150.0f; s_Tb.earned += 150.0f; TbSaveStats();
        s_Tb.tiredUntil = now + (uint64_t)kTbCool[TbLevel() - 1] * 1000;
        SysMsg(COL_GREEN, "Mission complete: %d trees. +$150.00 bonus on your paycheck.", TB_MISSION);
        SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}You feel tired of cutting trees, so you've decided to stop (rest %d s).", kTbCool[TbLevel() - 1]);
        TbEnd(false);
        return;
    }
    if(s_Tb.phase != TB_LOGS) TbNextTarget();   // 3.58: logs still on the ground: finish that tree first
}
static void TbSalary()
{
    TbLoadStats();
    SysMsg(COL_YELLOW, "{FFA500}..:: Salary statement ::..");
    SysMsg(COL_WHITE, "Paycheck (not collected): {33CC55}$%.2f{FFFFFF}   Earned in total: $%.2f", s_Tb.paycheck, s_Tb.earned);
    SysMsg(COL_WHITE, "Timber sold: %d   Trees cut: %d   Woodcutting level %d (%d xp)", s_Tb.sold, s_Tb.trees, TbLevel(), s_Tb.xp);
    SysMsg(COL_WHITE, "Collect it with /collectsalary at the Timber Storage.");
}
static bool TimberCommand(const char* cmd, const char* arg)
{
    if(!TbOn()) return false;
    const int ped = Game::LocalPed();
    if(!strcmp(cmd, "timber") || !strcmp(cmd, "lumberjack") || !strcmp(cmd, "lumber"))
    { if(arg && (!strcasecmp(arg, "stop") || !strcasecmp(arg, "quit"))) { if(s_Tb.on) TbEnd(true); } else if(arg && !strcasecmp(arg, "help")) TbHelp(); else TbStart(); return true; }
    if(!strcmp(cmd, "timberhelp")) { TbHelp(); return true; }
    if(!strcmp(cmd, "timbercar") || !strcmp(cmd, "jobcar"))
    {   // 3.58: Bobcat (422) support
        int m = s_Tb.truckModel;
        if(arg && (!strcasecmp(arg, "bobcat") || !strcmp(arg, "422"))) m = 422;
        else if(arg && (!strcasecmp(arg, "sadler") || !strcmp(arg, "543"))) m = 543;
        else { SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}Your job pickup: %s. Use /timbercar bobcat | sadler (you can also drive your own Bobcat / Sadler / Walton / Yosemite).", s_Tb.truckModel == 422 ? "Bobcat (422)" : "Sadler (543)"); return true; }
        s_Tb.truckModel = m; TbCfgPut("Car", m); ::cfg->Save();
        SysMsg(COL_GREEN, "{FFA500}LUMBERJACK: {FFFFFF}Your next job pickup is a %s.%s", m == 422 ? "Bobcat (422)" : "Sadler (543)", s_Tb.on ? " (/quitjob and /timber to get it now)" : "");
        return true;
    }
    if(!strcmp(cmd, "mysalary") || !strcmp(cmd, "salary")) { TbSalary(); return true; }
    if(!strcmp(cmd, "timberskill") || !strcmp(cmd, "woodskill"))
    { TbLoadStats(); int lv = TbLevel(); SysMsg(COL_YELLOW, "{FFA500}Woodcutting: {FFFFFF}level %d, %d xp%s.", lv, s_Tb.xp, lv == 1 ? " (oak trees at 20 xp)" : (lv == 2 ? " (yew trees at 60 xp)" : " (max)")); return true; }
    if(!strcmp(cmd, "checkjobcooldown") || !strcmp(cmd, "cooldowns") || !strcmp(cmd, "cooldown"))
    {
        const uint64_t now = MP_NowMs(); bool any = false;
        SysMsg(COL_YELLOW, "{FFA500}..:: Job Cooldowns ::..");
        if(s_Tb.tiredUntil > now) { SysMsg(COL_WHITE, "Wood Cutting Cooldown Time Remaining: %.2f minutes", (s_Tb.tiredUntil - now) / 60000.0f); any = true; }
        if(s_Tb.sellAgainAt > now) { SysMsg(COL_WHITE, "Lumber selling: %d s", (int)((s_Tb.sellAgainAt - now) / 1000)); any = true; }
        if(!any) SysMsg(COL_WHITE, "You have no job cooldown timers.");
        return true;
    }
    if(!strcmp(cmd, "collectsalary") || !strcmp(cmd, "paycheck") || !strcmp(cmd, "collectpay"))
    {
        Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return true;
        if(!TbAtStorage(me)) { SysMsg(COL_RED, "Collect your paycheck at the Timber Storage (/timbergps)."); return true; }
        if(s_Tb.paycheck < 0.01f) { SysMsg(COL_RED, "Your paycheck is empty."); return true; }
        int cash = (int)s_Tb.paycheck; Game::AddMoney(cash); s_Tb.paycheck -= cash; TbSaveStats();
        SysMsg(COL_GREEN, "{FFA500}SALARY: {FFFFFF}You collected {33CC55}$%d{FFFFFF} from your paycheck.", cash);
        return true;
    }
    if(!strcmp(cmd, "unloadtimber") || !strcmp(cmd, "selltimber") || !strcmp(cmd, "selllumber")) { if(!s_Tb.on) { SysMsg(COL_RED, "Start the job first: /timber"); return true; } TbUnload(); return true; }
    if(!strcmp(cmd, "timberpos"))
    {
        if(!MP::IsLocalMod() && !MP::IsDefaultAdmin() && !MP::IsLocalAdmin()) { SysMsg(COL_RED, "Staff only."); return true; }
        Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return true;
        const char* w = (arg && *arg) ? arg : "tree";
        if(strcasecmp(w, "tree") && strcasecmp(w, "storage") && strcasecmp(w, "start")) { SysMsg(COL_RED, "Use: /timberpos tree | storage | start"); return true; }
        char path[512]; snprintf(path, sizeof(path), "%s/timber.txt", s_szDataDir);
        if(FILE* f = fopen(path, "a")) { fprintf(f, "%s %.2f %.2f %.2f %.1f\n", w, me.x, me.y, me.z - 1.0f, Game::GetCharHeading(ped)); fclose(f); }
        s_Tb.spotsLoaded = false; TbLoadSpots();
        SysMsg(COL_GREEN, "Saved \"%s\" to timber.txt (%d trees in the file now; built-in spots are used until there are %d).", w, s_Tb.nPool, TB_MISSION);
        return true;
    }
    if(!s_Tb.on) return false;
    if(!strcmp(cmd, "quitjob") || !strcmp(cmd, "stopjob")) { TbEnd(true); return true; }
    if(!strcmp(cmd, "fixjob"))
    {
        TbStopAnim(ped);
        if(s_Tb.phase == TB_CUT) s_Tb.phase = TB_GO;
        if(s_Tb.phase == TB_LOAD) s_Tb.phase = TB_FALL;
        if(s_Tb.phase == TB_LOGS) { s_Tb.act = 0; s_Tb.actLog = -1; s_Tb.upperAt = 0; }   // 3.58
        s_Tb.holding = false; s_Tb.loadProg = 0; s_Tb.chainsawDue = true;
        SysMsg(COL_YELLOW, "Job state fixed (animation stopped, chainsaw back).");
        return true;
    }
    if(!strcmp(cmd, "timbergps") || !strcmp(cmd, "jobgps")) { TbSetBlip(s_Tb.phase == TB_SELL ? s_Tb.storage : (s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT ? s_Tb.treeAt : s_Tb.treeAt)); SysMsg(COL_YELLOW, "{FFA500}GPSINFO: {FFFFFF}Marked on your radar."); return true; }
    if(!strcmp(cmd, "cut") || !strcmp(cmd, "cuttree")) { s_Tb.cutReq = true; s_Tb.lastHold = MP_NowMs(); return true; }
    if(!strcmp(cmd, "loadtimber") || !strcmp(cmd, "load")) { if(s_Tb.nCarry) s_Tb.putReq = true; else s_Tb.pickReq = true; return true; }
    if(!strcmp(cmd, "pickuplog") || !strcmp(cmd, "pickup") || !strcmp(cmd, "takelog")) { s_Tb.pickReq = true; return true; }
    if(!strcmp(cmd, "putlogs") || !strcmp(cmd, "putlog") || !strcmp(cmd, "droplogs")) { s_Tb.putReq = true; return true; }
    return false;
}

// ---- the job (game thread) ------------------------------------------------------------------------------
static void TbSpawnTruck(int ped)
{
    // 3.53: the pickup used to be created in the same frame as the teleport, before the collision of the
    // barn was streamed in, 70 m up (GroundZ from 40 m above the tree tops) = it hung in mid-air. Now it
    // waits until the ground right under ITS OWN spot is loaded and is put on that surface.
    const uint64_t now = MP_NowMs();
    if(now < s_Tb.truckAt) return;
    const float r = s_Tb.startH * 0.0174533f;
    Vec3 cp = { s_Tb.start.x + cosf(r) * 4.0f, s_Tb.start.y + sinf(r) * 4.0f, s_Tb.start.z };
    if(s_Tb.truckTries == 0) Game::PreloadArea(cp, true);
    float g = Game::GroundZBelow(cp.x, cp.y, s_Tb.start.z + 1.0f);   // 3.53.1: from just above the floor (+3 / +8 m hit the canopy roof)
    if(g < -999.0f || s_Tb.start.z - g > 20.0f) g = Game::GroundZBelow(cp.x, cp.y, s_Tb.start.z + 1.6f);
    if((g < -999.0f || s_Tb.start.z - g > 20.0f) && ++s_Tb.truckTries < 40) { s_Tb.truckAt = now + 100; return; }   // not streamed yet: try again (max 4 s)
    if(g < -999.0f || s_Tb.start.z - g > 20.0f) g = TbFloor(s_Tb.start).z;
    // 3.58: CREATE_CAR already lifts the car by its own wheel height, so this is 12 cm between the tyres and the
    // floor: it settles on its wheels without a drop (the +1 m drop broke cars with wheel-detach mods)
    cp.z = g + 0.12f;
    int car = Game::CreateCar(s_Tb.truckModel == 422 ? 422 : TB_TRUCK, cp, true);
    if(car <= 0) { s_Tb.truckAt = now + 250; return; }
    Game::SetCarHeading(car, s_Tb.startH); Game::SetCarColours(car, 1, 1);
    Game::SetMoveSpeed(car, true, { 0, 0, 0 });
    s_Tb.truck = car; s_Tb.truckDue = false; s_Tb.truckTries = 0; (void)ped;
}
static void TimberProcess(int ped, uint64_t now, float dt)
{
    if(!TbOn()) { if(s_Tb.on) TbEnd(false); for(int& o : s_Tb.icon) TbDelObj(o); return; }
    TbLoadSpots(); TbLoadStats();
    TbIconsStream(now);
    if(s_Tb.stump && now > s_Tb.stumpUntil) TbDelObj(s_Tb.stump);
    if(s_Tb.sellMsgDue && now >= s_Tb.sellAgainAt) { s_Tb.sellMsgDue = false; SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}You can now sell lumbers again!"); }
    if(!s_Tb.on || ped <= 0) { s_Tb.cutReq = s_Tb.loadReq = false; return; }
    if(Game::IsCharDead(ped)) { TbEnd(false); SysMsg(COL_RED, "{FFA500}LUMBERJACK: {FFFFFF}You died - the Timber job stopped."); return; }
    if(s_Tb.tpDue) { s_Tb.tpDue = false; Game::Teleport(ped, TbFloor(s_Tb.start), s_Tb.startH); TbNextTarget(); return; }   // 3.53.1: feet on the floor, not on the roof
    if(s_Tb.chainsawDue && !Game::IsCharInAnyCar(ped)) { if(Game::GiveAndSelectWeapon(ped, 9)) s_Tb.chainsawDue = false; }
    if(s_Tb.truckDue) TbSpawnTruck(ped);
    if(s_Tb.truck && !Game::CarExists(s_Tb.truck)) { s_Tb.truck = 0; for(int& o : s_Tb.logs) o = 0; if(s_Tb.timber) SysMsg(COL_RED, "Your pickup is gone (the %d timber too). /quitjob and /timber for a new one.", s_Tb.timber); s_Tb.timber = 0; }
    if(s_Tb.nLogs != TbBedWant()) TbLogsUpdate();
    // 3.58: your own Bobcat / Sadler / Walton... becomes the job pickup when the job one is gone or still empty
    if(Game::IsCharInAnyCar(ped))
    {
        const int c = Game::GetCarCharIsIn(ped);
        if(c > 0 && c != s_Tb.truck && TbIsPickup(Game::GetCarModel(c)) && Game::GetDriverOfCar(c) == ped && s_Tb.timber == 0 && !s_Tb.truckDue)
        {
            if(s_Tb.truck && Game::CarExists(s_Tb.truck) && Game::GetDriverOfCar(s_Tb.truck) <= 0) Game::DeleteCar(s_Tb.truck);
            for(int& o : s_Tb.logs) TbDelObj(o);
            s_Tb.truck = c; s_Tb.nLogs = 0;
            SysMsg(COL_YELLOW, "{FFA500}LUMBERJACK: {FFFFFF}This %s is your timber pickup now.", Game::GetCarModel(c) == 422 ? "Bobcat" : "pickup");
        }
    }
    for(int i = 0; i < s_Tb.nLogs && i < TB_BED_SHOW; ++i) if(s_Tb.logs[i] > 0 && !Game::WorldObjectExists(s_Tb.logs[i])) { s_Tb.logs[i] = 0; s_Tb.nLogs = -1; }

    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    if(s_Tb.blip && !s_Tb.arrived && TbDist2D(me, s_Tb.blipAt) < 10.0f) { s_Tb.arrived = true; SysMsg(COL_YELLOW, "{FFA500}GPSINFO: {FFFFFF}You have arrived!"); }

    // the standing tree: our own object at the marked spot (no collision, so it can fall freely)
    if((s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT) && s_Tb.tree <= 0 && TbDist2D(me, s_Tb.treeAt) < 180.0f)
    {
        s_Tb.treeAt = TbGround(s_Tb.treeAt);
        // a spot of the built-in pool in the river / on the road below: use its anchor tree instead (always on land)
        if(s_Tb.builtIn && s_Tb.treeAt.z < 3.0f && s_Tb.mission[s_Tb.idx] >= 0)
        { TbLoadSpots(); const int ai = s_Tb.anchorOf[s_Tb.mission[s_Tb.idx]]; for(int j = 0; j < s_Tb.nPool; ++j) if(s_Tb.anchorOf[j] == ai) { s_Tb.treeAt = s_Tb.pool[j]; break; } TbSetBlip(s_Tb.treeAt); }
        s_Tb.tree = Game::CreateWorldObject(669, s_Tb.treeAt);
        if(s_Tb.tree > 0) { Game::PlaceMapObject(s_Tb.tree, s_Tb.treeAt, 0, 0, (float)(s_Tb.idx * 47 % 360)); Game::SetObjectCollision(s_Tb.tree, true); s_Tb.treeSnap = 0; }   // 3.53: a real hitbox while it stands
    }
    // 3.53: made from far away the ground under it was not streamed in yet (tree in the air / sunk): snap it again
    // once we are close, a few times, and keep its trunk solid while it stands
    if((s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT) && s_Tb.tree > 0 && s_Tb.treeSnap < 4 && TbDist2D(me, s_Tb.treeAt) < 60.0f && now - s_Tb.treeSnapAt > 700)
    {
        s_Tb.treeSnapAt = now; ++s_Tb.treeSnap;
        const float g = Game::GroundZBelow(s_Tb.treeAt.x, s_Tb.treeAt.y, me.z + 25.0f);
        if(g > -999.0f && fabsf(g - s_Tb.treeAt.z) > 0.25f && fabsf(g - me.z) < 25.0f) { s_Tb.treeAt.z = g; Game::PlaceMapObject(s_Tb.tree, s_Tb.treeAt, 0, 0, (float)(s_Tb.idx * 47 % 360)); TbSetBlip(s_Tb.treeAt); }
        Game::SetObjectCollision(s_Tb.tree, true);
    }

    const int type = s_Tb.mType[std::min(s_Tb.idx, TB_MISSION - 1)];
    const float dTree = TbDist2D(me, s_Tb.treeAt);
    if(s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT)
    {
        // 3.53: the game's own attack button with the chainsaw cuts too (before only the CUT button did, so swinging
        // the saw inside the circle did nothing), and the reach is the whole marker circle around the trunk
        const bool inRange = dTree < 3.8f && fabsf(me.z - s_Tb.treeAt.z) < 4.0f;
        const bool saw = inRange && Game::GetCharWeapon(ped) == 9 && (Game::IsCharShooting(ped) || Game::IsCharSawing(ped));
        if(saw) { s_Tb.lastHold = now; if(!s_Tb.sawing) { s_Tb.sawing = true; } }
        else if(s_Tb.sawing && now - s_Tb.lastHold > 600) s_Tb.sawing = false;
        const bool want = (s_Tb.holding || s_Tb.cutReq || saw || now - s_Tb.lastHold < 350) && inRange && !Game::IsCharInAnyCar(ped);
        s_Tb.cutReq = false;
        if(want && Game::GetCharWeapon(ped) != 9) { s_Tb.chainsawDue = true; if(now - s_Tb.lastHold < 400) SysMsg(COL_RED, "You need the chainsaw in your hands (it comes back by itself)."); s_Tb.lastHold = 0; return; }
        if(want && s_Tb.phase == TB_GO)
        {
            s_Tb.phase = TB_CUT;
            const float h = atan2f(-(s_Tb.treeAt.x - me.x), s_Tb.treeAt.y - me.y) * 57.29578f;
            Game::SetCharHeading(ped, h); Game::FreezeChar(ped, true);
            TbAnim(ped, "WEAPON_csaw", "CHAINSAW");
            SysMsg(COL_SYSTEM, "{C2A2DA}* %s seizes his chainsaw from behind body, then cut the tree.", settings.name);
        }
        if(s_Tb.phase == TB_CUT)
        {
            if(want) s_Tb.prog += kTbRate[type] * (TbLevel() > type + 1 ? 1.3f : 1.0f) * dt;
            else { s_Tb.prog -= 15.0f * dt; if(s_Tb.prog <= 0) { s_Tb.prog = 0; s_Tb.phase = TB_GO; TbStopAnim(ped); } }   // WW-RP: the bar drops when you stop
            if(s_Tb.prog >= 100.0f)
            {
                s_Tb.prog = 0; s_Tb.health -= kTbRound[type];
                if(s_Tb.health <= 0.5f)
                {   // TIMBER: it falls away from you
                    s_Tb.health = 0; TbStopAnim(ped); Game::SetCurrentWeapon(ped, 9);
                    s_Tb.fallH = atan2f(-(s_Tb.treeAt.x - me.x), s_Tb.treeAt.y - me.y) * 57.29578f;
                    s_Tb.ang = 2.0f; s_Tb.vel = 0.0f; s_Tb.bounces = 0; s_Tb.phase = TB_FALL; Game::SetObjectCollision(s_Tb.tree, false);   // 3.53: it falls freely (no push)
                    SysMsg(COL_YELLOW, "You have chopped down a %s%s{FFFFFF}. TIMBER!", kTbTypeC[type], kTbType[type]);
                    GameText("~y~TIMBER!", 1500, 3);
                }
            }
        }
        return;
    }
    if(s_Tb.phase == TB_FALL || s_Tb.phase == TB_LOAD)
    {
        // hinge physics: a rod falling around its base, alpha = 3 g sin(theta) / (2 L), then a small bounce
        if(s_Tb.ang < 89.5f || s_Tb.vel != 0.0f)
        {
            const float L = 9.0f, th = s_Tb.ang * 0.0174533f;
            s_Tb.vel += (3.0f * 9.81f * sinf(th) / (2.0f * L)) * 57.29578f * dt;
            s_Tb.ang += s_Tb.vel * dt;
            if(s_Tb.ang >= 90.0f)
            {
                s_Tb.ang = 90.0f;
                if(s_Tb.bounces < 2 && s_Tb.vel > 25.0f) { s_Tb.vel = -s_Tb.vel * 0.22f; ++s_Tb.bounces; }
                else s_Tb.vel = 0.0f;
            }
            Vec3 p = s_Tb.treeAt; p.z += 0.35f * (s_Tb.ang / 90.0f);
            if(s_Tb.tree > 0) Game::PlaceMapObject(s_Tb.tree, p, -s_Tb.ang, 0.0f, s_Tb.fallH);
        }
        // 3.58: once it lies still, the trunk breaks into 3 logs that scatter on the ground
        if(s_Tb.phase == TB_FALL && s_Tb.vel == 0.0f && s_Tb.ang >= 89.0f)
        {
            if(!s_Tb.settleAt) s_Tb.settleAt = now + 650;
            if(now >= s_Tb.settleAt)
            {
                s_Tb.settleAt = 0;
                TbDelObj(s_Tb.tree);
                TbDelObj(s_Tb.stump); s_Tb.stump = Game::CreateWorldObject((s_Tb.idx & 1) ? 832 : 831, s_Tb.treeAt); s_Tb.stumpUntil = now + 120000;   // WW-RP: the stump stays
                TbScatter(); s_Tb.phase = TB_LOGS; s_Tb.chainsawDue = false;
                Game::SetCurrentWeapon(ped, 0);
                SysMsg(COL_YELLOW, "{FFA500}JOBINFO: {FFFFFF}The tree broke into 3 logs. PICK UP (1-%d in your arms), stand at the back of the pickup and CROUCH to load them.", TB_CARRY_MAX);
            }
        }
        s_Tb.loadReq = false;
        return;
    }
    if(s_Tb.phase == TB_LOGS)
    {
        TbGroundStream(dt);
        const bool inCar = Game::IsCharInAnyCar(ped);
        // the pickup / put-down animation is running
        if(s_Tb.act)
        {
            if(now < s_Tb.actUntil) { s_Tb.pickReq = s_Tb.putReq = false; return; }
            if(s_Tb.act == 1 && s_Tb.actLog >= 0)
            {   // lifted: the ground log is gone, one more in the arms (stacked on the forearms)
                const int i = s_Tb.actLog; s_Tb.gAlive[i] = false; TbDelObj(s_Tb.gLog[i]);
                Vec3 at = me; at.z += 1.0f;
                int o = Game::CreateWorldObject(TB_LOG_MODEL, at);
                if(o > 0) { Game::AttachObjectToCharRot(o, ped, { 0.0f, 0.42f, 0.10f + s_Tb.nCarry * 0.20f }, { 0.0f, 0.0f, 90.0f }); s_Tb.carry[s_Tb.nCarry] = o; }
                ++s_Tb.nCarry; Game::FreezeChar(ped, false);
                SysMsg(COL_SYSTEM, "{C2A2DA}* %s lifts a log with both hands (%d/%d in the arms).", settings.name, s_Tb.nCarry, TB_CARRY_MAX);
                s_Tb.upperAt = 0;
            }
            else if(s_Tb.act == 2)
            {   // laid in the bed
                const int n = s_Tb.nCarry; TbDropCarried(); TbStopAnim(ped);
                s_Tb.timber = std::min(TB_CAP, s_Tb.timber + n);
                const int gain = n >= 2 ? 2 : 1; const int lv0 = TbLevel(); s_Tb.xp += gain;
                SysMsg(COL_SYSTEM, "{C2A2DA}AME: {FFFFFF}puts %d log%s in the back of the pickup truck", n, n == 1 ? "" : "s");
                SysMsg(COL_YELLOW, "{FFA500}JOBINFO: {FFFFFF}You've loaded the timber into the car! (logs: %d/%d)", s_Tb.timber, TB_CAP);
                if(TbLevel() > lv0) SysMsg(COL_GREEN, "Woodcutting level %d: %s.", TbLevel(), TbLevel() == 2 ? "you can cut oak trees now" : "you can cut yew trees now");
                TbLogsUpdate();
                if(TbGroundLeft() == 0)
                {   // this tree is done
                    ++s_Tb.trees; ++s_Tb.idx; TbSaveStats(); TbClearGround();
                    s_Tb.chainsawDue = true; s_Tb.phase = TB_GO;
                    TbNextTarget();
                    return;
                }
                TbSaveStats();
            }
            s_Tb.act = 0; s_Tb.actLog = -1;
        }
        // keep the arms around the logs while you walk (upper body only)
        if(s_Tb.nCarry && !inCar && now >= s_Tb.upperAt) { if(Game::PlayAnimUpper(ped, "crry_prtial", "CARRY", true)) s_Tb.upperAt = now + 2500; else s_Tb.upperAt = now + 200; }
        if(s_Tb.nCarry && inCar) { SysMsg(COL_RED, "You dropped the logs - you cannot drive with logs in your arms."); for(int k = 0; k < s_Tb.nCarry; ++k) { for(int i = 0; i < 3; ++i) if(!s_Tb.gAlive[i]) { s_Tb.gAlive[i] = true; s_Tb.gPos[i] = s_Tb.gFrom[i]; s_Tb.gPos[i].z -= 0.3f; s_Tb.gFrom[i] = s_Tb.gPos[i]; break; } } TbDropCarried(); }
        // PICK UP
        if(s_Tb.pickReq && !inCar)
        {
            const int li = TbNearestLog(me, 2.4f);
            if(li < 0) SysMsg(COL_RED, "Stand next to a log to pick it up.");
            else if(s_Tb.nCarry >= TB_CARRY_MAX) SysMsg(COL_RED, "Your arms are full (%d logs). Bring them to the back of the pickup and crouch.", TB_CARRY_MAX);
            else if(Game::PlayAnimOnce(ped, "liftup", "CARRY", 4.0f, true, -1))
            {
                const float h = atan2f(-(s_Tb.gPos[li].x - me.x), s_Tb.gPos[li].y - me.y) * 57.29578f;
                Game::SetCharHeading(ped, h);
                s_Tb.act = 1; s_Tb.actLog = li; s_Tb.actUntil = now + 1150;
            }
            else return;   // CARRY.ifp is still loading: try again next tick (keep the request)
        }
        s_Tb.pickReq = false;
        // PUT IN: at the tailgate, crouch (the game's crouch button) or the PUT IN button
        const bool tail = s_Tb.nCarry > 0 && !inCar && TbAtTailgate(me);
        if(tail && (s_Tb.putReq || Game::IsCharDucking(ped)))
        {
            if(Game::PlayAnimOnce(ped, "putdwn", "CARRY", 4.0f, true, -1)) { s_Tb.act = 2; s_Tb.actUntil = now + 1000; }
            else return;
        }
        else if(s_Tb.putReq && s_Tb.nCarry && !tail) SysMsg(COL_RED, "Stand at the back of your pickup (the tailgate) and crouch to put the logs in.");
        s_Tb.putReq = false;
        if(s_Tb.timber >= TB_CAP && s_Tb.nCarry == 0 && TbGroundLeft() > 0)
        { SysMsg(COL_RED, "The pickup is full (%d/%d). Sell at the Timber Storage, the rest of the logs stay here.", s_Tb.timber, TB_CAP); TbClearGround(); ++s_Tb.idx; ++s_Tb.trees; TbSaveStats(); TbNextTarget(); }
        return;
    }
}

// ---- drawing (render thread) ---------------------------------------------------------------------------
static void TbBar(ImDrawList* dl, const char* label, float pct)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float w = UI::S(400), h = UI::S(34), x = ds.x * 0.5f - w * 0.5f, y = ds.y * 0.70f;
    const float fs = UI::S(19);
    UI::OutlinedText(dl, fs, ImVec2(x, y - fs - UI::S(4)), IM_COL32_WHITE, label);
    char p[16]; snprintf(p, sizeof(p), "%d%%", (int)(pct + 0.5f));
    float pw = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, p).x;
    UI::OutlinedText(dl, fs, ImVec2(x + w - pw, y - fs - UI::S(4)), IM_COL32_WHITE, p);
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 170));
    const float k = std::max(0.0f, std::min(1.0f, pct / 100.0f));
    dl->AddRectFilled(ImVec2(x + UI::S(3), y + UI::S(3)), ImVec2(x + UI::S(3) + (w - UI::S(6)) * k, y + h - UI::S(3)), IM_COL32(255, 106, 0, 255));
}
static void TimberRender(ImDrawList* bg, ImDrawList* fg, float& y, float now)
{
    if(!TbOn()) return;
    // the storage + the job start: floating text over the "i" (screenshot 1)
    { Vec3 s = s_Tb.storage; s.z += 1.9f; BcwLabel3D(bg, s, "Timber Storage", 0x38BDF8FF, 21, 14); s.z -= 0.45f; BcwLabel3D(bg, s, "use '/unloadtimber' to sell collected timber", 0xFFFFFFFF, 17, 14); }
    { Vec3 s = s_Tb.start; s.z += 1.9f; BcwLabel3D(bg, s, "Lumberjack", 0xFFA500FF, 21, 14); s.z -= 0.45f; BcwLabel3D(bg, s, s_Tb.on ? "/timberhelp - the job commands" : "type '/timber' to start the job", 0xFFFFFFFF, 17, 14); }
    if(!s_Tb.on) { SaRouteClear(); return; }
    // 3.58: GPS tracker line on the game radar (roads to the tree / the storage)
    if(s_Tb.blip && !Game::IsPaused())
    {
        SaRouteTrack(myDrawPos.x, myDrawPos.y, myDrawPos.z, s_Tb.blipAt.x, s_Tb.blipAt.y, s_Tb.blipAt.z, now, IM_COL32(255, 60, 60, 255));
        SaRouteDrawWith(fg, myDrawPos.x, myDrawPos.y, UI::S(3.0f), [](float x, float yy, ImVec2& o) { float sx, sy; if(!Game::RadarWorldToScreen(x, yy, sx, sy, false)) return false; o = ImVec2(sx, sy); return true; });
    }
    else SaRouteClear();
    char t[64]; snprintf(t, sizeof(t), "TIMBER %d/%d  TREE %d/%d", s_Tb.timber, TB_CAP, std::min(s_Tb.idx + 1, TB_MISSION), TB_MISSION);
    GmHudLine(fg, y, IM_COL32(255, 165, 0, 255), t); y += UI::S(34);
    const int type = s_Tb.mType[std::min(s_Tb.idx, TB_MISSION - 1)];
    if(s_Tb.phase == TB_SELL) { MkBeam(bg, s_Tb.storage, 1.6f, 2.0f, 0xFF1A1AFF, 1.0f); return; }
    if(s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT)
    {
        MkBeam(bg, s_Tb.treeAt, 1.4f, 1.6f, 0xFF1A1AFF, 1.0f);   // the red marker at the tree to cut
        char l[48]; Vec3 p = s_Tb.treeAt; p.z += 1.6f;
        BcwLabel3D(bg, p, kTbType[type], kTbTypeRgba[type], 19, 10); p.z -= 0.4f;
        snprintf(l, sizeof(l), "Health: %d", (int)(s_Tb.health + 0.5f)); BcwLabel3D(bg, p, l, 0xFFFFFFFF, 17, 10);
    }
    const float dTree = TbDist2D(myDrawPos, s_Tb.treeAt);
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    bool showCut = (s_Tb.phase == TB_GO || s_Tb.phase == TB_CUT) && dTree < 3.8f;
    // 3.58: the logs on the ground (floating label) and the PICK UP / PUT IN button
    bool showLoad = false; const char* loadLab = "PICK UP";
    if(s_Tb.phase == TB_LOGS && !s_Tb.act)
    {
        for(int i = 0; i < 3; ++i) if(s_Tb.gAlive[i] && TbDist2D(myDrawPos, s_Tb.gPos[i]) < 25.0f) { Vec3 p = s_Tb.gPos[i]; p.z += 0.55f; BcwLabel3D(bg, p, "Log", 0xBA9F7BFF, 17, 12); }
        const int li = TbNearestLog(myDrawPos, 2.4f);
        if(li >= 0 && s_Tb.nCarry < TB_CARRY_MAX) { showLoad = true; loadLab = "PICK UP"; }
        else if(s_Tb.nCarry && TbAtTailgate(myDrawPos)) { showLoad = true; loadLab = "PUT IN"; }
        if(s_Tb.nCarry)
        {
            char c[96]; snprintf(c, sizeof(c), "Carrying %d/%d log%s - back of the pickup + CROUCH to load", s_Tb.nCarry, TB_CARRY_MAX, s_Tb.nCarry == 1 ? "" : "s");
            GmHudLine(fg, y, IM_COL32(186, 159, 123, 255), c); y += UI::S(34);
        }
    }
    if(s_Tb.phase == TB_LOGS && s_Tb.act) TbBar(fg, s_Tb.act == 1 ? "Picking up the log" : "Loading the logs", 100.0f * (1.0f - std::max(0.0f, (float)((int64_t)s_Tb.actUntil - (int64_t)MP_NowMs())) / (s_Tb.act == 1 ? 1150.0f : 1000.0f)));
    if(s_Tb.phase == TB_CUT) TbBar(fg, "Cutting tree", s_Tb.prog);
    if(showCut || showLoad)
    {
        // the CUT / LOAD button (hold CUT; tap LOAD) over my head, like the screenshot
        Vec3 hp = myDrawPos; hp.z += 1.2f; ImVec2 sp;
        if(!BcwW2S(hp, sp)) sp = ImVec2(ds.x * 0.5f, ds.y * 0.55f);
        const char* lab = showCut ? "CUT" : loadLab;
        const float bw = UI::S(110), bh = UI::S(56);
        ImGui::SetNextWindowPos(ImVec2(sp.x - bw * 0.5f, sp.y - bh * 0.5f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##tbbtn", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.35f)); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.1f, 0, 0.5f)); ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 0.42f, 0, 0.6f));
        ImGui::Button(lab, ImVec2(bw, bh));
        if(showCut) { s_Tb.holding = ImGui::IsItemActive(); if(s_Tb.holding) s_Tb.lastHold = MP_NowMs(); }
        else { s_Tb.holding = false; if(ImGui::IsItemClicked()) { if(!strcmp(loadLab, "PUT IN")) s_Tb.putReq = true; else s_Tb.pickReq = true; } }
        ImGui::PopStyleColor(3);
        ImGui::End();
    }
    else s_Tb.holding = false;
    (void)now;
}
static void TimberReset() { if(s_Tb.on) TbEnd(false); for(int& o : s_Tb.icon) TbDelObj(o); s_Tb.spotsLoaded = false; }

// ---- the "Broker MQQ" title on the default server -------------------------------------------------------
// Laid out like Screenshots/Screenshot_20261101-120203.png: an italic monogram icon ("BM") on top and the
// name in small letters under it. Mega Vil style lettering (Pricedown, thick black outline, drop shadow),
// in red and brown, and no ROLEPLAY ribbon: the default server is only half roleplay.
static void DefShear(ImDrawList* dl, int from, float baseY, float k)
{   // slant everything drawn since 'from' (italic, like the CP icon)
    for(int i = from; i < dl->VtxBuffer.Size; ++i) dl->VtxBuffer[i].pos.x += (baseY - dl->VtxBuffer[i].pos.y) * k;
}
static void DefaultDrawLogo(ImDrawList* dl, float cx, float y, float scale)
{
    ImFont* pd = UI::PricedownFont(); ImFont* f = pd ? pd : UI::Font();
    const float k = pd ? 1.0f : 0.8f;
    const ImU32 red = IM_COL32(220, 38, 38, 255), brown = IM_COL32(176, 112, 64, 255), dark = IM_COL32(92, 48, 20, 255);
    // the icon: a big red B and a brown M, slanted, with a red swoosh over them and a brown slash through
    const float fi = UI::S(54) * scale * k, th = UI::S(2.8f) * scale;
    const float wB = f->CalcTextSizeA(fi, FLT_MAX, 0, "B").x, wM = f->CalcTextSizeA(fi, FLT_MAX, 0, "M").x;
    const float ov = UI::S(1) * scale, iw = wB + wM - ov, ix = cx - iw * 0.5f, iy = y;
    const float base = iy + fi * 0.9f;
    const int v0 = dl->VtxBuffer.Size;
    RpGtaText(dl, f, fi, ImVec2(ix + wB - ov, iy + UI::S(3) * scale), brown, "M", th);
    RpGtaText(dl, f, fi, ImVec2(ix, iy), red, "B", th);   // the B over the M
    // the swoosh (red, outlined) curving over the icon, and a thin dark-brown slash under it
    ImVec2 c0(ix - UI::S(2) * scale, iy + fi * 0.32f), c1(ix + iw * 0.35f, iy - fi * 0.06f), c2(ix + iw * 0.85f, iy + fi * 0.02f), c3(ix + iw + UI::S(6) * scale, iy + fi * 0.18f);
    dl->AddBezierCubic(c0, c1, c2, c3, IM_COL32(0, 0, 0, 255), UI::S(7) * scale);
    dl->AddBezierCubic(c0, c1, c2, c3, red, UI::S(4) * scale);
    dl->AddLine(ImVec2(ix + iw * 0.15f, base + UI::S(3) * scale), ImVec2(ix + iw * 0.95f, base + UI::S(1) * scale), IM_COL32(0, 0, 0, 230), UI::S(4.5f) * scale);
    dl->AddLine(ImVec2(ix + iw * 0.15f, base + UI::S(3) * scale), ImVec2(ix + iw * 0.95f, base + UI::S(1) * scale), dark, UI::S(2.5f) * scale);
    DefShear(dl, v0, base, 0.22f);
    // the title under the icon: "Broker" red, "MQQ" brown
    const char* t1 = "Broker"; const char* t2 = "MQQ";
    const float ft = UI::S(21) * scale * k, tt = UI::S(1.6f) * scale;
    const float w1 = f->CalcTextSizeA(ft, FLT_MAX, 0, t1).x, w2 = f->CalcTextSizeA(ft, FLT_MAX, 0, t2).x, gap = UI::S(6) * scale;
    const float tx = cx - (w1 + gap + w2) * 0.5f, ty = base + UI::S(6) * scale;
    RpGtaText(dl, f, ft, ImVec2(tx, ty), red, t1, tt);
    RpGtaText(dl, f, ft, ImVec2(tx + w1 + gap, ty), brown, t2, tt);
}
static void DefaultLogoRender() {}   // 3.53.1: drawn by the HUD (MP::DrawDefaultTitle), also on the join screen
void MP::DrawDefaultTitle(ImDrawList* dl)
{
    // 3.53.1: from the moment you connect to the default server (connecting / black join screen, spawn
    // picker, under the RULES window and other dialogs), not only after every window was closed
    if(!MP::IsOnDefault() || Game::IsPaused()) return;
    // 3.55: bigger (x1.35) and a bit lower (4.5 % of the screen height, was 6 px), like the screenshot
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    DefaultDrawLogo(dl, ds.x * 0.5f, ds.y * 0.045f, 1.35f);
}
