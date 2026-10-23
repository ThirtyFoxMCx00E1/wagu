// 2.37: tests renamed-CheatMenu detection + label matching (src/game/modscan_core.h).
//   g++ -std=c++17 -I../src modscan_test.cpp -o modscan_test && ./modscan_test
#include <stdio.h>
#include <string>
#include <algorithm>
#include <string.h>
#include <strings.h>
#include "game/modscan_core.h"
using namespace ModScanCore;
static int fails = 0;
#define CHECK(c, m) do { if(c) printf("PASS  %s\n", m); else { printf("FAIL  %s\n", m); ++fails; } } while(0)
static bool Hit(const std::string& file, size_t chunk)
{
    Matcher m;
    for(size_t i = 0; i < file.size(); i += chunk) m.Feed((const unsigned char*)file.data() + i, std::min(chunk, file.size() - i));
    return m.IsCheatMenu();
}
static std::string S(const char* s) { return std::string(s, strlen(s) + 1); }
int main()
{
    std::string pad(5000, 'x');
    std::string cheat = pad + S("") + S("Dear ImGui 1.89") + pad + S("") + S("CheatMenu") + S("God mode") + pad;
    CHECK(Hit(cheat, 1 << 20), "renamed lib with ImGui + \"CheatMenu\" label: detected");
    bool all = true; for(size_t c : { 1, 3, 7, 64, 333, 4096 }) all &= Hit(cheat, c);
    CHECK(all, "detected however the file is split into read chunks");
    CHECK(Hit(pad + S("") + S("imgui.ini") + S("Cheat Menu##main") + pad, 4096), "\"Cheat Menu##id\" label: detected");
    CHECK(Hit(pad + S("") + S("ImGuiWindow") + S("CHEATMENU") + pad, 4096), "any case: detected");
    CHECK(!Hit(pad + S("") + S("CheatMenu") + pad, 4096), "no ImGui in the lib: not this menu");
    CHECK(!Hit(pad + S("") + S("ImGui") + S("libCheatMenu.so") + S("SuperCheatMenuX") + pad, 4096), "word only inside a longer string: not a hit");
    CHECK(!Hit(pad + S("") + S("ImGui") + S("CheatMenu") + S("net.thirtyfox.mp") + pad, 4096), "MP's own lib (has the word in its list): never a hit");
    CHECK(!Hit(pad + S("") + S("ImGui") + S("Cheats") + S("Settings") + pad, 4096), "normal ImGui mod (CLEO_ImGui / ImHudExtended): not a hit");
    CHECK(IsCheatLabel("CheatMenu") && IsCheatLabel("Cheat Menu") && IsCheatLabel("cheat_menu##btn") && IsCheatLabel("CheatMenu##open"), "button labels hidden");
    CHECK(!IsCheatLabel("Close") && !IsCheatLabel("Teleport") && !IsCheatLabel(nullptr), "other buttons untouched");
    CHECK(IsCheatWindow("Cheat Menu") && IsCheatWindow("##CheatMenuMain") && !IsCheatWindow("Debug##Default"), "menu window matched, ImGui default window not");
    CHECK(IsModPath("/storage/emulated/0/Android_unprotected/data/com.rockstargames.gtasa/mods//libFPSCounter.so"), "mods folder path");
    CHECK(!IsModPath("/data/app/com.rockstargames.gtasa/lib/arm/libGTASA.so") && !IsModPath("/system/lib/libc.so"), "game / system libs are not read");
    printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
    return fails ? 1 : 0;
}
