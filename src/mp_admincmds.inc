// 3.54 (included by mp.cpp): admin / moderator chat commands + what the server tells this phone to do.
// The server checks every permission again (featured servers: [ADMIN] + [MOD], custom servers: the owner).
//   /kick /ban /unban /bans /warn /mute /unmute /sethealth /setarmour /fine /givecash /reportmute
//   /goto /gethere /makevip /makeadmin /resetstats /givecookie /acmds   +   /report for everyone

struct AdmCmdHelp { const char* cmd; const char* args; const char* help; };
static const AdmCmdHelp kAdmCmds[] =
{
    { "/kick",       "[id] [reason]",          "kick a player" },
    { "/ban",        "[id] [reason]",          "ban a player (moderators kick on BCW / MW3 / Mega Vil)" },
    { "/unban",      "[number|name]",          "unban (see /bans)" },
    { "/bans",       "",                       "list the banned players" },
    { "/warn",       "[id] [reason]",          "warn a player (3 warnings = kick)" },
    { "/mute",       "[id]",                   "mute the player's chat (/unmute [id])" },
    { "/sethealth",  "[id] [0-100]",           "set health" },
    { "/setarmour",  "[id] [0-100]",           "set armour" },
    { "/fine",       "[id] [amount] [reason]", "take money from a player" },
    { "/givecash",   "[id] [amount]",          "give money to a player" },
    { "/reportmute", "[id] [minutes]",         "block /report for a player (0 = allow again, default 30)" },
    { "/goto",       "[id]",                   "go to a player" },
    { "/gethere",    "[id]",                   "bring a player to you" },
    { "/makevip",    "[id] [1|0]",             "give / take the [VIP] tag" },
    { "/makeadmin",  "[id] [1|0]",             "give / take moderator rights [MOD]" },
    { "/resetstats", "[id]",                   "reset money, weapons and server stats of a player" },
    { "/givecookie", "[id] [amount]",          "give a player a cookie" },
    { "/pay",        "[id] [amount]",          "everyone: give your own money to a player" },
};

static bool AdmLocalStaff()
{
    if(client.GetState() != NET_CONNECTED) return false;
    return MP::IsLocalAdmin() || MP::IsLocalMod() || MP::IsDefaultAdmin() || MP::IsHosting();
}

static const char* AdmBareName(const char* n)
{
    for(;;)
    {
        if(!strncmp(n, "[ADMIN] ", 8)) { n += 8; continue; }
        if(!strncmp(n, "[MOD] ", 6) || !strncmp(n, "[VIP] ", 6)) { n += 6; continue; }
        return n;
    }
}

// id or (part of a) name -> player id, -1 = nobody, -2 = more than one
static int AdmFindPlayer(const char* tok, bool allowMe)
{
    if(!tok || !*tok) return -1;
    bool digits = true; for(const char* c = tok; *c; ++c) if(!isdigit((unsigned char)*c)) { digits = false; break; }
    if(digits)
    {
        int id = atoi(tok);
        if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active && (allowMe || id != myId)) return id;
        return -1;
    }
    int found = -1, count = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!players[i].active || (!allowMe && i == myId) || i == MP_BOT_ID) continue;
        const char* b = AdmBareName(players[i].name);
        if(!strcasecmp(b, tok) || !strcasecmp(players[i].name, tok)) return i;   // exact name wins
        if(strcasestr(players[i].name, tok)) { found = i; ++count; }
    }
    return count == 1 ? found : (count > 1 ? -2 : -1);
}

static const char* AdmNextTok(const char* s, char* out, size_t n)
{
    while(*s == ' ') ++s;
    size_t o = 0; while(*s && *s != ' ') { if(o + 1 < n) out[o++] = *s; ++s; }
    out[o] = 0;
    while(*s == ' ') ++s;
    return s;
}

static void SendAdminCmd2(uint8_t cmd, int target, uint8_t value, int32_t amount, const char* text)
{
    PktAdminCmd2 c; memset(&c, 0, sizeof(c));
    c.type = PKT_ADMIN_CMD; c.cmd = cmd; c.target = (uint8_t)(target < 0 ? MP_INVALID_ID : target); c.value = value; c.amount = amount;
    MP_CopyStr(c.reason, text ? text : "", sizeof(c.reason));
    client.Send(&c, sizeof(c));
}

