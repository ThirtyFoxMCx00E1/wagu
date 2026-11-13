// 4.13.15: host map editor (net/hostmap.h) + Co-op story pairs (net/coop.h). Included by server.cpp.

void MPServer::SetHostMapFile(const char* path)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    MP_CopyStr(m_szHostMapFile, path && *path ? path : "hostmap.txt", sizeof(m_szHostMapFile));
    m_bHostMapLoaded = false;
}

void MPServer::HostMapLoad()
{
    if(m_bHostMapLoaded) return;
    m_bHostMapLoaded = true; m_HostMap.clear(); m_nHostMapNext = 1;
    FILE* f = fopen(m_szHostMapFile, "r"); if(!f) return;
    char line[256];
    while(fgets(line, sizeof(line), f) && (int)m_HostMap.size() < HMAP_MAX)
    {
        unsigned id, model, in = 0, fl = 0; float x, y, z, rx, ry, rz;
        if(sscanf(line, "O %u %u %f %f %f %f %f %f %u %u", &id, &model, &x, &y, &z, &rx, &ry, &rz, &in, &fl) < 8) continue;
        if(!model || model >= 20000 || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) continue;
        DzMapRow r; memset(&r, 0, sizeof(r));
        r.id = id; r.model = (uint16_t)model; r.interior = (uint8_t)in; r.flags = (uint8_t)fl; r.x = x; r.y = y; r.z = z;
        r.rx10 = DzMapAng10(rx); r.ry10 = DzMapAng10(ry); r.rz10 = DzMapAng10(rz);
        m_HostMap.push_back(r);
        if(id >= m_nHostMapNext) m_nHostMapNext = id + 1;
    }
    fclose(f);
    Log("Host map: %d objects (%s)", (int)m_HostMap.size(), m_szHostMapFile);
}

void MPServer::HostMapSave()
{
    m_bHostMapDirty = false;
    char tmp[300]; snprintf(tmp, sizeof(tmp), "%s.tmp", m_szHostMapFile);
    FILE* f = fopen(tmp, "w"); if(!f) { Log("Host map: can't write %s", m_szHostMapFile); return; }
    fprintf(f, "# MP host map editor (4.13.15): O id model x y z rx ry rz interior flags\n");
    for(const DzMapRow& r : m_HostMap)
        fprintf(f, "O %u %u %.3f %.3f %.3f %.1f %.1f %.1f %u %u\n", r.id, r.model, r.x, r.y, r.z, r.rx10 / 10.0f, r.ry10 / 10.0f, r.rz10 / 10.0f, r.interior, r.flags);
    fclose(f);
    rename(tmp, m_szHostMapFile);
}

void MPServer::HostMapTick(uint64_t now)
{
    if(m_bHostMapDirty && now >= m_nHostMapSaveAt) HostMapSave();
}

void MPServer::HostMapSend(int slot)
{
    if(!HostMapOn() || m_Slots[slot].build < 41315) return;
    HostMapLoad();
    PktHostMap p; memset(&p, 0, sizeof(p)); p.type = PKT_HOST_MAP;
    p.op = HMO_PERM; p.pad = m_Slots[slot].owner ? 1 : 0; p.total = (uint16_t)m_HostMap.size();
    SendTo(slot, &p, (int)offsetof(PktHostMap, r));
    const int total = (int)m_HostMap.size();
    int i = 0; bool first = true;
    do
    {
        const int n = std::min(DZM_PAGE, total - i);
        p.op = first ? HMO_LIST : HMO_MORE; p.n = (uint8_t)(n > 0 ? n : 0); p.pad = 0; p.total = (uint16_t)total;
        for(int k = 0; k < n; ++k) p.r[k] = m_HostMap[i + k];
        SendTo(slot, &p, (int)offsetof(PktHostMap, r) + (n > 0 ? n : 0) * (int)sizeof(DzMapRow));
        i += n > 0 ? n : 0; first = false;
    } while(i < total);
}

