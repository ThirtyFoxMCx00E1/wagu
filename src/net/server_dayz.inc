// 3.58: Project Day Z Remastered (gamemode "Proper DAYZ") - the server side.
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
    if(!IsDayzServer()) return;
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
        {   static const uint8_t k[] = { DZI_SODA, DZI_WATER, DZI_BURGER, DZI_PIZZA, DZI_BEANS, DZI_FUELCAN, DZI_SODA, DZI_WATER, DZI_BEER, DZI_TOOLBOX, DZI_MAP, DZI_BANDAGE };
            it = k[DzRand() % sizeof(k)]; break; }
        case DZS_MILITARY:
        {   static const uint8_t k[] = { DZI_AK47, DZI_M4, DZI_MP5, DZI_SNIPER, DZI_SPAS, DZI_DEAGLE, DZI_AMMO_RIFLE, DZI_AMMO_RIFLE, DZI_AMMO_SMG,
                                         DZI_AMMO_SNIPER, DZI_AMMO_SHELLS, DZI_AMMO_PISTOL, DZI_HELMET, DZI_CHEST, DZI_VEST, DZI_GLOVES, DZI_BOOTS,
                                         DZI_BAG2, DZI_BAG3, DZI_GPS, DZI_MEDKIT, DZI_ANTIBIOTICS, DZI_RIFLE, DZI_KATANA };
            it = k[DzRand() % sizeof(k)];
            if(r < 2) it = DZI_AK_GOLD;   // rare
            break; }
        case DZS_RUINS:
        {   static const uint8_t k[] = { DZI_TRASH_BOX, DZI_TRASH_BOTTLE, DZI_AMMO_PISTOL, DZI_BANDAGE, DZI_COLT, DZI_TEC9, DZI_BEANS, DZI_TOOLBOX, DZI_BAG1, DZI_PAINKILLERS };
            it = k[DzRand() % sizeof(k)]; break; }
        default:   // houses: food, trash and sometimes a gun
            if(r < 38)      { static const uint8_t k[] = { DZI_BEANS, DZI_BURGER, DZI_PIZZA, DZI_SODA, DZI_WATER, DZI_BEER }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 70) { static const uint8_t k[] = { DZI_TRASH_CAN, DZI_TRASH_BOTTLE, DZI_TRASH_BOX }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 80) { static const uint8_t k[] = { DZI_BANDAGE, DZI_PAINKILLERS, DZI_MEDKIT, DZI_ANTIBIOTICS }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 87) { static const uint8_t k[] = { DZI_KNIFE, DZI_COLT, DZI_SHOTGUN, DZI_UZI, DZI_RIFLE }; it = k[DzRand() % sizeof(k)]; }
            else if(r < 93) { static const uint8_t k[] = { DZI_AMMO_PISTOL, DZI_AMMO_SHELLS, DZI_AMMO_SMG }; it = k[DzRand() % sizeof(k)]; }
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
    for(int i = 0; i < z.nInv; ++i) kg += DzItemInfo(z.inv[i].item).kg * (DzItemInfo(z.inv[i].item).kind == DZK_AMMO || DzItemInfo(z.inv[i].item).kind == DZK_FOOD || DzItemInfo(z.inv[i].item).kind == DZK_DRINK || DzItemInfo(z.inv[i].item).kind == DZK_MED || DzItemInfo(z.inv[i].item).kind == DZK_CURE || DzItemInfo(z.inv[i].item).kind == DZK_TRASH ? z.inv[i].count : 1);
    return kg;
}
float MPServer::DzMaxKg(int slot) const
{
    const DzSurvivor& z = m_Dz[slot];
    return 10.0f + (z.bag == 1 ? 20.0f : z.bag == 2 ? 40.0f : z.bag == 3 ? 70.0f : 0.0f);   // pockets 10 kg + the bag
}
static bool DzStacks(int item) { int k = DzItemInfo(item).kind; return k == DZK_AMMO || k == DZK_FOOD || k == DZK_DRINK || k == DZK_MED || k == DZK_CURE || k == DZK_TRASH || k == DZK_FUEL; }
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
void MPServer::DzSave()
{
    // the online survivors first (their live state)
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_Dz[i].loaded) continue;
        char kb[40]; const char* key = RpKey(i, kb, sizeof(kb)); if(!key) continue;
        const DzSurvivor& z = m_Dz[i]; char buf[1300];
        int o = snprintf(buf, sizeof(buf), "%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%d|%u|%d|%d|%d,%d,%d,%d,%d|%.1f|%.1f|%.1f|",
                         z.alive, z.blood, z.hunger, z.thirst, z.infected, z.humanity, z.murders, z.zkills, z.bkills, z.headshots, z.points, z.aliveMin,
                         z.bag, z.gps, z.armor[0], z.armor[1], z.armor[2], z.armor[3], z.armor[4], m_Slots[i].pos[0], m_Slots[i].pos[1], m_Slots[i].pos[2]);
        for(int k = 0; k < z.nInv && o < (int)sizeof(buf) - 16; ++k) o += snprintf(buf + o, sizeof(buf) - o, "%d:%d,", z.inv[k].item, z.inv[k].count);
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
    z = DzSurvivor(); z.loaded = true; z.party = party;
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
        if(!z.alive || z.blood <= 0) { DzFresh(slot); z.alive = false; }
        else { m_Slots[slot].pos[0] = px; m_Slots[slot].pos[1] = py; m_Slots[slot].pos[2] = pz; }
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
    if(fresh)
    {
        DzMsg(slot, DZC_YELLOW, "Welcome to Project Day Z Remastered (Proper DAYZ). Survive. Trust nobody.");
        DzMsg(slot, DZC_GREY, "Press F2 to open the control panel - /help for all commands, /rules for the rules.");
    }
    if(m_Dz[slot].loaded) { DzSendMe(slot); DzSendInv(slot); }
}
void MPServer::DzOnLeave(int slot)
{
    if(!IsDayzServer()) return;
    DzSurvivor& z = m_Dz[slot];
    if(z.loaded && z.alive && z.combatUntil > MP_NowMs())
    {   // ANTI-RELOG: disconnecting in a fight kills your survivor
        z.alive = false; z.blood = 0;
        DzMsg(-1, DZC_RED, "[ANTI-RELOG] %s disconnected during combat and died.", m_Slots[slot].name);
        Log("DAYZ: anti-relog death: %s", m_Slots[slot].name);
    }
    if(z.loaded) { DzSave(); }
    if(z.party >= 0) DzPartyLeave(slot, false);
    z = DzSurvivor();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_nDzInvite[i] == slot) m_nDzInvite[i] = -1;
}

