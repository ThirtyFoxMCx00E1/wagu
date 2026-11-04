// 3.61: Project Day Z Legacy model pack (see dzmodels.h).
//
// Hosting: Host/DayZ/models of github.com/ThirtyFoxMCx00E1/wagu (tools/make_dz_models.py copies the
// whole MTA DayZ model folder there and writes manifest.txt). The files are the original PC dff / txd.
//
// Textures: GTA SA Android can't read PC (Direct3D) TXDs, so they are never given to the game as
// files. Each texture is decoded here (DXT1/3/5, 8888, 888, 565, 1555, 4444, palettes, L8; the
// biggest mip level that fits a size cap) and made into an RwTexture, put in our own TXD slot
// ("dzm_<id>") that is marked loaded and kept referenced, so the streamer never touches it.
//
// Models: PC dff files load fine on Android. CStreaming::ConvertBufferToObject is hooked: when the
// game converts a model we replace, it gets the downloaded dff instead of the bytes it read from
// gta3.img (its cd size is set to ours for that call, the memory counter is corrected after).
// 4.01: models (not vehicles) no longer swap the buffer: the game converts its own bytes and the
// CFileLoader::LoadClumpFile / LoadAtomicFile hooks (and CPedModelInfo::SetClump as a fallback)
// read our dff from a memory stream instead. Also fixed: the RwTexDictionaryAddTexture symbol name
// was mangled wrong (_Z26), so the engine check always failed and every model stayed GTA.
// A model is only switched (txd slot index in its model info) while it is not loaded, so the
// texture references of the game always stay balanced; models in use switch when they unload.
//
// Safety: every engine address is checked first (model info table, a live streaming table with txd
// entries at 20000+, the txd pool), else nothing is replaced. A marker file is written around the
// risky part of a conversion: if the game dies there, that model is never replaced again.
#include <stdarg.h>
#include <time.h>
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <ctype.h>
#include <algorithm>
#include "dzmodels.h"
#include "../net/online.h"
#include "../../imgui/rw_glue.h"

