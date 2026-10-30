#!/usr/bin/env bash
# Quick health check of this region's MP services.
for s in hivemq mp-server mp-bcw mp-mw3 mp-rp mp-legacy mp-keepalive.timer mp-bridge mp-status mp-console mp-railway@discordpy-bot; do
    printf '%-26s %s\n' "$s" "$(systemctl is-active "$s" 2>/dev/null)"
done
echo; echo "listening:"; ss -ltnup 2>/dev/null | grep -E ':(1883|8000|8883|8884|1922|1930|1931|1932|7681)\b' || true
echo; echo "connections to the broker: $(ss -tn state established '( sport = :1883 or sport = :8000 or sport = :8883 or sport = :8884 )' 2>/dev/null | tail -n +2 | wc -l)"
echo; echo "memory:"; free -m | head -2
echo; echo "last broker log lines:"; journalctl -u hivemq -n 5 --no-pager 2>/dev/null | tail -5
echo; echo "last server log lines:"; journalctl -u mp-server -n 5 --no-pager 2>/dev/null | tail -5
