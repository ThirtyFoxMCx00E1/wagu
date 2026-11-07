# MP regions: one VPS per city (4.12)

Up to 4.11 all four region names (`mpsamp`, `eusamp`, `nasamp`, `ocsamp.duckdns.org`) pointed at the
same home connection in Cagayan de Oro (Converge, PH). Players in the Philippines got a good ping,
everybody else connected across an ocean to one home line, whatever region they picked.

4.12 moves each region to its own VPS in its own city:

| Region in the mod | City | DuckDNS name | Vultr | Misaka | OVHcloud | Hetzner |
|---|---|---|---|---|---|---|
| Asia - Singapore | Singapore | mpsamp.duckdns.org | `sgp` | SIN03 | SGP1 | `sin` |
| Europe - Frankfurt | Frankfurt | eusamp.duckdns.org | `fra` | FRA13 / FRA02 | DE1 (Frankfurt) | `nbg1` / `fsn1` (closest, no Frankfurt) |
| North America - Los Angeles | Los Angeles | nasamp.duckdns.org | `lax` | LAX02 / LAX07 | - (Hillsboro) | - (`hil` Hillsboro) |
| Oceania - Sydney | Sydney | ocsamp.duckdns.org | `syd` | - | SYD1 | - |

Providers: [Vultr](https://vultr.com) (the only one with all 4 cities), [Misaka Network](https://misaka.io),
[OVHcloud](https://ovhcloud.com), [Hetzner](https://hetzner.com). Locations from
[Vultr data centers](https://www.vultr.com/features/datacenter-regions/),
[Misaka locations](https://www.misaka.io/locations),
[OVHcloud Public Cloud regions](https://www.ovhcloud.com/en/public-cloud/regions-availability/),
[Hetzner Cloud locations](https://docs.hetzner.com/cloud/general/locations/).

Each VPS runs the same kit as before (`HiveMQ/vps-setup.sh`): HiveMQ (MQTT TCP 1883, TLS 8883,
WebSocket 8884), the 24/7 game servers on **UDP** 1922 / 1930 / 1931 / 1932 (direct UDP, the fastest path),
MpBridge (server lists of all regions), the XDP flood filter and the network tuning.

## Option A: all 4 on Vultr, one command

```bash
git clone --depth 1 --filter=blob:none --sparse https://github.com/ThirtyFoxMCx00E1/wagu && cd wagu && git sparse-checkout set HiveMQ
export VULTR_API_KEY=...      # my.vultr.com > Account > API
export DUCKDNS_TOKEN=...      # duckdns.org (the account that owns mpsamp / eusamp / nasamp / ocsamp)
bash HiveMQ/providers/deploy-vultr.sh            # asia europe na oceania
bash HiveMQ/providers/deploy-vultr.sh --list     # names, cities, IPs
```

It makes one firewall group (`mp-regions`, the ports above + UDP 7777 / 7780 for open.mp cross-play),
4 servers (`PLAN=vc2-1c-2gb` by default, Ubuntu 24.04) and points each DuckDNS name at its server.
Setup runs by itself on first boot (about 5 minutes): `ssh root@IP tail -f /var/log/mp-firstboot.log`.

### Option A on Windows / phone-free PC: one program, no bash

`mp-deploy-vultr` does the same as `deploy-vultr.sh`, built on Vultr's official Go library
[govultr](https://github.com/vultr/govultr) (v3.34.0). Download it from the
[v4.12 release](https://github.com/ThirtyFoxMCx00E1/wagu/releases/tag/v4.12)
(`mp-deploy-vultr-windows-amd64.exe`, `-linux-amd64`, `-darwin-arm64`), double-click it, paste the
Vultr API key and the DuckDNS token when asked. `mp-deploy-vultr --list` shows the IPs; `asia oceania`
creates only those. Source: `HiveMQ/providers/vultr-go/` (`bash build.sh`, Go 1.23+).

## Option A2: Hetzner (3 regions) or OVHcloud (3 regions)

Mix providers if you like: each script only creates the regions you name, with the same firewall
ports, `user-data.sh` and DuckDNS step as the Vultr one.

```bash
# Hetzner Cloud (API token: Console > project > Security > API tokens, Read & Write)
export HCLOUD_TOKEN=...
bash HiveMQ/providers/deploy-hetzner.sh asia europe na   # sin cpx12, fsn1 cpx12, hil cpx11 (Hillsboro)
bash HiveMQ/providers/deploy-hetzner.sh --list

# OVHcloud Public Cloud (OpenStack, Anti-DDoS included): download the OpenStack RC file of a
# Public Cloud user first (Control Panel > Public Cloud > Users & Roles)
pip install python-openstackclient && source openrc.sh
bash HiveMQ/providers/deploy-ovh.sh asia europe oceania  # SGP1, DE1 Frankfurt, SYD1 Sydney
```

Hetzner has no Australia location and OVHcloud's North America regions are on the separate US
account (`NA_REGION=US-WEST-OR-1` with that account's RC file). A full set from two providers, for
example: OVHcloud for Asia / Europe / Oceania plus Hetzner `hil` or Vultr `lax` for North America
(`bash HiveMQ/providers/deploy-vultr.sh na`). API references:
[Hetzner Cloud API](https://docs.hetzner.cloud/) (same API as `hcloud-go` / `hcloud-python`),
[OVHcloud Public Cloud with OpenStack](https://help.ovhcloud.com/csm/en-public-cloud-compute-prepare-openstack-api-environment?id=kb_article_view&sysparm_article=KB0050995).

Misaka Network (LAX02, SIN03, FRA13) has no script: order the KVM VPS in the portal and use Option B
(paste `user-data.sh` as cloud-init, or run it as root over SSH: `REGION` and `DUCKDNS_TOKEN` filled in).

## Option B: any provider, by hand

1. Create an Ubuntu 24.04 / Debian 12 server in the city (table above), 2 GB RAM or more.
2. Paste `HiveMQ/providers/user-data.sh` as **User data / Startup script / Cloud-init**, with
   `REGION="asia"` (or `europe`, `na`, `oceania`) and your `DUCKDNS_TOKEN`.
3. Open the ports in the provider's firewall: TCP 22 80 1883 8883 8884 7681, UDP 1922 1930 1931 1932 7777 7780.

Without the token, change the 4 names on duckdns.org yourself (one IP each).

## DuckDNS note

DuckDNS ([server code](https://github.com/BenjaminHCCarr/duckdns-mirror), `UpdateServlet`) takes the
address the update comes **from** when `ip=` is empty. If that request goes out over IPv6, only the
AAAA record changes and the A record (IPv4) stays on the old home address. `user-data.sh` therefore
sends it with `curl -4`, retries until the answer is `OK`, and repeats it every 5 minutes (cron).
If Docker is already installed on the VPS it uses the
[linuxserver/docker-duckdns](https://github.com/linuxserver/docker-duckdns) container instead
(`SUBDOMAINS`, `TOKEN`, `UPDATE_IP=ipv4`, every 5 minutes with jitter):

```bash
docker run -d --name=mp-duckdns --net=host -e SUBDOMAINS=mpsamp -e TOKEN=your-token -e UPDATE_IP=ipv4 \
  --restart unless-stopped lscr.io/linuxserver/duckdns:latest
```

One subdomain per VPS (`mpsamp` on the Singapore one, `eusamp` on Frankfurt, `nasamp` on Los Angeles,
`ocsamp` on Sydney). Never put all four names in one updater, that is what tied them to one home IP.

## Check

- `getent hosts mpsamp.duckdns.org eusamp.duckdns.org nasamp.duckdns.org ocsamp.duckdns.org` = 4 different IPs.
- MP Launcher > region pin: each region shows its own ping. A region still on the same machine as
  another one is shown as "same server as Asia" and Auto never picks it (4.12).
- Your home line can stay as a 5th, private server; it is no longer the lobby of all 4 regions.
