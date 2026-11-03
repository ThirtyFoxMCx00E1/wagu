// ================================================================================================
// 3.50: server anti-cheat (included by server.cpp). Ported from the ideas, limits and warning
// counts of Nex-AC 1.9.69 (https://github.com/ThirtyFoxMCx00E1/Nex-AC) to the MP protocol: the
// server only sees what the phones send (sync, shots, damage, deaths, pickups, car state...), so
// every check works on those packets. Every function runs with m_Mutex held (Handle / Loop).
//
// Always on (all checks) on the default server, Mega Vil Futuristic, Brief Case War and MW3.
// Custom servers: SetAntiCheat(on, mask) from the Add server page (Gamemode -> Anti-cheat).
// Checks marked "cheat" in g_AcInfo only run while the MP / CLEO menu cheats are off on this
// server (PERM_NO_CHEATS and no CheatMenu), so the default server's own menu cheats keep working.
// Staff (admin, moderators, the host phone) are never kicked: they only get the warning.
// ================================================================================================

static inline float AcDist2(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1]; return sqrtf(x * x + y * y); }
static inline float AcDist3(const float* a, const float* b) { float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2]; return sqrtf(x * x + y * y + z * z); }
static inline bool  AcAirModel(int m)
{
    static const uint16_t k[] = { 417, 425, 447, 460, 469, 476, 487, 488, 497, 511, 512, 513, 519, 520, 548, 553, 563, 577, 592, 593 };
    for(uint16_t v : k) if(v == m) return true;
    return false;
}
static inline bool AcBoatModel(int m)
{
    static const uint16_t k[] = { 430, 446, 452, 453, 454, 472, 473, 484, 493, 595 };
    for(uint16_t v : k) if(v == m) return true;
    return false;
}
static inline bool AcValidWeapon(int w) { return w >= 0 && w <= 46 && !(w >= 19 && w <= 21); }
// Nex-AC ac_wMinRange-like fire intervals (ms between two shots, lowest legit value with a fast tap)
static int AcFireMs(int w)
{
    switch(w)
    {
        case 22: case 23: return 110;  case 24: return 480;  case 25: return 700;  case 26: return 110;
        case 27: return 230;  case 28: case 32: return 35;  case 29: return 60;  case 30: case 31: return 75;
        case 33: case 34: return 800;  case 35: case 36: return 2000;  case 38: return 15;
        default: return 0;
    }
}
static int AcMagSize(int w)
{
    switch(w)
    {
        case 22: return 34; case 23: return 17; case 24: return 7; case 26: return 4; case 27: return 7;
        case 28: case 32: return 100; case 29: return 30; case 30: return 30; case 31: return 50;
        default: return 0;   // single shot / melee / thrown / no reload
    }
}
static int AcMaxRange(int w)   // metres a hit can come from (2x the weapon range, Nex-AC ac_wMinRange)
{
    switch(w)
    {
        case 22: case 23: case 24: return 70;  case 25: case 26: case 27: return 80;
        case 28: case 32: return 70;  case 29: return 90;  case 30: return 140;  case 31: return 180;
        case 33: return 200;  case 34: return 600;  case 38: return 160;
        default: return 0;   // melee, explosives, fire, cars: no range check
    }
}
// mod garages (Transfender LS / SF / LV, Loco Low Co, Wheel Arch Angels)
static const float k_AcModShops[][3] = { { 1041.4f, -1023.0f, 32.1f }, { -1935.9f, 237.6f, 34.3f }, { 2386.6f, 1041.4f, 10.8f },
                                         { 2645.2f, -2039.2f, 13.6f }, { -2720.2f, 217.4f, 4.5f } };

