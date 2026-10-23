#include "client.h"
#include "server.h" // MP_NowMs
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <chrono>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <linux/sockios.h>

#define CONNECT_TIMEOUT_MS 10000
#define JOIN_RETRY_MS      1000
#define PING_INTERVAL_MS   1000

MPClient::MPClient() : m_Sock(-1), m_eState(NET_DISCONNECTED), m_nMyId(MP_INVALID_ID), m_nPing(0),
    m_nPort(MP_DEFAULT_PORT), m_nSkin(0), m_nConnectStart(0), m_nLastJoinSend(0), m_nLastPing(0), m_nLastRecv(0)
{
    m_szHost[0] = m_szName[0] = m_szServerName[0] = 0;
    memset(&m_Server, 0, sizeof(m_Server));
}

MPClient::~MPClient()
{
    m_bHbRun = false;
    if(m_Heartbeat.joinable()) m_Heartbeat.join();
}

// Runs on its own thread so a paused (or backgrounded) game keeps its seat on the server:
// sends PKT_PAUSE on change and pings every second while the game thread is not running.
void MPClient::Heartbeat()
{
    uint64_t lastPing = 0, lastPause = 0;
    while(m_bHbRun)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        if(m_eState != NET_CONNECTED) { m_bSentPaused = false; continue; }
        uint64_t now = MP_NowMs();
        bool paused = m_bGamePaused || (now - m_nLastGamePoll > 1500);
        if(paused != m_bSentPaused || (paused && now - lastPause > 3000))
        {
            PktPause p; p.type = PKT_PAUSE; p.id = m_nMyId; p.paused = paused ? 1 : 0;
            Send(&p, sizeof(p));
            if(paused != m_bSentPaused) Send(&p, sizeof(p)); // UDP: state changes twice
            m_bSentPaused = paused;
            lastPause = now;
        }
        if(paused && now - lastPing > 1000)
        {
            PktPing pg; pg.type = PKT_PING; pg.time = (uint32_t)now;
            Send(&pg, sizeof(pg));
            lastPing = now;
        }
    }
}

