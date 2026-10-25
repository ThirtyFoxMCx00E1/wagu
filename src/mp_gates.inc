// =====================================================================================================
// 3.04.2: gate teleports. Some gates in the map never open in single player (the Area 69 fence gates).
// Walk or drive up to one: the screen fades to black, you (and your car if you drive) are moved to the
// other side, the screen fades back in. Works on every server (and on Brief Case War, where the
// Schelapz base sits inside the Area 69 fence).
//
// Extra / corrected gates: files/MP/gates.txt, one gate per line:
//     x y z heading width name...
//   heading = the direction you walk THROUGH the gate (the game's heading, like /gatepos prints)
//   width   = how wide the gate opening is in metres
// Stand in the middle of a gate facing through it and type /gatepos: it prints a ready line.
// If gates.txt exists, its gates are used INSTEAD of the built-in ones (write "default" on a line to
// keep the built-in gates too).
// =====================================================================================================
struct MpGate { float x, y, z, nx, ny, half; char name[40]; };

static std::vector<MpGate> s_Gates;
static bool s_bGatesLoaded = false;

static void GateAdd(float x, float y, float z, float heading, float width, const char* name)
{
    MpGate g;
    g.x = x; g.y = y; g.z = z;
    float r = heading * 0.0174533f;
    g.nx = -sinf(r); g.ny = cosf(r);              // the game's forward vector for this heading
    g.half = (width < 2 ? 2 : width) * 0.5f;
    MP_CopyStr(g.name, name && name[0] ? name : "Gate", sizeof(g.name));
    s_Gates.push_back(g);
}
static void GatesDefaults()
{
    // Area 69 inner fence: the two gates SA-MP's a51_base filterscript turns into opening gates
    GateAdd(134.55f, 1941.53f, 17.70f,  0.0f, 11.0f, "Area 69 North Gate");   // gate along X: walk N/S
    GateAdd(286.01f, 1822.74f, 16.00f, 90.0f, 11.0f, "Area 69 East Gate");    // gate along Y: walk E/W
}
static void GatesLoad()
{
    s_bGatesLoaded = true;
    s_Gates.clear();
    char path[512]; snprintf(path, sizeof(path), "%s/gates.txt", MP::DataDir());
    FILE* f = fopen(path, "r");
    if(!f) { GatesDefaults(); return; }
    char line[256]; bool any = false, def = false;
    while(fgets(line, sizeof(line), f))
    {
        char* p = line; while(*p == ' ' || *p == '\t') ++p;
        if(!*p || *p == '#' || *p == '/' || *p == '\n' || *p == '\r') continue;
        if(!strncasecmp(p, "default", 7)) { def = true; continue; }
        float x, y, z, h, w; int used = 0;
        if(sscanf(p, "%f %f %f %f %f %n", &x, &y, &z, &h, &w, &used) < 5) continue;
        char name[40] = ""; MP_CopyStr(name, p + used, sizeof(name));
        for(char* e = name; *e; ++e) if(*e == '\n' || *e == '\r') { *e = 0; break; }
        GateAdd(x, y, z, h, w, name);
        any = true;
    }
    fclose(f);
    if(!any || def) GatesDefaults();
}

static struct
{
    int      phase = 0;        // 0 idle, 1 fading out, 2 black (teleported), 3 fading in
    uint64_t t = 0;            // phase start
    uint64_t coolUntil = 0;    // no new gate right after one
    Vec3     to = { 0, 0, 0 };
    float    heading = 0;
    float    alpha = 0;        // black overlay 0..1 (drawn in RenderGates)
} s_Gate;

static const uint64_t GATE_FADE_OUT = 350, GATE_HOLD = 400, GATE_FADE_IN = 450;

static bool GatesAllowed()
{
    if(JoinFlowActive()) return false;                         // connection screen / spawn picker
    if(MP::IsOnBcw() && s_Bcw.view < 2) return false;
    if(MP::IsOnMw3() && s_Mw3.view < 2) return false;   // 3.23          // BCW lobby / respawn wait
    return true;
}

