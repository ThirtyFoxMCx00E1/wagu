#include <time.h>
#include "server.h"
#include "safezone.h"
#include "admin.h"
#include "authcrypto.h"   // 3.21.6: login tickets
#include <mbedtls/sha256.h>   // 3.24: MW3 stats cards
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <chrono>
#include <vector>
#include <algorithm>
#include <errno.h>
#if defined(__linux__) || defined(__ANDROID__)
  #define MP_MMSG 1          // 3.22: recvmmsg / sendmmsg (Linux 3.0+, Android API 21+)
  #include <sys/uio.h>
#endif

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
  #define CLOSESOCK closesocket
#else
  #include <sys/socket.h>
  #include <arpa/inet.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <sys/time.h>
  #define CLOSESOCK close
  #define INVALID_SOCKET (-1)
#endif

#ifdef MP_MMSG
// 3.22: everything the network thread sends while it handles one batch of received packets is
// queued and leaves in a few sendmmsg() calls instead of one sendto() per packet. With 100 players
// one sync fans out to 99 phones: ~100x fewer system calls, the thread is back at the socket at
// once and nothing waits in the receive queue (that wait was the ping spike with a full server).
namespace {
struct OutQ
{
    bool on = false; int sock = -1;
    std::vector<uint8_t> buf; std::vector<sockaddr_in> to; std::vector<uint32_t> off, len;
    void Flush()
    {
        size_t n = to.size(), done = 0;
        if(!n) return;
        std::vector<mmsghdr> m(n); std::vector<iovec> io(n);
        for(size_t i = 0; i < n; ++i)
        {
            io[i].iov_base = buf.data() + off[i]; io[i].iov_len = len[i];
            memset(&m[i], 0, sizeof(m[i]));
            m[i].msg_hdr.msg_name = &to[i]; m[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
            m[i].msg_hdr.msg_iov = &io[i]; m[i].msg_hdr.msg_iovlen = 1;
        }
        int tries = 0;
        while(done < n && tries < 64)
        {
            int r = sendmmsg(sock, m.data() + done, (unsigned)std::min<size_t>(n - done, 1024), 0);
            if(r > 0) { done += (size_t)r; continue; }
            if(r < 0 && errno == EINTR) continue;
            ++tries;
            if(r < 0 && errno != EAGAIN && errno != ENOBUFS) ++done;   // a bad address: skip that one packet
        }
        buf.clear(); to.clear(); off.clear(); len.clear();
    }
    void Put(const sockaddr_in& a, const void* d, int l)
    {
        off.push_back((uint32_t)buf.size()); len.push_back((uint32_t)l); to.push_back(a);
        buf.insert(buf.end(), (const uint8_t*)d, (const uint8_t*)d + l);
        if(to.size() >= 512 || buf.size() >= 512 * 1024) Flush();
    }
};
thread_local OutQ t_Out;
struct OutBatch   // queue the sends of this scope, send them all at the end
{
    explicit OutBatch(int s) { t_Out.on = true; t_Out.sock = s; }
    ~OutBatch() { t_Out.Flush(); t_Out.on = false; }
};
}
#endif

uint64_t MP_NowMs()
{
    using namespace std::chrono;
    return (uint64_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

static inline bool SameAddr(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

MPServer::MPServer() : m_Sock(INVALID_SOCKET), m_bRunning(false), m_bPersistent(false), m_nPort(0), m_fnLog(nullptr), m_nMaxPlayers(MP_STD_MAX_PLAYERS), m_nRegion(REGION_ASIA)
{
    memset(m_Slots, 0, sizeof(m_Slots));
    memset(m_Pickups, 0, sizeof(m_Pickups));
    m_szName[0] = 0;
}

MPServer::~MPServer() { Stop(); }

void MPServer::Log(const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    if(m_fnLog) m_fnLog(buf); else printf("[MP Server] %s\n", buf);
}

bool MPServer::Start(uint16_t port, const char* serverName, MPServerLogFn log, int maxPlayers, int region)
{
    if(m_bRunning) return true;
    m_fnLog = log;
    SetRegion(region);
#ifdef _WIN32
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    m_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(m_Sock == INVALID_SOCKET) { Log("socket() failed"); return false; }

    int yes = 1;
    setsockopt(m_Sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
    int sb = MP_SERVER_SOCK_BUF;   // 100 players: don't drop bursts (3.22: 8 MB, was 512 KB)
    setsockopt(m_Sock, SOL_SOCKET, SO_RCVBUF, (const char*)&sb, sizeof(sb));
    setsockopt(m_Sock, SOL_SOCKET, SO_SNDBUF, (const char*)&sb, sizeof(sb));

    sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if(bind(m_Sock, (sockaddr*)&addr, sizeof(addr)) != 0)
    {
        Log("bind() on port %u failed (already in use?)", port);
        CLOSESOCK(m_Sock); m_Sock = INVALID_SOCKET;
        return false;
    }

#ifdef _WIN32
    DWORD tv = 50;
    setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
    timeval tv; tv.tv_sec = 0; tv.tv_usec = 50 * 1000;
    setsockopt(m_Sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    m_nPort = port;
    MP_CopyStr(m_szName, serverName ? serverName : "MP Server", sizeof(m_szName));
    // 3.10: the slot count is set AFTER the new name. It used to be set first, while m_szName still
    // held the previous server's name: a phone that hosted Brief Case War (20) and then hosted the
    // default server kept 20 slots ("0/20") on the default server. The default server is always 100.
    SetMaxPlayers(maxPlayers);
    memset(m_Slots, 0, sizeof(m_Slots));
    m_nNextAd = MP_NowMs() + m_nAdInterval;   // 3.11: the Discord clock starts with the server
    memset(m_Pickups, 0, sizeof(m_Pickups));
    m_nStartUnix = (uint32_t)time(nullptr);   // 2.27
    LoadTurf();                               // 3.01
    BcwReset();                               // 3.04
    Mw3Reset();                               // 3.23
    RpReset();                                // 3.25
    if(IsRpServer())
    {   // 3.25: Mega Vil Futuristic - roleplay only, 250 players, no cheats, no other gamemodes
        m_nMaxPlayers = MP_RP_MAX_PLAYERS;
        m_bTurfWars = false; m_bSafeZones = false;
        m_nBasePerms = PERM_NO_CHEATS; m_nPerms = PERM_NO_CHEATS;
        RpLoad();
    }
    if(IsMw3Server())
    {
        m_nMaxPlayers = MP_MW3_MAX_PLAYERS;
        m_bTurfWars = false; m_bSafeZones = false;
        m_nBasePerms = PERM_NO_CHEATS; m_nPerms = PERM_NO_CHEATS;
        Mw3Load();
    }
    if(IsBcwServer())
    {
        m_nMaxPlayers = MP_BCW_MAX_PLAYERS;
        m_bTurfWars = false;
        m_nBasePerms = PERM_NO_CHEATS; m_nPerms = PERM_NO_CHEATS;
    }
    m_bRunning = true;
    m_Thread = std::thread(&MPServer::Loop, this);
    Log("Server \"%s\" listening on UDP port %u%s", m_szName, port, m_bPersistent ? " (persistent)" : "");
    return true;
}

void MPServer::Stop()
{
    if(!m_bRunning) return;
    {
        // tell everyone right away (instead of a 10 s timeout) so a new host takes over
        std::lock_guard<std::mutex> lk(m_Mutex);
        if(IsMw3Server()) Mw3Save();   // 3.23
        if(IsRpServer()) RpSave();     // 3.25
        PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_SHUTDOWN;
        for(int k = 0; k < 2; ++k) Broadcast(&rj, sizeof(rj));
    }
    m_bRunning = false;
    if(m_Thread.joinable()) m_Thread.join();
    if(m_Sock != INVALID_SOCKET) { CLOSESOCK(m_Sock); m_Sock = INVALID_SOCKET; }
    Log("Server stopped");
}

// The server's day: real time based, so it is the same on every server and survives a host
// change. Nobody can move it (the phones lock their game clock + weather to this).
uint32_t MPServer::WorldMinute()
{
    return (uint32_t)time(nullptr);   // 1 game minute per real second (the game's normal speed)
}

void MPServer::FillWorld(PktWorld& w)
{
    memset(&w, 0, sizeof(w));
    uint32_t abs = WorldMinute();
    w.type = PKT_WORLD;
    w.hour = (uint8_t)((abs / 60) % 24); w.minute = (uint8_t)(abs % 60);
    w.weather = 1;            // SUNNY_LA, only used by 2.7 and older
    w.mode = WORLD_NATURAL;
    w.absMinute = abs;
}

int MPServer::GetPlayerCount()
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    int n = HasBot() ? 1 : 0;   // 3.23.5: + the 24/7 bot
    for(auto& s : m_Slots) if(s.used) ++n;
    return n;
}

int MPServer::FindSlot(const sockaddr_in& a)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && SameAddr(m_Slots[i].addr, a)) return i;
    return -1;
}

void MPServer::SendRaw(const sockaddr_in& a, const void* data, int len)
{
#ifdef MP_MMSG
    if(t_Out.on && t_Out.sock == (int)m_Sock && len > 0) { t_Out.Put(a, data, len); return; }
#endif
    sendto(m_Sock, (const char*)data, len, 0, (const sockaddr*)&a, sizeof(a));
}

void MPServer::SendTo(int slot, const void* data, int len)
{
    if(slot >= 0 && slot < MP_MAX_PLAYERS && m_Slots[slot].used) SendRaw(m_Slots[slot].addr, data, len);
}

void MPServer::Broadcast(const void* data, int len, int except)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i != except && m_Slots[i].used) SendRaw(m_Slots[i].addr, data, len);
}

// Forward one player's sync to the others. Nearby players get every update; far ones get one
// per MP_SYNC_FAR_MS (enough for the player list, health and mission state). State changes
// (death, mission, cutscene, car enter/exit) always go to everyone right away.
void MPServer::RelaySync(int slot, const void* pkt, int len, bool important)
{
    Slot& s = m_Slots[slot];
    uint64_t now = MP_NowMs();
    // 2.14 distance tiers: you only need full rate for players you can see clearly. With 30+
    // players this cuts what the host has to upload by 3-6x, which is what kept the ping low.
    int crowd = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) ++crowd;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == slot || !m_Slots[i].used) continue;
        Slot& r = m_Slots[i];
        // 3.34: downlink budget per receiver. Every phone used to get every nearby sync at 20 Hz, so each
        // player that joined added 20 more packets/s to EVERYBODY's download (through the broker / mobile
        // data) - the queue grew and so did everybody's ping (2/100 a bit, 25/100 a lot). Now each player
        // gets at most MP_DL_BUDGET syncs per 250 ms; when a window goes over, the rates of everything but
        // the closest players slow down a bit (x1.3 per window, max x6) and come back once it is quiet.
        if(now - r.dlWinAt >= 250)
        {
            float sc = r.dlScale < 1.0f ? 1.0f : r.dlScale;
            if(r.dlN > MP_DL_BUDGET) sc *= 1.3f;
            else if(r.dlN < MP_DL_BUDGET * 6 / 10) sc /= 1.15f;
            r.dlScale = sc < 1.0f ? 1.0f : (sc > 6.0f ? 6.0f : sc);
            r.dlN = 0; r.dlWinAt = now;
        }
        const float scale = r.dlScale < 1.0f ? 1.0f : r.dlScale;
        bool send = important || !r.hasPos || !s.hasPos;
        if(!send)
        {
            float dx = s.pos[0] - r.pos[0], dy = s.pos[1] - r.pos[1];
            float d2 = dx * dx + dy * dy;
            uint64_t every;
            if(d2 <= 30.0f * 30.0f)        every = crowd > 24 ? 66 : 0;   // close: every sync (max 15 Hz in a crowd)
            else if(d2 <= 60.0f * 60.0f)   every = crowd > 48 ? 150 : (crowd > 24 ? 66 : 0);   // 3.22: 50+ players packed together: 30-60 m at ~7 Hz
            else if(d2 <= 150.0f * 150.0f) every = 200;
            else if(d2 <= MP_SYNC_NEAR_M * MP_SYNC_NEAR_M) every = 500;
            else                           every = MP_SYNC_FAR_MS > 2000 ? MP_SYNC_FAR_MS : 2000;
            if(scale > 1.01f)
            {   // 3.34: busy downlink: the closest 30 m keep at least 10 Hz, the rest slows down with the scale
                if(d2 <= 30.0f * 30.0f) { uint64_t m = (uint64_t)(MP_SYNC_RATE_MS * (scale < 2.0f ? scale : 2.0f)); if(every < m) every = m; }
                else every = (uint64_t)((every ? every : MP_SYNC_RATE_MS) * scale);
            }
            send = now - s.lastFwd[i] >= every;
        }
        if(!send) continue;
        s.lastFwd[i] = now;
        if(r.dlN < 0xFFFF) ++r.dlN;
        SendRaw(r.addr, pkt, len);
    }
}

