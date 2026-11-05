// 4.09: CGRP systems for the default server and Mega Vil Futuristic (net/cgrp.h). Included at the end of
// server.cpp. Everything runs with m_Mutex held (packet handler + ticks).
//   cgrp_default.txt / cgrp_megavil.txt next to the Home Kit file:
//     E <row fields>          one entity (business, gate, ...)    P <key>|...  one account
//     F <family>              one family                         X <faction> one faction (default server)
//     I <ip>                  /banip
// Money: Mega Vil = the RP cash on the server (RpPay); the default server = the phone's GTA cash
// (sent with every command / heartbeat, taken with CGA_MONEY like the Home Kit does).

static const uint32_t kCgWhite = 0xFFFFFFFF, kCgGrey = 0xB4B4B4FF, kCgRed = 0xFF4040FF, kCgGreen = 0x5BD45BFF, kCgYellow = 0xF0CC00FF,
                      kCgBlue = 0x01FCFFFF, kCgOrange = 0xFF9900FF, kCgPurple = 0xC2A2DAFF, kCgFam = 0x33CCFFFF, kCgRadio = 0x8D8DFFFF, kCgStaff = 0xFFFF91FF;
#define CG_DLG_BASE   0x6000
enum { CGD_BUY = CG_DLG_BASE + 1, CGD_OOC, CGD_ORDER, CGD_ATM, CGD_ATM_W, CGD_ATM_D, CGD_VIEWMAP, CGD_VEHMENU, CGD_TUT, CGD_BIZINT };

static int CgAtoi(const char* s) { return s ? atoi(s) : 0; }
static char* CgTok(char*& p)
{   // next word (p moves on); nullptr = none
    while(*p == ' ') ++p;
    if(!*p) return nullptr;
    char* w = p; while(*p && *p != ' ') ++p;
    if(*p) *p++ = 0;
    return w;
}
static const char* CgRest(char* p) { while(p && *p == ' ') ++p; return p ? p : ""; }
static float CgDist2(const float* a, float x, float y, float z) { float dx = a[0] - x, dy = a[1] - y, dz = a[2] - z; return dx * dx + dy * dy + dz * dz; }

bool MPServer::CgOn() const { return IsDefaultServer() || IsRpServer(); }
void MPServer::CgFillWorld(PktWorld& w) const
{
    if(!CgOn()) return;
    if(m_nCgWeather >= 0) { w.mode = WORLD_FORCED; w.weather = (uint8_t)m_nCgWeather; }
    if(m_nCgHour >= 0)
    {   // keep the minutes running, move the hour
        uint32_t day = w.absMinute / 1440, min = w.absMinute % 60;
        w.absMinute = day * 1440 + (uint32_t)m_nCgHour * 60 + min; w.hour = (uint8_t)m_nCgHour; w.minute = (uint8_t)min;
    }
}
std::string MPServer::CgFile() const
{
    std::string p = m_szHomeFile; size_t s = p.find_last_of("/\\");
    std::string dir = s == std::string::npos ? "" : p.substr(0, s + 1);
    return dir + (IsRpServer() ? "cgrp_megavil.txt" : "cgrp_default.txt");
}

// ---- file ----------------------------------------------------------------------------------------
static void CgEsc(std::string& o, const char* s) { for(; s && *s; ++s) o += (*s == '|' || *s == '\n' || *s == '\r') ? '/' : *s; }
static std::vector<std::string> CgSplit(const char* line)
{
    std::vector<std::string> v; std::string cur;
    for(const char* c = line; *c && *c != '\n' && *c != '\r'; ++c) { if(*c == '|') { v.push_back(cur); cur.clear(); } else cur += *c; }
    v.push_back(cur); return v;
}
void MPServer::CgLoad()
{
    m_bCgLoaded = true; m_Cg.clear(); m_CgP.clear(); m_CgHouseLvl.clear(); m_CgFam.clear(); m_CgFac.clear(); m_CgIpBans.clear();
    m_nCgNext = m_nCgFamNext = m_nCgFacNext = 1;
    FILE* f = fopen(CgFile().c_str(), "r"); if(!f) { Log("CGRP: new file %s", CgFile().c_str()); return; }
    char line[1024];
    while(fgets(line, sizeof(line), f))
    {
        if(line[0] == '#' || line[1] != ' ') continue;
        std::vector<std::string> v = CgSplit(line + 2);
        auto I = [&](size_t i) { return i < v.size() ? atoi(v[i].c_str()) : 0; };
        auto F = [&](size_t i) { return i < v.size() ? (float)atof(v[i].c_str()) : 0.0f; };
        auto S = [&](size_t i) { return i < v.size() ? v[i] : std::string(); };
        if(line[0] == 'E' && v.size() >= 30)
        {
            CgEnt e; memset(&e.r, 0, sizeof(e.r));
            e.r.id = (uint16_t)I(0); e.r.kind = (uint8_t)I(1); e.r.flags = (uint8_t)(I(2) & (CGF_LOCKED | CGF_VIP | CGF_SALE)); e.r.sub = (uint8_t)I(3);
            e.r.area = (uint8_t)I(4); e.r.iarea = (uint8_t)I(5); e.r.model = (uint16_t)I(6); e.r.group = (uint16_t)I(7); e.r.level = (uint16_t)I(8); e.r.vw = (uint16_t)I(9);
            e.r.price = I(10); e.r.val = I(11);
            e.r.x = F(12); e.r.y = F(13); e.r.z = F(14); e.r.a = F(15); e.r.ix = F(16); e.r.iy = F(17); e.r.iz = F(18); e.r.ia = F(19);
            MP_CopyStr(e.r.name, S(20).c_str(), sizeof(e.r.name)); MP_CopyStr(e.r.owner, S(21).c_str(), sizeof(e.r.owner)); MP_CopyStr(e.r.text, S(22).c_str(), sizeof(e.r.text));
            e.ownerKey = S(23); e.till = I(24); e.fee = I(25);
            if(!e.r.id || e.r.kind == CGK_NONE || e.r.kind >= CGK_COUNT) continue;
            m_Cg.push_back(e); if(e.r.id >= m_nCgNext) m_nCgNext = e.r.id + 1;
        }
        else if(line[0] == 'P' && v.size() >= 16)
        {
            CgPlayer p; p.name = S(1); p.gold = I(2); p.bank = I(3); p.family = (uint16_t)I(4); p.famRank = (uint8_t)I(5); p.faction = (uint16_t)I(6); p.facRank = (uint8_t)I(7);
            p.level = (uint8_t)I(8); p.helper = (uint8_t)I(9); p.irc = (uint8_t)I(10); p.hasMap = (uint8_t)I(11); p.warns = I(12); p.jailS = I(13); p.minutes = I(14); p.motel = (uint16_t)I(15);
            p.paydays = (uint8_t)I(16); p.accent = S(17);
            m_CgP[S(0)] = p;
        }
        else if(line[0] == 'F' && v.size() >= 5)
        {
            CgFamily fm; fm.id = (uint16_t)I(0); fm.name = S(1); fm.leader = S(2); fm.color = (uint32_t)strtoul(S(3).c_str(), nullptr, 16); fm.safe = I(4);
            if(fm.id) { m_CgFam.push_back(fm); if(fm.id >= m_nCgFamNext) m_nCgFamNext = fm.id + 1; }
        }
        else if(line[0] == 'X' && v.size() >= 10)
        {
            CgFaction fc; fc.id = (uint16_t)I(0); fc.name = S(1); fc.leader = S(2); fc.type = (uint8_t)I(3);
            for(int k = 1; k <= 6; ++k) fc.pay[k] = I(3 + k);
            if(fc.id) { m_CgFac.push_back(fc); if(fc.id >= m_nCgFacNext) m_nCgFacNext = fc.id + 1; }
        }
        else if(line[0] == 'I') m_CgIpBans.push_back(S(0));
        else if(line[0] == 'H' && v.size() >= 2) m_CgHouseLvl[(uint16_t)I(0)] = I(1);
    }
    fclose(f);
    Log("CGRP: %d things, %d accounts, %d families, %d factions (%s)", (int)m_Cg.size(), (int)m_CgP.size(), (int)m_CgFam.size(), (int)m_CgFac.size(), CgFile().c_str());
}
void MPServer::CgSave()
{
    std::string path = CgFile(), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
    fprintf(f, "# CGRP systems (MP %s): E entity, P account, F family, X faction, I banned ip\n", MP_VERSION_STR);
    for(const auto& e : m_Cg)
    {
        std::string n, o, t, k; CgEsc(n, e.r.name); CgEsc(o, e.r.owner); CgEsc(t, e.r.text); CgEsc(k, e.ownerKey.c_str());
        fprintf(f, "E %u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%d|%d|%.3f|%.3f|%.3f|%.2f|%.3f|%.3f|%.3f|%.2f|%s|%s|%s|%s|%d|%d|0|0|0|0\n",
                e.r.id, e.r.kind, e.r.flags & (CGF_LOCKED | CGF_VIP | CGF_SALE), e.r.sub, e.r.area, e.r.iarea, e.r.model, e.r.group, e.r.level, e.r.vw, e.r.price, e.r.val,
                e.r.x, e.r.y, e.r.z, e.r.a, e.r.ix, e.r.iy, e.r.iz, e.r.ia, n.c_str(), o.c_str(), t.c_str(), k.c_str(), e.till, e.fee);
    }
    for(const auto& kv : m_CgP)
    {
        const CgPlayer& p = kv.second; std::string k, n, a; CgEsc(k, kv.first.c_str()); CgEsc(n, p.name.c_str()); CgEsc(a, p.accent.c_str());
        fprintf(f, "P %s|%s|%d|%d|%u|%u|%u|%u|%u|%u|%u|%u|%d|%d|%d|%u|%u|%s\n", k.c_str(), n.c_str(), p.gold, p.bank, p.family, p.famRank, p.faction, p.facRank,
                p.level, p.helper, p.irc, p.hasMap, p.warns, p.jailS, p.minutes, p.motel, p.paydays, a.c_str());
    }
    for(const auto& fm : m_CgFam) { std::string n, l; CgEsc(n, fm.name.c_str()); CgEsc(l, fm.leader.c_str()); fprintf(f, "F %u|%s|%s|%08X|%d\n", fm.id, n.c_str(), l.c_str(), fm.color, fm.safe); }
    for(const auto& fc : m_CgFac)
    {
        std::string n, l; CgEsc(n, fc.name.c_str()); CgEsc(l, fc.leader.c_str());
        fprintf(f, "X %u|%s|%s|%u|%d|%d|%d|%d|%d|%d\n", fc.id, n.c_str(), l.c_str(), fc.type, fc.pay[1], fc.pay[2], fc.pay[3], fc.pay[4], fc.pay[5], fc.pay[6]);
    }
    for(const auto& ip : m_CgIpBans) fprintf(f, "I %s\n", ip.c_str());
    for(const auto& kv : m_CgHouseLvl) if(kv.second > 0) fprintf(f, "H %u|%d\n", kv.first, kv.second);
    fclose(f); rename(tmp.c_str(), path.c_str());
    m_bCgDirty = false;
}

