// 3.39: Home Kit gamemode - server side (included by server.cpp).
// Houses live in m_szHomeFile ("homes.txt" next to the server, or the phone's data folder):
//   H id|rev|name|owner|ownerName|price|flags|w|h|interior|x|y|z|heading|prefab|ex|ey|ez|key1,key2,key3,key4
//   O model x y z rx ry rz flags         (objects of the house above, cm / degrees*10)

bool MPServer::HomesOn() const { return IsRpServer() || (!IsBcwServer() && !IsMw3Server() && (GetModes() & GM_HOME)); }

void MPServer::SetHomeFile(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szHomeFile, path && *path ? path : "homes.txt", sizeof(m_szHomeFile));
    m_bHomeLoaded = false;
}

int MPServer::HomeFind(uint16_t id) const
{
    for(size_t i = 0; i < m_Homes.size(); ++i) if(m_Homes[i].h.id == id) return (int)i;
    return -1;
}

int MPServer::HomeByName(const char* name) const
{
    for(size_t i = 0; i < m_Homes.size(); ++i) if(!strcasecmp(m_Homes[i].h.name, name)) return (int)i;
    return -1;
}

const char* MPServer::HomeKey(int slot, char* buf, size_t n) const
{
    const Slot& s = m_Slots[slot];
    if(s.authed && s.account[0]) snprintf(buf, n, "a:%s", s.account);
    else if(s.uid[0])            snprintf(buf, n, "u:%s", s.uid);
    else                         snprintf(buf, n, "n:%s", s.name);
    return buf;
}

bool MPServer::HomeStaff(int slot) const
{
    const Slot& s = m_Slots[slot];
    return s.admin || s.mod || s.owner;
}

void MPServer::HomeLoad()
{
    m_bHomeLoaded = true;
    m_Homes.clear(); m_nHomeNextId = 1;
    FILE* f = fopen(m_szHomeFile, "r");
    if(!f) return;
    char line[1024];
    HkHome* cur = nullptr;
    while(fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;
        if(line[0] == 'H' && line[1] == ' ')
        {
            if((int)m_Homes.size() >= HK_MAX_HOMES) { cur = nullptr; continue; }
            HkHome h; memset(&h.h, 0, sizeof(h.h));
            h.h.type = PKT_HOME_HEAD;
            char* f2[20] = {0}; int nf = 0; char* p = line + 2;
            while(nf < 20) { f2[nf++] = p; char* bar = strchr(p, '|'); if(!bar) break; *bar = 0; p = bar + 1; }
            if(nf < 18) { cur = nullptr; continue; }
            h.h.id = (uint16_t)atoi(f2[0]); h.h.rev = (uint16_t)atoi(f2[1]);
            MP_CopyStr(h.h.name, f2[2], sizeof(h.h.name)); MP_CopyStr(h.h.owner, f2[3], sizeof(h.h.owner));
            MP_CopyStr(h.h.ownerName, f2[4], sizeof(h.h.ownerName));
            h.h.price = atoi(f2[5]); h.h.flags = (uint8_t)atoi(f2[6]);
            h.h.w = (uint8_t)std::max(1, std::min(HK_SLOT_MAX, atoi(f2[7]))); h.h.h = (uint8_t)std::max(1, std::min(HK_SLOT_MAX, atoi(f2[8])));
            h.h.interior = (uint8_t)std::max(0, std::min(HK_INTERIOR_COUNT - 1, atoi(f2[9])));
            h.h.x = (float)atof(f2[10]); h.h.y = (float)atof(f2[11]); h.h.z = (float)atof(f2[12]); h.h.heading = (float)atof(f2[13]);
            h.h.prefab = (uint16_t)atoi(f2[14]);
            h.h.ex = (int16_t)atoi(f2[15]); h.h.ey = (int16_t)atoi(f2[16]); h.h.ez = (int16_t)atoi(f2[17]);
            if(nf >= 19 && f2[18])
            {
                int k = 0; char* q = f2[18];
                while(q && *q && k < HK_KEYS) { char* c = strchr(q, ','); if(c) *c = 0; MP_CopyStr(h.h.keys[k++], q, MP_NAME_LEN); q = c ? c + 1 : nullptr; }
            }
            if(!h.h.id || HomeFind(h.h.id) >= 0 || !h.h.name[0]) { cur = nullptr; continue; }
            if(h.h.id >= m_nHomeNextId) m_nHomeNextId = (uint16_t)(h.h.id + 1);
            m_Homes.push_back(h); cur = &m_Homes.back();
        }
        else if(line[0] == 'O' && line[1] == ' ' && cur && (int)cur->obj.size() < HK_MAX_OBJ)
        {
            int m, x, y, z, rx, ry, rz, fl;
            if(sscanf(line + 2, "%d %d %d %d %d %d %d %d", &m, &x, &y, &z, &rx, &ry, &rz, &fl) == 8 && m > 0 && m < 20000)
            {
                HkObj o; o.model = (uint16_t)m; o.x = (int16_t)x; o.y = (int16_t)y; o.z = (int16_t)z;
                o.rx = (int16_t)rx; o.ry = (int16_t)ry; o.rz = (int16_t)rz; o.flags = (uint8_t)fl;
                cur->obj.push_back(o);
            }
        }
    }
    fclose(f);
    for(auto& h : m_Homes) h.h.nObj = (uint16_t)h.obj.size();
    Log("Home Kit: %d houses loaded from %s", (int)m_Homes.size(), m_szHomeFile);
}

