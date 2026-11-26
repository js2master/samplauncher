#!/usr/bin/env python3
"""Offline dual-ABI inspector for GTA:SA v2.10 libGTASA.so (arm64-v8a & armeabi-v7a).

Eliminates ad-hoc reverse-engineering scripts and enforces pre-hook verification by providing:
  1. `find`   — regex/substring search over ~22,000 exported symbols (mangled + demangled).
  2. `disasm` — NDK llvm-objdump disassembly with automatic PLT stub, GOT load, and branch
                symbol annotation, plus return-register write hints before `ret`/`bx lr`.
  3. `xrefs`  — sub-second full-binary cross-reference scan (direct BL/B/BLX, PLT stub callers,
                ARM64 ADRP+LDR GOT loads, and vtable/pointer relocations in .data.rel.ro/.data).

Zero third-party Python dependencies (uses stdlib `struct` + NDK LLVM binutils).

Examples:
  python tools/engine/gtasa_inspect.py find ZeldaWeapon
  python tools/engine/gtasa_inspect.py xrefs _Z12Menu_NewGamev
  python tools/engine/gtasa_inspect.py xrefs _Z13DoGameRestartv --abi both
  python tools/engine/gtasa_inspect.py disasm _ZN15StartGameScreen14OnNewGameCheckEv
  python tools/engine/gtasa_inspect.py disasm 0x655334 --bytes 160 --abi arm64
"""

from __future__ import annotations

import argparse
import bisect
import glob
import os
import re
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SO_PATHS = {
    "arm64": os.path.join(REPO, "app", "src", "main", "jniLibs", "arm64-v8a", "libGTASA.so"),
    "arm32": os.path.join(REPO, "app", "src", "main", "jniLibs", "armeabi-v7a", "libGTASA.so"),
}

SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_REL = 9
SHT_DYNSYM = 11

STT_OBJECT = 1
STT_FUNC = 2


def find_llvm_tool(tool_base: str) -> str:
    for cand in (tool_base, f"{tool_base}.exe"):
        found = shutil.which(cand)
        if found:
            return found
    for env_key in ("LOCALAPPDATA", "ANDROID_SDK_ROOT", "ANDROID_HOME"):
        root = os.environ.get(env_key, "")
        if not root:
            continue
        sdk = os.path.join(root, "Android", "Sdk") if env_key == "LOCALAPPDATA" else root
        pattern = os.path.join(sdk, "ndk", "*", "toolchains", "llvm", "prebuilt", "*", "bin", f"{tool_base}*")
        matches = sorted(glob.glob(pattern))
        if matches:
            return matches[-1]
    raise RuntimeError(f"Could not locate NDK tool '{tool_base}' in PATH or Android SDK.")


def demangle_batch(names: list[str]) -> dict[str, str]:
    if not names:
        return {}
    try:
        cxxfilt = find_llvm_tool("llvm-cxxfilt")
    except RuntimeError:
        return {n: n for n in names}
    unique = list(dict.fromkeys(names))
    proc = subprocess.run(
        [cxxfilt],
        input="\n".join(unique) + "\n",
        text=True,
        capture_output=True,
        encoding="utf-8",
        errors="replace",
    )
    out_lines = proc.stdout.splitlines()
    mapping: dict[str, str] = {}
    for src, dst in zip(unique, out_lines):
        mapping[src] = dst.strip()
    for n in unique:
        mapping.setdefault(n, n)
    return mapping


@dataclass
class Symbol:
    index: int
    name: str
    value: int  # Thumb bit stripped for arm32 functions
    raw_value: int
    size: int
    stt: int

    @property
    def kind(self) -> str:
        if self.stt == STT_FUNC:
            return "FUNC"
        if self.stt == STT_OBJECT:
            return "DATA"
        return f"T{self.stt}"


