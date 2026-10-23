#!/usr/bin/env bash
# Builds bin/libMP32.so (armeabi-v7a) and bin/libMP64.so (arm64-v8a).
# Usage: NDK=/path/to/android-ndk ./build.sh
set -e
cd "$(dirname "$0")"
NDK="${NDK:-${ANDROID_NDK_HOME:-$HOME/android-ndk-r26d}}"
"$NDK/ndk-build" NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk -j"$(nproc)"
mkdir -p bin
cp libs/armeabi-v7a/libMP32.so bin/libMP32.so
cp libs/arm64-v8a/libMP64.so  bin/libMP64.so
echo "Done:"; ls -la bin/*.so
