// 3.21: hospital SAFE ZONES + 5 s SPAWN PROTECTION (zones: net/safezone.h, shared with the server)
//
// Inside a safe zone (all 8 GTA SA hospitals):
//   - nobody can hurt you: bullets, melee, rockets, grenades, car bombs, fire, ramming, being pulled
//     out of your car (proofs on you + your car; damage packets dropped here AND on the server)
//   - you can't hurt anyone: your weapon is put away (it stays in your inventory), drive-by is off,
//     and hits you make don't count
//   - no explosion / fire from another player is created inside a zone
// After every respawn (and the first spawn): 5 s spawn protection, same rules, wherever you spawn.
// Off on Brief Case War servers (own base protection), during /race /derby /dm /job and on a
// story mission (own world).

static struct
{
    int      zone = -1;            // zone the local player is in (-1 = none)
    uint64_t spawnUntil = 0;       // spawn protection end (MP_NowMs)
    bool     proofs = false;       // WE turned the proofs on
    int      provedCar = 0;
    bool     driveByOff = false;
    uint64_t lastWeap = 0, lastRefresh = 0;
    bool     wasConnected = false;
    uint8_t  remDead[MP_MAX_PLAYERS] = {0};
    uint64_t remProt[MP_MAX_PLAYERS] = {0};   // remote player's spawn protection end
} s_Safe;

static bool SafeZonesOn()
{
    return client.GetState() == NET_CONNECTED && myId != MP_INVALID_ID && !BcwOnServer()
        && !s_bLocalSeparate && !InEvent();
}
static bool SafeProtected()
{
    if(!SafeZonesOn()) return false;
    return s_Safe.zone >= 0 || MP_NowMs() < s_Safe.spawnUntil;
}
static void SafeOnRespawn(uint64_t now) { s_Safe.spawnUntil = now + MP_SPAWN_PROTECT_MS; }

static Vec3 SafeSyncPos(const SyncData& s)
{
    return (s.flags & SYNC_IN_CAR) ? MP_LoadVec3(s.carPos) : MP_LoadVec3(s.pos);
}
static bool SafeBlocksHit(int id)
{
    if(!SafeZonesOn()) return false;
    if(SafeProtected()) return true;                       // protected players don't hurt anyone either
    if(id < 0 || id >= MP_MAX_PLAYERS || !players[id].active) return false;
    if(MP_NowMs() < s_Safe.remProt[id]) return true;       // they just respawned
    Vec3 p = SafeSyncPos(players[id].sync);
    return MP_InSafeZone(p.x, p.y, p.z);
}
static bool SafePointProtected(const Vec3& p)
{
    if(client.GetState() != NET_CONNECTED || BcwOnServer()) return false;
    return MP_InSafeZone(p.x, p.y, p.z);
}

static void SafeRelease(int ped)
{
    if(s_Safe.proofs)
    {
        if(ped > 0 && !s_Cheat.invincible) Game::SetCharProofs(ped, false, false, false, false, false);
        if(s_Safe.provedCar && Game::CarPtr(s_Safe.provedCar) && !s_Cheat.invincible) Game::SetCarProofs(s_Safe.provedCar, false, false, false, false, false);
        s_Cheat.applied = false;   // /invincible or /nofall: ProcessCheats puts its own proofs back
    }
    s_Safe.proofs = false; s_Safe.provedCar = 0;
    if(s_Safe.driveByOff) { Game::SetDriveByAllowed(true); s_Safe.driveByOff = false; }
}

