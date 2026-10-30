// MP server core. Used by the in-game "Host LAN server" toggle and by the standalone server.
#pragma once
#include <stdint.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include "protocol.h"
#include "turf.h"
#include "bcw.h"
#include "mw3.h"   // 3.23
#include "rp.h"    // 3.25: Mega Vil Futuristic
#include <string>
#include <map>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  typedef SOCKET mp_socket_t;
#else
  #include <netinet/in.h>
  typedef int mp_socket_t;
#endif

typedef void (*MPServerLogFn)(const char* msg);

class MPServer
{
public:
    MPServer();
    ~MPServer();

    bool Start(uint16_t port, const char* serverName, MPServerLogFn log = nullptr, int maxPlayers = MP_STD_MAX_PLAYERS, int region = REGION_ASIA);
    void Stop();
    bool IsRunning() const { return m_bRunning; }
    int  GetPlayerCount();
    uint16_t GetPort() const { return m_nPort; }
    const char* GetName() const { return m_szName; }

    // Persistent servers do not shut down when the original host/owner leaves.
    // The server process must remain running (phone app, PC, or VPS).
    void SetPersistent(bool b) { m_bPersistent = b; }
    bool IsPersistent() const { return m_bPersistent; }

    // Player limit 1..100. Lowering it never kicks anyone; it only blocks new joins.
    void SetMaxPlayers(int n) { m_nMaxPlayers = n < 1 ? 1 : (n > MP_STD_MAX_PLAYERS ? MP_STD_MAX_PLAYERS : n); if(IsBcwServer()) m_nMaxPlayers = MP_BCW_MAX_PLAYERS; else if(IsMw3Server()) m_nMaxPlayers = MP_MW3_MAX_PLAYERS; else if(IsRpServer()) m_nMaxPlayers = MP_RP_MAX_PLAYERS; else if(IsDefaultServer()) m_nMaxPlayers = MP_STD_MAX_PLAYERS; }   // 3.04: Brief Case War = 20; 3.10: the default server is always 100; 3.25: Mega Vil Futuristic = 250
    int  GetMaxPlayers() const { return m_nMaxPlayers; }
    void SetRegion(int r) { m_nRegion = (r >= 0 && r < REGION_COUNT) ? r : REGION_ASIA; }
    int  GetRegion() const { return m_nRegion; }
    // 2.24: server icon shown in everyone's server list (0 = default main_df_icon.png, 1..250 = icon_N.png)
    void SetIcon(int i) { m_nIcon = (i >= 0 && i <= MP_ICON_MAX) ? i : 0; }
    int  GetIcon() const { return m_nIcon; }
    uint32_t GetStartUnix() const { return m_nStartUnix; }   // 2.27
    // 2.25: server language shown in the server list ("English", "Filipino / English"...)
    void SetLang(const char* l);
    void GetLang(char* out, size_t n);
    // 2.16: permissions (PERM_*). Turning mission pairs off ends every pair.
    void SetPerms(uint8_t perms);
    void SetAdminHash(const char* sha256Hex);   // 2.37: mp_server --admin-hash (default: MP_ADMIN_KEY_HASH)
    bool IsDefaultServer() const;               // 2.37: the name is MP_DEFAULT_SERVER_NAME
    void SetSafeZones(bool on) { m_bSafeZones = on; }   // 3.21: hospital safe zones (default on, --no-safezones)
    bool SafeZoneBlocks(int from, int to) const;       // 3.21: damage from -> to dropped (zone / spawn protection)
    bool IsBcwServer() const;                   // 3.04: the name is MP_BCW_SERVER_NAME (Brief Case War)
    bool IsMw3Server() const;                   // 3.23: Call Of Duty: Modern Warfare 3
    void SetMw3File(const char* path);          // 3.23: MW3 stats + zone owners (default mw3_stats.txt)
    bool IsRpServer() const;                    // 3.25: Mega Vil Futuristic (Roleplay)
    void SetRpFile(const char* path);           // 3.25: citizens (money, bank, job, house...) + house owners (default rp_citizens.txt)
    // 3.21.6: login system (Discord verification + account). Always on for the default server and
    // Brief Case War; custom servers: SetAuth(true) (Add server -> Authentication system, --auth)
    void SetAuth(bool on) { m_bAuthCustom = on; }
    // 3.33: NO bot. Until 3.32 the 4 featured servers (default 100, Brief Case War 20, MW3 64, Mega Vil 250) always
    // had a fake player "Broker" (id MP_BOT_ID) in them, so an empty server showed 1/100. Now they show the real
    // count (0/100 with nobody on) and every slot belongs to a real player. Kept as a function (always false) so
    // the places that used to account for the bot's slot stay valid and nothing else has to change.
    bool HasBot() const { return false; }
    bool AuthOn() const { return IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer() || m_bAuthCustom; }
    bool AccountOf(int id, char* out, size_t n);   // the logged in account of player id (false = none)
    // 3.04.6: refuse + track old MP versions
    void SetMinBuild(uint32_t b) { m_nMinBuild = b; }
    void SetOutdatedLog(const char* path) { snprintf(m_szOutdatedLog, sizeof(m_szOutdatedLog), "%s", path ? path : ""); }
    uint32_t OutdatedRefused() const { return m_nOutdatedCount.load(); }
    void SetBcwMinPlayers(int n);               // 3.04: players in the queue that start a match (default 20)
    uint8_t GetPerms() const { return m_nPerms; }
    void     SetModes(uint16_t modes);   // 3.02: GM_* (the default server always has all of them)
    uint16_t GetModes() const;
    // 2.19: join password (max 16 chars, empty = open server)
    void SetPassword(const char* pw);
    // 3.01: SA-MP features. Welcome dialog (shown once on join, {RRGGBB} colours + \n lines),
    // banner textdraw (bottom right, like SA-MP server sites), turf owners file (kept across restarts).
    void SetMotd(const char* caption, const char* text);
    void SetBanner(const char* text);
    void SetTurfFile(const char* path);
    void SetTurfWars(bool on) { m_bTurfWars = on; }
    bool HasPassword() const { return m_szPassword[0] != 0; }

