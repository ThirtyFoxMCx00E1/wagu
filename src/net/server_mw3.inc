// MP 3.23 - Call Of Duty: Modern Warfare 3 (MW3) 24/7 featured server, the server side.
// Included by server.cpp. Every method here runs with m_Mutex held (Start / Tick / packet handler).
//
// The server owns the whole gamemode: teams + balance, classes (rank locked), ranks, score / XP /
// cash, the 34 capture zones, killing sprees + multi kill badges, the hourly tournament, the 6 DM
// arenas, duels and the 15 min events in the LS Docks. The phones only draw it and spawn / teleport
// themselves when the server tells them to. Stats are saved per account in mw3_stats.txt.

#include <stdarg.h>

static const uint32_t MW3_GREEN  = 0x3CFF6AFF;
static const uint32_t MW3_YELLOW = 0xFFE14AFF;
static const uint32_t MW3_RED    = 0xFF4A4AFF;
static const uint32_t MW3_GREY   = 0xC8C8C8FF;

bool MPServer::IsMw3Server() const { return MP_IsMw3ServerName(m_szName); }

void MPServer::SetMw3File(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szMw3File, path && *path ? path : "mw3_stats.txt", sizeof(m_szMw3File));
    if(IsMw3Server()) Mw3Load();
}

void MPServer::Mw3Reset()
{
    memset(m_Mw3, 0, sizeof(m_Mw3));
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { m_Mw3[i].duelAsk = m_Mw3[i].duelWith = MP_INVALID_ID; }
    memset(m_Mw3Zone, 0, sizeof(m_Mw3Zone));
    for(int z = 0; z < MW3_ZONES_WIRE; ++z) m_Mw3Zone[z].capper = MP_INVALID_ID;
    uint64_t now = MP_NowMs();
    m_nMw3LastSend = m_nMw3LastPlayers = m_nMw3LastSave = 0;
    m_nMw3TourneyStart = now;
    m_nMw3NextEvent = now + 5ull * 60ull * 1000ull;   // the first event 5 min after the start
    m_nMw3EvAt = 0;
    m_nMw3ZonePay = now + 10ull * 60ull * 1000ull;
    m_nMw3TKind = 0; m_nMw3EvKind = 0; m_nMw3EvState = MW3ES_IDLE; m_nMw3EvNextKind = MW3E_TDM;
    m_nMw3EvSideKills[0] = m_nMw3EvSideKills[1] = 0;
    m_bMw3Dirty = false;
}

// ---- persistence ----
// "P <key> score xp cash kills deaths caps best team cls" and "Z <zone> <owner>"
void MPServer::Mw3Load()
{
    m_Mw3Saved.clear();
    FILE* f = fopen(m_szMw3File, "r");
    if(!f) return;
    char line[256];
    int np = 0;
    while(fgets(line, sizeof(line), f))
    {
        if(line[0] == 'Z')
        {
            int z = -1, o = 0;
            if(sscanf(line + 1, "%d %d", &z, &o) == 2 && z >= 0 && z < MW3_ZONE_COUNT && o >= 0 && o <= MW3_TEAMS) m_Mw3Zone[z].owner = (uint8_t)o;
        }
        else if(line[0] == 'P')
        {
            char key[64] = { 0 };
            int sc = 0, xp = 0, cash = 0, k = 0, d = 0, c = 0, b = 0, t = 0, cl = 0; unsigned int ts = 0;
            if(sscanf(line + 1, "%63s %d %d %d %d %d %d %d %d %d %u", key, &sc, &xp, &cash, &k, &d, &c, &b, &t, &cl, &ts) >= 8)
            {
                Mw3Saved v;
                v.score = sc; v.xp = xp; v.cash = cash;
                v.kills = (uint16_t)k; v.deaths = (uint16_t)d; v.caps = (uint16_t)c; v.best = (uint16_t)b;
                v.team = (uint8_t)(t >= 0 && t <= MW3_TEAMS ? t : 0); v.cls = (uint8_t)(cl >= 0 && cl < MW3_CLASSES ? cl : 0);
                v.t = ts;   // 3.24 (0 = an older file: any card wins)
                m_Mw3Saved[key] = v; ++np;
            }
        }
    }
    fclose(f);
    Log("MW3: loaded %d players from %s", np, m_szMw3File);
}

void MPServer::Mw3Save()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) Mw3Store(i);
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", m_szMw3File);
    FILE* f = fopen(tmp, "w");
    if(!f) return;
    for(int z = 0; z < MW3_ZONE_COUNT; ++z) fprintf(f, "Z %d %d\n", z, m_Mw3Zone[z].owner);
    for(auto& kv : m_Mw3Saved)
    {
        const Mw3Saved& v = kv.second;
        fprintf(f, "P %s %d %d %d %d %d %d %d %d %d %u\n", kv.first.c_str(), v.score, v.xp, v.cash, v.kills, v.deaths, v.caps, v.best, v.team, v.cls, v.t);
    }
    fclose(f);
    rename(tmp, m_szMw3File);
    m_bMw3Dirty = false;
}

// account when logged in (login is always on for MW3), never the plain name
const char* MPServer::Mw3Key(int slot, char* buf, size_t n)
{
    const Slot& s = m_Slots[slot];
    if(!s.authed || !s.account[0]) return nullptr;
    size_t j = 0;
    for(const char* p = s.account; *p && j + 1 < n; ++p) buf[j++] = (*p == ' ' || *p == '\t') ? '_' : (char)tolower((unsigned char)*p);
    buf[j] = 0;
    return buf;
}

void MPServer::Mw3Store(int slot)
{
    const Mw3SrvPlayer& m = m_Mw3[slot];
    if(!m.loaded) return;
    char kb[64];
    const char* key = Mw3Key(slot, kb, sizeof(kb));
    if(!key) return;
    Mw3Saved v;
    v.score = m.score; v.xp = m.xp; v.cash = m.cash; v.kills = m.kills; v.deaths = m.deaths; v.caps = m.caps; v.best = m.best; v.team = m.team; v.cls = m.cls;
    v.t = m.statT;
    m_Mw3Saved[key] = v;
    m_bMw3Dirty = true;
}

// ---- packets ----
int MPServer::Mw3TeamCount(int team) const
{
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].team == team) ++n;
    return n;
}

uint16_t MPServer::Mw3TourneyVal(int slot) const
{
    const Mw3SrvPlayer& m = m_Mw3[slot];
    switch(m_nMw3TKind)
    {
    case MW3T_KILLS:  return m.hKills;
    case MW3T_CAPS:   return m.hCaps;
    case MW3T_DEAGLE: return m.hDeagle;
    default:          return m.hSpree;
    }
}

int MPServer::Mw3EvAlive(int side) const
{
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        if(m_Slots[i].used && m_Mw3[i].evIn && m_Mw3[i].arena == MW3A_EVENT && (side == 0 || m_Mw3[i].sub == side)) ++n;
    return n;
}

