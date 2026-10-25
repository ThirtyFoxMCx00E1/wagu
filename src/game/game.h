// Thin, typed wrappers over game script opcodes + a few exported game functions.
#pragma once
#include <stdint.h>

struct Vec3 { float x, y, z; };
struct PktClothes;   // net/protocol.h (3.10)
// 2.33: crash fix (CPhysical::Add, libGTASA + 0x3FCF6C): an entity MP put outside the world grid
// (+-3000 m: 120 sectors) or at NaN made the game index past its sector arrays. Every position /
// speed MP gives the game goes through these first.
static inline bool MP_Finite(float f) { return f == f && f < 1e7f && f > -1e7f; }
static inline bool MP_WorldPos(Vec3& v)   // false = garbage (NaN / inf): don't use it at all
{
    if(!MP_Finite(v.x) || !MP_Finite(v.y) || !MP_Finite(v.z)) return false;
    const float L = 2990.0f;
    if(v.x > L) v.x = L; else if(v.x < -L) v.x = -L;
    if(v.y > L) v.y = L; else if(v.y < -L) v.y = -L;
    if(v.z > 2500.0f) v.z = 2500.0f; else if(v.z < -150.0f) v.z = -150.0f;
    return true;
}

namespace Game
{
    bool  Init(void* gameHandle);

    // Local player
    int   LocalPed();                       // script handle, 0 if not ready
    bool  IsReady();

    // Peds (script handles)
    bool  GetCharPos(int ped, Vec3& out);
    void  SetCharPos(int ped, const Vec3& centre, float baseOffset);
    float GetCharHeading(int ped);
    void  SetCharHeading(int ped, float h);
    int   GetCharHealth(int ped);
    void  SetCharHealth(int ped, int hp);
    int   GetCharArmour(int ped);
    int   GetCharWeapon(int ped);
    float GetCharSpeed(int ped);
    bool  IsCharDead(int ped);
    bool  IsCharShooting(int ped);
    bool  IsCharDucking(int ped);
    bool  IsCharInAnyCar(int ped);
    int   GetCarCharIsIn(int ped);
    bool  HasCharBeenDamagedByChar(int ped, int byPed);
    void  ClearCharLastDamageEntity(int ped);
    int   CreateRemotePed(int model, const Vec3& pos);
    bool  IsSafePedModel(int model);   // ped model exists and may be requested (2.12)
    // 2.19.6 (skins.cpp): player model sync + story characters (special slots)
    void  InitSkins(void* gameHandle);
    void  InitFileGuard(void* gameHandle);   // 3.11.5: bad file handles in the game's streaming don't crash
    int   FileGuardBlocked();
    bool  IsSafeSkin(int skin);        // network skin id we can show (normal ped or a known story character)
    bool  IsSpecialSkin(int skin);
    const char* SkinLabel(int skin);   // story character name, nullptr for normal peds
    int   SpecialSkinCount();
    int   SpecialSkinAt(int i, const char** label);
    int   ModelForSkin(int skin, int& specialSlot);   // 0 = can't now
    void  NoteSpecialPed(int slot, int ped);
    int   LocalSkinForNet(int fallback);              // your real model (CJ -> fallback)
    // 3.10 (skins.cpp): clothing sync
    void  InitClothes(void* gameHandle);
    bool  ClothesSyncAvailable();
    bool  GetLocalClothes(PktClothes& out);    // false = you are not CJ
    int   ModelForClothes(const PktClothes& c, int& specialSlot);   // 0 = can't now
    bool  ClothesLoadPending();   // 3.21: an outfit model is still streaming in
    bool  ClothesCanBuild(const PktClothes& c);  // a slot has this outfit already / is free now
    int   CreateRemoteClothedCJ(const PktClothes& c, const Vec3& pos);   // 0 = can't (use CreateRemotePed)
    void  DeleteChar(int ped);
    void  GiveAndSelectWeapon(int ped, int weapon);
    void  TaskGoTo(int ped, const Vec3& p, int moveState);
    void  TaskStandStill(int ped);
    void  TaskShootAt(int ped, const Vec3& p, int timeMs);
    void  TaskAimAt(int ped, const Vec3& p, int timeMs);
    void  TaskDuck(int ped, bool duck);
    void  TaskDie(int ped);