void MPServer::HomeSave()
{
    m_bHomeDirty = false;
    std::string tmp = std::string(m_szHomeFile) + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w");
    if(!f) return;
    fprintf(f, "# Home Kit houses (3.39) - written by the server\n");
    for(const auto& h : m_Homes)
    {
        std::string keys;
        for(int k = 0; k < HK_KEYS; ++k) if(h.h.keys[k][0]) { if(!keys.empty()) keys += ","; keys += h.h.keys[k]; }
        fprintf(f, "H %u|%u|%s|%s|%s|%d|%u|%u|%u|%u|%.2f|%.2f|%.2f|%.1f|%u|%d|%d|%d|%s\n",
                h.h.id, h.h.rev, h.h.name, h.h.owner, h.h.ownerName, h.h.price, h.h.flags, h.h.w, h.h.h, h.h.interior,
                h.h.x, h.h.y, h.h.z, h.h.heading, h.h.prefab, h.h.ex, h.h.ey, h.h.ez, keys.c_str());
        for(const auto& o : h.obj) fprintf(f, "O %u %d %d %d %d %d %d %u\n", o.model, o.x, o.y, o.z, o.rx, o.ry, o.rz, o.flags);
    }
    fclose(f);
    rename(tmp.c_str(), m_szHomeFile);
}

void MPServer::HomeTick(uint64_t now)
{
    if(!HomesOn()) return;
    if(!m_bHomeLoaded) HomeLoad();
    if(m_bHomeDirty && now >= m_nHomeSaveAt) HomeSave();
}

void MPServer::HomeChanged(int idx)
{
    HkHome& h = m_Homes[idx];
    h.h.rev = (uint16_t)(h.h.rev + 1); if(!h.h.rev) h.h.rev = 1;
    h.h.nObj = (uint16_t)h.obj.size();
    m_bHomeDirty = true; m_nHomeSaveAt = MP_NowMs() + 2000;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].build >= 33900) HomeSend(i, idx);
}