void MPServer::DzSpawn(int slot)
{
    DzSurvivor& z = m_Dz[slot];
    float p[4];
    char name[64];
    if(z.alive && (fabsf(m_Slots[slot].pos[0]) > 1.0f || fabsf(m_Slots[slot].pos[1]) > 1.0f))
    { p[0] = m_Slots[slot].pos[0]; p[1] = m_Slots[slot].pos[1]; p[2] = m_Slots[slot].pos[2]; p[3] = 0; MP_CopyStr(name, "where you logged out", sizeof(name)); }
    else
    {
        const DzSpawnPt& s = g_DzSpawns[DzRand() % 15];
        p[0] = s.x; p[1] = s.y; p[2] = s.z; p[3] = s.h; MP_CopyStr(name, s.name, sizeof(name));
        if(!z.alive) { z.alive = true; z.blood = 12000; z.hunger = z.thirst = 100; z.infected = false; z.aliveMin = 0; }
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
    DzSendMe(slot); DzSendInv(slot);
    DzMsg(slot, DZC_GREEN, "You woke up at %s. Find food, water, a GPS and a better bag.", name);
}

void MPServer::DzOnDeath(int slot, int killer, int weapon)
{
    if(!IsDayzServer()) return;
    DzSurvivor& z = m_Dz[slot]; if(!z.loaded) return;
    // the body: the inventory falls on the ground (max 10 items), stays 10 minutes
    const uint64_t now = MP_NowMs();
    int dropped = 0;
    for(int i = 0; i < z.nInv && dropped < 10; ++i)
    {
        DzLootSpot sp; sp.site = 255; sp.item = z.inv[i].item; sp.count = (uint8_t)std::min<int>(255, z.inv[i].count);
        float a = dropped * 0.63f; sp.x = m_Slots[slot].pos[0] + cosf(a) * 1.2f; sp.y = m_Slots[slot].pos[1] + sinf(a) * 1.2f;
        sp.respawnAt = 0; m_DzLoot.push_back(sp); DzSendLoot(-1, (int)m_DzLoot.size() - 1); ++dropped;
    }
    if(killer >= 0 && killer < MP_MAX_PLAYERS && killer != slot && m_Slots[killer].used && m_Dz[killer].loaded)
    {
        DzSurvivor& k = m_Dz[killer];
        if(z.humanity < 0) { k.bkills++; k.humanity += 500; k.points += 50; DzMsg(killer, DZC_GREEN, "Bandit killed: +500 humanity, +50 points."); }
        else               { k.murders++; k.humanity -= 1000; DzMsg(killer, DZC_RED, "You murdered a survivor: -1000 humanity."); }
        if(weapon == 33 || weapon == 34) { k.headshots++; }
        DzSendMe(killer);
    }
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
    m_Dz[from].combatUntil = m_Dz[to].combatUntil = now + 15000;   // ANTI-RELOG for both
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
    if(text[0] == '/')
    {
        char cmd[32] = ""; const char* arg = text + 1; int n = 0;
        while(*arg && *arg != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*arg++);
        cmd[n] = 0; while(*arg == ' ') ++arg;
        return DzCommand(slot, cmd, arg);
    }
    return false;   // normal chat: broadcast with the party colour as the name colour
}

static const char* const kDzRules[] = {
    "1. No cheats, CLEO, mod menus (libCheatMenu.so / libCheatMenu64.so are blocked) or bug abuse.",
    "2. Disconnecting during combat (ANTI-RELOG) = your survivor dies.",
    "3. Parties: max 4 survivors. Don't pretend to be another party (colours are unique).",
    "4. No spawn killing at the 15 spawn points.",
    "5. Respect everyone in the chat - no insults, spam or advertising.",
    "6. Don't impersonate staff (admin, moderator) or the Broker bot.",
    "7. Kick = wait 4:29 to rejoin. Fly / teleport hacks: 3 kicks = permanent ban.",
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
        DzMsg(slot, DZC_WHITE, "/pm <id> <text> /me /do /shout /whisper /time /fog /spawns /kill /rules /credits /admins /report");
        DzMsg(slot, DZC_GREY, "Press F2 (or PANEL) for the control panel. Most commands open their window too.");
        DzEvent(slot, DZEV_UI, 1, 0, 0, nullptr, "");
        return true;
    }
    if(IS("rules")) { for(const char* r : kDzRules) DzMsg(slot, DZC_WHITE, "%s", r); DzEvent(slot, DZEV_UI, 2, 0, 0, nullptr, ""); return true; }
    if(IS("credits")) { DzMsg(slot, DZC_YELLOW, "Project Day Z Remastered - based on Project DayZ SA-MP, made for MP by ThirtyFox."); DzEvent(slot, DZEV_UI, 5, 0, 0, nullptr, ""); return true; }
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
        case DZCMD_PLAY: DzSpawn(slot); break;
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
            if(sp.site == 255) { sp.item = 0; sp.respawnAt = 0; }                                   // a drop / body: gone
            else sp.respawnAt = now + (uint64_t)(25 + DzRand() % 11) * 60 * 1000;                   // a loot spot: back in 25-35 min
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
            else switch(d.kind)
            {
                case DZK_FOOD:  z.hunger = std::min(100, z.hunger + d.val); remove1 = true; DzMsg(slot, DZC_GREEN, "You ate %s (hunger %d%%).", d.name, z.hunger); DzEvent(slot, DZEV_HEAL, 0, 0, 300, nullptr, "eat"); break;
                case DZK_DRINK: z.thirst = std::min(100, z.thirst + d.val); remove1 = true; DzMsg(slot, DZC_GREEN, "You drank %s (thirst %d%%).", d.name, z.thirst); DzEvent(slot, DZEV_HEAL, 0, 0, 0, nullptr, "drink"); break;
                case DZK_MED:   z.blood = std::min(12000, z.blood + d.val); remove1 = true; DzEvent(slot, DZEV_HEAL, 0, 0, d.val, nullptr, d.name); DzMsg(slot, DZC_GREEN, "%s used (+%d blood).", d.name, d.val); break;
                case DZK_CURE:  z.infected = false; remove1 = true; DzMsg(slot, DZC_GREEN, "Antibiotics: the infection is gone."); break;
                case DZK_FUEL:  remove1 = true; DzEvent(slot, DZEV_FUEL, 0, 0, d.val, nullptr, ""); break;
                case DZK_WEAPON: z.hands = is.item; DzEvent(slot, DZEV_WEAPON, d.val, 0, std::max<int>(1, is.count), nullptr, d.name); break;
                case DZK_AMMO:  DzEvent(slot, DZEV_WEAPON, d.ammo, 1, d.val, nullptr, d.name); if(--is.count == 0) { memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv; } break;
                case DZK_GPS:   z.gps = true; DzMsg(slot, DZC_GREEN, "GPS on: the map is unlocked."); break;
                case DZK_TRASH: DzMsg(slot, DZC_GREY, "Just trash. Drop it to save weight."); break;
                default: DzMsg(slot, DZC_GREY, "%s can't be used.", d.name); break;
            }
            if(remove1) { if(is.count > 1) is.count--; else { memmove(&z.inv[i], &z.inv[i + 1], sizeof(DzInvSlot) * (z.nInv - i - 1)); --z.nInv; } }
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
            z.blood = std::min(12000, hp * 12000 / 100);
            if(c.a == 1 && !z.infected && DzRand() % 100 < 12) { z.infected = true; DzMsg(slot, DZC_RED, "A zombie bit you - you are INFECTED. Find Antibiotics."); }
            if(c.a == 1) z.combatUntil = std::max(z.combatUntil, now + 15000);
            break;
        }
    }
}

void MPServer::DzTick(uint64_t now)
{
    if(!IsDayzServer()) return;
    auto chatAll = [&](uint32_t col, const char* text) { DzMsg(-1, col, "%s", text); };
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
            "[SERVER] Project Day Z Remastered runs 24/7 - your survivor is saved every minute.",
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
        if(now - tH[i] > 45000) { tH[i] = now; if(z.hunger > 0) { z.hunger--; send = true; } }
        if(now - tT[i] > 30000) { tT[i] = now; if(z.thirst > 0) { z.thirst--; send = true; } }
        if(now - z.lastMin > 60000) { z.lastMin = now; z.aliveMin++; z.points += 1; send = true; }
        static thread_local uint8_t lastCombat[MP_MAX_PLAYERS];
        uint8_t cb = (uint8_t)(z.combatUntil > now ? (z.combatUntil - now + 999) / 1000 : 0);
        if(cb != lastCombat[i] && (cb == 0 || cb == 15 || lastCombat[i] == 0)) send = true;
        lastCombat[i] = cb;
        if(send) DzSendMe(i);
    }
    if(now >= m_nDzSaveAt) { m_nDzSaveAt = now + 60000; DzSave(); }
}
