#pragma once
#include <stdint.h>
#include <string>
#include <deque>
#include "net/protocol.h"
#include "net/client.h"
#include "game/game.h"

struct MPSettings
{
    char  name[MP_NAME_LEN] = "Player";
    char  host[64]          = MP_DEFAULT_HOST;
    int   port              = MP_DEFAULT_PORT;
    bool  hostLan           = false;
    char  serverName[32]    = MP_DEFAULT_SERVER_NAME; // name of the server you host (same name + port = same server)
    char  hostedName[32]    = MP_DEFAULT_SERVER_NAME; // server this phone hosted last (restored on start)
    int   hostedPort        = MP_DEFAULT_PORT;
    float nametagSize       = 1.0f;
    float nametagDistance   = 60.0f;
    bool  showOwnTag        = true;
    bool  chatBubbles       = false;   // 3.01: chat text over the head (SA-MP chat bubble); off until the player turns it on
    bool  showHealthBars    = true;
    bool  showRadarIcons    = true;
    bool  showChat          = true;
    bool  showKillFeed      = true;
    bool  showStatus        = true;
    float chatX             = 0.22f;   // relative position (mobile radar sits top-left)
    float chatY             = 0.02f;
    // 2.19.6: chat layout, all adjustable in game (Server > Chat or /pagesize /fontsize /chatwidth /chatfade)
    int   chatLines         = 6;       // lines shown (SA-MP /pagesize), 0 = only while the chat box is open
    float chatSize          = 0.80f;   // text size (1 = old size)
    float chatScale         = 1.00f;   // 3.04.5: scales the whole chat (lines + chat box + suggestions), Settings > Chat / Display
    float chatWidth         = 0.42f;   // max width of a line (share of the screen), longer lines wrap
    int   chatFade          = 25;      // seconds a line stays before it fades out (0 = always shown)
    bool  chatBg            = true;    // dark see-through strip behind the chat
    // 2.22: chat box (the "Tap to chat" input under the chat), set in Server > Display / Chat
    bool  chatBoxBg         = true;    // dark box + white frame of the chat box
    bool  chatBoxLabel      = true;    // "Tap to chat - type / for commands" hint
    bool  chatBoxTap        = true;    // tapping the box opens the keyboard (off = only the Chat pill)
    float chatBoxX          = -1.0f;   // relative position of the box; -1 = under the chat lines
    float chatBoxY          = -1.0f;
    bool  realSkin          = true;    // 2.19.6: others see your real ped model (skin mods / story characters)
    int   skin              = 106;     // GSF member used for your remote avatar
    int   region            = 0;       // eRegion: 0 Asia, 1 Europe, 2 NA, 3 Oceania
    bool  lobbyAuto         = true;    // 2.20: regional brokers: lobby on the lowest-ping region
    bool  regionPicked      = false;   // 2.20: the player chose a region (else it follows the lowest ping once)
    uint32_t nameColor      = 0;       // RGBA; 0 = automatic (server palette)
    int   maxPlayers        = 100;     // limit for your hosted LAN server (1..100)
    int   serverIcon        = 0;       // 2.24: icon of the server you host: 0 = default (main_df_icon.png), 1..250 = icon_N.png
    bool  fullProgressDefault = true;  // 100% save while on the default server (2.10)
    bool  syncAnims         = true;    // 2.13: copy other players' animations onto their avatars
    bool  syncEnv           = true;    // 2.13: explosions, fires, dropped weapons
    bool  syncNpcs          = true;    // 2.18: people + traffic shared with the players around you
    bool  unlockWorld       = true;    // 2.21: all cities, shops, gyms, food, barbers, tattoo, clothes, Ammu-Nation, garages
    bool  girlfriends       = false;   // 2.21: girlfriend houses open too (optional)
    bool  eventTips         = true;    // 2.21: [Race] / [DM] / [Job] tips in the chat now and then
    // 3.01: SA-MP features
    bool  labels3d          = true;    // 3D text labels at job / race / derby / DM starts
    bool  mapIcons          = true;    // radar icons for jobs, races, derbies, DM zones
    bool  actors            = true;    // standing NPCs (actors) at the job starts
    bool  gangZones         = true;    // turf zones on the radar + map
    int   team              = 0;       // class selection team (TEAM_*, 0 = civilian)
    int   classSkin         = -1;      // class selection skin (-1 = keep my own skin)
    char  serverPassword[MP_PASSWORD_LEN + 1] = "";   // 2.19: password of the server you host (empty = open)
    char  serverLang[MP_LANG_LEN] = "English";      // 2.25: language of the server you host (server list "Language")
    char  deviceId[MP_UID_LEN + 1] = "";            // 2.25: this phone's id (bans), made on first start
    char  adminKey[129] = "";                         // 2.37: read from files/MP/admin.txt (never saved in the config) - proof you are SENJI
    char  hostedPassword[MP_PASSWORD_LEN + 1] = "";   // 2.19: password of the server this phone hosted last
    bool  disableCheats     = false;   // 2.35: host setting: no MP chat cheats + no CLEO menu for everyone
    int   hostModes         = 0;       // 3.02: gamemodes (GM_*) of the custom server I host; new custom servers: none
    bool  allowCheatMenu    = false;   // 3.00: host permission (custom servers): players may use CheatMenu (libCheatMenu.so / libCheatMenu64.so)
    bool  hitSound          = true;    // 3.00: play sound_010.mp3 when your shot hits a player
    bool  allowMissionPair  = true;    // 2.16: host permission (custom servers): "Mission sync" (players see each other + share mission markers on missions)
};

