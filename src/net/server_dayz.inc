// 3.58: Project Day Z Legacy (gamemode "Proper DAYZ") - the server side.
// Included by server.cpp (MPServer members). Everything runs with m_Mutex held (10 Hz tick / packets).
//
//  * survivors per account: blood, hunger, thirst, infection, humanity, murders, zombies / bandits
//    killed, headshots, points + level, 5 armour slots, bag level, GPS, the inventory (weights in kg)
//  * loot: every gas station (food, drinks, fuel), every military base / camp (guns, ammo, armour,
//    big bags), towns (houses: food, trash, sometimes a gun) and ruins. Taken spots come back 25-35 min
//    later with a new roll; items you drop / bodies stay 10 min
//  * 15 random spawns, parties of max 4 (each party its own colour: name tag + chat + kill feed),
//    anti-relog (leave within 15 s of a fight = your survivor dies), fog events (real 90 m fog),
//    chat loops [INFO] 25 min 20 s, [SERVER] 35 min 20 s, [GUIDE] 10 min 20 s, 44+ commands
#include <stdarg.h>
#include <stddef.h>
#include <algorithm>

static char s_szDzFile[256] = "dayz_survivors.txt";
void MPServerSetDzFile(const char* p) { MP_CopyStr(s_szDzFile, p && *p ? p : "dayz_survivors.txt", sizeof(s_szDzFile)); }

bool MPServer::IsDayzServer() const { return MP_IsDzServerName(m_szName); }

static uint32_t DzRand() { static uint32_t s = 0x9E3779B9u ^ (uint32_t)time(nullptr); s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
static float DzFrand() { return (DzRand() & 0xFFFFFF) / 16777216.0f; }

void MPServer::DzReset()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { m_Dz[i] = DzSurvivor(); m_nDzInvite[i] = -1; }
    for(auto& p : m_DzParty) p = DzParty();
    m_DzLoot.clear();
    m_bDzFog = false; m_bDzDirty = false;
    uint64_t now = MP_NowMs();
    m_nDzInfoAt = now + (25 * 60 + 20) * 1000ull;
    m_nDzSrvAt  = now + (35 * 60 + 20) * 1000ull;
    m_nDzGuideAt = now + (10 * 60 + 20) * 1000ull;
    m_nDzFogAt = now + (uint64_t)(72 + DzRand() % 145) * 60 * 1000ull;   // every 3 to 9 game days (1 day = 24 min)
    m_nDzFogEnd = 0; m_nDzSaveAt = now + 60000;
    m_DzBodies.clear(); m_nDzWeather = 1; m_nDzRain = 0; m_nDzRainEnd = 0;
    m_nDzWeatherAt = now + (uint64_t)(15 + DzRand() % 20) * 60 * 1000ull;
    for(int v = 0; v < DZV_COUNT; ++v) m_DzVar[v] = g_DzVarDefs[v].def;
    for(int i = 0; i < 128; ++i) m_DzStation[i] = -1;
    if(!IsDayzServer()) return;
    DzLoadVars();
    m_nDzStationAt = now + (uint64_t)DzVar(DZV_ITEMRESPAWN) * 60 * 1000ull;
    for(int s = 0; s < DZ_SITES && s < 128; ++s) if(g_DzSites[s].kind == DZS_GAS) m_DzStation[s] = (1 + (int)(DzRand() % DzVar(DZV_PETROLMAX))) * 10;   // MTA: random 1..maxPetrolFuelAmount
    // loot spots around every site (stable positions from the site index, so a restart keeps them)
    for(int s = 0; s < DZ_SITES; ++s)
    {
        const DzSite& st = g_DzSites[s];
        uint32_t seed = 0x2545F491u * (uint32_t)(s + 1);
        for(int k = 0; k < st.spots; ++k)
        {
            seed = seed * 1664525u + 1013904223u; float a = (seed >> 8) / 16777216.0f * 6.2831853f;
            seed = seed * 1664525u + 1013904223u; float r = st.r * (0.25f + 0.75f * ((seed >> 8) / 16777216.0f));
            DzLootSpot sp; sp.site = (uint8_t)s; sp.x = st.x + cosf(a) * r; sp.y = st.y + sinf(a) * r; sp.respawnAt = 0; sp.item = 0; sp.count = 0;
            DzRoll(sp);
            m_DzLoot.push_back(sp);
        }
    }
    Log("DAYZ: %d loot spots at %d sites", (int)m_DzLoot.size(), DZ_SITES);
}

// what lies at a spot (by the kind of site)
void MPServer::DzRoll(DzLootSpot& sp)
{
    const uint8_t kind = sp.site < DZ_SITES ? g_DzSites[sp.site].kind : DZS_HOUSE;
    uint32_t r = DzRand() % 100; int it = DZI_TRASH_CAN, n = 1;
    switch(kind)
    {
        case DZS_GAS:
        {   static const uint8_t k[] = { DZI_SODA, DZI_WATER, DZI_BURGER, DZI_PIZZA, DZI_BEANS, DZI_FUELCAN, DZI_SODA, DZI_WATER, DZI_BEER, DZI_TOOLBOX, DZI_MAP, DZI_BANDAGE, DZI_MORPHINE, DZI_COMPASS,
                                         DZI_EMPTY_CAN_FUEL, DZI_TIRE, DZI_SCRAP, DZI_MRE, DZI_STEW, DZI_HEATPACK, DZI_EMPTY_CAN_FUEL };
            it = k[DzRand() % sizeof(k)]; break; }
        case DZS_MILITARY:
        {   static const uint8_t k[] = { DZI_AK47, DZI_M4, DZI_MP5, DZI_SNIPER, DZI_SPAS, DZI_DEAGLE, DZI_AMMO_RIFLE, DZI_AMMO_RIFLE, DZI_AMMO_SMG,
                                         DZI_AMMO_SNIPER, DZI_AMMO_SHELLS, DZI_AMMO_PISTOL, DZI_HELMET, DZI_CHEST, DZI_VEST, DZI_GLOVES, DZI_BOOTS,
                                         DZI_BAG2, DZI_BAG3, DZI_GPS, DZI_MEDKIT, DZI_ANTIBIOTICS, DZI_RIFLE, DZI_KATANA,
                                         DZI_MORPHINE, DZI_EPINEPHRINE, DZI_BLOODBAG, DZI_COMPASS,
                                         DZI_BINOCULARS, DZI_RANGEFINDER, DZI_CLOTHES_CAMO, DZI_ENGINE, DZI_HEATPACK, DZI_MRE, DZI_TIRE };
            it = k[DzRand() % sizeof(k)];
            if(r < 2) it = DZI_AK_GOLD;   // rare
            else if(r < 4) it = DZI_KEYCARD;          // 3.59
            else if(r < 6) it = DZI_CLOTHES_GHILLIE;
            break; }
        case DZS_RUINS:
        {   static const uint8_t k[] = { DZI_TRASH_BOX, DZI_TRASH_BOTTLE, DZI_AMMO_PISTOL, DZI_BANDAGE, DZI_COLT, DZI_TEC9, DZI_BEANS, DZI_TOOLBOX, DZI_BAG1, DZI_PAINKILLERS,
                                         DZI_SCRAP, DZI_SCRAP, DZI_ENGINE, DZI_TIRE, DZI_EMPTY_CAN_FUEL, DZI_STEW };
            it = k[DzRand() % sizeof(k)]; break; }
        default:   // houses: food, trash and sometimes a gun
            if(r < 38)      { static const uint8_t k[] = { DZI_BEANS, DZI_BURGER, DZI_PIZZA, DZI_SODA, DZI_WATER, DZI_BEER }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 70) { static const uint8_t k[] = { DZI_TRASH_CAN, DZI_TRASH_BOTTLE, DZI_TRASH_BOX }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 80) { static const uint8_t k[] = { DZI_BANDAGE, DZI_PAINKILLERS, DZI_MEDKIT, DZI_ANTIBIOTICS, DZI_MORPHINE, DZI_EPINEPHRINE, DZI_BLOODBAG }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 87) { static const uint8_t k[] = { DZI_KNIFE, DZI_COLT, DZI_SHOTGUN, DZI_UZI, DZI_RIFLE }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 93) { static const uint8_t k[] = { DZI_AMMO_PISTOL, DZI_AMMO_SHELLS, DZI_AMMO_SMG }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 95) { static const uint8_t k[] = { DZI_CLOTHES_CIV, DZI_CLOTHES_SURV, DZI_STEW, DZI_HEATPACK, DZI_EMPTY_CAN_FUEL, DZI_TIRE }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 96) it = DZI_BAG1;
            else if(r < 98) it = DZI_FUELCAN;   // not many
            else it = DZI_MAP;
            break;
    }
    const DzItem& d = DzItemInfo(it);
    if(d.kind == DZK_AMMO) n = 1 + (int)(DzRand() % 2);
    sp.item = (uint8_t)it; sp.count = (uint8_t)n; sp.respawnAt = 0;
}

float MPServer::DzKg(int slot) const
{
    const DzSurvivor& z = m_Dz[slot]; float kg = 0;
    for(int i = 0; i < z.nInv; ++i) kg += DzItemInfo(z.inv[i].item).kg * (DzItemStacks(z.inv[i].item) || DzItemInfo(z.inv[i].item).kind == DZK_FUEL ? z.inv[i].count : 1);
    return kg;
}
float MPServer::DzMaxKg(int slot) const
{
    const DzSurvivor& z = m_Dz[slot];
    return 10.0f + (z.bag == 1 ? 20.0f : z.bag == 2 ? 40.0f : z.bag == 3 ? 70.0f : 0.0f);   // pockets 10 kg + the bag
}
static bool DzStacks(int item) { return DzItemStacks(item) || DzItemInfo(item).kind == DZK_FUEL; }
bool MPServer::DzGive(int slot, int item, int count)
{
    DzSurvivor& z = m_Dz[slot];
    const DzItem& d = DzItemInfo(item); if(!d.kind || count <= 0) return false;
    if(d.kind == DZK_BAG)
    {   // a bag is worn at once (a better one replaces the old: the old one falls on the ground)
        int lvl = d.ammo; if(lvl <= z.bag) return false;
        z.bag = (uint8_t)lvl; return true;
    }
    if(d.kind == DZK_ARMOR)
    {
        int sl = d.val; if(sl < 0 || sl >= 5 || z.armor[sl]) return false;
        z.armor[sl] = (uint8_t)item; return true;
    }
    if(d.kind == DZK_GPS && !z.gps) { z.gps = true; }   // the GPS goes in the inventory too (and unlocks the map)
    float add = d.kg * (DzStacks(item) ? count : 1);
    if(DzKg(slot) + add > DzMaxKg(slot) + 0.001f) return false;
    if(DzStacks(item)) for(int i = 0; i < z.nInv; ++i) if(z.inv[i].item == item) { z.inv[i].count = (uint16_t)std::min(9999, z.inv[i].count + count); return true; }
    if(z.nInv >= DZ_INV_MAX) return false;
    z.inv[z.nInv].item = (uint8_t)item; z.inv[z.nInv].pad = 0; z.inv[z.nInv].count = (uint16_t)(DzStacks(item) ? count : (d.kind == DZK_WEAPON ? d.ammo : 1)); ++z.nInv;
    return true;
}

