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
#   - 3.21.7: MP Railway (our own Railway): the Discord login bot 24/7, auto deploy on git push
#   - 3.37 VPS: low-latency tuning (XDP game-port filter, conntrack bypass, NIC / CPU / THP tuning),
#     cache + log trim timer, and NO broker / server restart on an update unless something really changed
#     (players stay connected). The kit now lives in the wagu repo (HiveMQ/ folder) - one repo for everything.
set -euo pipefail

# 3.25: HiveMQ CE comes from our own fork first (ThirtyFoxMCx00E1/hivemq-community-edition, Apache 2.0,
# built by its "MP build" workflow into the release mp-<version>); the official release is the fallback.
# HIVEMQ_SOURCE=official in mp.env (or the environment) skips the fork.
HIVEMQ_FORK="ThirtyFoxMCx00E1/hivemq-community-edition"
HIVEMQ_FORK_VER="2026.6"
HIVEMQ_FORK_URL="https://github.com/${HIVEMQ_FORK}/releases/download/mp-${HIVEMQ_FORK_VER}/hivemq-ce-${HIVEMQ_FORK_VER}.zip"
HIVEMQ_VER="2026.5"
HIVEMQ_URL="https://github.com/hivemq/hivemq-community-edition/releases/download/${HIVEMQ_VER}/hivemq-ce-${HIVEMQ_VER}.zip"
# 3.25: prebuilt copy in the wagu v3.25 release (same file as the official 2026.5 zip, sha256 below);
# used before github.com/hivemq for the official build, the official link stays as the last fallback
HIVEMQ_MIRROR_URL="https://github.com/ThirtyFoxMCx00E1/wagu/releases/download/v3.25/hivemq-ce-${HIVEMQ_VER}.zip"
HIVEMQ_SHA256="fd005e535eca0baffb085e11b1512885d06ce00786f961701c76449816f7dec5"
RBAC_VER="4.6.18"
RBAC_URL="https://github.com/hivemq/hivemq-file-rbac-extension/releases/download/${RBAC_VER}/hivemq-file-rbac-extension-${RBAC_VER}.zip"
RBAC_SHA256="4089d09e5d2b6621a07aa86032230dce5aefbcc37130ef5c714fb6fa7185e3e6"
JDK_X64_URL="https://github.com/ThirtyFoxMCx00E1/wagu/releases/download/Assets-1-2/OpenJDK21U-jdk_x64_linux_hotspot_21.0.13_9.tar.gz"   # 3.10: wagu release "HiveMQ + OpenJDK Java 21"
JDK_X64_FALLBACK="https://api.adoptium.net/v3/binary/latest/21/ga/linux/x64/jdk/hotspot/normal/eclipse"
JDK_ARM_URL="https://api.adoptium.net/v3/binary/latest/21/ga/linux/aarch64/jdk/hotspot/normal/eclipse"
MOD_REPO="https://github.com/ThirtyFoxMCx00E1/wagu.git"
# 3.37 VPS: the kit is the HiveMQ/ folder of wagu (the separate HiveMQ repo is no longer used)
KIT_REPO="$MOD_REPO"

BASE=/opt/mp
ETC=/etc/mp-hivemq
ENV_FILE=$ETC/mp.env
SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

say()  { printf '\033[1;36m[mp]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[mp]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[mp]\033[0m %s\n' "$*" >&2; exit 1; }
rand() { tr -dc 'A-Za-z0-9' </dev/urandom | head -c "${1:-24}" || true; }
# 3.37 VPS: fast clone of only the folders we need from wagu (the repo has big release zips)
#   wagu_clone DEST folder...   (falls back to a normal shallow clone on old git)
wagu_clone() {
    local d=$1; shift; rm -rf "$d"
    if git clone -q --depth 1 --filter=blob:none --sparse "$MOD_REPO" "$d" </dev/null 2>/dev/null \
       && git -C "$d" sparse-checkout set "$@" </dev/null 2>/dev/null; then return 0; fi
    rm -rf "$d"; git clone -q --depth 1 "$MOD_REPO" "$d" </dev/null
}
sum() { cat "$@" 2>/dev/null | sha256sum | cut -c1-16; }   # content hash of files (missing = empty)
CHANGED=" "   # services whose files changed in this run (only those are restarted)

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

