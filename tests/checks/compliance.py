"""Stage 1: Objective legal compliance, secrets, and repository hygiene checks."""

from __future__ import annotations

import os
import re
from pathlib import Path
from typing import List

from .common import REPO_ROOT, Runner, read_text

FORBIDDEN_COMMERCIAL_FONTS = (
    "gotham",
    "museo_sans",
    "helvetica",
    "bradleyhand",
    "chaletcomprime",
)

PERSONAL_PATTERNS = (
    (re.compile(r"[A-Za-z]:\\{1,2}Users\\{1,2}(?!<YourUser>)[A-Za-z0-9._-]+", re.IGNORECASE), "hardcoded Windows user path"),
    (re.compile(r"/Users/(?!<YourUser>)[A-Za-z0-9._-]+/"), "hardcoded macOS user path"),
    (re.compile(r"/home/(?!<YourUser>)[A-Za-z0-9._-]+/"), "hardcoded Linux user path"),
    (re.compile(r"\badb(?:\.exe)?\s+-s\s+[A-Z0-9]{12,}\b"), "hardcoded ADB device serial"),
)

SKIP_DIRS = {
    ".git", ".gradle", "build", ".cxx", ".externalNativeBuild",
    ".idea", ".vscode", "dist", "reference", "captures",
    "Android", "GTA", "gamefiles", "__pycache__", "out", "Deps",
}

TEXT_EXTS = {
    ".java", ".cpp", ".cc", ".c", ".h", ".hpp", ".gradle", ".xml", ".toml",
    ".properties", ".md", ".txt", ".py", ".ps1", ".sh", ".bat", ".json", ".yml", ".yaml",
}


def run_compliance_checks(r: Runner) -> None:
    r.section("Stage 1: Compliance, Secrets & Repo Hygiene")

    for rel in ("LICENSE", "NOTICE", "third_party/licenses/THIRD_PARTY_NOTICES.md"):
        if (REPO_ROOT / rel).is_file():
            r.ok(f"Legal file present: {rel}")
        else:
            r.fail(f"Legal file present: {rel}", "missing required license/notice file")

    font_dir = REPO_ROOT / "app/src/main/res/font"
    bad_fonts: List[str] = []
    if font_dir.is_dir():
        for p in sorted(font_dir.iterdir()):
            low = p.name.lower()
            if any(tok in low for tok in FORBIDDEN_COMMERCIAL_FONTS):
                bad_fonts.append(p.name)
    if not bad_fonts:
        r.ok("Open-source fonts only in res/font/")
    else:
        r.fail("Open-source fonts only in res/font/", "commercial fonts found: " + ", ".join(bad_fonts))

    gitignore = read_text(".gitignore")
    required_ignores = (
        "app/src/main/jniLibs/*/libGTASA.so",
        "app/src/main/jniLibs/*/libImmEmulatorJ.so",
        "app/src/main/assets/*",
    )
    missing_ignores = [pat for pat in required_ignores if pat not in gitignore]
    if not missing_ignores:
        r.ok(".gitignore Rockstar exclusion rules")
    else:
        r.fail(".gitignore Rockstar exclusion rules", "missing: " + ", ".join(missing_ignores))

    gradle = read_text("app/build.gradle")
    if "123456" not in gradle:
        r.ok("No hardcoded keystore password in app/build.gradle")
    else:
        r.fail("No hardcoded keystore password in app/build.gradle", "found literal '123456'")

    forbidden_secrets = ("app/keystore.jks", "app/google-services.json")
    found_secrets = [p for p in forbidden_secrets if (REPO_ROOT / p).exists()]
    if not found_secrets:
        r.ok("No dev secrets in workspace", "keystore.jks / google-services.json absent")
    else:
        r.warn("Dev secret files present locally", ", ".join(found_secrets) + " (ensure not committed)")

    gradlew_bytes = (REPO_ROOT / "gradlew").read_bytes()
    if b"\r\n" not in gradlew_bytes:
        r.ok("gradlew line endings", "LF-only")
    else:
        r.fail("gradlew line endings", "CRLF detected in gradlew (breaks Linux/macOS builds)")

    hits: List[str] = []
    for root, dirs, files in os.walk(REPO_ROOT):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for fname in files:
            p = Path(root) / fname
            rel = p.relative_to(REPO_ROOT).as_posix()
            if rel in ("local.properties", "tests/checks/compliance.py"):
                continue
            if p.suffix.lower() not in TEXT_EXTS:
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="ignore")
            except OSError:
                continue
            for pat, label in PERSONAL_PATTERNS:
                if pat.search(text):
                    hits.append(f"{rel} ({label})")
                    break

    if not hits:
        r.ok("Repository path & serial hygiene", "no personal paths or ADB serials")
    else:
        r.fail("Repository path & serial hygiene", "; ".join(hits))
