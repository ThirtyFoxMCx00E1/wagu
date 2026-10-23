// Linux test (2.11): 100 players on one server. Half stand together at Grove Street, half are
// spread across the map. Checks that all 100 get a seat and that far players are thinned out.
//   g++ -O2 -std=c++17 -pthread -I../src load_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o load_test
//   ./load_test [players] [seconds]
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int syncs = 0, joins = 0, bad = 0; bool ok = false, rejected = false;
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t) override { rejected = true; }
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override { ++joins; }
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync& s, const SyncExt*) override {
        ++syncs;
        float x; memcpy(&x, (const char*)&s + offsetof(PktSync, d) + offsetof(SyncData, pos), 4);   // unaligned-safe
        if(x != x) ++bad;
    }
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
};

int main(int argc, char** argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 100;
    int secs = argc > 2 ? atoi(argv[2]) : 6;
    MPServer srv; srv.SetPersistent(true);
    if(!srv.Start(19222, "Load Test", [](const char*){}, MP_MAX_PLAYERS)) { printf("server failed\n"); return 1; }
    std::vector<std::unique_ptr<MPClient>> cl(n); std::vector<Ev> ev(n);
    for(int i = 0; i < n; ++i) { cl[i].reset(new MPClient()); char nm[24]; snprintf(nm, sizeof(nm), "P%d", i); cl[i]->Connect("127.0.0.1", 19222, nm, 106); }
    uint64_t end = MP_NowMs() + secs * 1000, last = 0; uint32_t seq = 0;
    while(MP_NowMs() < end) {
        uint64_t now = MP_NowMs();
        for(int i = 0; i < n; ++i) cl[i]->Poll(&ev[i]);
        if(now - last >= 100) {   // 10 Hz, like a full server in 2.11
            last = now; ++seq;
            for(int i = 0; i < n; ++i) if(cl[i]->GetState() == NET_CONNECTED) {
                PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = seq; p.d.health = 100;
                float x = i < n / 2 ? 2495.0f + i : -2500.0f + i * 60.0f, y = i < n / 2 ? -1686.0f : 500.0f * (i % 7);
                p.d.pos[0] = x; p.d.pos[1] = y; p.d.pos[2] = 13.5f;
                cl[i]->Send(&p, sizeof(p));
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    int conn = 0, rej = 0; long near = 0, far = 0;
    for(int i = 0; i < n; ++i) { conn += ev[i].ok; rej += ev[i].rejected; (i < n / 2 ? near : far) += ev[i].syncs; }
    printf("server players=%d  connected=%d/%d rejected=%d\n", srv.GetPlayerCount(), conn, n, rej);
    printf("avg syncs received: grouped player %ld, spread-out player %ld (over %d s)\n", near / (n / 2), far / (n - n / 2), secs);
    for(int i = 0; i < n; ++i) cl[i]->Disconnect(true);
    srv.Stop();
    return conn == n ? 0 : 1;
}
