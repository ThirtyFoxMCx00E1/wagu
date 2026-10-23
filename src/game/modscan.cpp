// 2.37: CheatMenu detection - see modscan.h
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <dlfcn.h>
#include <stdint.h>
#include <string>
#include <vector>
#include "modscan.h"
#include "modscan_core.h"

namespace
{
    bool   s_bFound = false;
    char   s_szWhat[96] = "";
    double s_fLastScan = -100.0;

    // Library file names of the cheat menu (32-bit and 64-bit builds)
    const char* const kLibs[] = { "libCheatMenu.so", "libCheatMenu64.so" };

    // GUIDs the cheat menu has been published under. aml->HasMod() is the
    // official AML way; the lib / folder checks below catch renamed GUIDs.
    const char* const kGuids[] = {
        "CheatMenu", "cheatmenu", "net.cheatmenu", "com.cheatmenu", "org.cheatmenu",
        "net.grinch.cheatmenu", "net.grinch_.cheatmenu", "com.grinch.cheatmenu",
        "net.rusjj.cheatmenu", "com.rusjj.cheatmenu", "net.cheatmenu.gtasa", "gtasa.cheatmenu",
    };

    double Now()
    {
        timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts.tv_sec + ts.tv_nsec / 1e9;
    }

    void Found(const char* what, const char* how)
    {
        if(!s_bFound) logger->Info("ModScan: %s detected (%s) - joining servers is disabled", what, how);
        s_bFound = true;
        snprintf(s_szWhat, sizeof(s_szWhat), "%s", what);
    }

    bool FileExists(const char* path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }

    // case-insensitive "contains libcheatmenu" for a path / map line
    const char* FindCheatLib(const char* line)
    {
        const char* p = strrchr(line, '/');
        p = p ? p + 1 : line;
        for(const char* lib : kLibs) if(strncasecmp(p, lib, strlen(lib)) == 0) return lib;
        return NULL;
    }

    void ScanMaps()
    {
        FILE* f = fopen("/proc/self/maps", "r");
        if(!f) return;
        char line[512];
        while(fgets(line, sizeof(line), f))
        {
            size_t n = strlen(line);
            while(n && (line[n-1] == '\n' || line[n-1] == ' ')) line[--n] = 0;
            if(const char* lib = FindCheatLib(line)) { Found(lib, "loaded"); break; }
        }
        fclose(f);
    }

    void ScanFolder(const char* base)
    {
        if(!base || !*base) return;
        static const char* const kSub[] = { "mods", "files/mods", "../mods" };
        char path[600];
        for(const char* sub : kSub)
            for(const char* lib : kLibs)
            {
                snprintf(path, sizeof(path), "%s/%s/%s", base, sub, lib);
                if(FileExists(path)) { Found(lib, "installed"); return; }
            }
    }

    // ---------------------------------------------------------------- 2.37: renamed libs
    // Players renamed libCheatMenu.so, so the file name is not enough: every .so in the mods folders
    // (loaded or not) is read and checked for the menu's own ImGui label (see modscan_core.h).
    struct Seen { std::string path; long long size; long long mtime; bool hit; };
    std::vector<Seen> s_Seen;
    std::string s_SelfPath;
    bool s_bNeutralized = false;

    bool ContentHit(const char* path)
    {
        struct stat st;
        if(stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 1024 || st.st_size > 96ll * 1024 * 1024) return false;
        for(auto& e : s_Seen) if(e.path == path && e.size == st.st_size && e.mtime == (long long)st.st_mtime) return e.hit;
        bool hit = false;
        if(FILE* f = fopen(path, "rb"))
        {
            ModScanCore::Matcher m;
            static unsigned char buf[256 * 1024];
            size_t n;
            while((n = fread(buf, 1, sizeof(buf), f)) > 0) m.Feed(buf, n);
            fclose(f);
            hit = m.IsCheatMenu();
        }
        s_Seen.push_back({ path, (long long)st.st_size, (long long)st.st_mtime, hit });
        return hit;
    }

    bool IsSelf(const char* path) { return !s_SelfPath.empty() && s_SelfPath == path; }

    const char* BaseName(const char* path) { const char* b = strrchr(path, '/'); return b ? b + 1 : path; }

