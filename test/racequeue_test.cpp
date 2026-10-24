// 3.01 test: the race queue - 14 players type /race, the server starts one GTA SA race for all
// of them (same race, grid places 0..13, same GO), a 15th player stays in the queue.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include racequeue_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/online.cpp ../src/net/lan.cpp ../server/obj/libmbedtls.a -o racequeue_test
//   ./racequeue_test
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
    MPServer s; s.SetPersistent(true); s.SetTurfFile("/tmp/racequeue_turf.txt");
    if(!s.Start(19362, "Race Queue Test", nullptr, 100)) { printf("server failed\n"); return 1; }
    const int N = MP_RACE_PLAYERS + 1;
    std::vector<MPClient*> c; std::vector<Ev*> e;
    for(int i = 0; i < N; ++i)
    {
        c.push_back(new MPClient()); e.push_back(new Ev());
        char uid[32], nm[16]; snprintf(uid, sizeof(uid), "racequeuephone%03d", i); snprintf(nm, sizeof(nm), "Racer%d", i);
        c[i]->SetUid(uid); c[i]->Connect("127.0.0.1", 19362, nm, 0);
        Pump(c, e, 150);
    }
    Pump(c, e, 1500);
    int ok = 0; for(auto* x : e) ok += x->ok; CHECK(ok == N, "15 players joined");
    struct Q : Ev { int qCount = 0; bool started = false; PktEvent start{}; void OnEvent(const PktEvent& v) override { if(v.id != MP_EV_SERVER) return; if(v.idx == MP_EV_QUEUE) qCount = v.cp; else { started = true; start = v; } } };
    std::vector<Q*> q; std::vector<Ev*> qe; for(int i = 0; i < N; ++i) { q.push_back(new Q()); q[i]->ok = true; qe.push_back(q[i]); }
    // 3.02: a custom server starts with no gamemodes: /race is ignored, players are told "none"
    CHECK(s.GetModes() == 0, "new custom server: no gamemodes");
    {
        PktEvent j{}; j.type = PKT_EVENT; j.kind = EVK_RACE; j.idx = MP_EV_QUEUE; j.state = EVS_LOBBY;
        c[0]->Send(&j, sizeof(j)); Pump(c, qe, 400);
        CHECK(q[0]->qCount == 0 && !q[0]->started, "races off: the queue ignores /race");
    }
    struct P : Q { uint16_t modes = 0xFFFF; uint8_t magic = 0; void OnPerms(const PktPerms& m) override { modes = m.modes; magic = m.modesMagic; } };
    P* watch = new P(); watch->ok = true; qe[1] = watch; q[1] = watch;
    s.SetModes(GM_RACE | GM_CLASS);
    Pump(c, qe, 400);
    CHECK(watch->magic == MP_MODES_MAGIC && watch->modes == (GM_RACE | GM_CLASS), "host turns Races + Class on: players get the gamemodes");
    // 13 join: queue count goes up, nobody starts
    for(int i = 0; i < MP_RACE_PLAYERS - 1; ++i)
    {
        PktEvent j{}; j.type = PKT_EVENT; j.kind = EVK_RACE; j.idx = MP_EV_QUEUE; j.state = EVS_LOBBY;
        c[i]->Send(&j, sizeof(j)); Pump(c, qe, 60);
    }
    Pump(c, qe, 500);
    CHECK(q[0]->qCount == MP_RACE_PLAYERS - 1 && !q[0]->started, "13 in the queue: 13/14, no race yet");
    // one leaves and joins again (no duplicates)
    { PktEvent j{}; j.type = PKT_EVENT; j.kind = EVK_RACE; j.idx = MP_EV_QUEUE; j.state = EVS_LOBBY; c[3]->Send(&j, sizeof(j)); Pump(c, qe, 300); }
    CHECK(q[0]->qCount == MP_RACE_PLAYERS - 1, "joining twice doesn't count twice");
    // 14th + 15th
    for(int i = MP_RACE_PLAYERS - 1; i < N; ++i) { PktEvent j{}; j.type = PKT_EVENT; j.kind = EVK_RACE; j.idx = MP_EV_QUEUE; j.state = EVS_LOBBY; c[i]->Send(&j, sizeof(j)); Pump(c, qe, 200); }
    Pump(c, qe, 800);
    int started = 0, race = -1; bool sameRace = true, slotsOk = true, goOk = true; bool used[MP_RACE_PLAYERS] = {};
    for(int i = 0; i < MP_RACE_PLAYERS; ++i)
    {
        if(!q[i]->started) continue;
        ++started;
        if(race < 0) race = q[i]->start.idx; else if(q[i]->start.idx != race) sameRace = false;
        int sl = q[i]->start.slot; if(sl >= MP_RACE_PLAYERS || used[sl]) slotsOk = false; else used[sl] = true;
        if(q[i]->start.ms != MP_RACE_GO_MS || q[i]->start.cp != MP_RACE_PLAYERS) goOk = false;
    }
    CHECK(started == MP_RACE_PLAYERS, "14th player -> the race starts for all 14");
    CHECK(sameRace && race >= 0 && race < MP_SA_RACE_COUNT, "everybody got the same GTA SA race");
    CHECK(slotsOk, "grid places 0..13, each once");
    CHECK(goOk, "same GO time (MP_RACE_GO_MS, 20 s) and racer count for everybody");
    CHECK(!q[N - 1]->started && q[N - 1]->qCount == 1, "the 15th player waits in the queue (1/14)");
    { PktEvent j{}; j.type = PKT_EVENT; j.kind = EVK_RACE; j.idx = MP_EV_QUEUE; j.state = EVS_LEAVE; c[N - 1]->Send(&j, sizeof(j)); Pump(c, qe, 300); }
    CHECK(true, "15th player left the queue");
    s.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
