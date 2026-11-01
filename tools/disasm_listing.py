"""Build one address-sorted disassembly listing from ps2_recomp's generated code.

Usage: python -I tools/disasm_listing.py <generated_dir> <out.txt>

ps2_recomp writes each guest instruction as a comment (`// 0xADDR: 0xWORD  op ...`).
Collecting them gives a grep-able listing annotated with the owning function, which
is the quickest way to trace hangs reported by the runtime's `pc=` diagnostics.
"""
from __future__ import annotations

import os
import re
import sys

LINE = re.compile(r"^\s*// (0x[0-9a-f]+): (0x[0-9a-f]+)\s+(.*)$")
FUNC = re.compile(r"^// Function: (\S+)")


def main() -> int:
    gen, out_path = sys.argv[1], sys.argv[2]
    rows: dict[int, str] = {}
    for name in os.listdir(gen):
        if not name.endswith(".cpp"):
            continue
        func = ""
        with open(os.path.join(gen, name), encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC.match(line)
                if m:
                    func = m.group(1)
                    continue
                m = LINE.match(line)
                if m:
                    addr = int(m.group(1), 16)
                    rows.setdefault(addr, f"{addr:08x}  {int(m.group(2), 16):08x}  {m.group(3):<48} ; {func}")
    with open(out_path, "w", encoding="utf-8") as out:
        for addr in sorted(rows):
            out.write(rows[addr] + "\n")
    print(f"{len(rows)} instructions -> {out_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
