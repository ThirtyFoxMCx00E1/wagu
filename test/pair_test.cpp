// Linux test (2.16): server permissions + mission pairs (request / accept / end, host switch off).
//   g++ -O2 -std=c++17 -pthread -I../src pair_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o pair_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int id = -1, perms = -1, partner = -1, asked = -1, denied = 0, ended = 0;
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
        if(a.action == PAIR_LINKED) partner = a.other;
        if(a.action == PAIR_END) { ++ended; if(partner == a.other) partner = -1; }
        if(a.action == PAIR_DENIED) ++denied;
    }
};
static MPClient cl[3]; static Ev ev[3];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < 3; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void Pair(int from, uint8_t act, int to) { PktPair q; q.type = PKT_PAIR; q.action = act; q.other = (uint8_t)ev[to].id; cl[from].Send(&q, sizeof(q)); }
int main()
{
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-58s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    MPServer srv; if(!srv.Start(19334, "Pair Test", [](const char*){}, 100)) return 1;
    for(int i = 0; i < 3; ++i) { char n[8]; snprintf(n, 8, "P%d", i); cl[i].Connect("127.0.0.1", 19334, n, 106); }
    Pump(800);
    CHECK(ev[0].id >= 0 && ev[1].id >= 0 && ev[2].id >= 0, "3 clients connected");
    CHECK(ev[0].perms == PERM_MISSION_PAIR, "joiner gets the server permissions (pairs on)");
    Pair(1, PAIR_ACCEPT, 0); Pump(200);
    CHECK(ev[1].denied == 1 && ev[1].partner < 0, "accepting without a request is refused");
    Pair(0, PAIR_REQUEST, 1); Pump(200);
    CHECK(ev[1].asked == ev[0].id && ev[2].asked < 0, "request reaches only the asked player");
    Pair(1, PAIR_ACCEPT, 0); Pump(200);
    CHECK(ev[0].partner == ev[1].id && ev[1].partner == ev[0].id && ev[2].partner < 0, "accept links both players");
    Pair(0, PAIR_END, 1); Pump(200);
    CHECK(ev[0].partner < 0 && ev[1].partner < 0, "unpair ends it for both");
    Pair(2, PAIR_REQUEST, 0); Pair(0, PAIR_REQUEST, 2); Pump(200);
    CHECK(ev[0].partner == ev[2].id && ev[2].partner == ev[0].id, "asking each other pairs up too");
    srv.SetPerms(0); Pump(200);
    CHECK(ev[1].perms == 0 && ev[0].partner < 0 && ev[2].partner < 0, "host switch off: everyone told, pairs end");
    Pair(0, PAIR_REQUEST, 1); Pump(200);
    CHECK(ev[0].denied >= 1 && ev[1].asked == ev[0].id, "requests refused while off (no new request)");
    srv.SetPerms(PERM_MISSION_PAIR); Pump(200);
    Pair(1, PAIR_REQUEST, 2); Pump(200); Pair(2, PAIR_ACCEPT, 1); Pump(200);
    int before = ev[2].ended; cl[1].Disconnect(); Pump(400);
    CHECK(ev[2].partner < 0 && ev[2].ended > before, "partner leaving ends the pair");
    printf(pass ? "ALL PASSED\n" : "SOME FAILED\n");
    for(auto& c : cl) c.Disconnect();
    srv.Stop();
    return pass ? 0 : 1;
}
