#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <mutex>
#include <vector>
// In-game HUD for MP, drawn with Dear ImGui through the RenderWare backend.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <math.h>
#include <string.h>
#include <vector>
#include "hud.h"
#include "ui_common.h"
#include "floatmenu.h"
#include "vkeyboard.h"
#include "../mp.h"
#include "../net/server.h"
#include "../../imgui/rw_glue.h"
#include "../../imgui/arial.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_FAILURE_STRINGS
#include "../../imgui/stb_image.h"
#include "emoji.h"

// ---- timings for the connected logo -------------------------------------------------
#define LOGO_TOTAL_MS     7000   // logo is gone at 7 s
#define LOGO_TICK_MS      50     // 1 tick = 50 ms (20 ticks per second)
#define LOGO_FADE_TICKS   20     // fade-off lasts 20 ticks (1 s): 6.0 s -> 7.0 s

void ImGui_ImplRenderWare_RenderDrawData(ImDrawData* draw_data);
bool ImGui_ImplRenderWare_Init();
void ImGui_ImplRenderWare_NewFrame();

static ImFont*   s_pFont = nullptr;
static ImFont*   s_pPricedown = nullptr;   // 3.00
static char      s_szPricedown[512] = "";
static RwRaster* s_pLogo = nullptr;
static ImVec2    s_LogoSize(0, 0);
static ImVec2    s_LogoUV(1, 1);
static bool      s_bLogoTried = false;
static bool      s_bInit = false;
static float     s_fScale = 1.0f;

float   UI::S(float px) { return px * s_fScale; }
ImFont* UI::Font()      { return s_pFont ? s_pFont : ImGui::GetFont(); }
ImFont* UI::PricedownFont() { return s_pPricedown; }
const char* UI::PricedownPath() { return s_szPricedown; }

// 3.00: Pricedown for the score bar. MP doesn't ship the font (its free licence doesn't cover games /
// apps); it uses a pricedown .ttf already on the phone: the MP folder first, then other mods' folders
// (SA-MP style HUD mods often bring one). TrueType only - a CFF .otf ("OTTO") can't be read by ImGui.
static bool IsPricedownName(const char* n)
{
    size_t l = strlen(n);
    if(l < 5 || strcasecmp(n + l - 4, ".ttf") != 0) return false;
    char low[256]; size_t i = 0; for(; n[i] && i < sizeof(low) - 1; ++i) low[i] = (char)tolower((unsigned char)n[i]); low[i] = 0;
    return strstr(low, "pricedow") != nullptr;
}
static bool FindPricedownIn(const char* dir, int depth, char* out, size_t cap)
{
    if(!dir || !*dir) return false;
    DIR* d = opendir(dir);
    if(!d) return false;
    char sub[16][512]; int nsub = 0; bool found = false;
    while(dirent* e = readdir(d))
    {
        if(e->d_name[0] == '.') continue;
        if(IsPricedownName(e->d_name)) { snprintf(out, cap, "%s/%s", dir, e->d_name); found = true; break; }
        if(depth > 0 && nsub < 16 && e->d_type == DT_DIR) snprintf(sub[nsub++], 512, "%s/%s", dir, e->d_name);
    }
    closedir(d);
    for(int i = 0; !found && i < nsub; ++i) found = FindPricedownIn(sub[i], depth - 1, out, cap);
    return found;
}
static ImFont* LoadPricedown(ImFontAtlas* atlas, float size)
{
    char path[512] = "";
    char dirs[6][512]; int n = 0;
    snprintf(dirs[n++], 512, "%s", MP::DataDir());
    if(aml)
    {
        snprintf(dirs[n++], 512, "%s/mods", aml->GetAndroidDataRootPath());
        snprintf(dirs[n++], 512, "%s", aml->GetAndroidDataPath());
        snprintf(dirs[n++], 512, "%s/files", aml->GetAndroidDataRootPath());
    }
    for(int i = 0; i < n && !path[0]; ++i) FindPricedownIn(dirs[i], i == 0 ? 0 : 2, path, sizeof(path));
    if(!path[0]) { logger->Info("3.00: no pricedown .ttf found (put pricedown.ttf in the MP folder) - score bar uses the game font / Arial"); return nullptr; }
    FILE* f = fopen(path, "rb");
    if(!f) return nullptr;
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    if(len < 1024 || len > 8 * 1024 * 1024) { fclose(f); return nullptr; }
    unsigned char* buf = (unsigned char*)IM_ALLOC((size_t)len);
    bool ok = fread(buf, 1, (size_t)len, f) == (size_t)len; fclose(f);
    // TrueType outlines only: 00 01 00 00 or 'true' (a CFF 'OTTO' font would fail the atlas build)
    if(!ok || !((buf[0] == 0 && buf[1] == 1 && buf[2] == 0 && buf[3] == 0) || !memcmp(buf, "true", 4)))
    { logger->Info("3.00: %s is not a TrueType font - skipped", path); IM_FREE(buf); return nullptr; }
    static const ImWchar ranges[] = { 0x0020, 0x007E, 0 };
    ImFontConfig fc; fc.FontDataOwnedByAtlas = true; fc.OversampleH = 2; fc.OversampleV = 2;
    ImFont* font = atlas->AddFontFromMemoryTTF(buf, (int)len, size, &fc, ranges);
    if(font) { snprintf(s_szPricedown, sizeof(s_szPricedown), "%s", path); logger->Info("3.00: Pricedown loaded from %s", path); }
    return font;
}

