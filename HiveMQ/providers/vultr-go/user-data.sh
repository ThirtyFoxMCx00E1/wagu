#!/bin/bash
# MP 4.12: first-boot script for ONE region VPS (paste it as "User data" / "Startup script" on
# Vultr, Hetzner, OVHcloud, Misaka or any Ubuntu 24.04 / Debian 12 cloud server).
# Change the 2 lines below (or let deploy-vultr.sh fill them in):
REGION="__REGION__"            # asia | europe | na | oceania
DUCKDNS_TOKEN="__DUCKDNS__"    # optional: your duckdns.org token -> mpsamp / eusamp / nasamp / ocsamp point here
set -u
exec > /var/log/mp-firstboot.log 2>&1
case "$REGION" in asia) D=mpsamp;; europe) D=eusamp;; na) D=nasamp;; oceania) D=ocsamp;; *) D=mpsamp;; esac
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq && apt-get install -y -qq curl ca-certificates >/dev/null
if [ -n "$DUCKDNS_TOKEN" ] && [ "$DUCKDNS_TOKEN" != "__DUCKDNS__" ]; then
    # DuckDNS (UpdateServlet, github.com/BenjaminHCCarr/duckdns-mirror): an empty ip= takes the address
    # the request comes FROM - over IPv6 that would only set the AAAA record and leave the A record
    # on the old home IP, so the request is forced over IPv4 (-4) and the answer must be "OK"
    duck() { curl -4 -fsS --max-time 15 "https://www.duckdns.org/update?domains=$D&token=$DUCKDNS_TOKEN&ip=&verbose=true"; }
    for i in 1 2 3 4 5; do R="$(duck || true)"; echo "duckdns $D: $R"; case "$R" in OK*) break;; esac; sleep 10; done
    if command -v docker >/dev/null; then
        # Docker already on this VPS: the linuxserver/docker-duckdns container keeps the name on this
        # IPv4 (every 5 min + jitter; UPDATE_IP=ipv4 = Cloudflare whoami, never only the AAAA record)
        docker rm -f mp-duckdns >/dev/null 2>&1 || true
        docker run -d --name=mp-duckdns --net=host -e SUBDOMAINS="$D" -e TOKEN="$DUCKDNS_TOKEN" -e UPDATE_IP=ipv4 \
            --restart unless-stopped lscr.io/linuxserver/duckdns:latest && echo "duckdns container started"
    else
        echo "*/5 * * * * root curl -4 -fsS --max-time 15 'https://www.duckdns.org/update?domains=$D&token=$DUCKDNS_TOKEN&ip=' >/dev/null 2>&1" > /etc/cron.d/mp-duckdns
    fi
    sleep 75   # the old address expires (TTL 60 s) before the TLS certificate is requested
fi
[ -z "$DUCKDNS_TOKEN" ] || [ "$DUCKDNS_TOKEN" = "__DUCKDNS__" ] && sleep 90   # deploy-vultr.sh sets the DuckDNS name: let it reach the DNS first
# open the ports on the VPS itself too (some images ship with ufw / iptables on)
if command -v ufw >/dev/null && ufw status | grep -q active; then
    for p in 22/tcp 1883/tcp 8883/tcp 8884/tcp 7681/tcp 80/tcp 1922/udp 1930/udp 1931/udp 1932/udp 7777/udp 7780/udp; do ufw allow $p; done
fi
curl -fsSL https://mpsamp-open.github.io/HiveMQ/vps-setup.sh | bash -s "$REGION"
echo "MP first boot done ($REGION)"