struct RemotePlayer
{
    bool      active = false;
    char      name[MP_NAME_LEN] = {0};
    uint32_t  color = 0xFFFFFFFF;
    uint16_t  skin = 106;

    SyncData  sync = {};
    bool      hasSync = false;
    uint64_t  syncTime = 0;
    uint32_t  lastSeq = 0;     // 2.18.3: newest sync applied (shared + direct updates can arrive out of order)
    Vec3      vel = {0, 0, 0};

    int       ped = 0, blip = 0, car = 0, carModel = 0, pedSkin = -1;
    // 3.10: clothing sync - what their CJ wears (clothesHash 0 = not CJ / no clothes). pedClothes =
    // the outfit the current avatar was made for (or tried: 0 + retry when no slot was free)
    PktClothes clothes = {};
    uint32_t  clothesHash = 0, pedClothes = 0;
    uint64_t  clothesRetryAt = 0;
    float     pedBase = 1.0f, carBase = 0.6f;
    // 2.35: visible enter / exit. leftCar = the car they just got out of (stays parked, handed to the
    // NPC copy of it); enterT / leaveT = when the enter / leave task started
    int       leftCar = 0, leftModel = 0; float leftBase = 0.6f; uint64_t leftT = 0, enterT = 0, leaveT = 0;
    int       weapon = 0;
    bool      ducking = false, deadApplied = false;
    bool      paused = false;            // player has their game paused (shown as PAUSED, can't be hurt)
    uint8_t   status = 0;                // STATUS_* : on a mission / in a cutscene (hidden, own story world)
    char      mission[8] = {0};          // mission script name, e.g. "SWEET1"
    int       moveState = 0;
    uint64_t  lastTask = 0, lastShoot = 0;

    Vec3      drawPos = {0, 0, 0};   // cached for the HUD (head position)
    bool      drawValid = false;