# Java heap. Empty = automatic: -Xmx16g with 26 GB+ RAM, -Xmx12g with 20 GB+, -Xms5g -Xmx10g with 14 GB RAM or more, -Xms4g -Xmx8g with 10 GB+ (3.25), -Xms2g -Xmx4g with 6 GB+, ~60 % of RAM below that.
# HiveMQ CE source: fork (default, our build) or official
HIVEMQ_SOURCE=fork
JVM_XMS=
JVM_XMX=
# limits per broker
MAX_CONNECTIONS=20000
# 3.33: 4 MB/s per client (was 512 KB/s: a busy relay host hit that limit, its packets queued up and every player's ping went to 200 ms+)
BANDWIDTH_PER_CLIENT=4194304
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
# 3.37 VPS: speed settings
grep -q '^XDP=' "$ENV_FILE" || cat >> "$ENV_FILE" <<EOF

# 3.37 VPS: XDP filter on the game UDP ports (drops floods / junk before Linux sees them). 0 = off
XDP=1
# packets per second per source IP before drops. High on purpose: many phones share one carrier IP (CGNAT)
XDP_PPS=20000
# extra IPs that are never limited (the HOST_* brokers are added by themselves)
XDP_ALLOW=
# game UDP skips connection tracking (faster, never "table full"). 0 = off
NOTRACK=1
# a running server is restarted by the keep-alive only after this many failed checks in a row (1 per minute)
KEEPALIVE_STRIKES=3
EOF
# shellcheck disable=SC1090
. "$ENV_FILE"
# 3.33: servers installed before 3.33 have the old 512 KB/s limit in mp.env: raise it (ping went high when a relay host got busy)
if [ "${BANDWIDTH_PER_CLIENT:-524288}" = 524288 ]; then
    BANDWIDTH_PER_CLIENT=4194304
    if grep -q '^BANDWIDTH_PER_CLIENT=' "$ENV_FILE"; then sed -i 's|^BANDWIDTH_PER_CLIENT=.*|BANDWIDTH_PER_CLIENT=4194304|' "$ENV_FILE"; else echo 'BANDWIDTH_PER_CLIENT=4194304' >> "$ENV_FILE"; fi
fi
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
[ "$RAM_MB" -ge 3500 ] && [ "$RAM_MB" -lt 10000 ] && warn "the 8 GB Java heap needs 10 GB+ RAM on this machine (you have $RAM_MB MB): using a smaller heap."
[ "$RAM_MB" -ge 10000 ] && [ "$RAM_MB" -lt 14000 ] && warn "the 10 GB Java heap needs 14 GB+ RAM on this machine (you have $RAM_MB MB): using the 8 GB heap."

# ---------------------------------------------------------------- packages
export DEBIAN_FRONTEND=noninteractive
say "installing packages"
apt-get update -qq
apt-get install -y -qq curl ca-certificates unzip tar openssl ufw git make g++ python3 ethtool iptables >/dev/null
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
# 3.37 VPS: fingerprint of everything the broker reads, BEFORE this run (restart only if it changes)
HQ_FILES=("$BASE/hivemq/.version" "$BASE/hivemq/conf/config.xml" "$BASE/hivemq/extensions/hivemq-file-rbac-extension/conf/config.xml" /etc/systemd/system/hivemq.service "$BASE/tls/keystore.p12")
HQ_BEFORE="$(sum "${HQ_FILES[@]}")"
HQ="$BASE/hivemq"
# 3.25: try the fork's build first; if its release isn't there (or HIVEMQ_SOURCE=official) use the official one
HQ_TAG="official-$HIVEMQ_VER"
if [ "${HIVEMQ_SOURCE:-fork}" != official ] && curl -fsIL --retry 2 -o /dev/null "$HIVEMQ_FORK_URL"; then
    HIVEMQ_VER="$HIVEMQ_FORK_VER"; HIVEMQ_URL="$HIVEMQ_FORK_URL"; HQ_TAG="fork-$HIVEMQ_FORK_VER"
