// Online lobby + relay over a public MQTT 3.1.1 broker. See online.h.
#include "online.h"
#include <sys/resource.h>
#include <sys/syscall.h>
#include "server.h"   // MP_NowMs
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <poll.h>
#include <dirent.h>
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/error.h"
#include "ca_roots.h"
#include "authcrypto.h"   // 3.21.6: login system topics
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#ifndef SIOCOUTQNSD
#define SIOCOUTQNSD 0x894B
#endif
#include <arpa/inet.h>
#include <atomic>
#include <deque>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <condition_variable>   // 4.05.8
#include <memory>

#if defined(__ANDROID__) && !defined(MP_LOG_STDOUT)   // MP_LOG_STDOUT: mp_server in Termux
  #include <android/log.h>
  #define OLOG(...) __android_log_print(ANDROID_LOG_INFO, "MP-Online", __VA_ARGS__)
#else
  #define OLOG(...) do { printf("[MP Online] " __VA_ARGS__); printf("\n"); } while(0)
#endif

// Everyone must use the same broker list in the same order.
// Broker list. A private broker from broker.txt (e.g. your own HiveMQ Cloud cluster: TLS + login)
// comes first; the free public brokers follow (TCP 1883, then MQTT-over-WebSocket for networks
// that block 1883). Everyone who should meet must use the same first reachable broker.
struct Broker
{
    std::string host; uint16_t port = 1883; bool ws = false; bool tls = false; std::string path = "/mqtt";
    std::string label, user, pass, caFile;
    int flushMs = 0;          // 0 = default; raise for brokers that limit messages per second
    int region = -1;          // 2.20: regional broker (REGION_*), -1 = private / public
};
static const Broker kPublicBrokers[] = {
    { "broker.hivemq.com",  1883, false, false, "",      "hivemq tcp" },     // no rate limit seen in tests (20 Hz sync, 0 % loss)
    { "broker.hivemq.com",  8000, true,  false, "/mqtt", "hivemq ws"  },
    { "broker.hivemq.com",  8883, false, true,  "",      "hivemq tls" },
    { "test.mosquitto.org", 1883, false, false, "",      "mosquitto tcp" },
    { "test.mosquitto.org", 8080, true,  false, "/",     "mosquitto ws" },
    { "broker.emqx.io",     1883, false, false, "",      "emqx tcp" },       // last: throttles fast message streams
    { "broker.emqx.io",     8083, true,  false, "/mqtt", "emqx ws" },
};
static std::vector<Broker> s_Brokers;        // built in Online::Start (read-only afterwards)
static std::vector<std::string> s_Diag;      // last result per broker (guarded by s_M)
static std::string s_ConfigNote;             // how broker.txt was read (guarded by s_M)
#define BROKER_COUNT ((int)s_Brokers.size())
// 3.04.6: the lobby moved to thirtyfox/mp5/ - MP 3.04.5 and older (they let players cheat) only know
// mp4, so they can't see or meet 3.04.6 phones over mobile data / Wi-Fi any more. On the regional
// brokers the mp4 topics are read-only for them (HiveMQ kit 3.04.6): they only see the
// "UPDATE MP" notice server, which refuses and logs them. mp_server --legacy-notice runs on mp4.
#define TOPIC_ROOT_NEW  "thirtyfox/mp5/"
#define TOPIC_ROOT_OLD  "thirtyfox/mp4/"
static std::string s_TopicRoot = TOPIC_ROOT_NEW;
#define TOPIC_ROOT      s_TopicRoot
#define KEEPALIVE_S     15
#define LIST_EVERY_MS   3000                  // host re-announces its server
#define LIST_FRESH_MS   10000                 // listing older than this = offline
#define READY_AFTER_MS  4500                  // lobby is trustworthy after this long online
#define FLUSH_MS        25                    // relay batching (fewer broker messages)
static int  s_nFlushMs = FLUSH_MS;
static int  s_nCurFlush = FLUSH_MS;
static bool s_bOwnBroker = false;   // 3.33: connected to one of our regional HiveMQ brokers with no flush / rate set in broker.txt (no message-rate limit)
static std::atomic<int> s_nCongestMs{0};   // 2.15: extra batching while the uplink is congested     // flush interval of the broker we are connected to
static int  s_nBrokerOverride = -1;
#define WANT_PUBLIC (-2)   // 4.06: DesiredRegion(): the public broker (joining a server listed only there)
// 2.20 regional brokers (own HiveMQ per region, see the HiveMQ repo). Built from broker.txt
// [Region] blocks or regions.txt downloaded from the website. Without them nothing changes.
#define MP_REGIONS_URL "https://mpsamp-open.github.io/regions.txt"            // 4.05.4: new website (mpsamp-open org)
#define MP_REGIONS_URL_OLD "https://thirtyfoxmcx00e1.github.io/wagu/regions.txt"  // 4.05.4: old address, used when the new one fails
static bool             s_bRegional = false;          // at least one regional broker known
static std::atomic<int> s_RegionPing[REGION_COUNT];   // TCP connect ms, -1 unknown, -2 unreachable
// 2.23: regions measured at least once (before that "-1" means "not measured", after it "down")
static std::atomic<bool> s_bPingedOnce{false};
static std::atomic<int>  s_nHostIcon{0};      // 2.24: icon of the server we host (0 = default)
static std::mutex        s_LangMx;            // 2.25
static std::atomic<uint32_t> s_nHostStart{0}; // 2.27
static std::atomic<bool>     s_bHostDedicated{false}; // 2.29
static char              s_szHostLang[MP_LANG_LEN] = "";
// 2.23: a broker that failed is skipped for a while (30 s, 1, 2, 4 ... max 10 min) so a dead
// region no longer costs 4-8 s of timeout on every reconnect (= the online/offline flapping)
static std::map<std::string, std::pair<int, uint64_t>> s_BrokerBackoff;   // net thread only: host:port -> fails, skip until
static std::string BrokerKey(const Broker& b) { return b.host + ":" + std::to_string(b.port) + (b.tls ? "s" : ""); }
static bool BrokerSkipped(const Broker& b, uint64_t now)
{
    auto it = s_BrokerBackoff.find(BrokerKey(b));
    return it != s_BrokerBackoff.end() && now < it->second.second;
}
static void BrokerFailed(const Broker& b, uint64_t now)
{
    auto& e = s_BrokerBackoff[BrokerKey(b)];
    e.first = e.first < 6 ? e.first + 1 : 6;
    uint64_t wait = 30000ull << (e.first - 1); if(wait > 600000) wait = 600000;
    e.second = now + wait;
}
static void BrokerOk(const Broker& b) { s_BrokerBackoff.erase(BrokerKey(b)); }
// 3.34: regions.txt "branch = 1" in a [region] block: that broker is a branch - MpBridge copies the
// game traffic between it and every other region's broker over the VPS backbone. A phone then stays
// on its own (nearest) region when it joins a server hosted in another one: the slow, jittery part
// (phone <-> broker over mobile data) stays short, the long hop runs VPS to VPS.
static std::atomic<bool> s_RegionBranch[REGION_COUNT];
static bool RegionUp(int r) { return r >= 0 && r < REGION_COUNT && s_RegionPing[r] >= 0; }
static std::atomic<int> s_nPrefRegion{-1};            // -1 = auto (lowest ping), else REGION_*
static std::atomic<int> s_nUiRegion{REGION_ASIA};     // region picked in the menu
static std::atomic<int> s_nCurRegion{-1};             // region of the broker we're connected to
static int              s_nJoinRegion = -1;           // region of the server we're joining (guarded)
static std::string      s_RegionsUrl = MP_REGIONS_URL;
static std::string      s_RegionsCache;               // dataDir/regions_cache.txt
static std::string      s_RegionsNote;                // guarded
static std::atomic<bool> s_bPinging{false};
static bool             s_bFetchedAtStart = false;
#define BRIDGE_IDLE_MS  30000
#define MAX_BRIDGES     120
#define RELAY_NET       0x7F4D0000u           // 127.77.0.0

// ------------------------------------------------------------------------------------------
struct Listing
{
    char     name[32];
    uint16_t port;
    int      players, maxPlayers, region;
    uint16_t proto;
    uint8_t  icon;           // 2.24: "proto:icon" in the listing (old phones read proto with atoi)
    char     lang[MP_LANG_LEN];   // 2.25: "proto:icon:language"
    uint32_t startUnix;           // 2.27: "proto:icon@start:language" (old phones: atoi stops at '@')
    int      hostRtt;             // 4.05.1: "@start!d~45:" = the host's own broker round trip (ms), 0 = unknown
    bool     dedicated;           // 2.29: "proto:icon@start!d:language" (old phones: strtoul stops at '!')
    bool     confirmed;      // got a live update (a retained copy alone may be stale)
    uint64_t lastSeen;
};

struct Bridge                 // host side: one local UDP socket per remote player
{
    int         fd = -1;
    uint64_t    lastSeen = 0;
    std::string down;         // batched frames for this player
    uint64_t    heldSince = 0;    // 2.18.3: positions waiting for the upload to be free (0 = none)
    bool        shared = false;   // 2.18.3: this player also gets the host's shared updates (b/<token>)
    std::string urg;          // 2.18.3: ping / pong / damage frames: sent right away, ahead of the queue
};

// 2.17: tiny frames that measure / decide something. They used to wait for the batch timer
// (25 ms + up to 150 ms "congestion" extra) on BOTH sides, which is most of the ping players saw.
// 2.18.3 shared updates: a player tells the host's relay "I also listen on b/<host token>"
// with this relay-only frame (never reaches the game server; older hosts just pass it on and
// the server ignores unknown packets). Updates the host would send to most players are then
// published ONCE there and the broker copies them to everyone, instead of the host uploading
// one copy per player - its upload grows with the number of players, not players squared.
#define MP_RELAY_CAP 0xFB
static inline bool MP_IsUrgentFrame(const uint8_t* d, int n) { return n > 0 && (d[0] == PKT_PING || d[0] == PKT_PONG || d[0] == PKT_DAMAGE || d[0] == PKT_VOICE); }   // 3.11: + voice (never waits for the batch)

static std::mutex             s_M;
static std::atomic<bool>      s_bStarted{false};
static std::atomic<bool>      s_bConnected{false};
static std::atomic<int>       s_nAttemptFailed{0};    // full broker list failed this many times in a row
static std::atomic<uint64_t>  s_nReadySince{0};       // first moment of the current online streak
static std::atomic<int>       s_nRtt{-1};
static std::atomic<int>       s_nRttJitter{0};   // 4.06: smoothed |rtt - last rtt| of the broker pings (host side jitter)
static char                   s_szBroker[64] = "";
static char                   s_szCid[24] = "";        // our MQTT client id = our relay address

static std::map<uint32_t, Listing> s_List;            // token -> listing (guarded)
static std::map<uint32_t, uint32_t> s_RelayIdx;       // token -> relay index (guarded)
// 4.06: the public lobby, read while we sit on a regional broker. MpBridge copies nothing from the
// public lobby into the regions, so players whose phones could not reach a region (blocked port, far away,
// home broker not reachable from their network) were invisible to everyone on a region - and the other way
// round. Their servers now show in the list; joining one moves the lobby link to the public broker.
static std::map<uint32_t, Listing> s_PubList;          // token -> listing (guarded)
static std::atomic<int>  s_nPubRtt{-1};
static std::atomic<bool> s_bPubUp{false};
static bool              s_bJoinPublic = false;        // guarded: the server we join is only in the public lobby
static void DirectThread();   // 3.11
struct DirectInfo { uint32_t ip = 0; int rtt = -1; uint64_t at = 0; bool ok = false; int fails = 0; uint64_t badUntil = 0; uint16_t port = 0; };
static std::map<uint32_t, DirectInfo> s_Direct;   // 3.11: token -> direct UDP probe result (guarded by s_M)

static uint32_t               s_nNextIdx = 1;

// hosting (game thread writes, net thread reads; guarded)
static bool     s_bHostOn = false;
static char     s_szHostName[32] = "";
static uint16_t s_nHostPort = 0;
static int      s_nHostPlayers = 0, s_nHostMax = 0, s_nHostRegion = 0;
static std::atomic<uint32_t> s_nHostToken{0};

// client proxy (guarded)
static int         s_ProxyFd = -1;
static uint32_t    s_ProxyToken = 0;
static sockaddr_in s_ProxyPeer;          // MPClient's socket
static bool        s_bProxyPeer = false;
static std::vector<int> s_CloseFds;

static uint32_t Rand32()
{
    uint32_t v = 0;
    FILE* f = fopen("/dev/urandom", "rb");
    if(f) { if(fread(&v, 1, sizeof(v), f) != sizeof(v)) v = 0; fclose(f); }
    if(!v) v = (uint32_t)MP_NowMs() * 2654435761u ^ (uint32_t)(uintptr_t)&v;
    return v ? v : 1;
}

static void Hex8(char* out, uint32_t v) { snprintf(out, 9, "%08x", v); }
static uint32_t ParseHex8(const char* s) { return (uint32_t)strtoul(s, nullptr, 16); }


// ------------------------------------------------------------------------------------------
// 4.05.8: network hand-over (Wi-Fi <-> mobile data)
// Switching Wi-Fi off / on (or Wi-Fi -> mobile data) used to freeze the game and leave the lobby on the
// public HiveMQ broker for minutes:
//  - the broker socket of the old network was only noticed as dead after 7 s (in a game) / 20 s
//  - getaddrinfo() right after the switch can hang for 5-30 s while the new network's DNS comes up,
//    and every broker was tried one after another (4-8 s each)
//  - every broker that failed during the switch got the 30 s ... 10 min back-off, and the region ping
//    that ran in that second marked all 4 regions "down", so the phone sat on the public lobby
//    (one far broker = the old 400+ ms) until the next ping round 2 minutes later
// Now: the interfaces are watched (every 0.5 s). On a change the socket of a network that is gone is
// dropped at once, back-offs are cleared, the regions are pinged again right away (results measured
// across the switch are thrown away), DNS has a time limit and falls back to the last good address.
static std::atomic<uint32_t> s_nNetGen{0};          // +1 on every interface change
static std::atomic<uint64_t> s_nNetChangedAt{0};    // MP_NowMs() of the last change
static std::atomic<bool>     s_bPingNow{false};     // ping thread: measure the regions now
static std::mutex            s_NetSigMx;
static std::string           s_NetSig;               // "wlan0=c0a80105,rmnet0=0a..." (guarded)

// 4.06: only the network the internet traffic actually uses (the source address of the default
// route). 4.05.8 hashed every interface: a VPN app, hotspot, Wi-Fi Direct, CLAT or a second SIM coming and
// going counted as "network changed" - region pings were wiped and the lobby left its broker again and
// again (= nobody in the list, high ping). A UDP connect() sends nothing, it only asks the routing table.
static std::string NetSignature()
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(fd < 0) return std::string();
    sockaddr_in d; memset(&d, 0, sizeof(d)); d.sin_family = AF_INET; d.sin_port = htons(53);
    d.sin_addr.s_addr = htonl(0x01010101u);   // 1.1.1.1
    std::string s;
    if(connect(fd, (sockaddr*)&d, sizeof(d)) == 0)
    {
        sockaddr_in la; socklen_t ll = sizeof(la); memset(&la, 0, sizeof(la));
        if(getsockname(fd, (sockaddr*)&la, &ll) == 0 && la.sin_addr.s_addr) { char b[16]; snprintf(b, sizeof(b), "%08x", ntohl(la.sin_addr.s_addr)); s = b; }
    }
    close(fd);
    return s;   // "" = no route to the internet
}
// true when the network the socket was opened on (its local address) is still on this phone
static bool SocketNetUp(int fd)
{
    if(fd < 0) return false;
    sockaddr_in la; socklen_t ll = sizeof(la); memset(&la, 0, sizeof(la));
    if(getsockname(fd, (sockaddr*)&la, &ll) != 0 || la.sin_family != AF_INET) return true;   // unknown: keep it
    uint32_t ip = ntohl(la.sin_addr.s_addr);
    if(!ip || (ip >> 24) == 127) return true;
    { char b[16]; snprintf(b, sizeof(b), "%08x", ip); if(NetSignature() == b) return true; }   // 4.06: still the internet route
    std::vector<LanIface> ifs = LAN::Interfaces();
    if(ifs.empty()) return false;                    // no network at all
    for(const LanIface& i : ifs) if(i.ip == ip) return true;
    return false;
}
// called often (net thread); true when the interfaces changed since the last call
static bool NetWatchTick()
{
    static uint64_t last = 0; static bool first = true;
    static std::string pend; static int pendN = 0;
    uint64_t now = MP_NowMs();
    if(now - last < 500) return false;
    last = now;
    std::string sig = NetSignature();
    std::lock_guard<std::mutex> lk(s_NetSigMx);
    if(first) { first = false; s_NetSig = sig; return false; }
    if(sig == s_NetSig) { pendN = 0; return false; }
    // 4.06: the new route has to stay for 1 s (3 reads): a blip while Android re-evaluates networks is not a switch
    if(sig != pend) { pend = sig; pendN = 1; return false; }
    if(++pendN < 3) return false;
    pendN = 0;
    OLOG("4.05.8: network changed [%s] -> [%s]", s_NetSig.c_str(), sig.c_str());
    s_NetSig = sig;
    ++s_nNetGen; s_nNetChangedAt = now; s_bPingNow = true;
    return true;
}
// 4.06: "no network" only when there is neither a route to the internet nor any interface address
static bool NoNetwork() { return NetSignature().empty() && !LAN::HasNetwork(); }
static bool NetChangedRecently(uint64_t now, uint64_t ms) { uint64_t t = s_nNetChangedAt; return t && now - t < ms; }
// other links (auth, social): drop the socket when the network it was on is gone
static bool NetMovedAway(int fd, uint32_t& seenGen)
{
    uint32_t g = s_nNetGen;
    if(g == seenGen) return false;
    seenGen = g;
    return fd >= 0 && !SocketNetUp(fd);
}

