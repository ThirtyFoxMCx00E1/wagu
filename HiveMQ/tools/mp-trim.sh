#!/usr/bin/env bash
# MP cache / log trim (3.37 VPS update), every 6 hours by mp-trim.timer. Keeps the disk and RAM light
# WITHOUT restarting anything or kicking players:
#   - journald logs capped at 200 MB / 7 days (busy servers write a lot)
#   - old HiveMQ logs (rotated .log / .gz) older than 7 days removed
#   - mp_server side logs (outdated_clients.log, ...) cut to their last 5000 lines when > 20 MB
#   - apt package cache cleaned, /tmp files older than 2 days removed
# It does NOT drop the Linux page cache: that cache is what keeps the servers fast (dropping it = disk reads = lag).
# HiveMQ's own "cache" (offline sessions + queued messages) is limited in config.xml: sessions expire after
# 2 minutes offline, at most 64 queued messages per client, old ones are discarded first.
set -uo pipefail
BASE=${BASE:-/opt/mp}
say() { logger -t mp-trim "$*" 2>/dev/null; echo "[mp-trim] $*"; }
before=$(df -Pm / | awk 'NR==2{print $4}')
journalctl --vacuum-size=200M --vacuum-time=7d >/dev/null 2>&1 || true
find "$BASE/hivemq/log" -type f \( -name '*.gz' -o -name '*.log.*' -o -name '*-*.log' \) -mtime +7 -delete 2>/dev/null || true
for f in "$BASE"/server/*.log; do
    [ -f "$f" ] || continue
    if [ "$(stat -c %s "$f")" -gt 20971520 ]; then tail -n 5000 "$f" > "$f.tmp" && cat "$f.tmp" > "$f"; rm -f "$f.tmp"; fi
done
apt-get clean >/dev/null 2>&1 || true
find /tmp /var/tmp -xdev -type f -atime +2 -delete 2>/dev/null || true
after=$(df -Pm / | awk 'NR==2{print $4}')
say "done: $(( after - before )) MB freed, $(free -m | awk '/Mem:/{print $7}') MB RAM available"
