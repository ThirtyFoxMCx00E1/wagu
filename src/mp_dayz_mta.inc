// MP 3.59 - Project Day Z Legacy: the MTA DayZ systems (NullSystemWorks mtadayz 0.9.9.5a) + the
// Apex-style loot beams / glow / party markers / downed + revive. Included from mp_dayz.inc.
//
//   textures (the original MTA DayZ art, tools/dz_mta_assets.py): dz_hud.png (compass ring + rose,
//   GPS frame, inventory panel, skill icons, fuel, vehicle parts), dz_items.png (an icon per item),
//   dz_status.png (blood / food / drink / temperature / ear / eye / humanity), dz_map.png (the GPS
//   map), dz_spawn.png (spawn selection), dz_journal.png (journal), dz_achbg.png (achievement)

// ---- state ---------------------------------------------------------------------------------------
struct DzBodyC { int id; Vec3 p; char victim[32]; uint64_t t; };
struct DzXState
{
    // fake death (DOWN) + revive
    bool        downed = false; uint64_t downAt = 0, downAnimAt = 0, giveUpAt = 0; int downAnim = 0;
    uint8_t     downedP[MP_MAX_PLAYERS] = {}; uint64_t remoteAnimAt = 0;
    int         reviveTarget = -1; uint64_t reviveAt = 0;
    // TAB player list
    bool        listOpen = false; std::vector<DzListRow> list; uint16_t listTotal = 0, listMax = 0; uint64_t listAsk = 0; int listSel = -1;
    // journal (6 / F1): 0 debug monitor, 1 achievements, 2 skills
    bool        journal = false; int jTab = 0;
    // keys 7 (GPS) / 8 (compass)
    bool        gpsOff = false, bigCompass = false;
    // conditions + temperature
    uint16_t    cond = 0; float temp = 37.0f, tempRate = 0.0f; uint64_t lastTempTick = 0, lastStatusTx = 0, lastBleed = 0, lastLegs = 0, lastCold = 0;
    int         netHit = 0;   // damage that came from the network this frame (not a fall)
    // weather from the server (sunny / cloudy / rain)
    int         weather = 0, rain = 0;
    // loot glow / beams: line of sight per loot spot
    std::unordered_map<uint16_t, bool> los; size_t losCursor = 0;
    int         aimLoot = -1, aimBody = -1;
    // bodies (Check Body)
    std::vector<DzBodyC> bodies;
    // inventory: right panel tab 0 bag, 1 ground
    int         bagTab = 0; int bagSel = -1; int groundSel = -1; int bodySel = -1;
    // refuel at a station
    uint64_t    stationAsk = 0;
    // optics + skin
    bool        binoc = false, range = false; int wantSkin = -1; uint64_t skinTry = 0;
    // achievement banner
    char        achName[64] = ""; uint64_t achAt = 0; bool houseSent = false;
    // zombie alarm (restricted areas without a keycard)
    int         alarmZeds = 0;
    // spawn selection
    bool        spawnSel = false;
};
static DzXState s_DzX;
static void DzXReset() { s_DzX = DzXState(); }
static int  DzZedAlarmTake() { int n = std::min(s_DzX.alarmZeds, 4); s_DzX.alarmZeds -= n; return n; }
static bool DzXDowned() { return s_DzX.downed; }
static bool DzXSpawnSelect() { s_DzX.spawnSel = true; return true; }
static void DzXJournal(int tab) { s_DzX.journal = true; s_DzX.jTab = (tab >= 0 && tab <= 2) ? tab : 0; }
static void DzXToggleList() { s_DzX.listOpen = !s_DzX.listOpen; s_DzX.listAsk = 0; }
static void DzXGpsToggle() { s_DzX.gpsOff = !s_DzX.gpsOff; }
static void DzXSpawned() { s_DzX.downed = false; s_DzX.downAt = 0; s_DzX.cond = 0; s_DzX.temp = 37.0f; s_DzX.spawnSel = false; s_DzX.reviveTarget = -1; s_DzX.bodies.clear(); }

// ---- texture helpers ----------------------------------------------------------------------------
static bool DzImgRect(ImDrawList* dl, const char* file, float W, float H, const DzRect& r, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE)
{
    ImVec2 uv; void* t = HUD::Texture(file, &uv); if(!t) return false;
    dl->AddImage((ImTextureID)t, a, b, ImVec2(r.x / W * uv.x, r.y / H * uv.y), ImVec2((r.x + r.w) / W * uv.x, (r.y + r.h) / H * uv.y), col);
    return true;
}
static bool DzImgFull(ImDrawList* dl, const char* file, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE)
{
    ImVec2 uv; void* t = HUD::Texture(file, &uv); if(!t) return false;
    dl->AddImage((ImTextureID)t, a, b, ImVec2(0, 0), uv, col); return true;
}
static bool DzHud(ImDrawList* dl, const DzRect& r, ImVec2 a, ImVec2 b, ImU32 col = IM_COL32_WHITE) { return DzImgRect(dl, "dz_hud.png", 1024, 1024, r, a, b, col); }
static bool DzStatus(ImDrawList* dl, int cell, ImVec2 a, float s, ImU32 col = IM_COL32_WHITE)
{
    if(cell < 0 || cell >= DZST_COUNT) return false;
    DzRect r = { (short)((cell % DZST_COLS) * 128), (short)((cell / DZST_COLS) * 128), 128, 128 };
    return DzImgRect(dl, "dz_status.png", DZST_COLS * 128.0f, DZST_ROWS * 128.0f, r, a, ImVec2(a.x + s, a.y + s), col);
}
static bool DzItemIcon(ImDrawList* dl, int item, ImVec2 a, float s, ImU32 col = IM_COL32_WHITE)
{
    if(item <= 0 || item >= 64) return false;
    DzRect r = { (short)((item % 8) * 96), (short)((item / 8) * 96), 96, 96 };
    return DzImgRect(dl, "dz_items.png", 768, 768, r, a, ImVec2(a.x + s, a.y + s), col);
}
// the image rotated around its centre (compass rose)
static void DzHudRot(ImDrawList* dl, const DzRect& r, ImVec2 c, float half, float deg, ImU32 col = IM_COL32_WHITE)
{
    ImVec2 uv; void* t = HUD::Texture("dz_hud.png", &uv); if(!t) return;
    float a = deg * 0.0174533f, cs = cosf(a), sn = sinf(a);
    ImVec2 p[4]; const float ox[4] = { -1, 1, 1, -1 }, oy[4] = { -1, -1, 1, 1 };
    for(int i = 0; i < 4; ++i) p[i] = ImVec2(c.x + (ox[i] * cs - oy[i] * sn) * half, c.y + (ox[i] * sn + oy[i] * cs) * half);
    float u0 = r.x / 1024.0f * uv.x, v0 = r.y / 1024.0f * uv.y, u1 = (r.x + r.w) / 1024.0f * uv.x, v1 = (r.y + r.h) / 1024.0f * uv.y;
    dl->AddImageQuad((ImTextureID)t, p[0], p[1], p[2], p[3], ImVec2(u0, v0), ImVec2(u1, v0), ImVec2(u1, v1), ImVec2(u0, v1), col);
}

// ---- small helpers --------------------------------------------------------------------------------
static bool DzIsParty(int i)
{
    if(i < 0 || i >= MP_MAX_PLAYERS || i == myId || !players[i].active) return false;
    if(!s_Dz.haveMe || !s_Dz.me.partyCol || !s_Dz.me.partyN) return false;
    return (players[i].color | 0xFF) == (s_Dz.me.partyCol | 0xFF);
}
static bool DzHasItem(int item) { for(int i = 0; i < s_Dz.inv.n; ++i) if(s_Dz.inv.s[i].item == item) return true; return false; }
static int  DzInvKind(int kind) { for(int i = 0; i < s_Dz.inv.n; ++i) if(DzItemInfo(s_Dz.inv.s[i].item).kind == kind) return i; return -1; }
static bool DzXCompassToggle() { if(!DzHasItem(DZI_COMPASS)) return false; s_DzX.bigCompass = !s_DzX.bigCompass; return true; }
static int  DzSkillRank(int k) { return s_Dz.haveMe && k >= 0 && k < DZSK_COUNT ? s_Dz.me.skills[k] : 0; }
static void DzItemDesc(int item, char* o, int n)
{
    const DzItem& it = DzItemInfo(item);
    switch(it.kind)
    {
        case DZK_WEAPON: if(it.ammo > 1) snprintf(o, n, "Weapon - %d rounds per magazine", it.ammo); else snprintf(o, n, "Melee weapon"); break;
        case DZK_AMMO:   snprintf(o, n, "%d rounds for the %s", it.val, DzWeaponItem(it.ammo) ? DzItemInfo(DzWeaponItem(it.ammo)).name : "guns"); break;
        case DZK_FOOD:   snprintf(o, n, "Restores %d%% hunger", it.val); break;
        case DZK_DRINK:  snprintf(o, n, "Restores %d%% thirst", it.val); break;
        case DZK_MED:    snprintf(o, n, item == DZI_BANDAGE ? "Heals %d blood - stops the bleeding" : item == DZI_PAINKILLERS ? "Heals %d blood - stops the pain (camera sway)" : "Heals %d blood - stops bleeding + pain", it.val); break;
        case DZK_CURE:   snprintf(o, n, "Cures the infection"); break;
        case DZK_BAG:    snprintf(o, n, "Backpack - carry %d KG", it.val); break;
        case DZK_ARMOR:  snprintf(o, n, item == DZI_HELMET ? "+%d armour - stops headshots" : "+%d armour", it.ammo); break;
        case DZK_GPS:    snprintf(o, n, "Unlocks the map (key 7)"); break;
        case DZK_FUEL:   snprintf(o, n, "+%d%% fuel for the car next to you", it.val); break;
        case DZK_REVIVE: snprintf(o, n, item == DZI_MORPHINE ? "Revives a downed party member (+%d%% HP) - fixes bones" : "Revives a downed party member (+%d%% HP)", it.val); break;
        case DZK_CLOTHES: snprintf(o, n, item == DZI_CLOTHES_GHILLIE ? "Clothes - almost invisible when still" : item == DZI_CLOTHES_CAMO ? "Clothes - harder to see" : "Clothes - change your skin"); break;
        case DZK_TRASH:  snprintf(o, n, "Trash - collect 5 cans: Litterbug"); break;
        case DZK_SPECIAL:
            switch(it.val)
            {
                case DZSP_HEATPACK: snprintf(o, n, "Warms you up (+3 C), cures the cold"); break;
                case DZSP_TIRE: snprintf(o, n, "Fixes the tyres of the car next to you"); break;
                case DZSP_ENGINE: snprintf(o, n, "+400 engine health (needs a Toolbox)"); break;
                case DZSP_SCRAP: snprintf(o, n, "+200 vehicle health (needs a Toolbox)"); break;
                case DZSP_EMPTYCAN: snprintf(o, n, "Fill it at a petrol station or SIPHON a car"); break;
                case DZSP_KEYCARD: snprintf(o, n, "Area 69 + the carrier: the alarm stays silent"); break;
                case DZSP_BINOCULARS: snprintf(o, n, "Look far (tap USE to toggle)"); break;
                default: snprintf(o, n, "Shows the distance to what you aim at"); break;
            }
            break;
        default: snprintf(o, n, item == DZI_TOOLBOX ? "Repairs: needed for engines and scrap metal" : item == DZI_COMPASS ? "The compass (key 8)" : item == DZI_MAP ? "A paper map of San Andreas" : item == DZI_WATCH ? "Shows the time" : "Item"); break;
    }
}
static uint32_t DzRarityCol(int item)
{
    const DzItem& it = DzItemInfo(item);
    switch(it.kind)
    {
        case DZK_WEAPON: return item == DZI_AK_GOLD ? 0xFACC15FFu : (it.ammo >= 30 || it.val == 34 ? 0xC084FCFFu : 0x60A5FAFFu);
        case DZK_AMMO: return 0xE5E7EBFFu;
        case DZK_MED: case DZK_CURE: case DZK_REVIVE: return 0xF87171FFu;
        case DZK_FOOD: case DZK_DRINK: return 0x86EFACFFu;
        case DZK_ARMOR: case DZK_BAG: return 0x60A5FAFFu;
        case DZK_TRASH: return 0x9CA3AFFFu;
        default: return 0xFFFFFFFFu;
    }
}
static void DzNoiseVis(int ped, int& noise, int& vis)
{   // MTA DayZ sound + visibility levels (0-5): what the zombies use to find you
    float sp = Game::GetCharSpeed(ped); bool inCar = Game::IsCharInAnyCar(ped), duck = Game::IsCharDucking(ped), shoot = Game::IsCharShooting(ped);
    int ms = Game::GetPedMoveState(ped);
    noise = shoot ? 5 : inCar ? 4 : (ms >= 7 || sp > 5.0f) ? 3 : (ms >= 4 || sp > 1.5f) ? 2 : duck ? 0 : 1;
    int hr = Game::GetHour(); bool night = hr >= 21 || hr < 5;
    vis = inCar ? 5 : duck ? 1 : (ms >= 7 || sp > 5.0f) ? 4 : (sp > 1.5f ? 3 : 2);
    if(night) vis -= 1; if(s_Dz.fog) vis -= 2; if(s_DzX.rain) vis -= 1;
    int skin = s_Dz.haveMe ? s_Dz.me.skin : 0;
    if(skin == 287) vis -= 1;
    if(skin == 285) vis -= (sp < 0.5f ? 3 : 1);   // ghillie: almost invisible when still
    noise -= DzSkillRank(DZSK_SHADOW); vis -= DzSkillRank(DZSK_CAMO);
    if(noise < 0) noise = 0; if(noise > 5) noise = 5; if(vis < 0) vis = 0; if(vis > 5) vis = 5;
}