bool MPServer::AcIsOn() const
{
    if(IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer()) return true;
    return m_bAcCustom.load();
}
uint64_t MPServer::AcMask() const
{
    if(IsDefaultServer() || IsBcwServer() || IsMw3Server() || IsRpServer()) return AC_ALL_MASK;
    return m_nAcMask.load();
}
bool MPServer::AcCheck(int c) const
{
    if(!AcIsOn() || !AcMaskHas(AcMask(), c)) return false;
    if(g_AcInfo[c].cheat && (!(m_nPerms & PERM_NO_CHEATS) || (m_nPerms & PERM_CHEAT_MENU))) return false;
    return true;
}
bool MPServer::AcExempt(int slot) const
{
    const Slot& s = m_Slots[slot];
    return s.admin || s.mod || s.owner;
}
void MPServer::AcReset(int slot)
{
    memset(&m_Ac[slot], 0, sizeof(m_Ac[slot]));
    const uint64_t now = MP_NowMs();
    m_Ac[slot].joinAt = now;
    m_Ac[slot].graceUntil = now + 20000;
    m_Ac[slot].lastSeat = -2;
}
void MPServer::AcGrace(int slot, uint32_t ms)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    uint64_t t = MP_NowMs() + ms;
    if(m_Ac[slot].graceUntil < t) m_Ac[slot].graceUntil = t;
    m_Ac[slot].hasAnchor = false; m_Ac[slot].hasZ = false;
}
void MPServer::AcStaffNotice(const char* text)
{
    PktServerMsg m; memset(&m, 0, sizeof(m)); m.type = PKT_SERVER_MSG; m.color = 0xFF6347FF;
    snprintf(m.text, sizeof(m.text), "{FF6347}[AC]{FFFFFF} %s", text);
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
        if(m_Slots[i].used && (m_Slots[i].admin || m_Slots[i].mod || m_Slots[i].owner)) SendTo(i, &m, sizeof(m));
}
// One warning for <check>. kickAt warnings inside 2 minutes -> kick (temporary, 4 min 29 s, like /kick).
// Returns false when the player got kicked (the slot is gone: stop handling the packet).
bool MPServer::AcFlag(int slot, int check, int kickAt, const char* fmt, ...)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS || !m_Slots[slot].used) return false;
    AcSlot& a = m_Ac[slot];
    const uint64_t now = MP_NowMs();
    if(now - a.flagAt[check] < 1500 && a.flags[check]) return true;   // one warning per 1.5 s and check
    if(now - a.flagAt[check] > 120000) a.flags[check] = 0;
    a.flagAt[check] = now;
    ++a.flags[check];
    char det[96]; va_list ap; va_start(ap, fmt); vsnprintf(det, sizeof(det), fmt, ap); va_end(ap);
    char line[200];
    snprintf(line, sizeof(line), "%s(%d): %s (%s) %d/%d", m_Slots[slot].name, slot, g_AcInfo[check].name, det, a.flags[check], kickAt);
    Log("[anticheat] %s", line);
    ++m_nAcEvents;
    if(a.flags[check] < kickAt || AcExempt(slot)) { AcStaffNotice(line); return true; }
    char reason[80]; snprintf(reason, sizeof(reason), "Anti-cheat: %s", g_AcInfo[check].name);
    AcStaffNotice(line);
    // 3.58: floating in the sky / teleporting / airbreak again and again = permanent ban
    //       (only [ADMIN] or [MOD] can /unban it). Counted per phone id for the life of the server.
    if(check == AC_FLY || check == AC_TELEPORT || check == AC_AIRBREAK)
    {
        std::string key = m_Slots[slot].uid[0] ? m_Slots[slot].uid : m_Slots[slot].name;
        int n = ++m_AcRepeat[key];
        if(n >= 3)
        {
            snprintf(reason, sizeof(reason), "Anti-cheat: %s (%d kicks) - permanent ban", g_AcInfo[check].name, n);
            KickLocked(slot, "Anti-cheat", reason, true);
            return false;
        }
        snprintf(reason, sizeof(reason), "Anti-cheat: %s (%d/3 - then a permanent ban)", g_AcInfo[check].name, n);
    }
    KickLocked(slot, "Anti-cheat", reason, false);
    return false;
}

// ---- per address: DoS, Rcon (key) brute force, connection flood --------------------------------
MPServer::AcAddr* MPServer::AcAddrOf(const sockaddr_in& from, bool portToo)
{
    const uint32_t ip = from.sin_addr.s_addr; const uint16_t port = portToo ? from.sin_port : 0;
    for(AcAddr& e : m_AcAddrs) if(e.ip == ip && e.port == port) return &e;
    const uint64_t now = MP_NowMs();
    if(m_AcAddrs.size() >= 2048)
    {   // forget the oldest idle entries
        m_AcAddrs.erase(std::remove_if(m_AcAddrs.begin(), m_AcAddrs.end(), [now](const AcAddr& e)
            { return e.blockUntil < now && now - e.seenAt > 600000; }), m_AcAddrs.end());
        if(m_AcAddrs.size() >= 2048) m_AcAddrs.erase(m_AcAddrs.begin());
    }
    AcAddr e; memset(&e, 0, sizeof(e)); e.ip = ip; e.port = port; e.seenAt = now;
    m_AcAddrs.push_back(e);
    return &m_AcAddrs.back();
}
bool MPServer::AcBlocked(const sockaddr_in& from)
{
    const uint64_t now = MP_NowMs();
    for(const AcAddr& e : m_AcAddrs)
        if(e.ip == from.sin_addr.s_addr && (e.port == 0 || e.port == from.sin_port) && e.blockUntil > now) return true;
    return false;
}

// Every packet: Anti-DoS (per address+port: over 600 packets in one second -> 60 s block) and
// Anti-callback flood (per player and packet type, Nex-AC ac_Mtfc intervals). false = drop it.
bool MPServer::AcPacket(const sockaddr_in& from, int slot, const uint8_t* data, int len)
{
    if(!AcIsOn() || len < 1) return true;
    const uint64_t now = MP_NowMs();
    if(AcCheck(AC_DOS))
    {
        if(!m_AcAddrs.empty() && AcBlocked(from)) return false;
        AcAddr* e = AcAddrOf(from, true);
        e->seenAt = now;
        if(now - e->winAt >= 1000) { e->winAt = now; e->pkts = 0; }
        if(++e->pkts > 600)
        {
            e->blockUntil = now + 60000; e->pkts = 0;
            char ip[32]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
            Log("[anticheat] DoS: %s:%d blocked for 60 s", ip, ntohs(from.sin_port));
            if(slot >= 0 && m_Slots[slot].used && !AcExempt(slot)) { AcFlag(slot, AC_DOS, 1, "packet flood"); }
            return false;
        }
    }
    if(slot < 0 || !m_Slots[slot].used || !AcCheck(AC_CALLBACK_FLOOD)) return true;
    // { type, min interval ms, warnings before the kick } (Nex-AC: OnDialogResponse 120/8, OnPlayerText 450/8,
    // OnPlayerCommandText 400/8, OnPlayerEnterVehicle 50/8, OnPlayerPickUpPickup 50/11, OnPlayerRequestClass 150/8,
    // OnPlayerEnterCheckpoint 150/8, OnVehicleDamageStatusUpdate 60/9, OnVehicleMod 450/2, OnPlayerDeath 300/2)
    struct Fl { uint8_t type; uint16_t ms; uint8_t kickAt; };
    static const Fl k[] = {
        { PKT_DIALOG_RESPONSE, 120, 8 }, { PKT_CHAT, 450, 8 }, { PKT_JACK, 50, 8 }, { PKT_PICKUP_TAKE, 50, 11 },
        { PKT_TEAM, 150, 8 }, { PKT_EVENT, 150, 8 }, { PKT_HOME_CMD, 150, 8 }, { PKT_DEATH, 300, 3 },
        { PKT_CLOTHES, 150, 8 }, { PKT_PAUSE, 100, 12 }, { PKT_RP_CMD, 150, 10 },
    };
    for(int i = 0; i < (int)(sizeof(k) / sizeof(k[0])); ++i)
    {
        if(k[i].type != data[0]) continue;
        AcSlot& a = m_Ac[slot];
        const int fi = i < AC_FLOOD_TYPES ? i : AC_FLOOD_TYPES - 1;
        const uint64_t last = a.floodAt[fi];
        a.floodAt[fi] = now;
        if(last && now - last < k[i].ms)
        {
            if(++a.floodN[fi] >= k[i].kickAt) { a.floodN[fi] = 0; AcFlag(slot, AC_CALLBACK_FLOOD, 1, "packet %d flood", (int)data[0]); }
            return false;
        }
        if(a.floodN[fi] && now - last > 5000) a.floodN[fi] = 0;
        return true;
    }
    return true;
}

