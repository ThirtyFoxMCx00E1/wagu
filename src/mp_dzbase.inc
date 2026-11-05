// 4.08: Project Day Z Legacy - base building (Rust style), the phone side. Included by mp_dayz.inc.
//
// The pieces (net/dz_build.h) are the SA-MP wall sections from samp.img, in the Day Z model pack
// (Host/DayZ/models/buildings/samp, tools/make_dz_build.py). The server keeps every piece
// (dayz_bases.txt) and sends them to everybody on Project Day Z Legacy (PKT_DZ_BASE); this file
// shows them as solid map objects near you, and draws the build mode: a blue ghost of the piece
// where it will go (red = it can't go there), snapping to the foundation grid like Rust:
//   * a foundation goes on the ground, or next to another foundation (same height, same grid)
//   * walls / doorways / window frames go on an edge of a foundation or a ceiling
//   * a ceiling goes on top of the walls of a cell, or next to another ceiling
//   * a door goes into a doorway and opens / closes for you and your party
// Building needs a Toolbox in the bag (the server checks it).

struct DzPieceC { DzBaseRow r; int obj = 0; uint64_t tryAt = 0; bool shown = false; };
static struct DzBaseClient
{
    std::map<uint32_t, DzPieceC> pieces;
    bool  open = false;                  // build mode
    int   kind = DZB_FOUNDATION, tier = DZBT_WOOD, rot = 0;   // rot: extra 15 degree steps for a free foundation
    // the ghost
    bool  ok = false; char why[80] = ""; float gx = 0, gy = 0, gz = 0, gh = 0; uint32_t target = 0;
    uint64_t colAt = 0, sentAt = 0; int nObj = 0;
} s_DzB;

static int DzBaseHost(int i) { static const int k[] = { 1253, 1254, 1272, 1273, 1274, 1275, 1313 }; return k[i]; }
#define DZB_HOSTS 7

static bool DzBaseHasToolbox()
{
    for(int i = 0; i < s_Dz.inv.n && i < DZ_INV_MAX; ++i) if(s_Dz.inv.s[i].item == DZI_TOOLBOX) return true;
    return false;
}
static void DzBaseDelObj(DzPieceC& p) { if(p.obj > 0) { Game::DeleteWorldObject(p.obj); --s_DzB.nObj; } p.obj = 0; p.shown = false; }
static void DzBaseClear()
{
    for(auto& kv : s_DzB.pieces) DzBaseDelObj(kv.second);
    s_DzB.pieces.clear(); s_DzB.nObj = 0; s_DzB.open = false;
}
static float DzBaseHead(const DzBaseRow& r) { return r.head10 / 10.0f; }
// where the object of a piece goes (a door turns 90 degrees on its hinge when it is open)
static void DzBasePose(const DzBaseRow& r, Vec3& p, float& rx, float& ry, float& rz)
{
    p = { r.x, r.y, r.z }; rx = 0; ry = DzPieceFlat(r.kind) ? 90.0f : 0.0f; rz = DzBaseHead(r);
    if(DzPieceFlat(r.kind) && (r.id & 1)) p.z += 0.006f;   // neighbour tiles overlap a bit: no flicker
    if(r.kind == DZB_DOOR && (r.flags & 1)) rz += 90.0f;
}

