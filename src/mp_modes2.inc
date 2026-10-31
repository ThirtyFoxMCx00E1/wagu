// 3.39: five more gamemodes, after the open.mp forum gamemodes (forum.open.mp, Gamemodes board):
//   GM_SURVIVAL  Sunset DayZ style: thirst, loot crates around you, zombies at night         /survival
//   GM_CNR       Project Open CnR style: cops vs robbers, /rob /cuff /arrest, wanted, hits    /cop /robber
//   GM_AD        League A/D style: attackers take the base checkpoint, defenders hold it        /ad
//   GM_STUNT     xSF / Magic Freeroam style: /ramp /jump /chiliad /lv /sf /ls /n /fix, stunt bonus
//   GM_DRIFT     RogueDrifterZ style: drift points + combo, cash, /drifttop
// (/fix /flip /lv /sf /ls /chiliad are the /spawn games' ones.) On by themselves on the default server; on custom servers when the host turns them on (Gamemodes).
// The players' clients talk with PKT_GM_EVENT (the server passes it on).

static bool GmOn(uint16_t gm) { return client.GetState() == NET_CONNECTED && Game::IsReady() && !BcwOnServer() && !Mw3OnServer() && !RpOnServer() && MP::ModeOn(gm); }
static void GmSend(uint8_t op, uint16_t mode, int to, int a = 0, int b = 0, int val = 0, const char* text = "")
{
    PktGmEvent e; memset(&e, 0, sizeof(e));
    e.type = PKT_GM_EVENT; e.op = op; e.to = (uint8_t)to; e.mode = mode; e.a = (int16_t)a; e.b = (int16_t)b; e.val = val;
    MP_CopyStr(e.text, text ? text : "", sizeof(e.text));
    client.Send(&e, sizeof(e));
}
static const char* GmName(int id) { return (id >= 0 && id < MP_MAX_PLAYERS && players[id].active) ? players[id].name : "someone"; }
static float GmDistTo(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active || players[id].ped <= 0) return 1e9f;
    Vec3 p; if(!Game::GetCharPos(players[id].ped, p)) return 1e9f;
    return sqrtf((p.x - myDrawPos.x) * (p.x - myDrawPos.x) + (p.y - myDrawPos.y) * (p.y - myDrawPos.y) + (p.z - myDrawPos.z) * (p.z - myDrawPos.z));
}
static int GmArgId(const char* a) { if(!a || !*a || !isdigit((unsigned char)*a)) return -1; int id = atoi(a); return (id >= 0 && id < MP_MAX_PLAYERS && players[id].active && id != (int)myId) ? id : -1; }
static void GmHudLine(ImDrawList* dl, float y, ImU32 col, const char* t)
{
    ImFont* f = UI::PricedownFont() ? UI::PricedownFont() : UI::Font();
    const float fs = UI::S(f == UI::Font() ? 26.0f : 30.0f);
    ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t);
    ImVec2 p(ImGui::GetIO().DisplaySize.x - ts.x - UI::S(20), y);
    for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) dl->AddText(f, fs, ImVec2(p.x + ox * 2, p.y + oy * 2), IM_COL32(0, 0, 0, 255), t);
    dl->AddText(f, fs, p, col, t);
}

