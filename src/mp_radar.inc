// MP 3.27 - Google-Maps-style vector radar (2D square + 3D tilted), first used by Mega Vil RP.
//
// No textures: everything comes from src/ui/sa_map_data.h (tools/radar/gen_map.py) - the game's
// own road network (every vehicle path node + link), its water polygons and the district names.
//
//   2D  north-up square like Google Maps: light land, white roads with a grey casing, blue water,
//       green parks, grey district names, a blue "you are here" dot with a heading beam, "N" on top.
//   3D  the same map tilted like Google navigation: heading-up (follows the camera), real height
//       (the node z - bridges and hills rise), roads get thinner with the distance, pins stand up,
//       a fog band hides the far edge, the "N" chip orbits the frame.
//
// Self-contained (ImGui + UI::S/UI::Font) so tools/preview can render it without the game.
#include "ui/sa_map_data.h"

struct SaPin
{
    float    x, y, z;
    uint32_t rgba;       // 0xRRGGBBAA
    char     glyph[4];   // 1-2 letters drawn in the pin ("$", "H", "P"...), "" = dot
    uint8_t  style;      // 0 pin, 1 player dot, 2 target (pin + pulse, clamped to the frame when off-map)
};
struct SaArea { float x0, y0, x1, y1; uint32_t rgba; };

struct SaRadarView
{
    float meX, meY, meZ;     // player
    float meHeading;         // degrees, GTA (0 = north, counter-clockwise)
    float camFx, camFy;      // camera forward on the ground (any length)
    float range;             // metres from the centre to the frame edge (2D)
    int   mode;              // 1 = 2D, 2 = 3D
    bool  fullMap;           // 3.34: pause-menu map - no frame, no "N", no blue dot, every road cell, bigger budget
};

// ---- colours (Google Maps light) ---------------------------------------------------------------
#define SAR_LAND     IM_COL32(236, 238, 241, 255)
#define SAR_LAND3D   IM_COL32(232, 235, 238, 255)
#define SAR_WATER    IM_COL32(158, 207, 247, 255)
#define SAR_PARK     IM_COL32(198, 232, 196, 255)
#define SAR_CASING   IM_COL32(205, 209, 215, 255)
#define SAR_ROAD     IM_COL32(255, 255, 255, 255)
#define SAR_LABEL    IM_COL32(95, 99, 104, 255)
#define SAR_LABELP   IM_COL32(52, 128, 62, 255)
#define SAR_BLUE     IM_COL32(26, 115, 232, 255)

static inline float SarClamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }

// ---- projection --------------------------------------------------------------------------------
struct SarProj
{
    int   mode;
    float cx, cy, half;          // frame centre + half size (px)
    float ox, oy, oz;            // world origin (player)
    float rx, ry, fx, fy;        // world -> local: right / forward unit vectors on the ground
    float scale;                 // 2D px per metre
    // 3D camera (local space: x right, y forward, z up)
    float camY, camZ, f[3], u[3], F, px, py, nearD, farD;
    void Local(float wx, float wy, float wz, float& lx, float& ly, float& lz) const
    {
        float dx = wx - ox, dy = wy - oy;
        lx = dx * rx + dy * ry; ly = dx * fx + dy * fy; lz = wz - oz;
    }
    // camera space: x right, y up, d depth
    void Cam(float lx, float ly, float lz, float& cxv, float& cyv, float& d) const
    {
        float vy = ly - camY, vz = lz - camZ;
        d = vy * f[1] + vz * f[2]; cxv = lx; cyv = vy * u[1] + vz * u[2];
    }
    ImVec2 FromCam(float cxv, float cyv, float d) const { return ImVec2(px + cxv / d * F, py - cyv / d * F); }
    bool Project(float wx, float wy, float wz, ImVec2& out, float* depth = nullptr) const
    {
        float lx, ly, lz; Local(wx, wy, wz, lx, ly, lz);
        if(mode != 2) { out = ImVec2(cx + lx * scale, cy - ly * scale); if(depth) *depth = 1; return true; }
        float a, b, d; Cam(lx, ly, lz, a, b, d);
        if(d < nearD) return false;
        out = FromCam(a, b, d); if(depth) *depth = d; return true;
    }
    float PxPerM(float depth) const { return mode == 2 ? F / depth : scale; }
};

