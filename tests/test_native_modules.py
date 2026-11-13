#!/usr/bin/env python3
"""N2: GFOpenLayout's host-native module builds and platform folders."""
from __future__ import annotations

import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "gfosmodules" / "build-native.sh"
MODULE = "gfopenlayout.library"


def elf_identity(path: Path) -> tuple[int, int, int, int]:
    header = path.read_bytes()[:64]
    if len(header) < 20 or header[:4] != b"\x7fELF":
        raise AssertionError(f"{path}: not an ELF file")
    endian = {1: "<", 2: ">"}.get(header[5])
    if endian is None:
        raise AssertionError(f"{path}: bad ELF data encoding {header[5]}")
    e_type, e_machine = struct.unpack_from(endian + "HH", header, 16)
    return header[4], header[5], e_type, e_machine


class NativeModules(unittest.TestCase):
    def test_arm64_and_x86_64_module_tree(self) -> None:
        arm = shutil.which(os.environ.get("ARM64_CC", "aarch64-linux-gnu-gcc"))
        x86 = shutil.which(os.environ.get("X86_64_CC", "cc"))
        if not arm or not x86:
            self.skipTest("the x86-64 and AArch64 C compilers are both required")
        self.assertTrue(BUILD.is_file(), f"missing {BUILD.relative_to(ROOT)}")
        with tempfile.TemporaryDirectory(prefix="gfopenlayout-native-") as tmp:
            out = Path(tmp)
            env = {**os.environ, "ARM64_CC": arm, "X86_64_CC": x86}
            subprocess.run([str(BUILD), str(out)], cwd=ROOT, env=env, check=True)
            expected = {
                "arm64": (out / "arm64" / "default" / MODULE, 183),
                "x86_64": (out / "x86_64" / "default" / MODULE, 62),
            }
            self.assertEqual(
                sorted(str(p.relative_to(out)) for p in out.rglob("*") if p.is_file()),
                [f"{platform}/default/{MODULE}" for platform in sorted(expected)],
            )
            for platform, (module, machine) in expected.items():
                with self.subTest(platform=platform):
                    self.assertEqual(elf_identity(module), (2, 1, 3, machine))
                    symbols = subprocess.check_output(
                        ["nm", "-D", "--defined-only", str(module)], text=True
                    )
                    for name in (
                        "ol_document_new",
                        "gfcss_sheet_new",
                        "olui_new",
                        "oldraw_new",
                        "oldraw_scroll",
                    ):
                        self.assertRegex(symbols, rf"(?m)\b{name}$")


if __name__ == "__main__":
    unittest.main()