// ================================ Cops and Robbers ==============================================
static struct { int team = 0; int wanted = 0; uint64_t jailUntil = 0, cuffUntil = 0; int teams[MP_MAX_PLAYERS] = {}; int wantedOf[MP_MAX_PLAYERS] = {}; int hit[MP_MAX_PLAYERS] = {}; uint64_t robAt = 0; } s_Cnr;
static void CnrSetWanted(int w) { s_Cnr.wanted = std::max(0, std::min(6, w)); GmSend(GMO_WANTED, GM_CNR, 0xFF, s_Cnr.wanted); }
static bool CnrCommand(const char* cmd, const char* arg)
{
    if(!GmOn(GM_CNR)) return false;
    const int ped = Game::LocalPed();
    if(!strcmp(cmd, "cop") || !strcmp(cmd, "robber") || !strcmp(cmd, "civilian"))
    {
        s_Cnr.team = !strcmp(cmd, "cop") ? 1 : (!strcmp(cmd, "robber") ? 2 : 0);
        if(s_Cnr.team == 1) { if(s_Cnr.wanted) { SysMsg(COL_RED, "Wanted players can't join the police."); s_Cnr.team = 2; } else { Game::GiveAndSelectWeapon(ped, 3); Game::GiveAndSelectWeapon(ped, 22); } }
        GmSend(GMO_TEAM, GM_CNR, 0xFF, s_Cnr.team);
        SysMsg(COL_YELLOW, s_Cnr.team == 1 ? "You are a COP: /cuff <id> and /arrest <id> wanted players ($500 a star)." :
                          (s_Cnr.team == 2 ? "You are a ROBBER: /rob <id> players for cash (it makes you wanted)." : "You are a civilian."));
        return true;
    }
    if(!strcmp(cmd, "rob"))
    {
        int id = GmArgId(arg);
        if(s_Cnr.team != 2) { SysMsg(COL_RED, "Only robbers can rob (/robber)."); return true; }
        if(id < 0 || GmDistTo(id) > 4.0f) { SysMsg(COL_RED, "Use: /rob <id> standing next to them."); return true; }
        if(MP_NowMs() - s_Cnr.robAt < 60000) { SysMsg(COL_RED, "Wait a minute before the next robbery."); return true; }
        s_Cnr.robAt = MP_NowMs();
        GmSend(GMO_ROB, GM_CNR, id); CnrSetWanted(s_Cnr.wanted + 1);
        SysMsg(COL_YELLOW, "You rob %s... (wanted %d)", GmName(id), s_Cnr.wanted);
        return true;
    }
    if(!strcmp(cmd, "cuff") || !strcmp(cmd, "arrest"))
    {
        int id = GmArgId(arg);
        if(s_Cnr.team != 1) { SysMsg(COL_RED, "Only cops (/cop)."); return true; }
        if(id < 0 || GmDistTo(id) > 4.0f) { SysMsg(COL_RED, "Use: /%s <id> standing next to them.", cmd); return true; }
        if(!s_Cnr.wantedOf[id]) { SysMsg(COL_RED, "%s is not wanted.", GmName(id)); return true; }
        GmSend(!strcmp(cmd, "cuff") ? GMO_CUFF : GMO_ARREST, GM_CNR, id);
        if(!strcmp(cmd, "arrest")) { Game::AddMoney(500 * s_Cnr.wantedOf[id]); SysMsg(COL_GREEN, "You arrested %s: +$%d.", GmName(id), 500 * s_Cnr.wantedOf[id]); s_Cnr.wantedOf[id] = 0; }
        else SysMsg(COL_SYSTEM, "%s is cuffed for 10 seconds - /arrest %d.", GmName(id), id);
        return true;
    }
    if(!strcmp(cmd, "placehit"))
    {
        int id = -1, amount = 0; char a[32] = ""; if(arg) sscanf(arg, "%31s %d", a, &amount); id = GmArgId(a);
        if(id < 0 || amount < 100) { SysMsg(COL_RED, "Use: /placehit <id> <amount $100+>"); return true; }
        if(Game::GetMoney() < amount) { SysMsg(COL_RED, "You don't have $%d.", amount); return true; }
        Game::AddMoney(-amount); s_Cnr.hit[id] += amount;
        GmSend(GMO_HIT, GM_CNR, 0xFF, id, 0, amount);
        SysMsg(COL_YELLOW, "Hit placed on %s: $%d (whoever kills them gets it).", GmName(id), s_Cnr.hit[id]);
        return true;
    }
    if(!strcmp(cmd, "hitlist"))
    {
        int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_Cnr.hit[i] > 0 && (players[i].active || i == (int)myId)) { SysMsg(COL_WHITE, "%s (%d): $%d", i == (int)myId ? settings.name : players[i].name, i, s_Cnr.hit[i]); ++n; }
        if(!n) SysMsg(COL_SYSTEM, "No hits. /placehit <id> <amount>");
        return true;
    }
    if(!strcmp(cmd, "wanted")) { SysMsg(COL_SYSTEM, "Your wanted level: %d", s_Cnr.wanted); return true; }
    return false;
}
static void CnrOnEvent(const PktGmEvent& e)
{
    const int ped = Game::LocalPed();
    switch(e.op)
    {
    case GMO_TEAM: if(e.from < MP_MAX_PLAYERS) s_Cnr.teams[e.from] = e.a; break;
    case GMO_WANTED: if(e.from < MP_MAX_PLAYERS) s_Cnr.wantedOf[e.from] = e.a; break;
    case GMO_ROB:
        if(e.b == 0)
        {
            int take = std::min(500, Game::GetMoney() / 5);
            if(take > 0) Game::AddMoney(-take);
            SysMsg(COL_RED, "%s robbed you: -$%d.", GmName(e.from), take);
            GmSend(GMO_ROB, GM_CNR, e.from, 0, 1, take);
        }
        else { if(e.val > 0) Game::AddMoney(e.val); SysMsg(COL_GREEN, "Robbery done: +$%d. Cops are after you.", e.val); }
        break;
    case GMO_CUFF: s_Cnr.cuffUntil = MP_NowMs() + 10000; SysMsg(COL_RED, "Officer %s cuffed you.", GmName(e.from)); break;
    case GMO_ARREST:
    {
        const int secs = 30 * std::max(1, s_Cnr.wanted);
        s_Cnr.jailUntil = MP_NowMs() + secs * 1000ULL; s_Cnr.cuffUntil = 0;
        SysMsg(COL_RED, "Officer %s arrested you: %d seconds in jail.", GmName(e.from), secs);
        CnrSetWanted(0);
        if(ped > 0) { Game::EnterArea(6); Game::Teleport(ped, { 264.0f, 77.5f, 1001.0f }, 270.0f); }   // the LSPD cells
        break;
    }
    case GMO_HIT:
        if(e.b == 0) { if(e.a >= 0 && e.a < MP_MAX_PLAYERS) { s_Cnr.hit[e.a] += e.val; SysMsg(COL_YELLOW, "A $%d hit was placed on %s.", e.val, e.a == (int)myId ? "YOU" : GmName(e.a)); } }
        else if(e.a == (int)myId && e.val > 0) { Game::AddMoney(e.val); SysMsg(COL_GREEN, "Hit done on %s: +$%d.", GmName(e.from), e.val); }
        if(e.b == 1 && e.from < MP_MAX_PLAYERS) s_Cnr.hit[e.from] = 0;
        break;
    }
}
static void CnrOnDeath(const PktDeath& d)
{
    if(!GmOn(GM_CNR)) return;
    if(d.victim == myId)
    {
        if(s_Cnr.hit[myId] > 0 && d.killer < MP_MAX_PLAYERS && d.killer != myId) GmSend(GMO_HIT, GM_CNR, 0xFF, d.killer, 1, s_Cnr.hit[myId]);
        s_Cnr.hit[myId] = 0;
        if(s_Cnr.wanted) CnrSetWanted(0);
    }
    else if(d.killer == myId && d.victim < MP_MAX_PLAYERS && s_Cnr.team == 2 && s_Cnr.teams[d.victim] == 1) CnrSetWanted(s_Cnr.wanted + 2);   // killed a cop
    else if(d.killer == myId && d.victim < MP_MAX_PLAYERS && s_Cnr.team != 1 && !s_Cnr.wantedOf[d.victim]) CnrSetWanted(s_Cnr.wanted + 1);
}
static void CnrProcess(int ped, uint64_t now)
{
    if(!GmOn(GM_CNR)) { s_Cnr.team = 0; s_Cnr.wanted = 0; s_Cnr.jailUntil = s_Cnr.cuffUntil = 0; return; }
    if(ped <= 0) return;
    if(s_Cnr.cuffUntil > now) { Vec3 z = { 0, 0, 0 }; Game::SetMoveSpeed(ped, false, z); }
    if(s_Cnr.jailUntil && now >= s_Cnr.jailUntil)
    {
        s_Cnr.jailUntil = 0; Game::EnterArea(0); Game::Teleport(ped, { 1544.0f, -1675.0f, 13.6f }, 90.0f);   // outside LSPD
        SysMsg(COL_GREEN, "You are free. Behave.");
    }
}
static void CnrRender(ImDrawList* dl, float& y)
{
    if(!GmOn(GM_CNR)) return;
    char t[64];
    if(s_Cnr.team || s_Cnr.wanted) { snprintf(t, sizeof(t), "%s  %s", s_Cnr.team == 1 ? "COP" : (s_Cnr.team == 2 ? "ROBBER" : "CIVILIAN"), std::string(s_Cnr.wanted, '*').c_str()); GmHudLine(dl, y, s_Cnr.team == 1 ? IM_COL32(80, 140, 255, 255) : IM_COL32(255, 160, 40, 255), t); y += UI::S(34); }
    if(s_Cnr.jailUntil) { snprintf(t, sizeof(t), "JAIL %d", (int)((s_Cnr.jailUntil - MP_NowMs()) / 1000)); GmHudLine(dl, y, IM_COL32(255, 60, 60, 255), t); y += UI::S(34); }
    // team + stars above the players
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!players[i].active || players[i].ped <= 0 || (!s_Cnr.teams[i] && !s_Cnr.wantedOf[i])) continue;
        Vec3 p = players[i].drawPos; p.z += 0.55f; ImVec2 s; if(!BcwW2S(p, s) || BcwCamDist(p) > 50.0f) continue;
        snprintf(t, sizeof(t), "%s %s", s_Cnr.teams[i] == 1 ? "COP" : (s_Cnr.teams[i] == 2 ? "ROBBER" : ""), std::string(s_Cnr.wantedOf[i], '*').c_str());
        ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(20), FLT_MAX, 0, t);
        dl->AddText(UI::Font(), UI::S(20), ImVec2(s.x - ts.x * 0.5f + 1, s.y + 1), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(UI::Font(), UI::S(20), ImVec2(s.x - ts.x * 0.5f, s.y), s_Cnr.teams[i] == 1 ? IM_COL32(80, 140, 255, 255) : IM_COL32(255, 160, 40, 255), t);
    }
}

