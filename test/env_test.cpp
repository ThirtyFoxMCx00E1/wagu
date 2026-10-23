// Linux test (2.13/2.14): SyncExt survives the server, explosions/fires only reach nearby players,
// shared weapon pickups (drop -> add for all, take -> remove for all, late joiner gets the list).
//   g++ -O2 -std=c++17 -pthread -I../src env_test.cpp ../src/net/client.cpp ../src/net/server.cpp -o env_test
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <thread>
#include <chrono>
#include "net/client.h"
#include "net/server.h"

struct Ev : INetEvents {
    int ok = 0, ext = 0, extTimeOk = 0, exps = 0, fires = 0, adds = 0, removes = 0; uint16_t lastUid = 0; uint8_t takenBy = 0;
    void OnConnected(const PktWelcome&) override { ok = 1; }
    void OnRejected(uint8_t) override {}
    void OnConnectFailed() override {}
    void OnLostConnection() override {}
    void OnPlayerJoin(const PktPlayerJoin&) override {}
    void OnPlayerLeave(const PktPlayerLeave&) override {}
    void OnSync(const PktSync&, const SyncExt* e) override { if(e) { ++ext; if(e->time == 123456 && e->animCount == 1 && !strcmp(e->anim[0].block, "dancing")) ++extTimeOk; } }
    void OnChat(const PktChat&) override {}
    void OnDeath(const PktDeath&) override {}
    void OnDamage(const PktDamage&) override {}
    void OnServerMessage(const PktServerMsg&) override {}
    void OnSetColor(const PktSetColor&) override {}
    void OnPause(const PktPause&) override {}
    void OnStatus(const PktStatus&) override {}
    void OnWorld(const PktWorld&) override {}
    void OnExplosion(const PktExplosion&) override { ++exps; }
    void OnFire(const PktFire&) override { ++fires; }
    void OnPickupAdd(const PktPickupAdd& a) override { ++adds; lastUid = a.uid; }
    void OnPickupRemove(const PktPickupRemove& r) override { ++removes; takenBy = r.takenBy; }
};

