// 4.13.15: MP no longer touches the HUD at all. The Advanced Settings bridge (it wrote the HUD_* positions
// into ARM's AdvancedSettings ini: the "HUD position" page and the one-time radar move) was removed.
// This only runs ONCE: every "<ini>.mp_backup" an older MP left in the AML config folder is the player's own
// original preset - it is copied back over the ini and the backup / temp files are deleted.
static int s_nDzPackMoved = 0;

static bool HudCopyFile(const char* from, const char* to)
{
    FILE* in = fopen(from, "rb"); if(!in) return false;
    char tmp[620]; snprintf(tmp, sizeof(tmp), "%s.mp_tmp", to);
    FILE* out = fopen(tmp, "wb"); if(!out) { fclose(in); return false; }
    char b[4096]; size_t r; bool ok = true;
    while((r = fread(b, 1, sizeof(b), in)) > 0) if(fwrite(b, 1, r, out) != r) { ok = false; break; }
    fclose(in); fclose(out);
    if(!ok) { unlink(tmp); return false; }
    return rename(tmp, to) == 0;
}

static void HudRestoreOriginalPresets()
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
        char p[600], bak[620];
        snprintf(p, sizeof(p), "%s%s%s", dir, sep, ini.c_str()); snprintf(bak, sizeof(bak), "%s.mp_backup", p);
        if(HudCopyFile(bak, p)) { unlink(bak); logger->Info("4.13.15: HUD preset: original restored (%s)", p); }
    }
}

static void HudTick()
{
    static int s_nState = 0; static uint64_t s_nAt = 0;
    const uint64_t now = MP_NowMs();
    if(s_nState == 0) { s_nState = 1; s_nAt = now + 3000; return; }
    if(s_nState != 1 || now < s_nAt) return;
    s_nState = 2;
    if(!::cfg->GetBool("HudRestoreOnce41315", false, "MP"))
    {
        HudRestoreOriginalPresets();
        ConfigEntry* e = ::cfg->Bind("HudRestoreOnce41315", true, "MP"); e->SetBool(true); delete e; ::cfg->Save();
    }
    if(s_nDzPackMoved > 0) SysMsg(COL_YELLOW, "Day Z model pack moved into the MP folder. Restart the game once so the normal models come back.");
}
