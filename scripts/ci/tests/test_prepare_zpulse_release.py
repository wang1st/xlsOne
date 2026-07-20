from __future__ import annotations

import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path
from urllib.parse import unquote, urlsplit


CI_DIR = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CI_DIR))

import prepare_zpulse_release as release  # noqa: E402


VERSION = "1.2.3"
PACKAGE_LAYOUT = {
    "xlsone-windows-amd64": [
        f"xlsone-{VERSION}-windows-amd64.msi",
        f"xlsone-{VERSION}-windows-amd64.zip",
    ],
    "xlsone-macos-universal": [f"xlsOne-{VERSION}-macos-universal.dmg"],
    "xlsone-linux-amd64": [f"xlsone-{VERSION}-linux-amd64.deb"],
    "xlsone-linux-arm64": [f"xlsone-{VERSION}-linux-arm64.deb"],
}


class PrepareZPulseReleaseTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "output"
        self.payloads: dict[str, bytes] = {}
        self._create_complete_artifact_set()

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def _create_complete_artifact_set(self) -> None:
        for artifact_name, filenames in PACKAGE_LAYOUT.items():
            artifact_dir = self.artifacts / artifact_name
            artifact_dir.mkdir(parents=True, exist_ok=True)
            for filename in filenames:
                payload = f"payload for {filename}".encode("utf-8")
                (artifact_dir / filename).write_bytes(payload)
                self.payloads[filename] = payload
            (artifact_dir / f"SHA256SUMS-{artifact_name}.txt").write_text(
                "Actions sidecar checksum\n", encoding="utf-8"
            )

    def _prepare(self, **overrides: object) -> dict[str, object]:
        arguments: dict[str, object] = {
            "artifacts_dir": self.artifacts,
            "output_dir": self.output,
            "version": VERSION,
            "changelog": "修复发布流程",
        }
        arguments.update(overrides)
        return release.prepare_release(**arguments)  # type: ignore[arg-type]

    def test_prepares_canonical_tree_manifest_and_checksums(self) -> None:
        manifest = self._prepare()

        expected_names = {
            f"xlsOne-{VERSION}-linux-amd64.deb",
            f"xlsOne-{VERSION}-linux-arm64.deb",
            f"xlsOne-{VERSION}-macos-universal.dmg",
            f"xlsone-{VERSION}-windows-amd64.msi",
            f"xlsone-{VERSION}-windows-amd64.zip",
        }
        actual_names = {
            path.name
            for path in (self.output / "downloads").iterdir()
            if path.name != "checksums.txt"
        }
        self.assertEqual(actual_names, expected_names)

        written_manifest = json.loads(
            (self.output / "api" / "version.json").read_text(encoding="utf-8")
        )
        self.assertEqual(written_manifest, manifest)
        self.assertEqual(manifest["latest_version"], VERSION)
        self.assertEqual(manifest["changelog"], "修复发布流程")

        downloads = manifest["downloads"]
        self.assertIsInstance(downloads, dict)
        for url in downloads.values():
            filename = Path(unquote(urlsplit(url).path)).name
            self.assertIn(filename, expected_names)
            self.assertEqual(url, f"https://z-pulse.cn/downloads/{filename}")

        expected_hashes: dict[str, str] = {}
        for filename in expected_names:
            input_name = filename
            if filename.startswith("xlsOne-") and "-linux-" in filename:
                input_name = "xlsone-" + filename.removeprefix("xlsOne-")
            expected_hashes[filename] = hashlib.sha256(
                self.payloads[input_name]
            ).hexdigest()
        self.assertEqual(manifest["checksums"], dict(sorted(expected_hashes.items())))

        checksum_text = (self.output / "downloads" / "checksums.txt").read_text(
            encoding="utf-8"
        )
        self.assertEqual(
            checksum_text,
            "".join(
                f"{expected_hashes[name]}  {name}\n"
                for name in sorted(expected_hashes)
            ),
        )

    def test_custom_base_url_is_normalized(self) -> None:
        manifest = self._prepare(base_url="https://cdn.example.test/releases/")

        self.assertTrue(
            all(
                url.startswith("https://cdn.example.test/releases/")
                for url in manifest["downloads"].values()
            )
        )

    def test_rejects_missing_package_before_creating_output(self) -> None:
        next(self.artifacts.rglob("*.dmg")).unlink()

        with self.assertRaisesRegex(release.PreparationError, "missing.*macos"):
            self._prepare()

        self.assertFalse(self.output.exists())

    def test_rejects_duplicate_package(self) -> None:
        source = next(self.artifacts.rglob("*.msi"))
        duplicate_dir = self.artifacts / "duplicate"
        duplicate_dir.mkdir()
        (duplicate_dir / source.name).write_bytes(source.read_bytes())

        with self.assertRaisesRegex(
            release.PreparationError, "duplicate.*windows_msi"
        ):
            self._prepare()

    def test_rejects_duplicate_linux_arch_and_missing_other_arch(self) -> None:
        next(self.artifacts.rglob("*-linux-arm64.deb")).unlink()
        source = next(self.artifacts.rglob("*-linux-amd64.deb"))
        duplicate_dir = self.artifacts / "duplicate-linux"
        duplicate_dir.mkdir()
        (duplicate_dir / source.name).write_bytes(source.read_bytes())

        with self.assertRaises(release.PreparationError) as context:
            self._prepare()

        message = str(context.exception)
        self.assertIn("missing packages: linux_arm64", message)
        self.assertIn("duplicate packages: linux_amd64 (2)", message)

    def test_rejects_additional_release_package(self) -> None:
        (self.artifacts / "unexpected.pkg").write_bytes(b"unexpected")

        with self.assertRaisesRegex(
            release.PreparationError, "unexpected release packages.*unexpected.pkg"
        ):
            self._prepare()

    def test_rejects_wrong_version_package(self) -> None:
        (self.artifacts / "old-version.zip").write_bytes(b"old")

        with self.assertRaisesRegex(
            release.PreparationError, "unexpected release packages.*old-version.zip"
        ):
            self._prepare()

    def test_rejects_invalid_version_and_base_url(self) -> None:
        with self.assertRaisesRegex(release.PreparationError, "version must look"):
            self._prepare(version="../1.2.3")
        with self.assertRaisesRegex(release.PreparationError, "absolute HTTP"):
            self._prepare(base_url="/downloads")
        with self.assertRaisesRegex(release.PreparationError, "query string"):
            self._prepare(base_url="https://z-pulse.cn/downloads?channel=stable")


if __name__ == "__main__":
    unittest.main()