static MPClient cl[4]; static Ev ev[4];
static void Pump(int ms) { for(int t = 0; t < ms / 5; ++t) { for(int i = 0; i < 4; ++i) cl[i].Poll(&ev[i]); std::this_thread::sleep_for(std::chrono::milliseconds(5)); } }
static void SendPos(int i, float x, float y)
{
    // a 2.13 player (raw v1 extension) must still work
    uint8_t buf[sizeof(PktSync) + sizeof(SyncExtV1)]; memset(buf, 0, sizeof(buf));
    PktSync p; memset(&p, 0, sizeof(p)); p.type = PKT_SYNC; p.d.pos[0] = x; p.d.pos[1] = y; p.d.health = 100;
    SyncExtV1 e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.time = 123456; e.animCount = 1; strcpy(e.anim[0].block, "dancing");
    memcpy(buf, &p, sizeof(p)); memcpy(buf + sizeof(p), &e, sizeof(e));
    cl[i].Send(buf, sizeof(buf));
}
int main()
{
    // 2.15 compact v3: round trip, passenger seat, full blend tree, size
    bool v2ok, v2small, v3full;
    {
        SyncExt e; memset(&e, 0, sizeof(e)); e.ver = MP_SYNC_EXT_VER; e.moveState = 6; e.time = 987654321;
        float v[3] = { 5.25f, -1.5f, 0.1f }; memcpy(e.vel, v, 12);
        uint8_t buf[sizeof(SyncExt)]; int n0 = MP_EncodeSyncExt(e, buf);
        e.animCount = 1; AnimSync a; memset(&a, 0, sizeof(a)); a.hash = 0xDEADBEEF; a.group = -1; a.id = 12; a.time = 1.234f; a.blend = 200; a.speed = 50; a.flags = 0x11; strcpy(a.block, "dancing");
        memcpy(&e.anim[0], &a, sizeof(a));
        float f[3] = { 0, 1, 0 }, u[3] = { 0, 0, 1 }; memcpy(e.carFwd, f, 12); memcpy(e.carUp, u, 12);
        int n = MP_EncodeSyncExt(e, buf);
        SyncExt d; bool ok = MP_DecodeSyncExt(buf, n, d);
        AnimSync b; memcpy(&b, &d.anim[0], sizeof(b)); float dv[3], du[3]; memcpy(dv, d.vel, 12); memcpy(du, d.carUp, 12);
        v2ok = ok && d.time == 987654321 && d.moveState == 6 && fabsf(dv[0] - 5.25f) < 0.01f && fabsf(du[2] - 1) < 0.001f &&
               d.animCount == 1 && b.hash == 0xDEADBEEF && b.group == -1 && fabsf(b.time - 1.234f) < 0.002f && !strcmp(b.block, "dancing") && b.flags == 0x11;
        SyncExt bad; v2small = n0 == 14 && n < 64 && !MP_DecodeSyncExt(buf, n - 3, bad);
        // walking player: 4 group anims (walk, run blend, idle partial, weapon) + passenger seat
        SyncExt w; memset(&w, 0, sizeof(w)); w.ver = MP_SYNC_EXT_VER; w.time = 5; w.fullAnims = 1; w.hasVeh = 1; w.vehOwner = 7; w.vehSeat = 2; w.animCount = 4;
        for(int i = 0; i < 4; ++i) { AnimSync g; memset(&g, 0, sizeof(g)); g.group = 54 + i; g.id = (int16_t)i; g.time = 0.5f * i; g.blend = (uint8_t)(250 - i * 40); g.speed = 60; g.flags = 0x2; memcpy(&w.anim[i], &g, sizeof(g)); }
        int nw = MP_EncodeSyncExt(w, buf);
        SyncExt wd; bool okw = MP_DecodeSyncExt(buf, nw, wd);
        AnimSync g3; memcpy(&g3, &wd.anim[3], sizeof(g3));
        v3full = okw && wd.fullAnims && wd.hasVeh && wd.vehOwner == 7 && wd.vehSeat == 2 && wd.animCount == 4 && g3.group == 57 && g3.id == 3 && g3.blend == 130 && fabsf(g3.time - 1.5f) < 0.002f;
        printf("compact ext: %d bytes on foot, %d with rotation + 1 IFP anim, %d walking with 4 anims + seat (2.13: %d)\n", n0, n, nw, (int)sizeof(SyncExtV1));
    }
    MPServer srv; if(!srv.Start(19333, "Env Test", [](const char*){}, 100)) return 1;
    for(int i = 0; i < 3; ++i) { char n[8]; snprintf(n, 8, "P%d", i); cl[i].Connect("127.0.0.1", 19333, n, 106); }
    Pump(600);
    SendPos(0, 2495, -1686); SendPos(1, 2500, -1690); SendPos(2, -2000, 500);   // 2 is ~4.5 km away
    Pump(300);
    PktExplosion e; e.type = PKT_EXPLOSION; e.id = 0; e.expType = 2; e.pos[0] = 2495; e.pos[1] = -1686; e.pos[2] = 13;
    cl[0].Send(&e, sizeof(e));
    PktFire f; f.type = PKT_FIRE; f.id = 0; f.size = 2; memcpy(f.pos, e.pos, 12);
    cl[0].Send(&f, sizeof(f));
    PktPickupDrop d; d.type = PKT_PICKUP_DROP; d.weapon = 31; d.ammo = 120; memcpy(d.pos, e.pos, 12);
    cl[0].Send(&d, sizeof(d));
    Pump(300);
    cl[3].Connect("127.0.0.1", 19333, "Late", 106); Pump(600);   // late joiner
    PktPickupTake t; t.type = PKT_PICKUP_TAKE; t.uid = ev[1].lastUid; cl[1].Send(&t, sizeof(t)); cl[2].Send(&t, sizeof(t));   // both grab
    Pump(300);
    bool pass = true;
    #define CHECK(c, msg) do { bool _c = (c); printf("%-58s %s\n", msg, _c ? "OK" : "FAIL"); pass &= _c; } while(0)
    CHECK(v2ok, "compact extension decodes to the same values");
    CHECK(v2small, "compact extension is small, truncated one rejected");
    CHECK(v3full, "2.15 full blend tree + passenger seat round trip");
    CHECK(ev[0].ok && ev[1].ok && ev[2].ok && ev[3].ok, "4 clients connected");
    CHECK(ev[1].ext >= 1 && ev[1].extTimeOk >= 1, "2.13 extension (clock + anim) still forwarded intact");
    CHECK(ev[1].exps == 1 && ev[1].fires == 1, "nearby player got explosion + fire");
    CHECK(ev[2].exps == 0 && ev[2].fires == 0, "far player (4.5 km) did not");
    CHECK(ev[0].exps == 0, "sender doesn't get its own explosion back");
    CHECK(ev[0].adds == 1 && ev[1].adds == 1 && ev[2].adds == 1, "weapon drop -> pickup for everyone");
    CHECK(ev[3].adds == 1, "late joiner got the pickup list");
    CHECK(ev[0].removes == 1 && ev[3].removes == 1 && ev[0].takenBy == 1, "first taker wins, removed once for everyone");
    for(int i = 0; i < 4; ++i) cl[i].Disconnect(true);
    srv.Stop();
    printf(pass ? "ALL PASSED\n" : "FAILED\n");
    return pass ? 0 : 1;
}
