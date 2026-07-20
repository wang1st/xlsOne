from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path
from unittest import mock


SCRIPT = Path(__file__).resolve().parents[1] / "check_linux_abi.py"
SPEC = importlib.util.spec_from_file_location("check_linux_abi", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CheckLinuxAbiTests(unittest.TestCase):
    def test_parse_symbol_versions_keeps_families_separate(self) -> None:
        output = """
Version symbols section '.gnu.version' contains 10 entries:
  000:   0 (*local*)

Version needs section '.gnu.version_r' contains 1 entry:
  Addr: 0x000000  Offset: 0x000000  Link: 0 (.dynstr)
          0x0010: Name: GLIBC_2.28  Flags: none  Version: 9
          0x0020: Name: GLIBC_2.34  Flags: none  Version: 8
          0x0030: Name: GLIBCXX_3.4.29  Flags: none  Version: 7
          0x0040: Name: CXXABI_1.3.13  Flags: none  Version: 6
          0x0050: Name: GLIBC_ABI_DT_RELR  Flags: none  Version: 5

Version definition section '.gnu.version_d' contains 1 entry:
          0x0060: Name: GLIBC_9.99  Flags: none  Version: 4
          0x0070: Name: GLIBCXX_9.99.99  Flags: none  Version: 3
        """
        self.assertEqual(
            MODULE.parse_symbol_versions(output),
            {
                "GLIBC": {"2.28", "2.34"},
                "GLIBCXX": {"3.4.29"},
                "CXXABI": {"1.3.13"},
            },
        )

    def test_numeric_versions_are_not_compared_lexicographically(self) -> None:
        self.assertGreater(
            MODULE.version_tuple("3.4.29"), MODULE.version_tuple("3.4.9")
        )
        self.assertLess(
            MODULE.version_tuple("1.3.9"), MODULE.version_tuple("1.3.11")
        )

    def test_validate_tree_allows_boundary_and_reports_newer_symbols(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "usr" / "bin" / "xlsOneQt"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"\x7fELFtest")

            requirements = {
                "GLIBC": {"2.28", "2.29"},
                "GLIBCXX": {"3.4.25", "3.4.26"},
                "CXXABI": {"1.3.11", "1.3.12"},
            }
            with mock.patch.object(
                MODULE, "inspect_elf", return_value=requirements
            ):
                violations = MODULE.validate_tree(
                    Path(temp_dir), MODULE.DEFAULT_LIMITS
                )

        self.assertEqual(
            violations,
            [
                f"{Path('usr/bin/xlsOneQt')}: requires GLIBC_2.29 "
                "(maximum allowed: GLIBC_2.28)",
                f"{Path('usr/bin/xlsOneQt')}: requires GLIBCXX_3.4.26 "
                "(maximum allowed: GLIBCXX_3.4.25)",
                f"{Path('usr/bin/xlsOneQt')}: requires CXXABI_1.3.12 "
                "(maximum allowed: CXXABI_1.3.11)",
            ],
        )


if __name__ == "__main__":
    unittest.main()
