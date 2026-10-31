#!/usr/bin/env bash
# MP Multiplayer - the 4 servers with their 24/7 bot host ("Broker") on an Android phone (Termux)
# or on ANY Linux machine (a PC, Termius / SSH to a box, Ubuntu on a phone). No cloud server, no card.
#   MP LAN Server (1922)  Brief Case War (1930)  Call Of Duty: Modern Warfare 3 (1931)  Mega Vil Futuristic (1932)
# A player who leaves never takes the server down: the bot host stays (3.38).
#
#   curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/install.sh | bash
#
# Builds mp_server from the Sa-mp source on the phone, then runs it in the background:
#   - listed in the internet lobby through the free public MQTT brokers (no port forwarding needed)
#   - restarts by itself after a crash, keeps the phone awake (termux-wake-lock)
#   - starts again after a reboot if the Termux:Boot app is installed
# Afterwards:  mp-server status | log | restart | stop | start | config | update
set -e

SITE="https://thirtyfoxmcx00e1.github.io/wagu"   # 3.35: the server source comes from the website (downloads/mp-server-src.tar.gz)
HOME_DIR="$HOME/mp-server"
SRC="$HOME_DIR/src"
CONF="$HOME_DIR/server.conf"

say()  { printf '\033[1;36m[mp]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[mp]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[mp]\033[0m %s\n' "$*" >&2; exit 1; }

# 3.38: Termux or plain Linux
TERMUX=0; case "${PREFIX:-}" in *com.termux*) TERMUX=1;; esac
if [ "$TERMUX" = 1 ]; then
    BIN="$PREFIX/bin"
    say "installing packages (clang, make)"
    # </dev/null everywhere: with "curl ... | bash" the script itself is on stdin
    pkg update -y -o Dpkg::Options::=--force-confnew </dev/null >/dev/null 2>&1 || true
    pkg install -y -o Dpkg::Options::=--force-confnew curl tar clang make binutils </dev/null >/dev/null 2>&1 \
        || pkg install -y curl tar clang make binutils </dev/null || die "pkg install failed (check the internet, then run again)"
else
    BIN="$HOME/.local/bin"; mkdir -p "$BIN"
    if ! { command -v make >/dev/null && { command -v clang++ >/dev/null || command -v g++ >/dev/null; }; }; then
        say "installing packages (g++, make)"
        SUDO=; [ "$(id -u)" = 0 ] || SUDO=sudo
        if command -v apt-get >/dev/null; then $SUDO apt-get update -qq </dev/null >/dev/null 2>&1; $SUDO apt-get install -y -qq curl tar g++ make </dev/null >/dev/null 2>&1
        elif command -v dnf >/dev/null; then $SUDO dnf install -y curl tar gcc-c++ make </dev/null >/dev/null 2>&1
        elif command -v apk >/dev/null; then $SUDO apk add --no-cache curl tar g++ make bash </dev/null >/dev/null 2>&1
        fi
        command -v make >/dev/null || die "install g++ and make, then run again"
    fi
fi
CXX_BIN="$(command -v clang++ || command -v g++)"; CC_BIN="$(command -v clang || command -v gcc)"
BASH_BIN="$(command -v bash)"

mkdir -p "$HOME_DIR"
say "downloading the server source ($SITE)"
curl -fsSL "$SITE/downloads/mp-server-src.tar.gz" -o "$HOME_DIR/src.tar.gz" </dev/null || die "download failed (check the internet, then run again)"
rm -rf "$SRC.new"; mkdir -p "$SRC.new"
tar --no-same-owner -xzf "$HOME_DIR/src.tar.gz" -C "$SRC.new" || die "the source download is broken - run again"
rm -rf "$SRC"; mv "$SRC.new" "$SRC"; rm -f "$HOME_DIR/src.tar.gz"

say "building mp_server (first time takes a few minutes)"
AR_BIN="$(command -v llvm-ar || command -v ar)"
make -s -C "$SRC/server" -j"$(nproc 2>/dev/null || echo 4)" CXX="$CXX_BIN" CC="$CC_BIN" AR="$AR_BIN" </dev/null >/dev/null 2>&1 || die "build failed"
# 3.21: cp + mv - a plain cp onto a running mp_server fails ("Text file busy") and the update stopped
cp "$SRC/server/mp_server" "$HOME_DIR/mp_server.new"
chmod 755 "$HOME_DIR/mp_server.new"
mv -f "$HOME_DIR/mp_server.new" "$HOME_DIR/mp_server"