// ---- fake death (DOWN at 5% HP) + revive ---------------------------------------------------------
// 11 lying "dead" animations (PED knock-outs + CRACK death)
struct DzDownAnim { const char* anim; const char* file; };
static const DzDownAnim kDzDownAnims[11] = {
    { "KO_shot_front", "PED" }, { "KO_shot_face", "PED" }, { "KO_shot_stom", "PED" }, { "KO_skid_back", "PED" }, { "KO_skid_front", "PED" },
    { "KO_spin_L", "PED" }, { "KO_spin_R", "PED" }, { "KD_left", "PED" }, { "KD_right", "PED" }, { "crckdeth1", "CRACK" }, { "crckdeth2", "CRACK" } };
static int DzBleedOutSec()
{
    bool party = s_Dz.haveMe && s_Dz.me.partyN > 1;
    return (party ? 120 : 45) + 15 * DzSkillRank(DZSK_SECOND);
}
static void DzDownStart(int ped, uint64_t now)
{
    s_DzX.downed = true; s_DzX.downAt = now; s_DzX.downAnimAt = 0; s_DzX.giveUpAt = 0;
    s_DzX.downAnim = rand() % 11;
    Game::SetCharHealth(ped, 5); s_Dz.prevHp = 5;
    Game::SetCurrentWeapon(ped, 0);
    Game::ClearTasksNow(ped);
    s_Dz.invOpen = false; s_Dz.panelOpen = false;
    DzSend(DZCMD_DOWN, 1);
    s_Dz.combatUntil = std::max<uint64_t>(s_Dz.combatUntil, now + 15000);
    DzNotice(DZ_RED, "You are DOWN - wait for your party to revive you.");
}
static void DzDownEnd(int ped, int addHp)
{
    if(!s_DzX.downed) return;
    s_DzX.downed = false;
    Game::FreezeChar(ped, false); Game::SetPlayerControl(true); Game::ClearTasksNow(ped);
    Game::PlayAnimOnce(ped, "getup_front", "PED", 4.0f, false, 1600);
    int hp = Game::GetCharHealth(ped) + addHp; if(hp > 100) hp = 100; if(hp < 1) hp = 1;
    Game::SetCharHealth(ped, hp); s_Dz.prevHp = hp;
    s_Dz.nums.push_back({ {0,0,0}, false, 0, 0, addHp * 120, MP_NowMs(), true });
}
static void DzDownTick(int ped, uint64_t now)
{
    if(!s_DzX.downed) return;
    Game::FreezeChar(ped, true);
    if(now % 1000 < 40) Game::SetPlayerControl(false);
    if(!s_DzX.downAnimAt || now - s_DzX.downAnimAt > 7000)
    {
        const DzDownAnim& a = kDzDownAnims[s_DzX.downAnim];
        if(Game::PlayAnimOnce(ped, a.anim, a.file, s_DzX.downAnimAt ? 1000.0f : 4.0f, true, -1)) s_DzX.downAnimAt = now;
    }
    int hp = Game::GetCharHealth(ped); if(hp > 5) { Game::SetCharHealth(ped, 5); s_Dz.prevHp = 5; }
    if(now - s_DzX.downAt > (uint64_t)DzBleedOutSec() * 1000ULL)
    {   // bled out: the real death
        s_DzX.downed = false; Game::FreezeChar(ped, false); Game::SetPlayerControl(true);
        Game::SetCharHealth(ped, 0);
    }
}
// the network damage hook (mp.cpp): returns the damage to take. Headshots kill unless you wear a
// helmet; anything that would drop you under 5% puts you DOWN instead (the fake death).
static int DzAdjustDamage(int hp, int dmg, bool instant)
{
    if(!DzOnServer() || s_Dz.view != 2) return dmg;
    s_DzX.netHit += dmg;
    if(s_DzX.downed) return dmg;   // finished off
    bool helmet = s_Dz.haveMe && s_Dz.me.armor[DZA_HELMET] != 0;
    if(instant) { if(!helmet) return dmg; dmg = 45; DzNotice(DZ_YEL, "Your helmet stopped a headshot."); }
    if(hp - dmg <= 5 && hp > 5) { dmg = hp - 5; }
    else if(hp <= 5) dmg = 0;
    return dmg;
}
static void DzRemoteDownTick(uint64_t now)
{
    if(now - s_DzX.remoteAnimAt < 2500) return;
    s_DzX.remoteAnimAt = now;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!s_DzX.downedP[i] || i == myId || !players[i].active || players[i].ped <= 0) continue;
        const DzDownAnim& a = kDzDownAnims[i % 11];
        Game::PlayAnimOnce(players[i].ped, a.anim, a.file, 1000.0f, true, -1);
    }
}
static int DzReviveCandidate(int ped)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!s_DzX.downedP[i] || !DzIsParty(i) || players[i].ped <= 0) continue;
        Vec3 p; if(!Game::GetCharPos(players[i].ped, p)) continue;
        float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z;
        if(dx * dx + dy * dy + dz * dz < 2.6f * 2.6f) return i;
    }
    return -1;
}
static void DzReviveTick(int ped, uint64_t now)
{
    if(s_DzX.reviveTarget < 0) return;
    if(now - s_DzX.reviveAt > 3600)
    {
        DzSend(DZCMD_REVIVE, s_DzX.reviveTarget);
        s_DzX.reviveTarget = -1; Game::FreezeChar(ped, false); Game::ClearTasksNow(ped);
    }
}
static void DzStartRevive(int ped, int target)
{
    if(DzInvKind(DZK_REVIVE) < 0) { DzNotice(DZ_RED, "You need a medicine: Morphine, Epinephrine or a Blood bag."); return; }
    s_DzX.reviveTarget = target; s_DzX.reviveAt = MP_NowMs();
    Vec3 p; if(players[target].ped > 0 && Game::GetCharPos(players[target].ped, p)) { Vec3 me; Game::GetCharPos(ped, me); Game::SetCharHeading(ped, atan2f(-(p.x - me.x), p.y - me.y) * 57.2958f); }
    Game::FreezeChar(ped, true);
    if(!Game::PlayAnimOnce(ped, "CPR", "MEDIC", 4.0f, false, 3500)) Game::PlayAnimOnce(ped, "BOM_Plant", "BOMBER", 4.0f, false, 3500);
}

// ---- conditions + temperature (MTA DayZ: bleeding, pain, broken legs, fractured arms, cold) -------
static void DzCondTick(int ped, int hp, int prevHp, uint64_t now)
{
    if(!s_Dz.haveMe) return;
    float dt = s_DzX.lastTempTick ? (now - s_DzX.lastTempTick) / 1000.0f : 0.0f; if(dt > 1.0f) dt = 1.0f; s_DzX.lastTempTick = now;
    bool inCar = Game::IsCharInAnyCar(ped); int ms = Game::GetPedMoveState(ped);
    int hr = Game::GetHour(); bool night = hr >= 21 || hr < 5;
    // temperature: night / rain / fog cool you down, sprinting / cars / heat packs warm you
    float rate = 0.0f;
    if(night) rate -= 0.0025f;
    if(s_DzX.rain && !inCar) rate -= 0.004f;
    if(s_Dz.fog) rate -= 0.0015f;
    if(ms >= 7) rate += 0.005f * std::max(1, (int)g_DzVarDefs[DZV_TEMPSPRINT].def);
    if(inCar) rate += 0.004f;
    if(!night && !s_DzX.rain && s_DzX.temp < 37.0f) rate += 0.003f;
    if(s_DzX.temp > 37.2f && ms < 7) rate -= 0.003f;
    s_DzX.temp += rate * dt; s_DzX.tempRate += (rate - s_DzX.tempRate) * std::min(1.0f, dt * 0.5f);
    if(s_DzX.temp < 31.0f) s_DzX.temp = 31.0f; if(s_DzX.temp > 41.0f) s_DzX.temp = 41.0f;
    if(s_DzX.temp < 34.0f) s_DzX.cond |= DZCO_COLD; else if(s_DzX.temp > 35.0f) s_DzX.cond &= ~DZCO_COLD;
    if((s_DzX.cond & DZCO_COLD) && now - s_DzX.lastCold > 12000) { s_DzX.lastCold = now; if(hp > 6) Game::SetCharHealth(ped, hp - 1); }
    // new damage: bleeding / pain / fractured arms; a fall (no zombie, no bullet) breaks the legs
    if(prevHp > 0 && hp < prevHp && !s_DzX.downed)
    {
        int dmg = prevHp - hp;
        bool fall = s_DzX.netHit == 0 && !inCar && dmg >= 8;
        bool zed = false; Vec3 me; if(Game::GetCharPos(ped, me)) for(auto& z : s_Dz.zeds) { Vec3 zp; if(z.ped > 0 && Game::GetCharPos(z.ped, zp) && (zp.x - me.x) * (zp.x - me.x) + (zp.y - me.y) * (zp.y - me.y) < 2.5f * 2.5f) { zed = true; break; } }
        if(fall && !zed) { if(!(s_DzX.cond & DZCO_LEGS)) DzNotice(DZ_RED, "You broke your legs - Morphine fixes them."); s_DzX.cond |= DZCO_LEGS; }
        else if(dmg >= 4)
        {
            if(rand() % 100 < 30 && !(s_DzX.cond & DZCO_BLEEDING)) { s_DzX.cond |= DZCO_BLEEDING; DzNotice(DZ_RED, "You are BLEEDING - use a Bandage."); }
            if(rand() % 100 < 20) s_DzX.cond |= DZCO_PAIN;
            if(s_DzX.netHit > 0 && rand() % 100 < 8) { s_DzX.cond |= DZCO_ARMS; DzNotice(DZ_RED, "Fractured arm - Morphine fixes it."); }
        }
    }
    s_DzX.netHit = 0;
    if((s_DzX.cond & DZCO_BLEEDING) && now - s_DzX.lastBleed > 3500 && !s_DzX.downed)
    {
        s_DzX.lastBleed = now; int h = Game::GetCharHealth(ped);
        if(h > 6) { Game::SetCharHealth(ped, h - 1); s_Dz.prevHp = h - 1; }
        if(s_Dz.opt.blood) s_Dz.drops.push_back({ 0.45f + (rand() % 100) / 1000.0f, 0.55f, 0.0f, now, false });
    }
    if((s_DzX.cond & DZCO_LEGS) && ms >= 6 && now - s_DzX.lastLegs > 2500 && !inCar)
    {   // running on broken legs hurts (and slows you: the run is cut)
        s_DzX.lastLegs = now; int h = Game::GetCharHealth(ped); if(h > 6) { Game::SetCharHealth(ped, h - 1); s_Dz.prevHp = h - 1; }
        Game::ClearTasksNow(ped);
    }
    if(now - s_DzX.lastStatusTx > 2000)
    {
        s_DzX.lastStatusTx = now;
        DzSend(DZCMD_STATUS, s_DzX.cond & 0xFF, s_DzX.cond >> 8, (int32_t)(s_DzX.temp * 100.0f));
    }
}

// ---- events / packets -----------------------------------------------------------------------------
static int DzCarNear(int ped, float r)
{
    if(ped <= 0) return 0;
    if(Game::IsCharInAnyCar(ped)) return Game::GetCarCharIsIn(ped);
    Vec3 p; if(!Game::GetCharPos(ped, p)) return 0;
    return Game::FindNearestCar(p, r);
}
static void DzXEvent(const PktDzEvent& e)
{
    const uint64_t now = MP_NowMs(); int ped = Game::LocalPed();
    switch(e.ev)
    {
        case DZEV_DOWNED:
            if(e.a >= MP_MAX_PLAYERS) break;
            if(e.a == myId) { if(!e.b && s_DzX.downed && ped > 0) DzDownEnd(ped, 0); break; }
            s_DzX.downedP[e.a] = e.b ? 1 : 0;
            if(e.b && DzIsParty(e.a)) DzNotice(DZ_RED, "%s is DOWN - revive them with a medicine.", e.text[0] ? e.text : players[e.a].name);
            if(!e.b && players[e.a].ped > 0) Game::ClearTasksNow(players[e.a].ped);
            break;
        case DZEV_REVIVED: if(ped > 0) DzDownEnd(ped, e.val > 0 ? e.val : 50); DzNotice(DZ_GRN, "%s revived you.", e.text); break;
        case DZEV_REVIVER: if(e.a < MP_MAX_PLAYERS) s_DzX.downedP[e.a] = 0; DzNotice(DZ_GRN, "You revived %s (+150 humanity).", e.text); break;
        case DZEV_COMBAT: s_Dz.combatUntil = std::max<uint64_t>(s_Dz.combatUntil, now + (uint64_t)std::max(1000, (int)e.val)); break;
        case DZEV_CURE:
        {
            uint16_t bits = (uint16_t)(e.a | (e.b << 8)); s_DzX.cond &= ~bits;
            if(e.val) s_DzX.temp = std::min(38.0f, s_DzX.temp + e.val / 100.0f);
            if(bits) DzNotice(DZ_GRN, "%s: %s%s%s%s", e.text, bits & DZCO_BLEEDING ? "bleeding stopped " : "", bits & DZCO_PAIN ? "pain gone " : "", bits & (DZCO_LEGS | DZCO_ARMS) ? "bones fixed " : "", bits & DZCO_COLD ? "warm " : "");
            break;
        }
        case DZEV_STATION:
        {
            int car = DzCarNear(ped, 8.0f);
            if(e.val > 0 && car > 0) { float& f = s_Dz.fuel[car]; f = std::min(100.0f, f + (float)e.val); if(f >= 100.0f) { s_Dz.refueling = false; DzNotice(DZ_GRN, "The tank is full. %s", e.text); } }
            else { s_Dz.refueling = false; DzNotice(e.a ? DZ_GRY : DZ_RED, "%s", e.text); }
            break;
        }
        case DZEV_SPECIAL:
        {
            if(e.a == 101) { s_DzX.alarmZeds += std::max(1, (int)e.val); DzNotice(DZ_RED, "ALARM - %s: the zombies are coming!", e.text); break; }
            if(e.a == DZSP_BINOCULARS) { s_DzX.binoc = !s_DzX.binoc; break; }
            if(e.a == DZSP_RANGEFINDER) { s_DzX.range = !s_DzX.range; break; }
            int car = DzCarNear(ped, 7.0f);
            if(car <= 0) { DzNotice(DZ_GRY, "No vehicle next to you - %s wasted.", e.text); break; }
            if(e.a == DZSP_TIRE) { int h = Game::GetCarHealth(car); Game::FixCar(car); Game::SetCarHealth(car, h); DzNotice(DZ_GRN, "Tyres fixed."); }
            else { int h = std::min(1000, Game::GetCarHealth(car) + (int)e.val); Game::SetCarHealth(car, h); DzNotice(DZ_GRN, "%s fitted: vehicle health %d.", e.text, h); }
            break;
        }
        case DZEV_SKIN: s_DzX.wantSkin = e.val; s_DzX.skinTry = 0; break;
        case DZEV_WEATHER:
            s_DzX.weather = e.a; s_DzX.rain = e.b;
            if(!s_Dz.fog && e.a) Game::ForceWeatherNow(e.b ? 8 : e.a);
            break;
        case DZEV_ACH: MP_CopyStr(s_DzX.achName, e.text, sizeof(s_DzX.achName)); s_DzX.achAt = now; break;
        case DZEV_BODY:
            if(e.a == 0) { DzBodyC b; b.id = e.val; b.p = { e.pos[0], e.pos[1], e.pos[2] }; MP_CopyStr(b.victim, e.text, sizeof(b.victim)); b.t = now; s_DzX.bodies.push_back(b); if(s_DzX.bodies.size() > 40) s_DzX.bodies.erase(s_DzX.bodies.begin()); }
            else for(size_t i = 0; i < s_DzX.bodies.size(); ++i) if(s_DzX.bodies[i].id == e.val) { s_DzX.bodies.erase(s_DzX.bodies.begin() + i); break; }
            break;
        case DZEV_VEHRESPAWN: s_Dz.fuel.clear(); DzNotice(DZ_YEL, "[DayZAdmin] The vehicles were respawned."); break;
    }
}
static void DzXList(const uint8_t* d, int len)
{
    if(len < 8) return;
    const PktDzList* L = (const PktDzList*)d; int n = std::min<int>(L->n, DZ_LIST_PAGE);
    if(len < 8 + n * (int)sizeof(DzListRow)) n = (len - 8) / (int)sizeof(DzListRow);
    if(L->page == 0) s_DzX.list.clear();
    for(int i = 0; i < n; ++i) { DzListRow r; memcpy(&r, &L->r[i], sizeof(r)); s_DzX.list.push_back(r); if(r.id < MP_MAX_PLAYERS && r.id != myId) s_DzX.downedP[r.id] = (r.flags & 2) ? 1 : 0; }
    s_DzX.listTotal = L->total; s_DzX.listMax = L->max;
}

