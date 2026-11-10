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
    // 4.00: status + a log file the player can read (files/TYFMC/MP/dzmodels_log.txt)
    bool Pending(int model);               // ready to switch, but the game still uses the GTA model
    int  SwapCount();
    struct Status { int engine = 0; bool disabled = false, hooked = false, cache = false; int wanted = 0, files = 0, swapped = 0, loaded = 0, waiting = 0, failed = 0, noTex = 0; };
    void GetStatus(Status& st);
    void SetLogPath(const char* path);
    void ResetBad();
    // 4.05.5: GTA V world textures (Host/DayZ/models/world) - downloaded + used on the Day Z server
    void SetWorld(bool want);
    void World(int& made, int& hits, bool& ready);                       // /dzmodels reset: try the skipped models again

    // 4.02: the launcher Cloud Storage page - the model pack per folder: download before joining,
    // or delete it from the phone (then download it again here or on the Day Z server)
    enum { kCloudGroups = 8 };             // zombies, players, weapons, items, vehicles, buildings, animals, world (4.05.5)
    struct CloudGroup { const char* name = ""; int models = 0, filesTotal = 0, filesOnPhone = 0; unsigned long long bytesTotal = 0, bytesOnPhone = 0; };
    struct Cloud
    {
        bool cache = false, listReady = false, listBusy = false, busy = false, gameDownload = false;
        int group = -2;                    // being downloaded: -1 = everything, -2 = none
        int filesDone = 0, filesTotal = 0, filesFailed = 0, inUse = 0;
        unsigned long long bytesDone = 0, bytesTotal = 0;
        CloudGroup g[kCloudGroups];
        char msg[192] = ""; char file[96] = "";
    };
    // 4.08: base building - give a (swapped) object model a collision made of boxes (model space,
    // min xyz + max xyz each, 6 floats a box). The GTA collision of the host model is replaced while
    // the game runs; called again every second (the collision streamer may put the GTA one back).
    bool SetBoxCollision(int model, const float* boxes, int nBoxes, uint8_t material);
    void GetCloud(Cloud& c);               // main thread
    void CloudRefresh();                   // get the file list (background)
    bool CloudDownload(int group);         // -1 = everything. false = busy
    int  CloudDelete(int group);           // -1 = everything. models in use stay. returns files deleted (-1 busy)
}
