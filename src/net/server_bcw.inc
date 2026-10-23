// MP 3.04 - Brief Case War, server side (included by server.cpp; m_Mutex is held in every
// function here). The server is the referee: it owns the teams, the briefcases, the steal bars,
// kills / stats, the victory, the scoreboard and the map vote, so every phone sees the same match.
//
//   QUEUE   players pick Gustav Team / Schelapz Team (max 10 each) and wait, frozen.
//           The match starts when m_nBcwMin players (20) are in the two teams.
//   ROLL    7 s slot machine: one leader per team (never the same player twice in a row of
//           matches until everybody in the team had a turn).
//   LIVE    steal the enemy briefcase: stand 5 s in the enemy circle (9 m) on foot. Bring it to
//           your own circle while YOUR briefcase is at home = victory. If both are stolen nobody
//           can win until a carrier dies: the briefcase drops, enemies pick it up, owners
//           touching it send it home (a dropped one goes home by itself after 60 s).
//   COOLDOWN 24 s + 20 ticks (25 s) after a victory: the briefcases are cleared (every phone deletes its briefcase
//           objects + war vehicles and goes back to the connection screen), the match is reset.
//   RESULTS scoreboard (25 s), then VOTE (20 s, 2 maps open + 4 locked), then a new QUEUE.

bool MPServer::IsBcwServer() const { return MP_IsBcwServerName(m_szName); }

void MPServer::SetBcwMinPlayers(int n)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_nBcwMin = n < 2 ? 2 : (n > MP_BCW_MAX_PLAYERS ? MP_BCW_MAX_PLAYERS : n);
}

void MPServer::BcwReset()
{
    memset(m_Bcw, 0, sizeof(m_Bcw));
    memset(m_BcwCase, 0, sizeof(m_BcwCase));
    m_BcwCase[0].carrier = m_BcwCase[1].carrier = MP_INVALID_ID;
    m_BcwCase[0].stealer = m_BcwCase[1].stealer = MP_INVALID_ID;
    m_nBcwLeader[0] = m_nBcwLeader[1] = MP_INVALID_ID;
    m_nBcwState = BCW_QUEUE; m_nBcwStateAt = MP_NowMs(); m_nBcwWinner = 0;
}

int MPServer::BcwCount(int team) const
{
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Bcw[i].team == team) ++n;
    return n;
}

void MPServer::BcwEvent(uint8_t ev, uint8_t team, uint8_t who)
{
    PktBcwEvent e; e.type = PKT_BCW_EVENT; e.ev = ev; e.team = team; e.who = who;
    Broadcast(&e, sizeof(e));
}

void MPServer::BcwSendState(int slot)
{
    PktBcwState p; memset(&p, 0, sizeof(p));
    p.type = PKT_BCW_STATE; p.state = m_nBcwState; p.map = m_nBcwMap; p.minPlayers = (uint8_t)m_nBcwMin;
    uint64_t now = MP_NowMs();
    uint64_t len = m_nBcwState == BCW_COOLDOWN ? BCW_COOLDOWN_MS : m_nBcwState == BCW_ROLL ? BCW_ROLL_MS : m_nBcwState == BCW_RESULTS ? BCW_RESULTS_MS : m_nBcwState == BCW_VOTE ? BCW_VOTE_MS : 0;
    if(m_nBcwState == BCW_LIVE) p.timerS = (uint16_t)((now - m_nBcwMatchStart) / 1000);
    else if(len) { uint64_t e = now - m_nBcwStateAt; p.timerS = (uint16_t)(e >= len ? 0 : (len - e + 999) / 1000); }
    p.count[0] = (uint8_t)BcwCount(BCW_T_GUSTAV); p.count[1] = (uint8_t)BcwCount(BCW_T_SCHELAPZ);
    p.leader[0] = m_nBcwLeader[0]; p.leader[1] = m_nBcwLeader[1];
    for(int c = 0; c < 2; ++c)
    {
        const BcwSrvCase& k = m_BcwCase[c];
        p.cases[c].state = k.state; p.cases[c].carrier = k.carrier; p.cases[c].stealer = k.stealer;
        float pr = k.stealer != MP_INVALID_ID ? (float)(now - k.stealStart) / BCW_STEAL_MS : 0;
        p.cases[c].progress = (uint8_t)(pr <= 0 ? 0 : pr >= 1 ? 255 : pr * 255);
        memcpy(p.cases[c].pos, k.pos, sizeof(k.pos));
        p.cases[c].dropLeftS = k.state == CASE_DROPPED && now - k.droppedAt < BCW_DROP_RETURN_MS ? (uint16_t)((BCW_DROP_RETURN_MS - (now - k.droppedAt)) / 1000) : 0;
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used) continue;
        p.team[i] = m_Bcw[i].team;
        if((m_nBcwState == BCW_VOTE || m_nBcwState == BCW_RESULTS) && m_Bcw[i].vote) { int v = m_Bcw[i].vote - 1; if(v >= 0 && v < BCW_MAP_COUNT && p.votes[v] < 255) ++p.votes[v]; }
    }
    if(slot >= 0) SendTo(slot, &p, sizeof(p)); else Broadcast(&p, sizeof(p));
    m_nBcwLastSend = now;
}