fi
if [ ! -f "$HQ/bin/hivemq.jar" ] || [ "$(cat "$HQ/.version" 2>/dev/null)" != "$HQ_TAG" ]; then
    say "installing HiveMQ CE $HIVEMQ_VER (${HQ_TAG%%-*} build)"
    tmp="$(mktemp -d)"
    if [ "${HQ_TAG%%-*}" = fork ]; then
        curl -fL --retry 3 -o "$tmp/hq.zip" "$HIVEMQ_URL"
        if curl -fsL "$HIVEMQ_FORK_URL.sha256" -o "$tmp/hq.sha256" 2>/dev/null; then
            echo "$(cat "$tmp/hq.sha256")  $tmp/hq.zip" | sha256sum -c --quiet - || die "HiveMQ CE fork zip: checksum mismatch"
        fi
    else
        # official 2026.5: the wagu v3.25 copy first, then github.com/hivemq; both must match the sha256
        if ! { curl -fL --retry 3 -o "$tmp/hq.zip" "$HIVEMQ_MIRROR_URL" && echo "$HIVEMQ_SHA256  $tmp/hq.zip" | sha256sum -c --quiet -; }; then
            say "wagu copy of HiveMQ CE not usable, downloading from hivemq"
            curl -fL --retry 3 -o "$tmp/hq.zip" "$HIVEMQ_URL"
            echo "$HIVEMQ_SHA256  $tmp/hq.zip" | sha256sum -c --quiet - || die "HiveMQ CE $HIVEMQ_VER zip: checksum mismatch"
        fi
    fi
    unzip -q "$tmp/hq.zip" -d "$tmp"
    systemctl stop hivemq 2>/dev/null || true
    rm -rf "$HQ"; mv "$tmp/hivemq-ce-$HIVEMQ_VER" "$HQ"; rm -rf "$tmp"
    echo "$HQ_TAG" > "$HQ/.version"
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
# 3.37 VPS: same passwords as last time = keep the file (the hashes are salted, a rewrite looked like a change)
CRED_SUM="$(printf '%s\n' "$MP_USER" "$MP_PASS" "$WEB_USER" "$WEB_PASS" "$BRIDGE_PASS" "$ADMIN_PASS" | cat - "$SRC_DIR/config/credentials.xml.template" | sha256sum | cut -c1-16)"
if [ -s "$RB/conf/credentials.xml" ] && [ "$(cat "$ETC/.cred.sum" 2>/dev/null)" = "$CRED_SUM" ]; then
say "logins unchanged"
else
say "writing logins (hashed)"
MP_HASH="$(hash_pw "$MP_PASS")"; WEB_HASH="$(hash_pw "$WEB_PASS")"
BRIDGE_HASH="$(hash_pw "$BRIDGE_PASS")"; ADMIN_HASH="$(hash_pw "$ADMIN_PASS")"
[ -n "$MP_HASH" ] && [ -n "$ADMIN_HASH" ] || die "could not hash the passwords"
sed -e "s|@@MP_USER@@|$MP_USER|; s|@@WEB_USER@@|$WEB_USER|" \
    -e "s|@@MP_HASH@@|$MP_HASH|; s|@@WEB_HASH@@|$WEB_HASH|; s|@@BRIDGE_HASH@@|$BRIDGE_HASH|; s|@@ADMIN_HASH@@|$ADMIN_HASH|" \
    "$SRC_DIR/config/credentials.xml.template" > "$RB/conf/credentials.xml"