void MPServer::HomeSendIndex(int slot)
{
    if(!HomesOn()) return;
    if(!m_bHomeLoaded) HomeLoad();
    const int total = (int)m_Homes.size();
    int page = 0;
    for(int start = 0; start < total || (start == 0 && page == 0); start += HK_INDEX_PAGE, ++page)
    {
        PktHomeIndex p; memset(&p, 0, sizeof(p));
        p.type = PKT_HOME_INDEX; p.page = (uint16_t)page; p.total = (uint16_t)total;
        int n = std::min(HK_INDEX_PAGE, total - start); if(n < 0) n = 0;
        for(int k = 0; k < n; ++k) { p.e[k * 2] = m_Homes[start + k].h.id; p.e[k * 2 + 1] = m_Homes[start + k].h.rev; }
        p.count = (uint16_t)n;
        SendTo(slot, &p, (int)(offsetof(PktHomeIndex, e) + n * 4));
        if(total == 0) break;
    }
}

void MPServer::HomeSend(int slot, int idx)
{
    const HkHome& h = m_Homes[idx];
    PktHomeHead hd = h.h; hd.type = PKT_HOME_HEAD; hd.nObj = (uint16_t)h.obj.size();
    SendTo(slot, &hd, sizeof(hd));
    for(size_t start = 0; start < h.obj.size(); start += HK_OBJ_PAGE)
    {
        PktHomeObjs o; memset(&o, 0, sizeof(o));
        o.type = PKT_HOME_OBJS; o.id = h.h.id; o.rev = h.h.rev; o.start = (uint16_t)start;
        int n = (int)std::min((size_t)HK_OBJ_PAGE, h.obj.size() - start);
        o.count = (uint8_t)n;
        memcpy(o.o, &h.obj[start], n * sizeof(HkObj));
        SendTo(slot, &o, (int)(offsetof(PktHomeObjs, o) + n * sizeof(HkObj)));
    }
}

void MPServer::HomeMsg(int slot, bool ok, uint16_t id, int32_t cash, const char* fmt, ...)
{
    PktHomeMsg m; memset(&m, 0, sizeof(m));
    m.type = PKT_HOME_MSG; m.ok = ok ? 1 : 0; m.id = id; m.cash = cash;
    va_list ap; va_start(ap, fmt); vsnprintf(m.text, sizeof(m.text), fmt, ap); va_end(ap);
    SendTo(slot, &m, sizeof(m));
}

// the slot rectangle (with its heading) of another house overlaps?
static bool HkOverlaps(const PktHomeHead& a, float ax, float ay, int aw, int ah, float ahdg, const PktHomeHead& b)
{
    float acx, acy, bcx, bcy;
    HkLocalToWorld(ax, ay, ahdg, aw * HK_TILE * 0.5f, ah * HK_TILE * 0.5f, acx, acy);
    HkLocalToWorld(b.x, b.y, b.heading, b.w * HK_TILE * 0.5f, b.h * HK_TILE * 0.5f, bcx, bcy);
    const float ra = 0.5f * HK_TILE * sqrtf((float)(aw * aw + ah * ah)), rb = 0.5f * HK_TILE * sqrtf((float)(b.w * b.w + b.h * b.h));
    const float dx = acx - bcx, dy = acy - bcy;
    (void)a;
    return dx * dx + dy * dy < (ra + rb) * (ra + rb) * 0.8f;   // circles a bit inside the squares
}

static bool HkObjInside(const PktHomeHead& h, const HkObj& o)
{
    if(o.flags & HKO_INTERIOR) return abs(o.x) <= 3000 && abs(o.y) <= 3000 && abs(o.z) <= 1500;   // 30 m around the interior anchor
    if(HkIsPrefab(o.model)) return o.x >= -200 && o.y >= -200 && o.x <= h.w * HK_TILE * 100 + 200 && o.y <= h.h * HK_TILE * 100 + 200;
    const float lx = o.x / 100.0f, ly = o.y / 100.0f, lz = o.z / 100.0f;
    return lx >= -1.0f && ly >= -1.0f && lx <= h.w * HK_TILE + 1.0f && ly <= h.h * HK_TILE + 1.0f && lz >= -3.0f && lz <= HK_HEIGHT_MAX + 2.0f;
}

