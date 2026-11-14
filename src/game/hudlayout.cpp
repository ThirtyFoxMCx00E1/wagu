// MP 4.13.22: GTA HUD settings - see hudlayout.h.
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <mod/config.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include "hudlayout.h"

namespace
{
struct CRect { float left, bottom, right, top; };      // the game's CRect (x1, y1, x2, y2)
struct CRGBA { uint8_t r, g, b, a; };
struct CVector2D { float x, y; };
struct FontDetails { CRGBA color; float scaleX, scaleY; };   // the head of CFont::Details (engine/Font.h)

static const char* const kKeys[HudLayout::PART_COUNT] = {
    "HUD_PlayerInfo", "HUD_BarHealth", "HUD_BarArmor", "HUD_BarBreath", "HUD_WeaponIcon", "HUD_Ammo", "HUD_Money",
    "HUD_MoneyChange", "HUD_Clock", "HUD_WantedStar", "HUD_Radar", "HUD_VehName", "HUD_AreaName", "HUD_Radio", "HUD_Subtitles" };
static const char* const kNames[HudLayout::PART_COUNT] = {
    "Player info (all)", "Health bar", "Armor bar", "Breath bar", "Weapon icon", "Ammo", "Money", "Money change",
    "Clock", "Wanted stars", "Radar", "Vehicle name", "Area name", "Radio station", "Subtitles" };

HudLayout::Cfg s_Cfg[HudLayout::PART_COUNT];
bool  s_bOn = false, s_bHooked = false, s_bWant = false;
int   s_nHooks = 0;
void* s_hGame = nullptr;
float s_fW = 1280, s_fH = 720;
FontDetails* s_pDetails = nullptr;

// the part stack while a part draws (PlayerInfo > Health bar ...)
struct Ctx { int part; bool anchored; float ax, ay; };
Ctx  s_Stack[4]; int s_nDepth = 0;
bool s_bInInfo = false;   // inside CWidgetPlayerInfo::Draw (for the text parts: money, clock, ammo)

inline bool Identity(const HudLayout::Cfg& c)
{ return c.show && c.x == 0 && c.y == 0 && c.z == 100 && c.size == 100 && c.w == 100 && c.h == 100 && c.alpha == 100; }
inline bool Active() { return s_bOn && s_nDepth > 0; }

void Push(int p) { if(s_nDepth < 4) { s_Stack[s_nDepth].part = p; s_Stack[s_nDepth].anchored = false; } ++s_nDepth; }
void Pop() { if(s_nDepth > 0) --s_nDepth; }

// one point through the stack: the innermost part first, then its parents
void Xform(float& x, float& y)
{
    const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = n - 1; i >= 0; --i)
    {
        Ctx& c = s_Stack[i];
        const HudLayout::Cfg& g = s_Cfg[c.part];
        if(!c.anchored) { c.anchored = true; c.ax = x; c.ay = y; }
        const float k = g.size * g.z * 0.0001f, sx = k * g.w * 0.01f, sy = k * g.h * 0.01f;
        x = c.ax + (x - c.ax) * sx + g.x * 0.01f * s_fW;
        y = c.ay + (y - c.ay) * sy + g.y * 0.01f * s_fH;
    }
}
void Scale(float& sx, float& sy)
{
    const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = 0; i < n; ++i)
    {
        const HudLayout::Cfg& g = s_Cfg[s_Stack[i].part];
        const float k = g.size * g.z * 0.0001f; sx *= k * g.w * 0.01f; sy *= k * g.h * 0.01f;
    }
}
float Alpha()
{
    float a = 1; const int n = s_nDepth < 4 ? s_nDepth : 4;
    for(int i = 0; i < n; ++i) a *= s_Cfg[s_Stack[i].part].alpha * 0.01f;
    return a < 0 ? 0 : (a > 1 ? 1 : a);
}
inline CRGBA A(const CRGBA& c, float a) { CRGBA o = c; o.a = (uint8_t)(c.a * a); return o; }
CRect XRect(const CRect& r)
{
    float x1 = r.left, y1 = r.bottom, x2 = r.right, y2 = r.top;
    Xform(x1, y1); Xform(x2, y2);
    return { x1, y1, x2, y2 };
}
bool Hidden(int p) { return s_bOn && !s_Cfg[p].show; }

