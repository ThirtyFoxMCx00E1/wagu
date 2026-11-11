// 4.13.4: San Andreas Online League (Rocket Soccer) - the server side.
// Included by server.cpp (MPServer members). Everything runs with m_Mutex held (10 Hz tick / packets).
//
//  * like SA:O "Rocket Soccer": a lobby (pick a car, then Ranked Soccar or Free Play), 2 teams
//    (Blue attacks +X, Orange attacks -X), 3 s kickoff countdown, 5:00 match, golden-goal overtime,
//    goal replay pause, end screen with the MVP
//  * the server owns the ball (league.h LgBallStep); phones send a TOUCH (their car position +
//    velocity) and the server checks it (the car is really there, near the ball, max 1 touch / 120 ms)
//  * RL scoreboard points: goal 100, assist 50, save 50, shot on goal 20, + 2 per touch (max 30)
//  * ranked MMR per account (Elo, K 24, start 600) with the RL tiers Bronze .. Supersonic Legend,
//    saved in league_stats.txt; /top, /stats, /rank, /team, /ff
#include <stdarg.h>

static char s_szLgFile[256] = "league_stats.txt";
void MPServerSetLgFile(const char* p) { MP_CopyStr(s_szLgFile, p && *p ? p : "league_stats.txt", sizeof(s_szLgFile)); }

bool MPServer::IsLgServer() const { return MP_IsLgServerName(m_szName); }

static const uint32_t LGC_BLUE = 0x3FA0FFFF, LGC_ORANGE = 0xFF9A2EFF, LGC_WHITE = 0xFFFFFFFF, LGC_GREY = 0xB8C2CCFF, LGC_CYAN = 0x48E0F0FF, LGC_RED = 0xFF5050FF;

void MPServer::LgReset()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) m_Lg[i] = LgPlayer();
    LgBallReset(m_LgBall);
    m_nLgPhase = LGP_WARMUP; m_nLgPhaseEnd = 0; m_nLgClockMs = LG_MATCH_SEC * 1000; m_bLgOT = false;
    m_nLgScore[0] = m_nLgScore[1] = 0; m_nLgLast = -1; m_nLgPrev = -1; m_nLgLastAt = 0; m_nLgPrevAt = 0;
    m_nLgSeq = 0; m_nLgPlayersAt = 0; m_nLgSaveAt = MP_NowMs() + 60000; m_bLgDirty = false; m_nLgMvp = -1; m_nLgTipAt = MP_NowMs() + 90000; m_nLgTip = 0;
}

void MPServer::LgLoad()
{
    m_LgSaved.clear();
    FILE* f = fopen(s_szLgFile, "r"); if(!f) { Log("LEAGUE: no %s yet (new season)", s_szLgFile); return; }
    char line[512]; int n = 0;
    while(fgets(line, sizeof(line), f))
    {
        char* tab = strchr(line, '\t'); if(!tab || line[0] == '#') continue;
        *tab = 0; size_t l = strlen(tab + 1); while(l && (tab[1 + l - 1] == '\n' || tab[1 + l - 1] == '\r')) tab[1 + --l] = 0;
        m_LgSaved[line] = tab + 1; ++n;
    }
    fclose(f);
    Log("LEAGUE: %d ranked players loaded from %s", n, s_szLgFile);
}

void MPServer::LgStore(int slot)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].account[0]) return;
    const LgPlayer& p = m_Lg[slot]; if(!p.loaded) return;
    char buf[256];
    snprintf(buf, sizeof(buf), "%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%.23s", p.mmr, p.peak, p.games, p.wins, p.tGoals, p.tAssists, p.tSaves, p.tShots, p.mvps, p.streak, m_Slots[slot].name);
    m_LgSaved[m_Slots[slot].account] = buf; m_bLgDirty = true;
}

void MPServer::LgSave()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) LgStore(i);
    if(!m_bLgDirty && !m_LgSaved.empty()) return;
    char tmp[300]; snprintf(tmp, sizeof(tmp), "%s.tmp", s_szLgFile);
    FILE* f = fopen(tmp, "w"); if(!f) return;
    fprintf(f, "# San Andreas Online League - account\\tmmr\\tpeak\\tgames\\twins\\tgoals\\tassists\\tsaves\\tshots\\tmvps\\tstreak\\tname\n");
    for(auto& kv : m_LgSaved) fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f); rename(tmp, s_szLgFile);
    m_bLgDirty = false;
}

