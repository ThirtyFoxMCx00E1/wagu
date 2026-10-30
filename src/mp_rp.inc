// MP 3.25 - Mega Vil Futuristic (Roleplay), client side (included by mp.cpp after mp_mw3.inc).
//
// The 24/7 featured roleplay server "Mega Vil Futuristic" (port 1932, 250 players, hosted by the
// Broker bot with the 100-rank standby pool). The server (net/server_rp.inc) keeps the money, jobs,
// houses, phone numbers and calls; this file draws and plays:
//   - the connection screen over the Lobby Hub (Mega Vil Plaza), sky blue < SPAWN >. Citizens with a
//     house (or a spawn point set inside it with /setspawn) always spawn at home - never at the hub,
//     not even after the hospital
//   - the RP HUD (from the screenshots): MEGA VIL FUTURISTIC ROLEPLAY logo on top, info panel
//     (time / id / name / job / cash / bank), status circles (health / armour / hunger / thirst /
//     stamina %), NEW CITIZEN countdown box, GREENZONE badge, voice (3.28: no phone button - PANEL > Phone)
//   - job shifts (vehicle, uniform, checkpoints), city events, houses, pets (holo drones), emotes
//   - people in the street: walk up to someone, they stop and look at you; Talk / Nevermind, then
//     custom subtitles and answers
//   - 100+ RP commands (no cheats), the real phone UI (mp_rp_phone.inc)

static const uint32_t RP_UI_SKY  = RP_SKY;
static const uint32_t RP_UI_DARK = 0x07111FFF;
// 3.29: click sounds from the MP folder (assets/MP; see PASTE_CLICK_MP3_HERE.txt):
//   click.mp3 = UI buttons, click2.mp3 = phone taps, click3.mp3 = a "Received" item card
enum { RPS_CLICK = 0, RPS_PHONE, RPS_CARD };
static void RpClick(int which)
{
    static const char* const kFile[3] = { "click.mp3", "click2.mp3", "click3.mp3" };
    if(which < 0 || which > 2 || !s_szDataDir[0]) return;
    char snd[512]; snprintf(snd, sizeof(snd), "%s/%s", s_szDataDir, kFile[which]);
    Sound::PlayUi(snd, 1.0f);
}
static bool RpLifeCommand(const char* cmd, const char* arg);   // mp_rp_life.inc
static void RpLifeProcess(int ped, uint64_t now, bool engaged);
static void RpInfoDraw(ImDrawList* dl);
static void RpDrawAtm(uint64_t nowMs);
static void RpDrawDrops(uint64_t nowMs);              // 3.30
static void RpDropsOnPacket(const uint8_t* d, int len);   // 3.30
static void MpUiClick() { RpClick(RPS_CLICK); }
// 3.29: an outlined button - no background, white outline, white text with a shadow (NPC talk menu)
static bool RpOutlineButton(const char* label, ImVec2 size)
{
    ImVec2 a = ImGui::GetCursorScreenPos();
    bool r = ImGui::InvisibleButton(label, size), down = ImGui::IsItemActive();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 b(a.x + size.x, a.y + size.y); float rr = UI::S(14);
    if(down) dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 60), rr);
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 110), rr, 0, UI::S(5));      // soft dark edge so it reads on bright sky
    dl->AddRect(a, b, IM_COL32_WHITE, rr, 0, UI::S(2.5f));
    const char* end = strstr(label, "##");
    float fs = UI::S(26) * (ImGui::GetFont() && ImGui::GetFont()->FontSize > 0 ? ImGui::GetFontSize() / ImGui::GetFont()->FontSize : 1.0f);
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, label, end);
    ImVec2 p(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f);
    dl->AddText(UI::Font(), fs, ImVec2(p.x + UI::S(2), p.y + UI::S(2)), IM_COL32(0, 0, 0, 200), label, end);
    dl->AddText(UI::Font(), fs, p, IM_COL32_WHITE, label, end);
    if(r) RpClick(RPS_CLICK);
    return r;
}
// 3.30: the column for the outlined talk / ATM / pick-up buttons - right of centre, but always
// left of the PANEL button (it sat under Nevermind / Goodbye before), and every button gets its
// own spot inside a window with room around it, so the outline is never cut at the bottom
static float RpSideColX(float bw)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    float panelLeft = ds.x - UI::S(560) - UI::S(48) - UI::S(36);   // RpDrawPanelButton: centre ds.x - 560, 96 wide
    float x = ds.x * 0.58f;
    if(x + bw > panelLeft) x = panelLeft - bw;
    if(x < ds.x * 0.36f) x = ds.x * 0.36f;
    return x;
}
static int RpOutlineColumn(const char* id, const char* const* labels, int n, float bw, float bh, float gap, float cy, float fontScale = 1.0f)
{
    const float total = n * bh + (n - 1) * gap, m = UI::S(20);
    float x = RpSideColX(bw), y = cy - total * 0.5f;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    if(y < UI::S(80)) y = UI::S(80);
    if(y + total > ds.y - UI::S(150)) y = ds.y - UI::S(150) - total;
    BcwBeginOverlay(id, ImVec2(x - m, y - m), ImVec2(bw + m * 2, total + m * 2), 0.0f);
    if(fontScale != 1.0f) ImGui::SetWindowFontScale(fontScale);
    int hit = -1;
    for(int i = 0; i < n; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(x, y + i * (bh + gap)));
        if(RpOutlineButton(labels[i], ImVec2(bw, bh))) hit = i;
    }
    if(fontScale != 1.0f) ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
    return hit;
}
static const Vec3 kRpLobbyCam  = { 1463.4f, -1673.4f, 20.5f };   // 3.29: low over Mega Vil Plaza, facing the info people
static const Vec3 kRpLobbyLook = { 1481.6f, -1692.0f, 15.2f };
static const Vec3 kRpJailCell  = { 264.0f, 77.6f, 1001.04f };    // LSPD cells (interior 6)
#define RP_JAIL_AREA 6

struct RpSms   { int32_t number; bool mine; char text[120]; uint64_t at; };
struct RpPost  { int32_t number; char name[24]; char text[120]; uint64_t at; };
struct RpCallLog { int32_t number; char name[24]; uint8_t kind; uint64_t at; };   // kind 0 in, 1 out, 2 missed

struct RpClientState
{
    bool        engaged = false;
    int         view = 0;                 // 0 off, 1 lobby, 2 playing
    PktRpMe     me{};  bool haveMe = false;
    PktRpState  st{};  bool haveState = false;
    RpRow       row[MP_MAX_PLAYERS]{}; bool haveRow[MP_MAX_PLAYERS]{}; bool rowSeen[MP_MAX_PLAYERS]{};
    uint64_t    connectedAt = 0, lastNoWanted = 0, lastHudOff = 0, lastArrive = 0, lastDisarm = 0, lastNeed = 0, lastMoney = 0, lastSpawnSend = 0;
    // job / call / event target
    bool        tgtOn = false; Vec3 tgt{}; int tgtCp = 0, tgtBlip = 0; uint32_t tgtCol = RP_SKY; char tgtText[96] = "";
    Vec3        callPos{}; int callBlip = 0; uint64_t callAt = 0; char callText[48] = "";
    int         gpsBlip = 0; char gpsName[48] = ""; Vec3 gpsPos{};
    // phone
    bool        phone = false; int app = 0; int appSub = 0; int appSel = -1; float phoneAnim = 0;
    int         ringFrom = -1; bool ringOut = false; int32_t ringNum = 0; char ringName[32] = ""; uint64_t ringAt = 0;
    int         callWith = -1; int32_t callNum = 0; char callName[32] = ""; uint64_t callStart = 0;
    std::vector<RpSms> sms; std::vector<RpPost> feed; std::vector<RpCallLog> calls; int unreadSms = 0;
    int32_t     smsThread = 0;
    // pet
    int         petObj = 0, petModel = 0; float petSpin = 0;
    // people in the street
    int         talkPed = 0, talkStage = 0, talkTopic = 0; uint64_t talkAt = 0, lastPedScan = 0; Vec3 talkPos{};
    // 3.27: people you just talked to / dismissed are left alone for a while (no instant re-prompt)
    int         talkDone[8]{}; uint64_t talkDoneAt[8]{}; int talkDoneN = 0; uint64_t talkCoolUntil = 0;
    // 3.27: the Google-style radar replaces the game's one
    bool        radarHidden = false; uint64_t lastRadarHide = 0;
    char        sub[160] = ""; char subWho[32] = ""; uint64_t subAt = 0; int subMs = 0;
    // emotes
    int         emote = -1; Vec3 emotePos{}; uint64_t emoteAt = 0; int emoteMs = 0; bool emoteTalk = false;   // 3.27: own timer, talk wave
    int         askEmote = -1, askFrom = -1; uint64_t askAt = 0;
    // status
    float       stamina = 100.0f; uint64_t lastStam = 0;
    bool        wasDead = false, needHome = false, cuffAnim = false; uint64_t respawnAt = 0;
    int         interior = 0;
    int         placeBlip[64]{}; bool blipsMade = false;
    char        banner[96] = ""; uint32_t bannerCol = RP_SKY; uint64_t bannerAt = 0;
    int         payFx = 0; char payWhy[48] = ""; uint64_t payAt = 0;
    uint64_t    lastFineAsk = 0;
};
static RpClientState s_Rp;

static bool RpOnServer()
{
    return client.GetState() == NET_CONNECTED && MP_IsRpServerName(client.GetServerName()[0] ? client.GetServerName() : s_szCurServer);
}
bool MP::IsOnRp() { return RpOnServer(); }
static bool RpHidden() { return s_Rp.engaged && s_Rp.view == 1; }
bool MP::RpOwnTagHidden() { return (RpOnServer() || s_bRpJoin) && !(s_Rp.engaged && s_Rp.view == 2); }
static const char* RpName(int id)
{
    if(id == myId) return settings.name;
    if(id >= 0 && id < MP_MAX_PLAYERS && players[id].active) return players[id].name;
    return "?";
}
static int RpIdForNumber(int32_t n) { for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_Rp.haveRow[i] && s_Rp.row[i].number == n) return i; return -1; }
static void RpBanner(uint32_t col, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Rp.banner, sizeof(s_Rp.banner), fmt, ap); va_end(ap);
    s_Rp.bannerCol = col; s_Rp.bannerAt = MP_NowMs();
}
static void RpSubtitle(const char* who, int ms, const char* fmt, ...)
{
    va_list ap; va_start(ap, fmt); vsnprintf(s_Rp.sub, sizeof(s_Rp.sub), fmt, ap); va_end(ap);
    MP_CopyStr(s_Rp.subWho, who ? who : "", sizeof(s_Rp.subWho)); s_Rp.subAt = MP_NowMs(); s_Rp.subMs = ms;
}
// name tag second line: "Police Officer - Lv 4" / "New Citizen"
bool MP::RpSubTag(int id, char* out, size_t n)
{
    if(!RpOnServer() || id < 0 || id >= MP_MAX_PLAYERS || !s_Rp.haveRow[id]) return false;
    const RpRow& r = s_Rp.row[id];
    const char* job = r.job < RPJ_COUNT ? g_RpJobs[r.job].name : "Citizen";
    if(r.flags & 2) snprintf(out, n, "CUFFED");
    else if(r.flags & 8) snprintf(out, n, "New Citizen - %s", job);
    else snprintf(out, n, "%s%s - Lv %d", job, (r.flags & 1) ? " (duty)" : "", r.level);
    return true;
}
int MP::RpCallPartner() { return RpOnServer() ? s_Rp.callWith : -1; }

static void RpSend(uint8_t cmd, uint8_t a = 0, uint8_t b = 0, int32_t val = 0, const char* text = nullptr)
{
    PktRpCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_RP_CMD; c.cmd = cmd; c.a = a; c.b = b; c.val = val;
    int ped = Game::LocalPed(); Vec3 p; if(ped > 0 && Game::GetCharPos(ped, p)) { c.pos[0] = p.x; c.pos[1] = p.y; c.pos[2] = p.z; }
    if(text) MP_CopyStr(c.text, text, sizeof(c.text));
    client.Send(&c, sizeof(c));
}

// ---- citizen backup cards (MP/rp_card.txt, like mw3_card.txt) ---------------------------------
static void RpCardPath(char* out, size_t n) { snprintf(out, n, "%s/rp_card.txt", s_szDataDir); }
static int RpCardsLoad(PktRpCard* out, int max)
{
    char path[512]; RpCardPath(path, sizeof(path));
    FILE* f = fopen(path, "r"); if(!f) return 0;
    char line[600]; int n = 0;
    while(n < max && fgets(line, sizeof(line), f))
    {
        char key[64], hex[520];
        if(sscanf(line, "%63s %519s", key, hex) != 2 || strlen(hex) != sizeof(PktRpCard) * 2) continue;
        uint8_t* b = (uint8_t*)&out[n]; bool ok = true;
        for(size_t i = 0; i < sizeof(PktRpCard) && ok; ++i) { unsigned v; ok = sscanf(hex + i * 2, "%2x", &v) == 1; b[i] = (uint8_t)v; }
        if(ok && out[n].type == PKT_RP_CARD) { out[n].key[sizeof(out[n].key) - 1] = 0; ++n; }
    }
    fclose(f); return n;
}
static void RpCardStore(const PktRpCard& c)
{
    PktRpCard all[9]; int n = RpCardsLoad(all, 8), at = -1;
    for(int i = 0; i < n; ++i) if(!strcmp(all[i].key, c.key)) at = i;
    if(at >= 0 && all[at].t > c.t) return;
    if(at < 0) { if(n == 8) { memmove(all, all + 1, sizeof(all[0]) * 7); n = 7; } at = n++; }
    all[at] = c;
    char path[512], tmp[520]; RpCardPath(path, sizeof(path)); snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE* f = fopen(tmp, "w"); if(!f) return;
    for(int i = 0; i < n; ++i)
    {
        fprintf(f, "%s ", all[i].key);
        const uint8_t* b = (const uint8_t*)&all[i];
        for(size_t k = 0; k < sizeof(PktRpCard); ++k) fprintf(f, "%02x", b[k]);
        fputc('\n', f);
    }
    fclose(f); rename(tmp, path);
}
static void RpCardsSend()
{
    PktRpCard all[8]; int n = RpCardsLoad(all, 8);
    for(int i = 0; i < n; ++i) client.Send(&all[i], sizeof(all[i]));
    if(n) logger->Info("RP: sent %d citizen backup card(s) to the host", n);
}

