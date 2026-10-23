// Linux test (3.02): mission peds / cars go to the whole mission group (14 players), not to others.
//   g++ -O2 -std=c++17 -pthread -I../src mission_npc_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o pair_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    unsigned long long grp = 0; int id = -1, perms = -1, partner = -1, asked = -1, denied = 0, ended = 0;
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
    void OnPerms(const PktPerms& p) override { perms = p.perms; }
    int mnpc = 0, mfrom = -1, mpeds = 0, mcars = 0; unsigned muid = 0;
    void OnMissionNpc(const PktNpc& m) override { ++mnpc; mfrom = m.h.id; mpeds = m.h.nPeds; mcars = m.h.nCars; muid = m.peds[0].uid; }
    void OnPair(const PktPair& a) override
    {
        if(a.action == PAIR_REQUEST) asked = a.other;
        if(a.action == PAIR_LINKED) { partner = a.other; grp |= 1ull << a.other; }
        if(a.action == PAIR_END) { grp &= ~(1ull << a.other); ++ended; if(partner == a.other) partner = -1; }
        if(a.action == PAIR_DENIED) ++denied;
    }
};

#define N 15
static MPClient cl[N]; static Ev ev[N];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < N; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void Pair(int from, uint8_t act, int to) { PktPair q; q.type = PKT_PAIR; q.action = act; q.other = (uint8_t)ev[to].id; cl[from].Send(&q, sizeof(q)); }
static void Join(int a, int b) { Pair(a, PAIR_REQUEST, b); Pump(150); Pair(b, PAIR_ACCEPT, a); Pump(200); }
static void SendMission(int from)
{
    static PktNpc m; memset(&m, 0, sizeof(m));
    m.h.type = PKT_MISSION_NPC; m.h.id = (uint8_t)ev[from].id; m.h.nPeds = 2; m.h.nCars = 1;
    m.peds[0].uid = 0xABCD; m.peds[0].model = 270; m.peds[1].uid = 0xABCE; m.peds[1].model = 105; m.cars[0].uid = 0x1234; m.cars[0].model = 492;
    uint8_t out[sizeof(PktNpc)]; cl[from].Send(out, MP_EncodeNpc(m, out));
}
int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-62s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    MPServer srv; if(!srv.Start(19336, "Mission NPC Test", [](const char*){}, 100)) return 1;
    srv.SetPerms(PERM_MISSION_PAIR);
    for(int i = 0; i < N; ++i) { char n[8]; snprintf(n, 8, "M%d", i); cl[i].Connect("127.0.0.1", 19336, n, 106); Pump(60); }
    Pump(800);
    bool all = true; for(int i = 0; i < N; ++i) all &= ev[i].id >= 0;
    CHECK(all, "15 clients connected");
    SendMission(0); Pump(300);
    int got = 0; for(int i = 1; i < N; ++i) got += ev[i].mnpc;
    CHECK(got == 0, "not in a group: mission peds go to nobody");
    for(int i = 1; i < 14; ++i) Join(i, 0);
    CHECK(__builtin_popcountll(ev[0].grp) == 13, "14 players in one mission sync group");
    SendMission(0); Pump(400);
    int ok = 0; for(int i = 1; i < 14; ++i) ok += ev[i].mnpc == 1 && ev[i].mfrom == ev[0].id && ev[i].mpeds == 2 && ev[i].mcars == 1 && ev[i].muid == 0xABCD;
    CHECK(ok == 13, "all 13 partners get the mission's 2 peds + 1 car");
    CHECK(ev[14].mnpc == 0, "the player outside the group gets nothing");
    CHECK(ev[0].mnpc == 0, "the mission owner doesn't get its own back");
    SendMission(7); Pump(400);
    CHECK(ev[0].mnpc == 1 && ev[0].mfrom == ev[7].id && ev[3].mnpc == 2, "any member's mission is shared with the rest");
    srv.SetPerms(0); Pump(200);
    int b3 = ev[3].mnpc; SendMission(0); Pump(300);
    CHECK(ev[3].mnpc == b3, "Mission sync permission off: nothing relayed");
    printf(pass ? "ALL PASSED\n" : "SOME FAILED\n");
    for(auto& c : cl) c.Disconnect();
    srv.Stop();
    return pass ? 0 : 1;
}
