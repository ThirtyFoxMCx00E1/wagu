#include "lan.h"
#include "server.h" // MP_NowMs
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <netdb.h>

// Interfaces where a /24 sweep makes sense (Wi-Fi, hotspot, USB/BT tethering, ethernet).
static bool IsLanName(const char* n)
{
    static const char* const k[] = { "wlan", "swlan", "ap", "softap", "wl", "eth", "en", "rndis", "usb", "bt-pan", "p2p", "br" };
    for(const char* p : k) if(!strncmp(n, p, strlen(p))) return true;
    return false;
}

std::vector<LanIface> LAN::Interfaces()
{
    std::vector<LanIface> out;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd < 0) return out;

    ifreq reqs[64];
    ifconf ifc; ifc.ifc_len = sizeof(reqs); ifc.ifc_req = reqs;
    if(ioctl(fd, SIOCGIFCONF, &ifc) == 0)
    {
        int n = ifc.ifc_len / (int)sizeof(ifreq);
        for(int i = 0; i < n; ++i)
        {
            sockaddr_in* a = (sockaddr_in*)&reqs[i].ifr_addr;
            if(a->sin_family != AF_INET) continue;
            uint32_t ip = ntohl(a->sin_addr.s_addr);
            if((ip >> 24) == 127 || ip == 0) continue;

            ifreq r; memset(&r, 0, sizeof(r));
            strncpy(r.ifr_name, reqs[i].ifr_name, IFNAMSIZ - 1);
            if(ioctl(fd, SIOCGIFFLAGS, &r) == 0)
            {
                if(!(r.ifr_flags & IFF_UP) || (r.ifr_flags & IFF_LOOPBACK)) continue;
            }

            LanIface f; memset(&f, 0, sizeof(f));
            strncpy(f.name, reqs[i].ifr_name, sizeof(f.name) - 1);
            f.ip = ip;
            f.mask = 0xFFFFFF00u;
            memset(&r, 0, sizeof(r)); strncpy(r.ifr_name, f.name, IFNAMSIZ - 1);
            if(ioctl(fd, SIOCGIFNETMASK, &r) == 0)
            {
                uint32_t m = ntohl(((sockaddr_in*)&r.ifr_netmask)->sin_addr.s_addr);
                if(m) f.mask = m;
            }
            f.bcast = (f.ip & f.mask) | ~f.mask;
            memset(&r, 0, sizeof(r)); strncpy(r.ifr_name, f.name, IFNAMSIZ - 1);
            if(ioctl(fd, SIOCGIFBRDADDR, &r) == 0)
            {
                uint32_t b = ntohl(((sockaddr_in*)&r.ifr_broadaddr)->sin_addr.s_addr);
                if(b && b != f.ip) f.bcast = b;
            }
            f.cellular = !IsLanName(f.name);
            out.push_back(f);
        }
    }
    close(fd);
    return out;
}

bool LAN::HasNetwork()
{
    return !Interfaces().empty();
}

bool LAN::IsOwnAddress(uint32_t ip)
{
    if((ip >> 24) == 127) return true;
    for(auto& f : Interfaces()) if(f.ip == ip) return true;
    return false;
}

bool LAN::IsPrivateAddress(const char* host)
{
    in_addr a;
    if(!host || inet_pton(AF_INET, host, &a) != 1) return false;
    uint32_t ip = ntohl(a.s_addr);
    return (ip >> 24) == 10 || (ip >> 24) == 127 || (ip >> 20) == 0xAC1 || (ip >> 16) == 0xC0A8 ||
           (ip >> 22) == (0x64400000u >> 22);
}

static void SendQuery(int sock, uint32_t ipHost, uint16_t port, uint32_t t)
{
    sockaddr_in d; memset(&d, 0, sizeof(d));
    d.sin_family = AF_INET; d.sin_port = htons(port); d.sin_addr.s_addr = htonl(ipHost);
    PktQuery q; q.type = PKT_QUERY; q.time = t;
    sendto(sock, (const char*)&q, sizeof(q), 0, (sockaddr*)&d, sizeof(d));
}

