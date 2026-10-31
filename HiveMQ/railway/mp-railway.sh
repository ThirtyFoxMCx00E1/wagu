#!/usr/bin/env bash
# MP Railway (3.21.7) - our own Railway: git push -> the app is rebuilt and restarted, 24/7.
# Runs on the HiveMQ server (systemd) or on an Android phone (Termux). No account, no card.
#
#   sudo mp-railway setup                  first time: Discord bot (asks DISCORD_TOKEN + AUTH_PRIVATE_KEY)
#   sudo mp-railway status | logs [-f]     is the bot online? which commit? live log
#   sudo mp-railway deploy                 pull the newest commit, build, restart (auto every 2 min)
#   sudo mp-railway restart | stop | start
#   sudo mp-railway env | env set KEY [VALUE] | env unset KEY
#   sudo mp-railway autodeploy on | off
#   sudo mp-railway add NAME REPO_URL [BRANCH]   any other Python / Node app, Railway style
#   sudo mp-railway apps | remove NAME
# The app name defaults to discordpy-bot; put another name last (mp-railway logs myapp).
set -uo pipefail
RW_VERSION="3.21.7"
DEFAULT_APP="discordpy-bot"
DEFAULT_REPO="https://github.com/ThirtyFoxMCx00E1/discordpy-bot.git"
SELF_URL="https://raw.githubusercontent.com/ThirtyFoxMCx00E1/wagu/main/HiveMQ/railway/mp-railway.sh"