void MPServer::HostMapBroadcast(uint8_t op, const DzMapRow& r, int bySlot)
{
    PktHostMap p; memset(&p, 0, sizeof(p)); p.type = PKT_HOST_MAP; p.op = op; p.n = 1; p.total = (uint16_t)m_HostMap.size();
    p.r[0] = r;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used || m_Slots[i].build < 41315) continue;
        p.pad = (op == HMO_SET && i == bySlot) ? 1 : 0;
        SendTo(i, &p, (int)offsetof(PktHostMap, r) + (int)sizeof(DzMapRow));
    }
}

void MPServer::HostMapHandle(int slot, const uint8_t* data, int len)
{
    if(!HostMapOn() || len < (int)offsetof(PktHostMap, r)) return;
    const uint8_t op = data[1];
    if(!m_Slots[slot].owner) return;   // only the host owner of this custom server edits it
    HostMapLoad();
    DzMapRow r; memset(&r, 0, sizeof(r));
    if(op != HMO_CLEAR)
    {
        if(len < (int)offsetof(PktHostMap, r) + (int)sizeof(DzMapRow)) return;
        memcpy(&r, data + offsetof(PktHostMap, r), sizeof(r));
        if(!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.z) || fabsf(r.x) > 20000 || fabsf(r.y) > 20000 || fabsf(r.z) > 5000) return;
    }
    const uint64_t now = MP_NowMs();
    auto find = [&](uint32_t id) { for(int i = 0; i < (int)m_HostMap.size(); ++i) if(m_HostMap[i].id == id) return i; return -1; };
    switch(op)
    {
        case HMO_ADD:
        {
            if(!r.model || r.model >= 20000) return;
            if((int)m_HostMap.size() >= HMAP_MAX) { { PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF4444FF; MP_CopyStr(m.text, "Host map: the server is full (3000 objects).", sizeof(m.text)); SendTo(slot, &m, sizeof(m)); } return; }
            r.id = m_nHostMapNext++; r.pad = 0;
            m_HostMap.push_back(r);
            HostMapBroadcast(HMO_SET, r, slot);
            Log("%s added map object %u (model %u)", m_Slots[slot].name, r.id, r.model);
            m_bHostMapDirty = true; m_nHostMapSaveAt = now + 1500;
            break;
        }
        case HMO_EDIT:
        {
            const int i = find(r.id); if(i < 0 || !r.model || r.model >= 20000) return;
            r.pad = 0; m_HostMap[i] = r;
            HostMapBroadcast(HMO_SET, r, -1);
            m_bHostMapDirty = true; if(!m_nHostMapSaveAt || m_nHostMapSaveAt < now) m_nHostMapSaveAt = now + 3000;
            break;
        }
        case HMO_REMOVE:
        {
            const int i = find(r.id); if(i < 0) return;
            r = m_HostMap[i]; m_HostMap.erase(m_HostMap.begin() + i);
            HostMapBroadcast(HMO_DEL, r, -1);
            Log("%s deleted map object %u", m_Slots[slot].name, r.id);
            m_bHostMapDirty = true; m_nHostMapSaveAt = now + 1500;
            break;
        }
        case HMO_CLEAR:
        {
            m_HostMap.clear();
            for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) HostMapSend(i);
            Log("%s cleared the host map", m_Slots[slot].name);
            m_bHostMapDirty = true; m_nHostMapSaveAt = now + 500;
            break;
        }
    }
}

