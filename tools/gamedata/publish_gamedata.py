#!/usr/bin/env python3
"""
把本地游戏数据打包成客户端可下载的分片 + 清单，可选直接上传到 Cloudflare R2。

用法：
    # 只生成产物到 dist/cdn/
    python tools/gamedata/publish_gamedata.py --game dist/gamedata/files

    # 生成后上传到 R2
    python tools/gamedata/publish_gamedata.py --game dist/gamedata/files --upload

    # 游戏本体 + 联机包分别指定
    python tools/gamedata/publish_gamedata.py \\
        --game dist/gamedata/files \\
        --online dist/samp_online/files \\
        --version 2

产物结构（dist/cdn/）：
    manifest.json
    chunks/<packageId>/0000.bin, 0001.bin, ...

设计要点：
- 先打 zip（不压缩，只做容器），再切片。这样客户端拼装后是一个标准 zip，
  解压逻辑不需要认识自定义格式。
- 每片 16MB：兼顾并发数与单片重传代价。R2 支持 Range，断点续传按片进行。
- 分片与整包都算 SHA-256，客户端两级校验。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import zipfile
from pathlib import Path

CHUNK_SIZE = 16 * 1024 * 1024  # 16MB

# 允许用环境变量覆盖分片大小，仅用于离线自检（真机发布不要改）。
_env_chunk = os.environ.get("SAMP_GAMEDATA_CHUNK_SIZE")
if _env_chunk:
    CHUNK_SIZE = int(_env_chunk)
MANIFEST_SCHEMA = 1


def human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024.0
    return f"{n:.1f} PB"


def sha256_file(path: Path, bufsize: int = 1024 * 1024) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        while True:
            block = f.read(bufsize)
            if not block:
                break
            h.update(block)
    return h.hexdigest()


def make_zip(src_dir: Path, out_zip: Path) -> Path:
    """把目录打成 zip（存储模式，不压缩——游戏数据多为已压缩格式，再压没收益）。"""
    if not src_dir.is_dir():
        raise SystemExit(f"目录不存在: {src_dir}")

    out_zip.parent.mkdir(parents=True, exist_ok=True)
    if out_zip.exists():
        out_zip.unlink()

    count = 0
    total = 0
    with zipfile.ZipFile(out_zip, "w", zipfile.ZIP_STORED, allowZip64=True) as zf:
        for root, _dirs, files in os.walk(src_dir):
            for name in files:
                full = Path(root) / name
                rel = full.relative_to(src_dir).as_posix()
                # 跳过隐藏文件与 macOS 垃圾
                if rel.startswith(".") or rel.startswith("__MACOSX") or name == ".DS_Store":
                    continue
                zf.write(full, rel)
                count += 1
                total += full.stat().st_size
                if count % 2000 == 0:
                    print(f"  打包中… {count} 个文件 / {human(total)}", flush=True)

    print(f"  完成：{count} 个文件 / {human(total)} -> {out_zip} ({human(out_zip.stat().st_size)})")
    return out_zip


def split_zip(zip_path: Path, out_dir: Path, package_id: str) -> list[dict]:
    """把 zip 切成固定大小的分片，返回分片描述列表。"""
    chunk_dir = out_dir / "chunks" / package_id
    if chunk_dir.exists():
        shutil.rmtree(chunk_dir)
    chunk_dir.mkdir(parents=True, exist_ok=True)

    chunks = []
    index = 0
    with zip_path.open("rb") as f:
        while True:
            block = f.read(CHUNK_SIZE)
            if not block:
                break
            name = f"{index:04d}.bin"
            target = chunk_dir / name
            target.write_bytes(block)
            chunks.append({
                "index": index,
                "url": f"chunks/{package_id}/{name}",
                "bytes": len(block),
                "sha256": hashlib.sha256(block).hexdigest(),
            })
            print(f"  分片 {name}: {human(len(block))}", flush=True)
            index += 1

    if not chunks:
        raise SystemExit(f"zip 为空: {zip_path}")
    return chunks


def build_package(src_dir: Path, out_dir: Path, work_dir: Path,
                  package_id: str, label: str) -> dict:
    print(f"[包 {package_id}]")
    zip_path = work_dir / f"{package_id}.zip"
    make_zip(src_dir, zip_path)

    total = zip_path.stat().st_size
    digest = sha256_file(zip_path)
    print(f"  整包 SHA-256: {digest}")

    chunks = split_zip(zip_path, out_dir, package_id)

    # 校验：分片大小之和 == 整包大小，否则客户端会拒绝
    assert sum(c["bytes"] for c in chunks) == total, "分片大小之和不等于整包"

    return {
        "id": package_id,
        "label": label,
        "bytes": total,
        "sha256": digest,
        "chunks": chunks,
    }


def upload_r2(out_dir: Path, bucket: str, account_id: str, access_key: str,
              secret_key: str) -> None:
    """用 boto3 上传到 R2（S3 兼容）。没装 boto3 就提示安装。"""
    try:
        import boto3
        from botocore.config import Config
    except ImportError:
        raise SystemExit(
            "上传需要 boto3：pip install boto3\n"
            "（不上传可去掉 --upload，产物在 dist/cdn/ 手动上传也行）"
        )

    endpoint = f"https://{account_id}.r2.cloudflarestorage.com"
    s3 = boto3.client(
        "s3",
        endpoint_url=endpoint,
        aws_access_key_id=access_key,
        aws_secret_access_key=secret_key,
        config=Config(signature_version="s3v4"),
        region_name="auto",
    )

    files = sorted(p for p in out_dir.rglob("*") if p.is_file())
    total = sum(p.stat().st_size for p in files)
    print(f"\n上传 {len(files)} 个文件 / {human(total)} -> r2://{bucket}")

    done = 0
    for path in files:
        key = path.relative_to(out_dir).as_posix()
        extra = {}
        if key.endswith(".json"):
            extra = {"ContentType": "application/json", "CacheControl": "no-cache"}
        else:
            # 分片内容不变，长缓存；断点续传依赖 Range（R2 原生支持）
            extra = {"ContentType": "application/octet-stream",
                     "CacheControl": "public, max-age=31536000, immutable"}
        s3.upload_file(str(path), bucket, key, ExtraArgs=extra)
        done += path.stat().st_size
        print(f"  {key}  ({done * 100 // max(total,1)}%)", flush=True)

    print("上传完成")


def main() -> int:
    ap = argparse.ArgumentParser(description="打包并发布游戏数据包")
    ap.add_argument("--game", help="游戏本体数据目录（会打成 game-data 包）")
    ap.add_argument("--online", help="联机数据目录（会打成 online-data 包）")
    ap.add_argument("--version", default="1", help="数据版本号，客户端据此判断是否重下")
    ap.add_argument("--out", default="dist/cdn", help="产物目录（默认 dist/cdn）")
    ap.add_argument("--work", default="dist/cdn-work", help="临时 zip 目录")
    ap.add_argument("--min-client", type=int, default=0,
                    help="要求的最低客户端 versionCode（0=不限）")
    ap.add_argument("--upload", action="store_true", help="上传到 Cloudflare R2")
    ap.add_argument("--bucket", default=os.environ.get("R2_BUCKET", ""))
    ap.add_argument("--account-id", default=os.environ.get("R2_ACCOUNT_ID", ""))
    ap.add_argument("--access-key", default=os.environ.get("R2_ACCESS_KEY_ID", ""))
    ap.add_argument("--secret-key", default=os.environ.get("R2_SECRET_ACCESS_KEY", ""))
    args = ap.parse_args()

    if not args.game and not args.online:
        ap.error("至少给一个 --game 或 --online")

    out_dir = Path(args.out)
    work_dir = Path(args.work)
    out_dir.mkdir(parents=True, exist_ok=True)
    work_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict = {
        "schema": MANIFEST_SCHEMA,
        "version": str(args.version),
    }
    if args.min_client > 0:
        manifest["minClientVersionCode"] = args.min_client

    if args.game:
        pkg = build_package(Path(args.game), out_dir, work_dir, "game-data", "游戏本体")
        manifest["game"] = {
            "label": "GTA:SA 游戏数据",
            "totalBytes": pkg["bytes"],
            "required": True,
            "packages": [pkg],
        }

    if args.online:
        pkg = build_package(Path(args.online), out_dir, work_dir, "online-data", "联机数据包")
        manifest["online"] = {
            "label": "SA-MP 联机数据",
            "totalBytes": pkg["bytes"],
            "required": True,
            "packages": [pkg],
        }

    manifest_path = out_dir / "manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    total = sum(g["totalBytes"] for g in manifest.values() if isinstance(g, dict) and "totalBytes" in g)
    print(f"\n清单已生成: {manifest_path}  (共 {human(total)})")

    if args.upload:
        missing = [k for k, v in {
            "bucket": args.bucket, "account-id": args.account_id,
            "access-key": args.access_key, "secret-key": args.secret_key,
        }.items() if not v]
        if missing:
            raise SystemExit("上传缺少参数：" + ", ".join(missing)
                             + "\n可用环境变量 R2_BUCKET / R2_ACCOUNT_ID / "
                               "R2_ACCESS_KEY_ID / R2_SECRET_ACCESS_KEY")
        upload_r2(out_dir, args.bucket, args.account_id, args.access_key, args.secret_key)
    else:
        print("未上传。产物可直接拖进 R2 控制台，或加 --upload 自动传。")
        print(f"上传后用这个地址配置客户端：")
        print(f"  SAMP_GAMEDATA_MANIFEST=https://<你的域名>/manifest.json")

    return 0


if __name__ == "__main__":
    sys.exit(main())
