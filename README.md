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


## MP 3.00: update the 24/7 servers

Rebuild `mp_server` on every region (console menu **7) update**). New in 3.00:

- **Cheat Menu permission.** A custom 24/7 server can let players use Cheat Menu (libCheatMenu.so / libCheatMenu64.so): add `--cheat-menu` to its `mp_server` command line. Without it (the default) those players get "This server doesn't allow cheat menus". The default server ignores the flag: Cheat Menu is never allowed there.
- Servers before 3.00 never send this permission, so players with Cheat Menu can't stay on them.
- Hit sound and the coloured Pricedown score bar are client-side (the phone's mod), nothing to change on the server.

## MP 2.37: update the 24/7 default server

2.37 moves some rules into the server, so rebuild `mp_server` on every region (console menu **7) update**, it pulls the wagu source and runs `install.sh` again):

- The default server's administrator is **SENJI** (proof: `admin.txt` with the admin key on his phone). He shows as `[ADMIN] SENJI` and can kick / ban / mute and switch cheats off. Joining first no longer makes anyone the host.
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
| RAM | 2 GB | **4 GB for the JVM** (`-Xms2g -Xmx4g`) + 2 GB for the OS = 6 GB+ |
| Disk | 10 GB SSD | SSD, 900+ IOPS |
| OS | Ubuntu 22.04 / 24.04 or Debian 12, x86_64 or arm64 | |
| Network | public IPv4, open ports below | |

Java heap is automatic: `-Xms2g -Xmx4g` with 6 GB RAM or more, about 60 % of RAM below that (giving Java all 4 GB of a 4 GB server leaves nothing for Linux and gets HiveMQ killed). Override with `JVM_XMS=2g` / `JVM_XMX=4g` in `/etc/mp-hivemq/mp.env`.

Pick each VPS in a data center close to its players, e.g. Singapore or Tokyo for Asia, Frankfurt for Europe, Virginia or Ohio for NA, Sydney for Oceania.

## Install

### 1. First region (with the bridge)

```bash
git clone https://github.com/ThirtyFoxMCx00E1/HiveMQ
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
| 1922/udp | the region's default game server |

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
| [Oracle Cloud Always Free](https://docs.oracle.com/iaas/Content/FreeTier/freetier_topic-Always_Free_Resources.htm) | Ampere arm64, 2 OCPU / 12 GB in total | Only in your home region (one region per account), card needed for verification. Good for one region with `-Xms2g -Xmx4g`. |
| Google Cloud / AWS free tiers | 1-2 small vCPUs, 1 GB RAM | Too small for 4 GB heap; fine for testing with automatic heap. |
| Paid VPS | 4 vCPU / 8 GB from about 10-25 USD a month | Hetzner, DigitalOcean, Vultr, Linode all have Asia/EU/US/Sydney locations. |

## Files

```
install.sh                   one-command setup (idempotent)
config/config.xml.template   HiveMQ listeners, limits, in-memory persistence
config/credentials.xml.template  roles mp / web / bridge / superuser and their topic rules
config/rbac-config.xml       File RBAC extension settings (hashed passwords)
config/99-mp-network.conf    sysctl: BBR, fq, bigger socket buffers, low-latency TCP
systemd/*.service            hivemq, mp-server, mp-bridge, mp-status, mp-console
cloud-init/user-data.yaml    paste-in VM setup from a phone (no computer, no SSH)
bridge/MpBridge.java         server-list bridge between regions (Java 21, no dependencies)
tools/                       status.sh, make-regions.sh, hash-password.sh, mp-console.sh (menu), mp-status.py (beacon + autopilot)
```

Why Community Edition: the HiveMQ Enterprise download without a license is an evaluation that allows only 25 connections, too few for a public lobby. CE has no such limit and covers everything the mod uses (MQTT 3.1.1, TLS, WebSocket, retained messages, last will).

## Tagalog

Walang computer? Gumawa ng VM sa browser ng phone, i-paste ang `cloud-init/user-data.yaml` (palitan ang REGION at DuckDNS), hintayin ang 10 minuto, tapos buksan ang Termius page ng website.


Isang VPS bawat rehiyon (Asia, Europe, NA, Oceania). Sa una: `sudo ./install.sh --region Asia --bridge`. Kopyahin ang `/etc/mp-hivemq/mp.env` sa ibang VPS at patakbuhin ang `install.sh` gamit ang rehiyon nila (walang `--bridge`). Ilagay ang address ng bawat server sa `HOST_*` sa `mp.env`, patakbuhin ang `tools/make-regions.sh`, at ilagay ang resulta sa `docs/regions.txt` ng Sa-mp repo. Ang mod na mismo ang pipili ng rehiyon na may pinakamababang ping. Inirerekomenda: 4 vCPU, 4 GB para sa JVM (`-Xms2g -Xmx4g`, kaya 6 GB+ RAM sa buong server), SSD na 900+ IOPS.
