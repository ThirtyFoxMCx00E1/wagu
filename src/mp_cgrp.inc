// 4.09: CGRP systems (CG-RP / UG-EDIT) on the default server and Mega Vil Futuristic - the phone side.
// The server (net/server_cgrp.inc) keeps every dynamic business, garage, motel, entrance, building, HQ,
// gate, ATM, map box, map icon, information board, OOC shop, gas station, drive-thru, money bag, gift box,
// point, house sale sign, pickup, speed camera, group vehicle spot and mapped object and sends them here
// (PKT_CG_ENT). This file shows them (objects / pickups near you, 3D labels, radar icons, an ENTER / EXIT
// button), forwards the CGRP commands (PKT_CG_CMD), sends a heartbeat every 2 s and does what the server
// asks for (PKT_CG_ACT: teleports, health, weapons, cars, prison, freeze, weather, the /editor bar ...).

struct CgEntC { CgRow r; int obj = 0, objIn = 0, blip = 0; uint64_t tryAt = 0; float t = 0; };   // t: gate 0 closed .. 1 open
struct CgLabel { Vec3 p; std::string text; float d; };
static struct CgClient
{
    std::map<uint16_t, CgEntC> ents;
    PktCgMe me; bool haveMe = false;
    uint64_t beatAt = 0, lblAt = 0, autoAt = 0;
    int nObj = 0;
    bool masked[MP_MAX_PLAYERS] = {};
    // what the server asked for
    uint64_t freezeUntil = 0; bool frozen = false;
    bool jail = false; Vec3 cell = { 0, 0, 0 }; float cellH = 0; int cellArea = 0; uint64_t jailUntil = 0, jailChk = 0;
    int gpsBlip = 0; Vec3 gps = { 0, 0, 0 };
    int specId = -1;
    int cdLeft = 0; uint64_t cdAt = 0;
    std::vector<int> cars;                     // cars the server spawned for me
    struct Pend { uint8_t act; int a, b; int32_t val; Vec3 p; float h; std::string text; uint64_t until; };
    std::vector<Pend> pend;                    // things that wait for a model / animation file
    std::map<uint16_t, uint64_t> usedAt;       // auto /takebag, /usepickup
    // the ENTER / EXIT button + the labels (made on the game thread, drawn on the render thread)
    std::mutex mx;
    std::vector<CgLabel> labels;
    int btn = 0; bool btnHit = false;          // 1 enter, 2 exit
    // /editor
    struct { bool open = false; uint16_t id = 0; int model = 0; float p[6] = {}; float step = 0.5f; int obj = 0; bool dirty = false, save = false, del = false, close = false, here = false; uint64_t tryAt = 0; } ed;
} s_Cg;

static bool CgActive() { return client.GetState() == NET_CONNECTED && Game::IsReady() && (MP::IsOnDefault() || RpOnServer()); }
static bool CgMasked(int id) { return id >= 0 && id < MP_MAX_PLAYERS && s_Cg.masked[id] && CgActive(); }
static int CgMyLevel() { return s_Cg.haveMe ? s_Cg.me.level : 0; }

static void CgFill(PktCgCmd& c, uint8_t op)
{
    memset(&c, 0, sizeof(c)); c.type = PKT_CG_CMD; c.op = op;
    const int ped = Game::LocalPed();
    c.cash = Game::GetMoney();
    if(ped > 0)
    {
        Vec3 p; if(Game::GetCharPos(ped, p)) { c.pos[0] = p.x; c.pos[1] = p.y; c.pos[2] = p.z; }
        c.heading = Game::GetCharHeading(ped);
        int ar = Game::GetCharArea(ped); c.area = (uint8_t)(ar < 0 ? 0 : ar > 255 ? 255 : ar);
        if(Game::IsCharInAnyCar(ped))
        {
            const int car = Game::GetCarCharIsIn(ped);
            c.inCar = 1; c.carModel = (uint16_t)Game::GetCarModel(car);
            Vec3 v; if(Game::GetMoveSpeed(car, true, v)) c.speed = (uint16_t)std::min(999.0f, sqrtf(v.x * v.x + v.y * v.y + v.z * v.z) * 3.6f);
        }
    }
}
static void CgSendText(const char* cmd, const char* arg)
{
    PktCgCmd c; CgFill(c, CGC_TEXT);
    snprintf(c.text, sizeof(c.text), "/%s%s%s", cmd, (arg && *arg) ? " " : "", arg ? arg : "");
    client.Send(&c, sizeof(c));
}

static void CgDelObj(int& h) { if(h > 0) { Game::DeleteWorldObject(h); --s_Cg.nObj; } h = 0; }
static void CgDrop(CgEntC& e) { CgDelObj(e.obj); CgDelObj(e.objIn); if(e.blip) { Game::RemoveBlip(e.blip); e.blip = 0; } }
static void CgCloseEditor()
{
    if(s_Cg.ed.obj > 0) Game::DeleteWorldObject(s_Cg.ed.obj);
    s_Cg.ed.obj = 0; s_Cg.ed.open = false;
}
static void CgReset()
{
    for(auto& kv : s_Cg.ents) CgDrop(kv.second);
    s_Cg.ents.clear(); s_Cg.nObj = 0; s_Cg.haveMe = false; s_Cg.beatAt = 0;
    memset(s_Cg.masked, 0, sizeof(s_Cg.masked));
    if(s_Cg.gpsBlip) { Game::RemoveBlip(s_Cg.gpsBlip); s_Cg.gpsBlip = 0; }
    if(s_Cg.frozen) { const int ped = Game::LocalPed(); if(ped > 0) Game::FreezeChar(ped, false); s_Cg.frozen = false; }
    if(s_Cg.specId >= 0) { Game::RestoreCamera(); s_Cg.specId = -1; }
    s_Cg.jail = false; s_Cg.cdLeft = 0; s_Cg.cars.clear(); s_Cg.pend.clear(); s_Cg.usedAt.clear();
    s_nCgWeather = -1;
    CgCloseEditor();
    std::lock_guard<std::mutex> lk(s_Cg.mx); s_Cg.labels.clear(); s_Cg.btn = 0; s_Cg.btnHit = false;
}

