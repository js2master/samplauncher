#!/usr/bin/env python3
"""
Reproducible, clean GTA:SA Mobile + SA-MP game data builder.

Generates a clean, compact `files/` directory purely from universally obtainable
sources — no third-party mobile data packs required:

  1. Official GTA:SA Android v2.10 (`--gtasa`): accepts `.obb` file(s)
     (main/patch OBB), an `.apk` containing `assets/`, or an unpacked GTA:SA
     `files/` directory. Extracts `anim/`, `data/`, `models/`, minimal
     `audio/CONFIG/` (or full `audio/` with `--keep-audio`), `texdb/*.img`, and
     single-format `texdb/<db>/` databases.
  2. Official PC GTA:SA + SA-MP 0.3.7 (`--pc-samp`, requires `numpy`):
     - Copies `SAMP/SAMP.ide` and `SAMP/SAMPCOL.img`.
     - Splits PC `SAMP/SAMP.img` (VER2): repacks models (`.dff`/`.ifp`) into
       mobile `texdb/samp.img`, and compiles all embedded `.txd` archives plus
       `SAMP/blanktex.txd`, `SAMP/samaps.txd`, and 5 procedurally drawn UI icons
       (`gtexture`, `voiceactive`, `voicepassive`, `voice_speaker`, `afk_icon`)
       into native mobile `texdb/samp/` with full mipmap chains and case-variant
       `affiliate` aliases.
     - Compiles PC `models/txd/*.txd`, `models/fronten2.txd`, and
       `models/generic/vehicle.txd` into `texdb/samp_extra/` for TextDraw sprites.
     - Generates `SAMP/gta.dat`, `SAMP/peds.ide`, `SAMP/TIMECYC.DAT`,
       `SAMP/vehicles.ide`, `SAMP/tracks2.dat`, and `SAMP/tracks4.dat` directly
       from official GTA:SA `data/`.
  3. Clean-room blank `SAMP/main.scm` + empty `SAMP/script.img` (generated
     directly by this script, or overridden via `--scm-dir`).
  4. Open-source fonts (`SAMP/fonts/`): uses bundled Roboto (Apache-2.0) for
     `arial_bold.ttf` and optional `--cjk-font` (e.g. Noto Sans SC) for
     `chinese.ttf`.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent.parent
DEFAULT_ROBO_FONT = REPO_ROOT / "app/src/main/res/font/roboto_medium.ttf"

# Single-format selection per official texdb database:
# - ETC1/RGB565/RGBA4444 databases -> 'etc' (mandatory in OpenGL ES 3.0)
# - Uncompressed player/playerhi databases -> 'pvr' (only format shipped by Rockstar)
OFFICIAL_TEXDB_FORMATS = {
    "gta3": "etc",
    "gta_int": "etc",
    "cutscene": "etc",
    "txd": "etc",
    "mobile": "etc",
    "menu": "etc",
    "player": "pvr",
    "playerhi": "pvr",
}

OFFICIAL_TEXDB_IMGS = {
    "gta3.img",
    "gta_int.img",
    "player.img",
    "cutscene.img",
}

# Minimal audio/CONFIG files needed so CAudioEngine initializes cleanly without .osw audio
# (trkhead.dat is required so CAudioEngine::Initialise does not abort before loading eventvol.dat)
MINIMAL_AUDIO_CONFIG = {
    "banklkup.dat",
    "bankslot.dat",
    "eventvol.dat",
    "pakfiles.dat",
    "soundinf.dat",
    "strmpaks.dat",
    "traklkup.dat",
    "trkhead.dat",
}

# Extra PC textures outside models/txd that SA-MP TextDraws reference
EXTRA_PC_TXD_SOURCES = [
    (os.path.join("models", "fronten2.txd"), {
        "back2", "back3", "back4", "back5", "back6", "back7", "back8",
        "back8_right", "back8_top",
    }),
    (os.path.join("models", "generic", "vehicle.txd"), {
        "tyre64a", "wheel_alloy64", "wheel_classic64", "wheel_lighttruck64",
        "wheel_lightvan64", "wheel_offroad64", "wheel_saloon64",
        "wheel_smallcar64", "wheel_sport64", "wheel_truck64", "whee_rim64",
    }),
]

# Standard SA-MP story/special ped skin definitions added to stock peds.ide
SAMP_EXTRA_PED_LINES = {
    1: "1, TRUTH, TRUTH, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_TRUTH, VOICE_GNG_TRUTH",
    2: "2, MACCER, MACCER, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_MACCER, VOICE_GNG_MACCER",
    3: "3, ANDRE, ANDRE, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    4: "4, BBTHIN, BBTHIN, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GANG, VOICE_GNG_BIG_BEAR, VOICE_GNG_BIG_BEAR",
    5: "5, BB, BB, CIVMALE, STAT_SENSIBLE_GUY, fatman, 0, 0, fatman, 1,4, PED_TYPE_GANG, VOICE_GNG_BIG_BEAR, VOICE_GNG_BIG_BEAR",
    6: "6, EMMET, EMMET, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    8: "8, JANITOR, JANITOR, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    42: "42, JETHRO, JETHRO, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    65: "65, KENDL, KENDL, CIVFEMALE, STAT_SENSIBLE_GUY, woman, 0, 0, woman, 1,4, PED_TYPE_GEN, VOICE_GEN_HFYRI, VOICE_GEN_HFYRI",
    86: "86, RYDER3, RYDER3, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GANG, VOICE_GNG_RYDER, VOICE_GNG_RYDER",
    119: "119, SINDACO, SINDACO, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    149: "149, SMOKEV, SMOKEV, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GANG, VOICE_GNG_SMOKE, VOICE_GNG_SMOKE",
    208: "208, SUZIE, SUZIE, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GANG, VOICE_GNG_STRI1, VOICE_GNG_STRI1",
    265: "265, TENPEN, TENPEN, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    266: "266, PULASKI, PULASKI, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GEN_NOVOICE, VOICE_EMG_PULASKI",
    267: "267, HERN, HERN, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    268: "268, DWAYNE, DWAYNE, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    269: "269, SMOKE, SMOKE, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_SMOKE, VOICE_GNG_SMOKE",
    270: "270, SWEET, SWEET, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_SWEET, VOICE_GNG_SWEET",
    271: "271, RYDER2, RYDER2, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_RYDER, VOICE_GNG_RYDER",
    272: "272, FORELLI, FORELLI, CIVMALE, STAT_STD_MISSION, man, 1FFF, 0, null, 9,9, PED_TYPE_SPC, VOICE_GNG_MAFBOSS, VOICE_GNG_MAFBOSS",
    273: "273, TBONE, TBONE, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GANG, VOICE_GNG_TBONE, VOICE_GNG_TBONE",
    289: "289, ZERO, ZERO, CIVMALE, STAT_SENSIBLE_GUY, man, 0, 0, man, 1,4, PED_TYPE_GEN, VOICE_GEN_MALE01, VOICE_GEN_MALE01",
    290: "290, ROSE, ROSE, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    291: "291, PAUL, PAUL, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    292: "292, CESAR, CESAR, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GNG_CESAR, VOICE_GNG_CESAR",
    293: "293, OGLOC, OGLOC, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GNG_OGLOC, VOICE_GNG_OGLOC",
    294: "294, WUZIMU, WUZIMU, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GNG_WOOZIE, VOICE_GNG_WOOZIE",
    295: "295, TORINO, TORINO, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GNG_TORENO, VOICE_GNG_TORENO",
    296: "296, JIZZY, JIZZY, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GNG_JIZZY, VOICE_GNG_JIZZY",
    297: "297, MADDOGG, MADDOGG, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
    298: "298, CAT, CAT, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GFD_CATALINA, VOICE_GFD_CATALINA",
    299: "299, CLAUDE, CLAUDE, CIVMALE, STAT_STREET_GUY, man, 1FFF, 0, null, 1,4, PED_TYPE_GEN, VOICE_GEN_NOVOICE, VOICE_GEN_NOVOICE",
}


# ══════════ RenderWare TXD Decoder & Mobile TexDB Encoder ══════════

def read_u32(d, o):
    return struct.unpack_from("<I", d, o)[0]


def read_u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def parse_txd_bytes(d: bytes):
    o = 0
    if len(d) < 24 or read_u32(d, o) != 0x16:
        return []
    o += 12
    if read_u32(d, o) != 0x01:
        return []
    ssz = read_u32(d, o + 4)
    o += 12
    num = read_u16(d, o)
    o += ssz
    textures = []
    for _ in range(num):
        if o + 12 > len(d) or read_u32(d, o) != 0x15:
            break
        chunk_end = o + 12 + read_u32(d, o + 4)
        body = o + 12
        if read_u32(d, body) != 0x01:
            o = chunk_end
            continue
        p = body + 12
        if p + 88 > len(d):
            o = chunk_end
            continue
        platform = read_u32(d, p)
        p += 8  # platform + filter/addressing
        name = d[p:p + 32].split(b"\x00", 1)[0].decode("latin1", errors="ignore")
        p += 32
        mask = d[p:p + 32].split(b"\x00", 1)[0].decode("latin1", errors="ignore")
        p += 32
        raster_fmt = read_u32(d, p)
        p += 4
        d3dfmt = read_u32(d, p)
        p += 4
        w, h = read_u16(d, p), read_u16(d, p + 2)
        p += 4
        depth, levels, _rtype, flags = d[p], d[p + 1], d[p + 2], d[p + 3]
        p += 4

        palette = None
        if raster_fmt & 0x2000:  # PAL8
            if p + 1024 <= len(d):
                palette = d[p:p + 1024]
                p += 1024
        elif raster_fmt & 0x4000:  # PAL4
            if p + 64 <= len(d):
                palette = d[p:p + 64]
                p += 64

        mips = []
        for _ in range(max(1, levels)):
            if p + 4 > len(d):
                break
            sz = read_u32(d, p)
            p += 4
            if p + sz > len(d):
                sz = max(0, len(d) - p)
            mips.append(d[p:p + sz])
            p += sz
            break  # Only mip0 is needed

        if mips and w > 0 and h > 0:
            textures.append({
                "name": name,
                "mask": mask,
                "platform": platform,
                "w": w,
                "h": h,
                "d3dfmt": d3dfmt,
                "depth": depth,
                "flags": flags,
                "raster_fmt": raster_fmt,
                "palette": palette,
                "mip0": mips[0],
            })
        o = chunk_end
    return textures


def parse_txd(path: str):
    with open(path, "rb") as f:
        return parse_txd_bytes(f.read())


def dxt1_decode(buf, w, h):
    out = bytearray(w * h * 4)
    bw = (w + 3) // 4
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = (by // 4) * bw * 8 + (bx // 4) * 8
            if b + 8 > len(buf):
                continue
            c0, c1 = struct.unpack_from("<HH", buf, b)
            bits = read_u32(buf, b + 4)
            cols = []
            for c in (c0, c1):
                r = ((c >> 11) & 31) * 255 // 31
                g = ((c >> 5) & 63) * 255 // 63
                bl = (c & 31) * 255 // 31
                cols.append((r, g, bl, 255))
            if c0 > c1:
                cols.append(tuple((cols[0][i] * 2 + cols[1][i]) // 3 for i in range(3)) + (255,))
                cols.append(tuple((cols[0][i] + cols[1][i] * 2) // 3 for i in range(3)) + (255,))
            else:
                cols.append(tuple((cols[0][i] + cols[1][i]) // 2 for i in range(3)) + (255,))
                cols.append((0, 0, 0, 0))
            for py in range(4):
                y = by + py
                if y >= h:
                    break
                for px in range(4):
                    x = bx + px
                    if x >= w:
                        break
                    idx = (bits >> ((py * 4 + px) * 2)) & 3
                    off = (y * w + x) * 4
                    out[off:off + 4] = bytes(cols[idx])
    return out


def dxt3_decode(buf, w, h):
    out = bytearray(w * h * 4)
    bw = (w + 3) // 4
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = (by // 4) * bw * 16 + (bx // 4) * 16
            if b + 16 > len(buf):
                continue
            a0 = struct.unpack_from("<Q", buf, b)[0]
            c0, c1 = struct.unpack_from("<HH", buf, b + 8)
            bits = read_u32(buf, b + 12)
            cols = []
            for c in (c0, c1):
                r = ((c >> 11) & 31) * 255 // 31
                g = ((c >> 5) & 63) * 255 // 63
                bl = (c & 31) * 255 // 31
                cols.append((r, g, bl))
            cols.append(tuple((cols[0][i] * 2 + cols[1][i]) // 3 for i in range(3)))
            cols.append(tuple((cols[0][i] + cols[1][i] * 2) // 3 for i in range(3)))
            for py in range(4):
                y = by + py
                if y >= h:
                    break
                for px in range(4):
                    x = bx + px
                    if x >= w:
                        break
                    idx = (bits >> ((py * 4 + px) * 2)) & 3
                    a4 = (a0 >> ((py * 4 + px) * 4)) & 0xF
                    off = (y * w + x) * 4
                    r, g, bl = cols[idx]
                    out[off:off + 4] = bytes((r, g, bl, a4 * 17))
    return out


def dxt5_decode(buf, w, h):
    out = bytearray(w * h * 4)
    bw = (w + 3) // 4
    for by in range(0, h, 4):
        for bx in range(0, w, 4):
            b = (by // 4) * bw * 16 + (bx // 4) * 16
            if b + 16 > len(buf):
                continue
            a0, a1 = buf[b], buf[b + 1]
            abits = int.from_bytes(buf[b + 2:b + 8], "little")
            alphas = [a0, a1]
            if a0 > a1:
                for i in range(1, 7):
                    alphas.append(((7 - i) * a0 + i * a1) // 7)
            else:
                for i in range(1, 5):
                    alphas.append(((5 - i) * a0 + i * a1) // 5)
                alphas.extend([0, 255])
            c0, c1 = struct.unpack_from("<HH", buf, b + 8)
            bits = read_u32(buf, b + 12)
            cols = []
            for c in (c0, c1):
                r = ((c >> 11) & 31) * 255 // 31
                g = ((c >> 5) & 63) * 255 // 63
                bl = (c & 31) * 255 // 31
                cols.append((r, g, bl))
            cols.append(tuple((cols[0][i] * 2 + cols[1][i]) // 3 for i in range(3)))
            cols.append(tuple((cols[0][i] + cols[1][i] * 2) // 3 for i in range(3)))
            for py in range(4):
                y = by + py
                if y >= h:
                    break
                for px in range(4):
                    x = bx + px
                    if x >= w:
                        break
                    pidx = py * 4 + px
                    cidx = (bits >> (pidx * 2)) & 3
                    aidx = (abits >> (pidx * 3)) & 7
                    off = (y * w + x) * 4
                    r, g, bl = cols[cidx]
                    out[off:off + 4] = bytes((r, g, bl, alphas[aidx]))
    return out


def decode_txd_texture(tex):
    import numpy as np

    w, h = tex["w"], tex["h"]
    fmt = tex["d3dfmt"]
    buf = tex["mip0"]
    rfmt = tex["raster_fmt"]
    platform = tex.get("platform", 9)
    flags = tex["flags"]

    dxt_mode = 0
    if platform == 9:
        if fmt == 0x31545844:
            dxt_mode = 1
        elif fmt == 0x33545844:
            dxt_mode = 3
        elif fmt == 0x35545844:
            dxt_mode = 5
    else:
        if flags in (1, 3, 5):
            dxt_mode = flags

    if dxt_mode == 1:
        return bytes(dxt1_decode(buf, w, h))
    if dxt_mode == 3:
        return bytes(dxt3_decode(buf, w, h))
    if dxt_mode == 5:
        return bytes(dxt5_decode(buf, w, h))

    if (rfmt & 0x2000) and tex.get("palette") is not None and len(buf) >= w * h:
        pal = np.frombuffer(tex["palette"], dtype=np.uint8).reshape(256, 4)
        idx = np.frombuffer(buf[:w * h], dtype=np.uint8).reshape(h, w)
        return pal[idx].tobytes()

    if (rfmt & 0x4000) and tex.get("palette") is not None and len(buf) >= (w * h + 1) // 2:
        pal = np.frombuffer(tex["palette"], dtype=np.uint8).reshape(16, 4)
        raw = np.frombuffer(buf[:(w * h + 1) // 2], dtype=np.uint8)
        idx = np.empty(len(raw) * 2, dtype=np.uint8)
        idx[0::2] = raw & 0x0F
        idx[1::2] = (raw >> 4) & 0x0F
        return pal[idx[:w * h].reshape(h, w)].tobytes()

    if tex["depth"] == 32 and len(buf) >= w * h * 4:
        arr = np.frombuffer(buf[:w * h * 4], dtype=np.uint8).reshape(h, w, 4)
        rgba = np.empty_like(arr)
        rgba[:, :, 0] = arr[:, :, 2]
        rgba[:, :, 1] = arr[:, :, 1]
        rgba[:, :, 2] = arr[:, :, 0]
        rgba[:, :, 3] = arr[:, :, 3] if (rfmt & 0x0F00) != 0x0600 else 255
        return rgba.tobytes()

    if tex["depth"] == 16 and len(buf) >= w * h * 2:
        pix = np.frombuffer(buf[:w * h * 2], dtype="<u2").reshape(h, w)
        rgba = np.empty((h, w, 4), dtype=np.uint8)
        sub = rfmt & 0x0F00
        if sub == 0x0200:  # RGB565
            rgba[:, :, 0] = ((pix >> 11) & 31) * 255 // 31
            rgba[:, :, 1] = ((pix >> 5) & 63) * 255 // 63
            rgba[:, :, 2] = (pix & 31) * 255 // 31
            rgba[:, :, 3] = 255
        elif sub == 0x0300:  # RGBA4444
            rgba[:, :, 0] = ((pix >> 8) & 15) * 17
            rgba[:, :, 1] = ((pix >> 4) & 15) * 17
            rgba[:, :, 2] = (pix & 15) * 17
            rgba[:, :, 3] = ((pix >> 12) & 15) * 17
        else:  # ARGB1555
            rgba[:, :, 0] = ((pix >> 10) & 31) * 255 // 31
            rgba[:, :, 1] = ((pix >> 5) & 31) * 255 // 31
            rgba[:, :, 2] = (pix & 31) * 255 // 31
            rgba[:, :, 3] = np.where((pix >> 15) & 1, 255, 0)
        return rgba.tobytes()
    return None


def parse_dff_diffuse_textures(buf: bytes) -> set[str]:
    """Extract exact-case diffuse texture names referenced by a RenderWare DFF."""
    names: set[str] = set()

    def walk(start: int, end: int):
        pos = start
        while pos + 12 <= end:
            ctype, csz, _cver = struct.unpack_from("<III", buf, pos)
            if pos + 12 + csz > end:
                break
            body = pos + 12
            if ctype in (0x10, 0x0F, 0x0E, 0x08, 0x07, 0x1A):
                walk(body, body + csz)
            elif ctype == 0x06:  # RW_TEXTURE: struct(0x01), diffuse(0x02), mask(0x02), ext(0x03)
                sub = body
                str_idx = 0
                while sub + 12 <= body + csz:
                    st, ssz, _ = struct.unpack_from("<III", buf, sub)
                    if sub + 12 + ssz > body + csz:
                        break
                    if st == 0x02:
                        if str_idx == 0:
                            s = buf[sub + 12:sub + 12 + ssz].split(b"\x00", 1)[0].decode("latin1", errors="ignore")
                            if s:
                                names.add(s)
                        str_idx += 1
                    sub += 12 + ssz
            pos = body + csz

    walk(0, len(buf))
    return names


def _pack_rgba_16bit(arr, has_alpha: bool) -> bytes:
    import numpy as np

    r = arr[:, :, 0].astype(np.uint16)
    g = arr[:, :, 1].astype(np.uint16)
    b = arr[:, :, 2].astype(np.uint16)
    if has_alpha:
        a = arr[:, :, 3].astype(np.uint16)
        packed = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4)
    else:
        packed = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return packed.astype("<u2").tobytes()


def _downsample_rgba_half(arr):
    import numpy as np

    h, w, _ = arr.shape
    a16 = arr.astype(np.uint16)
    if h > 1 and w > 1:
        h2, w2 = h // 2, w // 2
        a16 = a16[:h2 * 2, :w2 * 2]
        out = (a16[0::2, 0::2] + a16[0::2, 1::2] + a16[1::2, 0::2] + a16[1::2, 1::2] + 2) >> 2
    elif h > 1:
        h2 = h // 2
        a16 = a16[:h2 * 2, :]
        out = (a16[0::2, :] + a16[1::2, :] + 1) >> 1
    elif w > 1:
        w2 = w // 2
        a16 = a16[:, :w2 * 2]
        out = (a16[:, 0::2] + a16[:, 1::2] + 1) >> 1
    else:
        return arr
    return out.astype(np.uint8)


def encode_texdb_entry(rgba_bytes, w, h, mipmaps: bool = False):
    """Encode an RGBA8888 image into native GTA:SA Mobile TextureDatabaseRuntime
    format (uncompressed RGB565 if opaque, RGBA4444 if alpha, optional full
    mipmap chain down to 1x1) + 1x1 thumbnail."""
    import numpy as np

    arr = np.frombuffer(rgba_bytes, dtype=np.uint8).reshape(h, w, 4)
    has_alpha = bool((arr[:, :, 3] < 250).any())
    gl_fmt = 0x8033 if has_alpha else 0x8363  # RGBA4444 vs RGB565
    alphamode = 2 if has_alpha else 0

    if mipmaps:
        levels = []
        cur = arr
        while True:
            levels.append(_pack_rgba_16bit(cur, has_alpha))
            ch, cw, _ = cur.shape
            if cw == 1 and ch == 1:
                break
            cur = _downsample_rgba_half(cur)
        payload = b"".join(levels)
        h_hdr = h | 0x8000
    else:
        payload = _pack_rgba_16bit(arr, has_alpha)
        h_hdr = h

    checksum = zlib.crc32(payload) & 0xFFFF
    dat_blob = struct.pack("<HHHHII", checksum, gl_fmt, w, h_hdr, len(payload) + 4, 0) + payload

    # 1x1 average RGBA thumbnail (matches Rockstar menu.pvr.tmb format)
    mean_rgba = bytes(int(arr[:, :, c].mean()) for c in range(4))
    tmb_blob = struct.pack("<HHHHII", checksum, 0x1401, 1, 1, 8, 0) + mean_rgba
    img_hash = zlib.crc32(payload) & 0xFFFFFFFF
    return dat_blob, tmb_blob, alphamode, img_hash


def write_texdb_database(
    db_dir: str,
    db_name: str,
    entries,
    mipmaps: bool = False,
    alias_map: dict[str, set[str]] | None = None,
) -> tuple[int, int]:
    """Write `<db>.txt`, `<db>.etc.toc`, `<db>.etc.tmb`, `<db>.etc.dat`.

    Emits case-variant `"affiliate=<target>"` entries (with `0xFFFFFFFF` in
    `.toc` and zero extra `.dat`/`.tmb` bytes) so exact-case WarDrum
    TextureDatabaseRuntime lookups match both lowercase and DFF/TXD mixed-case
    names. Returns `(dat_bytes, alias_count)`.
    """
    os.makedirs(db_dir, exist_ok=True)
    txt_lines = ["cat=0 name=Default onfoot=0 slow=0 fast=0 defaultstream=0"]
    toc_offsets = []
    dat_chunks = []
    tmb_chunks = []
    cur_off = 0
    alias_count = 0
    emitted_exact: set[str] = set()
    mip_suffix = "" if mipmaps else " mipmode=0"

    for name, w, h, rgba in entries:
        dat_blob, tmb_blob, alphamode, img_hash = encode_texdb_entry(rgba, w, h, mipmaps=mipmaps)
        toc_offsets.append(cur_off)
        dat_chunks.append(dat_blob)
        tmb_chunks.append(tmb_blob)
        cur_off += len(dat_blob)
        emitted_exact.add(name)
        txt_lines.append(
            f'"{name}" camnorm=1 alphamode={alphamode} height={h} width={w} img={img_hash:08x}{mip_suffix}'
        )

        # Collect exact-case alias spellings (always include lowercase + any DFF/TXD variants)
        candidates: set[str] = {name.lower()}
        if alias_map:
            candidates.update(alias_map.get(name.lower(), ()))
        for alias in sorted(candidates):
            if not alias or alias in emitted_exact:
                continue
            emitted_exact.add(alias)
            toc_offsets.append(0xFFFFFFFF)
            txt_lines.append(f'"{alias}" "affiliate={name}"')
            alias_count += 1

    with open(os.path.join(db_dir, f"{db_name}.txt"), "w", encoding="latin1", newline="\n") as f:
        f.write("\n".join(txt_lines) + "\n")
    with open(os.path.join(db_dir, f"{db_name}.etc.toc"), "wb") as f:
        f.write(struct.pack("<I", cur_off))
        for off in toc_offsets:
            f.write(struct.pack("<I", off))
    with open(os.path.join(db_dir, f"{db_name}.etc.tmb"), "wb") as f:
        for b in tmb_chunks:
            f.write(b)
    with open(os.path.join(db_dir, f"{db_name}.etc.dat"), "wb") as f:
        for b in dat_chunks:
            f.write(b)
    return cur_off, alias_count


# ══════════ Procedural Clean-Room UI Icons (64x64 RGBA) ══════════

def _make_canvas(w: int, h: int):
    return bytearray(w * h * 4)


def _put_px(buf: bytearray, w: int, h: int, x: int, y: int, r: int, g: int, b: int, a: int):
    if 0 <= x < w and 0 <= y < h and a > 0:
        o = (y * w + x) * 4
        buf[o] = r
        buf[o + 1] = g
        buf[o + 2] = b
        buf[o + 3] = a


def _draw_circle_ring(buf, w, h, cx, cy, r_in, r_out, col):
    for y in range(h):
        for x in range(w):
            d = math.hypot(x - cx, y - cy)
            if r_in <= d <= r_out:
                _put_px(buf, w, h, x, y, *col)


def _draw_rect(buf, w, h, x0, y0, x1, y1, col):
    for y in range(max(0, y0), min(h, y1)):
        for x in range(max(0, x0), min(w, x1)):
            _put_px(buf, w, h, x, y, *col)


def generate_ui_icons():
    """Generate 5 original 64x64 RGBA UI icons required by native ImGui widgets."""
    icons = []
    w = h = 64

    # 1. gtexture (passenger 'G' icon)
    buf = _make_canvas(w, h)
    _draw_circle_ring(buf, w, h, 32, 32, 0, 29, (24, 28, 36, 210))
    _draw_circle_ring(buf, w, h, 32, 32, 26, 29, (255, 255, 255, 235))
    # Draw crisp 'G' glyph inside
    for y in range(h):
        for x in range(w):
            d = math.hypot(x - 32, y - 32)
            ang = math.degrees(math.atan2(y - 32, x - 32))
            if 11 <= d <= 17 and not (-35 < ang < 15):
                _put_px(buf, w, h, x, y, 255, 255, 255, 255)
    _draw_rect(buf, w, h, 32, 30, 48, 36, (255, 255, 255, 255))
    _draw_rect(buf, w, h, 42, 30, 48, 44, (255, 255, 255, 255))
    icons.append(("gtexture", w, h, bytes(buf)))

    # 2 & 3. voiceactive (green mic) / voicepassive (neutral mic)
    for name, ring_col, fg_col in (
        ("voiceactive", (46, 204, 113, 240), (255, 255, 255, 255)),
        ("voicepassive", (160, 170, 185, 200), (220, 225, 235, 230)),
    ):
        buf = _make_canvas(w, h)
        _draw_circle_ring(buf, w, h, 32, 32, 0, 29, (20, 24, 30, 195))
        _draw_circle_ring(buf, w, h, 32, 32, 26, 29, ring_col)
        # Mic capsule
        _draw_rect(buf, w, h, 26, 16, 38, 34, fg_col)
        _draw_circle_ring(buf, w, h, 32, 16, 0, 6, fg_col)
        _draw_circle_ring(buf, w, h, 32, 34, 0, 6, fg_col)
        # U-bracket
        for y in range(28, 48):
            for x in range(18, 46):
                d = math.hypot(x - 32, y - 33)
                if 10 <= d <= 13 and y >= 32:
                    _put_px(buf, w, h, x, y, *fg_col)
        # Stand
        _draw_rect(buf, w, h, 30, 44, 34, 52, fg_col)
        _draw_rect(buf, w, h, 23, 50, 41, 53, fg_col)
        icons.append((name, w, h, bytes(buf)))

    # 4. voice_speaker (white speaker icon)
    buf = _make_canvas(w, h)
    _draw_rect(buf, w, h, 10, 24, 22, 40, (255, 255, 255, 255))
    for x in range(22, 36):
        span = 8 + (x - 22)
        _draw_rect(buf, w, h, x, 32 - span, x + 1, 32 + span, (255, 255, 255, 255))
    for r_in, r_out in ((11, 14), (19, 22)):
        for y in range(h):
            for x in range(36, w):
                d = math.hypot(x - 34, y - 32)
                ang = abs(math.degrees(math.atan2(y - 32, x - 34)))
                if r_in <= d <= r_out and ang <= 48:
                    _put_px(buf, w, h, x, y, 255, 255, 255, 255)
    icons.append(("voice_speaker", w, h, bytes(buf)))

    # 5. afk_icon (amber clock badge shown above AFK players)
    buf = _make_canvas(w, h)
    _draw_circle_ring(buf, w, h, 32, 32, 0, 28, (24, 24, 30, 210))
    _draw_circle_ring(buf, w, h, 32, 32, 24, 28, (245, 185, 55, 255))
    # Clock hands (12 o'clock + 3 o'clock) and center dot
    _draw_rect(buf, w, h, 30, 14, 34, 34, (255, 255, 255, 255))
    _draw_rect(buf, w, h, 30, 30, 46, 34, (255, 255, 255, 255))
    _draw_circle_ring(buf, w, h, 32, 32, 0, 4, (245, 185, 55, 255))
    icons.append(("afk_icon", w, h, bytes(buf)))

    return icons


# ══════════ Clean-Room Blank SCM & Empty VER2 script.img Generator ══════════

def build_clean_blank_scm() -> bytes:
    """
    Generate a minimal, 100% clean-room GTA:SA SCM script (`SAMP/main.scm`)
    conforming to GTA:SA Android v2.10's 6-segment SCM binary layout.

    Creates player 0, disables wanted/wasted checks, fades in, and enters an
    idle loop so the SA-MP native client owns spawning, camera, and world state.
    """
    var_space_size = 4096

    # Segment 0 (0x73): Global variable space
    seg0_len = 8 + var_space_size
    seg0 = struct.pack("<HBIB", 0x0002, 0x01, seg0_len, 0x73) + bytes(var_space_size)

    # Segment 1 (0x00): Used object names (count=1, slot 0 reserved header)
    seg1_body = struct.pack("<I", 1) + b"SAMP-MOBILE CLEAN SCM".ljust(24, b"\x00")
    seg1_next = seg0_len + 8 + len(seg1_body)
    seg1 = struct.pack("<HBIB", 0x0002, 0x01, seg1_next, 0x00) + seg1_body

    # Segment 2 (0x01): Mission table (0 missions) — patched with final main_size below
    seg2_len = 8 + 16
    seg2_next = seg1_next + seg2_len

    # Segment 3 (0x02): External scripts (0 scripts, matching empty script.img)
    seg3_body = struct.pack("<II", 0, 0)
    seg3_next = seg2_next + 8 + len(seg3_body)
    seg3 = struct.pack("<HBIB", 0x0002, 0x01, seg3_next, 0x02) + seg3_body

    # Segment 4 (0x03): Reserved segment
    seg4_body = struct.pack("<I", 0)
    seg4_next = seg3_next + 8 + len(seg4_body)
    seg4 = struct.pack("<HBIB", 0x0002, 0x01, seg4_next, 0x03) + seg4_body

    # Segment 5 (0x04): Global variable space size + version marker
    seg5_body = struct.pack("<II", var_space_size, 1)
    seg5_next = seg4_next + 8 + len(seg5_body)
    seg5 = struct.pack("<HBIB", 0x0002, 0x01, seg5_next, 0x04) + seg5_body

    # Main script entry opcodes (starts at seg5_next)
    code = bytearray()
    # 03A4: name_thread 'MAIN' (type 0x09 = 8-byte short string)
    code += struct.pack("<HB8s", 0x03A4, 0x09, b"MAIN\x00\x00\x00\x00")
    # 01F0: set_max_wanted_level 0
    code += struct.pack("<HBb", 0x01F0, 0x04, 0)
    # 0111: set_wasted_busted_check 0
    code += struct.pack("<HBb", 0x0111, 0x04, 0)
    # 00C0: set_current_time 12 0
    code += struct.pack("<HBbBb", 0x00C0, 0x04, 12, 0x04, 0)
    # 01EB: set_car_density_multiplier 0.0
    code += struct.pack("<HBf", 0x01EB, 0x06, 0.0)
    # 03DE: set_ped_density_multiplier 0.0
    code += struct.pack("<HBf", 0x03DE, 0x06, 0.0)
    # 04E4: refresh_game_renderer 0.0 0.0
    code += struct.pack("<HBfBf", 0x04E4, 0x06, 0.0, 0x06, 0.0)
    # 03CB: set_camera_position 0.0 0.0 3.0
    code += struct.pack("<HBfBfBf", 0x03CB, 0x06, 0.0, 0x06, 0.0, 0x06, 3.0)
    # 0053: create_player 0 at 0.0 0.0 3.0 store_to $8 ($PLAYER_CHAR)
    code += struct.pack("<HBbBfBfBfBH", 0x0053, 0x04, 0, 0x06, 0.0, 0x06, 0.0, 0x06, 3.0, 0x02, 8)
    # 01F5: get_player_actor $8 store_to $12 ($PLAYER_ACTOR)
    code += struct.pack("<HBHBH", 0x01F5, 0x02, 8, 0x02, 12)
    # 016A: fade 0 ms in (1)
    code += struct.pack("<HBhBb", 0x016A, 0x05, 0, 0x04, 1)

    loop_offset = seg5_next + len(code)
    # 0001: wait 1000
    code += struct.pack("<HBh", 0x0001, 0x05, 1000)
    # 0002: jump @loop_offset
    code += struct.pack("<HBI", 0x0002, 0x01, loop_offset)

    main_size = seg5_next + len(code)
    seg2_body = struct.pack("<IIHHI", main_size, 0, 0, 0, 0)
    seg2 = struct.pack("<HBIB", 0x0002, 0x01, seg2_next, 0x01) + seg2_body

    return seg0 + seg1 + seg2 + seg3 + seg4 + seg5 + bytes(code)


def build_empty_ver2_img() -> bytes:
    """Empty VER2 IMG archive (0 entries)."""
    return struct.pack("<4sI", b"VER2", 0)


# ══════════ Official GTA:SA Base Extraction ══════════

def should_copy_official_rel(rel: str, keep_audio: bool, include_data: bool) -> bool:
    parts = rel.split("/")
    top = parts[0].lower()

    if top in ("anim", "models"):
        return True
    if top == "data":
        return include_data
    if top == "audio":
        if keep_audio:
            return True
        return len(parts) == 3 and parts[1].lower() == "config" and parts[2].lower() in MINIMAL_AUDIO_CONFIG
    if top == "texdb":
        if len(parts) == 2:
            return parts[1].lower() in OFFICIAL_TEXDB_IMGS
        if len(parts) == 3:
            db_name = parts[1].lower()
            fname = parts[2].lower()
            want_fmt = OFFICIAL_TEXDB_FORMATS.get(db_name)
            if want_fmt:
                return fname in (
                    f"{db_name}.txt",
                    f"{db_name}.{want_fmt}.dat",
                    f"{db_name}.{want_fmt}.toc",
                    f"{db_name}.{want_fmt}.tmb",
                )
    if rel.lower() == "stream.ini":
        return True
    return False


def index_texdb_txt(txt_path: str, existing_texnames: set) -> None:
    with open(txt_path, "r", encoding="latin1", errors="ignore") as f:
        for line in f:
            if line.startswith('"'):
                existing_texnames.add(line.split('"')[1].lower())


def extract_official_base(gtasa_sources, out_dir, keep_audio=False, skip_base_data=False):
    print(f"[1/4] Extracting official GTA:SA base from {len(gtasa_sources)} source(s) ...")
    existing_texnames = set()
    copied_count = 0

    for src in gtasa_sources:
        p = Path(src)
        if p.is_dir():
            for file_path in p.rglob("*"):
                if not file_path.is_file():
                    continue
                rel = file_path.relative_to(p).as_posix()
                if rel.lower().startswith("assets/"):
                    rel = rel[len("assets/"):]
                if not should_copy_official_rel(rel, keep_audio=keep_audio, include_data=not skip_base_data):
                    continue
                dst = os.path.join(out_dir, rel.replace("/", os.sep))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copy2(file_path, dst)
                copied_count += 1
                if rel.lower().startswith("texdb/") and dst.lower().endswith(".txt"):
                    index_texdb_txt(dst, existing_texnames)
        elif p.is_file() and zipfile.is_zipfile(p):
            with zipfile.ZipFile(p, "r") as z:
                for info in z.infolist():
                    if info.is_dir():
                        continue
                    rel = info.filename.replace("\\", "/")
                    if rel.lower().startswith("assets/"):
                        rel = rel[len("assets/"):]
                    if not should_copy_official_rel(rel, keep_audio=keep_audio, include_data=not skip_base_data):
                        continue
                    dst = os.path.join(out_dir, rel.replace("/", os.sep))
                    os.makedirs(os.path.dirname(dst), exist_ok=True)
                    with z.open(info, "r") as src_f, open(dst, "wb") as dst_f:
                        shutil.copyfileobj(src_f, dst_f, length=1024 * 1024)
                    copied_count += 1
                    if rel.lower().startswith("texdb/") and dst.lower().endswith(".txt"):
                        index_texdb_txt(dst, existing_texnames)
        else:
            raise ValueError(f"Unsupported --gtasa source (must be a directory, .obb, or .apk): {src}")

    print(f"      Official base extracted ({copied_count} files, {len(existing_texnames)} base textures).")

    required_base = [
        os.path.join("texdb", "gta3.img"),
        os.path.join("texdb", "gta3", "gta3.txt"),
        os.path.join("anim", "anim.img"),
    ]
    if not skip_base_data:
        required_base.append(os.path.join("data", "gta.dat"))
    missing_base = [rel for rel in required_base if not os.path.isfile(os.path.join(out_dir, rel))]
    if missing_base:
        raise RuntimeError(
            "Incomplete --gtasa input; missing required official GTA:SA base files: "
            + ", ".join(missing_base)
            + ". Note: a standard ~26 MB GTA:SA .apk only contains native code and small configs — "
            "you must also pass the matching main/patch .obb expansion files (or a 2 GB+ asset-pack .apk / unpacked files/ directory) to --gtasa."
        )

    return existing_texnames


# ══════════ SA-MP Overlay from Official PC GTA:SA + SA-MP 0.3.7 ══════════

def generate_samp_configs(out_dir: str, pc_samp_dir: str, scm_dir: str | None, cjk_font: str | None, fonts_dir: str | None):
    print("[2/4] Generating SAMP/ configs, blank SCM scripts, and open-source fonts ...")
    samp_dst = os.path.join(out_dir, "SAMP")
    data_dir = os.path.join(out_dir, "data")
    os.makedirs(samp_dst, exist_ok=True)

    # 1. SAMP/SAMP.ide from PC SA-MP
    pc_ide = os.path.join(pc_samp_dir, "SAMP", "SAMP.ide")
    if not os.path.isfile(pc_ide):
        raise FileNotFoundError(f"PC SA-MP SAMP.ide not found: {pc_ide}")
    shutil.copy2(pc_ide, os.path.join(samp_dst, "SAMP.ide"))

    # 2. SAMP/gta.dat patched from stock data/gta.dat
    stock_gta_dat = os.path.join(data_dir, "gta.dat")
    if os.path.isfile(stock_gta_dat):
        with open(stock_gta_dat, "r", encoding="latin1", errors="ignore") as f:
            lines = f.read().splitlines()
        out_lines = []
        inserted_samp_ide = False
        for ln in lines:
            if ln.strip().upper() == r"IMG DATA\PATHS\CARREC.IMG":
                continue
            out_lines.append(ln)
            if ln.strip().upper() == r"IDE DATA\TXDCUT.IDE":
                out_lines.append("")
                out_lines.append("# sa-mp additions")
                out_lines.append(r"IDE SAMP\samp.IDE")
                inserted_samp_ide = True
        if not inserted_samp_ide:
            out_lines.extend(["", "# sa-mp additions", r"IDE SAMP\samp.IDE"])
        with open(os.path.join(samp_dst, "gta.dat"), "w", encoding="latin1", newline="\n") as f:
            f.write("\n".join(out_lines) + "\n")
    else:
        print("      WARN: data/gta.dat not found; skipped generating SAMP/gta.dat.")

    # 3. SAMP/peds.ide patched from stock data/peds.ide with SA-MP story skins
    stock_peds_ide = os.path.join(data_dir, "peds.ide")
    if os.path.isfile(stock_peds_ide):
        with open(stock_peds_ide, "r", encoding="latin1", errors="ignore") as f:
            raw_lines = f.read().splitlines()
        existing_ids = set()
        patched = []
        in_peds = False
        for ln in raw_lines:
            st = ln.strip()
            if st.lower() == "peds":
                in_peds = True
                patched.append(ln)
                continue
            if in_peds and st.lower() == "end":
                for ped_id in sorted(SAMP_EXTRA_PED_LINES):
                    if ped_id not in existing_ids:
                        patched.append(SAMP_EXTRA_PED_LINES[ped_id])
                        existing_ids.add(ped_id)
                in_peds = False
                patched.append(ln)
                continue
            if in_peds and st and not st.startswith("#"):
                parts = st.split(",", 1)
                if parts and parts[0].strip().isdigit():
                    pid = int(parts[0].strip())
                    if pid in SAMP_EXTRA_PED_LINES:
                        patched.append(SAMP_EXTRA_PED_LINES[pid])
                        existing_ids.add(pid)
                        continue
                    existing_ids.add(pid)
            patched.append(ln)
        with open(os.path.join(samp_dst, "peds.ide"), "w", encoding="latin1", newline="\n") as f:
            f.write("\n".join(patched) + "\n")
    else:
        print("      WARN: data/peds.ide not found; skipped generating SAMP/peds.ide.")

    # 4. Copy unchanged configs from official data/
    for fn in ("TIMECYC.DAT", "vehicles.ide", "handling.cfg", "weapon.dat"):
        src = os.path.join(data_dir, fn)
        if not os.path.isfile(src):
            src = os.path.join(data_dir, fn.lower())
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(samp_dst, fn))

    # 5. Disable AI train tracks
    for fn in ("tracks2.dat", "tracks4.dat"):
        with open(os.path.join(samp_dst, fn), "w", encoding="ascii", newline="") as f:
            f.write("0")

    # 6. SAMP/main.scm + SAMP/script.img (clean-room blank by default, or user --scm-dir)
    if scm_dir:
        for fn in ("main.scm", "script.img"):
            src = os.path.join(scm_dir, fn)
            if not os.path.isfile(src):
                raise FileNotFoundError(f"Missing {fn} in --scm-dir: {scm_dir}")
            shutil.copy2(src, os.path.join(samp_dst, fn))
        print(f"      Copied custom main.scm & script.img from {scm_dir}.")
    else:
        with open(os.path.join(samp_dst, "main.scm"), "wb") as f:
            f.write(build_clean_blank_scm())
        with open(os.path.join(samp_dst, "script.img"), "wb") as f:
            f.write(build_empty_ver2_img())
        print("      Generated clean-room blank SAMP/main.scm & empty SAMP/script.img.")

    # 7. Open-source fonts in SAMP/fonts/
    fonts_dst = os.path.join(samp_dst, "fonts")
    os.makedirs(fonts_dst, exist_ok=True)
    arial_src = None
    if fonts_dir and os.path.isfile(os.path.join(fonts_dir, "arial_bold.ttf")):
        arial_src = os.path.join(fonts_dir, "arial_bold.ttf")
    elif DEFAULT_ROBO_FONT.is_file():
        arial_src = str(DEFAULT_ROBO_FONT)
    if arial_src:
        shutil.copy2(arial_src, os.path.join(fonts_dst, "arial_bold.ttf"))

    cjk_src = cjk_font
    if not cjk_src and fonts_dir and os.path.isfile(os.path.join(fonts_dir, "chinese.ttf")):
        cjk_src = os.path.join(fonts_dir, "chinese.ttf")
    if cjk_src:
        shutil.copy2(cjk_src, os.path.join(fonts_dst, "chinese.ttf"))
    elif arial_src:
        shutil.copy2(arial_src, os.path.join(fonts_dst, "chinese.ttf"))
        print("      NOTE: No --cjk-font provided; copied Roboto as chinese.ttf (pass an open-source CJK .ttf like Noto Sans SC for Chinese glyphs).")

    if fonts_dir and os.path.isfile(os.path.join(fonts_dir, "gtaweap3.ttf")):
        shutil.copy2(os.path.join(fonts_dir, "gtaweap3.ttf"), os.path.join(fonts_dst, "gtaweap3.ttf"))


def build_samp_img_and_texdb(pc_samp_dir: str, out_dir: str, existing_texnames: set):
    print("[3/4] Converting PC SAMP.img -> mobile texdb/samp.img + texdb/samp/ + texdb/samp_extra/ ...")
    try:
        import numpy  # noqa: F401
    except ImportError as exc:
        raise RuntimeError("--pc-samp requires numpy (pip install numpy)") from exc

    texdb_root = os.path.join(out_dir, "texdb")
    os.makedirs(texdb_root, exist_ok=True)

    # 1. Copy SAMPCOL.img directly
    pc_col = os.path.join(pc_samp_dir, "SAMP", "SAMPCOL.img")
    if not os.path.isfile(pc_col):
        raise FileNotFoundError(f"PC SA-MP SAMPCOL.img not found: {pc_col}")
    shutil.copy2(pc_col, os.path.join(texdb_root, "SAMPCOL.img"))

    # 2. Split PC SAMP.img: non-.txd -> mobile texdb/samp.img; .txd -> texdb/samp/
    pc_img = os.path.join(pc_samp_dir, "SAMP", "SAMP.img")
    if not os.path.isfile(pc_img):
        raise FileNotFoundError(f"PC SA-MP SAMP.img not found: {pc_img}")

    model_entries = []  # (name_bytes_24, raw_bytes)
    samp_tex_entries = []
    samp_tex_seen = set()
    samp_case_variants: dict[str, set[str]] = {}

    def collect_txd_from_bytes(
        blob: bytes,
        target_list,
        seen_set,
        filter_names=None,
        skip_existing=False,
        variant_map=None,
    ):
        for t in parse_txd_bytes(blob):
            key = t["name"].lower()
            if not key:
                continue
            if variant_map is not None:
                variant_map.setdefault(key, set()).add(t["name"])
            if key in seen_set:
                continue
            if skip_existing and key in existing_texnames:
                continue
            if filter_names is not None and key not in filter_names:
                continue
            rgba = decode_txd_texture(t)
            if rgba is None:
                continue
            seen_set.add(key)
            target_list.append((t["name"], t["w"], t["h"], rgba))

    with open(pc_img, "rb") as f:
        magic, count = struct.unpack("<4sI", f.read(8))
        if magic != b"VER2":
            raise ValueError(f"Unsupported SAMP.img magic: {magic!r}")
        dir_entries = [struct.unpack("<IHH24s", f.read(32)) for _ in range(count)]

        for off_sec, sz_sec, _sz2, raw_name in dir_entries:
            name = raw_name.split(b"\x00", 1)[0].decode("latin1", errors="ignore")
            f.seek(off_sec * 2048)
            blob = f.read(sz_sec * 2048)
            lname = name.lower()
            if lname.endswith(".txd"):
                collect_txd_from_bytes(blob, samp_tex_entries, samp_tex_seen, variant_map=samp_case_variants)
            else:
                model_entries.append((raw_name, blob))
                if lname.endswith(".dff"):
                    for ref in parse_dff_diffuse_textures(blob):
                        samp_case_variants.setdefault(ref.lower(), set()).add(ref)

    # Write mobile texdb/samp.img (models only, 2048-byte sector aligned VER2)
    header_bytes = 8 + len(model_entries) * 32
    cur_sector = (header_bytes + 2047) // 2048
    out_dir_table = []
    for raw_name, blob in model_entries:
        sectors = (len(blob) + 2047) // 2048
        out_dir_table.append((cur_sector, sectors, raw_name))
        cur_sector += sectors

    samp_img_out = os.path.join(texdb_root, "samp.img")
    with open(samp_img_out, "wb") as out_f:
        out_f.write(struct.pack("<4sI", b"VER2", len(model_entries)))
        for off_sec, sz_sec, raw_name in out_dir_table:
            out_f.write(struct.pack("<IHH24s", off_sec, sz_sec, 0, raw_name))
        pad = (out_dir_table[0][0] * 2048 - out_f.tell()) if out_dir_table else 0
        if pad > 0:
            out_f.write(b"\x00" * pad)
        for _raw_name, blob in model_entries:
            out_f.write(blob)
            rem = len(blob) % 2048
            if rem:
                out_f.write(b"\x00" * (2048 - rem))

    # Also include SAMP/blanktex.txd, SAMP/samaps.txd (skipping 'map' already in base txd),
    # and procedural clean-room UI icons
    for extra_txd, skip_exist in (("blanktex.txd", False), ("samaps.txd", True)):
        p = os.path.join(pc_samp_dir, "SAMP", extra_txd)
        if os.path.isfile(p):
            collect_txd_from_bytes(
                open(p, "rb").read(),
                samp_tex_entries,
                samp_tex_seen,
                skip_existing=skip_exist,
                variant_map=samp_case_variants,
            )

    for icon_name, iw, ih, rgba in generate_ui_icons():
        if icon_name.lower() not in samp_tex_seen:
            samp_tex_seen.add(icon_name.lower())
            samp_tex_entries.append((icon_name, iw, ih, rgba))

    samp_dat_bytes, samp_aliases = write_texdb_database(
        os.path.join(texdb_root, "samp"),
        "samp",
        samp_tex_entries,
        mipmaps=True,
        alias_map=samp_case_variants,
    )
    existing_texnames.update(samp_tex_seen)
    print(
        f"      Built texdb/samp.img ({len(model_entries)} models) + texdb/samp/ "
        f"({len(samp_tex_entries)} textures + {samp_aliases} case aliases, {samp_dat_bytes / 1048576:.2f} MB)."
    )

    # 3. Build texdb/samp_extra/ from PC GTA:SA models/txd/*.txd + fronten2.txd + vehicle.txd
    extra_entries = []
    extra_seen = set()
    txd_dir = os.path.join(pc_samp_dir, "models", "txd")
    if os.path.isdir(txd_dir):
        for fn in sorted(os.listdir(txd_dir)):
            if fn.lower().endswith(".txd"):
                p = os.path.join(txd_dir, fn)
                collect_txd_from_bytes(open(p, "rb").read(), extra_entries, extra_seen, skip_existing=True)

    for rel_txd, names in EXTRA_PC_TXD_SOURCES:
        p = os.path.join(pc_samp_dir, rel_txd)
        if os.path.isfile(p):
            collect_txd_from_bytes(
                open(p, "rb").read(), extra_entries, extra_seen, filter_names=names, skip_existing=True
            )

    if extra_entries:
        extra_bytes, extra_aliases = write_texdb_database(
            os.path.join(texdb_root, "samp_extra"),
            "samp_extra",
            extra_entries,
            mipmaps=False,
        )
        print(
            f"      Built texdb/samp_extra/ ({len(extra_entries)} PC TextDraw textures "
            f"+ {extra_aliases} case aliases, {extra_bytes / 1048576:.2f} MB)."
        )


def summarize(out_dir):
    print(f"[4/4] Summary of {out_dir}:")
    grand = 0
    for item in sorted(os.listdir(out_dir)):
        p = os.path.join(out_dir, item)
        if os.path.isdir(p):
            sz = sum(
                os.path.getsize(os.path.join(dp, fn))
                for dp, _, fns in os.walk(p)
                for fn in fns
            )
            cnt = sum(len(fns) for _, _, fns in os.walk(p))
            print(f"      {item + '/':16s} {sz / 1048576:8.2f} MB  ({cnt} files)")
            grand += sz
        else:
            sz = os.path.getsize(p)
            print(f"      {item:16s} {sz / 1048576:8.2f} MB")
            grand += sz
    print(f"      {'TOTAL':16s} {grand / 1048576:8.2f} MB")


def pack_gamedata_zip(src_dir: str, zip_path: str, compress: bool = False) -> tuple[int, int]:
    """Pack generated files/ tree into a single .zip ready for in-launcher one-click import."""
    os.makedirs(os.path.dirname(os.path.abspath(zip_path)), exist_ok=True)
    mode = zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED
    file_count = 0
    with zipfile.ZipFile(zip_path, "w", compression=mode, allowZip64=True) as zf:
        for dp, dirnames, filenames in os.walk(src_dir):
            dirnames.sort()
            for fn in sorted(filenames):
                full = os.path.join(dp, fn)
                arc = os.path.relpath(full, src_dir).replace("\\", "/")
                zf.write(full, arc)
                file_count += 1
    return file_count, os.path.getsize(zip_path)


FONT_EXTS = {".ttf", ".ttc", ".otf"}


def discover_gtasa_in_dir(search_dir: Path) -> list[str]:
    """Auto-discover GTA:SA Android v2.10 .obb (main before patch) or full .apk in dist/."""
    if not search_dir.is_dir():
        return []
    obbs = [p for p in search_dir.iterdir() if p.is_file() and p.suffix.lower() == ".obb"]
    if obbs:
        obbs.sort(
            key=lambda p: (
                0 if "main" in p.name.lower() else (2 if "patch" in p.name.lower() else 1),
                p.name.lower(),
            )
        )
        return [str(p) for p in obbs]
    apks = [p for p in search_dir.iterdir() if p.is_file() and p.suffix.lower() == ".apk"]
    if apks:
        apks.sort(key=lambda p: (-p.stat().st_size, p.name.lower()))
        return [str(apks[0])]
    return []


def discover_font_in_dir(search_dir: Path) -> str | None:
    """Auto-discover a CJK font file in dist/ (prefers filenames containing 'font' with .ttf/.ttc/.otf)."""
    if not search_dir.is_dir():
        return None
    fonts = sorted(
        (p for p in search_dir.iterdir() if p.is_file() and p.suffix.lower() in FONT_EXTS),
        key=lambda p: p.name.lower(),
    )
    if not fonts:
        return None
    for p in fonts:
        if "font" in p.stem.lower():
            return str(p)
    return str(fonts[0])


def main():
    ap = argparse.ArgumentParser(
        description="Build a clean, compact GTA:SA Mobile + SA-MP game data .zip (or files/ directory) from official GTA:SA + PC SA-MP."
    )
    ap.add_argument(
        "--gtasa",
        nargs="+",
        default=None,
        help="Official GTA:SA Android 2.10 source(s): .obb file(s), full .apk, or files/ directory (default: auto-detect main/patch .obb or .apk in dist/)",
    )
    ap.add_argument(
        "--pc-samp",
        required=True,
        help="Official PC GTA:SA directory with SA-MP 0.3.7 installed (containing SAMP/SAMP.img, SAMP.ide, SAMPCOL.img)",
    )
    ap.add_argument(
        "--cjk-font",
        default=None,
        help="Optional open-source CJK .ttf/.ttc/.otf font installed as SAMP/fonts/chinese.ttf (default: auto-detect font.* in dist/)",
    )
    ap.add_argument(
        "--fonts",
        default=None,
        help="Optional custom fonts directory (overrides arial_bold.ttf / chinese.ttf / gtaweap3.ttf if present)",
    )
    ap.add_argument(
        "--scm-dir",
        default=None,
        help="Optional directory containing custom main.scm & script.img (default: generate clean-room blank SCM)",
    )
    ap.add_argument(
        "--keep-audio",
        action="store_true",
        help="Include full GTA:SA audio streams/SFX (.osw) instead of minimal audio/CONFIG only",
    )
    ap.add_argument(
        "--skip-base-data",
        action="store_true",
        help="Do not copy data/ from --gtasa",
    )
    ap.add_argument(
        "--out",
        default=os.path.join("dist", "gamedata.zip"),
        help="Output .zip archive (recommended for in-launcher one-click import) or directory path (default: dist/gamedata.zip)",
    )
    ap.add_argument(
        "--compress",
        action="store_true",
        help="Use DEFLATE compression when writing .zip (smaller file, slower build & extraction; default is fast ZIP_STORED)",
    )
    args = ap.parse_args()

    default_dist = REPO_ROOT / "dist"
    if not args.gtasa:
        args.gtasa = discover_gtasa_in_dir(default_dist)
        if not args.gtasa:
            ap.error(
                f"No --gtasa specified and no .obb / .apk found in {default_dist}. "
                "Drop main/patch .obb (or a 2 GB+ full .apk) into dist/ or pass --gtasa explicitly."
            )
        print("Auto-detected --gtasa from dist/:", ", ".join(os.path.basename(p) for p in args.gtasa))

    if not args.cjk_font and not (args.fonts and os.path.isfile(os.path.join(args.fonts, "chinese.ttf"))):
        auto_font = discover_font_in_dir(default_dist)
        if auto_font:
            args.cjk_font = auto_font
            print(f"Auto-detected --cjk-font from dist/: {os.path.basename(auto_font)}")

    for p in args.gtasa:
        if not os.path.exists(p):
            ap.error(f"--gtasa path does not exist: {p}")
    if not os.path.isdir(args.pc_samp):
        ap.error(f"--pc-samp directory does not exist: {args.pc_samp}")
    if args.cjk_font and not os.path.isfile(args.cjk_font):
        ap.error(f"--cjk-font file does not exist: {args.cjk_font}")
    if args.fonts and not os.path.isdir(args.fonts):
        ap.error(f"--fonts directory does not exist: {args.fonts}")
    if args.scm_dir and not os.path.isdir(args.scm_dir):
        ap.error(f"--scm-dir directory does not exist: {args.scm_dir}")

    output_is_zip = args.out.lower().endswith(".zip")
    if output_is_zip:
        parent = os.path.dirname(os.path.abspath(args.out))
        os.makedirs(parent, exist_ok=True)
        work_dir = tempfile.mkdtemp(prefix=".gamedata_stage_", dir=parent)
    else:
        work_dir = args.out
        if os.path.exists(work_dir):
            shutil.rmtree(work_dir)
        os.makedirs(work_dir, exist_ok=True)

    try:
        existing = extract_official_base(
            args.gtasa,
            work_dir,
            keep_audio=args.keep_audio,
            skip_base_data=args.skip_base_data,
        )
        generate_samp_configs(work_dir, args.pc_samp, args.scm_dir, args.cjk_font, args.fonts)
        build_samp_img_and_texdb(args.pc_samp, work_dir, existing)
        summarize(work_dir)

        if output_is_zip:
            print(f"[5/5] Packing ZIP archive -> {args.out} ...")
            cnt, zsz = pack_gamedata_zip(work_dir, args.out, compress=args.compress)
            print(
                f"      Packed {cnt} files into {args.out} ({zsz / 1048576:.2f} MB). "
                "Copy this .zip anywhere on your phone and import it inside the launcher."
            )
    finally:
        if output_is_zip and os.path.isdir(work_dir):
            shutil.rmtree(work_dir, ignore_errors=True)


if __name__ == "__main__":
    main()
