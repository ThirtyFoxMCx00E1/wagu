// MP 3.34 - Mega Vil Futuristic: the COSMETICS room (included by mp.cpp after mp_rp_inv.inc).
// In the lobby the second button (a T-shirt icon, next to "Spawn") flies the camera - eased, like the
// default server's < > - into the Didier Sachs store (area 14). There you stand between clothes rails,
// shelves and mirrors; a side bar on the left shows the wardrobe categories (net/rp.h g_RpCos), the
// outfits of the picked category are on the right. SAVE sends RPC_SKIN_SET (the server keeps it with
// your citizen record + card, so it is there after you leave), the camera flies back to the lobby and
// you press Spawn. CANCEL puts your old outfit back.
// 3.37: CLOTHES tab = real CJ clothes per body part (net/cjclothes.h), the camera zooms to the part.

#define RP_COS_AREA 14
static const Vec3 kRpCosSpot = { 204.18f, -163.40f, 1000.523f };   // 3.35: Didier Sachs shop floor (was the door = in the wall)
// the shop opens to +y from the door (204.3,-168.7): the camera stands further in, looking back at you
static const float kRpCosCamDist = 3.3f;
struct RpCosState
{
    bool     on = false; int phase = 0;   // 0 flying in, 1 browsing, 2 flying out
    uint64_t t0 = 0; int cat = 0, sel = -1; int origSkin = 0, curSkin = -1, wantSkin = -1;
    Vec3     fromP{}, fromL{}, toP{}, toL{}, camP{}, camL{}; float yaw = 0;
    int      obj[16] = {}; int nObj = 0; bool objDone = false; float scroll = 0;
    // 3.35.1: landing - he is NOT frozen until he really stands on the shop floor (was frozen mid-fall)
    bool     landed = false; int still = 0; uint64_t placedAt = 0; float floorZ = -1000.0f; int replaced = 0;
    // 3.37: CLOTHES mode (real CJ clothes on body parts) + the close-up camera
    int      mode = 0, ccat = 0;              // mode 0 = OUTFITS (skins), 1 = CLOTHES
    int      cj[18];                          // per body part: catalog index, -1 untouched, -2 off / default
    bool     cjDirty = false, cjApply = false;
    int      focus = 0; uint64_t lastTick = 0; bool viewInit = false; Vec3 viewP{}, viewL{};
    char     tried[48] = {};
    RpCosState() { for(int i = 0; i < 18; ++i) cj[i] = -1; }
};
static RpCosState s_Cos;
static bool RpCosActive() { return s_Cos.on; }