    // 2.25 player management. The administrator is the one who hosts the server.
    struct PlayerView { int id; char name[MP_NAME_LEN]; char uid[MP_UID_LEN + 1]; uint32_t color; uint64_t joinedMs; };
    struct BanEntry   { char uid[MP_UID_LEN + 1]; char name[MP_NAME_LEN]; uint32_t when; char admin[MP_NAME_LEN]; char reason[MP_REASON_LEN]; };
    void GetPlayers(std::vector<PlayerView>& out);
    bool Kick(int id, const char* admin, const char* reason, bool ban);   // ban = also add to the ban list
    void SetBanFile(const char* path);   // load now, saved after every change ("uid|name|unix|admin|reason")
    void GetBans(std::vector<BanEntry>& out);
    bool Unban(int index);
    bool AddBan(const char* name, const char* uid, const char* admin, const char* reason);   // offline ban by name

    // Server world: the server owns the time + weather. 1 game minute = 1 real second, like GTA SA,
    // from the real clock, so every server shows the same normal day. Sent on join and every 3 s.
    static uint32_t WorldMinute();     // server day clock (game minutes since epoch)

private:
    struct Slot
    {
        bool         used;
        sockaddr_in  addr;
        char         name[MP_NAME_LEN];
        uint32_t     color;
        uint16_t     skin;
        uint64_t     lastSeen;
        bool         paused;      // paused players stay connected and can't be damaged
        uint8_t      status;      // STATUS_* (mission / cutscene): own story world, can't be damaged
        char         mission[8];
        // 2.11 interest management (100 players): last known position + when each other player
        // last got a sync from this one. Far players get 1 update/s instead of 20.
        float        pos[3];
        bool         hasPos;
        uint8_t      lastFlags;
        uint64_t     spawnProtUntil;   // 3.21: 5 s spawn protection after every respawn (and the join)
        uint64_t     lastFwd[MP_MAX_PLAYERS];
        uint8_t      pair;        // 2.31: mission-sync GROUP id (MP_INVALID_ID = none; was the partner id)
        uint8_t      pairReq;     // 2.16: who this player asked for Mission sync
        char         uid[MP_UID_LEN + 1];   // 2.25 (empty for phones older than 2.25)
        uint64_t     joinedMs;
        bool         admin;       // 2.37: SENJI with the right admin key ("[ADMIN] SENJI" on the default server)
        bool         muted;       // 2.37: muted by the admin
        uint8_t      team;        // 3.01: class selection team (TEAM_*), turf wars
        int16_t      classSkin;   // 3.01
        uint64_t     lastEvent;   // 3.01: PKT_EVENT rate limit
        uint64_t     raceQueuedAt;   // 3.01: in the race queue since (0 = not queued)
        bool         hasClothes;     // 3.10: CJ clothing sync (last PKT_CLOTHES, sent to new joiners)
        PktClothes   clothes;
        uint64_t     lastClothes;
        uint32_t     build;          // 3.11: the phone's MP build (0 = older than 3.04.6)
        uint64_t     voiceWin;       // 3.11: voice rate limit
        int          voiceCount;
        bool         authed;         // 3.21.6: login done (always true on servers without the login system)
        char         account[24];    // 3.21.6: the account (only the server knows it, never sent to others)
        char         chal[33];       // 3.21.6: this connection's random challenge (hex)
        uint64_t     lastAuthTry;
        uint8_t      authFails;
    };
    void AuthBegin(int slot);                    // 3.21.6, m_Mutex held
    void AuthHandle(int slot, const uint8_t* data, int len);
    void AnnounceJoin(int slot);                 // 3.21.6: PKT_PLAYER_JOIN to everybody else
    std::atomic<bool> m_bAuthCustom{false};
    void SetPermsLocked(uint8_t perms);                                         // 2.37, m_Mutex held
    bool KickLocked(int id, const char* admin, const char* reason, bool ban);   // 2.37, m_Mutex held
    char               m_szAdminHash[65] = {0};
    uint8_t            m_nBasePerms = PERM_MISSION_PAIR;   // 2.37: what the owner set; the admin's changes reset when everybody left
    int  FindBan(const char* name, const char* uid);   // m_Mutex held
    void SaveBans();                                   // m_Mutex held
    void SendKicked(const sockaddr_in& a, const BanEntry& b, bool banned);
    std::vector<BanEntry> m_Bans;
    char               m_szBanFile[512] = {0};
    void EndPair(int slot);                  // 2.31: slot leaves its mission group (everyone told)
    int  GroupSize(uint8_t g) const;
    void SendGroupLinks(int slot);           // PAIR_LINKED for every member, both ways
    uint8_t      m_nNextGroup = 0;

