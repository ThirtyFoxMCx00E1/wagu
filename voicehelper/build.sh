#!/bin/bash
# Builds MP-Voice.apk (no Gradle): aapt2 + javac + d8 + zipalign + apksigner.
#   SDK=~/android-sdk JAVA_HOME=~/jdk KEYSTORE=... KEYPASS=... bash build.sh
set -e
cd "$(dirname "$0")"
SDK=${SDK:-$HOME/android-sdk}; BT=$SDK/build-tools/34.0.0; JAR=$SDK/platforms/android-34/android.jar
export PATH=${JAVA_HOME:-$HOME/jdk}/bin:$PATH
rm -rf build; mkdir -p build/res build/classes build/dex
$BT/aapt2 compile --dir res -o build/res/res.zip
$BT/aapt2 link -I $JAR --manifest AndroidManifest.xml -o build/base.apk build/res/res.zip --min-sdk-version 21 --target-sdk-version 34
javac --release 8 -Xlint:-options -cp $JAR -d build/classes $(find src -name '*.java')
$BT/d8 --min-api 21 --lib $JAR --output build/dex $(find build/classes -name '*.class')
cp build/base.apk build/unsigned.apk
(cd build/dex && zip -q ../unsigned.apk classes.dex)
$BT/zipalign -f -p 4 build/unsigned.apk build/aligned.apk
$BT/apksigner sign --ks "$KEYSTORE" --ks-pass env:KEYPASS --out MP-Voice.apk build/aligned.apk
$BT/apksigner verify MP-Voice.apk && ls -la MP-Voice.apk
