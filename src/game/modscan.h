#pragma once
// 2.37: detects mods that are not allowed in multiplayer (libCheatMenu.so / libCheatMenu64.so).
// Scan() runs from ON_ALL_MODS_LOAD (every AML mod is loaded by then, so aml->HasMod() is reliable)
// and again right before every join, so a menu that is dlopen'ed late is still caught.
// If anything is found, MP refuses to join any server (default or custom) and refuses to host.
namespace ModScan
{
    void        Scan();            // full scan: aml->HasMod GUIDs + loaded libs + mods folders
    bool        CheatMenu();       // result of the last scan (cheap)
    bool        Recheck();         // re-scans (rate-limited to once per 3 s), returns CheatMenu()
    const char* What();            // e.g. "libCheatMenu64.so" - what was found, for the message
    void        SetAllowed(bool on); // 3.00: on a server that allows Cheat Menu: its button / window are not hidden
}
