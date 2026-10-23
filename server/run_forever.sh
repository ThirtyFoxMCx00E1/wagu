#!/usr/bin/env bash
# Keeps the MP server online 24/7: restarts it automatically if it ever stops or crashes.
# Works on Linux, a VPS and Termux (Android).
#   ./run_forever.sh [port] [server name] [max players] [region]
cd "$(dirname "$0")"
PORT="${1:-1922}"; NAME="${2:-MP LAN Server}"; MAX="${3:-100}"; REGION="${4:-Asia}"
[ -x ./mp_server ] || make
while true; do
    ./mp_server "$PORT" "$NAME" "$MAX" "$REGION" --persistent
    echo "[run_forever] mp_server exited ($?), restarting in 3 s..."
    sleep 3
done
