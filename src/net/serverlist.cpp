#include "serverlist.h"
#include "server.h" // MP_NowMs
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include "lan.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>

#define QUERY_TIMEOUT_MS 3000

static std::vector<ServerEntry> s_List;
static char     s_szPath[512] = "";
static int      s_Sock = -1;
static uint64_t s_nLastRefresh = 0;
static bool     s_bDirty = false;   // duplicates removed while loading -> rewrite the file

static void InitEntry(ServerEntry& e)
{
    e.resolved = false; memset(&e.addr, 0, sizeof(e.addr));
    e.online = false; e.players = -1; e.maxPlayers = 0; e.ping = -1; e.querySent = 0;
}

// The default server is not stored in the list: it always sits on top with the Connect button.
static bool IsDefaultEntry(const char* host, const char* name, int port)
{
    if(!strcmp(host, "127.0.0.1") || !strcmp(host, "localhost")) return true;
    return port == MP_DEFAULT_PORT && name && !strcasecmp(name, MP_DEFAULT_SERVER_NAME);
}

// Host names ("ggwar", "net.ggwar.org") are looked up on a background thread so a slow or
// unknown name never freezes the game. Results are cached for 2 minutes.
struct DnsEntry { bool pending; bool ok; uint32_t ip; uint64_t time; };
static std::mutex s_DnsMutex;
static std::map<std::string, DnsEntry> s_Dns;

static bool Resolve(ServerEntry& e)
{
    sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port = htons((uint16_t)e.port);
    if(inet_pton(AF_INET, e.host, &a.sin_addr) != 1)
    {
        std::string h = e.host;
        uint64_t now = MP_NowMs();
        std::lock_guard<std::mutex> lk(s_DnsMutex);
        auto it = s_Dns.find(h);
        if(it == s_Dns.end() || (!it->second.pending && now - it->second.time > 120000))
        {
            s_Dns[h] = { true, false, 0, now };
            std::thread([h]()
            {
                uint32_t ip = 0;
                bool ok = LAN::Resolve(h.c_str(), &ip);
                std::lock_guard<std::mutex> lk2(s_DnsMutex);
                s_Dns[h] = { false, ok, ip, MP_NowMs() };
            }).detach();
            return false;
        }
        if(it->second.pending || !it->second.ok) return false;
        a.sin_addr.s_addr = htonl(it->second.ip);
    }
    e.addr = a; e.resolved = true;
    return true;
}

void ServerList::Load(const char* path)
{
    MP_CopyStr(s_szPath, path, sizeof(s_szPath));
    s_List.clear();
    FILE* f = fopen(path, "r");
    if(f)
    {
        char line[256];
        while(fgets(line, sizeof(line), f) && (int)s_List.size() < MP_MAX_SERVERS)
        {
            // region|port|host|name[|password]   (2.19: optional password)
            line[strcspn(line, "\r\n")] = 0;
            if(!line[0] || line[0] == '#') continue;
            char* p[6] = { line, nullptr, nullptr, nullptr, nullptr, nullptr };   // 2.25: |language
            for(int i = 1; i < 6; ++i) { p[i] = p[i - 1] ? strchr(p[i - 1], '|') : nullptr; if(p[i]) *p[i]++ = 0; }
            if(!p[3]) continue;
            ServerEntry e; memset(&e, 0, sizeof(e)); InitEntry(e);
            e.region = atoi(p[0]); if(e.region < 0 || e.region >= REGION_COUNT) e.region = REGION_ASIA;
            e.port = atoi(p[1]); if(e.port <= 0 || e.port > 65535) e.port = MP_DEFAULT_PORT;
            MP_CopyStr(e.host, p[2], sizeof(e.host));
            MP_CopyStr(e.name, p[3][0] ? p[3] : "Server", sizeof(e.name));
            if(p[4]) MP_CopyStr(e.password, p[4], sizeof(e.password));
            if(p[5]) { MP_CopyStr(e.lang, p[5], sizeof(e.lang)); MP_CleanLang(e.lang); }
            if(!e.host[0] || IsDefaultEntry(e.host, e.name, e.port)) continue;
            // drop duplicates (same host + port, or same name + port): each server once
            bool dup = false;
            for(auto& o : s_List)
                if(o.port == e.port && (!strcasecmp(o.host, e.host) || !strcasecmp(o.name, e.name))) dup = true;
            if(dup) { s_bDirty = true; continue; }
            s_List.push_back(e);
        }
        fclose(f);
    }

    if(s_bDirty) { s_bDirty = false; Save(); }
}

