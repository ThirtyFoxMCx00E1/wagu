// 3.39: /mv - My Vehicles (like the screenshot): a SA-MP tablist "My Vehicles (n/4):" with the columns
// # / Vehicle Model / Status / Distance, buttons Select / Close. Select -> Spawn / Despawn, Lock / Unlock,
// Locate (radar blip), Park here, Remove. /savecar (or /mv add) keeps the car you sit in (up to 4 per server).
// Kept on the phone (MP data folder, myvehicles.txt), one list per server.

#define MV_MAX 4
struct MvCar { int model = 0; Vec3 p = { 0, 0, 0 }; float h = 0; bool locked = false; int car = 0; int blip = 0; uint64_t blipUntil = 0; };
static MvCar s_Mv[MV_MAX];
static int s_nMv = 0;
static char s_szMvKey[80] = "";
static int s_nMvSel = -1;

static bool MvOn() { return client.GetState() == NET_CONNECTED && Game::IsReady() && !BcwOnServer() && !Mw3OnServer() && !RpOnServer(); }
static void MvKey(char* out, size_t n) { snprintf(out, n, "%s:%u", client.GetServerName()[0] ? client.GetServerName() : "server", (unsigned)client.GetPort()); }
static std::string MvPath() { return std::string(s_szDataDir) + "/myvehicles.txt"; }
static void MvSave()
{
    // keep the other servers' lines, rewrite this server's
    std::vector<std::string> keep;
    if(FILE* f = fopen(MvPath().c_str(), "r"))
    {
        char l[256]; const size_t kl = strlen(s_szMvKey);
        while(fgets(l, sizeof(l), f)) { if(strncmp(l, s_szMvKey, kl) || l[kl] != '|') keep.push_back(l); }
        fclose(f);
    }
    if(FILE* f = fopen(MvPath().c_str(), "w"))
    {
        for(auto& l : keep) fputs(l.c_str(), f);
        for(int i = 0; i < s_nMv; ++i) fprintf(f, "%s|%d|%.2f|%.2f|%.2f|%.1f|%d\n", s_szMvKey, s_Mv[i].model, s_Mv[i].p.x, s_Mv[i].p.y, s_Mv[i].p.z, s_Mv[i].h, s_Mv[i].locked ? 1 : 0);
        fclose(f);
    }
}
static void MvLoad()
{
    char key[80]; MvKey(key, sizeof(key));
    if(!strcmp(key, s_szMvKey)) return;
    for(int i = 0; i < s_nMv; ++i) { if(s_Mv[i].blip) Game::RemoveBlip(s_Mv[i].blip); }
    s_nMv = 0; MP_CopyStr(s_szMvKey, key, sizeof(s_szMvKey));
    FILE* f = fopen(MvPath().c_str(), "r"); if(!f) return;
    char l[256]; const size_t kl = strlen(key);
    while(fgets(l, sizeof(l), f) && s_nMv < MV_MAX)
    {
        if(strncmp(l, key, kl) || l[kl] != '|') continue;
        MvCar c; int lk = 0;
        if(sscanf(l + kl + 1, "%d|%f|%f|%f|%f|%d", &c.model, &c.p.x, &c.p.y, &c.p.z, &c.h, &lk) >= 5 && c.model >= 400 && c.model <= 611) { c.locked = lk != 0; s_Mv[s_nMv++] = c; }
    }
    fclose(f);
}
static bool MvSpawned(const MvCar& c) { return c.car > 0 && Game::CarExists(c.car); }
static float MvDist(const MvCar& c)
{
    Vec3 p = c.p; if(MvSpawned(c)) Game::GetCarPos(c.car, p);
    const float dx = p.x - myDrawPos.x, dy = p.y - myDrawPos.y, dz = p.z - myDrawPos.z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}
