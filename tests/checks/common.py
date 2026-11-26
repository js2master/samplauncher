"""Shared runner, constants, and Android SDK/NDK tool discovery."""

from __future__ import annotations

import glob
import os
import re
import shutil
import sys
from pathlib import Path
from typing import Optional

REPO_ROOT = Path(__file__).resolve().parent.parent.parent

PINNED_NDK_VERSION = "25.1.8937393"

# Always-bundled repository native dependencies (non-Rockstar)
REPO_JNILIBS = {
    "arm64-v8a": [
        "libAML.so",
        "libmonetloader.so",
        "libluajit-5.1.so",
        "libBASS.so",
        "libbass_ssl.so",
        "libOpenAL64.so",
        "libc++_shared.so",
    ],
    "armeabi-v7a": [
        "libAML.so",
        "libmonetloader.so",
        "libluajit-5.1.so",
        "libBASS.so",
        "libbass_ssl.so",
        "libOpenAL32.so",
        "libc++_shared.so",
    ],
}

# User-supplied Rockstar GTA:SA v2.10 binaries (gitignored; populated by tools/engine/prepare_gtasa.py)
PROPRIETARY_JNILIBS = {
    "arm64-v8a": ["libGTASA.so"],
    "armeabi-v7a": ["libGTASA.so", "libImmEmulatorJ.so"],
}

ELF_EXPECTED = {
    "arm64-v8a": (2, 183),   # ELFCLASS64, EM_AARCH64
    "armeabi-v7a": (1, 40),  # ELFCLASS32, EM_ARM
}

# Official Android NDK public system libraries guaranteed on API 24+ (minSdk 24)
ANDROID_NDK_PUBLIC_LIBS = {
    "libc.so",
    "libm.so",
    "libdl.so",
    "liblog.so",
    "libz.so",
    "libandroid.so",
    "libEGL.so",
    "libGLESv1_CM.so",
    "libGLESv2.so",
    "libGLESv3.so",
    "libOpenSLES.so",
    "libOpenMAXAL.so",
    "libjnigraphics.so",
    "libmediandk.so",
    "libcamera2ndk.so",
    "libvulkan.so",
    "libstdc++.so",
}


class Runner:
    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0
        self.warnings = 0

    def section(self, title: str) -> None:
        print(f"\n=== {title} ===")

    def ok(self, name: str, detail: str = "") -> None:
        self.passed += 1
        suffix = f" ({detail})" if detail else ""
        print(f"[PASS] {name}{suffix}")

    def fail(self, name: str, detail: str) -> None:
        self.failed += 1
        print(f"[FAIL] {name}: {detail}")

    def warn(self, name: str, detail: str) -> None:
        self.warnings += 1
        print(f"[WARN] {name}: {detail}")

    def info(self, name: str, detail: str) -> None:
        print(f"[INFO] {name}: {detail}")


def read_text(rel_path: str) -> str:
    return (REPO_ROOT / rel_path).read_text(encoding="utf-8", errors="replace")


def find_android_sdk() -> Optional[Path]:
    local_props = REPO_ROOT / "local.properties"
    if local_props.is_file():
        for line in local_props.read_text(encoding="utf-8", errors="ignore").splitlines():
            m = re.match(r"^\s*sdk\.dir\s*=\s*(.+?)\s*$", line)
            if m:
                raw = m.group(1).replace(r"\:", ":").replace(r"\\", "\\")
                p = Path(raw)
                if p.is_dir():
                    return p

    for env_key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        val = os.environ.get(env_key)
        if val and Path(val).is_dir():
            return Path(val)

    candidates = []
    if sys.platform == "win32":
        local_app = os.environ.get("LOCALAPPDATA", "")
        if local_app:
            candidates.append(Path(local_app) / "Android" / "Sdk")
    else:
        home = Path.home()
        candidates.extend([home / "Android" / "Sdk", home / "Library" / "Android" / "sdk"])

    for c in candidates:
        if c.is_dir():
            return c
    return None


def find_ndk_tool(tool_name: str) -> Optional[Path]:
    exts = (".exe", "") if sys.platform == "win32" else ("",)
    sdk = find_android_sdk()
    if sdk:
        ndk_root = sdk / "ndk"
        ndk_dirs = []
        pinned = ndk_root / PINNED_NDK_VERSION
        if pinned.is_dir():
            ndk_dirs.append(pinned)
        if ndk_root.is_dir():
            for d in sorted(ndk_root.iterdir(), reverse=True):
                if d.is_dir() and d != pinned:
                    ndk_dirs.append(d)
        for ndk_dir in ndk_dirs:
            for prebuilt in sorted((ndk_dir / "toolchains" / "llvm" / "prebuilt").glob("*")):
                for ext in exts:
                    cand = prebuilt / "bin" / f"{tool_name}{ext}"
                    if cand.is_file():
                        return cand

    for ext in exts:
        found = shutil.which(f"{tool_name}{ext}")
        if found:
            return Path(found)
    return None


def find_build_tool(tool_name: str) -> Optional[Path]:
    exts = (".exe", ".bat", "") if sys.platform == "win32" else ("",)
    sdk = find_android_sdk()
    if sdk:
        bt_root = sdk / "build-tools"
        if bt_root.is_dir():
            versions = sorted((d for d in bt_root.iterdir() if d.is_dir()), reverse=True)
            # Prefer 35.x (matching compileSdk 35) when available
            versions.sort(key=lambda d: (0 if d.name.startswith("35.") else 1, d.name), reverse=False)
            for ver_dir in versions:
                for ext in exts:
                    cand = ver_dir / f"{tool_name}{ext}"
                    if cand.is_file():
                        return cand

    for ext in exts:
        found = shutil.which(f"{tool_name}{ext}")
        if found:
            return Path(found)
    return None


def find_compatible_java_home() -> Optional[str]:
    """Locate JDK 17/21 for Gradle (avoids Java 25 major version 69 failure)."""
    def is_supported_jdk(home: Path) -> bool:
        rel = home / "release"
        if not rel.is_file():
            return False
        txt = rel.read_text(encoding="utf-8", errors="ignore")
        return bool(re.search(r'JAVA_VERSION="(?:17|21)\.', txt))

    cur = os.environ.get("JAVA_HOME")
    if cur and is_supported_jdk(Path(cur)):
        return cur

    home = Path.home()
    for pat in (
        str(home / ".jdks" / "jbr-21*"),
        str(home / ".jdks" / "*21*"),
        str(home / ".jdks" / "*17*"),
    ):
        for hit in sorted(glob.glob(pat), reverse=True):
            p = Path(hit)
            if is_supported_jdk(p):
                return str(p)
    return cur