static void SarSetup(SarProj& P, const SaRadarView& v, ImVec2 p0, float size)
{
    P.mode = v.mode; P.half = size * 0.5f; P.cx = p0.x + P.half; P.cy = p0.y + P.half;
    P.ox = v.meX; P.oy = v.meY; P.oz = v.meZ;
    if(v.mode == 2)
    {
        float l = sqrtf(v.camFx * v.camFx + v.camFy * v.camFy);
        if(l < 1e-3f) { P.fx = -sinf(v.meHeading * 0.0174533f); P.fy = cosf(v.meHeading * 0.0174533f); }
        else { P.fx = v.camFx / l; P.fy = v.camFy / l; }
    }
    else { P.fx = 0; P.fy = 1; }                 // 2D: north up
    P.rx = P.fy; P.ry = -P.fx;                     // forward turned clockwise
    P.scale = P.half / v.range;
    // 3D: camera behind + above the player looking ahead and down (~48 deg)
    float R = v.range;
    P.camY = -R * 0.62f; P.camZ = R * 0.78f;
    float ty = R * 0.38f;                          // look-at point on the ground ahead
    float fy = ty - P.camY, fz = -P.camZ, fl = sqrtf(fy * fy + fz * fz);
    P.f[0] = 0; P.f[1] = fy / fl; P.f[2] = fz / fl;
    P.u[0] = 0; P.u[1] = -P.f[2]; P.u[2] = P.f[1];  // cross(right, f)
    P.F = size * 1.05f; P.px = P.cx; P.py = p0.y + size * 0.47f;
    P.nearD = R * 0.12f; P.farD = R * 3.2f;
}

// ---- drawing helpers ---------------------------------------------------------------------------
// one road link: 2D = a thick line, 3D = a trapezoid that narrows with the distance (near-plane clipped)
// 3.28: a segment fully outside the radar square (the current clip rect) is not sent to the draw list
static inline bool SarOffFrame(ImDrawList* dl, ImVec2 s0, ImVec2 s1, float w)
{
    const ImVec4& c = dl->_CmdHeader.ClipRect;
    if(w > 400.0f) w = 400.0f;
    return (s0.x < c.x - w && s1.x < c.x - w) || (s0.x > c.z + w && s1.x > c.z + w) ||
           (s0.y < c.y - w && s1.y < c.y - w) || (s0.y > c.w + w && s1.y > c.w + w);
}
// 3.28: the most vertices the roads may add in one frame (both passes). 3.27 had no limit and made
// 20 000 - 40 000+ vertices (3D, in a car), which froze the 32-bit game right after the spawn.
static const int kSarRoadVtxBudget = 16000;
static void SarRoad(ImDrawList* dl, const SarProj& P, const float a[3], const float b[3], float widthM, float minPx, ImU32 col)
{
    if(P.mode != 2)
    {
        ImVec2 s0, s1; P.Project(a[0], a[1], a[2], s0); P.Project(b[0], b[1], b[2], s1);
        float w = widthM * P.scale; if(w < minPx) w = minPx;
        if(SarOffFrame(dl, s0, s1, w)) return;
        dl->AddLine(s0, s1, col, w);
        return;
    }
    float l0[3], l1[3]; P.Local(a[0], a[1], a[2], l0[0], l0[1], l0[2]); P.Local(b[0], b[1], b[2], l1[0], l1[1], l1[2]);
    float c0x, c0y, d0, c1x, c1y, d1; P.Cam(l0[0], l0[1], l0[2], c0x, c0y, d0); P.Cam(l1[0], l1[1], l1[2], c1x, c1y, d1);
    if(d0 < P.nearD && d1 < P.nearD) return;
    if(d0 > P.farD && d1 > P.farD) return;
    if(d0 < P.nearD) { float t = (P.nearD - d0) / (d1 - d0); c0x += (c1x - c0x) * t; c0y += (c1y - c0y) * t; d0 = P.nearD; }
    else if(d1 < P.nearD) { float t = (P.nearD - d1) / (d0 - d1); c1x += (c0x - c1x) * t; c1y += (c0y - c1y) * t; d1 = P.nearD; }
    ImVec2 s0 = P.FromCam(c0x, c0y, d0), s1 = P.FromCam(c1x, c1y, d1);
    if(SarOffFrame(dl, s0, s1, widthM * P.F / (d0 < d1 ? d0 : d1))) return;   // 3.28: outside the square: nothing to draw
    float dx = s1.x - s0.x, dy = s1.y - s0.y, l = sqrtf(dx * dx + dy * dy);
    if(l < 0.25f) return;
    float nx = -dy / l, ny = dx / l;
    float w0 = widthM * P.F / d0 * 0.5f, w1 = widthM * P.F / d1 * 0.5f;
    if(w0 < minPx * 0.5f) w0 = minPx * 0.5f; if(w1 < minPx * 0.5f) w1 = minPx * 0.5f;
    dl->AddQuadFilled(ImVec2(s0.x + nx * w0, s0.y + ny * w0), ImVec2(s1.x + nx * w1, s1.y + ny * w1),
                      ImVec2(s1.x - nx * w1, s1.y - ny * w1), ImVec2(s0.x - nx * w0, s0.y - ny * w0), col);
}

