// MP 4.13.28: PCMenu-style drawing for the neon title menu (see neonpaint.h).
#include <mod/amlmod.h>
#include <mod/logger.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <atomic>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include "neonpaint.h"

namespace
{
    struct V2 { float x, y; };
    typedef void  (*DrawSpriteFn)(void* tex, V2* verts, V2* uvs, NeonPaint::Col* cols);
    typedef void  (*DrawQuadFn)(V2* verts, NeonPaint::Col* cols);
    typedef int   (*RenderStateSetFn)(int state, void* value);
    typedef void* (*PngReadFn)(const char*);
    typedef void* (*FindRasterFmtFn)(void* img, int type, int* w, int* h, int* d, int* flags);
    typedef void* (*RasterCreateFn)(int w, int h, int d, int flags);
    typedef void* (*RasterFromImgFn)(void* raster, void* img);
    typedef void* (*TexCreateFn)(void* raster);
    typedef void  (*ImgDestroyFn)(void* img);

    DrawSpriteFn     fnDrawSprite = nullptr;
    DrawQuadFn       fnDrawQuad = nullptr;
    RenderStateSetFn fnRenderState = nullptr;
    PngReadFn        fnPngRead = nullptr;
    FindRasterFmtFn  fnFindRasterFmt = nullptr;
    RasterCreateFn   fnRasterCreate = nullptr;
    RasterFromImgFn  fnRasterFromImg = nullptr;
    TexCreateFn      fnTexCreate = nullptr;
    ImgDestroyFn     fnImgDestroy = nullptr;
    int*             s_pRsGlobal = nullptr;
    bool             s_bInit = false, s_bLoaded = false, s_bFailed = false;

    enum { RS_TEXADDRESS = 2, RS_ZTEST = 6, RS_ZWRITE = 8, RS_TEXFILTER = 9, RS_SRCBLEND = 10, RS_DESTBLEND = 11, RS_VERTEXALPHA = 12 };

    struct NamedTex { char name[32]; NeonPaint::Tex t; };
    NamedTex s_Tex[40]; int s_nTex = 0;

    struct Glyph { float u0, v0, u1, v1; int w, h, xo, yo; float adv; bool ok; };
    struct Atlas { const NeonPaint::Tex* tex = nullptr; int size = 64, asc = 50; Glyph g[128]; };
    Atlas s_Font[NeonPaint::F_COUNT];

    // touches
    std::mutex s_TouchMx;
    int   s_nFinger = -1;
    bool  s_bDown = false, s_bUp = false;
    float s_fX = 0, s_fY = 0, s_fPX = 0, s_fPY = 0, s_fUX = 0, s_fUY = 0;
    // the frame's snapshot
    bool  f_bDown = false, f_bTap = false;
    float f_fX = 0, f_fY = 0, f_fPX = 0, f_fPY = 0, f_fUX = 0, f_fUY = 0;

