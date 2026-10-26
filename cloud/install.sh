#!/usr/bin/env bash
# MP Cloud installer - Debian 12 / Ubuntu 22.04 / 24.04, run as root.
#   curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/cloud/install.sh | sudo bash
#   ... | sudo bash -s -- --domain cloud.example.duckdns.org   (HTTPS with Caddy on 443)
#   ... | sudo bash -s -- --no-incus                           (Oracle-only console, no local servers)
set -euo pipefail
DOMAIN=""; INCUS=1; PORT=9000
while [ $# -gt 0 ]; do
  case "$1" in
    --domain) DOMAIN="$2"; shift 2;;
    --no-incus) INCUS=0; shift;;
    --port) PORT="$2"; shift 2;;
    *) echo "unknown option $1"; exit 1;;
  esac
done
[ "$(id -u)" = 0 ] || { echo "Run as root (sudo)."; exit 1; }
. /etc/os-release
export DEBIAN_FRONTEND=noninteractive
APP=/opt/mp-cloud; DATA=/var/lib/mp-cloud; REPO=https://github.com/ThirtyFoxMCx00E1/HiveMQ.git
say(){ printf '\n== %s\n' "$*"; }

say "Packages"
apt-get update -y
apt-get install -y python3 python3-venv python3-pip git curl ca-certificates gnupg btrfs-progs iptables

if [ "$INCUS" = 1 ]; then
  say "Incus (local servers)"
  if ! command -v incus >/dev/null; then
    if [ "$ID" = debian ] && [ "${VERSION_CODENAME:-}" = bookworm ]; then
      echo "deb http://deb.debian.org/debian bookworm-backports main" > /etc/apt/sources.list.d/backports.list
      apt-get update -y && apt-get install -y -t bookworm-backports incus
    elif apt-cache show incus >/dev/null 2>&1 && [ "$ID" = ubuntu ] && [ "${VERSION_ID%%.*}" -ge 24 ]; then
      apt-get install -y incus
    else
      mkdir -p /etc/apt/keyrings
      curl -fsSL https://pkgs.zabbly.com/key.asc -o /etc/apt/keyrings/zabbly.asc
      echo "deb [signed-by=/etc/apt/keyrings/zabbly.asc] https://pkgs.zabbly.com/incus/stable ${VERSION_CODENAME} main" > /etc/apt/sources.list.d/zabbly-incus.list
      apt-get update -y && apt-get install -y incus
    fi
  fi
  if ! incus storage list -f csv 2>/dev/null | grep -q .; then
    FREE=$(df -BG --output=avail / | tail -1 | tr -dc 0-9)
    SIZE=$(( FREE > 40 ? FREE - 20 : 20 ))
    cat <<YAML | incus admin init --preseed
config: {}
networks:
- name: incusbr0
  type: bridge
  config: {ipv4.address: 10.44.0.1/24, ipv4.nat: "true", ipv6.address: none}
storage_pools:
- name: default
  driver: btrfs
  config: {size: ${SIZE}GiB}
profiles:
- name: default
  devices:
    root: {path: /, pool: default, type: disk}
    eth0: {name: eth0, network: incusbr0, type: nic}
YAML
  fi
  DRIVER=incus
else
  DRIVER=none
fi

say "MP Cloud app"
if [ -d "$APP/src/.git" ]; then git -C "$APP/src" pull --ff-only; else rm -rf "$APP/src"; git clone --depth 1 "$REPO" "$APP/src"; fi
python3 -m venv "$APP/venv"
"$APP/venv/bin/pip" install -q --upgrade pip
"$APP/venv/bin/pip" install -q -r "$APP/src/cloud/requirements.txt"
mkdir -p "$DATA"; chmod 700 "$DATA"

cat > /etc/systemd/system/mp-cloud.service <<UNIT
[Unit]
Description=MP Cloud console
After=network-online.target incus.service
Wants=network-online.target

[Service]
WorkingDirectory=$APP/src/cloud
Environment=MP_CLOUD_DATA=$DATA
Environment=MP_CLOUD_DRIVER=$DRIVER
Environment=MP_CLOUD_PORT=$PORT
ExecStart=$APP/venv/bin/uvicorn mpcloud.app:app --host 0.0.0.0 --port $PORT --proxy-headers
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now mp-cloud
systemctl restart mp-cloud

say "Firewall"
open_port(){ iptables -C INPUT -p "$1" --dport "$2" -j ACCEPT 2>/dev/null || iptables -I INPUT -p "$1" --dport "$2" -j ACCEPT; }
open_port tcp "$PORT"; open_port tcp 22000:22999
for p in 80 443 1883 3000 6001 6002 7681 8000 8883 8884; do open_port tcp $p; done
for p in 1922 1930 1931 1932; do open_port udp $p; done
command -v netfilter-persistent >/dev/null && netfilter-persistent save || true

if [ -n "$DOMAIN" ]; then
  say "HTTPS for $DOMAIN (Caddy)"
  apt-get install -y debian-keyring debian-archive-keyring apt-transport-https
  curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/gpg.key | gpg --dearmor --yes -o /usr/share/keyrings/caddy-stable-archive-keyring.gpg
  curl -1sLf https://dl.cloudsmith.io/public/caddy/stable/debian.deb.txt > /etc/apt/sources.list.d/caddy-stable.list
  apt-get update -y && apt-get install -y caddy
  printf '%s {\n  reverse_proxy 127.0.0.1:%s\n}\n' "$DOMAIN" "$PORT" > /etc/caddy/Caddyfile
  systemctl restart caddy
  URL="https://$DOMAIN"
else
  IP=$(curl -fsS4 --max-time 5 https://ifconfig.me 2>/dev/null || hostname -I | awk '{print $1}')
  URL="http://$IP:$PORT"
fi

say "Done"
echo "Open $URL and create the first account - it becomes the admin."
echo "On Oracle/most clouds also open TCP $PORT (and 22000-22999 for local SSH) in the provider firewall."