// 2.13: explosions / fires only matter to players who can see them
void MPServer::RelayNear(int slot, const float pos[3], const void* pkt, int len, float range)
{
    const float rangeSq = range * range;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == slot || !m_Slots[i].used || m_Slots[i].status) continue;   // on a mission: own world
        const Slot& r = m_Slots[i];
        if(r.hasPos)
        {
            float dx = pos[0] - r.pos[0], dy = pos[1] - r.pos[1];
            if(dx * dx + dy * dy > rangeSq) continue;
        }
        SendRaw(r.addr, pkt, len);
    }
}

// 2.17: like RelayNear, but a player on a mission still reaches their mission-sync partner
void MPServer::RelayNearOrPartner(int slot, const float pos[3], const void* pkt, int len)
{
    const Slot& s = m_Slots[slot];
    if(!s.status) RelayNear(slot, pos, pkt, len);
    if(!(m_nPerms & PERM_MISSION_PAIR) || s.pair == MP_INVALID_ID) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // 2.31: every member of my mission group
        if(IsPartner(slot, i) && (s.status || m_Slots[i].status)) SendRaw(m_Slots[i].addr, pkt, len);
}

void MPServer::SendPickups(int slot)
{
    for(int i = 0; i < MP_MAX_PICKUPS; ++i)
    {
        const NetPickup& k = m_Pickups[i];
        if(!k.used) continue;
        PktPickupAdd a; a.type = PKT_PICKUP_ADD; a.uid = k.uid; a.weapon = k.weapon; a.ammo = k.ammo;
        memcpy(a.pos, k.pos, sizeof(a.pos));
        SendTo(slot, &a, sizeof(a));
    }
}

void MPServer::ExpirePickups(uint64_t now)
{
    for(int i = 0; i < MP_MAX_PICKUPS; ++i)
    {
        NetPickup& k = m_Pickups[i];
        if(!k.used || now - k.born < MP_PICKUP_LIFE_MS) continue;
        PktPickupRemove r; r.type = PKT_PICKUP_REMOVE; r.uid = k.uid; r.takenBy = MP_INVALID_ID;
        k.used = false;
        Broadcast(&r, sizeof(r));
    }
}

// 2.31 mission groups (m_Mutex held)
int MPServer::GroupSize(uint8_t g) const
{
    if(g == MP_INVALID_ID) return 0;
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].pair == g) ++n;
    return n;
}
void MPServer::SendGroupLinks(int slot)
{
    PktPair a; a.type = PKT_PAIR; a.action = PAIR_LINKED;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!IsPartner(slot, i)) continue;
        a.other = (uint8_t)i;    SendTo(slot, &a, sizeof(a));
        a.other = (uint8_t)slot; SendTo(i, &a, sizeof(a));
    }
}
// slot leaves its group: it and every member get PAIR_END about each other. A group left with
// one player is closed.
void MPServer::EndPair(int slot)
{
    Slot& s = m_Slots[slot];
    s.pairReq = MP_INVALID_ID;
    uint8_t g = s.pair; s.pair = MP_INVALID_ID;
    if(g == MP_INVALID_ID) return;
    PktPair a; a.type = PKT_PAIR; a.action = PAIR_END;
    int last = -1, left = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || m_Slots[i].pair != g) continue;
        a.other = (uint8_t)i;    if(s.used) SendTo(slot, &a, sizeof(a));
        a.other = (uint8_t)slot; SendTo(i, &a, sizeof(a));
        last = i; ++left;
    }
    Log("%s left Mission sync", s.name);
    if(left == 1) { m_Slots[last].pair = MP_INVALID_ID; }   // alone now: the client already got END about the one who left
}

void MPServer::SetPassword(const char* pw)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szPassword, pw ? pw : "", sizeof(m_szPassword));
}

void MPServer::SetPerms(uint8_t perms)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_nBasePerms = perms;
    SetPermsLocked(perms);
}

void MPServer::SetAdminHash(const char* h)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szAdminHash, h ? h : "", sizeof(m_szAdminHash));
}

// 3.21: hospital safe zones (safezone.h) + 5 s spawn protection. A hit is dropped when the target is
// in a zone or just respawned, or when the shooter stands in a zone (nobody shoots out of a zone).
// Not on Brief Case War servers (own base protection: BcwOnDamage).
bool MPServer::SafeZoneBlocks(int from, int to) const
{
    if(!m_bSafeZones || IsBcwServer() || IsMw3Server() || IsRpServer()) return false;
    if(to < 0 || to >= MP_MAX_PLAYERS || !m_Slots[to].used) return false;
    const Slot& t = m_Slots[to];
    if(t.spawnProtUntil && MP_NowMs() < t.spawnProtUntil) return true;
    if(t.hasPos && MP_InSafeZone(t.pos[0], t.pos[1], t.pos[2])) return true;
    if(from >= 0 && from < MP_MAX_PLAYERS && m_Slots[from].used && m_Slots[from].spawnProtUntil && MP_NowMs() < m_Slots[from].spawnProtUntil) return true;   // protected players don't hurt others either
    if(from >= 0 && from < MP_MAX_PLAYERS && m_Slots[from].used && m_Slots[from].hasPos
       && MP_InSafeZone(m_Slots[from].pos[0], m_Slots[from].pos[1], m_Slots[from].pos[2])) return true;
    return false;
}

bool MPServer::IsDefaultServer() const { return !strcasecmp(m_szName, MP_DEFAULT_SERVER_NAME); }

void MPServer::SetPermsLocked(uint8_t perms)
{
    if(IsDefaultServer()) perms &= ~PERM_CHEAT_MENU;   // 3.00: never a cheat menu on the default server
    if(IsBcwServer() || IsMw3Server() || IsRpServer()) { perms &= ~(PERM_CHEAT_MENU | PERM_MISSION_PAIR); perms |= PERM_NO_CHEATS; }   // 3.04 / 3.23 MW3: Brief Case War: no CheatMenu, MP + CLEO menu cheats off
    if(m_nPerms == perms) return;
    m_nPerms = perms;
    if(!(perms & PERM_MISSION_PAIR))
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used) { m_Slots[i].pairReq = MP_INVALID_ID; if(m_Slots[i].pair != MP_INVALID_ID) EndPair(i); }
    PktPerms p; MakePerms(p, perms);
    if(m_bRunning) Broadcast(&p, sizeof(p));
}
// 3.02: gamemodes
void MPServer::MakePerms(PktPerms& p, uint8_t perms) const
{
    memset(&p, 0, sizeof(p));
    p.type = PKT_PERMS; p.perms = perms | PERM_V219;
    p.modesMagic = MP_MODES_MAGIC; p.modes = GetModes();
}
uint16_t MPServer::GetModes() const { if(IsBcwServer() || IsMw3Server() || IsRpServer()) return 0; return IsDefaultServer() ? (uint16_t)GM_ALL : (uint16_t)(m_nModes & GM_ALL); }   // 3.04: Brief Case War runs only its own gamemode
void MPServer::SetModes(uint16_t modes)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(m_nModes == (modes & GM_ALL)) return;
    m_nModes = modes & GM_ALL;
    if(!(GetModes() & GM_RACE)) for(int i = 0; i < MP_MAX_PLAYERS; ++i) m_Slots[i].raceQueuedAt = 0;
    PktPerms p; MakePerms(p, m_nPerms);
    if(m_bRunning) Broadcast(&p, sizeof(p));
}

// ---- 3.21.6: login system ----
void MPServer::AnnounceJoin(int slot)
{
    Slot& s = m_Slots[slot];
    PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
    pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)slot; pj.color = s.color; pj.skin = s.skin;
    MP_CopyStr(pj.name, s.name, MP_NAME_LEN);
    Broadcast(&pj, sizeof(pj), slot);
}

static void AuthServerName(const char* in, char* out, size_t n)
{   // the name as the bot writes it into the ticket (no '|', max 39)
    size_t o = 0;
    for(size_t i = 0; in[i] && o + 1 < n && o < 31; ++i) if(in[i] != '|' && in[i] != '\n' && in[i] != '\r' && in[i] != '=') out[o++] = in[i];
    out[o] = 0;
}

void MPServer::AuthBegin(int slot)
{
    Slot& s = m_Slots[slot];
    if(!s.chal[0])
    {
        uint8_t r[16];
        if(!AuthCrypto::Random(r, sizeof(r))) for(int i = 0; i < 16; ++i) r[i] = (uint8_t)(rand() ^ (MP_NowMs() >> (i % 8)));
        AuthCrypto::Hex(r, 16, s.chal);
    }
    PktAuthChal c; memset(&c, 0, sizeof(c));
    c.type = PKT_AUTH; c.op = AUTH_CHALLENGE; c.style = IsBcwServer() ? AUTH_STYLE_BCW : (IsMw3Server() ? AUTH_STYLE_MW3 : (IsRpServer() ? AUTH_STYLE_RP : AUTH_STYLE_NORMAL));
    memcpy(c.chal, s.chal, sizeof(c.chal));
    MP_CopyStr(c.server, m_szName, sizeof(c.server));
    SendTo(slot, &c, sizeof(c));
}