// a convex world polygon (water, park, zone) at height z; 3D clips it against the near plane
static void SarPoly(ImDrawList* dl, const SarProj& P, const float* xs, const float* ys, int n, float z, ImU32 col)
{
    ImVec2 pts[12]; int m = 0;
    if(P.mode != 2)
    {
        for(int i = 0; i < n; ++i) P.Project(xs[i], ys[i], z, pts[m++]);
    }
    else
    {
        float cx[8], cy[8], cd[8];
        for(int i = 0; i < n; ++i) { float lx, ly, lz; P.Local(xs[i], ys[i], z, lx, ly, lz); P.Cam(lx, ly, lz, cx[i], cy[i], cd[i]); }
        for(int i = 0; i < n && m < 10; ++i)
        {
            int j = (i + 1) % n;
            bool in0 = cd[i] >= P.nearD, in1 = cd[j] >= P.nearD;
            if(in0) pts[m++] = P.FromCam(cx[i], cy[i], cd[i]);
            if(in0 != in1)
            {
                float t = (P.nearD - cd[i]) / (cd[j] - cd[i]);
                pts[m++] = P.FromCam(cx[i] + (cx[j] - cx[i]) * t, cy[i] + (cy[j] - cy[i]) * t, P.nearD);
            }
        }
    }
    if(m >= 3) dl->AddConvexPolyFilled(pts, m, col);
}

static void SarRect(ImDrawList* dl, const SarProj& P, float x0, float y0, float x1, float y1, float z, ImU32 col)
{
    // big rectangles are split so the 3D near-plane clip and the perspective stay right
    int nx = P.mode == 2 ? (int)SarClamp((x1 - x0) / 120.0f, 1, 8) : 1, ny = P.mode == 2 ? (int)SarClamp((y1 - y0) / 120.0f, 1, 8) : 1;
    for(int i = 0; i < nx; ++i) for(int j = 0; j < ny; ++j)
    {
        float ax = x0 + (x1 - x0) * i / nx, bx = x0 + (x1 - x0) * (i + 1) / nx, ay = y0 + (y1 - y0) * j / ny, by = y0 + (y1 - y0) * (j + 1) / ny;
        float xs[4] = { ax, bx, bx, ax }, ys[4] = { ay, ay, by, by };
        SarPoly(dl, P, xs, ys, 4, z, col);
    }
}

static void SarHaloText(ImDrawList* dl, float fs, ImVec2 c, ImU32 col, const char* t, float alpha = 1.0f)
{
    ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, t);
    ImVec2 p(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f);
    ImU32 halo = IM_COL32(255, 255, 255, (int)(220 * alpha));
    float o = fs * 0.08f; if(o < 1) o = 1;
    for(int i = 0; i < 8; ++i) { float a = i * 0.785f; dl->AddText(UI::Font(), fs, ImVec2(p.x + cosf(a) * o, p.y + sinf(a) * o), halo, t); }
    ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col); cc.w *= alpha;
    dl->AddText(UI::Font(), fs, p, ImGui::ColorConvertFloat4ToU32(cc), t);
}

static ImU32 SarRgba(uint32_t rgba, float a = 1.0f)
{
    return IM_COL32((rgba >> 24) & 255, (rgba >> 16) & 255, (rgba >> 8) & 255, (int)((rgba & 255) * a));
}