    // Cars
    int   CreateCar(int model, const Vec3& pos);
    void  DeleteCar(int car);
    bool  GetCarPos(int car, Vec3& out);
    void  SetCarPos(int car, const Vec3& centre, float baseOffset);
    float GetCarHeading(int car);
    void  SetCarHeading(int car, float h);
    int   GetCarModel(int car);
    void  WarpIntoCar(int ped, int car);
    void  WarpOutOfCar(int ped, const Vec3& p);

    // Blips (radar icons)
    int   AddBlipForChar(int ped, uint32_t rgba);
    void  RemoveBlip(int blip);
    void  SetBlipColour(int blip, uint32_t rgba);

    // World
    void  Teleport(int ped, const Vec3& footPos, float heading);
    bool  LoadModel(int model);
    bool  IsKnownModel(int model);            // 2.24: exists in this game (safe to REQUEST_MODEL)
    bool  IsKnownCarModel(int model);         // 2.24: 400-611 and exists
    void  GetTimeOfDay(int& hour, int& minute);
    void  SetTimeOfDay(int hour, int minute);
    int   GetWeather();                     // current weather id (CWeather::OldWeatherType), 1 if unknown
    void  ForceWeatherNow(int weather);
    void  ReleaseWeather();

    // Server world lock (called every frame right after the game's own update, so scripts, CLEO
    // and cheats can't change the time or weather while you are on a server).
    // absMinute = server clock in game minutes, secFrac = 0..1 of the current minute,
    // forcedWeather = -1 for the game's normal weather cycle, else one weather id everywhere.
    // Returns WORLD_FIX_* bits for what had been changed by something else.
    enum { WORLD_FIX_TIME = 1, WORLD_FIX_WEATHER = 2, WORLD_FIX_SPEED = 4 };
    int   EnforceWorld(uint32_t absMinute, float secFrac, int forcedWeather);
    void  ReleaseWorld();               // give time + weather back to the game (after leaving)
    bool  CanLockWorld();               // clock/weather variables found in libGTASA

    // Screen
    bool  WorldToScreen(const Vec3& world, float& sx, float& sy, float& depth);
    int   ScreenWidth();
    int   ScreenHeight();
    bool  IsPaused();

    // Story state of the local player (2.9): on a mission, the mission's script name (e.g. "SWEET1",
    // up to 7 chars, empty if unknown) and whether a cutscene / scripted scene is playing.
    void  GetMissionState(bool& onMission, bool& cutscene, char missionName[8]);
    bool  IsRealCutscene();   // 2.19: a real (CCutsceneMgr) cutscene, not just "player control off"

    // Save slots (2.10). slot 0..9 = GTASAsf1.b .. GTASAsf10.b
    bool  SaveSlotPath(int slot, char* out, int outSize);   // full path of the slot's file
    bool  RequestLoadSlot(int slot);   // same as picking the slot in Load Game (gMobileMenu)
    bool  IsLoadPending();             // the game hasn't picked the request up yet
    void  CancelLoad();
    float ProgressPercent();           // stats "Progress made" / "Total progress" * 100, -1 if unknown

    // ---- 2.13 player state + environment (netstate.cpp) ------------------------------------
    bool  InitNetState(void* gameHandle);
    bool  AnimSyncAvailable();
    void* PedPtr(int ped);                 // CPed* from a script handle (null if gone)
    void* CarPtr(int car);
    int   GetPedMoveState(int ped);        // eMoveState: 1 still, 4 walk, 5 jog, 6 run, 7 sprint
    int   GetPedState(int ped);
    bool  GetCarRotation(int car, float fwd[3], float up[3]);
    void  SetCarRotation(int car, const float fwd[3], const float up[3]);
    void  NudgeEntity(int handle, bool isCar, const Vec3& delta);       // move by delta, keep physics
    void  SetMoveSpeed(int handle, bool isCar, const Vec3& metresPerSec);