void ServerList::Save()
{
    if(!s_szPath[0]) return;
    FILE* f = fopen(s_szPath, "w");
    if(!f) return;
    fprintf(f, "# MP server list (max %d). Format: region|port|host|name[|password[|language]]  (region 0=Asia 1=Europe 2=NA 3=Oceania)\n", MP_MAX_SERVERS);
    for(auto& e : s_List)
    {
        if(e.lang[0])       fprintf(f, "%d|%d|%s|%s|%s|%s\n", e.region, e.port, e.host, e.name, e.password, e.lang);
        else if(e.password[0]) fprintf(f, "%d|%d|%s|%s|%s\n", e.region, e.port, e.host, e.name, e.password);
        else              fprintf(f, "%d|%d|%s|%s\n", e.region, e.port, e.host, e.name);
    }
    fclose(f);
}

int ServerList::Count() { return (int)s_List.size(); }
ServerEntry* ServerList::Get(int i) { return (i >= 0 && i < (int)s_List.size()) ? &s_List[i] : nullptr; }
int ServerList::CountInRegion(int r) { int n = 0; for(auto& e : s_List) if(e.region == r) ++n; return n; }
uint64_t ServerList::LastRefreshMs() { return s_nLastRefresh; }

void ServerList::SetPassword(int i, const char* password)
{
    if(i < 0 || i >= (int)s_List.size()) return;
    char pw[MP_PASSWORD_LEN + 1]; MP_CopyStr(pw, password ? password : "", sizeof(pw));
    for(char* c = pw; *c; ++c) if(*c == '|') *c = '/';
    if(strcmp(pw, s_List[i].password)) { memcpy(s_List[i].password, pw, sizeof(pw)); Save(); }
}

bool ServerList::Add(const char* name, const char* host, int port, int region, const char* password, const char* lang)
{
    if((int)s_List.size() >= MP_MAX_SERVERS || !host || !*host || port <= 0 || port > 65535) return false;
    if(IsDefaultEntry(host, name, port)) return false;
    for(auto& o : s_List)
        if(o.port == port && (!strcasecmp(o.host, host) || (name && *name && !strcasecmp(o.name, name)))) return false; // already created
    ServerEntry e; memset(&e, 0, sizeof(e)); InitEntry(e);
    // '|' would break the file format
    char nm[32]; MP_CopyStr(nm, (name && *name) ? name : host, sizeof(nm));
    for(char* c = nm; *c; ++c) if(*c == '|') *c = '/';
    MP_CopyStr(e.name, nm, sizeof(e.name));
    MP_CopyStr(e.host, host, sizeof(e.host));
    for(char* c = e.host; *c; ++c) if(*c == '|' || *c == ' ') *c = 0;
    e.port = port;
    e.region = (region >= 0 && region < REGION_COUNT) ? region : REGION_ASIA;
    if(password) { MP_CopyStr(e.password, password, sizeof(e.password)); for(char* c = e.password; *c; ++c) if(*c == '|') *c = '/'; }
    if(lang) { MP_CopyStr(e.lang, lang, sizeof(e.lang)); MP_CleanLang(e.lang); }   // 2.25
    s_List.push_back(e);
    Save();
    return true;
}

