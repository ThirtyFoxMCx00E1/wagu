// MP - multiplayer for GTA San Andreas Mobile (AML mod)
// Players meet and play together in the same game world: real-time movement, animations,
// combat, chat, name tags, kill feed and radar icons. Builds as libMP32.so / libMP64.so.
#include "../imgui/imgui_internal.h"   // 3.53: gamepad navigation (NavWindow / NavRectRel)
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <sys/stat.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <mutex>
#include <atomic>
#include <vector>
#include <exception>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "mp.h"
#include "game/game.h"
#include "game/scriptguard.h"
#include "game/modscan.h"
#include "game/screenguard.h"   // 4.12.2
#include "tyfmc.h"               // 4.12.7: files/TYFMC/MP + files/TYFMC/TYFMC.MP.ini
#include "ui/hud.h"
#include "ui/floatmenu.h"
#include "ui/vkeyboard.h"
#include "../imgui/rw_glue.h"
#include "net/server.h"

// 4.12.7: the config is files/TYFMC/TYFMC.MP.ini (the old configs/net.thirtyfox.mp.ini is moved there)
TYFMC_MODCFG(net.thirtyfox.mp, MP Multiplayer, 4.13.13, ThirtyFoxMCx00E1, MP, net.thirtyfox.mp)
NEEDGAME(com.rockstargames.gtasa)

// Globals used by the embedded RenderWare ImGui backend
uintptr_t pGameLib = 0;
void*     pGameHandle = nullptr;
RwReal*   nearScreenZ = nullptr;
RwReal*   recipNearClip = nullptr;
void    (*SetScissorRect)(float*) = nullptr;
void InitRenderWareFunctions();

static bool s_bRWReady = false;
static std::atomic<int> s_nClearMouse{0};
static std::atomic<bool> s_bPadCursor{false};   // 3.39.2: controller cursor on (the game gets no pad / touch)
static std::atomic<bool> s_bPadNav{false};      // 3.53: controller navigates MP's screens (focus highlight, A = press)
static void PadTick();
static int  s_nMouseFinger = -1;

// ---- 2.22: touch -> ImGui input goes through a queue ------------------------------------
// AND_TouchEvent runs on Android's Java/UI thread, the HUD renders on the game thread. Up to
// 2.21 the touch hook pushed ImGui input events and walked ImGui's window list straight from
// the touch thread while the game thread was inside NewFrame()/Render(). Both sides resize the
// same ImVector at the same time -> heap corruption -> random SIGSEGVs, often reported inside
// libart.so (the Java thread is the one that trips over the damage). It also dropped or
// reordered mouse-position events, so a tap could land at the OLD position (the chat box) -
// "anywhere I press opens the chat". Now: the touch thread only queues, the game thread applies.
enum { TQ_POS, TQ_DOWN, TQ_UP, TQ_CLEAR };
struct TouchQ { int kind; float x, y; };
static std::mutex          s_TouchQMx;
static std::vector<TouchQ> s_TouchQ;
static std::recursive_mutex s_DispatchMx;   // DispatchTouch: hook thread + polled fallback
static inline void QueueIO(int kind, float x = 0, float y = 0)
{
    std::lock_guard<std::mutex> lk(s_TouchQMx);
    if(s_TouchQ.size() > 512) s_TouchQ.erase(s_TouchQ.begin(), s_TouchQ.begin() + 256);
    s_TouchQ.push_back({ kind, x, y });
}
static void DrainIO()   // game thread, before ImGui::NewFrame()
{
    static std::vector<TouchQ> q;
    { std::lock_guard<std::mutex> lk(s_TouchQMx); q.swap(s_TouchQ); }
    if(q.empty()) return;
    ImGuiIO& io = ImGui::GetIO();
    for(const TouchQ& e : q)
    {
        switch(e.kind)
        {
            case TQ_POS:   io.AddMousePosEvent(e.x, e.y); break;
            case TQ_DOWN:  io.AddMouseButtonEvent(0, true); break;
            case TQ_UP:    io.AddMouseButtonEvent(0, false); break;
            case TQ_CLEAR: io.AddMousePosEvent(-FLT_MAX, -FLT_MAX); break;
        }
    }
    q.clear();
}
static bool s_bIgnoredFinger[16] = { false };

#define TOUCH_RELEASE 1
#define TOUCH_PUSH    2
#define TOUCH_MOVE    3
#define TOUCH_POINTER_UP 4
static void PollTouches();

// ---- Game logic tick ------------------------------------------------------------------
static void JPatchGuardSetup();
DECL_HOOKv(CGame_Process)
{
    JPatchGuardSetup();   // 4.13.8 (normally done in OnAllModsLoaded already)
    CGame_Process();
    ScriptGuard::Tick();   // 3.21.3
    MP::Process();
}

// 4.13.7: JPatch's idle camera (hooked CCam::Process) reads touch widget #175 with no null check
DECL_HOOKv(CCamera_Process, void* self)
{
    Game::IdleCamGuard(true);
    CCamera_Process(self);
    Game::IdleCamGuard(false);
}

// 4.13.8: the 4.13.7 guard used the game's m_pWidgets symbol, the crash went on. Now MP looks at JPatch
// 1.10.2.1 itself (research/libJPatch.so): its CCam::Process hook (+0x1A558) reads the widget list pointer
// it keeps at +0x5EAA8 and calls a virtual on entry 175 (+0x2BC) without a null check (crash +0x1A5A4).
// JPatch's own "original CCam::Process" is at +0xFB330. MP hooks CCam::Process on top of JPatch (after
// all mods loaded); when JPatch's widget #175 (or its list) is empty - right after Resume / Start / Load
// game, behind the Multiplayer screens - that one frame goes straight to JPatch's original (the game's
// camera code, JPatch's idle camera skipped), every other frame runs through JPatch as before. Only used
// when JPatch's code matches 1.10.2.1 byte for byte; JPatch's file is not changed.
#ifdef AML32
static const uint8_t kJPatchIdleSig[] = {   // libJPatch.so 1.10.2.1 +0x1A558 .. +0x1A5B2
    0xF0, 0xB5, 0x03, 0xAF, 0x2D, 0xE9, 0x00, 0x0F, 0x89, 0xB0, 0x80, 0x46, 0xCC, 0x48, 0xCD, 0x49, 0x78, 0x44, 0x79, 0x44,
    0x05, 0x68, 0x08, 0x68, 0xCB, 0x4C, 0x29, 0x68, 0x08, 0x91, 0x7C, 0x44, 0xD0, 0xF8, 0x00, 0xB0, 0x47, 0xF2, 0x30, 0x50,
    0x21, 0x68, 0x58, 0x44, 0x81, 0x42, 0x84, 0xBF, 0x00, 0x21, 0x21, 0x60, 0xC5, 0x4E, 0x7E, 0x44, 0x31, 0x68, 0x81, 0x42,
    0x84, 0xBF, 0x00, 0x20, 0x30, 0x60, 0xC3, 0x48, 0x78, 0x44, 0x00, 0x68, 0xD0, 0xF8, 0xBC, 0x02, 0x01, 0x68, 0x0A, 0x6D,
    0x00, 0x21, 0xCD, 0xE9, 0x05, 0x11, 0x05, 0xA9, 0x90, 0x47 };