// PKT_JOIN of a new player (no slot yet). false = refused (already answered with a reject).
bool MPServer::AcJoin(const sockaddr_in& from, const char* name, const char* uid, uint32_t build, int proto)
{
    if(!AcIsOn()) return true;
    const uint64_t now = MP_NowMs();
    auto reject = [&](const char* why) {
        PktReject rj; rj.type = PKT_REJECT; rj.reason = REJECT_KICKED;
        SendRaw(from, &rj, sizeof(rj));
        char ip[32]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        Log("[anticheat] join refused (%s): %s %s", why, name ? name : "?", ip);
        ++m_nAcEvents;
        return false;
    };
    if(AcCheck(AC_DOS) && AcBlocked(from)) return reject("blocked address");
    if(AcCheck(AC_INVALID_VERSION))
    {   // a build from the future, or a name the phone can't type
        if(build > (uint32_t)MP_BUILD + 20000u) return reject("invalid version");
        if(proto <= 0 || proto > MP_PROTO_VERSION + 50) return reject("invalid protocol");
        for(const unsigned char* c = (const unsigned char*)name; c && *c; ++c) if(*c < 32 || *c == '%' || *c == '~') return reject("invalid name");
    }
    if(AcCheck(AC_FAKE_NPC))
    {   // NPC names are the server's (the 24/7 bot), and every phone since 2.25 has an id
        if(name && (!strncasecmp(name, "[NPC]", 5) || !strncasecmp(name, "[BOT]", 5) || !strncasecmp(name, "NPC_", 4))) return reject("fake NPC");
    }
    if(AcCheck(AC_RECONNECT) && uid && uid[0])
    {   // Nex-AC: more than 3 reconnects in a minute -> wait
        AcUid* u = nullptr;
        for(AcUid& x : m_AcUids) if(!strcmp(x.uid, uid)) { u = &x; break; }
        if(!u)
        {
            if(m_AcUids.size() >= 1024) m_AcUids.erase(m_AcUids.begin());
            AcUid x; memset(&x, 0, sizeof(x)); MP_CopyStr(x.uid, uid, sizeof(x.uid));
            m_AcUids.push_back(x); u = &m_AcUids.back();
        }
        if(u->blockUntil > now) return reject("reconnect spam");
        if(now - u->winAt > 60000) { u->winAt = now; u->n = 0; }
        if(++u->n > 4) { u->blockUntil = now + 30000; return reject("reconnect spam"); }
    }
    if(AcCheck(AC_SLOT_FLOOD))
    {   // the same address+port knocking again and again (one slot flooded with joins)
        AcAddr* e = AcAddrOf(from, true);
        if(now - e->joinWin > 10000) { e->joinWin = now; e->joins = 0; }
        if(++e->joins > 6) { e->blockUntil = now + 60000; return reject("connection flood in one slot"); }
    }
    return true;
}
// a wrong admin / moderator key or login: 3 wrong keys from one IP in 10 minutes -> 30 minutes blocked
void MPServer::AcKeyFail(const sockaddr_in& from, const char* what)
{
    if(!AcCheck(AC_RCON)) return;
    const uint64_t now = MP_NowMs();
    AcAddr* e = AcAddrOf(from, true);   // address + port: players behind one relay / NAT share the IP
    if(now - e->keyWin > 600000) { e->keyWin = now; e->keyFails = 0; }
    const int limit = !strcmp(what, "login") ? 10 : 5;
    if(++e->keyFails >= limit)
    {
        e->keyBlockUntil = now + 30 * 60000ull; e->keyFails = 0;
        char ip[32]; inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
        Log("[anticheat] Rcon hack: %s:%d blocked for 30 minutes (%s brute force)", ip, ntohs(from.sin_port), what);
        char line[120]; snprintf(line, sizeof(line), "%s brute force from %s: blocked for 30 minutes", what, ip);
        AcStaffNotice(line);
        ++m_nAcEvents;
    }
}
bool MPServer::AcKeyBlocked(const sockaddr_in& from)
{
    if(!AcCheck(AC_RCON)) return false;
    const uint64_t now = MP_NowMs();
    for(const AcAddr& e : m_AcAddrs) if(e.ip == from.sin_addr.s_addr && e.port == from.sin_port && e.keyBlockUntil > now) return true;
    return false;
}

