// renders UI::NameText / KillPopup frames with a tiny software rasterizer -> PPM
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include <string>
#include "ui/ui_common.h"
#include "arial.h"
static ImFont* g_Font; static ImFont* g_Pd;
float UI::S(float px) { return px; }
ImFont* UI::Font() { return g_Font; }
void UI::OutlinedText(ImDrawList* dl, float size, ImVec2 p, ImU32 col, const char* text, float alphaMul)
{
    ImFont* f = Font(); const float o = fmaxf(1.0f, size * 0.06f);
    ImU32 black = IM_COL32(0, 0, 0, (int)(255 * alphaMul));
    dl->AddText(f, size, ImVec2(p.x - o, p.y), black, text); dl->AddText(f, size, ImVec2(p.x + o, p.y), black, text);
    dl->AddText(f, size, ImVec2(p.x, p.y - o), black, text); dl->AddText(f, size, ImVec2(p.x, p.y + o), black, text);
    dl->AddText(f, size, p, col, text);
}
static int W = 1100, H = 520;
static std::vector<float> fb;
static unsigned char* tex; static int tw, th;
static void Raster(ImDrawData* dd)
{
    for(int n = 0; n < dd->CmdListsCount; ++n)
    {
        const ImDrawList* cl = dd->CmdLists[n];
        const ImDrawVert* vb = cl->VtxBuffer.Data; const ImDrawIdx* ib = cl->IdxBuffer.Data;
        for(const ImDrawCmd& cmd : cl->CmdBuffer)
        {
            for(unsigned e = 0; e < cmd.ElemCount; e += 3)
            {
                const ImDrawVert& a = vb[ib[cmd.IdxOffset + e] + cmd.VtxOffset];
                const ImDrawVert& b = vb[ib[cmd.IdxOffset + e + 1] + cmd.VtxOffset];
                const ImDrawVert& c = vb[ib[cmd.IdxOffset + e + 2] + cmd.VtxOffset];
                float minx = fminf(a.pos.x, fminf(b.pos.x, c.pos.x)), maxx = fmaxf(a.pos.x, fmaxf(b.pos.x, c.pos.x));
                float miny = fminf(a.pos.y, fminf(b.pos.y, c.pos.y)), maxy = fmaxf(a.pos.y, fmaxf(b.pos.y, c.pos.y));
                int x0 = (int)fmaxf(0, floorf(minx)), x1 = (int)fminf(W - 1, ceilf(maxx));
                int y0 = (int)fmaxf(0, floorf(miny)), y1 = (int)fminf(H - 1, ceilf(maxy));
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
                    int tx = (int)(u * tw), ty = (int)(v * th); if(tx < 0) tx = 0; if(ty < 0) ty = 0; if(tx >= tw) tx = tw - 1; if(ty >= th) ty = th - 1;
                    float ta = tex[ty * tw + tx] / 255.0f;
                    float col[4];
                    for(int k = 0; k < 4; ++k)
                    {
                        float ca = ((a.col >> (8 * k)) & 255) / 255.0f, cb = ((b.col >> (8 * k)) & 255) / 255.0f, cc = ((c.col >> (8 * k)) & 255) / 255.0f;
                        col[k] = w0 * ca + w1 * cb + w2 * cc;
                    }
                    float al = col[3] * ta;
                    float* d = &fb[(y * W + x) * 3];
                    for(int k = 0; k < 3; ++k) d[k] = d[k] * (1 - al) + col[k] * al;
                }
            }
        }
    }
}
int main(int argc, char** argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 1; float fps = 30;
    const char* mode = argc > 2 ? argv[2] : "tag";
    float t0 = argc > 3 ? atof(argv[3]) : 0.0f;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(W, H); io.IniFilename = nullptr;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false;
    g_Font = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), 34.0f, &fc);
    { ImFontConfig pc; pc.OversampleH = 2; pc.OversampleV = 2; g_Pd = io.Fonts->AddFontFromFileTTF("/home/user/workspace/wagu/assets/MP/pricedown.ttf", 40.0f, &pc); }
    io.Fonts->GetTexDataAsAlpha8(&tex, &tw, &th);
    io.Fonts->SetTexID((ImTextureID)1);
    fb.resize(W * H * 3);
    for(int f = 0; f < frames; ++f)
    {
        io.DeltaTime = 1.0f / fps;
        ImGui::NewFrame();
        // ImGui::GetTime() follows DeltaTime; jump to t0 on first frame
        if(f == 0) GImGui->Time = t0;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        for(int i = 0; i < W * H; ++i) { float g = 0.18f + 0.10f * (i / W) / (float)H; fb[i*3] = g * 0.8f; fb[i*3+1] = g * 0.95f; fb[i*3+2] = g; }
        if(!strcmp(mode, "tag"))
        {
            float fs = 46; const char* nm = "[ADMIN] SENJI";
            float w = g_Font->CalcTextSizeA(fs, FLT_MAX, 0, nm).x;
            UI::NameText(dl, fs, ImVec2((W - w) * 0.5f, H * 0.42f), IM_COL32_WHITE, nm, 1.0f, true, UI::NAME_TAG);
        }
        else if(!strcmp(mode, "score"))
        {
            // same code as RenderGames (mp_games.inc): Pricedown, 4 colours, thick black edge
            struct S { const char* t; unsigned c; } seg[4] = { {"DM SCORE: 12", 0x3C8CFFFF}, {"RACE SCORE: 3", 0xFFD21EFF}, {"JOB SCORE: 7", 0xE6322DFF}, {"SCORE: 22", 0x37C846FF} };
            ImFont* f = g_Pd; float fs = 34.0f, gap = 16.0f, total = 0, w[4];
            for(int i = 0; i < 4; ++i) { w[i] = f->CalcTextSizeA(fs, FLT_MAX, 0, seg[i].t).x; total += w[i] + (i ? gap : 0); }
            float x = W - total - 24, y = 6; const float o = fmaxf(1.5f, fs * 0.07f);
            for(int i = 0; i < 4; ++i)
            {
                unsigned c = seg[i].c;
                for(int dx = -1; dx <= 1; ++dx) for(int dy = -1; dy <= 1; ++dy) if(dx || dy) dl->AddText(f, fs, ImVec2(x + dx * o, y + dy * o), IM_COL32(0,0,0,255), seg[i].t);
                dl->AddText(f, fs, ImVec2(x, y), IM_COL32(c >> 24, (c >> 16) & 0xFF, (c >> 8) & 0xFF, 255), seg[i].t);
                x += w[i] + gap;
            }
            // launcher title icon, now white
            ImVec2 cc(60, 120); dl->AddCircleFilled(cc, 22, IM_COL32(255,255,255,255)); dl->AddCircleFilled(ImVec2(cc.x + 6, cc.y - 2), 15, IM_COL32(15,15,18,255));
            dl->AddText(g_Font, 30, ImVec2(100, 104), IM_COL32_WHITE, "MP Launcher"); dl->AddText(g_Font, 30, ImVec2(290, 104), IM_COL32(150,150,150,255), "v3.00");
        }
        else if(!strcmp(mode, "bars"))
        {
            for(int who = 0; who < 2; ++who)
            {
                float fs = 30; const char* nm = who ? "[ADMIN] SENJI" : "Big Smoke";
                float sx = who ? W * 0.62f : W * 0.2f, sy = H * 0.55f;
                float w = g_Font->CalcTextSizeA(fs, FLT_MAX, 0, nm).x;
                float y = sy - fs - 4; float lift = who ? UI::AdminTagLift(fs) : 0; y -= lift;
                UI::NameText(dl, fs, ImVec2(sx - w * 0.5f, y), IM_COL32(255,190,120,255), nm);
                y += fs + 3 + lift;
                float bw = 90, bh = 9;   // health + armour bars like the game tag
                dl->AddRectFilled(ImVec2(sx - bw/2, y), ImVec2(sx + bw/2, y + bh), IM_COL32(0,0,0,200));
                dl->AddRectFilled(ImVec2(sx - bw/2 + 1, y + 1), ImVec2(sx + bw/2 - 15, y + bh - 1), IM_COL32(200,30,30,255));
                y += bh + 3;
                dl->AddRectFilled(ImVec2(sx - bw/2, y), ImVec2(sx + bw/2, y + bh), IM_COL32(0,0,0,200));
                dl->AddRectFilled(ImVec2(sx - bw/2 + 1, y + 1), ImVec2(sx + bw/2 - 40, y + bh - 1), IM_COL32(220,220,220,255));
                dl->AddCircleFilled(ImVec2(sx, sy + 70), 26, IM_COL32(90,70,60,255));   // head
            }
        }
        else if(!strcmp(mode, "chat"))
        {
            float fs = 30;
            UI::OutlinedText(dl, fs, ImVec2(40, 160), IM_COL32(255,200,80,255), "Carl: gg nice shot", 1.0f);
            UI::NameText(dl, fs, ImVec2(40, 230), IM_COL32_WHITE, "[ADMIN] SENJI: ", 1.0f, true, UI::NAME_CHAT);
            float w = g_Font->CalcTextSizeA(fs, FLT_MAX, 0, "[ADMIN] SENJI: ").x;
            UI::OutlinedText(dl, fs, ImVec2(40 + w, 230), IM_COL32_WHITE, "welcome to the server", 1.0f);
            UI::OutlinedText(dl, fs, ImVec2(40, 300), IM_COL32(120,200,255,255), "Ryder: thanks boss", 1.0f);
        }
        else
        {
            float fs0 = 26, icon0 = 36, gap = 8; float cx = W * 0.62f; float y = 200;
            float age = (float)ImGui::GetTime() - t0;
            float sc = UI::KillPopupScale(age); float fs = fs0 * sc, icon = icon0 * sc;
            ImVec2 ic(cx, y + icon * 0.5f);
            const char* killer = "[ADMIN] SENJI"; const char* victim = "Big Smoke";
            float lw = g_Font->CalcTextSizeA(fs, FLT_MAX, 0, killer).x, rw = g_Font->CalcTextSizeA(fs, FLT_MAX, 0, victim).x;
            UI::KillPopup(dl, ImVec2(ic.x - icon * 0.5f - gap - lw, y), ImVec2(ic.x + icon * 0.5f + gap + rw, y + icon), ic, age);
            dl->AddRectFilled(ImVec2(ic.x - icon * 0.5f, y), ImVec2(ic.x + icon * 0.5f, y + icon), IM_COL32(0, 0, 0, 200), 4);
            dl->AddCircleFilled(ic, icon * 0.25f, IM_COL32_WHITE);
            float ty = y + (icon - fs) * 0.5f;
            UI::NameText(dl, fs, ImVec2(ic.x - icon * 0.5f - gap - lw, ty), IM_COL32_WHITE, killer, 1.0f, true, UI::NAME_FEED);
            UI::NameText(dl, fs, ImVec2(ic.x + icon * 0.5f + gap, ty), IM_COL32(255,120,120,255), victim, 1.0f, true, UI::NAME_FEED);
        }
        if(f == 0) fprintf(stderr, "vtx %s %d\n", mode, dl->VtxBuffer.Size);
        ImGui::Render();
        Raster(ImGui::GetDrawData());
        char fn[64]; snprintf(fn, sizeof(fn), "/tmp/vfx/f_%s_%03d.ppm", mode, f);
        FILE* o = fopen(fn, "wb"); fprintf(o, "P6 %d %d 255\n", W, H);
        for(int i = 0; i < W * H * 3; ++i) { float v = fb[i]; if(v > 1) v = 1; fputc((int)(v * 255), o); }
        fclose(o);
    }
    return 0;
}
