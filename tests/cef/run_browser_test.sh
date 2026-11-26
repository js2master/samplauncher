#!/usr/bin/env bash
# On-device verification of the omp-cef Android client (stages 3-6).
#
# What it does:
#   1. builds tests/cef/cef_server.cpp for arm64 and pushes it
#   2. packs tests/cef/testpage into a real resource .pak
#      (tests/cef/make_pak.py: zip + OCEF2/AES-128-CBC, same format the
#      server plugin produces) and pushes it for the test server to stream
#   3. wipes the app's CEF cache so the download really happens
#   4. starts the test server on 127.0.0.1:17900
#   5. drives the app's CEF debug entry (broadcast) to join that server
#   6. walks the browser lifecycle and screenshots every phase
#   7. injects real input at the right moments (stage 6):
#        `input keyevent 29` (A, whitelisted) / `54` (Z, not whitelisted)
#        tap the page input -> IME -> `input text hello`
#        `input keyevent 4` (BACK) -> focus released
#        app-side chat keyboard open/close (ChatInputState uplink)
#   8. prints the server log with the PASS/FAIL assertions plus the client-side
#      touch/IME/back checks.
#
# Requirements: adb device with the DEBUG build installed, NDK, python3 with
# the `cryptography` package.
#   bash tests/cef/run_browser_test.sh [package]

set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CEF="$HERE/../../app/src/main/cpp/SAMPMOBILE/Cef"
PKG="${1:-com.sampmobile.launcher}"
OUT="$HERE/out"

# Device pixels of the page's IME input box (2388x1080 landscape). The box spans
# the panel width and is ~150px tall, so a rough centre hit is enough.
IME_TAP_X="${IME_TAP_X:-1300}"
IME_TAP_Y="${IME_TAP_Y:-577}"

: "${ANDROID_HOME:=D:/android-sdk}"
if [ -z "${NDK:-}" ]; then
    NDK="$(ls -d "$ANDROID_HOME"/ndk/* 2>/dev/null | sort -V | tail -1)"
fi
[ -d "$NDK" ] || { echo "NDK not found (set NDK=...)"; exit 1; }

BIN="$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin"
SYS="$NDK/toolchains/llvm/prebuilt/windows-x86_64/sysroot"
TARGET=aarch64-linux-android24

adbq() { MSYS_NO_PATHCONV=1 adb "$@"; }
winp() { cygpath -w "$1" 2>/dev/null || printf '%s' "$1"; }

mkdir -p "$OUT"

echo "== device check =="
adbq devices | grep -q "device$" || { echo "no adb device"; exit 1; }
adbq shell pm path "$PKG" >/dev/null 2>&1 || { echo "$PKG not installed"; exit 1; }
DEBUGGABLE=$(adbq shell "run-as $PKG id" 2>&1 | tr -d '\r')
case "$DEBUGGABLE" in
    *uid=*) : ;;
    *) echo "package is not debuggable - install the debug build first"; exit 1 ;;
esac
echo "package $PKG is debuggable"

echo "== compiling third-party objects =="
mkdir -p "$HERE/obj"
for f in tweetnacl ikcp aes blake2b-ref; do
    [ -f "$HERE/obj/$f.o" ] || "$BIN/clang.exe" --target=$TARGET --sysroot="$SYS" -O2 -w \
        -c "$CEF/thirdparty/$f.c" -o "$HERE/obj/$f.o"
done

