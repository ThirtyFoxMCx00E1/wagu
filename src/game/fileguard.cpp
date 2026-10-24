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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <elf.h>
#include <link.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
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
    int HookSetPos(void* h, int p)  { if(Bad(h)) { Report("OS_FileSetPosition", h); return 3; } return SetPos_orig(h, p); }   // 3 = the game's own seek error code
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

// ---- 3.21: stdio guard --------------------------------------------------------------------------
// Crash log (Samsung A02, Android 11): SIGSEGV in libc __fseeko64 with FILE* = 0x1, called through
// the game's file layer (OS_FileSetPosition) - the handle itself was fine (so the 3.11.5 check above
// let it through), the FILE* inside it was not. The game's own calls to fseek / ftell / fread /
// fwrite / fclose / fgets / feof go through its import table (GOT): those slots now point to
// checks that refuse a FILE* below 64 KB (never a real one) and let every other call through.
namespace
{
    inline bool BadFp(FILE* f) { return (uintptr_t)f < 0x10000; }
    int    (*o_fseek)(FILE*, long, int) = fseek;
    long   (*o_ftell)(FILE*) = ftell;
    size_t (*o_fread)(void*, size_t, size_t, FILE*) = fread;
    size_t (*o_fwrite)(const void*, size_t, size_t, FILE*) = fwrite;
    int    (*o_fclose)(FILE*) = fclose;
    char*  (*o_fgets)(char*, int, FILE*) = fgets;
    int    (*o_feof)(FILE*) = feof;
    int    g_fseek(FILE* f, long o, int w) { if(BadFp(f)) { Report("fseek", f); errno = EBADF; return -1; } return o_fseek(f, o, w); }
    long   g_ftell(FILE* f) { if(BadFp(f)) { Report("ftell", f); errno = EBADF; return -1; } return o_ftell(f); }
    size_t g_fread(void* b, size_t s, size_t n, FILE* f) { if(BadFp(f) || (!b && s && n)) { Report("fread", f); return 0; } return o_fread(b, s, n, f); }
    size_t g_fwrite(const void* b, size_t s, size_t n, FILE* f) { if(BadFp(f)) { Report("fwrite", f); return 0; } return o_fwrite(b, s, n, f); }
    int    g_fclose(FILE* f) { if(BadFp(f)) { Report("fclose", f); return EOF; } return o_fclose(f); }
    char*  g_fgets(char* b, int n, FILE* f) { if(BadFp(f) || !b) { Report("fgets", f); return nullptr; } return o_fgets(b, n, f); }
    int    g_feof(FILE* f) { if(BadFp(f)) { Report("feof", f); return 1; } return o_feof(f); }

    struct GotPatch { const char* name; void* hook; };
    const GotPatch kGot[] =
    {
        { "fseek", (void*)&g_fseek }, { "ftell", (void*)&g_ftell }, { "fread", (void*)&g_fread }, { "fwrite", (void*)&g_fwrite },
        { "fclose", (void*)&g_fclose }, { "fgets", (void*)&g_fgets }, { "feof", (void*)&g_feof },
    };

    struct FindLib { uintptr_t base; const ElfW(Phdr)* phdr; int phnum; };
    int FindCb(struct dl_phdr_info* info, size_t, void* data)
    {
        FindLib* f = (FindLib*)data;
        if(info->dlpi_addr != f->base) return 0;
        f->phdr = info->dlpi_phdr; f->phnum = info->dlpi_phnum;
        return 1;
    }

    // walk the PLT + dynamic relocations of the library, point the GOT slots of kGot at the checks
    int PatchGot(uintptr_t base, const ElfW(Phdr)* ph, int phnum)
    {
        const ElfW(Dyn)* dyn = nullptr;
        for(int i = 0; i < phnum; ++i) if(ph[i].p_type == PT_DYNAMIC) dyn = (const ElfW(Dyn)*)(base + ph[i].p_vaddr);
        if(!dyn) return 0;
        const ElfW(Sym)* symtab = nullptr; const char* strtab = nullptr;
        uintptr_t jmprel = 0, rel = 0; size_t jmpsz = 0, relsz = 0;
        for(const ElfW(Dyn)* d = dyn; d->d_tag != DT_NULL; ++d)
        {
            switch(d->d_tag)
            {
                case DT_SYMTAB:   symtab = (const ElfW(Sym)*)(base + d->d_un.d_ptr); break;
                case DT_STRTAB:   strtab = (const char*)(base + d->d_un.d_ptr); break;
                case DT_JMPREL:   jmprel = base + d->d_un.d_ptr; break;
                case DT_PLTRELSZ: jmpsz = d->d_un.d_val; break;
#ifdef AML32
                case DT_REL:      rel = base + d->d_un.d_ptr; break;
                case DT_RELSZ:    relsz = d->d_un.d_val; break;
#else
                case DT_RELA:     rel = base + d->d_un.d_ptr; break;
                case DT_RELASZ:   relsz = d->d_un.d_val; break;
#endif
            }
        }
        if(!symtab || !strtab) return 0;
#ifdef AML32
        typedef ElfW(Rel) R;
        #define R_SYMI(i) ELF32_R_SYM(i)
#else
        typedef ElfW(Rela) R;
        #define R_SYMI(i) ELF64_R_SYM(i)
#endif
        long page = sysconf(_SC_PAGESIZE); if(page <= 0) page = 4096;
        int n = 0;
        const uintptr_t tabs[2] = { jmprel, rel }; const size_t sizes[2] = { jmpsz, relsz };
        for(int t = 0; t < 2; ++t)
        {
            if(!tabs[t] || !sizes[t]) continue;
            const R* r = (const R*)tabs[t];
            size_t cnt = sizes[t] / sizeof(R);
            for(size_t k = 0; k < cnt; ++k)
            {
                uint32_t si = (uint32_t)R_SYMI(r[k].r_info);
                if(!si) continue;
                const char* nm = strtab + symtab[si].st_name;
                for(const GotPatch& g : kGot)
                {
                    if(strcmp(nm, g.name)) continue;
                    void** slot = (void**)(base + r[k].r_offset);
                    uintptr_t pg = (uintptr_t)slot & ~(uintptr_t)(page - 1);
                    if(mprotect((void*)pg, page, PROT_READ | PROT_WRITE) != 0) break;
                    *slot = g.hook;   // the real libc function is called by the check (o_*)
                    ++n;
                    break;
                }
            }
        }
        return n;
    }
}

static void InitStdioGuard(void* h)
{
    void* any = (void*)aml->GetSym(h, "_Z18OS_FileSetPositionPvi");
    Dl_info di; memset(&di, 0, sizeof(di));
    if(!any || !dladdr(any, &di) || !di.dli_fbase) { logger->Info("FileGuard: stdio guard off (library not found)"); return; }
    FindLib f = { (uintptr_t)di.dli_fbase, nullptr, 0 };
    dl_iterate_phdr(FindCb, &f);
    if(!f.phdr) { logger->Info("FileGuard: stdio guard off (no program headers)"); return; }
    int n = PatchGot(f.base, f.phdr, f.phnum);
    logger->Info("FileGuard: stdio guard %s (%d import slots: fseek ftell fread fwrite fclose fgets feof)", n ? "on" : "off", n);
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
    InitStdioGuard(h);   // 3.21
}
int Game::FileGuardBlocked() { return s_nBlocked.load(); }