// ---- home spawn point (MP/rp_spawn.txt: "house x y z heading") -------------------------------
struct RpHomeSpawn { int house = -1; Vec3 p{}; float h = 0; };
static RpHomeSpawn RpLoadHomeSpawn()
{
    RpHomeSpawn s; char path[512]; snprintf(path, sizeof(path), "%s/rp_spawn.txt", s_szDataDir);
    FILE* f = fopen(path, "r"); if(!f) return s;
    if(fscanf(f, "%d %f %f %f %f", &s.house, &s.p.x, &s.p.y, &s.p.z, &s.h) != 5) s.house = -1;
    fclose(f); return s;
}
static void RpSaveHomeSpawn(int house, const Vec3& p, float h)
{
    char path[512]; snprintf(path, sizeof(path), "%s/rp_spawn.txt", s_szDataDir);
    FILE* f = fopen(path, "w"); if(!f) return;
    fprintf(f, "%d %.2f %.2f %.2f %.1f\n", house, p.x, p.y, p.z, h); fclose(f);
}
static bool RpHasHome() { return s_Rp.haveMe && s_Rp.me.house < RP_HOUSE_COUNT; }
// where a citizen with a house appears: the /setspawn point inside, else the front door
static bool RpHomePoint(Vec3& at, float& h)
{
    if(!RpHasHome()) return false;
    const RpHouse& H = g_RpHouses[s_Rp.me.house];
    RpHomeSpawn s = RpLoadHomeSpawn();
    if(s.house == s_Rp.me.house) { at = s.p; h = s.h; return true; }
    at = { H.x, H.y, H.z }; h = 0; return true;
}

// ---- the world: no game police, place icons, interior ------------------------------------------
static void RpNoWanted(uint64_t now)
{
    if(!Game::IsReady() || Game::IsPaused()) return;
    if(now - s_Rp.lastNoWanted >= 250) { s_Rp.lastNoWanted = now; Game::SetMaxWanted(0); }
    if(Game::GetWantedLevel() > 0) Game::ClearWanted();
}
static int RpPlaceSprite(uint8_t k)
{
    switch(k)
    {
        case RPK_SPAWN: return 35; case RPK_JOBS: return 56; case RPK_BANK: return 52; case RPK_SHOP: return 17; case RPK_FOOD: return 10;
        case RPK_CLOTHES: return 45; case RPK_CARS: return 55; case RPK_TECH: return 48; case RPK_GUNS: return 6; case RPK_HOSPITAL: return 22;
        case RPK_POLICE: return 30; case RPK_ATM: return 52; case RPK_PETS: return 21; case RPK_ARMY: return 20; case RPK_AIRPORT: return 5;
        default: return 0;
    }
}
static void RpMakeBlips()
{
    if(s_Rp.blipsMade || !Game::IsReady()) return;
    for(int i = 0; i < RP_PLACE_COUNT && i < 64; ++i)
    {
        int sp = RpPlaceSprite(g_RpPlaces[i].kind);
        if(sp && g_RpPlaces[i].kind != RPK_ATM) s_Rp.placeBlip[i] = Game::AddSpriteBlip({ g_RpPlaces[i].x, g_RpPlaces[i].y, g_RpPlaces[i].z }, sp);
    }
    s_Rp.blipsMade = true;
}
static void RpClearBlips()
{
    for(int i = 0; i < 64; ++i) { if(s_Rp.placeBlip[i]) Game::RemoveBlip(s_Rp.placeBlip[i]); s_Rp.placeBlip[i] = 0; }
    s_Rp.blipsMade = false;
    if(s_Rp.tgtBlip) Game::RemoveBlip(s_Rp.tgtBlip); s_Rp.tgtBlip = 0;
    if(s_Rp.tgtCp) Game::DeleteRaceCheckpoint(s_Rp.tgtCp); s_Rp.tgtCp = 0;
    if(s_Rp.callBlip) Game::RemoveBlip(s_Rp.callBlip); s_Rp.callBlip = 0;
    if(s_Rp.gpsBlip) Game::RemoveBlip(s_Rp.gpsBlip); s_Rp.gpsBlip = 0;
}
static void RpSetInterior(int area) { if(s_Rp.interior == area) return; Game::EnterArea(area); s_Rp.interior = area; }
static void RpSetGps(const Vec3& p, const char* name)
{
    if(s_Rp.gpsBlip) Game::RemoveBlip(s_Rp.gpsBlip);
    s_Rp.gpsBlip = Game::AddCoordBlip(p, RP_SKY, 2); s_Rp.gpsPos = p;
    MP_CopyStr(s_Rp.gpsName, name ? name : "", sizeof(s_Rp.gpsName));
    SysMsg(RP_SKY, "[GPS] Route set: %s.", s_Rp.gpsName);
}
static void RpClearTarget()
{
    s_Rp.tgtOn = false;
    if(s_Rp.tgtBlip) Game::RemoveBlip(s_Rp.tgtBlip); s_Rp.tgtBlip = 0;
    if(s_Rp.tgtCp) Game::DeleteRaceCheckpoint(s_Rp.tgtCp); s_Rp.tgtCp = 0;
}
static void RpSetTarget(const float* p, uint32_t col, const char* text)
{
    RpClearTarget();
    s_Rp.tgtOn = true; s_Rp.tgt = { p[0], p[1], p[2] }; s_Rp.tgtCol = col ? col : RP_SKY;
    MP_CopyStr(s_Rp.tgtText, text ? text : "", sizeof(s_Rp.tgtText));
    s_Rp.tgtCp = Game::CreateRaceCheckpoint(1, s_Rp.tgt, s_Rp.tgt, 6.0f);
    s_Rp.tgtBlip = Game::AddCoordBlip(s_Rp.tgt, s_Rp.tgtCol | 0xFF, 2);
}

// ---- pets: a holo drone object that floats next to you -----------------------------------------
static void RpPetUpdate(int ped, float dt)
{
    int want = (s_Rp.haveMe && s_Rp.me.pet < RP_PET_COUNT && s_Rp.view == 2) ? g_RpPets[s_Rp.me.pet].model : 0;
    if(want != s_Rp.petModel && s_Rp.petObj) { Game::DeleteWorldObject(s_Rp.petObj); s_Rp.petObj = 0; }
    s_Rp.petModel = want;
    if(!want || ped <= 0 || Game::IsPaused()) return;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    float h = Game::GetCharHeading(ped) * 0.0174533f, bob = sinf(MP_NowMs() * 0.003f) * 0.15f;
    Vec3 at = { me.x + cosf(h) * 0.9f - sinf(h) * 0.6f, me.y + sinf(h) * 0.9f + cosf(h) * 0.6f, me.z + 0.6f + bob };
    if(Game::IsCharInAnyCar(ped)) at.z += 1.6f;
    if(!s_Rp.petObj || !Game::WorldObjectExists(s_Rp.petObj)) { s_Rp.petObj = Game::CreateWorldObject(want, at); return; }
    s_Rp.petSpin += dt * 90.0f; if(s_Rp.petSpin > 360) s_Rp.petSpin -= 360;
    Game::MoveWorldObject(s_Rp.petObj, at, s_Rp.petSpin);
}

