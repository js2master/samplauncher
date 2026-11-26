"""Stage 2: Objective Android build configuration, Manifest, and runtime asset contracts."""

from __future__ import annotations

import zipfile

from .common import PINNED_NDK_VERSION, REPO_ROOT, Runner, read_text


def run_platform_checks(r: Runner, require_gtasa: bool = False) -> None:
    r.section("Stage 2: Android Build & Platform Contracts")

    gradle = read_text("app/build.gradle")
    manifest = read_text("app/src/main/AndroidManifest.xml")

    if "useLegacyPackaging = true" in gradle:
        r.ok("Gradle jniLibs legacy packaging", "useLegacyPackaging = true (Android 14+ native loader requirement)")
    else:
        r.fail("Gradle jniLibs legacy packaging", "missing useLegacyPackaging = true")

    if '"armeabi-v7a"' in gradle and '"arm64-v8a"' in gradle:
        r.ok("Gradle dual-ABI filter", "armeabi-v7a + arm64-v8a")
    else:
        r.fail("Gradle dual-ABI filter", "both armeabi-v7a and arm64-v8a must be enabled")

    if f'ndkVersion "{PINNED_NDK_VERSION}"' in gradle:
        r.ok("Gradle NDK version pinned", PINNED_NDK_VERSION)
    else:
        r.fail("Gradle NDK version pinned", f'expected ndkVersion "{PINNED_NDK_VERSION}"')

    if 'file("google-services.json").exists()' in gradle:
        r.ok("Optional Firebase plugin guard", "conditional on google-services.json")
    else:
        r.fail("Optional Firebase plugin guard", "Firebase plugins must stay conditional on google-services.json")

    if 'android:allowNativeHeapPointerTagging="false"' in manifest:
        r.ok("Manifest Android 14+ TBI guard", 'allowNativeHeapPointerTagging="false"')
    else:
        r.fail("Manifest Android 14+ TBI guard", 'missing android:allowNativeHeapPointerTagging="false"')

    if 'android:name="com.sampmobile.game.SAMP"' in manifest and 'android:process=":game"' in manifest:
        r.ok("Manifest isolated :game process")
    else:
        r.fail("Manifest isolated :game process", 'com.sampmobile.game.SAMP must run in android:process=":game"')

    # Runtime assets
    assets_dir = REPO_ROOT / "app/src/main/assets"
    monet_zip = assets_dir / "monetloader_lib.zip"
    if not monet_zip.is_file():
        r.fail("monetloader_lib.zip integrity", "file missing")
    else:
        try:
            with zipfile.ZipFile(monet_zip, "r") as zf:
                names = set(zf.namelist())
                if "lib/moonloader.lua" in names:
                    r.ok("monetloader_lib.zip integrity", f"{len(names)} entries")
                else:
                    r.fail("monetloader_lib.zip integrity", "missing lib/moonloader.lua")
        except zipfile.BadZipFile as exc:
            r.fail("monetloader_lib.zip integrity", f"corrupt zip: {exc}")

    assetfile = assets_dir / "assetfile.txt"
    if not assetfile.is_file():
        if require_gtasa:
            r.fail("GTA:SA extracted APK assets", "missing assetfile.txt (run tools/engine/prepare_gtasa.py)")
        else:
            r.ok("GTA:SA proprietary assets stripped", "not present in clean repo")
        return

    lines = [ln.strip() for ln in assetfile.read_text(encoding="utf-8").splitlines() if ln.strip()]
    try:
        declared = int(lines[0])
        actual = len(lines) - 1
        if declared == actual:
            r.ok("GTA:SA assetfile.txt index", f"{actual} entries")
        else:
            r.fail("GTA:SA assetfile.txt index", f"header={declared}, actual={actual}")
    except (ValueError, IndexError):
        r.fail("GTA:SA assetfile.txt index", "invalid header count")

    core_assets = [
        "data/handling.cfg",
        "data/TouchDefaultPhoneWidescreen.cfg",
        "Text/american.gxt",
    ]
    missing_core = [c for c in core_assets if not (assets_dir / c).is_file()]
    if not (assets_dir / "data/script/mainV1.scm").is_file() and not (assets_dir / "data/mainV1.scm").is_file():
        missing_core.append("data/script/mainV1.scm")
    if not missing_core:
        r.ok("GTA:SA core runtime assets", f"{len(core_assets) + 1} verified")
    else:
        r.fail("GTA:SA core runtime assets", "missing: " + ", ".join(missing_core))
