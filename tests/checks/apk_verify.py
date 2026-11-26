"""Stage 4: Official Android SDK Build-Tools APK verification (aapt2, zipalign, apksigner)."""

from __future__ import annotations

import re
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import List

from .common import PROPRIETARY_JNILIBS, REPO_JNILIBS, Runner, find_build_tool


def _run_tool(cmd: List[str]) -> subprocess.CompletedProcess:
    use_shell = sys.platform == "win32" and cmd[0].lower().endswith(".bat")
    return subprocess.run(cmd, capture_output=True, text=True, errors="ignore", shell=use_shell)


def run_apk_checks(
    r: Runner,
    apk_path: Path,
    required: bool = False,
    require_gtasa: bool = False,
) -> None:
    r.section("Stage 4: APK Artifact & Official Android SDK Verification")

    if not apk_path.is_file():
        if required:
            r.fail("APK artifact present", f"APK not found: {apk_path}")
        else:
            r.info("APK artifact checks", f"skipped ({apk_path.name} not built yet)")
        return

    # 1) ZIP structure & dual-ABI completeness
    try:
        with zipfile.ZipFile(apk_path, "r") as zf:
            names = set(zf.namelist())
    except zipfile.BadZipFile as exc:
        r.fail("APK ZIP integrity", f"corrupt APK: {exc}")
        return

    missing: List[str] = []
    base_entries = ["classes.dex", "AndroidManifest.xml", "assets/monetloader_lib.zip"]
    if require_gtasa:
        base_entries.append("assets/assetfile.txt")
    for entry in base_entries:
        if entry not in names:
            missing.append(entry)

    for abi, libs in REPO_JNILIBS.items():
        expected = libs + ["libSAMPMOBILE.so", "libSCAnd.so"]
        if require_gtasa:
            expected += PROPRIETARY_JNILIBS[abi]
        for lib in expected:
            entry = f"lib/{abi}/{lib}"
            if entry not in names:
                missing.append(entry)

    size_mb = apk_path.stat().st_size / (1024 * 1024)
    if not missing:
        r.ok("APK structure & dual-ABI libraries", f"{apk_path.name} ({size_mb:.2f} MB)")
    else:
        r.fail("APK structure & dual-ABI libraries", "missing: " + ", ".join(missing))

    # 2) Official aapt2 dump badging + compiled AndroidManifest.xml verification
    aapt2 = find_build_tool("aapt2")
    if not aapt2:
        r.warn("aapt2 compiled manifest verification", "aapt2 not found in Android SDK build-tools")
    else:
        badging = _run_tool([str(aapt2), "dump", "badging", str(apk_path)])
        if badging.returncode != 0:
            r.fail("aapt2 dump badging", (badging.stderr or badging.stdout).strip()[:240])
        else:
            out = badging.stdout
            errs: List[str] = []
            if "package: name='com.sampmobile.launcher'" not in out:
                errs.append("unexpected package name")
            if not re.search(r"(?:minSdkVersion|sdkVersion):'24'", out):
                errs.append("expected minSdk=24")
            if "targetSdkVersion:'35'" not in out:
                errs.append("expected targetSdk=35")
            nc = re.search(r"native-code:\s*(.+)", out)
            if not nc or "'arm64-v8a'" not in nc.group(1) or "'armeabi-v7a'" not in nc.group(1):
                errs.append("native-code missing dual ABI (arm64-v8a + armeabi-v7a)")
            if not errs:
                r.ok("aapt2 compiled APK metadata", "pkg=com.sampmobile.launcher, minSdk=24, targetSdk=35, dual-ABI")
            else:
                r.fail("aapt2 compiled APK metadata", "; ".join(errs))

        xmltree = _run_tool([str(aapt2), "dump", "xmltree", "--file", "AndroidManifest.xml", str(apk_path)])
        if xmltree.returncode != 0:
            r.fail("aapt2 compiled AndroidManifest.xml", (xmltree.stderr or xmltree.stdout).strip()[:240])
        else:
            xt = xmltree.stdout
            m_errs: List[str] = []
            if "extractNativeLibs" not in xt or not re.search(r"extractNativeLibs.*=\s*(?:true|\(type 0x12\)0xffffffff)", xt):
                m_errs.append("extractNativeLibs!=true")
            if "allowNativeHeapPointerTagging" not in xt or not re.search(
                r"allowNativeHeapPointerTagging.*=\s*(?:false|\(type 0x12\)0x0\b)", xt
            ):
                m_errs.append("allowNativeHeapPointerTagging!=false")
            if not m_errs:
                r.ok("aapt2 compiled manifest flags", "extractNativeLibs=true, allowNativeHeapPointerTagging=false")
            else:
                r.fail("aapt2 compiled manifest flags", "; ".join(m_errs))

    # 3) Official zipalign -c 4 verification
    zipalign = find_build_tool("zipalign")
    if not zipalign:
        r.warn("zipalign APK alignment", "zipalign not found in Android SDK build-tools")
    else:
        za = _run_tool([str(zipalign), "-c", "4", str(apk_path)])
        if za.returncode == 0:
            r.ok("zipalign 4-byte APK alignment", "verified by Android SDK zipalign")
        else:
            r.fail("zipalign 4-byte APK alignment", (za.stderr or za.stdout).strip()[:240])

    # 4) Official apksigner cryptographic signature verification
    apksigner = find_build_tool("apksigner")
    if not apksigner:
        r.warn("apksigner APK signature", "apksigner not found in Android SDK build-tools")
    else:
        sig = _run_tool([str(apksigner), "verify", "--verbose", str(apk_path)])
        if sig.returncode == 0:
            schemes = [
                f"v{v}"
                for v in ("1", "2", "3", "4")
                if re.search(rf"Verified using v{v} scheme.*:\s*true", sig.stdout)
            ]
            r.ok("apksigner APK signature", f"valid ({', '.join(schemes) or 'signed'})")
        else:
            r.fail("apksigner APK signature", (sig.stderr or sig.stdout).strip()[:240])