SINCE=""; SINCE_LINE=0
if [[ "${PREFIX:-}" == */com.termux/* ]] || [ "${MP_RAILWAY_MODE:-}" = user ]; then
    MODE=termux; [ "${MP_RAILWAY_MODE:-}" = user ] && MODE=user   # user = no systemd, no root (tests, any Linux shell)
    ETC="$HOME/mp-railway/etc"; APPS="$HOME/mp-railway/apps"; DATA="$HOME/mp-railway/data"; LOGS="$HOME/mp-railway/logs"
    BIN="${PREFIX:-$HOME/.local}/bin/mp-railway"; [ "$MODE" = user ] && BIN="$HOME/.local/bin/mp-railway"
else
    MODE=systemd
    ETC=/etc/mp-railway; APPS=/opt/mp/railway; DATA=/var/lib/mp-railway; LOGS=/var/log/mp-railway
    BIN=/usr/local/bin/mp-railway
fi
RUN_USER=mprail

say()  { printf '\033[1;35m[railway]\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m[railway]\033[0m %s\n' "$*"; }
die()  { printf '\033[1;31m[railway]\033[0m %s\n' "$*" >&2; exit 1; }
need_root() { [ "$MODE" != systemd ] || [ "$(id -u)" = 0 ] || die "run as root: sudo mp-railway $*"; }
valid_app() { [[ "$1" =~ ^[a-z0-9][a-z0-9_-]{0,39}$ ]] || die "bad app name '$1' (a-z 0-9 - _)"; }

conf_of() { echo "$ETC/$1.conf"; }
env_of()  { echo "$ETC/$1.env"; }
load_conf() {
    REPO=""; BRANCH=main; AUTODEPLOY=1; START=""; HOST_LABEL="MP Railway"
    [ -f "$(conf_of "$1")" ] || die "no app '$1' (sudo mp-railway setup, or mp-railway add $1 REPO)"
    # shellcheck disable=SC1090
    . "$(conf_of "$1")"
}
save_conf() {   # app repo branch autodeploy start
    mkdir -p "$ETC"
    cat > "$(conf_of "$1")" <<EOF
# MP Railway app settings (not secret). Secrets are in $1.env.
REPO="$2"
BRANCH="$3"
AUTODEPLOY=$4
START="$5"
HOST_LABEL="${HOST_LABEL:-MP Railway}"
EOF
    chmod 644 "$(conf_of "$1")"
}

# ---------------------------------------------------------------- env (secrets)
env_get() { [ -f "$(env_of "$1")" ] && grep -E "^$2=" "$(env_of "$1")" | tail -n1 | cut -d= -f2- || true; }
env_set() {   # app key value
    local f; f="$(env_of "$1")"
    [[ "$2" =~ ^[A-Z_][A-Z0-9_]*$ ]] || die "bad variable name '$2'"
    case "$3" in *$'\n'*|*' '*) die "the value can't contain spaces or new lines" ;; esac
    mkdir -p "$ETC"; touch "$f"; chmod 600 "$f"
    grep -vE "^$2=" "$f" > "$f.tmp" 2>/dev/null || true
    printf '%s=%s\n' "$2" "$3" >> "$f.tmp"; mv -f "$f.tmp" "$f"; chmod 600 "$f"
}
env_unset() { local f; f="$(env_of "$1")"; [ -f "$f" ] && { grep -vE "^$2=" "$f" > "$f.tmp" || true; mv -f "$f.tmp" "$f"; chmod 600 "$f"; }; }
mask() { local v="$1"; [ ${#v} -le 8 ] && echo "********" || echo "${v:0:4}…${v: -4} (${#v} chars)"; }
ask_secret() {   # prompt -> REPLY (hidden); works with curl | bash too (reads the terminal)
    local p="$1"; REPLY=""
    if [ -r /dev/tty ]; then read -r -s -p "$p" REPLY </dev/tty; echo >/dev/tty; else read -r -s -p "$p" REPLY; echo; fi
    REPLY="$(printf '%s' "$REPLY" | tr -d '[:space:]')"
}

# ---------------------------------------------------------------- packages
packages() {
    if [ "$MODE" = user ]; then command -v git >/dev/null && command -v python3 >/dev/null || die "needs git + python3"
    elif [ "$MODE" = termux ]; then
        pkg install -y git python python-cryptography termux-services </dev/null >/dev/null 2>&1 || pkg install -y git python </dev/null || die "pkg install failed"
    else
        local miss=()
        command -v git >/dev/null || miss+=(git)
        command -v python3 >/dev/null || miss+=(python3)
        python3 -c 'import venv, ensurepip' 2>/dev/null || miss+=(python3-venv)
        [ ${#miss[@]} -eq 0 ] || { say "installing ${miss[*]}"; apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq "${miss[@]}" >/dev/null || die "apt-get install ${miss[*]} failed"; }
        id "$RUN_USER" >/dev/null 2>&1 || useradd --system --home-dir "$DATA" --shell /usr/sbin/nologin "$RUN_USER"
    fi
}

# ---------------------------------------------------------------- install the tool itself
self_install() {
    mkdir -p "$ETC" "$APPS" "$DATA" "$LOGS"; chmod 755 "$ETC"   # *.conf readable by the app user, *.env stay 600 (root)
    local me; me="$(readlink -f "${BASH_SOURCE[0]}" 2>/dev/null || echo "${BASH_SOURCE[0]}")"
    if [ -f "$me" ] && [ "$me" != "$BIN" ]; then cp -f "$me" "$BIN.new" && chmod 755 "$BIN.new" && mv -f "$BIN.new" "$BIN"
    elif [ ! -f "$BIN" ]; then curl -fsSL "$SELF_URL" -o "$BIN.new" && chmod 755 "$BIN.new" && mv -f "$BIN.new" "$BIN"; fi
    if [ "$MODE" = systemd ]; then
        cat > /etc/systemd/system/mp-railway@.service <<EOF
# MP Railway app %i - restarts on crash and on boot (24/7)
[Unit]
Description=MP Railway app %i
After=network-online.target
Wants=network-online.target
StartLimitIntervalSec=0

[Service]
Type=simple
User=$RUN_USER
EnvironmentFile=-$ETC/%i.env
Environment=PYTHONUNBUFFERED=1
ExecStart=$BIN run %i
Restart=always
RestartSec=5
NoNewPrivileges=true
ProtectSystem=full
PrivateTmp=true

[Install]
WantedBy=multi-user.target
EOF
        cat > /etc/systemd/system/mp-railway-watch.service <<EOF
[Unit]
Description=MP Railway auto deploy (new commit -> rebuild + restart)
After=network-online.target
[Service]
Type=oneshot
ExecStart=$BIN watch
EOF
        cat > /etc/systemd/system/mp-railway-watch.timer <<EOF
[Unit]
Description=MP Railway auto deploy every 2 minutes
[Timer]
OnBootSec=90
OnUnitActiveSec=120
Persistent=true
[Install]
WantedBy=timers.target
EOF
        systemctl daemon-reload
        systemctl enable --now mp-railway-watch.timer >/dev/null 2>&1 || true
    elif [ "$MODE" = termux ]; then
        mkdir -p "$HOME/.termux/boot"
        cat > "$HOME/.termux/boot/mp-railway" <<'EOF'
#!/data/data/com.termux/files/usr/bin/sh
# Termux:Boot - start MP Railway apps after a reboot
termux-wake-lock 2>/dev/null
mp-railway start-all >/dev/null 2>&1 &
EOF
        chmod 755 "$HOME/.termux/boot/mp-railway"
    fi
}

# ---------------------------------------------------------------- build (nixpacks style detection)
remote_head() { git ls-remote "$1" "refs/heads/$2" 2>/dev/null | cut -f1; }
current_commit() { git -C "$APPS/$1/src" rev-parse HEAD 2>/dev/null || true; }

build() {   # app
    local app="$1" dir="$APPS/$1" src="$APPS/$1/src"
    load_conf "$app"
    mkdir -p "$dir" "$DATA/$app" "$LOGS"
    say "$app: getting $REPO ($BRANCH)"
    if [ -d "$src/.git" ]; then
        git -C "$src" remote set-url origin "$REPO"
        git -C "$src" fetch -q --depth 1 origin "$BRANCH" </dev/null || die "$app: git fetch failed"
        git -C "$src" reset -q --hard FETCH_HEAD && git -C "$src" clean -qfdx -e data/
    else
        rm -rf "$src"; git clone -q --depth 1 --branch "$BRANCH" "$REPO" "$src" </dev/null || die "$app: git clone failed"
    fi
    local sha; sha="$(current_commit "$app")"
    say "$app: commit ${sha:0:7} $(git -C "$src" log -1 --format=%s 2>/dev/null | cut -c1-70)"
    if [ -f "$src/requirements.txt" ]; then
        say "$app: Python app - installing requirements"
        if [ "$MODE" != systemd ]; then
            [ -x "$dir/venv/bin/python" ] || "$(command -v python3 || command -v python)" -m venv --system-site-packages "$dir/venv" || die "$app: venv failed"
        else
            [ -x "$dir/venv/bin/python" ] || python3 -m venv "$dir/venv" || die "$app: venv failed (apt install python3-venv)"
        fi
        "$dir/venv/bin/python" -m pip install -q --upgrade pip >/dev/null 2>&1 || true
        "$dir/venv/bin/python" -m pip install -q -r "$src/requirements.txt" </dev/null || die "$app: pip install failed"
    elif [ -f "$src/package.json" ]; then
        command -v npm >/dev/null || { [ "$MODE" = termux ] && pkg install -y nodejs </dev/null >/dev/null; } || apt-get install -y -qq nodejs npm >/dev/null || die "$app: needs nodejs"
        say "$app: Node app - npm install"
        (cd "$src" && { npm ci --omit=dev 2>/dev/null || npm install --omit=dev; } ) </dev/null >/dev/null || die "$app: npm install failed"
    fi
    printf 'COMMIT=%s\nDEPLOYED=%s\n' "$sha" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$dir/release"
    echo "$(date -u +%FT%TZ) ${sha:0:7} $(git -C "$src" log -1 --format=%s | cut -c1-80)" >> "$dir/deploys.log"
    [ "$MODE" = systemd ] && chown -R "$RUN_USER:" "$dir" "$DATA/$app"
    return 0
}

start_cmd() {   # app -> prints the start command
    local src="$APPS/$1/src"
    if [ -n "$START" ]; then echo "$START"; return; fi
    if [ -f "$src/Procfile" ]; then
        local l; l="$(grep -E '^(worker|web):' "$src/Procfile" | head -n1 | cut -d: -f2-)"
        [ -n "$l" ] && { echo "$l"; return; }
    fi
    for f in main.py bot.py app.py; do [ -f "$src/$f" ] && { echo "python $f"; return; }; done
    [ -f "$src/package.json" ] && { echo "npm start"; return; }
    echo ""
}

# ---------------------------------------------------------------- run (inside the service)
run_app() {
    local app="$1" dir="$APPS/$1"
    load_conf "$app"
    cd "$dir/src" || exit 1
    [ "$MODE" != systemd ] && { set -a; . "$(env_of "$app")" 2>/dev/null; set +a; }
    export DATA_DIR="$DATA/$app" MP_RAILWAY_APP="$app" MP_RAILWAY_HOST="$HOST_LABEL"
    export MP_RAILWAY_COMMIT="$(sed -n 's/^COMMIT=//p' "$dir/release" 2>/dev/null)"
    [ -d "$dir/venv/bin" ] && export PATH="$dir/venv/bin:$PATH" VIRTUAL_ENV="$dir/venv"
    local cmd; cmd="$(start_cmd "$app")"
    [ -n "$cmd" ] || { echo "MP Railway: no start command (add START=... to $(conf_of "$app") or a Procfile)"; exit 1; }
    exec bash -c "$cmd"
}

# termux has no systemd: a small supervisor loop per app (restart on crash, 24/7 with wake lock)
t_pidf() { echo "$LOGS/$1.pid"; }
t_running() { local p; p="$(cat "$(t_pidf "$1")" 2>/dev/null)"; [ -n "$p" ] && kill -0 "$p" 2>/dev/null; }
t_start() {
    t_running "$1" && return 0
    termux-wake-lock 2>/dev/null || true
    nohup bash -c "while true; do '$BIN' run '$1' >> '$LOGS/$1.log' 2>&1; echo \"\$(date) exited (\$?), restart in 5 s\" >> '$LOGS/$1.log'; sleep 5; done" >/dev/null 2>&1 &
    echo $! > "$(t_pidf "$1")"
}
t_stop() { local p; p="$(cat "$(t_pidf "$1")" 2>/dev/null)"; [ -n "$p" ] && { pkill -P "$p" 2>/dev/null; kill "$p" 2>/dev/null; }; rm -f "$(t_pidf "$1")"; }

svc_start()   { rm -f "$APPS/$1/stopped"; if [ "$MODE" = systemd ]; then systemctl enable --now "mp-railway@$1" >/dev/null 2>&1; else t_start "$1"; fi; }
svc_restart() { rm -f "$APPS/$1/stopped"; if [ "$MODE" = systemd ]; then systemctl enable "mp-railway@$1" >/dev/null 2>&1; systemctl restart "mp-railway@$1"; else t_stop "$1"; sleep 1; t_start "$1"; fi; }
svc_stop()    { mkdir -p "$APPS/$1"; touch "$APPS/$1/stopped"; if [ "$MODE" = systemd ]; then systemctl disable --now "mp-railway@$1" >/dev/null 2>&1; else t_stop "$1"; fi; }
svc_active()  { if [ "$MODE" = systemd ]; then systemctl is-active --quiet "mp-railway@$1"; else t_running "$1"; fi; }
svc_log()     { if [ "$MODE" = systemd ]; then journalctl -u "mp-railway@$1" --no-pager -o cat -n "${2:-200}" 2>/dev/null; else tail -n "${2:-200}" "$LOGS/$1.log" 2>/dev/null; fi; }
# only the lines since the last (re)start, so an old "Slash commands" line doesn't count
mark_start()  { SINCE="$(date '+%Y-%m-%d %H:%M:%S')"; SINCE_LINE="$( { wc -l < "$LOGS/$1.log"; } 2>/dev/null || echo 0)"; }
svc_log_new() { if [ "$MODE" = systemd ]; then journalctl -u "mp-railway@$1" --no-pager -o cat --since "$SINCE" 2>/dev/null | tail -n 120; else tail -n +"$((SINCE_LINE + 1))" "$LOGS/$1.log" 2>/dev/null | tail -n 120; fi; }

# ---------------------------------------------------------------- checks after a deploy
health() {   # app
    [ "$1" = "$DEFAULT_APP" ] || { sleep 3; svc_active "$1" && say "$1: running" || warn "$1: not running - mp-railway logs $1"; return; }
    say "$1: waiting for Discord (up to 40 s)"
    local i l
    for i in $(seq 1 20); do
        sleep 2
        l="$(svc_log_new "$1")"
        if grep -q "Slash commands in" <<<"$l"; then
            grep -E "Logged in as|Slash commands in|No slash commands" <<<"$l" | tail -n 4 | sed 's/^.* INFO //; s/^.* WARNING /WARNING /'
            say "$1: ONLINE - /gen, /statsdc, /chardc are in your Discord server"; return 0
        fi
        grep -qE "Improper token|LoginFailure|401 Unauthorized" <<<"$l" && { HEALTH_FAIL=1; warn "$1: Discord refused the token - sudo mp-railway env set DISCORD_TOKEN (Developer Portal > Bot > Reset Token)"; return 1; }
        grep -q "PrivilegedIntentsRequired" <<<"$l" && { warn "$1: intents error - see mp-railway logs"; return 1; }
        grep -q "DISCORD_TOKEN is not set" <<<"$l" && { warn "$1: no DISCORD_TOKEN - sudo mp-railway env set DISCORD_TOKEN"; return 1; }
    done
    if grep -q "Logged in as" <<<"$l"; then warn "$1: online, but no server synced the commands yet - re-invite with the link in mp-railway logs"
    else warn "$1: not online yet - mp-railway logs -f"; return 1; fi
}

deploy() {   # app
    build "$1" || return 1
    say "$1: starting"
    mark_start "$1"; svc_restart "$1"
    health "$1"
}

# ---------------------------------------------------------------- commands
cmd_setup() {
    local app="${1:-$DEFAULT_APP}"; valid_app "$app"
    need_root setup
    packages; self_install
    HOST_LABEL="MP Railway"
    [ -f "$(conf_of "$app")" ] || save_conf "$app" "$DEFAULT_REPO" main 1 ""
    load_conf "$app"
    say "MP Railway $RW_VERSION - app $app ($REPO)"
    local cur; cur="$(env_get "$app" DISCORD_TOKEN)"
    if [ -n "${DISCORD_TOKEN:-}" ]; then env_set "$app" DISCORD_TOKEN "$DISCORD_TOKEN"
    elif [ -z "$cur" ] || [ "${FORCE_ASK:-0}" = 1 ]; then
        echo "Discord Developer Portal > your bot > Bot > Reset Token, copy it, paste here (hidden):"
        ask_secret "DISCORD_TOKEN: "; [ -n "$REPLY" ] || die "no token"; env_set "$app" DISCORD_TOKEN "$REPLY"
    else say "DISCORD_TOKEN kept ($(mask "$cur"))"; fi
    cur="$(env_get "$app" AUTH_PRIVATE_KEY)"
    if [ -n "${AUTH_PRIVATE_KEY:-}" ]; then env_set "$app" AUTH_PRIVATE_KEY "$AUTH_PRIVATE_KEY"
    elif [ -z "$cur" ] || [ "${FORCE_ASK:-0}" = 1 ]; then
        echo "Login system key (64 hex characters, your Auth_private_key.txt), paste here (hidden):"
        ask_secret "AUTH_PRIVATE_KEY: "
        [[ "$REPLY" =~ ^[0-9a-fA-F]{64}$ ]] || die "the key must be 64 hex characters"
        env_set "$app" AUTH_PRIVATE_KEY "$REPLY"
    else say "AUTH_PRIVATE_KEY kept ($(mask "$cur"))"; fi
    deploy "$app" || die "not online yet - fix the message above, then: sudo mp-railway restart"
    echo
    say "Done. The bot now runs 24/7 here: restarts on crash and on reboot, and every push to"
    say "$REPO is deployed by itself within 2 minutes. Website: https://thirtyfoxmcx00e1.github.io/wagu/railway.html"
}

cmd_status() {
    local app="${1:-}"
    local list; if [ -n "$app" ]; then list="$app"; else list="$(ls "$ETC" 2>/dev/null | sed -n 's/\.conf$//p')"; fi
    printf '\033[1;35mMP Railway %s\033[0m (%s)\n' "$RW_VERSION" "$MODE"
    [ -n "$list" ] || { echo "  no apps yet - sudo mp-railway setup"; return; }
    for a in $list; do
        load_conf "$a"
        local st c d
        if ! svc_active "$a"; then st=$'\033[1;31moffline\033[0m'
        elif [ "$a" != "$DEFAULT_APP" ]; then st=$'\033[1;32mrunning\033[0m'
        else   # the bot: online only when its newest log event is a Discord login (not a crash loop)
            case "$(svc_log "$a" 400 | grep -oE 'Logged in as|LoginFailure|Improper token|Traceback|DISCORD_TOKEN is not set|exited \(' | tail -n1)" in
                "Logged in as") st=$'\033[1;32monline \033[0m' ;;
                "") st=$'\033[1;33mstarting\033[0m' ;;
                *) st=$'\033[1;31mcrashing\033[0m (mp-railway logs)' ;;
            esac
        fi
        c="$(sed -n 's/^COMMIT=//p' "$APPS/$a/release" 2>/dev/null)"; d="$(sed -n 's/^DEPLOYED=//p' "$APPS/$a/release" 2>/dev/null)"
        printf '  %-16s %s  commit %s  deployed %s  autodeploy %s\n' "$a" "$st" "${c:0:7}" "${d:--}" "$([ "$AUTODEPLOY" = 1 ] && echo on || echo off)"
        printf '  %-16s repo %s (%s)\n' "" "$REPO" "$BRANCH"
        if [ "$a" = "$DEFAULT_APP" ]; then
            local l; l="$(svc_log "$a" 300)"
            printf '  %-16s %s\n' "" "$(grep -oE 'Logged in as [^,]*' <<<"$l" | tail -n1)"
            printf '  %-16s %s\n' "" "$(grep -oE 'Slash commands in .*' <<<"$l" | tail -n1)"
        fi
    done
}

cmd_env() {
    local sub="${1:-list}"
    case "$sub" in
        set)   need_root env; local k="${2:-}" v="${3:-}" app="${4:-$DEFAULT_APP}"
               [ -n "$k" ] || die "mp-railway env set KEY [VALUE] [app]"
               [ -n "$v" ] || { ask_secret "$k: "; v="$REPLY"; }
               [ -n "$v" ] || die "empty value"
               env_set "$app" "$k" "$v"; say "$app: $k set ($(mask "$v")) - restarting"; mark_start "$app"; svc_restart "$app"; health "$app" ;;
        unset) need_root env; local app="${3:-$DEFAULT_APP}"; env_unset "$app" "${2:?KEY}"; say "$app: ${2} removed - restarting"; svc_restart "$app" ;;
        list|*) need_root env; local app="${2:-$DEFAULT_APP}"; [ "$sub" != list ] && app="$sub"
               [ -f "$(env_of "$app")" ] || { echo "(no variables)"; return; }
               while IFS='=' read -r k v; do [ -n "$k" ] && printf '  %-20s %s\n' "$k" "$(mask "$v")"; done < "$(env_of "$app")" ;;
    esac
}

cmd_watch() {   # timer: deploy apps whose branch has a new commit
    for a in $(ls "$ETC" 2>/dev/null | sed -n 's/\.conf$//p'); do
        ( load_conf "$a"
          [ -f "$APPS/$a/stopped" ] && exit 0          # mp-railway stop: leave it off
          [ "$AUTODEPLOY" = 1 ] || { svc_active "$a" || svc_start "$a"; exit 0; }
          [ -s "$(env_of "$a")" ] || [ "$a" != "$DEFAULT_APP" ] || exit 0
          r="$(remote_head "$REPO" "$BRANCH")"; [ -n "$r" ] || exit 0
          c="$(current_commit "$a")"
          if [ "$r" != "$c" ]; then echo "$(date -u +%FT%TZ) new commit ${r:0:7} (was ${c:0:7}) - deploying"; deploy "$a"
          elif ! svc_active "$a"; then echo "$(date -u +%FT%TZ) $a was down - starting"; svc_start "$a"; fi
        ) 2>&1 | tee -a "$LOGS/watch.log"
    done
    # keep the tool itself current (MP kit updates replace it too)
    if [ "$MODE" = systemd ] && curl -fsSL --max-time 15 "$SELF_URL" -o "$BIN.dl" 2>/dev/null && head -n1 "$BIN.dl" | grep -q bash && bash -n "$BIN.dl"; then
        cmp -s "$BIN.dl" "$BIN" || { chmod 755 "$BIN.dl"; mv -f "$BIN.dl" "$BIN"; }
    fi
    rm -f "$BIN.dl"
}

main() {
    local c="${1:-help}"; shift || true
    case "$c" in
        setup)    cmd_setup "$@" ;;
        install)  need_root install; packages; self_install; say "installed $BIN ($MODE). Next: sudo mp-railway setup" ;;
        deploy|up|redeploy) need_root deploy; local a="${1:-$DEFAULT_APP}"; valid_app "$a"; deploy "$a" ;;
        status|ps) cmd_status "$@" ;;
        logs|log) local a="$DEFAULT_APP" f=0; for x in "$@"; do [ "$x" = -f ] && f=1 || a="$x"; done
                  if [ "$f" = 1 ]; then if [ "$MODE" = systemd ]; then journalctl -u "mp-railway@$a" -f -n 60 -o cat; else tail -n 60 -f "$LOGS/$a.log"; fi
                  else svc_log "$a" 120; fi ;;
        restart)  need_root restart; mark_start "${1:-$DEFAULT_APP}"; svc_restart "${1:-$DEFAULT_APP}"; health "${1:-$DEFAULT_APP}" ;;
        start)    need_root start; svc_start "${1:-$DEFAULT_APP}"; say "started" ;;
        stop)     need_root stop; svc_stop "${1:-$DEFAULT_APP}"; say "stopped (mp-railway start to run it again)" ;;
        start-all) for a in $(ls "$ETC" 2>/dev/null | sed -n 's/\.conf$//p'); do svc_start "$a"; done ;;
        env|variables) cmd_env "$@" ;;
        autodeploy) need_root autodeploy; local a="${2:-$DEFAULT_APP}"; load_conf "$a"
                  save_conf "$a" "$REPO" "$BRANCH" "$([ "${1:-on}" = off ] && echo 0 || echo 1)" "$START"; say "$a: autodeploy ${1:-on}" ;;
        add)      need_root add; local n="${1:?name}" r="${2:?repo url}" b="${3:-main}"; valid_app "$n"
                  packages; self_install; HOST_LABEL="MP Railway"; save_conf "$n" "$r" "$b" 1 ""; say "$n added - variables: mp-railway env set KEY VALUE $n"; deploy "$n" ;;
        remove)   need_root remove; local n="${1:?name}"; valid_app "$n"; svc_stop "$n"; rm -rf "$APPS/$n" "$(conf_of "$n")"; say "$n removed (secrets kept in $(env_of "$n"), data in $DATA/$n)" ;;
        apps)     ls "$ETC" 2>/dev/null | sed -n 's/\.conf$//p' ;;
        watch)    cmd_watch ;;
        run)      run_app "${1:?app}" ;;
        version|-v|--version) echo "MP Railway $RW_VERSION ($MODE)" ;;
        *)        sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//' ;;
    esac
}
main "$@"