namespace
{
const char* kBaseUrls[] = {
    "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/Host/DayZ/models/",
    "https://raw.githubusercontent.com/ThirtyFoxMCx00E1/mtadayz/master/DayZ/models/",   // files only
};
const int kTxdBase = 20000;

struct StreamingInfo { int16_t next, prev, nextOnCd; uint8_t flags, img; uint32_t cdPos, cdSize; uint8_t loadState; uint8_t pad[3]; };
static_assert(sizeof(StreamingInfo) == 20, "CStreamingInfo");
struct BaseMI { void* vt; uint32_t key; char name[22]; uint16_t refs; int16_t txd; };
struct TxdDef { void* dict; int16_t refs; int16_t parent; uint32_t hash; char name[20]; char db[20]; bool keepCPU; };
struct Pool { TxdDef* objs; uint8_t* flags; int size; int firstFree; };
struct RwTex { void* raster; void* dict; void* l0; void* l1; char name[32]; char mask[32]; uint32_t filterAddr; int32_t refCount; };

struct Entry
{
    int id = 0; char kind = 'o';
    std::string dff, txd; uint32_t dffSize = 0, txdSize = 0, dffCrc = 0, txdCrc = 0;
    std::atomic<int> file { 0 };     // 0 = not yet, 1 = on disk + checked, 2 = failed
    int slot = -1; int16_t origTxd = -1; bool built = false, swapped = false, bad = false, wanted = false;
    bool waitLogged = false, loadedOnce = false; int texMade = -1;   // 4.00: status / log
    int fails = 0;
    bool noStream = false;           // 4.01: this model needs the old buffer swap
};

std::vector<Entry*> s_E;               // from the manifest
Entry* s_ById[kTxdBase] = {};
std::mutex s_M;
std::string s_Cache, s_Err;
std::atomic<bool> s_Busy { false }, s_ListReady { false };
std::atomic<int> s_Done { 0 }, s_Total { 0 }, s_KbDone { 0 }, s_KbTotal { 0 };
// 3.62: per file (download window)
std::atomic<int> s_FDone { 0 }, s_FTotal { 0 }, s_FFailed { 0 }, s_FDownloaded { 0 };
std::atomic<uint64_t> s_BDone { 0 }, s_BTotal { 0 };
std::atomic<size_t> s_BCur { 0 };
std::string s_CurFile, s_CurHost;
std::atomic<bool> s_CloudBusy { false };   // 4.02: the launcher Cloud Storage page is downloading
bool s_Started = false, s_Checked = false, s_Ok = false, s_PoolOk = false, s_Disabled = false;
int s_Engine = 0;                 // 4.00: the engine check result (1 ok, 0 not yet, < 0 failed)
std::string s_LogPath;            // 4.00: files/MP/dzmodels_log.txt (readable on the phone)
std::mutex s_LogM;
void DzLog(bool err, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void DzLog(bool err, const char* fmt, ...)
{
    char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    if(err) logger->Error("%s", b); else logger->Info("%s", b);
    std::lock_guard<std::mutex> lk(s_LogM);
    if(s_LogPath.empty()) return;
    if(FILE* f = fopen(s_LogPath.c_str(), "ab"))
    {
        time_t t = time(nullptr); struct tm tm; localtime_r(&t, &tm);
        fprintf(f, "[%02d:%02d:%02d] %s%s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, err ? "ERROR " : "", b);
        fclose(f);
    }
}
uint64_t s_NextCheck = 0;
std::set<int> s_Bad;

// engine
BaseMI** s_MI = nullptr;
StreamingInfo* s_SI = nullptr;
int* s_MemUsed = nullptr;
Pool** s_TxdPool = nullptr;
bool (*CBTO_orig)(char*, int, int) = nullptr;
int  (*AddTxdSlot)(const char*, const char*, bool) = nullptr;
int  (*FindTxdSlot)(const char*) = nullptr;
void (*TxdCreate)(int) = nullptr;
void (*TxdAddRef)(int) = nullptr;
void (*SetupTxdParent)(int) = nullptr;
void (*RemoveModel)(int) = nullptr;
void* (*TexCreate)(RwRaster*) = nullptr;
void* (*DictAddTex)(void*, void*) = nullptr;
// 4.01: the model swap point (the way XkiLoader does it, see research/xkiloader-findings.md): the game
// reads its own gta3.img bytes as normal, and when it hands the stream to the clump / atomic loader
// (or gives a ped its clump) our dff is put in from a memory stream. Nothing about cd sizes changes.
struct RwMem { uint8_t* start; uint32_t length; };
bool  (*LoadClump_orig)(void*, unsigned) = nullptr;
bool  (*LoadAtomic_orig)(void*, unsigned) = nullptr;
void  (*PedSetClump_orig)(void*, void*) = nullptr;
void* (*StreamOpen)(int, int, const void*) = nullptr;
int   (*StreamClose)(void*, void*) = nullptr;
int   (*StreamFindChunk)(void*, uint32_t, uint32_t*, uint32_t*) = nullptr;
void* (*ClumpStreamRead)(void*) = nullptr;
int   (*ClumpDestroy)(void*) = nullptr;
int s_CurId = -1; bool s_CurUsed = false; const char* s_CurHow = "";
std::vector<uint8_t> s_CurBuf; uint8_t* s_CurPtr = nullptr; uint32_t s_CurLen = 0;
bool StreamMode() { return LoadClump_orig && LoadAtomic_orig && StreamOpen && StreamClose; }
void* OpenCur() { RwMem m { s_CurPtr, s_CurLen }; return StreamOpen(3 /*rwSTREAMMEMORY*/, 1 /*rwSTREAMREAD*/, &m); }
bool HookLoadClump(void* stream, unsigned id)
{
    if(s_CurId < 0 || (int)id != s_CurId || s_CurUsed) return LoadClump_orig(stream, id);
    void* ms = OpenCur(); if(!ms) return LoadClump_orig(stream, id);
    s_CurUsed = true; s_CurHow = "clump loader";
    bool r = LoadClump_orig(ms, id);
    StreamClose(ms, nullptr);
    return r;
}
bool HookLoadAtomic(void* stream, unsigned id)
{
    if(s_CurId < 0 || (int)id != s_CurId || s_CurUsed) return LoadAtomic_orig(stream, id);
    void* ms = OpenCur(); if(!ms) return LoadAtomic_orig(stream, id);
    s_CurUsed = true; s_CurHow = "atomic loader";
    bool r = LoadAtomic_orig(ms, id);
    StreamClose(ms, nullptr);
    return r;
}
// a ped clump that came another way (big model path): read ours, drop the game's one
void HookPedSetClump(void* mi, void* clump)
{
    if(s_CurId >= 0 && !s_CurUsed && s_MI && (void*)s_MI[s_CurId] == mi && StreamFindChunk && ClumpStreamRead && ClumpDestroy)
    {
        void* ours = nullptr;
        if(void* ms = OpenCur())
        {
            if(StreamFindChunk(ms, 0x10 /*rwID_CLUMP*/, nullptr, nullptr)) ours = ClumpStreamRead(ms);
            StreamClose(ms, nullptr);
        }
        if(ours) { s_CurUsed = true; s_CurHow = "ped SetClump"; if(clump) ClumpDestroy(clump); clump = ours; }
    }
    PedSetClump_orig(mi, clump);
}

// 4.02: the mobile texture database (gta3 / txd .dxt/.etc/.pvr + .txt index, see the txd+gta3.zip
// release asset) is ONE flat table keyed only by texture name - it does not know about txd slots. When
// a dff is read the material names go through TextureDatabaseRuntime::GetTexture(name), so any MTA
// texture whose name is also a stock one ("army", "sweet", "wmopj", "ak47", "white64", weapon icons...)
// came out as the GTA texture, and the rest only worked when the dictionary was asked first. While one of
// our models is being read, our dictionary (then the other MTA dictionaries, for shared item textures)
// answers first - XkiLoader does the same (research/xkiloader-findings.md).
void* (*DbGetTexture_orig)(const char*) = nullptr;
void* (*DictFindTex)(void*, const char*) = nullptr;
void* s_LoadDict = nullptr;                 // the dictionary of the MTA model being read now
std::vector<void*> s_AllDicts;              // every MTA dictionary we filled
std::atomic<int> s_DbHits { 0 };
void* WorldGet(const char* name);           // 4.05.5 (below)
void* HookDbGetTexture(const char* name)
{
    if(s_LoadDict && name && *name && DictFindTex)
    {
        if(void* t = DictFindTex(s_LoadDict, name)) { ++s_DbHits; return t; }
        for(void* d : s_AllDicts) if(d != s_LoadDict) if(void* t = DictFindTex(d, name)) { ++s_DbHits; return t; }
    }
    if(name && *name) if(void* t = WorldGet(name)) return t;   // 4.05.5: GTA V world textures (Day Z)
    return DbGetTexture_orig(name);
}
struct LoadScope { void* prev; explicit LoadScope(void* d) : prev(s_LoadDict) { s_LoadDict = d; } ~LoadScope() { s_LoadDict = prev; } };

uint64_t NowMs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000; }

void MkDirs(const std::string& p) { for(size_t i = 1; i <= p.size(); ++i) if(i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755); }

bool ReadFile(const std::string& p, std::vector<uint8_t>& out)
{
    FILE* f = fopen(p.c_str(), "rb"); if(!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n <= 0 || n > 64 * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)n); bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n; fclose(f); return ok;
}
bool WriteFile(const std::string& p, const void* d, size_t n)
{
    std::string t = p + ".part"; FILE* f = fopen(t.c_str(), "wb"); if(!f) return false;
    bool ok = fwrite(d, 1, n, f) == n; ok = (fclose(f) == 0) && ok;
    return ok && rename(t.c_str(), p.c_str()) == 0;
}
std::string CacheName(const std::string& rel) { std::string s = rel; for(char& c : s) if(c == '/') c = '_'; return s_Cache + "/" + s; }
uint32_t Crc(const std::vector<uint8_t>& d) { return (uint32_t)crc32(0L, d.data(), (uInt)d.size()); }

std::string FindCacheDir()
{
    char pkg[256] = ""; FILE* f = fopen("/proc/self/cmdline", "rb");
    if(f) { size_t n = fread(pkg, 1, sizeof(pkg) - 1, f); pkg[n] = 0; fclose(f); char* c = strchr(pkg, ':'); if(c) *c = 0; }
    std::vector<std::string> tries;
    if(pkg[0]) { tries.push_back(std::string("/data/data/") + pkg + "/cache/dzmodels"); tries.push_back(std::string("/data/user/0/") + pkg + "/cache/dzmodels"); }
    const char* ad = aml->GetAndroidDataPath(); if(ad && *ad) tries.push_back(std::string(ad) + "/cache/dzmodels");
    for(auto& t : tries)
    {
        MkDirs(t); std::string probe = t + "/.w";
        FILE* w = fopen(probe.c_str(), "wb"); if(w) { fclose(w); unlink(probe.c_str()); return t; }
    }
    return "";
}

void LoadBad()
{
    s_Bad.clear();
    std::vector<uint8_t> d;
    // a marker left over = the game died while converting that model last time
    if(ReadFile(s_Cache + "/inhook.txt", d))
    {
        int id = atoi(std::string(d.begin(), d.end()).c_str());
        FILE* f = fopen((s_Cache + "/bad.txt").c_str(), "ab"); if(f) { fprintf(f, "%d\n", id); fclose(f); }
        unlink((s_Cache + "/inhook.txt").c_str());
        DzLog(true, "DzModels: the game closed while loading model %d last time - it stays GTA now", id);
    }
    if(ReadFile(s_Cache + "/bad.txt", d))
    {
        std::string s(d.begin(), d.end()); const char* p = s.c_str();
        // 4.00: a model is only skipped after the game closed TWICE while loading it (closing the app
        // from the recent apps list while a model loads used to block that model forever)
        std::map<int, int> cnt;
        while(*p) { int id = atoi(p); if(id > 0) ++cnt[id]; while(*p && *p != '\n') ++p; while(*p == '\n') ++p; }
        for(auto& kv : cnt) if(kv.second >= 2) s_Bad.insert(kv.first);
    }
}
void Mark(int id)
{
    if(s_Cache.empty()) return;
    if(id) { FILE* f = fopen((s_Cache + "/inhook.txt").c_str(), "wb"); if(f) { fprintf(f, "%d", id); fclose(f); } }
    else unlink((s_Cache + "/inhook.txt").c_str());
}

// 4.05.5: "tex <file> <size> <crc>" = the world textures (world.idx + world_NNN.txd)
struct WFile { std::string rel; uint32_t size = 0, crc = 0; };
void ParseManifest(const std::string& text, std::vector<Entry*>& out, std::vector<WFile>* tex = nullptr)
{
    size_t p = 0;
    while(p < text.size())
    {
        size_t e = text.find('\n', p); if(e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p); p = e + 1;
        if(l.empty() || l[0] == '#') continue;
        int id = 0; char kind = 0; char dff[160], txd[160]; unsigned ds = 0, ts = 0, dc = 0, tc = 0;
        if(!strncmp(l.c_str(), "tex ", 4))
        {
            if(tex && sscanf(l.c_str() + 4, "%159s %u %x", dff, &ds, &dc) == 3 && !strstr(dff, "..")) { WFile w; w.rel = dff; w.size = ds; w.crc = dc; tex->push_back(w); }
            continue;
        }
        if(sscanf(l.c_str(), "%d %c %159s %159s %u %u %x %x", &id, &kind, dff, txd, &ds, &ts, &dc, &tc) != 8) continue;
        if(id <= 0 || id >= kTxdBase || !strchr("pzwovm", kind) || strstr(dff, "..") || strstr(txd, "..")) continue;
        Entry* x = new Entry; x->id = id; x->kind = kind; x->dff = dff; x->txd = txd;
        x->dffSize = ds; x->txdSize = ts; x->dffCrc = dc; x->txdCrc = tc; out.push_back(x);
    }
}

bool Fetch(const std::string& rel, uint32_t size, uint32_t crc, bool firstBaseOnly)
{
    std::string path = CacheName(rel);
    std::vector<uint8_t> d;
    { std::lock_guard<std::mutex> lk(s_M); s_CurFile = rel; }
    s_BCur = 0;
    bool ok = ReadFile(path, d) && d.size() == size && Crc(d) == crc;   // already in the cache
    std::string why;
    for(int b = 0; !ok && b < (firstBaseOnly ? 1 : 2); ++b)
    {
        std::string body;
        { std::lock_guard<std::mutex> lk(s_M); const char* u = kBaseUrls[b] + 8; s_CurHost.assign(u, strchr(u, '/') - u); s_CurHost += b ? " (mtadayz fork)" : " (wagu)"; }
        s_BCur = 0;
        if(!Online::HttpsFetch(kBaseUrls[b] + rel, body, 180000, why, (size_t)size + 65536, &s_BCur)) continue;
        d.assign(body.begin(), body.end());
        if(d.size() != size || Crc(d) != crc) { why = "bad file"; continue; }
        if(WriteFile(path, d.data(), d.size())) { ok = true; ++s_FDownloaded; break; }
        why = "cache full?";
    }
    s_BCur = 0;
    s_BDone += size; ++s_FDone;
    if(!ok) { ++s_FFailed; std::lock_guard<std::mutex> lk(s_M); s_Err = rel + ": " + why; }
    return ok;
}

void FetchWanted(int total)
{
    for(Entry* e : s_E)
    {
        if(!e->wanted || e->file == 1) continue;
        bool okD = Fetch(e->dff, e->dffSize, e->dffCrc, false);
        bool ok = Fetch(e->txd, e->txdSize, e->txdCrc, false) && okD;
        e->file = ok ? 1 : 2;
        if(ok) { ++s_Done; s_KbDone += (int)((e->dffSize + e->txdSize) / 1024); }
        else DzLog(true, "DzModels: %d (%s) not downloaded: %s", e->id, e->dff.c_str(), s_Err.c_str());
    }
    DzLog(false, "DzModels: %d / %d models ready", s_Done.load(), total);
}

// ---------------- 4.05.5: world textures ----------------
// The GTA V texture pack of release v4.02 (texdb gta3: .dat / .toc / .tmb, RLE packed) converted by
// tools/texdb2txd.py + tools/make_dz_world.py into plain PC TXDs (Host/DayZ/models/world/tex) and an
// index. On the Day Z server every texture-database lookup (TextureDatabaseRuntime::GetTexture) that
// the index knows is answered from those files, one texture at a time (read + decode on first use).
struct WEnt { uint8_t file = 0; uint32_t off = 0, size = 0; };
std::vector<WFile> s_WFiles;                       // from the manifest
std::atomic<bool> s_WWant { true }, s_WReady { false }, s_WOn { false };
std::mutex s_WM;
std::unordered_map<std::string, WEnt> s_WIdx;      // lower-case name -> where
std::unordered_map<std::string, std::string> s_WAlias;
std::vector<std::string> s_WPath;                  // index file column -> cache path
std::vector<FILE*> s_WF;
std::unordered_map<std::string, void*> s_WCache;   // lower-case name -> our RwTexture (nullptr = could not)
void* s_WDict = nullptr; int s_WSlot = -1;
std::atomic<int> s_WMade { 0 }, s_WHits { 0 }, s_WFail { 0 };
const int kWorldCap = 128;                         // pixels: our textures are RGBA on the GPU
const int kWorldMax = 3000;                        // textures at most (then GTA ones again)

std::string Lower(const char* n) { std::string s(n); for(char& c : s) c = (char)tolower((unsigned char)c); return s; }
bool LoadWorldIdx()
{
    std::vector<uint8_t> d; std::string idxRel;
    for(auto& w : s_WFiles) if(w.rel.size() > 4 && w.rel.compare(w.rel.size() - 4, 4, ".idx") == 0) idxRel = w.rel;
    if(idxRel.empty() || !ReadFile(CacheName(idxRel), d)) return false;
    const std::string dir = idxRel.substr(0, idxRel.rfind('/') + 1);
    std::lock_guard<std::mutex> lk(s_WM);
    s_WIdx.clear(); s_WAlias.clear(); s_WPath.clear();
    for(FILE* f : s_WF) if(f) fclose(f);
    s_WF.clear();
    std::map<std::string, int> fileNo;
    std::string text(d.begin(), d.end()); size_t p = 0;
    while(p < text.size())
    {
        size_t e = text.find('\n', p); if(e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p); p = e + 1;
        if(l.empty() || l[0] == '#') continue;
        std::vector<std::string> c; size_t q = 0;
        for(;;) { size_t t = l.find('\t', q); c.push_back(l.substr(q, t == std::string::npos ? std::string::npos : t - q)); if(t == std::string::npos) break; q = t + 1; }
        if(c.size() == 3 && c[0] == "alias") { s_WAlias[Lower(c[1].c_str())] = Lower(c[2].c_str()); continue; }
        if(c.size() != 4 || c[0] == "file" || strstr(c[1].c_str(), "..")) continue;
        auto it = fileNo.find(c[1]);
        if(it == fileNo.end()) { it = fileNo.emplace(c[1], (int)s_WPath.size()).first; s_WPath.push_back(CacheName(dir + c[1])); s_WF.push_back(nullptr); }
        WEnt w; w.file = (uint8_t)it->second; w.off = (uint32_t)strtoul(c[2].c_str(), nullptr, 10); w.size = (uint32_t)strtoul(c[3].c_str(), nullptr, 10);
        s_WIdx[Lower(c[0].c_str())] = w;
    }
    DzLog(false, "DzModels: world textures: %d in the index (%d aliases, %d files)", (int)s_WIdx.size(), (int)s_WAlias.size(), (int)s_WPath.size());
    return !s_WIdx.empty();
}
void FetchWorld()
{
    if(!s_WWant || s_WFiles.empty()) return;
    int ok = 0;
    for(auto& w : s_WFiles) if(Fetch(w.rel, w.size, w.crc, false)) ++ok;
    if(ok == (int)s_WFiles.size() && LoadWorldIdx()) s_WReady = true;
    else DzLog(true, "DzModels: world textures: %d of %d files downloaded - GTA textures stay", ok, (int)s_WFiles.size());
}

void DownloadThread(std::vector<int> objIds)
{
    s_BDone = 0; s_FDone = 0; s_FFailed = 0; s_FDownloaded = 0;   // 4.02: the Cloud Storage page uses Fetch too
    if(s_ListReady && !s_E.empty())
    {   // 4.02: deleted from the Cloud Storage page: get those files again (same list, same entries)
        int total = 0, kb = 0, done = 0, kbDone = 0; uint64_t bt = 0; int ft = 0;
        for(Entry* e : s_E) if(e->wanted)
        {
            ++total; kb += (int)((e->dffSize + e->txdSize) / 1024);
            if(e->file == 1) { ++done; kbDone += (int)((e->dffSize + e->txdSize) / 1024); } else { bt += e->dffSize + e->txdSize; ft += 2; }
        }
        if(s_WWant && !s_WReady) for(auto& w : s_WFiles) { bt += w.size; ++ft; }
        s_Total = total; s_KbTotal = kb; s_Done = done; s_KbDone = kbDone; s_BTotal = bt; s_FTotal = ft;
        FetchWanted(total);
        FetchWorld();
        s_Busy = false;
        return;
    }
    // 1) the list (fresh from GitHub, else the copy from last time)
    std::string text, why; const std::string mpath = s_Cache + "/manifest.txt";
    if(Online::HttpsFetch(std::string(kBaseUrls[0]) + "manifest.txt", text, 30000, why, 512 * 1024) && text.find("Project Day Z") != std::string::npos)
        WriteFile(mpath, text.data(), text.size());
    else
    {
        std::vector<uint8_t> d; if(ReadFile(mpath, d)) text.assign(d.begin(), d.end()); else text.clear();
        std::lock_guard<std::mutex> lk(s_M); s_Err = "model list: " + why;
    }
    std::vector<Entry*> list; std::vector<WFile> wf; ParseManifest(text, list, &wf);
    s_WFiles = wf;
    std::set<int> objs(objIds.begin(), objIds.end());
    for(Entry* e : list) e->wanted = e->kind != 'o' || objs.count(e->id);
    // zombies + survivors first (most visible), then loot, guns, the UAZ
    auto prio = [](char k) { return k == 'z' ? 0 : k == 'p' ? 1 : k == 'o' ? 2 : k == 'w' ? 3 : k == 'm' ? 4 : 5; };
    std::stable_sort(list.begin(), list.end(), [&](Entry* a, Entry* b) { return prio(a->kind) < prio(b->kind); });
    int total = 0, kb = 0;
    for(Entry* e : list) if(e->wanted) { ++total; kb += (int)((e->dffSize + e->txdSize) / 1024); }
    s_Total = total; s_KbTotal = kb;
    { uint64_t bt = 0; int ft = 0; for(Entry* e : list) if(e->wanted) { bt += e->dffSize + e->txdSize; ft += 2; } if(s_WWant) for(auto& w : wf) { bt += w.size; ++ft; } s_BTotal = bt; s_FTotal = ft; }
    s_E = list;
    for(Entry* e : list) if(!s_ById[e->id]) s_ById[e->id] = e;
    s_ListReady = true;
    DzLog(false, "DzModels: %d models in the list, %d wanted (%d MB)", (int)list.size(), total, kb / 1024);
    // 2) the files
    FetchWanted(total);
    FetchWorld();
    s_Busy = false;
}

// ---------------- PC TXD -> RwTexture ----------------
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
void C565(uint16_t c, uint8_t* o) { o[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31); o[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63); o[2] = (uint8_t)((c & 31) * 255 / 31); o[3] = 255; }

void DecodeDxtBlock(const uint8_t* b, int type, uint8_t px[16][4])
{
    const uint8_t* cb = type == 1 ? b : b + 8;
    uint16_t c0 = U16(cb), c1 = U16(cb + 2); uint8_t c[4][4];
    C565(c0, c[0]); C565(c1, c[1]);
    if(type != 1 || c0 > c1)
    {
        for(int k = 0; k < 3; ++k) { c[2][k] = (uint8_t)((2 * c[0][k] + c[1][k]) / 3); c[3][k] = (uint8_t)((c[0][k] + 2 * c[1][k]) / 3); }
        c[2][3] = c[3][3] = 255;
    }
    else
    {
        for(int k = 0; k < 3; ++k) { c[2][k] = (uint8_t)((c[0][k] + c[1][k]) / 2); c[3][k] = 0; }
        c[2][3] = 255; c[3][3] = 0;
    }
    uint32_t idx = U32(cb + 4);
    for(int i = 0; i < 16; ++i) memcpy(px[i], c[(idx >> (2 * i)) & 3], 4);
    if(type == 3)
        for(int i = 0; i < 16; ++i) { int a = (b[i / 2] >> ((i & 1) * 4)) & 15; px[i][3] = (uint8_t)(a * 17); }
    else if(type == 5)
    {
        uint8_t a[8] = { b[0], b[1] };
        if(a[0] > a[1]) for(int k = 1; k < 7; ++k) a[k + 1] = (uint8_t)(((7 - k) * a[0] + k * a[1]) / 7);
        else { for(int k = 1; k < 5; ++k) a[k + 1] = (uint8_t)(((5 - k) * a[0] + k * a[1]) / 5); a[6] = 0; a[7] = 255; }
        uint64_t bits = 0; for(int k = 0; k < 6; ++k) bits |= (uint64_t)b[2 + k] << (8 * k);
        for(int i = 0; i < 16; ++i) px[i][3] = a[(bits >> (3 * i)) & 7];
    }
}

struct Tex { std::string name, mask; uint32_t filterAddr = 0; int w = 0, h = 0; std::vector<uint8_t> rgba; };

// one texture native (struct chunk data). false = a format we don't read (that texture is skipped)
bool DecodeNative(const uint8_t* d, size_t n, int cap, Tex& t)
{
    if(n < 88) return false;
    uint32_t plat = U32(d); if(plat != 9 && plat != 8) return false;
    t.filterAddr = (2u /* linear: our rasters have no mip levels */) | ((uint32_t)(d[5] & 15) << 8) | ((uint32_t)(d[5] >> 4) << 12);
    char nm[33] = {}, mk[33] = {}; memcpy(nm, d + 8, 32); memcpy(mk, d + 40, 32); t.name = nm; t.mask = mk;
    uint32_t rf = U32(d + 72), fmt = U32(d + 76);
    int w = U16(d + 80), h = U16(d + 82), depth = d[84], levels = d[85] ? d[85] : 1; uint8_t flags = d[87];
    int dxt = 0;
    if(plat == 9) { if(fmt == 0x31545844) dxt = 1; else if(fmt == 0x33545844) dxt = 3; else if(fmt == 0x35545844) dxt = 5; else if(fmt == 0x32545844) dxt = 3; else if(fmt == 0x34545844) dxt = 5; }
    else if(flags >= 1 && flags <= 5) dxt = flags == 1 ? 1 : (flags <= 3 ? 3 : 5);
    const uint8_t* p = d + 88; const uint8_t* end = d + n;
    uint8_t pal[256][4] = {}; bool pal8 = (rf & 0x2000) != 0, pal4 = (rf & 0x4000) != 0;
    if(pal8 || pal4)
    {
        int cnt = pal8 ? 256 : 32; if(p + cnt * 4 > end) return false;
        for(int i = 0; i < cnt && i < 256; ++i) memcpy(pal[i], p + i * 4, 4);
        p += cnt * 4;
    }
    if(w <= 0 || h <= 0 || w > 4096 || h > 4096) return false;
    // the biggest level that fits the cap
    int lv = 0; while(lv + 1 < levels && ((w >> lv) > cap || (h >> lv) > cap) && (w >> (lv + 1)) >= 4 && (h >> (lv + 1)) >= 4) ++lv;
    for(int i = 0; i < lv; ++i) { if(p + 4 > end) return false; uint32_t sz = U32(p); p += 4 + sz; }
    if(p + 4 > end) return false;
    uint32_t sz = U32(p); p += 4; if(p + sz > end) return false;
    int lw = w >> lv, lh = h >> lv; if(lw < 1) lw = 1; if(lh < 1) lh = 1;
    std::vector<uint8_t> o((size_t)lw * lh * 4, 255);
    if(dxt)
    {
        int bw = (lw + 3) / 4, bh = (lh + 3) / 4, bs = dxt == 1 ? 8 : 16;
        if((size_t)bw * bh * bs > sz) return false;
        uint8_t px[16][4];
        for(int by = 0; by < bh; ++by) for(int bx = 0; bx < bw; ++bx)
        {
            DecodeDxtBlock(p + ((size_t)by * bw + bx) * bs, dxt, px);
            for(int i = 0; i < 16; ++i)
            {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2); if(x >= lw || y >= lh) continue;
                memcpy(&o[((size_t)y * lw + x) * 4], px[i], 4);
            }
        }
    }
    else if(pal8 || pal4)
    {
        if((size_t)lw * lh > sz) return false;
        for(size_t i = 0; i < (size_t)lw * lh; ++i) memcpy(&o[i * 4], pal[pal4 ? (p[i] & 31) : p[i]], 4);
    }
    else
    {
        // D3D9 format, or the RW raster format (D3D8 files)
        int f = 0;   // 1 = BGRA8888, 2 = BGRX8888, 3 = 565, 4 = 1555, 5 = 4444, 6 = L8, 7 = BGR888 (24 bit)
        if(plat == 9 && fmt == 21) f = 1; else if(plat == 9 && fmt == 22) f = 2; else if(plat == 9 && fmt == 23) f = 3;
        else if(plat == 9 && fmt == 25) f = 4; else if(plat == 9 && fmt == 26) f = 5; else if(plat == 9 && fmt == 50) f = 6;
        else switch(rf & 0x0F00) { case 0x500: f = 1; break; case 0x600: f = depth == 24 ? 7 : 2; break; case 0x200: f = 3; break; case 0x100: f = 4; break; case 0x300: f = 5; break; case 0x400: f = 6; break; default: f = 0; }
        int bpp = (f == 1 || f == 2) ? 4 : (f == 7 ? 3 : (f == 6 ? 1 : 2));
        if(!f || (size_t)lw * lh * bpp > sz) return false;
        for(size_t i = 0; i < (size_t)lw * lh; ++i)
        {
            const uint8_t* s = p + i * bpp; uint8_t* q = &o[i * 4];
            switch(f)
            {
            case 1: q[0] = s[2]; q[1] = s[1]; q[2] = s[0]; q[3] = s[3]; break;
            case 2: case 7: q[0] = s[2]; q[1] = s[1]; q[2] = s[0]; q[3] = 255; break;
            case 3: C565(U16(s), q); break;
            case 4: { uint16_t c = U16(s); q[0] = (uint8_t)(((c >> 10) & 31) * 255 / 31); q[1] = (uint8_t)(((c >> 5) & 31) * 255 / 31); q[2] = (uint8_t)((c & 31) * 255 / 31); q[3] = (c & 0x8000) ? 255 : 0; break; }
            case 5: { uint16_t c = U16(s); q[0] = (uint8_t)(((c >> 8) & 15) * 17); q[1] = (uint8_t)(((c >> 4) & 15) * 17); q[2] = (uint8_t)((c & 15) * 17); q[3] = (uint8_t)((c >> 12) * 17); break; }
            case 6: q[0] = q[1] = q[2] = s[0]; q[3] = 255; break;
            }
        }
    }
    // still too big (no mip levels in the file): halve
    while(lw > cap || lh > cap)
    {
        int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1; std::vector<uint8_t> r((size_t)nw * nh * 4);
        for(int y = 0; y < nh; ++y) for(int x = 0; x < nw; ++x) for(int k = 0; k < 4; ++k)
        {
            int x0 = x * lw / nw, y0 = y * lh / nh, x1 = x0 + (lw > 1), y1 = y0 + (lh > 1);
            r[((size_t)y * nw + x) * 4 + k] = (uint8_t)((o[((size_t)y0 * lw + x0) * 4 + k] + o[((size_t)y0 * lw + x1) * 4 + k] + o[((size_t)y1 * lw + x0) * 4 + k] + o[((size_t)y1 * lw + x1) * 4 + k]) / 4);
        }
        o.swap(r); lw = nw; lh = nh;
    }
    t.w = lw; t.h = lh; t.rgba.swap(o);
    return true;
}

