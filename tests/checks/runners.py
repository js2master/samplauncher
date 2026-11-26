"""Stage 5: Executable test suites (C++ native, Gradle JVM, Gamedata) & official Android Lint."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from .common import REPO_ROOT, Runner, find_compatible_java_home
from .gamedata import run_gamedata_check


def run_native_tests(r: Runner) -> None:
    if sys.platform == "win32":
        if not shutil.which("wsl"):
            r.warn("Native C++ host tests", "WSL not available on Windows")
            return
        cmd = ["wsl", "sh", "tests/native/run.sh"]
    else:
        cmd = ["sh", "tests/native/run.sh"]

    proc = subprocess.run(cmd, cwd=str(REPO_ROOT), capture_output=True, text=True, errors="ignore")
    out = (proc.stdout or "") + "\n" + (proc.stderr or "")
    m = re.search(r"(\d+)% tests passed,\s*(\d+) tests failed out of (\d+)", out)
    if proc.returncode == 0:
        detail = f"{m.group(3)}/{m.group(3)} passed" if m else "passed"
        r.ok("Native C++ host tests (tests/native)", detail)
    else:
        tail = "\n".join(out.strip().splitlines()[-12:])
        r.fail("Native C++ host tests (tests/native)", tail)


def _gradle_env() -> dict:
    env = os.environ.copy()
    java_home = find_compatible_java_home()
    if java_home:
        env["JAVA_HOME"] = java_home
    return env


def _gradlew_cmd(*args: str) -> list:
    if sys.platform == "win32":
        return [str(REPO_ROOT / "gradlew.bat"), *args]
    return ["sh", str(REPO_ROOT / "gradlew"), *args]


def run_jvm_tests(r: Runner) -> None:
    proc = subprocess.run(
        _gradlew_cmd("testDebugUnitTest", "--offline", "--console=plain"),
        cwd=str(REPO_ROOT),
        env=_gradle_env(),
        capture_output=True,
        text=True,
        errors="ignore",
    )
    if proc.returncode == 0:
        r.ok("Gradle JVM unit tests (testDebugUnitTest)", "passed")
    else:
        out = ((proc.stdout or "") + "\n" + (proc.stderr or "")).strip()
        r.fail("Gradle JVM unit tests (testDebugUnitTest)", "\n".join(out.splitlines()[-15:]))


def run_android_lint(r: Runner, online: bool = False) -> None:
    cmd = ["lintDebug", "--console=plain"] if online else ["lintDebug", "--offline", "--console=plain"]
    proc = subprocess.run(
        _gradlew_cmd(*cmd),
        cwd=str(REPO_ROOT),
        env=_gradle_env(),
        capture_output=True,
        text=True,
        errors="ignore",
    )
    report_xml = REPO_ROOT / "app/build/reports/lint-results-debug.xml"
    if not report_xml.is_file():
        out = ((proc.stdout or "") + "\n" + (proc.stderr or "")).strip()
        if not online and "available for offline mode" in out:
            r.warn(
                "Android Lint (lintDebug)",
                "lint dependencies not cached offline; run once with `python tests/check.py --stage lint --online`",
            )
            return
        if proc.returncode != 0:
            r.fail("Android Lint (lintDebug)", "\n".join(out.splitlines()[-12:]))
        else:
            r.warn("Android Lint (lintDebug)", "completed but lint-results-debug.xml not found")
        return

    tree = ET.parse(report_xml)
    root = tree.getroot()
    errors = []
    warnings = 0
    for issue in root.findall("issue"):
        sev = issue.attrib.get("severity", "")
        if sev in ("Fatal", "Error"):
            iid = issue.attrib.get("id", "?")
            loc = issue.find("location")
            fname = Path(loc.attrib["file"]).name if (loc is not None and "file" in loc.attrib) else "?"
            line = loc.attrib.get("line", "?") if loc is not None else "?"
            errors.append(f"{iid}@{fname}:{line}")
        elif sev == "Warning":
            warnings += 1

    if not errors:
        r.ok("Android Lint (lintDebug)", f"0 errors, {warnings} warnings (see app/build/reports/lint-results-debug.html)")
    else:
        sample = ", ".join(errors[:6]) + (f" (+{len(errors) - 6} more)" if len(errors) > 6 else "")
        r.warn("Android Lint (lintDebug)", f"{len(errors)} errors, {warnings} warnings: {sample}")


def run_executable_suites(r: Runner, include_lint: bool = False, online: bool = False) -> None:
    r.section("Stage 5: Executable Unit Tests, Gamedata Contract & Android Lint")
    run_native_tests(r)
    run_jvm_tests(r)
    run_gamedata_check(r)
    if include_lint:
        run_android_lint(r, online=online)