// ---- 3.37: the CJ outfit (kept on this phone in files/MP/outfit.txt, "part texture" per line) ----
#include "net/cjclothes.h"
struct CjOutfit { bool loaded = false, saved = false; int idx[18]; CjOutfit() { for(int i = 0; i < 18; ++i) idx[i] = -1; } };
static CjOutfit s_CjOut;
static void CjOutLoad()
{
    if(s_CjOut.loaded) return;
    s_CjOut.loaded = true;
    char path[512]; snprintf(path, sizeof(path), "%s/outfit.txt", MP::DataDir());
    FILE* f = fopen(path, "r"); if(!f) return;
    char line[128];
    while(fgets(line, sizeof(line), f))
    {
        int part = -1; char tex[64] = {};
        if(sscanf(line, "%d %63s", &part, tex) != 2 || part < 0 || part > 17) continue;
        if(!strcmp(tex, "-")) { s_CjOut.idx[part] = -2; s_CjOut.saved = true; continue; }
        for(int i = 0; i < CJ_CLOTH_COUNT; ++i)
            if(g_CjClothes[i].part == part && !strcasecmp(g_CjClothes[i].tex, tex)) { s_CjOut.idx[part] = i; s_CjOut.saved = true; break; }
    }
    fclose(f);
}
static void CjOutSave(const int* idx)
{
    CjOutLoad();
    for(int p = 0; p < 18; ++p) if(idx[p] != -1) s_CjOut.idx[p] = idx[p];
    s_CjOut.saved = true;
    char path[512]; snprintf(path, sizeof(path), "%s/outfit.txt", MP::DataDir());
    FILE* f = fopen(path, "w"); if(!f) return;
    fprintf(f, "# MP CJ outfit (wardrobe): body part + texture\n");
    for(int p = 0; p < 18; ++p)
    {
        if(s_CjOut.idx[p] == -1) continue;
        if(s_CjOut.idx[p] == -2) fprintf(f, "%d -\n", p); else fprintf(f, "%d %s\n", p, g_CjClothes[s_CjOut.idx[p]].tex);
    }
    fclose(f);
}
// put one body part on (the default piece / nothing for -2), no rebuild
static void CjPutPart(int part, int idx)
{
    if(idx == -1) return;
    if(idx >= 0) { Game::SetLocalClothes(g_CjClothes[idx].tex, g_CjClothes[idx].model, part); return; }
    for(const CjDefault& d : g_CjDefault) if(d.part == part) { Game::SetLocalClothes(d.tex, d.model, part); return; }
    Game::SetLocalClothes(nullptr, nullptr, part);   // accessory off
}
// he must be CJ: puts all the parts of idx on and rebuilds him
static void CjApplyOutfit(const int* idx)
{
    if(Game::LocalModel() != 0 && !Game::SetLocalModel(0)) return;
    for(int p = 0; p < 18; ++p) CjPutPart(p, idx[p]);
    Game::RebuildPlayer();
}
// Mega Vil spawn: your saved skin, or CJ in your saved clothes
static void RpApplyMySkin(int fallback)
{
    if(!s_Rp.haveMe) return;
    if(s_Rp.me.skin) { Game::SetLocalModel(s_Rp.me.skin); return; }
    CjOutLoad();
    if(s_CjOut.saved) CjApplyOutfit(s_CjOut.idx);
    else if(fallback > 0) Game::SetLocalModel(fallback);
}
// the close-up camera: where to look for each body part
enum { CJF_FULL, CJF_HEAD, CJF_FACE, CJF_TORSO, CJF_NECK, CJF_WRIST, CJF_LEGS, CJF_FEET, CJF_BODY };
static const int g_CjCatFocus[CJC_COUNT] = { CJF_TORSO, CJF_HEAD, CJF_LEGS, CJF_HEAD, CJF_FEET, CJF_FACE, CJF_NECK, CJF_WRIST, CJF_BODY };
static const char* const g_CjFocusName[] = { "", "HEAD", "FACE", "UPPER BODY", "NECK", "WRIST", "LEGS", "SHOES", "OUTFIT" };
struct CjFocus { float dist, camZ, lookZ, lookX; };   // from his centre (ped faces +y, his left hand is -x)
static const CjFocus g_CjFocus[] = {
    { kRpCosCamDist, 0.55f, 0.05f, 0.00f },   // full
    { 1.00f, 0.78f, 0.66f, 0.00f },          // head (hair / hats)
    { 0.80f, 0.70f, 0.64f, 0.00f },          // face (glasses)
    { 1.55f, 0.50f, 0.28f, 0.00f },          // upper body (tops)
    { 0.95f, 0.55f, 0.42f, 0.00f },          // neck (chains)
    { 1.10f, 0.20f, 0.02f, -0.30f },         // wrist (watches)
    { 1.85f, 0.00f, -0.42f, 0.00f },         // legs
    { 1.25f, -0.35f, -0.82f, 0.00f },        // feet
    { 2.40f, 0.45f, 0.00f, 0.00f },          // whole outfit
};