// ---- process (called every frame from DzProcess while playing) -----------------------------------
static void DzXProcess(int ped, int hp, int prevHp, uint64_t now)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    // fake death: 5% HP or less (the game's 10% low-health warning does not count)
    if(!s_DzX.downed && hp > 0 && hp <= 5 && now - s_Dz.spawnedAt > 3000) DzDownStart(ped, now);
    DzDownTick(ped, now);
    DzReviveTick(ped, now);
    DzRemoteDownTick(now);
    DzCondTick(ped, Game::GetCharHealth(ped), prevHp, now);
    // the old clothes / skin system
    if(s_DzX.wantSkin >= 0 && now - s_DzX.skinTry > 1000)
    {
        s_DzX.skinTry = now; int m = s_DzX.wantSkin ? s_DzX.wantSkin : 252;
        if(!Game::ModelReady(m) || Game::SetLocalModel(m)) s_DzX.wantSkin = Game::ModelReady(m) ? -1 : s_DzX.wantSkin;
    }
    // "I Can See My House From Here!": a plane / heli above 300 m
    if(!s_DzX.houseSent && Game::IsCharInAnyCar(ped) && me.z > 300.0f)
    {
        static const int air[] = { 417, 425, 447, 469, 487, 488, 497, 548, 563, 460, 476, 511, 512, 513, 519, 520, 553, 577, 592, 593 };
        int mdl = Game::GetCarModel(Game::GetCarCharIsIn(ped));
        for(int a : air) if(a == mdl) { s_DzX.houseSent = true; DzSend(DZCMD_ACH, DZACH_HOUSE); break; }
    }
    // bodies older than 10 minutes go
    while(!s_DzX.bodies.empty() && now - s_DzX.bodies.front().t > 600000) s_DzX.bodies.erase(s_DzX.bodies.begin());
    // line of sight to a few loot spots per frame (beams + glow never show through walls)
    Vec3 cp, cl;
    if(Game::GetCameraPos(cp, cl) && !s_Dz.loot.empty())
    {
        if(s_DzX.losCursor >= s_Dz.loot.size()) s_DzX.losCursor = 0;
        auto it = s_Dz.loot.begin(); std::advance(it, s_DzX.losCursor);
        for(int k = 0; k < 6 && it != s_Dz.loot.end(); ++k, ++it, ++s_DzX.losCursor)
        {
            const DzLootC& l = it->second; if(!l.zDone) continue;
            float dx = l.x - me.x, dy = l.y - me.y; if(dx * dx + dy * dy > 90.0f * 90.0f) { s_DzX.los[l.idx] = false; continue; }
            s_DzX.los[l.idx] = Game::LineOfSightClear(cp, { l.x, l.y, l.z + 0.30f });
        }
    }
    // keys: TAB player list, 6 / F1 journal, 7 GPS, 8 compass
    if(!ImGui::GetIO().WantTextInput)
    {
        if(ImGui::IsKeyPressed(ImGuiKey_Tab, false)) s_DzX.listOpen = !s_DzX.listOpen;
        if(ImGui::IsKeyPressed(ImGuiKey_6, false) || ImGui::IsKeyPressed(ImGuiKey_F1, false)) s_DzX.journal = !s_DzX.journal;
        if(ImGui::IsKeyPressed(ImGuiKey_7, false)) DzLocalCmd("gps");
        if(ImGui::IsKeyPressed(ImGuiKey_8, false)) DzLocalCmd("compass");
    }
    if(s_DzX.listOpen && now - s_DzX.listAsk > 2000) { s_DzX.listAsk = now; DzSend(DZCMD_LIST); }
}

// ---- zombies: MTA DayZ sound + visibility detection, sprinters, spawn near the loot ---------------
static float DzZedDetectRange(int ped)
{
    int noise, vis; DzNoiseVis(ped, noise, vis);
    return 4.0f + noise * 7.0f + vis * 5.0f;   // level 5 + 5 = 64 m
}

// ---- drawing: status icons (blood, food, drink, temperature, ear / eye, conditions) ---------------
static void DzDrawStatusMta(ImDrawList* dl, ImVec2 ds, int ped, float t)
{
    if(!s_Dz.haveMe) return;
    const PktDzMe& m = s_Dz.me;
    // 3.60: bigger, like the MTA DayZ screenshots. Ear + eye in the right column (above the armour),
    // temperature / blood / food / drink / humanity in a column left of the GTA fire + sprint buttons.
    auto blink = [&](bool warn) { return warn ? DzCol(DZ_RED, 0.55f + 0.45f * sinf(t * 7.0f)) : IM_COL32_WHITE; };
    // ear + eye (sound / visibility level)
    {
        const float es = UI::S(60), ex = ds.x - UI::S(132);
        float ey = ds.y * 0.40f;
        int noise, vis; DzNoiseVis(ped, noise, vis);
        DzStatus(dl, DZST_EAR, ImVec2(ex, ey), es);
        if(noise > 0) DzStatus(dl, DZST_LEVEL1 + noise - 1, ImVec2(ex + es * 0.86f, ey), es, noise >= 4 ? DzCol(DZ_RED) : IM_COL32_WHITE);
        DzStatus(dl, DZST_EYE, ImVec2(ex, ey + es * 0.92f), es);
        if(vis > 0) DzStatus(dl, DZST_LEVEL1 + vis - 1, ImVec2(ex + es * 0.86f, ey + es * 0.92f), es, vis >= 4 ? DzCol(DZ_RED) : IM_COL32_WHITE);
    }
    const float s = UI::S(62), x = ds.x - UI::S(256);
    float y = ds.y * 0.42f;
    // temperature + rising / falling arrows
    float temp = s_DzX.temp; int tl = (int)((temp - 33.0f) / 1.2f + 0.5f); if(tl < 0) tl = 0; if(tl > 4) tl = 4;
    ImU32 tc = temp < 34.5f ? IM_COL32(120, 180, 255, 255) : (temp > 38.5f ? DzCol(DZ_ORG) : IM_COL32_WHITE);
    DzStatus(dl, DZST_TEMP_BORDER, ImVec2(x, y), s); if(tl) DzStatus(dl, DZST_TEMP0 + tl, ImVec2(x, y), s, tc);
    float r = s_DzX.tempRate; int arrows = fabsf(r) < 0.0008f ? 0 : fabsf(r) < 0.003f ? 1 : fabsf(r) < 0.006f ? 2 : 3;
    if(arrows) DzStatus(dl, r < 0 ? DZST_TARROW0 + arrows : DZST_TARROW3 + arrows, ImVec2(x - s * 0.62f, y + s * 0.18f), s * 0.62f, r < 0 ? IM_COL32(120, 180, 255, 255) : DzCol(DZ_ORG));
    char tt[16]; snprintf(tt, sizeof(tt), "%.1fC", temp); DzTextR(dl, UI::S(17), x + s + UI::S(4), y + s - UI::S(16), IM_COL32(230, 230, 230, 220), tt);
    y += s + UI::S(2);
    // blood / food / drink
    struct L { int border, base; float k; bool warn; } ls[3] = {
        { DZST_BLOOD_BORDER, DZST_BLOOD0, m.blood / 12000.0f, m.blood < 4000 },
        { DZST_FOOD_BORDER,  DZST_FOOD0,  m.hunger / 100.0f,  m.hunger < 20 },
        { DZST_DRINK_BORDER, DZST_DRINK0, m.thirst / 100.0f,  m.thirst < 20 } };
    for(int i = 0; i < 3; ++i)
    {
        int lv = (int)ceilf(std::max(0.0f, std::min(1.0f, ls[i].k)) * 5.0f); if(lv > 5) lv = 5;
        DzStatus(dl, ls[i].border, ImVec2(x, y), s, blink(ls[i].warn));
        if(lv) DzStatus(dl, ls[i].base + lv, ImVec2(x, y), s, i == 0 && m.infected ? IM_COL32(110, 230, 120, 255) : blink(ls[i].warn));
        y += s + UI::S(2);
    }
    // conditions row (left of the column): bleeding, infection, broken bones, pain, helmet
    float cx = x - UI::S(4), cy = y - (s + UI::S(2)) * 3; const float cs = s * 0.66f;
    auto cond = [&](int cell, ImU32 c) { cx -= cs + UI::S(2); DzStatus(dl, cell, ImVec2(cx, cy), cs, c); };
    if(s_DzX.cond & DZCO_BLEEDING) cond(DZST_SPLAT, DzCol(DZ_RED, 0.7f + 0.3f * sinf(t * 6)));
    if(m.infected) cond(DZST_INFECTION, IM_COL32(110, 230, 120, 255));
    if(s_DzX.cond & (DZCO_LEGS | DZCO_ARMS)) cond(DZST_BONE, IM_COL32_WHITE);
    if(s_DzX.cond & DZCO_PAIN) cond(DZST_MEDIC, DzCol(DZ_YEL));
    if(m.armor[DZA_HELMET]) cond(DZST_HELMET, DzCol(DZ_GRN));
    // humanity face
    int hu = m.humanity < -5000 ? 0 : m.humanity < -1000 ? 1 : m.humanity < 0 ? 2 : m.humanity < 2500 ? 3 : m.humanity < 5000 ? 4 : 5;
    DzStatus(dl, DZST_HUM0 + hu, ImVec2(x + s * 0.12f, y), s * 0.76f, m.humanity < 0 ? DzCol(DZ_RED) : IM_COL32_WHITE);
}
// pain: the camera sway of MTA DayZ, drawn as a slow pulsing blur at the edges
static void DzDrawPain(ImDrawList* dl, ImVec2 ds, float t)
{
    if(!(s_DzX.cond & DZCO_PAIN) || !s_Dz.haveMe || !s_Dz.me.camSway) return;
    float k = 0.5f + 0.5f * sinf(t * 1.7f); int a = (int)(70 + 70 * k);
    float off = UI::S(14) * sinf(t * 1.3f);
    ImU32 c = IM_COL32(20, 0, 0, a), z = IM_COL32(20, 0, 0, 0);
    dl->AddRectFilledMultiColor(ImVec2(0 + off, 0), ImVec2(ds.x * 0.22f + off, ds.y), c, z, z, c);
    dl->AddRectFilledMultiColor(ImVec2(ds.x * 0.78f + off, 0), ImVec2(ds.x + off, ds.y), z, c, c, z);
}

