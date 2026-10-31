#!/usr/bin/env bash
# MP keep-alive (3.33): keeps the broker and the 4 featured servers online 24/7:
#   MP LAN Server (default, UDP 1922) - Brief Case War (1930) - Modern Warfare 3 (1931) - Mega Vil Futuristic (1932)
# No phone hosts these servers any more, so this VPS is the only host. Run every minute by mp-keepalive.timer.
#   - a stopped / failed service is started again
#   - a service that is "active" but whose port is not open (hung start) is restarted
#   - a service you stopped on purpose from the console (hold file in /etc/mp-hivemq/hold) is left alone
ENV_FILE=/etc/mp-hivemq/mp.env
HOLD=/etc/mp-hivemq/hold
# shellcheck disable=SC1090
[ -f "$ENV_FILE" ] && . "$ENV_FILE"
mkdir -p "$HOLD"
log() { logger -t mp-keepalive "$*"; echo "$*"; }
# 3.37 VPS: a busy server (100+ players joining at once) can miss one check. Restarting it then would kick
# everyone, so a running server is only restarted after STRIKES checks in a row fail (default 3 = ~3 min).
# A stopped / crashed service is still started at once.
STRIKES=${KEEPALIVE_STRIKES:-3}
SD=/run/mp-keepalive; mkdir -p "$SD"
strike() {  # unit reason -> restart only on the STRIKES-th failure in a row
    local n; n=$(( $(cat "$SD/$1" 2>/dev/null || echo 0) + 1 ))
    if [ "$n" -ge "$STRIKES" ]; then
        log "$1 $2 ($n checks in a row) - restarting it"; rm -f "$SD/$1"; systemctl restart "$1" || true
    else
        echo "$n" > "$SD/$1"; log "$1 $2 (check $n of $STRIKES, not restarting yet)"
    fi
}
# seconds the main process of a service has been running (0 = not running)
age() {
    local pid; pid=$(systemctl show -p MainPID --value "$1" 2>/dev/null)
    if [ -n "$pid" ] && [ "$pid" != 0 ]; then ps -o etimes= -p "$pid" 2>/dev/null | tr -d ' '; else echo 0; fi
}
# 3.34: a server can be "running" with its port open but hung (no answers = players see it offline /
# freezing). Ask it like the lobby does (PKT_QUERY = 14) up to 3 times; no PKT_INFO back = restart.
answers() {   # port -> 0 when the server answers
    command -v python3 >/dev/null 2>&1 || return 0
    python3 - "$1" <<'PY'
import socket, struct, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.settimeout(1.5)
for _ in range(5):   # 3.37 VPS: 5 tries (was 3)
    try:
        s.sendto(struct.pack("<BI", 14, int(time.time() * 1000) & 0xFFFFFFFF), ("127.0.0.1", int(sys.argv[1])))
        d, _a = s.recvfrom(2048)
        if d and d[0] == 15: sys.exit(0)
    except Exception: pass
sys.exit(1)
PY
}
check() {   # unit  u|t  port  min-age-seconds
    local u=$1 proto=$2 port=$3 grace=${4:-60} fl
    systemctl is-enabled --quiet "$u" 2>/dev/null || return 0
    [ -e "$HOLD/$u" ] && return 0
    if ! systemctl is-active --quiet "$u"; then
        log "$u is $(systemctl is-active "$u" 2>/dev/null) - starting it"
        systemctl reset-failed "$u" 2>/dev/null || true
        systemctl start "$u" || true
        return 0
    fi
    a=$(age "$u"); [ "${a:-0}" -lt "$grace" ] && return 0     # just started: it needs a moment to open its port
    [ "$proto" = u ] && fl=-lun || fl=-ltn
    if [ -z "$(ss -H $fl "sport = :$port" 2>/dev/null)" ]; then
        strike "$u" "is running but $port is not open"
        return 0
    fi
    if [ "$proto" = u ] && ! answers "$port"; then
        strike "$u" "has $port open but does not answer"
        return 0
    fi
    rm -f "$SD/$u"   # healthy: forget earlier misses
}
check hivemq t 1883 180
check mp-server u "${SERVER_PORT:-1922}"
check mp-bcw u 1930
check mp-mw3 u 1931
check mp-rp u 1932
exit 0
