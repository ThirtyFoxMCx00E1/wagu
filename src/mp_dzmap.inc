// 4.10: Project Day Z Legacy - dynamic mapping, the phone side (included by mp_dayz.inc).
// The server sends every map object (net/dz_map.h); this file makes the ones near you as solid map
// objects (like SA-MP's streamer: in within DZM_STREAM_IN, out past DZM_STREAM_OUT, at most DZM_MAX_SHOWN
// at once) and handles /mapping find + names on the phone with the model catalog
// (Host/DayZ/mapping/catalog.txt - every GTA SA + SA-MP model id, dff, txd and tags).

struct DzMapC { DzMapRow r; int obj = 0; uint64_t tryAt = 0; };
struct DzCatRow { uint16_t id; std::string dff, txd, tags; bool samp; };
static struct DzMapClient
{
    std::unordered_map<uint32_t, DzMapC> objs;
    int nShown = 0; uint64_t scanAt = 0; uint16_t total = 0;
    // the catalog (downloaded once, kept in the MP folder)
    std::mutex catM; std::vector<DzCatRow> cat; std::atomic<int> catState{0};   // 0 none, 1 loading, 2 ready, 3 failed
    std::string pendingFind;
} s_DzM;

static const char* kDzCatUrls[] = {
    "https://mpsamp-open.github.io/Host/DayZ/mapping/catalog.txt",   // 4.12.7: served by the website
};

static void DzMapDelObj(DzMapC& c) { if(c.obj > 0) { Game::DeleteWorldObject(c.obj); --s_DzM.nShown; } c.obj = 0; }
static void DzMapClear()
{
    for(auto& kv : s_DzM.objs) DzMapDelObj(kv.second);
    s_DzM.objs.clear(); s_DzM.nShown = 0; s_DzM.total = 0;
}

static void DzMapOnPacket(const uint8_t* d, int len)
{
    if(len < (int)offsetof(PktDzMap, r)) return;
    const PktDzMap* P = (const PktDzMap*)d;
    int n = std::min<int>(P->n, DZM_PAGE);
    if(len < (int)offsetof(PktDzMap, r) + n * (int)sizeof(DzMapRow)) n = (len - (int)offsetof(PktDzMap, r)) / (int)sizeof(DzMapRow);
    if(P->op == DZMO_LIST) { for(auto& kv : s_DzM.objs) DzMapDelObj(kv.second); s_DzM.objs.clear(); }
    s_DzM.total = P->total;
    for(int i = 0; i < n; ++i)
    {
        DzMapRow r; memcpy(&r, &P->r[i], sizeof(r));
        auto it = s_DzM.objs.find(r.id);
        if(P->op == DZMO_DEL) { if(it != s_DzM.objs.end()) { DzMapDelObj(it->second); s_DzM.objs.erase(it); } continue; }
        if(it == s_DzM.objs.end()) { DzMapC c; c.r = r; s_DzM.objs[r.id] = c; continue; }
        DzMapC& c = it->second; const DzMapRow o = c.r; c.r = r;
        if(c.obj > 0 && o.model != r.model) DzMapDelObj(c);
        else if(c.obj > 0) Game::PlaceMapObject(c.obj, { r.x, r.y, r.z }, r.rx10 / 10.0f, r.ry10 / 10.0f, r.rz10 / 10.0f);
    }
}

