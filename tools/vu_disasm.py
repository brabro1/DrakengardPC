"""Disassemble PS2 VU microcode (upper/lower instruction pairs).

Usage: python -I tools/vu_disasm.py <code.bin> [start_hex] [end_hex]

Opcode tables follow the VU instruction set (Sony VU User's Manual layout, as implemented by
PS2Recomp's VU1Interpreter and PCSX2's VUops tables). Unknown encodings print as `?? xxxxxxxx`.
Diagnostic tool: the input is a dump of the user's own game's VU memory and is never committed.
"""
import struct
import sys

BC = "xyzw"


def dest(i):
    d = (i >> 21) & 0xF
    return "".join(c for c, b in zip("xyzw", (8, 4, 2, 1)) if d & b) or "-"


def upper(i):
    ft, fs, fd, op = (i >> 16) & 31, (i >> 11) & 31, (i >> 6) & 31, i & 63
    d = dest(i)
    flags = "".join(n for n, b in (("[I]", 31), ("[E]", 30), ("[M]", 29), ("[D]", 28), ("[T]", 27)) if i >> b & 1)
    if op < 0x3C:
        names = {0x00: "add", 0x04: "sub", 0x08: "madd", 0x0C: "msub", 0x10: "max", 0x14: "mini", 0x18: "mul"}
        base = op & ~3
        if op < 0x1C and base in names:
            return f"{names[base]}{BC[op & 3]}.{d} vf{fd}, vf{fs}, vf{ft}{BC[op & 3]}{flags}"
        single = {0x1C: ("mulq", "Q"), 0x1D: ("maxi", "I"), 0x1E: ("muli", "I"), 0x1F: ("minii", "I"),
                  0x20: ("addq", "Q"), 0x21: ("maddq", "Q"), 0x22: ("addi", "I"), 0x23: ("maddi", "I"),
                  0x24: ("subq", "Q"), 0x25: ("msubq", "Q"), 0x26: ("subi", "I"), 0x27: ("msubi", "I")}
        if op in single:
            n, s = single[op]
            return f"{n}.{d} vf{fd}, vf{fs}, {s}{flags}"
        three = {0x28: "add", 0x29: "madd", 0x2A: "mul", 0x2B: "max", 0x2C: "sub", 0x2D: "msub",
                 0x2E: "opmsub", 0x2F: "mini"}
        if op in three:
            return f"{three[op]}.{d} vf{fd}, vf{fs}, vf{ft}{flags}"
        return f"?? {i:08x}"
    op2 = ((i >> 6) & 31) << 2 | (i & 3)
    names2 = {0x00: "adda", 0x04: "suba", 0x08: "madda", 0x0C: "msuba", 0x18: "mula"}
    base = op2 & ~3
    if op2 < 0x10 or 0x18 <= op2 < 0x1C:
        return f"{names2[base]}{BC[op2 & 3]}.{d} ACC, vf{fs}, vf{ft}{BC[op2 & 3]}{flags}"
    conv = {0x10: "itof0", 0x11: "itof4", 0x12: "itof12", 0x13: "itof15",
            0x14: "ftoi0", 0x15: "ftoi4", 0x16: "ftoi12", 0x17: "ftoi15", 0x1D: "abs"}
    if op2 in conv:
        return f"{conv[op2]}.{d} vf{ft}, vf{fs}{flags}"
    if op2 == 0x1F:
        return f"clipw.xyz vf{fs}, vf{ft}w{flags}"
    accq = {0x1C: ("mulaq", "Q"), 0x1E: ("mulai", "I"), 0x20: ("addaq", "Q"), 0x21: ("maddaq", "Q"),
            0x22: ("addai", "I"), 0x23: ("maddai", "I"), 0x24: ("subaq", "Q"), 0x25: ("msubaq", "Q"),
            0x26: ("subai", "I"), 0x27: ("msubai", "I")}
    if op2 in accq:
        n, s = accq[op2]
        return f"{n}.{d} ACC, vf{fs}, {s}{flags}"
    acc3 = {0x28: "adda", 0x29: "madda", 0x2A: "mula", 0x2C: "suba", 0x2D: "msuba", 0x2E: "opmula"}
    if op2 in acc3:
        return f"{acc3[op2]}.{d} ACC, vf{fs}, vf{ft}{flags}"
    if op2 == 0x2F:
        return f"nop{flags}"
    return f"?? {i:08x}"