class ElfBinary:
    def __init__(self, abi: str, path: str) -> None:
        self.abi = abi
        self.path = path
        with open(path, "rb") as fh:
            self.buf = fh.read()
        if self.buf[:4] != b"\x7fELF":
            raise ValueError(f"Not an ELF binary: {path}")
        self.is_64 = self.buf[4] == 2
        self.sections: dict[str, tuple[int, int, int, int, int, int]] = {}
        self.sec_list: list[tuple[str, int, int, int, int, int, int]] = []
        self._parse_sections()
        self.symbols: list[Symbol] = []
        self.sym_by_name: dict[str, Symbol] = {}
        self.sym_by_addr: dict[int, Symbol] = {}
        self.func_addrs: list[int] = []
        self.func_syms: list[Symbol] = []
        self._parse_dynsym()
        self.got_to_sym: dict[int, str] = {}
        self.ptr_relocs: dict[str, list[int]] = {}
        self._parse_relocs()
        self.plt_to_sym: dict[int, str] = {}
        self.sym_to_plt: dict[str, list[int]] = {}
        self._parse_plt()

    def _parse_sections(self) -> None:
        if self.is_64:
            e_shoff = struct.unpack_from("<Q", self.buf, 0x28)[0]
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", self.buf, 0x3A)
            raw = []
            for i in range(e_shnum):
                off = e_shoff + i * e_shentsize
                sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size, sh_link, _info, _align, sh_entsize = (
                    struct.unpack_from("<IIQQQQIIQQ", self.buf, off)
                )
                raw.append((sh_name, sh_type, sh_addr, sh_offset, sh_size, sh_link, sh_entsize))
        else:
            e_shoff = struct.unpack_from("<I", self.buf, 0x20)[0]
            e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", self.buf, 0x2E)
            raw = []
            for i in range(e_shnum):
                off = e_shoff + i * e_shentsize
                sh_name, sh_type, _flags, sh_addr, sh_offset, sh_size, sh_link, _info, _align, sh_entsize = (
                    struct.unpack_from("<IIIIIIIIII", self.buf, off)
                )
                raw.append((sh_name, sh_type, sh_addr, sh_offset, sh_size, sh_link, sh_entsize))

        shstr_off = raw[e_shstrndx][3]
        for sh_name, sh_type, sh_addr, sh_offset, sh_size, sh_link, sh_entsize in raw:
            end = self.buf.find(b"\x00", shstr_off + sh_name)
            name = self.buf[shstr_off + sh_name : end].decode("ascii", errors="replace")
            entry = (sh_type, sh_addr, sh_offset, sh_size, sh_link, sh_entsize)
            self.sections[name] = entry
            self.sec_list.append((name, *entry))

    def _read_cstr(self, strtab_off: int, idx: int) -> str:
        end = self.buf.find(b"\x00", strtab_off + idx)
        return self.buf[strtab_off + idx : end].decode("utf-8", errors="replace")

    def _parse_dynsym(self) -> None:
        if ".dynsym" not in self.sections:
            return
        _t, _addr, sym_off, sym_sz, link_idx, entsize = self.sections[".dynsym"]
        str_off = self.sec_list[link_idx][3]
        step = entsize or (24 if self.is_64 else 16)
        count = sym_sz // step
        funcs: list[Symbol] = []
        for i in range(count):
            pos = sym_off + i * step
            if self.is_64:
                st_name, st_info, _other, st_shndx, st_value, st_size = struct.unpack_from("<IBBHQQ", self.buf, pos)
            else:
                st_name, st_value, st_size, st_info, _other, st_shndx = struct.unpack_from("<IIIBBH", self.buf, pos)
            name = self._read_cstr(str_off, st_name) if st_name else ""
            stt = st_info & 0xF
            clean_val = (st_value & ~1) if (not self.is_64 and stt == STT_FUNC) else st_value
            sym = Symbol(index=i, name=name, value=clean_val, raw_value=st_value, size=st_size, stt=stt)
            self.symbols.append(sym)
            if name and st_shndx != 0 and clean_val != 0:
                self.sym_by_name[name] = sym
                if clean_val not in self.sym_by_addr or stt == STT_FUNC:
                    self.sym_by_addr[clean_val] = sym
                if stt == STT_FUNC:
                    funcs.append(sym)
        funcs.sort(key=lambda s: s.value)
        self.func_syms = funcs
        self.func_addrs = [s.value for s in funcs]

    def _parse_relocs(self) -> None:
        for sec_name, sh_type, _addr, sh_off, sh_sz, _link, entsize in self.sec_list:
            if sh_type not in (SHT_RELA, SHT_REL):
                continue
            is_rela = sh_type == SHT_RELA
            if self.is_64:
                step = entsize or (24 if is_rela else 16)
            else:
                step = entsize or (12 if is_rela else 8)
            for pos in range(sh_off, sh_off + sh_sz, step):
                if self.is_64:
                    if is_rela:
                        r_offset, r_info, r_addend = struct.unpack_from("<QQq", self.buf, pos)
                    else:
                        r_offset, r_info = struct.unpack_from("<QQ", self.buf, pos)
                        r_addend = 0
                    sym_idx = r_info >> 32
                    r_type = r_info & 0xFFFFFFFF
                else:
                    if is_rela:
                        r_offset, r_info, r_addend = struct.unpack_from("<IIi", self.buf, pos)
                    else:
                        r_offset, r_info = struct.unpack_from("<II", self.buf, pos)
                        r_addend = 0
                    sym_idx = r_info >> 8
                    r_type = r_info & 0xFF

                if 0 < sym_idx < len(self.symbols):
                    sname = self.symbols[sym_idx].name
                    if not sname:
                        continue
                    # JUMP_SLOT (1026 arm64 / 22 arm32) or GLOB_DAT (1025 arm64 / 21 arm32)
                    if (self.is_64 and r_type in (1025, 1026)) or (not self.is_64 and r_type in (21, 22)):
                        self.got_to_sym[r_offset] = sname
                    # ABS64 (257) / ABS32 (2) pointer tables (vtables, function tables)
                    elif (self.is_64 and r_type == 257 and r_addend == 0) or (not self.is_64 and r_type == 2):
                        self.ptr_relocs.setdefault(sname, []).append(r_offset)
                elif (self.is_64 and r_type == 1027) or (not self.is_64 and r_type == 23):
                    # R_AARCH64_RELATIVE / R_ARM_RELATIVE pointing straight to an exported function
                    target = r_addend if (self.is_64 and is_rela) else 0
                    if not self.is_64 and not is_rela:
                        voff = self.vaddr_to_offset(r_offset)
                        if voff is not None:
                            target = struct.unpack_from("<I", self.buf, voff)[0] & ~1
                    if target in self.sym_by_addr:
                        sname = self.sym_by_addr[target].name
                        self.ptr_relocs.setdefault(sname, []).append(r_offset)

    def _parse_plt(self) -> None:
        if ".plt" not in self.sections:
            return
        _t, plt_addr, plt_off, plt_sz, _link, _ent = self.sections[".plt"]
        if self.is_64:
            # Standard AArch64 16-byte PLT stub:
            #   +0: adrp x16, page
            #   +4: ldr  x17, [x16, #off]
            #   +8: add  x16, x16, #off
            #  +12: br   x17
            for off in range(0, plt_sz - 12, 4):
                w0, w1, _w2, w3 = struct.unpack_from("<IIII", self.buf, plt_off + off)
                if w3 == 0xD61F0220 and (w0 & 0x9F00001F) == 0x90000010 and (w1 & 0xFFC003FF) == 0xF9400211:
                    pc = plt_addr + off
                    immlo = (w0 >> 29) & 0x3
                    immhi = (w0 >> 5) & 0x7FFFF
                    imm = (immhi << 2) | immlo
                    if imm & (1 << 20):
                        imm -= 1 << 21
                    page = (pc & ~0xFFF) + (imm << 12)
                    ldr_off = ((w1 >> 10) & 0xFFF) * 8
                    got_addr = page + ldr_off
                    sname = self.got_to_sym.get(got_addr)
                    if sname:
                        self.plt_to_sym[pc] = sname
                        self.sym_to_plt.setdefault(sname, []).append(pc)
        else:
            # Standard ARM32 12-byte PLT stub:
            #   +0: add ip, pc, #hi   (e28fc6XX / e28fc2XX)
            #   +4: add ip, ip, #mid  (e28ccaXX)
            #   +8: ldr pc, [ip, #lo]! (e5bcfXXX)
            for off in range(0, plt_sz - 8, 4):
                w0, w1, w2 = struct.unpack_from("<III", self.buf, plt_off + off)
                if (w0 & 0xFFFF0000) in (0xE28FC600 >> 0 & 0xFFFF0000, 0xE28FC000) and (
                    (w0 & 0xFFF0F000) == 0xE280C000
                    and (w1 & 0xFFF0F000) == 0xE280C000
                    and (w2 & 0xFFFFF000) == 0xE5BCF000
                ):
                    def dec_rot_imm(insn: int) -> int:
                        imm8 = insn & 0xFF
                        rot = ((insn >> 8) & 0xF) * 2
                        return ((imm8 >> rot) | (imm8 << (32 - rot))) & 0xFFFFFFFF if rot else imm8

                    pc = plt_addr + off
                    ip = (pc + 8 + dec_rot_imm(w0) + dec_rot_imm(w1)) & 0xFFFFFFFF
                    got_addr = (ip + (w2 & 0xFFF)) & 0xFFFFFFFF
                    sname = self.got_to_sym.get(got_addr)
                    if sname:
                        self.plt_to_sym[pc] = sname
                        self.sym_to_plt.setdefault(sname, []).append(pc)

    def vaddr_to_offset(self, vaddr: int) -> int | None:
        for _name, _t, sh_addr, sh_off, sh_sz, _l, _e in self.sec_list:
            if sh_addr and sh_addr <= vaddr < sh_addr + sh_sz:
                return sh_off + (vaddr - sh_addr)
        return None

    def resolve_containing_func(self, vaddr: int) -> tuple[str, int]:
        idx = bisect.bisect_right(self.func_addrs, vaddr) - 1
        if idx >= 0:
            fn = self.func_syms[idx]
            diff = vaddr - fn.value
            if fn.size == 0 or diff < max(fn.size, 0x4000):
                return fn.name, diff
        for name, _t, sh_addr, _off, sh_sz, _l, _e in self.sec_list:
            if sh_addr and sh_addr <= vaddr < sh_addr + sh_sz:
                return name, vaddr - sh_addr
        return "<unknown>", vaddr

    def resolve_target(self, query: str) -> Symbol | None:
        if query in self.sym_by_name:
            return self.sym_by_name[query]
        if query.startswith("0x") or re.fullmatch(r"[0-9a-fA-F]+", query):
            addr = int(query, 16)
            clean = addr & ~1 if not self.is_64 else addr
            if clean in self.sym_by_addr:
                return self.sym_by_addr[clean]
            return Symbol(index=-1, name=f"sub_{clean:x}", value=clean, raw_value=addr, size=128, stt=STT_FUNC)
        matches = [s for s in self.symbols if s.name and query in s.name and s.value != 0]
        if len(matches) == 1:
            return matches[0]
        return None

    def find_xrefs(self, target: Symbol) -> list[tuple[str, int, str, int]]:
        """Returns list of (kind, site_vaddr, caller_sym_name, offset_in_caller)."""
        hits: list[tuple[str, int, str, int]] = []
        target_addrs = {target.value}
        plt_addrs = set(self.sym_to_plt.get(target.name, []))
        all_call_targets = target_addrs | plt_addrs
        got_slots = {g for g, n in self.got_to_sym.items() if n == target.name}

        if ".text" in self.sections:
            _t, text_addr, text_off, text_sz, _l, _e = self.sections[".text"]
            mv = memoryview(self.buf)[text_off : text_off + text_sz]
            if self.is_64:
                # Scan AArch64 4-byte instructions for BL / B / B.cond / ADRP+LDR
                u32 = mv.cast("I")
                n = len(u32)
                last_adrp_reg = -1
                last_adrp_page = 0
                last_adrp_pc = -1
                for i in range(n):
                    insn = u32[i]
                    pc = text_addr + (i << 2)
                    op6 = insn >> 26
                    if op6 == 0b100101 or op6 == 0b000101:  # BL or B
                        imm26 = insn & 0x3FFFFFF
                        if imm26 & (1 << 25):
                            imm26 -= 1 << 26
                        dest = pc + (imm26 << 2)
                        if dest in all_call_targets:
                            kind = ("PLT_" if dest in plt_addrs else "") + ("BL" if op6 == 0b100101 else "B")
                            fn_name, fn_off = self.resolve_containing_func(pc)
                            hits.append((kind, pc, fn_name, fn_off))
                    elif (insn & 0xFF000010) == 0x54000000:  # B.cond
                        imm19 = (insn >> 5) & 0x7FFFF
                        if imm19 & (1 << 18):
                            imm19 -= 1 << 19
                        dest = pc + (imm19 << 2)
                        if dest in all_call_targets:
                            fn_name, fn_off = self.resolve_containing_func(pc)
                            hits.append(("B.cond", pc, fn_name, fn_off))
                    elif got_slots and (insn & 0x9F000000) == 0x90000000:  # ADRP
                        rd = insn & 0x1F
                        immlo = (insn >> 29) & 0x3
                        immhi = (insn >> 5) & 0x7FFFF
                        imm = (immhi << 2) | immlo
                        if imm & (1 << 20):
                            imm -= 1 << 21
                        last_adrp_reg = rd
                        last_adrp_page = (pc & ~0xFFF) + (imm << 12)
                        last_adrp_pc = pc
                    elif got_slots and (insn & 0xFFC00000) == 0xF9400000 and pc - last_adrp_pc <= 16:
                        rn = (insn >> 5) & 0x1F
                        if rn == last_adrp_reg:
                            ldr_off = ((insn >> 10) & 0xFFF) * 8
                            if (last_adrp_page + ldr_off) in got_slots:
                                fn_name, fn_off = self.resolve_containing_func(pc)
                                hits.append(("GOT_LDR", pc, fn_name, fn_off))
            else:
                # Scan Thumb-2 32-bit BL / BLX / B.W (2-byte aligned)
                u16 = mv.cast("H")
                n = len(u16) - 1
                i = 0
                while i < n:
                    hw1 = u16[i]
                    if (hw1 & 0xF800) == 0xF000:
                        hw2 = u16[i + 1]
                        if (hw2 & 0x8000) == 0x8000:
                            is_bl = (hw2 & 0xD000) == 0xD000
                            is_blx = (hw2 & 0xD001) == 0xC000
                            is_bw = (hw2 & 0xD000) == 0x9000
                            if is_bl or is_blx or is_bw:
                                s = (hw1 >> 10) & 1
                                imm10 = hw1 & 0x3FF
                                j1 = (hw2 >> 13) & 1
                                j2 = (hw2 >> 11) & 1
                                imm11 = hw2 & 0x7FF
                                i1 = ~(j1 ^ s) & 1
                                i2 = ~(j2 ^ s) & 1
                                offset = (s << 24) | (i1 << 23) | (i2 << 22) | (imm10 << 12) | (imm11 << 1)
                                if s:
                                    offset -= 1 << 25
                                pc = text_addr + (i << 1)
                                base_pc = ((pc + 4) & ~3) if is_blx else (pc + 4)
                                dest = (base_pc + offset) & ~1
                                if dest in all_call_targets:
                                    op_str = "BLX" if is_blx else ("BL" if is_bl else "B.W")
                                    kind = ("PLT_" if dest in plt_addrs else "") + op_str
                                    fn_name, fn_off = self.resolve_containing_func(pc)
                                    hits.append((kind, pc, fn_name, fn_off))
                            i += 2
                            continue
                    i += 1

        for ptr_site in self.ptr_relocs.get(target.name, []):
            sec_name, sec_off = self.resolve_containing_func(ptr_site)
            hits.append(("PTR_DATA", ptr_site, sec_name, sec_off))

        return hits


