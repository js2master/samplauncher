# Third-Party Licenses & Notices

This directory collects license texts and attribution notices for third-party
libraries, prebuilt binaries, and fonts used by **SA-MP Mobile**.

## Bundled / Vendored Open-Source Components

| Component | Location | License | Source / Reference |
|---|---|---|---|
| **AndroidModLoader (`libAML.so`)** | `app/src/main/jniLibs/*/libAML.so` | [MIT](MIT.txt) | https://github.com/AndroidModLoader/AndroidModLoader |
| **MonetLoaderOSS (`libmonetloader.so`, `monetloader_lib.zip`)** | `app/src/main/jniLibs/`, `app/src/main/assets/` | [GPL-3.0](../../LICENSE) | https://github.com/xefinity/MonetLoaderOSS |
| **LuaJIT (`libluajit-5.1.so`)** | `app/src/main/jniLibs/*/libluajit-5.1.so` | [MIT](MIT.txt) | https://luajit.org/ |
| **Dear ImGui** | `app/src/main/cpp/SAMPMOBILE/Deps/Imgui/` | [MIT](MIT.txt) | https://github.com/ocornut/imgui |
| **spdlog / bundled fmt** | `app/src/main/cpp/SAMPMOBILE/Deps/spdlog/` | [MIT](MIT.txt) | https://github.com/gabime/spdlog |
| **RapidJSON** | `app/src/main/cpp/SAMPMOBILE/Deps/Rapidjson/` | [MIT](MIT.txt) | https://github.com/Tencent/rapidjson |
| **GlossHook** | `app/src/main/cpp/SAMPMOBILE/Memory/` | [MIT](MIT.txt) | https://github.com/Rprop/And64InlineHook |
| **Opus Audio Codec** | `app/src/main/cpp/SAMPMOBILE/Deps/Opus/` | [BSD-3-Clause](BSD-3-Clause.txt) | https://opus-codec.org/ |
| **RakNet (SA-MP fork)** | `app/src/main/cpp/SAMPMOBILE/Deps/RakNet/` | [BSD-3-Clause](BSD-3-Clause.txt) | https://github.com/facebookarchive/RakNet |
| **CLEO Android (specification reference)** | `app/src/main/cpp/SAMPMOBILE/Game/CleoEngine.*` | [BSD-3-Clause](BSD-3-Clause.txt) | https://github.com/cleolibrary/CLEO-ANDROID |
| **omp-cef (protocol & client reference)** | `app/src/main/cpp/SAMPMOBILE/Cef/`, `app/src/main/java/com/sampmobile/game/ui/cef/` | [MIT](MIT.txt) | https://github.com/aurora-mp/omp-cef |
| **KCP (`ikcp`)** | `app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/ikcp.{c,h}` | [MIT](MIT.txt) | https://github.com/skywind3000/kcp |
| **PicoSHA2** | `app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/picosha2.h` | [MIT](MIT.txt) | https://github.com/okdshin/PicoSHA2 |
| **BLAKE2 reference implementation** | `app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/blake2*` | CC0-1.0 / [Apache-2.0](Apache-2.0.txt) | https://github.com/BLAKE2/BLAKE2 |
| **tiny-AES-c** | `app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/aes.{c,h}` | Unlicense (Public Domain) | https://github.com/kokke/tiny-AES-c |
| **TweetNaCl** | `app/src/main/cpp/SAMPMOBILE/Cef/thirdparty/tweetnacl.{c,h}` | Public Domain | https://tweetnacl.cr.yp.to/ |
| **OpenAL Soft (`libOpenAL*.so`)** | `app/src/main/jniLibs/*/libOpenAL*.so` | [LGPL-2.1](LGPL-2.1.txt) | https://github.com/kcat/openal-soft |
| **LLVM `libc++_shared.so`** | `app/src/main/jniLibs/*/libc++_shared.so` | [Apache-2.0 WITH LLVM-exception](LLVM-Apache-2.0-Exception.txt) | https://llvm.org/ |
| **Roboto Fonts** | `app/src/main/res/font/roboto*.ttf` | [Apache-2.0](Apache-2.0.txt) | https://github.com/googlefonts/roboto-2 |
| **Montserrat Font (Downloadable Font XML)** | `app/src/main/res/font/montserrat*.xml` | SIL OFL 1.1 | https://fonts.google.com/specimen/Montserrat |
| **AndroidX / Material / OkHttp / Glide / JUnit** | Resolved via Gradle (`gradle/libs.versions.toml`) | Apache-2.0 / BSD / EPL-1.0 | Maven Central / Google Maven |

## Non-Commercial / Proprietary Third-Party Components

| Component | Status | Terms |
|---|---|---|
| **BASS / BASS_SSL (`libBASS*.so`)** | Prebuilt audio library (Un4seen Developments) | Free for non-commercial use; commercial distribution requires a license from https://www.un4seen.com/ |
| **GTA:SA Android v2.10 (`libGTASA.so`, `libImmEmulatorJ.so`, APK assets, game data)** | **Excluded from repository** | Proprietary (Rockstar Games / Take-Two Interactive). Supplied locally by the user via `tools/engine/prepare_gtasa.py`. |