// ---- PKT_SYNC ----------------------------------------------------------------------------------
bool MPServer::AcSync(int slot, const PktSync& p, const uint8_t* raw, int rawLen)
{
    if(!AcIsOn()) return true;
    AcSlot& a = m_Ac[slot];
    Slot& s = m_Slots[slot];
    const uint64_t now = MP_NowMs();
    const SyncData& d = p.d;
    SyncExt e; memset(&e, 0, sizeof(e));
    const bool hasExt = rawLen > (int)sizeof(PktSync) && MP_DecodeSyncExt(raw + sizeof(PktSync), rawLen - (int)sizeof(PktSync), e);
    const bool inCar = (d.flags & SYNC_IN_CAR) != 0, driver = inCar && (d.flags & SYNC_DRIVER);
    const bool dead = (d.flags & SYNC_DEAD) != 0;
    const bool story = (d.flags & (SYNC_MISSION | SYNC_CUTSCENE | SYNC_SCENE)) || s.status;
    float pos[3]; memcpy(pos, inCar ? d.carPos : d.pos, sizeof(pos));
    if(!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2]) || fabsf(pos[0]) > 20000.f || fabsf(pos[1]) > 20000.f || fabsf(pos[2]) > 20000.f)
    {   // invalid position = crasher
        if(AcCheck(AC_CRASHERS)) return AcFlag(slot, AC_CRASHERS, 1, "invalid position"), false;
        return false;
    }
    if(!a.firstSyncAt) a.firstSyncAt = now;
    a.lastSyncAt = now;

    // crashers: weapon ids that don't exist, impossible seats
    if(AcCheck(AC_CRASHERS))
    {
        if(!AcValidWeapon(d.weapon)) { AcFlag(slot, AC_CRASHERS, 1, "invalid weapon %d", d.weapon); return false; }
        if(hasExt && e.hasVeh && (e.vehSeat < -1 || e.vehSeat > 8)) { AcFlag(slot, AC_CRASHERS, 1, "invalid seat %d", e.vehSeat); return false; }
        if(inCar && d.carModel && (d.carModel < 400 || d.carModel > 611)) { AcFlag(slot, AC_CRASHERS, 1, "invalid car %d", d.carModel); return false; }
    }

    // FakeSpawn: alive again right after dying (the respawn takes a few seconds in SA)
    if(dead && !(a.lastFlags & SYNC_DEAD)) a.deadAt = now;
    if(!dead && (a.lastFlags & SYNC_DEAD))
    {
        if(AcCheck(AC_FAKE_SPAWN) && a.deadAt && now - a.deadAt < 1200 && !story)
            if(!AcFlag(slot, AC_FAKE_SPAWN, 3, "respawned in %d ms", (int)(now - a.deadAt))) return false;
        AcGrace(slot, 6000);   // hospital / spawn point
    }
    if(dead) { a.hasAnchor = false; a.hasZ = false; }
    if(a.lastInterior != (hasExt && e.hasX2 ? e.interior : a.lastInterior)) AcGrace(slot, 5000);   // EnEx marker
    if(hasExt && e.hasX2) a.lastInterior = e.interior;
    if(((a.lastFlags ^ d.flags) & SYNC_IN_CAR)) { a.hasAnchor = false; a.hasZ = false; }
    a.lastFlags = d.flags;

    // seat flood (Nex-AC: protection from flood by seat changing)
    {
        const int seat = !inCar ? -1 : (hasExt && e.hasVeh ? e.vehSeat + 1 : 0) + (int)d.carModel * 16;
        if(a.lastSeat != -2 && seat != a.lastSeat && AcCheck(AC_SEAT_FLOOD))
        {
            if(now - a.seatWin > 4000) { a.seatWin = now; a.seatN = 0; }
            if(++a.seatN > 6) { a.seatN = 0; if(!AcFlag(slot, AC_SEAT_FLOOD, 2, "seat changes")) return false; }
        }
        a.lastSeat = seat;
    }

    // lagcomp spoof: the phone's clock against ours over 10 s of continuous syncs
    if(hasExt && e.time && AcCheck(AC_LAGCOMP_SPOOF))
    {
        if(!a.clkBase || now - a.clkPrevNow > 2500 || e.time < a.clkPrev) { a.clkBase = e.time; a.clkBaseNow = now; }
        else if(now - a.clkBaseNow >= 10000)
        {
            const double sent = (double)(e.time - a.clkBase), real = (double)(now - a.clkBaseNow);
            const double ratio = sent / real;
            if(ratio > 1.35 || ratio < 0.65) { if(!AcFlag(slot, AC_LAGCOMP_SPOOF, 3, "clock x%.2f", ratio)) return false; }
            a.clkBase = e.time; a.clkBaseNow = now;
        }
        a.clkPrev = e.time; a.clkPrevNow = now;
    }

    // AFK ghost: marked as paused (can't be hurt) while still moving around
    if(s.paused && a.pauseAnchor && AcCheck(AC_AFK_GHOST))
    {
        if(AcDist3(pos, a.pausePos) > 4.0f) { a.pauseAnchor = false; if(!AcFlag(slot, AC_AFK_GHOST, 2, "moving while paused")) return false; }
    }

    // parkour mod / CJ run (animations)
    if(hasExt && !inCar)
    {
        bool parkour = false, cjLoco = false, styleLoco = false;
        for(int i = 0; i < e.animCount && i < MP_ANIM_SLOTS; ++i)
        {
            const AnimSync& an = e.anim[i];
            char blk[17]; memcpy(blk, an.block, 16); blk[16] = 0;
            for(char* c = blk; *c; ++c) if(*c >= 'A' && *c <= 'Z') *c += 32;
            if(strstr(blk, "parkour") || strstr(blk, "freerun")) parkour = true;
            if(an.id >= 0 && an.id <= 11 && an.blend > 100) { if(an.group == 0) cjLoco = true; else if(an.group >= 54 && an.group <= 87) styleLoco = true; }
        }
        if(parkour && AcCheck(AC_PARKOUR)) { if(!AcFlag(slot, AC_PARKOUR, 3, "parkour anims")) return false; }
        // CJ run: this skin walked with its own walk style before, now it runs with CJ's anims
        if(d.skin != a.styleSkin) { a.styleSkin = d.skin; a.styleSeen = false; }
        if(styleLoco) a.styleSeen = true;
        if(d.skin != 0 && a.styleSeen && cjLoco && !styleLoco && d.speed > 4.0f && AcCheck(AC_CJ_RUN))
            if(!AcFlag(slot, AC_CJ_RUN, 8, "skin %d", d.skin)) return false;
    }

    // health / armour (Nex-AC: health hack, armour hack)
    if(AcCheck(AC_HEALTH) && !story && now > a.healGrace)
    {
        if(d.health > 200 || d.armour > 150) { if(!AcFlag(slot, AC_HEALTH, 2, "health %d armour %d", d.health, d.armour)) return false; }
    }
    // godmode: pending damage check (damage >= 30 that never lowered health + armour)
    if(a.gmAt && now - a.gmAt > 1800)
    {
        const int hpNow = d.health + d.armour;
        if(AcCheck(AC_GODMODE) && !story && !dead && !s.paused && hpNow >= a.gmHp && now > a.healGrace)
        { a.gmAt = 0; if(!AcFlag(slot, AC_GODMODE, 3, "%d damage, health stays %d", (int)a.gmAmt, hpNow)) return false; }
        a.gmAt = 0;
    }
    a.health = d.health; a.armour = d.armour;

    // special actions: jetpack (Mega Vil sells jetpacks: allowed after the server gave one)
    if(hasExt && e.hasX2 && (e.x2flags & X2_JETPACK) && !story && AcCheck(AC_SPECIAL_ACTION) && !a.jetOk)
    {
        const int kickAt = (IsBcwServer() || IsMw3Server()) ? 1 : 3;
        if(!AcFlag(slot, AC_SPECIAL_ACTION, kickAt, "jetpack")) return false;
    }
    // invisible (Nex-AC: invisible hack): moving while hidden from the map + the other players
    if(hasExt && e.hasX2 && (e.x2flags & X2_INVISIBLE) && !story && AcCheck(AC_INVISIBLE))
    {
        if(!a.invisSince) a.invisSince = now;
        if(now - a.invisSince > 5000 && d.speed > 1.0f) { a.invisSince = now; if(!AcFlag(slot, AC_INVISIBLE, 2, "invisible")) return false; }
    }
    else a.invisSince = 0;

    // unfreeze: jailed / frozen by the server and walking away
    if(a.frozen && AcCheck(AC_UNFREEZE) && now > a.frozenAt + 8000)
    {
        if(!a.freezeAnchor) { memcpy(a.freezePos, pos, sizeof(pos)); a.freezeAnchor = true; }
        else if(AcDist3(pos, a.freezePos) > 80.0f) { a.freezeAnchor = false; if(!AcFlag(slot, AC_UNFREEZE, 2, "left the jail")) return false; }
    }
    // NOPs: the server moved this player (RP teleports, Cop vs Robber jail...) and they never arrived
    if(a.tpPending && now - a.tpAt > 1500)
    {
        if(AcDist3(pos, a.tpPos) < 60.0f) a.tpPending = false;
        else if(now - a.tpAt > 15000)
        {
            a.tpPending = false;
            if(AcCheck(AC_NOPS) && !story && !dead) { if(!AcFlag(slot, AC_NOPS, 3, "ignored a server teleport")) return false; }
        }
    }

    // ---- movement: airbreak, teleport, fly, speed, quick turn, carshot (needs a 0.5 s window) ----
    const bool grace = now < a.graceUntil || story || dead || s.paused || (hasExt && e.hasVeh);
    if(grace) { a.hasAnchor = false; a.hasZ = false; }
    float vel = 0.f, vel2 = 0.f;
    if(hasExt) { vel2 = sqrtf(e.vel[0] * e.vel[0] + e.vel[1] * e.vel[1]); vel = sqrtf(vel2 * vel2 + e.vel[2] * e.vel[2]); }
    else vel = vel2 = d.speed;
    const bool jet = hasExt && e.hasX2 && (e.x2flags & X2_JETPACK);
    if(!grace && driver && hasExt)
    {
        // quick turn: a fast car whose velocity points the other way 0.25 s later
        if(a.velAt && now - a.velAt < 260 && AcCheck(AC_QUICK_TURN))
        {
            const float pv = sqrtf(a.vel[0] * a.vel[0] + a.vel[1] * a.vel[1]);
            if(pv > 15.f && vel2 > 15.f)
            {
                const float dot = (a.vel[0] * e.vel[0] + a.vel[1] * e.vel[1]) / (pv * vel2);
                if(dot < -0.7f) { if(!AcFlag(slot, AC_QUICK_TURN, 3, "%.0f m/s", vel2)) return false; }
            }
        }
        // carshot: a car going from standstill to bullet speed at once (Nex-AC: CarShot)
        if(a.velAt && now - a.velAt < 400 && AcCheck(AC_CARSHOT) && !AcAirModel(d.carModel))
        {
            const float pv = sqrtf(a.vel[0] * a.vel[0] + a.vel[1] * a.vel[1] + a.vel[2] * a.vel[2]);
            if(pv < 4.f && vel > 45.f) { if(!AcFlag(slot, AC_CARSHOT, 4, "0 -> %.0f m/s", vel)) return false; }
        }
    }
    if(hasExt) { memcpy(a.vel, e.vel, sizeof(a.vel)); a.velAt = now; }

    if(!grace)
    {
        if(!a.hasAnchor) { memcpy(a.anchor, pos, sizeof(pos)); a.anchorAt = now; a.hasAnchor = true; a.overN = 0; }
        else if(now - a.anchorAt >= 500)
        {
            const float el = (now - a.anchorAt) / 1000.f;
            const float dh = AcDist2(pos, a.anchor), dz = pos[2] - a.anchor[2];
            const float vs = dh / el;
            // teleport: 120 m in under 2 s, on foot or in a car (into / in / between vehicles)
            if(dh > 120.f && el < 2.0f && AcCheck(AC_TELEPORT))
            { if(!AcFlag(slot, AC_TELEPORT, 3, "%.0f m in %.1f s%s", dh, el, inCar ? " (vehicle)" : "")) return false; }
            else if(AcCheck(AC_AIRBREAK) && hasExt && !jet && dh > 6.f)
            {   // airbreak: the position slides fast while the game says we stand still
                const float lim = inCar ? 25.f : 12.f, still = inCar ? 4.f : 2.f;
                if(vs > lim && vel < still && fabsf(dz) < el * 6.f)
                { if(!AcFlag(slot, AC_AIRBREAK, 4, "%.0f m/s while still%s", vs, inCar ? " (vehicle)" : "")) return false; }
            }
            // speed (sustained over 3 windows)
            if(AcCheck(AC_SPEED))
            {
                const float lim = !inCar ? 32.f : AcAirModel(d.carModel) ? 150.f : AcBoatModel(d.carModel) ? 60.f : 95.f;
                const float sp = hasExt ? (inCar && AcAirModel(d.carModel) ? vel : vel2) : vs;
                if(sp > lim && vs > lim * 0.7f && dh < 120.f) { if(++a.overN >= 3) { a.overN = 0; if(!AcFlag(slot, AC_SPEED, 6, "%.0f m/s%s", sp, inCar ? " (vehicle)" : "")) return false; } }
                else a.overN = 0;
            }
            memcpy(a.anchor, pos, sizeof(pos)); a.anchorAt = now;
        }
        // fly: rising without a way to rise (Nex-AC: FlyHack on foot / in vehicle)
        if(!a.hasZ || pos[2] < a.zMin || now - a.zAt > (inCar ? 4000 : 3000)) { a.zMin = pos[2]; a.zAt = now; a.hasZ = true; }
        else if(AcCheck(AC_FLY) && !jet)
        {
            const float rise = pos[2] - a.zMin;
            // 3.58: ANY vehicle in the air (helicopter, jet, a car off a ramp / stunt) is never "fly" - only
            //       a player floating up into the sky ON FOOT. 3 kicks for it = a permanent ban (AcFlag).
            if(!inCar && rise > 15.f) { a.hasZ = false; if(!AcFlag(slot, AC_FLY, 3, "floating %.0f m up on foot", rise)) return false; }
        }
    }
    return true;
}

