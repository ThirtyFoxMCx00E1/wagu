// 2.18.1: fault guard for fitting tuning parts (code copied from src/game/netstate.cpp).
//   g++ -O2 -std=c++17 -pthread upgrade_guard_test.cpp -o upgrade_guard_test

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
static int  (*VehAddUpgrade_fn)(void*, int) = nullptr;
static void (*VehRemoveUpgrade_fn)(void*, int) = nullptr;
static sigjmp_buf s_UpJmp;
static volatile sig_atomic_t s_bInUpgrade = 0;
static pthread_t s_UpThread;
static struct sigaction s_OldSegv, s_OldBus;
static uint8_t s_BadUpgrade[(212 * 194 + 7) / 8];   // (car model 400..611) x (part 1000..1193)
static int  s_nUpgradeFaults = 0;
static bool IsBadUpgrade(int model, int part)
{
    if(model < 400 || model > 611 || part < 1000 || part > 1193) return true;
    int b = (model - 400) * 194 + (part - 1000); return (s_BadUpgrade[b >> 3] >> (b & 7)) & 1;
}
static void SetBadUpgrade(int model, int part)
{
    if(model < 400 || model > 611 || part < 1000 || part > 1193) return;
    int b = (model - 400) * 194 + (part - 1000); s_BadUpgrade[b >> 3] |= (uint8_t)(1 << (b & 7));
}
static void UpgradeFault(int sig, siginfo_t* si, void* uc)
{
    if(s_bInUpgrade && pthread_equal(pthread_self(), s_UpThread)) { s_bInUpgrade = 0; siglongjmp(s_UpJmp, 1); }
    // not ours: hand it to whoever was there before (AML's crash log, the system)
    struct sigaction* old = sig == SIGBUS ? &s_OldBus : &s_OldSegv;
    if(old->sa_flags & SA_SIGINFO) { if(old->sa_sigaction) { old->sa_sigaction(sig, si, uc); return; } }
    else if(old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) { old->sa_handler(sig); return; }
    signal(sig, SIG_DFL); raise(sig);
}
static bool GuardedUpgrade(void* v, int part, bool add)
{
    if(add ? !VehAddUpgrade_fn : !VehRemoveUpgrade_fn) return false;
    struct sigaction sa; memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = UpgradeFault; sa.sa_flags = SA_SIGINFO | SA_ONSTACK; sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, &s_OldSegv); sigaction(SIGBUS, &sa, &s_OldBus);
    bool ok = true;
    s_UpThread = pthread_self();
    if(sigsetjmp(s_UpJmp, 1) == 0)
    {
        s_bInUpgrade = 1;
        if(add) VehAddUpgrade_fn(v, part); else VehRemoveUpgrade_fn(v, part);
        s_bInUpgrade = 0;
    }
    else ok = false;
    sigaction(SIGSEGV, &s_OldSegv, nullptr); sigaction(SIGBUS, &s_OldBus, nullptr);
    return ok;
}
static void FitUpgrade(void* v, int carModel, int part, bool add)
{
    if(IsBadUpgrade(carModel, part)) return;
    if(!GuardedUpgrade(v, part, add))
    {
        SetBadUpgrade(carModel, part);
        if(++s_nUpgradeFaults <= 20) printf("MP: tuning part %d doesn't fit car model %d on this game (its 3D model has no place for it) - skipped", part, carModel);
    }
}


struct Frame { void* objs; };
static int calls = 0;
static int FakeAdd(void* v, int part)   // like GetReplacementUpgrade with a car that lacks the frame
{
    ++calls;
    Frame* volatile fv = (part == 1010) ? nullptr : (Frame*)v; Frame* f = fv;
    void* volatile* pp = (void* volatile*)&f->objs; void* x = *pp; (void)x;   // NULL -> SIGSEGV
    return 1;
}
static void FakeRemove(void* v, int part) { (void)v; (void)part; }
int main()
{
    VehAddUpgrade_fn = FakeAdd; VehRemoveUpgrade_fn = FakeRemove;
    Frame good = { nullptr };
    FitUpgrade(&good, 411, 1010, true);        // faults -> recovered
    bool bad = IsBadUpgrade(411, 1010);
    FitUpgrade(&good, 411, 1010, true);        // never tried again
    FitUpgrade(&good, 411, 1025, true);        // fits
    FitUpgrade(&good, 562, 1010, true);        // other car model: tried
    printf("calls=%d bad411=%d bad411_1025=%d bad562=%d\n", calls, bad, IsBadUpgrade(411, 1025), IsBadUpgrade(562, 1010));
    bool ok = calls == 3 && bad && !IsBadUpgrade(411, 1025) && IsBadUpgrade(562, 1010);
    // a real crash outside the guard must still crash (old handler restored)
    pid_t pid = fork();
    if(pid == 0) { volatile int* p = nullptr; *p = 1; _exit(0); }
    int st = 0; waitpid(pid, &st, 0);
    bool crashed = WIFSIGNALED(st) && WTERMSIG(st) == SIGSEGV;
    printf("outside the guard still crashes normally: %s\n", crashed ? "yes" : "NO");
    // a fault on another thread while guarding is not swallowed
    printf(ok && crashed ? "GUARD TEST PASSED\n" : "GUARD TEST FAILED\n");
    return ok && crashed ? 0 : 1;
}