void MPServer::Mw3SendState(int slot)
{
    PktMw3State p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_MW3_STATE;
    uint64_t now = MP_NowMs();
    p.tKind = m_nMw3TKind;
    uint64_t tEnd = m_nMw3TourneyStart + MW3_TOURNEY_MS;
    p.tLeftS = (uint16_t)(tEnd > now ? (tEnd - now) / 1000 : 0);
    p.tLeader = MP_INVALID_ID;
    uint16_t best = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        if(m_Slots[i].used) { uint16_t v = Mw3TourneyVal(i); if(v > best) { best = v; p.tLeader = (uint8_t)i; } }
    p.tLeaderVal = best;
    p.evKind = m_nMw3EvState == MW3ES_IDLE ? m_nMw3EvNextKind : m_nMw3EvKind;
    p.evState = m_nMw3EvState;
    if(m_nMw3EvState == MW3ES_IDLE)
        p.evLeftS = (uint16_t)(m_nMw3NextEvent > now ? (m_nMw3NextEvent - now) / 1000 : 0);
    else
    {
        uint64_t end = m_nMw3EvAt + (m_nMw3EvState == MW3ES_SIGNUP ? MW3_EVENT_SIGNUP_MS : MW3_EVENT_MAX_MS);
        p.evLeftS = (uint16_t)(end > now ? (end - now) / 1000 : 0);
    }
    int signed_ = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].evIn) ++signed_;
    p.evCount = (uint8_t)(m_nMw3EvState == MW3ES_LIVE ? Mw3EvAlive(0) : signed_);
    if(m_nMw3EvState == MW3ES_LIVE)
    {
        if(m_nMw3EvKind == MW3E_TDM) { p.evSide[0] = (uint8_t)(m_nMw3EvSideKills[0] > 255 ? 255 : m_nMw3EvSideKills[0]); p.evSide[1] = (uint8_t)(m_nMw3EvSideKills[1] > 255 ? 255 : m_nMw3EvSideKills[1]); }
        else { p.evSide[0] = (uint8_t)Mw3EvAlive(1); p.evSide[1] = (uint8_t)Mw3EvAlive(2); }
    }
    for(int t = 1; t <= MW3_TEAMS; ++t) p.teamCount[t - 1] = (uint8_t)Mw3TeamCount(t);
    for(int z = 0; z < MW3_ZONES_WIRE; ++z)
    {
        const Mw3SrvZone& Z = m_Mw3Zone[z];
        p.zoneOwner[z] = Z.owner;
        p.zoneAtk[z] = Z.atk;
        p.zoneCapper[z] = Z.atk ? Z.capper : MP_INVALID_ID;
        if(Z.atk && z < MW3_ZONE_COUNT)
        {
            float f = (float)(now - Z.capStart) / Mw3CapMs(z);
            p.zoneProg[z] = (uint8_t)(f >= 1.0f ? 255 : (f <= 0 ? 0 : f * 255.0f));
        }
    }
    if(slot >= 0) SendTo(slot, &p, sizeof(p)); else Broadcast(&p, sizeof(p), -1);
}

void MPServer::Mw3SendPlayers(int slot)
{
    PktMw3Players p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_MW3_PLAYERS;
    for(int i = 0; i < MP_MAX_PLAYERS && p.n < MW3_ROWS_WIRE; ++i)
    {
        if(!m_Slots[i].used) continue;
        const Mw3SrvPlayer& m = m_Mw3[i];
        Mw3Row& r = p.rows[p.n++];
        r.id = (uint8_t)i; r.team = m.team; r.cls = m.cls; r.rank = m.rank ? m.rank : 1; r.arena = m.arena;
        r.sub = m.arena == MW3A_DUEL ? (uint8_t)(m.duelWith + 1) : m.sub;
        r.score = m.score;
    }
    int len = 2 + p.n * (int)sizeof(Mw3Row);
    if(slot >= 0) SendTo(slot, &p, len); else Broadcast(&p, len, -1);
}

void MPServer::Mw3SendMe(int slot)
{
    const Mw3SrvPlayer& m = m_Mw3[slot];
    PktMw3Me p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_MW3_ME;
    p.rank = m.rank ? m.rank : 1; p.spree = m.spree; p.score = m.score; p.xp = m.xp; p.cash = m.cash;
    p.kills = m.kills; p.deaths = m.deaths; p.caps = m.caps; p.deagle = m.deagle; p.best = m.best;
    SendTo(slot, &p, sizeof(p));
    Mw3SendCard(slot);
}

// ---- 3.24: stats card (signed backup on the player's phone) ----
static void Mw3CardKey(uint8_t out[32])
{
    static const uint8_t a[32] = { 0x32, 0x8C, 0x7E, 0xE9, 0xF4, 0x80, 0xBB, 0x20, 0xEC, 0xF5, 0x12, 0x5F, 0x47, 0x60, 0xD2, 0x13, 0x0B, 0x83, 0x9C, 0xB8, 0xA8, 0x6B, 0x03, 0x76, 0x9A, 0xA1, 0xFA, 0xF9, 0x6B, 0xA2, 0x60, 0x46 };
    static const uint8_t b[32] = { 0xE2, 0x44, 0xA6, 0xDE, 0x32, 0x43, 0x1E, 0xC4, 0x46, 0xDC, 0xB3, 0x05, 0xB0, 0x26, 0x56, 0xBE, 0x36, 0x52, 0x8C, 0xD5, 0x6C, 0xB6, 0x4D, 0x76, 0xBA, 0x1C, 0xA2, 0xD5, 0xA7, 0x93, 0x2C, 0xC0 };
    for(int i = 0; i < 32; ++i) out[i] = a[i] ^ b[i];
}
static void Mw3CardMac(const PktMw3Card& c, uint8_t mac[16])
{   // HMAC-SHA256 over everything before mac[]
    uint8_t k[32], ipad[64], opad[64], inner[32], full[32];
    Mw3CardKey(k);
    memset(ipad, 0x36, 64); memset(opad, 0x5C, 64);
    for(int i = 0; i < 32; ++i) { ipad[i] ^= k[i]; opad[i] ^= k[i]; }
    mbedtls_sha256_context h;
    mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    mbedtls_sha256_update(&h, ipad, 64); mbedtls_sha256_update(&h, (const unsigned char*)&c, offsetof(PktMw3Card, mac));
    mbedtls_sha256_finish(&h, inner); mbedtls_sha256_free(&h);
    mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    mbedtls_sha256_update(&h, opad, 64); mbedtls_sha256_update(&h, inner, 32);
    mbedtls_sha256_finish(&h, full); mbedtls_sha256_free(&h);
    memcpy(mac, full, 16);
}

