#!/usr/bin/env python3
"""Diff a PS2X_PKTORACLE pair (oracle_before/after .bin/.txt): which pages changed, and for the frame /
Z buffers of the dump's register state, which bits, plus bit-level hypotheses for the depth mask.

    venv/bin/python tools/oracle_diff.py <dir>
"""
import sys
import numpy as np
import gsvram as g


def main(d):
    b = g.Vram(d + '/oracle_before.bin'); a = g.Vram(d + '/oracle_after.bin')
    rb = g.regs(d + '/oracle_before.txt'); ra = g.regs(d + '/oracle_after.txt')
    diff = np.nonzero(b.raw != a.raw)[0]
    print('changed bytes', diff.size)
    if diff.size:
        pages = np.bincount(diff >> 13, minlength=512)
        print('changed pages (block addr: bytes):', [(hex(p * 32), int(n)) for p, n in enumerate(pages) if n])
    for c in ('ctx1', 'ctx2'):
        print(c, 'before', g.frame_fields(rb[c]['frame']), g.zbuf_fields(rb[c]['zbuf']), 'tex0 %016x' % rb[c]['tex0'])
        print(c, 'after ', g.frame_fields(ra[c]['frame']), g.zbuf_fields(ra[c]['zbuf']), 'tex0 %016x' % ra[c]['tex0'])
    for fb in (0x0, 0xe00):
        Fb = b.ct32(fb, 8, 512, 448); Fa = a.ct32(fb, 8, 512, 448)
        x = Fb ^ Fa
        if not x.any():
            print('frame', hex(fb), 'unchanged'); continue
        print('frame', hex(fb), 'rgb changed px', int(((x & 0xffffff) != 0).sum()), 'alpha changed px', int((x >> 24 != 0).sum()),
              'alpha bits changed:', {bit: int(((x >> (24 + bit)) & 1).sum()) for bit in range(8)},
              'rgb bits changed:', {bit: int(((x >> bit) & 1).sum()) for bit in range(24) if ((x >> bit) & 1).sum()})
        ys, xs = np.nonzero(x); print('  changed box x %d..%d y %d..%d' % (xs.min(), xs.max(), ys.min(), ys.max()))
        zb = 0x1c00
        Z = b.z32(zb, 8, 512, 448); z24 = Z & 0xffffff
        aa = (Fa >> 24) & 0xff; ab = (Fb >> 24) & 0xff
        m = x != 0
        for name, cand in [('z[23:22]', (z24 >> 22) & 3), ('z[15:14]', (z24 >> 14) & 3), ('z[7:6]', (z24 >> 6) & 3), ('z[31:30]', (Z >> 30) & 3),
                           ('z[23:16]>>6', ((z24 >> 16) & 0xff) >> 6), ('z[15:8]>>6', ((z24 >> 8) & 0xff) >> 6)]:
            print('  after.alpha[7:6] == %s: changed px %.2f%%  all px %.2f%%' % (name, (cand == (aa >> 6))[m].mean() * 100 if m.any() else 0, (cand == (aa >> 6)).mean() * 100))
        print('  alpha[5:0] preserved: %.2f%%' % (((ab & 0x3f) == (aa & 0x3f)).mean() * 100))
        # 16-bit view: which halves changed and their bits
        Hb = b.ct16(fb, 8, 512, 896); Ha = a.ct16(fb, 8, 512, 896); hx = Hb ^ Ha
        ZH = b.z16(zb, 8, 512, 896)
        print('  ct16 halves changed', int((hx != 0).sum()), 'bits', {bit: int(((hx >> bit) & 1).sum()) for bit in range(16) if ((hx >> bit) & 1).sum()})
        mh = hx != 0
        if mh.any():
            for name, cand in [('zh[15:14]', (ZH >> 14) & 3), ('zh[7:6]', (ZH >> 6) & 3)]:
                print('  ha[15:14] == %s on changed halves: %.2f%%' % (name, ((cand == ((Ha >> 14) & 3))[mh]).mean() * 100))
            ys, xs = np.nonzero(mh); print('  changed halves box x %d..%d y %d..%d (rows even %d odd %d)' % (xs.min(), xs.max(), ys.min(), ys.max(), int((ys % 2 == 0).sum()), int((ys % 2 == 1).sum())))
    Zb = b.z32(0x1c00, 8, 512, 448); Za = a.z32(0x1c00, 8, 512, 448)
    zx = Zb ^ Za
    print('Z changed px', int((zx != 0).sum()), 'bits', {bit: int(((zx >> bit) & 1).sum()) for bit in range(32) if ((zx >> bit) & 1).sum()})


if __name__ == '__main__':
    main(sys.argv[1])
