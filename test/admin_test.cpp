// 2.37 test: reserved name SENJI + [ADMIN] tag + admin commands + cheat switch reset when empty.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include admin_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o admin_test
//   ./admin_test <admin key>
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

int main(int argc, char** argv)
{
    const char* key = argc > 1 ? argv[1] : "";
    CHECK(MP_IsReservedName("SENJI") && MP_IsReservedName(" senji") && MP_IsReservedName("S_E_N_J_I") && MP_IsReservedName("SENJ1") && MP_IsReservedName("[ADMIN] SENJI"), "reserved name variants");
    CHECK(!MP_IsReservedName("SENJIRO") && !MP_IsReservedName("Senji2") && !MP_IsReservedName("Bob"), "other names allowed");
    CHECK(MP_AdminKeyOk(key) && !MP_AdminKeyOk("wrong") && !MP_AdminKeyOk(""), "admin key hash");

    MPServer srv; srv.SetPersistent(true);
    if(!srv.Start(19333, MP_DEFAULT_SERVER_NAME, [](const char* l){ printf("   [srv] %s\n", l); }, 100)) { printf("server failed\n"); return 1; }
    srv.SetPerms(PERM_MISSION_PAIR);

    MPClient fake; Ev ef; fake.SetUid("fakephone00000001"); fake.Connect("127.0.0.1", 19333, "SENJI", 0);
    Pump({&fake}, {&ef}, 800);
    CHECK(!ef.ok && ef.rej == REJECT_NAME_RESERVED, "SENJI without admin.txt key is refused");

    MPClient fake2; Ev ef2; fake2.SetAdminKey("guess"); fake2.Connect("127.0.0.1", 19333, "senj1", 0);
    Pump({&fake2}, {&ef2}, 800);
    CHECK(!ef2.ok && ef2.rej == REJECT_NAME_RESERVED, "look-alike name + wrong key is refused");

    MPClient bob; Ev eb; bob.SetUid("bobphone000000001"); bob.Connect("127.0.0.1", 19333, "[ADMIN] Bob", 0);
    MPClient adm; Ev ea; adm.SetUid("senjiphone0000001"); adm.SetAdminKey(key); adm.Connect("127.0.0.1", 19333, "SENJI", 0);
    Pump({&bob, &adm}, {&eb, &ea}, 1200);
    CHECK(eb.ok && ea.ok, "Bob and the real SENJI joined");
    std::vector<MPServer::PlayerView> pv; srv.GetPlayers(pv);
    bool tag = false, bobClean = false;
    for(auto& p : pv) { printf("   player %d '%s'\n", p.id, p.name); if(!strcmp(p.name, "[ADMIN] SENJI")) tag = true; if(!strcmp(p.name, "Bob")) bobClean = true; }
    CHECK(tag, "SENJI shows as [ADMIN] SENJI on the default server");
    CHECK(bobClean, "a fake [ADMIN] tag is stripped (Bob)");
    CHECK(eb.sawAdmin, "other players receive '[ADMIN] SENJI' (their 2.37 mod shows the name tag / chat / kill feed effects)");

    // Bob tries an admin command -> ignored
    PktAdminCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_ADMIN_CMD; c.cmd = ADMIN_CHEATS; c.value = 1; c.target = MP_INVALID_ID;
    bob.Send(&c, sizeof(c)); Pump({&bob, &adm}, {&eb, &ea}, 400);
    CHECK(!(eb.perms & PERM_NO_CHEATS), "non-admin can't switch cheats off");
    adm.Send(&c, sizeof(c)); Pump({&bob, &adm}, {&eb, &ea}, 400);
    CHECK(eb.perms & PERM_NO_CHEATS, "admin switched cheats off for everyone");

    int bobId = -1; srv.GetPlayers(pv); for(auto& p : pv) if(!strcmp(p.name, "Bob")) bobId = p.id;
    c.cmd = ADMIN_MUTE; c.target = (uint8_t)bobId; adm.Send(&c, sizeof(c)); Pump({&bob, &adm}, {&eb, &ea}, 300);
    PktChat ch; memset(&ch, 0, sizeof(ch)); ch.type = PKT_CHAT; snprintf(ch.text, sizeof(ch.text), "hi"); int before = eb.msgs;
    bob.Send(&ch, sizeof(ch)); Pump({&bob, &adm}, {&eb, &ea}, 300);
    CHECK(eb.msgs > before && strstr(eb.lastMsg, "muted"), "muted Bob gets 'You are muted'");
    c.cmd = ADMIN_KICK; snprintf(c.reason, sizeof(c.reason), "test"); adm.Send(&c, sizeof(c)); Pump({&bob, &adm}, {&eb, &ea}, 500);
    CHECK(eb.kicked == 1 || eb.rej == REJECT_KICKED || bob.GetState() == NET_DISCONNECTED, "admin kicked Bob");

    adm.Disconnect(); Pump({&adm}, {&ea}, 1500);
    CHECK(srv.IsRunning() && srv.GetPlayerCount() == 0, "everybody left: the 24/7 server stays up");
    MPClient p3; Ev e3; p3.Connect("127.0.0.1", 19333, "Carl", 0); Pump({&p3}, {&e3}, 800);
    CHECK(e3.ok && !(e3.perms & PERM_NO_CHEATS), "first player after empty: not host, cheat switch reset");
    p3.Disconnect(); srv.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails != 0;
}
