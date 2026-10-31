#!/usr/bin/env bash
# 3.38: the 4 featured servers with their 24/7 bot host ("Broker") on ANY always-on machine:
# a VPS, a home PC, or an old Android phone with Termux. Each one restarts by itself if it stops.
#   ./run_24_7.sh [region]          region = Asia (default) | Europe | NA | Oceania
#   ./run_24_7.sh stop              stops all 4
# Phones see them as "24/7" and move to them, so a player (the old phone host) leaving no longer
# drops everybody. Works without HiveMQ: with no broker.txt the servers use the regional brokers from
# the website, and the free public lobby when those are down. On a VPS use HiveMQ/vps-setup.sh instead
# (systemd services + the public mirror). Logs: logs/<port>.log
cd "$(dirname "$0")" || exit 1
REGION="${1:-${REGION:-Asia}}"
if [ "$REGION" = stop ]; then
    for p in run/*.pid; do [ -f "$p" ] && kill "$(cat "$p")" 2>/dev/null; rm -f "$p"; done
    pkill -f "mp_server 19(22|30|31|32) " 2>/dev/null; echo "stopped"; exit 0
fi
[ -x ./mp_server ] || make -j"$(nproc 2>/dev/null || echo 2)" || exit 1
mkdir -p logs run
BF=(); [ -f broker.txt ] && BF=(--broker-file "$PWD/broker.txt")
keep() {   # port name max extra...
    local port="$1" name="$2" max="$3"; shift 3
    if [ -f "run/$port.pid" ] && kill -0 "$(cat "run/$port.pid")" 2>/dev/null; then echo "  $name (port $port) already running"; return; fi
    ( while true; do
          ./mp_server "$port" "$name" "$max" "$REGION" --persistent "${BF[@]}" "$@" < /dev/null >> "logs/$port.log" 2>&1
          echo "[run_24_7] $name exited ($?), back in 2 s" >> "logs/$port.log"; sleep 2
      done ) &
    echo $! > "run/$port.pid"; echo "  $name (port $port) started"
}
echo "MP 24/7 servers, region $REGION:"
keep 1922 "MP LAN Server" 100
keep 1930 "Brief Case War" 20
keep 1931 "Call Of Duty: Modern Warfare 3" 64 --mw3-file mw3_stats.txt
keep 1932 "Mega Vil Futuristic" 250 --rp-file rp_citizens.txt
if [ "${FOREGROUND:-0}" = 1 ]; then   # 3.38: Docker / Railway: stay in the foreground, stop all 4 on exit
    trap 'kill 0' TERM INT
    tail -n 0 -F logs/*.log & wait
fi
echo "Running in the background (also after you close this terminal: use nohup / termux-wake-lock)."
echo "Stop: ./run_24_7.sh stop"
