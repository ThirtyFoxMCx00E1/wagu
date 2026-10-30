#!/usr/bin/env bash
# MP Multiplayer server console - the menu you get in the browser (website "Termius" page)
# or over SSH:  sudo /opt/mp/console/mp-console.sh
ENV_FILE=/etc/mp-hivemq/mp.env
HOLD=/etc/mp-hivemq/hold   # 3.33: a server stopped from this menu stays stopped (mp-keepalive skips it)
mkdir -p "$HOLD" 2>/dev/null
. "$ENV_FILE" 2>/dev/null || true
c() { printf '\033[%sm%s\033[0m' "$1" "$2"; }
line() { printf '%s\n' "------------------------------------------------------------"; }
svc() { local s; s=$(systemctl is-active "$1" 2>/dev/null); if [ -e "$HOLD/$1" ]; then c '1;33' "STOPPED"; elif [ "$s" = active ]; then c '1;32' "online "; else c '1;31' "${s:-off}"; fi; }
conns() { ss -tn state established '( sport = :1883 or sport = :8000 or sport = :8883 or sport = :8884 )' 2>/dev/null | tail -n +2 | wc -l; }
header() {
    clear
    c '1;36' "  MP Multiplayer server console"; printf '   region %s  %s\n' "${REGION:-?}" "${DOMAIN:-$(hostname -I 2>/dev/null | awk '{print $1}')}"
    line
    printf '  HiveMQ broker   %s   MQTT clients: %s\n' "$(svc hivemq)" "$(conns)"
    printf '  MP LAN Server   %s   UDP %s, %s slots\n' "$(svc mp-server)" "${SERVER_PORT:-1922}" "${MAX_PLAYERS:-100}"
    printf '  Brief Case War  %s   UDP 1930, 20 slots\n' "$(svc mp-bcw)"
    printf '  Modern Warfare 3 %s  UDP 1931, 64 slots\n' "$(svc mp-mw3)"
    printf '  Mega Vil Futuristic %s UDP 1932, 250 slots\n' "$(svc mp-rp)"
    printf '  keep-alive      %s   the 4 servers above run only here, 24/7 (no phone hosts them)\n' "$(svc mp-keepalive.timer)"
    printf '  old-MP notice   %s   %s old phones refused (o = list)\n' "$(svc mp-legacy)" "$(wc -l < /opt/mp/server/outdated_clients.log 2>/dev/null || echo 0)"
    printf '  region bridge   %s\n' "$(svc mp-bridge)"
    printf '  status beacon   %s\n' "$(svc mp-status)"
    printf '  Discord bot     %s   MP Railway (r = menu)\n' "$(svc mp-railway@discordpy-bot)"
    printf '  uptime          %s   load %s\n' "$(uptime -p 2>/dev/null)" "$(cut -d' ' -f1-3 /proc/loadavg)"
    printf '  memory          %s\n' "$(free -m | awk '/Mem:/ {printf "%d / %d MB used", $3, $2}')"
    line
}
# 3.33: the 4 featured servers, one by one. They run only on this machine (no phone hosts them) and
# mp-keepalive keeps them online 24/7; "stop" holds a server down until you start it again.
SRV_UNITS=(mp-server mp-bcw mp-mw3 mp-rp)
SRV_NAMES=("MP LAN Server (default, UDP ${SERVER_PORT:-1922})" "Brief Case War (UDP 1930)" "Modern Warfare 3 (UDP 1931)" "Mega Vil Futuristic (UDP 1932)")
servers_menu() {
    local i k n u
    while true; do
        clear; c '1;36' "  The 4 featured servers (24/7)"; echo; line
        for i in 0 1 2 3; do printf '  %d) %-46s %s\n' $((i + 1)) "${SRV_NAMES[$i]}" "$(svc "${SRV_UNITS[$i]}")"; done
        line
        echo "  choose a server 1-4, or  a) all four   0) back"
        read -r -p "  server: " n || return
        case "$n" in
            0|q|b) return ;;
            a|A) u="${SRV_UNITS[*]}" ;;
            1|2|3|4) u="${SRV_UNITS[$((n - 1))]}" ;;
            *) continue ;;
        esac
        echo "  l) live log   r) restart   t) start   s) stop (stays stopped)   0) back"
        read -r -p "  action: " k || return
        case "$k" in
            l|L) { [ "$n" = a ] || [ "$n" = A ]; } && { echo "pick one server for the log"; sleep 1; continue; }; echo "(Ctrl+C to go back)"; journalctl -u "$u" -f -n 40 ;;
            r|R) for x in $u; do rm -f "$HOLD/$x"; systemctl reset-failed "$x" 2>/dev/null; systemctl restart "$x"; done; echo restarted; sleep 2 ;;
            t|T) for x in $u; do rm -f "$HOLD/$x"; systemctl reset-failed "$x" 2>/dev/null; systemctl start "$x"; done; echo started; sleep 2 ;;
            s|S) for x in $u; do touch "$HOLD/$x"; systemctl stop "$x"; done; echo "stopped (use start / restart to bring it back)"; sleep 2 ;;
        esac
    done
}
RW=/usr/local/bin/mp-railway
railway_menu() {
    [ -x "$RW" ] || { curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/railway/mp-railway.sh -o /tmp/mp-railway.sh && bash /tmp/mp-railway.sh install; }
    while true; do
        clear; c '1;35' "  MP Railway - our own Railway (Discord bot 24/7)"; echo; line
        "$RW" status 2>/dev/null | sed 's/^/  /'
        line
        echo "  1) first-time setup (asks DISCORD_TOKEN + AUTH_PRIVATE_KEY, deploys)"
        echo "  2) live bot log           5) set DISCORD_TOKEN"
        echo "  3) deploy now (git pull)  6) set AUTH_PRIVATE_KEY"
        echo "  4) restart bot            7) variables (masked)"
        echo "  8) deploy history         0) back"
        line
        read -r -p "  choose: " k || return
        case "$k" in
            1) "$RW" setup; read -r -p "  enter to continue" _ ;;
            2) echo "(Ctrl+C to go back)"; "$RW" logs -f ;;
            3) "$RW" deploy; read -r -p "  enter to continue" _ ;;
            4) "$RW" restart; read -r -p "  enter to continue" _ ;;
            5) "$RW" env set DISCORD_TOKEN; read -r -p "  enter to continue" _ ;;
            6) "$RW" env set AUTH_PRIVATE_KEY; read -r -p "  enter to continue" _ ;;
            7) "$RW" env; read -r -p "  enter to continue" _ ;;
            8) tail -n 30 /opt/mp/railway/discordpy-bot/deploys.log 2>/dev/null || echo "(no deploys yet)"; read -r -p "  enter to continue" _ ;;
            0|q|b) return ;;
        esac
    done
}
while true; do
    header
    echo "  1) live broker log        5) restart the 4 servers"
    echo "  2) live server log        6) restart everything"
    echo "  3) full status            7) update (new mp_server + kit)"
    echo "  4) restart HiveMQ         8) regions.txt for the website"
    echo "  9) shell (bash)           0) logout"
    echo "  s) the 4 servers: log / start / stop / restart each one"
    echo "  o) old MP versions refused (name, version, phone id, IP)"
    echo "  r) MP Railway: Discord bot 24/7 (/gen /statsdc /chardc)"
    line
    read -r -p "  choose: " k || exit 0
    case "$k" in
        1) echo "(Ctrl+C to go back)"; journalctl -u hivemq -f -n 40 ;;
        2) echo "(Ctrl+C to go back)"; journalctl -u mp-server -f -n 40 ;;
        3) /opt/mp/console/status.sh | less -R ;;
        4) systemctl restart hivemq && echo restarted; sleep 2 ;;
        5) rm -f "$HOLD"/mp-server "$HOLD"/mp-bcw "$HOLD"/mp-mw3 "$HOLD"/mp-rp; systemctl restart mp-server mp-bcw mp-mw3 mp-rp mp-legacy && echo restarted; sleep 2 ;;
        6) rm -f "$HOLD"/*; systemctl restart hivemq mp-server mp-bcw mp-mw3 mp-rp mp-legacy mp-status; systemctl is-enabled -q mp-bridge 2>/dev/null && systemctl restart mp-bridge; echo restarted; sleep 2 ;;
        7) REGION="${REGION:-Asia}" bash /opt/mp/kit/update.sh 2>/dev/null || curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | REGION="${REGION:-Asia}" bash; read -r -p "  enter to continue" _ ;;
        8) /opt/mp/kit/tools/make-regions.sh; read -r -p "  enter to continue" _ ;;
        o|O) { echo "time  name  version  phone  IP  port"; tail -n 200 /opt/mp/server/outdated_clients.log 2>/dev/null || echo "(none yet)"; } | less -R ;;
        9) echo "type exit to come back"; bash -l ;;
        r|R) railway_menu ;;
        s|S) servers_menu ;;
        0|q|exit) exit 0 ;;
    esac
done
