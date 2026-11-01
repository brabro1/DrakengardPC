"""Build an evidence-tagged function map for the Drakengard EE ELF.

Usage: python -I tools/funcmap.py <elf> <out_dir> [--sce-toml analyzer.toml]

Sources, strongest first (each function records which sources support it):
  F  .eh_frame FDE         exact start and length emitted by the compiler
  L  .gnu.linkonce.t.*     exact start and length, plus the mangled C++ name
  E  ELF entry point
  J  JAL target            some instruction calls this address directly
  T  J target              tail jump from a different function
  D  data pointer          a word in a data section points here (vtables, callbacks)
  M  materialized address  lui/addiu (or ori) pair in code builds this address
  G  gap fill              code in a hole not covered by the above, split at returns

Outputs:
  funcmap.csv       Name,Start,End,Size  (the format ps2_recomp reads as ghidra_output)
  funcmap_full.csv  the same plus evidence flags and mangled names
  funcmap_report.txt coverage and consistency statistics
"""
from __future__ import annotations

import argparse
import bisect
import os
import re
import struct
import sys
from dataclasses import dataclass, field


@dataclass
class Section:
    name: str
    type: int
    flags: int
    addr: int
    offset: int
    size: int


@dataclass
class Func:
    start: int
    end: int = 0  # exclusive, 0 = unknown
    evidence: set = field(default_factory=set)
    name: str = ""


class Elf:
    def __init__(self, path: str):
        with open(path, "rb") as f:
            self.data = f.read()
        d = self.data
        (self.e_type, self.e_machine, _, self.entry, self.phoff, self.shoff, _, _,
         self.phentsize, self.phnum, self.shentsize, self.shnum, self.shstrndx) = struct.unpack_from(
            "<HHIIIIIHHHHHH", d, 16)
        raw = [struct.unpack_from("<IIIIIIIIII", d, self.shoff + i * self.shentsize) for i in range(self.shnum)]
        strtab = raw[self.shstrndx]
        names = d[strtab[4]:strtab[4] + strtab[5]]
        self.sections = []
        for r in raw:
            end = names.find(b"\0", r[0])
            self.sections.append(Section(names[r[0]:end].decode("ascii", "replace"), r[1], r[2], r[3], r[4], r[5]))
        self.loads = []
        for i in range(self.phnum):
            p = struct.unpack_from("<IIIIIIII", d, self.phoff + i * self.phentsize)
            if p[0] == 1:
                self.loads.append(p)

    def section(self, name: str) -> Section:
        return next(s for s in self.sections if s.name == name)

    def read32(self, addr: int) -> int | None:
        for p in self.loads:
            _, off, vaddr, _, filesz, _, _, _ = p
            if vaddr <= addr < vaddr + filesz - 3:
                return struct.unpack_from("<I", self.data, off + addr - vaddr)[0]
        return None


def parse_eh_frame(elf: Elf, sec: Section) -> tuple[list[tuple[int, int]], dict]:
    """Return [(pc_begin, pc_range)] from .eh_frame plus parse statistics.

    The EE toolchain (GCC 2.9x) writes CIEs with the old "eh" augmentation and FDEs
    with absolute 32-bit pc_begin / pc_range fields.
    """
    d = elf.data[sec.offset:sec.offset + sec.size]
    off, fdes, cies, augs = 0, [], 0, {}
    cie_aug: dict[int, str] = {}
    while off + 4 <= len(d):
        length = struct.unpack_from("<I", d, off)[0]
        if length == 0:
            break
        if length == 0xFFFFFFFF:
            raise ValueError("64-bit DWARF in .eh_frame not expected")
        rec = off + 4
        cie_ptr = struct.unpack_from("<I", d, rec)[0]
        if cie_ptr == 0:
            cies += 1
            aug_end = d.index(b"\0", rec + 5)
            aug = d[rec + 5:aug_end].decode("ascii", "replace")
            augs[aug] = augs.get(aug, 0) + 1
            cie_aug[off] = aug
        else:
            cie_off = rec - cie_ptr
            aug = cie_aug.get(cie_off, "?")
            if aug not in ("", "eh"):
                raise ValueError(f"unsupported CIE augmentation {aug!r} at .eh_frame+0x{cie_off:x}")
            pc_begin, pc_range = struct.unpack_from("<II", d, rec + 4)
            fdes.append((pc_begin, pc_range))
        off = rec + length
    return fdes, {"cies": cies, "fdes": len(fdes), "augmentations": augs, "bytes_parsed": off}


