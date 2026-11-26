#!/usr/bin/env python3
"""
Extract user-supplied GTA:SA Android v2.10 native binaries and APK assets
into the local (gitignored) build directories before packaging samp.apk.

Supports three input modes:
  1. No arguments: every dist/gtasa*.apk is used (name must start with "gtasa", any case),
     e.g. dist/gtasa_2.10_arm64.apk and dist/gtasa_2.10_arm32.apk:
       python tools/engine/prepare_gtasa.py
  2. Local APK file(s) or Split-APK directory:
       python tools/engine/prepare_gtasa.py --apk /path/to/gtasa_2.10.apk
  3. Pulling installed com.rockstargames.gtasa directly from a connected ADB device/emulator:
       python tools/engine/prepare_gtasa.py --from-adb

Add --libs-only to extract only the native libraries and leave app/src/main/assets untouched
(use it when the current assets already work, or when several APKs with different assets are given).
A library that is byte-identical to the one already in jniLibs is left alone; one that differs
is first copied to build/gtasa-backup/ and then replaced.

To remove all locally extracted Rockstar files again:
       python tools/engine/prepare_gtasa.py --clean
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Dict, Iterable, List, Optional

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
JNILIBS_DIR = REPO_ROOT / "app/src/main/jniLibs"
ASSETS_DIR = REPO_ROOT / "app/src/main/assets"
DIST_DIR = REPO_ROOT / "dist"
BACKUP_DIR = REPO_ROOT / "build/gtasa-backup"
PROTECTED_ASSETS = {"monetloader_lib.zip"}

# Heavy OBB / game-data subtrees that may appear inside 2 GB+ "full" (OBB-merged) APKs
# or Play Asset Delivery split APKs; these belong in external files/, never inside samp.apk.
SKIP_HEAVY_ASSET_PREFIXES = (
    "texdb/",
    "anim/",
    "models/",
    "audio/sfx/",
    "audio/streams/",
    "audio/config/",
    "samp/",
)

# Verified GTA:SA Android v2.10 SHA-256 digests (for version mismatch warning only)
VERIFIED_V210_SHA256: Dict[str, str] = {
    "lib/arm64-v8a/libGTASA.so": "98726dd7516f5a070a754888e5d06cdbe771c216462447d19b47bedea6136fda",
    "lib/armeabi-v7a/libGTASA.so": "ee1a331cda53cdfcfbf5f5b217ca981f288224dad5a538f36b931bb1a0cd5a8a",
    "lib/armeabi-v7a/libImmEmulatorJ.so": "407a5d8dd82a1eb9c53fd29ce771c00ac9ab83f6562b8c97b86163ad6cf110de",
}

TARGET_LIBS = {
    "lib/arm64-v8a/libGTASA.so": JNILIBS_DIR / "arm64-v8a/libGTASA.so",
    "lib/armeabi-v7a/libGTASA.so": JNILIBS_DIR / "armeabi-v7a/libGTASA.so",
    "lib/armeabi-v7a/libImmEmulatorJ.so": JNILIBS_DIR / "armeabi-v7a/libImmEmulatorJ.so",
}


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def sanitize_fastman92_elf(path: Path) -> Optional[str]:
    """Strip Fastman92 pluginLoader hook from a patched GTA:SA v2.10 libGTASA.so.

    Many circulating GTA:SA v2.10 APKs have libGTASA.so patched by Fastman92 to
    inject DT_NEEDED=libFastman92pluginLoader.so and redirect JNI_OnLoad/JNI_OnUnload
    while keeping .text/.data/.bss intact and preserving Original_JNI_OnLoad/Unload.
    Removes the DT_NEEDED dependency, restores JNI_OnLoad/JNI_OnUnload, and compacts
    out the PluginLoader_* PLT relocations so stock Android Bionic loads it cleanly.
    Returns a summary string if patched, or None if no Fastman92 hook was present.
    """
    buf = bytearray(path.read_bytes())
    if len(buf) < 64 or buf[:4] != b"\x7fELF" or buf[5] != 1:
        return None
    ei_class = buf[4]
    if ei_class not in (1, 2):
        return None
    is64 = (ei_class == 2)

    if is64:
        e_shoff = struct.unpack_from("<Q", buf, 40)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", buf, 58)
        shstr_off, shstr_sz = struct.unpack_from("<QQ", buf, e_shoff + e_shstrndx * e_shentsize + 24)
    else:
        e_shoff = struct.unpack_from("<I", buf, 32)[0]
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", buf, 46)
        shstr_off, shstr_sz = struct.unpack_from("<II", buf, e_shoff + e_shstrndx * e_shentsize + 16)

    if e_shoff == 0 or e_shnum == 0 or shstr_off + shstr_sz > len(buf):
        return None
    shstr = bytes(buf[shstr_off:shstr_off + shstr_sz])

    secs: Dict[str, tuple] = {}
    for i in range(e_shnum):
        hdr_off = e_shoff + i * e_shentsize
        if is64:
            sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size, sh_link = struct.unpack_from("<IIQQQQI", buf, hdr_off)
            sh_size_pos = hdr_off + 32
        else:
            sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size, sh_link = struct.unpack_from("<IIIIIII", buf, hdr_off)
            sh_size_pos = hdr_off + 20
        nm = shstr[sh_name:].split(b"\x00", 1)[0].decode("ascii", errors="ignore")
        secs[nm] = (sh_type, sh_addr, sh_offset, sh_size, sh_size_pos)

    if ".dynamic" not in secs or ".dynstr" not in secs or ".dynsym" not in secs:
        return None

    _, _, dyn_off, dyn_sz, _ = secs[".dynamic"]
    _, _, str_off, str_sz, _ = secs[".dynstr"]
    _, _, sym_off, sym_sz, _ = secs[".dynsym"]
    dynstr = bytes(buf[str_off:str_off + str_sz])

    dyn_fmt = "<qQ" if is64 else "<iI"
    dyn_step = 16 if is64 else 8
    dyn_entries = []
    pos = dyn_off
    while pos + dyn_step <= dyn_off + dyn_sz:
        tag, val = struct.unpack_from(dyn_fmt, buf, pos)
        dyn_entries.append((tag, val))
        pos += dyn_step
        if tag == 0:
            break

    # Check whether libFastman92pluginLoader.so is in DT_NEEDED
    def is_fastman_needed(tag: int, val: int) -> bool:
        if tag != 1 or val >= len(dynstr):
            return False
        lib = dynstr[val:].split(b"\x00", 1)[0].decode("ascii", errors="ignore")
        return lib.lower() == "libfastman92pluginloader.so"

    if not any(is_fastman_needed(t, v) for t, v in dyn_entries):
        return None

    # 1. Inspect .dynsym for Original_JNI_OnLoad/Unload and PluginLoader_* symbols
    sym_step = 24 if is64 else 16
    sym_map: Dict[str, tuple] = {}
    plugin_syms = set()
    for i in range(sym_sz // sym_step):
        so = sym_off + i * sym_step
        if is64:
            st_name, st_info, st_other, st_shndx, st_val, st_size = struct.unpack_from("<IBBHQQ", buf, so)
        else:
            st_name, st_val, st_size, st_info, st_other, st_shndx = struct.unpack_from("<IIIBBH", buf, so)
        if st_name >= len(dynstr):
            continue
        nm = dynstr[st_name:].split(b"\x00", 1)[0].decode("ascii", errors="ignore")
        if nm in ("JNI_OnLoad", "JNI_OnUnload", "Original_JNI_OnLoad", "Original_JNI_OnUnload"):
            sym_map[nm] = (so, st_name, st_info, st_other, st_shndx, st_val, st_size)
        elif nm in ("PluginLoader_OnLoad", "PluginLoader_OnUnload"):
            plugin_syms.add(i)

    restored = []
    for target_nm, orig_nm in (("JNI_OnLoad", "Original_JNI_OnLoad"), ("JNI_OnUnload", "Original_JNI_OnUnload")):
        if target_nm in sym_map and orig_nm in sym_map:
            t_so, t_name, t_info, t_other, _, _, _ = sym_map[target_nm]
            _, _, _, _, o_shndx, o_val, o_size = sym_map[orig_nm]
            if is64:
                struct.pack_into("<IBBHQQ", buf, t_so, t_name, t_info, t_other, o_shndx, o_val, o_size)
            else:
                struct.pack_into("<IIIBBH", buf, t_so, t_name, o_val, o_size, t_info, t_other, o_shndx)
            restored.append(f"{target_nm}->{o_val:#x}")

    # 2. Compact out PluginLoader_* relocations from .rela.plt / .rel.plt
    removed_relocs = 0
    new_pltrelsz: Optional[int] = None
    for rsec_name, r_step, is_rela in ((".rela.plt", 24 if is64 else 12, True), (".rel.plt", 8, False)):
        if rsec_name not in secs or not plugin_syms:
            continue
        _, _, roff, rsz, rsz_pos = secs[rsec_name]
        kept = []
        for i in range(rsz // r_step):
            eo = roff + i * r_step
            raw = bytes(buf[eo:eo + r_step])
            if is64:
                _, r_info, _ = struct.unpack_from("<QQq", raw, 0)
                rsym = r_info >> 32
            else:
                _, r_info = struct.unpack_from("<II", raw, 0)
                rsym = r_info >> 8
            if rsym in plugin_syms:
                removed_relocs += 1
            else:
                kept.append(raw)
        if removed_relocs > 0:
            packed = b"".join(kept)
            buf[roff:roff + len(packed)] = packed
            buf[roff + len(packed):roff + rsz] = b"\x00" * (rsz - len(packed))
            new_pltrelsz = len(packed)
            struct.pack_into("<Q" if is64 else "<I", buf, rsz_pos, new_pltrelsz)
            break

    # 3. Rewrite .dynamic: strip DT_NEEDED=libFastman92pluginLoader.so and update DT_PLTRELSZ
    new_dyn = []
    for tag, val in dyn_entries:
        if tag == 0:
            break
        if is_fastman_needed(tag, val):
            continue
        if tag == 2 and new_pltrelsz is not None:  # DT_PLTRELSZ
            val = new_pltrelsz
        new_dyn.append((tag, val))
    while len(new_dyn) < len(dyn_entries):
        new_dyn.append((0, 0))
    for i, (tag, val) in enumerate(new_dyn):
        struct.pack_into(dyn_fmt, buf, dyn_off + i * dyn_step, tag, val)

    path.write_bytes(buf)
    details = ", ".join(restored) if restored else "JNI symbols unchanged"
    return f"stripped Fastman92 pluginLoader ({removed_relocs} PLT relocs, {details})"


def relocate_program_headers(path: Path) -> Optional[str]:
    """Move the program header table back to its standard place right after the ELF header.

    Fastman92-patched libGTASA.so files keep their program headers in an extra segment at the
    end of the image (e_phoff far away from the ELF header). Android's own linker copes with
    that, but ARM translation layers on x86 emulators (libhoudini on LDPlayer) read the table
    at "load base + e_phoff" and crash while loading the library (SIGFPE / SIGSEGV inside
    libhoudini.so before JNI_OnLoad runs). The original dynamic-symbol tables that used to
    follow the header were moved by the patcher as well, so that space is free to reuse.
    Returns a summary string if the file was rewritten, or None if nothing had to be done or
    the layout is not one we can safely fix.
    """
    buf = bytearray(path.read_bytes())
    if len(buf) < 64 or buf[:4] != b"\x7fELF" or buf[5] != 1 or buf[4] not in (1, 2):
        return None
    is64 = buf[4] == 2
    ehsize = 0x40 if is64 else 0x34
    if is64:
        e_phoff = struct.unpack_from("<Q", buf, 0x20)[0]
        e_shoff = struct.unpack_from("<Q", buf, 0x28)[0]
        e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", buf, 0x36)
        ph_fmt, phnum_pos = "<IIQQQQQQ", 0x38
    else:
        e_phoff = struct.unpack_from("<I", buf, 0x1C)[0]
        e_shoff = struct.unpack_from("<I", buf, 0x20)[0]
        e_phentsize, e_phnum, e_shentsize, e_shnum = struct.unpack_from("<HHHH", buf, 0x2A)
        ph_fmt, phnum_pos = "<IIIIIIII", 0x2C
    if e_phoff == ehsize or e_phnum == 0 or e_phentsize != struct.calcsize(ph_fmt):
        return None

    def unpack(off: int) -> dict:
        v = struct.unpack_from(ph_fmt, buf, off)
        if is64:
            keys = ("type", "flags", "off", "vaddr", "paddr", "filesz", "memsz", "align")
        else:
            keys = ("type", "off", "vaddr", "paddr", "filesz", "memsz", "flags", "align")
        return dict(zip(keys, v))

    def pack(e: dict) -> bytes:
        if is64:
            return struct.pack(ph_fmt, e["type"], e["flags"], e["off"], e["vaddr"], e["paddr"], e["filesz"], e["memsz"], e["align"])
        return struct.pack(ph_fmt, e["type"], e["off"], e["vaddr"], e["paddr"], e["filesz"], e["memsz"], e["flags"], e["align"])

    entries = [unpack(e_phoff + i * e_phentsize) for i in range(e_phnum)]
    first_load = next((e for e in entries if e["type"] == 1 and e["off"] == 0 and e["vaddr"] == 0), None)
    old_phdr = next((e for e in entries if e["type"] == 6), None)
    if first_load is None:
        return None

    # Drop the segment whose only job was to map the relocated table, and the old PT_PHDR entry.
    table_vaddr = old_phdr["vaddr"] if old_phdr else None
    kept = [e for e in entries if e["type"] != 6
            and not (e["type"] == 1 and table_vaddr is not None and e["vaddr"] == table_vaddr
                     and e["filesz"] <= e_phnum * e_phentsize)]
    count = len(kept) + 1
    table_size = count * e_phentsize
    table_end = ehsize + table_size
    if table_end > first_load["filesz"]:
        return None

    # The target range must not hold anything the loader or the code still uses.
    def vaddr_in_use(lo: int, hi: int) -> Optional[str]:
        for e in kept:
            if e["type"] in (2, 4, 7, 0x6474E550, 0x6474E552) and e["vaddr"] < hi and e["vaddr"] + e["memsz"] > lo:
                return f"program header type 0x{e['type']:x}"
        dyn = next((e for e in kept if e["type"] == 2), None)
        if dyn is not None:
            step = 16 if is64 else 8
            ptr_tags = {3, 4, 5, 6, 7, 12, 13, 17, 23, 25, 26, 0x6FFFFEF5, 0x6FFFFFF0, 0x6FFFFFFC, 0x6FFFFFFE}
            for i in range(0, dyn["filesz"], step):
                tag, val = struct.unpack_from("<QQ" if is64 else "<II", buf, dyn["off"] + i)
                if tag == 0:
                    break
                if tag in ptr_tags and lo <= val < hi:
                    return f"dynamic tag 0x{tag:x}"
        if e_shoff and e_shnum and e_shoff + e_shnum * e_shentsize <= len(buf):
            for i in range(e_shnum):
                h = e_shoff + i * e_shentsize
                if is64:
                    _n, sh_type, sh_flags, sh_addr, _o, sh_size = struct.unpack_from("<IIQQQQ", buf, h)
                else:
                    _n, sh_type, sh_flags, sh_addr, _o, sh_size = struct.unpack_from("<IIIIII", buf, h)
                if sh_type not in (0, 8) and (sh_flags & 2) and sh_size and sh_addr < hi and sh_addr + sh_size > lo:
                    return f"section #{i}"
        return None

    blocker = vaddr_in_use(ehsize, table_end)
    if blocker:
        print(f"  [WARN]  program headers left in place: target range is used by {blocker}")
        return None

    phdr = {"type": 6, "flags": 4, "off": ehsize, "vaddr": ehsize, "paddr": ehsize,
            "filesz": table_size, "memsz": table_size, "align": 8 if is64 else 4}
    blob = b"".join(pack(e) for e in [phdr] + kept)
    buf[ehsize:table_end] = blob
    struct.pack_into("<Q" if is64 else "<I", buf, 0x20 if is64 else 0x1C, ehsize)
    struct.pack_into("<H", buf, phnum_pos, count)
    path.write_bytes(buf)
    return f"program headers moved back to 0x{ehsize:x} ({e_phnum} -> {count} entries) for emulator compatibility"


def resolve_adb(explicit: Optional[str]) -> str:
    if explicit:
        if not Path(explicit).is_file():
            raise FileNotFoundError(f"Specified adb not found: {explicit}")
        return explicit

    which = shutil.which("adb")
    if which:
        return which

    candidates: List[Path] = []
    for env_key in ("ANDROID_HOME", "ANDROID_SDK_ROOT"):
        val = os.environ.get(env_key)
        if val:
            candidates.append(Path(val))

    local_props = REPO_ROOT / "local.properties"
    if local_props.is_file():
        for line in local_props.read_text(encoding="utf-8", errors="ignore").splitlines():
            m = re.match(r"^\s*sdk\.dir\s*=\s*(.+)$", line)
            if m:
                raw = m.group(1).strip().replace("\\\\", "\\").replace("\\:", ":")
                candidates.append(Path(raw))

    exe = "adb.exe" if os.name == "nt" else "adb"
    for sdk in candidates:
        p = sdk / "platform-tools" / exe
        if p.is_file():
            return str(p)

    raise FileNotFoundError("adb not found. Add platform-tools to PATH, set ANDROID_HOME, or pass --adb.")


def pull_apks_from_adb(adb: str, serial: Optional[str], package: str, tmp_dir: Path) -> List[Path]:
    base_cmd = [adb]
    if serial:
        base_cmd += ["-s", serial]

    proc = subprocess.run(
        base_cmd + ["shell", "pm", "path", package],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0 or not proc.stdout.strip():
        raise RuntimeError(
            f"Package '{package}' not found on device (adb pm path failed).\n"
            f"stdout: {proc.stdout.strip()}\nstderr: {proc.stderr.strip()}"
        )

    remote_paths = [
        line.split(":", 1)[1].strip()
        for line in proc.stdout.splitlines()
        if line.strip().startswith("package:")
    ]
    if not remote_paths:
        raise RuntimeError(f"No APK paths returned for package '{package}'.")

    pulled: List[Path] = []
    for idx, rpath in enumerate(remote_paths):
        local_apk = tmp_dir / f"pulled_{idx}_{Path(rpath).name}"
        print(f"Pulling {rpath} -> {local_apk.name} ...")
        res = subprocess.run(base_cmd + ["pull", rpath, str(local_apk)], check=False)
        if res.returncode != 0 or not local_apk.is_file():
            raise RuntimeError(f"adb pull failed for {rpath}")
        pulled.append(local_apk)
    return pulled


def collect_apk_paths(inputs: Iterable[str]) -> List[Path]:
    apks: List[Path] = []
    for raw in inputs:
        p = Path(raw)
        if p.is_file():
            apks.append(p)
        elif p.is_dir():
            apks.extend(sorted(p.glob("*.apk")))
        else:
            raise FileNotFoundError(f"APK path does not exist: {raw}")
    if not apks:
        raise FileNotFoundError("No .apk files found in provided inputs.")
    return apks


def find_dist_apks() -> List[Path]:
    """APKs in dist/ whose file name starts with "gtasa" (case-insensitive), sorted by name."""
    if not DIST_DIR.is_dir():
        return []
    return sorted(p for p in DIST_DIR.glob("*.apk") if p.name.lower().startswith("gtasa"))


def install_lib(tmp: Path, dest: Path) -> str:
    """Move a freshly extracted library into jniLibs; keep the old one in build/gtasa-backup/ if it differs."""
    if dest.is_file():
        if sha256_file(dest) == sha256_file(tmp):
            tmp.unlink()
            return "unchanged"
        backup = BACKUP_DIR / dest.parent.name / dest.name
        backup.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(dest, backup)
        note = f"replaced, previous copy kept at {backup.relative_to(REPO_ROOT)}"
    else:
        note = "new"
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.move(str(tmp), str(dest))
    return note


def clean_extracted() -> None:
    removed = 0
    for dest in TARGET_LIBS.values():
        if dest.is_file():
            dest.unlink()
            print(f"Removed: {dest.relative_to(REPO_ROOT)}")
            removed += 1

    if ASSETS_DIR.is_dir():
        for item in sorted(ASSETS_DIR.iterdir()):
            if item.name in PROTECTED_ASSETS:
                continue
            if item.is_dir():
                shutil.rmtree(item)
            else:
                item.unlink()
            print(f"Removed: {item.relative_to(REPO_ROOT)}")
            removed += 1
    if BACKUP_DIR.is_dir():
        shutil.rmtree(BACKUP_DIR)
        print(f"Removed: {BACKUP_DIR.relative_to(REPO_ROOT)}")
        removed += 1
    print(f"Clean complete ({removed} items removed).")


def clean_extracted_assets_only() -> None:
    if ASSETS_DIR.is_dir():
        for item in sorted(ASSETS_DIR.iterdir()):
            if item.name in PROTECTED_ASSETS:
                continue
            if item.is_dir():
                shutil.rmtree(item)
            else:
                item.unlink()


def extract_from_apks(apks: List[Path], libs_only: bool = False) -> int:
    # Pre-scan APKs before touching local assets/ so incompatible APKs (e.g. v2.11+ or wrapper APKs)
    # fail fast without polluting app/src/main/assets/.
    found_any_gtasa = False
    for apk in apks:
        with zipfile.ZipFile(apk, "r") as zf:
            for info in zf.infolist():
                name = info.filename.replace("\\", "/")
                if name in ("lib/arm64-v8a/libGTASA.so", "lib/armeabi-v7a/libGTASA.so"):
                    found_any_gtasa = True
                    break
        if found_any_gtasa:
            break

    if not found_any_gtasa:
        print("ERROR: No lib/arm64-v8a/libGTASA.so or lib/armeabi-v7a/libGTASA.so found in provided APK(s).")
        print("       Make sure you are using GTA:SA Android v2.10 (if using Split APKs, include ABI split APKs).")
        return 1

    if not libs_only:
        clean_extracted_assets_only()

    extracted_libs: List[str] = []
    lib_source: Dict[str, Path] = {}
    extracted_assets = 0
    skipped_heavy = 0

    for apk in apks:
        print(f"Inspecting {apk} ...")
        with zipfile.ZipFile(apk, "r") as zf:
            for info in zf.infolist():
                if info.is_dir():
                    continue
                name = info.filename.replace("\\", "/")

                # 1. Native GTA:SA libraries
                if name in TARGET_LIBS:
                    dest = TARGET_LIBS[name]
                    if name in lib_source:
                        print(f"  [SKIP]  {name}: already taken from {lib_source[name].name}")
                        continue
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    tmp = dest.with_name(dest.name + ".extracting")
                    with zf.open(info, "r") as src, tmp.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                    digest = sha256_file(tmp)
                    expected = VERIFIED_V210_SHA256.get(name)
                    if digest == expected:
                        status = "verified v2.10"
                    else:
                        patch_note = sanitize_fastman92_elf(tmp) if name.endswith("libGTASA.so") else None
                        if patch_note:
                            status = f"auto-sanitized ({patch_note})"
                        else:
                            status = "WARNING: SHA-256 differs from verified v2.10"
                        if name.endswith("libGTASA.so"):
                            phdr_note = relocate_program_headers(tmp)
                            if phdr_note:
                                status += "; " + phdr_note
                    status += "; " + install_lib(tmp, dest)
                    print(f"  [LIB]   {name} -> {dest.relative_to(REPO_ROOT)} ({status})")
                    extracted_libs.append(name)
                    lib_source[name] = apk
                    continue

                # 2. Lightweight APK assets/ (skip heavy OBB trees if a 2 GB+ merged APK was passed)
                if libs_only:
                    continue
                if name.startswith("assets/"):
                    rel = name[len("assets/"):]
                    if not rel or rel.startswith("/") or ".." in Path(rel).parts:
                        continue
                    if rel in PROTECTED_ASSETS:
                        continue
                    rel_lower = rel.lower()
                    if any(rel_lower.startswith(prefix) for prefix in SKIP_HEAVY_ASSET_PREFIXES):
                        skipped_heavy += 1
                        continue
                    dest = ASSETS_DIR / rel
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    with zf.open(info, "r") as src, dest.open("wb") as dst:
                        shutil.copyfileobj(src, dst)
                    extracted_assets += 1

    # Ensure assetfile.txt exists (NvAPKFileHelper uses it as a fast index)
    assetfile = ASSETS_DIR / "assetfile.txt"
    if not libs_only and extracted_assets > 0 and not assetfile.is_file():
        entries = sorted(
            p.relative_to(ASSETS_DIR).as_posix()
            for p in ASSETS_DIR.rglob("*")
            if p.is_file() and p.name not in PROTECTED_ASSETS and p.name != "assetfile.txt"
        )
        all_entries = ["assetfile.txt"] + entries
        assetfile.write_text(f"{len(all_entries)}\n" + "\n".join(all_entries) + "\n", encoding="utf-8")
        print(f"  [INDEX] Generated {assetfile.relative_to(REPO_ROOT)} ({len(all_entries)} entries)")

    msg = f"\nExtracted {len(extracted_libs)} native libraries and {extracted_assets} asset files."
    if libs_only:
        msg = f"\nExtracted {len(extracted_libs)} native libraries (--libs-only: assets left untouched)."
    if skipped_heavy:
        msg += f" (Skipped {skipped_heavy} heavy OBB/gamedata files — use tools/gamedata/build_gamedata.py for those.)"
    print(msg)
    missing_libs = [k for k in ("lib/arm64-v8a/libGTASA.so", "lib/armeabi-v7a/libGTASA.so") if not TARGET_LIBS[k].is_file()]
    if missing_libs:
        print("WARNING: Missing expected GTA:SA libraries: " + ", ".join(missing_libs))
        print("         If using Split APKs, pass both base.apk and ABI split APKs (or a directory containing them).")
        return 1
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Extract user-owned GTA:SA v2.10 binaries and APK assets for local builds.")
    ap.add_argument("--apk", nargs="+", help="Path(s) to GTA:SA Android v2.10 APK file(s) or Split-APK folder")
    ap.add_argument("--from-adb", action="store_true", help="Pull installed GTA:SA APK from a connected ADB device/emulator")
    ap.add_argument("--adb", help="Explicit path to adb executable (used with --from-adb)")
    ap.add_argument("--serial", help="Target ADB device serial (used with --from-adb)")
    ap.add_argument("--package", default="com.rockstargames.gtasa", help="Installed package name (default: com.rockstargames.gtasa)")
    ap.add_argument("--clean", action="store_true", help="Remove all locally extracted Rockstar binaries and GTA assets")
    ap.add_argument("--libs-only", action="store_true", help="Extract native libraries only; do not touch app/src/main/assets")
    args = ap.parse_args()

    if args.clean:
        clean_extracted()
        return 0

    if args.from_adb:
        adb = resolve_adb(args.adb)
        with tempfile.TemporaryDirectory(prefix="gtasa_adb_") as td:
            apks = pull_apks_from_adb(adb, args.serial, args.package, Path(td))
            return extract_from_apks(apks, args.libs_only)

    if args.apk:
        apks = collect_apk_paths(args.apk)
    else:
        apks = find_dist_apks()
        if not apks:
            ap.error('No APK given and no dist/gtasa*.apk found. Put the GTA:SA v2.10 APK(s) in dist/ with a name '
                     'starting with "gtasa" (e.g. dist/gtasa_2.10_arm64.apk), or use --apk / --from-adb.')
        print("Using APKs found in dist/: " + ", ".join(p.name for p in apks))
        if len(apks) > 1 and not args.libs_only:
            print("NOTE: several APKs given without --libs-only: assets are wiped once, then files with the same")
            print("      name are overwritten by the APK that comes later in the list above.")
    return extract_from_apks(apks, args.libs_only)


if __name__ == "__main__":
    sys.exit(main())
