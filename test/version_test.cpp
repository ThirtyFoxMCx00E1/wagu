// 3.04.6: old MP versions are refused + logged, 3.04.6 gets in (g++ -std=c++17 -pthread -I../src ... like bcw_test)
#include "net/server.h"
#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <thread>
#include <chrono>
static int sendJoin(int len, uint32_t build, const char* name, uint8_t* reply, int& rlen, int& msgs)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    timeval tv{1,0}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(19771); a.sin_addr.s_addr = htonl(0x7f000001);
    PktJoin5 j; memset(&j, 0, sizeof(j));
    j.j4.j3.j2.j.type = PKT_JOIN; j.j4.j3.j2.j.proto = MP_PROTO_VERSION; strcpy(j.j4.j3.j2.j.name, name); strcpy(j.j4.j3.uid, "abcdef0123456789"); j.build = build;
    sendto(s, &j, len, 0, (sockaddr*)&a, sizeof(a));
    int last = 0; msgs = 0; uint8_t buf[2048];
    for(int k = 0; k < 6; ++k) { int r = recv(s, buf, sizeof(buf), 0); if(r <= 0) break; if(buf[0] == PKT_SERVER_MSG) { ++msgs; printf("  msg: %s\n", ((PktServerMsg*)buf)->text); } if(buf[0] == PKT_REJECT || buf[0] == PKT_WELCOME) { memcpy(reply, buf, r); rlen = r; last = buf[0]; break; } }
    close(s); return last;
}
int main()
{
    MPServer sv; sv.SetPersistent(true); sv.SetOutdatedLog("/tmp/vt/outdated.log");
    if(!sv.Start(19771, "VT", nullptr, 10, 0)) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    uint8_t rep[2048]; int rl = 0, m = 0, fails = 0;
    int t = sendJoin(sizeof(PktJoin4), 0, "Old3045", rep, rl, m);
    printf("old 3.04.5 join -> %s reason %d msgs %d\n", t == PKT_REJECT ? "REJECT" : "?", t == PKT_REJECT ? rep[1] : -1, m); if(t != PKT_REJECT || rep[1] != REJECT_VERSION || m < 1) ++fails;
    t = sendJoin(sizeof(PktJoin2), 0, "Old219", rep, rl, m);
    printf("old 2.19 join -> %s\n", t == PKT_REJECT ? "REJECT" : "?"); if(t != PKT_REJECT) ++fails;
    t = sendJoin(sizeof(PktJoin5), 30405, "Fake", rep, rl, m);
    printf("build 30405 join -> %s reason %d\n", t == PKT_REJECT ? "REJECT" : "?", t == PKT_REJECT ? rep[1] : -1); if(t != PKT_REJECT || rep[1] != REJECT_OUTDATED) ++fails;
    t = sendJoin(sizeof(PktJoin5), MP_BUILD, "New3046", rep, rl, m);
    printf("3.04.6 join -> %s len %d (welcome2 %d)\n", t == PKT_WELCOME ? "WELCOME" : "?", rl, (int)sizeof(PktWelcome2)); if(t != PKT_WELCOME || rl < (int)sizeof(PktWelcome2)) ++fails;
    printf("refused count %u\n", sv.OutdatedRefused());
    sv.Stop(); printf(fails ? "FAIL %d\n" : "ALL PASSED\n", fails); return fails;
}
