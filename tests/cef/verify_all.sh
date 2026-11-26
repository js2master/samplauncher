#!/usr/bin/env bash
# 一条命令跑完 omp-cef Android 客户端的全部验收。
#
#   bash tests/cef/verify_all.sh [package]
#
# 四个阶段：
#   1. 离线逻辑断言（javac/java，不需要设备、不需要 gradle）
#   2. 编译 debug APK（C++ 侧 constexpr 断言也在这里执行）
#   2b. JNI 符号导出检查（native 方法忘了 extern "C" 会在这里当场拦下）
#   3. 装机 + 自动进游戏（启动器 -> 单机模式）
#   4. 设备端全量回归（协议 / 触摸 / IME / 按键 / World2D / WorldObject3D，含截图）
#
# 全程无人值守：不需要在手机上点任何东西（安装被系统拦截时会自动点"继续安装"）。
# 结束时打印 PASS/FAIL 汇总，日志落在 tests/cef/out/。
#
# 可用环境变量覆盖：
#   JAVA_HOME         JDK 路径（未设置时沿用当前环境）
#   LAUNCH_TAP1/2     启动器里"单机模式"与确认按钮的坐标（默认按 2388x1080）
#   BOOT_WAIT         进入游戏后的额外等待秒数（默认 35，GTA 加载慢）
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"
PKG="${1:-com.sampmobile.launcher}"

JAVA_HOME="${JAVA_HOME:-}"
LAUNCH_TAP1="${LAUNCH_TAP1:-186 838}"     # 启动器首屏 -> 单机模式
LAUNCH_TAP2="${LAUNCH_TAP2:-1783 690}"    # 版权/提示页 -> 确认
BOOT_WAIT="${BOOT_WAIT:-35}"
INSTALL_OK_TAP="${INSTALL_OK_TAP:-558 2215}"

mkdir -p "$OUT"
say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
adbq() { MSYS_NO_PATHCONV=1 adb "$@"; }
top_activity() {
    adbq shell dumpsys activity activities 2>/dev/null \
        | grep -m1 -o "topResumedActivity[^}]*" | head -1
}

FAILED=0
LOGIC_OK=0
BUILD_OK=0
JNI_OK=0
DEVICE_OK=0
REGRESSION_OK=0

# --------------------------------------------------------------- 0. 前置检查
say "0/4 检查设备"
if ! adbq devices | grep -q "device$"; then
    echo "FAIL  没有连接的 adb 设备"
    exit 2
fi
adbq shell pm path "$PKG" >/dev/null 2>&1 \
    && echo "  $PKG 已安装（会覆盖安装）" \
    || echo "  $PKG 未安装（将全新安装）"

# --------------------------------------------------------- 1. 离线逻辑断言
say "1/4 离线逻辑断言（不需要设备）"
if bash "$HERE/logic_tests/run_logic_tests.sh" > "$OUT/verify-logic.log" 2>&1; then
    LOGIC_OK=1
    grep -E "^[0-9]+ passed|LOGIC TESTS OK" "$OUT/verify-logic.log"
else
    echo "FAIL  离线逻辑断言未通过："
    grep "FAIL" "$OUT/verify-logic.log" | head -10
    FAILED=$((FAILED + 1))
fi

