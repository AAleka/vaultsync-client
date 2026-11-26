#!/usr/bin/env python3
"""Static analysis with CodeChecker (clangsa + clang-tidy).

Fails only on HIGH or CRITICAL findings. Rules live in .clang-tidy.

    python scripts/analyze.py

Needs on PATH: cmake, ninja, clang, clang-tidy. If CodeChecker isn't installed,
it is pip-installed into build/analyze-venv on first run.

Full report of everything (all severities): build/analyze/html/index.html
"""

import json
import os
import re
import shutil
import subprocess
import sys
import venv
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build" / "analyze"
REPORTS = BUILD / "reports"
VENV = ROOT / "build" / "analyze-venv"
FAIL_SEVERITIES = {"HIGH", "CRITICAL"}
# Only product code is analyzed: third_party isn't ours and tests/ is test-only.
# CodeChecker's skipfile doesn't reliably match Windows paths, so these are
# also filtered out of the results below.
SKIP = re.compile(r"[\\/](third_party|tests)[\\/]")


def run(*cmd, check=True):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], cwd=ROOT, check=check)


def codechecker():
    """Path to CodeChecker, installing it into a local venv if needed."""
    found = shutil.which("CodeChecker")
    if found:
        return found

    bindir = VENV / ("Scripts" if os.name == "nt" else "bin")
    exe = bindir / ("CodeChecker.exe" if os.name == "nt" else "CodeChecker")
    if not exe.exists():
        print(f"CodeChecker not found, installing into {VENV}", flush=True)
        venv.create(VENV, with_pip=True)
        run(bindir / "python", "-m", "pip", "install", "--quiet", "codechecker")
    return exe


def main():
    missing = [t for t in ("cmake", "ninja", "clang", "clang-tidy") if not shutil.which(t)]
    if missing:
        sys.exit(f"missing on PATH: {', '.join(missing)}")

    cc = codechecker()

    run("cmake", "--preset", "analyze")

    skipfile = BUILD / "skipfile"
    skipfile.write_text("-*/third_party/*\n-*/tests/*\n")

    # analyze exits non-zero whenever it reports anything, so don't check it;
    # a missing report directory is the real failure.
    run(cc, "analyze", BUILD / "compile_commands.json",
        "--analyzers", "clangsa", "clang-tidy",
        "--skip", skipfile, "-o", REPORTS, "--clean", check=False)
    if not REPORTS.is_dir():
        sys.exit("CodeChecker produced no reports")

    out_json = BUILD / "reports.json"
    for fmt, dest in (("html", BUILD / "html"), ("json", out_json)):
        subprocess.run([str(cc), "parse", str(REPORTS), "-e", fmt, "-o", str(dest)],
                       cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    reports = [r for r in json.loads(out_json.read_text())["reports"]
               if not SKIP.search(r["file"]["path"])]
    bad = [r for r in reports if r["severity"] in FAIL_SEVERITIES]

    for r in bad:
        path = os.path.relpath(r["file"]["path"], ROOT)
        print(f"{path}:{r['line']}:{r['column']}: [{r['severity']}] "
              f"{r['message']} ({r['checker_name']})")
    print(f"CodeChecker: {len(bad)} HIGH/CRITICAL, {len(reports)} total findings "
          f"(see {os.path.relpath(BUILD / 'html' / 'index.html', ROOT)})")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
