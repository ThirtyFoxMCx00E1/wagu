// 4.07: the server's HUD ("Textdraw" in /settings), the same for everyone on that server, chosen by its
// host owner (featured servers: [ADMIN] / [MOD]):
//   HUD Mode     HUD Variant 1..5 (colour of the right side panel + the hexagons) or Disable
//                1 = Mega Vil blue (default), 2 = violet (the old look), 3 = emerald, 4 = crimson, 5 = gold
//   Dollar Cent  money as $525,250.00 instead of $525250
//   HUD Player   your name + id under the game's money / health (RP: the ID row)
//   Ammo Info    the ammo of the weapon in your hand under it
//   Logo Server  the server's title logo on top of the screen (+ Add server > Server title logo)
// and Entering Door: how long you stand in a house door before its menu opens.

struct HudCfg { uint8_t mode = 1, flags = HUD_DEFAULT_FLAGS, door = 2; uint8_t lx = 50, ly = 2, lz = 100; bool have = false; };   // l*: title logo X / Y %, Z size %
static HudCfg s_Hud;

static void HudReset() { s_Hud = HudCfg(); }
static bool HudOn() { return s_Hud.mode != 0; }
static bool HudFlag(uint8_t f) { return (s_Hud.flags & f) != 0; }
static int  HudDoorMs() { return s_Hud.door * 1000; }

struct HudPal { ImU32 fill, border, accent; const char* name; };
static const HudPal kHudPal[6] = {
    { IM_COL32( 10,  40,  90, 235), IM_COL32( 56, 189, 248, 255), IM_COL32(125, 211, 252, 255), "Disabled" },
    { IM_COL32( 10,  40,  90, 235), IM_COL32( 56, 189, 248, 255), IM_COL32(125, 211, 252, 255), "Mega Vil blue" },
    { IM_COL32(110,  24, 150, 235), IM_COL32(200, 110, 210, 255), IM_COL32(216, 180, 254, 255), "Violet" },
    { IM_COL32(  6,  78,  59, 235), IM_COL32( 52, 211, 153, 255), IM_COL32(110, 231, 183, 255), "Emerald" },
    { IM_COL32(127,  29,  29, 235), IM_COL32(248, 113, 113, 255), IM_COL32(252, 165, 165, 255), "Crimson" },
    { IM_COL32(113,  63,  18, 235), IM_COL32(250, 204,  21, 255), IM_COL32(253, 224,  71, 255), "Gold" },
};
static const HudPal& HudPalette() { return kHudPal[s_Hud.mode <= 5 ? s_Hud.mode : 1]; }

// "$525250" or (Dollar Cent) "$525,250.00"
static void HudMoney(char* out, size_t n, int v)
{
    if(!HudFlag(HUDF_CENTS)) { snprintf(out, n, "$%d", v); return; }
    char d[24]; snprintf(d, sizeof(d), "%d", v < 0 ? -v : v);
    char g[40]; int len = (int)strlen(d), o = 0;
    for(int i = 0; i < len; ++i) { if(i && (len - i) % 3 == 0) g[o++] = ','; g[o++] = d[i]; }
    g[o] = 0;
    snprintf(out, n, "%s$%s.00", v < 0 ? "-" : "", g);
}

static void HudOnConfig(const PktSrvSettings2& h)
{
    s_Hud.mode = h.hudMode <= 5 ? h.hudMode : 1; s_Hud.flags = h.hudFlags; s_Hud.door = h.doorSecs <= 10 ? h.doorSecs : 2; s_Hud.have = true;
    if(h.logoValid == 1) { s_Hud.lx = h.logoX <= 100 ? h.logoX : 50; s_Hud.ly = h.logoY <= 100 ? h.logoY : 2; s_Hud.lz = (h.logoZ >= 10 && h.logoZ <= 250) ? h.logoZ : 100; }
    // the host phone keeps its choice for the next start of its server
    if(s_Server.IsRunning() && !MP::IsHostingDefault() && client.GetPort() == s_Server.GetPort() && h.b.can)
    {
        if(settings.hudMode != s_Hud.mode || settings.hudFlags != s_Hud.flags || settings.hudDoor != s_Hud.door
           || settings.logoX != s_Hud.lx || settings.logoY != s_Hud.ly || settings.logoZ != s_Hud.lz)
        { settings.hudMode = s_Hud.mode; settings.hudFlags = s_Hud.flags; settings.hudDoor = s_Hud.door; settings.logoX = s_Hud.lx; settings.logoY = s_Hud.ly; settings.logoZ = s_Hud.lz; SaveConfig(); }
    }
}

