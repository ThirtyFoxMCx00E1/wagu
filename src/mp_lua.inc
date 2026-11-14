// MP 4.13.29: the Lua bridge (mp_lua_bridge.h) - MonetLoader for MP runs PC MoonLoader scripts from
// files/TYFMC/MP/moonloader/ and sees MP's chat, players, dialogs and game texts as the SA-MP RPCs scripts expect.
#include "mp_lua_bridge.h"

static MpLuaSink      s_LuaSink = nullptr;
static MpLuaOutChat   s_LuaOutChat = nullptr;
static MpLuaOutDialog s_LuaOutDialog = nullptr;
static void*          s_pLuaUser = nullptr;
static bool           s_bLuaInside = false;   // no bridge loop while a raw send is running

static void LuaSetHandlers(MpLuaSink sink, MpLuaOutChat oc, MpLuaOutDialog od, void* user)
{
    s_LuaSink = sink; s_LuaOutChat = oc; s_LuaOutDialog = od; s_pLuaUser = user;
    logger->Info("Lua bridge: MonetLoader %s", sink ? "connected (PC .lua scripts see MP's chat, players and dialogs)" : "disconnected");
}
// MP -> MonetLoader. true = go on (the event may have been edited), false = a script blocked it
static bool LuaEvent(MpLuaEvent& ev)
{
    if(!s_LuaSink || s_bLuaInside) return true;
    s_bLuaInside = true;
    const int r = s_LuaSink(&ev, s_pLuaUser);
    s_bLuaInside = false;
    ev.text[sizeof(ev.text) - 1] = ev.caption[sizeof(ev.caption) - 1] = ev.name[sizeof(ev.name) - 1] = 0;
    ev.button1[sizeof(ev.button1) - 1] = ev.button2[sizeof(ev.button2) - 1] = 0;
    return r != 0;
}
static MpLuaEvent* LuaEv(int kind)
{
    static MpLuaEvent ev; memset(&ev, 0, sizeof(ev)); ev.kind = kind; return &ev;
}
static bool LuaOnConnected(int id, const char* server)
{
    if(!s_LuaSink) return true;
    MpLuaEvent* e = LuaEv(MPL_CONNECTED); e->id = id; e->a = s_nLuaPort;
    MP_CopyStr(e->name, settings.name, sizeof(e->name)); MP_CopyStr(e->text, server ? server : "", sizeof(e->text));
    MP_CopyStr(e->caption, s_szLuaHost, sizeof(e->caption));
    return LuaEvent(*e);
}
static void LuaOnDisconnected() { if(s_LuaSink) LuaEvent(*LuaEv(MPL_DISCONNECTED)); }
static void LuaOnJoin(int id, const char* name, uint32_t color)
{
    if(!s_LuaSink) return;
    MpLuaEvent* e = LuaEv(MPL_PLAYER_JOIN); e->id = id; e->color = color; MP_CopyStr(e->name, name, sizeof(e->name));
    LuaEvent(*e);
}
static void LuaOnQuit(int id, int reason)
{
    if(!s_LuaSink) return;
    MpLuaEvent* e = LuaEv(MPL_PLAYER_QUIT); e->id = id; e->a = reason; LuaEvent(*e);
}
// chat / server lines / dialogs: edited in place, false = blocked
static bool LuaOnChat(int id, char* text, int n)
{
    if(!s_LuaSink) return true;
    MpLuaEvent* e = LuaEv(MPL_PLAYER_CHAT); e->id = id; MP_CopyStr(e->text, text, sizeof(e->text));
    if(!LuaEvent(*e)) return false;
    MP_CopyStr(text, e->text, n); return true;
}
static bool LuaOnClientMessage(uint32_t& color, std::string& text)
{
    if(!s_LuaSink) return true;
    MpLuaEvent* e = LuaEv(MPL_CLIENT_MESSAGE); e->color = color; MP_CopyStr(e->text, text.c_str(), sizeof(e->text));
    if(!LuaEvent(*e)) return false;
    color = e->color; text = e->text; return true;
}
static bool LuaOnDialog(PktDialog& d)
{
    if(!s_LuaSink) return true;
    MpLuaEvent* e = LuaEv(MPL_DIALOG); e->a = d.dialogId == 0xFFFF ? -1 : d.dialogId; e->b = d.style;
    MP_CopyStr(e->caption, d.caption, sizeof(e->caption)); MP_CopyStr(e->button1, d.button1, sizeof(e->button1));
    MP_CopyStr(e->button2, d.button2, sizeof(e->button2)); MP_CopyStr(e->text, d.info, sizeof(e->text));
    if(!LuaEvent(*e)) return false;
    d.dialogId = e->a < 0 ? 0xFFFF : (uint16_t)e->a; d.style = (uint8_t)e->b;
    MP_CopyStr(d.caption, e->caption, sizeof(d.caption)); MP_CopyStr(d.button1, e->button1, sizeof(d.button1));
    MP_CopyStr(d.button2, e->button2, sizeof(d.button2)); MP_CopyStr(d.info, e->text, sizeof(d.info));
    return true;
}
// the player's own chat / command / dialog answer from MP's UI: true = MonetLoader took it over
static bool LuaOutChat(const char* text)
{
    if(!s_LuaOutChat || s_bLuaInside || !text) return false;
    s_bLuaInside = true; const int r = s_LuaOutChat(text, s_pLuaUser); s_bLuaInside = false;
    return r != 0;
}
static bool LuaOutDialog(int id, int button, int item, const char* input)
{
    if(!s_LuaOutDialog || s_bLuaInside) return false;
    s_bLuaInside = true; const int r = s_LuaOutDialog(id, button, item, input ? input : "", s_pLuaUser); s_bLuaInside = false;
    return r != 0;
}