void MPServer::Mw3SendCard(int slot)
{
    Mw3SrvPlayer& m = m_Mw3[slot];
    if(!m.loaded) return;
    if(m.score != m.lastSc || m.xp != m.lastXp || m.cash != m.lastCash || m.kills != m.lastK || m.deaths != m.lastD || m.caps != m.lastC)
    {
        m.statT = (uint32_t)time(nullptr);
        m.lastSc = m.score; m.lastXp = m.xp; m.lastCash = m.cash; m.lastK = m.kills; m.lastD = m.deaths; m.lastC = m.caps;
        m_bMw3Dirty = true;
    }
    if(m.cardT == 0xFFFFFFFFu)
    {   // waiting for the phone's own card first (so an older line here never overwrites its backup)
        if(MP_NowMs() - m.cardWaitAt < 4000) return;
        m.cardT = 0;
    }
    if(m.cardT == m.statT && m.cardT) return;   // the phone already has these stats
    char kb[64];
    const char* key = Mw3Key(slot, kb, sizeof(kb));
    if(!key || strlen(key) >= sizeof(PktMw3Card::key)) return;
    if(!m.statT) return;   // nothing earned yet: never send a t=0 card (it could not win anyway)
    PktMw3Card c; memset(&c, 0, sizeof(c));
    c.type = PKT_MW3_CARD; c.ver = MW3_CARD_VER; c.team = m.team; c.cls = m.cls; c.t = m.statT;
    MP_CopyStr(c.key, key, sizeof(c.key));
    c.score = m.score; c.xp = m.xp; c.cash = m.cash; c.kills = m.kills; c.deaths = m.deaths; c.caps = m.caps; c.best = m.best;
    Mw3CardMac(c, c.mac);
    SendTo(slot, &c, sizeof(c));
    m.cardT = m.statT;
}

// the phone gave us its backup: newer than what this host knows = restore it
void MPServer::Mw3OnCard(int slot, const PktMw3Card& in)
{
    if(!IsMw3Server()) return;
    Mw3SrvPlayer& m = m_Mw3[slot];
    if(!m.loaded || in.ver != MW3_CARD_VER) return;
    PktMw3Card c = in; c.key[sizeof(c.key) - 1] = 0;
    uint8_t mac[16]; Mw3CardMac(c, mac);
    int diff = 0; for(int i = 0; i < 16; ++i) diff |= mac[i] ^ c.mac[i];
    if(diff) { Log("MW3: %s sent a bad stats card (ignored)", m_Slots[slot].name); return; }
    char kb[64];
    const char* key = Mw3Key(slot, kb, sizeof(kb));
    if(!key || strcmp(key, c.key)) return;                          // someone else's card
    uint32_t now = (uint32_t)time(nullptr);
    if(c.t > now + 600) return;                                      // from the future
    if(c.t <= m.statT) { if(m.cardT == 0xFFFFFFFFu) m.cardT = 0; Mw3SendCard(slot); return; }   // ours is newer: the phone gets ours
    m.score = c.score; m.xp = c.xp; m.cash = c.cash; m.kills = c.kills; m.deaths = c.deaths; m.caps = c.caps; m.best = c.best;
    m.rank = (uint8_t)Mw3RankOf(m.score);
    if(c.cls < MW3_CLASSES && g_Mw3Classes[c.cls].rank <= m.rank) m.cls = c.cls;
    m.statT = c.t; m.cardT = c.t;
    m.lastSc = m.score; m.lastXp = m.xp; m.lastCash = m.cash; m.lastK = m.kills; m.lastD = m.deaths; m.lastC = m.caps;
    Mw3Store(slot);
    Mw3Msg(slot, MW3_GREEN, "[MW3] Stats restored from your backup: rank %s, score %d, kills %d.", g_Mw3RankName[m.rank], m.score, m.kills);
    Log("MW3: restored %s from their stats card (score %d)", c.key, m.score);
    Mw3SendMe(slot);
    Mw3SendPlayers(-1);
}

void MPServer::Mw3Ev(int slot, uint8_t ev, uint8_t a, uint8_t b, uint8_t c, uint8_t who, int32_t val, const char* text)
{
    PktMw3Event p;
    memset(&p, 0, sizeof(p));
    p.type = PKT_MW3_EVENT; p.ev = ev; p.a = a; p.b = b; p.c = c; p.who = who; p.val = val;
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    if(slot >= 0) SendTo(slot, &p, sizeof(p)); else Broadcast(&p, sizeof(p), -1);
}

void MPServer::Mw3Msg(int slot, uint32_t rgba, const char* fmt, ...)
{
    PktServerMsg m;
    memset(&m, 0, sizeof(m));
    m.type = PKT_SERVER_MSG;
    m.color = rgba;
    va_list ap; va_start(ap, fmt);
    vsnprintf(m.text, sizeof(m.text), fmt, ap);
    va_end(ap);
    if(slot >= 0) SendTo(slot, &m, sizeof(m)); else Broadcast(&m, sizeof(m), -1);
}

void MPServer::Mw3Reward(int slot, int score, int cash, int xp)
{
    Mw3SrvPlayer& m = m_Mw3[slot];
    int old = m.rank ? m.rank : Mw3RankOf(m.score);
    m.score += score; if(m.score < 0) m.score = 0;
    m.cash += cash; m.xp += xp;
    m.rank = (uint8_t)Mw3RankOf(m.score);
    Mw3Ev(slot, MW3V_REWARD, (uint8_t)(score < 0 ? 0 : (score > 255 ? 255 : score)), (uint8_t)(xp < 0 ? 0 : (xp > 255 ? 255 : xp)), 0, MP_INVALID_ID, cash);
    if(m.rank > old)
    {
        Mw3Ev(slot, MW3V_RANKUP, m.rank, 0, 0, (uint8_t)slot, 0, g_Mw3RankName[m.rank]);
        Mw3Msg(-1, MW3_GREEN, "[MW3] %s has been promoted to %s (rank %d)", m_Slots[slot].name, g_Mw3RankName[m.rank], m.rank);
        for(int c = 0; c < MW3_CLASSES; ++c)
            if(g_Mw3Classes[c].rank > old && g_Mw3Classes[c].rank <= m.rank) Mw3Msg(slot, MW3_GREEN, "[MW3] New class unlocked: %s", g_Mw3Classes[c].name);
    }
    m_nMw3LastPlayers = 0;   // send the player list soon
    Mw3Store(slot);
}

// ---- join / leave ----
void MPServer::Mw3OnJoin(int slot, bool fresh)
{
    if(!IsMw3Server()) return;
    if(fresh)
    {
        memset(&m_Mw3[slot], 0, sizeof(m_Mw3[slot]));
        m_Mw3[slot].duelAsk = m_Mw3[slot].duelWith = MP_INVALID_ID;
        m_Mw3[slot].rank = 1;
    }
    Mw3SendState(slot);
    Mw3SendPlayers(-1);
    if(m_Slots[slot].authed) Mw3OnAuthed(slot);
}

