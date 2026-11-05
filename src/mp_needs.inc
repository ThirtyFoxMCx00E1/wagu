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
    if(client.GetState() != NET_CONNECTED || !Game::IsReady() || RpOnServer() || BcwOnServer() || Mw3OnServer() || DzOnServer()) return false;
    if(MP::IsOnDefault()) return true;
    return (s_nServerPerms & PERM_NEEDS) != 0;
}

// 3.56: vehicle HUD (speed / engine / fuel) in the same hexagons, one horizontal row with hunger + energy.
//   fuel: per car, -1 % every 350 m driven (and a little while the engine idles); 0 % = the engine stops.
//   /engine turns it off / on, /refuel at a gas station ($1 a %).
struct FuelCar { float fuel = 100.0f; bool off = false; };
static struct { std::map<int, FuelCar> cars; int car = 0; Vec3 last = { 0, 0, 0 }; bool haveLast = false;
                uint64_t forceAt = 0, warnAt = 0, pruneAt = 0, hintAt = 0; float mph = 0; bool engine = false, driver = false; } s_Fuel;
static const float kGasStations[][3] = {   // pump islands (GTA SA map; the pumps of the SA-MP fuel scripts)
    { -91.2f, -1169.3f, 2.8f },  { 1941.7f, -1772.9f, 14.1f }, { 1004.0f, -939.3f, 42.2f },   { 655.7f, -565.1f, 15.4f },
    { 1382.0f, 459.5f, 20.3f },  { -1328.2f, 2677.4f, 49.5f }, { -1471.6f, 1864.0f, 31.8f },  { -1605.6f, -2714.4f, 47.9f },
    { -1675.6f, 413.2f, 6.4f },  { -2410.8f, 975.2f, 45.4f },  { -2246.7f, -2559.7f, 31.9f }, { 618.8f, 1684.9f, 6.2f },
    { 1596.1f, 2199.1f, 11.3f }, { 2114.9f, 920.1f, 11.3f },   { 2147.5f, 2747.9f, 11.3f },   { 2202.3f, 2475.3f, 11.3f },
    { 2639.9f, 1106.4f, 11.3f },
};
static bool FuelNoEngine(int model) { return model == 481 || model == 509 || model == 510; }   // BMX, bike, mountain bike
static bool FuelAtStation(const Vec3& p)
{
    if(CgGasNear(p)) return true;   // 4.09: CGRP gas stations
    for(const auto& g : kGasStations)
    {
        float dx = p.x - g[0], dy = p.y - g[1], dz = p.z - g[2];
        if(dx * dx + dy * dy < 16.0f * 16.0f && fabsf(dz) < 8.0f) return true;
    }
    return false;
}
static void FuelProcess(int ped, uint64_t now, float dt)
{
    s_Fuel.driver = false;
    const int car = ped > 0 && Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
    if(car <= 0 || !Game::CarExists(car)) { s_Fuel.car = 0; s_Fuel.haveLast = false; return; }
    if(car != s_Fuel.car) { s_Fuel.car = car; s_Fuel.haveLast = false; }
    Vec3 v; s_Fuel.mph = Game::GetMoveSpeed(car, true, v) ? sqrtf(v.x * v.x + v.y * v.y + v.z * v.z) * 2.23694f : 0.0f;
    const int model = Game::GetCarModel(car);
    s_Fuel.driver = Game::GetDriverOfCar(car) == ped;
    if(FuelNoEngine(model)) { s_Fuel.engine = false; return; }
    FuelCar& f = s_Fuel.cars[car];
    if(!s_Fuel.driver) { s_Fuel.engine = Game::IsCarEngineOn(car); return; }
    if((f.off || f.fuel <= 0.0f) && now >= s_Fuel.forceAt) { s_Fuel.forceAt = now + 250; if(Game::IsCarEngineOn(car)) Game::SetCarEngineOn(car, false); }
    s_Fuel.engine = Game::IsCarEngineOn(car);
    Vec3 p; if(!Game::GetCarPos(car, p)) return;
    if(s_Fuel.engine && f.fuel > 0.0f)
    {
        float d = 0;
        if(s_Fuel.haveLast) { float dx = p.x - s_Fuel.last.x, dy = p.y - s_Fuel.last.y, dz = p.z - s_Fuel.last.z; d = sqrtf(dx * dx + dy * dy + dz * dz); if(d > 60.0f) d = 0; }   // a teleport: no fuel
        f.fuel -= d / 350.0f + dt / 150.0f;
        if(f.fuel <= 0.0f) { f.fuel = 0.0f; Game::SetCarEngineOn(car, false); SysMsg(COL_RED, "{FFA500}FUEL: {FFFFFF}Out of fuel - the engine stopped. Push it to a gas station and /refuel."); }
        else if(f.fuel < 15.0f && now - s_Fuel.warnAt > 120000) { s_Fuel.warnAt = now; SysMsg(COL_YELLOW, "{FFA500}FUEL: {FFFFFF}Low fuel (%d%%) - /refuel at a gas station.", (int)f.fuel); }
    }
    s_Fuel.last = p; s_Fuel.haveLast = true;
    if(f.fuel < 90.0f && now - s_Fuel.hintAt > 60000 && FuelAtStation(p)) { s_Fuel.hintAt = now; SysMsg(COL_YELLOW, "{FFA500}GAS STATION: {FFFFFF}/refuel  ($1 a %%, fuel %d%%)", (int)f.fuel); }
    if(now > s_Fuel.pruneAt)
    {   // forget cars that are gone
        s_Fuel.pruneAt = now + 30000;
        for(auto it = s_Fuel.cars.begin(); it != s_Fuel.cars.end();) { if(it->first != car && !Game::CarExists(it->first)) it = s_Fuel.cars.erase(it); else ++it; }
    }
}
static void NeedsProcess(int ped, uint64_t now)
{
    if(!NeedsOn() || ped <= 0) { s_Need.lastAt = 0; s_Need.sleeping = false; s_Fuel.car = 0; s_Fuel.driver = false; return; }
    if(!s_Need.lastAt) { s_Need.lastAt = now; return; }
    float dt = (now - s_Need.lastAt) / 1000.0f; s_Need.lastAt = now;
    if(dt <= 0 || dt > 5) return;
    FuelProcess(ped, now, dt);   // 3.56
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
    if(!strcmp(cmd, "engine"))   // 3.56
    {
        const int ped = Game::LocalPed(), car = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
        if(car <= 0 || Game::GetDriverOfCar(car) != ped) { SysMsg(COL_RED, "Drive a vehicle first."); return true; }
        if(FuelNoEngine(Game::GetCarModel(car))) { SysMsg(COL_SYSTEM, "A bike has no engine."); return true; }
        FuelCar& f = s_Fuel.cars[car];
        if(!f.off && Game::IsCarEngineOn(car)) { f.off = true; Game::SetCarEngineOn(car, false); SysMsg(COL_SYSTEM, "{FFA500}ENGINE: {FFFFFF}Off."); return true; }
        if(f.fuel <= 0.0f) { SysMsg(COL_RED, "{FFA500}ENGINE: {FFFFFF}No fuel - /refuel at a gas station."); return true; }
        f.off = false; Game::SetCarEngineOn(car, true); SysMsg(COL_GREEN, "{FFA500}ENGINE: {FFFFFF}On.");
        return true;
    }
    if(!strcmp(cmd, "refuel") || !strcmp(cmd, "fuel"))   // 3.56
    {
        const int ped = Game::LocalPed(), car = Game::IsCharInAnyCar(ped) ? Game::GetCarCharIsIn(ped) : 0;
        if(car <= 0 || Game::GetDriverOfCar(car) != ped) { SysMsg(COL_RED, "Drive your vehicle to a gas station first."); return true; }
        if(FuelNoEngine(Game::GetCarModel(car))) { SysMsg(COL_SYSTEM, "A bike needs no fuel."); return true; }
        FuelCar& f = s_Fuel.cars[car];
        if(!strcmp(cmd, "fuel")) { SysMsg(COL_SYSTEM, "{FFA500}FUEL: {FFFFFF}%d%%  (/refuel at a gas station, /engine on / off)", (int)f.fuel); return true; }
        Vec3 p; Game::GetCarPos(car, p);
        if(!FuelAtStation(p)) { SysMsg(COL_RED, "{FFA500}FUEL: {FFFFFF}You are not at a gas station."); return true; }
        int need = (int)ceilf(100.0f - f.fuel);
        if(need <= 0) { SysMsg(COL_SYSTEM, "{FFA500}FUEL: {FFFFFF}The tank is full."); return true; }
        const int money = Game::GetMoney();
        if(money <= 0) { SysMsg(COL_RED, "{FFA500}FUEL: {FFFFFF}$1 a %% - you have no money."); return true; }
        if(need > money) need = money;
        Game::AddMoney(-need); f.fuel = fminf(100.0f, f.fuel + need);
        SysMsg(COL_GREEN, "{FFA500}FUEL: {FFFFFF}Refueled for $%d - fuel %d%%.%s", need, (int)f.fuel, f.off ? "  /engine to start it." : "");
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
static void NeedsIconCar(ImDrawList* dl, ImVec2 c, float r)
{   // 3.56: a car from the side (white outline, black fill)
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.5f, r * 0.12f);
    ImVec2 roof[4] = { ImVec2(c.x - r * 0.45f, c.y - r * 0.05f), ImVec2(c.x - r * 0.25f, c.y - r * 0.55f), ImVec2(c.x + r * 0.3f, c.y - r * 0.55f), ImVec2(c.x + r * 0.55f, c.y - r * 0.05f) };
    dl->AddConvexPolyFilled(roof, 4, k); dl->AddPolyline(roof, 4, w, ImDrawFlags_Closed, t);
    ImVec2 a(c.x - r * 0.95f, c.y - r * 0.08f), b(c.x + r * 0.95f, c.y + r * 0.42f);
    dl->AddRectFilled(a, b, k, r * 0.15f); dl->AddRect(a, b, w, r * 0.15f, 0, t);
    for(int i = -1; i <= 1; i += 2) { ImVec2 wc(c.x + i * r * 0.52f, c.y + r * 0.45f); dl->AddCircleFilled(wc, r * 0.24f, k, 16); dl->AddCircle(wc, r * 0.24f, w, 16, t); }
}
static void NeedsIconEngine(ImDrawList* dl, ImVec2 c, float r)
{   // 3.56: an engine block: body, two pistons on top, intake on the left, fan on the right
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.5f, r * 0.12f);
    auto box = [&](float x0, float y0, float x1, float y1) { ImVec2 a(c.x + x0 * r, c.y + y0 * r), b(c.x + x1 * r, c.y + y1 * r); dl->AddRectFilled(a, b, k, r * 0.05f); dl->AddRect(a, b, w, r * 0.05f, 0, t); };
    box(-0.35f, -0.75f, -0.05f, -0.45f); box(0.1f, -0.75f, 0.4f, -0.45f);
    box(-0.6f, -0.45f, 0.65f, 0.6f);
    box(-0.95f, -0.15f, -0.6f, 0.3f); box(0.65f, -0.3f, 0.95f, 0.45f);
}
static void NeedsIconFuel(ImDrawList* dl, ImVec2 c, float r)
{   // 3.56: a fuel pump with the hose + nozzle on the right
    const ImU32 w = IM_COL32(255, 255, 255, 255), k = IM_COL32(0, 0, 0, 255);
    const float t = fmaxf(1.5f, r * 0.12f);
    ImVec2 a(c.x - r * 0.75f, c.y - r * 0.8f), b(c.x + r * 0.25f, c.y + r * 0.8f);
    dl->AddRectFilled(a, b, k, r * 0.1f); dl->AddRect(a, b, w, r * 0.1f, 0, t);
    dl->AddRectFilled(ImVec2(c.x - r * 0.55f, c.y - r * 0.6f), ImVec2(c.x + r * 0.05f, c.y - r * 0.15f), w, r * 0.05f);   // the window
    dl->AddRectFilled(ImVec2(c.x - r * 0.95f, c.y + r * 0.72f), ImVec2(c.x + r * 0.45f, c.y + r * 0.9f), w, r * 0.04f);   // the base
    dl->PathLineTo(ImVec2(c.x + r * 0.25f, c.y - r * 0.2f)); dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y - r * 0.2f));
    dl->PathLineTo(ImVec2(c.x + r * 0.6f, c.y + r * 0.45f)); dl->PathLineTo(ImVec2(c.x + r * 0.85f, c.y + r * 0.45f));
    dl->PathLineTo(ImVec2(c.x + r * 0.85f, c.y - r * 0.5f)); dl->PathLineTo(ImVec2(c.x + r * 0.62f, c.y - r * 0.75f));
    dl->PathStroke(w, 0, t);
}
static void NeedsRender()
{
    if(!NeedsOn() || Game::IsPaused() || !myDrawValid) return;
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 ds = ImGui::GetIO().DisplaySize;
    const float sz = UI::S(92.0f), gap = UI::S(10.0f), m = UI::S(18.0f);
    ImFont* pf = UI::PricedownFont() ? UI::PricedownFont() : UI::Font();
    const float fs = UI::S(pf == UI::Font() ? 21.0f : 25.0f);   // 3.55: fits in the hexagon
    // 3.56: one horizontal row, right to left: energy, hunger, then (in a vehicle) fuel, engine, speed
    struct Hex { int icon; char txt[16]; ImU32 col; } hx[5]; int n = 0;
    if(s_Fuel.car > 0)
    {
        const bool bike = FuelNoEngine(Game::GetCarModel(s_Fuel.car));
        Hex& h0 = hx[n++]; h0.icon = 2; snprintf(h0.txt, sizeof(h0.txt), "%d mph", (int)(s_Fuel.mph + 0.5f)); h0.col = IM_COL32(255, 255, 255, 255);
        if(!bike)
        {
            Hex& h1 = hx[n++]; h1.icon = 3; snprintf(h1.txt, sizeof(h1.txt), "%s", s_Fuel.engine ? "ON" : "OFF"); h1.col = s_Fuel.engine ? IM_COL32(60, 200, 70, 255) : IM_COL32(210, 40, 40, 255);
            auto it = s_Fuel.cars.find(s_Fuel.car); const float fu = it != s_Fuel.cars.end() ? it->second.fuel : 100.0f;
            Hex& h2 = hx[n++]; h2.icon = 4; snprintf(h2.txt, sizeof(h2.txt), "%d%%", (int)(fu + 0.5f)); h2.col = NeedsCol(fu);
        }
    }
    { Hex& h = hx[n++]; h.icon = 0; snprintf(h.txt, sizeof(h.txt), "%d%%", (int)(s_Need.hunger + 0.5f)); h.col = NeedsCol(s_Need.hunger); }
    { Hex& h = hx[n++]; h.icon = 1; snprintf(h.txt, sizeof(h.txt), "%d%%", (int)(s_Need.energy + 0.5f)); h.col = NeedsCol(s_Need.energy); }
    for(int i = 0; i < n; ++i)
    {
        ImVec2 a(ds.x - m - (n - i) * sz - (n - 1 - i) * gap, ds.y - m - sz);
        // 3.55: a hexagon (was a dark box); the border shows the colour of the value
        ImU32 border = hx[i].icon == 2 ? IM_COL32(190, 190, 200, 230) : ((hx[i].col & 0x00FFFFFF) | 0xE6000000);
        if(MP::IsOnDefault() && (hx[i].col & 0x00FFFFFF) == (IM_COL32(60, 200, 70, 255) & 0x00FFFFFF) && hx[i].icon != 2) border = 0;   // 4.09: default server - no green outline (yellow / red / grey stay)
        HexLabelGlass(dl, ImVec2(a.x + sz * 0.5f, a.y + sz * 0.5f), sz * 0.54f, HudOn() ? ((HudPalette().fill & 0x00FFFFFF) | 0x99000000) : IM_COL32(0, 0, 0, 128), border, UI::S(3));   // 4.07: tinted like the server HUD   // 3.58: black 50 % (was green: the border fill bled through)
        ImVec2 c(a.x + sz * 0.5f, a.y + sz * 0.36f);
        switch(hx[i].icon)
        {
            case 0: NeedsIconFood(dl, c, sz * 0.22f); break;
            case 1: NeedsIconEnergy(dl, c, sz * 0.22f); break;
            case 2: NeedsIconCar(dl, c, sz * 0.22f); break;
            case 3: NeedsIconEngine(dl, c, sz * 0.22f); break;
            default: NeedsIconFuel(dl, c, sz * 0.22f); break;
        }
        const char* t = hx[i].txt;
        float f2 = fs; ImVec2 ts = pf->CalcTextSizeA(f2, FLT_MAX, 0, t);
        if(ts.x > sz * 0.78f) { f2 *= sz * 0.78f / ts.x; ts = pf->CalcTextSizeA(f2, FLT_MAX, 0, t); }   // "120 mph" fits
        ImVec2 p(a.x + (sz - ts.x) * 0.5f, a.y + sz * 0.55f + (fs - f2) * 0.5f);   // 3.55: inside the hexagon
        const float o = fmaxf(1.0f, f2 * 0.07f);
        for(int ox = -1; ox <= 1; ++ox) for(int oy = -1; oy <= 1; ++oy) if(ox || oy) dl->AddText(pf, f2, ImVec2(p.x + ox * o, p.y + oy * o), IM_COL32(0, 0, 0, 255), t);
        dl->AddText(pf, f2, p, hx[i].col, t);
    }
}
