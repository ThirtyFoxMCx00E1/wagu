// 4.13.15: Host map editor, the phone side (net/hostmap.h). Included by mp.cpp after mp_dayz.inc.
//
// Everybody on a custom server sees the host's map objects (streamed in and out around you like the Day Z
// mapping). Only the HOST OWNER of that custom server (the phone that created it) can open the editor:
// /mapedit (or Server management > Map editor). Featured servers never run it.
//
// The editor works like SA-MP's object editor: create an object in front of you, tap one to select it, then
// drag the coloured handles of the gizmo - X red, Y green, Z blue - to move it, or switch to Rotate and drag
// them to turn it. The panel has fine +/- steps, snap to ground, duplicate, delete and clear all.

struct HmObj { DzMapRow r; int obj = 0; uint64_t tryAt = 0; };
static struct HostMapClient
{
    std::unordered_map<uint32_t, HmObj> objs;
    bool owner = false, open = false, rotate = false;
    int  nShown = 0; uint64_t scanAt = 0; uint16_t total = 0;
    uint32_t sel = 0; int model = 1238; int step = 1;
    // gizmo drag
    int drag = -1; float dragAcc = 0; uint64_t sentAt = 0; bool dirty = false;
    ImVec2 downAt = ImVec2(0, 0); bool downOnUi = false, wasDown = false;
    bool wantSelNew = false; uint64_t clearAskAt = 0;
} s_Hm;

// the gizmo's handles on screen, for the touch hook (another thread): a finger that lands on one is MP's
static std::mutex s_HmTouchMx;
static float s_HmHandles[3][2] = { { -1e9f, -1e9f }, { -1e9f, -1e9f }, { -1e9f, -1e9f } };
static float s_HmHandleR = 0;
bool MP_GizmoWantsTouch(float x, float y)
{
    std::lock_guard<std::mutex> lk(s_HmTouchMx);
    if(s_HmHandleR <= 0) return false;
    for(int a = 0; a < 3; ++a)
    {
        const float dx = x - s_HmHandles[a][0], dy = y - s_HmHandles[a][1];
        if(dx * dx + dy * dy <= s_HmHandleR * s_HmHandleR) return true;
    }
    return false;
}
static void HmSetHandles(const ImVec2* h, float r)
{
    std::lock_guard<std::mutex> lk(s_HmTouchMx);
    s_HmHandleR = r;
    for(int a = 0; a < 3; ++a) { s_HmHandles[a][0] = h ? h[a].x : -1e9f; s_HmHandles[a][1] = h ? h[a].y : -1e9f; }
}

static void HmDelObj(HmObj& c) { if(c.obj > 0) { Game::DeleteWorldObject(c.obj); --s_Hm.nShown; } c.obj = 0; }
static void HmReset()
{
    for(auto& kv : s_Hm.objs) HmDelObj(kv.second);
    s_Hm.objs.clear(); s_Hm.nShown = 0; s_Hm.total = 0; s_Hm.owner = false; s_Hm.open = false; s_Hm.sel = 0; s_Hm.drag = -1;
    HmSetHandles(nullptr, 0);
}

static void HmSend(uint8_t op, const DzMapRow* r)
{
    PktHostMap p; memset(&p, 0, sizeof(p)); p.type = PKT_HOST_MAP; p.op = op;
    int n = (int)offsetof(PktHostMap, r);
    if(r) { p.n = 1; p.r[0] = *r; n += (int)sizeof(DzMapRow); }
    client.Send(&p, n);
}

