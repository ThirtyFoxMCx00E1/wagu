// 3.61: Project Day Z Legacy model pack - the MTA DayZ models (github.com/NullSystemWorks/mtadayz,
// DayZ/models) hosted on GitHub (wagu repo, Host/DayZ/models + manifest.txt). Nothing has to be put in
// the MP folder: the game downloads the dff / txd it needs into its own cache and swaps them in at
// runtime while you are on the Day Z server (players, zombies, guns, loot items, the UAZ), then puts
// the GTA models back when you leave.
#pragma once

namespace DzModels
{
    void Init(void* gameHandle);           // once, from Game::Init (resolves the engine, hooks the streamer)
    // every frame (main thread). on = on the Day Z server and the setting is on. objIds = the item
    // models Day Z uses (only these object ids are replaced, nothing else of the GTA map changes).
    void Tick(bool on, const int* objIds, int nObj);
    bool IsReplaced(int model);            // the MTA model is in the game right now
    bool Available();                      // engine checks passed (else everything stays GTA)
    // download state for the HUD: done / total files, bytes, busy = still downloading
    void Progress(int& done, int& total, int& mbDone, int& mbTotal, bool& busy);
    // 3.62: the download window on the Day Z connection screen
    struct Download
    {
        bool started, busy, listReady;
        int filesDone, filesTotal, filesFailed, filesDownloaded, modelsReady, modelsTotal;
        unsigned long long bytesDone, bytesTotal;
        char file[96]; char host[80];
    };
    void GetDownload(Download& d);
    const char* LastError();
    // 4.00: status + a log file the player can read (files/MP/dzmodels_log.txt)
    bool Pending(int model);               // ready to switch, but the game still uses the GTA model
    int  SwapCount();
    struct Status { int engine = 0; bool disabled = false, hooked = false, cache = false; int wanted = 0, files = 0, swapped = 0, loaded = 0, waiting = 0, failed = 0, noTex = 0; };
    void GetStatus(Status& st);
    void SetLogPath(const char* path);
    void ResetBad();                       // /dzmodels reset: try the skipped models again
}
