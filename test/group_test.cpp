// Linux test (2.31): Mission sync groups (3+ players, up to MP_MISSION_GROUP_MAX) + Unsync one player.
//   g++ -O2 -std=c++17 -pthread -I../src group_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o pair_test
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
    void OnPair(const PktPair& a) override
    {
        if(a.action == PAIR_REQUEST) asked = a.other;
        if(a.action == PAIR_LINKED) { partner = a.other; grp |= 1ull << a.other; }
        if(a.action == PAIR_END) { grp &= ~(1ull << a.other); ++ended; if(partner == a.other) partner = -1; }
        if(a.action == PAIR_DENIED) ++denied;
    }
};

#define N 4
static MPClient cl[N]; static Ev ev[N];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < N; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void Pair(int from, uint8_t act, int to) { PktPair q; q.type = PKT_PAIR; q.action = act; q.other = (uint8_t)ev[to].id; cl[from].Send(&q, sizeof(q)); }
static unsigned long long B(int i) { return 1ull << ev[i].id; }
static void Join(int a, int b) { Pair(a, PAIR_REQUEST, b); Pump(200); Pair(b, PAIR_ACCEPT, a); Pump(250); }
int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-58s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    MPServer srv; if(!srv.Start(19335, "Group Test", [](const char*){}, 100)) return 1;
    srv.SetPerms(PERM_MISSION_PAIR);
    for(int i = 0; i < N; ++i) { char n[8]; snprintf(n, 8, "G%d", i); cl[i].Connect("127.0.0.1", 19335, n, 106); }
    Pump(800);
    CHECK(ev[0].id >= 0 && ev[3].id >= 0, "4 clients connected");
    Join(0, 1);
    CHECK(ev[0].grp == B(1) && ev[1].grp == B(0), "two players linked");
    Join(0, 2);
    CHECK(ev[0].grp == (B(1)|B(2)) && ev[1].grp == (B(0)|B(2)) && ev[2].grp == (B(0)|B(1)), "third player joins the same group (3 on a mission)");
    Join(3, 1);   // 3 asks a member; 1 accepts -> 3 joins the group
    CHECK(ev[3].grp == (B(0)|B(1)|B(2)) && ev[2].grp == (B(0)|B(1)|B(3)), "fourth player joins via any member");
    Pair(0, PAIR_END, 2); Pump(250);   // Unsync player 2 only
    CHECK(ev[2].grp == 0 && ev[0].grp == (B(1)|B(3)) && ev[1].grp == (B(0)|B(3)), "Unsync removes only that player");
    Pair(3, PAIR_END, 3); Pump(250);   // leave the group myself
    CHECK(ev[3].grp == 0 && ev[0].grp == B(1) && ev[1].grp == B(0), "Leave Mission sync: the rest stay synced");
    Pair(0, PAIR_END, 1); Pump(250);
    CHECK(ev[0].grp == 0 && ev[1].grp == 0, "unsync of the last other ends the group");
    Join(2, 3); Join(2, 0);
    int before = ev[3].ended; cl[2].Disconnect(); Pump(400);
    CHECK(ev[3].grp == B(0) && ev[0].grp == B(3) && ev[3].ended > before, "member leaving the server: rest stay synced");
    printf(pass ? "ALL PASSED\n" : "SOME FAILED\n");
    for(auto& c : cl) c.Disconnect();
    srv.Stop();
    return pass ? 0 : 1;
}
