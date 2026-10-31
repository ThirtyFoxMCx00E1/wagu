// 3.39: hunger + energy HUD (right edge, like the screenshot; 3.40: one box, two rows - see NeedsRender).
// Green when high, yellow in the middle, red when low.
// On the default server always, on custom servers when the host allows it (permission "Hunger + energy
// HUD" = PERM_NEEDS), never on Mega Vil Futuristic (it has its own) / Brief Case War / MW3.
//   hunger: -1 % every 40 s (sprinting / swimming: faster), 0 % = slowly losing health. /eat (+35 %, $25)
//   energy: -1 % every 25 s moving, +1 % every 3 s standing still, +3 % a second in a bed (/sleep in a house)

static struct { float hunger = 100.0f, energy = 100.0f; uint64_t lastAt = 0, hurtAt = 0, warnAt = 0; bool sleeping = false; } s_Need;

static bool NeedsOn()
{
    if(client.GetState() != NET_CONNECTED || !Game::IsReady() || RpOnServer() || BcwOnServer() || Mw3OnServer()) return false;
    if(MP::IsOnDefault()) return true;
    return (s_nServerPerms & PERM_NEEDS) != 0;
}
static void NeedsProcess(int ped, uint64_t now)
{
    if(!NeedsOn() || ped <= 0) { s_Need.lastAt = 0; s_Need.sleeping = false; return; }
    if(!s_Need.lastAt) { s_Need.lastAt = now; return; }
    float dt = (now - s_Need.lastAt) / 1000.0f; s_Need.lastAt = now;
    if(dt <= 0 || dt > 5) return;
    const float spd = Game::GetCharSpeed(ped);
    const bool car = Game::IsCharInAnyCar(ped);
    float hRate = 1.0f / 40.0f;              // % per second
    if(!car && spd > 5.0f) hRate *= 1.8f;    // sprinting
    s_Need.hunger -= hRate * dt;
    if(s_Need.sleeping)
    {
        s_Need.energy += 3.0f * dt;
        if(spd > 0.5f || s_Need.energy >= 100.0f) { s_Need.sleeping = false; if(s_Need.energy >= 100.0f) SysMsg(COL_GREEN, "You slept well: energy 100%%."); }
    }
    else if(!car && spd < 0.3f) s_Need.energy += dt / 3.0f;
    else if(!car) s_Need.energy -= (spd > 5.0f ? 2.2f : 1.0f) * dt / 25.0f;
    else s_Need.energy -= dt / 60.0f;
    s_Need.hunger = fmaxf(0, fminf(100, s_Need.hunger)); s_Need.energy = fmaxf(0, fminf(100, s_Need.energy));
    if(s_Need.hunger <= 0.0f && now - s_Need.hurtAt > 8000)
    {
        s_Need.hurtAt = now;
        int hp = Game::GetCharHealth(ped); if(hp > 10) Game::SetCharHealth(ped, hp - 3);
    }
    if(now - s_Need.warnAt > 120000 && (s_Need.hunger < 15.0f || s_Need.energy < 10.0f))
    {
        s_Need.warnAt = now;
        if(s_Need.hunger < 15.0f) SysMsg(COL_YELLOW, "You are hungry: /eat ($25 a meal).");
        if(s_Need.energy < 10.0f) SysMsg(COL_YELLOW, "You are tired: stand still to rest, or /sleep in your house.");
    }
}
static bool NeedsCommand(const char* cmd, const char*)
{
    if(!NeedsOn()) return false;
    if(!strcmp(cmd, "eat"))
    {
        if(s_Need.hunger >= 99.0f) { SysMsg(COL_SYSTEM, "You are not hungry."); return true; }
        if(Game::GetMoney() < 25) { SysMsg(COL_RED, "A meal is $25."); return true; }
        Game::AddMoney(-25); s_Need.hunger = fminf(100.0f, s_Need.hunger + 35.0f);
        SysMsg(COL_GREEN, "You ate a meal ($25). Hunger %d%%.", (int)s_Need.hunger);
        return true;
    }
    if(!strcmp(cmd, "sleep") || !strcmp(cmd, "rest"))
    {
        if(!s_nHkInside && strcmp(cmd, "rest")) { SysMsg(COL_RED, "Sleep in a house (/enterhouse first) - or /rest: stand still."); return true; }
        s_Need.sleeping = true; SysMsg(COL_SYSTEM, "Resting... (move to stop)");
        return true;
    }
    if(!strcmp(cmd, "needs") || !strcmp(cmd, "hunger") || !strcmp(cmd, "energy"))
    { SysMsg(COL_SYSTEM, "Hunger %d%%  Energy %d%%  (/eat, /sleep, /rest)", (int)s_Need.hunger, (int)s_Need.energy); return true; }
    return false;
}
// 3.39.2: like the screenshot - ONE dark see-through box on the right edge (above the bottom corner),
// two rows: white-outlined icon on the left (fork + spoon = hunger, a bowl/pot = energy) and the value on
// the right: green blocky digits + a thin pale "%". Sized from the screen height so it looks the same
// on every phone.
static ImU32 NeedsCol(float v) { return v > 50.0f ? IM_COL32(72, 178, 84, 255) : (v > 25.0f ? IM_COL32(222, 190, 50, 255) : IM_COL32(210, 52, 46, 255)); }
static void NeedsIconFood(ImDrawList* dl, ImVec2 c, float r)
{   // fork + spoon crossed (white outline, black inside)
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.2f, r * 0.16f);
    auto bar = [&](float ang, float off, bool spoon)
    {
        const float ca = cosf(ang), sa = sinf(ang);
        auto P = [&](float u, float v) { return ImVec2(c.x + (u * ca - v * sa) * r + off * sa * r, c.y + (u * sa + v * ca) * r - off * ca * r); };
        // handle
        ImVec2 q[4] = { P(-0.85f, -0.14f), P(0.20f, -0.14f), P(0.20f, 0.14f), P(-0.85f, 0.14f) };
        dl->AddConvexPolyFilled(q, 4, k); dl->AddPolyline(q, 4, w, ImDrawFlags_Closed, t);
        // head
        if(spoon)
        {
            ImVec2 hc = P(0.48f, 0.0f);
            dl->AddCircleFilled(hc, r * 0.30f, k, 18); dl->AddCircle(hc, r * 0.30f, w, 18, t);
        }
        else
        {
            ImVec2 h4[4] = { P(0.20f, -0.24f), P(0.90f, -0.24f), P(0.90f, 0.24f), P(0.20f, 0.24f) };
            dl->AddConvexPolyFilled(h4, 4, k); dl->AddPolyline(h4, 4, w, ImDrawFlags_Closed, t);
            dl->AddLine(P(0.50f, -0.08f), P(0.92f, -0.08f), w, t * 0.8f);
            dl->AddLine(P(0.50f, 0.08f), P(0.92f, 0.08f), w, t * 0.8f);
        }
    };
    bar(-2.356f, 0.30f, false);   // fork: handle bottom-right -> prongs top-left
    bar(-2.356f, -0.30f, true);   // spoon next to it
}
static void NeedsIconEnergy(ImDrawList* dl, ImVec2 c, float r)
{   // a pot / bowl: flat rim with a small knob on top, sides going in, round bottom
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.2f, r * 0.16f);
    dl->PathClear();
    dl->PathLineTo(ImVec2(c.x - r * 0.95f, c.y - r * 0.35f));
    dl->PathLineTo(ImVec2(c.x - r * 0.28f, c.y - r * 0.35f));
    dl->PathLineTo(ImVec2(c.x - r * 0.18f, c.y - r * 0.62f));
    dl->PathLineTo(ImVec2(c.x + r * 0.18f, c.y - r * 0.62f));
    dl->PathLineTo(ImVec2(c.x + r * 0.28f, c.y - r * 0.35f));
    dl->PathLineTo(ImVec2(c.x + r * 0.95f, c.y - r * 0.35f));
    dl->PathLineTo(ImVec2(c.x + r * 0.62f, c.y + r * 0.40f));
    dl->PathArcTo(ImVec2(c.x, c.y + r * 0.30f), r * 0.62f, 0.15f, IM_PI - 0.15f, 12);
    dl->PathLineTo(ImVec2(c.x - r * 0.62f, c.y + r * 0.40f));
    ImVec2 pts[64]; int n = std::min(64, dl->_Path.Size); for(int j = 0; j < n; ++j) pts[j] = dl->_Path[j];
    dl->PathClear();
    {   // fill: the body without the knob (convex) + the knob
        ImVec2 body[64]; int m = 0; body[m++] = pts[0];
        for(int j = 5; j < n; ++j) body[m++] = pts[j];
        dl->AddConvexPolyFilled(body, m, k);
        ImVec2 knob[4] = { pts[1], pts[2], pts[3], pts[4] };
        dl->AddConvexPolyFilled(knob, 4, k);
    }
    dl->AddPolyline(pts, n, w, ImDrawFlags_Closed, t);
}
static void NeedsRender()
{
    if(!NeedsOn() || Game::IsPaused() || !myDrawValid) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float H = ds.y;
    const float bw = H * 0.205f, bh = H * 0.128f;     // the box (screenshot: 72 x 46 on a 360 high screen)
    const float bottom = H * 0.083f;                    // above the bottom edge
    ImVec2 a(ds.x - bw, ds.y - bottom - bh), b(ds.x, ds.y - bottom);
    dl->AddRectFilled(a, b, IM_COL32(20, 22, 26, 150));
    ImFont* f = UI::Font();
    const float fs = H * 0.046f, fsp = H * 0.040f;
    const float vals[2] = { s_Need.hunger, s_Need.energy };
    for(int i = 0; i < 2; ++i)
    {
        const float cy = a.y + bh * (i == 0 ? 0.30f : 0.72f);
        ImVec2 ic(a.x + bw * 0.22f, cy);
        if(i == 0) NeedsIconFood(dl, ic, H * 0.024f); else NeedsIconEnergy(dl, ic, H * 0.024f);
        char t[8]; snprintf(t, sizeof(t), "%d", (int)(vals[i] + 0.5f));
        const ImVec2 pss = f->CalcTextSizeA(fsp, FLT_MAX, 0, "%");
        const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, t);
        const float right = b.x - bw * 0.08f;
        ImVec2 pp(right - pss.x, cy - pss.y * 0.5f);
        ImVec2 tp(pp.x - ts.x - H * 0.003f, cy - ts.y * 0.5f);
        const ImU32 col = NeedsCol(vals[i]);
        const ImU32 dark = IM_COL32(18, 60, 24, 255);
        const float o = fmaxf(1.0f, fs * 0.05f);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) dl->AddText(f, fs, ImVec2(tp.x + ox * o, tp.y + oy * o), vals[i] > 50.0f ? dark : IM_COL32(0, 0, 0, 255), t);
        dl->AddText(f, fs, tp, col, t);
        dl->AddText(f, fs, ImVec2(tp.x + o * 0.6f, tp.y), col, t);   // a bit bolder, blocky like the screenshot
        dl->AddText(f, fsp, pp, IM_COL32(200, 225, 205, 235), "%");
    }
}