static void AdmHelp()
{
    SysMsg(0xC080FFFF, "Staff commands (id = number from the player list, or part of the name):");
    for(const AdmCmdHelp& h : kAdmCmds) SysMsg(0xC080FFFF, "%s %s  -  %s", h.cmd, h.args, h.help);
}

static bool AdmCommand(const char* cmd, const char* arg)
{
    #define IS(x) (!strcmp(cmd, x))
    if(IS("report"))
    {   // everyone: /report [id] [what happened]
        if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "You are not on a server."); return true; }
        char tok[32]; const char* rest = AdmNextTok(arg, tok, sizeof(tok));
        int t = AdmFindPlayer(tok, false);
        if(t < 0 || !*rest) { SysMsg(COL_YELLOW, "/report [id] [what happened]  -  sends it to the [ADMIN] / [MOD] online."); return true; }
        SendAdminCmd2(ADMIN_REPORT, t, 0, 0, rest);
        return true;
    }
    // 4.12.3: /pay [id] [amount] (also /givemoney for players who aren't staff): give your money to a player.
    // Mega Vil keeps its own /pay (server money, 5 m) in mp_rp.inc.
    if((IS("pay") || IS("sendmoney") || IS("transfer") || (IS("givemoney") && !AdmLocalStaff())) && !RpOnServer())
    {
        if(client.GetState() != NET_CONNECTED) { SysMsg(COL_RED, "You are not on a server."); return true; }
        char tk[32]; const char* r2 = AdmNextTok(arg, tk, sizeof(tk));
        char tn[24]; AdmNextTok(r2, tn, sizeof(tn));
        int t = AdmFindPlayer(tk, false);
        long long v = tn[0] ? atoll(tn[0] == '$' ? tn + 1 : tn) : 0;
        if(t == -2) { SysMsg(COL_RED, "More than one player matches \"%s\" - use the id.", tk); return true; }
        if(t < 0 || v <= 0) { if(*tk && t < 0) SysMsg(COL_RED, "No player \"%s\".", tk); SysMsg(COL_YELLOW, "/pay [id] [amount]  -  gives your money to a player ($1 - $1,000,000)."); return true; }
        if(v > 1000000) { SysMsg(COL_RED, "/pay: at most $1,000,000 at a time."); return true; }
        if(!Mw3OnServer() && Game::GetMoney() < v) { SysMsg(COL_RED, "You don't have $%lld (you have $%d).", v, Game::GetMoney()); return true; }
        SendAdminCmd2(ADMIN_PAY, t, 1, (int)v, "");
        return true;
    }
    const bool staffCmd = IS("kick") || IS("ban") || IS("unban") || IS("bans") || IS("warn") || IS("mute") || IS("unmute") ||
                          IS("sethealth") || IS("sethp") || IS("setarmour") || IS("setarmor") || IS("fine") || IS("givecash") ||
                          IS("givemoney") || IS("reportmute") || IS("goto") || IS("gethere") || IS("makevip") || IS("makeadmin") ||
                          IS("makemod") || IS("resetstats") || IS("givecookie") || IS("acmds") || IS("staffcmds") || IS("ahelp");
    if(!staffCmd) return false;
    const bool staff = AdmLocalStaff();
    // /fine (Mega Vil police) and /goto [place] (games) stay what they were for everybody else
    if(!staff && (IS("fine") || IS("goto"))) return false;
    if(!staff)
    {
        if(client.GetState() != NET_CONNECTED) SysMsg(COL_RED, "You are not on a server.");
        else SysMsg(COL_RED, "/%s is for [ADMIN], [MOD] and the server owner.", cmd);
        return true;
    }
    if(IS("acmds") || IS("staffcmds") || IS("ahelp")) { AdmHelp(); return true; }
    if(IS("bans")) { SendAdminCmd2(ADMIN_BANS, -1, 0, 0, ""); return true; }
    if(IS("unban"))
    {
        if(!*arg) { SysMsg(COL_YELLOW, "/unban [number|name]  -  /bans shows the list."); return true; }
        SendAdminCmd2(ADMIN_UNBAN, -1, 0, 0, arg); return true;
    }
    char tok[32]; const char* rest = AdmNextTok(arg, tok, sizeof(tok));
    const bool selfOk = IS("sethealth") || IS("sethp") || IS("setarmour") || IS("setarmor") || IS("givecash") || IS("givemoney") || IS("makevip") || IS("resetstats");
    int t = AdmFindPlayer(tok, selfOk);
    if(IS("goto") && t < 0 && *tok) return false;   // /goto grove_street etc. (mp_games.inc)
    const AdmCmdHelp* h = nullptr;
    for(const AdmCmdHelp& x : kAdmCmds) if(!strcmp(x.cmd + 1, cmd)) { h = &x; break; }
    if(t == -2) { SysMsg(COL_RED, "More than one player matches \"%s\" - use the id.", tok); return true; }
    if(t < 0)
    {
        if(*tok) SysMsg(COL_RED, "No player \"%s\".", tok);
        SysMsg(COL_YELLOW, "Usage: /%s %s", cmd, h ? h->args : "[id]");
        return true;
    }
    char tok2[24]; const char* rest2 = AdmNextTok(rest, tok2, sizeof(tok2));
    const bool hasNum = tok2[0] && (isdigit((unsigned char)tok2[0]) || (tok2[0] == '-' && isdigit((unsigned char)tok2[1])));
    const int num = hasNum ? atoi(tok2) : 0;
    auto needNum = [&](const char* usage) -> bool { if(!hasNum) { SysMsg(COL_YELLOW, "Usage: /%s %s", cmd, usage); return false; } return true; };

    if(IS("kick"))            SendAdminCmd2(ADMIN_KICK, t, 0, 0, rest);
    else if(IS("ban"))        SendAdminCmd2(ADMIN_BAN, t, 0, 0, rest);
    else if(IS("warn"))       SendAdminCmd2(ADMIN_WARN, t, 0, 0, rest);
    else if(IS("mute"))       SendAdminCmd2(ADMIN_MUTE, t, 0, 0, "");
    else if(IS("unmute"))     SendAdminCmd2(ADMIN_UNMUTE, t, 0, 0, "");
    else if(IS("sethealth") || IS("sethp"))   { if(needNum("[id] [0-100]")) SendAdminCmd2(ADMIN_SETHEALTH, t, 0, num, ""); }
    else if(IS("setarmour") || IS("setarmor")) { if(needNum("[id] [0-100]")) SendAdminCmd2(ADMIN_SETARMOUR, t, 0, num, ""); }
    else if(IS("fine"))       { if(needNum("[id] [amount] [reason]")) SendAdminCmd2(ADMIN_FINE, t, 0, num, rest2); }
    else if(IS("givecash") || IS("givemoney")) { if(needNum("[id] [amount]")) SendAdminCmd2(ADMIN_GIVECASH, t, 0, num, ""); }
    else if(IS("reportmute")) SendAdminCmd2(ADMIN_REPORTMUTE, t, 0, hasNum ? num : 30, "");
    else if(IS("goto"))       SendAdminCmd2(ADMIN_GOTO, t, 0, 0, "");
    else if(IS("gethere"))    SendAdminCmd2(ADMIN_GETHERE, t, 0, 0, "");
    else if(IS("makevip"))    SendAdminCmd2(ADMIN_MAKEVIP, t, (uint8_t)(hasNum ? (num != 0) : 1), 0, "");
    else if(IS("makeadmin") || IS("makemod")) SendAdminCmd2(ADMIN_MAKEADMIN, t, (uint8_t)(hasNum ? (num < 0 ? 0 : (num > 9 ? 9 : num)) : 1), 0, "");   // 4.09: 2-9 = CGRP admin level
    else if(IS("resetstats")) SendAdminCmd2(ADMIN_RESETSTATS, t, 0, 0, "");
    else if(IS("givecookie")) SendAdminCmd2(ADMIN_GIVECOOKIE, t, 0, hasNum ? num : 1, "");
    #undef IS
    return true;
}

