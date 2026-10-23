// 3.11: voice relay (3.11 phones only, 60 m range, muted = no voice), Discord message every
// 45 min 20 s (never on join), direct UDP join + fallback to the relay port.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include v311_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o v311_test
#include "net/server.h"
#include "net/client.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <thread>
#include <chrono>
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static sockaddr_in Addr(int port) { sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(0x7f000001); return a; }
static void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
static int Join(int port, uint32_t build, const char* name, const char* uid, int& id)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    timeval tv{0, 200000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in a = Addr(port);
    PktJoin5 j; memset(&j, 0, sizeof(j));
    j.j4.j3.j2.j.type = PKT_JOIN; j.j4.j3.j2.j.proto = MP_PROTO_VERSION; strcpy(j.j4.j3.j2.j.name, name); strcpy(j.j4.j3.uid, uid); j.build = build;
    sendto(s, &j, sizeof(j), 0, (sockaddr*)&a, sizeof(a));
    uint8_t buf[2048]; id = -1;
    for(int k = 0; k < 20; ++k) { int r = recv(s, buf, sizeof(buf), 0); if(r <= 0) break; if(buf[0] == PKT_WELCOME) { id = ((PktWelcome*)buf)->yourId; break; } if(buf[0] == PKT_REJECT) break; }
    return s;
}
static void SendTo(int s, int port, const void* d, int n) { sockaddr_in a = Addr(port); sendto(s, d, n, 0, (sockaddr*)&a, sizeof(a)); }
static void SendPos(int s, int port, float x, float y)
{
    PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.seq = 1; p.d.pos[0] = x; p.d.pos[1] = y; p.d.pos[2] = 10; p.d.health = 100;
    SendTo(s, port, &p, sizeof(p));
}
static void Drain(int s) { uint8_t b[2048]; timeval tv{0, 1000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); while(recv(s, b, sizeof(b), 0) > 0) {} tv = {0, 200000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); }
static int CountVoice(int s, int ms, int* fromId = nullptr, int* lenOut = nullptr)
{
    int n = 0; uint8_t buf[2048];
    auto t0 = std::chrono::steady_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < ms)
    {
        int r = recv(s, buf, sizeof(buf), 0);
        if(r >= (int)sizeof(PktVoiceHdr) && buf[0] == PKT_VOICE) { PktVoiceHdr h; memcpy(&h, buf, sizeof(h)); if(fromId) *fromId = h.id; if(lenOut) *lenOut = h.len; ++n; }
    }
    return n;
}
static int CountAd(int s, int ms)
{
    int n = 0; uint8_t buf[2048];
    auto t0 = std::chrono::steady_clock::now();
    while(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() < ms)
    {
        int r = recv(s, buf, sizeof(buf), 0);
        if(r >= (int)sizeof(PktServerMsg) && buf[0] == PKT_SERVER_MSG) { PktServerMsg m; memcpy(&m, buf, sizeof(m)); if(strstr(m.text, "discord.gg/ZqgXKYhNmv")) ++n; }
    }
    return n;
}
static void Voice(int s, int port, uint16_t seq, int len = 60)
{
    uint8_t pkt[sizeof(PktVoiceHdr) + 400]; memset(pkt, 0x5A, sizeof(pkt));
    PktVoiceHdr h; h.type = PKT_VOICE; h.id = 0; h.seq = seq; h.flags = 0; h.pad = 0; h.len = (uint16_t)len;
    memcpy(pkt, &h, sizeof(h));
    SendTo(s, port, pkt, (int)sizeof(h) + len);
}

struct Ev : INetEvents
{
    bool connected = false, failed = false;
    void OnConnected(const PktWelcome&) override { connected = true; }
    void OnRejected(uint8_t) override { failed = true; }
    void OnConnectFailed() override { failed = true; }
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {}
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt*) override {}
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnWorld(const PktWorld&) override {}
    void OnStatus(const PktStatus&) override {}
    int voices = 0;
    void OnVoice(int, uint16_t, uint8_t, const uint8_t*, int) override { ++voices; }
};
static bool RunClient(MPClient& c, Ev& ev, int ms)
{
    for(int k = 0; k < ms / 10 && !ev.connected && !ev.failed; ++k) { c.Poll(&ev); Sleep(10); }
    return ev.connected;
}

