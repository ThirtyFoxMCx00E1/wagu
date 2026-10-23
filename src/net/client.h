#pragma once
#include <stdint.h>
#include <netinet/in.h>
#include <atomic>
#include <mutex>
#include <thread>
#include "protocol.h"

enum eNetState { NET_DISCONNECTED = 0, NET_CONNECTING, NET_CONNECTED };

class INetEvents
{
public:
    virtual void OnConnected(const PktWelcome& w) = 0;
    virtual void OnRejected(uint8_t reason) = 0;
    virtual void OnConnectFailed() = 0;
    virtual void OnLostConnection() = 0;
    virtual void OnPlayerJoin(const PktPlayerJoin& p) = 0;
    virtual void OnPlayerLeave(const PktPlayerLeave& p) = 0;
    virtual void OnSync(const PktSync& p, const SyncExt* ext) = 0;   // ext: 2.13+ sender, else null
    virtual void OnChat(const PktChat& p) = 0;
    virtual void OnDeath(const PktDeath& p) = 0;
    virtual void OnDamage(const PktDamage& p) = 0;
    virtual void OnServerMessage(const PktServerMsg& p) = 0;
    virtual void OnSetColor(const PktSetColor& p) = 0;
    virtual void OnPause(const PktPause& p) = 0;
    virtual void OnWorld(const PktWorld& w) = 0;
    virtual void OnStatus(const PktStatus& s) = 0;   // 2.9: mission / cutscene
    // 2.13 environment
    virtual void OnExplosion(const PktExplosion&) {}
    virtual void OnFire(const PktFire&) {}
    virtual void OnPickupAdd(const PktPickupAdd&) {}
    virtual void OnPickupRemove(const PktPickupRemove&) {}
    virtual void OnPerms(const PktPerms&) {}   // 2.16
    virtual void OnPair(const PktPair&) {}     // 2.16 (mission sync)
    virtual void OnShot(const PktShot&) {}                       // 2.17
    virtual void OnVehState(const PktVehState&) {}               // 2.17
    virtual void OnMissionMarkers(const PktMissionMarkers&) {}   // 2.17
    virtual void OnNpc(const PktNpc&) {}                         // 2.18
    virtual void OnMissionNpc(const PktNpc&) {}                  // 3.02: my mission group's mission peds / cars
    virtual void OnNpcHit(const PktNpcHit&) {}                   // 2.18
    virtual void OnNpcClaim(const PktNpcClaim&) {}               // 2.18
    virtual void OnPlayerInfo(const PktPlayerInfo&) {}           // 2.19
    virtual void OnJack(const PktJack&) {}                       // 2.19
    virtual void OnKicked(const PktKicked&) {}                   // 2.25 (comes right before the PKT_REJECT)
    // 3.01: SA-MP features
    virtual void OnEvent(const PktEvent&) {}
    virtual void OnTeam(const PktTeam&) {}
    virtual void OnTurf(const PktTurf&) {}
    virtual void OnDialog(const PktDialog&) {}
    virtual void OnTextDraw(const PktTextDraw&) {}
    virtual void OnBcw(const uint8_t* /*data*/, int /*len*/) {}   // 3.04: PKT_BCW_STATE / ROLL / SCORE / EVENT
    virtual void OnClothes(const PktClothes&) {}                  // 3.10: CJ clothing sync
};

class MPClient
{
public:
    MPClient();
    ~MPClient();
    // Tell the client the game is paused (pause menu). The heartbeat thread also treats
    // "game thread stopped calling Poll for 1.5 s" (app in background) as paused.
    void SetGamePaused(bool p) { m_bGamePaused = p; }
    bool IsPausedOnServer() const { return m_bSentPaused; }
    // viaLocalPort != 0: packets really go to 127.0.0.1:viaLocalPort (online relay proxy);
    // GetHost()/GetPort() still report host:port so the game logic sees the real server.
    bool Connect(const char* host, uint16_t port, const char* name, uint16_t skin, uint32_t color = 0, uint16_t viaLocalPort = 0);
    void SetJoinPassword(const char* pw) { MP_CopyStr(m_szPassword, pw ? pw : "", sizeof(m_szPassword)); }   // 2.19: before Connect
    void SetUid(const char* uid) { MP_CopyStr(m_szUid, uid ? uid : "", sizeof(m_szUid)); }   // 2.25: this phone's id
    void SetAdminKey(const char* k) { MP_CopyStr(m_szAdminKey, k ? k : "", sizeof(m_szAdminKey)); }   // 2.37: only for the reserved name SENJI
    void Disconnect(bool sendLeave = true);
    void Poll(INetEvents* ev);           // call every frame from the game thread
    void Send(const void* data, int len);

    eNetState GetState() const { return m_eState.load(); }
    uint8_t   GetMyId() const  { return m_nMyId; }
    int       GetPing() const  { return m_nPing; }
    const char* GetHost() const { return m_szHost; }
    uint16_t  GetPort() const  { return m_nPort; }
    const char* GetServerName() const { return m_szServerName; }
    int       GetMaxPlayers() const { return m_nMaxPlayers; }

private:
    void SendJoin();
    void Heartbeat();

    int         m_Sock;
    sockaddr_in m_Server;
    std::atomic<eNetState> m_eState;
    std::mutex  m_SockMutex;
    std::thread m_Heartbeat;
    std::atomic<bool> m_bHbRun{false}, m_bGamePaused{false}, m_bSentPaused{false};
    std::atomic<uint64_t> m_nLastGamePoll{0};
    uint8_t     m_nMyId;
    int         m_nPing;
    float       m_fPingAvg = -1.0f;   // 2.17: smoothed (one slow frame no longer shows as a spike)
    int         m_nMaxPlayers = 0;
    char        m_szHost[64];
    char        m_szName[MP_NAME_LEN];
    char        m_szServerName[32];
    char        m_szPassword[MP_PASSWORD_LEN + 1] = {0};
    char        m_szUid[MP_UID_LEN + 1] = {0};
    char        m_szAdminKey[128 + 1] = {0};   // 2.37
    uint16_t    m_nPort, m_nSkin;
    uint32_t    m_nColor = 0;
    uint64_t    m_nConnectStart, m_nLastJoinSend, m_nLastPing, m_nLastRecv;
};
