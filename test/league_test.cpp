// 4.13.4 test: San Andreas Online League - teams, kickoff, server ball, touch validation, goal, /stats.
//   AF="-DHAVE_STDINT_H -DAMX_ANSIONLY -DAMX_NODYNALOAD -DAMX_DONT_RELOCATE -DFLOATPOINT -Dstricmp=strcasecmp -DNDEBUG"; g++ -O2 -std=c++17 -pthread -DMP_AUTH_OFF_FOR_TESTS -I../src -I../third_party/mbedtls/include $AF league_test.cpp ../src/net/{client,server,online,lan,authcrypto,samp_import}.cpp ../server/obj/lib{mbedtls,samp,unrar}.a -ldl -lm -o league_test && ./league_test
#include <stdio.h>
#include <string.h>
#include <thread>
#include <chrono>
#include <vector>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    bool ok = false; int rej = 0; PktLgState st{}; bool hasSt = false; PktLgMe me{}; bool hasMe = false; int goals = 0, spawns = 0; int msgs = 0; char last[160] = "";
    void OnConnected(const PktWelcome&) override { ok = true; }
    void OnRejected(uint8_t r) override { rej = r; }
    void OnConnectFailed() override {} void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {} void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {} void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {} void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg& m) override { ++msgs; snprintf(last, sizeof(last), "%s", m.text); }
    void OnSetColor(const PktSetColor&) override {} void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {} void OnStatus(const PktStatus&) override {}
    void OnPerms(const PktPerms&) override {} void OnKicked(const PktKicked&) override {}
    void OnLg(const uint8_t* d, int len) override
    {
        if(len >= (int)sizeof(PktLgState) && d[1] == LGO_STATE) { memcpy(&st, d, sizeof(st)); hasSt = true; }
        if(len >= (int)sizeof(PktLgMe) && d[1] == LGO_ME) { memcpy(&me, d, sizeof(me)); hasMe = true; }
        if(len >= (int)sizeof(PktLgEvent) && d[1] == LGO_EVENT) { const PktLgEvent* e = (const PktLgEvent*)d; if(e->ev == LGE_GOAL) ++goals; if(e->ev == LGE_SPAWN) ++spawns; }
    }
};
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Pump(std::vector<MPClient*> c, std::vector<Ev*> e, int ms)
{ for(int t = 0; t < ms; t += 10) { for(size_t i = 0; i < c.size(); ++i) c[i]->Poll(e[i]); std::this_thread::sleep_for(std::chrono::milliseconds(10)); } }
static void Cmd(MPClient& c, uint8_t cmd, int a, const float* p = nullptr, const float* v = nullptr)
{ PktLgCmd k; memset(&k, 0, sizeof(k)); k.type = PKT_LG; k.op = LGO_CMD; k.cmd = cmd; k.a = (uint8_t)a; if(p) memcpy(k.pos, p, 12); if(v) memcpy(k.vel, v, 12); c.Send(&k, sizeof(k)); }

