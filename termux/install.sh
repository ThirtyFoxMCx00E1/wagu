#!/data/data/com.termux/files/usr/bin/bash
# MP Multiplayer - 24/7 default server on an Android phone (Termux). No cloud server, no card.
#
#   curl -fsSL https://thirtyfoxmcx00e1.github.io/openmp.com/install.sh | bash
#
# Builds mp_server from the Sa-mp source on the phone, then runs it in the background:
#   - listed in the internet lobby through the free public MQTT brokers (no port forwarding needed)
#   - restarts by itself after a crash, keeps the phone awake (termux-wake-lock)
#   - starts again after a reboot if the Termux:Boot app is installed
# Afterwards:  mp-server status | log | restart | stop | start | config | update
set -e

SITE="https://thirtyfoxmcx00e1.github.io/openmp.com"   # 3.35: the server source comes from the website (downloads/mp-server-src.tar.gz)
HOME_DIR="$HOME/mp-server"
SRC="$HOME_DIR/src"
CONF="$HOME_DIR/server.conf"

say()  { printf '\033[1;36m[mp]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[mp]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[mp]\033[0m %s\n' "$*" >&2; exit 1; }

[ -n "$PREFIX" ] && [ -d "$PREFIX" ] || die "run this inside Termux (from F-Droid or GitHub, not the old Play Store version)"

say "installing packages (git, clang, make)"
# </dev/null everywhere: with "curl ... | bash" the script itself is on stdin
pkg update -y -o Dpkg::Options::=--force-confnew </dev/null >/dev/null 2>&1 || true
pkg install -y -o Dpkg::Options::=--force-confnew curl tar clang make binutils </dev/null >/dev/null 2>&1 \
    || pkg install -y curl tar clang make binutils </dev/null || die "pkg install failed (check the internet, then run again)"

mkdir -p "$HOME_DIR"
say "downloading the server source ($SITE)"
curl -fsSL "$SITE/downloads/mp-server-src.tar.gz" -o "$HOME_DIR/src.tar.gz" </dev/null || die "download failed (check the internet, then run again)"
rm -rf "$SRC.new"; mkdir -p "$SRC.new"
tar -xzf "$HOME_DIR/src.tar.gz" -C "$SRC.new" || die "the source download is broken - run again"
rm -rf "$SRC"; mv "$SRC.new" "$SRC"; rm -f "$HOME_DIR/src.tar.gz"

say "building mp_server (first time takes a few minutes)"
AR_BIN="$(command -v llvm-ar || command -v ar)"
make -s -C "$SRC/server" -j"$(nproc 2>/dev/null || echo 4)" CXX=clang++ CC=clang AR="$AR_BIN" </dev/null >/dev/null || die "build failed"
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

# ---- runner: restarts mp_server forever, keeps a short log ----
cat > "$HOME_DIR/run.sh" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
cd "$HOME/mp-server" || exit 1
termux-wake-lock 2>/dev/null || true
echo $$ > run.pid
while true; do
    . ./server.conf
    args=("$PORT" "$NAME" "$MAX_PLAYERS" "$REGION" --persistent)
    [ -n "$PASSWORD" ] && args+=(--password "$PASSWORD")
    [ -n "$ICON" ] && [ "$ICON" != 0 ] && args+=(--icon "$ICON")   # 2.24
    [ -n "$SERVER_LANG" ] && args+=(--lang "$SERVER_LANG")          # 2.25
    [ -n "$ADMIN" ] && args+=(--admin "$ADMIN" --bans bans.txt)     # 2.25
    [ -f broker.txt ] && args+=(--broker-file broker.txt)
    [ -f server.log ] && [ "$(stat -c %s server.log 2>/dev/null || echo 0)" -gt 2000000 ] && mv -f server.log server.old.log
    echo "==== $(date '+%F %T') starting mp_server ${args[*]}" >> server.log
    ./mp_server "${args[@]}" >> server.log 2>&1
    echo "==== $(date '+%F %T') mp_server stopped (code $?), restarting in 3 s" >> server.log
    [ -f stop.flag ] && { rm -f stop.flag run.pid; termux-wake-unlock 2>/dev/null; exit 0; }
    sleep 3
