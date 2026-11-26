"""Stage 3: Objective binary/ELF, dual-ABI Symbols.def, DT_NEEDED, TEXTREL, and real JNI checks.

Backed by official Android NDK LLVM tools (llvm-nm, llvm-readelf).
"""

from __future__ import annotations

import re
import struct
import subprocess
import tempfile
import zipfile
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple

from .common import (
    ANDROID_NDK_PUBLIC_LIBS,
    ELF_EXPECTED,
    PROPRIETARY_JNILIBS,
    REPO_JNILIBS,
    REPO_ROOT,
    Runner,
    find_ndk_tool,
)

SYMBOLS_DEF = REPO_ROOT / "app/src/main/cpp/SAMPMOBILE/Game/Engine/Symbols.def"
SO_GTASA = {
    "arm64-v8a": REPO_ROOT / "app/src/main/jniLibs/arm64-v8a/libGTASA.so",
    "armeabi-v7a": REPO_ROOT / "app/src/main/jniLibs/armeabi-v7a/libGTASA.so",
}

OWN_LIBS = ("libSAMPMOBILE.so", "libSCAnd.so")


def _load_dynamic_exports(nm_bin: Path, so_path: Path) -> Dict[str, str]:
    out = subprocess.check_output(
        [str(nm_bin), "-D", "--defined-only", str(so_path)],
        text=True,
        errors="ignore",
    )
    exports: Dict[str, str] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            exports[parts[2]] = parts[1]
    return exports


def _parse_symbols_def(path: Path) -> List[Tuple[str, str, str, str, str]]:
    pat_data = re.compile(r'^\s*SYM_DATA\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*([^,]+?)\s*,\s*(Required|Optional)\s*\)')
    pat_fn = re.compile(r'^\s*SYM_FN\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*([^,]+?)\s*,\s*(Required|Optional)\s*\)')
    pat_abi = re.compile(
        r'^\s*SYM_(?:FN_)?ABI\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*"([^"]+)"\s*,\s*([^,]+?)\s*,\s*(Required|Optional)\s*\)'
    )
    entries: List[Tuple[str, str, str, str, str]] = []
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = pat_data.match(line)
        if m:
            entries.append(("data", m.group(1), m.group(2), m.group(2), m.group(4)))
            continue
        m = pat_fn.match(line)
        if m:
            entries.append(("fn", m.group(1), m.group(2), m.group(2), m.group(4)))
            continue
        m = pat_abi.match(line)
        if m:
            entries.append(("fn", m.group(1), m.group(2), m.group(3), m.group(5)))
    return entries


def _check_prebuilt_jnilibs(r: Runner, require_gtasa: bool) -> None:
    jni_root = REPO_ROOT / "app/src/main/jniLibs"
    errors: List[str] = []
    total_checked = 0
    prop_present = 0
    prop_missing: List[str] = []

    for abi, required in REPO_JNILIBS.items():
        abi_dir = jni_root / abi
        exp_class, exp_machine = ELF_EXPECTED[abi]
        for lib in required:
            if not (abi_dir / lib).is_file():
                errors.append(f"missing {abi}/{lib}")

        for plib in PROPRIETARY_JNILIBS[abi]:
            if (abi_dir / plib).is_file():
                prop_present += 1
            else:
                prop_missing.append(f"{abi}/{plib}")

        if abi_dir.is_dir():
            for so in sorted(abi_dir.glob("*.so")):
                total_checked += 1
                hdr = so.read_bytes()[:20]
                if len(hdr) < 20 or hdr[:4] != b"\x7fELF":
                    errors.append(f"{abi}/{so.name}: not a valid ELF")
                    continue
                ei_class = hdr[4]
                e_machine = struct.unpack_from("<H", hdr, 18)[0]
                if (ei_class, e_machine) != (exp_class, exp_machine):
                    errors.append(
                        f"{abi}/{so.name}: wrong ELF arch (class={ei_class}, machine={e_machine})"
                    )

    if not errors:
        r.ok("Dual-ABI jniLibs ELF integrity", f"{total_checked} .so files verified")
    else:
        r.fail("Dual-ABI jniLibs ELF integrity", "; ".join(errors))

    if prop_present == 0:
        if require_gtasa:
            r.fail(
                "Rockstar proprietary binaries",
                "missing: " + ", ".join(prop_missing) + " (run tools/engine/prepare_gtasa.py)",
            )
        else:
            r.ok("Rockstar proprietary binaries stripped", "not present in clean repo")
    elif not prop_missing:
        r.ok("Rockstar proprietary binaries present", f"{prop_present} files verified")
    elif not require_gtasa and "arm64-v8a/libGTASA.so" not in prop_missing:
        r.warn("Rockstar proprietary binaries (64-bit only)", "missing 32-bit: " + ", ".join(prop_missing))
    else:
        r.fail("Rockstar proprietary binaries incomplete", "missing: " + ", ".join(prop_missing))