void MPServer::LgMsg(int slot, uint32_t col, const char* fmt, ...)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = col;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    if(slot >= 0) { if(slot < MP_MAX_PLAYERS && m_Slots[slot].used) SendTo(slot, &m, sizeof(m)); }
    else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &m, sizeof(m));
}

void MPServer::LgEvent(int slot, uint8_t ev, int a, int b, int c, int val, const float* pos, const char* text)
{
    PktLgEvent e; memset(&e, 0, sizeof(e)); e.type = PKT_LG; e.op = LGO_EVENT; e.ev = ev;
    e.a = (uint8_t)a; e.b = (uint8_t)b; e.c = (uint8_t)c; e.val = (uint16_t)(val < 0 ? 0 : (val > 65535 ? 65535 : val));
    if(pos) memcpy(e.pos, pos, sizeof(e.pos));
    MP_CopyStr(e.text, text ? text : "", sizeof(e.text));
    if(slot >= 0) { if(slot < MP_MAX_PLAYERS && m_Slots[slot].used) SendTo(slot, &e, sizeof(e)); }
    else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &e, sizeof(e));
}

void MPServer::LgSendMe(int slot)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used) return;
    const LgPlayer& p = m_Lg[slot];
    PktLgMe m; memset(&m, 0, sizeof(m)); m.type = PKT_LG; m.op = LGO_ME;
    m.team = p.team; m.car = p.car; m.mmr = (uint16_t)p.mmr; m.peak = (uint16_t)p.peak;
    m.games = p.games; m.wins = p.wins; m.goals = p.tGoals; m.assists = p.tAssists; m.saves = p.tSaves; m.shots = p.tShots; m.mvps = p.mvps;
    m.tier = (uint8_t)LgTierOf(p.mmr); m.div = (uint8_t)LgDivOf(p.mmr); m.streak = (uint8_t)(p.streak < 0 ? 0 : (p.streak > 255 ? 255 : p.streak));
    SendTo(slot, &m, sizeof(m));
}

int MPServer::LgCount(int team) const
{
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team == team) ++n;
    return n;
}

void MPServer::LgSpawnOne(int slot, int order)
{
    LgPlayer& p = m_Lg[slot]; if(p.team > 1) return;
    // RL kickoff spots: one centred far back, two diagonals, then spread along the back
    static const float sp[6][2] = { { 40, 0 }, { 30, -14 }, { 30, 14 }, { 44, -7 }, { 44, 7 }, { 36, 0 } };
    const int k = order % 6; const float side = p.team == 0 ? -1.0f : 1.0f;
    float pos[4] = { side * sp[k][0], sp[k][1] + (order >= 6 ? (order - 5) * 3.0f : 0.0f), 1.2f, p.team == 0 ? 270.0f : 90.0f };
    LgEvent(slot, LGE_SPAWN, p.team, p.car, order, 0, pos, nullptr);
}

void MPServer::LgKickoff(uint64_t now)
{
    LgBallReset(m_LgBall);
    m_nLgLast = m_nLgPrev = -1; m_nLgLastAt = m_nLgPrevAt = 0;
    m_nLgPhase = LGP_KICKOFF; m_nLgPhaseEnd = now + LG_KICKOFF_MS;
    int ord[2] = { 0, 0 };
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team <= 1) LgSpawnOne(i, ord[m_Lg[i].team]++);
}

void MPServer::LgNewMatch(uint64_t now)
{
    m_nLgScore[0] = m_nLgScore[1] = 0; m_nLgClockMs = LG_MATCH_SEC * 1000; m_bLgOT = false; m_nLgMvp = -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { LgPlayer& p = m_Lg[i]; p.score = p.goals = p.assists = p.saves = p.shots = p.touches = 0; }
    LgBalance(true);
    LgKickoff(now);
    LgMsg(-1, LGC_CYAN, "[LEAGUE] New match: %d vs %d, 5:00 - first to score more goals wins. Golden goal if it's a tie.", LgCount(0), LgCount(1));
}

