// MP 4.13.32: MonetLoader for MP built into libMP32.so / libMP64.so.
// The library (monetloader/<abi>/libmonetloader.so, src/embed/monetloader_blob.S) is written to the game's private
// folder (aml->GetDataPath(), the same kind of folder AML runs the mods from) and started like AML starts a mod
// (OnModLoad). Off with [MP] MonetLoader = 0 in TYFMC.MP.ini. A separate libmonetloader.so in the mods folder wins
// (only one MonetLoader may run) - MP then does not start its own copy.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

extern "C" const unsigned char mp_monetloader_blob[];
extern "C" const unsigned char mp_monetloader_blob_end[];

namespace MonetEmbed
{
static void* s_hLib = nullptr;

static bool SameFile(const char* path, const unsigned char* data, size_t n)
{
    struct stat st;
    if(stat(path, &st) != 0 || (size_t)st.st_size != n) return false;
    FILE* f = fopen(path, "rb");
    if(!f) return false;
    std::vector<unsigned char> buf(1 << 16);
    size_t off = 0; bool same = true;
    while(same && off < n)
    {
        size_t want = n - off < buf.size() ? n - off : buf.size();
        size_t got = fread(buf.data(), 1, want, f);
        if(got != want || memcmp(buf.data(), data + off, got) != 0) same = false;
        off += got;
    }
    fclose(f);
    return same;
}

static bool WriteFile(const char* path, const unsigned char* data, size_t n)
{
    std::string tmp = std::string(path) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if(!f) { logger->Error("MonetLoader: can't write %s (errno %d)", tmp.c_str(), errno); return false; }
    bool ok = fwrite(data, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;
    if(!ok) { unlink(tmp.c_str()); logger->Error("MonetLoader: writing %s failed (full storage?)", tmp.c_str()); return false; }
    chmod(tmp.c_str(), 0700);
    if(rename(tmp.c_str(), path) != 0) { unlink(tmp.c_str()); logger->Error("MonetLoader: rename to %s failed (errno %d)", path, errno); return false; }
    return true;
}

void Start()
{
    if(s_hLib) return;
    ConfigEntry* e = cfg->Bind("MonetLoader", true, "MP");   // written to TYFMC.MP.ini so it can be switched off
    const bool on = e->GetBool(); delete e; cfg->Save();
    if(!on) { logger->Info("MonetLoader: off (MonetLoader = 0 in TYFMC.MP.ini)"); return; }
    if(aml->HasMod("com.monetloader.aml") || dlopen("libmonetloader.so", RTLD_NOW | RTLD_NOLOAD))
    {
        logger->Info("MonetLoader: a separate libmonetloader.so is in the mods folder - that one runs, MP's built-in copy is not started. Delete it from the mods folder to use the built-in MonetLoader for MP.");
        return;
    }
    const size_t n = (size_t)(mp_monetloader_blob_end - mp_monetloader_blob);
    if(n < 4096 || memcmp(mp_monetloader_blob, "\x7f" "ELF", 4) != 0) { logger->Error("MonetLoader: the built-in copy is missing"); return; }
    const char* dp = aml->GetDataPath();
    if(!dp || !dp[0]) { logger->Error("MonetLoader: no private game folder"); return; }
    std::string dir = dp;
    if(dir.back() != '/') dir += '/';
    std::string path = dir + "libMP_monetloader.so";
    if(!SameFile(path.c_str(), mp_monetloader_blob, n))
    {
        if(!WriteFile(path.c_str(), mp_monetloader_blob, n)) return;
        logger->Info("MonetLoader: built-in copy written to %s (%u KB)", path.c_str(), (unsigned)(n / 1024));
    }
    s_hLib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if(!s_hLib) { const char* er = dlerror(); logger->Error("MonetLoader: can't load the built-in copy: %s", er ? er : "?"); return; }
    typedef void (*EntryFn)();
    EntryFn entry = (EntryFn)dlsym(s_hLib, "OnModLoad");
    if(!entry) { logger->Error("MonetLoader: the built-in copy has no OnModLoad"); return; }
    logger->Info("MonetLoader for MP: starting (built into MP). Scripts: files/TYFMC/MP/moonloader, log: moonloader/moonloader.log");
    entry();
    logger->Info("MonetLoader for MP: started");
}

// 4.13.33: the running MonetLoader (MP's built-in copy, or a separate libmonetloader.so in the mods folder)
void* Handle()
{
    if(s_hLib) return s_hLib;
    static void* s_hExt = nullptr;
    if(!s_hExt) s_hExt = dlopen("libmonetloader.so", RTLD_NOW | RTLD_NOLOAD);
    return s_hExt;
}
}
