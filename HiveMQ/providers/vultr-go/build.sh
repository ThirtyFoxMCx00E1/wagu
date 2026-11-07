#!/usr/bin/env bash
# builds mp-deploy-vultr for Windows / Linux / Mac (Go 1.22+)
set -e; cd "$(dirname "$0")"; cp ../user-data.sh user-data.sh
for t in windows/amd64 linux/amd64 darwin/arm64; do
  o=mp-deploy-vultr-${t%/*}-${t#*/}; [ ${t%/*} = windows ] && o=$o.exe
  CGO_ENABLED=0 GOOS=${t%/*} GOARCH=${t#*/} go build -trimpath -ldflags="-s -w" -o "dist/$o" .
done; ls -la dist