static void ProcessGates(int ped, uint64_t now)
{
    if(!s_bGatesLoaded) GatesLoad();
    if(ped <= 0 || !Game::IsReady())   { s_Gate.phase = 0; s_Gate.alpha = 0; return; }
    if(s_Gate.phase == 1)
    {
        s_Gate.alpha = (float)(now - s_Gate.t) / GATE_FADE_OUT;
        if(now - s_Gate.t >= GATE_FADE_OUT)
        {
            s_Gate.alpha = 1;
            Game::PreloadArea(s_Gate.to, true);
            Game::TeleportGround(ped, s_Gate.to, s_Gate.heading);
            Game::RestoreCamera();
            s_Gate.phase = 2; s_Gate.t = now;
        }
        return;
    }
    if(s_Gate.phase == 2)
    {
        s_Gate.alpha = 1;
        if(now - s_Gate.t >= GATE_HOLD) { s_Gate.phase = 3; s_Gate.t = now; }
        return;
    }
    if(s_Gate.phase == 3)
    {
        s_Gate.alpha = 1.0f - (float)(now - s_Gate.t) / GATE_FADE_IN;
        if(now - s_Gate.t >= GATE_FADE_IN) { s_Gate.alpha = 0; s_Gate.phase = 0; s_Gate.coolUntil = now + 2500; }
        return;
    }
    if(now < s_Gate.coolUntil || Game::IsPaused() || !GatesAllowed()) return;
    if(Game::IsCharInAnyCar(ped))
    {
        int car = Game::GetCarCharIsIn(ped);
        if(car <= 0 || Game::GetDriverOfCar(car) != ped) return;   // passengers ride along with the driver
    }
    Vec3 p; if(!Game::GetCharPos(ped, p)) return;
    for(const MpGate& g : s_Gates)
    {
        float dx = p.x - g.x, dy = p.y - g.y;
        if(fabsf(p.z - g.z) > 8.0f) continue;
        float a = dx * g.nx + dy * g.ny;              // distance through the gate (- one side, + other)
        float l = -dx * g.ny + dy * g.nx;             // distance along the gate
        if(fabsf(l) > g.half || fabsf(a) > 3.2f) continue;
        float side = a < 0 ? 1.0f : -1.0f;            // go to the side you are NOT on
        bool inCar = Game::IsCharInAnyCar(ped);
        float off = inCar ? 9.0f : 6.0f;
        s_Gate.to = { g.x + g.nx * off * side, g.y + g.ny * off * side, g.z };
        float fx = g.nx * side, fy = g.ny * side;     // face away from the gate on the new side
        s_Gate.heading = atan2f(-fx, fy) * 57.29578f;
        s_Gate.phase = 1; s_Gate.t = now; s_Gate.alpha = 0;
        return;
    }
}

static void RenderGates()
{
    if(s_Gate.alpha > 0.001f)
    {
        float a = s_Gate.alpha > 1 ? 1 : s_Gate.alpha;
        ImVec2 ds = ImGui::GetIO().DisplaySize;
        ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0, 0), ds, IM_COL32(0, 0, 0, (int)(a * 255)));
    }
    if(!s_bGatesLoaded || !Game::IsReady() || Game::IsPaused() || !GatesAllowed()) return;
    // small floating name over each gate when close, so you know it lets you through
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    for(const MpGate& g : s_Gates)
    {
        Vec3 c = { g.x, g.y, g.z + 3.2f };
        if(BcwCamDist(c) > 45.0f) continue;
        BcwLabel3D(dl, c, g.name, 0xFFD25AFF, 26.0f, 12.0f);
    }
}

static void CmdGatePos()
{
    int ped = Game::LocalPed(); Vec3 p;
    if(ped <= 0 || !Game::GetCharPos(ped, p)) return;
    float h = Game::GetCharHeading(ped);
    SysMsg(COL_EVT, "gates.txt line:  %.2f %.2f %.2f %.0f 11 My Gate", p.x, p.y, p.z - 1.0f, h);
    SysMsg(COL_SYSTEM, "Stand in the middle of the gate, facing through it. Put the line in files/MP/gates.txt, then /gates reload.");
}
static bool GatesCommand(const char* cmd, const char* arg)
{
    if(!strcmp(cmd, "gatepos")) { CmdGatePos(); return true; }
    if(!strcmp(cmd, "gates"))
    {
        if(arg && !strncasecmp(arg, "reload", 6)) GatesLoad();
        if(!s_bGatesLoaded) GatesLoad();
        SysMsg(COL_SYSTEM, "%d gate teleport(s):", (int)s_Gates.size());
        for(const MpGate& g : s_Gates) SysMsg(COL_SYSTEM, "  %s  (%.1f, %.1f, %.1f)", g.name, g.x, g.y, g.z);
        return true;
    }
    return false;
}
