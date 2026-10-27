#!/usr/bin/env python3
"""The RP2350 bench commands' parts that need no board: the decoders on register words read from boards, with the field
tables taken from this repository's chip.svd (what the bench hands them, Kvasir_SDK tools/kvasir_bench_lib/svd.py).

    python3 -B tools/test_kvasir_bench_chip.py -v
"""
import importlib.util
import re
import sys
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__ next to the imported tools

spec = importlib.util.spec_from_file_location(
    "kvasir_bench_chip", Path(__file__).resolve().parent / "kvasir_bench_chip.py")
assert spec is not None and spec.loader is not None
chip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chip)

SVD = ET.parse(Path(__file__).resolve().parent.parent / "chip.svd").getroot()


def svd_fields(peripheral: str, register: str) -> dict[str, tuple[int, int]]:
    """A register's fields in chip.svd: name -> (lsb, width), read as the bench reads them."""
    for p in SVD.iter("peripheral"):
        if p.findtext("name") != peripheral:
            continue
        for r in p.iter("register"):
            if r.findtext("name") != register:
                continue
            fields = {}
            for f in r.iter("field"):
                m = re.fullmatch(r"\[(\d+):(\d+)\]",
                                 f.findtext("bitRange") or "")
                if m:
                    fields[f.findtext("name")] = (
                        int(m.group(2)), int(m.group(1)) - int(m.group(2)) + 1)
                else:
                    fields[f.findtext("name")] = (
                        int(f.findtext("bitOffset"), 0), int(f.findtext("bitWidth"), 0))
            return fields
    raise KeyError(f"chip.svd has no {peripheral}.{register}")


def svd_address(peripheral: str, register: str) -> int:
    """A register's address in chip.svd: the peripheral's base plus the register's offset."""
    for p in SVD.iter("peripheral"):
        if p.findtext("name") != peripheral:
            continue
        for r in p.iter("register"):
            if r.findtext("name") == register:
                return int(p.findtext("baseAddress"), 0) + int(r.findtext("addressOffset"), 0)
    raise KeyError(f"chip.svd has no {peripheral}.{register}")


def mask(fields: dict[str, tuple[int, int]], *names: str) -> int:
    return sum(((1 << fields[n][1]) - 1) << fields[n][0] for n in names)


TIMING = svd_fields("QMI", "M0_TIMING")
RFMT = svd_fields("QMI", "M0_RFMT")


class QmiReadMode(unittest.TestCase):
    def test_the_bootroms_dual_io_mode(self):
        # read on the bench: an RP2350 board with QE clear after flashInit
        lines = chip.qmi_read_mode(
            0x40002202, 0x00009154, 0x000000BB, TIMING, RFMT)
        self.assertEqual(
            lines[0], "clkdiv 2, rxdelay 2, cooldown 1, max_select 0")
        self.assertEqual(lines[1], "command BBh (single) on every burst, address dual, "
                                   "8-bit mode byte 00h (dual), data dual")

    def test_continuous_quad_io(self):
        # XipReadMode::apply()'s continuous EBh
        lines = chip.qmi_read_mode(
            0x40002202, 0x000492A8 & ~(1 << 12), 0xA0EB, TIMING, RFMT)
        self.assertEqual(lines[1], "no command prefix (continuous read), address quad, "
                                   "8-bit mode byte A0h (quad), 16 dummy bits (quad), data quad")


DIRECT_CSR = svd_fields("QMI", "DIRECT_CSR")


class QmiState(unittest.TestCase):
    def test_the_stuck_qmi_read(self):
        # read after a probe reset during a flash write
        busy, lines = chip.qmi_state(0x80C10803, 0x030B0003, DIRECT_CSR)
        self.assertTrue(busy)
        self.assertEqual(
            lines[0], "QMI DIRECT_CSR 0x80c10803: BUSY 1, EN 1, stuck")
        self.assertEqual(lines[1], "core 0 in LOCKUP (DHCSR 0x030b0003)")

    def test_after_the_watchdog_reset(self):
        busy, lines = chip.qmi_state(0x00C10800, 0x01000000, DIRECT_CSR)
        self.assertFalse(busy)
        self.assertEqual(lines[1], "core 0 running (DHCSR 0x01000000)")


# chip.cmake's J-Link words are hex literals the printer writes and `qmi --recover` reads back (one source): here they
# are checked against chip.svd, address and bits.
# atomic set / clear aliases (RP2350 datasheet 2.1.3)
ALIAS_SET, ALIAS_CLR = 0x2000, 0x3000


class JLinkWords(unittest.TestCase):
    def test_the_stall_recovery(self):
        wdsel = svd_fields("PSM", "WDSEL")
        self.assertEqual(chip.jlink_words("_chip_jlink_stall_recovery"), [
            # no CoreResetGuard boot vector
            (svd_address("WATCHDOG", "SCRATCH4"), 0),
            (svd_address("PSM", "WDSEL"), mask(
                wdsel, *(n for n in wdsel if n != "PROC_COLD"))),
            (svd_address("WATCHDOG", "CTRL"), mask(
                svd_fields("WATCHDOG", "CTRL"), "TRIGGER")),
        ])

    def test_the_connect_words(self):
        frce_off = svd_address("PSM", "FRCE_OFF")
        proc1 = mask(svd_fields("PSM", "FRCE_OFF"), "PROC1")
        self.assertEqual(chip.jlink_words("_chip_jlink_connect"), [
            (frce_off + ALIAS_SET, proc1),
            (frce_off + ALIAS_CLR, proc1),
            (svd_address("WATCHDOG", "SCRATCH4"), 0),
        ])

    def test_a_missing_list(self):
        with self.assertRaises(ValueError):
            chip.jlink_words("_chip_jlink_stall_recovery",
                             'set(_chip_jlink_stall "0x400d0000,0x3,0x3")')


if __name__ == "__main__":
    unittest.main()
