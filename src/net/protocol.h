// MP - GTA San Andreas Mobile multiplayer
// Wire protocol shared by the Android client (libMP32/libMP64) and the server
// (embedded LAN host inside the mod + standalone mp_server for PC/VPS).
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>

#define MP_PROTO_VERSION   4
// 3.04.6: mod build number (3.04.6 = 30406), sent when joining (PktJoin5) and in the welcome (PktWelcome2).
// Servers refuse phones older than MP_MIN_BUILD (old versions let players use cheats) and log them;
// phones refuse servers older than that too (they would let old phones in).
// 3.10: build 31000. MP_MIN_BUILD stays 30406 and the lobby stays thirtyfox/mp5/: 3.04.6 (no cheats)
// and 3.10 phones play together on the same servers; only 3.04.5 and older (mp4) stay shut out.
#define MP_BUILD           32200   // 3.22
#define MP_MIN_BUILD       30406
#define MP_AUTH_MIN_BUILD  32106   // 3.21.6: servers with the login system refuse older phones (they could not log in)
#define MP_VERSION_STR     "3.22"
#define MP_VOICE_BUILD     31100   // 3.11: phones from this build on get voice packets
#define MP_DOWNLOAD_URL    "thirtyfoxmcx00e1.github.io/wagu"
#define MP_DEFAULT_HOST    "127.0.0.1"
#define MP_DEFAULT_PORT    1922
#define MP_DEFAULT_SERVER_NAME "MP LAN Server"   // default hosted server; one per Wi-Fi/hotspot
#define MP_MAX_PLAYERS     100    // hard cap; each server picks its own limit (1..100)
#define MP_MAX_SERVERS     99     // custom servers in the list (+ the default server = 100)
#define MP_NAME_LEN        24
#define MP_CHAT_LEN        128
#define MP_INVALID_ID      0xFF
#define MP_TIMEOUT_MS      20000   // 2.21: was 10 s; covers an internet relay reconnect (no more random timeouts)
#define MP_SYNC_RATE_MS    50     // 20 Hz on-foot / in-car sync (lower with many players, 2.11)
#define MP_SYNC_NEAR_M     350.0f // server forwards every sync between players closer than this
#define MP_SYNC_FAR_MS     1000   // ...and 1 sync per second between players farther apart
#define MP_SOCK_BUF        (512 * 1024)    // UDP socket buffers (game client / server)
#define MP_SERVER_SOCK_BUF (8 * 1024 * 1024)   // 3.22: the server socket: a burst from 100 phones never overflows (the kernel caps it at net.core.rmem_max)
#define BCW_MAP_COUNT_WIRE 6      // 3.04: Brief Case War map vote slots (2 open, 4 locked)
#define BCW_TEAM_SIZE_WIRE 10     // 3.04: 20 players = 10 per team
#define MP_RELAY_SOCK_BUF  (128 * 1024)    // 2.14: relay bridges/proxy; anything older than ~0.2 s is useless

enum ePacket : uint8_t
{
    PKT_JOIN = 1,       // C->S  request to join
    PKT_WELCOME,        // S->C  join accepted, your id
    PKT_REJECT,         // S->C  join refused
    PKT_PLAYER_JOIN,    // S->C  a player is in the world
    PKT_PLAYER_LEAVE,   // S->C  a player left
    PKT_SYNC,           // C->S->C  player state
    PKT_CHAT,           // C->S->C  chat message
    PKT_DEATH,          // C->S->C  player died (kill feed)
    PKT_DAMAGE,         // C->S->target  shooter-authoritative damage
    PKT_PING,           // C->S
    PKT_PONG,           // S->C
    PKT_LEAVE,          // C->S  graceful quit
    PKT_SERVER_MSG,     // S->C  system message
    PKT_QUERY,          // C->S  server-list info request (no join needed)
    PKT_INFO,           // S->C  server-list info reply
    PKT_SET_COLOR,      // C->S->all  player picked a new name colour
    PKT_PAUSE,          // C->S->others  player paused / resumed their game
    PKT_WORLD,          // S->C  serverworld_host: the server's own time of day + weather (timecycle)
    PKT_STATUS,         // C->S->others  player started / ended a mission or cutscene (2.9)
    // 2.13 environment sync (older servers ignore these; everything else keeps working)
    PKT_EXPLOSION,      // C->S->near    explosion made by a player (grenade, rocket, car blowing up...)
    PKT_FIRE,           // C->S->near    fire started by a player (molotov, flamethrower...)
    PKT_PICKUP_DROP,    // C->S          I died holding this weapon: drop it for everybody
    PKT_PICKUP_ADD,     // S->C          a shared weapon pickup is on the ground (also sent on join)
    PKT_PICKUP_TAKE,    // C->S          I picked it up
    PKT_PICKUP_REMOVE,  // S->C          it's gone (taken or expired)
    // 2.16 (older phones ignore these)
    PKT_PERMS,          // S->C          server permissions the host set (PERM_*), on join + when changed
    PKT_PAIR,           // C->S->C       mission sync with somebody (request / accept / end)
    // 2.17 (older phones ignore these)
    PKT_SHOT,           // C->S->near    every bullet / rocket / grenade a player fired
    PKT_VEH_STATE,      // C->S->near    driver's car: colours, paintjob, tuning, health, damage, doors...
    PKT_MISSION_MARKERS,// C->S->partner mission sync: my mission's radar markers
    // 2.18 (older phones ignore these)
    PKT_NPC,            // C->S->near    NPC host: the people + traffic around me
    PKT_NPC_HIT,        // C->S->owner   I hurt one of your NPCs / NPC cars
    PKT_NPC_CLAIM,      // C->S->owner   I took your NPC car (I drive it now: remove yours)
    // ---- 2.19 ----
    PKT_PLAYER_INFO,    // C->S->all     my ping + score (player list / scoreboard), every 3 s
    PKT_JACK,           // C->S->victim  I'm pulling you out of your car (carjack)
    // ---- 2.21 ----
    PKT_ANNOUNCE,       // C->S->all     game event line (race finished, teleported, DM streak...): the
                        //               server turns it into a PKT_SERVER_MSG so older phones show it too
    // ---- 2.25 ----
    PKT_KICKED,         // S->C          you were kicked / you are banned (date, administrator, reason)
    PKT_ADMIN_CMD,      // C->S          2.37: default server admin (SENJI): kick / ban / mute / cheats off
    // ---- 3.01: SA-MP features (older phones / servers ignore these types) ----
    PKT_EVENT,          // C->S->all     race / derby / job / DM lobby, progress (positions), out, finish
    PKT_TEAM,           // C->S->all     my class: team (turf wars) + skin, on change + every 5 s
    PKT_TURF,           // S->C          gang zone owners + who is attacking + capture progress
    PKT_DIALOG,         // S->C          SA-MP dialog (msgbox / input / list / password / tablist)
    PKT_DIALOG_RESPONSE,// C->S          button + list item + input text
    PKT_TEXTDRAW,       // S->C          SA-MP textdraw show / hide (640x448 canvas)
    PKT_MISSION_NPC,    // C->S->group   3.02: the peds / cars my mission made (same format as PKT_NPC)
    PKT_BCW_STATE,      // S->C          3.04: Brief Case War match state (teams, briefcases, timers)
    PKT_BCW_CMD,        // C->S          3.04: Brief Case War: pick team / vote map / vehicle spawn notice
    PKT_BCW_ROLL,       // S->C          3.04: leader pick (slot-machine roll) for both teams
    PKT_BCW_SCORE,      // S->C          3.04: end-of-match scoreboard (one packet per team)
    PKT_BCW_EVENT,      // S->C          3.04: steal / drop / return / capture / victory notices
    PKT_CLOTHES,        // C->S->C       3.10: CJ's clothes (3.04.6 phones / servers simply ignore it)
    PKT_VOICE,          // C->S->C       3.11: voice chat (one Opus frame, 20 ms), only to players nearby
    PKT_AUTH,           // S<->C         3.21.6: login system (challenge / ticket / result), see net/authcrypto.h
};