void ServerList::AddOrUpdate(const char* name, const char* host, int port, int region)
{
    if(!host || !*host || !name || IsDefaultEntry(host, name, port)) return;
    if(region < 0 || region >= REGION_COUNT) region = REGION_ASIA;
    for(auto& e : s_List)
    {
        if(e.port == port && !strcmp(e.host, host))
        {
            if(strcmp(e.name, name) || e.region != region)
            {
                MP_CopyStr(e.name, name, sizeof(e.name));
                for(char* c = e.name; *c; ++c) if(*c == '|') *c = '/';
                e.region = region; Save();
            }
            return;
        }
    }
    // same name + port = the same custom server (keep the host/IP the user typed, e.g. "ggwar")
    for(auto& e : s_List) if(e.port == port && !strcasecmp(e.name, name)) return;
    Add(name, host, port, region);
}

int ServerList::FindByHostPort(const char* host, int port)
{
    for(int i = 0; i < (int)s_List.size(); ++i) if(s_List[i].port == port && host && !strcasecmp(s_List[i].host, host)) return i;
    return -1;
}

int ServerList::FindByNamePort(const char* name, int port)
{
    for(int i = 0; i < (int)s_List.size(); ++i) if(s_List[i].port == port && name && !strcasecmp(s_List[i].name, name)) return i;
    return -1;
}

void ServerList::Remove(int i)
{
    if(i < 0 || i >= (int)s_List.size()) return;
    s_List.erase(s_List.begin() + i);
    Save();
}

void ServerList::Refresh()
{
    if(s_Sock < 0)
    {
        s_Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(s_Sock < 0) return;
        fcntl(s_Sock, F_SETFL, fcntl(s_Sock, F_GETFL, 0) | O_NONBLOCK);
    }
    uint64_t now = MP_NowMs();
    s_nLastRefresh = now;
    for(auto& e : s_List)
    {
        if(!e.resolved && !Resolve(e)) { e.online = false; e.players = -1; e.ping = -1; e.querySent = 0; continue; } // name lookup still running / unknown name
        PktQuery q; q.type = PKT_QUERY; q.time = (uint32_t)now;
        sendto(s_Sock, &q, sizeof(q), 0, (sockaddr*)&e.addr, sizeof(e.addr));
        e.querySent = now;
    }
}

void ServerList::Poll()
{
    uint64_t now = MP_NowMs();
    if(s_Sock >= 0)
    {
        uint8_t buf[256];
        for(int guard = 0; guard < 128; ++guard)
        {
            sockaddr_in from; socklen_t fl = sizeof(from);
            int r = (int)recvfrom(s_Sock, buf, sizeof(buf), 0, (sockaddr*)&from, &fl);
            if(r <= 0) break;
            if(buf[0] != PKT_INFO || r < (int)sizeof(PktInfo)) continue;
            const PktInfo* in = (const PktInfo*)buf;
            for(auto& e : s_List)
            {
                if(!e.resolved || e.addr.sin_addr.s_addr != from.sin_addr.s_addr || e.addr.sin_port != from.sin_port) continue;
                e.online = (in->proto == MP_PROTO_VERSION);
                e.players = in->players; e.maxPlayers = in->maxPlayers;
                e.ping = (int)((uint32_t)now - in->time);
                e.locked = r >= (int)sizeof(PktInfo2) && ((const PktInfo2*)buf)->locked;   // 2.19
                if(r >= (int)sizeof(PktInfo4)) { MP_CopyStr(e.liveLang, ((const PktInfo4*)buf)->lang, sizeof(e.liveLang)); MP_CleanLang(e.liveLang); }   // 2.25
                e.icon = r >= (int)sizeof(PktInfo3) ? (((const PktInfo3*)buf)->icon <= MP_ICON_MAX ? ((const PktInfo3*)buf)->icon : 0) : 0;   // 2.24
                e.querySent = 0;
            }
        }
    }
    for(auto& e : s_List)
        if(e.querySent && now - e.querySent > QUERY_TIMEOUT_MS) { e.online = false; e.players = -1; e.ping = -1; e.querySent = 0; }
}