    bool LoadTex(const std::string& dir, const char* name);
    bool LoadTexOld(const std::string& dir, const char* name)
    {
        if(s_nTex >= (int)(sizeof(s_Tex) / sizeof(s_Tex[0]))) return false;
        std::string p = dir + name;
        struct stat st; if(stat(p.c_str(), &st) != 0) { logger->Error("Neon menu: %s missing", p.c_str()); return false; }
        void* img = fnPngRead(p.c_str());
        if(!img) { logger->Error("Neon menu: %s is not a readable PNG (8-bit RGBA, not interlaced)", p.c_str()); return false; }
        const int iw = ((int*)img)[1], ih = ((int*)img)[2];   // RwImage: flags, width, height, depth, stride, pixels
        void* tex = nullptr;
        int w = 0, h = 0, d = 0, flags = 0;
        fnFindRasterFmt(img, 4 /*rwRASTERTYPETEXTURE*/, &w, &h, &d, &flags);
        if(void* r = fnRasterCreate(w, h, d, flags)) { fnRasterFromImg(r, img); tex = fnTexCreate(r); }
        fnImgDestroy(img);
        if(!tex) { logger->Error("Neon menu: %s - no texture (out of memory?)", p.c_str()); return false; }
        NamedTex& n = s_Tex[s_nTex++];
        snprintf(n.name, sizeof(n.name), "%s", name); n.t.t = tex; n.t.w = iw; n.t.h = ih;
        return true;
    }
}
namespace NeonEmbed { std::string Builtin(const std::string& dir, const char* name); }
namespace {
    // 4.13.34: your own PNG in neon/ first; when it is missing or the game can't read it (a palette / 16-bit /
    // interlaced PNG, a JPEG renamed to .png, ...) the built-in one, so the neon / mp menus always show
    bool LoadTex(const std::string& dir, const char* name)
    {
        if(LoadTexOld(dir, name)) return true;
        const std::string b = NeonEmbed::Builtin(dir, name);
        if(b.empty()) return false;
        const size_t cut = b.rfind('/');
        const bool ok = LoadTexOld(b.substr(0, cut + 1), name);
        logger->Info("Neon menu: %s%s -> built-in %s %s", dir.c_str(), name, name, ok ? "used" : "failed too");
        return ok;
    }
    bool LoadFont(const std::string& dir, int idx, const char* base)
    {
        std::string tp = dir + base + ".txt";
        FILE* f = fopen(tp.c_str(), "rb");
        if(!f) { logger->Error("Neon menu: %s missing", tp.c_str()); return false; }
        Atlas& a = s_Font[idx]; memset(a.g, 0, sizeof(a.g));
        char line[160]; int size = 0, asc = 0, desc = 0, cap = 0, aw = 0, ah = 0;
        if(!fgets(line, sizeof(line), f) || sscanf(line, "FONT %d %d %d %d %d %d", &size, &asc, &desc, &cap, &aw, &ah) != 6 || aw <= 0 || ah <= 0)
        { fclose(f); logger->Error("Neon menu: %s is not a font description", tp.c_str()); return false; }
        a.size = size; a.asc = asc;
        while(fgets(line, sizeof(line), f))
        {
            int code, x, y, w, h, xo, yo; float adv;
            if(sscanf(line, "G %d %d %d %d %d %d %d %f", &code, &x, &y, &w, &h, &xo, &yo, &adv) == 8 && code >= 32 && code < 128)
            {
                Glyph& g = a.g[code];
                g.u0 = (float)x / aw; g.v0 = (float)y / ah; g.u1 = (float)(x + w) / aw; g.v1 = (float)(y + h) / ah;
                g.w = w; g.h = h; g.xo = xo; g.yo = yo; g.adv = adv; g.ok = true;
            }
        }
        fclose(f);
        char png[48]; snprintf(png, sizeof(png), "%s.png", base);
        if(!LoadTex(dir, png)) return false;
        a.tex = NeonPaint::T(png);
        return a.tex != nullptr;
    }
    inline NeonPaint::Col Lerp(NeonPaint::Col a, NeonPaint::Col b, float t)
    {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        return { (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t), (uint8_t)(a.b + (b.b - a.b) * t), (uint8_t)(a.a + (b.a - a.a) * t) };
    }
}

