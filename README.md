# HiveMQ regional brokers for MP Multiplayer

Self-hosted lobby servers for [MP Multiplayer](https://github.com/ThirtyFoxMCx00E1/Sa-mp) (GTA SA Android), one per region: **Asia, Europe, NA, Oceania**. Each server runs, always on:

- **HiveMQ Community Edition** (free, no connection limit) with logins and topic rules (File RBAC extension)
- the region's **24/7 default server** (`mp_server`, "MP LAN Server", UDP 1922)
- optionally **MpBridge**, which joins the server lists of all regions (run it on one server only)

The mod (2.20+) downloads [regions.txt](https://thirtyfoxmcx00e1.github.io/Sa-mp/regions.txt) from the website, pings every region in the background and uses the fastest one. Players in Manila land on Asia, players in Berlin on Europe, and both still see every server in the list. Joining a server from another region moves that phone to that region's broker for the game, then back.

```
 phones (Asia)      phones (Europe)     phones (NA)       phones (Oceania)
      |                   |                  |                   |
 [HiveMQ Asia] <---> [HiveMQ Europe] <---> [HiveMQ NA] <---> [HiveMQ Oceania]
  + mp_server          + mp_server          + mp_server        + mp_server
        \______________ MpBridge: server lists only ______________/
                 website (GitHub Pages) reads the list over wss://
```

Game traffic stays inside one region (no bridging), so ping is only phone -> nearest broker.

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

Put the output in `docs/regions.txt` of the Sa-mp repo and push. Within about 30 minutes (or on the next game start) every phone switches to the regional brokers. Until then everyone stays on the free public lobby, so nothing breaks while you set things up.

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

## Daily use

```bash
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
- `web` (the website) can only read the server list.
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
systemd/*.service            hivemq, mp-server, mp-bridge
bridge/MpBridge.java         server-list bridge between regions (Java 21, no dependencies)
tools/                       status.sh, make-regions.sh, hash-password.sh
```

Why Community Edition: the HiveMQ Enterprise download without a license is an evaluation that allows only 25 connections, too few for a public lobby. CE has no such limit and covers everything the mod uses (MQTT 3.1.1, TLS, WebSocket, retained messages, last will).

## Tagalog

Isang VPS bawat rehiyon (Asia, Europe, NA, Oceania). Sa una: `sudo ./install.sh --region Asia --bridge`. Kopyahin ang `/etc/mp-hivemq/mp.env` sa ibang VPS at patakbuhin ang `install.sh` gamit ang rehiyon nila (walang `--bridge`). Ilagay ang address ng bawat server sa `HOST_*` sa `mp.env`, patakbuhin ang `tools/make-regions.sh`, at ilagay ang resulta sa `docs/regions.txt` ng Sa-mp repo. Ang mod na mismo ang pipili ng rehiyon na may pinakamababang ping. Inirerekomenda: 4 vCPU, 4 GB para sa JVM (`-Xms2g -Xmx4g`, kaya 6 GB+ RAM sa buong server), SSD na 900+ IOPS.