// ---- small helpers -------------------------------------------------------------------------------
const char* MPServer::CgKey(int slot, char* buf, size_t n) const { return HomeKey(slot, buf, n); }
MPServer::CgPlayer& MPServer::CgP(int slot)
{
    char kb[64]; CgPlayer& p = m_CgP[CgKey(slot, kb, sizeof(kb))];
    if(p.name != m_Slots[slot].name) { p.name = m_Slots[slot].name; m_bCgDirty = true; }
    return p;
}
int MPServer::CgLevel(int slot)
{
    const Slot& s = m_Slots[slot];
    if(s.admin) return 9;
    if(!s.mod) return 0;
    int l = CgP(slot).level;
    if(l <= 0) l = s.granted ? 1 : 8;   // moderator.txt staff = Executive Administrator, a /makeadmin [MOD] = Server Moderator
    return l > 9 ? 9 : l;
}
int MPServer::CgPlayLevel(int slot)
{
    if(IsRpServer() && m_Rp[slot].loaded) return m_Rp[slot].level;
    return 1 + CgP(slot).minutes / 120;   // the default server: a level every 2 hours played
}
bool MPServer::CgIsVip(int slot) { return m_Slots[slot].vip || m_Slots[slot].mod || m_Slots[slot].admin; }
void MPServer::CgTell(int slot, uint32_t col, const char* fmt, ...)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = col;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    if(slot >= 0) { if(m_Slots[slot].used) SendTo(slot, &m, sizeof(m)); }
    else Broadcast(&m, sizeof(m));
}
void MPServer::CgAct(int slot, uint8_t act, int a, int b, int32_t val, const float* pos, float h, const char* text)
{
    PktCgAct p; memset(&p, 0, sizeof(p)); p.type = PKT_CG_ACT; p.act = act; p.a = (uint8_t)a; p.b = (uint8_t)b; p.val = val;
    if(pos) { p.pos[0] = pos[0]; p.pos[1] = pos[1]; p.pos[2] = pos[2]; } p.pos[3] = h;
    MP_CopyStr(p.text, text ? text : "", sizeof(p.text));
    if(slot >= 0) { if(!m_Slots[slot].used) return; if(act == CGA_TELE || act == CGA_HEALTH || act == CGA_ARMOUR || act == CGA_MONEY || act == CGA_SETMONEY || act == CGA_GUN || act == CGA_SPAWNCAR || act == CGA_CARTELE || act == CGA_PARACHUTE) AcGrace(slot, 6000); SendTo(slot, &p, sizeof(p)); }
    else for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) { AcGrace(i, 6000); SendTo(i, &p, sizeof(p)); }
}
void MPServer::CgDlg(int slot, uint16_t id, uint8_t style, const char* caption, const char* info, const char* b1, const char* b2)
{
    PktDialog d; memset(&d, 0, sizeof(d)); d.type = PKT_DIALOG; d.style = style; d.dialogId = id;
    MP_CopyStr(d.caption, caption, sizeof(d.caption)); MP_CopyStr(d.button1, b1 ? b1 : "OK", sizeof(d.button1)); MP_CopyStr(d.button2, b2 ? b2 : "", sizeof(d.button2));
    MP_CopyStr(d.info, info, sizeof(d.info));
    SendTo(slot, &d, sizeof(d));
}
bool MPServer::CgPay(int slot, int32_t amount, const char* why)
{
    if(amount <= 0) return true;
    if(IsRpServer()) { if(!m_Rp[slot].loaded) return false; return RpPay(slot, -amount, why); }
    CgSlot& c = m_CgS[slot];
    if(c.cash < amount) return false;
    c.cash -= amount; CgAct(slot, CGA_MONEY, 0, 0, -amount);
    return true;
}
void MPServer::CgGive(int slot, int32_t amount, const char* why)
{
    if(!amount) return;
    if(IsRpServer()) { if(m_Rp[slot].loaded) RpPay(slot, amount, why); return; }
    m_CgS[slot].cash += amount; CgAct(slot, CGA_MONEY, 0, 0, amount);
}
MPServer::CgFamily* MPServer::CgFam(int id) { for(auto& f : m_CgFam) if(f.id == id) return &f; return nullptr; }
MPServer::CgFaction* MPServer::CgFac(int id) { for(auto& f : m_CgFac) if(f.id == id) return &f; return nullptr; }
bool MPServer::CgMine(int slot, const CgEnt& e) { return !e.ownerKey.empty() && HomeOwns(slot, e.ownerKey.c_str()); }
bool MPServer::CgMember(int slot, const CgEnt& e)
{
    if(e.r.sub == CGG_PUBLIC) return true;
    if(e.r.sub == CGG_PLAYER) return CgMine(slot, e);
    if(e.r.sub == CGG_FAMILY) return e.r.group && CgP(slot).family == e.r.group;
    if(e.r.sub == CGG_FACTION)
    {
        if(IsRpServer()) return m_Rp[slot].loaded && m_Rp[slot].fac[0] && !strcasecmp(m_Rp[slot].fac, e.r.owner);
        return e.r.group && CgP(slot).faction == e.r.group;
    }
    return false;
}
MPServer::CgEnt* MPServer::CgGet(int id, int kind)
{
    for(auto& e : m_Cg) if(e.r.id == id && (!kind || e.r.kind == kind)) return &e;
    return nullptr;
}
int MPServer::CgNear(int slot, int kind, float range, bool inside, int sub)
{
    const CgSlot& c = m_CgS[slot]; int best = -1; float bd = range * range;
    for(size_t i = 0; i < m_Cg.size(); ++i)
    {
        const CgRow& r = m_Cg[i].r;
        if(kind && r.kind != kind) continue;
        if(sub >= 0 && r.sub != sub) continue;
        float d = inside ? CgDist2(c.pos, r.ix, r.iy, r.iz) : CgDist2(c.pos, r.x, r.y, r.z);
        if(inside && !(r.ix || r.iy || r.iz)) continue;
        if(d < bd) { bd = d; best = (int)i; }
    }
    return best;
}
MPServer::CgEnt& MPServer::CgNew(int slot, int kind)
{
    CgEnt e; memset(&e.r, 0, sizeof(e.r));
    e.r.id = m_nCgNext++; e.r.kind = (uint8_t)kind;
    const CgSlot& c = m_CgS[slot];
    e.r.x = c.pos[0]; e.r.y = c.pos[1]; e.r.z = c.pos[2]; e.r.a = c.heading; e.r.area = c.area; e.r.vw = c.vw;
    m_Cg.push_back(e); m_bCgDirty = true;
    return m_Cg.back();
}
void MPServer::CgChanged(CgEnt& e) { m_bCgDirty = true; CgSendOne(e, CGO_SET); }
void MPServer::CgDel(int id)
{
    for(size_t i = 0; i < m_Cg.size(); ++i)
        if(m_Cg[i].r.id == id) { CgEnt e = m_Cg[i]; m_Cg.erase(m_Cg.begin() + i); m_bCgDirty = true; CgSendOne(e, CGO_DEL); return; }
}
int MPServer::CgFindPlayer(const char* arg) const
{
    if(!arg || !*arg) return -1;
    bool num = true; for(const char* c = arg; *c; ++c) if(!isdigit((unsigned char)*c)) num = false;
    if(num) { int id = atoi(arg); return id >= 0 && id < MP_MAX_PLAYERS && m_Slots[id].used ? id : -1; }
    int hit = -1;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used) continue;
        if(!strcasecmp(m_Slots[i].name, arg)) return i;
        char a[64], b[64]; MP_CopyStr(a, m_Slots[i].name, sizeof(a)); MP_CopyStr(b, arg, sizeof(b));
        for(char* c = a; *c; ++c) *c = (char)tolower((unsigned char)*c);
        for(char* c = b; *c; ++c) *c = (char)tolower((unsigned char)*c);
        if(strstr(a, b)) { if(hit >= 0) return -1; hit = i; }
    }
    return hit;
}
void MPServer::CgFamMsg(int fam, uint32_t col, const char* text)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && (CgP(i).family == fam || m_CgS[i].bigears)) CgTell(i, col, "%s", text);
}
void MPServer::CgFacMsg(int fac, uint32_t col, const char* text)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && ((fac < 0 ? CgP(i).faction != 0 : CgP(i).faction == fac) || m_CgS[i].bigears)) CgTell(i, col, "%s", text);
}
void MPServer::CgStaffMsg(uint32_t col, const char* text)
{
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && (CgLevel(i) > 0 || CgP(i).irc)) CgTell(i, col, "%s", text);
}
void MPServer::CgRecolor(int slot, uint32_t color)
{
    Slot& s = m_Slots[slot]; s.color = color;
    PktPlayerJoin pj; memset(&pj, 0, sizeof(pj));
    pj.type = PKT_PLAYER_JOIN; pj.id = (uint8_t)slot; pj.color = s.color; pj.skin = s.skin;
    MP_CopyStr(pj.name, s.name, MP_NAME_LEN);
    Broadcast(&pj, sizeof(pj), -1);
}

