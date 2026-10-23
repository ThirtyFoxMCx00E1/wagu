#!/usr/bin/env bash
# MP Multiplayer - regional HiveMQ broker + 24/7 default server, one command.
#
#   sudo ./install.sh --region Asia [--domain asia-mp.duckdns.org --email you@mail.com] [--bridge]
#
# Run it once on every VPS (one VPS per region: Asia, Europe, NA, Oceania).
# The first run creates /etc/mp-hivemq/mp.env with the shared logins. Copy that file to the
# other servers (same passwords everywhere), then run install.sh there with their region.
#
# What it installs (Ubuntu 22.04/24.04 or Debian 12, x86_64 or arm64):
#   - Java 21 (your OpenJDK 21 from the wagu release on x86_64, Eclipse Temurin 21 on arm64)
#   - HiveMQ Community Edition 2026.5 (free, no connection limit) + File RBAC extension (logins + topic rules)
#   - mp_server: the region's "MP LAN Server" (default server), always online
#   - optional: MpBridge (joins the server lists of all regions; run it on ONE server only)
#   - systemd services (start on boot, restart on crash), firewall, network tuning
#   - optional TLS (Let's Encrypt) for MQTT 8883 and WebSocket 8884 (the website needs 8884)
#   - web console (ttyd, https://DOMAIN:7681, password) + status beacon for the website's Termius page
set -euo pipefail

HIVEMQ_VER="2026.5"
HIVEMQ_URL="https://github.com/hivemq/hivemq-community-edition/releases/download/${HIVEMQ_VER}/hivemq-ce-${HIVEMQ_VER}.zip"
RBAC_VER="4.6.18"
RBAC_URL="https://github.com/hivemq/hivemq-file-rbac-extension/releases/download/${RBAC_VER}/hivemq-file-rbac-extension-${RBAC_VER}.zip"
RBAC_SHA256="4089d09e5d2b6621a07aa86032230dce5aefbcc37130ef5c714fb6fa7185e3e6"
JDK_X64_URL="https://github.com/ThirtyFoxMCx00E1/wagu/releases/download/Assets-1-2/OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz"   # 3.10: wagu release "HiveMQ + OpenJDK Java 21"
JDK_X64_FALLBACK="https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse"
JDK_ARM_URL="https://api.adoptium.net/v3/binary/latest/21/ga/linux/aarch64/jdk/hotspot/normal/eclipse"
MOD_REPO="https://github.com/ThirtyFoxMCx00E1/wagu.git"

BASE=/opt/mp
ETC=/etc/mp-hivemq
ENV_FILE=$ETC/mp.env
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say()  { printf '\033[1;36m[mp]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[mp]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[mp]\033[0m %s\n' "$*" >&2; exit 1; }
rand() { tr -dc 'A-Za-z0-9' </dev/urandom | head -c "${1:-24}" || true; }

[ "$(id -u)" = 0 ] || die "run as root: sudo ./install.sh --region Asia"

# ---------------------------------------------------------------- arguments
ARG_REGION=""; ARG_DOMAIN=""; ARG_EMAIL=""; ARG_BRIDGE=""; ARG_NOSERVER=""
while [ $# -gt 0 ]; do
    case "$1" in
        --region) ARG_REGION="$2"; shift 2;;
        --domain) ARG_DOMAIN="$2"; shift 2;;
        --email)  ARG_EMAIL="$2"; shift 2;;
        --bridge) ARG_BRIDGE=1; shift;;
        --no-server) ARG_NOSERVER=1; shift;;
        -h|--help) sed -n '2,20p' "$0"; exit 0;;
        *) die "unknown option $1";;
    esac
done

# ---------------------------------------------------------------- settings (mp.env)
mkdir -p "$ETC"; chmod 700 "$ETC"
if [ ! -f "$ENV_FILE" ]; then
    say "first install: creating $ENV_FILE (shared logins - copy it to your other regions)"
    cat > "$ENV_FILE" <<EOF