enum eRegion : uint8_t { REGION_ASIA = 0, REGION_EUROPE, REGION_NA, REGION_OCEANIA, REGION_COUNT };
static const char* const g_MPRegionNames[REGION_COUNT] = { "Asia", "Europe", "NA", "Oceania" };

enum eRejectReason : uint8_t { REJECT_FULL = 1, REJECT_VERSION = 2, REJECT_SHUTDOWN = 3, REJECT_PASSWORD = 4, REJECT_BANNED = 5, REJECT_KICKED = 6, REJECT_NAME_RESERVED = 7, REJECT_OUTDATED = 8, REJECT_OLD_SERVER = 9 };   // 8/9: 3.04.6 your MP is too old / the server is too old   // 7: 2.37 the name SENJI is reserved   // 5/6: 2.25 // 3: host closed the server, players take over
enum eLeaveReason  : uint8_t { LEAVE_TIMEOUT = 0, LEAVE_QUIT = 1, LEAVE_KICK = 2 };

enum eSyncFlags : uint8_t
{
    SYNC_SHOOTING = 1 << 0,
    SYNC_IN_CAR   = 1 << 1,
    SYNC_DUCKING  = 1 << 2,
    SYNC_DEAD     = 1 << 3,
    SYNC_DRIVER   = 1 << 4,
    SYNC_MISSION  = 1 << 5,   // 2.9: on a mission (own story world: hidden from the others)
    SYNC_CUTSCENE = 1 << 6,   // 2.9: in a cutscene (2.19: real cutscenes only)
    SYNC_SCENE    = 1 << 7,   // 2.19: scripted scene (player control off, no cutscene): still visible to the partner
};

#pragma pack(push, 1)
struct SyncData
{
    float    pos[3];        // ped centre (GET_CHAR_COORDINATES)
    float    heading;       // degrees
    float    aim[3];        // aim/shoot target point
    float    speed;         // m/s
    uint8_t  health;
    uint8_t  armour;
    uint8_t  weapon;
    uint8_t  flags;         // eSyncFlags
    uint16_t skin;
    uint16_t carModel;      // 0 = on foot
    float    carPos[3];
    float    carHeading;
};

// ---- 2.13: extended player state, appended right after PktSync ------------------------------
// Senders always append it; receivers use it when the packet is long enough. 2.12 and older
// ignore the extra bytes, and a 2.12 host cuts it off (then the old smoothing is used).
#define MP_SYNC_EXT_VER    1
#define MP_ANIM_SLOTS      8      // 2.15: whole blend tree (walk/run/idle + everything else); 2.13 sent 2
#define MP_ANIM_SLOTS_V1   2
struct AnimSync
{
    uint32_t hash;          // CAnimBlendHierarchy::m_hashKey
    int16_t  group;         // AssocGroupId, -1 = not from an anim group (script / custom IFP)
    int16_t  id;            // AnimationId inside the group
    float    time;          // current time in the anim (s)
    uint8_t  blend;         // blend amount * 255
    uint8_t  speed;         // playback speed * 50
    uint16_t flags;         // anim flags (safe subset)
    char     block[16];     // IFP block name ("ped", "fight_b", "dancing"...)
};
// 2.13 wire layout (raw struct), still accepted from 2.13 players
struct SyncExtV1
{
    uint8_t  ver, moveState, animCount, pedState;
    uint32_t time;
    float    vel[3], carFwd[3], carUp[3];
    AnimSync anim[MP_ANIM_SLOTS_V1];
};
// decoded state (in memory only; on the wire it is the compact v3 below)
struct SyncExt
{
    uint8_t  ver;           // MP_SYNC_EXT_VER
    uint8_t  moveState;     // CPed::m_nMoveState (still / walk / jog / run / sprint)
    uint8_t  animCount;     // used entries in anim[]
    uint8_t  pedState;      // CPed::m_ePedState (low byte)
    uint32_t time;          // sender clock (ms) when this state was taken
    float    vel[3];        // m/s, on foot or the car
    float    carFwd[3];     // car rotation: forward vector
    float    carUp[3];      // car rotation: up vector (all zero = unknown)
    AnimSync anim[MP_ANIM_SLOTS];
    uint8_t  fullAnims;     // 2.15: anim[] is the complete blend tree incl. walking -> mirror it 1:1
    uint8_t  hasVeh;        // 2.15: passenger in another player's car
    uint8_t  vehOwner;      //       player id of the driver (whose car it is)
    int8_t   vehSeat;       //       passenger seat (0 = front right)
    // 2.17 extra block (bit 3 of the v3 header; 2.16 decoders ignore the trailing bytes)
    uint8_t  hasX2;
    uint8_t  x2flags;       // X2_*
    uint8_t  interior;      // area / interior id (EnEx): 0 = outside
    float    steer, gas, brake;   // driver inputs (X2_VEHFAST)
    uint16_t misc;          // Hydra nozzles / misc component angle
    float    doomV, doomH;  // tank / firetruck turret
    int8_t   stickX, stickY; // 2.19 X2_STICK: on-foot joystick (-127..127), drives the remote walk / run blend
};
enum eX2Flags : uint8_t { X2_AIMING = 1, X2_HORN = 2, X2_VEHFAST = 4, X2_SHOTSYNC = 8, X2_NPCHOST = 16,
                         X2_STICK = 32, X2_JETPACK = 64, X2_INVISIBLE = 128 };   // NPCHOST: 2.18; STICK/JETPACK/INVISIBLE: 2.19