void MPServer::LgBalance(bool quiet)
{   // RL auto-balance: move the newest players from the bigger team while it has 2+ more
    for(int guard = 0; guard < MP_MAX_PLAYERS; ++guard)
    {
        int b = LgCount(0), o = LgCount(1); if(abs(b - o) < 2) break;
        const int from = b > o ? 0 : 1; int pick = -1; uint64_t newest = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team == from && m_Lg[i].teamAt >= newest) { newest = m_Lg[i].teamAt; pick = i; }
        if(pick < 0) break;
        m_Lg[pick].team = (uint8_t)(1 - from); LgSendMe(pick);
        if(!quiet) LgMsg(pick, LGC_GREY, "[LEAGUE] Teams were uneven - you play for %s now.", from ? "Blue" : "Orange");
    }
}

void MPServer::LgSetTeam(int slot, int want)
{
    LgPlayer& p = m_Lg[slot];
    if(want == 255) { if(p.team <= 1) { p.team = 255; LgMsg(slot, LGC_GREY, "[LEAGUE] You left the match (Free Play in the lobby). /team to play again."); } LgSendMe(slot); return; }
    if(want != 0 && want != 1) want = LgCount(0) <= LgCount(1) ? 0 : 1;
    const int other = LgCount(1 - want) - (p.team == 1 - want ? 1 : 0);
    if(LgCount(want) - (p.team == want ? 1 : 0) > other) { want = 1 - want; LgMsg(slot, LGC_GREY, "[LEAGUE] That team is full - balanced to the other one."); }
    if(LgCount(want) >= MP_LG_MAX_PLAYERS / 2 && p.team != want) { LgMsg(slot, LGC_RED, "[LEAGUE] Both teams are full (8 v 8)."); return; }
    p.team = (uint8_t)want; p.teamAt = MP_NowMs();
    LgSendMe(slot);
    LgMsg(-1, want ? LGC_ORANGE : LGC_BLUE, "[LEAGUE] %s joined %s.", m_Slots[slot].name, want ? "Orange" : "Blue");
    if(m_nLgPhase == LGP_KICKOFF || m_nLgPhase == LGP_PLAY || m_nLgPhase == LGP_WARMUP) LgSpawnOne(slot, LgCount(want) - 1);
}

