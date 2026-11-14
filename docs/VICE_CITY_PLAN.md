# MP for GTA Vice City (Android): plan

Status in 4.13.37: phases 2 and 3 done in code (needs a device test). Every San Andreas sync that does not depend on SA-only content now runs in Vice City too. The same libMP*.so runs in Vice City with its own game layer (`src/vc/`). SA / VC servers never mix.

## Why one binary cannot just "work" in both games

- San Andreas Android: `libGTASA.so`, package `com.rockstargames.gtasa`. Vice City Android: `libGTAVC.so`, package `com.rockstargames.gtavc`. Different engine generation (VC = the GTA III engine, SA = a newer one).
- Every class has a different layout, so every SA hook / offset in MP would crash in VC. Correction (4.13.37): the local `GTAviceCity` clone is **re3 (GTA III)**, not reVC (its CPed is 0x53C and it has no VC models), so the class sizes this document listed before were GTA III sizes and are removed. The VC layer reads no class layout from it: offsets below were read from the disassembly of the real libGTAVC.so on both ABIs.
- Different map (Vice City, 1986), different ped / car / weapon IDs, different main.scm (1155 script commands in reVC `ScriptCommands.h`, opcode numbers shared with III / VC only).
- So the VC side is a second "game layer" in MP: same network, chat, voice, server browser, MonetLoader idea; new game code.

## What is done (4.13.35)

| Part | How |
|---|---|
| Game id in the join packet | `PktJoin6.game` (`MP_GAME_SA = 0`, `MP_GAME_VC = 1`). Older phones send 0 = SA. |
| Server refuses the other game | `REJECT_WRONG_GAME = 10`, message "This server is for the other game". |
| VC server | `mp_server --game vc` (puts the tag `vc` first in its list tags). |
| Server lists | an SA phone hides `vc` servers, a VC phone will show only `vc` servers. |
| libMP in Vice City | `libGTAVC.so` found, `libGTASA.so` missing: MP logs "GTA Vice City found" and stays off. |

## What is done (4.13.36)

- `src/vc/vc_layer.cpp`: VC game layer, every game function found by symbol name (no fixed addresses), so it works on both ABIs of the AML VC builds. All symbols used were checked in the real libGTAVC.so (gta_vc.apk, armeabi-v7a and arm64-v8a).
- Own script runner (`Cmd()`): a small CRunningScript with its own script bytes calls ProcessOneCommand. Used for cars, health, proofs, time, weather, teleport.
- Remote players (peds, name tags, colours), remote cars, own health / weapon / car / death sent, chat button + phone keyboard, dialogs, admin actions, damage, time / weather.
- Safe zones at the 4 hospitals: Ocean View (-108.3, -974.4, 9.4), Shady Palms (493.5, 703.1, 11.1), Schuman (-822.7, 1157.9, 10.1), West Haven (-885.2, -470.4, 12.1).
- `vc.ini` in `files/TYFMC/MP/` (Host, Port, Name).

### CRunningScript in Android libGTAVC.so (read from the disassembly)

| Field | 32-bit | 64-bit | Note |
|---|---|---|---|
| m_nIp | 0x10 | 0x18 | uint32 offset into CTheScripts::ScriptSpace, not a pointer |
| locals[16] | 0x30 | 0x38 | |
| m_bCondResult | 0x79 | 0x81 | |
| m_nAndOrState | 0x80 | 0x88 | |
| m_bNotFlag | 0x82 | 0x8A | |

Float params (type 6) are 4 bytes like int32. VCMP-Android copies the SA layout here, which is wrong for VC. `CFont::SetColor` / `SetDropColor` take CRGBA by value.

## What is done (4.13.37): every SA sync in VC

