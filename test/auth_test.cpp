// 3.21.6 login system, end to end on Linux: real MPServer ("MP LAN Server" = login on), the real
// phone login code (AuthClient + auth link over the public broker) and the real bot code
// (test/auth_bot_harness.py runs authcore + main.py's MQTT bridge, and types /gen itself).
//   g++ -O1 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include auth_test.cpp ../src/net/authclient.cpp \
//       ../src/net/authcrypto.cpp ../src/net/online.cpp ../src/net/server.cpp ../src/net/lan.cpp ../server/obj/libmbedtls.a -o auth_test
//   python3 auth_bot_harness.py 150 & ./auth_test
#include <stdio.h>
#include <string.h>
#include <set>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include "net/server.h"
#include "net/authclient.h"
#include "net/online.h"

static int g_Fail = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++g_Fail; } else printf("ok: %s\n", #c); } while(0)

struct RawClient
{
    int fd = -1; sockaddr_in srv{};
    PktAuthChal chal{}; bool gotChal = false; PktAuthResult res{}; bool gotRes = false; bool rejected = false;
    std::set<int> joinedIds; int myId = -1;
    void Open(uint16_t port) { fd = socket(AF_INET, SOCK_DGRAM, 0); srv.sin_family = AF_INET; srv.sin_port = htons(port); srv.sin_addr.s_addr = htonl(0x7F000001);
                               timeval tv{0, 20000}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)); }
    void Send(const void* d, int n) { sendto(fd, d, n, 0, (sockaddr*)&srv, sizeof(srv)); }
    void Join(const char* name, uint32_t build)
    {
        PktJoin5 j; memset(&j, 0, sizeof(j)); j.j4.j3.j2.j.type = PKT_JOIN; j.j4.j3.j2.j.proto = MP_PROTO_VERSION;
        snprintf(j.j4.j3.j2.j.name, MP_NAME_LEN, "%s", name); j.build = build; Send(&j, sizeof(j));
    }
    void Pump(int ms)
    {
        uint64_t end = MP_NowMs() + ms;
        while(MP_NowMs() < end)
        {
            uint8_t b[1500]; int r = recv(fd, b, sizeof(b), 0);
            if(r <= 0) continue;
            if(b[0] == PKT_AUTH && b[1] == AUTH_CHALLENGE && r >= (int)sizeof(PktAuthChal)) { memcpy(&chal, b, sizeof(chal)); gotChal = true; }
            if(b[0] == PKT_AUTH && b[1] == AUTH_RESULT && r >= (int)sizeof(PktAuthResult)) { memcpy(&res, b, sizeof(res)); gotRes = true; }
            if(b[0] == PKT_REJECT) rejected = true;
            if(b[0] == PKT_WELCOME) myId = b[1];
            if(b[0] == PKT_PLAYER_JOIN) joinedIds.insert(b[1]);
        }
    }
    void SendTicket(const std::string& p, const uint8_t sig[64])
    {
        uint8_t buf[400]; PktAuthTicket h; h.type = PKT_AUTH; h.op = AUTH_TICKET; h.plen = (uint16_t)p.size(); memcpy(h.sig, sig, 64);
        memcpy(buf, &h, sizeof(h)); memcpy(buf + sizeof(h), p.data(), p.size()); Send(buf, (int)(sizeof(h) + p.size()));
    }
};