// ---- sending -------------------------------------------------------------------------------------
void MPServer::CgRowFor(int slot, const CgEnt& e, CgRow& out)
{
    out = e.r;
    out.flags = (uint8_t)(e.r.flags & (CGF_LOCKED | CGF_VIP | CGF_SALE | CGF_OPEN | CGF_INUSE));
    if(slot >= 0 && CgMine(slot, e)) out.flags |= CGF_MINE;
    if(slot >= 0 && e.r.sub != CGG_PUBLIC && CgMember(slot, e)) out.flags |= CGF_MEMBER;
    if(e.r.kind == CGK_BIZ) out.val = e.r.val;   // products
}
void MPServer::CgSendAll(int slot)
{
    PktCgEnt b; memset(&b, 0, sizeof(b)); b.type = PKT_CG_ENT; b.op = CGO_LIST; b.total = (uint16_t)m_Cg.size();
    auto flush = [&](bool force) {
        if(!b.n && !force) return;
        SendTo(slot, &b, (int)(offsetof(PktCgEnt, r) + sizeof(CgRow) * b.n));
        b.n = 0; b.op = CGO_MORE;
    };
    for(const auto& e : m_Cg) { CgRowFor(slot, e, b.r[b.n++]); if(b.n == CG_PAGE) flush(false); }
    flush(b.op == CGO_LIST);
}
void MPServer::CgSendOne(const CgEnt& e, uint8_t op)
{
    PktCgEnt b; memset(&b, 0, sizeof(b)); b.type = PKT_CG_ENT; b.op = op; b.n = 1; b.total = (uint16_t)m_Cg.size();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_CgS[i].beat) continue;
        CgRowFor(i, e, b.r[0]);
        SendTo(i, &b, (int)(offsetof(PktCgEnt, r) + sizeof(CgRow)));
    }
}
void MPServer::CgSendMe(int slot)
{
    CgPlayer& p = CgP(slot); const CgSlot& c = m_CgS[slot];
    PktCgMe m; memset(&m, 0, sizeof(m)); m.type = PKT_CG_ME;
    m.level = (uint8_t)CgLevel(slot); m.helper = p.helper; m.family = p.family; m.famRank = p.famRank; m.vip = CgIsVip(slot) ? 1 : 0;
    m.flags = (uint8_t)((c.frozen ? 1 : 0) | (m_Slots[slot].muted ? 2 : 0) | (p.hasMap ? 4 : 0) | (c.duty ? 8 : 0) | (c.masked ? 16 : 0));
    m.jailMin = (uint8_t)(p.jailS > 0 ? (p.jailS + 59) / 60 > 255 ? 255 : (p.jailS + 59) / 60 : 0);
    m.gold = p.gold; m.bank = IsRpServer() ? m_Rp[slot].bank : p.bank; m.cookies = m_Slots[slot].cookies; m.insideId = c.inside; m.vw = c.vw;
    if(CgFamily* f = CgFam(p.family)) MP_CopyStr(m.famName, f->name.c_str(), sizeof(m.famName));
    if(IsRpServer()) { if(m_Rp[slot].fac[0]) MP_CopyStr(m.facName, m_Rp[slot].fac, sizeof(m.facName)); }
    else if(CgFaction* f = CgFac(p.faction)) { m.faction = p.faction; m.facRank = p.facRank; MP_CopyStr(m.facName, f->name.c_str(), sizeof(m.facName)); }
    SendTo(slot, &m, sizeof(m));
}

