// Linux test (2.17): sync extra block (aim / interior / driver inputs) round trip + 2.16 compat,
// every-bullet shot packets (near only, exact values), car state, mission-sync markers (partner
// only, also while on a mission), and the explosion relay that crashed the 32-bit host.
//   g++ -O2 -std=c++17 -pthread -I../src sync217_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o sync217_test
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int id = -1, partner = -1, shots = 0, shotEntries = 0, veh = 0, mm = 0, mmCount = 0, exps = 0;
    ShotEntry lastShot; PktVehState lastVeh; PktMissionMarkers lastMM;
    void OnConnected(const PktWelcome& w) override { id = w.yourId; }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {}
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {}
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
    void OnExplosion(const PktExplosion&) override { ++exps; }
    void OnPair(const PktPair& a) override { if(a.action == PAIR_LINKED) partner = a.other; }
    void OnShot(const PktShot& m) override { ++shots; shotEntries += m.count; memcpy(&lastShot, &m.e[m.count - 1], sizeof(lastShot)); }
    void OnVehState(const PktVehState& v) override { ++veh; lastVeh = v; }
    void OnMissionMarkers(const PktMissionMarkers& m) override { ++mm; mmCount = m.count; lastMM = m; }
};

static MPClient cl[4]; static Ev ev[4];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < 4; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void SendPos(int i, float x, float y, bool mission = false)
{
    PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.d.pos[0] = x; p.d.pos[1] = y; p.d.health = 100;
    if(mission) p.d.flags |= SYNC_MISSION;
    SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = 1;
    uint8_t buf[sizeof(PktSync) + sizeof(SyncExt)]; memcpy(buf, &p, sizeof(p));
    int n = MP_EncodeSyncExt(e, buf + sizeof(p));
    cl[i].Send(buf, (int)sizeof(p) + n);
}
static void Pair(int from, uint8_t act, int to) { PktPair q; q.type = PKT_PAIR; q.action = act; q.other = (uint8_t)ev[to].id; cl[from].Send(&q, sizeof(q)); }