static uintptr_t s_pJPatchWidgets = 0;   // &JPatch's widget list pointer
static uintptr_t s_pJPatchOrig = 0;      // &JPatch's original CCam::Process
static int s_nJPatchSkips = 0;
static uint32_t ThumbPcRelTarget(uintptr_t lib, uint32_t ldrAt, uint32_t addPcAt)
{   // "ldr rX, [pc, #imm8*4]" (16-bit) ... "add rX, pc"
    const uint16_t ld = *(const uint16_t*)(lib + ldrAt);
    if((ld & 0xF800) != 0x4800) return 0;
    const uint32_t lit = *(const uint32_t*)(lib + ((ldrAt + 4) & ~3u) + (ld & 0xFF) * 4);
    return lit + addPcAt + 4;
}
DECL_HOOKv(CCam_Process, void* self)
{
    if(s_pJPatchWidgets && s_pJPatchOrig)
    {
        void** w = *(void***)s_pJPatchWidgets;
        auto orig = *(void(**)(void*))s_pJPatchOrig;
        if((!w || !w[0x2BC / 4]) && orig)
        {
            if(s_nJPatchSkips++ < 3) logger->Info("JPatch guard: touch widget #175 is empty - JPatch's idle camera skipped this frame (no crash)");
            orig(self);
            return;
        }
    }
    CCam_Process(self);
}
#endif

// ---- 2D render: our whole HUD + floating menu ------------------------------------------
DECL_HOOKv(Render2dStuff)
{
    Render2dStuff();
    if(!s_bRWReady) { InitRenderWareFunctions(); s_bRWReady = true; }
    if(HUD::IsInitialised()) { PollTouches(); PadTick(); DrainIO(); }   // 3.39.2: + controller
    HUD::Render();
    int cm = s_nClearMouse.load();
    if(cm > 0 && s_nClearMouse.compare_exchange_strong(cm, cm - 1) && cm == 1) QueueIO(TQ_CLEAR);
}

// ---- 4.13.4: the loading screen of a main menu Multiplayer load = MP's launcher-styled panel ----------
static float* s_pPercentLoaded = nullptr;
static bool   s_bMpLoaded = false;   // OnModLoad got through (MP is on)
static bool   s_bMpSplashDrawn = false;
DECL_HOOKv(LS_RenderSplash)
{
    s_bMpSplashDrawn = false;
    if(MP::MenuMpLoading() && !MP::PCMenuLoadingActive())   // 4.13.12: PCMenu's PC loading screen wins when it is on
    {   // 4.13.5: tapping Multiplayer on the title menu = no in-game frame yet (Render2dStuff never ran),
        // so RenderWare + MP's UI are set up right here; before, the game's own splash showed instead
        if(!s_bRWReady) { InitRenderWareFunctions(); s_bRWReady = true; }
        float pct = s_pPercentLoaded ? *s_pPercentLoaded : 0.0f;
        if(!(pct >= 0.0f && pct <= 100.0f)) pct = 0.0f;
        if(HUD::RenderLoading(pct)) { s_bMpSplashDrawn = true; return; }
    }
    LS_RenderSplash();
}
// 4.13.9: the game's menu update (runs while the menu is up, CGame::Process does not)
DECL_HOOKv(MobileMenu_Update, void* self)
{
    MobileMenu_Update(self);
    MP::MenuMpMenuTick();
}
DECL_HOOKv(LS_RenderLoadingBar)
{
    if(s_bMpSplashDrawn && MP::MenuMpLoading()) return;   // our panel has its own bar
    LS_RenderLoadingBar();
}

// ---- Touch routing ----------------------------------------------------------------------
// The game reports: 1 = release, 2 = push, 3 = move, 4 = pointer-up (a finger lifted while
// another one is still down, e.g. while holding the movement joystick). v1.0 ignored type 4,
// so ImGui never saw the finger lift and taps on the MP / Chat bubbles never completed.
static uint64_t s_nLastAnyTouchMs = 0;
static uint64_t s_nLastHookTouchMs = 0;
static int      s_nTouchLogs = 0;
static bool     s_bScaleTouch = false;
static float    s_fTouchSX = 1.0f, s_fTouchSY = 1.0f;

static void MapTouch(int& x, int& y)
{
    int rw = Game::ScreenWidth(), rh = Game::ScreenHeight();
    // If the game renders below native resolution, touches arrive in device pixels.
    if(!s_bScaleTouch && (x > rw + 2 || y > rh + 2))
    {
        int dw = 0, dh = 0;
        aml->GetDisplaySize(&dw, &dh);
        if(dw < dh) { int t = dw; dw = dh; dh = t; }  // landscape
        if(dw > rw && dh > rh)
        {
            s_bScaleTouch = true;
            s_fTouchSX = (float)rw / dw; s_fTouchSY = (float)rh / dh;
            logger->Info("Touch scaling enabled: device %dx%d -> game %dx%d", dw, dh, rw, rh);
        }
    }
    if(s_bScaleTouch) { x = (int)(x * s_fTouchSX); y = (int)(y * s_fTouchSY); }
}

// 4.05.8: MP screens that cover the game: every new finger is MP's while one is open
static bool s_bGameFinger[16] = { false };   // the game saw this finger's press (it must get the release)
static inline bool MPScreenOwnsTouches()
{
    return FloatMenu::IsModal() || VKeyboard::IsOpen() || MP::AuthScreenShown() || MP::DealerShown() || MP::MenuMpBlack();
}
// Routes one touch to MP's UI. Returns true if MP used it (the game must not see it).
static bool DispatchTouch(int type, int finger, int x, int y)
{
    std::lock_guard<std::recursive_mutex> lk(s_DispatchMx);
    const bool isRelease = (type == TOUCH_RELEASE || type == TOUCH_POINTER_UP);
    uint64_t now = MP_NowMs();

    // a finger that "never lifted" (lost event) must not block ImGui forever.
    // 4.05.5: it blocked every tap that came less than 1.5 s after the last touch, so when a release got
    // lost the player kept tapping the region icon (or any launcher button) and nothing ever happened.
    // Now a new press also frees it when it is the same finger again (its release was lost for sure) or
    // while the launcher / keyboard is open (one finger is all they need - the newest one wins).
    if(type == TOUCH_PUSH && s_nMouseFinger >= 0 &&
       (now - s_nLastAnyTouchMs > 1500 || finger == s_nMouseFinger || FloatMenu::IsModal() || VKeyboard::IsOpen()))
    {
        static int s_nStuckLogs = 0;
        if(s_nStuckLogs < 6) { ++s_nStuckLogs; logger->Info("4.05.6: finger %d never lifted - freed by the press of finger %d", s_nMouseFinger, finger); }
        if(s_nMouseFinger != finger && s_nMouseFinger < 16) s_bIgnoredFinger[s_nMouseFinger] = false;   // else the game HUD stayed blocked
        QueueIO(TQ_UP);
        s_nMouseFinger = -1;
    }
    s_nLastAnyTouchMs = now;

    // 1) floating buttons (MP MENU / CHAT) get first pick, independent of ImGui
    if(FloatMenu::OnTouch(isRelease ? TOUCH_RELEASE : type, finger, (float)x, (float)y))
    {
        s_bIgnoredFinger[finger] = !isRelease;
        if(isRelease && finger == s_nMouseFinger) { QueueIO(TQ_UP); s_nMouseFinger = -1; s_nClearMouse = 3; }
        return true;
    }

    // 2) ImGui windows (menu, keyboard, chat box) - hit test on the last frame's snapshot
    if(type == TOUCH_PUSH && FloatMenu::IsModal()) MP::LauncherTouchDown((float)x, (float)y);   // 4.05.8: region icon fallback
    if(type == TOUCH_PUSH)
    {
        bool want = HUD::WantsTouch((float)x, (float)y);
        if(s_nMouseFinger < 0)
        {
            s_nClearMouse = 0;   // a new tap cancels the pending "mouse away"
            // 2.22: position + press always travel together, in order
            QueueIO(TQ_POS, (float)x, (float)y);
            QueueIO(TQ_DOWN);
            s_nMouseFinger = finger;
        }
        if(want) { s_bIgnoredFinger[finger] = true; return true; }
    }
    else if(isRelease)
    {
        if(finger == s_nMouseFinger)
        {
            QueueIO(TQ_POS, (float)x, (float)y);
            QueueIO(TQ_UP);
            s_nMouseFinger = -1;
            s_nClearMouse = 3;
        }
        if(s_bIgnoredFinger[finger]) { s_bIgnoredFinger[finger] = false; return true; }
    }
    else if(type == TOUCH_MOVE)
    {
        if(finger == s_nMouseFinger) QueueIO(TQ_POS, (float)x, (float)y);
        if(s_bIgnoredFinger[finger]) return true;
    }
    // 4.05.8: while an MP full screen is open (launcher, keyboard, login, dealer) the game gets NO new
    // finger at all. Before, a tap that MP's hit test missed (the launcher's toolbar sits right over GTA's
    // clock / money / weapon icon) still went to the game: the HUD - hidden or not - and CLEO touch zones
    // reacted behind the launcher, and the region icon looked dead. A finger that was already down for
    // the game before the screen opened (the joystick) still gets its moves and release, so it never sticks.
    if(MPScreenOwnsTouches())
    {
        if(type == TOUCH_PUSH) { s_bGameFinger[finger] = false; return true; }
        if(!s_bGameFinger[finger]) return true;
        if(isRelease) s_bGameFinger[finger] = false;
        return false;
    }
    if(type == TOUCH_PUSH) s_bGameFinger[finger] = true;
    else if(isRelease) s_bGameFinger[finger] = false;
    return false;
}