    void Loop();
    void Handle(const sockaddr_in& from, const uint8_t* data, int len);
    int  FindSlot(const sockaddr_in& a);
    void SendTo(int slot, const void* data, int len);
    void SendRaw(const sockaddr_in& a, const void* data, int len);
    void Broadcast(const void* data, int len, int except = -1);
    void RelaySync(int slot, const void* pkt, int len, bool important);
    void RelayNearOrPartner(int slot, const float pos[3], const void* pkt, int len);   // 2.17
    void RelayNear(int slot, const float pos[3], const void* pkt, int len, float range = MP_ENV_RANGE_M);   // 2.13 env events (2.18: range)
    void SendPickups(int slot);
    void ExpirePickups(uint64_t now);
    // 3.01: turf wars (m_Mutex held)
    void TurfTick(uint64_t now);
    void SendTurf(int slot);   // -1 = everybody
    void RaceQueueTick(uint64_t now, bool changed);   // 3.01: 14 players -> a GTA SA race starts
    int  m_nLastRace = -1;
    void LoadTurf();
    void SaveTurf();
    void SendJoinExtras(int slot, bool fresh);
    uint8_t m_TurfOwner[MP_TURF_MAX] = {0}, m_TurfAtk[MP_TURF_MAX] = {0};
    float   m_TurfProg[MP_TURF_MAX] = {0};
    bool    m_bTurfWars = true;
    bool    m_bSafeZones = true;   // 3.21
    uint64_t m_nLastTurfSend = 0;
    uint64_t m_nNextAd = 0;          // 3.11: next Discord message
    uint64_t m_nAdInterval = MP_AD_INTERVAL_MS;
public:
    void     SetAdInterval(uint64_t ms) { m_nAdInterval = ms ? ms : MP_AD_INTERVAL_MS; }   // tests
private:
    void     AdTick(uint64_t now);
    char    m_szMotdCaption[64] = {0};
    char    m_szMotd[MP_DIALOG_INFO_LEN] = {0};
    char    m_szBanner[128] = {0};
    char    m_szTurfFile[256] = {0};
    struct NetPickup { bool used; uint16_t uid; uint8_t weapon; uint16_t ammo; float pos[3]; uint64_t born; };
    NetPickup m_Pickups[MP_MAX_PICKUPS];
    uint16_t  m_nNextPickup = 1;
    void Drop(int slot, uint8_t reason);
    // ---- 3.04: Brief Case War (server_bcw.inc) ----
    struct BcwSrvPlayer { uint8_t team; uint8_t vote; uint16_t kills, deaths, steals, caps, returns; float damage; uint64_t spawnedAt; };
    struct BcwSrvCase   { uint8_t state; uint8_t carrier; uint8_t stealer; uint64_t stealStart; uint64_t droppedAt; float pos[3]; };
    BcwSrvPlayer m_Bcw[MP_MAX_PLAYERS];
    BcwSrvCase   m_BcwCase[2];
    uint8_t      m_nBcwState = BCW_QUEUE, m_nBcwMap = 0;
    uint8_t      m_nBcwLeader[2] = { MP_INVALID_ID, MP_INVALID_ID };
    int          m_nBcwWinner = 0, m_nBcwMin = BCW_MIN_PLAYERS;
    uint64_t     m_nBcwStateAt = 0, m_nBcwMatchStart = 0, m_nBcwMatchEnd = 0, m_nBcwLastSend = 0;
    std::vector<std::string> m_BcwPastLeaders;
    void BcwReset();
    int  BcwCount(int team) const;
    void BcwEvent(uint8_t ev, uint8_t team, uint8_t who);
    void BcwSendState(int slot);
    void BcwSetState(uint8_t st);
    void BcwPickLeaders();
    void BcwStartMatch();
    void BcwSendScores();
    void BcwVictory(int team);
    void BcwDrop(int c, const float* at);
    void BcwTick(uint64_t now);
    void BcwHandleCmd(int slot, const PktBcwCmd& c);
    void BcwOnDeath(int victim, int killer);
    bool BcwOnDamage(int from, int target, float amount);
    void BcwOnLeave(int slot);
    void BcwOnJoin(int slot, bool fresh);
    // ---- 3.23: MW3 (server_mw3.inc) ----
    struct Mw3SrvPlayer
    {
        uint8_t  team, cls, arena, sub, rank;
        bool     spawned, loaded, zombie;
        int32_t  score, xp, cash;
        uint16_t kills, deaths, caps, deagle, spree, best;
        uint16_t hKills, hCaps, hDeagle, hSpree;   // this hour's tournament
        uint8_t  multi; uint64_t lastKill;
        uint64_t spawnedAt, lastHeal;
        uint8_t  duelAsk, duelAskW, duelWith; uint64_t duelAskAt;
        uint8_t  arenaSpot;
        uint32_t statT, cardT;              // 3.24: when the stats last changed / the last card we sent
        uint64_t cardWaitAt;
        int32_t  lastSc, lastXp, lastCash; uint16_t lastK, lastD, lastC;
        uint16_t evKills;
        bool     evIn;
    };
    struct Mw3SrvZone { uint8_t owner, atk, capper; uint64_t capStart; };
    struct Mw3Saved   { int32_t score, xp, cash; uint16_t kills, deaths, caps, best; uint8_t team, cls; uint32_t t = 0; };   // 3.24: + t
    Mw3SrvPlayer m_Mw3[MP_MAX_PLAYERS];
    Mw3SrvZone   m_Mw3Zone[MW3_ZONES_WIRE];
    std::map<std::string, Mw3Saved> m_Mw3Saved;
    char         m_szMw3File[256] = "mw3_stats.txt";
    uint64_t     m_nMw3LastSend = 0, m_nMw3LastPlayers = 0, m_nMw3LastSave = 0, m_nMw3TourneyStart = 0, m_nMw3NextEvent = 0, m_nMw3EvAt = 0, m_nMw3ZonePay = 0;
    uint8_t      m_nMw3TKind = 0, m_nMw3EvKind = 0, m_nMw3EvState = 0, m_nMw3EvNextKind = 1;
    bool         m_bMw3Dirty = false;
    void Mw3Reset();
    void Mw3Load();
    void Mw3Save();
    const char* Mw3Key(int slot, char* buf, size_t n);
    void Mw3Store(int slot);
    void Mw3SendState(int slot);
    void Mw3SendPlayers(int slot);
    void Mw3SendMe(int slot);
    void Mw3SendCard(int slot);
    void Mw3OnCard(int slot, const PktMw3Card& c);
    void Mw3Ev(int slot, uint8_t ev, uint8_t a, uint8_t b, uint8_t c, uint8_t who, int32_t val, const char* text = nullptr);
    void Mw3Msg(int slot, uint32_t rgba, const char* fmt, ...);
    void Mw3Reward(int slot, int score, int cash, int xp);
    void Mw3Tick(uint64_t now);
    void Mw3ZoneTick(uint64_t now);
    void Mw3TourneyTick(uint64_t now);
    void Mw3EventTick(uint64_t now);
    void Mw3EventStart(uint8_t kind);
    void Mw3EventEnd(int winnerSide, int winner);
    void Mw3EventLeave(int slot, bool msg);
    int  Mw3TeamCount(int team) const;
    uint16_t Mw3TourneyVal(int slot) const;
    void Mw3HandleCmd(int slot, const PktMw3Cmd& c);
    void Mw3OnDeath(int victim, int killer, int weapon);
    bool Mw3OnDamage(int from, int target, float amount);
    void Mw3OnLeave(int slot);
    void Mw3OnJoin(int slot, bool fresh);
    void Mw3OnAuthed(int slot);
    void Mw3Spawned(int slot);
    int  Mw3EvAlive(int side) const;
    uint16_t m_nMw3EvSideKills[2] = { 0, 0 };
    // ---- 3.25: Mega Vil Futuristic (server_rp.inc) ----
    struct RpSrvPlayer
    {
        bool     loaded, spawned, duty, cuffed;
        uint8_t  job, level, hunger, thirst, house, pet, wanted, nveh;
        int32_t  cash, bank, xp, number;
        uint32_t playMin, pets, items, statT, cardT;
        uint16_t veh[RP_VEH_SLOTS], skin, tasks, lotto;
        uint16_t jailS;
        uint64_t minuteAt, needAt, paydayAt, cardWaitAt, lastChat, lastBuy, lastVeh, lastSms, lastEmoteAsk, lastFest;
        // job task
        bool     jobOn; float target[3]; uint64_t taskAt; float taskDist;
        // phone call
        uint8_t  callWith, ringFrom; uint64_t ringAt;
        // shared emote / any request
        uint8_t  askFrom; uint8_t askEmote; uint64_t askAt;
        int32_t  fineAsk; uint8_t fineFrom;
        int32_t  lastCash, lastBank; uint8_t lastJob, lastHunger, lastThirst; uint16_t lastJail;
        RpInvSlot inv[RP_INV_SLOTS]; uint64_t lastInv;   // 3.26: the bag
        // 3.29: investments (units: worth = units * price / 1000), marriage, seatbelt
        int64_t  invU[RPN_COUNT]; uint64_t lastInvest;
        bool     belt; uint8_t proposeFrom; uint64_t proposeAt;   // proposeFrom = slot + 1
        char     spouse[24], spouseKey[40];
    };
    struct RpSaved
    {
        int32_t cash = 0, bank = 0, xp = 0, number = 0; uint8_t job = 0, house = 255, pet = 255, hunger = 100, thirst = 100, nveh = 0;
        uint32_t playMin = 0, pets = 0, items = 0, t = 0; uint16_t veh[RP_VEH_SLOTS] = {}, skin = 0, tasks = 0;
        RpInvSlot inv[RP_INV_SLOTS] = {};   // 3.26
        int64_t invU[RPN_COUNT] = {}; std::string spouse, spouseKey;   // 3.29
    };
    RpSrvPlayer  m_Rp[MP_MAX_PLAYERS];
    std::map<std::string, RpSaved> m_RpSaved;
    std::string  m_RpHouseOwner[RP_HOUSES_WIRE];
    char         m_szRpFile[256] = "rp_citizens.txt";
    uint64_t     m_nRpLastSend = 0, m_nRpLastPlayers = 0, m_nRpLastSave = 0, m_nRpNextEvent = 0, m_nRpEvEnd = 0;
    uint8_t      m_nRpEvKind = 0, m_nRpEvState = 0, m_nRpEvNext = 1;
    float        m_fRpEvPos[3] = { 0, 0, 0 };
    int32_t      m_nRpLottoPot = 25000;
    float        m_fRpMarket[RPN_COUNT] = { 1000.0f, 1000.0f, 1000.0f }; int16_t m_nRpMarketChg[RPN_COUNT] = {}; uint64_t m_nRpMarketAt = 0;   // 3.29
    bool         m_bRpDirty = false;
    void RpReset();
    void RpLoad();
    void RpSave();
    const char* RpKey(int slot, char* buf, size_t n);
    void RpStore(int slot);
    void RpSendMe(int slot);
    void RpSendInv(int slot);                                        // 3.26: the bag
    void RpSendDrops(int slot);                                      // 3.30: items on the ground (-1 = everyone)
    struct RpSrvDrop { uint16_t uid; uint8_t id; uint16_t count; float pos[3]; uint64_t until; };
    RpSrvDrop    m_RpDrops[RP_DROPS_MAX] = {}; uint16_t m_nRpDropUid = 0;
    int  RpInvGive(int slot, uint8_t id, int n, const char* why);   // 3.26: into the bag + "Received" card, returns how many fit
    void RpSendPlayers(int slot);
    void RpSendState(int slot);
    void RpSendCard(int slot);
    void RpOnCard(int slot, const PktRpCard& c);
    void RpEv(int slot, uint8_t ev, uint8_t a, uint8_t b, uint8_t who, int32_t val, const float* f = nullptr, const char* text = nullptr);
    void RpMsg(int slot, uint32_t rgba, const char* fmt, ...);
    void RpNear(int slot, float range, uint32_t rgba, const char* text, bool fade = true);
    void RpAll(uint32_t rgba, const char* fmt, ...);
    bool RpPay(int slot, int32_t amount, const char* why);   // false = not enough cash (amount < 0)
    void RpXp(int slot, int xp);
    void RpTick(uint64_t now);
    void RpEventTick(uint64_t now);
    void RpNextTask(int slot);
    void RpHandleCmd(int slot, const PktRpCmd& c);
    bool RpOnDamage(int from, int target, float amount);
    bool RpCanHurt(int from) const;
    void RpOnDeath(int victim, int killer);
    void RpOnLeave(int slot);
    void RpOnJoin(int slot, bool fresh);
    void RpOnAuthed(int slot);
    void RpEndCall(int slot, const char* why);
    int  RpFindNumber(int32_t number) const;
    int  RpTarget(int slot, int id, float range, const char* what);
    int  RpCallPartner(int slot) const { return (slot >= 0 && slot < MP_MAX_PLAYERS && m_Rp[slot].callWith < MP_MAX_PLAYERS) ? m_Rp[slot].callWith : -1; }
    void Log(const char* fmt, ...);
    void FillWorld(PktWorld& w);

