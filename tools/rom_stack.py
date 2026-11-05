#!/usr/bin/env python3
"""The stack the RP2350's boot ROM functions take of their caller, measured on a dump of the ROM itself - for the
static stack bound's ROM budgets (chip_rp2350/stack_bound.txt `rom <fn> <bytes>`, kvasir_sdk/cmake/tools/stack_bound.py).

    rom_stack.py <rom.bin> [--codes IF,EX,...] [--elf <out.elf>] [--json <out.json>]

No document gives the ROM functions' stack use (datasheet 5.4 gives none for the Arm ones), and a byte-identical
build of the published source (pico-bootrom-rp2350) needs the exact compiler it was built with. The ROM's own bytes
need neither: read one over SWD (through a uc_log printer, Kvasir_SDK kvasir_bench_lib `Session.read_bytes(0, 0x8000)`,
or J-Link `savebin rom.bin 0x0 0x8000`) and this

  * reads the ROM table (the 16-bit pointer at 0x14; rom_table.S: a 2-char code, a flags hword, one hword per flag
    bit set, LSB first) for the Arm-secure entry of every code asked for, and the lookup function (0x16);
  * makes an ELF of the dump with a function symbol at every entry, then at every call and tail-call target its
    code reaches, until there are no new ones, and marks the data in the code - the literal words loads read
    (ldr/ldrd [pc]) and the tbb/tbh tables - so the disassembler never takes them for instructions;
  * runs stack_bound.py's own reading over that ELF: every function's frame from its code (push, sub sp, ...: the
    `~` estimate the tool uses for assembly), its calls, the longest path from each entry - and every call it cannot
    follow, which makes that entry a LOWER BOUND, printed with the reason.

The numbers hold for the ROM the dump came from (A2, A3 and A4 differ; the report names the dump's version byte
(0x13) and SHA-256): the chip's notes record them with both, and are re-measured for another revision. The stack bound
tool comes from Kvasir_SDK (sdk_tools(): $KVASIR_ROOT, the sibling checkout, or a shallow clone in .kvasir_sdk/), and
llvm-objdump and llvm-objcopy (or arm-none-eabi-objcopy) from PATH.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True
REPO = Path(__file__).resolve().parents[1]
SDK_URL = 'https://github.com/kvasir-io/Kvasir_SDK.git'


def sdk_tools():
    """Kvasir_SDK's cmake/tools (stack_bound.py): $KVASIR_ROOT, else the sibling checkout, else a shallow clone of
    master in .kvasir_sdk/ (gitignored) - as the firmware justfiles find the SDK's tools."""
    candidates = [Path(os.environ['KVASIR_ROOT'])
                  ] if os.environ.get('KVASIR_ROOT') else []
    candidates += [REPO.parent / 'Kvasir_SDK', REPO / '.kvasir_sdk']
    for root in candidates:
        if (root / 'cmake' / 'tools' / 'stack_bound.py').is_file():
            return root / 'cmake' / 'tools'
    clone = REPO / '.kvasir_sdk'
    print(
        f'rom_stack: no Kvasir_SDK found, cloning {SDK_URL} into {clone}', file=sys.stderr)
    subprocess.run(['git', 'clone', '--quiet', '--depth',
                   '1', SDK_URL, str(clone)], check=True)
    return clone / 'cmake' / 'tools'


sys.path.insert(0, str(sdk_tools()))
import stack_bound as sb  # noqa: E402

ROM_SIZE = 0x8000
FLAG_ARM_SEC = 0x04
# every code Kvasir's chip code looks up (rp_common Rom.hpp callers) and the XIP setup's ROM function (XM: the
# BOOTRAM stub XipControl.hpp copies tail-calls flash_select_xip_read_mode)
DEFAULT_CODES = 'IF,EX,FC,RE,RP,EB,LP,FA,FO,GP,GS,OA,RB,XM'
CPU = 'cortex-m33'
RX_LIT = re.compile(r'@ 0x([0-9a-f]+)')
RX_TB = re.compile(r'^\[pc, (r\d+)(, lsl #(0x)?1)?\]$')


def rom_table(rom):
    """{code: {flag bit: hword}} of the ROM table."""
    at = struct.unpack_from('<H', rom, 0x14)[0]
    out = {}
    while at + 4 <= len(rom):
        code, flags = struct.unpack_from('<HH', rom, at)
        if code == 0:
            break
        bits = [1 << b for b in range(16) if flags & (1 << b)]
        values = struct.unpack_from(f'<{len(bits)}H', rom, at + 4)
        out[code] = dict(zip(bits, values))
        at += 4 + 2 * len(bits)
    return out


def code_of(name):
    return ord(name[0]) | (ord(name[1]) << 8)


