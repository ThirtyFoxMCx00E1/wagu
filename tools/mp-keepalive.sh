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
# seconds the main process of a service has been running (0 = not running)
age() {
    local pid; pid=$(systemctl show -p MainPID --value "$1" 2>/dev/null)
    if [ -n "$pid" ] && [ "$pid" != 0 ]; then ps -o etimes= -p "$pid" 2>/dev/null | tr -d ' '; else echo 0; fi
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
        log "$u is running but $port is not open - restarting it"
        systemctl restart "$u" || true
    fi
}
check hivemq t 1883 180
check mp-server u "${SERVER_PORT:-1922}"
check mp-bcw u 1930
check mp-mw3 u 1931
check mp-rp u 1932
exit 0
