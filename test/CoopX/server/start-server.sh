#!/usr/bin/env sh
set -eu
cd "$(dirname "$0")"
if ! command -v node >/dev/null 2>&1; then
  echo "Node.js is required to run this optional external server." >&2
  exit 2
fi
exec node index.mjs