// the model a thing shows (0 = none)
static int CgModelOf(const CgRow& r)
{
    switch(r.kind)
    {
        case CGK_BIZ:       return 1274;
        case CGK_HSSIGN:    return 1273;
        case CGK_INFOBOARD: return 1239;
        case CGK_GARAGE: case CGK_MOTEL: case CGK_HQ: case CGK_ENTRANCE: case CGK_BUILDING: return 1318;
        case CGK_MONEYBAG:  return 1550;
        case CGK_GIFTBOX:   return 1271;
        case CGK_ATM:       return 2942;
        case CGK_MAPBOX:    return 1258;
        case CGK_POINT:     return 1247;
        case CGK_OOCSHOP:   return 1274;
        case CGK_PICKUP:    return r.model ? r.model : 1239;
        case CGK_GATE: case CGK_OBJECT: return r.model;
        default: return 0;
    }
}
static bool CgHasIn(const CgRow& r) { return r.ix || r.iy || r.iz; }
static bool CgIsDoor(int k) { return k == CGK_BIZ || k == CGK_GARAGE || k == CGK_MOTEL || k == CGK_HQ || k == CGK_ENTRANCE || k == CGK_BUILDING; }

static void CgPose(const CgEntC& e, Vec3& p, float& rx, float& ry, float& rz)
{
    const CgRow& r = e.r; rx = ry = 0;
    if(r.kind == CGK_OBJECT) { p = { r.x, r.y, r.z }; rx = r.ix; ry = r.iy; rz = r.iz; return; }
    if(r.kind == CGK_GATE)
    {
        if(!CgHasIn(r)) { p = { r.x, r.y, r.z }; rz = r.a; return; }
        p = { r.x + (r.ix - r.x) * e.t, r.y + (r.iy - r.y) * e.t, r.z + (r.iz - r.z) * e.t }; rz = r.a + (r.ia - r.a) * e.t; return;
    }
    p = { r.x, r.y, r.z }; rz = r.a;
    if(r.kind == CGK_ATM) p.z -= 0.35f;   // the machine stands on the ground, the row is where the player stood
}

static void CgOnEnt(const PktCgEnt* P, int len)
{
    if(len < (int)offsetof(PktCgEnt, r)) return;
    int n = std::min<int>(P->n, CG_PAGE);
    if(len < (int)offsetof(PktCgEnt, r) + n * (int)sizeof(CgRow)) n = (len - (int)offsetof(PktCgEnt, r)) / (int)sizeof(CgRow);
    if(P->op == CGO_LIST) { for(auto& kv : s_Cg.ents) CgDrop(kv.second); s_Cg.ents.clear(); }
    for(int i = 0; i < n; ++i)
    {
        CgRow r; memcpy(&r, &P->r[i], sizeof(r));
        r.name[CG_NAME_LEN - 1] = 0; r.owner[sizeof(r.owner) - 1] = 0; r.text[CG_TEXT_LEN - 1] = 0;
        auto it = s_Cg.ents.find(r.id);
        if(P->op == CGO_DEL) { if(it != s_Cg.ents.end()) { CgDrop(it->second); s_Cg.ents.erase(it); } continue; }
        if(r.kind == CGK_NONE || r.kind >= CGK_COUNT) continue;
        if(it == s_Cg.ents.end()) { CgEntC c; c.r = r; c.t = (r.flags & CGF_OPEN) ? 1.0f : 0.0f; s_Cg.ents[r.id] = c; continue; }
        CgEntC& c = it->second; const CgRow o = c.r; c.r = r;
        const bool moved = o.kind != r.kind || o.model != r.model || fabsf(o.x - r.x) > 0.01f || fabsf(o.y - r.y) > 0.01f || fabsf(o.z - r.z) > 0.01f || fabsf(o.a - r.a) > 0.1f ||
                           (r.kind == CGK_OBJECT && (fabsf(o.ix - r.ix) > 0.1f || fabsf(o.iy - r.iy) > 0.1f || fabsf(o.iz - r.iz) > 0.1f));
        if(moved || fabsf(o.ix - r.ix) > 0.01f || fabsf(o.iy - r.iy) > 0.01f || fabsf(o.iz - r.iz) > 0.01f) { CgDelObj(c.obj); CgDelObj(c.objIn); }
        if(c.blip && (moved || o.model != r.model)) { Game::RemoveBlip(c.blip); c.blip = 0; }
    }
}

static void CgTeleport(int ped, const Vec3& p, float h, int area, bool withCar)
{
    if(area != 255) { if(RpOnServer()) RpSetInterior(area); else Game::EnterArea(area); }
    const int car = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
    if(withCar && car > 0 && Game::GetDriverOfCar(car) == ped)
    {
        Game::SetCarPos(car, { p.x, p.y, p.z + 0.5f }, 0.0f); Game::SetCarHeading(car, h); Game::SetMoveSpeed(car, true, { 0, 0, 0 });
        if(area != 255) Game::SetCarArea(car, area);
    }
    else
    {
        if(car > 0) Game::WarpOutOfCar(ped, p);
        Game::Teleport(ped, p, h);
    }
}

static bool CgIsAvatarCar(int car)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i != (int)myId && players[i].active && players[i].car == car) return true;
    return false;
}