// ---- Co-op story pairs ------------------------------------------------------------------------
void MPServer::CoopInit()
{
    if(m_bCoopInit) return;
    m_bCoopInit = true;
    memset(m_CoopWith, MP_INVALID_ID, sizeof(m_CoopWith)); memset(m_CoopAsk, MP_INVALID_ID, sizeof(m_CoopAsk));
    memset(m_CoopHost, 0, sizeof(m_CoopHost));
}
void MPServer::CoopEnd(int slot)
{
    CoopInit();
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_CoopAsk[i] == slot) m_CoopAsk[i] = MP_INVALID_ID;
    m_CoopAsk[slot] = MP_INVALID_ID;
    const int o = m_CoopWith[slot];
    m_CoopWith[slot] = MP_INVALID_ID; m_CoopHost[slot] = false;
    if(o >= MP_MAX_PLAYERS) return;
    m_CoopWith[o] = MP_INVALID_ID; m_CoopHost[o] = false;
    PktCoop c; memset(&c, 0, COOP_HEAD); c.type = PKT_COOP; c.op = COOP_END; c.other = (uint8_t)slot;
    if(m_Slots[o].used) SendTo(o, &c, COOP_HEAD);
    c.other = (uint8_t)o;
    if(m_Slots[slot].used) SendTo(slot, &c, COOP_HEAD);
    Log("Co-op story ended (%s / %s)", m_Slots[slot].name, m_Slots[o].name);
}
void MPServer::CoopHandle(int slot, const uint8_t* data, int len)
{
    if(len < COOP_HEAD) return;
    CoopInit();
    PktCoop c; memset(&c, 0, sizeof(c)); memcpy(&c, data, std::min<int>(len, (int)sizeof(c)));
    const int o = c.other;
    auto reply = [&](uint8_t op, int other, uint32_t a, int to)
    {
        PktCoop r; memset(&r, 0, COOP_HEAD); r.type = PKT_COOP; r.op = op; r.other = (uint8_t)other; r.a = a;
        SendTo(to, &r, COOP_HEAD);
    };
    switch(c.op)
    {
        case COOP_INVITE:
            if(!(m_nPerms & PERM_MISSION_PAIR) || o >= MP_MAX_PLAYERS || o == slot || !m_Slots[o].used || m_Slots[o].build < 41315)
            { reply(COOP_DECLINE, o < MP_MAX_PLAYERS ? o : MP_INVALID_ID, 2, slot); return; }
            if(m_CoopWith[o] != MP_INVALID_ID) { reply(COOP_DECLINE, o, 3, slot); return; }   // already in a co-op
            m_CoopAsk[slot] = (uint8_t)o;
            reply(COOP_INVITE, slot, 0, o);
            Log("%s invited %s to Co-op story", m_Slots[slot].name, m_Slots[o].name);
            return;
        case COOP_DECLINE:
            if(o < MP_MAX_PLAYERS && m_CoopAsk[o] == slot) { m_CoopAsk[o] = MP_INVALID_ID; reply(COOP_DECLINE, slot, 1, o); }
            return;
        case COOP_ACCEPT:
        {
            if(o >= MP_MAX_PLAYERS || !m_Slots[o].used || m_CoopAsk[o] != slot) { reply(COOP_DECLINE, o < MP_MAX_PLAYERS ? o : MP_INVALID_ID, 2, slot); return; }
            if(m_CoopWith[slot] != MP_INVALID_ID) CoopEnd(slot);
            if(m_CoopWith[o] != MP_INVALID_ID) CoopEnd(o);
            m_CoopAsk[o] = MP_INVALID_ID;
            m_CoopWith[o] = (uint8_t)slot; m_CoopWith[slot] = (uint8_t)o;
            m_CoopHost[o] = true; m_CoopHost[slot] = false;
            reply(COOP_ACCEPT, slot, 1, o);    // the inviter = the host
            reply(COOP_ACCEPT, o, 0, slot);    // the one who accepted = the partner
            Log("Co-op story: %s (host) + %s", m_Slots[o].name, m_Slots[slot].name);
            return;
        }
        case COOP_END: CoopEnd(slot); return;
        default:
        {   // everything else goes to the partner only
            const int to = m_CoopWith[slot];
            if(to >= MP_MAX_PLAYERS || !m_Slots[to].used) return;
            // host-only messages from the host, partner-only from the partner
            const bool fromHost = m_CoopHost[slot];
            if((c.op == COOP_SAVE_BEGIN || c.op == COOP_SAVE_CHUNK || c.op == COOP_MISSION || c.op == COOP_WEAPON || c.op == COOP_REWARD || c.op == COOP_CUT || c.op == COOP_WORLD) && !fromHost) return;
            if((c.op == COOP_SAVE_ACK || c.op == COOP_SAVE_DONE) && fromHost) return;
            c.other = (uint8_t)slot;
            int n = COOP_HEAD;
            if(c.op == COOP_SAVE_CHUNK) { if(c.n > COOP_CHUNK || COOP_HEAD + c.n > len) return; n += c.n; }
            SendTo(to, &c, n);
            return;
        }
    }
}
