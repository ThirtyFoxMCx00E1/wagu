#!/bin/bash
# Builds Moquil.apk (no Gradle): aapt2 + javac + d8 + zipalign + apksigner, like textdraweditor/build.sh.
#   SDK=~/android-sdk JAVA_HOME=~/jdk KEYSTORE=... KEYPASS=... bash build.sh
# The GitHub workflow "Moquil" (.github/workflows/moquil.yml) runs this.
set -e
cd "$(dirname "$0")"
SDK=${SDK:-${ANDROID_HOME:-$HOME/android-sdk}}; BTV=${BUILD_TOOLS:-34.0.0}; BT=$SDK/build-tools/$BTV; JAR=$SDK/platforms/android-34/android.jar
[ -n "$JAVA_HOME" ] && export PATH=$JAVA_HOME/bin:$PATH
OUT=${OUT:-Moquil.apk}
rm -rf build; mkdir -p build/res build/classes build/dex
$BT/aapt2 compile --dir res -o build/res/res.zip
$BT/aapt2 link -I $JAR --manifest AndroidManifest.xml -A assets -o build/base.apk build/res/res.zip --min-sdk-version 21 --target-sdk-version 34
javac --release 8 -Xlint:-options -cp $JAR -d build/classes $(find src -name '*.java')
$BT/d8 --min-api 21 --lib $JAR --output build/dex $(find build/classes -name '*.class')
cp build/base.apk build/unsigned.apk
(cd build/dex && zip -q ../unsigned.apk classes.dex)
$BT/zipalign -f -p 4 build/unsigned.apk build/aligned.apk
$BT/apksigner sign --ks "$KEYSTORE" --ks-pass env:KEYPASS --out "$OUT" build/aligned.apk
$BT/apksigner verify "$OUT" && ls -la "$OUT"
