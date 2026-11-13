// 4.13.15: Co-op story (SA:COOP style) - the phone side of net/coop.h. Included by mp.cpp after mp_hostmap.inc.
//
// TWO players only. This is not the multiplayer "Mission sync" group: the host's save is the story.
//  * Accept: the partner's game backs up its own saves and loads the HOST's newest save, so both are at the
//    same mission progress (if the partner was further or behind, they are put to the host's state). The
//    partner keeps their own place, money, weapons, health and armour. Co-op end = the partner's own saves
//    come back and their own newest save loads.
//  * After every mission (the host saves / autosaves) the new save goes to the partner again.
//  * The pair is put into Mission sync too, so each sees the other on missions and in cutscenes (the remote
//    ped stays visible in the cutscene).
//  * Mission start (host): the partner is brought next to the host. Weapons the mission gives the host go to
//    the partner, and the money reward when it is passed.
//  * Cutscene (host): the partner's CJ is put next to the host's CJ, plays listen / nod / look animations,
//    looks at the host and can't walk off until it ends; both screens wait for it together.
//  * Time of day and weather follow the host (unless the server locks the world).

static struct CoopState
{
    int  partner = -1; bool host = false; int invitedBy = -1; uint64_t inviteAt = 0; int askedTo = -1;
    // host: sending a save
    std::vector<uint8_t> out; uint32_t outAcked = 0, outSent = 0, outCrc = 0; uint64_t outAt = 0, outProgAt = 0;
    time_t sentMtime = 0; uint64_t saveScanAt = 0; bool wantSend = false;
    // partner: receiving
    std::vector<uint8_t> in; uint32_t inSize = 0, inCrc = 0, inGot = 0; bool loadAsked = false;
    // partner: what stays yours across loads
    bool kept = false; Vec3 keepPos{}; float keepH = 0; int keepMoney = 0, keepHp = 100, keepArm = 0; int keepW[14][2] = {};
    // host: mission
    bool wasMission = false; int missionMoney = 0; int lastW[14][2] = {}; uint64_t wScanAt = 0, worldAt = 0;
    // cutscene
    bool hostCut = false, wasCut = false; Vec3 cutPos{}; float cutH = 0; uint64_t cutAnimAt = 0; int cutAnim = 0;
} s_Co;

static bool CoopOn() { return s_Co.partner >= 0 && s_Co.partner < MP_MAX_PLAYERS && players[s_Co.partner].active; }
static bool CoopIsPartnerOf(int id) { return CoopOn() && id == s_Co.partner; }

static void CoopSend(uint8_t op, uint32_t a = 0, uint32_t b = 0, const Vec3* pos = nullptr, float f = 0, const char* text = nullptr, const void* data = nullptr, int n = 0)
{
    PktCoop c; memset(&c, 0, COOP_HEAD); c.type = PKT_COOP; c.op = op; c.other = (uint8_t)(s_Co.partner >= 0 ? s_Co.partner : MP_INVALID_ID);
    c.a = a; c.b = b; c.f = f;
    if(pos) { c.pos[0] = pos->x; c.pos[1] = pos->y; c.pos[2] = pos->z; }
    if(text) MP_CopyStr(c.text, text, sizeof(c.text));
    if(data && n > 0) { n = std::min(n, COOP_CHUNK); memcpy(c.data, data, n); c.n = (uint16_t)n; }
    client.Send(&c, COOP_HEAD + (data ? n : 0));
}
static void CoopSendTo(uint8_t op, int other) { PktCoop c; memset(&c, 0, COOP_HEAD); c.type = PKT_COOP; c.op = op; c.other = (uint8_t)other; client.Send(&c, COOP_HEAD); }

static void CoopReset(bool restore)
{
    if(restore && !s_Co.host && Progress::InCoop()) Progress::CoopEnd(MP_NowMs());
    if(s_Co.wasCut) { Game::SetPlayerControl(true); }
    s_Co = CoopState();
}

