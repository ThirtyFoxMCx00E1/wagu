#pragma once
// MP 4.13.22: GTA HUD settings (Settings > Display > GTA HUD) - move / size every part of the game's own HUD.
//
// Built from what libAdvancedSettings.SA.so (ARM's Advanced Settings, research/) does - Ghidra decompile in
// research/libAdvancedSettings.SA_decompiled.c: the same HUD_* parts (PlayerInfo, BarHealth, BarArmor,
// BarBreath, WeaponIcon, Ammo, Money, MoneyChange, Clock, WantedStar, Radar, VehName, AreaName, Radio,
// Subtitles) and the same game functions (CWidgetPlayerInfo::Draw / Render*Bar / DrawWeaponIcon / DrawWanted,
// the radar, CFont::PrintString, the CSprite2d vertex setters). MP's own way: while a part draws, every sprite /
// rect / text it makes is moved + scaled around the part's first point (x, y, zoom, size, width, height, alpha).
// Nothing is hooked until the player turns "Custom GTA HUD" on; all 100 % / 0 = the game's own HUD.
namespace HudLayout
{
    enum Part { PLAYERINFO = 0, BAR_HEALTH, BAR_ARMOR, BAR_BREATH, WEAPON_ICON, AMMO, MONEY, MONEY_CHANGE, CLOCK,
                WANTED, RADAR, VEH_NAME, AREA_NAME, RADIO, SUBTITLES, PART_COUNT };
    struct Cfg { bool show; float x, y, z, size, w, h, alpha; };   // x / y: % of the screen, z (zoom) / size / w / h / alpha: %
    const char* PartName(int p);       // "Health bar"
    const char* PartKey(int p);        // "HUD_BarHealth" (the Advanced Settings section names)
    Cfg&  Get(int p);
    void  Reset(int p);
    bool  Enabled();
    void  SetEnabled(bool on);         // installs the hooks the first time
    int   HookCount();                 // game functions found + hooked (0 = off / not available)
    void  Init(void* gtasa);           // libGTASA handle (no hooks yet)
    void  SetScreen(float w, float h); // every frame (ImGui display size)
    void  Load();                      // from TYFMC.MP.ini [GTAHUD]
    void  Save();
    bool  Conflict();                  // 4.13.34: Advanced Settings is installed (it owns the HUD)
}