// ================================ Attack / Defend ===============================================
struct AdBase { const char* name; float x, y, z; };
static const AdBase g_AdBases[] = {
    { "Area 69 gate",        213.8f, 1875.0f, 13.1f }, { "LS Docks",          2770.0f, -2413.0f, 13.6f },
    { "LV Airport",         1477.0f, 1240.0f, 10.8f },   { "SF Construction", -2088.0f, 230.0f, 35.3f },
    { "Grove Street",       2495.0f, -1687.0f, 13.5f },  { "Bayside Marina",  -2274.0f, 2322.0f, 4.8f },
};
#define AD_BASES ((int)(sizeof(g_AdBases) / sizeof(g_AdBases[0])))
static struct { int team = 0; int base = -1; uint64_t endAt = 0; float capS = 0; int votes[AD_BASES] = {}; int teams[MP_MAX_PLAYERS] = {}; } s_Ad;
static void AdStart(int base, bool announce)
{
    s_Ad.base = base; s_Ad.endAt = MP_NowMs() + 300000; s_Ad.capS = 0;
    memset(s_Ad.votes, 0, sizeof(s_Ad.votes));
    if(announce) GmSend(GMO_AD_ROUND, GM_AD, 0xFF, base);
    SysMsg(COL_YELLOW, "A/D round: %s. Attackers: stand 20 s in the checkpoint. Defenders: hold it 5 minutes.", g_AdBases[base].name);
}
static void AdEnd(bool attackersWon, bool announce)
{
    if(s_Ad.base < 0) return;
    if(announce) GmSend(GMO_AD_CAP, GM_AD, 0xFF, s_Ad.base, attackersWon ? 1 : 0);
    const bool iWon = (s_Ad.team == 1) == attackersWon && s_Ad.team;
    GameText(attackersWon ? "~r~Attackers win" : "~b~Defenders win", 4000, 3);
    if(iWon) { Game::AddMoney(1000); SysMsg(COL_GREEN, "Your team won: +$1000."); }
    s_Ad.base = -1; s_Ad.endAt = 0;
}
static bool AdCommand(const char* cmd, const char* arg)
{
    if(!GmOn(GM_AD)) return false;
    if(!strcmp(cmd, "ad"))
    {
        if(arg && !strncasecmp(arg, "leave", 5)) { s_Ad.team = 0; GmSend(GMO_TEAM, GM_AD, 0xFF, 0); SysMsg(COL_SYSTEM, "You left A/D."); return true; }
        int na = 0, nd = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) { if(s_Ad.teams[i] == 1) ++na; else if(s_Ad.teams[i] == 2) ++nd; }
        s_Ad.team = (arg && !strncasecmp(arg, "att", 3)) ? 1 : ((arg && !strncasecmp(arg, "def", 3)) ? 2 : (na <= nd ? 1 : 2));
        GmSend(GMO_TEAM, GM_AD, 0xFF, s_Ad.team);
        const int ped = Game::LocalPed();
        Game::GiveAndSelectWeapon(ped, 24); Game::GiveAndSelectWeapon(ped, 31); Game::AddArmour(ped, 100);
        SysMsg(COL_YELLOW, "You are a %s. /vote <1-%d> picks the base, /ad start starts a round, /ad leave.", s_Ad.team == 1 ? "ATTACKER" : "DEFENDER", AD_BASES);
        if(arg && !strncasecmp(arg, "start", 5)) { if(s_Ad.base < 0) AdStart(rand() % AD_BASES, true); }
        return true;
    }
    if(!strcmp(cmd, "vote"))
    {
        int b = arg ? atoi(arg) - 1 : -1;
        if(b < 0 || b >= AD_BASES) { std::string l; for(int i = 0; i < AD_BASES; ++i) l += std::to_string(i + 1) + " " + g_AdBases[i].name + (i + 1 < AD_BASES ? ", " : ""); SysMsg(COL_SYSTEM, "/vote <n>: %s", l.c_str()); return true; }
        if(s_Ad.base >= 0) { SysMsg(COL_RED, "A round is on (%s).", g_AdBases[s_Ad.base].name); return true; }
        s_Ad.votes[b]++; GmSend(GMO_VOTE, GM_AD, 0xFF, b);
        if(s_Ad.votes[b] >= 2) AdStart(b, true); else SysMsg(COL_SYSTEM, "Voted %s (2 votes start it).", g_AdBases[b].name);
        return true;
    }
    return false;
}
static void AdOnEvent(const PktGmEvent& e)
{
    switch(e.op)
    {
    case GMO_TEAM: if(e.from < MP_MAX_PLAYERS) s_Ad.teams[e.from] = e.a; break;
    case GMO_VOTE: if(e.a >= 0 && e.a < AD_BASES && s_Ad.base < 0) { s_Ad.votes[e.a]++; SysMsg(COL_SYSTEM, "%s voted %s.", GmName(e.from), g_AdBases[e.a].name); } break;
    case GMO_AD_ROUND: if(e.a >= 0 && e.a < AD_BASES) AdStart(e.a, false); break;
    case GMO_AD_CAP: if(s_Ad.base >= 0) AdEnd(e.b != 0, false); break;
    }
}
static void AdProcess(int ped, uint64_t now, float dt)
{
    if(!GmOn(GM_AD)) { s_Ad.team = 0; s_Ad.base = -1; return; }
    if(s_Ad.base < 0 || ped <= 0) return;
    if(now >= s_Ad.endAt) { AdEnd(false, s_Ad.team != 0); return; }
    const AdBase& b = g_AdBases[s_Ad.base];
    const float d = sqrtf((b.x - myDrawPos.x) * (b.x - myDrawPos.x) + (b.y - myDrawPos.y) * (b.y - myDrawPos.y));
    if(s_Ad.team == 1 && d < 6.0f && !Game::IsCharDead(ped)) { s_Ad.capS += dt; if(s_Ad.capS >= 20.0f) AdEnd(true, true); }
}
static void AdRender(ImDrawList* bg, ImDrawList* fg, float& y, float now)
{
    if(!GmOn(GM_AD) || s_Ad.base < 0) return;
    const AdBase& b = g_AdBases[s_Ad.base];
    MkBeam(bg, { b.x, b.y, b.z - 1.0f }, 6.0f, 5.0f, s_Ad.team == 2 ? 0x3B82F6FF : 0xEF4444FF, 0.7f);
    char t[80]; const int left = (int)((s_Ad.endAt - MP_NowMs()) / 1000);
    snprintf(t, sizeof(t), "%s %d:%02d", b.name, left / 60, left % 60); GmHudLine(fg, y, IM_COL32(255, 255, 255, 255), t); y += UI::S(34);
    if(s_Ad.capS > 0) { snprintf(t, sizeof(t), "CAPTURE %d%%", (int)(s_Ad.capS * 5)); GmHudLine(fg, y, IM_COL32(255, 70, 70, 255), t); y += UI::S(34); }
    (void)now;
}

