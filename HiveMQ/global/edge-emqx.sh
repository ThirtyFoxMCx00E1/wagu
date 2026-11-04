#!/usr/bin/env bash
# MP Multiplayer 4.05 - global hub with EMQX (Docker). Heavier than Mosquitto (~300 MB RAM) but has a web
# dashboard on :18083 (login admin) with live client / topic / latency views, and scales far past 100 phones.
#
#   sudo MP_PASS=... WEB_PASS=... BRIDGE_PASS=... ADMIN_PASS=... bash edge-emqx.sh
set -euo pipefail
[ "$(id -u)" = 0 ] || { echo "run as root (sudo)"; exit 1; }
gen() { tr -dc 'A-Za-z0-9' </dev/urandom | head -c 20; }
MP_PASS=${MP_PASS:-$(gen)}; WEB_PASS=${WEB_PASS:-$(gen)}; BRIDGE_PASS=${BRIDGE_PASS:-$(gen)}; ADMIN_PASS=${ADMIN_PASS:-$(gen)}
HERE=$(cd "$(dirname "$0")" && pwd)
command -v docker >/dev/null || { curl -fsSL https://get.docker.com | sh; }
if [ -f "$HERE/../config/99-mp-network.conf" ]; then cp "$HERE/../config/99-mp-network.conf" /etc/sysctl.d/99-mp-network.conf; sysctl --system >/dev/null || true; fi

install -d /opt/mp-emqx
# built-in database logins, loaded at the first start
cat > /opt/mp-emqx/users.csv <<CSV
user_id,password,is_superuser
mp,$MP_PASS,false
web,$WEB_PASS,false
bridge,$BRIDGE_PASS,false
admin,$ADMIN_PASS,true
CSV
# topic rules: the same as the HiveMQ regions
cat > /opt/mp-emqx/acl.conf <<'ACL'
{allow, {username, {re, "^(mp|bridge)$"}}, all, ["thirtyfox/#"]}.
{allow, {username, "web"}, subscribe, ["thirtyfox/#"]}.
{allow, {username, "admin"}, all, ["#"]}.
{deny, all}.
ACL

docker rm -f mp-emqx 2>/dev/null || true
docker run -d --name mp-emqx --restart unless-stopped --network host \
  -v /opt/mp-emqx/users.csv:/opt/emqx/etc/users.csv:ro \
  -v /opt/mp-emqx/acl.conf:/opt/emqx/etc/acl.conf:ro \
  -e EMQX_DASHBOARD__DEFAULT_PASSWORD="$ADMIN_PASS" \
  -e EMQX_ALLOW_ANONYMOUS=false \
  -e EMQX_AUTHENTICATION__1__MECHANISM=password_based \
  -e EMQX_AUTHENTICATION__1__BACKEND=built_in_database \
  -e EMQX_AUTHENTICATION__1__USER_ID_TYPE=username \
  -e EMQX_AUTHENTICATION__1__PASSWORD_HASH_ALGORITHM__NAME=plain \
  -e EMQX_AUTHENTICATION__1__BOOTSTRAP_FILE=/opt/emqx/etc/users.csv \
  -e EMQX_AUTHENTICATION__1__BOOTSTRAP_TYPE=plain \
  -e EMQX_AUTHORIZATION__NO_MATCH=deny \
  -e EMQX_AUTHORIZATION__SOURCES__1__TYPE=file \
  -e EMQX_AUTHORIZATION__SOURCES__1__PATH=/opt/emqx/etc/acl.conf \
  -e EMQX_LISTENERS__TCP__DEFAULT__TCP_OPTIONS__NODELAY=true \
  -e EMQX_MQTT__MAX_MQUEUE_LEN=200 \
  -e EMQX_MQTT__MAX_PACKET_SIZE=256KB \
  emqx/emqx:5.8
command -v ufw >/dev/null && { ufw allow 1883/tcp; ufw allow 8083/tcp; ufw allow 18083/tcp; } || true
cat <<OUT

EMQX hub ready: MQTT 1883, WebSocket 8083 (/mqtt), dashboard http://<ip>:18083 (admin / $ADMIN_PASS).
  mp / $MP_PASS    web / $WEB_PASS    bridge / $BRIDGE_PASS
For TLS (8883 / wss 8084) put the Let's Encrypt cert in the dashboard: Management > Listeners.
OUT
