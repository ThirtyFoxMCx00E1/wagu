// MP 3.25 - Mega Vil Futuristic: the Roleplay server referee (included by server.cpp)
// Server authoritative: money, bank, job pay, shop prices, houses, phone numbers, calls, jail.
// The phone only asks (PKT_RP_CMD); every answer comes back as PKT_RP_ME / PKT_RP_EVENT.
// Peaceful city: PKT_DAMAGE / explosions / fire only pass from an on-duty Police Officer or
// Military Soldier who is not a new citizen, and never inside a greenzone.

static uint32_t RpHash(const char* s) { uint32_t h = 2166136261u; for(; s && *s; ++s) { h ^= (uint8_t)tolower(*s); h *= 16777619u; } return h; }
static float RpDist2D(const float* a, const float* b) { float dx = a[0] - b[0], dy = a[1] - b[1]; return sqrtf(dx * dx + dy * dy); }
static const char* RpMoney(int32_t v, char* buf, size_t n)
{   // 1234567 -> "$1,234,567"
    char raw[24]; snprintf(raw, sizeof(raw), "%d", v < 0 ? -v : v);
    size_t len = strlen(raw), o = 0; if(n < 2) return "";
    if(v < 0 && o + 1 < n) buf[o++] = '-';
    if(o + 1 < n) buf[o++] = '$';
    for(size_t i = 0; i < len && o + 1 < n; ++i) { buf[o++] = raw[i]; size_t left = len - 1 - i; if(left && left % 3 == 0 && o + 1 < n) buf[o++] = ','; }
    buf[o] = 0; return buf;
}

bool MPServer::IsRpServer() const { return MP_IsRpServerName(m_szName) || m_nPort == MP_RP_PORT; }
void MPServer::SetRpFile(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szRpFile, path && *path ? path : "rp_citizens.txt", sizeof(m_szRpFile));
    if(IsRpServer()) RpLoad();
}

void MPServer::RpReset()
{
    memset(m_Rp, 0, sizeof(m_Rp));
    memset(m_RpDrops, 0, sizeof(m_RpDrops));   // 3.30
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) { m_Rp[i].callWith = m_Rp[i].ringFrom = m_Rp[i].askFrom = m_Rp[i].fineFrom = MP_INVALID_ID; m_Rp[i].house = m_Rp[i].pet = 255; }
    m_nRpLastSend = m_nRpLastPlayers = m_nRpLastSave = 0;
    m_nRpNextEvent = MP_NowMs() + 4 * 60 * 1000; m_nRpEvEnd = 0; m_nRpEvKind = RPEV_NONE; m_nRpEvState = 0;
}

const char* MPServer::RpKey(int slot, char* buf, size_t n)
{
    const Slot& s = m_Slots[slot];
    if(!s.authed || !s.account[0]) return nullptr;
    size_t o = 0;
    for(size_t i = 0; s.account[i] && o + 1 < n; ++i) { char ch = (char)tolower((unsigned char)s.account[i]); if(ch != ' ' && ch != '|' && ch != '\n') buf[o++] = ch; }
    buf[o] = 0;
    return o ? buf : nullptr;
}

// file: one citizen per line, then the house owners
// C key|cash|bank|xp|number|job|house|pet|hunger|thirst|playMin|pets|items|skin|tasks|t|v0,v1,..
// H index|key
void MPServer::RpLoad()
{
    m_RpSaved.clear(); for(auto& h : m_RpHouseOwner) h.clear();
    FILE* f = fopen(m_szRpFile, "r"); if(!f) { Log("RP: no %s yet (a new city)", m_szRpFile); return; }
    char line[512]; int n = 0;
    while(fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if(line[0] == 'H' && line[1] == ' ') { int idx = atoi(line + 2); const char* k = strchr(line + 2, '|'); if(k && idx >= 0 && idx < RP_HOUSE_COUNT) m_RpHouseOwner[idx] = k + 1; continue; }
        if(line[0] == 'P' && line[1] == ' ') { m_nRpLottoPot = atoi(line + 2); continue; }
        if(line[0] == 'M' && line[1] == ' ')
        {   // 3.29: M bond|stocks|coin (market prices)
            char* q = line + 2;
            for(int k = 0; k < RPN_COUNT && q && *q; ++k) { float v = strtof(q, &q); if(v >= 50.0f && v <= 60000.0f) m_fRpMarket[k] = v; if(*q == '|') ++q; else break; }
            continue;
        }
        if(line[0] == 'X' && line[1] == ' ')
        {   // 3.29: X key|units0|units1|units2|spouseKey|spouse name
            char* p = line + 2; char* bar = strchr(p, '|'); if(!bar) continue; *bar = 0;
            auto it = m_RpSaved.find(p); if(it == m_RpSaved.end()) continue;
            char* q = bar + 1;
            for(int k = 0; k < RPN_COUNT; ++k) { long long u = strtoll(q, &q, 10); it->second.invU[k] = u > 0 ? u : 0; if(*q == '|') ++q; }
            char* b2 = strchr(q, '|');
            if(b2) { *b2 = 0; it->second.spouseKey = q; it->second.spouse = b2 + 1; }
            continue;
        }
        if(line[0] == 'I' && line[1] == ' ')
        {   // 3.26: I key|id:count,id:count,... (the bag, after the C line of that citizen)
            char* p = line + 2; char* bar = strchr(p, '|'); if(!bar) continue; *bar = 0;
            auto it = m_RpSaved.find(p); if(it == m_RpSaved.end()) continue;
            RpInvSlot* inv = it->second.inv; memset(inv, 0, sizeof(it->second.inv));
            char* q = bar + 1; int k = 0;
            while(q && *q && k < RP_INV_SLOTS)
            {
                int id = (int)strtol(q, &q, 10); int cnt = 0;
                if(*q == ':') { ++q; cnt = (int)strtol(q, &q, 10); }
                if(id > 0 && id < RPX_COUNT && cnt > 0) { inv[k].id = (uint8_t)id; inv[k].count = (uint16_t)(cnt > RP_INV_STACK ? RP_INV_STACK : cnt); }
                ++k; if(*q == ',') ++q; else break;
            }
            continue;
        }
        if(line[0] != 'C' || line[1] != ' ') continue;
        char* p = line + 2; char* bar = strchr(p, '|'); if(!bar) continue; *bar = 0;
        std::string key = p; RpSaved r;
        unsigned v[16] = {}; int got = 0; char* q = bar + 1;
        for(; got < 16 && q && *q; ++got) { v[got] = (unsigned)strtol(q, &q, 10); if(*q == '|') ++q; else break; }
        if(got < 15) continue;
        r.cash = (int32_t)v[0]; r.bank = (int32_t)v[1]; r.xp = (int32_t)v[2]; r.number = (int32_t)v[3]; r.job = (uint8_t)(v[4] < RPJ_COUNT ? v[4] : 0);
        r.house = (uint8_t)v[5]; r.pet = (uint8_t)v[6]; r.hunger = (uint8_t)(v[7] > 100 ? 100 : v[7]); r.thirst = (uint8_t)(v[8] > 100 ? 100 : v[8]);
        r.playMin = v[9]; r.pets = v[10]; r.items = v[11]; r.skin = (uint16_t)v[12]; r.tasks = (uint16_t)v[13]; r.t = v[14];
        if(q && *q == '|') ++q;
        r.nveh = 0;
        while(q && *q && r.nveh < RP_VEH_SLOTS) { int m = (int)strtol(q, &q, 10); if(m >= 400 && m < 612) r.veh[r.nveh++] = (uint16_t)m; if(*q == ',') ++q; else break; }
        m_RpSaved[key] = r; ++n;
    }
    fclose(f);
    Log("RP: %d citizens loaded from %s", n, m_szRpFile);
}

void MPServer::RpSave()
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded) RpStore(i);
    std::string tmp = std::string(m_szRpFile) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
    fprintf(f, "# Mega Vil Futuristic citizens (MP %s). C key|cash|bank|xp|number|job|house|pet|hunger|thirst|min|pets|items|skin|tasks|t|vehicles\n", MP_VERSION_STR);
    fprintf(f, "P %d\n", m_nRpLottoPot);
    fprintf(f, "M %.3f|%.3f|%.3f\n", m_fRpMarket[0], m_fRpMarket[1], m_fRpMarket[2]);   // 3.29
    for(auto& kv : m_RpSaved)
    {
        const RpSaved& r = kv.second;
        fprintf(f, "C %s|%d|%d|%d|%d|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|", kv.first.c_str(), r.cash, r.bank, r.xp, r.number, r.job, r.house, r.pet, r.hunger, r.thirst,
                r.playMin, r.pets, r.items, r.skin, r.tasks, r.t);
        for(int v = 0; v < r.nveh; ++v) fprintf(f, v ? ",%u" : "%u", r.veh[v]);
        fputc('\n', f);
        bool any = false; for(int i = 0; i < RP_INV_SLOTS; ++i) if(r.inv[i].id) any = true;
        if(any)
        {   // 3.26: the bag, slot by slot (0:0 = an empty slot, so the order stays)
            fprintf(f, "I %s|", kv.first.c_str());
            for(int i = 0; i < RP_INV_SLOTS; ++i) fprintf(f, i ? ",%u:%u" : "%u:%u", r.inv[i].id, r.inv[i].id ? r.inv[i].count : 0);
            fputc('\n', f);
        }
        if(r.invU[0] || r.invU[1] || r.invU[2] || !r.spouseKey.empty())   // 3.29
            fprintf(f, "X %s|%lld|%lld|%lld|%s|%s\n", kv.first.c_str(), (long long)r.invU[0], (long long)r.invU[1], (long long)r.invU[2], r.spouseKey.c_str(), r.spouse.c_str());
    }
    for(int h = 0; h < RP_HOUSE_COUNT; ++h) if(!m_RpHouseOwner[h].empty()) fprintf(f, "H %d|%s\n", h, m_RpHouseOwner[h].c_str());
    fclose(f);
    rename(tmp.c_str(), m_szRpFile);
    m_bRpDirty = false;
}

void MPServer::RpStore(int slot)
{
    RpSrvPlayer& m = m_Rp[slot]; if(!m.loaded) return;
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb)); if(!key) return;
    RpSaved& r = m_RpSaved[key];
    r.cash = m.cash; r.bank = m.bank; r.xp = m.xp; r.number = m.number; r.job = m.job; r.house = m.house; r.pet = m.pet;
    r.hunger = m.hunger; r.thirst = m.thirst; r.playMin = m.playMin; r.pets = m.pets; r.items = m.items; r.skin = m.skin; r.tasks = m.tasks;
    r.nveh = m.nveh; memcpy(r.veh, m.veh, sizeof(r.veh)); r.t = m.statT;
    memcpy(r.inv, m.inv, sizeof(r.inv));   // 3.26
    memcpy(r.invU, m.invU, sizeof(r.invU)); r.spouseKey = m.spouseKey; r.spouse = m.spouse;   // 3.29
}

// ---- signed citizen card (HMAC-SHA256 with the MW3 card key, other payload) -------------------
static void RpCardMac(const PktRpCard& c, uint8_t mac[16])
{
    uint8_t k[32], ipad[64], opad[64], inner[32], full[32];
    Mw3CardKey(k);
    for(int i = 0; i < 32; ++i) k[i] ^= (uint8_t)(0x52 + i * 7);   // "R": its own key, a MW3 card never passes as a citizen card
    memset(ipad, 0x36, 64); memset(opad, 0x5C, 64);
    for(int i = 0; i < 32; ++i) { ipad[i] ^= k[i]; opad[i] ^= k[i]; }
    mbedtls_sha256_context h;
    mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    mbedtls_sha256_update(&h, ipad, 64); mbedtls_sha256_update(&h, (const unsigned char*)&c, offsetof(PktRpCard, mac));
    mbedtls_sha256_finish(&h, inner); mbedtls_sha256_free(&h);
    mbedtls_sha256_init(&h); mbedtls_sha256_starts(&h, 0);
    mbedtls_sha256_update(&h, opad, 64); mbedtls_sha256_update(&h, inner, 32);
    mbedtls_sha256_finish(&h, full); mbedtls_sha256_free(&h);
    memcpy(mac, full, 16);
}

void MPServer::RpSendCard(int slot)
{
    RpSrvPlayer& m = m_Rp[slot]; if(!m.loaded) return;
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb)); if(!key) return;
    if(m.cash != m.lastCash || m.bank != m.lastBank || m.job != m.lastJob)
    {
        m.statT = (uint32_t)time(nullptr); m.lastCash = m.cash; m.lastBank = m.bank; m.lastJob = m.job; m_bRpDirty = true;
    }
    if(m.statT == m.cardT) return;
    PktRpCard c; memset(&c, 0, sizeof(c));
    c.type = PKT_RP_CARD; c.ver = RP_CARD_VER; c.job = m.job; c.level = m.level; c.t = m.statT;
    MP_CopyStr(c.key, key, sizeof(c.key));
    c.cash = m.cash; c.bank = m.bank; c.xp = m.xp; c.number = m.number; c.playMin = m.playMin; c.pets = m.pets; c.items = m.items;
    memcpy(c.veh, m.veh, sizeof(c.veh)); c.skin = m.skin; c.tasks = m.tasks; c.house = m.house; c.pet = m.pet; c.hunger = m.hunger; c.thirst = m.thirst;
    RpCardMac(c, c.mac);
    SendTo(slot, &c, sizeof(c));
    m.cardT = m.statT;
}