// ================================ Stunt freeroam ================================================
static struct { float air = 0; bool wasAir = false; int ramp = 0; uint64_t nitroUntil = 0; int bestBonus = 0; } s_St;
struct StTp { const char* cmd; const char* name; float x, y, z, h; };
static const StTp g_StTp[] = {
    { "lvair",   "LV airport stunts",  1477.0f, 1240.0f,   10.8f, 0 },     { "sfair","SF airport",   -1337.0f, -224.0f, 14.1f, 315 },
    { "bigear",  "Big Ear",            -310.0f, 1540.0f,   75.5f, 0 },     { "dam",  "Sherman Dam",  -900.0f, 2000.0f, 60.9f, 0 },
    { "stuntpark","Stunt park (LV)",   1388.0f, 2160.0f,   11.0f, 0 },     { "quarry","Hunter Quarry", 588.0f, 873.0f, -42.5f, 0 },
};
static bool StuntCommand(const char* cmd, const char*)
{
    if(!GmOn(GM_STUNT)) return false;
    const int ped = Game::LocalPed(); if(ped <= 0) return false;
    const bool car = Game::IsCharInAnyCar(ped); const int veh = car ? Game::GetCarCharIsIn(ped) : 0;
    for(const StTp& t : g_StTp)
        if(!strcmp(cmd, t.cmd))
        {
            if(InEvent()) { SysMsg(COL_YELLOW, "Not during an event."); return true; }
            if(car) { Game::SetCarPos(veh, { t.x, t.y, t.z + 1.0f }, 0.6f); Game::SetCarHeading(veh, t.h); } else Game::Teleport(ped, { t.x, t.y, t.z }, t.h);
            GameText(t.name, 2000, 1); return true;
        }
    if(!strcmp(cmd, "ramp") || !strcmp(cmd, "jump"))
    {
        float hd = car ? Game::GetCarHeading(veh) : Game::GetCharHeading(ped), r = hd * 0.0174533f;
        Vec3 p = { myDrawPos.x - sinf(r) * 14.0f, myDrawPos.y + cosf(r) * 14.0f, 0 };
        p.z = Game::GroundZ(p.x, p.y, myDrawPos.z + 3.0f); if(!(p.z > -100.0f)) p.z = myDrawPos.z - 1.0f;
        const int model = !strcmp(cmd, "ramp") ? 1632 : 1655;   // waterjump1 / waterjumpx2 (stock GTA SA)
        if(s_St.ramp > 0) Game::DeleteWorldObject(s_St.ramp);
        s_St.ramp = Game::IsKnownModel(model) ? Game::CreateMapObject(model, p, 0, 0, hd) : -1;
        SysMsg(s_St.ramp > 0 ? COL_GREEN : COL_RED, s_St.ramp > 0 ? "Ramp ahead of you. Go." : "Could not place the ramp (try again).");
        if(s_St.ramp <= 0) s_St.ramp = 0;
        return true;
    }
    if(!strcmp(cmd, "n") || !strcmp(cmd, "nitro")) { if(!car) { SysMsg(COL_RED, "Get in a car."); return true; } s_St.nitroUntil = MP_NowMs() + 4000; GameText("~b~NITRO", 1200, 3); return true; }
    if(!strcmp(cmd, "stunthelp")) { SysMsg(COL_YELLOW, "Stunt: /ramp /jump /n (nitro) /fix /flip  teleports: /chiliad /lv /sf /ls /lvair /sfair /bigear /dam /stuntpark /quarry"); return true; }
    return false;
}
static void StuntProcess(int ped, uint64_t now, float dt)
{
    if(!GmOn(GM_STUNT)) { if(s_St.ramp > 0) { Game::DeleteWorldObject(s_St.ramp); s_St.ramp = 0; } return; }
    if(ped <= 0 || !Game::IsCharInAnyCar(ped)) { s_St.air = 0; s_St.wasAir = false; return; }
    const int veh = Game::GetCarCharIsIn(ped);
    Vec3 p; if(!Game::GetCarPos(veh, p)) return;
    if(s_St.nitroUntil > now)
    {
        Vec3 v; if(Game::GetMoveSpeed(veh, true, v)) { float s = sqrtf(v.x * v.x + v.y * v.y); if(s > 3.0f && s < 70.0f) { const float k = 1.0f + 0.9f * dt; v.x *= k; v.y *= k; Game::SetMoveSpeed(veh, true, v); } }
    }
    const float g = Game::GroundZ(p.x, p.y, p.z + 1.0f);
    const bool air = g > -100.0f && p.z - g > 3.0f;
    if(air) s_St.air += dt;
    else if(s_St.wasAir)
    {
        if(s_St.air > 1.5f)
        {
            int bonus = (int)(s_St.air * 60.0f); Game::AddMoney(bonus);
            char t[48]; snprintf(t, sizeof(t), "~y~Stunt bonus ~w~$%d", bonus); GameText(t, 2500, 3);
            if(bonus > s_St.bestBonus) s_St.bestBonus = bonus;
        }
        s_St.air = 0;
    }
    s_St.wasAir = air;
}