def _check_symbols_def(r: Runner, nm_bin: Optional[Path], require_gtasa: bool) -> None:
    if not SYMBOLS_DEF.is_file():
        r.fail("Symbols.def present", "missing app/src/main/cpp/SAMPMOBILE/Game/Engine/Symbols.def")
        return

    entries = _parse_symbols_def(SYMBOLS_DEF)
    seen_names: Set[str] = set()
    dup_names: List[str] = []
    for _, name, _, _, _ in entries:
        if name in seen_names:
            dup_names.append(name)
        seen_names.add(name)
    if dup_names:
        r.fail("Symbols.def uniqueness", "duplicate names: " + ", ".join(dup_names[:10]))
        return

    if not nm_bin:
        r.warn("Symbols.def dual-ABI export verification", "llvm-nm not found; skipped")
        return

    for abi, so_path in SO_GTASA.items():
        if not so_path.is_file():
            if require_gtasa:
                r.fail(f"Symbols.def [{abi}]", f"missing {so_path.relative_to(REPO_ROOT).as_posix()}")
            else:
                r.info(f"Symbols.def [{abi}]", "skipped (local libGTASA.so not extracted)")
            continue

        exports = _load_dynamic_exports(nm_bin, so_path)
        missing: List[str] = []
        mismatches: List[str] = []
        seen_syms: Set[str] = set()
        dup_syms: List[str] = []

        for kind, name, s64, s32, req in entries:
            sym = s64 if abi == "arm64-v8a" else s32
            if sym in seen_syms:
                dup_syms.append(f"{name}->{sym}")
            seen_syms.add(sym)

            if sym not in exports:
                if req == "Required":
                    missing.append(f"{name} ({sym})")
                continue

            nm_type = exports[sym]
            if kind == "fn" and nm_type not in ("T", "W", "t"):
                mismatches.append(f"{name}: expected code, got '{nm_type}'")
            elif kind == "data" and nm_type in ("T", "t"):
                mismatches.append(f"{name}: expected data, got '{nm_type}'")

        if not dup_syms and not missing and not mismatches:
            r.ok(f"Symbols.def [{abi}]", f"{len(entries)} symbols verified against {len(exports)} libGTASA exports")
        else:
            parts = []
            if dup_syms:
                parts.append(f"dup={len(dup_syms)} ({dup_syms[0]})")
            if missing:
                parts.append(f"missing={len(missing)} ({', '.join(missing[:5])})")
            if mismatches:
                parts.append(f"type_mismatch={len(mismatches)} ({', '.join(mismatches[:5])})")
            r.fail(f"Symbols.def [{abi}]", "; ".join(parts))


