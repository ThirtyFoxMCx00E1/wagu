// 3.21.3: script fault guard - the part without AML / Android, so test/scriptfault_test.cpp can run
// it on Linux.
//
// Why: CLEO scripts keep handles of people / cars they picked up earlier. When MP joins a server it
// turns the phone's own street people and traffic off and removes the ones around you (the server's
// players and NPCs replace them). A script that then asks the game about "its" ped - HAS_CHAR_GOT_WEAPON
// (0491), LOCATE_CHAR_*_OBJECT (0471), ... - makes the game read a dead pointer: SIGSEGV in
// CRunningScript::ProcessCommands1100To1199 (realme RE6054 logs, every time you tap Connect).
// There are hundreds of opcodes that take a char / car / object, so instead of a list:
//
//   * every opcode range of the game (ProcessCommands0To99 ... 2600To2699) runs inside a fault guard;
//   * when an opcode faults, we jump back out of it, the game does NOT crash;
//   * that script is parked on that opcode (it stops there, every other script and the game go on);
//     the AML log names the script and the opcode. MP's own internal script is never parked.
#pragma once
#include <setjmp.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

namespace ScriptFault
{
    // ---- parked scripts: (script, opcode position) ----
    enum { MAX_PARKED = 64 };
    struct Parked { void* script; uintptr_t ip; uint16_t opcode; };
    struct Table
    {
        Parked p[MAX_PARKED];
        int    n = 0;
        // the script sits on the parked opcode again -> stays parked
        bool IsParked(void* script, uintptr_t ip) const
        {
            for(int i = 0; i < n; ++i) if(p[i].script == script && p[i].ip == ip) return true;
            return false;
        }
        // a script slot that moved on (the game reused it for a new script) is no longer parked
        void Forget(void* script, uintptr_t ip)
        {
            for(int i = 0; i < n; ++i) if(p[i].script == script && p[i].ip != ip) { p[i] = p[--n]; return; }
        }
        bool Add(void* script, uintptr_t ip, uint16_t opcode)
        {
            if(IsParked(script, ip)) return true;
            for(int i = 0; i < n; ++i) if(p[i].script == script) { p[i].ip = ip; p[i].opcode = opcode; return true; }
            if(n >= MAX_PARKED) return false;
            p[n].script = script; p[n].ip = ip; p[n].opcode = opcode; ++n;
            return true;
        }
    };

    // ---- the signal side ----
    struct Guard
    {
        sigjmp_buf* jmp;          // innermost guarded call on the guard thread, null = none
        pthread_t   thread;
        bool        threadSet;
    };
    inline Guard& G() { static Guard g = { nullptr, pthread_t(), false }; return g; }
    inline struct sigaction& OldSegv() { static struct sigaction s; return s; }
    inline struct sigaction& OldBus()  { static struct sigaction s; return s; }
    inline volatile sig_atomic_t& Faults() { static volatile sig_atomic_t n = 0; return n; }

    inline void Handler(int sig, siginfo_t* si, void* uc)
    {
        Guard& g = G();
        if(g.jmp && g.threadSet && pthread_equal(pthread_self(), g.thread))
        {
            sigjmp_buf* j = g.jmp; g.jmp = nullptr; ++Faults();
            siglongjmp(*j, 1);
        }
        // not ours: whoever was there before (AML's crash log, the system)
        struct sigaction* old = sig == SIGBUS ? &OldBus() : &OldSegv();
        if(old->sa_flags & SA_SIGINFO) { if(old->sa_sigaction) { old->sa_sigaction(sig, si, uc); return; } }
        else if(old->sa_handler != SIG_DFL && old->sa_handler != SIG_IGN) { old->sa_handler(sig); return; }
        signal(sig, SIG_DFL); raise(sig);
    }
    // (re)install: when another mod replaced our handler, put ours back in front of it (it is chained)
    inline bool Install()
    {
        struct sigaction cur; memset(&cur, 0, sizeof(cur));
        if(sigaction(SIGSEGV, nullptr, &cur) == 0 && (cur.sa_flags & SA_SIGINFO) && cur.sa_sigaction == Handler) return false;
        struct sigaction sa; memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = Handler;
        // SA_NODEFER: we leave the handler with siglongjmp and a cheap sigsetjmp(.., 0), so SIGSEGV
        // must not stay blocked afterwards (a 2nd fault would then kill the game without a log)
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGSEGV, &sa, &OldSegv());
        sigaction(SIGBUS, &sa, &OldBus());
        return true;
    }

    // Runs fn(script, opcode) guarded. Returns false when it faulted.
    template<typename R, typename S>
    inline bool Call(R (*fn)(S, int), S script, int opcode, R& out)
    {
        Guard& g = G();
        if(!g.threadSet) { g.thread = pthread_self(); g.threadSet = true; }
        if(!pthread_equal(pthread_self(), g.thread)) { out = fn(script, opcode); return true; }   // scripts run on one thread only
        sigjmp_buf here;
        sigjmp_buf* outer = g.jmp;
        if(sigsetjmp(here, 0) == 0)
        {
            g.jmp = &here;
            out = fn(script, opcode);
            g.jmp = outer;
            return true;
        }
        g.jmp = outer;
        return false;
    }
}
