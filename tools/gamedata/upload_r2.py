#!/usr/bin/env python3
"""
只上传已打包好的产物到 Cloudflare R2（不重新打包）。

打包（publish_gamedata.py）与上传分开，是因为 2GB 打包要几分钟，
而上传可能因网络中断重试多次 —— 不该每次都重打包一遍。

    python tools/gamedata/upload_r2.py --account-id XXX --bucket samp-mobile
    # 凭据从环境变量读，避免写进命令历史：
    set R2_ACCESS_KEY_ID=...        # Windows
    set R2_SECRET_ACCESS_KEY=...
    python tools/gamedata/upload_r2.py --account-id XXX --bucket samp-mobile

特性：
- 已存在且大小一致的对象跳过（R2 的 HeadObject 比上传便宜得多）
- 单文件失败重试 3 次，指数退避
- 按大小排序上传：先传小文件（清单最早就能用），大分片后续跟上
- 传完做一次抽样 HEAD 校验，确认远端 Content-Length 与本地一致
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import time
from pathlib import Path


def human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB", "TB"):
        if n < 1024:
            return f"{n:.2f} {unit}" if unit != "B" else f"{n} B"
        n /= 1024.0
    return f"{n:.2f} PB"


def make_client(account_id: str, access_key: str, secret_key: str):
    try:
        import boto3
        from botocore.config import Config
    except ImportError:
        raise SystemExit("需要 boto3：python -m pip install boto3")

    from boto3.s3.transfer import TransferConfig

    return boto3.client(
        "s3",
        endpoint_url=f"https://{account_id}.r2.cloudflarestorage.com",
        aws_access_key_id=access_key,
        aws_secret_access_key=secret_key,
        region_name="auto",
        config=Config(
            signature_version="s3v4",
            retries={"max_attempts": 3, "mode": "standard"},
            max_pool_connections=8,
        ),
    ), TransferConfig(
        # 16MB 分片：单块失败只重传该块，不整文件重来。
        # 2GB 文件约 135 块，配合并发 4，跨境线路也能跑满。
        multipart_threshold=16 * 1024 * 1024,
        multipart_chunksize=16 * 1024 * 1024,
        max_concurrency=4,
        use_threads=True,
    )


def remote_size(client, bucket: str, key: str) -> int | None:
    try:
        resp = client.head_object(Bucket=bucket, Key=key)
        return resp["ContentLength"]
    except Exception:
        return None


def upload_one(client, bucket: str, path: Path, key: str, xfer) -> bool:
    """上传单个文件，失败重试 3 次。"""
    size = path.stat().st_size

    existing = remote_size(client, bucket, key)
    if existing == size:
        print(f"  跳过（已存在且大小一致）: {key}")
        return True

    extra = {"ContentType": "application/json", "CacheControl": "no-cache"} \
        if key.endswith(".json") else \
        {"ContentType": "application/octet-stream",
         "CacheControl": "public, max-age=31536000, immutable"}

    for attempt in range(1, 4):
        try:
            client.upload_file(str(path), bucket, key, ExtraArgs=extra, Config=xfer)
            return True
        except Exception as e:
            print(f"  第 {attempt} 次失败 {key}: {e}")
            if attempt == 3:
                return False
            time.sleep(2 ** attempt)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="上传数据包到 Cloudflare R2")
    ap.add_argument("--dir", default="dist/cdn", help="产物目录（默认 dist/cdn）")
    ap.add_argument("--account-id", default=os.environ.get("R2_ACCOUNT_ID", ""))
    ap.add_argument("--bucket", default=os.environ.get("R2_BUCKET", ""))
    ap.add_argument("--access-key", default=os.environ.get("R2_ACCESS_KEY_ID", ""))
    ap.add_argument("--secret-key", default=os.environ.get("R2_SECRET_ACCESS_KEY", ""))
    ap.add_argument("--sample-check", type=int, default=5,
                    help="上传后抽样校验的个数（0=不校验）")
    args = ap.parse_args()

    root = Path(args.dir)
    if not (root / "manifest.json").is_file():
        raise SystemExit(f"{root} 里没有 manifest.json，先跑 publish_gamedata.py")

    missing = [n for n, v in {
        "account-id": args.account_id, "bucket": args.bucket,
        "access-key": args.access_key, "secret-key": args.secret_key,
    }.items() if not v]
    if missing:
        raise SystemExit(
            "缺少参数：" + ", ".join(missing) + "\n"
            "建议用环境变量传凭据（不进命令历史）：\n"
            "  export R2_ACCESS_KEY_ID=...\n"
            "  export R2_SECRET_ACCESS_KEY=...\n"
            "凭据在 Cloudflare 后台 -> R2 -> Manage R2 API Tokens 创建，\n"
            "权限选 Object Read & Write，并限定到目标 bucket。")

    client, xfer = make_client(args.account_id, args.access_key, args.secret_key)

    files = [p for p in root.rglob("*") if p.is_file()]
    # 清单最先传（小，且客户端靠它决定下什么），其余按大小升序
    files.sort(key=lambda p: (0 if p.name == "manifest.json" else 1, p.stat().st_size))

    total = sum(p.stat().st_size for p in files)
    print(f"准备上传 {len(files)} 个文件 / {human(total)} -> r2://{args.bucket}")

    done_bytes = 0
    failed: list[str] = []
    started = time.time()

    for i, path in enumerate(files, 1):
        key = path.relative_to(root).as_posix()
        size = path.stat().st_size
        ok = upload_one(client, args.bucket, path, key, xfer)
        if not ok:
            failed.append(key)
        done_bytes += size

        elapsed = max(time.time() - started, 0.001)
        speed = done_bytes / elapsed
        eta = (total - done_bytes) / speed if speed > 0 else 0
        print(f"  [{i}/{len(files)}] {key}  "
              f"{human(done_bytes)}/{human(total)}  "
              f"{human(speed)}/s  剩余 {int(eta)}s", flush=True)

    if failed:
        print(f"\n失败 {len(failed)} 个：")
        for k in failed:
            print("  " + k)
        return 1

    # 抽样校验：确认远端 Content-Length 与本地一致
    if args.sample_check > 0:
        print(f"\n抽样校验 {min(args.sample_check, len(files))} 个…")
        step = max(1, len(files) // args.sample_check)
        bad = 0
        for path in files[::step][:args.sample_check]:
            key = path.relative_to(root).as_posix()
            rs = remote_size(client, args.bucket, key)
            ls = path.stat().st_size
            ok = rs == ls
            if not ok:
                bad += 1
            print(f"  {'OK ' if ok else 'BAD'} {key} 远端={rs} 本地={ls}")
        if bad:
            print(f"\n{bad} 个对象大小不一致")
            return 1

    print(f"\n上传完成，用时 {int(time.time() - started)}s")
    print(f"清单地址：https://<你的自定义域>/manifest.json")
    print(f"（若还没绑域名，可临时用 R2 的 r2.dev 公开地址测试）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
