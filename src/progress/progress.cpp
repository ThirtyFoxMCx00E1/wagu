#include <mod/amlmod.h>
#include <mod/logger.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <zlib.h>
#include <vector>
#include "progress.h"
#include "save100.h"
#include "../game/game.h"
#include "../mp.h"

#define COL_SYS    0x99CCFFFF
#define COL_OK     0x33CC55FF
#define COL_WARN   0xFFDD33FF
#define NUM_SLOTS  10

enum ePhase { PH_IDLE, PH_WAIT, PH_LOADING_100, PH_ACTIVE, PH_LOADING_OWN };

static char     s_szDir[400] = "", s_szBackup[420] = "", s_szState[420] = "", s_szCustom[420] = "";
static Progress::Callback s_Before = nullptr, s_After = nullptr;
static ePhase   s_Phase = PH_IDLE;
static int      s_nOrig = -1, s_nTemp = -1;
static uint32_t s_nHad = 0;            // slots that had a file before (bit = slot)
static uint32_t s_nKeep = 0;           // 2.19.7: slots we couldn't read (empty / locked): never touched
static uint64_t s_nPhaseAt = 0, s_nOffDefaultAt = 0, s_nReadyAt = 0;
static bool     s_bSawNotReady = false, s_bFailed = false;
static char     s_szStatus[128] = "";

// 3.33: save-load crash guard. Two crash logs (realme RE6054, MP 3.32, 69 mods) died on the game's own thread inside
// DoGameRestart -> CGame::InitialiseWhenRestarting -> CGenericGameStorage::GenericLoad (CPools::LoadObjectPool ->
// CPopulation::ConvertToDummyObject -> CEntity::GetBoundCentre, and CFileLoader::LoadObjectInstance -> CDummyObject ->
// CPlaceable) with Project2DFX, DynamicSeason, CLEOPlus and a limit adjuster hooked into that restart. The game's
// restart code is not ours to change, but MP is what asks for the 100% save, so it must not ask again and again:
// loading.txt exists while a load we asked for is running. If it is still there at the next start, the game
// crashed in that load: 100% progress stays off for this start and the next 2 (your own progress is used) so a
// mod conflict can't crash the game every time you join the default server.
#define PROGRESS_SKIP_STARTS 3
static char     s_szLoadMark[420] = "", s_szSkipFile[420] = "";
static bool     s_bSkipped = false, s_bSkipMsg = false;
static void MarkLoadStart()
{
    if(!s_szLoadMark[0]) return;
    FILE* f = fopen(s_szLoadMark, "w");
    if(f) { fprintf(f, "a save load was requested\n"); fflush(f); fsync(fileno(f)); fclose(f); }
}
static void MarkLoadEnd() { if(s_szLoadMark[0]) unlink(s_szLoadMark); }
static int  ReadSkip()
{
    FILE* f = fopen(s_szSkipFile, "r"); if(!f) return 0;
    int n = 0; if(fscanf(f, "%d", &n) != 1) n = 0;
    fclose(f);
    return n < 0 ? 0 : (n > 20 ? 20 : n);
}
static void WriteSkip(int n)
{
    if(n <= 0) { unlink(s_szSkipFile); return; }
    FILE* f = fopen(s_szSkipFile, "w");
    if(f) { fprintf(f, "%d\n", n); fflush(f); fsync(fileno(f)); fclose(f); }
}

static void Msg(uint32_t col, const char* fmt, ...)
{
    char b[200]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof(b), fmt, ap); va_end(ap);
    MP::AddSystemMessage(col, b);
    logger->Info("[Progress] %s", b);
}

