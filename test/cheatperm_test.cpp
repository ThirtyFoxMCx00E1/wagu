// 3.00 test: Cheat Menu server permission (PERM_CHEAT_MENU) - custom servers only.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include cheatperm_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o cheatperm_test
//   ./cheatperm_test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"
#include "net/admin.h"

struct Ev : INetEvents {
    bool ok = false; int rej = 0; uint8_t perms = 0; int kicked = 0; int msgs = 0; char lastMsg[128] = "";
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t r) override { rej = r; }
    void OnConnectFailed() override {} void OnLostConnection() override {}
    bool sawAdmin = false;   // another player received the tagged name -> their mod draws the admin effects
    void OnPlayerJoin(const PktPlayerJoin& j) override { char n[MP_NAME_LEN + 1] = {0}; memcpy(n, j.name, MP_NAME_LEN); if(!strcmp(n, "[ADMIN] SENJI")) sawAdmin = true; }
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg& m) override { ++msgs; snprintf(lastMsg, sizeof(lastMsg), "%s", m.text); }
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnPerms(const PktPerms& p) override { perms = p.perms; }
    void OnKicked(const PktKicked&) override { ++kicked; }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }

int main()
{
    // default server: the Cheat Menu permission is stripped, whatever the owner sets
    MPServer def; def.SetPersistent(true);
    if(!def.Start(19341, MP_DEFAULT_SERVER_NAME, nullptr, 100)) { printf("server failed\n"); return 1; }
    def.SetPerms(PERM_MISSION_PAIR | PERM_CHEAT_MENU);
    MPClient a; Ev ea; a.SetUid("cmphone0000000001"); a.Connect("127.0.0.1", 19341, "Carl", 0);
    Pump({&a}, {&ea}, 1000);
    CHECK(ea.ok && !(ea.perms & PERM_CHEAT_MENU), "default server never allows Cheat Menu");

    // custom server, switch on -> players get the bit (they join directly)
    MPServer cus; cus.SetPersistent(true);
    if(!cus.Start(19342, "Senji Freeroam", nullptr, 100)) { printf("server failed\n"); return 1; }
    cus.SetPerms(PERM_MISSION_PAIR | PERM_CHEAT_MENU);
    MPClient b; Ev eb; b.SetUid("cmphone0000000002"); b.Connect("127.0.0.1", 19342, "Ryder", 0);
    Pump({&b}, {&eb}, 1000);
    CHECK(eb.ok && (eb.perms & PERM_CHEAT_MENU), "custom server with Cheat Menu on: players get the permission");

    // host switches it off while playing -> everybody is told at once
    cus.SetPerms(PERM_MISSION_PAIR);
    Pump({&b}, {&eb}, 500);
    CHECK(!(eb.perms & PERM_CHEAT_MENU), "switch turned off: the permission is gone right away (client leaves + message)");

    // custom server, switch off (default) -> no bit
    MPServer off; off.SetPersistent(true);
    if(!off.Start(19343, "Grove Only", nullptr, 100)) { printf("server failed\n"); return 1; }
    off.SetPerms(PERM_MISSION_PAIR);
    MPClient c; Ev ec; c.SetUid("cmphone0000000003"); c.Connect("127.0.0.1", 19343, "Sweet", 0);
    Pump({&c}, {&ec}, 1000);
    CHECK(ec.ok && !(ec.perms & PERM_CHEAT_MENU), "custom server with Cheat Menu off: no permission (\"doesn't allow\" message)");
    a.Disconnect(); b.Disconnect(); c.Disconnect(); def.Stop(); cus.Stop(); off.Stop();
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