void UI::OutlinedText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float alphaMul)
{
    ImFont* f = Font();
    const float o = fmaxf(1.0f, size * 0.06f);
    ImU32 black = IM_COL32(0, 0, 0, (int)(255 * alphaMul));
    dl->AddText(f, size, ImVec2(p.x - o, p.y), black, text);
    dl->AddText(f, size, ImVec2(p.x + o, p.y), black, text);
    dl->AddText(f, size, ImVec2(p.x, p.y - o), black, text);
    dl->AddText(f, size, ImVec2(p.x, p.y + o), black, text);
    dl->AddText(f, size, p, col, text);
}

// 2.37: UI::IsAdminName / UI::NameText (RGB admin rank + flowing name) are in nametext.cpp

bool HUD::IsInitialised() { return s_bInit; }

bool HUD::Init()
{
    if(s_bInit) return true;
    if(!RsGlobal || RsGlobal->maximumHeight <= 0) return false;

    s_fScale = RsGlobal->maximumHeight / 1080.0f;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_IsTouchScreen;
    ImGui_ImplRenderWare_Init();

    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0400, 0x045F, 0x2010, 0x2040, 0 };
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 2; fc.OversampleV = 2;
    s_pFont = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), UI::S(34.0f), &fc, ranges);
    io.FontDefault = s_pFont;
    // 2.19: coloured emoji glyphs (chat / keyboard)
    UI::EmojiAddGlyphs(io.Fonts, s_pFont, UI::S(34.0f));
    s_pPricedown = LoadPricedown(io.Fonts, UI::S(40.0f));   // 3.00: score bar
    io.Fonts->Build();
    UI::EmojiBake(io.Fonts, s_pFont);

    FloatMenu::ApplyStyle();
    s_bInit = true;
    logger->Info("ImGui initialised (%dx%d)", RsGlobal->maximumWidth, RsGlobal->maximumHeight);
    return true;
}

// ---- PNG -> RwRaster -----------------------------------------------------------------
static int NextPow2(int v) { int p = 1; while(p < v) p <<= 1; return p; }

// The game's GL raster upload expects power-of-two images: a 896x531 PNG made it read past
// the end of the pixel buffer (crash in _rwOpenGLRasterSetImage on Connect, v1.2).
// So the PNG is copied into the top-left of a transparent power-of-two canvas and drawn
// with matching UVs.
static RwRaster* LoadPNGRaster(const char* path, int* outW, int* outH)
{
    int w, h, n;
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);
    if(!px) return nullptr;
    if(w <= 0 || h <= 0 || w > 2048 || h > 2048) { logger->Error("Logo size %dx%d not supported (max 2048)", w, h); stbi_image_free(px); return nullptr; }
    const int pw = NextPow2(w), ph = NextPow2(h);
    const int side = pw > ph ? pw : ph; // square canvas, like the font atlas
    RwImage* img = RwImageCreate(side, side, 32);
    if(!img) { stbi_image_free(px); return nullptr; }
    RwImageAllocatePixels(img);
    if(!img->cpPixels || img->stride < side * 4) { RwImageDestroy(img); stbi_image_free(px); return nullptr; }
    for(int y = 0; y < side; ++y)
    {
        RwUInt8* row = img->cpPixels + (size_t)y * img->stride;
        memset(row, 0, (size_t)img->stride);
        if(y < h) memcpy(row, px + (size_t)y * w * 4, (size_t)w * 4);
    }
    stbi_image_free(px);
    RwInt32 rw = 0, rh = 0, rd = 0, rf = 0;
    RwImageFindRasterFormat(img, 4 /* rwRASTERTYPETEXTURE */, &rw, &rh, &rd, &rf);
    if(rw != side || rh != side)
    {
        logger->Error("Logo raster format %dx%d != image %dx%d, skipping logo", rw, rh, side, side);
        RwImageDestroy(img);
        return nullptr;
    }
    RwRaster* r = RwRasterCreate(rw, rh, rd, rf);
    if(r) r = RwRasterSetFromImage(r, img);
    RwImageDestroy(img);
    *outW = w; *outH = h;
    s_LogoUV = ImVec2((float)w / side, (float)h / side);
    return r;
}