void MPServer::DzMsg(int slot, uint32_t col, const char* fmt, ...)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = col;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    if(slot >= 0) { if(m_Slots[slot].used) SendTo(slot, &m, sizeof(m)); }
    else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &m, sizeof(m));
}
void MPServer::DzEvent(int slot, uint8_t ev, int a, int b, int32_t val, const float* pos, const char* text)
{
    PktDzEvent e; memset(&e, 0, sizeof(e)); e.type = PKT_DZ_EVENT; e.ev = ev; e.a = (uint8_t)a; e.b = (uint8_t)b; e.val = val;
    if(pos) memcpy(e.pos, pos, sizeof(float) * 4);
    MP_CopyStr(e.text, text ? text : "", sizeof(e.text));
    if(slot >= 0) { if(m_Slots[slot].used) SendTo(slot, &e, sizeof(e)); }
    else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &e, sizeof(e));
}
void MPServer::DzSendMe(int slot)
{
    const DzSurvivor& z = m_Dz[slot];
    PktDzMe m; memset(&m, 0, sizeof(m)); m.type = PKT_DZ_ME;
    m.party = (uint8_t)(z.party < 0 ? 255 : z.party); m.gps = z.gps; m.bag = z.bag;
    m.blood = (int16_t)z.blood; m.hunger = (uint8_t)z.hunger; m.thirst = (uint8_t)z.thirst; m.infected = z.infected;
    uint64_t now = MP_NowMs(); m.combat = (uint8_t)(z.combatUntil > now ? (z.combatUntil - now + 999) / 1000 : 0);
    m.humanity = z.humanity; m.murders = (uint16_t)z.murders; m.zkills = (uint16_t)z.zkills; m.bkills = (uint16_t)z.bkills; m.headshots = (uint16_t)z.headshots;
    m.points = z.points; memcpy(m.armor, z.armor, 5); m.alive = z.alive; m.fog = m_bDzFog;
    m.partyN = (uint8_t)(z.party >= 0 ? m_DzParty[z.party].n : 0);
    m.maxKg100 = (uint16_t)(DzMaxKg(slot) * 100); m.kg100 = (uint16_t)(DzKg(slot) * 100);
    m.partyCol = z.party >= 0 ? m_DzParty[z.party].col : DZ_SOLO_COL; m.aliveMin = z.aliveMin;
    // 3.59
    m.temp100 = (int16_t)z.temp100; m.cond = (uint16_t)(z.cond | (z.armor[DZA_HELMET] ? DZCO_HELMET : 0)); m.ach = z.ach;
    for(int k = 0; k < DZSK_COUNT; ++k) m.skills[k] = z.skills[k];
    { bool kc = false; for(int k = 0; k < z.nInv; ++k) if(z.inv[k].item == DZI_KEYCARD) kc = true; if(kc) m.cond |= DZCO_KEYCARD; }
    m.skillPts = (uint8_t)std::max(0, std::min(255, DzSkillPts(slot))); m.difficulty = (uint8_t)(DzVar(DZV_DIFFICULTY) - 1);
    m.spawnSel = (uint8_t)DzVar(DZV_SPAWNSELECTION); m.camSway = (uint8_t)DzVar(DZV_CAMSWAY);
    m.zombieSys = (uint8_t)DzVar(DZV_NEWZOMBIESPAWN); m.zombieRadius = (uint8_t)DzVar(DZV_ZOMBIERADIUS); m.zombiesPerLoot = (uint8_t)DzVar(DZV_ZOMBIESPERLOOT); m.rain = (uint8_t)m_nDzRain;
    m.skin = (uint16_t)z.skin; m.confirmed = (uint16_t)z.confirmed; m.zombieDmg = (uint16_t)DzVar(DZV_ZOMBIEDAMAGE); m.revives = (uint16_t)z.revives;
    SendTo(slot, &m, sizeof(m));
}
void MPServer::DzSendInv(int slot)
{
    const DzSurvivor& z = m_Dz[slot];
    PktDzInv p; memset(&p, 0, sizeof(p)); p.type = PKT_DZ_INV; p.n = (uint8_t)z.nInv; p.hands = z.hands;
    memcpy(p.s, z.inv, sizeof(DzInvSlot) * z.nInv);
    SendTo(slot, &p, (int)(offsetof(PktDzInv, s) + sizeof(DzInvSlot) * z.nInv));
}
void MPServer::DzSendLoot(int slot, int onlyIdx)
{
    PktDzLoot p; memset(&p, 0, sizeof(p)); p.type = PKT_DZ_LOOT; p.total = (uint16_t)m_DzLoot.size();
    auto flush = [&]() {
        if(!p.n) return;
        int len = (int)(offsetof(PktDzLoot, r) + sizeof(DzLootRow) * p.n);
        if(slot >= 0) SendTo(slot, &p, len); else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &p, len);
        p.n = 0;
    };
    for(int i = 0; i < (int)m_DzLoot.size(); ++i)
    {
        if(onlyIdx >= 0 && i != onlyIdx) continue;
        const DzLootSpot& sp = m_DzLoot[i];
        DzLootRow& r = p.r[p.n++]; r.idx = (uint16_t)i; r.item = sp.respawnAt ? 0 : sp.item; r.count = sp.count; r.x = sp.x; r.y = sp.y;
        if(p.n == DZ_LOOT_PAGE) flush();
    }
    flush();
}

void MPServer::DzSetColor(int slot)
{   // party colour on the name tag, the chat and the kill feed (solo = light grey)
    Slot& s = m_Slots[slot]; const DzSurvivor& z = m_Dz[slot];
    uint32_t c = z.party >= 0 ? m_DzParty[z.party].col : DZ_SOLO_COL;
    if(s.admin || s.mod) c = s.admin ? 0xEF4444FFu : 0xF97316FFu;   // staff keep red / orange
    if(s.color == c) return;
    s.color = c;
    PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
    pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)slot; pj.color = s.color; pj.skin = s.skin;
    MP_CopyStr(pj.name, s.name, MP_NAME_LEN);
    Broadcast(&pj, sizeof(pj), -1);
}

// ---- save / load: Z key|alive|blood|hunger|thirst|inf|hum|mur|zk|bk|hs|pts|min|bag|gps|a0..a4|x|y|z|items ----
void MPServer::DzLoad()
{
    m_DzSaved.clear();
    FILE* f = fopen(s_szDzFile, "r"); if(!f) { Log("DAYZ: no %s yet (new world)", s_szDzFile); return; }
    char line[1400]; int n = 0;
    while(fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if(line[0] != 'Z' || line[1] != ' ') continue;
        char* bar = strchr(line + 2, '|'); if(!bar) continue; *bar = 0;
        m_DzSaved[line + 2] = bar + 1; ++n;
    }
    fclose(f);
    Log("DAYZ: %d survivors loaded from %s", n, s_szDzFile);
}
// 3.60: a position the survivor really stood on. While on the connection screen the ped is parked at
// the cabin (x 2493 y -1700 = Grove Street, z -100 / interior z 1015) - that used to be saved as
// "where you logged out", so every spawn went to the Grove Street roof.
static bool DzPosValid(const float* p)
{
    if(!p || (fabsf(p[0]) < 1.0f && fabsf(p[1]) < 1.0f)) return false;
    if(fabsf(p[0]) > 3000.0f || fabsf(p[1]) > 3000.0f || p[2] < -20.0f || p[2] > 700.0f) return false;
    float dx = p[0] - 2497.4f, dy = p[1] + 1696.2f; if(dx * dx + dy * dy < 12.0f * 12.0f) return false;   // the cabin spot itself
    return true;
}
void MPServer::DzSave()
{
    // the online survivors first (their live state)
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_Dz[i].loaded) continue;
        char kb[40]; const char* key = RpKey(i, kb, sizeof(kb)); if(!key) continue;
        const DzSurvivor& z = m_Dz[i]; char buf[1300];
        int o = snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%u|%d|%d|%d,%d,%d,%d,%d|%.1f|%.1f|%.1f|",
                         z.relogDied ? -1 : (int)z.alive, z.blood, z.hunger, z.thirst, z.infected, z.humanity, z.murders, z.zkills, z.bkills, z.headshots, z.points, z.aliveMin,
                         z.bag, z.gps, z.armor[0], z.armor[1], z.armor[2], z.armor[3], z.armor[4], z.hasPos ? z.lastPos[0] : 0.0f, z.hasPos ? z.lastPos[1] : 0.0f, z.hasPos ? z.lastPos[2] : 0.0f);
        for(int k = 0; k < z.nInv && o < (int)sizeof(buf) - 16; ++k) o += snprintf(buf + o, sizeof(buf) - o, "%d:%d,", z.inv[k].item, z.inv[k].count);
        // 3.59: #temp|cond|achievements|skills|skin|confirmed|revives (after the items: older builds stop reading at the '#')
        char sk[DZSK_COUNT + 1]; for(int k = 0; k < DZSK_COUNT; ++k) sk[k] = (char)('0' + std::min<int>(9, z.skills[k])); sk[DZSK_COUNT] = 0;
        if(o < (int)sizeof(buf) - 80) snprintf(buf + o, sizeof(buf) - o, "#%d|%u|%u|%s|%d|%d|%d", z.temp100, (unsigned)(z.cond & ~(DZCO_HELMET | DZCO_KEYCARD | DZCO_UNCONSCIOUS)), (unsigned)z.ach, sk, z.skin, z.confirmed, z.revives);
        m_DzSaved[key] = buf;
    }
    std::string tmp = std::string(s_szDzFile) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
    for(auto& kv : m_DzSaved) fprintf(f, "Z %s|%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f); rename(tmp.c_str(), s_szDzFile);
    m_bDzDirty = false;
}

