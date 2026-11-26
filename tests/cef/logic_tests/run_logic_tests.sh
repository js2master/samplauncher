#!/usr/bin/env bash
# CEF 输入路径纯逻辑的离线回归（不需要手机 / 游戏 / gradle / 网络）。
#
#   bash tests/cef/logic_tests/run_logic_tests.sh
#
# 做法：把产品源码里的 CefKeys.java 与 CefLogic.java 原样复制到临时 ASCII 目录
# （中文路径会干扰 javac），用 android.jar 只做**编译期** classpath，**运行时故意
# 不带 android.jar** —— 这样一旦有人往这两个类里引入 android 的运行时类型，
# 测试会直接 NoClassDefFoundError，等于"纯逻辑"这条约束被破坏时立刻报警。
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
CEF_DIR="$ROOT/app/src/main/java/com/sampmobile/game/ui/cef"
TEST_SRC="$ROOT/tests/cef/logic_tests/LogicTests.java"

# --- 找 javac -----------------------------------------------------------------
JAVAC=""
JAVA=""
if [ -n "${JAVA_HOME:-}" ] && [ -x "$JAVA_HOME/bin/javac.exe" ]; then
    JAVAC="$JAVA_HOME/bin/javac.exe"
    JAVA="$JAVA_HOME/bin/java.exe"
elif [ -x "D:/服务器/DAYZSA/_build_env/jdk/jdk-21.0.2/bin/javac.exe" ]; then
    JAVAC="D:/服务器/DAYZSA/_build_env/jdk/jdk-21.0.2/bin/javac.exe"
    JAVA="D:/服务器/DAYZSA/_build_env/jdk/jdk-21.0.2/bin/java.exe"
elif command -v javac >/dev/null 2>&1; then
    JAVAC="$(command -v javac)"
    JAVA="$(command -v java)"
else
    echo "FAIL  javac not found (set JAVA_HOME)" >&2
    exit 2
fi

# --- 找 android.jar（取最高 platform） ----------------------------------------
ANDROID_JAR=""
SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-D:/android-sdk}}"
for d in "$SDK"/platforms/android-*/; do
    [ -f "$d/android.jar" ] && ANDROID_JAR="$d/android.jar"
done
if [ -z "$ANDROID_JAR" ]; then
    echo "FAIL  android.jar not found under $SDK/platforms" >&2
    exit 2
fi

for f in "$CEF_DIR/CefKeys.java" "$CEF_DIR/CefLogic.java" "$TEST_SRC"; do
    if [ ! -f "$f" ]; then
        echo "FAIL  missing source: $f" >&2
        exit 2
    fi
done

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP" >/dev/null 2>&1 || true' EXIT
SRC="$TMP/src/com/sampmobile/game/ui/cef"
OUT="$TMP/out"
mkdir -p "$SRC" "$OUT"

cp "$CEF_DIR/CefKeys.java" "$CEF_DIR/CefLogic.java" "$TEST_SRC" "$SRC/"

echo "== compiling (android.jar is compile-time only)"
if ! "$JAVAC" -nowarn -encoding UTF-8 -cp "$ANDROID_JAR" -d "$OUT" "$SRC"/*.java; then
    echo "FAIL  compile error" >&2
    exit 1
fi

# 运行时 classpath 只有 $OUT，没有 android.jar —— 见文件头的说明。
echo "== running (no android.jar on the runtime classpath)"
"$JAVA" -cp "$OUT" com.sampmobile.game.ui.cef.LogicTests
STATUS=$?
if [ $STATUS -ne 0 ]; then
    echo "LOGIC TESTS FAILED (exit $STATUS)" >&2
    exit $STATUS
fi