def simm(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def lower(i, pc):
    it, is_, id_ = (i >> 16) & 31, (i >> 11) & 31, (i >> 6) & 31
    d = dest(i)
    if i >> 31:
        op = i & 63
        if op < 0x3C:
            tbl = {0x30: "iadd", 0x31: "isub", 0x34: "iand", 0x35: "ior"}
            if op in tbl:
                return f"{tbl[op]} vi{id_}, vi{is_}, vi{it}"
            if op == 0x32:
                return f"iaddi vi{it}, vi{is_}, {simm(id_, 5)}"
            return f"?? {i:08x}"
        op2 = ((i >> 6) & 31) << 2 | (i & 3)
        fsf, ftf = BC[(i >> 21) & 3], BC[(i >> 23) & 3]
        tbl = {
            0x30: f"move.{d} vf{it}, vf{is_}", 0x31: f"mr32.{d} vf{it}, vf{is_}",
            0x34: f"lqi.{d} vf{it}, (vi{is_}++)", 0x35: f"sqi.{d} vf{is_}, (vi{it}++)",
            0x36: f"lqd.{d} vf{it}, (--vi{is_})", 0x37: f"sqd.{d} vf{is_}, (--vi{it})",
            0x38: f"div Q, vf{is_}{fsf}, vf{it}{ftf}", 0x39: f"sqrt Q, vf{it}{ftf}",
            0x3A: f"rsqrt Q, vf{is_}{fsf}, vf{it}{ftf}", 0x3B: "waitq",
            0x3C: f"mtir vi{it}, vf{is_}{fsf}", 0x3D: f"mfir.{d} vf{it}, vi{is_}",
            0x3E: f"ilwr.{d} vi{it}, (vi{is_})", 0x3F: f"iswr.{d} vi{it}, (vi{is_})",
            0x40: f"rnext.{d} vf{it}, R", 0x41: f"rget.{d} vf{it}, R", 0x42: f"rinit R, vf{is_}{fsf}",
            0x43: f"rxor R, vf{is_}{fsf}", 0x64: f"mfp.{d} vf{it}, P", 0x68: f"xtop vi{it}",
            0x69: f"xitop vi{it}", 0x6C: f"xgkick vi{is_}", 0x70: f"esadd P, vf{is_}",
            0x71: f"ersadd P, vf{is_}", 0x72: f"eleng P, vf{is_}", 0x73: f"erleng P, vf{is_}",
            0x74: f"eatanxy P, vf{is_}", 0x75: f"eatanxz P, vf{is_}", 0x76: f"esum P, vf{is_}",
            0x78: f"esqrt P, vf{is_}{fsf}", 0x79: f"ersqrt P, vf{is_}{fsf}", 0x7A: f"ercpr P, vf{is_}{fsf}",
            0x7B: "waitp", 0x7C: f"esin P, vf{is_}{fsf}", 0x7D: f"eatan P, vf{is_}{fsf}",
            0x7E: f"eexp P, vf{is_}{fsf}",
        }
        return tbl.get(op2, f"?? {i:08x}")
    op = i >> 25
    imm11 = simm(i & 0x7FF, 11)
    imm15 = ((i >> 10) & 0x7800) | (i & 0x7FF)
    imm12 = ((i >> 10) & 0x800) | (i & 0x7FF)
    imm24 = i & 0xFFFFFF
    tgt = (pc + 8 + imm11 * 8) & 0x3FFF
    tbl = {
        0x00: f"lq.{d} vf{it}, {imm11}(vi{is_})", 0x01: f"sq.{d} vf{is_}, {imm11}(vi{it})",
        0x04: f"ilw.{d} vi{it}, {imm11}(vi{is_})", 0x05: f"isw.{d} vi{it}, {imm11}(vi{is_})",
        0x08: f"iaddiu vi{it}, vi{is_}, {imm15}", 0x09: f"isubiu vi{it}, vi{is_}, {imm15}",
        0x10: f"fceq vi1, 0x{imm24:06x}", 0x11: f"fcset 0x{imm24:06x}", 0x12: f"fcand vi1, 0x{imm24:06x}",
        0x13: f"fcor vi1, 0x{imm24:06x}", 0x14: f"fseq vi{it}, 0x{imm12:03x}", 0x15: f"fsset 0x{imm12:03x}",
        0x16: f"fsand vi{it}, 0x{imm12:03x}", 0x17: f"fsor vi{it}, 0x{imm12:03x}",
        0x18: f"fmeq vi{it}, vi{is_}", 0x1A: f"fmand vi{it}, vi{is_}", 0x1B: f"fmor vi{it}, vi{is_}",
        0x1C: f"fcget vi{it}", 0x20: f"b 0x{tgt:04x}", 0x21: f"bal vi{it}, 0x{tgt:04x}",
        0x24: f"jr vi{is_}", 0x25: f"jalr vi{it}, vi{is_}",
        0x28: f"ibeq vi{it}, vi{is_}, 0x{tgt:04x}", 0x29: f"ibne vi{it}, vi{is_}, 0x{tgt:04x}",
        0x2C: f"ibltz vi{is_}, 0x{tgt:04x}", 0x2D: f"ibgtz vi{is_}, 0x{tgt:04x}",
        0x2E: f"iblez vi{is_}, 0x{tgt:04x}", 0x2F: f"ibgez vi{is_}, 0x{tgt:04x}",
    }
    if i == 0x8000033C:
        return "nop"
    return tbl.get(op, f"?? {i:08x}")


def main():
    data = open(sys.argv[1], "rb").read()
    start = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
    end = int(sys.argv[3], 16) if len(sys.argv) > 3 else len(data)
    for pc in range(start & ~7, min(end, len(data)), 8):
        lo, up = struct.unpack_from("<II", data, pc)
        lower_text = f"loi {struct.unpack('<f', struct.pack('<I', lo))[0]:g}" if up >> 31 else lower(lo, pc)
        print(f"{pc:04x}  {up:08x} {lo:08x}  {upper(up):38s} {lower_text}")


if __name__ == "__main__":
    main()