    // loaded libs from the mods folders: returns the hit's path in `out`
    bool ScanMapsContent(std::string& out, uintptr_t& base)
    {
        FILE* f = fopen("/proc/self/maps", "r");
        if(!f) return false;
        char line[600];
        std::vector<std::string> done;
        bool found = false;
        while(!found && fgets(line, sizeof(line), f))
        {
            size_t n = strlen(line);
            while(n && (line[n-1] == '\n' || line[n-1] == ' ')) line[--n] = 0;
            const char* path = strchr(line, '/');
            if(!path || !ModScanCore::IsModPath(path) || IsSelf(path)) continue;
            bool again = false;
            for(auto& d : done) if(d == path) { again = true; break; }
            if(again) continue;
            done.push_back(path);
            if(ContentHit(path)) { out = path; base = (uintptr_t)strtoull(line, NULL, 16); found = true; }
        }
        fclose(f);
        return found;
    }

    bool ScanFolderContent(const char* base, std::string& out)
    {
        if(!base || !*base) return false;
        static const char* const kSub[] = { "mods", "files/mods", "../mods" };
        char dirp[600], path[900];
        for(const char* sub : kSub)
        {
            snprintf(dirp, sizeof(dirp), "%s/%s", base, sub);
            DIR* d = opendir(dirp);
            if(!d) continue;
            while(dirent* e = readdir(d))
            {
                size_t l = strlen(e->d_name);
                if(l < 4 || strcasecmp(e->d_name + l - 3, ".so") != 0) continue;
                snprintf(path, sizeof(path), "%s/%s", dirp, e->d_name);
                if(IsSelf(path) || !strcmp(e->d_name, BaseName(s_SelfPath.c_str()))) continue;
                if(ContentHit(path)) { out = path; closedir(d); return true; }
            }
            closedir(d);
        }
        return false;
    }

    // ---------------------------------------------------------------- 2.37: hide the button
    // The menu draws with its own (statically linked) Dear ImGui. Its Button / ButtonEx / SmallButton
    // are hooked: a "CheatMenu" / "Cheat Menu" button draws nothing and never reports a press, so the
    // little button at the bottom of the screen is gone and the menu cannot be opened. If the window
    // opens anyway, its Begin() is moved off-screen, closed (p_open = false) and reports "collapsed",
    // so none of its items are drawn or usable.
    struct GVec2 { float x, y; };
    bool (*o_ButtonEx)(const char*, const GVec2&, int) = nullptr;
    bool (*o_Button)(const char*, const GVec2&) = nullptr;
    bool (*o_SmallButton)(const char*) = nullptr;
    bool (*o_Begin)(const char*, bool*, int) = nullptr;
    void (*f_SetNextWindowPos)(const GVec2&, int, const GVec2&) = nullptr;
    int  s_nHidden = 0;
    bool s_bAllowed = false;   // 3.00: the server allows Cheat Menu -> hooks pass everything through

    void Hid(const char* what, const char* label)
    {
        if(++s_nHidden <= 3) logger->Info("ModScan: cheat menu %s \"%s\" hidden", what, label);
    }
    bool H_ButtonEx(const char* l, const GVec2& sz, int fl) { if(!s_bAllowed && ModScanCore::IsCheatLabel(l)) { Hid("button", l); return false; } return o_ButtonEx(l, sz, fl); }
    bool H_Button(const char* l, const GVec2& sz)           { if(!s_bAllowed && ModScanCore::IsCheatLabel(l)) { Hid("button", l); return false; } return o_Button(l, sz); }
    bool H_SmallButton(const char* l)                        { if(!s_bAllowed && ModScanCore::IsCheatLabel(l)) { Hid("button", l); return false; } return o_SmallButton(l); }
    bool H_Begin(const char* name, bool* open, int fl)
    {
        if(s_bAllowed || !ModScanCore::IsCheatWindow(name)) return o_Begin(name, open, fl);
        Hid("window", name ? name : "");
        if(f_SetNextWindowPos) { GVec2 far = { -20000.0f, -20000.0f }, piv = { 0, 0 }; f_SetNextWindowPos(far, 1 /*ImGuiCond_Always*/, piv); }
        o_Begin(name, open, fl);   // Begin and End must stay paired: the menu still calls End()
        if(open) *open = false;
        return false;              // "collapsed": the menu skips all of its items
    }

    void* Sym(void* handle, uintptr_t base, const char* name)
    {
        void* p = handle ? (void*)aml->GetSym(handle, name) : nullptr;
        if(!p && base) p = (void*)aml->GetSym(base, name);
        return p;
    }