    #define MP_MIRROR_MAX 8
    struct PedAnim { uint32_t hash; int16_t group, id; float time, speed, blend; uint16_t flags; char block[16]; };
    struct AppliedAnims { int n = 0; int16_t group[4], id[4]; void* hier[4]; };
    // full = the whole blend tree incl. walk/run/idle (2.15); otherwise only non-walking anims
    int   GetPedAnims(int ped, PedAnim* out, int maxOut, bool full = false);   // strongest first
    void  ApplyPedAnims(int ped, const PedAnim* in, int n, AppliedAnims& state, bool moving = false);   // 2.13 senders
    // 2.15 senders. moveSpeed (2.17) = how fast the avatar really moves (m/s): any movement gets
    // real walking legs, and the pose always adds up to 100 % (no T-pose from missing anims)
    // 2.19: stick = sender's joystick deflection 0..1 (-1 unknown), gait = sender's move state
    void  ReleaseMirroredAnims(int ped);   // 2.19.5: fade the copied full-body anims out (game walk task takes over)
    void  MirrorPedAnims(int ped, const PedAnim* in, int n, AppliedAnims& state, float dt, float moveSpeed = 0.0f, float stick = -1.0f, int gait = 0);

    // 2.15 passengers
    int   GetDriverOfCar(int car);
    int   GetMaxPassengers(int car);
    int   GetPassengerSeat(int car, int ped);          // -1 = not a passenger
    bool  IsPassengerSeatFree(int car, int seat);
    int   FreePassengerSeat(int car, int prefer);      // -1 = full
    void  WarpIntoCarAsPassenger(int ped, int car, int seat);
    void  TaskEnterCarAsPassenger(int ped, int car, int seat);
    void  TaskEnterCarAsDriver(int ped, int car, int ms);   // 2.35: walk to the door, open, get in (anims)
    void  TaskLeaveCar(int ped, int car);                   // 2.35: open, get out (anims)
    bool  IsCharGettingInToACar(int ped);
    int   GetCarCharIsUsing(int ped);
    bool  IsCharInCar(int ped, int car);
    void  ReleaseCar(int car);                         // mission car -> normal car (stays in the world)
    void  ClearTasksNow(int ped);

    typedef void (*ExplosionCb)(int type, const Vec3& pos);   // made by the local player
    typedef void (*FireCb)(const Vec3& pos, float size);      // started by the local player
    typedef void (*ForeignCb)(const Vec3& pos, bool explosion);   // made by someone else on this phone
    void  SetEnvCallbacks(ExplosionCb onExp, FireCb onFire, ForeignCb onForeign);
    void  SetLocalEntities(int ped, int car);  // call every frame (hooks compare against these)
    void  AddExplosionNet(int type, const Vec3& pos);
    int   StartFireNet(const Vec3& pos, int size);
    void  RemoveFireNet(int fire);
    int   CreateWeaponPickup(int weapon, int ammo, const Vec3& pos);
    bool  PickupCollected(int pickup);
    void  RemovePickup(int pickup);
    int   GetAmmo(int ped, int weapon);
    void  SetDropsWeapons(int ped, bool on);

    // ---- 2.17 ---------------------------------------------------------------------------------
    void  InitNetState217(void* gameHandle);
    #define MP_VEH_MOD_SLOTS_G 17
    struct VehState
    {
        bool     valid; bool isAuto;
        uint8_t  col[4]; int8_t paintjob; uint8_t lock; uint8_t flags;   // VEHF_*
        uint8_t  doors[6], wheels[4]; uint32_t panels, lights; uint16_t health;
        uint16_t mods[MP_VEH_MOD_SLOTS_G]; uint8_t doorOpen[6];
    };
    bool  ReadVehState(int car, VehState& s);
    void  ApplyVehState(int car, const VehState& want, VehState& applied);   // only what changed
    struct VehFast { float steer, gas, brake; uint16_t misc; float doomV, doomH; bool horn; };
    bool  ReadVehFast(int car, VehFast& f);
    void  ApplyVehFast(int car, const VehFast& f);                          // every frame
    int   GetAreaVisible();
    int   GetCharArea(int ped);
    void  SetCharArea(int ped, int area);
    void  SetCarArea(int car, int area);
    bool  IsPedAimingGun(int ped);
    bool  GetAimPoint(Vec3& out);
    // kind 0 bullet (a = start, b = impact), 1 ped projectile, 2 vehicle projectile (a = pos, b = dir)
    typedef void (*ShotCb)(int kind, int weapon, const Vec3& a, const Vec3& b, float force);
    void  SetShotCallback(ShotCb cb);
    bool  ShotSyncAvailable();
    void  ReplayBullet(int ped, int weapon, const Vec3& from, const Vec3& to);
    void  ReplayProjectile(int ped, int car, int weapon, const Vec3& pos, const Vec3& dir, float force);
    #define MP_MAX_MAP_MARKERS 100
    struct MapMarker { float x, y, z; uint32_t rgba; char name[24]; };
    typedef int (*MapMarkerProvider)(MapMarker* out, int maxOut);
    void  SetMapMarkerProvider(MapMarkerProvider p);   // pause-menu map: player icons + names
    // 3.00: top-right score bar drawn by the game itself (CFont, FONT_PRICEDOWN = the HUD money font),
    // segments left -> right, each in its own colour (0xRRGGBBAA)
    struct HudSeg { char text[40]; uint32_t rgba; };
    typedef int (*HudTextProvider)(HudSeg* out, int maxOut);
    void  SetHudTextProvider(HudTextProvider p);
    bool  HudTextActive();   // the game drew it in the last 300 ms (else MP draws it with ImGui)
    struct MissionBlip { float x, y, z; uint32_t colour; uint8_t display, kind; };
    int   ReadMissionBlips(MissionBlip* out, int maxOut, const int* skipBlips, int nSkip);
    int   AddCoordBlip(const Vec3& p, uint32_t colour, int display);