// DNS with a time limit + the last good address per host. getaddrinfo cannot be cancelled, so it runs
// on its own short thread; a late answer still fills the cache for the next try.
static std::mutex s_DnsMx;
static std::map<std::string, std::vector<uint32_t>> s_DnsCache;   // host -> IPv4 (network order)
static int ResolveV4(const std::string& host, uint16_t port, std::vector<sockaddr_in>& out, char* err, size_t errLen)
{
    out.clear();
    sockaddr_in sa; memset(&sa, 0, sizeof(sa)); sa.sin_family = AF_INET; sa.sin_port = htons(port);
    if(inet_pton(AF_INET, host.c_str(), &sa.sin_addr) == 1) { out.push_back(sa); return 0; }
    std::vector<uint32_t> cached;
    { std::lock_guard<std::mutex> lk(s_DnsMx); auto it = s_DnsCache.find(host); if(it != s_DnsCache.end()) cached = it->second; }
    struct Job { std::mutex m; std::condition_variable cv; bool done = false; int rc = EAI_AGAIN; std::vector<uint32_t> ips; };
    auto job = std::make_shared<Job>();
    std::string h = host;
    std::thread([job, h]()
    {
        addrinfo hints, *res = nullptr; memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
        int rc = getaddrinfo(h.c_str(), nullptr, &hints, &res);
        std::vector<uint32_t> ips;
        if(rc == 0) for(addrinfo* a = res; a; a = a->ai_next)
            if(a->ai_family == AF_INET && a->ai_addrlen >= (socklen_t)sizeof(sockaddr_in)) ips.push_back(((sockaddr_in*)a->ai_addr)->sin_addr.s_addr);
        if(res) freeaddrinfo(res);
        if(rc == 0 && ips.empty()) rc = EAI_NONAME;
        if(rc == 0) { std::lock_guard<std::mutex> lk(s_DnsMx); s_DnsCache[h] = ips; }
        std::lock_guard<std::mutex> lk(job->m); job->rc = rc; job->ips.swap(ips); job->done = true; job->cv.notify_all();
    }).detach();
    int waitMs = cached.empty() ? 10000 : 2500;      // known host: do not wait for a slow new network
    std::vector<uint32_t> ips; int rc;
    {
        std::unique_lock<std::mutex> lk(job->m);
        job->cv.wait_for(lk, std::chrono::milliseconds(waitMs), [&]{ return job->done; });
        rc = job->done ? job->rc : EAI_AGAIN;
        if(job->done && rc == 0) ips = job->ips;
    }
    if(ips.empty() && !cached.empty()) { ips = cached; OLOG("4.05.8: DNS %s %s, using the last address", host.c_str(), rc == EAI_AGAIN ? "slow" : "failed"); }
    if(ips.empty()) { if(err) snprintf(err, errLen, "DNS lookup failed (%d)", rc); return rc ? rc : EAI_NONAME; }
    for(uint32_t ip : ips) { sa.sin_addr.s_addr = ip; out.push_back(sa); }
    return 0;
}
// 4.05.8: a path that died (old network, mobile NAT) errors out in ~10 s instead of minutes
#ifndef TCP_USER_TIMEOUT
#define TCP_USER_TIMEOUT 18
#endif
static void TcpLiveness(int fd)
{
    int one = 1, idle = 10, intvl = 3, cnt = 3, uto = 25000;   // 4.06: 25 s (12 s dropped slow mobile uplinks)
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one));
#ifdef TCP_KEEPIDLE
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
#endif
#ifdef TCP_KEEPINTVL
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
#endif
#ifdef TCP_KEEPCNT
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &cnt, sizeof(cnt));
#endif
    setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &uto, sizeof(uto));
    // 4.06: no DSCP EF on TCP any more (4.05.9 had it): the Wi-Fi voice queue sends without aggregation, the
    // lobby link's bigger bursts (lists, previews, relayed game) waited longer, and some mobile networks slow EF down
}

// ------------------------------------------------------------------------------------------
// minimal MQTT 3.1.1 client (QoS 0 only)
class Mqtt
{
public:
    int  fd = -1;
    bool ws = false;          // MQTT over WebSocket (for networks that block port 1883)
    bool tls = false;         // TLS (mbedTLS) on top of TCP
    bool tlsReady = false;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config  conf;
    std::string in;           // MQTT byte stream
    std::string wsin;         // raw WebSocket bytes not decoded yet
    std::string out;          // 2.14: bytes waiting for the socket (never block the relay thread)
    size_t      outOff = 0;
    // 2.18.3: urgent frames (ping / pong / damage) go out BEFORE queued positions. They used to
    // wait at the end of the queue, so on a busy upload every pong showed the whole queue as ping.
    std::string pri;          // urgent MQTT packets
    size_t      priOff = 0;
    std::deque<size_t> outEnds;   // where each packet in `out` ends (switch only between packets)
    int  midQ = 0;            // 1 = a packet of `out` is half written, 2 = of `pri` (TLS wants the same bytes again)
    bool nextPri = false;     // the next RawSend goes to `pri`
    bool blockingSend = true; // only while connecting (CONNECT must go out before CONNACK)
    uint32_t dropped = 0;     // batches dropped because the upload is too slow
    char err[96] = "";        // why the last Open failed

    size_t Pending() const { return (out.size() - outOff) + (pri.size() - priOff); }
    // 2.18.3: bytes the kernel has accepted but not put on the network yet
    int KernelUnsent() const { int v = 0; if(fd >= 0 && ioctl(fd, SIOCOUTQNSD, &v) == 0 && v > 0) return v; return 0; }
    // 2.18.3: the TCP connection's own smoothed round trip (ms, -1 unknown). Unlike the MQTT
    // ping it doesn't include the broker's processing, only the network path - so when it grows,
    // it's a queue in the network (usually the phone's / carrier's upload buffer) filling up.
    int TcpRttMs() const
    {
        if(fd < 0) return -1;
        struct tcp_info ti; socklen_t l = sizeof(ti); memset(&ti, 0, sizeof(ti));
        if(getsockopt(fd, IPPROTO_TCP, TCP_INFO, &ti, &l) != 0 || !ti.tcpi_rtt) return -1;
        return (int)(ti.tcpi_rtt / 1000);
    }
    // 2.15 delay-based rate control. A full queue in the NETWORK (carrier / Wi-Fi router
    // buffers) can't be seen from the phone, but our MQTT pings wait in it too: when their round
    // trip grows, the loop below lowers rateBps and position batches above it are skipped.
    double rateBps = 128 * 1024, tokens = 32 * 1024; uint64_t tokT = 0;
    bool TakeTokens(size_t n)
    {
        uint64_t t = MP_NowMs();
        if(tokT) tokens += (double)(t - tokT) * rateBps / 1000.0;
        tokT = t;
        double cap = rateBps * 0.25; if(cap < 4096) cap = 4096;
        if(tokens > cap) tokens = cap;
        if(tokens < (double)n) return false;
        tokens -= (double)n; return true;
    }
    // 2.15: how long the oldest queued byte has waited (ms). Position batches older than
    // ~150 ms are worthless, so this, not the byte count, decides what gets dropped.
    uint64_t queuedTotal = 0, sentTotal = 0;
    struct Mark { uint64_t end; uint64_t t; };
    std::vector<Mark> marks; size_t markHead = 0;
    int QueueAgeMs()
    {
        while(markHead < marks.size() && marks[markHead].end <= sentTotal) ++markHead;
        if(markHead > 256) { marks.erase(marks.begin(), marks.begin() + markHead); markHead = 0; }
        if(markHead >= marks.size()) return 0;
        return (int)(MP_NowMs() - marks[markHead].t);
    }

    bool Open(const Broker& b, const char* cid, const char* willTopic, int timeoutMs)
    {
        Close();
        ws = b.ws; tls = b.tls;
        std::vector<sockaddr_in> addrs;   // 4.05.8: DNS with a time limit + last good address
        if(ResolveV4(b.host, b.port, addrs, err, sizeof(err)) != 0) return false;
        bool ok = false;
        int lastErr = 0;
        for(size_t ai = 0; ai < addrs.size() && !ok; ++ai)
        {
            fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if(fd < 0) { lastErr = errno; continue; }
            fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
            int r = connect(fd, (const sockaddr*)&addrs[ai], sizeof(sockaddr_in));
            if(r != 0 && errno == EINPROGRESS)
            {
                pollfd p = { fd, POLLOUT, 0 };
                if(poll(&p, 1, timeoutMs) == 1)
                {
                    int e = 0; socklen_t el = sizeof(e);
                    getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &el);
                    r = e ? -1 : 0; if(e) lastErr = e;
                }
                else { r = -1; lastErr = ETIMEDOUT; }
            }
            else if(r != 0) lastErr = errno;
            if(r == 0) ok = true;
            else { close(fd); fd = -1; }
        }
        if(!ok) { snprintf(err, sizeof(err), "TCP connect failed (%s)", lastErr == ETIMEDOUT ? "timeout - port blocked?" : strerror(lastErr)); return false; }
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        TcpLiveness(fd);   // 4.05.8
        // 2.15: keep the kernel from hiding seconds of data. Phones auto-size the TCP send
        // buffer to megabytes; on a weak uplink that buffer filled over a few minutes and every
        // message (pings too) waited behind it. Now at most ~16 KB sit unsent in the kernel and
        // the rest stays in our queue, where old positions can be dropped.
#ifndef TCP_NOTSENT_LOWAT
#define TCP_NOTSENT_LOWAT 25
#endif
        int lowat = 8 * 1024;   // 2.18.3: was 16 KB
        setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(lowat));
        int sndbuf = 64 * 1024;
        setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
        in.clear(); wsin.clear();

        if(tls && !TlsHandshake(b, timeoutMs)) { Close(); return false; }
        if(ws && !WsHandshake(b, timeoutMs)) { Close(); return false; }

        // CONNECT
        std::string v;
        PutStr(v, "MQTT"); v += (char)4;
        uint8_t flags = 0x02;                                     // clean session
        if(willTopic && *willTopic) flags |= 0x04 | 0x20;         // will, retained, QoS 0
        if(!b.user.empty()) flags |= 0x80;                        // username
        if(!b.user.empty() && !b.pass.empty()) flags |= 0x40;     // password
        v += (char)flags; v += (char)(KEEPALIVE_S >> 8); v += (char)(KEEPALIVE_S & 0xFF);
        PutStr(v, cid);
        if(willTopic && *willTopic) { PutStr(v, willTopic); PutStr(v, ""); } // empty retained = delete listing
        if(!b.user.empty()) PutStr(v, b.user.c_str());
        if(!b.user.empty() && !b.pass.empty()) PutStr(v, b.pass.c_str());
        if(!SendPacket(0x10, v)) { snprintf(err, sizeof(err), "send failed"); Close(); return false; }

        uint64_t until = MP_NowMs() + (uint64_t)timeoutMs;
        while(MP_NowMs() < until)
        {
            uint8_t type; std::string body;
            int r = ReadPacket(type, body, 100);
            if(r < 0) { snprintf(err, sizeof(err), "broker closed the connection"); Close(); return false; }
            if(r == 0) continue;
            if((type >> 4) == 2)
            {
                if(body.size() >= 2 && body[1] == 0) { err[0] = 0; return true; }
                int rc = body.size() >= 2 ? (int)(uint8_t)body[1] : -1;
                snprintf(err, sizeof(err), "broker refused: %s", rc == 4 ? "wrong username/password" : rc == 5 ? "not authorized" : rc == 2 ? "client id rejected" : rc == 1 ? "MQTT version" : "unavailable");
                Close(); return false;
            }
        }
        snprintf(err, sizeof(err), "no answer from broker (timeout)");
        Close();
        return false;
    }

    void Close()
    {
        out.clear(); outOff = 0; pri.clear(); priOff = 0; outEnds.clear(); midQ = 0; nextPri = false; blockingSend = true; queuedTotal = sentTotal = 0; marks.clear(); markHead = 0;
        if(tlsReady) { mbedtls_ssl_close_notify(&ssl); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); tlsReady = false; }
        if(fd >= 0) close(fd);
        fd = -1; in.clear(); wsin.clear();
    }
    bool HasBuffered() { return tlsReady && mbedtls_ssl_get_bytes_avail(&ssl) > 0; }

    void Disconnect()                          // graceful: the broker does NOT send the will
    {
        if(fd >= 0) { std::string e; SendPacket(0xE0, e); }
        Close();
    }

    bool Subscribe(const char* filter, uint16_t id)
    {
        std::string v;
        v += (char)(id >> 8); v += (char)(id & 0xFF);
        PutStr(v, filter); v += (char)0;
        return SendPacket(0x82, v);
    }

    bool Unsubscribe(const char* filter, uint16_t id)   // 2.18.3
    {
        std::string v;
        v += (char)(id >> 8); v += (char)(id & 0xFF);
        PutStr(v, filter);
        return SendPacket(0xA2, v);
    }

    // droppable = position batches: if the upload can't keep up, sending them late is worse
    // than not sending them (the next batch has newer positions). This is what turned a slow
    // host upload into 30 s of "ping" in 2.11-2.13.
    bool Publish(const std::string& topic, const void* data, size_t len, bool retain, bool droppable = false, bool urgent = false)
    {
        if(droppable && (Pending() > OUT_SOFT_LIMIT || QueueAgeMs() > OUT_MAX_AGE_MS || !TakeTokens(len + topic.size() + 4))) { ++dropped; return true; }
        std::string v;
        PutStr(v, topic.c_str());
        v.append((const char*)data, len);
        nextPri = urgent;
        bool ok = SendPacket(0x30 | (retain ? 1 : 0), v);
        nextPri = false;
        return ok;
    }

    bool Ping() { std::string e; return SendPacket(0xC0, e); }

    // 1 = got a packet, 0 = nothing yet, -1 = connection closed/broken
    int ReadPacket(uint8_t& type, std::string& body, int waitMs)
    {
        for(;;)
        {
            int r = TryParse(type, body);
            if(r) return r;
            if(fd < 0) return -1;
            if(waitMs > 0 && !HasBuffered())
            {
                pollfd p = { fd, POLLIN, 0 };
                if(poll(&p, 1, waitMs) <= 0) return 0;
                waitMs = 0;
            }
            char buf[8192];
            ssize_t n;
            if(tls)
            {
                int r = mbedtls_ssl_read(&ssl, (unsigned char*)buf, sizeof(buf));
                if(r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) return 0;
#ifdef MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET
                if(r == MBEDTLS_ERR_SSL_RECEIVED_NEW_SESSION_TICKET) continue;
#endif
                if(r <= 0) return -1;
                n = r;
            }
            else
            {
                n = recv(fd, buf, sizeof(buf), 0);
                if(n == 0) return -1;
                if(n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? 0 : -1;
            }
            if(!ws) in.append(buf, (size_t)n);
            else { wsin.append(buf, (size_t)n); if(!WsDecode()) return -1; }
        }
    }

private:
    static void PutStr(std::string& v, const char* s)
    {
        size_t n = strlen(s);
        v += (char)(n >> 8); v += (char)(n & 0xFF); v.append(s, n);
    }

    static const size_t OUT_SOFT_LIMIT = 16 * 1024;    // drop position batches beyond this much queued...
    static const int    OUT_MAX_AGE_MS = 150;          // ...or when the queue is older than this
    static const size_t OUT_HARD_LIMIT = 512 * 1024;   // hopeless: reconnect

    bool RawSend(const std::string& p)
    {
        if(fd < 0) return false;
        if(Pending() + p.size() > OUT_HARD_LIMIT) return false;
        if(nextPri) pri += p;
        else
        {
            if(outOff && outOff == out.size() && midQ != 1) { out.clear(); outOff = 0; outEnds.clear(); }
            out += p; outEnds.push_back(out.size());
        }
        queuedTotal += p.size(); marks.push_back({ queuedTotal, MP_NowMs() });
        return FlushOut(blockingSend ? 3000 : 0);
    }

public:
    // write as much of the queues as the socket takes (urgent first, but never in the middle of
    // a packet); waits only if waitMs > 0
    bool FlushOut(int waitMs = 0)
    {
        uint64_t until = MP_NowMs() + (uint64_t)(waitMs > 0 ? waitMs : 0);
        while(Pending())
        {
            while(!outEnds.empty() && outEnds.front() <= outOff) outEnds.pop_front();
            bool usePri = midQ == 2 || (midQ != 1 && priOff < pri.size());
            const char* d; size_t n, end;
            if(usePri) { d = pri.data() + priOff; n = pri.size() - priOff; end = 0; }
            else { end = outEnds.empty() ? out.size() : outEnds.front(); d = out.data() + outOff; n = end - outOff; }
            if(!n) { midQ = 0; if(!usePri && outOff >= out.size()) break; continue; }
#ifndef __ANDROID__
            // PC test only: MP_TEST_UPLOAD_KBPS simulates a phone host's slow upload
            static double s_Tok = 0; static uint64_t s_TokT = 0; static int s_Kbps = getenv("MP_TEST_UPLOAD_KBPS") ? atoi(getenv("MP_TEST_UPLOAD_KBPS")) : 0;
            if(s_Kbps > 0 && !blockingSend)
            {
                uint64_t t = MP_NowMs(); if(s_TokT) s_Tok += (t - s_TokT) * s_Kbps; s_TokT = t;
                if(s_Tok > s_Kbps * 50.0) s_Tok = s_Kbps * 50.0;
                if(s_Tok < 1) break;
                if(n > (size_t)s_Tok) n = (size_t)s_Tok;
                s_Tok -= n;
            }
#endif
            short want = POLLOUT;
            ssize_t w = -1;
            if(tls)
            {
                int r = mbedtls_ssl_write(&ssl, (const unsigned char*)d, n);   // same bytes again after WANT_WRITE, as mbedTLS requires
                if(r > 0) w = r;
                else
                {
                    if(r != MBEDTLS_ERR_SSL_WANT_WRITE && r != MBEDTLS_ERR_SSL_WANT_READ) return false;
                    if(r == MBEDTLS_ERR_SSL_WANT_READ) want = POLLIN;
                    midQ = usePri ? 2 : 1;
                }
            }
            else
            {
                w = send(fd, d, n, MSG_NOSIGNAL);
                if(w <= 0 && !(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))) return false;
            }
            if(w > 0)
            {
                sentTotal += (size_t)w;
                if(usePri) { priOff += (size_t)w; midQ = priOff < pri.size() ? 2 : 0; }
                else       { outOff += (size_t)w; midQ = outOff < end ? 1 : 0; }
                continue;
            }
            if(waitMs <= 0 || MP_NowMs() > until) break;
            pollfd pf = { fd, want, 0 };
            poll(&pf, 1, 50);
        }
        if(priOff == pri.size()) { pri.clear(); priOff = 0; if(midQ == 2) midQ = 0; }
        if(outOff == out.size()) { out.clear(); outOff = 0; outEnds.clear(); if(midQ == 1) midQ = 0; }
        else if(outOff > 64 * 1024)
        {
            while(!outEnds.empty() && outEnds.front() <= outOff) outEnds.pop_front();
            out.erase(0, outOff); for(auto& e : outEnds) e -= outOff; outOff = 0;
        }
        return true;
    }

private:
    // client -> server WebSocket frame: binary, FIN, masked
    bool WsSend(uint8_t opcode, const std::string& data)
    {
        std::string f; f += (char)(0x80 | opcode);
        size_t n = data.size();
        if(n < 126) f += (char)(0x80 | n);
        else if(n < 65536) { f += (char)(0x80 | 126); f += (char)(n >> 8); f += (char)(n & 0xFF); }
        else { f += (char)(0x80 | 127); for(int i = 7; i >= 0; --i) f += (char)((uint64_t)n >> (i * 8)); }
        uint32_t m = (uint32_t)MP_NowMs() * 2654435761u ^ 0x5A17C3E1u;
        uint8_t mk[4] = { (uint8_t)(m >> 24), (uint8_t)(m >> 16), (uint8_t)(m >> 8), (uint8_t)m };
        f.append((const char*)mk, 4);
        size_t base = f.size();
        f += data;
        for(size_t i = 0; i < n; ++i) f[base + i] ^= mk[i & 3];
        return RawSend(f);
    }

    bool WsDecode()
    {
        for(;;)
        {
            if(wsin.size() < 2) return true;
            uint8_t b0 = (uint8_t)wsin[0], b1 = (uint8_t)wsin[1];
            size_t len = b1 & 0x7F, hl = 2;
            if(len == 126) { if(wsin.size() < 4) return true; len = ((size_t)(uint8_t)wsin[2] << 8) | (uint8_t)wsin[3]; hl = 4; }
            else if(len == 127) { if(wsin.size() < 10) return true; len = 0; for(int i = 0; i < 8; ++i) len = (len << 8) | (uint8_t)wsin[2 + i]; hl = 10; }
            bool masked = b1 & 0x80;
            if(masked) hl += 4;
            if(len > 1024 * 1024) return false;
            if(wsin.size() < hl + len) return true;
            std::string payload = wsin.substr(hl, len);
            if(masked) for(size_t i = 0; i < len; ++i) payload[i] ^= wsin[hl - 4 + (i & 3)];
            wsin.erase(0, hl + len);
            uint8_t op = b0 & 0x0F;
            if(op == 0x0 || op == 0x1 || op == 0x2) in += payload;     // data (MQTT is a byte stream)
            else if(op == 0x8) return false;                             // close
            else if(op == 0x9) WsSend(0xA, payload);                     // ping -> pong
        }
    }