// a thing that can need a second try (models / animation files load)
static bool CgDoPend(CgClient::Pend& q, int ped)
{
    switch(q.act)
    {
        case CGA_GUN: return Game::GiveWeaponAmmo(ped, q.a, q.val > 0 ? q.val : 1);
        case CGA_PARACHUTE: return Game::GiveWeaponAmmo(ped, 46, 1);
        case CGA_SKIN: return Game::ModelReady(q.val) && Game::SetLocalModel(q.val);
        case CGA_ANIM:
        {
            char file[32] = "", anim[48] = ""; if(sscanf(q.text.c_str(), "%31s %47s", file, anim) != 2) return true;
            return Game::PlayAnimOnce(ped, anim, file);
        }
        case CGA_SPAWNCAR:
        {
            if(q.val < 400 || q.val > 611) return true;
            if(!Game::ModelReady(q.val)) return false;
            Vec3 p = q.p; float h = q.h;
            if(!p.x && !p.y && !p.z)
            {
                Vec3 me; if(!Game::GetCharPos(ped, me)) return true;
                h = Game::GetCharHeading(ped); const float r = h * 0.0174533f;
                p = { me.x - sinf(r) * 3.5f, me.y + cosf(r) * 3.5f, me.z };
            }
            const int car = Game::CreateCar(q.val, { p.x, p.y, p.z + 0.5f }, true);
            if(car <= 0) return false;
            Game::SetCarHeading(car, h); Game::SetCarColours(car, q.a, q.b);
            const int ar = Game::GetCharArea(ped); if(ar > 0) Game::SetCarArea(car, ar);
            s_Cg.cars.push_back(car);
            if(!Game::IsCharInAnyCar(ped)) Game::WarpIntoCar(ped, car);
            return true;
        }
        default: return true;
    }
}

