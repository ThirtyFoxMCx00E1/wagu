// 3.21: SA-MP style RED CHECKPOINT BEAMS (the tall red column you drive into)
//
// In a race / job: a tall red beam on the checkpoint you have to reach (and a faint one on the
// next), with "CHECKPOINT 3/12 - 240 m" over it, seen from far away and through buildings.
// In a derby / DM: a red ring at the edge of the arena / zone, and a countdown when you are out.
// Not in an event: a red beam on every job start, derby arena and DM zone of this server (the ones
// near you also get a red dot on the radar). Walk or drive into one and it starts by itself
// ("/job pizza" etc. still works from anywhere).

struct MkStart { int kind; int idx; Vec3 pos; char label[48]; char cmd[24]; };
enum { MK_JOB = 1, MK_DERBY, MK_DM };

static struct
{
    std::vector<MkStart> starts;
    bool     built = false;
    int      blips[6] = {0};
    int      blipFor[6] = {-1, -1, -1, -1, -1, -1};
    uint64_t lastBlips = 0;
    int      inside = -1;          // start marker the player stands in
    uint64_t insideSince = 0;
    bool     needExit = true;      // after an event / join: leave the marker first (no instant restart)
    bool     wasInEvent = false;
    Vec3     last = { 0, 0, 0 };   // 3.21.4: where I was last frame (spawn / respawn / teleport = a jump)
    bool     haveLast = false;
    uint64_t calmUntil = 0;        // no auto start before this (just joined / spawned / teleported)
} s_Mk;
#define MK_CALM_MS     4000

#define MK_BEAM_RGBA   0xFF1E1EFF   // SA-MP checkpoint red
#define MK_BEAM_H      60.0f
#define MK_TRIGGER_R   4.0f
#define MK_TRIGGER_MS  1200         // stand in it this long

// 3.21.4: no start marker on a spawn spot or a hospital (you'd be put into /dm grove just by
// spawning at Grove Street). Those still start with their command (/dm grove, /job taxi ...).
#define MK_SPAWN_CLEAR 30.0f
static bool MkNearSpawn(const Vec3& p)
{
    for(int i = 0; i < MP_SPAWN_COUNT; ++i)
        if(GDist2D(p, { kSpawns[i].x, kSpawns[i].y, kSpawns[i].z }) < MK_SPAWN_CLEAR) return true;
    for(int i = 0; i < g_MPSafeZoneCount; ++i)
        if(GDist2D(p, { g_MPSafeZones[i].x, g_MPSafeZones[i].y, g_MPSafeZones[i].z }) < MK_SPAWN_CLEAR + MP_SAFEZONE_RADIUS) return true;
    return GDist2D(p, kGroveStreet) < MK_SPAWN_CLEAR;
}
static void MkPush(const MkStart& m) { if(!MkNearSpawn(m.pos)) s_Mk.starts.push_back(m); }
static void MkBuild()
{
    if(s_Mk.built) return;
    s_Mk.built = true;
    for(int i = 0; i < MP_ROUTE_COUNT; ++i)
    {
        MkStart m; m.kind = MK_JOB; m.idx = i; m.pos = kRoutes[i].cp[0];
        snprintf(m.label, sizeof(m.label), "JOB: %s", kRoutes[i].name);
        snprintf(m.cmd, sizeof(m.cmd), "/job %s", kRoutes[i].cmd);
        MkPush(m);
    }
    for(int i = 0; i < MP_DERBY_COUNT; ++i)
    {
        if(kDerbies[i].area) continue;   // inside a stadium interior: /derby takes you there
        MkStart m; m.kind = MK_DERBY; m.idx = i; m.pos = { kDerbies[i].x, kDerbies[i].y, kDerbies[i].z };
        snprintf(m.label, sizeof(m.label), "DERBY: %s", kDerbies[i].name);
        snprintf(m.cmd, sizeof(m.cmd), "/derby %s", kDerbies[i].cmd);
        MkPush(m);
    }
    for(int i = 0; i < MP_DM_COUNT; ++i)
    {
        MkStart m; m.kind = MK_DM; m.idx = i; m.pos = { kDms[i].x, kDms[i].y, kDms[i].z };
        snprintf(m.label, sizeof(m.label), "DM: %s", kDms[i].name);
        snprintf(m.cmd, sizeof(m.cmd), "/dm %s", kDms[i].cmd);
        MkPush(m);
    }
}
static bool MkModeOn(int kind)
{
    return kind == MK_JOB ? MP::ModeOn(GM_JOBS) : kind == MK_DERBY ? MP::ModeOn(GM_DERBY) : MP::ModeOn(GM_DM);
}
static bool MkActive() { return client.GetState() == NET_CONNECTED && !BcwOnServer() && Game::IsReady() && !s_bLocalSeparate; }

