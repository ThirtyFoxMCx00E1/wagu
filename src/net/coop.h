#pragma once
// 4.13.15: Co-op story (SA:COOP style) - TWO players play the GTA SA story together. This is not the
// multiplayer "Mission sync" group (up to 16): one player is the HOST (their save = the story), the other
// is the PARTNER. The server only pairs them and relays PKT_COOP between the two.
//
//  * progress: the partner's game backs up its own saves and loads the host's save, so both are at the
//    SAME mission progress (the partner's own progress is put back when the co-op ends). After every save
//    of the host (mission passed -> save / autosave) the new save goes to the partner again.
//  * the partner keeps their own place, money, weapons, health and armour across those loads.
//  * missions: the host starts them; the partner is brought next to the host when one starts, sees the
//    host's markers (Mission sync), gets the weapons the mission gives the host and the same reward.
//  * cutscenes: the partner stands next to the host's CJ, plays idle / talk animations and looks at the
//    host; the partner's screen follows the host while it plays. Skipping needs both (vote).
#include <stdint.h>

enum eCoopOp : uint8_t
{
    COOP_INVITE = 1,   // C->S other = who I invite (I am the host).  S->C other = who invites me
    COOP_ACCEPT,       // C->S other = the host I accept.            S->C other = my partner, a = 1 I am the host
    COOP_DECLINE,      // C->S / S->C
    COOP_END,          // C->S I leave.                               S->C other = who left
    COOP_SAVE_BEGIN,   // host -> partner: a = size, b = crc32, text = a label
    COOP_SAVE_CHUNK,   // host -> partner: a = offset, n = bytes in data
    COOP_SAVE_ACK,     // partner -> host: a = bytes received in order
    COOP_SAVE_DONE,    // partner -> host: a = 1 loaded, 0 failed
    COOP_MISSION,      // host -> partner: a = 1 started / 0 ended, b = 1 passed, text = script name, pos = host
    COOP_WEAPON,       // host -> partner: a = weapon, b = ammo
    COOP_REWARD,       // host -> partner: a = money
    COOP_CUT,          // host -> partner: a = 1 cutscene on / 0 off, pos = where CJ stands, f = heading
    COOP_SKIP,         // either: a = 1 asks to skip / agrees, 0 declines
    COOP_WORLD,        // host -> partner: a = minute of the day, b = weather
};

#define COOP_CHUNK 900
#pragma pack(push, 1)
struct PktCoop
{
    uint8_t  type, op, other, n16;   // n16: unused
    uint32_t a, b;
    float    pos[3], f;
    uint16_t n; char text[14];
    uint8_t  data[COOP_CHUNK];        // only SAVE_CHUNK sends it (the packet is cut after n bytes)
};
#pragma pack(pop)
#define COOP_HEAD ((int)offsetof(PktCoop, data))
