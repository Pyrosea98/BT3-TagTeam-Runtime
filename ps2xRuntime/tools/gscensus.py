#!/usr/bin/env python3
"""Offline census of a PS2X_GS_RECORD stream: which GS features a native renderer must implement.

Record layout (ps2_gs_gpu.cpp): 0x00 path u32len payload | 0x01 pad (vsync) | 0x02 u32 | 0x03 8192 bytes | 0x04 56 bytes.
Usage: gscensus.py file.gs [--frames a:b]
"""
import sys, struct, collections

PRIMN = ["point", "line", "lstrip", "tri", "tstrip", "tfan", "sprite", "?"]
PSMN = {0: "CT32", 1: "CT24", 2: "CT16", 10: "CT16S", 19: "T8", 20: "T4", 27: "T8H", 36: "T4HL", 44: "T4HH",
        48: "Z32", 49: "Z24", 50: "Z16", 58: "Z16S"}
REGN = {0: "PRIM", 1: "RGBAQ", 2: "ST", 3: "UV", 4: "XYZF2", 5: "XYZ2", 6: "TEX0_1", 7: "TEX0_2", 8: "CLAMP_1", 9: "CLAMP_2",
        0xA: "FOG", 0xC: "XYZF3", 0xD: "XYZ3", 0x14: "TEX1_1", 0x15: "TEX1_2", 0x16: "TEX2_1", 0x17: "TEX2_2",
        0x18: "XYOFFSET_1", 0x19: "XYOFFSET_2", 0x1A: "PRMODECONT", 0x1B: "PRMODE", 0x1C: "TEXCLUT", 0x22: "SCANMSK",
        0x34: "MIPTBP1_1", 0x35: "MIPTBP1_2", 0x36: "MIPTBP2_1", 0x37: "MIPTBP2_2", 0x3B: "TEXA", 0x3D: "FOGCOL",
        0x3F: "TEXFLUSH", 0x40: "SCISSOR_1", 0x41: "SCISSOR_2", 0x42: "ALPHA_1", 0x43: "ALPHA_2", 0x44: "DIMX", 0x45: "DTHE",
        0x46: "COLCLAMP", 0x47: "TEST_1", 0x48: "TEST_2", 0x49: "PABE", 0x4A: "FBA_1", 0x4B: "FBA_2", 0x4C: "FRAME_1",
        0x4D: "FRAME_2", 0x4E: "ZBUF_1", 0x4F: "ZBUF_2", 0x50: "BITBLTBUF", 0x51: "TRXPOS", 0x52: "TRXREG", 0x53: "TRXDIR",
        0x54: "HWREG", 0x60: "SIGNAL", 0x61: "FINISH", 0x62: "LABEL"}

