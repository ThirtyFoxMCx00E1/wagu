// 4.12.2: ScreenGuard - no screen recording / screenshots of MP (Android FLAG_SECURE) unless the phone
// has a valid src_Access.txt in the MP folder (2000 keys, given out by the owner on Discord).
#pragma once
namespace ScreenGuard
{
    void Init(const char* mpDir);   // checks src_Access.txt, starts the guard thread when there is no valid key
    void OnUiThread();              // 4.12.5: called from the game's touch hook - secures the window when that is the main thread
    bool Locked();                  // true = recording is blocked on this phone
    int  KeyNumber();               // 1..2000 of the valid key, 0 = none
}