// ---- drawing: loot beams + glow outline + the PICK UP card (Apex style) ---------------------------
static void DzGlowBox(ImDrawList* dl, const Vec3& c, float hw, float h, uint32_t rgba, float t)
{   // a screen-space outline glow around the object's box (only this phone draws it)
    float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f; int ok = 0;
    for(int i = 0; i < 8; ++i)
    {
        Vec3 p = { c.x + ((i & 1) ? hw : -hw), c.y + ((i & 2) ? hw : -hw), c.z + ((i & 4) ? h : 0.0f) };
        float sx, sy, dp; if(!Game::WorldToScreen(p, sx, sy, dp) || dp <= 0) continue;
        ++ok; mnx = std::min(mnx, sx); mny = std::min(mny, sy); mxx = std::max(mxx, sx); mxy = std::max(mxy, sy);
    }
    if(ok < 4) return;
    float pad = UI::S(4), pulse = 0.75f + 0.25f * sinf(t * 5.0f);
    for(int i = 4; i >= 1; --i)
    {
        float g = pad + i * UI::S(3);
        dl->AddRect(ImVec2(mnx - g, mny - g), ImVec2(mxx + g, mxy + g), DzCol(rgba, 0.10f * pulse * (5 - i)), UI::S(8), 0, UI::S(4));
    }
    dl->AddRect(ImVec2(mnx - pad, mny - pad), ImVec2(mxx + pad, mxy + pad), DzCol(rgba, 0.95f * pulse), UI::S(6), 0, UI::S(2));
}
static void DzDrawBeam(ImDrawList* dl, const Vec3& base, float dist, uint32_t rgba)
{   // a thin vertical light beam from the item up into the sky
    float x0, y0, d0, x1, y1, d1;
    if(!Game::WorldToScreen(base, x0, y0, d0) || d0 <= 0) return;
    if(!Game::WorldToScreen({ base.x, base.y, base.z + 9.0f }, x1, y1, d1) || d1 <= 0) return;
    float a = dist < 8.0f ? 0.35f + dist / 8.0f * 0.45f : std::max(0.15f, 0.80f - (dist - 8.0f) / 110.0f);
    float w = std::max(UI::S(1.2f), UI::S(3.2f) - dist * 0.03f);
    ImU32 core = DzCol(rgba, a), fade = DzCol(rgba, 0.0f);
    dl->AddRectFilledMultiColor(ImVec2(x0 - w * 2.5f, y1), ImVec2(x0 + w * 2.5f, y0), fade, fade, DzCol(rgba, a * 0.25f), DzCol(rgba, a * 0.25f));
    dl->AddRectFilledMultiColor(ImVec2(x0 - w * 0.5f, y1), ImVec2(x0 + w * 0.5f, y0), DzCol(0xFFFFFFFFu, 0.0f), DzCol(0xFFFFFFFFu, 0.0f), core, core);
    dl->AddCircleFilled(ImVec2(x0, y0), w * 1.6f, DzCol(rgba, a), 12);
}
static void DzDrawLootX(ImDrawList* dl, ImVec2 ds, int ped, float t)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const float cx = ds.x * 0.5f, cy = ds.y * 0.45f;
    // what the camera points at: the loot closest to the screen centre (inside the aim circle)
    int aim = -1; float best = UI::S(90) * UI::S(90);
    for(auto& kv : s_Dz.loot)
    {
        const DzLootC& l = kv.second; if(!l.item || !l.zDone) continue;
        float dx = l.x - me.x, dy = l.y - me.y, d2 = dx * dx + dy * dy; if(d2 > 85.0f * 85.0f) continue;
        auto lo = s_DzX.los.find(l.idx); bool seen = lo != s_DzX.los.end() && lo->second;
        if(!seen) continue;   // no beam / glow through the walls
        float d = sqrtf(d2); uint32_t col = DzRarityCol(l.item);
        DzDrawBeam(dl, { l.x, l.y, l.z }, d, col);
        if(d > 30.0f) continue;
        float sx, sy, dp; if(!Game::WorldToScreen({ l.x, l.y, l.z + 0.2f }, sx, sy, dp) || dp <= 0) continue;
        float q = (sx - cx) * (sx - cx) + (sy - cy) * (sy - cy); if(q < best) { best = q; aim = l.idx; }
    }
    s_DzX.aimLoot = aim;
    if(aim >= 0)
    {
        const DzLootC& l = s_Dz.loot[(uint16_t)aim]; const DzItem& it = DzItemInfo(l.item);
        uint32_t col = DzRarityCol(l.item);
        DzGlowBox(dl, { l.x, l.y, l.z }, 0.28f, 0.42f, col, t);
        float sx, sy, dp;
        if(Game::WorldToScreen({ l.x, l.y, l.z + 0.55f }, sx, sy, dp) && dp > 0)
        {   // the card: icon, name x count, description, distance
            char nm[48], ds2[96], dd[16]; float d = sqrtf((l.x - me.x) * (l.x - me.x) + (l.y - me.y) * (l.y - me.y));
            if(l.count > 1) snprintf(nm, sizeof(nm), "%s x%d", it.name, l.count); else snprintf(nm, sizeof(nm), "%s", it.name);
            DzItemDesc(l.item, ds2, sizeof(ds2)); snprintf(dd, sizeof(dd), "%dm", (int)d);
            float fs = UI::S(17), fs2 = UI::S(13), w = std::max(RpTextW(fs, nm), RpTextW(fs2, ds2)) + UI::S(76), h = UI::S(58);
            ImVec2 a(sx + UI::S(24), sy - h * 0.5f), b(a.x + w, a.y + h);
            if(b.x > ds.x - UI::S(8)) { a.x = sx - UI::S(24) - w; b.x = a.x + w; }
            dl->AddRectFilled(a, b, IM_COL32(12, 14, 18, 215), UI::S(4));
            dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), DzCol(col), UI::S(2));
            DzItemIcon(dl, l.item, ImVec2(a.x + UI::S(10), a.y + UI::S(5)), UI::S(48));
            dl->AddText(UI::Font(), fs, ImVec2(a.x + UI::S(64), a.y + UI::S(6)), DzCol(col), nm);
            dl->AddText(UI::Font(), fs2, ImVec2(a.x + UI::S(64), a.y + UI::S(30)), IM_COL32(210, 210, 210, 255), ds2);
            DzTextR(dl, UI::S(12), b.x - UI::S(6), a.y + UI::S(6), IM_COL32(170, 170, 170, 255), dd);
        }
    }
    // bodies: "Check Body" glow (red)
    s_DzX.aimBody = -1;
    for(size_t i = 0; i < s_DzX.bodies.size(); ++i)
    {
        const DzBodyC& b = s_DzX.bodies[i]; float dx = b.p.x - me.x, dy = b.p.y - me.y; if(dx * dx + dy * dy > 25.0f * 25.0f) continue;
        Vec3 cp, cl; if(!Game::GetCameraPos(cp, cl) || !Game::LineOfSightClear(cp, { b.p.x, b.p.y, b.p.z })) continue;
        DzGlowBox(dl, { b.p.x, b.p.y, b.p.z - 0.9f }, 0.7f, 0.5f, DZ_RED, t);
        float sx, sy, dp; if(!Game::WorldToScreen({ b.p.x, b.p.y, b.p.z }, sx, sy, dp) || dp <= 0) continue;
        char l[64]; snprintf(l, sizeof(l), "Body of %s", b.victim);
        DzText(dl, UI::S(15), ImVec2(sx - RpTextW(UI::S(15), l) * 0.5f, sy - UI::S(26)), DzCol(DZ_RED), l);
        if(dx * dx + dy * dy < 3.4f * 3.4f) s_DzX.aimBody = (int)i;
    }
}
// the action buttons (PICK UP / CHECK BODY / REVIVE / GIVE UP) - bottom centre
static void DzDrawActions(ImVec2 ds, int ped)
{
    if(s_Dz.invOpen || s_Dz.panelOpen || s_DzX.journal || s_DzX.listOpen) return;
    const float bw = UI::S(280), bh = UI::S(58);
    int btn = 0; char lb[80]; uint32_t col = 0xCA8A04FFu;
    int rv = -1, pickIdx = -1;
    if(s_DzX.downed)
    {
        uint64_t now = MP_NowMs();
        snprintf(lb, sizeof(lb), "%s##dzgu", s_DzX.giveUpAt && now - s_DzX.giveUpAt < 3000 ? "TAP AGAIN TO GIVE UP" : "GIVE UP"); col = 0x7F1D1DFFu; btn = 4;
    }
    else if(s_DzX.reviveTarget >= 0) return;
    else if(!Game::IsCharInAnyCar(ped) && (rv = DzReviveCandidate(ped)) >= 0) { snprintf(lb, sizeof(lb), "REVIVE  %s##dzrv", players[rv].name); col = 0x16A34AFFu; btn = 3; }
    else if(!Game::IsCharInAnyCar(ped))
    {
        Vec3 me; Game::GetCharPos(ped, me); float d = 0;
        pickIdx = DzNearestLoot(me, DZ_PICK_RANGE, &d);
        if(s_DzX.aimLoot >= 0 && s_Dz.loot.count((uint16_t)s_DzX.aimLoot))
        {   // the pointed one if it is in reach
            const DzLootC& l = s_Dz.loot[(uint16_t)s_DzX.aimLoot]; float dx = l.x - me.x, dy = l.y - me.y;
            if(dx * dx + dy * dy < DZ_PICK_RANGE * DZ_PICK_RANGE) pickIdx = s_DzX.aimLoot;
        }
        if(pickIdx >= 0) { const DzItem& it = DzItemInfo(s_Dz.loot[(uint16_t)pickIdx].item); snprintf(lb, sizeof(lb), "PICK UP  %s##dzpk", it.name); btn = 1; }
        else if(s_DzX.aimBody >= 0) { snprintf(lb, sizeof(lb), "CHECK BODY##dzcb"); col = 0x991B1BFFu; btn = 2; }
    }
    if(!btn) return;
    BcwBeginOverlay("##DzAct", ImVec2(ds.x * 0.5f - bw * 0.5f - UI::S(18), ds.y * 0.64f), ImVec2(bw + UI::S(36), bh + UI::S(28)), 0.0f);
    if(BcwTeamButton(lb, s_Dz.pickIdx >= 0 && btn == 1 ? 0x57534EFFu : col, ImVec2(bw, bh), false, s_Dz.pickIdx >= 0 && btn == 1))
    {
        if(btn == 1) DzStartPick(pickIdx);
        else if(btn == 2) DzSend(DZCMD_CHECKBODY, 0, 0, s_DzX.bodies[s_DzX.aimBody].id);
        else if(btn == 3) DzStartRevive(ped, rv);
        else if(btn == 4) { uint64_t now = MP_NowMs(); if(s_DzX.giveUpAt && now - s_DzX.giveUpAt < 3000) { s_DzX.downAt = 1; } else s_DzX.giveUpAt = now; }
    }
    BcwEndOverlay();
}
// downed screen
static void DzDrawDowned(ImDrawList* fg, ImDrawList* bg, ImVec2 ds, float t)
{
    if(s_DzX.reviveTarget >= 0)
    {
        float k = std::min(1.0f, (MP_NowMs() - s_DzX.reviveAt) / 3600.0f);
        char l[64]; snprintf(l, sizeof(l), "REVIVING %s", players[s_DzX.reviveTarget].name);
        BcwCenterText(fg, UI::S(22), ds.y * 0.58f, DzCol(DZ_GRN), l);
        float w = UI::S(260); ImVec2 a(ds.x * 0.5f - w * 0.5f, ds.y * 0.58f + UI::S(32));
        fg->AddRectFilled(a, ImVec2(a.x + w, a.y + UI::S(8)), IM_COL32(0, 0, 0, 170), UI::S(3));
        fg->AddRectFilled(a, ImVec2(a.x + w * k, a.y + UI::S(8)), DzCol(DZ_GRN), UI::S(3));
    }
    if(!s_DzX.downed) return;
    uint64_t now = MP_NowMs(); int left = DzBleedOutSec() - (int)((now - s_DzX.downAt) / 1000); if(left < 0) left = 0;
    bg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(70, 0, 0, (int)(90 + 30 * sinf(t * 2.0f))));
    BcwCenterText(fg, UI::S(40), ds.y * 0.30f, DzCol(DZ_RED), "YOU ARE DOWN");
    char l[96]; snprintf(l, sizeof(l), "Bleeding out in %d:%02d", left / 60, left % 60);
    BcwCenterText(fg, UI::S(22), ds.y * 0.30f + UI::S(52), IM_COL32_WHITE, l);
    BcwCenterText(fg, UI::S(16), ds.y * 0.30f + UI::S(84), IM_COL32(220, 220, 220, 230),
                  s_Dz.haveMe && s_Dz.me.partyN > 1 ? "Wait for your party: Morphine, Epinephrine or a Blood bag gets you up (+50% HP)." : "You have no party - nobody can revive you.");
    // the party: who is near
    float y = ds.y * 0.30f + UI::S(112); int ped = Game::LocalPed(); Vec3 me; Game::GetCharPos(ped, me);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!DzIsParty(i) || players[i].ped <= 0) continue;
        Vec3 p; if(!Game::GetCharPos(players[i].ped, p)) continue;
        snprintf(l, sizeof(l), "%s  -  %dm", players[i].name, (int)sqrtf((p.x - me.x) * (p.x - me.x) + (p.y - me.y) * (p.y - me.y)));
        BcwCenterText(fg, UI::S(16), y, UI::FromRGBA(players[i].color | 0xFF), l); y += UI::S(22);
    }
}
// party markers through the walls: diamond + name + metres (only your party, only on your screen)
static void DzDrawPartyMarkers(ImDrawList* dl, ImVec2 ds, int ped, float t)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!DzIsParty(i) || players[i].ped <= 0) continue;
        Vec3 p; if(!Game::GetCharPos(players[i].ped, p)) continue;
        float dx = p.x - me.x, dy = p.y - me.y, dz = p.z - me.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if(d < 3.0f) continue;
        float sx, sy, dp; if(!Game::WorldToScreen({ p.x, p.y, p.z + 1.25f }, sx, sy, dp) || dp <= 0) continue;
        if(sx < 0 || sx > ds.x || sy < 0 || sy > ds.y) continue;
        bool down = s_DzX.downedP[i] != 0;
        ImU32 c = down ? DzCol(DZ_RED, 0.75f + 0.25f * sinf(t * 6)) : UI::FromRGBA(players[i].color | 0xFF);
        float r = UI::S(9);
        ImVec2 q[4] = { ImVec2(sx, sy - r), ImVec2(sx + r * 0.8f, sy), ImVec2(sx, sy + r), ImVec2(sx - r * 0.8f, sy) };
        dl->AddConvexPolyFilled(q, 4, c);
        dl->AddPolyline(q, 4, IM_COL32(0, 0, 0, 200), ImDrawFlags_Closed, UI::S(1.5f));
        if(down) DzStatus(dl, DZST_MEDIC, ImVec2(sx - r, sy - r * 3.4f), r * 2.0f, DzCol(DZ_RED));
        char l[64]; snprintf(l, sizeof(l), "%s", players[i].name);
        char m[32]; snprintf(m, sizeof(m), down ? "DOWN  %dm" : "%dm", (int)d);
        float fs = UI::S(14);
        DzText(dl, fs, ImVec2(sx - RpTextW(fs, l) * 0.5f, sy - r - fs - UI::S(3)), c, l);
        DzText(dl, UI::S(13), ImVec2(sx - RpTextW(UI::S(13), m) * 0.5f, sy + r + UI::S(2)), IM_COL32(235, 235, 235, 235), m);
    }
}