void MPServer::HomeHandleCmd(int slot, const PktHomeCmd& cin)
{
    if(!HomesOn()) { HomeMsg(slot, false, 0, 0, "The Home Kit gamemode is off on this server."); return; }
    if(!m_bHomeLoaded) HomeLoad();
    PktHomeCmd c = cin; c.text[sizeof(c.text) - 1] = 0;
    Slot& s = m_Slots[slot];
    if(AuthOn() && !s.authed) { HomeMsg(slot, false, 0, 0, "Log in first."); return; }
    const uint64_t now = MP_NowMs();
    if(c.cmd != HC_INDEX && c.cmd != HC_GET && c.cmd != HC_OBJ_SET)
    {
        if(now - s.hkLastCmd < 250) return;   // flood guard
        s.hkLastCmd = now;
    }
    char key[HK_OWNER_LEN]; HomeKey(slot, key, sizeof(key));
    const bool staff = HomeStaff(slot);
    int idx = (c.cmd == HC_INDEX || c.cmd == HC_CREATE) ? -1 : HomeFind(c.id);
    if(c.cmd != HC_INDEX && c.cmd != HC_CREATE && idx < 0) { HomeMsg(slot, false, c.id, 0, "That house doesn't exist."); return; }
    HkHome* h = idx >= 0 ? &m_Homes[idx] : nullptr;
    const bool mine = h && !strcmp(h->h.owner, key);
    bool hasKey = false;
    if(h) for(int k = 0; k < HK_KEYS; ++k) if(h->h.keys[k][0] && !strcasecmp(h->h.keys[k], s.name)) hasKey = true;
    const bool canEdit = h && (mine || staff);

    switch(c.cmd)
    {
    case HC_INDEX: HomeSendIndex(slot); return;
    case HC_GET:   HomeSend(slot, idx); return;
    case HC_CREATE:
    {
        if(!HkValidName(c.text)) { HomeMsg(slot, false, 0, 0, "House name: 2-23 letters, numbers, _ - ."); return; }
        if(HomeByName(c.text) >= 0) { HomeMsg(slot, false, 0, 0, "A house called %s already exists.", c.text); return; }
        if((int)m_Homes.size() >= HK_MAX_HOMES) { HomeMsg(slot, false, 0, 0, "This server has the most houses it can keep (%d).", HK_MAX_HOMES); return; }
        int owned = 0; for(auto& o : m_Homes) if(!strcmp(o.h.owner, key)) ++owned;
        if(owned >= HK_PER_PLAYER && !staff) { HomeMsg(slot, false, 0, 0, "You already have %d houses (the most).", HK_PER_PLAYER); return; }
        PktHomeHead nh; memset(&nh, 0, sizeof(nh));
        nh.type = PKT_HOME_HEAD; nh.id = m_nHomeNextId++; if(!m_nHomeNextId) m_nHomeNextId = 1;
        MP_CopyStr(nh.name, c.text, sizeof(nh.name));
        MP_CopyStr(nh.owner, key, sizeof(nh.owner)); MP_CopyStr(nh.ownerName, s.name, sizeof(nh.ownerName));
        nh.w = 8; nh.h = 8; nh.heading = c.pos[3];
        // the player stands in the middle of the new 8x8 slot
        float ox, oy; HkLocalToWorld(0, 0, nh.heading, -4 * HK_TILE, -4 * HK_TILE, ox, oy);
        nh.x = c.pos[0] + ox; nh.y = c.pos[1] + oy; nh.z = c.pos[2] - 1.0f;
        nh.ex = (int16_t)(4 * HK_TILE * 100); nh.ey = (int16_t)(0); nh.ez = 100;
        for(auto& o : m_Homes) if(HkOverlaps(nh, nh.x, nh.y, nh.w, nh.h, nh.heading, o.h)) { HomeMsg(slot, false, 0, 0, "Too close to the house %s - move to an empty field.", o.h.name); return; }
        HkHome hh; hh.h = nh;
        m_Homes.push_back(hh);
        HomeChanged((int)m_Homes.size() - 1);
        Log("Home Kit: %s created the house %s (id %u)", s.name, nh.name, nh.id);
        HomeMsg(slot, true, nh.id, 0, "House %s created successfully. Now type /setup %s", nh.name, nh.name);
        return;
    }
    case HC_SETUP:
    {
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "Only the owner can set this house up."); return; }
        if(c.w < 1 || c.h < 1 || c.w > HK_SLOT_MAX || c.h > HK_SLOT_MAX) { HomeMsg(slot, false, c.id, 0, "The slot can be 1x1 up to %dx%d squares.", HK_SLOT_MAX, HK_SLOT_MAX); return; }
        const float x = c.pos[0], y = c.pos[1], z = c.pos[2], hdg = c.pos[3];
        if(!(x == x) || fabsf(x) > 4000 || fabsf(y) > 4000 || z < -100 || z > 1500) return;
        for(auto& o : m_Homes) if(o.h.id != h->h.id && HkOverlaps(h->h, x, y, c.w, c.h, hdg, o.h)) { HomeMsg(slot, false, c.id, 0, "The slot overlaps the house %s.", o.h.name); return; }
        h->h.x = x; h->h.y = y; h->h.z = z; h->h.heading = hdg; h->h.w = c.w; h->h.h = c.h;
        if(c.val > 0 && HkIsPrefab(c.val)) h->h.prefab = (uint16_t)c.val; else if(c.val < 0) h->h.prefab = 0;
        if(h->h.ex < 0 || h->h.ex > c.w * HK_TILE * 100 || h->h.ey < 0 || h->h.ey > c.h * HK_TILE * 100) { h->h.ex = (int16_t)(c.w * HK_TILE * 50); h->h.ey = 0; }
        // objects that are outside the new slot go
        std::vector<HkObj> keep; for(auto& o : h->obj) if(HkObjInside(h->h, o)) keep.push_back(o);
        const int dropped = (int)(h->obj.size() - keep.size()); h->obj.swap(keep);
        h->h.flags |= HKF_SETUP;
        HomeChanged(idx);
        HomeMsg(slot, true, c.id, 0, "Slot saved: %dx%d squares (%.0fx%.0f m).%s", c.w, c.h, c.w * HK_TILE, c.h * HK_TILE, dropped ? " Objects outside the slot were removed." : "");
        return;
    }
    case HC_BUY:
    {
        if(!(h->h.flags & HKF_SALE)) { HomeMsg(slot, false, c.id, 0, "%s is not for sale.", h->h.name); return; }
        if(mine) { HomeMsg(slot, false, c.id, 0, "It is already your house."); return; }
        int owned = 0; for(auto& o : m_Homes) if(!strcmp(o.h.owner, key)) ++owned;
        if(owned >= HK_PER_PLAYER && !staff) { HomeMsg(slot, false, c.id, 0, "You already have %d houses (the most).", HK_PER_PLAYER); return; }
        if(IsRpServer())
        {
            if(!RpPay(slot, -h->h.price, "House")) { HomeMsg(slot, false, c.id, 0, "You need $%d cash.", h->h.price); return; }
        }
        else if(c.val != h->h.price) { HomeMsg(slot, false, c.id, 0, "The price changed to $%d - try again.", h->h.price); return; }
        // the seller gets the money (online now: right away)
        for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        {
            if(!m_Slots[i].used || i == slot) continue;
            char k2[HK_OWNER_LEN]; HomeKey(i, k2, sizeof(k2));
            if(!strcmp(k2, h->h.owner))
            {
                if(IsRpServer()) { RpPay(i, h->h.price, "House sold"); HomeMsg(i, true, c.id, 0, "%s bought your house %s for $%d.", s.name, h->h.name, h->h.price); }
                else HomeMsg(i, true, c.id, h->h.price, "%s bought your house %s for $%d.", s.name, h->h.name, h->h.price);
            }
        }
        MP_CopyStr(h->h.owner, key, sizeof(h->h.owner)); MP_CopyStr(h->h.ownerName, s.name, sizeof(h->h.ownerName));
        h->h.flags &= ~HKF_SALE; h->h.flags &= ~HKF_LOCKED;
        memset(h->h.keys, 0, sizeof(h->h.keys));
        HomeChanged(idx);
        Log("Home Kit: %s bought %s for $%d", s.name, h->h.name, h->h.price);
        HomeMsg(slot, true, c.id, IsRpServer() ? 0 : -h->h.price, "You bought %s for $%d. Welcome home.", h->h.name, h->h.price);
        return;
    }
    case HC_SELL:
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(c.val < 0 || c.val > 50000000) { HomeMsg(slot, false, c.id, 0, "Price: $0 - $50,000,000."); return; }
        if(c.val == 0) h->h.flags &= ~HKF_SALE; else { h->h.flags |= HKF_SALE; h->h.price = c.val; }
        HomeChanged(idx);
        HomeMsg(slot, true, c.id, 0, c.val ? "%s is for sale for $%d (red marker: /buyhouse)." : "%s is not for sale any more.", h->h.name, c.val);
        return;
    case HC_DELETE:
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        {
            const uint16_t id = h->h.id; char nm[HK_NAME_LEN]; MP_CopyStr(nm, h->h.name, sizeof(nm));
            m_Homes.erase(m_Homes.begin() + idx);
            m_bHomeDirty = true; m_nHomeSaveAt = now + 1000;
            PktHomeDel d; d.type = PKT_HOME_DEL; d.op = 0; d.id = id;
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && m_Slots[i].build >= 33900) SendTo(i, &d, sizeof(d));
            HomeMsg(slot, true, 0, 0, "House %s deleted.", nm);
        }
        return;
    case HC_RENAME:
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(!HkValidName(c.text)) { HomeMsg(slot, false, c.id, 0, "House name: 2-23 letters, numbers, _ - ."); return; }
        if(HomeByName(c.text) >= 0) { HomeMsg(slot, false, c.id, 0, "That name is taken."); return; }
        MP_CopyStr(h->h.name, c.text, sizeof(h->h.name));
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Renamed to %s.", h->h.name);
        return;
    case HC_LOCK:
        if(!(canEdit || hasKey)) { HomeMsg(slot, false, c.id, 0, "You have no key for %s.", h->h.name); return; }
        if(c.val) h->h.flags |= HKF_LOCKED; else h->h.flags &= ~HKF_LOCKED;
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, c.val ? "%s locked." : "%s unlocked.", h->h.name);
        return;
    case HC_OBJ_ADD:
        if(!(canEdit || (hasKey && (c.obj.flags & HKO_INTERIOR)))) { HomeMsg(slot, false, c.id, 0, "You can't edit %s.", h->h.name); return; }
        if((int)h->obj.size() >= HK_MAX_OBJ) { HomeMsg(slot, false, c.id, 0, "This house has the most objects (%d).", HK_MAX_OBJ); return; }
        if(!c.obj.model || c.obj.model >= 20000) return;
        if(!HkObjInside(h->h, c.obj)) { HomeMsg(slot, false, c.id, 0, "Outside the slot (red) - keep objects on the green squares, up to %.0f m high.", HK_HEIGHT_MAX); return; }
        h->obj.push_back(c.obj);
        HomeChanged(idx);
        HomeMsg(slot, true, c.id, 0, "Object added (%d/%d).", (int)h->obj.size(), HK_MAX_OBJ);
        return;
    case HC_OBJ_SET:
        if(!(canEdit || hasKey)) return;
        if(c.objIdx >= h->obj.size()) return;
        if(!canEdit && !(h->obj[c.objIdx].flags & HKO_INTERIOR)) return;
        if(!HkObjInside(h->h, c.obj)) { HomeMsg(slot, false, c.id, 0, "That spot is outside the slot (red)."); HomeSend(slot, idx); return; }
        if(now - s.hkLastCmd < 120) return;   // a held arrow: ~8 moves a second
        s.hkLastCmd = now;
        h->obj[c.objIdx] = c.obj;
        HomeChanged(idx);
        return;
    case HC_OBJ_DEL:
        if(!(canEdit || hasKey)) return;
        if(c.objIdx >= h->obj.size()) return;
        if(!canEdit && !(h->obj[c.objIdx].flags & HKO_INTERIOR)) return;
        h->obj.erase(h->obj.begin() + c.objIdx);
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Object removed.");
        return;
    case HC_OBJ_CLEAR:
    {
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        std::vector<HkObj> keep;
        for(auto& o : h->obj) { const bool in = (o.flags & HKO_INTERIOR) != 0; if(c.val == 0 ? in : (c.val == 1 ? !in : false)) keep.push_back(o); }
        h->obj.swap(keep);
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Objects cleared.");
        return;
    }
    case HC_ENTRANCE:
    {
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        float lx, ly; HkWorldToLocal(h->h.x, h->h.y, h->h.heading, c.pos[0], c.pos[1], lx, ly);
        if(lx < -1.5f || ly < -1.5f || lx > h->h.w * HK_TILE + 1.5f || ly > h->h.h * HK_TILE + 1.5f) { HomeMsg(slot, false, c.id, 0, "Stand on (or next to) your slot to set the door."); return; }
        h->h.ex = (int16_t)(lx * 100); h->h.ey = (int16_t)(ly * 100); h->h.ez = (int16_t)((c.pos[2] - h->h.z) * 100);
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Door marker set.");
        return;
    }
    case HC_INTERIOR:
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(c.val < 0 || c.val >= HK_INTERIOR_COUNT) return;
        h->h.interior = (uint8_t)c.val;
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Interior: %s.", g_HkInteriors[c.val].name);
        return;
    case HC_PREFAB:
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(c.val != 0 && !HkIsPrefab(c.val)) return;
        h->h.prefab = (uint16_t)c.val;
        HomeChanged(idx); HomeMsg(slot, true, c.id, 0, c.val ? "Building: %s." : "Building removed.", c.val ? HkFindModel(c.val)->name : "");
        return;
    case HC_GIVE:
    {
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(c.val < 0 || c.val >= MP_MAX_PLAYERS || !m_Slots[c.val].used || c.val == slot) { HomeMsg(slot, false, c.id, 0, "No such player."); return; }
        char k2[HK_OWNER_LEN]; HomeKey(c.val, k2, sizeof(k2));
        int owned = 0; for(auto& o : m_Homes) if(!strcmp(o.h.owner, k2)) ++owned;
        if(owned >= HK_PER_PLAYER && !HomeStaff(c.val)) { HomeMsg(slot, false, c.id, 0, "%s already has %d houses.", m_Slots[c.val].name, HK_PER_PLAYER); return; }
        MP_CopyStr(h->h.owner, k2, sizeof(h->h.owner)); MP_CopyStr(h->h.ownerName, m_Slots[c.val].name, sizeof(h->h.ownerName));
        h->h.flags &= ~HKF_SALE; memset(h->h.keys, 0, sizeof(h->h.keys));
        HomeChanged(idx);
        HomeMsg(slot, true, c.id, 0, "You gave %s to %s.", h->h.name, m_Slots[c.val].name);
        HomeMsg(c.val, true, c.id, 0, "%s gave you the house %s.", s.name, h->h.name);
        return;
    }
    case HC_KEY:
    {
        if(!canEdit) { HomeMsg(slot, false, c.id, 0, "It is not your house."); return; }
        if(c.val < 0 || c.val >= MP_MAX_PLAYERS || !m_Slots[c.val].used) { HomeMsg(slot, false, c.id, 0, "No such player."); return; }
        const char* nm = m_Slots[c.val].name;
        for(int k = 0; k < HK_KEYS; ++k) if(!strcasecmp(h->h.keys[k], nm)) { h->h.keys[k][0] = 0; HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "Key taken from %s.", nm); return; }
        for(int k = 0; k < HK_KEYS; ++k) if(!h->h.keys[k][0]) { MP_CopyStr(h->h.keys[k], nm, MP_NAME_LEN); HomeChanged(idx); HomeMsg(slot, true, c.id, 0, "%s has a key now.", nm); HomeMsg(c.val, true, c.id, 0, "%s gave you a key of %s.", s.name, h->h.name); return; }
        HomeMsg(slot, false, c.id, 0, "%d keys at most - take one first.", HK_KEYS);
        return;
    }
    default: return;
    }
}

