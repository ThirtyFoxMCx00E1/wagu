#pragma once
// MP 4.13.29: the Lua bridge between MP (libMP32/64.so) and MonetLoader for MP (libmonetloader.so).
// MonetLoader runs PC MoonLoader .lua scripts (files/TYFMC/MP/moonloader/). Its SA-MP layer (samp* functions,
// samp.events, raknet* functions) is built on SA-MP RakNet RPCs; on MP servers there is no libsamp.so, so MP hands
// its network events over this bridge, MonetLoader turns them into the SA-MP RPCs scripts know (ClientMessage, Chat,
// ShowDialog, ServerJoin / Quit, InitGame ...), runs the scripts' handlers (they may read, edit or block them) and
// gives the result back. The same file is cpp/game/mp_lua_bridge.h in MonetLoaderOSS - keep both copies identical.
//
// lookup: dlsym(libMP32.so / libMP64.so, "MP_GetLuaBridge") -> MpLuaBridge*
// threading: every call is made on the game thread (MP::Process / CGame::Process).
#include <stdint.h>

#define MP_LUA_BRIDGE_VERSION 1

enum MpLuaEventKind
{
    MPL_CONNECTED = 1,    // joined a server: id = my id, name = my name, text = server name, a = port, caption = address
    MPL_DISCONNECTED,     // left / lost the server
    MPL_CLIENT_MESSAGE,   // a server / system line: color = 0xRRGGBBAA, text          (editable, blockable)
    MPL_PLAYER_CHAT,      // a player's chat line: id, text                            (editable, blockable)
    MPL_PLAYER_JOIN,      // id, name, color
    MPL_PLAYER_QUIT,      // id, a = reason (0 timeout, 1 quit, 2 kick)
    MPL_DIALOG,           // a = dialog id, b = style, caption, button1, button2, text  (editable, blockable)
    MPL_GAMETEXT,         // a = style, b = time ms, text                              (editable, blockable)
};

struct MpLuaEvent
{
    int      kind;
    int      id;
    uint32_t color;
    int      a, b;
    char     name[32];
    char     caption[128];
    char     button1[64];
    char     button2[64];
    char     text[4096];
};

// MonetLoader's handler: 1 = MP goes on with the (maybe edited) event, 0 = a script blocked it
typedef int (*MpLuaSink)(MpLuaEvent* ev, void* user);
// MonetLoader's hooks for what the player sends from MP's own UI: 1 = MonetLoader took it over (it sends it
// itself through SendChatRaw / DialogResponseRaw after the scripts' onSendChat / onSendCommand / onSendDialogResponse)
typedef int (*MpLuaOutChat)(const char* text, void* user);
typedef int (*MpLuaOutDialog)(int dialogId, int button, int listItem, const char* input, void* user);

struct MpLuaBridge
{
    int  version;                                       // MP_LUA_BRIDGE_VERSION
    void (*SetHandlers)(MpLuaSink sink, MpLuaOutChat outChat, MpLuaOutDialog outDialog, void* user);
    int  (*IsConnected)();
    int  (*LocalId)();                                  // -1 offline
    void (*LocalName)(char* out, int n);
    void (*ServerName)(char* out, int n);
    void (*ServerAddress)(char* out, int n, int* port);
    int  (*MaxPlayers)();
    // 1 = connected. ped = CPed* when streamed in (else null)
    int  (*PlayerInfo)(int id, char* name, int n, uint32_t* color, int* score, int* ping, void** ped);
    void (*SendChatRaw)(const char* text);              // chat line or "/command" to the server, no Lua hook
    void (*DialogResponseRaw)(int dialogId, int button, int listItem, const char* input);
    void (*AddChatMessage)(const char* text, uint32_t rgba);   // local line in MP's chat
    void (*ShowDialog)(int dialogId, int style, const char* caption, const char* info, const char* b1, const char* b2);   // as if the server sent it
    void (*GameText)(const char* text, int timeMs, int style);
    const char* (*DataDir)();                           // files/TYFMC/MP
    const char* (*Version)();                           // "4.13.29"
};