chmod 600 "$RB/conf/credentials.xml"
echo "$CRED_SUM" > "$ETC/.cred.sum"; chmod 600 "$ETC/.cred.sum"
fi   # 3.37 VPS (the RBAC extension re-reads credentials.xml by itself: no broker restart)

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
    # 3.37 VPS: rebuild the keystore only for a new certificate (a rebuild every run restarted the broker)
    if [ ! -s "$KS" ] || [ "$(readlink -f "/etc/letsencrypt/live/$DOMAIN/fullchain.pem")" -nt "$KS" ] || [ "$(cat "$ETC/.ks.pass.sum" 2>/dev/null)" != "$(printf %s "$KS_PASS" | sha256sum | cut -c1-16)" ]; then
        openssl pkcs12 -export -in "/etc/letsencrypt/live/$DOMAIN/fullchain.pem" -inkey "/etc/letsencrypt/live/$DOMAIN/privkey.pem" \
            -out "$KS" -name hivemq -passout "pass:$KS_PASS"
        printf %s "$KS_PASS" | sha256sum | cut -c1-16 > "$ETC/.ks.pass.sum"
    fi
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
TLS_XML="$TLS_XML" python3 - "$SRC_DIR/config/config.xml.template" "$HQ/conf/config.xml" "${MAX_CONNECTIONS:-20000}" "${BANDWIDTH_PER_CLIENT:-4194304}" <<'PY'
import os, sys
src, dst, maxc, bw = sys.argv[1:5]
tls = os.environ.get("TLS_XML", "")
s = open(src).read().replace("@@TLS_LISTENERS@@", tls).replace("@@MAX_CONNECTIONS@@", maxc).replace("@@BANDWIDTH@@", bw)
open(dst, "w").write(s)
PY
chown -R mp:mp "$BASE"
chmod 600 "$HQ/conf/config.xml"

# Java heap: 3.25: 16 GB (-Xms8g -Xmx16g) with 26 GB+ RAM, 12 GB (-Xms6g -Xmx12g) with 20 GB+,
# 10 GB (-Xms5g -Xmx10g) when the machine has 14 GB+ RAM, 8 GB (-Xms4g -Xmx8g) with
# 10 GB+, 4 GB (-Xms2g -Xmx4g) with 6 GB+, else ~60 % of RAM. The JVM needs ~1.5-2 GB on top of the heap
# (metaspace, threads, direct buffers) and Linux + the mp_server processes need ~2 GB more, so a 10 GB
# heap wants 14 GB of RAM. More heap = more queued messages / connections; it does NOT lower the ping.
if [ -z "${JVM_XMX:-}" ]; then
    if   [ "$RAM_MB" -ge 26000 ]; then JVM_XMS=8g; JVM_XMX=16g   # 3.25: 32 GB machines
    elif [ "$RAM_MB" -ge 20000 ]; then JVM_XMS=6g; JVM_XMX=12g   # 3.25: 24 GB machines
    elif [ "$RAM_MB" -ge 14000 ]; then JVM_XMS=5g; JVM_XMX=10g   # 16 GB machines
    elif [ "$RAM_MB" -ge 10000 ]; then JVM_XMS=4g; JVM_XMX=8g
    elif [ "$RAM_MB" -ge 6000 ];  then JVM_XMS=2g; JVM_XMX=4g
    else x=$(( RAM_MB * 60 / 100 )); JVM_XMX="${x}m"; JVM_XMS="$(( x < 2048 ? x : 2048 ))m"; fi
fi
JVM_XMS="${JVM_XMS:-$JVM_XMX}"
say "Java heap: -Xms$JVM_XMS -Xmx$JVM_XMX"