void MPServer::BcwSetState(uint8_t st)
{
    m_nBcwState = st; m_nBcwStateAt = MP_NowMs();
    BcwSendState(-1);
}

void MPServer::BcwPickLeaders()
{
    PktBcwRoll r; memset(&r, 0, sizeof(r));
    r.type = PKT_BCW_ROLL;
    for(int t = 0; t < 2; ++t)
    {
        uint8_t team = (uint8_t)(t + 1);
        int ids[MP_MAX_PLAYERS], n = 0, fresh[MP_MAX_PLAYERS], nf = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!m_Slots[i].used || m_Bcw[i].team != team) continue;
            if(n < BCW_TEAM_SIZE) { ids[n++] = i; r.ids[t][r.n[t]++] = (uint8_t)i; }
            const char* key = m_Slots[i].uid[0] ? m_Slots[i].uid : m_Slots[i].name;
            bool was = false;
            for(auto& s : m_BcwPastLeaders) if(s == key) { was = true; break; }
            if(!was && nf < BCW_TEAM_SIZE) fresh[nf++] = i;
        }
        if(n == 0) { m_nBcwLeader[t] = MP_INVALID_ID; r.leader[t] = MP_INVALID_ID; continue; }
        if(nf == 0)   // everybody in this team was a leader already: start over for them
        {
            for(int k = 0; k < n; ++k)
            {
                const char* key = m_Slots[ids[k]].uid[0] ? m_Slots[ids[k]].uid : m_Slots[ids[k]].name;
                for(size_t q = 0; q < m_BcwPastLeaders.size(); ++q) if(m_BcwPastLeaders[q] == key) { m_BcwPastLeaders.erase(m_BcwPastLeaders.begin() + q); break; }
                fresh[nf++] = ids[k];
            }
        }
        int pick = fresh[rand() % nf];
        m_nBcwLeader[t] = (uint8_t)pick;
        r.leader[t] = (uint8_t)pick;
        m_BcwPastLeaders.push_back(m_Slots[pick].uid[0] ? m_Slots[pick].uid : m_Slots[pick].name);
        if(m_BcwPastLeaders.size() > 200) m_BcwPastLeaders.erase(m_BcwPastLeaders.begin());
        Log("Brief Case War: %s leader is %s", g_BcwTeamName[team], m_Slots[pick].name);
    }
    Broadcast(&r, sizeof(r));
}

void MPServer::BcwStartMatch()
{
    uint64_t now = MP_NowMs();
    for(int c = 0; c < 2; ++c)
    {
        BcwSrvCase& k = m_BcwCase[c];
        k.state = CASE_HOME; k.carrier = k.stealer = MP_INVALID_ID; k.stealStart = 0;
        memcpy(k.pos, g_BcwMaps[m_nBcwMap].spawn[c], sizeof(k.pos));
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        BcwSrvPlayer t = m_Bcw[i];
        memset(&m_Bcw[i], 0, sizeof(m_Bcw[i]));
        m_Bcw[i].team = m_Slots[i].used ? t.team : 0;
        m_Bcw[i].spawnedAt = now;
    }
    m_nBcwMatchStart = now; m_nBcwWinner = 0;
    BcwSetState(BCW_LIVE);
    BcwEvent(BCWE_MATCH_START, 0, MP_INVALID_ID);
    Log("Brief Case War: match started on %s (%d vs %d)", g_BcwMaps[m_nBcwMap].name, BcwCount(BCW_T_GUSTAV), BcwCount(BCW_T_SCHELAPZ));
}

