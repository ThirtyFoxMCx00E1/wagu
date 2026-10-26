// Offscreen preview of src/mp_rp_inv.inc (3.26): the real drawing code with game stubs, software rasterized.
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>
#include <vector>
#include "ui/ui_common.h"
#include "net/protocol.h"
#include "net/rp.h"
#include "arial.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
static int W = 3200, H = 1440;
static ImFont* g_Font;
float UI::S(float px) { return px * H / 1080.0f; }
ImFont* UI::Font() { return g_Font; }
void UI::OutlinedText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float alphaMul)
{
    ImFont* f = Font(); const float o = fmaxf(1.0f, size * 0.06f);
    ImU32 black = IM_COL32(0, 0, 0, (int)(255 * alphaMul));
    ImU32 c = (col & 0x00FFFFFF) | ((ImU32)(((col >> 24) & 255) * alphaMul) << 24);
    dl->AddText(f, size, ImVec2(p.x - o, p.y), black, text); dl->AddText(f, size, ImVec2(p.x + o, p.y), black, text);
    dl->AddText(f, size, ImVec2(p.x, p.y - o), black, text); dl->AddText(f, size, ImVec2(p.x, p.y + o), black, text);
    dl->AddText(f, size, p, c, text);
}
// ---- stubs for the game side ----
static uint64_t g_Now = 100000;
static uint64_t MP_NowMs() { return g_Now; }
struct Vec3 { float x, y, z; };
#define COL_YELLOW 0xFFFF00FF
static void SysMsg(uint32_t, const char*, ...) {}
namespace Game {
    static int LocalPed() { return 1; }
    static int GetCharHealth(int) { return 86; }
    static int GetCharArmour(int) { return 40; }
    static bool IsCharInAnyCar(int) { return false; }
    static float GetCharSpeed(int) { return 0; }
    static bool GetCharPos(int, Vec3& v) { v = {0,0,0}; return true; }
}
static int g_ItemsTex = 2; static int g_ItemsW, g_ItemsH; static unsigned char* g_Items;
namespace HUD { static void* Texture(const char*, ImVec2* uv) { *uv = ImVec2(1, 1); return (void*)(intptr_t)g_ItemsTex; } }
struct { bool active; int ped; } players[MP_MAX_PLAYERS];
static int myId = 7;
struct { char name[32]; } settings = { "Senji_Macase" };
struct { int view = 2; bool phone = false, haveMe = true, ringOut = false; int ringFrom = -1, callWith = -1, unreadSms = 0; PktRpMe me{}; } s_Rp;
enum { RPS_CLICK = 0, RPS_PHONE, RPS_CARD };
static void RpClick(int) {}
static bool RpOnServer() { return true; }
static void RpSend(uint8_t, uint8_t = 0, uint8_t = 0, int = 0, const char* = nullptr) {}
enum { RPA_HOME = 0, RPA_GARAGE = 1 };
static void RpPhoneOpen(int) {}
namespace VKeyboard { static bool IsOpen() { return false; } static void Open(const char*, const char*, int, void(*)(const char*, void*), void*, bool) {} }
static bool SampDialogOpen() { return false; }
static void RpChat(int, const char*, int = 0) {}
static const char* RpName(int) { return "Player"; }
static ImU32 RpCol(uint32_t rgba, float a) { return UI::FromRGBA(rgba, a < 0 ? 0 : (a > 1 ? 1 : a)); }
static void RpText(ImDrawList* dl, float fs, ImVec2 p, ImU32 c, const char* t) { UI::OutlinedText(dl, fs, p, c, t); }
static float RpTextW(float fs, const char* t) { return UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t).x; }
static void BcwBeginOverlay(const char* id, ImVec2 pos, ImVec2 size, float bgAlpha)
{
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(size);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, bgAlpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UI::S(18));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(UI::S(18), UI::S(14)));
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}
static void BcwEndOverlay() { ImGui::End(); ImGui::PopStyleVar(2); ImGui::PopStyleColor(); }
#include "mp_rp_inv.inc"