static void CgOnAct(const PktCgAct& A)
{
    const int ped = Game::LocalPed(); if(ped <= 0) return;
    const Vec3 p = { A.pos[0], A.pos[1], A.pos[2] };
    const bool hasPos = A.pos[0] || A.pos[1] || A.pos[2];
    const int car = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
    const uint64_t now = MP_NowMs();
    auto pend = [&](uint8_t act, int a, int b, int32_t val, Vec3 pp, float h, const char* text) {
        CgClient::Pend q{ act, a, b, val, pp, h, text ? text : "", now + 6000 };
        if(!CgDoPend(q, ped)) s_Cg.pend.push_back(q);
    };
    switch(A.act)
    {
        case CGA_TELE: CgTeleport(ped, p, A.pos[3], A.a, A.b == 1); break;
        case CGA_HEALTH: { int hp = A.a == 1 ? Game::GetCharHealth(ped) + A.val : A.val; Game::SetCharHealth(ped, std::max(0, std::min(176, hp))); break; }
        case CGA_ARMOUR: { int cur = Game::GetCharArmour(ped); int want = std::max(0, std::min(100, (int)A.val)); if(want != cur) Game::AddArmour(ped, want - cur); break; }
        case CGA_MONEY: if(!RpOnServer()) Game::AddMoney(A.val); break;
        case CGA_SETMONEY: if(!RpOnServer()) Game::AddMoney(A.val - Game::GetMoney()); break;
        case CGA_SETCASH: break;
        case CGA_GUN: pend(CGA_GUN, A.a, 0, A.val, p, 0, nullptr); break;
        case CGA_DISARM: Game::RemoveAllWeapons(ped); break;
        case CGA_SKIN: pend(CGA_SKIN, 0, 0, A.val, p, 0, nullptr); break;
        case CGA_FREEZE:
            s_Cg.frozen = A.a != 0; Game::FreezeChar(ped, s_Cg.frozen);
            s_Cg.freezeUntil = (s_Cg.frozen && A.val > 0) ? now + (uint64_t)A.val : 0;
            if(car > 0 && Game::GetDriverOfCar(car) == ped) Game::FreezeCar(car, s_Cg.frozen);
            break;
        case CGA_WEATHER: s_nCgWeather = A.val >= 0 && A.val < 256 ? A.val : -1; if(s_nCgWeather >= 0) Game::ForceWeatherNow(s_nCgWeather); break;
        case CGA_WANTED: Game::SetWanted(A.a == 1 ? Game::GetWantedLevel() + 1 : A.val); break;
        case CGA_GAMETEXT: GameText(A.text, A.val > 0 ? A.val : 3000, A.a == 1 ? 6 : 3); break;
        case CGA_COUNTDOWN: s_Cg.cdLeft = std::max(0, std::min(60, (int)A.val)); s_Cg.cdAt = 0; break;
        case CGA_FIXCAR: if(car > 0) Game::FixCar(car); break;
        case CGA_CARCOLOR: if(car > 0) Game::SetCarColours(car, A.a, A.b); break;
        case CGA_SPAWNCAR: pend(CGA_SPAWNCAR, A.a, A.b, A.val, p, A.pos[3], nullptr); break;
        case CGA_DELCAR:
            if(A.a == 1)
            {   // every empty car the server made for me
                for(int c : s_Cg.cars) if(Game::CarExists(c) && Game::GetDriverOfCar(c) <= 0) Game::DeleteCar(c);
                s_Cg.cars.erase(std::remove_if(s_Cg.cars.begin(), s_Cg.cars.end(), [](int c) { return !Game::CarExists(c); }), s_Cg.cars.end());
                break;
            }
            {
                int c = car;
                if(c <= 0) for(auto it = s_Cg.cars.rbegin(); it != s_Cg.cars.rend(); ++it) if(Game::CarExists(*it)) { c = *it; break; }
                if(c <= 0 || CgIsAvatarCar(c)) break;
                if(c == car) { Vec3 me; Game::GetCharPos(ped, me); me.x += 2.0f; me.z += 0.5f; Game::WarpOutOfCar(ped, me); }
                Game::DeleteCar(c);
                s_Cg.cars.erase(std::remove(s_Cg.cars.begin(), s_Cg.cars.end(), c), s_Cg.cars.end());
            }
            break;
        case CGA_EXPLODE: Game::AddExplosionNet(A.a, p); break;
        case CGA_FIRE: Game::StartFireNet(p, 2); break;
        case CGA_SPEC:
            if(A.b == 1 && A.a < MP_MAX_PLAYERS) { s_Cg.specId = A.a; Game::FreezeChar(ped, true); }
            else if(s_Cg.specId >= 0) { s_Cg.specId = -1; Game::RestoreCamera(); Game::FreezeChar(ped, s_Cg.frozen); }
            break;
        case CGA_PARACHUTE: CgTeleport(ped, p, Game::GetCharHeading(ped), 0, false); pend(CGA_PARACHUTE, 0, 0, 1, p, 0, nullptr); break;
        case CGA_ANIM: pend(CGA_ANIM, 0, 0, 0, p, 0, A.text); break;
        case CGA_WALK: if(A.text[0]) Game::SetAnimGroup(ped, A.text); break;
        case CGA_FIGHT: break;   // kept by the server (the game has no script call for it here)
        case CGA_MVADD:
            if(MP::IsOnDefault())
            {
                MvLoad();
                if(s_nMv >= MV_MAX) { SysMsg(COL_RED, "My Vehicles is full (%d) - the car could not be added.", MV_MAX); break; }
                MvCar& m = s_Mv[s_nMv++]; m = MvCar(); m.model = A.val;
                Vec3 me; Game::GetCharPos(ped, me); m.p = { me.x + 3.0f, me.y, me.z }; m.h = Game::GetCharHeading(ped);
                MvSave(); SysMsg(COL_GREEN, "%s added to My Vehicles (/mv).", A.val >= 400 && A.val <= 611 ? kVehNames[A.val - 400] : "Car");
            }
            break;
        case CGA_MVDEL:
            if(MP::IsOnDefault())
            {
                MvLoad();
                for(int i = 0; i < s_nMv; ++i) if(s_Mv[i].model == A.val)
                {
                    if(MvSpawned(s_Mv[i])) { if(Game::GetDriverOfCar(s_Mv[i].car) == ped) { Vec3 me; Game::GetCharPos(ped, me); me.x += 2.0f; Game::WarpOutOfCar(ped, me); } Game::DeleteCar(s_Mv[i].car); }
                    if(s_Mv[i].blip) Game::RemoveBlip(s_Mv[i].blip);
                    for(int k = i; k + 1 < s_nMv; ++k) s_Mv[k] = s_Mv[k + 1];
                    --s_nMv; MvSave(); break;
                }
            }
            break;
        case CGA_FUEL:
            if(A.a == 1) { if(car > 0) s_Fuel.cars[car].fuel = (float)std::max(0, std::min(100, (int)A.val)); }
            else for(auto& kv : s_Fuel.cars) kv.second.fuel = 100.0f;
            break;
        case CGA_FEED: s_Need.hunger = (float)std::max(0, std::min(100, (int)A.val)); break;
        case CGA_GPS:
            if(s_Cg.gpsBlip) { Game::RemoveBlip(s_Cg.gpsBlip); s_Cg.gpsBlip = 0; }
            if(hasPos) { s_Cg.gps = p; s_Cg.gpsBlip = Game::AddCoordBlip(p, 0xFF3B3BFF, 2); }
            break;
        case CGA_JAIL:
            if(A.val > 0)
            {
                s_Cg.jail = true; s_Cg.cell = p; s_Cg.cellH = A.pos[3]; s_Cg.cellArea = A.a; s_Cg.jailUntil = now + (uint64_t)A.val * 1000;
                Game::RemoveAllWeapons(ped); CgTeleport(ped, p, A.pos[3], A.a, false);
            }
            else if(s_Cg.jail)
            {
                s_Cg.jail = false;
                if(!RpOnServer()) CgTeleport(ped, { 1552.6f, -1675.6f, 16.2f }, 90.0f, 0, false);   // outside the LSPD
            }
            break;
        case CGA_CARHP: if(car > 0) Game::SetCarHealth(car, A.val); break;
        case CGA_CARTELE: CgTeleport(ped, p, A.pos[3], A.a, true); break;
        case CGA_MASK: if(A.b < MP_MAX_PLAYERS) s_Cg.masked[A.b] = A.a != 0; break;
        case CGA_KILL: Game::SetCharHealth(ped, 0); break;
        case CGA_EJECT: if(car > 0) { Vec3 me; Game::GetCharPos(ped, me); me.x += 2.0f; me.z += 0.5f; Game::WarpOutOfCar(ped, me); } break;
        case CGA_GOINCAR:
            if(A.a < MP_MAX_PLAYERS && players[A.a].active && players[A.a].car > 0 && Game::CarExists(players[A.a].car))
            {
                const int c = players[A.a].car; const int seat = Game::FreePassengerSeat(c, 0);
                if(seat >= 0) { if(car > 0) { Vec3 me; Game::GetCharPos(ped, me); Game::WarpOutOfCar(ped, me); } Game::WarpIntoCarAsPassenger(ped, c, seat); }
                else SysMsg(COL_RED, "That car is full.");
            }
            break;
        case CGA_EDITOR:
            if(A.a == 1)
            {
                CgCloseEditor();
                auto& e = s_Cg.ed; e.open = true; e.id = (uint16_t)A.val; e.model = (int)A.pos[3]; e.tryAt = 0; e.dirty = e.save = e.del = e.close = e.here = false;
                auto it = e.id ? s_Cg.ents.find(e.id) : s_Cg.ents.end();
                if(it != s_Cg.ents.end()) { const CgRow& r = it->second.r; e.p[0] = r.x; e.p[1] = r.y; e.p[2] = r.z; e.p[3] = r.ix; e.p[4] = r.iy; e.p[5] = r.iz; CgDelObj(it->second.obj); }
                else
                {
                    e.id = 0; Vec3 me; Game::GetCharPos(ped, me); const float h = Game::GetCharHeading(ped), r = h * 0.0174533f;
                    e.p[0] = me.x - sinf(r) * 3.0f; e.p[1] = me.y + cosf(r) * 3.0f; e.p[2] = me.z; e.p[3] = e.p[4] = 0; e.p[5] = h;
                }
                SysMsg(COL_YELLOW, "{FFA500}EDITOR: {FFFFFF}model %d - move it with the bar, SAVE keeps it on the server.", e.model);
            }
            else if(A.a == 2) s_Cg.ed.id = (uint16_t)A.val;
            else CgCloseEditor();
            break;
        default: break;
    }
}

