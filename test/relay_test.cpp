// Linux test: joins a server found in the internet lobby through the relay, like a phone on mobile data.
//   g++ -O2 -std=c++17 -pthread -I../src relay_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp -o relay_test
//   ./relay_test "<server name>" <port> <player name> <seconds>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <strings.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"
#include "net/online.h"

struct Ev : INetEvents {
    int syncs = 0, joins = 0; bool ok = false;
    void OnConnected(const PktWelcome& w) override { ok = true; printf("WELCOME id=%d server=\"%s\" max=%d\n", w.yourId, w.serverName, w.maxPlayers); }
    void OnRejected(uint8_t r) override { printf("REJECTED %d\n", r); }
    void OnConnectFailed() override { printf("CONNECT FAILED\n"); }
    void OnLostConnection() override { printf("LOST\n"); }
    void OnPlayerJoin(const PktPlayerJoin& p) override { ++joins; printf("PLAYER_JOIN %d %s\n", p.id, p.name); }
    void OnPlayerLeave(const PktPlayerLeave& p) override { printf("PLAYER_LEAVE %d\n", p.id); }
    void OnSync(const PktSync&, const SyncExt*) override { ++syncs; }
    void OnChat(const PktChat& c) override { printf("CHAT %d: %s\n", c.id, c.text); }
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus& s) override { printf("STATUS id %d state %d mission '%s'\n", s.id, s.state, s.mission); }
    void OnWorld(const PktWorld& w) override { printf("WORLD %02d:%02d weather %d mode %d abs %u idx %u\n", w.hour, w.minute, w.weather, w.mode, w.absMinute, (w.absMinute / 60) % 64); }
};

int main(int argc, char** argv)
{
    const char* name = argc > 1 ? argv[1] : "MP LAN Server";
    int port = argc > 2 ? atoi(argv[2]) : 1922;
    const char* me = argc > 3 ? argv[3] : "Tester";
    int secs = argc > 4 ? atoi(argv[4]) : 15;
    Online::Start();
    uint64_t t0 = MP_NowMs();
    bool ready = Online::WaitReady(10000);
    { std::vector<std::string> d; Online::GetDiagnostics(d); for(auto& l : d) printf("  diag: %s\n", l.c_str()); }
    printf("lobby ready=%d broker=%s after %llu ms\n", ready, Online::BrokerName(), (unsigned long long)(MP_NowMs() - t0));
    LanServer found; bool have = false;
    for(auto& s : Online::Snapshot()) {
        printf("  online: \"%s\" port %u %d/%d at %s token %08x\n", s.name, s.port, s.players, s.maxPlayers, s.ip, s.token);
        if(!strcasecmp(s.name, name) && s.port == port) { found = s; have = true; }
    }
    if(!have) { printf("server not found\n"); return 1; }
    uint16_t via = 0;
    if(!Online::OpenProxy(found.ip, found.port, &via)) { printf("proxy failed\n"); return 1; }
    MPClient c; Ev ev;
    c.Connect(found.ip, found.port, me, 106, 0, via);
    uint64_t end = MP_NowMs() + secs * 1000, lastSync = 0; uint32_t seq = 0; bool chatted = false, sentM = false;
    while(MP_NowMs() < end) {
        c.Poll(&ev);
        uint64_t now = MP_NowMs();
        if(c.GetState() == NET_CONNECTED && now - lastSync >= 50) {
            // MP_TEST_MISSION=SWEET1: be "on a mission" for the first half of the run
            const char* tm = getenv("MP_TEST_MISSION");
            bool onM = tm && *tm && now < end - secs * 500;
            PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = ++seq; p.d.health = 100;
            if(onM) p.d.flags |= SYNC_MISSION;
            // MP_TEST_EXT=1: 2.13 raw extension (170-byte syncs), 2: 2.15 compact walking blend tree; players walk in a circle
            const char* te = getenv("MP_TEST_EXT");
            float ang = (float)(now % 20000) / 20000.0f * 6.2832f + (float)(me[strlen(me) - 1] % 10);
            p.d.pos[0] = 2495.0f + cosf(ang) * 20.0f; p.d.pos[1] = -1686.0f + sinf(ang) * 20.0f; p.d.pos[2] = 13.5f;
            if(te && *te)
            {
                SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = (uint32_t)now; e.moveState = 4;
                uint8_t out[sizeof(PktSync) + sizeof(SyncExt) + 64]; memcpy(out, &p, sizeof(p));
#ifdef MP_SYNC_EXT_V3
                int el;
                if(atoi(te) == 1)
                {
                    SyncExtV1 v; memset(&v, 0, sizeof(v)); v.ver = MP_SYNC_EXT_VER; v.time = (uint32_t)now; v.moveState = 4;
                    memcpy(out + sizeof(p), &v, sizeof(v)); el = (int)sizeof(v);
                }
                else
                {
                    // 2: compact with a walking blend tree (3 anims), like a 2.15 phone
                    e.fullAnims = 1; e.animCount = 3;
                    for(int i = 0; i < 3; ++i) { AnimSync g; memset(&g, 0, sizeof(g)); g.group = 54; g.id = (int16_t)i; g.blend = 120; g.speed = 50; g.flags = 2; memcpy(&e.anim[i], &g, sizeof(g)); }
                    el = MP_EncodeSyncExt(e, out + sizeof(p));
                }
#else
                memcpy(out + sizeof(p), &e, sizeof(e)); int el = (int)sizeof(e);   // building against 2.13
#endif
                c.Send(out, (int)sizeof(p) + el);
            }
            else c.Send(&p, sizeof(p));
            lastSync = now;
            static uint64_t lastRep = 0; static int maxPing = 0;
            if(c.GetPing() > maxPing && now > end - secs * 1000 + 5000) maxPing = c.GetPing();
            if(now - lastRep > 3000) { lastRep = now; printf("  t=%llus ping=%d ms (max %d) syncs=%d\n", (unsigned long long)((now - (end - secs * 1000)) / 1000), c.GetPing(), maxPing, ev.syncs); fflush(stdout); }
            if(tm && *tm && onM != sentM) { PktStatus st; memset(&st, 0, sizeof(st)); st.type = PKT_STATUS; st.state = onM ? STATUS_MISSION : 0; if(onM) snprintf(st.mission, sizeof(st.mission), "%s", tm); c.Send(&st, sizeof(st)); sentM = onM; printf("SENT status %d\n", (int)onM); }
            if(!chatted) { PktChat ch; memset(&ch, 0, sizeof(ch)); ch.type = PKT_CHAT; snprintf(ch.text, sizeof(ch.text), "hello from %s", me); c.Send(&ch, sizeof(ch)); chatted = true; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    printf("RESULT rtt=%d connected=%d ping=%d ms syncs_received=%d player_joins=%d\n", Online::BrokerRttMs(), ev.ok, c.GetPing(), ev.syncs, ev.joins);
    c.Disconnect(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    return ev.ok ? 0 : 1;
}