// ---- emotes -------------------------------------------------------------------------------------
// 3.27: msOverride > 0 = stop after that long even for a looping anim (the talk greeting)
static void RpPlayEmote(int i, int msOverride = 0)
{
    int ped = Game::LocalPed(); if(ped <= 0 || i < 0 || i >= RP_EMOTE_COUNT) return;
    if(Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Get out of the vehicle first."); return; }
    if(s_Rp.haveMe && s_Rp.me.cuffed) return;
    if(Game::PlayAnimLoop(ped, g_RpEmotes[i].anim, g_RpEmotes[i].file))
    {
        s_Rp.emote = i; s_Rp.emoteAt = MP_NowMs(); s_Rp.emoteMs = msOverride > 0 ? msOverride : g_RpEmotes[i].ms;
        s_Rp.emoteTalk = false; Game::GetCharPos(ped, s_Rp.emotePos);
    }
}
static void RpStopEmote()
{
    int ped = Game::LocalPed();
    if(s_Rp.emote >= 0 && ped > 0) Game::ClearTasksNow(ped);
    s_Rp.emote = -1; s_Rp.emoteTalk = false;
}
// 3.27: an emote always ends - its timer, walking off, or pushing the stick / Stop button.
// (Before, a looping emote only ended after moving 1.5 m, which the looping anim itself blocks:
//  the Talk greeting started the endless "wave" loop and you were stuck waving / dancing.)
static void RpEmoteTick(int ped, uint64_t now)
{
    if(s_Rp.emote < 0 || ped <= 0) return;
    Vec3 p; Game::GetCharPos(ped, p);
    float dx = p.x - s_Rp.emotePos.x, dy = p.y - s_Rp.emotePos.y;
    int sx = 0, sy = 0; Game::GetStick(sx, sy);
    bool stick = now - s_Rp.emoteAt > 600 && (sx * sx + sy * sy > 60 * 60);
    bool timed = s_Rp.emoteMs > 0 && now - s_Rp.emoteAt > (uint64_t)s_Rp.emoteMs;
    bool gone = Game::IsCharInAnyCar(ped) || Game::IsCharDead(ped);
    if(dx * dx + dy * dy > 1.5f * 1.5f || timed || stick || gone) { if(!gone) Game::ClearTasksNow(ped); s_Rp.emote = -1; s_Rp.emoteTalk = false; }
}

// ---- people in the street ----------------------------------------------------------------------
struct RpTalkTopic { const char* option; const char* const lines[4]; };
static const char* const kRpGreet[] =
{
    "Hey there, citizen. Beautiful neon night in Mega Vil, isn't it?", "Oh! You startled me. Can I help you?",
    "Welcome to the future, friend. What do you need?", "Hi! Don't mind me, just waiting for my hover bus.",
    "Good to see a friendly face. The city's busy today.", "Yo. You new around here? I can tell.",
    "Careful, the holo ads are loud today. What's up?", "Hello! My drone is charging, so I've got a minute.",
};
static const RpTalkTopic kRpTopics[] =
{
    { "Ask about the city", { "Mega Vil used to be Los Santos. Then the towers went up and the holograms came on.",
                              "City Hall has the Job Center. Everyone starts there - taxi, trucker, police, even pilot.",
                              "Paydays every half hour, straight to your bank. The Mega Bank pays interest too.",
                              "Stay in the greenzones if you want peace. Nobody can hurt you in there." } },
    { "Ask for directions", { "Let me put it on your GPS. Follow the sky blue marker.",
                              "It's not far. I'll send the route to your phone.",
                              "Easy. Check your GPS, I marked it.",
                              "Head that way. Your phone has the route now." } },
    { "Ask for a job tip",  { "Truckers make good money. Long routes pay the most.",
                              "Pilots get the biggest pay, but you need level 2 first.",
                              "Police need level 3 and two hours in the city. Then you can wear the badge.",
                              "Double Pay Hour comes with the city events. Work then - everything pays twice." } },
    { "Compliment them",    { "Aww, thank you! You made my day.", "Ha! You're too kind. Nice outfit yourself.",
                              "Thanks, citizen. Mega Vil needs more people like you.", "Oh stop it. ...No, keep going." } },
    { "Ask about the news", { "Did you hear? A Holo Cash Drop hits the streets every so often. Free money!",
                              "The Neon Night Festival at the Plaza is the best party in the city.",
                              "Lottery tickets are $500. I never win, but you might.",
                              "They say the holo towers pay $2,500 to whoever reaches them first." } },
};
#define RP_TOPICS ((int)(sizeof(kRpTopics) / sizeof(kRpTopics[0])))
static const char* const kRpBye[] = { "Take care out there!", "See you around, citizen.", "Stay safe in the neon.", "Bye! Watch out for drones." };

// 3.27: remember who we just talked to - the scan below leaves them alone for 90 s, everybody for 6 s
static void RpTalkForget(int h)
{
    if(h <= 0) return;
    int at = s_Rp.talkDoneN++ % 8; s_Rp.talkDone[at] = h; s_Rp.talkDoneAt[at] = MP_NowMs();
    s_Rp.talkCoolUntil = MP_NowMs() + 6000;
}
static bool RpTalkRecent(int h, uint64_t now)
{
    for(int i = 0; i < 8; ++i) if(s_Rp.talkDone[i] == h && now - s_Rp.talkDoneAt[i] < 90000) return true;
    return false;
}
static void RpTalkRelease()
{
    if(s_Rp.talkPed > 0) { Game::FreezeChar(s_Rp.talkPed, false); Game::ClearTasksNow(s_Rp.talkPed); RpTalkForget(s_Rp.talkPed); }
    if(s_Rp.emoteTalk) RpStopEmote();   // the greeting wave ends with the talk
    s_Rp.talkPed = 0; s_Rp.talkStage = 0;
}
static bool RpSkipNotAmbient(int h) { return h == Game::LocalPed(); }
static void RpPedTick(int ped, uint64_t now)
{
    if(s_Rp.view != 2 || ped <= 0 || Game::IsPaused() || s_Rp.phone) { if(s_Rp.talkPed && s_Rp.phone) RpTalkRelease(); return; }
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    if(s_Rp.talkPed)
    {
        Vec3 p; bool alive = Game::GetCharPos(s_Rp.talkPed, p) && !Game::IsCharDead(s_Rp.talkPed);
        float dx = p.x - me.x, dy = p.y - me.y;
        if(!alive || dx * dx + dy * dy > 6.0f * 6.0f || Game::IsCharInAnyCar(ped)) { if(alive) RpTalkRelease(); else { s_Rp.talkPed = 0; s_Rp.talkStage = 0; } return; }
        if(now - s_Rp.talkAt > 1500) { s_Rp.talkAt = now; Game::TaskFaceChar(s_Rp.talkPed, ped); Game::TaskLookAtChar(s_Rp.talkPed, ped, 3000); }
        if(s_Rp.talkStage == 3 && now - s_Rp.subAt > (uint64_t)s_Rp.subMs) RpTalkRelease();
        return;
    }
    if(Game::IsCharInAnyCar(ped) || now - s_Rp.lastPedScan < 350) return;
    s_Rp.lastPedScan = now;
    if(now < s_Rp.talkCoolUntil || s_Rp.emote >= 0) return;   // 3.27: not right after a talk, not while you emote
    Game::NpcPedInfo list[8];
    int n = Game::ListAmbientPeds(list, 8, me, 3.2f, RpSkipNotAmbient);
    int best = -1; float bd = 99;
    for(int i = 0; i < n; ++i)
    {
        if(list[i].dead || list[i].handle == ped) continue;
        bool isPlayer = false; for(int k = 0; k < MP_MAX_PLAYERS; ++k) if(players[k].active && players[k].ped == list[i].handle) { isPlayer = true; break; }
        if(isPlayer || RpTalkRecent(list[i].handle, now)) continue;
        float dx = list[i].pos.x - me.x, dy = list[i].pos.y - me.y, d = sqrtf(dx * dx + dy * dy);
        if(d < bd) { bd = d; best = i; }
    }
    if(best < 0) return;
    s_Rp.talkPed = list[best].handle; s_Rp.talkStage = 1; s_Rp.talkAt = now; s_Rp.talkPos = list[best].pos;
    Game::ClearTasksNow(s_Rp.talkPed);
    Game::TaskStandStill(s_Rp.talkPed);
    Game::TaskFaceChar(s_Rp.talkPed, ped);
    Game::TaskLookAtChar(s_Rp.talkPed, ped, 4000);
}
static void RpTalkChoose(int topic)
{
    int ped = Game::LocalPed();
    if(topic < 0)
    {   // Nevermind / Goodbye
        if(s_Rp.talkStage >= 2) RpSubtitle("Citizen", 2500, "%s", kRpBye[rand() % 4]);
        if(s_Rp.emoteTalk) RpStopEmote();
        if(s_Rp.talkStage >= 2) { if(s_Rp.talkPed > 0) { Game::FreezeChar(s_Rp.talkPed, false); Game::ClearTasksNow(s_Rp.talkPed); } s_Rp.talkStage = 3; }
        else RpTalkRelease();   // Nevermind: let them go and don't ask about them again right away
        return;
    }
    if(topic == 100)
    {   // 3.27: a short wave (2.5 s, ends by itself) instead of the endless wave loop
        s_Rp.talkStage = 2; RpSubtitle("Citizen", 4500, "%s", kRpGreet[rand() % 8]);
        if(ped > 0 && s_Rp.emote < 0) { int w = RpFindEmote("wave2"); if(w < 0) w = RpFindEmote("wave"); RpPlayEmote(w, 2500); if(s_Rp.emote >= 0) s_Rp.emoteTalk = true; }
        return;
    }
    const RpTalkTopic& T = kRpTopics[topic];
    RpSubtitle("Citizen", 6000, "%s", T.lines[rand() % 4]);
    if(topic == 1)
    {   // directions: a random useful place on the GPS
        const RpPlace& pl = g_RpPlaces[1 + rand() % (RP_PLACE_COUNT - 1)];
        RpSetGps({ pl.x, pl.y, pl.z }, pl.name);
    }
    s_Rp.talkStage = 2;
}
// ---- 3.27: Google-style radar (bottom left, square), replaces the game's radar -------------------
static bool RpRadarOn() { return settings.radarStyle != 0 && s_Rp.view == 2 && s_Rp.haveMe && s_Rp.interior == 0; }
static void RpRadarTick(uint64_t now)
{
    bool want = RpRadarOn() && !Game::IsPaused();
    if(want && (!s_Rp.radarHidden || now - s_Rp.lastRadarHide > 1000)) { Game::ShowRadar(false); s_Rp.radarHidden = true; s_Rp.lastRadarHide = now; }
    else if(!want && s_Rp.radarHidden && !Game::IsPaused()) { if(s_Rp.view == 2) Game::ShowRadar(true); s_Rp.radarHidden = false; }
}
static const char* RpPlaceGlyph(uint8_t k)
{
    switch(k)
    {
        case RPK_SPAWN: return "";   case RPK_JOBS: return "J";  case RPK_BANK: return "$";  case RPK_SHOP: return "S";
        case RPK_FOOD: return "F";   case RPK_CLOTHES: return "C"; case RPK_CARS: return "V"; case RPK_TECH: return "T";
        case RPK_GUNS: return "G";   case RPK_HOSPITAL: return "H"; case RPK_POLICE: return "P"; case RPK_ATM: return "$";
        case RPK_PETS: return "D";   case RPK_ARMY: return "A";  case RPK_AIRPORT: return "A";
        default: return "";
    }
}
static uint32_t RpPlacePinCol(uint8_t k)
{
    switch(k)
    {
        case RPK_SPAWN: return 0x22C55EFF;  case RPK_JOBS: return 0x0EA5E9FF; case RPK_BANK: case RPK_ATM: return 0x16A34AFF;
        case RPK_SHOP: case RPK_FOOD: return 0xF97316FF; case RPK_CLOTHES: return 0xEC4899FF; case RPK_CARS: return 0x6366F1FF;
        case RPK_TECH: return 0x8B5CF6FF;   case RPK_GUNS: return 0x78716CFF; case RPK_HOSPITAL: return 0xEF4444FF;
        case RPK_POLICE: return 0x2563EBFF; case RPK_PETS: return 0xA16207FF; case RPK_ARMY: return 0x4D7C0FFF;
        case RPK_AIRPORT: return 0x0284C7FF; default: return 0x64748BFF;
    }
}
static float s_RpRadarRange = 200.0f;
static void RpDrawRadar(uint64_t nowMs)
{
    if(!RpRadarOn() || Game::IsPaused()) return;
    int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float size = ds.y * 0.24f;
    const ImVec2 p0(ds.x * 0.012f, ds.y * 0.915f - size);
    SaRadarView v{};
    v.meX = me.x; v.meY = me.y; v.meZ = me.z; v.meHeading = Game::GetCharHeading(ped); v.mode = settings.radarStyle;
    Vec3 cp, cl; if(Game::GetCameraPos(cp, cl)) { v.camFx = cl.x - cp.x; v.camFy = cl.y - cp.y; }
    // zoom out with the speed (on foot ~200 m, fast car ~380 m), eased
    float sp = 0.0f;
    if(Game::IsCharInAnyCar(ped)) { int car = Game::GetCarCharIsIn(ped); Vec3 mv; if(car > 0 && Game::GetMoveSpeed(car, true, mv)) sp = sqrtf(mv.x * mv.x + mv.y * mv.y); }
    float want = 200.0f + SarClamp(sp, 0, 45) * 4.0f;
    s_RpRadarRange += (want - s_RpRadarRange) * 0.04f;
    v.range = s_RpRadarRange;
    SaPin pins[96]; int n = 0;
    for(int i = 0; i < RP_PLACE_COUNT && n < 70; ++i)
    {
        const RpPlace& pl = g_RpPlaces[i];
        float dx = pl.x - me.x, dy = pl.y - me.y; if(dx * dx + dy * dy > 900.0f * 900.0f) continue;
        SaPin& q = pins[n++]; q.x = pl.x; q.y = pl.y; q.z = pl.z; q.rgba = RpPlacePinCol(pl.kind); q.style = 0;
        MP_CopyStr(q.glyph, RpPlaceGlyph(pl.kind), sizeof(q.glyph));
    }
    if(s_Rp.haveMe && s_Rp.me.house < RP_HOUSE_COUNT && n < 96)
    {
        const RpHouse& h = g_RpHouses[s_Rp.me.house];
        SaPin& q = pins[n++]; q.x = h.x; q.y = h.y; q.z = h.z; q.rgba = 0xDC2626FF; q.style = 0; MP_CopyStr(q.glyph, "H", sizeof(q.glyph));   // my house
    }
    for(int i = 0; i < MP_MAX_PLAYERS && n < 92; ++i)
    {
        if(i == myId || !players[i].active || players[i].ped <= 0) continue;
        Vec3 pp; if(!Game::GetCharPos(players[i].ped, pp)) continue;
        SaPin& q = pins[n++]; q.x = pp.x; q.y = pp.y; q.z = pp.z; q.rgba = (players[i].color & 0xFFFFFF00) | 0xFF; q.style = 1; q.glyph[0] = 0;
    }
    if(s_Rp.callBlip && n < 96) { SaPin& q = pins[n++]; q.x = s_Rp.callPos.x; q.y = s_Rp.callPos.y; q.z = s_Rp.callPos.z; q.rgba = 0xEF4444FF; q.style = 2; MP_CopyStr(q.glyph, "!", sizeof(q.glyph)); }
    if(s_Rp.gpsBlip && n < 96)  { SaPin& q = pins[n++]; q.x = s_Rp.gpsPos.x;  q.y = s_Rp.gpsPos.y;  q.z = s_Rp.gpsPos.z;  q.rgba = RP_SKY;     q.style = 2; q.glyph[0] = 0; }
    if(s_Rp.tgtOn && n < 96)    { SaPin& q = pins[n++]; q.x = s_Rp.tgt.x;     q.y = s_Rp.tgt.y;     q.z = s_Rp.tgt.z;     q.rgba = s_Rp.tgtCol | 0xFF; q.style = 2; q.glyph[0] = 0; }
    SaArea areas[24]; int na = 0;
    for(const RpZone& z : g_RpGreen) { if(na >= 24) break; areas[na++] = { z.x - z.r, z.y - z.r, z.x + z.r, z.y + z.r, 0x22C55E38 }; }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    SaRadarDraw(dl, p0, size, v, pins, n, areas, na, (float)(nowMs % 100000) / 1000.0f);
    SaRadarDrawChip(dl, p0, size, v.mode);
    // tap the chip: 2D <-> 3D (a tiny window so the tap does not reach the game)
    ImVec2 a, b; SaRadarChipRect(p0, size, a, b);
    ImGui::SetNextWindowPos(a); ImGui::SetNextWindowSize(ImVec2(b.x - a.x, b.y - a.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##RpRadarChip", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
    if(ImGui::InvisibleButton("##rrchip", ImVec2(b.x - a.x, b.y - a.y))) { settings.radarStyle = settings.radarStyle == 2 ? 1 : 2; MP::SaveConfig(); }
    ImGui::End();
    ImGui::PopStyleVar();
}

static void RpDrawTalk(float now)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    // subtitles: bottom center, "Citizen: text" like the game's mission subtitles
    if(s_Rp.subAt && MP_NowMs() - s_Rp.subAt < (uint64_t)s_Rp.subMs)
    {
        char t[200]; snprintf(t, sizeof(t), "%s%s%s", s_Rp.subWho, s_Rp.subWho[0] ? ": " : "", s_Rp.sub);
        float fs = UI::S(30), wrap = ds.x * 0.6f;
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, wrap, t);
        ImVec2 p(ds.x * 0.5f - ts.x * 0.5f, ds.y * 0.70f);
        fg->AddRectFilled(ImVec2(p.x - UI::S(16), p.y - UI::S(8)), ImVec2(p.x + ts.x + UI::S(16), p.y + ts.y + UI::S(8)), IM_COL32(0, 0, 0, 150), UI::S(10));
        fg->AddText(UI::Font(), fs, ImVec2(p.x + UI::S(2), p.y + UI::S(2)), IM_COL32(0, 0, 0, 255), t, nullptr, wrap);
        fg->AddText(UI::Font(), fs, p, IM_COL32(255, 255, 255, 255), t, nullptr, wrap);
        // the "Citizen:" part in sky blue
        if(s_Rp.subWho[0]) { char w[40]; snprintf(w, sizeof(w), "%s:", s_Rp.subWho); fg->AddText(UI::Font(), fs, p, UI::FromRGBA(RP_SKY), w); }
    }
    // 3.27: a way out of every emote, always on screen while one plays
    if(s_Rp.emote >= 0 && s_Rp.view == 2 && !s_Rp.phone && !s_Rp.emoteTalk)
    {
        const float sw = UI::S(260), sh = UI::S(58);
        BcwBeginOverlay("##RpEmoteStop", ImVec2(ds.x * 0.5f - sw * 0.5f - UI::S(18), ds.y * 0.60f), ImVec2(sw + UI::S(36), sh + UI::S(28)), 0.0f);
        if(BcwTeamButton("STOP EMOTE##rpes", 0xDC2626FF, ImVec2(sw, sh), false, false)) RpStopEmote();
        BcwEndOverlay();
    }
    if(!s_Rp.talkPed || s_Rp.talkStage == 0 || s_Rp.talkStage == 3) return;
    // 3D marker over the person
    Vec3 pp; if(Game::GetCharPos(s_Rp.talkPed, pp)) BcwLabel3D(ImGui::GetBackgroundDrawList(), { pp.x, pp.y, pp.z + 1.25f }, s_Rp.talkStage == 1 ? "[ ! ]" : "...", RP_SKY, 34.0f, 6.0f);
    // 3.29: Talk / Nevermind / topics - no background, white outline, one button per line, a bit right of centre
    const float bw = UI::S(330), gap = UI::S(14);
    if(s_Rp.talkStage == 1)
    {
        static const char* const kL[2] = { "Talk##rpt", "Nevermind##rpn" };
        int h = RpOutlineColumn("##RpTalk", kL, 2, bw, UI::S(62), gap, ds.y * 0.42f);
        if(h == 0) RpTalkChoose(100); else if(h == 1) RpTalkChoose(-1);
    }
    else if(s_Rp.talkStage == 2)
    {
        static char lb[RP_TOPICS + 1][64]; const char* L[RP_TOPICS + 1];
        for(int i = 0; i < RP_TOPICS; ++i) { snprintf(lb[i], sizeof(lb[i]), "%s##tp%d", kRpTopics[i].option, i); L[i] = lb[i]; }
        snprintf(lb[RP_TOPICS], sizeof(lb[RP_TOPICS]), "Goodbye##tpb"); L[RP_TOPICS] = lb[RP_TOPICS];
        int h = RpOutlineColumn("##RpTalk2", L, RP_TOPICS + 1, bw, UI::S(54), UI::S(10), ds.y * 0.40f, 0.85f);
        if(h >= 0 && h < RP_TOPICS) RpTalkChoose(h); else if(h == RP_TOPICS) RpTalkChoose(-1);
    }
    (void)now;
}

// ---- lobby / spawn -----------------------------------------------------------------------------
static void RpHudOff(uint64_t now, bool force)
{
    if(force || now - s_Rp.lastHudOff > 1000) { Game::ShowGameHud(false); Game::SetPlayerControl(false); s_Rp.lastHudOff = now; }
}
static void RpPark(int ped)
{
    if(ped <= 0) return;
    if(Game::IsCharInAnyCar(ped)) { Vec3 o = { kRpLobbyLook.x, kRpLobbyLook.y, kRpLobbyLook.z }; Game::WarpOutOfCar(ped, o); }
    RpSetInterior(0);
    Game::SetMaxWanted(0); Game::ClearWanted();
    Game::PreloadArea(kRpLobbyLook, true);
    Game::FreezeChar(ped, false);
    Game::PlaceChar(ped, { kRpLobbyCam.x, kRpLobbyCam.y, -100.0f });
    Game::FreezeChar(ped, true);
    Game::SetCharVisible(ped, false);
    Game::SetCharGodMode(ped, true);
    Game::SetFixedCamera(kRpLobbyCam, kRpLobbyLook);
    RpHudOff(MP_NowMs(), true);
}
static void RpEnterLobby(int ped)
{
    logger->Info("RP: lobby (Mega Vil Plaza)"); fflush(nullptr);
    if(s_nSpawnedCar > 0)
    {
        if(ped > 0 && Game::IsCharInAnyCar(ped) && Game::GetCarCharIsIn(ped) == s_nSpawnedCar) { Vec3 o; Game::GetCharPos(ped, o); o.z += 1.0f; Game::WarpOutOfCar(ped, o); }
        if(Game::GetDriverOfCar(s_nSpawnedCar) <= 0 || Game::GetDriverOfCar(s_nSpawnedCar) == ped) Game::DeleteCar(s_nSpawnedCar); else Game::ReleaseCar(s_nSpawnedCar);
        s_nSpawnedCar = 0;
    }
    RpPark(ped);
    s_Rp.view = 1;
}
static void RpSyncMoney()
{
    if(!s_Rp.haveMe || s_Rp.view != 2 || !Game::IsReady() || Game::IsPaused()) return;
    int diff = s_Rp.me.cash - Game::GetMoney();
    if(diff) Game::AddMoney(diff);
}
// home = the house spawn (never the hub); else the Lobby Hub (Mega Vil Plaza)
static void RpSpawnNow(int ped)
{
    logger->Info("RP: spawn"); fflush(nullptr);
    if(ped <= 0) return;
    Vec3 at = { g_RpPlaces[RP_PLAZA].x, g_RpPlaces[RP_PLAZA].y, g_RpPlaces[RP_PLAZA].z }; float h = 180.0f + (float)(rand() % 40) - 20.0f;
    bool home = RpHomePoint(at, h);
    if(!home) { at.x += (float)(rand() % 9) - 4.0f; at.y += (float)(rand() % 9) - 4.0f; }
    Game::SetMaxWanted(0); Game::ClearWanted();
    if(Game::IsCharInAnyCar(ped)) { Vec3 o = { at.x, at.y, at.z + 1.0f }; Game::WarpOutOfCar(ped, o); }
    RpSetInterior(0);
    Game::PreloadArea(at, true);
    Game::FreezeChar(ped, false);
    if(s_Rp.haveMe && s_Rp.me.skin) { Game::SetLocalModel(s_Rp.me.skin); ped = Game::LocalPed(); if(ped <= 0) return; }
    Game::SetCharVisible(ped, !s_Cheat.invisible);
    Game::SetCharGodMode(ped, false);
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::TeleportGround(ped, at, h);
    s_Rp.view = 2;
    RpMakeBlips();
    RpSyncMoney();
    RpSend(RPC_SPAWNED);
    if(home) SysMsg(RP_SKY, "[MEGA VIL] Welcome home - %s.", g_RpHouses[s_Rp.me.house].addr);
    else SysMsg(RP_SKY, "[MEGA VIL] You arrived at the Lobby Hub (Mega Vil Plaza). Buy a house and you will always spawn at home.");
    if(s_Rp.haveMe && s_Rp.me.jailS) { RpSetInterior(RP_JAIL_AREA); Game::Teleport(ped, kRpJailCell, 0); }
}
// 3.26: the bag / PANEL (mp_rp_inv.inc)
static void RpInvOnPacket(const uint8_t* d, int len);
static void RpInvGot(uint8_t id, int n, const char* why);
static void RpInvReset();
static void RpPanelToggle();
static void RpInvShow(int win);
static void RenderRpInv();
static void RpShutdown()
{
    logger->Info("RP: left"); fflush(nullptr);
    RpInvReset();   // 3.26
    if(!s_Rp.engaged) { s_bRpJoin = false; return; }
    RpTalkRelease();
    RpClearBlips();
    if(s_Rp.radarHidden && Game::IsReady()) { Game::ShowRadar(true); s_Rp.radarHidden = false; }   // 3.27
    if(s_Rp.petObj) Game::DeleteWorldObject(s_Rp.petObj);
    int ped = Game::LocalPed();
    if(ped > 0 && Game::IsReady())
    {
        Game::EnterArea(0);
        Game::FreezeChar(ped, false);
        Game::SetCharVisible(ped, !s_Cheat.invisible);
        Game::SetCharGodMode(ped, false);
        if(s_Rp.view == 1 || s_Rp.interior) Game::Teleport(ped, kGroveStreet, kGroveHeading);
    }
    Game::RestoreCamera();
    Game::ShowGameHud(true); Game::SetPlayerControl(true);
    Game::SetMaxWanted(6);
    s_Rp = RpClientState();
    s_bRpJoin = false;
}
static bool RpJoinTakeover(int ped)
{
    logger->Info("RP: join takeover"); fflush(nullptr);
    if(!s_bRpJoin && !RpOnServer()) return false;
    s_bRpJoin = false;
    s_Rp.engaged = true; s_Rp.connectedAt = s_Rp.connectedAt ? s_Rp.connectedAt : MP_NowMs();
    RpEnterLobby(ped);
    return true;
}
static void RpJoinCam(Vec3& pos, Vec3& look) { pos = kRpLobbyCam; look = kRpLobbyLook; }

// ---- packets -----------------------------------------------------------------------------------
static void RpPhoneOpen(int app);
static void RpOnPacket(const uint8_t* d, int len)
{
    if(len < 1) return;
    uint64_t now = MP_NowMs();
    int ped = Game::LocalPed();
    switch(d[0])
    {
    case PKT_RP_STATE:
        if(len < (int)offsetof(PktRpState, market)) return;   // 3.29: a 3.28 server sends no market
        memset(&s_Rp.st, 0, sizeof(PktRpState));
        memcpy(&s_Rp.st, d, len < (int)sizeof(PktRpState) ? len : sizeof(PktRpState)); s_Rp.haveState = true; s_Rp.st.evText[sizeof(s_Rp.st.evText) - 1] = 0;
        break;
    case PKT_RP_PLAYERS:
    {
        if(len < 4) return;
        int n = d[1], page = d[2], pages = d[3]; if(n > RP_ROWS_WIRE) n = RP_ROWS_WIRE;
        if(len < 4 + n * (int)sizeof(RpRow)) return;
        if(page == 0) memset(s_Rp.rowSeen, 0, sizeof(s_Rp.rowSeen));
        const RpRow* rows = (const RpRow*)(d + 4);
        for(int i = 0; i < n; ++i) { RpRow r; memcpy(&r, &rows[i], sizeof(r)); if(r.id < MP_MAX_PLAYERS) { s_Rp.row[r.id] = r; s_Rp.haveRow[r.id] = true; s_Rp.rowSeen[r.id] = true; } }
        if(page + 1 >= pages) for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(!s_Rp.rowSeen[i]) s_Rp.haveRow[i] = false;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_Rp.haveRow[i] && players[i].active) players[i].score = s_Rp.row[i].level;
        break;
    }
    case PKT_RP_ME:
    {
        if(len < (int)offsetof(PktRpMe, invest)) return;   // 3.29: a 3.28 server sends the shorter one
        if(!s_Rp.haveMe) RpCardsSend();
        PktRpMe old = s_Rp.me; bool had = s_Rp.haveMe;
        memset(&s_Rp.me, 0, sizeof(PktRpMe));
        memcpy(&s_Rp.me, d, len < (int)sizeof(PktRpMe) ? len : sizeof(PktRpMe)); s_Rp.me.spouse[sizeof(s_Rp.me.spouse) - 1] = 0; s_Rp.haveMe = true;
        s_nMyScore = s_Rp.me.level;
        RpSyncMoney();
        if(s_Rp.me.jobOn && !s_Rp.tgtOn && (s_Rp.me.target[0] != 0.0f || s_Rp.me.target[1] != 0.0f)) RpSetTarget(s_Rp.me.target, g_RpJobs[s_Rp.me.job].col, g_RpJobs[s_Rp.me.job].name);
        if(!s_Rp.me.jobOn && s_Rp.tgtOn && had && old.jobOn) RpClearTarget();
        if(had && !old.cuffed && s_Rp.me.cuffed) RpBanner(RP_RED, "YOU ARE CUFFED");
        break;
    }
    case PKT_RP_INV:
        RpInvOnPacket(d, len);
        break;
    case PKT_RP_DROPS:   // 3.30: things on the ground
        RpDropsOnPacket(d, len);
        break;
    case PKT_RP_CARD:
    {
        if(len < (int)sizeof(PktRpCard)) return;
        PktRpCard c; memcpy(&c, d, sizeof(c)); c.key[sizeof(c.key) - 1] = 0;
        if(c.ver == RP_CARD_VER && c.key[0]) RpCardStore(c);
        break;
    }
    case PKT_RP_EVENT:
    {
        if(len < (int)sizeof(PktRpEvent)) return;
        PktRpEvent e; memcpy(&e, d, sizeof(e)); e.text[sizeof(e.text) - 1] = 0;
        switch(e.ev)
        {
        case RPV_TEXT:
            if(e.a == 1) { s_Rp.askEmote = e.b; s_Rp.askFrom = e.who; s_Rp.askAt = now; }
            else RpBanner((uint32_t)e.val ? (uint32_t)e.val : RP_SKY, "%s", e.text);
            break;
        case RPV_SMS:
        {
            RpSms m; m.number = e.val; m.mine = e.a == 1; MP_CopyStr(m.text, e.text, sizeof(m.text)); m.at = now;
            s_Rp.sms.push_back(m); if(s_Rp.sms.size() > 200) s_Rp.sms.erase(s_Rp.sms.begin());
            if(!m.mine) { ++s_Rp.unreadSms; SysMsg(0x7DD3FCFF, "[SMS] %s (%d): %s", e.who < MP_MAX_PLAYERS ? RpName(e.who) : "?", e.val, e.text); }
            break;
        }
        case RPV_RING:
            s_Rp.ringFrom = e.who < MP_MAX_PLAYERS ? e.who : -1; s_Rp.ringOut = e.a == 1; s_Rp.ringNum = e.val; s_Rp.ringAt = now;
            MP_CopyStr(s_Rp.ringName, e.text, sizeof(s_Rp.ringName));
            if(!s_Rp.ringOut) RpPhoneOpen(-2);   // the phone pops up with Answer / Decline
            break;
        case RPV_CALL_ON:
            s_Rp.callWith = e.who < MP_MAX_PLAYERS ? e.who : -1; s_Rp.callNum = e.val; s_Rp.callStart = now; s_Rp.ringFrom = -1;
            MP_CopyStr(s_Rp.callName, e.text, sizeof(s_Rp.callName));
            { RpCallLog l; l.number = e.val; MP_CopyStr(l.name, e.text, sizeof(l.name)); l.kind = s_Rp.ringOut ? 1 : 0; l.at = now; s_Rp.calls.push_back(l); }
            SysMsg(RP_SKY, "[PHONE] Connected with %s - talk with the voice chat (mic button).", e.text);
            break;
        case RPV_CALL_OFF:
            if(s_Rp.ringFrom >= 0 && !s_Rp.ringOut && s_Rp.callWith < 0) { RpCallLog l; l.number = s_Rp.ringNum; MP_CopyStr(l.name, s_Rp.ringName, sizeof(l.name)); l.kind = 2; l.at = now; s_Rp.calls.push_back(l); }
            if(s_Rp.callWith >= 0 || s_Rp.ringFrom >= 0) SysMsg(RP_OOC_COL, "[PHONE] %s", e.text[0] ? e.text : "Call ended.");
            s_Rp.callWith = -1; s_Rp.ringFrom = -1;
            break;
        case RPV_TARGET:
            if(e.a == 0) RpClearTarget();
            else if(e.a == 1) RpSetTarget(e.fval, (uint32_t)e.val, e.text);
            else
            {
                if(s_Rp.callBlip) Game::RemoveBlip(s_Rp.callBlip);
                s_Rp.callPos = { e.fval[0], e.fval[1], e.fval[2] }; s_Rp.callAt = now; MP_CopyStr(s_Rp.callText, e.text, sizeof(s_Rp.callText));
                s_Rp.callBlip = Game::AddCoordBlip(s_Rp.callPos, (uint32_t)e.val | 0xFF, 2);
            }
            break;
        case RPV_PAY:
            s_Rp.me.cash += e.val; s_Rp.payFx = e.val; MP_CopyStr(s_Rp.payWhy, e.text, sizeof(s_Rp.payWhy)); s_Rp.payAt = now;
            RpSyncMoney();
            break;
        case RPV_HEAL:
            if(ped > 0 && s_Rp.view == 2) { if(e.a) Game::SetCharHealth(ped, e.a); if(e.b) { int ar = Game::GetCharArmour(ped); if(ar < e.b) Game::AddArmour(ped, e.b - ar); } }
            if(e.who < MP_MAX_PLAYERS && e.who != myId) SysMsg(RP_GREEN, "%s treated you.", RpName(e.who));
            break;
        case RPV_REPAIR:
            if(ped > 0 && Game::IsCharInAnyCar(ped)) { Game::FixCar(Game::GetCarCharIsIn(ped)); SysMsg(RP_GREEN, "[MECHANIC] Vehicle repaired."); }
            else SysMsg(RP_YELLOW, "Sit in the vehicle to get it repaired.");
            break;
        case RPV_JAIL:
            if(ped <= 0) break;
            if(e.val > 0) { RpStopEmote(); if(Game::IsCharInAnyCar(ped)) { Vec3 o; Game::GetCharPos(ped, o); Game::WarpOutOfCar(ped, o); } Game::RemoveAllWeapons(ped); RpSetInterior(RP_JAIL_AREA); Game::Teleport(ped, kRpJailCell, 0); RpBanner(RP_RED, "JAILED  -  %d:%02d", e.val / 60, e.val % 60); }
            else { RpSetInterior(0); Game::TeleportGround(ped, { g_RpPlaces[3].x, g_RpPlaces[3].y, g_RpPlaces[3].z }, 90.0f); RpBanner(RP_GREEN, "RELEASED"); }
            break;
        case RPV_CUFF:
            if(ped > 0) { if(e.a) { RpStopEmote(); Game::RemoveAllWeapons(ped); } else Game::ClearTasksNow(ped); s_Rp.cuffAnim = false; }
            break;
        case RPV_SKIN:
            if(e.val > 0 && s_Rp.view == 2) { Game::SetLocalModel(e.val); RpSyncMoney(); }
            break;
        case RPV_WEAPON:
            if(ped > 0 && s_Rp.view == 2 && e.a) Game::GiveAndSelectWeapon(ped, e.a);
            break;
        case RPV_VEHICLE:
            if(s_Rp.view == 2 && e.val >= 400 && e.val < 612 && SpawnVehicleForMe(e.val, false)) SysMsg(RP_SKY, "[GARAGE] Your %s is here.", kVehNames[e.val - 400]);
            break;
        case RPV_JETPACK:
            if(s_Rp.view == 2) Game::GiveLocalJetpack();
            break;
        case RPV_FEED:
        {
            RpPost p; p.number = e.val; MP_CopyStr(p.name, e.who < MP_MAX_PLAYERS ? RpName(e.who) : "?", sizeof(p.name)); MP_CopyStr(p.text, e.text, sizeof(p.text)); p.at = now;
            s_Rp.feed.push_back(p); if(s_Rp.feed.size() > 80) s_Rp.feed.erase(s_Rp.feed.begin());
            break;
        }
        case RPV_NEWS:
            RpBanner(RP_SKY, "%s", e.text);
            break;
        case RPV_TELE:
            if(ped > 0) Game::TeleportGround(ped, { e.fval[0], e.fval[1], e.fval[2] }, 0);
            break;
        case RPV_DISARM:
            if(ped > 0) Game::RemoveAllWeapons(ped);
            if(e.a == 1 && s_Rp.haveMe) { int sk = e.val ? e.val : (s_Rp.me.skin ? s_Rp.me.skin : settings.skin); if(sk > 0) Game::SetLocalModel(sk); }
            break;
        case RPV_GOT:   // 3.26: "Received 10x" card
            RpInvGot(e.a, e.val, e.text);
            break;
        case RPV_ITEM:
            if(e.b == 1)
            {   // shared emote with e.who
                int other = e.who < MP_MAX_PLAYERS ? players[e.who].ped : 0;
                if(ped > 0 && other > 0) Game::TaskFaceChar(ped, other);
                RpPlayEmote(e.a < RP_EMOTE_COUNT ? e.a : -1);
            }
            else if(ped > 0 && !Game::IsCharInAnyCar(ped)) { RpPlayEmote(RpFindEmote(e.b == 2 ? "drink" : "eat")); RpBanner(RP_GREEN, "%s", e.text); }
            break;
        }
        break;
    }
    }
}