DECL_HOOKv(AND_TouchEvent, int type, int finger, int x, int y)
{
    ScreenGuard::OnUiThread();   // 4.12.5: FLAG_SECURE the normal way when this is Android's main thread
    if(!HUD::IsInitialised() || finger < 0 || finger >= 16) { AND_TouchEvent(type, finger, x, y); return; }
    s_nLastHookTouchMs = MP_NowMs();
    int ox = x, oy = y;
    MapTouch(x, y);
    if(s_nTouchLogs < 8) { ++s_nTouchLogs; logger->Info("hook touch type=%d finger=%d raw=%d,%d mapped=%d,%d", type, finger, ox, oy, x, y); }
    if(DispatchTouch(type, finger, x, y)) return;
    AND_TouchEvent(type, finger, ox, oy);
}

// ---- 2.19.5: keyboard open = the game's HUD gets no touches -----------------------------------
// The game's widgets (radar / map, weapon, enter car, camera swipes...) ask CTouchInterface
// whether they were touched; on some builds that reads the pointer state directly, so eating
// AND_TouchEvent wasn't enough and taps on the keyboard also hit the HUD behind it.
static uint64_t s_nKbOpenMs = 0;
// 2.28: a finger that is on an MP window (Player list, menu, chat...) is MP's. Some builds let the
// game's HUD - and CLEO touch-zone scripts that ask the same CTouchInterface - see it anyway, so a tap
// on a player's name could fire a CLEO "teleport / fly" touch shortcut behind the list. While such a
// finger is down (and briefly after it lifts) the game is told nothing was touched.
static inline bool UiOwnsAFinger()
{
    for(int i = 0; i < 16; ++i) if(s_bIgnoredFinger[i]) return true;
    return false;
}
static inline bool BlockGameTouch()
{
    if(VKeyboard::IsOpen() || FloatMenu::IsModal() || UiOwnsAFinger() || s_bPadCursor.load() || s_bPadNav.load()) { s_nKbOpenMs = MP_NowMs(); return true; }   // 2.26: + launcher, 2.28: + MP-owned finger
    return s_nKbOpenMs && MP_NowMs() - s_nKbOpenMs < 350;   // the release of the last key tap too
}
DECL_HOOK(bool, TI_IsTouched, int w, void* v, int i)        { return BlockGameTouch() ? false : TI_IsTouched(w, v, i); }
DECL_HOOK(bool, TI_IsReleased, int w, void* v, int i)       { return BlockGameTouch() ? false : TI_IsReleased(w, v, i); }
DECL_HOOK(bool, TI_IsDoubleTapped, int w, bool b, int i)    { return BlockGameTouch() ? false : TI_IsDoubleTapped(w, b, i); }
DECL_HOOK(bool, TI_IsHeldDown, int w, int i)                { return BlockGameTouch() ? false : TI_IsHeldDown(w, i); }
DECL_HOOK(bool, TI_IsSwipedLeft, int w, int i)              { return BlockGameTouch() ? false : TI_IsSwipedLeft(w, i); }
DECL_HOOK(bool, TI_IsSwipedRight, int w, int i)             { return BlockGameTouch() ? false : TI_IsSwipedRight(w, i); }
DECL_HOOK(bool, TI_IsSwipedUp, int w, int i)                { return BlockGameTouch() ? false : TI_IsSwipedUp(w, i); }
DECL_HOOK(bool, TI_IsSwipedDown, int w, int i)              { return BlockGameTouch() ? false : TI_IsSwipedDown(w, i); }
DECL_HOOK(bool, Widget_IsTouched, void* self, void* v)      { return BlockGameTouch() ? false : Widget_IsTouched(self, v); }
// 4.05.2: the launcher's toolbar (refresh, region, settings, add server) sits at the top right, right over
// the GTA weapon icon. On some builds the weapon widget still saw the finger (it reads the pointer state /
// CHID / CPad itself, not only CTouchInterface), so a tap on the region or gear icon switched the weapon
// and the launcher lost the tap. While the launcher / keyboard is open the game is now told: no finger is
// down, no weapon cycle, no HID button - MP keeps reading the real pointer through the original function.
static int  (*LIB_PointerGetButton)(int, int) = nullptr;
static uint64_t s_nScreenBlockMs = 0;
static inline bool ScreenBlocksGame()
{
    if(VKeyboard::IsOpen() || FloatMenu::IsModal()) { s_nScreenBlockMs = MP_NowMs(); return true; }
    return s_nScreenBlockMs && MP_NowMs() - s_nScreenBlockMs < 350;   // the release of the last tap too
}
DECL_HOOK(bool, Pad_CycleWeapL, void* self)                 { return ScreenBlocksGame() ? false : Pad_CycleWeapL(self); }
DECL_HOOK(bool, Pad_CycleWeapR, void* self)                 { return ScreenBlocksGame() ? false : Pad_CycleWeapR(self); }
DECL_HOOK(bool, Hid_JustPressed, int m)                     { return ScreenBlocksGame() ? false : Hid_JustPressed(m); }
DECL_HOOK(bool, Hid_Released, int m)                        { return ScreenBlocksGame() ? false : Hid_Released(m); }
DECL_HOOK(bool, Hid_DoubleTapped, int m)                    { return ScreenBlocksGame() ? false : Hid_DoubleTapped(m); }
DECL_HOOK(bool, Hid_Pressed, int m, float* v)               { if(ScreenBlocksGame()) { if(v) *v = 0.0f; return false; } return Hid_Pressed(m, v); }
DECL_HOOK(bool, Widget_IsReleased, void* self, void* v)     { return BlockGameTouch() ? false : Widget_IsReleased(self, v); }
DECL_HOOK(bool, Widget_IsDoubleTapped, void* self)          { return BlockGameTouch() ? false : Widget_IsDoubleTapped(self); }
DECL_HOOK(bool, Widget_IsHeldDown, void* self)              { return BlockGameTouch() ? false : Widget_IsHeldDown(self); }
DECL_HOOK(bool, Widget_IsSwipedLeft, void* self)            { return BlockGameTouch() ? false : Widget_IsSwipedLeft(self); }
DECL_HOOK(bool, Widget_IsSwipedRight, void* self)           { return BlockGameTouch() ? false : Widget_IsSwipedRight(self); }
DECL_HOOK(bool, Widget_IsSwipedUp, void* self)              { return BlockGameTouch() ? false : Widget_IsSwipedUp(self); }
DECL_HOOK(bool, Widget_IsSwipedDown, void* self)            { return BlockGameTouch() ? false : Widget_IsSwipedDown(self); }


