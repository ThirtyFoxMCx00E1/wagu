#!/usr/bin/env bash
# 4.05.8: MP connection test (Termux on the phone, or any Linux / PC shell).
# Measures the real UDP (= the game) and TCP (= the lobby / relay path) round trip, jitter and loss to
# each MP region with sockperf ping-pong against the sockperf server on the region VPS (UDP + TCP 11111).
# Run it once on Wi-Fi and once on mobile data: it says which region and which path is best right now.
#   bash mp-nettest.sh            all 4 regions, 8 s each
#   bash mp-nettest.sh eu 20      one region (asia / eu / na / oc), 20 s
# Needs sockperf: the MP-NetTools zip has it for Android (arm64 / armv7) in bin/, Termux: put it in $PREFIX/bin.
set -u
SECS=${2:-8}
declare -A HOST=( [asia]=mpsamp.duckdns.org [eu]=eusamp.duckdns.org [na]=nasamp.duckdns.org [oc]=ocsamp.duckdns.org )
ORDER="asia eu na oc"
[ -n "${1:-}" ] && ORDER="$1"
SP=$(command -v sockperf || true)
[ -z "$SP" ] && [ -x "$(dirname "$0")/bin/sockperf-$(uname -m)" ] && SP="$(dirname "$0")/bin/sockperf-$(uname -m)"
[ -z "$SP" ] && { echo "sockperf not found (copy bin/sockperf-<arch> from MP-NetTools to \$PREFIX/bin/sockperf)"; exit 1; }

net="?"
if command -v termux-wifi-connectioninfo >/dev/null 2>&1; then
    termux-wifi-connectioninfo 2>/dev/null | grep -q '"supplicant_state": "COMPLETED"' && net="Wi-Fi" || net="mobile data"
fi
echo "MP connection test - network: $net - ${SECS} s per test"
printf '%-6s %-5s %8s %8s %8s %7s\n' region path avg_ms p99_ms jit_ms loss
best=""; bestv=999999
for r in $ORDER; do
    h=${HOST[$r]:-$r}
    for mode in udp tcp; do
        flag=""; [ "$mode" = tcp ] && flag="--tcp"
        # 20 packets/s of 100 bytes ~ the game's sync stream
        out=$("$SP" ping-pong -i "$h" -p 11111 $flag -t "$SECS" --mps=20 -m 100 --full-rtt 2>&1)
        avg=$(echo "$out" | sed -n 's/.*avg-rtt=\([0-9.]*\).*/\1/p' | tail -1)
        p99=$(echo "$out" | sed -n 's/.*percentile 99.000 = *\([0-9.]*\).*/\1/p' | tail -1)
        p50=$(echo "$out" | sed -n 's/.*percentile 50.000 = *\([0-9.]*\).*/\1/p' | tail -1)
        drop=$(echo "$out" | sed -n 's/.*# dropped messages = \([0-9]*\).*/\1/p' | tail -1)
        sent=$(echo "$out" | sed -n 's/.*SentMessages=\([0-9]*\).*/\1/p' | tail -1)
        if [ -z "$avg" ]; then printf '%-6s %-5s %8s\n' "$r" "$mode" "no answer"; continue; fi
        # sockperf reports usec
        a=$(awk -v x="$avg" 'BEGIN{printf "%.0f", x/1000}')
        p=$(awk -v x="${p99:-0}" 'BEGIN{printf "%.0f", x/1000}')
        j=$(awk -v x="${p99:-0}" -v y="${p50:-0}" 'BEGIN{printf "%.0f", (x-y)/1000}')
        l=$(awk -v d="${drop:-0}" -v s="${sent:-0}" 'BEGIN{ if(s>0) printf "%.1f%%", d*100/s; else print "-" }')
        printf '%-6s %-5s %8s %8s %8s %7s\n' "$r" "$mode" "$a" "$p" "$j" "$l"
        if [ "$mode" = udp ]; then v=$((a + j)); [ "$v" -lt "$bestv" ] && { bestv=$v; best=$r; }; fi
    done
done
echo
[ -n "$best" ] && echo "Best region for the game right now: $best (UDP). In MP: launcher -> region icon -> $best, or leave it on Auto."
echo "UDP much worse than TCP (loss / no answer): this network limits UDP - MP falls back to the relay by itself."
echo "Loss on Wi-Fi but not on mobile data: weak Wi-Fi - MP 4.05.8 then sends every sync twice (\"weak link\")."