static void DzBaseOnPacket(const uint8_t* d, int len)
{
    if(len < (int)offsetof(PktDzBase, r)) return;
    const PktDzBase* P = (const PktDzBase*)d;
    int n = std::min<int>(P->n, DZB_PAGE);
    if(len < (int)offsetof(PktDzBase, r) + n * (int)sizeof(DzBaseRow)) n = (len - (int)offsetof(PktDzBase, r)) / (int)sizeof(DzBaseRow);
    if(P->op == DZBO_LIST) { for(auto& kv : s_DzB.pieces) DzBaseDelObj(kv.second); s_DzB.pieces.clear(); }
    for(int i = 0; i < n; ++i)
    {
        DzBaseRow r; memcpy(&r, &P->r[i], sizeof(r));
        auto it = s_DzB.pieces.find(r.id);
        if(P->op == DZBO_DEL) { if(it != s_DzB.pieces.end()) { DzBaseDelObj(it->second); s_DzB.pieces.erase(it); } continue; }
        if(r.kind >= DZB_COUNT || r.tier >= DZBT_COUNT) continue;
        if(it == s_DzB.pieces.end()) { DzPieceC c; c.r = r; s_DzB.pieces[r.id] = c; continue; }
        DzPieceC& c = it->second; const DzBaseRow o = c.r; c.r = r;
        if(c.obj > 0 && (o.kind != r.kind || o.tier != r.tier || fabsf(o.x - r.x) > 0.01f || fabsf(o.y - r.y) > 0.01f || fabsf(o.z - r.z) > 0.01f)) DzBaseDelObj(c);
        else if(c.obj > 0 && (o.flags & 1) != (r.flags & 1)) { Vec3 p; float rx, ry, rz; DzBasePose(r, p, rx, ry, rz); Game::PlaceMapObject(c.obj, p, rx, ry, rz); }
    }
}

// every frame on Project Day Z: the collision of the pieces + the objects near me
static void DzBaseTick(const Vec3& me, uint64_t now)
{
    if(now - s_DzB.colAt > 1000)
    {   // the box collision of the 7 models (the game can put the GTA collision back when it streams)
        s_DzB.colAt = now;
        for(int i = 0; i < DZB_HOSTS; ++i) { DzBox b[8]; int n = DzModelBoxes(DzBaseHost(i), b); if(n) DzModels::SetBoxCollision(DzBaseHost(i), b[0].v, n, i >= 3 && i < 6 ? 4 : 0); }
    }
    int made = 0;
    for(auto& kv : s_DzB.pieces)
    {
        DzPieceC& c = kv.second; const DzBaseRow& r = c.r;
        const float dx = r.x - me.x, dy = r.y - me.y, d2 = dx * dx + dy * dy;
        if(c.obj > 0)
        {
            if(d2 > 230.0f * 230.0f || !Game::WorldObjectExists(c.obj)) DzBaseDelObj(c);
            continue;
        }
        if(d2 > 180.0f * 180.0f || made >= 3 || now < c.tryAt || s_DzB.nObj >= 400) continue;
        const int model = DzPieceModel(r.kind, r.tier);
        if(!DzModels::IsReplaced(model)) { c.tryAt = now + 1500; continue; }   // never show the GTA pickup model
        Vec3 p; float rx, ry, rz; DzBasePose(r, p, rx, ry, rz);
        int h = Game::CreateMapObject(model, p, rx, ry, rz);
        if(h > 0) { c.obj = h; c.shown = true; ++s_DzB.nObj; ++made; }
        else c.tryAt = now + (h < 0 ? 4000 : 250);
    }
}