void MPServer::LgOnAuthed(int slot)
{
    if(!IsLgServer() || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    LgPlayer& p = m_Lg[slot]; p = LgPlayer(); p.loaded = true;
    auto it = m_LgSaved.find(m_Slots[slot].account);
    if(it != m_LgSaved.end())
        sscanf(it->second.c_str(), "%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d", &p.mmr, &p.peak, &p.games, &p.wins, &p.tGoals, &p.tAssists, &p.tSaves, &p.tShots, &p.mvps, &p.streak);
    if(p.mmr < 0 || p.mmr > 3000) p.mmr = 600;
    if(p.peak < p.mmr) p.peak = p.mmr;
    LgSendMe(slot);
    static const char* const dv[4] = { "I", "II", "III", "IV" };
    LgMsg(slot, LGC_CYAN, "[LEAGUE] Welcome to San Andreas Online League. Rank: %s %s (%d MMR) - %u wins in %u games.",
          g_LgTierName[LgTierOf(p.mmr)], LgTierOf(p.mmr) >= 7 ? "" : dv[LgDivOf(p.mmr)], p.mmr, p.wins, p.games);
    LgMsg(slot, LGC_GREY, "[LEAGUE] Choose a car, then your game mode and arena. /team blue|orange|auto, /top, /stats, /ff");
}

void MPServer::LgOnJoin(int slot, bool fresh)
{
    if(!IsLgServer() || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    if(fresh) { const bool ld = m_Lg[slot].loaded; if(!ld) m_Lg[slot] = LgPlayer(); }
    LgSendMe(slot);
    m_nLgPlayersAt = 0;   // a new scoreboard right away
}

void MPServer::LgOnLeave(int slot)
{
    if(!IsLgServer() || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    LgPlayer& p = m_Lg[slot];
    if(p.team <= 1 && (m_nLgPhase == LGP_PLAY || m_nLgPhase == LGP_GOAL || m_nLgPhase == LGP_KICKOFF) && LgCount(0) && LgCount(1))
    {   // RL: leaving a ranked match early counts as a loss
        p.mmr -= 12; if(p.mmr < 0) p.mmr = 0; p.games++; p.streak = 0;
    }
    if(m_Slots[slot].account[0] && p.loaded)
    {   // m_Slots[slot].used is already false here: store directly
        char buf[256];
        snprintf(buf, sizeof(buf), "%d\t%d\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%d\t%.23s", p.mmr, p.peak, p.games, p.wins, p.tGoals, p.tAssists, p.tSaves, p.tShots, p.mvps, p.streak, m_Slots[slot].name);
        m_LgSaved[m_Slots[slot].account] = buf; m_bLgDirty = true;
    }
    if(m_nLgLast == slot) m_nLgLast = -1;
    if(m_nLgPrev == slot) m_nLgPrev = -1;
    p = LgPlayer();
    m_nLgPlayersAt = 0;
}

void MPServer::LgEndMatch(uint64_t now)
{
    const int win = m_nLgScore[0] > m_nLgScore[1] ? 0 : 1;
    // MVP: the best score on the winning team
    int mvp = -1, best = -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team == win && m_Lg[i].score > best) { best = m_Lg[i].score; mvp = i; }
    m_nLgMvp = mvp;
    // Elo by team average
    double avg[2] = { 0, 0 }; int cnt[2] = { 0, 0 };
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team <= 1) { avg[m_Lg[i].team] += m_Lg[i].mmr; cnt[m_Lg[i].team]++; }
    const bool ranked = cnt[0] > 0 && cnt[1] > 0;
    for(int t = 0; t < 2; ++t) avg[t] = cnt[t] ? avg[t] / cnt[t] : 600;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || m_Lg[i].team > 1) continue;
        LgPlayer& p = m_Lg[i]; const int t = p.team;
        p.tGoals += p.goals; p.tAssists += p.assists; p.tSaves += p.saves; p.tShots += p.shots;
        if(!ranked) continue;
        const double exp = 1.0 / (1.0 + pow(10.0, (avg[1 - t] - avg[t]) / 400.0));
        const int oldTier = LgTierOf(p.mmr);
        int d = (int)lround(24.0 * ((t == win ? 1.0 : 0.0) - exp)); if(t == win && d < 4) d = 4; if(t != win && d > -4) d = -4;
        p.mmr += d; if(p.mmr < 0) p.mmr = 0; if(p.mmr > p.peak) p.peak = p.mmr;
        p.games++; if(t == win) { p.wins++; p.streak = p.streak >= 0 ? p.streak + 1 : 1; } else p.streak = 0;
        if(i == mvp) p.mvps++;
        LgSendMe(i); LgStore(i);
        LgEvent(i, LGE_RANK, LgTierOf(p.mmr), LgDivOf(p.mmr), t == win, d + 1000, nullptr, g_LgTierName[LgTierOf(p.mmr)]);
        if(LgTierOf(p.mmr) > oldTier) LgMsg(-1, LGC_CYAN, "[LEAGUE] %s ranked up to %s.", m_Slots[i].name, g_LgTierName[LgTierOf(p.mmr)]);
    }
    char t[72]; snprintf(t, sizeof(t), "%s WINS %d - %d", win ? "ORANGE" : "BLUE", m_nLgScore[0], m_nLgScore[1]);
    LgEvent(-1, LGE_END, win, mvp < 0 ? MP_INVALID_ID : mvp, ranked, best < 0 ? 0 : best, nullptr, t);
    LgMsg(-1, win ? LGC_ORANGE : LGC_BLUE, "[LEAGUE] %s%s%s", t, mvp >= 0 ? " - MVP: " : "", mvp >= 0 ? m_Slots[mvp].name : "");
    m_nLgPhase = LGP_END; m_nLgPhaseEnd = now + LG_END_MS;
    m_nLgPlayersAt = 0;
    LgSave();
}