public:   // 2.20: HttpsGet uses these too
    static int BioSend(void* ctx, const unsigned char* buf, size_t len)
    {
        int fd = (int)(intptr_t)ctx;
        ssize_t n = send(fd, buf, len, MSG_NOSIGNAL);
        if(n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
        return (int)n;
    }
    static int BioRecv(void* ctx, unsigned char* buf, size_t len)
    {
        int fd = (int)(intptr_t)ctx;
        ssize_t n = recv(fd, buf, len, 0);
        if(n == 0) return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
        if(n < 0) return (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
        return (int)n;
    }

    bool TlsHandshake(const Broker& b, int timeoutMs);

    bool WsHandshake(const Broker& b, int timeoutMs)
    {
        char req[512];
        snprintf(req, sizeof(req),
            "GET %s HTTP/1.1\r\nHost: %s:%u\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
            "Sec-WebSocket-Key: dGZtcC13cy1rZXktMDAwMQ==\r\nSec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: mqtt\r\n\r\n", b.path.c_str(), b.host.c_str(), b.port);
        if(!RawSend(req)) { snprintf(err, sizeof(err), "WebSocket send failed"); return false; }
        std::string resp;
        uint64_t until = MP_NowMs() + (uint64_t)timeoutMs;
        while(MP_NowMs() < until && resp.find("\r\n\r\n") == std::string::npos)
        {
            pollfd p = { fd, POLLIN, 0 };
            if(poll(&p, 1, 100) <= 0) continue;
            char buf[1024];
            ssize_t n;
            if(tls)
            {
                int r = mbedtls_ssl_read(&ssl, (unsigned char*)buf, sizeof(buf));
                if(r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
                if(r <= 0) break;
                n = r;
            }
            else
            {
                n = recv(fd, buf, sizeof(buf), 0);
                if(n <= 0 && !(n < 0 && (errno == EAGAIN || errno == EINTR))) break;
            }
            if(n > 0) resp.append(buf, (size_t)n);
        }
        size_t e = resp.find("\r\n\r\n");
        if(e == std::string::npos || resp.compare(0, 12, "HTTP/1.1 101")) { snprintf(err, sizeof(err), "WebSocket refused"); return false; }
        wsin = resp.substr(e + 4);
        return WsDecode();
    }

    bool SendPacket(uint8_t hdr, const std::string& v)
    {
        if(fd < 0) return false;
        std::string p; p += (char)hdr;
        size_t len = v.size();
        do { uint8_t d = len % 128; len /= 128; if(len) d |= 0x80; p += (char)d; } while(len);
        p += v;
        return ws ? WsSend(0x2, p) : RawSend(p);
    }

    int TryParse(uint8_t& type, std::string& body)
    {
        if(in.size() < 2) return 0;
        size_t len = 0, mul = 1, i = 1;
        for(;;)
        {
            if(i >= in.size()) return 0;
            uint8_t d = (uint8_t)in[i++];
            len += (d & 127) * mul;
            if(!(d & 128)) break;
            mul *= 128;
            if(i > 4) return -1;
        }
        if(len > 512 * 1024) return -1;
        if(in.size() < i + len) return 0;
        type = (uint8_t)in[0];
        body.assign(in, i, len);
        in.erase(0, i + len);
        return 1;
    }
};

// ------------------------------------------------------------------------------------------
static std::string TopicList(uint32_t tok) { char h[9]; Hex8(h, tok); return (TOPIC_ROOT + "l/") + h; }
static std::string TopicUp(uint32_t tok, const char* cid) { char h[9]; Hex8(h, tok); return (TOPIC_ROOT + "s/") + h + "/" + cid; }
static std::string TopicDown(const char* cid) { return (TOPIC_ROOT + "c/") + cid; }
static std::string TopicDetails(uint32_t tok) { char h[9]; Hex8(h, tok); return (TOPIC_ROOT + "d/") + h; }   // 4.03
static std::mutex        s_DetMx;                          // 4.03: CS:GO style list details
static std::string       s_HostDetails;                    // what we publish on d/<our token>
static std::map<uint32_t, std::pair<std::string, uint64_t>> s_Details;   // token -> details, arrival
static std::string TopicPreview(uint32_t tok) { char h[9]; Hex8(h, tok); return (TOPIC_ROOT + "p/") + h; }   // 4.04
static std::string       s_HostPreview;                    // 4.04: JPEG bytes we publish on p/<our token>
static std::map<uint32_t, std::string> s_Previews;         // token -> JPEG (Upload preview image of that host)
static std::string TopicShared(uint32_t tok) { char h[9]; Hex8(h, tok); return (TOPIC_ROOT + "b/") + h; }   // 2.18.3

static void AppendFrame(std::string& b, const uint8_t* d, int n)
{
    if(n <= 0 || n > 0xFFFF) return;
    b += (char)(n >> 8); b += (char)(n & 0xFF); b.append((const char*)d, (size_t)n);
}

// 2.14: a batch only needs the NEWEST position of each player. Older PKT_SYNC frames of the
// same player are removed; everything else (chat, deaths, joins, env events) is kept in order.
static void CoalesceSyncs(std::string& b)
{
    if(b.size() < 64) return;
    int last[256], lastNpc[256], lastVeh[256]; for(int i = 0; i < 256; ++i) last[i] = lastNpc[i] = lastVeh[i] = -1;   // 2.18: NPC frames too, 2.18.3: car state
    const uint8_t* p = (const uint8_t*)b.data(); size_t n = b.size(), o = 0; int idx = 0, syncs = 0;
    while(o + 2 <= n)
    {
        size_t l = ((size_t)p[o] << 8) | p[o + 1];
        if(o + 2 + l > n) break;
        if(l >= 2 && p[o + 2] == PKT_SYNC) { last[p[o + 3]] = idx; ++syncs; }
        if(l >= 2 && p[o + 2] == PKT_NPC) { lastNpc[p[o + 3]] = idx; ++syncs; }
        if(l >= 2 && p[o + 2] == PKT_VEH_STATE) { lastVeh[p[o + 3]] = idx; ++syncs; }
        o += 2 + l; ++idx;
    }
    if(syncs < 2) return;
    std::string r; r.reserve(n);
    o = 0; idx = 0;
    while(o + 2 <= n)
    {
        size_t l = ((size_t)p[o] << 8) | p[o + 1];
        if(o + 2 + l > n) break;
        bool keep = !(l >= 2 && ((p[o + 2] == PKT_SYNC && last[p[o + 3]] != idx) || (p[o + 2] == PKT_NPC && lastNpc[p[o + 3]] != idx)
                                 || (p[o + 2] == PKT_VEH_STATE && lastVeh[p[o + 3]] != idx)));
        if(keep) r.append((const char*)p + o, 2 + l);
        o += 2 + l; ++idx;
    }
    b.swap(r);
}

// 2.18.3: frames where only the newest one matters (position, NPC snapshot, car state). When
// the upload is busy they wait in the batch and get replaced by newer ones (CoalesceSyncs)
// instead of being queued behind each other - queued positions were what made the ping
// climb as soon as a second player joined on a slow mobile upload.
static inline bool MP_IsLatestOnly(uint8_t t) { return t == PKT_SYNC || t == PKT_NPC || t == PKT_VEH_STATE; }
// length of the part of a batch that must go now: everything up to and including the last
// frame that is NOT latest-only (chat, joins, leaves, shots, pongs...), so the order stays right
static size_t MustSendPrefix(const std::string& b)
{
    const uint8_t* p = (const uint8_t*)b.data(); size_t n = b.size(), o = 0, cut = 0;
    while(o + 2 <= n)
    {
        size_t l = ((size_t)p[o] << 8) | p[o + 1];
        if(o + 2 + l > n) { cut = n; break; }
        if(l < 1 || !MP_IsLatestOnly(p[o + 2])) cut = o + 2 + l;
        o += 2 + l;
    }
    return cut;
}

template<class F> static void ForFrames(const uint8_t* p, size_t n, F fn)
{
    size_t o = 0;
    while(o + 2 <= n)
    {
        size_t l = ((size_t)p[o] << 8) | p[o + 1]; o += 2;
        if(o + l > n) break;
        fn(p + o, (int)l);
        o += l;
    }
}

static void RelayIp(uint32_t idx, char* out)
{
    in_addr a; a.s_addr = htonl(RELAY_NET | (idx & 0xFFFF));
    inet_ntop(AF_INET, &a, out, 16);
}

static int NewUdp(uint32_t bindIpHost, uint16_t port)
{
    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(fd < 0) return -1;
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    int sb = MP_RELAY_SOCK_BUF;   // 2.14: small on purpose; a big buffer only stores old positions (= lag)
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sb, sizeof(sb));
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sb, sizeof(sb));
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_port = htons(port); a.sin_addr.s_addr = htonl(bindIpHost);
    if(bind(fd, (sockaddr*)&a, sizeof(a)) != 0) { close(fd); return -1; }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    return fd;
}

// ------------------------------------------------------------------------------------------
static void HandleListingIn(std::map<uint32_t, Listing>& list, uint32_t tok, const std::string& payload, bool retained)
{
    std::lock_guard<std::mutex> lk(s_M);
    if(payload.empty()) { list.erase(tok); return; }            // host closed it
    // "v1|port|players|max|region|proto|name"
    char buf[160]; MP_CopyStr(buf, payload.c_str(), sizeof(buf));
    char* f[7] = { buf };
    for(int i = 1; i < 7; ++i) { f[i] = f[i - 1] ? strchr(f[i - 1], '|') : nullptr; if(f[i]) *f[i]++ = 0; }
    if(!f[6] || strcmp(f[0], "v1")) return;
    Listing& l = list[tok];
    l.port = (uint16_t)atoi(f[1]);
    l.players = atoi(f[2]); l.maxPlayers = atoi(f[3]); l.region = atoi(f[4]);
    l.proto = (uint16_t)atoi(f[5]);
    { const char* ic = strchr(f[5], ':'); int v = ic ? atoi(ic + 1) : 0; l.icon = (uint8_t)((v >= 0 && v <= MP_ICON_MAX) ? v : 0);   // 2.24
      const char* at = ic ? strchr(ic + 1, '@') : nullptr; const char* c2 = ic ? strchr(ic + 1, ':') : nullptr;
      l.startUnix = (at && (!c2 || at < c2)) ? (uint32_t)strtoul(at + 1, nullptr, 10) : 0;   // 2.27
      { const char* bang = at ? strchr(at, '!') : nullptr; l.dedicated = bang && (!c2 || bang < c2) && bang[1] == 'd'; }   // 2.29
      { const char* td = at ? strchr(at, '~') : nullptr; int hr = (td && (!c2 || td < c2)) ? atoi(td + 1) : 0; l.hostRtt = (hr > 0 && hr < 5000) ? hr : 0; }   // 4.05.1
      const char* lg = c2; MP_CopyStr(l.lang, lg ? lg + 1 : "", sizeof(l.lang)); MP_CleanLang(l.lang); }   // 2.25
    MP_CopyStr(l.name, f[6], sizeof(l.name));
    if(!retained) l.confirmed = true;
    l.lastSeen = MP_NowMs();
    if(!s_RelayIdx.count(tok)) s_RelayIdx[tok] = s_nNextIdx++;
}
static void HandleListing(uint32_t tok, const std::string& payload, bool retained) { HandleListingIn(s_List, tok, payload, retained); }

// 2.20 (defined further down)
static void FetchRegions(bool apply, int tries);
static void ApplyPendingRegions();
static void PingThread();
static int  DesiredRegion(bool hostOn, int hostRegion);
static std::vector<int> ConnectOrder(int want);
static bool HaveRegion(int r);

