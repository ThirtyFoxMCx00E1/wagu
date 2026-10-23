// 3.11.5: file guard for the game's own file layer (OS_File*).
// Crash reports (SIGSEGV in OS_FileSetPosition with a null handle, SIGABRT from the same place on
// another phone) come from the game's streaming thread reading a model through a file handle that
// is null or already closed - a known GTA SA mobile crash with IMG / streaming / limit mods.
// A null handle now just makes that one read fail (the game retries or skips the model) instead of
// taking the whole game down. Valid handles are untouched.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <atomic>
#include <stdint.h>
#include "game.h"

namespace
{
    std::atomic<int> s_nBlocked{0};

    // null / tiny pointer = never a real handle. (Closed handles are NOT tracked on purpose: the game
    // reuses memory, and blocking a valid file that got the same address would break model loading.)
    inline bool Bad(void* h) { return (uintptr_t)h < 0x10000; }
    void Report(const char* fn, void* h)
    {
        int n = ++s_nBlocked;
        if(n <= 5 || (n % 100) == 0) logger->Error("FileGuard: %s on a bad file handle %p blocked (crash avoided, %d so far)", fn, h, n);
    }

    int (*Close_orig)(void*) = nullptr;
    int (*SetPos_orig)(void*, int) = nullptr;
    int (*Read_orig)(void*, void*, int) = nullptr;
    int (*Size_orig)(void*) = nullptr;
    int (*GetPos_orig)(void*) = nullptr;

    int HookClose(void* h)          { if(Bad(h)) { Report("OS_FileClose", h); return 0; } return Close_orig(h); }
    int HookSetPos(void* h, int p)  { if(Bad(h)) { Report("OS_FileSetPosition", h); return 3; } return SetPos_orig(h, p); }   // 2 = OSFileError read error
    int HookRead(void* h, void* b, int n) { if(Bad(h) || (!b && n > 0)) { Report("OS_FileRead", h); return 3; } return Read_orig(h, b, n); }
    int HookSize(void* h)           { if(Bad(h)) { Report("OS_FileSize", h); return 0; } return Size_orig(h); }
    int HookGetPos(void* h)         { if(Bad(h)) { Report("OS_FileGetPosition", h); return 0; } return GetPos_orig(h); }

    void H(void* lib, const char* sym, void* hook, void** orig, int& ok)
    {
        void* f = (void*)aml->GetSym(lib, sym);
        if(!f) return;
        aml->Hook(f, hook, orig);
        if(*orig) ++ok;
    }
}

void Game::InitFileGuard(void* h)
{
    int ok = 0;
    H(h, "_Z18OS_FileSetPositionPvi", (void*)&HookSetPos, (void**)&SetPos_orig, ok);
    H(h, "_Z11OS_FileReadPvS_i", (void*)&HookRead, (void**)&Read_orig, ok);
    H(h, "_Z11OS_FileSizePv", (void*)&HookSize, (void**)&Size_orig, ok);
    H(h, "_Z18OS_FileGetPositionPv", (void*)&HookGetPos, (void**)&GetPos_orig, ok);
    H(h, "_Z12OS_FileClosePv", (void*)&HookClose, (void**)&Close_orig, ok);
    logger->Info("FileGuard: %d/5 game file functions guarded", ok);
}
int Game::FileGuardBlocked() { return s_nBlocked.load(); }
