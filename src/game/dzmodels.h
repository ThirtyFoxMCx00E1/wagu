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
    const char* LastError();
}
