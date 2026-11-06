#!/usr/bin/env bash
# MP 4.12: builds the open.mp cross-play component for the open.mp 1.6.7 server (32-bit x86):
#   out/linux/mp-crossplay.so   -> <open.mp server>/components/   (Linux omp-server)
#   (Windows omp-server.exe: not yet, the MP server code is Linux / Android only - use a Linux VPS or WSL)
# Needs: zig 0.13+ (one cross compiler for both: https://ziglang.org/download/) and the
# open.mp SDK at the server's commit (fetched into crossplay/open.mp-sdk the first time).
#   ZIG=/path/to/zig bash crossplay/build.sh [linux]
set -euo pipefail
cd "$(dirname "$0")"
ROOT=$(cd .. && pwd)
ZIG="${ZIG:-zig}"
WHAT="${1:-all}"
SDK=open.mp-sdk
SDK_COMMIT=08cc69a32d96c0eb8fe063d26c78259e54ca853f   # = open.mp v1.6.7.3218 (open.mp/SDK submodule)

fetch() {   # repo commit dir
    [ -f "$3/.ok" ] && return 0
    rm -rf "$3"; mkdir -p "$3"
    curl -fsSL "https://codeload.github.com/$1/tar.gz/$2" | tar xz -C "$3" --strip-components=1
    touch "$3/.ok"
}
fetch openmultiplayer/open.mp-sdk $SDK_COMMIT $SDK
fetch g-truc/glm bf71a834948186f4097caa076cd2663c69a10e1e $SDK/lib/glm
fetch martinus/robin-hood-hashing 9145f963d80d6a02f0f96a47758050a89184a3ed $SDK/lib/robin-hood-hashing
fetch martinmoene/span-lite cbb9c3c5162a0d7018c7b3e053153a04d4fbbbb9 $SDK/lib/span-lite
fetch martinmoene/string-view-lite 3d2a4a7ebcc5dbdd55bd00026075c574a13c861b $SDK/lib/string-view-lite

MBED=$ROOT/third_party/mbedtls
AMX=$ROOT/third_party/pawn-amx
UNRAR=$ROOT/third_party/unrar
AMX_FLAGS="-DHAVE_STDINT_H -DAMX_ANSIONLY -DAMX_NODYNALOAD -DAMX_DONT_RELOCATE -DFLOATPOINT -Dstricmp=strcasecmp -DNDEBUG"
SDK_FLAGS="-I$SDK/include -I$SDK/lib/glm -I$SDK/lib/robin-hood-hashing/src/include -I$SDK/lib/span-lite/include -I$SDK/lib/string-view-lite/include \
  -DGLM_FORCE_QUAT_DATA_WXYZ -DNOMINMAX -Dnssv_CONFIG_SELECT_STRING_VIEW=nssv_STRING_VIEW_NONSTD -Dspan_CONFIG_SELECT_SPAN=span_SPAN_NONSTD -DGLM_FORCE_SSE2 -msse2 -mfpmath=sse"
UNRAR_SRC="rar strlist strfn pathfn smallfn global file filefn filcreat archive arcread unicode system crypt crc rawread encname resource match timefn rdwrfn consio options errhnd rarvm secpassword rijndael getbits sha1 sha256 blake2s hash extinfo extract volume list find unpack headers threadpool rs16 cmddata ui largepage filestr scantree dll qopen"
NET="server lan online authcrypto samp_import"

build() {   # name target ext extra-cflags extra-ldflags
    local N=$1 T=$2 EXT=$3 CF=$4 LF=$5 O=obj/$1 J=${JOBS:-2}
    mkdir -p $O/mbed $O/samp $O/unrar $O/net out/$N
    echo "== $N ($T)"
    for f in $MBED/library/*.c; do o=$O/mbed/$(basename $f .c).o; [ -f $o ] || echo "cc -I$MBED/include -I$MBED/library -c $f -o $o"; done > $O/jobs
    for f in amx amxcore float amxstring amxaux; do o=$O/samp/$f.o; [ -f $o ] || echo "cc $AMX_FLAGS -c $AMX/$f.c -o $o"; done >> $O/jobs
    [ -f $O/samp/sqlite3.o ] || echo "cc -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -c $ROOT/third_party/sqlite/sqlite3.c -o $O/samp/sqlite3.o" >> $O/jobs
    [ -f $O/samp/puff.o ] || echo "cc -c $ROOT/third_party/puff/puff.c -o $O/samp/puff.o" >> $O/jobs
    for f in $UNRAR_SRC; do o=$O/unrar/$f.o; [ -f $o ] || echo "cxx -std=c++11 -w -DMP_NO_UNRAR_SSE -DRARDLL -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -c $UNRAR/$f.cpp -o $o"; done >> $O/jobs
    for f in $NET; do echo "cxx -std=c++17 -I$ROOT/src -I$MBED/include $AMX_FLAGS -DMP_CROSSPLAY -c $ROOT/src/net/$f.cpp -o $O/net/$f.o"; done >> $O/jobs
    echo "cxx -std=c++17 -I$ROOT/src -I$MBED/include $AMX_FLAGS -c bridge_api.cpp -o $O/net/bridge_api.o" >> $O/jobs
    echo "cxx -std=c++17 $SDK_FLAGS -c mp_crossplay.cpp -o $O/net/mp_crossplay.o" >> $O/jobs
    xargs -P $J -I{} env ZIG="$ZIG" T=$T CF="$CF" bash -c 'set -e; cc() { "$ZIG" cc -target $T -O2 -fPIC $CF "$@"; }; cxx() { "$ZIG" c++ -target $T -O2 -fPIC $CF "$@"; }; {}' < $O/jobs
    "$ZIG" c++ -target $T -shared -O2 -s -o out/$N/mp-crossplay.$EXT $O/net/*.o $O/unrar/*.o $O/samp/*.o $O/mbed/*.o $LF
    ls -la out/$N/mp-crossplay.$EXT
}

case "$WHAT" in linux|all) build linux x86-linux-gnu.2.17 so "-pthread" "-pthread -ldl -lm";; esac
case "$WHAT" in windows) echo "Windows (.dll): not yet - the MP server code is Linux/Android only (netinet, sys/resource). Run the open.mp server on Linux (a VPS, WSL) for cross-play."; exit 1;; esac
