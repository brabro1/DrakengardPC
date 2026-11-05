"""Invariant checks on the analysis pipeline outputs for the user's own disc.

Run: python -I -m unittest discover -s tests -v   (from the repository root)

Tests skip when work/ has not been generated (no game data in the repository). They
guard the properties the architecture relies on: the supported ELF identity, function
boundaries consistent with compiler-provided ranges, VU microcode never treated as EE
code, and a recompile with no undecoded instructions.
"""
from __future__ import annotations

import csv
import hashlib
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "work")
ELF = os.path.join(WORK, "disc", "SLUS_207.32")
sys.path.insert(0, os.path.join(ROOT, "tools"))


def need(path: str) -> None:
    if not os.path.exists(path):
        raise unittest.SkipTest(f"{os.path.relpath(path, ROOT)} not generated (run tools/recomp.py)")


class ElfIdentity(unittest.TestCase):
    def test_sha1_and_pcsx2_crc(self):
        need(ELF)
        with open(ELF, "rb") as f:
            data = f.read()
        self.assertEqual(hashlib.sha1(data).hexdigest(), "652d95f1cc7f788e219948d52fc21ba2fffaefaa")
        crc = 0
        for i in range(0, len(data) // 4 * 4, 4):
            crc ^= int.from_bytes(data[i:i + 4], "little")
        self.assertEqual(crc, 0x9679D44C)


class FunctionMap(unittest.TestCase):
    def setUp(self):
        need(os.path.join(WORK, "funcmap", "funcmap_report.txt"))
        with open(os.path.join(WORK, "funcmap", "funcmap_report.txt")) as f:
            self.report = f.read()
        with open(os.path.join(WORK, "recomp", "funcmap_named.csv")) as f:
            self.rows = list(csv.DictReader(f))

    def test_no_calls_into_exact_ranges(self):
        m = re.search(r"targets strictly inside an F/L range: (\d+)", self.report)
        self.assertIsNotNone(m)
        self.assertEqual(int(m.group(1)), 0)

    def test_no_overlaps(self):
        spans = sorted((int(r["Start"], 16), int(r["End"], 16)) for r in self.rows)
        for (a0, a1), (b0, _) in zip(spans, spans[1:]):
            self.assertLessEqual(a1, b0, f"0x{a0:08x}-0x{a1:08x} overlaps 0x{b0:08x}")

    def test_vutext_excluded(self):
        from funcmap import Elf
        need(ELF)
        vu = Elf(ELF).section(".vutext")
        for r in self.rows:
            s, e = int(r["Start"], 16), int(r["End"], 16)
            self.assertFalse(s < vu.addr + vu.size and e > vu.addr, f"{r['Name']} overlaps .vutext")


class Recompile(unittest.TestCase):
    def test_report_clean(self):
        log = os.path.join(WORK, "recomp", "recomp.log")
        need(log)
        with open(log, encoding="utf-8", errors="replace") as f:
            text = f.read()
        self.assertIn("decode failures: 0", text)
        self.assertIn("Unhandled instructions: 0", text)
        self.assertIn("Recompilation completed successfully", text)


class NativeStubs(unittest.TestCase):
    def test_libvu0_is_guest_code(self):
        """No libvu0 function (public or internal) may be bound to a native stub: the
        library's helpers pass values in VU0 registers that C-ABI stubs ignore."""
        cfg = os.path.join(WORK, "recomp", "drakengard.toml")
        need(cfg)
        with open(cfg, encoding="utf-8") as f:
            text = f.read()
        m = re.search(r"stubs\s*=\s*\[(.*?)\]", text, re.S)
        self.assertIsNotNone(m)
        stubs = re.findall(r'"([^"@]+)@', m.group(1))
        self.assertEqual([s for s in stubs if "sceVu0" in s], [])


class Sha1Parity(unittest.TestCase):
    """The host's verifier (src/game_identity.cpp) must agree with hashlib; this pins the
    expected digest of a fixed vector so a C++ change can be checked by the boot log."""

    def test_known_vector(self):
        self.assertEqual(hashlib.sha1(b"abc").hexdigest(), "a9993e364706816aba3e25717850c26c9cd0d89d")


if __name__ == "__main__":
    unittest.main()