int main()
{
    // the shared ball model
    LgBall b; LgBallReset(b); b.v[0] = 40; int g = 0; for(int i = 0; i < 400 && !g; ++i) g = LgBallStep(b, 0.025f);
    CHECK(g == 1, "a 40 m/s ball rolls into the +X (orange) goal = Blue scores");
    LgBallReset(b); b.p[1] = 20; b.v[0] = 40; g = 0; for(int i = 0; i < 400 && !g; ++i) g = LgBallStep(b, 0.025f);
    CHECK(g == 0 && b.p[0] <= LG_HALF_L, "beside the goal mouth the end wall bounces it back");
    { LgBall t; LgBallReset(t); float car[3] = { -3.0f, 0, 0.6f }, cv[3] = { 30, 0, 0 }; LgBallHit(t, car, cv);
      printf("   ground shot: v %.1f %.1f %.1f\n", t.v[0], t.v[1], t.v[2]);
      CHECK(t.v[0] > 25 && t.v[2] > 0 && t.v[2] < 14, "a ground hit is a low, hard shot (not a launch)"); }
    CHECK(LgTierOf(350) == 0 && LgTierOf(650) == 2 && LgTierOf(1750) == 7, "RL tiers by MMR");

    remove("/tmp/league_test_stats.txt"); MPServerSetLgFile("/tmp/league_test_stats.txt");
    MPServer srv; srv.SetPersistent(true);
    if(!srv.Start(19934, MP_LG_SERVER_NAME, [](const char* l){ printf("   [srv] %s\n", l); }, 100)) { printf("server failed\n"); return 1; }
    CHECK(srv.IsLgServer(), "the name makes it the league server");
    MPClient a, o; Ev ea, eo; a.SetUid("lgphone0000000001"); o.SetUid("lgphone0000000002");
    a.Connect("127.0.0.1", 19934, "Ace", 0); o.Connect("127.0.0.1", 19934, "Oz", 0);
    Pump({&a, &o}, {&ea, &eo}, 1200);
    CHECK(ea.ok && eo.ok, "two phones joined");
    CHECK(ea.hasSt && ea.st.phase == LGP_WARMUP, "warmup / free play without teams");
    Cmd(a, LGC_TEAM, 2); Pump({&a, &o}, {&ea, &eo}, 1700); Cmd(o, LGC_TEAM, 2); Pump({&a, &o}, {&ea, &eo}, 600);
    CHECK(ea.hasMe && eo.hasMe && ea.me.team <= 1 && eo.me.team <= 1 && ea.me.team != eo.me.team, "auto team: one Blue, one Orange");
    CHECK(ea.st.phase == LGP_KICKOFF && ea.spawns > 0, "the match starts with a kickoff (spawn spots sent)");
    Pump({&a, &o}, {&ea, &eo}, 3300);
    CHECK(ea.st.phase == LGP_PLAY && ea.st.clock <= LG_MATCH_SEC, "GO after 3 s, the clock runs");
    // the Blue player pushes the ball toward +X
    MPClient& blue = ea.me.team == 0 ? a : o; Ev& eb = ea.me.team == 0 ? ea : eo;
    float far[3] = { kLgCenter[0] - 30, kLgCenter[1], kLgCenter[2] + 1 }, v[3] = { 40, 0, 0 };
    Cmd(blue, LGC_TOUCH, 0, far, v); Pump({&a, &o}, {&ea, &eo}, 300);
    CHECK(fabsf(eb.st.ball[0]) < 0.5f, "a touch from 30 m away is refused");
    for(int k = 0; k < 80 && eb.goals == 0; ++k)
    {
        if(eb.st.ball[2] > 4.0f) { Pump({&a, &o}, {&ea, &eo}, 150); continue; }   // the car waits for the ball to come down
        float p[3] = { kLgCenter[0] + eb.st.ball[0] - 2.5f, kLgCenter[1] + eb.st.ball[1], kLgCenter[2] + 0.6f };
        Cmd(blue, LGC_TOUCH, 0, p, v); Pump({&a, &o}, {&ea, &eo}, 250); if(getenv("LGDBG")) printf("   ball %.1f %.1f %.1f v %.1f ph %d\n", eb.st.ball[0], eb.st.ball[1], eb.st.ball[2], eb.st.vel[0], eb.st.phase);
    }
    CHECK(eb.goals >= 1 && eb.st.score[0] >= 1, "Blue drives the ball into the orange goal: GOAL, score 1 : 0");
    PktChat c; memset(&c, 0, sizeof(c)); c.type = PKT_CHAT; strcpy(c.text, "/stats"); blue.Send(&c, sizeof(c)); Pump({&a, &o}, {&ea, &eo}, 400);
    CHECK(strstr(eb.last, "Career") != nullptr, "/stats answers");
    srv.Stop();
    FILE* f = fopen("/tmp/league_test_stats.txt", "r"); CHECK(f != nullptr, "ranks saved on stop"); if(f) fclose(f);
    printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