// ---- radar: greenzones -------------------------------------------------------------------------
static int RpRadarZones(Game::RadarZone* out, int maxOut)
{
    if(!RpOnServer() || s_Rp.view != 2) return 0;
    int n = 0;
    for(const RpZone& z : g_RpGreen) { if(n >= maxOut) break; out[n++] = { z.x - z.r, z.y - z.r, z.x + z.r, z.y + z.r, 0x22C55E60 }; }
    return n;
}

// ---- game tick ---------------------------------------------------------------------------------
static bool RpMayArm()
{
    if(!s_Rp.haveMe) return false;
    const PktRpMe& m = s_Rp.me;
    return m.duty && RpJobArmed(m.job) && m.level >= g_RpJobs[m.job].minLevel && m.newbieS == 0 && !m.cuffed && !m.jailS;
}
static void RpProcess(int ped, uint64_t now)
{
    if(!RpOnServer())
    {
        if(s_Rp.engaged) RpShutdown();
        RpLifeProcess(ped, now, false);   // 3.29
        return;
    }
    if(!s_Rp.engaged)
    {
        if(JoinFlowActive()) return;
        s_Rp.engaged = true; s_Rp.connectedAt = now;
        RpEnterLobby(ped);
    }
    RpNoWanted(now);
    RpLifeProcess(ped, now, true);   // 3.29: ATM machines, plaza info people, welcome
    static uint64_t s_Last = 0; float dt = s_Last ? (now - s_Last) / 1000.0f : 0.0f; if(dt > 0.25f) dt = 0.25f; s_Last = now;
    if(ped <= 0) return;
    if(s_Rp.view == 1)
    {
        RpHudOff(now, false);
        if(now % 2000 < 40) { Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false); }
        // a citizen with a house never sees the hub: straight home as soon as the city knows it
        if(s_Rp.haveMe && RpHasHome() && now - s_Rp.connectedAt > 1200) RpSpawnNow(ped);
        return;
    }
    // ---- playing ----
    if(s_bLocalDead) { s_Rp.wasDead = true; RpStopEmote(); RpTalkRelease(); }
    else if(s_Rp.wasDead)
    {   // the game put us in the hospital. With a house: wake up at home instead.
        s_Rp.wasDead = false; s_Rp.respawnAt = now; s_Rp.needHome = RpHasHome();
        if(s_Rp.haveMe && s_Rp.me.skin) Game::SetLocalModel(s_Rp.me.skin);
        RpSend(RPC_SPAWNED);
    }
    if(s_Rp.needHome && now - s_Rp.respawnAt > 1500)
    {
        s_Rp.needHome = false;
        Vec3 at; float h; if(RpHomePoint(at, h)) { Game::PreloadArea(at, true); Game::TeleportGround(ped, at, h); SysMsg(RP_SKY, "[MEGA VIL] You woke up at home."); }
    }
    // jail: stay in the cell
    if(s_Rp.haveMe && s_Rp.me.jailS)
    {
        Vec3 p; Game::GetCharPos(ped, p);
        float dx = p.x - kRpJailCell.x, dy = p.y - kRpJailCell.y;
        if(dx * dx + dy * dy > 8.0f * 8.0f || s_Rp.interior != RP_JAIL_AREA) { RpSetInterior(RP_JAIL_AREA); Game::Teleport(ped, kRpJailCell, 0); }
    }
    // cuffed: hands behind, no driving, no weapons
    if(s_Rp.haveMe && s_Rp.me.cuffed)
    {
        if(Game::IsCharInAnyCar(ped) && Game::GetDriverOfCar(Game::GetCarCharIsIn(ped)) == ped) { Vec3 o; Game::GetCharPos(ped, o); o.z += 0.5f; Game::WarpOutOfCar(ped, o); }
        if(!s_Rp.cuffAnim && !Game::IsCharInAnyCar(ped)) { s_Rp.cuffAnim = Game::PlayAnimLoop(ped, "handsup", "ped"); }
    }
    else s_Rp.cuffAnim = false;
    // peace: harmful weapons only for on-duty police / soldiers
    if(now - s_Rp.lastDisarm > 400)
    {
        s_Rp.lastDisarm = now;
        int w = Game::GetCharWeapon(ped);
        if(w > 0 && !RpWeaponIsHarmless(w) && !RpMayArm())
        {
            Game::RemoveAllWeapons(ped);
            static uint64_t s_Told = 0; if(now - s_Told > 15000) { s_Told = now; SysMsg(COL_YELLOW, "[MEGA VIL] Weapons are for on-duty police and soldiers only. Mega Vil is peaceful."); }
        }
    }
    // hunger / thirst at 0: slowly weaker
    if(s_Rp.haveMe && (s_Rp.me.hunger == 0 || s_Rp.me.thirst == 0) && now - s_Rp.lastNeed > 8000)
    {
        s_Rp.lastNeed = now; int hp = Game::GetCharHealth(ped); if(hp > 25) Game::SetCharHealth(ped, hp - 1);
    }
    // stamina (sprint drains, rest refills)
    {
        float sp = Game::IsCharInAnyCar(ped) ? 0.0f : Game::GetCharSpeed(ped);
        if(sp > 6.0f) s_Rp.stamina -= dt * 9.0f; else if(sp > 2.5f) s_Rp.stamina += dt * 2.0f; else s_Rp.stamina += dt * 7.0f;
        if(s_Rp.stamina < 0) s_Rp.stamina = 0; if(s_Rp.stamina > 100) s_Rp.stamina = 100;
    }
    // job target reached
    if(s_Rp.tgtOn && now - s_Rp.lastArrive > 2500)
    {
        Vec3 p; Game::GetCharPos(ped, p);
        float dx = p.x - s_Rp.tgt.x, dy = p.y - s_Rp.tgt.y;
        if(dx * dx + dy * dy < 9.0f * 9.0f) { s_Rp.lastArrive = now; RpSend(RPC_ARRIVE); }
    }
    if(s_Rp.callBlip && now - s_Rp.callAt > 180000) { Game::RemoveBlip(s_Rp.callBlip); s_Rp.callBlip = 0; }
    if(now - s_Rp.lastMoney > 1000) { s_Rp.lastMoney = now; RpSyncMoney(); }
    RpEmoteTick(ped, now);
    RpPedTick(ped, now);
    RpRadarTick(now);   // 3.27
    RpPetUpdate(ped, dt);
}

