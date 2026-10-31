// 3.39 test: custom server owner (settings / kick), rules packets, Home Kit create + setup + sell + buy,
// kick = 4:29 wait (not a ban), featured server settings = staff only.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include v339_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o v339_test && ./v339_test
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    bool ok = false; int rej = 0; int kicked = 0; int kickWait = 0;
    int rulesHead = -1, rulesLines = 0; char rulesTitle[100] = "";
    bool haveSet = false; PktSrvSettings set = {};
    int homeMsgs = 0, homeOk = 0; uint16_t lastHome = 0; char lastHomeMsg[128] = ""; int cash = 0;
    char lastSrvMsg[128] = "";
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t r) override { rej = r; }
    void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {} void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg& m) override { snprintf(lastSrvMsg, sizeof(lastSrvMsg), "%s", m.text); }
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnKicked(const PktKicked&) override { ++kicked; }
    void OnKickWait(int s) override { kickWait = s; }
    void OnRules(const PktRules& r) override { if(r.idx == 0) { rulesHead = r.on; snprintf(rulesTitle, sizeof(rulesTitle), "%s", r.text); } else if(r.text[0]) ++rulesLines; }
    void OnSrvSettings(const PktSrvSettings& s) override { haveSet = true; set = s; }
    void OnHome(const uint8_t* d, int len) override
    {
        if(len >= (int)sizeof(PktHomeMsg) && d[0] == PKT_HOME_MSG) { PktHomeMsg m; memcpy(&m, d, sizeof(m)); ++homeMsgs; if(m.ok) ++homeOk; if(m.id) lastHome = m.id; cash += m.cash; snprintf(lastHomeMsg, sizeof(lastHomeMsg), "%s", m.text); }
    }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
static void Cmd(MPClient& c, uint8_t cmd, uint16_t id, int val, const char* text, float x = 1000, float y = 1000, float z = 20, float h = 0, uint8_t w = 0, uint8_t hh = 0)
{
    PktHomeCmd p; memset(&p, 0, sizeof(p)); p.type = PKT_HOME_CMD; p.cmd = cmd; p.id = id; p.val = val; p.w = w; p.h = hh;
    p.pos[0] = x; p.pos[1] = y; p.pos[2] = z; p.pos[3] = h; if(text) snprintf(p.text, sizeof(p.text), "%s", text);
    c.Send(&p, sizeof(p));
}

