"""Inspect a PS2 EE ELF: header, program/section headers, symbols, strings of interest.

Usage: python -I tools/inspect_elf.py <elf> [out_json]
"""
from __future__ import annotations

import json
import re
import struct
import sys

SHT = {0: "NULL", 1: "PROGBITS", 2: "SYMTAB", 3: "STRTAB", 4: "RELA", 5: "HASH", 6: "DYNAMIC",
       7: "NOTE", 8: "NOBITS", 9: "REL", 11: "DYNSYM", 0x70000006: "MIPS_REGINFO",
       0x7000000d: "MIPS_OPTIONS", 0x70000005: "MIPS_DEBUG"}
PT = {0: "NULL", 1: "LOAD", 2: "DYNAMIC", 3: "INTERP", 4: "NOTE", 6: "PHDR", 0x70000000: "MIPS_REGINFO"}


def parse(path: str) -> dict:
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        raise ValueError("not an ELF")
    ei_class, ei_data = data[4], data[5]
    (e_type, e_machine, e_version, e_entry, e_phoff, e_shoff, e_flags, e_ehsize,
     e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    out = {
        "class": ei_class, "endian": "little" if ei_data == 1 else "big",
        "type": e_type, "machine": e_machine, "entry": f"0x{e_entry:08x}",
        "flags": f"0x{e_flags:08x}", "phnum": e_phnum, "shnum": e_shnum,
        "segments": [], "sections": [], "symbols": {"count": 0},
    }
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from(
            "<IIIIIIII", data, e_phoff + i * e_phentsize)
        out["segments"].append({
            "type": PT.get(p_type, hex(p_type)), "offset": f"0x{p_offset:x}", "vaddr": f"0x{p_vaddr:08x}",
            "filesz": p_filesz, "memsz": p_memsz,
            "flags": "".join(c for c, b in (("R", 4), ("W", 2), ("X", 1)) if p_flags & b), "align": p_align,
        })
    shdrs = []
    for i in range(e_shnum):
        shdrs.append(struct.unpack_from("<IIIIIIIIII", data, e_shoff + i * e_shentsize))
    shstr = b""
    if e_shnum and e_shstrndx < e_shnum:
        s = shdrs[e_shstrndx]
        shstr = data[s[4]:s[4] + s[5]]

    def name_at(tab: bytes, off: int) -> str:
        end = tab.find(b"\0", off)
        return tab[off:end].decode("ascii", "replace")

    for sh in shdrs:
        sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize = sh
        out["sections"].append({
            "name": name_at(shstr, sh_name) if shstr else "", "type": SHT.get(sh_type, hex(sh_type)),
            "addr": f"0x{sh_addr:08x}", "offset": f"0x{sh_offset:x}", "size": sh_size,
            "flags": "".join(c for c, b in (("W", 1), ("A", 2), ("X", 4)) if sh_flags & b),
        })
    for sh in shdrs:
        if sh[1] == 2:  # SYMTAB
            strtab = shdrs[sh[6]]
            st = data[strtab[4]:strtab[4] + strtab[5]]
            n = sh[5] // 16
            funcs, objs = [], 0
            for j in range(n):
                st_name, st_value, st_size, st_info, st_other, st_shndx = struct.unpack_from(
                    "<IIIBBH", data, sh[4] + j * 16)
                if (st_info & 0xF) == 2:
                    funcs.append((st_value, st_size, name_at(st, st_name)))
                elif (st_info & 0xF) == 1:
                    objs += 1
            out["symbols"] = {"count": n, "functions": len(funcs), "objects": objs,
                              "sample": [f"0x{a:08x} {s:6d} {nm}" for a, s, nm in sorted(funcs)[:40]]}
    # Strings that hint at SDK versions, libraries and source files.
    strings = re.findall(rb"[\x20-\x7e]{6,}", data)
    hints = [s.decode() for s in strings if re.search(
        rb"(?i)(sce|libsd|libpad|libmc|libcdvd|ipu|mpeg|\.c\b|\.cpp|\.irx|PsIIlib|Release|version|ver\.|cdrom0|host0|\.bin)",
        s)]
    out["string_hints"] = hints[:400]
    return out


def main() -> int:
    info = parse(sys.argv[1])
    if len(sys.argv) > 2:
        json.dump(info, open(sys.argv[2], "w"), indent=2)
    hints = info.pop("string_hints")
    print(json.dumps(info, indent=2))
    print(f"--- {len(hints)} string hints (first 150) ---")
    for h in hints[:150]:
        print(h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