def _resolve_compiled_libs(apk_path: Optional[Path], tmp_dir: Path) -> Dict[str, Dict[str, Path]]:
    """Locate compiled libSAMPMOBILE.so / libSCAnd.so from APK (preferred) or Gradle intermediates."""
    found: Dict[str, Dict[str, Path]] = {"arm64-v8a": {}, "armeabi-v7a": {}}

    if apk_path and apk_path.is_file():
        try:
            with zipfile.ZipFile(apk_path, "r") as zf:
                for abi in ("arm64-v8a", "armeabi-v7a"):
                    for lib in OWN_LIBS:
                        entry = f"lib/{abi}/{lib}"
                        if entry in zf.namelist():
                            dest = tmp_dir / abi / lib
                            dest.parent.mkdir(parents=True, exist_ok=True)
                            dest.write_bytes(zf.read(entry))
                            found[abi][lib] = dest
        except zipfile.BadZipFile:
            pass

    for abi in ("arm64-v8a", "armeabi-v7a"):
        for lib in OWN_LIBS:
            if lib in found[abi]:
                continue
            for cand in (
                REPO_ROOT / f"app/build/intermediates/stripped_native_libs/debug/stripDebugDebugSymbols/out/lib/{abi}/{lib}",
                REPO_ROOT / f"app/build/intermediates/merged_native_libs/debug/mergeDebugNativeLibs/out/lib/{abi}/{lib}",
            ):
                if cand.is_file():
                    found[abi][lib] = cand
                    break
    return found


def _collect_java_native_methods() -> List[Tuple[str, str, str]]:
    java_root = REPO_ROOT / "app/src/main/java/com/sampmobile"
    native_decl_re = re.compile(r"\bnative\s+[\w<>\[\], ?]+\s+(\w+)\s*\(")
    pkg_re = re.compile(r"^\s*package\s+([\w.]+)\s*;", re.MULTILINE)
    methods: List[Tuple[str, str, str]] = []
    string_lit_re = re.compile(r'"(?:\\.|[^"\\])*"')
    char_lit_re = re.compile(r"'(?:\\.|[^'\\])'")
    for p in sorted(java_root.rglob("*.java")):
        text = p.read_text(encoding="utf-8", errors="ignore")
        text = string_lit_re.sub('""', text)
        text = char_lit_re.sub("''", text)
        pkg_m = pkg_re.search(text)
        if not pkg_m:
            continue
        pkg = pkg_m.group(1)
        cls = p.stem
        for m in native_decl_re.finditer(text):
            method = m.group(1)
            jni_sym = (
                "Java_"
                + pkg.replace("_", "_1").replace(".", "_")
                + "_"
                + cls.replace("_", "_1")
                + "_"
                + method.replace("_", "_1")
            )
            methods.append((f"{pkg}.{cls}.{method}", method, jni_sym))
    return methods


