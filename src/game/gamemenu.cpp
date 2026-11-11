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

// 4.13.6: JPatch 1.10.x patches a call to its idle (AFK) camera into CCamera::Process, right before
// CCam::Process (the crash: PC libJPatch.so + 0x1A5A4, LR libGTASA.so + 0x3DCA31, R2 = 14 =
// CIdleCam::m_TargetLOSFramestoReject). Its idle timer does not only look at CPad::LastTimeTouched, so
// 4.13.5's pad refresh was not enough. While MP is active the original 2.00 bytes go back over the
// patched part (the same thread that runs CCamera::Process, after it: never while it runs); JPatch's
// bytes are put back for single player.
extern uintptr_t pGameLib;
#ifdef AML32
static const uint32_t kCamWinOfs = 0x3DCA1E;
static const uint8_t kCamWinOrig[] = {
    0x94, 0xF8, 0x57, 0x00, 0x00, 0x22, 0x9F, 0xED, 0x9D, 0x8A, 0x00, 0x25, 0x00, 0xEB, 0x40, 0x11,
    0xB0, 0xEE, 0x48, 0x9A, 0x04, 0xEB, 0x01, 0x11, 0x91, 0xED, 0xB7, 0x1A, 0x91, 0xED, 0xB6, 0x0A,
    0x00, 0x21, 0xB5, 0xEE, 0x40, 0x1A, 0xF1, 0xEE, 0x10, 0xFA, 0xB5, 0xEE, 0x40, 0x0A, 0x08, 0xBF,
    0x01, 0x21, 0xF1, 0xEE, 0x10, 0xFA, 0x08, 0xBF, 0x01, 0x22, 0x0A, 0x42, 0x09, 0xD1, 0x10, 0xEE,
    0x10, 0x0A, 0x11, 0xEE, 0x10, 0x1A, 0xC1, 0xF5, 0x0A, 0xEB, 0x09, 0xEE, 0x10, 0x0A, 0x94, 0xF8,
    0x57, 0x00, 0x00, 0xEB, 0x40, 0x10, 0x04, 0xF5, 0xB8, 0x76, 0x06, 0xEB, 0x00, 0x10, 0xB3, 0xF5,
    0x90, 0xE9 };
// instruction starts (offsets from kCamWinOfs) of the original code in the window
static const uint8_t kCamWinIns[] = { 0x00, 0x04, 0x06, 0x0A, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x22, 0x26, 0x2A,
    0x2E, 0x30, 0x32, 0x36, 0x38, 0x3A, 0x3C, 0x3E, 0x42, 0x46, 0x4A, 0x4E, 0x52, 0x56, 0x5A, 0x5E, 0x62 };
static uint8_t s_CamSaved[sizeof(kCamWinOrig)];
static int  s_nCamLo = -1, s_nCamHi = -1;   // the restored span (offsets), -1 = nothing patched / not checked
static bool s_bCamChecked = false, s_bCamBlocked = false;
#endif
void Game::BlockIdleCam(bool block)
{
#ifdef AML32
    if(!pGameLib) return;
    uint8_t* w = (uint8_t*)(pGameLib + kCamWinOfs);
    if(!s_bCamChecked)
    {
        s_bCamChecked = true;
        if(*(uint32_t*)(pGameLib + 0x202020) != 0xE8BDB001 || memcmp(w, kCamWinOrig, 4) != 0)
        { logger->Info("Idle camera block: not GTA SA 2.00 / CCamera::Process changed - off"); return; }
        int lo = -1, hi = -1;
        for(int i = 0; i < (int)sizeof(kCamWinOrig); ++i) if(w[i] != kCamWinOrig[i]) { if(lo < 0) lo = i; hi = i + 1; }
        if(lo < 0) { logger->Info("Idle camera block: CCamera::Process is not patched (no JPatch idle camera)"); return; }
        int a = 0, b = sizeof(kCamWinOrig);
        for(uint8_t s : kCamWinIns) { if(s <= lo) a = s; }
        for(int k = (int)sizeof(kCamWinIns) - 1; k >= 0; --k) if(kCamWinIns[k] >= hi) b = kCamWinIns[k];
        s_nCamLo = a; s_nCamHi = b;
        memcpy(s_CamSaved, w, sizeof(kCamWinOrig));
        logger->Info("Idle camera block: CCamera::Process patched at +0x%X..+0x%X (JPatch idle camera), restored while MP is active",
            (unsigned)(kCamWinOfs + a), (unsigned)(kCamWinOfs + b));
    }
    if(s_nCamLo < 0 || block == s_bCamBlocked) return;
    const int n = s_nCamHi - s_nCamLo;
    if(block)
    {
        memcpy(s_CamSaved + s_nCamLo, w + s_nCamLo, n);   // JPatch's bytes as they are now
        aml->Write((uintptr_t)(w + s_nCamLo), (uintptr_t)(kCamWinOrig + s_nCamLo), n);
    }
    else
    {
        if(memcmp(w + s_nCamLo, kCamWinOrig + s_nCamLo, n) == 0)   // still ours: give JPatch its code back
            aml->Write((uintptr_t)(w + s_nCamLo), (uintptr_t)(s_CamSaved + s_nCamLo), n);
    }
    s_bCamBlocked = block;
    logger->Info("Idle camera block: %s", block ? "on (multiplayer)" : "off (single player)");
#else
    (void)block;
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