def cmd_find(args: argparse.Namespace) -> int:
    abis = ["arm64", "arm32"] if args.abi == "both" else [args.abi]
    pat = re.compile(args.pattern, re.IGNORECASE)
    for abi in abis:
        elf = ElfBinary(abi, SO_PATHS[abi])
        names = [s.name for s in elf.symbols if s.name and s.value != 0]
        demangled = demangle_batch(names)
        matches = []
        for s in elf.symbols:
            if not s.name or s.value == 0:
                continue
            dem = demangled.get(s.name, s.name)
            if pat.search(s.name) or pat.search(dem):
                matches.append((s, dem))
        matches.sort(key=lambda x: x[0].value)
        print(f"=== [{abi}] {len(matches)} match(es) for /{args.pattern}/ ===")
        for s, dem in matches[: args.limit]:
            print(f"  0x{s.value:08x}  sz={s.size:<5d}  {s.kind:<4s}  {dem}  [{s.name}]")
        if len(matches) > args.limit:
            print(f"  ... ({len(matches) - args.limit} more; pass --limit N)")
    return 0


def cmd_xrefs(args: argparse.Namespace) -> int:
    abis = ["arm64", "arm32"] if args.abi == "both" else [args.abi]
    for abi in abis:
        elf = ElfBinary(abi, SO_PATHS[abi])
        target = elf.resolve_target(args.target)
        if not target:
            print(f"[{abi}] Target '{args.target}' not found (use `find` to search).", file=sys.stderr)
            continue
        hits = elf.find_xrefs(target)
        to_dem = [target.name] + [h[2] for h in hits if h[2].startswith("_Z")]
        dem = demangle_batch(to_dem)
        t_label = dem.get(target.name, target.name)
        plt_str = ", ".join(f"0x{p:x}" for p in elf.sym_to_plt.get(target.name, [])) or "none"
        print(
            f"=== [{abi}] XREFs to {t_label} [{target.name}] @ 0x{target.value:x} "
            f"(PLT: {plt_str}) -> {len(hits)} hit(s) ==="
        )
        for kind, pc, fn_name, fn_off in hits:
            fn_dem = dem.get(fn_name, fn_name)
            print(f"  0x{pc:08x}  {kind:<9s}  {fn_dem} + 0x{fn_off:x}  [{fn_name}]")
    return 0


