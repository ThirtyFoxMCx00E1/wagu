// 4.13.40: VC:MP-style gamemode on GTA Vice City servers (net/vcmp.h).
// Classes / teams / server cars from a VC:MP 0.1c server.ini [GAME] section
// (VC:MP: https://github.com/ThirtyFoxMCx00E1/vc-mp, GPL v2, kyeman / SA:MP team - server/gamemode/generic.cpp,
// server/netrpc.cpp RequestClass, server/vehiclepool.cpp). Included by server.cpp.

static bool VcmpReadFloats(const char* s, float* out, int n)
{
    for(int i = 0; i < n; ++i)
    {
        while(*s == ' ' || *s == '\t') ++s;
        char* e = nullptr; out[i] = strtof(s, &e);
        if(e == s) return false;
        s = e;
    }
    return true;
}

bool MPServer::VcmpLoad(const char* path)
{
    FILE* f = fopen(path, "r");
    if(!f) return false;
    memset(&m_VcmpInfo, 0, sizeof(m_VcmpInfo));
    m_VcmpInfo.type = PKT_VCMP; m_VcmpInfo.op = VCMP_INFO;
    MP_CopyStr(m_VcmpInfo.gameName, "Vice City", sizeof(m_VcmpInfo.gameName));
    m_VcmpClasses.clear(); m_VcmpVehs.clear();
    memset(m_VcmpTeam, VCMP_TEAM_NONE, sizeof(m_VcmpTeam));
    char line[512];
    while(fgets(line, sizeof(line), f))
    {
        char* c = strchr(line, ';'); if(c) *c = 0;
        char* eq = strchr(line, '='); if(!eq) continue;
        *eq = 0; char* key = line; char* val = eq + 1;
        auto trim = [](char* s) { while(*s == ' ' || *s == '\t') ++s; char* e = s + strlen(s); while(e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r' || e[-1] == '\n')) *--e = 0; return s; };
        key = trim(key); val = trim(val);
        if(!strcasecmp(key, "GameName"))
        {
            if(*val == '"') { ++val; char* q = strchr(val, '"'); if(q) *q = 0; }
            MP_CopyStr(m_VcmpInfo.gameName, val, sizeof(m_VcmpInfo.gameName));
        }
        else if(!strcasecmp(key, "Teams")) { int t = atoi(val); m_VcmpInfo.teams = (uint8_t)(t < 0 ? 0 : (t > VCMP_MAX_TEAMS ? VCMP_MAX_TEAMS : t)); }
        else if(!strcasecmp(key, "TeamName"))
        {
            int t = atoi(val); const char* n = strchr(val, ' ');
            if(t >= 0 && t < VCMP_MAX_TEAMS && n) MP_CopyStr(m_VcmpInfo.teamName[t], n + 1, sizeof(m_VcmpInfo.teamName[t]));
        }
        else if(!strcasecmp(key, "PlayerPos")) VcmpReadFloats(val, m_VcmpInfo.playerPos, 3);
        else if(!strcasecmp(key, "CamPos"))    VcmpReadFloats(val, m_VcmpInfo.camPos, 3);
        else if(!strcasecmp(key, "CamLook"))   VcmpReadFloats(val, m_VcmpInfo.camLook, 3);
        else if(!strcasecmp(key, "VehicleRespawn")) { int s = atoi(val); m_nVcmpRespawnMs = (uint32_t)(s < 5 ? 5 : (s > 3600 ? 3600 : s)) * 1000u; }
        else if(!strcasecmp(key, "Class") && m_VcmpClasses.size() < VCMP_MAX_CLASSES)
        {
            float v[12];
            if(!VcmpReadFloats(val, v, 12)) continue;
            PktVcmpClass k; memset(&k, 0, sizeof(k));
            k.type = PKT_VCMP; k.op = VCMP_CLASS; k.idx = (uint8_t)m_VcmpClasses.size();
            k.team = (v[0] < 0 || v[0] >= VCMP_MAX_TEAMS) ? VCMP_TEAM_NONE : (uint8_t)v[0];
            k.skin = (int16_t)v[1]; k.pos[0] = v[2]; k.pos[1] = v[3]; k.pos[2] = v[4]; k.angle = v[5];
            for(int i = 0; i < 3; ++i)
            {
                const int w = (int)v[6 + i * 2], a = (int)v[7 + i * 2];
                k.weapon[i] = (uint8_t)(w > 0 && w <= 33 ? w : 0);
                k.ammo[i] = (uint16_t)(a < 0 ? 0 : (a > 9999 ? 9999 : a));
            }
            m_VcmpClasses.push_back(k);
        }
        else if(!strcasecmp(key, "Vehicle") && m_VcmpVehs.size() < VCMP_MAX_VEHICLES)
        {
            float v[7];
            if(!VcmpReadFloats(val, v, 7) || v[0] < 130 || v[0] > 236) continue;   // Vice City car models
            VcmpVeh cv; memset(&cv, 0, sizeof(cv));
            PktVcmpVeh& k = cv.v;
            k.type = PKT_VCMP; k.op = VCMP_VEH; k.idx = (uint8_t)m_VcmpVehs.size(); k.state = VCMP_PARKED; k.holder = MP_INVALID_ID;
            k.model = (int16_t)v[0]; k.pos[0] = v[1]; k.pos[1] = v[2]; k.pos[2] = v[3]; k.angle = v[4];
            k.col1 = (uint8_t)v[5]; k.col2 = (uint8_t)v[6];
            m_VcmpVehs.push_back(cv);
        }
    }
    fclose(f);
    m_VcmpInfo.classes = (uint8_t)m_VcmpClasses.size();
    m_VcmpInfo.vehicles = (uint8_t)m_VcmpVehs.size();
    m_bVcmp = !m_VcmpClasses.empty() || !m_VcmpVehs.empty();
    if(m_bVcmp) printf("[MP Server] VC:MP gamemode \"%s\": %d classes, %d teams, %d server cars (respawn %u s) from %s\n",
                       m_VcmpInfo.gameName, (int)m_VcmpClasses.size(), m_VcmpInfo.teams, (int)m_VcmpVehs.size(), m_nVcmpRespawnMs / 1000u, path);
    return m_bVcmp;
}

void MPServer::VcmpSend(int slot)
{
    if(!m_bVcmp || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    m_VcmpTeam[slot] = VCMP_TEAM_NONE;
    SendTo(slot, &m_VcmpInfo, sizeof(m_VcmpInfo));
    for(auto& k : m_VcmpClasses) SendTo(slot, &k, sizeof(k));
    for(auto& v : m_VcmpVehs) SendTo(slot, &v.v, sizeof(v.v));
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == slot || !m_Slots[i].used || m_VcmpTeam[i] == VCMP_TEAM_NONE) continue;
        PktVcmpCmd t = { PKT_VCMP, VCMP_PTEAM, (uint8_t)i, m_VcmpTeam[i] };
        SendTo(slot, &t, sizeof(t));
    }
}

void MPServer::VcmpHandle(int slot, const uint8_t* data, int len)
{
    if(!m_bVcmp || len < (int)sizeof(PktVcmpCmd)) return;
    PktVcmpCmd c; memcpy(&c, data, sizeof(c));
    const uint64_t now = MP_NowMs();
    if(c.op == VCMP_PICK)
    {
        if(c.idx >= m_VcmpClasses.size()) return;
        const PktVcmpClass& k = m_VcmpClasses[c.idx];
        const bool changed = m_VcmpTeam[slot] != k.team;
        m_VcmpTeam[slot] = k.team;
        m_Slots[slot].classSkin = k.skin;
        m_Slots[slot].spawnProtUntil = now + 5000;   // VC:MP-like spawn protection
        if(changed)
        {
            PktVcmpCmd t = { PKT_VCMP, VCMP_PTEAM, (uint8_t)slot, k.team };
            Broadcast(&t, sizeof(t), -1);
            const char* tn = k.team < VCMP_MAX_TEAMS && m_VcmpInfo.teamName[k.team][0] ? m_VcmpInfo.teamName[k.team] : nullptr;
            if(tn) Log("%s joined team %s", m_Slots[slot].name, tn); else Log("%s picked class %d", m_Slots[slot].name, c.idx + 1);
        }
    }
    else if(c.op == VCMP_TAKE || c.op == VCMP_FREE)
    {
        if(c.idx >= m_VcmpVehs.size()) return;
        VcmpVeh& v = m_VcmpVehs[c.idx];
        if(c.op == VCMP_TAKE)
        {
            if(v.v.state == VCMP_TAKEN && v.v.holder != slot && v.v.holder < MP_MAX_PLAYERS && m_Slots[v.v.holder].used && !v.freeAt) return;   // first one wins
            v.v.state = VCMP_TAKEN; v.v.holder = (uint8_t)slot; v.freeAt = 0;
            Broadcast(&v.v, sizeof(v.v), -1);
        }
        else if(v.v.state == VCMP_TAKEN && v.v.holder == slot && !v.freeAt) v.freeAt = now + m_nVcmpRespawnMs;
    }
}

void MPServer::VcmpTick(uint64_t now)
{
    if(!m_bVcmp) return;
    for(auto& v : m_VcmpVehs)
    {
        if(v.v.state != VCMP_TAKEN) continue;
        if(!v.freeAt && (v.v.holder >= MP_MAX_PLAYERS || !m_Slots[v.v.holder].used)) v.freeAt = now + m_nVcmpRespawnMs;
        if(v.freeAt && now >= v.freeAt)
        {   // back on its spawn
            v.v.state = VCMP_PARKED; v.v.holder = MP_INVALID_ID; v.freeAt = 0;
            Broadcast(&v.v, sizeof(v.v), -1);
        }
    }
}

void MPServer::VcmpLeave(int slot)
{
    if(!m_bVcmp || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    m_VcmpTeam[slot] = VCMP_TEAM_NONE;
    const uint64_t now = MP_NowMs();
    for(auto& v : m_VcmpVehs) if(v.v.state == VCMP_TAKEN && v.v.holder == slot && !v.freeAt) v.freeAt = now + m_nVcmpRespawnMs;
}

// no friendly fire: two players of the same team (VC:MP Teams) don't hurt each other
bool MPServer::VcmpOnDamage(int from, int to) const
{
    if(!m_bVcmp || from < 0 || to < 0 || from >= MP_MAX_PLAYERS || to >= MP_MAX_PLAYERS) return true;
    return !(m_VcmpTeam[from] != VCMP_TEAM_NONE && m_VcmpTeam[from] == m_VcmpTeam[to]);
}
