// 3.39: the RULES window after the login (SA-MP dialog look: dark see-through box, white title,
// coloured highlights, a black "Close" button with white text and no outline).
// Default server: 10 built-in rules, title "RULES". Custom servers: the host's title + 1 ) - 10 )
// from Add server > Rules (or nothing when the host turned the rules off). Once per join; /rules again.

static const char* const kDefaultRules[10] = {
    "{FF4444}No cheating{FFFFFF}: no hacks, aimbots, mod menus or speed / fly mods.",
    "{FF4444}No NSFW{FFFFFF}: no adult names, chat, skins or pictures.",
    "{FFFF00}Respect everyone{FFFFFF}: no insults, racism, threats or harassment.",
    "{FFFF00}No spam{FFFFFF}: no flooding the chat, caps lock or advertising other servers.",
    "{FFFF00}No bug abuse{FFFFFF}: report glitches to an {33CC55}admin{FFFFFF}, don't use them.",
    "{FFFF00}No spawn killing{FFFFFF} or griefing other players at spawns and safe zones.",
    "{FFFF00}Don't pretend to be staff{FFFFFF} (admin, moderator or the bot {00BFFF}Broker{FFFFFF}).",
    "{FFFF00}Keep the chat friendly{FFFFFF} and in a language the others understand.",
    "{FFFF00}Listen to the {33CC55}admins{FFFF00} and {33CC55}moderators{FFFFFF}: a kick waits {FF9900}4:29{FFFFFF}, a ban is {FF4444}permanent{FFFFFF}.",
    "{33CC55}Have fun{FFFFFF} and play fair.",
};
static struct { bool have = false, on = false; char title[48] = ""; char line[10][100] = {}; int count = 0; bool shown = false; uint64_t readyAt = 0; } s_Rules;

static void RulesReset() { s_Rules.have = false; s_Rules.on = false; s_Rules.count = 0; s_Rules.shown = false; s_Rules.readyAt = 0; }
static void RulesOnPacket(const PktRules& m)
{
    // idx 0 = the title (on = 0: no rules window), 1..10 = the lines
    if(m.idx == 0) { s_Rules.have = true; s_Rules.on = m.on != 0; MP_CopyStr(s_Rules.title, m.text, sizeof(s_Rules.title)); s_Rules.count = std::min((int)m.count, 10); for(auto& l : s_Rules.line) l[0] = 0; return; }
    if(m.idx >= 1 && m.idx <= 10) MP_CopyStr(s_Rules.line[m.idx - 1], m.text, sizeof(s_Rules.line[0]));
}
static void RulesDlg(int, int, const char*, int) {}
static bool RulesShow(bool quiet)
{
    std::string body; const char* title = "RULES";
    if(MP::IsOnDefault())
        for(int i = 0; i < 10; ++i) { char l[200]; snprintf(l, sizeof(l), "{FFFF00}%d ) {FFFFFF}%s\n", i + 1, kDefaultRules[i]); body += l; }
    else if(s_Rules.have && s_Rules.on)
    {
        title = s_Rules.title[0] ? s_Rules.title : "SERVER RULES";
        int n = 0;
        for(int i = 0; i < 10; ++i) { if(!s_Rules.line[i][0]) continue; char l[160]; snprintf(l, sizeof(l), "{FFFF00}%d ) {FFFFFF}%s\n", ++n, s_Rules.line[i]); body += l; }
        if(!n) { if(!quiet) SysMsg(COL_SYSTEM, "This server has no rules set."); return false; }
    }
    else { if(!quiet) SysMsg(COL_SYSTEM, "This server has no rules set."); return false; }
    char cap[64]; snprintf(cap, sizeof(cap), "{FFFFFF}%s", title);
    ShowDialog(0, DIALOG_STYLE_MSGBOX, cap, body.c_str(), "Close", "", RulesDlg);
    return true;
}
// after the login is done (or the join settled when the server has no login)
static void RulesProcess(uint64_t now)
{
    if(client.GetState() != NET_CONNECTED) { if(s_Rules.shown || s_Rules.have) RulesReset(); return; }
    if(s_Rules.shown || BcwOnServer() || Mw3OnServer() || RpOnServer() || !Game::IsReady()) return;
    if(!MP::IsOnDefault() && !(s_Rules.have && s_Rules.on)) return;
    const bool authDone = !s_bAuthReq || s_Auth.state == AuthClient::DONE;
    if(!authDone || AuthScreenUp() || SampDialogOpen() || VKeyboard::IsOpen()) { s_Rules.readyAt = 0; return; }
    if(!s_Rules.readyAt) { s_Rules.readyAt = now + 1800; return; }
    if(now < s_Rules.readyAt) return;
    s_Rules.shown = true;
    RulesShow(true);
}
static bool RulesCommand(const char* cmd, const char*)
{
    if(strcmp(cmd, "rules") || RpOnServer() || BcwOnServer() || Mw3OnServer()) return false;
    RulesShow(false);
    return true;
}