def cmd_disasm(args: argparse.Namespace) -> int:
    elf = ElfBinary(args.abi, SO_PATHS[args.abi])
    target = elf.resolve_target(args.target)
    if not target:
        print(f"[{args.abi}] Target '{args.target}' not found (use `find` to search).", file=sys.stderr)
        return 1
    span = args.bytes if args.bytes else (target.size if target.size > 0 else 160)
    start = target.value
    stop = start + span
    objdump = find_llvm_tool("llvm-objdump")
    cmd = [
        objdump,
        "-d",
        f"--start-address=0x{start:x}",
        f"--stop-address=0x{stop:x}",
    ]
    if not elf.is_64:
        cmd.append("--triple=thumbv7a-linux-androideabi")
    cmd.append(elf.path)
    out = subprocess.check_output(cmd, text=True, encoding="utf-8", errors="replace")

    # Pre-demangle PLT, GOT, and nearby branch symbols referenced in disassembly
    referenced_syms: set[str] = {target.name}
    hex_target_re = re.compile(r"\b(?:blx?|b(?:\.[a-z]+)?|cbn?z|tbn?z)\b[^\n]*?\b0x([0-9a-fA-F]+)\b")
    adrp_re = re.compile(r"^\s*([0-9a-fA-F]+):.*\badrp\s+x(\d+),\s*0x([0-9a-fA-F]+)")
    ldr_imm_re = re.compile(r"\bldr\s+([xw]\d+),\s*\[x(\d+)(?:,\s*#(0x[0-9a-fA-F]+|\d+))?\]")

    raw_lines = out.splitlines()
    adrp_pages: dict[int, int] = {}
    line_notes: dict[int, str] = {}
    ret_hints: list[str] = []
    recent_r0_writes: list[str] = []

    for idx, line in enumerate(raw_lines):
        m_adrp = adrp_re.match(line)
        if m_adrp:
            adrp_pages[int(m_adrp.group(2))] = int(m_adrp.group(3), 16)
        m_ldr = ldr_imm_re.search(line)
        if m_ldr:
            rd_str = m_ldr.group(1)
            rd_num = int(rd_str[1:])
            rn = int(m_ldr.group(2))
            off = int(m_ldr.group(3) or "0", 0)
            if rn in adrp_pages:
                got_addr = adrp_pages[rn] + off
                if got_addr in elf.got_to_sym:
                    sname = elf.got_to_sym[got_addr]
                    referenced_syms.add(sname)
                    line_notes[idx] = f"GOT:{sname}"
            adrp_pages.pop(rd_num, None)

        m_br = hex_target_re.search(line)
        if m_br:
            dest = int(m_br.group(1), 16)
            clean_dest = dest & ~1 if not elf.is_64 else dest
            if clean_dest in elf.plt_to_sym:
                sname = elf.plt_to_sym[clean_dest]
                referenced_syms.add(sname)
                line_notes[idx] = f"PLT:{sname}"
            elif clean_dest in elf.sym_by_addr and clean_dest != start:
                sname = elf.sym_by_addr[clean_dest].name
                referenced_syms.add(sname)
                line_notes[idx] = f"SYM:{sname}"

        # Track writes to return registers (w0/x0/r0/s0) right before ret / bx lr / pop {.., pc}
        stripped = line.strip()
        if re.search(r"\b(?:mov|orr|eor|add|sub|ldr\w*|cset|csel|sxth|uxth|sxtb|uxtb|fmov)\s+(?:[wxr]0|s0)\b", stripped):
            parts = stripped.split("\t")
            recent_r0_writes.append(parts[-1] if parts else stripped)
            if len(recent_r0_writes) > 3:
                recent_r0_writes.pop(0)
        if re.search(r"\b(?:ret|bx\s+lr)\b|\bpop(?:\.w)?\s*\{[^}]*\bpc\b", stripped):
            addr_m = re.match(r"^\s*([0-9a-fA-F]+):", line)
            at_str = f"0x{addr_m.group(1)}" if addr_m else "ret"
            last_w = recent_r0_writes[-1] if recent_r0_writes else "<none seen right before ret>"
            ret_hints.append(f"{at_str}: last r0/w0/x0/s0 write = `{last_w}`")

    dem = demangle_batch(list(referenced_syms))
    print(f"=== [{args.abi}] DISASM {dem.get(target.name, target.name)} [{target.name}] @ 0x{start:x} (+{span}B) ===")
    for idx, line in enumerate(raw_lines):
        if idx < 5 and ("file format" in line or not line.strip() or line.startswith("Disassembly")):
            continue
        note = line_notes.get(idx)
        if note:
            prefix, sname = note.split(":", 1)
            print(f"{line}    ; [{prefix}] {dem.get(sname, sname)}")
        else:
            print(line)
    if ret_hints:
        print("--- Return-register write summary (verify C++ hook return type!) ---")
        for h in ret_hints[:6]:
            print(f"  {h}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Dual-ABI libGTASA.so symbol, disasm & xref inspector")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p_find = sub.add_parser("find", help="Search exported symbols by regex/substring")
    p_find.add_argument("pattern", help="Regex or substring (matches mangled and demangled names)")
    p_find.add_argument("--abi", choices=("arm64", "arm32", "both"), default="arm64")
    p_find.add_argument("--limit", type=int, default=50)
    p_find.set_defaults(func=cmd_find)

    p_xrefs = sub.add_parser("xrefs", help="Find all callers and pointer references across libGTASA.so")
    p_xrefs.add_argument("target", help="Mangled symbol name, unique substring, or hex address")
    p_xrefs.add_argument("--abi", choices=("arm64", "arm32", "both"), default="both")
    p_xrefs.set_defaults(func=cmd_xrefs)

    p_dis = sub.add_parser("disasm", help="Disassemble symbol/address with automatic PLT & GOT annotations")
    p_dis.add_argument("target", help="Mangled symbol name, unique substring, or hex address")
    p_dis.add_argument("--abi", choices=("arm64", "arm32"), default="arm64")
    p_dis.add_argument("--bytes", type=int, default=0, help="Override byte count to disassemble")
    p_dis.set_defaults(func=cmd_disasm)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