void MPServer::DzFresh(int slot)
{   // a new survivor: better start gear than the old DayZ (a small bag, a pistol, food, water, bandages)
    DzSurvivor& z = m_Dz[slot]; int party = z.party;
    int hum = z.humanity, mur = z.murders, zk = z.zkills, bk = z.bkills, hs = z.headshots, pts = z.points;
    uint32_t ach = z.ach; uint8_t sk[DZSK_COUNT]; memcpy(sk, z.skills, sizeof(sk)); int conf = z.confirmed, rev = z.revives, adv = z.adverts;
    z = DzSurvivor(); z.loaded = true; z.party = party;
    z.ach = ach; memcpy(z.skills, sk, sizeof(sk)); z.confirmed = conf; z.revives = rev; z.adverts = adv;   // 3.59: achievements + skills stay
    z.humanity = hum; z.murders = mur; z.zkills = zk; z.bkills = bk; z.headshots = hs; z.points = pts;   // the stats stay, the gear is new
    z.bag = 1;
    DzGive(slot, DZI_COLT, 1); DzGive(slot, DZI_AMMO_PISTOL, 2); DzGive(slot, DZI_KNIFE, 1);
    DzGive(slot, DZI_BEANS, 1); DzGive(slot, DZI_WATER, 1); DzGive(slot, DZI_BANDAGE, 2); DzGive(slot, DZI_PAINKILLERS, 1);
    z.hands = DZI_COLT;
}
void MPServer::DzOnAuthed(int slot)
{
    if(!IsDayzServer()) return;
    DzSurvivor& z = m_Dz[slot]; z = DzSurvivor(); z.loaded = true;
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb));
    auto it = key ? m_DzSaved.find(key) : m_DzSaved.end();
    if(it == m_DzSaved.end()) { DzFresh(slot); z.alive = false; }
    else
    {
        char b[1300]; MP_CopyStr(b, it->second.c_str(), sizeof(b));
        int alive = 0, inf = 0, gps = 0, bag = 0, a[5] = { 0 }; float px = 0, py = 0, pz = 0;
        sscanf(b, "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%u|%d|%d|%d,%d,%d,%d,%d|%f|%f|%f|", &alive, &z.blood, &z.hunger, &z.thirst, &inf, &z.humanity, &z.murders,
               &z.zkills, &z.bkills, &z.headshots, &z.points, &z.aliveMin, &bag, &gps, &a[0], &a[1], &a[2], &a[3], &a[4], &px, &py, &pz);
        z.alive = alive != 0; z.infected = inf != 0; z.gps = gps != 0; z.bag = (uint8_t)std::max(0, std::min(3, bag));
        for(int k = 0; k < 5; ++k) z.armor[k] = (uint8_t)(a[k] > 0 && a[k] < DZI_COUNT ? a[k] : 0);
        // items after the 22nd '|'
        const char* q = b; for(int k = 0; k < 22 && q; ++k) { q = strchr(q, '|'); if(q) ++q; }
        while(q && *q && z.nInv < DZ_INV_MAX)
        {
            int id = 0, c = 0; if(sscanf(q, "%d:%d", &id, &c) != 2) break;
            if(id > 0 && id < DZI_COUNT && c > 0) { z.inv[z.nInv].item = (uint8_t)id; z.inv[z.nInv].count = (uint16_t)c; ++z.nInv; }
            q = strchr(q, ','); if(q) ++q;
        }
        if(const char* ex = strchr(b, '#'))
        {   // 3.59
            unsigned cond = 0, ach = 0; char sk[32] = "";
            sscanf(ex + 1, "%d|%u|%u|%31[0-9]|%d|%d|%d", &z.temp100, &cond, &ach, sk, &z.skin, &z.confirmed, &z.revives);
            z.cond = (uint16_t)cond; z.ach = ach;
            for(int k = 0; k < DZSK_COUNT && sk[k]; ++k) z.skills[k] = (uint8_t)std::min<int>(g_DzSkills[k].maxRank, sk[k] - '0');
            if(z.temp100 < 3000 || z.temp100 > 4200) z.temp100 = 3700;
        }
        if(alive < 0) z.relogNotice = true;   // 3.58b: died by ANTI-RELOG (told when the player is in)
        if(!z.alive || z.blood <= 0) { DzFresh(slot); z.alive = false; }
        else { m_Slots[slot].pos[0] = px; m_Slots[slot].pos[1] = py; m_Slots[slot].pos[2] = pz; float lp[3] = { px, py, pz }; if(DzPosValid(lp)) { memcpy(z.lastPos, lp, sizeof(lp)); z.hasPos = true; } }
        z.spawned = false;
        // remember where to wake up
        z.lastPick = 0;
        if(z.alive) { m_Dz[slot].combatUntil = 0; }
        if(z.alive) { float p4[4] = { px, py, pz, 0 }; (void)p4; }
    }
    DzSendMe(slot); DzSendInv(slot);
}
void MPServer::DzOnJoin(int slot, bool fresh)
{
    if(!IsDayzServer()) return;
    DzSendLoot(slot);
    DzSetColor(slot);
    if(m_bDzFog) DzEvent(slot, DZEV_FOG, 1, 0, 90, nullptr, "");
    DzSendWeather(slot);
    if(m_Dz[slot].skin) DzEvent(slot, DZEV_SKIN, 0, 0, m_Dz[slot].skin, nullptr, "");
    DzLog("JOIN %s (id %d)", m_Slots[slot].name, slot);
    if(fresh)
    {
        DzMsg(slot, DZC_YELLOW, "Welcome to Project Day Z Legacy (Proper DAYZ). Survive. Trust nobody.");
        DzMsg(slot, DZC_GREY, "Press F2 to open the control panel - /help for all commands, /rules for the rules.");
    }
    if(m_Dz[slot].relogNotice)
    {   // 3.58b
        m_Dz[slot].relogNotice = false;
        DzMsg(slot, DZC_RED, "[ANTI-RELOG] You disconnected during combat - your survivor DIED. Your gear stayed on the body.");
        DzEvent(slot, DZEV_NOTICE, 0, 0, 7000, nullptr, "ANTI-RELOG: you died on disconnect");
    }
    if(m_Dz[slot].loaded) { DzSendMe(slot); DzSendInv(slot); }
}
void MPServer::DzOnLeave(int slot)
{
    if(!IsDayzServer()) return;
    DzSurvivor& z = m_Dz[slot];
    if(z.loaded) DzLog("QUIT %s (id %d)%s", m_Slots[slot].name, slot, z.combatUntil > MP_NowMs() ? " IN COMBAT" : "");
    if(z.loaded && z.alive && DzVar(DZV_COMBATLOG) && (z.combatUntil > MP_NowMs() || z.downed))
    {   // ANTI-RELOG: disconnecting in a fight (or while downed) kills your survivor; the gear falls on the ground
        DzDropBody(slot);
        DzEvent(-1, DZEV_DOWNED, slot, 0, 0, nullptr, "");
        z.alive = false; z.blood = 0; z.nInv = 0; z.bag = 0; memset(z.armor, 0, 5); z.gps = false; z.hands = 0;
        z.relogDied = true; z.downed = false;
        DzMsg(-1, DZC_RED, "[ANTI-RELOG] %s disconnected during combat and died.", m_Slots[slot].name);
        Log("DAYZ: anti-relog death: %s", m_Slots[slot].name); DzLog("ANTI-RELOG death %s", m_Slots[slot].name);
    }
    if(z.loaded) { DzSave(); }
    if(z.party >= 0) DzPartyLeave(slot, false);
    z = DzSurvivor();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_nDzInvite[i] == slot) m_nDzInvite[i] = -1;
}
void MPServer::DzCombat(int slot, uint64_t now)
{   // 3.58b: ANTI-RELOG 15 s - the phone shows "ANTI-RELOG / DISCONNECT = DEATH / 15" and counts down
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Dz[slot].loaded) return;
    DzSurvivor& z = m_Dz[slot];
    bool was = z.combatUntil > now;
    z.combatUntil = now + 15000;
    DzEvent(slot, DZEV_COMBAT, 0, 0, 15000, nullptr, "");
    if(!was && now - z.relogWarned > 120000)
    {
        z.relogWarned = now;
        DzMsg(slot, DZC_YELLOW, "[!] If you quit right now while 'ANTI-RELOG' message is visible - you will die on disconnect");
    }
}
void MPServer::DzDropBody(int slot)
{   // the inventory + armour fall on the ground around the body (max 14 items)
    DzSurvivor& z = m_Dz[slot]; int dropped = 0;
    auto drop = [&](int item, int count) {
        if(dropped >= 14 || item <= 0 || item >= DZI_COUNT) return;
        DzLootSpot sp; sp.site = 255; sp.item = (uint8_t)item; sp.count = (uint8_t)std::max(1, std::min<int>(255, count));
        float a = dropped * 0.63f, r = 1.0f + (dropped % 3) * 0.35f; sp.x = m_Slots[slot].pos[0] + cosf(a) * r; sp.y = m_Slots[slot].pos[1] + sinf(a) * r;
        sp.respawnAt = 0; m_DzLoot.push_back(sp); DzSendLoot(-1, (int)m_DzLoot.size() - 1); ++dropped;
    };
    for(int i = 0; i < z.nInv; ++i) drop(z.inv[i].item, z.inv[i].count);
    for(int k = 0; k < 5; ++k) drop(z.armor[k], 1);
    if(z.bag) drop(DZI_BAG1 + z.bag - 1, 1);
    m_bDzDirty = true;
}
void MPServer::DzPartyEvent(int slot, uint8_t ev, int a, int b, const char* text)
{   // to every other member of slot's party
    const DzSurvivor& z = m_Dz[slot]; if(z.party < 0 || z.party >= 64) return;
    const DzParty& P = m_DzParty[z.party];
    for(int k = 0; k < P.n; ++k) { int m = P.members[k]; if(m != slot && m >= 0 && m < MP_MAX_PLAYERS && m_Slots[m].used) DzEvent(m, ev, a, b, 0, nullptr, text); }
}

void MPServer::DzSpawn(int slot, int region)
{
    DzSurvivor& z = m_Dz[slot];
    float p[4];
    char name[64];
    // 3.60: a location tapped on the spawn map always wins; plain Play wakes an alive survivor up where
    // they really logged out (the last outdoor spot, never the cabin parked under Grove Street).
    const bool picked = DzVar(DZV_SPAWNSELECTION) && region >= 1 && region <= 9;
    if(!picked && z.alive && z.hasPos)
    { p[0] = z.lastPos[0]; p[1] = z.lastPos[1]; p[2] = z.lastPos[2]; p[3] = 0; MP_CopyStr(name, "where you logged out", sizeof(name)); }
    else
    {
        int si = (int)(DzRand() % 15);
        if(DzVar(DZV_SPAWNSELECTION) && region >= 1 && region <= 8)
        {   // 3.59: MTA DayZ spawn selection (the region you tapped on the map)
            const DzRegion& rg = g_DzRegions[region - 1]; int n = 0; for(int8_t k : rg.spawns) if(k >= 0) ++n;
            if(n) { int pick = (int)(DzRand() % n); for(int8_t k : rg.spawns) if(k >= 0 && pick-- == 0) si = k; }
        }
        const DzSpawnPt& s = g_DzSpawns[si];
        p[0] = s.x; p[1] = s.y; p[2] = s.z; p[3] = s.h; MP_CopyStr(name, s.name, sizeof(name));
        if(!z.alive)
        {
            z.alive = true; z.blood = 12000; z.hunger = z.thirst = 100; z.infected = false; z.aliveMin = 0; z.temp100 = 3700; z.cond = 0; z.hunted = false;
            if(DzVar(DZV_CLOTHES)) { static const int sk[] = { 60, 73, 2, 7, 22, 23, 29, 44, 45, 47, 48, 98 }; z.skin = sk[DzRand() % (sizeof(sk) / sizeof(sk[0]))]; }   // MTA: random clothes on spawn
        }
    }
    z.spawned = true; z.lastNeeds = z.lastMin = MP_NowMs();
    AcGrace(slot, 8000);
    DzEvent(slot, DZEV_SPAWN, 0, 0, 0, p, name);
    // the weapons in the inventory go into the hands (the GTA weapon slots)
    for(int i = 0; i < z.nInv; ++i)
    {
        const DzItem& d = DzItemInfo(z.inv[i].item);
        if(d.kind == DZK_WEAPON) DzEvent(slot, DZEV_WEAPON, d.val, 0, std::max<int>(1, z.inv[i].count), nullptr, d.name);
    }
    if(z.skin) DzEvent(slot, DZEV_SKIN, 0, 0, z.skin, nullptr, "");
    DzSendWeather(slot);
    DzSendMe(slot); DzSendInv(slot);
    DzMsg(slot, DZC_GREEN, "You woke up at %s. Find food, water, a GPS and a better bag.", name);
    DzLog("SPAWN %s at %s", m_Slots[slot].name, name);
}

void MPServer::DzOnDeath(int slot, int killer, int weapon)
{
    if(!IsDayzServer()) return;
    DzSurvivor& z = m_Dz[slot]; if(!z.loaded) return;
    // the body: the inventory falls on the ground, stays 10 minutes
    const uint64_t now = MP_NowMs(); (void)now;
    DzDropBody(slot);
    if(z.downed) { z.downed = false; DzEvent(-1, DZEV_DOWNED, slot, 0, 0, nullptr, ""); }
    if(killer >= 0 && killer < MP_MAX_PLAYERS && killer != slot && m_Slots[killer].used && m_Dz[killer].loaded)
    {   // 3.59 (MTA DayZ): the humanity changes now, the kill counts only after "Check Body"
        DzSurvivor& k = m_Dz[killer];
        if(z.humanity < 0) { k.humanity += 500; k.points += 50; DzMsg(killer, DZC_GREEN, "Bandit killed: +500 humanity, +50 points. Check the body to confirm the kill."); }
        else               { k.humanity -= 1000; DzMsg(killer, DZC_RED, "You murdered a survivor: -1000 humanity. Check the body to confirm the kill."); }
        if(weapon == 33 || weapon == 34) { k.headshots++; }
        DzBody b; memset(&b, 0, sizeof(b)); b.id = m_nDzBodyId++; b.killer = killer; MP_CopyStr(b.victim, m_Slots[slot].name, sizeof(b.victim)); b.weapon = weapon;
        b.x = m_Slots[slot].pos[0]; b.y = m_Slots[slot].pos[1]; b.z = m_Slots[slot].pos[2]; b.at = MP_NowMs(); b.checked = false; b.bandit = z.humanity < 0;
        m_DzBodies.push_back(b); if(m_DzBodies.size() > 64) m_DzBodies.erase(m_DzBodies.begin());
        float bp[4] = { b.x, b.y, b.z, 0 };
        DzEvent(-1, DZEV_BODY, 0, 0, b.id, bp, b.victim);
        DzLog("KILL %s killed %s (weapon %d)", m_Slots[killer].name, m_Slots[slot].name, weapon);
        DzSendMe(killer);
    }
    else DzLog("DEATH %s (weapon %d)", m_Slots[slot].name, weapon);
    z.alive = false; z.blood = 0; z.nInv = 0; z.bag = 0; memset(z.armor, 0, 5); z.gps = false; z.combatUntil = 0;
    DzFresh(slot); z.alive = false;
    m_bDzDirty = true;
    DzEvent(slot, DZEV_DIED, 0, 0, 0, nullptr, "YOU ARE DEAD");
    DzSendMe(slot); DzSendInv(slot);
}
bool MPServer::DzOnDamage(int from, int to, int amount)
{
    if(!IsDayzServer()) return true;
    if(from < 0 || to < 0 || from >= MP_MAX_PLAYERS || to >= MP_MAX_PLAYERS) return true;
    // party members can't hurt each other
    if(m_Dz[from].party >= 0 && m_Dz[from].party == m_Dz[to].party) return false;
    uint64_t now = MP_NowMs();
    DzCombat(from, now); DzCombat(to, now);   // ANTI-RELOG for both
    (void)amount;
    return true;
}

// ---- parties (max 4) ----
void MPServer::DzPartyLeave(int slot, bool quiet)
{
    DzSurvivor& z = m_Dz[slot]; if(z.party < 0) return;
    DzParty& p = m_DzParty[z.party];
    for(int i = 0; i < p.n; ++i) if(p.members[i] == slot) { p.members[i] = p.members[--p.n]; break; }
    int pid = z.party; z.party = -1;
    if(m_Slots[slot].used) DzSetColor(slot);
    if(!quiet) for(int i = 0; i < p.n; ++i) DzMsg(p.members[i], DZC_ORANGE, "[PARTY] %s left the party.", m_Slots[slot].name);
    if(p.n == 0) { p = DzParty(); }
    else if(p.leader == slot) { p.leader = p.members[0]; DzMsg(p.leader, DZC_ORANGE, "[PARTY] You lead the party now."); }
    (void)pid;
}
bool MPServer::DzChat(int slot, const char* text)
{
    if(!text) return false;
    if(DzVar(DZV_NOADVERT) && !m_Slots[slot].admin && DzIsAdvert(text))
    {   // 3.59: MTA DayZ anti-advertising (IPs, links, invites) - 3 times = kick
        DzSurvivor& z = m_Dz[slot]; ++z.adverts;
        DzLog("ADVERT %s: %s", m_Slots[slot].name, text);
        DzMsg(slot, DZC_RED, "[ANTI-ADVERT] Advertising is not allowed here (%d/3). Your message was blocked.", z.adverts);
        if(z.adverts >= 3) { z.adverts = 0; DzMsg(-1, DZC_RED, "[ANTI-ADVERT] %s was kicked for advertising.", m_Slots[slot].name); KickLocked(slot, "Anti-Advert", "Advertising", false); }
        return true;
    }
    if(text[0] == '/')
    {
        char cmd[32] = ""; const char* arg = text + 1; int n = 0;
        while(*arg && *arg != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*arg++);
        cmd[n] = 0; while(*arg == ' ') ++arg;
        return DzCommand(slot, cmd, arg);
    }
    return false;   // normal chat: broadcast with the party colour as the name colour
}