static bool Exists(const char* p, time_t* mtime = nullptr)
{
    struct stat st; if(stat(p, &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if(mtime) *mtime = st.st_mtime;
    return true;
}

static bool ReadFile(const char* p, std::vector<uint8_t>& out)
{
    FILE* f = fopen(p, "rb"); if(!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if(n <= 0 || n > 8 * 1024 * 1024) { fclose(f); return false; }
    out.resize((size_t)n);
    bool ok = fread(out.data(), 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    return ok;
}

static bool WriteFile(const char* p, const uint8_t* d, size_t n)
{
    char tmp[460]; snprintf(tmp, sizeof(tmp), "%s.mptmp", p);
    FILE* f = fopen(tmp, "wb"); if(!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    ok = (fflush(f) == 0) && ok; fsync(fileno(f));
    fclose(f);
    if(!ok || rename(tmp, p) != 0) { unlink(tmp); return false; }
    return true;
}

static bool CopyFile(const char* from, const char* to)
{
    std::vector<uint8_t> d;
    return ReadFile(from, d) && WriteFile(to, d.data(), d.size());
}

static void BackupPath(int slot, char* out, int n) { snprintf(out, n, "%s/GTASAsf%d.b", s_szBackup, slot + 1); }

static void SaveState()
{
    FILE* f = fopen(s_szState, "w");
    if(!f) { logger->Error("[Progress] can't write %s", s_szState); return; }
    fprintf(f, "active 1\norig %d\ntemp %d\nhad %u\nkeep %u\n", s_nOrig, s_nTemp, s_nHad, s_nKeep);
    fflush(f); fsync(fileno(f)); fclose(f);
}

static bool LoadState()
{
    FILE* f = fopen(s_szState, "r"); if(!f) return false;
    char k[32]; long v; int active = 0;
    while(fscanf(f, "%31s %ld", k, &v) == 2)
    {
        if(!strcmp(k, "active")) active = (int)v;
        else if(!strcmp(k, "orig")) s_nOrig = (int)v;
        else if(!strcmp(k, "temp")) s_nTemp = (int)v;
        else if(!strcmp(k, "had")) s_nHad = (uint32_t)v;
        else if(!strcmp(k, "keep")) s_nKeep = (uint32_t)v;
    }
    fclose(f);
    return active == 1;
}

static void ClearState() { unlink(s_szState); }

// Put every save slot back exactly as it was before the default server.
static bool RestoreAll()
{
    bool ok = true;
    for(int s = 0; s < NUM_SLOTS; ++s)
    {
        char path[460], bak[460];
        if(!Game::SaveSlotPath(s, path, sizeof(path))) continue;
        if(s_nKeep & (1u << s)) continue;   // 2.19.7: left exactly as it was
        BackupPath(s, bak, sizeof(bak));
        if(s_nHad & (1u << s))
        {
            if(Exists(bak)) { if(!CopyFile(bak, path)) { ok = false; logger->Error("[Progress] restore %s failed", path); } }
            else { ok = false; logger->Error("[Progress] backup %s missing", bak); }
        }
        else if(Exists(path)) unlink(path);   // made while on the default server
    }
    return ok;
}

static void DeleteBackups()
{
    for(int s = 0; s < NUM_SLOTS; ++s) { char bak[460]; BackupPath(s, bak, sizeof(bak)); unlink(bak); }
}

static bool Get100(std::vector<uint8_t>& out)
{
    if(Exists(s_szCustom) && ReadFile(s_szCustom, out) && out.size() > 1000 && !memcmp(out.data(), "BLOCK", 5)) return true;
    out.resize(kSave100Size);
    uLongf n = kSave100Size;
    return uncompress(out.data(), &n, kSave100Z, kSave100ZSize) == Z_OK && n == kSave100Size;
}

void Progress::Init(const char* mpDir, Callback beforeLoad, Callback afterLoad)
{
    s_Before = beforeLoad; s_After = afterLoad;
    snprintf(s_szDir, sizeof(s_szDir), "%s/progress", mpDir);
    mkdir(s_szDir, 0777);
    snprintf(s_szBackup, sizeof(s_szBackup), "%s/backup", s_szDir);
    mkdir(s_szBackup, 0777);
    snprintf(s_szState, sizeof(s_szState), "%s/state.txt", s_szDir);
    snprintf(s_szCustom, sizeof(s_szCustom), "%s/100.b", s_szDir);   // optional: your own 100% save
    snprintf(s_szLoadMark, sizeof(s_szLoadMark), "%s/loading.txt", s_szDir);
    snprintf(s_szSkipFile, sizeof(s_szSkipFile), "%s/skip.txt", s_szDir);
    s_bSkipped = false; s_bSkipMsg = false;
    if(LoadState())
    {
        // the game closed while the 100% save was in use: put the player's own saves back first
        bool ok = RestoreAll();
        logger->Info("[Progress] restored your own save files after the last session (%s)", ok ? "ok" : "with errors");
        if(ok) { ClearState(); DeleteBackups(); }
    }
    // 3.33: did the game crash while a save we asked for was loading? Then no load this start (and the next 2)
    int skip = ReadSkip();
    if(Exists(s_szLoadMark))
    {
        MarkLoadEnd();
        skip = PROGRESS_SKIP_STARTS;
        logger->Error("[Progress] the game crashed while a save was loading last time: 100%% progress is off for %d game starts", PROGRESS_SKIP_STARTS);
    }
    if(skip > 0) { s_bSkipped = true; s_bFailed = true; WriteSkip(skip - 1); }
    s_Phase = PH_IDLE;
}

static bool StartLoad(int slot)
{
    if(s_Before) s_Before();
    MarkLoadStart();   // 3.33: removed when the load is done (or refused); still there at the next start = the game crashed in it
    const bool ok = Game::RequestLoadSlot(slot);
    if(!ok) MarkLoadEnd();
    return ok;
}

// 2.19.7: a failure used to leave the phase at PH_WAIT, so Enter() ran again every frame -
// copying every save file + a chat message each frame (the spam + lag). Now it gives up once
// for the session.
static void Fail(const char* what)
{
    Msg(COL_WARN, "100%% progress: %s - using your own progress (until you restart the game).", what);
    s_bFailed = true;
    s_Phase = PH_IDLE;
}

static void Enter(uint64_t now)
{
    // newest save = the player's last mission save point
    s_nOrig = -1; s_nHad = 0; s_nKeep = 0; time_t best = 0;
    mkdir(s_szDir, 0777); mkdir(s_szBackup, 0777);
    for(int s = 0; s < NUM_SLOTS; ++s)
    {
        char path[460]; time_t mt;
        if(!Game::SaveSlotPath(s, path, sizeof(path)) || !Exists(path, &mt)) continue;
        std::vector<uint8_t> d;
        if(!ReadFile(path, d))
        {
            // 2.19.7: an empty / unreadable slot isn't a reason to give up: leave it alone
            logger->Info("[Progress] slot %d (%s) can't be read, left as it is", s + 1, path);
            s_nKeep |= 1u << s;
            continue;
        }
        char bak[460]; BackupPath(s, bak, sizeof(bak));
        if(!WriteFile(bak, d.data(), d.size()))
        {
            logger->Error("[Progress] can't write backup %s (errno %d)", bak, errno);
            DeleteBackups();
            Fail("couldn't back up your saves");
            return;
        }
        s_nHad |= 1u << s;
        if(mt >= best) { best = mt; s_nOrig = s; }
    }
    s_nTemp = -1;
    for(int s = 0; s < 8 && s_nTemp < 0; ++s) if(!((s_nHad | s_nKeep) & (1u << s))) s_nTemp = s;   // free manual slot
    for(int s = 7; s >= 0 && s_nTemp < 0; --s) if(!(s_nKeep & (1u << s))) s_nTemp = s;            // backed up, put back later
    if(s_nTemp < 0) { DeleteBackups(); Fail("no save slot to use"); return; }
    std::vector<uint8_t> d; char path[460];
    if(!Get100(d) || !Game::SaveSlotPath(s_nTemp, path, sizeof(path)) || !WriteFile(path, d.data(), d.size()))
    { RestoreAll(); DeleteBackups(); Fail("couldn't write the 100% save"); return; }
    SaveState();
    if(!StartLoad(s_nTemp))
    { RestoreAll(); ClearState(); DeleteBackups(); Fail("the game didn't accept the 100% save"); return; }
    Msg(COL_SYS, "Default server: loading 100%% progress (all missions done)...");
    s_Phase = PH_LOADING_100; s_nPhaseAt = now; s_bSawNotReady = false; s_nReadyAt = 0;
}

static void Leave(uint64_t now)
{
    bool ok = RestoreAll();
    if(!ok) { Msg(COL_WARN, "Couldn't put all your saves back - they're kept in files/MP/progress/backup."); s_Phase = PH_IDLE; return; }
    ClearState(); DeleteBackups();
    if(s_nOrig < 0) { Msg(COL_WARN, "Left the default server. You had no save of your own - start a New Game from the menu."); s_Phase = PH_IDLE; return; }
    if(!StartLoad(s_nOrig)) { Msg(COL_WARN, "Left the default server. Load your save (slot %d) from the menu to continue your own story.", s_nOrig + 1); s_Phase = PH_IDLE; return; }
    Msg(COL_SYS, "Left the default server: loading your own save (slot %d)...", s_nOrig + 1);
    s_Phase = PH_LOADING_OWN; s_nPhaseAt = now; s_bSawNotReady = false; s_nReadyAt = 0;
}

// true once the requested load has run and the game is playing again for 2 s
static int LoadDone(uint64_t now, bool canLoad)
{
    if(!Game::IsReady()) s_bSawNotReady = true;
    bool pending = Game::IsLoadPending();
    if((s_bSawNotReady || !pending) && canLoad && now - s_nPhaseAt > 1500)
    {
        if(!s_nReadyAt) s_nReadyAt = now;
        if(now - s_nReadyAt > 2000) return 1;
    }
    else s_nReadyAt = 0;
    if(pending && now - s_nPhaseAt > 15000) { Game::CancelLoad(); return -1; }   // never picked up
    if(now - s_nPhaseAt > 90000) return -1;
    return 0;
}

static bool s_bHold = false;
void Progress::SetHold(bool hold) { s_bHold = hold; }
void Progress::Process(uint64_t now, bool enabled, bool onDefault, bool connectedElsewhere, bool canLoad)
{
    if(s_bHold && (s_Phase == PH_IDLE || s_Phase == PH_WAIT || s_Phase == PH_ACTIVE))
    {   // 3.29: on Mega Vil no save is loaded or swapped (the server's day + weather would jump)
        if(s_Phase == PH_WAIT) s_Phase = PH_IDLE;
        if(s_Phase == PH_ACTIVE) s_nOffDefaultAt = 0;
        snprintf(s_szStatus, sizeof(s_szStatus), "Blocked on Mega Vil Futuristic (the server sets day + weather)");
        return;
    }
    switch(s_Phase)
    {
        case PH_IDLE:
            snprintf(s_szStatus, sizeof(s_szStatus), "%s", !enabled ? "Off - your own progress everywhere" :
                     s_bSkipped ? "Off for a few game starts (the game crashed in a save load)" :
                     s_bFailed ? "Not available on this phone (see chat)" : "Join the default server to play at 100%");
            if(enabled && onDefault && s_bSkipped && !s_bSkipMsg)
            {
                s_bSkipMsg = true;
                Msg(COL_WARN, "100%% progress is off for now: the game crashed while loading a save last time (another mod can clash with the game's save loading). Your own progress is used; it tries again in a few game starts.");
            }
            if(enabled && onDefault && !s_bFailed) { s_Phase = PH_WAIT; s_nPhaseAt = now; }
            break;
        case PH_WAIT:      // a few seconds on the server first (feed, teleport), and not mid-mission
            if(!enabled || !onDefault) { s_Phase = PH_IDLE; break; }
            if(canLoad && now - s_nPhaseAt > 4000) Enter(now);
            break;
        case PH_LOADING_100:
        {
            snprintf(s_szStatus, sizeof(s_szStatus), "Loading 100%% save...");
            int r = LoadDone(now, canLoad);
            if(r != 0) { MarkLoadEnd(); if(r > 0) WriteSkip(0); }   // 3.33: the load ran: no crash
            if(r < 0)
            {
                Msg(COL_WARN, "100%% progress: the save didn't load - using your own progress.");
                s_bFailed = true; RestoreAll(); ClearState(); DeleteBackups(); s_Phase = PH_IDLE;
            }
            else if(r > 0)
            {
                // put the borrowed slot back right away (backups stay until you leave)
                char path[460], bak[460];
                if(Game::SaveSlotPath(s_nTemp, path, sizeof(path)))
                {
                    BackupPath(s_nTemp, bak, sizeof(bak));
                    if(s_nHad & (1u << s_nTemp)) CopyFile(bak, path); else unlink(path);
                }
                float pc = Game::ProgressPercent();
                if(pc >= 0) Msg(COL_OK, "100%% progress loaded (%.0f%%): every mission is done while you're on the default server.", pc);
                else        Msg(COL_OK, "100%% progress loaded: every mission is done while you're on the default server.");
                s_Phase = PH_ACTIVE; s_nOffDefaultAt = 0;
                if(s_After) s_After();
            }
            break;
        }
        case PH_ACTIVE:
            snprintf(s_szStatus, sizeof(s_szStatus), "On - 100%% while on the default server");
            if(enabled && onDefault) { s_nOffDefaultAt = 0; break; }
            if(!s_nOffDefaultAt) s_nOffDefaultAt = now;
            // short connection drops / host takeover: wait a bit before going back
            if(!enabled || connectedElsewhere || now - s_nOffDefaultAt > 15000) { if(canLoad || now - s_nOffDefaultAt > 60000) Leave(now); }
            break;
        case PH_LOADING_OWN:
        {
            snprintf(s_szStatus, sizeof(s_szStatus), "Loading your own save...");
            int r = LoadDone(now, canLoad);
            if(r != 0) MarkLoadEnd();   // 3.33
            if(r < 0) { Msg(COL_WARN, "Load your save (slot %d) from the menu to continue your own story.", s_nOrig + 1); s_Phase = PH_IDLE; }
            else if(r > 0) { Msg(COL_OK, "Your own progress is back (slot %d).", s_nOrig + 1); s_Phase = PH_IDLE; }   // stay at your save point
            break;
        }
    }
}

bool Progress::Pending() { return s_Phase == PH_WAIT || s_Phase == PH_LOADING_100 || s_Phase == PH_LOADING_OWN; }
bool Progress::IsActive() { return s_Phase == PH_LOADING_100 || s_Phase == PH_ACTIVE; }
const char* Progress::StatusText() { return s_szStatus; }