static void HmOnPacket(const uint8_t* d, int len)
{
    if(len < (int)offsetof(PktHostMap, r)) return;
    const PktHostMap* P = (const PktHostMap*)d;
    if(P->op == HMO_PERM)
    {
        const bool was = s_Hm.owner;
        s_Hm.owner = P->pad == 1;
        if(s_Hm.owner && !was) SysMsg(COL_SYSTEM, "You own this server: /mapedit opens the map editor (add, move, turn and delete objects).");
        if(!s_Hm.owner) s_Hm.open = false;
        return;
    }
    int n = std::min<int>(P->n, DZM_PAGE);
    if(len < (int)offsetof(PktHostMap, r) + n * (int)sizeof(DzMapRow)) n = (len - (int)offsetof(PktHostMap, r)) / (int)sizeof(DzMapRow);
    if(P->op == HMO_LIST) { for(auto& kv : s_Hm.objs) HmDelObj(kv.second); s_Hm.objs.clear(); }
    s_Hm.total = P->total;
    for(int i = 0; i < n; ++i)
    {
        DzMapRow r; memcpy(&r, &P->r[i], sizeof(r));
        auto it = s_Hm.objs.find(r.id);
        if(P->op == HMO_DEL) { if(it != s_Hm.objs.end()) { HmDelObj(it->second); s_Hm.objs.erase(it); } if(s_Hm.sel == r.id) s_Hm.sel = 0; continue; }
        if(P->op == HMO_SET && P->pad == 1 && s_Hm.wantSelNew) { s_Hm.sel = r.id; s_Hm.wantSelNew = false; }
        if(it == s_Hm.objs.end()) { HmObj c; c.r = r; s_Hm.objs[r.id] = c; continue; }
        HmObj& c = it->second;
        if(r.id == s_Hm.sel && (s_Hm.drag >= 0 || s_Hm.dirty)) continue;   // my own edit in flight: keep the local one
        const DzMapRow o = c.r; c.r = r;
        if(c.obj > 0 && o.model != r.model) HmDelObj(c);
        else if(c.obj > 0) Game::PlaceMapObject(c.obj, { r.x, r.y, r.z }, r.rx10 / 10.0f, r.ry10 / 10.0f, r.rz10 / 10.0f);
    }
}

static HmObj* HmSel() { if(!s_Hm.sel) return nullptr; auto it = s_Hm.objs.find(s_Hm.sel); return it == s_Hm.objs.end() ? nullptr : &it->second; }

// moved / turned locally: the object moves at once, the server hears it at most ~8 times a second
static void HmTouched(HmObj& c, uint64_t now)
{
    if(c.obj > 0) Game::PlaceMapObject(c.obj, { c.r.x, c.r.y, c.r.z }, c.r.rx10 / 10.0f, c.r.ry10 / 10.0f, c.r.rz10 / 10.0f);
    s_Hm.dirty = true;
    if(now - s_Hm.sentAt >= 120) { HmSend(HMO_EDIT, &c.r); s_Hm.sentAt = now; s_Hm.dirty = false; }
}

static void HmProcess(int ped, uint64_t now)
{
    if(client.GetState() != NET_CONNECTED) { if(!s_Hm.objs.empty() || s_Hm.owner) HmReset(); return; }
    if(s_Hm.dirty && s_Hm.drag < 0 && now - s_Hm.sentAt >= 120) { if(HmObj* c = HmSel()) HmSend(HMO_EDIT, &c->r); s_Hm.sentAt = now; s_Hm.dirty = false; }
    if(s_Hm.objs.empty() || ped <= 0 || now - s_Hm.scanAt < 250 || !Game::IsReady() || Game::IsLoadPending()) return;   // never while the world loads
    s_Hm.scanAt = now;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    const int area = Game::GetAreaVisible();
    int made = 0;
    for(auto& kv : s_Hm.objs)
    {
        HmObj& c = kv.second; const DzMapRow& r = c.r;
        const float dx = r.x - me.x, dy = r.y - me.y, d2 = dx * dx + dy * dy;
        if(c.obj > 0)
        {
            if(d2 > DZM_STREAM_OUT * DZM_STREAM_OUT || r.interior != area || !Game::WorldObjectExists(c.obj)) HmDelObj(c);
            continue;
        }
        if(d2 > DZM_STREAM_IN * DZM_STREAM_IN || r.interior != area || made >= 6 || now < c.tryAt || s_Hm.nShown >= DZM_MAX_SHOWN) continue;
        if(!Game::IsKnownModel(r.model)) { c.tryAt = now + 60000; continue; }
        int h = Game::CreateMapObject(r.model, { r.x, r.y, r.z }, r.rx10 / 10.0f, r.ry10 / 10.0f, r.rz10 / 10.0f);
        if(h > 0) { c.obj = h; ++s_Hm.nShown; ++made; if(r.flags & DZMF_NOCOL) Game::SetObjectCollision(h, false); }
        else c.tryAt = now + (h < 0 ? 4000 : 200);
    }
}