void MPServer::RpOnCard(int slot, const PktRpCard& in)
{
    if(!IsRpServer() || !m_Slots[slot].authed) return;
    PktRpCard c = in; c.key[sizeof(c.key) - 1] = 0;
    if(c.ver != RP_CARD_VER) return;
    uint8_t mac[16]; RpCardMac(c, mac);
    if(memcmp(mac, c.mac, 16)) { Log("RP: %s sent a citizen card with a bad signature (ignored)", m_Slots[slot].name); return; }
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb));
    if(!key || strcmp(key, c.key)) return;
    RpSrvPlayer& m = m_Rp[slot];
    if(!m.loaded || c.t <= m.statT) return;   // the host already knows newer numbers
    m.cash = c.cash; m.bank = c.bank; m.xp = c.xp; m.level = (uint8_t)RpLevelFromXp(m.xp); m.playMin = c.playMin; m.pets = c.pets; m.items = c.items;
    m.job = c.job < RPJ_COUNT ? c.job : 0; m.skin = c.skin; m.tasks = c.tasks; m.pet = c.pet; m.hunger = c.hunger > 100 ? 100 : c.hunger; m.thirst = c.thirst > 100 ? 100 : c.thirst;
    m.nveh = 0; for(int v = 0; v < RP_VEH_SLOTS; ++v) if(c.veh[v] >= 400 && c.veh[v] < 612) m.veh[m.nveh++] = c.veh[v];
    if(c.number > 0) m.number = c.number;
    // the house: back if still free, else the money (another host sold it meanwhile)
    if(c.house < RP_HOUSE_COUNT)
    {
        if(m_RpHouseOwner[c.house].empty() || m_RpHouseOwner[c.house] == key) { m_RpHouseOwner[c.house] = key; m.house = c.house; }
        else { m.bank += g_RpHouses[c.house].price; RpMsg(slot, RP_YELLOW, "[MEGA VIL] Your house was sold while you were away: %d back to your bank.", g_RpHouses[c.house].price); }
    }
    m.statT = m.cardT = c.t; m.lastCash = m.cash; m.lastBank = m.bank; m.lastJob = m.job;
    RpStore(slot); m_bRpDirty = true;
    Log("RP: restored %s from their citizen card", c.key);
    RpMsg(slot, RP_SKY, "[MEGA VIL] Welcome back - your money, job and things were restored from your phone.");
    RpSendMe(slot);
}

// ---- messages ----------------------------------------------------------------------------------
void MPServer::RpMsg(int slot, uint32_t rgba, const char* fmt, ...)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = rgba;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    if(slot >= 0) SendTo(slot, &m, sizeof(m)); else Broadcast(&m, sizeof(m));
}
void MPServer::RpAll(uint32_t rgba, const char* fmt, ...)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = rgba;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    Broadcast(&m, sizeof(m));
}
void MPServer::RpNear(int slot, float range, uint32_t rgba, const char* text, bool fade)
{
    const Slot& s = m_Slots[slot];
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const Slot& o = m_Slots[i];
        if(!o.used || !o.authed) continue;
        float d = 0.0f;
        if(i != slot)
        {
            if(!s.hasPos || !o.hasPos) continue;
            float dx = s.pos[0] - o.pos[0], dy = s.pos[1] - o.pos[1], dz = s.pos[2] - o.pos[2];
            d = sqrtf(dx * dx + dy * dy + dz * dz);
            if(d > range) continue;
        }
        uint32_t c = rgba;
        if(fade && d > range * 0.5f)
        {   // further away = greyer, like RP servers
            float k = 1.0f - (d - range * 0.5f) / (range * 0.5f) * 0.55f;
            uint32_t r = (uint32_t)(((rgba >> 24) & 255) * k), g = (uint32_t)(((rgba >> 16) & 255) * k), b = (uint32_t)(((rgba >> 8) & 255) * k);
            c = (r << 24) | (g << 16) | (b << 8) | 0xFF;
        }
        PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = c;
        MP_CopyStr(m.text, text, sizeof(m.text));
        SendTo(i, &m, sizeof(m));
    }
}
void MPServer::RpEv(int slot, uint8_t ev, uint8_t a, uint8_t b, uint8_t who, int32_t val, const float* f, const char* text)
{
    PktRpEvent e; memset(&e, 0, sizeof(e));
    e.type = PKT_RP_EVENT; e.ev = ev; e.a = a; e.b = b; e.who = who; e.val = val;
    if(f) memcpy(e.fval, f, sizeof(e.fval));
    if(text) MP_CopyStr(e.text, text, sizeof(e.text));
    if(slot >= 0) SendTo(slot, &e, sizeof(e)); else Broadcast(&e, sizeof(e));
}

bool MPServer::RpPay(int slot, int32_t amount, const char* why)
{
    RpSrvPlayer& m = m_Rp[slot];
    if(amount < 0 && m.cash < -amount) return false;
    m.cash += amount;
    if(m.cash > 2000000000) m.cash = 2000000000;
    RpEv(slot, RPV_PAY, 0, 0, MP_INVALID_ID, amount, nullptr, why);
    m_bRpDirty = true;
    return true;
}
void MPServer::RpXp(int slot, int xp)
{
    RpSrvPlayer& m = m_Rp[slot];
    int before = m.level; m.xp += xp; m.level = (uint8_t)RpLevelFromXp(m.xp);
    if(m.level > before) { RpMsg(slot, RP_SKY, "[MEGA VIL] Level up! You are now level %d.", m.level); RpEv(slot, RPV_TEXT, 0, 0, MP_INVALID_ID, (int32_t)RP_SKY, nullptr, "LEVEL UP"); }
    m_bRpDirty = true;
}

// ---- state packets -----------------------------------------------------------------------------
void MPServer::RpSendMe(int slot)
{
    const RpSrvPlayer& m = m_Rp[slot]; if(!m.loaded) return;
    uint64_t now = MP_NowMs();
    PktRpMe p; memset(&p, 0, sizeof(p));
    p.type = PKT_RP_ME; p.job = m.job; p.duty = m.duty; p.level = m.level; p.hunger = m.hunger; p.thirst = m.thirst; p.house = m.house; p.pet = m.pet;
    p.cuffed = m.cuffed; p.wanted = m.wanted; p.jobOn = m.jobOn; p.nveh = m.nveh; p.jailS = m.jailS;
    p.newbieS = (uint16_t)(m.playMin < RP_NEWBIE_MIN ? (RP_NEWBIE_MIN - m.playMin) * 60 - (uint32_t)((now - m.minuteAt) / 1000 % 60) : 0);
    p.cash = m.cash; p.bank = m.bank; p.xp = m.xp; p.number = m.number; p.playMin = m.playMin;
    p.paydayS = (uint16_t)(m.paydayAt > now ? (m.paydayAt - now) / 1000 : 0); p.tasks = m.tasks;
    memcpy(p.target, m.target, sizeof(p.target)); memcpy(p.veh, m.veh, sizeof(p.veh));
    p.pets = m.pets; p.items = m.items; p.skin = m.skin; p.lotto = m.lotto;
    for(int k = 0; k < RPN_COUNT; ++k) { long long w = (long long)(m.invU[k] * (double)m_fRpMarket[k] / 1000.0); p.invest[k] = (int32_t)(w > 2000000000LL ? 2000000000LL : w); }   // 3.29
    p.belt = m.belt; p.married = m.spouseKey[0] ? 1 : 0; MP_CopyStr(p.spouse, m.spouse, sizeof(p.spouse));
    SendTo(slot, &p, sizeof(p));
}
// 3.26: the bag
void MPServer::RpSendInv(int slot)
{
    const RpSrvPlayer& m = m_Rp[slot]; if(!m.loaded) return;
    PktRpInv p; memset(&p, 0, sizeof(p));
    p.type = PKT_RP_INV; p.n = RP_INV_SLOTS; p.weight = (uint16_t)RpInvWeight(m.inv); p.maxWeight = RP_INV_MAX_W;
    memcpy(p.slot, m.inv, sizeof(p.slot));
    SendTo(slot, &p, sizeof(p));
}
// 3.30: items on the ground, to one citizen or to everyone (-1)
void MPServer::RpSendDrops(int slot)
{
    PktRpDrops p; memset(&p, 0, sizeof(p));
    p.type = PKT_RP_DROPS;
    uint64_t now = MP_NowMs();
    for(int i = 0; i < RP_DROPS_MAX; ++i)
    {
        const RpSrvDrop& d = m_RpDrops[i]; if(!d.id) continue;
        RpDropRow& r = p.row[p.n++];
        r.uid = d.uid; r.id = d.id; r.count = d.count; r.ttlS = (uint16_t)(d.until > now ? (d.until - now) / 1000 : 0);
        memcpy(r.pos, d.pos, sizeof(r.pos));
    }
    int len = (int)(offsetof(PktRpDrops, row) + p.n * sizeof(RpDropRow));
    if(slot >= 0) { if(m_Rp[slot].loaded) SendTo(slot, &p, len); return; }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Rp[i].loaded) SendTo(i, &p, len);
}
int MPServer::RpInvGive(int slot, uint8_t id, int n, const char* why)
{
    RpSrvPlayer& m = m_Rp[slot]; if(!m.loaded || !id || id >= RPX_COUNT || n <= 0) return 0;
    int got = RpInvAdd(m.inv, id, n);
    if(got > 0)
    {
        RpEv(slot, RPV_GOT, id, 0, MP_INVALID_ID, got, nullptr, why ? why : "Received");
        m_bRpDirty = true;
        RpSendInv(slot);
    }
    if(got < n) RpMsg(slot, RP_RED, "[BAG] Your bag is full (%d.%d / %d.0 kg, %d slots): %d x %s did not fit.", RpInvWeight(m.inv) / 10, RpInvWeight(m.inv) % 10, RP_INV_MAX_W / 10, RP_INV_SLOTS, n - got, g_RpInv[id].name);
    return got;
}
void MPServer::RpSendPlayers(int slot)
{
    RpRow rows[MP_MAX_PLAYERS]; int n = 0;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_Rp[i].loaded) continue;
        const RpSrvPlayer& m = m_Rp[i];
        RpRow& r = rows[n++]; r.id = (uint8_t)i; r.job = m.job; r.level = m.level; r.number = m.number;
        r.flags = (m.duty ? 1 : 0) | (m.cuffed ? 2 : 0) | (m.wanted ? 4 : 0) | (m.playMin < RP_NEWBIE_MIN ? 8 : 0) | (m.callWith != MP_INVALID_ID ? 16 : 0);
    }
    int pages = (n + RP_ROWS_WIRE - 1) / RP_ROWS_WIRE; if(pages < 1) pages = 1;
    for(int pg = 0; pg < pages; ++pg)
    {
        PktRpPlayers p; memset(&p, 0, sizeof(p)); p.type = PKT_RP_PLAYERS; p.page = (uint8_t)pg; p.pages = (uint8_t)pages;
        int from = pg * RP_ROWS_WIRE, cnt = n - from; if(cnt > RP_ROWS_WIRE) cnt = RP_ROWS_WIRE; if(cnt < 0) cnt = 0;
        p.n = (uint8_t)cnt; memcpy(p.rows, rows + from, sizeof(RpRow) * cnt);
        int len = (int)offsetof(PktRpPlayers, rows) + (int)sizeof(RpRow) * cnt;
        if(slot >= 0) SendTo(slot, &p, len); else Broadcast(&p, len);
    }
}
void MPServer::RpSendState(int slot)
{
    uint64_t now = MP_NowMs();
    PktRpState p; memset(&p, 0, sizeof(p)); p.type = PKT_RP_STATE;
    p.evKind = m_nRpEvKind; p.evState = m_nRpEvState;
    p.evLeftS = (uint16_t)(m_nRpEvKind && m_nRpEvEnd > now ? (m_nRpEvEnd - now) / 1000 : 0);
    p.nextEvS = (uint16_t)(m_nRpNextEvent > now ? (m_nRpNextEvent - now) / 1000 : 0);
    memcpy(p.evPos, m_fRpEvPos, sizeof(p.evPos)); p.lottoPot = m_nRpLottoPot;
    for(int h = 0; h < RP_HOUSE_COUNT; ++h) p.houseOwned[h] = m_RpHouseOwner[h].empty() ? 0 : 1;
    if(m_nRpEvKind) MP_CopyStr(p.evText, g_RpEventNames[m_nRpEvKind], sizeof(p.evText));
    for(int k = 0; k < RPN_COUNT; ++k) { p.market[k] = (uint16_t)(m_fRpMarket[k] > 65000.0f ? 65000 : (int)m_fRpMarket[k]); p.marketChg[k] = m_nRpMarketChg[k]; }   // 3.29
    if(slot >= 0) SendTo(slot, &p, sizeof(p)); else Broadcast(&p, sizeof(p));
}

// ---- join / leave ------------------------------------------------------------------------------
void MPServer::RpOnJoin(int slot, bool fresh)
{
    if(!IsRpServer()) return;
    if(fresh)
    {
        memset(&m_Rp[slot], 0, sizeof(m_Rp[slot]));
        RpSrvPlayer& m = m_Rp[slot];
        m.callWith = m.ringFrom = m.askFrom = m.fineFrom = MP_INVALID_ID; m.house = m.pet = 255;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) { if(m_Rp[i].callWith == slot) m_Rp[i].callWith = MP_INVALID_ID; if(m_Rp[i].ringFrom == slot) m_Rp[i].ringFrom = MP_INVALID_ID; }
    }
    RpSendState(slot);
    RpSendPlayers(slot);
    if(m_Slots[slot].authed && !m_Rp[slot].loaded) RpOnAuthed(slot);
}