static void NetThread()
{
#if defined(__ANDROID__) || defined(__linux__)
    // 2.17: the game thread runs flat out once avatars are on screen; give the network thread a
    // head start so packets are not left waiting in the socket (best effort, may be refused)
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), -8);
#endif

    Mqtt mq;
    std::map<std::string, Bridge> bridges;      // host side, key = player's client id
    std::string up;                              // client side batch
    uint32_t curHostToken = 0;                   // token our current MQTT session is set up for
    uint16_t curHostPort = 0;
    uint64_t lastList = 0, lastFlush = 0, lastPing = 0, pingSent = 0, lastRx = 0, nextTry = 0;
    std::string lastDet; uint64_t lastDetAt = 0;   // 4.03
    std::string lastPv; uint32_t lastPvTok = 0; uint64_t lastPvAt = 0;   // 4.04
    int baseRtt = -1, lastQueueMs = 0;           // 2.15: fastest broker round trip seen / queueing on top of it
    uint64_t lastBaseDecay = 0;
    uint64_t upHeld = 0;                         // 2.18.3: client side positions held since
    int minTcpRtt = -1; uint64_t lastTcpDecay = 0; uint32_t holds = 0;
    std::string shared; uint64_t sharedHeld = 0;  // 2.18.3: host: updates for (almost) everyone, published once
    uint32_t sharedSubTok = 0; uint64_t lastCap = 0;   // client: b/<token> we listen on, last "I listen" frame
    int lastWant = -1; uint64_t lastSwitch = 0;  // 2.20: region broker we aimed for / when
    int lastGood = -1, stickyFails = 0;          // 2.21: broker of the running game: reconnect there first

    // 2.20: regional brokers: download the list (only if none is saved yet; else in the
    // background), measure each region's ping, then connect to the best one
    s_bFetchedAtStart = !s_bRegional;
    if(s_bFetchedAtStart) FetchRegions(true, 1);
    s_bPinging = true;   // 2.23: set before the thread starts, else the wait below can miss the first round
    std::thread(PingThread).detach();
    if(s_bRegional) for(int w = 0; w < 120 && s_bPinging; ++w) std::this_thread::sleep_for(std::chrono::milliseconds(100));   // 2.23: parallel ping + MQTT probe, ~12 s worst case

    for(;;)
    {
        uint64_t now = MP_NowMs();

        // ---- 4.05.8: Wi-Fi <-> mobile data ----
        if(NetWatchTick())
        {
            s_BrokerBackoff.clear();          // what failed during the switch is not "down"
            stickyFails = 0;
            // 4.06: region pings stay as they are until the new round (2 s later) replaces them. 4.05.8 wiped
            // them, which made the lobby leave a working broker for the menu region and try dead ones.
            if(mq.fd >= 0 && !SocketNetUp(mq.fd))
            {
                OLOG("4.05.8: the broker connection was on the network that is gone - reconnecting now");
                mq.Close();
                s_bConnected = false;
            }
            nextTry = 0;
        }

        // ---- hosting state from the game ----
        bool hostOn; char hostName[32]; uint16_t hostPort; int hp, hm, hr;
        {
            std::lock_guard<std::mutex> lk(s_M);
            hostOn = s_bHostOn; MP_CopyStr(hostName, s_szHostName, sizeof(hostName));
            hostPort = s_nHostPort; hp = s_nHostPlayers; hm = s_nHostMax; hr = s_nHostRegion;
            for(int fd : s_CloseFds) close(fd);
            s_CloseFds.clear();
        }
        uint32_t wantToken = s_nHostToken;
        if(!hostOn) wantToken = 0;
        // 2.20: on the wrong region's broker (joined / hosting a server of another region, or the
        // right one was down): move. Retries a failed region once a minute, not every loop.
        int wantRegion = DesiredRegion(hostOn, hr);
        // 2.21: never move a running game to another broker just because a ping changed: players
        // and their host must stay together (this was the "kicked after a few minutes" timeout)
        bool inGame = !bridges.empty();
        { std::lock_guard<std::mutex> lk(s_M); if(s_ProxyToken && s_ProxyFd >= 0) inGame = true; }
        bool mustMove = (hostOn && HaveRegion(hr));
        { std::lock_guard<std::mutex> lk(s_M); if((s_nJoinRegion >= 0 && HaveRegion(s_nJoinRegion)) || s_bJoinPublic) mustMove = true; }
        if(inGame && !mustMove) wantRegion = -1;
        // 2.23: only move to a region that answers right now (ping thread). A dead region used to
        // disconnect the working public lobby every 60 s, then time out on all region brokers.
        if(wantRegion >= 0 && s_bPingedOnce && !RegionUp(wantRegion)) wantRegion = -1;
        if(mq.fd >= 0 && ((wantRegion >= 0 && wantRegion != s_nCurRegion) || (wantRegion == WANT_PUBLIC && s_nCurRegion >= 0)) && (wantRegion != lastWant || now - lastSwitch > 60000))
        {
            OLOG("switching lobby to %s (was %d)", wantRegion >= 0 ? g_MPRegionNames[wantRegion] : "the public broker", s_nCurRegion.load());
            if(curHostToken) { mq.Publish(TopicList(curHostToken), "", 0, true); mq.Publish(TopicDetails(curHostToken), "", 0, true); mq.Publish(TopicPreview(curHostToken), "", 0, true); }
            mq.Disconnect();
            s_bConnected = false;
            nextTry = 0;
        }
        if(wantToken != curHostToken || (hostOn && hostPort != curHostPort))
        {
            // hosting changed: deliver the last packets (e.g. "server closed"), remove the old
            // listing, then reconnect so the broker's last will matches the new server
            if(mq.fd >= 0)
            {
                if(curHostToken)
                {
                    for(auto& kv : bridges)
                    {
                        uint8_t buf[1500];
                        for(int g = 0; g < 64; ++g)
                        {
                            int r = (int)recv(kv.second.fd, buf, sizeof(buf), 0);
                            if(r <= 0) break;
                            AppendFrame(kv.second.down, buf, r);
                        }
                        if(!kv.second.down.empty())
                        {
                            std::string p(4, 0); p[0] = curHostToken >> 24; p[1] = curHostToken >> 16; p[2] = curHostToken >> 8; p[3] = curHostToken;
                            p += kv.second.down;
                            mq.Publish(TopicDown(kv.first.c_str()), p.data(), p.size(), false);
                        }
                    }
                    mq.Publish(TopicList(curHostToken), "", 0, true);
                    mq.Publish(TopicDetails(curHostToken), "", 0, true);   // 4.03
                    mq.Publish(TopicPreview(curHostToken), "", 0, true);   // 4.04
                }
                mq.Disconnect();
                s_bConnected = false;
            }
            for(auto& kv : bridges) close(kv.second.fd);
            bridges.clear();
            curHostToken = wantToken; curHostPort = hostPort;
            nextTry = 0;
        }

        // ---- connect / reconnect ----
        if(mq.fd < 0)
        {
            s_bConnected = false;
            if(now < nextTry) { std::this_thread::sleep_for(std::chrono::milliseconds(100)); continue; }
            // 4.05.8: no network at all (between Wi-Fi going and mobile data coming): wait for it instead of
            // walking every broker into a timeout and a back-off
            if(NoNetwork())
            {
                { std::lock_guard<std::mutex> lk(s_M); s_szBroker[0] = 0; }
                s_nCurRegion = -1;
                nextTry = now + 300;
                continue;
            }
            const bool netNew = NetChangedRecently(now, 30000);   // 4.05.8: just switched networks
            const uint32_t gen0 = s_nNetGen;
            bool ok = false;
            std::string will = curHostToken ? TopicList(curHostToken) : std::string();
            ApplyPendingRegions();
            lastWant = DesiredRegion(hostOn, hr); lastSwitch = now;
            std::vector<int> order = ConnectOrder(lastWant);   // same order for everyone who wants the same region
            {   // 2.23: brokers that just failed (or whose region is measured down) go to the end
                std::vector<int> live, later;
                for(int x : order)
                {
                    const Broker& b = s_Brokers[x];
                    bool deadRegion = b.region >= 0 && s_bPingedOnce && !RegionUp(b.region) && x != s_nBrokerOverride;
                    bool skip = BrokerSkipped(b, now) && !(b.region >= 0 && RegionUp(b.region));   // region answers again -> try it
                    (skip || deadRegion ? later : live).push_back(x);
                }
                if(live.empty()) live.swap(later); else live.insert(live.end(), later.begin(), later.end());
                order.swap(live);
            }
            // 2.21: a game is running (players on our server / we play on someone's server): the
            // others are still on the broker we just lost. Retry that one first a few times instead of
            // landing on another broker where nobody can hear us (= timeout for everyone)
            if(inGame && lastGood >= 0 && lastGood < BROKER_COUNT && (stickyFails < 4 || netNew) && !(lastWant == WANT_PUBLIC && s_Brokers[lastGood].region >= 0))   // 4.05.8: + right after a network switch (4.06: not when moving to the public lobby)
            {
                std::vector<int> o2; o2.push_back(lastGood);
                for(int x : order) if(x != lastGood) o2.push_back(x);
                order.swap(o2);
                order.resize(1);
            }
            for(size_t k = 0; k < order.size() && !ok; ++k)
            {
                int bi = order[k];
                uint64_t t0 = MP_NowMs();
                const Broker& br = s_Brokers[bi];
                bool opened = mq.Open(br, s_szCid, will.c_str(), br.tls ? 8000 : 4000);
                {
                    std::lock_guard<std::mutex> lk(s_M);
                    char d[200];
                    if(opened) snprintf(d, sizeof(d), "%s:%u%s  OK (%d ms)", br.host.c_str(), br.port, br.tls ? " tls" : "", (int)(MP_NowMs() - t0));
                    else       snprintf(d, sizeof(d), "%s:%u%s  %s", br.host.c_str(), br.port, br.tls ? " tls" : "", mq.err);
                    s_Diag[bi] = d;
                }
                if(!opened)
                {
                    OLOG("%s:%u failed: %s", br.host.c_str(), br.port, mq.err);
                    if(!netNew) BrokerFailed(br, MP_NowMs());   // 4.05.8: no back-off for a failure during a network switch
                    if(NetWatchTick() || s_nNetGen != gen0 || NoNetwork()) { s_BrokerBackoff.clear(); stickyFails = 0; break; }   // network changed again: start over
                }
                if(opened)
                {
                    ok = true;
                    BrokerOk(br);
                    lastGood = bi; stickyFails = 0;
                    // 3.11: our own regional HiveMQ brokers have no message limit: batch for 10 ms, not 25
                    s_nCurFlush = br.flushMs > 0 ? br.flushMs : (br.region >= 0 ? (s_nFlushMs < 10 ? s_nFlushMs : 10) : s_nFlushMs);
                    s_bOwnBroker = br.flushMs <= 0 && br.region >= 0;   // 3.33
                    mq.blockingSend = false;   // 2.14: from now on the relay never waits for the upload
                    std::string down = TopicDown(s_szCid);
                    mq.Subscribe((TOPIC_ROOT + "l/+").c_str(), 1);
                    mq.Subscribe((TOPIC_ROOT + "d/+").c_str(), 1);   // 4.03
                    mq.Subscribe((TOPIC_ROOT + "p/+").c_str(), 1);   // 4.04: preview pictures
                    lastDet.clear(); lastPv.clear(); lastPvTok = 0;
                    mq.Subscribe(down.c_str(), 2);
                    sharedSubTok = 0; lastCap = 0;   // 2.18.3: new session -> subscribe to b/<token> again
                    if(curHostToken)
                    {
                        char h[9]; Hex8(h, curHostToken);
                        std::string f = (TOPIC_ROOT + "s/") + h + "/+";
                        mq.Subscribe(f.c_str(), 3);
                    }
                    { std::lock_guard<std::mutex> lk(s_M); MP_CopyStr(s_szBroker, br.label.c_str(), sizeof(s_szBroker)); }
                    s_nCurRegion = br.region;
                    OLOG("connected to %s:%u%s (client %s, hosting %08x)", br.host.c_str(), br.port, br.tls ? " tls" : "", s_szCid, curHostToken);
                }
            }
            now = MP_NowMs();
            if(!ok)
            {
                ++s_nAttemptFailed;
                s_nReadySince = 0;
                { std::lock_guard<std::mutex> lk(s_M); s_szBroker[0] = 0; }
                s_nCurRegion = -1; s_bOwnBroker = false;
                nextTry = now + 5000;
                if(s_nNetGen != gen0 || netNew) { nextTry = MP_NowMs() + 500; OLOG("4.05.8: network just changed, retrying in 0.5 s"); continue; }
                if(inGame && lastGood >= 0 && stickyFails < 4) { ++stickyFails; nextTry = MP_NowMs() + 600; OLOG("game broker not back yet (%d), retrying", stickyFails); continue; }
                OLOG("no broker reachable, retrying in 5 s");
                continue;
            }
            s_nAttemptFailed = 0;
            s_bConnected = true;
            if(!s_nReadySince) s_nReadySince = now;
            lastRx = now; lastPing = now; pingSent = 0; lastList = 0;
            baseRtt = -1; lastQueueMs = 0;
        }

        // ---- wait for data on the broker, the proxy and the bridges ----
        int proxyFd;
        { std::lock_guard<std::mutex> lk(s_M); proxyFd = s_ProxyFd; }
        // poll(), not select(): the game has many files open and fds >= 1024 would break FD_SET
        std::vector<pollfd> pfs;
        bool sendFail = !mq.FlushOut(0);
        pfs.push_back({ mq.fd, (short)(POLLIN | (mq.Pending() ? POLLOUT : 0)), 0 });
        if(proxyFd >= 0) pfs.push_back({ proxyFd, POLLIN, 0 });
        for(auto& kv : bridges) pfs.push_back({ kv.second.fd, POLLIN, 0 });
        poll(pfs.data(), (nfds_t)pfs.size(), 10);
        now = MP_NowMs();

        // broker
        bool broken = sendFail;
        for(int g = 0; g < 512; ++g)
        {
            uint8_t type; std::string body;
            int r = mq.ReadPacket(type, body, 0);
            if(r < 0) { broken = true; break; }
            if(r == 0) break;
            lastRx = now;
            uint8_t kind = type >> 4;
            if(kind == 13)
            {
                if(pingSent)
                {
                    int rtt = (int)(now - pingSent);
                    { static int lastR = -1; static float j = 0; if(lastR >= 0) { j += ((float)abs(rtt - lastR) - j) / 8.0f; s_nRttJitter = (int)(j + 0.5f); } lastR = rtt; }   // 4.06
                    s_nRtt = rtt; pingSent = 0;
                    if(baseRtt < 0 || rtt < baseRtt) baseRtt = rtt;
                    lastQueueMs = rtt - baseRtt; if(lastQueueMs < 0) lastQueueMs = 0;
                }
                continue;
            }
            if(kind != 3 || body.size() < 2) continue;
            bool retained = type & 1;
            int qos = (type >> 1) & 3;
            size_t tl = ((size_t)(uint8_t)body[0] << 8) | (uint8_t)body[1];
            if(2 + tl > body.size()) continue;
            std::string topic = body.substr(2, tl);
            size_t off = 2 + tl + (qos ? 2 : 0);
            if(off > body.size()) continue;
            const uint8_t* pl = (const uint8_t*)body.data() + off;
            size_t pn = body.size() - off;
            const size_t rootLen = TOPIC_ROOT.size();
            if(topic.compare(0, rootLen, TOPIC_ROOT)) continue;
            const char* t = topic.c_str() + rootLen;

            if(!strncmp(t, "l/", 2) && strlen(t) == 10)
            {
                uint32_t tok = ParseHex8(t + 2);
                if(tok && tok != curHostToken) HandleListing(tok, std::string((const char*)pl, pn), retained);
            }
            else if(!strncmp(t, "d/", 2) && strlen(t) == 10)   // 4.03: details of a listed server
            {
                uint32_t tok = ParseHex8(t + 2);
                std::lock_guard<std::mutex> lk(s_DetMx);
                if(!tok) {}
                else if(!pn) s_Details.erase(tok);
                else if(pn < 4096) s_Details[tok] = { std::string((const char*)pl, pn), now };
            }
            else if(!strncmp(t, "p/", 2) && strlen(t) == 10)   // 4.04: preview picture of a listed server
            {
                uint32_t tok = ParseHex8(t + 2);
                std::lock_guard<std::mutex> lk(s_DetMx);
                if(!tok) {}
                else if(pn < 4 || pl[0] != 0xFF || pl[1] != 0xD8) s_Previews.erase(tok);
                else if(pn <= 200 * 1024 && (s_Previews.size() < 300 || s_Previews.count(tok))) s_Previews[tok] = std::string((const char*)pl, pn);
            }
            else if(!strncmp(t, "c/", 2) && pn >= 4)              // to us, from a host
            {
                uint32_t tok = ((uint32_t)pl[0] << 24) | ((uint32_t)pl[1] << 16) | ((uint32_t)pl[2] << 8) | pl[3];
                std::lock_guard<std::mutex> lk(s_M);
                if(tok == s_ProxyToken && s_ProxyFd >= 0 && s_bProxyPeer)
                {
                    int fd = s_ProxyFd; sockaddr_in peer = s_ProxyPeer;
                    ForFrames(pl + 4, pn - 4, [&](const uint8_t* d, int n) { sendto(fd, d, n, 0, (sockaddr*)&peer, sizeof(peer)); });
                }
            }
            else if(!strncmp(t, "b/", 2) && strlen(t) == 10 && pn >= 4)   // 2.18.3: shared updates from our host
            {
                uint32_t tok = ((uint32_t)pl[0] << 24) | ((uint32_t)pl[1] << 16) | ((uint32_t)pl[2] << 8) | pl[3];
                std::lock_guard<std::mutex> lk(s_M);
                if(tok == s_ProxyToken && ParseHex8(t + 2) == tok && s_ProxyFd >= 0 && s_bProxyPeer)
                {
                    int fd = s_ProxyFd; sockaddr_in peer = s_ProxyPeer;
                    ForFrames(pl + 4, pn - 4, [&](const uint8_t* d, int n) { sendto(fd, d, n, 0, (sockaddr*)&peer, sizeof(peer)); });
                }
            }
            else if(!strncmp(t, "s/", 2) && curHostToken && strlen(t) > 11)  // to our server, from a player
            {
                if(ParseHex8(t + 2) != curHostToken) continue;
                std::string cid = t + 11;
                if(cid.empty() || cid.size() > 23) continue;
                auto it = bridges.find(cid);
                if(it == bridges.end())
                {
                    if((int)bridges.size() >= MAX_BRIDGES) continue;
                    int fd = NewUdp(0x7F000001u, 0);
                    if(fd < 0) continue;
                    Bridge b; b.fd = fd; b.lastSeen = now;
                    it = bridges.emplace(cid, b).first;
                }
                it->second.lastSeen = now;
                sockaddr_in srv; memset(&srv, 0, sizeof(srv));
                srv.sin_family = AF_INET; srv.sin_port = htons(curHostPort); srv.sin_addr.s_addr = htonl(0x7F000001u);
                int fd = it->second.fd; Bridge& br = it->second;
                ForFrames(pl, pn, [&](const uint8_t* d, int n) {
                    if(n >= 1 && d[0] == MP_RELAY_CAP) { br.shared = true; return; }   // 2.18.3: relay only
                    sendto(fd, d, n, 0, (sockaddr*)&srv, sizeof(srv));
                });
            }
        }

        // proxy: packets from our MPClient -> host
        uint32_t proxyTok = 0;
        std::string upUrg;
        {
            std::lock_guard<std::mutex> lk(s_M);
            proxyTok = s_ProxyToken;
            if(s_ProxyFd >= 0)
            {
                uint8_t buf[1500];
                for(int g = 0; g < 512; ++g)
                {
                    sockaddr_in from; socklen_t fl = sizeof(from);
                    int r = (int)recvfrom(s_ProxyFd, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
                    if(r <= 0) break;
                    s_ProxyPeer = from; s_bProxyPeer = true;
                    if(MP_IsUrgentFrame(buf, r)) AppendFrame(upUrg, buf, r);
                    else AppendFrame(up, buf, r);
                }
            }
        }
        // bridges: packets from our MPServer -> remote players
        for(auto& kv : bridges)
        {
            uint8_t buf[1500];
            for(int g = 0; g < 512; ++g)
            {
                int r = (int)recv(kv.second.fd, buf, sizeof(buf), 0);
                if(r <= 0) break;
                if(MP_IsUrgentFrame(buf, r)) AppendFrame(kv.second.urg, buf, r);
                else AppendFrame(kv.second.down, buf, r);
            }
        }

        // 2.18.3: client: listen on our host's shared updates and tell its relay so
        if(proxyTok != sharedSubTok)
        {
            if(sharedSubTok) mq.Unsubscribe(TopicShared(sharedSubTok).c_str(), 5);
            if(proxyTok) mq.Subscribe(TopicShared(proxyTok).c_str(), 4);
            sharedSubTok = proxyTok; lastCap = 0;
        }
        if(proxyTok && (!lastCap || now - lastCap >= 3000))
        {
            lastCap = now;
            uint8_t cap[2] = { MP_RELAY_CAP, 1 };
            AppendFrame(up, cap, 2);
        }

        // ---- flush batches ----
        // 2.11: with many relayed players, batch longer so the host stays under the broker's
        // message rate (1 MQTT message per player per flush): 25 ms up to 8, 50 ms up to 32, 80 ms beyond.
        // 3.33: that rule is only for brokers that limit messages (the free public ones). On our own regional
        // HiveMQ brokers it made the ping jump every time somebody joined: the 9th relayed player moved the
        // whole server from a 10 ms to a 50 ms window and the 33rd to 80 ms - that is added in BOTH directions
        // (+80 ms, then +140 ms round trip). The congestion control below already slows the batches down when
        // the uplink really is busy, so on our brokers the window stays short and grows only a little:
        //   24/7 machine (fast uplink): 10 ms up to 32 relayed players, 15 ms up to 64, 20 ms beyond
        //   a phone host (mobile uplink): 10 ms up to 8, 20 ms up to 32, 30 ms beyond
        int flushMs = s_nCurFlush;
        if(s_bOwnBroker)
        {
            const size_t nb = bridges.size();
            if(s_bHostDedicated) { if(nb > 64 && flushMs < 20) flushMs = 20; else if(nb > 32 && flushMs < 15) flushMs = 15; }
            else                 { if(nb > 32 && flushMs < 30) flushMs = 30; else if(nb > 8 && flushMs < 20) flushMs = 20; }
        }
        else if(bridges.size() > 32 && flushMs < 80) flushMs = 80;
        else if(bridges.size() > 8 && flushMs < 50) flushMs = 50;
        // 2.15 congestion control: when the uplink (or the broker's rate limit) can't keep up,
        // send fewer, bigger messages; slowly go back once it is fine again
        {
            static uint32_t lastDropped = 0, lastHolds = 0; static uint64_t lastAdj = 0, lastCut = 0, lastSent = 0; static int extra = 0;
            if(now - lastAdj >= 250)
            {
                uint64_t span = now - lastAdj; if(!lastAdj) span = 250;
                lastAdj = now;
                double sentBps = lastSent ? (double)(mq.sentTotal - lastSent) * 1000.0 / (double)span : 0; lastSent = mq.sentTotal;
                // queueing delay: from the last ping, or the one still waiting for its answer
                int qd = lastQueueMs;
                if(pingSent && baseRtt >= 0) { int w = (int)(now - pingSent) - baseRtt; if(w > qd) qd = w; }
                int app = mq.QueueAgeMs();
                // 2.17 ping fix: only OUR OWN backlog proves our uplink is too slow. Before, a slow
                // or jittery public broker (100-300 ms is normal there) also counted, and the cut
                // used what we happened to send as the "capacity": with 2 players (~3 KB/s) one
                // spike throttled the host to 1.5 KB/s, positions got dropped and every batch waited
                // up to 150 ms longer -> ping and lag went up as soon as a second player joined and
                // came back to normal when they left. Broker delay alone now only cuts the rate
                // when we really send a lot, and never below what a full server needs.
                static int slowSamples = 0;
                bool ownBacklog = app > 120;
                slowSamples = (qd > 250) ? slowSamples + 1 : 0;
                bool netSlow = slowSamples >= 3 && sentBps > 48 * 1024;
                if(ownBacklog || netSlow)
                {
                    if(now - lastCut >= 500)   // multiplicative decrease, at most twice a second
                    {
                        lastCut = now;
                        double r = mq.rateBps; if(ownBacklog && sentBps > 0 && sentBps < r) r = sentBps;
                        r *= 0.75;
                        double floorBps = ownBacklog ? 6 * 1024 : 32 * 1024;
                        if(r < floorBps) r = floorBps;
                        mq.rateBps = r;
                    }
                }
                else if(app < 40 && qd < 200)
                {
                    double inc = mq.rateBps * 0.08; if(inc < 2048) inc = 2048;
                    mq.rateBps += inc; if(mq.rateBps > 2.0 * 1024 * 1024) mq.rateBps = 2.0 * 1024 * 1024;
                }
                // 2.18.3: held positions also mean "send less" (the game lowers its sync rate)
                bool congested = mq.dropped != lastDropped || ownBacklog || holds - lastHolds > 2;
                lastHolds = holds;
                lastDropped = mq.dropped;
                // 2.18.3: at most 40 ms (was 100): positions are merged while busy, a longer wait only added ping
                if(congested) extra = extra < 15 ? 15 : (extra * 3 / 2 > 40 ? 40 : extra * 3 / 2);
                else if(extra > 0) extra -= 10;
                if(extra < 0) extra = 0;
                s_nCongestMs = extra;
#ifndef __ANDROID__
                static uint64_t lastDbg = 0;
                if(getenv("MP_RELAY_DEBUG") && now - lastDbg >= 2000) { lastDbg = now; printf("[relay] rtt %d base %d queue %d ms tcp %d/%d unsent %d app %d ms sent %.1f KB/s dropped %u holds %u extra %d ms\n", (int)s_nRtt, baseRtt, qd, mq.TcpRttMs(), minTcpRtt, mq.KernelUnsent(), app, sentBps / 1024, mq.dropped, holds, extra); fflush(stdout); }
#endif
            }
            flushMs += extra;
        }
        // ---- 2.18.3: is our upload busy? ----
        // Our own queue, the kernel's unsent bytes, or the TCP round trip clearly above the
        // fastest one seen (= a queue in the phone / carrier upload buffer). While busy,
        // positions are held and replaced by newer ones instead of queuing up behind each other.
        int unsent = mq.KernelUnsent();
        int tcpRtt = mq.TcpRttMs();
        if(tcpRtt > 0 && (minTcpRtt < 0 || tcpRtt < minTcpRtt)) minTcpRtt = tcpRtt;
        if(now - lastTcpDecay > 10000) { lastTcpDecay = now; if(minTcpRtt >= 0) minTcpRtt += 5; }   // routes change
        int tcpQueue = (tcpRtt > 0 && minTcpRtt >= 0) ? tcpRtt - minTcpRtt : 0;
        bool busy = mq.Pending() > 0 || unsent > 3000 || tcpQueue > 150;
        // send one batch: everything if the upload is free; if busy only what must go now
        // (chat, joins, shots, pongs... with the positions before them), the rest waits and is
        // merged with newer positions. Never held longer than MAX_HOLD_MS (keeps ~4 updates/s).
        const uint64_t MAX_HOLD_MS = 250;
        bool queueYoung = mq.QueueAgeMs() < 100 && unsent < 3000;   // can take a held update now
        auto sendBatch = [&](std::string& buf, uint64_t& heldSince, const std::string& topic, bool withToken) {
            if(buf.empty()) { heldSince = 0; return; }
            CoalesceSyncs(buf);
            size_t cut = buf.size();
            if(busy && !(heldSince && now - heldSince >= MAX_HOLD_MS && queueYoung)) cut = MustSendPrefix(buf);
            if(cut)
            {
                std::string p;
                if(withToken) { p.assign(4, 0); p[0] = (char)(curHostToken >> 24); p[1] = (char)(curHostToken >> 16); p[2] = (char)(curHostToken >> 8); p[3] = (char)curHostToken; }
                p.append(buf, 0, cut);
                if(!mq.Publish(topic, p.data(), p.size(), false, false)) broken = true;
                buf.erase(0, cut);
            }
            if(buf.empty()) heldSince = 0;
            else { if(!heldSince) heldSince = now; ++holds; }
        };

        // 2.17 / 2.18.3: ping / pong / damage frames go out now, on their own, ahead of anything queued
        if(!upUrg.empty() && proxyTok)
            if(!mq.Publish(TopicUp(proxyTok, s_szCid), upUrg.data(), upUrg.size(), false, false, true)) broken = true;
        for(auto& kv : bridges)
        {
            Bridge& b = kv.second;
            if(b.urg.empty()) continue;
            std::string p(4, 0);
            p[0] = (char)(curHostToken >> 24); p[1] = (char)(curHostToken >> 16); p[2] = (char)(curHostToken >> 8); p[3] = (char)curHostToken;
            p += b.urg; b.urg.clear();
            if(!mq.Publish(TopicDown(kv.first.c_str()), p.data(), p.size(), false, false, true)) broken = true;
        }
        if(now - lastFlush >= (uint64_t)flushMs || up.size() > 4096)
        {
            lastFlush = now;
            if(proxyTok) sendBatch(up, upHeld, TopicUp(proxyTok, s_szCid), false);
            else { up.clear(); upHeld = 0; }
            // 2.18.3 shared updates: a position the server sends to most players (>= 60%, at
            // least 3) goes out once on b/<token>; players it wasn't meant for just get an extra
            // update (the game skips their own / unknown players and older ones by seq).
            if(curHostToken)
            {
                int nShared = 0;
                for(auto& kv : bridges) if(kv.second.shared) ++nShared;
                if(nShared >= 3)
                {
                    struct Cnt { int n; size_t order; };
                    std::unordered_map<std::string, Cnt> cnt;
                    size_t order = 0;
                    for(auto& kv : bridges)
                    {
                        if(!kv.second.shared || kv.second.down.empty()) continue;
                        CoalesceSyncs(kv.second.down);
                        const std::string& d = kv.second.down;
                        ForFrames((const uint8_t*)d.data(), d.size(), [&](const uint8_t* f, int n) {
                            if(n < 2 || f[0] != PKT_SYNC) return;
                            auto r = cnt.emplace(std::string((const char*)f, n), Cnt{ 0, order++ });
                            ++r.first->second.n;
                        });
                    }
                    int need = (nShared * 6 + 9) / 10; if(need < 3) need = 3;
                    std::vector<std::pair<size_t, const std::string*>> pick;
                    for(auto& c : cnt) if(c.second.n >= need) pick.push_back({ c.second.order, &c.first });
                    if(!pick.empty())
                    {
                        std::sort(pick.begin(), pick.end());
                        std::unordered_set<std::string> picked;
                        for(auto& pr : pick) { AppendFrame(shared, (const uint8_t*)pr.second->data(), (int)pr.second->size()); picked.insert(*pr.second); }
                        for(auto& kv : bridges)
                        {
                            if(!kv.second.shared || kv.second.down.empty()) continue;
                            std::string keep; const std::string& d = kv.second.down;
                            ForFrames((const uint8_t*)d.data(), d.size(), [&](const uint8_t* f, int n) {
                                if(!picked.count(std::string((const char*)f, n))) AppendFrame(keep, f, n);
                            });
                            kv.second.down.swap(keep);
                        }
                    }
                }
                sendBatch(shared, sharedHeld, TopicShared(curHostToken), true);
            }
            else { shared.clear(); sharedHeld = 0; }
            for(auto it = bridges.begin(); it != bridges.end();)
            {
                Bridge& b = it->second;
                sendBatch(b.down, b.heldSince, TopicDown(it->first.c_str()), true);
                if(now - b.lastSeen > BRIDGE_IDLE_MS) { close(b.fd); it = bridges.erase(it); }
                else ++it;
            }
        }

        // ---- announce our server ----
        if(curHostToken && (now - lastList >= LIST_EVERY_MS || !lastList))
        {
            lastList = now;
            char p[160];
            int ic = s_nHostIcon.load();   // 2.24: icon rides behind the protocol number ("4:17"), invisible to old phones
            char lg[MP_LANG_LEN]; { std::lock_guard<std::mutex> lk2(s_LangMx); MP_CopyStr(lg, s_szHostLang, sizeof(lg)); }
            uint32_t st = s_nHostStart.load();
            // 4.05.1: + "~rtt" = this host's own broker round trip, so a player's list shows the real
            // phone -> broker -> host -> back time (his ping + ours) instead of guessing ours = his
            char rt[12] = ""; { int r = s_nRtt; if(r > 0 && r < 5000) snprintf(rt, sizeof(rt), "~%d", r); }
            if(st)     snprintf(p, sizeof(p), "v1|%u|%d|%d|%d|%d:%d@%u%s%s:%s|%s", hostPort, hp, hm, hr, MP_PROTO_VERSION, ic, st, s_bHostDedicated ? "!d" : "", rt, lg, hostName);   // 2.29: + !d = 24/7   // 2.27: + start time
            else if(lg[0])  snprintf(p, sizeof(p), "v1|%u|%d|%d|%d|%d:%d:%s|%s", hostPort, hp, hm, hr, MP_PROTO_VERSION, ic, lg, hostName);   // 2.25: + language
            else if(ic > 0) snprintf(p, sizeof(p), "v1|%u|%d|%d|%d|%d:%d|%s", hostPort, hp, hm, hr, MP_PROTO_VERSION, ic, hostName);
            else       snprintf(p, sizeof(p), "v1|%u|%d|%d|%d|%d|%s", hostPort, hp, hm, hr, MP_PROTO_VERSION, hostName);
            if(!mq.Publish(TopicList(curHostToken), p, strlen(p), true)) broken = true;
        }
        // 4.03: country / tags / description / players on d/<token>, only when it changed
        if(curHostToken)
        {
            std::string det; { std::lock_guard<std::mutex> lk(s_DetMx); det = s_HostDetails; }
            if(det != lastDet && now - lastDetAt >= 3000)
            {
                lastDet = det; lastDetAt = now;
                if(!mq.Publish(TopicDetails(curHostToken), det.data(), det.size(), true)) broken = true;
            }
            std::string pv; { std::lock_guard<std::mutex> lk(s_DetMx); pv = s_HostPreview; }
            if((pv != lastPv || lastPvTok != curHostToken) && now - lastPvAt >= 5000)   // 4.04
            {
                lastPv = pv; lastPvTok = curHostToken; lastPvAt = now;
                if(!mq.Publish(TopicPreview(curHostToken), pv.data(), pv.size(), true)) broken = true;
            }
        }

        // ---- keepalive ----
        // 2.15: while relaying, ping twice a second: the ping's round trip is our congestion signal
        {
            bool relaying = !bridges.empty() || proxyTok;
            uint64_t every = relaying ? 500 : 5000;
            if(now - lastPing >= every && (!pingSent || now - pingSent > 5000))
            { lastPing = now; pingSent = now; if(!mq.Ping()) broken = true; }
            if(now - lastBaseDecay > 10000) { lastBaseDecay = now; if(baseRtt >= 0) baseRtt += 10; }   // routes change
        }
        if(now - lastRx > (uint64_t)KEEPALIVE_S * 1000 + 5000) broken = true;
        // 2.21: while relaying we ping every 0.5 s; 7 s of silence = the socket died (mobile NAT,
        // broker restart). Reconnect now instead of after 20 s, well inside the game's timeout
        if((!bridges.empty() || proxyTok) && pingSent && now - lastRx > 7000) broken = true;

        if(broken)
        {
            OLOG("broker connection lost");
            mq.Close();
            s_bConnected = false;
            nextTry = MP_NowMs() + ((!bridges.empty() || proxyTok) ? 250 : 1000);
        }
    }
}

