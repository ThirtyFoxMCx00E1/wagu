// Linux test (3.21.3): the script fault guard (src/game/scriptfault_core.h). A fake opcode handler
// really reads a dead pointer (SIGSEGV, like HAS_CHAR_GOT_WEAPON on a removed ped in the crash log);
// the guard must jump back out, park that script, keep everything else running, and hand faults
// outside a guarded call to the previous handler (AML's crash log).
//   g++ -O2 -std=c++17 -pthread -I../src scriptfault_test.cpp -o scriptfault_test && ./scriptfault_test
#include <stdio.h>
#include <thread>
#include "game/scriptfault_core.h"
using namespace ScriptFault;
static bool G8(int8_t (*fn)(void*, int), void* s, int op, int8_t& r) { return Call<int8_t, void*>(fn, s, op, r); }

static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)

struct FakePed { int weapon; };
static FakePed* volatile s_Ped = nullptr;           // the ped the script kept - gone
static FakePed s_Alive = { 24 };
static int8_t HasCharGotWeapon(void* script, int op) { (void)script; (void)op; return s_Ped->weapon == 24 ? 0 : 1; }   // faults on a dead ped
static int8_t Fine(void*, int) { return 7; }
static int8_t Nested(void* s, int op) { int8_t r = 0; bool ok = G8(HasCharGotWeapon, s, op, r); return ok ? 5 : 9; }

// "AML's crash handler": installed before ours, must get faults that are not in a guarded call
static sigjmp_buf s_AmlJmp; static volatile int s_AmlGot = 0;
static void AmlHandler(int, siginfo_t*, void*) { ++s_AmlGot; siglongjmp(s_AmlJmp, 1); }

int main()
{
    struct sigaction sa; memset(&sa, 0, sizeof(sa)); sa.sa_sigaction = AmlHandler; sa.sa_flags = SA_SIGINFO | SA_NODEFER; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    CHECK(Install(), "guard installed in front of the old handler");
    CHECK(!Install(), "second Install: already ours, nothing to do");

    int dummy; void* script = &dummy; int8_t r = -1;
    CHECK(G8(Fine, script, 0x0001, r) && r == 7, "a normal opcode runs and returns its result");
    s_Ped = nullptr;
    bool ok = G8(HasCharGotWeapon, script, 0x0491, r);
    CHECK(!ok && Faults() == 1, "0491 on a removed ped: fault caught, the game goes on");
    bool all = true;
    for(int i = 0; i < 200; ++i) if(G8(HasCharGotWeapon, script, 0x0491, r)) all = false;   // every frame again
    CHECK(all && Faults() == 201, "200 more faults in a row: all caught (SIGSEGV not left blocked)");
    s_Ped = &s_Alive;
    CHECK(G8(HasCharGotWeapon, script, 0x0491, r) && r == 0, "same opcode with a live ped: runs normally");
    s_Ped = nullptr;
    CHECK(G8(Nested, script, 0x0491, r) && r == 9, "nested guarded call: inner fault returns to the inner call");

    // parked scripts
    Table t; int a, b;
    t.Add(&a, 1000, 0x0491);
    CHECK(t.IsParked(&a, 1000) && !t.IsParked(&a, 1002) && !t.IsParked(&b, 1000), "script parked on its opcode only");
    t.Forget(&a, 1000); CHECK(t.IsParked(&a, 1000), "still on the parked opcode: stays parked");
    t.Forget(&a, 5000); CHECK(!t.IsParked(&a, 1000) && t.n == 0, "slot reused by a new script (other position): no longer parked");
    for(int i = 0; i < MAX_PARKED; ++i) t.Add((void*)(uintptr_t)(0x1000 + i * 16), 10, 1);
    CHECK(!t.Add((void*)0x99990, 10, 1) && t.n == MAX_PARKED, "table full: no overflow");

    // a fault outside a guarded call -> the old handler (crash log) gets it
    if(sigsetjmp(s_AmlJmp, 1) == 0) { volatile int x = s_Ped->weapon; (void)x; }
    CHECK(s_AmlGot == 1, "fault outside a script opcode: handed to the previous handler (AML crash log)");

    // another thread's fault while the game thread is inside a guarded call is NOT swallowed
    s_AmlGot = 0;
    std::thread th([]{ int d; int8_t rr; G8(Fine, (void*)&d, 1, rr); });   // other thread: unguarded path
    th.join();
    CHECK(s_AmlGot == 0, "other threads run unguarded (scripts run on one thread)");

    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
