// MP 4.13.4: the game's own main menu gets a [Multiplayer] tile next to Resume / Start Game /
// Settings. The tile is added through SAUtils (RusJJ, MIT, include/sautils/isautils.h) when it is
// installed (1.4.1+ has AddSettingsTabButton); without it MP draws its own tile (mp_menu.inc).
// Also: is the game's menu on screen, and the sound faders for the black server-select screen.
// This file never includes scripting.h (isautils.h has its own DEFOPCODE / CALLSCM macros).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/interface.h>
#include <string.h>
#include "game.h"
#include <sautils/isautils.h>
#include <stdio.h>
#include <sys/stat.h>
#include "mp_menu_png.h"

static uint8_t* s_pMobileMenu = nullptr;
static bool (*MobileMenu_IsMainMenuScreen)(void*) = nullptr;
static void* s_pAudioEngine = nullptr;
static void (*AE_SetEffectsFader)(void*, float) = nullptr;
static void (*AE_SetMusicFader)(void*, float) = nullptr;

void Game::InitMenu(void* h)
{
    if(!h) return;
    SET_TO(s_pMobileMenu, aml->GetSym(h, "gMobileMenu"));
    SET_TO(MobileMenu_IsMainMenuScreen, aml->GetSym(h, "_ZN10MobileMenu16IsMainMenuScreenEv"));
    SET_TO(s_pAudioEngine, aml->GetSym(h, "AudioEngine"));
    SET_TO(AE_SetEffectsFader, aml->GetSym(h, "_ZN12CAudioEngine28SetEffectsFaderScalingFactorEf"));
    SET_TO(AE_SetMusicFader, aml->GetSym(h, "_ZN12CAudioEngine26SetMusicFaderScalingFactorEf"));
    logger->Info("Main menu: menu %s, main-screen check %s, sound fader %s", s_pMobileMenu ? "yes" : "no",
        MobileMenu_IsMainMenuScreen ? "yes" : "no", (s_pAudioEngine && AE_SetEffectsFader) ? "yes" : "no");
}

// screenStack (OSArray<MenuScreen*>: u32 alloc, u32 count, ptr) right after the 4 CVector2D (32 bytes)
static uint32_t MenuScreens()
{
    if(!s_pMobileMenu) return 0;
    uint32_t n = *(uint32_t*)(s_pMobileMenu + 32 + 4);
    return n > 64 ? 0 : n;
}
bool Game::IsFrontendMenu() { return MenuScreens() > 0; }
bool Game::IsMainMenuShown()
{
    if(!s_pMobileMenu || !MobileMenu_IsMainMenuScreen || MenuScreens() == 0) return false;
    return MobileMenu_IsMainMenuScreen(s_pMobileMenu);
}

void Game::SetGameSoundFader(float f)
{
    if(!s_pAudioEngine) return;
    if(f < 0) f = 0; else if(f > 1) f = 1;
    if(AE_SetEffectsFader) AE_SetEffectsFader(s_pAudioEngine, f);
    if(AE_SetMusicFader) AE_SetMusicFader(s_pAudioEngine, f);
}

bool Game::AddMainMenuTile(const char* name, const char* pngRel, const char* pngFull, void (*onTap)(void*))
{
    if(!aml || !onTap) return false;
    if(pngFull && pngFull[0])
    {   // the tile picture: always the one MP ships (a few KB)
        struct stat st;
        if(stat(pngFull, &st) != 0 || st.st_size != (off_t)sizeof(kMpMenuPng))
            if(FILE* f = fopen(pngFull, "wb")) { fwrite(kMpMenuPng, 1, sizeof(kMpMenuPng), f); fclose(f); }
    }
    if(!aml->HasMod("net.rusjj.gtasa.utils")) return false;
    if(!aml->HasModOfVersion("net.rusjj.gtasa.utils", "1.4.1")) { logger->Info("Main menu: SAUtils is older than 1.4.1 - MP draws its own Multiplayer tile"); return false; }
    ISAUtils* su = (ISAUtils*)GetInterface("SAUtils");
    if(!su) return false;
    su->AddSettingsTabButton(name, onTap, STB_MainMenu, pngRel && pngRel[0] && strlen(pngRel) < 32 ? pngRel : "menu_mainsettings", nullptr);
    logger->Info("Main menu: [%s] tile added (SAUtils)", name);
    return true;
}