echo "== building test server =="
"$BIN/clang++.exe" --target=$TARGET --sysroot="$SYS" -std=c++20 -O2 -w \
    -static-libstdc++ "$HERE/cef_server.cpp" \
    "$CEF/CefProtocol.cpp" "$CEF/CefCrypto.cpp" "$HERE"/obj/*.o \
    -o "$HERE/cef_server" || exit 1

echo "== building the resource pak =="
PAK_JSON=$(python "$HERE/make_pak.py" "$HERE/testpage" "$OUT/test.pak" 2>"$OUT/pak.log")
PAK_SIZE=$(printf '%s' "$PAK_JSON" | python -c "import json,sys; d=json.load(sys.stdin); print(d['size'])")
PAK_SHA=$(printf '%s' "$PAK_JSON" | python -c "import json,sys; d=json.load(sys.stdin); print(d['sha256'])")
echo "   $OUT/test.pak: $PAK_SIZE bytes, sha256=$PAK_SHA"
[ -s "$OUT/test.pak" ] || { echo "PAK BUILD FAILED"; cat "$OUT/pak.log"; exit 1; }

echo "== pushing assets =="
adbq push "$(winp "$HERE/cef_server")" /data/local/tmp/cef_server >/dev/null
adbq push "$(winp "$OUT/test.pak")" /data/local/tmp/cef_test.pak >/dev/null
adbq shell "chmod 755 /data/local/tmp/cef_server"

# Stage 5: the client must download. Drop the whole CEF tree (cache + the
# extracted resources) so the manifest check cannot be satisfied locally.
# KEEP_CACHE=1 instead keeps the previous run's cache, which exercises the
# second-join path: no RequestFiles, page served from the existing files.
if [ "${KEEP_CACHE:-0}" = "1" ]; then
    echo "keeping app cef dir (cache-hit run)"
    SERVER_ARGS="--cached"
else
    adbq shell "run-as $PKG sh -c 'rm -rf files/cef'" >/dev/null 2>&1 || true
    echo "app cef dir wiped"
    SERVER_ARGS=""
fi

echo "== starting test server =="
# Hosted by a host-side adb shell: device-side `nohup &` gets reaped when the
# adb shell session ends, which made the server die mid-test.
adbq shell "pkill -f /data/local/tmp/cef_server" 2>/dev/null
sleep 1
( MSYS_NO_PATHCONV=1 adb shell "/data/local/tmp/cef_server /data/local/tmp/cef_test.pak $SERVER_ARGS" > "$OUT/server.log" 2>&1 & )
sleep 2
grep -q "listening on" "$OUT/server.log" && echo "   server up" || { echo "SERVER FAILED"; cat "$OUT/server.log"; exit 1; }

shot() {  # $1 = output name
    adbq exec-out screencap -p > "$OUT/$1.png"
    echo "   screenshot -> $OUT/$1.png"
}

echo "== driving the app =="
# The CEF logs are snapshotted after every step instead of streamed: this
# device's adb server restarts on its own, which kills a long-lived
# `logcat` reader, and the game rotates the ring buffer within a minute.
adbq logcat -c 2>/dev/null || true
: > "$OUT/app.log"
snap_log() {
    adbq logcat -d -v brief 2>/dev/null \
        | grep -E "CefManager|CefWebView|CefBrowser|CefSession|CefJs|CefOverlay|CefResources|CefResourceProvider|CEF_RESOURCE|cef:|AndroidRuntime" \
        >> "$OUT/app.log" 2>/dev/null || true
}
# The legacy WebOverlay panel must not cover the CEF browser during the test.
adbq shell am broadcast -a com.sampmobile.OVERLAY -p "$PKG" --es action hide >/dev/null 2>&1 || true
# Drop any browser left over from a previous run so CreateBrowser(1) succeeds.
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action destroy --ei id 1 >/dev/null 2>&1 || true
# Reset transport state first: a stale session from an earlier run leaves the
# socket in a state where the new join cannot send.
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action disconnect >/dev/null 2>&1 || true
sleep 1
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" \
    --es action connect --es host 127.0.0.1 --ei port 17900 --ei playerid 1 >/dev/null
echo "   connect broadcast sent"
# test:click arrives only when the *page* is the one receiving the touch, so the
# count in the live server log is a wire-level touch probe.
clicks() {
    local n
    n=$(grep -c "ClientEmitEvent name=test:click" "$OUT/server.log" 2>/dev/null)
    echo "${n:-0}"
}
TAP_BTN_X="${TAP_BTN_X:-294}"
TAP_BTN_Y="${TAP_BTN_Y:-747}"

sleep 10
echo "[t=10s] CreateBrowser + test:hello/command should be on screen"
shot 1-created
snap_log

# ---------------------------------------------------------------- stage 6: input
# (a) the page must be the one receiving touches: a tap on the 甲 button has to
#     show up as test:click on the wire.
C0=$(clicks)
adbq shell input tap "$TAP_BTN_X" "$TAP_BTN_Y" >/dev/null 2>&1 || true
sleep 2
C1=$(clicks)
echo "   clicks: $C0 -> $C1 (tap on 甲 while the page has focus)"
snap_log

# (b) real Android key events: KEYCODE_A (29) is whitelisted by EnableKey so it
#     must reach the server as Windows VK 0x41; KEYCODE_Z (54) is NOT whitelisted
#     and must stay silent.
adbq shell input keyevent 29 >/dev/null 2>&1 || true
sleep 1
adbq shell input keyevent 54 >/dev/null 2>&1 || true
sleep 1
snap_log

# (c) tap the page's text input -> Chromium requests the IME (needs a gesture).
adbq shell input tap "$IME_TAP_X" "$IME_TAP_Y" >/dev/null 2>&1 || true
sleep 3
echo "[t=14s] IME should be up for the page input"
shot 1b-ime
snap_log
adbq shell dumpsys input_method > "$OUT/ime.txt" 2>/dev/null || true
grep -q "mInputShown=true" "$OUT/ime.txt" && echo "   IME shown = true" || echo "   IME shown = false"

# (d) type through the system input path: the text lands in the page input and
#     the page echoes it with cef.emit('test:input', ...).
adbq shell input text "hello" >/dev/null 2>&1 || true
sleep 2
echo "[t=17s] typed text should appear in the page input"
shot 1c-typed
snap_log

# (e) BACK must release the browser focus; the same tap then has to reach the
#     game instead of the page, i.e. test:click must NOT increase.
#     With the IME up the first BACK is swallowed by the input method itself
#     (standard Android behaviour), so a second one is sent only in that case.
if adbq shell dumpsys input_method 2>/dev/null | grep -q "mInputShown=true"; then
    echo "   IME is up -> first BACK dismisses it, second one releases the page"
    adbq shell input keyevent 4 >/dev/null 2>&1 || true
    sleep 1
fi
adbq shell input keyevent 4 >/dev/null 2>&1 || true
sleep 2
C2=$(clicks)
adbq shell input tap "$TAP_BTN_X" "$TAP_BTN_Y" >/dev/null 2>&1 || true
sleep 2
C3=$(clicks)
echo "   clicks after BACK: $C2 -> $C3 (passthrough expected)"
snap_log

# (f) game chat input open/close through the app's own UI.showKeyboard path -
#     the same call the native game code makes when the player taps the chat box.
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action chatstate --ei on 1 >/dev/null 2>&1 || true
sleep 2
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action chatstate --ei on 0 >/dev/null 2>&1 || true
sleep 2
snap_log

sleep 1
echo "[t=30s] SetBrowserVisible(false) should have hidden it"
shot 2-hidden
snap_log

sleep 4
echo "[t=35s] SetBrowserVisible(true) should have shown it again"
shot 3-visible
snap_log

sleep 6
echo "[t=41s] DestroyBrowser should have removed it"
shot 4-destroyed
snap_log

# ------------------------------------------------- controlsChat gate (negative)
# A focused page that declared controlsChat=1 must stop the game's own chat box
# from opening (PC FocusManager::ShouldBlockChat). Observable on the wire: the
# server asserts ChatInputState(open) is seen exactly once, from step (f).
echo "== controlsChat gate =="
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action create \
    --ei id 9 --es url "http://cef/test/index.html" --ei focused 1 --ei controls 1 >/dev/null 2>&1 || true
sleep 5
adbq shell input tap "$IME_TAP_X" "$IME_TAP_Y" >/dev/null 2>&1 || true
sleep 2
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action chatstate --ei on 1 >/dev/null 2>&1 || true
sleep 2
snap_log
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action chatstate --ei on 0 >/dev/null 2>&1 || true
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action destroy --ei id 9 >/dev/null 2>&1 || true

# ------------------------------------------- stage 7 batch 2: WorldObject3D
# Single player has no SA-MP object pool, so AttachBrowserToObject(objectId) has
# nothing to resolve there. This debug entry spawns a real game object in front
# of the player and runs the exact same texture/material path, which is the part
# that can actually break (RW raster upload + material swap).
echo "== world object 3D (stage 7 batch 2) =="
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action worldobj \
    --ei id 13 --ei model 19377 --ei dist 6 >/dev/null 2>&1 || true
sleep 9
shot 6-worldobj
WOBJ=$(grep -c "debug:worldobj" "$OUT/server.log" 2>/dev/null || true)
echo "   debug:worldobj reports on the wire: ${WOBJ:-0}"
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action destroy --ei id 13 >/dev/null 2>&1 || true

# ------------------------------------------------------------ stage 7: World2D
# The server created browser 20 as a World2D page (own px viewport, world
# anchored). That path is covered by the server assertions above. This part
# covers the part only the device can prove: the client projects a *known*
# world point (local player + 1.5m) to screen space and hits it.
taps() {
    local n
    n=$(grep -c "ClientEmitEvent name=test:tap" "$OUT/server.log" 2>/dev/null)
    echo "${n:-0}"
}
echo "== world2d (stage 7) =="
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action world2d \
    --ei id 12 --ei above 150 --ei w 512 --ei h 256 >/dev/null 2>&1 || true
sleep 3
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action focus --ei id 12 --ei on 1 >/dev/null 2>&1 || true
sleep 3
snap_log
shot 5-world2d
# Where did the client actually put the page? It reports its own projection
# over the wire (`debug:world2d`), because the device log on this ROM is not
# reliably readable (logd keeps restarting). server.log is a local file.
W2D_LINE=$(grep "debug:world2d id=12" "$OUT/server.log" 2>/dev/null | tail -1)
W2D_POS=$(printf '%s' "$W2D_LINE" | sed -n 's/.*pos=(\([0-9-]*\),\([0-9-]*\)).*/\1 \2/p')
W2D_ON=$(printf '%s' "$W2D_LINE" | sed -n 's/.*on=\([01]\).*/\1/p')
if [ -n "$W2D_POS" ] && [ "${W2D_ON:-0}" = "1" ]; then
    set -- $W2D_POS
    W2D_X=$(($1 + 256))
    W2D_Y=$(($2 + 128))