void MPServer::LgGoal(int scorerTeam, uint64_t now)
{
    m_nLgScore[scorerTeam]++;
    int scorer = -1, assist = -1;
    if(m_nLgLast >= 0 && m_Slots[m_nLgLast].used && m_Lg[m_nLgLast].team == scorerTeam) scorer = m_nLgLast;
    if(scorer >= 0 && m_nLgPrev >= 0 && m_nLgPrev != scorer && m_Slots[m_nLgPrev].used && m_Lg[m_nLgPrev].team == scorerTeam && m_nLgLastAt - m_nLgPrevAt < 5000) assist = m_nLgPrev;
    if(scorer >= 0) { m_Lg[scorer].goals++; m_Lg[scorer].score += 100; }
    if(assist >= 0) { m_Lg[assist].assists++; m_Lg[assist].score += 50; }
    const float sp = sqrtf(m_LgBall.v[0] * m_LgBall.v[0] + m_LgBall.v[1] * m_LgBall.v[1] + m_LgBall.v[2] * m_LgBall.v[2]) * 3.6f;   // km/h on the RL goal banner
    float pos[4] = { m_LgBall.p[0], m_LgBall.p[1], m_LgBall.p[2], sp };
    char t[72];
    if(scorer >= 0) snprintf(t, sizeof(t), "%s%s%s", m_Slots[scorer].name, assist >= 0 ? " - assist " : "", assist >= 0 ? m_Slots[assist].name : "");
    else snprintf(t, sizeof(t), "%s", m_nLgLast >= 0 && m_Slots[m_nLgLast].used ? m_Slots[m_nLgLast].name : "Own goal");
    LgEvent(-1, LGE_GOAL, scorerTeam, scorer < 0 ? MP_INVALID_ID : scorer, assist < 0 ? MP_INVALID_ID : assist, (int)sp, pos, t);
    LgMsg(-1, scorerTeam ? LGC_ORANGE : LGC_BLUE, "[LEAGUE] GOAL %s! %s  (%d km/h)  Blue %d - %d Orange", scorerTeam ? "ORANGE" : "BLUE", t, (int)sp, m_nLgScore[0], m_nLgScore[1]);
    m_nLgPlayersAt = 0;
    if(m_bLgOT) { LgEndMatch(now); return; }   // golden goal
    m_nLgPhase = LGP_GOAL; m_nLgPhaseEnd = now + LG_GOAL_MS;
}

void MPServer::LgTouch(int slot, const PktLgCmd& c, uint64_t now)
{
    LgPlayer& p = m_Lg[slot];
    if(p.team > 1 && m_nLgPhase != LGP_WARMUP) return;            // spectators / lobby don't touch the match ball
    if(m_nLgPhase == LGP_KICKOFF || m_nLgPhase == LGP_GOAL || m_nLgPhase == LGP_END) return;
    if(now - p.lastTouch < 120) return;
    for(int k = 0; k < 3; ++k) if(!std::isfinite(c.pos[k]) || !std::isfinite(c.vel[k])) return;
    const Slot& s = m_Slots[slot];
    float car[3] = { c.pos[0] - kLgCenter[0], c.pos[1] - kLgCenter[1], c.pos[2] - kLgCenter[2] };
    if(s.hasPos)
    {   // the car must really be where it says (a sync from the last ~second)
        const float dx = s.pos[0] - c.pos[0], dy = s.pos[1] - c.pos[1];
        if(dx * dx + dy * dy > 14.0f * 14.0f) return;
    }
    const float bx = m_LgBall.p[0] - car[0], by = m_LgBall.p[1] - car[1], bz = m_LgBall.p[2] - car[2];
    if(bx * bx + by * by + bz * bz > (LG_BALL_R + 6.0f) * (LG_BALL_R + 6.0f)) return;   // too far from the server ball (lag window ~3 m)
    float v[3] = { c.vel[0], c.vel[1], c.vel[2] };
    const float vs = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if(vs > 60.0f) for(int k = 0; k < 3; ++k) v[k] *= 60.0f / vs;
    p.lastTouch = now;
    // RL stats: a save = the ball was going into my goal and isn't any more; a shot = it now goes into theirs
    const bool wasIn = p.team <= 1 && LgHeadsIntoGoal(m_LgBall, p.team, 2.0f);
    LgBallHit(m_LgBall, car, v);
    if(m_nLgPhase == LGP_PLAY && p.team <= 1)
    {
        if(p.touches < 15) { p.touches++; p.score += 2; }
        if(wasIn && !LgHeadsIntoGoal(m_LgBall, p.team, 2.0f)) { p.saves++; p.score += 50; LgEvent(-1, LGE_SAVE, p.team, slot, 0, 50, nullptr, s.name); }
        else if(LgHeadsIntoGoal(m_LgBall, 1 - p.team, 2.5f) && now - p.lastShot > 1500) { p.lastShot = now; p.shots++; p.score += 20; LgEvent(slot, LGE_SHOT, p.team, slot, 0, 20, nullptr, "Shot on Goal"); }
        if(m_nLgLast != slot) { m_nLgPrev = m_nLgLast; m_nLgPrevAt = m_nLgLastAt; }
        m_nLgLast = slot; m_nLgLastAt = now;
    }
    else { m_nLgLast = slot; m_nLgLastAt = now; }
    m_nLgPushAt = 0;   // send the new ball right away
}