bool NeonPaint::Init(void* h)
{
    if(!h) return false;
    *(void**)&fnDrawSprite    = (void*)aml->GetSym(h, "_ZN10MobileMenu10DrawSpriteEP9RwTextureP9CVector2DS3_P5CRGBA");
    *(void**)&fnDrawQuad      = (void*)aml->GetSym(h, "_ZN10MobileMenu8DrawQuadEP9CVector2DP5CRGBA");
    *(void**)&fnRenderState   = (void*)aml->GetSym(h, "_Z16RwRenderStateSet13RwRenderStatePv");
    *(void**)&fnPngRead       = (void*)aml->GetSym(h, "_Z14RtPNGImageReadPKc");
    *(void**)&fnFindRasterFmt = (void*)aml->GetSym(h, "_Z23RwImageFindRasterFormatP7RwImageiPiS1_S1_S1_");
    *(void**)&fnRasterCreate  = (void*)aml->GetSym(h, "_Z14RwRasterCreateiiii");
    *(void**)&fnRasterFromImg = (void*)aml->GetSym(h, "_Z20RwRasterSetFromImageP8RwRasterP7RwImage");
    *(void**)&fnTexCreate     = (void*)aml->GetSym(h, "_Z15RwTextureCreateP8RwRaster");
    *(void**)&fnImgDestroy    = (void*)aml->GetSym(h, "_Z14RwImageDestroyP7RwImage");
    s_pRsGlobal = (int*)aml->GetSym(h, "RsGlobal");
    s_bInit = fnDrawSprite && fnDrawQuad && fnRenderState && fnPngRead && fnFindRasterFmt && fnRasterCreate && fnRasterFromImg && fnTexCreate && fnImgDestroy && s_pRsGlobal;
    logger->Info("Neon menu: PCMenu-style drawing %s", s_bInit ? "ready (game sprites + PNG textures)" : "NOT available (game functions missing)");
    return s_bInit;
}
bool NeonPaint::Ready() { return s_bInit && s_bLoaded; }
bool NeonPaint::Failed() { return s_bFailed; }
// RsGlobal { const char* appName; int maximumWidth, maximumHeight; ... }: +4/+8 on 32-bit, +8/+12 on 64-bit (PCMenu)
float NeonPaint::W() { return s_pRsGlobal ? (float)*(int*)((char*)s_pRsGlobal + sizeof(void*)) : 1280.0f; }
float NeonPaint::H() { return s_pRsGlobal ? (float)*(int*)((char*)s_pRsGlobal + sizeof(void*) + 4) : 720.0f; }

namespace NeonEmbed { int Ensure(const std::string& dir); }   // 4.13.33 (src/ui/neon_embed.cpp)
bool NeonPaint::Load(const char* mpDir)
{
    if(s_bLoaded) return true;
    if(s_bFailed || !s_bInit) return false;
    std::string dir = std::string(mpDir) + "/neon/";
    NeonEmbed::Ensure(dir);   // 4.13.33: missing / old files come from the copy built into MP
    static const char* const kTex[] = { "art.png", "logo.png", "shade_left.png", "shade_bottom.png", "shade_top.png", "card_identity.png",
        "card_featured.png", "dialog.png", "button.png", "pill.png", "discord.png", "arrow.png", "arrow_down.png", "dot.png", "white.png" };
    bool ok = true;
    for(const char* n : kTex) if(!LoadTex(dir, n)) ok = false;
    if(!LoadFont(dir, F_GOTHIC, "font_gothic")) ok = false;
    if(!LoadFont(dir, F_CAPS, "font_caps")) ok = false;
    if(!LoadFont(dir, F_UI, "font_ui")) ok = false;
    if(!ok)
    {
        s_bFailed = true;
        logger->Error("Neon menu: files/TYFMC/MP/neon/ is incomplete - copy the TYFMC folder of the MP package again. The game's own menu is shown.");
        return false;
    }
    s_bLoaded = true;
    logger->Info("Neon menu: %d textures + 3 font atlases loaded from %s", s_nTex, dir.c_str());
    return true;
}

const NeonPaint::Tex* NeonPaint::T(const char* name)
{
    for(int i = 0; i < s_nTex; ++i) if(!strcmp(s_Tex[i].name, name)) return &s_Tex[i].t;
    return nullptr;
}