    template<class F> void HookOne(void* handle, uintptr_t base, const char* sym, void* hook, F& orig, int& ok)
    {
        void* fn = Sym(handle, base, sym);
        if(fn && aml->Hook(fn, hook, (void**)&orig) && orig) ++ok;
    }

    void Neutralize(const char* path, uintptr_t base)
    {
        if(s_bNeutralized || !aml || !path || !*path) return;
        s_bNeutralized = true;
        void* h = dlopen(path, RTLD_NOW | RTLD_NOLOAD);   // only if it is already loaded
        if(!h && !base) { logger->Info("ModScan: %s is installed but not loaded - nothing to hide", BaseName(path)); return; }
        int ok = 0;
        HookOne(h, base, "_ZN5ImGui8ButtonExEPKcRK6ImVec2i", (void*)&H_ButtonEx, o_ButtonEx, ok);
        HookOne(h, base, "_ZN5ImGui6ButtonEPKcRK6ImVec2", (void*)&H_Button, o_Button, ok);
        HookOne(h, base, "_ZN5ImGui11SmallButtonEPKc", (void*)&H_SmallButton, o_SmallButton, ok);
        HookOne(h, base, "_ZN5ImGui5BeginEPKcPbi", (void*)&H_Begin, o_Begin, ok);
        SET_TO(f_SetNextWindowPos, Sym(h, base, "_ZN5ImGui16SetNextWindowPosERK6ImVec2iS2_"));
        logger->Info("ModScan: %s - %d ImGui hooks placed (button / window hidden)%s", BaseName(path), ok,
                     ok ? "" : ", its ImGui is not exported: still blocked from every server");
    }
}

namespace
{
    char      s_szPath[512] = "";   // the cheat lib's path (for the hooks)
    uintptr_t s_uBase = 0;

    // path + load address of a lib that is mapped under that file name
    void PathFromMaps(const char* libName)
    {
        FILE* f = fopen("/proc/self/maps", "r");
        if(!f) return;
        char line[600];
        while(fgets(line, sizeof(line), f))
        {
            size_t n = strlen(line);
            while(n && (line[n-1] == '\n' || line[n-1] == ' ')) line[--n] = 0;
            const char* path = strchr(line, '/');
            if(path && strcasecmp(BaseName(path), libName) == 0)
            { snprintf(s_szPath, sizeof(s_szPath), "%s", path); s_uBase = (uintptr_t)strtoull(line, NULL, 16); break; }
        }
        fclose(f);
    }

    void ScanAll()
    {
        // 1) the official AML way + the known file names
        for(const char* g : kGuids)
            if(aml->HasMod(g)) { Found("CheatMenu", g); return; }
        for(const char* lib : kLibs)
            if(aml->GetLib(lib)) { Found(lib, "aml->GetLib"); PathFromMaps(lib); return; }
        ScanMaps();
        if(s_bFound) { PathFromMaps(s_szWhat); return; }

        // 2) 2.37: renamed lib - check what is inside every mod lib (loaded first, then the folders)
        std::string path; uintptr_t base = 0;
        if(ScanMapsContent(path, base))
        {
            Found(BaseName(path.c_str()), "renamed, found by its CheatMenu label");
            snprintf(s_szPath, sizeof(s_szPath), "%s", path.c_str()); s_uBase = base;
            return;
        }
        const char* roots[] = { aml->GetAndroidDataRootPath(), aml->GetAndroidDataPath(), aml->GetDataPath() };
        for(const char* r : roots)
        {
            ScanFolder(r);
            if(s_bFound) return;
            if(ScanFolderContent(r, path)) { Found(BaseName(path.c_str()), "renamed, installed"); return; }
        }
    }
}

void ModScan::Scan()
{
    s_fLastScan = Now();
    if(s_SelfPath.empty()) { Dl_info di; if(dladdr((void*)&ModScan::Scan, &di) && di.dli_fname) s_SelfPath = di.dli_fname; }
    if(!aml) { ScanMaps(); return; }
    if(!s_bFound) ScanAll();
    if(s_bFound && s_szPath[0]) Neutralize(s_szPath, s_uBase);   // hide its button / window
}

bool ModScan::CheatMenu() { return s_bFound; }

bool ModScan::Recheck()
{
    if(!s_bFound && Now() - s_fLastScan > 3.0) Scan();
    return s_bFound;
}

const char* ModScan::What() { return s_szWhat[0] ? s_szWhat : "CheatMenu"; }

void ModScan::SetAllowed(bool on) { s_bAllowed = on; }
