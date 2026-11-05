// 4.09: the CGRP commands (included at the end of server_cgrp.inc)
struct CgEntOp { const char* n; uint8_t kind; char op; int sub; };
static const CgEntOp kCgEntOps[] =
{   // d delete, g goto, m move out/in, i nearest id, n inside = here, o open position = here, h outside = here,
    // l lock, v VIP, w virtual world, N name, T text, u music, L level, P price, Y type, V products, c clear, O owner
    { "deletebiz", CGK_BIZ, 'd', -1 }, { "gotobiz", CGK_BIZ, 'g', -1 }, { "movebiz", CGK_BIZ, 'm', -1 }, { "bizlevel", CGK_BIZ, 'L', -1 },
    { "bizprice", CGK_BIZ, 'P', -1 }, { "biztype", CGK_BIZ, 'Y', -1 }, { "bizprods", CGK_BIZ, 'V', -1 }, { "clearbiz", CGK_BIZ, 'c', -1 }, { "setbizowner", CGK_BIZ, 'O', -1 },
    { "deletebuilding", CGK_BUILDING, 'd', -1 }, { "gotobuilding", CGK_BUILDING, 'g', -1 }, { "movebuilding", CGK_BUILDING, 'm', -1 }, { "lockbuilding", CGK_BUILDING, 'l', -1 },
    { "makebuildingvip", CGK_BUILDING, 'v', -1 }, { "buildingvw", CGK_BUILDING, 'w', -1 }, { "buildingname", CGK_BUILDING, 'N', -1 },
    { "sbm", CGK_BUILDING, 'u', -1 }, { "setbuildingmusic", CGK_BUILDING, 'u', -1 },
    { "deleteentrance", CGK_ENTRANCE, 'd', -1 }, { "gotoentrance", CGK_ENTRANCE, 'g', -1 }, { "entranceid", CGK_ENTRANCE, 'i', -1 },
    { "deletegarage", CGK_GARAGE, 'd', -1 }, { "gotogarage", CGK_GARAGE, 'g', -1 }, { "garageid", CGK_GARAGE, 'i', -1 },
    { "deletemotel", CGK_MOTEL, 'd', -1 }, { "movemotel", CGK_MOTEL, 'm', -1 }, { "gotomotel", CGK_MOTEL, 'g', -1 }, { "lockmotel", CGK_MOTEL, 'l', -1 },
    { "makemotelvip", CGK_MOTEL, 'v', -1 }, { "motelvw", CGK_MOTEL, 'w', -1 }, { "motelname", CGK_MOTEL, 'N', -1 },
    { "deletecar", CGK_CARSPOT, 'd', -1 }, { "deletepcar", CGK_CARSPOT, 'd', -1 }, { "deletefcar", CGK_CARSPOT, 'd', -1 }, { "parkpcar", CGK_CARSPOT, 'h', -1 }, { "setcarinfo", CGK_CARSPOT, 'N', -1 },
    { "deletedrivethru", CGK_DRIVETHRU, 'd', -1 }, { "deletehss", CGK_HSSIGN, 'd', -1 }, { "hssignid", CGK_HSSIGN, 'i', -1 }, { "nearmapicon", CGK_MAPICON, 'i', -1 },
    { "deletemapb", CGK_MAPBOX, 'd', -1 }, { "deleteinfoboard", CGK_INFOBOARD, 'd', -1 }, { "setinfoboardstring", CGK_INFOBOARD, 'T', -1 },
    { "deletegstation", CGK_GSTATION, 'd', -1 }, { "deleteoocshop", CGK_OOCSHOP, 'd', -1 }, { "deleteatm", CGK_ATM, 'd', -1 }, { "deletebag", CGK_MONEYBAG, 'd', -1 },
    { "hqdelete", CGK_HQ, 'd', -1 }, { "ganghqid", CGK_HQ, 'i', -1 }, { "hqedit", CGK_HQ, 'n', -1 },
    { "deletefamgate", CGK_GATE, 'd', CGG_FAMILY }, { "deletefacgate", CGK_GATE, 'd', CGG_FACTION }, { "deletegate", CGK_GATE, 'd', CGG_PUBLIC },
    { "famgateid", CGK_GATE, 'i', CGG_FAMILY }, { "famegateid", CGK_GATE, 'i', CGG_FAMILY }, { "facgateid", CGK_GATE, 'i', CGG_FACTION }, { "gateid", CGK_GATE, 'i', CGG_PUBLIC },
    { "editfamgate", CGK_GATE, 'o', CGG_FAMILY }, { "editfacgate", CGK_GATE, 'o', CGG_FACTION }, { "editgate", CGK_GATE, 'o', CGG_PUBLIC },
};
static const char* const kCgWeapons =
    "1 knuckles 2 golf 3 nightstick 4 knife 5 bat 6 shovel 7 pool 8 katana 9 chainsaw 14 flowers 15 cane 16 grenade 17 teargas 18 molotov "
    "22 9mm 23 silenced 24 deagle 25 shotgun 26 sawnoff 27 combat 28 uzi 29 mp5 30 ak47 31 m4 32 tec9 33 rifle 34 sniper 35 rpg 36 hs-rocket "
    "37 flamethrower 38 minigun 41 spray 42 extinguisher 43 camera 44 nightvision 46 parachute";