else
    echo "   no on-screen projection reported by the client - using screen centre"
    W2D_X=1194
    W2D_Y=540
fi
# Keep the tap inside the panel and on the screen.
[ "$W2D_X" -lt 40 ] && W2D_X=40
[ "$W2D_X" -gt 2340 ] && W2D_X=2340
[ "$W2D_Y" -lt 40 ] && W2D_Y=40
[ "$W2D_Y" -gt 1040 ] && W2D_Y=1040
# A point that is certainly outside a 512x256 rect around the projected anchor.
if [ "$W2D_X" -gt 1500 ]; then
    OUT_X=$((W2D_X - 900))
else
    OUT_X=$((W2D_X + 900))
fi
OUT_Y=$W2D_Y
[ "$OUT_X" -gt 2300 ] && OUT_X=2300
echo "   projected rect centre: ($W2D_X,$W2D_Y), outside probe: ($OUT_X,$OUT_Y)"
T0=$(taps)
adbq shell input tap "$W2D_X" "$W2D_Y" >/dev/null 2>&1 || true
sleep 3
T1=$(taps)
adbq shell input tap "$OUT_X" "$OUT_Y" >/dev/null 2>&1 || true
sleep 3
T2=$(taps)
echo "   taps: $T0 -> $T1 (inside rect) -> $T2 (outside rect)"
snap_log
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action destroy --ei id 12 >/dev/null 2>&1 || true


