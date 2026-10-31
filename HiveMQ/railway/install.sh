#!/usr/bin/env bash
# MP Railway installer (3.21.7): our own Railway for the MP Discord bot, 24/7.
#   HiveMQ server / Ubuntu / Debian:  curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/railway/install.sh | sudo bash
#   Android (Termux):                 curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/railway/install.sh | bash
# Asks for DISCORD_TOKEN and AUTH_PRIVATE_KEY (hidden), deploys discordpy-bot and keeps it online.
# Non-interactive: DISCORD_TOKEN=... AUTH_PRIVATE_KEY=... bash install.sh
set -euo pipefail
URL="https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/railway/mp-railway.sh"
TMP="$(mktemp)"; trap 'rm -f "$TMP"' EXIT
curl -fsSL "$URL" -o "$TMP" || { echo "could not download $URL" >&2; exit 1; }
bash -n "$TMP"
exec bash "$TMP" setup "$@"
