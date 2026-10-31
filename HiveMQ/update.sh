#!/usr/bin/env bash
# MP 24/7 server update (3.22). Gets the newest kit + mp_server and reinstalls with your saved
# settings (/etc/mp-hivemq/mp.env stays as it is). Safe to run while the servers are running.
#   curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash
#   or on the server:  sudo bash /opt/mp/kit/update.sh      (Termius console: 7) update)
set -euo pipefail
# 3.37 VPS: the kit is the HiveMQ/ folder of the wagu repo (one repo for the mod, the servers and the website)
KIT_REPO="https://github.com/ThirtyFoxMCx00E1/wagu.git"
BASE=/opt/mp
export MP_FORCE_RESTART="${MP_FORCE_RESTART:-0}"
say() { printf '\033[1;32m[mp-update]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[mp-update]\033[0m %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root: curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | sudo bash   (Debian without sudo: su - , then wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/update.sh | bash)"
command -v git >/dev/null || { apt-get update -qq && apt-get install -y -qq git; }

say "getting the newest kit"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
if git clone -q --depth 1 --filter=blob:none --sparse "$KIT_REPO" "$TMP/w" </dev/null 2>/dev/null && git -C "$TMP/w" sparse-checkout set HiveMQ </dev/null 2>/dev/null; then :
else rm -rf "$TMP/w"; git clone -q --depth 1 "$KIT_REPO" "$TMP/w" </dev/null || die "could not download $KIT_REPO"; fi
[ -f "$TMP/w/HiveMQ/install.sh" ] || die "no HiveMQ/ kit in $KIT_REPO"
rm -rf "$BASE/kit.new"; cp -a "$TMP/w/HiveMQ" "$BASE/kit.new"
rm -rf "$BASE/kit"; mv "$BASE/kit.new" "$BASE/kit"
chmod 755 "$BASE/kit/install.sh" "$BASE/kit/update.sh"

ARGS=()
if [ -n "${REGION:-}" ]; then ARGS+=(--region "$REGION"); fi
say "installing (saved settings from /etc/mp-hivemq/mp.env are kept)"
cd "$BASE/kit"
bash ./install.sh ${ARGS[@]+"${ARGS[@]}"}

# 3.37 VPS: install.sh restarts only what changed (players stay connected otherwise).
# Restart everything anyway:  MP_FORCE_RESTART=1 bash update.sh
sleep 2
if systemctl is-active --quiet mp-server; then
    say "mp-server is running: $(journalctl -u mp-server -n 50 --no-pager 2>/dev/null | grep -o 'MP [0-9][0-9.]*' | tail -n1 || true)"
else
    say "mp-server is not running (RUN_SERVER=0?) - journalctl -u mp-server -n 50"
fi
if [ -s /etc/mp-railway/discordpy-bot.env ]; then
    /usr/local/bin/mp-railway status discordpy-bot 2>/dev/null | sed -n 2p
else
    say "Discord bot not set up here yet. On ONE server: sudo mp-railway setup (asks DISCORD_TOKEN + AUTH_PRIVATE_KEY)"
fi
say "done. Phones on MP 3.21 now see this server as the 24/7 copy (direct UDP when port ${SERVER_PORT:-1922}/udp is open)."