// ---- join / leave / tick -------------------------------------------------------------------------
void MPServer::CgOnJoin(int slot)
{
    if(!CgOn()) return;
    if(!m_bCgLoaded) CgLoad();
    m_CgS[slot] = CgSlot();
    char ip[32]; snprintf(ip, sizeof(ip), "%s", inet_ntoa(m_Slots[slot].addr.sin_addr));
    for(const auto& b : m_CgIpBans)
        if(!b.empty() && b.compare(0, 5, "acct:") && !strncmp(ip, b.c_str(), b.size()) && (b.size() == strlen(ip) || b.back() == '.'))
        { KickLocked(slot, "Server", "IP banned", false); return; }
}
void MPServer::CgOnLeave(int slot)
{
    if(!CgOn()) return;
    CgSaleClear(slot);
    for(auto& e : m_Cg)
    {
        if(e.capSlot == slot) { e.capSlot = -1; e.capStart = 0; }
        if(e.usedBy == slot) { e.usedBy = -1; e.r.flags &= ~CGF_INUSE; CgSendOne(e, CGO_SET); }
    }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_CgS[i].saleFrom == slot) m_CgS[i].saleFrom = -1;
    m_CgS[slot] = CgSlot();
}
void MPServer::CgTick(uint64_t now)
{
    if(!CgOn()) return;
    if(!m_bCgLoaded) CgLoad();
    if(!m_nCgPaydayAt) m_nCgPaydayAt = now + 3600000ULL;
    if(now >= m_nCgPaydayAt) { m_nCgPaydayAt = now + 3600000ULL; CgPayday(); }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_CgS[i].beat) continue;
        CgSlot& c = m_CgS[i];
        if(!c.lastMin) c.lastMin = now;
        if(now - c.lastMin >= 60000) { c.lastMin = now; ++CgP(i).minutes; m_bCgDirty = true; }
        CgPlayer& p = CgP(i);
        if(p.jailS > 0 && !IsRpServer())
        {   // the admin prison (Mega Vil uses its own jail)
            static thread_local uint64_t jt[MP_MAX_PLAYERS] = {};
            if(now - jt[i] >= 1000) { jt[i] = now; if(--p.jailS <= 0) { p.jailS = 0; CgAct(i, CGA_JAIL, 0, 0, 0); CgTell(i, kCgGreen, "[PRISON] You are free. Behave."); CgSendMe(i); } m_bCgDirty = true; }
        }
        if(c.saleFrom >= 0 && now - c.saleAt > 60000) { CgTell(i, kCgGrey, "The car offer ran out."); c.saleFrom = -1; }
    }
    for(auto& e : m_Cg)
    {
        if(e.r.kind == CGK_GATE && (e.r.flags & CGF_OPEN) && e.openUntil && now >= e.openUntil) { e.r.flags &= ~CGF_OPEN; e.openUntil = 0; CgSendOne(e, CGO_SET); }
        if(e.r.kind == CGK_POINT && e.capSlot >= 0)
        {
            int s = e.capSlot;
            if(!m_Slots[s].used || CgDist2(m_CgS[s].pos, e.r.x, e.r.y, e.r.z) > 30.0f * 30.0f) { if(m_Slots[s].used) CgTell(s, kCgRed, "[POINT] You left %s - the capture failed.", e.r.name); e.capSlot = -1; e.capStart = 0; continue; }
            const uint64_t need = (uint64_t)(e.r.val > 0 ? e.r.val : 10) * 60000ULL;
            if(now - e.capStart >= need)
            {
                CgFamily* f = CgFam(CgP(s).family);
                e.r.group = f ? f->id : 0; MP_CopyStr(e.r.owner, f ? f->name.c_str() : "", sizeof(e.r.owner));
                e.capSlot = -1; e.capStart = 0; CgChanged(e);
                CgTell(-1, kCgYellow, "[POINT] %s captured %s for %s.", m_Slots[s].name, e.r.name, f ? f->name.c_str() : "nobody");
            }
        }
    }
    if(m_bCgDirty && now >= m_nCgSaveAt) { m_nCgSaveAt = now + 20000; CgSave(); }
}
void MPServer::CgPayday()
{
    CgTell(-1, kCgWhite, "|___ PAYDAY (CGRP) ___|");
    for(auto& e : m_Cg)
        if(e.r.kind == CGK_POINT && e.r.group)
            if(CgFamily* f = CgFam(e.r.group)) { f->safe += e.r.price > 0 ? e.r.price : 5000; m_bCgDirty = true; }
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_CgS[i].beat) continue;
        CgPlayer& p = CgP(i);
        int32_t pay = 0, rent = 0, interest = 0;
        if(!IsRpServer())
        {
            if(CgFaction* f = CgFac(p.faction)) pay = f->pay[p.facRank >= 1 && p.facRank <= 6 ? p.facRank : 1];
            if(pay <= 0 && p.faction) pay = 500;
            interest = p.bank > 0 ? (int32_t)((int64_t)p.bank / 1000) : 0; if(interest > 5000) interest = 5000;
            p.bank += pay + interest;
        }
        if(p.motel) if(CgEnt* m = CgGet(p.motel, CGK_MOTEL))
        {
            rent = m->r.price > 0 ? m->r.price : 50;
            if(IsRpServer()) { if(!RpPay(i, -rent, "Motel rent")) { p.motel = 0; CgTell(i, kCgRed, "You could not pay the motel rent - you lost the room."); } }
            else if(p.bank >= rent) p.bank -= rent; else { p.motel = 0; CgTell(i, kCgRed, "You could not pay the motel rent - you lost the room."); }
        }
        if(++p.paydays >= 3) { p.paydays = 0; ++p.gold; CgTell(i, kCgYellow, "+1 gold bar (every 3 paydays). /oocshop"); }
        if(!IsRpServer()) CgTell(i, kCgGrey, "Paycheck: $%d  Interest: $%d  Rent: $%d  Bank: $%d", pay, interest, rent, p.bank);
        else if(rent) CgTell(i, kCgGrey, "Motel rent: $%d", rent);
        m_bCgDirty = true; CgSendMe(i);
    }
}