// ---- 2.14: compact extension on the wire (v2) ---------------------------------------------------
// Same information as SyncExt, ~12 bytes on foot without anims instead of 152 (velocity and
// rotation as int16, anims only when used, block names without padding). Decoded into SyncExt.
#define MP_SYNC_EXT_V2     2
#define MP_SYNC_MAX_LEN    (sizeof(PktSync) + 320)   // 2.15: v3 with 8 anims stays well below
static inline int16_t MP_Q16(float v, float scale) { float q = v * scale; if(q > 32767.f) q = 32767.f; if(q < -32767.f) q = -32767.f; return (int16_t)(q < 0 ? q - 0.5f : q + 0.5f); }
static inline void MP_Put16(uint8_t*& o, uint16_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); o += 2; }
static inline void MP_Put32(uint8_t*& o, uint32_t v) { MP_Put16(o, (uint16_t)v); MP_Put16(o, (uint16_t)(v >> 16)); }
static inline uint16_t MP_Get16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static inline uint32_t MP_Get32(const uint8_t* p) { return MP_Get16(p) | ((uint32_t)MP_Get16(p + 2) << 16); }

#define MP_SYNC_EXT_V3     3
// 2.15 v3: header + optional rotation + optional passenger seat + up to 8 compact anims.
// Group anims: 9 bytes (no names); script / custom IFP anims: 11 bytes + block name.
static inline int MP_EncodeSyncExt(const SyncExt& e, uint8_t* out)
{
    uint8_t* o = out;
    float fwd[3], up[3], vel[3]; memcpy(fwd, e.carFwd, 12); memcpy(up, e.carUp, 12); memcpy(vel, e.vel, 12);
    bool rot = (up[0] != 0 || up[1] != 0 || up[2] != 0);
    int n = e.animCount > MP_ANIM_SLOTS ? MP_ANIM_SLOTS : e.animCount;
    *o++ = MP_SYNC_EXT_V3; *o++ = e.moveState; *o++ = e.pedState;
    uint8_t* bits = o++;
    MP_Put32(o, e.time);
    for(int i = 0; i < 3; ++i) MP_Put16(o, (uint16_t)MP_Q16(vel[i], 100.f));        // cm/s
    if(rot) { for(int i = 0; i < 3; ++i) MP_Put16(o, (uint16_t)MP_Q16(fwd[i], 32767.f));
              for(int i = 0; i < 3; ++i) MP_Put16(o, (uint16_t)MP_Q16(up[i], 32767.f)); }
    if(e.hasVeh) { *o++ = e.vehOwner; *o++ = (uint8_t)e.vehSeat; }
    int written = 0;
    for(int i = 0; i < n; ++i)
    {
        AnimSync a; memcpy(&a, &e.anim[i], sizeof(a));
        float t = a.time * 1000.f; uint16_t tm = (uint16_t)(t < 0 ? 0 : (t > 65535.f ? 65535.f : t));
        if(a.group >= 0 && a.group < 255 && !a.block[0])
        {   // from an anim group: the receiver knows the rest
            *o++ = 0; *o++ = (uint8_t)a.group; MP_Put16(o, (uint16_t)a.id);
            MP_Put16(o, tm); *o++ = a.blend; *o++ = a.speed; *o++ = (uint8_t)a.flags;
        }
        else
        {
            size_t bl = strnlen(a.block, 15);
            *o++ = 1; MP_Put32(o, a.hash); MP_Put16(o, tm); *o++ = a.blend; *o++ = a.speed; *o++ = (uint8_t)a.flags;
            *o++ = (uint8_t)bl; memcpy(o, a.block, bl); o += bl;
        }
        ++written;
    }
    if(e.hasX2)
    {
        *o++ = e.x2flags; *o++ = e.interior;
        if(e.x2flags & X2_VEHFAST)
        {
            float st = e.steer, ga = e.gas, br = e.brake, dv = e.doomV, dh = e.doomH;
            *o++ = (uint8_t)(int8_t)MP_Q16(st > 1.27f ? 1.27f : (st < -1.27f ? -1.27f : st), 100.f);
            *o++ = (uint8_t)(int8_t)MP_Q16(ga > 1.f ? 1.f : (ga < -1.f ? -1.f : ga), 100.f);
            *o++ = (uint8_t)MP_Q16(br > 1.f ? 1.f : (br < 0.f ? 0.f : br), 250.f);
            MP_Put16(o, e.misc);
            MP_Put16(o, (uint16_t)MP_Q16(dv, 1000.f)); MP_Put16(o, (uint16_t)MP_Q16(dh, 1000.f));
        }
        if(e.x2flags & X2_STICK) { *o++ = (uint8_t)e.stickX; *o++ = (uint8_t)e.stickY; }   // 2.19
    }
    *bits = (uint8_t)((rot ? 1 : 0) | (e.fullAnims ? 2 : 0) | (e.hasVeh ? 4 : 0) | (e.hasX2 ? 8 : 0) | (written << 4));
    return (int)(o - out);
}