// the red column: translucent cylinder, brighter at the bottom, with a ground ring
static void MkBeam(ImDrawList* dl, const Vec3& c, float r, float h, uint32_t rgba, float alpha)
{
    float d = BcwCamDist(c);
    if(d > 900.0f) return;
    float fade = d < 600 ? 1.0f : 1.0f - (d - 600) / 300.0f;
    {   // 3.21.5: camera in / right next to the beam: fade it out (it covered the screen in red)
        Vec3 cam, look;
        if(Game::GetCameraPos(cam, look))
        {
            float dx = cam.x - c.x, dy = cam.y - c.y, edge = sqrtf(dx * dx + dy * dy) - r;
            float k = edge <= 2.0f ? 0.0f : (edge >= 8.0f ? 1.0f : (edge - 2.0f) / 6.0f);
            if(cam.z > c.z + h + 2.0f) k = 1.0f;   // above it (helicopter): fine
            fade *= k;
            if(fade <= 0.01f) return;
        }
    }
    const int N = 20;
    ImVec2 g[N + 1], t[N + 1]; bool okG[N + 1], okT[N + 1];
    for(int i = 0; i <= N; ++i)
    {
        float a = (float)i / N * 6.2831853f;
        Vec3 b = { c.x + cosf(a) * r, c.y + sinf(a) * r, c.z - 0.5f };
        okG[i] = BcwW2S(b, g[i]);
        okT[i] = BcwW2S({ b.x, b.y, b.z + h }, t[i]);
    }
    ImU32 cb = BcwCol(rgba, 0.40f * alpha * fade), ct = BcwCol(rgba, 0.12f * alpha * fade);
    ImU32 cm = BcwCol(rgba, 0.26f * alpha * fade);
    ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    // 3.21.4: walls / houses hide it: 3 levels (ground, middle, top), each part only where it is in sight
    ImVec2 m[N + 1]; bool okM[N + 1];
    for(int i = 0; i <= N; ++i)
    {
        float a = (float)i / N * 6.2831853f;
        okM[i] = BcwW2S({ c.x + cosf(a) * r, c.y + sinf(a) * r, c.z - 0.5f + h * 0.5f }, m[i]);
    }
    float lv[3] = { c.z + 0.4f, c.z - 0.5f + h * 0.5f, c.z - 0.5f + h };
    const uint8_t* vis = OccSample(c, r, N, lv, 3, 2);
    auto quad = [&](const ImVec2& a0, const ImVec2& a1, const ImVec2& b1, const ImVec2& b0, ImU32 ca, ImU32 cbb)
    {
        dl->PrimReserve(6, 4);
        ImDrawIdx idx = (ImDrawIdx)dl->_VtxCurrentIdx;
        dl->PrimWriteVtx(a0, uv, ca); dl->PrimWriteVtx(a1, uv, ca);
        dl->PrimWriteVtx(b1, uv, cbb); dl->PrimWriteVtx(b0, uv, cbb);
        dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 1); dl->PrimWriteIdx(idx + 2);
        dl->PrimWriteIdx(idx); dl->PrimWriteIdx(idx + 2); dl->PrimWriteIdx(idx + 3);
    };
    for(int i = 0; i < N; ++i)
    {
        const uint8_t* v0 = vis; const uint8_t* v1 = vis + (N + 1); const uint8_t* v2 = vis + 2 * (N + 1);
        if(okG[i] && okG[i + 1] && okM[i] && okM[i + 1] && v0[i] && v0[i + 1] && v1[i] && v1[i + 1]) quad(g[i], g[i + 1], m[i + 1], m[i], cb, cm);
        if(okM[i] && okM[i + 1] && okT[i] && okT[i + 1] && v1[i] && v1[i + 1] && v2[i] && v2[i + 1]) quad(m[i], m[i + 1], t[i + 1], t[i], cm, ct);
        if(okG[i] && okG[i + 1] && v0[i] && v0[i + 1]) dl->AddLine(g[i], g[i + 1], BcwCol(rgba, 0.85f * alpha * fade), UI::S(2.5f));
    }
}
// 3.21.4: a 3D label that walls hide too
static void MkLabel(ImDrawList* dl, const Vec3& p, const char* text, uint32_t rgba, float size, float ref)
{
    if(!OccPoint(p)) return;
    BcwLabel3D(dl, p, text, rgba, size, ref);
}