| Sync | San Andreas (src/mp.cpp) | Vice City (src/vc/vc_layer.cpp) |
|---|---|---|
| Avatars | AddPed + anims | CREATE_CHAR (009A) in the player's skin (CEntity::m_modelIndex), health held at 1000, the game's own walk / run / sprint objectives (0211 / 0239 / 0502) toward the predicted position, teleport when 8 m off |
| Weapon in hand | GIVE_WEAPON + SET_CURRENT | REQUEST_MODEL (CWeaponInfo model) + GIVE_WEAPON_TO_CHAR (01B2) + SET_CURRENT_CHAR_WEAPON (01B9) |
| Bullet sync | PKT_SHOT kind 0 | hook `CWeapon::DoBulletImpact`, replay with `CBulletTraces::AddTrace` + muzzle light (`CPointLights::AddLight`), the avatar faces the shot |
| Projectiles | PKT_SHOT kind 1 / 2 | hook `CProjectileInfo::AddProjectile` (ped or car), replayed from the avatar |
| Explosions | PKT_EXPLOSION | hook `CExplosion::AddExplosion` (culprit = me), ADD_EXPLOSION (020C) on the others, skipped where the avatar's replayed projectile already blew up and in safe zones |
| Fires | PKT_FIRE | hook both `CFireManager::StartFire`, START_SCRIPT_FIRE (02CF) for 12 s, max 16 |
| Damage | shooter-authoritative | HAS_CHAR_BEEN_DAMAGED_BY_CHAR (051A) on the avatar -> PKT_DAMAGE; armour first on the victim; blasts counted on the victim's own phone |
| Death / kills | PKT_DEATH | killer = last hit within 10 s, kill count on the scoreboard, avatar dies once and comes back |
| Crouch | SYNC_DUCKING | IS_CHAR_DUCKING (0597) / SET_CHAR_CROUCH (04EB) |
| Car rotation | SyncExt carFwd / carUp | the full matrix written on the remote car |
| Car colours + health | PKT_VEH_STATE | GET_CAR_COLOURS (03F3) / GET_CAR_HEALTH -> CHANGE_CAR_COLOUR / SET_CAR_HEALTH (min 300) |
| Passengers | SyncExt hasVeh | GET_DRIVER_OF_CAR (046C), CREATE_CHAR_AS_PASSENGER (01C8) into the owner's car |
| Carjack | PKT_JACK | avatar cars unlocked; taking one sends PKT_JACK, the victim is put beside the car (not in a safe zone) |
| Interiors | SyncExt interior | `CGame::currArea`; avatars only in the same area |
| Weapon pickups | PKT_PICKUP_* | my weapon drops on death, CREATE_PICKUP_WITH_AMMO (032B) type 3, HAS_PICKUP_BEEN_COLLECTED -> PKT_PICKUP_TAKE |
| Radar | entity blips | `CRadar::SetEntityBlip` on the avatar in the player's colour, coordinate blip when far |
| Name tags | tags + bars | name, health and armour bars (`CSprite2d::DrawRect`), (paused) tag from PKT_PAUSE |
| Scoreboard | PKT_PLAYER_INFO | ping + kills every 3 s, [ Players ] button |
| MP menu | ImGui | [ MP ] button: players, server host:port, name, reconnect, map markers, name tags, disconnect (saved in vc.ini) |
| Textdraws | PKT_TEXTDRAW | up to 64, 640 x 448 canvas scaled, box + alignment |

Server: the anti-cheat and the pickup check know the VC ids on a `--game vc` server (weapons 0-36, cars 130-236, no SA fire-rate / range tables).

### Offsets read from libGTAVC.so (both ABIs)

| Field | 32-bit | 64-bit |
|---|---|---|
| CEntity::m_modelIndex (int16) | 0x60 | 0x70 |
| CWeaponInfo size / m_nModelId | 0x64 / 0x60 | 0x64 / 0x60 |
| CWeapon::m_eWeaponType | 0x00 | 0x00 |
| CMatrix right / forward / up / pos (after the vtable) | 0x00 / 0x10 / 0x20 / 0x30 | same |

All 55 script commands used were checked against the Sanny Builder Vice City library (number, inputs, outputs, int / float types).

## Not ported (and why)