// ---- PKT_SHOT: rapid fire, infinite ammo, carshot weapons -------------------------------------
bool MPServer::AcShot(int slot, const PktShot& m)
{
    if(!AcIsOn()) return true;
    AcSlot& a = m_Ac[slot];
    const uint64_t now = MP_NowMs();
    int bullets = 0, weapon = 0;
    for(int i = 0; i < m.count && i < MP_SHOT_MAX; ++i) if(m.e[i].kind == 0) { ++bullets; weapon = m.e[i].weapon; }
    if(!bullets) return true;
    if(!AcValidWeapon(weapon))
    { if(AcCheck(AC_CRASHERS)) AcFlag(slot, AC_CRASHERS, 1, "invalid shot weapon %d", weapon); return false; }
    const bool shotgun = weapon >= 25 && weapon <= 27;
    const int shots = shotgun ? 1 : bullets;   // a shotgun sends every pellet
    if(weapon != a.shotWeapon) { a.shotWeapon = (uint8_t)weapon; a.shotWin = now; a.shotN = 0; a.magN = 0; a.lastShot = 0; }
    // rapid fire: more shots in 2 s than the weapon can fire (+50 %)
    const int fms = AcFireMs(weapon);
    if(fms && AcCheck(AC_RAPID_FIRE))
    {
        if(now - a.shotWin > 2000) { a.shotWin = now; a.shotN = 0; }
        a.shotN += shots;
        const int maxN = (int)(2000.f / fms * 1.5f) + 3;
        if(a.shotN > maxN) { a.shotN = 0; if(!AcFlag(slot, AC_RAPID_FIRE, 16, "%s %d shots / 2 s", "weapon", maxN)) return false; }
    }
    // infinite ammo / no reload: a whole extra magazine without the reload pause
    const int mag = AcMagSize(weapon);
    if(mag && AcCheck(AC_WEAPON))
    {
        if(a.lastShot && now - a.lastShot > 900) a.magN = 0;   // a pause long enough for a reload
        a.magN += shots;
        if(a.magN > mag * 2 + 4) { a.magN = 0; if(!AcFlag(slot, AC_WEAPON, 3, "no reload (%d shots)", mag * 2 + 4)) return false; }
    }
    a.lastShot = now;
    return true;
}

