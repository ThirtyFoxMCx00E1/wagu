// MP - multiplayer for GTA San Andreas Mobile (AML mod)
// Players meet and play together in the same game world: real-time movement, animations,
// combat, chat, name tags, kill feed and radar icons. Builds as libMP32.so / libMP64.so.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <sys/stat.h>
#include <string.h>
#include <float.h>
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
#include "ui/hud.h"
#include "ui/floatmenu.h"
#include "ui/vkeyboard.h"
#include "../imgui/rw_glue.h"
#include "net/server.h"

MYMODCFG(net.thirtyfox.mp, MP Multiplayer, 3.33, ThirtyFoxMCx00E1)   // 3.21: was stuck at 3.04.6 (crash logs showed the wrong version)
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
DECL_HOOKv(CGame_Process)
{
    CGame_Process();
    ScriptGuard::Tick();   // 3.21.3
    MP::Process();
}

// ---- 2D render: our whole HUD + floating menu ------------------------------------------
DECL_HOOKv(Render2dStuff)
{
    Render2dStuff();
    if(!s_bRWReady) { InitRenderWareFunctions(); s_bRWReady = true; }
    if(HUD::IsInitialised()) { PollTouches(); DrainIO(); }
    HUD::Render();
    int cm = s_nClearMouse.load();
    if(cm > 0 && s_nClearMouse.compare_exchange_strong(cm, cm - 1) && cm == 1) QueueIO(TQ_CLEAR);
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

// Routes one touch to MP's UI. Returns true if MP used it (the game must not see it).
static bool DispatchTouch(int type, int finger, int x, int y)
{
    std::lock_guard<std::recursive_mutex> lk(s_DispatchMx);
    const bool isRelease = (type == TOUCH_RELEASE || type == TOUCH_POINTER_UP);
    uint64_t now = MP_NowMs();

    // a finger that "never lifted" (lost event) must not block ImGui forever
    if(type == TOUCH_PUSH && s_nMouseFinger >= 0 && now - s_nLastAnyTouchMs > 1500)
    {
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
    return false;
}

DECL_HOOKv(AND_TouchEvent, int type, int finger, int x, int y)
{
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
    if(VKeyboard::IsOpen() || FloatMenu::IsModal() || UiOwnsAFinger()) { s_nKbOpenMs = MP_NowMs(); return true; }   // 2.26: + launcher, 2.28: + MP-owned finger
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
    logger->Info("Keyboard HUD block: %d touch checks hooked", hooked);
}

// ---- Fallback: read the touch state straight from the game every frame --------------------
// Works even if the AND_TouchEvent hook isn't reached on this game build.
// LIB_PointerGetButton(finger, 0): 0/1 = up, 2/3 = down (same test as the game's IsPointerDown).
static int  (*LIB_PointerGetButton)(int, int) = nullptr;
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

    // data folder: /sdcard/Android/data/com.rockstargames.gtasa/files/MP
    char dir[400];
    snprintf(dir, sizeof(dir), "%s/MP", aml->GetAndroidDataPath());
    mkdir(dir, 0777);
    MP::Init(dir);

    ScriptGuard::Init(pGameHandle);   // 2.16.2: a script using a dead car / object handle no longer crashes the game
    ScriptGuard::InitFaultGuard(pGameHandle);   // 3.21.3: any script opcode that crashes stops that script, not the game

    HOOKSYM(CGame_Process, pGameHandle, "_ZN5CGame7ProcessEv");
    HOOKSYM(Render2dStuff, pGameHandle, "_Z13Render2dStuffv");
    HookTouchInterface();   // 2.19.5

#ifdef AML32
    // GTA SA 2.00 (32-bit): the touch handler must be hooked through its PLT slot
    if(*(uint32_t*)(pGameLib + 0x202020) == 0xE8BDB001) HOOKPLT(AND_TouchEvent, pGameLib + 0x675DE4);
    else HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#else
    HOOKSYM(AND_TouchEvent, pGameHandle, "_Z14AND_TouchEventiiii");
#endif

    logger->Info("MP %s loaded. Data folder: %s", BYBIT("32-bit", "64-bit"), dir);
}

// 2.37: every AML mod is loaded now, so aml->HasMod() sees all of them.
// A player with CheatMenu (libCheatMenu.so / libCheatMenu64.so) is refused when joining any server.
ON_ALL_MODS_LOAD()
{
    ModScan::Scan();
    if(ModScan::CheatMenu()) logger->Info("MP: %s found - you can't join or host servers while it is installed", ModScan::What());
}