// v1 (2.13 raw struct), v2 (2.14 compact) or v3 (2.15). false = no / broken extension.
static inline bool MP_DecodeSyncExt(const uint8_t* p, int len, SyncExt& e)
{
    memset(&e, 0, sizeof(e));
    if(len <= 0) return false;
    if(p[0] == MP_SYNC_EXT_VER)
    {
        SyncExtV1 v;
        if(len < (int)sizeof(SyncExtV1)) return false;
        memcpy(&v, p, sizeof(v));
        if(v.animCount > MP_ANIM_SLOTS_V1) return false;
        e.ver = v.ver; e.moveState = v.moveState; e.animCount = v.animCount; e.pedState = v.pedState; e.time = v.time;
        memcpy(e.vel, v.vel, 12); memcpy(e.carFwd, v.carFwd, 12); memcpy(e.carUp, v.carUp, 12);
        memcpy(e.anim, v.anim, sizeof(v.anim));
        return true;
    }
    if(p[0] == MP_SYNC_EXT_V3)
    {
        if(len < 14) return false;
        const uint8_t* q = p + 4; const uint8_t* end = p + len;
        e.ver = MP_SYNC_EXT_VER; e.moveState = p[1]; e.pedState = p[2];
        bool rot = p[3] & 1; e.fullAnims = (p[3] >> 1) & 1; e.hasVeh = (p[3] >> 2) & 1; int n = p[3] >> 4;
        if(n > MP_ANIM_SLOTS) return false;
        e.time = MP_Get32(q); q += 4;
        float v[3]; for(int i = 0; i < 3; ++i) { v[i] = (int16_t)MP_Get16(q) / 100.f; q += 2; }
        memcpy(e.vel, v, 12);
        if(rot)
        {
            if(q + 12 > end) return false;
            float f[3], u[3];
            for(int i = 0; i < 3; ++i) { f[i] = (int16_t)MP_Get16(q) / 32767.f; q += 2; }
            for(int i = 0; i < 3; ++i) { u[i] = (int16_t)MP_Get16(q) / 32767.f; q += 2; }
            memcpy(e.carFwd, f, 12); memcpy(e.carUp, u, 12);
        }
        if(e.hasVeh) { if(q + 2 > end) return false; e.vehOwner = q[0]; e.vehSeat = (int8_t)q[1]; q += 2; }
        for(int i = 0; i < n; ++i)
        {
            if(q + 9 > end) return false;
            AnimSync a; memset(&a, 0, sizeof(a));
            if(q[0] == 0)
            {
                a.group = q[1]; a.id = (int16_t)MP_Get16(q + 2); a.time = MP_Get16(q + 4) / 1000.f;
                a.blend = q[6]; a.speed = q[7]; a.flags = q[8]; q += 9;
            }
            else
            {
                if(q + 11 > end) return false;
                a.group = -1; a.hash = MP_Get32(q + 1); a.time = MP_Get16(q + 5) / 1000.f;
                a.blend = q[7]; a.speed = q[8]; a.flags = q[9];
                int bl = q[10]; q += 11;
                if(bl > 15 || q + bl > end) return false;
                memcpy(a.block, q, (size_t)bl); a.block[bl] = 0; q += bl;
            }
            memcpy(&e.anim[i], &a, sizeof(a));
        }
        e.animCount = (uint8_t)n;
        if((p[3] & 8) && q + 2 <= end)   // 2.17 extra block
        {
            e.hasX2 = 1; e.x2flags = q[0]; e.interior = q[1]; q += 2;
            if(e.x2flags & X2_VEHFAST)
            {
                if(q + 9 > end) { e.x2flags &= (uint8_t)~X2_VEHFAST; return true; }
                e.steer = (int8_t)q[0] / 100.f; e.gas = (int8_t)q[1] / 100.f; e.brake = q[2] / 250.f;
                e.misc = MP_Get16(q + 3);
                e.doomV = (int16_t)MP_Get16(q + 5) / 1000.f; e.doomH = (int16_t)MP_Get16(q + 7) / 1000.f;
                q += 9;
            }
            if(e.x2flags & X2_STICK)   // 2.19
            {
                if(q + 2 > end) { e.x2flags &= (uint8_t)~X2_STICK; return true; }
                e.stickX = (int8_t)q[0]; e.stickY = (int8_t)q[1]; q += 2;
            }
        }
        return true;
    }
    if(p[0] != MP_SYNC_EXT_V2 || len < 14) return false;
    const uint8_t* q = p + 4; const uint8_t* end = p + len;
    e.ver = MP_SYNC_EXT_VER; e.moveState = p[1]; e.pedState = p[2];
    bool rot = p[3] & 1; int n = (p[3] >> 1) & 3;
    if(n > MP_ANIM_SLOTS) return false;
    e.time = MP_Get32(q); q += 4;
    float v[3]; for(int i = 0; i < 3; ++i) { v[i] = (int16_t)MP_Get16(q) / 100.f; q += 2; }
    memcpy(e.vel, v, 12);
    if(rot)
    {
        if(q + 12 > end) return false;
        float f[3], u[3];
        for(int i = 0; i < 3; ++i) { f[i] = (int16_t)MP_Get16(q) / 32767.f; q += 2; }
        for(int i = 0; i < 3; ++i) { u[i] = (int16_t)MP_Get16(q) / 32767.f; q += 2; }
        memcpy(e.carFwd, f, 12); memcpy(e.carUp, u, 12);
    }
    for(int i = 0; i < n; ++i)
    {
        if(q + 15 > end) return false;
        AnimSync a; memset(&a, 0, sizeof(a));
        a.hash = MP_Get32(q); a.group = (int16_t)MP_Get16(q + 4); a.id = (int16_t)MP_Get16(q + 6);
        a.time = MP_Get16(q + 8) / 1000.f; a.blend = q[10]; a.speed = q[11]; a.flags = MP_Get16(q + 12);
        int bl = q[14]; q += 15;
        if(bl > 15 || q + bl > end) return false;
        memcpy(a.block, q, (size_t)bl); a.block[bl] = 0; q += bl;
        memcpy(&e.anim[i], &a, sizeof(a));
    }
    e.animCount = (uint8_t)n;
    return true;
}

// ---- 2.13: environment -------------------------------------------------------------------------
#define MP_ENV_RANGE_M     400.0f   // explosions / fires go to players closer than this
#define MP_MAX_PICKUPS     64       // shared weapon pickups on a server
#define MP_PICKUP_LIFE_MS  60000    // dropped weapons vanish after a minute
struct PktExplosion   { uint8_t type; uint8_t id; uint8_t expType; float pos[3]; };
struct PktFire        { uint8_t type; uint8_t id; uint8_t size; float pos[3]; };   // size 1..4
struct PktPickupDrop  { uint8_t type; uint8_t weapon; uint16_t ammo; float pos[3]; };
struct PktPickupAdd   { uint8_t type; uint16_t uid; uint8_t weapon; uint16_t ammo; float pos[3]; };
struct PktPickupTake  { uint8_t type; uint16_t uid; };
struct PktPickupRemove{ uint8_t type; uint16_t uid; uint8_t takenBy; };   // takenBy MP_INVALID_ID = expired