void MPServer::AuthHandle(int slot, const uint8_t* data, int len)
{
    Slot& s = m_Slots[slot];
    if(len < (int)sizeof(PktAuthTicket) || data[1] != AUTH_TICKET) return;
    PktAuthResult res; memset(&res, 0, sizeof(res)); res.type = PKT_AUTH; res.op = AUTH_RESULT;
    if(s.authed)
    {   // the answer got lost: say it again
        res.ok = 1; MP_CopyStr(res.user, s.account, sizeof(res.user));
        SendTo(slot, &res, sizeof(res));
        return;
    }
    uint64_t now = MP_NowMs();
    if(now - s.lastAuthTry < 400) return;   // a signature check costs CPU: max ~2 per second
    s.lastAuthTry = now;
    PktAuthTicket h; memcpy(&h, data, sizeof(h));
    int plen = h.plen;
    const char* fail = nullptr;
    AuthCrypto::Ticket t;
    char srv[40]; AuthServerName(m_szName, srv, sizeof(srv));
    if(plen <= 0 || plen > MP_AUTH_TICKET_MAX || (int)sizeof(PktAuthTicket) + plen > len) fail = "Bad login ticket.";
    else if(!AuthCrypto::VerifyTicket((const char*)data + sizeof(PktAuthTicket), (size_t)plen, h.sig)) fail = "Login ticket not signed by the MP bot.";
    else if(!AuthCrypto::ParseTicket((const char*)data + sizeof(PktAuthTicket), (size_t)plen, t)) fail = "Bad login ticket.";
    else if(strcmp(t.chal, s.chal)) fail = "Login ticket is for another connection. Log in again.";
    else if(strcmp(t.server, srv)) fail = "Login ticket is for another server. Log in again.";
    if(fail)
    {
        res.ok = 0; MP_CopyStr(res.msg, fail, sizeof(res.msg));
        SendTo(slot, &res, sizeof(res));
        Log("Login refused for id %d: %s", slot, fail);
        if(++s.authFails >= 8) Drop(slot, LEAVE_KICK);
        return;
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // the same account on another slot (other phone / ghost): the new one wins
        if(i != slot && m_Slots[i].used && m_Slots[i].authed && !strcasecmp(m_Slots[i].account, t.user)) Drop(i, LEAVE_TIMEOUT);
    s.authed = true;
    MP_CopyStr(s.account, t.user, sizeof(s.account));
    s.chal[0] = 0;   // used up
    res.ok = 1; MP_CopyStr(res.user, t.user, sizeof(res.user));
    SendTo(slot, &res, sizeof(res));
    Log("%s (id %d) logged in", s.name, slot);   // the account name stays private (not in the public log)
    AnnounceJoin(slot);
    Mw3OnAuthed(slot);   // 3.23: load the MW3 stats of this account
    RpOnAuthed(slot);    // 3.25: load the citizen (money, job, house...)
    s.spawnProtUntil = now + MP_SPAWN_PROTECT_MS;
}

bool MPServer::AccountOf(int id, char* out, size_t n)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(id < 0 || id >= MP_MAX_PLAYERS || !m_Slots[id].used || !m_Slots[id].account[0]) return false;
    MP_CopyStr(out, m_Slots[id].account, n);
    return true;
}

void MPServer::Drop(int slot, uint8_t reason)
{
    if(!m_Slots[slot].used) return;
    EndPair(slot);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].pairReq == slot) m_Slots[i].pairReq = MP_INVALID_ID;
    Log("%s (id %d) left (%s)", m_Slots[slot].name, slot, reason == LEAVE_TIMEOUT ? "Timeout" : (reason == LEAVE_KICK ? "Kicked" : "Quit"));
    m_Slots[slot].used = false;
    BcwOnLeave(slot);   // 3.04
    Mw3OnLeave(slot);   // 3.23
    RpOnLeave(slot);    // 3.25
    PktPlayerLeave p; p.type = PKT_PLAYER_LEAVE; p.id = (uint8_t)slot; p.reason = reason;
    Broadcast(&p, sizeof(p));

    // Drop() runs with m_Mutex already held (server thread), so count here without locking:
    // calling GetPlayerCount() would lock m_Mutex again and freeze the server and the game.
    int left = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) ++left;
    if(left == 0 && m_nPerms != m_nBasePerms) SetPermsLocked(m_nBasePerms);   // 2.37: the admin's cheat switch ends with the session
    if(m_bPersistent && left == 0)
        Log("All players left; persistent server remains active and joinable (24/7).");
}

void MPServer::Loop()
{
#if defined(__ANDROID__) || defined(__linux__)
    // 2.17: the game thread runs flat out once avatars are on screen; give the network thread a
    // head start so packets are not left waiting in the socket (best effort, may be refused)
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), -8);
#endif
    uint8_t buf[1024];
    uint64_t lastCheck = MP_NowMs();
#ifdef MP_MMSG
    // 3.22: up to 64 packets per system call, handled under ONE lock, the replies sent together
    enum { RB = 64 };
    static thread_local uint8_t rbuf[RB][1024];
    sockaddr_in rfrom[RB]; iovec rio[RB]; mmsghdr rmsg[RB];
    bool mmsgOk = true;
#endif
    while(m_bRunning)
    {
#ifdef MP_MMSG
        if(mmsgOk)
        {
            for(int i = 0; i < RB; ++i)
            {
                rio[i].iov_base = rbuf[i]; rio[i].iov_len = sizeof(rbuf[i]);
                memset(&rmsg[i], 0, sizeof(rmsg[i]));
                rmsg[i].msg_hdr.msg_name = &rfrom[i]; rmsg[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
                rmsg[i].msg_hdr.msg_iov = &rio[i]; rmsg[i].msg_hdr.msg_iovlen = 1;
            }
            int n = recvmmsg(m_Sock, rmsg, RB, MSG_WAITFORONE, nullptr);
            if(n < 0 && errno == ENOSYS) mmsgOk = false;   // very old kernel: one packet at a time
            if(n > 0)
            {
                std::lock_guard<std::mutex> lk(m_Mutex);
                OutBatch ob((int)m_Sock);
                for(int i = 0; i < n; ++i)
                    if(rmsg[i].msg_len > 0 && rmsg[i].msg_hdr.msg_namelen >= sizeof(sockaddr_in))
                        Handle(rfrom[i], rbuf[i], (int)rmsg[i].msg_len);
            }
        }
        else
#endif
        {
            sockaddr_in from; socklen_t fl = sizeof(from);
            int r = (int)recvfrom(m_Sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
            if(r > 0)
            {
                std::lock_guard<std::mutex> lk(m_Mutex);
                Handle(from, buf, r);
            }
        }

        uint64_t now = MP_NowMs();
        static thread_local uint64_t lastBcw = 0;
        if(now - lastBcw >= 100)
        {
            lastBcw = now; std::lock_guard<std::mutex> lk(m_Mutex);
#ifdef MP_MMSG
            OutBatch ob((int)m_Sock);
#endif
            BcwTick(now);
            Mw3Tick(now);   // 3.23
            RpTick(now);    // 3.25
        }   // 3.04: 10 Hz referee
        if(now - lastCheck > 1000)
        {
            lastCheck = now;
            std::lock_guard<std::mutex> lk(m_Mutex);
#ifdef MP_MMSG
            OutBatch ob((int)m_Sock);
#endif
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                if(m_Slots[i].used && now - m_Slots[i].lastSeen > MP_TIMEOUT_MS) Drop(i, LEAVE_TIMEOUT);
            static thread_local uint32_t chalTick = 0;
            if(++chalTick % 3 == 0)   // 3.21.6: the login challenge again (UDP may lose it)
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                    if(m_Slots[i].used && !m_Slots[i].authed && m_Slots[i].chal[0]) AuthBegin(i);
            static thread_local uint64_t lastWorld = 0;
            if(now - lastWorld >= 3000) { lastWorld = now; PktWorld w; FillWorld(w); Broadcast(&w, sizeof(w)); }
            ExpirePickups(now);
            AdTick(now);     // 3.11: Discord message every 45 min 20 s
            TurfTick(now);   // 3.01
            RaceQueueTick(now, false);
        }
    }
}

// 3.11: "Join our discord server..." to everybody on this server every 45 min 20 s. One clock per
// server, started when the server starts: players joining never trigger it (no spam on join), and
// nothing is sent while the server is empty.
void MPServer::AdTick(uint64_t now)
{
    if(!m_nNextAd) { m_nNextAd = now + m_nAdInterval; return; }
    if(now < m_nNextAd) return;
    m_nNextAd = now + m_nAdInterval;
    bool any = false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) { any = true; break; }
    if(!any) return;
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0x7289DAFF;   // Discord blurple
    MP_CopyStr(m.text, MP_AD_TEXT, sizeof(m.text));
    Broadcast(&m, sizeof(m));
    Log("Sent the Discord message to everybody");
}