// ---- editing ----------------------------------------------------------------------------------
static bool HmCanEdit(bool say = true)
{
    if(client.GetState() != NET_CONNECTED) { if(say) SysMsg(COL_RED, "Join your server first."); return false; }
    if(MP::IsOnDefault() || MP_IsBcwServerName(client.GetServerName()) || MP_IsMw3ServerName(client.GetServerName()) || MP_IsRpServerName(client.GetServerName()) || MP_IsDzServerName(client.GetServerName()) || MP_IsLgServerName(client.GetServerName()))
    { if(say) SysMsg(COL_RED, "The map editor is only for custom servers."); return false; }
    if(client.GetServerBuild() < 41315) { if(say) SysMsg(COL_YELLOW, "This server runs an older MP: it can't keep map objects yet."); return false; }
    if(!s_Hm.owner) { if(say) SysMsg(COL_RED, "Only the host owner of this server can edit its map."); return false; }
    return true;
}
static float HmGroundZ(int model, float x, float y, float zFrom)
{
    const float g = Game::GroundZBelow(x, y, zFrom);
    float mb = Game::ModelBottom(model); if(mb > 0.0f) mb = 0.0f;
    return g > -999.0f ? g - mb : zFrom - 1.0f;
}
static void HmAddInFront(int model, float dist = 4.0f)
{
    if(!HmCanEdit()) return;
    if(model <= 0 || model >= 20000) { SysMsg(COL_RED, "Unknown model id."); return; }
    if(!Game::IsKnownModel(model)) { SysMsg(COL_YELLOW, "Model %d is not in this game.", model); return; }
    const int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return;
    const float h = Game::GetCharHeading(ped), rad = h * 0.0174533f;
    DzMapRow r; memset(&r, 0, sizeof(r));
    r.model = (uint16_t)model; r.interior = (uint8_t)Game::GetAreaVisible();
    r.x = me.x - sinf(rad) * dist; r.y = me.y + cosf(rad) * dist; r.z = HmGroundZ(model, r.x, r.y, me.z + 2.0f);
    if(r.z < me.z - 30.0f || r.z > me.z + 6.0f) r.z = me.z - 1.0f;
    r.rz10 = DzMapAng10(h);
    s_Hm.wantSelNew = true;
    HmSend(HMO_ADD, &r);
    SysMsg(COL_GREEN, "The object was successfully created at your position.");
}
static void HmSelectNearest()
{
    int ped = Game::LocalPed(); Vec3 me; if(ped <= 0 || !Game::GetCharPos(ped, me)) return;
    float best = 1e9f; uint32_t id = 0;
    for(auto& kv : s_Hm.objs)
    {
        const DzMapRow& r = kv.second.r; const float dx = r.x - me.x, dy = r.y - me.y, dz = r.z - me.z, d = dx * dx + dy * dy + dz * dz;
        if(d < best) { best = d; id = kv.first; }
    }
    s_Hm.sel = id;
    if(!id) SysMsg(COL_YELLOW, "No map objects yet - Create one first.");
}
static void HmMove(HmObj& c, int axis, float v, uint64_t now)
{
    if(axis == 0) c.r.x += v; else if(axis == 1) c.r.y += v; else if(axis == 2) c.r.z += v;
    else
    {
        int16_t* a = axis == 3 ? &c.r.rx10 : axis == 4 ? &c.r.ry10 : &c.r.rz10;
        *a = DzMapAng10(*a / 10.0f + v);
    }
    HmTouched(c, now);
}