static void ProcessSafeZones(int ped, uint64_t now)
{
    bool conn = client.GetState() == NET_CONNECTED;
    if(conn && !s_Safe.wasConnected) { s_Safe.spawnUntil = now + MP_SPAWN_PROTECT_MS; memset(s_Safe.remProt, 0, sizeof(s_Safe.remProt)); }
    s_Safe.wasConnected = conn;

    // the others: respawned -> 5 s spawn protection on our screen too
    if(conn) for(int i = 0; i < MP_MAX_PLAYERS; ++i)
    {
        if(!players[i].active) { s_Safe.remDead[i] = 0; continue; }
        uint8_t dead = (players[i].sync.flags & SYNC_DEAD) ? 1 : 0;
        if(s_Safe.remDead[i] && !dead) s_Safe.remProt[i] = now + MP_SPAWN_PROTECT_MS;
        s_Safe.remDead[i] = dead;
    }

    if(!SafeZonesOn() || ped <= 0 || JoinFlowActive() || !Game::IsReady())
    {
        if(s_Safe.zone >= 0) s_Safe.zone = -1;
        SafeRelease(ped);
        return;
    }

    bool inCar = Game::IsCharInAnyCar(ped);
    int car = inCar ? Game::GetCarCharIsIn(ped) : 0;
    Vec3 p;
    if(car) Game::GetCarPos(car, p); else if(!Game::GetCharPos(ped, p)) return;
    int z = MP_SafeZoneAt(p.x, p.y, p.z);
    if(z != s_Safe.zone)
    {
        if(z >= 0) SysMsg(COL_GREEN, "SAFE ZONE: %s - nobody can hurt you here, weapons are put away.", g_MPSafeZones[z].name);
        else if(s_Safe.zone >= 0) SysMsg(COL_YELLOW, "You left the safe zone - you can be hurt again.");
        s_Safe.zone = z;
    }

    bool prot = z >= 0 || now < s_Safe.spawnUntil;
    if(!prot) { SafeRelease(ped); return; }

    // proofs: everything (bullet, fire, explosion, collision / fall, melee) on you and your car
    bool refresh = now - s_Safe.lastRefresh > 1000;   // missions / the game reset these sometimes
    if(!s_Safe.proofs || refresh)
    {
        Game::SetCharProofs(ped, true, true, true, true, true);
        s_Safe.proofs = true;
        s_Safe.lastRefresh = now;
    }
    if(car != s_Safe.provedCar || (car && refresh))
    {
        if(s_Safe.provedCar && s_Safe.provedCar != car && Game::CarPtr(s_Safe.provedCar) && !s_Cheat.invincible)
            Game::SetCarProofs(s_Safe.provedCar, false, false, false, false, false);
        if(car) Game::SetCarProofs(car, true, true, true, true, true);
        s_Safe.provedCar = car;
    }

    // no weapons inside a zone (spawn protection alone lets you pick a weapon, but hits don't count)
    if(z >= 0)
    {
        if(!inCar && now - s_Safe.lastWeap > 100 && Game::GetCharWeapon(ped) != 0) { s_Safe.lastWeap = now; Game::SetCurrentWeapon(ped, 0); }
        if(!s_Safe.driveByOff) { Game::SetDriveByAllowed(false); s_Safe.driveByOff = true; }
    }
    else if(s_Safe.driveByOff) { Game::SetDriveByAllowed(true); s_Safe.driveByOff = false; }
}

static void RenderSafeZones()
{
    if(client.GetState() != NET_CONNECTED || BcwOnServer() || !Game::IsReady() || Game::IsPaused()) return;
    if(MP::AuthScreenShown()) return;   // 3.21.7: no SAFE ZONE / SPAWN PROTECTION text over the login screen
    float now = (float)(MP_NowMs() % 100000000ULL);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    for(int i = 0; i < g_MPSafeZoneCount; ++i)
    {
        const MPSafeZone& z = g_MPSafeZones[i];
        Vec3 c = { z.x, z.y, z.z - 1.0f };
        if(BcwCamDist(c) > 420.0f) continue;
        BcwDrawZone(bg, c, MP_SAFEZONE_RADIUS, 0x3CDC64FF, now, false, 5.0f);   // green ring + low wall
        if(!OccPoint({ z.x, z.y, z.z + 6.0f })) continue;   // 3.21.4: behind houses: no text
        BcwLabel3D(bg, { z.x, z.y, z.z + 7.0f }, "SAFE ZONE", 0x3CDC64FF, 44, 60.0f);
        BcwLabel3D(bg, { z.x, z.y, z.z + 5.2f }, z.name, 0xFFFFFFFF, 26, 50.0f);
    }
    if(!SafeZonesOn()) return;
    uint64_t ms = MP_NowMs();
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float y = ImGui::GetIO().DisplaySize.y * 0.16f;
    if(s_Safe.zone >= 0) BcwCenterText(fg, UI::S(30), y, UI::FromRGBA(0x3CDC64FF, 1.0f), "SAFE ZONE - weapons away, nobody can hurt you");
    else if(ms < s_Safe.spawnUntil)
    {
        char t[64]; snprintf(t, sizeof(t), "SPAWN PROTECTION  %d", (int)((s_Safe.spawnUntil - ms + 999) / 1000));
        BcwCenterText(fg, UI::S(30), y, UI::FromRGBA(0x3CDC64FF, 1.0f), t);
    }
}