# ---------------------------------------------------------------- mp_server (default server)
SV="$BASE/server"
if [ "${RUN_SERVER:-1}" = 1 ]; then
    say "building mp_server (the region's 24/7 default server)"
    wagu_clone "$BASE/src" server src/net third_party/mbedtls   # 3.37 VPS: only the server source (fast)
    make -s -C "$BASE/src/server" -j"$CPUS" >/dev/null
    [ "$(sum "$BASE/src/server/mp_server")" != "$(sum "$SV/mp_server")" ] && SERVER_BIN_CHANGED=1
    # 3.21: replace the binary with cp + mv. A plain cp onto the running mp_server fails with "Text file
    # busy" and set -e then aborted the whole update: the 24/7 servers silently kept the OLD mp_server
    # (old admin key refused, no direct UDP -> relay only ~400 ms, phones hosting the default server).
    mkdir -p "$SV"; cp "$BASE/src/server/mp_server" "$SV/mp_server.new"; chmod 755 "$SV/mp_server.new"; mv -f "$SV/mp_server.new" "$SV/mp_server"
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
    mkdir -p "$BASE/bridge"; BR_BEFORE="$(sum "$BASE/bridge/MpBridge.java" "$ETC/bridge.conf")"; cp "$SRC_DIR/bridge/MpBridge.java" "$BASE/bridge/"
    {
        echo "# generated by install.sh from HOST_* in $ENV_FILE"
        for r in Asia Europe NA Oceania; do
            v="HOST_${r^^}"; h="${!v:-}"
            [ "$r" = "$REGION" ] && h=127.0.0.1
            [ -n "$h" ] && echo "$r = mqtt://bridge:$BRIDGE_PASS@$h:1883"
        done
        echo "expire = 45"
        # 3.34: game branch (phones stay on the nearest region, the game crosses regions VPS to VPS)
        echo "branch = ${GAME_BRANCH:-on}"
    } > "$ETC/bridge.conf"
    chown mp:mp "$ETC/bridge.conf"; chmod 600 "$ETC/bridge.conf"
    [ "$(sum "$BASE/bridge/MpBridge.java" "$ETC/bridge.conf")" != "$BR_BEFORE" ] && BRIDGE_CHANGED=1
    [ "$(grep -c '= mqtt' "$ETC/bridge.conf")" -ge 2 ] || warn "bridge.conf has fewer than 2 brokers: fill HOST_* in $ENV_FILE and run install.sh again"
fi
chown mp:mp "$ETC" "$ENV_FILE" 2>/dev/null || true