if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<'EOF'
# MP 24/7 server settings (mp-server config to edit, mp-server restart to apply)
NAME="MP LAN Server"
# 1922 = default server port. If you also play/host on this same phone, use 1923.
PORT=1922
MAX_PLAYERS=100
# Asia, Europe, NA or Oceania (the region shown in the server list)
REGION=Asia
# optional join password (empty = open server)
PASSWORD=
# server icon in everyone's list: 0 = default (main_df_icon.png), 1..250 = icon_N.png (2.24)
ICON=0
# server language in the list, e.g. English or "Filipino / English" (2.25)
SERVER_LANG=English
# administrator name shown to kicked / banned players (2.25)
ADMIN=Server
EOF
fi
# 3.38: the other 3 featured servers, each with the 24/7 bot host (added to old server.conf files too)
grep -q '^RUN_BCW=' "$CONF" || cat >> "$CONF" <<'EOF'

# 3.38: also host these 24/7 on this device (1 = on, 0 = off). The bot "Broker" hosts each one,
# so nobody is dropped when a player leaves.
RUN_BCW=1
RUN_MW3=1
RUN_RP=1
EOF

# ---- runner: one per server (main | bcw | mw3 | rp), restarts it forever, keeps a short log ----
cat > "$HOME_DIR/run.sh" <<EOF
#!$BASH_BIN
EOF
cat >> "$HOME_DIR/run.sh" <<'EOF'
cd "$HOME/mp-server" || exit 1
command -v termux-wake-lock >/dev/null && termux-wake-lock 2>/dev/null
W="${1:-main}"
echo $$ > "run-$W.pid"
while true; do
    . ./server.conf
    case "$W" in
        bcw) args=(1930 "Brief Case War" 20 "$REGION" --persistent); LOG=server-bcw.log ;;
        mw3) args=(1931 "Call Of Duty: Modern Warfare 3" 64 "$REGION" --persistent --mw3-file mw3_stats.txt); LOG=server-mw3.log ;;
        rp)  args=(1932 "Mega Vil Futuristic" 250 "$REGION" --persistent --rp-file rp_citizens.txt); LOG=server-rp.log ;;
        *)   args=("$PORT" "$NAME" "$MAX_PLAYERS" "$REGION" --persistent); LOG=server.log
             [ -n "$PASSWORD" ] && args+=(--password "$PASSWORD")
             [ -n "$ICON" ] && [ "$ICON" != 0 ] && args+=(--icon "$ICON")   # 2.24
             [ -n "$SERVER_LANG" ] && args+=(--lang "$SERVER_LANG") ;;      # 2.25
    esac
    [ -n "$ADMIN" ] && args+=(--admin "$ADMIN" --bans bans.txt)     # 2.25
    [ -f broker.txt ] && args+=(--broker-file broker.txt)
    [ -f "$LOG" ] && [ "$(stat -c %s "$LOG" 2>/dev/null || echo 0)" -gt 2000000 ] && mv -f "$LOG" "${LOG%.log}.old.log"
    echo "==== $(date '+%F %T') starting mp_server ${args[*]}" >> "$LOG"
    ./mp_server "${args[@]}" < /dev/null >> "$LOG" 2>&1
    echo "==== $(date '+%F %T') mp_server stopped (code $?), restarting in 3 s" >> "$LOG"
    [ -f stop.flag ] && { rm -f "run-$W.pid"; exit 0; }
    sleep 3
done
EOF
chmod 755 "$HOME_DIR/run.sh"