void MPServer::BcwSendScores()
{
    uint16_t dur = (uint16_t)(((m_nBcwMatchEnd > m_nBcwMatchStart ? m_nBcwMatchEnd : MP_NowMs()) - m_nBcwMatchStart) / 1000);
    for(int t = 0; t < 2; ++t)
    {
        PktBcwScore s; memset(&s, 0, sizeof(s));
        s.type = PKT_BCW_SCORE; s.winner = (uint8_t)m_nBcwWinner; s.team = (uint8_t)(t + 1);
        s.map = m_nBcwMap; s.part = (uint8_t)t; s.durationS = dur;
        for(int i = 0; i < MP_MAX_PLAYERS && s.n < BCW_TEAM_SIZE; ++i)
        {
            if(!m_Slots[i].used || m_Bcw[i].team != t + 1) continue;
            const BcwSrvPlayer& b = m_Bcw[i];
            BcwScoreRow& r = s.rows[s.n++];
            r.id = (uint8_t)i; r.team = b.team; r.leader = m_nBcwLeader[t] == i;
            r.kills = (uint8_t)(b.kills > 255 ? 255 : b.kills); r.deaths = (uint8_t)(b.deaths > 255 ? 255 : b.deaths);
            r.steals = (uint8_t)(b.steals > 255 ? 255 : b.steals); r.caps = (uint8_t)(b.caps > 255 ? 255 : b.caps);
            r.returns = (uint8_t)(b.returns > 255 ? 255 : b.returns);
            r.damage = (uint16_t)(b.damage > 65535 ? 65535 : b.damage);
            int sc = b.kills * 100 + b.steals * 150 + b.caps * 500 + b.returns * 100 + (int)(b.damage / 10);
            r.score = (uint16_t)(sc > 65535 ? 65535 : sc);
            MP_CopyStr(r.name, m_Slots[i].name, MP_NAME_LEN);
        }
        Broadcast(&s, (int)(sizeof(s) - sizeof(s.rows) + sizeof(BcwScoreRow) * s.n));
    }
}

void MPServer::BcwVictory(int team)
{
    if(m_nBcwState != BCW_LIVE) return;
    m_nBcwWinner = team;
    Log("Brief Case War: %s wins", team ? g_BcwTeamName[team] : "nobody");
    BcwEvent(BCWE_VICTORY, (uint8_t)team, MP_INVALID_ID);
    m_nBcwMatchEnd = MP_NowMs();
    for(int c = 0; c < 2; ++c)   // reset: no briefcase carried / dropped any more (stats are kept for the scoreboard)
    {
        BcwSrvCase& k = m_BcwCase[c];
        k.state = CASE_HOME; k.carrier = k.stealer = MP_INVALID_ID; k.stealStart = 0;
        memcpy(k.pos, g_BcwMaps[m_nBcwMap].spawn[c], sizeof(k.pos));
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) m_Bcw[i].vote = 0;
    BcwSetState(BCW_COOLDOWN);   // scoreboard after the cooldown
}

void MPServer::BcwDrop(int c, const float* at)
{
    BcwSrvCase& k = m_BcwCase[c];
    uint8_t who = k.carrier;
    k.state = CASE_DROPPED; k.carrier = MP_INVALID_ID; k.droppedAt = MP_NowMs();
    if(at) memcpy(k.pos, at, sizeof(k.pos));
    BcwEvent(BCWE_DROPPED, (uint8_t)(c + 1), who);
    BcwSendState(-1);
}