void MPServer::Mw3OnAuthed(int slot)
{
    if(!IsMw3Server()) return;
    Mw3SrvPlayer& m = m_Mw3[slot];
    if(!m.loaded)
    {
        char kb[64];
        const char* key = Mw3Key(slot, kb, sizeof(kb));
        if(key)
        {
            auto it = m_Mw3Saved.find(key);
            if(it != m_Mw3Saved.end())
            {
                const Mw3Saved& v = it->second;
                m.score = v.score; m.xp = v.xp; m.cash = v.cash; m.kills = v.kills; m.deaths = v.deaths; m.caps = v.caps; m.best = v.best;
                m.cls = g_Mw3Classes[v.cls].rank <= Mw3RankOf(v.score) ? v.cls : 0;
                m.statT = v.t;
                Mw3Msg(slot, MW3_GREEN, "[MW3] Welcome back, %s. Rank: %s, score %d.", m_Slots[slot].name, g_Mw3RankName[Mw3RankOf(v.score)], v.score);
            }
            else Mw3Msg(slot, MW3_GREEN, "[MW3] Welcome to Call Of Duty: Modern Warfare 3, %s. Pick a team and a class.", m_Slots[slot].name);
        }
        m.rank = (uint8_t)Mw3RankOf(m.score);
        m.lastSc = m.score; m.lastXp = m.xp; m.lastCash = m.cash; m.lastK = m.kills; m.lastD = m.deaths; m.lastC = m.caps;
        m.cardT = 0xFFFFFFFFu;   // 3.24: no card until the phone had 4 s to send its own (newer?) backup
        m.cardWaitAt = MP_NowMs();
        m.loaded = true;
    }
    Mw3SendMe(slot);
    Mw3SendPlayers(-1);
    Mw3SendState(slot);
}

void MPServer::Mw3OnLeave(int slot)
{
    if(!IsMw3Server()) return;
    Mw3SrvPlayer& m = m_Mw3[slot];
    if(m.evIn) Mw3EventLeave(slot, false);
    if(m.arena == MW3A_DUEL && m.duelWith < MP_MAX_PLAYERS)
    {
        int o = m.duelWith;
        if(m_Slots[o].used && m_Mw3[o].duelWith == slot)
        {
            Mw3Msg(-1, MW3_YELLOW, "[DUEL] %s left the duel, %s wins.", m_Slots[slot].name, m_Slots[o].name);
            m_Mw3[o].arena = MW3A_NONE; m_Mw3[o].duelWith = MP_INVALID_ID;
            Mw3Reward(o, 5, 10000, 5);
            Mw3Ev(o, MW3V_ARENA, MW3A_NONE, 0, 0, MP_INVALID_ID, 0);
        }
    }
    for(int z = 0; z < MW3_ZONES_WIRE; ++z)
        if(m_Mw3Zone[z].atk && m_Mw3Zone[z].capper == slot) { m_Mw3Zone[z].atk = 0; m_Mw3Zone[z].capper = MP_INVALID_ID; }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Mw3[i].duelAsk == slot) m_Mw3[i].duelAsk = MP_INVALID_ID;
    Mw3Store(slot);
    if(m_bMw3Dirty) Mw3Save();   // 3.24: on the disk right away (a phone host can be closed any second)
    memset(&m, 0, sizeof(m));
    m.duelAsk = m.duelWith = MP_INVALID_ID;
    m_nMw3LastPlayers = 0;
}

// ---- spawn ----
void MPServer::Mw3Spawned(int slot)
{
    Mw3SrvPlayer& m = m_Mw3[slot];
    m.spawned = true;
    m.spawnedAt = MP_NowMs();
}