# MP Multiplayer broker settings. SAME file on every region's server, except REGION / DOMAIN.
REGION=Asia
# optional: a (free) domain pointing to this server, e.g. from duckdns.org -> enables TLS + the website's live list
DOMAIN=
EMAIL=

# public address of each region's broker (domain or IP) - used for the bridge and regions.txt
HOST_ASIA=
HOST_EUROPE=
HOST_NA=
HOST_OCEANIA=

# mod login (ships inside regions.txt, not secret - the topic rules protect it)
MP_USER=mp
MP_PASS=$(rand 20)
# website login (read-only server list, not secret)
WEB_USER=web
WEB_PASS=$(rand 20)
# secret logins - never publish these
BRIDGE_PASS=$(rand 32)
ADMIN_PASS=$(rand 32)

# 24/7 default server of this region
RUN_SERVER=1
SERVER_PORT=1922
SERVER_NAME="MP LAN Server"
MAX_PLAYERS=100
# run MpBridge here (only on ONE server of the whole network)
RUN_BRIDGE=0

# Java heap. Empty = automatic: -Xms2g -Xmx4g with 6 GB RAM or more, ~60 % of RAM below that.
JVM_XMS=
JVM_XMX=
# limits per broker
MAX_CONNECTIONS=20000
BANDWIDTH_PER_CLIENT=524288
EOF
    chmod 600 "$ENV_FILE"
fi
# 2.21: settings added after the first install
grep -q '^CONSOLE=' "$ENV_FILE" || cat >> "$ENV_FILE" <<EOF

# web console (browser terminal, website "Termius" page): https://DOMAIN:7681  (needs DOMAIN for TLS)
CONSOLE=1
CONSOLE_USER=admin
CONSOLE_PASS=$(rand 20)
# 1 = also serve the console without TLS on http://IP:7681 (not recommended)
CONSOLE_INSECURE=0
EOF
# shellcheck disable=SC1090
. "$ENV_FILE"
# mp.env copied from another region: drop that server's own domain / bridge setting
if [ -n "$ARG_REGION" ] && [ "${ARG_REGION,,}" != "${REGION,,}" ]; then DOMAIN=; EMAIL=; RUN_BRIDGE=0; fi
[ -n "$ARG_REGION" ] && REGION="$ARG_REGION"
[ -n "$ARG_DOMAIN" ] && DOMAIN="$ARG_DOMAIN"
[ -n "$ARG_EMAIL" ]  && EMAIL="$ARG_EMAIL"
[ -n "$ARG_BRIDGE" ] && RUN_BRIDGE=1
[ -n "$ARG_NOSERVER" ] && RUN_SERVER=0
case "${REGION,,}" in
    asia|as) REGION=Asia;; europe|eu) REGION=Europe;; na|us|"north america") REGION=NA;; oceania|oc|au) REGION=Oceania;;
    *) die "--region must be Asia, Europe, NA or Oceania (got '$REGION')";;
esac
# remember this server's own values
sed -i "s|^REGION=.*|REGION=$REGION|; s|^DOMAIN=.*|DOMAIN=${DOMAIN:-}|; s|^EMAIL=.*|EMAIL=${EMAIL:-}|; s|^RUN_BRIDGE=.*|RUN_BRIDGE=${RUN_BRIDGE:-0}|; s|^RUN_SERVER=.*|RUN_SERVER=${RUN_SERVER:-1}|" "$ENV_FILE"
say "region $REGION${DOMAIN:+, domain $DOMAIN}"

# ---------------------------------------------------------------- system check
ARCH="$(uname -m)"
CPUS="$(nproc)"; RAM_MB="$(awk '/MemTotal/ {print int($2/1024)}' /proc/meminfo)"
say "machine: $ARCH, $CPUS vCPU, $RAM_MB MB RAM"
[ "$CPUS" -lt 4 ] && warn "HiveMQ recommends 4 vCPUs (you have $CPUS). It runs, but keep the player count lower."
[ "$RAM_MB" -lt 3500 ] && warn "less than 4 GB RAM: the heap is reduced automatically."