static const char* const kDzRules[] = {   // 3.60: the same 10 rules as the client window
    "1. No cheating: no CLEO, mod menus, aimbots, speed / fly hacks (libCheatMenu is blocked).",
    "2. ANTI-RELOG: leaving the game during combat = your survivor dies.",
    "3. No bug abuse: no glitching into walls or under the map - report it to an admin.",
    "4. No spawn killing at the 15 spawn points or on fresh survivors.",
    "5. Parties: max 4 survivors. Don't pretend to be another party.",
    "6. Respect everyone: no insults, racism, threats or harassment.",
    "7. No spam: no flooding, caps lock or advertising other servers.",
    "8. Don't pretend to be staff (admin, moderator or the bot Broker).",
    "9. Listen to the admins: a kick waits 4:29, 3 fly / teleport kicks = permanent ban.",
    "10. Survive, loot, trust no one - and have fun.",
};

bool MPServer::DzCommand(int slot, const char* cmd, const char* arg)
{
    if(!IsDayzServer()) return false;
    DzSurvivor& z = m_Dz[slot]; Slot& s = m_Slots[slot];
    auto IS = [&](const char* c) { return !strcmp(cmd, c); };
    if(IS("help") || IS("cmds") || IS("commands"))
    {
        DzMsg(slot, DZC_YELLOW, "--- Project Day Z commands (44+) ---");
        DzMsg(slot, DZC_WHITE, "/inv /bag /map /gps /compass /hud /stats /level /humanity /top /armor /weapon /fps /ping");
        DzMsg(slot, DZC_WHITE, "/eat /drink /heal /bandage /cure /painkillers /drop /pickup /refuel /fuel /loot /supplies");
        DzMsg(slot, DZC_WHITE, "/party /pcreate /pinvite <id> /paccept /pdecline /pleave /pkick <id> /plist /p <text>");
        DzMsg(slot, DZC_WHITE, "/pm <id> <text> /me /do /shout /whisper /time /fog /spawns /kill /rules /credits /admins /report /music");
        DzMsg(slot, DZC_WHITE, "/skills /skill <n> /achievements /journal /undress /weather /station /checkbody /treat <id> /chat /vars");
        DzMsg(slot, DZC_GREY, "Keys: 6 / F1 journal (debug monitor), 7 GPS, 8 compass, TAB player list. Admin: /dzset /dzvars /respawnvehicles /dzweather");
        DzMsg(slot, DZC_GREY, "Press F2 (or PANEL) for the control panel. Most commands open their window too.");
        DzEvent(slot, DZEV_UI, 1, 0, 0, nullptr, "");
        return true;
    }
    if(IS("rules")) { for(const char* r : kDzRules) DzMsg(slot, DZC_WHITE, "%s", r); DzEvent(slot, DZEV_UI, 2, 0, 0, nullptr, ""); return true; }
    if(IS("credits")) { DzMsg(slot, DZC_YELLOW, "Project Day Z Legacy - based on Project DayZ SA-MP, made for MP by ThirtyFox.");
                        DzMsg(slot, DZC_WHITE, "Ambient music: No Man's Land | Metro / S.T.A.L.K.E.R Atmospheric Playlist by Lemuria (youtube.com/@lemurian.archives, youtu.be/aVTcVx1570w). /music on / off.");
                        DzEvent(slot, DZEV_UI, 5, 0, 0, nullptr, ""); return true; }
    if(IS("stats") || IS("level") || IS("humanity") || IS("score"))
    {
        DzMsg(slot, DZC_YELLOW, "Points: %d (level %d) | To next level: %d", z.points, DzLevel(z.points), DzToNext(z.points));
        DzMsg(slot, DZC_WHITE, "Murders: %d | Zombies killed: %d | Bandits killed: %d | Headshots: %d", z.murders, z.zkills, z.bkills, z.headshots);
        DzMsg(slot, z.humanity < 0 ? DZC_RED : DZC_GREEN, "Humanity: %d (%s) | Blood: %d | Alive: %u min", z.humanity, z.humanity < 0 ? "Bandit" : "Survivor", z.blood, z.aliveMin);
        DzEvent(slot, DZEV_UI, 3, 0, 0, nullptr, "");
        return true;
    }
    if(IS("top"))
    {
        int best[3] = { -1, -1, -1 };
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!m_Slots[i].used || !m_Dz[i].loaded) continue;
            for(int k = 0; k < 3; ++k) if(best[k] < 0 || m_Dz[i].points > m_Dz[best[k]].points) { for(int j = 2; j > k; --j) best[j] = best[j - 1]; best[k] = i; break; }
        }
        DzMsg(slot, DZC_YELLOW, "--- Top survivors online ---");
        for(int k = 0; k < 3; ++k) if(best[k] >= 0) DzMsg(slot, DZC_WHITE, "%d. %s - %d points (level %d), %d zombies", k + 1, m_Slots[best[k]].name, m_Dz[best[k]].points, DzLevel(m_Dz[best[k]].points), m_Dz[best[k]].zkills);
        return true;
    }
    if(IS("time")) { DzMsg(slot, DZC_GREY, "The day goes round every 24 minutes. Nights are dark - use the light of the fire and the streets."); return true; }
    if(IS("fog")) { DzMsg(slot, m_bDzFog ? DZC_RED : DZC_GREY, m_bDzFog ? "FOG: you can see only 90 m. Zombies and bandits hide in it." : "No fog right now. It comes back every 3 to 9 days."); return true; }
    if(IS("spawns")) { DzMsg(slot, DZC_YELLOW, "15 spawns: you wake up at a random one of them (Angel Pine, Flint, Blueberry, Dillimore, Palomino, Montgomery, Back o Beyond, Fort Carson, El Quebrados, Bayside...)."); return true; }
    if(IS("supplies") || IS("loot"))
    {
        DzMsg(slot, DZC_YELLOW, "[GUIDE] Gas stations: food, drinks and fuel. Military bases / camps: guns, ammo, armour, big bags, GPS.");
        DzMsg(slot, DZC_WHITE, "[GUIDE] Houses: food, trash and sometimes a gun. Ruins: a bit of everything. Taken loot comes back in ~30 min.");
        return true;
    }
    if(IS("kill") || IS("suicide")) { DzEvent(slot, DZEV_HEAL, 0, 0, -1, nullptr, ""); return true; }
    if(IS("admins"))
    {
        int n = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && (m_Slots[i].admin || m_Slots[i].mod)) { DzMsg(slot, DZC_ORANGE, "%s (id %d)", m_Slots[i].name, i); ++n; }
        if(!n) DzMsg(slot, DZC_GREY, "No staff online. Use /report <id> <reason> - the Broker bot keeps it for them.");
        return true;
    }
    if(IS("me") || IS("do") || IS("shout") || IS("s") || IS("whisper") || IS("w"))
    {
        if(!arg[0]) { DzMsg(slot, DZC_GREY, "Usage: /%s <text>", cmd); return true; }
        float r = IS("shout") || IS("s") ? 60.0f : (IS("whisper") || IS("w") ? 4.0f : 25.0f);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!m_Slots[i].used) continue;
            float dx = m_Slots[i].pos[0] - s.pos[0], dy = m_Slots[i].pos[1] - s.pos[1];
            if(dx * dx + dy * dy > r * r) continue;
            if(IS("me")) DzMsg(i, DZC_ORANGE, "* %s %s", s.name, arg);
            else if(IS("do")) DzMsg(i, DZC_ORANGE, "* %s (( %s ))", arg, s.name);
            else if(r > 50) DzMsg(i, DZC_WHITE, "%s shouts: %s!", s.name, arg);
            else DzMsg(i, DZC_GREY, "%s whispers: %s", s.name, arg);
        }
        return true;
    }
    if(IS("pm"))
    {
        int t = -1; char text[100] = ""; if(sscanf(arg, "%d %99[^\n]", &t, text) < 2 || t < 0 || t >= MP_MAX_PLAYERS || !m_Slots[t].used) { DzMsg(slot, DZC_GREY, "Usage: /pm <id> <text>"); return true; }
        DzMsg(t, DZC_YELLOW, "(PM from %s): %s", s.name, text); DzMsg(slot, DZC_YELLOW, "(PM to %s): %s", m_Slots[t].name, text);
        return true;
    }
    // parties
    if(IS("party") || IS("plist"))
    {
        if(z.party < 0) { DzMsg(slot, DZC_GREY, "You are solo. /pcreate to make a party (max 4), /pinvite <id> to invite."); DzEvent(slot, DZEV_UI, 4, 0, 0, nullptr, ""); return true; }
        DzParty& p = m_DzParty[z.party];
        DzMsg(slot, DZC_ORANGE, "[PARTY] %d/%d members:", p.n, MP_DZ_PARTY_MAX);
        for(int i = 0; i < p.n; ++i) DzMsg(slot, DZC_WHITE, "- %s (id %d)%s", m_Slots[p.members[i]].name, p.members[i], p.members[i] == p.leader ? " [leader]" : "");
        DzEvent(slot, DZEV_UI, 4, 0, 0, nullptr, "");
        return true;
    }
    if(IS("pcreate"))
    {
        if(z.party >= 0) { DzMsg(slot, DZC_GREY, "You are already in a party (/pleave)."); return true; }
        for(int i = 0; i < 64; ++i) if(!m_DzParty[i].used)
        {
            DzParty& p = m_DzParty[i]; p = DzParty(); p.used = true; p.leader = slot; p.col = 0;
            for(uint32_t c : g_DzPartyCols) { bool used = false; for(int q = 0; q < 64; ++q) if(m_DzParty[q].used && q != i && m_DzParty[q].col == c) used = true; if(!used) { p.col = c; break; } }
            if(!p.col) p.col = 0x40000000u | ((DzRand() & 0x7F7F7F) << 8) | 0x808000FFu; p.members[0] = slot; p.n = 1;
            z.party = i; DzSetColor(slot); DzSendMe(slot);
            DzMsg(slot, DZC_GREEN, "[PARTY] Party created - your colour is unique on the server. /pinvite <id> (max 4).");
            return true;
        }
        DzMsg(slot, DZC_RED, "No free party right now."); return true;
    }
    if(IS("pinvite"))
    {
        int t = atoi(arg);
        if(z.party < 0) { DzCommand(slot, "pcreate", ""); }
        if(z.party < 0) return true;
        DzParty& p = m_DzParty[z.party];
        if(p.n >= MP_DZ_PARTY_MAX) { DzMsg(slot, DZC_RED, "[PARTY] Full (max %d).", MP_DZ_PARTY_MAX); return true; }
        if(t < 0 || t >= MP_MAX_PLAYERS || !m_Slots[t].used || t == slot) { DzMsg(slot, DZC_GREY, "Usage: /pinvite <id>"); return true; }
        if(m_Dz[t].party >= 0) { DzMsg(slot, DZC_GREY, "%s is in another party.", m_Slots[t].name); return true; }
        m_nDzInvite[t] = slot;
        DzMsg(t, DZC_ORANGE, "[PARTY] %s invites you to the party. /paccept or /pdecline", s.name);
        DzMsg(slot, DZC_ORANGE, "[PARTY] Invite sent to %s.", m_Slots[t].name);
        return true;
    }
    if(IS("paccept"))
    {
        int from = m_nDzInvite[slot]; m_nDzInvite[slot] = -1;
        if(from < 0 || !m_Slots[from].used || m_Dz[from].party < 0) { DzMsg(slot, DZC_GREY, "No party invite."); return true; }
        DzParty& p = m_DzParty[m_Dz[from].party];
        if(p.n >= MP_DZ_PARTY_MAX) { DzMsg(slot, DZC_RED, "[PARTY] That party is full."); return true; }
        if(z.party >= 0) DzPartyLeave(slot, false);
        p.members[p.n++] = slot; z.party = m_Dz[from].party; DzSetColor(slot);
        for(int i = 0; i < p.n; ++i) { DzMsg(p.members[i], DZC_ORANGE, "[PARTY] %s joined (%d/%d).", s.name, p.n, MP_DZ_PARTY_MAX); DzSendMe(p.members[i]); }
        return true;
    }
    if(IS("pdecline")) { int from = m_nDzInvite[slot]; m_nDzInvite[slot] = -1; if(from >= 0 && m_Slots[from].used) DzMsg(from, DZC_GREY, "[PARTY] %s declined.", s.name); return true; }
    if(IS("pleave")) { if(z.party < 0) DzMsg(slot, DZC_GREY, "You are solo."); else { DzPartyLeave(slot, false); DzMsg(slot, DZC_ORANGE, "[PARTY] You left the party."); DzSendMe(slot); } return true; }
    if(IS("pkick"))
    {
        int t = atoi(arg);
        if(z.party < 0 || m_DzParty[z.party].leader != slot) { DzMsg(slot, DZC_GREY, "Only the party leader can kick."); return true; }
        if(t < 0 || t >= MP_MAX_PLAYERS || m_Dz[t].party != z.party || t == slot) { DzMsg(slot, DZC_GREY, "Usage: /pkick <id>"); return true; }
        DzPartyLeave(t, false); DzMsg(t, DZC_RED, "[PARTY] You were removed from the party."); DzSendMe(t);
        return true;
    }
    if(IS("p"))
    {
        if(z.party < 0) { DzMsg(slot, DZC_GREY, "You are solo (/pcreate)."); return true; }
        DzParty& p = m_DzParty[z.party];
        for(int i = 0; i < p.n; ++i) DzMsg(p.members[i], DZC_ORANGE, "[PARTY] %s: %s", s.name, arg);
        return true;
    }
    // ---- 3.59: MTA DayZ commands ----
    if(IS("skills"))
    {
        DzMsg(slot, DZC_YELLOW, "--- Skills: %d free point(s) (1 per level) - /skill <1-12> ---", DzSkillPts(slot));
        for(int k = 0; k < DZSK_COUNT; ++k) DzMsg(slot, z.skills[k] ? DZC_GREEN : DZC_WHITE, "%d. [%s] %s %d/%d - %s", k + 1, g_DzSkills[k].tree, g_DzSkills[k].name, z.skills[k], g_DzSkills[k].maxRank, g_DzSkills[k].desc);
        DzEvent(slot, DZEV_UI, 7, 2, 0, nullptr, ""); return true;
    }
    if(IS("skill")) { PktDzCmd c; memset(&c, 0, sizeof(c)); c.cmd = DZCMD_SKILL; c.a = (uint8_t)(atoi(arg) - 1); DzHandleCmd(slot, c); return true; }
    if(IS("achievements") || IS("ach"))
    {
        int n = 0; for(int k = 0; k < DZACH_COUNT; ++k) if(z.ach & (1u << k)) ++n;
        DzMsg(slot, DZC_YELLOW, "--- Achievements %d/%d ---", n, DZACH_COUNT);
        for(int k = 0; k < DZACH_COUNT; ++k) DzMsg(slot, (z.ach & (1u << k)) ? DZC_GREEN : DZC_GREY, "%s %s - %s", (z.ach & (1u << k)) ? "[X]" : "[ ]", g_DzAch[k].name, g_DzAch[k].desc);
        DzEvent(slot, DZEV_UI, 7, 1, 0, nullptr, ""); return true;
    }
    if(IS("journal") || IS("debug")) { DzEvent(slot, DZEV_UI, 7, 0, 0, nullptr, ""); return true; }
    if(IS("undress"))
    {
        if(!z.skin) { DzMsg(slot, DZC_GREY, "You wear no clothes already."); return true; }
        int item = 0; for(int i = 1; i < DZI_COUNT; ++i) if(g_DzItems[i].kind == DZK_CLOTHES && g_DzItems[i].val == z.skin) item = i;
        if(item && !DzGive(slot, item, 1)) { DzMsg(slot, DZC_RED, "No room for your clothes."); return true; }
        z.skin = 0; DzEvent(slot, DZEV_SKIN, 0, 0, 0, nullptr, ""); DzUnlock(slot, DZACH_COMMANDO); DzSendMe(slot); DzSendInv(slot); return true;
    }
    if(IS("weather")) { DzMsg(slot, DZC_GREY, "Weather: %s%s.", m_nDzWeather == 1 ? "sunny" : m_nDzWeather == 7 ? "cloudy" : "rainy", m_nDzRain ? " - it is raining (you get cold, zombies hear less)" : ""); return true; }
    if(IS("station") || IS("petrol"))
    {
        int st = DzNearStation(slot, 40.0f);
        if(st < 0) DzMsg(slot, DZC_GREY, "No petrol station here.");
        else DzMsg(slot, m_DzStation[st] > 0 ? DZC_GREEN : DZC_RED, "%s: %d%% of a tank left.", g_DzSites[st].name, m_DzStation[st]);
        return true;
    }
    if(IS("checkbody")) { DzCheckBody(slot, -1); return true; }
    if(IS("treat")) { DzMsg(slot, DZC_GREY, "Open the inventory, select a medicine and press TREAT next to the survivor (or: stand next to them)."); DzEvent(slot, DZEV_UI, 6, 0, 0, nullptr, "inv"); return true; }
    if(IS("vars"))
    {
        DzMsg(slot, DZC_YELLOW, "Difficulty %d | zombie damage %d | item respawn %d min | petrol max %d | rain %d", DzVar(DZV_DIFFICULTY), DzVar(DZV_ZOMBIEDAMAGE), DzVar(DZV_ITEMRESPAWN), DzVar(DZV_PETROLMAX), DzVar(DZV_RAIN));
        return true;
    }
    if(IS("dzvars") || IS("dzset") || IS("respawnvehicles") || IS("dzweather"))
    {
        if(!s.admin && !(s.mod && IS("respawnvehicles"))) { DzMsg(slot, DZC_RED, "[DayZAdmin] Admins only."); return true; }
        if(IS("dzvars")) { for(int v = 0; v < DZV_COUNT; ++v) DzMsg(slot, DZC_WHITE, "%s = %d (%d..%d) - %s", g_DzVarDefs[v].name, m_DzVar[v], g_DzVarDefs[v].min, g_DzVarDefs[v].max, g_DzVarDefs[v].help); return true; }
        if(IS("respawnvehicles")) { DzEvent(-1, DZEV_VEHRESPAWN, 0, 0, 0, nullptr, ""); DzMsg(-1, DZC_YELLOW, "[DayZAdmin] %s respawned the vehicles.", s.name); DzLog("ADMIN %s respawnvehicles", s.name); return true; }
        if(IS("dzweather")) { int w = atoi(arg); m_nDzWeather = w == 2 ? 7 : w == 3 ? 8 : 1; m_nDzRain = w == 3; m_nDzRainEnd = MP_NowMs() + 8 * 60000ull; DzSendWeather(-1); DzLog("ADMIN %s dzweather %d", s.name, w); return true; }
        char name[40] = ""; int val = 0;
        if(sscanf(arg, "%39s %d", name, &val) != 2) { DzMsg(slot, DZC_GREY, "Usage: /dzset <variable> <value> (/dzvars for the list)"); return true; }
        for(int v = 0; v < DZV_COUNT; ++v) if(!strcasecmp(name, g_DzVarDefs[v].name))
        {
            m_DzVar[v] = std::max(g_DzVarDefs[v].min, std::min(g_DzVarDefs[v].max, val)); DzSaveVars();
            DzMsg(-1, DZC_YELLOW, "[DayZAdmin] %s set %s to %d.", s.name, g_DzVarDefs[v].name, m_DzVar[v]); DzLog("ADMIN %s dzset %s %d", s.name, g_DzVarDefs[v].name, m_DzVar[v]);
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Dz[i].loaded) DzSendMe(i);
            return true;
        }
        DzMsg(slot, DZC_RED, "[DayZAdmin] Unknown variable %s (/dzvars).", name); return true;
    }
    // the client handles these (UI) - the server only answers when an old phone asks
    if(IS("inv") || IS("bag") || IS("map") || IS("gps") || IS("compass") || IS("hud") || IS("armor") || IS("weapon") || IS("fps") || IS("ping")
       || IS("eat") || IS("drink") || IS("heal") || IS("bandage") || IS("cure") || IS("painkillers") || IS("drop") || IS("pickup") || IS("refuel") || IS("fuel"))
    {
        DzEvent(slot, DZEV_UI, 6, 0, 0, nullptr, cmd);
        return true;
    }
    return false;
}

