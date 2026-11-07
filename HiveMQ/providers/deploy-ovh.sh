#!/usr/bin/env bash
# MP 4.12: region VPSes on OVHcloud Public Cloud (OpenStack API, Anti-DDoS included).
# OVHcloud has 3 of the 4 regions on one account (North America is the separate ovhcloud.com/en-us
# account - region US-WEST-OR-1 there, same script with its own openrc):
#   asia = Singapore (SGP1)   europe = Frankfurt (DE1)   oceania = Sydney (SYD1)
#
#   OVHcloud Control Panel > Public Cloud > your project > Users & Roles > add a user (Administrator)
#   > "Download OpenStack's RC file" (any region), then:
#   source openrc.sh                # asks for the OpenStack user's password
#   pip install python-openstackclient   (once)
#   export DUCKDNS_TOKEN=...        # optional
#   bash deploy-ovh.sh              # asia europe oceania | bash deploy-ovh.sh oceania
#   FLAVOR=b3-8 bash deploy-ovh.sh  # bigger plan (default d2-4: 2 vCPU / 4 GB)
#   NA_REGION=US-WEST-OR-1 bash deploy-ovh.sh na   # with the US account's openrc
set -euo pipefail
command -v openstack >/dev/null || { echo "pip install python-openstackclient, then source your OVHcloud openrc.sh"; exit 1; }
: "${OS_AUTH_URL:?source the OpenStack RC file from the OVHcloud Control Panel first}"
DIR="$(cd "$(dirname "$0")" && pwd)"
FLAVOR="${FLAVOR:-d2-4}"
reg() { case $1 in asia) echo SGP1;; europe) echo DE1;; oceania) echo SYD1;; na) echo "${NA_REGION:-?}";; *) echo "?";; esac; }
REGIONS=("$@"); [ ${#REGIONS[@]} -eq 0 ] && REGIONS=(asia europe oceania)
for R in "${REGIONS[@]}"; do
    G=$(reg "$R"); [ "$G" = "?" ] && { echo "$R: set NA_REGION (OVHcloud US account) or use deploy-vultr.sh / deploy-hetzner.sh"; continue; }
    export OS_REGION_NAME="$G"
    if ! openstack security group show mp-regions >/dev/null 2>&1; then
        openstack security group create mp-regions >/dev/null
        for r in tcp:22 tcp:80 tcp:1883 tcp:8883 tcp:8884 tcp:7681 udp:1922 udp:1930 udp:1931 udp:1932 udp:7777 udp:7780; do
            openstack security group rule create --ingress --protocol "${r%%:*}" --dst-port "${r##*:}" --remote-ip 0.0.0.0/0 mp-regions >/dev/null
        done
    fi
    IMG=$(openstack image list --public -f value -c Name | grep -m1 -x 'Ubuntu 24.04' || openstack image list --public -f value -c Name | grep -m1 '^Ubuntu 24')
    UD=$(mktemp); sed -e "s/__REGION__/$R/" -e "s/__DUCKDNS__/${DUCKDNS_TOKEN:-}/" "$DIR/user-data.sh" > "$UD"
    openstack server create --flavor "$FLAVOR" --image "$IMG" --network Ext-Net --security-group mp-regions \
        --user-data "$UD" ${KEY_NAME:+--key-name "$KEY_NAME"} --wait "mp-$R" >/dev/null
    rm -f "$UD"
    IP=$(openstack server show "mp-$R" -f json -c addresses | jq -r '.addresses["Ext-Net"][]? | select(test("^[0-9.]+$"))' | head -1)
    echo "mp-$R: $G $FLAVOR IP $IP"
    if [ -n "${DUCKDNS_TOKEN:-}" ] && [ -n "$IP" ]; then
        D=$(case $R in asia) echo mpsamp;; europe) echo eusamp;; na) echo nasamp;; oceania) echo ocsamp;; esac)
        echo "duckdns $D.duckdns.org -> $IP: $(curl -fsS "https://www.duckdns.org/update?domains=$D&token=$DUCKDNS_TOKEN&ip=$IP" || echo failed)"
    fi
    echo "mp-$R: setup runs by itself, about 5 min: ssh ubuntu@$IP sudo tail -f /var/log/mp-firstboot.log"
done