// ---- 3.39: rules + live server settings -----------------------------------------------------------
void MPServer::SetRules(bool on, const char* title, const char* const lines[10])
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    m_bRulesOn = on;
    MP_CopyStr(m_szRules[0], title && *title ? title : "SERVER RULES", sizeof(m_szRules[0]));
    for(int i = 0; i < 10; ++i) MP_CopyStr(m_szRules[i + 1], lines && lines[i] ? lines[i] : "", sizeof(m_szRules[0]));
}

void MPServer::SendRules(int slot)
{
    if(m_Slots[slot].build < 33900 || IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer()) return;   // the default server: the phone's own RULES
    int count = 0; for(int i = 1; i <= 10; ++i) if(m_szRules[i][0]) ++count;
    for(int i = 0; i <= 10; ++i)
    {
        if(i > 0 && !m_szRules[i][0]) continue;
        PktRules r; memset(&r, 0, sizeof(r));
        r.type = PKT_RULES; r.idx = (uint8_t)i; r.on = (m_bRulesOn && count) ? 1 : 0; r.count = (uint8_t)count;
        MP_CopyStr(r.text, m_szRules[i], sizeof(r.text));
        SendTo(slot, &r, sizeof(r));
        if(!r.on) break;   // off: just the title packet says so
    }
}

