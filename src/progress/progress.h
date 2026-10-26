// 100% progress on the default server (2.10).
// Joining the default server: every save file is backed up to files/MP/progress/backup, the
// built-in 100% save (progress/GTASAsf1.b) is written into a free slot and loaded, then that slot
// is put back. Leaving the default server: all save files are put back exactly as they were and
// your own latest save (your last mission save point) is loaded again. Custom servers always use
// your own progress. If the game closes while you are on the default server, the next start
// restores the save files first.
#pragma once
#include <stdint.h>

namespace Progress
{
    typedef void (*Callback)();
    // mpDir = files/MP. beforeLoad: remove network avatars (the game rebuilds the world),
    // afterLoad: called once the loaded save is running (teleport to the spawn).
    void Init(const char* mpDir, Callback beforeLoad, Callback afterLoad);
    // onDefault = connected to the default server, canLoad = game running, not paused, not on a mission
    void Process(uint64_t now, bool enabled, bool onDefault, bool connectedElsewhere, bool canLoad);
    // 3.29: Mega Vil Futuristic (roleplay) sets the day + weather itself: while held no save is
    // loaded at all (no 100% save, no switching back to your own) - it waits until you leave
    void SetHold(bool hold);
    bool IsActive();             // 100% save is loaded (or loading)
    bool Pending();              // 3.22.1: a save load is about to start or running (the login pop-up waits for it)
    const char* StatusText();    // one line for the menu
}