// ---- rasterizer (font alpha atlas = tex 1, rp_items.png RGBA = tex 2), with clip rects ----
static std::vector<float> fb; static unsigned char* ftex; static int ftw, fth;
static void Raster(ImDrawData* dd)
{
    for(int n = 0; n < dd->CmdListsCount; ++n)
    {
        const ImDrawList* cl = dd->CmdLists[n];
        const ImDrawVert* vb = cl->VtxBuffer.Data; const ImDrawIdx* ib = cl->IdxBuffer.Data;
        for(const ImDrawCmd& cmd : cl->CmdBuffer)
        {
            int texId = (int)(intptr_t)cmd.GetTexID();
            for(unsigned e = 0; e < cmd.ElemCount; e += 3)
            {
                const ImDrawVert& a = vb[ib[cmd.IdxOffset + e] + cmd.VtxOffset];
                const ImDrawVert& b = vb[ib[cmd.IdxOffset + e + 1] + cmd.VtxOffset];
                const ImDrawVert& c = vb[ib[cmd.IdxOffset + e + 2] + cmd.VtxOffset];
                float minx = fmaxf(cmd.ClipRect.x, fminf(a.pos.x, fminf(b.pos.x, c.pos.x))), maxx = fminf(cmd.ClipRect.z, fmaxf(a.pos.x, fmaxf(b.pos.x, c.pos.x)));
                float miny = fmaxf(cmd.ClipRect.y, fminf(a.pos.y, fminf(b.pos.y, c.pos.y))), maxy = fminf(cmd.ClipRect.w, fmaxf(a.pos.y, fmaxf(b.pos.y, c.pos.y)));
                int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(W - 1, ceilf(maxx) - 1);
                int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(H - 1, ceilf(maxy) - 1);
                float area = (b.pos.x - a.pos.x) * (c.pos.y - a.pos.y) - (b.pos.y - a.pos.y) * (c.pos.x - a.pos.x);
                if(fabsf(area) < 1e-6f) continue;
                for(int y = y0; y <= y1; ++y) for(int x = x0; x <= x1; ++x)
                {
                    float px = x + 0.5f, py = y + 0.5f;
                    float w0 = ((b.pos.x - px) * (c.pos.y - py) - (b.pos.y - py) * (c.pos.x - px)) / area;
                    float w1 = ((c.pos.x - px) * (a.pos.y - py) - (c.pos.y - py) * (a.pos.x - px)) / area;
                    float w2 = 1 - w0 - w1;
                    if(w0 < 0 || w1 < 0 || w2 < 0) continue;
                    float u = w0 * a.uv.x + w1 * b.uv.x + w2 * c.uv.x, v = w0 * a.uv.y + w1 * b.uv.y + w2 * c.uv.y;
                    float t[4] = { 1, 1, 1, 1 };
                    if(texId == 2)
                    {
                        int tx = (int)(u * g_ItemsW), ty = (int)(v * g_ItemsH); tx = tx < 0 ? 0 : tx >= g_ItemsW ? g_ItemsW - 1 : tx; ty = ty < 0 ? 0 : ty >= g_ItemsH ? g_ItemsH - 1 : ty;
                        for(int k = 0; k < 4; ++k) t[k] = g_Items[(ty * g_ItemsW + tx) * 4 + k] / 255.0f;
                    }
                    else
                    {
                        int tx = (int)(u * ftw), ty = (int)(v * fth); tx = tx < 0 ? 0 : tx >= ftw ? ftw - 1 : tx; ty = ty < 0 ? 0 : ty >= fth ? fth - 1 : ty;
                        t[3] = ftex[ty * ftw + tx] / 255.0f;
                    }
                    float col[4];
                    for(int k = 0; k < 4; ++k)
                    {
                        float ca = ((a.col >> (8 * k)) & 255) / 255.0f, cb = ((b.col >> (8 * k)) & 255) / 255.0f, cc = ((c.col >> (8 * k)) & 255) / 255.0f;
                        col[k] = (w0 * ca + w1 * cb + w2 * cc) * t[k];
                    }
                    float al = col[3];
                    float* d = &fb[(y * W + x) * 3];
                    for(int k = 0; k < 3; ++k) d[k] = d[k] * (1 - al) + col[k] * al;
                }
            }
        }
    }
}
int main(int argc, char** argv)
{
    const char* bg = argv[1]; const char* out = argv[2]; int mode = atoi(argv[3]);
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(W, H); io.IniFilename = nullptr;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 3; fc.OversampleV = 2;
    g_Font = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), 64.0f, &fc);
    io.Fonts->GetTexDataAsAlpha8(&ftex, &ftw, &fth);
    io.Fonts->SetTexID((ImTextureID)1);
    int n; g_Items = stbi_load("assets/MP/rp_items.png", &g_ItemsW, &g_ItemsH, &n, 4);
    int bw, bh; unsigned char* bgp = stbi_load(bg, &bw, &bh, &n, 3);
    fb.resize(W * H * 3);
    for(int y = 0; y < H; ++y) for(int x = 0; x < W; ++x) for(int k = 0; k < 3; ++k)
        fb[(y * W + x) * 3 + k] = bgp[((y * bh / H) * bw + x * bw / W) * 3 + k] / 255.0f;
    // the state shown: the starter backpack already opened
    s_Inv.have = true; s_Inv.inv.maxWeight = RP_INV_MAX_W;
    uint8_t ids[] = { RPX_ESTEH, RPX_NASIGORENG, RPX_PHONE, RPX_MEDKIT, RPX_BURGER }; uint16_t cnt[] = { 10, 10, 1, 1, 2 };
    int wsum = 0; for(int i = 0; i < 5; ++i) { s_Inv.inv.slot[i].id = ids[i]; s_Inv.inv.slot[i].count = cnt[i]; wsum += g_RpInv[ids[i]].weight * cnt[i]; }
    s_Inv.inv.weight = wsum; s_Inv.sel = 0;
    s_Inv.part[1] = 72; s_Inv.part[4] = 88; s_Inv.lastHp = 86;
    s_Rp.me.number = 5512340; s_Rp.me.job = 0; s_Rp.me.level = 3; s_Rp.me.house = 255; s_Rp.me.playMin = 135; s_Rp.me.items = RPIT_DRIVING; s_Rp.me.hunger = 64; s_Rp.me.thirst = 45;
    // the "Received" cards: 2 fresh (3 s old), 1 fading out (15.4 s old)
    s_Inv.got[0] = { RPX_ESTEH, 10, g_Now - 3000 }; s_Inv.got[1] = { RPX_NASIGORENG, 10, g_Now - 3000 }; s_Inv.nGot = 2;
    if(mode == 1) { s_Inv.got[0].at = g_Now - 15500; s_Inv.got[2] = { RPX_PHONE, 1, g_Now - 1500 }; s_Inv.nGot = 3; }
    if(mode == 0) { s_Inv.panel = true; s_Inv.panelAt = 0; io.MousePos = ImVec2(W * 0.5f + UI::S(150), H * 0.5f + UI::S(110)); }
    if(mode == 1) { s_Inv.win = RPW_INV; s_Inv.winAt = 0; }
    if(mode == 2) { s_Inv.win = RPW_DOCS; s_Inv.winAt = 0; }
    if(mode != 0) io.MousePos = ImVec2(-1, -1);
    for(int f = 0; f < 2; ++f) { io.DeltaTime = 1 / 30.f; ImGui::NewFrame(); RenderRpInv(); ImGui::Render(); }
    Raster(ImGui::GetDrawData());
    FILE* o = fopen(out, "wb"); fprintf(o, "P6 %d %d 255\n", W, H);
    for(int i = 0; i < W * H * 3; ++i) { float v = fb[i]; if(v > 1) v = 1; fputc((int)(v * 255), o); }
    fclose(o);
    return 0;
}