    // ---- 2.13 smooth motion ----------------------------------------------------------------
    // Every sync becomes a snapshot stamped with the SENDER's clock (mapped to ours), and the
    // avatar is drawn a little in the past, between two real snapshots. Relay bursts, Wi-Fi
    // jitter and lost packets no longer make avatars freeze, slide or teleport.
    struct Snap { uint64_t t; Vec3 pos; Vec3 vel; float heading; bool inCar; bool hasRot; float fwd[3], up[3]; };
    Snap      snaps[8];
    int       nSnaps = 0;
    bool      hasExt = false;            // sender is 2.13+ (clock, velocity, anims)
    bool      hasClock = false;
    double    clockOff = 0;              // our ms clock - sender's ms clock (least-delayed path)
    float     jitter = 0;                // ms
    float     delay = 100;               // ms we draw behind
    float     stale = 0;                 // 2.14: ms the drawn time is past the newest snapshot
    float     interval = 66;             // 2.14: average ms between this player's snapshots
    uint64_t  lastRecv = 0, lastFrame = 0;
    int       extMove = 0;               // sender's CPed move state
    Game::PedAnim anims[MP_ANIM_SLOTS];
    int       animCount = 0;
    Game::AppliedAnims applied;
    // 2.15: anims stamped like the snapshots, so the pose matches the drawn position
    struct AnimFrame { uint64_t t; int n; bool full; Game::PedAnim a[MP_ANIM_SLOTS]; };
    AnimFrame aframes[8];
    int       nAF = 0;
    double    renderT = 0;               // sender time (our clock) we are drawing
    bool      mirror = false;            // avatar driven by the mirrored blend tree (no walk task)
    uint64_t  gameWalkT = 0;             // 2.19.5: last time walk / run was left to the game's own walk task
    // 2.15: passenger in another player's car
    bool      hasVeh = false;
    uint8_t   vehOwner = 0;
    int8_t    vehSeat = 0;
    int       rideCar = 0;               // car handle this avatar sits in as a passenger (not its own)
    Vec3      lastGoal = {0, 0, 0};
    // ---- 2.17 --------------------------------------------------------------------------------
    bool      hasX2 = false;             // sender is 2.17+ (aim, interior, driver inputs)
    uint8_t   x2flags = 0;
    int       interior = 0;              // EnEx area the player is in (0 = outside)
    int       appliedArea = -1;          // area we put the avatar / its car in
    Game::VehFast vfast = {};
    Vec3      aimPoint = {0, 0, 0};
    uint64_t  lastShotPkt = 0;           // got PKT_SHOT recently: bullets are replayed one by one
    uint64_t  lastAim = 0, lastDuckFix = 0;
    float     lastAimPt[3] = { 0, 0, 0 };   // 2.32: where the aim task points (re-aim only when it moves)
    bool      gunUp = false;                // 2.32: aim pose is on (mirrored anims released)
    Game::VehState vwant = {}, vhave = {};   // car look: wanted (from the driver) / shown here
    uint16_t  vwantModel = 0;
    uint64_t  vApplied = 0;
    int       mmBlips[MP_MISSION_MARKERS_MAX] = {0};   // mission sync: partner's markers on my radar
    int       mmCount = 0;
    uint64_t  mmTime = 0;
    // ---- 2.19 --------------------------------------------------------------------------------
    float     stick = -1.0f;             // sender's joystick deflection 0..1 (-1 = old client)
    bool      jetpack = false, jetApplied = false;
    int       jetObj = 0;                // 2.19.1: jetpack object on the avatar's back
    bool      invisible = false;         // /invisible
    int       ping = -1, score = 0;      // scoreboard
    uint64_t  jackUntil = 0;             // we're pulling this player out of their car
    bool      tagBlocked = false;        // 2.19: a wall between us (name tag hidden unless /wallhack)
    uint64_t  losAt = 0;
};

struct ChatLine
{
    std::string prefix; uint32_t prefixColor;
    std::string text;   uint32_t textColor;
    uint64_t t = 0;     // 2.19.6: arrival time (fade out)
};

struct KillEntry
{
    std::string killer; uint32_t killerColor;   // empty killer = suicide / world
    std::string victim; uint32_t victimColor;
    uint8_t weapon;
    uint64_t time = 0;   // when it happened (2.17: lines no longer fade; the newest 5 stay)
    uint8_t victimId = 255;    // 2.37
    bool predicted = false;    // 2.37: my lethal hit, shown at once (confirmed / corrected by PKT_DEATH)
};

