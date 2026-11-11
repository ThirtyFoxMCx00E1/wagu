#!/bin/bash
# 4.13: copies the Moquil app (moquil/assets/www, the files inside the APK) to the website:
#   docs/moquil/  -> mpsamp-open.github.io/moquil/ and thirtyfoxmcx00e1.github.io/wagu/moquil/
#   and, with a checkout path, to the mpsamp-open/moquils-web repo (mpsamp-open.github.io/moquils-web/)
#   bash tools/sync_moquil_web.sh [path-to-moquils-web-checkout]
set -e
cd "$(dirname "$0")/.."
rm -rf docs/moquil; mkdir -p docs/moquil
cp -r moquil/assets/www/. docs/moquil/
if [ -n "$1" ]; then
  find "$1" -mindepth 1 -maxdepth 1 ! -name .git ! -name README.md ! -name .nojekyll -exec rm -rf {} +
  cp -r moquil/assets/www/. "$1"/; touch "$1/.nojekyll"
fi
echo "synced $(find docs/moquil -type f | wc -l) files"
