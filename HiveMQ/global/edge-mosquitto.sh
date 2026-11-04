#!/usr/bin/env bash
# MP Multiplayer 4.05 - global hub on a small VPS with Eclipse Mosquitto (5-20 MB RAM, fine for 100+ phones).
# Same logins + topic rules as the HiveMQ regions, so the mod, the website and MpBridge use it unchanged.
#
#   sudo MP_PASS=... WEB_PASS=... BRIDGE_PASS=... DOMAIN=fr.example.duckdns.org EMAIL=you@mail bash edge-mosquitto.sh
#
# Ports: 1883 MQTT, 8883 MQTT over TLS + 8884 secure WebSocket (only when DOMAIN is set; Let's Encrypt).
# Then: add a [Region] block to regions.txt (see regions-template.txt) and a line to bridge.conf.
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
gen() { tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20; }
MP_PASS=${MP_PASS:-$(gen)}; WEB_PASS=${WEB_PASS:-$(gen)}; BRIDGE_PASS=${BRIDGE_PASS:-$(gen)}; ADMIN_PASS=${ADMIN_PASS:-$(gen)}
DOMAIN=${DOMAIN:-}; EMAIL=${EMAIL:-}
HERE=$(cd "$(dirname "$0")" && pwd)

apt-get update -y
apt-get install -y mosquitto mosquitto-clients
[ -n "$DOMAIN" ] && apt-get install -y certbot

# low-latency TCP (BBR, fq, no slow start after idle, no corking) - the same file the HiveMQ regions use
if [ -f "$HERE/../config/99-mp-network.conf" ]; then cp "$HERE/../config/99-mp-network.conf" /etc/sysctl.d/99-mp-network.conf; sysctl --system >/dev/null || true; fi

install -d -o mosquitto -g mosquitto /etc/mosquitto/mp
PW=/etc/mosquitto/mp/passwd; : > "$PW"
mosquitto_passwd -b "$PW" mp "$MP_PASS"
mosquitto_passwd -b "$PW" web "$WEB_PASS"
mosquitto_passwd -b "$PW" bridge "$BRIDGE_PASS"
mosquitto_passwd -b "$PW" admin "$ADMIN_PASS"
chown mosquitto:mosquitto "$PW"; chmod 600 "$PW"

cat > /etc/mosquitto/mp/acl <<'ACL'
# phones + bridge: only the MP topics
user mp
topic readwrite thirtyfox/#
user bridge
topic readwrite thirtyfox/#
# website: reads the server lists, sends nothing
user web
topic read thirtyfox/#
user admin
topic readwrite #
ACL
chown mosquitto:mosquitto /etc/mosquitto/mp/acl

TLS=""
if [ -n "$DOMAIN" ]; then
  certbot certonly --standalone --non-interactive --agree-tos -m "${EMAIL:-admin@$DOMAIN}" -d "$DOMAIN" || true
  LE=/etc/letsencrypt/live/$DOMAIN
  if [ -f "$LE/fullchain.pem" ]; then
    install -d /etc/mosquitto/mp/certs
    cp -L "$LE/fullchain.pem" "$LE/privkey.pem" /etc/mosquitto/mp/certs/; chown -R mosquitto:mosquitto /etc/mosquitto/mp/certs
    TLS="
listener 8883
certfile /etc/mosquitto/mp/certs/fullchain.pem
keyfile /etc/mosquitto/mp/certs/privkey.pem
listener 8884
protocol websockets
certfile /etc/mosquitto/mp/certs/fullchain.pem
keyfile /etc/mosquitto/mp/certs/privkey.pem"
    # renewals copy the new cert and reload
    cat > /etc/letsencrypt/renewal-hooks/deploy/mp-mosquitto.sh <<HOOK
#!/bin/sh
cp -L $LE/fullchain.pem $LE/privkey.pem /etc/mosquitto/mp/certs/ && chown -R mosquitto:mosquitto /etc/mosquitto/mp/certs && systemctl reload mosquitto
HOOK
    chmod +x /etc/letsencrypt/renewal-hooks/deploy/mp-mosquitto.sh
  fi
fi

cat > /etc/mosquitto/conf.d/mp.conf <<CONF
# MP Multiplayer hub (4.05)
per_listener_settings false
allow_anonymous false
password_file /etc/mosquitto/mp/passwd
acl_file /etc/mosquitto/mp/acl
persistence false
# game frames are QoS 0 and small: no queue for old positions, keep up to 100+ phones moving
max_inflight_messages 0
max_queued_messages 200
max_packet_size 262144
max_keepalive 120
set_tcp_nodelay true
listener 1883
$TLS
CONF

systemctl enable mosquitto; systemctl restart mosquitto
command -v ufw >/dev/null && { ufw allow 1883/tcp; ufw allow 8883/tcp; ufw allow 8884/tcp; ufw allow 80/tcp; } || true

cat <<OUT

Mosquitto hub ready.
  mp     / $MP_PASS      (phones -> regions.txt user / pass)
  web    / $WEB_PASS     (website -> regions.txt web_user / web_pass)
  bridge / $BRIDGE_PASS  (MpBridge -> bridge.conf, keep private)
  admin  / $ADMIN_PASS   (MQTTX console, keep private)
Test:  mosquitto_sub -h ${DOMAIN:-<ip>} -u mp -P '$MP_PASS' -t 'thirtyfox/#' -v
OUT
