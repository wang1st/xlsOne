#!/usr/bin/env python3
"""Prepare a validated z-pulse.cn deployment payload from Actions artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit, urlunsplit


DEFAULT_BASE_URL = "https://z-pulse.cn/downloads"
VERSION_PATTERN = re.compile(
    r"\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?",
    re.ASCII,
)
RELEASE_PACKAGE_SUFFIXES = {
    ".7z",
    ".appimage",
    ".deb",
    ".dmg",
    ".exe",
    ".gz",
    ".msi",
    ".pkg",
    ".rpm",
    ".tar",
    ".zip",
}


class PreparationError(ValueError):
    """Raised when the artifact set cannot produce an unambiguous release."""


def _validate_version(version: str) -> None:
    if not VERSION_PATTERN.fullmatch(version):
        raise PreparationError(
            f"version must look like 1.2.3 or 1.2.3-rc.1, got {version!r}"
        )


def _normalize_base_url(base_url: str) -> str:
    parsed = urlsplit(base_url)
    if parsed.scheme not in {"http", "https"} or not parsed.netloc:
        raise PreparationError(
            f"base URL must be an absolute HTTP(S) URL, got {base_url!r}"
        )
    if parsed.query or parsed.fragment:
        raise PreparationError("base URL must not contain a query string or fragment")

    path = parsed.path.rstrip("/")
    return urlunsplit((parsed.scheme, parsed.netloc, path, "", ""))


def _package_patterns(version: str) -> dict[str, re.Pattern[str]]:
    escaped_version = re.escape(version)
    return {
        "windows_msi": re.compile(
            rf"xlsone-{escaped_version}-windows-amd64\.msi", re.IGNORECASE
        ),
        "windows_zip": re.compile(
            rf"xlsone-{escaped_version}-windows-amd64\.zip", re.IGNORECASE
        ),
        "macos": re.compile(
            rf"xlsone-{escaped_version}-macos-universal\.dmg", re.IGNORECASE
        ),
        "linux_amd64": re.compile(
            rf"xlsone-{escaped_version}-linux-amd64\.deb", re.IGNORECASE
        ),
        "linux_arm64": re.compile(
            rf"xlsone-{escaped_version}-linux-arm64\.deb", re.IGNORECASE
        ),
    }


def _looks_like_release_package(path: Path) -> bool:
    return path.suffix.lower() in RELEASE_PACKAGE_SUFFIXES


def _discover_packages(artifacts_dir: Path, version: str) -> dict[str, Path]:
    if not artifacts_dir.is_dir():
        raise PreparationError(f"artifacts directory does not exist: {artifacts_dir}")

    patterns = _package_patterns(version)
    matches: dict[str, list[Path]] = {kind: [] for kind in patterns}
    unexpected: list[Path] = []

    candidates = sorted(
        (
            path
            for path in artifacts_dir.rglob("*")
            if path.is_file() and _looks_like_release_package(path)
        ),
        key=lambda path: str(path).casefold(),
    )
    for path in candidates:
        kinds = [
            kind for kind, pattern in patterns.items() if pattern.fullmatch(path.name)
        ]
        if len(kinds) == 1:
            matches[kinds[0]].append(path)
        else:
            unexpected.append(path)

    missing = [kind for kind, paths in matches.items() if not paths]
    duplicates = {kind: paths for kind, paths in matches.items() if len(paths) > 1}

    problems: list[str] = []
    if missing:
        problems.append("missing packages: " + ", ".join(sorted(missing)))
    if duplicates:
        details = ", ".join(
            f"{kind} ({len(paths)})" for kind, paths in sorted(duplicates.items())
        )
        problems.append("duplicate packages: " + details)
    if unexpected:
        relative_paths = [
            str(path.relative_to(artifacts_dir)).replace("\\", "/")
            for path in unexpected
        ]
        problems.append("unexpected release packages: " + ", ".join(relative_paths))
    if problems:
        raise PreparationError("; ".join(problems))

    return {kind: paths[0] for kind, paths in matches.items()}


def _canonical_names(version: str) -> dict[str, str]:
    return {
        "windows_msi": f"xlsone-{version}-windows-amd64.msi",
        "windows_zip": f"xlsone-{version}-windows-amd64.zip",
        "macos": f"xlsOne-{version}-macos-universal.dmg",
        "linux_amd64": f"xlsOne-{version}-linux-amd64.deb",
        "linux_arm64": f"xlsOne-{version}-linux-arm64.deb",
    }


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download_url(base_url: str, filename: str) -> str:
    url = f"{base_url}/{filename}"
    actual_basename = Path(unquote(urlsplit(url).path)).name
    if actual_basename != filename:
        raise PreparationError(
            f"download URL basename {actual_basename!r} does not match {filename!r}"
        )
    return url


def prepare_release(
    *,
    artifacts_dir: Path,
    output_dir: Path,
    version: str,
    changelog: str,
    base_url: str = DEFAULT_BASE_URL,
) -> dict[str, object]:
    """Validate Actions artifacts and write the z-pulse.cn deployment tree."""

    _validate_version(version)
    normalized_base_url = _normalize_base_url(base_url)
    artifacts_dir = artifacts_dir.resolve()
    output_dir = output_dir.resolve()

    packages = _discover_packages(artifacts_dir, version)
    canonical_names = _canonical_names(version)

    downloads_dir = output_dir / "downloads"
    api_dir = output_dir / "api"
    downloads_dir.mkdir(parents=True, exist_ok=True)
    api_dir.mkdir(parents=True, exist_ok=True)

    checksums: dict[str, str] = {}
    for kind, filename in canonical_names.items():
        destination = downloads_dir / filename
        shutil.copy2(packages[kind], destination)
        checksums[filename] = _sha256(destination)

    download_kinds = {
        "linux_arm64": "linux_arm64",
        "linux_amd64": "linux_amd64",
        "windows_amd64": "windows_msi",
        "windows_amd64_zip": "windows_zip",
        "macos": "macos",
    }
    downloads = {
        manifest_key: _download_url(
            normalized_base_url, canonical_names[package_kind]
        )
        for manifest_key, package_kind in download_kinds.items()
    }

    manifest: dict[str, object] = {
        "latest_version": version,
        "changelog": changelog,
        "downloads": downloads,
        "checksums": {
            filename: checksums[filename] for filename in sorted(checksums)
        },
    }
    (api_dir / "version.json").write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    checksum_lines = [
        f"{checksums[filename]}  {filename}" for filename in sorted(checksums)
    ]
    (downloads_dir / "checksums.txt").write_text(
        "\n".join(checksum_lines) + "\n",
        encoding="utf-8",
    )
    return manifest


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Prepare z-pulse.cn files from GitHub Actions artifacts."
    )
    parser.add_argument("--artifacts-dir", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--changelog", required=True)
    parser.add_argument("--base-url", default=DEFAULT_BASE_URL)
    return parser


def main() -> int:
    args = _build_parser().parse_args()
    try:
        prepare_release(
            artifacts_dir=args.artifacts_dir,
            output_dir=args.output_dir,
            version=args.version,
            changelog=args.changelog,
            base_url=args.base_url,
        )
    except (OSError, PreparationError) as exc:
        print(f"prepare z-pulse release error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