// Google pin: a teardrop with a white glyph; tip at `at`
static void SarDrawPin(ImDrawList* dl, ImVec2 at, float r, uint32_t rgba, const char* glyph, float alpha = 1.0f)
{
    ImU32 c = SarRgba(rgba | 0xFF, alpha), dark = IM_COL32(0, 0, 0, (int)(70 * alpha));
    ImVec2 h(at.x, at.y - r * 1.55f);
    dl->AddCircleFilled(ImVec2(at.x, at.y + r * 0.1f), r * 0.35f, dark, 10);           // ground shadow
    dl->AddTriangleFilled(ImVec2(h.x - r * 0.78f, h.y + r * 0.55f), ImVec2(h.x + r * 0.78f, h.y + r * 0.55f), at, c);
    dl->AddCircleFilled(h, r, c, 18);
    dl->AddCircle(h, r, IM_COL32(255, 255, 255, (int)(230 * alpha)), 18, r * 0.16f);
    if(glyph && glyph[0])
    {
        float fs = r * 1.25f; ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, glyph);
        dl->AddText(UI::Font(), fs, ImVec2(h.x - ts.x * 0.5f, h.y - ts.y * 0.52f), IM_COL32(255, 255, 255, (int)(255 * alpha)), glyph);
    }
    else dl->AddCircleFilled(h, r * 0.36f, IM_COL32(255, 255, 255, (int)(255 * alpha)), 12);
}

// where a ray from the frame centre leaves the square (for off-map targets / the N chip)
static ImVec2 SarEdge(float cx, float cy, float half, float dx, float dy)
{
    float m = fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy); if(m < 1e-4f) m = 1e-4f;
    return ImVec2(cx + dx / m * half, cy + dy / m * half);
}