// 2.24: server icons. files/MP/main_df_icon.png = default icon, files/MP/icon_1.png .. icon_250.png =
// the icon a host picks in Server > Host. Loaded on first use (one new file per frame, max 256x256).
struct IconSlot { RwRaster* r = nullptr; ImVec2 uv{1, 1}; bool tried = false; };
static IconSlot s_Icons[MP_ICON_MAX + 1];
static uint32_t s_nIconLoadFrame = 0;
void* HUD::ServerIcon(int id, ImVec2* uv)
{
    if(id < 0 || id > MP_ICON_MAX) id = 0;
    IconSlot& s = s_Icons[id];
    if(!s.tried)
    {
        uint32_t fr = (uint32_t)ImGui::GetFrameCount();
        if(s_nIconLoadFrame == fr) return id ? ServerIcon(0, uv) : nullptr;   // next frame
        s_nIconLoadFrame = fr;
        s.tried = true;
        char path[512];
        if(id == 0) snprintf(path, sizeof(path), "%s/main_df_icon.png", MP::DataDir());
        else        snprintf(path, sizeof(path), "%s/icon_%d.png", MP::DataDir(), id);
        int w = 0, h = 0, n = 0;
        if(stbi_info(path, &w, &h, &n) && w > 0 && h > 0 && w <= 256 && h <= 256)
        {
            ImVec2 keep = s_LogoUV;
            s.r = LoadPNGRaster(path, &w, &h);
            s.uv = s_LogoUV; s_LogoUV = keep;
        }
        else if(w > 256 || h > 256) logger->Error("%s is %dx%d - server icons must be 256x256 or smaller", path, w, h);
    }
    if(!s.r) return id ? ServerIcon(0, uv) : nullptr;
    if(uv) *uv = s.uv;
    return (void*)s.r;
}
// 3.04: any other PNG in files/MP by file name (Brief Case War briefcase / lock / banner), max 512x512
struct NamedTex { char file[48]; RwRaster* r; ImVec2 uv; };
static NamedTex s_NamedTex[16]; static int s_nNamedTex = 0;
void* HUD::Texture(const char* file, ImVec2* uv)
{
    if(!file || !*file) return nullptr;
    for(int i = 0; i < s_nNamedTex; ++i)
        if(!strcmp(s_NamedTex[i].file, file)) { if(uv) *uv = s_NamedTex[i].uv; return (void*)s_NamedTex[i].r; }
    if(s_nNamedTex >= 16) return nullptr;
    uint32_t fr = (uint32_t)ImGui::GetFrameCount();
    if(s_nIconLoadFrame == fr) return nullptr;   // one file per frame
    s_nIconLoadFrame = fr;
    NamedTex& t = s_NamedTex[s_nNamedTex++];
    snprintf(t.file, sizeof(t.file), "%s", file); t.r = nullptr; t.uv = ImVec2(1, 1);
    char path[512]; snprintf(path, sizeof(path), "%s/%s", MP::DataDir(), file);
    int w = 0, h = 0, n = 0;
    if(stbi_info(path, &w, &h, &n) && w > 0 && h > 0 && w <= 512 && h <= 512)
    {
        ImVec2 keep = s_LogoUV;
        t.r = LoadPNGRaster(path, &w, &h);
        t.uv = s_LogoUV; s_LogoUV = keep;
    }
    else logger->Error("%s missing or bigger than 512x512", path);
    if(uv) *uv = t.uv;
    return (void*)t.r;
}

void HUD::ReloadServerIcons()
{
    for(auto& s : s_Icons) { if(s.r) RwRasterDestroy(s.r); s = IconSlot(); }
}

static void EnsureLogo()
{
    if(s_bLogoTried) return;
    s_bLogoTried = true;
    char path[512];
    snprintf(path, sizeof(path), "%s/title_logo_connected.png", MP::DataDir());
    int w = 0, h = 0;
    s_pLogo = LoadPNGRaster(path, &w, &h);
    if(s_pLogo) { s_LogoSize = ImVec2((float)w, (float)h); logger->Info("Loaded %s (%dx%d)", path, w, h); }
    else logger->Error("Could not load %s", path);
}