// the 3D gizmo (drawn on the background, under the MP windows)
static void HmGizmo(ImDrawList* dl, uint64_t now)
{
    HmObj* c = HmSel();
    if(!s_Hm.open || !c) { HmSetHandles(nullptr, 0); s_Hm.drag = -1; return; }
    const ImGuiIO& io = ImGui::GetIO();
    const Vec3 P = { c->r.x, c->r.y, c->r.z };
    float sx, sy, dep;
    if(!Game::WorldToScreen(P, sx, sy, dep) || dep <= 0.1f) { HmSetHandles(nullptr, 0); return; }
    Vec3 cam, look; float dist = 10.0f;
    if(Game::GetCameraPos(cam, look)) { const float dx = P.x - cam.x, dy = P.y - cam.y, dz = P.z - cam.z; dist = sqrtf(dx * dx + dy * dy + dz * dz); }
    const float L = fmaxf(1.2f, dist * 0.14f);
    const ImVec2 o(sx, sy);
    static const ImU32 kCol[3] = { IM_COL32(235, 60, 60, 255), IM_COL32(60, 210, 80, 255), IM_COL32(70, 130, 255, 255) };
    ImVec2 end[3]; float ppm[3]; ImVec2 dir[3];
    for(int a = 0; a < 3; ++a)
    {
        Vec3 q = P; (a == 0 ? q.x : a == 1 ? q.y : q.z) += L;
        float ex, ey, ed;
        if(!Game::WorldToScreen(q, ex, ey, ed) || ed <= 0.1f) { ex = sx; ey = sy - (a + 1) * UI::S(40); }
        end[a] = ImVec2(ex, ey);
        const float vx = ex - sx, vy = ey - sy, len = sqrtf(vx * vx + vy * vy);
        ppm[a] = fmaxf(len / L, 1.0f);
        dir[a] = len > 1.0f ? ImVec2(vx / len, vy / len) : ImVec2(0, -1);
    }
    const float hr = UI::S(30);
    HmSetHandles(end, hr * 1.25f);
    // the drag: starts on a handle, follows the finger along that axis on the screen
    if(io.MouseClicked[0] && s_Hm.drag < 0)
    {
        for(int a = 0; a < 3; ++a)
        {
            const float dx = io.MousePos.x - end[a].x, dy = io.MousePos.y - end[a].y;
            if(dx * dx + dy * dy <= hr * hr * 1.6f) { s_Hm.drag = a; s_Hm.dragAcc = 0; break; }
        }
    }
    if(s_Hm.drag >= 0)
    {
        const int a = s_Hm.drag;
        if(!io.MouseDown[0]) { s_Hm.drag = -1; HmSend(HMO_EDIT, &c->r); s_Hm.sentAt = now; s_Hm.dirty = false; }
        else
        {
            const float along = io.MouseDelta.x * dir[a].x + io.MouseDelta.y * dir[a].y;
            if(s_Hm.rotate) { if(fabsf(along) > 0.01f) HmMove(*c, 3 + a, along * 0.6f, now); }
            else if(fabsf(along) > 0.01f) HmMove(*c, a, along / ppm[a], now);
        }
    }
    // draw
    dl->AddCircleFilled(o, UI::S(7), IM_COL32(255, 255, 255, 230));
    static const char* const kAx[3] = { "X", "Y", "Z" };
    for(int a = 0; a < 3; ++a)
    {
        const bool hot = s_Hm.drag == a;
        dl->AddLine(o, end[a], kCol[a], UI::S(hot ? 6 : 4));
        if(s_Hm.rotate) dl->AddCircle(end[a], hr, kCol[a], 24, UI::S(5));
        else dl->AddCircleFilled(end[a], hr * (hot ? 1.15f : 1.0f), kCol[a]);
        dl->AddText(ImVec2(end[a].x - UI::S(6), end[a].y - UI::S(10)), IM_COL32(255, 255, 255, 255), kAx[a]);
    }
}

// tap on an object (not on a window / handle) = select it
static void HmTapSelect()
{
    const ImGuiIO& io = ImGui::GetIO();
    if(io.MouseClicked[0]) { s_Hm.downAt = io.MousePos; s_Hm.downOnUi = io.WantCaptureMouse || s_Hm.drag >= 0; }
    if(!io.MouseReleased[0] || s_Hm.downOnUi) return;
    const float mx = io.MousePos.x - s_Hm.downAt.x, my = io.MousePos.y - s_Hm.downAt.y;
    if(mx * mx + my * my > UI::S(20) * UI::S(20)) return;   // a swipe (camera), not a tap
    float best = UI::S(80) * UI::S(80); uint32_t id = 0;
    for(auto& kv : s_Hm.objs)
    {
        if(kv.second.obj <= 0) continue;
        float sx, sy, d; const DzMapRow& r = kv.second.r;
        if(!Game::WorldToScreen({ r.x, r.y, r.z }, sx, sy, d) || d <= 0.1f) continue;
        const float dx = sx - io.MousePos.x, dy = sy - io.MousePos.y, d2 = dx * dx + dy * dy;
        if(d2 < best) { best = d2; id = kv.first; }
    }
    if(id) s_Hm.sel = id;
}