void MPServer::Handle(const sockaddr_in& from, const uint8_t* data, int len)
{
    int slot = FindSlot(from);
    if(slot >= 0) m_Slots[slot].lastSeen = MP_NowMs();
    // 3.21.6: not logged in yet: only joining, ping, leaving and the login itself
    if(slot >= 0 && !m_Slots[slot].authed)
    {
        uint8_t t = data[0];
        if(t != PKT_JOIN && t != PKT_PING && t != PKT_LEAVE && t != PKT_AUTH && t != PKT_QUERY && t != PKT_PAUSE) return;
    }

    switch(data[0])
    {
        case PKT_AUTH:
            if(slot >= 0) AuthHandle(slot, data, len);
            return;
        case PKT_QUERY:   // server list: anyone may ask, no slot needed
        {
            if(len < (int)sizeof(PktQuery)) return;
            const PktQuery* q = (const PktQuery*)data;
            PktInfo5 in5; memset(&in5, 0, sizeof(in5));   // 2.27: + start time
            PktInfo4& in4 = in5.i4;   // 2.25: + language (2.24: + icon; old clients read the first part)
            PktInfo3& in3 = in4.i3;
            PktInfo2& in2 = in3.i2;
            PktInfo& in = in2.i;
            in.type = PKT_INFO; in.time = q->time; in.proto = MP_PROTO_VERSION;
            int n = HasBot() ? 1 : 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) ++n;   // 3.23.5: + bot
            in.players = (uint8_t)n; in.maxPlayers = (uint8_t)m_nMaxPlayers.load(); in.region = (uint8_t)m_nRegion.load();
            MP_CopyStr(in.serverName, m_szName, sizeof(in.serverName));
            in2.locked = m_szPassword[0] ? 1 : 0;   // 2.19: old clients read only PktInfo
            in3.icon = (uint8_t)m_nIcon.load();
            MP_CopyStr(in4.lang, m_szLang, sizeof(in4.lang));
            in5.startUnix = m_nStartUnix.load();
            SendRaw(from, &in5, sizeof(in5));
            return;
        }
        case PKT_JOIN:
        {
            if(len < (int)sizeof(PktJoin)) return;
            const PktJoin* j = (const PktJoin*)data;
            if(j->proto != MP_PROTO_VERSION)
            {
                PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_VERSION; SendRaw(from, &rj, sizeof(rj));
                return;
            }
            if(slot < 0)
            {   // 3.04.6: old MP versions (no build / too old) are refused and tracked - they allowed cheats
                uint32_t build = 0;
                if(len >= (int)sizeof(PktJoin5)) memcpy(&build, data + offsetof(PktJoin5, build), sizeof(build));
                if(build < m_nMinBuild || (AuthOn() && build < MP_AUTH_MIN_BUILD) || (IsMw3Server() && build < MW3_MIN_BUILD) || (IsRpServer() && build < MP_RP_BUILD))   // 3.23: MW3 needs 3.23   // 3.21.6: older phones can't log in
                {
                    TrackOutdated(from, data, len, build);
                    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF5050FF;
                    snprintf(m.text, sizeof(m.text), "Your MP version is too old. Download MP %s: %s", MP_VERSION_STR, MP_DOWNLOAD_URL);
                    SendRaw(from, &m, sizeof(m));
                    PktReject rj; rj.type = PKT_REJECT; rj.reason = build ? REJECT_OUTDATED : REJECT_VERSION;   // old phones know REJECT_VERSION
                    SendRaw(from, &rj, sizeof(rj));
                    return;
                }
            }
            if(slot < 0 && m_szPassword[0])   // 2.19: password protected server
            {
                char pw[MP_PASSWORD_LEN + 1] = {0};
                if(len >= (int)sizeof(PktJoin2)) memcpy(pw, ((const PktJoin2*)data)->password, MP_PASSWORD_LEN);
                if(strcmp(pw, m_szPassword))
                {
                    PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_PASSWORD; SendRaw(from, &rj, sizeof(rj));
                    Log("Join refused (wrong password)");
                    return;
                }
            }
            // 2.25: banned? (by this phone's id, or by name for phones older than 2.25)
            char juid[MP_UID_LEN + 1] = {0};
            if(len >= (int)sizeof(PktJoin3)) { memcpy(juid, ((const PktJoin3*)data)->uid, MP_UID_LEN); juid[MP_UID_LEN] = 0; }
            if(slot < 0)
            {
                char jn[MP_NAME_LEN]; MP_CopyStr(jn, j->name, MP_NAME_LEN);
                int b = FindBan(jn, juid);
                if(b >= 0)
                {
                    SendKicked(from, m_Bans[b], true);
                    PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_BANNED; SendRaw(from, &rj, sizeof(rj));
                    Log("Join refused: %s is banned (%s)", jn, m_Bans[b].reason);
                    return;
                }
            }
            // 2.37: the name SENJI is reserved - only the phone with the admin key may use it
            bool isAdmin = false;
            if(slot < 0)
            {
                char jn[MP_NAME_LEN]; MP_CopyStr(jn, j->name, MP_NAME_LEN);
                if(MP_IsReservedName(jn))
                {
                    char key[MP_ADMIN_KEY_LEN + 1] = {0};
                    if(len >= (int)sizeof(PktJoin4)) memcpy(key, ((const PktJoin4*)data)->adminKey, MP_ADMIN_KEY_LEN);
                    if(!MP_AdminKeyOk(key, m_szAdminHash))
                    {
                        PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_NAME_RESERVED; SendRaw(from, &rj, sizeof(rj));
                        Log("Join refused: \"%s\" is a reserved name (no admin key)", jn);
                        return;
                    }
                    for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // already on (another phone / ghost slot)? the new one wins
                        if(m_Slots[i].used && m_Slots[i].admin) Drop(i, LEAVE_TIMEOUT);
                    isAdmin = true;
                }
            }
            bool fresh = false;
            if(slot < 0)
            {
                const bool bot = HasBot();   // 3.23.5: the bot takes one place and keeps its id
                int used = bot ? 1 : 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) ++used;
                if(used < m_nMaxPlayers)
                    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(!m_Slots[i].used && !(bot && i == MP_BOT_ID)) { slot = i; break; }
                if(slot < 0)
                {
                    PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_FULL; SendRaw(from, &rj, sizeof(rj));
                    return;
                }
                fresh = true;
                Slot& s = m_Slots[slot];
                memset(&s, 0, sizeof(s));
                s.used = true; s.addr = from; s.lastSeen = MP_NowMs();
                s.pair = s.pairReq = MP_INVALID_ID;
                MP_CopyStr(s.uid, juid, sizeof(s.uid)); s.joinedMs = MP_NowMs();   // 2.25
                s.spawnProtUntil = MP_NowMs() + MP_SPAWN_PROTECT_MS;   // 3.21: the first spawn is protected too
                if(len >= (int)sizeof(PktJoin5)) memcpy(&s.build, data + offsetof(PktJoin5, build), sizeof(s.build));   // 3.11: who gets voice
                MP_CopyStr(s.name, j->name[0] ? j->name : "Player", MP_NAME_LEN);
                s.name[MP_NAME_LEN - 1] = 0;
                MP_StripAdminTag(s.name);                 // 2.37: nobody can fake the [ADMIN] tag
                if(!s.name[0]) MP_CopyStr(s.name, "Player", MP_NAME_LEN);
                s.admin = isAdmin;
                s.authed = !AuthOn();   // 3.21.6
                if(isAdmin) MP_CopyStr(s.name, MP_ADMIN_TAG MP_ADMIN_NAME, MP_NAME_LEN);   // 2.37: the rank shows on every server (admin powers: default server only)
                // make the name unique
                if(bot && !strcasecmp(s.name, MP_BOT_NAME)) { char tmp[MP_NAME_LEN]; snprintf(tmp, sizeof(tmp), "%.18s_%d", s.name, slot); MP_CopyStr(s.name, tmp, MP_NAME_LEN); }   // 3.23.5
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                {
                    if(i != slot && m_Slots[i].used && !strcmp(m_Slots[i].name, s.name))
                    {
                        char tmp[MP_NAME_LEN];
                        snprintf(tmp, sizeof(tmp), "%.18s_%d", s.name, slot);
                        MP_CopyStr(s.name, tmp, MP_NAME_LEN);
                        break;
                    }
                }
                s.color = g_MPPlayerColors[slot % (sizeof(g_MPPlayerColors) / sizeof(g_MPPlayerColors[0]))];
                s.skin = j->skin;
                if(j->color) s.color = j->color | 0xFF; // player's own name colour
                Log("%s (id %d) joined", s.name, slot);
            }
            Slot& s = m_Slots[slot];
            if(!fresh && AuthOn() && s.authed && MP_NowMs() - s.joinedMs > 5000)
            {   // 3.22.1: a new join on an old slot (the game was closed / restarted and came back from the same
                // relay or NAT address before the slot timed out): log in again like a fresh join. A phone that
                // is still in the same game session sends its ticket by itself (no login screen).
                s.authed = false; s.chal[0] = 0; s.authFails = 0;
                s.joinedMs = MP_NowMs();
                Log("%s (id %d) joined again: login required", s.name, slot);
            }
            PktWelcome2 w2; memset(&w2, 0, sizeof(w2));   // 3.04.6: + build
            PktWelcome& w = w2.w;
            w.type = PKT_WELCOME; w.yourId = (uint8_t)slot; w.maxPlayers = (uint8_t)m_nMaxPlayers.load(); w.color = s.color;
            MP_CopyStr(w.serverName, m_szName, sizeof(w.serverName));
            w2.build = MP_BUILD;
            SendTo(slot, &w2, sizeof(w2));

            // tell the newcomer about everyone (including itself, so it knows its final name/colour)
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            {
                if(!m_Slots[i].used) continue;
                PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
                pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)i; pj.color = m_Slots[i].color; pj.skin = m_Slots[i].skin;
                MP_CopyStr(pj.name, m_Slots[i].name, MP_NAME_LEN);
                SendTo(slot, &pj, sizeof(pj));
                if(m_Slots[i].paused && i != slot)
                {
                    PktPause pp; pp.type = PKT_PAUSE; pp.id = (uint8_t)i; pp.paused = 1;
                    SendTo(slot, &pp, sizeof(pp));
                }
                if(m_Slots[i].status && i != slot)
                {
                    PktStatus st; memset(&st, 0, sizeof(st));
                    st.type = PKT_STATUS; st.id = (uint8_t)i; st.state = m_Slots[i].status;
                    memcpy(st.mission, m_Slots[i].mission, sizeof(st.mission));
                    SendTo(slot, &st, sizeof(st));
                }
                if(m_Slots[i].hasClothes && i != slot)   // 3.10: what everybody's CJ is wearing
                    SendTo(slot, &m_Slots[i].clothes, sizeof(PktClothes));
            }
            if(HasBot())
            {   // 3.23.5: the 24/7 bot is always in the player list
                PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
                pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)MP_BOT_ID; pj.color = 0x2BD94FFF; pj.skin = 285;
                MP_CopyStr(pj.name, MP_BOT_NAME, MP_NAME_LEN);
                SendTo(slot, &pj, sizeof(pj));
            }
            { PktWorld wd; FillWorld(wd); SendTo(slot, &wd, sizeof(wd)); }   // serverworld_host
            { PktPerms pm; MakePerms(pm, m_nPerms); SendTo(slot, &pm, sizeof(pm)); }   // 2.16
            if(s.pair != MP_INVALID_ID) SendGroupLinks(slot);   // 2.31: whole mission group
            SendPickups(slot);   // 2.13: weapons lying around
            SendJoinExtras(slot, fresh);   // 3.01: turf, teams, welcome dialog, banner textdraw
            BcwOnJoin(slot, fresh);        // 3.04: Brief Case War state
            Mw3OnJoin(slot, fresh);        // 3.23: MW3 state
            RpOnJoin(slot, fresh);         // 3.25: Mega Vil Futuristic
            if(!s.authed) AuthBegin(slot);              // 3.21.6: log in first (the others see you after that)
            else if(fresh) AnnounceJoin(slot);
            break;
        }

        case PKT_SYNC:
        {
            if(slot < 0 || len < (int)sizeof(PktSync)) return;
            // 2.13: keep the SyncExt that newer clients append (anims, velocity, sender clock)
            // 2.14: any length (compact v2 extension is variable size), capped
            uint8_t raw[MP_SYNC_MAX_LEN];
            int rawLen = len <= (int)sizeof(raw) ? len : (int)sizeof(PktSync);
            memcpy(raw, data, rawLen);
            PktSync p; memcpy(&p, data, sizeof(p));   // packed + unaligned: never read it in place
            p.id = (uint8_t)slot;
            raw[offsetof(PktSync, id)] = (uint8_t)slot;
            {
                Slot& me = m_Slots[slot];
                memcpy(me.pos, (p.d.flags & SYNC_IN_CAR) ? (const void*)p.d.carPos : (const void*)p.d.pos, sizeof(me.pos));
                me.hasPos = true;
            }
            const uint8_t stateMask = SYNC_IN_CAR | SYNC_DEAD | SYNC_MISSION | SYNC_CUTSCENE | SYNC_DRIVER;
            bool important = ((p.d.flags ^ m_Slots[slot].lastFlags) & stateMask) != 0;
            if((m_Slots[slot].lastFlags & SYNC_DEAD) && !(p.d.flags & SYNC_DEAD))
                m_Slots[slot].spawnProtUntil = MP_NowMs() + MP_SPAWN_PROTECT_MS;   // 3.21: respawned -> 5 s spawn protection
            m_Slots[slot].lastFlags = p.d.flags;
            m_Slots[slot].skin = p.d.skin;
            m_Slots[slot].paused = false;
            {   // 2.9 sync flags also carry mission / cutscene (works without PKT_STATUS)
                uint8_t st = ((p.d.flags & SYNC_MISSION) ? STATUS_MISSION : 0) | ((p.d.flags & SYNC_CUTSCENE) ? STATUS_CUTSCENE : 0);
                if((m_Slots[slot].status & STATUS_MISSION) && !(st & STATUS_MISSION)) Log("%s ended a mission", m_Slots[slot].name);
                if(!st) m_Slots[slot].mission[0] = 0;
                m_Slots[slot].status = st;
            }
            RelaySync(slot, raw, rawLen, important);
            break;
        }

        // ---- 2.13 environment ---------------------------------------------------------------
        case PKT_EXPLOSION:
        {
            if(slot < 0 || len < (int)sizeof(PktExplosion)) return;
            PktExplosion e; memcpy(&e, data, sizeof(e));
            if(e.expType > 12) return;
            {   // 3.21: no explosion (rocket, grenade, car bomb, satchel...) is shared inside a hospital safe zone
                float ep[3]; memcpy(ep, e.pos, sizeof(ep));
                if(m_bSafeZones && !IsBcwServer() && MP_InSafeZone(ep[0], ep[1], ep[2])) return;
                if(IsRpServer() && (!RpCanHurt(slot) || RpInGreen(ep[0], ep[1]))) return;   // 3.25: a peaceful city
            }
            e.id = (uint8_t)slot;
            if(m_Slots[slot].status)   // 2.19: on a mission: only the mission-sync partner shares it
            {
                float pp[3]; memcpy(pp, e.pos, sizeof(pp));
                if(m_Slots[slot].pair != MP_INVALID_ID) RelayNearOrPartner(slot, pp, &e, sizeof(e));
                return;
            }
            // 2.17 crash fix: e.pos sits at an odd offset inside a packed struct. Passing &e.pos
            // as a float* let the compiler use VLDR, which needs 4-byte alignment on ARM32 ->
            // SIGBUS (BUS_ADRALN) on the host as soon as anybody blew something up. Copy first.
            float ep[3]; memcpy(ep, e.pos, sizeof(ep));
            RelayNear(slot, ep, &e, sizeof(e));
            break;
        }
        case PKT_FIRE:
        {
            if(slot < 0 || len < (int)sizeof(PktFire)) return;
            PktFire f; memcpy(&f, data, sizeof(f));
            if(f.size < 1) f.size = 1; if(f.size > 4) f.size = 4;
            {   // 3.21: no fire inside a hospital safe zone
                float fp[3]; memcpy(fp, f.pos, sizeof(fp));
                if(m_bSafeZones && !IsBcwServer() && MP_InSafeZone(fp[0], fp[1], fp[2])) return;
                if(IsRpServer() && (!RpCanHurt(slot) || RpInGreen(fp[0], fp[1]))) return;   // 3.25
            }
            f.id = (uint8_t)slot;
            if(m_Slots[slot].status)   // 2.19: partner only
            {
                float pp[3]; memcpy(pp, f.pos, sizeof(pp));
                if(m_Slots[slot].pair != MP_INVALID_ID) RelayNearOrPartner(slot, pp, &f, sizeof(f));
                return;
            }
            float fp[3]; memcpy(fp, f.pos, sizeof(fp));          // 2.17: same alignment fix
            RelayNear(slot, fp, &f, sizeof(f));
            break;
        }
        case PKT_PICKUP_DROP:
        {
            if(slot < 0 || len < (int)sizeof(PktPickupDrop) || m_Slots[slot].status) return;
            PktPickupDrop d; memcpy(&d, data, sizeof(d));
            if(d.weapon < 1 || d.weapon > 46 || d.weapon == 19 || d.weapon == 20 || d.weapon == 21 || d.weapon == 40 || d.weapon == 44 || d.weapon == 45) return;
            int free = -1; uint64_t oldest = ~0ull; int oldestI = 0;
            for(int i = 0; i < MP_MAX_PICKUPS; ++i)
            {
                if(!m_Pickups[i].used) { free = i; break; }
                if(m_Pickups[i].born < oldest) { oldest = m_Pickups[i].born; oldestI = i; }
            }
            if(free < 0)   // full: the oldest one goes
            {
                free = oldestI;
                PktPickupRemove r; r.type = PKT_PICKUP_REMOVE; r.uid = m_Pickups[free].uid; r.takenBy = MP_INVALID_ID;
                Broadcast(&r, sizeof(r));
            }
            NetPickup& k = m_Pickups[free];
            k.used = true; k.uid = m_nNextPickup++; if(!m_nNextPickup) m_nNextPickup = 1;
            k.weapon = d.weapon; k.ammo = d.ammo; memcpy(k.pos, d.pos, sizeof(k.pos)); k.born = MP_NowMs();
            PktPickupAdd a; a.type = PKT_PICKUP_ADD; a.uid = k.uid; a.weapon = k.weapon; a.ammo = k.ammo;
            memcpy(a.pos, k.pos, sizeof(a.pos));
            Broadcast(&a, sizeof(a));
            break;
        }
        case PKT_PICKUP_TAKE:
        {
            if(slot < 0 || len < (int)sizeof(PktPickupTake)) return;
            PktPickupTake t; memcpy(&t, data, sizeof(t));
            for(int i = 0; i < MP_MAX_PICKUPS; ++i)
            {
                if(!m_Pickups[i].used || m_Pickups[i].uid != t.uid) continue;
                m_Pickups[i].used = false;   // first one wins
                PktPickupRemove r; r.type = PKT_PICKUP_REMOVE; r.uid = t.uid; r.takenBy = (uint8_t)slot;
                Broadcast(&r, sizeof(r));
                break;
            }
            break;
        }

        case PKT_STATUS:
        {
            if(slot < 0 || len < (int)sizeof(PktStatus)) return;
            PktStatus p = *(const PktStatus*)data;
            p.id = (uint8_t)slot; p.state &= (STATUS_MISSION | STATUS_CUTSCENE);
            p.mission[sizeof(p.mission) - 1] = 0;
            if(!(p.state & STATUS_MISSION)) p.mission[0] = 0;
            Slot& s = m_Slots[slot];
            if((p.state & STATUS_MISSION) && (!(s.status & STATUS_MISSION) || strcmp(s.mission, p.mission)))
                Log("%s started a mission (%s)", s.name, p.mission[0] ? p.mission : "?");
            else if(!(p.state & STATUS_MISSION) && (s.status & STATUS_MISSION))
                Log("%s ended a mission", s.name);
            s.status = p.state; memcpy(s.mission, p.mission, sizeof(s.mission));
            Broadcast(&p, sizeof(p), slot);
            break;
        }

        case PKT_PAUSE:
        {
            if(slot < 0 || len < (int)sizeof(PktPause)) return;
            PktPause p = *(const PktPause*)data;
            p.id = (uint8_t)slot; p.paused = p.paused == 2 ? 2 : (p.paused ? 1 : 0);   // 3.34: 2 = AFK
            if((p.paused == 1) != m_Slots[slot].paused)
            {
                m_Slots[slot].paused = p.paused == 1;
                Log("%s %s", m_Slots[slot].name, p.paused == 1 ? "paused" : "resumed");
            }
            Broadcast(&p, sizeof(p), slot);
            break;
        }

        case PKT_VOICE:   // 3.11: voice chat -> 3.11+ players within MP_VOICE_RANGE (no batching, no delay)
        {
            if(slot < 0 || len < (int)sizeof(PktVoiceHdr) || len > (int)sizeof(PktVoiceHdr) + MP_VOICE_MAX_BYTES) return;
            Slot& s = m_Slots[slot];
            if(s.muted) return;                                  // muted by the admin: no voice either
            uint64_t now = MP_NowMs();
            if(now - s.voiceWin >= 1000) { s.voiceWin = now; s.voiceCount = 0; }
            if(++s.voiceCount > 60) return;                      // 50 frames/s + resend slack
            uint8_t out[sizeof(PktVoiceHdr) + MP_VOICE_MAX_BYTES];
            memcpy(out, data, len);
            PktVoiceHdr* h = (PktVoiceHdr*)out;
            if((int)sizeof(PktVoiceHdr) + h->len > len) return;
            h->id = (uint8_t)slot; h->type = PKT_VOICE;
            int n = (int)sizeof(PktVoiceHdr) + h->len;
            const float r2 = MP_VOICE_RANGE * MP_VOICE_RANGE;
            const int callee = IsRpServer() ? RpCallPartner(slot) : -1;   // 3.25: a phone call reaches anywhere
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            {
                const Slot& o = m_Slots[i];
                if(i == slot || !o.used || o.build < MP_VOICE_BUILD) continue;
                if(i == callee) { SendTo(i, out, n); continue; }
                if(s.hasPos && o.hasPos)
                {
                    float dx = s.pos[0] - o.pos[0], dy = s.pos[1] - o.pos[1], dz = s.pos[2] - o.pos[2];
                    if(dx * dx + dy * dy + dz * dz > r2) continue;
                }
                SendTo(i, out, n);
            }
            break;
        }

        case PKT_CLOTHES:   // 3.10: CJ clothing sync -> everybody else (and kept for players who join later)
        {
            if(slot < 0 || len < (int)sizeof(PktClothes)) return;
            PktClothes c; memcpy(&c, data, sizeof(c));
            c.type = PKT_CLOTHES; c.id = (uint8_t)slot; c.on = c.on ? 1 : 0; c.pad = 0;
            if(!MP_ClothesSane(c)) return;
            Slot& s = m_Slots[slot];
            uint64_t now = MP_NowMs();
            bool same = s.hasClothes == (bool)c.on && (!c.on || !memcmp(&s.clothes, &c, sizeof(c)));
            if(same && now - s.lastClothes < 4000) return;      // resends: at most one every 4 s
            if(!same && now - s.lastClothes < 250) return;      // changes: at most 4 per second
            s.lastClothes = now;
            s.hasClothes = c.on != 0; s.clothes = c;
            Broadcast(&c, sizeof(c), slot);
            break;
        }

        case PKT_SET_COLOR:
        {
            if(slot < 0 || len < (int)sizeof(PktSetColor)) return;
            PktSetColor p = *(const PktSetColor*)data;
            p.id = (uint8_t)slot;
            p.color = p.color ? (p.color | 0xFF) : g_MPPlayerColors[slot % (sizeof(g_MPPlayerColors) / sizeof(g_MPPlayerColors[0]))];
            m_Slots[slot].color = p.color;
            Broadcast(&p, sizeof(p));   // everyone incl. the sender
            break;
        }

        case PKT_ADMIN_CMD:   // 2.37: only the real SENJI (joined with the admin key) may use these
        {
            if(slot < 0 || len < (int)sizeof(PktAdminCmd) || !m_Slots[slot].admin) return;
            PktAdminCmd c = *(const PktAdminCmd*)data;
            c.reason[MP_REASON_LEN - 1] = 0;
            char who[MP_NAME_LEN]; MP_CopyStr(who, m_Slots[slot].name, sizeof(who));
            PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xC080FFFF;
            if(c.cmd == ADMIN_CHEATS)
            {
                uint8_t np = c.value ? (m_nPerms | PERM_NO_CHEATS) : (m_nPerms & ~PERM_NO_CHEATS);
                SetPermsLocked(np);
                snprintf(m.text, sizeof(m.text), "%s %s cheats for everyone.", who, c.value ? "disabled" : "enabled");
                Log("%s", m.text); Broadcast(&m, sizeof(m));
                return;
            }
            int t = c.target;
            if(t >= MP_MAX_PLAYERS || !m_Slots[t].used || t == slot) return;
            if(c.cmd == ADMIN_KICK || c.cmd == ADMIN_BAN) { KickLocked(t, who, c.reason, c.cmd == ADMIN_BAN); return; }
            if(c.cmd == ADMIN_MUTE || c.cmd == ADMIN_UNMUTE)
            {
                m_Slots[t].muted = c.cmd == ADMIN_MUTE;
                snprintf(m.text, sizeof(m.text), "%s was %s by %s", m_Slots[t].name, m_Slots[t].muted ? "muted" : "unmuted", who);
                Log("%s", m.text); Broadcast(&m, sizeof(m));
            }
            return;
        }

        case PKT_CHAT:
        {
            if(slot < 0 || len < (int)sizeof(PktChat)) return;
            if(m_Slots[slot].muted)   // 2.37
            {
                PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF6060FF;
                MP_CopyStr(m.text, "You are muted by the administrator.", sizeof(m.text));
                SendTo(slot, &m, sizeof(m));
                return;
            }
            PktChat p = *(const PktChat*)data;
            p.id = (uint8_t)slot;
            p.text[MP_CHAT_LEN - 1] = 0;
            Log("[chat] %s: %s", m_Slots[slot].name, p.text);
            if(IsRpServer())
            {   // 3.25: roleplay: people hear you only when they are close ("Name says: ...")
                PktRpCmd rc; memset(&rc, 0, sizeof(rc)); rc.type = PKT_RP_CMD; rc.cmd = RPC_CHAT; rc.a = RPH_SAY;
                MP_CopyStr(rc.text, p.text, sizeof(rc.text));
                RpHandleCmd(slot, rc);
                break;
            }
            Broadcast(&p, sizeof(p));
            break;
        }

        case PKT_ANNOUNCE:   // 2.21: game events (races, DM, teleports), max 2 per second per player
        {
            if(slot < 0 || len < (int)sizeof(PktAnnounce)) return;
            static uint64_t s_LastAnn[MP_MAX_PLAYERS] = {};
            uint64_t t = MP_NowMs();
            if(t - s_LastAnn[slot] < 500) return;
            s_LastAnn[slot] = t;
            const PktAnnounce& a = *(const PktAnnounce*)data;
            PktServerMsg m; memset(&m, 0, sizeof(m));
            m.type = PKT_SERVER_MSG;
            m.color = a.color | 0xFF;
            memcpy(m.text, a.text, MP_CHAT_LEN - 1); m.text[MP_CHAT_LEN - 1] = 0;
            for(char* c = m.text; *c; ++c) if((unsigned char)*c < 32) *c = ' ';
            Log("[event] %s: %s", m_Slots[slot].name, m.text);
            Broadcast(&m, sizeof(m));
            break;
        }

        case PKT_DEATH:
        {
            if(slot < 0 || len < (int)sizeof(PktDeath)) return;
            PktDeath p = *(const PktDeath*)data;
            p.victim = (uint8_t)slot;
            if(p.killer >= MP_MAX_PLAYERS || !m_Slots[p.killer].used) p.killer = MP_INVALID_ID;
            BcwOnDeath(slot, p.killer == MP_INVALID_ID ? -1 : p.killer);   // 3.04
            RpOnDeath(slot, p.killer == MP_INVALID_ID ? -1 : p.killer);    // 3.25
            Mw3OnDeath(slot, p.killer == MP_INVALID_ID ? -1 : p.killer, p.weapon);   // 3.23
            Broadcast(&p, sizeof(p));
            break;
        }

        case PKT_DAMAGE:
        {
            if(slot < 0 || len < (int)sizeof(PktDamage)) return;
            PktDamage p = *(const PktDamage*)data;
            p.from = (uint8_t)slot;
            // paused players and players on a mission / in a cutscene can't be hurt (or hurt others)
            // 2.19: mission-sync partners share one story world: they can hurt each other on a mission
            if(p.target < MP_MAX_PLAYERS && p.target != slot && m_Slots[p.target].used && !m_Slots[p.target].paused
               && !SafeZoneBlocks(slot, p.target)   // 3.21: hospital safe zones + 5 s spawn protection
               && ((!m_Slots[p.target].status && !m_Slots[slot].status) || IsPartner(slot, p.target))
               && BcwOnDamage(slot, p.target, p.amount) && Mw3OnDamage(slot, p.target, p.amount) && RpOnDamage(slot, p.target, p.amount)) SendTo(p.target, &p, sizeof(p));   // 3.04: no friendly fire in Brief Case War
            break;
        }

        case PKT_MW3_CARD:   // 3.24: the phone's stats backup
        {
            if(slot < 0 || len < (int)sizeof(PktMw3Card)) return;
            PktMw3Card c; memcpy(&c, data, sizeof(c));
            Mw3OnCard(slot, c);
            break;
        }
        case PKT_RP_CARD:   // 3.25: the phone's citizen backup
        {
            if(slot < 0 || len < (int)sizeof(PktRpCard)) return;
            PktRpCard c; memcpy(&c, data, sizeof(c));
            RpOnCard(slot, c);
            break;
        }
        case PKT_RP_CMD:    // 3.25
        {
            if(slot < 0 || len < (int)sizeof(PktRpCmd)) return;
            PktRpCmd c; memcpy(&c, data, sizeof(c)); c.text[sizeof(c.text) - 1] = 0;
            RpHandleCmd(slot, c);
            break;
        }
        case PKT_MW3_CMD:   // 3.23
        {
            if(slot < 0 || len < (int)sizeof(PktMw3Cmd)) return;
            Mw3HandleCmd(slot, *(const PktMw3Cmd*)data);
            break;
        }

        case PKT_BCW_CMD:   // 3.04
        {
            if(slot < 0 || len < (int)sizeof(PktBcwCmd)) return;
            BcwHandleCmd(slot, *(const PktBcwCmd*)data);
            break;
        }

        case PKT_PING:
        {
            if(len < (int)sizeof(PktPing)) return;
            PktPing p = *(const PktPing*)data;
            p.type = PKT_PONG;
            // 3.33/3.34: answer NOW, not in the sendmmsg batch (a join in the same batch held the pong back)
            sendto(m_Sock, (const char*)&p, sizeof(p), 0, (const sockaddr*)&from, sizeof(from));
            break;
        }

        case PKT_LEAVE:
            // 3.22.1: a phone sends a leave right before a fresh join (ghost slot of a closed game); one that
            // arrives just after that join (UDP order) must not drop the new slot
            if(slot >= 0 && !(m_Slots[slot].joinedMs && MP_NowMs() - m_Slots[slot].joinedMs < 1500)) Drop(slot, LEAVE_QUIT);
            break;

        // ---- 2.17: shots, car state, mission markers ------------------------------------
        case PKT_SHOT:
        {
            if(slot < 0 || len < MP_PKT_SHOT_HDR + (int)sizeof(ShotEntry)) return;
            PktShot m; memset(&m, 0, sizeof(m));
            memcpy(&m, data, len < (int)sizeof(m) ? (size_t)len : sizeof(m));
            int fit = (len - MP_PKT_SHOT_HDR) / (int)sizeof(ShotEntry);
            if(m.count > fit) m.count = (uint8_t)fit;
            if(m.count > MP_SHOT_MAX) m.count = MP_SHOT_MAX;
            if(!m.count) return;
            m.id = (uint8_t)slot;
            float sp[3]; memcpy(sp, m.e[0].a, sizeof(sp));       // aligned copy (see PKT_EXPLOSION)
            RelayNearOrPartner(slot, sp, &m, MP_PKT_SHOT_HDR + m.count * (int)sizeof(ShotEntry));
            break;
        }
        case PKT_VEH_STATE:
        {
            if(slot < 0 || len < (int)sizeof(PktVehState)) return;
            PktVehState v; memcpy(&v, data, sizeof(v));
            v.id = (uint8_t)slot;
            float vp[3]; memcpy(vp, v.pos, sizeof(vp));
            RelayNearOrPartner(slot, vp, &v, sizeof(v));
            break;
        }
        // ---- 2.18: NPC sync ------------------------------------------------------------------
        case PKT_NPC:
        {
            if(slot < 0 || m_Slots[slot].status) return;     // a host on a mission doesn't share its world
            PktNpc m;
            if(!MP_DecodeNpc((const uint8_t*)data, len, m)) return;
            m.h.id = (uint8_t)slot;
            uint8_t out[sizeof(PktNpc)]; int n = MP_EncodeNpc(m, out);
            float c[3]; memcpy(c, m.h.centre, sizeof(c));
            RelayNear(slot, c, out, n, MP_NPC_RANGE_M);
            break;
        }
        case PKT_NPC_HIT:
        {
            if(slot < 0 || len < (int)sizeof(PktNpcHit)) return;
            PktNpcHit h; memcpy(&h, data, sizeof(h));
            if(h.owner >= MP_MAX_PLAYERS || h.owner == slot || !m_Slots[h.owner].used) return;
            h.from = (uint8_t)slot;
            SendTo(h.owner, &h, sizeof(h));
            break;
        }
        case PKT_NPC_CLAIM:
        {
            if(slot < 0 || len < (int)sizeof(PktNpcClaim)) return;
            PktNpcClaim c; memcpy(&c, data, sizeof(c));
            if(c.owner >= MP_MAX_PLAYERS || c.owner == slot || !m_Slots[c.owner].used) return;
            c.from = (uint8_t)slot;
            SendTo(c.owner, &c, sizeof(c));
            break;
        }
        case PKT_MISSION_NPC:   // 3.02: mission peds / cars, only for my mission-sync group
        {
            if(slot < 0) return;
            if(!(m_nPerms & PERM_MISSION_PAIR) || m_Slots[slot].pair == MP_INVALID_ID) return;
            PktNpc m;
            if(!MP_DecodeNpc((const uint8_t*)data, len, m)) return;
            m.h.type = PKT_MISSION_NPC; m.h.id = (uint8_t)slot;
            uint8_t out[sizeof(PktNpc)]; int n = MP_EncodeNpc(m, out);
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(IsPartner(slot, i)) SendTo(i, out, n);
            break;
        }
        case PKT_MISSION_MARKERS:   // only for my mission-sync partner
        {
            if(slot < 0 || len < MP_PKT_MM_HDR) return;
            Slot& s = m_Slots[slot];
            if(!(m_nPerms & PERM_MISSION_PAIR) || s.pair == MP_INVALID_ID) return;
            PktMissionMarkers m; memset(&m, 0, sizeof(m));
            memcpy(&m, data, len < (int)sizeof(m) ? (size_t)len : sizeof(m));
            int fit = (len - MP_PKT_MM_HDR) / (int)sizeof(MissionMarker);
            if(m.count > fit) m.count = (uint8_t)fit;
            if(m.count > MP_MISSION_MARKERS_MAX) m.count = MP_MISSION_MARKERS_MAX;
            m.id = (uint8_t)slot;
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)   // 2.31: the whole mission group
                if(IsPartner(slot, i)) SendTo(i, &m, MP_PKT_MM_HDR + m.count * (int)sizeof(MissionMarker));
            break;
        }
        case PKT_PLAYER_INFO:   // 2.19: ping + score for the scoreboard
        {
            if(slot < 0 || len < (int)sizeof(PktPlayerInfo)) return;
            PktPlayerInfo pi; memcpy(&pi, data, sizeof(pi));
            pi.id = (uint8_t)slot;
            Broadcast(&pi, sizeof(pi), slot);
            break;
        }
        case PKT_EVENT:   // 3.01: race / derby lobby + positions -> everybody else
        {
            if(slot < 0 || len < (int)sizeof(PktEvent)) return;
            PktEvent e; memcpy(&e, data, sizeof(e));
            if(e.kind == EVK_NONE || e.kind > EVK_DERBY || e.state < EVS_LOBBY || e.state > EVS_LEAVE) return;
            uint64_t now = MP_NowMs();
            Slot& s = m_Slots[slot];
            if(e.kind == EVK_RACE && e.idx == MP_EV_QUEUE)   // 3.01: race queue, decided here
            {
                bool was = s.raceQueuedAt != 0;
                if(!(GetModes() & GM_RACE)) return;   // 3.02: gamemode off
                if(e.state == EVS_LOBBY && !was) { s.raceQueuedAt = now; Log("%s joined the race queue", s.name); }
                else if(e.state == EVS_LEAVE && was) s.raceQueuedAt = 0;
                RaceQueueTick(now, was != (s.raceQueuedAt != 0));
                return;
            }
            if(e.state == EVS_PROGRESS && now - s.lastEvent < 400) return;   // 2-3 per second is plenty
            s.lastEvent = now;
            if(!(e.dist == e.dist)) e.dist = 0;
            e.id = (uint8_t)slot;
            Broadcast(&e, sizeof(e), slot);
            break;
        }
        case PKT_TEAM:    // 3.01: class selection (turf wars)
        {
            if(slot < 0 || len < (int)sizeof(PktTeam)) return;
            PktTeam t; memcpy(&t, data, sizeof(t));
            if(t.team >= TEAM_COUNT) t.team = TEAM_NONE;
            Slot& s = m_Slots[slot];
            bool changed = s.team != t.team;
            s.team = t.team; s.classSkin = t.skin;
            t.id = (uint8_t)slot;
            if(changed)
            {
                Broadcast(&t, sizeof(t), slot);
                Log("%s is now %s", s.name, g_MPTeams[s.team].name);
            }
            break;
        }
        case PKT_DIALOG_RESPONSE:   // 3.01: SA-MP dialog answer (only the welcome dialog for now)
        {
            if(slot < 0 || len < (int)sizeof(PktDialogResp)) return;
            break;
        }
        case PKT_JACK:   // 2.19: carjack another player
        {
            if(slot < 0 || len < (int)sizeof(PktJack)) return;
            PktJack j; memcpy(&j, data, sizeof(j));
            if(j.victim >= MP_MAX_PLAYERS || j.victim == slot || !m_Slots[j.victim].used || m_Slots[j.victim].paused) return;
            if((m_Slots[j.victim].status || m_Slots[slot].status) && !IsPartner(slot, j.victim)) return;
            if(SafeZoneBlocks(slot, j.victim)) return;   // 3.21: nobody is pulled out of a car in a safe zone
            j.from = (uint8_t)slot;
            SendTo(j.victim, &j, sizeof(j));
            Log("%s carjacked %s", m_Slots[slot].name, m_Slots[j.victim].name);
            break;
        }
        case PKT_PAIR:   // 2.16: mission sync (2.17 name)
        {
            if(slot < 0 || len < (int)sizeof(PktPair)) return;
            PktPair q = *(const PktPair*)data;
            Slot& s = m_Slots[slot];
            if(q.action == PAIR_END)
            {   // 2.31: other = a member of my group -> Unsync that player; else I leave the group
                int o = q.other;
                if(o < MP_MAX_PLAYERS && o != slot && IsPartner(slot, o) && GroupSize(s.pair) > 2)
                {
                    Log("%s unsynced %s", s.name, m_Slots[o].name);
                    EndPair(o);
                }
                else if(s.pair != MP_INVALID_ID) EndPair(slot);
                s.pairReq = MP_INVALID_ID;
                break;
            }
            if(q.action == PAIR_DECLINE)   // 2.19: the asked player said no
            {
                int o = q.other;
                if(o < MP_MAX_PLAYERS && m_Slots[o].used && m_Slots[o].pairReq == slot)
                {
                    m_Slots[o].pairReq = MP_INVALID_ID;
                    PktPair a; a.type = PKT_PAIR; a.action = PAIR_DECLINED; a.other = (uint8_t)slot; SendTo(o, &a, sizeof(a));
                }
                break;
            }
            int o = q.other;
            PktPair a; a.type = PKT_PAIR;
            if(!(m_nPerms & PERM_MISSION_PAIR) || o >= MP_MAX_PLAYERS || o == slot || !m_Slots[o].used)
            { a.action = PAIR_DENIED; a.other = (uint8_t)(o < MP_MAX_PLAYERS ? o : MP_INVALID_ID); SendTo(slot, &a, sizeof(a)); break; }
            if(q.action == PAIR_REQUEST)
            {
                if(IsPartner(slot, o)) break;
                if(m_Slots[o].pairReq == slot) q.action = PAIR_ACCEPT;   // both asked each other
                else { s.pairReq = (uint8_t)o; a.action = PAIR_REQUEST; a.other = (uint8_t)slot; SendTo(o, &a, sizeof(a)); break; }
            }
            if(q.action == PAIR_ACCEPT)
            {
                if(m_Slots[o].pairReq != slot) { a.action = PAIR_DENIED; a.other = (uint8_t)o; SendTo(slot, &a, sizeof(a)); break; }
                // 2.31: join a group. The asker's group wins, else mine, else a new one. Up to
                // MP_MISSION_GROUP_MAX players do the same missions together.
                uint8_t g = m_Slots[o].pair != MP_INVALID_ID ? m_Slots[o].pair : s.pair;
                if(g != MP_INVALID_ID && s.pair != g && GroupSize(g) >= MP_MISSION_GROUP_MAX)
                { a.action = PAIR_DENIED; a.other = (uint8_t)o; SendTo(slot, &a, sizeof(a)); SendTo(o, &a, sizeof(a)); s.pairReq = m_Slots[o].pairReq = MP_INVALID_ID; break; }
                if(g == MP_INVALID_ID)
                {
                    do { m_nNextGroup = (uint8_t)(m_nNextGroup % 250 + 1); } while(GroupSize(m_nNextGroup));
                    g = m_nNextGroup;
                }
                if(s.pair != MP_INVALID_ID && s.pair != g) EndPair(slot);
                if(m_Slots[o].pair != MP_INVALID_ID && m_Slots[o].pair != g) EndPair(o);
                bool oNew = m_Slots[o].pair != g, sNew = s.pair != g;
                s.pair = g; m_Slots[o].pair = g;
                s.pairReq = m_Slots[o].pairReq = MP_INVALID_ID;
                if(sNew) SendGroupLinks(slot);
                if(oNew) SendGroupLinks(o);
                Log("%s and %s: Mission sync (group of %d)", s.name, m_Slots[o].name, GroupSize(g));
            }
            break;
        }
    }
}


