// 4.10: ARM's "Advanced Settings" (libAdvancedSettings.SA.so, 7.3.x) bridge.
//
// What that mod does (decompiled with Ghidra 12.2, see research/AdvancedSettings_decompiled.c):
//   * hooks CWidgetPlayerInfo::Draw / DrawWeaponIcon / DrawWanted / Render*Bar and CWidgetRadar::Update and
//     draws the whole GTA HUD (clock, money, bars, weapon icon, ammo, stars, radar, area / vehicle name,
//     radio, subtitles) at the positions of its ini - the HUD_* sections. PosX / PosY are % of the screen.
//   * the ini is aml->GetConfigPath() + "<its GUID>.ini" and it RELOADS IT LIVE: it compares the file's
//     mtime (stat) and re-reads it when it changed. So MP can move that HUD just by writing the ini.
//   * it does NOT hook AND_TouchEvent - it never takes a touch from MP. The trouble was the layout: the
//     common presets put the clock / money / weapon icon in the top right (PosX 88 - 95, PosY 6 - 18),
//     right where the MP launcher has its search / refresh / region (pin) / gear buttons.
//
// Settings > GTA HUD: shows every HUD_* item of that ini with sliders (position, size) and switches, and
// one button that moves the top-right items under MP's toolbar. The first change keeps a copy of the
// original file next to it (<ini>.mp_backup) and "Restore" puts it back.

struct AdvKey  { std::string key, val; int line = -1; };
struct AdvSect { std::string name; std::vector<AdvKey> keys; };
static struct
{
    char path[512] = "";
    bool found = false;
    uint64_t scanAt = 0, saveAt = 0;
    bool dirty = false;
    std::vector<std::string> lines;
    std::vector<AdvSect> sects;
    char preset[64] = "", version[32] = "";
} s_Adv;

static std::string AdvTrim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

