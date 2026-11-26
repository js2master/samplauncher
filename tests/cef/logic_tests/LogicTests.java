package com.sampmobile.game.ui.cef;

/**
 * CEF 输入路径纯逻辑的离线断言（不需要手机、不需要游戏、不需要 gradle）。
 *
 * 跑法：bash tests/cef/logic_tests/run_logic_tests.sh
 *
 * 为什么需要它：这台 ROM 的 logd 会反复重启，设备端只能靠"上行报文"证明某条
 * 路径被走到，证明不了每条分支都对。下面的期望值全部**独立硬编码**（不引用
 * CefKeys 里的表），所以能抓出产品侧的笔误；而 CefLogic 的判定直接对着 PC 源码
 * 的语义写。
 *
 * 断言里出现的 KeyEvent.KEYCODE_* / META_* 都是 static final 常量，javac 会内联，
 * 因此运行时 classpath **不含 android.jar**——如果以后有人往 CefKeys/CefLogic 里
 * import 了 android 的运行时类型，这个测试会当场 NoClassDefFoundError，正好当作
 * "纯逻辑被污染"的守门人。
 */
public final class LogicTests {

    private static int passed = 0;
    private static int failed = 0;

    public static void main(String[] args) {
        section("CefKeys: A..Z -> VK / Set-1");
        letters();

        section("CefKeys: digits and punctuation");
        digitsAndPunctuation();

        section("CefKeys: control / navigation / function keys");
        controlsAndFunctionKeys();

        section("CefKeys: table invariants");
        invariants();

        section("CefKeys: unknown keys are never faked");
        unknowns();

        section("CefLogic.shouldSendKey (PressKey gate)");
        pressKeyGate();

        section("CefLogic.imeInsetOf (soft keyboard height)");
        imeInset();

        section("CefLogic.shouldBlockChat (ShouldBlockChat)");
        blockChat();

        System.out.println();
        System.out.println(passed + " passed, " + failed + " failed");
        if (failed == 0) {
            System.out.println("LOGIC TESTS OK");
        }
        System.exit(failed == 0 ? 0 : 1);
    }

    // ---------------------------------------------------------------- CefKeys

