// 4.11: the phone side of SA-MP gamemodes running on an MP server (net/server_samp.inc, net/samp.h).
// The server runs the PC gamemode (.amx) and sends PKT_SAMP for the things MP had no packet for yet:
// checkpoints, map icons, camera, freeze, time / weather, interior, attached objects, 3D text labels
// and clickable textdraws (SelectTextDraw). Every /command goes to the gamemode (Pawn.CMD / OnPlayerCommandText),
// and the Y / N / H buttons send SA-MP keys (OnPlayerKeyStateChange).

struct SsLabel { Vec3 p; float dist; uint32_t color; std::string text; };
struct SsAttach { int obj = 0; int model = -1; Vec3 off{ 0, 0, 0 }, rot{ 0, 0, 0 }; uint64_t tryAt = 0; };
static struct SampSrvState
{
    bool on = false, pawnCmd = false;
    std::string mode;
    bool select = false; uint32_t hover = 0xFFFF00FF;
    bool cp[2] = { false, false }, inCp[2] = { false, false }; Vec3 cpPos[2]; float cpSize[2] = { 0, 0 };
    int icon[100] = { 0 };
    std::unordered_map<int, SsLabel> labels;
    SsAttach att[10];
    bool cam = false; Vec3 camPos{ 0, 0, 0 }, camLook{ 0, 0, 0 }; bool haveLook = false;
    bool frozen = false;
    int keys = 0;
} s_SS;

static bool SampSrvOn() { return s_SS.on && client.GetState() == NET_CONNECTED; }
static void SampSrvSend(uint8_t op, uint8_t a, uint8_t b, int32_t val, const char* text = nullptr)
{
    if(client.GetState() != NET_CONNECTED) return;
    PktSamp p; memset(&p, 0, sizeof(p)); p.type = PKT_SAMP; p.op = op; p.a = a; p.b = b; p.val = val;
    if(text) MP_CopyStr(p.text, text, sizeof(p.text));
    client.Send(&p, sizeof(p));
}

static void SampSrvReset()
{
    for(auto& b : s_SS.icon) if(b) { Game::RemoveBlip(b); b = 0; }
    for(auto& a : s_SS.att) { if(a.obj > 0) Game::DeleteWorldObject(a.obj); a = SsAttach(); }
    if(s_SS.cam) Game::RestoreCamera();
    if(s_SS.frozen) Game::SetPlayerControl(true);
    s_SS = SampSrvState();
}