// ---- drawing: the GPS (original map in the monitor bezel) + compass (key 8) -----------------------
static float s_DzMapRange = 260.0f;
static void DzDrawGps(ImDrawList* dl, ImVec2 ds, int ped, ImVec2& outP0, float& outSize)
{
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const float fw = ds.y * 0.36f, fh = fw * 274.0f / 340.0f;
    const ImVec2 p0(ds.x * 0.010f, ds.y * 0.93f - fh - UI::S(24));
    outP0 = p0; outSize = fh;
    const float tabH = UI::S(24);
    ImVec2 ia(p0.x + fw * 0.055f, p0.y + fh * 0.075f), ib(p0.x + fw * 0.945f, p0.y + fh * 0.79f);
    bool on = s_Dz.haveMe && s_Dz.me.gps && !s_DzX.gpsOff;
    if(!on)
    {
        dl->AddRectFilled(ia, ib, IM_COL32(0, 0, 0, 235));
        const char* l1 = s_DzX.gpsOff ? "GPS OFF (7)" : "USE GPS"; const char* l2 = s_DzX.gpsOff ? "" : "TO UNLOCK MAP"; float fs = UI::S(15);
        ImVec2 c((ia.x + ib.x) * 0.5f, (ia.y + ib.y) * 0.5f);
        DzText(dl, fs, ImVec2(c.x - RpTextW(fs, l1) * 0.5f, c.y - fs), IM_COL32(220, 220, 220, 255), l1);
        DzText(dl, fs, ImVec2(c.x - RpTextW(fs, l2) * 0.5f, c.y + UI::S(2)), IM_COL32(220, 220, 220, 255), l2);
    }
    else
    {
        float sp = Game::IsCharInAnyCar(ped) ? DzCarSpeed(Game::GetCarCharIsIn(ped)) : 0.0f;
        float want = 240.0f + std::min(45.0f, sp) * 5.0f; s_DzMapRange += (want - s_DzMapRange) * 0.04f;
        float rx = s_DzMapRange, ry = s_DzMapRange * (ib.y - ia.y) / (ib.x - ia.x);
        float u = (me.x + 3000.0f) / 6000.0f, v = (3000.0f - me.y) / 6000.0f, hu = rx / 6000.0f, hv = ry / 6000.0f;
        ImVec2 uvm; void* tex = HUD::Texture("dz_map.png", &uvm);
        dl->PushClipRect(ia, ib, true);
        dl->AddRectFilled(ia, ib, IM_COL32(40, 60, 90, 255));
        if(tex) dl->AddImage((ImTextureID)tex, ia, ib, ImVec2((u - hu) * uvm.x, (v - hv) * uvm.y), ImVec2((u + hu) * uvm.x, (v + hv) * uvm.y));
        auto toS = [&](float x, float y) { return ImVec2(ia.x + ((x - me.x) / rx * 0.5f + 0.5f) * (ib.x - ia.x), ia.y + ((me.y - y) / ry * 0.5f + 0.5f) * (ib.y - ia.y)); };
        for(int i = 0; i < DZ_SITES; ++i)
        {
            const DzSite& s = g_DzSites[i]; if(s.kind == DZS_HOUSE) continue;
            if(fabsf(s.x - me.x) > rx * 1.1f || fabsf(s.y - me.y) > ry * 1.1f) continue;
            ImVec2 q = toS(s.x, s.y); uint32_t c = s.kind == DZS_GAS ? 0xF97316FFu : s.kind == DZS_MILITARY ? 0x65A30DFFu : 0x78716CFFu;
            dl->AddCircleFilled(q, UI::S(5), DzCol(c), 10); dl->AddCircle(q, UI::S(5), IM_COL32(0, 0, 0, 200), 10, UI::S(1));
        }
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!DzIsParty(i) || players[i].ped <= 0) continue;
            Vec3 pp; if(!Game::GetCharPos(players[i].ped, pp)) continue;
            ImVec2 q = toS(pp.x, pp.y); dl->AddCircleFilled(q, UI::S(5), UI::FromRGBA(players[i].color | 0xFF), 12); dl->AddCircle(q, UI::S(5), IM_COL32_WHITE, 12, UI::S(1.2f));
        }
        // me: the arrow (heading)
        ImVec2 c = toS(me.x, me.y); float h = -Game::GetCharHeading(ped) * 0.0174533f, r = UI::S(9);
        auto rot = [&](float x, float y) { return ImVec2(c.x + x * cosf(h) - y * sinf(h), c.y + x * sinf(h) + y * cosf(h)); };
        dl->AddTriangleFilled(rot(0, -r), rot(r * 0.7f, r * 0.8f), rot(-r * 0.7f, r * 0.8f), IM_COL32_WHITE);
        dl->AddTriangle(rot(0, -r), rot(r * 0.7f, r * 0.8f), rot(-r * 0.7f, r * 0.8f), IM_COL32(0, 0, 0, 220), UI::S(1.5f));
        if(s_Dz.coords)
        {
            char t[64]; snprintf(t, sizeof(t), "X %.0f  Y %.0f  Z %.0f", me.x, me.y, me.z);
            dl->AddRectFilled(ia, ImVec2(ib.x, ia.y + UI::S(22)), IM_COL32(0, 0, 0, 170));
            dl->AddText(UI::Font(), UI::S(14), ImVec2(ia.x + UI::S(6), ia.y + UI::S(3)), DzCol(DZ_YEL), t);
        }
        dl->PopClipRect();
    }
    // the monitor bezel on top
    if(!DzHud(dl, DZR_GPS_FRAME, p0, ImVec2(p0.x + fw, p0.y + fh))) dl->AddRect(ia, ib, IM_COL32(80, 80, 80, 255), 0, 0, UI::S(6));
    // COORDS | MAP tabs under the frame (tap)
    ImVec2 a(p0.x + fw * 0.06f, p0.y + fh + UI::S(2)); float tw = fw * 0.88f;
    ImGui::SetNextWindowPos(a); ImGui::SetNextWindowSize(ImVec2(tw, tabH));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##DzRadarTabs", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    for(int i = 0; i < 2; ++i)
    {
        ImVec2 q = ImGui::GetCursorScreenPos(); float w = tw * 0.5f;
        if(ImGui::InvisibleButton(i ? "##dzmap" : "##dzcoords", ImVec2(w, tabH))) { s_Dz.coords = (i == 0); MpUiClick(); }
        bool sel = (i == 0) == s_Dz.coords;
        wl->AddRectFilled(q, ImVec2(q.x + w, q.y + tabH), sel ? IM_COL32(60, 60, 60, 230) : IM_COL32(20, 20, 20, 210));
        const char* l = i ? "MAP" : "COORDS"; float fs = UI::S(14);
        wl->AddText(UI::Font(), fs, ImVec2(q.x + w * 0.5f - RpTextW(fs, l) * 0.5f, q.y + UI::S(4)), sel ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255), l);
        if(!i) ImGui::SameLine();
    }
    ImGui::End(); ImGui::PopStyleVar(2);
    outP0 = ImVec2(p0.x + fw - fh, p0.y);   // the drive HUD goes right of the bezel
}
static void DzDrawBigCompass(ImDrawList* dl, ImVec2 ds)
{
    if(!s_DzX.bigCompass) return;
    Vec3 cp, cl; if(!Game::GetCameraPos(cp, cl)) return;
    float bearing = CompassBearing(cl.x - cp.x, cl.y - cp.y);
    float half = ds.y * 0.15f; ImVec2 c(ds.x * 0.5f, ds.y - half - UI::S(26));
    DzHudRot(dl, DZR_COMPASS_ROSE, c, half * 0.84f, -bearing);
    DzHud(dl, DZR_COMPASS_RING, ImVec2(c.x - half, c.y - half), ImVec2(c.x + half, c.y + half));
    char t[16]; snprintf(t, sizeof(t), "%03d", (int)(bearing + 0.5f) % 360);
    DzText(dl, UI::S(15), ImVec2(c.x - RpTextW(UI::S(15), t) * 0.5f, c.y - half - UI::S(18)), DzCol(DZ_YEL), t);
}
// binoculars / range finder overlays
static void DzDrawOptics(ImDrawList* dl, ImVec2 ds, int ped)
{
    if(s_DzX.binoc)
    {
        float r = ds.y * 0.42f; ImVec2 c1(ds.x * 0.5f - r * 0.55f, ds.y * 0.5f), c2(ds.x * 0.5f + r * 0.55f, ds.y * 0.5f);
        dl->AddRectFilled(ImVec2(0, 0), ImVec2(ds.x, ds.y * 0.5f - r), IM_COL32(0, 0, 0, 230));
        dl->AddRectFilled(ImVec2(0, ds.y * 0.5f + r), ds, IM_COL32(0, 0, 0, 230));
        dl->AddCircle(c1, r, IM_COL32(0, 0, 0, 230), 64, UI::S(40)); dl->AddCircle(c2, r, IM_COL32(0, 0, 0, 230), 64, UI::S(40));
        dl->AddLine(ImVec2(ds.x * 0.5f - UI::S(30), ds.y * 0.5f), ImVec2(ds.x * 0.5f + UI::S(30), ds.y * 0.5f), IM_COL32(255, 255, 255, 120), UI::S(1));
    }
    if(s_DzX.range)
    {   // the distance to whatever is under the crosshair (players, zombies, loot)
        Vec3 me; Game::GetCharPos(ped, me); float best = UI::S(60) * UI::S(60), dist = -1;
        auto test = [&](const Vec3& p) { float sx, sy, dp; if(!Game::WorldToScreen(p, sx, sy, dp) || dp <= 0) return; float q = (sx - ds.x * 0.5f) * (sx - ds.x * 0.5f) + (sy - ds.y * 0.45f) * (sy - ds.y * 0.45f); if(q < best) { best = q; dist = sqrtf((p.x - me.x) * (p.x - me.x) + (p.y - me.y) * (p.y - me.y) + (p.z - me.z) * (p.z - me.z)); } };
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(i != myId && players[i].active && players[i].ped > 0) { Vec3 p; if(Game::GetCharPos(players[i].ped, p)) test(p); }
        for(auto& z : s_Dz.zeds) { Vec3 p; if(z.ped > 0 && Game::GetCharPos(z.ped, p)) test(p); }
        for(auto& kv : s_Dz.loot) if(kv.second.zDone) test({ kv.second.x, kv.second.y, kv.second.z });
        char t[32]; if(dist >= 0) snprintf(t, sizeof(t), "RANGE %.0f m", dist); else snprintf(t, sizeof(t), "RANGE ---");
        BcwCenterText(dl, UI::S(18), ds.y * 0.45f + UI::S(40), DzCol(DZ_GRN), t);
    }
}
// achievement banner
static void DzDrawAch(ImDrawList* dl, ImVec2 ds, uint64_t now)
{
    if(!s_DzX.achAt || now - s_DzX.achAt > 6000) return;
    float a = now - s_DzX.achAt < 5200 ? 1.0f : 1.0f - (now - s_DzX.achAt - 5200) / 800.0f;
    float w = UI::S(420), h = w * 396.0f / 512.0f * 0.42f; ImVec2 p(ds.x * 0.5f - w * 0.5f, UI::S(70));
    if(!DzImgRect(dl, "dz_achbg.png", 512, 396, { 0, 115, 512, 166 }, p, ImVec2(p.x + w, p.y + h), DzCol(0xFFFFFFFFu, a)))
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(20, 20, 20, (int)(220 * a)), UI::S(6));
    BcwCenterText(dl, UI::S(14), p.y + h * 0.22f, DzCol(DZ_YEL, a), "ACHIEVEMENT UNLOCKED");
    BcwCenterText(dl, UI::S(22), p.y + h * 0.48f, DzCol(0xFFFFFFFFu, a), s_DzX.achName);
}
// ANTI-RELOG (top left, red, under the chat - like the original)
static void DzDrawAntiRelog(ImDrawList* dl, ImVec2 ds, uint64_t now, float t)
{
    if(s_Dz.combatUntil <= now) return;
    int left = (int)((s_Dz.combatUntil - now + 999) / 1000);
    float cx = ds.x * 0.11f, y = ds.y * 0.29f; ImU32 c = DzCol(DZ_RED, 0.80f + 0.20f * sinf(t * 6));
    const char* l1 = "ANTI-RELOG"; const char* l2 = "DISCONNECT = DEATH"; char l3[12]; snprintf(l3, sizeof(l3), "%d", left);
    DzText(dl, UI::S(26), ImVec2(cx - RpTextW(UI::S(26), l1) * 0.5f, y), c, l1);            // 3.60: bigger
    DzText(dl, UI::S(23), ImVec2(cx - RpTextW(UI::S(23), l2) * 0.5f, y + UI::S(30)), c, l2);
    DzText(dl, UI::S(32), ImVec2(cx - RpTextW(UI::S(32), l3) * 0.5f, y + UI::S(57)), c, l3);
}