# ---- mp-server command ----
cat > "$BIN/mp-server" <<EOF
#!$BASH_BIN
EOF
cat >> "$BIN/mp-server" <<'EOF'
D="$HOME/mp-server"
WHICH() { . "$D/server.conf"; echo main; [ "${RUN_BCW:-1}" = 1 ] && echo bcw; [ "${RUN_MW3:-1}" = 1 ] && echo mw3; [ "${RUN_RP:-1}" = 1 ] && echo rp; }
up()      { [ -f "$D/run-$1.pid" ] && kill -0 "$(cat "$D/run-$1.pid")" 2>/dev/null; }
start() {
    rm -f "$D/stop.flag"
    [ -f "$D/run.pid" ] && kill "$(cat "$D/run.pid")" 2>/dev/null && pkill -f "$D/mp_server" 2>/dev/null; rm -f "$D/run.pid"   # pre-3.38 runner
    command -v termux-wake-lock >/dev/null && termux-wake-lock 2>/dev/null
    for w in $(WHICH); do up "$w" || { nohup "$D/run.sh" "$w" </dev/null >/dev/null 2>&1 & }; done
    sleep 2; n=0; for w in $(WHICH); do up "$w" && n=$((n+1)); done
    echo "MP 24/7 bot host: $n of $(WHICH | wc -l) servers running. mp-server status / mp-server log to watch."
}
stop() {
    touch "$D/stop.flag"
    for f in "$D"/run-*.pid "$D/run.pid"; do [ -f "$f" ] && kill "$(cat "$f")" 2>/dev/null; rm -f "$f"; done
    pkill -f "mp_server 19[0-9][0-9] " 2>/dev/null
    sleep 1; rm -f "$D/stop.flag"; command -v termux-wake-unlock >/dev/null && termux-wake-unlock 2>/dev/null; echo "stopped"
}
case "$1" in
    start) start ;;
    stop) stop ;;
    restart) stop; sleep 1; start ;;
    log) case "$2" in bcw|mw3|rp) tail -n 40 -f "$D/server-$2.log" ;; *) tail -n 40 -f "$D/server.log" ;; esac ;;
    config) ${EDITOR:-nano} "$D/server.conf" 2>/dev/null || vi "$D/server.conf"; echo "mp-server restart to apply" ;;
    update) curl -fsSL https://thirtyfoxmcx00e1.github.io/wagu/install.sh | bash ;;
    vps-update)
        # 3.23.5: update the 24/7 VPS (HiveMQ kit + mp_server + MW3/BCW) from the phone over SSH.
        # The VPS autopilot also does it by itself within 10 min of a new release.
        UPD='curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash'
        H="$2"; [ -z "$H" ] && [ -f "$D/vps.txt" ] && H="$(cat "$D/vps.txt")"
        if [ -z "$H" ]; then echo "mp-server vps-update user@host   (for example ubuntu@mpsamp.duckdns.org; saved for next time)"; echo "or on the VPS: $UPD"; exit 1; fi
        echo "$H" > "$D/vps.txt"
        command -v ssh >/dev/null || pkg install -y openssh </dev/null >/dev/null 2>&1 || { echo "pkg install openssh failed"; exit 1; }
        echo "running on $H: $UPD"
        ssh -t -o ServerAliveInterval=20 "$H" "$UPD" ;;
    status|"")
        . "$D/server.conf"
        echo "MP 24/7 bot host (Broker) - region $REGION"
        for w in $(WHICH); do
            case "$w" in main) n="$NAME (port $PORT)";; bcw) n="Brief Case War (1930)";; mw3) n="Call Of Duty: Modern Warfare 3 (1931)";; rp) n="Mega Vil Futuristic (1932)";; esac
            if up "$w"; then printf '  \033[1;32monline \033[0m %s\n' "$n"; else printf '  \033[1;31moffline\033[0m %s\n' "$n"; fi
        done
        grep -hE "connected to|listed|joined|left" "$D"/server*.log 2>/dev/null | tail -n 5 ;;
    *) echo "mp-server start|stop|restart|status|log [bcw|mw3|rp]|config|update|vps-update [user@host]" ;;
esac
EOF
chmod 755 "$BIN/mp-server"

# ---- start on boot ----
if [ "$TERMUX" = 1 ]; then   # needs the Termux:Boot app, opened once
    mkdir -p "$HOME/.termux/boot"
    cat > "$HOME/.termux/boot/mp-server.sh" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
termux-wake-lock
sleep 20
mp-server start
EOF
    chmod 755 "$HOME/.termux/boot/mp-server.sh"
elif command -v crontab >/dev/null; then   # 3.38: Linux: start after a reboot, and check every 5 min
    ( crontab -l 2>/dev/null | grep -v 'mp-server start'; echo "@reboot sleep 20; $BIN/mp-server start >/dev/null 2>&1"; echo "*/5 * * * * $BIN/mp-server start >/dev/null 2>&1" ) | crontab - 2>/dev/null || true
fi

"$BIN/mp-server" stop </dev/null >/dev/null 2>&1 || true
"$BIN/mp-server" start </dev/null   # 3.21: an update must restart the running (old) server
echo
say "Done. The 4 MP servers run 24/7 on this device, hosted by the bot \"Broker\":"
echo "    MP LAN Server · Brief Case War · Call Of Duty: Modern Warfare 3 · Mega Vil Futuristic"
echo "    mp-server status    mp-server log [bcw|mw3|rp]    mp-server config    mp-server restart    mp-server update"
[ "$TERMUX" = 1 ] || case ":$PATH:" in *":$BIN:"*) ;; *) warn "add $BIN to PATH (or run $BIN/mp-server)";; esac
echo
if [ "$TERMUX" = 1 ]; then
    warn "Keep it alive:"
    echo "    1. Settings > Apps > Termux > Battery > Unrestricted (no battery optimization)"
    echo "    2. Keep the phone charging and on Wi-Fi"
    echo "    3. Install Termux:Boot (same store as Termux) and open it once -> starts after a reboot"
    echo "    4. Android 12+: Developer options > 'Disable child process restrictions' (if it exists)"
else
    warn "Keep this machine on and online. It starts again after a reboot and is checked every 5 min."
fi