struct HmPopular { uint16_t model; const char* name; };
static const HmPopular kHmPopular[] = {
    { 1238, "Traffic cone" }, { 1225, "Explosive barrel" }, { 3578, "Dock barrier" }, { 1634, "Jump ramp" },
    { 1655, "Big ramp" }, { 1632, "Water ramp" }, { 983, "Chainlink fence" }, { 987, "Electric fence" },
    { 970, "Small fence" }, { 1215, "Bollard light" }, { 1281, "Picnic table" }, { 1290, "Street lamp" },
    { 3279, "Spotlight tower" }, { 1312, "Overhead sign frame" }, { 19425, "Speed bump (SA-MP pack)" },
};
#define HM_POPULAR_N ((int)(sizeof(kHmPopular) / sizeof(kHmPopular[0])))

static void HmRender()
{
    const uint64_t now = MP_NowMs();
    if(!s_Hm.open) { HmSetHandles(nullptr, 0); return; }
    if(!HmCanEdit(false)) { s_Hm.open = false; HmSetHandles(nullptr, 0); return; }
    HmGizmo(ImGui::GetBackgroundDrawList(), now);
    HmTapSelect();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float ww = fminf(UI::S(600), ds.x * 0.46f);
    ImGui::SetNextWindowPos(ImVec2(ds.x - ww - UI::S(16), UI::S(90)), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(ww, ds.y - UI::S(180)), ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.78f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0.92f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const ImVec2 bs(UI::S(74), UI::S(54));
    if(ImGui::Begin("##hmedit", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::TextColored(ImVec4(1, 0.8f, 0.2f, 1), "MAP EDITOR");
        ImGui::SameLine(); ImGui::TextDisabled("%d objects on this server", (int)s_Hm.objs.size());
        // the model to create
        if(ImGui::Button("-100##m", bs)) s_Hm.model = std::max(1, s_Hm.model - 100); ImGui::SameLine();
        if(ImGui::Button("-1##m", bs)) s_Hm.model = std::max(1, s_Hm.model - 1); ImGui::SameLine();
        ImGui::Text("%5d", s_Hm.model); ImGui::SameLine();
        if(ImGui::Button("+1##m", bs)) s_Hm.model = std::min(19999, s_Hm.model + 1); ImGui::SameLine();
        if(ImGui::Button("+100##m", bs)) s_Hm.model = std::min(19999, s_Hm.model + 100);
        {
            const bool known = Game::IsKnownModel(s_Hm.model);
            const char* nm = nullptr; for(int i = 0; i < HM_POPULAR_N; ++i) if(kHmPopular[i].model == s_Hm.model) nm = kHmPopular[i].name;
            ImGui::TextColored(known ? ImVec4(0.5f, 1, 0.6f, 1) : ImVec4(1, 0.45f, 0.4f, 1), "%s%s", nm ? nm : (known ? "model in this game" : "not in this game"), "");
        }
        static int s_nPop = 0;
        if(ImGui::Button("<##pop", bs)) { s_nPop = (s_nPop + HM_POPULAR_N - 1) % HM_POPULAR_N; s_Hm.model = kHmPopular[s_nPop].model; } ImGui::SameLine();
        ImGui::Text("%s", kHmPopular[s_nPop].name); ImGui::SameLine();
        if(ImGui::Button(">##pop", bs)) { s_nPop = (s_nPop + 1) % HM_POPULAR_N; s_Hm.model = kHmPopular[s_nPop].model; }
        if(ImGui::Button("Create in front of me", ImVec2(-1, UI::S(58)))) HmAddInFront(s_Hm.model);
        ImGui::Separator();
        HmObj* c = HmSel();
        if(ImGui::Button("Nearest", ImVec2(UI::S(170), UI::S(54)))) HmSelectNearest();
        ImGui::SameLine();
        if(c) ImGui::Text("#%u  model %u", c->r.id, c->r.model); else ImGui::TextDisabled("Tap an object to select it");
        if(c)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, !s_Hm.rotate ? ImVec4(0.1f, 0.4f, 0.7f, 1) : ImVec4(0, 0, 0, 0.92f));
            if(ImGui::Button("Move", ImVec2(UI::S(150), UI::S(52)))) s_Hm.rotate = false;
            ImGui::PopStyleColor(); ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, s_Hm.rotate ? ImVec4(0.1f, 0.4f, 0.7f, 1) : ImVec4(0, 0, 0, 0.92f));
            if(ImGui::Button("Rotate", ImVec2(UI::S(150), UI::S(52)))) s_Hm.rotate = true;
            ImGui::PopStyleColor();
            static const float kSteps[3] = { 0.05f, 0.25f, 1.0f };
            static const float kAngSteps[3] = { 1.0f, 5.0f, 15.0f };
            for(int i = 0; i < 3; ++i)
            {
                if(i) ImGui::SameLine();
                char l[24]; snprintf(l, sizeof(l), "%.2g m / %.0f\xC2\xB0", kSteps[i], kAngSteps[i]);
                ImGui::PushStyleColor(ImGuiCol_Button, s_Hm.step == i ? ImVec4(0.1f, 0.4f, 0.7f, 1) : ImVec4(0, 0, 0, 0.92f));
                if(ImGui::Button(l, ImVec2(UI::S(170), UI::S(50)))) s_Hm.step = i;
                ImGui::PopStyleColor();
            }
            static const char* const kAx[6] = { "X", "Y", "Z", "Rot X", "Rot Y", "Rot Z" };
            for(int a = 0; a < 6; ++a)
            {
                ImGui::PushID(a);
                const float st = a < 3 ? kSteps[s_Hm.step] : kAngSteps[s_Hm.step];
                ImGui::Text("%s", kAx[a]); ImGui::SameLine(UI::S(120));
                ImGui::Button("-", bs); if(ImGui::IsItemActive() && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDownDuration[0] > 0.35f)) HmMove(*c, a, -st * (ImGui::IsItemActivated() ? 1 : 0.25f), now);
                ImGui::SameLine();
                ImGui::Button("+", bs); if(ImGui::IsItemActive() && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDownDuration[0] > 0.35f)) HmMove(*c, a, st * (ImGui::IsItemActivated() ? 1 : 0.25f), now);
                ImGui::SameLine();
                if(a < 3) ImGui::TextDisabled("%.2f", a == 0 ? c->r.x : a == 1 ? c->r.y : c->r.z);
                else ImGui::TextDisabled("%.1f", (a == 3 ? c->r.rx10 : a == 4 ? c->r.ry10 : c->r.rz10) / 10.0f);
                ImGui::PopID();
            }
            if(ImGui::Button("Snap to ground", ImVec2(UI::S(250), UI::S(54)))) { c->r.z = HmGroundZ(c->r.model, c->r.x, c->r.y, c->r.z + 3.0f); c->r.rx10 = c->r.ry10 = 0; HmTouched(*c, now); }
            ImGui::SameLine();
            if(ImGui::Button("Duplicate", ImVec2(UI::S(200), UI::S(54)))) { DzMapRow r = c->r; r.x += 1.0f; r.y += 1.0f; s_Hm.wantSelNew = true; HmSend(HMO_ADD, &r); }
            {
                bool solid = !(c->r.flags & DZMF_NOCOL);
                if(FloatMenu::ToggleSwitch("Solid (collision)", &solid))
                { c->r.flags = (uint8_t)(solid ? (c->r.flags & ~DZMF_NOCOL) : (c->r.flags | DZMF_NOCOL)); if(c->obj > 0) Game::SetObjectCollision(c->obj, solid); HmTouched(*c, now); }
            }
            if(ImGui::Button("Delete", ImVec2(UI::S(200), UI::S(56)))) { DzMapRow r = c->r; HmSend(HMO_REMOVE, &r); s_Hm.sel = 0; }
            ImGui::SameLine();
            if(ImGui::Button("Deselect", ImVec2(UI::S(200), UI::S(56)))) s_Hm.sel = 0;
        }
        ImGui::Separator();
        const bool confirm = s_Hm.clearAskAt && now - s_Hm.clearAskAt < 4000;
        if(ImGui::Button(confirm ? "Tap again: delete ALL objects" : "Clear all", ImVec2(UI::S(330), UI::S(54))))
        { if(confirm) { HmSend(HMO_CLEAR, nullptr); s_Hm.sel = 0; s_Hm.clearAskAt = 0; } else s_Hm.clearAskAt = now; }
        ImGui::SameLine();
        if(ImGui::Button("Close", ImVec2(-1, UI::S(54)))) { s_Hm.open = false; s_Hm.sel = 0; }
        ImGui::TextDisabled("Drag the X / Y / Z handles on the object. Tap an object to select it.");
    }
    ImGui::End();
    ImGui::PopStyleVar(2); ImGui::PopStyleColor(2);
}

