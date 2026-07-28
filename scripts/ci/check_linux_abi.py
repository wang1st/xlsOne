#!/usr/bin/env python3
"""Reject Linux release packages that require a newer target ABI.

The pure C application still relies on the target distribution's libc.
Checking every ELF in the completed DEB also guards against future bundled
components or toolchain changes silently raising system-library requirements.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path


DEFAULT_LIMITS = {
    "GLIBC": "2.28",
    "GLIBCXX": "3.4.25",
    "CXXABI": "1.3.11",
}

VERSION_PATTERNS = {
    family: re.compile(rf"(?<![A-Z0-9_]){family}_(\d+(?:\.\d+)+)")
    for family in DEFAULT_LIMITS
}


def version_tuple(version: str) -> tuple[int, ...]:
    """Convert a numeric ELF symbol version into a comparable tuple."""
    return tuple(int(part) for part in version.split("."))


def _version_needs_text(readelf_output: str) -> str:
    """Return only GNU readelf's version-needs sections.

    Version definitions describe symbols provided by an ELF, not target ABI
    requirements.  Keeping the sections separate prevents a future bundled
    libc/libstdc++ from being rejected because of versions it defines itself.
    """
    needs_lines: list[str] = []
    in_needs = False
    for line in readelf_output.splitlines():
        if line.startswith("Version needs section "):
            in_needs = True
            continue
        if line.startswith("Version ") and " section " in line:
            in_needs = False
        if in_needs:
            needs_lines.append(line)
    return "\n".join(needs_lines)


def parse_symbol_versions(readelf_output: str) -> dict[str, set[str]]:
    """Return numeric GLIBC/GLIBCXX/CXXABI requirements from readelf output."""
    needs_text = _version_needs_text(readelf_output)
    return {
        family: set(pattern.findall(needs_text))
        for family, pattern in VERSION_PATTERNS.items()
    }


def is_elf(path: Path) -> bool:
    try:
        with path.open("rb") as stream:
            return stream.read(4) == b"\x7fELF"
    except OSError:
        return False


def inspect_elf(path: Path) -> dict[str, set[str]]:
    environment = os.environ.copy()
    environment["LC_ALL"] = "C"
    result = subprocess.run(
        ["readelf", "--wide", "--version-info", str(path)],
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        env=environment,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or "unknown readelf error"
        raise RuntimeError(f"cannot inspect {path}: {detail}")
    return parse_symbol_versions(result.stdout)


def validate_tree(root: Path, limits: dict[str, str]) -> list[str]:
    violations: list[str] = []
    maxima: dict[str, tuple[tuple[int, ...], str, Path]] = {}
    inspected = 0

    for path in sorted(root.rglob("*")):
        if not path.is_file() or not is_elf(path):
            continue
        inspected += 1
        versions = inspect_elf(path)
        relative = path.relative_to(root)
        for family, found_versions in versions.items():
            limit_tuple = version_tuple(limits[family])
            for version in found_versions:
                current = version_tuple(version)
                previous = maxima.get(family)
                if previous is None or current > previous[0]:
                    maxima[family] = (current, version, relative)
                if current > limit_tuple:
                    violations.append(
                        f"{relative}: requires {family}_{version} "
                        f"(maximum allowed: {family}_{limits[family]})"
                    )

    if inspected == 0:
        raise RuntimeError(f"no ELF files found under {root}")

    print(f"Inspected {inspected} ELF files")
    for family in DEFAULT_LIMITS:
        maximum = maxima.get(family)
        if maximum is None:
            print(f"  {family}: no numeric requirements")
        else:
            print(f"  {family}: {maximum[1]} ({maximum[2]})")
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Validate the libc/libstdc++ ABI floor of every ELF in a DEB"
    )
    parser.add_argument("deb", type=Path, help="DEB package to inspect")
    parser.add_argument("--max-glibc", default=DEFAULT_LIMITS["GLIBC"])
    parser.add_argument("--max-glibcxx", default=DEFAULT_LIMITS["GLIBCXX"])
    parser.add_argument("--max-cxxabi", default=DEFAULT_LIMITS["CXXABI"])
    args = parser.parse_args()

    if not args.deb.is_file():
        parser.error(f"DEB does not exist: {args.deb}")

    limits = {
        "GLIBC": args.max_glibc,
        "GLIBCXX": args.max_glibcxx,
        "CXXABI": args.max_cxxabi,
    }
    for family, version in limits.items():
        if not re.fullmatch(r"\d+(?:\.\d+)+", version):
            parser.error(f"invalid {family} version: {version!r}")

    with tempfile.TemporaryDirectory(prefix="xlsone-linux-abi-") as temp_dir:
        root = Path(temp_dir)
        extraction = subprocess.run(
            ["dpkg-deb", "-x", str(args.deb.resolve()), str(root)],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if extraction.returncode != 0:
            detail = extraction.stderr.strip() or "unknown dpkg-deb error"
            raise RuntimeError(f"cannot extract {args.deb}: {detail}")
        violations = validate_tree(root, limits)

    if violations:
        print("Linux ABI compatibility check failed:")
        for violation in sorted(violations):
            print(f"  - {violation}")
        return 1

    print(
        "Linux ABI compatibility check passed "
        f"(GLIBC <= {limits['GLIBC']}, "
        f"GLIBCXX <= {limits['GLIBCXX']}, "
        f"CXXABI <= {limits['CXXABI']})"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
