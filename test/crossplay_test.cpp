// 4.12: open.mp cross-play bridge (MPServer::Bridge*) - a ghost PC player and a real phone client.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include crossplay_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp ../src/net/authcrypto.cpp ../src/net/samp_import.cpp ../server/obj/*.a -ldl -o crossplay_test
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int syncs = 0, joins = 0, bad = 0, lastSyncId = -1, chatFrom = -1; float lastX = 0; std::vector<std::string> chats; std::string joinName; bool ok = false, rejected = false;
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t) override { rejected = true; }
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin& j) override { ++joins; joinName = j.name; }
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync& s, const SyncExt*) override {
        ++syncs; lastSyncId = s.id;
        float x; memcpy(&x, (const char*)&s + offsetof(PktSync, d) + offsetof(SyncData, pos), 4);   // unaligned-safe
        if(x != x) ++bad; lastX = x;
    }
    void OnChat(const PktChat& c) override { chats.push_back(c.text); chatFrom = c.id; }
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
};


static int fails = 0;
#define CHECK(c, msg) do { if(c) printf("ok   %s\n", msg); else { printf("FAIL %s\n", msg); ++fails; } } while(0)

int main()
{
    MPServer srv; srv.SetPersistent(true); srv.BridgeEnable(true);
    if(!srv.Start(19781, "XP Test", [](const char*){}, MP_MAX_PLAYERS)) { printf("server failed\n"); return 1; }
    MPClient cl; Ev ev; cl.Connect("127.0.0.1", 19781, "Phone1", 106);
    auto pump = [&](int ms) { uint64_t e = MP_NowMs() + ms; while(MP_NowMs() < e) { cl.Poll(&ev); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } };
    pump(1500);
    CHECK(cl.GetState() == NET_CONNECTED, "phone client connected");
    int g = srv.BridgeAdd("CJ_PC", 0xFF0000FF, 0);
    CHECK(g >= 0, "ghost PC slot added");
    pump(300);
    CHECK(ev.joinName == "CJ_PC (PC)", "phone sees the PC player join as \"CJ_PC (PC)\"");
    SyncData d; memset(&d, 0, sizeof(d)); d.pos[0] = 2495.f; d.pos[1] = -1686.f; d.pos[2] = 13.5f; d.health = 100; d.skin = 0;
    for(int i = 0; i < 5; ++i) { d.pos[0] += 1.f; srv.BridgeSync(g, d); pump(60); }
    CHECK(ev.syncs > 0 && ev.lastSyncId == g, "phone gets the PC player's sync");
    CHECK(ev.lastX > 2495.f, "position arrives");
    srv.BridgeChat(g, "hello from PC");
    pump(200);
    CHECK(!ev.chats.empty() && ev.chats.back() == "hello from PC" && ev.chatFrom == g, "PC chat reaches the phone");
    PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = 1; p.d.pos[0] = 100.f; p.d.pos[1] = 200.f; p.d.pos[2] = 10.f; p.d.health = 90; p.d.skin = 106;
    cl.Send(&p, sizeof(p)); pump(200);
    std::vector<MPServer::BridgePeer> peers; srv.BridgePeers(peers);
    CHECK(peers.size() == 1 && std::string(peers[0].name) == "Phone1", "bridge lists only the phone (not the ghost)");
    CHECK(!peers.empty() && peers[0].hasSync && peers[0].d.pos[0] == 100.f && peers[0].d.health == 90, "phone sync reaches the bridge");
    PktChat c; memset(&c, 0, sizeof(c)); c.type = PKT_CHAT; strcpy(c.text, "hi PC"); cl.Send(&c, sizeof(c)); pump(200);
    std::vector<std::pair<int, std::string>> chat; srv.BridgeTakeChat(chat);
    CHECK(chat.size() == 1 && chat[0].second == "hi PC", "phone chat reaches the bridge");
    std::this_thread::sleep_for(std::chrono::milliseconds(MP_TIMEOUT_MS + 1500));   // the ghost must not time out
    cl.Poll(&ev);
    int n = srv.GetPlayerCount();
    srv.BridgeRemove(g); pump(300);
    CHECK(srv.GetPlayerCount() == n - 1, "ghost stays past the timeout and leaves on BridgeRemove");
    srv.Stop();
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