// ---- 2.25 player management: kick / ban -------------------------------------------------------
static void BanClean(char* s) { for(; *s; ++s) if(*s == '|' || *s == '\n' || *s == '\r') *s = ' '; }

void MPServer::GetPlayers(std::vector<PlayerView>& out)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    out.clear();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used) continue;
        PlayerView v; memset(&v, 0, sizeof(v));
        v.id = i; MP_CopyStr(v.name, m_Slots[i].name, sizeof(v.name)); MP_CopyStr(v.uid, m_Slots[i].uid, sizeof(v.uid));
        v.color = m_Slots[i].color; v.joinedMs = m_Slots[i].joinedMs;
        out.push_back(v);
    }
}

int MPServer::FindBan(const char* name, const char* uid)
{
    for(size_t i = 0; i < m_Bans.size(); ++i)
    {
        const BanEntry& b = m_Bans[i];
        if(uid && uid[0] && b.uid[0] && !strcmp(uid, b.uid)) return (int)i;
        if(name && name[0] && b.name[0] && !strcasecmp(name, b.name)) return (int)i;
    }
    return -1;
}

void MPServer::SendKicked(const sockaddr_in& a, const BanEntry& b, bool banned)
{
    // older phones don't know PKT_KICKED: they get the same text as a system message first
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF4040FF;
    snprintf(m.text, sizeof(m.text), "%s Administrator: %s  Reason: %s", banned ? "You are banned from the server!" : "You were kicked from the server!",
             b.admin, b.reason[0] ? b.reason : "-");
    SendRaw(a, &m, sizeof(m));
    PktKicked k; memset(&k, 0, sizeof(k));
    k.type = PKT_KICKED; k.banned = banned ? 1 : 0; k.when = b.when;
    MP_CopyStr(k.admin, b.admin, sizeof(k.admin)); MP_CopyStr(k.reason, b.reason, sizeof(k.reason));
    for(int r = 0; r < 2; ++r) SendRaw(a, &k, sizeof(k));
}