// ---- the functions MonetLoader calls ---------------------------------------------------------
static int  LbIsConnected() { return client.GetState() == NET_CONNECTED ? 1 : 0; }
static int  LbLocalId() { return LbIsConnected() ? (int)myId : -1; }
static void LbLocalName(char* o, int n) { if(o && n > 0) MP_CopyStr(o, settings.name, n); }
static void LbServerName(char* o, int n) { if(o && n > 0) MP_CopyStr(o, s_szCurServer, n); }
static void LbServerAddress(char* o, int n, int* port) { if(o && n > 0) MP_CopyStr(o, s_szLuaHost, n); if(port) *port = s_nLuaPort; }
static int  LbMaxPlayers() { return MP_MAX_PLAYERS; }
static int  LbPlayerInfo(int id, char* name, int n, uint32_t* color, int* score, int* ping, void** ped)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return 0;
    if(id == (int)myId && LbIsConnected())
    {
        if(name && n > 0) MP_CopyStr(name, settings.name, n);
        if(color) *color = myColor; if(score) *score = players[id].score; if(ping) *ping = MP::DisplayPing();
        if(ped) { const int h = Game::LocalPed(); *ped = h > 0 ? Game::PedPtr(h) : nullptr; }
        return 1;
    }
    const RemotePlayer& p = players[id];
    if(!p.active) return 0;
    if(name && n > 0) MP_CopyStr(name, p.name, n);
    if(color) *color = p.color; if(score) *score = p.score; if(ping) *ping = p.ping;
    if(ped) *ped = p.ped ? Game::PedPtr(p.ped) : nullptr;
    return 1;
}
static void LbSendChatRaw(const char* text)
{
    if(!text || !*text) return;
    const bool in = s_bLuaInside; s_bLuaInside = true; MP::SendChat(text); s_bLuaInside = in;
}
static void SendDialogResponseNow(int id, int button, int item, const char* input)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktDialogResp r; memset(&r, 0, sizeof(r));
    r.type = PKT_DIALOG_RESPONSE; r.dialogId = (uint16_t)id; r.response = (uint8_t)button; r.listItem = (int16_t)item;
    MP_CopyStr(r.input, input ? input : "", sizeof(r.input));
    client.Send(&r, sizeof(r));
}
static void LbDialogResponseRaw(int id, int button, int item, const char* input)
{
    if(s_Dlg.open && s_Dlg.fromServer && s_Dlg.id == id) s_Dlg.open = false;   // a script answered it: close MP's window
    SendDialogResponseNow(id, button, item, input);
}
static void LbAddChatMessage(const char* text, uint32_t rgba)
{
    if(!text) return;
    const bool in = s_bLuaInside; s_bLuaInside = true; MP::AddSystemMessage(rgba, text); s_bLuaInside = in;
}
static void LbShowDialog(int id, int style, const char* caption, const char* info, const char* b1, const char* b2)
{
    if(id < 0) { if(s_Dlg.open && s_Dlg.fromServer) s_Dlg.open = false; return; }
    ShowDialog(id, style <= DIALOG_STYLE_TABLIST_HEADERS ? style : DIALOG_STYLE_MSGBOX, caption ? caption : "", info ? info : "", b1 ? b1 : "", b2 ? b2 : "", nullptr, 0, true);
}
static void LbGameText(const char* text, int timeMs, int style)
{
    typedef void (*AddBigFn)(uint16_t*, unsigned, uint16_t);
    static AddBigFn fn = nullptr; static bool looked = false;
    if(!looked) { looked = true; fn = (AddBigFn)aml->GetSym(aml->GetLibHandle("libGTASA.so"), "_ZN9CMessages13AddBigMessageEPtjt"); }
    if(!text) return;
    if(!fn) { MP::AddSystemMessage(0xFFFFFFFF, text); return; }
    static uint16_t g[400]; int i = 0;
    for(; text[i] && i < 399; ++i) g[i] = (uint8_t)text[i];
    g[i] = 0;
    fn(g, (unsigned)(timeMs > 0 ? timeMs : 3000), (uint16_t)(style >= 0 && style <= 6 ? style : 1));
}
static const char* LbDataDir() { return MP::DataDir(); }
static const char* LbVersion() { return MP_VERSION_STR; }

static MpLuaBridge s_LuaBridge = { MP_LUA_BRIDGE_VERSION, LuaSetHandlers, LbIsConnected, LbLocalId, LbLocalName, LbServerName, LbServerAddress,
    LbMaxPlayers, LbPlayerInfo, LbSendChatRaw, LbDialogResponseRaw, LbAddChatMessage, LbShowDialog, LbGameText, LbDataDir, LbVersion };
extern "C" __attribute__((visibility("default"))) void* MP_GetLuaBridge() { return &s_LuaBridge; }
