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

static bool AdvParse(std::vector<std::string>& lines);
static bool AdvReadLines(const char* path, std::vector<std::string>& lines)
{
    FILE* f = fopen(path, "rb"); if(!f) return false;
    char buf[1024];
    while(fgets(buf, sizeof(buf), f)) { size_t n = strlen(buf); while(n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0; lines.push_back(buf); }
    fclose(f);
    return true;
}
static bool AdvLoad(const char* path)
{
    std::vector<std::string> lines;
    if(!AdvReadLines(path, lines)) return false;
    return AdvParse(lines);
}
// 4.12.5: parse lines (the file, or the lines after MP inserted a missing key) into s_Adv
static bool AdvParse(std::vector<std::string>& lines)
{
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

// 4.12.3: the "GTA HUD" page and the launcher move were removed (the HUD looked weird).
// 4.12.5: the restore of every <ini>.mp_backup MP left behind runs ONCE (not on every start any more),
// because Settings > Display > "HUD position" now lets the player move the HUD by hand and keeps a
// backup of the original preset for its "Restore" button.
static void AdvRestoreOld()
{
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
        if(AdvRestore()) { unlink(bak); logger->Info("4.12.5: Advanced Settings HUD: original preset restored (%s)", s_Adv.path); }
    }
    s_Adv.found = false; s_Adv.path[0] = 0;
}

static uint64_t s_nAdvLastSave = 0;
static void AdvTick()
{
    static int s_nState = 0; static uint64_t s_nAt = 0;
    const uint64_t now = MP_NowMs();
    if(s_nState == 0) { s_nState = 1; s_nAt = now + 3000; return; }
    if(s_nState == 1)
    {
        if(now < s_nAt) return;
        s_nState = 2;
        if(!::cfg->GetBool("AdvRestoreOnce4125", false, "MP"))
        {
            AdvRestoreOld();
            ConfigEntry* e = ::cfg->Bind("AdvRestoreOnce4125", true, "MP"); e->SetBool(true); delete e; ::cfg->Save();
        }
        return;
    }
    // HUD position page: write the ini a moment after the last slider move. Advanced Settings reloads it
    // when the file time changes - two writes in the same second could be missed, so keep them 1.1 s apart.
    if(s_Adv.dirty && now >= s_Adv.saveAt && now - s_nAdvLastSave >= 1100) { AdvSave(); s_nAdvLastSave = now; }
}

// ---- 4.12.5: Settings > Display > HUD position (Advanced Settings): X / Y / size of every HUD item ----
struct AdvItem { const char* sect; const char* label; };
static const AdvItem kAdvItems[] = {
    { "HUD_Radar",       "Radar (map)" },
    { "HUD_WeaponIcon",  "Weapon switch (weapon icon)" },
    { "HUD_Ammo",        "Ammo" },
    { "HUD_BarHealth",   "Health bar" },
    { "HUD_BarArmor",    "Armor bar" },
    { "HUD_BarBreath",   "Breath bar" },
    { "HUD_Clock",       "Clock" },
    { "HUD_Money",       "Money" },
    { "HUD_MoneyChange", "Money change" },
    { "HUD_WantedStar",  "Wanted stars" },
    { "HUD_AreaName",    "Area name" },
    { "HUD_VehName",     "Vehicle name" },
    { "HUD_Radio",       "Radio" },
    { "HUD_Subtitles",   "Subtitles" },
    { "HUD_PlayerInfo",  "Player info" },
};
static std::vector<AdvSect> s_AdvBase;   // the original preset (the backup, or the file before the first change)
static char s_szAdvBasePath[512] = "";

static float AdvF(const AdvKey& k) { return (float)atof(k.val.c_str()); }
static std::string AdvFmt(float f)
{
    char b[32]; snprintf(b, sizeof(b), "%.2f", f);
    std::string r = b;   // 7.50 -> 7.5, 7.00 -> 7
    while(!r.empty() && r.back() == '0') r.pop_back();
    if(!r.empty() && r.back() == '.') r.pop_back();
    return r.empty() || r == "-" ? std::string("0") : r;
}
static const AdvKey* AdvBaseGet(const char* sect, const char* key)
{
    for(auto& s : s_AdvBase) if(s.name == sect) for(auto& k : s.keys) if(k.key == key) return &k;
    return nullptr;
}
static void AdvLoadBase()
{
    if(!strcmp(s_szAdvBasePath, s_Adv.path) && !s_AdvBase.empty()) return;
    snprintf(s_szAdvBasePath, sizeof(s_szAdvBasePath), "%s", s_Adv.path);
    s_AdvBase = s_Adv.sects;
    char bak[540]; snprintf(bak, sizeof(bak), "%s.mp_backup", s_Adv.path);
    std::vector<std::string> lines;
    if(!AdvReadLines(bak, lines)) return;
    // parse the backup into s_AdvBase without touching s_Adv
    auto saveLines = s_Adv.lines; auto saveSects = s_Adv.sects;
    char p0[64], v0[32]; snprintf(p0, sizeof(p0), "%s", s_Adv.preset); snprintf(v0, sizeof(v0), "%s", s_Adv.version);
    if(AdvParse(lines)) s_AdvBase = s_Adv.sects;
    s_Adv.lines.swap(saveLines); s_Adv.sects.swap(saveSects);
    snprintf(s_Adv.preset, sizeof(s_Adv.preset), "%s", p0); snprintf(s_Adv.version, sizeof(s_Adv.version), "%s", v0);
}
// a key the preset doesn't have: add "Key = value" at the end of its section (the mod uses its default otherwise)
static AdvKey* AdvEnsure(const char* sect, const char* key, const char* def)
{
    if(AdvKey* k = AdvGet(sect, key)) return k;
    int at = -1;
    for(int i = 0; i < (int)s_Adv.lines.size(); ++i)
    {
        std::string t = AdvTrim(s_Adv.lines[i]);
        if(t.size() > 2 && t[0] == '[') { if(at >= 0) break; const size_t sl = strlen(sect); if(t.size() > sl + 1 && t.compare(1, sl, sect) == 0 && t[1 + sl] == ']') at = i + 1; continue; }
        if(at >= 0 && !t.empty()) at = i + 1;
    }
    if(at < 0) return nullptr;
    s_Adv.lines.insert(s_Adv.lines.begin() + at, std::string(key) + " = " + def);
    AdvParse(s_Adv.lines);
    s_Adv.dirty = true; s_Adv.saveAt = MP_NowMs() + 400;
    return AdvGet(sect, key);
}
// slider + [-] [+] (fine steps are hard with a finger)
static bool AdvSlider(const char* label, float* v, float mn, float mx, float step, const char* fmt)
{
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    const float bw = ImGui::GetFrameHeight() * 1.6f, sp = ImGui::GetStyle().ItemSpacing.x;
    bool ch = false;
    ImGui::SetNextItemWidth(ImMax(60.0f, ImGui::GetContentRegionAvail().x - 2 * (bw + sp)));
    ch |= ImGui::SliderFloat("##s", v, mn, mx, fmt);
    ImGui::SameLine(); if(ImGui::Button("-", ImVec2(bw, 0))) { *v = ImMax(mn, *v - step); ch = true; }
    ImGui::SameLine(); if(ImGui::Button("+", ImVec2(bw, 0))) { *v = ImMin(mx, *v + step); ch = true; }
    ImGui::PopID();
    return ch;
}

static void AdvHudPositionUI()
{
    AdvFind(false);
    if(!s_Adv.found)
    {
        ImGui::TextWrapped("ARM's Advanced Settings (libAdvancedSettings.SA.so) is not installed or has no HUD_* preset - the normal GTA HUD is used.");
        if(FloatMenu::BigButton("Look again##advfind")) AdvFind(true);
        return;
    }
    {   // the file changed outside MP (the mod's own menu, a new preset): read it again
        static uint64_t s_nChk = 0; static time_t s_tSeen = 0; static char s_szSeen[512] = "";
        const uint64_t now = MP_NowMs();
        if(!s_Adv.dirty && now - s_nChk > 2000)
        {
            s_nChk = now; struct stat st;
            if(stat(s_Adv.path, &st) == 0)
            {
                if(strcmp(s_szSeen, s_Adv.path)) { snprintf(s_szSeen, sizeof(s_szSeen), "%s", s_Adv.path); s_tSeen = st.st_mtime; }
                else if(st.st_mtime != s_tSeen) { s_tSeen = st.st_mtime; AdvLoad(s_Adv.path); s_szAdvBasePath[0] = 0; }
            }
        }
    }
    AdvLoadBase();
    ImGui::TextWrapped("Advanced Settings %s%s%s. X = left - right, Y = top - bottom (%% of the screen), Size = %% of the preset. Changes show at once.",
                       s_Adv.version, s_Adv.preset[0] ? ", preset by " : "", s_Adv.preset);
    ImGui::TextDisabled("%s", s_Adv.path);
    {
        char bak[540]; snprintf(bak, sizeof(bak), "%s.mp_backup", s_Adv.path);
        const bool has = access(bak, F_OK) == 0;
        if(!has) ImGui::BeginDisabled();
        if(FloatMenu::BigButton("Restore the original preset##advrest"))
        {
            const bool ok = AdvRestore();
            if(ok) unlink(bak);
            s_szAdvBasePath[0] = 0; s_AdvBase.clear();
            SysMsg(COL_SYSTEM, ok ? "HUD position: original preset restored." : "HUD position: no backup to restore.");
        }
        if(!has) ImGui::EndDisabled();
        ImGui::TextDisabled(has ? "MP kept a copy of your original preset (the first change made it)." : "Nothing changed yet - the first change keeps a copy of your preset.");
    }
    for(const AdvItem& it : kAdvItems)
    {
        bool present = false;
        for(auto& s : s_Adv.sects) if(s.name == it.sect) { present = true; break; }
        if(!present) continue;
        ImGui::PushID(it.sect);
        if(ImGui::CollapsingHeader(it.label))
        {
            const bool radar = !strcmp(it.sect, "HUD_Radar");
            // the radar's own defaults (from the mod): PosX 7, PosY 14, SizeX 5.5, SizeY 12.5
            if(radar)
            {   // inserting a line re-reads the sections: add the missing keys first, then take the pointers
                AdvEnsure(it.sect, "PosX", "7"); AdvEnsure(it.sect, "PosY", "14");
                AdvEnsure(it.sect, "SizeX", "5.5"); AdvEnsure(it.sect, "SizeY", "12.5");
            }
            AdvKey* kx = AdvGet(it.sect, "PosX");  AdvKey* ky = AdvGet(it.sect, "PosY");
            AdvKey* sx = AdvGet(it.sect, "SizeX"); AdvKey* sy = AdvGet(it.sect, "SizeY");
            if(kx) { float f = AdvF(*kx); if(AdvSlider("X (left - right %)", &f, 0.0f, 100.0f, 0.5f, "%.1f")) AdvSet(*kx, AdvFmt(f)); }
            if(ky) { float f = AdvF(*ky); if(AdvSlider("Y (top - bottom %)", &f, 0.0f, 100.0f, 0.5f, "%.1f")) AdvSet(*ky, AdvFmt(f)); }
            if(sx || sy)
            {   // Size (z): one slider for both, relative to the original preset
                const AdvKey* bx = AdvBaseGet(it.sect, "SizeX"); const AdvKey* by = AdvBaseGet(it.sect, "SizeY");
                const float b0 = sx ? (bx ? AdvF(*bx) : AdvF(*sx)) : 0.0f, b1 = sy ? (by ? AdvF(*by) : AdvF(*sy)) : 0.0f;
                float pct = 100.0f;
                if(sx && b0 > 0.001f) pct = AdvF(*sx) / b0 * 100.0f; else if(sy && b1 > 0.001f) pct = AdvF(*sy) / b1 * 100.0f;
                if(AdvSlider("Size (% of the preset)", &pct, 25.0f, 300.0f, 5.0f, "%.0f %%"))
                {
                    if(sx && b0 > 0.001f) AdvSet(*sx, AdvFmt(b0 * pct / 100.0f));
                    if(sy && b1 > 0.001f) AdvSet(*sy, AdvFmt(b1 * pct / 100.0f));
                }
            }
            if(radar) if(AdvKey* r = AdvGet(it.sect, "Range")) { float f = AdvF(*r); if(AdvSlider("Zoom (range)", &f, 0.0f, ImMax(f * 2.0f, 400.0f), 5.0f, "%.0f")) AdvSet(*r, AdvFmt(f)); }
            const AdvKey* ox = AdvBaseGet(it.sect, "PosX"); const AdvKey* oy = AdvBaseGet(it.sect, "PosY");
            if(ox || oy) ImGui::TextDisabled("Preset: X %s  Y %s", ox ? ox->val.c_str() : "-", oy ? oy->val.c_str() : "-");
            if(FloatMenu::BigButton("Reset this item"))
            {
                for(const char* key : { "PosX", "PosY", "SizeX", "SizeY", "Range" })
                    if(const AdvKey* b = AdvBaseGet(it.sect, key)) if(AdvKey* k = AdvGet(it.sect, key)) AdvSet(*k, b->val);
            }
            if(!kx && !ky && !sx && !sy) ImGui::TextDisabled("This preset has no position keys for it.");
        }
        ImGui::PopID();
    }
}