// ---- PKT_DAMAGE: pro aim (range / aim point), godmode bookkeeping -----------------------------
bool MPServer::AcDamage(int from, const PktDamage& d)
{
    if(!AcIsOn() || from < 0 || d.target >= MP_MAX_PLAYERS || !m_Slots[d.target].used) return true;
    AcSlot& a = m_Ac[from];
    if(d.weapon <= 46 && d.weapon >= 0 && a.dmgHead < 255)
    {   // remember who hurt whom (FakeKill)
        AcSlot& t = m_Ac[d.target];
        t.hitBy[t.hitHead % 4] = from; t.hitAt[t.hitHead % 4] = MP_NowMs(); ++t.hitHead;
    }
    const Slot& sf = m_Slots[from]; const Slot& st = m_Slots[d.target];
    if(!sf.hasPos || !st.hasPos) return true;
    const int range = AcMaxRange(d.weapon);
    if(range && AcCheck(AC_PRO_AIM))
    {
        const float dist = AcDist3(sf.pos, st.pos);
        if(dist > range) { if(!AcFlag(from, AC_PRO_AIM, 3, "hit from %.0f m (weapon %d)", dist, d.weapon)) return false; return false; }
    }
    // godmode from bullets: a big hit has to lower health + armour within ~2 s
    if(d.amount >= 30.f && range && d.target != from)
    {
        AcSlot& t = m_Ac[d.target];
        if(!t.gmAt) { t.gmAt = MP_NowMs(); t.gmHp = (uint16_t)(t.health + t.armour); t.gmAmt = d.amount; }
    }
    return true;
}
// PKT_DEATH: fake kill = the "killer" never hurt the victim in the last 15 s (the kill is not credited)
void MPServer::AcDeath(int victim, PktDeath& d)
{
    if(!AcIsOn() || d.killer >= MP_MAX_PLAYERS || d.killer == victim || !AcCheck(AC_FAKE_KILL)) return;
    const AcSlot& t = m_Ac[victim];
    const uint64_t now = MP_NowMs();
    for(int i = 0; i < 4; ++i) if(t.hitBy[i] == d.killer && t.hitAt[i] && now - t.hitAt[i] < 15000) return;
    if(d.weapon >= 47) return;   // cars, falls, drowning, explosions: not tracked as damage packets
    const int killer = d.killer;
    d.killer = MP_INVALID_ID;
    AcFlag(victim, AC_FAKE_KILL, 3, "kill credit for %s", m_Slots[killer].used ? m_Slots[killer].name : "?");
}
// PKT_PICKUP_TAKE: taking a pickup from far away
bool MPServer::AcPickup(int slot, const float pos[3])
{
    if(!AcCheck(AC_TELEPORT_PICKUP) || !m_Slots[slot].hasPos) return true;
    const float dist = AcDist3(m_Slots[slot].pos, pos);
    if(dist > 15.f) { AcFlag(slot, AC_TELEPORT_PICKUP, 3, "pickup %.0f m away", dist); return false; }
    return true;
}
// PKT_JACK: jacking somebody far away (Nex-AC: CarJack)
bool MPServer::AcJack(int slot, int victim)
{
    if(!AcCheck(AC_CARJACK) || victim < 0 || victim >= MP_MAX_PLAYERS || !m_Slots[victim].used) return true;
    const Slot& a = m_Slots[slot]; const Slot& b = m_Slots[victim];
    if(!a.hasPos || !b.hasPos) return true;
    const float dist = AcDist3(a.pos, b.pos);
    if(dist > 10.f) { AcFlag(slot, AC_CARJACK, 3, "jack from %.0f m", dist); return false; }
    return true;
}
// PKT_VEH_STATE: tuning crasher + tuning hack
bool MPServer::AcVehState(int slot, const PktVehState& v)
{
    if(!AcIsOn()) return true;
    if(AcCheck(AC_CRASHERS))
    {
        if(v.model && (v.model < 400 || v.model > 611)) { AcFlag(slot, AC_CRASHERS, 1, "invalid car %d", v.model); return false; }
        if(v.paintjob < -1 || v.paintjob > 3) { AcFlag(slot, AC_CRASHERS, 1, "invalid paintjob %d", v.paintjob); return false; }
        for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i)
            if(v.mods[i] && (v.mods[i] < 1000 || v.mods[i] > 1193)) { AcFlag(slot, AC_CRASHERS, 1, "invalid tuning %d", v.mods[i]); return false; }
    }
    AcSlot& a = m_Ac[slot];
    // a part added to the SAME car (only new parts, none removed - entering another tuned car changes many)
    bool added = false, removed = false;
    if(a.modsModel == v.model)
        for(int i = 0; i < MP_VEH_MOD_SLOTS; ++i)
        {
            if(v.mods[i] && !a.mods[i]) added = true;
            else if(v.mods[i] != a.mods[i]) removed = true;
        }
    if(removed) added = false;
    if(added && AcCheck(AC_TUNING))
    {
        float best = 1e9f; float p[3]; memcpy(p, v.pos, sizeof(p));
        for(const auto& g : k_AcModShops) { float dd = AcDist3(p, g); if(dd < best) best = dd; }
        if(best > 45.f)
        {
            memcpy(a.mods, v.mods, sizeof(a.mods));
            return AcFlag(slot, AC_TUNING, 2, "tuned %.0f m from a mod garage", best) && false;
        }
    }
    a.modsModel = v.model; memcpy(a.mods, v.mods, sizeof(a.mods));
    return true;
}
// PKT_DIALOG_RESPONSE: answers to dialogs the server never showed + crasher text
bool MPServer::AcDialog(int slot, PktDialogResp& r)
{
    if(!AcIsOn()) return true;
    AcSlot& a = m_Ac[slot];
    if(AcCheck(AC_CRASHERS))
        for(char* c = r.input; c < r.input + sizeof(r.input) && *c; ++c) if((unsigned char)*c < 32 || *c == '%') *c = '#';
    if(AcCheck(AC_DIALOG))
    {
        if(!a.dlgOpen || r.dialogId != a.dlgId) { AcFlag(slot, AC_DIALOG, 3, "dialog %d was not shown", r.dialogId); return false; }
        if(r.listItem < -1 || r.listItem > 255) { AcFlag(slot, AC_DIALOG, 3, "list item %d", r.listItem); return false; }
    }
    a.dlgOpen = false;
    return true;
}
void MPServer::AcDialogShown(int slot, uint16_t id)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    m_Ac[slot].dlgOpen = true; m_Ac[slot].dlgId = id;
}
// PKT_PLAYER_INFO: Nex-AC high ping (over 500 ms, 8 readings in a row)
void MPServer::AcPing(int slot, int ping)
{
    if(IsDayzServer() && slot >= 0 && slot < MP_MAX_PLAYERS && m_Dz[slot].loaded && DzVar(DZV_PINGKICK) && MP_NowMs() - m_Ac[slot].joinAt > 60000)
    {   // 3.59: MTA DayZ ping kicker (pingkick / ping variables)
        if(ping > DzVar(DZV_PING)) { if(++m_Dz[slot].pingBad == 6) DzMsg(slot, DZC_RED, "[PING] Your ping is too high (%d ms, max %d) - you will be kicked.", ping, DzVar(DZV_PING)); if(m_Dz[slot].pingBad >= 12) { m_Dz[slot].pingBad = 0; DzLog("PINGKICK %s %d ms", m_Slots[slot].name, ping); KickLocked(slot, "Ping kicker", "Ping too high", false); return; } }
        else m_Dz[slot].pingBad = 0;
    }
    if(!AcCheck(AC_HIGH_PING)) return;
    AcSlot& a = m_Ac[slot];
    if(ping > 500 && MP_NowMs() - a.joinAt > 60000) { if(++a.pingBad >= 8) { a.pingBad = 0; AcFlag(slot, AC_HIGH_PING, 1, "%d ms", ping); } }
    else a.pingBad = 0;
}
void MPServer::AcPause(int slot, bool paused)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    AcSlot& a = m_Ac[slot];
    if(paused && m_Slots[slot].hasPos) { memcpy(a.pausePos, m_Slots[slot].pos, sizeof(a.pausePos)); a.pauseAnchor = true; }
    if(!paused) { a.pauseAnchor = false; AcGrace(slot, 3000); }
}
// PKT_PROGRESS (money): more than $1,000,000 between two saves a minute apart
bool MPServer::AcMoney(int slot, int32_t money)
{
    if(!AcCheck(AC_MONEY) || IsRpServer()) { m_Ac[slot].money = money; m_Ac[slot].hasMoney = true; return true; }
    AcSlot& a = m_Ac[slot];
    const uint64_t now = MP_NowMs();
    bool ok = true;
    if(a.hasMoney && money - a.money > 1000000 && now - a.moneyAt < 120000 && now > a.moneyGrace)
        ok = AcFlag(slot, AC_MONEY, 2, "+$%d", (int)(money - a.money)) && false;
    if(ok) { a.money = money; a.moneyAt = now; a.hasMoney = true; }
    return ok;
}
// the server moved / froze / healed this player itself (RP, Cop vs Robber): no false alarms, NOP checks
void MPServer::AcServerTeleport(int slot, const float pos[3])
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    AcSlot& a = m_Ac[slot];
    AcGrace(slot, 10000);
    memcpy(a.tpPos, pos, sizeof(a.tpPos)); a.tpAt = MP_NowMs(); a.tpPending = true;
    a.freezeAnchor = false;
}
void MPServer::AcServerFreeze(int slot, bool on)
{
    if(slot < 0 || slot >= MP_MAX_PLAYERS) return;
    m_Ac[slot].frozen = on; m_Ac[slot].frozenAt = MP_NowMs(); m_Ac[slot].freezeAnchor = false;
}
void MPServer::AcServerHeal(int slot)   { if(slot >= 0 && slot < MP_MAX_PLAYERS) { m_Ac[slot].healGrace = MP_NowMs() + 5000; m_Ac[slot].gmAt = 0; } }
void MPServer::AcServerMoney(int slot)  { if(slot >= 0 && slot < MP_MAX_PLAYERS) m_Ac[slot].moneyGrace = MP_NowMs() + 120000; }
void MPServer::AcServerJetpack(int slot) { if(slot >= 0 && slot < MP_MAX_PLAYERS) m_Ac[slot].jetOk = true; }

// once a second (Loop): sandbox
void MPServer::AcTick(uint64_t now)
{
    if(!AcIsOn() || !AcCheck(AC_SANDBOX)) return;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        Slot& s = m_Slots[i];
        if(!s.used || !s.authed || s.paused || i == MP_BOT_ID) continue;
        AcSlot& a = m_Ac[i];
        // joined, logged in, never played (no sync) for 3 minutes and not paused: an empty bot client
        if(!a.firstSyncAt && a.joinAt && now - a.joinAt > 180000 && now - s.lastSeen < 5000)
        { a.joinAt = now; AcFlag(i, AC_SANDBOX, 1, "no game for 3 minutes"); }
    }
}
