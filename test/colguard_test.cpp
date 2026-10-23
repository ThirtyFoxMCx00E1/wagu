// 3.10: Linux test for the collision guard (src/game/colguard_core.h).
//   g++ -O2 -std=c++17 -I../src colguard_test.cpp -o colguard_test && ./colguard_test
#include <stdio.h>
#include "game/colguard_core.h"
using namespace ColGuard;
static int fails = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while(0)
template<typename P> static void Run(int ps)
{
    struct { uint16_t nS, nB, nT; uint8_t nL, fl; P sph, box, lines, verts, tris, planes; } cd;
    static float verts[300]; static int tris[100];
    // the crash: 3000 triangles, no vertices
    memset(&cd, 0, sizeof(cd)); cd.nT = 3000; cd.tris = (P)(uintptr_t)tris; cd.verts = 0; cd.nS = 2; cd.sph = (P)(uintptr_t)verts;
    CHECK(Sanitize((uint8_t*)&cd, ps) == FIX_TRIS); CHECK(cd.nT == 0); CHECK(cd.nS == 2);
    // fine data is never touched
    memset(&cd, 0, sizeof(cd)); cd.nT = 10; cd.tris = (P)(uintptr_t)tris; cd.verts = (P)(uintptr_t)verts; cd.nB = 1; cd.box = (P)(uintptr_t)verts; cd.nL = 4; cd.lines = (P)(uintptr_t)verts;
    CHECK(Sanitize((uint8_t*)&cd, ps) == 0); CHECK(cd.nT == 10 && cd.nB == 1 && cd.nL == 4);
    // garbage small pointers (0x5CAC style) + missing triangles + bad boxes/lines
    memset(&cd, 0, sizeof(cd)); cd.nT = 5; cd.verts = (P)(uintptr_t)verts; cd.tris = (P)0x5CAC; cd.nB = 3; cd.box = 0; cd.nL = 2; cd.lines = (P)0x10;
    CHECK(Sanitize((uint8_t*)&cd, ps) == (FIX_TRIS | FIX_BOX | FIX_LINES)); CHECK(cd.nT == 0 && cd.nB == 0 && cd.nL == 0);
    // empty model: nothing to do
    memset(&cd, 0, sizeof(cd)); CHECK(Sanitize((uint8_t*)&cd, ps) == 0);
    CHECK(Sanitize(nullptr, ps) == 0);
}
int main()
{
    if(sizeof(void*) == 8) Run<uint64_t>(8);
    Run<uint32_t>(4);
    CHECK(ColDataOffset(4) == 0x2C); CHECK(ColDataOffset(8) == 0x30);
    printf(fails ? "colguard_test: %d FAILED\n" : "colguard_test: all passed%.0d\n", fails);
    return fails != 0;
}
