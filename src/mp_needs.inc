// 3.39: hunger + energy HUD (bottom-right corner, like the screenshot): two dark see-through squares,
// a fork + knife and a kettlebell icon (white outline, black fill) and the % in pricedown with a black
// outline: green when high, yellow in the middle, red when low.
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
static ImU32 NeedsCol(float v) { return v > 50.0f ? IM_COL32(60, 200, 70, 255) : (v > 25.0f ? IM_COL32(230, 200, 40, 255) : IM_COL32(210, 40, 40, 255)); }
static void NeedsIconFood(ImDrawList* dl, ImVec2 c, float r)
{
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.5f, r * 0.12f);
    // fork (left): 3 prongs + handle
    const float fx = c.x - r * 0.35f;
    ImVec2 fa(fx - r * 0.18f, c.y - r * 0.85f), fb(fx + r * 0.18f, c.y - r * 0.15f);
    dl->AddRectFilled(fa, fb, k, r * 0.08f); dl->AddRect(fa, fb, w, r * 0.08f, 0, t);
    for(int i = -1; i <= 1; ++i) dl->AddLine(ImVec2(fx + i * r * 0.09f, c.y - r * 0.85f), ImVec2(fx + i * r * 0.09f, c.y - r * 0.45f), w, t * 0.6f);
    ImVec2 ha(fx - r * 0.07f, c.y - r * 0.15f), hb(fx + r * 0.07f, c.y + r * 0.85f);
    dl->AddRectFilled(ha, hb, k, r * 0.05f); dl->AddRect(ha, hb, w, r * 0.05f, 0, t);
    // knife (right): blade + handle
    const float kx = c.x + r * 0.35f;
    dl->PathLineTo(ImVec2(kx - r * 0.12f, c.y - r * 0.85f)); dl->PathLineTo(ImVec2(kx + r * 0.16f, c.y - r * 0.65f));
    dl->PathLineTo(ImVec2(kx + r * 0.16f, c.y + r * 0.05f)); dl->PathLineTo(ImVec2(kx - r * 0.12f, c.y + r * 0.05f));
    dl->PathFillConvex(k);
    dl->PathLineTo(ImVec2(kx - r * 0.12f, c.y - r * 0.85f)); dl->PathLineTo(ImVec2(kx + r * 0.16f, c.y - r * 0.65f));
    dl->PathLineTo(ImVec2(kx + r * 0.16f, c.y + r * 0.05f)); dl->PathLineTo(ImVec2(kx - r * 0.12f, c.y + r * 0.05f));
    dl->PathStroke(w, ImDrawFlags_Closed, t);
    ImVec2 ka(kx - r * 0.09f, c.y + r * 0.05f), kb(kx + r * 0.09f, c.y + r * 0.85f);
    dl->AddRectFilled(ka, kb, k, r * 0.05f); dl->AddRect(ka, kb, w, r * 0.05f, 0, t);
}
static void NeedsIconEnergy(ImDrawList* dl, ImVec2 c, float r)
{   // a kettlebell: round body, handle on top
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.5f, r * 0.12f);
    ImVec2 body(c.x, c.y + r * 0.22f);
    dl->PathArcTo(ImVec2(c.x, c.y - r * 0.42f), r * 0.42f, IM_PI, IM_PI * 2.0f, 16); dl->PathStroke(w, 0, t * 2.2f);
    dl->PathArcTo(ImVec2(c.x, c.y - r * 0.42f), r * 0.42f, IM_PI, IM_PI * 2.0f, 16); dl->PathStroke(k, 0, t * 0.8f);
    dl->AddCircleFilled(body, r * 0.62f, k, 28); dl->AddCircle(body, r * 0.62f, w, 28, t);
    dl->AddRectFilled(ImVec2(c.x - r * 0.45f, c.y + r * 0.78f), ImVec2(c.x + r * 0.45f, c.y + r * 0.9f), w, r * 0.04f);
}
static void NeedsRender()
{
    if(!NeedsOn() || Game::IsPaused() || !myDrawValid) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sz = UI::S(92.0f), gap = UI::S(10.0f), m = UI::S(18.0f);
    ImFont* pf = UI::PricedownFont() ? UI::PricedownFont() : UI::Font();
    const float fs = UI::S(pf == UI::Font() ? 26.0f : 32.0f);
    const float vals[2] = { s_Need.hunger, s_Need.energy };
    for(int i = 0; i < 2; ++i)
    {
        ImVec2 a(ds.x - m - (2 - i) * sz - (1 - i) * gap, ds.y - m - sz), b(a.x + sz, a.y + sz);
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120), UI::S(4));
        ImVec2 c(a.x + sz * 0.5f, a.y + sz * 0.38f);
        if(i == 0) NeedsIconFood(dl, c, sz * 0.28f); else NeedsIconEnergy(dl, c, sz * 0.28f);
        char t[8]; snprintf(t, sizeof(t), "%d%%", (int)(vals[i] + 0.5f));
        ImVec2 ts = pf->CalcTextSizeA(fs, FLT_MAX, 0, t);
        ImVec2 p(a.x + (sz - ts.x) * 0.5f, b.y - ts.y - UI::S(4));
        const float o = fmaxf(1.0f, fs * 0.07f);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) dl->AddText(pf, fs, ImVec2(p.x + ox * o, p.y + oy * o), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(pf, fs, p, NeedsCol(vals[i]), t);
    }
}