static void SampSrvOnPacket(const uint8_t* d, int len)
{
    if(len < (int)offsetof(PktSamp, text)) return;
    PktSamp p; memset(&p, 0, sizeof(p)); memcpy(&p, d, std::min<int>(len, (int)sizeof(p))); p.text[sizeof(p.text) - 1] = 0;
    const int ped = Game::LocalPed();
    const Vec3 f = { p.f[0], p.f[1], p.f[2] };
    switch(p.op)
    {
        case SPO_HELLO:
            s_SS.on = true; s_SS.pawnCmd = p.a != 0; s_SS.mode = p.text;
            SysMsg(COL_SYSTEM, "This server runs the SA-MP gamemode \"%s\" - every /command goes to it.", p.text);
            break;
        case SPO_TDSELECT: s_SS.select = p.a != 0; if(p.val) s_SS.hover = (uint32_t)p.val; break;
        case SPO_CHECKPOINT:
        {
            int k = p.b ? 1 : 0;
            s_SS.cp[k] = p.a != 0; s_SS.inCp[k] = false; s_SS.cpPos[k] = f; s_SS.cpSize[k] = p.f[3] > 0.5f ? p.f[3] : 3.0f;
            break;
        }
        case SPO_MAPICON:
        {
            if(p.a >= 100) break;
            if(s_SS.icon[p.a]) { Game::RemoveBlip(s_SS.icon[p.a]); s_SS.icon[p.a] = 0; }
            if(p.b) s_SS.icon[p.a] = Game::AddSpriteBlip(f, p.b);
            break;
        }
        case SPO_CAMERA:
            if(p.a == 0) { if(s_SS.cam) Game::RestoreCamera(); s_SS.cam = false; s_SS.haveLook = false; }
            else if(p.a == 1) { s_SS.camPos = f; s_SS.cam = true; if(!s_SS.haveLook) s_SS.camLook = { f.x, f.y + 1.0f, f.z }; }
            else { s_SS.camLook = f; s_SS.haveLook = true; s_SS.cam = true; }
            if(s_SS.cam) Game::SetFixedCamera(s_SS.camPos, s_SS.camLook);
            break;
        case SPO_CONTROL: s_SS.frozen = p.a == 0; Game::SetPlayerControl(p.a != 0); if(ped) Game::FreezeChar(ped, p.a == 0); break;
        case SPO_WORLD: if(ped) Game::SetInterior(ped, p.a); break;
        case SPO_TIME: Game::SetTimeOfDay(p.a, p.b); if(p.val >= 0) Game::ForceWeatherNow(p.val); break;
        case SPO_ATTACH:
        {
            if(p.a >= 10) break;
            SsAttach& a = s_SS.att[p.a];
            if(a.obj > 0) Game::DeleteWorldObject(a.obj);
            a = SsAttach();
            if(p.val >= 0)
            {
                a.model = p.val; a.off = f;
                float r[6] = { 0, 0, 0, 1, 1, 1 }; sscanf(p.text, "%f %f %f %f %f %f", &r[0], &r[1], &r[2], &r[3], &r[4], &r[5]);
                a.rot = { r[0], r[1], r[2] };
            }
            break;
        }
        case SPO_LABEL:
        {
            if(!p.a) { s_SS.labels.erase(p.val); break; }
            SsLabel l; l.p = f; l.dist = p.f[3] > 1.0f ? p.f[3] : 20.0f;
            // the colour is in the last 4 bytes of text (after the 0)
            size_t n = strnlen(p.text, sizeof(p.text) - 5);
            uint32_t c = 0xFFFFFFFF; memcpy(&c, p.text + sizeof(p.text) - 5, 4); if(!c) c = 0xFFFFFFFF;
            l.color = c; l.text.assign(p.text, n);
            s_SS.labels[p.val] = l;
            break;
        }
        default: break;   // SPO_SOUND / SPO_REMOVEBLD: not on MP yet
    }
}

// every frame: checkpoints, attached objects, the dynamic map objects, a fixed camera
static void SampSrvProcess(int ped, uint64_t now)
{
    if(!SampSrvOn()) { if(s_SS.on && client.GetState() != NET_CONNECTED) SampSrvReset(); return; }
    if(!ped) return;
    Vec3 me{ 0, 0, 0 }; Game::GetCharPos(ped, me);
    for(int k = 0; k < 2; ++k)
    {
        if(!s_SS.cp[k]) continue;
        const float dx = me.x - s_SS.cpPos[k].x, dy = me.y - s_SS.cpPos[k].y, dz = me.z - s_SS.cpPos[k].z;
        const bool in = dx * dx + dy * dy <= s_SS.cpSize[k] * s_SS.cpSize[k] && fabsf(dz) < 4.0f;
        if(in != s_SS.inCp[k]) { s_SS.inCp[k] = in; SampSrvSend(SPO_CPENTER, in ? 1 : 0, (uint8_t)k, 0); }
    }
    for(auto& a : s_SS.att)
    {
        if(a.model < 0 || a.obj > 0 || now < a.tryAt) continue;
        a.tryAt = now + 500;
        if(!Game::IsKnownModel(a.model)) { a.model = -1; continue; }   // SA-MP-only models (the model pack)
        int o = Game::CreateWorldObject(a.model, me);
        if(o > 0) { a.obj = o; Game::SetObjectCollision(o, false); Game::AttachObjectToCharRot(o, ped, a.off, a.rot); }
    }
    if(!DzOnServer()) DzMapTick(me, now);   // CreateObject / CreateDynamicObject of the gamemode (PKT_DZ_MAP)
    if(s_SS.cam && now % 1000 < 40) Game::SetFixedCamera(s_SS.camPos, s_SS.camLook);
}