struct PktJoin        { uint8_t type; uint16_t proto; char name[MP_NAME_LEN]; uint16_t skin; uint32_t color; }; // color 0 = let the server pick
#define MP_PASSWORD_LEN    16
struct PktJoin2       { PktJoin j; char password[MP_PASSWORD_LEN + 1]; };   // 2.19: + server password (old servers ignore the tail)
#define MP_UID_LEN         16
#define MP_REASON_LEN      64
struct PktJoin3       { PktJoin2 j2; char uid[MP_UID_LEN + 1]; };   // 2.25: + this phone's id (bans survive a name change)
struct PktJoin4       { PktJoin3 j3; char adminKey[128 + 1]; };   // 2.37: + admin key (only the reserved name SENJI needs it)
struct PktJoin5       { PktJoin4 j4; uint32_t build; };              // 3.04.6: + mod build (MP_BUILD)
// which MP versions an old join packet can come from (they had no build number)
inline const char* MP_GuessOldVersion(int joinLen, uint32_t build, char* out, int outLen)
{
    if(build) { snprintf(out, outLen, "%u.%02u.%u", build / 10000, (build / 100) % 100, build % 100); return out; }
    if(joinLen < (int)sizeof(PktJoin2)) return "2.18 or older";
    if(joinLen < (int)sizeof(PktJoin3)) return "2.19 - 2.24";
    if(joinLen < (int)sizeof(PktJoin4)) return "2.25 - 2.36";
    return "2.37 - 3.04.5";
}
// 2.37: trailers (Tug Stairs, baggage, farm / utility / article trailers) have no driving physics:
// sitting in one (cheat / CLEO spawn) made it fly and ram players to death in one hit
inline bool MP_IsTrailerModel(int m) { return m == 435 || m == 450 || m == 584 || m == 591 || m == 606 || m == 607 || m == 608 || m == 610 || m == 611; }
#define MP_RAM_WEAPON      49      // WEAPONTYPE_RAMMEDBYCAR
#define MP_RAM_MAX_DAMAGE  20.0f   // 2.37: one ram can't take more than 20 HP
#define MP_RAM_COOLDOWN_MS 700     // 2.37: and only one ram per attacker every 0.7 s
enum eAdminCmd : uint8_t { ADMIN_KICK = 1, ADMIN_BAN = 2, ADMIN_MUTE = 3, ADMIN_UNMUTE = 4, ADMIN_CHEATS = 5 };
struct PktAdminCmd    { uint8_t type; uint8_t cmd; uint8_t target; uint8_t value; char reason[MP_REASON_LEN]; };   // 2.37, value: ADMIN_CHEATS 1 = cheats off
struct PktKicked      { uint8_t type; uint8_t banned; uint32_t when; char admin[MP_NAME_LEN]; char reason[MP_REASON_LEN]; };   // 2.25, when = unix time
struct PktWelcome     { uint8_t type; uint8_t yourId; uint8_t maxPlayers; uint32_t color; char serverName[32]; };
struct PktWelcome2    { PktWelcome w; uint32_t build; };   // 3.04.6: + server build (older servers: no tail -> refused)
struct PktReject      { uint8_t type; uint8_t reason; };
struct PktPlayerJoin  { uint8_t type; uint8_t id; char name[MP_NAME_LEN]; uint32_t color; uint16_t skin; };
struct PktPlayerLeave { uint8_t type; uint8_t id; uint8_t reason; };
struct PktSync        { uint8_t type; uint8_t id; uint32_t seq; SyncData d; };
struct PktChat        { uint8_t type; uint8_t id; char text[MP_CHAT_LEN]; };
struct PktDeath       { uint8_t type; uint8_t victim; uint8_t killer; uint8_t weapon; };
struct PktDamage      { uint8_t type; uint8_t from; uint8_t target; uint8_t weapon; float amount; };
struct PktPing        { uint8_t type; uint32_t time; };
struct PktLeave       { uint8_t type; };
struct PktServerMsg   { uint8_t type; uint32_t color; char text[MP_CHAT_LEN]; };
struct PktAnnounce    { uint8_t type; uint8_t id; uint32_t color; char text[MP_CHAT_LEN]; };   // 2.21
// World clock: 1 game minute = 1 real second. absMinute = game minutes since the Unix epoch (= Unix
// seconds), so every server (default, custom, dedicated) shows the same normal day and weather.
// mode WORLD_NATURAL: the game's own weather cycle (per map area), locked to absMinute.
// mode WORLD_FORCED:  one weather everywhere (field weather). 2.7 and older only read hour/minute/weather.
enum eWorldMode : uint8_t { WORLD_FORCED = 0, WORLD_NATURAL = 1 };
struct PktWorld       { uint8_t type; uint8_t hour; uint8_t minute; uint8_t weather; uint8_t mode; uint32_t absMinute; };
#define MP_PKT_WORLD_V1_SIZE 4
struct PktPause       { uint8_t type; uint8_t id; uint8_t paused; };
// state = STATUS_* bits, mission = the mission's script name (e.g. "SWEET1"), empty if none
enum eStatus : uint8_t { STATUS_MISSION = 1, STATUS_CUTSCENE = 2 };
struct PktStatus      { uint8_t type; uint8_t id; uint8_t state; char mission[8]; };
// 2.16 server permissions + mission pairs
enum ePerm : uint8_t { PERM_MISSION_PAIR = 1, PERM_NO_CHEATS = 2, PERM_CHEAT_MENU = 4, PERM_V219 = 0x80 };   // 3.00: CHEAT_MENU = the host allows CheatMenu (libCheatMenu.so / libCheatMenu64.so), custom servers only   // 2.35: NO_CHEATS = the host disabled MP + CLEO menu cheats   // V219: server knows 2.19 packets (jack, player info, partner damage)     // "Mission sync" (2.17 name); hosts toggle it in the MP menu
// 3.02: + the server's gamemodes (GM_*). modesMagic == MP_MODES_MAGIC when the server sends them;
// older servers send 2 bytes (= no gamemodes on a custom server). Older phones read 2 bytes.
enum eGameMode : uint16_t
{
    GM_CLASS = 1,      // /class: class selection, gang / police teams, class skins
    GM_TURF = 2,       // gang zones on the radar + turf wars (/turf, /teams)
    GM_RACE = 4,       // /race queue: GTA SA races
    GM_DERBY = 8,      // /derby
    GM_DM = 16,        // /dm deathmatch zones
    GM_JOBS = 32,      // /job trucking, pizza, taxi ...
    GM_LABELS = 64,    // 3D text labels at the starts
    GM_ICONS = 128,    // 3.03: unused (map icons = a player setting)
    GM_ACTORS = 256,   // people standing at the job starts
    GM_BUBBLES = 512,  // 3.03: unused (chat bubbles = a player setting)
    GM_ALL = 1023,
};
#define MP_MODES_MAGIC 0x47
struct PktPerms       { uint8_t type; uint8_t perms; uint8_t modesMagic; uint8_t pad; uint16_t modes; };
enum ePairAction : uint8_t { PAIR_REQUEST = 1, PAIR_ACCEPT = 2, PAIR_END = 3, PAIR_LINKED = 4, PAIR_DENIED = 5,
                          PAIR_DECLINE = 6 /* 2.19 C->S: no thanks */, PAIR_DECLINED = 7 /* 2.19 S->C: they said no */ };