    // ---- 3.01: SA-MP features (game301.cpp) --------------------------------------------------
    // gang zones on the radar + pause-menu map (game draws them: CRadar::DrawAreaOnRadar)
    #define MP_MAX_RADAR_ZONES 64
    struct RadarZone { float minX, minY, maxX, maxY; uint32_t rgba; };
    typedef int (*ZoneProvider)(RadarZone* out, int maxOut);
    void  SetZoneProvider(ZoneProvider p);
    int   AddSpriteBlip(const Vec3& p, int sprite);   // short-range radar icon (map icons), RemoveBlip() removes it
    bool  SetLocalModel(int model);                   // class selection: 0 = CJ (rebuilds his clothes)
    int   LocalModel();
    bool  PlayAnimLoop(int ped, const char* anim, const char* file);   // actors (idle animations), false = file not loaded yet
    void  EnterArea(int area);                        // interior / stadium area for me + my car (0 = outside)

    // ---- 2.18: NPC sync ----------------------------------------------------------------------
    void  InitNpcGame(void* gameHandle);

    // ---- 2.19 ---------------------------------------------------------------------------------
    void  InitGame219(void* gameHandle);
    bool  GetStick(int& x, int& y);                    // on-foot joystick, -128..128
    bool  LineOfSightClear(const Vec3& a, const Vec3& b);
    void  SetFixedCamera(const Vec3& pos, const Vec3& look);
    void  RestoreCamera();
    bool  GetCameraPos(Vec3& pos, Vec3& look);
    void  SetPlayerControl(bool on);
    void  ShowGameHud(bool on);                        // HUD + radar
    void  SetCharVisible(int ped, bool on);
    void  FreezeChar(int ped, bool on);
    void  FreezeCar(int car, bool on);
    void  SetCharGodMode(int ped, bool on);
    void  SetCurrentWeapon(int ped, int weapon);   // 3.21: select a weapon the ped already has (0 = fists)
    void  SetDriveByAllowed(bool on);             // 3.21: local player drive-by on / off
    void  SetCarGodMode(int car, bool on);
    bool  IsLocalOnJetpack();
    int   GiveJetpack(int ped);      // 2.19.1: jetpack object on an avatar's back (0 = not yet)
    void  RemoveJetpack(int obj);
    void  GiveLocalJetpack();        // 3.23: the real jetpack (player only)
    void  PreloadArea(const Vec3& p, bool blocking);
    bool  GetMoveSpeed(int handle, bool isCar, Vec3& metresPerSec);
    void  PlaceChar(int ped, const Vec3& p);
    void  SetCharProofs(int ped, bool bullet, bool fire, bool explosion, bool collision, bool melee);
    void  SetCarProofs(int car, bool bullet, bool fire, bool explosion, bool collision, bool melee);           // no scene load (z <= -100: on the ground)
    bool  NpcSyncAvailable();
    struct NpcPedInfo { int handle, model; Vec3 pos, vel; float heading; int move, health; bool dead, duck; int weapon; };
    struct NpcCarInfo { int handle, model, driverModel; Vec3 pos, vel; float fwd[3], up[3]; int health; uint8_t col[2]; bool dead, siren, lights, engine; };
    int   ListAmbientPeds(NpcPedInfo* out, int maxOut, const Vec3& c, float r, bool (*skip)(int) = nullptr);              // the game's own people, nearest first
    int   FindNearestCar(const Vec3& c, float r, int skipCar = 0);   // 2.19.1: any car, 0 = none
    int   ListAmbientCars(NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, bool (*skip)(int) = nullptr); // the game's own traffic (2.32: + skip)
    // 2.28: script-made (CLEO / MP spawn) people and cars, for the shared world; skip(handle) = never list it
    int   ListSpawnedPeds(NpcPedInfo* out, int maxOut, const Vec3& c, float r, bool (*skip)(int));
    bool  HasLocalTwin(bool car, int model, const Vec3& at, float r, bool (*skip)(int));   // 2.35
    int   ListSpawnedCars(NpcCarInfo* out, int maxOut, const Vec3& c, float r, int skipCar, bool includeTraffic, bool (*skip)(int));
    void  SetAmbientPopulation(bool on);      // off: this phone makes no people / traffic (restores the old density)
    bool  IsAmbientPopulationOff();
    void  ClearAmbient(const Vec3& c, float r, int keepCar, bool first = false);
    bool  IsKeptLocalCar(int car);   // 2.34: a car this phone spawned / summoned (never cleared as traffic)
    bool  NpcDamagedByMe(int handle, bool car, int me, int myCar);
    void  MakePuppet(int ped);                // 2.21: empty decision maker (no own reactions)
    void  KeepPuppet(int ped);                // 2.32: a player avatar stays a script ped (never released to ambient AI / wander)
    void  KeepMissionCar(int car);            // 2.32: an avatar's car stays a script car (never becomes traffic)
    bool  GetCharTagPos(int ped, Vec3& o);    // 2.21: name tag spot (seat-aware in cars)
    // 2.21 world.cpp
    void  UnlockWorld(bool girlfriends);      // all cities, shop / gym / food / barber / tattoo doors, garages
    float GroundZ(float x, float y, float zHint);
    float GroundBelow(float x, float y, float fromZ);              // 3.21.4: -1000 = none
    bool  OpenGround(float x, float y, float zHint, float& out);   // 3.21.4: ground with open sky
    void  TeleportGround(int ped, Vec3 at, float heading);   // on the ground (car too if driving)
    int   CreateRaceCheckpoint(int type, const Vec3& p, const Vec3& next, float radius);
    void  DeleteRaceCheckpoint(int h);
    void  AddMoney(int amount);
    int   GetMoney();                         // 3.23
    void  AddArmour(int ped, int amount);
    void  RemoveAllWeapons(int ped);
    bool  ModelReady(int model);              // requests it if not loaded yet (never blocks)
    int   CreateNpcPed(int model, const Vec3& pos, float heading);
    int   CreateNpcCar(int model, int driverModel, const Vec3& pos, float heading, int* driverOut);
    int   GetCarHealth(int car);
    void  SetCarHealth(int car, int hp);
    void  ExplodeCar(int car);
    bool  IsCarDead(int car);
    void  SetCarColours(int car, int c1, int c2);
    bool  GetCarColours(int car, uint8_t out[4]);   // 2.19.1: the colours the car really has
    int   GetWantedLevel();
    // 3.04 (game304.cpp): Brief Case War
    int   CreateWorldObject(int model, const Vec3& p);   // 0 = model still loading (call again)
    void  DeleteWorldObject(int obj);
    bool  WorldObjectExists(int obj);
    void  MoveWorldObject(int obj, const Vec3& p, float spinDeg);
    int   CreateMapObject(int model, const Vec3& p, float rx, float ry, float rz);   // 3.23: solid map object (0 = loading)
    void  AttachObjectToChar(int obj, int ped, const Vec3& off);
    void  ClearWanted();
    void  SetMaxWanted(int stars);
    void  ReleaseNpc(int handle, bool car);
    void  DamageNpc(int handle, bool car, int dmg);
}
