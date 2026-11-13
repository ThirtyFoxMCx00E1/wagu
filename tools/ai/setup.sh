#!/usr/bin/env bash
# 4.13.19: installs the AI modding tools vendored in tools/ai/ (see README.md) into tools/ai/.venv
#   bash tools/ai/setup.sh            the Python tools (gta-scout, gta-flow, reagent, ghidra-bridge)
#   bash tools/ai/setup.sh --ghidra   + Ghidra (latest release, ~570 MB, needs JDK 21+) into tools/ai/ghidra/
set -euo pipefail
cd "$(dirname "$0")"
PY=${PYTHON:-python3}
"$PY" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)' || { echo "Python 3.10+ is needed"; exit 1; }
[ -d .venv ] || "$PY" -m venv .venv
. .venv/bin/activate
python -m pip install -q --upgrade pip
python -m pip install -q -e ./gta-scout -e ./gta-flow -e ./reagent -e "./ghidra-bridge[headless]"
echo "installed: asset-catalog (gta-scout), sa-traffic (gta-flow), re-agent (reagent), ghidra-bridge"

if [ "${1:-}" = "--ghidra" ]; then
    command -v java >/dev/null && java -version 2>&1 | head -1 || echo "warning: no java - Ghidra 12 needs JDK 21+ (apt install openjdk-21-jdk)"
    if ! ls -d ghidra/ghidra_*_PUBLIC >/dev/null 2>&1; then
        URL=$(curl -fsSL https://api.github.com/repos/NationalSecurityAgency/ghidra/releases/latest \
              | grep -o '"browser_download_url": *"[^"]*_PUBLIC_[^"]*\.zip"' | head -1 | cut -d'"' -f4)
        URL=${URL:-https://github.com/NationalSecurityAgency/ghidra/releases/download/Ghidra_12.2_build/ghidra_12.2_PUBLIC_20261016.zip}
        mkdir -p ghidra && echo "downloading $URL" && curl -fL -o ghidra/ghidra.zip "$URL"
        (cd ghidra && unzip -q ghidra.zip && rm ghidra.zip)
    fi
    G=$(ls -d "$PWD"/ghidra/ghidra_*_PUBLIC | tail -1)
    echo "Ghidra: $G   (export GHIDRA_INSTALL_DIR=$G)"
fi
echo "next: . tools/ai/.venv/bin/activate   then see tools/ai/README.md"