void* MakeTexture(const Tex& t)
{
    RwImage* img = RwImageCreate(t.w, t.h, 32); if(!img) return nullptr;
    RwImageAllocatePixels(img);
    if(!img->cpPixels || img->stride < t.w * 4) { RwImageDestroy(img); return nullptr; }
    RwInt32 rw = 0, rh = 0, rd = 0, rf = 0;
    for(int y = 0; y < t.h; ++y) memcpy(img->cpPixels + (size_t)y * img->stride, &t.rgba[(size_t)y * t.w * 4], (size_t)t.w * 4);
    RwImageFindRasterFormat(img, 4 /* rwRASTERTYPETEXTURE */, &rw, &rh, &rd, &rf);
    if(rw != t.w || rh != t.h)
    {
        // the device wants other dimensions (not a power of two?): nearest resample
        RwImage* i2 = (rw > 0 && rh > 0 && rw <= 4096 && rh <= 4096) ? RwImageCreate(rw, rh, 32) : nullptr;
        if(i2) RwImageAllocatePixels(i2);
        if(!i2 || !i2->cpPixels) { if(i2) RwImageDestroy(i2); RwImageDestroy(img); return nullptr; }
        for(int y = 0; y < rh; ++y) for(int x = 0; x < rw; ++x)
            memcpy(i2->cpPixels + (size_t)y * i2->stride + x * 4, &t.rgba[((size_t)(y * t.h / rh) * t.w + (x * t.w / rw)) * 4], 4);
        RwImageDestroy(img); img = i2;
    }
    RwRaster* r = RwRasterCreate(rw, rh, rd, rf);
    if(r) r = RwRasterSetFromImage(r, img);
    RwImageDestroy(img);
    if(!r) return nullptr;
    RwTex* tex = (RwTex*)TexCreate(r);
    if(!tex) { RwRasterDestroy(r); return nullptr; }
    strncpy(tex->name, t.name.c_str(), 31); tex->name[31] = 0;
    strncpy(tex->mask, t.mask.c_str(), 31); tex->mask[31] = 0;
    tex->filterAddr = t.filterAddr;
    return tex;
}

