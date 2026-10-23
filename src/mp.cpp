// MP core: networking events, local player sync, remote player avatars, combat, chat, menus.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <math.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>
#include "mp.h"
#include "progress/progress.h"
#include "net/server.h"
#include "net/serverlist.h"
#include "net/lan.h"
#include "net/online.h"
#include <map>
#include <algorithm>
#include <string>
#include "audio/sound.h"
#include "audio/voice.h"   // 3.11
#include <set>
#include <time.h>
#include "ui/hud.h"
#include "ui/floatmenu.h"
#include "ui/vkeyboard.h"
#include "ui/ui_common.h"
#include "game/cleoblock.h"
#include "game/modscan.h"
#include "net/admin.h"
#include "ui/emoji.h"
#include <ctype.h>
#include <strings.h>
#include <float.h>

// Grove Street (in front of CJ's house)
// 2.16: middle of the Grove Street cul-de-sac. The old spot (2495, -1686) was CJ's front
// door, where the story starts a mission/cutscene as soon as you stand there.
static const Vec3  kGroveStreet = { 2487.60f, -1667.40f, 13.40f };
static const float kGroveHeading = 180.0f;

// SA-MP style colours (RGBA)
#define COL_SYSTEM  0xA9C4E4FF
#define COL_WHITE   0xFFFFFFFF
#define COL_GREEN   0x33CC55FF
#define COL_RED     0xFF4444FF
#define COL_YELLOW  0xFFDD33FF

namespace MP
{
    MPSettings   settings;
    RemotePlayer players[MP_MAX_PLAYERS];
    std::deque<ChatLine>  chat;
    std::deque<KillEntry> kills;
    MPClient     client;

    uint8_t  myId = MP_INVALID_ID;
    Vec3     myDrawPos = { 0, 0, 0 };
    bool     myDrawValid = false;
    bool     classSelect = false;   // 3.01
    int      myHealth = 100, myArmour = 0;
    uint32_t myColor = 0xFFFFFFFF;
    uint64_t logoStartMs = 0;
}

// Read 3 floats from a packed (possibly unaligned) wire struct. The parameter is const void*, so
// the compiler can't assume 4-byte alignment and never emits VLDR/LDRD on it (ARMv7 SIGBUS).
static inline Vec3 MP_LoadVec3(const void* src)
{
    Vec3 v; memcpy(&v, src, sizeof(v)); return v;
}

// ---- 100 players: avatar streaming (2.11) --------------------------------------------------
// The game's ped / vehicle pools can't hold 99 extra network peds + cars, so only the nearest
// players get a real avatar. Everyone stays in the player list, chat, kill feed and Tab list.
#define MP_STREAM_MAX       32        // max remote avatars at once
#define MP_STREAM_IN        250.0f    // spawn an avatar inside this range (metres)
#define MP_STREAM_OUT       290.0f    // remove it outside this range (hysteresis)
static bool s_bStreamed[MP_MAX_PLAYERS];

static MPServer   s_Server;
static char       s_szDataDir[400] = "";
static char       s_szLanIp[64] = "";
static uint64_t   s_nLastSync = 0;
static uint32_t   s_nSeq = 0;

// local combat state
static int        s_nPrevLocalHealth = -1;
static float      s_fPendingDamage = 0;
static uint8_t    s_nLastDamager = MP_INVALID_ID, s_nLastDamageWeapon = 255;
static uint64_t   s_nLastDamageTime = 0;
static bool       s_bLocalDead = false;
static bool       s_bWantTeleport = false;
static bool       JoinFlowActive();
static void       JoinMarkConnected();

// ---- LAN search / auto host / host takeover -------------------------------------------------
// One background search at a time (it blocks ~1.5 s, so it never runs on the game thread).
enum eLanJob { JOB_NONE = 0, JOB_HOST, JOB_CONNECT, JOB_TAKEOVER, JOB_DUPCHECK, JOB_SCAN, JOB_JOIN };
static std::atomic<int>       s_nJob{JOB_NONE};
static std::atomic<bool>      s_bJobDone{false};
static bool                   s_bJobAutoConnect = false;
static std::mutex             s_LanMutex;
static std::vector<LanServer> s_JobResult;           // guarded by s_LanMutex
static std::vector<LanServer> s_Nearby;              // last search result (Servers tab), guarded
static uint32_t               s_nNearbyVersion = 0;  // guarded
static uint64_t               s_nNearbyTime = 0;
static std::atomic<bool>      s_bHasNetwork{true};
static uint64_t               s_nLastNetCheck = 0, s_nLastDupCheck = 0;
static bool                   s_bTakeoverPending = false;
static uint64_t               s_nTakeoverAt = 0;
static int                    s_nLostPort = MP_DEFAULT_PORT;
static bool                   s_bConnectingDefault = false;  // current connection is the default server
static char                   s_szJobName[32] = "";          // server the running job is about
static int                    s_nJobPort = MP_DEFAULT_PORT;
struct QueuedJob { int job = JOB_NONE; bool autoConnect = false; char name[32] = ""; int port = 0; char host[64] = ""; };
static char                   s_szJobHost[64] = "";          // custom server host/IP the job also asks
static char                   s_szJobResolved[16] = "";      // that host resolved (guarded by s_LanMutex)
static QueuedJob              s_Queued;                      // waits for a background scan to finish
static char                   s_szLostServerName[32] = "";
static bool OnLostServer();   // defined below; true when a takeover search was scheduled
static void ProcessLan(uint64_t now);

using namespace MP;

// ------------------------------------------------------------------------------------------
const char* MP::DataDir() { return s_szDataDir; }
bool MP::IsConnected() { return client.GetState() == NET_CONNECTED; }
bool MP::IsHosting() { return s_Server.IsRunning(); }
// 2.28: the ping shown for ME. The host's game talks to the server on its own phone (127.0.0.1),
// so the measured round trip is honestly 0-1 ms - no network in between. That told nobody how
// good the host's connection is. A host now shows its real network leg: the round trip to the
// online relay (HiveMQ broker) the joiners' packets travel through, or - on Wi-Fi / LAN - the
// players' average ping to it. Joiners keep their measured ping.
int MP::DisplayPing()
{
    int measured = client.GetPing();
    if(!s_Server.IsRunning() || client.GetState() != NET_CONNECTED) return measured;
    const char* h = client.GetHost();
    bool loopback = h && (!strncmp(h, "127.0.0.", 8) || !strcmp(h, "localhost")) && !Online::IsRelayIp(h);
    if(!loopback || client.GetPort() != s_Server.GetPort()) return measured;   // joined someone else's server
    if(Online::IsConnected())
    {
        int rtt = Online::BrokerRttMs();
        if(rtt > 0 && rtt < 5000) return rtt;
    }
    int sum = 0, n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        if(i != (int)myId && players[i].active && players[i].ping > 0 && players[i].ping < 2000) { sum += players[i].ping; ++n; }
    if(n) return (sum + n / 2) / n;
    return measured;
}
int  MP::HostedPlayerCount() { return s_Server.GetPlayerCount(); }
int  MP::OnlineCount()
{
    if(!IsConnected()) return 0;
    int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active) ++n;
    return n ? n : 1;
}
int  MP::OnlineMax()
{
    if(IsConnected() && client.GetMaxPlayers() > 0) return client.GetMaxPlayers();
    return settings.maxPlayers;
}

static void PushChat(const ChatLine& l)
{
    chat.push_back(l);
    chat.back().t = MP_NowMs();   // 2.19.6: for the chat fade
    {   // 3.11: chat time stamp (local clock)
        time_t tt = time(nullptr); struct tm lt; localtime_r(&tt, &lt);
        snprintf(chat.back().stamp, sizeof(chat.back().stamp), "[%02d:%02d:%02d]", lt.tm_hour, lt.tm_min, lt.tm_sec);
    }
    while(chat.size() > 100) chat.pop_front();
}

void MP::AddSystemMessage(uint32_t rgba, const char* text)
{
    PushChat({ "", 0, text ? text : "", rgba });
}

static void SysMsg(uint32_t col, const char* fmt, ...)
{
    char buf[256];
    va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
    MP::AddSystemMessage(col, buf);
}

static void ServerLog(const char* msg) { logger->Info("[Server] %s", msg); }

// ------------------------------------------------------------------------------------------
// Server resources, listed in the chat when you join any server (default or custom):
//   : serverworld_host resources
//   : server_combat system resources
// plus 123 more built-in resources (125 total, 2.21: +35). Other mods can still add their own with IMPMenu::AddResource.
#define MP_BUILTIN_RESOURCES 125
#define MP_MAX_RESOURCES     (MP_BUILTIN_RESOURCES + 32)   // 125 built in + 32 from other mods
static std::vector<std::string> s_Resources =
{
    "serverworld_host",
    "server_combat system",
    "server_player sync",
    "server_vehicle sync",
    "server_weapon sync",
    "server_damage system",
    "server_death system",
    "server_kill feed",
    "server_chat system",
    "server_name tags",
    "server_health bars",
    "server_armour bars",
    "server_radar icons",
    "server_player colors",
    "server_pause system",
    "server_heartbeat",
    "server_ping system",
    "server_spawn system",
    "server_grove street spawn",
    "server_teleport system",
    "server_skin system",
    "server_animation sync",
    "server_movement sync",
    "server_aim sync",
    "server_shooting sync",
    "server_crouch sync",
    "server_driver sync",
    "server_passenger sync",
    "server_vehicle health",
    "server_vehicle colors",
    "server_vehicle spawn",
    "server_vehicle respawn",
    "server_weather sync",
    "server_clock sync",
    "server_day night cycle",
    "server_timecycle",
    "server_world objects",
    "server_world streaming",
    "server_map zones",
    "server_gang zones",
    "server_turf wars",
    "server_territory system",
    "server_police system",
    "server_wanted level",
    "server_jail system",
    "server_hospital respawn",
    "server_money system",
    "server_bank system",
    "server_shop system",
    "server_ammu nation",
    "server_property system",
    "server_house system",
    "server_business system",
    "server_job system",
    "server_taxi job",
    "server_pizza job",
    "server_trucker job",
    "server_mission system",
    "server_race system",
    "server_deathmatch",
    "server_team deathmatch",
    "server_capture the flag",
    "server_freeroam",
    "server_minigames",
    "server_casino system",
    "server_stunt system",
    "server_drift system",
    "server_score board",
    "server_stats system",
    "server_level system",
    "server_achievements",
    "server_admin system",
    "server_anti cheat",
    "server_ban system",
    "server_kick system",
    "server_mute system",
    "server_report system",
    "server_player list",
    "server_region system",
    "server_server list",
    "server_lan discovery",
    "server_host takeover",
    "server_save system",
    "server_account system",
    "server_login system",
    "server_audio system",
    "server_connect logo",
    "server_virtual keyboard",
    "server_floating menu",
    "server_hud system",
    // ---- 2.21: 35 more (125 total) ----
    "server_world unlock",
    "server_city unlock",
    "server_safe houses",
    "server_driving school",
    "server_flight school",
    "server_bike school",
    "server_boat school",
    "server_race tournaments",
    "server_pay n spray",
    "server_barber shops",
    "server_well stacked pizza",
    "server_burger shot",
    "server_cluckin bell",
    "server_tattoo shops",
    "server_gym system",
    "server_clothing shops",
    "server_mod garages",
    "server_interior doors",
    "server_trucking job",
    "server_quarry job",
    "server_delivery jobs",
    "server_girlfriends",
    "server_emmet",
    "server_vehicle spawner",
    "server_weapon spawner",
    "server_teleport events",
    "server_deathmatch zones",
    "server_kill streaks",
    "server_race checkpoints",
    "server_race timer",
    "server_event announcer",
    "server_score board hud",
    "server_npc world mirror",
    "server_seat name tags",
    "server_relay keepalive",
};

bool MP::AddResource(const char* name)
{
    if(!name) return false;
    std::string n = name;
    while(!n.empty() && (n[0] == ':' || n[0] == ' ' || n[0] == '\t')) n.erase(0, 1);   // ": server_example"
    while(!n.empty() && (n.back() == ' ' || n.back() == '\r' || n.back() == '\n' || n.back() == '\t')) n.pop_back();
    const char* suf = " resources";
    if(n.size() > strlen(suf) && !strcasecmp(n.c_str() + n.size() - strlen(suf), suf)) n.resize(n.size() - strlen(suf));
    if(n.empty() || n.size() > 64 || (int)s_Resources.size() >= MP_MAX_RESOURCES) return false;
    for(auto& r : s_Resources) if(!strcasecmp(r.c_str(), n.c_str())) return false;
    s_Resources.push_back(n);
    return true;
}
int MP::ResourceCount() { return (int)s_Resources.size(); }

// Chat feed while joining: Connecting. .. ... .... ..... . .. ... .... then the resources.
static int      s_nFeedStep = -1;    // 0..8 = "Connecting" lines, -1 = idle
// 2.27: moving to another copy of the SAME server (duplicate merge / host takeover) while you
// play: reconnect silently - no "Connecting...", no resource list, no teleport to Grove Street
static bool     s_bQuietRejoin = false;
static int      s_nFeedRes  = -1;    // index of the next resource line, -1 = idle
static uint64_t s_nFeedNext = 0;
#define FEED_DOT_STEPS 9
#define FEED_DOT_MS    300
#define FEED_RES_MS    58   // 2.21: 125 resources in about the same time as 90 before

// After "Connecting...." and all 125 resources: message, then teleport + title logo + sound.
static void FinishJoin()
{
    SysMsg(COL_GREEN, "Connected successfully!");
    if(JoinFlowActive()) JoinMarkConnected();   // 2.19: reveal the connection camera, then the spawn picker
    else s_bWantTeleport = true;            // Grove Street, on this game tick
    MP::logoStartMs = MP_NowMs();           // title_logo_connected.png
    char snd[512]; snprintf(snd, sizeof(snd), "%s/sound_effect_connected.mp3", s_szDataDir);
    if(!Sound::PlayFile(snd)) logger->Error("Could not play %s", snd);
    if(settings.hitSound) { snprintf(snd, sizeof(snd), "%s/sound_010.mp3", s_szDataDir); Sound::PreloadHit(snd); }   // 3.00: ready before the first hit
}

static void StartFeed() { if(s_nFeedStep < 0) { s_nFeedStep = 0; s_nFeedRes = -1; s_nFeedNext = 0; } }
static void StopFeed()  { s_nFeedStep = -1; s_nFeedRes = -1; }

static void ProcessFeed(uint64_t now)
{
    if(s_nFeedStep >= 0 && now >= s_nFeedNext)
    {
        if(s_nFeedStep < FEED_DOT_STEPS)
        {
            int dots = s_nFeedStep < 5 ? s_nFeedStep + 1 : s_nFeedStep - 4;
            char t[16] = "Connecting";
            for(int i = 0; i < dots; ++i) strcat(t, ".");
            MP::AddSystemMessage(COL_SYSTEM, t);
            ++s_nFeedStep;
            s_nFeedNext = now + FEED_DOT_MS;
        }
        else if(client.GetState() == NET_CONNECTED) { s_nFeedStep = -1; s_nFeedRes = 0; s_nFeedNext = now; }
        else if(client.GetState() == NET_DISCONNECTED && !MP::IsSearching()) StopFeed();
    }
    if(s_nFeedRes >= 0 && now >= s_nFeedNext)
    {
        if(s_nFeedRes < (int)s_Resources.size())
        {
            SysMsg(COL_SYSTEM, ": %s resources", s_Resources[s_nFeedRes].c_str());
            ++s_nFeedRes;
            s_nFeedNext = now + FEED_RES_MS;
        }
        else
        {
            // all resources listed: now the join is complete
            s_nFeedRes = -1;
            if(client.GetState() == NET_CONNECTED) FinishJoin();
        }
    }
}

static char s_szCurServer[32] = "";   // name of the server we are connected to

// ---- 2.19: chat commands (cheats, local only; /invisible is also told to the others) ----------
struct Cheats
{
    bool  wallhack = false, nofall = false, invincible = false, freeze = false, invisible = false;
    bool  fly = false, flash = false;
    float gravity = 1.0f;          // 1 = normal
    bool  applied = false;         // proofs currently set on the ped / car
    int   provedCar = 0, frozenPed = 0, hiddenPed = 0;
};
static Cheats s_Cheat;
bool MP_WallhackOn() { return s_Cheat.wallhack; }
// 2.19 (mp_cmds.inc / mp_join.inc, included further down)
static void ShowPairPopup(int from);
static bool JoinFlowActive();
static void JoinOverlayStart();
static void LeaveServer(const char* msg);
static char s_szDirectRelay[32] = "";   // 3.11: relay address of the server we joined over direct UDP
static bool CheatMenuBlocks(const char* serverName, bool isDefault);
// 3.04: Brief Case War (mp_bcw.inc)
static bool s_bBcwJoin = false;   // the join in progress is Brief Case War
static bool BcwHidden();
static bool BcwSameTeam(int a, int b);
static void BcwProcess(int ped, uint64_t now);
static void BcwOnPacket(const uint8_t* d, int len);
static bool BcwBlockCommand(const char* cmd);
static bool BcwJoinTakeover(int ped);
static void BcwJoinCam(Vec3& pos, Vec3& look);
static void BcwDecorateStr(int id, std::string& name, uint32_t& color);
static void RenderBcw();
static void ProcessGates(int ped, uint64_t now);
static void RenderGates();
static bool GatesCommand(const char* cmd, const char* arg);
static bool IsBusy();
static void LoadAdminFile();
static bool HaveAdminKey();
static void SenjiBlockedMsg();   // 3.11.2
static std::vector<std::string> s_AdminKeys;   // 3.11.3: every valid key found (old + new admin key)
static size_t s_nAdminKeyIdx = 0;
static char s_szLastConnHost[256] = ""; static int s_nLastConnPort = 0;
static void DoConnect(const char* host, int port);
static void JoinOverlayCancel();
static void JoinMarkConnected();
static void JoinFlowProcess(int ped, uint64_t now);
static void ProcessCheats(int ped, uint64_t now);
static void ProcessGames(int ped, uint64_t now);   // 2.21 mp_games.inc
static void GamesOnDeath(const PktDeath& d);
static void SendPlayerInfo(uint64_t now);
static void UpdateTagOcclusion(int id, uint64_t now);
static void TryJack(int ped, int id, uint64_t now);
static bool HandleCommand(const char* text);
static void UpdatePassengerCandidate(int ped, uint64_t now);
static int  s_nMyScoreFwd = 0;
static int      s_nPassBtnCar = 0;   // 2.19.1: car we walk to via the Passenger button
static uint64_t s_nPassBtnT = 0;

// ------------------------------------------------------------------------------------------
// Separate missions + cutscenes (2.9). Every phone plays its own story (own save, own mission
// progress). While a player is on a mission or in a cutscene / scripted scene they are in their
// own story world: the others don't see them (no avatar, car, blip or name tag, no damage) and
// they don't see the others, so nobody walks into someone else's cutscene or mission.
// Mission names: script names from the original main.scm mission table.
struct MissionName { const char* script; const char* title; };
static const MissionName kMissions[] =
{
    { "INTRO",   "In the Beginning" },        { "INTRO1",  "Big Smoke / Sweet & Kendl" },
    { "INTRO2",  "Ryder" },                   { "SWEET1",  "Tagging Up Turf" },
    { "SWEET1B", "Cleaning the Hood" },       { "SWEET3",  "Drive-Thru" },
    { "SWEET2",  "Nines and AK's" },          { "SWEET4",  "Drive-By" },
    { "HOODS5",  "Sweet's Girl" },            { "SWEET6",  "Cesar Vialpando" },
    { "SWEET7",  "Los Sepulcros" },           { "CRASH4",  "Doberman" },
    { "CRASH1",  "Burning Desire" },          { "DRUGS3",  "Gray Imports" },
    { "GUNS1",   "Home Invasion" },           { "RYDER3",  "Catalyst" },
    { "RYDER2",  "Robbing Uncle Sam" },       { "TWAR7",   "OG Loc" },
    { "SMOKE2",  "Running Dog" },             { "SMOKE3",  "Wrong Side of the Tracks" },
    { "DRUGS1",  "Just Business" },           { "MUSIC1",  "Life's a Beach" },
    { "MUSIC2",  "Madd Dogg's Rhymes" },      { "MUSIC3",  "Management Issues" },
    { "MUSIC5",  "House Party" },             { "CESAR1",  "High Stakes, Low Rider" },
    { "DRUGS4",  "Reuniting the Families" },  { "LA1FIN2", "The Green Sabre" },
    { "BCRASH1", "Badlands" },                { "CATALIN", "First Date" },
    { "CAT1",    "First Base / Local Liquor Store" }, { "CAT2", "Made in Heaven / Small Town Bank" },
    { "CAT3",    "Tanker Commander" },        { "CAT4",    "Gone Courting / Against All Odds" },
    { "CATCUT",  "King in Exile" },           { "TRUTH1",  "Body Harvest" },
    { "TRUTH2",  "Are You Going to San Fierro?" }, { "BCESAR4", "Wu Zi Mu / Farewell, My Love..." },
    { "GARAG1",  "Wear Flowers in Your Hair" }, { "DECON",  "Deconstruction" },
    { "SCRASH3", "555 We Tip" },              { "SCRASH2", "Snail Trail" },
    { "WUZI1",   "Mountain Cloud Boys" },     { "FARLIE4", "Ran Fa Li" },
    { "DRIV6",   "Lure" },                    { "WUZI2",   "Amphibious Assault" },
    { "WUZI5",   "The Da Nang Thang" },       { "SYN1",    "Photo Opportunity" },
    { "SYN2",    "Jizzy" },                   { "SYN3",    "Outrider" },
    { "SYND4",   "Ice Cold Killa" },          { "SYN6",    "Toreno's Last Flight" },
    { "SYN7",    "Yay Ka-Boom-Boom" },        { "SYN5",    "Pier 69" },
    { "DRIV2",   "T-Bone Mendez" },           { "DRIV3",   "Mike Toreno" },
    { "STEAL1",  "Zeroing In" },              { "STEAL2",  "Test Drive" },
    { "STEAL4",  "Customs Fast Track" },      { "STEAL5",  "Puncture Wounds" },
    { "DSKOOL",  "Back to School" },          { "ZERO1",   "Air Raid" },
    { "ZERO2",   "Supply Lines..." },         { "ZERO4",   "New Model Army" },
    { "TORENO1", "Monster" },                 { "TORENO2", "Highjack" },
    { "DES3",    "Interdiction" },            { "DESERT4", "Verdant Meadows" },
    { "DESERT6", "N.O.E." },                  { "DESERT9", "Stowaway" },
    { "MAF4",    "Black Project" },           { "DES10",   "Green Goo" },
    { "DESERT5", "Learning to Fly" },         { "CASINO1", "Fender Ketchup" },
    { "CASINO2", "Explosive Situation" },     { "CASINO3", "You've Had Your Chips" },
    { "CASINO7", "Fish in a Barrel" },        { "CASINO4", "Don Peyote" },
    { "CASINO5", "Intensive Care" },          { "CASINO6", "The Meat Business" },
    { "CASINO9", "Freefall" },                { "CASIN10", "Saint Mark's Bistro" },
    { "VCRASH1", "Misappropriation" },        { "VCR2",    "High Noon" },
    { "DOC2",    "Madd Dogg" },               { "HEIST1",  "Architectural Espionage" },
    { "HEIST3",  "Key to Her Heart" },        { "HEIST2",  "Dam and Blast" },
    { "HEIST4",  "Cop Wheels" },              { "HEIST5",  "Up, Up and Away!" },
    { "HEIST9",  "Breaking the Bank at Caligula's" }, { "MANSIO1", "A Home in the Hills" },
    { "MANSIO2", "Vertical Bird" },           { "MANSIO3", "Home Coming" },
    { "MANSON5", "Cut Throat Business" },     { "GROVE1",  "Beat Down on B Dup" },
    { "GROVE2",  "Grove 4 Life" },            { "RIOT1",   "Riot" },
    { "RIOT2",   "Los Desperados" },          { "FINALEA", "End of the Line" },
    { "FINALEB", "End of the Line" },         { "FINALEC", "End of the Line" },
    // side jobs / schools / races
    { "SHRANGE", "Shooting Range" },          { "GYMLS",   "Gym Fight School" },
    { "GYMSF",   "Gym Fight School" },        { "GYMLV",   "Gym Fight School" },
    { "TRUCK",   "Trucking" },                { "QUARRY",  "Quarry" },
    { "BOAT",    "Boat School" },             { "BSKOOL",  "Bike School" },
    { "TAXIODD", "Taxi Driver" },             { "AMBULAN", "Paramedic" },
    { "FIRETRU", "Firefighter" },             { "COPCAR",  "Vigilante" },
    { "BURGJB",  "Burglary" },                { "FREIGHT", "Freight Train" },
    { "PIMP",    "Pimping" },                 { "CPRACE",  "Race Tournament" },
    { "LOWR",    "Lowrider Challenge" },      { "POOL2",   "Pool" },
    { "ZERO5",   "Beefy Baron" },             { "BLOOD",   "Blood Ring" },
    { "KICKSTA", "Kickstart" },               { "TRIA",    "Beat the Cock!" },
    { "BCOUR",   "Courier" },                 { "MTBIKER", "The Chiliad Challenge" },
    { "STUNT",   "Stunt Challenge" },         { "BUYPRO1", "Buy Property" },
};
const char* MP::MissionTitle(const char* s)
{
    if(!s || !*s) return "a mission";
    for(const MissionName& m : kMissions) if(!strcasecmp(m.script, s)) return m.title;
    return "a mission";
}

// 2.16: server permissions + mission partner
static bool     s_bPermsSeen = false;   // 3.00: this server already sent its permissions
static bool     s_bModesSeen = false;           // 3.02
static uint16_t s_nServerModes = 0;             // 3.02: GM_* of the server we're on (custom servers: 0 until it says)
static uint8_t  s_nServerPerms = 0;              // PERM_* of the server we're on (0 until it tells us)
static uint8_t  s_nPartner = MP_INVALID_ID;      // 2.31: first member of my mission group (messages); see s_bGroup
static bool     s_bGroup[MP_MAX_PLAYERS];        // 2.31: Mission sync group members (up to MP_MISSION_GROUP_MAX with me)
static bool     s_bPairAsked[MP_MAX_PLAYERS];    // they asked us
static uint8_t  s_nPairAskedTo = MP_INVALID_ID;  // we asked them
static void ResetPairs() { s_nPartner = MP_INVALID_ID; s_nPairAskedTo = MP_INVALID_ID; memset(s_bPairAsked, 0, sizeof(s_bPairAsked)); memset(s_bGroup, 0, sizeof(s_bGroup)); }
static bool IsPartner(int id) { return id >= 0 && id < MP_MAX_PLAYERS && s_bGroup[id] && (s_nServerPerms & PERM_MISSION_PAIR); }
static int  GroupCount() { int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_bGroup[i] && players[i].active) ++n; return n; }   // others, without me
static void GroupFixFirst() { s_nPartner = MP_INVALID_ID; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_bGroup[i]) { s_nPartner = (uint8_t)i; break; } }
static bool AnyGroupActive() { for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(IsPartner(i) && players[i].active) return true; return false; }
static void SendPair(uint8_t action, uint8_t other) { PktPair q; q.type = PKT_PAIR; q.action = action; q.other = other; client.Send(&q, sizeof(q)); }

static bool     s_bLocalMission = false, s_bLocalCutscene = false;
static bool     s_bLocalScene = false;        // 2.19: scripted scene on a mission (partner stays)
static bool     s_bLocalSeparate = false;     // on a mission or in a cutscene: own story world
static char     s_szLocalMission[8] = "";
static uint8_t  s_nSentStatus = 0;
static char     s_szSentMission[8] = "";
static uint64_t s_nStatusSentMs = 0, s_nSeparateNoticeMs = 0, s_nSeparateEndMs = 0;

static void UpdateLocalStory(uint64_t now)
{
    bool mission = false, cut = false; char name[8];
    if(Game::IsReady()) Game::GetMissionState(mission, cut, name); else name[0] = 0;
    bool sep = mission || cut;
    // 2.19: only a real cutscene hides the mission-sync partner; a scripted scene (player control
    // off: mission intro camera, phone call, etc.) keeps them visible (that broke Mission sync)
    bool realCut = cut && Game::IsRealCutscene();
    s_bLocalScene = cut && !realCut;
    if(!mission && cut) realCut = true;   // off-mission scenes: own world as before
    cut = realCut;
    bool connected = client.GetState() == NET_CONNECTED && myId != MP_INVALID_ID;
    if(sep && !s_bLocalSeparate && connected && now - s_nSeparateNoticeMs > 60000)
    {
        s_nSeparateNoticeMs = now;
        if(mission && AnyGroupActive())
        {
            int n = GroupCount();
            if(n > 1) SysMsg(COL_SYSTEM, "*** %s: other players are hidden until it ends. Your Mission sync group (%d players) stays with you.", MP::MissionTitle(name), n + 1);
            else SysMsg(COL_SYSTEM, "*** %s: other players are hidden until it ends. %s (Mission sync) stays with you.", MP::MissionTitle(name), s_nPartner < MP_MAX_PLAYERS ? players[s_nPartner].name : "your partner");
        }
        else
            SysMsg(COL_SYSTEM, "*** %s: other players are hidden until it ends (your own story).",
                   mission ? MP::MissionTitle(name) : "Cutscene");
    }
    if(!sep && s_bLocalSeparate) s_nSeparateEndMs = now;
    s_bLocalMission = mission; s_bLocalCutscene = cut; s_bLocalSeparate = sep;
    MP_CopyStr(s_szLocalMission, mission ? name : "", sizeof(s_szLocalMission));

    if(!connected) { s_nSentStatus = 0; s_szSentMission[0] = 0; return; }
    uint8_t st = (mission ? STATUS_MISSION : 0) | (cut ? STATUS_CUTSCENE : 0);
    bool changed = st != s_nSentStatus || strcmp(s_szSentMission, s_szLocalMission);
    if(changed || (st && now - s_nStatusSentMs > 10000))   // resend now and then (UDP)
    {
        PktStatus p; memset(&p, 0, sizeof(p));
        p.type = PKT_STATUS; p.id = myId; p.state = st;
        MP_CopyStr(p.mission, s_szLocalMission, sizeof(p.mission));
        client.Send(&p, sizeof(p));
        s_nSentStatus = st; MP_CopyStr(s_szSentMission, s_szLocalMission, sizeof(s_szSentMission));
        s_nStatusSentMs = now;
    }
}

static void SetRemoteStatus(RemotePlayer& p, uint8_t st, const char* mission, bool announce)
{
    bool wasMission = (p.status & STATUS_MISSION) != 0;
    bool isMission  = (st & STATUS_MISSION) != 0;
    if(announce && isMission && mission && mission[0] && (!wasMission || strcmp(p.mission, mission)))
        SysMsg(COL_SYSTEM, "*** %s is on a mission: %s", p.name, MP::MissionTitle(mission));
    else if(announce && wasMission && !isMission)
        SysMsg(COL_SYSTEM, "*** %s finished their mission.", p.name);
    p.status = st;
    if(!isMission) p.mission[0] = 0;
    else if(mission && mission[0]) MP_CopyStr(p.mission, mission, sizeof(p.mission));
}
// Server world: the server owns the time of day and the weather. Every frame (right after the
// game's own update) the clock and weather are put back to the server's, so CLEO scripts, cheats,
// sleeping or mission scripts can't change them while you're on a server. Released on leave.
static bool     s_bWorldPending = false, s_bWorldSynced = false, s_bReleaseWeather = false;
static PktWorld s_World = {};
static uint64_t s_nWorldRecvMs = 0;
static uint64_t s_nWorldSyncedAt = 0;
static int      s_nWorldNoticed = 0;   // 2.18.2: Game::WORLD_FIX_* already told this connection (once each)
static int      s_nWorldBlocked = 0;
static void ApplyWorld(uint64_t now)
{
    if((s_bReleaseWeather || s_bWorldSynced) && Game::IsReady() && client.GetState() == NET_DISCONNECTED)
    { s_bReleaseWeather = false; s_bWorldSynced = false; Game::ReleaseWorld(); }   // left / lost the server: your own time + weather again
    if(s_bWorldPending) { s_bWorldPending = false; s_nWorldRecvMs = now; if(!s_bWorldSynced) s_nWorldSyncedAt = 0; s_bWorldSynced = true; }
    if(!s_bWorldSynced || !Game::IsReady() || client.GetState() == NET_DISCONNECTED) return;
    s_bReleaseWeather = false;
    if(s_bLocalSeparate) return;                     // missions / cutscenes set their own time + weather
    if(s_nSeparateEndMs && now - s_nSeparateEndMs < 3000) s_nWorldSyncedAt = now;   // back to the server day: not a cheat
    uint64_t el = now - s_nWorldRecvMs;              // predict between the server's updates (every 3 s)
    if(el > 60000) el = 60000;
    uint32_t absMin = s_World.absMinute + (uint32_t)(el / 1000);
    float frac = (float)(el % 1000) / 1000.0f;
    int forced = s_World.mode == WORLD_NATURAL ? -1 : (int)s_World.weather;
    int fixed = Game::EnforceWorld(absMin, frac, forced);
    if(!s_nWorldSyncedAt) { s_nWorldSyncedAt = now; return; }     // first lock after joining: not a cheat
    if(fixed && now - s_nWorldSyncedAt > 3000)
    {
        ++s_nWorldBlocked;
        // 2.18.2: told only ONCE per connection (time once, weather once) - a script that keeps
        // changing them used to bring the line back every 15 s. The lock itself keeps working.
        int kinds = ((fixed & (Game::WORLD_FIX_TIME | Game::WORLD_FIX_SPEED)) ? 1 : 0) | ((fixed & Game::WORLD_FIX_WEATHER) ? 2 : 0);
        int fresh = kinds & ~s_nWorldNoticed;
        if(fresh)
        {
            s_nWorldNoticed |= fresh;
            SysMsg(COL_YELLOW, "%s set by the server - change blocked.",
                fresh == 3 ? "Time and weather are" : (fresh == 2 ? "Weather is" : "Time is"));
            logger->Info("World lock: blocked a change (flags %d, total %d)", fixed, s_nWorldBlocked);
        }
    }
}

const char* MP::LanAddress()
{
    // SIOCGIFCONF works on every Android API level (getifaddrs needs API 24+)
    s_szLanIp[0] = 0;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd >= 0)
    {
        char buf[2048];
        ifconf ifc; ifc.ifc_len = sizeof(buf); ifc.ifc_buf = buf;
        if(ioctl(fd, SIOCGIFCONF, &ifc) == 0)
        {
            int n = ifc.ifc_len / (int)sizeof(ifreq);
            ifreq* r = (ifreq*)buf;
            for(int i = 0; i < n; ++i)
            {
                sockaddr_in* a = (sockaddr_in*)&r[i].ifr_addr;
                if(a->sin_family != AF_INET || (ntohl(a->sin_addr.s_addr) >> 24) == 127) continue;
                inet_ntop(AF_INET, &a->sin_addr, s_szLanIp, sizeof(s_szLanIp));
                if(!strncmp(r[i].ifr_name, "wlan", 4) || !strncmp(r[i].ifr_name, "ap", 2) || !strncmp(r[i].ifr_name, "swlan", 5)) break;
            }
        }
        close(fd);
    }
    return s_szLanIp[0] ? s_szLanIp : "unknown";
}

// ------------------------------------------------------------------------------------------
// Remote players
static int s_nLastLocalCar = 0;   // 2.15: the car we drove last (passengers stay in it when we get out)

// 2.15: an avatar's car can have people in it now (us, other avatars): never delete it with
// them inside. Avatars get out first; if WE ride in it, it stays as a normal car.
static void RemoveAvatarCar(RemotePlayer& p, const Vec3& outPos)
{
    if(!p.car) return;
    Vec3 cp; if(!Game::GetCarPos(p.car, cp)) cp = outPos;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& q = players[i];
        if(&q == &p || q.rideCar != p.car) continue;
        if(q.ped && Game::IsCharInCar(q.ped, p.car)) { Vec3 o = { cp.x + 2.0f, cp.y, cp.z }; Game::WarpOutOfCar(q.ped, o); }
        q.rideCar = 0; q.moveState = -1;
    }
    if(p.ped && Game::IsCharInCar(p.ped, p.car)) Game::WarpOutOfCar(p.ped, outPos);
    int lp = Game::LocalPed();
    if(lp > 0 && Game::IsCharInCar(lp, p.car)) Game::ReleaseCar(p.car);   // we are sitting in it
    else Game::DeleteCar(p.car);
    p.car = 0; p.carModel = 0;
    p.vhave.valid = false;   // 2.17
}

static int NpcTakeCopyCar(int model, const Vec3& at, float& base);
// 2.35: the player got out. Before, the avatar was teleported out and its car deleted at once - then
// the same car came back a moment later as the sender's shared "spawned" car (it blinked out / in,
// the player popped beside it). Now the avatar plays the real get-out animation and the car stays
// parked; the NPC copy of that car takes it over (NpcReceive) or it is removed after 5 s.
static void LeaveAvatarCar(RemotePlayer& p, const Vec3& outPos, uint64_t now)
{
    if(!p.car) return;
    int lp = Game::LocalPed();
    bool meIn = lp > 0 && Game::IsCharInCar(lp, p.car);
    bool anyRide = false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(&players[i] != &p && players[i].rideCar == p.car) anyRide = true;
    if(meIn || anyRide || !p.ped || !Game::IsCharInCar(p.ped, p.car)) { RemoveAvatarCar(p, outPos); return; }
    Game::TaskLeaveCar(p.ped, p.car);
    p.leaveT = now;
    if(p.leftCar && p.leftCar != p.car && Game::CarPtr(p.leftCar)) Game::DeleteCar(p.leftCar);
    p.leftCar = p.car; p.leftModel = p.carModel; p.leftBase = p.carBase; p.leftT = now;
    p.car = 0; p.carModel = 0; p.vhave.valid = false;
}
static void DropLeftCar(RemotePlayer& p)
{
    if(!p.leftCar) return;
    int lp = Game::LocalPed();
    if(Game::CarPtr(p.leftCar))
    {
        if(p.ped && Game::IsCharInCar(p.ped, p.leftCar)) { Vec3 o; Game::GetCarPos(p.leftCar, o); o.x += 2.0f; Game::WarpOutOfCar(p.ped, o); }
        if(lp > 0 && Game::IsCharInCar(lp, p.leftCar)) Game::ReleaseCar(p.leftCar); else Game::DeleteCar(p.leftCar);
    }
    p.leftCar = 0; p.leftModel = 0;
}

// 2.32: every ped / car MP ever made for a player avatar. A rebuilt avatar (respawn, skin change,
// mission cleanup, death) could leave the OLD ped behind when the delete failed: the game then
// ran it as an ordinary script ped - it walked around like a bot with the player's skin - and the
// 2.28 "share CLEO-spawned peds" sync sent it to every phone as an NPC. Tracked handles are never
// shared, and leftovers that no player owns any more are removed (AvatarSweep).
static int s_AvatarPeds[256], s_nAvatarPeds = 0, s_AvatarCars[128], s_nAvatarCars = 0;
static void AvatarTrack(int* arr, int& n, int cap, int h)
{
    if(h <= 0) return;
    for(int i = 0; i < n; ++i) if(arr[i] == h) return;
    if(n >= cap) { int k = 0; for(int i = 0; i < n; ++i) if(arr == s_AvatarPeds ? Game::PedPtr(arr[i]) != nullptr : Game::CarPtr(arr[i]) != nullptr) arr[k++] = arr[i]; n = k; }
    if(n < cap) arr[n++] = h; else arr[cap - 1] = h;
}
static bool IsAvatarMade(int h)
{
    if(h <= 0) return false;
    for(int i = 0; i < s_nAvatarPeds; ++i) if(s_AvatarPeds[i] == h) return true;
    for(int i = 0; i < s_nAvatarCars; ++i) if(s_AvatarCars[i] == h) return true;
    return false;
}

static void DestroyAvatar(RemotePlayer& p)
{
    if(p.blip) { Game::RemoveBlip(p.blip); p.blip = 0; }
    if(p.car)  { Vec3 at = MP_LoadVec3(p.sync.carPos); RemoveAvatarCar(p, at); }
    DropLeftCar(p); p.enterT = p.leaveT = 0;   // 2.35
    if(p.jetObj) { Game::RemoveJetpack(p.jetObj); p.jetObj = 0; p.jetApplied = false; }   // 2.19.1
    if(p.ped)  { Game::DeleteChar(p.ped); p.ped = 0; }
    p.rideCar = 0; p.mirror = false; p.gunUp = false;
    p.drawValid = false; p.weapon = 0; p.ducking = false; p.deadApplied = false; p.moveState = 0; p.pedSkin = -1;
    p.applied.n = 0; p.lastFrame = 0;   // 2.13
    p.appliedArea = -1; p.vhave.valid = false;   // 2.17
}

// ---- 2.13 environment sync ---------------------------------------------------------------------
// Explosions and fires the LOCAL player makes are sent to players nearby, who recreate them.
// Things that happen on this phone because of an avatar (its cosmetic rocket hitting a wall)
// are remembered for a moment, so the real one from that player doesn't explode twice.
struct EnvMark { Vec3 pos; uint64_t t; bool explosion; };
static EnvMark  s_Foreign[16]; static int s_nForeign = 0;
static EnvMark  s_RecentExp[8]; static int s_nRecentExp = 0;      // explosions (sent or applied)
static uint64_t s_nLastExpSend = 0, s_nLastFireSend = 0;
static Vec3     s_LastFirePos = { 0, 0, 0 };
struct NetFire { int handle; uint64_t until; };
static NetFire  s_NetFires[16];
struct NetPickup { uint16_t uid; int handle; uint8_t weapon; uint16_t ammo; Vec3 pos; };
static NetPickup s_NetPickups[MP_MAX_PICKUPS]; static int s_nNetPickups = 0;
static bool     s_bLocalDropsOff = false;

static inline float Dist3(const Vec3& a, const Vec3& b) { float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z; return sqrtf(x * x + y * y + z * z); }
static void Mark(EnvMark* arr, int& n, int cap, const Vec3& pos, bool exp)
{
    if(n < cap) arr[n++] = { pos, MP_NowMs(), exp };
    else { memmove(&arr[0], &arr[1], sizeof(EnvMark) * (cap - 1)); arr[cap - 1] = { pos, MP_NowMs(), exp }; }
}
static bool Near(EnvMark* arr, int& n, const Vec3& pos, float r, uint64_t ms, int kind, bool consume)
{
    uint64_t now = MP_NowMs();
    for(int i = 0; i < n; ++i)
    {
        if(now - arr[i].t > ms || (kind >= 0 && arr[i].explosion != (kind == 1))) continue;
        if(Dist3(arr[i].pos, pos) > r) continue;
        if(consume) { arr[i] = arr[--n]; }
        return true;
    }
    return false;
}
static bool EnvAllowed() { return MP::settings.syncEnv && MP::client.GetState() == NET_CONNECTED && !s_bLocalSeparate && myId != MP_INVALID_ID; }

static void OnLocalExplosion(int type, const Vec3& pos)
{
    if(!EnvAllowed() || type < 0 || type > 12) return;
    uint64_t now = MP_NowMs();
    if(now - s_nLastExpSend < 80) return;   // max ~12/s
    s_nLastExpSend = now;
    PktExplosion e; e.type = PKT_EXPLOSION; e.id = myId; e.expType = (uint8_t)type;
    e.pos[0] = pos.x; e.pos[1] = pos.y; e.pos[2] = pos.z;
    client.Send(&e, sizeof(e));
    Mark(s_RecentExp, s_nRecentExp, 8, pos, true);
}
static void OnLocalFire(const Vec3& pos, float size)
{
    if(!EnvAllowed()) return;
    if(Near(s_RecentExp, s_nRecentExp, pos, 12.0f, 5000, -1, false)) return;   // molotov / car fire: the explosion makes it
    uint64_t now = MP_NowMs();
    if(now - s_nLastFireSend < 250 || (now - s_nLastFireSend < 1500 && Dist3(pos, s_LastFirePos) < 2.0f)) return;
    s_nLastFireSend = now; s_LastFirePos = pos;
    PktFire f; f.type = PKT_FIRE; f.id = myId;
    int sz = (int)(size + 0.5f); f.size = (uint8_t)(sz < 1 ? 1 : (sz > 4 ? 4 : sz));
    f.pos[0] = pos.x; f.pos[1] = pos.y; f.pos[2] = pos.z;
    client.Send(&f, sizeof(f));
}
static void OnForeignEnv(const Vec3& pos, bool explosion) { Mark(s_Foreign, s_nForeign, 16, pos, explosion); }

static void ApplyNetExplosion(const PktExplosion& e)
{
    if(!EnvAllowed() || e.id == myId || e.expType > 12) return;
    if(e.id < MP_MAX_PLAYERS && players[e.id].status) return;
    Vec3 pos = MP_LoadVec3(e.pos);
    if(Near(s_Foreign, s_nForeign, pos, 8.0f, 2500, 1, true)) return;   // already blew up here (avatar's shot)
    Game::AddExplosionNet(e.expType, pos);
    Mark(s_RecentExp, s_nRecentExp, 8, pos, true);
}
static void ApplyNetFire(const PktFire& f)
{
    if(!EnvAllowed() || f.id == myId) return;
    Vec3 pos = MP_LoadVec3(f.pos);
    if(Near(s_Foreign, s_nForeign, pos, 4.0f, 2500, 0, true)) return;
    if(Near(s_RecentExp, s_nRecentExp, pos, 12.0f, 5000, -1, false)) return;
    int slot = -1; uint64_t soonest = ~0ull;
    for(int i = 0; i < 16; ++i) { if(!s_NetFires[i].handle) { slot = i; break; } if(s_NetFires[i].until < soonest) { soonest = s_NetFires[i].until; slot = i; } }
    if(s_NetFires[slot].handle) Game::RemoveFireNet(s_NetFires[slot].handle);
    s_NetFires[slot].handle = Game::StartFireNet(pos, f.size);
    s_NetFires[slot].until = MP_NowMs() + 12000;
}

static void AddNetPickup(const PktPickupAdd& a)
{
    for(int i = 0; i < s_nNetPickups; ++i) if(s_NetPickups[i].uid == a.uid) return;
    if(s_nNetPickups >= MP_MAX_PICKUPS) return;
    NetPickup& k = s_NetPickups[s_nNetPickups++];
    k.uid = a.uid; k.weapon = a.weapon; k.ammo = a.ammo; k.pos = MP_LoadVec3(a.pos);
    k.handle = 0;   // created in ProcessEnv (game thread, world ready)
}
static void RemoveNetPickup(uint16_t uid)
{
    for(int i = 0; i < s_nNetPickups; ++i)
    {
        if(s_NetPickups[i].uid != uid) continue;
        if(s_NetPickups[i].handle) Game::RemovePickup(s_NetPickups[i].handle);
        s_NetPickups[i] = s_NetPickups[--s_nNetPickups];
        return;
    }
}
static void ClearEnv(bool keepPickupList)
{
    for(int i = 0; i < 16; ++i) { if(s_NetFires[i].handle) Game::RemoveFireNet(s_NetFires[i].handle); s_NetFires[i].handle = 0; }
    for(int i = 0; i < s_nNetPickups; ++i) { if(s_NetPickups[i].handle) Game::RemovePickup(s_NetPickups[i].handle); s_NetPickups[i].handle = 0; }
    if(!keepPickupList) s_nNetPickups = 0;
    s_nForeign = 0; s_nRecentExp = 0;
}

// every frame while connected
static void ProcessEnv(int ped)
{
    uint64_t now = MP_NowMs();
    int car = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
    Game::SetLocalEntities(ped, car);
    if(!settings.syncEnv) { if(s_nNetPickups) ClearEnv(true); return; }
    if(!s_bLocalDropsOff) { Game::SetDropsWeapons(ped, false); s_bLocalDropsOff = true; }   // the shared drop replaces it
    for(int i = 0; i < 16; ++i)
        if(s_NetFires[i].handle && now > s_NetFires[i].until) { Game::RemoveFireNet(s_NetFires[i].handle); s_NetFires[i].handle = 0; }
    for(int i = 0; i < s_nNetPickups; ++i)
    {
        NetPickup& k = s_NetPickups[i];
        if(!k.handle) { if(!s_bLocalSeparate) k.handle = Game::CreateWeaponPickup(k.weapon, k.ammo, k.pos); continue; }
        if(Game::PickupCollected(k.handle))
        {
            PktPickupTake t; t.type = PKT_PICKUP_TAKE; t.uid = k.uid;
            client.Send(&t, sizeof(t));
            Game::RemovePickup(k.handle); k.handle = -1;   // wait for the server's PKT_PICKUP_REMOVE
        }
    }
}

static void NpcStop();   // 2.18 (defined further down)
static void ClearMissionMarkers(RemotePlayer& p)   // 2.17 mission sync
{
    for(int i = 0; i < p.mmCount; ++i) if(p.mmBlips[i]) Game::RemoveBlip(p.mmBlips[i]);
    p.mmCount = 0; p.mmTime = 0;
}

static void RemovePlayer(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return;
    ClearMissionMarkers(players[id]);
    DestroyAvatar(players[id]);
    players[id] = RemotePlayer();
}

static void ClearAllPlayers()
{
    ResetPairs(); s_nServerPerms = 0; s_nServerModes = 0; s_bModesSeen = false; s_bPermsSeen = false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) RemovePlayer(i);
    ClearEnv(false);   // 2.13: shared pickups / fires belong to the server we left
    if(s_bLocalDropsOff) { int ped = Game::LocalPed(); if(ped > 0) Game::SetDropsWeapons(ped, true); s_bLocalDropsOff = false; }
}

// the game is about to load a save (100% progress on / off): it rebuilds the world, so drop
// the network avatars first; they come back by themselves once the save is running
static void OnBeforeSaveLoad()
{
    NpcStop();   // 2.18
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { ClearMissionMarkers(players[i]); DestroyAvatar(players[i]); }
    ClearEnv(true);    // 2.13: pickups come back after the load
    s_bLocalDropsOff = false;
    s_fPendingDamage = 0; s_nPrevLocalHealth = -1;
}
static void OnAfterSaveLoad()
{
    s_bWantTeleport = true;          // the server's spawn (Grove Street)
    s_nWorldSyncedAt = 0;            // re-lock the server's time + weather without a "blocked" notice
    s_nPrevLocalHealth = -1;
}

static inline float Dist2D(const Vec3& a, const Vec3& b) { float x = a.x - b.x, y = a.y - b.y; return sqrtf(x * x + y * y); }

// ---- 3.11: voice chat --------------------------------------------------------------------
static std::set<std::string> s_VoiceMuted;   // names muted in the player list (this session)
static bool VoiceMutedId(int id) { return id >= 0 && id < MP_MAX_PLAYERS && players[id].active && s_VoiceMuted.count(players[id].name) > 0; }
static void VoiceToggleMute(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active) return;
    std::string n = players[id].name;
    if(s_VoiceMuted.count(n)) { s_VoiceMuted.erase(n); SysMsg(COL_SYSTEM, "*** Voice of %s unmuted.", n.c_str()); }
    else { s_VoiceMuted.insert(n); Voice::ResetSpeaker(id); SysMsg(COL_SYSTEM, "*** Voice of %s muted (only for you).", n.c_str()); }
}
static bool s_bVoiceMuteAll = false, s_bVoicePanel = false;
static bool s_bMicAsked = false, s_bHelperOpened = false, s_bHelperWarned = false;   // 3.11.1
static uint64_t s_nHelperWaitAt = 0;
static int s_nHelperPrevState = 0;
static float VoiceDistanceGain(float d) { return Voice::DistanceGain(d, MP_VOICE_RANGE); }
static void VoiceSend(const void* pkt, int len) { if(client.GetState() == NET_CONNECTED) client.Send(pkt, len); }   // audio thread
static bool s_bVoiceInit = false;
static void VoiceEnsure() { if(!s_bVoiceInit) { s_bVoiceInit = true; Voice::Init(VoiceSend); } }
static int VoiceMicState()   // the mic pill: 0 not connected, 1 off, 2 on, 3 talking
{
    if(!MP::IsConnected()) return 0;
    if(!Voice::MicOn()) return 1;
    return Voice::MicTalking() ? 3 : 2;
}
static void VoiceMicTap(void*)
{
    VoiceEnsure();
    if(!MP::IsConnected()) { SysMsg(COL_YELLOW, "Voice chat works on a server: connect first, then tap the mic."); return; }
    if(Voice::MicOn()) { Voice::StopMic(); SysMsg(COL_SYSTEM, "*** Microphone off."); return; }
    if(!Voice::Available()) { SysMsg(COL_RED, "Voice chat is not available on this phone (%s).", Voice::LastError()); return; }
    if(!settings.voiceChat) { settings.voiceChat = true; SaveConfig(); }
    int perm = Voice::MicPermission();
    if(perm == Voice::PERM_ASK && !s_bMicAsked) { s_bMicAsked = true; Voice::RequestMicPermission(); SysMsg(COL_YELLOW, "Allow the microphone, then tap the mic again."); return; }
    // 3.11.1: no microphone for the game (the APK doesn't ask for it, it can't be allowed in Settings)
    // -> the MP Voice app records for us. It is opened automatically if it isn't running yet.
    if(!Voice::StartMic(myId)) { SysMsg(COL_RED, "Could not turn on the microphone: %s.", Voice::LastError()); return; }
    if(Voice::UsingHelper())
    {
        s_nHelperWaitAt = MP_NowMs(); s_bHelperOpened = false; s_bHelperWarned = false;
        SysMsg(COL_SYSTEM, "*** Your GTA SA app can't use the microphone, so the MP Voice app does it for you...");
        return;
    }
    SysMsg(COL_GREEN, "*** Microphone on: players near you hear you (louder up close, nothing past %d m).", (int)MP_VOICE_RANGE);
}
static void ProcessVoice()
{
    bool conn = MP::IsConnected();
    if(!conn) { if(s_bVoiceInit) { if(Voice::MicOn()) Voice::StopMic(); Voice::SetOutput(false); } return; }
    VoiceEnsure();
    if(!Voice::Available()) return;
    Voice::SetOutput(settings.voiceChat);
    if(!settings.voiceChat && Voice::MicOn()) Voice::StopMic();
    Voice::SetVolume(settings.voiceVolume);
    Voice::SetMicGain(settings.micGain);
    Voice::SetPitch(settings.voicePitch);
    if(Voice::UsingHelper())   // 3.11.1: MP Voice helper app
    {
        int hs = Voice::HelperState();
        uint64_t now = MP_NowMs();
        if(hs == 2 && s_nHelperPrevState != 2) SysMsg(COL_GREEN, "*** Microphone on (MP Voice): players near you hear you (louder up close, nothing past %d m).", (int)MP_VOICE_RANGE);
        if(hs == 1 && s_nHelperPrevState == 2) SysMsg(COL_YELLOW, "MP Voice stopped sending your voice. Open MP Voice and tap Start microphone.");
        if(hs == 1 && !s_bHelperOpened && now - s_nHelperWaitAt > 1500) { s_bHelperOpened = true; Voice::OpenHelperApp(); }   // not running: open it
        if(hs == 1 && s_bHelperOpened && !s_bHelperWarned && Voice::OpenHelperResult() == 2)
        {
            s_bHelperWarned = true;
            SysMsg(COL_RED, "Install the MP Voice app (MP-Voice.apk in the MP package / on the MP website), open it once, tap Start microphone, then tap the mic here again.");
            SysMsg(COL_SYSTEM, "You can still hear the others without it.");
            Voice::StopMic();
        }
        s_nHelperPrevState = hs;
    }
    else s_nHelperPrevState = 0;
    if(!Voice::MicOn() && Voice::Monitor()) Voice::SetMonitor(false);
    // distance volume: full up to 8 m, silent at MP_VOICE_RANGE
    int lp = Game::LocalPed(); Vec3 me; bool haveMe = lp > 0 && Game::GetCharPos(lp, me);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(!p.active || i == myId) continue;
        float g = 1.0f;
        if(VoiceMutedId(i)) g = 0.0f;
        else if(s_bVoiceMuteAll) g = 0.0f;
        else if(haveMe && p.hasSync)
        {
            // like real life: full voice right next to you, quieter the further away (inverse distance,
            // reference 4 m), fading to nothing at MP_VOICE_RANGE (40 m). Walk closer = louder.
            Vec3 at = MP_LoadVec3((p.sync.flags & SYNC_IN_CAR) ? (const void*)p.sync.carPos : (const void*)p.sync.pos);
            float dx = at.x - me.x, dy = at.y - me.y, dz = at.z - me.z;
            g = VoiceDistanceGain(sqrtf(dx * dx + dy * dy + dz * dz));
        }
        else g = 0.0f;   // no position yet: don't play someone we can't place
        Voice::SetGain(i, g);
    }
}
bool MP::VoiceSpeaking(int id) { return id >= 0 && id < MP_MAX_PLAYERS && players[id].active && !VoiceMutedId(id) && Voice::IsSpeaking(id); }
bool MP::VoiceMuted(int id) { return VoiceMutedId(id); }
bool MP::MeTalking() { return Voice::MicTalking(); }

// 3.10: clothing sync - send what your CJ wears (on change + every 8 s for anyone who missed it).
// Not CJ / "show my real model" off -> one "off" packet, so the others show your avatar skin again.
static void ProcessClothes(uint64_t now)
{
    static uint64_t s_nCheck = 0, s_nSent = 0;
    static uint32_t s_nHash = 0xFFFFFFFFu;
    if(client.GetState() != NET_CONNECTED || myId == MP_INVALID_ID) { s_nHash = 0xFFFFFFFFu; return; }
    if(now - s_nCheck < 1000) return;
    s_nCheck = now;
    PktClothes c;
    bool on = settings.realSkin && Game::GetLocalClothes(c);
    if(!on) { memset(&c, 0, sizeof(c)); c.type = PKT_CLOTHES; }
    c.id = myId;
    uint32_t h = MP_ClothesHash(c);
    if(h == s_nHash && (!h || now - s_nSent < 8000)) return;   // nothing new ("off" is sent once)
    s_nHash = h; s_nSent = now;
    client.Send(&c, sizeof(c));
}

static void SpawnAvatar(RemotePlayer& p, const Vec3& at)
{
    p.ped = 0;
    p.pedClothes = p.clothesHash;
    if(p.clothesHash)   // 3.10: their CJ with their clothes (0 = no free slot right now -> avatar skin, retried)
    {
        p.ped = Game::CreateRemoteClothedCJ(p.clothes, at);
        if(!p.ped) p.clothesRetryAt = MP_NowMs() + 5000;   // pedClothes stays = the outfit we tried
    }
    if(!p.ped) p.ped = Game::CreateRemotePed(p.skin, at);
    if(!p.ped) return;
    AvatarTrack(s_AvatarPeds, s_nAvatarPeds, 256, p.ped);   // 2.32
    p.pedSkin = p.skin;
    // measure the centre-to-feet offset used by SET_CHAR_COORDINATES
    Vec3 foot = { at.x, at.y, at.z - 1.0f };
    Game::SetCharPos(p.ped, foot, 0.0f);
    Vec3 got;
    if(Game::GetCharPos(p.ped, got)) { float off = got.z - foot.z; p.pedBase = (off > 0.3f && off < 1.6f) ? off : 1.0f; }
    Game::SetCharHeading(p.ped, p.sync.heading);
    if(settings.showRadarIcons) p.blip = Game::AddBlipForChar(p.ped, p.color);
}

static int MoveStateForSpeed(float v)
{
    if(v < 0.6f) return 0;       // idle
    if(v < 3.2f) return 4;       // walk
    if(v < 7.0f) return 6;       // run
    return 7;                    // sprint
}

// ---- 2.13 snapshots / interpolation ------------------------------------------------------------
static void PushSnapshot(RemotePlayer& p, const SyncData& d, const SyncExt* ext, uint64_t now)
{
    RemotePlayer::Snap sn; memset(&sn, 0, sizeof(sn));
    sn.inCar = (d.flags & SYNC_IN_CAR) != 0;
    sn.pos = MP_LoadVec3(sn.inCar ? (const void*)d.carPos : (const void*)d.pos);
    memcpy(&sn.heading, sn.inCar ? (const void*)&d.carHeading : (const void*)&d.heading, 4);
    if(ext)
    {
        // map the sender's clock onto ours; the smallest offset seen is the fastest path, and
        // it creeps up 2 ms/s so clock drift can't freeze us
        double sample = (double)now - (double)ext->time;
        if(!p.hasClock || sample < p.clockOff) { p.clockOff = sample; p.hasClock = true; }
        else p.clockOff += (double)(now - p.lastRecv) * 0.002;
        if(p.clockOff > sample) p.clockOff = sample;
        float late = (float)(sample - p.clockOff);
        p.jitter = p.jitter * 0.92f + late * 0.08f;
        sn.t = (uint64_t)((double)ext->time + p.clockOff);
        sn.vel = MP_LoadVec3(ext->vel);
        sn.hasRot = sn.inCar && (ext->carUp[0] != 0.0f || ext->carUp[1] != 0.0f || ext->carUp[2] != 0.0f);
        if(sn.hasRot) { memcpy(sn.fwd, ext->carFwd, 12); memcpy(sn.up, ext->carUp, 12); }
        p.hasExt = true;
        p.hasVeh = ext->hasVeh != 0; p.vehOwner = ext->vehOwner; p.vehSeat = ext->vehSeat;
        {
            RemotePlayer::AnimFrame af; af.t = sn.t; af.full = ext->fullAnims != 0;
            af.n = ext->animCount > MP_ANIM_SLOTS ? MP_ANIM_SLOTS : ext->animCount;
            for(int i = 0; i < af.n; ++i)
            {
                AnimSync a; memcpy(&a, &ext->anim[i], sizeof(a));
                Game::PedAnim& g = af.a[i];
                g.hash = a.hash; g.group = a.group; g.id = a.id; g.time = a.time;
                g.speed = a.speed / 50.0f; g.blend = a.blend / 255.0f; g.flags = a.flags;
                memcpy(g.block, a.block, 16); g.block[15] = 0;
            }
            if(p.nAF && af.t <= p.aframes[p.nAF - 1].t) af.t = p.aframes[p.nAF - 1].t + 1;
            if(p.nAF == 8) { memmove(&p.aframes[0], &p.aframes[1], sizeof(p.aframes[0]) * 7); p.nAF = 7; }
            p.aframes[p.nAF++] = af;
        }
        p.extMove = ext->moveState;
        p.animCount = ext->animCount > MP_ANIM_SLOTS ? MP_ANIM_SLOTS : ext->animCount;
        for(int i = 0; i < p.animCount; ++i)
        {
            AnimSync a; memcpy(&a, &ext->anim[i], sizeof(a));
            Game::PedAnim& g = p.anims[i];
            g.hash = a.hash; g.group = a.group; g.id = a.id; g.time = a.time;
            g.speed = a.speed / 50.0f; g.blend = a.blend / 255.0f; g.flags = a.flags;
            memcpy(g.block, a.block, 16); g.block[15] = 0;
        }
    }
    else
    {
        sn.t = now;
        sn.vel = p.vel;
        p.hasExt = false; p.animCount = 0; p.nAF = 0; p.hasVeh = false;
    }
    p.lastRecv = now;

    // teleport / respawn / car change: don't slide across the map
    if(p.nSnaps)
    {
        const RemotePlayer::Snap& last = p.snaps[p.nSnaps - 1];
        float dx = sn.pos.x - last.pos.x, dy = sn.pos.y - last.pos.y, dz = sn.pos.z - last.pos.z;
        if(dx * dx + dy * dy + dz * dz > 30.0f * 30.0f || last.inCar != sn.inCar) p.nSnaps = 0;
        else if(sn.t <= last.t) { if(sn.t + 5 < last.t) return; sn.t = last.t + 1; }   // late / duplicate
    }
    if(p.nSnaps == 8) { memmove(&p.snaps[0], &p.snaps[1], sizeof(p.snaps[0]) * 7); p.nSnaps = 7; }
    p.snaps[p.nSnaps++] = sn;
}

static float LerpAngle(float a, float b, float f)
{
    float d = fmodf(b - a + 540.0f, 360.0f) - 180.0f;
    float r = a + d * f;
    if(r < 0) r += 360.0f; if(r >= 360.0f) r -= 360.0f;
    return r;
}

// Where the player was `delay` ms ago (between two snapshots), or a short extrapolation.
static bool SampleMotion(RemotePlayer& p, uint64_t now, float dt, RemotePlayer::Snap& out)
{
    if(!p.nSnaps) return false;
    // 2.14: far players arrive only every 200-500 ms (server distance tiers, idle senders):
    // draw far enough behind to have two real positions to move between
    if(p.nSnaps >= 2)
    {
        float iv = (float)(p.snaps[p.nSnaps - 1].t - p.snaps[0].t) / (float)(p.nSnaps - 1);
        if(iv > 0 && iv < 3000) p.interval += (iv - p.interval) * 0.2f;
    }
    float want = p.hasExt ? 60.0f + p.jitter * 2.5f : 110.0f;
    float ivWant = p.interval + 40.0f; if(ivWant > 600.0f) ivWant = 600.0f;
    if(want < ivWant) want = ivWant;
    if(want < 70.0f) want = 70.0f; if(want > 600.0f) want = 600.0f;
    float k = dt * 2.0f; if(k > 1.0f) k = 1.0f;
    p.delay += (want - p.delay) * k;
    double rt = (double)now - p.delay;
    p.renderT = rt;

    int ia = -1;
    for(int i = 0; i < p.nSnaps; ++i) { if((double)p.snaps[i].t <= rt) ia = i; else break; }
    p.stale = (float)(rt - (double)p.snaps[p.nSnaps - 1].t); if(p.stale < 0) p.stale = 0;
    if(ia < 0) { out = p.snaps[0]; out.vel = { 0, 0, 0 }; return true; }   // 2.14: waiting for the 2nd snapshot: stand, don't walk in place
    const RemotePlayer::Snap& a = p.snaps[ia];
    if(ia + 1 < p.nSnaps)
    {
        const RemotePlayer::Snap& b = p.snaps[ia + 1];
        float f = (float)((rt - (double)a.t) / (double)(b.t - a.t > 0 ? b.t - a.t : 1));
        if(f < 0) f = 0; if(f > 1) f = 1;
        out = b;
        out.pos.x = a.pos.x + (b.pos.x - a.pos.x) * f;
        out.pos.y = a.pos.y + (b.pos.y - a.pos.y) * f;
        out.pos.z = a.pos.z + (b.pos.z - a.pos.z) * f;
        out.vel.x = a.vel.x + (b.vel.x - a.vel.x) * f;
        out.vel.y = a.vel.y + (b.vel.y - a.vel.y) * f;
        out.vel.z = a.vel.z + (b.vel.z - a.vel.z) * f;
        out.heading = LerpAngle(a.heading, b.heading, f);
        if(a.hasRot && b.hasRot)
            for(int c = 0; c < 3; ++c) { out.fwd[c] = a.fwd[c] + (b.fwd[c] - a.fwd[c]) * f; out.up[c] = a.up[c] + (b.up[c] - a.up[c]) * f; }
        return true;
    }
    // newer than anything we have: carry on along the last velocity for at most 300 ms
    out = a;
    float ex = (float)((rt - (double)a.t) / 1000.0); if(ex > 0.3f) ex = 0.3f;
    out.pos.x += a.vel.x * ex; out.pos.y += a.vel.y * ex; out.pos.z += a.vel.z * ex;
    return true;
}

// sender's CPed move state -> TASK_GO_STRAIGHT_TO_COORD walk speed (4 walk, 6 run, 7 sprint)
static int MoveStateFromExt(int st, float speed)
{
    switch(st)
    {
        case 4: return 4;            // PEDMOVE_WALK
        case 5: case 6: return 6;    // JOG / RUN
        case 7: return 7;            // SPRINT
        default: return speed > 1.2f ? MoveStateForSpeed(speed) : 0;   // still / turning
    }
}

// 2.32: aim / shoot pose that really raises the arms. Before, TASK_AIM_GUN_AT_COORD was given again
// every 300 ms for 450 ms: each new task restarted the "raise the gun" motion, so the arm and elbow
// never came up on other phones. Now it's given once (1.6 s) and again only when the aim point moves
// (> 1.2 m) or it runs out; mirrored body anims are released first so they don't cover the arms.
static void AvatarAim(RemotePlayer& p, const Vec3& aim, bool fire, uint64_t now)
{
    if(!p.gunUp) { Game::ReleaseMirroredAnims(p.ped); p.applied.n = 0; p.gunUp = true; p.lastAim = 0; p.lastShoot = 0; }
    float dx = aim.x - p.lastAimPt[0], dy = aim.y - p.lastAimPt[1], dz = aim.z - p.lastAimPt[2];
    bool moved = dx * dx + dy * dy + dz * dz > 1.2f * 1.2f;
    if(fire)
    {
        if(now - p.lastShoot > 1100 || (moved && now - p.lastShoot > 250))
        { Game::TaskShootAt(p.ped, aim, 1500); p.lastShoot = now; p.lastAim = now; p.lastAimPt[0] = aim.x; p.lastAimPt[1] = aim.y; p.lastAimPt[2] = aim.z; }
        return;
    }
    if(now - p.lastAim > 1200 || (moved && now - p.lastAim > 250))
    { Game::TaskAimAt(p.ped, aim, 1600); p.lastAim = now; p.lastAimPt[0] = aim.x; p.lastAimPt[1] = aim.y; p.lastAimPt[2] = aim.z; }
}

// 2.37: ramming a player on foot with a car used to send the avatar's whole 1000 HP pool loss
// (or "instant kill" when the avatar got knocked dead) = one hit kills. Now a ram is weapon 49,
// scaled by speed and capped at MP_RAM_MAX_DAMAGE, one per MP_RAM_COOLDOWN_MS. Trailers do nothing.
// Returns false = don't send.
// 2.37: kill feed without the delay. A kill used to show only after my hit went to the victim's
// phone, it noticed its own death and the death came back (2 round trips, ~0.5-1.5 s over the
// internet relay). Now, when the damage I send is enough to finish the victim (from its last
// synced health + armour), the kill shows at once; PKT_DEATH then confirms it (no double line),
// corrects it (someone else got the kill) or, after 3 s without a death, removes it.
static float    s_fKillSent[MP_MAX_PLAYERS] = {};
static int      s_nKillHp[MP_MAX_PLAYERS];
static uint64_t s_nKillPredMs[MP_MAX_PLAYERS] = {};
static std::string MyFeedName()
{
    if(MP::IsLocalAdmin()) return "[ADMIN] SENJI";
    return (myId < MP_MAX_PLAYERS && players[myId].active) ? players[myId].name : settings.name;
}
static void PushKillEntry(KillEntry& k)
{
    kills.push_back(k);
    while(kills.size() > 5) kills.pop_front();
}
// 3.00: hit sound (assets/MP/sound_010.mp3) when OUR hit lands on a player - guns, fists, melee,
// drive-by, rams. One sound per 70 ms at most (a minigun stays a rattle, not a wall of noise).
static void PlayHitSound()
{
    if(!settings.hitSound) return;
    static uint64_t s_nLast = 0;
    uint64_t now = MP_NowMs();
    if(now - s_nLast < 70) return;
    s_nLast = now;
    char snd[512]; snprintf(snd, sizeof(snd), "%s/sound_010.mp3", s_szDataDir);
    Sound::PlayHit(snd, 1.0f);
}

static void PredictKill(int target, float amount, uint8_t weapon)
{
    if(target < 0 || target >= MP_MAX_PLAYERS || myId >= MP_MAX_PLAYERS) return;
    const RemotePlayer& p = players[target];
    if(!p.active || (p.sync.flags & SYNC_DEAD)) return;
    int hp = (int)p.sync.health + (int)p.sync.armour;
    static bool s_bInit = false;
    if(!s_bInit) { for(int i = 0; i < MP_MAX_PLAYERS; ++i) s_nKillHp[i] = -1; s_bInit = true; }
    if(s_nKillHp[target] != hp) { s_nKillHp[target] = hp; s_fKillSent[target] = 0; }   // a new health value arrived
    s_fKillSent[target] += amount;
    uint64_t now = MP_NowMs();
    if(now - s_nKillPredMs[target] < 4000) return;   // already shown
    if(amount < 999.0f && (hp <= 0 || s_fKillSent[target] + 0.5f < (float)hp)) return;
    s_nKillPredMs[target] = now;
    KillEntry k;
    if(BcwSameTeam(myId, target)) return;   // 3.04: no friendly fire in Brief Case War
    k.killer = MyFeedName(); k.killerColor = myColor;
    k.victim = p.name; k.victimColor = p.color;
    BcwDecorateStr(myId, k.killer, k.killerColor); BcwDecorateStr(target, k.victim, k.victimColor);   // 3.04
    k.weapon = weapon; k.time = now; k.victimId = (uint8_t)target; k.predicted = true;
    PushKillEntry(k);
}
static void ExpireKillPredictions(uint64_t now)
{
    for(auto it = kills.begin(); it != kills.end(); )
    {
        if(it->predicted && now - it->time > 3000 && !(it->victimId < MP_MAX_PLAYERS && (players[it->victimId].sync.flags & SYNC_DEAD)))
        { if(it->victimId < MP_MAX_PLAYERS) s_nKillPredMs[it->victimId] = 0; it = kills.erase(it); }   // they survived
        else ++it;
    }
}

static uint64_t s_nRamSentMs[MP_MAX_PLAYERS] = {};
// 3.00: fists / melee. A punch sometimes takes HP off the avatar without the game noting who did it
// (HasCharBeenDamagedByChar stays false), so the hit was lost - no damage sent, no hit sound. Counted
// as ours when: we hold a melee weapon (fist, knife, bat...), on foot, the avatar lost HP this frame,
// it is within 2.5 m and in front of us (not a stranger punching it behind our back).
static bool MeleeHitByMe(int localPed, const RemotePlayer& p)
{
    if(!p.ped || Game::IsCharInAnyCar(localPed)) return false;
    int w = Game::GetCharWeapon(localPed);
    if(w < 0 || w > 15) return false;                       // 0 fist, 1-15 melee
    if(Game::GetCharHealth(p.ped) >= 1000) return false;    // the avatar is refilled to 1000 every frame
    Vec3 a, b; Game::GetCharPos(localPed, a); Game::GetCharPos(p.ped, b);
    float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
    float d2 = dx * dx + dy * dy;
    if(d2 > 2.5f * 2.5f || fabsf(dz) > 1.6f) return false;
    float h = Game::GetCharHeading(localPed) * 0.0174533f;   // degrees, 0 = north
    float fx = -sinf(h), fy = cosf(h), d = sqrtf(d2);
    return d < 0.6f || (dx * fx + dy * fy) / d > 0.35f;    // in front (or touching)
}

static bool RamDamage(int localPed, const RemotePlayer& p, PktDamage& d)
{
    if(!Game::IsCharInAnyCar(localPed) || Game::IsCharShooting(localPed)) return true;   // not a ram (drive-by is normal damage)
    if(p.sync.flags & SYNC_IN_CAR) return true;                                         // car vs car: the game's own damage
    int car = Game::GetCarCharIsUsing(localPed);
    if(car > 0 && MP_IsTrailerModel(Game::GetCarModel(car))) return false;
    if(car > 0 && p.ped)   // far away = tank shell / Hydra rocket, not a ram
    {
        Vec3 a, b; Game::GetCarPos(car, a); Game::GetCharPos(p.ped, b);
        float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        if(dx * dx + dy * dy + dz * dz > 10.0f * 10.0f) return true;
    }
    uint64_t now = MP_NowMs();
    if(d.target < MP_MAX_PLAYERS)
    {
        if(now - s_nRamSentMs[d.target] < MP_RAM_COOLDOWN_MS) return false;
        s_nRamSentMs[d.target] = now;
    }
    float v = Game::GetCharSpeed(localPed);   // m/s
    float dmg = 3.0f + v * 0.6f;              // 10 m/s ~ 9 HP, 28+ m/s = cap
    d.weapon = MP_RAM_WEAPON;
    d.amount = dmg < MP_RAM_MAX_DAMAGE ? dmg : MP_RAM_MAX_DAMAGE;
    return true;
}

static void UpdateRemote(int id, int localPed, uint64_t now)
{
    RemotePlayer& p = players[id];
    if(!p.active || !p.hasSync || id == myId) return;
    const SyncData& s = p.sync;

    // separate story worlds: hide players on a mission / in a cutscene, and everyone while we are
    // 2.16: a mission partner stays visible on missions (cutscenes still hide everybody)
    bool hide = p.status || (s.flags & (SYNC_MISSION | SYNC_CUTSCENE)) || s_bLocalSeparate;
    if(hide && IsPartner(id) && !(p.status & STATUS_CUTSCENE) && !(s.flags & SYNC_CUTSCENE) && !s_bLocalCutscene) hide = false;
    if(p.invisible) hide = true;   // 2.19 /invisible
    // 2.19: a mission script can delete our avatar / its car (clear area, mission cleanup): the
    // handle is dead then and the avatar never came back -> forget it, it is rebuilt below
    if(p.ped && !Game::PedPtr(p.ped)) { p.ped = 0; p.blip = 0; Game::RemoveJetpack(p.jetObj); p.jetObj = 0; p.jetApplied = false; p.applied.n = 0; p.mirror = false; }
    if(p.car && !Game::CarPtr(p.car)) { p.car = 0; p.carModel = 0; }
    if(hide)
    {
        if(p.ped || p.car || p.blip) DestroyAvatar(p);
        p.drawValid = false;
        return;
    }

    // 2.11: too far away / too many players around: no avatar (the player stays in the lists)
    if(!s_bStreamed[id])
    {
        if(p.ped || p.car || p.blip) DestroyAvatar(p);
        p.drawValid = false;
        return;
    }

    // respawn handling: remote came back alive -> rebuild avatar at the new place
    if(p.ped && p.deadApplied && !(s.flags & SYNC_DEAD)) DestroyAvatar(p);
    if(p.ped && !p.deadApplied && Game::IsCharDead(p.ped))
    {
        // killed instantly on our screen (headshot / explosion): report it if we did it
        if(localPed && myId != MP_INVALID_ID && !p.paused && Game::HasCharBeenDamagedByChar(p.ped, localPed))
        {
            PktDamage d; d.type = PKT_DAMAGE; d.from = myId; d.target = (uint8_t)id;
            d.weapon = (uint8_t)Game::GetCharWeapon(localPed); d.amount = 1000.0f;
            if(RamDamage(localPed, p, d)) { client.Send(&d, sizeof(d)); PredictKill(id, d.amount, d.weapon); PlayHitSound(); }
        }
        DestroyAvatar(p);
    }
    if(p.ped && p.pedSkin != p.skin && !(s.flags & SYNC_IN_CAR)) DestroyAvatar(p);
    // 3.10: clothing sync - they changed clothes / stopped being CJ, or a slot may be free now
    if(p.ped && !(s.flags & SYNC_IN_CAR))
    {
        bool changed = p.pedClothes != p.clothesHash;
        bool retry = !changed && p.clothesHash && p.clothesRetryAt && MP_NowMs() >= p.clothesRetryAt;   // shown with the avatar skin: no slot was free
        if(retry) { p.clothesRetryAt = MP_NowMs() + 5000; retry = Game::ClothesCanBuild(p.clothes); }
        if(changed || retry) { p.clothesRetryAt = 0; DestroyAvatar(p); }
    }

    // 2.13: interpolated motion (see SampleMotion)
    float dt = p.lastFrame ? (now - p.lastFrame) / 1000.0f : 0.016f;
    if(dt > 0.1f) dt = 0.1f;
    p.lastFrame = now;
    RemotePlayer::Snap mo;
    bool haveMo = SampleMotion(p, now, dt, mo);
    bool inCarNow = (s.flags & SYNC_IN_CAR) != 0;
    if(haveMo && mo.inCar != inCarNow) haveMo = false;   // getting in / out right now: use the latest
    Vec3 target = haveMo && !inCarNow ? mo.pos : MP_LoadVec3(s.pos);
    Vec3 mvel = haveMo ? mo.vel : p.vel;

    if(!p.ped)
    {
        if(s.flags & SYNC_DEAD) return;
        SpawnAvatar(p, target);
        if(!p.ped) return;
    }

    // blips follow the setting
    if(settings.showRadarIcons && !p.blip) p.blip = Game::AddBlipForChar(p.ped, p.color);
    if(!settings.showRadarIcons && p.blip) { Game::RemoveBlip(p.blip); p.blip = 0; }

    // 2.17 EnEx / interiors: the avatar (and its car) live in the player's area, so somebody
    // inside a house or shop is seen by the people in there - not floating outside
    if(p.hasX2 && p.appliedArea != p.interior)
    {
        Game::SetCharArea(p.ped, p.interior);
        if(p.car) Game::SetCarArea(p.car, p.interior);
        p.appliedArea = p.interior;
    }

    // --- combat: did WE hurt this avatar? send shooter-authoritative damage ---------------
    if(localPed && !p.deadApplied)
    {
        if(p.paused) Game::ClearCharLastDamageEntity(p.ped);   // paused players can't be hurt
        else if(Game::HasCharBeenDamagedByChar(p.ped, localPed) || MeleeHitByMe(localPed, p))   // 3.00: + fist / melee fallback
        {
            int hp = Game::GetCharHealth(p.ped);
            bool instantKill = hp <= 0 || Game::IsCharDead(p.ped);   // headshot / explosion
            float dmg = instantKill ? 1000.0f : (float)(1000 - hp);
            if(dmg > 0.5f)
            {
                PktDamage d; d.type = PKT_DAMAGE; d.from = myId; d.target = (uint8_t)id;
                d.weapon = (uint8_t)Game::GetCharWeapon(localPed); d.amount = dmg;
                if(RamDamage(localPed, p, d)) { client.Send(&d, sizeof(d)); PredictKill(id, d.amount, d.weapon); PlayHitSound(); }
            }
            else PlayHitSound();   // 3.00: a punch that landed but took (almost) no HP still sounds like a hit
            if(instantKill)
            {
                // the local avatar died but the real player decides; rebuild it next tick
                DestroyAvatar(p);
                return;
            }
            Game::ClearCharLastDamageEntity(p.ped);
        }
        Game::SetCharHealth(p.ped, 1000);  // avatar never dies locally; real health is synced
    }

    // --- death ---------------------------------------------------------------------------
    if(s.flags & SYNC_DEAD)
    {
        if(!p.deadApplied)
        {
            if(p.car) RemoveAvatarCar(p, target);
            if(p.rideCar) { if(Game::IsCharInCar(p.ped, p.rideCar)) Game::WarpOutOfCar(p.ped, target); p.rideCar = 0; }
            Game::SetCharHealth(p.ped, 0);
            Game::TaskDie(p.ped);
            p.deadApplied = true;
        }
        Game::GetCharPos(p.ped, p.drawPos); p.drawPos.z += 1.0f; p.drawValid = true;
        return;
    }

    // --- weapon ----------------------------------------------------------------------------
    if(s.weapon != p.weapon) { Game::GiveAndSelectWeapon(p.ped, s.weapon); p.weapon = s.weapon; }

    // --- 2.15: passenger in another player's car (or in ours) -------------------------------
    if((s.flags & SYNC_IN_CAR) && !(s.flags & SYNC_DRIVER) && p.hasVeh)
    {
        int car = 0;
        if(p.vehOwner == myId)
        {
            // our car: the one we sit in, or the one we just parked (they stay inside)
            if(localPed && Game::IsCharInAnyCar(localPed)) car = Game::GetCarCharIsIn(localPed);
            else if(s_nLastLocalCar > 0 && Game::CarPtr(s_nLastLocalCar))   // handle still a live car
            {
                Vec3 lc, rc = MP_LoadVec3(s.carPos);
                if(Game::GetCarPos(s_nLastLocalCar, lc) && Dist2D(lc, rc) < 15.0f) car = s_nLastLocalCar;
            }
        }
        else if(p.vehOwner < MP_MAX_PLAYERS && players[p.vehOwner].ped && players[p.vehOwner].car) car = players[p.vehOwner].car;
        if(car > 0)
        {
            if(p.car) RemoveAvatarCar(p, target);   // had its own copy of the car
            if(!Game::IsCharInCar(p.ped, car))
            {
                if(Game::IsCharInAnyCar(p.ped)) { Vec3 o = MP_LoadVec3(s.carPos); o.x += 2.0f; Game::WarpOutOfCar(p.ped, o); }
                int seat = Game::FreePassengerSeat(car, p.vehSeat);
                if(seat >= 0 && now - p.lastTask > 400) { Game::WarpIntoCarAsPassenger(p.ped, car, seat); p.lastTask = now; }
            }
            p.rideCar = car; p.mirror = false; p.moveState = -1;
            // 2.21: tag on the passenger's own seat, not on the middle of the car (on top of the driver's)
            Vec3 cp;
            if(Game::IsCharInAnyCar(p.ped) && Game::GetCharTagPos(p.ped, cp)) { p.drawPos = cp; p.drawValid = true; }
            else if(Game::GetCarPos(car, cp)) { p.drawPos = cp; p.drawPos.z += 1.2f; p.drawValid = true; }
            return;
        }
        // the driver's car isn't on this phone (yet): show our own copy below
    }
    if(p.rideCar)
    {
        if(p.ped && Game::IsCharInCar(p.ped, p.rideCar)) Game::WarpOutOfCar(p.ped, target);
        p.rideCar = 0; p.moveState = -1;
    }

    // --- vehicle ---------------------------------------------------------------------------
    if((s.flags & SYNC_IN_CAR) && s.carModel >= 400 && s.carModel <= 611)
    {
        Vec3 cp = haveMo ? mo.pos : MP_LoadVec3(s.carPos);
        p.mirror = false;
        if(p.car && p.carModel != s.carModel) RemoveAvatarCar(p, cp);
        if(!p.car)
        {
            // 2.35: the car is already here (the one they just left, or the shared NPC copy of the parked
            // car they got into): use THAT car - no second car popping up on the same spot
            bool reuse = false;
            if(p.leftCar && Game::CarPtr(p.leftCar) && p.leftModel == s.carModel)
            {
                Vec3 lc; if(Game::GetCarPos(p.leftCar, lc) && Dist2D(lc, cp) < 8.0f)
                { p.car = p.leftCar; p.carBase = p.leftBase; p.leftCar = 0; p.leftModel = 0; reuse = true; }
            }
            if(!p.car) p.car = NpcTakeCopyCar(s.carModel, cp, p.carBase);
            if(p.car) reuse = true;
            if(p.leftCar) DropLeftCar(p);
            if(!p.car && !MP_IsTrailerModel(s.carModel)) p.car = Game::CreateCar(s.carModel, cp);   // 2.37: no flying Tug Stairs
            if(p.car)
            {
                AvatarTrack(s_AvatarCars, s_nAvatarCars, 128, p.car);   // 2.32
                Game::KeepMissionCar(p.car);
                p.carModel = s.carModel;
                p.vhave.valid = false;
                if(p.hasX2 && p.interior) Game::SetCarArea(p.car, p.interior);
                if(!reuse)
                {
                    Vec3 got; Game::GetCarPos(p.car, got);
                    float off = got.z - cp.z; p.carBase = (off > -0.5f && off < 2.5f) ? off : 0.0f;
                }
                // 2.35: on foot next to it and the car is (almost) standing: walk in with the real
                // open-door / get-in animation of that car type. Otherwise (far, fast, riding) warp.
                Vec3 pp, cc; bool near = !Game::IsCharInAnyCar(p.ped) && Game::GetCharPos(p.ped, pp) && Game::GetCarPos(p.car, cc) && Dist2D(pp, cc) < 8.0f;
                float sp2 = mvel.x * mvel.x + mvel.y * mvel.y;
                if(near && sp2 < 4.0f * 4.0f && settings.syncAnims) { Game::TaskEnterCarAsDriver(p.ped, p.car, 2500); p.enterT = now; }
                else { Game::WarpIntoCar(p.ped, p.car); p.enterT = 0; }
            }
        }
        if(p.car && p.enterT && !Game::IsCharInCar(p.ped, p.car))
        {   // still getting in: the car stays where it is, the tag follows the ped
            float sp2 = mvel.x * mvel.x + mvel.y * mvel.y;
            if(now - p.enterT > 2800 || sp2 > 6.0f * 6.0f) { Game::WarpIntoCar(p.ped, p.car); p.enterT = 0; }
            else
            {
                Vec3 tp; if(Game::GetCharPos(p.ped, tp)) { p.drawPos = tp; p.drawPos.z += 1.0f; p.drawValid = true; }
                return;
            }
        }
        else if(p.car && p.enterT) p.enterT = 0;
        if(p.car)
        {
            Vec3 cur; Game::GetCarPos(p.car, cur);
            Vec3 err = { cp.x - cur.x, cp.y - cur.y, cp.z - cur.z };
            float e2 = err.x * err.x + err.y * err.y + err.z * err.z;
            float hd = haveMo ? mo.heading : s.carHeading;
            if(e2 > 6.0f * 6.0f)
            {
                Game::SetCarPos(p.car, cp, p.carBase);   // far off: put it there
                Game::SetCarHeading(p.car, hd);
            }
            else
            {
                // glide: the game's physics moves the car at the sender's speed, we only pull
                // out the remaining error a bit every frame (no more jumping every packet)
                float k = dt * 5.0f; if(k > 1.0f) k = 1.0f;
                Vec3 step = { err.x * k, err.y * k, err.z * k };
                Game::NudgeEntity(p.car, true, step);
                if(haveMo && mo.hasRot) Game::SetCarRotation(p.car, mo.fwd, mo.up);
                else Game::SetCarHeading(p.car, hd);
            }
            Game::SetMoveSpeed(p.car, true, mvel);
            // 2.17 vehicle sync: steering angle, pedals, horn, Hydra nozzles, tank / firetruck
            // turret every frame; colours, paintjob, tuning, health, damage, doors, lock,
            // engine, lights, siren and landing gear when the driver's state changes
            if(p.hasX2 && (p.x2flags & X2_VEHFAST)) Game::ApplyVehFast(p.car, p.vfast);
            if(p.vwant.valid && p.vwantModel == p.carModel && now - p.vApplied > 250)
            {
                Game::ApplyVehState(p.car, p.vwant, p.vhave);
                p.vApplied = now;
            }
            // 2.21: driver's tag over the driver's seat (same spot on every phone)
            Vec3 tp;
            if(Game::IsCharInAnyCar(p.ped) && Game::GetCharTagPos(p.ped, tp)) p.drawPos = tp;
            else { p.drawPos = cp; p.drawPos.z += 1.2f; }
            p.drawValid = true;
        }
        return;
    }
    else if(p.car)
    {
        LeaveAvatarCar(p, target, now);   // 2.35: real get-out animation, the car stays
        p.moveState = -1;
    }
    if(p.enterT) p.enterT = 0;
    if(p.leaveT && p.ped && p.leftCar && Game::IsCharInCar(p.ped, p.leftCar))
    {
        if(now - p.leaveT < 1800)
        {
            Vec3 tp; if(Game::GetCharTagPos(p.ped, tp)) { p.drawPos = tp; p.drawValid = true; }
            return;   // getting out: don't move / warp it meanwhile
        }
        Vec3 o = target; Game::WarpOutOfCar(p.ped, o);   // took too long
        p.leaveT = 0;
    }
    else if(p.leaveT && now - p.leaveT > 1800) p.leaveT = 0;
    if(p.leftCar && (now - p.leftT > 5000 || !Game::CarPtr(p.leftCar))) DropLeftCar(p);
    if(p.car) {}
    else if(Game::IsCharInAnyCar(p.ped) && !p.leaveT)
    {
        Game::WarpOutOfCar(p.ped, target);   // left a car it was riding in
        p.moveState = -1;
    }

    // --- on-foot movement + animations -------------------------------------------------------
    Vec3 cur;
    if(!Game::GetCharPos(p.ped, cur)) return;
    float d = Dist2D(cur, target);
    float speed = sqrtf(mvel.x * mvel.x + mvel.y * mvel.y);
    // 2.13: the sender tells us how it moves (walk / run / sprint); 2.12 senders: from speed
    int ms = p.hasExt ? MoveStateFromExt(p.extMove, speed) : MoveStateForSpeed(speed);
    // 2.14 "walking in place": legs only move when the drawn position really moves.
    //  - no new data for a while (lag spike): stand, don't keep the last walk task going
    //  - speed decides when it disagrees with the reported state (state is sampled, speed is real)
    float staleLimit = p.interval * 2.5f; if(staleLimit < 300.0f) staleLimit = 300.0f;
    if(haveMo && p.stale > staleLimit) ms = 0;
    else if(speed < 0.12f) ms = 0;
    else if(speed > 3.2f && ms < 6) ms = 6;
    else if(ms == 0) ms = 4;   // 2.17: any real movement walks (a snail-slow push used to stand + slide)
    // speed the legs should show (0 while frozen by lag: then the avatar stands)
    float legSpeed = (haveMo && p.stale > staleLimit) ? 0.0f : speed;
    // 2.17 aim / shots / crouch
    const bool duckWant  = (s.flags & SYNC_DUCKING) != 0;
    const bool aimingNow = p.hasX2 && (p.x2flags & X2_AIMING) && p.weapon >= 22 && p.weapon <= 38;
    const bool bulletSync = now - p.lastShotPkt < 1500 || (p.hasX2 && (p.x2flags & X2_SHOTSYNC) && Game::ShotSyncAvailable());

    if(d > 4.0f || fabsf(cur.z - target.z) > 3.0f)
    {
        Game::SetCharPos(p.ped, target, p.pedBase);   // too far: snap
        cur = target; d = 0;
        p.moveState = -1;
    }

    // --- 2.19: jetpack sync --------------------------------------------------------------------
    if(p.jetpack && !(s.flags & SYNC_DEAD))
    {
        if(!p.jetApplied && (p.jetObj = Game::GiveJetpack(p.ped)) != 0) { p.jetApplied = true; p.moveState = -1; }
        float heading = haveMo ? mo.heading : s.heading;
        float ch = Game::GetCharHeading(p.ped);
        Game::SetCharHeading(p.ped, LerpAngle(ch, heading, dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f));
        float k = dt * 7.0f; if(k > 1.0f) k = 1.0f;
        Vec3 step = { (target.x - cur.x) * k, (target.y - cur.y) * k, (target.z - cur.z) * k };
        Game::NudgeEntity(p.ped, false, step);
        Game::SetMoveSpeed(p.ped, false, mvel);
        p.drawPos = cur; p.drawPos.z += 1.0f; p.drawValid = true;
        return;
    }
    if(p.jetApplied) { Game::RemoveJetpack(p.jetObj); p.jetObj = 0; p.jetApplied = false; p.moveState = -1; }

    // --- 2.15: full animation mirror ---------------------------------------------------------
    // 2.15 senders send their whole blend tree (walk / run / sprint / sneak / crouch / idle /
    // fight / jump / climb / swim / custom IFP...). The avatar gets no walk task at all: it plays
    // exactly those anims at the sender's weights, times and speeds (time-aligned with the drawn
    // position), so the legs move like the real player's. The anims' own root motion moves the
    // ped; we only correct the remaining error.
    {
        const RemotePlayer::AnimFrame* af = nullptr;
        for(int i = 0; i < p.nAF; ++i) { if((double)p.aframes[i].t <= p.renderT || !af) af = &p.aframes[i]; else break; }
        bool mirror = settings.syncAnims && p.hasExt && af && af->full && Game::AnimSyncAvailable();
        bool shootingNow = (s.flags & SYNC_SHOOTING) != 0;
        bool gunUp = shootingNow || (aimingNow && legSpeed < 3.0f);   // 2.32: aiming while walking / strafing raises the gun too
        // 2.19.5 walk / run T-pose fix: sprint mirrored fine, but a plain walk or run (no sprint
        // button) T-posed - the avatar's stand-still task fades the game's own walk / run anims
        // out, which are exactly the ones we copy, so the weights dropped below 100 % and the
        // bind pose showed. While the sender is only walking / running (nothing else full-body
        // going on), the avatar walks with the game's own walk task at the same gait instead;
        // upper-body anims are still copied. Sprint, jumps, fights, climbing... stay mirrored.
        if(mirror && !gunUp && !duckWant && (ms == 4 || ms == 6) && p.extMove != 7 && !aimingNow)
        {
            float otherFull = 0;
            for(int i = 0; i < af->n; ++i)
            {
                const Game::PedAnim& a = af->a[i];
                if(a.flags & 0x10) continue;                                            // partial: arms only
                bool loco = a.id >= 0 && a.id <= 11 && (a.group == 0 || (a.group >= 54 && a.group <= 87));
                if(!loco) otherFull += a.blend;
            }
            if(otherFull < 0.25f) p.gameWalkT = now;
        }
        if(mirror && p.gameWalkT && now - p.gameWalkT < 300 && ms > 0 && ms < 7 && !gunUp) mirror = false;   // short hold: no flicker between the two
        if(mirror)
        {
            if(!p.mirror || (p.moveState != 0 && !gunUp))
            {
                if(p.ducking) { Game::TaskDuck(p.ped, false); p.ducking = false; }
                Game::TaskStandStill(p.ped);
                p.moveState = 0; p.mirror = true;
            }
            float heading = haveMo ? mo.heading : s.heading;
            if(gunUp)
            {
                // 2.17 crouch desync fix: shooting / aiming crouched stays crouched here too
                if(duckWant != p.ducking) { Game::TaskDuck(p.ped, duckWant); p.ducking = duckWant; p.lastDuckFix = now; }
                Vec3 aim = MP_LoadVec3(s.aim);
                AvatarAim(p, aim, shootingNow && !bulletSync, now);   // 2.32 (bullets come one by one with PKT_SHOT)
                p.moveState = -1;
            }
            else
            {
                if(p.gunUp) { p.gunUp = false; Game::TaskStandStill(p.ped); }   // 2.32: gun down = aim task off now (not 1.6 s later)
                float ch = Game::GetCharHeading(p.ped);
                float hd = fmodf(heading - ch + 540.0f, 360.0f) - 180.0f;
                if(fabsf(hd) > 1.0f) Game::SetCharHeading(p.ped, LerpAngle(ch, heading, dt * 12.0f > 1.0f ? 1.0f : dt * 12.0f));
                // pose at the drawn time: advance each anim by the time since that frame
                Game::PedAnim pose[MP_ANIM_SLOTS];
                float el = (float)((p.renderT - (double)af->t) / 1000.0); if(el < 0) el = 0; if(el > 0.5f) el = 0.5f;
                for(int i = 0; i < af->n; ++i) { pose[i] = af->a[i]; pose[i].time += el * pose[i].speed; }
                Game::MirrorPedAnims(p.ped, pose, af->n, p.applied, dt, legSpeed, p.stick, p.extMove);   // 2.19: + joystick
            }
            // position: sideways error out firmly, along the way gently (the anim walks it)
            if(d > 0.05f || fabsf(target.z - cur.z) > 0.35f)
            {
                Vec3 e = { target.x - cur.x, target.y - cur.y, 0 }, step;
                if(speed > 0.3f)
                {
                    float fx = mvel.x / speed, fy = mvel.y / speed;
                    float al = e.x * fx + e.y * fy;
                    float lx = e.x - al * fx, ly = e.y - al * fy;
                    float kl = dt * 6.0f; if(kl > 1.0f) kl = 1.0f;
                    float ka = fabsf(al) > 0.25f ? dt * 4.0f : dt * 1.0f; if(ka > 1.0f) ka = 1.0f;
                    step.x = lx * kl + fx * al * ka; step.y = ly * kl + fy * al * ka;
                }
                else { float k = dt * 8.0f; if(k > 1.0f) k = 1.0f; step.x = e.x * k; step.y = e.y * k; }
                step.z = fabsf(target.z - cur.z) > 0.35f ? (target.z - cur.z) * (dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f) : 0.0f;
                Game::NudgeEntity(p.ped, false, step);
            }
            p.drawPos = cur; p.drawPos.z += 1.0f; p.drawValid = true;
            return;
        }
        if(p.mirror) { p.mirror = false; p.moveState = -1; Game::ReleaseMirroredAnims(p.ped); }   // 2.19.5: hand the legs back to the game
    }

    bool ducking = duckWant;
    if(ducking != p.ducking) { Game::TaskDuck(p.ped, ducking); p.ducking = ducking; p.lastDuckFix = now; }
    else if(now - p.lastDuckFix > 500)
    {
        // 2.17 crouch desync fix: a walk / shoot task can stand the avatar up (or leave it
        // crouched) without us knowing - check the real state and correct it
        p.lastDuckFix = now;
        if(Game::IsCharDucking(p.ped) != ducking) Game::TaskDuck(p.ped, ducking);
    }

    Vec3 aim = MP_LoadVec3(s.aim);
    bool shooting = (s.flags & SYNC_SHOOTING) != 0;
    float heading = haveMo ? mo.heading : s.heading;
    if(p.gunUp && !(shooting || (aimingNow && ms == 0))) { p.gunUp = false; Game::TaskStandStill(p.ped); p.moveState = -1; }   // 2.32
    if(shooting || (aimingNow && ms == 0))
    {
        AvatarAim(p, aim, shooting && !bulletSync, now);   // 2.32: arms really come up
        p.moveState = -1;
    }
    else if(ms > 0)
    {
        // aim the walk task a few metres ahead along the real direction of travel, and only
        // re-issue it when something changes (re-issuing every 250 ms restarted the walk cycle)
        float dx, dy;
        if(speed > 0.3f) { dx = mvel.x / speed; dy = mvel.y / speed; }
        else { float hr = heading * 0.0174533f; dx = -sinf(hr); dy = cosf(hr); }
        // catch up with the legs instead of sliding: ahead of the real position -> slower gait,
        // behind -> faster (walk <-> run <-> sprint), so the feet match the ground
        float along = (target.x - cur.x) * dx + (target.y - cur.y) * dy;
        if(along < -1.0f)     ms = (ms >= 6) ? 4 : 0;
        else if(along > 2.0f) ms = (ms == 4) ? 6 : 7;
        if(ms == 0)
        {
            if(p.moveState != 0) { Game::TaskStandStill(p.ped); p.moveState = 0; }
        }
        else
        {
        Vec3 goal = { target.x + dx * 4.0f, target.y + dy * 4.0f, target.z };
        // re-issue also when the ped is about to reach the old goal (it stopped there = stutter)
        if(ms != p.moveState || Dist2D(goal, p.lastGoal) > 1.5f || Dist2D(cur, p.lastGoal) < 2.5f || now - p.lastTask > 1000)
        {
            Game::TaskGoTo(p.ped, goal, ms);
            p.lastTask = now; p.moveState = ms; p.lastGoal = goal;
        }
        }
    }
    else
    {
        if(p.moveState != 0) { Game::TaskStandStill(p.ped); p.moveState = 0; }
        float ch = Game::GetCharHeading(p.ped);
        float hd = fmodf(heading - ch + 540.0f, 360.0f) - 180.0f;
        if(fabsf(hd) > 3.0f) Game::SetCharHeading(p.ped, LerpAngle(ch, heading, dt * 10.0f > 1.0f ? 1.0f : dt * 10.0f));
    }

    // pull out the remaining position error smoothly instead of teleporting every 0.6 m
    if(d > 0.05f || fabsf(target.z - cur.z) > 0.35f)
    {
        Vec3 e = { target.x - cur.x, target.y - cur.y, 0 };
        Vec3 step;
        if(ms > 0 && speed > 0.3f)
        {
            // 2.14: while walking, fix sideways error firmly but leave the direction of travel
            // to the walk animation (pulling it back is what looked like walking in place)
            float fx = mvel.x / speed, fy = mvel.y / speed;
            float al = e.x * fx + e.y * fy;
            float lx = e.x - al * fx, ly = e.y - al * fy;
            float kl = dt * 6.0f; if(kl > 1.0f) kl = 1.0f;
            float ka = fabsf(al) > 0.3f ? dt * 3.0f : 0.0f; if(ka > 1.0f) ka = 1.0f;
            step.x = lx * kl + fx * al * ka; step.y = ly * kl + fy * al * ka;
        }
        else
        {
            float k = dt * 8.0f; if(k > 1.0f) k = 1.0f;
            step.x = e.x * k; step.y = e.y * k;
        }
        step.z = fabsf(target.z - cur.z) > 0.35f ? (target.z - cur.z) * (dt * 6.0f > 1.0f ? 1.0f : dt * 6.0f) : 0.0f;
        Game::NudgeEntity(p.ped, false, step);
    }

    // 2.13: everything else the player is doing (fighting, jumping, climbing, dancing, phone,
    // idle moves, script anims...) is copied from the sender's anim list
    if(!shooting && !(aimingNow && ms == 0) && p.hasExt) Game::ApplyPedAnims(p.ped, p.anims, settings.syncAnims ? p.animCount : 0, p.applied, ms > 0);

    p.drawPos = cur; p.drawPos.z += 1.0f; p.drawValid = true;
}

// One place that applies a player's colour everywhere: name tag (HUD reads p.color / myColor),
// radar icon, chat names (old lines too) and kill-feed names.
static void ApplyPlayerColor(int id, uint32_t color)
{
    if(id < 0 || id >= MP_MAX_PLAYERS) return;
    RemotePlayer& p = players[id];
    uint32_t old = p.color;
    p.color = color;
    if(id == myId) myColor = color;
    if(p.blip) Game::SetBlipColour(p.blip, color);
    if(old == color || !p.name[0]) return;
    for(auto& l : chat)  if(l.prefixColor == old && l.prefix == p.name) l.prefixColor = color;
    for(auto& k : kills)
    {
        if(k.killerColor == old && k.killer == p.name) k.killerColor = color;
        if(k.victimColor == old && k.victim == p.name) k.victimColor = color;
    }
}

// ------------------------------------------------------------------------------------------
// Network events
// 2.17 (defined further down)
static void NpcReceive(const PktNpc& m, bool mission = false);
static void NpcHit(const PktNpcHit& h);
static void NpcClaim(const PktNpcClaim& c);
static bool NpcIsHost();
static void NpcStatusLine();
static void SyncStatusLines();
static void NpcStop();
static void ReplayRemoteShots(const PktShot& m);
static void StoreVehState(const PktVehState& v);
static void ApplyMissionMarkers(const PktMissionMarkers& m);

static void ShowKicked(const PktKicked& k);   // 2.25 (mp_launcher.inc)
// 3.01: SA-MP features (mp_samp.inc / mp_world.inc)
static void SampOnEvent(const PktEvent& e);
static void SampOnTeam(const PktTeam& t);
static void SampOnTurf(const PktTurf& t);
static void SampOnDialog(const PktDialog& d);
static void SampOnTextDraw(const PktTextDraw& t);
static void SampOnPlayerLeave(int id);
static void SampReset();
static void GamemodesChanged(uint16_t modes);   // 3.02 (mp_world.inc)
static void SrvModesPut(const char* name, int port, uint16_t modes);   // 3.03
static uint16_t s_nRunModes = 0;   // 3.03: gamemodes of the server running on this phone (fixed at creation)
static void SrvCmPut(const char* name, int port, bool allow);   // 3.10
static bool s_bRunCheatMenu = false;  // 3.10: Cheat Menu permission of the server running on this phone (kept per server)
static void SampOnChat(int id, const char* text);
static bool IsSampActor(int h);
static void ProcessSamp(int ped, uint64_t now);
class Events : public INetEvents
{
public:
    void OnConnected(const PktWelcome& w) override
    {
        SampReset();   // 3.01
        myId = w.yourId; myColor = w.color;
        MP_CopyStr(s_szCurServer, w.serverName, sizeof(s_szCurServer));
        s_bWorldSynced = false;
        s_nWorldNoticed = 0;   // 2.18.2: new connection -> each notice may show once again
        // teleport + logo + sound wait for the Connecting feed and all resources (FinishJoin)
        if(s_bQuietRejoin) { s_bQuietRejoin = false; StopFeed(); }   // 2.27: same server, another phone: stay where you are
        else if(s_nFeedStep < 0 && s_nFeedRes < 0) { s_nFeedRes = 0; s_nFeedNext = MP_NowMs(); }
        s_nPrevLocalHealth = -1; s_bLocalDead = false; s_fPendingDamage = 0;
    }
    void OnRejected(uint8_t r) override
    {
        StopFeed(); s_bQuietRejoin = false;
        if(r == REJECT_SHUTDOWN)
        {
            if(!OnLostServer()) SysMsg(COL_YELLOW, "The server was closed by its host.");
            ClearAllPlayers();
            return;
        }
        JoinOverlayCancel();   // 2.19
        if(r == REJECT_OUTDATED)   // 3.04.6
        { SysMsg(COL_RED, "Your MP version is too old. Download MP %s: %s", MP_VERSION_STR, MP_DOWNLOAD_URL); ClearAllPlayers(); return; }
        if(r == REJECT_OLD_SERVER)
        { SysMsg(COL_RED, "This server runs an old MP version (it lets cheaters in). Ask the host to update to MP %s.", MP_VERSION_STR); ClearAllPlayers(); return; }
        if(r == REJECT_PASSWORD) { SysMsg(COL_RED, "Wrong server password. Edit the server and type the right one (max 16 characters)."); ClearAllPlayers(); return; }
        if(r == REJECT_NAME_RESERVED)   // 2.37
        {
            if(HaveAdminKey() && s_nAdminKeyIdx + 1 < s_AdminKeys.size() && s_szLastConnHost[0])
            {   // 3.11.3: admin.txt has another key (old + new): try it on this server
                ++s_nAdminKeyIdx;
                MP_CopyStr(settings.adminKey, s_AdminKeys[s_nAdminKeyIdx].c_str(), sizeof(settings.adminKey));
                client.SetAdminKey(settings.adminKey);
                ClearAllPlayers();
                SysMsg(COL_YELLOW, "This server didn't take your first admin key - trying your other key from administrator.txt...");
                char h[256]; MP_CopyStr(h, s_szLastConnHost, sizeof(h));
                DoConnect(h, s_nLastConnPort);
                return;
            }
            if(HaveAdminKey())
            {   // our key is valid here. A 3.10+ server always accepts it, so the SERVER program is older than 3.10
                // (it only knows the first admin key). Not this phone - updating the mod doesn't help.
                SysMsg(COL_RED, "This server refused your admin key. Your MP %s and administrator.txt are fine: the problem is the SERVER - it runs MP older than 3.11.6, which doesn't know the new admin key.", MP_VERSION_STR);
                SysMsg(COL_YELLOW, "Fix: update the server program. Your 24/7 servers: Termius > console > 7) update. Termux server: mp-server update. A phone host: that player updates MP.");
            }
            else SenjiBlockedMsg();
            if(s_AdminKeys.size() > 1) { s_nAdminKeyIdx = 0; MP_CopyStr(settings.adminKey, s_AdminKeys[0].c_str(), sizeof(settings.adminKey)); client.SetAdminKey(settings.adminKey); }
            ClearAllPlayers(); return;
        }
        if(r == REJECT_BANNED || r == REJECT_KICKED)   // 2.25
        {
            SysMsg(COL_RED, r == REJECT_BANNED ? "You are banned from the server!" : "You were kicked from the server!");
            ClearAllPlayers(); return;
        }
        SysMsg(COL_RED, r == REJECT_FULL ? "Server is full." : "Server uses a different MP version.");
        ClearAllPlayers();
    }
    void OnConnectFailed() override
    {
        if(s_szDirectRelay[0]) { Online::DirectFailed(s_szDirectRelay); s_szDirectRelay[0] = 0; }   // 3.11: next try uses the relay
        StopFeed(); JoinOverlayCancel(); s_bQuietRejoin = false;
        SysMsg(COL_RED, "Could not connect to %s (no response).", s_szJobName[0] ? s_szJobName : "the server");   // 2.16: no IP
        ClearAllPlayers();
    }
    void OnLostConnection() override
    {
        if(s_szDirectRelay[0] && client.IsDirect()) Online::DirectFailed(s_szDirectRelay);   // 3.11: rejoin over the relay
        s_szDirectRelay[0] = 0;
        // decide before the player list is cleared (takeover order = player id order)
        StopFeed(); JoinOverlayCancel();
        if(!OnLostServer()) SysMsg(COL_RED, "Lost connection to the server. (Timeout)");
        ClearAllPlayers();
    }
    void OnPlayerJoin(const PktPlayerJoin& j) override
    {
        if(j.id >= MP_MAX_PLAYERS) return;
        RemotePlayer& p = players[j.id];
        bool wasActive = p.active;
        p.active = true;
        if(!wasActive) p.lastSeq = 0;
        MP_CopyStr(p.name, j.name, MP_NAME_LEN);
        p.skin = Game::IsSafeSkin(j.skin) ? j.skin : 106;   // 2.12: never trust a network skin id (2.19.6: + story characters)
        ApplyPlayerColor(j.id, j.color);
        if(j.id == myId) return;
        if(!wasActive) SysMsg(COL_SYSTEM, "*** %s joined the server.", p.name);
    }
    void OnPlayerLeave(const PktPlayerLeave& l) override
    {
        if(l.id >= MP_MAX_PLAYERS || !players[l.id].active) return;
        SampOnPlayerLeave(l.id);   // 3.01
        SysMsg(COL_SYSTEM, "*** %s left the server.%s", players[l.id].name,
               l.reason == LEAVE_TIMEOUT ? " (Timeout)" : (l.reason == LEAVE_KICK ? " (Kicked)" : ""));
        if(s_bGroup[l.id]) { s_bGroup[l.id] = false; GroupFixFirst(); }   // 2.31
        if(l.id == s_nPairAskedTo) s_nPairAskedTo = MP_INVALID_ID;
        s_bPairAsked[l.id] = false;
        RemovePlayer(l.id);
    }
    void OnPerms(const PktPerms& m) override
    {
        bool was = s_nServerPerms & PERM_MISSION_PAIR, now = m.perms & PERM_MISSION_PAIR;
        bool nc0 = s_nServerPerms & PERM_NO_CHEATS, nc1 = m.perms & PERM_NO_CHEATS;
        s_nServerPerms = m.perms;
        s_bPermsSeen = true;
        // 3.10: remember this custom server's Cheat Menu permission, so if this phone ever hosts it
        // (the host left / nobody runs it yet) it keeps the permission its creator chose
        if(!MP::IsOnDefault() && !MP_IsBcwServerName(client.GetServerName()) && client.GetServerName()[0])
            SrvCmPut(client.GetServerName(), client.GetPort(), (m.perms & PERM_CHEAT_MENU) != 0);
        GamemodesChanged(m.modesMagic == MP_MODES_MAGIC ? m.modes : 0);   // 3.02   // 3.00: allowed -> you just play; not allowed -> Process() refuses at once
        if(nc1 && !nc0) SysMsg(COL_YELLOW, "*** The host disabled cheats on this server (MP cheats + CLEO menu).");
        else if(!nc1 && nc0 && IsConnected()) SysMsg(COL_SYSTEM, "*** The host allowed cheats again.");
        if(was && !now) { ResetPairs(); SysMsg(COL_YELLOW, "*** The host turned off Mission sync."); }
        else if(!was && now && IsConnected()) SysMsg(COL_SYSTEM, "*** Mission sync is allowed here (MP menu > Players online).");
    }
    void OnPair(const PktPair& a) override
    {
        int o = a.other; bool valid = o < MP_MAX_PLAYERS && players[o].active;
        switch(a.action)
        {
            case PAIR_REQUEST:
                if(!valid) break;
                s_bPairAsked[o] = true;
                ShowPairPopup(o);   // 2.19: Accept / Decline popup
                SysMsg(COL_YELLOW, "*** %s wants Mission sync with you: Accept or Decline.", players[o].name);
                break;
            case PAIR_LINKED:
                if(!valid) break;
                if(s_bGroup[o]) break;
                s_bGroup[o] = true; GroupFixFirst(); s_bPairAsked[o] = false; s_nPairAskedTo = MP_INVALID_ID;
                SysMsg(COL_GREEN, "*** Mission sync with %s (group: %d/%d): you see each other and each other's mission markers on missions now.", players[o].name, GroupCount() + 1, MP_MISSION_GROUP_MAX);
                break;
            case PAIR_END:
                if(o < MP_MAX_PLAYERS && s_bGroup[o]) { s_bGroup[o] = false; GroupFixFirst(); if(valid) SysMsg(COL_SYSTEM, "*** Mission sync with %s ended.", players[o].name); }
                break;
            case PAIR_DECLINED:   // 2.19
                if(o == s_nPairAskedTo) s_nPairAskedTo = MP_INVALID_ID;
                SysMsg(COL_RED, "*** %s declined Mission sync.", valid ? players[o].name : "The player");
                break;
            case PAIR_DENIED:
                if(o == s_nPairAskedTo) s_nPairAskedTo = MP_INVALID_ID;
                SysMsg(COL_RED, (s_nServerPerms & PERM_MISSION_PAIR) ? "*** Could not start Mission sync (the player left, didn't ask you, or the group is full: 16 max)." : "*** Mission sync is turned off on this server.");
                break;
        }
    }
    void OnShot(const PktShot& m) override { ReplayRemoteShots(m); }                       // 2.17
    void OnVehState(const PktVehState& v) override { StoreVehState(v); }                 // 2.17
    void OnMissionMarkers(const PktMissionMarkers& m) override { ApplyMissionMarkers(m); }   // 2.17
    void OnNpc(const PktNpc& m) override { NpcReceive(m); }                                 // 2.18
    void OnMissionNpc(const PktNpc& m) override { NpcReceive(m, true); }                    // 3.02
    void OnNpcHit(const PktNpcHit& h) override { NpcHit(h); }
    void OnNpcClaim(const PktNpcClaim& c) override { NpcClaim(c); }
    void OnSync(const PktSync& s, const SyncExt* ext) override
    {
        if(s.id >= MP_MAX_PLAYERS || s.id == myId) return;
        RemotePlayer& p = players[s.id];
        if(!p.active) return;
        // 2.18.3: skip an older position that arrived after a newer one (rejoin restarts seq: big gap -> accept)
        if(p.hasSync && s.seq && p.lastSeq) { int32_t d = (int32_t)(s.seq - p.lastSeq); if(d <= 0 && d > -100000) return; }
        p.lastSeq = s.seq;
        uint64_t now = MP_NowMs();
        if(p.hasSync && !ext)   // 2.12 and older senders: estimate speed from arrival times
        {
            float dt = (now - p.syncTime) / 1000.0f;
            if(dt > 0.03f && dt < 1.0f)
            {
                // 2.11 crash fix (SIGBUS BUS_ADRALN in Events::OnSync on 32-bit): SyncData is packed and
                // sits at offset 6 of the UDP packet, so its floats are NOT 4-byte aligned. A plain
                // float* into it made the compiler use VLDR, which faults on unaligned addresses on
                // ARMv7. Copy the bytes into aligned locals instead.
                Vec3 np = MP_LoadVec3((s.d.flags & SYNC_IN_CAR) ? (const void*)s.d.carPos : (const void*)s.d.pos);
                Vec3 op = MP_LoadVec3((p.sync.flags & SYNC_IN_CAR) ? (const void*)p.sync.carPos : (const void*)p.sync.pos);
                Vec3 v = { (np.x - op.x) / dt, (np.y - op.y) / dt, (np.z - op.z) / dt };
                // light smoothing
                p.vel.x = p.vel.x * 0.4f + v.x * 0.6f; p.vel.y = p.vel.y * 0.4f + v.y * 0.6f; p.vel.z = p.vel.z * 0.4f + v.z * 0.6f;
            }
        }
        memcpy(&p.sync, &s.d, sizeof(SyncData)); p.syncTime = now; p.hasSync = true;
        p.paused = false;   // syncs only come from players who are playing
        {
            uint8_t st = ((s.d.flags & SYNC_MISSION) ? STATUS_MISSION : 0) | ((s.d.flags & SYNC_CUTSCENE) ? STATUS_CUTSCENE : 0);
            if(st != p.status) SetRemoteStatus(p, st, nullptr, (p.status & STATUS_MISSION) && !(st & STATUS_MISSION));
        }
        if(s.d.skin && s.d.skin != p.skin && Game::IsSafeSkin(s.d.skin)) p.skin = s.d.skin;
        PushSnapshot(p, s.d, ext, now);
        if(ext && ext->hasX2)   // 2.17
        {
            p.hasX2 = true; p.x2flags = ext->x2flags; p.interior = ext->interior;
            if(ext->x2flags & X2_VEHFAST)
            {
                p.vfast.steer = ext->steer; p.vfast.gas = ext->gas; p.vfast.brake = ext->brake;
                p.vfast.misc = ext->misc; p.vfast.doomV = ext->doomV; p.vfast.doomH = ext->doomH;
            }
            p.vfast.horn = (ext->x2flags & X2_HORN) != 0;
            // 2.19
            if(ext->x2flags & X2_STICK)
            {
                float sx = ext->stickX / 127.0f, sy = ext->stickY / 127.0f;
                float m = sqrtf(sx * sx + sy * sy); p.stick = m > 1.0f ? 1.0f : m;
            }
            else p.stick = 0.0f;
            p.jetpack = (ext->x2flags & X2_JETPACK) != 0;
            p.invisible = (ext->x2flags & X2_INVISIBLE) != 0;
        }
    }

    // ---- 2.13 environment -------------------------------------------------------------------
    void OnExplosion(const PktExplosion& e) override  { ApplyNetExplosion(e); }
    void OnFire(const PktFire& f) override            { ApplyNetFire(f); }
    void OnPickupAdd(const PktPickupAdd& a) override  { AddNetPickup(a); }
    void OnPickupRemove(const PktPickupRemove& r) override { RemoveNetPickup(r.uid); }
    void OnChat(const PktChat& c) override
    {
        if(c.id >= MP_MAX_PLAYERS) return;
        const RemotePlayer& p = players[c.id];
        std::string nm = p.active ? p.name : "?"; uint32_t nc = p.active ? p.color : COL_WHITE;
        BcwDecorateStr(c.id, nm, nc);   // 3.04: Brief Case War team tag + colour (admins unchanged)
        PushChat({ nm, nc, c.text, COL_WHITE });
        SampOnChat(c.id, c.text);   // 3.01: chat bubble (only drawn when Settings > Chat > Chat bubbles is on)
    }
    void OnDeath(const PktDeath& d) override
    {
        if(d.victim >= MP_MAX_PLAYERS) return;
        bool confirmed = false;
        // 2.37: my predicted kill of this player is already on screen -> confirm it (right weapon), no 2nd line
        for(auto it = kills.begin(); it != kills.end(); ++it)
            if(it->predicted && it->victimId == d.victim && MP_NowMs() - it->time < 6000)
            {
                if(d.killer == myId) { it->predicted = false; if(d.weapon != 255) it->weapon = d.weapon; s_nKillPredMs[d.victim] = 0; confirmed = true; }
                else kills.erase(it);   // somebody else got the kill: replace my guess
                break;
            }
        if(!confirmed)
        {
            KillEntry k;
            k.victim = players[d.victim].name; k.victimColor = players[d.victim].color;
            if(d.killer == myId) { k.killer = MyFeedName(); k.killerColor = myColor; }
            else if(d.killer < MP_MAX_PLAYERS && players[d.killer].active) { k.killer = players[d.killer].name; k.killerColor = players[d.killer].color; }
            else { k.killerColor = 0; }
            if(d.victim == myId && MP::IsLocalAdmin()) k.victim = "[ADMIN] SENJI";
            k.weapon = (d.killer == MP_INVALID_ID) ? 255 : d.weapon;
            k.time = MP_NowMs();   // 2.16: fades out after 12 s
            k.victimId = d.victim;
            if(k.killerColor && d.killer < MP_MAX_PLAYERS) BcwDecorateStr(d.killer, k.killer, k.killerColor);   // 3.04: team colours
            BcwDecorateStr(d.victim, k.victim, k.victimColor);
            PushKillEntry(k);
        }
        if(d.killer == myId && d.victim != myId) ++s_nMyScoreFwd;   // 2.19 scoreboard
        GamesOnDeath(d);   // 2.21
    }
    void OnPlayerInfo(const PktPlayerInfo& pi) override   // 2.19
    {
        if(pi.id >= MP_MAX_PLAYERS || pi.id == myId) return;
        players[pi.id].ping = pi.ping; players[pi.id].score = pi.score;
    }
    void OnKicked(const PktKicked& k) override { ShowKicked(k); }
    void OnEvent(const PktEvent& e) override { SampOnEvent(e); }          // 3.01
    void OnTeam(const PktTeam& t) override { SampOnTeam(t); }
    void OnTurf(const PktTurf& t) override { SampOnTurf(t); }
    void OnDialog(const PktDialog& d) override { SampOnDialog(d); }
    void OnTextDraw(const PktTextDraw& t) override { SampOnTextDraw(t); }
    void OnBcw(const uint8_t* d, int len) override { BcwOnPacket(d, len); }   // 3.04   // 2.25: the dialog opens; PKT_REJECT follows
    void OnJack(const PktJack& j) override   // 2.19: somebody pulls me out of my car
    {
        if(j.victim != myId || !Game::IsReady()) return;
        int ped = Game::LocalPed();
        if(!Game::IsCharInAnyCar(ped)) return;
        int car = Game::GetCarCharIsIn(ped);
        if(Game::GetDriverOfCar(car) != ped) return;
        Vec3 cp; Game::GetCarPos(car, cp);
        float h = Game::GetCarHeading(car) * 0.0174533f;
        Game::WarpOutOfCar(ped, { cp.x - cosf(h) * 2.2f, cp.y - sinf(h) * 2.2f, cp.z });
        if(j.from < MP_MAX_PLAYERS && players[j.from].active) SysMsg(COL_YELLOW, "*** %s pulled you out of your car.", players[j.from].name);
    }
    void OnDamage(const PktDamage& d) override
    {
        if(d.amount <= 0 || d.amount > 1000) return;
        if(Game::IsPaused()) return;   // can't be hurt while paused
        if(s_Cheat.invincible) return;   // 2.19 /invincible
        bool partner = IsPartner(d.from) && !s_bLocalCutscene;   // 2.19: mission-sync partners share one world
        if(s_bLocalSeparate && !partner) return;   // on a mission / in a cutscene: own story world
        if(!partner && d.from < MP_MAX_PLAYERS && (players[d.from].status || (players[d.from].sync.flags & (SYNC_MISSION | SYNC_CUTSCENE)))) return;   // 1000 = instant kill (headshot / explosion)
        float amount = d.amount;
        // 2.37: the victim caps rams too, so older / modified phones can't one-hit people with a car
        if(d.from < MP_MAX_PLAYERS)
        {
            const SyncData& as = players[d.from].sync;
            int me = Game::LocalPed();
            bool near = false;
            if(me && (as.flags & SYNC_IN_CAR))
            {
                Vec3 mp; Game::GetCharPos(me, mp);
                float dx = as.carPos[0] - mp.x, dy = as.carPos[1] - mp.y, dz = as.carPos[2] - mp.z;
                near = dx * dx + dy * dy + dz * dz < 10.0f * 10.0f;
            }
            bool ram = d.weapon == MP_RAM_WEAPON || d.weapon == 50 ||
                       (near && !(as.flags & SYNC_SHOOTING) && !Game::IsCharInAnyCar(me));
            if(ram)
            {
                static uint64_t s_nRamGotMs[MP_MAX_PLAYERS] = {};
                uint64_t now = MP_NowMs();
                if((as.flags & SYNC_IN_CAR) && MP_IsTrailerModel(as.carModel)) return;   // flying Tug Stairs
                if(now - s_nRamGotMs[d.from] < MP_RAM_COOLDOWN_MS) return;
                s_nRamGotMs[d.from] = now;
                if(amount > MP_RAM_MAX_DAMAGE) amount = MP_RAM_MAX_DAMAGE;
            }
            if(ram && d.weapon != 50) { s_fPendingDamage += amount; s_nLastDamager = d.from; s_nLastDamageWeapon = MP_RAM_WEAPON; s_nLastDamageTime = MP_NowMs(); return; }   // 2.37: kill feed shows the car-hit icon, not the driver's weapon
        }
        s_fPendingDamage += amount;
        s_nLastDamager = d.from; s_nLastDamageWeapon = d.weapon; s_nLastDamageTime = MP_NowMs();
    }
    void OnServerMessage(const PktServerMsg& m) override { MP::AddSystemMessage(m.color, m.text); }
    void OnSetColor(const PktSetColor& c) override { ApplyPlayerColor(c.id, c.color); }
    void OnVoice(int id, uint16_t seq, uint8_t flags, const uint8_t* opus, int len) override   // 3.11
    {
        if(id < 0 || id >= MP_MAX_PLAYERS || id == myId || !players[id].active || !settings.voiceChat) return;
        if(VoiceMutedId(id)) return;   // muted in the player list: not even decoded
        Voice::OnPacket(id, seq, flags, opus, len);
    }
    void OnClothes(const PktClothes& c0) override   // 3.10: clothing sync
    {
        if(c0.id >= MP_MAX_PLAYERS || c0.id == myId) return;
        RemotePlayer& p = players[c0.id];
        if(!p.active) return;
        PktClothes c = c0;
        if(!MP_ClothesSane(c)) return;
        uint32_t h = MP_ClothesHash(c);
        if(h == p.clothesHash) return;
        p.clothes = c; p.clothesHash = h; p.clothesRetryAt = 0;   // the avatar is rebuilt in UpdateRemote
    }
    void OnWorld(const PktWorld& w) override { s_World = w; s_bWorldPending = true; }
    void OnStatus(const PktStatus& st) override
    {
        if(st.id >= MP_MAX_PLAYERS || st.id == myId) return;
        RemotePlayer& p = players[st.id];
        if(!p.active) return;
        SetRemoteStatus(p, st.state & (STATUS_MISSION | STATUS_CUTSCENE), st.mission, true);
    }
    void OnPause(const PktPause& pp) override
    {
        if(pp.id >= MP_MAX_PLAYERS || pp.id == myId) return;
        RemotePlayer& p = players[pp.id];
        p.paused = pp.paused != 0;
        if(p.paused) { p.vel = { 0, 0, 0 }; p.syncTime = MP_NowMs(); } // stand still, no sliding
    }
};
static Events s_Events;

// ------------------------------------------------------------------------------------------
static void SendLocalSync(int ped, uint64_t now)
{
    PktSync p; memset(&p, 0, sizeof(p));
    p.type = PKT_SYNC; p.id = myId; p.seq = ++s_nSeq;
    SyncData& d = p.d;
    Vec3 pos; Game::GetCharPos(ped, pos);
    d.pos[0] = pos.x; d.pos[1] = pos.y; d.pos[2] = pos.z;
    d.heading = Game::GetCharHeading(ped);
    float hr = d.heading * 0.0174533f;
    d.aim[0] = pos.x - sinf(hr) * 25.0f; d.aim[1] = pos.y + cosf(hr) * 25.0f; d.aim[2] = pos.z + 0.4f;
    int hp = Game::GetCharHealth(ped); if(hp < 0) hp = 0; if(hp > 255) hp = 255;
    int ar = Game::GetCharArmour(ped); if(ar < 0) ar = 0; if(ar > 255) ar = 255;
    d.health = (uint8_t)hp; d.armour = (uint8_t)ar;
    d.weapon = (uint8_t)Game::GetCharWeapon(ped);
    d.speed = Game::GetCharSpeed(ped);
    d.skin = (uint16_t)(settings.realSkin ? Game::LocalSkinForNet(settings.skin) : settings.skin);   // 2.19.6: your real model
    if(s_bLocalDead) d.flags |= SYNC_DEAD;
    if(Game::IsCharShooting(ped)) d.flags |= SYNC_SHOOTING;
    if(Game::IsCharDucking(ped)) d.flags |= SYNC_DUCKING;
    if(s_bLocalMission)  d.flags |= SYNC_MISSION;
    if(s_bLocalCutscene) d.flags |= SYNC_CUTSCENE;
    if(s_bLocalScene)    d.flags |= SYNC_SCENE;   // 2.19
    int rideOwner = -1, rideSeat = -1;   // 2.15: passenger in another player's car
    if(Game::IsCharInAnyCar(ped))
    {
        int car = Game::GetCarCharIsIn(ped);
        if(car > 0)
        {
            int driver = Game::GetDriverOfCar(car);
            static int s_nRideCar = 0, s_nRideOwner = -1;
            if(driver > 0 && driver != ped)
                for(int i = 0; i < MP_MAX_PLAYERS; ++i)
                    if(i != myId && players[i].ped && (players[i].car == car || players[i].ped == driver)) { rideOwner = i; break; }
            // the driver got out: we still sit in THEIR car (it stays parked on their phone too)
            if(rideOwner < 0 && driver != ped && car == s_nRideCar && s_nRideOwner >= 0) rideOwner = s_nRideOwner;
            if(rideOwner >= 0) { s_nRideCar = car; s_nRideOwner = rideOwner; } else { s_nRideCar = 0; s_nRideOwner = -1; }
            if(rideOwner >= 0) { rideSeat = Game::GetPassengerSeat(car, ped); if(rideSeat < 0) rideSeat = 0; }
            d.flags |= SYNC_IN_CAR;
            if(rideOwner < 0) d.flags |= SYNC_DRIVER;   // driving, or a passenger of a normal (NPC) car
            d.carModel = (uint16_t)Game::GetCarModel(car);
            Vec3 cp; Game::GetCarPos(car, cp);
            d.carPos[0] = cp.x; d.carPos[1] = cp.y; d.carPos[2] = cp.z;
            d.carHeading = Game::GetCarHeading(car);
        }
    }
    // ---- 2.13 extension: sender clock, real velocity, move state, car rotation, anims ----
    SyncExt e; memset(&e, 0, sizeof(e));
    e.ver = MP_SYNC_EXT_VER;
    e.time = (uint32_t)now;
    {
        bool inCar = (d.flags & SYNC_IN_CAR) != 0;
        Vec3 at = MP_LoadVec3(inCar ? (const void*)d.carPos : (const void*)d.pos);
        static Vec3 s_LastAt; static uint64_t s_LastAtT = 0; static bool s_LastInCar = false; static Vec3 s_Vel;
        float sdt = s_LastAtT ? (now - s_LastAtT) / 1000.0f : 0.0f;
        if(sdt > 0.02f && sdt < 0.5f && inCar == s_LastInCar)
        {
            Vec3 v = { (at.x - s_LastAt.x) / sdt, (at.y - s_LastAt.y) / sdt, (at.z - s_LastAt.z) / sdt };
            if(v.x * v.x + v.y * v.y + v.z * v.z > 90.0f * 90.0f) v = { 0, 0, 0 };   // teleport
            s_Vel.x = s_Vel.x * 0.3f + v.x * 0.7f; s_Vel.y = s_Vel.y * 0.3f + v.y * 0.7f; s_Vel.z = s_Vel.z * 0.3f + v.z * 0.7f;
        }
        else s_Vel = { 0, 0, 0 };
        s_LastAt = at; s_LastAtT = now; s_LastInCar = inCar;
        memcpy(e.vel, &s_Vel, 12);
        e.moveState = (uint8_t)Game::GetPedMoveState(ped);
        e.pedState  = (uint8_t)Game::GetPedState(ped);
        if(inCar)
        {
            int car = Game::GetCarCharIsIn(ped);
            float f[3], u[3];
            if(car > 0 && Game::GetCarRotation(car, f, u)) { memcpy(e.carFwd, f, 12); memcpy(e.carUp, u, 12); }
        }
        else if(Game::AnimSyncAvailable())
        {
            Game::PedAnim pa[MP_ANIM_SLOTS];
            int n = Game::GetPedAnims(ped, pa, MP_ANIM_SLOTS, true);   // 2.15: whole blend tree
            e.animCount = (uint8_t)n;
            e.fullAnims = 1;
            for(int i = 0; i < n; ++i)
            {
                AnimSync a; memset(&a, 0, sizeof(a));
                a.hash = pa[i].hash; a.group = pa[i].group; a.id = pa[i].id; a.time = pa[i].time;
                float bl = pa[i].blend * 255.0f; a.blend = (uint8_t)(bl < 0 ? 0 : (bl > 255 ? 255 : bl));
                float sp = pa[i].speed * 50.0f;  a.speed = (uint8_t)(sp < 0 ? 0 : (sp > 255 ? 255 : sp));
                a.flags = pa[i].flags;
                memcpy(a.block, pa[i].block, 16); a.block[15] = 0;
                memcpy(&e.anim[i], &a, sizeof(a));
            }
        }
    }
    if(rideOwner >= 0) { e.hasVeh = 1; e.vehOwner = (uint8_t)rideOwner; e.vehSeat = (int8_t)rideSeat; }
    // ---- 2.17: aim, interior, driver inputs ----
    e.hasX2 = 1;
    e.interior = (uint8_t)Game::GetAreaVisible();
    if(Game::ShotSyncAvailable()) e.x2flags |= X2_SHOTSYNC;
    if(NpcIsHost()) e.x2flags |= X2_NPCHOST;   // 2.18
    // 2.19: joystick (remote walk / run / turn blend), jetpack, /invisible
    if(!(d.flags & SYNC_IN_CAR))
    {
        int sx = 0, sy = 0;
        if(Game::GetStick(sx, sy))
        {
            if(sx < -127) sx = -127; if(sx > 127) sx = 127; if(sy < -127) sy = -127; if(sy > 127) sy = 127;
            if(sx > 6 || sx < -6 || sy > 6 || sy < -6) { e.x2flags |= X2_STICK; e.stickX = (int8_t)sx; e.stickY = (int8_t)sy; }
        }
        if(Game::IsLocalOnJetpack()) e.x2flags |= X2_JETPACK;
    }
    if(s_Cheat.invisible || JoinFlowActive() || BcwHidden()) e.x2flags |= X2_INVISIBLE;   // 3.04: Brief Case War lobby / respawn wait
    if(!(d.flags & SYNC_IN_CAR) && d.weapon >= 22 && d.weapon <= 38 && (Game::IsPedAimingGun(ped) || (d.flags & SYNC_SHOOTING)))
    {
        // the real point the player aims at (was: 25 m straight ahead of the body)
        Vec3 ap;
        if(Game::GetAimPoint(ap))
        {
            float dx = ap.x - pos.x, dy = ap.y - pos.y;
            if(dx * dx + dy * dy < 400.0f * 400.0f) { d.aim[0] = ap.x; d.aim[1] = ap.y; d.aim[2] = ap.z; }
        }
        if(Game::IsPedAimingGun(ped)) e.x2flags |= X2_AIMING;
    }
    if((d.flags & SYNC_IN_CAR) && (d.flags & SYNC_DRIVER))
    {
        int car = Game::GetCarCharIsIn(ped);
        Game::VehFast f;
        if(car > 0 && Game::ReadVehFast(car, f))
        {
            e.x2flags |= X2_VEHFAST;
            if(f.horn) e.x2flags |= X2_HORN;
            e.steer = f.steer; e.gas = f.gas; e.brake = f.brake; e.misc = f.misc; e.doomV = f.doomV; e.doomH = f.doomH;
        }
    }
    // 2.14: standing still and nothing changed -> 4 updates per second are enough
    // (20 players idling at the spawn were half of all relay traffic)
    // 2.15: "nothing changed" includes the anims: same looping anims at about the same weight
    bool sameAnims;
    {
        static AnimSync s_LastA[MP_ANIM_SLOTS]; static int s_LastN = -1;
        sameAnims = e.animCount == s_LastN;
        for(int i = 0; sameAnims && i < e.animCount; ++i)
        {
            AnimSync x, y; memcpy(&x, &e.anim[i], sizeof(x)); memcpy(&y, &s_LastA[i], sizeof(y));
            if(x.hash != y.hash || x.group != y.group || x.id != y.id || !(x.flags & 0x2) || abs((int)x.blend - (int)y.blend) > 25) sameAnims = false;
        }
        s_LastN = e.animCount; memcpy(s_LastA, e.anim, sizeof(s_LastA));
    }
    {
        static SyncData s_Last; static uint64_t s_LastSent = 0; static bool s_HasLast = false;
        float v[3]; memcpy(v, e.vel, 12);
        Vec3 a = MP_LoadVec3(d.pos), b = MP_LoadVec3(s_Last.pos);
        float dh = fabsf(d.heading - s_Last.heading); if(dh > 180.0f) dh = 360.0f - dh;
        static uint8_t s_LastX2 = 0;
        bool x2same = e.x2flags == s_LastX2; s_LastX2 = e.x2flags;
        bool idle = s_HasLast && x2same && !(d.flags & SYNC_IN_CAR) && sameAnims && d.flags == s_Last.flags &&
                    d.weapon == s_Last.weapon && d.health == s_Last.health && d.armour == s_Last.armour && d.skin == s_Last.skin &&
                    v[0] * v[0] + v[1] * v[1] + v[2] * v[2] < 0.04f && dh < 2.0f &&
                    (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z) < 0.0025f;
        s_nLastSync = now;
        if(idle && now - s_LastSent < 250) return;
        s_Last = d; s_HasLast = true; s_LastSent = now;
    }
    uint8_t out[sizeof(PktSync) + sizeof(SyncExt)];
    memcpy(out, &p, sizeof(p));
    int el = MP_EncodeSyncExt(e, out + sizeof(p));   // 2.15 compact v3: ~14 bytes + 9 per anim
    client.Send(out, (int)sizeof(p) + el);
}

// 2.15: the avatar driving another player's car can't be dragged out (it's them), so pressing
// "enter" used to do nothing. Now it gets you in as their passenger; they see you in their car.
static void ProcessPassengerEntry(int ped, uint64_t now)
{
    static int s_nCar = 0; static uint64_t s_nT = 0;
    if(Game::IsCharInAnyCar(ped) || !Game::IsCharGettingInToACar(ped)) return;
    int car = Game::GetCarCharIsUsing(ped);
    Vec3 me; Game::GetCharPos(ped, me);
    int owner = -1; float best = 6.0f;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = players[i];
        if(i == myId || !p.ped || !p.car) continue;
        if(car > 0 && p.car != car) continue;
        if(Game::GetDriverOfCar(p.car) != p.ped) continue;
        Vec3 cp; if(!Game::GetCarPos(p.car, cp)) continue;
        float dd = Dist2D(me, cp);
        if(dd < best) { best = dd; owner = i; }
    }
    if(owner < 0) return;
    int target = players[owner].car;
    if(target == s_nPassBtnCar && now - s_nPassBtnT < 8000) return;   // 2.19.1: Passenger button already did it
    if(target == s_nCar && now - s_nT < 6000) return;   // already walking to the door
    int seat = Game::FreePassengerSeat(target, 0);
    if(seat < 0)                                         // 2.19: full -> pull the driver out (2.19 servers)
    {
        if(s_nServerPerms & PERM_V219) { TryJack(ped, owner, now); s_nCar = target; s_nT = now; }
        else { Game::ClearTasksNow(ped); SysMsg(COL_YELLOW, "That car is full."); s_nCar = target; s_nT = now; }
        return;
    }
    Game::ClearTasksNow(ped);
    Game::TaskEnterCarAsPassenger(ped, target, seat);
    s_nCar = target; s_nT = now;
}

static void ProcessLocalCombat(int ped)
{
    int hp = Game::GetCharHealth(ped);

    // Undo local damage caused by avatars (their bullets are cosmetic; real damage arrives via PKT_DAMAGE)
    if(s_nPrevLocalHealth > 0 && hp > 0 && hp < s_nPrevLocalHealth)
    {
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            RemotePlayer& p = players[i];
            if(p.active && p.ped && Game::HasCharBeenDamagedByChar(ped, p.ped))
            {
                Game::SetCharHealth(ped, s_nPrevLocalHealth);
                hp = s_nPrevLocalHealth;
                Game::ClearCharLastDamageEntity(ped);
                break;
            }
        }
    }

    // Apply network damage
    if(s_fPendingDamage > 0 && hp > 0)
    {
        int dmg = (int)(s_fPendingDamage + 0.5f);
        s_fPendingDamage = 0;
        hp -= dmg; if(hp < 0) hp = 0;
        Game::SetCharHealth(ped, hp);
    }
    s_nPrevLocalHealth = hp;

    bool dead = hp <= 0 || Game::IsCharDead(ped);
    if(dead && !s_bLocalDead)
    {
        s_bLocalDead = true;
        // 2.13: drop the weapon we were holding for everybody (server hands out the pickup)
        if(!s_bLocalSeparate && settings.syncEnv)
        {
            int w = Game::GetCharWeapon(ped);
            if(w > 0 && w <= 46)
            {
                int ammo = Game::GetAmmo(ped, w);
                if(w <= 15) ammo = 1;   // melee
                if(ammo > 0)
                {
                    Vec3 at; Game::GetCharPos(ped, at);
                    PktPickupDrop pd; pd.type = PKT_PICKUP_DROP; pd.weapon = (uint8_t)w; pd.ammo = (uint16_t)(ammo > 65535 ? 65535 : ammo);
                    pd.pos[0] = at.x + 0.6f; pd.pos[1] = at.y; pd.pos[2] = at.z - 0.5f;
                    client.Send(&pd, sizeof(pd));
                }
            }
        }
        PktDeath d; d.type = PKT_DEATH; d.victim = myId;
        bool recent = MP_NowMs() - s_nLastDamageTime < 15000;
        d.killer = recent ? s_nLastDamager : MP_INVALID_ID;
        d.weapon = recent ? s_nLastDamageWeapon : 255;
        client.Send(&d, sizeof(d));
    }
    else if(!dead && s_bLocalDead)
    {
        s_bLocalDead = false;
        s_nLastDamager = MP_INVALID_ID;
        s_nPrevLocalHealth = hp;
    }
}

// Pick which remote players get a real avatar: inside MP_STREAM_IN (kept until MP_STREAM_OUT),
// nearest first, at most MP_STREAM_MAX.
static void UpdateStreaming()
{
    struct Cand { float d; int id; };
    Cand c[MP_MAX_PLAYERS]; int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& p = players[i];
        if(!p.active || !p.hasSync || i == myId || !myDrawValid) { s_bStreamed[i] = false; continue; }
        Vec3 at = MP_LoadVec3((p.sync.flags & SYNC_IN_CAR) ? (const void*)p.sync.carPos : (const void*)p.sync.pos);
        float dx = at.x - myDrawPos.x, dy = at.y - myDrawPos.y, dz = at.z - myDrawPos.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz * 0.25f);
        if(d > (s_bStreamed[i] ? MP_STREAM_OUT : MP_STREAM_IN)) { s_bStreamed[i] = false; continue; }
        c[n].d = s_bStreamed[i] ? d * 0.9f : d;   // small bonus: avatars already in the world stay
        c[n].id = i; ++n;
    }
    if(n > MP_STREAM_MAX)
    {
        std::nth_element(c, c + MP_STREAM_MAX, c + n, [](const Cand& a, const Cand& b) { return a.d < b.d; });
        for(int k = MP_STREAM_MAX; k < n; ++k) s_bStreamed[c[k].id] = false;
        n = MP_STREAM_MAX;
    }
    for(int k = 0; k < n; ++k) s_bStreamed[c[k].id] = true;
}


// ------------------------------------------------------------------------------------------
// 2.17: every-bullet shot sync, car state, mission sync markers, pause-map player markers
// ------------------------------------------------------------------------------------------
static ShotEntry s_ShotBuf[MP_SHOT_MAX];
static int       s_nShotBuf = 0;
static void OnLocalShot(int kind, int weapon, const Vec3& a, const Vec3& b, float force)
{
    if(client.GetState() != NET_CONNECTED || myId == MP_INVALID_ID) return;
    if(s_nShotBuf >= MP_SHOT_MAX) return;              // 8 per frame is plenty (shotguns send 1)
    ShotEntry e; memset(&e, 0, sizeof(e));
    e.kind = (uint8_t)kind; e.weapon = (uint8_t)weapon; e.force = force;
    float fa[3] = { a.x, a.y, a.z }, fb[3] = { b.x, b.y, b.z };
    memcpy(e.a, fa, sizeof(fa)); memcpy(e.b, fb, sizeof(fb));
    s_ShotBuf[s_nShotBuf++] = e;
}
static void FlushShots()
{
    if(!s_nShotBuf) return;
    if(client.GetState() == NET_CONNECTED && myId != MP_INVALID_ID && !s_bLocalCutscene)
    {
        PktShot m; memset(&m, 0, sizeof(m));
        m.type = PKT_SHOT; m.id = myId; m.count = (uint8_t)s_nShotBuf;
        memcpy(m.e, s_ShotBuf, sizeof(ShotEntry) * s_nShotBuf);
        client.Send(&m, MP_PKT_SHOT_HDR + s_nShotBuf * (int)sizeof(ShotEntry));
    }
    s_nShotBuf = 0;
}

static void ReplayRemoteShots(const PktShot& m)
{
    if(m.id >= MP_MAX_PLAYERS || m.id == myId) return;
    RemotePlayer& p = players[m.id];
    if(!p.active) return;
    p.lastShotPkt = MP_NowMs();
    if(!p.ped || p.deadApplied || Game::IsPaused()) return;   // not visible here (hidden / far / dead)
    Vec3 me; int lp = Game::LocalPed();
    bool haveMe = lp > 0 && Game::GetCharPos(lp, me);
    for(int i = 0; i < m.count && i < MP_SHOT_MAX; ++i)
    {
        ShotEntry e; memcpy(&e, &m.e[i], sizeof(e));
        float fa[3], fb[3]; memcpy(fa, e.a, sizeof(fa)); memcpy(fb, e.b, sizeof(fb));
        Vec3 a = { fa[0], fa[1], fa[2] }, b = { fb[0], fb[1], fb[2] };
        if(!(a.x == a.x) || !(b.x == b.x)) continue;
        if(haveMe && Dist2D(a, me) > 350.0f) continue;
        if(e.kind == 0) Game::ReplayBullet(p.ped, e.weapon, a, b);
        else if(e.kind == 1) Game::ReplayProjectile(p.ped, 0, e.weapon, a, b, e.force);
        else if(e.kind == 2 && p.car) Game::ReplayProjectile(0, p.car, e.weapon, a, b, e.force);
    }
}

// the car I drive: its look and state for everybody around (on change, at least every 2 s)
static void SendVehState(int ped, uint64_t now)
{
    static uint64_t s_Last = 0, s_LastFull = 0; static PktVehState s_Prev; static bool s_Has = false;
    if(now - s_Last < 300) return;
    s_Last = now;
    if(!Game::IsCharInAnyCar(ped)) { s_Has = false; return; }
    int car = Game::GetCarCharIsIn(ped);
    if(car <= 0 || Game::GetDriverOfCar(car) != ped) { s_Has = false; return; }
    Game::VehState vs;
    if(!Game::ReadVehState(car, vs)) return;
    PktVehState v; memset(&v, 0, sizeof(v));
    v.type = PKT_VEH_STATE; v.id = myId; v.model = (uint16_t)Game::GetCarModel(car);
    memcpy(v.col, vs.col, 4); v.paintjob = vs.paintjob; v.lock = vs.lock; v.flags = vs.flags; v.isAuto = vs.isAuto ? 1 : 0;
    memcpy(v.doors, vs.doors, 6); memcpy(v.wheels, vs.wheels, 4);
    v.panels = vs.panels; v.lights = vs.lights; v.health = vs.health;
    memcpy(v.mods, vs.mods, sizeof(v.mods)); memcpy(v.doorOpen, vs.doorOpen, 6);
    Vec3 cp; Game::GetCarPos(car, cp); float fp[3] = { cp.x, cp.y, cp.z }; memcpy(v.pos, fp, sizeof(fp));
    // health changes a little all the time: only resend for 25+ points (or every 2 s)
    bool same = false;
    if(s_Has)
    {
        PktVehState a = v, b = s_Prev;
        int dh = (int)a.health - (int)b.health;
        a.health = b.health = 0; memset(a.pos, 0, sizeof(a.pos)); memset(b.pos, 0, sizeof(b.pos));
        same = !memcmp(&a, &b, sizeof(a)) && dh > -25 && dh < 25;
    }
    if(same && now - s_LastFull < 2000) return;
    s_Prev = v; s_Has = true; s_LastFull = now;
    client.Send(&v, sizeof(v));
}

static void StoreVehState(const PktVehState& v)
{
    if(v.id >= MP_MAX_PLAYERS || v.id == myId) return;
    RemotePlayer& p = players[v.id];
    if(!p.active || v.model < 400 || v.model > 611) return;
    Game::VehState& w = p.vwant;
    memcpy(w.col, v.col, 4); w.paintjob = v.paintjob; w.lock = v.lock; w.flags = v.flags; w.isAuto = v.isAuto != 0;
    memcpy(w.doors, v.doors, 6); memcpy(w.wheels, v.wheels, 4);
    uint32_t pn, li; uint16_t hp; memcpy(&pn, (const uint8_t*)&v + offsetof(PktVehState, panels), 4);
    memcpy(&li, (const uint8_t*)&v + offsetof(PktVehState, lights), 4); memcpy(&hp, (const uint8_t*)&v + offsetof(PktVehState, health), 2);
    w.panels = pn; w.lights = li; w.health = hp;
    memcpy(w.mods, (const uint8_t*)&v + offsetof(PktVehState, mods), sizeof(w.mods));
    for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i) if(w.mods[i] && (w.mods[i] < 1000 || w.mods[i] > 1193)) w.mods[i] = 0;
    memcpy(w.doorOpen, v.doorOpen, 6);
    if(w.paintjob < -1 || w.paintjob > 7) w.paintjob = -1;
    w.valid = true; p.vwantModel = v.model; p.vApplied = 0;
}

// mission sync: while I'm on a mission, my partner gets my mission's radar markers (and sees
// the checkpoint markers in the world); theirs come to me. Gone when the mission ends.
static void MissionNpcSend(uint64_t now);   // 3.02 (after NpcSend)
static void ProcessMissionSync(uint64_t now)
{
    static uint64_t s_LastSend = 0;
    bool synced = AnyGroupActive();   // 2.31: anyone in my group
    if(synced && s_bLocalMission && client.GetState() == NET_CONNECTED) MissionNpcSend(now);   // 3.02
    if(synced && s_bLocalMission && now - s_LastSend >= 1000)
    {
        s_LastSend = now;
        int skip[MP_MAX_PLAYERS * 2 + 8]; int ns = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            const RemotePlayer& q = players[i];
            if(q.blip && ns < (int)(sizeof(skip) / sizeof(skip[0]))) skip[ns++] = q.blip;
            for(int k = 0; k < q.mmCount && ns < (int)(sizeof(skip) / sizeof(skip[0])); ++k) skip[ns++] = q.mmBlips[k];
        }
        Game::MissionBlip mb[MP_MISSION_MARKERS_MAX];
        int n = Game::ReadMissionBlips(mb, MP_MISSION_MARKERS_MAX, skip, ns);
        PktMissionMarkers m; memset(&m, 0, sizeof(m));
        m.type = PKT_MISSION_MARKERS; m.id = myId; m.count = (uint8_t)n;
        for(int i = 0; i < n; ++i)
        {
            MissionMarker mk; memset(&mk, 0, sizeof(mk));
            float fp[3] = { mb[i].x, mb[i].y, mb[i].z }; memcpy(mk.pos, fp, sizeof(fp));
            mk.colour = mb[i].colour; mk.display = mb[i].display; mk.kind = mb[i].kind;
            memcpy(&m.m[i], &mk, sizeof(mk));
        }
        client.Send(&m, MP_PKT_MM_HDR + n * (int)sizeof(MissionMarker));
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& q = players[i];
        if(!q.mmCount) continue;
        bool keep = IsPartner(i) && q.active && now - q.mmTime < 3500 && !s_bLocalCutscene;
        if(!keep) ClearMissionMarkers(q);
    }
}

static void ApplyMissionMarkers(const PktMissionMarkers& m)
{
    if(m.id >= MP_MAX_PLAYERS || m.id == myId || !IsPartner(m.id)) return;
    RemotePlayer& p = players[m.id];
    if(!p.active || !Game::IsReady()) return;
    p.mmTime = MP_NowMs();
    // same markers as last time (positions within 1.5 m): keep them, no blinking
    bool same = m.count == p.mmCount;
    static float s_Pos[MP_MAX_PLAYERS][MP_MISSION_MARKERS_MAX][3];
    for(int i = 0; same && i < m.count; ++i)
    {
        MissionMarker mk; memcpy(&mk, &m.m[i], sizeof(mk));
        float fp[3]; memcpy(fp, mk.pos, sizeof(fp));
        float dx = fp[0] - s_Pos[m.id][i][0], dy = fp[1] - s_Pos[m.id][i][1];
        if(dx * dx + dy * dy > 1.5f * 1.5f) same = false;
    }
    if(same) return;
    ClearMissionMarkers(p);
    p.mmTime = MP_NowMs();
    for(int i = 0; i < m.count && i < MP_MISSION_MARKERS_MAX; ++i)
    {
        MissionMarker mk; memcpy(&mk, &m.m[i], sizeof(mk));
        float fp[3]; memcpy(fp, mk.pos, sizeof(fp));
        if(!(fp[0] == fp[0])) continue;
        memcpy(s_Pos[m.id][p.mmCount], fp, sizeof(fp));
        Vec3 at = { fp[0], fp[1], fp[2] };
        // a checkpoint (coord marker) shows its marker in the world too; car / ped targets: radar only
        int b = Game::AddCoordBlip(at, mk.colour, mk.kind == 4 ? (mk.display ? mk.display : 3) : 2);
        if(b) p.mmBlips[p.mmCount++] = b;
    }
}

// pause-menu map: every player's icon in their colour + the name they set
static int MapMarkers(Game::MapMarker* out, int maxOut)
{
    if(client.GetState() != NET_CONNECTED || !settings.showRadarIcons) return 0;
    int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS && n < maxOut; ++i)
    {
        const RemotePlayer& p = players[i];
        if(i == myId || !p.active || !p.hasSync || !p.name[0]) continue;
        // separate story worlds stay hidden (a mission partner is shown)
        bool hide = p.status || (p.sync.flags & (SYNC_MISSION | SYNC_CUTSCENE)) || s_bLocalSeparate;
        if(hide && IsPartner(i) && !(p.status & STATUS_CUTSCENE) && !s_bLocalCutscene) hide = false;
        if(hide) continue;
        Vec3 at;
        if(p.ped && p.drawValid) at = p.drawPos;
        else at = MP_LoadVec3((p.sync.flags & SYNC_IN_CAR) ? (const void*)p.sync.carPos : (const void*)p.sync.pos);
        Game::MapMarker& m = out[n++];
        m.x = at.x; m.y = at.y; m.z = at.z; m.rgba = p.color;
        MP_CopyStr(m.name, p.name, sizeof(m.name));
    }
    return n;
}

// ------------------------------------------------------------------------------------------
// 2.18: NPC sync (people + traffic)
//  In every area ONE phone is the NPC host: the lowest player id around that isn't on a
//  mission. It keeps its own people / traffic and sends them (nearest 16 people, 10 cars,
//  5x a second, only to players within 200 m). The other phones there switch their own
//  people / traffic off and show copies of the host's. Damage a player does to a copy goes to
//  the host, who applies it; the result (dead, burning car) comes back to everybody. A player
//  taking a copied car "claims" it: the host removes its car, the player drives the copy.
//  Players on a mission or with a wanted level keep their own world (police must work).
// ------------------------------------------------------------------------------------------
//  2.28: + spawned entities. Cars / people made by a script (CLEO cheat menus, /v, ...) and cars
//  left parked by their driver used to exist on ONE phone only, so every player had a different
//  world (cars appearing / vanishing, people standing in different places). Now every phone sends
//  its own spawned ones (flag *_SPAWNED) and everybody near shows copies, host or not.
#define NPC_MAX_PEDS 32
#define NPC_MAX_CARS 22
struct NpcPedCopy
{
    bool used, spawned; uint8_t owner; uint32_t uid; int ped; int model;
    Vec3 target, vel; float heading; int move, weapon, weaponApplied; bool dead, deadApplied, duck, duckApplied;
    uint64_t seen, lastTask; int lastMoveTask;
};
struct NpcCarCopy
{
    bool used, spawned; uint8_t owner; uint32_t uid; int car, driver; int model, driverModel;
    Vec3 target, vel; float fwd[3], up[3]; int health, lastSetHp; uint8_t col[2]; bool colApplied;
    bool dead, deadApplied, siren, lights; uint64_t seen; float base;
};
static NpcPedCopy s_NpcPed[NPC_MAX_PEDS];
static NpcCarCopy s_NpcCar[NPC_MAX_CARS];
static bool     s_bNpcHost = false, s_bNpcFollow = false;
static int      s_nNpcHostId = -1;          // who I show NPCs from (UI)
static int      s_nNpcFollowers = 0;        // players I send NPCs to (UI)
static uint64_t s_nNpcNoHostSince = 0, s_nNpcLastRole = 0, s_nNpcLastSend = 0, s_nNpcLastClear = 0;
static uint8_t  s_nNpcSeq = 0;

static bool NpcIsHost() { return s_bNpcHost; }

static bool NpcRemotePos(const RemotePlayer& p, Vec3& at)
{
    if(!p.active || !p.hasSync) return false;
    if(p.ped && p.drawValid) at = p.drawPos;
    else at = MP_LoadVec3((p.sync.flags & SYNC_IN_CAR) ? (const void*)p.sync.carPos : (const void*)p.sync.pos);
    return at.x == at.x;
}
static bool NpcRemoteInOwnWorld(const RemotePlayer& p) { return p.status || (p.sync.flags & (SYNC_MISSION | SYNC_CUTSCENE)); }

static void NpcDeletePed(NpcPedCopy& c) { if(c.ped) Game::DeleteChar(c.ped); memset(&c, 0, sizeof(c)); }
static void NpcDeleteCar(NpcCarCopy& c)
{
    if(c.driver && Game::PedPtr(c.driver)) Game::DeleteChar(c.driver);
    if(c.car) Game::DeleteCar(c.car);
    memset(&c, 0, sizeof(c));
}
// 2.35: a player gets into a parked car that is an NPC copy here: their avatar takes THAT car
// (before, a second car was made on the same spot and the copy stayed / blinked)
static int NpcTakeCopyCar(int model, const Vec3& at, float& base)
{
    NpcCarCopy* best = nullptr; float bd = 6.0f * 6.0f;
    for(auto& c : s_NpcCar)
    {
        if(!c.used || !c.car || c.model != model || c.dead || !Game::CarPtr(c.car)) continue;
        if(c.driver && Game::PedPtr(c.driver)) continue;   // somebody drives it: not this one
        Vec3 cp; if(!Game::GetCarPos(c.car, cp)) continue;
        float dx = cp.x - at.x, dy = cp.y - at.y, dz = cp.z - at.z, d2 = dx * dx + dy * dy + dz * dz;
        if(d2 < bd) { bd = d2; best = &c; }
    }
    if(!best) return 0;
    int h = best->car; base = best->base;
    memset(best, 0, sizeof(*best));
    return h;
}
// 2.35: the shared copy of a car a player just got out of: take over the parked avatar car
static int NpcAdoptLeftCar(int model, const float* pos, float& base)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& q = players[i];
        if(!q.active || !q.leftCar || q.leftModel != model || !Game::CarPtr(q.leftCar)) continue;
        if(q.ped && Game::IsCharInCar(q.ped, q.leftCar)) continue;   // still getting out
        Vec3 cp; if(!Game::GetCarPos(q.leftCar, cp)) continue;
        float dx = pos[0] - cp.x, dy = pos[1] - cp.y, dz = pos[2] - cp.z;
        if(dx * dx + dy * dy + dz * dz > 5.0f * 5.0f) continue;
        int h = q.leftCar; base = q.leftBase; q.leftCar = 0; q.leftModel = 0;
        return h;
    }
    return 0;
}
static void NpcClearCopies(bool keepSpawned = false)
{
    for(auto& c : s_NpcPed) if(c.used && !(keepSpawned && c.spawned)) NpcDeletePed(c);
    for(auto& c : s_NpcCar) if(c.used && !(keepSpawned && c.spawned)) NpcDeleteCar(c);
}
static bool NpcHasSpawnedCopies()
{
    for(auto& c : s_NpcPed) if(c.used && c.spawned) return true;
    for(auto& c : s_NpcCar) if(c.used && c.spawned) return true;
    return false;
}
// 2.28: handles this phone must never send as its own spawned entity: player avatars and their cars,
// copies of other phones' NPCs, my own ped and the car I'm in (player sync shows those)
static bool NpcNeverShare(int h)
{
    if(h <= 0) return true;
    int lp = Game::LocalPed();
    if(h == lp) return true;
    if(lp > 0 && Game::IsCharInAnyCar(lp) && Game::GetCarCharIsIn(lp) == h) return true;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& p = players[i];
        if(!p.active) continue;
        if(p.ped == h || p.car == h || p.rideCar == h) return true;
        if(p.ped && Game::CarPtr(h) && Game::IsCharInCar(p.ped, h)) return true;   // an avatar rides in it
    }
    for(auto& c : s_NpcPed) if(c.used && c.ped == h) return true;
    for(auto& c : s_NpcCar) if(c.used && (c.car == h || c.driver == h)) return true;
    if(IsSampActor(h)) return true;   // 3.01: actors are made on every phone, never shared
    if(IsAvatarMade(h)) return true;   // 2.32: any avatar MP ever made (also an old / leftover one)
    return false;
}

// 2.32: a leftover avatar ped (no player owns it any more) is deleted: it would run around as a bot
static void AvatarSweep(uint64_t now)
{
    static uint64_t s_Last = 0; if(now - s_Last < 250) return; s_Last = now;   // 4x a second
    if(!Game::IsReady()) return;
    int k = 0;
    for(int i = 0; i < s_nAvatarPeds; ++i)
    {
        int h = s_AvatarPeds[i];
        if(!Game::PedPtr(h)) continue;   // gone: forget it
        bool owned = false;
        for(int j = 0; j < MP_MAX_PLAYERS && !owned; ++j) if(players[j].ped == h) owned = true;
        if(owned) Game::KeepPuppet(h);   // never released to the game's street-ped AI
        if(!owned && h != Game::LocalPed()) { Game::DeleteChar(h); if(!Game::PedPtr(h)) continue; }
        s_AvatarPeds[k++] = h;   // still there (owned, or the game won't let it go yet: next second)
    }
    s_nAvatarPeds = k;
    k = 0;
    for(int i = 0; i < s_nAvatarCars; ++i)
    {
        int h = s_AvatarCars[i];
        if(!Game::CarPtr(h)) continue;
        for(int j = 0; j < MP_MAX_PLAYERS; ++j) if(players[j].active && players[j].car == h) { Game::KeepMissionCar(h); break; }
        s_AvatarCars[k++] = h;
    }
    s_nAvatarCars = k;
}

// 2.32: is this spawned NPC really a player (an avatar on the sender's phone)? Then it's not shown as
// an NPC: the player's own sync shows them. Near a player's position, or wearing a player's skin nearby.
static bool NpcLooksLikePlayer(const float* pos, int model)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& q = players[i];
        if(!q.active || !q.hasSync) continue;
        Vec3 at = MP_LoadVec3((q.sync.flags & SYNC_IN_CAR) ? (const void*)q.sync.carPos : (const void*)q.sync.pos);
        if(q.ped && q.drawValid) at = q.drawPos;
        float dx = pos[0] - at.x, dy = pos[1] - at.y, dz = pos[2] - at.z, d2 = dx * dx + dy * dy + dz * dz;
        if(d2 < 3.0f * 3.0f) return true;
        if((model == q.skin || model == q.pedSkin) && d2 < 40.0f * 40.0f) return true;
    }
    Vec3 me; int lp = Game::LocalPed();
    if(lp > 0 && Game::GetCharPos(lp, me))
    {
        float dx = pos[0] - me.x, dy = pos[1] - me.y, dz = pos[2] - me.z;
        if(dx * dx + dy * dy + dz * dz < 2.0f * 2.0f) return true;   // a copy of ME on their phone
        if(model == Game::LocalSkinForNet(settings.skin) && dx * dx + dy * dy < 40.0f * 40.0f) return true;
    }
    return false;
}
// 2.34: a "spawned" car on the sender's phone that is really a player's car here (an avatar drives it)
static bool NpcCarNearAvatarCar(const float* pos)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& q = players[i];
        if(!q.active || !q.car) continue;
        Vec3 at; if(!Game::GetCarPos(q.car, at)) continue;
        float dx = pos[0] - at.x, dy = pos[1] - at.y, dz = pos[2] - at.z;
        if(dx * dx + dy * dy + dz * dz < 2.5f * 2.5f) return true;
    }
    return false;
}
static void NpcStop()
{
    if(s_bNpcFollow || s_bNpcHost) { NpcClearCopies(); Game::SetAmbientPopulation(true); }
    else if(Game::IsAmbientPopulationOff()) Game::SetAmbientPopulation(true);
    s_bNpcFollow = s_bNpcHost = false; s_nNpcHostId = -1; s_nNpcFollowers = 0;
}

// who is the NPC host here? (every 500 ms)
static void NpcDecideRole(int ped, uint64_t now)
{
    Vec3 me; bool haveMe = Game::GetCharPos(ped, me);
    bool eligible = settings.syncNpcs && Game::NpcSyncAvailable() && haveMe && myId != MP_INVALID_ID &&
                    !s_bLocalMission && !s_bLocalCutscene && !s_bLocalSeparate;
    // 2.21: wanted stars no longer switch the shared world off (that made everyone's NPCs vanish
    // and come back each time the host got / lost stars); cops are shared like everyone else
    bool wanted = false;
    int area = Game::GetAreaVisible();
    // a lower-id NPC host near me (in the same interior, in the shared world)?
    int host = -1; float best = 1e18f;
    const float r = s_bNpcFollow ? MP_NPC_RANGE_M : MP_NPC_SHOW_M;   // hysteresis
    s_nNpcFollowers = 0;
    for(int i = 0; eligible && i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& p = players[i];
        if(i == myId || !p.active) continue;
        Vec3 at; if(!NpcRemotePos(p, at) || NpcRemoteInOwnWorld(p)) continue;
        float dx = at.x - me.x, dy = at.y - me.y, d = dx * dx + dy * dy;
        if(d > MP_NPC_RANGE_M * MP_NPC_RANGE_M) continue;
        if(p.hasX2 && p.interior != area) continue;
        bool isHost = p.hasX2 && (p.x2flags & X2_NPCHOST);
        if(i < myId && isHost && d < r * r && d < best) { best = d; host = i; }
        if(i > myId) ++s_nNpcFollowers;
    }
    bool follow = eligible && !wanted && host >= 0;
    if(!follow && s_bNpcFollow && eligible && !wanted)
    {
        // the host walked away / stopped: wait 2 s before making my own world again
        if(!s_nNpcNoHostSince) s_nNpcNoHostSince = now;
        if(now - s_nNpcNoHostSince < 2000) follow = true;
    }
    else s_nNpcNoHostSince = 0;
    bool hostNow = eligible && !wanted && !follow;

    if(follow && !s_bNpcFollow)
    {
        Game::SetAmbientPopulation(false);    // my own people / traffic off
        Game::ClearAmbient(me, MP_NPC_RANGE_M, s_nLastLocalCar, true);
        s_nNpcLastClear = now;
    }
    if(!follow && s_bNpcFollow)
    {
        NpcClearCopies(true);                 // 2.28: spawned copies stay (they're not the host's)
        Game::SetAmbientPopulation(true);     // back to my own world
    }
    s_bNpcFollow = follow; s_bNpcHost = hostNow;
    if(follow && host >= 0) s_nNpcHostId = host; else if(!follow) s_nNpcHostId = -1;
    if(!hostNow) s_nNpcFollowers = 0;
}

static inline int16_t NpcQ(float v, float s) { float q = v * s; if(q > 32767.f) q = 32767.f; if(q < -32767.f) q = -32767.f; return (int16_t)lrintf(q); }
static inline int8_t  NpcQ8(float v) { float q = v * 127.f; if(q > 127.f) q = 127.f; if(q < -127.f) q = -127.f; return (int8_t)lrintf(q); }

// 2.35: handle -> was it made on / for this phone (decided the first time it is seen)
struct NpcOwn { int h; bool car, mine; uint64_t seen; };
static NpcOwn s_Own[192]; static int s_nOwn = 0;
static bool NpcIsMine(int h, bool car, const Vec3& pos, const Vec3& me, int myCar, uint64_t now)
{
    if(car && (h == myCar || h == s_nLastLocalCar || Game::IsKeptLocalCar(h))) return true;   // I drove / summoned it
    for(int i = 0; i < s_nOwn; ++i) if(s_Own[i].h == h && s_Own[i].car == car)
    {
        s_Own[i].seen = now;
        return s_Own[i].mine;
    }
    float dx = pos.x - me.x, dy = pos.y - me.y, dz = pos.z - me.z;
    float r = car ? 30.0f : 25.0f;
    bool mine = dx * dx + dy * dy + dz * dz < r * r;
    if(s_nOwn >= 192)
    {   // forget the oldest / gone ones
        int k = 0;
        for(int i = 0; i < s_nOwn; ++i) if(now - s_Own[i].seen < 60000 && (s_Own[i].car ? Game::CarPtr(s_Own[i].h) : Game::PedPtr(s_Own[i].h))) s_Own[k++] = s_Own[i];
        s_nOwn = k; if(s_nOwn >= 192) s_nOwn = 96;
    }
    s_Own[s_nOwn++] = { h, car, mine, now };
    return mine;
}

static void NpcSend(int ped, uint64_t now)
{
    if(now - s_nNpcLastSend < 200) return;
    // 2.28: spawned entities go to everybody near me, host or not (never from my own mission world)
    const bool shareSpawned = settings.syncNpcs && Game::NpcSyncAvailable() && myId != MP_INVALID_ID &&
                              !s_bLocalMission && !s_bLocalCutscene && !s_bLocalSeparate;
    const bool sendAmbient = s_bNpcHost && s_nNpcFollowers > 0;
    if(!sendAmbient && !shareSpawned) return;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    int area = Game::GetAreaVisible();
    if(!sendAmbient)
    {   // anybody in the shared world near me to send to?
        bool any = false;
        for(int i = 0; i < MP_MAX_PLAYERS && !any; ++i)
        {
            const RemotePlayer& rp = players[i];
            if(i == myId || !rp.active) continue;
            Vec3 at; if(!NpcRemotePos(rp, at) || NpcRemoteInOwnWorld(rp)) continue;
            if(rp.hasX2 && rp.interior != area) continue;
            float dx = at.x - me.x, dy = at.y - me.y;
            any = dx * dx + dy * dy < MP_NPC_RANGE_M * MP_NPC_RANGE_M;
        }
        if(!any) return;
    }
    s_nNpcLastSend = now;
    int myCar = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
    // spawned first (they are few and matter most), the host's traffic / people fill the rest
    static Game::NpcPedInfo spPeds[MP_NPC_SPAWNED_PEDS];
    static Game::NpcCarInfo spCars[MP_NPC_SPAWNED_CARS];
    int nsp = 0, nsc = 0;
    if(shareSpawned)
    {
        // 2.35 one world: only what THIS phone made is sent. Every phone runs the same game scripts
        // (parked-car generators, gang / dealer peds, ...), so the same car / person exists on each
        // phone already; when every player sent those too, players riding together saw everything
        // two / three times ("my peds" vs "their peds"). Mine = first seen close to me (a CLEO menu
        // spawns it next to the player) or a car I drove. What streams in far away is the world's.
        static Game::NpcPedInfo lp_[24]; static Game::NpcCarInfo lc_[24];
        int a = Game::ListSpawnedPeds(lp_, 24, me, MP_NPC_SHOW_M, NpcNeverShare);
        for(int i = 0; i < a && nsp < MP_NPC_SPAWNED_PEDS; ++i) if(NpcIsMine(lp_[i].handle, false, lp_[i].pos, me, myCar, now)) spPeds[nsp++] = lp_[i];
        // a follower's own traffic is only the car it took from the host (the rest is cleared): share it too
        int b = Game::ListSpawnedCars(lc_, 24, me, MP_NPC_SHOW_M, myCar, s_bNpcFollow, NpcNeverShare);
        for(int i = 0; i < b && nsc < MP_NPC_SPAWNED_CARS; ++i) if(NpcIsMine(lc_[i].handle, true, lc_[i].pos, me, myCar, now)) spCars[nsc++] = lc_[i];
    }
    // 2.21: sticky pick. Before, the nearest 16 were sent: with a crowd around, people kept
    // dropping in and out of that list, so on the other phones they vanished and came back
    // (flicker). Now everyone already shared stays in the packet while still in range.
    static Game::NpcPedInfo pall[48], peds[MP_NPC_PEDS_MAX];
    static Game::NpcCarInfo call[32], cars[MP_NPC_CARS_MAX];
    static int s_PrevPed[MP_NPC_PEDS_MAX], s_PrevPedN = 0, s_PrevCar[MP_NPC_CARS_MAX], s_PrevCarN = 0;
    int na = sendAmbient ? Game::ListAmbientPeds(pall, 48, me, MP_NPC_SHOW_M, NpcNeverShare) : 0;
    int nca = sendAmbient ? Game::ListAmbientCars(call, 32, me, MP_NPC_SHOW_M, myCar, NpcNeverShare) : 0;
    // 2.34: sticky over the WHOLE packet (spawned + traffic). Only the traffic part was sticky, so when
    // the number of spawned ones changed (a player came near / spawned a car) cars were pushed out of
    // the 10 places, and on the other phones they disappeared for 3 s and came back (flicker).
    int np = 0, nc = 0;
    static bool s_PedSpawnedFlag[MP_NPC_PEDS_MAX], s_CarSpawnedFlag[MP_NPC_CARS_MAX];
    {
        static Game::NpcPedInfo pc[MP_NPC_SPAWNED_PEDS + 48]; static bool pcSp[MP_NPC_SPAWNED_PEDS + 48];
        static Game::NpcCarInfo cc[MP_NPC_SPAWNED_CARS + 32]; static bool ccSp[MP_NPC_SPAWNED_CARS + 32];
        int npc = 0, ncc = 0;
        for(int k = 0; k < nsp; ++k) { pc[npc] = spPeds[k]; pcSp[npc++] = true; }
        for(int k = 0; k < na; ++k)  { pc[npc] = pall[k];   pcSp[npc++] = false; }
        for(int k = 0; k < nsc; ++k) { cc[ncc] = spCars[k]; ccSp[ncc++] = true; }
        for(int k = 0; k < nca; ++k) { cc[ncc] = call[k];   ccSp[ncc++] = false; }
        static bool pSp[MP_NPC_PEDS_MAX], cSp[MP_NPC_CARS_MAX];
        bool pt[MP_NPC_SPAWNED_PEDS + 48] = {}, ct[MP_NPC_SPAWNED_CARS + 32] = {};
        for(int k = 0; k < npc && np < MP_NPC_PEDS_MAX; ++k)            // 1. everyone already in the packet
            for(int j = 0; j < s_PrevPedN; ++j) if(pc[k].handle == s_PrevPed[j]) { pSp[np] = pcSp[k]; peds[np++] = pc[k]; pt[k] = true; break; }
        for(int k = 0; k < npc && np < MP_NPC_PEDS_MAX; ++k)            // 2. new spawned ones
            if(!pt[k] && pcSp[k]) { pSp[np] = true; peds[np++] = pc[k]; pt[k] = true; }
        for(int k = 0; k < npc && np < MP_NPC_PEDS_MAX; ++k)            // 3. new traffic people, nearest first
            if(!pt[k]) { pSp[np] = false; peds[np++] = pc[k]; }
        for(int k = 0; k < ncc && nc < MP_NPC_CARS_MAX; ++k)
            for(int j = 0; j < s_PrevCarN; ++j) if(cc[k].handle == s_PrevCar[j]) { cSp[nc] = ccSp[k]; cars[nc++] = cc[k]; ct[k] = true; break; }
        for(int k = 0; k < ncc && nc < MP_NPC_CARS_MAX; ++k)
            if(!ct[k] && ccSp[k]) { cSp[nc] = true; cars[nc++] = cc[k]; ct[k] = true; }
        for(int k = 0; k < ncc && nc < MP_NPC_CARS_MAX; ++k)
            if(!ct[k]) { cSp[nc] = false; cars[nc++] = cc[k]; }
        s_PrevPedN = np; for(int k = 0; k < np; ++k) s_PrevPed[k] = peds[k].handle;
        s_PrevCarN = nc; for(int k = 0; k < nc; ++k) s_PrevCar[k] = cars[k].handle;
        // the SPAWNED flag per entry (was: the first nsp / nsc entries)
        for(int k = 0; k < np; ++k) s_PedSpawnedFlag[k] = pSp[k];
        for(int k = 0; k < nc; ++k) s_CarSpawnedFlag[k] = cSp[k];
    }
    static PktNpc m; memset(&m, 0, sizeof(m));
    m.h.type = PKT_NPC; m.h.id = myId; m.h.interior = (uint8_t)Game::GetAreaVisible(); m.h.seq = ++s_nNpcSeq;
    float c[3] = { me.x, me.y, me.z }; memcpy(m.h.centre, c, sizeof(c));
    for(int i = 0; i < np; ++i)
    {
        NpcPed e; memset(&e, 0, sizeof(e));
        e.uid = (uint32_t)peds[i].handle; e.model = (uint16_t)peds[i].model;
        float p[3] = { peds[i].pos.x, peds[i].pos.y, peds[i].pos.z }; memcpy(e.pos, p, sizeof(p));
        e.vel[0] = NpcQ(peds[i].vel.x, 100.f); e.vel[1] = NpcQ(peds[i].vel.y, 100.f); e.vel[2] = NpcQ(peds[i].vel.z, 100.f);
        float h = peds[i].heading; while(h < 0) h += 360.f; while(h >= 360.f) h -= 360.f;
        e.heading = (uint8_t)(h * 256.f / 360.f);
        e.move = (uint8_t)peds[i].move; e.health = (uint8_t)peds[i].health; e.weapon = (uint8_t)peds[i].weapon;
        e.flags = (peds[i].dead ? NPCF_DEAD : 0) | (peds[i].duck ? NPCF_DUCK : 0) | (s_PedSpawnedFlag[i] ? NPCF_SPAWNED : 0);
        memcpy(&m.peds[i], &e, sizeof(e));
    }
    for(int i = 0; i < nc; ++i)
    {
        NpcCar e; memset(&e, 0, sizeof(e));
        e.uid = (uint32_t)cars[i].handle; e.model = (uint16_t)cars[i].model; e.driver = (uint16_t)cars[i].driverModel;
        float p[3] = { cars[i].pos.x, cars[i].pos.y, cars[i].pos.z }; memcpy(e.pos, p, sizeof(p));
        for(int k = 0; k < 3; ++k) { e.fwd[k] = NpcQ8(cars[i].fwd[k]); e.up[k] = NpcQ8(cars[i].up[k]); }
        e.vel[0] = NpcQ(cars[i].vel.x, 100.f); e.vel[1] = NpcQ(cars[i].vel.y, 100.f); e.vel[2] = NpcQ(cars[i].vel.z, 100.f);
        e.health = (uint16_t)cars[i].health; e.col[0] = cars[i].col[0]; e.col[1] = cars[i].col[1];
        e.flags = (cars[i].dead ? NCARF_DEAD : 0) | (cars[i].siren ? NCARF_SIREN : 0) | (cars[i].lights ? NCARF_LIGHTS : 0) | (cars[i].engine ? NCARF_ENGINE : 0) | (s_CarSpawnedFlag[i] ? NCARF_SPAWNED : 0);
        memcpy(&m.cars[i], &e, sizeof(e));
    }
    m.h.nPeds = (uint8_t)np; m.h.nCars = (uint8_t)nc;
    static uint8_t out[sizeof(PktNpc)];
    int n = MP_EncodeNpc(m, out);
    client.Send(out, n);
}

// 3.02: my mission's peds / cars for my mission group. Only the phone that started the mission runs
// its script, so before this the others saw the markers but none of the people / cars it made.
static void MissionNpcSend(uint64_t now)
{
    static uint64_t s_Last = 0;
    if(now - s_Last < 150) return;
    s_Last = now;
    int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return;
    int myCar = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;   // my own car goes with my avatar
    static Game::NpcPedInfo lp[MP_NPC_PEDS_MAX]; static Game::NpcCarInfo lc[MP_NPC_CARS_MAX];
    int np = Game::ListSpawnedPeds(lp, MP_NPC_PEDS_MAX, me, MP_NPC_SHOW_M, NpcNeverShare);
    int nc = Game::ListSpawnedCars(lc, MP_NPC_CARS_MAX, me, MP_NPC_SHOW_M, myCar, false, NpcNeverShare);
    static uint8_t s_Seq = 0;
    static PktNpc m; memset(&m, 0, sizeof(m));
    m.h.type = PKT_MISSION_NPC; m.h.id = myId; m.h.interior = (uint8_t)Game::GetAreaVisible(); m.h.seq = ++s_Seq;
    float c[3] = { me.x, me.y, me.z }; memcpy(m.h.centre, c, sizeof(c));
    for(int i = 0; i < np; ++i)
    {
        NpcPed e; memset(&e, 0, sizeof(e));
        e.uid = (uint32_t)lp[i].handle; e.model = (uint16_t)lp[i].model;
        float p[3] = { lp[i].pos.x, lp[i].pos.y, lp[i].pos.z }; memcpy(e.pos, p, sizeof(p));
        e.vel[0] = NpcQ(lp[i].vel.x, 100.f); e.vel[1] = NpcQ(lp[i].vel.y, 100.f); e.vel[2] = NpcQ(lp[i].vel.z, 100.f);
        float h = lp[i].heading; while(h < 0) h += 360.f; while(h >= 360.f) h -= 360.f;
        e.heading = (uint8_t)(h * 256.f / 360.f);
        e.move = (uint8_t)lp[i].move; e.health = (uint8_t)lp[i].health; e.weapon = (uint8_t)lp[i].weapon;
        e.flags = (lp[i].dead ? NPCF_DEAD : 0) | (lp[i].duck ? NPCF_DUCK : 0) | NPCF_SPAWNED;
        memcpy(&m.peds[i], &e, sizeof(e));
    }
    for(int i = 0; i < nc; ++i)
    {
        NpcCar e; memset(&e, 0, sizeof(e));
        e.uid = (uint32_t)lc[i].handle; e.model = (uint16_t)lc[i].model; e.driver = (uint16_t)lc[i].driverModel;
        float p[3] = { lc[i].pos.x, lc[i].pos.y, lc[i].pos.z }; memcpy(e.pos, p, sizeof(p));
        for(int k = 0; k < 3; ++k) { e.fwd[k] = NpcQ8(lc[i].fwd[k]); e.up[k] = NpcQ8(lc[i].up[k]); }
        e.vel[0] = NpcQ(lc[i].vel.x, 100.f); e.vel[1] = NpcQ(lc[i].vel.y, 100.f); e.vel[2] = NpcQ(lc[i].vel.z, 100.f);
        e.health = (uint16_t)lc[i].health; e.col[0] = lc[i].col[0]; e.col[1] = lc[i].col[1];
        e.flags = (lc[i].dead ? NCARF_DEAD : 0) | (lc[i].siren ? NCARF_SIREN : 0) | (lc[i].lights ? NCARF_LIGHTS : 0) | (lc[i].engine ? NCARF_ENGINE : 0) | NCARF_SPAWNED;
        memcpy(&m.cars[i], &e, sizeof(e));
    }
    m.h.nPeds = (uint8_t)np; m.h.nCars = (uint8_t)nc;
    if(!np && !nc) return;
    static uint8_t out[sizeof(PktNpc)];
    client.Send(out, MP_EncodeNpc(m, out));
}

static void NpcReceive(const PktNpc& m, bool mission)
{
    if(m.h.id >= MP_MAX_PLAYERS || m.h.id == myId || !players[m.h.id].active) return;
    // 3.02: mission sync - the peds / cars a group member's mission made (only their phone runs the
    // mission script). Taken from my group only, even when I'm on a mission / cutscene myself.
    if(mission && !IsPartner(m.h.id)) return;
    // 2.21: only my chosen NPC host's people / traffic. With 3+ players two hosts' frames used to
    // mix here and the copies jumped between both worlds (visible / invisible flicker)
    // 2.28: a spawned entity (*_SPAWNED) is taken from ANY player - it exists only on their phone
    const bool fromMyHost = s_bNpcFollow && m.h.id < myId && (s_nNpcHostId < 0 || m.h.id == s_nNpcHostId);
    const bool takeSpawned = mission || (settings.syncNpcs && !s_bLocalMission && !s_bLocalCutscene && !s_bLocalSeparate);
    if(!fromMyHost && !takeSpawned) return;
    if(m.h.interior != (uint8_t)Game::GetAreaVisible()) return;
    Vec3 ownerCar = { 0, 0, 0 }; bool hasOwnerCar = players[m.h.id].car && Game::GetCarPos(players[m.h.id].car, ownerCar);
    int lp = Game::LocalPed(); Vec3 me; if(!Game::GetCharPos(lp, me)) return;
    uint64_t now = MP_NowMs();
    const float show2 = MP_NPC_SHOW_M * MP_NPC_SHOW_M;
    for(int i = 0; i < m.h.nPeds && i < MP_NPC_PEDS_MAX; ++i)
    {
        NpcPed e; memcpy(&e, &m.peds[i], sizeof(e));
        const bool sp = mission || (e.flags & NPCF_SPAWNED) != 0;
        if(sp ? !takeSpawned : !fromMyHost) continue;
        float p[3]; memcpy(p, e.pos, sizeof(p));
        if(!(p[0] == p[0])) continue;
        float dx = p[0] - me.x, dy = p[1] - me.y; if(dx * dx + dy * dy > show2) continue;
        NpcPedCopy* c = nullptr; NpcPedCopy* fr = nullptr;
        for(auto& x : s_NpcPed) { if(x.used && x.owner == m.h.id && x.uid == e.uid) { c = &x; break; } if(!x.used && !fr) fr = &x; }
        // 2.32: a player (avatar on their phone), not an NPC. 2.34: only checked before a copy is made -
        // an NPC that walks / drives past a player used to be deleted and re-made every packet (flicker)
        if(!c && !mission && NpcLooksLikePlayer(p, e.model)) continue;   // 3.02: a mission's people walk with the player (Sweet, Smoke ...)
        // 2.35: the same person already stands there on my phone (same game script on both phones): one, not two
        if(!c && sp && !mission && Game::HasLocalTwin(false, e.model, Vec3{ p[0], p[1], p[2] }, 4.0f, NpcNeverShare)) continue;
        if(c && c->model != e.model) { NpcDeletePed(*c); fr = c; c = nullptr; }
        if(!c) { if(!fr) continue; c = fr; memset(c, 0, sizeof(*c)); c->used = true; c->owner = m.h.id; c->uid = e.uid; c->model = e.model; }
        c->spawned = sp;
        c->target = { p[0], p[1], p[2] };
        c->vel = { e.vel[0] / 100.f, e.vel[1] / 100.f, e.vel[2] / 100.f };
        c->heading = e.heading * 360.f / 256.f; c->move = e.move; c->weapon = e.weapon;
        c->dead = (e.flags & NPCF_DEAD) != 0; c->duck = (e.flags & NPCF_DUCK) != 0;
        c->seen = now;
    }
    for(int i = 0; i < m.h.nCars && i < MP_NPC_CARS_MAX; ++i)
    {
        NpcCar e; memcpy(&e, &m.cars[i], sizeof(e));
        const bool sp = mission || (e.flags & NCARF_SPAWNED) != 0;
        if(sp ? !takeSpawned : !fromMyHost) continue;
        float p[3]; memcpy(p, e.pos, sizeof(p));
        if(!(p[0] == p[0]) || e.model < 400 || e.model > 611) continue;
        float dx = p[0] - me.x, dy = p[1] - me.y; if(dx * dx + dy * dy > show2) continue;
        NpcCarCopy* c = nullptr; NpcCarCopy* fr = nullptr;
        for(auto& x : s_NpcCar) { if(x.used && x.owner == m.h.id && x.uid == e.uid) { c = &x; break; } if(!x.used && !fr) fr = &x; }
        if(sp && hasOwnerCar)
        {   // the owner's avatar car is still here (just got out / I ride in it): that IS this car
            // 2.34: an existing copy is only dropped when it is really the same spot (owner sits in it);
            // before, it stopped updating whenever the owner drove close and blinked out / in
            float ox = p[0] - ownerCar.x, oy = p[1] - ownerCar.y, oz = p[2] - ownerCar.z, o2 = ox * ox + oy * oy + oz * oz;
            if(!c && o2 < 16.0f) continue;
            if(c && o2 < 1.0f) { NpcDeleteCar(*c); continue; }
        }
        if(!c && fr)
        {   // 2.35: the car a player just parked here (their avatar got out of it): the same car
            float base = 0.6f; int h = NpcAdoptLeftCar(e.model, p, base);
            if(h)
            {
                c = fr; memset(c, 0, sizeof(*c)); c->used = true; c->owner = m.h.id; c->uid = e.uid; c->model = e.model;
                c->driverModel = 0; c->car = h; c->base = base; c->lastSetHp = 1000;
            }
        }
        if(!c && NpcCarNearAvatarCar(p)) continue;   // 2.34
        if(!c && sp && !mission && Game::HasLocalTwin(true, e.model, Vec3{ p[0], p[1], p[2] }, 4.0f, NpcNeverShare)) continue;   // 2.35: same parked car here already: a player's car (their avatar drives it here), not an NPC
        if(c && c->model != e.model) { NpcDeleteCar(*c); fr = c; c = nullptr; }
        if(!c) { if(!fr) continue; c = fr; memset(c, 0, sizeof(*c)); c->used = true; c->owner = m.h.id; c->uid = e.uid; c->model = e.model; c->driverModel = e.driver; }
        c->spawned = sp;
        c->target = { p[0], p[1], p[2] };
        c->vel = { e.vel[0] / 100.f, e.vel[1] / 100.f, e.vel[2] / 100.f };
        for(int k = 0; k < 3; ++k) { c->fwd[k] = e.fwd[k] / 127.f; c->up[k] = e.up[k] / 127.f; }
        c->health = e.health; c->col[0] = e.col[0]; c->col[1] = e.col[1];
        c->dead = (e.flags & NCARF_DEAD) != 0; c->siren = (e.flags & NCARF_SIREN) != 0; c->lights = (e.flags & NCARF_LIGHTS) != 0;
        c->seen = now;
    }
}

// host: somebody hurt one of my NPCs / took one of my NPC cars
static void NpcHit(const PktNpcHit& h)
{
    if(h.owner != myId || h.damage == 0) return;
    int handle = (int)h.uid;
    bool car = h.kind == 1;
    // 2.28: not only the host's people / traffic - my spawned ones get hurt too (never avatars / copies)
    if(!s_bNpcHost && NpcNeverShare(handle)) return;
    if(car ? !Game::CarPtr(handle) : !Game::PedPtr(handle)) return;
    if(handle == Game::LocalPed() || (car && handle == s_nLastLocalCar)) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active && (players[i].ped == handle || players[i].car == handle)) return;   // never an avatar
    Game::DamageNpc(handle, car, h.damage > 2000 ? 2000 : h.damage);
}
static void NpcClaim(const PktNpcClaim& c)
{
    if(c.owner != myId) return;
    int handle = (int)c.uid;
    if(handle <= 0 || handle == s_nLastLocalCar) return;
    int lp = Game::LocalPed();
    if(Game::IsCharInAnyCar(lp) && Game::GetCarCharIsIn(lp) == handle) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active && players[i].car == handle) return;
    Game::NpcCarInfo tmp[MP_NPC_CARS_MAX + 6]; Vec3 me; Game::GetCharPos(lp, me);
    int n = Game::ListAmbientCars(tmp, MP_NPC_CARS_MAX + 6, me, MP_NPC_RANGE_M, 0, NpcNeverShare);
    for(int i = 0; i < n; ++i) if(tmp[i].handle == handle) { Game::DeleteCar(handle); return; }   // only my own traffic
    // 2.28: or one of my spawned / parked cars (the player who got in drives the shared copy now)
    n = Game::ListSpawnedCars(tmp, MP_NPC_CARS_MAX + 6, me, MP_NPC_RANGE_M, 0, true, NpcNeverShare);
    for(int i = 0; i < n; ++i) if(tmp[i].handle == handle) { Game::DeleteCar(handle); return; }
}

static void NpcSendHit(uint8_t owner, int kind, uint32_t uid, int dmg)
{
    PktNpcHit h; memset(&h, 0, sizeof(h));
    h.type = PKT_NPC_HIT; h.from = myId; h.owner = owner; h.kind = (uint8_t)kind; h.uid = uid;
    h.damage = (uint16_t)(dmg > 65000 ? 65000 : dmg);
    client.Send(&h, sizeof(h));
}

// follower: create / move / remove the copies
static void NpcUpdateCopies(int lp, uint64_t now)
{
    static uint64_t s_Last = 0;
    float dt = s_Last ? (now - s_Last) / 1000.0f : 0.016f; s_Last = now;
    if(dt > 0.1f) dt = 0.1f;
    int created = 0;
    int area = Game::GetAreaVisible();
    int myCarUsing = Game::GetCarCharIsUsing(lp);
    int myCar = Game::IsCharInAnyCar(lp) ? Game::GetCarCharIsIn(lp) : 0;
    for(auto& c : s_NpcCar)
    {
        if(!c.used) continue;
        if(now - c.seen > 3000 || !players[c.owner].active) { NpcDeleteCar(c); continue; }
        if(c.car && !Game::CarPtr(c.car)) { if(c.driver && Game::PedPtr(c.driver)) Game::DeleteChar(c.driver); c.car = 0; c.driver = 0; }   // the game removed it (2.35: and its driver)
        // I take it: it's mine now (the host removes its own)
        // 2.34: "using" alone is not enough (the game keeps the last car there): close to it too
        bool getIn = false;
        if(c.car && Game::IsCharInAnyCar(lp) && Game::GetCarCharIsIn(lp) == c.car) getIn = true;
        else if(c.car && myCarUsing == c.car)
        {
            Vec3 mp, cp; getIn = Game::GetCharPos(lp, mp) && Game::GetCarPos(c.car, cp) && Dist2D(mp, cp) < 7.0f;
        }
        if(getIn)
        {
            PktNpcClaim q; memset(&q, 0, sizeof(q)); q.type = PKT_NPC_CLAIM; q.from = myId; q.owner = c.owner; q.uid = c.uid;
            client.Send(&q, sizeof(q));
            Game::ReleaseNpc(c.car, true);
            // 2.35: the copy's driver is deleted, not let go: released it became one of MY street
            // people and my phone shared it back to everybody (a second one of the same person)
            if(c.driver && Game::PedPtr(c.driver) && !(Game::IsCharInAnyCar(lp) && Game::GetCarCharIsIn(lp) == c.car && Game::IsCharInCar(c.driver, c.car))) Game::DeleteChar(c.driver);
            else if(c.driver) Game::ReleaseNpc(c.driver, false);
            memset(&c, 0, sizeof(c));
            continue;
        }
        float age = (now - c.seen) / 1000.0f; if(age > 0.5f) age = 0.5f;
        Vec3 want = { c.target.x + c.vel.x * age, c.target.y + c.vel.y * age, c.target.z + c.vel.z * age };
        if(!c.car)
        {
            if(created >= 1) continue;
            bool ok = Game::ModelReady(c.model);
            if(c.driverModel) ok = Game::ModelReady(c.driverModel) && ok;
            if(!ok) continue;
            int drv = 0;
            c.car = Game::CreateNpcCar(c.model, c.driverModel, want, 0.0f, &drv);
            c.driver = drv; ++created;
            if(!c.car) continue;
            Game::SetCarArea(c.car, area);
            Vec3 got; Game::GetCarPos(c.car, got);
            float off = got.z - want.z; c.base = (off > -0.5f && off < 2.5f) ? off : 0.0f;   // as for players' cars
            Game::SetCarRotation(c.car, c.fwd, c.up);
            c.colApplied = false; c.deadApplied = false; c.lastSetHp = 1000;
        }
        {   // 2.19.1 colour sync: same colours as on the host's phone, re-applied if the game changes them
            uint8_t real[4];
            if(c.colApplied && Game::GetCarColours(c.car, real) && (real[0] != c.col[0] || real[1] != c.col[1])) c.colApplied = false;
        }
        if(!c.colApplied) { Game::SetCarColours(c.car, c.col[0], c.col[1]); c.colApplied = true; }
        if(c.dead)
        {
            if(!c.deadApplied) { Game::ExplodeCar(c.car); c.deadApplied = true; }
            continue;
        }
        if(Game::IsCarDead(c.car)) { NpcDeleteCar(c); continue; }   // blew up here only: rebuilt next packet
        // damage I (or anybody here) did -> to the host; health follows the host
        int hp = Game::GetCarHealth(c.car);
        // only what I did counts (copies bumping into each other here don't hurt the host's cars)
        if(hp < c.lastSetHp - 3 && Game::NpcDamagedByMe(c.car, true, lp, myCar)) NpcSendHit(c.owner, 1, c.uid, c.lastSetHp - hp);
        int ownerHp = c.health > 1000 ? 1000 : c.health;
        if(hp != ownerHp) { Game::SetCarHealth(c.car, ownerHp); }
        c.lastSetHp = ownerHp;
        if(c.driver && Game::PedPtr(c.driver) && Game::GetCharHealth(c.driver) < 1000) Game::SetCharHealth(c.driver, 1000);
        // 2.35 "weird exit": the copy's driver had its own brain and climbed out (bumped, stuck,
        // a gun pointed at it) and walked away as a second person. It is a puppet now and stays in.
        if(c.driver && Game::PedPtr(c.driver) && !Game::IsCharDead(c.driver) && !Game::IsCharInCar(c.driver, c.car) && myCarUsing != c.car)
        {
            if(!Game::IsCharInAnyCar(c.driver)) Game::WarpIntoCar(c.driver, c.car);
            else { Game::DeleteChar(c.driver); c.driver = 0; }
        }
        // motion: same smoothing as players' cars
        Vec3 cur; if(!Game::GetCarPos(c.car, cur)) continue;
        Vec3 d = { want.x - cur.x, want.y - cur.y, want.z - cur.z };
        float dist = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);
        if(dist > 10.0f) { Game::SetCarPos(c.car, want, c.base); Game::SetCarRotation(c.car, c.fwd, c.up); }
        else
        {
            float k = dt * 5.0f; if(k > 1.0f) k = 1.0f;
            Vec3 step = { d.x * k, d.y * k, d.z * k };
            Game::NudgeEntity(c.car, true, step);
            Game::SetCarRotation(c.car, c.fwd, c.up);
        }
        Game::SetMoveSpeed(c.car, true, c.vel);
    }
    for(auto& c : s_NpcPed)
    {
        if(!c.used) continue;
        if(now - c.seen > 3000 || !players[c.owner].active) { NpcDeletePed(c); continue; }
        if(c.ped && !Game::PedPtr(c.ped)) c.ped = 0;
        float age = (now - c.seen) / 1000.0f; if(age > 0.5f) age = 0.5f;
        Vec3 want = { c.target.x + c.vel.x * age, c.target.y + c.vel.y * age, c.target.z + c.vel.z * age };
        if(!c.ped)
        {
            if(created >= 2 || c.dead) continue;   // a dead one I never saw alive: not worth a body
            if(!Game::ModelReady(c.model)) continue;
            c.ped = Game::CreateNpcPed(c.model, want, c.heading); ++created;
            if(!c.ped) continue;
            Game::SetCharArea(c.ped, area);
            c.weaponApplied = 0; c.deadApplied = c.duckApplied = false; c.lastMoveTask = -1;
        }
        if(c.dead)
        {
            if(!c.deadApplied) { Game::SetCharHealth(c.ped, 0); Game::TaskDie(c.ped); c.deadApplied = true; }
            continue;
        }
        if(Game::IsCharDead(c.ped)) { NpcDeletePed(c); c.used = false; continue; }   // died here only
        int hp = Game::GetCharHealth(c.ped);
        if(hp < 998)
        {
            if(Game::NpcDamagedByMe(c.ped, false, lp, myCar)) NpcSendHit(c.owner, 0, c.uid, 1000 - hp);
            Game::SetCharHealth(c.ped, 1000);
        }
        if(c.weapon != c.weaponApplied) { if(c.weapon > 0 && c.weapon < 47) Game::GiveAndSelectWeapon(c.ped, c.weapon); c.weaponApplied = c.weapon; }
        if(c.duck != c.duckApplied && now - c.lastTask > 300) { Game::TaskDuck(c.ped, c.duck); c.duckApplied = c.duck; }
        Vec3 cur; if(!Game::GetCharPos(c.ped, cur)) continue;
        Vec3 d = { want.x - cur.x, want.y - cur.y, 0.0f };
        float dist = sqrtf(d.x * d.x + d.y * d.y);
        float spd = sqrtf(c.vel.x * c.vel.x + c.vel.y * c.vel.y);
        if(dist > 6.0f) { Game::SetCharPos(c.ped, want, 1.0f); Game::SetCharHeading(c.ped, c.heading); continue; }
        // legs: walk / run / sprint like the original (move state), standing still when it stands
        int ms = spd < 0.25f && c.move < 4 ? 0 : (c.move >= 7 ? 7 : (c.move >= 5 || spd > 3.2f ? 6 : 4));
        if(ms)
        {
            if(ms != c.lastMoveTask || now - c.lastTask > 400)
            {
                Vec3 goal = { want.x + c.vel.x * 0.6f, want.y + c.vel.y * 0.6f, want.z };
                Game::TaskGoTo(c.ped, goal, ms);
                c.lastTask = now; c.lastMoveTask = ms;
            }
        }
        else if(c.lastMoveTask != 0) { Game::TaskStandStill(c.ped); Game::SetCharHeading(c.ped, c.heading); c.lastMoveTask = 0; c.lastTask = now; }
        if(dist > 0.3f)
        {
            float k = dt * 3.0f; if(k > 1.0f) k = 1.0f;
            Vec3 step = { d.x * k, d.y * k, 0.0f };
            Game::NudgeEntity(c.ped, false, step);
        }
    }
}

static void NpcProcess(int ped, uint64_t now)
{
    if(now - s_nNpcLastRole >= 500) { s_nNpcLastRole = now; NpcDecideRole(ped, now); }
    NpcSend(ped, now);   // 2.28: host traffic and / or my spawned entities
    if(!s_bNpcFollow && NpcHasSpawnedCopies()) NpcUpdateCopies(ped, now);   // 2.28: other players' spawned ones
    if(s_bNpcFollow)
    {
        Game::SetAmbientPopulation(false);   // scripts may have turned the density back up
        if(now - s_nNpcLastClear >= 1000)
        {
            s_nNpcLastClear = now;
            Vec3 me; if(Game::GetCharPos(ped, me)) Game::ClearAmbient(me, MP_NPC_RANGE_M, s_nLastLocalCar);   // never the car I took
        }
        NpcUpdateCopies(ped, now);
    }
}

static void NpcStatusLine()
{
    if(client.GetState() != NET_CONNECTED || !settings.syncNpcs) return;
    if(!Game::NpcSyncAvailable()) { ImGui::TextDisabled("NPC sync isn't available on this game version."); return; }
    if(s_bNpcFollow && s_nNpcHostId >= 0 && players[s_nNpcHostId].active)
        ImGui::TextDisabled("NPCs around you come from %s.", players[s_nNpcHostId].name);
    else if(s_bNpcHost && s_nNpcFollowers > 0)
        ImGui::TextDisabled("You are the NPC host for %d player%s near you.", s_nNpcFollowers, s_nNpcFollowers == 1 ? "" : "s");
    else if(s_bLocalSeparate || s_bLocalMission) ImGui::TextDisabled("On a mission: your own NPCs.");
    else if(Game::GetWantedLevel() > 0) ImGui::TextDisabled("Wanted: your own NPCs and police.");
    else ImGui::TextDisabled("Nobody near you: your own NPCs.");
}

// 2.19.6: what is really synced right now (so it's visible when something doesn't work)
static void SyncStatusLines()
{
    if(client.GetState() != NET_CONNECTED) { ImGui::TextDisabled("Sync status shows here while you are on a server."); return; }
    int near = 0, mirrored = 0, full = 0, special = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const RemotePlayer& p = players[i];
        if(!p.active || i == myId || !p.ped) continue;
        ++near;
        if(p.mirror) ++mirrored;
        if(p.nAF && p.aframes[p.nAF - 1].full) ++full;
        if(Game::IsSpecialSkin(p.pedSkin)) ++special;
    }
    if(!settings.syncAnims) ImGui::TextDisabled("Animations: off (switch on above).");
    else if(!Game::AnimSyncAvailable()) ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Animations: not available on this game version.");
    else if(!near) ImGui::TextDisabled("Animations: on (no player near you).");
    else ImGui::TextColored(ImVec4(0.35f, 0.95f, 0.45f, 1), "Animations: %d of %d players near you send their full animations.", full, near);
    if(near && full < near && settings.syncAnims) ImGui::TextDisabled("Players on an older MP version only send walk / run / basic moves.");
    if(special) ImGui::TextDisabled("%d player%s shown as story characters.", special, special == 1 ? "" : "s");
    (void)mirrored;
}

static bool JoinHudOff();   // 2.35 (mp_join.inc)
void MP::Process()
{
    uint64_t now = MP_NowMs();
    {   // 2.18.3: remember where the MP MENU | CHAT buttons were dropped
        float bx, by;
        if(FloatMenu::GetBarPos(&bx, &by))
        {
            ConfigEntry* e = ::cfg->Bind("ButtonsX", 0.5f, "MP"); e->SetFloat(bx); delete e;
            e = ::cfg->Bind("ButtonsY", 0.07f, "MP"); e->SetFloat(by); delete e;
            ::cfg->Save();
        }
    }
    // pause menu = paused for the others; they keep playing, we keep our seat (heartbeat thread)
    const bool paused = Game::IsPaused();
    client.SetGamePaused(paused);
    client.Poll(&s_Events);
    // 2.17: kill feed lines no longer expire - they stay until pushed out by newer kills.
    // 2.35: ...but they belong to that server: left / lost the server = feed cleared
    if(client.GetState() == NET_DISCONNECTED && !kills.empty()) kills.clear();
    ProcessLan(now);
    ProcessFeed(now);
    UpdateLocalStory(now);
    Progress::Process(now, settings.fullProgressDefault, client.GetState() == NET_CONNECTED && MP::IsOnDefault(),
                      client.GetState() == NET_CONNECTED && !MP::IsOnDefault(),
                      Game::IsReady() && !paused && !s_bLocalSeparate && s_nFeedStep < 0);
    ApplyWorld(now);
    CleoBlock::Process(MP::CheatsBlocked());   // 2.35
    ExpireKillPredictions(now);                // 2.37
    {   // 2.37 / 3.00: CheatMenu installed -> allowed only where the server says so (PERM_CHEAT_MENU)
        static uint64_t s_nCmT = 0, s_nConnT = 0;
        if(client.GetState() != NET_CONNECTED) s_nConnT = 0;
        else if(!s_nConnT) s_nConnT = now;
        ModScan::SetAllowed(MP::CheatMenuAllowed());   // its button / window come back only there
        // connected: decided as soon as the server's permissions arrive (right after the welcome; an
        // old server never sends the bit -> not allowed after 4 s). Allowed = you just play, no
        // message. Not allowed = leave at once + "This server doesn't allow cheat menus".
        // A Cheat Menu loaded later is caught by the rescan every 2 s.
        if(client.GetState() == NET_CONNECTED && (s_bPermsSeen || now - s_nConnT > 4000) && !MP::CheatMenuAllowed())
        {
            bool cm = ModScan::CheatMenu();
            if(!cm && now - s_nCmT > 2000) { s_nCmT = now; cm = ModScan::Recheck(); }
            if(cm) { char nm[64]; MP_CopyStr(nm, client.GetServerName(), sizeof(nm)); LeaveServer(nullptr); CheatMenuBlocks(nm, true); }
        }
    }

    if(!Game::IsReady()) return;
    if(client.GetState() == NET_CONNECTED)
    {   // 2.37: Tug Stairs & other trailers can't be driven in MP - sitting in one made it fly
        // around and one-hit players. Put the player next to it and stop it.
        static uint64_t s_nTrailerT = 0;
        int ped = Game::LocalPed();
        if(ped && now - s_nTrailerT > 250 && Game::IsCharInAnyCar(ped))
        {
            s_nTrailerT = now;
            int car = Game::GetCarCharIsUsing(ped);
            if(car > 0 && MP_IsTrailerModel(Game::GetCarModel(car)))
            {
                Vec3 cp; Game::GetCarPos(car, cp);
                Game::SetMoveSpeed(car, true, Vec3{ 0, 0, 0 });
                cp.x += 2.5f; cp.z += 0.5f;
                Game::WarpOutOfCar(ped, cp);
                SysMsg(COL_YELLOW, "Trailers like the Tug Stairs can't be driven in MP (they fly off). Use the Tug to pull it.");
            }
        }
    }
    {   // 2.35: MP Launcher open = the game's HUD (radar, weapon, buttons) is hidden behind it and
        // gets no touches; it comes back when the launcher closes
        static bool s_bLaunchHud = false; static uint64_t s_nLaunchHudT = 0;
        bool modal = FloatMenu::IsModal() && !paused;
        if(modal && (!s_bLaunchHud || now - s_nLaunchHudT > 500)) { Game::ShowGameHud(false); s_bLaunchHud = true; s_nLaunchHudT = now; }
        else if(!modal && s_bLaunchHud) { s_bLaunchHud = false; if(!JoinHudOff()) Game::ShowGameHud(true); }
    }
    int ped = Game::LocalPed();

    if(s_bWantTeleport)
    {
        s_bWantTeleport = false;
        Game::Teleport(ped, kGroveStreet, kGroveHeading);
    }
    JoinFlowProcess(ped, now);   // 2.19: join screen, connection camera, spawn picker
    BcwProcess(ped, now);        // 3.04: Brief Case War
    ProcessGates(ped, now);      // 3.04.2: Area 69 gates
    ProcessClothes(now);         // 3.10: clothing sync
    ProcessVoice();              // 3.11: voice chat
    ProcessCheats(ped, now);     // 2.19: chat-command cheats
    ProcessGames(ped, now);      // 2.21: races, deathmatch, jobs, open world
    ProcessSamp(ped, now);       // 3.01: map icons, actors, turf HUD, class skin, team
    UpdatePassengerCandidate(ped, now);   // 2.19.1: Passenger button

    // cache local data for the own name tag
    myDrawValid = Game::GetCharPos(ped, myDrawPos);
    myDrawPos.z += 1.0f;
    myHealth = Game::GetCharHealth(ped);
    myArmour = Game::GetCharArmour(ped);

    if(client.GetState() != NET_CONNECTED) { NpcStop(); return; }

    if(paused) { s_fPendingDamage = 0; return; }   // no sync / damage while paused
    if(s_bLocalSeparate) s_fPendingDamage = 0;      // own story world: no player damage
    ProcessLocalCombat(ped);
    ProcessEnv(ped);   // 2.13: explosions / fires / shared weapon pickups
    ProcessPassengerEntry(ped, now);   // 2.15: "enter car" on a player's car = ride along
    if(Game::IsCharInAnyCar(ped)) { int c = Game::GetCarCharIsIn(ped); if(c > 0 && Game::GetDriverOfCar(c) == ped) s_nLastLocalCar = c; }
    // 2.11: sync rate scales with the player count so a full 100-player server (and the internet
    // relay that carries it) isn't flooded: 20 Hz up to 16 players, 15 Hz up to 40, 10 Hz above.
    int online = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active) ++online;
    uint64_t syncMs = online <= 16 ? MP_SYNC_RATE_MS : (online <= 40 ? 66 : 100);
    // 2.15: a congested uplink or a high ping -> send less instead of queuing (queued = lag)
    {
        int cg = Online::CongestionMs(), png = client.GetPing();
        if(cg > 0 || png > 600) { if(syncMs < 100) syncMs = 100; }
        if(cg > 60 || png > 1200) { if(syncMs < 150) syncMs = 150; }
    }
    if(now - s_nLastSync >= syncMs) SendLocalSync(ped, now);
    FlushShots();             // 2.17: this frame's bullets / rockets
    SendVehState(ped, now);   // 2.17: my car's look and state
    AvatarSweep(now);         // 2.32: leftover avatars never become bots
    ProcessMissionSync(now);  // 2.17: mission markers for / from my partner
    UpdateStreaming();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { UpdateRemote(i, ped, now); if(players[i].active && i != myId) UpdateTagOcclusion(i, now); }
    SendPlayerInfo(now);      // 2.19: ping + score for the player list
    NpcProcess(ped, now);     // 2.18: people + traffic
}

// ------------------------------------------------------------------------------------------
// ------------------------------------------------------------------------------------------
// Connect / host
//  - Default server ("MP LAN Server", port 1922): the big Connect button. Always available:
//    joins the copy on your Wi-Fi/hotspot, or hosts it when nobody has it yet.
//  - Custom servers (up to 99, Servers tab): each has its own Join button.
bool MP::HasNetwork()  { return s_bHasNetwork; }
bool MP::IsSearching() { return s_nJob != JOB_NONE; }
// 2.21: background jobs (LAN scan, the host's duplicate check every 8 s) never block the buttons:
// a request waits for them instead of showing "Please wait..."
static bool IsBackgroundJob(int j) { return j == JOB_SCAN || j == JOB_DUPCHECK; }
static bool IsBusy()    { int j = s_nJob; return (j != JOB_NONE && !IsBackgroundJob(j)) || s_Queued.job != JOB_NONE; }

static bool IsLocalHost(const char* h) { return !h || !*h || !strcmp(h, "127.0.0.1") || !strcmp(h, "localhost"); }
bool MP::IsDefaultServer(const char* name, int port) { return port == MP_DEFAULT_PORT && name && !strcasecmp(name, MP_DEFAULT_SERVER_NAME); }
// 2.16: the host's permission switches (the default server always allows pairs)
static void ApplyServerPerms()
{
    if(!s_Server.IsRunning()) return;
    bool def = IsDefaultServer(s_Server.GetName(), s_Server.GetPort());
    // 2.37: on the default server only [ADMIN] SENJI switches cheats off (from his phone, PKT_ADMIN_CMD) -
    // the phone that happens to host it is not the administrator any more
    s_Server.SetPerms(((def || settings.allowMissionPair) ? PERM_MISSION_PAIR : 0) | (!def && settings.disableCheats ? PERM_NO_CHEATS : 0)
                      | (!def && s_bRunCheatMenu ? PERM_CHEAT_MENU : 0));   // 3.00 / 3.10: the server's own permission
    s_Server.SetModes(def ? (uint16_t)GM_ALL : s_nRunModes);   // 3.03: fixed when the server was created
}

bool MP::CheatsBlocked()
{
    if(s_Server.IsRunning() && settings.disableCheats && !MP::IsHostingDefault()) return true;   // my own server: at once
    return client.GetState() == NET_CONNECTED && (s_nServerPerms & PERM_NO_CHEATS);
}
// 3.00: CheatMenu (libCheatMenu.so / libCheatMenu64.so) is fine on a custom server whose host turned
// "Cheat Menu" on (Add server > permissions) - never on the default server, never with "Disable cheats"
bool MP::CheatMenuAllowed()
{
    if(client.GetState() != NET_CONNECTED || MP::IsOnDefault()) return false;
    return (s_nServerPerms & PERM_CHEAT_MENU) && !(s_nServerPerms & PERM_NO_CHEATS);
}
bool MP::IsLocalAdmin() { return MP_IsReservedName(settings.name) && HaveAdminKey(); }
bool MP::IsDefaultAdmin()
{
    return client.GetState() == NET_CONNECTED && MP::IsOnDefault() && myId < MP_MAX_PLAYERS && players[myId].active && UI::IsAdminName(players[myId].name);
}
static void SendAdminCmd(uint8_t cmd, int target, uint8_t value, const char* reason)   // 2.37
{
    PktAdminCmd c; memset(&c, 0, sizeof(c));
    c.type = PKT_ADMIN_CMD; c.cmd = cmd; c.target = (uint8_t)(target < 0 ? MP_INVALID_ID : target); c.value = value;
    MP_CopyStr(c.reason, reason ? reason : "", sizeof(c.reason));
    client.Send(&c, sizeof(c));
}
bool MP::IsHostingDefault() { return s_Server.IsRunning() && IsDefaultServer(s_Server.GetName(), s_Server.GetPort()); }
bool MP::IsOnDefault()
{
    return client.GetState() != NET_DISCONNECTED && client.GetPort() == MP_DEFAULT_PORT &&
           (client.GetServerName()[0] ? !strcasecmp(client.GetServerName(), MP_DEFAULT_SERVER_NAME) : s_bConnectingDefault);
}
// 3.02: gamemodes - the default server has all of them, a custom server only what its host turned on
bool MP::ModeOn(uint16_t gm)
{
    if(client.GetState() != NET_CONNECTED) return false;
    if(MP::IsOnDefault()) return true;
    return (s_nServerModes & gm) != 0;
}

static void OfflineMsg()
{
    SysMsg(COL_RED, "You are offline. Turn on Wi-Fi or mobile data.");
}

static char s_szJoinPw[MP_PASSWORD_LEN + 1] = "";   // 2.19: password for the server we join / host now
static const ImVec4 kGreenTxt(0.35f, 0.95f, 0.45f, 1), kGreyTxt(0.6f, 0.6f, 0.6f, 1), kRedTxt(1, 0.4f, 0.4f, 1), kYellowTxt(1, 0.85f, 0.2f, 1);
#include "mp_join.inc"   // 2.19: join overlay, connection camera, spawn picker
static bool JoinHudOff() { return s_bJoinHudOff; }
#include "mp_cmds.inc"   // 2.19: chat commands, cheats, player list, chat box, spawn buttons
#include "mp_samp.inc"   // 3.01: SA-MP dialogs, textdraws, race / derby state of the others
#include "mp_games.inc"  // 2.21: /spawn, /w, races, deathmatch, jobs, teleport events, open world (3.01: derbies, live race positions)
#include "mp_world.inc"  // 3.01: 3D labels, map icons, actors, gang zones + turf wars, class selection
#include "mp_bcw.inc"    // 3.04: Brief Case War
#include "mp_gates.inc"  // 3.04.2: gates that never open (Area 69): fade out, through, fade in

// 2.37: CheatMenu (libCheatMenu.so / libCheatMenu64.so) is not allowed on any server - the default
// server and every custom server. Detected with aml->HasMod() in ON_ALL_MODS_LOAD (+ a re-check of
// the loaded libs before each join). A detected player is refused before anything connects.
struct CheatGateInfo { bool show; char server[64]; };
static CheatGateInfo s_CheatGate = {};
// 3.00: before connecting only the default server is refused at once; a custom server tells us in
// its permissions (PKT_PERMS, right after the welcome) whether Cheat Menu is allowed - see Process().
static bool CheatMenuBlocks(const char* serverName, bool isDefault)
{
    if(!(isDefault || MP_IsBcwServerName(serverName)) || !ModScan::Recheck()) return false;   // 3.04: Brief Case War refuses cheat menus too
    JoinOverlayCancel();
    StopFeed();
    s_CheatGate.show = true;
    MP_CopyStr(s_CheatGate.server, serverName && serverName[0] ? serverName : "this server", sizeof(s_CheatGate.server));
    SysMsg(COL_RED, "Sorry, this server doesn't allow cheat menus. %s is loaded in your game: remove it from your mods folder and restart the game.", ModScan::What());
    return true;
}

static void DoConnect(const char* host, int port)
{
    if(CheatMenuBlocks(s_szJobName[0] ? s_szJobName : host, s_bConnectingDefault)) return;   // 2.37: last line of defence (takeover, auto-rejoin...)
    if(MP_IsReservedName(settings.name))   // 2.37: SENJI without admin.txt is blocked before it even asks the server
    {
        if(!HaveAdminKey()) LoadAdminFile();
        if(!HaveAdminKey()) { JoinOverlayCancel(); SenjiBlockedMsg(); return; }
    }
    MP_CopyStr(s_szLastConnHost, host, sizeof(s_szLastConnHost)); s_nLastConnPort = port;   // 3.11.3: retry with the other admin key
    // internet server: packets go through the online relay (local proxy on 127.77.x.y:port)
    uint16_t via = 0;
    if(Online::IsRelayIp(host))
    {
        if(!Online::OpenProxy(host, (uint16_t)port, &via))
        {
            SysMsg(COL_RED, "That online server is gone. Tap Connect / Join again.");
            return;
        }
        // 3.11: a 24/7 server that answers over direct UDP: connect straight to it (real low ping),
        // the relay stays open as the fallback if the direct join gets no answer
        uint32_t dip = 0; int drtt = -1;
        if(Online::DirectAddr(host, (uint16_t)port, &dip, &drtt))
        {
            client.SetDirect(dip, via);
            MP_CopyStr(s_szDirectRelay, host, sizeof(s_szDirectRelay));
            logger->Info("Direct UDP to the 24/7 server (probe %d ms)", drtt);
        }
        else s_szDirectRelay[0] = 0;
    }
    else Online::CloseProxy();
    client.SetJoinPassword(s_szJoinPw);   // 2.19
    if(!client.Connect(host, (uint16_t)port, settings.name, (uint16_t)(settings.realSkin && Game::IsReady() ? Game::LocalSkinForNet(settings.skin) : settings.skin), settings.nameColor, via))
    {
        JoinOverlayCancel();
        SysMsg(COL_RED, "Invalid server address."); (void)host;
        s_bQuietRejoin = false;
        return;
    }
    if(!s_bQuietRejoin) StartFeed();   // 2.27
}

static void LeaveServer(const char* msg)
{
    if(client.GetState() == NET_DISCONNECTED) return;
    client.Disconnect(true);
    ClearAllPlayers();
    myId = MP_INVALID_ID;
    s_bTakeoverPending = false;
    s_bConnectingDefault = false;
    StopFeed();
    if(s_bWorldSynced) s_bReleaseWeather = true;   // back to your own weather (done in Process, not from the menu)
    s_bWorldSynced = false;
    if(msg) SysMsg(COL_YELLOW, "%s", msg);
}

static bool StartJob(int job, bool autoConnect, const char* name, int port, const char* host = nullptr)
{
    // a background scan is running: run this job right after it
    if(!IsBackgroundJob(job) && IsBackgroundJob(s_nJob) && (s_Queued.job == JOB_NONE || IsBackgroundJob(s_Queued.job)))
    {
        s_Queued.job = job; s_Queued.autoConnect = autoConnect; s_Queued.port = port;
        MP_CopyStr(s_Queued.name, name ? name : "", sizeof(s_Queued.name));
        MP_CopyStr(s_Queued.host, host ? host : "", sizeof(s_Queued.host));
        return true;
    }
    int expected = JOB_NONE;
    if(!s_nJob.compare_exchange_strong(expected, job)) return false;
    s_bJobAutoConnect = autoConnect;
    MP_CopyStr(s_szJobName, name ? name : "", sizeof(s_szJobName));
    s_nJobPort = port;
    MP_CopyStr(s_szJobHost, host ? host : "", sizeof(s_szJobHost));
    s_bJobDone = false;

    // search the target port, the default server port and your own server port
    std::vector<uint16_t> ports;
    auto addPort = [&](int p) { if(p > 0 && p < 65536) { for(auto q : ports) if(q == p) return; ports.push_back((uint16_t)p); } };
    addPort(port); addPort(MP_DEFAULT_PORT); addPort(settings.port); addPort(MP_DEFAULT_PORT + 1);   // 2.19.6: + the usual hosted port
    for(int i = 0; i < ServerList::Count() && ports.size() < 8; ++i)   // saved custom servers too
        if(ServerEntry* e = ServerList::Get(i)) addPort(e->port);
    std::string h = (host && *host && strcmp(host, "127.0.0.1") && strcmp(host, "localhost")) ? host : "";
    uint16_t tport = (uint16_t)port;
    std::string want = (job == JOB_JOIN || job == JOB_CONNECT) && name ? name : "";
    std::thread([ports, h, tport, want]()
    {
        // custom server address (name or IP) -> ask it directly as well
        std::vector<LAN::Target> extra;
        char resolved[16] = "";
        uint32_t ip = 0;
        if(!h.empty() && LAN::Resolve(h.c_str(), &ip))
        {
            extra.push_back({ ip, tport });
            in_addr a; a.s_addr = htonl(ip); inet_ntop(AF_INET, &a, resolved, sizeof(resolved));
        }
        std::vector<LanServer> r = LAN::Discover(ports, 1500, false, extra);
        // internet lobby: servers hosted by phones on any network (mobile data, other Wi-Fi).
        // Waits only right after the game starts, until every live host had time to announce itself.
        if(Online::WaitReady(10000))
            for(auto& sv : Online::Snapshot()) r.push_back(sv);
        // 2.21: joining a server that is online but whose listing did not reach us yet (it repeats
        // every 3 s): wait for it a little instead of failing / starting a second copy of it
        if(!want.empty() && Online::IsConnected())
        {
            auto has = [&](const std::vector<LanServer>& v) { for(auto& sv : v) if(sv.port == tport && !strcasecmp(sv.name, want.c_str())) return true; return false; };
            for(int w = 0; w < 9 && !has(r); ++w)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                for(auto& sv : Online::Snapshot()) if(sv.port == tport && !strcasecmp(sv.name, want.c_str())) r.push_back(sv);
            }
        }
        { std::lock_guard<std::mutex> lk(s_LanMutex); s_JobResult = r; s_Nearby = r; ++s_nNearbyVersion; MP_CopyStr(s_szJobResolved, resolved, sizeof(s_szJobResolved)); }
        s_bJobDone = true;
    }).detach();
    return true;
}

// 3.03: every custom server keeps the gamemodes it was CREATED with. The Add server > Gamemodes
// switches are only the choice for the next new server: they never change a server that already
// exists (one you joined, one this phone auto-hosts / takes over, or your own from before).
// Saved per server (name + port) in [MPServerModes]; learnt from the server when you are on it.
static bool     s_bCreatingNew = false;   // the next StartLocalServer is "Add server > Host" (create)
static bool     s_bHostJobCreate = false; // the running JOB_HOST came from the Host button (not the restore on start)
static void SrvModesKey(const char* name, int port, char* out, int n)
{
    uint32_t h = 2166136261u;
    for(const char* c = name; c && *c; ++c) { h ^= (uint8_t)tolower((unsigned char)*c); h *= 16777619u; }
    h ^= (uint32_t)port; h *= 16777619u;
    snprintf(out, n, "GM_%08X", h);
}
static int SrvModesGet(const char* name, int port)   // -1 = unknown
{
    char k[16]; SrvModesKey(name, port, k, sizeof(k));
    return ::cfg->GetInt(k, -1, "MPServerModes");
}
// 3.10: the Cheat Menu permission is kept per server too. It used to be the Cheat Menu switch of
// whatever phone happened to host the server: the creator allowed Cheat Menu, but as soon as
// another phone hosted it (the creator left, the server restarted on someone else's phone) that
// phone's switch (off) was used and every Cheat Menu player got "this server doesn't allow cheat
// menus". Saved in [MPServerCheatMenu]; learnt from the server's permissions when you are on it.
static int SrvCmGet(const char* name, int port)   // -1 = unknown, 0 = not allowed, 1 = allowed
{
    char k[16]; SrvModesKey(name, port, k, sizeof(k)); k[0] = 'C'; k[1] = 'M';
    return ::cfg->GetInt(k, -1, "MPServerCheatMenu");
}
static void SrvCmPut(const char* name, int port, bool allow)
{
    if(!name || !*name || MP::IsDefaultServer(name, port) || MP_IsBcwServerName(name)) return;
    if(SrvCmGet(name, port) == (allow ? 1 : 0)) return;
    char k[16]; SrvModesKey(name, port, k, sizeof(k)); k[0] = 'C'; k[1] = 'M';
    ConfigEntry* e = ::cfg->Bind(k, allow ? 1 : 0, "MPServerCheatMenu"); e->SetInt(allow ? 1 : 0); delete e;
    ::cfg->Save();
}
static void SrvModesPut(const char* name, int port, uint16_t modes)
{
    if(!name || !*name || MP::IsDefaultServer(name, port)) return;
    if(SrvModesGet(name, port) == (int)modes) return;
    char k[16]; SrvModesKey(name, port, k, sizeof(k));
    ConfigEntry* e = ::cfg->Bind(k, (int)modes, "MPServerModes"); e->SetInt((int)modes); delete e;
    ::cfg->Save();
}

// Starts a server on this phone. It keeps running when you disconnect, and it is saved
// (HostLAN / HostedName / HostedPort) so it comes back the next time the game starts.
static bool StartLocalServer(const char* name, int port)
{
    if(s_Server.IsRunning())
    {
        if(port == s_Server.GetPort() && !strcasecmp(name, s_Server.GetName())) return true;
        // one server per phone: hand the old one over (its players take over automatically)
        if(client.GetState() != NET_DISCONNECTED && IsLocalHost(client.GetHost())) LeaveServer(nullptr);
        s_Server.Stop();
    }
    s_Server.SetPersistent(true);
    const bool bcw = MP_IsBcwServerName(name);   // 3.04: Brief Case War: open, 20 players, its own gamemode only
    s_Server.SetPassword(MP::IsDefaultServer(name, port) || bcw ? "" : s_szJoinPw);   // 2.19
    // 2.29: the default server always has the full 100 slots (0/100), wherever it runs
    int maxp = MP::IsDefaultServer(name, port) ? MP_MAX_PLAYERS : settings.maxPlayers;
    if(!s_Server.Start((uint16_t)port, name, ServerLog, maxp, settings.region))
    {
        SysMsg(COL_RED, "Could not start the server on port %d", port);
        settings.hostLan = false; SaveConfig();
        return false;
    }
    MP_CopyStr(settings.hostedName, name, sizeof(settings.hostedName));
    settings.hostedPort = port;
    {   // 3.03: the gamemodes of this server
        const bool creating = s_bCreatingNew; s_bCreatingNew = false;
        int known = SrvModesGet(name, port);
        if(MP::IsDefaultServer(name, port)) s_nRunModes = GM_ALL;
        else if(bcw) s_nRunModes = 0;
        else if(known >= 0) s_nRunModes = (uint16_t)known;                 // it exists: keeps its own
        else if(creating) { s_nRunModes = (uint16_t)settings.hostModes; SrvModesPut(name, port, s_nRunModes); }   // new: your choice
        else s_nRunModes = 0;                                               // someone else's server we never saw: none
        // 3.10: Cheat Menu permission - the server's own, not this phone's switch
        int cm = SrvCmGet(name, port);
        bool own = !strcasecmp(name, settings.serverName) && port == settings.port;
        if(MP::IsDefaultServer(name, port) || bcw) s_bRunCheatMenu = false;
        else if(creating || (cm < 0 && own)) { s_bRunCheatMenu = settings.allowCheatMenu; SrvCmPut(name, port, s_bRunCheatMenu); }
        else s_bRunCheatMenu = cm > 0;
    }
    MP_CopyStr(settings.hostedPassword, MP::IsDefaultServer(name, port) || bcw ? "" : s_szJoinPw, sizeof(settings.hostedPassword));
    ApplyServerPerms();   // 2.16
    settings.hostLan = true;
    SaveConfig();
    return true;
}

static void JoinFound(const LanServer& sv, bool connect)
{
    if(!sv.token) { MP_CopyStr(settings.host, sv.ip, sizeof(settings.host)); SaveConfig(); }  // relay IPs change every session
    s_bConnectingDefault = IsDefaultServer(sv.name, sv.port);
    if(connect && client.GetState() == NET_DISCONNECTED) DoConnect(sv.ip, sv.port);
}

// Big Connect button: the default server.
void MP::ToggleConnect()
{
    if(client.GetState() != NET_DISCONNECTED)
    {
        LeaveServer(IsHosting() ? "Disconnected. Your server stays online for the others." : "Disconnected from the server.");
        return;
    }
    if(CheatMenuBlocks(MP_DEFAULT_SERVER_NAME, true)) return;   // 2.37
    if(!LAN::HasNetwork()) { s_bHasNetwork = false; OfflineMsg(); return; }
    s_bHasNetwork = true;
    if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    s_bConnectingDefault = true;
    s_bBcwJoin = false;       // 3.04
    s_bQuietRejoin = false;   // 2.27: a join you start yourself shows the normal feed
    s_szJoinPw[0] = 0;   // the default server is open
    JoinOverlayStart();  // 2.19: black slide + connection camera
    if(IsHostingDefault()) { DoConnect("127.0.0.1", MP_DEFAULT_PORT); return; }
    StartFeed();
    StartJob(JOB_CONNECT, true, MP_DEFAULT_SERVER_NAME, MP_DEFAULT_PORT);
}

// Join button of a custom server. Custom servers are always active like the default one:
//  1. the host/IP itself answers (a PC/VPS/domain server)  -> join it
//  2. a phone on your Wi-Fi/hotspot runs the same name+port -> join it
//  3. nobody runs it yet                                    -> this phone hosts it and joins
void MP::JoinServer(const char* host, int port, const char* name)
{
    {   // 3.10: the server you are already on -> don't leave it and connect all over again
        const char* nm = name && *name ? name : host;
        if(client.GetState() != NET_DISCONNECTED && client.GetPort() == port && nm &&
           ((client.GetServerName()[0] && !strcasecmp(client.GetServerName(), nm)) || (host && *host && !strcmp(client.GetHost(), host)) ||
            (IsDefaultServer(nm, port) && IsOnDefault())))
        {
            SysMsg(COL_SYSTEM, client.GetState() == NET_CONNECTED ? "You are already connected to %s." : "Already connecting to %s...", nm);
            return;
        }
    }
    if(CheatMenuBlocks(name && *name ? name : host, IsDefaultServer(name && *name ? name : host, port))) return;   // 2.37 / 3.00
    if(!LAN::HasNetwork()) { s_bHasNetwork = false; OfflineMsg(); return; }
    s_bHasNetwork = true;
    if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    LeaveServer(nullptr);
    if(!name || !*name) name = host;
    s_bConnectingDefault = IsDefaultServer(name, port);
    s_bBcwJoin = MP_IsBcwServerName(name);   // 3.04
    s_bQuietRejoin = false;   // 2.27
    s_szJoinPw[0] = 0;   // 2.19: the saved password of that custom server
    for(int i = 0; i < ServerList::Count(); ++i)
        if(ServerEntry* e = ServerList::Get(i))
            if(e->port == port && (!strcasecmp(e->name, name) || !strcasecmp(e->host, host ? host : ""))) { MP_CopyStr(s_szJoinPw, e->password, sizeof(s_szJoinPw)); break; }
    JoinOverlayStart();   // 2.19
    if(s_Server.IsRunning() && s_Server.GetPort() == port && !strcasecmp(s_Server.GetName(), name))
    {
        MP_CopyStr(s_szJoinPw, settings.hostedPassword, sizeof(s_szJoinPw));   // 2.19.6: your own password
        DoConnect("127.0.0.1", port);   // you host it
        return;
    }
    StartFeed();
    StartJob(JOB_JOIN, true, name, port, host);
}

void MP::SetHostLan(bool on, bool autoConnect)
{
    if(on && !s_Server.IsRunning())
    {
        // 2.37 / 3.00: hosting with a cheat menu only when your own server allows it
        if(CheatMenuBlocks(settings.serverName, !settings.allowCheatMenu || !settings.serverName[0] || !strcasecmp(settings.serverName, MP_DEFAULT_SERVER_NAME))) { settings.hostLan = false; return; }
        if(!LAN::HasNetwork()) { s_bHasNetwork = false; OfflineMsg(); settings.hostLan = false; return; }
        if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
        // 2.19.6: hosting used to fail silently. With the default name + port, "Host" found the
        // default server online and just joined it; a custom name on port 1922 was refused.
        // Now the name / port are fixed up so you always get your own server.
        if(!settings.serverName[0] || !strcasecmp(settings.serverName, MP_DEFAULT_SERVER_NAME) || MP_IsBcwServerName(settings.serverName))   // 3.04: Brief Case War is private
        {
            char nm[64]; snprintf(nm, sizeof(nm), "%s's server", settings.name[0] ? settings.name : "Player");
            MP_CopyStr(settings.serverName, nm, sizeof(settings.serverName));
            SysMsg(COL_SYSTEM, "Server name: \"%s\" (change it in Host your own server).", settings.serverName);
        }
        if(settings.port == MP_DEFAULT_PORT)
        {
            settings.port = MP_DEFAULT_PORT + 1;
            SysMsg(COL_SYSTEM, "Port %d is the default server's, your server uses port %d.", MP_DEFAULT_PORT, settings.port);
        }
        SaveConfig();
        MP_CopyStr(s_szJoinPw, settings.serverPassword, sizeof(s_szJoinPw));   // 2.19
        // never create a second copy of a server that is already online
        s_bHostJobCreate = true;   // 3.03
        StartJob(JOB_HOST, autoConnect, settings.serverName, settings.port);
        return;
    }
    else if(!on && s_Server.IsRunning())
    {
        if(client.GetState() != NET_DISCONNECTED && IsLocalHost(client.GetHost())) LeaveServer(nullptr);
        s_Server.Stop();   // connected players move to a new host automatically
        SysMsg(COL_YELLOW, "Server stopped.");
    }
    settings.hostLan = s_Server.IsRunning();
    SaveConfig();
}

// Called when the connection to the server is lost. On a LAN server the remaining players
// take over: the first player (lowest id) becomes the new host with the same name and port,
// everybody else finds that new host and joins it, so the server never disappears.
static bool OnLostServer()
{
    // our own server (127.0.0.1) or an internet server: nothing to take over
    if(IsHosting() || IsLocalHost(client.GetHost()) || !LAN::IsPrivateAddress(client.GetHost()) || !LAN::HasNetwork()) return false;
    int rank = 0;
    for(int i = 0; i < MP_MAX_PLAYERS && i < myId; ++i) if(players[i].active) ++rank;
    MP_CopyStr(s_szLostServerName, client.GetServerName()[0] ? client.GetServerName() : MP_DEFAULT_SERVER_NAME, sizeof(s_szLostServerName));
    s_nLostPort = client.GetPort();
    s_bTakeoverPending = true;
    s_nTakeoverAt = MP_NowMs() + 500 + (uint64_t)(rank > 20 ? 20 : rank) * 2500;
    SysMsg(COL_YELLOW, "The host left - moving to the new host...");
    s_bQuietRejoin = true;   // 2.27: same server on another phone, you keep playing where you are
    return true;
}

static bool IpLess(const char* a, const char* b)
{
    in_addr x, y;
    if(inet_pton(AF_INET, a, &x) != 1 || inet_pton(AF_INET, b, &y) != 1) return strcmp(a, b) < 0;
    return ntohl(x.s_addr) < ntohl(y.s_addr);
}

// exact server: same name and port
static int FindExact(const std::vector<LanServer>& list, const char* name, int port)
{
    int best = LAN::PickBest(list, name, (uint16_t)port);
    return (best >= 0 && !strcasecmp(list[best].name, name)) ? best : -1;
}

static void FinishJob(int job, std::vector<LanServer>& list)
{
    const bool connect = s_bJobAutoConnect;
    const char* name = s_szJobName;
    const int port = s_nJobPort;
    switch(job)
    {
        case JOB_HOST:
        {
            int best = FindExact(list, name, port);
            if(best >= 0)
            {
                const LanServer& sv = list[best];
                settings.hostLan = false; SaveConfig();
                SysMsg(COL_YELLOW, "\"%s\" is already online on another phone - joining it. Pick another name to host your own.", name);   // 2.19.6
                JoinFound(sv, connect);
                return;
            }
            s_bCreatingNew = s_bHostJobCreate; s_bHostJobCreate = false;   // 3.03: Host button = create it (switches, if it is new)
            if(StartLocalServer(name, port))
            {
                SysMsg(COL_GREEN, "Your server \"%s\" is online (port %d).", name, port);   // 2.19.6
                if(connect && client.GetState() == NET_DISCONNECTED)
                {
                    s_bConnectingDefault = IsDefaultServer(name, port);
                    DoConnect("127.0.0.1", port);
                }
            }
            return;
        }
        case JOB_CONNECT:   // default server
        {
            int best = FindExact(list, name, port);
            if(best >= 0)
            {
                JoinFound(list[best], true);
                return;
            }
            // nobody runs it yet: this phone hosts it, so the default server is always online
            if(StartLocalServer(name, port)) { s_bConnectingDefault = true; DoConnect("127.0.0.1", port); }
            return;
        }
        case JOB_JOIN:      // custom server
        {
            char resolved[16];
            { std::lock_guard<std::mutex> lk(s_LanMutex); MP_CopyStr(resolved, s_szJobResolved, sizeof(resolved)); }
            for(auto& sv : list)
            {
                if(resolved[0] && !strcmp(sv.ip, resolved) && sv.port == port && sv.proto == MP_PROTO_VERSION)
                {
                    s_bConnectingDefault = false;
                    DoConnect(sv.ip, sv.port);
                    return;
                }
            }
            int best = FindExact(list, name, port);
            if(best >= 0)
            {
                s_bConnectingDefault = false;
                DoConnect(list[best].ip, list[best].port);
                return;
            }
            if(port == MP_DEFAULT_PORT && !IsDefaultServer(name, port))
            {
                SysMsg(COL_RED, "\"%s\" isn't online and uses port %d (reserved for the default server). Edit it to port %d or higher.", name, MP_DEFAULT_PORT, MP_DEFAULT_PORT + 1);
                return;
            }
            if(StartLocalServer(name, port)) { s_bConnectingDefault = false; DoConnect("127.0.0.1", port); }
            return;
        }
        case JOB_TAKEOVER:
        {
            s_bTakeoverPending = false;
            int best = FindExact(list, s_szLostServerName, s_nLostPort);
            if(best >= 0)
            {
                JoinFound(list[best], true);
                return;
            }
            if(StartLocalServer(s_szLostServerName, s_nLostPort))
            {
                s_bConnectingDefault = IsDefaultServer(s_szLostServerName, s_nLostPort);
                DoConnect("127.0.0.1", s_nLostPort);
            }
            return;
        }
        case JOB_DUPCHECK:
        {
            // two copies of the same server (e.g. the old host came back): merge into one.
            if(!IsHosting()) return;
            const char* me = LanAddress();
            int mine = s_Server.GetPlayerCount();
            uint32_t myTok = Online::MyToken();
            bool onlineCopy = false;   // the other copy is in the online lobby: decide by token (same answer on both phones)
            for(auto& sv : list)
                if(sv.token && sv.port == s_Server.GetPort() && !strcasecmp(sv.name, s_Server.GetName())) onlineCopy = true;
            for(auto& sv : list)
            {
                if(sv.port != s_Server.GetPort() || strcasecmp(sv.name, s_Server.GetName()) || sv.proto != MP_PROTO_VERSION) continue;
                bool otherWins;
                if(sv.token) otherWins = sv.players > mine || (sv.players == mine && myTok && sv.token < myTok);
                else if(onlineCopy && myTok) continue;
                else otherWins = sv.players > mine || (sv.players == mine && IpLess(sv.ip, me));
                // 2.27: "someone joins -> I get Connecting... again". A phone that couldn't see this
                // server yet starts its own copy; on a 1-1 tie the token / IP rule could make the
                // phone that was ALREADY playing leave and rejoin the newcomer. Now every copy
                // announces when it started and the older copy wins a tie (same answer on both
                // phones; a server older than 2.27 = the old rule).
                const uint32_t myStart = s_Server.GetStartUnix();
                if(sv.players == mine && sv.startUnix && myStart && sv.startUnix != myStart)
                    otherWins = sv.startUnix < myStart;   // older copy wins the tie
                // 2.29: the 24/7 copy (mp_server on a machine that never leaves) always wins over a phone
                // copy, even with fewer players: a phone host leaving would take the server with it.
                if(sv.dedicated) otherWins = true;
                if(!otherWins) continue;
                bool wasConnected = client.GetState() != NET_DISCONNECTED;
                if(client.GetState() == NET_CONNECTED) { s_bQuietRejoin = true; SysMsg(COL_SYSTEM, "Same server on another phone - moving there (you stay connected)."); }
                LeaveServer(nullptr);
                s_Server.Stop();
                settings.hostLan = false; SaveConfig();
                JoinFound(sv, wasConnected);
                if(client.GetState() == NET_DISCONNECTED) s_bQuietRejoin = false;
                return;
            }
            return;
        }
        default: return;
    }
}

static void ProcessLan(uint64_t now)
{
    // online lobby: announce the server this phone hosts (player count refreshed every 0.5 s)
    {
        static uint64_t lastCount = 0; static int count = 0;
        bool run = s_Server.IsRunning();
        if(run && now - lastCount > 500) { lastCount = now; count = s_Server.GetPlayerCount(); }
        Online::SetHosted(run, run ? s_Server.GetName() : "", run ? s_Server.GetPort() : 0, run ? count : 0, s_Server.GetMaxPlayers(), s_Server.GetRegion());
        {   // 2.24: server icon (the default "MP LAN Server" always shows the default icon)
            int ic = IsHostingDefault() ? 0 : settings.serverIcon;
            s_Server.SetIcon(ic); Online::SetHostIcon(ic);
            Online::SetHostStart(run ? s_Server.GetStartUnix() : 0);   // 2.27: duplicate merge (older copy wins)
            // 2.25: language - the one saved with this custom server when it was added, else yours
            const char* lg = settings.serverLang;
            if(run)
            {
                int k = ServerList::FindByNamePort(s_Server.GetName(), s_Server.GetPort());
                if(ServerEntry* e = ServerList::Get(k)) if(e->lang[0]) lg = e->lang;
            }
            static char lastLg[MP_LANG_LEN] = "\x01";
            if(strcmp(lastLg, lg)) { MP_CopyStr(lastLg, lg, sizeof(lastLg)); s_Server.SetLang(lg); Online::SetHostLang(lg); }
        }
        // 2.20 regional brokers: until the player picks a region, use the lowest-ping one (once)
        static bool regionAutoDone = false;
        if(!regionAutoDone && !settings.regionPicked && Online::HasRegionalBrokers() && !run && client.GetState() == NET_DISCONNECTED)
        {
            int best = Online::BestRegion();
            if(best >= 0)
            {
                regionAutoDone = true;
                if(best != settings.region)
                {
                    settings.region = best; s_Server.SetRegion(best); SaveConfig();
                    SysMsg(COL_SYSTEM, "Region set to %s (lowest ping, %d ms). Change it in the Server menu.", g_MPRegionNames[best], Online::RegionPingMs(best));
                }
            }
        }
        Online::SetRegionPrefs(settings.region, settings.lobbyAuto);
    }
    // network state: no Wi-Fi / data / hotspot = not connected
    if(now - s_nLastNetCheck > 2000)
    {
        s_nLastNetCheck = now;
        // 2.21: Android drops the interface list for a moment on Wi-Fi scans / data handover: only
        // count it as offline after ~6 s in a row, and never while the internet relay still talks
        static int s_nNoNetChecks = 0;
        bool has = LAN::HasNetwork() || Online::IsConnected();
        s_nNoNetChecks = has ? 0 : s_nNoNetChecks + 1;
        if(!has && s_nNoNetChecks < 3) has = true;
        if(!has && client.GetState() != NET_DISCONNECTED) { LeaveServer(nullptr); OfflineMsg(); }
        if(!has) { s_bTakeoverPending = false; s_bQuietRejoin = false; }
        if(has && !s_bHasNetwork) SysMsg(COL_GREEN, "You are online again.");
        s_bHasNetwork = has;
    }

    if(s_bJobDone)
    {
        std::vector<LanServer> list;
        { std::lock_guard<std::mutex> lk(s_LanMutex); list = s_JobResult; }
        s_nNearbyTime = now;
        int job = s_nJob;
        s_bJobDone = false;
        s_nJob = JOB_NONE;
        FinishJob(job, list);
        if(s_Queued.job != JOB_NONE)
        {
            QueuedJob q = s_Queued; s_Queued = QueuedJob();
            StartJob(q.job, q.autoConnect, q.name, q.port, q.host);
        }
    }
    if(!s_bHasNetwork || IsSearching()) return;

    if(s_bTakeoverPending && now >= s_nTakeoverAt && client.GetState() == NET_DISCONNECTED && !IsHosting())
    {
        StartJob(JOB_TAKEOVER, true, s_szLostServerName, s_nLostPort);
        return;
    }
    if(IsHosting() && now - s_nLastDupCheck > 8000)
    {
        s_nLastDupCheck = now;
        StartJob(JOB_DUPCHECK, false, s_Server.GetName(), s_Server.GetPort());
    }
}

void MP::SetNameColor(uint32_t rgba)
{
    settings.nameColor = rgba ? (rgba | 0xFF) : 0;
    SaveConfig();
    if(IsConnected())
    {
        PktSetColor c; c.type = PKT_SET_COLOR; c.id = myId; c.color = settings.nameColor;
        client.Send(&c, sizeof(c));
    }
    else if(settings.nameColor) myColor = settings.nameColor;
}

void MP::SendChat(const char* text)
{
    if(!text || !*text) return;
    if(text[0] == '/' && HandleCommand(text)) return;   // 2.19 commands (work offline too)
    if(!IsConnected())
    {
        std::string mn = MP::IsLocalAdmin() ? "[ADMIN] SENJI" : settings.name; uint32_t mc = settings.nameColor ? settings.nameColor : COL_WHITE;
        BcwDecorateStr(myId, mn, mc);   // 3.04
        PushChat({ mn, mc, text, COL_WHITE });   // 2.37: my own chat shows the admin look too
        SysMsg(COL_RED, "Not connected.");
        return;
    }
    PktChat c; memset(&c, 0, sizeof(c));
    c.type = PKT_CHAT; c.id = myId;
    MP_CopyStr(c.text, text, MP_CHAT_LEN);
    client.Send(&c, sizeof(c));
}

// ------------------------------------------------------------------------------------------
// Config
void MP::SaveConfig()
{
    auto S = [](const char* k, const char* v) { ConfigEntry* e = ::cfg->Bind(k, v, "MP"); e->SetString(v); delete e; };
    auto I = [](const char* k, int v) { ConfigEntry* e = ::cfg->Bind(k, v, "MP"); e->SetInt(v); delete e; };
    auto F = [](const char* k, float v) { ConfigEntry* e = ::cfg->Bind(k, v, "MP"); e->SetFloat(v); delete e; };
    auto B = [](const char* k, bool v) { ConfigEntry* e = ::cfg->Bind(k, v, "MP"); e->SetBool(v); delete e; };
    S("Name", settings.name); S("Host", settings.host); S("ServerName", settings.serverName); S("HostedName", settings.hostedName); I("HostedPort", settings.hostedPort); I("Port", settings.port); B("HostLAN", settings.hostLan);
    F("NametagSize", settings.nametagSize); F("NametagDistance", settings.nametagDistance);
    B("ShowOwnNametag", settings.showOwnTag); B("ShowHealthBars", settings.showHealthBars); B("ShowRadarIcons", settings.showRadarIcons);
    B("ShowChat", settings.showChat); B("ShowKillFeed", settings.showKillFeed); B("ShowStatus", settings.showStatus);
    B("SyncAnims", settings.syncAnims); B("SyncEnv", settings.syncEnv); B("SyncNPCs", settings.syncNpcs); B("AllowMissionPair", settings.allowMissionPair); B("AllowCheatMenu", settings.allowCheatMenu); B("HitSound", settings.hitSound); B("DisableCheats", settings.disableCheats);
    F("ChatX", settings.chatX); F("ChatY", settings.chatY); I("Skin", settings.skin);
    I("ChatLines", settings.chatLines); F("ChatSize", settings.chatSize); F("ChatScale", settings.chatScale); F("ChatWidth", settings.chatWidth);   // 2.19.6
    I("ChatFade", settings.chatFade); B("ChatBackground", settings.chatBg);
    B("ChatBoxBackground", settings.chatBoxBg); B("ChatBoxLabel", settings.chatBoxLabel); B("ChatBoxTap", settings.chatBoxTap);   // 2.22
    F("ChatBoxX", settings.chatBoxX); F("ChatBoxY", settings.chatBoxY); B("RealSkin", settings.realSkin);
    I("Region", settings.region); B("LobbyAuto", settings.lobbyAuto); B("RegionPicked", settings.regionPicked); I("NameColor", (int)settings.nameColor); I("MaxPlayers", settings.maxPlayers); I("HostGamemodes", settings.hostModes); I("ServerIcon", settings.serverIcon); S("ServerLang", settings.serverLang); S("DeviceId", settings.deviceId);
    B("FullProgressDefault", settings.fullProgressDefault);
    B("UnlockWorld", settings.unlockWorld); B("Girlfriends", settings.girlfriends); B("EventTips", settings.eventTips);   // 2.21
    B("ChatTimestamps", settings.chatTimestamps); B("VoiceChat", settings.voiceChat); F("VoiceVolume", settings.voiceVolume); F("MicGain", settings.micGain); F("VoicePitch", settings.voicePitch);   // 3.11
    B("ChatBubbles", settings.chatBubbles); B("Labels3D", settings.labels3d); B("MapIcons", settings.mapIcons); B("Actors", settings.actors); B("GangZones", settings.gangZones);   // 3.01
    I("Team", settings.team); I("ClassSkin", settings.classSkin);
    S("ServerPassword", settings.serverPassword); S("HostedPassword", settings.hostedPassword);   // 2.19
    ::cfg->Save();
}

static void LoadConfig()
{
    ::cfg->GetString(settings.name, sizeof(settings.name), "Name", "Player", "MP");
    ::cfg->GetString(settings.host, sizeof(settings.host), "Host", MP_DEFAULT_HOST, "MP");
    settings.port            = ::cfg->GetInt("Port", MP_DEFAULT_PORT, "MP");
    ::cfg->GetString(settings.serverName, sizeof(settings.serverName), "ServerName", MP_DEFAULT_SERVER_NAME, "MP");
    if(!settings.serverName[0]) MP_CopyStr(settings.serverName, MP_DEFAULT_SERVER_NAME, sizeof(settings.serverName));
    ::cfg->GetString(settings.hostedName, sizeof(settings.hostedName), "HostedName", settings.serverName, "MP");
    if(!settings.hostedName[0]) MP_CopyStr(settings.hostedName, MP_DEFAULT_SERVER_NAME, sizeof(settings.hostedName));
    settings.hostedPort      = ::cfg->GetInt("HostedPort", settings.port, "MP");
    if(settings.hostedPort <= 0 || settings.hostedPort > 65535) settings.hostedPort = MP_DEFAULT_PORT;
    settings.hostLan         = ::cfg->GetBool("HostLAN", false, "MP");
    settings.nametagSize     = ::cfg->GetFloat("NametagSize", 1.0f, "MP");
    settings.nametagDistance = ::cfg->GetFloat("NametagDistance", 60.0f, "MP");
    settings.showOwnTag      = ::cfg->GetBool("ShowOwnNametag", true, "MP");
    settings.showHealthBars  = ::cfg->GetBool("ShowHealthBars", true, "MP");
    settings.showRadarIcons  = ::cfg->GetBool("ShowRadarIcons", true, "MP");
    settings.showChat        = ::cfg->GetBool("ShowChat", true, "MP");
    settings.showKillFeed    = ::cfg->GetBool("ShowKillFeed", true, "MP");
    settings.syncAnims       = ::cfg->GetBool("SyncAnims", true, "MP");
    settings.syncEnv         = ::cfg->GetBool("SyncEnv", true, "MP");
    settings.syncNpcs        = ::cfg->GetBool("SyncNPCs", true, "MP");
    settings.allowMissionPair = ::cfg->GetBool("AllowMissionPair", true, "MP");
    settings.allowCheatMenu = ::cfg->GetBool("AllowCheatMenu", false, "MP");   // 3.00
    settings.hitSound = ::cfg->GetBool("HitSound", true, "MP");               // 3.00
    settings.chatBubbles = ::cfg->GetBool("ChatBubbles", false, "MP");   // 3.01: off by default
    settings.chatTimestamps = ::cfg->GetBool("ChatTimestamps", false, "MP");   // 3.11: off until the player turns it on
    settings.voiceChat   = ::cfg->GetBool("VoiceChat", true, "MP");
    settings.voiceVolume = ::cfg->GetFloat("VoiceVolume", 1.0f, "MP");
    settings.micGain     = ::cfg->GetFloat("MicGain", 1.0f, "MP");
    settings.voicePitch  = ::cfg->GetFloat("VoicePitch", 1.0f, "MP");
    settings.disableCheats = ::cfg->GetBool("DisableCheats", false, "MP");   // 2.35
    settings.showStatus      = ::cfg->GetBool("ShowStatus", true, "MP");
    settings.chatX           = ::cfg->GetFloat("ChatX", 0.22f, "MP");
    settings.chatY           = ::cfg->GetFloat("ChatY", 0.02f, "MP");
    settings.skin            = ::cfg->GetInt("Skin", 106, "MP");
    settings.chatLines       = ::cfg->GetInt("ChatLines", 6, "MP");          // 2.19.6
    settings.chatSize        = ::cfg->GetFloat("ChatSize", 0.80f, "MP");
    settings.chatScale       = ::cfg->GetFloat("ChatScale", 1.00f, "MP");   // 3.04.5
    settings.chatWidth       = ::cfg->GetFloat("ChatWidth", 0.42f, "MP");
    settings.chatFade        = ::cfg->GetInt("ChatFade", 25, "MP");
    settings.chatBg          = ::cfg->GetBool("ChatBackground", true, "MP");
    settings.chatBoxBg       = ::cfg->GetBool("ChatBoxBackground", true, "MP");   // 2.22
    settings.chatBoxLabel    = ::cfg->GetBool("ChatBoxLabel", true, "MP");
    settings.chatBoxTap      = ::cfg->GetBool("ChatBoxTap", true, "MP");
    settings.chatBoxX        = ::cfg->GetFloat("ChatBoxX", -1.0f, "MP");
    settings.chatBoxY        = ::cfg->GetFloat("ChatBoxY", -1.0f, "MP");
    settings.realSkin        = ::cfg->GetBool("RealSkin", true, "MP");
    MP::ClampChatSettings();
    settings.region          = ::cfg->GetInt("Region", REGION_ASIA, "MP");
    settings.lobbyAuto       = ::cfg->GetBool("LobbyAuto", true, "MP");
    settings.unlockWorld     = ::cfg->GetBool("UnlockWorld", true, "MP");   // 2.21
    settings.girlfriends     = ::cfg->GetBool("Girlfriends", false, "MP");
    settings.eventTips       = ::cfg->GetBool("EventTips", true, "MP");
    settings.labels3d        = ::cfg->GetBool("Labels3D", true, "MP");
    settings.mapIcons        = ::cfg->GetBool("MapIcons", true, "MP");
    settings.actors          = ::cfg->GetBool("Actors", true, "MP");
    settings.gangZones       = ::cfg->GetBool("GangZones", true, "MP");
    settings.team            = ::cfg->GetInt("Team", 0, "MP");
    settings.classSkin       = ::cfg->GetInt("ClassSkin", -1, "MP");
    if(settings.team < 0 || settings.team >= 8) settings.team = 0;
    settings.regionPicked    = ::cfg->GetBool("RegionPicked", false, "MP");
    settings.nameColor       = (uint32_t)::cfg->GetInt("NameColor", 0, "MP");
    settings.maxPlayers      = ::cfg->GetInt("MaxPlayers", MP_MAX_PLAYERS, "MP");
    settings.hostModes       = ::cfg->GetInt("HostGamemodes", 0, "MP") & GM_ALL;   // 3.02: none until the host turns them on
    settings.serverIcon      = ::cfg->GetInt("ServerIcon", 0, "MP");   // 2.24
    ::cfg->GetString(settings.serverLang, sizeof(settings.serverLang), "ServerLang", "English", "MP");   // 2.25
    MP_CleanLang(settings.serverLang);
    ::cfg->GetString(settings.deviceId, sizeof(settings.deviceId), "DeviceId", "", "MP");
    LoadAdminFile();   // 2.37: admin.txt = proof you are SENJI
    MP_StripAdminTag(settings.name);
    if(MP_IsReservedName(settings.name) && !settings.adminKey[0]) MP_CopyStr(settings.name, "Player", sizeof(settings.name));   // no admin.txt: the name is not yours
    if(strlen(settings.deviceId) != MP_UID_LEN)   // 2.25: first start -> make this phone's id
    {
        srand((unsigned)(time(nullptr) ^ (uintptr_t)&settings));
        for(int i = 0; i < MP_UID_LEN; ++i) settings.deviceId[i] = "0123456789abcdef"[rand() & 15];
        settings.deviceId[MP_UID_LEN] = 0;
        ConfigEntry* e = ::cfg->Bind("DeviceId", settings.deviceId, "MP"); e->SetString(settings.deviceId); delete e; ::cfg->Save();
    }
    if(settings.serverIcon < 0 || settings.serverIcon > MP_ICON_MAX) settings.serverIcon = 0;
    settings.fullProgressDefault = ::cfg->GetBool("FullProgressDefault", true, "MP");
    ::cfg->GetString(settings.serverPassword, sizeof(settings.serverPassword), "ServerPassword", "", "MP");   // 2.19
    ::cfg->GetString(settings.hostedPassword, sizeof(settings.hostedPassword), "HostedPassword", "", "MP");
    if(settings.region < 0 || settings.region >= REGION_COUNT) settings.region = REGION_ASIA;
    if(settings.maxPlayers < 1 || settings.maxPlayers > MP_MAX_PLAYERS) settings.maxPlayers = MP_MAX_PLAYERS;
    if(settings.port <= 0 || settings.port > 65535) settings.port = MP_DEFAULT_PORT;
    if(settings.nametagSize < 0.4f || settings.nametagSize > 3.0f) settings.nametagSize = 1.0f;
}

// ------------------------------------------------------------------------------------------
// Floating menu sections
// 2.37: the name SENJI is reserved for the default server's administrator. The proof is the file
//   /sdcard/Android/data/com.rockstargames.gtasa/files/MP/admin.txt   (one line: the admin key)
// No admin.txt (or a wrong key in it) = SENJI turns red in the keyboard, can't be entered, and the
// server refuses the join. The server checks the key too, so editing the config doesn't help.
// 3.11.2: admin.txt is looked for in every usual place, not only the MP folder AML gives us
// (AML on Android 11+ uses /storage/emulated/0/Android_unprotected/data/<game>/files, while people
// copy files to /sdcard/Android/data/<game>/files). Found somewhere else = copied into the MP folder.
static char s_szAdminFound[512] = "";
static void ReadAdminKeys(const char* path, std::vector<std::string>& keys);
static bool ReadAdminKeyFile(const char* path, char* out, size_t outSz);
static void AdminCandidates(std::vector<std::string>& v)
{
    std::string dd = s_szDataDir;
    v.push_back(dd + "/admin.txt");
    auto swap = [&](const char* a, const char* b) { size_t k = dd.find(a); if(k != std::string::npos) { std::string o = dd; o.replace(k, strlen(a), b); v.push_back(o + "/admin.txt"); } };
    swap("/Android_unprotected/", "/Android/");
    swap("/Android/", "/Android_unprotected/");
    swap("/storage/emulated/0/", "/sdcard/");
    swap("/sdcard/", "/storage/emulated/0/");
    const char* pk[] = { "com.rockstargames.gtasa" };
    for(const char* k : pk)
    {
        char b[256];
        const char* fmt[] = { "/storage/emulated/0/Android/data/%s/files/MP/admin.txt", "/storage/emulated/0/Android_unprotected/data/%s/files/MP/admin.txt",
                              "/storage/emulated/0/Android/media/%s/MP/admin.txt", "/storage/emulated/0/Android_unprotected/data/%s/mods/admin.txt",
                              "/storage/emulated/0/Android/data/%s/mods/admin.txt", "/storage/emulated/0/Android_unprotected/data/%s/files/admin.txt",
                              "/storage/emulated/0/Android/data/%s/files/admin.txt" };
        for(const char* f : fmt) { snprintf(b, sizeof(b), f, k); v.push_back(b); }
    }
    v.push_back("/storage/emulated/0/Download/admin.txt");
    v.push_back("/storage/emulated/0/Download/MP/admin.txt");
    v.push_back("/storage/emulated/0/admin.txt");
    v.push_back("/storage/emulated/0/MP/admin.txt");
    // 3.11.6: the admin key file is administrator.txt (admin.txt still read, in case it was renamed back)
    std::vector<std::string> both;
    for(auto& p : v)
    {
        size_t k = p.rfind("/admin.txt");
        if(k != std::string::npos) both.push_back(p.substr(0, k) + "/administrator.txt");
    }
    for(auto& p : v) both.push_back(p);
    v.swap(both);
}
static void LoadAdminFile()
{
    settings.adminKey[0] = 0; s_szAdminFound[0] = 0;
    std::vector<std::string> cand; AdminCandidates(cand);
    s_AdminKeys.clear(); s_nAdminKeyIdx = 0;
    bool inMine = false;
    for(size_t i = 0; i < cand.size(); ++i)
    {
        size_t before = s_AdminKeys.size();
        ReadAdminKeys(cand[i].c_str(), s_AdminKeys);
        if(s_AdminKeys.size() == before) continue;
        if(!s_szAdminFound[0]) MP_CopyStr(s_szAdminFound, cand[i].c_str(), sizeof(s_szAdminFound));
        if(i == 0) inMine = true;
    }
    if(!s_AdminKeys.empty())
    {
        MP_CopyStr(settings.adminKey, s_AdminKeys[0].c_str(), sizeof(settings.adminKey));
        if(!inMine)   // found somewhere else: keep a copy (all keys) in the MP folder too
        {
            char dst[512]; snprintf(dst, sizeof(dst), "%s/administrator.txt", s_szDataDir);
            if(FILE* o = fopen(dst, "w")) { for(auto& k : s_AdminKeys) fprintf(o, "%s\n", k.c_str()); fclose(o); }
        }
    }
    client.SetAdminKey(settings.adminKey);
    if(settings.adminKey[0]) logger->Info("MP: administrator.txt OK (%s, %d key(s)) - you are the administrator (SENJI)", s_szAdminFound, (int)s_AdminKeys.size());
    else logger->Info("MP: no valid administrator.txt (looked in %s and %d other places)", cand[0].c_str(), (int)cand.size() - 1);
}
static bool ReadAdminKeyFile(const char* path, char* out, size_t outSz)
{
    FILE* f = fopen(path, "r");
    if(!f) return false;
    bool ok = false;
    char line[512];
    while(fgets(line, sizeof(line), f))
    {
        char* p = line;
        while(*p == ' ' || *p == '\t' || (unsigned char)*p == 0xEF || (unsigned char)*p == 0xBB || (unsigned char)*p == 0xBF) ++p;   // spaces / UTF-8 BOM
        if(!strncasecmp(p, "key=", 4)) p += 4; else if(!strncasecmp(p, "key:", 4)) p += 4;
        while(*p == ' ') ++p;
        size_t n = strlen(p); while(n && (p[n-1] == '\n' || p[n-1] == '\r' || p[n-1] == ' ' || p[n-1] == '\t')) p[--n] = 0;
        if(!*p || *p == '#') continue;
        // a key pasted with stray quotes / spaces in the middle of the line still counts
        char clean[256]; size_t k = 0;
        for(const char* q = p; *q && k + 1 < sizeof(clean); ++q) if(*q != '"' && *q != '\'' && *q != ' ' && *q != '\t') clean[k++] = *q;
        clean[k] = 0;
        if(MP_AdminKeyOk(p)) { MP_CopyStr(out, p, outSz); ok = true; break; }
        if(MP_AdminKeyOk(clean)) { MP_CopyStr(out, clean, outSz); ok = true; break; }
    }
    fclose(f);
    return ok;
}
static void ReadAdminKeys(const char* path, std::vector<std::string>& keys)   // every valid line
{
    FILE* f = fopen(path, "r");
    if(!f) return;
    char line[512];
    while(fgets(line, sizeof(line), f))
    {
        char* p = line;
        while(*p == ' ' || *p == '\t' || (unsigned char)*p == 0xEF || (unsigned char)*p == 0xBB || (unsigned char)*p == 0xBF) ++p;
        if(!strncasecmp(p, "key=", 4) || !strncasecmp(p, "key:", 4)) p += 4;
        char clean[256]; size_t k = 0;
        for(const char* q = p; *q && k + 1 < sizeof(clean); ++q) if(*q != '"' && *q != '\'' && *q != ' ' && *q != '\t' && *q != '\r' && *q != '\n') clean[k++] = *q;
        clean[k] = 0;
        if(!clean[0] || clean[0] == '#') continue;
        size_t n = strlen(p); while(n && (p[n-1] == '\n' || p[n-1] == '\r' || p[n-1] == ' ' || p[n-1] == '\t')) p[--n] = 0;
        std::string key = MP_AdminKeyOk(p) ? std::string(p) : (MP_AdminKeyOk(clean) ? std::string(clean) : std::string());
        if(key.empty()) continue;
        bool dup = false; for(auto& e : keys) if(e == key) dup = true;
        if(!dup) keys.push_back(key);
    }
    fclose(f);
}
static void SenjiBlockedMsg()   // 3.11.2: say exactly where admin.txt has to go
{
    SysMsg(COL_RED, "The name SENJI is reserved for the administrator. No valid administrator.txt found.");
    SysMsg(COL_YELLOW, "Put administrator.txt here: %s/administrator.txt", s_szDataDir);
}
static bool HaveAdminKey() { return settings.adminKey[0] && MP_AdminKeyOk(settings.adminKey); }
static const char* NameBlocked(const char* t)
{
    if(!t || !*t) return nullptr;
    if(MP_IsReservedName(t) && !HaveAdminKey()) return "Reserved name";
    if(!strncasecmp(t, "[ADMIN]", 7)) return "Not allowed";
    return nullptr;
}
static void OnName(const char* t, void*)
{
    if(!*t) return;
    if(MP_IsReservedName(t) && !HaveAdminKey()) LoadAdminFile();   // admin.txt just copied in? read it again
    if(NameBlocked(t))
    {
        if(MP_IsReservedName(t)) SenjiBlockedMsg(); else SysMsg(COL_RED, "Names can't start with [ADMIN].");
        return;
    }
    if(MP_IsReservedName(t)) { MP_CopyStr(settings.name, MP_ADMIN_NAME, sizeof(settings.name)); SaveConfig(); SysMsg(COL_GREEN, "administrator.txt found: you are the administrator. On the default server you show as [ADMIN] SENJI."); return; }
    MP_CopyStr(settings.name, t, sizeof(settings.name)); SaveConfig();
}
static void OnHost(const char* t, void*) { if(*t) { MP_CopyStr(settings.host, t, sizeof(settings.host)); SaveConfig(); } }
static void OnServerName(const char* t, void*)
{
    char n[32]; MP_CopyStr(n, t, sizeof(n));
    for(char* c = n; *c; ++c) if(*c == '|') *c = '/';
    if(*n) { MP_CopyStr(settings.serverName, n, sizeof(settings.serverName)); SaveConfig(); }
}
static void OnPort(const char* t, void*) { int p = atoi(t); if(p > 0 && p < 65536) { settings.port = p; SaveConfig(); } }
static void OnChatSubmit(const char* t, void*) { MP::SendChat(t); }

// ---- Region + server list -----------------------------------------------------------------
static char s_szNewName[32] = "";
static char s_szNewHost[64] = "";
static char s_szNewPort[8]  = "1923";
static int  s_nSelServer = -1;
static void OnNewName(const char* t, void*) { MP_CopyStr(s_szNewName, t, sizeof(s_szNewName)); }
static void OnNewHost(const char* t, void*) { MP_CopyStr(s_szNewHost, t, sizeof(s_szNewHost)); }
static char s_szLocalIp[64] = "";       // 2.19.1 Local IP section
static char s_szLocalPort[8] = "";
static void OnLocalIp(const char* t, void*) { MP_CopyStr(s_szLocalIp, t, sizeof(s_szLocalIp)); char* c = s_szLocalIp; while(*c == ' ') memmove(c, c + 1, strlen(c)); for(; *c; ++c) if(*c == ' ' || *c == '/') { *c = 0; break; } }
static void OnLocalPort(const char* t, void*) { MP_CopyStr(s_szLocalPort, t, sizeof(s_szLocalPort)); }
static void ConnectLocalIp(const char* ip, int port)
{
    if(!ip || !*ip || port <= 0 || port > 65535) { SysMsg(COL_RED, "Type the IP and the port (1-65535)."); return; }
    if(CheatMenuBlocks(ip, false)) return;   // 2.37 / 3.00: decided by the server's permissions
    if(!LAN::HasNetwork()) { s_bHasNetwork = false; OfflineMsg(); return; }
    if(IsBusy()) { SysMsg(COL_YELLOW, "Please wait..."); return; }
    LeaveServer(nullptr);
    s_bConnectingDefault = false;
    s_szJoinPw[0] = 0;
    if(s_Server.IsRunning() && s_Server.GetPort() == port && (IsLocalHost(ip) || !strcmp(ip, LanAddress()))) { MP_CopyStr(s_szJoinPw, settings.serverPassword, sizeof(s_szJoinPw)); ip = "127.0.0.1"; }
    SysMsg(COL_SYSTEM, "Connecting to %s:%d ...", ip, port);
    JoinOverlayStart();
    DoConnect(ip, port);
}
static void OnNewPort(const char* t, void*) { MP_CopyStr(s_szNewPort, t, sizeof(s_szNewPort)); }
static char s_szNewPw[MP_PASSWORD_LEN + 1] = "";   // 2.19: optional password, max 16 characters
static char s_szNewLang[MP_LANG_LEN] = "English";   // 2.25: language of the custom server you add
static void OnNewLang(const char* t, void*) { MP_CopyStr(s_szNewLang, t ? t : "", sizeof(s_szNewLang)); MP_CleanLang(s_szNewLang); }
static void OnHostLang(const char* t, void*) { MP_CopyStr(settings.serverLang, t ? t : "", sizeof(settings.serverLang)); MP_CleanLang(settings.serverLang); MP::SaveConfig(); }
// quick language buttons (tap = set, the text field takes anything, e.g. "Filipino / English")
static void LanguageChips(const char* id, char* dst, size_t n, bool save)
{
    static const char* langs[] = { "English", "Filipino", "Tagalog / English", "Bisaya", "Hindi", "Indonesian", "Russian", "Spanish", "Portuguese", "Arabic" };
    ImGui::PushID(id);
    float x = 0, avail = ImGui::GetContentRegionAvail().x;
    for(int i = 0; i < (int)(sizeof(langs) / sizeof(langs[0])); ++i)
    {
        float w = ImGui::CalcTextSize(langs[i]).x + ImGui::GetStyle().FramePadding.x * 2;
        if(i && x + w < avail) ImGui::SameLine(); else if(i) x = 0;
        bool on = !strcasecmp(dst, langs[i]);
        if(on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.85f)), ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
        if(ImGui::Button(langs[i], ImVec2(0, UI::S(54)))) { MP_CopyStr(dst, langs[i], n); if(save) MP::SaveConfig(); }
        if(on) ImGui::PopStyleColor(2);
        x += w + ImGui::GetStyle().ItemSpacing.x;
    }
    ImGui::PopID();
}
static void OnNewPw(const char* t, void*) { MP_CopyStr(s_szNewPw, t, sizeof(s_szNewPw)); }
static void OnHostPw(const char* t, void*) { MP_CopyStr(MP::settings.serverPassword, t, sizeof(MP::settings.serverPassword)); MP::SaveConfig(); }

// Players online per region: every live server (internet lobby, fresh every frame + Wi-Fi), plus
// the server you host and the one you're playing on, even when the search hasn't listed them.
static void RegionPlayers(const std::vector<LanServer>& nearby, int out[REGION_COUNT], int servers[REGION_COUNT])
{
    struct E { int region, players; };
    std::map<std::string, E> all;
    auto key = [](const char* name, int port) { std::string k = name; for(auto& c : k) c = (char)tolower((unsigned char)c); return k + "|" + std::to_string(port); };
    auto put = [&](const char* name, int port, int region, int players)
    {
        if(region < 0 || region >= REGION_COUNT) region = REGION_ASIA;
        auto it = all.find(key(name, port));
        if(it == all.end()) all[key(name, port)] = { region, players };
        else if(players > it->second.players) it->second.players = players;
    };
    for(auto& sv : nearby) if(!sv.token) put(sv.name, sv.port, sv.region, sv.players);   // Wi-Fi
    for(auto& sv : Online::Snapshot()) put(sv.name, sv.port, sv.region, sv.players);     // internet (live)
    if(s_Server.IsRunning()) put(s_Server.GetName(), s_Server.GetPort(), s_Server.GetRegion(), s_Server.GetPlayerCount());
    if(client.GetState() == NET_CONNECTED && s_szCurServer[0])
    {
        auto it = all.find(key(s_szCurServer, client.GetPort()));
        if(it != all.end()) { if(OnlineCount() > it->second.players) it->second.players = OnlineCount(); }
        else put(s_szCurServer, client.GetPort(), settings.region, OnlineCount());
    }
    for(int r = 0; r < REGION_COUNT; ++r) out[r] = servers[r] = 0;
    for(auto& kv : all) { out[kv.second.region] += kv.second.players; servers[kv.second.region]++; }
}

static void RegionPicker(const std::vector<LanServer>& nearby)
{
    int players[REGION_COUNT], servers[REGION_COUNT];
    RegionPlayers(nearby, players, servers);
    ImGui::TextUnformatted("Region");
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * (REGION_COUNT - 1)) / REGION_COUNT;
    for(int r = 0; r < REGION_COUNT; ++r)
    {
        if(r) ImGui::SameLine();
        bool sel = settings.region == r;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImGui::ColorConvertU32ToFloat4(UI::kAccentDark) : ImVec4(0.16f, 0.16f, 0.16f, 1));
        char lbl[80];
        int ms = Online::RegionPingMs(r);   // 2.20: regional broker ping
        if(ms >= 0) snprintf(lbl, sizeof(lbl), "%s  %d ms\n%d online##rg%d", g_MPRegionNames[r], ms, players[r], r);
        else snprintf(lbl, sizeof(lbl), "%s\n%d online##rg%d", g_MPRegionNames[r], players[r], r);   // players
        if(ImGui::Button(lbl, ImVec2(w, UI::S(76))) && !sel)
        {
            settings.region = r; s_nSelServer = -1;
            settings.regionPicked = true;   // 2.20
            s_Server.SetRegion(r);
            SaveConfig();
        }
        ImGui::PopStyleColor();
    }
    if(Online::HasRegionalBrokers())   // 2.20: own broker per region
    {
        if(FloatMenu::ToggleSwitch("Lobby: lowest ping region", &settings.lobbyAuto)) SaveConfig();
        int cr = Online::CurrentRegion();
        if(cr >= 0) ImGui::TextDisabled("Lobby broker: %s%s. Servers of other regions are joined through their own broker.", g_MPRegionNames[cr], settings.lobbyAuto ? " (auto)" : "");
        else ImGui::TextDisabled("Lobby broker: public (regional brokers not reachable)");
    }
}

// One list row: info on the left, an action button (Join / Connect / Leave) on the right.
// Returns true when the action button was tapped.
// 2.24: tap the icon = next icon (Default -> 1 -> 2 ... -> 250 -> Default). Long list, so also - / + buttons.
static void ServerIconPicker()
{
    ImGui::TextUnformatted("Server icon (tap it for the next one)");
    const float sz = UI::S(96);
    ImVec2 uv(1, 1);
    void* tex = HUD::ServerIcon(settings.serverIcon, &uv);
    bool next = false;
    ImGui::PushID("srvicon");
    if(tex) next = ImGui::ImageButton("##ic", (ImTextureID)tex, ImVec2(sz, sz), ImVec2(0, 0), uv);
    else    next = ImGui::Button(settings.serverIcon ? "?" : "MP", ImVec2(sz + UI::S(8), sz + UI::S(8)));
    ImGui::SameLine();
    ImGui::BeginGroup();
    if(settings.serverIcon == 0) ImGui::TextUnformatted("Default (main_df_icon.png)");
    else ImGui::TextUnformatted("Custom icon");   // 2.32: no numbers shown
    bool prev = ImGui::Button(" - ", ImVec2(UI::S(70), UI::S(56)));
    ImGui::SameLine();
    bool plus = ImGui::Button(" + ", ImVec2(UI::S(70), UI::S(56)));
    ImGui::SameLine();
    bool def = ImGui::Button("Default", ImVec2(UI::S(130), UI::S(56)));
    ImGui::EndGroup();
    ImGui::PopID();
    int v = settings.serverIcon;
    if(next || plus) v = v >= MP_ICON_MAX ? 0 : v + 1;
    if(prev) v = v <= 0 ? MP_ICON_MAX : v - 1;
    if(def) v = 0;
    if(v != settings.serverIcon) { settings.serverIcon = v; SaveConfig(); }
    if(IsHostingDefault()) ImGui::TextDisabled("The default MP LAN Server always shows the default icon.");
    ImGui::TextDisabled("Your own pictures: files/MP/icon_1.png ... icon_250.png (max 256x256).");
}

static bool ServerRow(const char* id, const char* title, const char* info, ImVec4 infoCol,
                      const char* action, ImU32 actionCol, bool actionEnabled, bool selected, bool* rowTapped, int icon = -1)
{
    const float h = UI::S(84), bw = UI::S(170), sp = ImGui::GetStyle().ItemSpacing.x;
    float w = ImGui::GetContentRegionAvail().x - bw - sp;
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? ImVec4(1, 1, 1, 0.28f) : ImVec4(0.06f, 0.06f, 0.06f, 0.55f));
    bool tapped = ImGui::Button("##row", ImVec2(w, h));
    ImGui::PopStyleColor();
    if(rowTapped) *rowTapped = tapped;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float lh = ImGui::GetTextLineHeight();
    dl->PushClipRect(p, ImVec2(p.x + w, p.y + h), true);
    float tx = p.x + UI::S(14);
    if(icon >= 0)   // 2.24: server icon on the left (icon_N.png, falls back to main_df_icon.png)
    {
        ImVec2 uv(1, 1);
        if(void* tex = HUD::ServerIcon(icon, &uv))
        {
            float is = h - UI::S(16);
            ImVec2 a(p.x + UI::S(10), p.y + UI::S(8));
            dl->AddImageRounded((ImTextureID)tex, a, ImVec2(a.x + is, a.y + is), ImVec2(0, 0), uv, IM_COL32_WHITE, UI::S(10));
            tx = a.x + is + UI::S(12);
        }
    }
    dl->AddText(ImVec2(tx, p.y + h * 0.5f - lh - UI::S(2)), IM_COL32(255, 255, 255, 255), title);
    dl->AddText(ImVec2(tx, p.y + h * 0.5f + UI::S(2)), ImGui::ColorConvertFloat4ToU32(infoCol), info);
    dl->PopClipRect();
    ImGui::SameLine();
    if(!actionEnabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(actionCol));
    bool act = ImGui::Button(action, ImVec2(bw, h));
    ImGui::PopStyleColor();
    if(!actionEnabled) ImGui::EndDisabled();
    ImGui::PopID();
    return act && actionEnabled;
}

static const ImU32  kJoinCol = IM_COL32(255, 255, 255, 235), kLeaveCol = IM_COL32(150, 30, 30, 255), kWaitCol = IM_COL32(240, 200, 60, 255);

static bool IsConnectedTo(const char* host, int port)
{
    if(client.GetState() == NET_DISCONNECTED || client.GetPort() != port) return false;
    return !strcmp(client.GetHost(), host);
}

static void DefaultServerCard(const std::vector<LanServer>& nearby)
{
    char info[128];
    ImVec4 col = kGreenTxt;
    int found = -1;
    for(int i = 0; i < (int)nearby.size(); ++i)
        if(IsDefaultServer(nearby[i].name, nearby[i].port)) { found = i; break; }
    if(!s_bHasNetwork)                       { snprintf(info, sizeof(info), "Offline: turn on Wi-Fi or mobile data"); col = kRedTxt; }
    else if(IsOnDefault() && IsConnected())  snprintf(info, sizeof(info), "Connected  |  %d/%d  |  %d ms", OnlineCount(), OnlineMax(), MP::DisplayPing());
    else if(IsHostingDefault())              snprintf(info, sizeof(info), "Online (you host it)  |  %d/%d", s_Server.GetPlayerCount(), s_Server.GetMaxPlayers());
    else if(found >= 0)                      snprintf(info, sizeof(info), "Online at %s  |  %d/%d  |  %d ms", nearby[found].token ? "internet" : nearby[found].ip, nearby[found].players, nearby[found].maxPlayers, nearby[found].ping);
    else                                     snprintf(info, sizeof(info), "Always online  |  tap Connect");

    const char* act = "Connect"; ImU32 ac = UI::kAccentDark;
    if(IsOnDefault()) { act = client.GetState() == NET_CONNECTING ? "Cancel" : "Leave"; ac = kLeaveCol; }
    else if(IsBusy() && s_nJob == JOB_CONNECT) { act = "Search..."; ac = kWaitCol; }

    char title[80]; snprintf(title, sizeof(title), "%s  (default)  -  port %d", MP_DEFAULT_SERVER_NAME, MP_DEFAULT_PORT);
    if(ServerRow("default", title, info, col, act, ac, s_bHasNetwork || IsOnDefault(), true, nullptr, 0))
    {
        if(IsOnDefault()) MP::ToggleConnect();
        else { LeaveServer(nullptr); MP::ToggleConnect(); }
    }
}

// Internet lobby state + why it fails (shown in the Servers and Connection tabs).
static void LobbyStatus(const std::vector<LanServer>& nearby)
{
    int net = 0, wifi = 0;
    for(auto& sv : nearby) (sv.token ? net : wifi)++;
    if(!s_bHasNetwork)
        ImGui::TextColored(kRedTxt, "Offline: turn on Wi-Fi or mobile data.");
    else if(Online::IsConnected())
        ImGui::TextColored(kGreenTxt, "Internet lobby: ONLINE via %s (%d ms)  |  %d internet + %d Wi-Fi servers", Online::BrokerName(), Online::BrokerRttMs(), net, wifi);
    else
        ImGui::TextColored(kYellowTxt, "Internet lobby: NOT connected - other networks can't see servers yet");
    static bool showDiag = false;
    if(ImGui::Button(showDiag ? "Hide lobby details" : "Lobby details", ImVec2(-FLT_MIN, UI::S(64)))) showDiag = !showDiag;   // 2.18.3: normal size
    if(showDiag || (s_bHasNetwork && !Online::IsConnected()))
    {
        std::vector<std::string> lines; Online::GetDiagnostics(lines);
        for(auto& l : lines) ImGui::TextDisabled("%s", l.c_str());
    }
}

// 2.16.1: permission switches for custom servers you create / host. Always visible (2.16 only
// showed them after the server was already running). Changes apply at once if you host.
static void PermissionSwitches(const char* where)
{
    ImGui::PushID(where);   // shown in two tabs: keep the switches' ids apart
    ImGui::TextUnformatted("Custom server permissions");
    if(FloatMenu::ToggleSwitch("Mission sync with somebody", &settings.allowMissionPair)) { ApplyServerPerms(); SaveConfig(); }
    ImGui::TextDisabled(settings.allowMissionPair ? "On: two players can sync up: they see each other on missions and share their mission markers."
                                                  : "Off: everyone is alone on missions; running mission syncs end.");
    // 3.00: Cheat Menu permission (libCheatMenu.so / libCheatMenu64.so)
    if(FloatMenu::ToggleSwitch("Cheat Menu", &settings.allowCheatMenu))
    {
        if(s_Server.IsRunning() && !IsHostingDefault() && !s_Server.IsBcwServer())   // 3.10: the server you host now keeps it
        { s_bRunCheatMenu = settings.allowCheatMenu; SrvCmPut(s_Server.GetName(), s_Server.GetPort(), s_bRunCheatMenu); }
        ApplyServerPerms(); SaveConfig();
    }
    ImGui::TextDisabled(settings.allowCheatMenu ? "On: players may use Cheat Menu (libCheatMenu.so / libCheatMenu64.so) on your server."
                                                : "Off: players with Cheat Menu (libCheatMenu.so / libCheatMenu64.so) can't join.");
    ImGui::TextDisabled("Never on the default server. \"Disable cheats\" (Settings > Cheats) switches it off too.");
    if(IsHostingDefault()) ImGui::TextDisabled("(The default server always allows Mission sync.)");
    else if(s_Server.IsRunning()) ImGui::TextColored(kGreenTxt, "Applied to \"%s\" now.", s_Server.GetName());
    else ImGui::TextDisabled("Used for every custom server you create or host.");
    ImGui::PopID();
}

// 2.35: Settings > Cheats. One switch, only the host (the phone running the server - on the default
// server: the first player who joined) can change it; it is sent to everyone as a server permission.
static void SectionCheats(void*)
{
    // 2.37: default server -> only [ADMIN] SENJI; your own custom server -> you (the host)
    bool admin = MP::IsDefaultAdmin();
    bool host = (s_Server.IsRunning() && !MP::IsHostingDefault()) || admin;
    bool on = (host && !admin) ? settings.disableCheats : MP::CheatsBlocked();
    ImGui::TextUnformatted("Server cheats");
    if(!host) ImGui::BeginDisabled();
    if(FloatMenu::ToggleSwitch("Disable cheats", &on) && host)
    {
        if(admin) SendAdminCmd(ADMIN_CHEATS, -1, on ? 1 : 0, "");
        else
        {
            settings.disableCheats = on; ApplyServerPerms(); SaveConfig();
            SysMsg(on ? COL_YELLOW : COL_SYSTEM, on ? "*** Cheats disabled for everyone on your server (MP cheats + CLEO menu)." : "*** Cheats allowed again on your server.");
        }
    }
    if(!host) ImGui::EndDisabled();
    ImGui::TextDisabled(on ? "On: nobody can use MP cheat commands (/fly, /god, /tp, /wh, ...) or open the CLEO menu / CLEO touch menus (swipe)."
                           : "Off: MP cheat commands and the CLEO menu work as usual.");
    if(on && !CleoBlock::Available()) ImGui::TextDisabled("(CLEO menu: no CLEO Android found on this phone - only MP cheats are blocked here.)");
    if(admin) ImGui::TextColored(kGreenTxt, "You are [ADMIN] SENJI: this applies to every player on the default server (until everybody left).");
    else if(host) ImGui::TextColored(kGreenTxt, "You are the host of \"%s\": this applies to every player.", s_Server.GetName());
    else if(MP::IsOnDefault()) ImGui::TextColored(kYellowTxt, "On the default server only the administrator ([ADMIN] SENJI) can change this.");
    else if(client.GetState() == NET_CONNECTED) ImGui::TextColored(kYellowTxt, "Only the host of this server can change this.");
    else ImGui::TextDisabled("Host your own server to change this. On the default server only [ADMIN] SENJI can.");
}

static void SectionServers(void*)
{
    // list + queries live on the render thread only (no locking needed)
    ServerList::Poll();

    // auto refresh every 10 s while the list is visible
    if(MP_NowMs() - ServerList::LastRefreshMs() > 10000) ServerList::Refresh();

    std::vector<LanServer> nearby;
    {
        static uint32_t seenVersion = 0;
        uint32_t ver;
        { std::lock_guard<std::mutex> lk(s_LanMutex); nearby = s_Nearby; ver = s_nNearbyVersion; }
        if(ver != seenVersion)
        {
            seenVersion = ver;   // found servers are only shown (with Join), never added to your list
        }
        if(s_bHasNetwork && !IsSearching() && MP_NowMs() - s_nNearbyTime > 15000) { s_nNearbyTime = MP_NowMs(); StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
    }

    RegionPicker(nearby);
    LobbyStatus(nearby);
    ImGui::Separator();

    // ---- default server: always on top, always available ----
    ImGui::TextUnformatted("Default server");
    DefaultServerCard(nearby);
    ImGui::Separator();

    // ---- custom servers found on this Wi-Fi / hotspot (any region) ----
    // every live server is listed here, the default one included, so you can always see who is online
    auto listed = [](const LanServer& sv) { return false; };
    ImGui::Text("Online now - %d server%s (internet + Wi-Fi)%s", (int)nearby.size(), nearby.size() == 1 ? "" : "s", IsSearching() ? "  (searching...)" : "");
    if(nearby.empty())
        ImGui::TextDisabled(IsSearching() ? "Searching..." : "No other server online right now. Tap Connect to host the default one.");
    {
        for(int i = 0; i < (int)nearby.size(); ++i)
        {
            const LanServer& sv = nearby[i];
            if(listed(sv)) continue;
            char id[16], title[80], info[128];
            snprintf(id, sizeof(id), "lan%d", i);
            snprintf(title, sizeof(title), "%s  [%s]", sv.name, sv.region >= 0 && sv.region < REGION_COUNT ? g_MPRegionNames[sv.region] : "?");
            if(sv.token) snprintf(info, sizeof(info), "internet  |  %d/%d  |  ~%d ms", sv.players, sv.maxPlayers, sv.ping);
            else         snprintf(info, sizeof(info), "on Wi-Fi  |  %d/%d  |  %d ms", sv.players, sv.maxPlayers, sv.ping);   // 2.16: IP hidden
            bool on = IsConnectedTo(sv.ip, sv.port);
            if(ServerRow(id, title, info, kGreenTxt, on ? "Leave" : "Join", on ? kLeaveCol : kJoinCol, true, false, nullptr,
                         IsDefaultServer(sv.name, sv.port) ? 0 : sv.icon))
            {
                if(on) LeaveServer("Left the server.");
                else if(IsDefaultServer(sv.name, sv.port)) { if(IsOnDefault()) SysMsg(COL_SYSTEM, "You are already connected to %s.", MP_DEFAULT_SERVER_NAME); else { LeaveServer(nullptr); MP::ToggleConnect(); } }   // 3.10
                else MP::JoinServer(sv.ip, sv.port, sv.name);
            }
        }
        ImGui::Separator();
    }

    // ---- custom servers (saved, max 99) ----
    ImGui::Text("Custom servers in %s  (%d / %d)", g_MPRegionNames[settings.region], ServerList::Count(), MP_MAX_SERVERS);
    int shown = 0;
    for(int i = 0; i < ServerList::Count(); ++i)
    {
        ServerEntry* e = ServerList::Get(i);
        if(!e || e->region != settings.region) continue;
        ++shown;
        char id[16], title[80], info[128];
        snprintf(id, sizeof(id), "sv%d", i);
        snprintf(title, sizeof(title), "%s%s", e->name, (e->password[0] || e->locked) ? "   [password]" : "");
        // live state: the address answered, a phone on the Wi-Fi/hotspot runs it, or you host it
        const LanServer* lan = nullptr;
        for(auto& sv : nearby) if(sv.port == e->port && !strcasecmp(sv.name, e->name)) { lan = &sv; break; }
        bool mine = s_Server.IsRunning() && s_Server.GetPort() == e->port && !strcasecmp(s_Server.GetName(), e->name);
        bool on = client.GetState() != NET_DISCONNECTED && client.GetPort() == e->port &&
                  (IsConnectedTo(e->host, e->port) || (lan && !strcmp(client.GetHost(), lan->ip)) ||
                   (mine && IsLocalHost(client.GetHost())) || (client.GetServerName()[0] && !strcasecmp(client.GetServerName(), e->name)));
        bool joining = IsBusy() && s_nJob == JOB_JOIN && s_nJobPort == e->port && !strcasecmp(s_szJobName, e->name);
        ImVec4 col = kGreenTxt;
        // 2.16: a custom server's IP / host is never shown (only its name)
        if(on && IsConnected())  snprintf(info, sizeof(info), "IP hidden  |  connected  |  %d/%d  |  %d ms", OnlineCount(), OnlineMax(), MP::DisplayPing());
        else if(mine)            snprintf(info, sizeof(info), "IP hidden  |  online (you host it)  |  %d/%d", s_Server.GetPlayerCount(), s_Server.GetMaxPlayers());
        else if(e->online)       snprintf(info, sizeof(info), "IP hidden  |  online %d/%d  |  %d ms", e->players, e->maxPlayers, e->ping);
        else if(lan)             snprintf(info, sizeof(info), "IP hidden  |  online %s %d/%d  |  %d ms", lan->token ? "(internet)" : "on Wi-Fi", lan->players, lan->maxPlayers, lan->ping);
        else if(!s_bHasNetwork)  { snprintf(info, sizeof(info), "IP hidden  |  you are offline (Wi-Fi/data off)"); col = kRedTxt; }
        else                     snprintf(info, sizeof(info), "IP hidden  |  always active  |  tap Join");

        const char* act = "Join"; ImU32 ac = kJoinCol;
        if(on) { act = client.GetState() == NET_CONNECTING ? "Cancel" : "Leave"; ac = kLeaveCol; }
        else if(joining) { act = "Joining..."; ac = kWaitCol; }
        bool enabled = on || (s_bHasNetwork && !joining);
        bool rowTap = false;
        int icon = mine ? settings.serverIcon : (e->online ? e->icon : (lan ? lan->icon : 0));   // 2.24
        if(ServerRow(id, title, info, col, act, ac, enabled, i == s_nSelServer, &rowTap, icon))
        {
            if(on) LeaveServer("Left the server.");
            else MP::JoinServer(e->host, e->port, e->name);
        }
        if(rowTap) s_nSelServer = (s_nSelServer == i) ? -1 : i;
    }
    if(!shown) ImGui::TextDisabled("No custom servers in this region yet. Add one below.");

    ImGui::Spacing();
    ServerEntry* sel = ServerList::Get(s_nSelServer);
    if(sel && sel->region != settings.region) { sel = nullptr; s_nSelServer = -1; }
    float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    if(ImGui::Button("Refresh", ImVec2(half, UI::S(64)))) { ServerList::Refresh(); if(s_bHasNetwork) StartJob(JOB_SCAN, false, "", MP_DEFAULT_PORT); }
    ImGui::SameLine();
    if(!sel) ImGui::BeginDisabled();
    if(ImGui::Button("Delete selected", ImVec2(half, UI::S(64))) && sel) { ServerList::Remove(s_nSelServer); s_nSelServer = -1; }
    if(!sel) ImGui::EndDisabled();
    ImGui::TextDisabled("Tap a server to select it, tap Join to play on it.");
    ImGui::TextDisabled("Custom servers are always active: if nobody runs it, Join hosts it.");

}

// 2.25: "Add server" page in the launcher (add a custom server + Local IP)
// 2.37: one page for everything about a custom server. The "Hosted" page is gone: its server name,
// port, password, language, icon, max players and permissions were the same fields as here, so now
// you fill them in once and either Add the server to your list (with a Host / IP) or Host it on
// this phone (Host / IP left empty).
static bool s_bAddPrefilled = false;
static void AddFormPrefill()
{
    if(s_bAddPrefilled) return;
    s_bAddPrefilled = true;
    if(!s_szNewPort[0] && settings.port > 0 && settings.port != MP_DEFAULT_PORT) snprintf(s_szNewPort, sizeof(s_szNewPort), "%d", settings.port);
    if(!s_szNewLang[0] && settings.serverLang[0]) MP_CopyStr(s_szNewLang, settings.serverLang, sizeof(s_szNewLang));
}
static void HostFromForm(bool on)
{
    if(on)
    {   // the same fields become your hosted server's settings
        if(s_szNewName[0]) MP_CopyStr(settings.serverName, s_szNewName, sizeof(settings.serverName));
        if(atoi(s_szNewPort) > 0 && atoi(s_szNewPort) <= 65535) settings.port = atoi(s_szNewPort);
        MP_CopyStr(settings.serverPassword, s_szNewPw, sizeof(settings.serverPassword));
        if(s_szNewLang[0]) MP_CopyStr(settings.serverLang, s_szNewLang, sizeof(settings.serverLang));
        SaveConfig();
    }
    MP::SetHostLan(on, client.GetState() == NET_DISCONNECTED);   // 2.19.6: join it right away
}

// 3.02: Add server > Gamemodes. The default server always has all of them; a custom server you
// create starts with none, and you turn on the ones you want. Sent to the players (PKT_PERMS).
static void GamemodeSwitches()
{
    ImGui::PushID("gamemodes");
    ImGui::Spacing();
    ImGui::TextUnformatted("Gamemodes");
    ImGui::TextDisabled("The default server has all of them. A custom server starts with none: turn on the ones you want.");
    ImGui::TextDisabled("Map icons and Chat bubbles are each player's own switches: Settings > Display / Chat.");
    bool changed = false;
    for(int i = 0; i < MP_GAMEMODE_COUNT; ++i)
    {
        const GameModeInfo& g = kGameModes[i];
        bool on = (settings.hostModes & g.bit) != 0;
        if(FloatMenu::ToggleSwitch(g.name, &on)) { settings.hostModes = on ? (settings.hostModes | g.bit) : (settings.hostModes & ~g.bit); changed = true; }
        ImGui::TextDisabled("%s", g.desc);
    }
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
    uint16_t every = 0; for(int i = 0; i < MP_GAMEMODE_COUNT; ++i) every |= kGameModes[i].bit;
    if(ImGui::Button("All on", ImVec2(w, UI::S(70)))) { settings.hostModes = GM_ALL; changed = true; }
    ImGui::SameLine();
    if(ImGui::Button("All off", ImVec2(w, UI::S(70)))) { settings.hostModes = 0; changed = true; }
    if(changed) { settings.hostModes &= GM_ALL; SaveConfig(); }   // 3.03: only the choice for a NEW server
    ImGui::TextDisabled("Only for a NEW server you create (Host). Servers that already exist keep their gamemodes - the one you are on is not changed.");
    if(s_Server.IsRunning())
    {
        std::string run = GameModeList(s_Server.GetModes(), ", ", false);
        ImGui::TextDisabled("This phone runs \"%s\": %s (fixed).", s_Server.GetName(), IsHostingDefault() ? "every gamemode" : run.empty() ? "no gamemodes" : run.c_str());
    }
    std::string cl = GameModeList((uint16_t)settings.hostModes, ",", true);
    ImGui::TextDisabled("24/7 server (mp_server): --gamemodes %s", cl.empty() ? "none" : ((settings.hostModes & every) == every ? "all" : cl.c_str()));
    ImGui::PopID();
}

static void SectionAddServer(void*)
{
    AddFormPrefill();
    bool full = ServerList::Count() >= MP_MAX_SERVERS;
    bool hosting = s_Server.IsRunning();
    ImGui::Text("Custom server  -  %s", g_MPRegionNames[settings.region]);
    FloatMenu::TextField("Server name", s_szNewName, "Server name", 31, OnNewName);
    FloatMenu::TextField("Host / IP  (empty = host it on this phone)", s_szNewHost, "e.g. ggwar or net.ggwar.org", 63, OnNewHost, nullptr, false, true);   // 2.16: masked
    FloatMenu::TextField("Port", s_szNewPort, "Port", 6, OnNewPort, nullptr, true);
    FloatMenu::TextField("Password (optional, max 16)", s_szNewPw, "Password (max 16)", MP_PASSWORD_LEN, OnNewPw, nullptr, false, true);   // 2.19
    FloatMenu::TextField("Language", s_szNewLang, "Language (e.g. English)", MP_LANG_LEN - 1, OnNewLang);   // 2.25
    LanguageChips("newlang", s_szNewLang, sizeof(s_szNewLang), false);
    ServerIconPicker();   // 2.24 (shown in the list when you host)
    ImGui::TextUnformatted("Max players");
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##maxpl", &settings.maxPlayers, 1, MP_MAX_PLAYERS, "%d players"))
        s_Server.SetMaxPlayers(settings.maxPlayers);
    if(ImGui::IsItemDeactivatedAfterEdit()) SaveConfig();
    PermissionSwitches("add");   // 2.37: once (was on Hosted + here)
    GamemodeSwitches();          // 3.02
    ImGui::Separator();

    bool hasHost = s_szNewHost[0] != 0;
    if(full || !hasHost) ImGui::BeginDisabled();
    char addLbl[64]; snprintf(addLbl, sizeof(addLbl), full ? "Server list full (%d/%d)" : "Add server  (%d/%d)", ServerList::Count(), MP_MAX_SERVERS);
    if(FloatMenu::BigButton(addLbl))
    {
        // accept "ggwar", "net.ggwar.org", "http://net.ggwar.org/" or an IP
        char* h = s_szNewHost;
        if(!strncasecmp(h, "http://", 7)) memmove(h, h + 7, strlen(h + 7) + 1);
        else if(!strncasecmp(h, "https://", 8)) memmove(h, h + 8, strlen(h + 8) + 1);
        while(*h == ' ') memmove(h, h + 1, strlen(h));
        for(char* c = h; *c; ++c) if(*c == '/' || *c == ' ') { *c = 0; break; }
        if(MP_IsBcwServerName(s_szNewName) || atoi(s_szNewPort) == MP_BCW_PORT)   // 3.04
            SysMsg(COL_YELLOW, "Brief Case War is a private gamemode server - it is always in the Internet list, just tap it.");
        else if(IsDefaultServer(s_szNewName, atoi(s_szNewPort)) || IsLocalHost(s_szNewHost))
            SysMsg(COL_YELLOW, "That is the default server - it is always on top, just tap Connect.");
        else if(atoi(s_szNewPort) == MP_DEFAULT_PORT)
            SysMsg(COL_RED, "Port %d is for the default server. Use %d or higher for a custom server.", MP_DEFAULT_PORT, MP_DEFAULT_PORT + 1);
        else if(ServerList::FindByNamePort(s_szNewName[0] ? s_szNewName : s_szNewHost, atoi(s_szNewPort)) >= 0 ||
                ServerList::FindByHostPort(s_szNewHost, atoi(s_szNewPort)) >= 0)
            SysMsg(COL_YELLOW, "That server is already in your list.");
        else if(ServerList::Add(s_szNewName, s_szNewHost, atoi(s_szNewPort), settings.region, s_szNewPw, s_szNewLang))
        {
            SysMsg(COL_GREEN, "Added server \"%s\" (%s). Its IP stays hidden.", s_szNewName[0] ? s_szNewName : "custom", g_MPRegionNames[settings.region]);
            s_szNewName[0] = s_szNewHost[0] = s_szNewPw[0] = 0;
            ServerList::Refresh();
        }
        else SysMsg(COL_RED, "Enter a host/IP and a port (1-65535).");
    }
    if(full || !hasHost) ImGui::EndDisabled();

    // host the same server on this phone (was the "Hosted" page)
    bool lan = hosting;
    if(FloatMenu::ToggleSwitch("Host this server on my phone", &lan)) HostFromForm(lan);
    if(hosting)
    {
        ImGui::TextColored(kGreenTxt, "Hosting \"%s\"  (IP hidden, %d/%d online)", s_Server.GetName(), s_Server.GetPlayerCount(), s_Server.GetMaxPlayers());
        if(Online::IsConnected()) ImGui::TextColored(kGreenTxt, "Listed in the internet lobby: players on mobile data can join.");
        else ImGui::TextColored(kYellowTxt, "Not in the internet lobby yet (Wi-Fi/hotspot players only).");
        ImGui::TextDisabled("Stays online when you disconnect. If you leave the game, another player takes over.");
        if(!IsHostingDefault() && !IsConnectedTo("127.0.0.1", s_Server.GetPort()) &&
           FloatMenu::BigButton(client.GetState() == NET_DISCONNECTED ? "Join my server" : "Leave + join my server"))
            MP::JoinServer("127.0.0.1", s_Server.GetPort(), s_Server.GetName());
    }
    else if(IsBusy() && s_nJob == JOB_HOST)
        ImGui::TextColored(kYellowTxt, "Checking online + Wi-Fi for \"%s\"...", settings.serverName);
    else
        ImGui::TextDisabled(hasHost ? "Add server = save it to your list.  Host = run it on this phone with the same name, port and password."
                                    : "No Host / IP: tap Host to run it on this phone. If it already runs on another phone, you join it.");

    // 2.19.1: Local IP - join a phone / PC on the same Wi-Fi (or a public IP) directly
    ImGui::Separator();
    ImGui::TextUnformatted("Local IP");
    if(!s_szLocalPort[0]) snprintf(s_szLocalPort, sizeof(s_szLocalPort), "%d", settings.port);
    {
        const char* me = LanAddress();
        if(me && *me) ImGui::TextDisabled("Your local IP: %s  (port %d when you host)", me, settings.port);
    }
    FloatMenu::TextField("IP", s_szLocalIp, "e.g. 192.168.1.5", 63, OnLocalIp, nullptr, false);
    FloatMenu::TextField("Port##lip", s_szLocalPort, "Port", 6, OnLocalPort, nullptr, true);
    {
        bool onIt = client.GetState() != NET_DISCONNECTED && !strcmp(client.GetHost(), s_szLocalIp) && client.GetPort() == atoi(s_szLocalPort);
        if(FloatMenu::BigButton(onIt ? (client.GetState() == NET_CONNECTING ? "Cancel##lip" : "Disconnect##lip") : "Connect##lip"))
        {
            if(onIt) LeaveServer("Left the server.");
            else ConnectLocalIp(s_szLocalIp, atoi(s_szLocalPort));
        }
    }
    ImGui::TextDisabled("Same Wi-Fi / hotspot: type the host's local IP and port, then Connect.");
}

static void SectionConnection(void*)
{
    bool idle = client.GetState() == NET_DISCONNECTED;
    if(!idle) ImGui::BeginDisabled();
    FloatMenu::TextField("Name", settings.name, "Name", MP_NAME_LEN, OnName);
    if(MP::IsLocalAdmin())   // 2.37: live preview of the admin look (RGB rank + flowing name)
    {
        float fs = UI::S(46), h = fs * 3.2f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(10, 10, 14, 220), UI::S(10));
        const char* nm = "[ADMIN] SENJI";
        float tw = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, nm).x;
        UI::NameText(dl, fs, ImVec2(p.x + (w - tw) * 0.5f, p.y + (h - fs) * 0.5f), IM_COL32_WHITE, nm);
        ImGui::Dummy(ImVec2(w, h));
        ImGui::TextColored(kGreenTxt, "administrator.txt OK - this is how everyone sees you (name tag, chat, kill feed).");
    }
    if(!idle) ImGui::EndDisabled();

    // ---- the big button: default server only ----
    ImGui::Spacing();
    char lbl[96]; snprintf(lbl, sizeof(lbl), "Connect  -  %s (default)", MP_DEFAULT_SERVER_NAME);
    ImU32 col = UI::kAccentDark;
    if(client.GetState() == NET_CONNECTING) { snprintf(lbl, sizeof(lbl), "Connecting...  (tap to cancel)"); col = kWaitCol; }
    else if(client.GetState() == NET_CONNECTED) { snprintf(lbl, sizeof(lbl), "Connected  -  tap to Disconnect"); col = kLeaveCol; }
    else if(IsBusy()) { snprintf(lbl, sizeof(lbl), "Searching online + Wi-Fi..."); col = kWaitCol; }
    if(FloatMenu::BigButton(lbl, col)) MP::ToggleConnect();
    if(client.GetState() != NET_DISCONNECTED)
        ImGui::TextColored(kGreenTxt, "On \"%s\"  (%s)", client.GetServerName()[0] ? client.GetServerName() : "...", Online::IsRelayIp(client.GetHost()) ? (client.IsDirect() ? "internet, direct UDP" : "internet") : "IP hidden");
    if(!s_bHasNetwork)
        ImGui::TextColored(kRedTxt, "Offline: turn on Wi-Fi or mobile data to play.");
    else if(s_bTakeoverPending)
        ImGui::TextColored(kYellowTxt, "Host left - reconnecting to \"%s\"...", s_szLostServerName);
    { std::vector<LanServer> nb; { std::lock_guard<std::mutex> lk(s_LanMutex); nb = s_Nearby; } LobbyStatus(nb); }
    ImGui::TextDisabled("Connect = the default server, always online.");
    ImGui::TextDisabled("Custom servers: Servers tab -> Join.");
    ImGui::Text("Players online: %d/%d", MP::OnlineCount(), MP::OnlineMax());
}

// 2.37: SectionHost ("Hosted" page) was merged into SectionAddServer

// 3.00: Settings > Sound
static void SectionSound(void*)
{
    ImGui::TextUnformatted("Sound effects");
    if(FloatMenu::ToggleSwitch("Hitsound effect", &settings.hitSound)) SaveConfig();
    ImGui::TextDisabled(settings.hitSound ? "Enabled: a hit sound plays when you hit a player (guns, fists, melee, drive-by, cars)."
                                          : "Disabled: no hit sound.");
    if(settings.hitSound && ImGui::Button("Test hit sound", ImVec2(UI::S(320), UI::S(64))))
    {
        char snd[512]; snprintf(snd, sizeof(snd), "%s/sound_010.mp3", s_szDataDir);
        if(!Sound::PlayHit(snd, 1.0f)) SysMsg(COL_RED, "sound_010.mp3 is missing from the MP folder (assets/MP).");
    }
}

static void SectionVoice(void*)   // 3.11
{
    bool changed = false;
    ImGui::TextUnformatted("Voice chat");
    changed |= FloatMenu::ToggleSwitch("Voice chat (hear players near you)", &settings.voiceChat);
    ImGui::TextDisabled("Only on a server. Turn your mic on/off with the mic button next to Chat at the top. Players within 40 m hear you: louder up close, quieter further away.");
    ImGui::TextUnformatted("Voice volume");
    int vv = (int)(settings.voiceVolume * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##vvol", &vv, 0, 200, "%d %%")) { settings.voiceVolume = vv / 100.0f; changed = true; }
    ImGui::TextUnformatted("Microphone boost");
    int mg = (int)(settings.micGain * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##mgain", &mg, 50, 300, "%d %%")) { settings.micGain = mg / 100.0f; changed = true; }
    ImGui::TextUnformatted("Voice pitch (how the others hear you)");
    int pc = (int)(settings.voicePitch * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##spitch", &pc, 60, 160, pc == 100 ? "normal" : "%d %%")) { settings.voicePitch = pc / 100.0f; changed = true; }
    if(MP::IsConnected())
    {
        bool on = Voice::MicOn();
        if(FloatMenu::BigButton(on ? "Turn microphone off" : "Turn microphone on")) VoiceMicTap(nullptr);
        if(on) { ImGui::TextUnformatted("Mic level"); ImGui::ProgressBar(Voice::MicLevel(), ImVec2(-1, UI::S(18)), ""); }
    }
    else ImGui::TextDisabled("Connect to a server to turn on your microphone.");
    if(!s_VoiceMuted.empty())
    {
        ImGui::Separator();
        ImGui::Text("Muted players: %d", (int)s_VoiceMuted.size());
        if(FloatMenu::BigButton("Unmute everybody")) s_VoiceMuted.clear();
    }
    if(changed) SaveConfig();
}

// 3.11: the panel under the top bar when the mic pill is tapped
static void VoicePanelTap(void*) { s_bVoicePanel = !s_bVoicePanel; VoiceEnsure(); }
static void RenderVoicePanel(void*)
{
    if(!s_bVoicePanel || FloatMenu::IsModal()) return;
    float cx, y;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(!FloatMenu::BarBottom(&cx, &y)) { cx = ds.x * 0.5f; y = ds.y * 0.12f; }
    float w = ImMin(UI::S(640), ds.x * 0.6f);
    ImGui::SetNextWindowPos(ImVec2(ImClamp(cx - w * 0.5f, UI::S(8), ds.x - w - UI::S(8)), y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, 0), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f);
    if(!ImGui::Begin("Voice chat##MPVoice", &s_bVoicePanel, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
    { ImGui::End(); return; }
    bool changed = false;
    const bool conn = MP::IsConnected();
    float bw = ImGui::GetContentRegionAvail().x;
    // microphone on / off (big button with the mic icon + level)
    {
        bool on = Voice::MicOn();
        ImGui::PushStyleColor(ImGuiCol_Button, !conn ? ImVec4(0.25f, 0.25f, 0.25f, 0.9f) : (on ? ImVec4(0.12f, 0.55f, 0.22f, 0.95f) : ImVec4(0.55f, 0.12f, 0.12f, 0.95f)));
        ImVec2 bp = ImGui::GetCursorScreenPos(); float bh = UI::S(78);
        if(ImGui::Button(!conn ? "          Microphone (connect to a server first)##vmic" : (on ? "          Microphone ON - tap to turn off##vmic" : "          Microphone OFF - tap to turn on##vmic"), ImVec2(bw, bh))) VoiceMicTap(nullptr);
        ImGui::PopStyleColor();
        UI::DrawMic(ImGui::GetWindowDrawList(), ImVec2(bp.x + UI::S(40), bp.y + bh * 0.5f), UI::S(20), IM_COL32_WHITE, !on);
        if(on)
        {
            float lv = Voice::MicLevel();
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + bw, a.y + UI::S(10)), IM_COL32(255, 255, 255, 40), UI::S(4));
            ImGui::GetWindowDrawList()->AddRectFilled(a, ImVec2(a.x + bw * lv, a.y + UI::S(10)), Voice::MicTalking() ? IM_COL32(60, 220, 90, 255) : IM_COL32(200, 200, 200, 200), UI::S(4));
            ImGui::Dummy(ImVec2(bw, UI::S(12)));
            ImGui::TextDisabled(Voice::MicTalking() ? "Sending your voice..." : "Listening - talk and the bar turns green.");
            if(Voice::UsingHelper())
            {
                int hs = Voice::HelperState();
                if(hs == 2) ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Microphone: MP Voice app (working)");
                else
                {
                    ImGui::TextColored(ImVec4(1, 0.85f, 0.3f, 1), "Waiting for the MP Voice app...");
                    if(FloatMenu::BigButton("Open MP Voice")) { s_bHelperOpened = true; s_bHelperWarned = false; Voice::OpenHelperApp(); }
                }
            }
        }
    }
    ImGui::Separator();
    changed |= FloatMenu::ToggleSwitch("Hear voice chat", &settings.voiceChat);
    if(FloatMenu::ToggleSwitch("Mute everybody (only for you)", &s_bVoiceMuteAll) && s_bVoiceMuteAll) Voice::ResetAll();
    ImGui::TextUnformatted("Voice volume (what you hear)");
    int vv = (int)(settings.voiceVolume * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##pvvol", &vv, 0, 200, "%d %%")) { settings.voiceVolume = vv / 100.0f; changed = true; }
    ImGui::TextUnformatted("Microphone boost");
    int mg = (int)(settings.micGain * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##pmgain", &mg, 50, 300, "%d %%")) { settings.micGain = mg / 100.0f; changed = true; }
    ImGui::TextUnformatted("Voice pitch (how the others hear you)");
    int pc = (int)(settings.voicePitch * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##ppitch", &pc, 60, 160, pc == 100 ? "normal" : "%d %%")) { settings.voicePitch = pc / 100.0f; changed = true; }
    {
        static const struct { const char* n; float v; } kP[] = { { "Deep", 0.80f }, { "Normal", 1.0f }, { "High", 1.25f }, { "Chipmunk", 1.5f } };
        float q = (bw - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
        for(int k = 0; k < 4; ++k)
        {
            if(k) ImGui::SameLine();
            bool sel = fabsf(settings.voicePitch - kP[k].v) < 0.01f;
            if(sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.92f)), ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 1));
            if(ImGui::Button(kP[k].n, ImVec2(q, UI::S(58)))) { settings.voicePitch = kP[k].v; changed = true; }
            if(sel) ImGui::PopStyleColor(2);
        }
    }
    {
        bool mon = Voice::Monitor();
        if(!Voice::MicOn()) ImGui::BeginDisabled();
        if(FloatMenu::ToggleSwitch("Hear myself (test the pitch, use earphones)", &mon)) Voice::SetMonitor(mon);
        if(!Voice::MicOn()) ImGui::EndDisabled();
    }
    if(!s_VoiceMuted.empty())
    {
        ImGui::Text("Muted in the player list: %d", (int)s_VoiceMuted.size());
        ImGui::SameLine();
        if(ImGui::SmallButton("Unmute all")) s_VoiceMuted.clear();
    }
    ImGui::Separator();
    ImGui::TextWrapped("Like real life: players right next to you are loud, further away they get quieter, past %d m you can't hear them. Walk closer to hear someone.", (int)MP_VOICE_RANGE);
    ImGui::End();
    if(changed) SaveConfig();
}

static bool ChatPlacementOptions(const char* id);   // 2.22
static bool ChatScaleOptions(const char* id);   // 3.04.5 (below)
static void SectionDisplay(void*)
{
    bool changed = false;
    ImGui::TextUnformatted("Name tag size");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderFloat("##ntsize", &settings.nametagSize, 0.5f, 2.5f, "%.2fx");
    ImGui::TextUnformatted("Name tag distance");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderFloat("##ntdist", &settings.nametagDistance, 10.0f, 150.0f, "%.0f m");
    changed |= FloatMenu::ToggleSwitch("Show my own name tag", &settings.showOwnTag);
    changed |= FloatMenu::ToggleSwitch("Health bars", &settings.showHealthBars);
    changed |= FloatMenu::ToggleSwitch("Player radar icons", &settings.showRadarIcons);
    changed |= FloatMenu::ToggleSwitch("Chat", &settings.showChat);
    changed |= ChatScaleOptions("dispsc");   // 3.04.5
    changed |= FloatMenu::ToggleSwitch("Kill feed", &settings.showKillFeed);
    changed |= FloatMenu::ToggleSwitch("Connection status", &settings.showStatus);
    changed |= FloatMenu::ToggleSwitch("Sync player animations", &settings.syncAnims);
    changed |= FloatMenu::ToggleSwitch("Sync explosions, fire, weapons", &settings.syncEnv);
    changed |= FloatMenu::ToggleSwitch("Sync NPCs (people + traffic)", &settings.syncNpcs);
    changed |= FloatMenu::ToggleSwitch("Open world (cities, shops, food, gyms, garages)", &settings.unlockWorld);   // 2.21
    changed |= FloatMenu::ToggleSwitch("Girlfriend houses (optional)", &settings.girlfriends);
    changed |= FloatMenu::ToggleSwitch("[Race] / [DM] / [Job] tips in the chat", &settings.eventTips);
    changed |= FloatMenu::ToggleSwitch("3D text labels (job / race / derby starts)", &settings.labels3d);
    changed |= FloatMenu::ToggleSwitch("Map icons (jobs, races, derbies, DM)", &settings.mapIcons);   // 3.03: a player setting, not a server gamemode
    changed |= FloatMenu::ToggleSwitch("Actors (people at the job starts)", &settings.actors);
    changed |= FloatMenu::ToggleSwitch("Gang zones on the radar + map", &settings.gangZones);
    NpcStatusLine();
    SyncStatusLines();   // 2.19.6
    changed |= ChatPlacementOptions("disp");   // 2.22: scroll down - chat position, chat box background / label
    static bool dirty = false;
    if(changed) dirty = true;
    if(dirty && !ImGui::IsAnyItemActive()) { dirty = false; SaveConfig(); }
}

// 2.22: chat position + chat box look (shown at the bottom of Display and in Chat)
static bool ChatPlacementOptions(const char* id)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::Separator();
    ImGui::TextUnformatted("Chat position + chat box");
    changed |= FloatMenu::ToggleSwitch("Show chat background (strip behind the lines)", &settings.chatBg);
    changed |= FloatMenu::ToggleSwitch("Show chat box background", &settings.chatBoxBg);
    changed |= FloatMenu::ToggleSwitch("Show chat box label (\"Tap to chat\")", &settings.chatBoxLabel);
    changed |= FloatMenu::ToggleSwitch("Tap the chat box to type", &settings.chatBoxTap);
    if(!settings.chatBoxTap) ImGui::TextDisabled("Only the Chat button opens the keyboard - taps on the box go to the game.");
    ImGui::TextUnformatted("Chat lines position X / Y");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderFloat("##chx", &settings.chatX, 0.0f, 0.8f, "X %.2f");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderFloat("##chy", &settings.chatY, 0.0f, 0.7f, "Y %.2f");
    bool under = settings.chatBoxX < 0 || settings.chatBoxY < 0;
    if(FloatMenu::ToggleSwitch("Chat box right under the chat lines", &under))
    {
        if(under) { settings.chatBoxX = -1; settings.chatBoxY = -1; }
        else      { settings.chatBoxX = settings.chatX; settings.chatBoxY = ImMin(settings.chatY + 0.30f, 0.80f); }
        changed = true;
    }
    if(!under)
    {
        ImGui::TextUnformatted("Chat box position X / Y");
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::SliderFloat("##cbx", &settings.chatBoxX, 0.0f, 0.8f, "X %.2f");
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed |= ImGui::SliderFloat("##cby", &settings.chatBoxY, 0.0f, 0.85f, "Y %.2f");
    }
    if(FloatMenu::BigButton("Reset chat position"))
    {
        settings.chatX = 0.22f; settings.chatY = 0.02f; settings.chatBoxX = -1; settings.chatBoxY = -1;
        settings.chatBg = true; settings.chatBoxBg = true; settings.chatBoxLabel = true; settings.chatBoxTap = true;
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

// 3.04.5: one slider that scales the whole chat (lines, chat box, command suggestions) + presets
static bool ChatScaleOptions(const char* id)
{
    bool changed = false;
    ImGui::PushID(id);
    ImGui::TextUnformatted("Chat scale (chat lines + chat box)");
    int sp = (int)(settings.chatScale * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##csc", &sp, 50, 200, "%d %%")) { settings.chatScale = sp / 100.0f; changed = true; }
    static const int kPre[] = { 75, 100, 130, 160 };
    static const char* const kPreName[] = { "Small", "Normal", "Big", "Huge" };
    float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3) / 4;
    for(int k = 0; k < 4; ++k)
    {
        if(k) ImGui::SameLine();
        char b[32]; snprintf(b, sizeof(b), "%s %d%%##cp%d", kPreName[k], kPre[k], k);
        bool on = sp == kPre[k];
        if(on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.12f, 0.12f, 1));
        if(ImGui::Button(b, ImVec2(bw, UI::S(64)))) { settings.chatScale = kPre[k] / 100.0f; changed = true; }
        if(on) ImGui::PopStyleColor();
    }
    // live preview line at the chosen size
    float fs = UI::S(27.0f) * settings.chatSize * settings.chatScale;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(1, fs + UI::S(14)));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + fs + UI::S(10)), IM_COL32(0, 0, 0, 120), UI::S(6));
    UI::OutlinedText(dl, fs, ImVec2(p.x + UI::S(8), p.y + UI::S(5)), IM_COL32(255, 200, 60, 255), "Player: preview of the chat size");
    ImGui::PopID();
    if(changed) ClampChatSettings();
    return changed;
}
// 2.19.6: the chat layout, set in game (same as /pagesize /fontsize /chatwidth /chatfade /chatbg)
static void SectionChat(void*)
{
    bool changed = false;
    changed |= FloatMenu::ToggleSwitch("Chat timestamps [HH:MM:SS]", &settings.chatTimestamps);   // 3.11
    ImGui::TextDisabled(settings.chatTimestamps ? "On: every chat line starts with the time it arrived, like [18:02:48]." : "Off: no time in front of the chat lines (turn on to see [18:02:48]).");
    changed |= ChatScaleOptions("chatsc");   // 3.04.5
    ImGui::TextUnformatted("Lines shown (0 = only while typing)");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderInt("##chl", &settings.chatLines, 0, 20, "%d lines");
    ImGui::TextUnformatted("Text size");
    int pct = (int)(settings.chatSize * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##chs", &pct, 50, 150, "%d %%")) { settings.chatSize = pct / 100.0f; changed = true; }
    ImGui::TextUnformatted("Width (long lines wrap)");
    int wp = (int)(settings.chatWidth * 100 + 0.5f);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::SliderInt("##chw", &wp, 20, 90, "%d %% of the screen")) { settings.chatWidth = wp / 100.0f; changed = true; }
    ImGui::TextUnformatted("Fade out after");
    ImGui::SetNextItemWidth(-FLT_MIN);
    changed |= ImGui::SliderInt("##chf", &settings.chatFade, 0, 120, settings.chatFade ? "%d seconds" : "never");
    changed |= ChatPlacementOptions("chatsec");   // 2.22
    changed |= FloatMenu::ToggleSwitch("Chat bubbles (text over the head)", &settings.chatBubbles);   // 3.01: off by default
    if(FloatMenu::BigButton("Small chat (4 lines, 70 %)")) { settings.chatLines = 4; settings.chatSize = 0.7f; settings.chatWidth = 0.36f; changed = true; }
    if(FloatMenu::BigButton("Reset chat layout"))
    { settings.chatLines = 6; settings.chatSize = 0.8f; settings.chatScale = 1.0f; settings.chatWidth = 0.42f; settings.chatFade = 25; settings.chatBg = true; settings.chatX = 0.22f; settings.chatY = 0.02f;
      settings.chatBoxX = -1; settings.chatBoxY = -1; settings.chatBoxBg = true; settings.chatBoxLabel = true; settings.chatBoxTap = true; changed = true; }
    if(FloatMenu::BigButton("Clear chat")) chat.clear();
    ImGui::TextDisabled("Chat commands: /chatscale /pagesize /fontsize /chatwidth /chatfade /chatbg /clearchat");
    if(changed) ClampChatSettings();
    static bool dirty = false;
    if(changed) dirty = true;
    if(dirty && !ImGui::IsAnyItemActive()) { dirty = false; SaveConfig(); }
}

// Name colour: used for your name tag, your chat name, kill feed and your radar icon on everyone's screen.
static void NameColorPicker()
{
    uint32_t cur = settings.nameColor ? settings.nameColor : myColor;
    ImGui::TextUnformatted("Name color");
    ImGui::SameLine();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(cur | 0xFF)), "  %s", settings.name);
    ImGui::TextDisabled("Shows on your name tag, chat, kill feed and radar icon.");

    static const uint32_t swatches[] = {
        0xFFFFFFFF, 0xFF4444FF, 0xFF8C13FF, 0xFFDD33FF, 0x33CC55FF, 0x20B2AAFF, 0x0FD9FAFF, 0x6495EDFF,
        0x4949FFFF, 0xC715FFFF, 0xFF1493FF, 0xEE82EEFF, 0x8B4513FF, 0x778899FF, 0x247C1BFF, 0x111111FF,
    };
    const int n = (int)(sizeof(swatches) / sizeof(swatches[0])), perRow = 8;
    float sp = ImGui::GetStyle().ItemSpacing.x;
    float w = (ImGui::GetContentRegionAvail().x - sp * (perRow - 1)) / perRow;
    for(int i = 0; i < n; ++i)
    {
        if(i % perRow) ImGui::SameLine();
        ImGui::PushID(i);
        ImVec4 c = ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(swatches[i]));
        bool sel = settings.nameColor == swatches[i];
        if(sel) { ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, UI::S(4)); ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 1, 1)); }
        if(ImGui::ColorButton("##sw", c, ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoAlpha, ImVec2(w, w * 0.8f)))
            MP::SetNameColor(swatches[i]);
        if(sel) { ImGui::PopStyleColor(); ImGui::PopStyleVar(); }
        ImGui::PopID();
    }

    // any custom colour
    static float rgb[3] = { 1, 1, 1 };
    static uint32_t lastSynced = 1;
    if(lastSynced != cur)
    {
        lastSynced = cur;
        rgb[0] = ((cur >> 24) & 0xFF) / 255.f; rgb[1] = ((cur >> 16) & 0xFF) / 255.f; rgb[2] = ((cur >> 8) & 0xFF) / 255.f;
    }
    ImGui::TextUnformatted("Custom color (R / G / B)");
    bool done = false;
    const char* lbl[3] = { "##r", "##g", "##b" };
    const char* fmt[3] = { "R %.2f", "G %.2f", "B %.2f" };
    for(int k = 0; k < 3; ++k)
    {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SliderFloat(lbl[k], &rgb[k], 0.0f, 1.0f, fmt[k]);
        done |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if(done)
    {
        uint32_t c = ((uint32_t)(rgb[0] * 255 + 0.5f) << 24) | ((uint32_t)(rgb[1] * 255 + 0.5f) << 16) | ((uint32_t)(rgb[2] * 255 + 0.5f) << 8) | 0xFF;
        if(c < 0x100) c = 0x010101FF;       // keep away from 0 (= automatic)
        lastSynced = c;
        MP::SetNameColor(c);
    }
    if(ImGui::Button("Automatic (server color)", ImVec2(-FLT_MIN, UI::S(64)))) MP::SetNameColor(0);
}

static void SectionPlayer(void*)
{
    // 2.19.6: gang members, cops + story characters (special characters, loaded on everyone's phone)
    ImGui::TextUnformatted("Avatar skin (how others see you as CJ)");
    static const int base[] = { 105, 106, 107, 269, 270, 271, 102, 103, 104, 108, 109, 110, 114, 115, 116, 173, 174, 175,
                                117, 118, 120, 121, 122, 123, 280, 281, 282, 283, 284, 285, 286, 287, 0 };
    static const char* baseNames[] = { "GSF 1", "GSF 2", "GSF 3", "Big Smoke", "Sweet", "Ryder", "Ballas 1", "Ballas 2", "Ballas 3",
                                       "Vagos 1", "Vagos 2", "Vagos 3", "Aztecas 1", "Aztecas 2", "Aztecas 3", "Rifa 1", "Rifa 2", "Rifa 3",
                                       "Triads 1", "Triads 2", "Triad boss", "Da Nang 1", "Da Nang 2", "Da Nang 3",
                                       "LSPD", "SFPD", "LVPD", "Country cop", "Biker cop", "SWAT", "FBI", "Army" };
    static std::vector<int> ids; static std::vector<const char*> labels;
    if(ids.empty())
    {
        for(int i = 0; base[i]; ++i) { ids.push_back(base[i]); labels.push_back(baseNames[i]); }
        for(int i = 0; i < Game::SpecialSkinCount(); ++i) { const char* l = nullptr; int s = Game::SpecialSkinAt(i, &l); ids.push_back(s); labels.push_back(l); }
    }
    int cur = -1;
    for(int i = 0; i < (int)ids.size(); ++i) if(ids[i] == settings.skin) cur = i;
    if(cur < 0) cur = 1;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if(ImGui::Combo("##skin", &cur, labels.data(), (int)labels.size(), 10)) { settings.skin = ids[cur]; SaveConfig(); }
    if(FloatMenu::ToggleSwitch("Show my real model (skin mods, story characters, CJ's clothes)", &settings.realSkin)) SaveConfig();
    {
        int net = settings.realSkin ? Game::LocalSkinForNet(settings.skin) : settings.skin;
        const char* sl = Game::SkinLabel(net);
        const char* bl = nullptr;
        for(int i = 0; i < (int)ids.size(); ++i) if(ids[i] == net) bl = labels[i];
        if(sl || bl) ImGui::TextDisabled("Others see you as: %s", sl ? sl : bl);
        else ImGui::TextDisabled("Others see you as: ped model %d", net);
    }
    ImGui::Separator();
    NameColorPicker();
    ImGui::Separator();
    if(FloatMenu::BigButton("Teleport to Grove Street")) s_bWantTeleport = true;
    ImGui::Separator();
    if(FloatMenu::ToggleSwitch("100% progress on the default server", &settings.fullProgressDefault)) SaveConfig();
    ImGui::TextDisabled("%s", Progress::StatusText());
    ImGui::TextWrapped("All missions done while you play on the default server. Leaving it loads your own latest save again (your saves are backed up and put back). Custom servers always use your own progress.");
}

// 2.19: Chat pill = show / hide the chat box (tap the box to type; / shows the commands)
static void BubbleChat(void*)
{
    s_bChatBox = !s_bChatBox;
    if(s_bChatBox) OpenChatKeyboard("");
    else if(VKeyboard::IsOpen() && VKeyboard::IsChat()) VKeyboard::Close();
}

// ------------------------------------------------------------------------------------------
#include "mp_launcher.inc"   // 2.25: full-screen server launcher, player management, kick / ban

void MP::Init(const char* dataDir)
{
    MP_CopyStr(s_szDataDir, dataDir, sizeof(s_szDataDir));
    LoadConfig();
    Game::SetEnvCallbacks(OnLocalExplosion, OnLocalFire, OnForeignEnv);   // 2.13
    Game::SetShotCallback(OnLocalShot);        // 2.17: every bullet / rocket
    Game::SetMapMarkerProvider(MapMarkers);    // 2.17: players on the pause-menu map
    Progress::Init(s_szDataDir, OnBeforeSaveLoad, OnAfterSaveLoad);
    char bf[512]; snprintf(bf, sizeof(bf), "%s/broker.txt", s_szDataDir);
    Online::Start(bf);   // internet lobby + relay; broker.txt = optional private broker (e.g. HiveMQ Cloud)

    char sl[512]; snprintf(sl, sizeof(sl), "%s/servers.txt", s_szDataDir);
    ServerList::Load(sl);

    FloatMenu::AddSection("Connection", SectionConnection);
    FloatMenu::AddSection("Servers", SectionServers);
    FloatMenu::AddSection("Display", SectionDisplay);
    FloatMenu::AddSection("Chat", SectionChat);   // 2.19.6
    FloatMenu::AddSection("Sound", SectionSound);  // 3.00
    FloatMenu::AddSection("Voice", SectionVoice);  // 3.11
    FloatMenu::AddSection("Player", SectionPlayer);
    FloatMenu::AddSection("Cheats", SectionCheats);   // 2.35
    // 2.19 TopbarPlus bar: [ ... ] (Help / Server / Player list) + [ Chat ] (chat box)
    FloatMenu::AddDotsButton();
    FloatMenu::AddBarButton("Chat", BubbleChat, nullptr, &s_bChatBox);
    FloatMenu::AddMicButton(VoicePanelTap, VoiceMicState);   // 3.11: [ ... ][ Chat ][ mic ] -> voice chat panel
    FloatMenu::AddFloatingWindow(RenderVoicePanel);
    FloatMenu::SetPage(FloatMenu::PAGE_PLAYERS, PagePlayers);
    FloatMenu::SetPage(FloatMenu::PAGE_HELP, PageHelp);
    FloatMenu::AddFloatingWindow(Render219);
    FloatMenu::SetServerPageRenderer(RenderLauncher);   // 2.25: full-screen launcher
    FloatMenu::AddFloatingWindow(RenderKickDialog);     // 2.25: "You are banned from the server!"
    FloatMenu::AddFloatingWindow(RenderCheatGateDialog);  // 2.37: "This server doesn't allow cheat menus"
    client.SetUid(settings.deviceId);
    VKeyboard::SetValidator(OnName, NameBlocked);       // 2.37: SENJI turns red, Enter refused
    { char bp[512]; snprintf(bp, sizeof(bp), "%s/bans.txt", s_szDataDir); s_Server.SetBanFile(bp); }
    FloatMenu::SetBarPos(::cfg->GetFloat("ButtonsX", 0.5f, "MP"), ::cfg->GetFloat("ButtonsY", 0.07f, "MP"));
    // first launch: open the menu once so it's easy to find
    if(!::cfg->GetBool("MenuShownOnce", false, "MP"))
    {
        FloatMenu::Toggle();
        ConfigEntry* e = ::cfg->Bind("MenuShownOnce", true, "MP"); e->SetBool(true); delete e; ::cfg->Save();
    }

    // saved server comes back on start; if someone else already runs it, we join that one instead
    if(settings.hostLan && LAN::HasNetwork())
    {
        settings.hostLan = false;
        MP_CopyStr(s_szJoinPw, settings.hostedPassword, sizeof(s_szJoinPw));   // 2.19
        s_bHostJobCreate = false;   // 3.03: it exists already: it keeps its gamemodes
        StartJob(JOB_HOST, false, settings.hostedName, settings.hostedPort);
    }
}
