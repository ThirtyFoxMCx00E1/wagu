#pragma once
// TYFMC: one home for the TYFMC mods (MP, MLG, CamFX).
//   data:   <game files>/TYFMC/<mod>        e.g. Android/data/com.rockstargames.gtasa/files/TYFMC/MP
//   config: <AML configs>/TYFMC.<mod>.ini
// Old folders and old ini files are moved in on start (same storage = an instant rename),
// so nothing is lost. A newer copy of an old folder (for example a zip unpacked into files/TYFMC/MP
// again) is merged in: its files replace the ones already there.
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string>
#include <vector>
#include <mod/amlmod.h>
#include <mod/iaml.h>

namespace TYFMC
{
inline bool Exists(const char* p) { struct stat st; return stat(p, &st) == 0; }
inline bool IsDir(const char* p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }
inline std::string Join(const char* a, const char* b)
{
    std::string s = a ? a : "";
    if(!s.empty() && s.back() != '/') s += '/';
    return s + b;
}
inline bool CopyFile(const char* src, const char* dst)
{
    FILE* in = fopen(src, "rb"); if(!in) return false;
    FILE* out = fopen(dst, "wb"); if(!out) { fclose(in); return false; }
    char buf[65536]; size_t n; bool ok = true;
    while((n = fread(buf, 1, sizeof(buf), in)) > 0) if(fwrite(buf, 1, n, out) != n) { ok = false; break; }
    fclose(in); if(fclose(out) != 0) ok = false;
    return ok;
}
// moves everything inside src into dst, then removes src
inline void MoveTree(const std::string& src, const std::string& dst, int depth = 0)
{
    if(depth > 16) return;
    mkdir(dst.c_str(), 0777);
    std::vector<std::string> names;
    if(DIR* d = opendir(src.c_str()))
    {
        while(dirent* e = readdir(d)) if(strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) names.push_back(e->d_name);
        closedir(d);
    }
    for(const std::string& n : names)
    {
        const std::string s = src + "/" + n, t = dst + "/" + n;
        if(IsDir(s.c_str()))
        {
            if(IsDir(t.c_str())) MoveTree(s, t, depth + 1);
            else { if(Exists(t.c_str())) remove(t.c_str()); if(rename(s.c_str(), t.c_str()) != 0) MoveTree(s, t, depth + 1); }
        }
        else
        {
            if(IsDir(t.c_str())) continue;
            if(Exists(t.c_str())) remove(t.c_str());
            if(rename(s.c_str(), t.c_str()) != 0 && CopyFile(s.c_str(), t.c_str())) remove(s.c_str());
        }
    }
    rmdir(src.c_str());   // only goes if it is empty now
}
// <game files>/TYFMC/<mod>, with the old folders (relative to <game files>) moved in
inline std::string DataDir(const char* mod, const char* const* oldNames, int nOld)
{
    const char* base = aml ? aml->GetAndroidDataPath() : nullptr;
    if(!base || !*base) base = "/sdcard/Android/data/com.rockstargames.gtasa/files";
    const std::string root = Join(base, "TYFMC"), dir = root + "/" + mod;
    mkdir(root.c_str(), 0777);
    for(int i = 0; i < nOld; ++i)
    {
        const std::string o = Join(base, oldNames[i]);
        if(IsDir(o.c_str())) MoveTree(o, dir);
    }
    mkdir(dir.c_str(), 0777);
    return dir;
}
// The TYFMC ini files live in <game files>/TYFMC too:  files/TYFMC/TYFMC.<mod>.ini
// AML's own Config always opens "<configs>/<name>.ini" and the configs folder sits next to files/
// (Android/data/<game>/configs and Android/data/<game>/files), so the Config name
// "../files/TYFMC/TYFMC.<mod>" points it at files/TYFMC/TYFMC.<mod>.ini.
inline std::string Root()
{
    const char* base = aml ? aml->GetAndroidDataPath() : nullptr;
    if(!base || !*base) base = "/sdcard/Android/data/com.rockstargames.gtasa/files";
    const std::string root = Join(base, "TYFMC");
    mkdir(root.c_str(), 0777);
    return root;
}
inline std::string IniPath(const char* mod) { return Root() + "/TYFMC." + mod + ".ini"; }
// moves an older ini in when files/TYFMC/TYFMC.<mod>.ini is not there yet:
// first configs/TYFMC.<mod>.ini, then configs/<oldName>.ini
inline void MigrateIni(const char* oldName, const char* mod)
{
    const std::string n = IniPath(mod);
    if(Exists(n.c_str()) || !aml || !aml->GetConfigPath()) return;
    const std::string cands[2] = { Join(aml->GetConfigPath(), (std::string("TYFMC.") + mod + ".ini").c_str()),
                                   Join(aml->GetConfigPath(), (std::string(oldName ? oldName : "") + ".ini").c_str()) };
    for(const std::string& o : cands)
    {
        if(!Exists(o.c_str())) continue;
        if(rename(o.c_str(), n.c_str()) != 0 && CopyFile(o.c_str(), n.c_str())) remove(o.c_str());
        return;
    }
}
}

// Runs after AML hands over its interface (priority 101) and before the mod's Config object reads
// its ini (default priority), so the moved file is the one that gets loaded.
#define TYFMC_INI_MIGRATE(_old, _mod) \
    namespace { struct TyfmcIniStub { TyfmcIniStub() { TYFMC::MigrateIni(#_old, #_mod); } }; } \
    static TyfmcIniStub tyfmcIniStub __attribute__((init_priority(102)));

// MYMODCFGNAME with the ini in files/TYFMC/TYFMC.<mod>.ini (the old configs/<_old>.ini is moved there)
#define TYFMC_MODCFG(_guid, _name, _version, _author, _mod, _old) \
    MYMOD(_guid, _name, _version, _author); \
    TYFMC_INI_MIGRATE(_old, _mod) \
    static Config cfgLocal("../files/TYFMC/TYFMC." #_mod); \
    Config* cfg = &cfgLocal;
