#include <vector>
#include <string>
// Standalone MP server (Linux / Windows / Termux). Same core as the in-game "Host LAN server".
//   g++ -O2 -std=c++17 -pthread -I../src mp_server.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp -o mp_server
//   ./mp_server [port] [server name] [max players 1-100] [region: Asia|Europe|NA|Oceania] [--persistent] [--no-online]
//               [--broker-file broker.txt] [--password pw] [--admin-hash sha256hex] [--no-safezones] [--auth]
//               3.04: Brief Case War 24/7: ./mp_server 1930 "Brief Case War" 20 Asia --persistent [--bcw-min 20]
//               3.25: Mega Vil Futuristic (Roleplay) 24/7: ./mp_server 1932 "Mega Vil Futuristic" 250 Asia --persistent [--rp-file rp_citizens.txt]
//               3.23: MW3 24/7: ./mp_server 1931 "Call Of Duty: Modern Warfare 3" 64 Asia --persistent [--mw3-file mw3_stats.txt]
//               3.04.6: [--outdated-log file] (default outdated_clients.log): phones with an old MP version
//               are refused + written there. --legacy-notice: run in the OLD lobby (thirtyfox/mp4/, what
//               MP 3.04.5 and older use) as "UPDATE MP TO <version>": every join is refused with the
//               download link and logged (old phones can't see the new lobby at all)
//               3.01: [--motd "text"] [--motd-file motd.txt] [--banner "text"] [--turf-file turf.txt] [--no-turf]
//   --motd / --motd-file: SA-MP style welcome dialog on join ({RRGGBB} colours, \n = new line)
//   --banner: small textdraw bottom right (your site / Discord), --turf-file: gang zone owners kept
//   across restarts (default turf.txt), --no-turf: no turf wars (zones stay with their gangs)
//   2.20: broker.txt may list one broker per region ([Asia] host=... blocks); the server then uses
//   the broker of its own region (e.g. the HiveMQ broker on the same VPS: host=127.0.0.1).
//   Without --no-online the server is also listed in the internet lobby (MQTT relay), so phones
//   on mobile data can join it without opening any port.
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <signal.h>
#include <atomic>
#include <thread>
#include <chrono>
#include "net/server.h"
#include "net/lan.h"
#include "net/online.h"

static std::atomic<bool> g_bQuit(false);
static void OnSignal(int) { g_bQuit = true; }

