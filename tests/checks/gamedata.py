"""End-to-end contract test for tools/gamedata/publish_gamedata.py & DataManifest.java."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Tuple

from .common import REPO_ROOT, Runner

PUBLISH_SCRIPT = REPO_ROOT / "tools/gamedata/publish_gamedata.py"


def _is_hex64(s: object) -> bool:
    return isinstance(s, str) and len(s) == 64 and all(c in "0123456789abcdef" for c in s.lower())


def _validate_manifest(m: dict) -> Tuple[bool, str]:
    """Match DataManifest.java parsing rules."""
    if m.get("schema") != 1:
        return False, f"schema={m.get('schema')}"
    if not m.get("version"):
        return False, "missing version"
    groups = [k for k in ("game", "online") if k in m]
    if not groups:
        return False, "missing both game and online groups"

    for key in groups:
        g = m[key]
        pkgs = g.get("packages")
        if not pkgs:
            return False, f"{key}.packages is empty"
        for i, p in enumerate(pkgs):
            where = f"{key}.packages[{i}]"
            pid = p.get("id", "")
            if not pid or "/" in pid or "\\" in pid or ".." in pid:
                return False, f"{where}.id invalid ({pid!r})"
            if not isinstance(p.get("bytes"), int) or p["bytes"] <= 0:
                return False, f"{where}.bytes invalid"
            if not _is_hex64(p.get("sha256", "")):
                return False, f"{where}.sha256 not hex64"
            chunks = p.get("chunks")
            if not chunks:
                return False, f"{where}.chunks is empty"
            total = 0
            for j, c in enumerate(chunks):
                if not isinstance(c.get("index"), int) or c["index"] < 0:
                    return False, f"{where}.chunks[{j}].index invalid"
                if not c.get("url"):
                    return False, f"{where}.chunks[{j}] missing url"
                if not isinstance(c.get("bytes"), int) or c["bytes"] <= 0:
                    return False, f"{where}.chunks[{j}].bytes invalid"
                if c.get("sha256") and not _is_hex64(c["sha256"]):
                    return False, f"{where}.chunks[{j}].sha256 not hex64"
                total += c["bytes"]
            if total != p["bytes"]:
                return False, f"{where} sum(chunks)={total} != bytes {p['bytes']}"
    return True, "ok"


def run_gamedata_check(r: Runner) -> None:
    if not PUBLISH_SCRIPT.is_file():
        r.fail("Gamedata publish roundtrip", f"missing {PUBLISH_SCRIPT.relative_to(REPO_ROOT).as_posix()}")
        return

    with tempfile.TemporaryDirectory(prefix="samp_gd_test_") as td:
        tmp = Path(td)
        src = tmp / "data"
        out = tmp / "cdn"
        (src / "SAMP" / "fonts").mkdir(parents=True)
        (src / "a.txt").write_text("hello 数据包\n", encoding="utf-8")
        (src / "SAMP" / "big.bin").write_bytes(os.urandom(1_300_000))
        (src / "SAMP" / "fonts" / "chinese.ttf").write_bytes(os.urandom(2048))
        (src / ".hidden").write_text("should be skipped", encoding="utf-8")

        env = dict(os.environ, SAMP_GAMEDATA_CHUNK_SIZE=str(512 * 1024))
        proc = subprocess.run(
            [
                sys.executable,
                str(PUBLISH_SCRIPT),
                "--game", str(src),
                "--out", str(out),
                "--work", str(tmp / "w"),
                "--version", "7",
            ],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            env=env,
        )
        if proc.returncode != 0:
            r.fail("Gamedata publish_gamedata.py execution", (proc.stderr or proc.stdout).strip()[-240:])
            return

        manifest_path = out / "manifest.json"
        if not manifest_path.is_file():
            r.fail("Gamedata manifest.json generated", "file missing")
            return

        m = json.loads(manifest_path.read_text(encoding="utf-8"))
        ok, reason = _validate_manifest(m)
        if not ok or m.get("version") != "7" or "online" in m:
            r.fail("Gamedata manifest schema contract", reason or "unexpected version/group")
            return

        pkg = m["game"]["packages"][0]
        chunks = pkg["chunks"]
        if len(chunks) < 3 or [c["index"] for c in chunks] != list(range(len(chunks))):
            r.fail("Gamedata multi-chunk indexing", f"chunks={len(chunks)}")
            return

        # Reassemble all chunks and verify every chunk + whole-archive SHA-256
        parts = []
        for c in chunks:
            cp = out / c["url"]
            if not cp.is_file():
                r.fail("Gamedata chunk presence", f"missing {c['url']}")
                return
            data = cp.read_bytes()
            if len(data) != c["bytes"] or hashlib.sha256(data).hexdigest() != c["sha256"]:
                r.fail("Gamedata chunk integrity", f"size/sha256 mismatch on {c['url']}")
                return
            parts.append(data)

        rebuilt = b"".join(parts)
        if len(rebuilt) != pkg["bytes"] or hashlib.sha256(rebuilt).hexdigest() != pkg["sha256"]:
            r.fail("Gamedata reassembled archive SHA-256", "size or digest mismatch")
            return

        zpath = tmp / "rebuilt.zip"
        zpath.write_bytes(rebuilt)
        with zipfile.ZipFile(zpath, "r") as zf:
            names = set(zf.namelist())
            if zf.testzip() is not None:
                r.fail("Gamedata reassembled ZIP CRC", "corrupt entry")
                return
            if not {"a.txt", "SAMP/big.bin", "SAMP/fonts/chinese.ttf"}.issubset(names):
                r.fail("Gamedata reassembled ZIP entries", f"entries={sorted(names)}")
                return
            if any(n.startswith(".") for n in names):
                r.fail("Gamedata hidden-file exclusion", "hidden file leaked into archive")
                return

        # Negative validation checks
        bad_cases = [
            {"schema": 2, "version": "1", "game": m["game"]},
            {"schema": 1, "game": m["game"]},
            {"schema": 1, "version": "1"},
        ]
        broken_sum = json.loads(json.dumps(m))
        broken_sum["game"]["packages"][0]["chunks"][0]["bytes"] += 1
        broken_id = json.loads(json.dumps(m))
        broken_id["game"]["packages"][0]["id"] = "../../etc"
        bad_cases.extend([broken_sum, broken_id])
        for idx, bad in enumerate(bad_cases):
            good, _ = _validate_manifest(bad)
            if good:
                r.fail("Gamedata negative manifest validation", f"case #{idx} erroneously accepted")
                return

        r.ok("Gamedata publish/chunk/SHA-256 roundtrip", f"{len(chunks)} chunks reassembled, ZIP + negative cases verified")