// ---- what the server asks this phone to do (game thread, next frame) ----
static PktAdminAct s_AdmQ[8]; static int s_nAdmQ = 0;
static int s_nCookies = 0;
static void AdmOnAct(const PktAdminAct& a)
{
    if(s_nAdmQ < 8) { s_AdmQ[s_nAdmQ] = a; s_AdmQ[s_nAdmQ].text[MP_CHAT_LEN - 1] = 0; ++s_nAdmQ; }
}
static void AdmProcess(int ped)
{
    if(!s_nAdmQ) return;
    if(client.GetState() != NET_CONNECTED) { s_nAdmQ = 0; return; }
    if(ped <= 0 || !Game::IsReady()) return;
    for(int i = 0; i < s_nAdmQ; ++i)
    {
        const PktAdminAct& a = s_AdmQ[i];
        switch(a.act)
        {
        case AACT_HEALTH:
        {
            int v = a.amount < 0 ? 0 : (a.amount > 100 ? 100 : a.amount);
            Game::SetCharHealth(ped, v);
            SysMsg(0xC080FFFF, "%s set your health to %d.", a.text, v);
            break;
        }
        case AACT_ARMOUR:
        {
            int v = a.amount < 0 ? 0 : (a.amount > 100 ? 100 : a.amount);
            int cur = Game::GetCharArmour(ped);
            if(v != cur) Game::AddArmour(ped, v - cur);
            SysMsg(0xC080FFFF, "%s set your armour to %d.", a.text, v);
            break;
        }
        case AACT_MONEY:
            Game::AddMoney(a.amount);
            if(a.amount >= 0) SysMsg(COL_GREEN, "%s gave you $%d.", a.text, a.amount);
            else SysMsg(COL_RED, "%s fined you $%d.", a.text, -a.amount);
            break;
        case AACT_PAY:   // 4.12.3: /pay
            if(a.amount < 0)
            {
                int v = -a.amount, have = Game::GetMoney();
                Game::AddMoney(-(v < have ? v : have));
                SysMsg(COL_GREEN, "You gave $%d to %s.", v, a.text);
            }
            else if(a.amount > 0) { Game::AddMoney(a.amount); SysMsg(COL_GREEN, "%s gave you $%d.", a.text, a.amount); }
            break;
        case AACT_TELEPORT:
        {
            if(!std::isfinite(a.pos[0]) || !std::isfinite(a.pos[1]) || !std::isfinite(a.pos[2])) break;
            Vec3 p = { a.pos[0], a.pos[1], a.pos[2] };
            if(Game::IsCharInAnyCar(ped)) Game::WarpOutOfCar(ped, p);
            Game::Teleport(ped, { p.x, p.y, p.z - 1.0f }, a.heading);
            SysMsg(0xC080FFFF, "Teleported to %s.", a.text);
            break;
        }
        case AACT_RESET:
        {
            const int cur = Game::GetMoney(); if(cur) Game::AddMoney(-cur);
            Game::RemoveAllWeapons(ped);
            Game::ClearWanted();
            Game::SetCharHealth(ped, 100);
            { int ar = Game::GetCharArmour(ped); if(ar) Game::AddArmour(ped, -ar); }
            s_nCookies = 0;
            SysMsg(COL_RED, "Your stats were reset by %s.", a.text);
            break;
        }
        case AACT_WARN:
        {
            char by[MP_NAME_LEN] = ""; const char* why = a.text;
            if(const char* bar = strchr(a.text, '|')) { size_t n = std::min((size_t)(bar - a.text), sizeof(by) - 1); memcpy(by, a.text, n); by[n] = 0; why = bar + 1; }
            SysMsg(COL_RED, "WARNING %d/3 from %s: %s", a.amount, by[0] ? by : "the staff", why);
            if(a.amount >= 2) SysMsg(COL_RED, "One more warning and you are kicked.");
            break;
        }
        case AACT_COOKIE:
            s_nCookies = a.amount;
            SysMsg(COL_YELLOW, "%s gave you a cookie. You have %d cookie%s.", a.text, a.amount, a.amount == 1 ? "" : "s");
            break;
        default: break;
        }
    }
    s_nAdmQ = 0;
}