// C->S: other = who I ask / accept / leave.  S->C: other = the other player, action as above
struct PktPair        { uint8_t type; uint8_t action; uint8_t other; };
// ---- 2.17 --------------------------------------------------------------------------------------
// every shot: kind 0 = bullet (a = start, b = impact), 1 = thrown / fired projectile from the ped,
// 2 = projectile from the player's vehicle (a = position, b = direction, force)
#define MP_SHOT_MAX        8
struct ShotEntry      { uint8_t kind; uint8_t weapon; float a[3]; float b[3]; float force; };
struct PktShot        { uint8_t type; uint8_t id; uint8_t count; ShotEntry e[MP_SHOT_MAX]; };   // only count entries are sent
#define MP_PKT_SHOT_HDR    3
// driver's car state (sent on change + every 2 s)
#define MP_VEH_MOD_SLOTS   17
enum eVehFlags : uint8_t { VEHF_ENGINE = 1, VEHF_LIGHTS = 2, VEHF_SIREN = 4, VEHF_GEAR_UP = 8 };
struct PktVehState    { uint8_t type; uint8_t id; uint16_t model; uint8_t col[4]; int8_t paintjob; uint8_t lock; uint8_t flags; uint8_t isAuto;
                        uint8_t doors[6]; uint8_t wheels[4]; uint32_t panels; uint32_t lights; uint16_t health;
                        uint16_t mods[MP_VEH_MOD_SLOTS]; uint8_t doorOpen[6]; float pos[3]; };
// mission sync: the radar markers of my mission, for my partner
#define MP_MISSION_MARKERS_MAX 8
// 2.31: Mission sync is a GROUP (was: exactly 2 players). PAIR_LINKED / PAIR_END are sent once per
// member ("other" = that member). C->S PAIR_END with other = a member removes that member from
// your group (Unsync); other = you / MP_INVALID_ID = you leave the group.
#define MP_MISSION_GROUP_MAX 16   // up to 16 players on the same missions
struct MissionMarker  { float pos[3]; uint32_t colour; uint8_t display; uint8_t kind; };
struct PktMissionMarkers { uint8_t type; uint8_t id; uint8_t count; MissionMarker m[MP_MISSION_MARKERS_MAX]; };
#define MP_PKT_MM_HDR      3
// ---- 2.18: NPC sync ----------------------------------------------------------------------------
// In every area ONE phone (the NPC host: lowest player id around, not on a mission) keeps its
// people and traffic and sends them; the other phones there stop their own and show the host's.
#define MP_NPC_PEDS_MAX    16
#define MP_NPC_CARS_MAX    10
#define MP_NPC_RANGE_M     200.0f   // NPC packets go to players this close to the host
#define MP_NPC_SHOW_M      150.0f   // NPCs this close to me are shown
// 2.28 *_SPAWNED: a script / CLEO / MP-spawned (or parked) entity of the sender. Shared with EVERY player
// near it - not only the sender's NPC followers - so a spawned car or ped is one thing everybody sees.
enum eNpcFlags : uint8_t { NPCF_DEAD = 1, NPCF_DUCK = 2, NPCF_SPAWNED = 4 };
enum eNpcCarFlags : uint8_t { NCARF_DEAD = 1, NCARF_SIREN = 2, NCARF_LIGHTS = 4, NCARF_ENGINE = 8, NCARF_SPAWNED = 16 };
#define MP_NPC_SPAWNED_PEDS 6    // per frame, out of MP_NPC_PEDS_MAX
#define MP_NPC_SPAWNED_CARS 6    // per frame, out of MP_NPC_CARS_MAX
struct NpcPed         { uint32_t uid; uint16_t model; float pos[3]; int16_t vel[3]; uint8_t heading; uint8_t move; uint8_t health; uint8_t flags; uint8_t weapon; };
struct NpcCar         { uint32_t uid; uint16_t model; uint16_t driver; float pos[3]; int8_t fwd[3]; int8_t up[3]; int16_t vel[3]; uint16_t health; uint8_t col[2]; uint8_t flags; uint8_t pad; };
// on the wire: header, then nPeds NpcPed, then nCars NpcCar
struct PktNpcHdr      { uint8_t type; uint8_t id; uint8_t interior; uint8_t nPeds; uint8_t nCars; uint8_t seq; float centre[3]; };
struct PktNpc         { PktNpcHdr h; NpcPed peds[MP_NPC_PEDS_MAX]; NpcCar cars[MP_NPC_CARS_MAX]; };   // decoded form
struct PktNpcHit      { uint8_t type; uint8_t from; uint8_t owner; uint8_t kind; uint32_t uid; uint16_t damage; uint8_t weapon; };   // kind 0 ped, 1 car
struct PktNpcClaim    { uint8_t type; uint8_t from; uint8_t owner; uint8_t pad; uint32_t uid; };
struct PktSetColor    { uint8_t type; uint8_t id; uint32_t color; };   // RGBA, alpha forced to FF
struct PktQuery       { uint8_t type; uint32_t time; };
struct PktInfo        { uint8_t type; uint32_t time; uint16_t proto; uint8_t players; uint8_t maxPlayers; uint8_t region; char serverName[32]; };
#define MP_PKT_INFO_V1_SIZE ((int)sizeof(PktInfo))
struct PktInfo2       { PktInfo i; uint8_t locked; };   // 2.19: password protected
struct PktInfo3       { PktInfo2 i2; uint8_t icon; };  // 2.24: server icon 0 = default (main_df_icon.png), 1..250 = icon_N.png
#define MP_ICON_MAX 250
#define MP_LANG_LEN 24
struct PktInfo4       { PktInfo3 i3; char lang[MP_LANG_LEN]; };
struct PktInfo5       { PktInfo4 i4; uint32_t startUnix; };   // 2.27: when the server started (older copy wins a duplicate merge)   // 2.25: server language ("English", "Filipino / English"...)
// language text is shown in lists: no '|' or ':' (listing separators)
inline void MP_CleanLang(char* s) { for(; *s; ++s) if(*s == '|' || *s == ':' || *s == '\n' || *s == '\r') *s = '/'; }
struct PktPlayerInfo  { uint8_t type; uint8_t id; uint16_t ping; int16_t score; };
struct PktJack        { uint8_t type; uint8_t from; uint8_t victim; uint8_t seat; };