// every frame on Project Day Z: stream the objects near me in and out (a full pass every 250 ms)
static void DzMapTick(const Vec3& me, uint64_t now)
{
    if(s_DzM.objs.empty() || now - s_DzM.scanAt < 250 || !Game::IsReady() || Game::IsLoadPending()) return;   // 4.13.15: never while the world loads
    s_DzM.scanAt = now;
    const int area = Game::GetAreaVisible();
    int made = 0;
    for(auto& kv : s_DzM.objs)
    {
        DzMapC& c = kv.second; const DzMapRow& r = c.r;
        const float dx = r.x - me.x, dy = r.y - me.y, d2 = dx * dx + dy * dy;
        if(c.obj > 0)
        {
            if(d2 > DZM_STREAM_OUT * DZM_STREAM_OUT || r.interior != area || !Game::WorldObjectExists(c.obj)) DzMapDelObj(c);
            continue;
        }
        if(d2 > DZM_STREAM_IN * DZM_STREAM_IN || r.interior != area || made >= 6 || now < c.tryAt || s_DzM.nShown >= DZM_MAX_SHOWN) continue;
        if(!Game::IsKnownModel(r.model)) { c.tryAt = now + 60000; continue; }   // a SA-MP id that is not in this game / the Day Z pack
        int h = Game::CreateMapObject(r.model, { r.x, r.y, r.z }, r.rx10 / 10.0f, r.ry10 / 10.0f, r.rz10 / 10.0f);
        if(h > 0) { c.obj = h; ++s_DzM.nShown; ++made; }
        else c.tryAt = now + (h < 0 ? 4000 : 200);   // 0 = the model is loading
    }
}

// ---- the model catalog --------------------------------------------------------------------------
static void DzCatParse(const std::string& text, std::vector<DzCatRow>& out)
{
    size_t p = 0;
    while(p < text.size())
    {
        size_t e = text.find('\n', p); if(e == std::string::npos) e = text.size();
        if(text[p] != '#' && e > p)
        {
            char dff[64], txd[64], ide[48], kind[8], tags[160] = "-"; unsigned id; float dd; unsigned long fl;
            std::string line = text.substr(p, e - p);
            if(sscanf(line.c_str(), "%u %63s %63s %47s %f %lu %7s %159s", &id, dff, txd, ide, &dd, &fl, kind, tags) >= 7 && id < 20000)
                out.push_back({ (uint16_t)id, dff, txd, strcmp(tags, "-") ? tags : "", !strcmp(kind, "samp") });
        }
        p = e + 1;
    }
}
static void DzCatLoad()
{
    int st = s_DzM.catState.load(); if(st == 1 || st == 2) return;
    s_DzM.catState = 1;
    std::thread([]() {
        char path[460]; snprintf(path, sizeof(path), "%s/dz_catalog.txt", MP::DataDir());
        std::string text;
        if(FILE* f = fopen(path, "rb"))
        {
            fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
            if(sz > 1000 && sz < 8 * 1024 * 1024) { text.resize((size_t)sz); text.resize(fread(&text[0], 1, (size_t)sz, f)); }
            fclose(f);
        }
        if(text.find("dynamic mapping catalog") == std::string::npos || text.find("tags+desc") == std::string::npos)   // 4.13.19: re-download once for the description tags
        {
            text.clear();
            for(const char* u : kDzCatUrls)
            {
                std::string body, why;
                if(Online::HttpsFetch(u, body, 60000, why, 6 * 1024 * 1024) && body.find("dynamic mapping catalog") != std::string::npos) { text.swap(body); break; }
                logger->Info("4.10: mapping catalog %s: %s", u, why.c_str());
            }
            if(!text.empty()) if(FILE* f = fopen(path, "wb")) { fwrite(text.data(), 1, text.size(), f); fclose(f); }
        }
        std::vector<DzCatRow> rows; DzCatParse(text, rows);
        { std::lock_guard<std::mutex> lk(s_DzM.catM); s_DzM.cat.swap(rows); }
        s_DzM.catState = s_DzM.cat.empty() ? 3 : 2;
        logger->Info("4.10: mapping catalog: %d models", (int)s_DzM.cat.size());
    }).detach();
}
static void DzCatFind(const char* q)
{
    if(s_DzM.catState != 2) { DzCatLoad(); s_DzM.pendingFind = q; DzNotice(DZ_GRY, "Loading the model catalog..."); return; }
    // 4.13.19: every word must match the dff, the txd or a tag (the tags hold gta-scout's reviewed
    // descriptions too), so "/mapedit find wood pallet" or "red barrel" finds a model by its look
    std::vector<std::string> words; std::string w;
    for(const char* s = q;; ++s)
    {
        if(!*s || *s == ' ' || *s == ',') { if(!w.empty()) words.push_back(w); w.clear(); if(!*s) break; continue; }
        w += (char)tolower((unsigned char)*s);
    }
    if(words.empty()) return;
    int shown = 0, hits = 0;
    std::lock_guard<std::mutex> lk(s_DzM.catM);
    for(const auto& r : s_DzM.cat)
    {
        std::string d = r.dff + " " + r.txd; for(auto& ch : d) ch = (char)tolower((unsigned char)ch);
        bool hit = isdigit((unsigned char)q[0]) && (unsigned)atoi(q) == r.id;
        if(!hit) { hit = true; for(const auto& x : words) if(d.find(x) == std::string::npos && r.tags.find(x) == std::string::npos) { hit = false; break; } }
        if(!hit) continue;
        ++hits;
        if(shown < 14) { SysMsg(r.samp ? 0xA0A0A0FF : COL_WHITE, "%u  %s  (txd %s)%s%s%s", r.id, r.dff.c_str(), r.txd.c_str(), r.tags.empty() ? "" : "  ", r.tags.c_str(), r.samp ? "  [SA-MP]" : ""); ++shown; }
    }
    SysMsg(COL_SYSTEM, hits ? "%d models match \"%s\" (%d shown). /mapping add <id> puts one in front of you." : "No model matches \"%s\".", hits ? hits : 0, q, shown);
}
static int DzCatId(const char* name)
{   // a dff name (exact, any case) -> model id; -1 unknown, -2 the catalog is not loaded yet
    if(s_DzM.catState != 2) { DzCatLoad(); return -2; }
    std::lock_guard<std::mutex> lk(s_DzM.catM);
    for(const auto& r : s_DzM.cat) if(!strcasecmp(r.dff.c_str(), name)) return r.id;
    return -1;
}