# ---------------------------------------------------------------- packages
export DEBIAN_FRONTEND=noninteractive
say "installing packages"
apt-get update -qq
apt-get install -y -qq curl ca-certificates unzip tar openssl ufw git make g++ python3 >/dev/null
[ -n "${DOMAIN:-}" ] && apt-get install -y -qq certbot >/dev/null

id mp >/dev/null 2>&1 || useradd --system --home "$BASE" --shell /usr/sbin/nologin mp
mkdir -p "$BASE"

# ---------------------------------------------------------------- Java 21
JAVA_HOME="$BASE/jdk"
if [ ! -x "$JAVA_HOME/bin/java" ]; then
    say "installing Java 21 ($ARCH)"
    tmp="$(mktemp -d)"
    case "$ARCH" in
        x86_64|amd64)  curl -fL --retry 3 -o "$tmp/jdk.tar.gz" "$JDK_X64_URL" || { warn "wagu JDK download failed - using Eclipse Temurin 21"; curl -fL --retry 3 -o "$tmp/jdk.tar.gz" "$JDK_X64_FALLBACK"; };;
        aarch64|arm64) curl -fL --retry 3 -o "$tmp/jdk.tar.gz" "$JDK_ARM_URL";;
        *) die "unsupported CPU $ARCH (need x86_64 or arm64)";;
    esac
    mkdir -p "$tmp/x"; tar -xzf "$tmp/jdk.tar.gz" -C "$tmp/x"
    rm -rf "$JAVA_HOME"; mv "$tmp/x"/* "$JAVA_HOME"; rm -rf "$tmp"
fi
"$JAVA_HOME/bin/java" -version 2>&1 | head -1

# ---------------------------------------------------------------- HiveMQ CE + RBAC
HQ="$BASE/hivemq"
if [ ! -f "$HQ/bin/hivemq.jar" ] || [ "$(cat "$HQ/.version" 2>/dev/null)" != "$HIVEMQ_VER" ]; then
    say "installing HiveMQ CE $HIVEMQ_VER"
    tmp="$(mktemp -d)"
    curl -fL --retry 3 -o "$tmp/hq.zip" "$HIVEMQ_URL"
    unzip -q "$tmp/hq.zip" -d "$tmp"
    systemctl stop hivemq 2>/dev/null || true
    rm -rf "$HQ"; mv "$tmp/hivemq-ce-$HIVEMQ_VER" "$HQ"; rm -rf "$tmp"
    echo "$HIVEMQ_VER" > "$HQ/.version"
    chmod +x "$HQ/bin/"*.sh
fi
rm -rf "$HQ/extensions/hivemq-allow-all-extension"     # NEVER allow anonymous clients
RB="$HQ/extensions/hivemq-file-rbac-extension"
if [ ! -f "$RB/hivemq-file-rbac-extension-$RBAC_VER.jar" ]; then
    say "installing File RBAC extension $RBAC_VER"
    tmp="$(mktemp -d)"
    curl -fL --retry 3 -o "$tmp/rbac.zip" "$RBAC_URL"
    echo "$RBAC_SHA256  $tmp/rbac.zip" | sha256sum -c - >/dev/null || die "RBAC download checksum mismatch"
    rm -rf "$RB"; unzip -q "$tmp/rbac.zip" -d "$HQ/extensions"; rm -rf "$tmp"
fi
cp "$SRC_DIR/config/rbac-config.xml" "$RB/conf/config.xml"

hash_pw() { "$JAVA_HOME/bin/java" -jar "$RB/hivemq-file-rbac-extension-$RBAC_VER.jar" -p "$1" -q 2>/dev/null | tail -1; }
say "writing logins (hashed)"
MP_HASH="$(hash_pw "$MP_PASS")"; WEB_HASH="$(hash_pw "$WEB_PASS")"
BRIDGE_HASH="$(hash_pw "$BRIDGE_PASS")"; ADMIN_HASH="$(hash_pw "$ADMIN_PASS")"
[ -n "$MP_HASH" ] && [ -n "$ADMIN_HASH" ] || die "could not hash the passwords"
sed -e "s|@@MP_USER@@|$MP_USER|; s|@@WEB_USER@@|$WEB_USER|" \
    -e "s|@@MP_HASH@@|$MP_HASH|; s|@@WEB_HASH@@|$WEB_HASH|; s|@@BRIDGE_HASH@@|$BRIDGE_HASH|; s|@@ADMIN_HASH@@|$ADMIN_HASH|" \
    "$SRC_DIR/config/credentials.xml.template" > "$RB/conf/credentials.xml"
chmod 600 "$RB/conf/credentials.xml"

# ---------------------------------------------------------------- TLS (optional)
TLS_XML=""
KS="$BASE/tls/keystore.p12"
if [ -n "${DOMAIN:-}" ]; then
    say "TLS certificate for $DOMAIN (Let's Encrypt, port 80 must reach this server)"
    ufw allow 80/tcp >/dev/null 2>&1 || true
    if [ ! -d "/etc/letsencrypt/live/$DOMAIN" ]; then
        if [ -n "${EMAIL:-}" ]; then mail_opt=(-m "$EMAIL"); else mail_opt=(--register-unsafely-without-email); fi
        certbot certonly --standalone --non-interactive --agree-tos "${mail_opt[@]}" -d "$DOMAIN" \
            || die "certbot failed: does $DOMAIN point to this server's IP, and is port 80 open in your cloud firewall?"
    fi
    KS_PASS="$(cat "$ETC/keystore.pass" 2>/dev/null || true)"
    [ -n "$KS_PASS" ] || { KS_PASS="$(rand 24)"; echo "$KS_PASS" > "$ETC/keystore.pass"; chmod 600 "$ETC/keystore.pass"; }
    mkdir -p "$BASE/tls"
    cat > /etc/letsencrypt/renewal-hooks/deploy/mp-hivemq.sh <<EOF
#!/bin/sh
# rebuild HiveMQ's keystore after every renewal, then restart the broker (clients reconnect by themselves)
openssl pkcs12 -export -in /etc/letsencrypt/live/$DOMAIN/fullchain.pem -inkey /etc/letsencrypt/live/$DOMAIN/privkey.pem \\
    -out $KS -name hivemq -passout pass:$KS_PASS
chown mp:mp $KS; chmod 600 $KS
systemctl restart hivemq
EOF
    chmod 700 /etc/letsencrypt/renewal-hooks/deploy/mp-hivemq.sh
    openssl pkcs12 -export -in "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" -inkey "/etc/letsencrypt/live/$DOMAIN/privkey.pem" \
        -out "$KS" -name hivemq -passout "pass:$KS_PASS"
    tls_keystore="            <tls>
                <keystore>
                    <path>$KS</path>
                    <password>$KS_PASS</password>
                    <private-key-password>$KS_PASS</private-key-password>
                </keystore>
                <protocols><protocol>TLSv1.3</protocol><protocol>TLSv1.2</protocol></protocols>
                <client-authentication-mode>NONE</client-authentication-mode>
            </tls>"
    TLS_XML="        <tls-tcp-listener>
            <port>8883</port>
            <bind-address>0.0.0.0</bind-address>
            <name>mqtts</name>
$tls_keystore
        </tls-tcp-listener>
        <tls-websocket-listener>
            <port>8884</port>
            <bind-address>0.0.0.0</bind-address>
            <path>/mqtt</path>
            <name>wss</name>
            <subprotocols><subprotocol>mqttv3.1</subprotocol><subprotocol>mqtt</subprotocol></subprotocols>
            <allow-extensions>true</allow-extensions>
$tls_keystore
        </tls-websocket-listener>"
fi

# ---------------------------------------------------------------- HiveMQ config
say "writing HiveMQ config"
TLS_XML="$TLS_XML" python3 - "$SRC_DIR/config/config.xml.template" "$HQ/conf/config.xml" "${MAX_CONNECTIONS:-20000}" "${BANDWIDTH_PER_CLIENT:-524288}" <<'PY'
import os, sys
src, dst, maxc, bw = sys.argv[1:5]
tls = os.environ.get("TLS_XML", "")
s = open(src).read().replace("@@TLS_LISTENERS@@", tls).replace("@@MAX_CONNECTIONS@@", maxc).replace("@@BANDWIDTH@@", bw)
open(dst, "w").write(s)
PY
chown -R mp:mp "$BASE"
chmod 600 "$HQ/conf/config.xml"

# Java heap: the 2 GB / 4 GB you asked for when the machine has room, else ~60 % of RAM
if [ -z "${JVM_XMX:-}" ]; then
    if [ "$RAM_MB" -ge 6000 ]; then JVM_XMS=2g; JVM_XMX=4g
    else x=$(( RAM_MB * 60 / 100 )); JVM_XMX="${x}m"; JVM_XMS="$(( x < 2048 ? x : 2048 ))m"; fi
fi
JVM_XMS="${JVM_XMS:-$JVM_XMX}"
say "Java heap: -Xms$JVM_XMS -Xmx$JVM_XMX"

# ---------------------------------------------------------------- mp_server (default server)
SV="$BASE/server"
if [ "${RUN_SERVER:-1}" = 1 ]; then
    say "building mp_server (the region's 24/7 default server)"
    rm -rf "$BASE/src"; git clone -q --depth 1 "$MOD_REPO" "$BASE/src"
    make -s -C "$BASE/src/server" -j"$CPUS" >/dev/null
    mkdir -p "$SV"; cp "$BASE/src/server/mp_server" "$SV/"
    cat > "$SV/broker.txt" <<EOF
# mp_server uses this server's own broker (generated by install.sh)
[$REGION]
host=127.0.0.1
port=1883
tls=0
user=$MP_USER
pass=$MP_PASS
public=0
regions_url=off
EOF
    # 3.04.6: the "UPDATE MP" notice server hosts in the old lobby (thirtyfox/mp4/): the public mp login
    # can only read there, so it uses the bridge login (secret, stays on this server)
    cat > "$SV/broker-legacy.txt" <<EOF
# mp_server --legacy-notice (old lobby, MP 3.04.5 and older) - generated by install.sh
[$REGION]
host=127.0.0.1
port=1883
tls=0
user=bridge
pass=$BRIDGE_PASS
public=0
regions_url=off
EOF
    chmod 600 "$SV/broker.txt" "$SV/broker-legacy.txt"; chown -R mp:mp "$SV"
fi

# ---------------------------------------------------------------- bridge
if [ "${RUN_BRIDGE:-0}" = 1 ]; then
    say "setting up MpBridge (server lists of all regions)"
    mkdir -p "$BASE/bridge"; cp "$SRC_DIR/bridge/MpBridge.java" "$BASE/bridge/"
    {
        echo "# generated by install.sh from HOST_* in $ENV_FILE"
        for r in Asia Europe NA Oceania; do
            v="HOST_${r^^}"; h="${!v:-}"
            [ "$r" = "$REGION" ] && h=127.0.0.1
            [ -n "$h" ] && echo "$r = mqtt://bridge:$BRIDGE_PASS@$h:1883"
        done
        echo "expire = 45"
    } > "$ETC/bridge.conf"
    chown mp:mp "$ETC/bridge.conf"; chmod 600 "$ETC/bridge.conf"
    [ "$(grep -c '= mqtt' "$ETC/bridge.conf")" -ge 2 ] || warn "bridge.conf has fewer than 2 brokers: fill HOST_* in $ENV_FILE and run install.sh again"
fi
chown mp:mp "$ETC" "$ENV_FILE" 2>/dev/null || true

# ---------------------------------------------------------------- 2.21: web console + status beacon
TTYD_VER="1.7.7"
CON="$BASE/console"; mkdir -p "$CON"
cp "$SRC_DIR/tools/mp-console.sh" "$SRC_DIR/tools/status.sh" "$SRC_DIR/tools/mp-status.py" "$CON/"; chmod 755 "$CON"/*.sh "$CON"/*.py
# keep a copy of this kit for the console's "update" choice
if [ "$SRC_DIR" != "$BASE/kit" ]; then rm -rf "$BASE/kit"; cp -a "$SRC_DIR" "$BASE/kit"; fi
if [ "${CONSOLE:-1}" = 1 ]; then
    case "$ARCH" in
        x86_64|amd64)  TT=ttyd.x86_64;  TT_SHA=8a217c968aba172e0dbf3f34447218dc015bc4d5e59bf51db2f2cd12b7be4f55;;
        aarch64|arm64) TT=ttyd.aarch64; TT_SHA=b38acadd89d1d396a0f5649aa52c539edbad07f4bc7348b27b4f4b7219dd4165;;
    esac
    if ! echo "$TT_SHA  $CON/ttyd" | sha256sum -c - >/dev/null 2>&1; then
        say "installing web console (ttyd $TTYD_VER)"
        curl -fL --retry 3 -o "$CON/ttyd" "https://github.com/tsl0922/ttyd/releases/download/$TTYD_VER/$TT"
        echo "$TT_SHA  $CON/ttyd" | sha256sum -c - >/dev/null || die "ttyd download checksum mismatch"
        chmod 755 "$CON/ttyd"
    fi
    CON_SSL=""; CON_BIND="-i lo"
    if [ -n "${DOMAIN:-}" ]; then
        CON_SSL="-S -C /etc/letsencrypt/live/$DOMAIN/fullchain.pem -K /etc/letsencrypt/live/$DOMAIN/privkey.pem"; CON_BIND=""
    elif [ "${CONSOLE_INSECURE:-0}" = 1 ]; then CON_BIND=""; warn "web console without TLS (CONSOLE_INSECURE=1): the password travels in clear text"
    else warn "web console only on localhost: set a DOMAIN for https://DOMAIN:7681 (or CONSOLE_INSECURE=1)"; fi
    sed -e "s|@@BASE@@|$BASE|g; s|@@BIND@@|$CON_BIND|; s|@@CRED@@|${CONSOLE_USER:-admin}:$CONSOLE_PASS|; s|@@SSL@@|$CON_SSL|" "$SRC_DIR/systemd/mp-console.service" > /etc/systemd/system/mp-console.service
    chmod 600 /etc/systemd/system/mp-console.service
    HOOK=/etc/letsencrypt/renewal-hooks/deploy/mp-hivemq.sh
    if [ -f "$HOOK" ] && ! grep -q mp-console "$HOOK"; then echo "systemctl restart mp-console" >> "$HOOK"; fi
fi
sed -e "s|@@BASE@@|$BASE|g" "$SRC_DIR/systemd/mp-status.service" > /etc/systemd/system/mp-status.service

# ---------------------------------------------------------------- systemd
say "installing services"
sed -e "s|@@JAVA_HOME@@|$JAVA_HOME|g; s|@@BASE@@|$BASE|g; s|@@XMS@@|$JVM_XMS|; s|@@XMX@@|$JVM_XMX|" "$SRC_DIR/systemd/hivemq.service" > /etc/systemd/system/hivemq.service
sed -e "s|@@BASE@@|$BASE|g; s|@@PORT@@|${SERVER_PORT:-1922}|; s|@@NAME@@|${SERVER_NAME:-MP LAN Server}|; s|@@MAX@@|${MAX_PLAYERS:-100}|; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-server.service" > /etc/systemd/system/mp-server.service
sed -e "s|@@JAVA_HOME@@|$JAVA_HOME|g; s|@@BASE@@|$BASE|g; s|@@ETC@@|$ETC|g" "$SRC_DIR/systemd/mp-bridge.service" > /etc/systemd/system/mp-bridge.service
# 3.04: Brief Case War 24/7 (UDP 1930)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|; s|@@BCWMIN@@|${BCW_MIN:-20}|" "$SRC_DIR/systemd/mp-bcw.service" > /etc/systemd/system/mp-bcw.service
# 3.04.6: "UPDATE MP" notice server in the old lobby (refuses + logs MP 3.04.5 and older)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-legacy.service" > /etc/systemd/system/mp-legacy.service
systemctl daemon-reload
systemctl enable --now hivemq >/dev/null
systemctl restart hivemq
if [ "${RUN_SERVER:-1}" = 1 ]; then systemctl enable mp-server >/dev/null; systemctl restart mp-server; else systemctl disable --now mp-server 2>/dev/null || true; fi
if [ "${RUN_SERVER:-1}" = 1 ] && [ "${RUN_BCW:-1}" = 1 ]; then systemctl enable mp-bcw >/dev/null; systemctl restart mp-bcw; else systemctl disable --now mp-bcw 2>/dev/null || true; fi   # 3.04
if [ "${RUN_SERVER:-1}" = 1 ] && [ "${RUN_LEGACY:-1}" = 1 ]; then systemctl enable mp-legacy >/dev/null; systemctl restart mp-legacy; else systemctl disable --now mp-legacy 2>/dev/null || true; fi   # 3.04.6
if [ "${RUN_BRIDGE:-0}" = 1 ]; then systemctl enable mp-bridge >/dev/null; systemctl restart mp-bridge; else systemctl disable --now mp-bridge 2>/dev/null || true; fi
systemctl enable mp-status >/dev/null; systemctl restart mp-status
if [ "${CONSOLE:-1}" = 1 ]; then systemctl enable mp-console >/dev/null; systemctl restart mp-console; else systemctl disable --now mp-console 2>/dev/null || true; fi

# ---------------------------------------------------------------- network tuning + firewall
cp "$SRC_DIR/config/99-mp-network.conf" /etc/sysctl.d/99-mp-network.conf
sysctl -q --system || true
say "firewall: SSH, MQTT 1883, WebSocket 8000${DOMAIN:+, TLS 8883/8884}, game UDP ${SERVER_PORT:-1922} + 1930 (Brief Case War)"
ufw allow OpenSSH >/dev/null 2>&1 || ufw allow 22/tcp >/dev/null
ufw allow 1883/tcp >/dev/null; ufw allow 8000/tcp >/dev/null
[ -n "${DOMAIN:-}" ] && { ufw allow 8883/tcp >/dev/null; ufw allow 8884/tcp >/dev/null; }
if [ "${CONSOLE:-1}" = 1 ] && { [ -n "${DOMAIN:-}" ] || [ "${CONSOLE_INSECURE:-0}" = 1 ]; }; then ufw allow 7681/tcp >/dev/null; fi
ufw allow "${SERVER_PORT:-1922}"/udp >/dev/null
ufw allow 1930/udp >/dev/null   # 3.04: Brief Case War
ufw deny 9010/tcp >/dev/null 2>&1 || true    # JMX (also switched off in the service)
ufw --force enable >/dev/null

# ---------------------------------------------------------------- done
sleep 6
systemctl is-active --quiet hivemq && say "HiveMQ is running" || warn "HiveMQ did not start: journalctl -u hivemq -n 50"
echo
say "Done. This region: $REGION"
echo "    status:  sudo $SRC_DIR/tools/status.sh     menu: sudo $BASE/console/mp-console.sh"
if [ "${CONSOLE:-1}" = 1 ] && [ -n "${DOMAIN:-}" ]; then echo "    web console (website Termius page): https://$DOMAIN:7681  login ${CONSOLE_USER:-admin} / CONSOLE_PASS in $ENV_FILE"; fi
echo "    logs:    journalctl -u hivemq -f    |  journalctl -u mp-server -f   |  journalctl -u mp-bcw -f   |  journalctl -u mp-legacy -f (old versions refused: $SV/outdated_clients.log)   |  journalctl -u mp-bridge -f"
echo "    logins:  $ENV_FILE  (copy to the other regions; keep BRIDGE_PASS / ADMIN_PASS secret)"
echo "    open these ports in your cloud provider's firewall too: 1883/tcp 8000/tcp ${DOMAIN:+8883/tcp 8884/tcp 7681/tcp 80/tcp }${SERVER_PORT:-1922}/udp"
echo "    then put this server into the website's regions.txt:  sudo $SRC_DIR/tools/make-regions.sh"
