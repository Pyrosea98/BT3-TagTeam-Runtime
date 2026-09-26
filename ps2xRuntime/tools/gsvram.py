#!/usr/bin/env python3
"""GS VRAM views over a raw 4 MB dump (ps2x_pgs::dumpVramRaw / PS2X_STEPORACLE).

The swizzles are the runtime's own (include/runtime/ps2_gs_psmct32.h, ps2_gs_psmct16.h, and the Z32
variant used by ps2_gs_pgs.cpp: the 32-bit block id xor 0x18). Block addresses are in 256-byte
blocks (the units of FRAME.FBP*32, ZBUF.ZBP*32, TEX0.TBP0, CBP); widths in 64-pixel pages.

    v = Vram('oracle_before.bin')
    a = v.ct32(0xe00, 8, 512, 448)     # uint32 array [h][w] (A<<24|B<<16|G<<8|R)
    z = v.z32(0x1c00, 8, 512, 448)     # uint32 Z words
    h = v.ct16(0xe00, 8, 512, 896)     # uint16 halves of the CT32 frame, as the game's CT16 view sees it
    zh = v.z16(0x1c00, 8, 512, 896)    # 16-bit halves of Z through the PSMZ16 layout
"""
import numpy as np

BLOCK32 = np.array([[0, 1, 4, 5, 16, 17, 20, 21], [2, 3, 6, 7, 18, 19, 22, 23],
                    [8, 9, 12, 13, 24, 25, 28, 29], [10, 11, 14, 15, 26, 27, 30, 31]], dtype=np.uint32)
COLUMN32 = np.array([[0, 1, 4, 5, 8, 9, 12, 13], [2, 3, 6, 7, 10, 11, 14, 15],
                     [16, 17, 20, 21, 24, 25, 28, 29], [18, 19, 22, 23, 26, 27, 30, 31],
                     [32, 33, 36, 37, 40, 41, 44, 45], [34, 35, 38, 39, 42, 43, 46, 47],
                     [48, 49, 52, 53, 56, 57, 60, 61], [50, 51, 54, 55, 58, 59, 62, 63]], dtype=np.uint32)
BLOCK16 = np.array([[0, 2, 8, 10], [1, 3, 9, 11], [4, 6, 12, 14], [5, 7, 13, 15],
                    [16, 18, 24, 26], [17, 19, 25, 27], [20, 22, 28, 30], [21, 23, 29, 31]], dtype=np.uint32)
BLOCK16S = np.array([[0, 2, 16, 18], [1, 3, 17, 19], [8, 10, 24, 26], [9, 11, 25, 27],
                     [4, 6, 20, 22], [5, 7, 21, 23], [12, 14, 28, 30], [13, 15, 29, 31]], dtype=np.uint32)
BLOCKZ16 = np.array([[24, 26, 16, 18], [25, 27, 17, 19], [28, 30, 20, 22], [29, 31, 21, 23],
                     [8, 10, 0, 2], [9, 11, 1, 3], [12, 14, 4, 6], [13, 15, 5, 7]], dtype=np.uint32)
BLOCKZ16S = np.array([[24, 26, 8, 10], [25, 27, 9, 11], [16, 18, 0, 2], [17, 19, 1, 3],
                      [28, 30, 12, 14], [29, 31, 13, 15], [20, 22, 4, 6], [21, 23, 5, 7]], dtype=np.uint32)
COLUMN16 = np.array([[0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27],
                     [4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31]], dtype=np.uint32)
VRAM_MASK = (4 << 20) - 1


def addr32(block, width, x, y, z=False):
    """Byte address of pixel (x, y) in a 32-bit layout (PSMCT32, or PSMZ32 with z=True). x, y arrays."""
    width = width if width else 1
    page = (block >> 5) + (y >> 5) * width + (x >> 6)
    bt = BLOCK32[(y >> 3) & 3, (x >> 3) & 7]
    if z:
        bt = bt ^ 0x18
    block_id = (block & 0x1F) + bt
    page_off = (block_id >> 5) << 13
    local = block_id & 0x1F
    return ((page << 13) + page_off + local * 256 + COLUMN32[y & 7, x & 7] * 4) & VRAM_MASK