static void HmOpenEditor()
{
    if(!HmCanEdit()) return;
    s_Hm.open = !s_Hm.open;
    if(s_Hm.open) SysMsg(COL_SYSTEM, "Map editor: Create an object, or tap one to select it. You now are able to edit your object.");
}

static bool HmCommand(const char* cmd, const char* arg)
{
    if(strcmp(cmd, "mapedit") && strcmp(cmd, "editmap") && strcmp(cmd, "hostmap")) return false;
    while(arg && *arg == ' ') ++arg;
    if(!arg || !*arg) { HmOpenEditor(); return true; }
    if(!strncasecmp(arg, "help", 4))
    {
        SysMsg(COL_YELLOW, "/mapedit - open / close the editor.  /mapedit add <id|name>  /mapedit model <id>  /mapedit find <name>");
        SysMsg(COL_YELLOW, "/mapedit select (nearest)  /mapedit delete  /mapedit count.  Only the host owner of a custom server can edit.");
        return true;
    }
    if(!strncasecmp(arg, "find ", 5)) { const char* q = arg + 5; while(*q == ' ') ++q; if(*q) DzCatFind(q); return true; }
    if(!strncasecmp(arg, "count", 5)) { SysMsg(COL_SYSTEM, "%d map objects on this server (%d shown near you).", (int)s_Hm.objs.size(), s_Hm.nShown); return true; }
    if(!HmCanEdit()) return true;
    if(!strncasecmp(arg, "add ", 4) || !strncasecmp(arg, "model ", 6))
    {
        const bool add = !strncasecmp(arg, "add ", 4);
        const char* a = arg + (add ? 4 : 6); while(*a == ' ') ++a;
        int model = isdigit((unsigned char)a[0]) ? atoi(a) : DzCatId(a);
        if(model == -2) { SysMsg(COL_YELLOW, "Loading the model catalog - try again in a moment (or use the id)."); return true; }
        if(model <= 0 || model >= 20000) { SysMsg(COL_RED, "Unknown model. /mapedit find <name> lists the ids."); return true; }
        s_Hm.model = model;
        if(add) HmAddInFront(model); else SysMsg(COL_SYSTEM, "Map editor model: %d.", model);
        if(!s_Hm.open) s_Hm.open = true;
        return true;
    }
    if(!strncasecmp(arg, "select", 6)) { HmSelectNearest(); s_Hm.open = true; if(s_Hm.sel) SysMsg(COL_SYSTEM, "Please select the object you'd like to edit - #%u picked.", s_Hm.sel); return true; }
    if(!strncasecmp(arg, "del", 3)) { if(HmObj* c = HmSel()) { DzMapRow r = c->r; HmSend(HMO_REMOVE, &r); s_Hm.sel = 0; } else SysMsg(COL_YELLOW, "Select an object first."); return true; }
    SysMsg(COL_YELLOW, "/mapedit help");
    return true;
}