// ---- commands from the phone ----
void MPServer::Mw3HandleCmd(int slot, const PktMw3Cmd& c)
{
    if(!IsMw3Server() || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    const Slot& s = m_Slots[slot];
    Mw3SrvPlayer& m = m_Mw3[slot];
    if(!s.authed) return;
    uint64_t now = MP_NowMs();
    switch(c.cmd)
    {
    case MW3C_TEAM:
    {
        int t = c.arg;
        if(t < 1 || t > MW3_TEAMS) break;
        if(t == m.team) { Mw3SendPlayers(slot); break; }
        // any team, right away: no queue, no balance lock
        if(m.evIn) Mw3EventLeave(slot, false);
        m.team = (uint8_t)t;
        m.spawned = false;
        Mw3Store(slot);
        Mw3SendPlayers(-1);
        break;
    }
    case MW3C_CLASS:
    {
        int k = c.arg;
        if(k >= MW3_CLASSES) break;
        int rank = Mw3RankOf(m.score);
        if(g_Mw3Classes[k].rank > rank) { Mw3Msg(slot, MW3_RED, "[MW3] %s needs rank %d (%s).", g_Mw3Classes[k].name, g_Mw3Classes[k].rank, g_Mw3RankName[g_Mw3Classes[k].rank]); break; }
        m.cls = (uint8_t)k;
        Mw3Store(slot);
        Mw3SendPlayers(-1);
        break;
    }
    case MW3C_SPAWNED:
        if(!m.team) break;
        Mw3Spawned(slot);
        m_nMw3LastPlayers = 0;
        break;
    case MW3C_ARENA:
    {
        int a = c.arg;
        if(a > MW3A_HSDM) break;
        if(m.arena == MW3A_EVENT || m.arena == MW3A_DUEL) { Mw3Msg(slot, MW3_RED, "[MW3] Leave the event / duel first."); break; }
        if(a == m.arena) break;
        m.arena = (uint8_t)a;
        m.spawned = false;
        int sp = a ? (g_Mw3Arenas[a].nsp ? rand() % g_Mw3Arenas[a].nsp : 0) : 0;
        Mw3Ev(slot, MW3V_ARENA, (uint8_t)a, 0, (uint8_t)sp, MP_INVALID_ID, 0);
        if(a) Mw3Msg(-1, MW3_GREY, "[DM] %s joined the %s (/%s).", s.name, g_Mw3Arenas[a].name, g_Mw3Arenas[a].cmd);
        for(int z = 0; z < MW3_ZONES_WIRE; ++z)
            if(m_Mw3Zone[z].atk && m_Mw3Zone[z].capper == slot) { m_Mw3Zone[z].atk = 0; m_Mw3Zone[z].capper = MP_INVALID_ID; Mw3Ev(-1, MW3V_CAP_FAIL, (uint8_t)z, 0, 0, (uint8_t)slot, 0); }
        m_nMw3LastPlayers = 0;
        break;
    }
    case MW3C_DUEL:
    {
        int t = c.arg, w = c.arg2;
        if(t >= MP_MAX_PLAYERS || t == slot || !m_Slots[t].used || !m_Slots[t].authed) { Mw3Msg(slot, MW3_RED, "[DUEL] That player is not here."); break; }
        if(m.arena != MW3A_NONE || m_Mw3[t].arena != MW3A_NONE) { Mw3Msg(slot, MW3_RED, "[DUEL] You both must be out of the DM arenas, events and duels."); break; }
        if(w < 1 || w > 38 || (w >= 19 && w <= 21)) w = 24;
        m_Mw3[t].duelAsk = (uint8_t)slot; m_Mw3[t].duelAskW = (uint8_t)w; m_Mw3[t].duelAskAt = now;
        Mw3Msg(t, MW3_YELLOW, "[DUEL] %s challenged you to a duel (weapon %d). Type /accept within 30 seconds.", s.name, w);
        Mw3Ev(t, MW3V_TEXT, 0, 0, 0, (uint8_t)slot, (int32_t)MW3_YELLOW, "DUEL REQUEST - /accept");
        Mw3Msg(slot, MW3_YELLOW, "[DUEL] Request sent to %s.", m_Slots[t].name);
        break;
    }
    case MW3C_ACCEPT:
    {
        int a = m.duelAsk;
        if(a >= MP_MAX_PLAYERS || !m_Slots[a].used || now - m.duelAskAt > 30000) { m.duelAsk = MP_INVALID_ID; Mw3Msg(slot, MW3_RED, "[DUEL] Nobody challenged you (or it expired)."); break; }
        if(m.arena != MW3A_NONE || m_Mw3[a].arena != MW3A_NONE) { Mw3Msg(slot, MW3_RED, "[DUEL] One of you is busy in an arena or event."); break; }
        int w = m.duelAskW ? m.duelAskW : 24;
        m.duelAsk = MP_INVALID_ID;
        m.arena = m_Mw3[a].arena = MW3A_DUEL;
        m.duelWith = (uint8_t)a; m_Mw3[a].duelWith = (uint8_t)slot;
        m.spawned = m_Mw3[a].spawned = false;
        Mw3Ev(a, MW3V_DUEL_START, (uint8_t)w, 0, 0, (uint8_t)slot, 0);
        Mw3Ev(slot, MW3V_DUEL_START, (uint8_t)w, 0, 1, (uint8_t)a, 0);
        Mw3Msg(-1, MW3_YELLOW, "[DUEL] %s vs %s (weapon %d) has started.", m_Slots[a].name, s.name, w);
        m_nMw3LastPlayers = 0;
        break;
    }
    case MW3C_JOIN_EVENT:
        if(m_nMw3EvState != MW3ES_SIGNUP) { Mw3Msg(slot, MW3_RED, "[EVENT] There is no event to join right now."); break; }
        if(m.evIn) { Mw3Msg(slot, MW3_GREY, "[EVENT] You are already signed up."); break; }
        if(m.arena == MW3A_DUEL) { Mw3Msg(slot, MW3_RED, "[EVENT] Finish your duel first."); break; }
        m.evIn = true;
        Mw3Msg(slot, MW3_GREEN, "[EVENT] You joined the %s. It starts when the sign-up ends.", g_Mw3EventName[m_nMw3EvKind]);
        break;
    case MW3C_LEAVE_EVENT:
        if(m.evIn) Mw3EventLeave(slot, true);
        break;
    case MW3C_START_EVENT:
        if(!s.admin) { Mw3Msg(slot, MW3_RED, "[EVENT] Only the admin can start an event."); break; }
        if(m_nMw3EvState != MW3ES_IDLE) { Mw3Msg(slot, MW3_RED, "[EVENT] An event is already running."); break; }
        Mw3EventStart(c.arg >= MW3E_TDM && c.arg < MW3E_KINDS ? c.arg : m_nMw3EvNextKind);
        break;
    case MW3C_HEAL:
    {
        if(!(g_Mw3Classes[m.cls].flags & MW3CF_MEDIC)) { Mw3Msg(slot, MW3_RED, "[MW3] Only the Medic class can heal."); break; }
        if(!m.spawned || m.arena != MW3A_NONE) break;
        if(now - m.lastHeal < 30000) { Mw3Msg(slot, MW3_GREY, "[MW3] Heal is ready in %d s.", (int)((30000 - (now - m.lastHeal)) / 1000)); break; }
        m.lastHeal = now;
        int healed = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(i == slot || !m_Slots[i].used || !m_Slots[i].hasPos || m_Mw3[i].team != m.team || m_Mw3[i].arena != MW3A_NONE || !m_Mw3[i].spawned) continue;
            float dx = m_Slots[i].pos[0] - s.pos[0], dy = m_Slots[i].pos[1] - s.pos[1], dz = m_Slots[i].pos[2] - s.pos[2];
            if(dx * dx + dy * dy + dz * dz > 10.0f * 10.0f) continue;
            Mw3Ev(i, MW3V_HEAL, 0, 0, 0, (uint8_t)slot, 100);
            Mw3Msg(i, MW3_GREEN, "[MW3] Medic %s healed you.", s.name);
            ++healed;
        }
        Mw3Ev(slot, MW3V_HEAL, 0, 0, 0, (uint8_t)slot, 100);
        if(healed) Mw3Reward(slot, healed > 3 ? 3 : healed, 500 * healed, 2 * healed);
        Mw3Msg(slot, MW3_GREEN, "[MW3] You healed %d team mate%s.", healed, healed == 1 ? "" : "s");
        break;
    }
    case MW3C_DIED_OUT:
    default: break;
    }
}

// ---- damage filter (true = deliver it) ----
bool MPServer::Mw3OnDamage(int from, int target, float amount)
{
    (void)amount;
    if(!IsMw3Server()) return true;
    if(from < 0 || from >= MP_MAX_PLAYERS || target < 0 || target >= MP_MAX_PLAYERS) return false;
    const Mw3SrvPlayer& A = m_Mw3[from];
    const Mw3SrvPlayer& T = m_Mw3[target];
    if(!A.team || !T.team || !T.spawned) return false;
    if(A.arena != T.arena) return false;
    uint64_t now = MP_NowMs();
    if(now - T.spawnedAt < MW3_SPAWN_PROTECT_MS) return false;
    switch(T.arena)
    {
    case MW3A_NONE:
        if(A.team == T.team) return false;   // no friendly fire
        if(m_Slots[target].hasPos && Mw3InBase(T.team, m_Slots[target].pos[0], m_Slots[target].pos[1])) return false;   // safe in your own base
        if(m_Slots[from].hasPos && Mw3InBase(A.team, m_Slots[from].pos[0], m_Slots[from].pos[1])) return false;       // no base camping
        return true;
    case MW3A_EVENT:
        if(m_nMw3EvState != MW3ES_LIVE || !A.evIn || !T.evIn) return false;
        if(m_nMw3EvKind == MW3E_TDM || m_nMw3EvKind == MW3E_ZOMBIE) return A.sub != T.sub;
        return true;
    case MW3A_DUEL:
        return A.duelWith == target && T.duelWith == from;
    default:
        return true;   // DM arenas: everybody vs everybody
    }
}

