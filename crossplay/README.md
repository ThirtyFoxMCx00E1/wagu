# MP cross-play: PC (open.mp / SA-MP) + Mobile (MP for Android) - 4.12

`mp-crossplay.so` is an [open.mp](https://github.com/openmultiplayer/open.mp) server component. Put it in
the `components/` folder of an open.mp server and the same world gets a second door: the MP mod on
Android phones joins it next to the PC players of the normal
[open.mp launcher](https://github.com/openmultiplayer/launcher).

| Who | Joins with | Shows up on the other side as |
|---|---|---|
| PC player | open.mp launcher / SA-MP 0.3.7 client, port 7777 | a player named `Name (PC)` on the phones (position, facing, health, armour, skin, weapon, car) |
| Android player | MP mod: lobby / LAN list or `IP:7780` | an open.mp NPC `M_Name` on PC (same data, ~12 updates a second) |
| Chat | both | `[Mobile] Name: text` on PC, `Name (PC): text` on the phones |

The component runs the MP server of the mobile mod inside `omp-server` on its own UDP port and copies
every player across with the open.mp NPCs component
([open.mp-sdk](https://github.com/openmultiplayer/open.mp-sdk) `Server/Components/NPCs/npcs.hpp`).

## Install (Linux open.mp server)

1. open.mp server 1.6.7 (from [open.mp releases](https://github.com/openmultiplayer/open.mp/releases)),
   with the `NPCs` component in `components/` (it ships with the server).
2. Copy `mp-crossplay.so` (release zip `MP-4.12-crossplay-omp.zip`, or build it below) into `components/`.
3. Start `omp-server`. The first start writes `mp_crossplay.cfg`:

```ini
port=7780                 # UDP port for the phones (open it like 7777)
name=open.mp [cross-play] # name in the MP lobby
region=Asia               # Asia | Europe | NA | Oceania
online=1                  # 0 = LAN / direct IP only
max=100                   # Android players max (each one is an NPC; max_bots is raised to this)
```

4. Firewall: UDP 7777 (PC) and UDP 7780 (phones).

Console when it works:

```
[mp-crossplay] MP 4.12 mobile side on UDP 7780 ("open.mp [cross-play]", Asia - Singapore, internet lobby) - PC + Mobile cross-play is ON
[npc:join] M_AndroidJoe has joined the server
[mp-crossplay] AndroidJoe joined from Android (MP) as NPC M_AndroidJoe
```

## Limits (honest list)

- The gamemode sees the Android players as NPCs (`IsPlayerNPC` = 1): commands, dialogs and
  `GivePlayerMoney` / `SetPlayerPos` on them do not reach the phone. PC players get the full gamemode.
- Android players see PC players and their cars, but not the gamemode's objects, textdraws, pickups
  or checkpoints (the phone side is the MP world).
- Damage between the two sides is not bridged yet (hits show, health does not drop across).
- Linux open.mp servers only (32-bit `.so`). The Windows `.dll` needs a Windows port of the MP server code.
- Built against open.mp-sdk `08cc69a3`, the SDK commit of the open.mp 1.6.7.3218 server.

## Build

```bash
ZIG=/path/to/zig bash crossplay/build.sh linux     # zig 0.13+, fetches the SDK at the pinned commit
# -> crossplay/out/linux/mp-crossplay.so
```

Tests: `test/crossplay_test.cpp` (bridge: ghost PC player <-> real phone client, join, sync, chat,
timeout, leave). Checked in an open.mp 1.6.7.3218 Linux server: the component loads, opens UDP 7780,
a phone client joins and appears as the NPC `M_AndroidJoe`, its chat reaches the PC chat.

Repos used: [open.mp](https://github.com/openmultiplayer/open.mp),
[open.mp-sdk](https://github.com/openmultiplayer/open.mp-sdk/tree/08cc69a32d96c0eb8fe063d26c78259e54ca853f),
[open.mp-capi](https://github.com/openmultiplayer/open.mp-capi/tree/d952ccb75511fe50880d9bd1fe0648bb2a172d30),
[launcher](https://github.com/openmultiplayer/launcher), [web](https://github.com/openmultiplayer/web).
