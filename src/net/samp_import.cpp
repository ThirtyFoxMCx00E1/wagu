// 4.11: import a PC SA-MP server folder (see samp_import.h).
// .rar: UnRAR source code by Alexander Roshal (third_party/unrar, its licence.txt): "UnRAR source code
// may be used in any software to handle RAR archives without limitations free of charge, but cannot be
// used to develop RAR (WinRAR) compatible archiver and to re-create RAR compression algorithm, which is
// proprietary. Distribution of modified UnRAR source code in separate form or as a part of other
// software is permitted, provided that full text of this paragraph, starting from "UnRAR source code"
// words, is included in license, or in documentation if license is not available, and in source code
// comments of resulting package."
#include "samp_import.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <strings.h>
#include <errno.h>
#include <vector>
#include <deque>
#include <sys/stat.h>
#include <dirent.h>
extern "C" {
#include "../../third_party/puff/puff.h"   // zlib contrib/puff (Mark Adler, zlib licence): raw deflate
}

#ifndef _UNIX
#define _UNIX
#endif
#include "../../third_party/unrar/dll.hpp"
#undef HANDLE
#undef LPARAM
#undef LONG
#undef UINT
#undef CALLBACK
#undef PASCAL

static bool IsDir(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
static void MkDirs(const std::string& p)
{
    std::string cur;
    for(size_t i = 0; i < p.size(); ++i)
    {
        cur += p[i];
        if((p[i] == '/' && i > 0) || i + 1 == p.size()) mkdir(cur.c_str(), 0755);
    }
}
static std::string Lower(std::string s) { for(auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool EndsWith(const std::string& s, const char* e) { size_t n = strlen(e); return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, e); }

std::string SampFindRoot(const std::string& dir)
{
    std::deque<std::pair<std::string, int>> q; q.push_back({ dir, 0 });
    while(!q.empty())
    {
        auto cur = q.front(); q.pop_front();
        DIR* d = opendir(cur.first.c_str()); if(!d) continue;
        std::vector<std::string> subs; bool gm = false, cfg = false;
        while(dirent* e = readdir(d))
        {
            if(e->d_name[0] == '.') continue;
            std::string n = e->d_name, full = cur.first + "/" + n;
            if(IsDir(full)) { if(Lower(n) == "gamemodes") gm = true; else subs.push_back(full); }
            else if(Lower(n) == "server.cfg") cfg = true;
        }
        closedir(d);
        if(gm) return cur.first;
        if(cfg && subs.empty()) return cur.first;
        if(cur.second < 5) for(auto& s : subs) q.push_back({ s, cur.second + 1 });
    }
    return "";
}

// ---- .zip (stored + deflate) -------------------------------------------------------------------
static uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static bool SafeName(std::string& n)
{
    for(auto& c : n) if(c == '\\') c = '/';
    while(!n.empty() && n[0] == '/') n.erase(0, 1);
    if(n.empty() || n.find("../") != std::string::npos || n == ".." || n.find(':') != std::string::npos) return false;
    return true;
}
static bool UnZip(const std::string& path, const std::string& dest, std::string& msg)
{
    FILE* f = fopen(path.c_str(), "rb"); if(!f) { msg = "can't open the file"; return false; }
    fseek(f, 0, SEEK_END); long size = ftell(f);
    long tail = size < 70000 ? size : 70000;
    std::vector<uint8_t> t((size_t)tail); fseek(f, size - tail, SEEK_SET);
    if(fread(t.data(), 1, t.size(), f) != t.size()) { fclose(f); msg = "read error"; return false; }
    long eocd = -1;
    for(long i = tail - 22; i >= 0; --i) if(U32(&t[i]) == 0x06054b50) { eocd = i; break; }
    if(eocd < 0) { fclose(f); msg = "not a zip (no end record)"; return false; }
    uint16_t count = U16(&t[eocd + 10]); uint32_t cdSize = U32(&t[eocd + 12]), cdOff = U32(&t[eocd + 16]);
    std::vector<uint8_t> cd(cdSize); fseek(f, cdOff, SEEK_SET);
    if(fread(cd.data(), 1, cd.size(), f) != cd.size()) { fclose(f); msg = "read error"; return false; }
    size_t p = 0; int done = 0, failed = 0;
    for(int i = 0; i < count && p + 46 <= cd.size(); ++i)
    {
        if(U32(&cd[p]) != 0x02014b50) break;
        uint16_t method = U16(&cd[p + 10]), flags = U16(&cd[p + 8]);
        uint32_t csize = U32(&cd[p + 20]), usize = U32(&cd[p + 24]), loff = U32(&cd[p + 42]);
        uint16_t nl = U16(&cd[p + 28]), xl = U16(&cd[p + 30]), cl = U16(&cd[p + 32]);
        std::string name((const char*)&cd[p + 46], nl);
        p += 46 + nl + xl + cl;
        if(!SafeName(name)) continue;
        std::string out = dest + "/" + name;
        if(name.back() == '/') { MkDirs(out); continue; }
        if(flags & 1) { ++failed; continue; }   // encrypted
        uint8_t lh[30]; fseek(f, loff, SEEK_SET);
        if(fread(lh, 1, 30, f) != 30 || U32(lh) != 0x04034b50) { ++failed; continue; }
        fseek(f, loff + 30 + U16(lh + 26) + U16(lh + 28), SEEK_SET);
        std::vector<uint8_t> in(csize), data;
        if(csize && fread(in.data(), 1, csize, f) != csize) { ++failed; continue; }
        if(method == 0) data = in;
        else if(method == 8)
        {
            data.resize(usize ? usize : 1);
            unsigned long dl = usize, sl = csize;
            if(puff(data.data(), &dl, in.data(), &sl) != 0) { ++failed; continue; }
            data.resize(dl);
        }
        else { ++failed; continue; }
        size_t sl = out.find_last_of('/'); if(sl != std::string::npos) MkDirs(out.substr(0, sl));
        FILE* o = fopen(out.c_str(), "wb"); if(!o) { ++failed; continue; }
        if(!data.empty()) fwrite(data.data(), 1, data.size(), o);
        fclose(o); ++done;
    }
    fclose(f);
    char b[96]; snprintf(b, sizeof(b), "%d files from the zip%s", done, failed ? " (some skipped: encrypted / unsupported)" : "");
    msg = b;
    return done > 0;
}

// ---- .rar (RAR 1.5 - 5.x) ---------------------------------------------------------------------
static bool UnRar(const std::string& path, const std::string& dest, std::string& msg)
{
    RAROpenArchiveDataEx a; memset(&a, 0, sizeof(a));
    std::vector<char> arc(path.begin(), path.end()); arc.push_back(0);
    a.ArcName = arc.data(); a.OpenMode = RAR_OM_EXTRACT;
    void* h = RAROpenArchiveEx(&a);
    if(!h || a.OpenResult != ERAR_SUCCESS) { char b[64]; snprintf(b, sizeof(b), "can't open the rar (error %u)", a.OpenResult); msg = b; if(h) RARCloseArchive(h); return false; }
    std::vector<char> dst(dest.begin(), dest.end()); dst.push_back(0);
    RARHeaderDataEx hd; int done = 0, failed = 0, r;
    while(true)
    {
        memset(&hd, 0, sizeof(hd));
        r = RARReadHeaderEx(h, &hd);
        if(r != ERAR_SUCCESS) break;
        int pr = RARProcessFile(h, RAR_EXTRACT, dst.data(), nullptr);
        if(pr == ERAR_SUCCESS) ++done; else ++failed;
    }
    RARCloseArchive(h);
    char b[96]; snprintf(b, sizeof(b), "%d files from the rar%s", done, failed ? " (some failed: password / broken)" : "");
    msg = b;
    return done > 0;
}

static bool CopyFile(const std::string& a, const std::string& b)
{
    FILE* i = fopen(a.c_str(), "rb"); if(!i) return false;
    FILE* o = fopen(b.c_str(), "wb"); if(!o) { fclose(i); return false; }
    char buf[65536]; size_t n; while((n = fread(buf, 1, sizeof(buf), i)) > 0) fwrite(buf, 1, n, o);
    fclose(i); fclose(o); return true;
}

bool SampImport(const std::string& src, const std::string& destDir, std::string& root, std::string& msg)
{
    root.clear();
    if(IsDir(src))
    {
        root = SampFindRoot(src);
        if(root.empty()) { msg = "no gamemodes/ folder in it"; return false; }
        msg = "folder"; return true;
    }
    uint8_t mg[8] = { 0 };
    { FILE* f = fopen(src.c_str(), "rb"); if(!f) { msg = "can't open the file"; return false; } size_t n = fread(mg, 1, 8, f); fclose(f); (void)n; }
    MkDirs(destDir);
    bool ok;
    if(!memcmp(mg, "Rar!", 4)) ok = UnRar(src, destDir, msg);
    else if(mg[0] == 'P' && mg[1] == 'K') ok = UnZip(src, destDir, msg);
    else if(!memcmp(mg, "7z\xBC\xAF", 4)) { msg = ".7z is not supported yet - re-pack it as .zip or .rar"; return false; }
    else if(EndsWith(src, ".amx"))
    {   // one compiled gamemode: make the folder SA-MP wants
        std::string base = src.substr(src.find_last_of('/') + 1); base.resize(base.size() - 4);
        MkDirs(destDir + "/gamemodes"); MkDirs(destDir + "/filterscripts"); MkDirs(destDir + "/scriptfiles");
        if(!CopyFile(src, destDir + "/gamemodes/" + base + ".amx")) { msg = "can't copy the .amx"; return false; }
        FILE* c = fopen((destDir + "/server.cfg").c_str(), "wb");
        if(c) { fprintf(c, "gamemode0 %s 1\nfilterscripts \n", base.c_str()); fclose(c); }
        msg = "1 gamemode (.amx)"; ok = true;
    }
    else if(EndsWith(src, ".pwn") || EndsWith(src, ".inc"))
    { msg = "a .pwn is source code - compile it with pawncc on a PC (gamemodes/*.amx) and import the .amx / the whole server"; return false; }
    else { msg = "unknown file type (use .rar, .zip or .amx)"; return false; }
    if(!ok) return false;
    root = SampFindRoot(destDir);
    if(root.empty()) { msg += " - but no gamemodes/ folder in it"; return false; }
    return true;
}
