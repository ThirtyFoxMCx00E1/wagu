// Linux test (2.16.2): the script guard that stops "GET_OFFSET_FROM_CAR / OBJECT_IN_WORLD_COORDS with a
// dead handle" from crashing the game. Runs the guard's real decision code (src/game/scriptguard_core.h)
// against a fake game: a script is a list of parameters with a read position, pools know which handles live.
//   g++ -O2 -std=c++17 -I../src guard_test.cpp -o guard_test && ./guard_test
#include <stdio.h>
#include <vector>
#include <set>
#include "game/scriptguard_core.h"

using namespace ScriptGuard;

// ---- fake game -------------------------------------------------------------------------------
struct FakeScript { std::vector<int> params; size_t pos = 0; };
static std::set<int> s_LiveCars, s_LiveObjects;
static int  s_nPeeks = 0, s_nOriginalCalls = 0, s_nCrashes = 0, s_nResults = 0;
static char s_Car = 0, s_Obj = 0;   // addresses used as "pointers"

static int   FakePeek(void* s) { ++s_nPeeks; FakeScript* f = (FakeScript*)s; return f->params[f->pos]; }        // does not move
static void  FakeSkip(void* s, int16_t n) { ((FakeScript*)s)->pos += n; }
static void* FakeGetVehicle(int h) { return (h >= 0 && s_LiveCars.count(h)) ? &s_Car : nullptr; }   // same as CPools::GetVehicle
static void* FakeGetObject(int h)  { return (h >= 0 && s_LiveObjects.count(h)) ? &s_Obj : nullptr; }

// What the game's own opcode does (disassembled from libGTASA 2.00 / 2.10): collect 4 parameters, look the
// handle up, carry on with whatever came back - a NULL entity is dereferenced - then store 3 results.
static int8_t FakeOriginal(void* s, int opcode)
{
    FakeScript* f = (FakeScript*)s;
    ++s_nOriginalCalls;
    int handle = f->params[f->pos];
    f->pos += 4;
    void* entity = (opcode == OP_GET_OFFSET_FROM_CAR) ? FakeGetVehicle(handle) : FakeGetObject(handle);
    if(!entity) ++s_nCrashes;   // ldr r1, [r5, #0x14] with r5 = 0
    else ++s_nResults;
    f->pos += 3;
    return 0;
}

static Env MakeEnv() { Env e; e.peekParam = FakePeek; e.skipParams = FakeSkip; e.getVehicle = FakeGetVehicle; e.getObject = FakeGetObject; return e; }

// The hook in scriptguard.cpp, minus logging.
static int8_t Hook(const Env& e, void* script, int opcode)
{
    int handle = 0;
    if(ShouldSkip(e, script, opcode, handle)) return 0;
    return FakeOriginal(script, opcode);
}

static int s_Fail = 0;
#define CHECK(c) do { if(!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++s_Fail; } } while(0)
static void Reset() { s_nPeeks = s_nOriginalCalls = s_nCrashes = s_nResults = 0; }

// handle, offset x y z, result variables 3x, then the NEXT command's first parameter (marker 777)
static FakeScript MakeScript(int handle) { FakeScript f; f.params = { handle, 1, 2, 3, 10, 11, 12, 777 }; return f; }

