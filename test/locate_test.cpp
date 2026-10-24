// Linux test (3.21): LOCATE_CHAR_*_OBJECT_2D/3D (0471-0476) with a dead char or object - the crash in
// CRunningScript::LocateCharObjectCommand (realme RE6054 logs). Runs the guard's real decision code
// (src/game/scriptguard_core.h) on a fake script: params are 4-byte cells, the IP is a real pointer.
//   g++ -O2 -std=c++17 -I../src locate_test.cpp -o locate_test && ./locate_test
#include <stdio.h>
#include <string.h>
#include <set>
#include "game/scriptguard_core.h"
using namespace ScriptGuard;

struct FakeScript { uint8_t* base; uint8_t* ip; int32_t code[64]; };
static std::set<int> s_Peds, s_Objs;
static char s_P, s_O;
static int   Peek(void* s) { int32_t v; memcpy(&v, ((FakeScript*)s)->ip, 4); return v; }
static void  Skip(void* s, int16_t n) { ((FakeScript*)s)->ip += 4 * n; }
static void* GetPed(int h) { return s_Peds.count(h) ? &s_P : nullptr; }
static void* GetObj(int h) { return s_Objs.count(h) ? &s_O : nullptr; }
static uintptr_t GetIp(void* s) { return (uintptr_t)((FakeScript*)s)->ip; }
static void SetIp(void* s, uintptr_t v) { ((FakeScript*)s)->ip = (uint8_t*)v; }
static uintptr_t GetBase(void* s) { return (uintptr_t)((FakeScript*)s)->base; }

static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)

// what the game does: collect the params, use the char and the object (NULL -> crash)
static int crashes = 0, results = 0;
static void Original(FakeScript* f, int op)
{
    int ch = Peek(f); Skip(f, 1); int ob = Peek(f); Skip(f, LocateParams(op) - 1);
    if(!GetPed(ch) || !GetObj(ob)) ++crashes; else ++results;
}
// the hook in scriptguard.cpp, minus logging
static bool compareFalse = false;
static void Hook(const LocateEnv& l, FakeScript* f, int op, bool& ipOk)
{
    int ch, ob;
    if(LocateShouldSkip(l, f, op, ipOk, ch, ob)) { l.e.skipParams(f, LocateParams(op)); compareFalse = true; return; }
    Original(f, op);
}
static void Make(FakeScript& f, int ch, int ob, int nParams)
{
    memset(&f, 0, sizeof(f)); f.base = (uint8_t*)f.code; f.code[0] = 0x1234;   // something before
    f.ip = (uint8_t*)&f.code[1]; f.code[1] = ch; f.code[2] = ob;
    for(int i = 3; i <= nParams; ++i) f.code[i] = 100 + i;
    f.code[nParams + 1] = 0x7777;   // next command
}

int main()
{
    LocateEnv l; memset(&l, 0, sizeof(l));
    l.e.peekParam = Peek; l.e.skipParams = Skip; l.e.getObject = GetObj; l.getPed = GetPed; l.getIp = GetIp; l.setIp = SetIp; l.getBase = GetBase;
    s_Peds = {0x101}; s_Objs = {0x205};
    bool ipOk = true;
    for(int op = 0x0471; op <= 0x0476; ++op)
    {
        int n = LocateParams(op);
        FakeScript a; Make(a, 0x101, 0x205, n); crashes = results = 0; compareFalse = false; Hook(l, &a, op, ipOk);
        bool ok1 = results == 1 && crashes == 0 && !compareFalse && Peek(&a) == 0x7777;
        FakeScript b; Make(b, 0x101, 0x75C9, n); crashes = results = 0; compareFalse = false; Hook(l, &b, op, ipOk);   // the log's dead object
        bool ok2 = crashes == 0 && results == 0 && compareFalse && Peek(&b) == 0x7777;
        FakeScript c; Make(c, 0x6E3A, 0x205, n); crashes = results = 0; compareFalse = false; Hook(l, &c, op, ipOk);    // dead char
        bool ok3 = crashes == 0 && results == 0 && compareFalse && Peek(&c) == 0x7777;
        char m[120];
        snprintf(m, sizeof(m), "%04X (%d params): live -> runs; dead object -> skipped; dead char -> skipped; script carries on", op, n);
        CHECK(ok1 && ok2 && ok3, m);
    }
    CHECK(ipOk, "script position layout accepted (object check active)");
    {   // other opcodes are never touched
        FakeScript a; Make(a, 0x6E3A, 0x75C9, 5); int ch, ob; bool k = true;
        CHECK(!LocateShouldSkip(l, &a, 0x0470, k, ch, ob) && !LocateShouldSkip(l, &a, 0x0477, k, ch, ob) && a.ip == (uint8_t*)&a.code[1], "0470 / 0477 untouched");
    }
    {   // a strange IP (not inside the script): only the char is checked, the IP is not moved
        FakeScript a; Make(a, 0x101, 0x75C9, 5); a.base = a.ip + 64; bool k = true; int ch, ob;
        bool skip = LocateShouldSkip(l, &a, 0x0471, k, ch, ob);
        CHECK(!skip && !k && a.ip == (uint8_t*)&a.code[1], "unknown script layout: IP untouched, char-only check from then on");
        FakeScript b; Make(b, 0x6E3A, 0x205, 5);
        CHECK(LocateShouldSkip(l, &b, 0x0471, k, ch, ob), "char-only mode still skips a dead char");
    }
    { int c0, o0; CHECK(!LocateShouldSkip(l, nullptr, 0x0471, ipOk, c0, o0), "null script (R0 = 0 in the log) -> no access"); }
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
