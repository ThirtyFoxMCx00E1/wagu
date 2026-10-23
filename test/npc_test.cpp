// Linux test (2.18 NPC sync) - harness from sync217_test.
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
    int npc = 0, npcPeds = 0, npcCars = 0, hits = 0, claims = 0; PktNpc lastNpc; PktNpcHit lastHit; PktNpcClaim lastClaim;
    void OnNpc(const PktNpc& m) override { ++npc; npcPeds = m.h.nPeds; npcCars = m.h.nCars; lastNpc = m; }
    void OnNpcHit(const PktNpcHit& h) override { ++hits; lastHit = h; }
    void OnNpcClaim(const PktNpcClaim& c) override { ++claims; lastClaim = c; }
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


static void SendNpc(int from, int np, int nc, float cx, float cy)
{
    PktNpc m; memset(&m, 0, sizeof(m)); m.h.type = PKT_NPC; m.h.nPeds = (uint8_t)np; m.h.nCars = (uint8_t)nc;
    float c[3] = { cx, cy, 13.f }; memcpy(m.h.centre, c, 12);
    for(int i = 0; i < np; ++i) { NpcPed e; memset(&e, 0, sizeof(e)); e.uid = 0x1000 + i; e.model = 7 + i; float p[3] = { cx + i, cy, 13.f }; memcpy(e.pos, p, 12); e.vel[0] = 150; e.heading = 64; e.move = 4; e.health = 100; memcpy(&m.peds[i], &e, sizeof(e)); }
    for(int i = 0; i < nc; ++i) { NpcCar e; memset(&e, 0, sizeof(e)); e.uid = 0x2000 + i; e.model = 400 + i; e.driver = 7; float p[3] = { cx, cy + i * 5, 13.f }; memcpy(e.pos, p, 12); e.fwd[1] = 127; e.up[2] = 127; e.health = 850; e.col[0] = 3; memcpy(&m.cars[i], &e, sizeof(e)); }
    uint8_t out[sizeof(PktNpc)]; int n = MP_EncodeNpc(m, out);
    cl[from].Send(out, n);
}

int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-62s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    {
        PktNpc m; memset(&m, 0, sizeof(m)); m.h.type = PKT_NPC; m.h.nPeds = 16; m.h.nCars = 10;
        uint8_t out[sizeof(PktNpc)]; int n = MP_EncodeNpc(m, out);
        printf("full NPC packet: %d bytes (16 people + 10 cars)\n", n);
        CHECK(n < 1000, "a full NPC packet fits one datagram");
        PktNpc d; CHECK(MP_DecodeNpc(out, n, d) && d.h.nPeds == 16 && d.h.nCars == 10, "encode / decode round trip");
        CHECK(!MP_DecodeNpc(out, n - 1, d), "cut-off packet is rejected");
        out[3] = 200; CHECK(!MP_DecodeNpc(out, n, d), "too many entries is rejected");
    }
    MPServer srv; if(!srv.Start(19336, "NPC Test", [](const char*){}, 100)) return 1;
    for(int i = 0; i < 4; ++i) { char nm[8]; snprintf(nm, 8, "P%d", i); cl[i].Connect("127.0.0.1", 19336, nm, 106); }
    Pump(700);
    CHECK(ev[0].id >= 0 && ev[3].id >= 0, "4 clients connected");
    // P0 host at Grove, P1 120 m away, P2 far away, P3 next to P0 but on a mission
    SendPos(0, 2495, -1686); SendPos(1, 2495, -1566); SendPos(2, -2000, 500); SendPos(3, 2490, -1680, true);
    Pump(300);
    SendNpc(0, 5, 3, 2495, -1686); Pump(200);
    CHECK(ev[1].npc == 1 && ev[1].npcPeds == 5 && ev[1].npcCars == 3, "NPCs reach a player 120 m away");
    NpcPed p2; memcpy(&p2, &ev[1].lastNpc.peds[2], sizeof(p2)); float pp[3]; memcpy(pp, p2.pos, 12);
    NpcCar c1; memcpy(&c1, &ev[1].lastNpc.cars[1], sizeof(c1));
    CHECK(p2.uid == 0x1002 && p2.model == 9 && fabsf(pp[0] - 2497.f) < 0.001f && p2.vel[0] == 150 && c1.model == 401 && c1.health == 850 && c1.col[0] == 3, "values arrive exactly");
    CHECK(ev[1].lastNpc.h.id == ev[0].id, "server stamps the real sender id");
    CHECK(ev[2].npc == 0, "far player (> 200 m) doesn't get them");
    CHECK(ev[3].npc == 0, "player on a mission keeps their own world");
    CHECK(ev[0].npc == 0, "host doesn't get its own back");
    SendPos(0, 2495, -1686, true); Pump(200);
    SendNpc(0, 2, 0, 2495, -1686); Pump(200);
    CHECK(ev[1].npc == 1, "host on a mission: NPCs not relayed");
    SendPos(0, 2495, -1686); Pump(200);
    // damage + claim go to the owner only
    PktNpcHit h; memset(&h, 0, sizeof(h)); h.type = PKT_NPC_HIT; h.owner = (uint8_t)ev[0].id; h.kind = 1; h.uid = 0x2001; h.damage = 300; h.from = 77;
    cl[1].Send(&h, sizeof(h)); Pump(200);
    CHECK(ev[0].hits == 1 && ev[0].lastHit.uid == 0x2001 && ev[0].lastHit.damage == 300 && ev[0].lastHit.kind == 1 && ev[0].lastHit.from == ev[1].id, "hit reaches the owner (sender id fixed by the server)");
    CHECK(ev[2].hits == 0 && ev[3].hits == 0 && ev[1].hits == 0, "nobody else gets the hit");
    PktNpcClaim q; memset(&q, 0, sizeof(q)); q.type = PKT_NPC_CLAIM; q.owner = (uint8_t)ev[0].id; q.uid = 0x2002;
    cl[1].Send(&q, sizeof(q)); Pump(200);
    CHECK(ev[0].claims == 1 && ev[0].lastClaim.uid == 0x2002 && ev[0].lastClaim.from == ev[1].id, "claim reaches the owner");
    h.owner = (uint8_t)ev[1].id; cl[1].Send(&h, sizeof(h)); Pump(150);
    CHECK(ev[1].hits == 0, "hit on yourself is dropped");
    printf(pass ? "ALL PASSED\n" : "SOME FAILED\n");
    for(auto& c : cl) c.Disconnect();
    srv.Stop();
    return pass ? 0 : 1;
}