bool MPServer::Kick(int id, const char* admin, const char* reason, bool ban)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    return KickLocked(id, admin, reason, ban);
}

bool MPServer::KickLocked(int id, const char* admin, const char* reason, bool ban)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !m_Slots[id].used) return false;
    BanEntry b; memset(&b, 0, sizeof(b));
    MP_CopyStr(b.uid, m_Slots[id].uid, sizeof(b.uid)); MP_CopyStr(b.name, m_Slots[id].name, sizeof(b.name));
    b.when = (uint32_t)time(nullptr);
    MP_CopyStr(b.admin, admin && admin[0] ? admin : "Server", sizeof(b.admin));
    MP_CopyStr(b.reason, reason ? reason : "", sizeof(b.reason));
    BanClean(b.name); BanClean(b.admin); BanClean(b.reason);
    if(ban && FindBan(b.name, b.uid) < 0) { m_Bans.push_back(b); SaveBans(); }
    sockaddr_in a = m_Slots[id].addr;
    SendKicked(a, b, ban);
    PktReject rj; rj.type = PKT_REJECT; rj.reason = ban ? REJECT_BANNED : REJECT_KICKED;
    for(int r = 0; r < 2; ++r) SendRaw(a, &rj, sizeof(rj));
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF6060FF;
    snprintf(m.text, sizeof(m.text), "%s was %s by %s%s%s", b.name, ban ? "banned" : "kicked", b.admin, b.reason[0] ? ". Reason: " : "", b.reason);
    Log("%s", m.text);
    Drop(id, LEAVE_KICK);
    Broadcast(&m, sizeof(m));
    return true;
}