void MPServer::BcwTick(uint64_t now)
{
    if(!IsBcwServer()) return;
    bool changed = false;
    int gtv = BcwCount(BCW_T_GUSTAV), slz = BcwCount(BCW_T_SCHELAPZ);
    switch(m_nBcwState)
    {
    case BCW_QUEUE:
        if(gtv + slz >= m_nBcwMin && gtv > 0 && slz > 0)
        {
            BcwSetState(BCW_ROLL);
            BcwPickLeaders();
            return;
        }
        break;
    case BCW_ROLL:
        if(gtv == 0 || slz == 0) { BcwSetState(BCW_QUEUE); return; }
        if(now - m_nBcwStateAt >= BCW_ROLL_MS) { BcwStartMatch(); return; }
        break;
    case BCW_LIVE:
    {
        if(gtv == 0 && slz == 0) { BcwReset(); BcwSendState(-1); return; }
        if(gtv == 0 || slz == 0) { BcwVictory(gtv ? BCW_T_GUSTAV : BCW_T_SCHELAPZ); return; }   // the other team left
        const BcwMap& m = g_BcwMaps[m_nBcwMap];
        for(int c = 0; c < 2; ++c)
        {
            BcwSrvCase& k = m_BcwCase[c];
            const uint8_t owner = (uint8_t)(c + 1);
            if(k.state == CASE_HOME)
            {
                int found = -1;
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                {
                    const Slot& s = m_Slots[i];
                    if(!s.used || !s.hasPos || !m_Bcw[i].team || m_Bcw[i].team == owner) continue;
                    if(s.lastFlags & (SYNC_DEAD | SYNC_IN_CAR)) continue;   // on foot and alive
                    if(now < m_Bcw[i].spawnedAt) continue;                 // dead, waiting for the respawn
                    if(fabsf(s.pos[2] - m.spawn[c][2]) > 6.0f || BcwDist2D(m.spawn[c], s.pos[0], s.pos[1]) > BCW_SAFE_R) continue;
                    if(m_BcwCase[1 - c].carrier == i) continue;   // already carrying their own team's prize
                    if(i == k.stealer) { found = i; break; }
                    if(found < 0) found = i;
                }
                if(found < 0) { if(k.stealer != MP_INVALID_ID) { k.stealer = MP_INVALID_ID; changed = true; } }
                else if(found != k.stealer) { k.stealer = (uint8_t)found; k.stealStart = now; changed = true; }
                else if(now - k.stealStart >= BCW_STEAL_MS)
                {
                    k.state = CASE_CARRIED; k.carrier = k.stealer; k.stealer = MP_INVALID_ID;
                    m_Bcw[k.carrier].steals++;
                    Log("Brief Case War: %s stole the %s briefcase", m_Slots[k.carrier].name, g_BcwTeamName[owner]);
                    BcwEvent(BCWE_STOLEN, owner, k.carrier);
                    changed = true;
                }
            }
            else if(k.state == CASE_CARRIED)
            {
                int ci = k.carrier;
                if(ci >= MP_MAX_PLAYERS || !m_Slots[ci].used || !m_Bcw[ci].team) { BcwDrop(c, nullptr); continue; }
                const Slot& s = m_Slots[ci];
                if(s.hasPos) memcpy(k.pos, s.pos, sizeof(k.pos));
                if(s.lastFlags & SYNC_DEAD) { BcwDrop(c, s.pos); continue; }
                int home = 1 - c;   // the carrier's own base
                if(m_BcwCase[home].state == CASE_HOME && BcwDist2D(m.spawn[home], s.pos[0], s.pos[1]) <= BCW_SAFE_R && fabsf(s.pos[2] - m.spawn[home][2]) < 8.0f)
                {
                    m_Bcw[ci].caps++;
                    BcwVictory(m_Bcw[ci].team);
                    return;
                }
            }
            else if(k.state == CASE_DROPPED)
            {
                if(now - k.droppedAt >= BCW_DROP_RETURN_MS)
                {
                    k.state = CASE_HOME; memcpy(k.pos, m.spawn[c], sizeof(k.pos));
                    BcwEvent(BCWE_RETURNED, owner, MP_INVALID_ID); changed = true; continue;
                }
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                {
                    const Slot& s = m_Slots[i];
                    if(!s.used || !s.hasPos || !m_Bcw[i].team || (s.lastFlags & SYNC_DEAD) || now < m_Bcw[i].spawnedAt) continue;
                    if(BcwDist2D(k.pos, s.pos[0], s.pos[1]) > BCW_PICK_R || fabsf(s.pos[2] - k.pos[2]) > 3.0f) continue;
                    if(m_Bcw[i].team == owner)
                    {
                        k.state = CASE_HOME; memcpy(k.pos, m.spawn[c], sizeof(k.pos));
                        m_Bcw[i].returns++;
                        BcwEvent(BCWE_RETURNED, owner, (uint8_t)i);
                    }
                    else if(m_BcwCase[1 - c].carrier != i)
                    {
                        k.state = CASE_CARRIED; k.carrier = (uint8_t)i;
                        BcwEvent(BCWE_PICKED, owner, (uint8_t)i);
                    }
                    else continue;
                    changed = true; break;
                }
            }
        }
        break;
    }
    case BCW_COOLDOWN:
        if(now - m_nBcwStateAt >= BCW_COOLDOWN_MS) { BcwSendScores(); BcwSetState(BCW_RESULTS); return; }
        break;
    case BCW_RESULTS:
        if(now - m_nBcwStateAt >= BCW_RESULTS_MS) { BcwSetState(BCW_VOTE); return; }
        break;
    case BCW_VOTE:
        if(now - m_nBcwStateAt >= BCW_VOTE_MS)
        {
            int votes[BCW_MAP_COUNT] = {0}, best = -1;
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Bcw[i].vote) { int v = m_Bcw[i].vote - 1; if(v >= 0 && v < BCW_MAP_COUNT && g_BcwMaps[v].open) ++votes[v]; }
            int top = 0; for(int v = 0; v < BCW_MAP_COUNT; ++v) if(votes[v] > top) top = votes[v];
            if(top > 0)
            {
                int ties[BCW_MAP_COUNT], nt = 0;
                for(int v = 0; v < BCW_MAP_COUNT; ++v) if(votes[v] == top) ties[nt++] = v;
                best = ties[rand() % nt];
            }
            else { best = m_nBcwMap; do { best = (best + 1) % BCW_MAP_COUNT; } while(!g_BcwMaps[best].open); }   // nobody voted: next open map
            std::vector<std::string> keep = m_BcwPastLeaders;
            BcwReset();
            m_BcwPastLeaders = keep;
            m_nBcwMap = (uint8_t)best;
            Log("Brief Case War: next map %s", g_BcwMaps[best].name);
            BcwEvent(BCWE_MAP, (uint8_t)best, MP_INVALID_ID);
            BcwSendState(-1);   // everybody back to the team picker (teams reset)
            return;
        }
        break;
    }
    if(changed || now - m_nBcwLastSend >= (m_nBcwState == BCW_LIVE || m_nBcwState == BCW_ROLL ? 200u : 1000u)) BcwSendState(-1);
}