// ---- the inventory (MTA DayZ: "(n) Inventory" + the bag list) -------------------------------------
static const char* DzBagName(int lvl) { return lvl == 3 ? "Military rucksack" : lvl == 2 ? "Assault pack" : lvl == 1 ? "Small backpack" : "Pockets"; }
static void DzSectionHead(ImDrawList* wl, ImVec2 a, float w, const char* t)
{
    wl->AddText(UI::Font(), UI::S(14), a, IM_COL32(235, 235, 235, 255), t);
    wl->AddLine(ImVec2(a.x, a.y + UI::S(18)), ImVec2(a.x + w, a.y + UI::S(18)), IM_COL32(255, 255, 255, 60), UI::S(1));
}
static bool DzSlotBox(ImDrawList* wl, const char* id, ImVec2 a, ImVec2 sz, int item, int count, bool sel, const char* empty)
{
    ImGui::SetCursorScreenPos(a); bool hit = ImGui::InvisibleButton(id, sz);
    ImVec2 b(a.x + sz.x, a.y + sz.y);
    if(sel) wl->AddRectFilled(a, b, IM_COL32(90, 70, 20, 150), UI::S(2));   // 3.60: the texture is the background
    wl->AddRect(a, b, sel ? DzCol(DZ_YEL) : IM_COL32(255, 255, 255, 45), UI::S(2), 0, UI::S(sel ? 2.0f : 1.0f));
    if(item)
    {
        float s = std::min(sz.x, sz.y) * 0.86f;
        DzItemIcon(wl, item, ImVec2(a.x + sz.x * 0.5f - s * 0.5f, a.y + sz.y * 0.5f - s * 0.5f), s);
        if(count > 1) { char c[12]; snprintf(c, sizeof(c), "%d", count); wl->AddText(UI::Font(), UI::S(12), ImVec2(b.x - RpTextW(UI::S(12), c) - UI::S(3), b.y - UI::S(15)), DzCol(DZ_YEL), c); }
    }
    else if(empty) wl->AddText(UI::Font(), UI::S(11), ImVec2(a.x + UI::S(4), a.y + UI::S(3)), IM_COL32(200, 200, 200, 120), empty);
    if(hit) MpUiClick();
    return hit;
}
static void DzDrawInventoryMta(ImVec2 ds, int ped)
{
    if(!s_Dz.invOpen) return;
    const float W = std::min(ds.x * 0.90f, UI::S(1180)), H = ds.y * 0.80f, x0 = ds.x * 0.5f - W * 0.5f, y0 = ds.y * 0.07f;
    const float LW = W * 0.57f, gapP = UI::S(10), RX = x0 + LW + gapP, RW = W - LW - gapP;
    ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(W, H + UI::S(30)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##DzInv", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    const PktDzInv& inv = s_Dz.inv;
    // ---- left: "(n) Inventory" on the grunge panel
    if(!DzHud(wl, DZR_INV_DARK, ImVec2(x0, y0), ImVec2(x0 + LW, y0 + H), IM_COL32(255, 255, 255, 245))) wl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + LW, y0 + H), IM_COL32(24, 24, 22, 240));
    // 3.60: no extra black layer over the texture (it looked doubled)
    int nItems = 0; for(int i = 0; i < inv.n; ++i) nItems += inv.s[i].count ? 1 : 0;
    char hd[48]; snprintf(hd, sizeof(hd), "(%d) Inventory", nItems);
    RpGtaText(wl, UI::Font(), UI::S(22), ImVec2(x0 + UI::S(14), y0 + UI::S(8)), IM_COL32_WHITE, hd, UI::S(1.5f));
    ImGui::SetCursorScreenPos(ImVec2(x0 + LW - UI::S(34), y0 + UI::S(6)));
    if(ImGui::InvisibleButton("##dzinvx", ImVec2(UI::S(30), UI::S(28)))) s_Dz.invOpen = false;
    wl->AddText(UI::Font(), UI::S(20), ImVec2(x0 + LW - UI::S(26), y0 + UI::S(8)), IM_COL32(230, 230, 230, 255), "X");
    const float pad = UI::S(14), cw = LW - pad * 2; float y = y0 + UI::S(42);
    // weapons strip: primary + secondary (the weapons you carry)
    std::vector<int> weap, gear, food, med, belt;
    for(int i = 0; i < inv.n; ++i)
    {
        switch(DzSection(DzItemInfo(inv.s[i].item).kind))
        {
            case DZSEC_WEAPON: weap.push_back(i); break;
            case DZSEC_FOOD: food.push_back(i); break;
            case DZSEC_MED: med.push_back(i); break;
            case DZSEC_TOOLBELT: belt.push_back(i); break;
            default: gear.push_back(i); break;
        }
    }
    {
        float ww = cw * 0.62f, sw = cw - ww - UI::S(8), wh = UI::S(70);
        int p = weap.size() > 0 ? weap[0] : -1, s = weap.size() > 1 ? weap[1] : -1;
        if(DzSlotBox(wl, "##dzwp", ImVec2(x0 + pad, y), ImVec2(ww, wh), p >= 0 ? inv.s[p].item : 0, 1, p >= 0 && s_Dz.invSel == p, "PRIMARY") && p >= 0) { s_Dz.invSel = p; s_DzX.groundSel = -1; }
        if(DzSlotBox(wl, "##dzws", ImVec2(x0 + pad + ww + UI::S(8), y), ImVec2(sw, wh), s >= 0 ? inv.s[s].item : 0, 1, s >= 0 && s_Dz.invSel == s, "SECONDARY") && s >= 0) { s_Dz.invSel = s; s_DzX.groundSel = -1; }
        y += wh + UI::S(8);
        // more weapons go into the gear grid
        for(size_t k = 2; k < weap.size(); ++k) gear.insert(gear.begin(), weap[k]);
    }
    // description box
    {
        float dh = UI::S(70); ImVec2 a(x0 + pad, y), b(x0 + pad + cw, y + dh);
        wl->AddRect(a, b, IM_COL32(255, 255, 255, 40), UI::S(2));
        int item = 0, cnt = 0;
        if(s_Dz.invSel >= 0 && s_Dz.invSel < inv.n) { item = inv.s[s_Dz.invSel].item; cnt = inv.s[s_Dz.invSel].count; }
        else if(s_DzX.groundSel >= 0 && s_Dz.loot.count((uint16_t)s_DzX.groundSel)) { item = s_Dz.loot[(uint16_t)s_DzX.groundSel].item; cnt = s_Dz.loot[(uint16_t)s_DzX.groundSel].count; }
        if(item)
        {
            DzItemIcon(wl, item, ImVec2(a.x + UI::S(6), a.y + UI::S(5)), dh - UI::S(10));
            char nm[64], de[96]; snprintf(nm, sizeof(nm), cnt > 1 ? "%s  x%d" : "%s", DzItemInfo(item).name, cnt); DzItemDesc(item, de, sizeof(de));
            wl->AddText(UI::Font(), UI::S(17), ImVec2(a.x + dh + UI::S(4), a.y + UI::S(8)), DzCol(DZ_YEL), nm);
            wl->AddText(UI::Font(), UI::S(13), ImVec2(a.x + dh + UI::S(4), a.y + UI::S(32)), IM_COL32(220, 220, 220, 255), de);
            char kg[24]; snprintf(kg, sizeof(kg), "%.2f KG", DzItemInfo(item).kg * std::max(1, cnt)); DzTextR(wl, UI::S(12), b.x - UI::S(6), b.y - UI::S(18), IM_COL32(180, 180, 180, 255), kg);
        }
        else wl->AddText(UI::Font(), UI::S(14), ImVec2(a.x + UI::S(10), a.y + UI::S(26)), IM_COL32(180, 180, 180, 200), "Tap an item to see what it does.");
        y += dh + UI::S(10);
    }
    // sections: GEAR, FOOD / DRINKS, MEDICINES, TOOLBELT
    const float tile = UI::S(54), tg = UI::S(5); int cols = std::max(1, (int)((cw + tg) / (tile + tg)));
    auto grid = [&](const char* title, const std::vector<int>& v, int minRows, const char* tag)
    {
        DzSectionHead(wl, ImVec2(x0 + pad, y), cw, title); y += UI::S(22);
        int rows = std::max(minRows, ((int)v.size() + cols - 1) / cols);
        for(int r = 0; r < rows; ++r) for(int c = 0; c < cols; ++c)
        {
            size_t k = (size_t)(r * cols + c); ImVec2 a(x0 + pad + c * (tile + tg), y + r * (tile + tg));
            if(a.y + tile > y0 + H - UI::S(64)) continue;
            char id[24]; snprintf(id, sizeof(id), "##dz%s%d", tag, (int)k);
            int i = k < v.size() ? v[k] : -1;
            if(DzSlotBox(wl, id, a, ImVec2(tile, tile), i >= 0 ? inv.s[i].item : 0, i >= 0 ? inv.s[i].count : 0, i >= 0 && s_Dz.invSel == i, nullptr) && i >= 0) { s_Dz.invSel = s_Dz.invSel == i ? -1 : i; s_DzX.groundSel = -1; }
        }
        y += rows * (tile + tg) + UI::S(4);
    };
    grid("GEAR", gear, 1, "g");
    grid("FOOD / DRINKS", food, 1, "f");
    grid("MEDICINES", med, 1, "m");
    grid("TOOLBELT", belt, 1, "t");
    // actions
    {
        float ay = y0 + H - UI::S(56);
        ImGui::SetCursorScreenPos(ImVec2(x0 + pad, ay));
        ImGui::SetWindowFontScale(0.85f);
        ImVec2 bs(UI::S(118), UI::S(44));
        if(s_Dz.invSel >= 0 && s_Dz.invSel < inv.n)
        {
            const int item = inv.s[s_Dz.invSel].item; const DzItem& it = DzItemInfo(item);
            bool equip = it.kind == DZK_ARMOR || it.kind == DZK_BAG || it.kind == DZK_GPS || it.kind == DZK_WEAPON;
            if(BcwTeamButton(equip ? "EQUIP##dzeq" : "USE##dzuse", 0xCA8A04FFu, bs, false, false))
                DzSend(it.kind == DZK_FUEL ? DZCMD_FUEL : (equip && it.kind != DZK_WEAPON ? DZCMD_EQUIP : DZCMD_USE), s_Dz.invSel);
            ImGui::SameLine(0, UI::S(6));
            if(BcwTeamButton("DROP##dzdrop", 0x57534EFFu, bs, false, false)) { DzSend(DZCMD_DROP, s_Dz.invSel); s_Dz.invSel = -1; }
            // TREAT: a survivor next to you (bandage, painkillers, medkit, antibiotics, morphine, blood bag, heat pack)
            bool med = it.kind == DZK_MED || it.kind == DZK_CURE || item == DZI_MORPHINE || item == DZI_BLOODBAG || item == DZI_HEATPACK;
            if(med)
            {
                int tgt = -1; Vec3 me; Game::GetCharPos(ped, me); float bd = 3.5f * 3.5f;
                for(int i = 0; i < MP_MAX_PLAYERS; ++i) { if(i == myId || !players[i].active || players[i].ped <= 0) continue; Vec3 p; if(!Game::GetCharPos(players[i].ped, p)) continue; float d = (p.x - me.x) * (p.x - me.x) + (p.y - me.y) * (p.y - me.y); if(d < bd) { bd = d; tgt = i; } }
                if(tgt >= 0) { ImGui::SameLine(0, UI::S(6)); char l[64]; snprintf(l, sizeof(l), "TREAT %s##dztr", players[tgt].name); if(BcwTeamButton(l, 0x16A34AFFu, ImVec2(UI::S(170), bs.y), false, false)) DzSend(DZCMD_TREAT, tgt, s_Dz.invSel); }
            }
            if(it.kind == DZK_SPECIAL && it.val == DZSP_EMPTYCAN)
            {
                int car = DzCarNear(ped, 6.0f);
                if(car > 0) { ImGui::SameLine(0, UI::S(6)); if(BcwTeamButton("SIPHON##dzsi", 0xEA580CFFu, bs, false, false)) { float& f = s_Dz.fuel[car]; if(f < 20.0f) DzNotice(DZ_RED, "This vehicle has not enough fuel to siphon."); else { f -= 20.0f; DzSend(DZCMD_USE, s_Dz.invSel, 1); } } }
            }
        }
        else if(s_DzX.groundSel >= 0 && s_Dz.loot.count((uint16_t)s_DzX.groundSel))
        {
            if(BcwTeamButton("PICK UP##dzpk2", 0xCA8A04FFu, ImVec2(UI::S(200), bs.y), false, s_Dz.pickIdx >= 0)) { DzStartPick(s_DzX.groundSel); s_DzX.groundSel = -1; }
        }
        ImGui::SetWindowFontScale(1.0f);
    }
    // ---- right: the bag ("Small backpack (kg/max)") + the ground
    {
        // 3.60: the same inventory texture as the left side (no flat box + border on top of it)
        if(!DzHud(wl, DZR_INV_DARK, ImVec2(RX, y0), ImVec2(RX + RW, y0 + H), IM_COL32(255, 255, 255, 245))) wl->AddRectFilled(ImVec2(RX, y0), ImVec2(RX + RW, y0 + H), IM_COL32(24, 24, 22, 240), UI::S(2));
        // the backpack silhouette, faint, behind the list
        { float bh = H * 0.62f, bw = bh * 0.5f; DzImgFull(wl, "dz_bag.png", ImVec2(RX + RW * 0.5f - bw * 0.5f, y0 + H * 0.24f), ImVec2(RX + RW * 0.5f + bw * 0.5f, y0 + H * 0.24f + bh), IM_COL32(255, 255, 255, 34)); }
        float kgCur = s_Dz.haveMe ? s_Dz.me.kg100 / 100.0f : 0, kgMax = s_Dz.haveMe ? s_Dz.me.maxKg100 / 100.0f : 10;
        char t[64]; snprintf(t, sizeof(t), "%s (%.0f/%.0f)", DzBagName(s_Dz.haveMe ? s_Dz.me.bag : 0), kgCur, kgMax);
        RpGtaText(wl, UI::Font(), UI::S(20), ImVec2(RX + UI::S(12), y0 + UI::S(9)), IM_COL32_WHITE, t, UI::S(1.5f));
        // tabs
        std::vector<int> near;
        Vec3 me; if(Game::GetCharPos(ped, me)) for(auto& kv : s_Dz.loot) { const DzLootC& l = kv.second; float ddx = l.x - me.x, ddy = l.y - me.y; if(l.item && ddx * ddx + ddy * ddy < 4.0f * 4.0f) near.push_back(l.idx); }
        float ty = y0 + UI::S(40), tw = (RW - UI::S(24)) * 0.5f;
        for(int i = 0; i < 2; ++i)
        {
            ImVec2 a(RX + UI::S(12) + i * tw, ty); ImGui::SetCursorScreenPos(a);
            char id[16]; snprintf(id, sizeof(id), "##dzbt%d", i);
            if(ImGui::InvisibleButton(id, ImVec2(tw, UI::S(28)))) { s_DzX.bagTab = i; MpUiClick(); }
            bool sel = s_DzX.bagTab == i;
            wl->AddRectFilled(a, ImVec2(a.x + tw, a.y + UI::S(28)), sel ? IM_COL32(70, 70, 70, 220) : IM_COL32(30, 30, 30, 200));
            char l[32]; if(i) snprintf(l, sizeof(l), "GROUND (%d)", (int)near.size()); else snprintf(l, sizeof(l), "BAG (%d)", inv.n);
            wl->AddText(UI::Font(), UI::S(14), ImVec2(a.x + tw * 0.5f - RpTextW(UI::S(14), l) * 0.5f, a.y + UI::S(6)), sel ? IM_COL32_WHITE : IM_COL32(170, 170, 170, 255), l);
        }
        float ly = ty + UI::S(36), rh = UI::S(34), bottom = y0 + H - UI::S(40);
        auto row = [&](const char* id, int item, int count, bool sel) -> bool
        {
            if(ly + rh > bottom) return false;
            ImVec2 a(RX + UI::S(10), ly), b(RX + RW - UI::S(10), ly + rh);
            ImGui::SetCursorScreenPos(a); bool hit = ImGui::InvisibleButton(id, ImVec2(b.x - a.x, rh));
            if(sel) wl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 28));
            wl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), IM_COL32(255, 255, 255, 22));
            wl->AddText(UI::Font(), UI::S(15), ImVec2(a.x + UI::S(6), a.y + UI::S(8)), sel ? DzCol(DZ_YEL) : IM_COL32(235, 235, 235, 255), DzItemInfo(item).name);
            char c[12]; snprintf(c, sizeof(c), "%d", std::max(1, count));
            float ix = b.x - UI::S(64);
            DzItemIcon(wl, item, ImVec2(ix, a.y + UI::S(1)), rh - UI::S(2));
            wl->AddText(UI::Font(), UI::S(14), ImVec2(ix - RpTextW(UI::S(14), c) - UI::S(8), a.y + UI::S(9)), IM_COL32(200, 200, 200, 255), c);
            if(sel) wl->AddText(UI::Font(), UI::S(15), ImVec2(b.x - UI::S(26), a.y + UI::S(8)), DzCol(DZ_YEL), "<<");
            ly += rh; if(hit) MpUiClick();
            return hit;
        };
        if(s_DzX.bagTab == 0)
        {
            for(int i = 0; i < inv.n; ++i) { char id[16]; snprintf(id, sizeof(id), "##dzbr%d", i); if(row(id, inv.s[i].item, inv.s[i].count, s_Dz.invSel == i)) { s_Dz.invSel = s_Dz.invSel == i ? -1 : i; s_DzX.groundSel = -1; } }
            if(!inv.n) wl->AddText(UI::Font(), UI::S(14), ImVec2(RX + UI::S(16), ly + UI::S(8)), IM_COL32(150, 150, 150, 255), "Empty. Loot houses, petrol stations and bases.");
        }
        else
        {
            for(size_t i = 0; i < near.size(); ++i) { const DzLootC& l = s_Dz.loot[(uint16_t)near[i]]; char id[16]; snprintf(id, sizeof(id), "##dzgr%d", (int)i); if(row(id, l.item, l.count, s_DzX.groundSel == l.idx)) { s_DzX.groundSel = l.idx; s_Dz.invSel = -1; } }
            for(size_t i = 0; i < s_DzX.bodies.size(); ++i)
            {
                const DzBodyC& b = s_DzX.bodies[i]; float ddx = b.p.x - me.x, ddy = b.p.y - me.y; if(ddx * ddx + ddy * ddy > 3.4f * 3.4f || ly + rh > bottom) continue;
                ImVec2 a(RX + UI::S(10), ly); ImGui::SetCursorScreenPos(a); char id[16]; snprintf(id, sizeof(id), "##dzbd%d", (int)i);
                if(ImGui::InvisibleButton(id, ImVec2(RW - UI::S(20), rh))) { DzSend(DZCMD_CHECKBODY, 0, 0, b.id); MpUiClick(); }
                char l[64]; snprintf(l, sizeof(l), "Body of %s  -  CHECK BODY", b.victim);
                wl->AddText(UI::Font(), UI::S(15), ImVec2(a.x + UI::S(6), a.y + UI::S(8)), DzCol(DZ_RED), l); ly += rh;
            }
            if(near.empty()) wl->AddText(UI::Font(), UI::S(14), ImVec2(RX + UI::S(16), ly + UI::S(8)), IM_COL32(150, 150, 150, 255), "Nothing on the ground here.");
        }
        // carry bar
        float k = kgMax > 0 ? kgCur / kgMax : 0; if(k > 1) k = 1;
        ImVec2 ba(RX + UI::S(12), y0 + H - UI::S(28)); float bw = RW - UI::S(24);
        wl->AddRectFilled(ba, ImVec2(ba.x + bw, ba.y + UI::S(8)), IM_COL32(60, 60, 60, 255));
        wl->AddRectFilled(ba, ImVec2(ba.x + bw * k, ba.y + UI::S(8)), k > 0.9f ? DzCol(DZ_RED) : DzCol(DZ_GRN));
        snprintf(t, sizeof(t), "%.2f / %.2f KG", kgCur, kgMax);
        wl->AddText(UI::Font(), UI::S(12), ImVec2(ba.x, ba.y + UI::S(10)), IM_COL32(200, 200, 200, 255), t);
    }
    wl->AddText(UI::Font(), UI::S(13), ImVec2(x0 + UI::S(4), y0 + H + UI::S(6)), IM_COL32(230, 230, 230, 220), "* tap an item to open its options (right mouse button on PC)");
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}

