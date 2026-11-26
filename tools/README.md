# tools/

纯开发、构建准备、数据包发布、引擎逆向与抓包调试工具目录（所有自动化检测与测试套件统一位于 `tests/`）。

## 目录结构

### `tools/engine/` —— GTA:SA v2.10 引擎提取与离线逆向
| 脚本 | 用途 |
|---|---|
| `prepare_gtasa.py` | 构建前从已安装 GTA:SA v2.10 的 ADB 设备（`--from-adb`）或本地 APK/Split-APK（`--apk` / 默认扫描 `dist/gtasa*.apk`）提取 `libGTASA.so`、`libImmEmulatorJ.so` 与轻量 APK 资产到本地（已 `.gitignore`）。自动跳过大体积 OBB、剥离 Fastman92 加载器注入并修复程序头表位置（防 x86 模拟器闪退）；支持 `--libs-only` 与 `--clean`。 |
| `gtasa_inspect.py` | 零第三方依赖的双 ABI（`arm64-v8a` / `armeabi-v7a`）`libGTASA.so` 离线反汇编与分析工具，提供 `find`（查符号）、`disasm`（自动还原 PLT/GOT 的反汇编）与 `xrefs`（全库交叉引用扫描）。 |

### `tools/gamedata/` —— 游戏数据包制作、分片发布与本地调试
| 脚本 | 用途 |
|---|---|
| `build_gamedata.py` | 基于官方 GTA:SA Android v2.10（`--gtasa` / `dist/`）与 PC 版 GTA:SA + SA-MP 0.3.7（`--pc-samp`），全自动转换并打包可直接由启动器一键导入的 `dist/gamedata.zip`。 |
| `publish_gamedata.py` | 将本地游戏数据目录打包并按 16 MB 切片生成 `manifest.json` + `chunks/`（用于启动器在线分片下载），可选 `--upload` 直传 Cloudflare R2。 |
| `upload_r2.py` | 仅将 `publish_gamedata.py` 已生成的 `dist/cdn/` 产物上传至 Cloudflare R2（支持断点跳过、失败重试与抽样 HEAD 校验）。 |
| `dev_rangeserver.py` | 支持 HTTP `206 Partial Content` Range 请求的本地调试静态服务器（附 `--no-range` 与 `--slow` 开关，用于本机验证客户端断点续传与弱网降级）。 |

### `tools/netcap/` —— `/netcap` 抓包离线分析工具
| 源码 | 用途 |
|---|---|
| `ncapdump.cpp` | 将游戏内 `/netcap` 录制的二进制抓包文件解析并列出逐条收发包摘要。 |
| `ncapreplay.cpp` | 离线回放 `/netcap` 抓包文件，校验客户端 `Net/Proto` 解包行为。 |

> `tools/netcap/` 由 `tests/native/CMakeLists.txt` 一起编译至 `build/native-tests/`，确保始终与客户端 `Net/Proto/` 源码保持同步。