def _check_compiled_binaries_and_jni(
    r: Runner,
    nm_bin: Optional[Path],
    readelf_bin: Optional[Path],
    apk_path: Optional[Path],
) -> None:
    methods = _collect_java_native_methods()
    cpp_root = REPO_ROOT / "app/src/main/cpp/SAMPMOBILE"
    cpp_blob = "\n".join(
        p.read_text(encoding="utf-8", errors="ignore")
        for p in cpp_root.rglob("*")
        if p.is_file() and p.suffix in {".cpp", ".h", ".cc", ".hpp"}
    )

    with tempfile.TemporaryDirectory(prefix="samp_abi_check_") as td:
        compiled = _resolve_compiled_libs(apk_path, Path(td))
        has_any_compiled = any(bool(v) for v in compiled.values())

        if not has_any_compiled or not readelf_bin or not nm_bin:
            # Source-level JNI fallback when compiled .so or NDK tools are absent
            missing = [
                f"{fq} -> {sym}"
                for fq, method, sym in methods
                if sym not in cpp_blob and not ("RegisterNatives" in cpp_blob and f'"{method}"' in cpp_blob)
            ]
            if not missing:
                r.ok("JNI native method bindings (source fallback)", f"{len(methods)} Java native methods verified")
            else:
                r.fail("JNI native method bindings", "; ".join(missing))
            if not has_any_compiled:
                r.info("Compiled .so ELF/DT_NEEDED/TEXTREL audit", "skipped (no built APK or CMake intermediates yet)")
            return

        # 1) DT_NEEDED, TEXTREL, PT_LOAD alignment via llvm-readelf
        textrel_hits: List[str] = []
        bad_needed: List[str] = []
        align_notes: List[str] = []
        audited_count = 0

        for abi in ("arm64-v8a", "armeabi-v7a"):
            allowed_libs = (
                ANDROID_NDK_PUBLIC_LIBS
                | set(REPO_JNILIBS[abi])
                | set(PROPRIETARY_JNILIBS[abi])
                | set(OWN_LIBS)
            )
            for lib_name, lib_path in sorted(compiled[abi].items()):
                audited_count += 1
                dyn_out = subprocess.check_output(
                    [str(readelf_bin), "-d", str(lib_path)],
                    text=True,
                    errors="ignore",
                )
                if "TEXTREL" in dyn_out:
                    textrel_hits.append(f"{abi}/{lib_name}")

                for needed in re.findall(r"\(NEEDED\)\s+Shared library:\s+\[([^\]]+)\]", dyn_out):
                    if needed not in allowed_libs:
                        bad_needed.append(f"{abi}/{lib_name} -> {needed}")

                # Check PT_LOAD page alignment (informational for Android 15+ 16KB devices)
                phdr_out = subprocess.check_output(
                    [str(readelf_bin), "-lW", str(lib_path)],
                    text=True,
                    errors="ignore",
                )
                load_aligns = []
                for ln in phdr_out.splitlines():
                    parts = ln.split()
                    if parts and parts[0] == "LOAD":
                        try:
                            load_aligns.append(int(parts[-1], 16))
                        except ValueError:
                            pass
                if load_aligns and abi == "arm64-v8a" and min(load_aligns) < 0x4000:
                    align_notes.append(f"{abi}/{lib_name} (min LOAD align 0x{min(load_aligns):x})")

        if not textrel_hits:
            r.ok("Compiled .so zero TEXTREL (Android 6+ requirement)", f"{audited_count} binaries verified")
        else:
            r.fail("Compiled .so zero TEXTREL", "DT_TEXTREL present in: " + ", ".join(textrel_hits))

        if not bad_needed:
            r.ok("Compiled .so DT_NEEDED dependency closure", f"{audited_count} binaries link only NDK-public or bundled libs")
        else:
            r.fail("Compiled .so DT_NEEDED dependency closure", "unbundled/non-NDK libs: " + "; ".join(bad_needed))

        if align_notes:
            r.info("Android 15+ 16KB page-size readiness", "4KB aligned (NDK 25 default): " + ", ".join(align_notes))

        # 2) Real compiled ELF dynamic JNI symbol verification on BOTH ABIs
        for abi in ("arm64-v8a", "armeabi-v7a"):
            abi_exports: Set[str] = set()
            for lib_path in compiled[abi].values():
                abi_exports.update(_load_dynamic_exports(nm_bin, lib_path).keys())
            if not abi_exports:
                r.warn(f"Compiled JNI exports [{abi}]", "no compiled libSAMPMOBILE.so found")
                continue

            missing_jni: List[str] = []
            for fq, method, jni_sym in methods:
                exact_or_overload = any(s == jni_sym or s.startswith(jni_sym + "__") for s in abi_exports)
                dynamic_reg = (
                    "JNI_OnLoad" in abi_exports
                    and "RegisterNatives" in cpp_blob
                    and f'"{method}"' in cpp_blob
                )
                if not exact_or_overload and not dynamic_reg:
                    missing_jni.append(f"{fq} ({jni_sym})")

            if not missing_jni:
                r.ok(f"Compiled JNI exports [{abi}]", f"{len(methods)} Java native methods matched real ELF exports")
            else:
                r.fail(f"Compiled JNI exports [{abi}]", "; ".join(missing_jni))