void NeonPaint::Begin()
{
    fnRenderState(RS_ZTEST, (void*)0);
    fnRenderState(RS_ZWRITE, (void*)0);
    fnRenderState(RS_VERTEXALPHA, (void*)1);
    fnRenderState(RS_SRCBLEND, (void*)5);   // rwBLENDSRCALPHA
    fnRenderState(RS_DESTBLEND, (void*)6);  // rwBLENDINVSRCALPHA
    fnRenderState(RS_TEXFILTER, (void*)2);  // rwFILTERLINEAR
    fnRenderState(RS_TEXADDRESS, (void*)3); // rwTEXTUREADDRESSCLAMP
}
void NeonPaint::End()
{
    fnRenderState(RS_ZTEST, (void*)1);
    fnRenderState(RS_ZWRITE, (void*)1);
}
void NeonPaint::Quad(float x0, float y0, float x1, float y1, Col c)
{
    if(const Tex* w = T("white.png")) { Img(w, x0, y0, x1, y1, c); return; }
    V2 v[4] = { {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1} };
    Col cc[4] = { c, c, c, c };
    fnDrawQuad(v, cc);
}
void NeonPaint::ImgUV(const Tex* t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Col top, Col bottom)
{
    if(!t || !t->t) return;
    V2 v[4]  = { {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1} };
    V2 uv[4] = { {u0, v0}, {u1, v0}, {u0, v1}, {u1, v1} };
    Col cc[4] = { top, top, bottom, bottom };
    fnDrawSprite(t->t, v, uv, cc);
}
void NeonPaint::Img(const Tex* t, float x0, float y0, float x1, float y1, Col c) { ImgUV(t, x0, y0, x1, y1, 0, 0, 1, 1, c, c); }

float NeonPaint::TextW(int font, const char* s, float px, float track)
{
    if(font < 0 || font >= F_COUNT || !s) return 0;
    const Atlas& a = s_Font[font]; const float sc = px / (float)a.size;
    float w = 0; int n = 0;
    for(const unsigned char* p = (const unsigned char*)s; *p; ++p, ++n)
    {
        unsigned c = *p; if(c < 32 || c > 126 || !a.g[c].ok) c = '?';
        w += a.g[c].adv * sc + track;
    }
    return n ? w - track : 0;
}
void NeonPaint::Text(int font, const char* s, float x, float y, float px, Col top, Col bottom, float track)
{
    if(font < 0 || font >= F_COUNT || !s) return;
    const Atlas& a = s_Font[font]; if(!a.tex) return;
    const float sc = px / (float)a.size, base = y + a.asc * sc;
    float pen = x;
    for(const unsigned char* p = (const unsigned char*)s; *p; ++p)
    {
        unsigned c = *p; if(c < 32 || c > 126 || !a.g[c].ok) c = '?';
        const Glyph& g = a.g[c];
        if(g.w > 0 && g.h > 0)
        {
            const float gx = pen + g.xo * sc, gy = base + g.yo * sc, gx1 = gx + g.w * sc, gy1 = gy + g.h * sc;
            ImgUV(a.tex, gx, gy, gx1, gy1, g.u0, g.v0, g.u1, g.v1, Lerp(top, bottom, (gy - y) / px), Lerp(top, bottom, (gy1 - y) / px));
        }
        pen += g.adv * sc + track;
    }
}

void NeonPaint::OnTouch(int kind, int finger, float x, float y)
{
    std::lock_guard<std::mutex> lk(s_TouchMx);
    if(kind == 1)
    {
        if(s_nFinger >= 0 && s_nFinger != finger) return;   // one finger is enough for a menu
        s_nFinger = finger; s_bDown = true; s_fX = s_fPX = x; s_fY = s_fPY = y;
    }
    else if(finger == s_nFinger)
    {
        s_fX = x; s_fY = y;
        if(kind == 3) { s_bDown = false; s_bUp = true; s_fUX = x; s_fUY = y; s_nFinger = -1; }
    }
}
void NeonPaint::FrameInput()
{
    std::lock_guard<std::mutex> lk(s_TouchMx);
    f_bDown = s_bDown; f_fX = s_fX; f_fY = s_fY; f_fPX = s_fPX; f_fPY = s_fPY;
    f_bTap = s_bUp; f_fUX = s_fUX; f_fUY = s_fUY; s_bUp = false;
}
static inline bool In(float x, float y, float x0, float y0, float x1, float y1) { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
bool NeonPaint::Tap(float x0, float y0, float x1, float y1) { return f_bTap && In(f_fPX, f_fPY, x0, y0, x1, y1) && In(f_fUX, f_fUY, x0, y0, x1, y1); }
bool NeonPaint::Held(float x0, float y0, float x1, float y1) { return f_bDown && In(f_fX, f_fY, x0, y0, x1, y1) && In(f_fPX, f_fPY, x0, y0, x1, y1); }
void NeonPaint::EatTap() { f_bTap = false; }