echo "== waiting for the server script to finish =="
sleep 20


echo "== touch / IME / world2d probes =="
FAILED=0
if [ "$C1" -gt "$C0" ]; then
    echo "PASS  focused page consumed the tap (test:click on the wire)"
else
    echo "FAIL  focused page did not receive the tap ($C0 -> $C1)"
    FAILED=$((FAILED + 1))
fi
if [ "$C3" -eq "$C2" ]; then
    echo "PASS  after BACK the tap went to the game (no test:click)"
else
    echo "FAIL  page kept receiving touches after BACK ($C2 -> $C3)"
    FAILED=$((FAILED + 1))
fi
if grep -q "mInputShown=true" "$OUT/ime.txt" 2>/dev/null; then
    echo "PASS  IME was shown for the page input"
else
    echo "FAIL  IME was not shown (see $OUT/ime.txt)"
    FAILED=$((FAILED + 1))
fi
if [ "$T1" -gt "$T0" ]; then
    echo "PASS  tap inside the projected World2D rect reached the page (test:tap)"
else
    echo "FAIL  tap inside the projected World2D rect did not reach the page ($T0 -> $T1)"
    FAILED=$((FAILED + 1))
fi
if [ "$T2" -eq "$T1" ]; then
    echo "PASS  tap outside the World2D rect went to the game (no test:tap)"
else
    echo "FAIL  World2D page swallowed a tap outside its rect ($T1 -> $T2)"
    FAILED=$((FAILED + 1))
fi
echo "   ($FAILED probe failure(s))"

echo "== server log =="
cat "$OUT/server.log"

snap_log
echo "== app log (CEF) =="
cat "$OUT/app.log" 2>/dev/null | tr -d '\r' | tail -160

echo "== extracted resource tree on device =="
adbq shell "run-as $PKG sh -c 'ls -lR files/cef/resources files/cef/cache 2>/dev/null'" | tr -d '\r' | head -30

adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action disconnect >/dev/null 2>&1 || true
echo "== done =="
