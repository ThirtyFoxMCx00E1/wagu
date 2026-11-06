#pragma once
// 4.11: SA-MP gamemodes on MP servers (net/server_samp.inc). A custom server (phone host, Termux, VPS)
// can run a PC SA-MP server folder as it is: server.cfg, gamemodes/*.amx, filterscripts/*.amx and
// scriptfiles/ - the compiled Pawn scripts run in the Pawn abstract machine (third_party/pawn-amx,
// zLib licence) with the SA-MP natives made on MP (messages, dialogs, textdraws, teleports, money,
// weapons, objects, Pawn.CMD, sscanf, timers, PVars, files) and MySQL R41 natives on SQLite (the
// tables and columns the script uses are made by themselves - no MySQL server needed).
#include <stdint.h>

// PKT_SAMP: S<->C. Old phones / servers ignore it.
enum eSampOp : uint8_t
{
    SPO_HELLO = 1,    // S->C  the server runs a SA-MP gamemode: text = its name. Every /command goes to it.
    SPO_CMD,          // C->S  text = "/command params"
    SPO_TDSELECT,     // S->C  a = 1 SelectTextDraw (val = hover colour) / 0 CancelSelectTextDraw
    SPO_TDCLICK,      // C->S  val = textdraw id (MP id: 0x4000+ global, 0x6000+ player), -1 = Esc / cancelled
    SPO_CHECKPOINT,   // S->C  a = 1 set / 0 off, f = x y z size (b = 1 race checkpoint)
    SPO_CPENTER,      // C->S  a = 1 entered / 0 left, b = 1 race checkpoint
    SPO_MAPICON,      // S->C  a = icon id (0..99), b = marker type (0 = remove), val = colour, f = x y z
    SPO_REMOVEBLD,    // S->C  val = model (-1 = every model), f = x y z radius
    SPO_CAMERA,       // S->C  a = 0 behind the player, 1 = set pos (f = x y z), 2 = look at (f = x y z)
    SPO_CONTROL,      // S->C  a = 1 controllable / 0 frozen
    SPO_WORLD,        // S->C  val = virtual world, a = interior
    SPO_TIME,         // S->C  a = hour, b = minute (val = weather, -1 = keep)
    SPO_SOUND,        // S->C  val = sound id, f = x y z
    SPO_ATTACH,       // S->C  a = slot (0..9), b = bone, val = model (-1 = remove), f = x y z, text = "rx ry rz sx sy sz"
    SPO_LABEL,        // S->C  val = label id, a = 1 add / 0 del, f = x y z dist, b = colour index, text = text
    SPO_KEYS,         // C->S  val = SA-MP key bits (for OnPlayerKeyStateChange)
};
struct PktSamp { uint8_t type, op, a, b; int32_t val; float f[4]; char text[200]; };