static void CgOnPacket(const uint8_t* d, int len)
{
    if(len < 2) return;
    switch(d[0])
    {
        case PKT_CG_ENT: CgOnEnt((const PktCgEnt*)d, len); break;
        case PKT_CG_ME:
        {
            if(len < (int)sizeof(PktCgMe)) return;
            memcpy(&s_Cg.me, d, sizeof(PktCgMe)); s_Cg.me.famName[CG_NAME_LEN - 1] = 0; s_Cg.me.facName[CG_NAME_LEN - 1] = 0;
            s_Cg.haveMe = true;
            break;
        }
        case PKT_CG_ACT:
        {
            if(len < (int)sizeof(PktCgAct)) return;
            PktCgAct a; memcpy(&a, d, sizeof(a)); a.text[sizeof(a.text) - 1] = 0;
            CgOnAct(a);
            break;
        }
        default: break;
    }
}

// the default server: CGRP gas stations count for /refuel (mp_needs.inc)
static bool CgGasNear(const Vec3& p)
{
    if(!CgActive()) return false;
    for(const auto& kv : s_Cg.ents)
    {
        const CgRow& r = kv.second.r; if(r.kind != CGK_GSTATION) continue;
        float dx = p.x - r.x, dy = p.y - r.y, dz = p.z - r.z;
        if(dx * dx + dy * dy < 14.0f * 14.0f && fabsf(dz) < 8.0f) return true;
    }
    return false;
}

static void CgLabelFor(const CgEntC& e, bool inside, char* out, size_t n)
{
    const CgRow& r = e.r; out[0] = 0;
    const char* lock = (r.flags & CGF_LOCKED) ? "\n{FF4444}Locked" : "";
    const char* vip = (r.flags & CGF_VIP) ? " {FFD700}[VIP]" : "";
    if(inside) { snprintf(out, n, "{FFFFFF}%s%s\n{AAAAAA}/exit", r.name, vip); return; }
    switch(r.kind)
    {
        case CGK_BIZ:
            if(r.flags & CGF_SALE) snprintf(out, n, "{FFD700}%s {AAAAAA}(%s)\n{33CC55}For sale: ${FFFFFF}%d  {AAAAAA}level %d\n/buybiz  -  /enter%s", r.name, kCgBizType[r.sub < CGB_COUNT ? r.sub : 0], r.price, r.level, lock);
            else snprintf(out, n, "{FFD700}%s {AAAAAA}(%s)\n{FFFFFF}Owner: %s\n{AAAAAA}/enter%s", r.name, kCgBizType[r.sub < CGB_COUNT ? r.sub : 0], r.owner[0] ? r.owner : "-", lock);
            break;
        case CGK_GARAGE:
            if(r.flags & CGF_SALE) snprintf(out, n, "{FFD700}Garage %d\n{33CC55}For sale: ${FFFFFF}%d\n{AAAAAA}/buygarage%s", r.id, r.price, lock);
            else snprintf(out, n, "{FFD700}Garage %d\n{FFFFFF}Owner: %s\n{AAAAAA}/enter (in your car)%s", r.id, r.owner[0] ? r.owner : "-", lock);
            break;
        case CGK_MOTEL: snprintf(out, n, "{FFD700}%s%s\n{FFFFFF}Room: $%d a payday\n{AAAAAA}/rentroom  -  /enter%s", r.name, vip, r.price, lock); break;
        case CGK_ATM: snprintf(out, n, "{33CC55}ATM\n{AAAAAA}/atm"); break;
        case CGK_MAPBOX: snprintf(out, n, "{33CCFF}Map Box\n{AAAAAA}/buymap ($25)  -  /viewmap"); break;
        case CGK_HQ: snprintf(out, n, "{FFD700}%s\n{FFFFFF}%s HQ\n{AAAAAA}/enter%s", r.name[0] ? r.name : "Headquarters", r.sub == CGG_FAMILY ? "Family" : "Faction", lock); break;
        case CGK_ENTRANCE: case CGK_BUILDING: snprintf(out, n, "{FFD700}%s%s\n{AAAAAA}/enter%s", r.name, vip, lock); break;
        case CGK_INFOBOARD:
        {
            char t[CG_TEXT_LEN]; MP_CopyStr(t, r.text, sizeof(t)); for(char* c = t; *c; ++c) if(*c == '|') *c = '\n';
            snprintf(out, n, "{33CCFF}%s\n{FFFFFF}%s", r.name[0] ? r.name : "Information", t); break;
        }
        case CGK_OOCSHOP: snprintf(out, n, "{FFD700}OOC Shop\n{AAAAAA}/oocshop (gold bars)"); break;
        case CGK_GSTATION: snprintf(out, n, "{FFA500}%s\n{AAAAAA}/refuel", r.name); break;
        case CGK_DRIVETHRU: snprintf(out, n, "{FFA500}%s\n{AAAAAA}/order (in a car)", r.name); break;
        case CGK_MONEYBAG: snprintf(out, n, "{33CC55}Money Bag\n{FFFFFF}$%d", r.val); break;
        case CGK_GIFTBOX: snprintf(out, n, "{FF66CC}Gift Box\n{AAAAAA}/getgift"); break;
        case CGK_POINT: snprintf(out, n, "{FFD700}%s\n{FFFFFF}Owner: %s\n{AAAAAA}/capture", r.name, r.owner[0] ? r.owner : "nobody"); break;
        case CGK_HSSIGN: snprintf(out, n, "{33CC55}House for sale\n{FFFFFF}%s\n{AAAAAA}$%d", r.name[0] ? r.name : r.text, r.price); break;
        case CGK_PICKUP: if(r.sub == CGP_INFO && r.text[0]) snprintf(out, n, "{FFFFFF}%s", r.text); break;
        case CGK_CARSPOT:
            snprintf(out, n, "{FFD700}%s vehicle\n{FFFFFF}%s%s\n{AAAAAA}/getcar", r.sub == CGG_FAMILY ? "Family" : r.sub == CGG_FACTION ? "Faction" : r.sub == CGG_PLAYER ? "Personal" : "Public",
                     r.model >= 400 && r.model <= 611 ? kVehNames[r.model - 400] : "Car", r.price > 0 ? (std::string("  $") + std::to_string(r.price)).c_str() : "");
            break;
        case CGK_SPEEDCAM: if(CgMyLevel() >= 7) snprintf(out, n, "{FF4444}Speed camera %d\n{AAAAAA}%d km/h, fine $%d", r.id, r.val, r.price); break;
        case CGK_OBJECT: case CGK_GATE: if(s_Cg.ed.open) snprintf(out, n, "{AAAAAA}%s %d (%d)", kCgKindName[r.kind], r.id, r.model); break;
        default: break;
    }
}