# ------------------------------------------------------------------ 2. 编译
say "2/4 编译 debug APK（C++ constexpr 断言在此执行）"
( cd "$ROOT" && JAVA_HOME="$JAVA_HOME" cmd //c "gradlew.bat assembleDebug --offline" ) \
    > "$OUT/verify-build.log" 2>&1
if grep -q "BUILD SUCCESSFUL" "$OUT/verify-build.log"; then
    BUILD_OK=1
    grep "BUILD SUCCESSFUL" "$OUT/verify-build.log"
    APK="$ROOT/app/build/outputs/apk/debug/samp.apk"
    echo "  apk $(stat -c %s "$APK" 2>/dev/null || echo '?') bytes, sha256 $(sha256sum "$APK" | cut -c1-16)"
else
    echo "FAIL  编译失败："
    grep -E "error:|FAILED" "$OUT/verify-build.log" | head -10
    FAILED=$((FAILED + 1))
fi

if [ $BUILD_OK -eq 1 ]; then
    # ------------------------------------------------ 2b. JNI 符号（防 mangle）
    say "2b JNI 符号导出检查"
    if bash "$HERE/jni_symbol_check.sh" | tee "$OUT/verify-jni.log"; then
        JNI_OK=1
    else
        echo "FAIL  CefManager 的 native 方法在 .so 里没有可解析的符号（见上）"
        FAILED=$((FAILED + 1))
    fi

    # ------------------------------------------------------- 3. 装机 + 进游戏
    say "3/4 装机并进入游戏"
    # 注意：adbq 关掉了 MSYS 路径转换，所以必须自己转成 Windows 路径，
    # 否则 adb 收到 /d/服务器/... 会报 "failed to stat"。
    adbq install -r "$(cygpath -w "$APK")" 2>&1 | tail -2

    # 部分 ROM 会把安装拦成"是否继续安装"对话框。
    sleep 3
    if top_activity | grep -q "PackageInstaller"; then
        echo "  安装被系统拦截，点「继续安装」$INSTALL_OK_TAP"
        adbq shell input tap $INSTALL_OK_TAP
        sleep 6
    fi

    adbq shell am start -n "$PKG/.SplashActivity" >/dev/null 2>&1
    sleep 14
    adbq shell input tap $LAUNCH_TAP1
    sleep 3
    adbq shell input tap $LAUNCH_TAP2
    echo "  等待 GTA 加载 ${BOOT_WAIT}s ..."
    sleep "$BOOT_WAIT"

    # 确认真的进了游戏 Activity；CefManager.install() 只在游戏里跑，
    # 不在游戏里的话所有 CEFDEBUG 广播都会被静默丢弃。
    IN_GAME=0
    for _ in 1 2 3 4 5 6; do
        if top_activity | grep -q "com.sampmobile.game.SAMP"; then IN_GAME=1; break; fi
        echo "  还没进游戏，再等 10s ..."
        sleep 10
    done
    if [ $IN_GAME -eq 0 ]; then
        echo "FAIL  没能进入游戏 Activity（$(top_activity)）"
        echo "      手动进到游戏画面后重跑本脚本的 4/4 段即可。"
        FAILED=$((FAILED + 1))
    else
        DEVICE_OK=1
        echo "  已进入游戏"
    fi
fi

# ------------------------------------------------------- 4. 设备端全量回归
if [ $DEVICE_OK -eq 1 ]; then
    say "4/4 设备端全量回归（协议 / 触摸 / IME / 按键 / World2D）"
    bash "$HERE/run_browser_test.sh" "$PKG" > "$OUT/verify-device.log" 2>&1
    DEVICE_RC=$?
    grep -E "PASS |FAIL |STAGE|probe failure" "$OUT/verify-device.log" | tail -45
    if [ $DEVICE_RC -eq 0 ] && ! grep -q "FAIL " "$OUT/verify-device.log"; then
        REGRESSION_OK=1
    else
        FAILED=$((FAILED + 1))
    fi
fi

# ------------------------------------------------------------------ 汇总
say "验收汇总"
[ $LOGIC_OK -eq 1 ]      && echo "  PASS  1. 离线逻辑断言（142 项）"        || echo "  FAIL  1. 离线逻辑断言"
[ $BUILD_OK -eq 1 ]      && echo "  PASS  2. 编译（含 C++ 静态断言）"      || echo "  FAIL  2. 编译"
[ $JNI_OK -eq 1 ]        && echo "  PASS  2b. JNI 符号导出（防 C++ mangle）" || echo "  FAIL  2b. JNI 符号导出"
[ $DEVICE_OK -eq 1 ]     && echo "  PASS  3. 装机并进入游戏"              || echo "  FAIL  3. 装机并进入游戏"
[ $REGRESSION_OK -eq 1 ] && echo "  PASS  4. 设备端全量回归"              || echo "  FAIL  4. 设备端全量回归"
echo
echo "  日志: $OUT/verify-logic.log  verify-build.log  verify-device.log"
echo "  截图: $OUT/1-created.png 1b-ime.png 1c-typed.png 2-hidden.png 3-visible.png 5-world2d.png"

if [ $FAILED -eq 0 ]; then
    echo
    echo "=== VERIFY ALL OK"
    exit 0
fi
echo
echo "=== VERIFY ALL FAILED ($FAILED 段)"
exit 1