int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-62s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)

    // ---- sync extra block -------------------------------------------------------------------
    {
        SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = 42; e.fullAnims = 1; e.animCount = 2;
        for(int i = 0; i < 2; ++i) { AnimSync g; memset(&g, 0, sizeof(g)); g.group = 54; g.id = (int16_t)i; g.blend = 120; g.speed = 50; g.flags = 2; memcpy(&e.anim[i], &g, sizeof(g)); }
        e.hasX2 = 1; e.x2flags = X2_AIMING | X2_SHOTSYNC | X2_VEHFAST | X2_HORN; e.interior = 3;
        e.steer = -0.55f; e.gas = 0.8f; e.brake = 0.4f; e.misc = 4321; e.doomV = 0.25f; e.doomH = -1.5f;
        uint8_t buf[sizeof(SyncExt)]; int n = MP_EncodeSyncExt(e, buf);
        SyncExt d; bool ok = MP_DecodeSyncExt(buf, n, d);
        CHECK(ok && d.hasX2 && d.x2flags == e.x2flags && d.interior == 3, "2.17 extra block: aim / shot sync / horn / interior");
        CHECK(fabsf(d.steer + 0.55f) < 0.011f && fabsf(d.gas - 0.8f) < 0.011f && fabsf(d.brake - 0.4f) < 0.005f &&
              d.misc == 4321 && fabsf(d.doomV - 0.25f) < 0.002f && fabsf(d.doomH + 1.5f) < 0.002f, "driver inputs: steering, pedals, Hydra nozzles, turret");
        // a 2.16 phone: same bytes, it simply doesn't know bit 3 -> anims still decode identically
        uint8_t old[sizeof(SyncExt)]; memcpy(old, buf, n); old[3] &= (uint8_t)~8;
        SyncExt o; bool okOld = MP_DecodeSyncExt(old, n, o);
        AnimSync a1; memcpy(&a1, &o.anim[1], sizeof(a1));
        CHECK(okOld && !o.hasX2 && o.animCount == 2 && a1.group == 54 && a1.id == 1, "older phones ignore the extra block (anims unchanged)");
        SyncExt f; memset(&f, 0, sizeof(f)); f.ver = MP_SYNC_EXT_VER; f.hasX2 = 1; f.interior = 1;
        int nf = MP_EncodeSyncExt(f, buf);
        printf("extra block cost: %d bytes on foot, %d driving\n", nf - 14, n - MP_EncodeSyncExt(([&]{ SyncExt z = e; z.hasX2 = 0; return z; })(), old));
    }

    MPServer srv; if(!srv.Start(19335, "Sync 2.17 Test", [](const char*){}, 100)) return 1;
    for(int i = 0; i < 4; ++i) { char n[8]; snprintf(n, 8, "P%d", i); cl[i].Connect("127.0.0.1", 19335, n, 106); }
    Pump(700);
    CHECK(ev[0].id >= 0 && ev[1].id >= 0 && ev[2].id >= 0 && ev[3].id >= 0, "4 clients connected");
    SendPos(0, 2495, -1686); SendPos(1, 2500, -1690); SendPos(2, -2000, 500); SendPos(3, 2490, -1680);
    Pump(300);

    // ---- explosion relay (the 2.16.2 SIGBUS: pos at an odd offset inside the packet) --------
    PktExplosion ex; memset(&ex, 0, sizeof(ex)); ex.type = PKT_EXPLOSION; ex.expType = 2; ex.pos[0] = 2495; ex.pos[1] = -1686; ex.pos[2] = 13;
    cl[0].Send(&ex, sizeof(ex)); Pump(200);
    CHECK(ev[1].exps == 1 && ev[3].exps == 1 && ev[2].exps == 0, "explosion relayed near only (no unaligned float* any more)");

    // ---- shots ------------------------------------------------------------------------------
    PktShot s; memset(&s, 0, sizeof(s)); s.type = PKT_SHOT; s.count = 3;
    for(int i = 0; i < 3; ++i)
    {
        ShotEntry e; memset(&e, 0, sizeof(e)); e.kind = (uint8_t)(i == 2 ? 1 : 0); e.weapon = (uint8_t)(i == 2 ? 35 : 31); e.force = 0.5f * i;
        float a[3] = { 2495.f + i, -1686.f, 14.f }, b[3] = { 2510.f, -1690.f + i, 13.5f };
        memcpy(e.a, a, 12); memcpy(e.b, b, 12); memcpy(&s.e[i], &e, sizeof(e));
    }
    cl[0].Send(&s, MP_PKT_SHOT_HDR + 3 * (int)sizeof(ShotEntry)); Pump(200);
    float lb[3]; memcpy(lb, ev[1].lastShot.b, 12);
    CHECK(ev[1].shots == 1 && ev[1].shotEntries == 3 && ev[3].shotEntries == 3, "every bullet + the rocket reach nearby players");
    CHECK(ev[1].lastShot.kind == 1 && ev[1].lastShot.weapon == 35 && fabsf(lb[1] + 1688.f) < 0.001f && fabsf(ev[1].lastShot.force - 1.0f) < 0.001f, "shot values arrive exactly (impact point, rocket force)");
    CHECK(ev[2].shots == 0 && ev[0].shots == 0, "far player and the shooter don't get them");
    PktShot bad = s; bad.count = 200; cl[0].Send(&bad, MP_PKT_SHOT_HDR + 2 * (int)sizeof(ShotEntry)); Pump(200);
    CHECK(ev[1].shotEntries == 5, "lying count is clamped to what really arrived");

    // ---- car state --------------------------------------------------------------------------
    PktVehState v; memset(&v, 0, sizeof(v)); v.type = PKT_VEH_STATE; v.model = 411; v.col[0] = 3; v.col[1] = 1; v.paintjob = 2;
    v.flags = VEHF_ENGINE | VEHF_LIGHTS | VEHF_SIREN; v.lock = 2; v.isAuto = 1; v.doors[2] = 4; v.wheels[1] = 1; v.panels = 0x0300020; v.lights = 0x5; v.health = 640;
    v.mods[0] = 1010; v.mods[16] = 1193; v.doorOpen[0] = 200; float vp[3] = { 2496, -1687, 13 }; memcpy(v.pos, vp, 12);
    cl[0].Send(&v, sizeof(v)); Pump(200);
    CHECK(ev[1].veh == 1 && ev[1].lastVeh.id == ev[0].id && ev[1].lastVeh.model == 411 && ev[1].lastVeh.paintjob == 2 && ev[1].lastVeh.col[0] == 3,
          "car state: model, colours, paintjob");
    CHECK(ev[1].lastVeh.health == 640 && ev[1].lastVeh.mods[0] == 1010 && ev[1].lastVeh.mods[16] == 1193 && ev[1].lastVeh.doors[2] == 4 &&
          ev[1].lastVeh.wheels[1] == 1 && ev[1].lastVeh.panels == 0x0300020 && ev[1].lastVeh.lock == 2 && ev[1].lastVeh.doorOpen[0] == 200 &&
          ev[1].lastVeh.flags == (VEHF_ENGINE | VEHF_LIGHTS | VEHF_SIREN), "car state: tuning, health, damage, doors, lock, engine/lights/siren");
    CHECK(ev[2].veh == 0, "far player doesn't get it");

    // ---- mission sync -----------------------------------------------------------------------
    Pair(0, PAIR_REQUEST, 1); Pump(150); Pair(1, PAIR_ACCEPT, 0); Pump(200);
    CHECK(ev[0].partner == ev[1].id && ev[1].partner == ev[0].id, "mission sync linked P0 + P1");
    PktMissionMarkers m; memset(&m, 0, sizeof(m)); m.type = PKT_MISSION_MARKERS; m.count = 2;
    for(int i = 0; i < 2; ++i) { MissionMarker k; memset(&k, 0, sizeof(k)); float p[3] = { 100.f * i, 200.f, 10.f }; memcpy(k.pos, p, 12); k.colour = (uint32_t)i; k.display = 3; k.kind = 4; memcpy(&m.m[i], &k, sizeof(k)); }
    cl[0].Send(&m, MP_PKT_MM_HDR + 2 * (int)sizeof(MissionMarker)); Pump(200);
    MissionMarker k1; memcpy(&k1, &ev[1].lastMM.m[1], sizeof(k1)); float kp[3]; memcpy(kp, k1.pos, 12);
    CHECK(ev[1].mm == 1 && ev[1].mmCount == 2 && fabsf(kp[0] - 100.f) < 0.001f && k1.kind == 4, "mission markers reach the partner");
    CHECK(ev[3].mm == 0 && ev[2].mm == 0, "nobody else gets them");
    // P0 starts a mission: other players are hidden, but the partner still sees the shots
    SendPos(0, 2495, -1686, true); Pump(200);
    int p1 = ev[1].shotEntries, p3 = ev[3].shotEntries;
    cl[0].Send(&s, MP_PKT_SHOT_HDR + 1 * (int)sizeof(ShotEntry)); Pump(200);
    CHECK(ev[1].shotEntries == p1 + 1 && ev[3].shotEntries == p3, "on a mission: shots only reach the mission-sync partner");
    srv.SetPerms(0); Pump(200);
    cl[0].Send(&m, MP_PKT_MM_HDR + 2 * (int)sizeof(MissionMarker)); Pump(200);
    CHECK(ev[1].mm == 1, "host turns Mission sync off: markers stop");

    printf(pass ? "ALL PASSED\n" : "SOME FAILED\n");
    for(auto& c : cl) c.Disconnect();
    srv.Stop();
    return pass ? 0 : 1;
}