// /mapping on the phone: find runs here; add gets the spot in front of you (on the ground) + a name
// turned into its id; the rest goes to the server as it is
static bool DzMapLocalCmd(const char* arg)
{
    if(s_DzM.catState == 2 && !s_DzM.pendingFind.empty()) { std::string q; q.swap(s_DzM.pendingFind); DzCatFind(q.c_str()); }
    while(arg && *arg == ' ') ++arg;
    if(!arg) arg = "";
    if(!strncasecmp(arg, "find ", 5) || !strncasecmp(arg, "search ", 7))
    {
        const char* q = strchr(arg, ' '); while(*q == ' ') ++q;
        if(*q) DzCatFind(q);
        return true;
    }
    if(!strncasecmp(arg, "add ", 4))
    {
        const char* a = arg + 4; while(*a == ' ') ++a;
        char what[64] = ""; float dist = 4.0f; sscanf(a, "%63s %f", what, &dist);
        if(dist < 1.0f) dist = 1.0f; if(dist > 40.0f) dist = 40.0f;
        int model = isdigit((unsigned char)what[0]) ? atoi(what) : DzCatId(what);
        if(model == -2) { DzNotice(DZ_GRY, "Loading the model catalog - try again in a moment (or use the id)."); return true; }
        if(model <= 0 || model >= 20000) { DzNotice(DZ_RED, "Unknown model. /mapping find <name> lists the ids."); return true; }
        if(!Game::IsKnownModel(model)) DzNotice(DZ_YEL, "Model %d is not in this game (SA-MP object?) - others with the pack will see it.", model);
        int ped = Game::LocalPed(); Vec3 me;
        if(ped <= 0 || !Game::GetCharPos(ped, me)) return true;
        float h = Game::GetCharHeading(ped), rad = h * 3.14159265f / 180.0f;
        float x = me.x - sinf(rad) * dist, y = me.y + cosf(rad) * dist;
        float g = Game::GroundZBelow(x, y, me.z + 3.0f);
        float mb = Game::ModelBottom(model); if(mb > 0.0f) mb = 0.0f;
        float z = (g > me.z - 30.0f && g < me.z + 4.0f) ? g - mb : me.z - 1.0f;
        char t[100]; snprintf(t, sizeof(t), "/mapping add %d %.2f %.2f %.2f %.1f", model, x, y, z, h);
        DzSend(DZCMD_TEXT, 0, 0, 0, t);
        return true;
    }
    return false;
}
