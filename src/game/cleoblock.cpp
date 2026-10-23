// 2.35: "Disable cheats" (host setting) - the CLEO side.
//
// CLEO Android (Alexander Blade, the libcleo.so that CLEOMod / the APK loads, build "04 Apr 2021",
// armv7) hooks the game's AND_TouchEvent. Its hook (libcleo + 0x4DC4) first gives every touch to
// libcleo + 0x9BB0, which records the 9 screen touch zones (what CLEO scripts read with 0DE0 / 0DE1:
// "touch point / slide" - the swipe that opens CLEO menu scripts) and drives the CLEO menu itself
// (swipe down -> "CLEO MENU" list of .csi scripts), and then always calls the game's original.
//
// While cheats are disabled MP hooks 0x9BB0 and drops the touch there: the game still gets it (the
// CLEO hook calls the original by itself), but CLEO and its menu / swipe scripts never see it. An
// open CLEO menu is closed (libcleo + 0xB68C = its own "close menu"; menu state int at 0x19280,
// -1 = closed). Only exactly this libcleo build is touched (the code bytes are checked first).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <link.h>
#include <string.h>
#include <stdint.h>
#include "cleoblock.h"

#ifdef AML32
static uintptr_t s_nCleo = 0;
static bool      s_bHooked = false, s_bGaveUp = false;
static int       s_nTries = 0;
static volatile bool s_bBlock = false;
static void (*s_fnCloseMenu)() = nullptr;

typedef void (*TouchFn)(int, int, unsigned, unsigned, unsigned, unsigned);
static TouchFn s_fnTouch = nullptr;
static void HookOf_CleoTouch(int type, int finger, unsigned x, unsigned y, unsigned w, unsigned h)
{
    if(s_bBlock) return;
    s_fnTouch(type, finger, x, y, w, h);
}

static const uint8_t kTouchCode[16] = { 0x06,0xee,0x90,0x2a,0x2d,0xe9,0xf0,0x41,0x2d,0xed,0x02,0x8b,0xb8,0xee,0x66,0x7a };
static const uint8_t kCloseCode[16] = { 0x07,0x4b,0x4f,0xf0,0xff,0x32,0x7b,0x44,0x5a,0x60,0x9a,0x60,0x05,0x4b,0x7b,0x44 };

static int FindCleo(struct dl_phdr_info* info, size_t, void* out)
{
    const char* n = info->dlpi_name;
    if(!n || !strstr(n, "libcleo")) return 0;
    for(int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr)& ph = info->dlpi_phdr[i];
        if(ph.p_type == PT_LOAD && (ph.p_flags & PF_X) && ph.p_vaddr <= 0x9BB0 && ph.p_vaddr + ph.p_memsz > 0xB6A0)
        {
            *(uintptr_t*)out = (uintptr_t)info->dlpi_addr;
            return 1;
        }
    }
    return 0;
}

static void TryHook()
{
    if(s_bHooked || s_bGaveUp) return;
    if(++s_nTries > 40) { s_bGaveUp = true; logger->Info("CleoBlock: CLEO Android not found (no CLEO, or another CLEO build): only MP cheats are blocked"); return; }
    uintptr_t base = 0;
    dl_iterate_phdr(FindCleo, &base);
    if(!base) return;
    if(memcmp((const void*)(base + 0x9BB0), kTouchCode, 16) || memcmp((const void*)(base + 0xB68C), kCloseCode, 16))
    {
        s_bGaveUp = true;
        logger->Info("CleoBlock: libcleo found but it is not the known build: CLEO menu can't be blocked");
        return;
    }
    if(!aml->Hook((void*)(base + 0x9BB0 + 1), (void*)&HookOf_CleoTouch, (void**)&s_fnTouch) || !s_fnTouch)
    {
        s_bGaveUp = true; logger->Info("CleoBlock: hook failed"); return;
    }
    s_nCleo = base;
    s_fnCloseMenu = (void(*)())(base + 0xB68C + 1);
    s_bHooked = true;
    logger->Info("CleoBlock: ready (CLEO menu + CLEO touch scripts are blocked while the host disables cheats)");
}

void CleoBlock::Process(bool block)
{
    static unsigned s_nFrame = 0;
    if(!s_bHooked && !s_bGaveUp && (s_nFrame++ % 30) == 0) TryHook();
    s_bBlock = block && s_bHooked;
    if(s_bBlock && *(volatile int*)(s_nCleo + 0x19280) != -1 && s_fnCloseMenu) s_fnCloseMenu();   // close an open CLEO menu
}
bool CleoBlock::Available() { return s_bHooked; }
#else
// the CLEO Android library is 32-bit only
void CleoBlock::Process(bool) {}
bool CleoBlock::Available() { return false; }
#endif