void MPServer::RpOnAuthed(int slot)
{
    if(!IsRpServer() || m_Rp[slot].loaded) return;
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb)); if(!key) return;
    RpSrvPlayer& m = m_Rp[slot];
    uint64_t now = MP_NowMs();
    auto it = m_RpSaved.find(key);
    bool isNew = it == m_RpSaved.end();
    if(isNew)
    {
        m.cash = RP_START_CASH; m.bank = RP_START_BANK; m.hunger = m.thirst = 100; m.house = m.pet = 255;
        // a phone number that stays the same on every host: from the account, unique in this city
        int32_t num = 100000 + (int32_t)(RpHash(key) % 899999u);
        for(int tries = 0; tries < 2000; ++tries)
        {
            bool used = false; for(auto& kv : m_RpSaved) if(kv.second.number == num) { used = true; break; }
            if(!used) break; num = num >= 999999 ? 100000 : num + 1;
        }
        m.number = num; m.statT = (uint32_t)time(nullptr);
    }
    else
    {
        const RpSaved& r = it->second;
        m.cash = r.cash; m.bank = r.bank; m.xp = r.xp; m.number = r.number; m.job = r.job; m.house = r.house; m.pet = r.pet;
        m.hunger = r.hunger; m.thirst = r.thirst; m.playMin = r.playMin; m.pets = r.pets; m.items = r.items; m.skin = r.skin; m.tasks = r.tasks;
        m.nveh = r.nveh; memcpy(m.veh, r.veh, sizeof(m.veh)); m.statT = r.t;
        memcpy(m.inv, r.inv, sizeof(m.inv));   // 3.26
        memcpy(m.invU, r.invU, sizeof(m.invU)); MP_CopyStr(m.spouseKey, r.spouseKey.c_str(), sizeof(m.spouseKey)); MP_CopyStr(m.spouse, r.spouse.c_str(), sizeof(m.spouse));   // 3.29
        if(m.house < RP_HOUSE_COUNT && m_RpHouseOwner[m.house] != key) m.house = 255;
        if(m.hunger < 25) m.hunger = 25; if(m.thirst < 25) m.thirst = 25;   // you ate while you were away
    }
    m.level = (uint8_t)RpLevelFromXp(m.xp);
    m.loaded = true; m.minuteAt = now; m.needAt = now; m.paydayAt = now + RP_PAYDAY_S * 1000ull;
    m.lastCash = m.cash; m.lastBank = m.bank; m.lastJob = m.job; m.cardT = 0;
    RpStore(slot);
    if(isNew)
    {
        RpMsg(slot, RP_SKY, "[MEGA VIL] Welcome to Mega Vil Futuristic, new citizen! You got $%d cash and $%d in the bank.", RP_START_CASH, RP_START_BANK);
        RpMsg(slot, RP_WHITE, "Your phone number is %d. Open the phone (menu - Phone, or /phone). Get a job at City Hall or in the Jobs app.", m.number);
        RpMsg(slot, RP_YELLOW, "Mega Vil is peaceful: only on-duty police and soldiers can use force. /help for everything.");
    }
    else RpMsg(slot, RP_SKY, "[MEGA VIL] Welcome back, %s. Level %d %s - phone %d.", m_Slots[slot].name, m.level, g_RpJobs[m.job].name, m.number);
    // 3.26: every citizen gets the starter backpack once (new ones, and the ones from before the bag)
    if(!(m.items & RPIT_STARTER) && RpInvGive(slot, RPX_BACKPACK, 1, "Starter pack"))
    {
        m.items |= RPIT_STARTER; m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpMsg(slot, RP_YELLOW, "[INFO] You got a Starter Pack Backpack. Open it: PANEL > Bag > select the Backpack > USE (or /inv).");
    }
    RpSendMe(slot);
    RpSendInv(slot);
    RpSendDrops(slot);   // 3.30
    RpSendCard(slot);
    RpSendPlayers(-1);
}

void MPServer::RpEndCall(int slot, const char* why)
{
    RpSrvPlayer& m = m_Rp[slot];
    int other = m.callWith != MP_INVALID_ID ? m.callWith : (m.ringFrom != MP_INVALID_ID ? m.ringFrom : -1);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Rp[i].ringFrom == slot) { m_Rp[i].ringFrom = MP_INVALID_ID; RpEv(i, RPV_CALL_OFF, 0, 0, (uint8_t)slot, 0, nullptr, why); }
    if(other >= 0 && other < MP_MAX_PLAYERS)
    {
        if(m_Rp[other].callWith == slot) m_Rp[other].callWith = MP_INVALID_ID;
        RpEv(other, RPV_CALL_OFF, 0, 0, (uint8_t)slot, 0, nullptr, why);
    }
    m.callWith = m.ringFrom = MP_INVALID_ID;
    RpEv(slot, RPV_CALL_OFF, 0, 0, other >= 0 ? (uint8_t)other : MP_INVALID_ID, 0, nullptr, why);
}

void MPServer::RpOnLeave(int slot)
{
    if(!IsRpServer()) return;
    RpSrvPlayer& m = m_Rp[slot];
    if(m.callWith != MP_INVALID_ID || m.ringFrom != MP_INVALID_ID) RpEndCall(slot, "The other phone went offline.");
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(m_Rp[i].callWith == slot) { m_Rp[i].callWith = MP_INVALID_ID; RpEv(i, RPV_CALL_OFF, 0, 0, (uint8_t)slot, 0, nullptr, "The other phone went offline."); }
        if(m_Rp[i].ringFrom == slot) m_Rp[i].ringFrom = MP_INVALID_ID;
        if(m_Rp[i].askFrom == slot) m_Rp[i].askFrom = MP_INVALID_ID;
        if(m_Rp[i].fineFrom == slot) m_Rp[i].fineFrom = MP_INVALID_ID;
    }
    if(m.loaded) { RpStore(slot); m_bRpDirty = true; RpSave(); }
    memset(&m, 0, sizeof(m)); m.callWith = m.ringFrom = m.askFrom = m.fineFrom = MP_INVALID_ID; m.house = m.pet = 255;
}

// ---- peace -------------------------------------------------------------------------------------
bool MPServer::RpCanHurt(int from) const
{
    if(from < 0 || from >= MP_MAX_PLAYERS) return false;
    const RpSrvPlayer& m = m_Rp[from];
    return m.loaded && m.duty && RpJobArmed(m.job) && m.level >= g_RpJobs[m.job].minLevel && m.playMin >= RP_NEWBIE_MIN && !m.cuffed && !m.jailS;
}
bool MPServer::RpOnDamage(int from, int target, float /*amount*/)
{
    if(!IsRpServer()) return true;
    if(from == target) return true;
    if(target < 0 || target >= MP_MAX_PLAYERS || !m_Slots[target].used) return false;
    if(m_Slots[target].hasPos && RpInGreen(m_Slots[target].pos[0], m_Slots[target].pos[1])) return false;   // greenzone
    return RpCanHurt(from);
}
void MPServer::RpOnDeath(int victim, int killer)
{
    if(!IsRpServer() || victim < 0 || victim >= MP_MAX_PLAYERS) return;
    RpSrvPlayer& m = m_Rp[victim]; if(!m.loaded) return;
    m.cuffed = false;
    int bill = m.cash >= 300 ? 300 : (m.cash > 0 ? m.cash : 0);
    if(bill) RpPay(victim, -bill, "Hospital bill");
    RpMsg(victim, 0xF9A8D4FF, "[HOSPITAL] You were treated at the hospital (bill $%d).", bill);
    if(killer >= 0 && killer < MP_MAX_PLAYERS && killer != victim && m_Rp[killer].loaded && RpCanHurt(killer))
        RpMsg(killer, RP_YELLOW, "[DUTY] %s is down. Write a report: /me writes a report.", m_Slots[victim].name);
    RpSendMe(victim);
}

// ---- jobs --------------------------------------------------------------------------------------
void MPServer::RpNextTask(int slot)
{
    RpSrvPlayer& m = m_Rp[slot]; const Slot& s = m_Slots[slot];
    const RpJob& j = g_RpJobs[m.job];
    float best[3] = { 0, 0, 0 }; float bestD = -1.0f;
    for(int tries = 0; tries < 10; ++tries)
    {
        float p[3];
        switch(j.set)
        {
            case RPT_TRUCK: { int i = rand() % RP_TRUCK_COUNT; memcpy(p, g_RpTruck[i], sizeof(p)); break; }
            case RPT_HOUSE: { const RpHouse& h = g_RpHouses[rand() % RP_HOUSE_COUNT]; p[0] = h.x; p[1] = h.y; p[2] = h.z; break; }
            case RPT_AIR:   { int i = rand() % RP_AIR_COUNT; memcpy(p, g_RpAir[i], sizeof(p)); break; }
            case RPT_DOCK:  { int i = rand() % RP_DOCK_COUNT; memcpy(p, g_RpDock[i], sizeof(p)); break; }
            case RPT_PLACE: { const RpPlace& pl = g_RpPlaces[2 + rand() % (RP_PLACE_COUNT - 2)]; p[0] = pl.x; p[1] = pl.y; p[2] = pl.z; break; }
            default:        { int i = rand() % RP_STREET_COUNT; memcpy(p, g_RpStreet[i], sizeof(p)); break; }
        }
        float d = s.hasPos ? RpDist2D(p, s.pos) : 500.0f;
        if(d < 25.0f) continue;
        bool good = j.set == RPT_AIR || j.set == RPT_DOCK ? d > 40.0f : (d > 150.0f && d < 1600.0f);
        if(good || bestD < 0 || (bestD < 150.0f && d > bestD)) { memcpy(best, p, sizeof(best)); bestD = d; if(good) break; }
    }
    if(bestD < 0) { memcpy(best, g_RpStreet[0], sizeof(best)); bestD = 300.0f; }
    memcpy(m.target, best, sizeof(m.target)); m.taskAt = MP_NowMs(); m.taskDist = bestD; m.jobOn = true;
    char t[96]; snprintf(t, sizeof(t), "%s: go to the marker (%d m)", j.name, (int)bestD);
    RpEv(slot, RPV_TARGET, 1, 0, MP_INVALID_ID, (int32_t)j.col, m.target, t);
    RpSendMe(slot);
}

int MPServer::RpFindNumber(int32_t number) const
{
    if(number <= 0) return -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded && m_Rp[i].number == number) return i;
    return -1;
}
int MPServer::RpTarget(int slot, int id, float range, const char* what)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || id == slot || !m_Slots[id].used || !m_Rp[id].loaded) { RpMsg(slot, RP_RED, "%s: no citizen with that id here.", what); return -1; }
    if(range > 0.0f)
    {
        const Slot& a = m_Slots[slot]; const Slot& b = m_Slots[id];
        if(!a.hasPos || !b.hasPos || RpDist2D(a.pos, b.pos) > range) { RpMsg(slot, RP_RED, "%s: %s is too far away (get within %d m).", what, b.name, (int)range); return -1; }
    }
    return id;
}