namespace MP
{
    const char* MissionTitle(const char* scriptName);   // "SWEET1" -> "Tagging Up Turf"
    extern MPSettings   settings;
    extern RemotePlayer players[MP_MAX_PLAYERS];
    extern std::deque<ChatLine>  chat;
    extern std::deque<KillEntry> kills;
    extern MPClient     client;

    // local player (for own name tag)
    extern uint8_t  myId;
    extern Vec3     myDrawPos;
    extern bool     myDrawValid;
    extern bool     classSelect;
    bool ModeOn(uint16_t gm);   // 3.02: the server I am on has this gamemode (default server: all)   // 3.01: /class screen is open (no name tags, like SA-MP)
    extern int      myHealth, myArmour;
    extern uint32_t myColor;

    // connected logo animation
    extern uint64_t logoStartMs;

    void Init(const char* dataDir);
    void Process();                 // game thread, every frame
    void ToggleConnect();           // big Connect button: the default server (always online)
    void JoinServer(const char* host, int port, const char* name); // Join button of a custom server
    bool IsDefaultServer(const char* name, int port);
    bool IsHostingDefault();
    bool IsOnDefault();
    bool CheatMenuAllowed();   // 3.00: on a custom server whose host turned Cheat Menu on
    bool IsLocalAdmin();     // 2.37: this phone has a valid admin.txt and the name SENJI
    bool IsDefaultAdmin();   // 2.37: I am [ADMIN] SENJI on the default server (the server gave me the tag)
    bool AddResource(const char* name);  // ": server_example" -> listed in the chat on join (90 built in, +32)
    int  ResourceCount();             // connected/connecting to the default server
    void SetHostLan(bool on, bool autoConnect = true);
    bool HasNetwork();              // Wi-Fi, mobile data or hotspot is on
    bool IsSearching();             // LAN search in progress
    void SendChat(const char* text);
    bool IsConnected();
    void AddSystemMessage(uint32_t rgba, const char* text);
    void SaveConfig();
    const char* DataDir();
    const char* LanAddress();
    int  HostedPlayerCount();
    bool IsHosting();
    bool CheatsBlocked();   // 2.35: the server's host disabled cheats
    int  DisplayPing();   // 2.28: my ping for the UI / scoreboard (host: real network leg, not loopback 0)
    void SetNameColor(uint32_t rgba);  // 0 = automatic
    void ClampChatSettings();          // 2.19.6
    bool IsChatBoxOpen();              // 2.19.6: chat box (typing) shown -> all recent lines visible
    extern float chatDrawnH;           // 2.19.6: height of the chat lines drawn last frame
    int  OnlineCount();             // players on the current server (incl. you), 0 when offline
    int  OnlineMax();               // that server's limit (or your hosting limit when offline)
    // 2.19 join screen: black slide overlay (x0..x1 = covered part, 0..1 of the width) + spawn picker
    bool JoinOverlay(float& x0, float& x1);
    const char* SpawnSpotName(int i);
    int  SpawnSpotCount();
    bool SpawnPickerShown();
    int  SpawnSelected();
    void SpawnStep(int d);
    void SpawnConfirm();
    float SpawnPickerAlpha();
    bool JoinScreenActive();   // 2.19.1: join screen / connection view / spawn picker on screen
    // 3.04: Brief Case War (private 24/7 gamemode server, pinned in the Internet list)
    void JoinBcw();            // join it (or leave it when you are on it)
    bool IsOnBcw();
    bool BcwOwnTagHidden();    // 3.10: your ped is parked hidden (connection screen - e.g. Gustav's base view - or respawn wait)
    uint32_t BcwDecorate(int id, const char* name, uint32_t color, char* out, size_t n);   // [GTV] / [SLZ] tag + team colour
}
bool MP_WallhackOn();   // 2.19 /wallhack