// ---- the server's title logo (+ Add server > Server title logo, g/<token> in the lobby) ----------
static uint32_t HudServerToken()
{
    if(client.GetState() != NET_CONNECTED) return 0;
    static uint64_t at = 0; static uint32_t tok = 0; static char key[64] = "";
    char k[64]; snprintf(k, sizeof(k), "%s|%s|%u", client.GetHost() ? client.GetHost() : "", client.GetServerName(), (unsigned)client.GetPort());
    uint64_t now = MP_NowMs();
    if(strcmp(k, key) || now - at > 5000)
    {
        at = now; MP_CopyStr(key, k, sizeof(key)); tok = 0;
        const char* h = client.GetHost();
        for(auto& sv : Online::Snapshot())
        {
            if(h && !strcmp(sv.ip, h) && sv.port == client.GetPort()) { tok = sv.token; break; }
            if(!tok && sv.port == client.GetPort() && !strcasecmp(sv.name, client.GetServerName())) tok = sv.token;
        }
    }
    return tok;
}
static const char* HudLogoFile()   // file in files/MP, "" = none
{
    // the host's own server: the picked file straight away
    if(s_Server.IsRunning() && client.GetPort() == s_Server.GetPort() && settings.serverLogo[0]) return settings.serverLogo;
    uint32_t tok = HudServerToken();
    if(!tok) return "";
    static std::map<uint32_t, std::pair<uint64_t, std::string>> s_Lg;
    auto& c = s_Lg[tok];
    uint64_t now = MP_NowMs();
    if(!c.first || now - c.first > 5000)
    {
        c.first = now;
        std::string img;
        if(!Online::Logo(tok, img)) c.second.clear();
        else
        {
            uint32_t h = 2166136261u; for(unsigned char ch : img) h = (h ^ ch) * 16777619u;
            const bool png = img.size() > 4 && (uint8_t)img[0] == 0x89;
            char fn[48]; snprintf(fn, sizeof(fn), "lg_%08x_%08x.%s", tok, h, png ? "png" : "jpg");
            if(c.second != fn)
            {
                char pp[512]; snprintf(pp, sizeof(pp), "%s/%s", s_szDataDir, fn);
                FILE* f = fopen(pp, "rb");
                if(f) fclose(f);
                else if((f = fopen(pp, "wb"))) { fwrite(img.data(), 1, img.size(), f); fclose(f); }
                c.second = fn;
            }
        }
    }
    return c.second.c_str();
}
// true = a custom logo was drawn
static bool HudDrawServerLogo(ImDrawList* dl)
{
    if(!HudFlag(HUDF_LOGO) || client.GetState() != NET_CONNECTED) return false;
    const char* f = HudLogoFile();
    if(!f[0]) return false;
    ImVec2 uv(1, 1);
    void* tex = HUD::Texture(f, &uv);
    if(!tex) return false;
    int tw = 0, th = 0;
    if(!HUD::TextureSize(f, &tw, &th) || tw <= 0 || th <= 0) { tw = 512; th = 160; }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    // 4.07: X / Y = where its centre / top is (% of the screen), Z = size (% of 30 % x 13 % of the screen)
    const float z = s_Hud.lz / 100.0f;
    float maxW = ds.x * 0.30f * z, maxH = ds.y * 0.13f * z;
    float s = fminf(maxW / tw, maxH / th);
    float w = tw * s, h = th * s;
    float x = ds.x * s_Hud.lx / 100.0f - w * 0.5f, y = ds.y * s_Hud.ly / 100.0f;
    if(x < 0) x = 0; if(x + w > ds.x) x = ds.x - w; if(y + h > ds.y) y = ds.y - h; if(y < 0) y = 0;
    dl->AddImage((ImTextureID)tex, ImVec2(x, y), ImVec2(x + w, y + h), ImVec2(0, 0), uv);
    return true;
}

// ---- HUD Player + Ammo Info (every server except the ones with their own full HUD) ----------------
static void HudRenderExtras()
{
    if(client.GetState() != NET_CONNECTED || !Game::IsReady() || Game::IsPaused() || !myDrawValid || !HudOn()) return;
    if(DzOnServer() || Mw3OnServer() || BcwOnServer() || MP::AuthScreenShown()) return;
    int ped = Game::LocalPed(); if(ped <= 0) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const HudPal& pal = HudPalette();
    float x1 = ds.x - UI::S(14), y = UI::S(196);
    char t[96];
    if(HudFlag(HUDF_AMMO))
    {
        int w = Game::GetCharWeapon(ped);
        if(w >= 16 && w != 40 && w != 44 && w != 45 && w != 46)   // guns / throwables (not fists, melee, detonator, goggles, parachute)
        {
            int a = Game::GetAmmo(ped, w);
            snprintf(t, sizeof(t), "AMMO %d", a);
            float fs = UI::S(20), tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
            const float ay = RpOnServer() ? UI::S(180) : y + (HudFlag(HUDF_PLAYER) ? UI::S(40) : 0.0f);
            UI::OutlinedText(dl, fs, ImVec2(x1 - tw, ay), pal.accent, t);
        }
    }
    if(HudFlag(HUDF_PLAYER) && !RpOnServer() && myId < MP_MAX_PLAYERS)
    {
        snprintf(t, sizeof(t), "%s (%d)", settings.name, (int)myId);
        float fs = UI::S(22), tw = ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0, t).x;
        float pad = UI::S(8);
        // 4.08: only the text (no coloured box / border behind the name), like the game's own HUD text
        UI::OutlinedText(dl, fs, ImVec2(x1 - tw, y + pad * 0.5f), IM_COL32_WHITE, t);
    }
}