// ---- TLS -----------------------------------------------------------------------------------
static std::mutex s_TlsM;
static bool s_bTlsInit = false, s_bTlsOk = false;
static mbedtls_entropy_context  s_Entropy;
static mbedtls_ctr_drbg_context s_Drbg;
static mbedtls_x509_crt         s_Ca;
static int                      s_nCaCount = 0;
static std::string              s_CaExtraLoaded;

static bool TlsGlobalInit(const std::string& caFile)
{
    std::lock_guard<std::mutex> lk(s_TlsM);
    if(!s_bTlsInit)
    {
        s_bTlsInit = true;
#if defined(MBEDTLS_USE_PSA_CRYPTO) || defined(MBEDTLS_SSL_PROTO_TLS1_3)
        psa_crypto_init();
#endif
        mbedtls_entropy_init(&s_Entropy);
        mbedtls_ctr_drbg_init(&s_Drbg);
        mbedtls_x509_crt_init(&s_Ca);
        const char* pers = "thirtyfox-mp";
        if(mbedtls_ctr_drbg_seed(&s_Drbg, mbedtls_entropy_func, &s_Entropy, (const unsigned char*)pers, strlen(pers)) != 0) return false;
        if(mbedtls_x509_crt_parse(&s_Ca, (const unsigned char*)kMPCaRoots, sizeof(kMPCaRoots)) >= 0) ++s_nCaCount;
#ifdef __ANDROID__
        if(mbedtls_x509_crt_parse_path(&s_Ca, "/system/etc/security/cacerts") >= 0) ++s_nCaCount;
        if(mbedtls_x509_crt_parse_path(&s_Ca, "/apex/com.android.conscrypt/cacerts") >= 0) ++s_nCaCount;
#else
        mbedtls_x509_crt_parse_file(&s_Ca, "/etc/ssl/certs/ca-certificates.crt");
#endif
        s_bTlsOk = true;
    }
    if(s_bTlsOk && !caFile.empty() && s_CaExtraLoaded.find("|" + caFile + "|") == std::string::npos)
    {
        int r = mbedtls_x509_crt_parse_file(&s_Ca, caFile.c_str());
        OLOG("extra CA %s: %d", caFile.c_str(), r);
        s_CaExtraLoaded += "|" + caFile + "|";
    }
    return s_bTlsOk;
}

bool Mqtt::TlsHandshake(const Broker& b, int timeoutMs)
{
    if(!TlsGlobalInit(b.caFile)) { snprintf(err, sizeof(err), "TLS init failed"); return false; }
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    tlsReady = true;   // Close() frees from here on
    if(mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    { snprintf(err, sizeof(err), "TLS config failed"); return false; }
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &s_Ca, nullptr);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &s_Drbg);
    mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);   // TLS 1.2: widely supported, no PSA ticket quirks
    if(mbedtls_ssl_setup(&ssl, &conf) != 0) { snprintf(err, sizeof(err), "TLS setup failed"); return false; }
    mbedtls_ssl_set_hostname(&ssl, b.host.c_str());   // SNI (HiveMQ Cloud needs it) + name check
    mbedtls_ssl_set_bio(&ssl, (void*)(intptr_t)fd, BioSend, BioRecv, nullptr);
    uint64_t until = MP_NowMs() + (uint64_t)timeoutMs;
    for(;;)
    {
        int r = mbedtls_ssl_handshake(&ssl);
        if(r == 0) break;
        if(r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            uint32_t v = mbedtls_ssl_get_verify_result(&ssl);
            if(r == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED && v)
            {
                if(v & MBEDTLS_X509_BADCERT_CN_MISMATCH)      snprintf(err, sizeof(err), "TLS: certificate name mismatch");
                else if(v & MBEDTLS_X509_BADCERT_EXPIRED)     snprintf(err, sizeof(err), "TLS: certificate expired (check phone date)");
                else if(v & MBEDTLS_X509_BADCERT_FUTURE)      snprintf(err, sizeof(err), "TLS: certificate not valid yet (check phone date)");
                else if(v & MBEDTLS_X509_BADCERT_NOT_TRUSTED) snprintf(err, sizeof(err), "TLS: certificate not trusted (add ca= in broker.txt)");
                else                                          snprintf(err, sizeof(err), "TLS: certificate rejected (0x%x)", v);
            }
            else
            {
                char e[96]; mbedtls_strerror(r, e, sizeof(e));
                snprintf(err, sizeof(err), "TLS failed: %s", e);
            }
            return false;
        }
        int left = (int)((int64_t)until - (int64_t)MP_NowMs());
        if(left <= 0) { snprintf(err, sizeof(err), "TLS handshake timeout"); return false; }
        pollfd p = { fd, (short)(r == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT), 0 };
        poll(&p, 1, left < 200 ? left : 200);
    }
    return true;
}

// ---- broker.txt ------------------------------------------------------------------------------
static std::string Trim(const std::string& x)
{
    size_t a = 0, b = x.size();
    while(a < b && (unsigned char)x[a] <= ' ') ++a;
    while(b > a && (unsigned char)x[b - 1] <= ' ') --b;
    return x.substr(a, b - a);
}

// Format (one key=value per line, # comments):
//   host=xxxxxxxx.s1.eu.hivemq.cloud   port=8883   tls=1   user=...   pass=...
//   ws=0  path=/mqtt  ca=/path/extra_root.pem  public=1 (also try the free public brokers if yours is down)
// 2.20: regional brokers - one block per region (same keys), e.g.
//   [Asia]
//   host=asia.example.com  port=1883  tls=0  user=mp  pass=...
//   [Europe]
//   host=eu.example.com ...
// Lines before the first [Region] are the single private broker (old format, still works).
//   regions_url=https://.../regions.txt   (where the region list is downloaded from; "off" = never)
static bool CfgBool(const std::string& v) { return atoi(v.c_str()) != 0 || v == "yes" || v == "true" || v == "on"; }
static int RegionFromName(std::string v)
{
    for(auto& c : v) c = (char)tolower((unsigned char)c);
    for(int r = 0; r < REGION_COUNT; ++r) { std::string n = g_MPRegionNames[r]; for(auto& c : n) c = (char)tolower((unsigned char)c); if(v == n) return r; }
    if(v == "as" || v == "asia-pacific" || v == "apac" || v == "sea") return REGION_ASIA;
    if(v == "eu") return REGION_EUROPE;
    if(v == "us" || v == "usa" || v == "north america" || v == "northamerica") return REGION_NA;
    if(v == "oc" || v == "au" || v == "australia" || v == "oce") return REGION_OCEANIA;
    return -1;
}

struct BrokerCfg
{
    std::vector<Broker> regional;
    Broker priv; bool havePriv = false;
    bool publicFallback = true, publicSet = false;
    std::string regionsUrl; bool regionsUrlSet = false;
};

static void ParseBrokerText(const std::string& text, BrokerCfg& pc)
{
    for(int r = 0; r < REGION_COUNT; ++r) s_RegionBranch[r] = false;   // 3.34
    Broker cur; cur.tls = true; cur.port = 8883; cur.path = "/mqtt";
    bool portSet = false; int section = -1;   // -1 = private broker, -2 = unknown [section] (ignored)
    auto flush = [&]()
    {
        if(!cur.host.empty() && section != -2)
        {
            if(!portSet) cur.port = cur.ws ? (cur.tls ? 8884 : 8000) : (cur.tls ? 8883 : 1883);
            cur.region = section;
            if(section >= 0) { cur.label = std::string(g_MPRegionNames[section]) + " " + cur.host; pc.regional.push_back(cur); }
            else { cur.label = "private " + cur.host; pc.priv = cur; pc.havePriv = true; }
        }
        cur = Broker(); cur.tls = true; cur.port = 8883; cur.path = "/mqtt"; portSet = false;
    };
    size_t pos = 0;
    while(pos < text.size())
    {
        size_t e = text.find('\n', pos); if(e == std::string::npos) e = text.size();
        std::string l = Trim(text.substr(pos, e - pos)); pos = e + 1;
        if(l.empty() || l[0] == '#' || l[0] == ';') continue;
        if(l[0] == '[')
        {
            flush();
            size_t c = l.find(']');
            int r = RegionFromName(Trim(l.substr(1, c == std::string::npos ? std::string::npos : c - 1)));
            section = r >= 0 ? r : -2;
            continue;
        }
        size_t eq = l.find('=');
        if(eq == std::string::npos) continue;
        std::string k = Trim(l.substr(0, eq)), v = Trim(l.substr(eq + 1));
        size_t hashC = v.find(" #"); if(hashC != std::string::npos) v = Trim(v.substr(0, hashC));   // trailing comment
        for(auto& c : k) c = (char)tolower((unsigned char)c);
        if(k == "region") { flush(); int r = RegionFromName(v); section = r >= 0 ? r : -2; }
        else if(k == "host" || k == "broker" || k == "url")
        {
            if(!cur.host.empty()) { int s = section; flush(); section = s; }   // a second host in one block = another broker of that region
            // accept "mqtts://host:port" or "host:port" too
            size_t sch = v.find("://");
            if(sch != std::string::npos)
            {
                std::string scheme = v.substr(0, sch);
                if(scheme == "mqtt" || scheme == "tcp") cur.tls = false;
                else if(scheme == "mqtts" || scheme == "ssl" || scheme == "tls") cur.tls = true;
                else if(scheme == "ws") { cur.ws = true; cur.tls = false; }
                else if(scheme == "wss") { cur.ws = true; cur.tls = true; }
                v = v.substr(sch + 3);
            }
            size_t sl = v.find('/'); if(sl != std::string::npos) { if(cur.ws) cur.path = v.substr(sl); v = v.substr(0, sl); }
            size_t co = v.rfind(':');
            if(co != std::string::npos) { cur.port = (uint16_t)atoi(v.c_str() + co + 1); portSet = true; v = v.substr(0, co); }
            cur.host = v;
        }
        else if(k == "port") { cur.port = (uint16_t)atoi(v.c_str()); portSet = true; }
        else if(k == "branch") { if(section >= 0 && section < REGION_COUNT) s_RegionBranch[section] = CfgBool(v); }   // 3.34
        else if(k == "user" || k == "username") cur.user = v;
        else if(k == "pass" || k == "password") cur.pass = v;
        else if(k == "tls" || k == "ssl") cur.tls = CfgBool(v);
        else if(k == "ws" || k == "websocket") cur.ws = CfgBool(v);
        else if(k == "path") cur.path = v;
        else if(k == "ca") cur.caFile = v;
        else if(k == "flush" || k == "flush_ms") { int x = atoi(v.c_str()); cur.flushMs = x < 10 ? 10 : x > 500 ? 500 : x; }
        else if(k == "rate" || k == "max_msgs_per_sec") { int x = atoi(v.c_str()); if(x > 0) cur.flushMs = (1000 + x - 1) / x + 5; }
        else if(k == "public") { pc.publicFallback = CfgBool(v); pc.publicSet = true; }
        else if(k == "regions_url" || k == "regions") { pc.regionsUrl = v; pc.regionsUrlSet = true; }
    }
    flush();
}

