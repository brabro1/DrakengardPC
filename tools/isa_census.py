"""Instruction-class census of the EE code in an ELF, using a function map.

Usage: python -I tools/isa_census.py <elf> <funcmap_full.csv>

Reports how much of the R5900 ISA the game actually uses (MMI, COP1, COP2/VU0
macro mode, 128-bit loads/stores, COP0, cache ops), the kernel syscall numbers
it issues, and indirect jumps that are not plain returns (jump tables and
function-pointer calls), so the recompiler/runtime work can be scoped from
evidence instead of guesses.
"""
from __future__ import annotations

import collections
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from funcmap import Elf  # noqa: E402

PRIMARY = {
    0x00: "SPECIAL", 0x01: "REGIMM", 0x02: "J", 0x03: "JAL", 0x04: "BEQ", 0x05: "BNE", 0x06: "BLEZ", 0x07: "BGTZ",
    0x08: "ADDI", 0x09: "ADDIU", 0x0A: "SLTI", 0x0B: "SLTIU", 0x0C: "ANDI", 0x0D: "ORI", 0x0E: "XORI", 0x0F: "LUI",
    0x10: "COP0", 0x11: "COP1", 0x12: "COP2", 0x14: "BEQL", 0x15: "BNEL", 0x16: "BLEZL", 0x17: "BGTZL",
    0x18: "DADDI", 0x19: "DADDIU", 0x1A: "LDL", 0x1B: "LDR", 0x1C: "MMI", 0x1E: "LQ", 0x1F: "SQ",
    0x20: "LB", 0x21: "LH", 0x22: "LWL", 0x23: "LW", 0x24: "LBU", 0x25: "LHU", 0x26: "LWR", 0x27: "LWU",
    0x28: "SB", 0x29: "SH", 0x2A: "SWL", 0x2B: "SW", 0x2C: "SDL", 0x2D: "SDR", 0x2E: "SWR", 0x2F: "CACHE",
    0x31: "LWC1", 0x33: "PREF", 0x36: "LQC2", 0x37: "LD", 0x39: "SWC1", 0x3E: "SQC2", 0x3F: "SD",
}
SPECIAL = {0x08: "JR", 0x09: "JALR", 0x0C: "SYSCALL", 0x0D: "BREAK", 0x0F: "SYNC", 0x10: "MFHI", 0x12: "MFLO",
           0x18: "MULT", 0x19: "MULTU", 0x1A: "DIV", 0x1B: "DIVU", 0x28: "MFSA", 0x29: "MTSA", 0x0A: "MOVZ",
           0x0B: "MOVN"}


def main() -> int:
    elf = Elf(sys.argv[1])
    funcs = []
    with open(sys.argv[2]) as f:
        for row in csv.DictReader(f):
            funcs.append((int(row["Start"], 16), int(row["End"], 16)))

    prim = collections.Counter()
    special = collections.Counter()
    mmi = collections.Counter()
    cop2 = collections.Counter()
    cop0 = collections.Counter()
    syscalls = collections.Counter()
    jr_nonra = 0
    jalr = 0
    unknown_syscall = 0
    total = 0
    for start, end in funcs:
        last_v1 = None
        for a in range(start, end, 4):
            w = elf.read32(a)
            if w is None:
                continue
            total += 1
            op = w >> 26
            name = PRIMARY.get(op, f"op{op:02x}")
            prim[name] += 1
            rs, rt, rd, fn = (w >> 21) & 31, (w >> 16) & 31, (w >> 11) & 31, w & 63
            if op == 0x09 and rt == 3 and rs == 0:  # addiu v1, zero, imm
                imm = w & 0xFFFF
                last_v1 = imm - 0x10000 if imm & 0x8000 else imm
            elif op == 0x0D and rt == 3 and rs == 0:  # ori v1, zero, imm
                last_v1 = w & 0xFFFF
            if op == 0:
                sname = SPECIAL.get(fn, f"sp{fn:02x}")
                special[sname] += 1
                if fn == 0x08 and rs != 31:
                    jr_nonra += 1
                elif fn == 0x09:
                    jalr += 1
                elif fn == 0x0C:
                    if last_v1 is None:
                        unknown_syscall += 1
                    else:
                        syscalls[last_v1] += 1
            elif op == 0x1C:
                sub = fn
                if sub in (0x08, 0x09, 0x28, 0x29):  # MMI0..3 groups
                    mmi[f"MMI{[0x08, 0x28, 0x09, 0x29].index(sub)}.{(w >> 6) & 31:02x}"] += 1
                else:
                    mmi[f"MMI.{sub:02x}"] += 1
            elif op == 0x12:
                if rs & 0x10:
                    # COP2 macro-mode VU instruction: special1 when fn < 0x3C else special2
                    if fn >= 0x3C:
                        cop2[f"VU.sp2.{((w >> 6) & 31) * 4 + (fn & 3):03x}"] += 1
                    else:
                        cop2[f"VU.{fn:02x}"] += 1
                else:
                    cop2[{0x01: "QMFC2", 0x02: "CFC2", 0x05: "QMTC2", 0x06: "CTC2", 0x08: "BC2"}.get(rs, f"c2rs{rs:02x}")] += 1
            elif op == 0x10:
                if rs == 0x10:
                    cop0[{0x01: "TLBR", 0x02: "TLBWI", 0x06: "TLBWR", 0x08: "TLBP", 0x18: "ERET", 0x38: "EI", 0x39: "DI"}
                         .get(fn, f"c0fn{fn:02x}")] += 1
                else:
                    cop0[{0x00: f"MFC0 r{rd}", 0x04: f"MTC0 r{rd}", 0x08: "BC0"}.get(rs, f"c0rs{rs:02x}")] += 1

    print(f"instructions decoded in mapped functions: {total}")
    print("primary opcodes:", ", ".join(f"{k}={v}" for k, v in prim.most_common()))
    print("SPECIAL:", ", ".join(f"{k}={v}" for k, v in special.most_common()))
    print(f"MMI total {sum(mmi.values())}:", ", ".join(f"{k}={v}" for k, v in mmi.most_common(40)))
    print(f"COP2 total {sum(cop2.values())}:", ", ".join(f"{k}={v}" for k, v in cop2.most_common(60)))
    print(f"COP0 total {sum(cop0.values())}:", ", ".join(f"{k}={v}" for k, v in cop0.most_common()))
    print(f"indirect: JR (non-$ra) = {jr_nonra}, JALR = {jalr}")
    print(f"syscalls by $v1 (number=count): " + ", ".join(f"{k:#x}={v}" for k, v in sorted(syscalls.items())))
    print(f"syscalls with $v1 not set by a preceding immediate in the same function: {unknown_syscall}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
