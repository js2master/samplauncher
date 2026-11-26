#!/usr/bin/env bash
# 端到端验收：**原版 omp-cef 服务端组件**（官方发布包，未改一行代码）× Android 客户端。
#
#   bash tests/cef/e2e_real_plugin.sh
#
# 跟 verify_all.sh 的区别：
#   verify_all.sh  用 tests/cef/cef_server.cpp「复刻协议」的测试服务端，跑得飞快，
#                  覆盖协议/输入/World2D/WorldObject3D 的全部细节断言。
#   本脚本         用的是 aurora-mp/omp-cef 官方 release 里的 components/Cef.dll
#                  （v1.3.0-x86.zip），装进真 open.mp 服务器，客户端用真 SA-MP 连接
#                  进入服务器，走的完全是玩家路径：游戏连接 -> CEF 握手 -> 建浏览器
#                  -> 页面加载 -> 双向事件。这是任务书里的最终验收条件。
#
# 依赖（一次性准备，见 AUDIT.md「阶段8 第二批」）：
#   D:/服务器/DAYZSA/omp-cef-build/server    open.mp 服务器 + components/Cef.dll + gamemodes/cef_e2e.amx + config.json
#   D:/服务器/DAYZSA/omp-cef-build/www       首页 index.html
#   端口：游戏 7877 / CEF 7879 / 页面 HTTP 8000（不用 7777，避免和本机现有服冲突）
#
# 会改动设备上的 servers.json（先备份成 servers.json.bak），测试服务器会留在后台运行。
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUT="$HERE/out"
SRV="${OMP_CEF_SERVER:-D:/服务器/DAYZSA/omp-cef-build/server}"
WWW="${OMP_CEF_WWW:-D:/服务器/DAYZSA/omp-cef-build/www}"
PKG="${1:-com.sampmobile.launcher}"
HOST_IP="${HOST_IP:-192.168.0.117}"
GAME_PORT=7877
CEF_PORT=7879
HTTP_PORT=8000
LOG="$SRV/log.txt"

# 启动器里的固定坐标（2388x1080 横屏，见 README 的截图）
# 收藏夹首行：表头「N 个服务器」固定占 190-227，首行文字块 276-357，中心 y≈316。
# 之前写 186 会打在表头上，点了等于没点，白等 120s。
TAP_ROW1="800 316"          # 收藏夹第一行（测试服务器）
TAP_ENTER_PANEL="2020 948"  # 右侧详情面板底部的「进入服务器」
TAP_ENTER_DIALOG="1333 820" # 昵称/版本确认弹窗里的「进入服务器」

mkdir -p "$OUT"
say() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
adbq() { MSYS_NO_PATHCONV=1 adb "$@"; }
fail() { echo "FAIL  $*"; exit 1; }

# --------------------------------------------------------------- 0. 前置检查
say "0/5 前置检查"
adbq devices | grep -q "device$" || fail "没有连接的 adb 设备"
[ -f "$SRV/omp-server.exe" ]                  || fail "缺 $SRV/omp-server.exe"
[ -f "$SRV/components/Cef.dll" ]              || fail "缺 $SRV/components/Cef.dll（官方 release 里的组件）"
[ -f "$SRV/gamemodes/cef_e2e.amx" ]           || fail "缺 $SRV/gamemodes/cef_e2e.amx（先跑 gamemodes 下的 pawncc）"
[ -f "$WWW/index.html" ]                      || fail "缺 $WWW/index.html"
# gamemode 必须是 cef_e2e：跑成 login_demo 之类的话服务端一条 [e2e] 行都不会打，
# 下面是全部断言一起红，看起来像客户端问题，其实只是配置没切。
grep -q '"cef_e2e"' "$SRV/config.json" 2>/dev/null \
    || fail "config.json 的 pawn.main_scripts 不是 cef_e2e（当前配置跑的是别的 gamemode）"
echo "  组件: $(grep -m1 -ao 'Cef Component ([0-9.]*)' "$SRV/log.txt" 2>/dev/null || echo '（启动后见日志）')"
echo "  服务端目录: $SRV"

# 组件版本必须和客户端声明的协议版本严格相等（plugin.cpp 是 == 比较）。
CLIENT_VER=$(sed -n 's/^inline constexpr uint8_t VERSION_\(MAJOR\|MINOR\|PATCH\).*=\s*\([0-9]*\);/\2/p' \
    "$ROOT/app/src/main/cpp/SAMPMOBILE/Cef/CefProtocol.h" | paste -sd. -)