void MPServer::LgHandleCmd(int slot, const PktLgCmd& c)
{
    if(!IsLgServer() || slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].authed) return;
    const uint64_t now = MP_NowMs();
    LgPlayer& p = m_Lg[slot];
    switch(c.cmd)
    {
        case LGC_TOUCH: LgTouch(slot, c, now); break;
        case LGC_CAR: if(c.a < LG_CAR_COUNT) { p.car = c.a; LgSendMe(slot); } break;
        case LGC_TEAM: if(now - p.teamCmdAt > 1500) { p.teamCmdAt = now; LgSetTeam(slot, c.a); } break;
        case LGC_QCHAT:
        {   // RL quick chat (fixed lines only)
            static const char* const q[8] = { "I got it!", "Need boost!", "Take the shot!", "Defending...", "Nice shot!", "What a save!", "Great pass!", "Thanks!" };
            if(c.a < 8 && now - p.qchatAt > 1200)
            {
                p.qchatAt = now;
                char t[72]; snprintf(t, sizeof(t), "%s: %s", m_Slots[slot].name, q[c.a]);
                LgEvent(-1, LGE_QCHAT, p.team, slot, c.a, 0, nullptr, t);
            }
            break;
        }
        case LGC_SPAWN: if(p.team <= 1) LgSpawnOne(slot, LgCount(p.team) - 1); break;
        case LGC_LEAVE_ARENA: LgSetTeam(slot, 255); break;
        case LGC_STATS: LgSendMe(slot); m_nLgPlayersAt = 0; break;
    }
}

void MPServer::LgPushState(uint64_t now)
{
    PktLgState s; memset(&s, 0, sizeof(s)); s.type = PKT_LG; s.op = LGO_STATE;
    s.phase = m_nLgPhase; s.overtime = m_bLgOT ? 1 : 0;
    s.score[0] = (uint8_t)(m_nLgScore[0] > 255 ? 255 : m_nLgScore[0]); s.score[1] = (uint8_t)(m_nLgScore[1] > 255 ? 255 : m_nLgScore[1]);
    s.clock = (uint16_t)((m_nLgClockMs + 999) / 1000);
    s.phaseMs = (uint16_t)(m_nLgPhaseEnd > now ? (m_nLgPhaseEnd - now > 65000 ? 65000 : m_nLgPhaseEnd - now) : 0);
    s.lastTouch = (uint8_t)(m_nLgLast < 0 ? MP_INVALID_ID : m_nLgLast); s.lastTeam = m_nLgLast >= 0 ? m_Lg[m_nLgLast].team : 255;
    memcpy(s.ball, m_LgBall.p, sizeof(s.ball)); memcpy(s.vel, m_LgBall.v, sizeof(s.vel));
    s.seq = ++m_nLgSeq; s.blueN = (uint8_t)LgCount(0); s.orangeN = (uint8_t)LgCount(1); s.mvp = (uint8_t)(m_nLgMvp < 0 ? MP_INVALID_ID : m_nLgMvp);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].authed) SendTo(i, &s, sizeof(s));
}

void MPServer::LgPushPlayers()
{
    PktLgPlayers pk; memset(&pk, 0, sizeof(pk)); pk.type = PKT_LG; pk.op = LGO_PLAYERS;
    for(int i = 0; i < MP_MAX_PLAYERS && pk.n < MP_LG_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used) continue;
        const LgPlayer& p = m_Lg[i]; LgRow& r = pk.r[pk.n++];
        r.id = (uint8_t)i; r.team = p.team; r.car = p.car; r.flags = i == m_nLgMvp ? 1 : 0;
        r.score = (uint16_t)p.score; r.goals = (uint16_t)p.goals; r.assists = (uint16_t)p.assists; r.saves = (uint16_t)p.saves; r.shots = (uint16_t)p.shots; r.mmr = (uint16_t)p.mmr;
    }
    const int len = (int)offsetof(PktLgPlayers, r) + pk.n * (int)sizeof(LgRow);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].authed) SendTo(i, &pk, len);
}

