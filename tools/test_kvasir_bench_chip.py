#!/usr/bin/env python3
"""The RP2350 bench commands' parts that need no board."""
import importlib.util
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__ next to the imported tools

spec = importlib.util.spec_from_file_location(
    "kvasir_bench_chip", Path(__file__).resolve().parent / "kvasir_bench_chip.py")
assert spec is not None and spec.loader is not None
chip = importlib.util.module_from_spec(spec)
spec.loader.exec_module(chip)

# RP2350 QMI M0_TIMING / M0_RFMT from chip_rp2350/chip.svd: field -> (lsb, width)
TIMING = {"COOLDOWN": (30, 2), "PAGEBREAK": (28, 2), "SELECT_SETUP": (25, 1), "SELECT_HOLD": (23, 2),
          "MAX_SELECT": (17, 6), "MIN_DESELECT": (12, 5), "RXDELAY": (8, 3), "CLKDIV": (0, 8)}
RFMT = {"DTR": (28, 1), "DUMMY_LEN": (16, 3), "SUFFIX_LEN": (14, 2), "PREFIX_LEN": (12, 1),
        "DATA_WIDTH": (8, 2), "DUMMY_WIDTH": (6, 2), "SUFFIX_WIDTH": (4, 2), "ADDR_WIDTH": (2, 2),
        "PREFIX_WIDTH": (0, 2)}


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


# QMI DIRECT_CSR from chip_rp2350/chip.svd: field -> (lsb, width)
DIRECT_CSR = {"BUSY": (1, 1), "EN": (0, 1)}


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


if __name__ == "__main__":
    unittest.main()