class C:
    def __init__(self):
        self.reg = collections.Counter()          # (path, regname)
        self.prims = collections.Counter()        # (path, prim, tme, abe, iip, fst)
        self.tex0 = collections.Counter()         # (path, psm, tw, th, cpsm, csm, tfx, tcc)
        self.uploads = collections.Counter()      # (path, dpsm, w, h)
        self.upbytes = 0
        self.frames = collections.Counter()       # (fbp, fbw, psm)
        self.zbufs = collections.Counter()
        self.feedback = collections.Counter()     # tex0 tbp that hits a FRAME fbp written this frame
        self.alpha = collections.Counter(); self.test = collections.Counter(); self.clamp = collections.Counter()
        self.tex1 = collections.Counter(); self.misc = collections.Counter()
        self.vsync = 0; self.pkts = collections.Counter(); self.bytes = collections.Counter()
        self.localcopies = 0
        self.vertsPerFrame = []

    def note_tex0(self, path, v, state):
        tbp = v & 0x3FFF; tbw = (v >> 14) & 0x3F; psm = (v >> 20) & 0x3F; tw = (v >> 26) & 0xF; th = (v >> 30) & 0xF
        tcc = (v >> 34) & 1; tfx = (v >> 35) & 3; cbp = (v >> 37) & 0x3FFF; cpsm = (v >> 51) & 0xF; csm = (v >> 55) & 1
        self.tex0[(path, PSMN.get(psm, psm), 1 << tw, 1 << th, PSMN.get(cpsm, cpsm) if psm in (19, 20, 27, 36, 44) else "-", csm, tfx, tcc)] += 1
        for fbp in state["frames_written"]:
            if fbp <= tbp < fbp + 0x1000 or (tbp <= fbp < tbp + (1 << tw) * (1 << th) // 2048 + 1):
                self.feedback[(path, hex(tbp), PSMN.get(psm, psm), hex(fbp))] += 1
                break

def parse_packet(c, path, d, state):
    """GIF tag state persists across packets on a path (DIRECT IMAGE data spans many VIF packets)."""
    off = 0; n = len(d)
    c.pkts[path] += 1; c.bytes[path] += n
    ps = state["paths"].setdefault(path, {"nloop": 0, "flg": 0, "nreg": 1, "regs": 0, "eop": 0, "ri": 0, "li": 0})
    while off < n:
        if ps["nloop"] == 0:
            if off + 16 > n: break
            lo, hi = struct.unpack_from("<QQ", d, off); off += 16
            ps["nloop"] = lo & 0x7FFF; ps["eop"] = (lo >> 15) & 1; pre = (lo >> 46) & 1; ps["flg"] = (lo >> 58) & 3
            ps["nreg"] = (lo >> 60) & 0xF or 16; ps["regs"] = hi; ps["ri"] = 0; ps["li"] = 0
            if pre and ps["nloop"]: state["prim"] = (lo >> 47) & 0x7FF
            if ps["flg"] == 3: ps["flg"] = 2
            continue
        flg = ps["flg"]
        if flg == 2:   # IMAGE: nloop qwords of pixels
            take = min(ps["nloop"] * 16, n - off)
            c.upbytes += take; off += take; ps["nloop"] -= take // 16
            if take % 16: ps["nloop"] = 0
            continue
        if flg == 1:   # REGLIST: 8 bytes per register, nreg per loop, padded to qword at the end
            if off + 8 > n: break
            v, = struct.unpack_from("<Q", d, off); off += 8
            desc = (ps["regs"] >> (4 * ps["ri"])) & 0xF
            handle_reg(c, path, desc if desc != 0xE else 0x0F, v, state, desc, None)
            ps["ri"] += 1
            if ps["ri"] == ps["nreg"]:
                ps["ri"] = 0; ps["nloop"] -= 1
                if ps["nloop"] == 0 and ((ps["nreg"] * (ps["li"] + 1)) & 1): off += 8
                ps["li"] += 1
            continue
        if off + 16 > n: break   # PACKED
        qlo, qhi = struct.unpack_from("<QQ", d, off); off += 16
        desc = (ps["regs"] >> (4 * ps["ri"])) & 0xF
        if desc == 0xE:
            handle_reg(c, path, qhi & 0xFF, qlo, state, None, None)
        elif desc == 0x1:
            handle_reg(c, path, 1, 0, state, 1, None)
        elif desc in (0x4, 0x5):
            handle_reg(c, path, desc, 0, state, desc, (qhi >> 47) & 1)
        elif desc == 0x0:
            state["prim"] = qlo & 0x7FF
            handle_reg(c, path, 0, qlo, state, 0, None)
        else:
            handle_reg(c, path, desc, qlo, state, desc, None)
        ps["ri"] += 1
        if ps["ri"] == ps["nreg"]: ps["ri"] = 0; ps["nloop"] -= 1

def handle_reg(c, path, addr, v, state, packedDesc, adc):
    name = REGN.get(addr, hex(addr))
    c.reg[(path, name)] += 1
    if addr in (4, 5, 0xC, 0xD):
        if adc: return
        p = state["prim"]; kind = p & 7
        c.prims[(path, PRIMN[kind], "tme" if (p >> 4) & 1 else "flat", "abe" if (p >> 6) & 1 else "-", "iip" if (p >> 3) & 1 else "-",
                 "uv" if (p >> 8) & 1 else "st", "fge" if (p >> 5) & 1 else "-", "aa1" if (p >> 7) & 1 else "-", "ctx" + str((p >> 9) & 1))] += 1
        state["verts"] += 1
    elif addr == 0:
        state["prim"] = v & 0x7FF
    elif addr in (6, 7):
        c.note_tex0(path, v, state)
        state["tex0"][addr - 6] = v
    elif addr in (0x4C, 0x4D):
        fbp = (v & 0x1FF) * 32; fbw = (v >> 16) & 0x3F; psm = (v >> 24) & 0x3F; fbmsk = (v >> 32) & 0xFFFFFFFF
        c.frames[(path, name, hex(fbp), fbw, PSMN.get(psm, psm), hex(fbmsk))] += 1
        state["frames_written"].add(fbp)
    elif addr in (0x4E, 0x4F):
        zbp = (v & 0x1FF) * 32; psm = (v >> 24) & 0xF; zmsk = (v >> 32) & 1
        c.zbufs[(path, name, hex(zbp), PSMN.get(psm | 48, psm), zmsk)] += 1
    elif addr == 0x50:
        state["blt"] = v
    elif addr == 0x52:
        state["trxreg"] = v
    elif addr == 0x53:
        blt = state.get("blt", 0); tr = state.get("trxreg", 0); xdir = v & 3
        dpsm = (blt >> 56) & 0x3F; dbp = ((blt >> 32) & 0x3FFF); spsm = (blt >> 24) & 0x3F
        w = tr & 0xFFF; h = (tr >> 32) & 0xFFF
        if xdir == 0: c.uploads[(path, PSMN.get(dpsm, dpsm), w, h)] += 1; state["uploaded"].add(dbp)
        elif xdir == 2: c.localcopies += 1; c.misc[("localcopy", PSMN.get(spsm, spsm), PSMN.get(dpsm, dpsm), w, h)] += 1
        elif xdir == 1: c.misc[("download", PSMN.get(spsm, spsm), w, h)] += 1
    elif addr in (0x42, 0x43):
        c.alpha[(path, name, v & 3, (v >> 2) & 3, (v >> 4) & 3, (v >> 6) & 3, (v >> 32) & 0xFF)] += 1
    elif addr in (0x47, 0x48):
        c.test[(path, name, "ate" if v & 1 else "-", (v >> 1) & 7, (v >> 4) & 0xFF, (v >> 12) & 3, "date" if (v >> 14) & 1 else "-", (v >> 15) & 1,
                "zte" if (v >> 16) & 1 else "-", (v >> 17) & 3)] += 1
    elif addr in (8, 9):
        c.clamp[(path, name, v & 3, (v >> 2) & 3)] += 1
    elif addr in (0x14, 0x15):
        c.tex1[(path, name, "lcm" if v & 1 else "-", (v >> 2) & 7, "mag" if (v >> 5) & 1 else "-", (v >> 6) & 7, (v >> 9) & 3, (v >> 19) & 3, (v >> 32) & 0xFFF)] += 1
    elif addr in (0x3B, 0x3D, 0x45, 0x46, 0x49, 0x4A, 0x4B, 0x1C, 0x22, 0x1A, 0x1B, 0x44, 0xA):
        c.misc[(path, name, hex(v))] += 1

def main():
    path = sys.argv[1]
    fr = None
    if len(sys.argv) > 3 and sys.argv[2] == "--frames":
        a, b = sys.argv[3].split(":"); fr = (int(a), int(b))
    d = open(path, "rb").read()
    c = C(); state = {"prim": 0, "tex0": [0, 0], "frames_written": set(), "uploaded": set(), "verts": 0, "paths": {}}
    off = 0; frame = 0
    while off < len(d):
        t = d[off]; off += 1
        if t == 0:
            p = d[off]; ln, = struct.unpack_from("<I", d, off + 1); off += 5
            if fr is None or fr[0] <= frame < fr[1]:
                parse_packet(c, p, d[off:off + ln], state)
            off += ln
        elif t == 1:
            off += 1; frame += 1
            if fr is None or fr[0] <= frame < fr[1]:
                c.vsync += 1; c.vertsPerFrame.append(state["verts"])
            state["verts"] = 0; state["frames_written"] = set()
        elif t == 2: off += 4
        elif t == 3: off += 8192
        elif t == 4: off += 56
        else:
            print("unknown record", t, "at", off - 1); break
    n = max(1, c.vsync)
    print(f"{frame} frames in file, {c.vsync} analysed; upload {c.upbytes / 1e6 / n:.2f} MB/frame; local copies {c.localcopies / n:.1f}/frame")
    print("verts/frame min/avg/max:", min(c.vertsPerFrame or [0]), sum(c.vertsPerFrame) // n, max(c.vertsPerFrame or [0]))
    def dump(title, cnt, top=40):
        print(f"\n== {title} (per frame)")
        for k, v in sorted(cnt.items(), key=lambda kv: -kv[1])[:top]:
            print(f"  {v / n:10.1f}  {k}")
    dump("packets by path", c.pkts); dump("bytes by path", c.bytes)
    dump("registers by path", c.reg, 80)
    dump("primitives (path, kind, tme, abe, iip, uv/st, fog, aa1, ctx)", c.prims)
    dump("TEX0 (path, psm, w, h, cpsm, csm, tfx, tcc)", c.tex0)
    dump("uploads (path, dpsm, w, h)", c.uploads)
    dump("FRAME (path, reg, fbp, fbw, psm, fbmsk)", c.frames); dump("ZBUF", c.zbufs)
    dump("feedback: TEX0 reading a FRAME written this frame (path, tbp, psm, fbp)", c.feedback)
    dump("ALPHA (path, reg, A, B, C, D, FIX)", c.alpha); dump("TEST (path, reg, ate, atst, aref, afail, date, datm, zte, ztst)", c.test)
    dump("CLAMP (path, reg, wms, wmt)", c.clamp); dump("TEX1 (path, reg, lcm, mxl, mmag, mmin, mtba, l, k)", c.tex1)
    dump("misc", c.misc, 60)

main()