// fills our txd slot (once). Main thread (inside the streamer).
bool BuildTxd(Entry* e)
{
    if(e->built) return true;
    e->built = true;
    std::vector<uint8_t> d;
    if(!ReadFile(CacheName(e->txd), d) || d.size() < 28 || U32(d.data()) != 0x16) return false;
    TxdDef* def = s_PoolOk ? &(*s_TxdPool)->objs[e->slot] : nullptr;
    void* dict = def ? def->dict : nullptr;
    if(!dict) return false;
    const int cap = e->kind == 'p' ? 1024 : ((e->kind == 'o' || e->kind == 'm') ? 256 : 512);
    int count = U16(d.data() + 24), made = 0;
    size_t p = 28;
    for(int i = 0; i < count && p + 12 <= d.size(); ++i)
    {
        uint32_t ct = U32(&d[p]), cs = U32(&d[p + 4]); size_t next = p + 12 + cs;
        if(ct != 0x15 || next > d.size()) break;
        size_t q = p + 12;
        if(q + 12 <= next && U32(&d[q]) == 0x01)
        {
            uint32_t ss = U32(&d[q + 4]);
            Tex t;
            if(q + 12 + ss <= next && DecodeNative(&d[q + 12], ss, cap, t))
                if(void* tex = MakeTexture(t)) { DictAddTex(dict, tex); ++made; }
        }
        p = next;
    }
    if(SetupTxdParent && def->parent >= 0) SetupTxdParent(e->slot);
    e->texMade = made;
    if(made > 0 && std::find(s_AllDicts.begin(), s_AllDicts.end(), dict) == s_AllDicts.end()) s_AllDicts.push_back(dict);   // 4.02
    DzLog(made > 0 ? false : true, "DzModels: %d of %d textures decoded for %d (%s)", made, count, e->id, e->txd.c_str());
    return made > 0;
}

