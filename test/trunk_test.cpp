// 3.40.1 test: the reserved moderator name Trunk + moderator.txt -> "[MOD] Trunk" (name tag / chat / kill feed / permissions).
//   g++ -O2 -std=c++17 -pthread -DMP_AUTH_OFF_FOR_TESTS -I../src -I../third_party/mbedtls/include trunk_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/authcrypto.cpp ../server/obj/libmbedtls.a -o trunk_test
//   ./trunk_test <MP moderator key>
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"
#include "net/admin.h"

struct Ev : INetEvents {
    bool ok = false; int rej = 0; int kicked = 0; uint8_t perms = 0; bool sawMod = false;
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t r) override { rej = r; }
    void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin& j) override { char n[MP_NAME_LEN + 1] = {0}; memcpy(n, j.name, MP_NAME_LEN); if(!strcmp(n, "[MOD] Trunk")) sawMod = true; }
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnPerms(const PktPerms& p) override { perms = p.perms; }
    void OnKicked(const PktKicked&) override { ++kicked; }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }

int main(int argc, char** argv)
{
    const char* key = argc > 1 ? argv[1] : "";
    CHECK(MP_IsModReservedName("Trunk") && MP_IsModReservedName("TRUNK") && MP_IsModReservedName("[MOD] trunk") && MP_IsModReservedName("T_r_u_n_k") && MP_IsModReservedName("7runk"), "Trunk look-alikes are reserved");
    CHECK(!MP_IsModReservedName("Trunks") && !MP_IsModReservedName("Truck") && !MP_IsModReservedName("Juan"), "other names are free");
    CHECK(MP_ModMasterOk(key), "the MP moderator key is valid");

    MPServer srv; srv.SetPersistent(true);
    if(!srv.Start(19355, MP_DEFAULT_SERVER_NAME, [](const char* l){ printf("   [srv] %s\n", l); }, 100)) { printf("server failed\n"); return 1; }

    MPClient fake; Ev ef; fake.SetUid("fakephone00000011"); fake.Connect("127.0.0.1", 19355, "Trunk", 0);
    Pump({&fake}, {&ef}, 800);
    CHECK(!ef.ok && ef.rej == REJECT_NAME_RESERVED, "Trunk without moderator.txt is refused");
    MPClient fake2; Ev ef2; fake2.SetUid("fakephone00000012"); fake2.SetModKey("wrongkey1234"); fake2.Connect("127.0.0.1", 19355, "tRuNk", 0);
    Pump({&fake2}, {&ef2}, 800);
    CHECK(!ef2.ok && ef2.rej == REJECT_NAME_RESERVED, "Trunk with a wrong key is refused");

    MPClient bob; Ev eb; bob.SetUid("bobphone000000011"); bob.Connect("127.0.0.1", 19355, "Bob", 0);
    MPClient mod; Ev em; mod.SetUid("trunkphone0000001"); mod.SetModKey(key); mod.Connect("127.0.0.1", 19355, "trunk", 0);
    Pump({&bob, &mod}, {&eb, &em}, 1200);
    CHECK(eb.ok && em.ok, "Bob and the real Trunk (moderator.txt) joined the default server");
    std::vector<MPServer::PlayerView> pv; srv.GetPlayers(pv);
    bool tag = false; for(auto& p : pv) { printf("   player %d '%s'\n", p.id, p.name); if(!strcmp(p.name, "[MOD] Trunk")) tag = true; }
    CHECK(tag, "shows as [MOD] Trunk (name tag / chat / kill feed use this name)");
    CHECK(eb.sawMod, "other players receive '[MOD] Trunk'");

    PktAdminCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_ADMIN_CMD; c.cmd = ADMIN_CHEATS; c.value = 1; c.target = MP_INVALID_ID;
    bob.Send(&c, sizeof(c)); Pump({&bob, &mod}, {&eb, &em}, 400);
    CHECK(!(eb.perms & PERM_NO_CHEATS), "Bob (no rank) can't use moderator commands");
    mod.Send(&c, sizeof(c)); Pump({&bob, &mod}, {&eb, &em}, 400);
    CHECK(eb.perms & PERM_NO_CHEATS, "[MOD] Trunk has the moderator permissions on the default server");
    int bobId = -1; srv.GetPlayers(pv); for(auto& p : pv) if(!strcmp(p.name, "Bob")) bobId = p.id;
    c.cmd = ADMIN_KICK; c.target = (uint8_t)bobId; snprintf(c.reason, sizeof(c.reason), "test"); mod.Send(&c, sizeof(c)); Pump({&bob, &mod}, {&eb, &em}, 500);
    CHECK(eb.kicked == 1 || bob.GetState() == NET_DISCONNECTED, "[MOD] Trunk kicked Bob");

    // the moderator's phone comes back on the same address (game restarted) with moderator.txt: still [MOD]
    MPClient mod2; Ev em2; mod2.SetUid("trunkphone0000001"); mod2.SetModKey(key); mod2.Connect("127.0.0.1", 19355, "Trunk", 0);
    Pump({&mod, &mod2}, {&em, &em2}, 1500);
    srv.GetPlayers(pv); int tags = 0; for(auto& p : pv) { printf("   player %d '%s'\n", p.id, p.name); if(strstr(p.name, "Trunk")) ++tags; }
    CHECK(em2.ok && tags == 1, "a second Trunk join with the key replaces the old slot (no Trunk_2 ghost)");
    srv.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
