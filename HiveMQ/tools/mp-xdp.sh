#!/usr/bin/env bash
# MP XDP game-port filter (3.37 VPS update) - builds and attaches xdp/mp_xdp.c to the public network card.
#   mp-xdp.sh start | stop | status | allow IP | build
# Settings in /etc/mp-hivemq/mp.env:
#   XDP=1                 0 = never attach (default 1)
#   XDP_PPS=20000         packets per second per source IP before drops (0 = no limit, only size/type checks)
#   XDP_PORTS="1922 1930 1931 1932"
#   XDP_ALLOW="1.2.3.4 5.6.7.8"   IPs never limited (the other regions' VPSes are added from HOST_*)
#   XDP_IFACE=            empty = the interface of the default route
# Native XDP is used when the NIC driver supports it (virtio_net, ena, mlx5, ixgbe...), else generic XDP.
# If anything fails the server just runs without XDP - it is a fast path, never a requirement.
set -uo pipefail
ENV_FILE=/etc/mp-hivemq/mp.env
# shellcheck disable=SC1090
[ -f "$ENV_FILE" ] && . "$ENV_FILE"
BASE=${BASE:-/opt/mp}
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
C="$SRC/../xdp/mp_xdp.c"; [ -f "$C" ] || C="$BASE/kit/xdp/mp_xdp.c"
OBJ=$BASE/xdp/mp_xdp.o
PIN=/sys/fs/bpf/mp_xdp
IFACE=${XDP_IFACE:-$(ip -o -4 route show to default 2>/dev/null | awk '{for(i=1;i<NF;i++) if($i=="dev"){print $(i+1); exit}}')}
PORTS=${XDP_PORTS:-"${SERVER_PORT:-1922} 1930 1931 1932"}
PPS=${XDP_PPS:-20000}
log() { logger -t mp-xdp "$*" 2>/dev/null; echo "[mp-xdp] $*"; }

build() {
    command -v clang >/dev/null && command -v bpftool >/dev/null || {
        export DEBIAN_FRONTEND=noninteractive
        apt-get install -y -qq clang llvm libbpf-dev >/dev/null 2>&1
        apt-get install -y -qq bpftool >/dev/null 2>&1 || apt-get install -y -qq "linux-tools-$(uname -r)" linux-tools-common >/dev/null 2>&1
    }
    command -v clang >/dev/null || { log "clang missing - XDP skipped"; return 1; }
    mkdir -p "$(dirname "$OBJ")"
    clang -O2 -g -target bpf -I"/usr/include/$(uname -m)-linux-gnu" -c "$C" -o "$OBJ.new" && mv -f "$OBJ.new" "$OBJ"
}
bt() { bpftool "$@" 2>/dev/null; }
map() { echo "$PIN/maps/$1"; }
le16() { printf '%02x %02x' $(( $1 & 255 )) $(( $1 >> 8 )); }
le32() { printf '%02x %02x %02x %02x' $(( $1 & 255 )) $(( ($1 >> 8) & 255 )) $(( ($1 >> 16) & 255 )) $(( ($1 >> 24) & 255 )); }
ipkey() { local IFS=.; set -- $1; printf '%02x %02x %02x %02x' "$1" "$2" "$3" "$4"; }   # network order
resolve() { getent ahostsv4 "$1" 2>/dev/null | awk 'NR==1{print $1}'; }

allow() {
    local ip=$1; [[ $ip =~ ^[0-9.]+$ ]] || ip=$(resolve "$1"); [ -n "$ip" ] || return 0
    bt map update pinned "$(map mp_allow)" key hex $(ipkey "$ip") value hex 01 && echo "  allow $ip"
}

start() {
    [ "${XDP:-1}" = 1 ] || { log "XDP=0 in mp.env - not attached"; stop >/dev/null; return 0; }
    [ -n "$IFACE" ] || { log "no default route interface"; return 1; }
    [ -f "$OBJ" ] && [ "$OBJ" -nt "$C" ] || build || return 1
    command -v bpftool >/dev/null || { log "bpftool missing - XDP skipped"; return 1; }
    mountpoint -q /sys/fs/bpf || mount -t bpf bpf /sys/fs/bpf
    stop >/dev/null 2>&1
    bt prog load "$OBJ" "$PIN/prog" type xdp pinmaps "$PIN/maps" || { mkdir -p "$PIN/maps"; bpftool prog load "$OBJ" "$PIN/prog" type xdp pinmaps "$PIN/maps" || { log "load failed"; return 1; }; }
    local mode=xdpdrv
    ip link set dev "$IFACE" xdpdrv pinned "$PIN/prog" 2>/dev/null || { mode=xdpgeneric; ip link set dev "$IFACE" xdpgeneric pinned "$PIN/prog" || { log "attach failed on $IFACE"; rm -rf "$PIN"; return 1; }; }
    local p; [ "$PPS" = 0 ] && p=4294967295 || p=$PPS
    bt map update pinned "$(map mp_cfg)" key hex 00 00 00 00 value hex $(le32 "$p") $(le32 $(( p == 4294967295 ? 0 : p * 2 )))
    for port in $PORTS; do bt map update pinned "$(map mp_ports)" key hex $(le16 "$port") value hex 01; done
    allow 127.0.0.1
    for h in ${XDP_ALLOW:-} ${HOST_ASIA:-} ${HOST_EUROPE:-} ${HOST_NA:-} ${HOST_OCEANIA:-}; do allow "$h"; done
    echo "$IFACE $mode" > "$BASE/xdp/attached"
    log "attached to $IFACE ($mode), game ports: $PORTS, limit ${PPS}/s per IP (burst x2)"
}

stop() {
    local ifc=$IFACE; [ -f "$BASE/xdp/attached" ] && ifc=$(awk '{print $1}' "$BASE/xdp/attached")
    [ -n "$ifc" ] && { ip link set dev "$ifc" xdpdrv off 2>/dev/null; ip link set dev "$ifc" xdpgeneric off 2>/dev/null; }
    rm -rf "$PIN" "$BASE/xdp/attached"
    echo "[mp-xdp] detached"
}

status() {
    if [ ! -f "$BASE/xdp/attached" ]; then echo "XDP: off"; return 0; fi
    echo "XDP: on ($(cat "$BASE/xdp/attached")), ports $PORTS, ${PPS}/s per IP"
    local names=(passed dropped_size dropped_type dropped_rate)
    for k in 0 1 2 3; do
        n=$(bpftool -j map lookup pinned "$(map stats)" key hex $(le32 $k) 2>/dev/null | python3 -c 'import json,sys
d=json.load(sys.stdin); print(sum(int(v.get("value",0)) if not isinstance(v.get("value"),list) else 0 for v in d.get("formatted",{}).get("values",[])))' 2>/dev/null)
        echo "  ${names[$k]}: ${n:-?}"
    done
}

case "${1:-status}" in
    start) start;; stop) stop;; status) status;; build) build && echo "built $OBJ";;
    allow) shift; for a in "$@"; do allow "$a"; done;;
    *) echo "usage: $0 start|stop|status|allow IP|build"; exit 1;;
esac
