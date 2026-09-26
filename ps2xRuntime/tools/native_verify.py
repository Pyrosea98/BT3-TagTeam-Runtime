#!/usr/bin/env python3
"""Verify natively implemented post-chain steps (PS2X_POSTNATIVE) against the pinned rules.

For each oracle pair in <dir> (oracle_<lo>_before/after), compute the expected 'after' planes from the
'before' dump with the rule pinned in docs/SEAM-POSTCHAIN.md and compare with the real 'after'.

    venv/bin/python tools/native_verify.py <dir>
"""
import sys, os
import numpy as np
import gsvram as g


def ch(x, s): return ((x >> s) & 0xff).astype(np.int32)


def clut(v, cbp):
    c = v.ct32(cbp, 1, 16, 16).reshape(-1); idx = np.arange(256)
    return c[(idx & ~0x18) | ((idx & 8) << 1) | ((idx & 16) >> 1)]


def scene_fbp(regs):
    return g.frame_fields(regs['ctx1']['frame'])['fbp']


def report(name, ok_mask, extra=''):
    print('%-28s %7.3f%% exact %s' % (name, ok_mask.mean() * 100, extra))


def main(d):
    for lo, name in [('109848', 'depth mask'), ('106ba8', 'alpha clear'), ('24b118', 'Z top byte'), ('245a50', 'blur weight'), ('103070', 'glow')]:
        pb, pa = d + f'/oracle_{lo}_before.bin', d + f'/oracle_{lo}_after.bin'
        if not (os.path.exists(pb) and os.path.exists(pa)):
            print('%-28s (no pair)' % name); continue
        b = g.Vram(pb); a = g.Vram(pa); rb = g.regs(pb.replace('.bin', '.txt'))
        fbp = scene_fbp(rb)
        F0 = b.ct32(fbp, 8, 512, 448); F1 = a.ct32(fbp, 8, 512, 448)
        A0 = ch(F0, 24); A1 = ch(F1, 24)
        rgb_same = ((F0 ^ F1) & 0xffffff) == 0
        if lo == '109848':
            Z = b.z32(0x1c00, 8, 512, 448); exp = (Z >> 8) & 0xff
            report(name, (A1 == exp) & rgb_same, 'fbp %#x' % fbp)
        elif lo == '106ba8':
            report(name, (A1 == 0) & rgb_same)
        elif lo == '24b118':
            zc1 = ch(a.ct32(0x1c00, 8, 512, 448), 24)
            report(name, (zc1 == A0) & (F0 == F1))
        elif lo == '245a50':
            zc = ch(a.ct32(0x1c00, 8, 512, 448), 24); c84 = ch(clut(a, 0x3e84), 24)
            report(name, (A1 == c84[zc]) & rgb_same)
        elif lo == '103070':
            T1 = a.ct32(0x2a00, 4, 256, 256)
            okc = np.ones((448, 512), dtype=bool)
            for s in (0, 8, 16):
                P = ch(F0, s).astype(np.float64)
                Tc = ch(T1, s)[:224].astype(np.float64)
                Y, X = np.mgrid[0:448, 0:512]
                x0 = np.minimum(X // 2, 255); y0 = np.minimum(Y // 2, 223); x1 = np.minimum(x0 + 1, 255); y1 = np.minimum(y0 + 1, 223)
                U = np.where((X & 1) & (Y & 1), (Tc[y0, x0] + Tc[y0, x1] + Tc[y1, x0] + Tc[y1, x1]) / 4,
                             np.where(X & 1, (Tc[y0, x0] + Tc[y0, x1]) / 2, np.where(Y & 1, (Tc[y0, x0] + Tc[y1, x0]) / 2, Tc[y0, x0])))
                exp = np.floor(P + (np.floor(U + 0.5) - P) * A0 / 128)
                okc &= (exp == ch(F1, s))
            report(name + ' composite', okc, '(against the produced 0x2a00)')
            box = lambda P: (P[0::2, 0::2] + P[1::2, 0::2] + P[0::2, 1::2] + P[1::2, 1::2] + 2) // 4
            okd = np.ones((224, 256), dtype=bool)
            for s in (0, 8, 16): okd &= (box(ch(F0, s))[:224] == ch(T1, s)[:224])
            report(name + ' downscale(box)', okd, '(box rule; the game itself differs by filter rounding)')


if __name__ == '__main__':
    main(sys.argv[1])