echo "  客户端协议版本: ${CLIENT_VER:-?}（服务端组件必须同版本，否则 reason=2 版本不匹配）"

# --------------------------------------------------- 1. 清端口 + 起 HTTP 服务
say "1/5 端口与 HTTP"
for p in $GAME_PORT $CEF_PORT; do
    pid=$(netstat -ano -p UDP 2>/dev/null | awk -v p=":$p" '$2 ~ p {print $NF; exit}')
    if [ -n "${pid:-}" ]; then
        echo "  端口 $p 被 PID $pid 占用，先停掉（只动这个测试端口）"
        taskkill //PID "$pid" //F >/dev/null 2>&1
    fi
done
sleep 2

if ! curl -sS -m 3 -o /dev/null "http://127.0.0.1:$HTTP_PORT/index.html" 2>/dev/null; then
    echo "  启动测试页 HTTP 服务 :$HTTP_PORT"
    # 必须起成独立进程。原来用 cmd /b 起，脚本一退出（或被 Ctrl-C / kill 掉）python
    # 就被回收，客户端后面看到的全是 CEF 的「网页无法打开」错误页 —— 而断言只会
    # 报 "没等到 PAGE -> SERVER"，看起来像客户端不 emit，其实是页面根本没加载。
    powershell -NoProfile -Command \
        "Start-Process -FilePath 'python' -ArgumentList '-m','http.server','$HTTP_PORT','--bind','0.0.0.0' -WorkingDirectory '$(cygpath -w "$WWW")' -WindowStyle Hidden" \
        >/dev/null 2>&1
    for _ in $(seq 1 20); do
        curl -sS -m 2 -o /dev/null "http://127.0.0.1:$HTTP_PORT/index.html" 2>/dev/null && break
        sleep 0.5
    done
fi
curl -sS -m 3 -o /dev/null -w "  测试页 HTTP %{http_code}\n" "http://127.0.0.1:$HTTP_PORT/index.html" \
    || fail "测试页起不来"

# 再从手机侧确认一次：本机 curl 通不代表手机连得上（防火墙 Public profile / AP 隔离）。
# 连不上时只告警 —— 让流程继续，好在日志里留下「页面加载失败」的真实原因。
if adbq shell "toybox nc -w 3 $HOST_IP $HTTP_PORT </dev/null" >/dev/null 2>&1; then
    echo "  手机 -> $HOST_IP:$HTTP_PORT 可达"
else
    echo "  [warn] 手机侧连不上 $HOST_IP:$HTTP_PORT，页面会显示「网页无法打开」（PAGE -> SERVER 必挂）"
fi

# 入站放行（需要管理员；失败只告警，不中断）
netsh advfirewall firewall add rule name="omp-cef-e2e-udp" dir=in action=allow \
    protocol=UDP localport=$GAME_PORT,$CEF_PORT profile=any >/dev/null 2>&1 || \
    echo "  [warn] 加防火墙规则失败，手机可能连不进来"
netsh advfirewall firewall add rule name="omp-cef-e2e-http" dir=in action=allow \
    protocol=TCP localport=$HTTP_PORT profile=any >/dev/null 2>&1 || true