void MPServer::CgCommand(int slot, const char* cmd, char* args, const PktCgCmd& c)
{
    (void)c;
    const int mask = IsRpServer() ? CGM_M : CGM_D;
    const CgCmdDef* def = CgFindCmd(cmd, mask);
    if(!def) { CgTell(slot, kCgGrey, "Unknown command /%s. /cgrp for the list.", cmd); return; }
    const int lvl = CgLevel(slot);
    if(lvl < def->lvl) { CgTell(slot, kCgRed, "/%s is for a %s (level %d) and up.", cmd, kCgLevelName[def->lvl], def->lvl); return; }
    Slot& me = m_Slots[slot]; CgSlot& s = m_CgS[slot]; CgPlayer& p = CgP(slot);
    const uint64_t now = MP_NowMs();
    char raw[128]; MP_CopyStr(raw, CgRest(args), sizeof(raw));
    char raw2[128]; MP_CopyStr(raw2, raw, sizeof(raw2));   // untouched copy for the helpers that read words again
    char* a[6] = {}; { char* q = args; for(int i = 0; i < 6; ++i) a[i] = CgTok(q); }
    auto restFrom = [&](int n) -> const char* {   // the text after n words
        const char* q = raw;
        for(int i = 0; i < n; ++i) { while(*q == ' ') ++q; while(*q && *q != ' ') ++q; }
        while(*q == ' ') ++q; return q;
    };
    auto IS = [&](const char* n) { return !strcmp(cmd, n); };
    auto usage = [&]() { CgTell(slot, kCgGrey, "USAGE: /%s %s", def->name, def->usage); };
    auto T = [&](int i) -> int {
        if(!a[i]) { usage(); return -1; }
        int t = CgFindPlayer(a[i]); if(t < 0) CgTell(slot, kCgGrey, "No such player: %s.", a[i]); return t;
    };
    auto canHit = [&](int t) { if(t == slot || me.admin || lvl > CgLevel(t)) return true; CgTell(slot, kCgRed, "%s is staff at your level or higher.", m_Slots[t].name); return false; };
    char buf[200], lg[240];
    auto adm = [&](const char* text) { snprintf(lg, sizeof(lg), "AdmCmd: %s %s", me.name, text); CgStaffMsg(kCgOrange, lg); Log("CGRP %s", lg); };
    auto near = [&](int kind, float r) -> CgEnt* { int i = CgNear(slot, kind, r); return i >= 0 ? &m_Cg[i] : nullptr; };
    auto here = [&](float* o) { o[0] = s.pos[0]; o[1] = s.pos[1]; o[2] = s.pos[2]; };
    auto tele = [&](int t, float x, float y, float z, float h, int area) { float pp[3] = { x, y, z }; CgAct(t, CGA_TELE, area, 0, 0, pp, h); };
    auto within = [&](int t, float r) { return CgDist2(s.pos, m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2]) <= r * r; };
    auto famLeader = [&]() { return p.family && p.famRank >= 5; };
    auto facLeader = [&]() { return p.faction && p.facRank >= 5; };

    // ---- generic entity edits ----
    for(const auto& op : kCgEntOps)
    {
        if(!IS(op.n)) continue;
        if(op.op == 'i')
        {
            int best = -1; float bd = 15.0f * 15.0f;
            for(size_t i = 0; i < m_Cg.size(); ++i) { const CgRow& r = m_Cg[i].r; if(r.kind != op.kind || (op.sub >= 0 && r.sub != op.sub)) continue; float d = CgDist2(s.pos, r.x, r.y, r.z); if(d < bd) { bd = d; best = (int)i; } }
            if(best < 0) CgTell(slot, kCgGrey, "No %s near you.", kCgKindName[op.kind]);
            else CgTell(slot, kCgYellow, "%s id %d (%s) %.1f m away.", kCgKindName[op.kind], m_Cg[best].r.id, m_Cg[best].r.name, sqrtf(bd));
            return;
        }
        if(!a[0]) { usage(); return; }
        CgEnt* e = CgGet(CgAtoi(a[0]), op.kind);
        if(!e || (op.sub >= 0 && e->r.sub != op.sub)) { CgTell(slot, kCgGrey, "No %s with id %s.", kCgKindName[op.kind], a[0]); return; }
        CgRow& r = e->r;
        switch(op.op)
        {
        case 'd': CgTell(slot, kCgYellow, "Deleted %s %d (%s).", kCgKindName[r.kind], r.id, r.name); CgDel(r.id); return;
        case 'g': if(a[1] && !strcmp(a[1], "in") && CgHasInside(r)) tele(slot, r.ix, r.iy, r.iz, r.ia, r.iarea); else tele(slot, r.x, r.y, r.z + 0.5f, r.a, r.area); return;
        case 'm':
        {
            const bool in = a[1] && !strcmp(a[1], "in");
            if(in) { r.ix = s.pos[0]; r.iy = s.pos[1]; r.iz = s.pos[2]; r.ia = s.heading; r.iarea = s.area; }
            else { r.x = s.pos[0]; r.y = s.pos[1]; r.z = s.pos[2]; r.a = s.heading; r.area = s.area; }
            CgChanged(*e); CgTell(slot, kCgGreen, "%s %d: %s is where you stand.", kCgKindName[r.kind], r.id, in ? "the inside" : "the outside"); return;
        }
        case 'n': r.ix = s.pos[0]; r.iy = s.pos[1]; r.iz = s.pos[2]; r.ia = s.heading; r.iarea = s.area; CgChanged(*e); CgTell(slot, kCgGreen, "%s %d: the inside is here.", kCgKindName[r.kind], r.id); return;
        case 'o': r.ix = s.pos[0]; r.iy = s.pos[1]; r.iz = s.pos[2]; r.ia = s.heading; CgChanged(*e); CgTell(slot, kCgGreen, "Gate %d: the open position is here.", r.id); return;
        case 'h': r.x = s.pos[0]; r.y = s.pos[1]; r.z = s.pos[2]; r.a = s.heading; r.area = s.area; CgChanged(*e); CgTell(slot, kCgGreen, "Vehicle spot %d moved here.", r.id); return;
        case 'l': r.flags ^= CGF_LOCKED; CgChanged(*e); CgTell(slot, kCgGreen, "%s %d %s.", kCgKindName[r.kind], r.id, (r.flags & CGF_LOCKED) ? "locked" : "unlocked"); return;
        case 'v': r.flags ^= CGF_VIP; CgChanged(*e); CgTell(slot, kCgGreen, "%s %d: VIP lounge %s.", kCgKindName[r.kind], r.id, (r.flags & CGF_VIP) ? "on" : "off"); return;
        case 'w': if(!a[1]) { usage(); return; } r.vw = (uint16_t)CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "%s %d: virtual world %d.", kCgKindName[r.kind], r.id, r.vw); return;
        case 'N': if(!*restFrom(1)) { usage(); return; } MP_CopyStr(r.name, restFrom(1), sizeof(r.name)); CgChanged(*e); CgTell(slot, kCgGreen, "%s %d is called %s.", kCgKindName[r.kind], r.id, r.name); return;
        case 'T': if(!*restFrom(1)) { usage(); return; } MP_CopyStr(r.text, restFrom(1), sizeof(r.text)); CgChanged(*e); CgTell(slot, kCgGreen, "Board %d text set.", r.id); return;
        case 'u': if(!a[1]) { usage(); return; } r.val = CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "Building %d music: %s.", r.id, r.val >= 0 ? a[1] : "off"); return;
        case 'L': if(!a[1]) { usage(); return; } r.level = (uint16_t)CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "Business %d needs level %d.", r.id, r.level); return;
        case 'P': if(!a[1]) { usage(); return; } r.price = CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "Business %d costs $%d.", r.id, r.price); return;
        case 'Y':
        {
            int v = a[1] ? CgAtoi(a[1]) : -1;
            if(v < 0 || v >= CGB_COUNT) { CgTell(slot, kCgGrey, "Types: 0 24/7, 1 Ammunation, 2 Clothes, 3 Restaurant, 4 Bar, 5 Gym, 6 Electronics, 7 Casino."); return; }
            r.sub = (uint8_t)v; CgChanged(*e); CgTell(slot, kCgGreen, "Business %d: %s.", r.id, kCgBizType[v]); return;
        }
        case 'V': if(!a[1]) { usage(); return; } r.val = CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "Business %d: %d products.", r.id, r.val); return;
        case 'c': e->ownerKey.clear(); r.owner[0] = 0; r.flags |= CGF_SALE; e->till = 0; CgChanged(*e); CgTell(slot, kCgGreen, "Business %d has no owner and is for sale.", r.id); return;
        case 'O':
        {
            int t = T(1); if(t < 0) return;
            char kb[64]; e->ownerKey = CgKey(t, kb, sizeof(kb)); MP_CopyStr(r.owner, m_Slots[t].name, sizeof(r.owner)); r.flags &= ~CGF_SALE; CgChanged(*e);
            CgTell(slot, kCgGreen, "%s owns business %d now.", m_Slots[t].name, r.id); CgTell(t, kCgGreen, "An admin gave you the business %s.", r.name); return;
        }
        }
        return;
    }

    // ================= players =================
    if(IS("cgrp") || IS("cgadmin") || IS("eventhelp"))
    {
        const bool pl = IS("cgrp");
        std::string out; int shown = 0;
        for(int L = pl ? 0 : 1; L <= (pl ? 0 : lvl); ++L)
        {
            std::string line;
            for(const auto& d : kCgCmds)
                if(d.lvl == L && (d.mask & mask) && strcmp(d.name, "usepickup") && strcmp(d.name, "takebag") && strncmp(d.name, "accept", 6)) { line += "/"; line += d.name; line += " "; ++shown; }
            if(line.empty()) continue;
            out += "{FFCC00}"; out += kCgLevelName[L]; out += "{FFFFFF}\n"; out += line; out += "\n";
        }
        if(pl) out += "/accept car  /accept family" + std::string(IsRpServer() ? "" : "  /accept faction") + "\n";
        if(out.size() >= MP_DIALOG_INFO_LEN) out.resize(MP_DIALOG_INFO_LEN - 1);
        CgDlg(slot, CG_DLG_BASE + 0x80, DIALOG_STYLE_MSGBOX, pl ? "CGRP commands" : "CGRP admin commands", out.c_str(), "OK", "");
        if(!pl) CgTell(slot, kCgGrey, "You are a %s (level %d). %d commands.", kCgLevelName[lvl], lvl, shown);
        return;
    }
    if(IS("enter")) { CgEnter(slot); return; }
    if(IS("exit")) { CgExit(slot); return; }
    if(IS("buy"))
    {
        CgEnt* e = CgGet(s.inside, CGK_BIZ); if(!e) { CgTell(slot, kCgGrey, "You are not in a business."); return; }
        std::string list;
        for(const auto& it : kCgBizItems) if(it.type == e->r.sub) { snprintf(buf, sizeof(buf), "%s\t$%d\n", it.name, it.price); list += buf; }
        if(list.empty()) { CgTell(slot, kCgGrey, "This business sells nothing."); return; }
        snprintf(buf, sizeof(buf), "%s (%s)", e->r.name, kCgBizType[e->r.sub < CGB_COUNT ? e->r.sub : 0]);
        CgDlg(slot, CGD_BUY, DIALOG_STYLE_TABLIST, buf, list.c_str(), "Buy", "Close");
        return;
    }
    if(IS("buybiz") || IS("buygarage"))
    {
        const int kind = IS("buybiz") ? CGK_BIZ : CGK_GARAGE;
        CgEnt* e = near(kind, 4.0f); if(!e) { CgTell(slot, kCgGrey, "Stand at the %s door.", kCgKindName[kind]); return; }
        if(!e->ownerKey.empty() && !(e->r.flags & CGF_SALE)) { CgTell(slot, kCgRed, "%s is not for sale.", e->r.name); return; }
        if(CgMine(slot, *e)) { CgTell(slot, kCgGrey, "It is yours already."); return; }
        int owned = 0; for(auto& x : m_Cg) if(x.r.kind == kind && CgMine(slot, x)) ++owned;
        if(owned >= (CgIsVip(slot) ? 2 : 1)) { CgTell(slot, kCgRed, "You already have %d (the most%s).", owned, CgIsVip(slot) ? "" : " - VIPs can have 2"); return; }
        if(CgPlayLevel(slot) < e->r.level) { CgTell(slot, kCgRed, "You need level %d.", e->r.level); return; }
        if(!CgPay(slot, e->r.price, kCgKindName[kind])) { CgTell(slot, kCgRed, "You need $%d.", e->r.price); return; }
        CgPayKey(e->ownerKey, e->r.price);
        char kb[64]; e->ownerKey = CgKey(slot, kb, sizeof(kb)); MP_CopyStr(e->r.owner, me.name, sizeof(e->r.owner)); e->r.flags &= ~CGF_SALE;
        CgChanged(*e);
        CgTell(slot, kCgGreen, "Congratulations, you bought %s for $%d.", e->r.name[0] ? e->r.name : kCgKindName[kind], e->r.price);
        return;
    }
    if(IS("sellbiz") || IS("sellgarage"))
    {
        const int kind = IS("sellbiz") ? CGK_BIZ : CGK_GARAGE;
        CgEnt* mine = nullptr; for(auto& x : m_Cg) if(x.r.kind == kind && CgMine(slot, x)) { if(!mine || (a[0] && x.r.id == CgAtoi(a[0]))) mine = &x; }
        if(!mine) { CgTell(slot, kCgGrey, "You have no %s.", kCgKindName[kind]); return; }
        const int back = mine->r.price / 2 + (kind == CGK_BIZ ? mine->till : 0);
        CgGive(slot, back, "Sold"); mine->ownerKey.clear(); mine->r.owner[0] = 0; mine->r.flags |= CGF_SALE; mine->till = 0; CgChanged(*mine);
        CgTell(slot, kCgGreen, "You sold %s to the state for $%d.", mine->r.name, back);
        return;
    }
    if(IS("bizinfo") || IS("bizname") || IS("bizfee") || IS("bizwithdraw") || IS("bizdeposit") || IS("buyprods") || IS("lockbiz"))
    {
        CgEnt* e = nullptr; if(CgEnt* in = CgGet(s.inside, CGK_BIZ)) if(CgMine(slot, *in)) e = in;
        if(!e) { CgEnt* n = near(CGK_BIZ, 6.0f); if(n && CgMine(slot, *n)) e = n; }
        if(!e) for(auto& x : m_Cg) if(x.r.kind == CGK_BIZ && CgMine(slot, x)) { e = &x; break; }
        if(!e) { CgTell(slot, kCgGrey, "You have no business."); return; }
        CgRow& r = e->r;
        if(IS("bizinfo")) { CgTell(slot, kCgWhite, "[%s] id %d, %s, till $%d, products %d, entrance fee $%d, %s, level %d, worth $%d.", r.name, r.id, kCgBizType[r.sub < CGB_COUNT ? r.sub : 0], e->till, r.val, e->fee, (r.flags & CGF_LOCKED) ? "locked" : "open", r.level, r.price); return; }
        if(IS("bizname")) { if(!*raw) { usage(); return; } MP_CopyStr(r.name, raw, sizeof(r.name)); CgChanged(*e); CgTell(slot, kCgGreen, "Your business is called %s.", r.name); return; }
        if(IS("bizfee")) { int v = a[0] ? CgAtoi(a[0]) : -1; if(v < 0 || v > 5000) { CgTell(slot, kCgGrey, "USAGE: /bizfee [0-5000]"); return; } e->fee = v; m_bCgDirty = true; CgTell(slot, kCgGreen, "Entrance fee: $%d.", v); return; }
        if(IS("lockbiz")) { r.flags ^= CGF_LOCKED; CgChanged(*e); CgTell(slot, kCgGreen, "%s %s.", r.name, (r.flags & CGF_LOCKED) ? "locked" : "unlocked"); return; }
        int v = a[0] ? CgAtoi(a[0]) : 0; if(v <= 0) { usage(); return; }
        if(IS("bizwithdraw")) { if(v > e->till) { CgTell(slot, kCgRed, "The till has $%d.", e->till); return; } e->till -= v; CgGive(slot, v, "Business till"); }
        else if(IS("bizdeposit")) { if(!CgPay(slot, v, "Business till")) { CgTell(slot, kCgRed, "You don't have $%d.", v); return; } e->till += v; }
        else { if(v > 500) v = 500; if(!CgPay(slot, v * 5, "Products")) { CgTell(slot, kCgRed, "%d products cost $%d.", v, v * 5); return; } r.val += v; CgChanged(*e); }
        m_bCgDirty = true; CgTell(slot, kCgGreen, "%s: till $%d, products %d.", r.name, e->till, r.val);
        return;
    }
    if(IS("lockgarage"))
    {
        CgEnt* e = nullptr; for(auto& x : m_Cg) if(x.r.kind == CGK_GARAGE && CgMine(slot, x)) { e = &x; break; }
        if(!e) { CgTell(slot, kCgGrey, "You have no garage."); return; }
        e->r.flags ^= CGF_LOCKED; CgChanged(*e); CgTell(slot, kCgGreen, "Garage %s.", (e->r.flags & CGF_LOCKED) ? "locked" : "unlocked"); return;
    }
    if(IS("rentroom"))
    {
        CgEnt* e = near(CGK_MOTEL, 5.0f); if(!e) { CgTell(slot, kCgGrey, "Stand at a motel door."); return; }
        if(p.motel == e->r.id) { CgTell(slot, kCgGrey, "You rent a room here already."); return; }
        if((e->r.flags & CGF_VIP) && !CgIsVip(slot)) { CgTell(slot, kCgRed, "VIP motel."); return; }
        const int price = e->r.price > 0 ? e->r.price : 50;
        if(!CgPay(slot, price, "Motel")) { CgTell(slot, kCgRed, "The room costs $%d.", price); return; }
        p.motel = e->r.id; m_bCgDirty = true; CgTell(slot, kCgGreen, "You rent a room at %s ($%d every payday). /enter, /unrentroom.", e->r.name, price); return;
    }
    if(IS("unrentroom")) { if(!p.motel) { CgTell(slot, kCgGrey, "You rent no room."); return; } p.motel = 0; m_bCgDirty = true; CgTell(slot, kCgGreen, "You don't rent the room any more."); return; }

    // ---- families ----
    if(IS("families"))
    {
        if(m_CgFam.empty()) { CgTell(slot, kCgGrey, "There are no families yet."); return; }
        for(const auto& f : m_CgFam) { int on = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && CgP(i).family == f.id) ++on; CgTell(slot, kCgFam, "Family %d: %s (leader %s), %d online.", f.id, f.name.c_str(), f.leader.c_str(), on); }
        return;
    }
    if(IS("family"))
    {
        CgFamily* f = CgFam(p.family); if(!f) { CgTell(slot, kCgGrey, "You are in no family."); return; }
        CgTell(slot, kCgFam, "[%s] leader %s, safe $%d. Online:", f->name.c_str(), f->leader.c_str(), f->safe);
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && CgP(i).family == f->id) CgTell(slot, kCgFam, "  (%d) %s rank %d", i, m_Slots[i].name, CgP(i).famRank);
        return;
    }
    if(IS("fam")) { if(!p.family) { CgTell(slot, kCgGrey, "You are in no family."); return; } if(!*raw) { usage(); return; } snprintf(buf, sizeof(buf), "** (%d) %s: %s **", p.famRank, me.name, raw); CgFamMsg(p.family, kCgFam, buf); return; }
    if(IS("finvite"))
    {
        if(!famLeader()) { CgTell(slot, kCgRed, "Family rank 5+ only."); return; }
        int t = T(0); if(t < 0) return; if(CgP(t).family) { CgTell(slot, kCgGrey, "%s is in a family already.", m_Slots[t].name); return; }
        m_CgS[t].famInvite = p.family; CgFamily* f = CgFam(p.family);
        CgTell(t, kCgFam, "%s invited you to the family %s. Type /accept family.", me.name, f ? f->name.c_str() : "?"); CgTell(slot, kCgFam, "You invited %s.", m_Slots[t].name); return;
    }
    if(IS("acceptfamily"))
    {
        CgFamily* f = CgFam(s.famInvite); if(!f) { CgTell(slot, kCgGrey, "Nobody invited you to a family."); return; }
        p.family = f->id; p.famRank = 1; s.famInvite = 0; m_bCgDirty = true;
        snprintf(buf, sizeof(buf), "%s joined the family.", me.name); CgFamMsg(f->id, kCgFam, buf); CgSendMe(slot); return;
    }
    if(IS("fkick") || IS("frank"))
    {
        if(!famLeader()) { CgTell(slot, kCgRed, "Family rank 5+ only."); return; }
        int t = T(0); if(t < 0) return; CgPlayer& tp = CgP(t);
        if(tp.family != p.family || t == slot) { CgTell(slot, kCgGrey, "%s is not in your family.", m_Slots[t].name); return; }
        if(tp.famRank >= p.famRank && p.famRank < 6) { CgTell(slot, kCgRed, "Same rank or higher."); return; }
        const uint16_t fam = p.family;
        if(IS("fkick")) { tp.family = 0; tp.famRank = 0; CgTell(t, kCgFam, "%s kicked you from the family.", me.name); snprintf(buf, sizeof(buf), "%s kicked %s from the family.", me.name, m_Slots[t].name); CgFamMsg(fam, kCgFam, buf); }
        else
        {
            int r = a[1] ? CgAtoi(a[1]) : 0;
            if(r < 1 || r > 6 || (r >= p.famRank && p.famRank < 6)) { CgTell(slot, kCgGrey, "Rank 1 - %d.", p.famRank < 6 ? p.famRank - 1 : 6); return; }
            tp.famRank = (uint8_t)r; snprintf(buf, sizeof(buf), "%s gave %s family rank %d.", me.name, m_Slots[t].name, r); CgFamMsg(fam, kCgFam, buf);
        }
        m_bCgDirty = true; CgSendMe(t); return;
    }
    if(IS("fquit"))
    {
        if(!p.family) { CgTell(slot, kCgGrey, "You are in no family."); return; }
        snprintf(buf, sizeof(buf), "%s left the family.", me.name); CgFamMsg(p.family, kCgFam, buf);
        p.family = 0; p.famRank = 0; m_bCgDirty = true; CgSendMe(slot); return;
    }
    if(IS("fdeposit") || IS("fwithdraw"))
    {
        CgFamily* f = CgFam(p.family); if(!f) { CgTell(slot, kCgGrey, "You are in no family."); return; }
        int hq = -1; for(size_t i = 0; i < m_Cg.size(); ++i) { const CgRow& r = m_Cg[i].r; if(r.kind == CGK_HQ && r.sub == CGG_FAMILY && r.group == f->id && (CgDist2(s.pos, r.ix, r.iy, r.iz) < 900.0f || CgDist2(s.pos, r.x, r.y, r.z) < 100.0f)) hq = (int)i; }
        if(hq < 0) { CgTell(slot, kCgGrey, "Go to your family HQ."); return; }
        int v = a[0] ? CgAtoi(a[0]) : 0; if(v <= 0) { usage(); return; }
        if(IS("fwithdraw")) { if(!famLeader()) { CgTell(slot, kCgRed, "Family rank 5+ only."); return; } if(f->safe < v) { CgTell(slot, kCgRed, "The safe has $%d.", f->safe); return; } f->safe -= v; CgGive(slot, v, "Family safe"); }
        else { if(!CgPay(slot, v, "Family safe")) { CgTell(slot, kCgRed, "You don't have $%d.", v); return; } f->safe += v; }
        m_bCgDirty = true; snprintf(buf, sizeof(buf), "%s %s $%d (safe $%d).", me.name, IS("fwithdraw") ? "took" : "put in", v, f->safe); CgFamMsg(f->id, kCgFam, buf); return;
    }
    if(IS("fgate")) { CgGate(slot, CGG_FAMILY, false); return; }
    if(IS("facgate")) { CgGate(slot, CGG_FACTION, false); return; }
    if(IS("gate")) { CgGate(slot, -1, false); return; }
    if(IS("afgate")) { CgGate(slot, -1, true); return; }
    if(IS("agate")) { CgGate(slot, CGG_PUBLIC, true); return; }
    if(IS("lockhq"))
    {
        CgEnt* e = nullptr; float bd = 900.0f;
        for(auto& x : m_Cg) if(x.r.kind == CGK_HQ && CgMember(slot, x)) { float d = std::min(CgDist2(s.pos, x.r.x, x.r.y, x.r.z), CgDist2(s.pos, x.r.ix, x.r.iy, x.r.iz)); if(d < bd) { bd = d; e = &x; } }
        if(!e) { CgTell(slot, kCgGrey, "You are not at your HQ."); return; }
        const bool boss = e->r.sub == CGG_FAMILY ? famLeader() : (IsRpServer() || facLeader());
        if(!boss && lvl < 6) { CgTell(slot, kCgRed, "Rank 5+ only."); return; }
        e->r.flags ^= CGF_LOCKED; CgChanged(*e); CgTell(slot, kCgGreen, "HQ %s.", (e->r.flags & CGF_LOCKED) ? "locked" : "unlocked"); return;
    }
    if(IS("famcar") || IS("faccar"))
    {
        const int g = IS("famcar") ? CGG_FAMILY : CGG_FACTION; int n = 0;
        for(const auto& x : m_Cg) if(x.r.kind == CGK_CARSPOT && x.r.sub == g && CgMember(slot, x)) { CgTell(slot, kCgFam, "Spot %d: model %d at %.0f %.0f (/getcar there).", x.r.id, x.r.model, x.r.x, x.r.y); ++n; }
        if(!n) CgTell(slot, kCgGrey, "Your %s has no vehicles.", g == CGG_FAMILY ? "family" : "faction");
        return;
    }
    if(IS("getcar"))
    {
        CgEnt* e = near(CGK_CARSPOT, 6.0f); if(!e) { CgTell(slot, kCgGrey, "Stand at a vehicle spot."); return; }
        if(e->r.sub != CGG_PUBLIC && !CgMember(slot, *e) && lvl < 4) { CgTell(slot, kCgRed, "This vehicle is not yours."); return; }
        if(e->r.sub == CGG_PUBLIC && e->r.price > 0 && !CgPay(slot, e->r.price, "Rental")) { CgTell(slot, kCgRed, "The rental costs $%d.", e->r.price); return; }
        float pp[3] = { e->r.x, e->r.y, e->r.z };
        CgAct(slot, CGA_SPAWNCAR, e->r.val & 0xFF, (e->r.val >> 8) & 0xFF, e->r.model, pp, e->r.a);
        return;
    }
    if(IS("park"))
    {
        if(!s.inCar) { CgTell(slot, kCgGrey, "Sit in the car."); return; }
        for(auto& x : m_Cg)
        {
            if(x.r.kind != CGK_CARSPOT || x.r.model != s.carModel || x.r.sub == CGG_PUBLIC || !CgMember(slot, x)) continue;
            const bool may = x.r.sub == CGG_PLAYER || (x.r.sub == CGG_FAMILY ? famLeader() : (facLeader() || IsRpServer()));
            if(!may) continue;
            here(&x.r.x); x.r.a = s.heading; x.r.area = s.area; CgChanged(x); CgTell(slot, kCgGreen, "Vehicle spot %d parked here.", x.r.id); return;
        }
        CgTell(slot, kCgGrey, "This car has no spot you can move (owner / rank 5+).");
        return;
    }
    if(IS("points"))
    {
        int n = 0;
        for(const auto& x : m_Cg) if(x.r.kind == CGK_POINT) { CgTell(slot, kCgYellow, "Point %d %s: %s, $%d a payday, %d min to capture.", x.r.id, x.r.name, x.r.owner[0] ? x.r.owner : "nobody", x.r.price > 0 ? x.r.price : 5000, x.r.val > 0 ? x.r.val : 10); ++n; }
        if(!n) CgTell(slot, kCgGrey, "There are no points yet.");
        return;
    }
    if(IS("capture"))
    {
        if(!p.family) { CgTell(slot, kCgGrey, "Families capture points - you are in no family."); return; }
        CgEnt* e = near(CGK_POINT, 10.0f); if(!e) { CgTell(slot, kCgGrey, "Stand at a point."); return; }
        if(e->r.group == p.family) { CgTell(slot, kCgGrey, "Your family owns it."); return; }
        if(e->capSlot >= 0) { CgTell(slot, kCgGrey, "%s is capturing it.", m_Slots[e->capSlot].name); return; }
        e->capSlot = slot; e->capStart = now;
        CgFamily* f = CgFam(p.family);
        CgTell(-1, kCgYellow, "[POINT] %s (%s) is capturing %s - stay %d minutes.", me.name, f ? f->name.c_str() : "?", e->r.name, e->r.val > 0 ? e->r.val : 10);
        return;
    }
    if(IS("goldbars")) { CgTell(slot, kCgYellow, "You have %d gold bars (1 every 3 paydays). Spend them at an OOC shop (/oocshop).", p.gold); return; }
    if(IS("oocshop"))
    {
        if(!near(CGK_OOCSHOP, 5.0f) && lvl < 1) { CgTell(slot, kCgGrey, "Stand at an OOC shop."); return; }
        std::string list; for(const auto& it : kCgOoc) { snprintf(buf, sizeof(buf), "%s\t%d gold\n", it.name, it.gold); list += buf; }
        snprintf(buf, sizeof(buf), "OOC Shop - you have %d gold bars", p.gold);
        CgDlg(slot, CGD_OOC, DIALOG_STYLE_TABLIST, buf, list.c_str(), "Buy", "Close"); return;
    }
    if(IS("order"))
    {
        if(!s.inCar || !near(CGK_DRIVETHRU, 12.0f)) { CgTell(slot, kCgGrey, "Drive up to a drive-thru."); return; }
        std::string list; for(const auto& f : kCgFood) { snprintf(buf, sizeof(buf), "%s\t$%d\n", f.name, f.price); list += buf; }
        CgDlg(slot, CGD_ORDER, DIALOG_STYLE_TABLIST, "Drive-Thru", list.c_str(), "Order", "Close"); return;
    }
    if(IS("getgift"))
    {
        CgEnt* e = near(CGK_GIFTBOX, 4.0f); if(!e) { CgTell(slot, kCgGrey, "Stand at a gift box."); return; }
        char kb[64]; std::string k = CgKey(slot, kb, sizeof(kb));
        auto it = e->took.find(k); if(it != e->took.end() && now - it->second < 86400000ULL) { CgTell(slot, kCgGrey, "One gift a day from each box."); return; }
        e->took[k] = now;
        switch(rand() % 5)
        {
        case 0: { int v = 1000 + rand() % 9000; CgGive(slot, v, "Gift"); CgTell(slot, kCgGreen, "[GIFT] $%d.", v); break; }
        case 1: ++p.gold; m_bCgDirty = true; CgTell(slot, kCgGreen, "[GIFT] A gold bar."); break;
        case 2: me.cookies = (uint16_t)(me.cookies + 3); CgTell(slot, kCgGreen, "[GIFT] 3 cookies."); break;
        case 3: CgAct(slot, CGA_GUN, 24, 0, 50); CgTell(slot, kCgGreen, "[GIFT] A Desert Eagle."); break;
        default: CgAct(slot, CGA_HEALTH, 0, 0, 100); CgAct(slot, CGA_ARMOUR, 0, 0, 100); CgTell(slot, kCgGreen, "[GIFT] Full health and armour."); break;
        }
        CgSendMe(slot); return;
    }
    if(IS("buymap"))
    {
        if(!near(CGK_MAPBOX, 4.0f)) { CgTell(slot, kCgGrey, "Stand at a map box."); return; }
        if(p.hasMap) { CgTell(slot, kCgGrey, "You have a map. /viewmap"); return; }
        if(!CgPay(slot, 100, "Map")) { CgTell(slot, kCgRed, "A map costs $100."); return; }
        p.hasMap = 1; m_bCgDirty = true; CgTell(slot, kCgGreen, "You bought a map. /viewmap"); CgSendMe(slot); return;
    }
    if(IS("viewmap"))
    {
        if(!p.hasMap) { CgTell(slot, kCgGrey, "Buy a map at a map box (/buymap)."); return; }
        std::string list; int n = 0;
        for(const auto& e : m_Cg)
            if(e.r.kind == CGK_BIZ || e.r.kind == CGK_GARAGE || e.r.kind == CGK_MOTEL || e.r.kind == CGK_GSTATION || e.r.kind == CGK_ATM || e.r.kind == CGK_DRIVETHRU || e.r.kind == CGK_OOCSHOP)
            { snprintf(buf, sizeof(buf), "%s\t%s\n", kCgKindName[e.r.kind], e.r.name[0] ? e.r.name : "-"); if(list.size() + strlen(buf) < MP_DIALOG_INFO_LEN - 4) list += buf; ++n; }
        if(!n) { CgTell(slot, kCgGrey, "Nothing on the map yet."); return; }
        CgDlg(slot, CGD_VIEWMAP, DIALOG_STYLE_TABLIST, "Map - pick a place (GPS)", list.c_str(), "GPS", "Close"); return;
    }
    if(IS("sellcar")) { CgCarSale(slot, raw2); return; }
    if(IS("cancelcar")) { CgSaleClear(slot); CgTell(slot, kCgGrey, "Car offer cancelled."); return; }
    if(IS("acceptcar")) { CgAcceptCar(slot); return; }
    if(IS("atm"))
    {
        if(!near(CGK_ATM, 4.0f)) { CgTell(slot, kCgGrey, "Stand at an ATM."); return; }
        snprintf(buf, sizeof(buf), "Balance ($%d)\nWithdraw\nDeposit", p.bank);
        CgDlg(slot, CGD_ATM, DIALOG_STYLE_LIST, "ATM", buf, "OK", "Close"); return;
    }
    if(IS("refuel"))
    {
        if(!near(CGK_GSTATION, 15.0f)) { CgTell(slot, kCgGrey, "Drive to a gas station."); return; }
        if(!s.inCar) { CgTell(slot, kCgGrey, "Sit in the car."); return; }
        if(!CgPay(slot, 50, "Fuel")) { CgTell(slot, kCgRed, "Fuel costs $50."); return; }
        CgAct(slot, CGA_FUEL, 1, 0, 100); CgTell(slot, kCgGreen, "Refuelled for $50."); return;
    }
    if(IS("takebag"))
    {
        CgEnt* e = near(CGK_MONEYBAG, 2.5f); if(!e) return;
        const int v = e->r.val > 0 ? e->r.val : 1000; const uint16_t id = e->r.id;
        CgGive(slot, v, "Money bag"); CgTell(-1, kCgYellow, "[MONEY BAG] %s found a money bag with $%d.", me.name, v); CgDel(id); return;
    }
    if(IS("usepickup"))
    {
        CgEnt* e = near(CGK_PICKUP, 2.5f); if(!e) return;
        char kb[64]; std::string k = CgKey(slot, kb, sizeof(kb));
        auto it = e->took.find(k); if(it != e->took.end() && now - it->second < 30000) return;
        e->took[k] = now;
        switch(e->r.sub)
        {
        case CGP_INFO: CgTell(slot, kCgWhite, "%s", e->r.text[0] ? e->r.text : e->r.name); break;
        case CGP_HEALTH: CgAct(slot, CGA_HEALTH, 0, 0, 100); break;
        case CGP_ARMOUR: CgAct(slot, CGA_ARMOUR, 0, 0, 100); break;
        case CGP_WEAPON: CgAct(slot, CGA_GUN, e->r.val > 0 ? e->r.val : 24, 0, 50); break;
        case CGP_CASH: CgGive(slot, e->r.val > 0 ? e->r.val : 100, "Pickup"); break;
        }
        return;
    }

    // ---- factions (default server; Mega Vil has its own) ----
    if(IS("factions"))
    {
        if(m_CgFac.empty()) { CgTell(slot, kCgGrey, "There are no factions yet (/editfaction new [name])."); return; }
        for(const auto& f : m_CgFac) { int on = 0; for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used && CgP(i).faction == f.id) ++on; CgTell(slot, kCgBlue, "Faction %d: %s (leader %s), %d online.", f.id, f.name.c_str(), f.leader.c_str(), on); }
        return;
    }
    if(IS("invite"))
    {
        if(!facLeader()) { CgTell(slot, kCgRed, "Faction rank 5+ only."); return; }
        int t = T(0); if(t < 0) return; if(CgP(t).faction) { CgTell(slot, kCgGrey, "%s is in a faction already.", m_Slots[t].name); return; }
        m_CgS[t].facInvite = p.faction; CgFaction* f = CgFac(p.faction);
        CgTell(t, kCgBlue, "%s invited you to %s. Type /accept faction.", me.name, f ? f->name.c_str() : "?"); CgTell(slot, kCgBlue, "You invited %s.", m_Slots[t].name); return;
    }
    if(IS("acceptfaction"))
    {
        CgFaction* f = CgFac(s.facInvite); if(!f) { CgTell(slot, kCgGrey, "Nobody invited you to a faction."); return; }
        p.faction = f->id; p.facRank = 1; s.facInvite = 0; m_bCgDirty = true;
        snprintf(buf, sizeof(buf), "%s joined %s.", me.name, f->name.c_str()); CgFacMsg(f->id, kCgBlue, buf); CgSendMe(slot); return;
    }
    if(IS("uninvite") || IS("giverank"))
    {
        if(!facLeader()) { CgTell(slot, kCgRed, "Faction rank 5+ only."); return; }
        int t = T(0); if(t < 0) return; CgPlayer& tp = CgP(t);
        if(tp.faction != p.faction || t == slot) { CgTell(slot, kCgGrey, "%s is not in your faction.", m_Slots[t].name); return; }
        if(IS("uninvite")) { tp.faction = 0; tp.facRank = 0; CgTell(t, kCgBlue, "%s removed you from the faction.", me.name); }
        else
        {
            int r = a[1] ? CgAtoi(a[1]) : 0;
            if(r < 1 || r > 6 || (r >= p.facRank && p.facRank < 6)) { CgTell(slot, kCgGrey, "Rank 1 - %d.", p.facRank < 6 ? p.facRank - 1 : 6); return; }
            tp.facRank = (uint8_t)r; CgTell(t, kCgBlue, "%s gave you rank %d.", me.name, r);
        }
        m_bCgDirty = true; CgSendMe(t); CgTell(slot, kCgBlue, "Done."); return;
    }
    if(IS("quitfaction")) { if(!p.faction) { CgTell(slot, kCgGrey, "You are in no faction."); return; } p.faction = 0; p.facRank = 0; m_bCgDirty = true; CgTell(slot, kCgBlue, "You left the faction."); CgSendMe(slot); return; }
    if(IS("r") || IS("dept"))
    {
        if(!p.faction) { CgTell(slot, kCgGrey, "You are in no faction."); return; }
        if(!*raw) { usage(); return; }
        CgFaction* f = CgFac(p.faction);
        if(IS("r")) { snprintf(buf, sizeof(buf), "** [%s] (%d) %s: %s, over. **", f ? f->name.c_str() : "Radio", p.facRank, me.name, raw); CgFacMsg(p.faction, kCgRadio, buf); }
        else { snprintf(buf, sizeof(buf), "** [DEPT] %s %s: %s **", f ? f->name.c_str() : "", me.name, raw); CgFacMsg(-1, kCgRed, buf); }
        return;
    }

    // ================= level 1 =================
    if(IS("listguns")) { CgTell(slot, kCgWhite, "Weapons: %s", kCgWeapons); return; }
    if(IS("check") || IS("ch"))
    {
        int t = T(0); if(t < 0) return; CgPlayer& tp = CgP(t); const CgSlot& ts = m_CgS[t];
        const int32_t cash = IsRpServer() ? m_Rp[t].cash : ts.cash, bank = IsRpServer() ? m_Rp[t].bank : tp.bank;
        CgTell(slot, kCgYellow, "[%d] %s: level %d, played %dh, cash $%d, bank $%d, gold %d, cookies %d, staff %s, warns %d.", t, m_Slots[t].name, CgPlayLevel(t), tp.minutes / 60, cash, bank, tp.gold, m_Slots[t].cookies, kCgLevelName[CgLevel(t)], tp.warns + m_Slots[t].warns);
        const char* fac = IsRpServer() ? m_Rp[t].fac : (CgFac(tp.faction) ? CgFac(tp.faction)->name.c_str() : "-");
        CgTell(slot, kCgYellow, "Health %d, armour %d, weapon %d, area %d, VW %d, %s, family %d, faction %s.", m_CgHp[t].hp, m_CgHp[t].ar, m_CgHp[t].weapon, ts.area, ts.vw, ts.inCar ? "in a car" : "on foot", tp.family, fac[0] ? fac : "-");
        if(IS("ch") && (m_CgHp[t].hp > 100 || m_CgHp[t].ar > 100 || (!IsRpServer() && ts.cash > 50000000))) CgTell(slot, kCgRed, "Suspicious: check this player.");
        return;
    }
    if(IS("spec"))
    {
        if(a[0] && !strcmp(a[0], "off")) { CgAct(slot, CGA_SPEC, 0, 0); return; }
        int t = T(0); if(t < 0) return; if(t == slot) return;
        CgAct(slot, CGA_SPEC, t, 1); CgTell(slot, kCgGrey, "Spectating %s. /spec off", m_Slots[t].name); return;
    }
    if(IS("setint"))
    {
        int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; }
        float pp[3] = { m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2] }; CgAct(t, CGA_TELE, CgAtoi(a[1]), 0, 0, pp, m_CgS[t].heading);
        snprintf(buf, sizeof(buf), "set the interior of %s to %d.", m_Slots[t].name, CgAtoi(a[1])); adm(buf); return;
    }
    if(IS("setvw")) { int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; } m_CgS[t].vw = (uint16_t)CgAtoi(a[1]); CgSendMe(t); CgTell(slot, kCgGreen, "%s: virtual world %d.", m_Slots[t].name, m_CgS[t].vw); return; }
    if(IS("nmute") || IS("admute") || IS("rmute"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        bool on;
        if(IS("nmute")) on = m_CgS[t].nmute = !m_CgS[t].nmute;
        else if(IS("admute")) on = m_CgS[t].admute = !m_CgS[t].admute;
        else { on = m_Slots[t].reportMuteUntil <= now; m_Slots[t].reportMuteUntil = on ? now + 3600000ULL : 0; }
        const char* what = IS("nmute") ? "the newbie / help chat" : IS("admute") ? "adverts" : "reports";
        snprintf(buf, sizeof(buf), "%s %s from %s.", on ? "muted" : "unmuted", m_Slots[t].name, what); adm(buf);
        CgTell(t, kCgRed, "%s %s you from %s.", me.name, on ? "muted" : "unmuted", what);
        return;
    }
    if(IS("a") || IS("admin")) { if(!*raw) { usage(); return; } snprintf(buf, sizeof(buf), "* %s %s: %s", kCgLevelName[lvl], me.name, raw); CgStaffMsg(kCgStaff, buf); return; }
    if(IS("warnings")) { int t = T(0); if(t < 0) return; CgTell(slot, kCgYellow, "%s has %d warnings (%d this session).", m_Slots[t].name, CgP(t).warns + m_Slots[t].warns, m_Slots[t].warns); return; }
    if(IS("kcc")) { int t = T(0); if(t < 0) return; CgAct(t, CGA_EJECT); snprintf(buf, sizeof(buf), "ejected %s from his car.", m_Slots[t].name); adm(buf); return; }
    if(IS("senddmv")) { int t = T(0); if(t < 0) return; tele(t, -2026.6f, -114.0f, 35.2f, 0.0f, 0); CgTell(t, kCgYellow, "An admin sent you to the DMV."); return; }
    if(IS("sendtols")) { int t = T(0); if(t < 0) return; tele(t, 1773.1f, -1897.8f, 13.6f, 270.0f, 0); CgTell(t, kCgYellow, "An admin sent you to Los Santos."); return; }

    // ================= level 2 =================
    if(IS("skydive")) { int t = T(0); if(t < 0) return; float pp[3] = { m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2] + 900.0f }; CgAct(t, CGA_PARACHUTE, 0, 0, 0, pp); return; }
    if(IS("bigears")) { s.bigears = !s.bigears; CgTell(slot, kCgGreen, "Big ears %s.", s.bigears ? "on (you hear family / faction chats)" : "off"); return; }
    if(IS("freeze") || IS("unfreeze"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        const bool on = IS("freeze"); m_CgS[t].frozen = on; CgAct(t, CGA_FREEZE, on ? 1 : 0); CgSendMe(t);
        snprintf(buf, sizeof(buf), "%s %s.", on ? "froze" : "unfroze", m_Slots[t].name); adm(buf); return;
    }
    if(IS("freezeplayers") || IS("unfreezeplayers") || IS("muteplayers") || IS("unmuteplayers"))
    {
        float r = a[0] ? (float)atof(a[0]) : 30.0f; if(r <= 0) r = 30.0f; int n = 0;
        for(int t = 0; t < MP_MAX_PLAYERS; ++t)
        {
            if(!m_Slots[t].used || t == slot || !m_CgS[t].beat || !within(t, r) || CgLevel(t) >= lvl) continue;
            if(IS("freezeplayers") || IS("unfreezeplayers")) { m_CgS[t].frozen = IS("freezeplayers"); CgAct(t, CGA_FREEZE, m_CgS[t].frozen ? 1 : 0); }
            else m_Slots[t].muted = IS("muteplayers");
            ++n;
        }
        snprintf(buf, sizeof(buf), "used /%s on %d players (%.0f m).", cmd, n, r); adm(buf); return;
    }
    if(IS("slap"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        float pp[3] = { m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2] + 5.0f }; CgAct(t, CGA_TELE, 255, 0, 0, pp, m_CgS[t].heading);
        CgAct(t, CGA_HEALTH, 1, 0, -5);
        snprintf(buf, sizeof(buf), "slapped %s.", m_Slots[t].name); adm(buf); return;
    }
    if(IS("cnn") || IS("mole") || IS("had"))
    {
        if(!*raw) { usage(); return; }
        if(IS("cnn")) { CgAct(-1, CGA_GAMETEXT, 0, 0, 5000, nullptr, 0, raw); return; }
        if(IS("mole")) CgTell(-1, kCgYellow, "SMS: %s, Sender: MOLE (555)", raw); else CgTell(-1, kCgBlue, "HQ: %s", raw);
        return;
    }
    if(IS("refund"))
    {
        int t = T(0); if(t < 0) return; int v = a[1] ? CgAtoi(a[1]) : 0; if(v <= 0) { usage(); return; }
        if(lvl < 5 && v > 1000000) { CgTell(slot, kCgRed, "Up to $1,000,000 below Head Administrator."); return; }
        CgGive(t, v, "Refund"); CgTell(t, kCgGreen, "%s refunded you $%d: %s", me.name, v, restFrom(2));
        snprintf(buf, sizeof(buf), "refunded $%d to %s (%s).", v, m_Slots[t].name, restFrom(2)); adm(buf); return;
    }
    if(IS("tuteject") || IS("forcetut"))
    {
        int t = T(0); if(t < 0) return;
        if(IS("forcetut")) CgDlg(t, CGD_TUT, DIALOG_STYLE_MSGBOX, "Tutorial", "Welcome to the server.\n\n/cgrp - every player command\n/enter /exit - doors\n/buy - shops\n/oocshop - gold bars (1 every 3 paydays)\n/families /factions\n/sellcar [id] [price] and /accept car\n\nAsk staff with /report.", "OK", "");
        else CgTell(t, kCgGreen, "The tutorial is over. Have fun.");
        CgTell(slot, kCgGreen, "Done."); return;
    }
    if(IS("hoseject") || IS("nohospital")) { int t = T(0); if(t < 0) return; CgAct(t, CGA_HEALTH, 0, 0, 100); CgTell(t, kCgGreen, "An admin healed you."); snprintf(buf, sizeof(buf), "healed %s.", m_Slots[t].name); adm(buf); return; }
    if(IS("fuelcars")) { CgAct(-1, CGA_FUEL, 0, 0, 100); adm("filled every car."); return; }
    if(IS("setjob"))
    {
        int t = T(0); if(t < 0) return;
        if(!IsRpServer()) { CgTell(slot, kCgGrey, "Jobs are a Mega Vil Futuristic thing (this server has none)."); return; }
        int j = a[1] ? CgAtoi(a[1]) : -1; if(j < 0 || j >= RPJ_COUNT || !m_Rp[t].loaded) { CgTell(slot, kCgGrey, "Job 0 - %d.", RPJ_COUNT - 1); return; }
        m_Rp[t].job = (uint8_t)j; m_Rp[t].jobOn = false; m_bRpDirty = true; RpSendMe(t);
        snprintf(buf, sizeof(buf), "set the job of %s to %s.", m_Slots[t].name, g_RpJobs[j].name); adm(buf); return;
    }
    if(IS("setskin")) { int t = T(0); if(t < 0) return; int v = a[1] ? CgAtoi(a[1]) : -1; if(v < 0 || v > 311) { usage(); return; } CgAct(t, CGA_SKIN, 0, 0, v); snprintf(buf, sizeof(buf), "set the skin of %s to %d.", m_Slots[t].name, v); adm(buf); return; }
    if(IS("listmuted") || IS("listmasked"))
    {
        int n = 0;
        for(int t = 0; t < MP_MAX_PLAYERS; ++t)
        {
            if(!m_Slots[t].used) continue;
            const bool hit = IS("listmasked") ? m_CgS[t].masked : (m_Slots[t].muted || m_CgS[t].nmute || m_CgS[t].admute || m_Slots[t].reportMuteUntil > now);
            if(hit) { CgTell(slot, kCgYellow, "(%d) %s%s%s%s%s", t, m_Slots[t].name, m_Slots[t].muted ? " [muted]" : "", m_CgS[t].nmute ? " [newbie]" : "", m_CgS[t].admute ? " [adverts]" : "", m_Slots[t].reportMuteUntil > now ? " [reports]" : ""); ++n; }
        }
        if(!n) CgTell(slot, kCgGrey, "Nobody.");
        return;
    }
    if(IS("aduty"))
    {
        if(s.duty) { CgTell(slot, kCgGrey, "You are on duty already (/adutyoff)."); return; }
        s.duty = true; s.dutyColor = me.color; CgRecolor(slot, 0xFF6347FF);
        CgTell(-1, kCgOrange, "%s %s is now on admin duty.", kCgLevelName[lvl], me.name); CgSendMe(slot); return;
    }
    if(IS("adutyoff"))
    {
        if(!s.duty) { CgTell(slot, kCgGrey, "You are not on duty."); return; }
        s.duty = false; CgRecolor(slot, s.dutyColor ? s.dutyColor : me.color);
        CgTell(-1, kCgOrange, "%s %s is off admin duty.", kCgLevelName[lvl], me.name); CgSendMe(slot); return;
    }
    if(IS("checkprison")) { int t = T(0); if(t < 0) return; int sec = IsRpServer() ? m_Rp[t].jailS : CgP(t).jailS; CgTell(slot, kCgYellow, "%s: %d minutes left.", m_Slots[t].name, (sec + 59) / 60); return; }

    // ================= level 3 =================
    if(IS("fws"))
    {
        int t = T(0); if(t < 0) return;
        static const int set[][2] = { { 24, 200 }, { 25, 100 }, { 29, 400 }, { 31, 500 }, { 34, 50 } };
        for(const auto& w : set) CgAct(t, CGA_GUN, w[0], 0, w[1]);
        snprintf(buf, sizeof(buf), "gave %s a full weapon set.", m_Slots[t].name); adm(buf); return;
    }
    if(IS("atazer"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        CgAct(t, CGA_FREEZE, 1, 0, 10000); CgAct(t, CGA_ANIM, 0, 0, 0, nullptr, 0, "CRACK crckdeth2"); CgAct(t, CGA_GAMETEXT, 0, 0, 3000, nullptr, 0, "~r~Tazed");
        snprintf(buf, sizeof(buf), "tazed %s.", m_Slots[t].name); adm(buf); return;
    }
    if(IS("amask")) { s.masked = !s.masked; CgAct(-1, CGA_MASK, s.masked ? 1 : 0, slot); CgTell(slot, kCgGreen, "Mask %s.", s.masked ? "on - your name is hidden" : "off"); CgSendMe(slot); return; }
    if(IS("rhw") || IS("disarm")) { int t = T(0); if(t < 0) return; if(!canHit(t)) return; CgAct(t, CGA_DISARM); snprintf(buf, sizeof(buf), "took the weapons of %s.", m_Slots[t].name); adm(buf); return; }
    if(IS("cp")) { int t = T(0); if(t < 0) return; float pp[3]; here(pp); CgAct(t, CGA_GPS, 0, 0, 0, pp); CgTell(t, kCgYellow, "An admin set a checkpoint for you."); return; }
    if(IS("ipcheck")) { int t = T(0); if(t < 0) return; CgTell(slot, kCgYellow, "%s: %s", m_Slots[t].name, inet_ntoa(m_Slots[t].addr.sin_addr)); return; }
    if(IS("countdown"))
    {
        int v = a[0] ? CgAtoi(a[0]) : 5; if(v < 1) v = 1; if(v > 60) v = 60;
        for(int t = 0; t < MP_MAX_PLAYERS; ++t) if(m_Slots[t].used && m_CgS[t].beat && within(t, 60.0f)) CgAct(t, CGA_COUNTDOWN, 0, 0, v);
        return;
    }
    if(IS("setaccent")) { int t = T(0); if(t < 0) return; CgP(t).accent = restFrom(1); m_bCgDirty = true; CgTell(slot, kCgGreen, "%s: accent %s.", m_Slots[t].name, *restFrom(1) ? restFrom(1) : "none"); return; }
    if(IS("acolorcar")) { if(!a[0]) { usage(); return; } CgAct(slot, CGA_CARCOLOR, CgAtoi(a[0]), a[1] ? CgAtoi(a[1]) : CgAtoi(a[0])); return; }
    if(IS("setgas")) { int v = a[0] ? CgAtoi(a[0]) : -1; if(v < 0 || v > 100) { usage(); return; } CgAct(slot, CGA_FUEL, 1, 0, v); return; }
    if(IS("setcarint")) { if(!a[0]) { usage(); return; } float pp[3]; here(pp); CgAct(slot, CGA_CARTELE, CgAtoi(a[0]), 0, 0, pp, s.heading); return; }
    if(IS("setcarvw")) { if(!a[0]) { usage(); return; } s.vw = (uint16_t)CgAtoi(a[0]); CgSendMe(slot); CgTell(slot, kCgGreen, "Virtual world %d.", s.vw); return; }

    // ================= level 4 =================
    if(IS("vehmenu")) { CgDlg(slot, CGD_VEHMENU, DIALOG_STYLE_LIST, "Vehicle menu", "Repair my car\nRemove my car\nInfernus\nNRG-500\nMaverick\nSultan\nBullet\nTurismo\nPatriot\nRhino", "OK", "Close"); return; }
    if(IS("veh")) { int m = a[0] ? CgAtoi(a[0]) : 0; if(m < 400 || m > 611) { CgTell(slot, kCgGrey, "USAGE: /veh [400-611] [color1] [color2]"); return; } CgAct(slot, CGA_SPAWNCAR, a[1] ? CgAtoi(a[1]) : 1, a[2] ? CgAtoi(a[2]) : 1, m); return; }
    if(IS("fixveh")) { CgAct(slot, CGA_FIXCAR); return; }
    if(IS("givegun"))
    {
        int t = T(0); if(t < 0) return; int w = a[1] ? CgAtoi(a[1]) : 0; int am = a[2] ? CgAtoi(a[2]) : 100;
        if(w < 1 || w > 46) { CgTell(slot, kCgGrey, "USAGE: /givegun [id] [weapon 1-46] [ammo] (/listguns)"); return; }
        CgAct(t, CGA_GUN, w, 0, am); snprintf(buf, sizeof(buf), "gave weapon %d (%d) to %s.", w, am, m_Slots[t].name); adm(buf); return;
    }
    if(IS("givegunall")) { int w = a[0] ? CgAtoi(a[0]) : 0; int am = a[1] ? CgAtoi(a[1]) : 100; if(w < 1 || w > 46) { usage(); return; } CgAct(-1, CGA_GUN, w, 0, am); snprintf(buf, sizeof(buf), "gave everybody weapon %d.", w); adm(buf); return; }
    if(IS("setgunskill") || IS("setfightstyle"))
    {
        int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; }
        if(IS("setfightstyle")) CgAct(t, CGA_FIGHT, 0, 0, CgAtoi(a[1]));
        CgTell(slot, kCgGreen, "%s: %s %s.", m_Slots[t].name, IS("setgunskill") ? "gun skill" : "fight style", restFrom(1)); return;
    }
    if(IS("setmoney"))
    {
        int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; } int v = CgAtoi(a[1]);
        if(IsRpServer()) { if(!m_Rp[t].loaded) return; RpPay(t, v - m_Rp[t].cash, "Admin"); }
        else { m_CgS[t].cash = v; CgAct(t, CGA_SETMONEY, 0, 0, v); }
        snprintf(buf, sizeof(buf), "set the money of %s to $%d.", m_Slots[t].name, v); adm(buf); return;
    }
    if(IS("setstat"))
    {
        int t = T(0); if(t < 0) return; if(!a[1] || !a[2]) { usage(); return; } CgPlayer& tp = CgP(t); int v = CgAtoi(a[2]);
        if(!strcmp(a[1], "gold")) tp.gold = v;
        else if(!strcmp(a[1], "bank")) { if(IsRpServer() && m_Rp[t].loaded) { m_Rp[t].bank = v; m_bRpDirty = true; RpSendMe(t); } else tp.bank = v; }
        else if(!strcmp(a[1], "cookies")) m_Slots[t].cookies = (uint16_t)v;
        else if(!strcmp(a[1], "level")) { if(IsRpServer() && m_Rp[t].loaded) { m_Rp[t].level = (uint8_t)v; m_bRpDirty = true; RpSendMe(t); } else tp.minutes = (v - 1) * 120; }
        else { usage(); return; }
        m_bCgDirty = true; CgSendMe(t); snprintf(buf, sizeof(buf), "set the %s of %s to %d.", a[1], m_Slots[t].name, v); adm(buf); return;
    }
    if(IS("setanim")) { int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; } snprintf(buf, sizeof(buf), "%s %s", a[2] ? a[2] : "PED", a[1]); CgAct(t, CGA_ANIM, 0, 0, 0, nullptr, 0, buf); return; }
    if(IS("givecookieall") || IS("givegoldbarall"))
    {
        int v = a[0] ? CgAtoi(a[0]) : 1; if(v < 1 || v > 100) { usage(); return; }
        const bool ck = IS("givecookieall");
        for(int t = 0; t < MP_MAX_PLAYERS; ++t) if(m_Slots[t].used) { if(ck) m_Slots[t].cookies = (uint16_t)(m_Slots[t].cookies + v); else CgP(t).gold += v; CgSendMe(t); }
        m_bCgDirty = true; CgTell(-1, kCgGreen, "%s gave everybody %d %s.", me.name, v, ck ? "cookies" : "gold bars"); return;
    }
    if(IS("setplayercolor")) { int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; } uint32_t col = (uint32_t)strtoul(a[1], nullptr, 16); CgRecolor(t, (col << 8) | 0xFF); CgTell(slot, kCgGreen, "Colour set."); return; }
    if(IS("setcolor")) { if(!a[0]) { usage(); return; } uint32_t col = (uint32_t)strtoul(a[0], nullptr, 16); CgRecolor(slot, (col << 8) | 0xFF); CgTell(slot, kCgGreen, "Colour set."); return; }
    if(IS("setplayerwanted") || IS("wl") || IS("setcrime"))
    {
        int t = T(0); if(t < 0) return;
        const bool crime = IS("setcrime");
        int v = crime ? -1 : (a[1] ? CgAtoi(a[1]) : -2); if(v == -2 || v > 6) { usage(); return; }
        if(IsRpServer() && m_Rp[t].loaded) { m_Rp[t].wanted = (uint8_t)(v < 0 ? std::min(6, m_Rp[t].wanted + 1) : v); m_bRpDirty = true; RpSendMe(t); v = m_Rp[t].wanted; }
        CgAct(t, CGA_WANTED, crime ? 1 : 0, 0, v);   // a = 1: one star more
        if(crime) CgTell(t, kCgRed, "You committed a crime: %s.", *restFrom(1) ? restFrom(1) : "unknown");
        CgTell(slot, kCgGreen, "%s: wanted level changed.", m_Slots[t].name); return;
    }
    if(IS("adivorce"))
    {
        int t = T(0); if(t < 0) return;
        if(!IsRpServer() || !m_Rp[t].loaded || !m_Rp[t].spouse[0]) { CgTell(slot, kCgGrey, "%s is not married.", m_Slots[t].name); return; }
        m_Rp[t].spouse[0] = 0; m_Rp[t].spouseKey[0] = 0; m_bRpDirty = true; CgTell(t, kCgYellow, "An admin divorced you."); CgTell(slot, kCgGreen, "Done."); return;
    }
    if(IS("makeircadmin")) { int t = T(0); if(t < 0) return; CgP(t).irc ^= 1; m_bCgDirty = true; CgTell(slot, kCgGreen, "%s %s the staff chat.", m_Slots[t].name, CgP(t).irc ? "reads" : "doesn't read"); return; }
    if(IS("destroycar")) { int t = T(0); if(t < 0) return; CgAct(t, CGA_DELCAR); snprintf(buf, sizeof(buf), "removed the car of %s.", m_Slots[t].name); adm(buf); return; }
    if(IS("destroycars")) { CgAct(-1, CGA_DELCAR, 1); adm("removed every empty spawned car."); return; }
    if(IS("gotobluebus")) { tele(slot, 1219.4f, -1812.8f, 16.6f, 90.0f, 0); return; }
    if(IS("gotoblackbus")) { tele(slot, -1987.0f, 138.0f, 27.7f, 0.0f, 0); return; }
    if(IS("fartbomb"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        float pp[3] = { m_CgS[t].pos[0], m_CgS[t].pos[1], m_CgS[t].pos[2] }; CgAct(slot, CGA_EXPLODE, 12, 0, 0, pp);
        CgTell(-1, kCgPurple, "* %s smells something awful near %s.", me.name, m_Slots[t].name); return;
    }

    // ================= level 5 =================
    if(IS("weather")) { int t = T(0); if(t < 0) return; int w = a[1] ? CgAtoi(a[1]) : -1; CgAct(t, CGA_WEATHER, 0, 0, w); CgTell(slot, kCgGreen, "%s: weather %d.", m_Slots[t].name, w); return; }
    if(IS("weatherall"))
    {
        int w = a[0] ? CgAtoi(a[0]) : -1; m_nCgWeather = (w < 0 || w > 45) ? -1 : w; CgWorldNow();
        snprintf(buf, sizeof(buf), "set the weather to %s.", m_nCgWeather < 0 ? "normal" : a[0]); adm(buf); return;
    }
    if(IS("tod")) { int h = a[0] ? CgAtoi(a[0]) : -1; m_nCgHour = (h < 0 || h > 23) ? -1 : h; CgWorldNow(); snprintf(buf, sizeof(buf), "set the hour to %s.", m_nCgHour < 0 ? "normal" : a[0]); adm(buf); return; }
    if(IS("dedit"))
    {
        if(!a[0] || !a[1]) { usage(); return; } int v = CgAtoi(a[1]);
        for(auto& kv : m_CgP)
            if(!strcasecmp(kv.second.name.c_str(), a[0]) || (kv.first.size() > 2 && !strcasecmp(kv.first.c_str() + 2, a[0])))
            { kv.second.bank += v; m_bCgDirty = true; CgTell(slot, kCgGreen, "%s (%s): $%d %s.", kv.second.name.c_str(), kv.first.c_str(), v, IsRpServer() ? "paid as cash at the next login" : "added to the CGRP bank"); return; }
        CgTell(slot, kCgGrey, "No account %s.", a[0]); return;
    }
    if(IS("pointtime")) { CgEnt* e = a[0] ? CgGet(CgAtoi(a[0]), CGK_POINT) : nullptr; if(!e || !a[1]) { usage(); return; } e->r.val = CgAtoi(a[1]); CgChanged(*e); CgTell(slot, kCgGreen, "Point %d: %d minutes.", e->r.id, e->r.val); return; }
    if(IS("adetain")) { int t = T(0); if(t < 0) return; if(!canHit(t)) return; CgJail(t, a[1] ? CgAtoi(a[1]) : 10, me.name); return; }
    if(IS("gotocar")) { int t = T(0); if(t < 0) return; tele(slot, m_CgS[t].pos[0] + 1.5f, m_CgS[t].pos[1], m_CgS[t].pos[2] + 1.0f, m_CgS[t].heading, m_CgS[t].area); return; }
    if(IS("goincar")) { int t = T(0); if(t < 0) return; CgAct(slot, CGA_GOINCAR, t); return; }
    if(IS("pban"))
    {
        int t = T(0); if(t < 0) return; if(!canHit(t)) return;
        const char* why = *restFrom(1) ? restFrom(1) : "Banned";
        snprintf(buf, sizeof(buf), "banned %s: %s", m_Slots[t].name, why); adm(buf); KickLocked(t, me.name, why, true); return;
    }
    if(IS("banaccount") || IS("deleteaccount"))
    {
        if(!a[0]) { usage(); return; }
        std::string acct = a[0]; for(auto& ch : acct) ch = (char)tolower((unsigned char)ch);
        const bool ban = IS("banaccount");
        const char* why = *restFrom(1) ? restFrom(1) : (ban ? "Account banned" : "Account deleted");
        for(int t = 0; t < MP_MAX_PLAYERS; ++t)
            if(m_Slots[t].used && t != slot && CgLevel(t) < lvl && (!strcasecmp(m_Slots[t].account, a[0]) || !strcasecmp(m_Slots[t].name, a[0])))
                KickLocked(t, me.name, why, false);
        if(ban) { m_CgIpBans.push_back("acct:" + acct); AddBan(a[0], "", me.name, why); }
        else
        {
            int n = 0;
            for(auto it = m_CgP.begin(); it != m_CgP.end();) { std::string k = it->first; for(auto& ch : k) ch = (char)tolower((unsigned char)ch); if(k == "a:" + acct || k == "n:" + acct) { it = m_CgP.erase(it); ++n; } else ++it; }
            for(auto it = m_RpSaved.begin(); it != m_RpSaved.end();) { std::string k = it->first; for(auto& ch : k) ch = (char)tolower((unsigned char)ch); if(k == "a:" + acct || k == acct) { it = m_RpSaved.erase(it); ++n; m_bRpDirty = true; } else ++it; }
            CgTell(slot, kCgGreen, "%d saved records of %s deleted.", n, a[0]);
        }
        m_bCgDirty = true; snprintf(buf, sizeof(buf), "%s the account %s.", ban ? "banned" : "deleted", a[0]); adm(buf); return;
    }
    if(IS("banip") || IS("unbanip") || IS("rangeban"))
    {
        std::string ip;
        if(IS("rangeban")) { int t = T(0); if(t < 0) return; if(!canHit(t)) return; ip = inet_ntoa(m_Slots[t].addr.sin_addr); size_t d = ip.find_last_of('.'); if(d != std::string::npos) ip = ip.substr(0, d + 1); }
        else { if(!a[0]) { usage(); return; } ip = a[0]; }
        if(IS("unbanip"))
        {
            auto it = std::find(m_CgIpBans.begin(), m_CgIpBans.end(), ip);
            if(it == m_CgIpBans.end()) { CgTell(slot, kCgGrey, "%s is not banned.", ip.c_str()); return; }
            m_CgIpBans.erase(it);
        }
        else
        {
            m_CgIpBans.push_back(ip);
            for(int t = 0; t < MP_MAX_PLAYERS; ++t)
            {
                if(!m_Slots[t].used || t == slot) continue;
                std::string tip = inet_ntoa(m_Slots[t].addr.sin_addr);
                if(tip == ip || (ip.back() == '.' && !tip.compare(0, ip.size(), ip))) KickLocked(t, me.name, "IP banned", false);
            }
        }
        m_bCgDirty = true; snprintf(buf, sizeof(buf), "%s %s.", IS("unbanip") ? "unbanned" : "banned the IP", ip.c_str()); adm(buf); return;
    }
    if(IS("setgoldbars") || IS("givegoldbar"))
    {
        int t = T(0); if(t < 0) return; if(!a[1]) { usage(); return; } int v = CgAtoi(a[1]);
        if(IS("setgoldbars")) CgP(t).gold = v; else CgP(t).gold += v;
        m_bCgDirty = true; CgSendMe(t); CgTell(t, kCgYellow, "You have %d gold bars now.", CgP(t).gold);
        snprintf(buf, sizeof(buf), "changed the gold bars of %s (%d).", m_Slots[t].name, CgP(t).gold); adm(buf); return;
    }
    if(IS("fire")) { float pp[3]; here(pp); pp[0] += 2.0f; CgAct(slot, CGA_FIRE, 0, 0, 0, pp); return; }
    if(IS("re") || IS("resources"))
    {
        int on = 0; for(int t = 0; t < MP_MAX_PLAYERS; ++t) if(m_Slots[t].used) ++on;
        int k[CGK_COUNT] = {}; for(const auto& e : m_Cg) ++k[e.r.kind];
        CgTell(slot, kCgYellow, "Players %d/%d, CGRP things %d (biz %d, objects %d, gates %d), accounts %d, families %d, factions %d.", on, MP_MAX_PLAYERS, (int)m_Cg.size(), k[CGK_BIZ], k[CGK_OBJECT], k[CGK_GATE], (int)m_CgP.size(), (int)m_CgFam.size(), (int)m_CgFac.size());
        return;
    }

    // ================= creation (levels 5-9) =================
    if((int)m_Cg.size() >= CG_MAX_ENTS && (IS("giftbox") || !strncmp(cmd, "create", 6) || !strncmp(cmd, "make", 4) || IS("moneybag") || IS("hqcreate") || IS("gcam") || IS("editpoint")))
    { CgTell(slot, kCgRed, "The server has the most CGRP things (%d).", CG_MAX_ENTS); return; }
    auto made = [&](CgEnt& e) { CgChanged(e); CgTell(slot, kCgGreen, "%s %d created%s%s.", kCgKindName[e.r.kind], e.r.id, e.r.name[0] ? ": " : "", e.r.name); };
    if(IS("giftbox")) { CgEnt& e = CgNew(slot, CGK_GIFTBOX); MP_CopyStr(e.r.name, "Gift Box", sizeof(e.r.name)); made(e); return; }
    if(IS("makemoneybag") || IS("moneybag"))
    {
        int v = a[0] ? CgAtoi(a[0]) : 0; if(v <= 0) { usage(); return; }
        CgEnt& e = CgNew(slot, CGK_MONEYBAG); e.r.val = v; made(e);
        CgTell(-1, kCgYellow, "[MONEY BAG] A money bag with $%d is hidden somewhere. Find it.", v); return;
    }
    if(IS("deletebags")) { std::vector<int> ids; for(auto& e : m_Cg) if(e.r.kind == CGK_MONEYBAG) ids.push_back(e.r.id); for(int id : ids) CgDel(id); CgTell(slot, kCgGreen, "%d money bags removed.", (int)ids.size()); return; }
    if(IS("editpoint"))
    {
        if(!a[0]) { usage(); return; }
        CgEnt* e = CgGet(CgAtoi(a[0]), CGK_POINT);
        if(!e) { CgEnt& n = CgNew(slot, CGK_POINT); MP_CopyStr(n.r.name, raw, sizeof(n.r.name)); n.r.val = 10; n.r.price = 5000; made(n); return; }
        if(!a[1]) { usage(); return; }
        if(!strcmp(a[1], "delete")) { CgDel(e->r.id); CgTell(slot, kCgGreen, "Point deleted."); return; }
        if(!strcmp(a[1], "pos")) here(&e->r.x);
        else if(!strcmp(a[1], "name")) MP_CopyStr(e->r.name, restFrom(2), sizeof(e->r.name));
        else if(!strcmp(a[1], "owner")) { CgFamily* f = CgFam(a[2] ? CgAtoi(a[2]) : 0); e->r.group = f ? f->id : 0; MP_CopyStr(e->r.owner, f ? f->name.c_str() : "", sizeof(e->r.owner)); }
        else if(!strcmp(a[1], "pay")) e->r.price = a[2] ? CgAtoi(a[2]) : 5000;
        else { usage(); return; }
        CgChanged(*e); CgTell(slot, kCgGreen, "Point %d changed.", e->r.id); return;
    }
    if(IS("editor"))
    {
        int id = 0, model = a[0] ? CgAtoi(a[0]) : 0;
        if(!model) { CgEnt* e = near(CGK_OBJECT, 25.0f); if(!e) { CgTell(slot, kCgGrey, "USAGE: /editor [model] - or stand near a mapped object."); return; } id = e->r.id; model = e->r.model; }
        CgAct(slot, CGA_EDITOR, 1, 0, id, nullptr, (float)model);
        return;
    }
    if(IS("hqcreate"))
    {
        if(!a[0] || !a[1]) { usage(); return; }
        const bool fam = !strcmp(a[0], "fam");
        CgEnt& e = CgNew(slot, CGK_HQ); e.r.sub = fam ? CGG_FAMILY : CGG_FACTION;
        if(fam || !IsRpServer())
        {
            e.r.group = (uint16_t)CgAtoi(a[1]);
            if(fam) { if(CgFamily* f = CgFam(e.r.group)) MP_CopyStr(e.r.owner, f->name.c_str(), sizeof(e.r.owner)); }
            else if(CgFaction* f = CgFac(e.r.group)) MP_CopyStr(e.r.owner, f->name.c_str(), sizeof(e.r.owner));
        }
        else MP_CopyStr(e.r.owner, a[1], sizeof(e.r.owner));   // Mega Vil: the faction tag
        snprintf(e.r.name, sizeof(e.r.name), "%s HQ", e.r.owner[0] ? e.r.owner : (fam ? "Family" : "Faction"));
        made(e); CgTell(slot, kCgGrey, "Now go inside and type /hqedit %d.", e.r.id); return;
    }
    if(IS("createfamgate") || IS("createfacgate") || IS("creategate"))
    {
        const bool pub = IS("creategate"); const int ai = pub ? 0 : 1;
        if(!a[ai]) { usage(); return; }
        CgEnt& e = CgNew(slot, CGK_GATE); e.r.model = (uint16_t)CgAtoi(a[ai]); if(!e.r.model) e.r.model = 980;
        e.r.sub = pub ? CGG_PUBLIC : (IS("createfamgate") ? CGG_FAMILY : CGG_FACTION);
        if(!pub) { if(IsRpServer() && e.r.sub == CGG_FACTION) MP_CopyStr(e.r.owner, a[0], sizeof(e.r.owner)); else e.r.group = (uint16_t)CgAtoi(a[0]); }
        e.r.ix = e.r.x; e.r.iy = e.r.y; e.r.iz = e.r.z - 6.0f; e.r.ia = e.r.a;
        made(e);
        CgTell(slot, kCgGrey, "Gate closed here. Stand where it should open to and type /edit%sgate %d (default: it sinks 6 m).", pub ? "" : (e.r.sub == CGG_FAMILY ? "fam" : "fac"), e.r.id); return;
    }
    if(IS("famgateeditor") || IS("facgateeditor"))
    {
        CgEnt* e = a[0] ? CgGet(CgAtoi(a[0]), CGK_GATE) : nullptr; if(!e || !a[1] || !a[2]) { usage(); return; }
        float v = (float)atof(a[2]);
        if(!strcmp(a[1], "x")) e->r.x += v; else if(!strcmp(a[1], "y")) e->r.y += v; else if(!strcmp(a[1], "z")) e->r.z += v; else if(!strcmp(a[1], "rot")) e->r.a += v; else { usage(); return; }
        CgChanged(*e); CgTell(slot, kCgGreen, "Gate %d nudged.", e->r.id); return;
    }
    if(IS("createfamily"))
    {
        if(!a[0]) { usage(); return; }
        CgFamily f; f.id = m_nCgFamNext++; f.name = a[0]; f.color = 0x33CCFFFF;
        int t = a[1] ? CgFindPlayer(a[1]) : -1;
        if(t >= 0) { f.leader = m_Slots[t].name; CgP(t).family = f.id; CgP(t).famRank = 6; }
        m_CgFam.push_back(f); m_bCgDirty = true;
        if(t >= 0) { CgSendMe(t); CgTell(t, kCgFam, "You lead the family %s now.", f.name.c_str()); }
        CgTell(slot, kCgGreen, "Family %d (%s) created.", f.id, f.name.c_str()); return;
    }
    if(IS("acceptrequest"))
    {
        if(!a[0] || !a[1]) { usage(); return; }
        int t = CgFindPlayer(a[1]); if(t < 0) { CgTell(slot, kCgGrey, "No such player."); return; }
        CgFamily* f = nullptr; for(auto& x : m_CgFam) if(!strcasecmp(x.name.c_str(), a[0])) f = &x;
        if(!f) { CgFamily nf; nf.id = m_nCgFamNext++; nf.name = a[0]; m_CgFam.push_back(nf); f = &m_CgFam.back(); }
        f->leader = m_Slots[t].name; CgP(t).family = f->id; CgP(t).famRank = 6; m_bCgDirty = true; CgSendMe(t);
        CgTell(t, kCgFam, "Your family request was accepted: you lead %s.", f->name.c_str()); CgTell(slot, kCgGreen, "Done."); return;
    }
    if(IS("deletefamily") || IS("fedit"))
    {
        CgFamily* f = a[0] ? CgFam(CgAtoi(a[0])) : nullptr; if(!f) { usage(); return; }
        if(IS("deletefamily"))
        {
            const uint16_t id = f->id;
            for(auto& kv : m_CgP) if(kv.second.family == id) { kv.second.family = 0; kv.second.famRank = 0; }
            for(size_t i = 0; i < m_CgFam.size(); ++i) if(m_CgFam[i].id == id) { m_CgFam.erase(m_CgFam.begin() + i); break; }
            m_bCgDirty = true; for(int t = 0; t < MP_MAX_PLAYERS; ++t) if(m_Slots[t].used) CgSendMe(t);
            CgTell(slot, kCgGreen, "Family %d deleted.", id); return;
        }
        if(!a[1]) { usage(); return; }
        if(!strcmp(a[1], "name")) f->name = restFrom(2);
        else if(!strcmp(a[1], "leader")) { int t = CgFindPlayer(a[2]); if(t < 0) { CgTell(slot, kCgGrey, "No such player."); return; } f->leader = m_Slots[t].name; CgP(t).family = f->id; CgP(t).famRank = 6; CgSendMe(t); }
        else if(!strcmp(a[1], "color")) f->color = ((uint32_t)strtoul(a[2] ? a[2] : "33CCFF", nullptr, 16) << 8) | 0xFF;
        else if(!strcmp(a[1], "safe")) f->safe = a[2] ? CgAtoi(a[2]) : 0;
        else { usage(); return; }
        m_bCgDirty = true; CgTell(slot, kCgGreen, "Family %d changed.", f->id); return;
    }
    if(IS("makeleader"))
    {
        int t = T(0); if(t < 0) return; CgFaction* f = a[1] ? CgFac(CgAtoi(a[1])) : nullptr;
        if(!f) { CgTell(slot, kCgGrey, "No faction %s (/factions).", a[1] ? a[1] : "?"); return; }
        f->leader = m_Slots[t].name; CgP(t).faction = f->id; CgP(t).facRank = 6; m_bCgDirty = true; CgSendMe(t);
        CgTell(-1, kCgBlue, "%s leads %s now.", m_Slots[t].name, f->name.c_str()); return;
    }
    if(IS("editfaction") || IS("editfactionpay"))
    {
        if(IS("editfaction") && a[0] && !strcmp(a[0], "new"))
        {
            CgFaction f; f.id = m_nCgFacNext++; f.name = *restFrom(1) ? restFrom(1) : "Faction"; for(int k = 1; k <= 6; ++k) f.pay[k] = 300 + k * 200;
            m_CgFac.push_back(f); m_bCgDirty = true; CgTell(slot, kCgGreen, "Faction %d (%s) created. /makeleader [id] %d", f.id, f.name.c_str(), f.id); return;
        }
        CgFaction* f = a[0] ? CgFac(CgAtoi(a[0])) : nullptr; if(!f || !a[1]) { usage(); return; }
        if(IS("editfactionpay")) { int r = CgAtoi(a[1]); if(r < 1 || r > 6 || !a[2]) { usage(); return; } f->pay[r] = CgAtoi(a[2]); }
        else if(!strcmp(a[1], "name")) f->name = restFrom(2);
        else if(!strcmp(a[1], "type")) f->type = (uint8_t)(a[2] ? CgAtoi(a[2]) : 0);
        else if(!strcmp(a[1], "leader")) f->leader = restFrom(2);
        else if(!strcmp(a[1], "delete"))
        {
            const uint16_t id = f->id;
            for(auto& kv : m_CgP) if(kv.second.faction == id) { kv.second.faction = 0; kv.second.facRank = 0; }
            for(size_t i = 0; i < m_CgFac.size(); ++i) if(m_CgFac[i].id == id) { m_CgFac.erase(m_CgFac.begin() + i); break; }
        }
        else { usage(); return; }
        m_bCgDirty = true; CgTell(slot, kCgGreen, "Faction changed."); return;
    }
    if(IS("createfcar") || IS("createcar") || IS("createpcar"))
    {
        int model = 0, sub = CGG_PUBLIC, t = -1, price = 0, c1 = 1, c2 = 1; uint16_t group = 0; const char* tag = "";
        if(IS("createcar")) { model = a[0] ? CgAtoi(a[0]) : 0; price = a[1] ? CgAtoi(a[1]) : 0; }
        else if(IS("createpcar")) { t = a[0] ? CgFindPlayer(a[0]) : -1; if(t < 0) { CgTell(slot, kCgGrey, "No such player."); return; } model = a[1] ? CgAtoi(a[1]) : 0; sub = CGG_PLAYER; }
        else
        {
            const bool fam = a[0] && !strcmp(a[0], "fam"); sub = fam ? CGG_FAMILY : CGG_FACTION;
            if(!fam && IsRpServer()) tag = a[1] ? a[1] : ""; else group = (uint16_t)(a[1] ? CgAtoi(a[1]) : 0);
            model = a[2] ? CgAtoi(a[2]) : 0; c1 = a[3] ? CgAtoi(a[3]) & 0xFF : 1; c2 = a[4] ? CgAtoi(a[4]) & 0xFF : 1;
        }
        if(model < 400 || model > 611) { usage(); return; }
        CgEnt& e = CgNew(slot, CGK_CARSPOT); e.r.model = (uint16_t)model; e.r.sub = (uint8_t)sub; e.r.price = price; e.r.group = group; e.r.val = c1 | (c2 << 8);
        MP_CopyStr(e.r.owner, tag, sizeof(e.r.owner));
        if(t >= 0) { char kb[64]; e.ownerKey = CgKey(t, kb, sizeof(kb)); MP_CopyStr(e.r.owner, m_Slots[t].name, sizeof(e.r.owner)); }
        snprintf(e.r.name, sizeof(e.r.name), "Vehicle %d", e.r.model);
        made(e); return;
    }
    if(IS("bizint"))
    {
        if(!a[0] || !strcmp(a[0], "list"))
        {
            std::string l; for(int i = 1; i < kCgIntCount; ++i) { snprintf(buf, sizeof(buf), "%d %s\n", i, kCgInts[i].name); l += buf; }
            CgDlg(slot, CG_DLG_BASE + 0x81, DIALOG_STYLE_MSGBOX, "Interiors", l.c_str(), "OK", ""); return;
        }
        CgEnt* e = CgGet(CgAtoi(a[0])); int v = a[1] ? CgAtoi(a[1]) : -1;
        if(!e || !CgDoorKind(e->r.kind) || v < 0 || v >= kCgIntCount) { usage(); return; }
        const CgInt& in = kCgInts[v]; e->r.iarea = in.area; e->r.ix = in.x; e->r.iy = in.y; e->r.iz = in.z; e->r.ia = in.a;
        CgChanged(*e); CgTell(slot, kCgGreen, "%s %d: interior %s.", kCgKindName[e->r.kind], e->r.id, in.name); return;
    }
    if(IS("createbiz"))
    {
        CgEnt& e = CgNew(slot, CGK_BIZ); MP_CopyStr(e.r.name, *raw ? raw : "Business", sizeof(e.r.name));
        e.r.price = 100000; e.r.val = 200; e.r.level = 1; e.r.flags = CGF_SALE; e.r.sub = CGB_247;
        const CgInt& in = kCgInts[1]; e.r.iarea = in.area; e.r.ix = in.x; e.r.iy = in.y; e.r.iz = in.z; e.r.ia = in.a;
        made(e); CgTell(slot, kCgGrey, "For sale: $100,000. /biztype /bizint /bizprice %d.", e.r.id); return;
    }
    if(IS("houseint") || IS("chouseint") || IS("createhouse") || IS("deletehouse") || IS("houselevel") || IS("houseprice") || IS("movehouse") || IS("clearhouse") || IS("sethouseowner") || IS("gotohouse"))
    { CgHomeCmd(slot, cmd, raw2); return; }
    if(IS("createbuilding") || IS("createentrance") || IS("createmotel"))
    {
        const int kind = IS("createbuilding") ? CGK_BUILDING : IS("createentrance") ? CGK_ENTRANCE : CGK_MOTEL;
        CgEnt& e = CgNew(slot, kind); MP_CopyStr(e.r.name, *raw ? raw : kCgKindName[kind], sizeof(e.r.name)); e.r.val = -1;
        if(kind == CGK_MOTEL) { e.r.price = 50; e.r.val = 0; const CgInt& in = kCgInts[31]; e.r.iarea = in.area; e.r.ix = in.x; e.r.iy = in.y; e.r.iz = in.z; e.r.ia = in.a; }
        made(e); CgTell(slot, kCgGrey, "Go to the inside and type /%s %d in.", kind == CGK_ENTRANCE ? "editentrance" : kind == CGK_MOTEL ? "movemotel" : "movebuilding", e.r.id); return;
    }
    if(IS("editentrance") || IS("editgarage"))
    {
        const int kind = IS("editentrance") ? CGK_ENTRANCE : CGK_GARAGE;
        CgEnt* e = a[0] ? CgGet(CgAtoi(a[0]), kind) : nullptr; if(!e || !a[1]) { usage(); return; }
        CgRow& r = e->r;
        if(!strcmp(a[1], "out")) { here(&r.x); r.a = s.heading; r.area = s.area; }
        else if(!strcmp(a[1], "in")) { r.ix = s.pos[0]; r.iy = s.pos[1]; r.iz = s.pos[2]; r.ia = s.heading; r.iarea = s.area; }
        else if(!strcmp(a[1], "name")) MP_CopyStr(r.name, restFrom(2), sizeof(r.name));
        else if(!strcmp(a[1], "vip")) r.flags ^= CGF_VIP;
        else if(!strcmp(a[1], "lock")) r.flags ^= CGF_LOCKED;
        else if(!strcmp(a[1], "price")) r.price = a[2] ? CgAtoi(a[2]) : 0;
        else if(!strcmp(a[1], "clear")) { e->ownerKey.clear(); r.owner[0] = 0; r.flags |= CGF_SALE; }
        else { usage(); return; }
        CgChanged(*e); CgTell(slot, kCgGreen, "%s %d changed.", kCgKindName[kind], r.id); return;
    }
    if(IS("creategarage"))
    {
        CgEnt& e = CgNew(slot, CGK_GARAGE); e.r.price = a[0] ? CgAtoi(a[0]) : 25000; e.r.flags = CGF_SALE; MP_CopyStr(e.r.name, "Garage", sizeof(e.r.name));
        made(e); CgTell(slot, kCgGrey, "Drive to the inside and type /editgarage %d in.", e.r.id); return;
    }
    if(IS("createdrivethru")) { CgEnt& e = CgNew(slot, CGK_DRIVETHRU); e.r.group = (uint16_t)(a[0] ? CgAtoi(a[0]) : 0); MP_CopyStr(e.r.name, *restFrom(1) ? restFrom(1) : "Drive-Thru", sizeof(e.r.name)); made(e); return; }
    if(IS("editdrivethru"))
    {
        CgEnt* e = a[0] ? CgGet(CgAtoi(a[0]), CGK_DRIVETHRU) : nullptr; if(!e || !a[1]) { usage(); return; }
        if(!strcmp(a[1], "location")) { here(&e->r.x); e->r.a = s.heading; }
        else if(!strcmp(a[1], "name")) MP_CopyStr(e->r.name, restFrom(2), sizeof(e->r.name));
        else if(!strcmp(a[1], "biz")) e->r.group = (uint16_t)(a[2] ? CgAtoi(a[2]) : 0);
        else { usage(); return; }
        CgChanged(*e); CgTell(slot, kCgGreen, "Drive-thru %d changed.", e->r.id); return;
    }
    if(IS("createhss"))
    {
        if(!m_bHomeLoaded) HomeLoad();
        int hi = a[0] ? HomeFind((uint16_t)CgAtoi(a[0])) : -1; if(hi < 0) { usage(); return; }
        CgEnt& e = CgNew(slot, CGK_HSSIGN); e.r.group = m_Homes[hi].h.id; MP_CopyStr(e.r.name, m_Homes[hi].h.name, sizeof(e.r.name));
        snprintf(e.r.text, sizeof(e.r.text), "FOR SALE: $%d - /buyhouse at the door", m_Homes[hi].h.price); e.r.price = m_Homes[hi].h.price; made(e); return;
    }
    if(IS("createmapicon")) { if(!a[0]) { usage(); return; } CgEnt& e = CgNew(slot, CGK_MAPICON); MP_CopyStr(e.r.name, a[0], sizeof(e.r.name)); e.r.model = (uint16_t)(a[1] ? CgAtoi(a[1]) : 56); made(e); return; }
    if(IS("editmapicon"))
    {
        CgEnt* e = a[0] ? CgGet(CgAtoi(a[0]), CGK_MAPICON) : nullptr; if(!e || !a[1]) { usage(); return; }
        if(!strcmp(a[1], "delete")) { CgDel(e->r.id); CgTell(slot, kCgGreen, "Map icon deleted."); return; }
        if(!strcmp(a[1], "icon")) e->r.model = (uint16_t)(a[2] ? CgAtoi(a[2]) : 56);
        else if(!strcmp(a[1], "pos")) here(&e->r.x);
        else if(!strcmp(a[1], "name")) MP_CopyStr(e->r.name, restFrom(2), sizeof(e->r.name));
        else { usage(); return; }
        CgChanged(*e); CgTell(slot, kCgGreen, "Map icon %d changed.", e->r.id); return;
    }
    if(IS("createmapb")) { CgEnt& e = CgNew(slot, CGK_MAPBOX); MP_CopyStr(e.r.name, "Map Box", sizeof(e.r.name)); made(e); return; }
    if(IS("createinfoboard")) { CgEnt& e = CgNew(slot, CGK_INFOBOARD); MP_CopyStr(e.r.name, "Information", sizeof(e.r.name)); MP_CopyStr(e.r.text, raw, sizeof(e.r.text)); made(e); return; }
    if(IS("creategstation")) { CgEnt& e = CgNew(slot, CGK_GSTATION); MP_CopyStr(e.r.name, *raw ? raw : "Gas Station", sizeof(e.r.name)); made(e); return; }
    if(IS("createoocshop")) { CgEnt& e = CgNew(slot, CGK_OOCSHOP); MP_CopyStr(e.r.name, "OOC Shop", sizeof(e.r.name)); made(e); return; }
    if(IS("createatm")) { CgEnt& e = CgNew(slot, CGK_ATM); MP_CopyStr(e.r.name, "ATM", sizeof(e.r.name)); made(e); return; }
    if(IS("gcam"))
    {
        if(a[0] && !strcmp(a[0], "delete")) { CgEnt* e = a[1] ? CgGet(CgAtoi(a[1]), CGK_SPEEDCAM) : nullptr; if(!e) { usage(); return; } CgDel(e->r.id); CgTell(slot, kCgGreen, "Speed camera deleted."); return; }
        CgEnt& e = CgNew(slot, CGK_SPEEDCAM); e.r.val = a[0] ? CgAtoi(a[0]) : 90; e.r.price = a[1] ? CgAtoi(a[1]) : 500;
        snprintf(e.r.name, sizeof(e.r.name), "Speed camera %d km/h", e.r.val); made(e); return;
    }
    if(IS("createpickup"))
    {
        if(!a[0] || !a[1]) { usage(); return; }
        static const char* const types[] = { "info", "health", "armour", "weapon", "cash" };
        int ty = -1; for(int i = 0; i < 5; ++i) if(!strcmp(a[1], types[i])) ty = i;
        if(ty < 0) { usage(); return; }
        CgEnt& e = CgNew(slot, CGK_PICKUP); e.r.model = (uint16_t)CgAtoi(a[0]); e.r.sub = (uint8_t)ty;
        if(ty == CGP_INFO) MP_CopyStr(e.r.text, restFrom(2), sizeof(e.r.text)); else e.r.val = a[2] ? CgAtoi(a[2]) : 0;
        snprintf(e.r.name, sizeof(e.r.name), "Pickup (%s)", types[ty]); made(e); return;
    }

    // ================= levels 8-9 =================
    if(IS("payday")) { CgPayday(); adm("gave everybody a payday."); return; }
    if(IS("oprison"))
    {
        if(!a[0]) { usage(); return; } int mins = a[1] ? CgAtoi(a[1]) : 30;
        for(int t = 0; t < MP_MAX_PLAYERS; ++t) if(m_Slots[t].used && (!strcasecmp(m_Slots[t].account, a[0]) || !strcasecmp(m_Slots[t].name, a[0]))) { CgJail(t, mins, me.name); return; }
        for(auto& kv : m_CgP)
            if(!strcasecmp(kv.second.name.c_str(), a[0]) || (kv.first.size() > 2 && !strcasecmp(kv.first.c_str() + 2, a[0])))
            { kv.second.jailS = mins * 60; m_bCgDirty = true; CgTell(slot, kCgGreen, "%s goes to the admin prison for %d minutes at the next login.", kv.second.name.c_str(), mins); return; }
        CgTell(slot, kCgGrey, "No account %s.", a[0]); return;
    }
    if(IS("lethimtalk"))
    {
        int t = T(0); if(t < 0) return;
        m_Slots[t].muted = false; m_CgS[t].nmute = m_CgS[t].admute = false; m_Slots[t].reportMuteUntil = 0;
        CgTell(t, kCgGreen, "Every mute was removed."); snprintf(buf, sizeof(buf), "unmuted %s everywhere.", m_Slots[t].name); adm(buf); return;
    }
    if(IS("shawntermination"))
    {
        int t = T(0); if(t < 0) return; if(m_Slots[t].admin || t == slot) { CgTell(slot, kCgRed, "Not on the administrator."); return; }
        Slot& ts = m_Slots[t]; CgP(t).level = 0; CgP(t).helper = 0; m_bCgDirty = true;
        if(ts.granted) { ts.mod = false; ts.granted = false; if(ts.uid[0]) { if(ts.vip) m_Ranks[ts.uid] = 'V'; else m_Ranks.erase(ts.uid); RanksSave(); } Retag(t); }
        CgSendMe(t); snprintf(buf, sizeof(buf), "removed every staff level of %s.", ts.name); adm(buf); return;
    }
    if(IS("forcerelog")) { int t = T(0); if(t < 0) return; if(!canHit(t)) return; KickLocked(t, me.name, "Please reconnect (relog)", false); return; }
    if(IS("savechars")) { CgSave(); if(m_bHomeLoaded) HomeSave(); if(IsRpServer()) m_bRpDirty = true; CgTell(slot, kCgGreen, "Saved."); return; }
    if(IS("makehelper"))
    {
        int t = T(0); if(t < 0) return; CgP(t).helper = (uint8_t)(a[1] ? (CgAtoi(a[1]) != 0) : 1); m_bCgDirty = true; CgSendMe(t);
        CgTell(-1, kCgGreen, "%s is %s a helper.", m_Slots[t].name, CgP(t).helper ? "now" : "no longer"); return;
    }

    CgTell(slot, kCgGrey, "/%s: %s", def->name, def->usage);
}