def objcopy():
    for tool in ('llvm-objcopy', 'arm-none-eabi-objcopy'):
        if shutil.which(tool):
            return tool
    raise FileNotFoundError('no llvm-objcopy or arm-none-eabi-objcopy in PATH')


def make_elf(rom_path, elf, starts, data):
    """The dump as an ELF: .text at 0, a FUNC symbol per start, mapping symbols for the data ranges."""
    syms = []
    for a in sorted(starts):
        syms += ['--add-symbol',
                 f'rom_{a:04x}=.text:{a | 1:#x},function,global']
    marks = {0: 't'}
    for lo, hi in sorted(data):
        marks[lo] = 'd'
        if not any(l2 <= hi < h2 for l2, h2 in data):
            marks.setdefault(hi, 't')
    for a, kind in sorted(marks.items()):
        syms += ['--add-symbol', f'${kind}.{a:x}=.text:{a:#x},local']
    # first the section (objcopy's _binary_* labels stripped: they would be function starts), then the symbols in it
    plain = Path(elf).with_suffix('.plain.elf')
    subprocess.run([objcopy(), '-I', 'binary', '-O', 'elf32-littlearm', '--rename-section',
                    '.data=.text,alloc,load,readonly,code', '--strip-all', str(rom_path), str(plain)], check=True)
    subprocess.run([objcopy(), *syms, str(plain), str(elf)], check=True)
    # mapping symbols are `$t` / `$d` exactly: the suffix keeps the names apart for objcopy, strip it again
    raw = bytearray(Path(elf).read_bytes())
    for a, kind in marks.items():
        name = f'${kind}.{a:x}'.encode()
        i = raw.find(name + b'\0')
        while i >= 0:
            raw[i + 2:i + len(name)] = b'\0' * (len(name) - 2)
            i = raw.find(name + b'\0', i + 1)
    Path(elf).write_bytes(bytes(raw))


def disassemble(elf):
    """{pc: (op, rest, next pc)} of the whole dump (data lines left out)."""
    out = subprocess.run(['llvm-objdump', '-d', '--no-show-raw-insn', '--print-imm-hex', f'--mcpu={CPU}', str(elf)],
                         check=True, capture_output=True, text=True).stdout
    lines = [(int(m.group(1), 16), m.group(2), m.group(3))
             for m in map(sb.RX_INSN.match, out.splitlines()) if m]
    code = {}
    for i, (pc, op, rest) in enumerate(lines):
        if not sb.data_line(op, rest):
            nxt = lines[i + 1][0] if i + 1 < len(lines) else pc + 2
            code[pc] = (op, rest, nxt if nxt - pc in (2, 4)
                        else pc + (4 if '.w' in op else 2))
    return code


def returns(op, args):
    regs = sb.register_list(args)
    return (op.startswith('bx') and args == 'lr') or ((sb.RX_POP.match(op) or op.startswith('ldm')) and 'pc' in regs) \
        or (op.startswith('ldr') and args.startswith('pc') and ']' in args and not sb.RX_JUMP_TABLE.match(args))


def walk(rom, code, start, starts):
    """What the function at `start` reaches: (end of its code, calls, tail calls, falls, data ranges). Its code ends
    where the next function starts: ROM functions share code (an entry that sets a register and falls into the next,
    one that branches into another's body), and what lies beyond is another function - a branch there, or before its
    own start, is a tail call (to a new function at the target), running into it a fall (an edge stack_bound.py does
    not see by itself). Branches inside are followed; a return and an indirect jump end a way; a table switch
    (tbb/tbh) goes to every entry of its table."""
    limit = min((x for x in starts if x > start), default=ROM_SIZE)
    seen, todo, calls, tails, falls, data = set(
    ), [start], set(), set(), set(), set()
    while todo:
        pc = todo.pop()
        while pc not in seen and pc in code and pc < ROM_SIZE:
            if pc >= limit:
                falls.add(pc)
                break
            seen.add(pc)
            op, rest, nxt = code[pc]
            args = rest.split('@', 1)[0].strip()
            t = sb.RX_TARGET.search(args)
            if op.startswith('ldr') and '[pc' in args and (lit := RX_LIT.search(rest)):
                a = int(lit.group(1), 16) & ~3
                data.add((a, a + (8 if op.startswith('ldrd') else 4)))
            if (sb.RX_BL.match(op) or sb.RX_BLX.match(op)) and t and not args.startswith(('r', 'lr')):
                calls.add(int(t.group(1), 16) & ~1)
            elif (sb.RX_B.match(op) or sb.RX_CB.match(op)) and t:
                target = int(t.group(1), 16) & ~1
                if target < start or target >= limit:
                    tails.add(target)
                else:
                    todo.append(target)
                if op in ('b', 'b.n', 'b.w'):
                    break
            elif (m := RX_TB.match(args) if op.startswith(('tbb', 'tbh')) else None):
                half = op.startswith('tbh')
                table, size, first, end = pc + 4, 2 if half else 1, None, pc + 4
                while end < ROM_SIZE and (first is None or end < first):
                    off = struct.unpack_from(
                        '<H' if half else '<B', rom, end)[0]
                    target = table + 2 * off
                    if start <= target < limit:   # a case of this function; beyond: another's
                        todo.append(target)
                    else:
                        tails.add(target)
                    first = target if first is None else min(first, target)
                    end += size
                data.add((table, (end + 1) & ~1))
                break
            elif returns(op, args) or ((sb.RX_BX.match(op) or op.startswith('mov') and args.startswith('pc'))
                                       and not args.startswith('lr')):
                break
            pc = nxt
    end = min(max((code[p][2] for p in seen), default=start + 2), limit)
    return end, calls, tails, falls, data