static bool ReadFileText(const std::string& path, std::string& out, size_t maxBytes = 65536)
{
    out.clear();
    FILE* f = path.empty() ? nullptr : fopen(path.c_str(), "rb");
    if(!f) return false;
    char buf[4096]; size_t n;
    while((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < maxBytes) out.append(buf, n);
    fclose(f);
    return true;
}

static BrokerCfg s_Cfg;   // broker.txt + built-in (set in Online::Start, then only the net thread)

// s_Brokers = regional brokers, then the private one, then the public ones (if allowed).
// The connect ORDER is decided per attempt (see ConnectOrder).
static void RebuildBrokers(const std::vector<Broker>& regional)
{
    std::lock_guard<std::mutex> lk(s_M);
    s_Brokers.clear();
    for(const auto& b : regional) s_Brokers.push_back(b);
    if(s_Cfg.havePriv) s_Brokers.push_back(s_Cfg.priv);
    if(s_Brokers.empty() || s_Cfg.publicFallback)
        for(const auto& b : kPublicBrokers) s_Brokers.push_back(b);
    s_bRegional = !regional.empty();
    s_Diag.assign(s_Brokers.size(), std::string());
}

static void LoadBrokerConfig(const char* path)
{
    s_Cfg = BrokerCfg();
    std::string note;
#if defined(__has_include)
#if __has_include("broker_private.h")
#include "broker_private.h"   // optional, git-ignored: #define MP_PRIV_HOST "...", MP_PRIV_USER, MP_PRIV_PASS [, MP_PRIV_PORT]
#endif
#endif
#ifdef MP_PRIV_HOST
    s_Cfg.priv.host = MP_PRIV_HOST; s_Cfg.priv.tls = true; s_Cfg.priv.port = 8883;
#ifdef MP_PRIV_PORT
    s_Cfg.priv.port = MP_PRIV_PORT;
#endif
#ifdef MP_PRIV_USER
    s_Cfg.priv.user = MP_PRIV_USER;
#endif
#ifdef MP_PRIV_PASS
    s_Cfg.priv.pass = MP_PRIV_PASS;
#endif
    s_Cfg.priv.label = "private " + s_Cfg.priv.host;
    s_Cfg.havePriv = true; note = "Private broker: built in";
#endif
    std::string text;
    if(path && *path && ReadFileText(path, text))
    {
        BrokerCfg fc; ParseBrokerText(text, fc);
        if(fc.havePriv) { s_Cfg.priv = fc.priv; s_Cfg.havePriv = true; note = std::string("Private broker from broker.txt") + (fc.priv.user.empty() ? " (no login!)" : ""); }
        s_Cfg.regional = fc.regional;
        if(fc.publicSet) s_Cfg.publicFallback = fc.publicFallback;
        if(fc.regionsUrlSet) { s_Cfg.regionsUrl = fc.regionsUrl; s_Cfg.regionsUrlSet = true; }
        if(!fc.regional.empty()) note += std::string(note.empty() ? "" : ", ") + std::to_string(fc.regional.size()) + " regional broker(s) from broker.txt";
        if(!fc.havePriv && fc.regional.empty() && !fc.regionsUrlSet && !fc.publicSet) note = "broker.txt found but has no host=";
    }
    if(s_Cfg.havePriv && !s_Cfg.publicFallback) note += ", public brokers off";
    // regions list from the website: only when broker.txt doesn't set regions itself
    if(s_Cfg.regionsUrlSet) s_RegionsUrl = s_Cfg.regionsUrl;
    if(!s_Cfg.regional.empty() || s_RegionsUrl == "off" || s_RegionsUrl == "0" || s_RegionsUrl.empty()) s_RegionsUrl.clear();
    std::vector<Broker> regional = s_Cfg.regional;
    if(regional.empty() && !s_RegionsUrl.empty() && ReadFileText(s_RegionsCache, text))
    {
        BrokerCfg rc; ParseBrokerText(text, rc);
        regional = rc.regional;
        if(!regional.empty()) { std::lock_guard<std::mutex> lk(s_M); s_RegionsNote = std::to_string(regional.size()) + " regional broker(s) (saved list)"; }
    }
    RebuildBrokers(regional);
    std::lock_guard<std::mutex> lk(s_M);
    s_ConfigNote = note;
}

// ---- 2.20: small HTTPS GET (regions.txt from the website) --------------------------------
static int TcpConnectTimed(const std::string& host, uint16_t port, int timeoutMs, int* rttMs, char* err, size_t errLen)
{
    std::vector<sockaddr_in> addrs;   // 4.05.8
    if(ResolveV4(host, port, addrs, nullptr, 0) != 0) { if(err) snprintf(err, errLen, "DNS lookup failed"); return -1; }
    int fd = -1;
    for(size_t ai = 0; ai < addrs.size() && fd < 0; ++ai)
    {
        int s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if(s < 0) continue;
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
        uint64_t t0 = MP_NowMs();
        int r = connect(s, (const sockaddr*)&addrs[ai], sizeof(sockaddr_in));
        if(r != 0 && errno == EINPROGRESS)
        {
            pollfd p = { s, POLLOUT, 0 };
            if(poll(&p, 1, timeoutMs) == 1) { int e = 0; socklen_t el = sizeof(e); getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &el); r = e ? -1 : 0; }
            else r = -1;
        }
        if(r == 0) { fd = s; if(rttMs) *rttMs = (int)(MP_NowMs() - t0); }
        else close(s);
    }
    if(fd < 0 && err) snprintf(err, errLen, "unreachable");
    return fd;
}

