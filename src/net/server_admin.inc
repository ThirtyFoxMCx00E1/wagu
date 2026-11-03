// 3.54: admin / moderator chat commands (included by server.cpp, m_Mutex held)
//   /kick /ban /unban /warn /sethealth /setarmour /fine /givecash /reportmute /goto /gethere
//   /makevip /makeadmin /resetstats /givecookie  +  /report (every player, to the staff online)
// Who may use them: the 4 featured servers -> [ADMIN] SENJI + moderators; a custom server -> its owner.
// On the default server moderators have the admin permissions (3.50.1). On the other featured servers
// a moderator can't ban / unban / resetstats / makeadmin (ban becomes a kick).
#include <sys/stat.h>

static const uint32_t kAdmCol = 0xC080FFFF, kAdmRed = 0xFF6060FF, kAdmGreen = 0x60E070FF, kAdmYellow = 0xFFD040FF;

void MPServer::RanksLoad()
{
    struct stat st; long stamp = stat("ranks.txt", &st) == 0 ? (long)st.st_mtime : 0;
    if(stamp == m_nRanksStamp) return;
    m_nRanksStamp = stamp; m_Ranks.clear();
    FILE* f = fopen("ranks.txt", "r"); if(!f) return;
    char line[160];
    while(fgets(line, sizeof(line), f))
    {
        char r = 0; char uid[MP_UID_LEN + 2] = "";
        if(line[0] == '#' || sscanf(line, "%c %32s", &r, uid) != 2) continue;
        if((r == 'M' || r == 'V') && uid[0]) m_Ranks[uid] = r;
    }
    fclose(f);
}
void MPServer::RanksSave()
{
    FILE* f = fopen("ranks.txt.tmp", "w"); if(!f) return;
    fprintf(f, "# MP ranks given in game (3.54): M = /makeadmin (moderator), V = /makevip\n");
    for(const auto& kv : m_Ranks) fprintf(f, "%c %s\n", kv.second, kv.first.c_str());
    fclose(f);
    rename("ranks.txt.tmp", "ranks.txt");
    struct stat st; m_nRanksStamp = stat("ranks.txt", &st) == 0 ? (long)st.st_mtime : 0;
}
char MPServer::RankOf(const char* uid)
{
    if(!uid || !uid[0]) return 0;
    RanksLoad();
    auto it = m_Ranks.find(uid);
    return it == m_Ranks.end() ? 0 : it->second;
}

void MPServer::Retag(int slot)
{   // name = [MOD] / [VIP] + the bare name; everyone (the player too) gets the new name
    Slot& s = m_Slots[slot];
    if(s.admin) return;
    char base[MP_NAME_LEN]; MP_CopyStr(base, s.name, sizeof(base));
    for(;;)
    {
        if(!strncmp(base, MP_MOD_TAG, 6)) { memmove(base, base + 6, strlen(base + 6) + 1); continue; }
        if(!strncmp(base, "[VIP] ", 6))   { memmove(base, base + 6, strlen(base + 6) + 1); continue; }
        break;
    }
    char tmp[MP_NAME_LEN + 8];
    if(s.mod)      snprintf(tmp, sizeof(tmp), MP_MOD_TAG "%.17s", base);
    else if(s.vip) snprintf(tmp, sizeof(tmp), "[VIP] %.17s", base);
    else           MP_CopyStr(tmp, base, sizeof(tmp));
    MP_CopyStr(s.name, tmp, MP_NAME_LEN);
    PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
    pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)slot; pj.color = s.color; pj.skin = s.skin;
    MP_CopyStr(pj.name, s.name, MP_NAME_LEN);
    Broadcast(&pj, sizeof(pj), -1);
}

void MPServer::AdminAct(int slot, uint8_t act, int32_t amount, const float* pos, float heading, const char* text)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used) return;
    PktAdminAct a; memset(&a, 0, sizeof(a));
    a.type = PKT_ADMIN_ACT; a.act = act; a.amount = amount; a.heading = heading;
    if(pos) memcpy(a.pos, pos, sizeof(a.pos));
    MP_CopyStr(a.text, text ? text : "", sizeof(a.text));
    AcGrace(slot, 6000);   // health / teleport / money set by the staff: never an anti-cheat hit
    SendTo(slot, &a, sizeof(a));
}