// ---- the part hooks --------------------------------------------------------------------------------------
typedef void (*FnV)(void*);
typedef void (*FnBar)(void*, int, float);
typedef void (*FnIcon)(void*, void*, CRect, float);
typedef void (*FnS)();
FnV    oInfoDraw = nullptr, oWanted = nullptr, oRadarW = nullptr, oRadio = nullptr;
FnBar  oHealth = nullptr, oArmor = nullptr, oBreath = nullptr;
FnIcon oIcon = nullptr;
FnS    oHudRadar = nullptr, oArea = nullptr, oVeh = nullptr, oSubs = nullptr;

#define PART_WRAP(part, call) do { if(!s_bOn) { call; return; } if(Hidden(part)) return; Push(part); call; Pop(); } while(0)
void hInfoDraw(void* w) { if(!s_bOn) { oInfoDraw(w); return; } if(Hidden(HudLayout::PLAYERINFO)) return; Push(HudLayout::PLAYERINFO); s_bInInfo = true; oInfoDraw(w); s_bInInfo = false; Pop(); }
void hHealth(void* w, int a, float b) { PART_WRAP(HudLayout::BAR_HEALTH, oHealth(w, a, b)); }
void hArmor(void* w, int a, float b)  { PART_WRAP(HudLayout::BAR_ARMOR, oArmor(w, a, b)); }
void hBreath(void* w, int a, float b) { PART_WRAP(HudLayout::BAR_BREATH, oBreath(w, a, b)); }
void hIcon(void* w, void* ped, CRect r, float f) { PART_WRAP(HudLayout::WEAPON_ICON, oIcon(w, ped, r, f)); }
void hWanted(void* w) { PART_WRAP(HudLayout::WANTED, oWanted(w)); }
void hRadarW(void* w) { PART_WRAP(HudLayout::RADAR, oRadarW(w)); }
void hHudRadar() { if(s_nDepth > 0 && s_Stack[(s_nDepth < 4 ? s_nDepth : 4) - 1].part == HudLayout::RADAR) { oHudRadar(); return; } PART_WRAP(HudLayout::RADAR, oHudRadar()); }
void hArea() { PART_WRAP(HudLayout::AREA_NAME, oArea()); }
void hVeh()  { PART_WRAP(HudLayout::VEH_NAME, oVeh()); }
void hSubs() { PART_WRAP(HudLayout::SUBTITLES, oSubs()); }
void hRadio(void* m) { PART_WRAP(HudLayout::RADIO, oRadio(m)); }

// ---- the primitives ----------------------------------------------------------------------------------------
typedef void (*FnSV2)(const CRect&, const CRGBA&, const CRGBA&, const CRGBA&, const CRGBA&);
typedef void (*FnSV4)(const CRect&, const CRGBA&, const CRGBA&, const CRGBA&, const CRGBA&, float, float, float, float, float, float, float, float);
typedef void (*FnSV3)(int, float*, float*, const CRGBA&);
typedef void (*FnSV6)(int, float*, CRGBA*);
typedef void (*FnMask)(unsigned int, float*, float);
typedef void (*FnPrint)(float, float, uint16_t*);
FnSV2 oSV2 = nullptr; FnSV4 oSV4 = nullptr; FnSV3 oSV3 = nullptr; FnSV6 oSV6 = nullptr; FnMask oMask = nullptr; FnPrint oPrint = nullptr;