- Voice chat: the SA voice code hooks SA's audio; VC needs its own audio path.
- NPC traffic sync, SA clothes, SA gamemodes (BCW, MW3, turfs, RP), co-op missions: SA content / SA scripts.
- Gunshot sounds for remote shots (tracers and the light are there; the sound needs VC's CWeapon fire audio).
- Remote players aim / fire animation: the avatar faces the shot, but VC has no script command to make a char play the fire anim at a point.

## Sources used

- `ThirtyFoxMCx00E1/GTAviceCity`: the local clone is re3 (GTA III); used only for engine ideas shared by III and VC, never for VC layouts, CWorld, CPools, CStreaming, CTheScripts (128 scripts, 120 mission scripts), CCutsceneMgr, CGarages, CPickups, CRadar, CHud. This is the "header file" for the VC layer.
- gta-vice-city-wasm (`ThirtyFoxMCx00E1/gta-vice-city-wasm`): reVC running in a browser, with touch controls, saves (`GTAVCsf1.b` - `GTAVCsf8.b`) and skin import. Useful for testing logic on PC / browser and for the touch-control layout.
- vc-android-sdk: AML template for libGTAVC.so; mods folder `/storage/emulated/0/data/com.rockstargames.gtavc/mods` (AML: `Android/data/com.rockstargames.gtavc/mods`). Symbols: `FindPlayerPed`, `CHud::SetHelpMessage` `_ZN4CHud14SetHelpMessageEPtbbb`, `CHud::SetBigMessage` `_ZN4CHud13SetBigMessageEPtt`.
- VCMP-Android (fet1sov): an open-source start of VC:MP on Android, both ABIs. Addresses for the 1.09-era libGTAVC.so it targets (32-bit / 64-bit): game state 0x469D24 / 0x991E84 (== 9 = in game), script thread runner 0x144B14 / 0x19D1A4, NvUtilInit 0x3AC59C / 0x3ACE04, touch 0x3ACBA8 / 0x45D4E8, player model change 0x1B9B1C / 0x211730. They must be re-checked on the APK the players use (AML builds of VC 1.09 / 1.12).
- PC VC:MP 0.4 (vc-mp.org): what players expect: on-foot / vehicle / passenger sync, weapons and damage, animations, scoreboard, chat box, nametags, health bars, server browser, up to 100 players, custom objects / skins / vehicles, server-side anti-cheat.

## Script opcodes for the VC layer (from reVC ScriptCommands.h)

| Command | Opcode |
|---|---|
| TERMINATE_THIS_SCRIPT | 0x004E |
| START_NEW_SCRIPT | 0x004F |
| GET_PLAYER_COORDINATES | 0x0054 |
| CREATE_CHAR | 0x009A |
| SET_CHAR_COORDINATES | 0x00A1 |
| CREATE_CAR | 0x00A5 |
| SET_CAR_COORDINATES | 0x00AB |
| CREATE_OBJECT | 0x0107 |
| GET_CHAR_HEADING | 0x0172 |
| GIVE_WEAPON_TO_PLAYER | 0x01B1 |
| SET_PLAYER_CONTROL | 0x01B4 |
| LOAD_CUTSCENE | 0x02E4 |
| START_CUTSCENE | 0x02E7 |
| REGISTER_MISSION_PASSED | 0x0318 |
| LOAD_AND_LAUNCH_MISSION | 0x0416 |
| FAIL_CURRENT_MISSION | 0x045C |

## Missions and cutscenes

GTA Wiki counts 86 missions: 38 story, 19 asset, 6 racing, 5 phone, 3 stadium, 4 off-road, 3 RC, 1 Sparrow, 7 side ([GTA Wiki](https://gta.wiki/w/Missions_in_GTA_Vice_City)).

Story by giver: Intro (In The Beginning..., An Old Friend), Ken Rosenberg (The Party, Back Alley Brawl, Jury Fury, Riot), Avery Carrington (Four Iron, Demolition Man, Two Bit Hit), Juan Cortez (Treacherous Swine, Mall Shootout, Guardian Angels, Sir Yes Sir!, All Hands On Deck!), Ricardo Diaz (The Chase, Phnom Penh '86, The Fastest Boat, Supply & Demand), Kent Paul (Death Row), Lance Vance (Rub Out), Vercetti Estate (Shakedown, Bar Brawl, Cop Land), Umberto Robina (4), Auntie Poulet (3), Love Fist (3), Mitch Baker (3), Print Works (Spilling the Beans, Hit the Courier, Cap the Collector), final (Keep Your Friends Close...). Assets: Boatyard, Cherry Popper, Pole Position, Kaufman Cabs, Sunshine Autos, InterGlobal Studios, Print Works, Malibu ([GTAbase](https://www.gtabase.com/gta-vice-city/guides/gta-vice-city-100-completion-guide-checklist-definitive-edition)).

How they work in the engine: each mission is a block of main.scm started by LOAD_AND_LAUNCH_MISSION (0x0416). Cutscenes are LOAD_CUTSCENE / START_CUTSCENE (CCutsceneMgr, anims in `anim/cuts.img`). Co-op works the same way as MP's SA co-op: the host runs the mission script, the server relays "mission N started / passed / failed" and the cutscene name, the other phones play the same cutscene locally and see the host's mission peds / cars as synced entities.

## Map SA features to VC

| MP feature (SA) | VC version |
|---|---|
| Player / car sync | same packets; the VC layer reads only the matrix + m_modelIndex and uses script commands for the rest (4.13.37 table above). |
| Chat, voice, server browser, accounts | shared code, unchanged (UI is ImGui, game independent). |
| Neon / Multiplayer main menu | VC has its own FrontEnd; first the [Multiplayer] ImGui tile (like mp mode 4.13.20), native menu later. |
| Featured servers (BCW, MW3, RP, Day Z, Rocket League) | SA maps; VC gets its own: Freeroam Vice City, Deathmatch (Downtown / Starfish), Cops and Robbers, Vice Point races, Roleplay. |
| Missions co-op, cutscene sync | as above (0x0416, 0x02E4 / 0x02E7). |
| MonetLoader / Lua | MonetLoader is SA-only; a VC build needs its own CLEO / script bridge. Later phase. |
| SA-MP bridge (.amx gamemodes) | SA only. VC servers use MP's own server scripting. |

## Phases

1. Done: separation (game id, refused joins, `vc` tag, safe stop in VC).
2. Started in 4.13.36 (see above). VC core: `src/vc/` with a VC game layer (addresses by symbol name first, offsets from reVC), local player read, remote ped spawn, chat + server browser ImGui in VC, `mp_server --game vc` freeroam. Needs a device test on the exact VC APK (1.09 AML / 1.12).
3. Done in 4.13.37 (code, untested on a device): VC vehicles, weapons, bullets, damage, map blips, nametags, scoreboard, MP menu.
4. VC co-op missions and cutscene sync, VC featured servers.
5. VC main menu (native), VC Lua bridge.