int main(int argc, char** argv)
{
    int port = argc > 1 ? atoi(argv[1]) : MP_DEFAULT_PORT;
    const char* name = argc > 2 ? argv[2] : (port == MP_DEFAULT_PORT ? MP_DEFAULT_SERVER_NAME : (port == MP_BCW_PORT ? MP_BCW_SERVER_NAME : (port == MP_MW3_PORT ? MP_MW3_SERVER_NAME : (port == MP_RP_PORT ? MP_RP_SERVER_NAME : "MP Server"))));   // 2.29: port 1922 = the default server, 3.04: 1930 = Brief Case War
    int maxPlayers = argc > 3 ? atoi(argv[3]) : MP_STD_MAX_PLAYERS;
    int region = REGION_ASIA;
    bool persistent = false;

    for(int i = 1; i < argc; ++i)
    {
        if(!strcasecmp(argv[i], "--persistent")) persistent = true;
        else if(i == 4) for(int r = 0; r < REGION_COUNT; ++r) if(!strcasecmp(argv[i], g_MPRegionNames[r])) region = r;
    }

    signal(SIGINT, OnSignal);
    signal(SIGTERM, OnSignal);

    bool legacy = false;   // 3.04.6
    for(int i = 1; i < argc; ++i) if(!strcasecmp(argv[i], "--legacy-notice")) legacy = true;
    if(legacy)
    {
        Online::UseLegacyTopics();
        static char nm[32]; snprintf(nm, sizeof(nm), "UPDATE MP TO %s", MP_VERSION_STR);
        if(argc <= 2 || argv[2][0] == '-') name = nm;
        persistent = true;
    }
    bool offline = false;
    for(int i = 1; i < argc; ++i) if(!strcasecmp(argv[i], "--no-online")) offline = true;
    const char* brokerFile = "broker.txt";
    for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--broker-file")) brokerFile = argv[i + 1];
    int icon = 0;   // 2.24: --icon N = server icon 1..250 (0 = default main_df_icon.png)
    for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--icon")) icon = atoi(argv[i + 1]);
    if(icon < 0 || icon > MP_ICON_MAX) icon = 0;
    Online::SetHostIcon(icon);
    Online::SetRegionPrefs(region, false);   // 2.20: regional brokers: this region's broker + default server
    if(!offline) Online::Start(brokerFile);
    bool dupOnline = false;
    if(!offline && Online::WaitReady(8000))
        for(auto& sv : Online::Snapshot()) if(sv.port == port && !strcasecmp(sv.name, name)) dupOnline = true;
    // 2.29: a 24/7 (--persistent) server never gives up because a PHONE runs a copy of the same
    // server: it starts anyway and the phones move to it (it is announced as dedicated).
    if(persistent) { Online::SetHostDedicated(true); dupOnline = false; }
    if(dupOnline)
    {
        printf("[MP Server] \"%s\" (port %d) is already online in the internet lobby. Join it instead.\n", name, port);
        return 2;
    }
    if(!persistent && MP_IsDuplicateServer(name, (uint16_t)port))
    {
        printf("[MP Server] A server named \"%s\" is already running on LAN port %u.\n", name, port);
        printf("[MP Server] Join the existing server instead of creating a duplicate.\n");
        return 2;
    }

    MPServer server;
    server.SetPersistent(persistent);
    // 3.21.6: --auth = login system (Discord verification + account) on a custom server too.
    // The default server and Brief Case War always have it.
    for(int i = 1; i < argc; ++i) if(!strcasecmp(argv[i], "--auth")) server.SetAuth(true);
    for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--password")) server.SetPassword(argv[i + 1]);   // 2.19 (max 16 chars)
    if(!server.Start((uint16_t)port, name, nullptr, maxPlayers, region)) return 1;
    server.SetIcon(icon);   // 2.24
    {   // 3.04.6: old MP versions are refused and logged
        const char* ol = "outdated_clients.log";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--outdated-log")) ol = argv[i + 1];
        server.SetOutdatedLog(ol);
        if(legacy) server.SetMinBuild(0xFFFFFFFFu);   // the notice server lets nobody in
        printf("[MP Server] MP %s required (build %u); old versions refused + logged to %s%s\n", MP_VERSION_STR, (unsigned)MP_MIN_BUILD, ol, legacy ? " - LEGACY NOTICE SERVER (old lobby)" : "");
    }
    // 3.04: Brief Case War. --bcw-min N = players in the team queue that start a match (default 20 = 10 vs 10)
    if(server.IsBcwServer())
    {
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--bcw-min")) server.SetBcwMinPlayers(atoi(argv[i + 1]));
        printf("[MP Server] Brief Case War: %d players max (10 per team), cheats off, CheatMenu refused\n", MP_BCW_MAX_PLAYERS);
    }
    // 3.23: Call Of Duty: Modern Warfare 3. --mw3-file PATH = stats + zone owners (default mw3_stats.txt)
    if(server.IsMw3Server())
    {
        const char* mf = "mw3_stats.txt";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--mw3-file")) mf = argv[i + 1];
        server.SetMw3File(mf);
        printf("[MP Server] MW3: %d players max, 8 teams, 34 zones, login on, cheats off, MP 3.23+ only, stats in %s\n", MP_MW3_MAX_PLAYERS, mf);
    }
    // 3.25: Mega Vil Futuristic. --rp-file PATH = citizens (money, jobs, houses, phone numbers)
    if(server.IsRpServer())
    {
        const char* rf = "rp_citizens.txt";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--rp-file")) rf = argv[i + 1];
        server.SetRpFile(rf);
        printf("[MP Server] Mega Vil Futuristic: Roleplay only, %d players max, login on, cheats off, peaceful (police/military on duty only), citizens in %s\n", MP_RP_MAX_PLAYERS, rf);
    }
    // 3.39: Home Kit houses. --home-file PATH (default homes.txt; Mega Vil: homes_rp.txt)
    {
        const char* hf = server.IsRpServer() ? "homes_rp.txt" : "homes.txt";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--home-file")) hf = argv[i + 1];
        server.SetHomeFile(hf);
        // 3.39.1: saved progress (place, money, weapons, health, hunger). --progress-file PATH
        const char* pf = "progress.txt";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--progress-file")) pf = argv[i + 1];
        server.SetProgressFile(pf);
    }
    // 3.39: --rules "Title|rule 1|rule 2|..." = the rules window after the login (custom servers)
    for(int i = 1; i + 1 < argc; ++i)
        if(!strcasecmp(argv[i], "--rules"))
        {
            static char buf[1200]; MP_CopyStr(buf, argv[i + 1], sizeof(buf));
            const char* parts[11] = {0}; int n = 0; char* p = buf;
            while(p && n < 11) { parts[n++] = p; char* bar = strchr(p, '|'); if(bar) { *bar = 0; p = bar + 1; } else p = nullptr; }
            server.SetRules(n > 1, parts[0], parts + 1);
        }
    // 3.00: --cheat-menu = players may use CheatMenu (libCheatMenu.so / libCheatMenu64.so) here.
    // Custom servers only: the default server ignores it.
    for(int i = 1; i < argc; ++i)
        if(!strcasecmp(argv[i], "--cheat-menu"))
        {
            if(server.IsDefaultServer()) printf("[MP Server] --cheat-menu ignored: never allowed on the default server.\n");
            else { server.SetPerms(PERM_MISSION_PAIR | PERM_CHEAT_MENU); printf("[MP Server] Cheat Menu allowed on this server.\n"); }
        }
    // 3.02: gamemodes. The default server always has all of them; a custom server has none until
    // its owner lists them: --gamemodes all | none | class,turf,race,derby,dm,jobs,labels,actors
    for(int i = 1; i + 1 < argc; ++i)
        if(!strcasecmp(argv[i], "--gamemodes"))
        {
            static const struct { const char* n; uint16_t m; } kModes[] =
            {
                { "class", GM_CLASS }, { "turf", GM_TURF }, { "race", GM_RACE }, { "derby", GM_DERBY }, { "dm", GM_DM },
                { "jobs", GM_JOBS }, { "labels", GM_LABELS }, { "actors", GM_ACTORS },
                { "home", GM_HOME }, { "survival", GM_SURVIVAL }, { "cnr", GM_CNR }, { "ad", GM_AD }, { "stunt", GM_STUNT }, { "drift", GM_DRIFT },   // 3.39
                { "icons", 0 }, { "bubbles", 0 },   // 3.03: player settings now, accepted and ignored
            };
            uint16_t m = 0;
            char list[256]; MP_CopyStr(list, argv[i + 1], sizeof(list));
            for(char* t = strtok(list, ", "); t; t = strtok(nullptr, ", "))
            {
                if(!strcasecmp(t, "all")) { m = GM_ALL; continue; }
                if(!strcasecmp(t, "none")) { m = 0; continue; }
                bool found = false;
                for(auto& k : kModes) if(!strcasecmp(t, k.n)) { m |= k.m; found = true; }
                if(!found) printf("[MP Server] --gamemodes: unknown \"%s\" (class turf race derby dm jobs labels actors home survival cnr ad stunt drift)\n", t);
            }
            server.SetModes(m);
        }
    if(server.IsDefaultServer()) printf("[MP Server] Gamemodes: all (default server).\n");
    else
    {
        static const char* kNames[] = { "class", "turf", "race", "derby", "dm", "jobs", "labels", "icons", "actors", "bubbles", "home", "survival", "cnr", "ad", "stunt", "drift" };
        std::string on; for(int b = 0; b < 16; ++b) if((server.GetModes() & (1 << b)) && (1 << b) != GM_ICONS && (1 << b) != GM_BUBBLES) { if(!on.empty()) on += ","; on += kNames[b]; }
        printf("[MP Server] Gamemodes: %s%s\n", on.empty() ? "none" : on.c_str(), on.empty() ? " (add --gamemodes all, or a list)" : "");
    }
    // 2.25: language, administrator name (shown to kicked / banned players), ban list
    const char* admin = "Server";
    for(int i = 1; i + 1 < argc; ++i)
    {
        if(!strcasecmp(argv[i], "--lang"))  { server.SetLang(argv[i + 1]); Online::SetHostLang(argv[i + 1]); }
        if(!strcasecmp(argv[i], "--admin")) admin = argv[i + 1];
        if(!strcasecmp(argv[i], "--admin-hash")) server.SetAdminHash(argv[i + 1]);
        if(!strcasecmp(argv[i], "--mods")) server.SetModFile(argv[i + 1]);   // 3.35: moderator list (default ./moderator.txt)   // 2.37: SHA-256 of "wagu-admin-v1:" + key
    }
    {
        const char* bf = "bans.txt";
        for(int i = 1; i + 1 < argc; ++i) if(!strcasecmp(argv[i], "--bans")) bf = argv[i + 1];
        server.SetBanFile(bf);
    }
    {   // 3.01: SA-MP features
        const char* tf = "turf.txt"; std::string motd; bool turf = true;
        for(int i = 1; i < argc; ++i) if(!strcasecmp(argv[i], "--no-turf")) turf = false;
        for(int i = 1; i < argc; ++i) if(!strcasecmp(argv[i], "--no-safezones")) { server.SetSafeZones(false); printf("[MP Server] Hospital safe zones: off\n"); }   // 3.21
        for(int i = 1; i + 1 < argc; ++i)
        {
            if(!strcasecmp(argv[i], "--turf-file")) tf = argv[i + 1];
            if(!strcasecmp(argv[i], "--motd")) motd = argv[i + 1];
            if(!strcasecmp(argv[i], "--banner")) server.SetBanner(argv[i + 1]);
            if(!strcasecmp(argv[i], "--motd-file"))
            {
                if(FILE* f = fopen(argv[i + 1], "r")) { char b[1024]; size_t n = fread(b, 1, sizeof(b) - 1, f); b[n] = 0; fclose(f); motd = b; }
                else printf("[MP Server] can't read %s\n", argv[i + 1]);
            }
        }
        if(motd.empty() && server.IsDefaultServer())
            motd = "{FFFFFF}Welcome to {FFD21E}" MP_DEFAULT_SERVER_NAME "{FFFFFF}!\n\n"
                   "{A9C4E4}/class{FFFFFF}  pick a gang (Grove, Ballas, Vagos, Aztecas, Rifa, Da Nang) or Police\n"
                   "{A9C4E4}/turf{FFFFFF}  gang zones: stand in an enemy zone for 60 s to take it\n"
                   "{A9C4E4}/race{FFFFFF}  races with real positions   {A9C4E4}/derby{FFFFFF}  last car standing wins\n"
                   "{A9C4E4}/job{FFFFFF}  jobs   {A9C4E4}/dm{FFFFFF}  deathmatch   {A9C4E4}/help{FFFFFF}  everything else";
        for(size_t k; (k = motd.find("\\n")) != std::string::npos; ) motd.replace(k, 2, "\n");
        if(!motd.empty()) server.SetMotd(nullptr, motd.c_str());
        server.SetTurfWars(turf);
        server.SetTurfFile(tf);
    }
    // console: players | kick <id> [reason] | ban <id> [reason] | banname <name> [reason] | bans | unban <n>
    std::thread([&server, admin]()
    {
        char line[256];
        while(!g_bQuit && fgets(line, sizeof(line), stdin))
        {
            line[strcspn(line, "\r\n")] = 0;
            char cmd[16] = {0}; int n = 0; sscanf(line, "%15s %n", cmd, &n);
            const char* rest = line + n;
            if(!strcasecmp(cmd, "players"))
            {
                std::vector<MPServer::PlayerView> pv; server.GetPlayers(pv);
                printf("[MP Server] %d player(s)\n", (int)pv.size());
                for(auto& v : pv) printf("  %3d  %-24s  %s\n", v.id, v.name, v.uid[0] ? v.uid : "(old version)");
            }
            else if(!strcasecmp(cmd, "kick") || !strcasecmp(cmd, "ban"))
            {
                int id = -1, m = 0; sscanf(rest, "%d %n", &id, &m);
                bool ok = id >= 0 && server.Kick(id, admin, rest + m, !strcasecmp(cmd, "ban"));
                printf("[MP Server] %s\n", ok ? "done" : "no player with that id (type: players)");
            }
            else if(!strcasecmp(cmd, "banname"))
            {
                char nm[MP_NAME_LEN] = {0}; int m = 0; sscanf(rest, "%23s %n", nm, &m);
                printf("[MP Server] %s\n", server.AddBan(nm, "", admin, rest + m) ? "banned" : "already banned / no name");
            }
            else if(!strcasecmp(cmd, "bans"))
            {
                std::vector<MPServer::BanEntry> b; server.GetBans(b);
                for(size_t i = 0; i < b.size(); ++i) printf("  %2d  %-24s  by %s  (%s)\n", (int)i, b[i].name[0] ? b[i].name : b[i].uid, b[i].admin, b[i].reason);
                if(b.empty()) printf("[MP Server] nobody is banned\n");
            }
            else if(!strcasecmp(cmd, "unban")) printf("[MP Server] %s\n", server.Unban(atoi(rest)) ? "unbanned" : "no ban with that number (type: bans)");
            else if(cmd[0]) printf("[MP Server] commands: players | kick <id> [reason] | ban <id> [reason] | banname <name> [reason] | bans | unban <n>\n");
            fflush(stdout);
        }
    }).detach();
    printf("[MP Server] Press Ctrl+C to stop. Region: %s, max players: %d, protocol v%d%s\n",
           g_MPRegionNames[server.GetRegion()], server.GetMaxPlayers(), MP_PROTO_VERSION,
           persistent ? ", persistent" : "");

    while(!g_bQuit)
    {
        // internet lobby: phones on any network see and join this server through the relay
        if(!offline) { Online::SetHostStart(server.GetStartUnix()); Online::SetHosted(true, server.GetName(), server.GetPort(), server.GetPlayerCount(), server.GetMaxPlayers(), server.GetRegion()); }   // 2.27: + start time
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    server.Stop();
    if(!offline) { Online::SetHosted(false, "", 0, 0, 0, 0); std::this_thread::sleep_for(std::chrono::milliseconds(500)); }
    return 0;
}