done
EOF
chmod 755 "$HOME_DIR/run.sh"

# ---- mp-server command ----
cat > "$PREFIX/bin/mp-server" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
D="$HOME/mp-server"
running() { [ -f "$D/run.pid" ] && kill -0 "$(cat "$D/run.pid")" 2>/dev/null; }
start() {
    if running; then echo "already running"; return; fi
    rm -f "$D/stop.flag"
    nohup "$D/run.sh" </dev/null >/dev/null 2>&1 &
    sleep 2; running && echo "MP server started (24/7). mp-server log to watch it." || echo "could not start, see mp-server log"
}
stop() {
    touch "$D/stop.flag"
    pkill -f "$D/mp_server" 2>/dev/null
    [ -f "$D/run.pid" ] && kill "$(cat "$D/run.pid")" 2>/dev/null
    rm -f "$D/run.pid" "$D/stop.flag"; termux-wake-unlock 2>/dev/null; echo "stopped"
}
case "$1" in
    start) start ;;
    stop) stop ;;
    restart) stop; sleep 1; start ;;
    log) tail -n 40 -f "$D/server.log" ;;
    config) ${EDITOR:-nano} "$D/server.conf" 2>/dev/null || vi "$D/server.conf"; echo "mp-server restart to apply" ;;
    update) curl -fsSL https://thirtyfoxmcx00e1.github.io/openmp.com/install.sh | bash ;;
    vps-update)
        # 3.23.5: update the 24/7 VPS (HiveMQ kit + mp_server + MW3/BCW) from the phone over SSH.
        # The VPS autopilot also does it by itself within 10 min of a new release.
        UPD='curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash'
        H="$2"; [ -z "$H" ] && [ -f "$D/vps.txt" ] && H="$(cat "$D/vps.txt")"
        if [ -z "$H" ]; then echo "mp-server vps-update user@host   (for example ubuntu@mpsamp.duckdns.org; saved for next time)"; echo "or on the VPS: $UPD"; exit 1; fi
        echo "$H" > "$D/vps.txt"
        command -v ssh >/dev/null || pkg install -y openssh </dev/null >/dev/null 2>&1 || { echo "pkg install openssh failed"; exit 1; }
        echo "running on $H: $UPD"
        ssh -t -o ServerAliveInterval=20 "$H" "$UPD" ;;
    status|"")
        . "$D/server.conf"
        if running; then printf '\033[1;32monline\033[0m'; else printf '\033[1;31moffline\033[0m'; fi
        echo "  $NAME  port $PORT  $MAX_PLAYERS slots  region $REGION"
        grep -E "connected to|listed|player|joined|left" "$D/server.log" 2>/dev/null | tail -n 5 ;;
    *) echo "mp-server start|stop|restart|status|log|config|update|vps-update [user@host]" ;;
esac
EOF
chmod 755 "$PREFIX/bin/mp-server"

# ---- start on boot (needs the Termux:Boot app, opened once) ----
mkdir -p "$HOME/.termux/boot"
cat > "$HOME/.termux/boot/mp-server.sh" <<'EOF'
#!/data/data/com.termux/files/usr/bin/bash
termux-wake-lock
sleep 20
mp-server start
EOF
chmod 755 "$HOME/.termux/boot/mp-server.sh"

mp-server stop </dev/null >/dev/null 2>&1 || true
mp-server restart </dev/null   # 3.21: an update must restart the running (old) server
echo
say "Done. Your 24/7 MP server runs on this phone."
echo "    mp-server status    mp-server log    mp-server config    mp-server restart    mp-server update"
echo
warn "Keep it alive:"
echo "    1. Settings > Apps > Termux > Battery > Unrestricted (no battery optimization)"
echo "    2. Keep the phone charging and on Wi-Fi"
echo "    3. Install Termux:Boot (same store as Termux) and open it once -> starts after a reboot"
echo "    4. Android 12+: Developer options > 'Disable child process restrictions' (if it exists)"