void MPServer::DzHandleCmd(int slot, const PktDzCmd& c)
{
    DzSurvivor& z = m_Dz[slot]; if(!z.loaded) return;
    const uint64_t now = MP_NowMs();
    switch(c.cmd)
    {
        case DZCMD_TEXT:
        {
            char cmd[32] = ""; const char* arg = c.text; while(*arg == '/') ++arg; int n = 0;
            while(*arg && *arg != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*arg++);
            cmd[n] = 0; while(*arg == ' ') ++arg;
            if(!DzCommand(slot, cmd, arg)) DzMsg(slot, DZC_GREY, "Unknown command /%s - /help", cmd);
            break;
        }
        case DZCMD_PLAY: DzSpawn(slot, c.a); break;
        case DZCMD_PICK:
        {
            int idx = c.val; if(idx < 0 || idx >= (int)m_DzLoot.size()) return;
            DzLootSpot& sp = m_DzLoot[idx];
            if(!sp.item || sp.respawnAt) { DzSendLoot(slot, idx); return; }
            float dx = sp.x - m_Slots[slot].pos[0], dy = sp.y - m_Slots[slot].pos[1];
            if(dx * dx + dy * dy > 8.0f * 8.0f) { DzMsg(slot, DZC_GREY, "Too far away."); return; }
            if(now - z.lastPick < 600) return;
            z.lastPick = now;
            const DzItem& d = DzItemInfo(sp.item);
            const int oldBag = z.bag;
            if(!DzGive(slot, sp.item, sp.count))
            {
                if(d.kind == DZK_BAG) DzMsg(slot, DZC_GREY, "Your bag is as good or better.");
                else if(d.kind == DZK_ARMOR) DzMsg(slot, DZC_GREY, "You already wear a %s.", d.name);
                else DzMsg(slot, DZC_RED, "Too heavy: %.2f / %.2f KG. Find a bigger bag.", DzKg(slot), DzMaxKg(slot));
                return;
            }
            if(d.kind == DZK_WEAPON) DzEvent(slot, DZEV_WEAPON, d.val, 0, d.ammo, nullptr, d.name);
            if(d.kind == DZK_AMMO)   DzEvent(slot, DZEV_WEAPON, d.ammo, 1, d.val * sp.count, nullptr, d.name);   // b = 1: only ammo (if you have that gun)
            if(d.kind == DZK_BAG && oldBag > 0)
            {   // the old bag stays on the ground
                DzLootSpot ob; ob.site = 255; ob.item = (uint8_t)(DZI_BAG1 + oldBag - 1); ob.count = 1; ob.x = m_Slots[slot].pos[0] + 0.8f; ob.y = m_Slots[slot].pos[1]; ob.respawnAt = 0;
                m_DzLoot.push_back(ob); DzSendLoot(-1, (int)m_DzLoot.size() - 1);
            }
            DzEvent(slot, DZEV_PICKED, sp.item, sp.count, idx, nullptr, d.name);
            if(d.kind == DZK_AMMO && z.skills[DZSK_AMMO] && DzRand() % 100 < 15u * z.skills[DZSK_AMMO])
            {   // 3.59 skill: Ammo Crafter
                DzGive(slot, sp.item, 1); DzEvent(slot, DZEV_WEAPON, d.ammo, 1, d.val, nullptr, d.name); DzMsg(slot, DZC_GREEN, "Ammo Crafter: +1 %s.", d.name);
            }
            if(sp.site != 255 && z.skills[DZSK_GOOD] && DzRand() % 100 < 10u * z.skills[DZSK_GOOD])
            {   // 3.59 skill: Good Eye - a second item from the same spot
                DzLootSpot ex = sp; DzRoll(ex); const DzItem& e = DzItemInfo(ex.item);
                if(DzGive(slot, ex.item, ex.count)) { DzMsg(slot, DZC_GREEN, "Good Eye: you also found %s.", e.name); if(e.kind == DZK_WEAPON) DzEvent(slot, DZEV_WEAPON, e.val, 0, e.ammo, nullptr, e.name); }
            }
            if(sp.site == 255) { sp.item = 0; sp.respawnAt = 0; }                                   // a drop / body: gone
            else
            {   // a loot spot: back after itemrespawntimer (+-5 min) at a new random place of the site (MTA: randomized loot spots)
                int m = DzVar(DZV_ITEMRESPAWN); sp.respawnAt = now + (uint64_t)std::max(1, m - 5 + (int)(DzRand() % 11)) * 60 * 1000;
                if(sp.site < DZ_SITES) { const DzSite& st = g_DzSites[sp.site]; float a = DzFrand() * 6.2831853f, r = st.r * (0.25f + 0.75f * DzFrand()); sp.x = st.x + cosf(a) * r; sp.y = st.y + sinf(a) * r; }
            }
            DzSendLoot(-1, idx);
            DzSendMe(slot); DzSendInv(slot); m_bDzDirty = true;
            break;
        }
        case DZCMD_USE: case DZCMD_DROP: case DZCMD_EQUIP: case DZCMD_FUEL:
        {
            int i = c.a; if(i < 0 || i >= z.nInv) return;
            DzInvSlot& is = z.inv[i]; const DzItem& d = DzItemInfo(is.item);
            bool remove1 = false;
            if(c.cmd == DZCMD_DROP)
            {
                DzLootSpot sp; sp.site = 255; sp.item = is.item; sp.count = (uint8_t)std::min<int>(255, DzStacks(is.item) ? is.count : 1);
                sp.x = m_Slots[slot].pos[0] + 0.9f; sp.y = m_Slots[slot].pos[1] + 0.3f; sp.respawnAt = 0;
                m_DzLoot.push_back(sp); DzSendLoot(-1, (int)m_DzLoot.size() - 1);
                if(d.kind == DZK_WEAPON) DzEvent(slot, DZEV_TAKEWEAPON, d.val, 0, 0, nullptr, d.name);
                if(d.kind == DZK_GPS) { bool other = false; for(int k = 0; k < z.nInv; ++k) if(k != i && DzItemInfo(z.inv[k].item).kind == DZK_GPS) other = true; z.gps = other; }
                memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv;
                DzMsg(slot, DZC_GREY, "Dropped: %s", d.name);
            }
            else if(c.cmd != DZCMD_FUEL && DzUseExtra(slot, i, c)) { DzSendMe(slot); DzSendInv(slot); m_bDzDirty = true; break; }
            else switch(d.kind)
            {
                case DZK_FOOD:  z.hunger = std::min(100, z.hunger + d.val); remove1 = true; DzMsg(slot, DZC_GREEN, "You ate %s (hunger %d%%).", d.name, z.hunger); DzEvent(slot, DZEV_HEAL, 0, 0, 300, nullptr, "eat"); break;
                case DZK_DRINK: z.thirst = std::min(100, z.thirst + d.val); remove1 = true; DzMsg(slot, DZC_GREEN, "You drank %s (thirst %d%%).", d.name, z.thirst); DzEvent(slot, DZEV_HEAL, 0, 0, 0, nullptr, "drink"); break;
                case DZK_MED:   z.blood = std::min(12000, z.blood + d.val); remove1 = true; DzEvent(slot, DZEV_HEAL, 0, 0, d.val, nullptr, d.name); DzMsg(slot, DZC_GREEN, "%s used (+%d blood).", d.name, d.val); break;
                case DZK_CURE:  z.infected = false; remove1 = true; DzMsg(slot, DZC_GREEN, "Antibiotics: the infection is gone."); break;
                case DZK_FUEL:  remove1 = true; DzEvent(slot, DZEV_FUEL, 0, 0, d.val * (100 + 20 * z.skills[DZSK_ANALYSIS]) / 100, nullptr, ""); break;
                case DZK_WEAPON: z.hands = is.item; DzEvent(slot, DZEV_WEAPON, d.val, 0, std::max<int>(1, is.count), nullptr, d.name); break;
                case DZK_AMMO:  DzEvent(slot, DZEV_WEAPON, d.ammo, 1, d.val, nullptr, d.name); if(--is.count == 0) { memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv; } break;
                case DZK_GPS:   z.gps = true; DzMsg(slot, DZC_GREEN, "GPS on: the map is unlocked."); break;
                case DZK_TRASH: DzMsg(slot, DZC_GREY, "Just trash. Drop it to save weight."); break;
                case DZK_REVIVE: DzMsg(slot, DZC_YELLOW, "%s revives a DOWNED party member: stand next to them and press REVIVE (+%d%% HP).", d.name, d.val); break;
                default: DzMsg(slot, DZC_GREY, "%s can't be used.", d.name); break;
            }
            bool wasFuel = d.kind == DZK_FUEL && c.cmd != DZCMD_DROP;
            if(remove1) { if(is.count > 1) is.count--; else { memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv; } }
            if(wasFuel) DzGive(slot, DZI_EMPTY_CAN_FUEL, 1);   // 3.59: the empty canister stays (fill it at a petrol station)
            DzSendMe(slot); DzSendInv(slot); m_bDzDirty = true;
            break;
        }
        case DZCMD_UNEQUIP:
        {   // armour slot a -> back to the inventory (if it fits)
            int sl = c.a; if(sl < 0 || sl >= 5 || !z.armor[sl]) return;
            int it = z.armor[sl]; z.armor[sl] = 0;
            if(z.nInv < DZ_INV_MAX) { z.inv[z.nInv].item = (uint8_t)it; z.inv[z.nInv].count = 1; ++z.nInv; }
            DzSendMe(slot); DzSendInv(slot);
            break;
        }
        case DZCMD_ZKILL:
            if(now - z.lastZk < 1200) return;   // max one zombie / 1.2 s (a phone can't fake a horde)
            z.lastZk = now; z.zkills++; z.points += 10; z.humanity += 5;
            if(c.a) z.headshots++;
            DzSendMe(slot);
            break;
        case DZCMD_HIT:
        {   // status from the phone: blood (health * 120), zombie bite -> infection chance
            int hp = c.val; if(hp < 0) hp = 0; if(hp > 176) hp = 176;
            if(c.b) z.fps = c.b;   // 3.58b: for the player list
            z.blood = std::min(12000, hp * 12000 / 100);
            if(c.a == 1 && !z.infected && DzRand() % 100 < 12) { z.infected = true; DzMsg(slot, DZC_RED, "A zombie bit you - you are INFECTED. Find Antibiotics."); }
            if(c.a == 1) DzCombat(slot, now);
            if(z.blood < 6000) DzUnlock(slot, DZACH_PULP);
            if(z.blood < 3000) z.hunted = true;
            break;
        }
        // ---- 3.59: MTA DayZ ----
        case DZCMD_STATUS:   // conditions + temperature from the phone (bleeding, pain, broken legs / arms, cold)
            if(now - z.lastStatus < 1500) return;
            z.lastStatus = now; z.cond = (uint16_t)((c.a | (c.b << 8)) & (DZCO_BLEEDING | DZCO_PAIN | DZCO_LEGS | DZCO_ARMS | DZCO_COLD));
            z.temp100 = std::max(3000, std::min(4200, (int)c.val));
            break;
        case DZCMD_SKILL:
        {
            int k = c.a; if(k < 0 || k >= DZSK_COUNT) { DzMsg(slot, DZC_GREY, "Usage: /skill <1-%d> (/skills)", DZSK_COUNT); return; }
            if(DzSkillPts(slot) <= 0) { DzMsg(slot, DZC_RED, "No free skill points - you get 1 every level."); return; }
            if(z.skills[k] >= g_DzSkills[k].maxRank) { DzMsg(slot, DZC_GREY, "%s is at the max rank.", g_DzSkills[k].name); return; }
            z.skills[k]++; DzMsg(slot, DZC_GREEN, "[%s] %s rank %d/%d.", g_DzSkills[k].tree, g_DzSkills[k].name, z.skills[k], g_DzSkills[k].maxRank);
            DzSendMe(slot); m_bDzDirty = true; break;
        }
        case DZCMD_STATION:
        {   // refuel the car at a petrol station: the station has limited fuel (maxPetrolFuelAmount)
            if(now - z.lastStation < 1500) return;
            z.lastStation = now;
            int st = DzNearStation(slot, 40.0f); int want = std::max(0, std::min(100, (int)c.val));
            if(st < 0) { DzEvent(slot, DZEV_STATION, 0, 0, 0, nullptr, "No petrol station here."); return; }
            int got = std::min(want, m_DzStation[st]); m_DzStation[st] -= got;
            char t[96]; snprintf(t, sizeof(t), got ? "%s: %d%% left" : "%s is EMPTY - try another station", g_DzSites[st].name, m_DzStation[st]);
            DzEvent(slot, DZEV_STATION, m_DzStation[st] > 0, 0, got, nullptr, t);
            break;
        }
        case DZCMD_CHECKBODY: DzCheckBody(slot, c.val); break;
        case DZCMD_TREAT: DzTreat(slot, c); break;
        case DZCMD_ACH: if(c.a == DZACH_HOUSE) DzUnlock(slot, DZACH_HOUSE); break;
        case DZCMD_LIST:
        {   // 3.58b: the TAB player list (pages of 60)
            if(now - z.lastList < 900) return;
            z.lastList = now;
            std::vector<DzListRow> rows;
            for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            {
                if(!m_Slots[i].used) continue;
                const DzSurvivor& o = m_Dz[i];
                DzListRow r; memset(&r, 0, sizeof(r)); r.id = (uint8_t)i;
                r.level = (uint8_t)std::min(99, DzLevel(o.points)); r.fps = o.fps; r.murders = (uint16_t)o.murders; r.zkills = (uint16_t)std::min(65535, o.zkills); r.aliveMin = o.aliveMin;
                r.flags = (o.alive ? 1 : 0) | (o.downed ? 2 : 0) | (o.humanity < 0 ? 4 : 0) | (z.party >= 0 && o.party == z.party ? 8 : 0);
                rows.push_back(r);
            }
            int pages = std::max(1, ((int)rows.size() + DZ_LIST_PAGE - 1) / DZ_LIST_PAGE);
            for(int pg = 0; pg < pages; ++pg)
            {
                PktDzList L; memset(&L, 0, sizeof(L)); L.type = PKT_DZ_LIST; L.page = (uint8_t)pg; L.pages = (uint8_t)pages;
                L.total = (uint16_t)rows.size(); L.max = (uint16_t)m_nMaxPlayers;
                int n = std::min<int>(DZ_LIST_PAGE, (int)rows.size() - pg * DZ_LIST_PAGE); L.n = (uint8_t)n;
                if(n > 0) memcpy(L.r, &rows[pg * DZ_LIST_PAGE], n * sizeof(DzListRow));
                SendTo(slot, &L, (int)offsetof(PktDzList, r) + n * (int)sizeof(DzListRow));
            }
            break;
        }
        case DZCMD_COMBAT:   // 3.58b: the phone fired a gun / got hurt -> ANTI-RELOG
            if(now - z.lastCombatCmd < 900 || !z.alive) return;
            z.lastCombatCmd = now; DzCombat(slot, now);
            break;
        case DZCMD_DOWN:
        {   // 3.58b: fake death - HP 5% or less: lying on the ground, can't move, waits for a party revive
            bool d = c.a != 0;
            if(d == z.downed || !z.alive) return;
            z.downed = d; z.downedAt = now;
            DzCombat(slot, now);
            DzEvent(-1, DZEV_DOWNED, slot, d ? 1 : 0, 0, nullptr, m_Slots[slot].name);
            if(d)
            {
                bool party = false; if(z.party >= 0) for(int k = 0; k < m_DzParty[z.party].n; ++k) if(m_DzParty[z.party].members[k] != slot) party = true;
                DzMsg(slot, DZC_RED, party ? "You are DOWN. Wait for your party to revive you (Morphine, Epinephrine or a Blood bag)." : "You are DOWN and you have no party - you will bleed out.");
            }
            break;
        }
        case DZCMD_REVIVE:
        {   // 3.58b: revive a downed party member with a medicine (+50% HP)
            int t = c.a;
            if(t < 0 || t >= MP_MAX_PLAYERS || t == slot || !m_Slots[t].used || !m_Dz[t].loaded) return;
            DzSurvivor& v = m_Dz[t];
            if(!v.downed) { DzMsg(slot, DZC_GREY, "%s is not down.", m_Slots[t].name); return; }
            if(z.party < 0 || z.party != v.party) { DzMsg(slot, DZC_RED, "You can only revive your party members."); return; }
            if(z.downed) return;
            float dx = m_Slots[t].pos[0] - m_Slots[slot].pos[0], dy = m_Slots[t].pos[1] - m_Slots[slot].pos[1], dz = m_Slots[t].pos[2] - m_Slots[slot].pos[2];
            if(dx * dx + dy * dy + dz * dz > 4.5f * 4.5f) { DzMsg(slot, DZC_GREY, "Get closer to %s.", m_Slots[t].name); return; }
            int mi = -1;
            for(int i = 0; i < z.nInv; ++i) if(DzItemInfo(z.inv[i].item).kind == DZK_REVIVE) { mi = i; break; }
            if(mi < 0) { DzMsg(slot, DZC_RED, "You need a medicine: Morphine, Epinephrine or a Blood bag."); return; }
            const DzItem& d = DzItemInfo(z.inv[mi].item);
            int hp = d.val;
            DzMsg(slot, DZC_GREEN, "You revived %s with %s.", m_Slots[t].name, d.name);
            DzMsg(t, DZC_GREEN, "%s revived you with %s (+%d%% HP).", m_Slots[slot].name, d.name, hp);
            if(z.inv[mi].count > 1) z.inv[mi].count--; else { memmove(&z.inv[mi], &z.inv[mi + 1], sizeof(DzInvSlot) * (z.nInv - mi - 1)); --z.nInv; }
            v.downed = false;
            v.blood = std::min(12000, v.blood + hp * 120);
            z.humanity += 150; z.points += 25; z.revives++;
            DzEvent(t, DZEV_REVIVED, slot, 0, hp, nullptr, m_Slots[slot].name);
            DzEvent(slot, DZEV_REVIVER, t, 0, 0, nullptr, m_Slots[t].name);
            DzEvent(-1, DZEV_DOWNED, t, 0, 0, nullptr, m_Slots[t].name);
            DzSendMe(slot); DzSendInv(slot); DzSendMe(t); m_bDzDirty = true;
            break;
        }
    }
}

