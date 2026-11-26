# tests/

SA-MP Mobile 统一自动化校验与测试目录。

设计原则：**只校验 100% 客观可验证的工程、二进制、符号与 Android 平台契约，外加与真实业务共用源码的可执行测试**，不把易随设计演进而过时的业务代码猜测写死成门禁。

---

## 统一入口：`tests/check.py`

```powershell
# 日常快检（约 2–3 秒）：合规与卫生 + 平台底线 + NDK 二进制/537 符号/真实 JNI + SDK APK 工具校验
python tests/check.py

# 全量校验：上述全部 + C++ 电脑端单测 + Gradle JVM 单测 + gamedata 分片拼包闭环
python tests/check.py --full

# 发包前严格门禁：--full + 强制要求已提取 GTA:SA 与已构建 APK + Android Lint 静态检查
python tests/check.py --release

# 单独运行某一环节
python tests/check.py --stage <compliance|platform|abi|apk|native|jvm|gamedata|lint>
```

---

## 5 阶段校验流程

| 阶段 | 模块 | 校验内容与官方工具链 |
|---|---|---|
| **Stage 1** | `checks/compliance.py` | 法律/许可证文件、`res/font/` 禁商用字体、`.gitignore` 专有资产屏蔽、无硬编码密钥/本机路径/设备序列号、`gradlew` LF 换行 |
| **Stage 2** | `checks/platform.py` | `app/build.gradle` 双 ABI / 固定 NDK / `useLegacyPackaging` / 条件 Firebase；`AndroidManifest.xml` TBI 关闭与 `:game` 独立进程；`monetloader_lib.zip` 与 GTA:SA 提取资产索引 |
| **Stage 3** | `checks/binary_abi.py` | 双 ABI `jniLibs/*.so` ELF 头；NDK `llvm-nm` 双 ABI `Symbols.def` 全量 537 符号导出校验；NDK `llvm-readelf` 校验编译出的 `libSAMPMOBILE.so` / `libSCAnd.so` **零 `TEXTREL`**、**`DT_NEEDED` 依赖闭包**（仅链接 NDK 公开库或随包库）、**Android 15+ 16KB 页对齐提示**、以及双 ABI **真实 ELF 导出符号 ↔ Java `native` 方法对齐** |
| **Stage 4** | `checks/apk_verify.py` | APK ZIP 结构与双 ABI 库完整度；Android SDK **`aapt2`** 核对编译后二进制 `AndroidManifest.xml`（包名、`minSdk`/`targetSdk`、双 ABI、`extractNativeLibs`、`allowNativeHeapPointerTagging`）；**`zipalign -c 4`** 对齐校验；**`apksigner verify`** 签名合法性校验 |
| **Stage 5** | `checks/runners.py` & `checks/gamedata.py` | ① `tests/native/` 20 项 C++ 电脑端单测（含 open.mp 上游对拍）；② `gradlew testDebugUnitTest` Java JUnit 单测；③ `publish_gamedata.py` 分片/清单/重装 SHA-256 端到端闭环；④（`--lint` / `--release`）官方 Android Lint (`gradlew lintDebug`) |

---

## 目录布局

| 路径 | 职责 |
|---|---|
| `tests/check.py` | 统一命令行校验入口 |
| `tests/checks/` | 模块化客观门禁实现（仅依赖 Python 3 标准库 + 本机 Android SDK/NDK 工具） |
| `tests/native/` | 电脑端 C++ 单元测试（CMake + Ninja + CTest，WSL/Linux 下 `sh tests/native/run.sh`，不进入 APK）：<br>- `proto/`：RakNet BitStream、线格式、`/netcap`、入站 RPC / 同步 / 出站包单测<br>- `core/`：无引擎依赖的客户端核心纯逻辑单测（`InputArbiterCore`、`SettingsContract`）<br>- `interop/`：与本地 `reference/open.mp 上游源码` 对拍测试（无本地源码时自动跳过） |
| `tests/cef/` | `omp-cef` 协议 KAT 向量、逻辑断言与设备端浏览器回归套件（见 `tests/cef/verify_all.sh`） |
| `app/src/test/` | Gradle 标准 JVM 单元测试（如 `ClientSettingsTest`、`ModManagerTest`），由 `tests/check.py --full` 统一调起 |