// ---- TAB player list (like the #1 TOP-GTA DayZ list, in English) ---------------------------------
static void DzDrawPlayerList(ImVec2 ds)
{
    if(!s_DzX.listOpen) return;
    const float W = std::min(ds.x * 0.86f, UI::S(1100)), H = ds.y * 0.78f, x0 = ds.x * 0.5f - W * 0.5f, y0 = ds.y * 0.10f;
    ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 0.90f));
    ImGui::Begin("##DzList", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    // header: "█ #1 Project Day Z Legacy █"   + online / max
    const float hh = UI::S(40);
    wl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + hh), IM_COL32(16, 16, 18, 255));
    char title[96]; snprintf(title, sizeof(title), "#1 %s", MP_DZ_SERVER_NAME);
    float tw = RpTextW(UI::S(20), title), tx = x0 + W * 0.5f - tw * 0.5f;
    wl->AddRectFilled(ImVec2(tx - UI::S(26), y0 + UI::S(12)), ImVec2(tx - UI::S(12), y0 + UI::S(28)), DzCol(DZ_RED));
    wl->AddRectFilled(ImVec2(tx + tw + UI::S(12), y0 + UI::S(12)), ImVec2(tx + tw + UI::S(26), y0 + UI::S(28)), DzCol(DZ_RED));
    wl->AddText(UI::Font(), UI::S(20), ImVec2(tx, y0 + UI::S(9)), IM_COL32_WHITE, title);
    int online = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(players[i].active) ++online; ++online;
    char cnt[24]; snprintf(cnt, sizeof(cnt), "%d/%d", s_DzX.listTotal ? (int)s_DzX.listTotal : online, s_DzX.listMax ? (int)s_DzX.listMax : MP_DZ_MAX_PLAYERS);
    DzTextR(wl, UI::S(17), x0 + W - UI::S(14), y0 + UI::S(10), IM_COL32(230, 230, 230, 255), cnt);
    ImGui::SetCursorScreenPos(ImVec2(x0 + UI::S(6), y0 + UI::S(6)));
    if(ImGui::InvisibleButton("##dzlx", ImVec2(UI::S(30), UI::S(28)))) s_DzX.listOpen = false;
    wl->AddText(UI::Font(), UI::S(18), ImVec2(x0 + UI::S(14), y0 + UI::S(9)), IM_COL32(200, 200, 200, 255), "X");
    // columns
    static const char* const cols[9] = { "#", "Party", "Nick", "Level", "Kills", "Zombies killed", "Alive time", "FPS", "Ping" };
    static const float cx[9] = { 0.02f, 0.07f, 0.14f, 0.45f, 0.53f, 0.61f, 0.75f, 0.87f, 0.94f };
    float y = y0 + hh + UI::S(6); const float fs = UI::S(15), rh = UI::S(26);
    for(int c = 0; c < 9; ++c) wl->AddText(UI::Font(), fs, ImVec2(x0 + W * cx[c], y), IM_COL32(150, 150, 155, 255), cols[c]);
    y += rh;
    wl->AddLine(ImVec2(x0 + UI::S(8), y - UI::S(3)), ImVec2(x0 + W - UI::S(8), y - UI::S(3)), IM_COL32(255, 255, 255, 40));
    // rows (from the server list; me first if it has not come yet)
    std::vector<DzListRow> rows = s_DzX.list;
    if(rows.empty() && s_Dz.haveMe) { DzListRow r; memset(&r, 0, sizeof(r)); r.id = (uint8_t)myId; r.level = (uint8_t)DzLevel(s_Dz.me.points); r.murders = s_Dz.me.murders; r.zkills = s_Dz.me.zkills; r.aliveMin = s_Dz.me.aliveMin; r.fps = (uint8_t)std::min(255.0f, ImGui::GetIO().Framerate); r.flags = 1; rows.push_back(r); }
    ImGui::SetCursorScreenPos(ImVec2(x0, y));
    ImGui::BeginChild("##dzlrows", ImVec2(W, y0 + H - y - UI::S(30)), false);
    ImDrawList* cl = ImGui::GetWindowDrawList();
    for(size_t k = 0; k < rows.size(); ++k)
    {
        const DzListRow& r = rows[k];
        ImVec2 a = ImGui::GetCursorScreenPos();
        char id[16]; snprintf(id, sizeof(id), "##dzlr%d", (int)k);
        if(ImGui::InvisibleButton(id, ImVec2(W, rh))) { s_DzX.listSel = r.id; MpUiClick(); }
        bool sel = s_DzX.listSel == r.id;
        if(sel) cl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(37, 99, 235, 200));
        else if(k & 1) cl->AddRectFilled(a, ImVec2(a.x + W, a.y + rh), IM_COL32(255, 255, 255, 8));
        bool mine = r.id == myId;
        const char* nm = mine ? settings.name : (r.id < MP_MAX_PLAYERS && players[r.id].active ? players[r.id].name : "?");
        uint32_t ncol = mine ? 0xFFFFFFFFu : (r.id < MP_MAX_PLAYERS ? (players[r.id].color | 0xFF) : 0xFFFFFFFFu);
        if(r.flags & 4) ncol = 0xEF4444FFu;   // bandit
        int ping = mine ? MP::DisplayPing() : (r.id < MP_MAX_PLAYERS ? players[r.id].ping : -1);
        int fps = mine ? (int)ImGui::GetIO().Framerate : r.fps;
        char v[9][48];
        snprintf(v[0], 48, "%d", r.id); v[1][0] = 0; snprintf(v[2], 48, "%s%s", nm, (r.flags & 2) ? "  [DOWN]" : (!(r.flags & 1) ? "  [lobby]" : ""));
        snprintf(v[3], 48, "%d", r.level); snprintf(v[4], 48, "%d", r.murders); snprintf(v[5], 48, "%d", r.zkills);
        snprintf(v[6], 48, "%02u:%02u:00", r.aliveMin / 60, r.aliveMin % 60); snprintf(v[7], 48, "%d", fps); snprintf(v[8], 48, "%d", ping < 0 ? 0 : ping);
        for(int c = 0; c < 9; ++c)
        {
            ImVec2 p(x0 + W * cx[c], a.y + UI::S(4));
            if(c == 1)
            {   // party colour chip instead of a country flag
                ImU32 pc = (r.flags & 8) || mine ? UI::FromRGBA((s_Dz.me.partyCol ? s_Dz.me.partyCol : 0x9CA3AFFFu) | 0xFF) : IM_COL32(80, 80, 80, 255);
                cl->AddRectFilled(ImVec2(p.x, p.y + UI::S(2)), ImVec2(p.x + UI::S(22), p.y + UI::S(15)), pc, UI::S(2));
                continue;
            }
            ImU32 col = c == 2 ? UI::FromRGBA(ncol) : IM_COL32(225, 225, 225, 255);
            if(c == 8) col = ping > 250 ? DzCol(DZ_RED) : ping > 120 ? DzCol(DZ_YEL) : DzCol(DZ_GRN);
            cl->AddText(UI::Font(), fs, p, col, v[c]);
        }
    }
    ImGui::EndChild();
    wl->AddText(UI::Font(), UI::S(13), ImVec2(x0 + UI::S(12), y0 + H - UI::S(22)), IM_COL32(170, 170, 170, 255), "TAB - close the list.  Tap a row to select a player.  Red names = bandits.");
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}

