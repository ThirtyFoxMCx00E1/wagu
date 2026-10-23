// LAN discovery + network state.
//  - HasNetwork(): is Wi-Fi, mobile data or a hotspot up? (no network = no multiplayer)
//  - Discover():   finds MP servers on the same Wi-Fi / hotspot (broadcast + /24 unicast sweep,
//                  the sweep still works on phones whose Wi-Fi driver drops broadcasts)
// Uses only SIOCGIFCONF-style ioctls, so it works on every Android API level (and on Linux).
#pragma once
#include <stdint.h>
#include <vector>
#include <netinet/in.h>
#include "protocol.h"

struct LanIface
{
    char     name[16];
    uint32_t ip;        // host byte order
    uint32_t mask;      // host byte order
    uint32_t bcast;     // host byte order
    bool     cellular;  // rmnet / ccmni / clat ... (mobile data)
};

struct LanServer
{
    char     ip[16];
    uint16_t port;
    char     name[32];
    int      players, maxPlayers, region, ping;
    uint16_t proto;
    uint8_t  locked;    // 2.19: password protected
    uint8_t  icon;      // 2.24: 0 = default icon, 1..250 = icon_N.png
    char     lang[MP_LANG_LEN];   // 2.25: server language (empty = not set / older server)
    uint32_t startUnix;           // 2.27: when that server started (0 = older server)
    uint32_t token;     // != 0: found in the online lobby (ip is then a 127.77.x.y relay address)
    uint8_t  dedicated; // 2.29: a 24/7 machine (mp_server --persistent), not a phone: always the copy to join
};

namespace LAN
{
    // Up, non-loopback IPv4 interfaces.
    std::vector<LanIface> Interfaces();
    // true when Wi-Fi, mobile data or a hotspot is on (any non-loopback IPv4 interface is up).
    bool HasNetwork();
    // true when the address belongs to this device (127.x or one of our interfaces).
    bool IsOwnAddress(uint32_t ipHostOrder);
    // true for 10/8, 172.16/12, 192.168/16, 100.64/10 (CGNAT) and 127/8.
    bool IsPrivateAddress(const char* host);

    // Blocking: queries the local networks and collects every MP server that answers
    // within timeoutMs. Our own servers are skipped unless includeSelf is set.
    struct Target { uint32_t ip; uint16_t port; };   // ip in host byte order
    // extra: also ask these exact addresses (a custom server's own host/IP, any network)
    std::vector<LanServer> Discover(const std::vector<uint16_t>& ports, int timeoutMs = 1500, bool includeSelf = false,
                                    const std::vector<Target>& extra = std::vector<Target>());
    // Blocking DNS/IP lookup ("ggwar", "net.ggwar.org", "192.168.1.5"). Call off the game thread.
    bool Resolve(const char* host, uint32_t* ipHostOrder);
    inline std::vector<LanServer> Discover(uint16_t port, int timeoutMs = 1500, bool includeSelf = false)
    { return Discover(std::vector<uint16_t>{ port }, timeoutMs, includeSelf); }

    // Picks the best server: same name + port first (most players wins), then any server
    // on that port with the same protocol. Returns -1 when none.
    int  PickBest(const std::vector<LanServer>& list, const char* name, uint16_t port);
}

// Kept for the standalone server: true when a reachable LAN server already uses the
// same server name and UDP port (a second host must join it instead of creating a copy).
bool MP_IsDuplicateServer(const char* serverName, uint16_t port, int timeoutMs = 1500);
