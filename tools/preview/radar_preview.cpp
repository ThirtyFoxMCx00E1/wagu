// Offscreen preview of src/mp_radar.inc + src/mp_compass.inc (3.27): the real drawing code, software rasterized.
// build: g++ -O2 -std=c++17 -Isrc -Iimgui tools/preview/radar_preview.cpp imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp -o /tmp/radar_preview
// run:   /tmp/radar_preview bg.png out.ppm <mode 1=2D 2=3D 3=compass> x y z heading
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vector>
#include "ui/ui_common.h"
#include "arial.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
static int W = 3200, H = 1440;
static ImFont* g_Font;
float UI::S(float px) { return px * H / 1080.0f; }
ImFont* UI::Font() { return g_Font; }
#include "mp_radar.inc"
#include "mp_compass.inc"
static int g_ItemsW, g_ItemsH; static unsigned char* g_Items;
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
                    if(false)
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
    float x = argc > 4 ? atof(argv[4]) : 1480, y = argc > 5 ? atof(argv[5]) : -1700, z = argc > 6 ? atof(argv[6]) : 13, hd = argc > 7 ? atof(argv[7]) : 30;
    float range = argc > 8 ? atof(argv[8]) : 180;
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); io.DisplaySize = ImVec2(W, H); io.IniFilename = nullptr;
    ImFontConfig fc; fc.FontDataOwnedByAtlas = false; fc.OversampleH = 3; fc.OversampleV = 2;
    g_Font = io.Fonts->AddFontFromMemoryTTF((void*)arialData, sizeof(arialData), 64.0f, &fc);
    io.Fonts->GetTexDataAsAlpha8(&ftex, &ftw, &fth);
    io.Fonts->SetTexID((ImTextureID)1);
    int n, bw, bh; unsigned char* bgp = stbi_load(bg, &bw, &bh, &n, 3);
    fb.resize(W * H * 3);
    for(int yy = 0; yy < H; ++yy) for(int xx = 0; xx < W; ++xx) for(int k = 0; k < 3; ++k)
        fb[(yy * W + xx) * 3 + k] = bgp[((yy * bh / H) * bw + xx * bw / W) * 3 + k] / 255.0f;
    io.DeltaTime = 1 / 30.f; ImGui::NewFrame();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    float h = hd * 0.0174533f;
    if(mode == 3)
    {
        CompassMark m[5] = { { fmodf(hd + 20, 360), 0x3B82F6FF, 40, 0 }, { fmodf(hd + 330, 360), 0x3B82F6FF, 80, 0 }, { fmodf(hd + 120, 360), 0x3B82F6FF, 80, 0 },
                             { fmodf(hd + 38, 360), 0xEF4444FF, 214, 1 }, { fmodf(hd + 300, 360), 0xFFFFFFFF, 530, 1 } };
        PubgCompassDraw(dl, W * 0.5f, UI::S(12), W * 0.30f, hd, m, 5);
    }
    else
    {
        SaRadarView v; v.meX = x; v.meY = y; v.meZ = z; v.meHeading = 360 - hd; v.camFx = -sinf((360 - hd) * 0.0174533f); v.camFy = cosf((360 - hd) * 0.0174533f);
        v.range = range; v.mode = mode;
        SaPin pins[6] = {
            { 1481, -1772, 14, 0x22C55EFF, "", 0 },     // city hall-ish
            { 1172, -1323, 14, 0xEF4444FF, "H", 0 },
            { 1555, -1675, 16, 0x2563EBFF, "P", 0 },
            { 1462, -1650, 14, 0x0EA5E9FF, "", 1 },
            { 1500, -1600, 14, 0xF59E0BFF, "", 1 },
            { 2495, -1690, 14, 0xA855F7FF, "", 2 },
        };
        SaArea ar[1] = { { 1440, -1730, 1550, -1620, 0x22C55E40 } };
        float size = H * 0.25f;
        ImVec2 p0(W * 0.012f, H * 0.915f - size);
        SaRadarDraw(dl, p0, size, v, pins, 6, ar, 1, 0.5f);
        SaRadarDrawChip(dl, p0, size, mode);
    }
    ImGui::Render();
    Raster(ImGui::GetDrawData());
    FILE* o = fopen(out, "wb"); fprintf(o, "P6 %d %d 255\n", W, H);
    for(int i = 0; i < W * H * 3; ++i) { float v = fb[i]; if(v > 1) v = 1; fputc((int)(v * 255), o); }
    fclose(o);
    return 0;
}