// ---- 3.01: SA-MP features --------------------------------------------------------------------
enum eEvKind  : uint8_t { EVK_NONE = 0, EVK_RACE, EVK_JOB, EVK_DM, EVK_DERBY };
enum eEvState : uint8_t { EVS_LOBBY = 1, EVS_PROGRESS, EVS_OUT, EVS_FINISH, EVS_LEAVE };
// LOBBY: ms = time left until GO (others joining use the same start). PROGRESS: cp + dist to the
// next checkpoint (race positions), every second. FINISH: ms = race time. OUT: wrecked (derby).
struct PktEvent       { uint8_t type; uint8_t id; uint8_t kind; uint8_t idx; uint8_t state; uint8_t cp; uint8_t slot; uint8_t pad; uint32_t ms; float dist; };
struct PktTeam        { uint8_t type; uint8_t id; uint8_t team; uint8_t pad; int16_t skin; };
#define MP_TURF_MAX 32
// 3.01: race queue. "/race" = PktEvent kind RACE, idx MP_EV_QUEUE, state LOBBY (join) / LEAVE.
// The server answers with id MP_EV_SERVER: idx QUEUE = queue count (cp = players, slot = needed),
// idx 0..MP_SA_RACE_COUNT-1 = "this race starts" (slot = your grid place, cp = racers, ms = time to GO).
#define MP_EV_QUEUE      255
#define MP_EV_SERVER     255
#define MP_SA_RACE_COUNT 27   // the GTA SA races (src/mp_races_sa.inc)
#define MP_RACE_PLAYERS  14   // a race starts when this many players are in the queue
#define MP_RACE_WAIT_MS  45000   // 3.21: ...or when 2+ players waited this long
#define MP_RACE_SOLO_MS  60000   // 3.21: ...or one player waited this long (solo time trial)
#define MP_RACE_GO_MS    20000   // 3.21: time on the grid before GO (was 12 s)
struct PktTurf        { uint8_t type; uint8_t count; uint8_t owner[MP_TURF_MAX]; uint8_t attacker[MP_TURF_MAX]; uint8_t progress[MP_TURF_MAX]; };   // progress 0..100
enum eDialogStyle : uint8_t { DIALOG_STYLE_MSGBOX = 0, DIALOG_STYLE_INPUT, DIALOG_STYLE_LIST, DIALOG_STYLE_PASSWORD, DIALOG_STYLE_TABLIST, DIALOG_STYLE_TABLIST_HEADERS };
#define MP_DIALOG_INFO_LEN 640
struct PktDialog      { uint8_t type; uint8_t style; uint16_t dialogId; char caption[64]; char button1[16]; char button2[16]; char info[MP_DIALOG_INFO_LEN]; };
struct PktDialogResp  { uint8_t type; uint8_t response; uint16_t dialogId; int16_t listItem; char input[64]; };
struct PktTextDraw    { uint8_t type; uint8_t show; uint16_t tdId; float x, y, letterX, letterY, boxX; uint32_t color, boxColor, bgColor; uint8_t font, align, outline, shadow, useBox, proportional, pad[2]; char text[256]; };
// ---- 3.04: Brief Case War (private 24/7 gamemode server, see net/bcw.h) ---------------------
struct BcwCase      { uint8_t state; uint8_t carrier; uint8_t stealer; uint8_t progress; float pos[3]; uint16_t dropLeftS; };   // state eBcwCase, progress 0..255 of the steal bar
struct PktBcwState
{
    uint8_t  type;
    uint8_t  state;          // eBcwState
    uint8_t  map;            // 0..BCW_MAP_COUNT-1
    uint8_t  minPlayers;     // queue size that starts a match (20)
    uint16_t timerS;         // seconds left in this state (roll / results / vote); match clock when live
    uint8_t  count[2];       // players in each team
    uint8_t  leader[2];      // leader id per team (MP_INVALID_ID = none yet)
    BcwCase  cases[2];       // [0] = Gustav briefcase (at Gustav base), [1] = Schelapz briefcase
    uint8_t  votes[BCW_MAP_COUNT_WIRE];
    uint8_t  team[MP_MAX_PLAYERS];   // eBcwTeam per player id
};
enum eBcwCmd : uint8_t { BCWC_TEAM = 1, BCWC_VOTE = 2, BCWC_LEAVE_TEAM = 3 };
struct PktBcwCmd    { uint8_t type; uint8_t cmd; uint8_t arg; };
struct PktBcwRoll   { uint8_t type; uint8_t n[2]; uint8_t leader[2]; uint8_t ids[2][BCW_TEAM_SIZE_WIRE]; };
struct BcwScoreRow  { uint8_t id; uint8_t team; uint8_t leader; uint8_t kills; uint8_t deaths; uint8_t steals; uint8_t caps; uint8_t returns; uint16_t damage; uint16_t score; char name[MP_NAME_LEN]; };
struct PktBcwScore  { uint8_t type; uint8_t winner; uint8_t team; uint8_t map; uint8_t part; uint8_t n; uint16_t durationS; BcwScoreRow rows[BCW_TEAM_SIZE_WIRE]; };
enum eBcwEvent : uint8_t { BCWE_STOLEN = 1, BCWE_DROPPED, BCWE_PICKED, BCWE_RETURNED, BCWE_VICTORY, BCWE_MATCH_START, BCWE_MAP };
struct PktBcwEvent  { uint8_t type; uint8_t ev; uint8_t team; uint8_t who; };

