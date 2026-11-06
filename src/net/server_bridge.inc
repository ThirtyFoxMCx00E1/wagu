// 4.12: open.mp cross-play bridge - included by server.cpp.
// The open.mp component crossplay/mp_crossplay.cpp runs this MPServer inside omp-server:
//   PC player on open.mp  -> BridgeAdd() a "ghost" slot here, BridgeSync() every 50-100 ms
//   phone player here     -> BridgePeers() -> an open.mp NPC that copies the phone's sync
// A ghost slot has no address (sin_port 0): SendRaw() skips it, the timeout never drops it,
// everything else (player list, sync relay, chat, join/leave) treats it like a real player.

int MPServer::BridgeAdd(const char* name, uint32_t color, uint16_t skin)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(i == MP_BOT_ID || m_Slots[i].used) continue;
        Slot& s = m_Slots[i];
        memset(&s, 0, sizeof(s));
        AcReset(i);
        s.used = true; s.ghost = true; s.authed = true;
        s.lastSeen = s.joinedMs = MP_NowMs();
        s.pair = s.pairReq = MP_INVALID_ID;
        char nm[MP_NAME_LEN];
        snprintf(nm, sizeof(nm), "%.*s (PC)", MP_NAME_LEN - 6, name && *name ? name : "Player");
        MP_CopyStr(s.name, nm, MP_NAME_LEN);
        s.color = color ? color : 0xFFFFFFFF;
        s.skin = skin;
        Log("%s (id %d) joined from open.mp (PC cross-play)", s.name, i);
        AnnounceJoin(i);
        return i;
    }
    return -1;
}

void MPServer::BridgeSync(int slot, const SyncData& d)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].ghost) return;
    Slot& me = m_Slots[slot];
    PktSync p; memset(&p, 0, sizeof(p));
    p.type = PKT_SYNC; p.id = (uint8_t)slot; p.seq = m_nBridgeSeq++; p.d = d;
    memcpy(me.pos, (d.flags & SYNC_IN_CAR) ? (const void*)d.carPos : (const void*)d.pos, sizeof(me.pos));
    me.hasPos = true;
    const uint8_t stateMask = SYNC_IN_CAR | SYNC_DEAD | SYNC_MISSION | SYNC_CUTSCENE | SYNC_DRIVER;
    bool important = ((d.flags ^ me.lastFlags) & stateMask) != 0;
    me.lastFlags = d.flags; me.skin = d.skin; me.lastSeen = MP_NowMs();
    me.lastSync = d; me.hasSync = true;
    RelaySync(slot, &p, (int)sizeof(p), important);
}

void MPServer::BridgeChat(int slot, const char* text)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].ghost || !text) return;
    PktChat p; memset(&p, 0, sizeof(p));
    p.type = PKT_CHAT; p.id = (uint8_t)slot;
    MP_CopyStr(p.text, text, sizeof(p.text));
    Log("[chat] %s: %s", m_Slots[slot].name, p.text);
    Broadcast(&p, sizeof(p));
}

void MPServer::BridgeRemove(int slot)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used || !m_Slots[slot].ghost) return;
    Drop(slot, LEAVE_QUIT);
}

void MPServer::BridgePeers(std::vector<BridgePeer>& out)
{
    out.clear();
    std::lock_guard<std::mutex> lk(m_Mutex);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        const Slot& s = m_Slots[i];
        if(!s.used || s.ghost || !s.authed) continue;
        BridgePeer b; memset(&b, 0, sizeof(b));
        b.slot = i; MP_CopyStr(b.name, s.name, MP_NAME_LEN); b.skin = s.skin;
        b.hasSync = s.hasSync; if(s.hasSync) b.d = s.lastSync;
        out.push_back(b);
    }
}

void MPServer::BridgeTakeChat(std::vector<std::pair<int, std::string>>& out)
{
    std::lock_guard<std::mutex> lk(m_Mutex);
    out.swap(m_BridgeChatOut);
    m_BridgeChatOut.clear();
}
