// 4.10: Project Day Z Legacy - dynamic mapping, the server side (included at the end of server_dayz.inc).
// See net/dz_map.h for the files and formats. Admins and moderators place / move / remove objects with
// /mapping (alias /mo); everybody gets the whole list on join and every change at once.
#include <dirent.h>
#include <ctype.h>
#include <strings.h>

static int DzMapNumArgs(const char* p, float* out, int maxN)
{   // "123, 1.5, -2.0e1, ..." up to ')' - returns how many numbers were read
    int n = 0;
    while(*p && n < maxN)
    {
        while(*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if(*p == ')' || !*p) break;
        char* e = nullptr; float v = strtof(p, &e);
        if(e == p) break;   // a variable / expression: stop (the rest stays default)
        out[n++] = v; p = e;
        while(*p == ' ' || *p == '\t') ++p;
        if(*p == ',') ++p; else break;
    }
    return n;
}

static bool DzMapAttr(const char* tag, const char* tagEnd, const char* name, float& v)
{   // name="value" inside one <object ... /> tag
    size_t nl = strlen(name);
    for(const char* p = tag; p && p < tagEnd; ++p)
    {
        p = strstr(p, name); if(!p || p >= tagEnd) return false;
        if((p == tag || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n') && p[nl] == '=' && (p[nl + 1] == '"' || p[nl + 1] == '\''))
        { v = strtof(p + nl + 2, nullptr); return true; }
    }
    return false;
}

void MPServer::DzMapAddRow(uint16_t model, float x, float y, float z, float rx, float ry, float rz, int interior, uint8_t flags, const char* owner)
{
    if((int)m_DzMap.size() >= DZM_MAX_ALL || model == 0 || model >= 20000) return;
    if(!(x == x) || !(y == y) || !(z == z) || fabsf(x) > 3500.0f || fabsf(y) > 3500.0f || z < -100.0f || z > 1500.0f) return;
    DzMapObj o; memset(&o.r, 0, sizeof(o.r));
    o.r.id = m_nDzMapId++; o.r.model = model; o.r.interior = (uint8_t)(interior < 0 ? 0 : interior > 255 ? 255 : interior); o.r.flags = flags;
    o.r.x = x; o.r.y = y; o.r.z = z; o.r.rx10 = DzMapAng10(rx); o.r.ry10 = DzMapAng10(ry); o.r.rz10 = DzMapAng10(rz);
    o.owner = owner ? owner : "";
    m_DzMap.push_back(o);
}

int MPServer::DzMapImportText(const char* text, uint8_t flags)
{
    int n = 0;
    // SA-MP: CreateObject / CreateDynamicObject / CreateDynamicObjectEx / CreatePlayerObject
    static const struct { const char* fn; int skip; int intArg; } kFns[] = {
        { "CreateDynamicObjectEx(", 0, -1 }, { "CreateDynamicObject(", 0, 8 }, { "CreatePlayerObject(", 1, -1 }, { "CreateObject(", 0, -1 },
    };
    for(const auto& f : kFns)
    {
        size_t fl = strlen(f.fn);
        for(const char* p = strstr(text, f.fn); p; p = strstr(p + fl, f.fn))
        {
            if(p > text && (isalnum((unsigned char)p[-1]) || p[-1] == '_')) continue;   // MyCreateObject( ...
            const char* a = p + fl;
            if(f.skip) { const char* c = strchr(a, ','); if(!c) continue; a = c + 1; }   // playerid
            float v[10]; int k = DzMapNumArgs(a, v, 10);
            if(k < 4) continue;
            float rx = k > 4 ? v[4] : 0, ry = k > 5 ? v[5] : 0, rz = k > 6 ? v[6] : 0;
            int interior = (f.intArg >= 0 && k > f.intArg && v[f.intArg] >= 0) ? (int)v[f.intArg] : 0;
            size_t before = m_DzMap.size();
            DzMapAddRow((uint16_t)v[0], v[1], v[2], v[3], rx, ry, rz, interior, flags, "");
            if(m_DzMap.size() > before) ++n;
        }
    }
    // MTA: <object ... model="" posX="" ... />
    for(const char* p = strstr(text, "<object"); p; p = strstr(p + 7, "<object"))
    {
        const char* e = strchr(p, '>'); if(!e) break;
        float m = 0, x = 0, y = 0, z = 0, rx = 0, ry = 0, rz = 0, in = 0;
        if(!DzMapAttr(p, e, "model", m) || !DzMapAttr(p, e, "posX", x) || !DzMapAttr(p, e, "posY", y) || !DzMapAttr(p, e, "posZ", z)) continue;
        DzMapAttr(p, e, "rotX", rx); DzMapAttr(p, e, "rotY", ry); DzMapAttr(p, e, "rotZ", rz); DzMapAttr(p, e, "interior", in);
        size_t before = m_DzMap.size();
        DzMapAddRow((uint16_t)m, x, y, z, rx, ry, rz, (int)in, flags, "");
        if(m_DzMap.size() > before) ++n;
    }
    return n;
}

void MPServer::DzMapLoad()
{
    m_DzMap.clear(); m_nDzMapId = 1;
    int files = 0, fromFiles = 0;
    std::string dir = DzSideFile("dayz_maps");
    if(DIR* d = opendir(dir.c_str()))
    {
        std::vector<std::string> names;
        while(dirent* e = readdir(d))
        {
            const char* n = e->d_name; size_t l = strlen(n);
            if(l < 5 || n[0] == '.') continue;
            const char* ext = strrchr(n, '.'); if(!ext) continue;
            if(strcasecmp(ext, ".pwn") && strcasecmp(ext, ".inc") && strcasecmp(ext, ".txt") && strcasecmp(ext, ".map")) continue;
            names.push_back(n);
        }
        closedir(d);
        std::sort(names.begin(), names.end());
        for(const auto& n : names)
        {
            std::string path = dir + "/" + n;
            FILE* f = fopen(path.c_str(), "rb"); if(!f) continue;
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            if(sz <= 0 || sz > 16 * 1024 * 1024) { fclose(f); continue; }
            std::string text((size_t)sz, '\0'); size_t r = fread(&text[0], 1, (size_t)sz, f); fclose(f); text.resize(r);
            int k = DzMapImportText(text.c_str(), DZMF_FILE);
            Log("DAYZ: mapping file %s: %d objects", n.c_str(), k);
            ++files; fromFiles += k;
        }
    }
    int placed = 0;
    if(FILE* f = fopen(DzSideFile("dayz_mapping.txt").c_str(), "r"))
    {
        char line[256];
        while(fgets(line, sizeof(line), f))
        {
            if(line[0] != 'O' || line[1] != ' ') continue;
            unsigned id = 0; int model = 0, in = 0; float x, y, z, rx, ry, rz; char owner[64] = "";
            if(sscanf(line + 2, "%u %d %f %f %f %f %f %f %d %63s", &id, &model, &x, &y, &z, &rx, &ry, &rz, &in, owner) < 9) continue;
            size_t before = m_DzMap.size();
            DzMapAddRow((uint16_t)model, x, y, z, rx, ry, rz, in, 0, owner);
            if(m_DzMap.size() > before) ++placed;
        }
        fclose(f);
    }
    Log("DAYZ: dynamic mapping: %d objects (%d from %d files in dayz_maps/, %d placed in game)", (int)m_DzMap.size(), fromFiles, files, placed);
}

void MPServer::DzMapSave()
{
    std::string path = DzSideFile("dayz_mapping.txt"), tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "w"); if(!f) return;
    fprintf(f, "# Project Day Z Legacy - objects placed in game with /mapping (4.10): O id model x y z rx ry rz interior placedBy\n");
    fprintf(f, "# whole map files (SA-MP CreateObject / CreateDynamicObject, MTA .map) go into the dayz_maps folder\n");
    for(const auto& o : m_DzMap)
    {
        if(o.r.flags & DZMF_FILE) continue;
        fprintf(f, "O %u %u %.3f %.3f %.3f %.1f %.1f %.1f %u %s\n", o.r.id, o.r.model, o.r.x, o.r.y, o.r.z, o.r.rx10 / 10.0f, o.r.ry10 / 10.0f, o.r.rz10 / 10.0f, o.r.interior, o.owner.empty() ? "-" : o.owner.c_str());
    }
    fclose(f); rename(tmp.c_str(), path.c_str());
    m_bDzMapDirty = false;
}

void MPServer::DzMapSend(int slot)
{
    PktDzMap b; memset(&b, 0, sizeof(b)); b.type = PKT_DZ_MAP; b.op = DZMO_LIST; b.total = (uint16_t)std::min<size_t>(m_DzMap.size(), 65535);
    auto flush = [&](bool force) {
        if(!b.n && !force) return;
        SendTo(slot, &b, (int)(offsetof(PktDzMap, r) + sizeof(DzMapRow) * b.n));
        b.n = 0; b.op = DZMO_MORE;
    };
    for(const auto& o : m_DzMap) { b.r[b.n++] = o.r; if(b.n == DZM_PAGE) flush(false); }
    flush(b.op == DZMO_LIST);   // an empty list still clears the phone
}

void MPServer::DzMapSendOne(const DzMapRow& r, uint8_t op)
{
    PktDzMap b; memset(&b, 0, sizeof(b)); b.type = PKT_DZ_MAP; b.op = op; b.n = 1; b.total = (uint16_t)std::min<size_t>(m_DzMap.size(), 65535); b.r[0] = r;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) SendTo(i, &b, (int)(offsetof(PktDzMap, r) + sizeof(DzMapRow)));
}

bool MPServer::DzMapCmd(int slot, const char* arg)
{
    Slot& s = m_Slots[slot];
    char sub[16] = ""; const char* rest = "";
    if(arg) { while(*arg == ' ') ++arg; int n = 0; while(arg[n] && arg[n] != ' ' && n < 15) { sub[n] = (char)tolower((unsigned char)arg[n]); ++n; } sub[n] = 0; rest = arg + n; while(*rest == ' ') ++rest; }
    auto IS = [&](const char* c) { return !strcmp(sub, c); };
    const float* me = s.pos;
    if(!sub[0] || IS("help"))
    {
        DzMsg(slot, DZC_YELLOW, "--- Dynamic mapping (%d objects) ---", (int)m_DzMap.size());
        DzMsg(slot, DZC_WHITE, "/mapping find <name>  /mapping add <id | name> [dist]  /mapping del [id]  /mapping undo");
        DzMsg(slot, DZC_WHITE, "/mapping move <id> <x> <y> <z>  /mapping rot <id> <rx> <ry> <rz>  /mapping near  /mapping reload");
        DzMsg(slot, DZC_GREY, "Any GTA SA model id works (gtastuff.com/models, dev.prineside.com/gtasa_samp_model_id). Map files: dayz_maps/ (SA-MP .pwn, MTA .map)");
        return true;
    }
    if(IS("near") || IS("count"))
    {
        int shown = 0;
        DzMsg(slot, DZC_YELLOW, "[MAP] %d objects on the server. Near you:", (int)m_DzMap.size());
        for(const auto& o : m_DzMap)
        {
            float dx = o.r.x - me[0], dy = o.r.y - me[1], dz = o.r.z - me[2];
            if(dx * dx + dy * dy + dz * dz > 40.0f * 40.0f) continue;
            DzMsg(slot, DZC_WHITE, "#%u model %u  %.1f m%s", o.r.id, o.r.model, sqrtf(dx * dx + dy * dy + dz * dz), (o.r.flags & DZMF_FILE) ? "  (map file)" : "");
            if(++shown >= 12) break;
        }
        if(!shown) DzMsg(slot, DZC_GREY, "[MAP] nothing within 40 m.");
        return true;
    }
    if(!s.admin && !s.mod) { DzMsg(slot, DZC_RED, "[MAP] Admins and moderators only."); return true; }
    char kb[40]; const char* key = RpKey(slot, kb, sizeof(kb));
    std::string who = key ? key : s.name;
    if(IS("reload"))
    {
        DzMapLoad();
        for(int i = 0; i < MP_MAX_PLAYERS; ++i) if(m_Slots[i].used) DzMapSend(i);
        DzMsg(-1, DZC_YELLOW, "[MAP] %s reloaded the map: %d objects.", s.name, (int)m_DzMap.size());
        DzLog("ADMIN %s mapping reload (%d)", s.name, (int)m_DzMap.size());
        return true;
    }
    if(IS("add"))
    {   // the phone sends: add <model> <x> <y> <z> <rz>  (in front of you, on the ground)
        float v[5] = { 0, 0, 0, 0, 0 };
        int k = sscanf(rest, "%f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4]);
        if(k < 1 || v[0] < 1 || v[0] >= 20000) { DzMsg(slot, DZC_RED, "[MAP] /mapping add <model id | name> - find ids with /mapping find <name>"); return true; }
        float x = k >= 4 ? v[1] : me[0], y = k >= 4 ? v[2] : me[1], z = k >= 4 ? v[3] : me[2] - 1.0f, rz = k >= 5 ? v[4] : 0;
        float dx = x - me[0], dy = y - me[1];
        if(dx * dx + dy * dy > 60.0f * 60.0f) { DzMsg(slot, DZC_RED, "[MAP] Too far away."); return true; }
        size_t before = m_DzMap.size();
        DzMapAddRow((uint16_t)v[0], x, y, z, 0, 0, rz, 0, 0, who.c_str());
        if(m_DzMap.size() == before) { DzMsg(slot, DZC_RED, "[MAP] Can't place that (limit %d objects).", DZM_MAX_ALL); return true; }
        m_bDzMapDirty = true; m_DzMapLast[slot] = m_DzMap.back().r.id;
        DzMapSendOne(m_DzMap.back().r, DZMO_SET);
        DzMsg(slot, DZC_GREEN, "[MAP] #%u placed (model %d). /mapping rot %u 0 0 <deg> to turn it, /mapping undo to remove it.", m_DzMap.back().r.id, (int)v[0], m_DzMap.back().r.id);
        DzLog("ADMIN %s mapping add %d at %.1f %.1f %.1f", s.name, (int)v[0], x, y, z);
        return true;
    }
    // the rest works on one object: by id, or (del / undo) the nearest / last one
    uint32_t id = 0;
    if(IS("undo")) id = m_DzMapLast[slot];
    else id = (uint32_t)strtoul(rest, nullptr, 10);
    if(!id && IS("del"))
    {
        float best = 12.0f * 12.0f;
        for(const auto& o : m_DzMap)
        {
            if(o.r.flags & DZMF_FILE) continue;
            float dx = o.r.x - me[0], dy = o.r.y - me[1], dz = o.r.z - me[2], d2 = dx * dx + dy * dy + dz * dz;
            if(d2 < best) { best = d2; id = o.r.id; }
        }
        if(!id) { DzMsg(slot, DZC_RED, "[MAP] No placed object within 12 m (map file objects: edit the file)."); return true; }
    }
    for(size_t i = 0; i < m_DzMap.size(); ++i)
    {
        DzMapObj& o = m_DzMap[i]; if(o.r.id != id) continue;
        if(o.r.flags & DZMF_FILE) { DzMsg(slot, DZC_RED, "[MAP] #%u comes from a map file in dayz_maps/ - change the file and /mapping reload.", id); return true; }
        if(IS("del") || IS("undo"))
        {
            DzMapRow gone = o.r; m_DzMap.erase(m_DzMap.begin() + (long)i); m_bDzMapDirty = true;
            if(m_DzMapLast[slot] == id) m_DzMapLast[slot] = 0;
            DzMapSendOne(gone, DZMO_DEL);
            DzMsg(slot, DZC_GREEN, "[MAP] #%u removed.", id);
            DzLog("ADMIN %s mapping del %u", s.name, id);
            return true;
        }
        const char* nums = rest; while(*nums && *nums != ' ') ++nums;
        float a = 0, b = 0, c = 0;
        if(sscanf(nums, "%f %f %f", &a, &b, &c) != 3) { DzMsg(slot, DZC_RED, "[MAP] /mapping %s <id> <3 numbers>", sub); return true; }
        if(IS("move"))
        {
            if(fabsf(a) > 3500.0f || fabsf(b) > 3500.0f || c < -100.0f || c > 1500.0f) return true;
            o.r.x = a; o.r.y = b; o.r.z = c;
        }
        else if(IS("rot")) { o.r.rx10 = DzMapAng10(a); o.r.ry10 = DzMapAng10(b); o.r.rz10 = DzMapAng10(c); }
        else { DzMsg(slot, DZC_GREY, "[MAP] /mapping help"); return true; }
        m_bDzMapDirty = true;
        DzMapSendOne(o.r, DZMO_SET);
        DzMsg(slot, DZC_GREEN, "[MAP] #%u updated.", id);
        return true;
    }
    DzMsg(slot, DZC_RED, "[MAP] No object #%u.", id);
    return true;
}
