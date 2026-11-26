#!/usr/bin/env bash
# Builds and runs the omp-cef protocol known-answer tests and the transport
# end-to-end probe on a connected Android device.
#
#   tests/cef/run_kat.sh              # vectors + probe
#   tests/cef/run_kat.sh kat          # vectors only
#   tests/cef/run_kat.sh probe        # probe only
#
# Requires: NDK (env NDK or auto-detected under $ANDROID_HOME/ndk), adb,
# python3 + pynacl (vectors are regenerated on every run).

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CEF="$HERE/../../app/src/main/cpp/SAMPMOBILE/Cef"
MODE="${1:-all}"

: "${ANDROID_HOME:=D:/android-sdk}"
if [ -z "${NDK:-}" ]; then
    NDK="$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)"
fi
[ -d "$NDK" ] || { echo "NDK not found (set NDK=...)"; exit 1; }

BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
SYS="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
TARGET=aarch64-linux-android24

echo "== regenerating vectors =="
(cd "$HERE" && python gen_vectors.py)

echo "== compiling third-party objects =="
mkdir -p "$HERE/obj"
for f in tweetnacl ikcp aes blake2b-ref; do
    "$BIN/clang.exe" --target=$TARGET --sysroot="$SYS" -O2 -w \
        -c "$CEF/thirdparty/$f.c" -o "$HERE/obj/$f.o"
done

run_on_device() {  # $1 = local binary, $2 = remote name
    # Git Bash rewrites /data/local/tmp into a Windows path without
    # MSYS_NO_PATHCONV, but then adb also needs the local path in Windows form.
    local src; src="$(cygpath -w "$1" 2>/dev/null || printf '%s' "$1")"
    MSYS_NO_PATHCONV=1 adb push "$src" "/data/local/tmp/$2" >/dev/null
    MSYS_NO_PATHCONV=1 adb shell "chmod 755 /data/local/tmp/$2; /data/local/tmp/$2"
}

if [ "$MODE" = all ] || [ "$MODE" = kat ]; then
    echo "== building KAT =="
    "$BIN/clang++.exe" --target=$TARGET --sysroot="$SYS" -std=c++20 -O2 -w \
        -static-libstdc++ "$HERE/cef_kat_main.cpp" \
        "$CEF/CefProtocol.cpp" "$CEF/CefCrypto.cpp" "$CEF/CefJson.cpp" "$CEF/CefPak.cpp" "$HERE"/obj/*.o \
        -lz -o "$HERE/cef_kat"
    echo "== running KAT on device =="
    run_on_device "$HERE/cef_kat" cef_kat
fi

if [ "$MODE" = all ] || [ "$MODE" = probe ]; then
    echo "== building E2E probe =="
    "$BIN/clang++.exe" --target=$TARGET --sysroot="$SYS" -std=c++20 -O2 -w \
        -static-libstdc++ "$HERE/cef_probe_main.cpp" \
        "$CEF/CefProtocol.cpp" "$CEF/CefCrypto.cpp" "$CEF/CefNetwork.cpp" \
        "$HERE"/obj/*.o -llog -o "$HERE/cef_probe"
    echo "== running E2E probe on device =="
    run_on_device "$HERE/cef_probe" cef_probe
fi

echo "done"