bool MPServer::AddBan(const char* name, const char* uid, const char* admin, const char* reason)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if((!name || !name[0]) && (!uid || !uid[0])) return false;
    if(FindBan(name, uid) >= 0) return false;
    BanEntry b; memset(&b, 0, sizeof(b));
    MP_CopyStr(b.uid, uid ? uid : "", sizeof(b.uid)); MP_CopyStr(b.name, name ? name : "", sizeof(b.name));
    b.when = (uint32_t)time(nullptr);
    MP_CopyStr(b.admin, admin && admin[0] ? admin : "Server", sizeof(b.admin));
    MP_CopyStr(b.reason, reason ? reason : "", sizeof(b.reason));
    BanClean(b.name); BanClean(b.admin); BanClean(b.reason);
    m_Bans.push_back(b); SaveBans();
    return true;
}

void MPServer::GetBans(std::vector<BanEntry>& out) { std::lock_guard<std::mutex> lk(m_Mutex); out = m_Bans; }

bool MPServer::Unban(int index)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(index < 0 || index >= (int)m_Bans.size()) return false;
    Log("Unbanned %s", m_Bans[index].name);
    m_Bans.erase(m_Bans.begin() + index); SaveBans();
    return true;
}

void MPServer::SetBanFile(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szBanFile, path ? path : "", sizeof(m_szBanFile));
    m_Bans.clear();
    FILE* f = m_szBanFile[0] ? fopen(m_szBanFile, "r") : nullptr;
    if(!f) return;
    char line[512];
    while(fgets(line, sizeof(line), f))
    {
        if(line[0] == '#' || line[0] == '\n') continue;
        char* p[5] = { line }; int n = 1;
        for(char* c = line; *c && n < 5; ++c) if(*c == '|') { *c = 0; p[n++] = c + 1; }
        if(n < 5) continue;
        for(char* c = p[4]; *c; ++c) if(*c == '\n' || *c == '\r') { *c = 0; break; }
        BanEntry b; memset(&b, 0, sizeof(b));
        MP_CopyStr(b.uid, p[0], sizeof(b.uid)); MP_CopyStr(b.name, p[1], sizeof(b.name)); b.when = (uint32_t)strtoul(p[2], nullptr, 10);
        MP_CopyStr(b.admin, p[3], sizeof(b.admin)); MP_CopyStr(b.reason, p[4], sizeof(b.reason));
        if(b.uid[0] || b.name[0]) m_Bans.push_back(b);
    }
    fclose(f);
}

void MPServer::SaveBans()
{
    if(!m_szBanFile[0]) return;
    FILE* f = fopen(m_szBanFile, "w");
    if(!f) return;
    fprintf(f, "# MP ban list (2.25): phone id|name|unix time|administrator|reason\n");
    for(auto& b : m_Bans) fprintf(f, "%s|%s|%u|%s|%s\n", b.uid, b.name, b.when, b.admin, b.reason);
    fclose(f);
}

void MPServer::SetLang(const char* l)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szLang, l ? l : "", sizeof(m_szLang)); MP_CleanLang(m_szLang);
}
void MPServer::GetLang(char* out, size_t n) { std::lock_guard<std::mutex> lk(m_Mutex); MP_CopyStr(out, m_szLang, n); }

