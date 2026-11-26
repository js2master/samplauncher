#!/usr/bin/env python3
"""Build a real omp-cef resource .pak from a directory tree.

This is the host-side counterpart of the server plugin's packaging step
(src/server/common/resource_manager.cpp ProcessResourceDirectory):

  * only the extensions the plugin accepts are included
  * .html/.css/.js/.json/.xml/.svg are wrapped as
        "OCEF2" + flags(1) + iv(16) + AES-128-CBC(pkcs7) ciphertext
  * everything else is wrapped as
        "OCEF2" + flags(0) + plaintext
  * the wrappers are stored in a deflate-compressed zip, entry names use '/'
    as separator and are relative to the resource root

Usage: make_pak.py <src_dir> <out.pak> [master_key]
Prints one JSON line: {"path":..., "size":..., "sha256":..., "files":[...]}
"""

import hashlib
import io
import json
import os
import sys
import zipfile

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ALLOWED = {
    ".html", ".css", ".js", ".json", ".xml", ".png", ".jpg", ".jpeg", ".gif",
    ".svg", ".ico", ".mp3", ".ogg", ".wav", ".woff", ".woff2", ".ttf", ".otf",
    ".eot",
}
ENCRYPTED = {".html", ".css", ".js", ".json", ".xml", ".svg"}

DEFAULT_KEY = b"ThisIsA16ByteKey"  # server default: cef.master_resource_key


def wrap(plain: bytes, ext: str, key: bytes, name: str) -> bytes:
    if ext not in ENCRYPTED:
        return b"OCEF2" + bytes([0]) + plain

    if len(key) != 16:
        raise ValueError(f"AES-128 needs a 16-byte key, got {len(key)}")

    pad = 16 - (len(plain) % 16)
    padded = plain + bytes([pad]) * pad
    # Deterministic IV (name + payload) so the same tree always produces the
    # same .pak: the client's SHA-256 cache hit path can be tested by rebuilding
    # the pak. The server plugin uses a random IV; decryption is identical.
    iv = hashlib.sha256(name.encode() + b"\0" + plain).digest()[:16]
    enc = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return b"OCEF2" + bytes([1]) + iv + enc.update(padded) + enc.finalize()


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2

    src = sys.argv[1]
    out = sys.argv[2]
    key = sys.argv[3].encode() if len(sys.argv) > 3 else DEFAULT_KEY

    entries = []
    for root, _dirs, names in os.walk(src):
        for name in sorted(names):
            path = os.path.join(root, name)
            rel = os.path.relpath(path, src).replace(os.sep, "/")
            ext = os.path.splitext(name)[1].lower()
            if ext not in ALLOWED:
                print(f"skip (extension not allowed by the plugin): {rel}", file=sys.stderr)
                continue
            with open(path, "rb") as handle:
                entries.append((rel, handle.read(), ext))

    if not entries:
        print("no packable files", file=sys.stderr)
        return 1

    entries.sort(key=lambda item: item[0])

    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for rel, data, ext in entries:
            # Fixed timestamp so the archive is byte-identical across runs.
            info = zipfile.ZipInfo(rel, date_time=(2024, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, wrap(data, ext, key, rel))

    pak = buffer.getvalue()
    with open(out, "wb") as handle:
        handle.write(pak)

    print(json.dumps({
        "path": os.path.abspath(out),
        "size": len(pak),
        "sha256": hashlib.sha256(pak).hexdigest(),
        "files": [{"path": rel, "encrypted": ext in ENCRYPTED} for rel, _d, ext in entries],
    }))
    return 0


if __name__ == "__main__":
    sys.exit(main())