// ---- deaths ----
void MPServer::Mw3OnDeath(int victim, int killer, int weapon)
{
    if(!IsMw3Server() || victim < 0 || victim >= MP_MAX_PLAYERS) return;
    Mw3SrvPlayer& V = m_Mw3[victim];
    uint64_t now = MP_NowMs();
    if(!V.team) return;
    bool wasSpawned = V.spawned;
    V.spawned = false;
    if(wasSpawned) V.deaths++;
    uint16_t vSpree = V.spree;
    V.spree = 0; V.multi = 0;
    // captures die with you
    for(int z = 0; z < MW3_ZONES_WIRE; ++z)
        if(m_Mw3Zone[z].atk && m_Mw3Zone[z].capper == victim)
        {
            m_Mw3Zone[z].atk = 0; m_Mw3Zone[z].capper = MP_INVALID_ID;
            Mw3Ev(-1, MW3V_CAP_FAIL, (uint8_t)z, 0, 0, (uint8_t)victim, 0);
            Mw3Msg(-1, MW3_GREY, "[MW3] %s failed to capture %s.", m_Slots[victim].name, g_Mw3Zones[z].name);
        }
    bool kOk = killer >= 0 && killer < MP_MAX_PLAYERS && killer != victim && m_Slots[killer].used && m_Mw3[killer].team;
    Mw3SrvPlayer* K = kOk ? &m_Mw3[killer] : nullptr;
    if(K && K->arena != V.arena) { K = nullptr; kOk = false; }
    if(K && V.arena == MW3A_NONE && K->team == V.team) { K = nullptr; kOk = false; }

    // duel: the other one wins (even on a suicide)
    if(V.arena == MW3A_DUEL)
    {
        int o = V.duelWith;
        V.arena = MW3A_NONE; V.duelWith = MP_INVALID_ID;
        if(o < MP_MAX_PLAYERS && m_Slots[o].used && m_Mw3[o].duelWith == victim)
        {
            m_Mw3[o].arena = MW3A_NONE; m_Mw3[o].duelWith = MP_INVALID_ID; m_Mw3[o].spawned = false;
            Mw3Msg(-1, MW3_YELLOW, "[DUEL] %s won the duel against %s.", m_Slots[o].name, m_Slots[victim].name);
            Mw3Reward(o, 5, 10000, 5);
            Mw3Ev(o, MW3V_ARENA, MW3A_NONE, 0, 0, MP_INVALID_ID, 0);
        }
        m_nMw3LastPlayers = 0;
        return;
    }

    // events
    if(V.arena == MW3A_EVENT && m_nMw3EvState == MW3ES_LIVE && V.evIn)
    {
        if(K && K->evIn)
        {
            K->evKills++;
            if(m_nMw3EvKind == MW3E_TDM && K->sub >= 1 && K->sub <= 2) m_nMw3EvSideKills[K->sub - 1]++;
            Mw3Reward(killer, 1, 500, 2);
        }
        if(m_nMw3EvKind == MW3E_LMS)
        {
            Mw3Msg(-1, MW3_GREY, "[EVENT] %s is out (%d left).", m_Slots[victim].name, Mw3EvAlive(0) - 1);
            Mw3EventLeave(victim, false);
        }
        else if(m_nMw3EvKind == MW3E_ZOMBIE && V.sub == 1)
        {
            V.sub = 2;
            Mw3Ev(victim, MW3V_ZOMBIE, 0, 0, 0, (uint8_t)(K ? killer : MP_INVALID_ID), 0);
            Mw3Msg(-1, MW3_RED, "[EVENT] %s became a zombie (%d humans left).", m_Slots[victim].name, Mw3EvAlive(1));
            m_nMw3LastPlayers = 0;
        }
        return;   // no sprees / badges in events, the event pays its own reward
    }

    if(!K) { m_nMw3LastPlayers = 0; return; }
    // normal kill (battlefield or a DM arena)
    bool field = V.arena == MW3A_NONE;
    K->kills++; K->hKills++;
    if(weapon == 24) { K->deagle++; K->hDeagle++; }
    K->spree++;
    if(K->spree > K->best) K->best = K->spree;
    if(K->spree > K->hSpree) K->hSpree = K->spree;
    K->multi = (now - K->lastKill <= MW3_MULTI_MS) ? (uint8_t)(K->multi + 1) : 1;
    K->lastKill = now;
    Mw3Reward(killer, field ? 2 : 1, field ? 1500 : 1000, field ? 5 : 3);
    if(K->multi >= 2)
    {
        int lv = K->multi > 7 ? 7 : K->multi;
        Mw3Ev(killer, MW3V_BADGE, (uint8_t)lv, 0, 0, (uint8_t)victim, 0, g_Mw3Badge[lv]);
        if(lv >= 3) Mw3Msg(-1, MW3_YELLOW, "[MW3] %s: %s", m_Slots[killer].name, g_Mw3Badge[lv]);
        Mw3Reward(killer, lv - 1, 500 * lv, lv);
    }
    if(K->spree % 5 == 0)
    {
        Mw3Msg(-1, MW3_YELLOW, "[MW3] %s is on a killing spree of %d kills.", m_Slots[killer].name, K->spree);
        Mw3Ev(killer, MW3V_TEXT, 0, 0, 0, (uint8_t)killer, (int32_t)MW3_YELLOW, "KILLING SPREE");
        Mw3Reward(killer, K->spree / 5 * 3, 5000, 10);
    }
    if(vSpree >= 5)
    {
        Mw3Msg(-1, MW3_YELLOW, "[MW3] %s ended %s's killing spree of %d.", m_Slots[killer].name, m_Slots[victim].name, vSpree);
        Mw3Reward(killer, 3, 3000, 5);
    }
}