static thread_local std::atomic<size_t>* t_HttpsProg = nullptr;   // 3.62: bytes received so far (Day Z download bar)
static bool HttpsGet(const std::string& url, std::string& body, int timeoutMs, std::string& why, size_t maxBytes = 256 * 1024)
{
    body.clear();
    bool tls = true; std::string rest;
    if(!url.compare(0, 8, "https://")) rest = url.substr(8);
    else if(!url.compare(0, 7, "http://")) { rest = url.substr(7); tls = false; }
    else { why = "bad URL"; return false; }
    size_t sl = rest.find('/');
    std::string host = rest.substr(0, sl), path = sl == std::string::npos ? "/" : rest.substr(sl);
    uint16_t port = tls ? 443 : 80;
    size_t co = host.find(':'); if(co != std::string::npos) { port = (uint16_t)atoi(host.c_str() + co + 1); host = host.substr(0, co); }
    char e[64] = "";
    int fd = TcpConnectTimed(host, port, timeoutMs, nullptr, e, sizeof(e));
    if(fd < 0) { why = e; return false; }
    uint64_t until = MP_NowMs() + (uint64_t)timeoutMs;
    mbedtls_ssl_context ssl; mbedtls_ssl_config conf;
    bool tlsOn = false, ok = false;
    std::string raw;
    std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUser-Agent: ThirtyFoxMP\r\nAccept: */*\r\nConnection: close\r\n\r\n";
    auto waitFd = [&](short ev) -> bool { int left = (int)((int64_t)until - (int64_t)MP_NowMs()); if(left <= 0) return false; pollfd p = { fd, ev, 0 }; poll(&p, 1, left < 200 ? left : 200); return true; };
    do
    {
        if(tls)
        {
            if(!TlsGlobalInit("")) { why = "TLS init failed"; break; }
            mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&conf); tlsOn = true;
            if(mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0) { why = "TLS config failed"; break; }
            mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            mbedtls_ssl_conf_ca_chain(&conf, &s_Ca, nullptr);
            mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &s_Drbg);
            mbedtls_ssl_conf_max_tls_version(&conf, MBEDTLS_SSL_VERSION_TLS1_2);
            if(mbedtls_ssl_setup(&ssl, &conf) != 0) { why = "TLS setup failed"; break; }
            mbedtls_ssl_set_hostname(&ssl, host.c_str());
            mbedtls_ssl_set_bio(&ssl, (void*)(intptr_t)fd, Mqtt::BioSend, Mqtt::BioRecv, nullptr);
            int r;
            while((r = mbedtls_ssl_handshake(&ssl)) != 0)
            {
                if(r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) break;
                if(!waitFd(r == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT)) break;
            }
            if(r != 0) { why = "TLS handshake failed"; break; }
        }
        size_t off = 0; bool sendOk = true;
        while(off < req.size())
        {
            int n = tls ? mbedtls_ssl_write(&ssl, (const unsigned char*)req.data() + off, req.size() - off)
                        : (int)send(fd, req.data() + off, req.size() - off, MSG_NOSIGNAL);
            if(n > 0) { off += (size_t)n; continue; }
            bool again = tls ? (n == MBEDTLS_ERR_SSL_WANT_WRITE || n == MBEDTLS_ERR_SSL_WANT_READ) : (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
            if(!again || !waitFd(POLLOUT)) { sendOk = false; break; }
        }
        if(!sendOk) { why = "send failed"; break; }
        for(;;)
        {
            unsigned char buf[4096];
            int n = tls ? mbedtls_ssl_read(&ssl, buf, sizeof(buf)) : (int)recv(fd, buf, sizeof(buf), 0);
            if(n > 0) { raw.append((const char*)buf, n); if(t_HttpsProg) t_HttpsProg->store(raw.size()); if(raw.size() > maxBytes) break; continue; }
            if(n == 0 || (tls && (n == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY))) break;
            bool again = tls ? (n == MBEDTLS_ERR_SSL_WANT_READ || n == MBEDTLS_ERR_SSL_WANT_WRITE) : (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR);
            if(!again) break;
            // Content-Length reached? (keep-alive servers ignore Connection: close sometimes)
            size_t he = raw.find("\r\n\r\n");
            if(he != std::string::npos)
            {
                std::string hl = raw.substr(0, he); for(auto& c : hl) c = (char)tolower((unsigned char)c);
                size_t cl = hl.find("content-length:");
                if(cl != std::string::npos && raw.size() - (he + 4) >= (size_t)atol(hl.c_str() + cl + 15)) break;
            }
            if(!waitFd(POLLIN)) break;
        }
        size_t he = raw.find("\r\n\r\n");
        if(raw.compare(0, 5, "HTTP/") || he == std::string::npos) { why = raw.empty() ? "no answer" : "bad answer"; break; }
        int status = atoi(raw.c_str() + raw.find(' ') + 1);
        if(status != 200) { why = "HTTP " + std::to_string(status); break; }
        std::string hl = raw.substr(0, he); for(auto& c : hl) c = (char)tolower((unsigned char)c);
        std::string b = raw.substr(he + 4);
        if(hl.find("transfer-encoding: chunked") != std::string::npos)
        {
            size_t p = 0;
            while(p < b.size())
            {
                size_t le = b.find("\r\n", p); if(le == std::string::npos) break;
                long n = strtol(b.c_str() + p, nullptr, 16); p = le + 2;
                if(n <= 0 || p + (size_t)n > b.size()) break;
                body.append(b, p, (size_t)n); p += (size_t)n + 2;
            }
        }
        else
        {
            size_t cl = hl.find("content-length:");
            if(cl != std::string::npos) { size_t n = (size_t)atol(hl.c_str() + cl + 15); if(b.size() > n) b.resize(n); }
            body = b;
        }
        ok = true;
    } while(false);
    if(tlsOn) { mbedtls_ssl_close_notify(&ssl); mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&conf); }
    close(fd);
    return ok;
}

static std::vector<Broker> s_PendingRegional;   // guarded
static bool                s_bPendingRegional = false;
static std::string         s_LastRegionsBody;   // guarded

// ---- 2.20: region pings + which broker to use -----------------------------------------------
static bool PingRegionsOnce()
{
    const uint32_t gen0 = s_nNetGen;   // 4.05.8
    std::vector<Broker> list;
    {
        std::lock_guard<std::mutex> lk(s_M);
        const std::vector<Broker>& src = s_bPendingRegional ? s_PendingRegional : s_Brokers;
        for(const auto& b : src) if(b.region >= 0) list.push_back(b);
    }
    int best[REGION_COUNT]; bool has[REGION_COUNT];
    for(int r = 0; r < REGION_COUNT; ++r) { best[r] = -2; has[r] = false; }
    // 2.23: all brokers at once (was one after another: 8 dead brokers = 40 s before the first
    // result, so the first connect still walked through every dead region)
    std::vector<int> res(list.size(), -2);
    std::vector<std::thread> th;
    for(size_t i = 0; i < list.size(); ++i)
    {
        has[list[i].region] = true;
        th.emplace_back([&list, &res, i]()
        {
            for(int t = 0; t < 2; ++t)   // best of 2: the first one includes the DNS lookup
            {
                int rtt = -1;
                int fd = TcpConnectTimed(list[i].host, list[i].port, 2500, &rtt, nullptr, 0);
                if(fd >= 0) { close(fd); if(res[i] < 0 || rtt < res[i]) res[i] = rtt; }
            }
            if(res[i] < 0) return;
            // 2.23: TCP alone is not enough - a router / firewall can accept every port and never
            // answer. Up = a real MQTT login works (own client id, so the lobby session is untouched).
            char cid[40]; snprintf(cid, sizeof(cid), "tfpr%08x%04x", Rand32(), (unsigned)i & 0xFFFF);
            Mqtt probe;
            if(probe.Open(list[i], cid, "", list[i].tls ? 6000 : 3500)) probe.Disconnect();
            else { OLOG("region probe %s:%u: TCP ok, MQTT %s -> down", list[i].host.c_str(), list[i].port, probe.err); res[i] = -2; }
            probe.Close();
        });
    }
    for(auto& t : th) t.join();
    for(size_t i = 0; i < list.size(); ++i)
    {
        int r = list[i].region;
        if(res[i] >= 0 && (best[r] < 0 || res[i] < best[r])) best[r] = res[i];
    }
    // 4.05.8: measured while the network changed (or nothing answered right after a switch): throw it
    // away and measure again in a moment, never mark all regions "down" because Wi-Fi just went
    bool any = false; for(int r = 0; r < REGION_COUNT; ++r) if(has[r] && best[r] >= 0) any = true;
    if(s_nNetGen != gen0 || NoNetwork() || (!any && NetChangedRecently(MP_NowMs(), 30000)))
    {
        OLOG("4.05.8: region ping across a network switch - measuring again");
        return false;
    }
    for(int r = 0; r < REGION_COUNT; ++r) s_RegionPing[r] = has[r] ? best[r] : -1;
    s_bPingedOnce = true;
    return true;
}

static void PingThread()
{
    s_bPinging = true;
    bool fetched = s_bFetchedAtStart;   // NetThread already downloaded it when nothing was saved
    for(int round = 0; ; ++round)
    {
        if(!fetched || round % 15 == 14) { FetchRegions(false, 2); fetched = true; }   // refresh about every 30 min
        bool done = true;
        if(s_bRegional || s_bPendingRegional) done = PingRegionsOnce();
        s_bPinging = false;
        // 4.05.8: wake up at once when the network changes (was a plain 20 s / 2 min sleep)
        int waitMs = !done ? 2000 : (round < 3 ? 20000 : 120000);
        for(int t = 0; t < waitMs; t += 250)
        {
            if(s_bPingNow.exchange(false)) { std::this_thread::sleep_for(std::chrono::milliseconds(1200)); break; }   // let the new network settle
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
}

static bool HaveRegion(int r)
{
    if(r < 0) return false;
    for(const auto& b : s_Brokers) if(b.region == r) return true;
    return false;
}

static int AutoRegion()
{
    int best = -1, bestMs = 0;
    for(int r = 0; r < REGION_COUNT; ++r)
    {
        int ms = s_RegionPing[r];
        if(ms >= 0 && (best < 0 || ms < bestMs)) { best = r; bestMs = ms; }
    }
    // 2.23: every region measured and down -> want none (stay on the public lobby), do not keep
    // aiming for the chosen region and dropping the working connection for it every minute
    if(best < 0 && !s_bPingedOnce && HaveRegion(s_nUiRegion)) best = s_nUiRegion;
    return best;
}

// The region whose broker we should be on: hosting -> the server's region (its listing says so),
// joining -> the server's region, else the chosen region or the lowest ping.
static int DesiredRegion(bool hostOn, int hostRegion)
{
    if(!s_bRegional) return -1;
    if(hostOn && HaveRegion(hostRegion)) return hostRegion;
    int jr; bool jp; { std::lock_guard<std::mutex> lk(s_M); jr = s_nJoinRegion; jp = s_bJoinPublic; }
    if(jp && !hostOn) return WANT_PUBLIC;   // 4.06
    if(jr >= 0 && HaveRegion(jr))
    {   // 3.34: both ends are branches -> stay on my nearest region, the bridge carries the game to jr
        int near = s_nPrefRegion >= 0 && HaveRegion(s_nPrefRegion) ? (int)s_nPrefRegion : AutoRegion();
        if(near >= 0 && near != jr && near < REGION_COUNT && jr < REGION_COUNT && s_RegionBranch[near] && s_RegionBranch[jr]) return near;
        return jr;
    }
    int pr = s_nPrefRegion;
    if(pr >= 0 && HaveRegion(pr)) return pr;
    return AutoRegion();
}

static std::vector<int> ConnectOrder(int want)
{
    std::vector<int> o;
    if(want == WANT_PUBLIC)   // 4.06: public brokers first (where that host relays), then the rest
    {
        for(int i = 0; i < BROKER_COUNT; ++i) if(s_Brokers[i].region < 0 && s_Brokers[i].user.empty()) o.push_back(i);
        for(int i = 0; i < BROKER_COUNT; ++i) if(std::find(o.begin(), o.end(), i) == o.end()) o.push_back(i);
        return o;
    }
    if(s_nBrokerOverride >= 0 && s_nBrokerOverride < BROKER_COUNT) o.push_back(s_nBrokerOverride);
    if(!s_bRegional) { for(int i = 0; i < BROKER_COUNT; ++i) if(i != s_nBrokerOverride) o.push_back(i); return o; }
    auto add = [&](int i) { for(int x : o) if(x == i) return; o.push_back(i); };
    for(int i = 0; i < BROKER_COUNT; ++i) if(s_Brokers[i].region == want && want >= 0) add(i);
    // then the other regions in a fixed order (lobby is bridged between them), then private + public.
    // Fixed, not by ping: a host and its players who want the same region must land on the same broker.
    for(int i = 0; i < BROKER_COUNT; ++i) if(s_Brokers[i].region >= 0) add(i);
    for(int i = 0; i < BROKER_COUNT; ++i) add(i);   // private, then the public ones
    return o;
}

// Downloads regions.txt (if configured). apply = use it now (net thread, before connecting),
// else it waits in s_Pending until the net thread (re)connects.
static void FetchRegions(bool apply, int tries)
{
    if(s_RegionsUrl.empty()) return;
    std::string body, why;
    bool ok = false;
    for(int t = 0; t < tries && !ok; ++t) ok = HttpsGet(s_RegionsUrl, body, 5000, why);
    if(!ok && s_RegionsUrl == MP_REGIONS_URL)   // 4.05.4: new website down / not reachable -> old wagu address
    {
        std::string why2;
        ok = HttpsGet(MP_REGIONS_URL_OLD, body, 5000, why2);
        if(ok) OLOG("regions list: new site failed (%s), got it from %s", why.c_str(), MP_REGIONS_URL_OLD);
    }
    if(!ok)
    {
        OLOG("regions list: %s (%s)", why.c_str(), s_RegionsUrl.c_str());
        std::lock_guard<std::mutex> lk(s_M);
        if(s_RegionsNote.empty()) s_RegionsNote = "Region list not downloaded (" + why + ")";
        return;
    }
    BrokerCfg rc; ParseBrokerText(body, rc);
    OLOG("regions list: %d regional broker(s)", (int)rc.regional.size());
    {
        std::lock_guard<std::mutex> lk(s_M);
        if(body == s_LastRegionsBody) return;    // unchanged
        s_LastRegionsBody = body;
    }
    if(!s_RegionsCache.empty())
    {
        std::string tmp = s_RegionsCache + ".tmp";
        if(FILE* f = fopen(tmp.c_str(), "wb")) { fwrite(body.data(), 1, body.size(), f); fclose(f); rename(tmp.c_str(), s_RegionsCache.c_str()); }
    }
    if(apply) RebuildBrokers(rc.regional);
    std::lock_guard<std::mutex> lk(s_M);
    if(!apply) { s_PendingRegional = rc.regional; s_bPendingRegional = true; }
    s_RegionsNote = rc.regional.empty() ? "Region list: no regional brokers yet (public lobby)" : std::to_string(rc.regional.size()) + " regional broker(s) from the website";
}

static void ApplyPendingRegions()   // net thread only
{
    std::vector<Broker> v;
    {
        std::lock_guard<std::mutex> lk(s_M);
        if(!s_bPendingRegional) return;
        v.swap(s_PendingRegional); s_bPendingRegional = false;
    }
    RebuildBrokers(v);
}

// ------------------------------------------------------------------------------------------
void Online::UseLegacyTopics() { s_TopicRoot = TOPIC_ROOT_OLD; }   // 3.04.6: mp_server --legacy-notice only
// 4.06: public lobby watcher (read only: list + details, no listing of our own, no game traffic)
static void PubWatchThread()
{
    Mqtt mq;
    int bi = 0, fails = 0;
    uint64_t lastPing = 0, pingSent = 0, idleSince = 0;
    const int nPub = 3;   // hivemq tcp / ws / tls - the public lobby the mod falls back to
    for(;;)
    {
        uint64_t now = MP_NowMs();
        bool need = s_bRegional && s_bConnected && s_nCurRegion >= 0;
        if(!need)
        {
            if(!idleSince) idleSince = now;
            if(mq.fd >= 0 && now - idleSince > 3000) { mq.Disconnect(); s_bPubUp = false; }
            if(now - idleSince > 3000) { std::lock_guard<std::mutex> lk(s_M); s_PubList.clear(); }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        idleSince = 0;
        if(mq.fd < 0)
        {
            s_bPubUp = false;
            const Broker& b = kPublicBrokers[bi % nPub];
            char cid[24]; snprintf(cid, sizeof(cid), "tfpw%08x%04x", Rand32(), Rand32() & 0xFFFF);
            if(!mq.Open(b, cid, "", 6000))
            {
                OLOG("public lobby watch: %s failed (%s)", b.label.c_str(), mq.err);
                ++bi; ++fails;
                std::this_thread::sleep_for(std::chrono::milliseconds(fails < 3 ? 2000 : 15000));
                continue;
            }
            fails = 0; mq.blockingSend = false;
            if(!mq.Subscribe((TOPIC_ROOT + "l/+").c_str(), 1) || !mq.Subscribe((TOPIC_ROOT + "d/+").c_str(), 2)) { mq.Close(); continue; }
            s_bPubUp = true; lastPing = now; pingSent = 0;
            OLOG("public lobby watch: on %s", b.label.c_str());
        }
        for(int n = 0; n < 256; ++n)
        {
            uint8_t type; std::string body;
            int r = mq.ReadPacket(type, body, n == 0 ? 100 : 0);
            if(r < 0) { mq.Close(); break; }
            if(r == 0) break;
            uint8_t kind = type >> 4;
            if(kind == 13 && pingSent) { s_nPubRtt = (int)(MP_NowMs() - pingSent); pingSent = 0; continue; }
            if(kind != 3 || body.size() < 2) continue;
            bool retained = type & 1; int qos = (type >> 1) & 3;
            size_t tl = ((size_t)(uint8_t)body[0] << 8) | (uint8_t)body[1];
            if(2 + tl > body.size()) continue;
            std::string topic = body.substr(2, tl);
            size_t off = 2 + tl + (qos ? 2 : 0);
            if(off > body.size()) continue;
            const size_t rootLen = TOPIC_ROOT.size();
            if(topic.compare(0, rootLen, TOPIC_ROOT)) continue;
            const char* t = topic.c_str() + rootLen;
            if(strlen(t) != 10) continue;
            uint32_t tok = ParseHex8(t + 2);
            if(!tok) continue;
            std::string pl = body.substr(off);
            if(!strncmp(t, "l/", 2)) HandleListingIn(s_PubList, tok, pl, retained);
            else if(!strncmp(t, "d/", 2))
            {
                std::lock_guard<std::mutex> lk(s_DetMx);
                if(!pl.empty() && pl.size() < 4096) s_Details[tok] = { pl, MP_NowMs() };
            }
        }
        if(mq.fd < 0) { s_bPubUp = false; continue; }
        now = MP_NowMs();
        { static uint32_t netSeen = 0; if(NetMovedAway(mq.fd, netSeen)) { mq.Close(); s_bPubUp = false; continue; } }
        if(pingSent && now - pingSent > 15000) { mq.Close(); s_bPubUp = false; continue; }
        if(now - lastPing > 10000) { lastPing = now; pingSent = now; if(!mq.Ping()) { mq.Close(); s_bPubUp = false; } }
    }
}

void Online::Start(const char* brokerFile)
{
    bool expected = false;
    if(!s_bStarted.compare_exchange_strong(expected, true)) return;
#ifndef __ANDROID__
    if(getenv("MP_FLUSH_MS")) s_nFlushMs = atoi(getenv("MP_FLUSH_MS"));    // test tuning (PC only)
    if(getenv("MP_BROKER")) s_nBrokerOverride = atoi(getenv("MP_BROKER"));
    if(getenv("MP_BROKER_FILE")) brokerFile = getenv("MP_BROKER_FILE");
#endif
    for(auto& p : s_RegionPing) p = -1;
    {   // 2.20: the downloaded region list is saved next to broker.txt
        std::string dir = brokerFile && *brokerFile ? brokerFile : "";
        size_t sl = dir.rfind('/');
        s_RegionsCache = (sl == std::string::npos ? std::string() : dir.substr(0, sl + 1)) + "regions_cache.txt";
    }
    LoadBrokerConfig(brokerFile);
    for(auto& b : s_Brokers) OLOG("broker %s:%u%s%s%s", b.host.c_str(), b.port, b.tls ? " tls" : "", b.ws ? " ws" : "", b.user.empty() ? "" : " +login");
    snprintf(s_szCid, sizeof(s_szCid), "tfmp%08x%04x", Rand32(), Rand32() & 0xFFFF);
    { std::lock_guard<std::mutex> lk(s_M); s_nNextIdx = 1 + Rand32() % 60000; }   // relay IPs differ per session
    std::thread(NetThread).detach();
    std::thread(DirectThread).detach();   // 3.11: direct UDP to 24/7 servers
    std::thread(PubWatchThread).detach(); // 4.06: public lobby players visible on a region
}

bool Online::IsConnected() { return s_bConnected; }
uint32_t Online::NetGeneration() { return s_nNetGen; }   // 4.05.8
int  Online::BrokerRttMs() { return s_nRtt; }
int  Online::BrokerJitterMs() { return s_nRttJitter; }   // 4.06
int  Online::CongestionMs() { return s_nCongestMs.load(); }
const char* Online::BrokerName()
{
    static char copy[64];
    std::lock_guard<std::mutex> lk(s_M);
    MP_CopyStr(copy, s_bConnected ? s_szBroker : "", sizeof(copy));
    return copy;
}

bool Online::WaitReady(int maxMs)
{
    if(!s_bStarted) return false;
    uint64_t until = MP_NowMs() + (uint64_t)maxMs;
    int failedAtStart = s_nAttemptFailed;
    while(MP_NowMs() < until)
    {
        uint64_t since = s_nReadySince;
        if(s_bConnected && since && MP_NowMs() - since >= READY_AFTER_MS) return true;
        if(!s_bConnected && s_nAttemptFailed > failedAtStart) return false;   // no broker reachable
        if(!s_bConnected && s_nAttemptFailed >= 2) return false;              // offline for a while: don't wait
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    uint64_t since = s_nReadySince;
    return s_bConnected && since && MP_NowMs() - since >= READY_AFTER_MS;
}

std::vector<LanServer> Online::Snapshot()
{
    std::vector<LanServer> out;
    uint64_t now = MP_NowMs();
    int rtt = s_nRtt;
    std::lock_guard<std::mutex> lk(s_M);
    for(auto it = s_List.begin(); it != s_List.end();)
    {
        const Listing& l = it->second;
        if(now - l.lastSeen > LIST_FRESH_MS * 6) { it = s_List.erase(it); continue; }   // long dead
        // 2.20: with regional brokers every region has its own default server: only show ours
        bool otherDefault = s_bRegional && l.region != s_nUiRegion && l.port == MP_DEFAULT_PORT && !strcasecmp(l.name, MP_DEFAULT_SERVER_NAME);
        if(l.confirmed && now - l.lastSeen <= LIST_FRESH_MS && it->first != s_nHostToken && !otherDefault)
        {
            LanServer s; memset(&s, 0, sizeof(s));
            RelayIp(s_RelayIdx[it->first], s.ip);
            s.port = l.port; MP_CopyStr(s.name, l.name, sizeof(s.name));
            s.players = l.players; s.maxPlayers = l.maxPlayers; s.region = l.region;
            s.proto = l.proto; s.icon = l.icon; MP_CopyStr(s.lang, l.lang, sizeof(s.lang)); s.startUnix = l.startUnix; s.dedicated = l.dedicated ? 1 : 0;
            s.ping = rtt >= 0 ? (l.hostRtt > 0 ? rtt + l.hostRtt : rtt * 2) : 0;   // 4.05.1: measured on both ends when the host sends it
            s.token = it->first;
            {   // 3.11: a 24/7 server we can reach over direct UDP: show the real (measured) ping
                auto d = s_Direct.find(it->first);
                if(d != s_Direct.end() && d->second.ok && d->second.rtt >= 0 && now - d->second.at < 90000) s.ping = d->second.rtt;
            }
            out.push_back(s);
        }
        ++it;
    }
    // 4.06: + servers that are only in the public lobby (we are on a region broker)
    if(s_bPubUp && s_nCurRegion >= 0)
    {
        int prtt = s_nPubRtt;
        for(auto it = s_PubList.begin(); it != s_PubList.end();)
        {
            const Listing& l = it->second;
            if(now - l.lastSeen > LIST_FRESH_MS * 6) { it = s_PubList.erase(it); continue; }
            auto mine = s_List.find(it->first);
            bool dup = mine != s_List.end() && mine->second.confirmed && now - mine->second.lastSeen <= LIST_FRESH_MS;
            if(!dup && l.confirmed && now - l.lastSeen <= LIST_FRESH_MS && it->first != s_nHostToken)
            {
                LanServer s; memset(&s, 0, sizeof(s));
                RelayIp(s_RelayIdx[it->first], s.ip);
                s.port = l.port; MP_CopyStr(s.name, l.name, sizeof(s.name));
                s.players = l.players; s.maxPlayers = l.maxPlayers; s.region = l.region;
                s.proto = l.proto; s.icon = l.icon; MP_CopyStr(s.lang, l.lang, sizeof(s.lang)); s.startUnix = l.startUnix; s.dedicated = l.dedicated ? 1 : 0;
                s.ping = prtt >= 0 ? (l.hostRtt > 0 ? prtt + l.hostRtt : prtt * 2) : 0;
                s.token = it->first;
                bool twin = false;   // the same 24/7 server also on our region: keep the region copy (no broker move)
                if(s.dedicated) for(const LanServer& o : out) if(o.dedicated && o.port == s.port && !strcasecmp(o.name, s.name)) { twin = true; break; }
                if(!twin) out.push_back(s);
            }
            ++it;
        }
    }
    // 4.05.1: a 24/7 server listed by two hosts (shift hand-over, or a second 24/7 runner) shows once:
    // the newest start wins (that is the copy that stays)
    for(size_t i = 0; i < out.size(); ++i)
    {
        if(!out[i].dedicated) continue;
        for(size_t j = i + 1; j < out.size();)
        {
            if(out[j].dedicated && out[j].port == out[i].port && !strcasecmp(out[j].name, out[i].name))
            {
                if(out[j].startUnix > out[i].startUnix) out[i] = out[j];
                out.erase(out.begin() + (long)j);
            }
            else ++j;
        }
    }
    return out;
}

void Online::SetHosted(bool running, const char* name, uint16_t port, int players, int maxPlayers, int region)
{
    std::lock_guard<std::mutex> lk(s_M);
    bool changed = running != s_bHostOn || (running && (port != s_nHostPort || strcasecmp(name ? name : "", s_szHostName)));
    if(running && s_bHostOn && region != s_nHostRegion && s_bRegional) changed = true;   // 2.20: new region = new broker = new listing
    if(changed) s_nHostToken = running ? Rand32() : 0;   // every hosted server gets a new token
    s_bHostOn = running;
    MP_CopyStr(s_szHostName, name ? name : "", sizeof(s_szHostName));
    for(char* c = s_szHostName; *c; ++c) if(*c == '|') *c = '/';
    s_nHostPort = port; s_nHostPlayers = players; s_nHostMax = maxPlayers; s_nHostRegion = region;
}

uint32_t Online::MyToken() { return s_nHostToken; }
void Online::SetHostStart(uint32_t unixStart) { s_nHostStart = unixStart; }
void Online::SetHostDedicated(bool on) { s_bHostDedicated = on; }   // 2.29   // 2.27
void Online::SetHostLang(const char* lang)   // 2.25
{
    std::lock_guard<std::mutex> lk(s_LangMx);
    MP_CopyStr(s_szHostLang, lang ? lang : "", sizeof(s_szHostLang)); MP_CleanLang(s_szHostLang);
}
void Online::SetHostDetails(const std::string& d) { std::lock_guard<std::mutex> lk(s_DetMx); s_HostDetails = d.size() > 2000 ? d.substr(0, 2000) : d; }   // 4.03
void Online::SetHostPreview(const std::string& jpeg) { std::lock_guard<std::mutex> lk(s_DetMx); s_HostPreview = jpeg.size() <= 200 * 1024 ? jpeg : std::string(); }   // 4.04
bool Online::Preview(uint32_t tok, std::string& out)
{
    std::lock_guard<std::mutex> lk(s_DetMx);
    auto it = s_Previews.find(tok);
    if(it == s_Previews.end()) return false;
    out = it->second; return true;
}
bool Online::Details(uint32_t tok, std::string& out)
{
    std::lock_guard<std::mutex> lk(s_DetMx);
    auto it = s_Details.find(tok);
    if(it == s_Details.end()) return false;
    out = it->second.first; return true;
}
void Online::SetHostIcon(int icon) { s_nHostIcon = (icon >= 0 && icon <= MP_ICON_MAX) ? icon : 0; }   // 2.24

// ---- 3.11: direct UDP to 24/7 servers ------------------------------------------------------
// A 24/7 server ("!d" in the listing) installed with the HiveMQ kit runs on the same machine as
// its region's broker, and its UDP game port is open to the internet. Going through the broker
// (phone -TCP-> broker -TCP-> server and back, batched) is what made the ping 200-500 ms. This
// thread asks that machine directly (PKT_QUERY over UDP to <region broker host>:<server port>)
// every 15 s. If the answer is the same server (name + start time), MP connects straight to it:
// one UDP hop each way. If UDP is blocked on the phone's network the probe fails and the relay
// is used like before (and MPClient falls back to the relay by itself if a direct join is not
// answered within 2.5 s).

static std::string RegionHostName(int region)
{
    for(const Broker& b : s_Brokers)
    {
        if(b.region != region || b.region < 0 || b.ws) continue;
        std::string h = b.host;
        size_t sc = h.find("://"); if(sc != std::string::npos) h = h.substr(sc + 3);
        size_t c = h.find_first_of(":/"); if(c != std::string::npos) h = h.substr(0, c);
        if(!h.empty()) return h;
    }
    return "";
}
static bool ResolveV4(const std::string& host, uint32_t* ipNet)
{
    static std::map<std::string, std::pair<uint32_t, uint64_t>> cache;   // probe thread only
    uint64_t now = MP_NowMs();
    auto c = cache.find(host);
    if(c != cache.end() && now - c->second.second < 300000) { *ipNet = c->second.first; return c->second.first != 0; }
    addrinfo hints, *res = nullptr; memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    uint32_t ip = 0;
    if(getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) { ip = ((sockaddr_in*)res->ai_addr)->sin_addr.s_addr; freeaddrinfo(res); }
    cache[host] = { ip, now };
    *ipNet = ip;
    return ip != 0;
}
// -1 = no answer / not that server, else the round trip in ms
static int ProbeDirect(uint32_t ipNet, uint16_t port, const char* name, uint32_t startUnix)
{
    int fd = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(fd < 0) return -1;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    sockaddr_in to; memset(&to, 0, sizeof(to)); to.sin_family = AF_INET; to.sin_addr.s_addr = ipNet; to.sin_port = htons(port);
    int best = -1;
    uint64_t t0 = MP_NowMs();
    int sent = 0;
    while(MP_NowMs() - t0 < 1200 && best < 0)
    {
        uint64_t now = MP_NowMs();
        if(sent < 3 && now - t0 >= (uint64_t)sent * 300)   // 3 tries: one lost UDP packet is no reason to give up
        {
            PktQuery q; q.type = PKT_QUERY; q.time = (uint32_t)now;
            sendto(fd, (const char*)&q, sizeof(q), 0, (sockaddr*)&to, sizeof(to));
            ++sent;
        }
        pollfd pf; pf.fd = fd; pf.events = POLLIN; pf.revents = 0;
        if(poll(&pf, 1, 50) <= 0) continue;
        uint8_t buf[512]; sockaddr_in from; socklen_t fl = sizeof(from);
        int r = (int)recvfrom(fd, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if(r < (int)sizeof(PktInfo) || buf[0] != PKT_INFO || from.sin_addr.s_addr != ipNet || from.sin_port != to.sin_port) continue;
        PktInfo in; memcpy(&in, buf, sizeof(in));
        char nm[33]; memcpy(nm, in.serverName, 32); nm[32] = 0;
        if(strncasecmp(nm, name, 31)) continue;                     // another server on that port
        if(startUnix && r >= (int)sizeof(PktInfo5))
        {
            uint32_t su; memcpy(&su, buf + offsetof(PktInfo5, startUnix), sizeof(su));
            if(su && su != startUnix) continue;                     // not the same copy of the server
        }
        best = (int)(MP_NowMs() - (uint64_t)in.time);
        if(best < 0 || best > 5000) best = (int)(MP_NowMs() - now);
    }
    close(fd);
    return best;
}
static void DirectThread()
{
    uint64_t lastRound = 0;
    while(s_bStarted)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if(s_bHostDedicated || getenv("MP_NO_DIRECT")) continue;   // a 24/7 server itself never needs it
        uint64_t now = MP_NowMs();
        struct Cand { uint32_t token; Listing l; bool fresh; };
        std::vector<Cand> cands;
        {
            std::lock_guard<std::mutex> lk(s_M);
            for(auto& it : s_List)
            {
                const Listing& l = it.second;
                if(!l.confirmed || !l.dedicated || l.region < 0 || now - l.lastSeen > LIST_FRESH_MS) continue;
                auto d = s_Direct.find(it.first);
                bool unknown = d == s_Direct.end() || !d->second.at;
                if(d != s_Direct.end() && d->second.badUntil > now) continue;
                cands.push_back({ it.first, l, unknown });
            }
        }
        bool round = now - lastRound >= 15000;
        if(round) lastRound = now;
        for(const Cand& c : cands)
        {
            if(!round && !c.fresh) continue;   // new servers right away, the rest every 15 s
            std::string host = RegionHostName(c.l.region);
            uint32_t ip = 0;
            int rtt = (!host.empty() && ResolveV4(host, &ip)) ? ProbeDirect(ip, c.l.port, c.l.name, c.l.startUnix) : -1;
            std::lock_guard<std::mutex> lk(s_M);
            DirectInfo& d = s_Direct[c.token];
            d.at = MP_NowMs(); d.port = c.l.port;
            if(rtt >= 0) { d.ok = true; d.ip = ip; d.rtt = d.rtt >= 0 ? (d.rtt * 3 + rtt) / 4 : rtt; d.fails = 0; }
            else if(++d.fails >= 2) { d.ok = false; d.rtt = -1; }
        }
    }
}
static uint32_t TokenForRelayIp(const char* relayIp)   // s_M held
{
    in_addr a;
    if(!relayIp || inet_pton(AF_INET, relayIp, &a) != 1) return 0;
    uint32_t idx = ntohl(a.s_addr) & 0xFFFFu;
    for(auto& r : s_RelayIdx) if(r.second == idx) return r.first;
    return 0;
}
bool Online::DirectAddr(const char* relayIp, uint16_t port, uint32_t* ipNet, int* rttMs)
{
    std::lock_guard<std::mutex> lk(s_M);
    uint32_t tok = TokenForRelayIp(relayIp);
    if(!tok) return false;
    auto d = s_Direct.find(tok);
    if(d == s_Direct.end() || !d->second.ok || d->second.port != port || d->second.badUntil > MP_NowMs()) return false;
    if(MP_NowMs() - d->second.at > 90000) return false;
    *ipNet = d->second.ip;
    if(rttMs) *rttMs = d->second.rtt;
    return true;
}
void Online::DirectFailed(const char* relayIp)
{
    std::lock_guard<std::mutex> lk(s_M);
    uint32_t tok = TokenForRelayIp(relayIp);
    if(!tok) return;
    DirectInfo& d = s_Direct[tok];
    d.ok = false; d.badUntil = MP_NowMs() + 10 * 60 * 1000;   // relay for the next 10 minutes
}

bool Online::IsRelayIp(const char* ip)
{
    in_addr a;
    if(!ip || inet_pton(AF_INET, ip, &a) != 1) return false;
    return (ntohl(a.s_addr) & 0xFFFF0000u) == RELAY_NET;
}

bool Online::OpenProxy(const char* relayIp, uint16_t port, uint16_t* localPort)
{
    in_addr a;
    if(!IsRelayIp(relayIp) || inet_pton(AF_INET, relayIp, &a) != 1) return false;
    uint32_t idx = ntohl(a.s_addr) & 0xFFFF;
    std::lock_guard<std::mutex> lk(s_M);
    uint32_t tok = 0;
    for(auto& kv : s_RelayIdx) if(kv.second == idx) tok = kv.first;
    if(!tok) return false;
    if(s_ProxyFd >= 0) { s_CloseFds.push_back(s_ProxyFd); s_ProxyFd = -1; }
    // plain 127.0.0.1:<free port>: works on every phone (no special loopback addresses needed)
    int fd = NewUdp(0x7F000001u, 0);
    if(fd < 0) return false;
    sockaddr_in la; socklen_t ll = sizeof(la);
    if(getsockname(fd, (sockaddr*)&la, &ll) != 0) { close(fd); return false; }
    if(localPort) *localPort = ntohs(la.sin_port);
    (void)port;
    s_ProxyFd = fd; s_ProxyToken = tok; s_bProxyPeer = false;
    // 2.20: the host relays on its region's broker; go there (the net thread switches)
    auto li = s_List.find(tok);
    s_nJoinRegion = (li != s_List.end() && s_bRegional) ? li->second.region : -1;
    // 4.06: listed only in the public lobby -> its host relays there: the lobby link moves to the public broker
    s_bJoinPublic = s_bRegional && (li == s_List.end() || !li->second.confirmed || MP_NowMs() - li->second.lastSeen > LIST_FRESH_MS) && s_PubList.count(tok);
    if(s_bJoinPublic) { s_nJoinRegion = -1; OLOG("4.06: joining a server of the public lobby - moving the lobby link there"); }
    return true;
}

void Online::CloseProxy()
{
    std::lock_guard<std::mutex> lk(s_M);
    if(s_ProxyFd >= 0) s_CloseFds.push_back(s_ProxyFd);
    s_ProxyFd = -1; s_ProxyToken = 0; s_bProxyPeer = false;
    s_nJoinRegion = -1; s_bJoinPublic = false;
}

void Online::SetRegionPrefs(int menuRegion, bool autoPick)
{
    if(menuRegion >= 0 && menuRegion < REGION_COUNT) s_nUiRegion = menuRegion;
    s_nPrefRegion = autoPick ? -1 : s_nUiRegion.load();
}
bool Online::HasRegionalBrokers() { return s_bRegional; }
int  Online::CurrentRegion() { return s_bConnected ? s_nCurRegion.load() : -1; }
int  Online::RegionPingMs(int r) { return (r >= 0 && r < REGION_COUNT) ? s_RegionPing[r].load() : -1; }
int  Online::BestRegion()
{
    if(!s_bRegional) return -1;
    int best = -1, bestMs = 0;
    for(int r = 0; r < REGION_COUNT; ++r) { int ms = s_RegionPing[r]; if(ms >= 0 && (best < 0 || ms < bestMs)) { best = r; bestMs = ms; } }
    return best;
}

void Online::GetDiagnostics(std::vector<std::string>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lk(s_M);
    char line[160];
    snprintf(line, sizeof(line), "Lobby: %s  |  my id %s", s_bConnected ? s_szBroker : "not connected", s_szCid);
    out.push_back(line);
    int live = 0, all = (int)s_List.size();
    uint64_t now = MP_NowMs();
    for(auto& kv : s_List) if(kv.second.confirmed && now - kv.second.lastSeen <= LIST_FRESH_MS) ++live;
    snprintf(line, sizeof(line), "Servers in lobby: %d live (%d seen)%s", live, all, s_nHostToken ? "  |  you are listed" : "");
    out.push_back(line);
    if(!s_ConfigNote.empty()) out.push_back(s_ConfigNote);
    if(!s_RegionsNote.empty()) out.push_back(s_RegionsNote);
    if(s_bRegional)
    {
        std::string pl = "Region ping:";
        for(int r = 0; r < REGION_COUNT; ++r)
        {
            int ms = s_RegionPing[r];
            char b[48];
            if(ms >= 0) snprintf(b, sizeof(b), "  %s %d ms", g_MPRegionNames[r], ms);
            else if(ms == -2) snprintf(b, sizeof(b), "  %s down", g_MPRegionNames[r]);
            else continue;
            pl += b;
        }
        out.push_back(pl);
        int cr = s_nCurRegion;
        snprintf(line, sizeof(line), "Lobby region: %s%s", cr >= 0 ? g_MPRegionNames[cr] : "public broker", s_nPrefRegion < 0 ? " (auto: lowest ping)" : "");
        out.push_back(line);
        bool anyUp = false; for(int r = 0; r < REGION_COUNT; ++r) if(RegionUp(r)) anyUp = true;
        if(s_bPingedOnce && !anyUp) out.push_back("All regions down: staying on the public lobby, regions re-checked every 2 min (2.23)");
    }
    for(int i = 0; i < (int)s_Brokers.size() && i < (int)s_Diag.size(); ++i) if(!s_Diag[i].empty()) out.push_back(s_Diag[i]);
}

// ---------------------------------------------------------------------------------------------
// 3.21.6: AUTH LINK. The login system talks to the MP Discord bot over its own small MQTT
// connection (never the game relay: a login can't slow the game, the game can't delay a login).
// Everything on it is end-to-end encrypted (authcrypto.h), so plain public brokers are fine.
// The bot listens on all of these; we use the first one that answers.
// ---------------------------------------------------------------------------------------------
static const Broker kAuthBrokers[] = {
    { "broker.hivemq.com",  1883, false, false, "",      "hivemq tcp" },
    { "broker.hivemq.com",  8000, true,  false, "/mqtt", "hivemq ws"  },
    { "test.mosquitto.org", 1883, false, false, "",      "mosquitto tcp" },
    { "broker.emqx.io",     1883, false, false, "",      "emqx tcp" },
};
static std::mutex              s_AuthM;
static std::atomic<bool>       s_bAuthRun{false}, s_bAuthUp{false};
static std::thread             s_AuthThread;
static std::string             s_AuthSid;               // guarded: response topic id (empty = none)
static std::deque<std::string> s_AuthOut, s_AuthIn;     // guarded
static char                    s_szAuthBroker[32] = "";

static void AuthThread()
{
    Mqtt mq;
    int bi = 0;
    std::string subbed;
    while(s_bAuthRun)
    {
        if(mq.fd < 0)
        {
            s_bAuthUp = false; subbed.clear();
            const Broker& b = kAuthBrokers[bi % (int)(sizeof(kAuthBrokers) / sizeof(kAuthBrokers[0]))];
            char cid[24]; snprintf(cid, sizeof(cid), "wa%08x%08x", Rand32(), Rand32());
            if(!mq.Open(b, cid, nullptr, 5000))
            {
                OLOG("auth link: %s failed (%s)", b.label.c_str(), mq.err);
                ++bi;
                for(int i = 0; i < 20 && s_bAuthRun; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            snprintf(s_szAuthBroker, sizeof(s_szAuthBroker), "%s", b.label.c_str());
            s_bAuthUp = true;
        }
        std::string sid;
        { std::lock_guard<std::mutex> lk(s_AuthM); sid = s_AuthSid; }
        if(sid != subbed)
        {
            if(!subbed.empty()) mq.Unsubscribe((std::string(MP_AUTH_TOPIC_RES) + subbed).c_str(), 2);
            if(!sid.empty() && !mq.Subscribe((std::string(MP_AUTH_TOPIC_RES) + sid).c_str(), 1)) { mq.Close(); continue; }
            subbed = sid;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));   // SUBACK before the first request
        }
        for(;;)
        {
            std::string f;
            { std::lock_guard<std::mutex> lk(s_AuthM); if(s_AuthOut.empty()) break; f = s_AuthOut.front(); s_AuthOut.pop_front(); }
            if(!mq.Publish(MP_AUTH_TOPIC_REQ, f.data(), f.size(), false)) { std::lock_guard<std::mutex> lk(s_AuthM); s_AuthOut.push_front(f); mq.Close(); break; }
        }
        if(mq.fd < 0) continue;
        mq.FlushOut(50);
        uint8_t type; std::string body;
        int r = mq.ReadPacket(type, body, 100);
        if(r < 0) { OLOG("auth link: connection lost"); mq.Close(); continue; }
        if(r > 0 && (type >> 4) == 3 && body.size() >= 2)
        {
            size_t tl = ((uint8_t)body[0] << 8) | (uint8_t)body[1];
            size_t off = 2 + tl + (((type >> 1) & 3) ? 2 : 0);
            if(off <= body.size())
            {
                std::string topic = body.substr(2, tl);
                std::lock_guard<std::mutex> lk(s_AuthM);
                if(!subbed.empty() && topic == std::string(MP_AUTH_TOPIC_RES) + subbed && s_AuthIn.size() < 64)
                    s_AuthIn.push_back(body.substr(off));
            }
        }
        static uint64_t lastPing = 0;
        static uint32_t netSeen = 0;
        uint64_t now = MP_NowMs();
        if(NetMovedAway(mq.fd, netSeen)) { OLOG("auth link: network changed - reconnecting"); mq.Close(); continue; }   // 4.05.8
        if(now - lastPing > 20000) { lastPing = now; if(!mq.Ping()) mq.Close(); }
    }
    if(mq.fd >= 0) mq.Disconnect();
    s_bAuthUp = false;
}

void Online::AuthLinkStart(const char* sid)
{
    {
        std::lock_guard<std::mutex> lk(s_AuthM);
        s_AuthSid = sid ? sid : "";
        s_AuthIn.clear(); s_AuthOut.clear();
    }
    if(!s_bAuthRun.exchange(true))
    {
        if(s_AuthThread.joinable()) s_AuthThread.join();
        s_AuthThread = std::thread(AuthThread);
    }
}
void Online::AuthLinkStop()
{
    if(!s_bAuthRun.exchange(false)) return;
    if(s_AuthThread.joinable()) s_AuthThread.join();
    std::lock_guard<std::mutex> lk(s_AuthM);
    s_AuthSid.clear(); s_AuthIn.clear(); s_AuthOut.clear();
}
bool Online::AuthLinkUp() { return s_bAuthUp; }
const char* Online::AuthLinkBroker() { return s_bAuthUp ? s_szAuthBroker : ""; }
void Online::AuthLinkSend(const std::string& frame) { std::lock_guard<std::mutex> lk(s_AuthM); if(s_AuthOut.size() < 32) s_AuthOut.push_back(frame); }
bool Online::AuthLinkRecv(std::string& frame)
{
    std::lock_guard<std::mutex> lk(s_AuthM);
    if(s_AuthIn.empty()) return false;
    frame = s_AuthIn.front(); s_AuthIn.pop_front();
    return true;
}

// ---------------------------------------------------------------------------------------------
// 3.63: SOCIAL LINK. Same idea as the auth link: one small MQTT connection of its own. Subscriptions are
// remembered and sent again after a reconnect (retained messages = the mailbox / directory come again).
// ---------------------------------------------------------------------------------------------
static const Broker kSocBrokers[] = {
    { "broker.hivemq.com", 1883, false, false, "",      "HiveMQ" },
    { "broker.hivemq.com", 8000, true,  false, "/mqtt", "HiveMQ ws" },
};
struct SocOut { std::string topic, payload; bool retain; };
static std::mutex              s_SocM;
static std::atomic<bool>       s_bSocRun{false}, s_bSocUp{false};
static std::atomic<int>        s_nSocGen{0};
static std::thread             s_SocThread;
static std::string             s_SocWill;                       // guarded
static std::vector<std::string> s_SocSubs;                       // guarded: wanted filters
static std::deque<SocOut>      s_SocOut;                         // guarded
static std::deque<std::pair<std::string, std::string>> s_SocIn;  // guarded
static char                    s_szSocBroker[32] = "";

static void SocThread()
{
    Mqtt mq;
    int bi = 0, fails = 0;
    std::vector<std::string> subbed;
    uint64_t lastPing = 0;
    while(s_bSocRun)
    {
        if(mq.fd < 0)
        {
            s_bSocUp = false; subbed.clear();
            const Broker& b = kSocBrokers[bi % (int)(sizeof(kSocBrokers) / sizeof(kSocBrokers[0]))];
            char cid[24]; snprintf(cid, sizeof(cid), "ws%08x%08x", Rand32(), Rand32());
            std::string will; { std::lock_guard<std::mutex> lk(s_SocM); will = s_SocWill; }
            if(!mq.Open(b, cid, will.c_str(), 6000))
            {
                OLOG("social link: %s failed (%s)", b.label.c_str(), mq.err);
                ++bi; ++fails;
                int waitMs = fails < 3 ? 1000 : 5000;
                for(int i = 0; i < waitMs / 50 && s_bSocRun; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            fails = 0;
            mq.blockingSend = false;
            snprintf(s_szSocBroker, sizeof(s_szSocBroker), "%s", b.label.c_str());
            s_bSocUp = true; ++s_nSocGen; lastPing = MP_NowMs();
        }
        std::vector<std::string> want;
        { std::lock_guard<std::mutex> lk(s_SocM); want = s_SocSubs; }
        bool bad = false;
        for(auto& f : subbed) if(std::find(want.begin(), want.end(), f) == want.end()) mq.Unsubscribe(f.c_str(), 7);
        for(auto& f : want) if(std::find(subbed.begin(), subbed.end(), f) == subbed.end()) if(!mq.Subscribe(f.c_str(), 6)) bad = true;
        subbed = want;
        for(int n = 0; n < 64 && !bad; ++n)
        {
            SocOut o;
            { std::lock_guard<std::mutex> lk(s_SocM); if(s_SocOut.empty()) break; o = s_SocOut.front(); s_SocOut.pop_front(); }
            if(!mq.Publish(o.topic, o.payload.data(), o.payload.size(), o.retain)) { std::lock_guard<std::mutex> lk(s_SocM); s_SocOut.push_front(o); bad = true; }
        }
        if(bad || !mq.FlushOut(20)) { OLOG("social link: send failed"); mq.Close(); continue; }
        for(int n = 0; n < 128; ++n)
        {
            uint8_t type; std::string body;
            int r = mq.ReadPacket(type, body, n == 0 ? 60 : 0);
            if(r < 0) { OLOG("social link: connection lost"); mq.Close(); break; }
            if(r == 0) break;
            if((type >> 4) == 3 && body.size() >= 2)
            {
                size_t tl = ((uint8_t)body[0] << 8) | (uint8_t)body[1];
                size_t off = 2 + tl + (((type >> 1) & 3) ? 2 : 0);
                if(off > body.size()) continue;
                std::lock_guard<std::mutex> lk(s_SocM);
                if(s_SocIn.size() < 4096) s_SocIn.emplace_back(body.substr(2, tl), body.substr(off));
            }
        }
        if(mq.fd < 0) continue;
        uint64_t now = MP_NowMs();
        { static uint32_t netSeen = 0; if(NetMovedAway(mq.fd, netSeen)) { OLOG("social link: network changed - reconnecting"); mq.Close(); continue; } }   // 4.05.8
        if(now - lastPing > 20000) { lastPing = now; if(!mq.Ping()) mq.Close(); }
    }
    if(mq.fd >= 0)
    {   // clean exit: the will is not sent, so say "offline" ourselves
        std::string will; { std::lock_guard<std::mutex> lk(s_SocM); will = s_SocWill; }
        if(!will.empty()) { mq.Publish(will, "", 0, true); mq.FlushOut(300); }
        mq.Disconnect();
    }
    s_bSocUp = false;
}

void Online::SocLinkStart(const char* willTopic)
{
    { std::lock_guard<std::mutex> lk(s_SocM); s_SocWill = willTopic ? willTopic : ""; }
    if(!s_bSocRun.exchange(true))
    {
        if(s_SocThread.joinable()) s_SocThread.join();
        s_SocThread = std::thread(SocThread);
    }
}
void Online::SocLinkStop()
{
    if(!s_bSocRun.exchange(false)) return;
    if(s_SocThread.joinable()) s_SocThread.join();
}
bool Online::SocLinkUp() { return s_bSocUp; }
int  Online::SocLinkGen() { return s_nSocGen; }
const char* Online::SocLinkBroker() { return s_bSocUp ? s_szSocBroker : ""; }
void Online::SocLinkSub(const std::string& f)
{
    std::lock_guard<std::mutex> lk(s_SocM);
    if(std::find(s_SocSubs.begin(), s_SocSubs.end(), f) == s_SocSubs.end()) s_SocSubs.push_back(f);
}
void Online::SocLinkUnsub(const std::string& f)
{
    std::lock_guard<std::mutex> lk(s_SocM);
    s_SocSubs.erase(std::remove(s_SocSubs.begin(), s_SocSubs.end(), f), s_SocSubs.end());
}
void Online::SocLinkPub(const std::string& topic, const std::string& payload, bool retain)
{
    std::lock_guard<std::mutex> lk(s_SocM);
    if(s_SocOut.size() < 512) s_SocOut.push_back({ topic, payload, retain });
}
bool Online::SocLinkRecv(std::string& topic, std::string& payload)
{
    std::lock_guard<std::mutex> lk(s_SocM);
    if(s_SocIn.empty()) return false;
    topic.swap(s_SocIn.front().first); payload.swap(s_SocIn.front().second); s_SocIn.pop_front();
    return true;
}

// 3.61: Day Z model pack (dff / txd from GitHub, src/game/dzmodels.cpp). Blocking, background threads only.
bool Online::HttpsFetch(const std::string& url, std::string& body, int timeoutMs, std::string& why, size_t maxBytes, std::atomic<size_t>* got)
{
    t_HttpsProg = got;
    bool ok = HttpsGet(url, body, timeoutMs, why, maxBytes);
    t_HttpsProg = nullptr;
    return ok;
}