// ---- weapon icons (kill feed) ----------------------------------------------------------
static void DrawWeaponIcon(ImDrawList* dl, ImVec2 c, float s, uint8_t weapon)
{
    const ImU32 W = IM_COL32_WHITE;
    auto R = [&](float x0, float y0, float x1, float y1) { dl->AddRectFilled(ImVec2(c.x + x0 * s, c.y + y0 * s), ImVec2(c.x + x1 * s, c.y + y1 * s), W); };
    auto L = [&](float x0, float y0, float x1, float y1, float t) { dl->AddLine(ImVec2(c.x + x0 * s, c.y + y0 * s), ImVec2(c.x + x1 * s, c.y + y1 * s), W, t * s); };

    if(weapon == 0 || weapon == 1)                 // fist
    {
        dl->AddCircleFilled(ImVec2(c.x, c.y - 0.05f * s), 0.28f * s, W, 16);
        R(-0.22f, 0.05f, 0.22f, 0.32f);
    }
    else if(weapon <= 15)                           // melee
    {
        L(-0.35f, 0.35f, 0.25f, -0.25f, 0.10f);
        L(-0.12f, 0.02f, 0.02f, 0.16f, 0.08f);
        L(0.25f, -0.25f, 0.38f, -0.38f, 0.06f);
    }
    else if(weapon >= 16 && weapon <= 18 || weapon == 39 || weapon == 51) // explosives
    {
        dl->AddCircleFilled(c, 0.22f * s, W, 16);
        for(int i = 0; i < 8; ++i) { float a = i * 0.785f; L(cosf(a) * 0.22f, sinf(a) * 0.22f, cosf(a) * 0.40f, sinf(a) * 0.40f, 0.07f); }
    }
    else if(weapon >= 22 && weapon <= 24)           // pistols
    {
        R(-0.34f, -0.18f, 0.34f, -0.04f);
        dl->AddQuadFilled(ImVec2(c.x - 0.30f * s, c.y - 0.05f * s), ImVec2(c.x - 0.10f * s, c.y - 0.05f * s),
                          ImVec2(c.x - 0.16f * s, c.y + 0.32f * s), ImVec2(c.x - 0.36f * s, c.y + 0.30f * s), W);
    }
    else if(weapon == 28 || weapon == 29 || weapon == 32) // SMGs
    {
        R(-0.38f, -0.16f, 0.38f, -0.02f);
        R(-0.12f, -0.02f, 0.00f, 0.30f);
        R(-0.34f, -0.02f, -0.22f, 0.18f);
    }
    else if(weapon >= 25 && weapon <= 34)           // shotguns / rifles / sniper
    {
        R(-0.44f, -0.10f, 0.44f, 0.00f);
        dl->AddQuadFilled(ImVec2(c.x - 0.44f * s, c.y - 0.10f * s), ImVec2(c.x - 0.18f * s, c.y - 0.10f * s),
                          ImVec2(c.x - 0.22f * s, c.y + 0.16f * s), ImVec2(c.x - 0.44f * s, c.y + 0.24f * s), W);
        if(weapon == 30 || weapon == 31) R(0.00f, 0.00f, 0.10f, 0.26f);     // magazine
        if(weapon == 33 || weapon == 34) R(-0.10f, -0.24f, 0.16f, -0.14f);  // scope
    }
    else if(weapon == 35 || weapon == 36)           // rocket launchers
    {
        R(-0.42f, -0.10f, 0.30f, 0.06f);
        dl->AddTriangleFilled(ImVec2(c.x + 0.30f * s, c.y - 0.16f * s), ImVec2(c.x + 0.46f * s, c.y - 0.02f * s), ImVec2(c.x + 0.30f * s, c.y + 0.12f * s), W);
    }
    else if(weapon == 37)                           // flamethrower
    {
        dl->AddTriangleFilled(ImVec2(c.x, c.y - 0.38f * s), ImVec2(c.x + 0.26f * s, c.y + 0.30f * s), ImVec2(c.x - 0.26f * s, c.y + 0.30f * s), IM_COL32(255, 140, 20, 255));
    }
    else if(weapon == 38)                           // minigun
    {
        for(int i = 0; i < 3; ++i) R(-0.42f, -0.16f + i * 0.10f, 0.42f, -0.10f + i * 0.10f);
        R(-0.10f, 0.14f, 0.06f, 0.32f);
    }
    else if(weapon == 49)                           // 2.37: car hit - a car ramming a player (impact burst)
    {
        auto P = [&](float x, float y) { return ImVec2(c.x + x * s, c.y + y * s); };
        // car (left), driving to the right
        R(-0.46f, 0.02f, 0.10f, 0.20f);                                                   // body
        dl->AddQuadFilled(P(-0.32f, 0.02f), P(-0.20f, -0.14f), P(-0.02f, -0.14f), P(0.06f, 0.02f), W);   // cabin
        dl->AddQuadFilled(P(-0.17f, -0.03f), P(-0.12f, -0.10f), P(-0.05f, -0.10f), P(-0.02f, -0.03f), IM_COL32(0, 0, 0, 200));   // window
        dl->AddCircleFilled(P(-0.32f, 0.22f), 0.075f * s, W, 10);
        dl->AddCircleFilled(P(-0.02f, 0.22f), 0.075f * s, W, 10);
        dl->AddCircleFilled(P(-0.32f, 0.22f), 0.03f * s, IM_COL32(0, 0, 0, 220), 8);
        dl->AddCircleFilled(P(-0.02f, 0.22f), 0.03f * s, IM_COL32(0, 0, 0, 220), 8);
        // speed lines behind the car
        L(-0.52f, -0.04f, -0.40f, -0.04f, 0.035f); L(-0.54f, 0.08f, -0.46f, 0.08f, 0.035f);
        // impact burst at the bumper
        const ImU32 O = IM_COL32(255, 150, 30, 255);
        for(int i = 0; i < 7; ++i)
        {
            float a = -1.4f + i * 0.47f;
            dl->AddLine(P(0.16f + cosf(a) * 0.04f, 0.06f + sinf(a) * 0.04f), P(0.16f + cosf(a) * 0.13f, 0.06f + sinf(a) * 0.13f), O, 0.04f * s);
        }
        // player knocked away (right), tilted back
        dl->AddCircleFilled(P(0.38f, -0.24f), 0.065f * s, W, 12);                       // head
        L(0.35f, -0.17f, 0.28f, 0.04f, 0.06f);                                          // body
        L(0.34f, -0.12f, 0.44f, -0.20f, 0.045f); L(0.33f, -0.10f, 0.22f, -0.20f, 0.045f);   // arms up
        L(0.28f, 0.04f, 0.40f, 0.14f, 0.05f);  L(0.28f, 0.04f, 0.24f, 0.18f, 0.05f);    // legs
    }
    else if(weapon == 50)                           // heli blades
    {
        L(-0.42f, -0.20f, 0.42f, -0.20f, 0.06f);
        R(-0.03f, -0.20f, 0.03f, -0.08f);
        dl->AddCircleFilled(ImVec2(c.x, c.y + 0.04f * s), 0.16f * s, W, 14);
        L(0.12f, 0.04f, 0.42f, 0.02f, 0.06f);
    }
    else if(weapon == 54)                           // fall
    {
        L(0, -0.36f, 0, 0.20f, 0.10f);
        dl->AddTriangleFilled(ImVec2(c.x - 0.20f * s, c.y + 0.10f * s), ImVec2(c.x + 0.20f * s, c.y + 0.10f * s), ImVec2(c.x, c.y + 0.38f * s), W);
    }
    else                                            // skull: suicide / unknown / drown
    {
        dl->AddCircleFilled(ImVec2(c.x, c.y - 0.06f * s), 0.30f * s, W, 20);
        R(-0.17f, 0.14f, 0.17f, 0.34f);
        ImU32 B = IM_COL32(0, 0, 0, 255);
        dl->AddCircleFilled(ImVec2(c.x - 0.12f * s, c.y - 0.06f * s), 0.08f * s, B, 10);
        dl->AddCircleFilled(ImVec2(c.x + 0.12f * s, c.y - 0.06f * s), 0.08f * s, B, 10);
        dl->AddLine(ImVec2(c.x - 0.06f * s, c.y + 0.26f * s), ImVec2(c.x - 0.06f * s, c.y + 0.34f * s), B, 0.04f * s);
        dl->AddLine(ImVec2(c.x + 0.06f * s, c.y + 0.26f * s), ImVec2(c.x + 0.06f * s, c.y + 0.34f * s), B, 0.04f * s);
    }
}

