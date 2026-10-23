// 2.37: tests the CPhysical::Add guard decision (src/game/scriptguard_core.h) on Linux.
//   g++ -std=c++17 -I../src addguard_test.cpp -o addguard_test && ./addguard_test
#include <stdio.h>
#include <math.h>
#include "game/scriptguard_core.h"
using namespace ScriptGuard;
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
int main()
{
    float car[3] = { 2495.0f, -1665.0f, 13.3f };                       // Grove Street
    float rc[4]  = { 2492.0f, -1668.0f, 2498.0f, -1662.0f };
    int need = 0;
    CHECK(CheckAdd(car, rc, 3000, 3000, &need) == ADD_OK && need == 1, "normal car at Grove Street is added (1 sector)");
    float edge[4] = { 2449.0f, -1668.0f, 2452.0f, -1662.0f };
    CHECK(CheckAdd(car, edge, 3000, 3000, &need) == ADD_OK && need == 2, "rect over a sector line takes 2 nodes");
    float nanp[3] = { NAN, 0, 0 };
    CHECK(CheckAdd(nanp, rc, 3000, 3000) == ADD_REFUSE_BAD_POS, "NaN position: refused (no wild sector)");
    float infp[3] = { 0, INFINITY, 0 };
    CHECK(CheckAdd(infp, rc, 3000, 3000) == ADD_REFUSE_BAD_POS, "inf position: refused");
    float far[3] = { -1.0e6f, 200, 10 };
    CHECK(CheckAdd(far, rc, 3000, 3000) == ADD_CLAMP_POS, "far past the map edge: pulled back (negative sector = the crash)");
    CHECK(ClampWorld(-1.0e6f) == -WORLD_LIMIT && ClampWorld(5.0f) == 5.0f, "clamp keeps it inside +-2990");
    float huge[4] = { -2900, -2900, 2900, 2900 };
    CHECK(CheckAdd(car, huge, 3000, 3000) == ADD_REFUSE_BAD_RECT, "broken bounding box (thousands of sectors): refused");
    float nanr[4] = { 0, NAN, 1, 1 };
    CHECK(CheckAdd(car, nanr, 3000, 3000) == ADD_REFUSE_BAD_RECT, "NaN bounding box: refused");
    float out[4] = { 2980, 10, 3020, 20 };
    float nearEdge[3] = { 2985, 15, 5 };
    CHECK(CheckAdd(nearEdge, out, 3000, 3000) == ADD_CLAMP_POS, "rect poking past the edge: nudged in");
    { float dx, dy; ShiftInside(out, dx, dy); float moved[4] = { out[0] + dx, out[1] + dy, out[2] + dx, out[3] + dy };
      float np[3] = { nearEdge[0] + dx, nearEdge[1] + dy, 5 };
      CHECK(dx < 0 && dy == 0 && CheckAdd(np, moved, 3000, 3000) == ADD_OK, "after the nudge it is added normally"); }
    CHECK(CheckAdd(car, rc, 10, 3000) == ADD_REFUSE_POOL, "link pool nearly empty: refused (no null node)");
    CHECK(CheckAdd(car, rc, 3000, 5) == ADD_REFUSE_POOL, "entry-info pool nearly empty: refused");
    CHECK(CheckAdd(car, rc, -1, -1) == ADD_OK, "pools unknown: not blocked");
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
