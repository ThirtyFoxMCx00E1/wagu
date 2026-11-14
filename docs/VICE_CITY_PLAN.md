# MP for GTA Vice City (Android): plan

Status in 4.13.35: Vice City and San Andreas are kept apart. Same libMP*.so, but in Vice City MP stays off (no crash), and SA / VC servers never mix.

## Why one binary cannot just "work" in both games

- San Andreas Android: `libGTASA.so`, package `com.rockstargames.gtasa`. Vice City Android: `libGTAVC.so`, package `com.rockstargames.gtavc`. Different engine generation (VC = the GTA III engine, SA = a newer one).
- Every class has a different layout. From the reVC source (GTAviceCity repo, `VALIDATE_SIZE`): CEntity 0x64, CPhysical 0x128, CPed 0x53C, CPlayerPed 0x5F0, CVehicle 0x288, CAutomobile 0x5A8, CPlayerInfo 0x13C. SA's are all different, so every SA hook / offset in MP would crash in VC.
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

## Sources used

- reVC (miami branch) = `ThirtyFoxMCx00E1/GTAviceCity`: full class layouts, CWorld, CPools, CStreaming, CTheScripts (128 scripts, 120 mission scripts), CCutsceneMgr, CGarages, CPickups, CRadar, CHud. This is the "header file" for the VC layer.
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
| Player / car sync | same packets; VC layer reads CPed (0x53C) / CVehicle (0x288) / CBike fields, and creates remote peds with CREATE_CHAR + SET_CHAR_COORDINATES. |
| Chat, voice, server browser, accounts | shared code, unchanged (UI is ImGui, game independent). |
| Neon / Multiplayer main menu | VC has its own FrontEnd; first the [Multiplayer] ImGui tile (like mp mode 4.13.20), native menu later. |
| Featured servers (BCW, MW3, RP, Day Z, Rocket League) | SA maps; VC gets its own: Freeroam Vice City, Deathmatch (Downtown / Starfish), Cops and Robbers, Vice Point races, Roleplay. |
| Missions co-op, cutscene sync | as above (0x0416, 0x02E4 / 0x02E7). |
| MonetLoader / Lua | MonetLoader is SA-only; a VC build needs its own CLEO / script bridge. Later phase. |
| SA-MP bridge (.amx gamemodes) | SA only. VC servers use MP's own server scripting. |

## Phases

1. Done: separation (game id, refused joins, `vc` tag, safe stop in VC).
2. VC core: `src/vc/` with a VC game layer (addresses by symbol name first, offsets from reVC), local player read, remote ped spawn, chat + server browser ImGui in VC, `mp_server --game vc` freeroam. Needs a device test on the exact VC APK (1.09 AML / 1.12).
3. VC vehicles, weapons, damage, map blips, nametags, scoreboard.
4. VC co-op missions and cutscene sync, VC featured servers.
5. VC main menu (native), VC Lua bridge.
