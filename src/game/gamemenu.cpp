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
// 4.13.5: idle camera guard
static void**    s_ppCurrentPad = nullptr;            // currentPad (CPad*)
static void*   (*CPad_GetPad)(int) = nullptr;
static uint32_t* s_pTimeMs = nullptr;                 // CTimer::m_snTimeInMilliseconds
static const int kPadLastTouchedOfs = 0x134;          // CPad::LastTimeTouched (2.00 32-bit and 64-bit,
                                                      // read by CIdleCam::ProcessIdleCamTicker)

void Game::InitMenu(void* h)
{
    if(!h) return;
    SET_TO(s_pMobileMenu, aml->GetSym(h, "gMobileMenu"));
    SET_TO(MobileMenu_IsMainMenuScreen, aml->GetSym(h, "_ZN10MobileMenu16IsMainMenuScreenEv"));
    SET_TO(s_pAudioEngine, aml->GetSym(h, "AudioEngine"));
    SET_TO(AE_SetEffectsFader, aml->GetSym(h, "_ZN12CAudioEngine28SetEffectsFaderScalingFactorEf"));
    SET_TO(AE_SetMusicFader, aml->GetSym(h, "_ZN12CAudioEngine26SetMusicFaderScalingFactorEf"));
    SET_TO(s_ppCurrentPad, aml->GetSym(h, "currentPad"));
    SET_TO(CPad_GetPad, aml->GetSym(h, "_ZN4CPad6GetPadEi"));
    SET_TO(s_pTimeMs, aml->GetSym(h, "_ZN6CTimer22m_snTimeInMillisecondsE"));
    logger->Info("Idle camera guard: pad %s, timer %s", (s_ppCurrentPad || CPad_GetPad) ? "yes" : "no", s_pTimeMs ? "yes" : "no");
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

// CIdleCam::ProcessIdleCamTicker counts the idle time while CPad::LastTimeTouched stays the same and resets
// it when the value changes. MP's launcher / black server select / chat keyboard take every touch, so the
// game thinks nobody touched the phone, the idle (AFK) camera kicks in (JPatch brings it back) and picks a
// target that is gone / null -> SIGSEGV in libJPatch.so called from CCamera::Process. A new value every
// frame keeps the ticker at 0.
void Game::KeepPadAwake()
{
    if(!s_pTimeMs) return;
    const uint32_t t = *s_pTimeMs;
    uint8_t* pads[2] = { (uint8_t*)(s_ppCurrentPad ? *s_ppCurrentPad : nullptr), (uint8_t*)(CPad_GetPad ? CPad_GetPad(0) : nullptr) };
    for(int i = 0; i < 2; ++i)
    {   // 4.13.6: both the current pad and pad 0 (JPatch reads CPad::GetPad(0))
        if(!pads[i] || (i == 1 && pads[1] == pads[0])) continue;
        uint32_t* last = (uint32_t*)(pads[i] + kPadLastTouchedOfs);
        *last = (*last == t) ? t + 1 : t;
    }
}

// 4.13.7: the JPatch crash (PC libJPatch.so + 0x1A5A4, LR in CCamera::Process). JPatch 1.10.2.1 hooks
// CCam::Process for its PC-style idle camera ("IdleCameraStartSpeedPC") and, to see if you touched the
// screen, calls a virtual (+0x50, a CVector2D out) on CTouchInterface::m_pWidgets[175] with no null check.
// That widget does not exist yet right after Resume / Start / Load game, or while the game runs behind
// MP's Multiplayer screens -> null read. JPatch is not changed: for the duration of CCamera::Process an
// empty slot 175 holds a do-nothing widget (every virtual returns 0 = "not touched", its delete does not
// free), and is emptied again right after. 4.13.5 / 4.13.6's pad refresh / code restore are gone.
extern uintptr_t pGameLib;
extern "C" int MP_DummyWidgetFn() { return 0; }
static void** s_ppWidgets = nullptr;
static bool  s_bWidgetChecked = false, s_bWidgetPlaced = false;
static int   s_nWidgetGuards = 0;
alignas(8) static uintptr_t s_DummyWidgetVtbl[256];
alignas(8) static uintptr_t s_DummyWidget[256];
static const int kJPatchIdleWidget = 0x2BC / sizeof(uint32_t);   // 175 (32-bit)

void Game::IdleCamGuard(bool enter)
{
#ifdef AML32
    if(!s_bWidgetChecked)
    {
        s_bWidgetChecked = true;
        s_ppWidgets = (void**)aml->GetSym(aml->GetLibHandle("libGTASA.so"), "_ZN15CTouchInterface10m_pWidgetsE");
        for(auto& v : s_DummyWidgetVtbl) v = (uintptr_t)&MP_DummyWidgetFn;
        s_DummyWidget[0] = (uintptr_t)s_DummyWidgetVtbl;
        logger->Info("Idle camera guard: CTouchInterface::m_pWidgets %s (JPatch idle camera widget #%d)",
            s_ppWidgets ? "found" : "NOT found - off", kJPatchIdleWidget);
    }
    if(!s_ppWidgets) return;
    void*& slot = s_ppWidgets[kJPatchIdleWidget];
    if(enter)
    {
        if(slot) return;
        slot = (void*)s_DummyWidget;
        s_bWidgetPlaced = true;
        if(s_nWidgetGuards++ < 3) logger->Info("Idle camera guard: touch widget #%d is empty - placeholder for CCamera::Process", kJPatchIdleWidget);
    }
    else if(s_bWidgetPlaced)
    {
        if(slot == (void*)s_DummyWidget) slot = nullptr;   // the game made its own meanwhile: leave that one
        s_bWidgetPlaced = false;
    }
#else
    (void)enter;
#endif
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
