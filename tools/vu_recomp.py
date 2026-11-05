"""Ahead-of-time compiler for the game's VU1 microprograms.

Usage: python -I tools/vu_recomp.py <SLUS_207.32> <output dir>

Every VU microprogram Drakengard runs is uploaded with VIF MPG commands that sit inline in the
ELF: VU1 programs in .vudata, VU0 programs in .vutext. This tool extracts each MPG payload (load address + code) and translates
it into one C++ function against runtime/vu_aot.h. At run time the VIF1 MPG handler matches
uploads to these programs by address and FNV-1a hash, and VU1Interpreter::run calls the
compiled code instead of interpreting it (unknown code is still interpreted).

Semantics mirror PS2Recomp's VU1Interpreter (ps2_vu1_upper.cpp / ps2_vu1_lower.cpp /
ps2_vu1_core.cpp): the same instruction decoding, operand normalisation, FMAC value/flag
rules, instruction latencies and stalls. See runtime/vu_aot.h for the timing model.

The output is game-derived (it contains the translated microcode) and belongs in work/.
"""
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from funcmap import Elf  # noqa: E402

LANES = (8, 4, 2, 1)  # x y z w lane bits (VU dest field)
COMP = "xyzw"


def comps(mask):
    return [c for c in range(4) if mask & LANES[c]]


