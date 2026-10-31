// 3.39: "Server settings" - the management page after joining a server.
// Everyone can look (gamemodes, permissions, rules). Who can change them:
//   the 4 featured servers -> admin + moderators only (gamemodes stay fixed there),
//   a custom server        -> its host owner (the phone / VPS that hosts it).
// /settings (/srvsettings, /ss) or MP menu > Server settings.

static struct { bool have = false; PktSrvSettings s = {}; uint64_t at = 0; } s_SrvSet;

static void SrvSettingsReset() { s_SrvSet.have = false; s_SrvSet.at = 0; }
static void SrvSettingsShow();
static void SrvSettingsOnPacket(const PktSrvSettings& m)
{
    const bool reopen = s_SrvSet.have && SampDialogOpen() && s_Dlg.fn != nullptr && s_Dlg.user == 0x5353;
    s_SrvSet.have = true; s_SrvSet.s = m; s_SrvSet.at = MP_NowMs();
    if(reopen) SrvSettingsShow();
}
static void SrvSettingsSend()
{
    PktSrvSettings p = s_SrvSet.s; p.type = PKT_SRV_SETTINGS; p.op = 1;
    client.Send(&p, sizeof(p));
}
struct SrvPermRow { uint8_t bit; const char* name; };
static const SrvPermRow kSrvPerms[] = {
    { PERM_MISSION_PAIR, "Mission sync" },
    { PERM_NO_CHEATS,    "Block menu cheats" },
    { PERM_CHEAT_MENU,   "Allow CheatMenu" },
    { PERM_AFK,          "AFK tag" },
    { PERM_DMG_IND,      "Damage indicator" },
    { PERM_SHOW_IDS,     "Show player ids" },
    { PERM_NEEDS,        "Hunger + energy HUD" },
};
#define SRV_PERM_ROWS ((int)(sizeof(kSrvPerms) / sizeof(kSrvPerms[0])))
static void SrvSettingsDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0) return;
    if(!s_SrvSet.s.can) { SysMsg(COL_RED, "Only %s can change these settings.", MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer() ? "the admin and moderators" : "the server owner"); SrvSettingsShow(); return; }
    const bool featured = MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer();
    if(item < MP_GAMEMODE_COUNT)
    {
        if(featured) { SysMsg(COL_YELLOW, "The featured servers keep their gamemodes."); SrvSettingsShow(); return; }
        s_SrvSet.s.modes ^= kGameModes[item].bit;
    }
    else if(item < MP_GAMEMODE_COUNT + SRV_PERM_ROWS) s_SrvSet.s.perms ^= kSrvPerms[item - MP_GAMEMODE_COUNT].bit;
    else if(item == MP_GAMEMODE_COUNT + SRV_PERM_ROWS) s_SrvSet.s.rulesOn = s_SrvSet.s.rulesOn ? 0 : 1;
    else return;
    SrvSettingsSend();
    SrvSettingsShow();   // redrawn again when the server answers
}
static void SrvSettingsShow()
{
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "Join a server first."); return; }
    if(!s_SrvSet.have) { PktSrvSettings q; memset(&q, 0, sizeof(q)); q.type = PKT_SRV_SETTINGS; q.op = 0; client.Send(&q, sizeof(q)); SysMsg(COL_SYSTEM, "Asking the server for its settings..."); return; }
    const PktSrvSettings& s = s_SrvSet.s;
    std::string body = "Setting\tState\n";
    for(int i = 0; i < MP_GAMEMODE_COUNT; ++i)
    {
        char l[160]; snprintf(l, sizeof(l), "{FFFFFF}Gamemode: %s\t%s\n", kGameModes[i].name, (s.modes & kGameModes[i].bit) ? "{33CC55}ON" : "{FF4444}OFF"); body += l;
    }
    for(int i = 0; i < SRV_PERM_ROWS; ++i)
    {
        char l[160]; snprintf(l, sizeof(l), "{FFFFFF}Permission: %s\t%s\n", kSrvPerms[i].name, (s.perms & kSrvPerms[i].bit) ? "{33CC55}ON" : "{FF4444}OFF"); body += l;
    }
    { char l[96]; snprintf(l, sizeof(l), "{FFFFFF}Rules window\t%s\n", s.rulesOn ? "{33CC55}ON" : "{FF4444}OFF"); body += l; }
    char cap[96]; snprintf(cap, sizeof(cap), "{FFFFFF}Server settings %s", s.can ? "{33CC55}(you can change them)" : "{AAAAAA}(view only)");
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, cap, body.c_str(), s.can ? "Toggle" : "OK", "Close", SrvSettingsDlg, 0x5353);
}
static bool SrvSettingsCommand(const char* cmd, const char*)
{
    if(strcmp(cmd, "settings") && strcmp(cmd, "srvsettings") && strcmp(cmd, "ss") && strcmp(cmd, "serversettings")) return false;
    SrvSettingsShow();
    return true;
}
