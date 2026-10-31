#!/usr/bin/env bash
# MP VPS low-latency tuning (3.37 VPS update), run at every boot by mp-net-tune.service.
# Things sysctl can't do: NIC interrupt coalescing, receive spreading over all CPUs, conntrack bypass for
# the game UDP ports, transparent huge page compaction (a hidden source of 10-100 ms lag spikes).
# Every step is optional: a VPS that doesn't support one just skips it. Undo: mp-net-tune.sh off
set -uo pipefail
ENV_FILE=/etc/mp-hivemq/mp.env
# shellcheck disable=SC1090
[ -f "$ENV_FILE" ] && . "$ENV_FILE"
PORTS=${XDP_PORTS:-"${SERVER_PORT:-1922} 1930 1931 1932"}
IFACE=${XDP_IFACE:-$(ip -o -4 route show to default 2>/dev/null | awk '{for(i=1;i<NF;i++) if($i=="dev"){print $(i+1); exit}}')}
CPUS=$(nproc)
say() { logger -t mp-net-tune "$*" 2>/dev/null; echo "[mp-net-tune] $*"; }
w() { [ -w "$1" ] && echo "$2" > "$1" 2>/dev/null; }

notrack() {   # $1 = -A / -D
    command -v iptables >/dev/null || return 0
    for p in $PORTS; do
        if [ "$1" = -A ]; then
            iptables -t raw -C PREROUTING -p udp --dport "$p" -j CT --notrack 2>/dev/null || iptables -t raw -A PREROUTING -p udp --dport "$p" -j CT --notrack 2>/dev/null
            iptables -t raw -C OUTPUT -p udp --sport "$p" -j CT --notrack 2>/dev/null || iptables -t raw -A OUTPUT -p udp --sport "$p" -j CT --notrack 2>/dev/null
        else
            iptables -t raw -D PREROUTING -p udp --dport "$p" -j CT --notrack 2>/dev/null
            iptables -t raw -D OUTPUT -p udp --sport "$p" -j CT --notrack 2>/dev/null
        fi
    done
}

if [ "${1:-on}" = off ]; then notrack -D; say "conntrack bypass removed (reboot resets the rest)"; exit 0; fi

# 0) the conntrack sysctls in 99-mp-network.conf only exist once the module is loaded: load it, apply again
modprobe nf_conntrack 2>/dev/null || true
[ -f /etc/sysctl.d/99-mp-network.conf ] && sysctl -q -p /etc/sysctl.d/99-mp-network.conf >/dev/null 2>&1 || true

# 1) game UDP skips connection tracking: no per-packet conntrack lookup, and a full conntrack table
#    (100+ phones x 4 servers, port scans) can never drop game packets. ufw still allows the ports.
[ "${NOTRACK:-1}" = 1 ] && command -v iptables >/dev/null && notrack -A && say "conntrack bypass for UDP $PORTS"

# 2) transparent huge pages: only for programs that ask (HiveMQ does), and never stall a thread to compact memory
w /sys/kernel/mm/transparent_hugepage/enabled madvise
w /sys/kernel/mm/transparent_hugepage/defrag defer+madvise
w /sys/kernel/mm/transparent_hugepage/khugepaged/defrag 0

# 3) CPU: full speed, no deep sleep states between packets (only where the VPS exposes them)
for g in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do w "$g" performance; done

if [ -n "$IFACE" ] && [ -d "/sys/class/net/$IFACE" ]; then
    # 4) interrupt coalescing off: a packet is handed over at once instead of waiting for more (saves 0.05-0.5 ms per hop)
    if command -v ethtool >/dev/null; then
        ethtool -C "$IFACE" adaptive-rx off rx-usecs 0 rx-frames 1 >/dev/null 2>&1 || ethtool -C "$IFACE" rx-usecs 0 >/dev/null 2>&1 || true
        # bigger NIC rings: a burst from 100 phones isn't dropped by the card
        mx=$(ethtool -g "$IFACE" 2>/dev/null | awk '/Pre-set/{f=1} f&&/^RX:/{print $2; exit}')
        [ -n "${mx:-}" ] && [ "$mx" != n/a ] && ethtool -G "$IFACE" rx "$mx" >/dev/null 2>&1 || true
    fi
    # 5) RPS/RFS: single-queue virtual NICs put every packet on CPU 0; spread them over all CPUs
    if [ "$CPUS" -gt 1 ]; then
        mask=$(printf '%x' $(( (1 << (CPUS > 31 ? 31 : CPUS)) - 1 )))
        w /proc/sys/net/core/rps_sock_flow_entries 32768
        nq=$(ls -d /sys/class/net/"$IFACE"/queues/rx-* 2>/dev/null | wc -l)
        for q in /sys/class/net/"$IFACE"/queues/rx-*; do
            w "$q/rps_cpus" "$mask"; w "$q/rps_flow_cnt" $(( 32768 / (nq > 0 ? nq : 1) ))
        done
    fi
    # 6) short transmit queue: fq + BBR pace it, a long qdisc queue only adds delay
    ip link set dev "$IFACE" txqueuelen 2000 2>/dev/null || true
    say "$IFACE: coalescing off, RPS over $CPUS CPU(s), THP defer"
fi
exit 0