// ---- the 10 Hz tick ----------------------------------------------------------------------------
void MPServer::RpTick(uint64_t now)
{
    if(!IsRpServer()) return;
    static uint64_t s_Last1s = 0;
    if(now - s_Last1s < 1000) return;
    s_Last1s = now;
    {   // 3.30: dropped items fade away after RP_DROP_LIFE_S
        bool gone = false;
        for(int i = 0; i < RP_DROPS_MAX; ++i) if(m_RpDrops[i].id && now >= m_RpDrops[i].until) { memset(&m_RpDrops[i], 0, sizeof(m_RpDrops[i])); gone = true; }
        if(gone) RpSendDrops(-1);
    }
    // 3.29: the market moves every RP_MARKET_S seconds (one market for the whole city)
    if(!m_nRpMarketAt) m_nRpMarketAt = now + RP_MARKET_S * 1000ull;
    if(now >= m_nRpMarketAt)
    {
        m_nRpMarketAt = now + RP_MARKET_S * 1000ull;
        for(int k = 0; k < RPN_COUNT; ++k)
        {
            const RpPlanDef& d = g_RpPlans[k];
            int mv = d.lo + (d.hi > d.lo ? rand() % (d.hi - d.lo + 1) : 0);
            if(m_fRpMarket[k] < 400.0f && mv < 0) mv = -mv / 2;           // a crash bounces back
            if(m_fRpMarket[k] > 20000.0f && mv > 0) mv = -mv;             // a bubble pops
            m_fRpMarket[k] *= 1.0f + mv / 1000.0f; m_nRpMarketChg[k] = (int16_t)mv;
        }
        m_bRpDirty = true; RpSendState(-1);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && m_Rp[i].loaded && (m_Rp[i].invU[1] || m_Rp[i].invU[2]))
            { RpMsg(i, RP_SKY, "[MARKET] Stocks %+.1f%%  VilCoin %+.1f%%  Bond %+.1f%%  - /invest to see yours", m_nRpMarketChg[1] / 10.0f, m_nRpMarketChg[2] / 10.0f, m_nRpMarketChg[0] / 10.0f); RpSendMe(i); }
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_Rp[i].loaded) continue;
        RpSrvPlayer& m = m_Rp[i];
        if(!m.spawned) { m.minuteAt = now; m.paydayAt += 1000; }   // the clocks start when you are in the city
        // played minutes, respect points
        if(m.spawned && now - m.minuteAt >= 60000)
        {
            m.minuteAt += 60000; ++m.playMin; m_bRpDirty = true;
            if(m.playMin % 10 == 0) RpXp(i, 1);
            if(m.playMin == RP_NEWBIE_MIN) RpMsg(i, RP_SKY, "[MEGA VIL] You are no longer a new citizen. Police / military jobs are open to you now.");
        }
        // hunger / thirst
        if(m.spawned && now - m.needAt >= RP_NEED_TICK_S * 1000ull)
        {
            m.needAt = now;
            if(m.hunger) --m.hunger; if(m.thirst) --m.thirst;
            if(m.hunger == 20 || m.thirst == 20) RpMsg(i, RP_YELLOW, "[NEEDS] You are getting %s. Buy something at a 24/7 or in the Shop app.", m.hunger <= m.thirst ? "hungry" : "thirsty");
        }
        // payday
        if(m.spawned && now >= m.paydayAt)
        {
            m.paydayAt = now + RP_PAYDAY_S * 1000ull;
            int salary = 250 + g_RpJobs[m.job].pay / 2 + (m.duty ? 150 : 0);
            int interest = m.bank > 0 ? (int)(m.bank / 1000) : 0; if(interest > 2000) interest = 2000;
            int tax = m.house < RP_HOUSE_COUNT ? g_RpHouses[m.house].price / 2000 : 0;
            m.bank += salary + interest - tax; RpXp(i, 2);
            RpMsg(i, RP_SKY, "________ PAYDAY ________");
            RpMsg(i, RP_WHITE, "Salary ($%d) + bank interest ($%d) - house tax ($%d) = $%d to your bank. Bank: $%d", salary, interest, tax, salary + interest - tax, m.bank);
            RpEv(i, RPV_TEXT, 0, 0, MP_INVALID_ID, (int32_t)RP_SKY, nullptr, "PAYDAY");
        }
        // jail
        if(m.jailS)
        {
            --m.jailS;
            if(!m.jailS) { RpMsg(i, RP_GREEN, "[JAIL] You served your time. Be good, citizen."); RpEv(i, RPV_JAIL, 0, 0, MP_INVALID_ID, 0); m.wanted = 0; }
        }
        // ringing too long
        if(m.ringFrom != MP_INVALID_ID && now - m.ringAt > 30000)
        {
            int c = m.ringFrom; m.ringFrom = MP_INVALID_ID;
            RpEv(i, RPV_CALL_OFF, 0, 0, (uint8_t)c, 0, nullptr, "Missed call.");
            if(c < MP_MAX_PLAYERS) RpEv(c, RPV_CALL_OFF, 0, 0, (uint8_t)i, 0, nullptr, "No answer.");
        }
        // the cash drop / holo tower / festival
        if(m.spawned && m_Slots[i].hasPos && m_nRpEvKind && m_nRpEvState == 1)
        {
            float d = RpDist2D(m_Slots[i].pos, m_fRpEvPos);
            if(m_nRpEvKind == RPEV_CASH_DROP && d < 5.0f)
            {
                int prize = 7500 + rand() % 7501;
                RpPay(i, prize, "Holo Cash Drop");
                RpAll(RP_SKY, "[EVENT] %s found the Holo Cash Drop: $%d!", m_Slots[i].name, prize);
                m_nRpEvState = 2; m_nRpEvEnd = now + 3000; RpSendState(-1);
            }
            else if(m_nRpEvKind == RPEV_HOLO_RUSH && d < 6.0f && m.lastFest != m_nRpNextEvent)
            {
                m.lastFest = m_nRpNextEvent; RpPay(i, 2500, "Holo Tower Rush"); RpXp(i, 1);
                RpMsg(i, RP_SKY, "[EVENT] You reached the holo tower: +$2,500.");
            }
            else if(m_nRpEvKind == RPEV_FESTIVAL && d < 60.0f && now - m.lastFest >= 60000)
            {
                m.lastFest = now; RpPay(i, 300, "Neon Night Festival"); RpXp(i, 1);
            }
        }
        if((now / 1000 + i) % 2 == 0) RpSendMe(i);
        if((now / 1000 + i) % 5 == 0) RpSendCard(i);
    }
    RpEventTick(now);
    if(now - m_nRpLastPlayers >= 3000) { m_nRpLastPlayers = now; RpSendPlayers(-1); }
    if(now - m_nRpLastSend >= 2000) { m_nRpLastSend = now; RpSendState(-1); }
    if(m_bRpDirty && now - m_nRpLastSave >= 30000) { m_nRpLastSave = now; RpSave(); }
}

void MPServer::RpEventTick(uint64_t now)
{
    int online = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded && m_Rp[i].spawned) ++online;
    if(m_nRpEvKind && now >= m_nRpEvEnd)
    {
        if(m_nRpEvKind == RPEV_CASH_DROP && m_nRpEvState == 1) RpAll(RP_OOC_COL, "[EVENT] Nobody found the Holo Cash Drop in time.");
        else RpAll(RP_SKY, "[EVENT] %s is over. Next city event in 15 minutes.", g_RpEventNames[m_nRpEvKind]);
        m_nRpEvKind = RPEV_NONE; m_nRpEvState = 0; m_nRpNextEvent = now + 15 * 60 * 1000; RpSendState(-1);
        return;
    }
    if(m_nRpEvKind || now < m_nRpNextEvent) return;
    if(!online) { m_nRpNextEvent = now + 60000; return; }
    uint8_t k = m_nRpEvNext; m_nRpEvNext = (uint8_t)(m_nRpEvNext % (RPEV_COUNT - 1) + 1);
    m_nRpEvKind = k; m_nRpEvState = 1; m_nRpNextEvent = now;   // m_nRpNextEvent doubles as the event id (holo rush claims)
    switch(k)
    {
        case RPEV_DOUBLE_PAY:
            m_nRpEvEnd = now + 10 * 60 * 1000; memset(m_fRpEvPos, 0, sizeof(m_fRpEvPos));
            RpAll(RP_SKY, "[EVENT] DOUBLE PAY HOUR: every job task pays x2 for 10 minutes. /work!");
            break;
        case RPEV_CASH_DROP:
        {
            int i = rand() % RP_STREET_COUNT; memcpy(m_fRpEvPos, g_RpStreet[i], sizeof(m_fRpEvPos));
            m_nRpEvEnd = now + 10 * 60 * 1000;
            RpAll(RP_SKY, "[EVENT] HOLO CASH DROP: $7,500 - $15,000 is hidden on a street of Mega Vil. Look for the sky blue marker!");
            break;
        }
        case RPEV_FESTIVAL:
            memcpy(m_fRpEvPos, &g_RpPlaces[RP_PLAZA].x, sizeof(m_fRpEvPos)); m_nRpEvEnd = now + 6 * 60 * 1000;
            RpAll(RP_SKY, "[EVENT] NEON NIGHT FESTIVAL at Mega Vil Plaza: $300 + respect every minute you party there. /dance!");
            break;
        case RPEV_LOTTERY:
        {
            int win = 1 + rand() % 60; int winners[MP_MAX_PLAYERS], nw = 0, sold = 0;
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].lotto) { ++sold; if(m_Rp[i].lotto == win) winners[nw++] = i; }
            RpAll(RP_SKY, "[LOTTERY] Mega Lottery draw: the number is %d! (%d tickets)", win, sold);
            if(nw)
            {
                int each = m_nRpLottoPot / nw;
                for(int w = 0; w < nw; ++w) { RpPay(winners[w], each, "Mega Lottery jackpot"); RpAll(RP_YELLOW, "[LOTTERY] %s wins $%d!", m_Slots[winners[w]].name, each); }
                m_nRpLottoPot = 25000;
            }
            else { RpAll(RP_OOC_COL, "[LOTTERY] No winner - the jackpot ($%d) rolls over. /lotto 1-60 for the next draw.", m_nRpLottoPot); }
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) m_Rp[i].lotto = 0;
            m_nRpEvEnd = now + 15000; m_bRpDirty = true;
            break;
        }
        case RPEV_HOLO_RUSH:
        {
            int i = rand() % RP_STREET_COUNT; memcpy(m_fRpEvPos, g_RpStreet[i], sizeof(m_fRpEvPos)); m_nRpEvEnd = now + 8 * 60 * 1000;
            RpAll(RP_SKY, "[EVENT] HOLO TOWER RUSH: a holo tower appeared. Everyone who reaches it in 8 minutes gets $2,500.");
            break;
        }
    }
    RpSendState(-1);
}

// ---- commands ----------------------------------------------------------------------------------
static int RpPlaceNear(const float* p, uint8_t kind, float r)
{
    for(int i = 0; i < RP_PLACE_COUNT; ++i) if(g_RpPlaces[i].kind == kind) { float q[3] = { g_RpPlaces[i].x, g_RpPlaces[i].y, g_RpPlaces[i].z }; if(RpDist2D(p, q) < r) return i; }
    return -1;
}

