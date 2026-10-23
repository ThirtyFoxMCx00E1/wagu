// 3.10: 3.04.6 + 3.10 phones on one server, clothing sync relay (+ kept for late joiners),
// default server = 100 slots even right after hosting Brief Case War.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include v310_test.cpp ../src/net/client.cpp ../src/net/server.cpp ../server/obj/libmbedtls.a -o v310_test
#include "net/server.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <thread>
#include <chrono>
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static sockaddr_in Addr(int port) { sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(0x7f000001); return a; }
static int Join(int port, uint32_t build, const char* name, const char* uid, int& id)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    timeval tv{0, 300000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in a = Addr(port);
    PktJoin5 j; memset(&j, 0, sizeof(j));
    j.j4.j3.j2.j.type = PKT_JOIN; j.j4.j3.j2.j.proto = MP_PROTO_VERSION; strcpy(j.j4.j3.j2.j.name, name); strcpy(j.j4.j3.uid, uid); j.build = build;
    sendto(s, &j, sizeof(j), 0, (sockaddr*)&a, sizeof(a));
    uint8_t buf[2048]; id = -1;
    for(int k = 0; k < 20; ++k) { int r = recv(s, buf, sizeof(buf), 0); if(r <= 0) break; if(buf[0] == PKT_WELCOME) { id = ((PktWelcome*)buf)->yourId; break; } if(buf[0] == PKT_REJECT) break; }
    return s;
}
static bool WaitClothes(int s, PktClothes& out)
{
    uint8_t buf[2048];
    for(int k = 0; k < 40; ++k) { int r = recv(s, buf, sizeof(buf), 0); if(r <= 0) return false; if(buf[0] == PKT_CLOTHES && r >= (int)sizeof(PktClothes)) { memcpy(&out, buf, sizeof(out)); return true; } }
    return false;
}
int main()
{
    {   // the 20-slot bug
        MPServer sv; sv.SetPersistent(true);
        sv.Start(19781, MP_BCW_SERVER_NAME, nullptr, MP_MAX_PLAYERS, 0);
        CHECK(sv.GetMaxPlayers() == MP_BCW_MAX_PLAYERS, "Brief Case War = 20 slots");
        sv.Stop();
        sv.Start(19781, MP_DEFAULT_SERVER_NAME, nullptr, MP_MAX_PLAYERS, 0);
        CHECK(sv.GetMaxPlayers() == MP_MAX_PLAYERS, "default server right after Brief Case War = 100 slots (was 20)");
        sv.SetMaxPlayers(20);
        CHECK(sv.GetMaxPlayers() == MP_MAX_PLAYERS, "default server can't be set below 100");
        sv.Stop();
        sv.Start(19781, "My server", nullptr, 30, 0);
        CHECK(sv.GetMaxPlayers() == 30, "custom server keeps its own max players");
        sv.Stop();
    }
    MPServer sv; sv.SetPersistent(true);
    if(!sv.Start(19782, MP_DEFAULT_SERVER_NAME, nullptr, MP_MAX_PLAYERS, 0)) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    int ida, idb, idc;
    int a = Join(19782, 30406, "Old3046", "aaaaaaaaaaaaaaaa", ida);
    int b = Join(19782, MP_BUILD, "New310", "bbbbbbbbbbbbbbbb", idb);
    CHECK(ida >= 0, "a 3.04.6 phone gets in (not blocked)");
    CHECK(idb >= 0, "a 3.10 phone gets in");
    int o = Join(19782, 30405, "Old3045", "cccccccccccccccc", idc);
    CHECK(idc < 0, "3.04.5 and older stay refused"); close(o);
    PktClothes c; memset(&c, 0, sizeof(c)); c.type = PKT_CLOTHES; c.id = 99; c.on = 1;
    for(int i = 0; i < MP_CLOTHES_MODELS; ++i) c.model[i] = 0x1000 + i;
    for(int i = 0; i < MP_CLOTHES_TEXTURES; ++i) c.tex[i] = 0x2000 + i;
    c.fat = 10.0f; c.muscle = 500.0f;
    sockaddr_in sa = Addr(19782);
    sendto(b, &c, sizeof(c), 0, (sockaddr*)&sa, sizeof(sa));
    PktClothes got;
    bool ok = WaitClothes(a, got);
    CHECK(ok && got.id == idb && got.model[3] == 0x1003 && got.tex[17] == 0x2011 && got.muscle == 500.0f, "clothes relayed to the other phone, sender id set by the server");
    int c3 = Join(19782, MP_BUILD, "Late310", "dddddddddddddddd", idc);
    ok = WaitClothes(c3, got);
    CHECK(ok && got.id == idb && got.model[0] == 0x1000, "a player who joins later gets the outfit right away");
    // garbage is dropped
    PktClothes bad = c; memset(bad.model, 0, sizeof(bad.model));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sendto(b, &bad, sizeof(bad), 0, (sockaddr*)&sa, sizeof(sa));
    CHECK(!WaitClothes(a, got), "an outfit without any piece is dropped");
    close(a); close(b); close(c3);
    sv.Stop();
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails;
}