// ---- the radar ---------------------------------------------------------------------------------
static void SaRadarDraw(ImDrawList* dl, ImVec2 p0, float size, const SaRadarView& v,
                        const SaPin* pins, int nPins, const SaArea* areas, int nAreas, float now)
{
    SarProj P; SarSetup(P, v, p0, size);
    const ImVec2 p1(p0.x + size, p0.y + size);
    const float round = v.fullMap ? 0.0f : UI::S(10), border = v.fullMap ? 0.0f : UI::S(3.5f);
    const bool d3 = v.mode == 2 && !v.fullMap;
    // frame shadow + background
    if(!v.fullMap) dl->AddRectFilled(ImVec2(p0.x + UI::S(3), p0.y + UI::S(5)), ImVec2(p1.x + UI::S(3), p1.y + UI::S(5)), IM_COL32(0, 0, 0, 70), round + UI::S(2));
    dl->AddRectFilled(p0, p1, d3 ? SAR_LAND3D : SAR_LAND, round);
    dl->PushClipRect(ImVec2(p0.x + border * 0.5f, p0.y + border * 0.5f), ImVec2(p1.x - border * 0.5f, p1.y - border * 0.5f), true);

    // what part of the world to look at
    const float reach = d3 ? v.range * 3.0f : v.range * 1.5f;
    float qx = v.meX, qy = v.meY;
    if(d3) { qx += P.fx * v.range * 1.1f; qy += P.fy * v.range * 1.1f; }     // the 3D view is mostly ahead
    const float R2 = reach * reach;

    // outside the map = sea (SA has water all around)
    {
        const float W = 3000.0f;
        if(qx - reach < -W) SarRect(dl, P, -W - 3000, -W - 3000, -W, W + 3000, 0, SAR_WATER);
        if(qx + reach >  W) SarRect(dl, P,  W, -W - 3000,  W + 3000, W + 3000, 0, SAR_WATER);
        if(qy - reach < -W) SarRect(dl, P, -W, -W - 3000,  W, -W, 0, SAR_WATER);
        if(qy + reach >  W) SarRect(dl, P, -W,  W,  W,  W + 3000, 0, SAR_WATER);
    }
    // parks
    for(int i = 0; i < SAMAP_PARKS; ++i)
    {
        const SaMapRect& r = kSaMapPark[i];
        float nx = SarClamp(qx, r.x0, r.x1) - qx, ny = SarClamp(qy, r.y0, r.y1) - qy;
        if(nx * nx + ny * ny > R2) continue;
        SarRect(dl, P, r.x0, r.y0, r.x1, r.y1, v.meZ - 2.0f, SAR_PARK);
    }
    // water
    for(int i = 0; i < SAMAP_WATER; ++i)
    {
        const SaMapWater& w = kSaMapWater[i];
        float xs[4], ys[4], x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for(int k = 0; k < w.n; ++k) { xs[k] = w.x[k] * 0.25f; ys[k] = w.y[k] * 0.25f; x0 = fminf(x0, xs[k]); x1 = fmaxf(x1, xs[k]); y0 = fminf(y0, ys[k]); y1 = fmaxf(y1, ys[k]); }
        float nx = SarClamp(qx, x0, x1) - qx, ny = SarClamp(qy, y0, y1) - qy;
        if(nx * nx + ny * ny > R2) continue;
        float z = w.z * 0.25f;
        if(d3 && (x1 - x0 > 240 || y1 - y0 > 240))
        {   // big sea quads: cut into pieces so the near-plane clip keeps the perspective right
            int sx = (int)SarClamp((x1 - x0) / 160.0f, 1, 12), sy = (int)SarClamp((y1 - y0) / 160.0f, 1, 12);
            if(w.n == 4)
            {
                for(int a = 0; a < sx; ++a) for(int b = 0; b < sy; ++b)
                {
                    float ax = x0 + (x1 - x0) * a / sx, bx = x0 + (x1 - x0) * (a + 1) / sx, ay = y0 + (y1 - y0) * b / sy, by = y0 + (y1 - y0) * (b + 1) / sy;
                    float mx = (ax + bx) * 0.5f - qx, my = (ay + by) * 0.5f - qy; float hr = reach + 230.0f;
                    if(mx * mx + my * my > hr * hr) continue;
                    float px4[4] = { ax, bx, bx, ax }, py4[4] = { ay, ay, by, by };
                    SarPoly(dl, P, px4, py4, 4, z, SAR_WATER);
                }
                continue;
            }
        }
        SarPoly(dl, P, xs, ys, w.n, z, SAR_WATER);
    }
    // server areas (greenzones, turfs...)
    for(int i = 0; i < nAreas; ++i)
    {
        const SaArea& a = areas[i];
        float nx = SarClamp(qx, a.x0, a.x1) - qx, ny = SarClamp(qy, a.y0, a.y1) - qy;
        if(nx * nx + ny * ny > R2) continue;
        SarRect(dl, P, a.x0, a.y0, a.x1, a.y1, v.meZ - 1.0f, SarRgba(a.rgba));
    }

    // roads: two passes (casing, fill) over the grid cells around the view
    const float roadW = d3 ? 9.0f : 7.5f;
    const float minPx = UI::S(d3 ? 1.6f : 2.2f);
    int c0x = (int)((qx - reach - 90 + 3000) / SAMAP_CELL), c1x = (int)((qx + reach + 90 + 3000) / SAMAP_CELL);
    int c0y = (int)((qy - reach - 90 + 3000) / SAMAP_CELL), c1y = (int)((qy + reach + 90 + 3000) / SAMAP_CELL);
    c0x = c0x < 0 ? 0 : c0x; c0y = c0y < 0 ? 0 : c0y; c1x = c1x >= SAMAP_GRID ? SAMAP_GRID - 1 : c1x; c1y = c1y >= SAMAP_GRID ? SAMAP_GRID - 1 : c1y;
    // 3.28: nearest cells first, so a frame that hits the vertex budget only loses the far edge;
    // no anti-aliasing on the road strips (half the vertices; they are thin and get a casing anyway)
    int cells[SAMAP_GRID * SAMAP_GRID]; float cellD[SAMAP_GRID * SAMAP_GRID]; int nCells = 0;
    const int cellCap = v.fullMap ? SAMAP_GRID * SAMAP_GRID : 64;   // 3.34: the full map may need every cell
    const int vtxBudget = v.fullMap ? kSarRoadVtxBudget * 4 : kSarRoadVtxBudget;
    {
        float qcx = (qx + 3000) / SAMAP_CELL - 0.5f, qcy = (qy + 3000) / SAMAP_CELL - 0.5f;
        for(int cy = c0y; cy <= c1y; ++cy) for(int cx = c0x; cx <= c1x && nCells < cellCap; ++cx)
        {
            float d = (cx - qcx) * (cx - qcx) + (cy - qcy) * (cy - qcy);
            int j = nCells++;
            while(j > 0 && cellD[j - 1] > d) { cells[j] = cells[j - 1]; cellD[j] = cellD[j - 1]; --j; }
            cells[j] = cy * SAMAP_GRID + cx; cellD[j] = d;
        }
    }
    const ImDrawListFlags oldFlags = dl->Flags;
    dl->Flags &= ~(ImDrawListFlags_AntiAliasedLines | ImDrawListFlags_AntiAliasedFill | ImDrawListFlags_AntiAliasedLinesUseTex);
    const int vtxStart = dl->VtxBuffer.Size;
    for(int pass = 0; pass < 2; ++pass)
    {
        const int vtxCap = vtxStart + (pass == 0 ? vtxBudget / 2 : vtxBudget);
        for(int ci = 0; ci < nCells && dl->VtxBuffer.Size < vtxCap; ++ci)
        {
            int c = cells[ci];
            for(int k = kSaMapCellStart[c]; k < kSaMapCellStart[c + 1] && dl->VtxBuffer.Size < vtxCap; ++k)
            {
                int e = kSaMapCellEdge[k];
                const int16_t* na = &kSaMapNode[kSaMapEdge[e * 2] * 3];
                const int16_t* nb = &kSaMapNode[kSaMapEdge[e * 2 + 1] * 3];
                float a[3] = { na[0] * 0.25f, na[1] * 0.25f, na[2] * 0.25f }, b[3] = { nb[0] * 0.25f, nb[1] * 0.25f, nb[2] * 0.25f };
                float mx = (a[0] + b[0]) * 0.5f - qx, my = (a[1] + b[1]) * 0.5f - qy;
                if(mx * mx + my * my > (reach + 60) * (reach + 60)) continue;
                if(!d3) { a[2] = b[2] = v.meZ; }
                if(pass == 0) SarRoad(dl, P, a, b, roadW + 3.0f, minPx + UI::S(2), SAR_CASING);
                else          SarRoad(dl, P, a, b, roadW, minPx, SAR_ROAD);
            }
        }
    }
    dl->Flags = oldFlags;

    // district names (biggest first, no overlaps)
    {
        ImVec2 used[24][2]; int nu = 0;
        float fsBase = UI::S(d3 ? 13.0f : 12.5f);
        for(int i = 0; i < SAMAP_LABELS && nu < 24; ++i)
        {
            const SaMapLabel& L = kSaMapLabel[i];
            float dx = L.x - qx, dy = L.y - qy;
            if(dx * dx + dy * dy > R2) continue;
            if(L.size < v.range * 0.55f && !L.park) continue;           // tiny districts only when zoomed in
            ImVec2 s; float depth = 1;
            if(!P.Project(L.x, L.y, d3 ? v.meZ : v.meZ, s, &depth)) continue;
            if(s.x < p0.x || s.x > p1.x || s.y < p0.y || s.y > p1.y) continue;
            float fs = fsBase; float alpha = 1.0f;
            if(d3) { fs = SarClamp(fsBase * (v.range * 1.25f / depth), UI::S(9), UI::S(16)); alpha = SarClamp(1.6f - depth / (v.range * 2.6f), 0.15f, 1.0f); }
            ImVec2 ts = UI::Font()->CalcTextSizeA(fs, FLT_MAX, 0, L.name);
            if(ts.x > size - UI::S(16)) continue;
            // keep the whole name inside the frame (Google slides labels in instead of cutting them)
            s.x = SarClamp(s.x, p0.x + ts.x * 0.5f + UI::S(6), p1.x - ts.x * 0.5f - UI::S(6));
            s.y = SarClamp(s.y, p0.y + ts.y * 0.5f + UI::S(d3 ? 20 : 8), p1.y - ts.y * 0.5f - UI::S(6));
            ImVec2 a(s.x - ts.x * 0.5f - 2, s.y - ts.y * 0.5f - 1), b(s.x + ts.x * 0.5f + 2, s.y + ts.y * 0.5f + 1);
            bool hit = false;
            for(int k = 0; k < nu && !hit; ++k) hit = !(b.x < used[k][0].x || a.x > used[k][1].x || b.y < used[k][0].y || a.y > used[k][1].y);
            if(hit) continue;
            used[nu][0] = a; used[nu][1] = b; ++nu;
            SarHaloText(dl, fs, s, L.park ? SAR_LABELP : SAR_LABEL, L.name, alpha);
        }
    }

    // 3D: haze over the far edge + a soft sky line
    if(d3)
    {
        float hy = p0.y + size * 0.30f;
        dl->AddRectFilledMultiColor(p0, ImVec2(p1.x, hy), IM_COL32(222, 232, 244, 255), IM_COL32(222, 232, 244, 255), IM_COL32(232, 235, 238, 0), IM_COL32(232, 235, 238, 0));
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + size * 0.06f), IM_COL32(222, 232, 244, 255));
    }

    // pins (far first so near ones overlap them)
    struct Item { ImVec2 s; float depth; int i; bool edge; };
    Item items[96]; int ni = 0;
    for(int i = 0; i < nPins && ni < 96; ++i)
    {
        const SaPin& pn = pins[i];
        ImVec2 s; float depth = 1;
        bool ok = P.Project(pn.x, pn.y, d3 ? pn.z : v.meZ, s, &depth);
        bool inside = ok && s.x > p0.x + UI::S(6) && s.x < p1.x - UI::S(6) && s.y > p0.y + UI::S(6) && s.y < p1.y - UI::S(4);
        if(!inside)
        {
            if(pn.style != 2) continue;
            float lx, ly, lz; P.Local(pn.x, pn.y, pn.z, lx, ly, lz);
            float ex = lx, ey = -ly; float half = P.half - UI::S(16);
            float cyc = d3 ? P.py : P.cy;
            s = SarEdge(P.cx, cyc, half, ex, ey);
            s.y = SarClamp(s.y, p0.y + UI::S(30), p1.y - UI::S(8));
            items[ni++] = { s, 1e9f, i, true };
            continue;
        }
        items[ni++] = { s, depth, i, false };
    }
    for(int a = 1; a < ni; ++a) for(int b = a; b > 0 && items[b].depth > items[b - 1].depth; --b) { Item t = items[b]; items[b] = items[b - 1]; items[b - 1] = t; }
    for(int k = 0; k < ni; ++k)
    {
        const SaPin& pn = pins[items[k].i];
        ImVec2 s = items[k].s;
        float r = UI::S(d3 ? 9.0f : 8.0f);
        if(d3 && !items[k].edge) r = SarClamp(r * (v.range * 0.9f / items[k].depth), UI::S(5.5f), UI::S(11));
        if(pn.style == 1)
        {   // other players: a coloured dot with a white ring
            float pr = r * 0.62f;
            dl->AddCircleFilled(s, pr + UI::S(1.6f), IM_COL32(255, 255, 255, 255), 14);
            dl->AddCircleFilled(s, pr, SarRgba(pn.rgba | 0xFF), 14);
            continue;
        }
        if(items[k].edge)
        {   // off-map target: a small pin hugging the frame
            SarDrawPin(dl, ImVec2(s.x, s.y + r * 0.6f), r * 0.8f, pn.rgba, pn.glyph, 0.95f);
            continue;
        }
        if(pn.style == 2)
        {   // pulse under the target
            float t = fmodf(now, 1.4f) / 1.4f;
            dl->AddCircle(s, r * (0.6f + t * 1.6f), SarRgba(pn.rgba, 1.0f - t), 20, UI::S(2));
        }
        SarDrawPin(dl, s, r, pn.rgba, pn.glyph);
    }

    // you: Google's blue dot + heading beam (2D), a navigation arrow lying on the ground (3D)
    if(!v.fullMap)   // 3.34: on the pause map the game draws your own arrow
    {
        float h = v.meHeading * 0.0174533f;
        float hx = -sinf(h), hy = cosf(h);            // world forward of the ped
        if(!d3)
        {
            ImVec2 c(P.cx, P.cy);
            float sx = hx * P.rx + hy * P.ry, sy = -(hx * P.fx + hy * P.fy);   // to screen (y down)
            float ang = atan2f(sy, sx), spread = 0.55f, R = UI::S(30);
            dl->PathLineTo(c);
            for(int i = 0; i <= 10; ++i) { float a = ang - spread + spread * 2 * i / 10.0f; dl->PathLineTo(ImVec2(c.x + cosf(a) * R, c.y + sinf(a) * R)); }
            dl->PathFillConvex(IM_COL32(26, 115, 232, 70));
            dl->AddCircleFilled(c, UI::S(12), IM_COL32(26, 115, 232, 45), 24);
            dl->AddCircleFilled(c, UI::S(7.5f), IM_COL32(255, 255, 255, 255), 20);
            dl->AddCircleFilled(c, UI::S(5.5f), SAR_BLUE, 20);
        }
        else
        {
            float L = v.range * 0.085f, W = v.range * 0.06f;
            float rx = hy, ry = -hx;                    // ped right
            float tip[3]  = { v.meX + hx * L,       v.meY + hy * L,       v.meZ };
            float lft[3]  = { v.meX - hx * L * 0.7f - rx * W, v.meY - hy * L * 0.7f - ry * W, v.meZ };
            float rgt[3]  = { v.meX - hx * L * 0.7f + rx * W, v.meY - hy * L * 0.7f + ry * W, v.meZ };
            float notch[3]= { v.meX - hx * L * 0.3f, v.meY - hy * L * 0.3f, v.meZ };
            ImVec2 st, sl, sr, sn;
            if(P.Project(tip[0], tip[1], tip[2], st) && P.Project(lft[0], lft[1], lft[2], sl) && P.Project(rgt[0], rgt[1], rgt[2], sr) && P.Project(notch[0], notch[1], notch[2], sn))
            {
                ImVec2 sh(0, UI::S(3));
                dl->AddTriangleFilled(ImVec2(st.x + sh.x, st.y + sh.y), ImVec2(sl.x, sl.y + sh.y), ImVec2(sn.x, sn.y + sh.y), IM_COL32(0, 0, 0, 60));
                dl->AddTriangleFilled(ImVec2(st.x + sh.x, st.y + sh.y), ImVec2(sn.x, sn.y + sh.y), ImVec2(sr.x, sr.y + sh.y), IM_COL32(0, 0, 0, 60));
                ImVec2 lift(0, -UI::S(2.5f));
                ImVec2 a(st.x + lift.x, st.y + lift.y), b(sl.x, sl.y + lift.y), c(sr.x, sr.y + lift.y), n(sn.x, sn.y + lift.y);
                dl->AddTriangleFilled(a, b, n, IM_COL32(20, 98, 210, 255));
                dl->AddTriangleFilled(a, n, c, SAR_BLUE);
                ImVec2 out[4] = { a, b, n, c };
                dl->AddPolyline(out, 4, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, UI::S(2.2f));
            }
        }
    }
    dl->PopClipRect();
    if(v.fullMap) return;   // 3.34: the pause map has no frame / "N" chip

    // frame
    dl->AddRect(p0, p1, IM_COL32(20, 22, 26, 255), round, 0, border);
    // "N": top centre in 2D (north up); orbits the frame in 3D (heading up)
    {
        ImVec2 n;
        if(!d3) n = ImVec2(P.cx, p0.y);
        else
        {
            float lx = 0 * P.rx + 1 * P.ry, ly = 0 * P.fx + 1 * P.fy;      // north in local space
            n = SarEdge(P.cx, P.cy, P.half, lx, -ly);
        }
        float r = UI::S(11);
        dl->AddCircleFilled(n, r + UI::S(1.5f), IM_COL32(255, 255, 255, 255), 20);
        dl->AddCircleFilled(n, r, IM_COL32(32, 33, 36, 255), 20);
        ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(15), FLT_MAX, 0, "N");
        dl->AddText(UI::Font(), UI::S(15), ImVec2(n.x - ts.x * 0.5f, n.y - ts.y * 0.5f), IM_COL32(255, 255, 255, 255), "N");
    }
}

// the 2D / 3D chip in the top-right corner (the caller makes it tappable)
static void SaRadarChipRect(ImVec2 p0, float size, ImVec2& a, ImVec2& b)
{
    float w = UI::S(40), h = UI::S(24);
    a = ImVec2(p0.x + size - w - UI::S(7), p0.y + UI::S(7)); b = ImVec2(a.x + w, a.y + h);
}
static void SaRadarDrawChip(ImDrawList* dl, ImVec2 p0, float size, int mode)
{
    ImVec2 a, b; SaRadarChipRect(p0, size, a, b);
    dl->AddRectFilled(ImVec2(a.x, a.y + UI::S(1.5f)), ImVec2(b.x, b.y + UI::S(1.5f)), IM_COL32(0, 0, 0, 50), UI::S(12));
    dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 240), UI::S(12));
    const char* t = mode == 2 ? "3D" : "2D";
    ImVec2 ts = UI::Font()->CalcTextSizeA(UI::S(15), FLT_MAX, 0, t);
    dl->AddText(UI::Font(), UI::S(15), ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), SAR_BLUE, t);
}
