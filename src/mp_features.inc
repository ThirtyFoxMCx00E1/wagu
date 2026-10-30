// MP 3.34 (included by mp.cpp): features that are always on on the 4 featured servers (default,
// Brief Case War, MW3, Mega Vil Futuristic) and a host permission on custom servers (Add server >
// scroll down > Custom server permissions):
//   - AFK: 3 minutes without moving = AFK tag over your name (PKT_PAUSE paused = 2), 20 minutes =
//     you leave the server so your seat is free again (never while you host)
//   - damage indicator: "-25.00" and "<shooter> hit the head / torso / left arm ..." over the name
//     tag of the player you hit, fading out in 4 s
//   - player ids: "name (1)" in the name tags and the player list

static bool FeatFeatured()
{
    if(client.GetState() != NET_CONNECTED) return false;
    return MP::IsOnDefault() || MP::IsOnBcw() || MP::IsOnMw3() || MP::IsOnRp();
}
static bool FeatOn(uint8_t perm)
{
    if(client.GetState() != NET_CONNECTED) return false;
    if(FeatFeatured()) return true;
    if(!s_bPermsSeen) return false;   // an old server: nothing new
    return (s_nServerPerms & perm) != 0;
}

// ---- AFK ----------------------------------------------------------------------------------
#define MP_AFK_MS       (3 * 60 * 1000)
#define MP_AFK_KICK_MS  (20 * 60 * 1000)
static struct { Vec3 pos{}; float head = 0; uint64_t movedAt = 0; bool afk = false, warned = false; } s_Afk;
static void AfkTick(int ped, uint64_t now)
{
    if(!FeatOn(PERM_AFK) || ped <= 0 || Game::IsPaused())
    {
        if(s_Afk.afk) { s_Afk.afk = false; client.SetAfk(false); }
        s_Afk.movedAt = now; return;
    }
    Vec3 p; if(!Game::GetCharPos(ped, p)) return;
    float h = Game::GetCharHeading(ped);
    float dx = p.x - s_Afk.pos.x, dy = p.y - s_Afk.pos.y, dz = p.z - s_Afk.pos.z, dh = fabsf(h - s_Afk.head);
    bool moved = dx * dx + dy * dy + dz * dz > 0.25f * 0.25f || (dh > 4.0f && dh < 356.0f) || ImGui::GetIO().MouseDown[0];
    if(moved || !s_Afk.movedAt)
    {
        s_Afk.pos = p; s_Afk.head = h; s_Afk.movedAt = now; s_Afk.warned = false;
        if(s_Afk.afk) { s_Afk.afk = false; client.SetAfk(false); SysMsg(COL_SYSTEM, "You are back (not AFK any more)."); }
        return;
    }
    uint64_t idle = now - s_Afk.movedAt;
    if(!s_Afk.afk && idle > MP_AFK_MS) { s_Afk.afk = true; client.SetAfk(true); SysMsg(COL_YELLOW, "You are AFK (3 minutes without moving). Move to come back."); }
    if(s_Afk.afk && !s_Server.IsRunning())
    {
        if(!s_Afk.warned && idle > MP_AFK_KICK_MS - 60000) { s_Afk.warned = true; SysMsg(COL_RED, "AFK: you leave the server in 1 minute if you don't move."); }
        if(idle > MP_AFK_KICK_MS)
        {
            s_Afk = {}; client.SetAfk(false);
            LeaveServer("AFK for 20 minutes: you left the server so somebody else can play.");
        }
    }
}

