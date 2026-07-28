#!/usr/bin/env python3
"""Validate and print the canonical release version from CMake."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


def fail(message: str) -> "NoReturn":
    print(f"release version error: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--tag",
        help="Optional Git tag to validate (for example: v1.0.6 or v1.0.6-rc.1)",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[2]
    cmake_file = repo_root / "CMakeLists.txt"
    cmake_text = cmake_file.read_text(encoding="utf-8")
    match = re.search(
        r"project\s*\(\s*xlsOne\s+VERSION\s+(\d+\.\d+\.\d+)",
        cmake_text,
        re.IGNORECASE | re.MULTILINE,
    )
    if not match:
        fail(f"cannot find project version in {cmake_file}")
    cmake_version = match.group(1)

    if args.tag:
        tag = args.tag.removeprefix("refs/tags/")
        tag_match = re.fullmatch(r"v(\d+\.\d+\.\d+)(?:[-+][0-9A-Za-z.-]+)?", tag)
        if not tag_match:
            fail(f"release tag must look like v1.2.3 or v1.2.3-rc.1, got {tag!r}")
        if tag_match.group(1) != cmake_version:
            fail(f"tag {tag!r} does not match project version {cmake_version}")

    print(cmake_version)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