void MPServer::LgTick(uint64_t now)
{
    if(!IsLgServer()) return;
    if(!m_nLgTickAt) m_nLgTickAt = now;
    float dt = (now - m_nLgTickAt) / 1000.0f; m_nLgTickAt = now;
    if(dt > 0.5f) dt = 0.5f;
    const int nb = LgCount(0), no = LgCount(1);
    switch(m_nLgPhase)
    {
        case LGP_WARMUP:
        {   // free play until both teams have someone
            for(int k = 0; k < 4; ++k) { int g = LgBallStep(m_LgBall, dt / 4); if(g) { LgEvent(-1, LGE_NOTICE, g - 1, 0, 0, 0, nullptr, "Free play goal!"); LgBallReset(m_LgBall); break; } }
            if(nb && no) LgNewMatch(now);
            break;
        }
        case LGP_KICKOFF:
            LgBallReset(m_LgBall);
            if(now >= m_nLgPhaseEnd) { m_nLgPhase = LGP_PLAY; LgEvent(-1, LGE_NOTICE, 0, 0, 0, 0, nullptr, "GO!"); }
            break;
        case LGP_PLAY:
        {
            if(!nb || !no) { m_nLgPhase = LGP_WARMUP; LgMsg(-1, LGC_GREY, "[LEAGUE] Not enough players - back to free play."); break; }
            for(int k = 0; k < 4; ++k)
            {
                const int g = LgBallStep(m_LgBall, dt / 4);
                if(g) { LgGoal(g - 1, now); break; }
            }
            if(m_nLgPhase != LGP_PLAY) break;
            const uint32_t ms = (uint32_t)(dt * 1000);
            if(m_bLgOT) m_nLgClockMs += ms;
            else if(m_nLgClockMs > ms) m_nLgClockMs -= ms;
            else
            {   // RL: at 0:00 the match ends once the ball touches the ground
                m_nLgClockMs = 0;
                if(m_LgBall.p[2] <= LG_BALL_R + 0.3f)
                {
                    if(m_nLgScore[0] == m_nLgScore[1]) { m_bLgOT = true; LgEvent(-1, LGE_NOTICE, 0, 0, 0, 0, nullptr, "OVERTIME"); LgMsg(-1, LGC_CYAN, "[LEAGUE] OVERTIME - next goal wins."); LgKickoff(now); }
                    else LgEndMatch(now);
                }
            }
            break;
        }
        case LGP_GOAL:
            for(int k = 0; k < 4; ++k) LgBallStep(m_LgBall, dt / 4);   // the ball keeps rolling in the net (replay)
            if(now >= m_nLgPhaseEnd)
            {
                if(!m_bLgOT && m_nLgClockMs == 0) LgEndMatch(now);
                else LgKickoff(now);
            }
            break;
        case LGP_END:
            if(now >= m_nLgPhaseEnd) { if(nb && no) LgNewMatch(now); else { m_nLgPhase = LGP_WARMUP; LgBallReset(m_LgBall); } }
            break;
    }
    if(now >= m_nLgPushAt) { m_nLgPushAt = now + 100; LgPushState(now); }
    if(now >= m_nLgPlayersAt) { m_nLgPlayersAt = now + 1000; LgPushPlayers(); }
    if(now >= m_nLgTipAt)
    {
        static const char* const tips[] = {
            "[TIP] Hold BOOST to fly over the ball - boost pads on the field refill it.",
            "[TIP] Double tap JUMP for a dodge: the hardest hits come from a flip into the ball.",
            "[TIP] Rotate: if a team mate goes for the ball, fall back toward your own goal.",
            "[TIP] /top shows the season leaderboard, /stats your career, /team blue|orange|auto.",
            "[TIP] A save = you clear a ball that was going into your net (+50).",
        };
        LgMsg(-1, LGC_GREY, "%s", tips[m_nLgTip++ % (sizeof(tips) / sizeof(tips[0]))]);
        m_nLgTipAt = now + 5 * 60 * 1000;
    }
    if(now >= m_nLgSaveAt) { m_nLgSaveAt = now + 120000; LgSave(); }
}