bool HookCBTO(char* buf, int idx, int ch)
{
    Entry* e = (idx > 0 && idx < kTxdBase) ? s_ById[idx] : nullptr;
    if(!e || !e->swapped || e->bad || !s_MI[idx] || s_MI[idx]->txd != e->slot) return CBTO_orig(buf, idx, ch);
    Mark(idx);
    BuildTxd(e);
    // 4.02: our textures answer the texture-database lookups while this dff is read
    LoadScope scope(s_PoolOk && e->slot >= 0 ? (*s_TxdPool)->objs[e->slot].dict : nullptr);
    std::vector<uint8_t> dff;
    if(!ReadFile(CacheName(e->dff), dff) || dff.size() < 12) { e->bad = true; Mark(0); return CBTO_orig(buf, idx, ch); }
    bool r;
    const char* how = "buffer swap";
    if(StreamMode() && !e->noStream && e->kind != 'v' && s_CurId < 0)   // vehicles keep the buffer swap (big-model path)
    {   // 4.01: the game reads its own bytes, our loader hooks hand it the MTA dff
        s_CurBuf.assign(dff.size() + 16, 0);
        s_CurPtr = (uint8_t*)(((uintptr_t)s_CurBuf.data() + 15) & ~(uintptr_t)15);
        memcpy(s_CurPtr, dff.data(), dff.size()); s_CurLen = (uint32_t)dff.size();
        s_CurId = idx; s_CurUsed = false;
        r = CBTO_orig(buf, idx, ch);
        bool used = s_CurUsed; how = s_CurHow;
        s_CurId = -1; s_CurPtr = nullptr; s_CurLen = 0; std::vector<uint8_t>().swap(s_CurBuf);
        if(!used)
        {   // the conversion never reached a loader we hook: use the old way from the next load on
            e->noStream = true; Mark(0);
            DzLog(true, "DzModels: model %d did not go through the clump/atomic loader - next load uses the buffer swap", idx);
            return r;
        }
    }
    else
    {
        uint32_t sectors = (uint32_t)((dff.size() + 2047) / 2048);
        dff.resize((size_t)sectors * 2048, 0);
        StreamingInfo& si = s_SI[idx];
        uint32_t oldSize = si.cdSize; int mem0 = *s_MemUsed;
        si.cdSize = sectors;
        r = CBTO_orig((char*)dff.data(), idx, ch);
        si.cdSize = oldSize;
        int added = *s_MemUsed - mem0;
        if(added == (int)sectors * 2048) *s_MemUsed -= ((int)sectors - (int)oldSize) * 2048;
    }
    Mark(0);
    if(r && !e->loadedOnce) { e->loadedOnce = true; DzLog(false, "DzModels: model %d (%s) is in the game now (MTA dff via %s, %d textures from our txd)", idx, e->dff.c_str(), how, s_DbHits.exchange(0)); }
    if(!r) DzLog(true, "DzModels: the game could not read %s for model %d via %s (try %d)", e->dff.c_str(), idx, how, e->fails + 1);
    if(!r && ++e->fails >= 2) { e->bad = true; DzLog(true, "DzModels: model %d (%s) did not load - GTA model kept", idx, e->dff.c_str()); }
    return r;
}

// the engine table checks (once in game)
int CheckEngine()
{
    if(!s_MI || !s_SI || !s_MemUsed || !CBTO_orig || !AddTxdSlot || !TxdCreate || !TxdAddRef || !RemoveModel || !TexCreate || !DictAddTex) return -1;
    if(!s_MI[400] || strncasecmp(s_MI[400]->name, "landstal", 8) != 0) return -2;
    int good = 0, badc = 0;
    for(int i = 1; i < 612; ++i)
    {
        if((i >= 300 && i < 400) || !s_MI[i] || s_SI[i].loadState != 1) continue;
        int t = s_MI[i]->txd; if(t < 0 || t >= 5000) continue;
        if(s_SI[kTxdBase + t].loadState == 1) ++good; else ++badc;
    }
    if(good < 2) return 0;          // nothing loaded yet - try again later
    DzLog(badc > good / 4, "DzModels: engine check - %d loaded models with their txd at 20000+, %d without", good, badc);
    if(badc > good / 4) return -3;  // the txd entries are not at 20000+ (limit adjuster?)
    s_PoolOk = false;
    if(s_TxdPool && *s_TxdPool && (*s_TxdPool)->objs && (*s_TxdPool)->size > 1000 && (*s_TxdPool)->size < 100000)
    {
        int t = s_MI[400]->txd;
        if(t >= 0 && t < (*s_TxdPool)->size) { char n[21] = {}; memcpy(n, (*s_TxdPool)->objs[t].name, 20); s_PoolOk = strncasecmp(n, "landstal", 8) == 0; }
    }
    if(!s_PoolOk) { DzLog(true, "DzModels: the txd pool did not look right (size %d)", (s_TxdPool && *s_TxdPool) ? (*s_TxdPool)->size : -1); return -4; }
    return 1;
}

