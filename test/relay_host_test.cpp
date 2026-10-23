// Linux test: HOSTS a server in the internet lobby like a phone does (MPServer + online relay +
// the host's own player on 127.0.0.1). Run relay_test.cpp in other processes to join it through
// the public broker and compare everyone's ping with 1, 2, 5... players.
//   g++ -O2 -std=c++17 -pthread -I../src relay_host_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp -o relay_host_test
//   ./relay_host_test "<server name>" <port> <seconds>
//   MP_TEST_NPC=1: the host's player is the NPC host and sends a full PKT_NPC 5x a second
//   MP_RELAY_DEBUG=1: print the relay's congestion state every 2 s
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"
#include "net/online.h"

struct Ev : INetEvents {
    int syncs = 0; bool ok = false;
    void OnConnected(const PktWelcome& w) override { ok = true; printf("HOST PLAYER WELCOME id=%d\n", w.yourId); }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override { printf("HOST PLAYER CONNECT FAILED\n"); }
    void OnLostConnection() override { printf("HOST PLAYER LOST\n"); }
    void OnPlayerJoin(const PktPlayerJoin& p) override { printf("PLAYER_JOIN %d %s\n", p.id, p.name); fflush(stdout); }
    void OnPlayerLeave(const PktPlayerLeave& p) override { printf("PLAYER_LEAVE %d\n", p.id); fflush(stdout); }
    void OnSync(const PktSync&, const SyncExt*) override { ++syncs; }
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
    const char* name = argc > 1 ? argv[1] : "MP Relay Test";
    int port = argc > 2 ? atoi(argv[2]) : 1950;
    int secs = argc > 3 ? atoi(argv[3]) : 60;
    Online::Start();
    Online::WaitReady(10000);
    printf("broker %s rtt %d ms\n", Online::BrokerName(), Online::BrokerRttMs());
    MPServer srv;
    if(!srv.Start((uint16_t)port, name)) { printf("server start failed\n"); return 1; }
    MPClient c; Ev ev;
    c.Connect("127.0.0.1", (uint16_t)port, "Host", 106);
    uint64_t t0 = MP_NowMs(), end = t0 + secs * 1000ull, lastSync = 0, lastList = 0, lastRep = 0, lastNpc = 0;
    uint32_t seq = 0;
    const bool npc = getenv("MP_TEST_NPC") && atoi(getenv("MP_TEST_NPC"));
    while(MP_NowMs() < end)
    {
        c.Poll(&ev);
        uint64_t now = MP_NowMs();
        if(now - lastList >= 1000) { lastList = now; Online::SetHosted(true, name, (uint16_t)port, srv.GetPlayerCount(), 100, 0); }
        if(c.GetState() == NET_CONNECTED && now - lastSync >= 50)
        {
            lastSync = now;
            PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = ++seq; p.d.health = 100;
            float ang = (float)(now % 20000) / 20000.0f * 6.2832f;
            p.d.pos[0] = 2495.0f + cosf(ang) * 20.0f; p.d.pos[1] = -1686.0f + sinf(ang) * 20.0f; p.d.pos[2] = 13.5f;
            SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)now; e.moveState = 4;
            e.fullAnims = 1; e.animCount = 3;
            for(int i = 0; i < 3; ++i) { AnimSync g; memset(&g, 0, sizeof(g)); g.group = 54; g.id = (int16_t)i; g.blend = 120; g.speed = 50; g.flags = 2; memcpy(&e.anim[i], &g, sizeof(g)); }
            uint8_t out[sizeof(PktSync) + sizeof(SyncExt) + 64]; memcpy(out, &p, sizeof(p));
            int el = MP_EncodeSyncExt(e, out + sizeof(p));
            c.Send(out, (int)sizeof(p) + el);
        }
        if(npc && c.GetState() == NET_CONNECTED && now - lastNpc >= 200)
        {
            lastNpc = now;
            static PktNpc m; memset(&m, 0, sizeof(m));
            m.h.type = PKT_NPC; m.h.id = 0; m.h.nPeds = MP_NPC_PEDS_MAX; m.h.nCars = MP_NPC_CARS_MAX; m.h.seq = (uint8_t)(now / 200);
            float cc[3] = { 2495.0f, -1686.0f, 13.5f }; memcpy(m.h.centre, cc, sizeof(cc));
            for(int i = 0; i < MP_NPC_PEDS_MAX; ++i) { m.peds[i].uid = 100 + i; m.peds[i].model = 7; float q[3] = { 2495.0f + i, -1686.0f, 13.5f }; memcpy(m.peds[i].pos, q, 12); m.peds[i].health = 100; }
            for(int i = 0; i < MP_NPC_CARS_MAX; ++i) { m.cars[i].uid = 500 + i; m.cars[i].model = 400 + i; float q[3] = { 2495.0f, -1686.0f + i * 5, 13.5f }; memcpy(m.cars[i].pos, q, 12); m.cars[i].health = 1000; }
            uint8_t buf[1400]; int n = MP_EncodeNpc(m, buf);
            if(n > 0) c.Send(buf, n);
        }
        if(now - lastRep >= 3000)
        {
            lastRep = now;
            printf("  host t=%llus players=%d hostping=%d ms congest=%d brokerRtt=%d\n", (unsigned long long)((now - t0) / 1000), srv.GetPlayerCount(), c.GetPing(), Online::CongestionMs(), Online::BrokerRttMs());
            fflush(stdout);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    Online::SetHosted(false, "", 0, 0, 0, 0);
    c.Disconnect(true);
    srv.Stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    return 0;
}