# ------------------------------------------------------- 2. 起 open.mp 服务端
say "2/5 启动 open.mp + Cef.dll"
rm -f "$LOG"
( cd "$SRV" && cmd //c "start /b omp-server.exe" >/dev/null 2>&1 )
READY=0
for _ in $(seq 1 40); do
    if grep -q "Legacy Network started on port $GAME_PORT" "$LOG" 2>/dev/null; then READY=1; break; fi
    sleep 0.5
done
[ $READY -eq 1 ] || { tail -30 "$LOG" 2>/dev/null; fail "服务端没起来（见上面的日志）"; }
grep -E "Cef Component|CEF\] Network Server|\[e2e\] gamemode ready" "$LOG" | sed 's/^/  /'

# ------------------------------------------------- 3. 设备侧：收藏夹 + 进服务器
say "3/5 设备：写入收藏并进入服务器"
# 只看「本段」的证据：客户端上一轮残留的连接会在服务器一起来就自动重连，如果只按
# 出现顺序断言，会在点击之前就 PASS —— 那验证的是上一轮。App 在下面被 force-stop，
# 旧连接随之断开，所以把 SINCE 定在点击之后的时刻最干净。
adbq shell "cp /sdcard/Android/data/$PKG/files/servers.json \
            /sdcard/Android/data/$PKG/files/servers.json.bak" >/dev/null 2>&1 || true
cat > "$OUT/servers-e2e.json" <<EOF
{
    "servers": [
        { "host": "$HOST_IP", "port": "$GAME_PORT" },
        { "host": "192.168.0.117", "port": "7777" },
        { "host": "116.62.52.151", "port": "7777" }
    ]
}
EOF
adbq push "$(cygpath -w "$OUT/servers-e2e.json")" \
    "/sdcard/Android/data/$PKG/files/servers.json" >/dev/null 2>&1 \
    || fail "推送 servers.json 失败（adbq 关了路径转换，必须 cygpath -w）"

adbq shell am force-stop "$PKG"
sleep 2
# WorldObject3D 的 stage 事件默认关（正式服不该刷这类日志），验收要看 c=1/c=2 就得先开。
# 客户端读的是 debug.samp.cef.worldobj 系统属性，不是环境变量 —— 安卓上没法给应用
# 进程注入 env，用 getenv 等于永久关着。
adbq shell setprop debug.samp.cef.worldobj 1
adbq shell am start -n "$PKG/.SplashActivity" >/dev/null 2>&1
sleep 14
adbq exec-out screencap -p > "$OUT/e2e-1-list.png"

# 列表第一行 -> 右侧详情 -> 进入服务器 -> 弹窗确认
adbq shell input tap $TAP_ROW1
sleep 3
adbq shell input tap $TAP_ENTER_PANEL
sleep 3
adbq shell input tap $TAP_ENTER_DIALOG
SINCE=$(date +%Y-%m-%dT%H:%M:%S)
echo "  已点击进入服务器，只统计 $SINCE 之后的日志；等 GTA + 连接（最多 120s）"

# ------------------------------------------------------------- 4. 等服务端事件
say "4/5 等待握手与浏览器创建"
# 只取 SINCE 之后的行：既排除上一轮残留的连接，也不依赖 print 的落盘顺序
# （open.mp 的脚本输出是缓冲的，行序不等于时间序，靠 marker 行切不准）。
slice() {
    awk -v s="$SINCE" '{
        if (match($0, /\[[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}/)) {
            ts = substr($0, RSTART + 1, 19);
            if (ts >= s) print;
        }
    }' "$LOG"
}
WAIT_OK=0
for _ in $(seq 1 60); do
    if slice | grep -q "PAGE -> SERVER"; then WAIT_OK=1; break; fi
    sleep 2
done
sleep 3
adbq exec-out screencap -p > "$OUT/e2e-2-ingame.png"

# WorldObject3D 要等玩家真的 spawn。这个客户端（0.3.7-R5 模式）连上后停在选人
# 镜头，底部有 Spawn 按钮；服务端 SpawnPlayer 不生效，必须点它。
#
# 注意这里**故意不关**那块全屏 overlay（browser 7）：它现在是 focused=false 创建的
# HUD 语义页面，按设计不该吃触摸。所以"整屏 web 页还挂着时点 Spawn 能点动"本身
# 就是一条断言 —— 用 web 代替 textdraw 不会压死虚拟摇杆/游戏按钮。
# World2D 的服务端条目默认不上报投影（正常玩家不该付这个开销），验收前显式打开。
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action world2dprobe --ei on 1 >/dev/null 2>&1 || true
sleep 3
adbq shell input tap 1194 1008
SPAWN_OK=0
for _ in $(seq 1 30); do
    if slice | grep -q "board object id="; then SPAWN_OK=1; break; fi
    sleep 2
done
# spawn 之后再关掉那块 HUD：板子和悬浮页都在它下面，留着会挡住截图证据。
adbq shell am broadcast -a com.sampmobile.CEFDEBUG -p "$PKG" --es action destroy --ei id 7 >/dev/null 2>&1 || true
# 再给客户端一点时间抓帧/上传纹理，然后截图看板上是不是页面内容。
sleep 4
adbq exec-out screencap -p > "$OUT/e2e-3-world3d.png"

# World2D：服务端在 spawn 时下发 CreateWorld2DBrowser，8 秒后再调
# SetWorld2DBrowserPos 把页面抬高 1.5 米。等客户端上报"移动前后屏幕 y 变了"，
# 说明投影真的在跟着世界坐标跑，而不是画在固定位置。
SHIFT_OK=0
for _ in $(seq 1 15); do
    if slice | grep -q "WORLD2D SHIFT"; then SHIFT_OK=1; break; fi
    sleep 2
done
sleep 2
adbq exec-out screencap -p > "$OUT/e2e-4-world2d.png"

# --------------------------------------------------------------- 5. 断言与汇总
say "5/5 断言（只看 $SINCE 之后的日志）"
FAILED=0
check() {  # check <pattern> <描述>
    if slice | grep -qE "$1" 2>/dev/null; then
        echo "  PASS  $2"
    else
        echo "  FAIL  $2"
        FAILED=$((FAILED + 1))
    fi
}
check "\[CEF\] RequestJoin pid=[0-9]+ from=" "原版插件收到本轮客户端的 RequestJoin（含 IP 校验行）"
check "\[e2e\] OnCefInitialize player=[0-9]+ success=1 reason=0" "握手成功（CEF_INIT_OK，版本/密钥/IP 全部匹配）"
check "\[e2e\] CEF_CreateBrowser" "OnCefInitialize 之后服务端调了 CEF_CreateBrowser"
check "\[e2e\] OnCefBrowserCreated .*success=1" "浏览器在客户端建成功（BrowserCreateResult 回到 Pawn）"
check "\[e2e\] PAGE -> SERVER" "页面 window.cef.emit 回到服务端（CEF_RegisterEvent 触发了回调）"
check "\[e2e\] board object id=[0-9]+" "整屏 web HUD（focused=false）不挡游戏输入：点 Spawn 生效、服务端拿到真实 objectid"
check "\[e2e\] CEF_CreateWorldBrowser" "WorldObject3D：CEF_CreateWorldBrowser 下发了"
check "\[e2e\] CEF_AttachBrowserToObject" "WorldObject3D：CEF_AttachBrowserToObject 下发了"
check "CLIENT worldobj debug: .* c=1" "客户端把页面材质贴到了真实 objectid 上（stage=1 材质已替换）"
check "CLIENT worldobj debug: .* c=2" "页面首帧进了 RenderWare 纹理（stage=2 首帧上传）"
check "\[e2e\] CEF_CreateWorld2DBrowser" "World2D：CEF_CreateWorld2DBrowser 下发了"
check "CLIENT world2d debug: .* w=512 h=256 on=1" "客户端把服务端给的 512x256 页面摆到了屏幕内（尺寸走协议字段）"
check "\[e2e\] CEF_SetWorld2DBrowserPos" "World2D：CEF_SetWorld2DBrowserPos 下发了"
check "\[e2e\] WORLD2D SHIFT ok" "页面跟着世界坐标移动了（投影每帧重算，坐标变了）"
check "CLIENT world2d debug: browser=11 " "第二条 World2D 独立存在并单独上报投影（多 web 并发）"

# 延迟探针：服务端 100ms 一轮 ping，页面立刻回 pong，服务端用自己发出去的
# 毫秒时间戳算往返。这里只卡窗口平均值 —— 尾部尖刺（GC/掉帧）在 HUD 刷新
# 这种 5~10Hz 的场景里不可见，卡 p99 会让断言随机器状态乱红。
PING_MAX_AVG=$(slice | grep -oE "PING browser=9 .*avg=[0-9]+" | sed 's/.*avg=//' | sort -n | tail -1)
if [ -n "${PING_MAX_AVG:-}" ] && [ "$PING_MAX_AVG" -lt 100 ]; then
    echo "  PASS  往返延迟（服务端 -> 页面 -> 服务端）窗口均值最高 ${PING_MAX_AVG}ms < 100ms"
else
    echo "  FAIL  往返延迟窗口均值最高 ${PING_MAX_AVG:-无数据}ms（阈值 100ms）"
    FAILED=$((FAILED + 1))
fi

echo
echo "  本轮日志片段:"
slice | grep -E "\[e2e\]|RequestJoin" | sed 's/^/    /'
echo
echo "  日志: $LOG"
echo "  截图: $OUT/e2e-1-list.png  e2e-2-ingame.png  e2e-3-world3d.png  e2e-4-world2d.png"
# 验收完把调试开关关回去，别把设备留在「刷调试事件」的状态。
adbq shell setprop debug.samp.cef.worldobj 0 >/dev/null 2>&1 || true
if [ $FAILED -eq 0 ] && [ $WAIT_OK -eq 1 ]; then
    echo
    echo "=== E2E (REAL PLUGIN) OK"
    exit 0
fi
echo
echo "=== E2E (REAL PLUGIN) FAILED ($FAILED 条断言失败)"
[ $WAIT_OK -eq 1 ] || echo "  提示：120s 内没等到本轮的 PAGE -> SERVER。检查手机是否真的进了服务器、测试页 HTTP 是否可达。"
exit 1