bool MakeSlot(Entry* e)
{
    if(e->slot >= 0) return true;
    char nm[20]; snprintf(nm, sizeof(nm), "dzm_%d", e->id);
    int orig = s_MI[e->id]->txd;
    const TxdDef* od = (orig >= 0 && orig < (*s_TxdPool)->size) ? &(*s_TxdPool)->objs[orig] : nullptr;
    char db[21] = {}; if(od) memcpy(db, od->db, 20);
    int slot = FindTxdSlot ? FindTxdSlot(nm) : -1;
    if(slot < 0) slot = AddTxdSlot(nm, db, false);
    if(slot < 0 || slot >= 5000 || slot >= (*s_TxdPool)->size) { e->bad = true; return false; }
    TxdDef& d = (*s_TxdPool)->objs[slot];
    if(!d.dict) TxdCreate(slot);
    if(!d.dict) { e->bad = true; return false; }
    d.parent = od ? od->parent : -1;
    TxdAddRef(slot);                        // ours, forever: the streamer never removes the slot
    s_SI[kTxdBase + slot].loadState = 1;    // "loaded" - it is never read from gta3.img
    e->slot = slot; e->origTxd = (int16_t)orig;
    return true;
}

// 4.05.5: one world texture (main thread, inside the game's texture lookup). nullptr = not ours / off
bool WorldDict()
{
    if(s_WDict) return true;
    if(s_WSlot == -2 || !s_PoolOk || !AddTxdSlot || !TxdCreate || !TxdAddRef) return false;
    int slot = FindTxdSlot ? FindTxdSlot("dzm_world") : -1;
    if(slot < 0) slot = AddTxdSlot("dzm_world", "", false);
    if(slot < 0 || slot >= 5000 || slot >= (*s_TxdPool)->size) { s_WSlot = -2; return false; }
    TxdDef& d = (*s_TxdPool)->objs[slot];
    if(!d.dict) TxdCreate(slot);
    if(!d.dict) { s_WSlot = -2; return false; }
    TxdAddRef(slot);                        // ours forever, never freed (models may still use the textures)
    s_SI[kTxdBase + slot].loadState = 1;
    s_WSlot = slot; s_WDict = d.dict;
    return true;
}
void* WorldGet(const char* name)
{
    if(!s_WOn || !s_WReady || !s_Ok) return nullptr;
    std::string k = Lower(name);
    std::lock_guard<std::mutex> lk(s_WM);
    auto c = s_WCache.find(k);
    if(c != s_WCache.end())
    {
        if(!c->second) return nullptr;
        ++((RwTex*)c->second)->refCount; ++s_WHits;   // the game's GetTexture hands out a reference too
        return c->second;
    }
    std::string n = k;
    auto it = s_WIdx.find(n);
    for(int i = 0; i < 4 && it == s_WIdx.end(); ++i) { auto a = s_WAlias.find(n); if(a == s_WAlias.end()) break; n = a->second; it = s_WIdx.find(n); }
    if(it == s_WIdx.end()) return nullptr;                       // not in the pack: GTA's own
    if(s_WMade >= kWorldMax || !WorldDict()) return nullptr;
    void* tex = nullptr;
    const WEnt& w = it->second;
    if(w.file < s_WF.size() && w.size >= 88 && w.size < 8 * 1024 * 1024)
    {
        FILE*& f = s_WF[w.file];
        if(!f) f = fopen(s_WPath[w.file].c_str(), "rb");
        std::vector<uint8_t> d(w.size); Tex t;
        if(f && fseek(f, (long)w.off, SEEK_SET) == 0 && fread(d.data(), 1, d.size(), f) == d.size() && DecodeNative(d.data(), d.size(), kWorldCap, t))
        {
            t.name = name;                                       // the name the game asked for (aliases)
            tex = MakeTexture(t);
            if(tex) { DictAddTex(s_WDict, tex); ++s_WMade; }
        }
    }
    s_WCache[k] = tex;
    if(!tex) { if(++s_WFail <= 5) DzLog(true, "DzModels: world texture %s could not be read", name); return nullptr; }
    if(s_WMade == 1 || s_WMade % 500 == 0) DzLog(false, "DzModels: world textures in the game: %d", s_WMade.load());
    ++((RwTex*)tex)->refCount; ++s_WHits;
    return tex;
}

// switch one model (only while the game has it unloaded). true = done
bool Switch(Entry* e, bool on)
{
    BaseMI* mi = s_MI[e->id]; if(!mi) { e->bad = true; return false; }
    if(on && !MakeSlot(e)) return false;
    StreamingInfo& si = s_SI[e->id];
    if(si.loadState == 1 && mi->refs == 0) RemoveModel(e->id);
    if(si.loadState != 0)                   // in use / being read: next time
    {
        if(!e->waitLogged) { e->waitLogged = true; DzLog(false, "DzModels: model %d waits - the game is using it (refs %d, state %d)", e->id, (int)mi->refs, (int)si.loadState); }
        return false;
    }
    if(on) DzLog(false, "DzModels: model %d switched to the MTA files (txd slot %d)", e->id, e->slot);
    if(on) { if(mi->txd != e->origTxd) { e->bad = true; return false; } mi->txd = (int16_t)e->slot; }
    else mi->txd = e->origTxd;
    e->swapped = on;
    return true;
}
} // namespace