std::vector<LanServer> LAN::Discover(const std::vector<uint16_t>& ports, int timeoutMs, bool includeSelf, const std::vector<Target>& extra)
{
    std::vector<LanServer> found;
    std::vector<LanIface> ifs = Interfaces();
    if(ifs.empty()) return found;

    int sock = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return found;
    int yes = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
    int rcv = 256 * 1024;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv, sizeof(rcv));
    timeval tv; tv.tv_sec = 0; tv.tv_usec = 100 * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    uint32_t t0 = (uint32_t)MP_NowMs();
    for(uint16_t port : ports)
    {
        SendQuery(sock, 0xFFFFFFFFu, port, t0);              // 255.255.255.255
        for(auto& f : ifs)
        {
            if(f.cellular) continue;                         // never sweep the carrier network
            SendQuery(sock, f.bcast, port, t0);              // subnet broadcast
            uint32_t mask = f.mask;
            if(~mask > 1023u) mask = 0xFFFFFF00u;            // big subnets: sweep our /24 only
            uint32_t net = f.ip & mask, last = net | ~mask;
            for(uint32_t h = net + 1; h < last; ++h)         // unicast sweep (works without broadcast)
                if(h != f.ip) SendQuery(sock, h, port, t0);
        }
        if(includeSelf) SendQuery(sock, 0x7F000001u, port, t0);
    }
    for(int k = 0; k < 3; ++k)                           // UDP: ask the exact servers 3 times
        for(auto& t : extra) SendQuery(sock, t.ip, t.port, t0);
    uint64_t deadline = MP_NowMs() + (uint64_t)timeoutMs;
    uint8_t buf[512];
    while(MP_NowMs() < deadline)
    {
        sockaddr_in from; socklen_t fl = sizeof(from);
        int r = (int)recvfrom(sock, (char*)buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
        if(r <= 0) continue;
        if(r < (int)sizeof(PktInfo) || buf[0] != PKT_INFO) continue;
        uint32_t ip = ntohl(from.sin_addr.s_addr);
        bool own = (ip >> 24) == 127;
        for(auto& f : ifs) if(f.ip == ip) own = true;
        if(own && !includeSelf) continue;

        const PktInfo* in = (const PktInfo*)buf;
        LanServer s; memset(&s, 0, sizeof(s));
        inet_ntop(AF_INET, &from.sin_addr, s.ip, sizeof(s.ip));
        s.port = ntohs(from.sin_port);
        MP_CopyStr(s.name, in->serverName, sizeof(s.name));
        s.players = in->players; s.maxPlayers = in->maxPlayers; s.region = in->region;
        s.proto = in->proto; s.ping = (int)((uint32_t)MP_NowMs() - in->time);
        s.locked = (r >= (int)sizeof(PktInfo2) && ((const PktInfo2*)buf)->locked) ? 1 : 0;   // 2.19
        if(r >= (int)sizeof(PktInfo3)) { uint8_t ic = ((const PktInfo3*)buf)->icon; s.icon = ic <= MP_ICON_MAX ? ic : 0; }   // 2.24
        if(r >= (int)sizeof(PktInfo5)) s.startUnix = ((const PktInfo5*)buf)->startUnix;   // 2.27
        if(r >= (int)sizeof(PktInfo4)) { MP_CopyStr(s.lang, ((const PktInfo4*)buf)->lang, sizeof(s.lang)); MP_CleanLang(s.lang); }   // 2.25
        bool dup = false;
        for(auto& e : found) if(!strcmp(e.ip, s.ip) && e.port == s.port) dup = true;
        if(!dup) found.push_back(s);
    }
    close(sock);
    return found;
}

bool LAN::Resolve(const char* host, uint32_t* out)
{
    if(!host || !*host) return false;
    in_addr a;
    if(inet_pton(AF_INET, host, &a) == 1) { *out = ntohl(a.s_addr); return true; }
    addrinfo hints, *res = nullptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    if(getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
    *out = ntohl(((sockaddr_in*)res->ai_addr)->sin_addr.s_addr);
    freeaddrinfo(res);
    return true;
}

int LAN::PickBest(const std::vector<LanServer>& list, const char* name, uint16_t port)
{
    int best = -1, bestScore = -1;
    for(int i = 0; i < (int)list.size(); ++i)
    {
        const LanServer& s = list[i];
        if(s.port != port || s.proto != MP_PROTO_VERSION) continue;
        int score = s.players;
        if(s.dedicated) score += 500;                            // 2.29: the 24/7 copy beats any phone copy
        if(name && !strcasecmp(s.name, name)) score += 1000;    // same server name wins
        if(score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}

bool MP_IsDuplicateServer(const char* serverName, uint16_t port, int timeoutMs)
{
    for(auto& s : LAN::Discover(port, timeoutMs))
        if(s.port == port && serverName && !strcasecmp(s.name, serverName)) return true;
    return false;
}
