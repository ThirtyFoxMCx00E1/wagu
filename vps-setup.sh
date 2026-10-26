#!/usr/bin/env bash
# MP 3.28.2: one command for a new 24/7 VPS (Ubuntu 22.04 / 24.04, Debian 12), as root:
#   curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh | sudo bash
# Same as: apt install git + clone + install.sh --region Asia --domain mpsamp.duckdns.org --bridge,
# then the mod / website passwords from the public regions.txt (so every phone connects with no change),
# then update.sh. Change the region / domain:  ... | sudo REGION=Europe DOMAIN=eusamp.duckdns.org bash
# Point the DuckDNS name at this VPS's IP first (duckdns.org -> update ip), and open the ports
# (TCP 1883 8883 8884, UDP 1932 + the other server ports in the README).
set -euo pipefail
REGION="${REGION:-Asia}"
DOMAIN="${DOMAIN:-mpsamp.duckdns.org}"
EMAIL="${EMAIL:-mrfoxmanking@gmail.com}"
BRIDGE="${BRIDGE:-1}"
say() { printf '\033[1;32m[mp-setup]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[mp-setup]\033[0m %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root: curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/vps-setup.sh | sudo bash"

say "packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq </dev/null && apt-get install -y -qq git curl ca-certificates </dev/null

say "getting the kit"
rm -rf /root/HiveMQ
git clone -q --depth 1 https://github.com/ThirtyFoxMCx00E1/HiveMQ /root/HiveMQ </dev/null
cd /root/HiveMQ

ARGS=(--region "$REGION" --domain "$DOMAIN" --email "$EMAIL")
[ "$BRIDGE" = 1 ] && ARGS+=(--bridge)
say "installing $REGION ($DOMAIN) - HiveMQ + bridge + 24/7 servers (Mega Vil on UDP 1932)"
bash ./install.sh "${ARGS[@]}" </dev/null

# the phones already know the passwords in the website's regions.txt: use the same ones
say "using the passwords from regions.txt"
RT="$(curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/regions.txt || true)"
blk="$(printf '%s\n' "$RT" | awk -v r="[$REGION]" '$0==r{f=1;next} /^\[/{f=0} f')"
MPP="$(printf '%s\n' "$blk" | sed -n 's/^pass=//p' | head -1)"
WBP="$(printf '%s\n' "$blk" | sed -n 's/^web_pass=//p' | head -1)"
ENV=/etc/mp-hivemq/mp.env
if [ -n "$MPP" ] && [ -f "$ENV" ]; then
    sed -i "s|^MP_PASS=.*|MP_PASS=$MPP|" "$ENV"
    [ -n "$WBP" ] && sed -i "s|^WEB_PASS=.*|WEB_PASS=$WBP|" "$ENV"
    say "updating with those passwords"
    bash ./update.sh </dev/null
else
    say "no $REGION block in regions.txt: keeping the new random passwords (put them in regions.txt)"
fi

say "status"
systemctl --no-pager --lines=0 status hivemq mp-rp 2>/dev/null | grep -E '●|Active:' || true
say "done - Mega Vil Futuristic runs 24/7 here. Logs: journalctl -u mp-rp -f"
