#!/usr/bin/env python3
"""tools/rom_stack.py (the boot ROM's stack budgets): the ROM table and the walk over shared code, on hand-made input
(no toolchain, no dump)."""

import struct
import sys
import unittest
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import rom_stack as rs  # noqa: E402


class Table(unittest.TestCase):
    def test_entries_and_their_hwords(self):
        rom = bytearray(rs.ROM_SIZE)
        struct.pack_into('<H', rom, 0x14, 0x100)
        # 'RE': RISC-V far (2 hwords) and Arm secure; 'GS': Arm secure and non-secure; then the end
        struct.pack_into('<HHHHH', rom, 0x100, rs.code_of(
            'RE'), 0x07, 0x1111, 0x2222, 0x0f15)
        struct.pack_into('<HHHH', rom, 0x10a, rs.code_of(
            'GS'), 0x14, 0x0bb1, 0x7e19)
        table = rs.rom_table(bytes(rom))
        self.assertEqual(table[rs.code_of('RE')][rs.FLAG_ARM_SEC], 0x0f15)
        self.assertEqual(table[rs.code_of('GS')][rs.FLAG_ARM_SEC], 0x0bb1)
        self.assertEqual(len(table), 2)


class Walk(unittest.TestCase):
    """Shared ROM code: an entry that sets a register and falls into the next, one that branches into another's body."""

    def code(self, lines):
        out = {}
        for i, (pc, op, rest) in enumerate(lines):
            nxt = lines[i + 1][0] if i + 1 < len(lines) else pc + 2
            out[pc] = (op, rest, nxt)
        return out

    def test_a_fall_and_a_branch_into_another_body(self):
        code = self.code([(0x808, 'movs', 'r1, #0x20'),
                          (0x80a, 'movs', 'r3, #0x0'), (0x80c, 'b', '0x814 <x>'),
                          (0x810, 'movs', 'r2, r1'),
                          (0x814, 'push', '{r4, lr}'), (0x816, 'pop', '{r4, pc}')])
        starts = {0x808, 0x80a, 0x810}
        end, calls, tails, falls, data = rs.walk(
            bytes(rs.ROM_SIZE), code, 0x808, starts)
        self.assertEqual((end, falls, tails), (0x80a, {0x80a}, set()))
        end, calls, tails, falls, data = rs.walk(
            bytes(rs.ROM_SIZE), code, 0x80a, starts)
        # past the next start (0x810): another function's body, a tail call there
        self.assertEqual((end, tails), (0x810, {0x814}))

    def test_a_literal_is_data(self):
        code = self.code(
            [(0x100, 'ldr', 'r0, [pc, #0x4]  @ 0x108'), (0x102, 'bx', 'lr')])
        _end, _calls, _tails, _falls, data = rs.walk(
            bytes(rs.ROM_SIZE), code, 0x100, {0x100})
        self.assertEqual(data, {(0x108, 0x10c)})


if __name__ == '__main__':
    unittest.main()