// ---- the newest save of this phone (the host's story) -----------------------------------------
static int CoopNewestSlot(time_t* mt)
{
    int best = -1; time_t bt = 0;
    for(int s = 0; s < 10; ++s)
    {
        char path[460]; struct stat st;
        if(!Game::SaveSlotPath(s, path, sizeof(path)) || stat(path, &st) != 0 || st.st_size < 1000) continue;
        if(st.st_mtime >= bt) { bt = st.st_mtime; best = s; }
    }
    if(mt) *mt = bt;
    return best;
}
static bool CoopStartSend()
{
    time_t mt = 0; const int slot = CoopNewestSlot(&mt);
    if(slot < 0) { SysMsg(COL_YELLOW, "Co-op: you have no save yet - save once (a save house) so your partner can load your story."); return false; }
    char path[460]; if(!Game::SaveSlotPath(slot, path, sizeof(path))) return false;
    FILE* f = fopen(path, "rb"); if(!f) return false;
    std::vector<uint8_t> d; uint8_t buf[8192]; size_t r;
    while((r = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + r);
    fclose(f);
    if(d.size() < 1000 || d.size() > 4 * 1024 * 1024) return false;
    s_Co.out.swap(d); s_Co.outAcked = s_Co.outSent = 0; s_Co.outAt = s_Co.outProgAt = MP_NowMs();
    s_Co.outCrc = (uint32_t)crc32(0L, s_Co.out.data(), (unsigned int)s_Co.out.size());
    s_Co.sentMtime = mt; s_Co.wantSend = false;
    char lbl[14]; snprintf(lbl, sizeof(lbl), "slot %d", slot + 1);
    CoopSend(COOP_SAVE_BEGIN, (uint32_t)s_Co.out.size(), s_Co.outCrc, nullptr, 0, lbl);
    return true;
}
static void CoopPumpSend(uint64_t now)
{
    if(s_Co.out.empty()) return;
    const uint32_t size = (uint32_t)s_Co.out.size();
    if(now - s_Co.outProgAt > 600) { s_Co.outSent = s_Co.outAcked; s_Co.outProgAt = now; }   // nothing acked lately: from the last ack again
    if(now - s_Co.outAt > 60000) { s_Co.out.clear(); SysMsg(COL_YELLOW, "Co-op: sending your save timed out (it tries again after your next save)."); return; }
    int burst = 0;
    while(s_Co.outSent < size && s_Co.outSent < s_Co.outAcked + 16 * COOP_CHUNK && burst < 6)
    {
        const int n = (int)std::min<uint32_t>(COOP_CHUNK, size - s_Co.outSent);
        CoopSend(COOP_SAVE_CHUNK, s_Co.outSent, 0, nullptr, 0, nullptr, s_Co.out.data() + s_Co.outSent, n);
        s_Co.outSent += n; ++burst;
    }
}

// ---- partner: keep what is yours across a load --------------------------------------------------
static void CoopKeepMine()
{
    const int ped = Game::LocalPed(); if(ped <= 0) return;
    s_Co.kept = Game::GetCharPos(ped, s_Co.keepPos);
    s_Co.keepH = Game::GetCharHeading(ped); s_Co.keepMoney = Game::GetMoney();
    s_Co.keepHp = Game::GetCharHealth(ped); s_Co.keepArm = Game::GetCharArmour(ped);
    for(int s = 1; s <= 13; ++s) { int w = 0, a = 0; if(!Game::GetWeaponInSlot(ped, s, w, a)) w = a = 0; s_Co.keepW[s][0] = w; s_Co.keepW[s][1] = a; }
}
static void CoopPutMineBack()
{
    const int ped = Game::LocalPed(); if(ped <= 0 || !s_Co.kept) return;
    Game::Teleport(ped, s_Co.keepPos, s_Co.keepH);
    const int m = Game::GetMoney(); if(m != s_Co.keepMoney) Game::AddMoney(s_Co.keepMoney - m);
    Game::SetCharHealth(ped, s_Co.keepHp > 0 ? s_Co.keepHp : 100);
    const int arm = Game::GetCharArmour(ped); if(s_Co.keepArm > arm) Game::AddArmour(ped, s_Co.keepArm - arm);
    Game::RemoveAllWeapons(ped);
    for(int s = 1; s <= 13; ++s) if(s_Co.keepW[s][0] > 0) Game::GiveWeaponAmmo(ped, s_Co.keepW[s][0], s_Co.keepW[s][1]);
}

static void CoopOnSaveChunk(const PktCoop& c, int len)
{
    if(s_Co.host || !s_Co.inSize) return;
    if(c.a != s_Co.inGot || c.n == 0 || c.n > COOP_CHUNK || COOP_HEAD + c.n > len) { CoopSend(COOP_SAVE_ACK, s_Co.inGot); return; }
    if(s_Co.inGot + c.n > s_Co.inSize) return;
    memcpy(s_Co.in.data() + s_Co.inGot, c.data, c.n); s_Co.inGot += c.n;
    CoopSend(COOP_SAVE_ACK, s_Co.inGot);
    if(s_Co.inGot < s_Co.inSize) return;
    if((uint32_t)crc32(0L, s_Co.in.data(), (unsigned int)s_Co.in.size()) != s_Co.inCrc) { SysMsg(COL_RED, "Co-op: the host's save arrived broken - asking again."); CoopSend(COOP_SAVE_DONE, 0); s_Co.inSize = 0; return; }
    s_Co.loadAsked = true;   // loads when you can (not on a mission, not paused) - CoopProcess
}

// ---- packets --------------------------------------------------------------------------------
static void CoopOnPacket(const uint8_t* d, int len)
{
    if(len < COOP_HEAD) return;
    PktCoop c; memset(&c, 0, sizeof(c)); memcpy(&c, d, std::min<int>(len, (int)sizeof(c)));
    const int o = c.other;
    const char* nm = o < MP_MAX_PLAYERS && players[o].active ? players[o].name : "the other player";
    switch(c.op)
    {
        case COOP_INVITE: s_Co.invitedBy = o; s_Co.inviteAt = MP_NowMs(); SysMsg(COL_SYSTEM, "*** %s invites you to Co-op story (2 players, their story).", nm); return;
        case COOP_DECLINE:
            if(c.a == 3) SysMsg(COL_YELLOW, "*** %s is already in a Co-op story.", nm);
            else if(c.a == 2) SysMsg(COL_YELLOW, "*** Co-op story is not possible with %s (Mission sync off on this server, or they run an older MP).", nm);
            else SysMsg(COL_SYSTEM, "*** %s declined Co-op story.", nm);
            s_Co.askedTo = -1; return;
        case COOP_ACCEPT:
            CoopReset(false);
            s_Co.partner = o; s_Co.host = c.a == 1;
            if(s_Co.host)
            {
                SysMsg(COL_GREEN, "*** Co-op story with %s: you are the HOST - your save is the story. Start missions, your partner comes along.", nm);
                if(!IsPartner(o)) SendPair(PAIR_REQUEST, (uint8_t)o);   // + Mission sync: see each other on missions / cutscenes
                CoopStartSend();
            }
            else SysMsg(COL_GREEN, "*** Co-op story with %s: you are the PARTNER - you get the host's mission progress (your own saves are kept and come back).", nm);
            return;
        case COOP_END:
            if(CoopOn() || s_Co.partner >= 0) SysMsg(COL_SYSTEM, "*** Co-op story ended.");
            CoopReset(true); return;
        default: break;
    }
    if(!CoopOn() || o != s_Co.partner) return;
    const uint64_t now = MP_NowMs();
    switch(c.op)
    {
        case COOP_SAVE_BEGIN:
            if(s_Co.host || c.a < 1000 || c.a > 4 * 1024 * 1024) return;
            s_Co.in.assign(c.a, 0); s_Co.inSize = c.a; s_Co.inCrc = c.b; s_Co.inGot = 0; s_Co.loadAsked = false;
            CoopSend(COOP_SAVE_ACK, 0);
            break;
        case COOP_SAVE_CHUNK: CoopOnSaveChunk(c, len); break;
        case COOP_SAVE_ACK:
            if(s_Co.host && !s_Co.out.empty() && c.a > s_Co.outAcked) { s_Co.outAcked = c.a; s_Co.outProgAt = now; if(s_Co.outAcked >= s_Co.out.size()) s_Co.out.clear(); }
            break;
        case COOP_SAVE_DONE:
            if(!s_Co.host) break;
            if(c.a == 1) SysMsg(COL_GREEN, "*** %s now has your story progress.", nm);
            else s_Co.wantSend = true;   // broken / failed: send again
            break;
        case COOP_MISSION:
            if(s_Co.host) break;
            if(c.a == 1)
            {
                SysMsg(COL_GREEN, "*** Co-op: %s started %s - you're in it together.", nm, MP::MissionTitle(c.text));
                const int ped = Game::LocalPed(); Vec3 me;
                const Vec3 hp = { c.pos[0], c.pos[1], c.pos[2] };
                if(ped > 0 && Game::GetCharPos(ped, me) && (fabsf(me.x - hp.x) + fabsf(me.y - hp.y) > 40.0f) && !Game::IsCharInAnyCar(ped))
                    Game::Teleport(ped, { hp.x + 1.5f, hp.y - 1.0f, hp.z }, c.f);
            }
            else SysMsg(c.b ? COL_GREEN : COL_RED, c.b ? "*** Co-op: mission passed - the new progress comes with the host's next save." : "*** Co-op: the mission is over.");
            break;
        case COOP_WEAPON:
            if(!s_Co.host && c.a > 0 && c.a < 47) Game::GiveWeaponAmmo(Game::LocalPed(), (int)c.a, (int)std::min<uint32_t>(c.b, 9999));
            break;
        case COOP_REWARD:
            if(!s_Co.host && c.a > 0 && c.a < 10000000) { Game::AddMoney((int)c.a); s_Co.keepMoney += (int)c.a; SysMsg(COL_GREEN, "*** Co-op reward: $%u", c.a); }
            break;
        case COOP_CUT:
            if(s_Co.host) break;
            s_Co.hostCut = c.a == 1; s_Co.cutPos = { c.pos[0], c.pos[1], c.pos[2] }; s_Co.cutH = c.f;
            break;
        case COOP_WORLD:
            if(!s_Co.host && !s_bWorldSynced) { Game::SetTimeOfDay((int)(c.a / 60) % 24, (int)(c.a % 60)); if(c.b < 256 && (int)c.b != Game::GetWeather()) Game::ForceWeatherNow((int)c.b); }
            break;
    }
}

// ---- every frame --------------------------------------------------------------------------------
static void CoopProcess(int ped, uint64_t now)
{
    if(client.GetState() != NET_CONNECTED) { if(s_Co.partner >= 0 || Progress::InCoop()) CoopReset(true); return; }
    if(s_Co.partner >= 0 && !players[s_Co.partner].active) { SysMsg(COL_SYSTEM, "*** Co-op story ended (your partner left)."); CoopReset(true); return; }
    if(s_Co.invitedBy >= 0 && now - s_Co.inviteAt > 30000) { CoopSendTo(COOP_DECLINE, s_Co.invitedBy); s_Co.invitedBy = -1; }
    if(!CoopOn() || ped <= 0) return;
    bool onMission = false, cut = false; char mname[8] = "";
    Game::GetMissionState(onMission, cut, mname);
    const bool realCut = Game::IsRealCutscene();
    if(s_Co.host)
    {
        CoopPumpSend(now);
        // a new save (mission passed -> save, autosave, save house): to the partner, not mid-mission
        if(now - s_Co.saveScanAt > 2000 && s_Co.out.empty())
        {
            s_Co.saveScanAt = now; time_t mt = 0;
            if(CoopNewestSlot(&mt) >= 0 && mt > s_Co.sentMtime) s_Co.wantSend = true;
            if(s_Co.wantSend && !onMission) CoopStartSend();
        }
        Vec3 me; Game::GetCharPos(ped, me);
        if(onMission != s_Co.wasMission)
        {
            if(onMission)
            {
                s_Co.missionMoney = Game::GetMoney();
                for(int s = 1; s <= 13; ++s) { int w = 0, a = 0; if(!Game::GetWeaponInSlot(ped, s, w, a)) w = a = 0; s_Co.lastW[s][0] = w; s_Co.lastW[s][1] = a; }
                CoopSend(COOP_MISSION, 1, 0, &me, Game::GetCharHeading(ped), mname);
            }
            else
            {
                const int gain = Game::GetMoney() - s_Co.missionMoney;
                const bool passed = gain > 0 && !Game::IsCharDead(ped);
                CoopSend(COOP_MISSION, 0, passed ? 1 : 0, &me, 0, mname);
                if(passed) CoopSend(COOP_REWARD, (uint32_t)gain);
            }
            s_Co.wasMission = onMission;
        }
        if(onMission && now - s_Co.wScanAt > 700)
        {   // weapons the mission hands the host
            s_Co.wScanAt = now;
            for(int s = 1; s <= 13; ++s)
            {
                int w = 0, a = 0; if(!Game::GetWeaponInSlot(ped, s, w, a)) continue;
                if(w > 0 && (w != s_Co.lastW[s][0] || a > s_Co.lastW[s][1] + 20)) CoopSend(COOP_WEAPON, (uint32_t)w, (uint32_t)std::max(a, 1));
                s_Co.lastW[s][0] = w; s_Co.lastW[s][1] = a;
            }
        }
        if(realCut != s_Co.wasCut || (realCut && now - s_Co.cutAnimAt > 3000))
        {
            CoopSend(COOP_CUT, realCut ? 1 : 0, 0, &me, Game::GetCharHeading(ped));
            s_Co.wasCut = realCut; s_Co.cutAnimAt = now;
        }
        if(now - s_Co.worldAt > 5000) { s_Co.worldAt = now; int h = 0, m = 0; Game::GetTimeOfDay(h, m); CoopSend(COOP_WORLD, (uint32_t)(h * 60 + m), (uint32_t)Game::GetWeather()); }
        return;
    }
    // ---- partner ----
    if(s_Co.loadAsked && !onMission && !cut && Game::IsReady() && !Progress::Pending())
    {
        s_Co.loadAsked = false;
        CoopKeepMine();
        if(!Progress::CoopLoad(s_Co.in.data(), s_Co.in.size(), now)) { CoopSend(COOP_SAVE_DONE, 0); s_Co.kept = false; }
        s_Co.in.clear(); s_Co.in.shrink_to_fit(); s_Co.inSize = 0;
    }
    if(const int r = Progress::CoopTakeResult())
    {
        CoopPutMineBack();
        CoopSend(COOP_SAVE_DONE, r > 0 ? 1 : 0);
        if(r > 0) SysMsg(COL_GREEN, "*** Co-op: you have the host's story progress now (your place, money and weapons kept).");
    }
    // the host's cutscene: stand next to the host's CJ, listen / nod / look, wait together
    if(s_Co.hostCut)
    {
        const int hp = players[s_Co.partner].ped;
        Vec3 me; Game::GetCharPos(ped, me);
        const float r = s_Co.cutH * 0.0174533f;
        // beside CJ: 1.4 m to his right, 0.6 m behind
        const Vec3 spot = { s_Co.cutPos.x + cosf(r) * 1.4f + sinf(r) * 0.6f, s_Co.cutPos.y + sinf(r) * 1.4f - cosf(r) * 0.6f, s_Co.cutPos.z };
        if(!s_Co.wasCut)
        {
            s_Co.wasCut = true; s_Co.cutAnimAt = 0;
            SysMsg(COL_SYSTEM, "*** Co-op: %s is in a cutscene - you're standing with them.", players[s_Co.partner].name);
            if(Game::IsCharInAnyCar(ped)) Game::WarpOutOfCar(ped, spot);
            Game::Teleport(ped, spot, s_Co.cutH);
            Game::SetPlayerControl(false);
        }
        else if(fabsf(me.x - spot.x) + fabsf(me.y - spot.y) > 4.0f) Game::Teleport(ped, spot, s_Co.cutH);   // follows CJ when he moves
        if(now - s_Co.cutAnimAt > 4500)
        {
            static const char* const kAnims[4][2] = { { "IDLE_chat", "PED" }, { "endchat_01", "PED" }, { "IDLE_stance", "PED" }, { "endchat_03", "PED" } };
            s_Co.cutAnimAt = now; s_Co.cutAnim = (s_Co.cutAnim + 1) % 4;
            Game::PlayAnimOnce(ped, kAnims[s_Co.cutAnim][0], kAnims[s_Co.cutAnim][1], 4.0f, false, 4000);
            if(hp > 0) Game::TaskLookAtChar(ped, hp, 5000);
        }
    }
    else if(s_Co.wasCut) { s_Co.wasCut = false; Game::SetPlayerControl(true); Game::TaskStandStill(ped); }
}

// ---- UI ------------------------------------------------------------------------------------------
static void CoopInvite(int id)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active || id == myId) { SysMsg(COL_RED, "No such player."); return; }
    if(!(s_nServerPerms & PERM_MISSION_PAIR)) { SysMsg(COL_RED, "Co-op story needs Mission sync on this server."); return; }
    if(client.GetServerBuild() < 41315) { SysMsg(COL_YELLOW, "This server runs an older MP: no Co-op story yet."); return; }
    if(CoopOn()) { SysMsg(COL_YELLOW, "You're already in a Co-op story (/coop end first)."); return; }
    CoopSendTo(COOP_INVITE, id); s_Co.askedTo = id;
    SysMsg(COL_SYSTEM, "*** Asked %s for Co-op story (you host: your save is the story). Waiting...", players[id].name);
}
static void CoopAccept()
{
    if(s_Co.invitedBy < 0) return;
    const int id = s_Co.invitedBy; s_Co.invitedBy = -1;
    if(!players[id].active) return;
    CoopSendTo(COOP_ACCEPT, id);
}
static void CoopDecline() { if(s_Co.invitedBy >= 0) CoopSendTo(COOP_DECLINE, s_Co.invitedBy); s_Co.invitedBy = -1; }