    /**
     * Set-1 扫描码，按字母顺序独立写一遍（PC/AT 键盘规范：
     * Q=0x10 W=0x11 E=0x12 R=0x13 T=0x14 Y=0x15 U=0x16 I=0x17 O=0x18 P=0x19
     * A=0x1E S=0x1F D=0x20 F=0x21 G=0x22 H=0x23 J=0x24 K=0x25 L=0x26
     * Z=0x2C X=0x2D C=0x2E V=0x2F B=0x30 N=0x31 M=0x32）。
     */
    private static final int[] EXPECT_SCAN = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
            0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C
    };

    private static void letters() {
        for (int i = 0; i < 26; i++) {
            final int keyCode = android.view.KeyEvent.KEYCODE_A + i;   // 29..54
            final char letter = (char) ('A' + i);
            eq("KEYCODE_" + letter + " -> VK 0x" + hex(0x41 + i), 0x41 + i, CefKeys.vkOf(keyCode));
            eq("KEYCODE_" + letter + " -> scan 0x" + hex(EXPECT_SCAN[i]), EXPECT_SCAN[i],
                    CefKeys.scanOf(keyCode));
        }
        // 一个具体的"服务端会看到的"例子：设备上 input keyevent 29 落了 VK 0x41。
        eq("injected KEYCODE_A(29) becomes Windows VK 0x41", 0x41,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_A));
    }

    private static void digitsAndPunctuation() {
        final int[] digitScan = {0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A};
        for (int i = 0; i < 10; i++) {
            final int keyCode = android.view.KeyEvent.KEYCODE_0 + i;
            eq("KEYCODE_" + i + " -> VK 0x" + hex(0x30 + i), 0x30 + i, CefKeys.vkOf(keyCode));
            eq("KEYCODE_" + i + " -> scan 0x" + hex(digitScan[i]), digitScan[i],
                    CefKeys.scanOf(keyCode));
        }
        // 标点的 VK 用的是 US 布局的 OEM 码，扫��码是 Set-1 物理位置。
        eq("GRAVE  -> VK_OEM_3 0xC0 / scan 0x29", 0xC0, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_GRAVE));
        eq("GRAVE  -> scan 0x29", 0x29, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_GRAVE));
        eq("MINUS  -> VK_OEM_MINUS 0xBD", 0xBD, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_MINUS));
        eq("EQUALS -> VK_OEM_PLUS 0xBB", 0xBB, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_EQUALS));
        eq("COMMA  -> VK_OEM_COMMA 0xBC / scan 0x33", 0xBC,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_COMMA));
        eq("PERIOD -> scan 0x34", 0x34, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_PERIOD));
        eq("SLASH  -> VK_OEM_2 0xBF / scan 0x35", 0xBF,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_SLASH));
    }

    private static void controlsAndFunctionKeys() {
        eq("ENTER -> VK_RETURN 0x0D", 0x0D, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_ENTER));
        eq("ENTER -> scan 0x1C", 0x1C, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_ENTER));
        eq("TAB   -> VK_TAB 0x09 / scan 0x0F", 0x09, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_TAB));
        eq("TAB   -> scan 0x0F", 0x0F, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_TAB));
        eq("DEL   -> VK_BACK 0x08 / scan 0x0E (backspace)", 0x08,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_DEL));
        eq("DEL   -> scan 0x0E", 0x0E, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_DEL));
        eq("ESCAPE-> VK_ESCAPE 0x1B / scan 0x01", 0x1B,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_ESCAPE));
        eq("SPACE -> VK_SPACE 0x20 / scan 0x39", 0x20,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_SPACE));

        eq("SHIFT_LEFT  -> VK_SHIFT 0x10 / scan 0x2A", 0x10,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_SHIFT_LEFT));
        eq("SHIFT_RIGHT -> VK_SHIFT 0x10 / scan 0x36", 0x36,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_SHIFT_RIGHT));
        eq("CTRL_LEFT   -> VK_CONTROL 0x11 / scan 0x1D", 0x11,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_CTRL_LEFT));
        eq("ALT_LEFT    -> VK_MENU 0x12 / scan 0x38", 0x12,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_ALT_LEFT));

        eq("DPAD_UP    -> VK_UP 0x26 / scan 0x48", 0x26,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_DPAD_UP));
        eq("DPAD_UP    -> scan 0x48", 0x48, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_DPAD_UP));
        eq("DPAD_DOWN  -> VK_DOWN 0x28 / scan 0x50", 0x50,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_DPAD_DOWN));
        eq("DPAD_LEFT  -> VK_LEFT 0x25 / scan 0x4B", 0x4B,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_DPAD_LEFT));
        eq("DPAD_RIGHT -> VK_RIGHT 0x27 / scan 0x4D", 0x4D,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_DPAD_RIGHT));

        eq("F1  -> VK_F1 0x70 / scan 0x3B", 0x70, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_F1));
        eq("F1  -> scan 0x3B", 0x3B, CefKeys.scanOf(android.view.KeyEvent.KEYCODE_F1));
        eq("F10 -> VK_F10 0x79 / scan 0x44", 0x44,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_F10));
        eq("F11 -> scan 0x57 (Set-1 F11 is not contiguous)", 0x57,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_F11));
        eq("F12 -> VK_F12 0x7B / scan 0x58", 0x58,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_F12));

        eq("NUMPAD_0 -> VK_NUMPAD0 0x60 / scan 0x52", 0x52,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_NUMPAD_0));
        eq("NUMPAD_5 -> VK_NUMPAD5 0x65 / scan 0x4C", 0x4C,
                CefKeys.scanOf(android.view.KeyEvent.KEYCODE_NUMPAD_5));
        eq("NUMPAD_ENTER -> VK_RETURN 0x0D", 0x0D,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_NUMPAD_ENTER));
    }

    private static void invariants() {
        boolean vkWithoutScan = false;
        boolean scanWithoutVk = false;
        boolean vkOutOfByte = false;
        String firstBad = "";
        for (int keyCode = 0; keyCode < 300; keyCode++) {
            final int vk = CefKeys.vkOf(keyCode);
            final int scan = CefKeys.scanOf(keyCode);
            if (vk != 0 && scan == 0) {
                vkWithoutScan = true;
                if (firstBad.isEmpty()) firstBad = "keyCode " + keyCode + " vk=" + vk;
            }
            if (vk == 0 && scan != 0) {
                scanWithoutVk = true;
                if (firstBad.isEmpty()) firstBad = "keyCode " + keyCode + " scan=" + scan;
            }
            // 服务端 EnableKey 白名单是 256 项，超过 255 的 VK 永远发不出去。
            if (vk > 255) vkOutOfByte = true;
        }
        check("every mapped key has both VK and scan", !vkWithoutScan, firstBad);
        check("no scan without VK", !scanWithoutVk, firstBad);
        check("all VKs fit the 256-entry EnableKey whitelist", !vkOutOfByte, null);

        // 至少 90 个键被收录：少了说明哪段 put 循环被删了。
        int mapped = 0;
        for (int k = 0; k < 300; k++) if (CefKeys.vkOf(k) != 0) mapped++;
        check("table covers at least 90 keys (got " + mapped + ")", mapped >= 90, null);
    }

    private static void unknowns() {
        // 这几个是 Android 特有的键，Windows 上没有对应 VK，必须返回 0 而不是
        // 挑一个"差不多"的码——服务端会拿它当真实按键处理。
        eq("KEYCODE_BACK  -> 0 (no VK equivalent, never faked)", 0,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_BACK));
        eq("KEYCODE_HOME  -> 0 (only MOVE_HOME is mapped)", 0,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_HOME));
        eq("KEYCODE_MENU  -> 0", 0, CefKeys.vkOf(android.view.KeyEvent.KEYCODE_MENU));
        eq("KEYCODE_VOLUME_UP -> 0", 0,
                CefKeys.vkOf(android.view.KeyEvent.KEYCODE_VOLUME_UP));
        eq("negative keyCode -> 0", 0, CefKeys.vkOf(-1));
        eq("keyCode past table -> 0", 0, CefKeys.vkOf(299));
        eq("keyCode far out of range -> 0", 0, CefKeys.vkOf(9999));
    }

    // --------------------------------------------------------------- CefLogic

    private static boolean[] whitelist(int... vks) {
        boolean[] allowed = new boolean[256];
        for (int vk : vks) allowed[vk] = true;
        return allowed;
    }

    private static void pressKeyGate() {
        final boolean[] allowed = whitelist(0x41, 0x0D);   // A, ENTER

        check("capture disabled -> nothing sent, even for whitelisted VK",
                !CefLogic.shouldSendKey(false, allowed, 0x41, true, false), null);
        check("capture enabled + whitelisted + keydown -> sent",
                CefLogic.shouldSendKey(true, allowed, 0x41, true, false), null);
        check("capture enabled + whitelisted + keyup -> sent",
                CefLogic.shouldSendKey(true, allowed, 0x41, false, false), null);
        check("VK not in EnableKey whitelist -> not sent (0x5A = Z)",
                !CefLogic.shouldSendKey(true, allowed, 0x5A, true, false), null);
        check("keydown auto-repeat -> dropped (PC: if (!(down && repeat)))",
                !CefLogic.shouldSendKey(true, allowed, 0x41, true, true), null);
        // 抬起即便带 repeat 也要发，否则服务端收到"按下没抬起"，页面里的 keyup 永远不来。
        check("keyup carrying repeat -> still sent",
                CefLogic.shouldSendKey(true, allowed, 0x41, false, true), null);
        check("vk 0 (unmapped Android key) -> not sent",
                !CefLogic.shouldSendKey(true, whitelist(0), 0, true, false), null);
        check("vk 256 (beyond whitelist array) -> not sent",
                !CefLogic.shouldSendKey(true, allowed, 256, true, false), null);
        check("negative vk -> not sent",
                !CefLogic.shouldSendKey(true, allowed, -5, true, false), null);
        check("null whitelist -> not sent",
                !CefLogic.shouldSendKey(true, null, 0x41, true, false), null);
        check("empty whitelist -> not sent",
                !CefLogic.shouldSendKey(true, new boolean[256], 0x41, true, false), null);

        // 实机脚本里打过 ENTER（0x0D）和 A（0x41），服务端两条都收到了。
        check("ENTER stays deliverable while capture is on",
                CefLogic.shouldSendKey(true, allowed, 0x0D, true, false), null);
    }

    private static void imeInset() {
        // 真机（V2312A, 1080 高）实测：键盘弹出时可见区 bottom = 372。
        eq("1080px window, visible bottom 372 -> 708px keyboard", 708,
                CefLogic.imeInsetOf(1080, 372));
        eq("keyboard hidden (visible bottom == window height) -> 0", 0,
                CefLogic.imeInsetOf(1080, 1080));
        eq("48px navigation bar is not a keyboard -> 0", 0,
                CefLogic.imeInsetOf(1080, 1032));
        // 阈值正好落在 1/4 时的边界：270 不小于 270，按键盘处理。
        eq("exactly 1/4 of the window is treated as keyboard", 270,
                CefLogic.imeInsetOf(1080, 810));
        eq("one pixel below 1/4 is treated as chrome, not keyboard -> 0", 0,
                CefLogic.imeInsetOf(1080, 811));
        eq("visible bottom past the window bottom clamps to 0", 0,
                CefLogic.imeInsetOf(1080, 1200));
        eq("zero-height window -> 0 (avoids divide-by-four noise)", 0,
                CefLogic.imeInsetOf(0, 0));
        eq("negative height -> 0", 0, CefLogic.imeInsetOf(-100, -50));
        eq("small window keeps proportional behaviour", 40,
                CefLogic.imeInsetOf(100, 60));

        // ---- imeMarginOf：adjustResize 已经让出空间时不能再减 bottomMargin ----
        // 键盘弹出、窗口没被 resize（adjustPan）：要自己退让
        eq("pan mode, keyboard 708px -> margin 708", 708,
                CefLogic.imeMarginOf(1080, 372, 1080));
        // 键盘弹出、窗口已被 resize 到 372：空间系统已经让了，margin 必须为 0
        eq("resize mode, window already shrunk to 372 -> margin 0 (no double give-back)", 0,
                CefLogic.imeMarginOf(372, 372, 1080));
        // 反馈回路的精确复现：窗口 372 高、可见 bottom 也是 372
        eq("feedback loop guard: decor 372 == visible 372 -> 0", 0,
                CefLogic.imeMarginOf(372, 372, 1080));
        // 键盘收起且窗口恢复全屏 -> 0
        eq("keyboard hidden, full-height window -> 0", 0,
                CefLogic.imeMarginOf(1080, 1080, 1080));
        // 量不到时按 0，等下一次布局，不要乱减
        eq("zero decor height -> 0 (wait for next layout)", 0,
                CefLogic.imeMarginOf(0, 372, 1080));
        eq("unknown screen height -> 0 (fail safe)", 0,
                CefLogic.imeMarginOf(1080, 372, 0));
    }

    private static void blockChat() {
        check("server disabled chat input -> blocked regardless of the page",
                CefLogic.shouldBlockChat(false, -1, false), null);
        check("no page input focused -> game chat stays available",
                !CefLogic.shouldBlockChat(true, -1, true), null);
        check("page owns chat input -> blocked (ShouldBlockChat)",
                CefLogic.shouldBlockChat(true, 1, true), null);
        check("page with controls_chat_input=false -> game chat stays available",
                !CefLogic.shouldBlockChat(true, 1, false), null);
        check("server disabled chat input still blocks a controls_chat_input=false page",
                CefLogic.shouldBlockChat(false, 1, false), null);
    }

    // ---------------------------------------------------------------- helpers

    private static void section(String title) {
        System.out.println();
        System.out.println("== " + title);
    }

    private static void eq(String what, long want, long got) {
        check(what, want == got, "want=" + want + " got=" + got);
    }

    private static void check(String what, boolean ok, String detail) {
        if (ok) {
            passed++;
            System.out.println("  PASS  " + what);
        } else {
            failed++;
            System.out.println("  FAIL  " + what + (detail == null ? "" : "   [" + detail + "]"));
        }
    }

    private static String hex(int v) {
        return Integer.toHexString(v).toUpperCase();
    }

    private LogicTests() {
    }
}
