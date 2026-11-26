#!/usr/bin/env python3
"""
Unified objective verification pipeline for SA-MP Mobile.

Only verifies 100% objective engineering, binary, symbol, and Android platform contracts
plus real executable test suites — zero fragile business-logic regexes.

Modes:
  python tests/check.py            # Fast gate (Stages 1-4: compliance, platform, ELF/537 symbols/JNI, APK tools)
  python tests/check.py --full     # Full gate (Stages 1-5: adds C++ native tests, Gradle JVM tests, gamedata roundtrip)
  python tests/check.py --release  # Pre-release gate (--full + --require-apk + --require-gtasa + Android Lint)
  python tests/check.py --stage <compliance|platform|abi|apk|native|jvm|gamedata|lint>
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# Ensure tests/ is importable regardless of current working directory
sys.path.insert(0, str(Path(__file__).resolve().parent))

from checks.apk_verify import run_apk_checks
from checks.binary_abi import run_binary_abi_checks
from checks.common import REPO_ROOT, Runner
from checks.compliance import run_compliance_checks
from checks.gamedata import run_gamedata_check
from checks.platform import run_platform_checks
from checks.runners import (
    run_android_lint,
    run_executable_suites,
    run_jvm_tests,
    run_native_tests,
)


def main() -> int:
    parser = argparse.ArgumentParser(description="Unified objective verification pipeline for SA-MP Mobile.")
    parser.add_argument(
        "--apk",
        default="app/build/outputs/apk/debug/samp.apk",
        help="Target APK path (default: app/build/outputs/apk/debug/samp.apk)",
    )
    parser.add_argument("--full", action="store_true", help="Run all 5 stages (including C++/JVM/gamedata tests)")
    parser.add_argument(
        "--release",
        action="store_true",
        help="Strict pre-release gate (--full + --require-apk + --require-gtasa + --lint)",
    )
    parser.add_argument("--lint", action="store_true", help="Also run official Android Lint (gradlew lintDebug)")
    parser.add_argument("--online", action="store_true", help="Allow Gradle online mode for lint dependency download")
    parser.add_argument("--require-apk", action="store_true", help="Fail if APK artifact does not exist")
    parser.add_argument("--require-gtasa", action="store_true", help="Fail if local GTA:SA v2.10 binaries/assets are absent")
    parser.add_argument(
        "--stage",
        choices=("compliance", "platform", "abi", "apk", "native", "jvm", "gamedata", "lint"),
        help="Run a single verification stage only",
    )
    args = parser.parse_args()

    if args.release:
        args.full = True
        args.require_apk = True
        args.require_gtasa = True
        args.lint = True

    apk_path = Path(args.apk)
    if not apk_path.is_absolute():
        apk_path = REPO_ROOT / apk_path

    r = Runner()

    if args.stage:
        if args.stage == "compliance":
            run_compliance_checks(r)
        elif args.stage == "platform":
            run_platform_checks(r, require_gtasa=args.require_gtasa)
        elif args.stage == "abi":
            run_binary_abi_checks(r, require_gtasa=args.require_gtasa, apk_path=apk_path)
        elif args.stage == "apk":
            run_apk_checks(r, apk_path=apk_path, required=True, require_gtasa=args.require_gtasa)
        elif args.stage == "native":
            r.section("Stage 5a: Native C++ Host Tests")
            run_native_tests(r)
        elif args.stage == "jvm":
            r.section("Stage 5b: Gradle JVM Unit Tests")
            run_jvm_tests(r)
        elif args.stage == "gamedata":
            r.section("Stage 5c: Gamedata Publish Roundtrip")
            run_gamedata_check(r)
        elif args.stage == "lint":
            r.section("Stage 5d: Official Android Lint")
            run_android_lint(r, online=args.online)
    else:
        run_compliance_checks(r)
        run_platform_checks(r, require_gtasa=args.require_gtasa)
        run_binary_abi_checks(r, require_gtasa=args.require_gtasa, apk_path=apk_path)
        run_apk_checks(r, apk_path=apk_path, required=args.require_apk, require_gtasa=args.require_gtasa)
        if args.full:
            run_executable_suites(r, include_lint=args.lint, online=args.online)
        elif args.lint:
            r.section("Stage 5: Official Android Lint")
            run_android_lint(r, online=args.online)

    print(f"\nSummary: {r.passed} passed, {r.failed} failed, {r.warnings} warnings.")
    return 0 if r.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