static void CoopButton(int i, float w)
{
    const char* lbl; ImVec4 c(1, 1, 1, 0.94f); int act = 0;
    if(CoopIsPartnerOf(i))           { lbl = "End Co-op story##co"; c = ImVec4(0.85f, 0.25f, 0.25f, 0.95f); act = 3; }
    else if(s_Co.invitedBy == i)     { lbl = "Accept Co-op story##co"; act = 2; }
    else if(CoopOn())                { lbl = "In a Co-op story##co"; c = ImVec4(0.5f, 0.5f, 0.5f, 0.9f); }
    else if(s_Co.askedTo == i)       { lbl = "Co-op asked...##co"; c = ImVec4(0.95f, 0.80f, 0.35f, 0.95f); }
    else                             { lbl = "Co-op story (2 players)##co"; act = 1; }
    ImGui::PushStyleColor(ImGuiCol_Button, c);
    if(ImGui::Button(lbl, ImVec2(w, UI::S(68))) && act)
    {
        if(act == 1) CoopInvite(i); else if(act == 2) CoopAccept(); else { CoopSendTo(COOP_END, i); }
    }
    ImGui::PopStyleColor();
}

static void CoopRender()
{
    if(s_Co.invitedBy < 0) return;
    const int id = s_Co.invitedBy;
    if(id >= MP_MAX_PLAYERS || !players[id].active) { s_Co.invitedBy = -1; return; }
    const uint64_t age = MP_NowMs() - s_Co.inviteAt;
    const ImVec2 ds = ImGui::GetIO().DisplaySize; const float w = UI::S(700);
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - w * 0.5f, ds.y * 0.17f)); ImGui::SetNextWindowSize(ImVec2(w, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.75f));
    if(ImGui::Begin("##MPCoopPopup", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::TextUnformatted("Co-op story request (2 players)");
        ImGui::Separator();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(UI::FromRGBA(players[id].color | 0xFF)), "%s", players[id].name);
        ImGui::SameLine(0, UI::S(8));
        ImGui::TextWrapped("wants to play the story with you. Their save is the story: your mission progress becomes theirs until the co-op ends (your own saves are backed up and come back). Your place, money and weapons stay yours.");
        ImGui::TextDisabled("Closes in %d s", (int)((30000 - std::min<uint64_t>(age, 30000)) / 1000));
        const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if(ImGui::Button("Accept##co", ImVec2(half, UI::S(70)))) CoopAccept();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.25f, 0.95f));
        if(ImGui::Button("Decline##co", ImVec2(half, UI::S(70)))) CoopDecline();
        ImGui::PopStyleColor();
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