int main()
{
    Env env = MakeEnv();
    s_LiveCars = { 0x1805 };       // index 24, id 5
    s_LiveObjects = { 0x0A03 };

    // 0. sanity: without the guard a dead handle really "crashes" in the fake game (so the test means something)
    { Reset(); FakeScript f = MakeScript(0x18A6); FakeOriginal(&f, OP_GET_OFFSET_FROM_CAR); CHECK(s_nCrashes == 1); }

    // 1. dead car (slot's id changed): skipped, original never runs, script stands right behind the 7 parameters
    { Reset(); FakeScript f = MakeScript(0x18A6); CHECK(Hook(env, &f, OP_GET_OFFSET_FROM_CAR) == 0);
      CHECK(s_nOriginalCalls == 0 && s_nCrashes == 0); CHECK(f.pos == 7 && f.params[f.pos] == 777); }

    // 2. negative handle (e.g. -1 from a failed create)
    { Reset(); FakeScript f = MakeScript(-1); Hook(env, &f, OP_GET_OFFSET_FROM_CAR);
      CHECK(s_nOriginalCalls == 0 && s_nCrashes == 0 && f.pos == 7); }

    // 3. handle 0
    { Reset(); FakeScript f = MakeScript(0); Hook(env, &f, OP_GET_OFFSET_FROM_CAR);
      CHECK(s_nOriginalCalls == 0 && s_nCrashes == 0 && f.pos == 7); }

    // 4. dead object (opcode 0400)
    { Reset(); FakeScript f = MakeScript(0x0A99); Hook(env, &f, OP_GET_OFFSET_FROM_OBJECT);
      CHECK(s_nOriginalCalls == 0 && s_nCrashes == 0 && f.pos == 7); }

    // 5. live car: goes to the game untouched - peeking must not have moved the script
    { Reset(); FakeScript f = MakeScript(0x1805); Hook(env, &f, OP_GET_OFFSET_FROM_CAR);
      CHECK(s_nOriginalCalls == 1 && s_nResults == 1 && s_nCrashes == 0); CHECK(f.pos == 7 && f.params[f.pos] == 777); }

    // 6. live object
    { Reset(); FakeScript f = MakeScript(0x0A03); Hook(env, &f, OP_GET_OFFSET_FROM_OBJECT);
      CHECK(s_nOriginalCalls == 1 && s_nResults == 1 && s_nCrashes == 0 && f.pos == 7); }

    // 7. pools are separate: a live car handle given to the object opcode is a dead object handle
    { Reset(); FakeScript f = MakeScript(0x1805); Hook(env, &f, OP_GET_OFFSET_FROM_OBJECT);
      CHECK(s_nOriginalCalls == 0 && s_nCrashes == 0 && f.pos == 7); }

    // 8. every other opcode is forwarded without even looking at the script
    { Reset(); FakeScript f = MakeScript(0x18A6); int h = 0;
      CHECK(!ShouldSkip(env, &f, 0x0441, h)); CHECK(!ShouldSkip(env, &f, 0x0406, h)); CHECK(!ShouldSkip(env, &f, 0x0408, h));
      CHECK(!ShouldSkip(env, &f, 0x044B, h)); CHECK(s_nPeeks == 0 && f.pos == 0); }

    // 9. the handle is reported for the log
    { FakeScript f = MakeScript(0x18A6); int h = 0; CHECK(ShouldSkip(env, &f, OP_GET_OFFSET_FROM_CAR, h) && h == 0x18A6); }

    // 10. a script that loops on the dead handle (every frame) never crashes
    { Reset(); for(int frame = 0; frame < 1000; ++frame) { FakeScript f = MakeScript(0x18A6); Hook(env, &f, OP_GET_OFFSET_FROM_CAR); }
      CHECK(s_nCrashes == 0 && s_nOriginalCalls == 0); }

    // 11. (2.28) 0432 GET_CHAR_IN_CAR_PASSENGER_SEAT with a dead car: skipped past its 3 params (car, seat, result)
    { Reset(); FakeScript f; f.params = { 0x18A6, 1, 0, 777 }; int h = 0;
      CHECK(ShouldSkip(env, &f, OP_GET_CHAR_IN_CAR_PASSENGER_SEAT, h) && h == 0x18A6); CHECK(f.pos == 3 && f.params[f.pos] == 777); }
    // 12. (2.28) 0432 with a live car goes to the game untouched
    { Reset(); FakeScript f; f.params = { 0x1805, 1, 0, 777 }; int h = 0;
      CHECK(!ShouldSkip(env, &f, OP_GET_CHAR_IN_CAR_PASSENGER_SEAT, h)); CHECK(f.pos == 0); }

    printf(s_Fail ? "guard_test: %d FAILED\n" : "guard_test: all OK\n", s_Fail);
    return s_Fail ? 1 : 0;
}