// ---- name tag + health bar --------------------------------------------------------------
static void DrawNameTag(ImDrawList* dl, const Vec3& head, const char* name, uint32_t rgba, int health, int armour, const Vec3& camRef, bool paused = false)
{
    float dx = head.x - camRef.x, dy = head.y - camRef.y, dz = head.z - camRef.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    if(dist > MP::settings.nametagDistance && !MP_WallhackOn()) return;   // 2.19 /wallhack: any distance

    Vec3 top = head; top.z += 0.30f;
    float sx, sy, depth;
    if(!Game::WorldToScreen(top, sx, sy, depth)) return;

    const float k = MP::settings.nametagSize;
    const float fs = UI::S(26.0f) * k;
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, name);

    // name on top
    float y = sy - fs - UI::S(4) * k;
    // AdminTagLift is 0 since 3.00 (no dragon), kept so the tag position has one place to change
    const float lift = UI::IsAdminName(name) ? UI::AdminTagLift(fs) : 0.0f;
    y -= lift;
    if(paused)
    {
        const float pfs = fs * 0.8f;
        ImVec2 pts = UI::Font()->CalcTextSizeA(pfs, FLT_MAX, 0, "PAUSED");
        UI::OutlinedText(dl, pfs, ImVec2(sx - pts.x * 0.5f, y - pfs - UI::S(2) * k), IM_COL32(255, 210, 60, 255), "PAUSED");
    }
    UI::NameText(dl, fs, ImVec2(sx - ts.x * 0.5f, y), UI::FromRGBA(rgba | 0xFF), name);   // 2.37
    y += fs + UI::S(3) * k + lift;

    if(!MP::settings.showHealthBars) return;

    const float bw = UI::S(68.0f) * k, bh = UI::S(9.0f) * k, b = fmaxf(1.0f, UI::S(2.0f) * k);
    auto Bar = [&](float frac, ImU32 bg, ImU32 fg)
    {
        if(frac < 0) frac = 0; if(frac > 1) frac = 1;
        ImVec2 p0(sx - bw * 0.5f, y), p1(sx + bw * 0.5f, y + bh);
        dl->AddRectFilled(ImVec2(p0.x - b, p0.y - b), ImVec2(p1.x + b, p1.y + b), IM_COL32(0, 0, 0, 255));
        dl->AddRectFilled(p0, p1, bg);
        dl->AddRectFilled(p0, ImVec2(p0.x + bw * frac, p1.y), fg);
        y += bh + b * 2 + UI::S(2) * k;
    };
    if(armour > 0) Bar(armour / 100.0f, IM_COL32(40, 40, 40, 255), IM_COL32(200, 200, 200, 255));
    Bar(health / 100.0f, IM_COL32(75, 11, 20, 255), IM_COL32(185, 34, 30, 255));
}