void hSV2(const CRect& r, const CRGBA& a, const CRGBA& b, const CRGBA& c, const CRGBA& d)
{
    if(!Active()) { oSV2(r, a, b, c, d); return; }
    const float k = Alpha(); const CRect x = XRect(r);
    oSV2(x, A(a, k), A(b, k), A(c, k), A(d, k));
}
void hSV4(const CRect& r, const CRGBA& a, const CRGBA& b, const CRGBA& c, const CRGBA& d, float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4)
{
    if(!Active()) { oSV4(r, a, b, c, d, u1, v1, u2, v2, u3, v3, u4, v4); return; }
    const float k = Alpha(); const CRect x = XRect(r);
    oSV4(x, A(a, k), A(b, k), A(c, k), A(d, k), u1, v1, u2, v2, u3, v3, u4, v4);
}
void hSV3(int n, float* v, float* tc, const CRGBA& col)
{
    if(!Active() || n <= 0 || n > 64 || !v) { oSV3(n, v, tc, col); return; }
    float t[128]; for(int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); }
    oSV3(n, t, tc, A(col, Alpha()));
}
void hSV6(int n, float* v, CRGBA* cols)
{
    if(!Active() || n <= 0 || n > 64 || !v) { oSV6(n, v, cols); return; }
    float t[128]; CRGBA c[64]; const float k = Alpha();
    for(int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); if(cols) c[i] = A(cols[i], k); }
    oSV6(n, t, cols ? c : nullptr);
}
void hMask(unsigned int n, float* v, float z)
{
    if(!Active() || n == 0 || n > 64 || !v) { oMask(n, v, z); return; }
    float t[128]; for(unsigned int i = 0; i < n; ++i) { t[i * 2] = v[i * 2]; t[i * 2 + 1] = v[i * 2 + 1]; Xform(t[i * 2], t[i * 2 + 1]); }
    oMask(n, t, z);
}
// the text parts of the player info: money "$00000123", money change "+$50" / "-$50", clock "12:34", ammo "30-120"
int TextPart(const uint16_t* s)
{
    if(!s) return -1;
    int i = 0; while(s[i] == ' ') ++i;
    const uint16_t c0 = s[i], c1 = c0 ? s[i + 1] : 0;
    if(c0 == '$') return HudLayout::MONEY;
    if((c0 == '+' || c0 == '-') && c1 == '$') return HudLayout::MONEY_CHANGE;
    bool digits = false, colon = false;
    for(int k = i; s[k] && k < i + 16; ++k) { if(s[k] >= '0' && s[k] <= '9') digits = true; else if(s[k] == ':') colon = true; }
    if(colon && digits) return HudLayout::CLOCK;
    if(digits) return HudLayout::AMMO;
    return -1;
}
void hPrint(float x, float y, uint16_t* s)
{
    if(!s_bOn) { oPrint(x, y, s); return; }
    bool pushed = false;
    if(s_bInInfo && s_nDepth == 1)
    {
        const int p = TextPart(s);
        if(p >= 0) { if(Hidden(p)) return; Push(p); pushed = true; }
    }
    if(!Active()) { oPrint(x, y, s); return; }
    Xform(x, y);
    if(s_pDetails)
    {
        const FontDetails keep = *s_pDetails;
        Scale(s_pDetails->scaleX, s_pDetails->scaleY);
        s_pDetails->color = A(keep.color, Alpha());
        oPrint(x, y, s);
        s_pDetails->scaleX = keep.scaleX; s_pDetails->scaleY = keep.scaleY; s_pDetails->color = keep.color;
    }
    else oPrint(x, y, s);
    if(pushed) Pop();
}

template<typename F> bool H(const char* sym, F hook, F* orig)
{
    void* f = (void*)aml->GetSym(s_hGame, sym);
    if(!f) { logger->Info("GTA HUD: %s not in this game", sym); return false; }
    if(!aml->Hook(f, (void*)hook, (void**)orig) || !*orig) { logger->Info("GTA HUD: hook failed (%s)", sym); return false; }
    ++s_nHooks; return true;
}
void InstallHooks()
{
    if(s_bHooked || !s_hGame) return;
    s_bHooked = true;
    s_pDetails = (FontDetails*)aml->GetSym(s_hGame, "_ZN5CFont7DetailsE");
    // the primitives first: a part hook without them only hides / shows
    H("_ZN9CSprite2d11SetVerticesERK5CRectRK5CRGBAS5_S5_S5_", &hSV2, &oSV2);
    H("_ZN9CSprite2d11SetVerticesERK5CRectRK5CRGBAS5_S5_S5_ffffffff", &hSV4, &oSV4);
    H("_ZN9CSprite2d11SetVerticesEiPfS0_RK5CRGBA", &hSV3, &oSV3);
    H("_ZN9CSprite2d11SetVerticesEiPfP5CRGBA", &hSV6, &oSV6);
    H("_ZN9CSprite2d15SetMaskVerticesEiPff", &hMask, &oMask);
    H("_ZN5CFont11PrintStringEffPt", &hPrint, &oPrint);
    H("_ZN17CWidgetPlayerInfo4DrawEv", &hInfoDraw, &oInfoDraw);
    H("_ZN17CWidgetPlayerInfo15RenderHealthBarEif", &hHealth, &oHealth);
    H("_ZN17CWidgetPlayerInfo14RenderArmorBarEif", &hArmor, &oArmor);
    H("_ZN17CWidgetPlayerInfo15RenderBreathBarEif", &hBreath, &oBreath);
    H("_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf", &hIcon, &oIcon);
    H("_ZN17CWidgetPlayerInfo10DrawWantedEv", &hWanted, &oWanted);
    H("_ZN12CWidgetRadar4DrawEv", &hRadarW, &oRadarW);
    H("_ZN4CHud9DrawRadarEv", &hHudRadar, &oHudRadar);
    H("_ZN4CHud12DrawAreaNameEv", &hArea, &oArea);
    H("_ZN4CHud15DrawVehicleNameEv", &hVeh, &oVeh);
    H("_ZN4CHud13DrawSubtitlesEv", &hSubs, &oSubs);
    H("_ZN20CAERadioTrackManager23DisplayRadioStationNameEv", &hRadio, &oRadio);
    logger->Info("GTA HUD: custom HUD on, %d game functions hooked (font details %s)", s_nHooks, s_pDetails ? "ok" : "-");
}
void Defaults(HudLayout::Cfg& c) { c.show = true; c.x = c.y = 0; c.z = c.size = c.w = c.h = c.alpha = 100; }
float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
struct InitDefaults { InitDefaults() { for(auto& c : s_Cfg) Defaults(c); } } s_InitDefaults;
}   // namespace