// ---- tick (10 Hz) ----
void MPServer::Mw3ZoneTick(uint64_t now)
{
    bool any = false;
    for(int z = 0; z < MW3_ZONE_COUNT; ++z)
    {
        Mw3SrvZone& Z = m_Mw3Zone[z];
        const Mw3Zone& D = g_Mw3Zones[z];
        if(Z.atk)
        {
            int c = Z.capper;
            bool ok = c < MP_MAX_PLAYERS && m_Slots[c].used && m_Slots[c].hasPos && m_Mw3[c].team == Z.atk && m_Mw3[c].spawned && m_Mw3[c].arena == MW3A_NONE
                   && !(m_Slots[c].lastFlags & (SYNC_DEAD | SYNC_IN_CAR))
                   && Mw3Dist2D(m_Slots[c].pos, D.cp[0], D.cp[1]) <= MW3_CAP_R && fabsf(m_Slots[c].pos[2] - D.cp[2]) < 4.0f;
            if(!ok)
            {
                if(c < MP_MAX_PLAYERS && m_Slots[c].used) Mw3Msg(-1, MW3_GREY, "[MW3] %s failed to capture %s.", m_Slots[c].name, D.name);
                Mw3Ev(-1, MW3V_CAP_FAIL, (uint8_t)z, 0, 0, (uint8_t)c, 0);
                Z.atk = 0; Z.capper = MP_INVALID_ID;
                continue;
            }
            any = true;
            if((float)(now - Z.capStart) >= Mw3CapMs(z))
            {
                int team = Z.atk;
                Z.owner = (uint8_t)team; Z.atk = 0; Z.capper = MP_INVALID_ID;
                Mw3SrvPlayer& P = m_Mw3[c];
                P.caps++; P.hCaps++;
                Mw3Reward(c, D.special ? 8 : 5, D.special ? 8000 : 5000, D.special ? 15 : 10);
                Mw3Ev(-1, MW3V_CAPTURED, (uint8_t)z, (uint8_t)team, 0, (uint8_t)c, 0, D.name);
                Mw3Msg(-1, g_Mw3Teams[team].rgba, "[MW3] %s (%s) captured %s for %s.", m_Slots[c].name, g_Mw3Teams[team].tag, D.name, g_Mw3Teams[team].name);
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // team mates in the zone help
                {
                    if(i == c || !m_Slots[i].used || !m_Slots[i].hasPos || m_Mw3[i].team != team || !m_Mw3[i].spawned || m_Mw3[i].arena != MW3A_NONE) continue;
                    const float* p = m_Slots[i].pos;
                    if(p[0] < D.minX || p[0] > D.maxX || p[1] < D.minY || p[1] > D.maxY) continue;
                    Mw3Reward(i, 2, 2000, 4);
                    Mw3Msg(i, MW3_GREEN, "[MW3] You helped capture %s: +2 score, $2000.", D.name);
                }
                m_bMw3Dirty = true;
                m_nMw3LastSend = 0;
            }
            continue;
        }
        // somebody new in the checkpoint?
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            const Slot& s = m_Slots[i];
            const Mw3SrvPlayer& m = m_Mw3[i];
            if(!s.used || !s.hasPos || !m.team || !m.spawned || m.arena != MW3A_NONE || m.team == Z.owner) continue;
            if(s.lastFlags & (SYNC_DEAD | SYNC_IN_CAR)) continue;
            if(Mw3Dist2D(s.pos, D.cp[0], D.cp[1]) > MW3_CAP_R || fabsf(s.pos[2] - D.cp[2]) >= 4.0f) continue;
            Z.atk = m.team; Z.capper = (uint8_t)i; Z.capStart = now;
            Mw3Ev(-1, MW3V_CAP_START, (uint8_t)z, m.team, 0, (uint8_t)i, (int32_t)Mw3CapMs(z), D.name);
            Mw3Msg(-1, g_Mw3Teams[m.team].rgba, "[MW3] %s (%s) is capturing %s%s.", s.name, g_Mw3Teams[m.team].tag, D.name, Z.owner ? " from " : "");
            if(Z.owner)
                for(int k = 0; k < MP_MAX_PLAYERS; ++k)
                    if(m_Slots[k].used && m_Mw3[k].team == Z.owner) Mw3Ev(k, MW3V_TEXT, 0, 0, 0, (uint8_t)i, (int32_t)MW3_RED, "YOUR ZONE IS UNDER ATTACK");
            any = true;
            m_nMw3LastSend = 0;
            break;
        }
    }
    (void)any;
    // owned zones pay their team every 10 min
    if(now >= m_nMw3ZonePay)
    {
        m_nMw3ZonePay = now + 10ull * 60ull * 1000ull;
        int owned[MW3_TEAMS + 1] = { 0 };
        for(int z = 0; z < MW3_ZONE_COUNT; ++z) if(m_Mw3Zone[z].owner) owned[m_Mw3Zone[z].owner]++;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!m_Slots[i].used || !m_Mw3[i].team || !owned[m_Mw3[i].team]) continue;
            int n = owned[m_Mw3[i].team];
            Mw3Reward(i, n, 1000 * n, n);
            Mw3Msg(i, MW3_GREEN, "[MW3] Your team owns %d zone%s: +%d score, $%d.", n, n == 1 ? "" : "s", n, 1000 * n);
        }
    }
}

void MPServer::Mw3TourneyTick(uint64_t now)
{
    if(now < m_nMw3TourneyStart + MW3_TOURNEY_MS) return;
    int win = -1; uint16_t best = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        if(m_Slots[i].used) { uint16_t v = Mw3TourneyVal(i); if(v > best) { best = v; win = i; } }
    if(win >= 0)
    {
        Mw3Msg(-1, MW3_YELLOW, "[TOURNAMENT] %s won the %s tournament with %d. Prize: +50 score, +50 XP, $50000.", m_Slots[win].name, g_Mw3TourneyName[m_nMw3TKind], best);
        Mw3Reward(win, 50, 50000, 50);
        Mw3Ev(-1, MW3V_TEXT, 0, 0, 0, (uint8_t)win, (int32_t)MW3_YELLOW, "TOURNAMENT WINNER");
    }
    else Mw3Msg(-1, MW3_GREY, "[TOURNAMENT] Nobody scored in the %s tournament.", g_Mw3TourneyName[m_nMw3TKind]);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { m_Mw3[i].hKills = m_Mw3[i].hCaps = m_Mw3[i].hDeagle = m_Mw3[i].hSpree = 0; }
    m_nMw3TKind = (uint8_t)((m_nMw3TKind + 1) % MW3T_KINDS);
    m_nMw3TourneyStart = now;
    Mw3Msg(-1, MW3_YELLOW, "[TOURNAMENT] A new hourly tournament started: %s.", g_Mw3TourneyName[m_nMw3TKind]);
    m_nMw3LastSend = 0;
}

void MPServer::Mw3EventStart(uint8_t kind)
{
    m_nMw3EvKind = kind;
    m_nMw3EvState = MW3ES_SIGNUP;
    m_nMw3EvAt = MP_NowMs();
    m_nMw3EvSideKills[0] = m_nMw3EvSideKills[1] = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { m_Mw3[i].evIn = false; m_Mw3[i].evKills = 0; }
    Mw3Msg(-1, MW3_GREEN, "[EVENT] %s at the LS Docks starts in 60 seconds. Type /join to play.", g_Mw3EventName[kind]);
    Mw3Ev(-1, MW3V_TEXT, kind, 0, 0, MP_INVALID_ID, (int32_t)MW3_GREEN, "EVENT SIGN-UP - /join");
    m_nMw3LastSend = 0;
}

void MPServer::Mw3EventLeave(int slot, bool msg)
{
    Mw3SrvPlayer& m = m_Mw3[slot];
    bool wasIn = m.arena == MW3A_EVENT;
    m.evIn = false;
    m.sub = 0;
    if(wasIn)
    {
        m.arena = MW3A_NONE;
        m.spawned = false;
        Mw3Ev(slot, MW3V_EVENT_OUT, 0, 0, 0, MP_INVALID_ID, 0);
    }
    if(msg) Mw3Msg(slot, MW3_GREY, "[EVENT] You left the event.");
    m_nMw3LastPlayers = 0;
}

void MPServer::Mw3EventEnd(int winnerSide, int winner)
{
    uint8_t kind = m_nMw3EvKind;
    if(winner >= 0 && winner < MP_MAX_PLAYERS && m_Slots[winner].used)
    {
        Mw3Msg(-1, MW3_GREEN, "[EVENT] %s won the %s. Prize: +10 score, $20000.", m_Slots[winner].name, g_Mw3EventName[kind]);
        Mw3Reward(winner, 10, 20000, 20);
    }
    else if(winnerSide > 0)
    {
        const char* side = kind == MW3E_ZOMBIE ? (winnerSide == 1 ? "The humans" : "The zombies") : (winnerSide == 1 ? "The red team" : "The blue team");
        Mw3Msg(-1, MW3_GREEN, "[EVENT] %s won the %s. Prize: +5 score, $10000 each.", side, g_Mw3EventName[kind]);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && m_Mw3[i].evIn && m_Mw3[i].sub == winnerSide) Mw3Reward(i, 5, 10000, 10);
    }
    else Mw3Msg(-1, MW3_GREY, "[EVENT] The %s ended with no winner.", g_Mw3EventName[kind]);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && (m_Mw3[i].evIn || m_Mw3[i].arena == MW3A_EVENT)) Mw3EventLeave(i, false);
    m_nMw3EvState = MW3ES_IDLE;
    m_nMw3EvKind = 0;
    m_nMw3NextEvent = MP_NowMs() + MW3_EVENT_EVERY_MS;
    m_nMw3LastSend = 0;
}

