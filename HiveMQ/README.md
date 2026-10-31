# HiveMQ regional brokers for MP Multiplayer

Self-hosted lobby servers for [MP Multiplayer](https://github.com/ThirtyFoxMCx00E1/wagu) (GTA SA Android), one per region: **Asia, Europe, NA, Oceania**. Each server runs, always on:

- **HiveMQ Community Edition** (free, no connection limit) with logins and topic rules (File RBAC extension)
- the region's **24/7 default server** (`mp_server`, "MP LAN Server", UDP 1922)
- optionally **MpBridge**, which joins the server lists of all regions (run it on one server only)

The mod (2.20+) downloads [regions.txt](https://thirtyfoxmcx00e1.github.io/wagu/regions.txt) from the website, pings every region in the background and uses the fastest one. Players in Manila land on Asia, players in Berlin on Europe, and both still see every server in the list. Joining a server from another region moves that phone to that region's broker for the game, then back.

```
 phones (Asia)      phones (Europe)     phones (NA)       phones (Oceania)
      |                   |                  |                   |
 [HiveMQ Asia] <---> [HiveMQ Europe] <---> [HiveMQ NA] <---> [HiveMQ Oceania]
  + mp_server          + mp_server          + mp_server        + mp_server
        \______________ MpBridge: server lists only ______________/
                 website (GitHub Pages) reads the list over wss://
```

Game traffic stays inside one region (no bridging), so ping is only phone -> nearest broker.



## 3.38: 24/7 bot host on the 4 servers (no more "lost connection" when a player leaves)

"Lost connection to the server" when the first player leaves = that player's **phone was the host**. It
happens when phones can't see the 24/7 copy (the VPS is off, its IP changed, DuckDNS points to a dead
address, or a phone's network blocks port 1883 and it fell back to the free public lobby). The phones
then host the server themselves, and when that phone leaves everybody on it is moved to a new host.

The 4 featured servers are hosted 24/7 by the bot **Broker** (`mp-server`, `mp-bcw`, `mp-mw3`, `mp-rp`,
always in the player list as id 99). 3.38 makes sure every phone finds them:

| Part | What it does |
|---|---|
| Public mirror (`PUBLIC_MIRROR=1`, default) | `mp-bridge` now runs on **every** VPS. It lists the region's 4 24/7 servers in the free public lobby (`broker.hivemq.com`) and carries their game frames both ways. Only those servers' upload topics are read there; nothing from the public lobby is copied in. Phones already on a phone copy move to the 24/7 one by themselves within 10 s. |
| `server/run_24_7.sh [region]` | The 4 bot servers on any always-on machine (home PC, old Android phone with Termux) without HiveMQ. Each restarts within 2 s. `./run_24_7.sh stop` stops them. |

Check that the bot hosts are online (the `!d` = 24/7):

```bash
mosquitto_sub -h broker.hivemq.com -t 'thirtyfox/mp5/l/+' -v -W 6 | grep '!d'
journalctl -u mp-bridge -f        # "public lobby: listing 24/7 server ..."
```

Settings in `/etc/mp-hivemq/mp.env`: `PUBLIC_MIRROR=0` turns the mirror off, `PUBLIC_MIRROR_BROKERS="mqtt://broker.hivemq.com:1883 mqtt://test.mosquitto.org:1883"` lists on more public brokers.

## 3.37 VPS update: fast, steady ping with 100+ players

The kit now lives in the **wagu repo** (`HiveMQ/` folder) - wagu is the only repo for the mod, the servers and the
website ([thirtyfoxmcx00e1.github.io/wagu](https://thirtyfoxmcx00e1.github.io/wagu/)). `vps-setup.sh` and `update.sh`
download only this folder (sparse clone), so a new VPS is set up fast.

| What | Where | Effect |
|---|---|---|
| XDP flood filter | `xdp/mp_xdp.c`, `tools/mp-xdp.sh`, `mp-xdp.service` | runs on the network card before Linux: drops empty / oversized / type-0 packets and any IP over `XDP_PPS` (20000/s, high on purpose for carrier CGNAT) on UDP 1922 1930 1931 1932. Other regions' VPSes are never limited. Native XDP when the NIC supports it, generic XDP otherwise; if it can't load, the server just runs without it |
| conntrack bypass | `tools/mp-net-tune.sh` | game UDP skips connection tracking (less CPU per packet, a full table can't drop game packets) |
| NIC / CPU tuning | `tools/mp-net-tune.sh`, `mp-net-tune.service` | interrupt coalescing off, bigger RX ring, RPS over all CPUs, `performance` governor, THP defrag `defer` (no compaction stalls) |
| kernel | `config/99-mp-network.conf` | bigger softirq budget, UDP minimum buffers, conntrack 262144, swappiness 10, small dirty-page bursts, faster dead-peer cleanup |
| real-time packets | `src/net/server.cpp` (Linux VPS build only) | game socket marked DSCP EF + priority 6 |
| no restart on join | `tools/mp-keepalive.sh` | a running server is restarted only after `KEEPALIVE_STRIKES` (3) failed checks in a row, never on one slow answer while 100 phones join |
| no restart on update | `install.sh` | HiveMQ and each game server restart only when their files really changed (logins, TLS keystore and config are compared) - players stay connected. `MP_FORCE_RESTART=1` restarts all |
| light broker | `config/config.xml.template` | sessions freed 2 min after a phone leaves (was 5), message expiry 10 min (was 60), queue 64 per client (newest state, not seconds of old positions), keep-alive 60 s |
| cache / log trim | `tools/mp-trim.sh`, `mp-trim.timer` | every 6 h: journald 200 MB / 7 days, old HiveMQ logs, big server logs, apt cache, old /tmp. The Linux page cache is NOT dropped (that cache keeps the servers fast) |
| priorities | `systemd/*.service` | game servers and the broker get the CPU first and are the last to be killed on low RAM |

Settings (in `/etc/mp-hivemq/mp.env`): `XDP=1`, `XDP_PPS=20000`, `XDP_ALLOW=`, `NOTRACK=1`, `KEEPALIVE_STRIKES=3`.

```
sudo /opt/mp/console/mp-xdp.sh status     # XDP on/off + passed / dropped counters
sudo /opt/mp/console/mp-xdp.sh allow IP   # never limit this IP
sudo /opt/mp/console/mp-net-tune.sh off   # remove the conntrack bypass (XDP=0 + update.sh turns XDP off)
journalctl -t mp-keepalive -t mp-xdp -t mp-net-tune -t mp-trim
```

Ping still depends on distance: a phone in Manila to a Frankfurt VPS is ~180 ms whatever the tuning. The tuning removes
the spikes and the extra delay the server adds; for low ping everywhere, run one VPS per region (Asia, Europe, NA, Oceania).

> **Easiest way (3.32):** open the [MP Oracle page](https://thirtyfoxmcx00e1.github.io/wagu/oracle.html), paste a GitHub token
> (fine-grained, this repo only, Secrets + Variables + Actions = Read and write), the Oracle API key config text and the
> private key file, pick the MP region and press **Save secrets + run claim**. The page encrypts everything in your browser
> before it goes to GitHub and shows the server IP when the claim finishes.

> **MP Cloud (your own Oracle-style console):** [cloud/](cloud/README.md) - one install command gives a free-tier
> website where friends sign up and make servers on that machine or in your Oracle account (capacity retry across all
> availability domains), with one-click MP servers, Coolify and Dokploy, plus a REST API.

## One command (3.28.2)

On a new VPS (Ubuntu 22.04 / 24.04 or Debian 12), as root - installs everything for a region (Asia on `mpsamp.duckdns.org` by default) with the bridge, uses the passwords from the public regions.txt (phones connect with no change) and starts the 24/7 servers, Mega Vil Futuristic included:

```bash
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | sudo bash
```

All four regions (one VPS each):

| Region | DuckDNS | On the VPS |
|---|---|---|
| Asia | mpsamp.duckdns.org | `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| sudo bash -s asia` |
| Europe | eusamp.duckdns.org | `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| sudo bash -s europe` |
| NA | nasamp.duckdns.org | `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| sudo bash -s na` |
| Oceania | ocsamp.duckdns.org | `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| sudo bash -s oceania` |

From Termux on a phone: `pkg install -y openssh && ssh root@YOUR_VPS_IP "curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | bash -s europe"`. Point the DuckDNS name at the VPS first and open the ports (see Cloud firewall).

## MP Oracle: free Oracle server by itself

[`oracle/mp_oracle.py`](oracle/mp_oracle.py) is our own Oracle Cloud Always Free claimer, built from
[oracle-freetier-instance-creation](https://github.com/ThirtyFoxMCx00E1/oracle-freetier-instance-creation),
[oracle-cloud-free-arm-instance](https://github.com/ThirtyFoxMCx00E1/oracle-cloud-free-arm-instance) and
[oci-claimer](https://github.com/ThirtyFoxMCx00E1/oci-claimer), plus the MP setup. It:

1. checks the Oracle login and shows the home region and the free A1 left
2. makes the network once (`mp-vcn`, gateway, public subnet) with every MP port open: TCP 22 80 1883 8000 8883 8884 7681, UDP 1922 1930 1931 1932
3. asks for the free Ampere A1 server (2 OCPU / 12 GB, Ubuntu 24.04 arm64, public IP) and keeps asking on "Out of host capacity" in every availability domain
4. the server installs itself on first boot: DuckDNS, iptables ports, then `vps-setup.sh <region>` (HiveMQ + Java 21 + Mega Vil 24/7)
5. points `mpsamp` / `eusamp` / `nasamp` / `ocsamp.duckdns.org` at the new IP and tells Discord

### From the phone (GitHub Actions, no computer)

1. Make the Oracle account at [oracle.com/cloud/free](https://www.oracle.com/cloud/free/). The home region is the only free region and can't be changed: Singapore (Asia), Frankfurt (Europe), Ashburn (NA) or Sydney (Oceania).
2. Oracle: Profile -> API keys -> Add API key -> download the private key -> Add. Copy the config it shows.
3. This repo: Settings -> Secrets and variables -> Actions -> New repository secret:
   `OCI_USER`, `OCI_TENANCY`, `OCI_FINGERPRINT`, `OCI_REGION` (from that config) and `OCI_KEY` (the whole .pem text).
   Optional: `DUCKDNS_TOKEN`, `DISCORD_WEBHOOK`, `SSH_PUBLIC_KEY`. Variables tab: `MP_REGION` = `asia` / `europe` / `na` / `oceania`.
4. Actions -> MP Oracle claim -> Run workflow. It tries for 50 minutes every hour until Oracle has room, then the server sets itself up in about 15 minutes. The run summary shows the IP.

### From a Linux box or Termux

```bash
pip install oci
python3 oracle/mp_oracle.py check                 # login + home region + free A1 left
python3 oracle/mp_oracle.py claim --region europe # keeps trying until it gets the server
python3 oracle/mp_oracle.py status                # the mp-* servers and their IPs
```

Login from `~/.oci/config` or the same `OCI_*` environment values. `--dry-run` shows what it would make.

Rules from Oracle: free servers only in the home region, 2 OCPU + 12 GB of A1 in total, one account per person.
So one account covers one MP region. For the other regions use the owner's own account (a team member there) or a paid VPS with `vps-setup.sh`.

## Debian 12 (own PC or VPS)

Debian 12 works the same as Ubuntu and is a much smaller download. The official installer is on [Debian's archive](https://cdimage.debian.org/cdimage/archive/12.15.0/amd64/iso-cd/):

- [debian-12.15.0-amd64-netinst.iso](https://cdimage.debian.org/cdimage/archive/12.15.0/amd64/iso-cd/debian-12.15.0-amd64-netinst.iso) (677 MB, x86_64)
- sha256 `cd4462c06aa8892e692c0c4b9c17802f38c8ab8690e85cbfb5ccaa5956e9af17` (same as Debian's own [SHA256SUMS](https://cdimage.debian.org/cdimage/archive/latest-oldstable/amd64/iso-cd/SHA256SUMS))

On a VPS you don't need the ISO: pick "Debian 12" in the provider's image list. For your own PC, write the ISO to a USB stick (Rufus or balenaEtcher) and boot from it. The netinst installer downloads the rest while it installs, so keep the PC online.

1. At "Software selection" tick only **SSH server** and **standard system utilities** (no desktop).
2. Set a root password.
3. Log in as root (or `su -`). A fresh Debian has no `sudo` and often no `curl`, so use `wget`:

| Region | DuckDNS | On the Debian machine, as root |
|---|---|---|
| Asia | mpsamp.duckdns.org | `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| bash -s asia` |
| Europe | eusamp.duckdns.org | `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| bash -s europe` |
| NA | nasamp.duckdns.org | `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| bash -s na` |
| Oceania | ocsamp.duckdns.org | `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh \| bash -s oceania` |

The script installs curl, git, Java 21 and HiveMQ by itself. Updates later: `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | bash` (as root).

Ping depends on where the machine is, not on the system: a Debian PC in the Philippines is still far from Europe, NA and Oceania. For those regions use a VPS in Frankfurt, Virginia / Ohio or Sydney.

## MP 3.21.7: MP Railway - the Discord bot 24/7 on our own server

Railway.app stopped taking new accounts, so the MP login bot ([discordpy-bot](https://github.com/ThirtyFoxMCx00E1/discordpy-bot): `/gen`, `/stats`, `/char`) now runs on **MP Railway**, our own small Railway that is part of this kit. Without a running bot Discord doesn't show `/gen` and nobody can log in on 3.21.6+ servers.

On **one** server (the Asia / bridge server), once:

```bash
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash   # new kit + mp-railway tool
sudo mp-railway setup                                                                            # asks DISCORD_TOKEN + AUTH_PRIVATE_KEY (hidden)
```

or in the Termius console: **7) update**, then **r) MP Railway > 1) first-time setup**. It prints `ONLINE - /gen, /stats, /char are in your Discord server` when the bot is logged in and the commands are synced.

What MP Railway does (like Railway, without the cloud):

- clones the bot repo to `/opt/mp/railway/discordpy-bot`, builds it (Python venv + `requirements.txt`, or `npm` for Node apps), starts the `Procfile` command as the `mp-railway@discordpy-bot` systemd service (own user `mprail`, restart on crash, start on boot)
- **auto deploy:** `mp-railway-watch.timer` checks GitHub every 2 minutes; a new commit on `main` is built and restarted by itself (`/opt/mp/railway/discordpy-bot/deploys.log`)
- secrets in `/etc/mp-railway/discordpy-bot.env` (mode 600, never in git), accounts in `/var/lib/mp-railway/discordpy-bot/accounts.db`
- the bot publishes a signed status every 30 s; the website's [Railway page](https://thirtyfoxmcx00e1.github.io/wagu/railway.html) shows it live (checked with the mod's public key, so it can't be faked)

```
sudo mp-railway status            online / crashing / offline, commit, servers with the commands
sudo mp-railway logs -f           live log
sudo mp-railway deploy            deploy now
sudo mp-railway restart | stop | start
sudo mp-railway env               variables (masked)     sudo mp-railway env set DISCORD_TOKEN   (asks, restarts)
sudo mp-railway add NAME REPO     host another Python / Node app the same way
```

Without a VPS it also runs on an Android phone in Termux: `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/railway/install.sh | bash` (Termux:Boot starts it after a reboot). Run the bot in **one** place only.


## MP 3.34: region branches, 24/7 bot host, lower ping with many players

This kit now also lives inside the main repo: [wagu/HiveMQ](https://github.com/ThirtyFoxMCx00E1/wagu/tree/main/HiveMQ) (same files, kept in sync), so the mod, the website and the servers are never separated.

- **Region branches (game branch):** set `branch = on` in `bridge.conf` on the bridge machine. Every phone stays on the broker of **its own region** (Asia, Europe, NA, Oceania) and the bridge carries the game frames VPS to VPS to the region that hosts the server (`s/` phone to host, `c/` host to phone, `b/` host to all). Each VPS is a branch connected to the main HiveMQ machine with the same MQTT broker setup; players only pay the ping to their nearest VPS plus the fast VPS-to-VPS hop. `install.sh --bridge` writes `branch = on` (default); set `GAME_BRANCH=on` in `/etc/mp-hivemq/mp.env` and `tools/make-regions.sh` adds `branch=1` to that region in `regions.txt`, so 3.34 phones know they can stay on their own region.
- **No ping climb when 25 / 100 players join:** `mp_server` keeps a per-player send budget (far players are sent less often when the server is full, near players and shots always go first), and the phone measures ping on its own channel so it is not stuck behind game packets.
- **The 4 servers stay online 24/7 with the bot (1/100):** MP LAN Server, Brief Case War, Modern Warfare 3 and Mega Vil Futuristic always show the bot as a player, even when nobody plays. `mp-keepalive` now also **asks every server like the lobby does** (UDP query) and restarts one that has its port open but does not answer (hung / frozen), so the list never shows it offline.
- **Console autobot:** the Termius / Termux pages run several bot typers at once (one per lane), each in its own endless loop, so the console never gets stuck on one long command.
- **Discord bot:** the in-game `/stats` and `/char` are `/statsdc` and `/chardc` in Discord.

## MP 3.33: the 4 servers run only here, 24/7 - no Broker bot, lower ping

Run the update on every region (Termius console **7) update**, or `curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash`). New in 3.33:

- **The 4 featured servers are hosted by this machine only:** MP LAN Server (UDP 1922), Brief Case War (1930), Modern Warfare 3 (1931), Mega Vil Futuristic (1932). The **Broker bot is gone** (the server shows 0 players when empty) and no phone hosts them any more (the 100-rank Broker standby is off). A player who joins first is just a player, never the host. While a server restarts, 3.33 phones wait and reconnect by themselves.
- **Always online:** the four units restart 2 s after a crash (`RestartSec=2`, no start limit) and the new **`mp-keepalive.timer`** checks every minute that the broker and the four servers are running and that their port is open, and starts / restarts them when not. Log: `journalctl -t mp-keepalive`.
- **Console (website Termius / Termux pages):** the new menu **s) the 4 servers** lets you see each server, read its live log, **start, stop or restart** it one by one (or all four). A server you **stop** stays stopped (hold file in `/etc/mp-hivemq/hold`, the keep-alive skips it) until you start / restart it. **5)** and **6)** restart them and clear the holds.
- **Ping goes high when someone joins - fixed:** the broker limited every client to 512 KB/s (`BANDWIDTH_PER_CLIENT=524288`); a relay host with a few players hit it and the packets queued up. New default **4 MB/s** (`4194304`), and `install.sh` raises an old 512 KB/s value in `mp.env` by itself. `mp_server` now answers a ping at once (it waited in the send queue behind the batch) and the relay batching no longer grows to 50-80 ms on our own brokers.
- **Ping stuck at 200 ms+:** that is the relay. Direct UDP (about 20-100 ms) needs **all four UDP ports open**: `1922/udp 1930/udp 1931/udp 1932/udp` in the cloud firewall (Oracle Security List, AWS Security Group...). `cloud-init/user-data.yaml` and `install.sh` (ufw + the closing message) now open / list all four (before, only 1922 was opened, so Brief Case War, MW3 and Mega Vil fell back to the slow relay). `tools/status.sh` shows which of them are listening.

## Auto update (3.23.5)

The autopilot (mp-status) checks GitHub every 10 minutes. When the mod or this kit has a new version it runs
`curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash` by itself
(unit `mp-autoupdate`, log: `journalctl -u mp-autoupdate`). `MP_AUTOUPDATE=0` in `/etc/mp-hivemq/mp.env` turns it off.
It also restarts mp-bcw, mp-mw3, mp-rp and mp-legacy when they stop. Servers older than this need the command run once by hand.

## MP 3.21: update the 24/7 servers (important)

Run this on every region (or Termius console **7) update**, which now runs the same script):

```bash
curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash
```

- **Why the old servers never updated:** `install.sh` copied the new `mp_server` over the running one. Linux refuses that ("Text file busy"), and `set -e` stopped the whole update, so the regions kept an old `mp_server`. That old server refuses SENJI's new admin key (`administrator.txt`), and phones kept becoming the host of the default server. Fixed: the binary is now copied to `mp_server.new` and renamed, and the kit in `/opt/mp/kit` is a real git clone (the old copy had no `.git`, so `git pull` in menu 7 failed too).
- **`update.sh`** downloads the newest kit, re-runs `install.sh` with your saved `/etc/mp-hivemq/mp.env` and restarts `mp-server`, `mp-bcw`, `mp-mw3`, `mp-rp` and `mp-legacy`.
- New in `mp_server` 3.21: hospital safe zones + 5 s spawn protection checked on the server (`--no-safezones` to turn off), race queue (14 players, or 45 s with 2+, or 60 s alone, 20 s GO), and phones on MP 3.21 move to the 24/7 copy by themselves, so a phone host leaving no longer "restarts" the server.
- **Ping (~400 ms):** that is the internet relay through the broker. Direct UDP (about the real distance ping) needs the server's UDP port reachable: open `1922/udp` (and `1930/udp` for Brief Case War, `1931/udp` for MW3, `1932/udp` for Mega Vil Futuristic) in the VPS firewall, or forward them on the router when the server runs at home. A VPN (Amnezia, OpenVPN) adds one more hop, it doesn't lower the ping; a VPS in each region does.

## MP 3.03: update the 24/7 servers

Rebuild `mp_server` on every region (console menu **7) update**). New in 3.03:

- **Gamemodes.** The default server always has every gamemode. A custom server has none until its owner lists them: `--gamemodes all`, or e.g. `--gamemodes class,turf,race,derby,dm,jobs,labels,actors`.
- **Mission sync** relays the mission's peds / cars (PKT_MISSION_NPC) to the mission group only.

## MP 3.02: update the 24/7 servers

Rebuild `mp_server` on every region (console menu **7) update**). New in 3.02:

- **Race queue.** Players type `/race` to wait for a race; when 14 are waiting, the server picks one of the 27 GTA SA races and starts it for those 14 (grid places, same GO). Phones on an older server get "This server can't run races yet".
- The phone's crash fix (sound) and the race scoreboard are client-side.

## MP 3.01: update the 24/7 servers

Rebuild `mp_server` on every region (console menu **7) update**). New in 3.01 (SA-MP features):

- **Turf wars run on the server.** 24 gang zones (20 in Los Santos, 4 in San Fierro). A gang member who stands in an enemy zone for 60 s takes it; everyone gets "TURF: X took Y from Z!". Owners are kept in `turf.txt` next to `mp_server` (`--turf-file path` to move it, `--no-turf` to switch turf wars off).
- **Welcome dialog + banner.** `--motd "text"` or `--motd-file motd.txt` shows a SA-MP style dialog on join (`{RRGGBB}` colours, `\n` = new line); `--banner "text"` shows a small textdraw bottom right (site / Discord). The default server has a built-in welcome.
- **Races / derbies with other players.** The server relays race and derby lobbies, positions and finishes, and the class team of every player.
- Servers before 3.01 still work with 3.01 phones, but without turf wars, live race positions or welcome dialogs.

## MP 3.00: update the 24/7 servers

Rebuild `mp_server` on every region (console menu **7) update**). New in 3.00:

- **Cheat Menu permission.** A custom 24/7 server can let players use Cheat Menu (libCheatMenu.so / libCheatMenu64.so): add `--cheat-menu` to its `mp_server` command line. Without it (the default) those players get "This server doesn't allow cheat menus". The default server ignores the flag: Cheat Menu is never allowed there.
- Servers before 3.00 never send this permission, so players with Cheat Menu can't stay on them.
- Hit sound and the coloured Pricedown score bar are client-side (the phone's mod), nothing to change on the server.

## MP 2.37: update the 24/7 default server

2.37 moves some rules into the server, so rebuild `mp_server` on every region (console menu **7) update**, it pulls the wagu source and runs `install.sh` again):

- The default server's administrator is **SENJI** (proof: `administrator.txt` with the admin key on his phone; since 3.11.6 only the new key is accepted, so keep `mp_server` updated). He shows as `[ADMIN] SENJI` and can kick / ban / mute and switch cheats off. Joining first no longer makes anyone the host.
- The name SENJI is refused for everybody else (`REJECT_NAME_RESERVED`), and fake `[ADMIN]` tags are removed.
- When everybody has left, the admin's "Disable cheats" switch is reset and the server stays online 24/7.
- Optional: use your own admin key hash with `--admin-hash <sha256 hex of "wagu-admin-v1:" + key>` in `systemd/mp-server.service`.

## Our regions

| Region | Domain | Notes |
|---|---|---|
| Asia | `mpsamp.duckdns.org` | runs MpBridge (`--bridge`) |
| Europe | `eusamp.duckdns.org` | |
| NA | `nasamp.duckdns.org` | |
| Oceania | `ocsamp.duckdns.org` | |

All four use the same `mp.env` (same `mp` / `web` / bridge / admin passwords); only `REGION` and `DOMAIN` differ. The public part is in the website's [regions.txt](https://thirtyfoxmcx00e1.github.io/wagu/regions.txt). Keep `mp.env` private.

## Server requirements

| | Minimum | Recommended |
|---|---|---|
| CPU | 2 vCPU (small tests) | **4 vCPU** (HiveMQ CE system requirement) |
| RAM | 2 GB | **10 GB for the JVM** (`-Xms5g -Xmx10g`, 3.25) + ~2 GB JVM overhead + 2 GB for the OS = 14 GB+ (16 GB machine ideal); 10-14 GB machines get the 8 GB heap; 24 GB machines get 12 GB, 32 GB machines 16 GB |
| Disk | 10 GB SSD | SSD, 900+ IOPS |
| OS | Ubuntu 22.04 / 24.04 or Debian 12, x86_64 or arm64 | |
| Network | public IPv4, open ports below | |

Java heap is automatic (3.25): `-Xms8g -Xmx16g` with 26 GB RAM or more (32 GB machine), `-Xms6g -Xmx12g` with 20 GB+ (24 GB machine), `-Xms5g -Xmx10g` with 14 GB+ (16 GB machine), `-Xms4g -Xmx8g` with 10 GB+, `-Xms2g -Xmx4g` with 6 GB+, about 60 % of RAM below that (giving Java all the RAM leaves nothing for Linux and gets HiveMQ killed). Override with `JVM_XMS=5g` / `JVM_XMX=10g` in `/etc/mp-hivemq/mp.env`. `sudo ./update.sh` picks the new heap up by itself. More heap = room for many more phones and no garbage-collection stalls under load; the ping itself depends on the internet line and how close the machine is to the players.

**HiveMQ CE from our fork (3.25):** `install.sh` / `update.sh` install HiveMQ CE built from [ThirtyFoxMCx00E1/hivemq-community-edition](https://github.com/ThirtyFoxMCx00E1/hivemq-community-edition) (Apache 2.0). Its `MP build` workflow builds `hivemq-ce-<version>.zip` on every change and publishes it as the release `mp-<version>` (with a `.sha256`, checked on install). If that release is missing, the official HiveMQ CE release is used. Force the official one with `HIVEMQ_SOURCE=official` in `/etc/mp-hivemq/mp.env`. The official 2026.5 zip is first taken from the prebuilt copy in the [wagu v3.25 release](https://github.com/ThirtyFoxMCx00E1/wagu/releases/tag/v3.25) (`hivemq-ce-2026.5.zip`, same file as HiveMQ's, sha256 `fd005e53...7dec5` checked), then from github.com/hivemq.

Pick each VPS in a data center close to its players, e.g. Singapore or Tokyo for Asia, Frankfurt for Europe, Virginia or Ohio for NA, Sydney for Oceania.

## Install

### 1. First region (with the bridge)

```bash
git clone https://github.com/ThirtyFoxMCx00E1/wagu/tree/main/HiveMQ
cd HiveMQ
sudo ./install.sh --region Asia --domain asia-you.duckdns.org --email you@mail.com --bridge
```

The first run creates `/etc/mp-hivemq/mp.env` with random passwords. `--domain` is optional but needed for TLS and for the website's live list (browsers only allow `wss://`). A free domain from [DuckDNS](https://www.duckdns.org) works: point it at the server's IP before running the script.

### 2. Other regions

Copy `mp.env` to every other server (same passwords everywhere), then run the script with that server's region. Its own domain and bridge setting are reset automatically when the region differs:

```bash
sudo mkdir -p /etc/mp-hivemq
sudo cp mp.env /etc/mp-hivemq/mp.env && sudo chmod 600 /etc/mp-hivemq/mp.env
sudo ./install.sh --region Europe --domain eu-you.duckdns.org --email you@mail.com
```

Use `--region NA` and `--region Oceania` on the others. Leave out `--bridge` on all of them.

### 3. Fill in the addresses

On every server edit `/etc/mp-hivemq/mp.env`:

```
HOST_ASIA=asia-you.duckdns.org
HOST_EUROPE=eu-you.duckdns.org
HOST_NA=na-you.duckdns.org
HOST_OCEANIA=oc-you.duckdns.org
```

Then run `sudo ./install.sh --region <its region>` again on the bridge server so MpBridge connects to all of them. Empty regions are simply skipped; you can start with one or two regions.

### 4. Publish regions.txt

```bash
sudo ./tools/make-regions.sh > regions.txt
```

Put the output in `docs/regions.txt` of the wagu repo and push. Within about 30 minutes (or on the next game start) every phone switches to the regional brokers. Until then everyone stays on the free public lobby, so nothing breaks while you set things up.

### 5. Cloud firewall

`install.sh` opens the ports in `ufw`. Most providers also have their own firewall (Oracle "Security List", AWS "Security Group", Google "VPC firewall") where you must allow the same ports:

| Port | Use |
|---|---|
| 22/tcp | SSH |
| 1883/tcp | MQTT, the mod |
| 8883/tcp | MQTT over TLS (with `--domain`) |
| 8884/tcp | secure WebSocket, the website (with `--domain`) |
| 8000/tcp | WebSocket without TLS |
| 80/tcp | Let's Encrypt renewals (with `--domain`) |
| 1922/udp | the region's default game server (MP LAN Server) |
| 1930/udp | Brief Case War |
| 1931/udp | Call Of Duty: Modern Warfare 3 |
| 1932/udp | Mega Vil Futuristic (a closed UDP port = that server runs on the slow relay, 200 ms+) |

## No computer: phone-only setup (2.21)

Everything below works from a phone browser, no SSH app and no computer:

1. Create a free domain at [duckdns.org](https://www.duckdns.org) (e.g. `asia-yourname`) and copy the token.
2. Create an Ubuntu 22.04/24.04 VM (e.g. Oracle Cloud Always Free, Ampere A1). In "Advanced options -> cloud-init / user data", paste [`cloud-init/user-data.yaml`](cloud-init/user-data.yaml) with your region, DuckDNS name, token and email filled in. In the cloud firewall (Oracle: VCN security list) open TCP 80, 1883, 8000, 8883, 8884, 7681 and UDP 1922.
3. Wait about 10 minutes. The VM installs Java 21, HiveMQ CE, the 24/7 server, the status beacon and the web console by itself.
4. Open the website's [Termius page](https://thirtyfoxmcx00e1.github.io/wagu/termius.html): the region turns green ("online 24/7") as soon as the status beacon reports. Tap "console" (https://DOMAIN:7681) and log in as `admin`; the password is `CONSOLE_PASS` in `/etc/mp-hivemq/mp.env` (read it once through the cloud provider's serial console or `sudo cat` in the web shell of the provider).
5. In the console menu choose 8 (regions.txt) and copy the block into `docs/regions.txt` of the wagu repo. Every copy of the mod then uses your broker automatically.

The web console is [ttyd](https://github.com/tsl0922/ttyd) behind TLS and a password: live logs, restart, update, status and a bash shell. Without a domain it only listens on localhost (set `CONSOLE_INSECURE=1` in `mp.env` to allow plain http, not recommended).

### Shared logins for 100 phones

Every phone uses the same `mp` login on purpose (it is inside `regions.txt`). That is safe and does not limit players: each phone connects with its own random client ID, the topic rules are bound to that ID, so 100+ phones on one login never read each other's messages or kick each other off. `max-connections` is 20000 per broker; the 24/7 server has 100 slots (`MAX_PLAYERS`).

### Why not HiveMQ Cloud free

HiveMQ retires the free HiveMQ Cloud Serverless plan on 2026-12-31 and no new Serverless clusters can be created ([HiveMQ docs](https://docs.hivemq.com/hivemq-cloud/serverless-end-of-life.html)); the free self-managed Enterprise "Lab" license allows only 25 connections. So this kit keeps HiveMQ Community Edition on your own VM (no connection limit). Until a region is in `regions.txt`, the mod and website fall back to the free public lobby brokers.

## Autopilot + MP Bot (2.22)

Nobody has to type in a terminal any more.

- **On every region server** `mp-status` (the status beacon) is also the autopilot. Every 5 s it checks `hivemq`, `mp-server`, `mp-bridge` and `mp-console`; an enabled service that stopped is restarted (max 3 times in 10 minutes, then it waits and reports). Disk above 90 % = journal vacuum + old HiveMQ logs removed. Every action is published on `thirtyfox/mp4/bot/<Region>`. Turn it off with `MP_AUTOPILOT=0` in `/etc/mp-hivemq/mp.env`.
- **On the website** the MP Bot runs the [Termius](https://thirtyfoxmcx00e1.github.io/wagu/termius.html) and [Termux](https://thirtyfoxmcx00e1.github.io/wagu/termux.html) terminals by itself: it types each command, presses Enter, and reacts to what it sees (region down -> `diagnose`, autopilot report -> `autopilot`, new servers -> `servers`, ping every 90 s). There is no input box.
- Run `install.sh` again (or `mp-server update` style re-run of cloud-init) on existing regions to get the new topic rule for `bot/+`.

## Daily use

```bash
sudo /opt/mp/console/mp-console.sh     # menu (same as the web console)
sudo ./tools/status.sh                 # services, ports, connections, memory, recent logs
sudo systemctl restart hivemq          # restart the broker
journalctl -u mp-server -f             # default server log
journalctl -u mp-bridge -f             # bridge log
sudo ./install.sh --region Asia        # update: re-run any time, keeps mp.env
sudo ./tools/hash-password.sh 'pw'     # hash a new password for credentials.xml
```

All services start on boot and restart after a crash.

## Security

- No anonymous logins; wrong passwords are refused. Passwords are stored as salted hashes.
- `mp` (the mod) may only use `thirtyfox/mp4/...` topics, may only read its own player channel and cannot send in another player's name (topics are tied to its client ID). Anything else disconnects the client.
- `web` (the website) can only read the server list and the region status.
- Web console: TLS + password, max 3 sessions; only on localhost without a domain.
- `bridge` and `admin` passwords are secret. They are never written to `regions.txt`; keep `mp.env` private (`chmod 600`).
- Limits: 64 KB per packet, bandwidth per client, max connections, idle sockets without CONNECT dropped after 5 s, in-memory queues drop oldest when full.
- HiveMQ's JMX port 9010 is off and blocked; services run as an unprivileged `hivemq` user.
- The `mp` login is inside `regions.txt` on purpose: every phone needs it. The topic rules above are what protect the broker.

## Free hosting options

| Option | What you get | Notes |
|---|---|---|
| [Oracle Cloud Always Free](https://docs.oracle.com/iaas/Content/FreeTier/freetier_topic-Always_Free_Resources.htm) | Ampere arm64, 2 OCPU / 12 GB in total | Only in your home region (one region per account), card needed for verification. Good for one region with `-Xms4g -Xmx8g` (12 GB; the 10 GB heap needs 14 GB+). |
| Google Cloud / AWS free tiers | 1-2 small vCPUs, 1 GB RAM | Too small for 4 GB heap; fine for testing with automatic heap. |
| Paid VPS | 4 vCPU / 8 GB from about 10-25 USD a month | Hetzner, DigitalOcean, Vultr, Linode all have Asia/EU/US/Sydney locations. |

## Files

```
install.sh                   one-command setup (idempotent)
config/config.xml.template   HiveMQ listeners, limits, in-memory persistence
config/credentials.xml.template  roles mp / web / bridge / superuser and their topic rules
config/rbac-config.xml       File RBAC extension settings (hashed passwords)
config/99-mp-network.conf    sysctl: BBR, fq, bigger socket buffers, low-latency TCP
systemd/*.service            hivemq, mp-server, mp-bcw, mp-mw3, mp-rp, mp-bridge, mp-status, mp-console, mp-keepalive (+ .timer)
cloud-init/user-data.yaml    paste-in VM setup from a phone (no computer, no SSH)
oracle/mp_oracle.py          MP Oracle: claims the Oracle Always Free A1 server and installs it (+ .github/workflows/oracle-claim.yml)
bridge/MpBridge.java         server-list bridge between regions (Java 21, no dependencies)
tools/                       status.sh, make-regions.sh, hash-password.sh, mp-console.sh (menu), mp-keepalive.sh (24/7 watchdog), mp-status.py (beacon + autopilot)
```

Why Community Edition: the HiveMQ Enterprise download without a license is an evaluation that allows only 25 connections, too few for a public lobby. CE has no such limit and covers everything the mod uses (MQTT 3.1.1, TLS, WebSocket, retained messages, last will).

## Tagalog

Walang Ubuntu? Puwede ang Debian 12 (677 MB lang): i-download ang ISO sa [Debian archive](https://cdimage.debian.org/cdimage/archive/12.15.0/amd64/iso-cd/), i-install (SSH server + standard system utilities lang), tapos bilang root: `wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | bash -s europe` (o `asia` / `na` / `oceania`).

MP Oracle: ilagay ang Oracle API key sa repo Secrets (OCI_USER, OCI_TENANCY, OCI_FINGERPRINT, OCI_REGION, OCI_KEY), tapos Actions -> MP Oracle claim -> Run. Paulit-ulit itong susubok hanggang may libreng server, tapos kusa nang mag-iinstall.

Walang computer? Gumawa ng VM sa browser ng phone, i-paste ang `cloud-init/user-data.yaml` (palitan ang REGION at DuckDNS), hintayin ang 10 minuto, tapos buksan ang Termius page ng website.


Isang VPS bawat rehiyon (Asia, Europe, NA, Oceania). Sa una: `sudo ./install.sh --region Asia --bridge`. Kopyahin ang `/etc/mp-hivemq/mp.env` sa ibang VPS at patakbuhin ang `install.sh` gamit ang rehiyon nila (walang `--bridge`). Ilagay ang address ng bawat server sa `HOST_*` sa `mp.env`, patakbuhin ang `tools/make-regions.sh`, at ilagay ang resulta sa `docs/regions.txt` ng Sa-mp repo. Ang mod na mismo ang pipili ng rehiyon na may pinakamababang ping. Inirerekomenda: 4 vCPU, 10 GB para sa JVM (`-Xms5g -Xmx10g`, kaya 14 GB+ RAM sa buong server; 8 GB heap sa 10-14 GB), SSD na 900+ IOPS.

## Brief Case War 24/7 (MP 3.04)

`install.sh` also installs **`mp-bcw.service`**: the region's 24/7 "Brief Case War" gamemode server on **UDP 1930** (20 players, 10 vs 10, cheats off, CheatMenu refused). It is pinned in the in-game Internet list with a red banner, even at 0/20.

- logs: `journalctl -u mp-bcw -f`  |  restart: `systemctl restart mp-bcw` (console option 5 restarts it too)
- a match starts at 20 queued players; for testing with fewer: `BCW_MIN=4 ./install.sh` (or edit `--bcw-min` in `/etc/systemd/system/mp-bcw.service`)

### Call Of Duty: Modern Warfare 3 (3.23)

`install.sh` also installs **`mp-mw3.service`**: the region's 24/7 featured "Call Of Duty: Modern Warfare 3" server on **UDP 1931** (64 players, 8 teams, 34 capture zones, 6 DM arenas, duels, events every 15 min, hourly tournament; login on, cheats off, MP 3.23+ only). It is pinned in the in-game Internet list with a green banner. Only the VPS runs it, phones never become host.

- logs: `journalctl -u mp-mw3 -f`  |  restart: `systemctl restart mp-mw3` (console option 5 restarts it too)
- stats + zone owners: `/opt/mp/server/mw3_stats.txt` (saved every minute and on stop)
- turn it off: `RUN_MW3=0 ./install.sh`

`install.sh` also installs **`mp-rp.service`** (3.25): the region's 24/7 featured **"Mega Vil Futuristic"** Roleplay server on **UDP 1932** (250 players, Roleplay RP only - no teams, no other gamemodes; money + bank, 20 jobs, shops, 48 houses, pets, the in-game phone, city events every 15 min, lottery, 100+ RP commands; peaceful - only on-duty police / military can hurt; login on, cheats off, MP 3.25+ only). It is pinned in the in-game Internet list with a sky blue banner. 3.33: only this VPS hosts it (no Broker bot, no phone standby) and `mp-keepalive` keeps it online 24/7.

- logs: `journalctl -u mp-rp -f`  |  restart: `systemctl restart mp-rp` (console option 5 restarts it too)
- citizens (money, bank, job, house, pets, phone number): `/opt/mp/server/rp_citizens.txt` (saved every minute and on stop)
- 250 players is heavy: give it its own core, and keep at least 1 GB RAM free for it
- turn it off: `RUN_RP=0 ./install.sh`
- don't want it: `RUN_BCW=0 ./install.sh`

## MP 3.11: lower ping + voice chat

Rebuild `mp_server` on every region (console menu **7) update**), then re-run `install.sh` (or copy `systemd/hivemq.service` + `config/99-mp-network.conf` and restart). New in 3.11:

- **Direct UDP (the big ping drop).** 3.11 phones ask the region's broker machine over UDP (`PKT_QUERY` to `<broker host>:1922` / `:1930`). If the 24/7 server answers, they play straight over UDP instead of through MQTT: one hop, no TCP stalls, no batching. **The UDP ports must be reachable from the internet:**
  - cloud server: open **UDP 1922, 1930, 1931 and 1932** in the provider's firewall / security group (ufw is already done by `install.sh`)
  - home server (e.g. `*.duckdns.org` on a home connection): on the router, **port-forward UDP 1922, 1930, 1931 and 1932** to the machine running `mp_server`, the same way TCP 1883/8883 are forwarded
  - check from outside: `printf '\x0e\x00\x00\x00\x00' | nc -u -w2 <host> 1922 | head -c 40` should print the server name
  If the ports are closed nothing breaks: phones keep using the relay (and its ping).
- **Voice chat relay.** The server forwards voice (`PKT_VOICE`, Opus frames) only to 3.11 phones within 40 m of the talker, max 60 frames/s per player, nothing for admin-muted players.
- **Admin key (SENJI).** The admin key given out since 3.10 is only known by `mp_server` 3.10 or newer. A server built earlier refuses SENJI ("reserved name") even with a valid admin.txt, so update every region (menu **7) update**) after a new admin key.
- **Discord message** every 45 min 20 s on every server (not on join, not while empty).
- **Broker latency.** HiveMQ now runs on **Generational ZGC** (JDK 21: GC pauses < 1 ms instead of up to 20 ms with G1). `99-mp-network.conf` adds `tcp_slow_start_after_idle=0` (no window reset after a quiet moment, which caused 200-500 ms spikes), `tcp_autocorking=0`, `tcp_notsent_lowat=4096`, fast loss recovery and bigger UDP buffers.

## MP 3.10

- 3.10 keeps the `thirtyfox/mp5/` lobby and lets **3.04.6 and 3.10 phones play together**. Nothing changes in the topic rules; only 3.04.5 and older stay shut out.
- Update the 24/7 servers (console option 7, or `git pull && sudo ./install.sh --region <Region>`): the rebuilt `mp_server` relays the new clothing sync, keeps the default server at 100 slots and accepts the new admin key.
- Java 21 for x86_64 now comes from the wagu release **Assets-1-2** (`OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz`). If that download fails, Eclipse Temurin 21 is used instead.

## Old MP versions are shut out (MP 3.04.6)

MP 3.04.5 and older let players use cheats. From 3.04.6 on:

- The lobby moved from `thirtyfox/mp4/` to `thirtyfox/mp5/`. The public `mp` login has full access on mp5, but on mp4 it can only **read** the server list and send a join. So old phones can't host anything on these brokers, even though their `broker.txt` / `regions.txt` still has the login.
- **`mp-legacy.service`** runs `mp_server 1931 --legacy-notice` in the old lobby as **UPDATE MP TO 3.04.6** (bridge login, `server/broker-legacy.txt`). It's the only server old phones can see. Joining it gets refused with the download link.
- Every server (`mp-server`, `mp-bcw`, `mp-mw3`, `mp-legacy`) refuses old versions and logs them to `/opt/mp/server/outdated_clients.log`: time, name, version range, phone id, IP, port. Console: `o`. Disable the notice with `RUN_LEGACY=0 ./install.sh`.
- MpBridge copies both lobbies between the regions.
- **Update every region** (console option 7, or `git pull && sudo ./install.sh --region <Region>`). Until then, 3.04.6 phones can't see that region's 24/7 servers.