const char* HudLayout::PartName(int p) { return p >= 0 && p < PART_COUNT ? kNames[p] : ""; }
const char* HudLayout::PartKey(int p) { return p >= 0 && p < PART_COUNT ? kKeys[p] : ""; }
HudLayout::Cfg& HudLayout::Get(int p) { return s_Cfg[p >= 0 && p < PART_COUNT ? p : 0]; }
void HudLayout::Reset(int p) { if(p >= 0 && p < PART_COUNT) Defaults(s_Cfg[p]); }
bool HudLayout::Enabled() { return s_bOn; }
int  HudLayout::HookCount() { return s_bHooked ? s_nHooks : 0; }
void HudLayout::SetEnabled(bool on) { s_bOn = on; if(on) { s_bWant = true; InstallHooks(); } }
void HudLayout::SetScreen(float w, float h) { if(w > 16 && h > 16) { s_fW = w; s_fH = h; } }
void HudLayout::Init(void* gtasa) { s_hGame = gtasa; if(s_bWant) InstallHooks(); }

void HudLayout::Load()
{
    if(!::cfg) return;
    for(int p = 0; p < PART_COUNT; ++p)
    {
        Cfg& c = s_Cfg[p]; char k[64];
        snprintf(k, sizeof(k), "%s_Show", kKeys[p]);   c.show  = ::cfg->GetBool(k, true, "GTAHUD");
        snprintf(k, sizeof(k), "%s_X", kKeys[p]);      c.x     = Clamp(::cfg->GetFloat(k, 0, "GTAHUD"), -100, 100);
        snprintf(k, sizeof(k), "%s_Y", kKeys[p]);      c.y     = Clamp(::cfg->GetFloat(k, 0, "GTAHUD"), -100, 100);
        snprintf(k, sizeof(k), "%s_Z", kKeys[p]);      c.z     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Size", kKeys[p]);   c.size  = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Width", kKeys[p]);  c.w     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Height", kKeys[p]); c.h     = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 10, 400);
        snprintf(k, sizeof(k), "%s_Alpha", kKeys[p]);  c.alpha = Clamp(::cfg->GetFloat(k, 100, "GTAHUD"), 0, 100);
    }
    const bool on = ::cfg->GetBool("CustomHud", false, "GTAHUD");
    bool any = false; for(auto& c : s_Cfg) if(!Identity(c)) any = true;
    SetEnabled(on && any);   // all default = nothing hooked
    s_bOn = on;
}
void HudLayout::Save()
{
    if(!::cfg) return;
    auto B = [](const char* k, bool v) { ConfigEntry* e = ::cfg->Bind(k, v, "GTAHUD"); e->SetBool(v); delete e; };
    auto F = [](const char* k, float v) { ConfigEntry* e = ::cfg->Bind(k, v, "GTAHUD"); e->SetFloat(v); delete e; };
    B("CustomHud", s_bOn);
    for(int p = 0; p < PART_COUNT; ++p)
    {
        const Cfg& c = s_Cfg[p]; char k[64];
        snprintf(k, sizeof(k), "%s_Show", kKeys[p]);   B(k, c.show);
        snprintf(k, sizeof(k), "%s_X", kKeys[p]);      F(k, c.x);
        snprintf(k, sizeof(k), "%s_Y", kKeys[p]);      F(k, c.y);
        snprintf(k, sizeof(k), "%s_Z", kKeys[p]);      F(k, c.z);
        snprintf(k, sizeof(k), "%s_Size", kKeys[p]);   F(k, c.size);
        snprintf(k, sizeof(k), "%s_Width", kKeys[p]);  F(k, c.w);
        snprintf(k, sizeof(k), "%s_Height", kKeys[p]); F(k, c.h);
        snprintf(k, sizeof(k), "%s_Alpha", kKeys[p]);  F(k, c.alpha);
    }
    ::cfg->Save();
}