void MPServer::DzTick(uint64_t now)
{
    if(!IsDayzServer()) return;
    auto chatAll = [&](uint32_t col, const char* text) { DzMsg(-1, col, "%s", text); };
    // 3.60: remember the last real outdoor spot of every survivor in the game (for "where you logged out")
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        DzSurvivor& z = m_Dz[i];
        if(!m_Slots[i].used || !z.loaded || !z.alive || !z.spawned || z.downed || !DzPosValid(m_Slots[i].pos)) continue;
        memcpy(z.lastPos, m_Slots[i].pos, sizeof(z.lastPos)); z.hasPos = true;
    }
    // chat loops ([INFO] 25:20, [SERVER] 35:20, [GUIDE] 10:20) - only grey / white / yellow / green / red / orange
    if(now >= m_nDzInfoAt)
    {
        m_nDzInfoAt = now + (25 * 60 + 20) * 1000ull;
        static const char* const k[] = {
            "[INFO] Hunger and thirst go down all the time - eat and drink before they reach 0.",
            "[INFO] A zombie bite can infect you (green blood drop + X). Antibiotics cure it.",
            "[INFO] Bags: Small backpack 30 KG, Assault pack 50 KG, Military rucksack 80 KG.",
            "[INFO] The map stays black until you find a GPS - military bases have them.",
            "[INFO] Killing survivors lowers your humanity: you become a Bandit.",
            "[INFO] Fog comes every 3 to 9 days. You see only 90 m - listen for footsteps.",
        };
        char t[160]; snprintf(t, sizeof(t), "%s", k[m_nDzInfoIdx++ % 6]); chatAll(DZC_YELLOW, t);
    }
    if(now >= m_nDzSrvAt)
    {
        m_nDzSrvAt = now + (35 * 60 + 20) * 1000ull;
        static const char* const k[] = {
            "[SERVER] Project Day Z Legacy runs 24/7 - your survivor is saved every minute.",
            "[SERVER] ANTI-RELOG: if you disconnect during a fight, your character will die.",
            "[SERVER] CLEO menus and libCheatMenu.so / libCheatMenu64.so are blocked here.",
            "[SERVER] Found a cheater? /report <id> <reason>. Staff: /admins.",
            "[SERVER] Parties are max 4 survivors and every party has its own colour.",
        };
        char t[160]; snprintf(t, sizeof(t), "%s", k[m_nDzSrvIdx++ % 5]); chatAll(DZC_WHITE, t);
    }
    if(now >= m_nDzGuideAt)
    {
        m_nDzGuideAt = now + (10 * 60 + 20) * 1000ull;
        static const char* const k[] = {
            "[GUIDE] You can find supplies (food, drinks, fuel) at every gas station.",
            "[GUIDE] You can find guns, ammo, armour and big bags at the military bases and camps (Area 69, Easter Basin, K.A.C.C.).",
            "[GUIDE] You can find food and sometimes guns in the houses of every town.",
            "[GUIDE] You can find fuel canisters in houses and gas stations (not many) - cars run out of fuel.",
            "[GUIDE] You can find a GPS at the military bases: it unlocks the map.",
        };
        char t[200]; snprintf(t, sizeof(t), "%s", k[m_nDzGuideIdx++ % 5]); chatAll(DZC_GREEN, t);
    }
    // fog events (every 3 to 9 game days, 10-15 minutes, a warning first)
    if(!m_bDzFog && now >= m_nDzFogAt)
    {
        m_bDzFog = true; m_nDzFogEnd = now + (uint64_t)(10 + DzRand() % 6) * 60 * 1000;
        chatAll(DZC_RED, "[WARNING] A thick fog is rolling in - you can see only 90 m. Stay together.");
        DzEvent(-1, DZEV_FOG, 1, 0, 90, nullptr, "FOG - visibility 90 m");
    }
    else if(m_bDzFog && now >= m_nDzFogEnd)
    {
        m_bDzFog = false; m_nDzFogAt = now + (uint64_t)(72 + DzRand() % 145) * 60 * 1000;
        chatAll(DZC_GREEN, "[INFO] The fog is lifting.");
        DzEvent(-1, DZEV_FOG, 0, 0, 0, nullptr, "");
    }
    // loot respawn + old drops
    for(int i = 0; i < (int)m_DzLoot.size(); ++i)
    {
        DzLootSpot& sp = m_DzLoot[i];
        if(sp.respawnAt && now >= sp.respawnAt) { DzRoll(sp); DzSendLoot(-1, i); }
    }
    // needs: hunger -1 / 45 s, thirst -1 / 30 s (alive and spawned)
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_Dz[i].loaded || !m_Dz[i].alive || !m_Dz[i].spawned) continue;
        DzSurvivor& z = m_Dz[i];
        if(!z.lastNeeds) z.lastNeeds = now;
        bool send = false;
        static thread_local uint64_t tH[MP_MAX_PLAYERS], tT[MP_MAX_PLAYERS];
        const int df = DzVar(DZV_DIFFICULTY);   // 3.59: hunger & thirst by difficulty (normal 45 / 30 s, veteran 38 / 25, hardcore 30 / 20)
        if(now - tH[i] > (uint64_t)(df >= 3 ? 30000 : df == 2 ? 38000 : 45000)) { tH[i] = now; if(z.hunger > 0) { z.hunger--; send = true; } }
        if(now - tT[i] > (uint64_t)(df >= 3 ? 20000 : df == 2 ? 25000 : 30000)) { tT[i] = now; if(z.thirst > 0) { z.thirst--; send = true; } }
        if(now - z.lastAchCheck > 4000)
        {
            z.lastAchCheck = now; DzCheckAch(i);
            // keycard areas (Area 69 / the carrier): visiting = achievement; without a Keycard the alarm calls zombies
            int in = -1; for(int a = 0; a < 2; ++a) { float dx = m_Slots[i].pos[0] - g_DzAreas[a].x, dy = m_Slots[i].pos[1] - g_DzAreas[a].y; if(dx * dx + dy * dy < g_DzAreas[a].r * g_DzAreas[a].r) in = a; }
            if(in >= 0)
            {
                DzUnlock(i, g_DzAreas[in].ach);
                bool kc = false; for(int k = 0; k < z.nInv; ++k) if(z.inv[k].item == DZI_KEYCARD) kc = true;
                if(!kc && (z.areaIdx != in || now - z.areaAt > 60000)) { z.areaAt = now; DzMsg(i, DZC_RED, "[ALARM] Restricted area: %s. Without a Keycard the alarm calls the zombies.", g_DzAreas[in].name); DzEvent(i, DZEV_SPECIAL, 101, 0, 4, nullptr, g_DzAreas[in].name); }
            }
            z.areaIdx = in;
        }
        if(now - z.lastMin > 60000) { z.lastMin = now; z.aliveMin++; z.points += 1; send = true; }
        static thread_local uint8_t lastCombat[MP_MAX_PLAYERS];
        uint8_t cb = (uint8_t)(z.combatUntil > now ? (z.combatUntil - now + 999) / 1000 : 0);
        if(cb != lastCombat[i] && (cb == 0 || cb == 15 || lastCombat[i] == 0)) send = true;
        lastCombat[i] = cb;
        if(send) DzSendMe(i);
    }
    // 3.59: weather - sunny / cloudy, occasional rain when it is cloudy (MTA DayZ)
    if(now >= m_nDzWeatherAt)
    {
        m_nDzWeatherAt = now + (uint64_t)(15 + DzRand() % 20) * 60 * 1000ull;
        m_nDzWeather = DzRand() % 100 < 55 ? 1 : 7; m_nDzRain = 0;
        if(m_nDzWeather == 7 && DzVar(DZV_RAIN) && DzRand() % 100 < 45) { m_nDzRain = 1; m_nDzWeather = 8; m_nDzRainEnd = now + (uint64_t)(5 + DzRand() % 6) * 60 * 1000ull; chatAll(DZC_GREY, "[WEATHER] It starts to rain - you get cold faster, zombies hear less."); }
        DzSendWeather(-1);
    }
    else if(m_nDzRain && now >= m_nDzRainEnd) { m_nDzRain = 0; m_nDzWeather = 7; DzSendWeather(-1); chatAll(DZC_GREY, "[WEATHER] The rain stops."); }
    // petrol stations refill (random 1..max) with the item respawn timer
    if(now >= m_nDzStationAt)
    {
        m_nDzStationAt = now + (uint64_t)DzVar(DZV_ITEMRESPAWN) * 60 * 1000ull;
        for(int s = 0; s < DZ_SITES && s < 128; ++s) if(m_DzStation[s] >= 0 && m_DzStation[s] < 10) m_DzStation[s] = (1 + (int)(DzRand() % DzVar(DZV_PETROLMAX))) * 10;
    }
    // old bodies (Check Body) - 10 minutes
    m_DzBodies.erase(std::remove_if(m_DzBodies.begin(), m_DzBodies.end(), [&](const DzBody& b) { return now - b.at > 600000; }), m_DzBodies.end());
    if(now >= m_nDzSaveAt) { m_nDzSaveAt = now + 60000; DzSave(); }
}