// the PKT_SYNC hook: speed cameras
void MPServer::CgSync(int slot, const SyncData& d)
{
    m_CgHp[slot].hp = d.health; m_CgHp[slot].ar = d.armour; m_CgHp[slot].weapon = d.weapon;
    CgSlot& c = m_CgS[slot];
    c.pos[0] = d.pos[0]; c.pos[1] = d.pos[1]; c.pos[2] = d.pos[2]; c.heading = d.heading;
    if(!(d.flags & SYNC_IN_CAR)) return;
    const uint64_t now = MP_NowMs();
    if(now - c.camAt < 20000) return;
    const float kmh = d.speed * 3.6f;
    for(const auto& e : m_Cg)
    {
        if(e.r.kind != CGK_SPEEDCAM) continue;
        const int lim = e.r.val > 0 ? e.r.val : 90;
        if(kmh <= lim + 5 || CgDist2(d.carPos, e.r.x, e.r.y, e.r.z) > 18.0f * 18.0f) continue;
        c.camAt = now;
        const int32_t fine = e.r.price > 0 ? e.r.price : 500;
        bool paid = CgPay(slot, fine, "Speed camera");
        if(!paid && !IsRpServer()) { CgPlayer& p = CgP(slot); p.bank -= fine; m_bCgDirty = true; paid = true; }
        CgAct(slot, CGA_GAMETEXT, 1, 0, 3000, nullptr, 0, "~r~SPEED CAMERA");
        CgTell(slot, kCgRed, "[SPEED CAMERA] %d km/h in a %d zone - fine $%d.", (int)kmh, lim, fine);
        return;
    }
}

// ---- money to an owner who may be offline ----------------------------------------------------------
void MPServer::CgPayKey(const std::string& key, int32_t amount)
{
    if(key.empty() || !amount) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || !m_CgS[i].beat) continue;
        char kb[64]; if(key == CgKey(i, kb, sizeof(kb))) { CgGive(i, amount, "CGRP"); return; }
    }
    m_CgP[key].bank += amount; m_bCgDirty = true;   // the default server: the CGRP bank. Mega Vil: paid out as cash at the next login
}
void MPServer::CgWorldNow() { PktWorld w; FillWorld(w); Broadcast(&w, sizeof(w)); }
void MPServer::CgJail(int t, int minutes, const char* who)
{
    if(minutes < 0) minutes = 0; if(minutes > 600) minutes = 600;
    if(IsRpServer() && m_Rp[t].loaded)
    {
        m_Rp[t].jailS = (uint16_t)(minutes * 60 > 65000 ? 65000 : minutes * 60); m_Rp[t].cuffed = false; m_Rp[t].jobOn = false; m_Rp[t].duty = false; m_bRpDirty = true;
        RpEv(t, RPV_JAIL, 0, 0, MP_INVALID_ID, m_Rp[t].jailS);
    }
    else
    {
        CgP(t).jailS = minutes * 60; m_bCgDirty = true;
        static const float cell[3] = { 264.0f, 77.5f, 1001.04f };
        CgAct(t, CGA_JAIL, 6, 0, minutes * 60, cell, 270.0f);
    }
    if(minutes) CgTell(-1, kCgRed, "AdmCmd: %s was put in the admin prison by %s for %d minutes.", m_Slots[t].name, who, minutes);
    else CgTell(t, kCgGreen, "You were released by %s.", who);
    CgSendMe(t);
}

// ---- the packet --------------------------------------------------------------------------------------
void MPServer::CgHandle(int slot, PktCgCmd& c)
{
    if(!CgOn()) return;
    if(!m_bCgLoaded) CgLoad();
    c.text[sizeof(c.text) - 1] = 0;
    CgSlot& s = m_CgS[slot];
    const bool first = !s.beat;
    s.beat = true; s.area = c.area; s.inCar = c.inCar; s.carModel = c.carModel; s.speed = c.speed;
    if(!IsRpServer()) s.cash = c.cash;
    if(std::isfinite(c.pos[0]) && std::isfinite(c.pos[1]) && std::isfinite(c.pos[2])) { s.pos[0] = c.pos[0]; s.pos[1] = c.pos[1]; s.pos[2] = c.pos[2]; s.heading = c.heading; }
    if(first)
    {
        if(m_Slots[slot].account[0])
        {
            std::string acct = std::string("acct:") + m_Slots[slot].account; for(auto& ch : acct) ch = (char)tolower((unsigned char)ch);
            for(const auto& b : m_CgIpBans) if(b == acct) { KickLocked(slot, "Server", "Account banned", false); return; }
        }
        CgSendAll(slot); CgSendMe(slot);
        CgPlayer& p = CgP(slot);
        if(p.jailS > 0 && !IsRpServer()) { static const float cell[3] = { 264.0f, 77.5f, 1001.04f }; CgAct(slot, CGA_JAIL, 6, 0, p.jailS, cell, 270.0f); CgTell(slot, kCgRed, "You still have %d minutes in the admin prison.", (p.jailS + 59) / 60); }
        if(IsRpServer() && p.bank > 0 && m_Rp[slot].loaded) { int32_t b = p.bank; p.bank = 0; m_bCgDirty = true; RpPay(slot, b, "CGRP sales"); CgTell(slot, kCgGreen, "You got $%d from sales while you were away.", b); }
        if(m_nCgWeather >= 0 || m_nCgHour >= 0) { PktWorld w; FillWorld(w); SendTo(slot, &w, sizeof(w)); }
    }
    if(c.op == CGC_BEAT) return;
    if(c.op == CGC_EDITOR) { CgEditorObj(slot, c); return; }
    if(c.op != CGC_TEXT) return;
    char line[128]; MP_CopyStr(line, c.text, sizeof(line));
    char* p = line; if(*p == '/') ++p;
    char* cmd = CgTok(p); if(!cmd) return;
    for(char* q = cmd; *q; ++q) *q = (char)tolower((unsigned char)*q);
    CgCommand(slot, cmd, p, c);
}

// ---- doors -------------------------------------------------------------------------------------------
static bool CgHasInside(const CgRow& r) { return r.ix || r.iy || r.iz; }
static bool CgDoorKind(int k) { return k == CGK_BIZ || k == CGK_GARAGE || k == CGK_MOTEL || k == CGK_HQ || k == CGK_ENTRANCE || k == CGK_BUILDING; }
void MPServer::CgEnter(int slot)
{
    CgSlot& s = m_CgS[slot]; int best = -1; float bd = 3.5f * 3.5f;
    for(size_t i = 0; i < m_Cg.size(); ++i)
    {
        const CgRow& r = m_Cg[i].r; if(!CgDoorKind(r.kind) || !CgHasInside(r)) continue;
        float range = r.kind == CGK_GARAGE ? 7.0f : 3.5f;
        float d = CgDist2(s.pos, r.x, r.y, r.z); if(d < range * range && d < bd + (r.kind == CGK_GARAGE ? 40.0f : 0.0f)) { bd = d; best = (int)i; }
    }
    if(best < 0) { CgTell(slot, kCgGrey, "There is no door near you."); return; }
    CgEnt& e = m_Cg[best]; const CgRow& r = e.r;
    const bool staff = CgLevel(slot) >= 1;
    if((r.flags & CGF_LOCKED) && !CgMine(slot, e) && !(r.sub != CGG_PUBLIC && (r.kind == CGK_HQ) && CgMember(slot, e)) && !staff) { CgAct(slot, CGA_GAMETEXT, 0, 0, 2500, nullptr, 0, "~r~Locked"); return; }
    if((r.flags & CGF_VIP) && !CgIsVip(slot)) { CgTell(slot, kCgRed, "VIP lounge - VIP only."); return; }
    if(r.kind == CGK_HQ && !CgMember(slot, e) && !staff) { CgTell(slot, kCgRed, "This HQ is for members only."); return; }
    if(r.kind == CGK_MOTEL && CgP(slot).motel != r.id && !staff) { CgTell(slot, kCgRed, "Rent a room first: /rentroom ($%d a payday).", r.price > 0 ? r.price : 50); return; }
    if(r.kind == CGK_BIZ && r.price > 0 && e.fee > 0 && !CgMine(slot, e))
    {
        if(!CgPay(slot, e.fee, "Entrance fee")) { CgTell(slot, kCgRed, "The entrance fee is $%d.", e.fee); return; }
        e.till += e.fee; m_bCgDirty = true;
    }
    float pos[3] = { r.ix, r.iy, r.iz };
    s.inside = r.id; s.vw = r.vw;
    CgAct(slot, CGA_TELE, r.iarea, r.kind == CGK_GARAGE && s.inCar ? 1 : 0, 0, pos, r.ia);
    if(r.kind == CGK_BIZ) CgTell(slot, kCgWhite, "Welcome to %s. /buy to shop%s.", r.name, CgMine(slot, e) ? ", /bizinfo for your business" : "");
    else if(r.name[0]) CgAct(slot, CGA_GAMETEXT, 0, 0, 2500, nullptr, 0, r.name);
    CgSendMe(slot);
}
void MPServer::CgExit(int slot)
{
    CgSlot& s = m_CgS[slot];
    int best = -1; float bd = 4.0f * 4.0f;
    for(size_t i = 0; i < m_Cg.size(); ++i)
    {
        const CgRow& r = m_Cg[i].r; if(!CgDoorKind(r.kind) || !CgHasInside(r)) continue;
        float range = r.kind == CGK_GARAGE ? 9.0f : 4.0f;
        float d = CgDist2(s.pos, r.ix, r.iy, r.iz); if(d < range * range && (best < 0 || d < bd)) { bd = d; best = (int)i; }
    }
    if(best < 0 && s.inside) { CgEnt* e = CgGet(s.inside); if(e) for(size_t i = 0; i < m_Cg.size(); ++i) if(&m_Cg[i] == e) best = (int)i; }
    if(best < 0) { CgTell(slot, kCgGrey, "You are not at an exit."); return; }
    const CgRow& r = m_Cg[best].r;
    float pos[3] = { r.x, r.y, r.z };
    s.inside = 0; s.vw = 0;
    CgAct(slot, CGA_TELE, r.area, r.kind == CGK_GARAGE && s.inCar ? 1 : 0, 0, pos, r.a + 180.0f);
    CgSendMe(slot);
}