def discover(rom, rom_path, entries, work):
    """Function starts, their ends and the data ranges, grown until the code reaches nothing new."""
    starts, data = set(entries), set()
    for _ in range(64):
        elf = work / 'rom.elf'
        make_elf(rom_path, elf, starts, data)
        code = disassemble(elf)
        new_starts, new_data, ends, fall_edges = set(), set(), {}, {}
        for s in sorted(starts):
            end, calls, tails, falls, d = walk(rom, code, s, starts)
            ends[s] = end
            fall_edges[s] = falls
            new_starts |= {c for c in calls | tails | falls if c < ROM_SIZE}
            new_data |= d
        new_starts -= starts
        new_data -= data
        if not new_starts and not new_data:
            return starts, data, elf, ends, fall_edges
        starts |= new_starts
        data |= new_data
    raise RuntimeError(
        'rom_stack: the function set did not settle in 64 passes')


class RomImage(sb.Image):
    """stack_bound.Image over the dump's ELF, each function ending where its code does (walk)."""
    ends = {}

    def _functions(self):
        super()._functions()
        for a, f in self.funcs.items():
            if a in self.ends:
                f.size = self.ends[a] - a


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path)
    ap.add_argument('--codes', default=DEFAULT_CODES)
    ap.add_argument('--elf', type=Path, help='keep the ELF made of the dump')
    ap.add_argument('--json', type=Path)
    a = ap.parse_args(argv)
    rom = a.rom.read_bytes()
    if len(rom) != ROM_SIZE:
        print(
            f'rom_stack: {a.rom}: {len(rom)} bytes, not a {ROM_SIZE}-byte ROM dump', file=sys.stderr)
        return 2
    table = rom_table(rom)
    entries = {'lookup': struct.unpack_from('<H', rom, 0x16)[0] & ~1}
    for c in a.codes.split(','):
        ptr = table.get(code_of(c), {}).get(FLAG_ARM_SEC)
        if ptr is None:
            print(
                f'rom_stack: no Arm-secure entry for {c!r} in the ROM table', file=sys.stderr)
            return 2
        entries[c] = ptr & ~1
    with tempfile.TemporaryDirectory() as d:
        starts, data, elf, ends, falls = discover(
            rom, a.rom, set(entries.values()), Path(d))
        sb.disassembly_cpu = lambda _attributes: CPU
        RomImage.ends = ends
        image = RomImage(elf, notes=sb.Notes(), objdump='llvm-objdump')
        for a_from, targets in falls.items():
            image.funcs[a_from].calls |= targets
        image.solve()
        if a.elf:
            shutil.copy(elf, a.elf)
    report = {'rom': str(a.rom), 'sha256': hashlib.sha256(rom).hexdigest(), 'rom_version': rom[0x13],
              'functions': len(starts), 'data_ranges': len(data), 'entries': {}}
    print(
        f'{a.rom}: ROM version {rom[0x13]}, sha256 {report["sha256"]}, {len(starts)} functions reached')
    for name, addr in entries.items():
        why = sorted(image.reasons[addr])
        report['entries'][name] = {'address': addr, 'depth': image.depth[addr], 'lower': sb.lower(why),
                                   'reasons': why, 'path': image.chain(addr)}
        print(f'  {name:7} {addr:#06x} {image.depth[addr]:5} B' + (
            ' LOWER BOUND' if sb.lower(why) else ''))
        for w in why:
            if w.startswith('!'):
                print(f'          {w}')
    if a.json:
        a.json.write_text(json.dumps(report, indent=1) + '\n')
    return 1 if any(e['lower'] for e in report['entries'].values()) else 0


if __name__ == '__main__':
    sys.exit(main())