static void MkReleaseBlips()
{
    for(int k = 0; k < 6; ++k) { if(s_Mk.blips[k]) Game::RemoveBlip(s_Mk.blips[k]); s_Mk.blips[k] = 0; s_Mk.blipFor[k] = -1; }
}
// radar: a red dot on the 6 nearest start markers within 800 m (refreshed every 3 s)
static void MkUpdateBlips(const Vec3& me, uint64_t now, bool show)
{
    if(!show) { if(s_Mk.blips[0] || s_Mk.blipFor[0] >= 0) MkReleaseBlips(); return; }
    if(now - s_Mk.lastBlips < 3000) return;
    s_Mk.lastBlips = now;
    int best[6]; float bd[6]; int nb = 0;
    for(int i = 0; i < (int)s_Mk.starts.size(); ++i)
    {
        const MkStart& m = s_Mk.starts[i];
        if(!MkModeOn(m.kind)) continue;
        float d = GDist2D(me, m.pos);
        if(d > 800.0f) continue;
        int k = nb < 6 ? nb++ : 5;
        if(k == 5 && nb == 6 && d >= bd[5]) continue;
        best[k] = i; bd[k] = d;
        for(int j = k; j > 0 && bd[j] < bd[j - 1]; --j) { std::swap(bd[j], bd[j - 1]); std::swap(best[j], best[j - 1]); }
    }
    for(int k = 0; k < 6; ++k)
    {
        int want = k < nb ? best[k] : -1;
        if(s_Mk.blipFor[k] == want) continue;
        if(s_Mk.blips[k]) { Game::RemoveBlip(s_Mk.blips[k]); s_Mk.blips[k] = 0; }
        s_Mk.blipFor[k] = want;
        if(want >= 0) s_Mk.blips[k] = Game::AddCoordBlip(s_Mk.starts[want].pos, MK_BEAM_RGBA, 1);
    }
}

// per frame (from RenderMarkers): start markers -> walk / drive in = start
static void MkProcessStarts(const Vec3& me, uint64_t now)
{
    int in = -1;
    for(int i = 0; i < (int)s_Mk.starts.size(); ++i)
    {
        const MkStart& m = s_Mk.starts[i];
        if(!MkModeOn(m.kind)) continue;
        float dz = me.z - m.pos.z;
        if(GDist2D(me, m.pos) < MK_TRIGGER_R && dz > -6.0f && dz < 8.0f) { in = i; break; }
    }
    if(in < 0) { s_Mk.inside = -1; if(now >= s_Mk.calmUntil) s_Mk.needExit = false; return; }
    if(s_Mk.needExit || now < s_Mk.calmUntil) { s_Mk.needExit = true; s_Mk.inside = -1; return; }
    if(in != s_Mk.inside) { s_Mk.inside = in; s_Mk.insideSince = now; return; }
    if(now - s_Mk.insideSince < MK_TRIGGER_MS) return;
    const MkStart& m = s_Mk.starts[in];
    s_Mk.needExit = true; s_Mk.inside = -1;
    if(m.kind == MK_JOB) StartRoute(m.idx);
    else if(m.kind == MK_DERBY) StartDerby(m.idx);
    else StartDm(m.idx);
}