// ---- gates -------------------------------------------------------------------------------------------
void MPServer::CgGate(int slot, int group, bool admin)
{
    int best = -1; float bd = 14.0f * 14.0f;
    for(size_t i = 0; i < m_Cg.size(); ++i)
    {
        const CgEnt& e = m_Cg[i]; if(e.r.kind != CGK_GATE) continue;
        if(group >= 0 && e.r.sub != group) continue;
        if(!admin && !CgMember(slot, e)) continue;
        float d = CgDist2(m_CgS[slot].pos, e.r.x, e.r.y, e.r.z); if(d < bd) { bd = d; best = (int)i; }
    }
    if(best < 0) { CgTell(slot, kCgGrey, admin ? "There is no gate near you." : "There is no gate of yours near you."); return; }
    CgEnt& e = m_Cg[best];
    if(e.r.flags & CGF_OPEN) { e.r.flags &= ~CGF_OPEN; e.openUntil = 0; }
    else { e.r.flags |= CGF_OPEN; e.openUntil = MP_NowMs() + 9000; }
    CgSendOne(e, CGO_SET);
    CgAct(slot, CGA_GAMETEXT, 1, 0, 1500, nullptr, 0, (e.r.flags & CGF_OPEN) ? "~g~Gate open" : "~r~Gate closed");
}

// ---- car sale (the old script missed /accept car) ----------------------------------------------------
void MPServer::CgSaleClear(int slot)
{
    CgSlot& s = m_CgS[slot];
    if(s.saleTo >= 0 && s.saleTo < MP_MAX_PLAYERS && m_CgS[s.saleTo].saleFrom == slot) m_CgS[s.saleTo].saleFrom = -1;
    s.saleTo = -1; s.salePrice = 0; s.saleModel = 0;
}
void MPServer::CgCarSale(int slot, char* args)
{
    CgSlot& s = m_CgS[slot];
    char* a1 = CgTok(args); char* a2 = CgTok(args);
    if(!a1 || !a2) { CgTell(slot, kCgGrey, "USAGE: /sellcar [player id] [price] - sit in the car you sell. He types /accept car."); return; }
    int t = CgFindPlayer(a1); int price = CgAtoi(a2);
    if(t < 0 || t == slot || !m_CgS[t].beat) { CgTell(slot, kCgGrey, "No such player."); return; }
    if(price < 1 || price > 50000000) { CgTell(slot, kCgGrey, "Price: $1 - $50,000,000."); return; }
    if(!s.inCar || !s.carModel) { CgTell(slot, kCgGrey, "Sit in the car you want to sell."); return; }
    if(CgDist2(s.pos, m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2]) > 10.0f * 10.0f) { CgTell(slot, kCgGrey, "%s is too far away.", m_Slots[t].name); return; }
    if(IsRpServer())
    {
        bool have = false; for(int k = 0; k < m_Rp[slot].nveh && k < RP_VEH_SLOTS; ++k) if(m_Rp[slot].veh[k] == s.carModel) have = true;
        if(!have) { CgTell(slot, kCgGrey, "This is not one of your cars (/vehicles)."); return; }
    }
    CgSaleClear(slot);
    s.saleTo = t; s.salePrice = price; s.saleModel = s.carModel; s.saleAt = MP_NowMs();
    m_CgS[t].saleFrom = slot; m_CgS[t].saleAt = s.saleAt;
    CgTell(slot, kCgYellow, "You offered your car (model %d) to %s for $%d.", s.carModel, m_Slots[t].name, price);
    CgTell(t, kCgYellow, "%s wants to sell you a car (model %d) for $%d. Type /accept car (60 seconds).", m_Slots[slot].name, s.carModel, price);
}
void MPServer::CgAcceptCar(int slot)
{
    CgSlot& b = m_CgS[slot]; const int from = b.saleFrom;
    if(from < 0 || !m_Slots[from].used || m_CgS[from].saleTo != slot) { CgTell(slot, kCgGrey, "Nobody offered you a car."); b.saleFrom = -1; return; }
    CgSlot& s = m_CgS[from];
    const int price = s.salePrice; const uint16_t model = s.saleModel;
    if(IsRpServer())
    {
        RpSrvPlayer& sm = m_Rp[from]; RpSrvPlayer& bm = m_Rp[slot];
        if(!sm.loaded || !bm.loaded) return;
        if(bm.nveh >= RP_VEH_SLOTS) { CgTell(slot, kCgRed, "You have the most cars already."); return; }
        int k = -1; for(int i = 0; i < sm.nveh && i < RP_VEH_SLOTS; ++i) if(sm.veh[i] == model) { k = i; break; }
        if(k < 0) { CgTell(slot, kCgRed, "The seller doesn't have that car any more."); CgSaleClear(from); return; }
        if(!RpPay(slot, -price, "Car bought")) { CgTell(slot, kCgRed, "You need $%d cash.", price); return; }
        RpPay(from, price, "Car sold");
        for(int i = k; i + 1 < sm.nveh; ++i) sm.veh[i] = sm.veh[i + 1];
        --sm.nveh; sm.veh[sm.nveh] = 0;
        bm.veh[bm.nveh++] = model; m_bRpDirty = true;
        RpSendMe(from); RpSendMe(slot);
        CgAct(from, CGA_DELCAR);
    }
    else
    {
        if(!CgPay(slot, price, "Car bought")) { CgTell(slot, kCgRed, "You need $%d cash.", price); return; }
        CgGive(from, price, "Car sold");
        CgAct(from, CGA_MVDEL, 0, 0, model);
        CgAct(slot, CGA_MVADD, 0, 0, model);
    }
    CgTell(slot, kCgGreen, "You bought the car (model %d) from %s for $%d.", model, m_Slots[from].name, price);
    CgTell(from, kCgGreen, "%s bought your car for $%d.", m_Slots[slot].name, price);
    Log("CGRP: %s sold car %d to %s for $%d", m_Slots[from].name, model, m_Slots[slot].name, price);
    CgSaleClear(from); b.saleFrom = -1;
}