void DzModels::Init(void* h)
{
    *(void**)&s_MI = (void*)aml->GetSym(h, "_ZN10CModelInfo16ms_modelInfoPtrsE");
    *(void**)&s_SI = (void*)aml->GetSym(h, "_ZN10CStreaming16ms_aInfoForModelE");
    *(void**)&s_MemUsed = (void*)aml->GetSym(h, "_ZN10CStreaming13ms_memoryUsedE");
    *(void**)&s_TxdPool = (void*)aml->GetSym(h, "_ZN9CTxdStore11ms_pTxdPoolE");
    *(void**)&AddTxdSlot = (void*)aml->GetSym(h, "_ZN9CTxdStore10AddTxdSlotEPKcS1_b");
    *(void**)&FindTxdSlot = (void*)aml->GetSym(h, "_ZN9CTxdStore11FindTxdSlotEPKc");
    *(void**)&TxdCreate = (void*)aml->GetSym(h, "_ZN9CTxdStore6CreateEi");
    *(void**)&TxdAddRef = (void*)aml->GetSym(h, "_ZN9CTxdStore6AddRefEi");
    *(void**)&SetupTxdParent = (void*)aml->GetSym(h, "_ZN9CTxdStore14SetupTxdParentEi");
    *(void**)&RemoveModel = (void*)aml->GetSym(h, "_ZN10CStreaming11RemoveModelEi");
    *(void**)&TexCreate = (void*)aml->GetSym(h, "_Z15RwTextureCreateP8RwRaster");
    *(void**)&DictAddTex = (void*)aml->GetSym(h, "_Z25RwTexDictionaryAddTextureP15RwTexDictionaryP9RwTexture");
    // 4.02: the mobile texture database lookup (flat, by name) - ours first while an MTA dff is read
    *(void**)&DictFindTex = (void*)aml->GetSym(h, "_Z31RwTexDictionaryFindNamedTextureP15RwTexDictionaryPKc");
    if(!DictFindTex) *(void**)&DictFindTex = (void*)aml->GetSym(h, "RwTexDictionaryFindNamedTexture");
    if(DictFindTex)
        if(void* g = (void*)aml->GetSym(h, "_ZN22TextureDatabaseRuntime10GetTextureEPKc")) aml->Hook(g, (void*)&HookDbGetTexture, (void**)&DbGetTexture_orig);
    if(!TexCreate) *(void**)&TexCreate = (void*)aml->GetSym(h, "RwTextureCreate");
    if(!DictAddTex) *(void**)&DictAddTex = (void*)aml->GetSym(h, "RwTexDictionaryAddTexture");
    void* f = (void*)aml->GetSym(h, "_ZN10CStreaming21ConvertBufferToObjectEPcii");
    if(f && s_SI && s_MemUsed && s_MI) aml->Hook(f, (void*)&HookCBTO, (void**)&CBTO_orig);
    // 4.01: RenderWare streams + the loader hooks
    *(void**)&StreamOpen = (void*)aml->GetSym(h, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv");
    *(void**)&StreamClose = (void*)aml->GetSym(h, "_Z13RwStreamCloseP8RwStreamPv");
    *(void**)&StreamFindChunk = (void*)aml->GetSym(h, "_Z17RwStreamFindChunkP8RwStreamjPjS1_");
    *(void**)&ClumpStreamRead = (void*)aml->GetSym(h, "_Z17RpClumpStreamReadP8RwStream");
    *(void**)&ClumpDestroy = (void*)aml->GetSym(h, "_Z14RpClumpDestroyP7RpClump");
    if(CBTO_orig && StreamOpen && StreamClose)
    {
        if(void* g = (void*)aml->GetSym(h, "_ZN11CFileLoader13LoadClumpFileEP8RwStreamj")) aml->Hook(g, (void*)&HookLoadClump, (void**)&LoadClump_orig);
        if(void* g = (void*)aml->GetSym(h, "_ZN11CFileLoader14LoadAtomicFileEP8RwStreamj")) aml->Hook(g, (void*)&HookLoadAtomic, (void**)&LoadAtomic_orig);
        if(StreamFindChunk && ClumpStreamRead && ClumpDestroy)
            if(void* g = (void*)aml->GetSym(h, "_ZN14CPedModelInfo8SetClumpEP7RpClump")) aml->Hook(g, (void*)&HookPedSetClump, (void**)&PedSetClump_orig);
    }
    s_Cache = FindCacheDir();
    if(!s_Cache.empty()) LoadBad();
    DzLog(false, "DzModels: loader hooks clump %s atomic %s ped %s texdb %s", LoadClump_orig ? "ok" : "-", LoadAtomic_orig ? "ok" : "-", PedSetClump_orig ? "ok" : "-", DbGetTexture_orig ? "ok" : "-");
    DzLog(false, "DzModels: hook %s, txd %s, rw %s, cache %s", CBTO_orig ? "ok" : "-", (AddTxdSlot && TxdCreate && s_TxdPool) ? "ok" : "-",
                 (TexCreate && DictAddTex) ? "ok" : "-", s_Cache.empty() ? "-" : s_Cache.c_str());
}

void DzModels::Tick(bool on, const int* objIds, int nObj)
{
    if(s_Disabled) return;
    if(on && !s_Started && !s_Cache.empty() && CBTO_orig && !s_CloudBusy)
    {
        s_Started = true; s_Busy = true;
        std::vector<int> ids(objIds, objIds + (nObj > 0 ? nObj : 0));
        std::thread(DownloadThread, ids).detach();
    }
    if(!s_ListReady) return;
    uint64_t now = NowMs();
    if(on && !s_Checked && now >= s_NextCheck)
    {
        int r = CheckEngine();
        if(r == 0) s_NextCheck = now + 2000;
        else
        {
            s_Checked = true; s_Ok = r > 0;
            s_Engine = r;
            if(!s_Ok) { s_Disabled = true; std::lock_guard<std::mutex> lk(s_M); s_Err = "engine check failed (" + std::to_string(r) + ")"; DzLog(true, "DzModels: engine check %d - models stay GTA", r); return; }
            DzLog(false, "DzModels: engine ok");
        }
    }
    if(!s_Ok) return;
    s_WOn = on && s_WWant && s_WReady;   // 4.05.5
    // ~110 entries, a switch is a few field checks (a model in use simply waits)
    for(Entry* e : s_E)
    {
        if(s_ById[e->id] != e) continue;
        bool want = on && e->wanted && e->file == 1 && !e->bad && !s_Bad.count(e->id) && (e->kind != 'v' || s_PoolOk);
        if(e->bad && e->swapped) want = false;
        if(want == e->swapped) continue;
        Switch(e, want);
    }
}

bool DzModels::Pending(int model)
{   // 4.00: downloaded, wanted, engine ok, but the game is still using the GTA model
    Entry* e = (model > 0 && model < kTxdBase) ? s_ById[model] : nullptr;
    return s_Ok && e && s_ById[model] == e && e->wanted && e->file == 1 && !e->bad && !e->swapped && !s_Bad.count(model);
}
int DzModels::SwapCount() { int n = 0; for(Entry* e : s_E) if(e->swapped && !e->bad) ++n; return n; }
void DzModels::GetStatus(Status& st)
{
    st = Status();
    st.engine = s_Checked ? s_Engine : 0; st.disabled = s_Disabled; st.hooked = CBTO_orig != nullptr; st.cache = !s_Cache.empty();
    for(Entry* e : s_E)
    {
        if(s_ById[e->id] != e || !e->wanted) continue;
        ++st.wanted;
        if(e->file == 1) ++st.files;
        if(e->bad || s_Bad.count(e->id)) ++st.failed;
        else if(e->swapped) { ++st.swapped; if(e->loadedOnce) ++st.loaded; if(e->texMade == 0) ++st.noTex; }
        else if(e->file == 1) ++st.waiting;
    }
}
void DzModels::SetLogPath(const char* path)
{
    std::lock_guard<std::mutex> lk(s_LogM);
    s_LogPath = path ? path : "";
    if(s_LogPath.empty()) return;
    if(FILE* f = fopen(s_LogPath.c_str(), "wb"))
    {
        fputs("MP - Project Day Z Legacy MTA model loader log (send this file if the models stay GTA)\n", f);
        fprintf(f, "streamer hook: %s | txd store: %s | RW textures: %s | model table: %s | streaming table: %s\n",
                CBTO_orig ? "ok" : "MISSING", (AddTxdSlot && TxdCreate && s_TxdPool) ? "ok" : "MISSING", (TexCreate && DictAddTex) ? "ok" : "MISSING",
                s_MI ? "ok" : "MISSING", s_SI ? "ok" : "MISSING");
        fprintf(f, "4.01 loader hooks: clump %s | atomic %s | ped SetClump %s | RW stream %s\n", LoadClump_orig ? "ok" : "MISSING",
                LoadAtomic_orig ? "ok" : "MISSING", PedSetClump_orig ? "ok" : "MISSING", (StreamOpen && StreamClose) ? "ok" : "MISSING");
        fprintf(f, "cache: %s\n", s_Cache.empty() ? "NONE (no writable folder - nothing can be downloaded)" : s_Cache.c_str());
        if(!s_Bad.empty()) { fputs("skipped (the game closed twice while loading them):", f); for(int id : s_Bad) fprintf(f, " %d", id); fputs("  - /dzmodels reset to try them again\n", f); }
        fclose(f);
    }
}
void DzModels::ResetBad()
{
    if(!s_Cache.empty()) { unlink((s_Cache + "/bad.txt").c_str()); unlink((s_Cache + "/inhook.txt").c_str()); }
    s_Bad.clear();
    for(Entry* e : s_E) { e->bad = false; e->fails = 0; e->noStream = false; }
    s_Disabled = false; s_Checked = false; s_Ok = false; s_NextCheck = 0;
    DzLog(false, "DzModels: reset - every model is tried again");
}
bool DzModels::IsReplaced(int model) { Entry* e = (model > 0 && model < kTxdBase) ? s_ById[model] : nullptr; return e && e->swapped && !e->bad; }
bool DzModels::Available() { return s_Ok; }
void DzModels::SetWorld(bool want) { if(!want) s_WOn = false; s_WWant = want; }
void DzModels::World(int& made, int& hits, bool& ready) { made = s_WMade; hits = s_WHits; ready = s_WReady; }
void DzModels::Progress(int& done, int& total, int& mbDone, int& mbTotal, bool& busy)
{
    done = s_Done; total = s_Total; mbDone = s_KbDone / 1024; mbTotal = s_KbTotal / 1024; busy = s_Busy;
}
void DzModels::GetDownload(Download& d)
{
    d.started = s_Started; d.busy = s_Busy; d.listReady = s_ListReady;
    d.filesDone = s_FDone; d.filesTotal = s_FTotal; d.filesFailed = s_FFailed; d.filesDownloaded = s_FDownloaded;
    d.bytesTotal = s_BTotal; d.bytesDone = s_BDone + s_BCur; if(d.bytesDone > d.bytesTotal) d.bytesDone = d.bytesTotal;
    d.modelsReady = s_Done; d.modelsTotal = s_Total;
    std::lock_guard<std::mutex> lk(s_M);
    snprintf(d.file, sizeof(d.file), "%s", s_CurFile.c_str());
    snprintf(d.host, sizeof(d.host), "%s", s_CurHost.empty() ? "raw.githubusercontent.com (wagu)" : s_CurHost.c_str());
}
const char* DzModels::LastError() { static std::string c; std::lock_guard<std::mutex> lk(s_M); c = s_Err; return c.c_str(); }

// ---------------- 4.02: Cloud Storage page (launcher) ----------------
// The same files as the in-game download (Host/DayZ/models on GitHub, the mtadayz fork as the second
// host), grouped by folder. The player can get a group before joining, or delete it from the phone and
// download it again (a broken / old copy, or just to free the space).
namespace
{
struct CItem { int id = 0; std::string dff, txd; uint32_t dffSize = 0, txdSize = 0, dffCrc = 0, txdCrc = 0; int group = 0; };
const char* const kGroupDir[DzModels::kCloudGroups] = { "zombies", "players", "weapons", "items", "vehicles", "buildings", "animals", "world" };
std::vector<CItem> s_Cl;                 // the manifest (cloud page copy)
std::atomic<bool> s_ClReady { false }, s_ClListBusy { false };
std::atomic<int> s_ClDone { 0 }, s_ClTotal { 0 }, s_ClFailed { 0 }, s_ClGroup { -2 };
std::atomic<uint64_t> s_ClBDone { 0 }, s_ClBTotal { 0 };
std::string s_ClMsg;                     // last result line (under s_M)
struct ClOnPhone { uint64_t bytes = 0; int files = 0; };
ClOnPhone s_ClHave[DzModels::kCloudGroups];
uint64_t s_ClScanAt = 0;

int GroupOf(const std::string& dff)
{
    size_t sl = dff.find('/'); std::string d = sl == std::string::npos ? dff : dff.substr(0, sl);
    for(int g = 0; g < DzModels::kCloudGroups; ++g) if(d == kGroupDir[g]) return g;
    return 3;   // anything else = items
}
uint64_t FileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? (uint64_t)st.st_size : 0; }
void ClMsg(const std::string& m) { std::lock_guard<std::mutex> lk(s_M); s_ClMsg = m; }

bool ClLoadList()
{
    std::string text, why; const std::string mpath = s_Cache + "/manifest.txt";
    if(Online::HttpsFetch(std::string(kBaseUrls[0]) + "manifest.txt", text, 30000, why, 512 * 1024) && text.find("Project Day Z") != std::string::npos)
        WriteFile(mpath, text.data(), text.size());
    else { std::vector<uint8_t> d; if(ReadFile(mpath, d)) text.assign(d.begin(), d.end()); else { ClMsg("Could not get the file list: " + why); return false; } }
    std::vector<Entry*> tmp; std::vector<WFile> wf; ParseManifest(text, tmp, &wf);
    std::vector<CItem> out;
    for(auto& w : wf) { CItem c; c.dff = w.rel; c.dffSize = w.size; c.dffCrc = w.crc; c.group = GroupOf(w.rel); out.push_back(c); }   // 4.05.5: one file, no model
    for(Entry* e : tmp)
    {
        CItem c; c.id = e->id; c.dff = e->dff; c.txd = e->txd; c.dffSize = e->dffSize; c.txdSize = e->txdSize;
        c.dffCrc = e->dffCrc; c.txdCrc = e->txdCrc; c.group = GroupOf(e->dff); out.push_back(c); delete e;
    }
    { std::lock_guard<std::mutex> lk(s_M); s_Cl.swap(out); }
    if(!s_Busy && !s_WReady) s_WFiles = wf;   // 4.05.5
    s_ClReady = true; s_ClScanAt = 0;
    return true;
}
void ClListThread() { ClLoadList(); s_ClListBusy = false; }
void ClDownloadThread(int group)
{
    if(!s_ClReady && !ClLoadList()) { s_CloudBusy = false; s_ClGroup = -2; return; }
    std::vector<CItem> list; { std::lock_guard<std::mutex> lk(s_M); list = s_Cl; }
    uint64_t bt = 0; int ft = 0;
    for(auto& c : list) if(group < 0 || c.group == group) { bt += c.dffSize + c.txdSize; ft += c.txd.empty() ? 1 : 2; }
    s_ClBTotal = bt; s_ClTotal = ft; s_ClBDone = 0; s_ClDone = 0; s_ClFailed = 0;
    for(auto& c : list)
    {
        if(group >= 0 && c.group != group) continue;
        const std::string* f[2] = { &c.dff, &c.txd }; uint32_t sz[2] = { c.dffSize, c.txdSize }, cr[2] = { c.dffCrc, c.txdCrc };
        bool okAll = true;
        for(int k = 0; k < 2; ++k)
        {
            if(f[k]->empty()) continue;
            bool ok = Fetch(*f[k], sz[k], cr[k], false); okAll = okAll && ok;
            if(!ok) ++s_ClFailed;
            s_ClBDone += sz[k]; ++s_ClDone;
        }
        if(okAll) if(Entry* e = (c.id > 0 && c.id < kTxdBase) ? s_ById[c.id] : nullptr) if(e->dff == c.dff && e->file != 1) e->file = 1;   // the game can use it now
    }
    if(!s_WReady && !s_Busy && !s_WFiles.empty())   // 4.05.5: the world textures are complete now
    {
        bool all = true; for(auto& w : s_WFiles) all = all && FileSize(CacheName(w.rel)) == w.size;
        if(all && LoadWorldIdx()) s_WReady = true;
    }
    char m[160]; snprintf(m, sizeof(m), s_ClFailed ? "Download finished: %d of %d files failed (%s)." : "Download finished: %d files on the phone.",
                          s_ClFailed ? s_ClFailed.load() : s_ClDone.load(), s_ClTotal.load(), s_Err.c_str());
    ClMsg(m); DzLog(s_ClFailed > 0, "DzModels: cloud page %s", m);
    s_ClScanAt = 0; s_ClGroup = -2; s_CloudBusy = false;
}
} // namespace