// ================================ Drift =========================================================
static struct { float pts = 0, combo = 1, comboT = 0, calm = 0; int lastHp = 0; int best = 0, total = 0; int others[MP_MAX_PLAYERS] = {}; } s_Dr;
static void DriftBank(bool crashed)
{
    if(s_Dr.pts < 50.0f) { s_Dr.pts = 0; s_Dr.combo = 1; return; }
    int score = (int)(s_Dr.pts * s_Dr.combo);
    if(crashed) { GameText("~r~Drift lost", 1500, 3); }
    else
    {
        Game::AddMoney(score / 10); s_Dr.total += score;
        char t[64]; snprintf(t, sizeof(t), "~g~Drift %d ~w~+$%d", score, score / 10); GameText(t, 2500, 3);
        if(score > s_Dr.best) { s_Dr.best = score; GmSend(GMO_SCORE, GM_DRIFT, 0xFF, 0, 0, score); SysMsg(COL_YELLOW, "New best drift: %d.", score); }
    }
    s_Dr.pts = 0; s_Dr.combo = 1; s_Dr.comboT = 0;
}
static void DriftProcess(int ped, uint64_t now, float dt)
{
    (void)now;
    if(!GmOn(GM_DRIFT) || ped <= 0 || !Game::IsCharInAnyCar(ped)) { if(s_Dr.pts > 0) DriftBank(false); s_Dr.lastHp = 0; return; }
    const int veh = Game::GetCarCharIsIn(ped);
    Vec3 v; if(!Game::GetMoveSpeed(veh, true, v)) return;
    const float spd = sqrtf(v.x * v.x + v.y * v.y);
    const float hd = Game::GetCarHeading(veh) * 0.0174533f;
    const float fx = -sinf(hd), fy = cosf(hd);
    float ang = 0; if(spd > 1.0f) { float c = (v.x * fx + v.y * fy) / spd; c = fmaxf(-1, fminf(1, c)); ang = acosf(c) * 57.2958f; }
    const int hp = Game::GetCarHealth(veh);
    if(s_Dr.lastHp && hp < s_Dr.lastHp - 40 && s_Dr.pts > 0) { s_Dr.lastHp = hp; DriftBank(true); return; }
    s_Dr.lastHp = hp;
    if(spd > 9.0f && ang > 12.0f && ang < 80.0f)
    {
        s_Dr.pts += ang * spd * dt * 0.25f; s_Dr.calm = 0;
        s_Dr.comboT += dt; if(s_Dr.comboT > 3.0f) { s_Dr.comboT = 0; s_Dr.combo = fminf(5.0f, s_Dr.combo + 1.0f); }
    }
    else if(s_Dr.pts > 0) { s_Dr.calm += dt; if(s_Dr.calm > 1.6f) DriftBank(false); }
}
static bool DriftCommand(const char* cmd, const char*)
{
    if(!GmOn(GM_DRIFT)) return false;
    if(!strcmp(cmd, "drift")) { SysMsg(COL_YELLOW, "Drift: slide sideways over 30 km/h. Best %d, total %d. /drifttop", s_Dr.best, s_Dr.total); return true; }
    if(!strcmp(cmd, "drifttop"))
    {
        std::vector<std::pair<int, std::string>> t; t.push_back({ s_Dr.best, settings.name });
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active && s_Dr.others[i] > 0) t.push_back({ s_Dr.others[i], players[i].name });
        std::sort(t.begin(), t.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first > b.first; });
        for(size_t i = 0; i < t.size() && i < 5; ++i) SysMsg(COL_WHITE, "%d. %s - %d", (int)i + 1, t[i].second.c_str(), t[i].first);
        return true;
    }
    return false;
}
static void DriftRender(ImDrawList* dl, float& y)
{
    if(!GmOn(GM_DRIFT) || s_Dr.pts < 1.0f) return;
    char t[48]; snprintf(t, sizeof(t), "DRIFT %d x%d", (int)s_Dr.pts, (int)s_Dr.combo);
    GmHudLine(dl, y, IM_COL32(255, 221, 51, 255), t); y += UI::S(34);
}

