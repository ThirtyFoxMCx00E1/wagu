// 4.08: Project Day Z Legacy - base building, the server side (included at the end of server_dayz.inc).
// Every piece (net/dz_build.h) is kept in dayz_bases.txt next to the survivors file and sent to every
// phone on the Day Z server. Building needs a Toolbox in the bag; a survivor can have DZB_MAX_OWN
// pieces. Only the owner removes a piece; the owner and his party open / close the doors.

void MPServer::DzBaseLoad()
{
    m_DzBase.clear(); m_nDzBaseId = 1;
    FILE* f = fopen(DzSideFile("dayz_bases.txt").c_str(), "r"); if(!f) return;
    char line[256];
    while(fgets(line, sizeof(line), f))
    {
        if(line[0] != 'B' || line[1] != ' ') continue;
        unsigned id = 0; int kind = 0, tier = 0, flags = 0, head = 0; float x = 0, y = 0, z = 0; char owner[64] = "";
        if(sscanf(line + 2, "%u %d %d %d %f %f %f %d %63s", &id, &kind, &tier, &flags, &x, &y, &z, &head, owner) != 9) continue;
        if(kind < 0 || kind >= DZB_COUNT || tier < 0 || tier >= DZBT_COUNT || !id) continue;
        DzPiece p; memset(&p.r, 0, sizeof(p.r));
        p.r.id = id; p.r.kind = (uint8_t)kind; p.r.tier = (uint8_t)tier; p.r.flags = (uint8_t)(flags & 1);
        p.r.x = x; p.r.y = y; p.r.z = z; p.r.head10 = (int16_t)head; p.r.hp = 100; p.owner = owner;
        m_DzBase.push_back(p);
        if(id >= m_nDzBaseId) m_nDzBaseId = id + 1;
    }
    fclose(f);
    Log("DAYZ: %d base pieces loaded", (int)m_DzBase.size());
}
void MPServer::DzBaseSave()
{
    std::string path = DzSideFile("dayz_bases.txt"), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
    fprintf(f, "# Project Day Z Legacy bases (4.08): B id kind tier flags x y z heading*10 owner\n");
    for(const auto& p : m_DzBase)
        fprintf(f, "B %u %d %d %d %.3f %.3f %.3f %d %s\n", p.r.id, p.r.kind, p.r.tier, p.r.flags & 1, p.r.x, p.r.y, p.r.z, p.r.head10, p.owner.c_str());
    fclose(f); rename(tmp.c_str(), path.c_str());
    m_bDzBaseDirty = false;
}
bool MPServer::DzBaseMayUse(int slot, const DzPiece& p)
{
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb));
    if(key && p.owner == key) return true;
    const int party = m_Dz[slot].party; if(party < 0) return false;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {   // the owner is online and in my party
        if(i == slot || !m_Slots[i].used || m_Dz[i].party != party) continue;
        char k2[40]; const char* ok = RpKey(i, k2, sizeof(k2)); if(ok && p.owner == ok) return true;
    }
    return false;
}
// the whole list to one phone (on join), with "yours" set per phone
void MPServer::DzBaseSend(int slot)
{
    PktDzBase b; memset(&b, 0, sizeof(b)); b.type = PKT_DZ_BASE; b.op = DZBO_LIST; b.total = (uint16_t)m_DzBase.size(); b.max = DZB_MAX_OWN;
    auto flush = [&](bool force) {
        if(!b.n && !force) return;
        SendTo(slot, &b, (int)(offsetof(PktDzBase, r) + sizeof(DzBaseRow) * b.n));
        b.n = 0; b.op = DZBO_MORE;
    };
    for(const auto& p : m_DzBase)
    {
        DzBaseRow& r = b.r[b.n++]; r = p.r; r.flags = (uint8_t)((p.r.flags & 1) | (DzBaseMayUse(slot, p) ? 2 : 0));
        if(b.n == DZB_PAGE) flush(false);
    }
    flush(b.op == DZBO_LIST);   // an empty list still clears the phone
}
void MPServer::DzBaseSendOne(const DzPiece& p, uint8_t op)
{
    PktDzBase b; memset(&b, 0, sizeof(b)); b.type = PKT_DZ_BASE; b.op = op; b.n = 1; b.total = (uint16_t)m_DzBase.size(); b.max = DZB_MAX_OWN;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!m_Slots[i].used) continue;
        b.r[0] = p.r; b.r[0].flags = (uint8_t)((p.r.flags & 1) | (op != DZBO_DEL && DzBaseMayUse(i, p) ? 2 : 0));
        SendTo(i, &b, (int)(offsetof(PktDzBase, r) + sizeof(DzBaseRow)));
    }
}
void MPServer::DzBaseCmd(int slot, const PktDzCmd& c)
{
    DzSurvivor& z = m_Dz[slot];
    if(!z.alive || !z.spawned) return;
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb));
    if(!key) { DzMsg(slot, DZC_RED, "[BASE] Log in to an account to build."); return; }
    const float* me = m_Slots[slot].pos;
    if(c.cmd == DZCMD_BASE_PLACE)
    {
        if(c.a >= DZB_COUNT || c.b >= DZBT_COUNT) return;
        bool tool = false; for(int k = 0; k < z.nInv; ++k) if(z.inv[k].item == DZI_TOOLBOX) tool = true;
        if(!tool) { DzMsg(slot, DZC_RED, "[BASE] You need a Toolbox in your bag to build."); return; }
        const float x = c.pos[0], y = c.pos[1], zz = c.pos[2];
        if(!(x == x) || !(y == y) || !(zz == zz) || fabsf(x) > 3000.0f || fabsf(y) > 3000.0f || zz < -50.0f || zz > 800.0f) return;
        float dx = x - me[0], dy = y - me[1], dz = zz - me[2];
        if(dx * dx + dy * dy + dz * dz > DZB_REACH * DZB_REACH) { DzMsg(slot, DZC_RED, "[BASE] Too far away."); return; }
        int mine = 0;
        for(const auto& p : m_DzBase)
        {
            if(p.owner == key) ++mine;
            float ex = p.r.x - x, ey = p.r.y - y, ez = p.r.z - zz;
            const bool sameGroup = (DzPieceFlat(p.r.kind) == DzPieceFlat(c.a)) && ((p.r.kind == DZB_DOOR) == (c.a == DZB_DOOR));
            if(sameGroup && ex * ex + ey * ey < 0.35f * 0.35f && fabsf(ez) < 0.4f) { DzMsg(slot, DZC_RED, "[BASE] There is already a piece there."); return; }
        }
        if(mine >= DZB_MAX_OWN) { DzMsg(slot, DZC_RED, "[BASE] You have %d pieces - that is the limit. Remove some first.", mine); return; }
        if((int)m_DzBase.size() >= DZB_MAX_ALL) { DzMsg(slot, DZC_RED, "[BASE] The server is full of bases."); return; }
        DzPiece p; memset(&p.r, 0, sizeof(p.r));
        p.r.id = m_nDzBaseId++; p.r.kind = c.a; p.r.tier = c.b; p.r.x = x; p.r.y = y; p.r.z = zz;
        int h = c.val % 3600; if(h < 0) h += 3600; p.r.head10 = (int16_t)h; p.r.hp = 100; p.owner = key;
        m_DzBase.push_back(p); m_bDzBaseDirty = true;
        DzBaseSendOne(p, DZBO_SET);
        DzLog("BASE %s built a %s %s (%d) at %.1f %.1f %.1f", m_Slots[slot].name, kDzTierName[c.b], kDzPieceName[c.a], (int)p.r.id, x, y, zz);
        return;
    }
    // remove / door: the piece by id
    for(size_t i = 0; i < m_DzBase.size(); ++i)
    {
        DzPiece& p = m_DzBase[i]; if(p.r.id != (uint32_t)c.val) continue;
        float dx = p.r.x - me[0], dy = p.r.y - me[1]; if(dx * dx + dy * dy > 8.0f * 8.0f) { DzMsg(slot, DZC_RED, "[BASE] Too far away."); return; }
        if(c.cmd == DZCMD_BASE_DEL)
        {
            if(p.owner != key) { DzMsg(slot, DZC_RED, "[BASE] That is not your piece."); return; }
            DzPiece gone = p; m_DzBase.erase(m_DzBase.begin() + (long)i); m_bDzBaseDirty = true;
            DzBaseSendOne(gone, DZBO_DEL);
            return;
        }
        if(c.cmd == DZCMD_BASE_DOOR)
        {
            if(p.r.kind != DZB_DOOR) return;
            if(!DzBaseMayUse(slot, p)) { DzMsg(slot, DZC_RED, "[BASE] The door is locked."); return; }
            p.r.flags ^= 1; m_bDzBaseDirty = true;
            DzBaseSendOne(p, DZBO_SET);
            return;
        }
    }
}