def _check_got_relocations(r: Runner, readelf_bin: Optional[Path], require_gtasa: bool) -> None:
    """Verify Addr.h hardcoded GOT redirection offsets match real ELF GLOB_DAT relocations on both ABIs."""
    addr_h = REPO_ROOT / "app/src/main/cpp/SAMPMOBILE/Addr.h"
    if not addr_h.is_file():
        r.fail("Addr.h GOT relocation audit", "missing Addr.h")
        return
    if not readelf_bin:
        r.warn("Addr.h GOT relocation audit", "llvm-readelf not found; skipped")
        return

    text = addr_h.read_text(encoding="utf-8", errors="ignore")
    # Match: static constexpr uintptr_t <NAME> = !VER_x32 ? 0x<ARM64> : 0x<ARM32>;
    got_specs = [
        ("GOT_CModelInfo_ms_modelInfoPtrs", "_ZN10CModelInfo16ms_modelInfoPtrsE"),
        ("GOT_CStreaming_ms_aInfoForModel", "_ZN10CStreaming16ms_aInfoForModelE"),
        ("_ZN6CWorld7PlayersE_ptr", "_ZN6CWorld7PlayersE"),
    ]
    parsed_addrs: Dict[str, Tuple[int, int]] = {}
    for const_name, sym_name in got_specs:
        m = re.search(
            rf"{re.escape(const_name)}\s*=\s*!VER_x32\s*\?\s*0x([0-9A-Fa-f]+)\s*:\s*0x([0-9A-Fa-f]+)",
            text,
        )
        if not m:
            r.fail("Addr.h GOT relocation audit", f"could not parse {const_name} in Addr.h")
            return
        parsed_addrs[sym_name] = (int(m.group(1), 16), int(m.group(2), 16))

    expected_reloc_type = {"arm64-v8a": "R_AARCH64_GLOB_DAT", "armeabi-v7a": "R_ARM_GLOB_DAT"}
    for abi, so_path in SO_GTASA.items():
        if not so_path.is_file():
            if require_gtasa:
                r.fail(f"GOT relocation audit [{abi}]", f"missing {so_path.relative_to(REPO_ROOT).as_posix()}")
            else:
                r.info(f"GOT relocation audit [{abi}]", "skipped (local libGTASA.so not extracted)")
            continue

        rel_out = subprocess.check_output([str(readelf_bin), "-rW", str(so_path)], text=True, errors="ignore")
        sym_relocs: Dict[str, List[Tuple[int, str]]] = {s: [] for _, s in got_specs}
        for line in rel_out.splitlines():
            parts = line.split()
            if len(parts) < 5:
                continue
            sym = parts[4].split("@", 1)[0]
            if sym in sym_relocs:
                try:
                    off = int(parts[0], 16)
                except ValueError:
                    continue
                sym_relocs[sym].append((off, parts[2]))

        errors: List[str] = []
        want_type = expected_reloc_type[abi]
        for const_name, sym in got_specs:
            want_off = parsed_addrs[sym][0] if abi == "arm64-v8a" else parsed_addrs[sym][1]
            hits = sym_relocs[sym]
            got_hits = [(o, t) for (o, t) in hits if t == want_type]
            non_got = [(o, t) for (o, t) in hits if t != want_type]
            if not any(o == want_off for (o, _) in got_hits):
                actual = ", ".join(f"0x{o:x}({t})" for o, t in hits) or "none"
                errors.append(f"{const_name}: Addr.h=0x{want_off:x} != ELF [{actual}]")
            if non_got:
                extra = ", ".join(f"0x{o:x}({t})" for o, t in non_got)
                errors.append(f"{const_name}: unexpected non-GOT reloc(s) [{extra}]")

        if not errors:
            r.ok(f"GOT relocation audit [{abi}]", f"{len(got_specs)} relocated table GOT slots verified (zero non-GOT relocs)")
        else:
            r.fail(f"GOT relocation audit [{abi}]", "; ".join(errors))


def run_binary_abi_checks(
    r: Runner,
    require_gtasa: bool = False,
    apk_path: Optional[Path] = None,
) -> None:
    r.section("Stage 3: Binary ELF, Dual-ABI Symbols & Real JNI Exports")
    nm_bin = find_ndk_tool("llvm-nm")
    readelf_bin = find_ndk_tool("llvm-readelf")

    _check_prebuilt_jnilibs(r, require_gtasa=require_gtasa)
    _check_symbols_def(r, nm_bin=nm_bin, require_gtasa=require_gtasa)
    _check_got_relocations(r, readelf_bin=readelf_bin, require_gtasa=require_gtasa)
    _check_compiled_binaries_and_jni(r, nm_bin=nm_bin, readelf_bin=readelf_bin, apk_path=apk_path)
