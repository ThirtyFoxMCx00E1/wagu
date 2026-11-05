#!/system/bin/sh
# 4.05.9: downloads two root network modules straight from their authors (they have no licence that lets MP
# ship copies). Flash the zips in Magisk / KernelSU / APatch -> Modules -> Install from storage, reboot.
#   NetworkEnhancer (RAAJK20Pro)          TCP/IP buffers + latency settings in the kernel
#   wifi-bonding-nolog (Magisk Alt Repo)  Wi-Fi channel bonding + low-latency TCP + no Wi-Fi packet logging
# Root only. Without root use the apps in apps/ instead. Undo: remove the module in the root app, reboot.
OUT=${1:-/sdcard/Download/MP-NetTools}
mkdir -p "$OUT" || exit 1
get() { if command -v curl >/dev/null 2>&1; then curl -fL "$1" -o "$2"; else wget -O "$2" "$1"; fi; }
get https://github.com/RAAJK20Pro/NetworkEnhancer/releases/download/V6.5/NetworkEnhancerV6.5.zip "$OUT/NetworkEnhancer-V6.5.zip" && echo "ok  $OUT/NetworkEnhancer-V6.5.zip"
get https://codeload.github.com/Magisk-Modules-Alt-Repo/wifi-bonding-nolog/zip/refs/heads/main "$OUT/wifi-bonding-src.zip" || exit 1
# GitHub's source zip has a top folder; Magisk wants module.prop at the root: repack it
if command -v unzip >/dev/null 2>&1 && command -v zip >/dev/null 2>&1; then
    T="$OUT/.wb"; rm -rf "$T"; mkdir -p "$T"; unzip -q "$OUT/wifi-bonding-src.zip" -d "$T"
    (cd "$T"/wifi-bonding-nolog-* && zip -q -r "$OUT/wifi-bonding-nolog.zip" .) && rm -rf "$T" "$OUT/wifi-bonding-src.zip" && echo "ok  $OUT/wifi-bonding-nolog.zip"
else
    echo "unzip/zip missing (Termux: pkg install zip unzip) - $OUT/wifi-bonding-src.zip must be repacked before flashing"
fi