void MPServer::Mw3EventTick(uint64_t now)
{
    if(m_nMw3EvState == MW3ES_IDLE)
    {
        if(now < m_nMw3NextEvent) return;
        int online = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].team) ++online;
        if(online < MW3_EVENT_MIN) { m_nMw3NextEvent = now + 60000; return; }
        uint8_t k = m_nMw3EvNextKind;
        m_nMw3EvNextKind = (uint8_t)(k + 1 >= MW3E_KINDS ? MW3E_TDM : k + 1);
        Mw3EventStart(k);
        return;
    }
    if(m_nMw3EvState == MW3ES_SIGNUP)
    {
        if(now < m_nMw3EvAt + MW3_EVENT_SIGNUP_MS) return;
        int ids[MP_MAX_PLAYERS], n = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && m_Mw3[i].evIn && m_Mw3[i].arena != MW3A_DUEL) ids[n++] = i; else m_Mw3[i].evIn = false;
        if(n < MW3_EVENT_MIN)
        {
            Mw3Msg(-1, MW3_GREY, "[EVENT] The %s was cancelled: not enough players (%d / %d).", g_Mw3EventName[m_nMw3EvKind], n, MW3_EVENT_MIN);
            for(int j = 0; j < n; ++j) m_Mw3[ids[j]].evIn = false;
            m_nMw3EvState = MW3ES_IDLE; m_nMw3EvKind = 0;
            m_nMw3NextEvent = now + MW3_EVENT_EVERY_MS;
            m_nMw3LastSend = 0;
            return;
        }
        for(int j = n - 1; j > 0; --j) { int r = rand() % (j + 1); int t = ids[j]; ids[j] = ids[r]; ids[r] = t; }   // shuffle
        int zombie = m_nMw3EvKind == MW3E_ZOMBIE ? 0 : -1;
        for(int j = 0; j < n; ++j)
        {
            int i = ids[j];
            Mw3SrvPlayer& m = m_Mw3[i];
            if(m.arena != MW3A_NONE) m.arena = MW3A_NONE;
            for(int z = 0; z < MW3_ZONES_WIRE; ++z)
                if(m_Mw3Zone[z].atk && m_Mw3Zone[z].capper == i) { m_Mw3Zone[z].atk = 0; m_Mw3Zone[z].capper = MP_INVALID_ID; Mw3Ev(-1, MW3V_CAP_FAIL, (uint8_t)z, 0, 0, (uint8_t)i, 0); }
            m.arena = MW3A_EVENT;
            m.spawned = false;
            m.evKills = 0;
            if(m_nMw3EvKind == MW3E_TDM) m.sub = (uint8_t)(1 + (j & 1));
            else if(m_nMw3EvKind == MW3E_ZOMBIE) m.sub = (uint8_t)(j == zombie ? 2 : 1);
            else m.sub = 0;
            Mw3Ev(i, MW3V_EVENT_SPAWN, m_nMw3EvKind, m.sub, (uint8_t)(j % 8), MP_INVALID_ID, 0, g_Mw3EventName[m_nMw3EvKind]);
        }
        if(zombie >= 0) Mw3Msg(-1, MW3_RED, "[EVENT] %s is the first zombie. Humans, survive 5 minutes.", m_Slots[ids[0]].name);
        Mw3Msg(-1, MW3_GREEN, "[EVENT] The %s has started with %d players.", g_Mw3EventName[m_nMw3EvKind], n);
        m_nMw3EvState = MW3ES_LIVE;
        m_nMw3EvAt = now;
        m_nMw3LastPlayers = 0; m_nMw3LastSend = 0;
        return;
    }
    // live
    bool timeUp = now >= m_nMw3EvAt + MW3_EVENT_MAX_MS;
    int alive = Mw3EvAlive(0);
    switch(m_nMw3EvKind)
    {
    case MW3E_TDM:
    {
        int r = Mw3EvAlive(1), b = Mw3EvAlive(2);
        if(m_nMw3EvSideKills[0] >= 20 || b == 0) { Mw3EventEnd(1, -1); return; }
        if(m_nMw3EvSideKills[1] >= 20 || r == 0) { Mw3EventEnd(2, -1); return; }
        if(timeUp) Mw3EventEnd(m_nMw3EvSideKills[0] == m_nMw3EvSideKills[1] ? 0 : (m_nMw3EvSideKills[0] > m_nMw3EvSideKills[1] ? 1 : 2), -1);
        return;
    }
    case MW3E_DM:
    {
        int best = -1; uint16_t bk = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].evIn && m_Mw3[i].evKills > bk) { bk = m_Mw3[i].evKills; best = i; }
        if(bk >= 10 || alive <= 1 || timeUp)
        {
            if(best < 0 && alive == 1) for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].evIn) { best = i; break; }
            Mw3EventEnd(0, best);
        }
        return;
    }
    case MW3E_ZOMBIE:
        if(Mw3EvAlive(1) == 0) { Mw3EventEnd(2, -1); return; }
        if(Mw3EvAlive(2) == 0 || timeUp) { Mw3EventEnd(1, -1); return; }
        return;
    case MW3E_LMS:
        if(alive <= 1 || timeUp)
        {
            int w = -1;
            if(alive == 1) for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Mw3[i].evIn && m_Mw3[i].arena == MW3A_EVENT) { w = i; break; }
            Mw3EventEnd(0, w);
        }
        return;
    default:
        Mw3EventEnd(0, -1);
        return;
    }
}

void MPServer::Mw3Tick(uint64_t now)
{
    if(!IsMw3Server()) return;
    Mw3ZoneTick(now);
    Mw3TourneyTick(now);
    Mw3EventTick(now);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // duel requests time out
        if(m_Mw3[i].duelAsk != MP_INVALID_ID && now - m_Mw3[i].duelAskAt > 30000) m_Mw3[i].duelAsk = MP_INVALID_ID;
    bool capping = false;
    for(int z = 0; z < MW3_ZONES_WIRE; ++z) if(m_Mw3Zone[z].atk) capping = true;
    if(now - m_nMw3LastSend >= (capping ? 500u : 1000u))
    {
        m_nMw3LastSend = now;
        Mw3SendState(-1);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].authed) Mw3SendMe(i);
    }
    if(now - m_nMw3LastPlayers >= 2000)
    {
        m_nMw3LastPlayers = now;
        Mw3SendPlayers(-1);
    }
    if(m_bMw3Dirty && now - m_nMw3LastSave >= 15000)   // 3.24: every 15 s (was 60 s, and even when nothing changed)
    {
        m_nMw3LastSave = now;
        Mw3Save();
    }
}