// ================================ Survival (DayZ) ===============================================
#define SV_CRATES 6
#define SV_ZOMBIES 5
static struct { bool on = false; float thirst = 100; int crate[SV_CRATES] = {}; Vec3 crateAt[SV_CRATES] = {}; int zom[SV_ZOMBIES] = {}; uint64_t zomDeadAt[SV_ZOMBIES] = {}; uint64_t thinkAt = 0, biteAt = 0; int kills = 0; } s_Sv;
static void SvClear()
{
    for(int& c : s_Sv.crate) { if(c > 0) Game::DeleteWorldObject(c); c = 0; }
    for(int& z : s_Sv.zom) { if(z > 0) Game::DeleteChar(z); z = 0; }
}
static bool SvCommand(const char* cmd, const char*)
{
    if(!GmOn(GM_SURVIVAL)) return false;
    if(!strcmp(cmd, "survival") || !strcmp(cmd, "dayz"))
    {
        s_Sv.on = !s_Sv.on;
        if(!s_Sv.on) SvClear(); else s_Sv.thirst = 100;
        SysMsg(COL_YELLOW, s_Sv.on ? "Survival on: find loot crates (yellow arrows), keep your thirst up (/drink $15), zombies come at night (21:00-06:00)." : "Survival off.");
        return true;
    }
    if(!strcmp(cmd, "drink") && s_Sv.on)
    {
        if(Game::GetMoney() < 15) { SysMsg(COL_RED, "A drink is $15."); return true; }
        Game::AddMoney(-15); s_Sv.thirst = fminf(100, s_Sv.thirst + 40); SysMsg(COL_GREEN, "Thirst %d%%.", (int)s_Sv.thirst); return true;
    }
    return false;
}
static void SvProcess(int ped, uint64_t now, float dt)
{
    if(!GmOn(GM_SURVIVAL) || !s_Sv.on) { if(s_Sv.on && !GmOn(GM_SURVIVAL)) { SvClear(); s_Sv.on = false; } return; }
    if(ped <= 0) return;
    s_Sv.thirst = fmaxf(0, s_Sv.thirst - dt / 30.0f);
    if(now < s_Sv.thinkAt) return;
    s_Sv.thinkAt = now + 1000;
    if(s_Sv.thirst <= 0) { int hp = Game::GetCharHealth(ped); if(hp > 10) Game::SetCharHealth(ped, hp - 2); }
    // loot crates
    for(int i = 0; i < SV_CRATES; ++i)
    {
        Vec3& a = s_Sv.crateAt[i];
        const float d = sqrtf((a.x - myDrawPos.x) * (a.x - myDrawPos.x) + (a.y - myDrawPos.y) * (a.y - myDrawPos.y));
        if(s_Sv.crate[i] > 0 && d < 2.0f)
        {
            Game::DeleteWorldObject(s_Sv.crate[i]); s_Sv.crate[i] = 0;
            switch(rand() % 5)
            {
            case 0: s_Sv.thirst = fminf(100, s_Sv.thirst + 50); GameText("~b~Water", 1500, 3); break;
            case 1: Game::SetCharHealth(ped, std::min(100, Game::GetCharHealth(ped) + 40)); GameText("~g~Medkit", 1500, 3); break;
            case 2: { static const int w[] = { 22, 25, 29, 30, 33 }; Game::GiveAndSelectWeapon(ped, w[rand() % 5]); GameText("~y~Weapon", 1500, 3); break; }
            case 3: Game::AddArmour(ped, 50); GameText("~w~Armour", 1500, 3); break;
            default: Game::AddMoney(150); GameText("~g~$150", 1500, 3); break;
            }
            continue;
        }
        if(s_Sv.crate[i] > 0 && d > 300.0f) { Game::DeleteWorldObject(s_Sv.crate[i]); s_Sv.crate[i] = 0; }
        if(s_Sv.crate[i] <= 0 && !Game::IsCharInAnyCar(ped))
        {
            const float ang = (rand() % 360) * 0.0174533f, r = 40.0f + rand() % 100;
            Vec3 p = { myDrawPos.x + cosf(ang) * r, myDrawPos.y + sinf(ang) * r, 0 };
            p.z = Game::GroundZ(p.x, p.y, myDrawPos.z + 30.0f);
            if(!(p.z > -100.0f) || fabsf(p.z - myDrawPos.z) > 25.0f) continue;
            const int model = Game::IsKnownModel(1271) ? 1271 : 1220;   // gen_box / cardboard box
            int h = Game::CreateMapObject(model, { p.x, p.y, p.z + 0.4f }, 0, 0, (float)(rand() % 360));
            if(h > 0) { s_Sv.crate[i] = h; a = p; }
            break;   // one a second
        }
    }
    // zombies at night
    const int hr = Game::GetHour(); const bool night = hr >= 21 || hr < 6;
    for(int i = 0; i < SV_ZOMBIES; ++i)
    {
        int& z = s_Sv.zom[i];
        if(z > 0)
        {
            if(Game::IsCharDead(z))
            {
                if(!s_Sv.zomDeadAt[i]) { s_Sv.zomDeadAt[i] = now; ++s_Sv.kills; Game::AddMoney(50); }
                else if(now - s_Sv.zomDeadAt[i] > 6000) { Game::DeleteChar(z); z = 0; s_Sv.zomDeadAt[i] = 0; }
                continue;
            }
            Vec3 zp; if(!Game::GetCharPos(z, zp)) { z = 0; continue; }
            const float d = sqrtf((zp.x - myDrawPos.x) * (zp.x - myDrawPos.x) + (zp.y - myDrawPos.y) * (zp.y - myDrawPos.y));
            if(!night || d > 120.0f) { Game::DeleteChar(z); z = 0; continue; }
            Game::TaskGoTo(z, myDrawPos, d > 8.0f ? 6 : 4);
            if(d < 1.6f && fabsf(zp.z - myDrawPos.z) < 2.0f && now - s_Sv.biteAt > 900 && !Game::IsCharInAnyCar(ped))
            { s_Sv.biteAt = now; int hp = Game::GetCharHealth(ped); Game::SetCharHealth(ped, std::max(1, hp - 6)); }
        }
        else if(night && !Game::IsCharInAnyCar(ped) && (rand() % 4) == 0)
        {
            static const int kZ[] = { 162, 212, 230, 78, 79, 134 };
            const float ang = (rand() % 360) * 0.0174533f, r = 30.0f + rand() % 20;
            Vec3 p = { myDrawPos.x + cosf(ang) * r, myDrawPos.y + sinf(ang) * r, 0 };
            p.z = Game::GroundZ(p.x, p.y, myDrawPos.z + 20.0f);
            if(!(p.z > -100.0f) || fabsf(p.z - myDrawPos.z) > 15.0f) continue;
            const int zm = kZ[rand() % 6]; if(!Game::LoadModel(zm)) continue;
            z = Game::CreateNpcPed(zm, { p.x, p.y, p.z + 1.0f }, 0);
            if(z < 0) z = 0;
            break;
        }
    }
}
static void SvRender(ImDrawList* bg, ImDrawList* fg, float& y, float now)
{
    if(!GmOn(GM_SURVIVAL) || !s_Sv.on) return;
    char t[48]; snprintf(t, sizeof(t), "THIRST %d%%  KILLS %d", (int)s_Sv.thirst, s_Sv.kills);
    GmHudLine(fg, y, s_Sv.thirst > 50 ? IM_COL32(80, 170, 255, 255) : (s_Sv.thirst > 25 ? IM_COL32(230, 200, 40, 255) : IM_COL32(220, 40, 40, 255)), t); y += UI::S(34);
    for(int i = 0; i < SV_CRATES; ++i) if(s_Sv.crate[i] > 0 && BcwCamDist(s_Sv.crateAt[i]) < 150.0f) HkArrow(bg, s_Sv.crateAt[i], now, IM_COL32(255, 221, 51, 220));
}