    mp_socket_t        m_Sock;
    std::atomic<bool>  m_bRunning;
    std::atomic<bool>  m_bPersistent;
    std::thread        m_Thread;
    std::mutex         m_Mutex;
    uint16_t           m_nPort;
    std::atomic<uint32_t> m_nMinBuild{MP_MIN_BUILD};
    std::atomic<uint32_t> m_nOutdatedCount{0};
    char               m_szOutdatedLog[256] = "";
    std::mutex         m_OutdatedMutex;
    std::map<std::string, uint64_t> m_Outdated;
    void TrackOutdated(const sockaddr_in& from, const uint8_t* data, int len, uint32_t build);
    char               m_szName[32];
    Slot               m_Slots[MP_MAX_PLAYERS];
    MPServerLogFn      m_fnLog;
    std::atomic<int>   m_nMaxPlayers;
    std::atomic<int>   m_nRegion;
    std::atomic<int>   m_nIcon{0};
    char               m_szLang[MP_LANG_LEN] = {0};
    std::atomic<uint32_t> m_nStartUnix{0};
    std::atomic<uint8_t> m_nPerms{PERM_MISSION_PAIR};
    std::atomic<uint16_t> m_nModes{0};   // 3.02: custom servers start with no gamemodes
    void MakePerms(PktPerms& p, uint8_t perms) const;
    char               m_szPassword[MP_PASSWORD_LEN + 1] = {0};
    bool IsPartner(int a, int b) const { return a >= 0 && b >= 0 && a < MP_MAX_PLAYERS && b < MP_MAX_PLAYERS && a != b && m_Slots[a].used && m_Slots[b].used && m_Slots[a].pair != MP_INVALID_ID && m_Slots[a].pair == m_Slots[b].pair; }
};

uint64_t MP_NowMs();
