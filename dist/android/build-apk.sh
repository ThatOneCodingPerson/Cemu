#!/usr/bin/env bash
# One-shot Android APK build for Cemu on Linux / WSL / macOS (mirrors .github/workflows/deploy_release_android.yml).
# Windows users: use build-apk.ps1 / build-apk.cmd instead (it bootstraps everything itself).
#
# Usage: dist/android/build-apk.sh [dev|release|debug] [--install] [--clean]
# Needs: git, JDK 17-21 (JAVA_HOME), Android SDK (ANDROID_HOME) with cmdline-tools; NDK/platform/CMake are
# installed via sdkmanager when missing. Optional: gettext (msgunfmt) for translations.
set -euo pipefail

BUILD_TYPE="dev"
INSTALL=0
CLEAN=0
for arg in "$@"; do
	case "$arg" in
		dev|release|debug) BUILD_TYPE="$arg" ;;
		--install) INSTALL=1 ;;
		--clean) CLEAN=1 ;;
		*) echo "unknown argument: $arg" >&2; exit 1 ;;
	esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
OUT_DIR="$SCRIPT_DIR/output"
GRADLE_FILE="$REPO_ROOT/src/android/app/build.gradle.kts"
mkdir -p "$OUT_DIR/logs"

: "${ANDROID_HOME:=${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
export ANDROID_HOME ANDROID_SDK_ROOT="$ANDROID_HOME" VCPKG_DISABLE_METRICS=1
[ -n "${JAVA_HOME:-}" ] || { echo "JAVA_HOME is not set (need JDK 17-21; CI uses 21)" >&2; exit 1; }

NDK_VERSION="$(sed -n 's/.*ndkVersion *= *"\([^"]*\)".*/\1/p' "$GRADLE_FILE" | head -1)"
COMPILE_SDK="$(sed -n 's/.*compileSdk *= *\([0-9]*\).*/\1/p' "$GRADLE_FILE" | head -1)"
SDKMANAGER="$ANDROID_HOME/cmdline-tools/latest/bin/sdkmanager"
missing=()
[ -f "$ANDROID_HOME/ndk/$NDK_VERSION/source.properties" ] || missing+=("ndk;$NDK_VERSION")
[ -d "$ANDROID_HOME/platforms/android-$COMPILE_SDK" ] || missing+=("platforms;android-$COMPILE_SDK")
[ -x "$ANDROID_HOME/platform-tools/adb" ] || missing+=("platform-tools")
if [ ${#missing[@]} -gt 0 ]; then
	[ -x "$SDKMANAGER" ] || { echo "Missing ${missing[*]} and no sdkmanager at $SDKMANAGER" >&2; exit 1; }
	yes | "$SDKMANAGER" --licenses >/dev/null || true
	"$SDKMANAGER" --install "${missing[@]}"
fi
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/$NDK_VERSION"

git -C "$REPO_ROOT" submodule update --init --recursive

# Translations (CI step): turn bin/resources/*/cemu.mo into .po files the app loads
if command -v msgunfmt >/dev/null 2>&1; then
	for mo_file in "$REPO_ROOT"/bin/resources/*/cemu.mo; do
		lang="$(basename "$(dirname "$mo_file")")"
		po_file="$REPO_ROOT/src/android/app/src/main/assets/translations/$lang/cemu.po"
		mkdir -p "$(dirname "$po_file")"
		msgunfmt "$mo_file" -o "$po_file" || echo "warning: could not convert $mo_file"
	done
else
	echo "note: gettext (msgunfmt) not installed; building without native UI translations"
fi

cd "$REPO_ROOT/src/android"
if [ "$CLEAN" = 1 ]; then rm -rf app/build app/.cxx build; fi
TASK=":app:assemble${BUILD_TYPE^}"
LOG="$OUT_DIR/logs/build-$BUILD_TYPE-$(date +%Y%m%d-%H%M%S).log"
echo "==> ./gradlew $TASK (log: $LOG)"
./gradlew --console=plain "$TASK" 2>&1 | tee "$LOG"

APK_DIR="$REPO_ROOT/src/android/app/build/outputs/apk/$BUILD_TYPE"
APK="$(ls -t "$APK_DIR"/*.apk | head -1)"
VERSION="$(sed -n 's/.*"versionName": *"\([^"]*\)".*/\1/p' "$APK_DIR/output-metadata.json" | head -1)"
VERSION="${VERSION%-$BUILD_TYPE}"
HASH="$(git -C "$REPO_ROOT" rev-parse --short HEAD)"
git -C "$REPO_ROOT" diff --quiet HEAD -- src || HASH="$HASH-dirty"
FINAL="$OUT_DIR/Cemu-$VERSION-$HASH-$BUILD_TYPE.apk"
cp -f "$APK" "$FINAL"
cp -f "$APK" "$OUT_DIR/Cemu-latest-$BUILD_TYPE.apk"
echo "============================================================"
echo " APK READY: $FINAL"
echo "============================================================"

if [ "$INSTALL" = 1 ]; then
	ADB="$ANDROID_HOME/platform-tools/adb"
	for serial in $("$ADB" devices | awk 'NR>1 && $2=="device" {print $1}'); do
		echo "==> installing on $serial"
		"$ADB" -s "$serial" install -r "$FINAL"
	done
fi
