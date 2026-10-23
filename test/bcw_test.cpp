// 3.04 test: Brief Case War - a whole match on the real server core.
// 20 players join "Brief Case War", pick Gustav / Schelapz (10 each, the 21st is refused as
// "full"), the queue starts the leader roll, the match goes live, a Schelapz player stands in the
// Gustav circle and steals the briefcase, gets killed (it drops), a Gustav player returns it, a
// second steal is carried home = victory, scoreboard (2 packets), map vote, back to the queue.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include bcw_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../src/net/online.cpp ../src/net/lan.cpp ../server/obj/libmbedtls.a -o bcw_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"
#include "net/bcw.h"

struct Ev : INetEvents {
    bool ok = false; int rejected = -1, id = -1; PktPerms perms{}; bool gotPerms = false;
    PktBcwState st{}; int states = 0; PktBcwRoll roll{}; int rolls = 0;
    PktBcwScore score[2]{}; int scores = 0; std::vector<PktBcwEvent> evs; int damages = 0; char lastMsg[128] = {0};
    void OnConnected(const PktWelcome& w) override { ok = true; id = w.yourId; }
    void OnRejected(uint8_t r) override { rejected = r; } void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {} void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override { ++damages; }
    void OnServerMessage(const PktServerMsg& m) override { MP_CopyStr(lastMsg, m.text, sizeof(lastMsg)); }
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnPerms(const PktPerms& p) override { perms = p; gotPerms = true; }
    void OnBcw(const uint8_t* d, int len) override
    {
        switch(d[0])
        {
        case PKT_BCW_STATE: if(len >= (int)sizeof(PktBcwState)) { memcpy(&st, d, sizeof(st)); ++states; } break;
        case PKT_BCW_ROLL:  if(len >= (int)sizeof(PktBcwRoll)) { memcpy(&roll, d, sizeof(roll)); ++rolls; } break;
        case PKT_BCW_SCORE: { PktBcwScore s; memset(&s, 0, sizeof(s)); memcpy(&s, d, len < (int)sizeof(s) ? len : sizeof(s)); if(s.part < 2) score[s.part] = s; ++scores; } break;
        case PKT_BCW_EVENT: if(len >= (int)sizeof(PktBcwEvent)) { PktBcwEvent e; memcpy(&e, d, sizeof(e)); evs.push_back(e); } break;
        }
    }
    bool HasEv(uint8_t ev) const { for(auto& e : evs) if(e.ev == ev) return true; return false; }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static std::vector<MPClient*> C; static std::vector<Ev*> E;
static void Pump(int ms) { for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < C.size(); ++i) C[i]->Poll(E[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
static void Sync(int i, const float* p, uint8_t flags = 0)
{
    PktSync s; memset(&s, 0, sizeof(s)); s.type = PKT_SYNC; s.id = (uint8_t)E[i]->id; s.seq = (uint32_t)(MP_NowMs() & 0x7FFFFFFF);
    s.d.pos[0] = p[0]; s.d.pos[1] = p[1]; s.d.pos[2] = p[2]; s.d.health = 100; s.d.flags = flags; s.d.skin = 0;
    C[i]->Send(&s, sizeof(s));
}
static void Cmd(int i, uint8_t cmd, uint8_t arg) { PktBcwCmd c; c.type = PKT_BCW_CMD; c.cmd = cmd; c.arg = arg; C[i]->Send(&c, sizeof(c)); }
// keep everybody "present" (synced) somewhere harmless
static void HoldAll(const float far[3], int except = -1, int except2 = -1) { for(size_t i = 0; i < C.size(); ++i) if((int)i != except && (int)i != except2 && E[i]->ok) Sync((int)i, far); }

int main()
{
    srand(7);
    MPServer s; s.SetPersistent(true); s.SetTurfFile("/tmp/bcw_turf.txt");
    if(!s.Start(19430, MP_BCW_SERVER_NAME, nullptr, 100)) { printf("server failed\n"); return 1; }
    CHECK(s.IsBcwServer(), "\"Brief Case War\" is the Brief Case War server");
    CHECK(s.GetMaxPlayers() == 20, "Brief Case War: max players forced to 20 (asked for 100)");
    CHECK(s.GetModes() == 0, "no other gamemodes on Brief Case War");
    CHECK((s.GetPerms() & PERM_NO_CHEATS) && !(s.GetPerms() & PERM_CHEAT_MENU), "cheats off, CheatMenu never allowed");
    s.SetPerms(PERM_CHEAT_MENU | PERM_MISSION_PAIR);
    CHECK((s.GetPerms() & PERM_NO_CHEATS) && !(s.GetPerms() & (PERM_CHEAT_MENU | PERM_MISSION_PAIR)), "owner can't switch cheats / CheatMenu / mission sync on");

    const int N = 21;
    for(int i = 0; i < N; ++i)
    {
        C.push_back(new MPClient()); E.push_back(new Ev());
        char uid[32], nm[16]; snprintf(uid, sizeof(uid), "bcwphone%03d", i); snprintf(nm, sizeof(nm), "War%d", i);
        C[i]->SetUid(uid); C[i]->Connect("127.0.0.1", 19430, nm, 0);
        Pump(120);
    }
    Pump(1500);
    int ok = 0; for(int i = 0; i < 20; ++i) ok += E[i]->ok;
    CHECK(ok == 20, "20 players joined");
    CHECK(!E[20]->ok && E[20]->rejected == REJECT_FULL, "the 21st player is refused: server full (20/20)");
    CHECK(E[0]->states > 0 && E[0]->st.state == BCW_QUEUE && E[0]->st.minPlayers == 20, "joiners get the queue state (needs 20)");
    CHECK(E[0]->gotPerms && (E[0]->perms.perms & PERM_NO_CHEATS), "players are told: cheats off");
    C.pop_back(); E.pop_back();

    const float far[3] = { 300.0f, 2200.0f, 20.0f };
    HoldAll(far); Pump(300);
    // team picks: 0..9 Gustav, 10..19 Schelapz, one Schelapz tries Gustav when it's full first
    for(int i = 0; i < 10; ++i) { Cmd(i, BCWC_TEAM, BCW_T_GUSTAV); Pump(40); }
    Pump(300);
    CHECK(E[0]->st.count[0] == 10 && E[0]->st.state == BCW_QUEUE, "10 in Gustav Team, still queueing (10/20)");
    Cmd(10, BCWC_TEAM, BCW_T_GUSTAV); Pump(300);
    CHECK(E[10]->st.team[E[10]->id] == 0 && strstr(E[10]->lastMsg, "full"), "Gustav Team full: 11th Gustav pick refused");
    for(int i = 10; i < 19; ++i) { Cmd(i, BCWC_TEAM, BCW_T_SCHELAPZ); Pump(40); }
    Pump(300);
    CHECK(E[0]->st.state == BCW_QUEUE && E[0]->st.count[1] == 9, "19/20: no match yet");
    { float a[3] = { 1, 2, 3 }; PktDamage d; d.type = PKT_DAMAGE; d.from = 0; d.target = (uint8_t)E[10]->id; d.weapon = 24; d.amount = 30; int before = E[10]->damages; C[0]->Send(&d, sizeof(d)); Pump(200); CHECK(E[10]->damages == before, "no damage while queueing"); (void)a; }
    Cmd(19, BCWC_TEAM, BCW_T_SCHELAPZ); Pump(500);
    CHECK(E[0]->st.state == BCW_ROLL && E[0]->rolls == 1, "20/20: the leader roll starts");
    const PktBcwRoll& r = E[5]->roll;
    bool rollOk = r.n[0] == 10 && r.n[1] == 10 && r.leader[0] != MP_INVALID_ID && r.leader[1] != MP_INVALID_ID;
    bool lt0 = false, lt1 = false; for(int k = 0; k < 10; ++k) { if(r.ids[0][k] == r.leader[0]) lt0 = true; if(r.ids[1][k] == r.leader[1]) lt1 = true; }
    CHECK(rollOk && lt0 && lt1, "roll: 10 names per team, each leader is in its own team");
    int firstLeaders[2] = { r.leader[0], r.leader[1] };
    for(int t = 0; t < 8; ++t) { HoldAll(far); Pump(1000); }
    CHECK(E[0]->st.state == BCW_LIVE && E[0]->HasEv(BCWE_MATCH_START), "after the roll the match is live");

    // teammates can't hurt each other, enemies can (after spawn protection)
    { PktDamage d; d.type = PKT_DAMAGE; d.from = 0; d.target = (uint8_t)E[1]->id; d.weapon = 24; d.amount = 30; int b = E[1]->damages; C[0]->Send(&d, sizeof(d)); Pump(200); CHECK(E[1]->damages == b, "friendly fire off"); }
    const BcwMap& m = g_BcwMaps[E[0]->st.map];
    // Schelapz player 10 stands in the Gustav circle (base 1) on foot for 5 s
    int thief = 10, thiefId = E[thief]->id;
    for(int t = 0; t < 30; ++t) { HoldAll(far, thief); Sync(thief, m.spawn[0]); Pump(100); }
    CHECK(E[0]->st.cases[0].stealer == thiefId && E[0]->st.cases[0].progress > 0 && E[0]->st.cases[0].state == CASE_HOME, "thief in the Gustav circle: steal bar filling");
    for(int t = 0; t < 30; ++t) { HoldAll(far, thief); Sync(thief, m.spawn[0]); Pump(100); }
    CHECK(E[0]->st.cases[0].state == CASE_CARRIED && E[0]->st.cases[0].carrier == thiefId && E[0]->HasEv(BCWE_STOLEN), "5 s later: Gustav briefcase stolen");
    // in a car you can't steal: Gustav 0 drives into Schelapz circle - nothing
    for(int t = 0; t < 12; ++t) { HoldAll(far, 0, thief); Sync(0, m.spawn[1], SYNC_IN_CAR | SYNC_DRIVER); Sync(thief, far); Pump(100); }
    CHECK(E[0]->st.cases[1].stealer == MP_INVALID_ID, "in a car the steal bar doesn't start");
    // spawn protection is over: Gustav 0 hits the thief, then the thief dies -> briefcase drops
    { PktDamage d; d.type = PKT_DAMAGE; d.from = 0; d.target = (uint8_t)thiefId; d.weapon = 31; d.amount = 40; int b = E[thief]->damages; C[0]->Send(&d, sizeof(d)); Pump(200); CHECK(E[thief]->damages == b + 1, "enemy damage goes through"); }
    float dropAt[3] = { far[0] + 5, far[1], far[2] };
    Sync(thief, dropAt); Pump(150);
    { PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)thiefId; d.killer = (uint8_t)E[0]->id; d.weapon = 31; C[thief]->Send(&d, sizeof(d)); Pump(400); }
    CHECK(E[0]->st.cases[0].state == CASE_DROPPED && fabsf(E[0]->st.cases[0].pos[0] - dropAt[0]) < 0.5f && E[0]->HasEv(BCWE_DROPPED), "carrier killed: briefcase dropped where he died");
    // a Gustav player touches it -> back home
    float far2[3] = { far[0] + 40, far[1], far[2] };
    for(int t = 0; t < 4; ++t) { HoldAll(far2, 1); Sync(1, dropAt); Pump(100); }
    CHECK(E[0]->st.cases[0].state == CASE_HOME && E[0]->HasEv(BCWE_RETURNED), "Gustav player touched it: briefcase returned");
    // Gustav player 2 steals the Schelapz briefcase and brings it home = victory
    int hero = 2;
    for(int t = 0; t < 60; ++t) { HoldAll(far2, hero); Sync(hero, m.spawn[1]); Pump(100); }
    CHECK(E[0]->st.cases[1].state == CASE_CARRIED && E[0]->st.cases[1].carrier == E[hero]->id, "Gustav hero carries the Schelapz briefcase");
    // both stolen: Schelapz 11 steals the Gustav briefcase -> hero can't win at home
    for(int t = 0; t < 60; ++t) { HoldAll(far2, hero, 11); Sync(hero, far2); Sync(11, m.spawn[0]); Pump(100); }
    CHECK(E[0]->st.cases[0].state == CASE_CARRIED, "both briefcases stolen");
    for(int t = 0; t < 10; ++t) { HoldAll(far2, hero, 11); Sync(hero, m.spawn[0]); Sync(11, far); Pump(100); }
    CHECK(E[0]->st.state == BCW_LIVE, "both stolen: carrying it home is NOT a victory");
    // hero kills the Schelapz carrier: Gustav briefcase drops, hero's team returns it, hero wins
    { PktDeath d; d.type = PKT_DEATH; d.victim = (uint8_t)E[11]->id; d.killer = (uint8_t)E[hero]->id; d.weapon = 31; C[11]->Send(&d, sizeof(d)); Pump(300); }
    for(int t = 0; t < 4; ++t) { HoldAll(far2, 3, hero); Sync(3, E[0]->st.cases[0].pos); Sync(hero, far2); Pump(100); }
    CHECK(E[0]->st.cases[0].state == CASE_HOME, "carrier killed, Gustav briefcase back home");
    for(int t = 0; t < 5; ++t) { HoldAll(far2, hero); Sync(hero, m.spawn[0]); Pump(100); }
    Pump(300);
    CHECK(E[0]->HasEv(BCWE_VICTORY) && E[0]->st.state == BCW_COOLDOWN, "hero brings it home: Gustav Team victory -> cooldown");
    CHECK(E[0]->st.cases[0].state == CASE_HOME && E[0]->st.cases[1].state == CASE_HOME && E[0]->st.cases[1].carrier == MP_INVALID_ID, "cooldown: briefcases reset");
    CHECK(E[7]->scores == 0, "no scoreboard during the cooldown");
    for(int t = 0; t < 26; ++t) { HoldAll(far); Pump(1000); }
    CHECK(E[0]->st.state == BCW_RESULTS, "after the 24 s + 20 ticks cooldown: scoreboard");
    CHECK(E[7]->scores == 2 && E[7]->score[0].winner == BCW_T_GUSTAV && E[7]->score[0].n == 10 && E[7]->score[1].n == 10, "scoreboard: 2 packets, 10 + 10 rows, winner Gustav");
    int heroCaps = 0, k0 = 0;
    for(int k = 0; k < E[7]->score[0].n; ++k) { auto& row = E[7]->score[0].rows[k]; if(row.id == E[hero]->id) heroCaps = row.caps; if(row.id == E[0]->id) k0 = row.kills; }
    CHECK(heroCaps == 1 && k0 == 1, "stats: hero 1 capture, player 0 1 kill");
    // vote: 12 vote map 1 (San Fierro), 3 try a locked map
    for(int i = 0; i < 12; ++i) Cmd(i, BCWC_VOTE, 1);
    for(int i = 12; i < 15; ++i) Cmd(i, BCWC_VOTE, 3);
    Pump(400);
    CHECK(E[0]->st.votes[1] == 12 && E[0]->st.votes[3] == 0, "votes counted; locked maps can't be voted");
    for(int t = 0; t < 48; ++t) { HoldAll(far); Pump(1000); }
    CHECK(E[0]->st.state == BCW_QUEUE && E[0]->st.map == 1 && E[0]->st.count[0] == 0 && E[0]->st.count[1] == 0, "after the vote: San Fierro Bay, teams reset, back to the queue");
    // next match: leaders are different players
    for(int i = 0; i < 10; ++i) Cmd(i, BCWC_TEAM, BCW_T_GUSTAV);
    for(int i = 10; i < 20; ++i) Cmd(i, BCWC_TEAM, BCW_T_SCHELAPZ);
    Pump(800);
    CHECK(E[0]->rolls == 2 && E[0]->roll.leader[0] != firstLeaders[0] && E[0]->roll.leader[1] != firstLeaders[1], "second match: new leaders (never the last ones)");
    // a team empties mid-match -> the other team wins
    for(int t = 0; t < 8; ++t) { HoldAll(far); Pump(1000); }
    CHECK(E[0]->st.state == BCW_LIVE, "second match live");
    for(int i = 10; i < 20; ++i) C[i]->Disconnect();
    Pump(800);
    CHECK(E[0]->st.state == BCW_COOLDOWN, "Schelapz Team left: Gustav wins by default (cooldown)");
    for(int t = 0; t < 26; ++t) { Pump(1000); }
    CHECK(E[0]->st.state == BCW_RESULTS && E[0]->score[0].winner == BCW_T_GUSTAV, "then the scoreboard, winner Gustav");

    for(auto* c : C) c->Disconnect();
    s.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
