// 3.39: "Server settings" - the management page after joining a server.
// Everyone can look (gamemodes, permissions, rules). Who can change them:
//   the 4 featured servers -> admin + moderators only (gamemodes stay fixed there),
//   a custom server        -> its host owner (the phone / VPS that hosts it).
// /settings (/srvsettings, /ss) or MP menu > Server settings.

static struct { bool have = false; PktSrvSettings s = {}; uint64_t at = 0; } s_SrvSet;

static bool s_bHudMenuPending = false;   // 4.07: /settings waits for the server's answer (mp_hudmenu.inc)
static void HudMenuShow();
static void HudMenuOpen();
static void SrvSettingsReset() { s_SrvSet.have = false; s_SrvSet.at = 0; }
static void SrvSettingsShow();
static void SrvSettingsOnPacket(const PktSrvSettings& m)
{
    const bool reopen = SampDialogOpen() && s_Dlg.fn != nullptr && s_Dlg.user == 0x5353;
    s_SrvSet.have = true; s_SrvSet.s = m; s_SrvSet.at = MP_NowMs();
    if(reopen) SrvSettingsShow();
    if(s_bHudMenuPending) { s_bHudMenuPending = false; HudMenuShow(); }   // 4.07
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
        if(featured && !MP::IsOnDefault()) { SysMsg(COL_YELLOW, "This featured server runs only its own gamemode."); SrvSettingsShow(); return; }   // 3.53: the default server's staff can
        s_SrvSet.s.modes ^= kGameModes[item].bit;
    }
    else if(item < MP_GAMEMODE_COUNT + SRV_PERM_ROWS) s_SrvSet.s.perms ^= kSrvPerms[item - MP_GAMEMODE_COUNT].bit;
    else if(item == MP_GAMEMODE_COUNT + SRV_PERM_ROWS) s_SrvSet.s.rulesOn = s_SrvSet.s.rulesOn ? 0 : 1;
    else if(item == MP_GAMEMODE_COUNT + SRV_PERM_ROWS + 1)
    {   // 3.58: anti-cheat gamemode
        if(!(s_SrvSet.s.pad & 0x80)) { SysMsg(COL_YELLOW, "This server is older than MP 3.58 (anti-cheat state unknown)."); SrvSettingsShow(); return; }
        if(!(s_SrvSet.s.pad & 2)) { SysMsg(COL_YELLOW, MP::IsOnDefault() ? "The anti-cheat is always off on the default server." : "The anti-cheat always runs on Brief Case War, MW3 and Mega Vil."); SrvSettingsShow(); return; }
        s_SrvSet.s.pad ^= 1;
    }
    else if(item == MP_GAMEMODE_COUNT + SRV_PERM_ROWS + 2 && (s_SrvSet.s.pad & 0x20)) s_SrvSet.s.pad ^= 0x10;   // 4.13.15: login system
    else return;
    SrvSettingsSend();
    SrvSettingsShow();   // redrawn again when the server answers
}
static void SrvSettingsShow()
{
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "Join a server first."); return; }
    // 3.39.1: never wait - show what this phone already knows (gamemodes + permissions the server sent
    // at the join) right away, ask the server in the background and refresh the page when it answers.
    // A server older than 3.39 can't answer at all (it updates itself within ~10 minutes): view only.
    const bool newSrv = client.GetServerBuild() >= 33900;
    if(!s_SrvSet.have || MP_NowMs() - s_SrvSet.at > 15000)
    {
        if(newSrv) { PktSrvSettings q; memset(&q, 0, sizeof(q)); q.type = PKT_SRV_SETTINGS; q.op = 0; client.Send(&q, sizeof(q)); }
        if(!s_SrvSet.have)
        {
            memset(&s_SrvSet.s, 0, sizeof(s_SrvSet.s));
            s_SrvSet.s.type = PKT_SRV_SETTINGS; s_SrvSet.s.modes = s_nServerModes; s_SrvSet.s.perms = s_nServerPerms; s_SrvSet.s.can = 0;
            s_SrvSet.s.rulesOn = MP::IsOnDefault() ? 1 : 0;
        }
    }
    if(!newSrv)
    {
        char b[16]; unsigned sb = client.GetServerBuild();
        snprintf(b, sizeof(b), "%u.%02u", sb / 10000, (sb / 100) % 100);
        SysMsg(COL_YELLOW, "This server still runs MP %s: settings are view only until it updates to 3.39 (the 24/7 servers update by themselves).", sb ? b : "(old)");
    }
    // 3.50.1: the default server always runs every gamemode (whatever an older copy reported), and its
    // staff ([MOD] Trunk / the admin) manage it even before the server's answer arrives
    if((MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer()) && (MP::IsLocalMod() || MP::IsLocalAdmin()) && newSrv) s_SrvSet.s.can = 1;
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
    {   // 3.58: the anti-cheat as its own gamemode row
        char l[160];
        if(!(s.pad & 0x80)) snprintf(l, sizeof(l), "{FFFFFF}Gamemode: Anti-cheat\t{AAAAAA}?\n");
        else snprintf(l, sizeof(l), "{FFFFFF}Gamemode: Anti-cheat%s\t%s\n", (s.pad & 2) ? "" : ((s.pad & 1) ? " {AAAAAA}(always on)" : " {AAAAAA}(always off)"), (s.pad & 1) ? "{33CC55}ON" : "{FF4444}OFF");
        body += l;
    }
    if(s.pad & 0x20)
    {   // 4.13.15: the default server's login system (off by default, back off when the last staff leaves)
        char l[128]; snprintf(l, sizeof(l), "{FFFFFF}Login system (authentication)\t%s\n", (s.pad & 0x10) ? "{33CC55}ON" : "{FF4444}OFF"); body += l;
    }
    char cap[96]; snprintf(cap, sizeof(cap), "{FFFFFF}Server settings %s", s.can ? "{33CC55}(you can change them)" : "{AAAAAA}(view only)");
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, cap, body.c_str(), s.can ? "Toggle" : "OK", "Close", SrvSettingsDlg, 0x5353);
}
static void ModCheckReport()
{   // 3.50.1: /modcheck - why am I (not) [MOD] here?
    LoadModFile();
    SysMsg(COL_YELLOW, "Name: %s. moderator.txt: %s.", settings.name, s_bModFileSeen ? s_szModFound : "not found (put it in the MP folder)");
    if(s_bModFileSeen) SysMsg(COL_YELLOW, "Key: %s.", s_bModMaster ? "the MP moderator key (valid on every server)" : client.HasModKey() ? "not the MP moderator key (only servers that list it)" : "empty / unreadable");
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_YELLOW, "Not on a server right now."); return; }
    unsigned sb = client.GetServerBuild();
    char b[16]; snprintf(b, sizeof(b), "%u.%02u", sb / 10000, (sb / 100) % 100);
    const bool mod = myId < MP_MAX_PLAYERS && players[myId].active && UI::IsModName(players[myId].name);
    SysMsg(mod ? COL_GREEN : COL_YELLOW, "Server \"%s\" runs MP %s: you are %s.", s_szCurServer, sb ? b : "(old)", mod ? "[MOD] (moderator permissions)" : "NOT [MOD]");
    if(!mod && s_bModMaster && sb < 34010)
        SysMsg(COL_RED, "This copy of the server is older than MP 3.40.1 and doesn't know the Trunk key. It is hosted by an old phone / a VPS that didn't update - rejoin when it runs 3.50.1.");
    else if(!mod && s_bModMaster) SysMsg(COL_RED, "Rejoin the server (Disconnect, then Connect) - the key is sent at the join.");
}
static bool SrvSettingsCommand(const char* cmd, const char* arg)
{
    // 3.51.1: /cheats on|off - [ADMIN] / [MOD] (default + featured servers) or the host of a custom server
    // switch the MP cheats for everyone. The CLEO menu + Cheat Menu stay blocked either way.
    if(!strcmp(cmd, "cheats") || !strcmp(cmd, "togglecheats"))
    {
        const bool staff = MP::IsDefaultAdmin() || MP::IsLocalMod() || MP::IsLocalAdmin();
        const bool host = s_Server.IsRunning() && !MP::IsHostingDefault();
        if(client.GetState() != NET_CONNECTED || (!staff && !host)) { SysMsg(COL_RED, "Only [ADMIN], [MOD] or the host of this server can switch the cheats."); return true; }
        bool off;
        if(arg && (!strcasecmp(arg, "on") || !strcasecmp(arg, "enable"))) off = false;
        else if(arg && (!strcasecmp(arg, "off") || !strcasecmp(arg, "disable"))) off = true;
        else { SysMsg(COL_YELLOW, "MP cheats are %s. Use /cheats on or /cheats off (the CLEO menu + Cheat Menu are always blocked).", MP::CheatsBlocked() ? "OFF" : "ON"); return true; }
        if(staff) SendAdminCmd(ADMIN_CHEATS, -1, off ? 1 : 0, "");
        else { settings.disableCheats = off; ApplyServerPerms(); SaveConfig(); SysMsg(COL_YELLOW, off ? "*** MP cheats disabled for everyone on your server." : "*** MP cheats allowed again on your server."); }
        return true;
    }
    if(!strcmp(cmd, "modcheck") || !strcmp(cmd, "whyamimod")) { ModCheckReport(); return true; }
    if(!strcmp(cmd, "settings") || !strcmp(cmd, "hud") || !strcmp(cmd, "textdraw")) { HudMenuOpen(); return true; }   // 4.07: Settings Menu (host owner)
    if(strcmp(cmd, "srvsettings") && strcmp(cmd, "ss") && strcmp(cmd, "serversettings")) return false;
    SrvSettingsShow();
    return true;
}

// 3.39.1: moderator.txt check after the join - tell the player why there is no [MOD] tag
static void ModCheckProcess(uint64_t now)
{
    static uint64_t s_At = 0; static bool s_Said = false;
    if(client.GetState() != NET_CONNECTED) { s_At = 0; s_Said = false; return; }
    if(!s_At) { s_At = now; return; }
    if(s_Said || now - s_At < 5000 || myId >= MP_MAX_PLAYERS || !players[myId].active) return;
    s_Said = true;
    if(MP_IsReservedName(settings.name) || UI::IsAdminName(players[myId].name)) return;
    if(UI::IsModName(players[myId].name)) { SysMsg(COL_GREEN, "Moderator: you are [MOD] %s here (kick / ban / mute / freeze: MP menu > Players, server settings: /settings).", settings.name); return; }
    if(client.HasModKey())
        SysMsg(COL_YELLOW, s_bModMaster ? "moderator.txt: the MP moderator key was read, but this server didn't give [MOD] (it runs an older MP). Type /modcheck to see why."
                                         : "moderator.txt: this key is not the MP moderator key and this server's owner hasn't listed it - ask for the MP moderator key.");
}