// ---- damage indicator ---------------------------------------------------------------------
#define MP_DMG_IND_MS 4000   // 4 s, the last 1 s (20 ticks) fades
struct DmgInd { uint64_t at = 0; float amount = 0; char part[24] = ""; };
static DmgInd s_DmgInd[MP_MAX_PLAYERS];
static const char* DmgPart(int id, int weapon)
{
    RemotePlayer& p = players[id];
    if(weapon == MP_RAM_WEAPON || weapon == 49 || weapon == 50 || weapon == 51) return "body";
    if(!p.drawValid || MP_NowMs() - s_nLastShotMs > 600) return weapon <= 15 ? "torso" : "body";
    // where my last bullet hit, seen from the avatar: height = head / torso / legs, side = left / right
    float dz = s_LastShotEnd.z - p.drawPos.z;   // drawPos: about the neck
    float h = (p.ped ? Game::GetCharHeading(p.ped) : 0) * 0.0174533f, rx = cosf(h), ry = sinf(h);
    float side = (s_LastShotEnd.x - p.drawPos.x) * rx + (s_LastShotEnd.y - p.drawPos.y) * ry;
    if(dz > -0.08f) return "head";
    if(dz > -0.55f) return fabsf(side) > 0.22f ? (side > 0 ? "right arm" : "left arm") : (side > 0 ? "right torso" : "left torso");
    if(dz > -0.85f) return "groin";
    return side > 0 ? "right leg" : "left leg";
}
static void DmgIndAdd(int id, float amount, int weapon)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !FeatOn(PERM_DMG_IND)) return;
    DmgInd& d = s_DmgInd[id];
    float hp = (float)players[id].sync.health;
    float a = amount >= 1000.0f ? (hp > 0 ? hp : 100.0f) : amount;
    if(MP_NowMs() - d.at < 900) d.amount += a; else d.amount = a;   // a burst adds up
    d.at = MP_NowMs();
    MP_CopyStr(d.part, DmgPart(id, weapon), sizeof(d.part));
}
// over the name tag (above the name / sub tag / PAUSED line), so nothing overlaps
void MP::DrawDamageIndicator(ImDrawList* dl, int id, const Vec3& head)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !MP::settings.showDmgInd) return;   // 3.35
    DmgInd& d = s_DmgInd[id];
    if(!d.at) return;
    uint64_t e = MP_NowMs() - d.at;
    if(e > MP_DMG_IND_MS) { d.at = 0; return; }
    float alpha = e < MP_DMG_IND_MS - 1000 ? 1.0f : 1.0f - (e - (MP_DMG_IND_MS - 1000)) / 1000.0f;
    Vec3 top = head; top.z += 0.30f;
    float sx, sy, depth;
    if(!Game::WorldToScreen(top, sx, sy, depth)) return;
    const float k = settings.nametagSize, fs = UI::S(26.0f) * k;
    float rise = UI::S(18) * k * (e / (float)MP_DMG_IND_MS);
    float y = sy - fs * 3.3f - rise;
    char a[24]; snprintf(a, sizeof(a), "-%.2f", d.amount);
    char b[96]; snprintf(b, sizeof(b), "%s hit the %s", (myId < MP_MAX_PLAYERS && players[myId].active) ? players[myId].name : settings.name, d.part);
    float bfs = fs * 0.72f;
    ImVec2 as = UI::Font()->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0, a), bs = UI::Font()->CalcTextSizeA(bfs, FLT_MAX, 0, b);
    UI::OutlinedText(dl, bfs, ImVec2(sx - bs.x * 0.5f, y), IM_COL32(255, 255, 255, (int)(235 * alpha)), b, alpha);
    UI::OutlinedText(dl, fs * 1.05f, ImVec2(sx - as.x * 0.5f, y - fs * 1.1f), IM_COL32(255, 70, 60, (int)(255 * alpha)), a, alpha);
}

// ---- player ids ---------------------------------------------------------------------------
void MP::NameTagId(int id, char* name, size_t n)
{
    if(id < 0 || id >= MP_MAX_PLAYERS || !FeatOn(PERM_SHOW_IDS)) return;
    size_t l = strlen(name); if(l + 6 >= n) return;
    snprintf(name + l, n - l, " (%d)", id);
}
bool MP::IsAfk(int id) { return id >= 0 && id < MP_MAX_PLAYERS && players[id].afk && FeatOn(PERM_AFK); }