def demangle_hint(mangled: str) -> str:
    # Readable enough for logs; full GCC 2.x demangling is not needed for the map.
    return mangled


def is_code_addr(addr: int, code_ranges: list[tuple[int, int]]) -> bool:
    return addr % 4 == 0 and any(lo <= addr < hi for lo, hi in code_ranges)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("out_dir")
    args = ap.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    elf = Elf(args.elf)

    # EE code lives in .text and the .gnu.linkonce.t.* sections. .vutext is VU
    # microcode (flagged AX by the linker) and must never be decoded as EE code.
    text = elf.section(".text")
    linkonce = [s for s in elf.sections if s.name.startswith(".gnu.linkonce.t.")]
    vutext = elf.section(".vutext")
    code_ranges = []
    for lo, hi in sorted([(text.addr, text.addr + text.size)] + [(s.addr, s.addr + s.size) for s in linkonce if s.size]):
        if code_ranges and lo <= code_ranges[-1][1] + 64:
            code_ranges[-1] = (code_ranges[-1][0], max(code_ranges[-1][1], hi))
        else:
            code_ranges.append((lo, hi))
    for lo, hi in code_ranges:
        assert hi <= vutext.addr or lo >= vutext.addr + vutext.size, "EE code overlaps .vutext"
    code_lo, code_hi = code_ranges[0][0], code_ranges[-1][1]

    funcs: dict[int, Func] = {}

    def add(start: int, ev: str, end: int = 0, name: str = "") -> Func:
        f = funcs.get(start)
        if f is None:
            f = funcs[start] = Func(start)
        f.evidence.add(ev)
        if end and not f.end:
            f.end = end
        if name and not f.name:
            f.name = name
        return f

    report: list[str] = []

    # F: .eh_frame
    fdes, eh_stats = parse_eh_frame(elf, elf.section(".eh_frame"))
    bad_fde = 0
    for begin, rng in fdes:
        if not is_code_addr(begin, code_ranges) or rng == 0 or rng % 4:
            bad_fde += 1
            continue
        add(begin, "F", begin + rng)
    report.append(f".eh_frame: {eh_stats}; FDEs outside EE code or malformed: {bad_fde}")

    # L: linkonce sections
    for s in linkonce:
        if s.size:
            add(s.addr, "L", s.addr + s.size, s.name[len(".gnu.linkonce.t."):])
    report.append(f"linkonce text sections: {len(linkonce)} (non-empty {sum(1 for s in linkonce if s.size)})")

    # E: entry
    add(elf.entry, "E")

    # Decode code words once.
    words = {}
    for lo, hi in code_ranges:
        for a in range(lo, hi, 4):
            w = elf.read32(a)
            if w is not None:
                words[a] = w

    # J/T: jump targets. M: lui + addiu/ori address materialization.
    jal_targets, j_targets, mat = set(), set(), set()
    last_lui: dict[int, tuple[int, int]] = {}
    for a, w in words.items():
        op = w >> 26
        if op in (2, 3):
            tgt = ((a + 4) & 0xF0000000) | ((w & 0x3FFFFFF) << 2)
            if is_code_addr(tgt, code_ranges):
                (jal_targets if op == 3 else j_targets).add((a, tgt))
        elif op == 0x0F:  # lui
            last_lui[(w >> 16) & 31] = (a, (w & 0xFFFF) << 16)
        elif op in (0x09, 0x0D):  # addiu / ori
            rs = (w >> 21) & 31
            if rs in last_lui and a - last_lui[rs][0] <= 32:
                imm = w & 0xFFFF
                val = (last_lui[rs][1] + (imm - 0x10000 if (op == 0x09 and imm & 0x8000) else imm)) & 0xFFFFFFFF
                if is_code_addr(val, code_ranges):
                    mat.add(val)
    for _, t in jal_targets:
        add(t, "J")

    # D: pointers in data sections into EE code.
    data_ptrs = set()
    for s in elf.sections:
        if s.type != 1 or not (s.flags & 2) or (s.flags & 4) or s.size < 4:
            continue  # allocated, non-exec PROGBITS only
        if s.name.startswith(".DVP") or s.name in (".vudata",):
            continue
        blob = elf.data[s.offset:s.offset + s.size]
        for i in range(0, len(blob) - 3, 4):
            v = struct.unpack_from("<I", blob, i)[0]
            if is_code_addr(v, code_ranges):
                data_ptrs.add(v)

    # Consistency: how do raw targets relate to compiler-provided (F/L) ranges?
    exact = sorted((f.start, f.end) for f in funcs.values() if f.end and ({"F", "L"} & f.evidence))
    starts = [s for s, _ in exact]

    def containing(addr: int) -> tuple[int, int] | None:
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and exact[i][0] <= addr < exact[i][1]:
            return exact[i]
        return None

    jal_inside = sorted({t for _, t in jal_targets if (c := containing(t)) and c[0] != t})
    report.append(f"JAL call sites: {len(jal_targets)}, distinct targets: {len({t for _, t in jal_targets})}, "
                  f"targets strictly inside an F/L range: {len(jal_inside)}")
    if jal_inside:
        report.append("  first JAL-inside targets: " + ", ".join(f"0x{t:08x}" for t in jal_inside[:20]))

    # T: a J whose target lies outside the jumping function's F/L range is a tail call.
    tail = 0
    for a, t in j_targets:
        src = containing(a)
        if src and not (src[0] <= t < src[1]):
            c = containing(t)
            if c is None or c[0] == t:
                add(t, "T")
                tail += 1
    report.append(f"J sites: {len(j_targets)}, tail-call targets accepted: {tail}")

    def is_terminator(w: int) -> bool:
        op = w >> 26
        if op == 0 and (w & 0x3F) == 0x08:  # jr (returns and jump-table dispatch)
            return True
        if op == 2:  # j
            return True
        if op == 4 and ((w >> 21) & 31) == 0 and ((w >> 16) & 31) == 0:  # b == beq $zero,$zero
            return True
        return op == 0x10 and ((w >> 21) & 31) == 0x10 and (w & 0x3F) == 0x18  # eret

    exact_ends = {e for _, e in exact}

    def follows_function_end(addr: int) -> bool:
        """True when control cannot fall into addr from the preceding instruction."""
        if any(addr == lo for lo, _ in code_ranges) or addr in exact_ends:
            return True
        p = addr - 4
        while p in words and words[p] == 0:  # skip nop padding
            if p in exact_ends:
                return True
            p -= 4
        if p not in words:
            return True
        # p is the last non-zero word: either a delay slot after a terminator, or a
        # terminator whose (nop) delay slot was skipped as padding.
        return is_terminator(words.get(p - 4, 0)) or (addr - p >= 8 and is_terminator(words[p]))

    # D/M only count where they hit a known start, or uncovered code that begins
    # right after a function end. A data word that merely equals a code address in
    # the middle of straight-line code (seen at 0x142900) must not split a function.
    d_hits = m_hits = rejected = 0
    for ev, pool in (("D", data_ptrs), ("M", mat)):
        for addr in pool:
            c = containing(addr)
            if not (c is None or c[0] == addr):
                continue
            if c is None and addr not in funcs and not follows_function_end(addr):
                rejected += 1
                continue
            add(addr, ev)
            if ev == "D":
                d_hits += 1
            else:
                m_hits += 1
    report.append(f"data pointers into code: {len(data_ptrs)} (accepted {d_hits}); "
                  f"materialized code addresses: {len(mat)} (accepted {m_hits}); "
                  f"rejected mid-code starts: {rejected}")

    # Ends for functions without exact ranges: next known start, but never past an
    # enclosing exact range.
    def range_end(addr: int) -> int:
        return next(hi for lo, hi in code_ranges if lo <= addr < hi)

    ordered = sorted(funcs)
    for i, s in enumerate(ordered):
        f = funcs[s]
        if f.end:
            continue
        nxt = ordered[i + 1] if i + 1 < len(ordered) else code_hi
        f.end = min(nxt, range_end(s))

    # Gap report: code bytes not covered by any function range, per code range.
    gaps = []
    for lo, hi in code_ranges:
        cursor = lo
        for s in sorted(x for x in funcs if lo <= x < hi):
            if s > cursor:
                gaps.append((cursor, s))
            cursor = max(cursor, funcs[s].end)
        if cursor < hi:
            gaps.append((cursor, hi))

    # G: fill gaps that contain real instructions (not all zero) as their own functions.
    gap_funcs = 0
    nonzero_gaps = []
    for lo, hi in gaps:
        if any(words.get(a, 0) for a in range(lo, hi, 4)):
            # skip leading nop padding
            a = lo
            while a < hi and words.get(a, 0) == 0:
                a += 4
            if a < hi:
                nonzero_gaps.append((a, hi))
                add(a, "G", hi)
                gap_funcs += 1
    report.append(f"gaps between ranges: {len(gaps)}, gaps with code: {len(nonzero_gaps)} (added as G functions)")
    for lo, hi in nonzero_gaps[:40]:
        report.append(f"  gap code 0x{lo:08x}-0x{hi:08x} ({hi - lo} bytes)")

    # Overlap check (should be zero for a valid map).
    ordered = sorted(funcs)
    overlaps = []
    for a, b in zip(ordered, ordered[1:]):
        if funcs[a].end > b:
            overlaps.append((a, funcs[a].end, b))
    # Resolve overlaps by truncating the earlier function only when it lacks exact
    # (F/L) evidence; exact-vs-exact overlaps are reported, not hidden.
    exact_overlaps = 0
    for a, end, b in overlaps:
        fa, fb = funcs[a], funcs[b]
        if {"F", "L"} & fa.evidence and {"F", "L"} & fb.evidence:
            exact_overlaps += 1
        elif not ({"F", "L"} & fa.evidence):
            fa.end = b
    report.append(f"overlapping ranges: {len(overlaps)} (exact-vs-exact: {exact_overlaps})")

    total = sum(hi - lo for lo, hi in code_ranges)
    ev_counts: dict[str, int] = {}
    for f in funcs.values():
        key = "".join(sorted(f.evidence))
        ev_counts[key] = ev_counts.get(key, 0) + 1
    covered_exact = sum(f.end - f.start for f in funcs.values() if {"F", "L"} & f.evidence)
    report.insert(0, "EE code ranges " + ", ".join(f"0x{lo:08x}-0x{hi:08x}" for lo, hi in code_ranges)
                  + f" ({total} bytes), entry 0x{elf.entry:08x}; bytes covered by F/L ranges: "
                  f"{covered_exact} ({100 * covered_exact / total:.1f}%)")
    report.insert(1, f"functions: {len(funcs)}; with exact compiler ranges (F or L): "
                     f"{sum(1 for f in funcs.values() if {'F', 'L'} & f.evidence)}")
    report.append("evidence combinations: " + ", ".join(f"{k}={v}" for k, v in sorted(ev_counts.items(), key=lambda x: -x[1])))

    with open(os.path.join(args.out_dir, "funcmap.csv"), "w", newline="\n") as out:
        out.write("Name,Start,End,Size\n")
        for s in sorted(funcs):
            f = funcs[s]
            out.write(f"sub_{s:08x},0x{s:08X},0x{f.end:08X},{f.end - s}\n")
    with open(os.path.join(args.out_dir, "funcmap_full.csv"), "w", newline="\n") as out:
        out.write("Start,End,Size,Evidence,MangledName\n")
        for s in sorted(funcs):
            f = funcs[s]
            out.write(f"0x{s:08X},0x{f.end:08X},{f.end - s},{''.join(sorted(f.evidence))},{f.name}\n")
    with open(os.path.join(args.out_dir, "funcmap_report.txt"), "w") as out:
        out.write("\n".join(report) + "\n")
    print("\n".join(report))
    return 0


if __name__ == "__main__":
    sys.exit(main())