bool MPServer::LgChat(int slot, const char* text)
{
    if(!text || text[0] != '/' || slot < 0 || slot >= MP_MAX_PLAYERS) return false;
    char cmd[24] = ""; const char* arg = text + 1; int n = 0;
    while(*arg && *arg != ' ' && n < 22) cmd[n++] = (char)tolower((unsigned char)*arg++);
    cmd[n] = 0; while(*arg == ' ') ++arg;
    LgPlayer& p = m_Lg[slot];
    if(!strcmp(cmd, "team"))
    {
        int want = 2;
        if(!strncasecmp(arg, "b", 1)) want = 0; else if(!strncasecmp(arg, "o", 1)) want = 1; else if(!strncasecmp(arg, "l", 1) || !strncasecmp(arg, "s", 1)) want = 255;
        LgSetTeam(slot, want); return true;
    }
    if(!strcmp(cmd, "stats") || !strcmp(cmd, "rank") || !strcmp(cmd, "mmr"))
    {
        static const char* const dv[4] = { "I", "II", "III", "IV" };
        LgMsg(slot, LGC_CYAN, "[LEAGUE] %s %s - %d MMR (peak %d). %u games, %u wins (%u%%), %u MVP.", g_LgTierName[LgTierOf(p.mmr)], LgTierOf(p.mmr) >= 7 ? "" : dv[LgDivOf(p.mmr)], p.mmr, p.peak,
              p.games, p.wins, p.games ? p.wins * 100 / p.games : 0, p.mvps);
        LgMsg(slot, LGC_GREY, "[LEAGUE] Career: %u goals, %u assists, %u saves, %u shots. Win streak %d.", p.tGoals + p.goals, p.tAssists + p.assists, p.tSaves + p.saves, p.tShots + p.shots, p.streak);
        return true;
    }
    if(!strcmp(cmd, "top"))
    {
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) LgStore(i);
        std::vector<std::pair<int, std::string>> v;
        for(auto& kv : m_LgSaved)
        {
            int mmr = 0; char nm[32] = "";
            const char* s = kv.second.c_str(); mmr = atoi(s);
            const char* last = strrchr(s, '\t'); MP_CopyStr(nm, last ? last + 1 : kv.first.c_str(), sizeof(nm));
            v.push_back({ mmr, nm });
        }
        std::sort(v.begin(), v.end(), [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) { return a.first > b.first; });
        LgMsg(slot, LGC_CYAN, "[LEAGUE] Season leaderboard (%d ranked):", (int)v.size());
        for(int i = 0; i < (int)v.size() && i < 10; ++i) LgMsg(slot, i < 3 ? LGC_WHITE : LGC_GREY, "  %d. %s - %s (%d)", i + 1, v[i].second.c_str(), g_LgTierName[LgTierOf(v[i].first)], v[i].first);
        return true;
    }
    if(!strcmp(cmd, "ff") || !strcmp(cmd, "forfeit"))
    {
        if(p.team > 1 || m_nLgPhase != LGP_PLAY) { LgMsg(slot, LGC_GREY, "[LEAGUE] You can only forfeit during a match."); return true; }
        const int t = p.team; int yes = 0, all = 0;
        p.ffVote = true;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Lg[i].team == t) { ++all; if(m_Lg[i].ffVote) ++yes; }
        LgMsg(-1, t ? LGC_ORANGE : LGC_BLUE, "[LEAGUE] %s wants to forfeit (%d/%d).", m_Slots[slot].name, yes, all);
        if(yes * 2 > all || all == 1)
        {
            m_nLgScore[1 - t] = std::max(m_nLgScore[1 - t], m_nLgScore[t] + 1);
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) m_Lg[i].ffVote = false;
            LgEndMatch(MP_NowMs());
        }
        return true;
    }
    if(!strcmp(cmd, "help") || !strcmp(cmd, "cmds") || !strcmp(cmd, "commands"))
    {
        LgMsg(slot, LGC_CYAN, "[LEAGUE] /team blue|orange|auto|leave  /stats  /top  /ff");
        return true;
    }
    return false;
}