// every game tick on the default server / Mega Vil
static void CgProcess(int ped, uint64_t now)
{
    if(!CgActive()) { if(!s_Cg.ents.empty() || s_Cg.haveMe) CgReset(); return; }
    if(ped <= 0) return;
    if(!s_Cg.beatAt || now - s_Cg.beatAt > 2000) { s_Cg.beatAt = now; PktCgCmd c; CgFill(c, CGC_BEAT); client.Send(&c, sizeof(c)); }
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const int area = Game::GetCharArea(ped);
    const float dt = 1.0f / 30.0f;

    // things that wait
    for(size_t i = 0; i < s_Cg.pend.size();) { if(CgDoPend(s_Cg.pend[i], ped) || now > s_Cg.pend[i].until) s_Cg.pend.erase(s_Cg.pend.begin() + i); else ++i; }
    if(s_Cg.frozen && s_Cg.freezeUntil && now >= s_Cg.freezeUntil) { s_Cg.frozen = false; s_Cg.freezeUntil = 0; Game::FreezeChar(ped, s_Cg.specId >= 0); }
    if(s_Cg.jail)
    {
        if(now >= s_Cg.jailUntil + 5000) s_Cg.jail = false;   // the server frees me; this is only the backup
        else if(now - s_Cg.jailChk > 1000)
        {
            s_Cg.jailChk = now;
            float dx = me.x - s_Cg.cell.x, dy = me.y - s_Cg.cell.y, dz = me.z - s_Cg.cell.z;
            if(dx * dx + dy * dy + dz * dz > 9.0f * 9.0f) CgTeleport(ped, s_Cg.cell, s_Cg.cellH, s_Cg.cellArea, false);
        }
    }
    if(s_Cg.gpsBlip) { float dx = me.x - s_Cg.gps.x, dy = me.y - s_Cg.gps.y; if(dx * dx + dy * dy < 6.0f * 6.0f) { Game::RemoveBlip(s_Cg.gpsBlip); s_Cg.gpsBlip = 0; GameText("~g~Checkpoint", 1500, 3); } }
    if(s_Cg.cdLeft > 0 && now - s_Cg.cdAt >= 1000)
    {
        s_Cg.cdAt = now; char t[16]; snprintf(t, sizeof(t), "~w~%d", s_Cg.cdLeft); GameText(t, 900, 3);
        if(--s_Cg.cdLeft == 0) s_Cg.cdLeft = -1;
    }
    else if(s_Cg.cdLeft < 0 && now - s_Cg.cdAt >= 1000) { s_Cg.cdLeft = 0; GameText("~g~GO!", 1200, 3); }
    if(s_Cg.specId >= 0)
    {
        const RemotePlayer& t = players[s_Cg.specId];
        Vec3 tp; if(!t.active || t.ped <= 0 || !Game::GetCharPos(t.ped, tp)) { s_Cg.specId = -1; Game::RestoreCamera(); Game::FreezeChar(ped, s_Cg.frozen); }
        else { const float h = Game::GetCharHeading(t.ped) * 0.0174533f; Game::SetFixedCamera({ tp.x + sinf(h) * 6.0f, tp.y - cosf(h) * 6.0f, tp.z + 2.5f }, tp); }
    }

    // objects / pickups / gates / radar icons
    int made = 0; const int freeSlots = Game::FreeObjectSlots();
    for(auto& kv : s_Cg.ents)
    {
        CgEntC& c = kv.second; const CgRow& r = c.r;
        if(r.kind == CGK_MAPICON)
        {
            if(!c.blip) c.blip = Game::AddSpriteBlip({ r.x, r.y, r.z }, r.model ? r.model : 56);
            continue;
        }
        if(r.kind == CGK_GATE && CgHasIn(r))
        {
            const float want = (r.flags & CGF_OPEN) ? 1.0f : 0.0f;
            if(c.t != want)
            {
                c.t = want > c.t ? std::min(want, c.t + dt * 0.5f) : std::max(want, c.t - dt * 0.5f);
                if(c.obj > 0) { Vec3 p; float rx, ry, rz; CgPose(c, p, rx, ry, rz); Game::PlaceMapObject(c.obj, p, rx, ry, rz); }
            }
        }
        const int model = CgModelOf(r);
        if(!model || (s_Cg.ed.open && r.kind == CGK_OBJECT && r.id == s_Cg.ed.id)) { CgDelObj(c.obj); continue; }
        const float dx = r.x - me.x, dy = r.y - me.y, d2 = dx * dx + dy * dy;
        if(c.obj > 0) { if(d2 > (CG_STREAM_M + 20.0f) * (CG_STREAM_M + 20.0f) || !Game::WorldObjectExists(c.obj)) CgDelObj(c.obj); }
        else if(d2 < CG_STREAM_M * CG_STREAM_M && made < 4 && now >= c.tryAt && s_Cg.nObj < 600 && (freeSlots < 0 || freeSlots > 80))
        {
            Vec3 p; float rx, ry, rz; CgPose(c, p, rx, ry, rz);
            const int h = Game::CreateMapObject(model, p, rx, ry, rz);
            if(h > 0) { c.obj = h; ++s_Cg.nObj; ++made; }
            else c.tryAt = now + (h < 0 ? 5000 : 300);
        }
        // the way out of an inside: an arrow there too
        if(CgIsDoor(r.kind) && CgHasIn(r))
        {
            const float ex = r.ix - me.x, ey = r.iy - me.y, e2 = ex * ex + ey * ey;
            if(c.objIn > 0) { if(e2 > 80.0f * 80.0f || !Game::WorldObjectExists(c.objIn)) CgDelObj(c.objIn); }
            else if(e2 < 60.0f * 60.0f && made < 4 && now >= c.tryAt && s_Cg.nObj < 600)
            {
                const int h = Game::CreateMapObject(1318, { r.ix, r.iy, r.iz }, 0, 0, r.ia);
                if(h > 0) { c.objIn = h; ++s_Cg.nObj; ++made; }
                else c.tryAt = now + (h < 0 ? 5000 : 300);
            }
        }
    }

    // walking into a money bag / pickup takes it
    const bool inCar = Game::IsCharInAnyCar(ped);
    if(now - s_Cg.autoAt > 300)
    {
        s_Cg.autoAt = now;
        for(const auto& kv : s_Cg.ents)
        {
            const CgRow& r = kv.second.r; if(r.kind != CGK_MONEYBAG && r.kind != CGK_PICKUP) continue;
            if(r.kind == CGK_PICKUP && r.sub == CGP_INFO) continue;
            float dx = r.x - me.x, dy = r.y - me.y, dz = r.z - me.z; if(dx * dx + dy * dy > 1.5f * 1.5f || fabsf(dz) > 2.0f || inCar) continue;
            uint64_t& at = s_Cg.usedAt[r.id]; if(at && now - at < 6000) continue;
            at = now; CgSendText(r.kind == CGK_MONEYBAG ? "takebag" : "usepickup", "");
            break;
        }
    }

    // labels + the ENTER / EXIT button
    if(now - s_Cg.lblAt > 200)
    {
        s_Cg.lblAt = now;
        std::vector<CgLabel> L; int btn = 0;
        const uint16_t inside = s_Cg.haveMe ? s_Cg.me.insideId : 0;
        for(const auto& kv : s_Cg.ents)
        {
            const CgEntC& c = kv.second; const CgRow& r = c.r;
            char t[256];
            float dx = r.x - me.x, dy = r.y - me.y, dz = r.z - me.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
            if(d < 18.0f && (r.kind != CGK_MAPICON))
            {
                CgLabelFor(c, false, t, sizeof(t));
                if(t[0]) L.push_back({ { r.x, r.y, r.z + (r.kind == CGK_ATM ? 1.4f : 0.9f) }, t, d });
                const float range = r.kind == CGK_GARAGE ? 6.0f : 2.2f;
                if(CgIsDoor(r.kind) && CgHasIn(r) && d < range && (!inCar || r.kind == CGK_GARAGE)) btn = 1;
            }
            if(CgIsDoor(r.kind) && CgHasIn(r))
            {
                float ix = r.ix - me.x, iy = r.iy - me.y, iz = r.iz - me.z, di = sqrtf(ix * ix + iy * iy + iz * iz);
                if(di < 15.0f && (area == r.iarea || inside == r.id))
                {
                    CgLabelFor(c, true, t, sizeof(t)); L.push_back({ { r.ix, r.iy, r.iz + 0.9f }, t, di });
                    if(di < (r.kind == CGK_GARAGE ? 6.0f : 2.2f) && inside == r.id) btn = 2;
                }
            }
        }
        bool hit;
        { std::lock_guard<std::mutex> lk(s_Cg.mx); s_Cg.labels.swap(L); s_Cg.btn = btn; hit = s_Cg.btnHit; s_Cg.btnHit = false; }
        if(hit && btn) CgSendText(btn == 1 ? "enter" : "exit", "");
    }

    // /editor: the object I move
    auto& e = s_Cg.ed;
    if(e.open)
    {
        bool save, del, close, here, dirty; float pp[6];
        { std::lock_guard<std::mutex> lk(s_Cg.mx); save = e.save; del = e.del; close = e.close; here = e.here; dirty = e.dirty; e.save = e.del = e.close = e.here = e.dirty = false; memcpy(pp, e.p, sizeof(pp)); }
        if(here) { const float h = Game::GetCharHeading(ped), r = h * 0.0174533f; pp[0] = me.x - sinf(r) * 3.0f; pp[1] = me.y + cosf(r) * 3.0f; pp[2] = me.z; { std::lock_guard<std::mutex> lk(s_Cg.mx); memcpy(e.p, pp, sizeof(pp)); } dirty = true; }
        if(e.obj <= 0 && now >= e.tryAt)
        {
            const int h = Game::CreateMapObject(e.model, { pp[0], pp[1], pp[2] }, pp[3], pp[4], pp[5]);
            if(h > 0) e.obj = h; else { e.tryAt = now + (h < 0 ? 3000 : 250); if(h < 0) SysMsg(COL_RED, "EDITOR: model %d could not be made here.", e.model); }
        }
        else if(e.obj > 0 && dirty) Game::PlaceMapObject(e.obj, { pp[0], pp[1], pp[2] }, pp[3], pp[4], pp[5]);
        if(save || del)
        {
            PktCgCmd c; CgFill(c, CGC_EDITOR); c.edId = e.id; c.edModel = del ? 0 : (uint16_t)e.model; memcpy(c.ed, pp, sizeof(c.ed));
            if(!del || e.id) client.Send(&c, sizeof(c));
            if(del) close = true;
        }
        if(close) CgCloseEditor();
    }
}

