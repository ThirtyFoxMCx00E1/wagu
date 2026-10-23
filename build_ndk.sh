#!/usr/bin/env bash
# Builds the Android multiplayer native libraries:
#   bin/libMP32.so  (armeabi-v7a, 32-bit)
#   bin/libMP64.so  (arm64-v8a, 64-bit)
#
# Requirements: Android NDK. Set NDK_HOME or ANDROID_NDK_HOME first.
set -e

NDK="${NDK_HOME:-${ANDROID_NDK_HOME:-}}"
if [ -z "$NDK" ]; then
    echo "Error: set NDK_HOME or ANDROID_NDK_HOME to your Android NDK directory."
    exit 1
fi

ABI="$1"
if [ -z "$ABI" ]; then
    ABI="armeabi-v7a arm64-v8a"
fi

echo "Building Android MP libraries for: $ABI"
ndk-build -j8 NDK_PROJECT_PATH=. APP_BUILD_SCRIPT=./Android.mk NDK_APPLICATION_MK=./Application.mk APP_ABI="$ABI"

mkdir -p bin

if [ -d "libs/armeabi-v7a" ]; then
    cp -f libs/armeabi-v7a/*.so bin/libMP32.so
    echo "Created bin/libMP32.so"
fi

if [ -d "libs/arm64-v8a" ]; then
    cp -f libs/arm64-v8a/*.so bin/libMP64.so
    echo "Created bin/libMP64.so"
fi

echo "Done. Binary libraries are in bin/."