void MPServer::BcwHandleCmd(int slot, const PktBcwCmd& c)
{
    if(!IsBcwServer() || slot < 0) return;
    BcwSrvPlayer& b = m_Bcw[slot];
    if(c.cmd == BCWC_TEAM)
    {
        uint8_t team = c.arg;
        if(team != BCW_T_GUSTAV && team != BCW_T_SCHELAPZ) return;
        if(m_nBcwState == BCW_RESULTS || m_nBcwState == BCW_VOTE || m_nBcwState == BCW_COOLDOWN) return;   // wait for the next queue
        if(b.team == team) return;
        if(b.team && m_nBcwState != BCW_QUEUE) return;   // no switching sides mid-match
        if(BcwCount(team) >= BCW_TEAM_SIZE)
        {
            PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFFC800FF;
            snprintf(m.text, sizeof(m.text), "%s is full (%d/%d). Pick the other team.", g_BcwTeamName[team], BCW_TEAM_SIZE, BCW_TEAM_SIZE);
            SendTo(slot, &m, sizeof(m));
            BcwSendState(slot);
            return;
        }
        memset(&b, 0, sizeof(b));
        b.team = team; b.spawnedAt = MP_NowMs();
        Log("Brief Case War: %s joined %s", m_Slots[slot].name, g_BcwTeamName[team]);
        BcwSendState(-1);
    }
    else if(c.cmd == BCWC_LEAVE_TEAM)
    {
        if(m_nBcwState != BCW_QUEUE) return;
        b.team = 0; BcwSendState(-1);
    }
    else if(c.cmd == BCWC_VOTE)
    {
        if(m_nBcwState != BCW_VOTE && m_nBcwState != BCW_RESULTS) return;
        if(c.arg >= BCW_MAP_COUNT || !g_BcwMaps[c.arg].open) return;
        b.vote = (uint8_t)(c.arg + 1);
        BcwSendState(-1);
    }
}

