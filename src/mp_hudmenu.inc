// 4.07: /settings - "Settings Menu" (like the SA-MP RP servers): only the host owner of the server
// (a custom server: the phone / VPS that hosts it; the featured servers: [ADMIN] / [MOD]).
//   Settings Menu         Account, Textdraw >>, Entering Door, Server Logo, Server settings >>, Rules >>
//   Settings Menu >> Textdraw   HUD Mode, Dollar Cent, HUD Player, Ammo Info, Logo Server
//   Hud Mode              HUD Variant - 1..5, Disable
// Every change goes to the server (PktSrvSettings2) and from there to everyone on it.

enum { HUDM_MAIN = 0x4D31, HUDM_TD = 0x4D32, HUDM_MODE = 0x4D33 };

static bool HudMenuIsOwner()
{
    if(client.GetState() != NET_CONNECTED) return false;
    if(s_SrvSet.have && s_SrvSet.s.can) return true;
    const bool featured = MP::IsOnDefault() || BcwOnServer() || Mw3OnServer() || RpOnServer() || DzOnServer();
    if(featured) return MP::IsLocalMod() || MP::IsLocalAdmin() || MP::IsDefaultAdmin();
    return s_Server.IsRunning() && !MP::IsHostingDefault() && client.GetPort() == s_Server.GetPort();
}
static const char* HudOnOff(bool on) { return on ? "{33CC55}Enabled" : "{FF4444}Disabled"; }

static void HudMenuSend()
{
    if(client.GetServerBuild() < 40700)
    {
        char b[16]; unsigned sb = client.GetServerBuild();
        snprintf(b, sizeof(b), "%u.%02u", sb / 10000, (sb / 100) % 100);
        SysMsg(COL_YELLOW, "This server runs MP %s: the HUD settings work once it runs 4.07 (only you see them until then).", sb ? b : "(old)");
        return;
    }
    PktSrvSettings2 p; memset(&p, 0, sizeof(p));
    p.b = s_SrvSet.s; p.b.type = PKT_SRV_SETTINGS; p.b.op = 1;
    if(!s_SrvSet.have) { p.b.modes = s_nServerModes; p.b.perms = s_nServerPerms; p.b.rulesOn = MP::IsOnDefault() ? 1 : 0; }
    p.hudMagic = HUD_MAGIC; p.hudMode = s_Hud.mode; p.hudFlags = (uint8_t)(s_Hud.flags & ~HUDF_PLAYER); p.doorSecs = s_Hud.door;
    p.logoX = s_Hud.lx; p.logoY = s_Hud.ly; p.logoZ = s_Hud.lz; p.logoValid = 1;
    client.Send(&p, sizeof(p));
}

static void HudModeShow();
static void HudTdShow();
static void HudLogoPosAsk(int which);
static void HudLogoPosDlg(int resp, int, const char* input, int which)
{
    if(resp && input && *input)
    {
        int v = atoi(input);
        if(which == 2) { if(v < 10) v = 10; if(v > 250) v = 250; s_Hud.lz = (uint8_t)v; }
        else { if(v < 0) v = 0; if(v > 100) v = 100; if(which == 0) s_Hud.lx = (uint8_t)v; else s_Hud.ly = (uint8_t)v; }
        HudMenuSend();
    }
    HudTdShow();
}
void HudLogoPosAsk(int which)
{
    static const char* const k[3] = { "Logo Position X: 0 = left, 50 = centre, 100 = right (now %d)", "Logo Position Y: 0 = top, 100 = bottom (now %d)", "Logo Size Z: 10 - 250 %%, 100 = normal (now %d)" };
    const int now = which == 0 ? s_Hud.lx : which == 1 ? s_Hud.ly : s_Hud.lz;
    char b[160]; snprintf(b, sizeof(b), k[which], now);
    ShowDialog(0, DIALOG_STYLE_INPUT, "{FFFFFF}Server Logo Position", b, "Set", "Back", HudLogoPosDlg, which);
}

static void HudMainDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0) return;
    switch(item)
    {
    case 0: SysMsg(COL_SYSTEM, "Account: %s (id %d) on %s", settings.name, (int)myId, client.GetServerName()); HudMenuShow(); break;
    case 1: HudTdShow(); break;
    case 2: s_Hud.door = (uint8_t)((s_Hud.door + 1) % 6); HudMenuSend(); HudMenuShow(); break;
    case 3:
        SysMsg(COL_SYSTEM, "Server Logo: MP Launcher > + Add server > scroll down > Import title logo (opens Files) - a PNG (transparent) or JPG. Hide / show it in Textdraw > Logo Server.");
        HudMenuShow(); break;
    case 4: SrvSettingsShow(); break;
    case 5: if(!RulesShow(false)) SysMsg(COL_YELLOW, "This server has no rules window (Server settings > Rules window)."); break;
    default: break;
    }
}
void HudMenuShow()
{
    if(!HudMenuIsOwner()) { SysMsg(COL_RED, "Only the host owner can open /settings."); return; }
    char b[1400];
    const bool lg = s_Server.IsRunning() && client.GetPort() == s_Server.GetPort() ? settings.serverLogo[0] != 0 : HudLogoFile()[0] != 0;
    snprintf(b, sizeof(b),
        "Settings\tStatus\n"
        "{FFFFFF}Account\t{AAAAAA}%s\n"
        "{FFFFFF}Textdraw\t{FFD700}>>\n"
        "{FFFFFF}Entering Door\t{FFFFFF}%d second(s)\n"
        "{FFFFFF}Server Logo\t%s\n"
        "{FFFFFF}Server settings (gamemodes / permissions)\t{FFD700}>>\n"
        "{FFFFFF}Server rules\t{FFD700}>>\n",
        settings.name, (int)s_Hud.door, lg ? "{33CC55}Uploaded" : "{AAAAAA}Not set >>");
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, "{FFFFFF}Settings Menu", b, "Select", "Close", HudMainDlg, HUDM_MAIN);
}