// the render thread: labels, the ENTER / EXIT button, the /editor bar
static void CgRender()
{
    if(!CgActive()) return;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    std::lock_guard<std::mutex> lk(s_Cg.mx);
    for(const auto& l : s_Cg.labels) HkText3D(bg, l.p, l.text.c_str(), l.d);
    if(SampDialogOpen()) return;
    const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing;
    if(s_Cg.btn && !s_Cg.ed.open)
    {
        const float w = UI::S(130), h = UI::S(48), x = (ds.x - w) * 0.5f, y = ds.y * 0.62f;
        ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("##CgDoor", nullptr, wf);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if(ImGui::InvisibleButton("##cgdoor", ImVec2(w, h))) s_Cg.btnHit = true;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::IsItemActive() ? IM_COL32(40, 40, 40, 190) : IM_COL32(0, 0, 0, 140), UI::S(10));
        const char* t = s_Cg.btn == 1 ? "ENTER" : "EXIT";
        dl->AddText(nullptr, UI::S(17), ImVec2(x + (w - RpTextW(UI::S(17), t)) * 0.5f, y + (h - UI::S(17)) * 0.5f), IM_COL32_WHITE, t);
        ImGui::End(); ImGui::PopStyleVar();
    }
    auto& e = s_Cg.ed;
    if(!e.open) return;
    const float bs = UI::S(62), gap = UI::S(7), bh = UI::S(44), bw = 7 * bs + 6 * gap;
    const float x0 = (ds.x - bw) * 0.5f, y0 = ds.y - UI::S(200);
    ImGui::SetNextWindowPos(ImVec2(x0 - UI::S(12), y0 - UI::S(40))); ImGui::SetNextWindowSize(ImVec2(bw + UI::S(24), bh * 2 + gap + UI::S(52)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##CgEditor", nullptr, wf);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char top[160];
    snprintf(top, sizeof(top), "%s %d  model %d   %.1f %.1f %.1f   rot %.0f %.0f %.0f   step %.1f", e.id ? "Object" : "New object", e.id, e.model, e.p[0], e.p[1], e.p[2], e.p[3], e.p[4], e.p[5], e.step);
    dl->AddRectFilled(ImVec2(x0 - UI::S(8), y0 - UI::S(34)), ImVec2(x0 + bw + UI::S(8), y0 - UI::S(6)), IM_COL32(0, 0, 0, 150), UI::S(8));
    dl->AddText(nullptr, UI::S(15), ImVec2(x0 + (bw - RpTextW(UI::S(15), top)) * 0.5f, y0 - UI::S(28)), IM_COL32_WHITE, top);
    static const char* const lab[14] = { "X -", "X +", "Y -", "Y +", "Z -", "Z +", "Step", "Turn -", "Turn +", "Tilt", "Here", "SAVE", "Delete", "Close" };
    for(int i = 0; i < 14; ++i)
    {
        const int row = i / 7, col = i % 7;
        ImVec2 a(x0 + col * (bs + gap), y0 + row * (bh + gap));
        ImGui::SetCursorScreenPos(a); char id[16]; snprintf(id, sizeof(id), "##cge%d", i);
        const bool hit = ImGui::InvisibleButton(id, ImVec2(bs, bh));
        ImU32 fill = ImGui::IsItemActive() ? IM_COL32(40, 40, 40, 200) : IM_COL32(0, 0, 0, 150);
        if(i == 11) fill = IM_COL32(37, 99, 235, 215); else if(i == 12) fill = IM_COL32(185, 28, 28, 200);
        dl->AddRectFilled(a, ImVec2(a.x + bs, a.y + bh), fill, UI::S(9));
        const float fs = UI::S(14);
        dl->AddText(nullptr, fs, ImVec2(a.x + (bs - RpTextW(fs, lab[i])) * 0.5f, a.y + (bh - fs) * 0.5f), IM_COL32_WHITE, lab[i]);
        if(!hit) continue;
        switch(i)
        {
            case 0: e.p[0] -= e.step; e.dirty = true; break;   case 1: e.p[0] += e.step; e.dirty = true; break;
            case 2: e.p[1] -= e.step; e.dirty = true; break;   case 3: e.p[1] += e.step; e.dirty = true; break;
            case 4: e.p[2] -= e.step; e.dirty = true; break;   case 5: e.p[2] += e.step; e.dirty = true; break;
            case 6: e.step = e.step < 0.2f ? 0.5f : e.step < 1.0f ? 2.0f : 0.1f; break;
            case 7: e.p[5] = fmodf(e.p[5] - 15.0f + 360.0f, 360.0f); e.dirty = true; break;
            case 8: e.p[5] = fmodf(e.p[5] + 15.0f, 360.0f); e.dirty = true; break;
            case 9: e.p[3] = fmodf(e.p[3] + 15.0f, 360.0f); e.dirty = true; break;
            case 10: e.here = true; break;
            case 11: e.save = true; break;
            case 12: e.del = true; break;
            default: e.close = true; break;
        }
    }
    ImGui::End(); ImGui::PopStyleVar();
}

// true = the CGRP has it (sent to the server)
static bool CgCommand(const char* cmd, const char* arg)
{
    if(!CgActive()) return false;
    const bool rp = RpOnServer();
    const int mask = rp ? CGM_M : CGM_D;
    #define CGIS(x) (!strcmp(cmd, x))
    if(CGIS("accept"))
    {
        if(!strncasecmp(arg, "car", 3)) { CgSendText("acceptcar", ""); return true; }
        if(!strncasecmp(arg, "family", 6)) { CgSendText("acceptfamily", ""); return true; }
        if(!rp && !strncasecmp(arg, "faction", 7)) { CgSendText("acceptfaction", ""); return true; }
        return false;
    }
    if(CGIS("exit")) { if(!s_Cg.haveMe || !s_Cg.me.insideId) return false; CgSendText(cmd, arg); return true; }
    if(CGIS("buy"))
    {
        if(!s_Cg.haveMe || !s_Cg.me.insideId) return false;
        auto it = s_Cg.ents.find(s_Cg.me.insideId); if(it == s_Cg.ents.end() || it->second.r.kind != CGK_BIZ) return false;
        CgSendText(cmd, arg); return true;
    }
    if(CGIS("sellcar") && rp) { int a = 0, b = 0; if(sscanf(arg, "%d %d", &a, &b) < 2) return false; }   // one number = the Mega Vil /sellcar [slot]
    if(CGIS("freeze") && (!*arg || CgMyLevel() < 2)) return false;   // the old /freeze toggle stays
    const CgCmdDef* def = CgFindCmd(cmd, mask);
    if(!def) return false;
    CgSendText(cmd, arg);
    return true;
    #undef CGIS
}