// PKT_DEATH: kills / deaths, a carrier drops the briefcase where he died
void MPServer::BcwOnDeath(int victim, int killer)
{
    if(!IsBcwServer() || m_nBcwState != BCW_LIVE || victim < 0 || victim >= MP_MAX_PLAYERS) return;
    if(!m_Bcw[victim].team) return;
    m_Bcw[victim].deaths++;
    if(killer >= 0 && killer < MP_MAX_PLAYERS && killer != victim && m_Bcw[killer].team && m_Bcw[killer].team != m_Bcw[victim].team) m_Bcw[killer].kills++;
    for(int c = 0; c < 2; ++c) if(m_BcwCase[c].state == CASE_CARRIED && m_BcwCase[c].carrier == victim) BcwDrop(c, m_Slots[victim].hasPos ? m_Slots[victim].pos : nullptr);
    m_Bcw[victim].spawnedAt = MP_NowMs() + BCW_WASTED_MS + BCW_RESPAWN_MS;   // spawn protection starts after the respawn
}

// PKT_DAMAGE: false = drop it (team mates, players still in the queue, spawn protection)
bool MPServer::BcwOnDamage(int from, int target, float amount)
{
    if(!IsBcwServer()) return true;
    if(m_nBcwState != BCW_LIVE) return false;
    if(from < 0 || target < 0 || from >= MP_MAX_PLAYERS || target >= MP_MAX_PLAYERS) return false;
    uint8_t a = m_Bcw[from].team, b = m_Bcw[target].team;
    if(!a || !b || a == b) return false;   // friendly fire off
    uint64_t now = MP_NowMs();
    if(now < m_Bcw[target].spawnedAt + BCW_SPAWN_PROTECT_MS) return false;
    if(amount > 0 && amount < 1000) m_Bcw[from].damage += amount;
    return true;
}

void MPServer::BcwOnLeave(int slot)
{
    if(!IsBcwServer()) return;
    for(int c = 0; c < 2; ++c)
    {
        if(m_BcwCase[c].state == CASE_CARRIED && m_BcwCase[c].carrier == slot) BcwDrop(c, m_Slots[slot].hasPos ? m_Slots[slot].pos : nullptr);
        if(m_BcwCase[c].stealer == slot) m_BcwCase[c].stealer = MP_INVALID_ID;
    }
    memset(&m_Bcw[slot], 0, sizeof(m_Bcw[slot]));
    BcwSendState(-1);
}

void MPServer::BcwOnJoin(int slot, bool fresh)
{
    if(!IsBcwServer()) return;
    if(fresh) memset(&m_Bcw[slot], 0, sizeof(m_Bcw[slot]));
    BcwSendState(slot);
    if(m_nBcwState == BCW_ROLL)   // late: show the roll result too
    {
        PktBcwRoll r; memset(&r, 0, sizeof(r)); r.type = PKT_BCW_ROLL;
        for(int t = 0; t < 2; ++t)
        {
            r.leader[t] = m_nBcwLeader[t];
            for(int i = 0; i < MP_MAX_PLAYERS && r.n[t] < BCW_TEAM_SIZE; ++i) if(m_Slots[i].used && m_Bcw[i].team == t + 1) r.ids[t][r.n[t]++] = (uint8_t)i;
        }
        SendTo(slot, &r, sizeof(r));
    }
}