int main()
{
    unlink("/tmp/v339_homes.txt");
    MPServer srv; srv.SetPersistent(true);
    srv.SetHomeFile("/tmp/v339_homes.txt");
    srv.SetOwnerUid("ownerphone0000001");
    const char* rules[10] = { "No cheating", "Be nice", "", "", "", "", "", "", "", "" };
    srv.SetRules(true, "MY RULES", rules);
    if(!srv.Start(19339, "Test Custom", [](const char* l){ printf("   [srv] %s\n", l); }, 50)) { printf("server failed\n"); return 1; }
    srv.SetModes(GM_HOME);

    MPClient own, bob; Ev eo, eb;
    own.SetUid("ownerphone0000001"); bob.SetUid("bobphone000000001");
    own.Connect("127.0.0.1", 19339, "Owner", 0); bob.Connect("127.0.0.1", 19339, "Bob", 0);
    Pump({&own, &bob}, {&eo, &eb}, 1500);
    CHECK(eo.ok && eb.ok, "owner + Bob joined");
    printf("   rules head=%d title=%s lines=%d\n", eb.rulesHead, eb.rulesTitle, eb.rulesLines);
    CHECK(eb.rulesHead == 1 && !strcmp(eb.rulesTitle, "MY RULES") && eb.rulesLines == 2, "custom rules arrive (title + 2 lines)");
    CHECK(eo.haveSet && eo.set.can == 1, "owner can manage the custom server");
    CHECK(eb.haveSet && eb.set.can == 0, "Bob can only view the settings");

    // Bob tries to change settings -> ignored
    PktSrvSettings s = eb.set; s.op = 1; s.modes = GM_ALL; bob.Send(&s, sizeof(s)); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(srv.GetModes() == GM_HOME, "Bob can't change the gamemodes");
    s = eo.set; s.op = 1; s.modes = GM_HOME | GM_DRIFT; s.rulesOn = 1; own.Send(&s, sizeof(s)); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(srv.GetModes() == (GM_HOME | GM_DRIFT), "owner turned Drift on");
    CHECK(eb.set.modes == (GM_HOME | GM_DRIFT), "Bob's settings page updates");

    // Home Kit
    Cmd(own, HC_CREATE, 0, 0, "OwnerVilla"); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(eo.homeOk >= 1 && eo.lastHome, "/create house OwnerVilla");
    const uint16_t hid = eo.lastHome;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Cmd(own, HC_SETUP, hid, 0, nullptr, 996, 996, 19, 0, 16, 16); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(strstr(eo.lastHomeMsg, "16x16") != nullptr, "/setup 16x16 slot saved");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Cmd(own, HC_SETUP, hid, 0, nullptr, 996, 996, 19, 0, 200, 16); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(strstr(eo.lastHomeMsg, "up to") != nullptr, "an over-limit slot is refused (red)");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Cmd(bob, HC_SETUP, hid, 0, nullptr, 996, 996, 19, 0, 4, 4); Pump({&own, &bob}, {&eo, &eb}, 400);
    CHECK(strstr(eb.lastHomeMsg, "Only the owner") != nullptr, "Bob can't set up the owner's house");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Cmd(own, HC_SELL, hid, 5000, nullptr); Pump({&own, &bob}, {&eo, &eb}, 400);
    Cmd(bob, HC_BUY, hid, 5000, nullptr); Pump({&own, &bob}, {&eo, &eb}, 500);
    CHECK(strstr(eb.lastHomeMsg, "You bought") != nullptr && eb.cash == -5000, "/buyhouse: Bob paid $5000");
    CHECK(eo.cash == 5000, "the owner got $5000");
    FILE* f = fopen("/tmp/v339_homes.txt", "r"); CHECK(f != nullptr, "houses saved to the home file"); if(f) fclose(f);

    // owner kicks Bob -> 4:29 wait, then the same uid is refused
    std::vector<MPServer::PlayerView> pv; srv.GetPlayers(pv); int bobId = -1; for(auto& p : pv) if(!strcmp(p.name, "Bob")) bobId = p.id;
    PktAdminCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_ADMIN_CMD; c.cmd = ADMIN_KICK; c.target = (uint8_t)bobId; snprintf(c.reason, sizeof(c.reason), "spam");
    own.Send(&c, sizeof(c)); Pump({&own, &bob}, {&eo, &eb}, 600);
    printf("   kicked=%d wait=%d rej=%d\n", eb.kicked, eb.kickWait, eb.rej);
    CHECK(eb.kicked >= 1 && eb.kickWait == MP_KICK_WAIT_SEC, "owner kicked Bob: wait 4:29");
    CHECK(strstr(eo.lastSrvMsg, "got kicked by") != nullptr, "everyone sees '[SERVER]: ... got kicked by ...'");
    printf("   broadcast: %s\n", eo.lastSrvMsg);
    MPClient bob2; Ev eb2; bob2.SetUid("bobphone000000001"); bob2.Connect("127.0.0.1", 19339, "Bob", 0);
    Pump({&own, &bob2}, {&eo, &eb2}, 1200);
    printf("   rejoin ok=%d rej=%d wait=%d\n", eb2.ok, eb2.rej, eb2.kickWait);
    CHECK(!eb2.ok && eb2.rej == REJECT_KICKED, "Bob can't rejoin during the 4:29 wait");
    CHECK(eb2.kickWait > 200 && eb2.kickWait <= MP_KICK_WAIT_SEC, "the rejoin refusal tells the seconds left");
    std::vector<MPServer::BanEntry> bans; srv.GetBans(bans);
    CHECK(bans.empty(), "a kick is not a ban");

    own.Disconnect(); bob2.Disconnect(); Pump({&own}, {&eo}, 300);
    srv.Stop();

    // a featured server (default): settings are staff-only, gamemodes fixed
    MPServer def; def.SetPersistent(true); def.SetOwnerUid("ownerphone0000001");
    def.Start(19340, MP_DEFAULT_SERVER_NAME, [](const char*){}, 50); def.SetModes(GM_ALL);
    MPClient o2; Ev e2; o2.SetUid("ownerphone0000001"); o2.Connect("127.0.0.1", 19340, "Owner", 0);
    Pump({&o2}, {&e2}, 1200);
    CHECK(e2.ok && e2.haveSet && e2.set.can == 0, "default server: the hosting phone is NOT the owner (only admin / mods)");
    s = e2.set; s.op = 1; s.modes = 0; o2.Send(&s, sizeof(s)); Pump({&o2}, {&e2}, 400);
    CHECK(def.GetModes() == GM_ALL, "default server keeps every gamemode");
    o2.Disconnect(); Pump({&o2}, {&e2}, 300); def.Stop();
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