static bool CoopCommand(const char* cmd, const char* arg)
{
    if(strcmp(cmd, "coop")) return false;
    while(arg && *arg == ' ') ++arg;
    if(!arg || !*arg || !strncasecmp(arg, "help", 4))
    {
        SysMsg(COL_YELLOW, "/coop invite <id> - Co-op story with one player (you host).  /coop accept  /coop decline  /coop end");
        SysMsg(COL_YELLOW, "/coop sync - send your save again (host).  /coop tp - go to your co-op partner.");
        if(CoopOn()) SysMsg(COL_SYSTEM, "Co-op story with %s (%s).", players[s_Co.partner].name, s_Co.host ? "you host" : "partner");
        return true;
    }
    if(!strncasecmp(arg, "invite", 6)) { const char* a = arg + 6; while(*a == ' ') ++a; if(!*a) SysMsg(COL_YELLOW, "/coop invite <id>"); else CoopInvite(atoi(a)); return true; }
    if(!strncasecmp(arg, "accept", 6)) { if(s_Co.invitedBy < 0) SysMsg(COL_YELLOW, "Nobody invited you."); else CoopAccept(); return true; }
    if(!strncasecmp(arg, "decline", 7)) { CoopDecline(); return true; }
    if(!strncasecmp(arg, "end", 3) || !strncasecmp(arg, "leave", 5)) { if(s_Co.partner >= 0) CoopSendTo(COOP_END, s_Co.partner); else SysMsg(COL_YELLOW, "You're not in a Co-op story."); return true; }
    if(!CoopOn()) { SysMsg(COL_YELLOW, "You're not in a Co-op story."); return true; }
    if(!strncasecmp(arg, "sync", 4)) { if(s_Co.host) { if(CoopStartSend()) SysMsg(COL_SYSTEM, "Co-op: sending your save to %s...", players[s_Co.partner].name); } else SysMsg(COL_YELLOW, "Only the host sends the story."); return true; }
    if(!strncasecmp(arg, "tp", 2))
    {
        const int ped = Game::LocalPed(), hp = players[s_Co.partner].ped; Vec3 p;
        if(ped > 0 && hp > 0 && Game::GetCharPos(hp, p)) { Game::Teleport(ped, { p.x + 1.5f, p.y, p.z }, 0); SysMsg(COL_SYSTEM, "Teleported to %s.", players[s_Co.partner].name); }
        return true;
    }
    SysMsg(COL_YELLOW, "/coop help");
    return true;
}