// ================================ shared ========================================================
static void Modes2OnEvent(const uint8_t* d, int len)
{
    if(len < (int)sizeof(PktGmEvent)) return;
    PktGmEvent e; memcpy(&e, d, sizeof(e)); e.text[sizeof(e.text) - 1] = 0;
    if(e.mode == GM_CNR) CnrOnEvent(e);
    else if(e.mode == GM_AD) AdOnEvent(e);
    else if(e.mode == GM_DRIFT && e.op == GMO_SCORE && e.from < MP_MAX_PLAYERS) { s_Dr.others[e.from] = e.val; }
}
static bool Modes2Command(const char* cmd, const char* arg)
{
    return CnrCommand(cmd, arg) || AdCommand(cmd, arg) || StuntCommand(cmd, arg) || DriftCommand(cmd, arg) || SvCommand(cmd, arg);
}
static void Modes2Process(int ped, uint64_t now)
{
    static uint64_t s_nLast = 0;
    float dt = s_nLast ? (now - s_nLast) / 1000.0f : 0.0f; s_nLast = now;
    if(dt < 0 || dt > 1.0f) dt = 0;
    CnrProcess(ped, now); AdProcess(ped, now, dt); StuntProcess(ped, now, dt); DriftProcess(ped, now, dt); SvProcess(ped, now, dt);
}
static void Modes2Render()
{
    if(client.GetState() != NET_CONNECTED || Game::IsPaused() || !myDrawValid) return;
    ImDrawList* bg = ImGui::GetBackgroundDrawList(); ImDrawList* fg = ImGui::GetForegroundDrawList();
    float y = UI::S(170); const float now = (float)ImGui::GetTime();
    CnrRender(fg, y); AdRender(bg, fg, y, now); DriftRender(fg, y); SvRender(bg, fg, y, now);
}
static void Modes2Reset()
{
    SvClear(); s_Sv.on = false;
    memset(s_Cnr.teams, 0, sizeof(s_Cnr.teams)); memset(s_Cnr.wantedOf, 0, sizeof(s_Cnr.wantedOf)); memset(s_Cnr.hit, 0, sizeof(s_Cnr.hit));
    memset(s_Ad.teams, 0, sizeof(s_Ad.teams)); s_Ad.base = -1; memset(s_Dr.others, 0, sizeof(s_Dr.others));
    if(s_St.ramp > 0) Game::DeleteWorldObject(s_St.ramp); s_St.ramp = 0;
}
