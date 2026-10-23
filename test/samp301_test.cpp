// 3.01 test: SA-MP features over the network - turf state, MOTD dialog, banner textdraw, race /
// derby events and class teams relayed to the other players.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include samp301_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/online.cpp ../src/net/lan.cpp ../server/obj/libmbedtls.a -o samp301_test
//   ./samp301_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"
#include "net/turf.h"

struct Ev : INetEvents {
    bool ok = false; int turf = 0, dialogs = 0, tds = 0, events = 0, teams = 0;
    PktTurf lastTurf{}; PktDialog lastDlg{}; PktTextDraw lastTd{}; PktEvent lastEv{}; PktTeam lastTeam{};
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t) override {} void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {} void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnTurf(const PktTurf& t) override { ++turf; lastTurf = t; }
    void OnDialog(const PktDialog& d) override { ++dialogs; lastDlg = d; }
    void OnTextDraw(const PktTextDraw& t) override { ++tds; lastTd = t; }
    void OnEvent(const PktEvent& e) override { ++events; lastEv = e; }
    void OnTeam(const PktTeam& t) override { ++teams; lastTeam = t; }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }

int main()
{
    remove("/tmp/samp301_turf.txt");
    MPServer s; s.SetPersistent(true);
    s.SetTurfFile("/tmp/samp301_turf.txt");
    s.SetMotd("{FFE14D}Welcome", "{FFFFFF}Type /race, /derby, /class.");
    s.SetBanner("~y~my.server ~w~discord");
    if(!s.Start(19361, "SAMP 3.01 Test", nullptr, 100)) { printf("server failed\n"); return 1; }
    s.SetModes(GM_ALL);   // 3.02: custom servers start with no gamemodes
    MPClient a, b; Ev ea, eb;
    a.SetUid("samp301phone00001"); b.SetUid("samp301phone00002");
    a.Connect("127.0.0.1", 19361, "Carl", 0);
    Pump({&a}, {&ea}, 800);
    b.Connect("127.0.0.1", 19361, "Ryder", 0);
    Pump({&a, &b}, {&ea, &eb}, 1500);
    CHECK(ea.ok && eb.ok, "both players joined");
    CHECK(eb.turf > 0 && eb.lastTurf.count == MP_TURF_COUNT, "turf state sent on join (all zones)");
    bool own = true; for(int i = 0; i < MP_TURF_COUNT; ++i) if(eb.lastTurf.owner[i] != g_MPTurf[i].owner) own = false;
    CHECK(own, "zones start with their default gangs");
    CHECK(eb.dialogs > 0 && strstr(eb.lastDlg.info, "/derby") != nullptr, "MOTD dialog sent on join");
    CHECK(eb.tds > 0 && eb.lastTd.show && strstr(eb.lastTd.text, "my.server"), "banner textdraw sent on join");

    // race lobby: A joins race 3 -> B sees it (with A's id), not A itself
    PktEvent e{}; e.type = PKT_EVENT; e.id = 0; e.kind = EVK_RACE; e.idx = 2; e.state = EVS_LOBBY; e.ms = 12000;
    int aBefore = ea.events;
    a.Send(&e, sizeof(e));
    Pump({&a, &b}, {&ea, &eb}, 500);
    CHECK(eb.events > 0 && eb.lastEv.kind == EVK_RACE && eb.lastEv.idx == 2 && eb.lastEv.state == EVS_LOBBY, "race lobby relayed to the other player");
    CHECK(ea.events == aBefore, "sender doesn't get its own event back");
    e.state = EVS_FINISH; e.cp = 7; e.ms = 83456;
    a.Send(&e, sizeof(e));
    Pump({&a, &b}, {&ea, &eb}, 500);
    CHECK(eb.lastEv.state == EVS_FINISH && eb.lastEv.ms == 83456, "race finish (time) relayed");

    // class team: A picks Ballas skin 102 -> B is told
    PktTeam t{}; t.type = PKT_TEAM; t.team = TEAM_BALLAS; t.skin = 102;
    a.Send(&t, sizeof(t));
    Pump({&a, &b}, {&ea, &eb}, 500);
    CHECK(eb.teams > 0 && eb.lastTeam.team == TEAM_BALLAS && eb.lastTeam.skin == 102, "class team relayed");
    int tb = eb.teams;
    a.Send(&t, sizeof(t));   // same team again (sent every 5 s) -> not relayed again
    Pump({&a, &b}, {&ea, &eb}, 500);
    CHECK(eb.teams == tb, "unchanged team is not relayed again");

    // turf: on change, every 2 s during a war, and every 15 s anyway (late packets / lost ones)
    int tc = eb.turf;
    Pump({&a, &b}, {&ea, &eb}, 16000);
    CHECK(eb.turf > tc, "turf state re-sent within 15 s when nothing changes");
    s.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