// ---- /editor objects ---------------------------------------------------------------------------------
void MPServer::CgEditorObj(int slot, const PktCgCmd& c)
{
    if(CgLevel(slot) < 6) return;
    for(int i = 0; i < 6; ++i) if(!std::isfinite(c.ed[i])) return;
    if(c.edId && c.edModel == 0)
    {
        CgEnt* e = CgGet(c.edId, CGK_OBJECT); if(!e) return;
        CgTell(slot, kCgYellow, "Object %d deleted.", c.edId); CgDel(c.edId); return;
    }
    if(c.edModel == 0 || c.edModel >= 20000) return;
    CgEnt* e = c.edId ? CgGet(c.edId, CGK_OBJECT) : nullptr;
    if(!e)
    {
        int n = 0; for(auto& x : m_Cg) if(x.r.kind == CGK_OBJECT) ++n;
        if(n >= 2000) { CgTell(slot, kCgRed, "The server has the most mapped objects (2000)."); return; }
        e = &CgNew(slot, CGK_OBJECT);
    }
    e->r.model = c.edModel; e->r.x = c.ed[0]; e->r.y = c.ed[1]; e->r.z = c.ed[2]; e->r.ix = c.ed[3]; e->r.iy = c.ed[4]; e->r.iz = c.ed[5];
    e->r.area = m_CgS[slot].area;
    MP_CopyStr(e->r.owner, m_Slots[slot].name, sizeof(e->r.owner));
    CgChanged(*e);
    CgTell(slot, kCgGreen, "Object %d (model %d) saved.", e->r.id, e->r.model);
    CgAct(slot, CGA_EDITOR, 2, 0, e->r.id);   // a = 2: the id of what you just saved
}

// ---- Home Kit houses from the CGRP admin commands ----------------------------------------------------
void MPServer::CgHomeCmd(int slot, const char* cmd, char* args)
{
    if(!HomesOn()) { CgTell(slot, kCgRed, "Houses (Home Kit) are off on this server."); return; }
    if(!m_bHomeLoaded) HomeLoad();
    char* a1 = CgTok(args); char* a2 = CgTok(args);
    PktHomeCmd hc; memset(&hc, 0, sizeof(hc)); hc.type = PKT_HOME_CMD;
    const CgSlot& s = m_CgS[slot];
    auto house = [&](const char* a) -> int { if(!a) { CgTell(slot, kCgGrey, "USAGE: /%s [house id] ...", cmd); return -1; } int i = HomeFind((uint16_t)CgAtoi(a)); if(i < 0) CgTell(slot, kCgGrey, "No house %s (Home Kit ids: /houses).", a); return i; };
    if(!strcmp(cmd, "createhouse"))
    {
        int price = a1 ? CgAtoi(a1) : 50000; int in = a2 ? CgAtoi(a2) : 1;
        char nm[HK_NAME_LEN]; for(int n = 1; ; ++n) { snprintf(nm, sizeof(nm), "House%d", m_nHomeNextId + n - 1); if(HomeByName(nm) < 0) break; }
        hc.cmd = HC_CREATE; MP_CopyStr(hc.text, nm, sizeof(hc.text)); hc.pos[0] = s.pos[0]; hc.pos[1] = s.pos[1]; hc.pos[2] = s.pos[2]; hc.pos[3] = s.heading;
        HomeHandleCmd(slot, hc);
        int idx = HomeByName(nm); if(idx < 0) return;
        HkHome& h = m_Homes[idx]; h.h.owner[0] = 0; h.h.ownerName[0] = 0; h.h.flags |= HKF_SALE; h.h.price = price > 0 ? price : 50000;
        if(in > 0 && in < HK_INTERIOR_COUNT) h.h.interior = (uint8_t)in;
        HomeChanged(idx);
        CgTell(slot, kCgGreen, "House %d (%s) created for sale: $%d, interior %d. Players: /buyhouse.", h.h.id, h.h.name, h.h.price, h.h.interior);
        return;
    }
    int idx = house(a1); if(idx < 0) return;
    HkHome& h = m_Homes[idx];
    if(!strcmp(cmd, "deletehouse")) { hc.cmd = HC_DELETE; hc.id = h.h.id; m_CgHouseLvl.erase(h.h.id); m_bCgDirty = true; HomeHandleCmd(slot, hc); return; }
    if(!strcmp(cmd, "houseint") || !strcmp(cmd, "chouseint"))
    {
        int v = a2 ? CgAtoi(a2) : -1; if(v < 0 || v >= HK_INTERIOR_COUNT) { CgTell(slot, kCgGrey, "Interior 0 - %d.", HK_INTERIOR_COUNT - 1); return; }
        h.h.interior = (uint8_t)v; HomeChanged(idx); CgTell(slot, kCgGreen, "House %d: interior %s.", h.h.id, g_HkInteriors[v].name); return;
    }
    if(!strcmp(cmd, "houselevel")) { int v = a2 ? CgAtoi(a2) : 0; m_CgHouseLvl[h.h.id] = v; m_bCgDirty = true; CgTell(slot, kCgGreen, "House %d needs level %d.", h.h.id, v); return; }
    if(!strcmp(cmd, "houseprice")) { int v = a2 ? CgAtoi(a2) : 0; if(v < 1) { CgTell(slot, kCgGrey, "USAGE: /houseprice [house id] [price]"); return; } h.h.price = v; HomeChanged(idx); CgTell(slot, kCgGreen, "House %d costs $%d.", h.h.id, v); return; }
    if(!strcmp(cmd, "movehouse"))
    {
        const float dx = s.pos[0] - h.h.x, dy = s.pos[1] - h.h.y, dz = s.pos[2] - 1.0f - h.h.z;
        h.h.x += dx; h.h.y += dy; h.h.z += dz; HomeChanged(idx); CgTell(slot, kCgGreen, "House %d moved here.", h.h.id); return;
    }
    if(!strcmp(cmd, "clearhouse")) { h.h.owner[0] = 0; h.h.ownerName[0] = 0; memset(h.h.keys, 0, sizeof(h.h.keys)); h.h.flags |= HKF_SALE; HomeChanged(idx); CgTell(slot, kCgGreen, "House %d has no owner and is for sale ($%d).", h.h.id, h.h.price); return; }
    if(!strcmp(cmd, "sethouseowner"))
    {
        int t = CgFindPlayer(a2); if(t < 0) { CgTell(slot, kCgGrey, "No such player."); return; }
        char k2[HK_OWNER_LEN]; HomeKey(t, k2, sizeof(k2));
        MP_CopyStr(h.h.owner, k2, sizeof(h.h.owner)); MP_CopyStr(h.h.ownerName, m_Slots[t].name, sizeof(h.h.ownerName)); h.h.flags &= ~HKF_SALE;
        HomeChanged(idx); CgTell(slot, kCgGreen, "%s owns house %d now.", m_Slots[t].name, h.h.id); CgTell(t, kCgGreen, "An admin gave you house %d (%s).", h.h.id, h.h.name); return;
    }
    if(!strcmp(cmd, "gotohouse")) { float p[3] = { h.h.x + h.h.ex / 100.0f, h.h.y + h.h.ey / 100.0f, h.h.z + h.h.ez / 100.0f + 1.0f }; CgAct(slot, CGA_TELE, 0, 0, 0, p, h.h.heading); return; }
}

