#!/usr/bin/env bash
# MP 24/7 server update (3.22). Gets the newest kit + mp_server and reinstalls with your saved
# settings (/etc/mp-hivemq/mp.env stays as it is). Safe to run while the servers are running.
#   curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash
#   or on the server:  sudo bash /opt/mp/kit/update.sh      (Termius console: 7) update)
set -euo pipefail
KIT_REPO="https://github.com/ThirtyFoxMCx00E1/HiveMQ.git"
BASE=/opt/mp
say() { printf '\033[1;32m[mp-update]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[mp-update]\033[0m %s\n' "$*" >&2; exit 1; }
[ "$(id -u)" = 0 ] || die "run as root: curl -fsSL https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | sudo bash   (Debian without sudo: su - , then wget -qO- https://raw.githubusercontent.com/ThirtyFoxMCx00E1/HiveMQ/main/update.sh | bash)"
command -v git >/dev/null || { apt-get update -qq && apt-get install -y -qq git; }

say "getting the newest kit"
TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
git clone -q --depth 1 "$KIT_REPO" "$TMP/kit" </dev/null || die "could not download $KIT_REPO"
rm -rf "$BASE/kit.new"; cp -a "$TMP/kit" "$BASE/kit.new"
rm -rf "$BASE/kit"; mv "$BASE/kit.new" "$BASE/kit"
chmod 755 "$BASE/kit/install.sh" "$BASE/kit/update.sh"

ARGS=()
if [ -n "${REGION:-}" ]; then ARGS+=(--region "$REGION"); fi
say "installing (saved settings from /etc/mp-hivemq/mp.env are kept)"
cd "$BASE/kit"
bash ./install.sh ${ARGS[@]+"${ARGS[@]}"}

for s in mp-server mp-bcw mp-mw3 mp-rp mp-legacy; do
    systemctl is-enabled --quiet "$s" 2>/dev/null && systemctl restart "$s" || true
done
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