// ---- the ghost: where the piece goes ----------------------------------------------------------
static bool DzBaseIsCell(const DzBaseRow& r) { return r.kind == DZB_FOUNDATION || r.kind == DZB_CEILING; }
static bool DzBaseIsWall(const DzBaseRow& r) { return r.kind == DZB_WALL || r.kind == DZB_DOORWAY || r.kind == DZB_WINDOW; }
static bool DzBaseTaken(bool cell, float x, float y, float z, float r2d, float rz)
{
    for(const auto& kv : s_DzB.pieces)
    {
        const DzBaseRow& r = kv.second.r;
        if(cell ? !DzBaseIsCell(r) : !DzBaseIsWall(r)) continue;
        float dx = r.x - x, dy = r.y - y; if(dx * dx + dy * dy < r2d * r2d && fabsf(r.z - z) < rz) return true;
    }
    return false;
}
static bool DzBaseWallsUnder(const DzBaseRow& cell)
{   // a wall on one of the edges of this cell (standing on it)
    for(const auto& kv : s_DzB.pieces)
    {
        const DzBaseRow& r = kv.second.r; if(!DzBaseIsWall(r)) continue;
        float dx = r.x - cell.x, dy = r.y - cell.y; float d = sqrtf(dx * dx + dy * dy);
        if(d < DZB_CELL * 0.5f + 0.25f && d > DZB_CELL * 0.5f - 0.25f && fabsf(r.z - (cell.z + DZB_HALF_T + DZB_WALL_H * 0.5f)) < 0.3f) return true;
    }
    return false;
}
static void DzBaseGhost(int ped, const Vec3& me)
{
    s_DzB.ok = false; s_DzB.why[0] = 0; s_DzB.target = 0;
    Vec3 cam, look; float yaw = Game::GetCharHeading(ped) + 90.0f;
    if(Game::GetCameraPos(cam, look)) { float fx = look.x - cam.x, fy = look.y - cam.y; if(fx * fx + fy * fy > 0.0001f) yaw = atan2f(fy, fx) * 57.29578f; }
    const float yr = yaw * 0.0174533f, fx = cosf(yr), fy = sinf(yr);
    const int k = s_DzB.kind;
    const float reach = k == DZB_FOUNDATION ? 4.2f : k == DZB_CEILING ? 3.0f : 2.4f;
    const float ax = me.x + fx * reach, ay = me.y + fy * reach;
    float best = 1e9f;
    auto consider = [&](float x, float y, float z, float h, float d) { if(d < best) { best = d; s_DzB.gx = x; s_DzB.gy = y; s_DzB.gz = z; s_DzB.gh = h; } };
    if(k == DZB_FOUNDATION)
    {
        for(const auto& kv : s_DzB.pieces)
        {
            const DzBaseRow& r = kv.second.r; if(r.kind != DZB_FOUNDATION) continue;
            float dx = r.x - me.x, dy = r.y - me.y; if(dx * dx + dy * dy > 14.0f * 14.0f) continue;
            const float h = DzBaseHead(r) * 0.0174533f, ux = cosf(h), uy = sinf(h);
            const float nb[4][2] = { { ux, uy }, { -ux, -uy }, { -uy, ux }, { uy, -ux } };
            for(auto& n : nb)
            {
                float x = r.x + n[0] * DZB_CELL, y = r.y + n[1] * DZB_CELL;
                if(DzBaseTaken(true, x, y, r.z, DZB_CELL * 0.5f, 0.6f)) continue;
                float ex = x - ax, ey = y - ay; consider(x, y, r.z, DzBaseHead(r), sqrtf(ex * ex + ey * ey));
            }
        }
        if(best < 2.6f) s_DzB.ok = true;
        else
        {   // a free foundation on the ground in front of me
            best = 0; const float h = yaw + s_DzB.rot * 15.0f, hr = h * 0.0174533f, ux = cosf(hr), uy = sinf(hr);
            float gmax = -1000, gmin = 1000;
            const float c[5][2] = { { 0, 0 }, { 1, 1 }, { 1, -1 }, { -1, 1 }, { -1, -1 } };
            for(auto& q : c)
            {
                float x = ax + (ux * q[0] - uy * q[1]) * 1.6f, y = ay + (uy * q[0] + ux * q[1]) * 1.6f;
                float g = Game::GroundZBelow(x, y, me.z + 3.0f); if(g < -900.0f) { gmin = -1000; break; }
                gmax = std::max(gmax, g); gmin = std::min(gmin, g);
            }
            s_DzB.gx = ax; s_DzB.gy = ay; s_DzB.gh = h; s_DzB.gz = gmax + DZB_HALF_T + 0.05f;
            if(gmin < -900.0f) snprintf(s_DzB.why, sizeof(s_DzB.why), "No ground here");
            else if(gmax - gmin > 1.5f) snprintf(s_DzB.why, sizeof(s_DzB.why), "The ground is too steep");
            else if(DzBaseTaken(true, ax, ay, s_DzB.gz, DZB_CELL * 0.95f, 1.5f)) snprintf(s_DzB.why, sizeof(s_DzB.why), "Too close to another foundation");
            else s_DzB.ok = true;
        }
    }
    else if(k == DZB_WALL || k == DZB_DOORWAY || k == DZB_WINDOW)
    {
        const float az = me.z + 1.2f;
        for(const auto& kv : s_DzB.pieces)
        {
            const DzBaseRow& r = kv.second.r; if(!DzBaseIsCell(r)) continue;
            float dx = r.x - me.x, dy = r.y - me.y; if(dx * dx + dy * dy > 12.0f * 12.0f) continue;
            const float hd = DzBaseHead(r), h = hd * 0.0174533f, ux = cosf(h), uy = sinf(h);
            const float z = r.z + DZB_HALF_T + DZB_WALL_H * 0.5f;
            const float ed[4][3] = { { ux, uy, hd }, { -ux, -uy, hd }, { -uy, ux, hd + 90.0f }, { uy, -ux, hd + 90.0f } };
            for(auto& e : ed)
            {
                float x = r.x + e[0] * DZB_CELL * 0.5f, y = r.y + e[1] * DZB_CELL * 0.5f;
                if(DzBaseTaken(false, x, y, z, 0.4f, 0.5f)) continue;
                float ex = x - ax, ey = y - ay, ez = z - az; consider(x, y, z, e[2], sqrtf(ex * ex + ey * ey + ez * ez));
            }
        }
        if(best < 3.2f) s_DzB.ok = true; else snprintf(s_DzB.why, sizeof(s_DzB.why), "Look at the edge of a foundation or a ceiling");
    }
    else if(k == DZB_CEILING)
    {
        const float az = me.z + 2.6f;
        for(const auto& kv : s_DzB.pieces)
        {
            const DzBaseRow& r = kv.second.r; if(!DzBaseIsCell(r)) continue;
            float dx = r.x - me.x, dy = r.y - me.y; if(dx * dx + dy * dy > 12.0f * 12.0f) continue;
            const float hd = DzBaseHead(r);
            if(DzBaseWallsUnder(r))
            {   // on top of the walls of this cell
                float z = r.z + DZB_WALL_H + 2.0f * DZB_HALF_T;
                if(!DzBaseTaken(true, r.x, r.y, z, DZB_CELL * 0.5f, 0.6f)) { float ex = r.x - ax, ey = r.y - ay, ez = z - az; consider(r.x, r.y, z, hd, sqrtf(ex * ex + ey * ey + ez * ez)); }
            }
            if(r.kind == DZB_CEILING)
            {   // next to another ceiling
                const float h = hd * 0.0174533f, ux = cosf(h), uy = sinf(h);
                const float nb[4][2] = { { ux, uy }, { -ux, -uy }, { -uy, ux }, { uy, -ux } };
                for(auto& n : nb)
                {
                    float x = r.x + n[0] * DZB_CELL, y = r.y + n[1] * DZB_CELL;
                    if(DzBaseTaken(true, x, y, r.z, DZB_CELL * 0.5f, 0.6f)) continue;
                    float ex = x - ax, ey = y - ay, ez = r.z - az; consider(x, y, r.z, hd, sqrtf(ex * ex + ey * ey + ez * ez));
                }
            }
        }
        if(best < 4.5f) s_DzB.ok = true; else snprintf(s_DzB.why, sizeof(s_DzB.why), "A ceiling needs walls under it (or another ceiling)");
    }
    else if(k == DZB_DOOR)
    {
        for(const auto& kv : s_DzB.pieces)
        {
            const DzBaseRow& r = kv.second.r; if(r.kind != DZB_DOORWAY) continue;
            const float h = DzBaseHead(r), hr = h * 0.0174533f;
            // the hinge: wall y -0.732 (the left side of the opening), the bottom of the opening
            const float x = r.x - sinf(hr) * -0.732f, y = r.y + cosf(hr) * -0.732f, z = r.z - DZB_WALL_H * 0.5f + 1.255f;
            bool has = false;
            for(const auto& k2 : s_DzB.pieces) { const DzBaseRow& q = k2.second.r; if(q.kind == DZB_DOOR && fabsf(q.x - x) < 0.3f && fabsf(q.y - y) < 0.3f && fabsf(q.z - z) < 0.5f) { has = true; break; } }
            if(has) continue;
            float ex = r.x - me.x, ey = r.y - me.y, ez = r.z - (me.z + 0.8f); consider(x, y, z, h + 90.0f, sqrtf(ex * ex + ey * ey + ez * ez));
        }
        if(best < 4.0f) s_DzB.ok = true; else snprintf(s_DzB.why, sizeof(s_DzB.why), "Stand next to a doorway without a door");
    }
    if(s_DzB.ok && !DzBaseHasToolbox()) { s_DzB.ok = false; snprintf(s_DzB.why, sizeof(s_DzB.why), "You need a Toolbox in your bag"); }
}
// the piece I look at (remove / door)
static uint32_t DzBaseAimed(const Vec3& me, bool doorsOnly)
{
    Vec3 cam, look; float fx = 0, fy = 0;
    if(Game::GetCameraPos(cam, look)) { fx = look.x - cam.x; fy = look.y - cam.y; float l = sqrtf(fx * fx + fy * fy); if(l > 0.001f) { fx /= l; fy /= l; } }
    uint32_t best = 0; float bd = 1e9f;
    for(const auto& kv : s_DzB.pieces)
    {
        const DzBaseRow& r = kv.second.r;
        if(doorsOnly ? r.kind != DZB_DOOR : !(r.flags & 2)) continue;
        float dx = r.x - me.x, dy = r.y - me.y, dz = r.z - (me.z + 0.6f); float d = sqrtf(dx * dx + dy * dy + dz * dz);
        if(d > 4.5f) continue;
        float along = dx * fx + dy * fy; if(along < -0.5f) continue;   // in front of me
        float s = d - along * 0.5f; if(s < bd) { bd = s; best = r.id; }
    }
    return best;
}