static void DrawNameTags(ImDrawList* dl)
{
    if(MP::classSelect) return;   // 3.01: class selection shows the skin only, no name tags
    Vec3 ref = MP::myDrawPos;
    for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        RemotePlayer& p = MP::players[i];
        if(!p.active || i == MP::myId || !p.drawValid || !p.hasSync) continue;
        if(p.sync.flags & SYNC_DEAD) continue;
        if(p.invisible) continue;                               // 2.19 /invisible
        if(p.tagBlocked && !MP_WallhackOn()) continue;          // 2.19: wall in between
        char bn[64]; uint32_t bc = MP::BcwDecorate(i, p.name, p.color, bn, sizeof(bn));   // 3.04: [GTV] / [SLZ] team tag + colour
        DrawNameTag(dl, p.drawPos, bn, bc, p.sync.health, p.sync.armour, ref, p.paused);
    }
    if(MP::settings.showOwnTag && MP::myDrawValid)
    {
        const char* nm = (MP::myId < MP_MAX_PLAYERS && MP::players[MP::myId].active) ? MP::players[MP::myId].name : MP::settings.name;
        if(MP::IsLocalAdmin()) nm = "[ADMIN] SENJI";   // 2.37: admin.txt + name SENJI: my own tag always has the RGB rank
        char bn[64]; uint32_t bc = MP::BcwDecorate(MP::myId, nm, MP::myColor, bn, sizeof(bn));   // 3.04
        DrawNameTag(dl, MP::myDrawPos, bn, bc, MP::myHealth, MP::myArmour, ref);
    }
}

// ---- chat -------------------------------------------------------------------------------
// 2.19.6: the chat no longer covers the screen. Lines shown, text size, width (long lines wrap),
// fade-out time and a see-through background are all set in game (Server > Chat, or
// /pagesize /fontsize /chatwidth /chatfade). While the chat box is open every recent line shows.
struct ChatRow { const ChatLine* l; bool first; std::string text; float alpha; };
static void DrawChat(ImDrawList* dl)
{
    MP::chatDrawnH = 0;
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const MPSettings& st = MP::settings;
    const float fs = UI::S(27.0f) * st.chatSize * st.chatScale, rowH = fs + UI::S(3) * st.chatSize * st.chatScale;   // 3.04.5: chat scale
    const float x = ds.x * st.chatX, y0 = ds.y * st.chatY;
    const float W = ImMax(UI::S(120), ImMin(ds.x * st.chatWidth, ds.x - x - UI::S(8)));
    bool open = MP::IsChatBoxOpen();
    int maxRows = open ? ImMax(st.chatLines, 10) : st.chatLines;
    if(maxRows <= 0 || MP::chat.empty()) return;
    uint64_t now = MP_NowMs();
    ImFont* f = UI::Font();

    // newest first, split into wrapped rows, until the row budget is used
    std::vector<ChatRow> rows;
    for(int i = (int)MP::chat.size() - 1; i >= 0 && (int)rows.size() < maxRows; --i)
    {
        const ChatLine& l = MP::chat[i];
        float alpha = 1.0f;
        if(!open && st.chatFade > 0 && l.t)
        {
            float age = (float)(now - l.t) / 1000.0f, life = (float)st.chatFade;
            if(age >= life) break;                          // older ones are gone too
            if(age > life - 1.5f) alpha = (life - age) / 1.5f;
        }
        std::string pre = l.prefix.empty() ? "" : l.prefix + ": ";
        std::string t = UI::EmojiToPUA(l.text.c_str());   // 2.19: emojis
        float preW = pre.empty() ? 0 : f->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
        std::vector<ChatRow> mine;
        const char* s = t.c_str(); const char* e = s + t.size();
        bool first = true;
        do
        {
            float avail = first ? ImMax(W - preW, UI::S(40)) : W;
            const char* cut = f->CalcWordWrapPositionA(fs / f->FontSize, s, e, avail);
            if(cut <= s) cut = s + 1 > e ? e : s + 1;
            mine.push_back({ &l, first, std::string(s, cut), alpha });
            s = cut; while(s < e && *s == ' ') ++s;
            first = false;
        } while(s < e && mine.size() < 6);
        for(int k = (int)mine.size() - 1; k >= 0 && (int)rows.size() < maxRows; --k) rows.push_back(mine[k]);
    }
    if(rows.empty()) return;

    float h = rows.size() * rowH;
    if(st.chatBg)
    {
        float bgA = 0;
        for(auto& r : rows) bgA = ImMax(bgA, r.alpha);
        dl->AddRectFilled(ImVec2(x - UI::S(6), y0 - UI::S(4)), ImVec2(x + W + UI::S(6), y0 + h + UI::S(4)),
                          IM_COL32(0, 0, 0, (int)(85 * bgA)), UI::S(8));
    }
    float y = y0 + h - rowH;
    for(auto& r : rows)   // newest at the bottom
    {
        float cx = x;
        if(r.first && !r.l->prefix.empty())
        {
            std::string pre = r.l->prefix + ": ";
            UI::NameText(dl, fs, ImVec2(cx, y), UI::FromRGBA(r.l->prefixColor), pre.c_str(), r.alpha, true, UI::NAME_CHAT);
            cx += f->CalcTextSizeA(fs, FLT_MAX, 0, pre.c_str()).x;
        }
        else if(!r.first) cx += UI::S(14) * st.chatSize * st.chatScale;   // wrapped rest: small indent
        UI::OutlinedText(dl, fs, ImVec2(cx, y), UI::FromRGBA(r.l->textColor), r.text.c_str(), r.alpha);
        y -= rowH;
    }
    MP::chatDrawnH = h;
}