// ---- commands ----------------------------------------------------------------------------------
static void RpShowHelp()
{
    ShowDialog(9401, 0, "{38BDF8}MEGA VIL FUTURISTIC - Help",
        "{FFFFFF}Roleplay only: be your character. Mega Vil is peaceful - only on-duty police and soldiers may use force.\n"
        "Open the phone: PANEL > Phone or {38BDF8}/phone{FFFFFF}.\n"
        "PANEL (the 4-square icon on the right, or {38BDF8}/panel{FFFFFF}): vehicle, documents, player info + bag, phone.\n\n"
        "{38BDF8}Talk:{FFFFFF} normal chat (20 m), /me /do /ame /try /b /s /l /w /o /n /ad /feed /r /d /m /gov\n"
        "{38BDF8}Money:{FFFFFF} /pay /deposit /withdraw /transfer /balance /atm\n"
        "{38BDF8}Jobs:{FFFFFF} /jobs /job /quitjob /work /stopwork /duty /myjob\n"
        "{38BDF8}Phone:{FFFFFF} /call /sms /answer /hangup /number /contacts /911 /taxi\n"
        "{38BDF8}Home:{FFFFFF} /houses /buyhouse /sellhouse /home /setspawn /myhouse\n"
        "{38BDF8}Shops:{FFFFFF} /shop /food /clothes /cars /tech /ammu /lotto   {38BDF8}Bag:{FFFFFF} /inv /docs /panel\n"
        "{38BDF8}Garage & pets:{FFFFFF} /garage /v [n] /sellveh [n] /pets /pet [n] /petoff /jetpack\n"
        "{38BDF8}Police:{FFFFFF} /cuff /uncuff /arrest /fine /su /frisk /mdc   {38BDF8}Medic:{FFFFFF} /heal   {38BDF8}Mechanic:{FFFFFF} /repair   {38BDF8}Lawyer:{FFFFFF} /defend\n"
        "{38BDF8}Fun:{FFFFFF} /e [emote] /emotes /e c /dance /accept /flip /dice /games /event /joinevent\n"
        "{38BDF8}Info:{FFFFFF} /stats /id /time /gps /places /rules /levels /players /greenzones /talk /rp",
        "Close", "", nullptr);
}
static void RpShowRules()
{
    ShowDialog(9402, 0, "{38BDF8}Mega Vil - Rules",
        "{FFFFFF}1. Roleplay at all times. Out of character only with /b or /o.\n"
        "2. No deathmatch: the city is peaceful. Police / soldiers on duty use force only when the roleplay needs it.\n"
        "3. No cheats, no mods that give an advantage, no bug abuse.\n"
        "4. Respect everyone - new citizens too. Help them in /n.\n"
        "5. No powergaming / metagaming. Your character only knows what it saw.\n"
        "6. Greenzones (green squares on the radar) are safe for everyone.\n"
        "7. Have fun in the city of the future.",
        "Close", "", nullptr);
}
static void RpPlacesPick(int response, int item, const char*, int)
{
    if(!response || item < 0 || item >= RP_PLACE_COUNT) return;
    RpSetGps({ g_RpPlaces[item].x, g_RpPlaces[item].y, g_RpPlaces[item].z }, g_RpPlaces[item].name);
}
static void RpShowPlaces()
{
    std::string s; for(int i = 0; i < RP_PLACE_COUNT; ++i) { s += g_RpPlaces[i].name; s += "\n"; }
    ShowDialog(9403, 2, "{38BDF8}GPS - Mega Vil", s.c_str(), "Route", "Close", RpPlacesPick);
}
static void RpShowEmotes()
{
    std::string s = "Command\tEmote\tType\n"; char b[128];
    for(int i = 0; i < RP_EMOTE_COUNT; ++i) { snprintf(b, sizeof(b), "/e %s\t%s\t%s\n", g_RpEmotes[i].cmd, g_RpEmotes[i].label, g_RpEmoteCats[g_RpEmotes[i].cat]); s += b; }
    ShowDialog(9404, 5, "{38BDF8}Emotes & dances (rpemotes)", s.c_str(), "Close", "", [](int r, int item, const char*, int) { if(r && item >= 0 && item < RP_EMOTE_COUNT && g_RpEmotes[item].cat != RPE_SHARED) RpPlayEmote(item); });
}
static void RpShowStatsLocal()
{
    RpSend(RPC_STATS);
}
static bool RpParseIdText(const char* arg, int& id, const char** rest)
{
    char* end = nullptr; long v = strtol(arg, &end, 10);
    if(end == arg || v < 0 || v >= MP_MAX_PLAYERS) return false;
    id = (int)v; while(end && *end == ' ') ++end; if(rest) *rest = end ? end : ""; return true;
}
static void RpChat(uint8_t kind, const char* text, int id = 0)
{
    if(!text || !*text) { SysMsg(COL_YELLOW, "Type something after the command."); return; }
    RpSend(RPC_CHAT, kind, (uint8_t)id, 0, text);
}
static void RpPhoneOpenShop(int cat);
static bool RpCommand(const char* cmd, const char* arg)
{
    if(!RpOnServer()) return false;
    const uint64_t now = MP_NowMs();
    int id = -1; const char* rest = "";
    #define IS(x) (!strcmp(cmd, x))
    if(RpLifeCommand(cmd, arg)) return true;   // 3.29: mp_rp_life.inc
    // ---- talking ----
    if(IS("me"))    { RpChat(RPH_ME, arg); return true; }
    if(IS("do"))    { RpChat(RPH_DO, arg); return true; }
    if(IS("ame"))   { RpChat(RPH_AME, arg); return true; }
    if(IS("try"))   { RpChat(RPH_TRY, arg); return true; }
    if(IS("b"))     { RpChat(RPH_B, arg); return true; }
    if(IS("o") || IS("ooc")) { RpChat(RPH_OOC, arg); return true; }
    if(IS("s") || IS("shout")) { RpChat(RPH_SHOUT, arg); return true; }
    if(IS("l") || IS("low"))   { RpChat(RPH_LOW, arg); return true; }
    if(IS("n") || IS("newbie")) { RpChat(RPH_NEWBIE, arg); return true; }
    if(IS("m") || IS("megaphone")) { RpChat(RPH_MEGAPHONE, arg); return true; }
    if(IS("r") || IS("radio")) { RpChat(RPH_RADIO, arg); return true; }
    if(IS("d") || IS("dept"))  { RpChat(RPH_DEPT, arg); return true; }
    if(IS("gov"))   { RpChat(RPH_GOV, arg); return true; }
    if(IS("w") || IS("whisper")) { if(!RpParseIdText(arg, id, &rest)) { SysMsg(COL_YELLOW, "/w [id] [text]"); return true; } RpChat(RPH_WHISPER, rest, id); return true; }
    if(IS("ad") || IS("advert")) { RpSend(RPC_AD, 0, 0, 0, arg); return true; }
    if(IS("feed") || IS("tweet") || IS("post")) { RpSend(RPC_FEED, 0, 0, 0, arg); return true; }
    // ---- money ----
    if(IS("pay") || IS("give")) { int a = 0; if(sscanf(arg, "%d %d", &id, &a) != 2) { SysMsg(COL_YELLOW, "/pay [id] [amount]"); return true; } RpSend(RPC_PAY, 0, (uint8_t)id, a); return true; }
    if(IS("deposit")) { int a = atoi(arg); if(a <= 0) { SysMsg(COL_YELLOW, "/deposit [amount] (at an ATM or the bank)"); return true; } RpSend(RPC_DEPOSIT, 0, 0, a); return true; }
    if(IS("withdraw")) { int a = atoi(arg); if(a <= 0) { SysMsg(COL_YELLOW, "/withdraw [amount] (at an ATM or the bank)"); return true; } RpSend(RPC_WITHDRAW, 0, 0, a); return true; }
    if(IS("transfer")) { char num[16]; int a = 0; if(sscanf(arg, "%15s %d", num, &a) != 2) { SysMsg(COL_YELLOW, "/transfer [phone number] [amount]"); return true; } RpSend(RPC_TRANSFER, 1, 0, a, num); return true; }
    if(IS("balance") || IS("bank") || IS("money")) { if(s_Rp.haveMe) SysMsg(RP_SKY, "[BANK] Cash $%d  |  Bank $%d  |  Payday in %d:%02d", s_Rp.me.cash, s_Rp.me.bank, s_Rp.me.paydayS / 60, s_Rp.me.paydayS % 60); return true; }
    if(IS("atm")) { int best = -1; float bd = 1e9f; int ped = Game::LocalPed(); Vec3 p; Game::GetCharPos(ped, p); for(int i = 0; i < RP_PLACE_COUNT; ++i) if(g_RpPlaces[i].kind == RPK_ATM || g_RpPlaces[i].kind == RPK_BANK) { float dx = g_RpPlaces[i].x - p.x, dy = g_RpPlaces[i].y - p.y, d = dx * dx + dy * dy; if(d < bd) { bd = d; best = i; } } if(best >= 0) RpSetGps({ g_RpPlaces[best].x, g_RpPlaces[best].y, g_RpPlaces[best].z }, g_RpPlaces[best].name); return true; }
    // ---- shops ----
    if(IS("shop") || IS("store") || IS("buy")) { RpPhoneOpenShop(-1); return true; }
    if(IS("food") || IS("eat") || IS("drink")) { RpPhoneOpenShop(RPS_FOOD); return true; }
    if(IS("clothes") || IS("skin")) { RpPhoneOpenShop(RPS_CLOTHES); return true; }
    if(IS("cars") || IS("dealership") || IS("buycar")) { RpPhoneOpenShop(RPS_CARS); return true; }
    if(IS("tech")) { RpPhoneOpenShop(RPS_TECH); return true; }
    if(IS("ammu") || IS("guns")) { RpPhoneOpenShop(RPS_GUNS); return true; }
    if(IS("lotto") || IS("lottery")) { int n = atoi(arg); RpSend(RPC_LOTTO, 0, (uint8_t)(n >= 1 && n <= 60 ? n : 0)); return true; }
    // ---- jobs ----
    if(IS("jobs") || IS("jobcenter")) { RpPhoneOpen(5); return true; }
    if(IS("job") || IS("joinjob") || IS("takejob"))
    {
        int j = atoi(arg);
        if(j <= 0 || j >= RPJ_COUNT) { for(int k = 0; k < RPJ_COUNT && !j; ++k) if(*arg && strcasestr(g_RpJobs[k].name, arg)) j = k; }
        if(j <= 0 || j >= RPJ_COUNT) { RpPhoneOpen(5); return true; }
        RpSend(RPC_JOIN_JOB, (uint8_t)j); return true;
    }
    if(IS("quitjob")) { RpSend(RPC_QUIT_JOB); return true; }
    if(IS("work") || IS("startwork") || IS("shift")) { RpSend(RPC_WORK); return true; }
    if(IS("stopwork") || IS("endshift")) { RpSend(RPC_STOP_WORK); return true; }
    if(IS("duty") || IS("onduty") || IS("offduty")) { RpSend(RPC_DUTY); return true; }
    if(IS("myjob")) { if(s_Rp.haveMe) SysMsg(RP_SKY, "[JOB] %s - %s", g_RpJobs[s_Rp.me.job].name, g_RpJobs[s_Rp.me.job].desc); return true; }
    // ---- phone ----
    if(IS("phone") || IS("p") || IS("cellphone")) { RpPhoneOpen(0); return true; }
    if(IS("call")) { int n = atoi(arg); if(n <= 0) { RpPhoneOpen(3); return true; } RpSend(RPC_CALL, 0, 0, n); return true; }
    if(IS("sms") || IS("text")) { int n = 0; char t[100] = ""; if(sscanf(arg, "%d %99[^\n]", &n, t) != 2) { SysMsg(COL_YELLOW, "/sms [number] [text]"); return true; } RpSend(RPC_SMS, 0, 0, n, t); return true; }
    if(IS("answer") || IS("pickup")) { RpSend(RPC_ANSWER); return true; }
    if(IS("hangup") || IS("h")) { RpSend(RPC_HANGUP); return true; }
    if(IS("number") || IS("mynumber")) { if(s_Rp.haveMe) SysMsg(RP_SKY, "[PHONE] Your number: %d", s_Rp.me.number); return true; }
    if(IS("contacts") || IS("phonebook")) { RpPhoneOpen(1); return true; }
    if(IS("911") || IS("police") || IS("ambulance")) { RpSend(RPC_911, 0, 0, 0, *arg ? arg : "Help needed!"); return true; }
    if(IS("taxi") || IS("calltaxi")) { RpSend(RPC_TAXI_CALL, 0, 0, 0, arg); return true; }
    // ---- houses ----
    if(IS("houses") || IS("realty")) { RpPhoneOpen(9); return true; }
    if(IS("buyhouse"))
    {
        int ped = Game::LocalPed(); Vec3 p; Game::GetCharPos(ped, p); int best = -1; float bd = 12.0f;
        for(int h = 0; h < RP_HOUSE_COUNT; ++h) { float dx = g_RpHouses[h].x - p.x, dy = g_RpHouses[h].y - p.y, d = sqrtf(dx * dx + dy * dy); if(d < bd) { bd = d; best = h; } }
        if(best < 0) { SysMsg(COL_RED, "Stand at a house door (Houses app shows them on the GPS)."); return true; }
        RpSend(RPC_BUY_HOUSE, (uint8_t)best); return true;
    }
    if(IS("sellhouse")) { RpSend(RPC_SELL_HOUSE); return true; }
    if(IS("myhouse")) { if(RpHasHome()) { const RpHouse& H = g_RpHouses[s_Rp.me.house]; RpSetGps({ H.x, H.y, H.z }, H.addr); } else SysMsg(COL_YELLOW, "You don't own a house yet (/houses)."); return true; }
    if(IS("home") || IS("gohome"))
    {
        if(!RpHasHome()) { SysMsg(COL_YELLOW, "You don't own a house yet (/houses)."); return true; }
        const RpHouse& H = g_RpHouses[s_Rp.me.house]; RpSetGps({ H.x, H.y, H.z }, H.addr); return true;
    }
    if(IS("setspawn") || IS("spawnpoint"))
    {
        if(!RpHasHome()) { SysMsg(COL_RED, "Buy a house first - then /setspawn inside or at it."); return true; }
        int ped = Game::LocalPed(); Vec3 p; Game::GetCharPos(ped, p);
        const RpHouse& H = g_RpHouses[s_Rp.me.house]; float dx = H.x - p.x, dy = H.y - p.y;
        if(dx * dx + dy * dy > 30.0f * 30.0f || Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Stand at your house (within 30 m, on foot) to set the spawn point."); return true; }
        RpSaveHomeSpawn(s_Rp.me.house, p, Game::GetCharHeading(ped));
        SysMsg(RP_SKY, "[HOME] Spawn point set. You will always spawn here - never at the Lobby Hub.");
        return true;
    }
    // ---- garage / pets ----
    if(IS("garage") || IS("myvehicles") || IS("vehicles")) { RpPhoneOpen(8); return true; }
    if(IS("v") || IS("myveh") || IS("callcar")) { int n = atoi(arg); if(!s_Rp.haveMe || !s_Rp.me.nveh) { SysMsg(COL_YELLOW, "No vehicles yet (/cars)."); return true; } RpSend(RPC_MY_VEH, (uint8_t)(n >= 1 ? n - 1 : 0)); return true; }
    if(IS("sellveh") || IS("sellcar")) { int n = atoi(arg); if(n < 1) { SysMsg(COL_YELLOW, "/sellveh [slot 1-6] (Garage app shows them)"); return true; } RpSend(RPC_SELL_VEH, (uint8_t)(n - 1)); return true; }
    if(IS("pets")) { RpPhoneOpen(10); return true; }
    if(IS("pet")) { int n = atoi(arg); if(n < 1 || n > RP_PET_COUNT) { RpPhoneOpen(10); return true; } RpSend(RPC_PET, (uint8_t)(n - 1)); return true; }
    if(IS("petoff") || IS("nopet")) { RpSend(RPC_PET, 255); return true; }
    if(IS("jetpack") || IS("jp")) { RpSend(RPC_JETPACK); return true; }
    // ---- police / services ----
    if(IS("cuff") || IS("uncuff") || IS("arrest") || IS("frisk") || IS("heal") || IS("defend"))
    {
        if(!RpParseIdText(arg, id, &rest)) { SysMsg(COL_YELLOW, "/%s [id]", cmd); return true; }
        uint8_t c = IS("cuff") ? RPC_CUFF : IS("uncuff") ? RPC_UNCUFF : IS("arrest") ? RPC_ARREST : IS("frisk") ? RPC_FRISK : IS("heal") ? RPC_HEAL : RPC_DEFEND;
        RpSend(c, 0, (uint8_t)id, 0, rest); return true;
    }
    if(IS("repair") || IS("fix")) { if(!RpParseIdText(arg, id, &rest)) id = myId; RpSend(RPC_REPAIR, 0, (uint8_t)id); return true; }
    if(IS("fine") || IS("ticket")) { int a = 0; char why[80] = ""; if(sscanf(arg, "%d %d %79[^\n]", &id, &a, why) < 2) { SysMsg(COL_YELLOW, "/fine [id] [amount] [reason]"); return true; } RpSend(RPC_FINE, 0, (uint8_t)id, a, why); return true; }
    if(IS("su") || IS("wanted") || IS("suspect")) { int lv = 1; char why[80] = ""; if(sscanf(arg, "%d %d %79[^\n]", &id, &lv, why) < 2) { SysMsg(COL_YELLOW, "/su [id] [level 1-6] [crime]"); return true; } RpSend(RPC_WANTED, (uint8_t)lv, (uint8_t)id, 0, why); return true; }
    if(IS("mdc") || IS("wantedlist"))
    {
        int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(s_Rp.haveRow[i] && (s_Rp.row[i].flags & 4)) { SysMsg(0x93C5FDFF, "[MDC] %s (%d) - WANTED", RpName(i), i); ++n; }
        if(!n) SysMsg(0x93C5FDFF, "[MDC] Nobody is wanted. A quiet day in Mega Vil.");
        return true;
    }
    // ---- fun ----
    if(IS("e") || IS("emote") || IS("anim"))
    {
        char nm[32] = ""; int who = -1; sscanf(arg, "%31s %d", nm, &who);
        if(!nm[0]) { RpShowEmotes(); return true; }
        if(!strcasecmp(nm, "c") || !strcasecmp(nm, "cancel") || !strcasecmp(nm, "stop")) { RpStopEmote(); return true; }
        int e = RpFindEmote(nm); if(e < 0) { SysMsg(COL_RED, "No emote \"%s\" (/emotes)", nm); return true; }
        if(g_RpEmotes[e].cat == RPE_SHARED) { if(who < 0) { SysMsg(COL_YELLOW, "/e %s [player id] - a shared emote", nm); return true; } RpSend(RPC_SHARED_EMOTE, (uint8_t)e, (uint8_t)who); return true; }
        RpPlayEmote(e); return true;
    }
    if(IS("emotes") || IS("anims") || IS("animlist")) { RpShowEmotes(); return true; }
    if(IS("stopanim") || IS("sa")) { RpStopEmote(); return true; }
    if(IS("dance")) { int n = atoi(arg); int found = 0; for(int i = 0; i < RP_EMOTE_COUNT; ++i) if(g_RpEmotes[i].cat == RPE_DANCE && ++found == (n > 0 ? n : 1 + rand() % 6)) { RpPlayEmote(i); return true; } for(int i = 0; i < RP_EMOTE_COUNT; ++i) if(g_RpEmotes[i].cat == RPE_DANCE) { RpPlayEmote(i); break; } return true; }
    if(IS("accept"))
    {
        if(!strncasecmp(arg, "fine", 4) || !strncasecmp(arg, "ticket", 6)) { RpSend(RPC_ACCEPT, 1); return true; }
        if(!strncasecmp(arg, "marry", 5) || !strncasecmp(arg, "propos", 6) || !strncasecmp(arg, "wedding", 7)) { RpSend(RPC_ACCEPT, 2); return true; }
        RpSend(RPC_ACCEPT, 0); s_Rp.askFrom = -1; return true;
    }
    if(IS("flip") || IS("coinflip") || IS("coin")) { RpSend(RPC_COINFLIP); return true; }
    if(IS("dice") || IS("roll")) { RpSend(RPC_DICE); return true; }
    if(IS("games") || IS("arcade")) { RpPhoneOpen(13); return true; }
    if(IS("event") || IS("events")) { if(s_Rp.haveState) { if(s_Rp.st.evKind) SysMsg(RP_SKY, "[EVENT] %s - %d s left. /joinevent for the GPS.", g_RpEventNames[s_Rp.st.evKind % RPEV_COUNT], s_Rp.st.evLeftS); else SysMsg(RP_SKY, "[EVENT] Next city event in %d:%02d. Lottery jackpot $%d.", s_Rp.st.nextEvS / 60, s_Rp.st.nextEvS % 60, s_Rp.st.lottoPot); } return true; }
    if(IS("joinevent")) { RpSend(RPC_EVENT_JOIN); return true; }
    if(IS("talk")) { if(s_Rp.talkPed && s_Rp.talkStage == 1) RpTalkChoose(100); else SysMsg(COL_YELLOW, "Walk up to someone in the street - they stop and you can talk."); return true; }
    // ---- info ----
    if(IS("help") || IS("cmds") || IS("commands") || IS("rphelp")) { RpShowHelp(); return true; }
    if(IS("rules")) { RpShowRules(); return true; }
    if(IS("stats") || IS("me2") || IS("profile")) { RpShowStatsLocal(); return true; }
    if(IS("levels") || IS("level")) { int into = 0, need = RpXpForNext(s_Rp.me.xp, &into); SysMsg(RP_SKY, "[LEVEL] You are level %d: %d / %d respect. +1 every 10 min played, +2 every payday, +1 every 3 job tasks.", s_Rp.me.level, into, need); return true; }
    if(IS("gps") || IS("places") || IS("locations") || IS("map")) { int n = atoi(arg); if(n >= 1 && n <= RP_PLACE_COUNT) RpPlacesPick(1, n - 1, nullptr, 0); else RpShowPlaces(); return true; }
    if(IS("greenzones") || IS("greenzone") || IS("safezones")) { SysMsg(RP_GREEN, "[GREENZONE] Lobby Hub (Plaza), City Hall, both hospitals and the Mega Bank: nobody can be hurt there."); return true; }
    if(IS("time")) { int h, m; Game::GetTimeOfDay(h, m); time_t t = time(nullptr); char b[32]; strftime(b, sizeof(b), "%H:%M", localtime(&t)); SysMsg(RP_SKY, "Mega Vil time %02d:%02d - your time %s", h, m, b); return true; }
    if(IS("panel") || IS("menu2")) { RpPanelToggle(); return true; }   // 3.26
    if(IS("inv") || IS("inventory") || IS("bag") || IS("i") || IS("items")) { RpInvShow(1); return true; }
    if(IS("docs") || IS("documents") || IS("idcard") || IS("licenses") || IS("licences")) { RpInvShow(2); return true; }
    if(IS("id")) { if(!*arg) { SysMsg(RP_SKY, "Your id: %d", myId); return true; } for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active && strcasestr(players[i].name, arg)) SysMsg(RP_SKY, "%s: id %d, phone %d", players[i].name, i, s_Rp.haveRow[i] ? s_Rp.row[i].number : 0); return true; }
    if(IS("players") || IS("list") || IS("score")) { FloatMenu::OpenPage(FloatMenu::PAGE_PLAYERS); return true; }
    if(IS("rp") || IS("about") || IS("server")) { SysMsg(RP_SKY, "MEGA VIL FUTURISTIC - Roleplay 24/7, %d citizens max. The future awaits.", MP_RP_MAX_PLAYERS); return true; }
    #undef IS
    // a plain /dance-like emote name works too ( /wave /sit /lean ... )
    { int e = RpFindEmote(cmd); if(e >= 0 && g_RpEmotes[e].cat != RPE_SHARED) { RpPlayEmote(e); return true; } }
    static const char* const kOk[] = { "q", "quit", "pm", "ping", "clear", "char", "kill", "admins", "report" };
    for(const char* c : kOk) if(!strcmp(cmd, c)) return false;
    SysMsg(COL_RED, "/%s is not available on Mega Vil Futuristic (no cheats / other gamemodes). /help", cmd);
    (void)now;
    return true;
}