void MPServer::RpHandleCmd(int slot, const PktRpCmd& c)
{
    if(!IsRpServer() || slot < 0 || slot >= MP_MAX_PLAYERS) return;
    Slot& s = m_Slots[slot]; RpSrvPlayer& m = m_Rp[slot];
    if(!s.authed || !m.loaded) { RpMsg(slot, RP_RED, "Log in first (Mega Vil needs an account)."); return; }
    const uint64_t now = MP_NowMs();
    char line[200], mb[24];
    const char* nm = s.name;
    const bool onDuty = m.duty && RpJobArmed(m.job);
    switch(c.cmd)
    {
    case RPC_SPAWNED:
        m.spawned = true;
        if(m.skin) RpEv(slot, RPV_SKIN, 0, 0, MP_INVALID_ID, m.skin);
        if(m.jailS) RpEv(slot, RPV_JAIL, 0, 0, MP_INVALID_ID, m.jailS);
        RpSendMe(slot);
        break;

    // -------- talking ---------------------------------------------------------------------------
    case RPC_CHAT:
    {
        if(s.muted) { RpMsg(slot, RP_RED, "You are muted."); break; }
        if(!c.text[0]) break;
        if(now - m.lastChat < 600) break;
        m.lastChat = now;
        switch(c.a)
        {
            case RPH_SAY:     snprintf(line, sizeof(line), "%s says: %s", nm, c.text); RpNear(slot, RP_TALK_RANGE, RP_WHITE, line); break;
            case RPH_ME:
            case RPH_AME:     snprintf(line, sizeof(line), "* %s %s", nm, c.text); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false); break;
            case RPH_DO:      snprintf(line, sizeof(line), "* %s (( %s ))", c.text, nm); RpNear(slot, RP_TALK_RANGE, RP_DO_COL, line, false); break;
            case RPH_TRY:     snprintf(line, sizeof(line), "* %s tries to %s and %s.", nm, c.text, rand() % 2 ? "succeeds" : "fails"); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false); break;
            case RPH_B:       snprintf(line, sizeof(line), "(( %s [%d]: %s ))", nm, slot, c.text); RpNear(slot, RP_TALK_RANGE, RP_OOC_COL, line, false); break;
            case RPH_SHOUT:   snprintf(line, sizeof(line), "%s shouts: %s!", nm, c.text); RpNear(slot, RP_SHOUT_RANGE, RP_WHITE, line); break;
            case RPH_LOW:     snprintf(line, sizeof(line), "%s says quietly: %s", nm, c.text); RpNear(slot, RP_LOW_RANGE, 0xD4D4D4FF, line); break;
            case RPH_OOC:
                if(now - m.lastSms < 8000) { RpMsg(slot, RP_RED, "Global OOC: wait a few seconds."); break; }
                m.lastSms = now; RpAll(0xE2E8F0FF, "(( [OOC] %s [%d]: %s ))", nm, slot, c.text); break;
            case RPH_NEWBIE:
                if(now - m.lastSms < 8000) { RpMsg(slot, RP_RED, "Newbie chat: wait a few seconds."); break; }
                m.lastSms = now; RpAll(0x86EFACFF, "[NEWBIE] %s%s [%d]: %s", m.playMin < RP_NEWBIE_MIN ? "(new) " : "", nm, slot, c.text); break;
            case RPH_WHISPER:
            {
                int t = RpTarget(slot, c.b, 3.0f, "/w"); if(t < 0) break;
                RpMsg(t, 0xFDE68AFF, "%s whispers: %s", nm, c.text); RpMsg(slot, 0xFDE68AFF, "You whisper to %s: %s", m_Slots[t].name, c.text);
                snprintf(line, sizeof(line), "* %s whispers something to %s.", nm, m_Slots[t].name); RpNear(slot, RP_LOW_RANGE, RP_ME_COL, line, false);
                break;
            }
            case RPH_MEGAPHONE:
                if(!onDuty) { RpMsg(slot, RP_RED, "Only on-duty police / military have a megaphone."); break; }
                snprintf(line, sizeof(line), "[MEGAPHONE] %s: %s", nm, c.text); RpNear(slot, 70.0f, RP_YELLOW, line, false); break;
            case RPH_RADIO:
                if(!m.job) { RpMsg(slot, RP_RED, "You need a job for a work radio."); break; }
                for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded && m_Rp[i].job == m.job) RpMsg(i, 0x93C5FDFF, "[RADIO %s] %s: %s", g_RpJobs[m.job].name, nm, c.text);
                break;
            case RPH_DEPT:
                if(m.job != RPJ_POLICE && m.job != RPJ_MILITARY && m.job != RPJ_MEDIC && m.job != RPJ_FIRE) { RpMsg(slot, RP_RED, "/d is for police, military, medics and firefighters."); break; }
                for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded) { uint8_t j = m_Rp[i].job; if(j == RPJ_POLICE || j == RPJ_MILITARY || j == RPJ_MEDIC || j == RPJ_FIRE) RpMsg(i, 0xFCA5A5FF, "[DEPT] %s (%s): %s", nm, g_RpJobs[m.job].name, c.text); }
                break;
            case RPH_GOV:
                if(!(onDuty && m.level >= 5)) { RpMsg(slot, RP_RED, "/gov: on-duty police / military, level 5+."); break; }
                RpAll(RP_SKY, "[GOVERNMENT] %s: %s", nm, c.text); break;
            default: break;
        }
        break;
    }
    case RPC_FEED:   // Vil Feed (the city's social app)
        if(now - m.lastSms < 15000) { RpMsg(slot, RP_RED, "Vil Feed: one post every 15 s."); break; }
        if(!c.text[0]) break;
        m.lastSms = now;
        RpEv(-1, RPV_FEED, 0, 0, (uint8_t)slot, m.number, nullptr, c.text);
        RpAll(0x7DD3FCFF, "[VilFeed] @%s: %s", nm, c.text);
        break;
    case RPC_AD:
        if(!c.text[0]) break;
        if(now - m.lastSms < 30000) { RpMsg(slot, RP_RED, "Adverts: one every 30 s."); break; }
        if(!RpPay(slot, -100, "Advert")) { RpMsg(slot, RP_RED, "An advert costs $100."); break; }
        m.lastSms = now;
        RpAll(0x6EE7B7FF, "[AD] %s  - call %d (%s)", c.text, m.number, nm);
        break;

    // -------- money -----------------------------------------------------------------------------
    case RPC_PAY:
    {
        int t = RpTarget(slot, c.b, 5.0f, "/pay"); if(t < 0) break;
        if(c.val <= 0 || c.val > 1000000) { RpMsg(slot, RP_RED, "/pay [id] [1 - 1,000,000]"); break; }
        if(!RpPay(slot, -c.val, "Paid")) { RpMsg(slot, RP_RED, "You don't have that much cash."); break; }
        RpPay(t, c.val, "Received");
        RpMsg(slot, RP_GREEN, "You gave %s %s.", m_Slots[t].name, RpMoney(c.val, mb, sizeof(mb)));
        RpMsg(t, RP_GREEN, "%s gave you %s.", nm, RpMoney(c.val, mb, sizeof(mb)));
        snprintf(line, sizeof(line), "* %s takes out some cash and hands it to %s.", nm, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        Log("RP: %s paid %s $%d", nm, m_Slots[t].name, c.val);
        break;
    }
    case RPC_DEPOSIT:
    case RPC_WITHDRAW:
    {
        if(!s.hasPos || (RpPlaceNear(s.pos, RPK_ATM, 8.0f) < 0 && RpPlaceNear(s.pos, RPK_BANK, 15.0f) < 0)) { RpMsg(slot, RP_RED, "Go to an ATM or the Mega Bank (GPS app). Transfers work anywhere."); break; }
        int32_t v = c.val; if(v <= 0) { RpMsg(slot, RP_RED, "Amount?"); break; }
        if(c.cmd == RPC_DEPOSIT) { if(m.cash < v) { RpMsg(slot, RP_RED, "Not enough cash."); break; } RpPay(slot, -v, "Bank deposit"); m.bank += v; RpMsg(slot, RP_GREEN, "Deposited %s. Bank: $%d", RpMoney(v, mb, sizeof(mb)), m.bank); }
        else                     { if(m.bank < v) { RpMsg(slot, RP_RED, "Not enough in the bank."); break; } m.bank -= v; RpPay(slot, v, "Bank withdrawal"); RpMsg(slot, RP_GREEN, "Withdrew %s. Bank: $%d", RpMoney(v, mb, sizeof(mb)), m.bank); }
        RpSendMe(slot);
        break;
    }
    case RPC_TRANSFER:
    {
        int t = (c.a == 1) ? RpFindNumber((int32_t)strtol(c.text, nullptr, 10)) : RpTarget(slot, c.b, 0.0f, "Transfer");
        if(t < 0 || t == slot) { if(c.a == 1) RpMsg(slot, RP_RED, "Transfer: nobody online has that phone number."); break; }
        if(c.val <= 0 || c.val > 10000000) { RpMsg(slot, RP_RED, "Transfer: 1 - 10,000,000."); break; }
        if(m.bank < c.val) { RpMsg(slot, RP_RED, "Transfer: not enough money in the bank."); break; }
        m.bank -= c.val; m_Rp[t].bank += c.val; m_bRpDirty = true;
        RpMsg(slot, RP_GREEN, "[BANK] Sent %s to %s.", RpMoney(c.val, mb, sizeof(mb)), m_Slots[t].name);
        RpMsg(t, RP_GREEN, "[BANK] %s sent you %s.", nm, RpMoney(c.val, mb, sizeof(mb)));
        RpSendMe(slot); RpSendMe(t);
        Log("RP: %s transferred %s $%d", nm, m_Slots[t].name, c.val);
        break;
    }

    // -------- shops -----------------------------------------------------------------------------
    case RPC_BUY:
    {
        int idx = c.val; if(idx < 0 || idx >= RP_SHOP_COUNT) break;
        if(now - m.lastBuy < 400) break; m.lastBuy = now;
        const RpShopItem& it = g_RpShop[idx];
        bool phone = c.a == 1;
        static const uint8_t kKind[RPS_CAT_COUNT] = { RPK_SHOP, RPK_CLOTHES, RPK_CARS, RPK_TECH, RPK_GUNS };
        bool atStore = s.hasPos && (RpPlaceNear(s.pos, kKind[it.cat], 18.0f) >= 0 || (it.cat == RPS_FOOD && RpPlaceNear(s.pos, RPK_FOOD, 18.0f) >= 0) || (it.cat == RPS_TECH && RpPlaceNear(s.pos, RPK_PETS, 18.0f) >= 0));
        if(it.cat == RPS_GUNS && !atStore) { RpMsg(slot, RP_RED, "Ammu-Nation items: only at the Licensed Ammu-Nation (GPS)."); break; }
        if(!phone && !atStore) phone = true;   // not at the counter: it comes by drone
        int price = phone ? it.price + it.price / 10 : it.price;
        if(it.kind == RPI_WEAPON && !RpWeaponIsHarmless(it.a) && !(onDuty && (m.items & RPIT_GUNLIC))) { RpMsg(slot, RP_RED, "Weapons: on-duty police / military with a gun licence only. Mega Vil is peaceful."); break; }
        if(it.kind == RPI_ARMOUR && !onDuty) { RpMsg(slot, RP_RED, "Body armour: on-duty police / military only."); break; }
        if(it.kind == RPI_ITEM && it.a == RPIT_GUNLIC && !RpJobArmed(m.job)) { RpMsg(slot, RP_RED, "Gun licences are for police officers and soldiers."); break; }
        if(it.kind == RPI_ITEM && (m.items & (uint32_t)it.a)) { RpMsg(slot, RP_RED, "You already have %s.", it.name); break; }
        if(it.kind == RPI_PET && (m.pets & (1u << it.a))) { RpMsg(slot, RP_RED, "You already have this pet. Pets app to call it."); break; }
        if(it.kind == RPI_VEH && m.nveh >= RP_VEH_SLOTS) { RpMsg(slot, RP_RED, "Your garage is full (%d). Sell one in the Garage app.", RP_VEH_SLOTS); break; }
        if(it.kind == RPI_LOTTO && m.lotto) { RpMsg(slot, RP_RED, "You already have a ticket (number %d).", m.lotto); break; }
        const uint8_t toBag = RpInvForShop(it.name);   // 3.26: food, drinks, medkits and repair kits go into the bag
        if(toBag && RpInvRoom(m.inv, toBag) < 1) { RpMsg(slot, RP_RED, "[BAG] Your bag is full - use or drop something first (PANEL > Bag)."); break; }
        int32_t fromBank = 0;
        if(m.cash < price) { if(it.price >= 10000 && m.cash + m.bank >= price) { fromBank = price - (m.cash > 0 ? m.cash : 0); } else { RpMsg(slot, RP_RED, "%s costs %s - not enough money.", it.name, RpMoney(price, mb, sizeof(mb))); break; } }
        if(fromBank) { m.bank -= fromBank; RpPay(slot, -(price - fromBank), it.name); }
        else RpPay(slot, -price, it.name);
        if(toBag)
        {
            RpInvGive(slot, toBag, 1, "Received");
            RpMsg(slot, RP_GREEN, "[SHOP] You bought %s for %s%s - it is in your bag (PANEL > Bag > USE).", it.name, RpMoney(price, mb, sizeof(mb)), phone ? " (drone delivery +10%)" : "");
            m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
            RpSendMe(slot);
            break;
        }
        switch(it.kind)
        {
            case RPI_FOOD:   m.hunger = (uint8_t)std::min(100, m.hunger + it.a); m.thirst = (uint8_t)std::min(100, m.thirst + it.b); RpEv(slot, RPV_ITEM, 0, it.b > it.a ? 2 : 0, MP_INVALID_ID, idx, nullptr, it.name); break;
            case RPI_MEDKIT: RpEv(slot, RPV_HEAL, (uint8_t)it.a, 0, MP_INVALID_ID, 0); break;
            case RPI_SKIN:   m.skin = (uint16_t)it.a; RpEv(slot, RPV_SKIN, 0, 0, MP_INVALID_ID, it.a); break;
            case RPI_VEH:    m.veh[m.nveh++] = (uint16_t)it.a; m.lastVeh = now; RpEv(slot, RPV_VEHICLE, 0, 0, MP_INVALID_ID, it.a); break;
            case RPI_PET:    m.pets |= 1u << it.a; m.pet = (uint8_t)it.a; break;
            case RPI_ITEM:   m.items |= (uint32_t)it.a; if(it.a == RPIT_PARACHUTE) RpEv(slot, RPV_WEAPON, 46, 0, MP_INVALID_ID, 1); break;
            case RPI_WEAPON: RpEv(slot, RPV_WEAPON, (uint8_t)it.a, 0, MP_INVALID_ID, it.b); break;
            case RPI_ARMOUR: RpEv(slot, RPV_HEAL, 0, 100, MP_INVALID_ID, 0); break;
            case RPI_LOTTO:  m.lotto = (uint16_t)(c.b >= 1 && c.b <= 60 ? c.b : 1 + rand() % 60); m_nRpLottoPot += 50; RpMsg(slot, RP_YELLOW, "[LOTTERY] Your number: %d. Draw at the next Mega Lottery event.", m.lotto); break;
            case RPI_REPAIR: RpEv(slot, RPV_REPAIR, 0, 0, MP_INVALID_ID, 0); break;
        }
        RpMsg(slot, RP_GREEN, "[SHOP] You bought %s for %s%s.", it.name, RpMoney(price, mb, sizeof(mb)), phone ? " (drone delivery +10%)" : "");
        m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpSendMe(slot);
        break;
    }

    // -------- jobs ------------------------------------------------------------------------------
    case RPC_JOIN_JOB:
    {
        int j = c.a; if(j <= RPJ_NONE || j >= RPJ_COUNT) break;
        if(m.level < g_RpJobs[j].minLevel) { RpMsg(slot, RP_RED, "%s needs level %d (you are %d).", g_RpJobs[j].name, g_RpJobs[j].minLevel, m.level); break; }
        if(RpJobArmed(j) && m.playMin < RP_NEWBIE_MIN) { RpMsg(slot, RP_RED, "New citizens can't join the police or the military yet."); break; }
        if(m.jailS) { RpMsg(slot, RP_RED, "Not from jail."); break; }
        m.job = (uint8_t)j; m.duty = false; m.jobOn = false;
        RpMsg(slot, RP_SKY, "[JOB CENTER] You are now a %s. %s", g_RpJobs[j].name, g_RpJobs[j].desc);
        RpMsg(slot, RP_WHITE, RpJobArmed(j) ? "/duty to go on duty (uniform + equipment), /work to patrol." : "/work to start a shift (job vehicle + markers), /stopwork to end it.");
        RpSendMe(slot); RpSendPlayers(-1);
        break;
    }
    case RPC_QUIT_JOB:
        if(!m.job) break;
        RpMsg(slot, RP_SKY, "[JOB CENTER] You quit your job as %s.", g_RpJobs[m.job].name);
        if(m.duty) RpEv(slot, RPV_DISARM, 0, 0, MP_INVALID_ID, 0);
        m.job = 0; m.duty = false; m.jobOn = false; RpSendMe(slot); RpSendPlayers(-1);
        break;
    case RPC_DUTY:
        if(!RpJobArmed(m.job) && m.job != RPJ_MEDIC && m.job != RPJ_FIRE) { RpMsg(slot, RP_RED, "/duty is for police, military, medics and firefighters."); break; }
        m.duty = !m.duty;
        if(m.duty)
        {
            if(g_RpJobs[m.job].skin) RpEv(slot, RPV_SKIN, 1, 0, MP_INVALID_ID, g_RpJobs[m.job].skin);
            if(m.job == RPJ_POLICE)   { RpEv(slot, RPV_WEAPON, 3, 0, MP_INVALID_ID, 1); RpEv(slot, RPV_WEAPON, 24, 0, MP_INVALID_ID, 70); RpEv(slot, RPV_WEAPON, 29, 0, MP_INVALID_ID, 240); RpEv(slot, RPV_HEAL, 100, 100, MP_INVALID_ID, 0); }
            if(m.job == RPJ_MILITARY) { RpEv(slot, RPV_WEAPON, 24, 0, MP_INVALID_ID, 70); RpEv(slot, RPV_WEAPON, 31, 0, MP_INVALID_ID, 300); RpEv(slot, RPV_HEAL, 100, 100, MP_INVALID_ID, 0); }
            if(m.job == RPJ_FIRE)     RpEv(slot, RPV_WEAPON, 42, 0, MP_INVALID_ID, 2000);
            RpMsg(slot, RP_SKY, "[DUTY] You are on duty as %s.%s", g_RpJobs[m.job].name, RpCanHurt(slot) ? " You may use force when it is needed - roleplay it." : "");
            snprintf(line, sizeof(line), "* %s puts on the %s uniform.", nm, g_RpJobs[m.job].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        }
        else { RpEv(slot, RPV_DISARM, 1, 0, MP_INVALID_ID, m.skin); RpMsg(slot, RP_SKY, "[DUTY] Off duty."); }
        RpSendMe(slot); RpSendPlayers(-1);
        break;
    case RPC_WORK:
        if(!m.job) { RpMsg(slot, RP_RED, "You have no job. City Hall or the Jobs app."); break; }
        if(m.jailS || m.cuffed) break;
        if(g_RpJobs[m.job].vehicle && now - m.lastVeh > 45000) { m.lastVeh = now; RpEv(slot, RPV_VEHICLE, 1, 0, MP_INVALID_ID, g_RpJobs[m.job].vehicle); }
        if(g_RpJobs[m.job].skin && !RpJobArmed(m.job)) RpEv(slot, RPV_SKIN, 1, 0, MP_INVALID_ID, g_RpJobs[m.job].skin);
        RpMsg(slot, RP_SKY, "[WORK] Shift started: %s. Follow the markers. /stopwork to end.", g_RpJobs[m.job].name);
        RpNextTask(slot);
        break;
    case RPC_STOP_WORK:
        if(!m.jobOn) break;
        m.jobOn = false; RpEv(slot, RPV_TARGET, 0, 0, MP_INVALID_ID, 0, nullptr, "");
        RpMsg(slot, RP_SKY, "[WORK] Shift over. %u tasks done in total.", m.tasks);
        if(m.skin && !m.duty) RpEv(slot, RPV_SKIN, 0, 0, MP_INVALID_ID, m.skin);
        RpSendMe(slot);
        break;
    case RPC_ARRIVE:
    {
        if(!m.jobOn || !s.hasPos) break;
        float d = RpDist2D(s.pos, m.target);
        if(d > 16.0f) break;
        float minS = m.taskDist / 55.0f; if(minS < 6.0f) minS = 6.0f;
        if(g_RpJobs[m.job].set == RPT_AIR) minS = m.taskDist / 90.0f;
        if((now - m.taskAt) / 1000.0f < minS) { RpMsg(slot, RP_RED, "[WORK] Too fast - no pay for teleports."); RpNextTask(slot); break; }
        float k = 0.6f + std::min(m.taskDist, 2500.0f) / 2500.0f * 1.4f;
        int pay = (int)(g_RpJobs[m.job].pay * k);
        if(m_nRpEvKind == RPEV_DOUBLE_PAY) pay *= 2;
        if(m_nRpEvKind == RPEV_HOLO_RUSH && (m.job == RPJ_HOLO || m.job == RPJ_HACKER)) pay *= 3;
        if(m.hunger < 10 || m.thirst < 10) pay = pay * 3 / 4;
        ++m.tasks; RpPay(slot, pay, g_RpJobs[m.job].name);
        if(m.tasks % 3 == 0) RpXp(slot, 1);
        RpMsg(slot, RP_GREEN, "[WORK] Task done: +%s (%d m).", RpMoney(pay, mb, sizeof(mb)), (int)m.taskDist);
        RpNextTask(slot);
        break;
    }

    // -------- phone -----------------------------------------------------------------------------
    case RPC_SMS:
    {
        if(now - m.lastSms < 1500 || !c.text[0]) break;
        m.lastSms = now;
        int t = c.a == 1 ? RpTarget(slot, c.b, 0.0f, "SMS") : RpFindNumber(c.val);
        if(t < 0 || t == slot) { if(c.a != 1) RpMsg(slot, RP_RED, "[PHONE] Number %d is not reachable.", c.val); break; }
        RpEv(t, RPV_SMS, 0, 0, (uint8_t)slot, m.number, nullptr, c.text);
        RpEv(slot, RPV_SMS, 1, 0, (uint8_t)t, m_Rp[t].number, nullptr, c.text);   // a = 1: sent (for my thread)
        break;
    }
    case RPC_CALL:
    {
        int t = c.a == 1 ? RpTarget(slot, c.b, 0.0f, "Call") : RpFindNumber(c.val);
        if(c.a == 0 && (c.val == 911 || c.val == 112)) { RpMsg(slot, RP_YELLOW, "[911] Use /911 [what happened] - dispatch sends it to police, medics and firefighters."); break; }
        if(t < 0 || t == slot) { if(c.a != 1) RpMsg(slot, RP_RED, "[PHONE] The number %d is not reachable.", c.val); break; }
        if(m.callWith != MP_INVALID_ID) { RpMsg(slot, RP_RED, "[PHONE] Hang up first."); break; }
        if(m_Rp[t].callWith != MP_INVALID_ID || m_Rp[t].ringFrom != MP_INVALID_ID || m_Rp[t].jailS) { RpEv(slot, RPV_CALL_OFF, 0, 0, (uint8_t)t, 0, nullptr, "Busy."); break; }
        m_Rp[t].ringFrom = (uint8_t)slot; m_Rp[t].ringAt = now;
        RpEv(t, RPV_RING, 0, 0, (uint8_t)slot, m.number, nullptr, nm);
        RpEv(slot, RPV_RING, 1, 0, (uint8_t)t, m_Rp[t].number, nullptr, m_Slots[t].name);   // a = 1: outgoing
        snprintf(line, sizeof(line), "* %s takes out a phone and dials a number.", nm); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    }
    case RPC_ANSWER:
    {
        int cl = m.ringFrom; if(cl == MP_INVALID_ID || cl >= MP_MAX_PLAYERS || !m_Slots[cl].used) { m.ringFrom = MP_INVALID_ID; break; }
        m.ringFrom = MP_INVALID_ID; m.callWith = (uint8_t)cl; m_Rp[cl].callWith = (uint8_t)slot;
        RpEv(slot, RPV_CALL_ON, 0, 0, (uint8_t)cl, m_Rp[cl].number, nullptr, m_Slots[cl].name);
        RpEv(cl, RPV_CALL_ON, 0, 0, (uint8_t)slot, m.number, nullptr, nm);
        snprintf(line, sizeof(line), "* %s answers the phone.", nm); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    }
    case RPC_HANGUP:
        RpEndCall(slot, "Call ended.");
        break;
    case RPC_911:
    case RPC_TAXI_CALL:
    {
        if(now - m.lastSms < 10000) { RpMsg(slot, RP_RED, "Wait a few seconds."); break; }
        m.lastSms = now;
        int n = 0;
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(i == slot || !m_Slots[i].used || !m_Rp[i].loaded) continue;
            uint8_t j = m_Rp[i].job;
            bool to = c.cmd == RPC_TAXI_CALL ? j == RPJ_TAXI : (j == RPJ_POLICE || j == RPJ_MEDIC || j == RPJ_FIRE || j == RPJ_MILITARY);
            if(!to) continue;
            ++n;
            if(c.cmd == RPC_TAXI_CALL) RpMsg(i, RP_YELLOW, "[TAXI] %s (%d, phone %d) needs a taxi%s%s. Marker on your GPS.", nm, slot, m.number, c.text[0] ? ": " : "", c.text);
            else RpMsg(i, RP_RED, "[911] %s (%d, phone %d): %s - marker on your GPS.", nm, slot, m.number, c.text[0] ? c.text : "help!");
            if(s.hasPos) RpEv(i, RPV_TARGET, 2, 0, (uint8_t)slot, (int32_t)(c.cmd == RPC_TAXI_CALL ? 0xFACC15FF : RP_RED), s.pos, c.cmd == RPC_TAXI_CALL ? "Taxi call" : "911 call");
        }
        RpMsg(slot, n ? RP_GREEN : RP_YELLOW, n ? "[%s] Sent to %d %s." : "[%s] Nobody is on that job right now. Try again later.", c.cmd == RPC_TAXI_CALL ? "TAXI" : "911", n, c.cmd == RPC_TAXI_CALL ? "taxi drivers" : "officers / medics");
        break;
    }

    // -------- houses / vehicles / pets ----------------------------------------------------------
    case RPC_BUY_HOUSE:
    {
        int h = c.a; if(h >= RP_HOUSE_COUNT) break;
        if(m.house < RP_HOUSE_COUNT) { RpMsg(slot, RP_RED, "You already own a house. /sellhouse first."); break; }
        float hp[3] = { g_RpHouses[h].x, g_RpHouses[h].y, g_RpHouses[h].z };
        if(!s.hasPos || RpDist2D(s.pos, hp) > 10.0f) { RpMsg(slot, RP_RED, "Go to the house door first (Houses app - GPS)."); break; }
        if(!m_RpHouseOwner[h].empty()) { RpMsg(slot, RP_RED, "This house is sold."); break; }
        int price = g_RpHouses[h].price;
        if(m.bank + m.cash < price) { RpMsg(slot, RP_RED, "This house costs %s.", RpMoney(price, mb, sizeof(mb))); break; }
        int fromBank = std::min(m.bank, price); m.bank -= fromBank; if(price - fromBank) RpPay(slot, -(price - fromBank), "House");
        char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb)); m_RpHouseOwner[h] = key ? key : "";
        m.house = (uint8_t)h; m_bRpDirty = true; m.statT = (uint32_t)time(nullptr);
        RpAll(RP_SKY, "[REALTY] %s bought %s for %s. Welcome home!", nm, g_RpHouses[h].addr, RpMoney(price, mb, sizeof(mb)));
        RpSendMe(slot); RpSendState(-1); RpSave();
        break;
    }
    case RPC_SELL_HOUSE:
    {
        if(m.house >= RP_HOUSE_COUNT) { RpMsg(slot, RP_RED, "You don't own a house."); break; }
        int back = g_RpHouses[m.house].price * 6 / 10;
        m_RpHouseOwner[m.house].clear(); m.bank += back;
        RpMsg(slot, RP_SKY, "[REALTY] You sold %s: %s to your bank.", g_RpHouses[m.house].addr, RpMoney(back, mb, sizeof(mb)));
        m.house = 255; m_bRpDirty = true; m.statT = (uint32_t)time(nullptr); RpSendMe(slot); RpSendState(-1); RpSave();
        break;
    }
    case RPC_MY_VEH:
        if(c.a >= m.nveh) break;
        if(now - m.lastVeh < 20000) { RpMsg(slot, RP_RED, "[GARAGE] Your valet is on the way - wait %d s.", (int)((20000 - (now - m.lastVeh)) / 1000)); break; }
        if(m.jailS || m.cuffed) break;
        m.lastVeh = now; RpEv(slot, RPV_VEHICLE, 0, 0, MP_INVALID_ID, m.veh[c.a]);
        break;
    case RPC_SELL_VEH:
    {
        if(c.a >= m.nveh) break;
        int model = m.veh[c.a], back = 1000;
        for(int i = 0; i < RP_SHOP_COUNT; ++i) if(g_RpShop[i].kind == RPI_VEH && g_RpShop[i].a == model) { back = g_RpShop[i].price / 2; break; }
        for(int v = c.a; v + 1 < m.nveh; ++v) m.veh[v] = m.veh[v + 1];
        m.veh[--m.nveh] = 0; m.bank += back; m_bRpDirty = true; m.statT = (uint32_t)time(nullptr);
        RpMsg(slot, RP_SKY, "[GARAGE] Sold for %s (to your bank).", RpMoney(back, mb, sizeof(mb)));
        RpSendMe(slot);
        break;
    }
    case RPC_PET:
        if(c.a == 255) { m.pet = 255; RpSendMe(slot); break; }
        if(c.a >= RP_PET_COUNT || !(m.pets & (1u << c.a))) { RpMsg(slot, RP_RED, "You don't own that pet (Shop - Tech & Pets)."); break; }
        m.pet = c.a; RpSendMe(slot);
        snprintf(line, sizeof(line), "* %s's %s floats next to them.", nm, g_RpPets[c.a].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    case RPC_JETPACK:
        if(!(m.items & RPIT_JETPACK)) { RpMsg(slot, RP_RED, "You need a jetpack licence (Neo Tech Store / Shop app)."); break; }
        if(now - m.lastVeh < 60000) { RpMsg(slot, RP_RED, "The jetpack is recharging (60 s)."); break; }
        m.lastVeh = now; RpEv(slot, RPV_JETPACK, 0, 0, MP_INVALID_ID, 0);
        break;

    // -------- police / medic / mechanic / lawyer ------------------------------------------------
    case RPC_CUFF:
    case RPC_UNCUFF:
    {
        if(!(m.job == RPJ_POLICE && m.duty)) { RpMsg(slot, RP_RED, "Police on duty only."); break; }
        int t = RpTarget(slot, c.b, 4.0f, c.cmd == RPC_CUFF ? "/cuff" : "/uncuff"); if(t < 0) break;
        m_Rp[t].cuffed = c.cmd == RPC_CUFF;
        RpEv(t, RPV_CUFF, m_Rp[t].cuffed ? 1 : 0, 0, (uint8_t)slot, 0);
        snprintf(line, sizeof(line), c.cmd == RPC_CUFF ? "* %s puts handcuffs on %s." : "* %s takes the handcuffs off %s.", nm, m_Slots[t].name);
        RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false); RpSendMe(t);
        break;
    }
    case RPC_ARREST:
    {
        if(!(m.job == RPJ_POLICE && m.duty)) { RpMsg(slot, RP_RED, "Police on duty only."); break; }
        int t = RpTarget(slot, c.b, 6.0f, "/arrest"); if(t < 0) break;
        if(!m_Rp[t].cuffed && !m_Rp[t].wanted) { RpMsg(slot, RP_RED, "Cuff them first or they must be wanted (/wanted)."); break; }
        int secs = RP_JAIL_S + m_Rp[t].wanted * 60; if(secs > 900) secs = 900;
        m_Rp[t].jailS = (uint16_t)secs; m_Rp[t].cuffed = false; m_Rp[t].jobOn = false; m_Rp[t].duty = false;
        RpEv(t, RPV_JAIL, 0, 0, (uint8_t)slot, secs);
        RpAll(0x93C5FDFF, "[MVPD] %s was arrested by Officer %s (%d min). %s", m_Slots[t].name, nm, secs / 60, c.text);
        RpPay(slot, 500, "Arrest bonus"); RpXp(slot, 1); RpSendMe(t);
        break;
    }
    case RPC_FINE:
    {
        if(!(m.job == RPJ_POLICE && m.duty)) { RpMsg(slot, RP_RED, "Police on duty only."); break; }
        int t = RpTarget(slot, c.b, 8.0f, "/fine"); if(t < 0) break;
        if(c.val < 1 || c.val > 10000) { RpMsg(slot, RP_RED, "/fine [id] [1 - 10,000] [reason]"); break; }
        m_Rp[t].fineAsk = c.val; m_Rp[t].fineFrom = (uint8_t)slot;
        RpMsg(t, RP_YELLOW, "[MVPD] Officer %s gives you a ticket of %s: %s. /accept fine to pay.", nm, RpMoney(c.val, mb, sizeof(mb)), c.text[0] ? c.text : "traffic");
        RpMsg(slot, RP_YELLOW, "[MVPD] Ticket given to %s.", m_Slots[t].name);
        break;
    }
    case RPC_WANTED:
    {
        if(!(m.job == RPJ_POLICE && m.duty)) { RpMsg(slot, RP_RED, "Police on duty only."); break; }
        int t = RpTarget(slot, c.b, 0.0f, "/su"); if(t < 0) break;
        m_Rp[t].wanted = (uint8_t)std::min(6, (int)c.a);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Rp[i].loaded && m_Rp[i].job == RPJ_POLICE) RpMsg(i, 0x93C5FDFF, "[MDC] %s is now wanted (level %d) by %s: %s", m_Slots[t].name, m_Rp[t].wanted, nm, c.text);
        RpMsg(t, RP_RED, "[MVPD] You are wanted (level %d): %s", m_Rp[t].wanted, c.text);
        RpSendMe(t);
        break;
    }
    case RPC_FRISK:
    {
        if(!(m.job == RPJ_POLICE && m.duty)) { RpMsg(slot, RP_RED, "Police on duty only."); break; }
        int t = RpTarget(slot, c.b, 3.0f, "/frisk"); if(t < 0) break;
        RpMsg(slot, 0x93C5FDFF, "[FRISK] %s: cash %s, phone %d, licences:%s%s%s", m_Slots[t].name, RpMoney(m_Rp[t].cash, mb, sizeof(mb)), m_Rp[t].number,
              (m_Rp[t].items & RPIT_DRIVING) ? " driving" : "", (m_Rp[t].items & RPIT_GUNLIC) ? " gun" : "", (m_Rp[t].items & RPIT_JETPACK) ? " jetpack" : "");
        snprintf(line, sizeof(line), "* %s frisks %s.", nm, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    }
    case RPC_HEAL:
    {
        if(m.job != RPJ_MEDIC) { RpMsg(slot, RP_RED, "Paramedics only."); break; }
        int t = RpTarget(slot, c.b, 5.0f, "/heal"); if(t < 0) break;
        RpEv(t, RPV_HEAL, 100, 0, (uint8_t)slot, 0);
        int fee = m_Rp[t].cash >= 100 ? 100 : 0; if(fee) RpPay(t, -fee, "Medic");
        RpPay(slot, 150 + fee, "Treatment"); RpXp(slot, 1);
        snprintf(line, sizeof(line), "* %s treats %s's wounds.", nm, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    }
    case RPC_REPAIR:
    {
        if(m.job != RPJ_MECHANIC && !(m.items & RPIT_TOOLKIT)) { RpMsg(slot, RP_RED, "Mechanics (or a toolkit owner) only."); break; }
        int t = c.b == slot ? slot : RpTarget(slot, c.b, 8.0f, "/repair"); if(t < 0) break;
        RpEv(t, RPV_REPAIR, 0, 0, (uint8_t)slot, 0);
        if(t != slot && m.job == RPJ_MECHANIC) { int fee = m_Rp[t].cash >= 150 ? 150 : 0; if(fee) RpPay(t, -fee, "Mechanic"); RpPay(slot, 150 + fee, "Repair job"); RpXp(slot, 1); }
        snprintf(line, sizeof(line), "* %s repairs %s vehicle.", nm, t == slot ? "their" : (std::string(m_Slots[t].name) + "'s").c_str()); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        break;
    }
    case RPC_DEFEND:
    {
        if(m.job != RPJ_LAWYER) { RpMsg(slot, RP_RED, "Lawyers only."); break; }
        int t = RpTarget(slot, c.b, 0.0f, "/defend"); if(t < 0) break;
        if(!m_Rp[t].jailS) { RpMsg(slot, RP_RED, "%s is not in jail.", m_Slots[t].name); break; }
        if(now - m.lastEmoteAsk < 120000) { RpMsg(slot, RP_RED, "One case every 2 minutes."); break; }
        m.lastEmoteAsk = now; m_Rp[t].jailS = m_Rp[t].jailS > 60 ? m_Rp[t].jailS - 60 : 1;
        RpPay(slot, 300, "Lawyer fee"); RpXp(slot, 1);
        RpMsg(t, RP_GREEN, "[COURT] Lawyer %s got you 1 minute off your sentence.", nm);
        break;
    }

    // -------- fun / social ----------------------------------------------------------------------
    case RPC_LOTTO:
    {
        PktRpCmd b = c; b.cmd = RPC_BUY; b.a = 1; b.b = c.b;
        for(int i = 0; i < RP_SHOP_COUNT; ++i) if(g_RpShop[i].kind == RPI_LOTTO) { b.val = i; break; }
        RpHandleCmd(slot, b);
        break;
    }
    case RPC_COINFLIP:
        snprintf(line, sizeof(line), "* %s flips a coin: %s.", nm, rand() % 2 ? "heads" : "tails"); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false); break;
    case RPC_DICE:
        snprintf(line, sizeof(line), "* %s rolls a dice: %d.", nm, 1 + rand() % 6); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false); break;
    case RPC_SHARED_EMOTE:
    {
        if(c.a >= RP_EMOTE_COUNT || g_RpEmotes[c.a].cat != RPE_SHARED) break;
        int t = RpTarget(slot, c.b, 3.0f, "Shared emote"); if(t < 0) break;
        m_Rp[t].askFrom = (uint8_t)slot; m_Rp[t].askEmote = c.a; m_Rp[t].askAt = now;
        RpMsg(t, RP_SKY, "%s wants to %s with you. /accept emote (or the phone popup).", nm, g_RpEmotes[c.a].label);
        RpEv(t, RPV_TEXT, 1, c.a, (uint8_t)slot, (int32_t)RP_SKY, nullptr, "emote request");
        RpMsg(slot, RP_SKY, "Asked %s to %s.", m_Slots[t].name, g_RpEmotes[c.a].label);
        break;
    }
    case RPC_ACCEPT:
        if(c.a == 0)
        {
            int f = m.askFrom; m.askFrom = MP_INVALID_ID;
            if(f == MP_INVALID_ID || f >= MP_MAX_PLAYERS || !m_Slots[f].used || now - m.askAt > 30000) { RpMsg(slot, RP_RED, "No emote request."); break; }
            if(RpTarget(slot, f, 4.0f, "Shared emote") < 0) break;
            RpEv(slot, RPV_ITEM, m.askEmote, 1, (uint8_t)f, 0); RpEv(f, RPV_ITEM, m.askEmote, 1, (uint8_t)slot, 0);
        }
        else if(c.a == 1)
        {
            int f = m.fineFrom; int32_t amt = m.fineAsk; m.fineFrom = MP_INVALID_ID; m.fineAsk = 0;
            if(f == MP_INVALID_ID || amt <= 0) { RpMsg(slot, RP_RED, "No ticket to pay."); break; }
            if(m.bank >= amt) m.bank -= amt; else if(!RpPay(slot, -amt, "Ticket")) { RpMsg(slot, RP_RED, "You can't pay it - the officer may /arrest you."); break; }
            if(f < MP_MAX_PLAYERS && m_Slots[f].used) { RpPay(f, amt / 4, "Ticket paid"); RpMsg(f, RP_GREEN, "[MVPD] %s paid the ticket.", nm); }
            RpMsg(slot, RP_GREEN, "[MVPD] Ticket paid (%s).", RpMoney(amt, mb, sizeof(mb))); m.wanted = 0; RpSendMe(slot);
        }
        else if(c.a == 2)
        {   // 3.29: /accept marry
            int f = (int)m.proposeFrom - 1; m.proposeFrom = 0;
            if(f < 0 || f >= MP_MAX_PLAYERS || !m_Slots[f].used || !m_Rp[f].loaded || now - m.proposeAt > 30000) { RpMsg(slot, RP_RED, "Nobody proposed to you."); break; }
            if(RpTarget(slot, f, 5.0f, "Marry") < 0) break;
            if(m.spouseKey[0] || m_Rp[f].spouseKey[0]) { RpMsg(slot, RP_RED, "One of you is already married."); break; }
            if(!RpPay(f, -500, "Wedding")) { RpMsg(slot, RP_RED, "%s can't pay the $500 wedding.", m_Slots[f].name); break; }
            char k1[40], k2[40]; const char* a1 = RpKey(slot, k1, sizeof(k1)); const char* a2 = RpKey(f, k2, sizeof(k2)); if(!a1 || !a2) break;
            MP_CopyStr(m.spouseKey, a2, sizeof(m.spouseKey)); MP_CopyStr(m.spouse, m_Slots[f].name, sizeof(m.spouse));
            MP_CopyStr(m_Rp[f].spouseKey, a1, sizeof(m_Rp[f].spouseKey)); MP_CopyStr(m_Rp[f].spouse, nm, sizeof(m_Rp[f].spouse));
            RpAll(RP_SKY, "[WEDDING] %s and %s just got married. Congratulations.", m_Slots[f].name, nm);
            m_bRpDirty = true; RpSendMe(slot); RpSendMe(f);
        }
        break;
    // -------- 3.29: from Diverse Roleplay: investments, marriage, seatbelt, pawn shop ----------
    case RPC_INVEST_INFO:
    {
        RpMsg(slot, RP_SKY, "______ MEGA VIL MARKET (moves every 5 min) ______");
        for(int k = 0; k < RPN_COUNT; ++k)
        {
            long long w = (long long)(m.invU[k] * (double)m_fRpMarket[k] / 1000.0);
            RpMsg(slot, RP_WHITE, "%d. %s - price %.0f (%+.1f%%) - %s - yours: $%lld", k + 1, g_RpPlans[k].name, m_fRpMarket[k], m_nRpMarketChg[k] / 10.0f, g_RpPlans[k].risk, w);
        }
        RpMsg(slot, RP_SKY, "/invest [1-3] [amount] buys from your bank  ·  /sellinvest [1-3] [amount or all]  ·  or the ATM screen");
        break;
    }
    case RPC_INVEST:
    {
        if(now - m.lastInvest < 800) break; m.lastInvest = now;
        int k = c.a; if(k >= RPN_COUNT) { RpMsg(slot, RP_RED, "Plan 1-3 (/invest)."); break; }
        if(m.jailS) { RpMsg(slot, RP_RED, "Not from jail."); break; }
        float price = m_fRpMarket[k] < 1.0f ? 1.0f : m_fRpMarket[k];
        if(c.b == 0)
        {   // buy with bank money
            int32_t amt = c.val; if(amt < 100) { RpMsg(slot, RP_RED, "At least $100."); break; }
            if(amt > m.bank) { RpMsg(slot, RP_RED, "Not enough in the bank ($%d). /deposit at an ATM first.", m.bank); break; }
            m.bank -= amt; m.invU[k] += (int64_t)((double)amt * 1000.0 / price);
            RpMsg(slot, RP_GREEN, "[INVEST] You put %s into %s (price %.0f).", RpMoney(amt, mb, sizeof(mb)), g_RpPlans[k].name, price);
        }
        else
        {   // sell back to the bank (fee)
            if(!m.invU[k]) { RpMsg(slot, RP_RED, "You have nothing in %s.", g_RpPlans[k].name); break; }
            double worth = m.invU[k] * (double)price / 1000.0, want = c.val > 0 ? (double)c.val : worth;
            if(want > worth) want = worth;
            int64_t units = want >= worth ? m.invU[k] : (int64_t)(want * 1000.0 / price);
            int32_t fee = (int32_t)(want * g_RpPlans[k].feePct / 100.0), got = (int32_t)want - fee;
            m.invU[k] -= units; if(m.invU[k] < 0) m.invU[k] = 0; m.bank += got;
            RpMsg(slot, RP_GREEN, "[INVEST] Sold %s for %s (fee $%d) - to your bank.", g_RpPlans[k].name, RpMoney(got, mb, sizeof(mb)), fee);
        }
        m_bRpDirty = true; RpSendMe(slot);
        break;
    }
    case RPC_PROPOSE:
    {
        if(m.spouseKey[0]) { RpMsg(slot, RP_RED, "You are married to %s. /divorce first.", m.spouse); break; }
        int t = RpTarget(slot, c.b, 4.0f, "Propose"); if(t < 0) break;
        if(m_Rp[t].spouseKey[0]) { RpMsg(slot, RP_RED, "%s is already married.", m_Slots[t].name); break; }
        if(m.cash < 500) { RpMsg(slot, RP_RED, "The wedding costs $500 cash."); break; }
        m_Rp[t].proposeFrom = (uint8_t)(slot + 1); m_Rp[t].proposeAt = now;
        snprintf(line, sizeof(line), "* %s gets down on one knee in front of %s and holds out a ring.", nm, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        RpMsg(t, RP_SKY, "%s asks you to marry them. /accept marry (30 s).", nm);
        break;
    }
    case RPC_DIVORCE:
    {
        if(!m.spouseKey[0]) { RpMsg(slot, RP_RED, "You are not married."); break; }
        if(!RpPay(slot, -1000, "Divorce")) { RpMsg(slot, RP_RED, "A divorce costs $1,000 cash."); break; }
        std::string sk = m.spouseKey; char ex[24]; MP_CopyStr(ex, m.spouse, sizeof(ex));
        m.spouseKey[0] = 0; m.spouse[0] = 0;
        char kb2[40];
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
            if(m_Slots[i].used && m_Rp[i].loaded) { const char* k2 = RpKey(i, kb2, sizeof(kb2)); if(k2 && sk == k2) { m_Rp[i].spouseKey[0] = 0; m_Rp[i].spouse[0] = 0; RpMsg(i, RP_YELLOW, "[CITY HALL] %s divorced you.", nm); RpSendMe(i); } }
        auto it = m_RpSaved.find(sk); if(it != m_RpSaved.end()) { it->second.spouseKey.clear(); it->second.spouse.clear(); }
        RpMsg(slot, RP_YELLOW, "[CITY HALL] You and %s are divorced ($1,000).", ex);
        m_bRpDirty = true; RpSendMe(slot);
        break;
    }
    case RPC_BELT:
        m.belt = !m.belt;
        snprintf(line, sizeof(line), "* %s %s their seatbelt.", nm, m.belt ? "buckles" : "unbuckles"); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        RpSendMe(slot);
        break;
    case RPC_CHECK_BELT:
    {
        int t = RpTarget(slot, c.b, 6.0f, "Check seatbelt"); if(t < 0) break;
        snprintf(line, sizeof(line), "* %s looks through the window at %s's seatbelt.", nm, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        RpMsg(slot, m_Rp[t].belt ? RP_GREEN : RP_RED, "%s is %s a seatbelt.%s", m_Slots[t].name, m_Rp[t].belt ? "wearing" : "NOT wearing", m_Rp[t].belt || !(m.job == RPJ_POLICE && m.duty) ? "" : " /fine them ($150).");
        break;
    }
    case RPC_PAWN:
    {
        const Slot& sp = m_Slots[slot];
        if(!sp.hasPos || (RpPlaceNear(sp.pos, RPK_SHOP, 12.0f) < 0 && RpPlaceNear(sp.pos, RPK_TECH, 12.0f) < 0)) { RpMsg(slot, RP_RED, "Go to a 24/7 or a tech shop to pawn things (GPS app)."); break; }
        int bs = c.a; if(bs >= RP_INV_SLOTS || !m.inv[bs].id || m.inv[bs].id >= RPX_COUNT) { RpMsg(slot, RP_RED, "/pawn [bag slot 1-%d] [amount]", RP_INV_SLOTS); break; }
        int id = m.inv[bs].id, per = g_RpPawn[id]; if(per <= 0) { RpMsg(slot, RP_RED, "The pawn shop doesn't want that."); break; }
        int n = c.val < 1 ? 1 : c.val; if(n > m.inv[bs].count) n = m.inv[bs].count;
        m.inv[bs].count -= n; if(!m.inv[bs].count) m.inv[bs].id = 0;
        RpPay(slot, per * n, "Pawn shop");
        RpMsg(slot, RP_GREEN, "[PAWN] %dx %s sold for $%d.", n, g_RpInv[id].name, per * n);
        m_bRpDirty = true; RpSendInv(slot); RpSendMe(slot);
        break;
    }
    // -------- 3.26: the bag ---------------------------------------------------------------------
    case RPC_INV_REQ:
        RpSendInv(slot);
        break;
    case RPC_INV_USE:
    {
        if(now - m.lastInv < 350) break; m.lastInv = now;
        int bs = c.a; if(bs >= RP_INV_SLOTS || !m.inv[bs].id || m.inv[bs].id >= RPX_COUNT) { RpSendInv(slot); break; }
        const uint8_t id = m.inv[bs].id; const RpInvDef& d = g_RpInv[id];
        if(m.jailS && d.use != RPU_FOOD && d.use != RPU_DRINK) { RpMsg(slot, RP_RED, "Not from jail."); break; }
        int n = c.val < 1 ? 1 : c.val; if(n > m.inv[bs].count) n = m.inv[bs].count;
        switch(d.use)
        {
        case RPU_FOOD: case RPU_DRINK:
        {
            if(d.use == RPU_FOOD && m.hunger >= 100) { RpMsg(slot, RP_YELLOW, "[BAG] You are not hungry."); break; }
            if(d.use == RPU_DRINK && m.thirst >= 100) { RpMsg(slot, RP_YELLOW, "[BAG] You are not thirsty."); break; }
            // only as many as you need (the rest stays in the bag)
            int need = 1;
            for(; need < n; ++need)
            {
                int h = m.hunger + d.hunger * need, t = m.thirst + d.thirst * need;
                if((d.hunger == 0 || h >= 100) && (d.thirst == 0 || t >= 100)) break;
            }
            n = RpInvTake(m.inv, bs, need);
            m.hunger = (uint8_t)std::min(100, m.hunger + d.hunger * n); m.thirst = (uint8_t)std::min(100, m.thirst + d.thirst * n);
            char t[96]; if(n > 1) snprintf(t, sizeof(t), "%dx %s", n, d.name); else snprintf(t, sizeof(t), "%s", d.name);
            RpEv(slot, RPV_ITEM, 0, d.use == RPU_DRINK ? 2 : 0, MP_INVALID_ID, 0, nullptr, t);
            snprintf(line, sizeof(line), "* %s %s %s.", nm, d.use == RPU_DRINK ? "drinks" : "eats", t); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
            break;
        }
        case RPU_MEDKIT:
            RpInvTake(m.inv, bs, 1); RpEv(slot, RPV_HEAL, d.hunger, 0, MP_INVALID_ID, 0);
            snprintf(line, sizeof(line), "* %s opens a medkit and patches up the wounds.", nm); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
            break;
        case RPU_REPAIR:
            RpInvTake(m.inv, bs, 1); RpEv(slot, RPV_REPAIR, 0, 0, MP_INVALID_ID, 0);
            snprintf(line, sizeof(line), "* %s takes out a repair kit and fixes the vehicle.", nm); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
            break;
        case RPU_PHONE:
            break;   // the phone opens it by itself
        case RPU_STARTER:
        {
            // room for everything inside first (minus the backpack's own weight)
            RpInvSlot tmp[RP_INV_SLOTS]; memcpy(tmp, m.inv, sizeof(tmp)); RpInvTake(tmp, bs, 1);
            bool fits = true; for(const RpInvPack& g : g_RpStarter) if(RpInvAdd(tmp, g.id, g.count) < g.count) fits = false;
            if(!fits) { int all = 0; for(const RpInvPack& g : g_RpStarter) all += g.count; RpMsg(slot, RP_RED, "[BAG] Make room in your bag first (the Starter Pack has %d things inside).", all); break; }
            RpInvTake(m.inv, bs, 1);
            snprintf(line, sizeof(line), "ACTION: %s opens the Backpack and takes out the things inside.", nm); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
            for(const RpInvPack& g : g_RpStarter) RpInvGive(slot, g.id, g.count, "Received");
            break;
        }
        default: RpMsg(slot, RP_OOC_COL, "[BAG] %s can't be used.", d.name); break;
        }
        m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpSendInv(slot); RpSendMe(slot);
        break;
    }
    case RPC_INV_GIVE:
    {
        if(now - m.lastInv < 350) break; m.lastInv = now;
        int bs = c.a; if(bs >= RP_INV_SLOTS || !m.inv[bs].id || m.inv[bs].id >= RPX_COUNT) { RpSendInv(slot); break; }
        if(m.cuffed || m.jailS) { RpMsg(slot, RP_RED, "Not now."); break; }
        int t = RpTarget(slot, c.b, 4.0f, "Give"); if(t < 0) break;
        const uint8_t id = m.inv[bs].id; int n = c.val < 1 ? 1 : c.val; if(n > m.inv[bs].count) n = m.inv[bs].count;
        int room = RpInvRoom(m_Rp[t].inv, id); if(room < n) { if(room <= 0) { RpMsg(slot, RP_RED, "[BAG] %s's bag is full.", m_Slots[t].name); break; } n = room; }
        RpInvTake(m.inv, bs, n);
        char why[48]; snprintf(why, sizeof(why), "From %s", nm);
        RpInvGive(t, id, n, why);
        RpMsg(slot, RP_GREEN, "[BAG] You gave %dx %s to %s.", n, g_RpInv[id].name, m_Slots[t].name);
        snprintf(line, sizeof(line), "* %s gives %dx %s to %s.", nm, n, g_RpInv[id].name, m_Slots[t].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        RpEv(slot, RPV_ITEM, (uint8_t)RpFindEmote("give"), 1, (uint8_t)t, 0);
        m.statT = m_Rp[t].statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpSendInv(slot);
        Log("RP: %s gave %s %dx %s", nm, m_Slots[t].name, n, g_RpInv[id].name);
        break;
    }
    case RPC_INV_DROP:
    {
        if(now - m.lastInv < 350) break; m.lastInv = now;
        int bs = c.a; if(bs >= RP_INV_SLOTS || !m.inv[bs].id || m.inv[bs].id >= RPX_COUNT) { RpSendInv(slot); break; }
        const uint8_t id = m.inv[bs].id; int n = RpInvTake(m.inv, bs, c.val < 1 ? 1 : c.val);
        // 3.30: the item lands on the ground as a real object (everyone sees it, anyone can pick it up)
        const Slot& sp = m_Slots[slot];
        bool onGround = false;
        if(sp.hasPos && n > 0)
        {
            int at = -1; uint64_t oldest = ~0ull;
            for(int i = 0; i < RP_DROPS_MAX; ++i) { if(!m_RpDrops[i].id) { at = i; break; } if(m_RpDrops[i].until < oldest) { oldest = m_RpDrops[i].until; at = i; } }
            RpSrvDrop& d = m_RpDrops[at];
            d.uid = ++m_nRpDropUid; if(!d.uid) d.uid = ++m_nRpDropUid;
            d.id = id; d.count = (uint16_t)n; d.until = now + RP_DROP_LIFE_S * 1000ull;
            float ang = (float)(d.uid * 137 % 360) * 0.0174533f;   // a little in front / around the feet
            d.pos[0] = sp.pos[0] + cosf(ang) * 0.7f; d.pos[1] = sp.pos[1] + sinf(ang) * 0.7f; d.pos[2] = sp.pos[2];
            onGround = true;
            RpSendDrops(-1);
        }
        RpMsg(slot, RP_OOC_COL, onGround ? "[BAG] You dropped %dx %s on the ground (walk to it to pick it up)." : "[BAG] You dropped %dx %s.", n, g_RpInv[id].name);
        snprintf(line, sizeof(line), "* %s drops %dx %s on the ground.", nm, n, g_RpInv[id].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpSendInv(slot);
        break;
    }
    case RPC_INV_PICKUP:   // 3.30
    {
        if(now - m.lastInv < 350) break; m.lastInv = now;
        const Slot& sp = m_Slots[slot];
        int at = -1; for(int i = 0; i < RP_DROPS_MAX; ++i) if(m_RpDrops[i].id && m_RpDrops[i].uid == (uint16_t)c.val) { at = i; break; }
        if(at < 0) { RpMsg(slot, RP_RED, "Someone was faster - it's gone."); RpSendDrops(slot); break; }
        RpSrvDrop& d = m_RpDrops[at];
        if(sp.hasPos)
        {
            float dx = sp.pos[0] - d.pos[0], dy = sp.pos[1] - d.pos[1], dz = sp.pos[2] - d.pos[2];
            if(dx * dx + dy * dy + dz * dz > (RP_DROP_RANGE + 1.5f) * (RP_DROP_RANGE + 1.5f)) { RpMsg(slot, RP_RED, "Walk closer to pick it up."); break; }
        }
        const uint8_t id = d.id;
        int got = RpInvGive(slot, id, d.count, "Picked up");
        if(got <= 0) { RpMsg(slot, RP_RED, "[BAG] Your bag is full (or too heavy)."); break; }
        snprintf(line, sizeof(line), "* %s picks up %dx %s.", nm, got, g_RpInv[id].name); RpNear(slot, RP_TALK_RANGE, RP_ME_COL, line, false);
        if(got >= d.count) memset(&d, 0, sizeof(d)); else d.count = (uint16_t)(d.count - got);
        m.statT = (uint32_t)time(nullptr); m_bRpDirty = true;
        RpSendDrops(-1);
        break;
    }
    case RPC_STATS:
    {
        int into = 0; int need = RpXpForNext(m.xp, &into);
        RpMsg(slot, RP_SKY, "______ %s (id %d) ______", nm, slot);
        RpMsg(slot, RP_WHITE, "Level %d (%d / %d respect) | %s%s | played %u h %u min | phone %d", m.level, into, need, g_RpJobs[m.job].name, m.duty ? " (on duty)" : "", m.playMin / 60, m.playMin % 60, m.number);
        RpMsg(slot, RP_WHITE, "Cash %s | Bank $%d | Tasks %u | House: %s | Vehicles %d | Pets %d", RpMoney(m.cash, mb, sizeof(mb)), m.bank, m.tasks, m.house < RP_HOUSE_COUNT ? g_RpHouses[m.house].addr : "none", m.nveh, __builtin_popcount(m.pets));
        break;
    }
    case RPC_EVENT_JOIN:
        if(!m_nRpEvKind) { RpMsg(slot, RP_OOC_COL, "No event right now. Next one in %d min.", (int)((m_nRpNextEvent > now ? m_nRpNextEvent - now : 0) / 60000)); break; }
        if(m_fRpEvPos[0] != 0.0f) RpEv(slot, RPV_TARGET, 3, 0, MP_INVALID_ID, (int32_t)RP_SKY, m_fRpEvPos, g_RpEventNames[m_nRpEvKind]);
        RpMsg(slot, RP_SKY, "[EVENT] %s - %d s left.", g_RpEventNames[m_nRpEvKind], (int)((m_nRpEvEnd > now ? m_nRpEvEnd - now : 0) / 1000));
        break;
    default: break;
    }
}