// ---- dialogs -----------------------------------------------------------------------------------------
bool MPServer::CgDialog(int slot, const PktDialogResp& r)
{
    if(r.dialogId < CG_DLG_BASE || r.dialogId > CG_DLG_BASE + 0xFF) return false;
    if(!CgOn() || !r.response) return true;
    CgSlot& s = m_CgS[slot]; CgPlayer& p = CgP(slot);
    const int li = r.listItem;
    switch(r.dialogId)
    {
    case CGD_BUY:
    {
        CgEnt* e = CgGet(s.inside, CGK_BIZ); if(!e) return true;
        int n = 0;
        for(const auto& it : kCgBizItems)
        {
            if(it.type != e->r.sub) continue;
            if(n++ != li) continue;
            const bool owned = !e->ownerKey.empty();
            if(owned && e->r.val <= 0) { CgTell(slot, kCgRed, "%s is out of products.", e->r.name); return true; }
            if(it.kind == 6)
            {
                if(!CgPay(slot, it.price, "Casino")) { CgTell(slot, kCgRed, "You need $%d.", it.price); return true; }
                if(rand() % 100 < 45) { CgGive(slot, it.price * 2, "Casino win"); CgTell(slot, kCgGreen, "[CASINO] You won $%d.", it.price * 2); }
                else { e->till += it.price; CgTell(slot, kCgRed, "[CASINO] You lost $%d.", it.price); }
                m_bCgDirty = true; return true;
            }
            if(!CgPay(slot, it.price, it.name)) { CgTell(slot, kCgRed, "You need $%d.", it.price); return true; }
            if(owned) { e->till += it.price; --e->r.val; CgChanged(*e); }
            switch(it.kind)
            {
            case 0: CgAct(slot, CGA_HEALTH, 1, 0, it.a); break;   // a = 1: add
            case 1: CgAct(slot, CGA_ARMOUR, 0, 0, it.a); break;
            case 2: CgAct(slot, CGA_GUN, it.a, 0, it.b); break;
            case 3: CgAct(slot, CGA_SKIN, 0, 0, it.a); break;
            case 4: CgAct(slot, CGA_FEED, 0, 0, it.a); break;
            default: break;
            }
            CgTell(slot, kCgGreen, "You bought %s for $%d.", it.name, it.price);
            return true;
        }
        return true;
    }
    case CGD_OOC:
    {
        if(li < 0 || li >= (int)(sizeof(kCgOoc) / sizeof(kCgOoc[0]))) return true;
        const CgOocItem& it = kCgOoc[li];
        if(p.gold < it.gold) { CgTell(slot, kCgRed, "You need %d gold bars (you have %d).", it.gold, p.gold); return true; }
        p.gold -= it.gold; m_bCgDirty = true;
        switch(it.kind)
        {
        case 0: CgGive(slot, it.val, "OOC shop"); break;
        case 1: m_Slots[slot].cookies = (uint16_t)(m_Slots[slot].cookies + it.val); break;
        case 2:
            m_Slots[slot].vip = true;
            if(m_Slots[slot].uid[0] && !m_Slots[slot].mod) { m_Ranks[m_Slots[slot].uid] = 'V'; RanksSave(); }
            Retag(slot); break;
        case 3: CgAct(slot, CGA_HEALTH, 0, 0, 100); CgAct(slot, CGA_ARMOUR, 0, 0, 100); break;
        case 4: CgAct(slot, CGA_GUN, 24, 0, 200); CgAct(slot, CGA_GUN, 31, 0, 500); CgAct(slot, CGA_GUN, 25, 0, 100); break;
        case 6: if(IsRpServer()) { m_Rp[slot].bank += it.val; m_bRpDirty = true; RpSendMe(slot); } else p.bank += it.val; break;
        }
        CgTell(slot, kCgGreen, "[OOC SHOP] %s for %d gold bars. %d left.", it.name, it.gold, p.gold);
        CgSendMe(slot);
        return true;
    }
    case CGD_ORDER:
    {
        if(li < 0 || li >= (int)(sizeof(kCgFood) / sizeof(kCgFood[0]))) return true;
        const CgFood& f = kCgFood[li];
        if(!CgPay(slot, f.price, "Drive-thru")) { CgTell(slot, kCgRed, "You need $%d.", f.price); return true; }
        int d = CgNear(slot, CGK_DRIVETHRU, 12.0f);
        if(d >= 0) if(CgEnt* b = CgGet(m_Cg[d].r.group, CGK_BIZ)) { b->till += f.price; m_bCgDirty = true; }
        CgAct(slot, CGA_FEED, 0, 0, f.hunger); CgAct(slot, CGA_HEALTH, 1, 0, f.hp);
        CgTell(slot, kCgGreen, "Here is your %s ($%d). Enjoy.", f.name, f.price);
        return true;
    }
    case CGD_ATM:
        if(li == 0) CgTell(slot, kCgWhite, "[ATM] Bank: $%d", p.bank);
        else if(li == 1) CgDlg(slot, CGD_ATM_W, DIALOG_STYLE_INPUT, "ATM - withdraw", "How much?", "Withdraw", "Cancel");
        else if(li == 2) CgDlg(slot, CGD_ATM_D, DIALOG_STYLE_INPUT, "ATM - deposit", "How much?", "Deposit", "Cancel");
        return true;
    case CGD_ATM_W: case CGD_ATM_D:
    {
        int v = atoi(r.input); if(v <= 0) return true;
        if(CgNear(slot, CGK_ATM, 4.0f) < 0) return true;
        if(r.dialogId == CGD_ATM_W) { if(p.bank < v) { CgTell(slot, kCgRed, "You have only $%d in the bank.", p.bank); return true; } p.bank -= v; CgGive(slot, v, "ATM"); }
        else { if(!CgPay(slot, v, "ATM")) { CgTell(slot, kCgRed, "You don't have $%d.", v); return true; } p.bank += v; }
        m_bCgDirty = true; CgTell(slot, kCgGreen, "[ATM] Done. Bank: $%d", p.bank); CgSendMe(slot);
        return true;
    }
    case CGD_VIEWMAP:
    {
        int n = 0;
        for(const auto& e : m_Cg)
        {
            if(!(e.r.kind == CGK_BIZ || e.r.kind == CGK_GARAGE || e.r.kind == CGK_MOTEL || e.r.kind == CGK_GSTATION || e.r.kind == CGK_ATM || e.r.kind == CGK_DRIVETHRU || e.r.kind == CGK_OOCSHOP)) continue;
            if(n++ != li) continue;
            float pos[3] = { e.r.x, e.r.y, e.r.z };
            CgAct(slot, CGA_GPS, 0, 0, 0, pos);
            CgTell(slot, kCgYellow, "GPS: %s %s (the red marker on the map).", kCgKindName[e.r.kind], e.r.name);
            return true;
        }
        return true;
    }
    case CGD_VEHMENU:
    {
        static const uint16_t cars[] = { 0, 0, 411, 522, 487, 560, 541, 451, 470, 432 };
        if(li == 0) CgAct(slot, CGA_FIXCAR);
        else if(li == 1) CgAct(slot, CGA_DELCAR);
        else if(li >= 2 && li < (int)(sizeof(cars) / sizeof(cars[0]))) CgAct(slot, CGA_SPAWNCAR, 1, 1, cars[li]);
        return true;
    }
    default: return true;
    }
}

#include "server_cgrp_cmds.inc"