// ---- 3.39.2: controller (gamepad) support for MP's screens ---------------------------------
// The game reads the pad through WarGamepad_GetGamepadButtons / _GetGamepadAxis (Java side). MP hooks
// both: it sees every button, and while the MP cursor is on, the game gets nothing (no walking or
// shooting behind a menu). Buttons (Xbox names): A 0x1, B 0x2, X 0x4, Y 0x8, Start 0x10, Back 0x20,
// LB 0x40, RB 0x80, D-pad up/down/left/right 0x100/0x200/0x400/0x800, L3 0x1000, R3 0x2000.
//   Back (Select)        : MP cursor on / off   (hold 0.6 s: the MP menu + cursor)
//   left stick / D-pad   : move the cursor       right stick: scroll
//   A                    : tap (hold A + move = drag)    B: cursor off
//   on the spawn picker  : D-pad left / right = the spot, A = spawn
// The cursor turns itself on when the launcher / login / keyboard opens after the pad was used.
// 3.53 full controller support: MP's screens open in NAVIGATION mode (every button, icon / symbol button,
// switch, slider, dropdown and keyboard key gets a highlight and is pressed with A):
//   D-pad / left stick : move the highlight     A: press / open / type     B: back (twice = close the screen)
//   Y                  : text input / keyboard  X: switch to the cursor (and back)
//   LB / RB            : previous / next page (launcher pages, tab bars)
//   LT / RT            : scroll a page up / down   right stick: scroll
//   Start              : close the MP screen
enum { PB_A = 0x1, PB_B = 0x2, PB_X = 0x4, PB_Y = 0x8, PB_START = 0x10, PB_BACK = 0x20, PB_LB = 0x40, PB_RB = 0x80,
       PB_UP = 0x100, PB_DOWN = 0x200, PB_LEFT = 0x400, PB_RIGHT = 0x800, PB_L3 = 0x1000, PB_R3 = 0x2000,
       PB_LT = 0x4000, PB_RT = 0x8000 };   // 3.53: LT / RT = the trigger axes (4 / 5) or these bits