int main()
{
    const int P = 19811;
    {
        MPServer sv; sv.Start(P, MP_DEFAULT_SERVER_NAME, nullptr, MP_MAX_PLAYERS, 0);
        int ia, ib, ic, id;
        int a = Join(P, MP_BUILD, "Alice", "uida", ia);
        int b = Join(P, MP_BUILD, "Bob", "uidb", ib);
        int c = Join(P, 30406, "Carl", "uidc", ic);   // 3.04.6: never gets voice packets
        int d = Join(P, MP_BUILD, "Dave", "uidd", id);
        CHECK(ia >= 0 && ib >= 0 && ic >= 0 && id >= 0, "4 phones joined (3 x 3.11, 1 x 3.04.6)");
        SendPos(a, P, 0, 0); SendPos(b, P, 20, 0); SendPos(c, P, 5, 0); SendPos(d, P, 500, 0);   // Dave is 500 m away
        Sleep(150); Drain(a); Drain(b); Drain(c); Drain(d);
        for(int k = 0; k < 10; ++k) Voice(a, P, (uint16_t)k);
        int from = -1, len = 0;
        int nb = CountVoice(b, 400, &from, &len);
        int nc = CountVoice(c, 200), nd = CountVoice(d, 200), na = CountVoice(a, 100);
        CHECK(nb == 10, "voice reaches the 3.11 player 20 m away (all 10 frames)");
        CHECK(from == ia && len == 60, "voice frame carries the sender id + full Opus payload");
        CHECK(nc == 0, "3.04.6 phone gets no voice packets");
        CHECK(nd == 0, "player 500 m away gets no voice (60 m range)");
        CHECK(na == 0, "the talker doesn't get his own voice back");
        // rate limit
        for(int k = 0; k < 200; ++k) Voice(a, P, (uint16_t)(100 + k));
        int flood = CountVoice(b, 500);
        CHECK(flood <= 61, "voice flood is capped (60 frames/s)");
        // oversize frame is dropped
        Sleep(1100); Drain(b);
        { uint8_t big[sizeof(PktVoiceHdr) + 500]; memset(big, 1, sizeof(big)); PktVoiceHdr h{}; h.type = PKT_VOICE; h.len = 500; memcpy(big, &h, sizeof(h)); SendTo(a, P, big, sizeof(big)); }
        CHECK(CountVoice(b, 200) == 0, "oversize voice frame dropped");
        close(a); close(b); close(c); close(d);
        sv.Stop();
    }
    {   // Discord message: once per interval, not on join
        MPServer sv; sv.SetAdInterval(2000); sv.Start(P + 1, MP_DEFAULT_SERVER_NAME, nullptr, MP_MAX_PLAYERS, 0);
        auto t0 = std::chrono::steady_clock::now();
        auto At = [&]() { return (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(); };
        int ia, ib, ic;
        int a = Join(P + 1, MP_BUILD, "Alice", "uida", ia);
        Sleep(300);
        int b = Join(P + 1, MP_BUILD, "Bob", "uidb", ib);
        int c = Join(P + 1, 30406, "Carl", "uidc", ic);
        int early = CountAd(a, 1500 - At());
        CHECK(early == 0, "joining players don't trigger the Discord message");
        int na = 0, nb = 0, nc = 0;
        while(At() < 4000 && !na) na += CountAd(a, 50);
        long t1 = At();
        nb = CountAd(b, 100); nc = CountAd(c, 100);
        printf("      first message at %ld ms (interval 2000 ms, server ticks once a second)\n", t1);
        CHECK(na == 1, "Discord message arrives after the interval (exactly once)");
        CHECK(nb == 1 && nc == 1, "everybody (also 3.04.6 phones) gets it once");
        int d0; int d = Join(P + 1, MP_BUILD, "Dave", "uidd", d0);
        int again = CountAd(a, 900) + CountAd(d, 100);   // the next one is >= 2 s after the first
        CHECK(again == 0, "a new player joining doesn't send it again");
        int second = 0; while(At() < t1 + 3500) second += CountAd(a, 100);
        CHECK(second == 1, "next one after the next interval");
        CHECK(strlen(MP_AD_TEXT) < MP_CHAT_LEN && MP_AD_INTERVAL_MS == 2720000ull, "text fits a chat line, interval = 45 min 20 s");
        close(a); close(b); close(c); close(d);
        sv.Stop();
    }
    {   // direct UDP join; fallback to the relay port when the direct address is dead
        MPServer sv; sv.Start(P + 2, "Direct test", nullptr, MP_MAX_PLAYERS, 0);
        MPClient c1; Ev e1;
        c1.SetDirect(htonl(0x7f000001), 0);
        c1.Connect("127.77.0.5", P + 2, "Direct", 0, 0, 0);
        bool ok = RunClient(c1, e1, 3000);
        CHECK(ok && c1.IsDirect() && !c1.DirectFellBack(), "direct UDP join works (no relay)");
        c1.Disconnect(true);

        MPClient c2; Ev e2;
        c2.SetDirect(inet_addr("10.255.255.1"), (uint16_t)(P + 2));   // dead address; "relay" = the server port on 127.0.0.1
        auto t0 = std::chrono::steady_clock::now();
        c2.Connect("127.77.0.6", P + 2, "Fallback", 0, 0, 0);
        bool ok2 = RunClient(c2, e2, 6000);
        long ms = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(ok2 && c2.DirectFellBack() && !c2.IsDirect(), "dead direct address -> falls back to the relay and joins");
        printf("      fallback join took %ld ms\n", ms);
        CHECK(ms < 4500, "fallback is quick (< 4.5 s)");
        c2.Disconnect(true);
        sv.Stop();
    }
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