def addr16(block, width, x, y, table):
    width = width if width else 1
    page = (block >> 5) + (y >> 6) * width + (x >> 6)
    block_id = (block & 0x1F) + table[(y >> 3) & 7, (x >> 4) & 3]
    page_off = (block_id >> 5) << 13
    local = block_id & 0x1F
    col = ((y >> 1) & 3) * 64
    return ((page << 13) + page_off + local * 256 + col + COLUMN16[y & 1, x & 15] * 2) & VRAM_MASK


class Vram:
    def __init__(self, path):
        self.raw = np.fromfile(path, dtype=np.uint8)
        assert self.raw.size == 4 << 20, path
        self.u32 = self.raw.view(np.uint32)
        self.u16 = self.raw.view(np.uint16)

    def _grid(self, w, h):
        y, x = np.mgrid[0:h, 0:w]
        return x.astype(np.uint32), y.astype(np.uint32)

    def ct32(self, block, width, w, h):
        x, y = self._grid(w, h)
        return self.u32[addr32(block, width, x, y) >> 2]

    def z32(self, block, width, w, h):
        x, y = self._grid(w, h)
        return self.u32[addr32(block, width, x, y, z=True) >> 2]

    def ct16(self, block, width, w, h, s=False):
        x, y = self._grid(w, h)
        return self.u16[addr16(block, width, x, y, BLOCK16S if s else BLOCK16) >> 1]

    def z16(self, block, width, w, h, s=False):
        x, y = self._grid(w, h)
        return self.u16[addr16(block, width, x, y, BLOCKZ16S if s else BLOCKZ16) >> 1]

    def t8h(self, block, width, w, h):
        """PSMT8H: the top byte of each 32-bit word in the CT32 layout."""
        return (self.ct32(block, width, w, h) >> 24).astype(np.uint8)


def rgba_png(a, path):
    """Write a uint32 A<<24|B<<16|G<<8|R array as an RGBA PNG (imagemagick-free: PPM + PGM)."""
    h, w = a.shape
    rgb = np.stack([(a & 0xFF), (a >> 8) & 0xFF, (a >> 16) & 0xFF], axis=-1).astype(np.uint8)
    with open(path + '.ppm', 'wb') as f:
        f.write(b'P6\n%d %d\n255\n' % (w, h)); f.write(rgb.tobytes())
    with open(path + '_a.pgm', 'wb') as f:
        f.write(b'P5\n%d %d\n255\n' % (w, h)); f.write(((a >> 24) & 0xFF).astype(np.uint8).tobytes())


def regs(path):
    """Parse the .txt register dump into {ctx1: {...}, ctx2: {...}, global: {...}} of ints."""
    out = {}
    for line in open(path):
        parts = line.split()
        if not parts:
            continue
        key = parts[0] if parts[0].startswith('ctx') else 'global'
        d = out.setdefault(key, {})
        it = iter(parts[1:] if key != 'global' else parts)
        for name in it:
            d[name] = int(next(it), 16)
    return out


def frame_fields(v):
    return {'fbp': (v & 0x1FF) * 32, 'fbw': (v >> 16) & 0x3F, 'psm': (v >> 24) & 0x3F, 'fbmsk': (v >> 32) & 0xFFFFFFFF}


def zbuf_fields(v):
    return {'zbp': (v & 0x1FF) * 32, 'psm': (v >> 24) & 0xF, 'zmsk': (v >> 32) & 1}


if __name__ == '__main__':
    import sys
    v = Vram(sys.argv[1])
    r = regs(sys.argv[1].replace('.bin', '.txt'))
    for c in ('ctx1', 'ctx2'):
        print(c, frame_fields(r[c]['frame']), zbuf_fields(r[c]['zbuf']))