// ======================================================================================
// 3.59: the MTA DayZ systems
// ======================================================================================
static std::string DzSideFile(const char* name)
{   // next to the survivors file
    std::string p = s_szDzFile; size_t sl = p.find_last_of('/');
    return sl == std::string::npos ? std::string(name) : p.substr(0, sl + 1) + name;
}
void MPServer::DzLoadVars()
{
    FILE* f = fopen(DzSideFile("dayz_vars.txt").c_str(), "r"); if(!f) return;
    char line[160]; int n = 0;
    while(fgets(line, sizeof(line), f))
    {
        char name[64]; int v; if(sscanf(line, "%63s %d", name, &v) != 2) continue;
        for(int k = 0; k < DZV_COUNT; ++k) if(!strcasecmp(name, g_DzVarDefs[k].name)) { m_DzVar[k] = std::max(g_DzVarDefs[k].min, std::min(g_DzVarDefs[k].max, v)); ++n; }
    }
    fclose(f); Log("DAYZ: %d gameplay variables loaded", n);
}
void MPServer::DzSaveVars()
{
    FILE* f = fopen(DzSideFile("dayz_vars.txt").c_str(), "w"); if(!f) return;
    fprintf(f, "# Project Day Z Legacy gameplay variables (MTA DayZ names) - /dzset <name> <value> in game\n");
    for(int k = 0; k < DZV_COUNT; ++k) fprintf(f, "%s %d\n", g_DzVarDefs[k].name, m_DzVar[k]);
    fclose(f);
}
void MPServer::DzLog(const char* fmt, ...)
{   // dayz_log.txt: joins, quits, kills, deaths, anti-relog, adverts, admin commands
    FILE* f = fopen(DzSideFile("dayz_log.txt").c_str(), "a"); if(!f) return;
    time_t t = time(nullptr); struct tm tmv; gmtime_r(&t, &tmv); char ts[32]; strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    char msg[300]; va_list ap; va_start(ap, fmt); vsnprintf(msg, sizeof(msg), fmt, ap); va_end(ap);
    fprintf(f, "[%s UTC] %s\n", ts, msg); fclose(f);
}
int MPServer::DzSkillPts(int slot) const
{
    const DzSurvivor& z = m_Dz[slot]; int used = 0; for(int k = 0; k < DZSK_COUNT; ++k) used += z.skills[k];
    return DzLevel(z.points) - 1 - used;
}
void MPServer::DzUnlock(int slot, int a)
{
    DzSurvivor& z = m_Dz[slot]; if(a < 0 || a >= DZACH_COUNT || (z.ach & (1u << a))) return;
    z.ach |= 1u << a; m_bDzDirty = true;
    const DzAch& A = g_DzAch[a];
    DzMsg(-1, DZC_YELLOW, "[ACHIEVEMENT] %s unlocked \"%s\".", m_Slots[slot].name, A.name);
    if(A.rewardN && DzGive(slot, A.reward, A.rewardN)) { DzMsg(slot, DZC_GREEN, "Reward: %dx %s.", A.rewardN, DzItemInfo(A.reward).name); const DzItem& d = DzItemInfo(A.reward); if(d.kind == DZK_WEAPON) DzEvent(slot, DZEV_WEAPON, d.val, 0, d.ammo, nullptr, d.name); }
    DzEvent(slot, DZEV_ACH, a, 0, 0, nullptr, A.name);
    DzSendMe(slot); DzSendInv(slot);
}
void MPServer::DzCheckAch(int slot)
{
    const DzSurvivor& z = m_Dz[slot];
    if(z.zkills >= 10) DzUnlock(slot, DZACH_TASTE1); if(z.zkills >= 100) DzUnlock(slot, DZACH_TASTE2); if(z.zkills >= 500) DzUnlock(slot, DZACH_TASTE3);
    if(z.aliveMin >= 60) DzUnlock(slot, DZACH_FITTEST1); if(z.aliveMin >= 1440) DzUnlock(slot, DZACH_FITTEST2);
    if(z.headshots >= 10) DzUnlock(slot, DZACH_ODDS1); if(z.headshots >= 100) DzUnlock(slot, DZACH_ODDS2);
    if(z.murders >= 1) DzUnlock(slot, DZACH_DARK1); if(z.murders >= 10) DzUnlock(slot, DZACH_DARK2);
    if(z.bkills >= 1) DzUnlock(slot, DZACH_MONSTER1); if(z.bkills >= 10) DzUnlock(slot, DZACH_MONSTER2); if(z.bkills >= 50) DzUnlock(slot, DZACH_MONSTER3);
    int cans = 0; for(int k = 0; k < z.nInv; ++k) if(z.inv[k].item == DZI_TRASH_CAN) cans += z.inv[k].count;
    if(cans >= 5) DzUnlock(slot, DZACH_LITTER);
    if(z.confirmed >= 5) DzUnlock(slot, DZACH_HEADHUNTER);
    if(z.revives >= 5) DzUnlock(slot, DZACH_MEDIC);
    if(z.hunted && z.blood >= 9000 && z.alive) DzUnlock(slot, DZACH_HUNTED);
}
void MPServer::DzSendWeather(int slot) { DzEvent(slot, DZEV_WEATHER, m_nDzWeather, m_nDzRain, 0, nullptr, ""); }
int MPServer::DzNearStation(int slot, float maxDist) const
{
    int best = -1; float bd = maxDist * maxDist;
    for(int s = 0; s < DZ_SITES && s < 128; ++s)
    {
        if(m_DzStation[s] < 0) continue;
        float dx = m_Slots[slot].pos[0] - g_DzSites[s].x, dy = m_Slots[slot].pos[1] - g_DzSites[s].y, d = dx * dx + dy * dy;
        if(d < bd) { bd = d; best = s; }
    }
    return best;
}
void MPServer::DzRemove1(int slot, int i)
{
    DzSurvivor& z = m_Dz[slot]; if(i < 0 || i >= z.nInv) return;
    if(z.inv[i].count > 1 && DzStacks(z.inv[i].item)) z.inv[i].count--; else { memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv; }
}
// item effects from MTA DayZ (true = handled)
bool MPServer::DzUseExtra(int slot, int i, const PktDzCmd& c)
{
    DzSurvivor& z = m_Dz[slot]; const int item = z.inv[i].item; const DzItem& d = DzItemInfo(item);
    const int prof = 100 + 20 * z.skills[DZSK_PROF];
    auto hasTool = [&]() { for(int k = 0; k < z.nInv; ++k) if(z.inv[k].item == DZI_TOOLBOX) return true; return false; };
    switch(d.kind)
    {
        case DZK_MED:
        {   // bandage: stops the bleeding; painkillers: the pain; medkit: both (+ blood, with Proficiency)
            int blood = d.val * prof / 100;
            uint16_t cure = item == DZI_BANDAGE ? DZCO_BLEEDING : item == DZI_PAINKILLERS ? DZCO_PAIN : (DZCO_BLEEDING | DZCO_PAIN);
            z.blood = std::min(12000, z.blood + blood); z.cond &= ~cure;
            DzEvent(slot, DZEV_HEAL, 0, 0, blood, nullptr, d.name); DzEvent(slot, DZEV_CURE, cure & 0xFF, cure >> 8, 0, nullptr, d.name);
            DzMsg(slot, DZC_GREEN, "%s used (+%d blood%s%s).", d.name, blood, (cure & DZCO_BLEEDING) ? ", bleeding stopped" : "", (cure & DZCO_PAIN) ? ", pain gone" : "");
            DzRemove1(slot, i); return true;
        }
        case DZK_REVIVE:
            if(item == DZI_MORPHINE && !z.downed)
            {   // morphine: fixes broken legs / fractured arms
                z.cond &= ~(DZCO_LEGS | DZCO_ARMS); DzEvent(slot, DZEV_CURE, DZCO_LEGS | DZCO_ARMS, 0, 0, nullptr, d.name);
                DzMsg(slot, DZC_GREEN, "Morphine: your bones are fixed."); DzRemove1(slot, i); return true;
            }
            if(item == DZI_BLOODBAG) { DzMsg(slot, DZC_YELLOW, "You can't give yourself a transfusion - a survivor has to TREAT you (+%d blood) or revive you when DOWN.", 4000 * prof / 100); return true; }
            return false;
        case DZK_SPECIAL:
            switch(d.val)
            {
                case DZSP_HEATPACK: z.cond &= ~DZCO_COLD; z.temp100 = std::min(3800, z.temp100 + 300); DzEvent(slot, DZEV_CURE, DZCO_COLD, 0, 300, nullptr, d.name); DzMsg(slot, DZC_GREEN, "Heat pack: you are warm again."); DzRemove1(slot, i); return true;
                case DZSP_TIRE: DzEvent(slot, DZEV_SPECIAL, DZSP_TIRE, 0, 0, nullptr, d.name); DzRemove1(slot, i); return true;
                case DZSP_ENGINE: case DZSP_SCRAP:
                    if(!hasTool()) { DzMsg(slot, DZC_RED, "You need a Toolbox to fit %s.", d.name); return true; }
                    DzEvent(slot, DZEV_SPECIAL, d.val, 0, (d.val == DZSP_ENGINE ? 400 : 200) * (100 + 20 * z.skills[DZSK_DUCT]) / 100, nullptr, d.name); DzRemove1(slot, i); return true;
                case DZSP_EMPTYCAN:
                {
                    int st = DzNearStation(slot, 25.0f);
                    if(c.b == 1) { DzRemove1(slot, i); DzGive(slot, DZI_FUELCAN, 1); DzMsg(slot, DZC_GREEN, "You siphoned fuel from the vehicle into the canister."); return true; }
                    if(st < 0) { DzMsg(slot, DZC_GREY, "Fill it at a petrol station (or stand next to a car with fuel and press SIPHON)."); return true; }
                    if(m_DzStation[st] < 30) { DzMsg(slot, DZC_RED, "%s has not enough fuel (%d%%).", g_DzSites[st].name, m_DzStation[st]); return true; }
                    m_DzStation[st] -= 30; DzRemove1(slot, i); DzGive(slot, DZI_FUELCAN, 1); DzMsg(slot, DZC_GREEN, "Canister filled at %s (%d%% left).", g_DzSites[st].name, m_DzStation[st]); return true;
                }
                case DZSP_KEYCARD: DzMsg(slot, DZC_YELLOW, "Keycard: keep it in your inventory - the alarms of Area 69 and the carrier stay silent."); return true;
                case DZSP_BINOCULARS: case DZSP_RANGEFINDER: DzEvent(slot, DZEV_SPECIAL, d.val, 0, 0, nullptr, d.name); return true;
            }
            return false;
        case DZK_CLOTHES:
        {   // the old clothes / skin system: wear them, your old clothes go back in the bag
            int old = z.skin; int skin = d.val;
            DzRemove1(slot, i);
            if(old) for(int k = 1; k < DZI_COUNT; ++k) if(g_DzItems[k].kind == DZK_CLOTHES && g_DzItems[k].val == old) { DzGive(slot, k, 1); break; }
            z.skin = skin; DzEvent(slot, DZEV_SKIN, 0, 0, skin, nullptr, d.name);
            DzMsg(slot, DZC_GREEN, "You put on the %s.", d.name); return true;
        }
    }
    return false;
}
void MPServer::DzTreat(int slot, const PktDzCmd& c)
{   // treat another survivor (bandage, painkillers, medkit, antibiotics, morphine, blood bag, heat pack)
    DzSurvivor& z = m_Dz[slot]; const uint64_t now = MP_NowMs();
    int t = c.a, i = c.b;
    if(now - z.lastTreat < 1500) return;
    if(t < 0 || t >= MP_MAX_PLAYERS || t == slot || !m_Slots[t].used || !m_Dz[t].loaded || !m_Dz[t].alive) return;
    if(i < 0 || i >= z.nInv) return;
    float dx = m_Slots[t].pos[0] - m_Slots[slot].pos[0], dy = m_Slots[t].pos[1] - m_Slots[slot].pos[1];
    if(dx * dx + dy * dy > 4.0f * 4.0f) { DzMsg(slot, DZC_GREY, "Get closer to %s.", m_Slots[t].name); return; }
    z.lastTreat = now;
    DzSurvivor& v = m_Dz[t]; const int item = z.inv[i].item; const DzItem& d = DzItemInfo(item); const int prof = 100 + 20 * z.skills[DZSK_PROF];
    uint16_t cure = 0; int blood = 0;
    if(d.kind == DZK_MED) { blood = d.val * prof / 100; cure = item == DZI_BANDAGE ? DZCO_BLEEDING : item == DZI_PAINKILLERS ? DZCO_PAIN : (DZCO_BLEEDING | DZCO_PAIN); }
    else if(d.kind == DZK_CURE) { v.infected = false; }
    else if(item == DZI_MORPHINE) cure = DZCO_LEGS | DZCO_ARMS;
    else if(item == DZI_BLOODBAG) blood = 4000 * prof / 100;
    else if(item == DZI_HEATPACK) cure = DZCO_COLD;
    else { DzMsg(slot, DZC_GREY, "%s can't treat anybody.", d.name); return; }
    v.cond &= ~cure; if(blood) { v.blood = std::min(12000, v.blood + blood); DzEvent(t, DZEV_HEAL, 0, 0, blood, nullptr, d.name); }
    if(cure) DzEvent(t, DZEV_CURE, cure & 0xFF, cure >> 8, item == DZI_HEATPACK ? 300 : 0, nullptr, d.name);
    DzRemove1(slot, i);
    z.humanity += 40; z.points += 5;
    DzMsg(slot, DZC_GREEN, "You treated %s with %s (+40 humanity).", m_Slots[t].name, d.name);
    DzMsg(t, DZC_GREEN, "%s treated you with %s.", m_Slots[slot].name, d.name);
    DzSendMe(slot); DzSendInv(slot); DzSendMe(t); m_bDzDirty = true;
}
void MPServer::DzCheckBody(int slot, int id)
{   // "Check Body": the victim, the weapon and when - the killer's kill is confirmed (player kills only)
    float best = 3.5f * 3.5f; DzBody* b = nullptr;
    for(auto& x : m_DzBodies)
    {
        float dx = x.x - m_Slots[slot].pos[0], dy = x.y - m_Slots[slot].pos[1], d = dx * dx + dy * dy;
        if((id < 0 || x.id == id) && d < best) { best = d; b = &x; }
    }
    if(!b) { DzMsg(slot, DZC_GREY, "There is no body here."); return; }
    int mins = (int)((MP_NowMs() - b->at) / 60000);
    int wi = DzWeaponItem(b->weapon);
    DzMsg(slot, DZC_WHITE, "Body of %s - killed with %s, %d minute(s) ago.", b->victim, wi ? DzItemInfo(wi).name : "a weapon", mins);
    if(b->killer == slot && !b->checked)
    {
        DzSurvivor& k = m_Dz[slot]; b->checked = true;
        if(b->bandit) k.bkills++; else k.murders++;
        k.confirmed++;
        DzMsg(slot, DZC_GREEN, "Kill confirmed (%s). Confirmed kills: %d.", b->bandit ? "bandit" : "murder", k.confirmed);
        DzEvent(-1, DZEV_BODY, 1, 0, b->id, nullptr, "");
        DzSendMe(slot); m_bDzDirty = true;
    }
}