bool MPServer::CanManage(int slot) const
{
    const Slot& s = m_Slots[slot];
    if(IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer()) return s.admin || s.mod;   // the 4 featured servers: staff only
    return s.owner;                                                                                  // a custom server: its host owner
}

void MPServer::SendSrvSettings(int slot)
{
    if(m_Slots[slot].build < 33900) return;
    PktSrvSettings p; memset(&p, 0, sizeof(p));
    p.type = PKT_SRV_SETTINGS; p.op = 0; p.perms = m_nPerms.load(); p.modes = GetModes(); p.rulesOn = m_bRulesOn ? 1 : 0;
    p.can = CanManage(slot) ? 1 : 0;
    SendTo(slot, &p, sizeof(p));
}

void MPServer::OnSrvSettings(int slot, const PktSrvSettings& p)
{
    if(!CanManage(slot)) return;
    if(IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer()) { SendSrvSettings(slot); return; }   // featured: fixed gamemodes
    m_nModes = p.modes & GM_ALL;
    m_nPerms = p.perms; m_nBasePerms = p.perms;
    m_bRulesOn = p.rulesOn != 0;
    Log("%s changed the server settings (gamemodes 0x%04X, permissions 0x%02X)", m_Slots[slot].name, (unsigned)m_nModes.load(), (unsigned)m_nPerms.load());
    PktPerms pm; memset(&pm, 0, sizeof(pm));
    pm.type = PKT_PERMS; pm.perms = m_nPerms.load(); pm.modesMagic = 0x47; pm.modes = GetModes();
    Broadcast(&pm, sizeof(pm));
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) { SendSrvSettings(i); if(HomesOn()) HomeSendIndex(i); }
}
