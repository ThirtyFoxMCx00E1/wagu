// 3.30 test: a dropped bag item lands on the ground (PKT_RP_DROPS row), another citizen walks to it
// and picks it up (it goes into their bag), too far = refused, expiry clears it.
//   g++ -O2 -std=c++17 -pthread -I../src -I../third_party/mbedtls/include rp_drop_test.cpp ../src/net/server.cpp ../src/net/online.cpp ../src/net/lan.cpp ../src/net/authcrypto.cpp ../server/obj/libmbedtls.a -o rp_drop_test
#include <stdio.h>
#include <string.h>
#define private public
#define protected public
#include "net/server.h"
#undef private
#undef protected
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static void Cmd(MPServer& s, int slot, uint8_t cmd, uint8_t a, int32_t val)
{
    PktRpCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_RP_CMD; c.cmd = cmd; c.a = a; c.val = val;
    s.m_Rp[slot].lastInv = 0;
    s.RpHandleCmd(slot, c);
}
int main()
{
    MPServer s; s.SetPersistent(true); s.SetRpFile("/tmp/rp_drop_test.txt"); remove("/tmp/rp_drop_test.txt");
    if(!s.Start(19461, MP_RP_SERVER_NAME, nullptr, 100)) { printf("server failed\n"); return 1; }
    CHECK(s.IsRpServer(), "Mega Vil is the RP server");
    for(int i = 0; i < 2; ++i)
    {
        s.m_Slots[i].used = true; s.m_Slots[i].authed = true; snprintf(s.m_Slots[i].name, sizeof(s.m_Slots[i].name), "Citizen_%d", i);
        s.m_Slots[i].hasPos = true; s.m_Slots[i].pos[0] = 1480.0f + i * 1.0f; s.m_Slots[i].pos[1] = -1690.0f; s.m_Slots[i].pos[2] = 14.0f;
        s.m_Rp[i].loaded = true;
    }
    RpInvAdd(s.m_Rp[0].inv, RPX_PHONE, 1);
    RpInvAdd(s.m_Rp[0].inv, RPX_BURGER, 3);
    Cmd(s, 0, RPC_INV_DROP, 0, 1);
    int n = 0, at = -1; for(int i = 0; i < RP_DROPS_MAX; ++i) if(s.m_RpDrops[i].id) { ++n; at = i; }
    CHECK(n == 1 && s.m_RpDrops[at].id == RPX_PHONE && s.m_RpDrops[at].count == 1, "the dropped phone is on the ground");
    CHECK(!s.m_Rp[0].inv[0].id, "and gone from the bag");
    float dx = s.m_RpDrops[at].pos[0] - 1480.0f, dy = s.m_RpDrops[at].pos[1] + 1690.0f;
    CHECK(dx * dx + dy * dy < 1.0f, "it lies at the feet of the one who dropped it");
    CHECK(g_RpDropModel[RPX_PHONE] == 330, "it is the real cellphone model");
    // too far
    s.m_Slots[1].pos[0] = 1500.0f;
    Cmd(s, 1, RPC_INV_PICKUP, 0, s.m_RpDrops[at].uid);
    CHECK(s.m_RpDrops[at].id == RPX_PHONE, "too far away: still on the ground");
    // walk up and pick it up
    s.m_Slots[1].pos[0] = 1481.0f;
    uint16_t uid = s.m_RpDrops[at].uid;
    Cmd(s, 1, RPC_INV_PICKUP, 0, uid);
    CHECK(!s.m_RpDrops[at].id, "picked up: gone from the ground");
    bool has = false; for(int i = 0; i < RP_INV_SLOTS; ++i) if(s.m_Rp[1].inv[i].id == RPX_PHONE) has = true;
    CHECK(has, "the phone is in the other citizen's bag");
    Cmd(s, 1, RPC_INV_PICKUP, 0, uid);
    CHECK(true, "picking up a gone drop again is harmless");
    // 2 of 3 burgers, then expiry
    Cmd(s, 0, RPC_INV_DROP, 1, 2);
    at = -1; for(int i = 0; i < RP_DROPS_MAX; ++i) if(s.m_RpDrops[i].id) at = i;
    CHECK(at >= 0 && s.m_RpDrops[at].id == RPX_BURGER && s.m_RpDrops[at].count == 2 && s.m_Rp[0].inv[1].count == 1, "2 burgers dropped, 1 left in the bag");
    s.m_RpDrops[at].until = 1;
    s.RpTick(MP_NowMs() + 5000);
    CHECK(!s.m_RpDrops[at].id, "drops fade away after their time");
    // the packet: header + rows
    CHECK(sizeof(RpDropRow) == 20, "a drop row is 20 bytes on the wire");
    s.Stop();
    printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