// the props: clothes rails / shirts / shoe shelves round the spot (SA clothes shop models 2371 - 2401)
struct RpCosProp { int model; float dx, dy, dz, rz; };
static const RpCosProp kRpCosProps[] = {   // 3.35: on both sides of you, nothing between you and the camera
    { 2372, -2.0f,  0.4f, -1.0f,  90 }, { 2372,  2.0f,  0.4f, -1.0f, 270 }, { 2374, -2.0f, -1.2f, -1.0f,  90 },
    { 2374,  2.0f, -1.2f, -1.0f, 270 }, { 2381, -2.4f,  1.8f, -1.0f,  90 }, { 2389,  2.4f,  1.8f, -1.0f, 270 },
    { 2394, -1.2f, -1.6f, -1.0f,   0 }, { 2399,  1.2f, -1.6f, -1.0f,   0 },
};
static void RpCosFreeProps()
{
    for(int i = 0; i < s_Cos.nObj; ++i) if(s_Cos.obj[i] > 0) Game::DeleteWorldObject(s_Cos.obj[i]);
    s_Cos.nObj = 0; s_Cos.objDone = false;
}
static void RpCosMakeProps()
{
    if(s_Cos.objDone) return;
    const int n = (int)(sizeof(kRpCosProps) / sizeof(kRpCosProps[0]));
    while(s_Cos.nObj < n)
    {
        const RpCosProp& p = kRpCosProps[s_Cos.nObj];
        int o = Game::CreateMapObject(p.model, { kRpCosSpot.x + p.dx, kRpCosSpot.y + p.dy, kRpCosSpot.z + p.dz }, 0, 0, p.rz);
        if(o == 0) return;   // still loading: next tick
        s_Cos.obj[s_Cos.nObj++] = o;   // -1 = no room: skipped
    }
    s_Cos.objDone = true;
}
// the camera: 3.4 m from you towards the open side of the room (a clear line), a little above
static void RpCosPickCam()   // 3.35: fixed (the collision is not there yet when the room is picked)
{
    s_Cos.yaw = 1.570796f;
    s_Cos.toP = { kRpCosSpot.x, kRpCosSpot.y + kRpCosCamDist, kRpCosSpot.z + 0.55f };
    s_Cos.toL = { kRpCosSpot.x, kRpCosSpot.y, kRpCosSpot.z + 0.05f };
}
// 3.36: the floor under the spot (-1000 = the collision is not loaded yet)
static float RpCosFloor()
{
    Game::PreloadArea(kRpCosSpot, false);
    float z = Game::GroundBelow(kRpCosSpot.x, kRpCosSpot.y, kRpCosSpot.z + 1.2f);
    if(z < -999.0f || fabsf(z - (kRpCosSpot.z - 1.0f)) > 2.5f) return -1000.0f;   // not this room's floor
    return z;
}
// 3.35.2: Game::PlaceChar() (SET_CHAR_COORDINATES) puts his FEET at the given z, GetCharPos() gives his
// CENTRE (feet + ~1 m). 3.35.1 placed him at floor + 1 (= 1 m in the air) and then checked his centre
// against floor + 1 -> he fell, the "moved off the spot" check teleported him up again, he fell again...
static const float kRpCosCentre = 1.0f;   // ped centre above his feet
static void RpCosPlacePed(int ped)
{
    if(ped <= 0) return;
    Game::FreezeChar(ped, false);
    Game::ClearTasksNow(ped);   // no falling / jumping anim left over
    Game::PreloadArea(kRpCosSpot, true);   // the floor collision is in before he stands on it
    float f = RpCosFloor(); if(f > -999.0f) s_Cos.floorZ = f;
    if(s_Cos.floorZ < -999.0f)
    {   // no floor yet: hold him hidden at the spot (RpCosSettle places him when the floor is in)
        Game::PlaceChar(ped, { kRpCosSpot.x, kRpCosSpot.y, kRpCosSpot.z - kRpCosCentre });
        Game::FreezeChar(ped, true); Game::SetCharVisible(ped, false);
        s_Cos.landed = false; s_Cos.still = 0; s_Cos.placedAt = MP_NowMs();
        return;
    }
    Game::PlaceChar(ped, { kRpCosSpot.x, kRpCosSpot.y, s_Cos.floorZ + 0.02f });   // feet ON the floor (no drop)
    Game::SetCharHeading(ped, 0.0f);   // face the camera (+y)
    Game::ClearTasksNow(ped);
    Game::SetCharVisible(ped, true);
    s_Cos.landed = false; s_Cos.still = 0; s_Cos.placedAt = MP_NowMs();
}
// every tick in the room until he stands: no floor yet -> keep him held; floor there -> he is already on
// it, wait a few still frames (standing idle), then freeze him where he IS (no second teleport)
static void RpCosSettle(int ped, uint64_t now)
{
    if(s_Cos.landed || ped <= 0) return;
    Vec3 q; if(!Game::GetCharPos(ped, q)) return;
    if(s_Cos.floorZ < -999.0f)
    {
        float f = RpCosFloor();
        if(f < -999.0f) { Game::FreezeChar(ped, true); return; }
        s_Cos.floorZ = f; RpCosPlacePed(ped); return;   // the floor came in: put him on it
    }
    const float stand = s_Cos.floorZ + kRpCosCentre;
    if(q.z < s_Cos.floorZ - 1.5f)
    {   // really fell through the floor (collision dropped): put him back, at most 3 times
        if(now - s_Cos.placedAt > 500 && s_Cos.replaced < 3) { ++s_Cos.replaced; RpCosPlacePed(ped); }
        return;
    }
    Vec3 v{}; Game::GetMoveSpeed(ped, false, v);
    if(fabsf(v.z) < 0.15f) ++s_Cos.still; else s_Cos.still = 0;
    if(s_Cos.still >= 6 || now - s_Cos.placedAt > 2500)
    {
        Game::ClearTasksNow(ped);   // any land anim off -> he stands
        Game::SetCharHeading(ped, 0.0f);
        Game::FreezeChar(ped, true);   // frozen where he stands (no re-teleport = no new fall)
        s_Cos.landed = true;
        // the camera at his real height
        float cz = (fabsf(q.z - stand) < 0.6f) ? q.z : stand;
        s_Cos.toP.z = cz + 0.55f; s_Cos.toL.z = cz + 0.05f;
    }
}
static void RpCosOpen()
{
    if(s_Cos.on || !s_Rp.haveMe) return;
    s_Cos = RpCosState(); s_Cos.on = true; s_Cos.phase = 0; s_Cos.t0 = MP_NowMs();
    s_Cos.origSkin = s_Rp.me.skin; s_Cos.curSkin = s_Rp.me.skin;
    for(int i = 0; i < RP_COS_COUNT; ++i) if(g_RpCos[i].skin == s_Rp.me.skin) { s_Cos.sel = i; s_Cos.cat = g_RpCos[i].cat; break; }
    s_Cos.fromP = kRpLobbyCam; s_Cos.fromL = kRpLobbyLook;
    CjOutLoad();
    for(int p = 0; p < 18; ++p) s_Cos.cj[p] = s_CjOut.idx[p];
    if(s_Rp.me.skin == 0) s_Cos.mode = 1;   // CJ: open on the clothes
    Game::PreloadArea(kRpCosSpot, true);
    RpCosPickCam();
    RpClick(RPS_CLICK);
}
static void RpCosClose(bool save)
{
    if(!s_Cos.on || s_Cos.phase == 2) return;
    if(save && s_Cos.sel >= 0)
    {
        int sk = g_RpCos[s_Cos.sel].skin;
        RpSend(RPC_SKIN_SET, 0, 0, sk); s_Rp.me.skin = (uint16_t)sk;
        if(sk == 0 && (s_Cos.cjDirty || s_Cos.mode == 1))
        {   // 3.37: CJ in his clothes - the pieces stay on this phone (outfit.txt), the others see them (clothes sync)
            CjOutSave(s_Cos.cj);
            SysMsg(RP_SKY, "[MEGA VIL] CJ outfit saved. Press Spawn.");
        }
        else SysMsg(RP_SKY, "[MEGA VIL] Outfit saved: %s. Press Spawn.", g_RpCos[s_Cos.sel].name);
    }
    if(save)
    {   // 3.35: SAVE goes straight back to the lobby screen (no camera flight)
        RpCosFreeProps(); RpSetInterior(0);
        s_Cos.on = false; RpClick(RPS_CLICK);
        RpPark(Game::LocalPed());
        return;
    }
    else if(s_Cos.curSkin != s_Cos.origSkin) s_Cos.wantSkin = s_Cos.origSkin;
    s_Cos.phase = 2; s_Cos.t0 = MP_NowMs();
    s_Cos.fromP = s_Cos.camP; s_Cos.fromL = s_Cos.camL; s_Cos.toP = kRpLobbyCam; s_Cos.toL = kRpLobbyLook;
    RpClick(RPS_CLICK);
}
static void RpCosAbort()   // left the server / the lobby while in the room
{
    if(!s_Cos.on) return;
    RpCosFreeProps(); s_Cos.on = false;
}
static float RpCosEase(float k) { return k < 0.5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; }
// game tick (lobby only)
static void RpCosTick(int ped, uint64_t now)
{
    if(!s_Cos.on) return;
    const float flightMs = 2200.0f;
    float k = (now - s_Cos.t0) / flightMs; if(k > 1) k = 1;
    if(s_Cos.phase == 0 || s_Cos.phase == 2)
    {
        float e = RpCosEase(k), arc = sinf(k * 3.14159f) * 60.0f;
        s_Cos.camP = { s_Cos.fromP.x + (s_Cos.toP.x - s_Cos.fromP.x) * e, s_Cos.fromP.y + (s_Cos.toP.y - s_Cos.fromP.y) * e,
                       s_Cos.fromP.z + (s_Cos.toP.z - s_Cos.fromP.z) * e + arc };
        s_Cos.camL = { s_Cos.fromL.x + (s_Cos.toL.x - s_Cos.fromL.x) * e, s_Cos.fromL.y + (s_Cos.toL.y - s_Cos.fromL.y) * e,
                       s_Cos.fromL.z + (s_Cos.toL.z - s_Cos.fromL.z) * e };
        Game::SetFixedCamera(s_Cos.camP, s_Cos.camL);
        if(s_Cos.phase == 0 && k > 0.6f && s_Rp.interior != RP_COS_AREA) { RpSetInterior(RP_COS_AREA); RpCosPlacePed(ped); }
        if(s_Cos.phase == 2 && k > 0.4f && s_Rp.interior == RP_COS_AREA) { RpCosFreeProps(); RpSetInterior(0); }
        if(k >= 1)
        {
            if(s_Cos.phase == 0) s_Cos.phase = 1;
            else
            {
                if(s_Cos.wantSkin >= 0) { Game::SetLocalModel(s_Cos.wantSkin); ped = Game::LocalPed(); }
                if(s_Cos.cjDirty && Game::LocalModel() == 0)
                {   // CANCEL after trying clothes: the saved CJ outfit back (or the default pieces)
                    int back[18]; for(int p = 0; p < 18; ++p) back[p] = s_Cos.cj[p] == -1 ? -1 : (s_CjOut.idx[p] != -1 ? s_CjOut.idx[p] : -2);
                    CjApplyOutfit(back); ped = Game::LocalPed();
                }
                s_Cos.on = false; RpPark(ped);
                return;
            }
        }
    }
    if(s_Rp.interior == RP_COS_AREA) { RpCosMakeProps(); if(s_Cos.phase != 2) RpCosSettle(ped > 0 ? ped : Game::LocalPed(), now); }
    if(s_Cos.phase == 1)
    {
        // a new outfit: change the model, put him back on the spot
        if(s_Cos.sel >= 0 && g_RpCos[s_Cos.sel].skin != s_Cos.curSkin)
        {
            if(Game::SetLocalModel(g_RpCos[s_Cos.sel].skin)) { s_Cos.curSkin = g_RpCos[s_Cos.sel].skin; s_Cos.replaced = 0; RpCosPlacePed(Game::LocalPed()); }
        }
        if(s_Cos.landed && now % 1000 < 40)
        {   // 3.35.2: only when he is really gone (pushed far / under the floor), and at most 3 times
            int lp = Game::LocalPed(); Vec3 q;
            if(lp > 0 && s_Cos.replaced < 3 && Game::GetCharPos(lp, q) &&
               (fabsf(q.x - kRpCosSpot.x) + fabsf(q.y - kRpCosSpot.y) > 1.5f || q.z < s_Cos.floorZ - 1.5f))
            { ++s_Cos.replaced; RpCosPlacePed(lp); }
        }
        // 3.37: clothes picked -> CJ wears them (switches to CJ first)
        if(s_Cos.cjApply)
        {
            s_Cos.cjApply = false;
            bool wasCj = Game::LocalModel() == 0;
            CjApplyOutfit(s_Cos.cj);
            if(Game::LocalModel() == 0)
            {
                s_Cos.curSkin = 0;
                for(int i = 0; i < RP_COS_COUNT; ++i) if(g_RpCos[i].skin == 0) { s_Cos.sel = i; break; }
                if(!wasCj) { s_Cos.replaced = 0; RpCosPlacePed(Game::LocalPed()); }
            }
        }
        // 3.37: the camera glides to the body part you dress (a slow sway round him while close)
        const CjFocus& fc = g_CjFocus[s_Cos.focus];
        const float cz = s_Cos.toP.z - 0.55f;   // his centre (RpCosSettle sets toP at his real height)
        float ang = s_Cos.focus ? sinf(now * 0.0006f) * 0.22f : 0.0f;
        Vec3 tp = { kRpCosSpot.x + fc.lookX + sinf(ang) * fc.dist, kRpCosSpot.y + cosf(ang) * fc.dist, cz + fc.camZ };
        Vec3 tl = { kRpCosSpot.x + fc.lookX, kRpCosSpot.y, cz + fc.lookZ };
        if(!s_Cos.viewInit) { s_Cos.viewInit = true; s_Cos.viewP = s_Cos.toP; s_Cos.viewL = s_Cos.toL; s_Cos.lastTick = now; }
        float dt = (now - s_Cos.lastTick) / 1000.0f; s_Cos.lastTick = now; if(dt > 0.1f) dt = 0.1f;
        float a = 1.0f - expf(-dt * 4.5f);
        s_Cos.viewP = { s_Cos.viewP.x + (tp.x - s_Cos.viewP.x) * a, s_Cos.viewP.y + (tp.y - s_Cos.viewP.y) * a, s_Cos.viewP.z + (tp.z - s_Cos.viewP.z) * a };
        s_Cos.viewL = { s_Cos.viewL.x + (tl.x - s_Cos.viewL.x) * a, s_Cos.viewL.y + (tl.y - s_Cos.viewL.y) * a, s_Cos.viewL.z + (tl.z - s_Cos.viewL.z) * a };
        Game::SetFixedCamera(s_Cos.viewP, s_Cos.viewL);
        s_Cos.camP = s_Cos.viewP; s_Cos.camL = s_Cos.viewL;
    }
}
// the side bars: categories on the left, the outfits of the category on the right, SAVE / CANCEL
static void RpCosDraw(float now)
{
    (void)now;
    if(!s_Cos.on || s_Cos.phase != 1) return;
    ImVec2 ds = ImGui::GetIO().DisplaySize;
    const uint64_t nowMs = MP_NowMs();
    BcwBeginOverlay("##RpCos", ImVec2(0, 0), ds, 0.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 glass = IM_COL32(10, 10, 12, 153), sky = RpCol(RP_SKY, 1), orange = IM_COL32(245, 130, 32, 255);
    // title
    BcwCenterText(dl, UI::S(30), UI::S(26), IM_COL32_WHITE, "WARDROBE");
    ImGuiIO& io = ImGui::GetIO();
    // 3.37: top of the left bar: OUTFITS (whole skins) | CLOTHES (CJ pieces)
    const float lw = UI::S(240), gap = UI::S(6), x0 = UI::S(24), y0 = UI::S(90), th = UI::S(48);
    for(int m = 0; m < 2; ++m)
    {
        const float tw = (lw - gap) * 0.5f;
        ImVec2 a(x0 + m * (tw + gap), y0), b(a.x + tw, a.y + th);
        ImGui::SetCursorScreenPos(a);
        if(ImGui::InvisibleButton(m ? "##cosM1" : "##cosM0", ImVec2(tw, th)) && s_Cos.mode != m)
        { s_Cos.mode = m; s_Cos.scroll = 0; s_Cos.focus = m ? g_CjCatFocus[s_Cos.ccat] : CJF_FULL; RpClick(RPS_CLICK); }
        bool on = s_Cos.mode == m;
        dl->AddRectFilled(a, b, on ? orange : glass);
        RpCenterIn(dl, UI::S(18), a, b, on ? IM_COL32(20, 12, 4, 255) : IM_COL32_WHITE, m ? "CLOTHES" : "OUTFITS");
    }
    // left: categories (skin groups or body parts)
    const int nCat = s_Cos.mode ? CJC_COUNT : RPCO_CAT_COUNT;
    const float cy0 = y0 + th + UI::S(12), avail = ds.y - UI::S(130) - cy0;
    float rowH = avail / nCat - gap; if(rowH > UI::S(56)) rowH = UI::S(56); if(rowH < UI::S(30)) rowH = UI::S(30);
    for(int c = 0; c < nCat; ++c)
    {
        ImVec2 a(x0, cy0 + c * (rowH + gap)), b(a.x + lw, a.y + rowH);
        ImGui::SetCursorScreenPos(a);
        char id[16]; snprintf(id, sizeof(id), "##cc%d", c);
        int& cur = s_Cos.mode ? s_Cos.ccat : s_Cos.cat;
        if(ImGui::InvisibleButton(id, ImVec2(lw, rowH)))
        {
            cur = c; s_Cos.scroll = 0; RpClick(RPS_CLICK);
            s_Cos.focus = s_Cos.mode ? g_CjCatFocus[c] : CJF_FULL;   // the camera goes close to that body part
        }
        bool on = cur == c;
        dl->AddRectFilled(a, b, on ? IM_COL32(56, 189, 248, 120) : glass);
        dl->AddRectFilled(a, ImVec2(a.x + UI::S(4), b.y), on ? sky : IM_COL32(255, 255, 255, 40));
        float ic = rowH * 0.55f; if(ic > UI::S(30)) ic = UI::S(30);
        RpHudIcon(dl, RPH_COSMETIC, ImVec2(a.x + UI::S(32), (a.y + b.y) * 0.5f), ic, on ? IM_COL32_WHITE : IM_COL32(200, 200, 200, 200));
        const float fs = rowH < UI::S(44) ? UI::S(16) : UI::S(19);
        RpPlainText(dl, fs, ImVec2(a.x + UI::S(60), a.y + (rowH - fs) * 0.5f), IM_COL32_WHITE, s_Cos.mode ? g_CjCats[c] : g_RpCosCats[c]);
    }
    // right: the boxes of the category (2 columns, drag / wheel to scroll)
    const float rw = UI::S(330), cw = (rw - gap) * 0.5f, chh = UI::S(70), rx = ds.x - rw - UI::S(24);
    int list[CJ_CLOTH_COUNT + RP_COS_COUNT + 1]; int n = 0;
    if(s_Cos.mode)
    {
        list[n++] = -2;   // "default" / "none"
        for(int i = 0; i < CJ_CLOTH_COUNT; ++i) if(g_CjClothes[i].cat == s_Cos.ccat) list[n++] = i;
    }
    else for(int i = 0; i < RP_COS_COUNT; ++i) if(g_RpCos[i].cat == s_Cos.cat) list[n++] = i;
    const ImVec2 va(rx, y0), vb(rx + rw, ds.y - UI::S(130));
    const int rows = (n + 1) / 2;
    const float contentH = rows * (chh + gap) - gap, viewH = vb.y - va.y, maxS = contentH > viewH ? contentH - viewH : 0;
    ImGui::SetCursorScreenPos(va);
    bool tap = ImGui::InvisibleButton("##cosgrid", ImVec2(rw, viewH));
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(0, UI::S(6))) s_Cos.scroll -= io.MouseDelta.y;
    if(ImGui::IsItemHovered() && io.MouseWheel != 0) s_Cos.scroll -= io.MouseWheel * (chh + gap);
    if(s_Cos.scroll < 0) s_Cos.scroll = 0;
    if(s_Cos.scroll > maxS) s_Cos.scroll = maxS;
    const int part = g_CjCatPart[s_Cos.ccat];
    if(tap && io.MouseDragMaxDistanceSqr[0] < UI::S(12) * UI::S(12))
    {
        float lx = io.MousePos.x - va.x, ly = io.MousePos.y - va.y + s_Cos.scroll;
        int c = (int)(lx / (cw + gap)), r = (int)(ly / (chh + gap)), k = r * 2 + c;
        if(lx >= 0 && ly >= 0 && c < 2 && lx - c * (cw + gap) <= cw && ly - r * (chh + gap) <= chh && k < n)
        {
            RpClick(RPS_CLICK);
            if(!s_Cos.mode) { s_Cos.sel = list[k]; s_Cos.focus = CJF_FULL; }
            else
            {
                s_Cos.cj[part] = list[k];
                if((part == 0 || part == 2) && list[k] >= 0) s_Cos.cj[17] = -2;   // a top / pants: the full outfit off
                s_Cos.cjDirty = true; s_Cos.cjApply = true;
                s_Cos.focus = g_CjCatFocus[s_Cos.ccat];   // close-up on what you just put on
                snprintf(s_Cos.tried, sizeof(s_Cos.tried), "%s", list[k] >= 0 ? g_CjClothes[list[k]].name : (g_CjCatOff[s_Cos.ccat] ? "None" : "Default"));
            }
        }
    }
    dl->PushClipRect(va, vb, true);
    for(int k = 0; k < n; ++k)
    {
        ImVec2 a(rx + (k % 2) * (cw + gap), y0 + (k / 2) * (chh + gap) - s_Cos.scroll), b(a.x + cw, a.y + chh);
        if(b.y < va.y || a.y > vb.y) continue;
        const int i = list[k];
        bool on; const char* name; char sub[40];
        if(!s_Cos.mode) { on = s_Cos.sel == i; name = g_RpCos[i].name; snprintf(sub, sizeof(sub), "skin %d", g_RpCos[i].skin); }
        else
        {
            int cur = s_Cos.cj[part];
            on = cur == i;
            if(i >= 0) { name = g_CjClothes[i].name; snprintf(sub, sizeof(sub), "%s", g_CjClothes[i].tex); }
            else { name = g_CjCatOff[s_Cos.ccat] ? "None" : "Default"; snprintf(sub, sizeof(sub), "%s", g_CjCatOff[s_Cos.ccat] ? "take it off" : "CJ's own"); }
        }
        dl->AddRectFilled(a, b, on ? IM_COL32(56, 189, 248, 110) : IM_COL32(40, 40, 44, 102));
        dl->AddRectFilled(ImVec2(a.x, b.y - UI::S(4)), b, on ? sky : orange);
        RpPlainText(dl, UI::S(17), ImVec2(a.x + UI::S(10), a.y + UI::S(10)), IM_COL32_WHITE, name);
        RpPlainText(dl, UI::S(13), ImVec2(a.x + UI::S(10), a.y + UI::S(36)), IM_COL32(190, 196, 204, 255), sub);
    }
    dl->PopClipRect();
    if(maxS > 0)
    {   // scroll bar
        float h = viewH * viewH / contentH, y = va.y + (viewH - h) * (s_Cos.scroll / maxS);
        dl->AddRectFilled(ImVec2(vb.x + UI::S(4), y), ImVec2(vb.x + UI::S(8), y + h), IM_COL32(255, 255, 255, 110), UI::S(2));
    }
    // 3.37: close-up look: corner brackets round him, the body part + the piece, FULL VIEW button
    if(s_Cos.focus != CJF_FULL)
    {
        const float cx = ds.x * 0.5f, cyy = ds.y * 0.47f, hw = UI::S(170), hh = UI::S(190), L = UI::S(34), t = UI::S(3);
        const ImU32 bc = IM_COL32(255, 255, 255, 170);
        const ImVec2 c4[4] = { ImVec2(cx - hw, cyy - hh), ImVec2(cx + hw, cyy - hh), ImVec2(cx - hw, cyy + hh), ImVec2(cx + hw, cyy + hh) };
        for(int q = 0; q < 4; ++q)
        {
            float sx = (q & 1) ? -1.0f : 1.0f, sy = (q & 2) ? -1.0f : 1.0f;
            dl->AddLine(c4[q], ImVec2(c4[q].x + sx * L, c4[q].y), bc, t);
            dl->AddLine(c4[q], ImVec2(c4[q].x, c4[q].y + sy * L), bc, t);
        }
        char lab[96];
        if(s_Cos.tried[0] && s_Cos.mode) snprintf(lab, sizeof(lab), "%s  -  %s", g_CjFocusName[s_Cos.focus], s_Cos.tried);
        else snprintf(lab, sizeof(lab), "%s", g_CjFocusName[s_Cos.focus]);
        BcwCenterText(dl, UI::S(18), cyy + hh + UI::S(14), IM_COL32(255, 255, 255, 230), lab);
        const float fw = UI::S(150), fh = UI::S(40);
        ImVec2 fa(cx - fw * 0.5f, UI::S(64)), fb(fa.x + fw, fa.y + fh);
        ImGui::SetCursorScreenPos(fa);
        if(ImGui::InvisibleButton("##cosFull", ImVec2(fw, fh))) { s_Cos.focus = CJF_FULL; RpClick(RPS_CLICK); }
        dl->AddRectFilled(fa, fb, glass, UI::S(6)); dl->AddRect(fa, fb, IM_COL32(255, 255, 255, 70), UI::S(6));
        RpCenterIn(dl, UI::S(16), fa, fb, IM_COL32_WHITE, "FULL VIEW");
    }
    // bottom: SAVE / CANCEL
    const float bw = UI::S(260), bh = UI::S(64), by = ds.y - bh - UI::S(36);
    ImGui::SetCursorScreenPos(ImVec2(ds.x * 0.5f - bw - UI::S(8), by));
    bool save = ImGui::InvisibleButton("##cosS", ImVec2(bw, bh));
    ImGui::SetCursorScreenPos(ImVec2(ds.x * 0.5f + UI::S(8), by));
    bool cancel = ImGui::InvisibleButton("##cosC", ImVec2(bw, bh));
    {
        ImVec2 a(ds.x * 0.5f - bw - UI::S(8), by), b(a.x + bw, by + bh);
        dl->AddRectFilled(a, b, sky); RpCenterIn(dl, UI::S(22), a, b, IM_COL32(8, 16, 26, 255), "SAVE");
        ImVec2 c(ds.x * 0.5f + UI::S(8), by), d(c.x + bw, by + bh);
        dl->AddRectFilled(c, d, glass); dl->AddRect(c, d, IM_COL32(255, 255, 255, 60)); RpCenterIn(dl, UI::S(22), c, d, IM_COL32_WHITE, "CANCEL");
    }
    BcwEndOverlay();
    (void)nowMs;
    if(save) RpCosClose(true);
    else if(cancel) RpCosClose(false);
}