static void MvShowAction(int i);
static void MvListDlg(int response, int item, const char*, int)
{
    if(!response || item < 0 || item >= s_nMv) return;
    s_nMvSel = item; MvShowAction(item);
}
static void MvShow()
{
    MvLoad();
    if(!s_nMv) { SysMsg(COL_SYSTEM, "My Vehicles is empty: sit in a car and type /savecar (up to %d).", MV_MAX); return; }
    std::string body = "#\tVehicle Model\tStatus\tDistance\n";
    for(int i = 0; i < s_nMv; ++i)
    {
        const MvCar& c = s_Mv[i];
        const bool sp = MvSpawned(c);
        char row[200];
        snprintf(row, sizeof(row), "{FFFFFF}%c%d:\t{FFFFFF}%s\t%s\t{FFFF00}%.1f\n", sp ? 'S' : 'V', c.model - 400 + 100 * (i + 1),
                 kVehNames[c.model - 400], c.locked ? "{FF3333}Locked" : (sp ? "{33CC55}Spawned" : "{FF9900}Despawned"), MvDist(c));
        body += row;
    }
    char cap[48]; snprintf(cap, sizeof(cap), "{FFFFFF}My Vehicles (%d/%d):", s_nMv, MV_MAX);
    ShowDialog(0, DIALOG_STYLE_TABLIST_HEADERS, cap, body.c_str(), "Select", "Close", MvListDlg);
}
static void MvSpawn(MvCar& c)
{
    if(MvSpawned(c)) return;
    int car = Game::CreateCar(c.model, { c.p.x, c.p.y, c.p.z + 0.4f }, true);
    if(car <= 0) { SysMsg(COL_RED, "Could not spawn the %s here (try again near it).", kVehNames[c.model - 400]); return; }
    Game::SetCarHeading(car, c.h);
    Game::LockCar(car, c.locked);
    c.car = car;
    SysMsg(COL_GREEN, "Your %s is spawned (%.0f m away).", kVehNames[c.model - 400], MvDist(c));
}
static void MvDespawn(MvCar& c)
{
    if(!MvSpawned(c)) { c.car = 0; return; }
    if(Game::GetDriverOfCar(c.car) > 0 && Game::GetDriverOfCar(c.car) != Game::LocalPed()) { SysMsg(COL_RED, "Somebody is driving it."); return; }
    Vec3 p; if(Game::GetCarPos(c.car, p)) { c.p = p; c.h = Game::GetCarHeading(c.car); }
    if(Game::GetDriverOfCar(c.car) == Game::LocalPed()) { Vec3 out = c.p; out.x += 2.0f; out.z += 0.5f; Game::WarpOutOfCar(Game::LocalPed(), out); }
    Game::DeleteCar(c.car); c.car = 0; MvSave();
    SysMsg(COL_SYSTEM, "%s despawned (parked here for next time).", kVehNames[c.model - 400]);
}
static void MvActionDlg(int response, int item, const char*, int user)
{
    if(!response || user < 0 || user >= s_nMv) { if(!response) MvShow(); return; }
    MvCar& c = s_Mv[user];
    switch(item)
    {
    case 0: if(MvSpawned(c)) MvDespawn(c); else MvSpawn(c); break;
    case 1: c.locked = !c.locked; if(MvSpawned(c)) Game::LockCar(c.car, c.locked); MvSave(); SysMsg(COL_SYSTEM, "%s %s.", kVehNames[c.model - 400], c.locked ? "locked" : "unlocked"); break;
    case 2:
    {
        Vec3 p = c.p; if(MvSpawned(c)) Game::GetCarPos(c.car, p);
        if(c.blip) Game::RemoveBlip(c.blip);
        c.blip = Game::AddCoordBlip(p, 0xFFFF00FF, 2); c.blipUntil = MP_NowMs() + 60000;
        SysMsg(COL_YELLOW, "Your %s is marked on the radar (%.0f m).", kVehNames[c.model - 400], MvDist(c));
        break;
    }
    case 3:
    {
        if(!MvSpawned(c)) { SysMsg(COL_RED, "Spawn it first."); break; }
        Vec3 p; if(Game::GetCarPos(c.car, p)) { c.p = p; c.h = Game::GetCarHeading(c.car); MvSave(); SysMsg(COL_GREEN, "Parking spot saved."); }
        break;
    }
    case 4:
        if(MvSpawned(c) && Game::GetDriverOfCar(c.car) <= 0) Game::DeleteCar(c.car);
        if(c.blip) Game::RemoveBlip(c.blip);
        SysMsg(COL_SYSTEM, "%s removed from My Vehicles.", kVehNames[c.model - 400]);
        for(int i = user; i < s_nMv - 1; ++i) s_Mv[i] = s_Mv[i + 1];
        s_Mv[--s_nMv] = MvCar(); MvSave();
        break;
    }
}
static void MvShowAction(int i)
{
    const MvCar& c = s_Mv[i];
    char body[300];
    snprintf(body, sizeof(body), "%s\n%s\n{FFFF00}Locate\n{FFFFFF}Park here\n{FF3333}Remove", MvSpawned(c) ? "{FF9900}Despawn" : "{33CC55}Spawn", c.locked ? "{FFFFFF}Unlock" : "{FF3333}Lock");
    char cap[64]; snprintf(cap, sizeof(cap), "{FFFFFF}%s", kVehNames[c.model - 400]);
    ShowDialog(0, DIALOG_STYLE_LIST, cap, body, "Select", "Back", MvActionDlg, i);
}
static bool MvCommand(const char* cmd, const char* arg)
{
    const bool add = !strcmp(cmd, "savecar") || (!strcmp(cmd, "mv") && arg && !strncasecmp(arg, "add", 3));
    if(!add && strcmp(cmd, "mv") && strcmp(cmd, "myvehicles") && strcmp(cmd, "mycars")) return false;
    if(!MvOn()) return false;   // Mega Vil / BCW / MW3: their own commands
    MvLoad();
    if(add)
    {
        const int ped = Game::LocalPed();
        if(ped <= 0 || !Game::IsCharInAnyCar(ped)) { SysMsg(COL_RED, "Sit in the car you want to keep, then /savecar."); return true; }
        const int car = Game::GetCarCharIsIn(ped);
        for(int i = 0; i < s_nMv; ++i) if(s_Mv[i].car == car) { SysMsg(COL_SYSTEM, "It is already in My Vehicles."); return true; }
        if(s_nMv >= MV_MAX) { SysMsg(COL_RED, "My Vehicles is full (%d) - /mv > Remove one first.", MV_MAX); return true; }
        MvCar c; c.model = Game::GetCarModel(car); if(c.model < 400 || c.model > 611) return true;
        Game::GetCarPos(car, c.p); c.h = Game::GetCarHeading(car); c.car = car;
        s_Mv[s_nMv++] = c; MvSave();
        SysMsg(COL_GREEN, "%s saved to My Vehicles (%d/%d). /mv to manage.", kVehNames[c.model - 400], s_nMv, MV_MAX);
        return true;
    }
    MvShow();
    return true;
}
static void MvProcess(uint64_t now)
{
    for(int i = 0; i < s_nMv; ++i) if(s_Mv[i].blip && now > s_Mv[i].blipUntil) { Game::RemoveBlip(s_Mv[i].blip); s_Mv[i].blip = 0; }
    if(client.GetState() != NET_CONNECTED && s_szMvKey[0])
    {   // left the server: the cars go with me
        for(int i = 0; i < s_nMv; ++i) { if(s_Mv[i].blip) Game::RemoveBlip(s_Mv[i].blip); s_Mv[i].blip = 0; s_Mv[i].car = 0; }
        s_szMvKey[0] = 0; s_nMv = 0;
    }
}
