# MP NetTools (4.05.9)

Helpers for the MP connection: weak Wi-Fi, mobile data, Wi-Fi + mobile data at once, UDP or TCP problems,
lost connections. MP itself (4.05.9) already does the parts that work inside the game without root:
network switch hand-over, DNS time limit, voice-class Wi-Fi priority (DSCP EF) on the game socket,
loss / jitter measurement and sending every sync twice on a weak link. These are the extra tools around it.

## test/ - find out what your connection really does
`mp-nettest.sh` measures UDP (the game) and TCP (lobby / relay) round trip, jitter and loss to the 4 MP
regions. Every region VPS runs a sockperf test server on UDP + TCP 11111.

    # Termux
    cp bin/sockperf-aarch64 $PREFIX/bin/sockperf && chmod +x $PREFIX/bin/sockperf   # armv7l on 32-bit phones
    bash test/mp-nettest.sh            # all regions
    bash test/mp-nettest.sh eu 20      # one region, 20 s

Run it on Wi-Fi and again on mobile data. Loss only on Wi-Fi = weak Wi-Fi (move closer, 5 GHz, or use the apps below).

## bin/ - sockperf, netperf, netserver for Android (arm64 aarch64, 32-bit armv7l)
Static C++ runtime, run from Termux or `adb shell`. Examples:

    sockperf ping-pong -i asia.host -p 11111 -t 10          # UDP latency
    sockperf ping-pong -i asia.host -p 11111 --tcp -t 10    # TCP latency
    netserver                                              # on one phone / PC
    netperf -H <that ip> -t UDP_RR                          # request/response rate between two devices (LAN host)

## apps/ - no root
- **net-merge-android.apk** (MIT) - uses Wi-Fi and mobile data at the same time. Each connection goes over one
  network, so the game's UDP stream is not split, but downloads (MP updates, Day Z packs) use both and the
  game keeps a working path when one network drops. It is a VPN app: only one VPN app can run at a time.
- **RIPDPI-*.apk** (BSD-3) - when your mobile operator blocks or slows connections (DPI): bypasses it on the phone,
  or tunnels through a relay server you control (the MP VPS can be one). Also shows what is being blocked.
- **Relay-android-arm64-v8a.apk** (GPL-3, source: github.com/Mahdi-mortazavi/relay) - phone internet to a PC
  (reverse tethering over WireGuard): host the MP server on a PC on the phone's mobile data.

## pc/ - net-merge for Linux (MIT)
For a PC / box that hosts an MP server: Wi-Fi + Ethernet + phone tether at once.

## vps/ - NetworkOptimizer (MIT)
`optimize.sh` - BBR, fq, buffers. The MP VPS install (HiveMQ/install.sh, 99-mp-network.conf) already sets
these and more (XDP, conntrack bypass, coalescing off), so only use it on a non-MP VPS / relay.

## magisk/ - root only
`get-modules.sh` downloads NetworkEnhancer and wifi-bonding-nolog from their authors (they can't be shipped
in this zip), then flash them in Magisk / KernelSU / APatch and reboot.

Sources: sockperf github.com/ThirtyFoxMCx00E1/sockperf, netperf github.com/ThirtyFoxMCx00E1/netperf,
RIPDPI github.com/po4yka/RIPDPI, net-merge github.com/net-merge/net-merge + github.com/net-merge/android,
NetworkOptimizer github.com/emadtoranji/NetworkOptimizer, NetworkEnhancer github.com/RAAJK20Pro/NetworkEnhancer,
wifi-bonding-nolog github.com/Magisk-Modules-Alt-Repo/wifi-bonding-nolog, flat (eBPF latency, Linux root)
github.com/ThirtyFoxMCx00E1/flat, EtherifyZero (Windows PCs) github.com/PhilosopherZero/EtherifyZero.