// 3.10: clothing sync. CJ (model 0) can't be put on another ped, so a remote CJ is rebuilt the way
// the game builds the cutscene CJ ("csplay" in a special slot + CClothes::ConstructPedModel) from
// these keys (CPedClothesDesc: 10 model keys, 18 texture keys, fat + muscle stat). on = 0: the
// player is not CJ any more (or turned "real skin" off) -> show the normal avatar skin again.
#define MP_CLOTHES_MODELS   10
#define MP_CLOTHES_TEXTURES 18
struct PktClothes
{
    uint8_t  type;           // PKT_CLOTHES
    uint8_t  id;             // sender (set by the server)
    uint8_t  on;             // 1 = wearing these clothes (CJ), 0 = off
    uint8_t  pad;
    uint32_t model[MP_CLOTHES_MODELS];
    uint32_t tex[MP_CLOTHES_TEXTURES];
    float    fat, muscle;
};
// 3.21.6: login system. The default server and Brief Case War always ask for a login, custom
// servers when their owner turns it on (Add server -> Authentication system, mp_server --auth).
// Until the login is done the player is connected but nobody sees them and nothing they send
// (position, chat, shots...) reaches anybody.
enum eAuthOp : uint8_t { AUTH_CHALLENGE = 1, AUTH_TICKET = 2, AUTH_RESULT = 3 };
enum eAuthStyle : uint8_t { AUTH_STYLE_NORMAL = 0, AUTH_STYLE_BCW = 1 };
struct PktAuthChal   { uint8_t type; uint8_t op; uint8_t style; uint8_t pad; char chal[33]; char server[32]; };
struct PktAuthTicket { uint8_t type; uint8_t op; uint16_t plen; uint8_t sig[64]; };   // + plen bytes of ticket text
struct PktAuthResult { uint8_t type; uint8_t op; uint8_t ok; uint8_t pad; char user[24]; char msg[96]; };
#define MP_AUTH_TICKET_MAX 255
#pragma pack(pop)

// 3.11: voice chat. One Opus frame (48 kHz mono, 20 ms) per packet, sent the moment it is
// recorded (no batching). The server sends it only to 3.11+ players within MP_VOICE_RANGE.
#define MP_VOICE_RANGE     40.0f   // like real life: loud up close, quieter further away, nothing past 40 m
// 3.11: server message every 45 min 20 s (not on join)
#define MP_AD_INTERVAL_MS  ((45ull * 60ull + 20ull) * 1000ull)
#define MP_AD_TEXT         "Join our discord server for more updates & info! https://discord.gg/ZqgXKYhNmv"
#define MP_VOICE_MAX_BYTES 400
#define MP_VOICE_HELPER_PORT 19228   // 3.11.1: MP Voice app on 127.0.0.1
enum eVoiceFlags : uint8_t { VOICE_END = 1 };   // last frame of a talk spurt (mic off)
struct PktVoiceHdr { uint8_t type; uint8_t id; uint16_t seq; uint8_t flags; uint8_t pad; uint16_t len; };   // + len bytes of Opus (packed: sizeof 8)

inline uint32_t MP_ClothesHash(const PktClothes& c)   // FNV-1a over the outfit, 0 = none
{
    if(!c.on) return 0;
    uint32_t h = 2166136261u;
    const uint8_t* b = (const uint8_t*)c.model;
    for(size_t i = 0; i < sizeof(c.model) + sizeof(c.tex) + 2 * sizeof(float); ++i) { h ^= b[i]; h *= 16777619u; }
    return h ? h : 1;
}
inline bool MP_ClothesSane(PktClothes& c)
{
    if(!c.on) return true;
    if(!(c.fat == c.fat) || c.fat < 0.0f || c.fat > 1000.0f) c.fat = 0.0f;
    if(!(c.muscle == c.muscle) || c.muscle < 0.0f || c.muscle > 1000.0f) c.muscle = 0.0f;
    int n = 0; for(int i = 0; i < MP_CLOTHES_MODELS; ++i) if(c.model[i]) ++n;
    return n >= 1;   // at least a body piece, or it's not a real outfit
}

// 2.18: NPC packet encode / decode (variable length, every field copied: no unaligned access)
inline int MP_EncodeNpc(const PktNpc& m, uint8_t* out)
{
    int np = m.h.nPeds > MP_NPC_PEDS_MAX ? MP_NPC_PEDS_MAX : m.h.nPeds, nc = m.h.nCars > MP_NPC_CARS_MAX ? MP_NPC_CARS_MAX : m.h.nCars;
    PktNpcHdr h = m.h; h.nPeds = (uint8_t)np; h.nCars = (uint8_t)nc;
    int o = 0; memcpy(out, &h, sizeof(h)); o += sizeof(h);
    memcpy(out + o, m.peds, sizeof(NpcPed) * np); o += (int)sizeof(NpcPed) * np;
    memcpy(out + o, m.cars, sizeof(NpcCar) * nc); o += (int)sizeof(NpcCar) * nc;
    return o;
}
inline bool MP_DecodeNpc(const uint8_t* in, int len, PktNpc& m)
{
    if(len < (int)sizeof(PktNpcHdr)) return false;
    memset(&m, 0, sizeof(m));
    memcpy(&m.h, in, sizeof(PktNpcHdr));
    int np = m.h.nPeds, nc = m.h.nCars;
    if(np > MP_NPC_PEDS_MAX || nc > MP_NPC_CARS_MAX) return false;
    if(len < (int)sizeof(PktNpcHdr) + np * (int)sizeof(NpcPed) + nc * (int)sizeof(NpcCar)) return false;
    int o = sizeof(PktNpcHdr);
    memcpy(m.peds, in + o, sizeof(NpcPed) * np); o += (int)sizeof(NpcPed) * np;
    memcpy(m.cars, in + o, sizeof(NpcCar) * nc);
    return true;
}

// RGBA colours handed out by the server (SA-MP like palette)
static const uint32_t g_MPPlayerColors[] =
{
    0xFF8C13FF, 0xC715FFFF, 0x20B2AAFF, 0xDC143CFF, 0x6495EDFF, 0xF0E68CFF, 0x778899FF, 0xFF1493FF,
    0xF4A460FF, 0xEE82EEFF, 0xFFD720FF, 0x8B4513FF, 0x4949A0FF, 0x148B8BFF, 0x14FF7FFF, 0x556B2FFF,
    0x0FD9FAFF, 0x10DC29FF, 0x534081FF, 0x0495CDFF, 0xEF6CE8FF, 0xBD34DAFF, 0x247C1BFF, 0x0C8E5DFF,
    0x635B03FF, 0xCB7ED3FF, 0x65ADEBFF, 0x5C1ACCFF, 0xF2F853FF, 0x11F891FF, 0x7B39AAFF, 0x53EB10FF,
};

inline void MP_CopyStr(char* dst, const char* src, size_t n)
{
    strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = 0;
}
