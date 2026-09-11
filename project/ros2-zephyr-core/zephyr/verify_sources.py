#!/usr/bin/env python3
"""Verify that a vcstool tree contains exactly the pinned commits."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import yaml


def git(path: pathlib.Path, *args: str) -> str:
    return subprocess.check_output(
        ["git", "-C", str(path), *args], text=True, stderr=subprocess.DEVNULL
    ).strip()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=pathlib.Path)
    parser.add_argument("source_root", type=pathlib.Path)
    arguments = parser.parse_args()

    manifest = yaml.safe_load(arguments.manifest.read_text(encoding="utf-8"))
    failed = False
    for name, entry in manifest["repositories"].items():
        checkout = arguments.source_root / name
        expected = entry["version"]
        try:
            actual = git(checkout, "rev-parse", "HEAD")
        except (OSError, subprocess.CalledProcessError):
            print(f"missing checkout: {checkout}", file=sys.stderr)
            failed = True
            continue
        if actual != expected:
            print(
                f"revision mismatch: {checkout}: expected {expected}, found {actual}",
                file=sys.stderr,
            )
            failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