// ---- drawing the ghost ------------------------------------------------------------------------
static void DzBaseDrawBoxes(ImDrawList* dl, int model, bool flat, float x, float y, float z, float headDeg, ImU32 line, ImU32 fill)
{
    DzBox b[8]; int n = DzModelBoxes(model, b);
    const float h = headDeg * 0.0174533f, c = cosf(h), s = sinf(h);
    for(int i = 0; i < n; ++i)
    {
        ImVec2 sp[8]; bool vis = true;
        for(int k = 0; k < 8; ++k)
        {
            float lx = b[i].v[(k & 1) ? 3 : 0], ly = b[i].v[(k & 2) ? 4 : 1], lz = b[i].v[(k & 4) ? 5 : 2];
            if(flat) { float t = lx; lx = lz; lz = -t; }   // turned 90 degrees on y: x <- z, z <- -x
            Vec3 w = { x + lx * c - ly * s, y + lx * s + ly * c, z + lz };
            float sx, sy, dp; if(!Game::WorldToScreen(w, sx, sy, dp) || dp <= 0.05f) { vis = false; break; }
            sp[k] = ImVec2(sx, sy);
        }
        if(!vis) continue;
        static const int f[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
        for(auto& q : f) dl->AddQuadFilled(sp[q[0]], sp[q[1]], sp[q[2]], sp[q[3]], fill);
        static const int e[12][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 }, { 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
        for(auto& q : e) dl->AddLine(sp[q[0]], sp[q[1]], line, UI::S(1.6f));
    }
}

// the build bar (bottom middle) + the ghost
static void DzBaseDraw(ImDrawList* bg, ImVec2 ds, int ped)
{
    if(!s_DzB.open) return;
    if(s_DzX.downed || Game::IsCharInAnyCar(ped)) { s_DzB.open = false; return; }
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const uint64_t now = MP_NowMs();
    DzBaseGhost(ped, me);
    const ImU32 line = s_DzB.ok ? IM_COL32(110, 190, 255, 235) : IM_COL32(239, 68, 68, 235);
    const ImU32 fill = s_DzB.ok ? IM_COL32(70, 150, 255, 60) : IM_COL32(239, 68, 68, 55);
    if(s_DzB.ok || s_DzB.kind == DZB_FOUNDATION)
        DzBaseDrawBoxes(bg, DzPieceModel(s_DzB.kind, s_DzB.tier), DzPieceFlat(s_DzB.kind), s_DzB.gx, s_DzB.gy, s_DzB.gz, s_DzB.gh, line, fill);
    // the piece I would remove: outlined orange
    const uint32_t aimed = DzBaseAimed(me, false);
    if(aimed) { auto it = s_DzB.pieces.find(aimed); if(it != s_DzB.pieces.end()) { const DzBaseRow& r = it->second.r; Vec3 p; float rx, ry, rz; DzBasePose(r, p, rx, ry, rz);
        DzBaseDrawBoxes(bg, DzPieceModel(r.kind, r.tier), DzPieceFlat(r.kind), p.x, p.y, p.z, rz, IM_COL32(249, 115, 22, 120), IM_COL32(0, 0, 0, 0)); } }

    // the bar: 6 pieces, wood / concrete, rotate, PLACE, REMOVE, DOOR, X
    const float bs = UI::S(64), gap = UI::S(8), bw = 6 * bs + 5 * gap;
    const float x0 = (ds.x - bw) * 0.5f, y0 = ds.y - UI::S(190);
    ImGui::SetNextWindowPos(ImVec2(x0 - UI::S(12), y0 - UI::S(40))); ImGui::SetNextWindowSize(ImVec2(bw + UI::S(24), bs * 2 + gap + UI::S(52)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DzBuild", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    char top[128];
    if(s_DzB.ok) snprintf(top, sizeof(top), "%s %s", kDzTierName[s_DzB.tier], kDzPieceName[s_DzB.kind]);
    else snprintf(top, sizeof(top), "%s", s_DzB.why[0] ? s_DzB.why : "Can't build here");
    DzText(dl, UI::S(17), ImVec2(x0 + (bw - RpTextW(UI::S(17), top)) * 0.5f, y0 - UI::S(30)), s_DzB.ok ? IM_COL32_WHITE : DzCol(DZ_RED), top);
    const ImU32 wc = IM_COL32(255, 255, 255, 235), dk = IM_COL32(25, 25, 25, 255);
    auto button = [&](int id, ImVec2 a, ImVec2 sz, bool on) -> bool {
        ImGui::SetCursorScreenPos(a); char b[16]; snprintf(b, sizeof(b), "##dzb%d", id);
        bool hit = ImGui::InvisibleButton(b, sz);
        dl->AddRectFilled(a, ImVec2(a.x + sz.x, a.y + sz.y), ImGui::IsItemActive() ? IM_COL32(40, 40, 40, 190) : IM_COL32(0, 0, 0, 140), UI::S(10));
        if(on) dl->AddRect(a, ImVec2(a.x + sz.x, a.y + sz.y), IM_COL32(110, 190, 255, 230), UI::S(10), 0, UI::S(2.4f));
        return hit;
    };
    for(int i = 0; i < DZB_COUNT; ++i)
    {
        ImVec2 a(x0 + i * (bs + gap), y0);
        if(button(i, a, ImVec2(bs, bs), s_DzB.kind == i)) { s_DzB.kind = i; s_DzB.rot = 0; }
        // a small drawing of the piece
        const float cx = a.x + bs * 0.5f, cy = a.y + bs * 0.42f, r = bs * 0.26f, lw = UI::S(2.2f);
        switch(i)
        {
            case DZB_FOUNDATION: dl->AddQuadFilled(ImVec2(cx - r, cy + r * 0.2f), ImVec2(cx, cy - r * 0.35f), ImVec2(cx + r, cy + r * 0.2f), ImVec2(cx, cy + r * 0.75f), wc); break;
            case DZB_WALL: dl->AddRectFilled(ImVec2(cx - r * 0.8f, cy - r), ImVec2(cx + r * 0.8f, cy + r), wc); break;
            case DZB_DOORWAY: dl->AddRectFilled(ImVec2(cx - r * 0.8f, cy - r), ImVec2(cx + r * 0.8f, cy + r), wc); dl->AddRectFilled(ImVec2(cx - r * 0.3f, cy - r * 0.2f), ImVec2(cx + r * 0.3f, cy + r), dk); break;
            case DZB_WINDOW: dl->AddRectFilled(ImVec2(cx - r * 0.8f, cy - r), ImVec2(cx + r * 0.8f, cy + r), wc); dl->AddRectFilled(ImVec2(cx - r * 0.4f, cy - r * 0.45f), ImVec2(cx + r * 0.4f, cy + r * 0.25f), dk); break;
            case DZB_CEILING: dl->AddQuadFilled(ImVec2(cx - r, cy - r * 0.35f), ImVec2(cx, cy - r * 0.9f), ImVec2(cx + r, cy - r * 0.35f), ImVec2(cx, cy + r * 0.2f), wc);
                              dl->AddLine(ImVec2(cx - r * 0.7f, cy - r * 0.2f), ImVec2(cx - r * 0.7f, cy + r), wc, lw); dl->AddLine(ImVec2(cx + r * 0.7f, cy - r * 0.2f), ImVec2(cx + r * 0.7f, cy + r), wc, lw); break;
            default: dl->AddRect(ImVec2(cx - r * 0.5f, cy - r), ImVec2(cx + r * 0.5f, cy + r), wc, 0, 0, lw); dl->AddCircleFilled(ImVec2(cx + r * 0.25f, cy), lw, wc); break;
        }
        const char* nm = i == DZB_WINDOW ? "Window" : kDzPieceName[i];
        dl->AddText(nullptr, UI::S(12), ImVec2(cx - RpTextW(UI::S(12), nm) * 0.5f, a.y + bs - UI::S(16)), wc, nm);
    }
    // second row: tier, rotate, PLACE, REMOVE, DOOR, close
    const float y1 = y0 + bs + gap;
    const char* lab[6] = { kDzTierName[s_DzB.tier], "Rotate", "PLACE", "Remove", "Door", "Close" };
    for(int i = 0; i < 6; ++i)
    {
        ImVec2 a(x0 + i * (bs + gap), y1);
        const bool place = i == 2;
        bool hit = button(10 + i, a, ImVec2(bs, bs * 0.62f), false);
        if(place) dl->AddRectFilled(a, ImVec2(a.x + bs, a.y + bs * 0.62f), s_DzB.ok ? IM_COL32(37, 99, 235, 210) : IM_COL32(70, 70, 70, 170), UI::S(10));
        const float fs = UI::S(i == 2 ? 15 : 13);
        dl->AddText(nullptr, fs, ImVec2(a.x + (bs - RpTextW(fs, lab[i])) * 0.5f, a.y + (bs * 0.62f - fs) * 0.5f), wc, lab[i]);
        if(!hit) continue;
        switch(i)
        {
            case 0: s_DzB.tier = (s_DzB.tier + 1) % DZBT_COUNT; break;
            case 1: s_DzB.rot = (s_DzB.rot + 1) % 24; break;   // a free foundation turns 15 degrees
            case 2:
                if(!s_DzB.ok) { DzNotice(DZ_RED, "%s", s_DzB.why[0] ? s_DzB.why : "Can't build here"); break; }
                if(now - s_DzB.sentAt < 400) break;
                s_DzB.sentAt = now;
                {
                    PktDzCmd c; memset(&c, 0, sizeof(c)); c.type = PKT_DZ_CMD; c.cmd = DZCMD_BASE_PLACE; c.a = (uint8_t)s_DzB.kind; c.b = (uint8_t)s_DzB.tier;
                    float h = fmodf(s_DzB.gh, 360.0f); if(h < 0) h += 360.0f;
                    c.val = (int32_t)lroundf(h * 10.0f); c.pos[0] = s_DzB.gx; c.pos[1] = s_DzB.gy; c.pos[2] = s_DzB.gz;
                    client.Send(&c, sizeof(c));
                }
                break;
            case 3: { uint32_t id = DzBaseAimed(me, false); if(id) DzSend(DZCMD_BASE_DEL, 0, 0, (int32_t)id); else DzNotice(DZ_GRY, "Look at one of your pieces to remove it."); break; }
            case 4: { uint32_t id = DzBaseAimed(me, true); if(id) DzSend(DZCMD_BASE_DOOR, 0, 0, (int32_t)id); else DzNotice(DZ_GRY, "No door near you."); break; }
            default: s_DzB.open = false; break;
        }
    }
    ImGui::End(); ImGui::PopStyleVar();
}

// outside the build mode: a small DOOR button when I stand at a door I can open
static void DzBaseDoorButton(ImVec2 ds, int ped)
{
    if(s_DzB.open || s_DzX.downed || Game::IsCharInAnyCar(ped)) return;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    uint32_t id = DzBaseAimed(me, true); if(!id) return;
    auto it = s_DzB.pieces.find(id); if(it == s_DzB.pieces.end() || !(it->second.r.flags & 2)) return;
    float dx = it->second.r.x - me.x, dy = it->second.r.y - me.y; if(dx * dx + dy * dy > 2.6f * 2.6f) return;
    const float w = UI::S(120), h = UI::S(46), x = (ds.x - w) * 0.5f, y = ds.y * 0.62f;
    ImGui::SetNextWindowPos(ImVec2(x, y)); ImGui::SetNextWindowSize(ImVec2(w, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DzDoor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoFocusOnAppearing);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    bool hit = ImGui::InvisibleButton("##dzdoor", ImVec2(w, h));
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), ImGui::IsItemActive() ? IM_COL32(40, 40, 40, 190) : IM_COL32(0, 0, 0, 140), UI::S(10));
    const char* t = (it->second.r.flags & 1) ? "CLOSE DOOR" : "OPEN DOOR";
    dl->AddText(nullptr, UI::S(15), ImVec2(x + (w - RpTextW(UI::S(15), t)) * 0.5f, y + (h - UI::S(15)) * 0.5f), IM_COL32_WHITE, t);
    if(hit) DzSend(DZCMD_BASE_DOOR, 0, 0, (int32_t)id);
    ImGui::End(); ImGui::PopStyleVar();
}

static void DzBaseToggle()
{
    if(!s_DzB.open && !settings.dzModels) { DzNotice(DZ_RED, "Turn on the Day Z models in the settings to build."); return; }
    s_DzB.open = !s_DzB.open;
    if(s_DzB.open)
    {
        s_Dz.invOpen = false;
        if(!DzBaseHasToolbox()) DzNotice(DZ_YEL, "Building needs a Toolbox in your bag (gas stations).");
        else DzNotice(DZ_GRN, "Build mode: pick a piece, look where it goes, PLACE.");
    }
}