// ---- journal (6 / F1): debug monitor, achievements, skills ----------------------------------------
static const DzRect* DzSkillIcon(int k)
{
    static const DzRect* const r[DZSK_COUNT] = { &DZR_SKILL_SOLD_CRIT, &DZR_SKILL_SOLD_TOUGH, &DZR_SKILL_SOLD_DODGE, &DZR_SKILL_MED_PROF, &DZR_SKILL_MED_SCNDCHANCE, &DZR_SKILL_MED_WBOR,
                                                 &DZR_SKILL_SCAV_GOOD, &DZR_SKILL_SCAV_SHADOW, &DZR_SKILL_SCAV_CAMOUFLAGE, &DZR_SKILL_ENG_AMMO, &DZR_SKILL_ENG_ANALYSIS, &DZR_SKILL_ENG_DUCT };
    return k >= 0 && k < DZSK_COUNT ? r[k] : nullptr;
}
static void DzDrawJournal(ImVec2 ds)
{
    if(!s_DzX.journal) return;
    const float W = std::min(ds.x * 0.80f, UI::S(1000)), H = W * 700.0f / 1024.0f > ds.y * 0.86f ? ds.y * 0.86f : W * 700.0f / 1024.0f;
    const float x0 = ds.x * 0.5f - W * 0.5f, y0 = ds.y * 0.5f - H * 0.5f;
    ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::Begin("##DzJournal", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar);
    ImDrawList* wl = ImGui::GetWindowDrawList();
    if(!DzImgFull(wl, "dz_journal.png", ImVec2(x0, y0), ImVec2(x0 + W, y0 + H))) wl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + H), IM_COL32(60, 48, 30, 245), UI::S(8));
    const ImU32 ink = IM_COL32(40, 30, 20, 255), ink2 = IM_COL32(90, 70, 50, 255);
    // tabs on the left page
    static const char* const tabs[3] = { "DEBUG MONITOR", "ACHIEVEMENTS", "SKILLS" };
    float lx = x0 + W * 0.09f, ly = y0 + H * 0.12f;
    RpGtaText(wl, UI::Font(), UI::S(26), ImVec2(lx, ly), ink, "JOURNAL", UI::S(0.1f)); ly += UI::S(44);
    for(int i = 0; i < 3; ++i)
    {
        ImGui::SetCursorScreenPos(ImVec2(lx, ly)); char id[16]; snprintf(id, sizeof(id), "##dzjt%d", i);
        if(ImGui::InvisibleButton(id, ImVec2(W * 0.34f, UI::S(34)))) { s_DzX.jTab = i; MpUiClick(); }
        wl->AddText(UI::Font(), UI::S(20), ImVec2(lx, ly + UI::S(4)), s_DzX.jTab == i ? IM_COL32(150, 20, 20, 255) : ink, tabs[i]);
        if(s_DzX.jTab == i) wl->AddLine(ImVec2(lx, ly + UI::S(30)), ImVec2(lx + RpTextW(UI::S(20), tabs[i]), ly + UI::S(30)), IM_COL32(150, 20, 20, 255), UI::S(2));
        ly += UI::S(40);
    }
    const PktDzMe& m = s_Dz.me;
    if(s_Dz.haveMe)
    {   // the journal stamps (left page): zombies, headshots, murders, bandits
        float sy = ly + UI::S(14), sw = W * 0.34f, sh = sw * 92.0f / 370.0f;
        const DzRect* st[4] = { &DZR_JOURNAL_ZK, &DZR_JOURNAL_HS, &DZR_JOURNAL_MU, &DZR_JOURNAL_BK }; int vals[4] = { m.zkills, m.headshots, m.murders, m.bkills };
        for(int i = 0; i < 4 && sy + sh < y0 + H * 0.92f; ++i)
        {
            DzHud(wl, *st[i], ImVec2(lx, sy), ImVec2(lx + sw, sy + sh), IM_COL32(255, 255, 255, 230));
            char v[16]; snprintf(v, sizeof(v), "%d", vals[i]); DzTextR(wl, UI::S(20), lx + sw - UI::S(8), sy + sh * 0.5f - UI::S(10), IM_COL32_WHITE, v);
            sy += sh + UI::S(6);
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(x0 + W - UI::S(60), y0 + UI::S(14)));
    if(ImGui::InvisibleButton("##dzjx", ImVec2(UI::S(40), UI::S(36)))) s_DzX.journal = false;
    wl->AddText(UI::Font(), UI::S(24), ImVec2(x0 + W - UI::S(48), y0 + UI::S(18)), ink, "X");
    // right page
    float rx = x0 + W * 0.53f, ry = y0 + H * 0.10f, rw = W * 0.40f;
    ImGui::SetCursorScreenPos(ImVec2(rx, ry));
    ImGui::BeginChild("##dzjpage", ImVec2(rw, H * 0.80f), false);
    ImDrawList* pl = ImGui::GetWindowDrawList();
    auto line = [&](const char* l, const char* v, ImU32 c)
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        pl->AddText(UI::Font(), UI::S(16), p, ink, l);
        pl->AddText(UI::Font(), UI::S(16), ImVec2(p.x + rw - RpTextW(UI::S(16), v) - UI::S(8), p.y), c, v);
        ImGui::Dummy(ImVec2(rw, UI::S(22)));
    };
    char v[64];
    if(s_DzX.jTab == 0)
    {
        static const char* const diff[3] = { "Normal", "Veteran", "Hardcore" };
        snprintf(v, sizeof(v), "%d", m.blood); line("Blood", v, m.blood < 4000 ? IM_COL32(170, 20, 20, 255) : ink);
        snprintf(v, sizeof(v), "%d%%", m.hunger); line("Food", v, ink);
        snprintf(v, sizeof(v), "%d%%", m.thirst); line("Water", v, ink);
        snprintf(v, sizeof(v), "%.2f C", s_DzX.temp); line("Temperature", v, s_DzX.temp < 34.5f ? IM_COL32(30, 60, 160, 255) : ink);
        snprintf(v, sizeof(v), "%d", m.humanity); line("Humanity", v, m.humanity < 0 ? IM_COL32(170, 20, 20, 255) : IM_COL32(20, 110, 40, 255));
        snprintf(v, sizeof(v), "%d (%d pts)", DzLevel(m.points), m.points); line("Level", v, ink);
        snprintf(v, sizeof(v), "%d", m.zkills); line("Zombies killed", v, ink);
        snprintf(v, sizeof(v), "%d", m.headshots); line("Headshots", v, ink);
        snprintf(v, sizeof(v), "%d", m.murders); line("Murders", v, ink);
        snprintf(v, sizeof(v), "%d", m.bkills); line("Bandits killed", v, ink);
        snprintf(v, sizeof(v), "%d", m.confirmed); line("Confirmed kills", v, ink);
        snprintf(v, sizeof(v), "%d", m.revives); line("Revives", v, ink);
        snprintf(v, sizeof(v), "%02u:%02u", m.aliveMin / 60, m.aliveMin % 60); line("Alive (h:m)", v, ink);
        snprintf(v, sizeof(v), "%s%s%s%s%s", s_DzX.cond & DZCO_BLEEDING ? "Bleeding " : "", s_DzX.cond & DZCO_PAIN ? "Pain " : "", s_DzX.cond & DZCO_LEGS ? "Legs " : "", s_DzX.cond & DZCO_ARMS ? "Arm " : "", m.infected ? "Infected" : "");
        line("Condition", v[0] ? v : "Healthy", v[0] ? IM_COL32(170, 20, 20, 255) : IM_COL32(20, 110, 40, 255));
        snprintf(v, sizeof(v), "%s", diff[m.difficulty < 3 ? m.difficulty : 0]); line("Difficulty", v, ink);
        snprintf(v, sizeof(v), "%d | %d ms", (int)ImGui::GetIO().Framerate, MP::DisplayPing()); line("FPS | Ping", v, ink);
    }
    else if(s_DzX.jTab == 1)
    {
        int got = 0; for(int i = 0; i < DZACH_COUNT; ++i) if(m.ach & (1u << i)) ++got;
        snprintf(v, sizeof(v), "%d / %d unlocked", got, DZACH_COUNT); pl->AddText(UI::Font(), UI::S(16), ImGui::GetCursorScreenPos(), ink2, v); ImGui::Dummy(ImVec2(rw, UI::S(24)));
        for(int i = 0; i < DZACH_COUNT; ++i)
        {
            bool on = (m.ach & (1u << i)) != 0; ImVec2 p = ImGui::GetCursorScreenPos();
            DzHud(pl, DZR_TROPHY, p, ImVec2(p.x + UI::S(30), p.y + UI::S(30)), on ? IM_COL32_WHITE : IM_COL32(0, 0, 0, 90));
            pl->AddText(UI::Font(), UI::S(15), ImVec2(p.x + UI::S(36), p.y), on ? IM_COL32(20, 110, 40, 255) : ink, g_DzAch[i].name);
            pl->AddText(UI::Font(), UI::S(12), ImVec2(p.x + UI::S(36), p.y + UI::S(17)), ink2, g_DzAch[i].desc);
            ImGui::Dummy(ImVec2(rw, UI::S(36)));
        }
    }
    else
    {
        snprintf(v, sizeof(v), "Skill points: %d   (1 per level)", m.skillPts); pl->AddText(UI::Font(), UI::S(16), ImGui::GetCursorScreenPos(), m.skillPts ? IM_COL32(20, 110, 40, 255) : ink2, v); ImGui::Dummy(ImVec2(rw, UI::S(26)));
        const char* tree = "";
        for(int k = 0; k < DZSK_COUNT; ++k)
        {
            const DzSkill& s = g_DzSkills[k];
            if(strcmp(tree, s.tree)) { tree = s.tree; pl->AddText(UI::Font(), UI::S(17), ImGui::GetCursorScreenPos(), IM_COL32(120, 20, 20, 255), tree); ImGui::Dummy(ImVec2(rw, UI::S(22))); }
            ImVec2 p = ImGui::GetCursorScreenPos(); float is = UI::S(40);
            if(const DzRect* r = DzSkillIcon(k)) DzHud(pl, *r, p, ImVec2(p.x + is, p.y + is));
            char nm[64]; snprintf(nm, sizeof(nm), "%s  %d/%d", s.name, m.skills[k], s.maxRank);
            pl->AddText(UI::Font(), UI::S(15), ImVec2(p.x + is + UI::S(6), p.y), ink, nm);
            pl->AddText(UI::Font(), UI::S(12), ImVec2(p.x + is + UI::S(6), p.y + UI::S(18)), ink2, s.desc);
            if(m.skillPts && m.skills[k] < s.maxRank)
            {
                ImGui::SetCursorScreenPos(ImVec2(p.x + rw - UI::S(48), p.y + UI::S(4))); char id[16]; snprintf(id, sizeof(id), "+##dzsk%d", k);
                if(BcwTeamButton(id, 0x16A34AFFu, ImVec2(UI::S(38), UI::S(32)), false, false)) DzSend(DZCMD_SKILL, k);
                ImGui::SetCursorScreenPos(p);
            }
            ImGui::Dummy(ImVec2(rw, is + UI::S(6)));
        }
    }
    ImGui::EndChild();
    ImGui::End(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}

// ---- spawn selection (the original spawnselection map, 8 regions + random) -----------------------
static void DzDrawSpawnSelect(ImVec2 ds, float t)
{
    if(!s_DzX.spawnSel) return;
    float W = std::min(ds.x * 0.86f, ds.y * 0.80f * 2.0f), H = W * 0.5f, x0 = ds.x * 0.5f - W * 0.5f, y0 = ds.y * 0.5f - H * 0.5f + UI::S(14);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, 200));
    if(!DzImgFull(fg, "dz_spawn.png", ImVec2(x0, y0), ImVec2(x0 + W, y0 + H))) fg->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + W, y0 + H), IM_COL32(30, 40, 30, 255));
    fg->AddRect(ImVec2(x0, y0), ImVec2(x0 + W, y0 + H), IM_COL32(255, 255, 255, 60), 0, 0, UI::S(2));
    BcwCenterText(fg, UI::S(26), y0 - UI::S(40), DzCol(DZ_YEL), "SELECT YOUR SPAWN");
    ImGui::SetNextWindowPos(ImVec2(x0, y0)); ImGui::SetNextWindowSize(ImVec2(W, H));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DzSpawnSel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    for(int i = 0; i < 9; ++i)
    {
        const DzRegion& r = g_DzRegions[i]; ImVec2 c(x0 + r.u * W, y0 + r.v * H);
        float fs = UI::S(17), w = RpTextW(fs, r.name) + UI::S(28);
        ImVec2 a(c.x - w * 0.5f, c.y - UI::S(16));
        ImGui::SetCursorScreenPos(a); char id[16]; snprintf(id, sizeof(id), "##dzsp%d", i);
        bool hit = ImGui::InvisibleButton(id, ImVec2(w, UI::S(32))); bool hot = ImGui::IsItemActive();
        fg->AddRectFilled(a, ImVec2(a.x + w, a.y + UI::S(32)), hot ? DzCol(DZ_YEL, 0.85f) : IM_COL32(0, 0, 0, 170), UI::S(4));
        fg->AddCircleFilled(ImVec2(a.x + UI::S(10), c.y), UI::S(4) + UI::S(1.5f) * sinf(t * 4 + i), DzCol(i == 8 ? DZ_ORG : DZ_GRN), 10);
        fg->AddText(UI::Font(), fs, ImVec2(a.x + UI::S(20), a.y + UI::S(7)), hot ? IM_COL32(0, 0, 0, 255) : IM_COL32_WHITE, r.name);
        if(hit) { MpUiClick(); s_DzX.spawnSel = false; s_Dz.playAsked = MP_NowMs(); DzSend(DZCMD_PLAY, i == 8 ? 9 : i + 1); }
    }
    ImGui::End(); ImGui::PopStyleVar();
    // back
    ImGui::SetNextWindowPos(ImVec2(ds.x * 0.5f - UI::S(80), y0 + H + UI::S(8))); ImGui::SetNextWindowSize(ImVec2(UI::S(160), UI::S(56)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DzSpawnBack", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground);
    if(BcwTeamButton("BACK##dzspb", 0x57534EFFu, ImVec2(UI::S(160), UI::S(50)), false, false)) s_DzX.spawnSel = false;
    ImGui::End(); ImGui::PopStyleVar();
}
