#!/usr/bin/env bash
# MP 4.12: region VPSes on Hetzner Cloud (api.hetzner.cloud, same API as hcloud-go / hcloud-python).
# Hetzner has 3 of the 4 regions (no Australia - use Vultr or OVHcloud SYD1 for Oceania):
#   asia = Singapore (sin, cpx12)   europe = Falkenstein (fsn1, cpx12)   na = Hillsboro OR (hil, cpx11)
#
#   export HCLOUD_TOKEN=...         # console.hetzner.com > project > Security > API tokens (Read & Write)
#   export DUCKDNS_TOKEN=...        # optional: the DuckDNS names follow the new VPSes
#   bash deploy-hetzner.sh          # asia europe na  | bash deploy-hetzner.sh asia
#   TYPE=cpx22 bash deploy-hetzner.sh europe       # bigger plan
#   EU_LOC=nbg1 bash deploy-hetzner.sh europe      # Nuremberg instead of Falkenstein
#   bash deploy-hetzner.sh --list
set -euo pipefail
API=https://api.hetzner.cloud/v1
if [ "${MP_CRED_PROXY:-0}" != 1 ]; then : "${HCLOUD_TOKEN:?set HCLOUD_TOKEN (Hetzner Console > Security > API tokens)}"; fi
DIR="$(cd "$(dirname "$0")" && pwd)"
h() {
    local m=$1 p=$2; shift 2
    if [ "${MP_CRED_PROXY:-0}" = 1 ]; then curl -fsS -X "$m" -H "Content-Type: application/json" "$API$p" "$@"
    else curl -fsS -X "$m" -H "Authorization: Bearer $HCLOUD_TOKEN" -H "Content-Type: application/json" "$API$p" "$@"; fi
}
need() { command -v "$1" >/dev/null || { echo "install $1 first"; exit 1; }; }
need curl; need jq

if [ "${1:-}" = "--list" ]; then
    h GET "/servers?label_selector=mp&per_page=50" | jq -r '.servers[] | "\(.name)\t\(.datacenter.location.name)\t\(.public_net.ipv4.ip)\t\(.status)"'
    exit 0
fi

loc()  { case $1 in asia) echo sin;; europe) echo "${EU_LOC:-fsn1}";; na) echo hil;; *) echo "?";; esac; }
stype() { [ -n "${TYPE:-}" ] && { echo "$TYPE"; return; }; case $1 in na) echo cpx11;; *) echo cpx12;; esac; }  # 2 GB (HiveMQ)
REGIONS=("$@"); [ ${#REGIONS[@]} -eq 0 ] && REGIONS=(asia europe na)

# firewall "mp-regions": SSH, MQTT 1883/8883/8884, console 7681, certificates 80, game UDP, open.mp cross-play
FW=$(h GET "/firewalls?name=mp-regions" | jq -r '.firewalls[0].id // empty')
if [ -z "$FW" ]; then
    RULES=$(for r in tcp:22 tcp:80 tcp:1883 tcp:8883 tcp:8884 tcp:7681 udp:1922 udp:1930 udp:1931 udp:1932 udp:7777 udp:7780; do
        printf '{"direction":"in","protocol":"%s","port":"%s","source_ips":["0.0.0.0/0","::/0"]}\n' "${r%%:*}" "${r##*:}"; done | jq -s .)
    FW=$(h POST /firewalls -d "$(jq -n --argjson r "$RULES" '{name:"mp-regions",rules:$r}')" | jq -r '.firewall.id')
    echo "firewall mp-regions: $FW"
fi

for R in "${REGIONS[@]}"; do
    L=$(loc "$R"); [ "$L" = "?" ] && { echo "$R: Hetzner has no city for it (oceania: use deploy-vultr.sh oceania or deploy-ovh.sh oceania)"; continue; }
    UD=$(sed -e "s/__REGION__/$R/" -e "s/__DUCKDNS__/${DUCKDNS_TOKEN:-}/" "$DIR/user-data.sh")
    BODY=$(jq -n --arg n "mp-$R" --arg t "$(stype "$R")" --arg l "$L" --arg u "$UD" --argjson f "$FW" --arg r "$R" \
        '{name:$n,server_type:$t,image:"ubuntu-24.04",location:$l,user_data:$u,firewalls:[{firewall:$f}],labels:{mp:$r},public_net:{enable_ipv4:true,enable_ipv6:true}}')
    IP=$(h POST /servers -d "$BODY" | jq -r '.server.public_net.ipv4.ip')
    echo "mp-$R: created in $L ($(stype "$R")), IP $IP"
    if { [ -n "${DUCKDNS_TOKEN:-}" ] || [ "${MP_CRED_PROXY:-0}" = 1 ]; } && [ -n "$IP" ] && [ "$IP" != null ]; then
        D=$(case $R in asia) echo mpsamp;; europe) echo eusamp;; na) echo nasamp;; esac)
        if [ "${MP_CRED_PROXY:-0}" = 1 ]; then U="https://www.duckdns.org/update?domains=$D&ip=$IP"; else U="https://www.duckdns.org/update?domains=$D&token=$DUCKDNS_TOKEN&ip=$IP"; fi
        echo "duckdns $D.duckdns.org -> $IP: $(curl -fsS "$U" || echo failed)"
    fi
    echo "mp-$R: setup runs by itself, about 5 min: ssh root@$IP tail -f /var/log/mp-firstboot.log"
done