static int AdmFindBan(const std::vector<MPServer::BanEntry>& bans, const char* arg)
{
    if(!arg || !arg[0]) return -1;
    char* e = nullptr; long n = strtol(arg, &e, 10);
    if(e && !*e && n >= 1 && n <= (long)bans.size()) return (int)n - 1;   // number from /bans
    for(size_t i = 0; i < bans.size(); ++i) if(!strcasecmp(bans[i].name, arg)) return (int)i;
    for(size_t i = 0; i < bans.size(); ++i) if(strcasestr(bans[i].name, arg)) return (int)i;
    return -1;
}

void MPServer::AdminCmd(int slot, const uint8_t* data, int len)
{
    if(slot < 0 || len < (int)sizeof(PktAdminCmd)) return;
    PktAdminCmd2 c; memset(&c, 0, sizeof(c));
    memcpy(&c, data, len >= (int)sizeof(PktAdminCmd2) ? sizeof(PktAdminCmd2) : sizeof(PktAdminCmd));
    c.reason[MP_REASON_LEN - 1] = 0;
    Slot& me = m_Slots[slot];
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = kAdmCol;
    auto tell = [&](uint32_t col, const char* fmt, ...) {
        PktServerMsg r; memset(&r, 0, sizeof(r)); r.type = PKT_SERVER_MSG; r.color = col;
        va_list ap; va_start(ap, fmt); vsnprintf(r.text, sizeof(r.text), fmt, ap); va_end(ap);
        SendTo(slot, &r, sizeof(r));
    };
    auto all = [&](const char* fmt, ...) {
        va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
        Log("%s", m.text); Broadcast(&m, sizeof(m), -1);
    };

    // ---- /report: every player -> the staff online ----
    if(c.cmd == ADMIN_REPORT)
    {
        const uint64_t now = MP_NowMs();
        if(me.reportMuteUntil && now < me.reportMuteUntil) { tell(kAdmRed, "You can't use /report right now (report-muted by the staff, %d min left).", (int)((me.reportMuteUntil - now) / 60000 + 1)); return; }
        if(me.lastReport && now - me.lastReport < 30000) { tell(kAdmRed, "Wait %d s before the next /report.", (int)(30 - (now - me.lastReport) / 1000)); return; }
        me.lastReport = now;
        const int t = c.target;
        const char* tn = (t < MP_MAX_PLAYERS && m_Slots[t].used) ? m_Slots[t].name : "-";
        PktServerMsg r; memset(&r, 0, sizeof(r)); r.type = PKT_SERVER_MSG; r.color = kAdmYellow;
        snprintf(r.text, sizeof(r.text), "[REPORT] %s (%d) about %s (%d): %.60s", me.name, slot, tn, t < MP_MAX_PLAYERS ? t : -1, c.reason);
        Log("%s", r.text);
        int staff = 0;
        const bool featured = IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer();
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && (m_Slots[i].admin || m_Slots[i].mod || (!featured && m_Slots[i].owner))) { SendTo(i, &r, sizeof(r)); ++staff; }
        tell(kAdmGreen, staff ? "Your report was sent to %d staff member%s online." : "Your report was saved - no staff online right now.", staff, staff == 1 ? "" : "s");
        return;
    }

    const bool featured = IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer();
    const bool staff = featured ? (me.admin || me.mod) : (me.owner || me.admin || (me.mod && me.granted));   // custom: the owner + who he made [MOD]
    if(!staff)
    {
        tell(kAdmRed, featured ? "Only [ADMIN] and [MOD] can use this command here." : "Only the owner of this server can use this command.");
        return;
    }
    const bool full = me.admin || !featured || IsDefaultServer();   // admin powers
    const bool boss = me.admin || (!featured && me.owner);   // the server owner always wins
    const bool rankGiver = me.admin || (!featured && me.owner) || (IsDefaultServer() && me.mod && !me.granted);
    char who[MP_NAME_LEN]; MP_CopyStr(who, me.name, sizeof(who));
    const char* why = c.reason[0] ? c.reason : nullptr;

    if(c.cmd == ADMIN_CHEATS)
    {
        uint8_t np = c.value ? (m_nPerms | PERM_NO_CHEATS) : (m_nPerms & ~PERM_NO_CHEATS);
        SetPermsLocked(np);
        all("%s %s cheats for everyone.", who, c.value ? "disabled" : "enabled");
        return;
    }
    if(c.cmd == ADMIN_BANS)
    {
        if(m_Bans.empty()) { tell(kAdmCol, "Nobody is banned on this server."); return; }
        int from = (int)m_Bans.size() > 8 ? (int)m_Bans.size() - 8 : 0;
        tell(kAdmCol, "Banned players (%d, newest last) - /unban [number or name]:", (int)m_Bans.size());
        for(int i = from; i < (int)m_Bans.size(); ++i) tell(kAdmCol, "  %d. %s  (by %s: %.40s)", i + 1, m_Bans[i].name, m_Bans[i].admin, m_Bans[i].reason);
        return;
    }
    if(c.cmd == ADMIN_UNBAN)
    {
        // 3.58: [ADMIN] and [MOD] can both unban (anti-cheat bans too)
        int i = AdmFindBan(m_Bans, c.reason);
        if(i < 0) { tell(kAdmRed, "No ban found for \"%s\". /bans shows the list.", c.reason); return; }
        char nm[MP_NAME_LEN]; MP_CopyStr(nm, m_Bans[i].name, sizeof(nm));
        m_Bans.erase(m_Bans.begin() + i); SaveBans();
        all("%s was unbanned by %s.", nm, who);
        return;
    }

    if(c.cmd == ADMIN_BANNAME)
    {   // 3.58: the launcher's Bans page on a remote / 24/7 server: ban a name, kicks them if online
        char nm[MP_NAME_LEN] = ""; const char* rs = "";
        char* tab = strchr(c.reason, '\t'); if(tab) { *tab = 0; rs = tab + 1; }
        MP_CopyStr(nm, c.reason, sizeof(nm));
        if(!nm[0]) { tell(kAdmRed, "Type a player name."); return; }
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && !strcasecmp(m_Slots[i].name, nm) && (m_Slots[i].admin || (m_Slots[i].mod && !me.admin)))
            { tell(kAdmRed, "You can't ban another staff member."); return; }
        if(BanNameLocked(nm, "", who, rs[0] ? rs : "Banned")) tell(kAdmGreen, "%s is banned now (permanent, until /unban).", nm);
        else tell(kAdmYellow, "%s is already banned.", nm);
        return;
    }

    const int t = c.target;
    if(t >= MP_MAX_PLAYERS || !m_Slots[t].used) { tell(kAdmRed, "No player with that id."); return; }
    Slot& ts = m_Slots[t];
    const bool tStaff = ts.admin || ts.mod;
    const bool selfOk = c.cmd == ADMIN_SETHEALTH || c.cmd == ADMIN_SETARMOUR || c.cmd == ADMIN_GIVECASH || c.cmd == ADMIN_MAKEVIP || c.cmd == ADMIN_RESETSTATS;
    if(t == slot && !selfOk) { tell(kAdmRed, "You can't use that on yourself."); return; }
    const bool harmful = c.cmd == ADMIN_KICK || c.cmd == ADMIN_BAN || c.cmd == ADMIN_MUTE || c.cmd == ADMIN_WARN || c.cmd == ADMIN_FINE ||
                         c.cmd == ADMIN_REPORTMUTE || c.cmd == ADMIN_RESETSTATS || c.cmd == ADMIN_MAKEADMIN || c.cmd == ADMIN_GETHERE ||
                         (c.cmd == ADMIN_SETHEALTH && c.amount < 50) || (c.cmd == ADMIN_SETARMOUR && c.amount < 1);
    if(t != slot && !boss && tStaff && harmful) { tell(kAdmRed, "You can't use that on another staff member."); return; }
    if(ts.admin && t != slot && !me.admin && c.cmd != ADMIN_GOTO && c.cmd != ADMIN_GIVECOOKIE) { tell(kAdmRed, "You can't use that on the administrator."); return; }

    switch(c.cmd)
    {
    case ADMIN_BAN:
        KickLocked(t, who, why ? why : "Banned", true); return;   // 3.58: [MOD] bans for real too (was a kick: "the ban didn't work")
    case ADMIN_KICK:
        KickLocked(t, who, why ? why : "Kicked", false); return;
    case ADMIN_MUTE: case ADMIN_UNMUTE:
        ts.muted = c.cmd == ADMIN_MUTE;
        all("%s was %s by %s", ts.name, ts.muted ? "muted" : "unmuted", who);
        return;
    case ADMIN_WARN:
    {
        if(ts.warns < 255) ++ts.warns;
        all("%s got a warning from %s (%d/3): %s", ts.name, who, ts.warns, why ? why : "Follow the server rules");
        char txt[MP_CHAT_LEN]; snprintf(txt, sizeof(txt), "%s|%.80s", who, why ? why : "Follow the server rules");
        AdminAct(t, AACT_WARN, ts.warns, nullptr, 0, txt);
        if(ts.warns >= 3) { ts.warns = 0; KickLocked(t, who, "3 warnings", false); }
        return;
    }
    case ADMIN_SETHEALTH: case ADMIN_SETARMOUR:
    {
        int v = c.amount < 0 ? 0 : (c.amount > 100 ? 100 : c.amount);
        const bool hp = c.cmd == ADMIN_SETHEALTH;
        AdminAct(t, hp ? AACT_HEALTH : AACT_ARMOUR, v, nullptr, 0, who);
        tell(kAdmGreen, "%s of %s set to %d.", hp ? "Health" : "Armour", ts.name, v);
        Log("%s set the %s of %s to %d", who, hp ? "health" : "armour", ts.name, v);
        return;
    }
    case ADMIN_FINE: case ADMIN_GIVECASH:
    {
        int32_t v = c.amount;
        if(v <= 0 || v > 10000000) { tell(kAdmRed, "Amount: 1 - 10,000,000."); return; }
        const bool give = c.cmd == ADMIN_GIVECASH;
        if(IsRpServer() && m_Rp[t].loaded)
        {   // server money on Mega Vil: cash first, the rest from the bank
            RpSrvPlayer& r = m_Rp[t];
            if(give) RpPay(t, v, "From the staff");
            else
            {
                int32_t fromCash = r.cash >= v ? v : (r.cash > 0 ? r.cash : 0);
                if(fromCash) RpPay(t, -fromCash, "Staff fine");
                int32_t rest = v - fromCash; if(rest > 0) { r.bank -= rest; if(r.bank < 0) r.bank = 0; m_bRpDirty = true; }
            }
            RpSendMe(t);
        }
        else if(IsMw3Server())
        {
            Mw3SrvPlayer& w = m_Mw3[t];
            w.cash += give ? v : -v; if(w.cash < 0) w.cash = 0;
            Mw3Store(t); Mw3SendMe(t);
        }
        else AdminAct(t, AACT_MONEY, give ? v : -v, nullptr, 0, who);   // the game's own money (default / BCW / custom servers)
        if(give) { all("%s gave %s $%d.", who, ts.name, v); }
        else     { all("%s was fined $%d by %s: %s", ts.name, v, who, why ? why : "Breaking the rules"); }
        return;
    }
    case ADMIN_REPORTMUTE:
    {
        int min = c.amount; if(min < 0) min = 0; if(min > 1440) min = 1440;
        if(min == 0) { ts.reportMuteUntil = 0; tell(kAdmGreen, "%s can use /report again.", ts.name); Log("%s report-unmuted %s", who, ts.name); return; }
        ts.reportMuteUntil = MP_NowMs() + (uint64_t)min * 60000;
        all("%s can't use /report for %d min (by %s).", ts.name, min, who);
        return;
    }
    case ADMIN_GOTO:
    {
        if(!ts.hasPos) { tell(kAdmRed, "%s has no position yet.", ts.name); return; }
        float p[3] = { ts.pos[0] + 1.5f, ts.pos[1] + 1.0f, ts.pos[2] };
        AdminAct(slot, AACT_TELEPORT, 0, p, 0, ts.name);
        Log("%s went to %s", who, ts.name);
        return;
    }
    case ADMIN_GETHERE:
    {
        if(!me.hasPos) { tell(kAdmRed, "Your position isn't known yet - move a bit."); return; }
        float p[3] = { me.pos[0] + 1.5f, me.pos[1] + 1.0f, me.pos[2] };
        AdminAct(t, AACT_TELEPORT, 0, p, 0, who);
        tell(kAdmGreen, "%s was brought to you.", ts.name);
        Log("%s brought %s", who, ts.name);
        return;
    }
    case ADMIN_MAKEVIP:
    {
        bool on = c.value != 0;
        if(ts.admin) { tell(kAdmRed, "The administrator has the [ADMIN] tag already."); return; }
        ts.vip = on;
        if(ts.uid[0]) { if(on) { if(RankOf(ts.uid) != 'M') m_Ranks[ts.uid] = 'V'; } else if(RankOf(ts.uid) == 'V') m_Ranks.erase(ts.uid); RanksSave(); }
        if(!ts.mod) Retag(t);
        all(on ? "%s is now a [VIP] (by %s)." : "%s is no longer a VIP (by %s).", ts.name, who);
        return;
    }
    case ADMIN_MAKEADMIN:
    {
        if(!rankGiver) { tell(kAdmRed, "Only the administrator (or a moderator.txt moderator on the default server) can give ranks."); return; }
        bool on = c.value != 0;
        if(ts.admin) { tell(kAdmRed, "%s is the administrator.", ts.name); return; }
        if(!on && ts.mod && !ts.granted) { tell(kAdmRed, "%s is a moderator from moderator.txt - only that file can take it away.", ts.name); return; }
        if(on && ts.mod) { tell(kAdmRed, "%s is a moderator already.", ts.name); return; }
        ts.mod = on; ts.granted = on;
        if(ts.uid[0]) { if(on) m_Ranks[ts.uid] = 'M'; else if(ts.vip) m_Ranks[ts.uid] = 'V'; else m_Ranks.erase(ts.uid); RanksSave(); }
        Retag(t);
        all(on ? "%s is now a moderator [MOD] (by %s)." : "%s is no longer a moderator (by %s).", ts.name, who);
        return;
    }
    case ADMIN_RESETSTATS:
    {
        if(!full) { tell(kAdmRed, "Only the administrator can reset stats on this server."); return; }
        if(IsRpServer() && m_Rp[t].loaded) { RpSrvPlayer& r = m_Rp[t]; r.cash = 0; r.bank = 0; r.xp = 0; r.level = 1; r.wanted = 0; m_bRpDirty = true; RpSendMe(t); }
        if(IsMw3Server()) { Mw3SrvPlayer& w = m_Mw3[t]; w.score = w.xp = w.cash = 0; w.kills = w.deaths = w.caps = w.deagle = w.spree = w.best = 0; w.rank = 0; Mw3Store(t); Mw3SendMe(t); }
        { char key[HK_OWNER_LEN]; HomeKey(t, key, sizeof(key)); if(m_Prog.erase(key)) { m_bProgDirty = true; m_nProgSaveAt = MP_NowMs() + 2000; } }
        ts.warns = 0; ts.cookies = 0;
        AdminAct(t, AACT_RESET, 0, nullptr, 0, who);
        all("The stats of %s were reset by %s.", ts.name, who);
        return;
    }
    case ADMIN_GIVECOOKIE:
    {
        int n = c.amount < 1 ? 1 : (c.amount > 100 ? 100 : c.amount);
        ts.cookies = (uint16_t)std::min(60000, ts.cookies + n);
        AdminAct(t, AACT_COOKIE, ts.cookies, nullptr, 0, who);
        all(n == 1 ? "%s gave %s a cookie (%d in total)." : "%s gave %s cookies (%d in total).", who, ts.name, (int)ts.cookies);
        return;
    }
    default: return;
    }
}