def fnv1a(data):
    h = 14695981039346656037
    for b in data:
        h = ((h ^ b) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return h


def simm(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


class Unsupported(Exception):
    pass


# --------------------------------------------------------------------------------------------
# Usage (reads/writes/latency) exactly as VU1Interpreter::decodeUpperUsage/decodeLowerUsage.
# --------------------------------------------------------------------------------------------
class Usage:
    def __init__(self):
        self.vf_read = []      # (reg, lanes)
        self.vf_write = None   # (reg, lanes)
        self.vi_read = set()
        self.vi_write = set()
        self.acc_read = 0
        self.acc_write = 0
        self.latency = 0
        self.vi_latency = 0
        self.pipeline = None   # fmac lsu fdiv efu ialu branch xgkick
        self.wait_q = False
        self.wait_p = False
        self.reads_clip = False
        self.delays_branch = False

    def rvf(self, reg, lanes):
        self.vf_read.append((reg, lanes))

    def wvf(self, reg, lanes):
        self.vf_write = (reg, lanes)


ACC_READ_OPS = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x21, 0x23, 0x25, 0x27, 0x29, 0x2D, 0x2E}


def upper_usage(u):
    us = Usage()
    us.pipeline = "fmac"
    us.latency = 4
    op = u & 0x3F
    dest = (u >> 21) & 0xF
    fs, ft, fd = (u >> 11) & 31, (u >> 16) & 31, (u >> 6) & 31
    if op <= 0x2F:
        us.rvf(fs, dest)
        us.wvf(fd, dest)
        if op <= 0x1B:
            us.rvf(ft, LANES[op & 3])
        elif op >= 0x28:
            us.rvf(ft, 0xE if op == 0x2E else dest)
        if op in ACC_READ_OPS:
            us.acc_read = dest
        return us
    if op >= 0x3C:
        sp = (u & 3) | ((u >> 4) & 0x7C)
        writes_acc = sp <= 0x0F or 0x18 <= sp <= 0x1C or sp == 0x1E or 0x20 <= sp <= 0x2A or 0x2C <= sp <= 0x2E
        if writes_acc:
            us.rvf(fs, dest)
            if sp <= 0x1B:
                us.rvf(ft, LANES[sp & 3])
            elif 0x28 <= sp <= 0x2E:
                us.rvf(ft, 0xE if sp == 0x2E else dest)
            us.acc_write = dest
            if 0x08 <= sp <= 0x0F or sp in (0x21, 0x23, 0x25, 0x27, 0x29, 0x2D):
                us.acc_read = dest
        elif 0x10 <= sp <= 0x17 or sp == 0x1D:
            us.rvf(fs, dest)
            us.wvf(ft, dest)
        elif sp == 0x1F:
            us.rvf(fs, 0xE)
            us.rvf(ft, 0x1)
        elif sp not in (0x2F, 0x30):
            raise Unsupported(f"upper special {sp:#x}")
        return us
    raise Unsupported(f"upper op {op:#x}")


def lower_usage(lo):
    us = Usage()
    if lo in (0, 0x8000033C):
        return us
    hi = (lo >> 25) & 0x7F
    vft, vfs = (lo >> 16) & 31, (lo >> 11) & 31
    vit, vis, vid = (lo >> 16) & 15, (lo >> 11) & 15, (lo >> 6) & 15
    dest = (lo >> 21) & 0xF

    def rvi(r):
        if r:
            us.vi_read.add(r)

    def wvi(r):
        if r:
            us.vi_write.add(r)
    simple = {
        0x00: ("lsu", 4, lambda: (rvi(vis), us.wvf(vft, dest))),
        0x01: ("lsu", 1, lambda: (rvi(vit), us.rvf(vfs, dest))),
        0x04: ("lsu", 4, lambda: (rvi(vis), wvi(vit))),
        0x05: ("lsu", 1, lambda: (rvi(vis), rvi(vit))),
        0x14: ("ialu", 1, lambda: wvi(vit)), 0x16: ("ialu", 1, lambda: wvi(vit)), 0x17: ("ialu", 1, lambda: wvi(vit)),
        0x15: ("fmac", 4, lambda: None),
        0x18: ("ialu", 1, lambda: (rvi(vis), wvi(vit))), 0x1A: ("ialu", 1, lambda: (rvi(vis), wvi(vit))),
        0x1B: ("ialu", 1, lambda: (rvi(vis), wvi(vit))),
        0x20: ("branch", 0, lambda: None),
        0x21: ("branch", 1, lambda: wvi(vit)),
        0x24: ("branch", 0, lambda: rvi(vis)),
        0x25: ("branch", 1, lambda: (rvi(vis), wvi(vit))),
        0x28: ("branch", 0, lambda: (rvi(vis), rvi(vit))), 0x29: ("branch", 0, lambda: (rvi(vis), rvi(vit))),
        0x2C: ("branch", 0, lambda: rvi(vis)), 0x2D: ("branch", 0, lambda: rvi(vis)),
        0x2E: ("branch", 0, lambda: rvi(vis)), 0x2F: ("branch", 0, lambda: rvi(vis)),
    }
    if hi in (0x08, 0x09):
        us.pipeline, us.latency, us.delays_branch = "ialu", 1, True
        rvi(vis)
        wvi(vit)
        return us
    if hi in (0x10, 0x12, 0x13):
        us.pipeline, us.latency, us.reads_clip = "ialu", 1, True
        wvi(1)
        return us
    if hi == 0x11:
        us.pipeline, us.latency = "fmac", 4
        return us
    if hi == 0x1C:
        us.pipeline, us.latency, us.reads_clip = "ialu", 1, True
        wvi(vit)
        return us
    if hi in simple:
        us.pipeline, us.latency, fn = simple[hi]
        fn()
        return us
    if hi != 0x40:
        raise Unsupported(f"lower op {hi:#x}")
    direct = lo & 0x3F
    if direct in (0x30, 0x31, 0x34, 0x35):
        us.pipeline, us.latency, us.delays_branch = "ialu", 1, True
        rvi(vis)
        rvi(vit)
        wvi(vid)
        return us
    if direct == 0x32:
        us.pipeline, us.latency, us.delays_branch = "ialu", 1, True
        rvi(vis)
        wvi(vit)
        return us
    if direct < 0x3C:
        raise Unsupported(f"lower direct {direct:#x}")
    sp = (lo & 3) | ((lo >> 4) & 0x7C)
    fsf, ftf = (lo >> 21) & 3, (lo >> 23) & 3
    if sp in (0x30, 0x31):
        us.pipeline, us.latency = "fmac", 4
        us.rvf(vfs, 0xF if sp == 0x31 else dest)
        us.wvf(vft, dest)
    elif sp in (0x34, 0x36):
        us.pipeline, us.latency, us.vi_latency, us.delays_branch = "lsu", 4, 1, True
        rvi(vis)
        wvi(vis)
        us.wvf(vft, dest)
    elif sp in (0x35, 0x37):
        us.pipeline, us.latency, us.delays_branch = "lsu", 1, True
        rvi(vit)
        wvi(vit)
        us.rvf(vfs, dest)
    elif sp == 0x38:
        us.pipeline, us.latency = "fdiv", 7
        us.rvf(vfs, LANES[fsf])
        us.rvf(vft, LANES[ftf])
    elif sp == 0x39:
        us.pipeline, us.latency = "fdiv", 7
        us.rvf(vft, LANES[ftf])
    elif sp == 0x3A:
        us.pipeline, us.latency = "fdiv", 13
        us.rvf(vfs, LANES[fsf])
        us.rvf(vft, LANES[ftf])
    elif sp == 0x3B:
        us.pipeline, us.wait_q = "fdiv", True
    elif sp == 0x3C:
        us.pipeline, us.latency, us.delays_branch = "ialu", 1, True
        us.rvf(vfs, LANES[fsf])
        wvi(vit)
    elif sp == 0x3D:
        us.pipeline, us.latency = "fmac", 4
        rvi(vis)
        us.wvf(vft, dest)
    elif sp == 0x3E:
        us.pipeline, us.latency = "lsu", 4
        rvi(vis)
        wvi(vit)
    elif sp == 0x3F:
        us.pipeline, us.latency = "lsu", 1
        rvi(vis)
        rvi(vit)
    elif sp in (0x40, 0x41):
        us.pipeline, us.latency = "fmac", 4
        us.wvf(vft, dest)
    elif sp in (0x42, 0x43):
        us.pipeline, us.latency = "ialu", 1
        us.rvf(vfs, LANES[fsf])
    elif sp == 0x64:
        us.pipeline, us.latency = "fmac", 4
        us.wvf(vft, dest)
    elif sp in (0x68, 0x69):
        us.pipeline, us.latency = "ialu", 1
        wvi(vit)
    elif sp == 0x6C:
        us.pipeline, us.latency = "xgkick", 2
        rvi(vis)
    elif sp in (0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x7C, 0x7D):
        us.pipeline = "efu"
        us.latency = {0x70: 11, 0x71: 18, 0x72: 18, 0x77: 18, 0x73: 24, 0x74: 54, 0x75: 54, 0x7C: 54,
                      0x76: 12, 0x78: 12, 0x7A: 12, 0x79: 29, 0x7D: 44}[sp]
        if 0x70 <= sp <= 0x73:
            us.rvf(vfs, 0xE)
        elif sp == 0x74:
            us.rvf(vfs, 0xC)
        elif sp == 0x75:
            us.rvf(vfs, 0xA)
        elif sp == 0x76:
            us.rvf(vfs, 0xF)
        else:
            us.rvf(vfs, LANES[fsf])
    elif sp == 0x7B:
        us.pipeline, us.wait_p = "efu", True
    else:
        raise Unsupported(f"lower special {sp:#x}")
    return us


# --------------------------------------------------------------------------------------------
# Code emission
# --------------------------------------------------------------------------------------------
def fl(x):
    return f"s.vf[{x[0]}][{x[1]}]"


Q_UPPER = {0x1C, 0x20, 0x21, 0x24, 0x25}
Q_SPECIAL = {0x1C, 0x20, 0x21, 0x24, 0x25}


def emit_upper(u, out):
    """Upper instruction. Results go to temporaries; returns (target, lanes) to write back."""
    op = u & 0x3F
    dest = (u >> 21) & 0xF
    fs, ft, fd = (u >> 11) & 31, (u >> 16) & 31, (u >> 6) & 31
    lanes = comps(dest)

    def fmac(kind, rhs, acc_target, product=False, cross=False, target=None):
        # kind: add sub mul madd msub ; rhs(c) -> C++ expr for the second operand of lane c
        out.append("{ uint32_t mac = 0, st = 0, sticky = 0;")
        for c in lanes:
            if cross:
                left = ["s.vf[%d][1]" % fs, "s.vf[%d][2]" % fs, "s.vf[%d][0]" % fs][c] if c < 3 else None
                right = ["s.vf[%d][2]" % ft, "s.vf[%d][0]" % ft, "s.vf[%d][1]" % ft][c] if c < 3 else None
                if c == 3:
                    out.append(f"  u{c} = 0.0f; f{c} = vuaot::laneFlags(u{c}, 0.0);")
                else:
                    out.append(f"  {{ const float a = vuaot::nop_({left}), b = vuaot::nop_({right});")
                    if kind == "msub":
                        out.append(f"    const float ac = vuaot::nop_(s.acc[{c}]); u{c} = ac - a * b; "
                                   f"f{c} = vuaot::laneFlags(u{c}, double(ac) - double(a) * double(b));")
                    else:
                        out.append(f"    u{c} = a * b; f{c} = vuaot::laneFlags(u{c}, double(a) * double(b));")
                    if product:
                        out.append("    sticky |= vuaot::productSticky(a, b);")
                    out.append("  }")
                continue
            out.append(f"  {{ const float a = vuaot::nop_(s.vf[{fs}][{c}]), b = {rhs(c)};")
            if kind == "add":
                expr, ex = "a + b", "double(a) + double(b)"
            elif kind == "sub":
                expr, ex = "a - b", "double(a) - double(b)"
            elif kind == "mul":
                expr, ex = "a * b", "double(a) * double(b)"
            elif kind == "madd":
                out.append(f"    const float ac = vuaot::nop_(s.acc[{c}]);")
                expr, ex = "ac + a * b", "double(ac) + double(a) * double(b)"
            else:
                out.append(f"    const float ac = vuaot::nop_(s.acc[{c}]);")
                expr, ex = "ac - a * b", "double(ac) - double(a) * double(b)"
            out.append(f"    u{c} = {expr}; f{c} = vuaot::laneFlags(u{c}, {ex});")
            if product:
                out.append("    sticky |= vuaot::productSticky(a, b);")
            out.append("  }")
        for c in lanes:
            lane = LANES[c]
            out.append(f"  mac |= ((f{c} & 1u) ? {lane}u : 0u) | ((f{c} & 2u) ? {lane << 4}u : 0u) | "
                       f"((f{c} & 4u) ? {lane << 8}u : 0u) | ((f{c} & 8u) ? {lane << 12}u : 0u); st |= f{c};")
        if lanes:
            out.append("  vuaot::fmacFlags(c, mac, st, sticky);")
        out.append("}")
        return ("acc", lanes) if acc_target else (("vf", target if target is not None else fd), lanes)

    for c in lanes:
        out.append(f"float u{c}; uint32_t f{c} = 0; (void)f{c};")

    bc = lambda reg, b: (lambda c: f"vuaot::nop_(s.vf[{reg}][{b}])")
    vec = lambda reg: (lambda c: f"vuaot::nop_(s.vf[{reg}][{c}])")
    qv = lambda c: "vuaot::nop_(s.q)"
    iv = lambda c: "vuaot::nop_(s.i)"

    def minmax(is_max, rhs, target):
        for c in lanes:
            out.append(f"{{ const float a = vuaot::nop_(s.vf[{fs}][{c}]), b = {rhs(c)}; "
                       f"u{c} = (a {'>' if is_max else '<'} b) ? a : b; }}")
        return (("vf", target), lanes)

    if op <= 0x2F:
        if op <= 0x1B:
            kind = ["add", "sub", "madd", "msub", "max", "mini", "mul"][op >> 2]
            rhs = bc(ft, op & 3)
            if kind == "max":
                return minmax(True, rhs, fd)
            if kind == "mini":
                return minmax(False, rhs, fd)
            return fmac(kind, rhs, False, product=kind in ("madd", "msub"))
        table = {0x1C: ("mul", qv), 0x1E: ("mul", iv), 0x20: ("add", qv), 0x21: ("madd", qv), 0x22: ("add", iv),
                 0x23: ("madd", iv), 0x24: ("sub", qv), 0x25: ("msub", qv), 0x26: ("sub", iv), 0x27: ("msub", iv),
                 0x28: ("add", vec(ft)), 0x29: ("madd", vec(ft)), 0x2A: ("mul", vec(ft)), 0x2C: ("sub", vec(ft)),
                 0x2D: ("msub", vec(ft))}
        if op in table:
            kind, rhs = table[op]
            return fmac(kind, rhs, False, product=kind in ("madd", "msub"))
        if op == 0x1D:
            return minmax(True, iv, fd)
        if op == 0x1F:
            return minmax(False, iv, fd)
        if op == 0x2B:
            return minmax(True, vec(ft), fd)
        if op == 0x2F:
            return minmax(False, vec(ft), fd)
        if op == 0x2E:
            return fmac("msub", None, False, product=True, cross=True)
        raise Unsupported(f"upper {op:#x}")
    sp = (u & 3) | ((u >> 4) & 0x7C)
    if sp <= 0x0F or 0x18 <= sp <= 0x1B:
        kind = {0: "add", 1: "sub", 2: "madd", 3: "msub", 6: "mul"}[sp >> 2]
        return fmac(kind, bc(ft, sp & 3), True, product=kind in ("madd", "msub"))
    table = {0x1C: ("mul", qv), 0x1E: ("mul", iv), 0x20: ("add", qv), 0x21: ("madd", qv), 0x22: ("add", iv),
             0x23: ("madd", iv), 0x24: ("sub", qv), 0x25: ("msub", qv), 0x26: ("sub", iv), 0x27: ("msub", iv),
             0x28: ("add", vec(ft)), 0x29: ("madd", vec(ft)), 0x2A: ("mul", vec(ft)), 0x2C: ("sub", vec(ft)),
             0x2D: ("msub", vec(ft))}
    if sp in table:
        kind, rhs = table[sp]
        return fmac(kind, rhs, True, product=kind in ("madd", "msub"))
    if sp == 0x2E:  # OPMULA
        return fmac("mul", None, True, cross=True)
    if 0x10 <= sp <= 0x13:  # ITOF
        scale = [1.0, 16.0, 4096.0, 32768.0][sp & 3]
        for c in lanes:
            out.append(f"{{ int32_t iv; std::memcpy(&iv, &s.vf[{fs}][{c}], 4); u{c} = float(iv)"
                       + ("" if scale == 1.0 else f" / {scale}f") + "; }")
        return (("vf", ft), lanes)
    if 0x14 <= sp <= 0x17:  # FTOI
        scale = [1.0, 16.0, 4096.0, 32768.0][sp & 3]
        for c in lanes:
            out.append(f"{{ const double v = double(vuaot::nop_(s.vf[{fs}][{c}])) * {scale}; int32_t iv = "
                       "v >= 2147483647.0 ? INT32_MAX : v <= -2147483648.0 ? INT32_MIN : int32_t(v); "
                       f"std::memcpy(&u{c}, &iv, 4); }}")
        return (("vf", ft), lanes)
    if sp == 0x1D:  # ABS
        for c in lanes:
            out.append(f"u{c} = std::fabs(vuaot::nop_(s.vf[{fs}][{c}]));")
        return (("vf", ft), lanes)
    if sp == 0x1F:  # CLIP
        out.append(f"{{ uint32_t wb; std::memcpy(&wb, &s.vf[{ft}][3], 4); "
                   "const int32_t lim = (wb & 0x7F800000u) ? int32_t(wb & 0x7FFFFFFFu) : 0x007FFFFF; "
                   "uint32_t cf = 0; for (int k = 0; k < 3; ++k) { uint32_t b; "
                   f"std::memcpy(&b, &s.vf[{fs}][k], 4); "
                   "if (int32_t(b) > lim) cf |= 1u << (2 * k); "
                   "if (int32_t(b ^ 0x80000000u) > lim) cf |= 2u << (2 * k); } vuaot::clip(c, cf); }")
        return (None, [])
    if sp in (0x2F, 0x30):
        return (None, [])
    raise Unsupported(f"upper special {sp:#x}")


def emit_lower(lo, pc, out, ctx):
    """Lower instruction. VF results go to temporaries l0..l3 (returned); VI/memory/Q/P are
    applied immediately. ctx collects branch info."""
    if lo in (0, 0x8000033C):
        return None
    hi = (lo >> 25) & 0x7F
    vft, vfs = (lo >> 16) & 31, (lo >> 11) & 31
    vit, vis, vid = (lo >> 16) & 15, (lo >> 11) & 15, (lo >> 6) & 15
    dest = (lo >> 21) & 0xF
    lanes = comps(dest)
    imm11 = simm(lo & 0x7FF, 11)

    def setvi(reg, expr):
        if reg:
            out.append(f"s.vi[{reg}] = {expr};")

    def load_into(addr_expr, target, lns):
        out.append(f"{{ const uint8_t *m = vuaot::qw(c, {addr_expr});")
        for k in lns:
            out.append(f"  std::memcpy(&l{k}, m + {4 * k}, 4);")
        out.append("}")
        return (("vf", target), lns)

    def store_from(addr_expr, src, lns):
        out.append(f"{{ uint8_t *m = vuaot::qw(c, {addr_expr});")
        for k in lns:
            out.append(f"  std::memcpy(m + {4 * k}, &s.vf[{src}][{k}], 4);")
        out.append("}")

    for k in range(4):
        out.append(f"float l{k}; (void)l{k};")
    if hi == 0x00:
        return load_into(f"s.vi[{vis}] + ({imm11})", vft, lanes)
    if hi == 0x01:
        store_from(f"s.vi[{vit}] + ({imm11})", vfs, lanes)
        return None
    if hi == 0x04:
        k = lanes[0] if lanes else 3
        out.append(f"{{ uint32_t v; std::memcpy(&v, vuaot::qw(c, s.vi[{vis}] + ({imm11})) + {4 * k}, 4);")
        setvi(vit, "int32_t(int16_t(v & 0xFFFF))")
        out.append("}")
        return None
    if hi == 0x05:
        out.append(f"{{ uint8_t *m = vuaot::qw(c, s.vi[{vis}] + ({imm11})); const uint32_t v = uint16_t(s.vi[{vit}]);")
        for k in lanes:
            out.append(f"  std::memcpy(m + {4 * k}, &v, 4);")
        out.append("}")
        return None
    if hi in (0x08, 0x09):
        imm15 = (lo & 0x7FF) | ((lo >> 10) & 0x7800)
        setvi(vit, f"int16_t(s.vi[{vis}] {'+' if hi == 0x08 else '-'} {imm15})")
        return None
    if hi in (0x10, 0x12, 0x13):
        imm = lo & 0xFFFFFF
        cond = {0x10: f"(s.clip & 0xFFFFFFu) == 0x{imm:06x}u", 0x12: f"(s.clip & 0x{imm:06x}u) != 0",
                0x13: f"(s.clip | 0x{imm:06x}u) == 0xFFFFFFu"}[hi]
        out.append(f"s.vi[1] = ({cond}) ? 1 : 0;")
        return None
    if hi == 0x11:
        out.append(f"vuaot::fcset(c, 0x{lo & 0xFFFFFF:06x}u);")
        return None
    imm12 = (((lo >> 21) & 1) << 11) | (lo & 0x7FF)
    if hi == 0x14:
        setvi(vit, f"((s.status & 0xFFFu) == 0x{imm12:03x}u) ? 1 : 0")
        return None
    if hi == 0x15:
        out.append(f"vuaot::fsset(c, 0x{imm12:03x}u);")
        return None
    if hi == 0x16:
        setvi(vit, f"int32_t((s.status & 0xFFFu) & 0x{imm12:03x}u)")
        return None
    if hi == 0x17:
        setvi(vit, f"int32_t((s.status & 0xFFFu) | 0x{imm12:03x}u)")
        return None
    if hi == 0x18:
        setvi(vit, f"((s.mac & 0xFFFFu) == uint32_t(uint16_t(s.vi[{vis}]))) ? 1 : 0")
        return None
    if hi == 0x1A:
        setvi(vit, f"int32_t(s.mac & uint32_t(uint16_t(s.vi[{vis}])))")
        return None
    if hi == 0x1B:
        setvi(vit, f"int32_t(s.mac | uint32_t(uint16_t(s.vi[{vis}])))")
        return None
    if hi == 0x1C:
        setvi(vit, "int32_t(s.clip & 0x0FFFu)")
        return None
    if 0x20 <= hi <= 0x2F:
        target = (pc + 8 + imm11 * 8) & 0x3FFF

        def brvi(r):
            if r == 0:
                return "0"
            return f"((c.bkCyc == start && c.bkReg == {r}) ? c.bkVal : s.vi[{r}])"
        link = (pc + 16) // 8
        if hi == 0x20:
            ctx["branch"] = ("true", str(target), target)
        elif hi == 0x21:
            ctx["branch"] = ("true", str(target), target)
            setvi(vit, str(link))
        elif hi in (0x24, 0x25):
            ctx["branch"] = ("true", f"(uint32_t(uint16_t({brvi(vis)})) * 8u) & 0x3FFFu", None)
            if hi == 0x25:
                ctx["link"] = (vit, link)
        else:
            a, b = f"int16_t({brvi(vis)})", f"int16_t({brvi(vit)})"
            cond = {0x28: f"{a} == {b}", 0x29: f"{a} != {b}", 0x2C: f"{a} < 0", 0x2D: f"{a} > 0",
                    0x2E: f"{a} <= 0", 0x2F: f"{a} >= 0"}.get(hi)
            if cond is None:
                raise Unsupported(f"branch {hi:#x}")
            ctx["branch"] = (cond, str(target), target)
        return None
    if hi != 0x40:
        raise Unsupported(f"lower {hi:#x}")
    direct = lo & 0x3F
    if direct == 0x30:
        setvi(vid, f"int16_t(s.vi[{vis}] + s.vi[{vit}])")
        return None
    if direct == 0x31:
        setvi(vid, f"int16_t(s.vi[{vis}] - s.vi[{vit}])")
        return None
    if direct == 0x32:
        setvi(vit, f"int16_t(s.vi[{vis}] + ({simm((lo >> 6) & 0x1F, 5)}))")
        return None
    if direct == 0x34:
        setvi(vid, f"s.vi[{vis}] & s.vi[{vit}]")
        return None
    if direct == 0x35:
        setvi(vid, f"s.vi[{vis}] | s.vi[{vit}]")
        return None
    sp = (lo & 3) | ((lo >> 4) & 0x7C)
    fsf, ftf = (lo >> 21) & 3, (lo >> 23) & 3
    if sp == 0x30:
        for k in lanes:
            out.append(f"l{k} = s.vf[{vfs}][{k}];")
        return (("vf", vft), lanes)
    if sp == 0x31:
        for k in lanes:
            out.append(f"l{k} = s.vf[{vfs}][{(k + 1) % 4}];")
        return (("vf", vft), lanes)
    if sp == 0x34:
        r = load_into(f"int32_t(uint16_t(s.vi[{vis}]))", vft, lanes)
        if vis:
            out.append(f"s.vi[{vis}] = int16_t(s.vi[{vis}] + 1);")
        return r
    if sp == 0x35:
        store_from(f"int32_t(uint16_t(s.vi[{vit}]))", vfs, lanes)
        if vit:
            out.append(f"s.vi[{vit}] = int16_t(s.vi[{vit}] + 1);")
        return None
    if sp == 0x36:
        if vis:
            out.append(f"s.vi[{vis}] = int16_t(s.vi[{vis}] - 1);")
        return load_into(f"int32_t(uint16_t(s.vi[{vis}]))", vft, lanes)
    if sp == 0x37:
        if vit:
            out.append(f"s.vi[{vit}] = int16_t(s.vi[{vit}] - 1);")
        store_from(f"int32_t(uint16_t(s.vi[{vit}]))", vfs, lanes)
        return None
    if sp == 0x38:
        out.append(f"{{ const float n = vuaot::nop_(s.vf[{vfs}][{fsf}]), d = vuaot::nop_(s.vf[{vft}][{ftf}]); "
                   "uint32_t di = 0; float r; if (d == 0.0f) { di = n == 0.0f ? 0x10u : 0x20u; "
                   "r = std::signbit(n) != std::signbit(d) ? -FLT_MAX : FLT_MAX; } else r = n / d; "
                   "vuaot::queueQ(c, r, 7, di); }")
        return None
    if sp == 0x39:
        out.append(f"{{ const float v = vuaot::nop_(s.vf[{vft}][{ftf}]); "
                   "vuaot::queueQ(c, std::sqrt(std::fabs(v)), 7, v < 0.0f ? 0x10u : 0u); }")
        return None
    if sp == 0x3A:
        out.append(f"{{ const float n = vuaot::nop_(s.vf[{vfs}][{fsf}]), rad = vuaot::nop_(s.vf[{vft}][{ftf}]); "
                   "const float d = std::sqrt(std::fabs(rad)); uint32_t di = rad < 0.0f ? 0x10u : 0u; float r; "
                   "if (d != 0.0f) r = n / d; else { di = n == 0.0f ? 0x10u : 0x20u; "
                   "r = std::signbit(n) ? -FLT_MAX : FLT_MAX; } vuaot::queueQ(c, r, 13, di); }")
        return None
    if sp == 0x3B:
        return None  # WAITQ: stall handled at issue
    if sp == 0x3C:
        out.append(f"{{ uint32_t v; std::memcpy(&v, &s.vf[{vfs}][{fsf}], 4);")
        setvi(vit, "int32_t(int16_t(v & 0xFFFF))")
        out.append("}")
        return None
    if sp == 0x3D:
        out.append(f"{{ const int32_t v = int32_t(int16_t(s.vi[{vis}] & 0xFFFF));")
        for k in lanes:
            out.append(f"  std::memcpy(&l{k}, &v, 4);")
        out.append("}")
        return (("vf", vft), lanes)
    if sp == 0x3E:
        k = lanes[0] if lanes else 3
        out.append(f"{{ uint32_t v; std::memcpy(&v, vuaot::qw(c, int32_t(uint16_t(s.vi[{vis}]))) + {4 * k}, 4);")
        setvi(vit, "int32_t(int16_t(v & 0xFFFF))")
        out.append("}")
        return None
    if sp == 0x3F:
        out.append(f"{{ uint8_t *m = vuaot::qw(c, int32_t(uint16_t(s.vi[{vis}]))); const uint32_t v = uint16_t(s.vi[{vit}]);")
        for k in lanes:
            out.append(f"  std::memcpy(m + {4 * k}, &v, 4);")
        out.append("}")
        return None
    if sp == 0x40:
        out.append("{ const uint32_t x = (s.r >> 4) & 1u, y = (s.r >> 22) & 1u; "
                   "s.r = (((s.r << 1) ^ x ^ y) & 0x007FFFFFu) | 0x3F800000u; }")
    if sp in (0x40, 0x41):
        for k in lanes:
            out.append(f"std::memcpy(&l{k}, &s.r, 4);")
        return (("vf", vft), lanes)
    if sp == 0x42:
        out.append(f"{{ uint32_t b; std::memcpy(&b, &s.vf[{vfs}][{fsf}], 4); s.r = 0x3F800000u | (b & 0x007FFFFFu); }}")
        return None
    if sp == 0x43:
        out.append(f"{{ uint32_t b; std::memcpy(&b, &s.vf[{vfs}][{fsf}], 4); "
                   "s.r = 0x3F800000u | ((s.r ^ b) & 0x007FFFFFu); }")
        return None
    if sp == 0x64:
        for k in lanes:
            out.append(f"l{k} = s.p;")
        return (("vf", vft), lanes)
    if sp == 0x68:
        setvi(vit, "int32_t(s.top & 0x3FFu)")
        return None
    if sp == 0x69:
        setvi(vit, "int32_t(s.itop & 0x3FFu)")
        return None
    if sp == 0x6C:
        out.append(f"vuaot::xgkick(c, uint32_t(uint16_t(s.vi[{vis}])));")
        return None
    xyz = f"const float x = vuaot::nop_(s.vf[{vfs}][0]), y = vuaot::nop_(s.vf[{vfs}][1]), z = vuaot::nop_(s.vf[{vfs}][2]);"
    one = f"const float v = vuaot::nop_(s.vf[{vfs}][{fsf}]);"
    efu = {
        0x70: (xyz, "x * x + y * y + z * z", 11),
        0x71: (xyz + " const float sum = x * x + y * y + z * z;", "sum != 0.0f ? 1.0f / sum : sum", 18),
        0x72: (xyz, "std::sqrt(x * x + y * y + z * z)", 18),
        0x73: (xyz + " const float len = std::sqrt(x * x + y * y + z * z);", "len != 0.0f ? 1.0f / len : len", 24),
        0x74: (xyz, "x != 0.0f ? vuaot::efuAtan(y / x) : 0.0f", 54),
        0x75: (xyz, "x != 0.0f ? vuaot::efuAtan(z / x) : 0.0f", 54),
        0x76: ("float sum = 0.0f; for (int k = 0; k < 4; ++k) sum += vuaot::nop_(s.vf[%d][k]);" % vfs, "sum", 12),
        0x77: (one + " float r = v; if (r >= 0.0f) { r = std::sqrt(r); if (r != 0.0f) r = 1.0f / r; }", "r", 18),
        0x78: (one, "v >= 0.0f ? std::sqrt(v) : v", 12),
        0x79: (one, "vuaot::efuSin(v)", 29),
        0x7A: (one, "v != 0.0f ? 1.0f / v : v", 12),
        0x7C: (one, "vuaot::efuAtan(v)", 54),
        0x7D: (one, "vuaot::efuExp(v)", 44),
    }
    if sp in efu:
        pre, expr, lat = efu[sp]
        out.append(f"{{ {pre} vuaot::queueP(c, {expr}, {lat}); }}")
        return None
    if sp == 0x7B:
        return None  # WAITP: stall handled at issue
    raise Unsupported(f"lower special {sp:#x}")


def emit_pair(pc, lo, up, prog):
    """C++ for one instruction pair (labelled L_<pc>)."""
    out = []
    ibit = bool(up & 0x80000000)
    ebit = bool(up & 0x40000000)
    uu = upper_usage(up)
    lu = Usage() if ibit else lower_usage(lo)
    is_branch = lu.pipeline == "branch"
    out.append(f"L_{pc:04x}: {{")
    if is_branch:
        out.append("const uint32_t start = c.cyc; (void)start;")
    # ---- issue: stalls on operands and resources (VU1Interpreter::calculatePairReadyCycle) ----
    terms = []
    for us in (uu, lu):
        for reg, lanes in us.vf_read:
            if reg == 0:
                continue
            for k in comps(lanes):
                terms.append(f"c.vfReady[{reg}][{k}]")
        for r in sorted(us.vi_read):
            terms.append(f"c.viReady[{r}]")
        for k in comps(us.acc_read):
            terms.append(f"c.accReady[{k}]")
    for t in dict.fromkeys(terms):
        out.append(f"if ({t} > c.cyc) c.cyc = {t};")
    if lu.pipeline == "fdiv":
        out.append("if (c.qPending && c.qReady > c.cyc) c.cyc = c.qReady;")
        out.append("vuaot::syncQ(c);")
    if lu.pipeline == "efu" and not lu.wait_p:
        out.append("if (c.efuResource > c.cyc) c.cyc = c.efuResource;")
        out.append("vuaot::syncP(c);")
    if lu.wait_p:
        out.append("if (c.efuResource > c.cyc) c.cyc = c.efuResource; if (c.pPending && c.pReady > c.cyc) c.cyc = c.pReady;")
    # ---- make pipeline results due at this cycle visible to the readers in this pair ----
    op = up & 0x3F
    sp = (up & 3) | ((up >> 4) & 0x7C)
    upper_reads_q = (op in Q_UPPER) or (op >= 0x3C and sp in Q_SPECIAL)
    lower_hi = (lo >> 25) & 0x7F
    lower_sp = (lo & 3) | ((lo >> 4) & 0x7C)
    reads_flags = not ibit and (lower_hi in (0x10, 0x12, 0x13, 0x14, 0x16, 0x17, 0x18, 0x1A, 0x1B, 0x1C))
    reads_p = not ibit and lower_hi == 0x40 and (lo & 0x3F) >= 0x3C and lower_sp == 0x64
    if upper_reads_q or reads_flags or (not ibit and lu.wait_q):
        out.append("vuaot::syncQ(c);")
    if reads_flags:
        out.append("vuaot::commitFlags(c, c.cyc);")
    if reads_p:
        out.append("vuaot::syncP(c);")
    # ---- execute: upper into u*, lower into l* (both read the pre-pair registers) ----
    branch = {}
    utarget, ulanes = emit_upper(up, out)
    lres = None
    old_vi = None
    if not ibit:
        written = sorted(lu.vi_write)
        if lu.delays_branch and written:
            old_vi = written[0]
            out.append(f"const int32_t oldVi = s.vi[{old_vi}];")
        lres = emit_lower(lo, pc, out, branch)
    else:
        imm = struct.unpack("<f", struct.pack("<I", lo))[0]
        out.append(f"{{ uint32_t ib = 0x{lo:08x}u; float iv; std::memcpy(&iv, &ib, 4); s.i = vuaot::nop_(iv); }}")
        del imm
    # ---- write back (upper wins over a lower write to the same VF register) ----
    if utarget:
        kind, reg = utarget if isinstance(utarget, tuple) else (utarget, None)
        if utarget == "acc" or (isinstance(utarget, tuple) and utarget[0] == "acc"):
            for k in ulanes:
                out.append(f"s.acc[{k}] = u{k};")
        elif reg:
            for k in ulanes:
                out.append(f"s.vf[{reg}][{k}] = u{k};")
    if lres:
        (_, lreg), llanes = lres
        uw = utarget[1] if isinstance(utarget, tuple) and utarget[0] == "vf" else None
        if lreg and lreg != uw:
            for k in llanes:
                out.append(f"s.vf[{lreg}][{k}] = l{k};")
    # ---- ready cycles for later readers ----
    if uu.vf_write and uu.vf_write[0]:
        for k in comps(uu.vf_write[1]):
            out.append(f"c.vfReady[{uu.vf_write[0]}][{k}] = c.cyc + {uu.latency};")
    for k in comps(uu.acc_write):
        out.append(f"c.accReady[{k}] = c.cyc + 1;")
    if lu.vf_write and lu.vf_write[0]:
        for k in comps(lu.vf_write[1]):
            out.append(f"c.vfReady[{lu.vf_write[0]}][{k}] = c.cyc + {lu.latency};")
    for r in sorted(lu.vi_write):
        out.append(f"c.viReady[{r}] = c.cyc + {lu.vi_latency or lu.latency};")
    if old_vi is not None:
        out.append(f"c.bkCyc = c.cyc + 1; c.bkReg = {old_vi}; c.bkVal = oldVi;")
    out.append("c.cyc += 1;")
    # ---- control flow: E-bit end, branch from the previous pair, this pair's branch ----
    out.append("if (c.ebitPending) { c.ebitPending = false; c.pc = 0x%04xu; return vuaot::ExitEnd; }" % ((pc + 8) & 0x3FFF))
    if ebit:
        out.append("c.ebitPending = true;")
    if "branch" in branch:
        cond, target_expr, _ = branch["branch"]
        out.append(f"if ({cond}) {{ c.brTaken = true; c.brTarget = {target_expr}; }}")
        if "link" in branch:
            vit, link = branch["link"]
            if vit:
                out.append(f"s.vi[{vit}] = {link};")
    out.append("}")
    return "\n".join(out), "branch" in branch


def emit_program(name, base, code):
    pairs = len(code) // 8
    end = base + pairs * 8
    body = []
    for i in range(pairs):
        pc = base + i * 8
        lo, up = struct.unpack_from("<II", code, i * 8)
        try:
            text, is_branch = emit_pair(pc, lo, up, None)
        except Unsupported as ex:
            text = (f"L_{pc:04x}: {{ /* unsupported: {ex} */ c.pc = 0x{pc:04x}u; return vuaot::ExitFallback; }}")
            is_branch = False
        body.append(text)
        # After the delay slot of a taken branch, jump. The delay slot is the pair after a branch.
        prev_branch = False
        if i > 0:
            plo, pup = struct.unpack_from("<II", code, (i - 1) * 8)
            prev_branch = not (pup & 0x80000000) and 0x20 <= ((plo >> 25) & 0x7F) <= 0x2F
        if prev_branch:
            body.append("if (c.brTaken) { c.brTaken = false; target = c.brTarget; "
                        f"if (c.cyc >= c.budget) {{ c.pc = target; return vuaot::ExitBudget; }} goto dispatch; }}")
        if is_branch and i == pairs - 1:
            # Delay slot is outside this payload: let the next program (or the interpreter) run it.
            body.append(f"c.pc = 0x{(pc + 8) & 0x3FFF:04x}u; return vuaot::ExitLeave;")
    cases = "\n".join(f"case 0x{base + i * 8:04x}u: goto L_{base + i * 8:04x};" for i in range(pairs))
    return f"""
uint32_t {name}(vuaot::Context &c, uint32_t pc)
{{
    VU1State &s = *c.s;
    uint32_t target = pc;
dispatch:
    switch (target)
    {{
{cases}
    default:
        c.pc = target;
        return vuaot::ExitLeave;
    }}
{chr(10).join(body)}
    c.pc = 0x{end & 0x3FFF:04x}u;
    return vuaot::ExitLeave;
}}
"""


def extract_mpg(elf_path):
    e = Elf(elf_path)
    data = open(elf_path, "rb").read()
    found = {}
    # VIF packets with inline MPG: .vudata holds the VU1 programs, .vutext the VU0 ones.
    for section in (".vudata", ".vutext"):
        sec = e.section(section)
        buf = data[sec.offset:sec.offset + sec.size]
        for i in range(0, len(buf) - 4, 4):
            w = struct.unpack_from("<I", buf, i)[0]
            if (w >> 24) & 0x7F != 0x4A:
                continue
            num = (w >> 16) & 0xFF or 256
            base = (w & 0xFFFF) * 8
            payload = buf[i + 4:i + 4 + num * 8]
            if len(payload) != num * 8 or base + num * 8 > 0x4000:
                continue
            key = (base, fnv1a(payload))
            found.setdefault(key, (base, payload, sec.addr + i))
    return list(found.values())


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    elf, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    progs = extract_mpg(elf)
    header = ('// Generated by tools/vu_recomp.py from the game ELF. Do not commit.\n'
              '#include "runtime/vu_aot.h"\n#include <cfloat>\n#include <cmath>\n#include <cstring>\n'
              '#include <cstdint>\n\n')
    reg = [header]
    decls = []
    entries = []
    unsupported = 0
    for n, (base, payload, src) in enumerate(sorted(progs, key=lambda p: (p[2]))):
        name = f"vu1_prog_{n:02d}_{base:04x}"
        text = emit_program(name, base, payload)
        unsupported += text.count("/* unsupported")
        with open(os.path.join(outdir, f"{name}.cpp"), "w", newline="\n") as f:
            f.write(header + f"// MPG payload at ELF 0x{src:08x}: {len(payload) // 8} pairs loaded at VU1 0x{base:04x}\n")
            f.write(text)
        decls.append(f"uint32_t {name}(vuaot::Context &c, uint32_t pc);")
        entries.append(f"    {{0x{base:04x}u, {len(payload)}u, 0x{fnv1a(payload):016x}ull, &{name}}},")
    reg.append("\n".join(decls))
    reg.append("\n\nstatic const vuaot::Program kPrograms[] = {\n" + "\n".join(entries) + "\n};\n\n")
    reg.append("void drakengardRegisterVuPrograms()\n{\n"
               "    vuaot::registerPrograms(kPrograms, sizeof(kPrograms) / sizeof(kPrograms[0]));\n}\n")
    with open(os.path.join(outdir, "vu1_registry.cpp"), "w", newline="\n") as f:
        f.write("".join(reg))
    print(f"{len(progs)} VU1 programs, {sum(len(p[1]) // 8 for p in progs)} pairs, "
          f"{unsupported} unsupported pairs -> {outdir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