static void HudTdDlg(int resp, int item, const char*, int)
{
    if(!resp) { HudMenuShow(); return; }
    if(item < 0) return;
    if(item == 0) { HudModeShow(); return; }
    static const uint8_t bits[] = { HUDF_CENTS, HUDF_PLAYER, HUDF_AMMO, HUDF_LOGO };
    if(item == 2) { s_Hud.flags &= ~HUDF_PLAYER; SysMsg(COL_SYSTEM, "HUD Player was removed in 4.12.5 - your name and id are no longer drawn on the HUD."); HudTdShow(); return; }
    if(item >= 1 && item <= 4) { s_Hud.flags ^= bits[item - 1]; s_Hud.flags &= ~HUDF_PLAYER; HudMenuSend(); }
    if(item >= 5 && item <= 7) { HudLogoPosAsk(item - 5); return; }
    HudTdShow();
}
void HudTdShow()
{
    char mode[48];
    if(s_Hud.mode == 0) snprintf(mode, sizeof(mode), "{FF4444}Disabled");
    else snprintf(mode, sizeof(mode), "{FFFFFF}HUD Variant - %d", (int)s_Hud.mode);
    char b[900];
    snprintf(b, sizeof(b),
        "Settings\tStatus\n"
        "{FFFFFF}HUD Mode\t%s\n"
        "{FFFFFF}Dollar Cent\t%s\n"
        "{FFFFFF}HUD Player\t%s\n"
        "{FFFFFF}Ammo Info\t%s\n"
        "{FFFFFF}Logo Server\t%s\n"
        "{FFFFFF}Logo Position X\t{FFFFFF}%d%%\n"
        "{FFFFFF}Logo Position Y\t{FFFFFF}%d%%\n"
        "{FFFFFF}Logo Size Z\t{FFFFFF}%d%%\n",
        mode, HudOnOff(HudFlag(HUDF_CENTS)), "{AAAAAA}Removed", HudOnOff(HudFlag(HUDF_AMMO)), HudOnOff(HudFlag(HUDF_LOGO)), (int)s_Hud.lx, (int)s_Hud.ly, (int)s_Hud.lz);
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, "{FFFFFF}Settings Menu >> Textdraw", b, "Select", "Back", HudTdDlg, HUDM_TD);
}

static void HudModeDlg(int resp, int item, const char*, int)
{
    if(!resp || item < 0) { HudTdShow(); return; }
    s_Hud.mode = item < 5 ? (uint8_t)(item + 1) : 0;
    HudMenuSend();
    SysMsg(COL_SYSTEM, s_Hud.mode ? "HUD Variant %d (%s) for everyone on this server." : "The server HUD is off for everyone on this server.", (int)s_Hud.mode, HudPalette().name);
    HudTdShow();
}
void HudModeShow()
{
    std::string b;
    for(int i = 1; i <= 5; ++i)
    {
        char l[96]; snprintf(l, sizeof(l), "%sHUD Variant - %d {AAAAAA}(%s)\n", s_Hud.mode == i ? "{33CC55}" : "{FFFFFF}", i, kHudPal[i].name); b += l;
    }
    b += s_Hud.mode == 0 ? "{33CC55}Disable\n" : "{FF4444}Disable\n";
    ShowDialog(0, DIALOG_STYLE_LIST, "{FFFFFF}Hud Mode", b.c_str(), "Change", "Back", HudModeDlg, HUDM_MODE);
}

void HudMenuOpen()
{
    if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "Join a server first."); return; }
    if(HudMenuIsOwner()) { HudMenuShow(); return; }
    // not sure yet (the server's answer about who may manage it didn't come): ask, open when it answers
    if(!s_SrvSet.have && client.GetServerBuild() >= 33900)
    {
        PktSrvSettings q; memset(&q, 0, sizeof(q)); q.type = PKT_SRV_SETTINGS; q.op = 0; client.Send(&q, sizeof(q));
        s_bHudMenuPending = true;
        return;
    }
    SysMsg(COL_RED, "Only the host owner can open /settings.");
}

// 4.07: + Add server > Title logo X / Y / Z changed: save, use it here at once and (my own server) tell everyone
static void HudLogoPosChanged()
{
    SaveConfig();
    const bool mine = s_Server.IsRunning() && !MP::IsHostingDefault();
    if(mine) s_Server.SetLogoPos((uint8_t)settings.logoX, (uint8_t)settings.logoY, (uint8_t)settings.logoZ);
    if(mine && client.GetState() == NET_CONNECTED && client.GetPort() == s_Server.GetPort())
    {
        s_Hud.lx = (uint8_t)settings.logoX; s_Hud.ly = (uint8_t)settings.logoY; s_Hud.lz = (uint8_t)settings.logoZ;
        HudMenuSend();
    }
}
