#!/usr/bin/env bash
# MP 4.12: the 4 region VPSes on Vultr in one command - each in its own city:
#   Asia = Singapore (sgp)   Europe = Frankfurt (fra)   NA = Los Angeles (lax)   Oceania = Sydney (syd)
# Vultr is the one provider of the four with all 4 cities (Hetzner / OVHcloud / Misaka: see
# docs/REGIONS_VPS.md and use user-data.sh there by hand).
#
#   export VULTR_API_KEY=...        # my.vultr.com > Account > API (allow your IP)
#   export DUCKDNS_TOKEN=...        # optional, duckdns.org: the 4 names follow the new VPSes
#   bash deploy-vultr.sh            # all 4      | bash deploy-vultr.sh asia oceania  (some)
#   PLAN=vc2-2c-4gb bash deploy-vultr.sh          # bigger plan (default vc2-1c-2gb: HiveMQ needs 2 GB)
#   bash deploy-vultr.sh --list     # your MP VPSes + IPs
set -euo pipefail
API=https://api.vultr.com/v2
# MP_CRED_PROXY=1: an HTTPS proxy adds the API key / DuckDNS token (nothing secret in this script)
if [ "${MP_CRED_PROXY:-0}" != 1 ]; then : "${VULTR_API_KEY:?set VULTR_API_KEY (my.vultr.com > Account > API)}"; fi
PLAN="${PLAN:-vc2-1c-2gb}"
OS_ID="${OS_ID:-2284}"          # Ubuntu 24.04 LTS x64
DIR="$(cd "$(dirname "$0")" && pwd)"
v() {
    local m=$1 p=$2; shift 2
    if [ "${MP_CRED_PROXY:-0}" = 1 ]; then curl -fsS -X "$m" -H "Content-Type: application/json" "$API$p" "$@"
    else curl -fsS -X "$m" -H "Authorization: Bearer $VULTR_API_KEY" -H "Content-Type: application/json" "$API$p" "$@"; fi
}
need() { command -v "$1" >/dev/null || { echo "install $1 first"; exit 1; }; }
need curl; need jq; need base64

if [ "${1:-}" = "--list" ]; then
    v GET "/instances?label=&per_page=100" | jq -r '.instances[] | select(.label|startswith("mp-")) | "\(.label)\t\(.region)\t\(.main_ip)\t\(.status)/\(.server_status)"'
    exit 0
fi

city() { case $1 in asia) echo sgp;; europe) echo fra;; na) echo lax;; oceania) echo syd;; *) echo "?";; esac; }
REGIONS=("$@"); [ ${#REGIONS[@]} -eq 0 ] && REGIONS=(asia europe na oceania)

# one firewall group for all 4: SSH, MQTT 1883 / TLS 8883 / WebSocket 8884, web console 7681,
# certificates 80, the game servers UDP 1922 1930 1931 1932, open.mp cross-play 7777 + 7780
FW=$(v GET "/firewalls?per_page=100" | jq -r '.firewall_groups[] | select(.description=="mp-regions") | .id' | head -1)
if [ -z "$FW" ]; then
    FW=$(v POST /firewalls -d '{"description":"mp-regions"}' | jq -r '.firewall_group.id')
    for r in tcp:22 tcp:80 tcp:1883 tcp:8883 tcp:8884 tcp:7681 udp:1922 udp:1930 udp:1931 udp:1932 udp:7777 udp:7780; do
        v POST "/firewalls/$FW/rules" -d "{\"ip_type\":\"v4\",\"protocol\":\"${r%%:*}\",\"port\":\"${r##*:}\",\"subnet\":\"0.0.0.0\",\"subnet_size\":0,\"notes\":\"mp\"}" >/dev/null
    done
    echo "firewall group mp-regions: $FW"
fi

for R in "${REGIONS[@]}"; do
    C=$(city "$R"); [ "$C" = "?" ] && { echo "unknown region $R (asia europe na oceania)"; continue; }
    UD=$(sed -e "s/__REGION__/$R/" -e "s/__DUCKDNS__/${DUCKDNS_TOKEN:-}/" "$DIR/user-data.sh" | base64 -w0)
    ID=$(v POST /instances -d "{\"region\":\"$C\",\"plan\":\"$PLAN\",\"os_id\":$OS_ID,\"label\":\"mp-$R\",\"hostname\":\"mp-$R\",\"firewall_group_id\":\"$FW\",\"user_data\":\"$UD\",\"backups\":\"disabled\"}" | jq -r '.instance.id')
    echo "mp-$R: created in $C ($ID), waiting for its IP..."
    for i in $(seq 1 60); do
        IP=$(v GET "/instances/$ID" | jq -r '.instance.main_ip')
        [ -n "$IP" ] && [ "$IP" != "0.0.0.0" ] && break; sleep 5
    done
    if { [ -n "${DUCKDNS_TOKEN:-}" ] || [ "${MP_CRED_PROXY:-0}" = 1 ]; } && [ -n "$IP" ]; then   # at once with the exact IPv4 (the VPS repeats it every 5 min)
        D=$(case $R in asia) echo mpsamp;; europe) echo eusamp;; na) echo nasamp;; oceania) echo ocsamp;; esac)
        if [ "${MP_CRED_PROXY:-0}" = 1 ]; then U="https://www.duckdns.org/update?domains=$D&ip=$IP"; else U="https://www.duckdns.org/update?domains=$D&token=$DUCKDNS_TOKEN&ip=$IP"; fi
        echo "duckdns $D.duckdns.org -> $IP: $(curl -fsS "$U" || echo failed)"
    fi
    echo "mp-$R: $IP  (setup runs by itself, about 5 min: ssh root@$IP tail -f /var/log/mp-firstboot.log)"
done
echo
echo "Without DUCKDNS_TOKEN: point mpsamp / eusamp / nasamp / ocsamp.duckdns.org at the IPs above."
echo "Check: every region shows its own ping in MP Launcher > region pin (no 'same server as')."