// ---- kill feed --------------------------------------------------------------------------
// 2.17: no animation at all. A line appears instantly and simply stays visible - no slide-in,
// no fade-off. Only the newest 5 kills are kept (MP pushes the oldest out when a 6th arrives).
static void DrawKillFeed(ImDrawList* dl)
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs0 = UI::S(26.0f), icon0 = UI::S(36.0f), gap = UI::S(8.0f);
    const float cx = ds.x * 0.80f;
    float y = ds.y * 0.30f;
    for(const KillEntry& k : MP::kills)
    {
        // 3.01: every kill line looks the same - [ADMIN] SENJI only has the RGB rank + name (no popup)
        const float fs = fs0, icon = icon0;
        ImVec2 ic(cx, y + icon * 0.5f);
        dl->AddRectFilled(ImVec2(ic.x - icon * 0.5f, y), ImVec2(ic.x + icon * 0.5f, y + icon), IM_COL32(0, 0, 0, 200), UI::S(4));
        DrawWeaponIcon(dl, ic, icon * 0.9f, k.weapon);
        float ty = y + (icon - fs) * 0.5f;
        if(!k.killer.empty())
        {
            float w = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, k.killer.c_str()).x;
            UI::NameText(dl, fs, ImVec2(ic.x - icon * 0.5f - gap - w, ty), UI::FromRGBA(k.killerColor), k.killer.c_str(), 1.0f, true, UI::NAME_FEED);
        }
        UI::NameText(dl, fs, ImVec2(ic.x + icon * 0.5f + gap, ty), UI::FromRGBA(k.victimColor), k.victim.c_str(), 1.0f, true, UI::NAME_FEED);
        y += icon + UI::S(6);
    }
}

// ---- connection status (bottom-left) ------------------------------------------------------
static void DrawStatus(ImDrawList* dl)
{
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float fs = UI::S(24.0f);
    char buf[256];
    ImU32 col;
    int count = 0;
    for(auto& p : MP::players) if(p.active) ++count;
    switch(MP::client.GetState())
    {
        case NET_CONNECTED:
            // 2.16: server name only, the IP is never shown
            snprintf(buf, sizeof(buf), "MP: Connected to %s  |  %s  |  Players: %d/%d  |  Ping: %d ms%s",
                     MP::client.GetServerName()[0] ? MP::client.GetServerName() : "server", g_MPRegionNames[MP::settings.region], MP::OnlineCount(), MP::OnlineMax(), MP::DisplayPing(), MP::IsHosting() ? "  |  Hosting" : "");
            col = IM_COL32(60, 220, 90, 255); break;
        case NET_CONNECTING:
            snprintf(buf, sizeof(buf), "MP: Connecting ...");
            col = IM_COL32(240, 200, 40, 255); break;
        default:
            snprintf(buf, sizeof(buf), "MP: Disconnected%s", MP::IsHosting() ? "  |  Hosting" : "");
            col = IM_COL32(230, 60, 60, 255); break;
    }
    UI::OutlinedText(dl, fs, ImVec2(UI::S(12), ds.y - fs - UI::S(10)), col, buf);
}