// ================================ drawing =====================================================
static ImU32 RpCol(uint32_t rgba, float a) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }
[[maybe_unused]] static void RpGlowRect(ImDrawList* dl, ImVec2 a, ImVec2 b, float r, float now, float alpha = 1.0f, uint32_t col = RP_SKY)
{
    float pulse = 0.6f + 0.4f * sinf(now * 0.004f);
    for(int i = 3; i >= 1; --i) { float g = UI::S(3.0f * i); dl->AddRect(ImVec2(a.x - g, a.y - g), ImVec2(b.x + g, b.y + g), RpCol(col, 0.10f * pulse * alpha * (4 - i)), r + g, 0, UI::S(3)); }
    dl->AddRect(a, b, RpCol(col, 0.95f * alpha), r, 0, UI::S(2.5f));
}
static void RpText(ImDrawList* dl, float fs, ImVec2 p, ImU32 c, const char* t) { UI::OutlinedText(dl, fs, p, c, t); }
static float RpTextW(float fs, const char* t) { return UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t).x; }

// the title at the top (screenshot 1). Drawn like the GTA SA titles: Pricedown, thick black outline and a
// drop shadow, "Mega Vil" in sky blue, "Futuristic" in white, a small ROLEPLAY ribbon (no badge).
// (falls back to the normal font when pricedown.ttf isn't on the phone)
static void RpGtaText(ImDrawList* dl, ImFont* f, float fs, ImVec2 p, ImU32 fill, const char* t, float th)
{
    dl->AddText(f, fs, ImVec2(p.x + th * 1.6f, p.y + th * 1.9f), IM_COL32(0, 0, 0, 140), t);   // drop shadow
    for(int k = 0; k < 12; ++k) { float a = k * (IM_PI * 2.0f / 12.0f); dl->AddText(f, fs, ImVec2(p.x + cosf(a) * th, p.y + sinf(a) * th), IM_COL32(0, 0, 0, 255), t); }
    dl->AddText(f, fs, p, fill, t);
}
static void RpDrawLogo(ImDrawList* dl, float cx, float y, float scale, float now)
{
    ImFont* pd = UI::PricedownFont(); ImFont* f = pd ? pd : UI::Font();
    const float k = pd ? 1.0f : 0.8f;
    const char* t1 = "Mega Vil"; const char* t2 = "Futuristic";
    float fs1 = UI::S(46) * scale * k, fs2 = UI::S(30) * scale * k, th = UI::S(2.6f) * scale;
    float w1 = f->CalcTextSizeA(fs1, FLT_MAX, 0, t1).x, w2 = f->CalcTextSizeA(fs2, FLT_MAX, 0, t2).x;
    float gap = UI::S(10) * scale, tw = w1 + gap + w2;
    float bs = fs1 * 1.25f;
    float x = cx - tw * 0.5f;   // 3.25: title only, no badge next to it (the badge stays in the server list)
    float ty = y + (bs - fs1) * 0.5f - UI::S(4) * scale;
    RpGtaText(dl, f, fs1, ImVec2(x, ty), RpCol(0x7DD3FCFF, 1), t1, th);
    RpGtaText(dl, f, fs2, ImVec2(x + w1 + gap, ty + (fs1 - fs2) * 0.78f), IM_COL32(255, 255, 255, 255), t2, th * 0.85f);
    // the ROLEPLAY ribbon under the words, slightly off-centre like a sticker
    const char* t3 = "ROLEPLAY"; float fs3 = UI::S(15) * scale; float w3 = RpTextW(fs3, t3);
    float rx = x + w1 - w3 * 0.5f, ry = ty + fs1 * 0.98f;
    float pad = UI::S(8) * scale, rh = fs3 + UI::S(4) * scale;
    dl->AddQuadFilled(ImVec2(rx - pad, ry), ImVec2(rx + w3 + pad, ry - UI::S(1) * scale), ImVec2(rx + w3 + pad - UI::S(4) * scale, ry + rh), ImVec2(rx - pad + UI::S(3) * scale, ry + rh + UI::S(1) * scale), IM_COL32(255, 255, 255, 235));
    dl->AddText(UI::Font(), fs3, ImVec2(rx, ry + UI::S(2) * scale), RpCol(0x0C3C64FF, 1), t3);
    (void)now;
}
// a round status icon with a ring (health / armour / hunger / thirst / stamina) and the %
static void RpStatCircle(ImDrawList* dl, ImVec2 c, float r, float k, uint32_t col, const char* glyph, const char* label)
{
    if(k < 0) k = 0; if(k > 1) k = 1;
    dl->AddCircleFilled(c, r, IM_COL32(8, 18, 32, 200), 32);
    dl->AddCircle(c, r, IM_COL32(255, 255, 255, 40), 32, UI::S(4));
    if(k > 0) { dl->PathArcTo(c, r, -IM_PI * 0.5f, -IM_PI * 0.5f + IM_PI * 2.0f * k, 40); dl->PathStroke(RpCol(col, 1), 0, UI::S(4.5f)); }
    float fs = r * 0.95f; float w = RpTextW(fs, glyph);
    RpText(dl, fs, ImVec2(c.x - w * 0.5f, c.y - fs * 0.55f), RpCol(col, 1), glyph);
    char p[16]; snprintf(p, sizeof(p), "%d%%", (int)(k * 100 + 0.5f));
    float fs2 = UI::S(15); float w2 = RpTextW(fs2, p);
    RpText(dl, fs2, ImVec2(c.x - w2 * 0.5f, c.y + r + UI::S(3)), IM_COL32_WHITE, p);
    (void)label;
}
static void RpDrawHud(ImDrawList* dl, float now, uint64_t nowMs)
{
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const PktRpMe& m = s_Rp.me;
    int ped = Game::LocalPed();
    char t[160], mb[32];
    RpDrawLogo(dl, ds.x * 0.5f, UI::S(10), 1.0f, now);
    // ---- right info panel (TIME / ID / NAME / JOB / CASH / BANK) ----
    {
        // 3.25: under the game's clock / money / weapon / health / wanted stars (it covered them before),
        // no background and no outline - only the text (it has its own black text outline)
        float w = UI::S(330), x1 = ds.x - UI::S(14), x0 = x1 - w, y0 = UI::S(228), rh = UI::S(27);
        const int rows = 6; float y1 = y0 + rows * rh + UI::S(16);
        int hh, mm; Game::GetTimeOfDay(hh, mm);
        time_t tt = time(nullptr); struct tm* lt = localtime(&tt); char clock[32]; strftime(clock, sizeof(clock), "%H:%M  %d/%m", lt);
        const char* keys[rows] = { "TIME", "ID", "NAME", "JOB", "CASH", "BANK" };
        char vals[rows][48];
        snprintf(vals[0], 48, "%s  (city %02d:%02d)", clock, hh, mm);
        snprintf(vals[1], 48, "%d  |  Lv %d", myId, m.level);
        snprintf(vals[2], 48, "%s", settings.name);
        snprintf(vals[3], 48, "%s%s", g_RpJobs[m.job < RPJ_COUNT ? m.job : 0].name, m.duty ? " (duty)" : "");
        snprintf(vals[4], 48, "$%d", m.cash);
        snprintf(vals[5], 48, "$%d", m.bank);
        float fs = UI::S(19);
        for(int i = 0; i < rows; ++i)
        {
            float y = y0 + UI::S(8) + i * rh;
            RpText(dl, fs, ImVec2(x0 + UI::S(14), y), RpCol(RP_SKY, 1), keys[i]);
            RpText(dl, fs, ImVec2(x0 + UI::S(14) + UI::S(70), y), RpCol(RP_SKY, 1), ":");
            uint32_t vc = i == 4 ? 0x86EFACFF : (i == 5 ? 0x7DD3FCFF : 0xFFFFFFFF);
            RpText(dl, fs, ImVec2(x0 + UI::S(100), y), RpCol(vc, 1), vals[i]);
        }
        // payday + money popup
        snprintf(t, sizeof(t), "Payday in %d:%02d", m.paydayS / 60, m.paydayS % 60);
        RpText(dl, UI::S(16), ImVec2(x1 - RpTextW(UI::S(16), t) - UI::S(10), y1 + UI::S(6)), RpCol(0xBAE6FDFF, 1), t);
        if(s_Rp.payAt && nowMs - s_Rp.payAt < 2600 && s_Rp.payFx)
        {
            float k = (nowMs - s_Rp.payAt) / 2600.0f;
            snprintf(t, sizeof(t), "%s$%d  %s", s_Rp.payFx > 0 ? "+" : "-", s_Rp.payFx > 0 ? s_Rp.payFx : -s_Rp.payFx, s_Rp.payWhy);
            RpText(dl, UI::S(22), ImVec2(x1 - RpTextW(UI::S(22), t) - UI::S(10), y1 + UI::S(30) + k * UI::S(20)), RpCol(s_Rp.payFx > 0 ? RP_GREEN : RP_RED, 1.0f - k * k), t);
        }
        // voice (screenshot: VOICE: NORMAL + who is talking)
        float vy = y1 + UI::S(58);
        snprintf(t, sizeof(t), "VOICE: %s%s", s_Rp.callWith >= 0 ? "PHONE CALL" : "NORMAL", MP::MeTalking() ? "  (talking)" : "");
        RpText(dl, UI::S(17), ImVec2(x0 + UI::S(6), vy), RpCol(MP::MeTalking() ? RP_GREEN : RP_SKY, 1), t);
        int shown = 0;
        for(int i = 0; i < MP_MAX_PLAYERS && shown < 4; ++i)
            if(i != myId && MP::VoiceSpeaking(i)) { snprintf(t, sizeof(t), "> %s [%d]", RpName(i), i); RpText(dl, UI::S(16), ImVec2(x0 + UI::S(6), vy + UI::S(22) * (1 + shown)), IM_COL32_WHITE, t); ++shown; }
    }
    // ---- bottom status row ----
    // 3.28: in the bottom-right corner (was centred at the bottom), left of the game's sprint / fist buttons
    {
        float r = UI::S(25), gap = UI::S(70), cy = ds.y - UI::S(70);
        float cx = ds.x - ds.x * 0.11f - r - gap * 4.0f;
        int hp = ped > 0 ? Game::GetCharHealth(ped) : 0, ar = ped > 0 ? Game::GetCharArmour(ped) : 0;
        RpStatCircle(dl, ImVec2(cx, cy), r, hp / 100.0f, 0xF87171FF, "+", "HEALTH");
        RpStatCircle(dl, ImVec2(cx + gap, cy), r, ar / 100.0f, 0x93C5FDFF, "A", "ARMOUR");
        RpStatCircle(dl, ImVec2(cx + gap * 2, cy), r, m.hunger / 100.0f, 0xFDBA74FF, "F", "HUNGER");
        RpStatCircle(dl, ImVec2(cx + gap * 3, cy), r, m.thirst / 100.0f, 0x67E8F9FF, "W", "THIRST");
        RpStatCircle(dl, ImVec2(cx + gap * 4, cy), r, s_Rp.stamina / 100.0f, 0xFDE047FF, "S", "STAMINA");
    }
    // ---- city event: centred at the bottom (the action centre), text only ----
    // 3.28: was a line in the top-left list under the chat
    float evY = ds.y - UI::S(96);
    if(s_Rp.haveState && s_Rp.st.evKind)
    {
        snprintf(t, sizeof(t), "EVENT: %s  %d:%02d  /joinevent", g_RpEventNames[s_Rp.st.evKind % RPEV_COUNT], s_Rp.st.evLeftS / 60, s_Rp.st.evLeftS % 60);
        float fs = UI::S(22), w = RpTextW(fs, t);
        RpText(dl, fs, ImVec2((ds.x - w) * 0.5f, evY), RpCol(RP_SKY, 1), t);
        evY -= UI::S(34);
    }
    // ---- NEW CITIZEN box ----
    if(m.newbieS)
    {
        // 3.25: on the left (it covered the sprint button on the right), black 60 %, no outline
        // 3.25.1: a bit lower (47 % of the height, was 40 %): it touched the checkmark / chat lines above it
        // 3.28: right above the radar (its top is at 67.5 % of the height), lined up with its left edge
        float w = UI::S(300), h = UI::S(64), x0 = ds.x * 0.012f, y0 = ds.y * (0.915f - 0.24f) - h - UI::S(12);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + w, y0 + h), IM_COL32(0, 0, 0, 153), UI::S(10));
        RpText(dl, UI::S(20), ImVec2(x0 + UI::S(12), y0 + UI::S(6)), IM_COL32_WHITE, "YOU ARE A NEW CITIZEN");
        snprintf(t, sizeof(t), "%02d:%02d:%02d left  -  /n for help", m.newbieS / 3600, (m.newbieS / 60) % 60, m.newbieS % 60);
        RpText(dl, UI::S(18), ImVec2(x0 + UI::S(12), y0 + UI::S(34)), RpCol(0xE0F2FEFF, 1), t);
    }
    // ---- GREENZONE badge ----
    Vec3 me{}; bool haveMe = ped > 0 && Game::GetCharPos(ped, me);
    if(haveMe && RpInGreen(me.x, me.y))
    {
        // 3.25: centred at the bottom, text only (no background); 3.28: above the event line
        const char* g = "YOU ARE IN A GREENZONE"; float fs = UI::S(22), w = RpTextW(fs, g);
        RpText(dl, fs, ImVec2((ds.x - w) * 0.5f, evY), RpCol(0x4ADE80FF, 1), g);
    }
    // ---- status lines: job / wanted / jail / cuffed / event ----
    float ly = UI::S(118);
    auto Line = [&](uint32_t col, const char* s) { RpText(dl, UI::S(19), ImVec2(UI::S(14), ly), RpCol(col, 1), s); ly += UI::S(26); };
    if(m.jailS) { snprintf(t, sizeof(t), "JAIL: %d:%02d", m.jailS / 60, m.jailS % 60); Line(RP_RED, t); }
    if(m.cuffed) Line(RP_RED, "CUFFED");
    if(m.wanted) { snprintf(t, sizeof(t), "WANTED LEVEL %d", m.wanted); Line(RP_RED, t); }
    if(m.jobOn && s_Rp.tgtOn)
    {
        float d = haveMe ? sqrtf((me.x - s_Rp.tgt.x) * (me.x - s_Rp.tgt.x) + (me.y - s_Rp.tgt.y) * (me.y - s_Rp.tgt.y)) : 0;
        snprintf(t, sizeof(t), "%s: marker %d m  |  tasks %u", g_RpJobs[m.job].name, (int)d, m.tasks); Line(g_RpJobs[m.job].col, t);
    }
    if(s_Rp.gpsName[0]) { snprintf(t, sizeof(t), "GPS: %s", s_Rp.gpsName); Line(0xBAE6FDFF, t); }
    if(s_Rp.callWith >= 0) { int s = (int)((nowMs - s_Rp.callStart) / 1000); snprintf(t, sizeof(t), "ON CALL: %s  %d:%02d  /h", s_Rp.callName, s / 60, s % 60); Line(RP_GREEN, t); }
    (void)mb;
}
static void RpDrawWorld(ImDrawList* dl, float now)
{
    if(s_Rp.view != 2) return;
    RpInfoDraw(dl);   // 3.29: plaza info people
    char t[96];
    for(int i = 0; i < RP_PLACE_COUNT; ++i)
    {
        Vec3 p = { g_RpPlaces[i].x, g_RpPlaces[i].y, g_RpPlaces[i].z + 1.4f };
        float d = BcwCamDist(p); if(d > 70.0f) continue;
        BcwLabel3D(dl, p, g_RpPlaces[i].name, RP_SKY, 26.0f, 22.0f);
        if(g_RpPlaces[i].kind == RPK_ATM || g_RpPlaces[i].kind == RPK_SHOP || g_RpPlaces[i].kind == RPK_JOBS || g_RpPlaces[i].kind == RPK_BANK)
            BcwDrawZone(dl, { p.x, p.y, p.z - 2.3f }, 1.6f, RP_SKY, now, false, 0.5f);
    }
    for(int h = 0; h < RP_HOUSE_COUNT; ++h)
    {
        Vec3 p = { g_RpHouses[h].x, g_RpHouses[h].y, g_RpHouses[h].z + 1.0f };
        if(BcwCamDist(p) > 35.0f) continue;
        bool mine = s_Rp.haveMe && s_Rp.me.house == h, sold = s_Rp.haveState && s_Rp.st.houseOwned[h];
        if(mine) snprintf(t, sizeof(t), "YOUR HOME\n%s", g_RpHouses[h].addr);
        else if(sold) snprintf(t, sizeof(t), "%s\nSOLD", g_RpHouses[h].addr);
        else snprintf(t, sizeof(t), "%s\nFOR SALE  $%d  /buyhouse", g_RpHouses[h].addr, g_RpHouses[h].price);
        BcwLabel3D(dl, p, t, mine ? RP_GREEN : (sold ? 0xA3A3A3FF : RP_SKY), 22.0f, 12.0f);
    }
    if(s_Rp.tgtOn) { BcwDrawZone(dl, { s_Rp.tgt.x, s_Rp.tgt.y, s_Rp.tgt.z - 0.9f }, 5.0f, s_Rp.tgtCol, now, true, 1.2f, false); BcwLabel3D(dl, { s_Rp.tgt.x, s_Rp.tgt.y, s_Rp.tgt.z + 2.5f }, s_Rp.tgtText, s_Rp.tgtCol, 26.0f, 40.0f); }
    if(s_Rp.haveState && s_Rp.st.evKind && s_Rp.st.evState == 1 && (s_Rp.st.evPos[0] != 0 || s_Rp.st.evPos[1] != 0))
    {
        Vec3 c = { s_Rp.st.evPos[0], s_Rp.st.evPos[1], s_Rp.st.evPos[2] - 0.9f };
        float r = s_Rp.st.evKind == RPEV_FESTIVAL ? 50.0f : 5.0f;
        BcwDrawZone(dl, c, r, RP_SKY, now, true, s_Rp.st.evKind == RPEV_HOLO_RUSH ? 25.0f : 2.0f, false);
        BcwLabel3D(dl, { c.x, c.y, c.z + 4.0f }, g_RpEventNames[s_Rp.st.evKind % RPEV_COUNT], RP_SKY, 34.0f, 60.0f);
    }
    if(s_Rp.callBlip) BcwLabel3D(dl, { s_Rp.callPos.x, s_Rp.callPos.y, s_Rp.callPos.z + 2.0f }, s_Rp.callText, RP_YELLOW, 26.0f, 40.0f);
}
// the connection screen: Lobby Hub camera + logo + sky blue < SPAWN >
static void RpDrawLobby(float now)
{
    if(MP::AuthScreenShown() || SampDialogOpen()) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    RpDrawLogo(bg, ds.x * 0.5f, ds.y * 0.08f, 1.7f, now);
    char t[128];
    if(!s_Rp.haveMe) return;   // 3.28.2: no "Connecting to the city..." text while the citizen data loads
    if(RpHasHome()) { snprintf(t, sizeof(t), "Going home: %s", g_RpHouses[s_Rp.me.house].addr); BcwCenterText(bg, UI::S(28), ds.y * 0.45f, RpCol(RP_SKY, 1), t); return; }
    BcwCenterText(bg, UI::S(30), ds.y * 0.215f, IM_COL32_WHITE, "LOBBY HUB  -  MEGA VIL PLAZA");
    snprintf(t, sizeof(t), "Level %d  |  Cash $%d  |  Bank $%d  |  Phone %d", s_Rp.me.level, s_Rp.me.cash, s_Rp.me.bank, s_Rp.me.number);
    BcwCenterText(bg, UI::S(22), ds.y * 0.215f + UI::S(44), RpCol(0xBAE6FDFF, 1), t);
    const float bw = UI::S(420), bh = UI::S(84);
    BcwBeginOverlay("##RpSpawn", ImVec2(ds.x * 0.5f - bw * 0.5f - UI::S(18), ds.y * 0.72f), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    ImGui::SetWindowFontScale(1.1f);
    if(BcwTeamButton("<  SPAWN  >##rps", RP_SKY, ImVec2(bw, bh), true, false)) RpSpawnNow(Game::LocalPed());
    ImGui::SetWindowFontScale(1.0f);
    BcwEndOverlay();
    BcwCenterText(bg, UI::S(20), ds.y * 0.72f + bh + UI::S(40), IM_COL32(220, 220, 220, 255), "Roleplay only - peaceful city - no cheats. Own a house and you always spawn at home.");
}
// shared emote request popup
static void RpDrawAsk()
{
    if(s_Rp.askFrom < 0 || MP_NowMs() - s_Rp.askAt > 30000) { s_Rp.askFrom = -1; return; }
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float bw = UI::S(220), bh = UI::S(58);
    char t[96]; snprintf(t, sizeof(t), "%s: %s?", RpName(s_Rp.askFrom), s_Rp.askEmote >= 0 && s_Rp.askEmote < RP_EMOTE_COUNT ? g_RpEmotes[s_Rp.askEmote].label : "emote");
    BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(24), ds.y * 0.60f - UI::S(36), RpCol(RP_SKY, 1), t);
    BcwBeginOverlay("##RpAsk", ImVec2(ds.x * 0.5f - bw - UI::S(24), ds.y * 0.60f), ImVec2(bw * 2 + UI::S(48), bh + UI::S(28)), 0.0f);
    if(BcwTeamButton("Accept##ra", RP_SKY, ImVec2(bw, bh), true, false)) { RpSend(RPC_ACCEPT, 0); s_Rp.askFrom = -1; }
    ImGui::SameLine(0, UI::S(12));
    if(BcwTeamButton("Decline##rd", 0x0C4A6EFF, ImVec2(bw, bh), false, false)) s_Rp.askFrom = -1;
    BcwEndOverlay();
}
static void RenderRpPhone(float now);   // mp_rp_phone.inc
static void RenderRp()
{
    if(!s_Rp.engaged) return;
    if(FloatMenu::IsModal()) return;   // 3.25.1: the full-screen MP launcher is open: no Mega Vil HUD / phone / banner over it
    float now = (float)(MP_NowMs() % 100000000ULL);
    uint64_t nowMs = MP_NowMs();
    if(s_Rp.view == 1) { if(Game::IsReady()) RpInfoDraw(ImGui::GetBackgroundDrawList()); RpDrawLobby(now); return; }
    if(MP::AuthScreenShown()) return;
    if(Game::IsReady() && !Game::IsPaused()) RpDrawWorld(ImGui::GetBackgroundDrawList(), now);
    if(s_Rp.haveMe && !Game::IsPaused()) RpDrawHud(ImGui::GetForegroundDrawList(), now, nowMs);
    RpDrawRadar(nowMs);   // 3.27
    if(!Game::IsPaused())
    {
        RpDrawTalk(now);
        RpDrawAsk();
        RenderRpPhone(now);
        RenderRpInv();     // 3.26: PANEL, player info + bag, documents, "Received" cards
        RpDrawAtm(nowMs);  // 3.29: the ATM screen
        RpDrawDrops(nowMs);   // 3.30: items on the ground
    }
    if(s_Rp.bannerAt && nowMs - s_Rp.bannerAt < 3500)
    {
        float a = nowMs - s_Rp.bannerAt < 3000 ? 1.0f : 1.0f - (nowMs - s_Rp.bannerAt - 3000) / 500.0f;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        BcwCenterText(ImGui::GetForegroundDrawList(), UI::S(40), ds.y * 0.26f, RpCol(s_Rp.bannerCol, a), s_Rp.banner);
    }
}

// launcher / menu entry: join the 24/7 Mega Vil (the VPS / Broker bot, or a phone when it is down)
void MP::JoinRp()
{
    if(RpOnServer()) { LeaveServer("Left Mega Vil Futuristic."); return; }
    s_bRpJoin = true;
    MP::JoinServer("", MP_RP_PORT, MP_RP_SERVER_NAME);
    if(!JoinFlowActive() && client.GetState() == NET_DISCONNECTED && !IsBusy()) s_bRpJoin = false;
}