static std::atomic<int> s_nPadMask{0};
static float s_fPadAxis[6] = { 0, 0, 0, 0, 0, 0 };
static std::atomic<uint64_t> s_nPadHookAt{0}, s_nPadUsedAt{0};
static bool  s_bPadAuto = false, s_bPadADown = false, s_bPadLogged = false;
static float s_fPadX = -1, s_fPadY = -1;
static int   s_nPadPrev = 0;
static uint64_t s_nPadBackAt = 0, s_nPadLastMs = 0;
static int   (*Pad_Buttons0)() = nullptr;
static float (*Pad_Axis1)(int) = nullptr;
static inline bool PadBlocksGame() { return s_bPadCursor.load() || s_bPadNav.load(); }
DECL_HOOK(int, WarPadButtons, void)
{
    int m = WarPadButtons();
    s_nPadMask = m; s_nPadHookAt = MP_NowMs();
    if(m) s_nPadUsedAt = MP_NowMs();
    if(PadBlocksGame()) return 0;
    return m & ~PB_BACK;   // Back is MP's cursor key
}
DECL_HOOK(float, WarPadAxis, int axis)
{
    float v = WarPadAxis(axis);
    if(axis >= 0 && axis < 6) s_fPadAxis[axis] = v;
    if(fabsf(v) > 0.3f) s_nPadUsedAt = MP_NowMs();
    return PadBlocksGame() ? 0.0f : v;
}
static void HookGamepad()
{
    void* b = (void*)aml->GetSym(pGameHandle, "_Z28WarGamepad_GetGamepadButtonsv");
    void* a = (void*)aml->GetSym(pGameHandle, "_Z25WarGamepad_GetGamepadAxisi");
    if(b) { HOOK(WarPadButtons, b); Pad_Buttons0 = WarPadButtons; }
    if(a) { HOOK(WarPadAxis, a); Pad_Axis1 = WarPadAxis; }
    logger->Info("Controller: buttons %s, axes %s", b ? "hooked" : "not found", a ? "hooked" : "not found");
}
static bool DispatchTouch(int type, int finger, int x, int y);
// ---- 3.53: ImGui gamepad navigation ----
static int s_nNavPrev = 0;
static uint64_t s_nNavScrollAt = 0;
static void PadNavKeys(int m, bool on)
{
    ImGuiIO& io = ImGui::GetIO();
    struct K { int bit; ImGuiKey key; };
    static const K ks[] = { { PB_A, ImGuiKey_GamepadFaceDown }, { PB_Y, ImGuiKey_GamepadFaceUp },
                            { PB_UP, ImGuiKey_GamepadDpadUp }, { PB_DOWN, ImGuiKey_GamepadDpadDown }, { PB_LEFT, ImGuiKey_GamepadDpadLeft }, { PB_RIGHT, ImGuiKey_GamepadDpadRight } };
    for(const K& k : ks)
    {
        const bool now = on && (m & k.bit), was = (s_nNavPrev & k.bit) != 0;
        if(now != was) io.AddKeyEvent(k.key, now);
    }
    // left stick = analog moves (dead zone)
    float lx = on ? s_fPadAxis[0] : 0, ly = on ? s_fPadAxis[1] : 0;
    auto an = [](float v) { v = fabsf(v); return v < 0.35f ? 0.0f : fminf(1.0f, (v - 0.35f) / 0.5f); };
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickLeft,  lx < 0 && an(lx) > 0, lx < 0 ? an(lx) : 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickRight, lx > 0 && an(lx) > 0, lx > 0 ? an(lx) : 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickUp,    ly < 0 && an(ly) > 0, ly < 0 ? an(ly) : 0);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadLStickDown,  ly > 0 && an(ly) > 0, ly > 0 ? an(ly) : 0);
    s_nNavPrev = on ? (m & (PB_A | PB_Y | PB_UP | PB_DOWN | PB_LEFT | PB_RIGHT)) : 0;
}
static void PadNavScroll(float pages)
{
    ImGuiContext* g = ImGui::GetCurrentContext(); if(!g) return;
    ImGuiWindow* w = g->NavWindow ? g->NavWindow : g->HoveredWindow;
    // the innermost scrollable window: the nav window or a parent of it
    while(w && w->ScrollMax.y <= 0.0f && (w->Flags & ImGuiWindowFlags_ChildWindow) && w->ParentWindow) w = w->ParentWindow;
    if(!w) return;
    ImGui::SetScrollY(w, ImClamp(w->Scroll.y + pages * w->InnerRect.GetHeight() * 0.85f, 0.0f, w->ScrollMax.y));
}
static void PadCloseScreen()
{
    if(VKeyboard::IsOpen()) { VKeyboard::Close(); return; }
    if(MP::DealerBack()) return;   // 3.55
    if(FloatMenu::IsOpen()) { FloatMenu::Toggle(); return; }
    FloatMenu::CloseAll();
}
static void PadNavTick(int m, int pressed, int released, float dt, bool screen)
{
    ImGuiIO& io = ImGui::GetIO();
    const bool on = s_bPadNav.load() && screen;
    if(!on)
    {
        if(s_nNavPrev) PadNavKeys(0, false);
        if(io.ConfigFlags & ImGuiConfigFlags_NavEnableGamepad) { io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad; io.BackendFlags &= ~ImGuiBackendFlags_HasGamepad; }
        if(s_bPadNav.load() && !screen) s_bPadNav = false;
        return;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    if(s_bPadADown) { DispatchTouch(TOUCH_RELEASE, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = false; }
    ImGuiContext* g = ImGui::GetCurrentContext();
    if(g)
    {   // the screen on top gets the controller (keyboard > login > launcher), so the highlight is never lost
        static const char* const kTop[] = { "##MPKeyboard", "##MPLogin", "##MPRegionPick", "##MPKicked", "##MPCheatGate", "##MPLauncher" };
        for(const char* nm : kTop)
        {
            ImGuiWindow* w = ImGui::FindWindowByName(nm);
            if(!w || !w->WasActive) continue;
            if(!g->NavWindow || g->NavWindow->RootWindow != w) ImGui::FocusWindow(w);
            break;
        }
    }
    // B: back - ImGui cancels first (popup / dropdown / active field / out of a list); nothing left = close
    if(pressed & PB_B)
    {
        const bool canCancel = g && (g->ActiveId != 0 || g->OpenPopupStack.Size > 0 || (g->NavWindow && (g->NavWindow->Flags & ImGuiWindowFlags_ChildWindow)));
        if(canCancel) { io.AddKeyEvent(ImGuiKey_GamepadFaceRight, true); }
        else PadCloseScreen();
    }
    if(released & PB_B) io.AddKeyEvent(ImGuiKey_GamepadFaceRight, false);
    if(pressed & PB_START) PadCloseScreen();
    // X: the cursor instead (for the map, drag & drop ...)
    if(pressed & PB_X) { s_bPadNav = false; s_bPadCursor = true; PadNavKeys(0, false); return; }
    // LB / RB: pages (launcher), else the tab bars
    if(pressed & (PB_LB | PB_RB))
    {
        const int dir = (pressed & PB_LB) ? -1 : 1;
        if(!(FloatMenu::IsModal() && MP::LauncherPadPage(dir))) { io.AddKeyEvent(dir < 0 ? ImGuiKey_GamepadL1 : ImGuiKey_GamepadR1, true); }
    }
    if(released & PB_LB) io.AddKeyEvent(ImGuiKey_GamepadL1, false);
    if(released & PB_RB) io.AddKeyEvent(ImGuiKey_GamepadR1, false);
    // LT / RT: a page up / down (again every 0.35 s while held)
    const uint64_t now = MP_NowMs();
    if(pressed & (PB_LT | PB_RT)) { PadNavScroll((pressed & PB_LT) ? -1.0f : 1.0f); s_nNavScrollAt = now + 450; }
    else if((m & (PB_LT | PB_RT)) && now > s_nNavScrollAt) { PadNavScroll((m & PB_LT) ? -1.0f : 1.0f); s_nNavScrollAt = now + 350; }
    // right stick: smooth scroll
    const float ry = s_fPadAxis[3];
    if(fabsf(ry) > 0.25f) PadNavScroll(ry * dt * 2.2f);
    PadNavKeys(m, true);
}
static void PadTick()   // game thread, every 2D frame (before ImGui's frame)
{
    if(!Pad_Buttons0) return;
    const uint64_t now = MP_NowMs();
    // the game stopped asking (paused / a menu of its own): read it ourselves
    if(now - s_nPadHookAt.load() > 300)
    {
        int m = WarPadButtons(); s_nPadMask = m; if(m) s_nPadUsedAt = now;
        if(Pad_Axis1) for(int i = 0; i < 4; ++i) { s_fPadAxis[i] = WarPadAxis(i); if(fabsf(s_fPadAxis[i]) > 0.3f) s_nPadUsedAt = now; }
    }
    int mm = s_nPadMask.load();
    if(Pad_Axis1 && (s_bPadNav.load() || s_bPadCursor.load()))
    {   // 3.53: LT / RT (the game may not ask for the trigger axes)
        for(int i = 4; i < 6; ++i) { float v = WarPadAxis(i); s_fPadAxis[i] = v; }
    }
    if(s_fPadAxis[4] > 0.5f) mm |= PB_LT;
    if(s_fPadAxis[5] > 0.5f) mm |= PB_RT;
    const int m = mm, pressed = m & ~s_nPadPrev, released = ~m & s_nPadPrev; s_nPadPrev = m;
    float dt = s_nPadLastMs ? (now - s_nPadLastMs) / 1000.0f : 0.016f; s_nPadLastMs = now; if(dt > 0.1f) dt = 0.1f;
    if(m && !s_bPadLogged) { s_bPadLogged = true; logger->Info("Controller: first input (mask 0x%X)", m); }
    const float W = (float)Game::ScreenWidth(), H = (float)Game::ScreenHeight();
    if(W < 10 || H < 10) return;
    // spawn picker
    if(MP::SpawnPickerShown() && !s_bPadCursor.load())
    {
        if(pressed & PB_LEFT) MP::SpawnStep(-1);
        if(pressed & PB_RIGHT) MP::SpawnStep(1);
        if(pressed & PB_A) MP::SpawnConfirm();
    }
    // Back: short = cursor on/off, long = MP menu
    if(pressed & PB_BACK) s_nPadBackAt = now;
    if((m & PB_BACK) && s_nPadBackAt && now - s_nPadBackAt > 600)
    {
        s_nPadBackAt = 0;
        if(!FloatMenu::IsOpen()) FloatMenu::Toggle();
        s_bPadCursor = false; s_bPadNav = true; s_bPadAuto = false;   // 3.53: the menu opens in navigation mode
    }
    if((released & PB_BACK) && s_nPadBackAt)
    {
        s_nPadBackAt = 0; s_bPadAuto = false;
        if(s_bPadNav.load()) { s_bPadNav = false; s_bPadCursor = true; }
        else s_bPadCursor = !s_bPadCursor.load();
    }
    // auto: an MP screen opened and the pad is being used -> navigation mode (3.53; before: the cursor)
    const bool screen = FloatMenu::IsModal() || VKeyboard::IsOpen() || MP::AuthScreenShown() || FloatMenu::IsOpen() || MP::DealerShown();
    const bool padRecent = s_nPadUsedAt.load() && now - s_nPadUsedAt.load() < 60000;
    if(screen && padRecent && !s_bPadCursor.load() && !s_bPadNav.load()) { s_bPadNav = true; s_bPadAuto = true; }
    if(!screen && s_bPadAuto && (s_bPadCursor.load() || s_bPadNav.load())) { s_bPadCursor = false; s_bPadNav = false; s_bPadAuto = false; }
    PadNavTick(m, pressed, released, dt, screen);
    if(s_bPadNav.load()) return;
    if(!s_bPadCursor.load())
    {
        if(s_bPadADown) { DispatchTouch(TOUCH_RELEASE, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = false; }
        return;
    }
    if(pressed & PB_X) { s_bPadCursor = false; s_bPadNav = true; if(s_bPadADown) { DispatchTouch(TOUCH_RELEASE, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = false; } return; }   // 3.53: X = navigation
    if(s_fPadX < 0) { s_fPadX = W * 0.5f; s_fPadY = H * 0.5f; }
    if(pressed & PB_B) { s_bPadCursor = false; s_bPadAuto = false; if(s_bPadADown) { DispatchTouch(TOUCH_RELEASE, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = false; } return; }
    // move: left stick (curve: fine near the middle), D-pad steps
    float lx = s_fPadAxis[0], ly = s_fPadAxis[1];
    if(fabsf(lx) < 0.15f) lx = 0; if(fabsf(ly) < 0.15f) ly = 0;
    const float sp = H * 1.1f;
    const float ox = s_fPadX, oy = s_fPadY;
    s_fPadX += lx * fabsf(lx) * sp * dt; s_fPadY += ly * fabsf(ly) * sp * dt;
    const float step = H * 0.035f;
    if(pressed & PB_LEFT) s_fPadX -= step; if(pressed & PB_RIGHT) s_fPadX += step;
    if(pressed & PB_UP) s_fPadY -= step;   if(pressed & PB_DOWN) s_fPadY += step;
    s_fPadX = fmaxf(0, fminf(W - 1, s_fPadX)); s_fPadY = fmaxf(0, fminf(H - 1, s_fPadY));
    const bool moved = fabsf(s_fPadX - ox) > 0.5f || fabsf(s_fPadY - oy) > 0.5f;
    if(!s_bPadADown && moved) QueueIO(TQ_POS, s_fPadX, s_fPadY);   // hover
    // A = finger 15
    if(pressed & PB_A) { DispatchTouch(TOUCH_PUSH, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = true; }
    else if(s_bPadADown && moved) DispatchTouch(TOUCH_MOVE, 15, (int)s_fPadX, (int)s_fPadY);
    if((released & PB_A) && s_bPadADown) { DispatchTouch(TOUCH_RELEASE, 15, (int)s_fPadX, (int)s_fPadY); s_bPadADown = false; }
    // right stick = scroll
    const float ry = s_fPadAxis[3];
    if(fabsf(ry) > 0.25f) { ImGui::GetIO().AddMouseWheelEvent(0.0f, -ry * dt * 8.0f); }
}
static void PadDrawHint(const char* hint)
{
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float H = ImGui::GetIO().DisplaySize.y, fs = H * 0.028f;
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, hint);
    ImVec2 hp(ImGui::GetIO().DisplaySize.x * 0.5f - ts.x * 0.5f, H - ts.y - H * 0.012f);
    dl->AddRectFilled(ImVec2(hp.x - fs * 0.5f, hp.y - fs * 0.2f), ImVec2(hp.x + ts.x + fs * 0.5f, hp.y + ts.y + fs * 0.2f), IM_COL32(0, 0, 0, 150), fs * 0.3f);
    dl->AddText(ImGui::GetFont(), fs, hp, IM_COL32(230, 230, 230, 255), hint);
}
void PadDrawCursor()   // inside the HUD's ImGui frame (hud.cpp)
{
    if(s_bPadNav.load())
    {   // 3.53: a bright frame around the highlighted item (icon buttons draw no highlight of their own)
        ImGuiContext* g = ImGui::GetCurrentContext();
        if(g && g->NavWindow && g->NavId && !g->NavDisableHighlight)
        {
            ImGuiWindow* w = g->NavWindow;
            const ImRect rel = w->NavRectRel[g->NavLayer];
            const ImVec2 off = w->DC.CursorStartPos;
            ImVec2 a(rel.Min.x + off.x, rel.Min.y + off.y), b(rel.Max.x + off.x, rel.Max.y + off.y);
            const float H = ImGui::GetIO().DisplaySize.y, t = fmaxf(2.0f, H * 0.004f);
            const float pulse = 0.65f + 0.35f * sinf((float)ImGui::GetTime() * 6.0f);
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            dl->AddRect(ImVec2(a.x - t, a.y - t), ImVec2(b.x + t, b.y + t), IM_COL32(56, 189, 248, (int)(255 * pulse)), H * 0.01f, 0, t);
        }
        PadDrawHint("A select   B back   X cursor   LB/RB page   LT/RT scroll   Start close");
        return;
    }
    if(!s_bPadCursor.load() || s_fPadX < 0) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float H = ImGui::GetIO().DisplaySize.y, r = H * 0.018f;
    ImVec2 p(s_fPadX, s_fPadY);
    ImVec2 t[3] = { p, ImVec2(p.x + r * 0.55f, p.y + r * 1.6f), ImVec2(p.x + r * 1.25f, p.y + r * 1.05f) };
    dl->AddTriangleFilled(t[0], t[1], t[2], s_bPadADown ? IM_COL32(56, 189, 248, 255) : IM_COL32(255, 255, 255, 255));
    dl->AddTriangle(t[0], t[1], t[2], IM_COL32(0, 0, 0, 255), fmaxf(1.0f, r * 0.12f));
    PadDrawHint("A tap   B close   X highlight mode   Back cursor");
}

// 3.58: Project DayZ real fog - the RenderWare camera far clip / fog distance are capped while a fog
// event is on (Game::SetFogRadius): the world past the radius is inside the game's own fog, not an overlay
DECL_HOOK(void*, RwCamFarClip, void* cam, float f)
{
    const float r = Game::FogRadius();
    if(r > 0.0f && f > r) f = r;
    return RwCamFarClip(cam, f);
}
DECL_HOOK(void*, RwCamFogDist, void* cam, float f)
{
    const float r = Game::FogRadius();
    if(r > 0.0f && f > r * 0.18f) f = r * 0.18f;
    return RwCamFogDist(cam, f);
}
// 4.04.2: crash on the game's loading screen (SIGSEGV libc memcpy <- libGTASA + 0x1E73DD RLEDecompress <-
// TextureDatabaseRuntime::LoadFullTexture <- RwTextureRead <- CSprite2d::SetTexture <- CTouchInterface::CreateAll).
// The texture databases (texdb/*/ *.dat) keep some textures RLE packed: [esc][count][unit bytes] = a run,
// anything else = one literal unit. The game never checks the end of the output buffer, so one bad /
// mismatched entry (a replaced or half-copied texdb file, another mod's texture pack) writes past the
// texture and the game dies (or hangs) while loading. Same decoder here, but it stops at the buffer end.
static std::atomic<int> s_nRleClamped{0};
DECL_HOOKv(TexRLEDecompress, uint8_t* dst, unsigned int dstSize, const uint8_t* src, unsigned int unit, unsigned int esc)
{
    if(!dst || !src || !unit || unit > 64) { TexRLEDecompress(dst, dstSize, src, unit, esc); return; }
    uint8_t* const end = dst + dstSize;
    uint8_t* d = dst;
    bool clamped = false;
    while(d < end)
    {
        if(src[0] == (uint8_t)esc)
        {
            unsigned n = src[1];
            const uint8_t* v = src + 2;
            for(unsigned k = 0; k < n; ++k)
            {
                size_t left = (size_t)(end - d);
                if(left < unit) { if(left) memcpy(d, v, left); d = end; clamped = true; break; }
                memcpy(d, v, unit); d += unit;
            }
            src += unit + 2;
        }
        else
        {
            size_t left = (size_t)(end - d);
            if(left < unit) { memcpy(d, src, left); d = end; clamped = true; break; }
            memcpy(d, src, unit); d += unit; src += unit;
        }
    }
    if(clamped && s_nRleClamped.fetch_add(1) < 20)
        logger->Error("Texture database: a packed texture is bigger than its buffer (%u bytes) - cut off instead of crashing. A texdb file (texdb/*.dat / .toc) is damaged or from another game version / texture mod.", dstSize);
}

#define HOOK_IF(_name, _sym) do { void* _a = (void*)aml->GetSym(pGameHandle, _sym); if(_a) { HOOK(_name, _a); ++hooked; } } while(0)
static void HookTouchInterface()
{
    int hooked = 0;
    HOOK_IF(TI_IsTouched,      "_ZN15CTouchInterface9IsTouchedENS_9WidgetIDsEP9CVector2Di");
    HOOK_IF(TI_IsReleased,     "_ZN15CTouchInterface10IsReleasedENS_9WidgetIDsEP9CVector2Di");
    HOOK_IF(TI_IsDoubleTapped, "_ZN15CTouchInterface14IsDoubleTappedENS_9WidgetIDsEbi");
    HOOK_IF(TI_IsHeldDown,     "_ZN15CTouchInterface10IsHeldDownENS_9WidgetIDsEi");
    HOOK_IF(TI_IsSwipedLeft,   "_ZN15CTouchInterface12IsSwipedLeftENS_9WidgetIDsEi");
    HOOK_IF(TI_IsSwipedRight,  "_ZN15CTouchInterface13IsSwipedRightENS_9WidgetIDsEi");
    HOOK_IF(TI_IsSwipedUp,     "_ZN15CTouchInterface10IsSwipedUpENS_9WidgetIDsEi");
    HOOK_IF(TI_IsSwipedDown,   "_ZN15CTouchInterface12IsSwipedDownENS_9WidgetIDsEi");
    HOOK_IF(Widget_IsTouched,  "_ZN7CWidget9IsTouchedEP9CVector2D");
    // 4.05.2: the weapon icon (and every other widget) behind the launcher / keyboard
    HOOK_IF(Widget_IsReleased,     "_ZN7CWidget10IsReleasedEP9CVector2D");
    HOOK_IF(Widget_IsDoubleTapped, "_ZN7CWidget14IsDoubleTappedEv");
    HOOK_IF(Widget_IsHeldDown,     "_ZN7CWidget10IsHeldDownEv");
    HOOK_IF(Widget_IsSwipedLeft,   "_ZN7CWidget12IsSwipedLeftEv");
    HOOK_IF(Widget_IsSwipedRight,  "_ZN7CWidget13IsSwipedRightEv");
    HOOK_IF(Widget_IsSwipedUp,     "_ZN7CWidget10IsSwipedUpEv");
    HOOK_IF(Widget_IsSwipedDown,   "_ZN7CWidget12IsSwipedDownEv");
    HOOK_IF(Pad_CycleWeapL,    "_ZN4CPad23CycleWeaponLeftJustDownEv");
    HOOK_IF(Pad_CycleWeapR,    "_ZN4CPad24CycleWeaponRightJustDownEv");
    HOOK_IF(Hid_JustPressed,   "_ZN4CHID13IsJustPressedE10HIDMapping");
    HOOK_IF(Hid_Released,      "_ZN4CHID10IsReleasedE10HIDMapping");
    HOOK_IF(Hid_DoubleTapped,  "_ZN4CHID14IsDoubleTappedE10HIDMapping");
    HOOK_IF(Hid_Pressed,       "_ZN4CHID9IsPressedE10HIDMappingPf");
    // 4.12.9: LIB_PointerGetButton is no longer hooked. Crash log (TECNO KL5, Android 14, 33 mods): SIGSEGV in
    // LIB_PointerGetButton+40 <- MobileMenu::Update <- PCMenu64 HookOf_MobileMenu_Update at the game start.
    // PCMenu, TouchFixes and Unified Button Tweaks patch the same touch path; a 2nd inline patch on that tiny
    // function broke it for them. 3.04.6 never hooked it - the CPad / CHID / CTouchInterface / CWidget hooks
    // above already keep the taps on the launcher away from the game.
    logger->Info("Keyboard HUD block: %d touch checks hooked", hooked);
}

// ---- Fallback: read the touch state straight from the game every frame --------------------
// Works even if the AND_TouchEvent hook isn't reached on this game build.
// LIB_PointerGetButton(finger, 0): 0/1 = up, 2/3 = down (same test as the game's IsPointerDown).
static int  (*LIB_PointerGetCoordinates)(int, int*, int*, float*) = nullptr;
struct PolledFinger { bool down; int x, y; };
static PolledFinger s_Poll[4] = {};
static bool s_bPollLogged = false;

static void PollTouches()
{
    if(!LIB_PointerGetButton || !LIB_PointerGetCoordinates) return;
    // the hook is alive -> it already feeds us; keep poll state in sync and skip
    bool hookAlive = s_nLastHookTouchMs && (MP_NowMs() - s_nLastHookTouchMs < 4000);
    for(int f = 0; f < 4; ++f)
    {
        int st = LIB_PointerGetButton(f, 0);
        bool down = (st == 2 || st == 3);
        int x = s_Poll[f].x, y = s_Poll[f].y;
        float pr = 0;
        if(down) { LIB_PointerGetCoordinates(f, &x, &y, &pr); MapTouch(x, y); }
        if(!hookAlive)
        {
            if(down && !s_Poll[f].down)
            {
                if(!s_bPollLogged) { s_bPollLogged = true; logger->Info("Using polled touch input (finger %d at %d,%d)", f, x, y); }
                DispatchTouch(TOUCH_PUSH, f, x, y);
            }
            else if(down && (x != s_Poll[f].x || y != s_Poll[f].y)) DispatchTouch(TOUCH_MOVE, f, x, y);
            else if(!down && s_Poll[f].down) DispatchTouch(TOUCH_RELEASE, f, x, y);
        }
        s_Poll[f].down = down; s_Poll[f].x = x; s_Poll[f].y = y;
    }
}

// ---- AML entry points ----------------------------------------------------------------------
extern "C" void OnModPreLoad()
{
    logger->SetTag(BYBIT("MP32", "MP64"));
    RegisterInterface("MPMenu", FloatMenu::GetInterface());
    RegisterInterface("MPMultiplayer", MP::GetMultiplayerApi());   // 4.13.10: PCMenu's MULTIPLAYER button
    logger->Info("MPMultiplayer interface registered (%s)", GetInterface("MPMultiplayer") ? "ok" : "AML refused it");
}

// 3.11.4: crash on closing the game. exit() destroys every library's global objects (mutexes, maps...)
// while background threads of the 60+ loaded mods (and ours: network, voice) are still running; a
// thread then locks a destroyed mutex and bionic aborts (SIGABRT on a background thread, "game
// closed by something?"). The process is ending anyway: end it right here, before any destructor.
static void MP_QuickExit()
{
    if(logger) logger->Info("MP: game closing - quick exit (no exit-time crash)");
    fflush(nullptr);
    _exit(0);
}
static void MP_Terminate()   // an uncaught C++ exception in MP: say so in the log before aborting
{
    if(logger) logger->Error("MP: std::terminate (uncaught exception) - please send this log");
    fflush(nullptr);
    abort();
}

static void JPatchGuardSetup()
{
#ifdef AML32
    static bool done = false;
    if(done || !s_bMpLoaded) return;
    done = true;
    uintptr_t jp = aml->GetLib("libJPatch.so");
    if(!jp) { logger->Info("JPatch guard: JPatch not loaded"); return; }
    if(memcmp((const void*)(jp + 0x1A558), kJPatchIdleSig, sizeof(kJPatchIdleSig)) != 0)
    { logger->Info("JPatch guard: JPatch is not 1.10.2.1 (idle camera code differs) - off"); return; }
    const uint32_t wv = ThumbPcRelTarget(jp, 0x1A59A, 0x1A59C);
    const uint32_t ov = ThumbPcRelTarget(jp, 0x3843C, 0x38442);
    if(wv != 0x5EAA8 || ov != 0xFB330) { logger->Info("JPatch guard: JPatch layout differs (0x%X / 0x%X) - off", wv, ov); return; }
    s_pJPatchWidgets = jp + wv; s_pJPatchOrig = jp + ov;
    HOOKSYM(CCam_Process, pGameHandle, "_ZN4CCam7ProcessEv");
    logger->Info("JPatch guard: on (JPatch 1.10.2.1 idle camera, CCam::Process)");
#endif
}

extern "C" void OnModLoad()
{
    atexit(MP_QuickExit);          // registered last = runs first, before the static destructors
    std::set_terminate(MP_Terminate);
    pGameLib = aml->GetLib("libGTASA.so");
    pGameHandle = aml->GetLibHandle("libGTASA.so");
    if(!pGameLib || !pGameHandle) { logger->Error("libGTASA.so not found - MP disabled"); return; }

    SET_TO(nearScreenZ,    aml->GetSym(pGameHandle, "_ZN9CSprite2d11NearScreenZE"));
    SET_TO(recipNearClip,  aml->GetSym(pGameHandle, "_ZN9CSprite2d13RecipNearClipE"));
    SET_TO(SetScissorRect, aml->GetSym(pGameHandle, "_ZN7CWidget10SetScissorER5CRect"));

    SET_TO(LIB_PointerGetButton, aml->GetSym(pGameHandle, "_Z20LIB_PointerGetButtonii"));
    SET_TO(LIB_PointerGetCoordinates, aml->GetSym(pGameHandle, "_Z25LIB_PointerGetCoordinatesiPiS_"));
    if(!LIB_PointerGetCoordinates) SET_TO(LIB_PointerGetCoordinates, aml->GetSym(pGameHandle, "_Z25LIB_PointerGetCoordinatesiPiS_Pf"));
    logger->Info("Touch polling: %s", (LIB_PointerGetButton && LIB_PointerGetCoordinates) ? "available" : "not found");

    if(!Game::Init(pGameHandle)) { logger->Error("Script engine not found - MP disabled"); return; }

    // 4.12.7: data folder /sdcard/Android/data/com.rockstargames.gtasa/files/TYFMC/MP (the old files/MP
    // is moved in, and a zip unpacked into files/TYFMC/MP later is merged in on the next start)
    char dir[400];
    { static const char* olds[] = { "MP" }; snprintf(dir, sizeof(dir), "%s", TYFMC::DataDir("MP", olds, 1).c_str()); }
    ScreenGuard::Init(dir);   // 4.12.2: no screen recording without a valid MP/src_Access.txt
    MP::Init(dir);

    ScriptGuard::Init(pGameHandle);   // 2.16.2: a script using a dead car / object handle no longer crashes the game
    ScriptGuard::InitFaultGuard(pGameHandle);   // 3.21.3: any script opcode that crashes stops that script, not the game

    HOOKSYM(CGame_Process, pGameHandle, "_ZN5CGame7ProcessEv");
#ifdef AML32
    HOOKSYM(CCamera_Process, pGameHandle, "_ZN7CCamera7ProcessEv");   // 4.13.7
#endif
    HOOKSYM(Render2dStuff, pGameHandle, "_Z13Render2dStuffv");
    HookTouchInterface();   // 2.19.5
    // 4.05.8: back on. 4.05.5 switched it off, and phones that still have a texdb pack in the game
    // folder (the v4.02 GTA V pack, another mod's textures, a half-copied update) got the 4.04.2
    // loading-screen crash / hang back. The guard is the same decoder with an end-of-buffer check, so it
    // costs nothing on good files.
    if(void* rle = (void*)aml->GetSym(pGameHandle, "_Z13RLEDecompressPhjPKhjj")) { HOOK(TexRLEDecompress, rle); logger->Info("Texture database RLE guard on (4.05.8)"); }   // 4.04.2
    { int hooked = 0; HOOK_IF(RwCamFarClip, "_Z23RwCameraSetFarClipPlaneP8RwCameraf"); HOOK_IF(RwCamFogDist, "_Z22RwCameraSetFogDistanceP8RwCameraf"); logger->Info("Fog: %d camera hooks", hooked); }   // 3.58
    HookGamepad();          // 3.39.2: controller
    {   // 4.13.4: main menu Multiplayer
        Game::InitMenu(pGameHandle);
        SET_TO(s_pPercentLoaded, aml->GetSym(pGameHandle, "_ZN14CLoadingScreen15m_PercentLoadedE"));
        int hooked = 0;
        HOOK_IF(LS_RenderSplash, "_ZN14CLoadingScreen12RenderSplashEv");
        HOOK_IF(LS_RenderLoadingBar, "_ZN14CLoadingScreen16RenderLoadingBarEv");
        HOOK_IF(MobileMenu_Update, "_ZN10MobileMenu6UpdateEv");   // 4.13.9: menu closed after a Multiplayer load
        logger->Info("Loading screen: %d hooks, percent %s", hooked, s_pPercentLoaded ? "yes" : "no");
    }

#ifdef AML32
    // GTA SA 2.00 (32-bit): the touch handler must be hooked through its PLT slot
    if(*(uint32_t*)(pGameLib + 0x202020) == 0xE8BDB001) HOOKPLT(AND_TouchEvent, pGameLib + 0x675DE4);
    else HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#else
    HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#endif

    s_bMpLoaded = true;
    MP::SetMenuApiReady();   // 4.13.11: PCMenu's MULTIPLAYER works from here on (no need to wait for OnAllModsLoaded)
    logger->Info("MP %s loaded. Data folder: %s", BYBIT("32-bit", "64-bit"), dir);
}

// 2.37: every AML mod is loaded now, so aml->HasMod() sees all of them.
// A player with CheatMenu (libCheatMenu.so / libCheatMenu64.so) is refused when joining any server.
ON_ALL_MODS_LOAD()
{
    ModScan::Scan();
    JPatchGuardSetup();   // 4.13.8: on top of JPatch's CCam::Process hook
    if(s_bMpLoaded) MP::InitMainMenu();   // 4.13.4: [Multiplayer] on the game's main menu (SAUtils tile)
    if(ModScan::CheatMenu()) logger->Info("MP: %s found - you can't join or host servers while it is installed", ModScan::What());
}