// ---- 3.01: SA-MP features: turf wars, welcome dialog, banner textdraw ------------------------
void MPServer::SetMotd(const char* caption, const char* text)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szMotdCaption, caption && *caption ? caption : m_szName, sizeof(m_szMotdCaption));
    MP_CopyStr(m_szMotd, text ? text : "", sizeof(m_szMotd));
}
void MPServer::SetBanner(const char* text)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szBanner, text ? text : "", sizeof(m_szBanner));
}
void MPServer::SetTurfFile(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szTurfFile, path ? path : "", sizeof(m_szTurfFile));
    LoadTurf();
}
void MPServer::LoadTurf()
{
    for(int i = 0; i < MP_TURF_MAX; ++i) { m_TurfOwner[i] = i < MP_TURF_COUNT ? g_MPTurf[i].owner : 0; m_TurfAtk[i] = 0; m_TurfProg[i] = 0; }
    if(!m_szTurfFile[0]) return;
    FILE* f = fopen(m_szTurfFile, "r"); if(!f) return;
    char line[128]; int n = 0;
    while(fgets(line, sizeof(line), f))
    {
        int zi = -1, ow = -1;
        if(sscanf(line, "%d %d", &zi, &ow) == 2 && zi >= 0 && zi < MP_TURF_COUNT && MP_TeamCanTakeTurf(ow)) { m_TurfOwner[zi] = (uint8_t)ow; ++n; }
    }
    fclose(f);
    Log("turf: %d zone owners loaded from %s", n, m_szTurfFile);
}
void MPServer::SaveTurf()
{
    if(!m_szTurfFile[0]) return;
    FILE* f = fopen(m_szTurfFile, "w"); if(!f) return;
    fprintf(f, "# zone owner (MP 3.01 turf wars: %d zones)\n", MP_TURF_COUNT);
    for(int i = 0; i < MP_TURF_COUNT; ++i) fprintf(f, "%d %d %s\n", i, m_TurfOwner[i], g_MPTurf[i].name);
    fclose(f);
}
void MPServer::SendTurf(int slot)
{
    PktTurf t; memset(&t, 0, sizeof(t));
    t.type = PKT_TURF; t.count = (uint8_t)MP_TURF_COUNT;
    for(int i = 0; i < MP_TURF_COUNT; ++i)
    {
        t.owner[i] = m_TurfOwner[i]; t.attacker[i] = m_TurfAtk[i];
        float p = m_TurfProg[i] * 100.0f / MP_TURF_CAPTURE_SECS;
        t.progress[i] = (uint8_t)(p < 0 ? 0 : p > 100 ? 100 : p);
    }
    if(slot < 0) Broadcast(&t, sizeof(t)); else SendTo(slot, &t, sizeof(t));
}
void MPServer::TurfTick(uint64_t now)
{
    if(!m_bTurfWars || !(GetModes() & GM_TURF)) return;   // 3.02: gamemode
    int cnt[MP_TURF_MAX][TEAM_COUNT]; memset(cnt, 0, sizeof(cnt));
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const Slot& s = m_Slots[i];
        if(!s.used || !s.hasPos || s.paused || s.status || s.team == TEAM_NONE || s.team >= TEAM_COUNT) continue;
        int z = MP_TurfAt(s.pos[0], s.pos[1], s.pos[2]);
        if(z >= 0) ++cnt[z][s.team];
    }
    bool changed = false, active = false;
    for(int z = 0; z < MP_TURF_COUNT; ++z)
    {
        int owner = m_TurfOwner[z];
        int defenders = cnt[z][owner] + cnt[z][TEAM_POLICE];
        int atkTeam = 0, atkTeams = 0, atkN = 0;
        for(int t = TEAM_GROVE; t <= TEAM_DANANG; ++t)
            if(t != owner && cnt[z][t] > 0) { ++atkTeams; if(cnt[z][t] > atkN) { atkN = cnt[z][t]; atkTeam = t; } }
        uint8_t oldAtk = m_TurfAtk[z];
        if(atkTeams == 1 && defenders == 0)
        {
            if(m_TurfAtk[z] != atkTeam) { m_TurfAtk[z] = (uint8_t)atkTeam; m_TurfProg[z] = 0; }
            m_TurfProg[z] += atkN > 3 ? 3 : atkN;
            if(m_TurfProg[z] >= MP_TURF_CAPTURE_SECS)
            {
                PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = g_MPTeams[atkTeam].rgba;
                snprintf(m.text, sizeof(m.text), "TURF: %s took %s from %s!", g_MPTeams[atkTeam].name, g_MPTurf[z].name, g_MPTeams[owner].name);
                Broadcast(&m, sizeof(m));
                Log("%s", m.text);
                m_TurfOwner[z] = (uint8_t)atkTeam; m_TurfAtk[z] = 0; m_TurfProg[z] = 0;
                SaveTurf();
            }
        }
        else if(m_TurfAtk[z])
        {
            // contested (defenders / police / two gangs) holds the war; nobody there = it fades out
            if(defenders > 0 && atkTeams == 0) m_TurfProg[z] -= 3;
            else if(atkTeams == 0) m_TurfProg[z] -= 1;
            if(m_TurfProg[z] <= 0) { m_TurfProg[z] = 0; m_TurfAtk[z] = 0; }
        }
        if(oldAtk != m_TurfAtk[z]) changed = true;
        if(m_TurfAtk[z]) active = true;
    }
    if(changed || (active && now - m_nLastTurfSend >= 2000) || now - m_nLastTurfSend >= 15000)
    {
        m_nLastTurfSend = now;
        SendTurf(-1);
    }
}
void MPServer::SendJoinExtras(int slot, bool fresh)
{
    if(GetModes() & GM_TURF) SendTurf(slot);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == slot || !m_Slots[i].used || m_Slots[i].team == TEAM_NONE) continue;
        PktTeam t; memset(&t, 0, sizeof(t)); t.type = PKT_TEAM; t.id = (uint8_t)i; t.team = m_Slots[i].team; t.skin = m_Slots[i].classSkin;
        SendTo(slot, &t, sizeof(t));
    }
    if(fresh && m_szMotd[0])
    {
        PktDialog d; memset(&d, 0, sizeof(d));
        d.type = PKT_DIALOG; d.style = DIALOG_STYLE_MSGBOX; d.dialogId = 1;
        MP_CopyStr(d.caption, m_szMotdCaption[0] ? m_szMotdCaption : m_szName, sizeof(d.caption));
        MP_CopyStr(d.button1, "OK", sizeof(d.button1));
        MP_CopyStr(d.info, m_szMotd, sizeof(d.info));
        SendTo(slot, &d, sizeof(d));
    }
    if(m_szBanner[0])
    {
        PktTextDraw t; memset(&t, 0, sizeof(t));
        t.type = PKT_TEXTDRAW; t.show = 1; t.tdId = 1;
        t.x = 636.0f; t.y = 436.0f; t.letterX = 0.24f; t.letterY = 1.0f;
        t.color = 0xFFFFFFC8; t.bgColor = 0x000000FF; t.font = 1; t.align = 3; t.outline = 1; t.proportional = 1;
        MP_CopyStr(t.text, m_szBanner, sizeof(t.text));
        SendTo(slot, &t, sizeof(t));
    }
}

// 3.01: race queue. Players type /race and wait (anywhere on the map); when MP_RACE_PLAYERS are in
// it, the first ones (by time joined) get one GTA SA race, each with a grid place and the same GO.
void MPServer::RaceQueueTick(uint64_t now, bool changed)
{
    static thread_local uint64_t lastSend = 0;
    int q[MP_MAX_PLAYERS], n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].raceQueuedAt) q[n++] = i;
    for(int a = 1; a < n; ++a) for(int b = a; b > 0 && m_Slots[q[b]].raceQueuedAt < m_Slots[q[b - 1]].raceQueuedAt; --b) { int t = q[b]; q[b] = q[b - 1]; q[b - 1] = t; }
    // 3.21: a race also starts without 14 players: 2+ players waited MP_RACE_WAIT_MS, or one player
    // waited MP_RACE_SOLO_MS (solo time trial). Before, the queue waited for 14 forever.
    uint64_t waited = n ? now - m_Slots[q[0]].raceQueuedAt : 0;
    int go = n >= MP_RACE_PLAYERS ? MP_RACE_PLAYERS
           : (n >= 2 && waited >= MP_RACE_WAIT_MS) ? n
           : (n == 1 && waited >= MP_RACE_SOLO_MS) ? 1 : 0;
    if(go > 0)
    {
        int race = rand() % MP_SA_RACE_COUNT;
        if(race == m_nLastRace) race = (race + 1) % MP_SA_RACE_COUNT;
        m_nLastRace = race;
        for(int k = 0; k < go; ++k)
        {
            PktEvent e; memset(&e, 0, sizeof(e));
            e.type = PKT_EVENT; e.id = MP_EV_SERVER; e.kind = EVK_RACE; e.idx = (uint8_t)race; e.state = EVS_LOBBY;
            e.slot = (uint8_t)k; e.cp = (uint8_t)go; e.ms = MP_RACE_GO_MS;   // 3.21: 20 s to get ready (was 12)
            SendTo(q[k], &e, sizeof(e));
            m_Slots[q[k]].raceQueuedAt = 0;
        }
        Log("Race %d starts with %d players", race, go);
        for(int k = go; k < n; ++k) q[k - go] = q[k];
        n -= go;
        changed = true;
    }
    if(!changed && now - lastSend < 3000) return;
    lastSend = now;
    for(int k = 0; k < n; ++k)
    {
        PktEvent e; memset(&e, 0, sizeof(e));
        e.type = PKT_EVENT; e.id = MP_EV_SERVER; e.kind = EVK_RACE; e.idx = MP_EV_QUEUE; e.state = EVS_LOBBY;
        e.cp = (uint8_t)n; e.slot = MP_RACE_PLAYERS;
        SendTo(q[k], &e, sizeof(e));
    }
}

#include "server_bcw.inc"   // 3.04: Brief Case War
#include "server_mw3.inc"   // 3.23: Call Of Duty: Modern Warfare 3
#include "server_rp.inc"    // 3.25: Mega Vil Futuristic (Roleplay)

// 3.04.6: an old MP version tried to join: log it once per phone every 10 minutes (console + the
// --outdated-log file of the 24/7 server) and count it
void MPServer::TrackOutdated(const sockaddr_in& from, const uint8_t* data, int len, uint32_t build)
{
    char name[MP_NAME_LEN] = "", uid[MP_UID_LEN + 1] = "", ver[24];
    memcpy(name, ((const PktJoin*)data)->name, MP_NAME_LEN - 1);
    if(len >= (int)sizeof(PktJoin3)) memcpy(uid, ((const PktJoin3*)data)->uid, MP_UID_LEN);
    for(char* c = name; *c; ++c) if((unsigned char)*c < 32) *c = '?';
    const char* v = MP_GuessOldVersion(len, build, ver, sizeof(ver));
    char ip[32]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
    std::string key = std::string(uid[0] ? uid : ip) + "|" + name;
    uint64_t now = MP_NowMs();
    {
        std::lock_guard<std::mutex> lk(m_OutdatedMutex);
        auto it = m_Outdated.find(key);
        if(it != m_Outdated.end() && now - it->second < 600000) return;
        if(m_Outdated.size() > 5000) m_Outdated.clear();
        m_Outdated[key] = now;
        ++m_nOutdatedCount;
    }
    Log("Outdated MP refused: %s (MP %s, phone %s, %s) - told to download %s", name, v, uid[0] ? uid : "?", ip, MP_VERSION_STR);
    if(m_szOutdatedLog[0])
    {
        FILE* f = fopen(m_szOutdatedLog, "a");
        if(f)
        {
            time_t t = time(nullptr); char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&t));
            fprintf(f, "%s\t%s\tMP %s\tphone %s\t%s\tport %u\n", ts, name, v, uid[0] ? uid : "?", ip, (unsigned)m_nPort);
            fclose(f);
        }
    }
}