# ---------------------------------------------------------------- 2.21: web console + status beacon
TTYD_VER="1.7.7"
CON="$BASE/console"; mkdir -p "$CON"
PY_BEFORE="$(sum "$CON/mp-status.py")"
cp "$SRC_DIR/tools/mp-console.sh" "$SRC_DIR/tools/status.sh" "$SRC_DIR/tools/mp-status.py" "$SRC_DIR/tools/mp-keepalive.sh" \
   "$SRC_DIR/tools/mp-net-tune.sh" "$SRC_DIR/tools/mp-xdp.sh" "$SRC_DIR/tools/mp-trim.sh" "$CON/"; chmod 755 "$CON"/*.sh "$CON"/*.py
mkdir -p "$CON/../xdp"; cp "$SRC_DIR/xdp/mp_xdp.c" "$CON/../xdp/"   # 3.37 VPS
[ "$(sum "$CON/mp-status.py")" != "$PY_BEFORE" ] && CHANGED+="mp-status "
# keep a copy of this kit for the console's "update" choice
# 3.21: the kit is a git clone, so "7) update" (git pull) really gets the new version
if [ "$SRC_DIR" != "$BASE/kit" ]; then
    rm -rf "$BASE/kit"
    cp -a "$SRC_DIR" "$BASE/kit"   # 3.37 VPS: update.sh fetches the newest kit from wagu itself
fi
chmod 755 "$BASE/kit/update.sh" 2>/dev/null || true
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
    CON_BEFORE="$(sum /etc/systemd/system/mp-console.service "$CON/ttyd")"
    sed -e "s|@@BASE@@|$BASE|g; s|@@BIND@@|$CON_BIND|; s|@@CRED@@|${CONSOLE_USER:-admin}:$CONSOLE_PASS|; s|@@SSL@@|$CON_SSL|" "$SRC_DIR/systemd/mp-console.service" > /etc/systemd/system/mp-console.service
    chmod 600 /etc/systemd/system/mp-console.service
    [ "$(sum /etc/systemd/system/mp-console.service "$CON/ttyd")" != "$CON_BEFORE" ] && CHANGED+="mp-console "
    HOOK=/etc/letsencrypt/renewal-hooks/deploy/mp-hivemq.sh
    if [ -f "$HOOK" ] && ! grep -q mp-console "$HOOK"; then echo "systemctl restart mp-console" >> "$HOOK"; fi
fi
ST_BEFORE="$(sum /etc/systemd/system/mp-status.service)"
sed -e "s|@@BASE@@|$BASE|g" "$SRC_DIR/systemd/mp-status.service" > /etc/systemd/system/mp-status.service
[ "$(sum /etc/systemd/system/mp-status.service)" != "$ST_BEFORE" ] && CHANGED+="mp-status "

# ---------------------------------------------------------------- 3.21.7: MP Railway (our own Railway)
# Runs the MP Discord login bot (discordpy-bot) 24/7 with auto deploy. The tool is installed on every
# server, but the bot runs only where it was set up once (sudo mp-railway setup) - ONE server only,
# else two bots answer /gen and the accounts split.
if [ -f "$SRC_DIR/railway/mp-railway.sh" ]; then
    install -m 755 "$SRC_DIR/railway/mp-railway.sh" /usr/local/bin/mp-railway
    /usr/local/bin/mp-railway install >/dev/null || warn "MP Railway install failed (sudo mp-railway install)"
    if [ -s /etc/mp-railway/discordpy-bot.env ]; then
        say "MP Railway: deploying the Discord bot"
        /usr/local/bin/mp-railway deploy discordpy-bot || warn "Discord bot not online yet: sudo mp-railway logs"
    fi
fi

# ---------------------------------------------------------------- systemd
say "installing services"
sed -e "s|@@JAVA_HOME@@|$JAVA_HOME|g; s|@@BASE@@|$BASE|g; s|@@XMS@@|$JVM_XMS|; s|@@XMX@@|$JVM_XMX|" "$SRC_DIR/systemd/hivemq.service" > /etc/systemd/system/hivemq.service
[ "$(sum "${HQ_FILES[@]}")" != "$HQ_BEFORE" ] && CHANGED+="hivemq "
# 3.37 VPS: a game server is restarted only when its binary or its service file changed
for u in mp-server mp-bcw mp-mw3 mp-rp mp-legacy mp-bridge; do eval "U_BEFORE_${u//-/_}=\"\$(sum /etc/systemd/system/$u.service)\""; done
sed -e "s|@@BASE@@|$BASE|g; s|@@PORT@@|${SERVER_PORT:-1922}|; s|@@NAME@@|${SERVER_NAME:-MP LAN Server}|; s|@@MAX@@|${MAX_PLAYERS:-100}|; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-server.service" > /etc/systemd/system/mp-server.service
sed -e "s|@@JAVA_HOME@@|$JAVA_HOME|g; s|@@BASE@@|$BASE|g; s|@@ETC@@|$ETC|g" "$SRC_DIR/systemd/mp-bridge.service" > /etc/systemd/system/mp-bridge.service
# 3.04: Brief Case War 24/7 (UDP 1930)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|; s|@@BCWMIN@@|${BCW_MIN:-20}|" "$SRC_DIR/systemd/mp-bcw.service" > /etc/systemd/system/mp-bcw.service
# 3.23: Call Of Duty: Modern Warfare 3 24/7 (UDP 1931)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-mw3.service" > /etc/systemd/system/mp-mw3.service
# 3.25: Mega Vil Futuristic 24/7 Roleplay (UDP 1932, 250 players)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-rp.service" > /etc/systemd/system/mp-rp.service
# 3.04.6: "UPDATE MP" notice server in the old lobby (refuses + logs MP 3.04.5 and older)
sed -e "s|@@BASE@@|$BASE|g; s|@@REGION@@|$REGION|" "$SRC_DIR/systemd/mp-legacy.service" > /etc/systemd/system/mp-legacy.service
# 3.33: keep-alive: the broker and the 4 featured servers must be online 24/7 (checked every minute)
sed -e "s|@@BASE@@|$BASE|g" "$SRC_DIR/systemd/mp-keepalive.service" > /etc/systemd/system/mp-keepalive.service
cp "$SRC_DIR/systemd/mp-keepalive.timer" /etc/systemd/system/mp-keepalive.timer
# 3.37 VPS: tuning at boot, XDP filter, cache / log trim timer
for u in mp-net-tune.service mp-xdp.service mp-trim.service; do sed -e "s|@@BASE@@|$BASE|g" "$SRC_DIR/systemd/$u" > "/etc/systemd/system/$u"; done
cp "$SRC_DIR/systemd/mp-trim.timer" /etc/systemd/system/mp-trim.timer
for u in mp-server mp-bcw mp-mw3 mp-rp mp-legacy mp-bridge; do
    v="U_BEFORE_${u//-/_}"; [ "$(sum /etc/systemd/system/$u.service)" != "${!v}" ] && CHANGED+="$u "
done
[ "${SERVER_BIN_CHANGED:-0}" = 1 ] && CHANGED+="mp-server mp-bcw mp-mw3 mp-rp mp-legacy "
[ "${BRIDGE_CHANGED:-0}" = 1 ] && CHANGED+="mp-bridge "
[ "${MP_FORCE_RESTART:-0}" = 1 ] && CHANGED+="hivemq mp-server mp-bcw mp-mw3 mp-rp mp-legacy mp-bridge "
systemctl daemon-reload
# apply: start what is stopped, restart only what changed - players on an unchanged server stay connected
up() {   # unit  wanted(1/0)
    if [ "$2" != 1 ]; then systemctl disable --now "$1" 2>/dev/null || true; return 0; fi
    systemctl enable "$1" >/dev/null 2>&1
    if ! systemctl is-active --quiet "$1"; then systemctl start "$1" || true
    elif [[ "$CHANGED" == *" $1 "* ]]; then say "restarting $1 (updated)"; systemctl restart "$1" || true
    else say "$1 unchanged - kept running (no reconnect)"; fi
}
up hivemq 1
S=${RUN_SERVER:-1}
up mp-server "$S"
up mp-bcw "$([ "$S" = 1 ] && [ "${RUN_BCW:-1}" = 1 ] && echo 1 || echo 0)"        # 3.04
up mp-mw3 "$([ "$S" = 1 ] && [ "${RUN_MW3:-1}" = 1 ] && echo 1 || echo 0)"        # 3.23
up mp-rp "$([ "$S" = 1 ] && [ "${RUN_RP:-1}" = 1 ] && echo 1 || echo 0)"          # 3.25
up mp-legacy "$([ "$S" = 1 ] && [ "${RUN_LEGACY:-1}" = 1 ] && echo 1 || echo 0)"  # 3.04.6
up mp-bridge "${RUN_BRIDGE:-0}"
if [ "${XDP:-1}" = 1 ]; then systemctl enable mp-xdp.service >/dev/null 2>&1; systemctl restart mp-xdp.service || true
else systemctl disable --now mp-xdp.service 2>/dev/null || true; "$BASE/console/mp-xdp.sh" stop >/dev/null 2>&1 || true; fi
systemctl enable --now mp-trim.timer >/dev/null 2>&1 || true
up mp-status 1
# 3.37 VPS: the web console is only restarted when it changed (an update started from the console kept going)
up mp-console "${CONSOLE:-1}"
systemctl enable --now mp-keepalive.timer >/dev/null 2>&1 || true   # 3.33

# ---------------------------------------------------------------- network tuning + firewall
cp "$SRC_DIR/config/99-mp-network.conf" /etc/sysctl.d/99-mp-network.conf
sysctl -q --system || true
# 3.37 VPS: NIC / CPU / conntrack tuning now (and at every boot)
systemctl enable mp-net-tune.service >/dev/null 2>&1 || true
systemctl restart mp-net-tune.service 2>/dev/null || true
say "firewall: SSH, MQTT 1883, WebSocket 8000${DOMAIN:+, TLS 8883/8884}, game UDP ${SERVER_PORT:-1922} + 1930 (Brief Case War) + 1931 (MW3) + 1932 (Mega Vil RP)"
ufw allow OpenSSH >/dev/null 2>&1 || ufw allow 22/tcp >/dev/null
ufw allow 1883/tcp >/dev/null; ufw allow 8000/tcp >/dev/null
[ -n "${DOMAIN:-}" ] && { ufw allow 8883/tcp >/dev/null; ufw allow 8884/tcp >/dev/null; }
if [ "${CONSOLE:-1}" = 1 ] && { [ -n "${DOMAIN:-}" ] || [ "${CONSOLE_INSECURE:-0}" = 1 ]; }; then ufw allow 7681/tcp >/dev/null; fi
ufw allow "${SERVER_PORT:-1922}"/udp >/dev/null
ufw allow 1930/udp >/dev/null   # 3.04: Brief Case War
ufw allow 1931/udp >/dev/null   # 3.23: Modern Warfare 3
ufw allow 1932/udp >/dev/null   # 3.25: Mega Vil Futuristic
ufw deny 9010/tcp >/dev/null 2>&1 || true    # JMX (also switched off in the service)
ufw --force enable >/dev/null

# ---------------------------------------------------------------- done
sleep 6
systemctl is-active --quiet hivemq && say "HiveMQ is running" || warn "HiveMQ did not start: journalctl -u hivemq -n 50"
echo
say "Done. This region: $REGION   (restarted this run:${CHANGED})"
echo "    speed:   sudo $BASE/console/mp-xdp.sh status   (XDP flood filter)   |  XDP=0 / XDP_PPS= / NOTRACK=0 in $ENV_FILE"
echo "    status:  sudo $SRC_DIR/tools/status.sh     menu: sudo $BASE/console/mp-console.sh"
if [ "${CONSOLE:-1}" = 1 ] && [ -n "${DOMAIN:-}" ]; then echo "    web console (website Termius page): https://$DOMAIN:7681  login ${CONSOLE_USER:-admin} / CONSOLE_PASS in $ENV_FILE"; fi
echo "    logs:    journalctl -u hivemq -f    |  journalctl -u mp-server -f   |  journalctl -u mp-bcw -f   |  journalctl -u mp-mw3 -f   |  journalctl -u mp-rp -f   |  journalctl -u mp-legacy -f (old versions refused: $SV/outdated_clients.log)   |  journalctl -u mp-bridge -f"
echo "    logins:  $ENV_FILE  (copy to the other regions; keep BRIDGE_PASS / ADMIN_PASS secret)"
echo "    open these ports in your cloud provider's firewall too: 1883/tcp 8000/tcp ${DOMAIN:+8883/tcp 8884/tcp 7681/tcp 80/tcp }${SERVER_PORT:-1922}/udp 1930/udp 1931/udp 1932/udp"
echo "    (a closed UDP port makes that server run on the slow relay: ping 200 ms+. All four UDP ports must be open)"
echo "    then put this server into the website's regions.txt:  sudo $SRC_DIR/tools/make-regions.sh"
if [ -s /etc/mp-railway/discordpy-bot.env ]; then echo "    Discord bot (MP Railway): sudo mp-railway status   |  sudo mp-railway logs -f"
else echo "    Discord bot 24/7 on this server (ONE server only): sudo mp-railway setup   (console: r)"; fi