// /commands: the gamemode has them (a few phone-only ones stay here)
static bool SampSrvCommand(const char* text)
{
    if(!SampSrvOn() || !text || text[0] != '/') return false;
    static const char* const kLocal[] = { "q", "quit", "fps", "bubbles", "icons", "mapicons", "hudmenu", "launcher", "mpsettings" };
    char cmd[32] = ""; int n = 0; const char* a = text + 1;
    while(*a && *a != ' ' && n < 30) cmd[n++] = (char)tolower((unsigned char)*a++);
    cmd[n] = 0;
    for(const char* l : kLocal) if(!strcmp(cmd, l)) return false;
    SampSrvSend(SPO_CMD, 0, 0, 0, text);
    return true;
}

static void SampSrvRender(ImDrawList* dl)
{
    if(!SampSrvOn()) return;
    const int ped = Game::LocalPed();
    Vec3 me{ 0, 0, 0 }; if(ped) Game::GetCharPos(ped, me);
    // checkpoints: the SA-MP red beam
    for(int k = 0; k < 2; ++k) if(s_SS.cp[k]) MkBeam(dl, s_SS.cpPos[k], s_SS.cpSize[k], 6.0f, MK_BEAM_RGBA, s_SS.inCp[k] ? 0.4f : 0.8f);
    // 3D text labels
    ImFont* font = ImGui::GetFont();
    for(auto& kv : s_SS.labels)
    {
        const SsLabel& l = kv.second;
        const float dx = l.p.x - me.x, dy = l.p.y - me.y, dz = l.p.z - me.z, d = sqrtf(dx * dx + dy * dy + dz * dz);
        if(d > l.dist) continue;
        float sx, sy, depth; if(!Game::WorldToScreen(l.p, sx, sy, depth)) continue;
        std::vector<SampSeg> segs; SampSplitColors(l.text.c_str(), TdCol(l.color), segs);
        const float fs = ImGui::GetFontSize() * 0.9f;
        SampDrawSegs(dl, font, fs, ImVec2(sx - SampSegsWidth(font, fs, segs) * 0.5f, sy), segs, std::max(0.35f, 1.0f - d / l.dist));
    }
    // SelectTextDraw: tap a selectable textdraw (Close = Esc)
    if(s_SS.select)
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0)); ImGui::SetNextWindowSize(ds);
        ImGui::Begin("##SampTdSelect", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
        const ImVec2 m = ImGui::GetIO().MousePos;
        ImDrawList* wl = ImGui::GetWindowDrawList();
        for(int i = 0; i < SAMP_TD_LOCAL; ++i)
        {
            const SampTd& t = s_Td[i];
            if(!t.used || !t.d.pad[0]) continue;
            const ImVec4 r = t.rect;
            if(m.x < r.x || m.y < r.y || m.x > r.z || m.y > r.w) continue;
            wl->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.z, r.w), (TdCol(s_SS.hover) & 0x00FFFFFF) | 0x50000000);
            if(ImGui::IsMouseClicked(0)) SampSrvSend(SPO_TDCLICK, 0, 0, i);
        }
        ImGui::SetCursorPos(ImVec2(ds.x - 130.0f, 10.0f));
        if(ImGui::Button("Close", ImVec2(120.0f, 44.0f))) { s_SS.select = false; SampSrvSend(SPO_TDCLICK, 0, 0, -1); }
        ImGui::End();
    }
    // Y / N / H keys (SA-MP KEY_YES / KEY_NO / KEY_CTRL_BACK)
    if(!s_Dlg.open)
    {
        const ImVec2 ds = ImGui::GetIO().DisplaySize;
        const float bw = 56.0f;
        ImGui::SetNextWindowPos(ImVec2(ds.x - bw - 12.0f, ds.y * 0.42f)); ImGui::SetNextWindowSize(ImVec2(bw + 8.0f, bw * 3 + 32.0f));
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin("##SampKeys", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
        static const struct { const char* l; int bit; } kKeys[] = { { "Y", 65536 }, { "N", 131072 }, { "H", 262144 } };
        int keys = 0;
        for(auto& k : kKeys) { ImGui::Button(k.l, ImVec2(bw, bw)); if(ImGui::IsItemActive()) keys |= k.bit; }
        ImGui::End();
        if(keys != s_SS.keys) { s_SS.keys = keys; SampSrvSend(SPO_KEYS, 0, 0, keys); }
    }
}
