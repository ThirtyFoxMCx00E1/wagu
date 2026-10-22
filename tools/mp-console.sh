#!/usr/bin/env bash
# MP Multiplayer server console - the menu you get in the browser (website "Termius" page)
# or over SSH:  sudo /opt/mp/console/mp-console.sh
ENV_FILE=/etc/mp-hivemq/mp.env
. "$ENV_FILE" 2>/dev/null || true
c() { printf '\033[%sm%s\033[0m' "$1" "$2"; }
line() { printf '%s\n' "------------------------------------------------------------"; }
svc() { local s; s=$(systemctl is-active "$1" 2>/dev/null); [ "$s" = active ] && c '1;32' "online " || c '1;31' "${s:-off}"; }
conns() { ss -tn state established '( sport = :1883 or sport = :8000 or sport = :8883 or sport = :8884 )' 2>/dev/null | tail -n +2 | wc -l; }
header() {
    clear
    c '1;36' "  MP Multiplayer server console"; printf '   region %s  %s\n' "${REGION:-?}" "${DOMAIN:-$(hostname -I 2>/dev/null | awk '{print $1}')}"
    line
    printf '  HiveMQ broker   %s   MQTT clients: %s\n' "$(svc hivemq)" "$(conns)"
    printf '  24/7 server     %s   UDP %s, %s slots\n' "$(svc mp-server)" "${SERVER_PORT:-1922}" "${MAX_PLAYERS:-100}"
    printf '  Brief Case War  %s   UDP 1930, 20 slots\n' "$(svc mp-bcw)"
    printf '  region bridge   %s\n' "$(svc mp-bridge)"
    printf '  status beacon   %s\n' "$(svc mp-status)"
    printf '  uptime          %s   load %s\n' "$(uptime -p 2>/dev/null)" "$(cut -d' ' -f1-3 /proc/loadavg)"
    printf '  memory          %s\n' "$(free -m | awk '/Mem:/ {printf "%d / %d MB used", $3, $2}')"
    line
}
while true; do
    header
    echo "  1) live broker log        5) restart 24/7 server"
    echo "  2) live server log        6) restart everything"
    echo "  3) full status            7) update (git pull + install.sh)"
    echo "  4) restart HiveMQ         8) regions.txt for the website"
    echo "  9) shell (bash)           0) logout"
    line
    read -r -p "  choose: " k || exit 0
    case "$k" in
        1) echo "(Ctrl+C to go back)"; journalctl -u hivemq -f -n 40 ;;
        2) echo "(Ctrl+C to go back)"; journalctl -u mp-server -f -n 40 ;;
        3) /opt/mp/console/status.sh | less -R ;;
        4) systemctl restart hivemq && echo restarted; sleep 2 ;;
        5) systemctl restart mp-server mp-bcw && echo restarted; sleep 2 ;;
        6) systemctl restart hivemq mp-server mp-bcw mp-status; systemctl is-enabled -q mp-bridge 2>/dev/null && systemctl restart mp-bridge; echo restarted; sleep 2 ;;
        7) cd /opt/mp/kit && git pull -q && ./install.sh --region "${REGION:-Asia}"; read -r -p "  enter to continue" _ ;;
        8) /opt/mp/kit/tools/make-regions.sh; read -r -p "  enter to continue" _ ;;
        9) echo "type exit to come back"; bash -l ;;
        0|q|exit) exit 0 ;;
    esac
done
