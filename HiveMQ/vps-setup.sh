#!/usr/bin/env bash
# MP 3.28.2: one command for a new 24/7 VPS (Ubuntu 22.04 / 24.04, Debian 12), as root:
#   curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | sudo bash
# Debian 12 (no sudo / curl yet), as root:  wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | bash -s europe
# Same as: apt install git + clone + install.sh --region Asia --domain mpsamp.duckdns.org --bridge,
# then the mod / website passwords from the public regions.txt (so every phone connects with no change),
# then update.sh. Other regions:  ... | sudo bash -s europe   (or na / oceania; domain eusamp / nasamp / ocsamp.duckdns.org)
# Point the DuckDNS name at this VPS's IP first (duckdns.org -> update ip), and open the ports
# (TCP 1883 8883 8884, UDP 1922 1930 1931 1932 - all four game servers).
set -euo pipefail
# region: first argument (curl ... | sudo bash -s europe) or REGION=; asia (default) europe na oceania
REGION="${1:-${REGION:-Asia}}"
case "$(printf '%s' "$REGION" | tr 'A-Z' 'a-z')" in
  asia|as) REGION=Asia; D=mpsamp;; europe|eu) REGION=Europe; D=eusamp;;
  na|us|"north america") REGION=NA; D=nasamp;; oceania|oc|au) REGION=Oceania; D=ocsamp;;
  *) printf 'region must be asia, europe, na or oceania (got %s)\n' "$REGION" >&2; exit 1;;
esac
DOMAIN="${DOMAIN:-$D.duckdns.org}"
EMAIL="${EMAIL:-mrfoxmanking@gmail.com}"
BRIDGE="${BRIDGE:-1}"
say() { printf '\033[1;32m[mp-setup]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[mp-setup]\033[0m %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root: curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | sudo bash   (Debian without sudo: su - , then wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/vps-setup.sh | bash)"

say "packages"
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq </dev/null && apt-get install -y -qq git curl ca-certificates </dev/null

say "getting the kit"
# 3.37 VPS: the kit is the HiveMQ/ folder of wagu; only that folder is downloaded (fast)
rm -rf /root/HiveMQ /root/wagu-kit
if git clone -q --depth 1 --filter=blob:none --sparse https://github.com/ThirtyFoxMCx00E1/wagu /root/wagu-kit </dev/null 2>/dev/null \
   && git -C /root/wagu-kit sparse-checkout set HiveMQ </dev/null 2>/dev/null; then :
else rm -rf /root/wagu-kit; git clone -q --depth 1 https://github.com/ThirtyFoxMCx00E1/wagu /root/wagu-kit </dev/null; fi
mv /root/wagu-kit/HiveMQ /root/HiveMQ; rm -rf /root/wagu-kit
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