// ---- connected logo -----------------------------------------------------------------------
static uint64_t NowMs() { return MP_NowMs(); }
static void DrawLogo(ImDrawList* dl)
{
    if(!MP::logoStartMs) return;
    uint64_t t = NowMs() - MP::logoStartMs;
    if(t >= LOGO_TOTAL_MS) { MP::logoStartMs = 0; return; }
    const uint64_t fadeStart = LOGO_TOTAL_MS - LOGO_FADE_TICKS * LOGO_TICK_MS;
    float alpha = 1.0f;
    if(t > fadeStart)
    {
        // step the fade in whole ticks: 20 ticks -> 20 alpha steps
        int tick = (int)((t - fadeStart) / LOGO_TICK_MS);
        alpha = 1.0f - (float)tick / (float)LOGO_FADE_TICKS;
    }
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    EnsureLogo();
    if(s_pLogo)
    {
        float maxW = ds.x * 0.60f, maxH = ds.y * 0.40f;
        float sc = fminf(maxW / s_LogoSize.x, maxH / s_LogoSize.y);
        ImVec2 sz(s_LogoSize.x * sc, s_LogoSize.y * sc);
        ImVec2 p0((ds.x - sz.x) * 0.5f, (ds.y - sz.y) * 0.5f);
        dl->AddImage((ImTextureID)s_pLogo, p0, ImVec2(p0.x + sz.x, p0.y + sz.y), ImVec2(0, 0), s_LogoUV, IM_COL32(255, 255, 255, (int)(255 * alpha)));
    }
    else
    {
        const char* txt = "CONNECTED";
        float fs = UI::S(110);
        ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, txt);
        UI::OutlinedText(dl, fs, ImVec2((ds.x - ts.x) * 0.5f, (ds.y - ts.y) * 0.5f), IM_COL32(46, 180, 70, (int)(255 * alpha)), txt, alpha);
    }
}

// ---- frame ----------------------------------------------------------------------------------
static void PublishHitRects();   // 2.22
void HUD::Render()
{
    if(!s_bInit && !Init()) return;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)RsGlobal->maximumWidth, (float)RsGlobal->maximumHeight);
    ImGui_ImplRenderWare_NewFrame();
    ImGui::NewFrame();

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    if(!Game::IsPaused())
    {
        // 2.19 join screen: full black slide overlay (under the chat + title logo)
        float ox0, ox1; bool overlay = MP::JoinOverlay(ox0, ox1);
        if(overlay) dl->AddRectFilled(ImVec2(ox0 * io.DisplaySize.x, 0), ImVec2(ox1 * io.DisplaySize.x, io.DisplaySize.y), IM_COL32(0, 0, 0, 255));
        // 2.35: launcher open = nothing of the HUD behind it (tags, chat, kill feed, status)
        const bool launcher = FloatMenu::IsModal();
        if(!launcher)
        {
            if(MP::IsConnected() && !overlay && !MP::JoinScreenActive()) DrawNameTags(dl);   // 2.19.1: no tags (not even yours) on the join view
            if(MP::settings.showChat) DrawChat(dl);
            if(MP::settings.showKillFeed && MP::IsConnected()) DrawKillFeed(dl);   // 2.35: not after leaving
            if(MP::settings.showStatus) DrawStatus(dl);
        }
        DrawLogo(ImGui::GetForegroundDrawList());
        FloatMenu::Render();
        VKeyboard::Render();
    }

    ImGui::EndFrame();
    PublishHitRects();   // 2.22
    ImGui::Render();
    ImGui_ImplRenderWare_RenderDrawData(ImGui::GetDrawData());
}

// Touch routing: returns true if the touch at (x,y) is over an interactive MP window.
// 2.22: called from the touch (Java) thread, so it never reads ImGui's live window list - the
// game thread publishes a small snapshot of the clickable window rects after every frame.
static std::mutex          s_HitMx;
static std::vector<ImRect> s_HitRects;
static void PublishHitRects()
{
    static std::vector<ImRect> tmp;
    tmp.clear();
    ImGuiContext* g = ImGui::GetCurrentContext();
    for(ImGuiWindow* w : g->Windows)
    {
        if(!w->WasActive || w->Hidden) continue;
        if(w->Flags & ImGuiWindowFlags_NoMouseInputs) continue;
        if(w->Flags & ImGuiWindowFlags_Tooltip) continue;
        if(strncmp(w->Name, "Debug##", 7) == 0) continue;
        tmp.push_back(w->Rect());
    }
    std::lock_guard<std::mutex> lk(s_HitMx);
    s_HitRects.swap(tmp);
}
bool HUD::WantsTouch(float x, float y)
{
    if(!s_bInit) return false;
    if(VKeyboard::IsOpen()) return true;
    if(FloatMenu::IsModal()) return true;   // 2.26: full-screen launcher: nothing behind it gets the touch
    std::lock_guard<std::mutex> lk(s_HitMx);
    for(const ImRect& r : s_HitRects) if(r.Contains(ImVec2(x, y))) return true;
    return false;
}