static bool AdvLoad(const char* path)
{
    FILE* f = fopen(path, "rb"); if(!f) return false;
    std::vector<std::string> lines; char buf[1024];
    while(fgets(buf, sizeof(buf), f)) { size_t n = strlen(buf); while(n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0; lines.push_back(buf); }
    fclose(f);
    std::vector<AdvSect> sects; std::string cur;
    char preset[64] = "", version[32] = "";
    bool hud = false;
    for(int i = 0; i < (int)lines.size(); ++i)
    {
        std::string t = AdvTrim(lines[i]);
        if(t.empty() || t[0] == ';' || t[0] == '#') continue;
        if(t[0] == '[') { size_t e = t.find(']'); cur = t.substr(1, e == std::string::npos ? std::string::npos : e - 1); if(!cur.compare(0, 4, "HUD_")) { hud = true; sects.push_back(AdvSect{ cur, {} }); } continue; }
        size_t eq = t.find('='); if(eq == std::string::npos) continue;
        std::string k = AdvTrim(t.substr(0, eq)), v = t.substr(eq + 1);
        size_t sc = v.find(';'); if(sc != std::string::npos) v = v.substr(0, sc);
        v = AdvTrim(v);
        if(cur == "About" && k == "PresetBy") snprintf(preset, sizeof(preset), "%s", v.c_str());
        if(cur == "About" && k == "Version")  snprintf(version, sizeof(version), "%s", v.c_str());
        if(!cur.compare(0, 4, "HUD_") && !sects.empty()) sects.back().keys.push_back(AdvKey{ k, v, i });
    }
    if(!hud) return false;
    s_Adv.lines.swap(lines); s_Adv.sects.swap(sects);
    snprintf(s_Adv.preset, sizeof(s_Adv.preset), "%s", preset); snprintf(s_Adv.version, sizeof(s_Adv.version), "%s", version);
    return true;
}

// configs/AdvancedSettings.SA.ini (its GUID), else any configs/*.ini with a [HUD_WeaponIcon] section
static void AdvFind(bool force)
{
    uint64_t now = MP_NowMs();
    if(!force && (s_Adv.found || (s_Adv.scanAt && now - s_Adv.scanAt < 10000))) return;
    s_Adv.scanAt = now;
    const char* dir = aml->GetConfigPath();
    if(!dir || !*dir) return;
    char p[512];
    snprintf(p, sizeof(p), "%s%sAdvancedSettings.SA.ini", dir, dir[strlen(dir) - 1] == '/' ? "" : "/");
    if(AdvLoad(p)) { snprintf(s_Adv.path, sizeof(s_Adv.path), "%s", p); s_Adv.found = true; logger->Info("4.10: AdvancedSettings ini: %s", p); return; }
    if(DIR* d = opendir(dir))
    {
        while(dirent* e = readdir(d))
        {
            size_t n = strlen(e->d_name);
            if(n < 5 || strcasecmp(e->d_name + n - 4, ".ini") || strstr(e->d_name, "thirtyfox")) continue;
            snprintf(p, sizeof(p), "%s%s%s", dir, dir[strlen(dir) - 1] == '/' ? "" : "/", e->d_name);
            FILE* f = fopen(p, "rb"); if(!f) continue;
            char buf[4096]; size_t r = fread(buf, 1, sizeof(buf) - 1, f); buf[r] = 0; fclose(f);
            if(!strstr(buf, "[HUD_WeaponIcon]") && !strstr(buf, "[HUD_BarHealth]")) continue;
            if(AdvLoad(p)) { snprintf(s_Adv.path, sizeof(s_Adv.path), "%s", p); s_Adv.found = true; logger->Info("4.10: AdvancedSettings ini: %s", p); break; }
        }
        closedir(d);
    }
}

static AdvKey* AdvGet(const char* sect, const char* key)
{
    for(auto& s : s_Adv.sects) if(s.name == sect) for(auto& k : s.keys) if(k.key == key) return &k;
    return nullptr;
}

// write the value back into its own line, keeping the key's spacing and the "; comment"
static void AdvSet(AdvKey& k, const std::string& v)
{
    if(k.val == v || k.line < 0 || k.line >= (int)s_Adv.lines.size()) return;
    std::string& L = s_Adv.lines[k.line];
    size_t eq = L.find('='); if(eq == std::string::npos) return;
    size_t vs = L.find_first_not_of(" \t", eq + 1); if(vs == std::string::npos) vs = L.size();
    size_t sc = L.find(';', vs);
    size_t ve = sc == std::string::npos ? L.size() : sc;
    std::string tail = sc == std::string::npos ? std::string() : L.substr(sc);
    std::string pad;
    if(!tail.empty()) { int w = (int)(ve - vs) - (int)v.size(); pad.assign(w > 1 ? w : 1, ' '); }
    L = L.substr(0, vs) + v + pad + tail;
    k.val = v; s_Adv.dirty = true; s_Adv.saveAt = MP_NowMs() + 400;
}

static void AdvSave()
{
    if(!s_Adv.dirty || !s_Adv.found) return;
    char bak[540]; snprintf(bak, sizeof(bak), "%s.mp_backup", s_Adv.path);
    if(access(bak, F_OK) != 0)
    {   // the very first change: keep the player's original preset
        FILE* in = fopen(s_Adv.path, "rb"); FILE* out = in ? fopen(bak, "wb") : nullptr;
        if(in && out) { char b[4096]; size_t r; while((r = fread(b, 1, sizeof(b), in)) > 0) fwrite(b, 1, r, out); }
        if(in) fclose(in); if(out) fclose(out);
    }
    char tmp[540]; snprintf(tmp, sizeof(tmp), "%s.mp_tmp", s_Adv.path);
    FILE* f = fopen(tmp, "wb"); if(!f) { logger->Error("4.10: AdvancedSettings ini not writable: %s", s_Adv.path); s_Adv.dirty = false; return; }
    for(auto& l : s_Adv.lines) { fputs(l.c_str(), f); fputc('\n', f); }
    fclose(f);
    rename(tmp, s_Adv.path);   // whole file at once: the mod never reads half a file
    s_Adv.dirty = false;
}

static bool AdvRestore()
{
    char bak[540]; snprintf(bak, sizeof(bak), "%s.mp_backup", s_Adv.path);
    FILE* in = fopen(bak, "rb"); if(!in) return false;
    char tmp[540]; snprintf(tmp, sizeof(tmp), "%s.mp_tmp", s_Adv.path);
    FILE* out = fopen(tmp, "wb"); if(!out) { fclose(in); return false; }
    char b[4096]; size_t r; while((r = fread(b, 1, sizeof(b), in)) > 0) fwrite(b, 1, r, out);
    fclose(in); fclose(out); rename(tmp, s_Adv.path);
    s_Adv.dirty = false;
    AdvLoad(s_Adv.path);
    return true;   // 4.12.4: the file is back even if it has no HUD_* sections
}

// 4.12.3: the "GTA HUD" page and the launcher move are gone (the HUD looked weird). Once per game start,
// if MP ever changed the Advanced Settings ini (<ini>.mp_backup exists) the original preset goes back and the
// backup is deleted - the player's own HUD returns. MP never writes that ini again.
static void AdvTick()
{
    static int s_nState = 0; static uint64_t s_nAt = 0;
    if(s_nState == 2) return;
    uint64_t now = MP_NowMs();
    if(s_nState == 0) { s_nState = 1; s_nAt = now + 3000; return; }
    if(now < s_nAt) return;
    s_nState = 2;
    // 4.12.4: every ini MP ever changed in the configs folder goes back (4.12.3 only looked at the first ini
    // with HUD_* sections, so a preset kept in another ini - weapon icon / bars / radar - stayed moved).
    // A left-over <ini>.mp_tmp (game closed while saving) is deleted too.
    const char* dir = aml->GetConfigPath();
    if(!dir || !*dir) return;
    const char* sep = dir[strlen(dir) - 1] == '/' ? "" : "/";
    std::vector<std::string> baks, tmps;
    if(DIR* d = opendir(dir))
    {
        while(dirent* e = readdir(d))
        {
            const size_t n = strlen(e->d_name);
            if(n > 14 && !strcmp(e->d_name + n - 10, ".mp_backup")) baks.push_back(std::string(e->d_name, n - 10));
            else if(n > 11 && !strcmp(e->d_name + n - 7, ".mp_tmp")) tmps.push_back(e->d_name);
        }
        closedir(d);
    }
    for(auto& t : tmps) { char p[600]; snprintf(p, sizeof(p), "%s%s%s", dir, sep, t.c_str()); unlink(p); }
    for(auto& ini : baks)
    {
        snprintf(s_Adv.path, sizeof(s_Adv.path), "%s%s%s", dir, sep, ini.c_str());
        char bak[600]; snprintf(bak, sizeof(bak), "%s.mp_backup", s_Adv.path);
        if(AdvRestore()) { unlink(bak); logger->Info("4.12.4: Advanced Settings HUD: original preset restored (%s)", s_Adv.path); }
    }
    s_Adv.found = false; s_Adv.path[0] = 0;   // MP never writes that ini again
}