bool MPClient::Connect(const char* host, uint16_t port, const char* name, uint16_t skin, uint32_t color, uint16_t viaLocalPort)
{
    Disconnect(false);
    m_fPingAvg = -1.0f; m_nPing = 0;
    m_bDirect = false; m_bDirectFellBack = false; m_bGotReply = false; m_nFallbackVia = 0;

    if(m_nDirectIp)   // 3.11: straight to the 24/7 server's public address (relay only as fallback)
    {
        memset(&m_Server, 0, sizeof(m_Server));
        m_Server.sin_family = AF_INET;
        m_Server.sin_addr.s_addr = m_nDirectIp;
        m_Server.sin_port = htons(port);
        m_bDirect = true;
        m_nFallbackVia = m_nDirectVia ? m_nDirectVia : viaLocalPort;
        m_nDirectIp = 0; m_nDirectVia = 0;
    }
    else if(viaLocalPort)
    {
        memset(&m_Server, 0, sizeof(m_Server));
        m_Server.sin_family = AF_INET;
        m_Server.sin_addr.s_addr = htonl(0x7F000001u);
        m_Server.sin_port = htons(viaLocalPort);
    }
    else
    {
        addrinfo hints, *res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if(getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
        m_Server = *(sockaddr_in*)res->ai_addr;
        m_Server.sin_port = htons(port);
        freeaddrinfo(res);
    }

    m_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(m_Sock < 0) return false;
    fcntl(m_Sock, F_SETFL, fcntl(m_Sock, F_GETFL, 0) | O_NONBLOCK);
    { int sb = MP_SOCK_BUF; setsockopt(m_Sock, SOL_SOCKET, SO_RCVBUF, &sb, sizeof(sb)); }   // 99 others' syncs

    MP_CopyStr(m_szHost, host, sizeof(m_szHost));
    MP_CopyStr(m_szName, name, sizeof(m_szName));
    m_nPort = port; m_nSkin = skin; m_nColor = color;
    m_eState = NET_CONNECTING;
    m_nConnectStart = MP_NowMs();
    m_nLastJoinSend = 0;
    m_nMyId = MP_INVALID_ID;
    m_nLastGamePoll = MP_NowMs();
    m_bSentPaused = false;
    if(!m_bHbRun) { m_bHbRun = true; m_Heartbeat = std::thread(&MPClient::Heartbeat, this); }
    SendJoin();
    return true;
}

void MPClient::SendJoin()
{
    PktJoin5 j5; memset(&j5, 0, sizeof(j5));   // 3.04.6: + build (servers refuse old MP versions)
    PktJoin4& j4 = j5.j4;                       // 2.37: + admin key (older servers read only the first part)
    PktJoin3& j3 = j4.j3;                       // 2.25: + phone id
    PktJoin2& j2 = j3.j2;
    PktJoin& j = j2.j;
    j.type = PKT_JOIN; j.proto = MP_PROTO_VERSION; j.skin = m_nSkin; j.color = m_nColor;
    MP_CopyStr(j.name, m_szName, MP_NAME_LEN);
    MP_CopyStr(j2.password, m_szPassword, sizeof(j2.password));   // 2.19
    MP_CopyStr(j3.uid, m_szUid, sizeof(j3.uid));
    MP_CopyStr(j4.adminKey, m_szAdminKey, sizeof(j4.adminKey));
    j5.build = MP_BUILD;
    Send(&j5, sizeof(j5));
    m_nLastJoinSend = MP_NowMs();
}

void MPClient::Disconnect(bool sendLeave)
{
    if(m_Sock >= 0)
    {
        if(sendLeave && m_eState == NET_CONNECTED)
        {
            PktLeave l; l.type = PKT_LEAVE;
            Send(&l, sizeof(l)); Send(&l, sizeof(l)); // UDP: send twice
        }
        std::lock_guard<std::mutex> lk(m_SockMutex);
        close(m_Sock);
        m_Sock = -1;
    }
    m_Sock = -1;
    m_eState = NET_DISCONNECTED;
    m_nMyId = MP_INVALID_ID;
}

void MPClient::Send(const void* data, int len)
{
    std::lock_guard<std::mutex> lk(m_SockMutex);
    if(m_Sock < 0) return;
    sendto(m_Sock, data, len, 0, (sockaddr*)&m_Server, sizeof(m_Server));
}

void MPClient::Poll(INetEvents* ev)
{
    if(m_Sock < 0) return;
    uint64_t now = MP_NowMs();
    m_nLastGamePoll = now;

    alignas(8) uint8_t buf[1024];
    for(int guard = 0; guard < 2048; ++guard)   // 2.11: a full server sends ~2000 packets/s
    {
        sockaddr_in from; socklen_t fl = sizeof(from);
        int r = (int)recvfrom(m_Sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if(r <= 0) break;
        if(from.sin_addr.s_addr != m_Server.sin_addr.s_addr || from.sin_port != m_Server.sin_port) continue;
        m_nLastRecv = now;
        m_bGotReply = true;

        switch(buf[0])
        {
            case PKT_WELCOME:
                if(r >= (int)sizeof(PktWelcome) && m_eState == NET_CONNECTING)
                {
                    // 3.04.6: a server without a build number (or an older one) runs an old MP that lets
                    // old phones in with cheats: refuse it
                    uint32_t sb = 0;
                    if(r >= (int)sizeof(PktWelcome2)) memcpy(&sb, buf + offsetof(PktWelcome2, build), sizeof(sb));
                    if(sb < MP_MIN_BUILD) { Disconnect(true); ev->OnRejected(REJECT_OLD_SERVER); return; }
                    const PktWelcome* w = (const PktWelcome*)buf;
                    m_nMyId = w->yourId;
                    MP_CopyStr(m_szServerName, w->serverName, sizeof(m_szServerName));
                    m_nMaxPlayers = w->maxPlayers;
                    m_eState = NET_CONNECTED;
                    m_nLastPing = 0;
                    ev->OnConnected(*w);
                }
                break;
            case PKT_REJECT:
                if(r >= (int)sizeof(PktReject)) { uint8_t rs = ((PktReject*)buf)->reason; Disconnect(false); ev->OnRejected(rs); return; }
                break;
            case PKT_KICKED:       if(r >= (int)sizeof(PktKicked)) { PktKicked k; memcpy(&k, buf, sizeof(k)); k.admin[MP_NAME_LEN - 1] = 0; k.reason[MP_REASON_LEN - 1] = 0; ev->OnKicked(k); } break;   // 2.25
            case PKT_PLAYER_JOIN:  if(r >= (int)sizeof(PktPlayerJoin))  ev->OnPlayerJoin(*(PktPlayerJoin*)buf);   break;
            // 3.01: SA-MP features (copied out: packed + unaligned)
            case PKT_EVENT:    if(r >= (int)sizeof(PktEvent)) { PktEvent e; memcpy(&e, buf, sizeof(e)); ev->OnEvent(e); } break;
            case PKT_TEAM:     if(r >= (int)sizeof(PktTeam))  { PktTeam t; memcpy(&t, buf, sizeof(t)); ev->OnTeam(t); } break;
            case PKT_TURF:     if(r >= (int)sizeof(PktTurf))  { PktTurf t; memcpy(&t, buf, sizeof(t)); if(t.count > MP_TURF_MAX) t.count = MP_TURF_MAX; ev->OnTurf(t); } break;
            case PKT_DIALOG:   if(r >= (int)sizeof(PktDialog)) { static PktDialog d; memcpy(&d, buf, sizeof(d)); d.caption[63] = d.button1[15] = d.button2[15] = d.info[MP_DIALOG_INFO_LEN - 1] = 0; ev->OnDialog(d); } break;
            case PKT_TEXTDRAW: if(r >= (int)sizeof(PktTextDraw)) { PktTextDraw t; memcpy(&t, buf, sizeof(t)); t.text[255] = 0; ev->OnTextDraw(t); } break;
            case PKT_PLAYER_LEAVE: if(r >= (int)sizeof(PktPlayerLeave)) ev->OnPlayerLeave(*(PktPlayerLeave*)buf); break;
            case PKT_SYNC:
                if(r >= (int)sizeof(PktSync))
                {
                    // 2.13: copy out of the receive buffer (packed, unaligned) before use
                    PktSync ps; memcpy(&ps, buf, sizeof(ps));
                    SyncExt ext; bool hasExt = false;
                    if(r > (int)sizeof(PktSync))   // 2.13 raw (v1) or 2.14 compact (v2)
                        hasExt = MP_DecodeSyncExt(buf + sizeof(PktSync), r - (int)sizeof(PktSync), ext);
                    ev->OnSync(ps, hasExt ? &ext : nullptr);
                }
                break;
            case PKT_EXPLOSION:     if(r >= (int)sizeof(PktExplosion))    { PktExplosion e; memcpy(&e, buf, sizeof(e)); ev->OnExplosion(e); } break;
            case PKT_FIRE:          if(r >= (int)sizeof(PktFire))         { PktFire f; memcpy(&f, buf, sizeof(f)); ev->OnFire(f); } break;
            case PKT_PICKUP_ADD:    if(r >= (int)sizeof(PktPickupAdd))    { PktPickupAdd a; memcpy(&a, buf, sizeof(a)); ev->OnPickupAdd(a); } break;
            case PKT_PERMS:         if(r >= 2)                            { PktPerms m; memset(&m, 0, sizeof(m)); memcpy(&m, buf, r < (int)sizeof(m) ? (size_t)r : sizeof(m)); ev->OnPerms(m); } break;   // 3.02: 2 bytes (old) or 6
            case PKT_PAIR:          if(r >= (int)sizeof(PktPair))         { PktPair m; memcpy(&m, buf, sizeof(m)); ev->OnPair(m); } break;
            case PKT_SHOT:
                if(r >= MP_PKT_SHOT_HDR + (int)sizeof(ShotEntry))
                {
                    PktShot m; memset(&m, 0, sizeof(m));
                    memcpy(&m, buf, r < (int)sizeof(m) ? (size_t)r : sizeof(m));
                    int fit = (r - MP_PKT_SHOT_HDR) / (int)sizeof(ShotEntry);
                    if(m.count > fit) m.count = (uint8_t)fit;
                    if(m.count > MP_SHOT_MAX) m.count = MP_SHOT_MAX;
                    ev->OnShot(m);
                }
                break;
            case PKT_VEH_STATE:     if(r >= (int)sizeof(PktVehState))     { PktVehState m; memcpy(&m, buf, sizeof(m)); ev->OnVehState(m); } break;
            case PKT_MISSION_MARKERS:
                if(r >= MP_PKT_MM_HDR)
                {
                    PktMissionMarkers m; memset(&m, 0, sizeof(m));
                    memcpy(&m, buf, r < (int)sizeof(m) ? (size_t)r : sizeof(m));
                    int fit = (r - MP_PKT_MM_HDR) / (int)sizeof(MissionMarker);
                    if(m.count > fit) m.count = (uint8_t)fit;
                    if(m.count > MP_MISSION_MARKERS_MAX) m.count = MP_MISSION_MARKERS_MAX;
                    ev->OnMissionMarkers(m);
                }
                break;
            case PKT_NPC:           { static PktNpc m; if(MP_DecodeNpc(buf, r, m)) ev->OnNpc(m); } break;   // 2.18
            case PKT_BCW_STATE: case PKT_BCW_ROLL: case PKT_BCW_SCORE: case PKT_BCW_EVENT: ev->OnBcw(buf, r); break;   // 3.04
            case PKT_MISSION_NPC:   { static PktNpc m; if(MP_DecodeNpc(buf, r, m)) ev->OnMissionNpc(m); } break;   // 3.02
            case PKT_NPC_HIT:       if(r >= (int)sizeof(PktNpcHit))       { PktNpcHit m; memcpy(&m, buf, sizeof(m)); ev->OnNpcHit(m); } break;
            case PKT_NPC_CLAIM:     if(r >= (int)sizeof(PktNpcClaim))     { PktNpcClaim m; memcpy(&m, buf, sizeof(m)); ev->OnNpcClaim(m); } break;
            case PKT_PLAYER_INFO:   if(r >= (int)sizeof(PktPlayerInfo))   { PktPlayerInfo m; memcpy(&m, buf, sizeof(m)); ev->OnPlayerInfo(m); } break;   // 2.19
            case PKT_JACK:          if(r >= (int)sizeof(PktJack))         { PktJack m; memcpy(&m, buf, sizeof(m)); ev->OnJack(m); } break;
            case PKT_PICKUP_REMOVE: if(r >= (int)sizeof(PktPickupRemove)) { PktPickupRemove m; memcpy(&m, buf, sizeof(m)); ev->OnPickupRemove(m); } break;
            case PKT_CHAT:         if(r >= (int)sizeof(PktChat))        { ((PktChat*)buf)->text[MP_CHAT_LEN-1] = 0; ev->OnChat(*(PktChat*)buf); } break;
            case PKT_DEATH:        if(r >= (int)sizeof(PktDeath))       ev->OnDeath(*(PktDeath*)buf);             break;
            case PKT_DAMAGE:       if(r >= (int)sizeof(PktDamage))      ev->OnDamage(*(PktDamage*)buf);           break;
            case PKT_SERVER_MSG:   if(r >= (int)sizeof(PktServerMsg))   { ((PktServerMsg*)buf)->text[MP_CHAT_LEN-1] = 0; ev->OnServerMessage(*(PktServerMsg*)buf); } break;
            case PKT_PAUSE:        if(r >= (int)sizeof(PktPause))       ev->OnPause(*(PktPause*)buf);             break;
            case PKT_STATUS:       if(r >= (int)sizeof(PktStatus))      { ((PktStatus*)buf)->mission[7] = 0; ev->OnStatus(*(PktStatus*)buf); } break;
            case PKT_WORLD:
                if(r >= MP_PKT_WORLD_V1_SIZE)
                {
                    PktWorld w; memset(&w, 0, sizeof(w));
                    memcpy(&w, buf, r < (int)sizeof(w) ? (size_t)r : sizeof(w));
                    if(r < (int)sizeof(w)) { w.mode = WORLD_FORCED; w.absMinute = (uint32_t)(w.hour * 60 + w.minute); }   // old server
                    ev->OnWorld(w);
                }
                break;
            case PKT_SET_COLOR:    if(r >= (int)sizeof(PktSetColor))    ev->OnSetColor(*(PktSetColor*)buf);       break;
            case PKT_VOICE:        if(r >= (int)sizeof(PktVoiceHdr)) { PktVoiceHdr h; memcpy(&h, buf, sizeof(h)); if(h.len && (int)sizeof(h) + h.len <= r) ev->OnVoice(h.id, h.seq, h.flags, buf + sizeof(h), h.len); } break;   // 3.11
            case PKT_CLOTHES:      if(r >= (int)sizeof(PktClothes))     { PktClothes c; memcpy(&c, buf, sizeof(c)); ev->OnClothes(c); } break;   // 3.10
            case PKT_PONG:
                if(r >= (int)sizeof(PktPing))
                {
                    // 2.17: the pong is read once per game frame, so a busy frame (more avatars on
                    // screen = more work) used to be counted as network ping. Use the time the
                    // kernel received it instead, and smooth the value.
                    uint32_t sent; memcpy(&sent, buf + 1, 4);
                    uint32_t arrived = (uint32_t)now;
                    struct timeval tv;
                    if(ioctl(m_Sock, SIOCGSTAMP, &tv) == 0)
                    {
                        struct timeval nowTv; gettimeofday(&nowTv, nullptr);
                        int64_t lateMs = ((int64_t)nowTv.tv_sec - tv.tv_sec) * 1000 + ((int64_t)nowTv.tv_usec - tv.tv_usec) / 1000;
                        if(lateMs > 0 && lateMs < 2000) arrived -= (uint32_t)lateMs;
                    }
                    int rtt = (int)(arrived - sent);
                    if(rtt < 0) rtt = 0;
                    if(rtt < 60000)
                    {
                        if(m_fPingAvg < 0 || rtt < m_fPingAvg) m_fPingAvg = m_fPingAvg < 0 ? (float)rtt : m_fPingAvg * 0.5f + rtt * 0.5f;
                        else m_fPingAvg = m_fPingAvg * 0.75f + rtt * 0.25f;
                        m_nPing = (int)(m_fPingAvg + 0.5f);
                    }
                }
                break;
        }
        if(m_Sock < 0) return;
    }

    if(m_eState == NET_CONNECTING)
    {
        if(now - m_nConnectStart > CONNECT_TIMEOUT_MS) { Disconnect(false); ev->OnConnectFailed(); return; }
        // 3.11: direct UDP got no answer (UDP blocked on this network?) -> use the relay from now on
        if(m_bDirect && !m_bGotReply && m_nFallbackVia && now - m_nConnectStart > 2500)
        {
            std::lock_guard<std::mutex> lk(m_SockMutex);
            memset(&m_Server, 0, sizeof(m_Server));
            m_Server.sin_family = AF_INET;
            m_Server.sin_addr.s_addr = htonl(0x7F000001u);
            m_Server.sin_port = htons(m_nFallbackVia);
            m_bDirect = false; m_bDirectFellBack = true;
            m_nLastJoinSend = 0;
        }
        if(now - m_nLastJoinSend > JOIN_RETRY_MS) SendJoin();
    }
    else if(m_eState == NET_CONNECTED)
    {
        if(now - m_nLastPing > PING_INTERVAL_MS)
        {
            PktPing p; p.type = PKT_PING; p.time = (uint32_t)now;
            Send(&p, sizeof(p));
            m_nLastPing = now;
        }
        if(now - m_nLastRecv > MP_TIMEOUT_MS) { Disconnect(false); ev->OnLostConnection(); }
    }
}