static bool RunUntil(AuthClient& a, AuthClient::State want, int ms)
{
    uint64_t end = MP_NowMs() + ms;
    while(MP_NowMs() < end) { a.Tick(MP_NowMs()); if(a.state == want) return true; std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    printf("   (state %d err '%s')\n", a.state, a.err.c_str());
    return false;
}

int main()
{
    // 1. codes: random, never the same
    {
        std::set<std::string> s; bool alpha = true;
        for(int i = 0; i < 200000; ++i) { char c[9]; AuthCrypto::RandomCode(c); s.insert(c); for(int k = 0; k < 8; ++k) if(!strchr("ABCDEFGHJKLMNPQRSTUVWXYZ23456789", c[k])) alpha = false; }
        CHECK(s.size() == 200000); CHECK(alpha);
    }
    MPServer srv;
    CHECK(srv.Start(19922, MP_DEFAULT_SERVER_NAME, nullptr, 100, 0));
    CHECK(srv.AuthOn());

    // 2. old phone refused on a login server
    { RawClient o; o.Open(19922); o.Join("OldPhone", 32105); o.Pump(400); CHECK(o.rejected); }

    // 3. watcher: a logged in player sees who joins
    RawClient a; a.Open(19922); a.Join("Alice", MP_BUILD); a.Pump(500);
    CHECK(a.gotChal); CHECK(a.chal.style == AUTH_STYLE_NORMAL); CHECK(strlen(a.chal.chal) == 32);

    AuthClient ac;
    ac.Begin(a.chal.chal, MP_DEFAULT_SERVER_NAME, false, "");
    CHECK(RunUntil(ac, AuthClient::CODE, 30000));
    printf("   code on screen: %s via %s\n", ac.code, Online::AuthLinkBroker());
    CHECK(RunUntil(ac, AuthClient::SIGNUP, 40000));     // harness typed /gen
    CHECK(ac.discordName == "tester#0001");
    // bad sign up data is refused by the bot
    ac.Signup("a", "secret12", "Male", 20);
    CHECK(RunUntil(ac, AuthClient::SIGNUP, 20000) && !ac.err.empty());
    printf("   error shown: %s\n", ac.err.c_str());
    ac.Signup("Tester_1", "secret12", "Male", 20);
    CHECK(RunUntil(ac, AuthClient::WAIT_SERVER, 20000));
    CHECK(ac.createdNow);
    std::string tk; uint8_t sig[64];
    CHECK(ac.TakeTicket(tk, sig));
    printf("   ticket: %.60s...\n", tk.c_str());
    // tampered ticket refused
    { std::string bad = tk; bad[7] ^= 1; a.SendTicket(bad, sig); a.Pump(600); CHECK(a.gotRes && !a.res.ok); printf("   server: %s\n", a.res.msg); a.gotRes = false; }
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    a.SendTicket(tk, sig); a.Pump(600);
    CHECK(a.gotRes && a.res.ok); CHECK(!strcmp(a.res.user, "Tester_1"));
    char acc[24] = ""; CHECK(srv.AccountOf(a.myId, acc, sizeof(acc)) && !strcmp(acc, "Tester_1"));

    // 4. Bob joins: nobody sees him before his login, his ticket replay (Alice's) fails
    RawClient b; b.Open(19922); b.Join("Bob", MP_BUILD); b.Pump(500); a.Pump(300);
    CHECK(b.gotChal);
    CHECK(a.joinedIds.count(b.myId) == 0);
    b.SendTicket(tk, sig); b.Pump(600);
    CHECK(b.gotRes && !b.res.ok); printf("   replay: %s\n", b.res.msg);
    { PktChat c; memset(&c, 0, sizeof(c)); c.type = PKT_CHAT; snprintf(c.text, sizeof(c.text), "spam"); b.Send(&c, sizeof(c)); }
    // 5. login on a new connection (account on the phone): wrong password, then right
    AuthClient bc;
    bc.Begin(b.chal.chal, MP_DEFAULT_SERVER_NAME, true, "Tester_1");
    CHECK(RunUntil(bc, AuthClient::LOGIN, 30000));
    bc.Login("Tester_1", "wrongpass");
    CHECK(RunUntil(bc, AuthClient::LOGIN, 20000) && !bc.err.empty()); printf("   error shown: %s\n", bc.err.c_str());
    bc.Login("tester_1", "secret12");
    CHECK(RunUntil(bc, AuthClient::WAIT_SERVER, 20000));
    CHECK(!bc.createdNow);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    CHECK(bc.TakeTicket(tk, sig)); b.gotRes = false; b.SendTicket(tk, sig); b.Pump(600);
    CHECK(b.gotRes && b.res.ok);
    a.Pump(600);
    CHECK(srv.AccountOf(b.myId, acc, sizeof(acc)));   // Bob is in now
    // same account twice: the older connection (Alice) was dropped
    CHECK(!srv.AccountOf(a.myId, acc, sizeof(acc)));

    // 6. reconnect (host change): ticket from the session, no screen
    RawClient c; c.Open(19922); c.Join("Bob2", MP_BUILD); c.Pump(500); CHECK(c.gotChal);
    bc.Begin(c.chal.chal, MP_DEFAULT_SERVER_NAME, true, "Tester_1");
    CHECK(RunUntil(bc, AuthClient::WAIT_SERVER, 30000));
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    CHECK(bc.TakeTicket(tk, sig)); c.SendTicket(tk, sig); c.Pump(600); CHECK(c.gotRes && c.res.ok);

    // 7. ticket for another server name refused
    { MPServer other; CHECK(other.Start(19923, "Some Server", nullptr, 10, 0)); other.SetAuth(true);
      RawClient d; d.Open(19923); d.Join("Dan", MP_BUILD); d.Pump(500); CHECK(d.gotChal);
      d.SendTicket(tk, sig); d.Pump(600); CHECK(d.gotRes && !d.res.ok); printf("   other server: %s\n", d.res.msg); other.Stop(); }

    // 8. /stats
    bc.RequestStats();
    for(int i = 0; i < 500 && bc.statsPending; ++i) { bc.Tick(MP_NowMs()); std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
    CHECK(bc.stats["ok"] == "1"); CHECK(bc.stats["gender"] == "Male"); CHECK(bc.stats["age"] == "20"); CHECK(bc.stats["discord"] == "tester#0001");
    printf("   stats: user=%s logins=%s\n", bc.stats["user"].c_str(), bc.stats["logins"].c_str());

    // 9. "new account" with a Discord that already has one -> back to login
    AuthClient ec; RawClient e; e.Open(19922); e.Join("Eve", MP_BUILD); e.Pump(500);
    ec.Begin(e.chal.chal, MP_DEFAULT_SERVER_NAME, true, "");
    CHECK(RunUntil(ec, AuthClient::LOGIN, 30000));
    ec.WantNewAccount(); CHECK(ec.state == AuthClient::CODE);
    CHECK(RunUntil(ec, AuthClient::LOGIN, 40000)); CHECK(ec.prefillUser == "Tester_1");
    printf("   info: %s\n", ec.info.c_str());

    // 10. a server without the login system: no challenge
    { MPServer open; CHECK(open.Start(19924, "Open Server", nullptr, 10, 0)); RawClient f; f.Open(19924); f.Join("Fay", 32105); f.Pump(500); CHECK(!f.gotChal && !f.rejected); open.Stop(); }

    Online::AuthLinkStop();
    srv.Stop();
    printf(g_Fail ? "FAILED %d\n" : "ALL PASSED\n", g_Fail);
    return g_Fail ? 1 : 0;
}