void DzModels::CloudRefresh()
{
    if(s_Cache.empty() || s_ClListBusy || s_CloudBusy) return;
    s_ClListBusy = true; std::thread(ClListThread).detach();
}
bool DzModels::CloudDownload(int group)
{
    if(s_Cache.empty() || s_CloudBusy || s_Busy || s_ClListBusy) return false;
    s_CloudBusy = true; s_ClGroup = group; ClMsg("");
    std::thread(ClDownloadThread, group).detach();
    return true;
}
int DzModels::CloudDelete(int group)
{
    if(s_Cache.empty() || s_CloudBusy || s_Busy) return -1;
    std::vector<CItem> list; { std::lock_guard<std::mutex> lk(s_M); list = s_Cl; }
    int removed = 0, kept = 0;
    for(auto& c : list)
    {
        if(group >= 0 && c.group != group) continue;
        Entry* e = (c.id > 0 && c.id < kTxdBase) ? s_ById[c.id] : nullptr;
        if(e && e->dff == c.dff && e->swapped) { ++kept; continue; }   // the game is using this model right now
        if(c.id == 0 && s_WOn) { ++kept; continue; }                 // 4.05.5: world textures in use
        if(c.id == 0) s_WReady = false;
        if(unlink(CacheName(c.dff).c_str()) == 0) ++removed;
        if(!c.txd.empty() && unlink(CacheName(c.txd).c_str()) == 0) ++removed;
        if(e && e->dff == c.dff) e->file = 0;   // the next Day Z visit downloads it again
    }
    if(s_Started && !s_Busy) s_Started = false;   // DzModels::Tick starts the download again on the Day Z server
    char m[160];
    if(kept) snprintf(m, sizeof(m), "Deleted %d files. %d models are in use on the Day Z server - leave it to delete them.", removed, kept);
    else snprintf(m, sizeof(m), "Deleted %d files. Download again here or join the Day Z server.", removed);
    ClMsg(m); DzLog(false, "DzModels: cloud page %s", m);
    s_ClScanAt = 0;
    return removed;
}
void DzModels::GetCloud(Cloud& c)
{
    c = Cloud();
    c.cache = !s_Cache.empty(); c.listReady = s_ClReady; c.listBusy = s_ClListBusy; c.busy = s_CloudBusy || s_Busy; c.gameDownload = s_Busy;
    c.group = s_ClGroup; c.filesDone = s_ClDone; c.filesTotal = s_ClTotal; c.filesFailed = s_ClFailed;
    c.bytesTotal = s_ClBTotal; c.bytesDone = s_ClBDone + (s_CloudBusy ? (uint64_t)s_BCur : 0); if(c.bytesDone > c.bytesTotal) c.bytesDone = c.bytesTotal;
    c.inUse = SwapCount();
    std::lock_guard<std::mutex> lk(s_M);
    snprintf(c.msg, sizeof(c.msg), "%s", s_ClMsg.c_str());
    snprintf(c.file, sizeof(c.file), "%s", s_CloudBusy ? s_CurFile.c_str() : "");
    // what is on the phone (a stat per file, at most twice a second)
    uint64_t now = NowMs();
    if(s_ClReady && now - s_ClScanAt > 500)
    {
        s_ClScanAt = now;
        for(auto& h : s_ClHave) h = ClOnPhone();
        for(auto& it : s_Cl)
        {
            uint64_t a = FileSize(CacheName(it.dff)), b = it.txd.empty() ? 0 : FileSize(CacheName(it.txd));
            if(a == it.dffSize) { s_ClHave[it.group].bytes += a; ++s_ClHave[it.group].files; }
            if(!it.txd.empty() && b == it.txdSize) { s_ClHave[it.group].bytes += b; ++s_ClHave[it.group].files; }
        }
    }
    for(int g = 0; g < kCloudGroups; ++g) { c.g[g].name = kGroupDir[g]; c.g[g].bytesOnPhone = s_ClHave[g].bytes; c.g[g].filesOnPhone = s_ClHave[g].files; }
    for(auto& it : s_Cl) { CloudGroup& g = c.g[it.group]; if(it.id > 0) ++g.models; g.filesTotal += it.txd.empty() ? 1 : 2; g.bytesTotal += it.dffSize + it.txdSize; }
}