static void MkCalm(uint64_t now) { s_Mk.needExit = true; s_Mk.inside = -1; s_Mk.calmUntil = now + MK_CALM_MS; }
static void RenderMarkers()
{
    if(!MkActive() || Game::IsPaused() || JoinFlowActive())
    {   // 3.21.4: not playing yet (connecting, spawn picker): the next start needs a real walk in
        if(s_Mk.blipFor[0] >= 0) MkReleaseBlips();
        if(!Game::IsPaused()) { s_Mk.haveLast = false; MkCalm(MP_NowMs()); }
        return;
    }
    MkBuild();
    int ped = Game::LocalPed(); if(ped <= 0) return;
    Vec3 me; if(!Game::GetCharPos(ped, me)) return;
    uint64_t nowMs = MP_NowMs();
    {   // 3.21.4: spawned / respawned / teleported (a jump of 12 m+ in one frame) or dead: no auto start there
        float dx = me.x - s_Mk.last.x, dy = me.y - s_Mk.last.y, dz = me.z - s_Mk.last.z;
        if(!s_Mk.haveLast || dx * dx + dy * dy + dz * dz > 144.0f || Game::IsCharDead(ped)) MkCalm(nowMs);
        s_Mk.last = me; s_Mk.haveLast = true;
    }
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float now = (float)(nowMs % 100000000ULL);
    bool inEv = InEvent();
    if(s_Mk.wasInEvent && !inEv) s_Mk.needExit = true;   // just finished / left: don't start again at once
    s_Mk.wasInEvent = inEv;

    if(!inEv)
    {
        MkUpdateBlips(me, nowMs, !s_RQ.on);
        bool dead = Game::IsCharDead(ped);
        for(int i = 0; i < (int)s_Mk.starts.size(); ++i)
        {
            const MkStart& m = s_Mk.starts[i];
            if(!MkModeOn(m.kind) || BcwCamDist(m.pos) > 450.0f) continue;
            MkBeam(bg, m.pos, 2.2f, 26.0f, MK_BEAM_RGBA, 1.0f);
            if(!OccPoint({ m.pos.x, m.pos.y, m.pos.z + 3.5f })) continue;   // 3.21.4: behind a house: no text
            BcwLabel3D(bg, { m.pos.x, m.pos.y, m.pos.z + 4.2f }, m.label, MK_BEAM_RGBA, 34, 40.0f);
            BcwLabel3D(bg, { m.pos.x, m.pos.y, m.pos.z + 2.8f }, m.cmd, 0xFFFFFFFF, 24, 35.0f);
        }
        if(!dead && !s_RQ.on) MkProcessStarts(me, nowMs);
        if(s_Mk.inside >= 0 && !s_Mk.needExit)
        {
            char t[96]; snprintf(t, sizeof(t), "%s - starting...", s_Mk.starts[s_Mk.inside].label);
            BcwCenterText(fg, UI::S(30), ImGui::GetIO().DisplaySize.y * 0.22f, BcwCol(MK_BEAM_RGBA, 1.0f), t);
        }
        return;
    }
    MkUpdateBlips(me, nowMs, false);

    // in a race / job: the checkpoint to reach (+ a faint next one)
    if(s_Ev.kind == EV_RACE || s_Ev.kind == EV_ROUTE)
    {
        int n = EvN();
        if(s_Ev.cp < n)
        {
            Vec3 p = EvCp(s_Ev.cp);
            if(s_Ev.kind == EV_ROUTE) p.z = Game::GroundZ(p.x, p.y, p.z);
            float r = EvAir() ? 10.0f : 5.0f;
            MkBeam(bg, p, r, MK_BEAM_H, MK_BEAM_RGBA, 1.0f);
            if(s_Ev.cp + 1 < n) { Vec3 q = EvCp(s_Ev.cp + 1); if(s_Ev.kind == EV_ROUTE) q.z = Game::GroundZ(q.x, q.y, q.z); MkBeam(bg, q, r, MK_BEAM_H, MK_BEAM_RGBA, 0.35f); }
            char t[64];
            int shown = s_Ev.kind == EV_RACE ? s_Ev.cp : s_Ev.cp + 1, total = s_Ev.kind == EV_RACE ? n - 1 : n;
            snprintf(t, sizeof(t), "%s %d/%d - %d m", s_Ev.cp == n - 1 ? "FINISH" : "CHECKPOINT", shown, total, (int)GDist2D(me, p));
            MkLabel(bg, { p.x, p.y, p.z + 8.0f }, t, 0xFFFFFFFF, 34, 80.0f);
        }
    }
    // derby / DM: red ring at the edge + countdown when you are out
    float y = ImGui::GetIO().DisplaySize.y * 0.22f;
    if(s_Ev.kind == EV_DERBY)
    {
        const GDerby& d = kDerbies[s_Ev.idx];
        BcwDrawZone(bg, { d.x, d.y, d.z - 1.0f }, d.arena, MK_BEAM_RGBA, now, false, 8.0f);
        uint64_t since = s_Ev.outOfCarSince ? s_Ev.outOfCarSince : s_Ev.outOfZoneSince;
        if(since && nowMs - since < MP_DERBY_GRACE_MS)
        {
            char t[64]; snprintf(t, sizeof(t), "%s  %d", s_Ev.outOfCarSince ? "GET BACK IN YOUR CAR" : "BACK INTO THE ARENA", (int)((MP_DERBY_GRACE_MS - (nowMs - since) + 999) / 1000));
            BcwCenterText(fg, UI::S(34), y, BcwCol(MK_BEAM_RGBA, 1.0f), t);
        }
    }
    if(s_Ev.kind == EV_DM)
    {
        const GDm& d = kDms[s_Ev.idx];
        BcwDrawZone(bg, { d.x, d.y, d.z - 1.0f }, d.r, MK_BEAM_RGBA, now, false, 8.0f);
        if(s_Ev.outOfZoneSince && nowMs - s_Ev.outOfZoneSince < 10000)
        {
            char t[48]; snprintf(t, sizeof(t), "BACK INTO THE DM ZONE  %d", (int)((10000 - (nowMs - s_Ev.outOfZoneSince) + 999) / 1000));
            BcwCenterText(fg, UI::S(34), y, BcwCol(MK_BEAM_RGBA, 1.0f), t);
        }
    }
    if((s_Ev.kind == EV_RACE || s_Ev.kind == EV_ROUTE) && s_Ev.outOfCarSince && nowMs - s_Ev.outOfCarSince < 20000)
    {
        char t[48]; snprintf(t, sizeof(t), "GET BACK IN YOUR VEHICLE  %d", (int)((20000 - (nowMs - s_Ev.outOfCarSince) + 999) / 1000));
        BcwCenterText(fg, UI::S(34), y, BcwCol(MK_BEAM_RGBA, 1.0f), t);
    }
}
