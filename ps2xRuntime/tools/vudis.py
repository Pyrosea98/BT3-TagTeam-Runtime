#!/usr/bin/env python3
"""VU1 microcode disassembler.

    vudis.py <image.bin> [start_hex] [end_hex]      raw 16 KB VU1 image (games/bt3/work/vu1/*.bin)
    vudis.py --pairs <pairs.txt> [start] [end]       lines of "pc lo up" hex words

Prints one line per instruction pair: pc, upper op, lower op. Flags on the upper word:
I (lower is a float immediate), E (end after the next pair), M, D, T.
"""
import struct, sys

DEST = lambda w: ''.join(c for c, b in zip('xyzw', (24, 23, 22, 21)) if (w >> b) & 1)
BC = 'xyzw'

def vf(n): return f'vf{n:02d}'
def vi(n): return f'vi{n:02d}'

UP_BASE = {0x1C: 'MULq', 0x1D: 'MAXi', 0x1E: 'MULi', 0x1F: 'MINIi', 0x20: 'ADDq', 0x21: 'MADDq', 0x22: 'ADDi',
           0x23: 'MADDi', 0x24: 'SUBq', 0x25: 'MSUBq', 0x26: 'SUBi', 0x27: 'MSUBi', 0x28: 'ADD', 0x29: 'MADD',
           0x2A: 'MUL', 0x2B: 'MAX', 0x2C: 'SUB', 0x2D: 'MSUB', 0x2E: 'OPMSUB', 0x2F: 'MINI'}
UP_BC = {0: 'ADD', 1: 'SUB', 2: 'MADD', 3: 'MSUB', 4: 'MAX', 5: 'MINI', 6: 'MUL'}
UP_SP = {0x10: 'ITOF0', 0x11: 'ITOF4', 0x12: 'ITOF12', 0x13: 'ITOF15', 0x14: 'FTOI0', 0x15: 'FTOI4', 0x16: 'FTOI12',
         0x17: 'FTOI15', 0x1C: 'MULAq', 0x1D: 'ABS', 0x1E: 'MULAi', 0x1F: 'CLIPw', 0x20: 'ADDAq', 0x21: 'MADDAq',
         0x22: 'ADDAi', 0x23: 'MADDAi', 0x24: 'SUBAq', 0x25: 'MSUBAq', 0x26: 'SUBAi', 0x27: 'MSUBAi', 0x28: 'ADDA',
         0x29: 'MADDA', 0x2A: 'MULA', 0x2B: 'NOP', 0x2C: 'SUBA', 0x2D: 'MSUBA', 0x2E: 'OPMULA', 0x2F: 'NOP'}
UP_SP_BC = {0: 'ADDA', 1: 'SUBA', 2: 'MADDA', 3: 'MSUBA', 6: 'MULA'}

def dis_upper(w):
    op = w & 0x3F
    fd, fs, ft = (w >> 6) & 31, (w >> 11) & 31, (w >> 16) & 31
    d = DEST(w)
    if op < 0x1C:
        name = UP_BC[op >> 2] + BC[op & 3]
        return f'{name}.{d} {vf(fd)}, {vf(fs)}, {vf(ft)}{BC[op & 3]}'
    if op < 0x3C:
        name = UP_BASE[op]
        if name.endswith('q'): return f'{name}.{d} {vf(fd)}, {vf(fs)}, Q'
        if name.endswith('i'): return f'{name}.{d} {vf(fd)}, {vf(fs)}, I'
        return f'{name}.{d} {vf(fd)}, {vf(fs)}, {vf(ft)}'
    op2 = (((w >> 6) & 31) << 2) | (w & 3)
    if op2 < 0x10:
        name = UP_SP_BC[op2 >> 2] + BC[op2 & 3]
        return f'{name}.{d} ACC, {vf(fs)}, {vf(ft)}{BC[op2 & 3]}'
    if op2 in (0x18, 0x19, 0x1A, 0x1B):
        return f'MULA{BC[op2 & 3]}.{d} ACC, {vf(fs)}, {vf(ft)}{BC[op2 & 3]}'
    name = UP_SP.get(op2, f'UP?{op2:02x}')
    if name == 'NOP': return 'NOP'
    if name == 'CLIPw': return f'CLIPw.xyz {vf(fs)}, {vf(ft)}'
    if name.startswith(('ITOF', 'FTOI', 'ABS')): return f'{name}.{d} {vf(ft)}, {vf(fs)}'
    if name.endswith('q'): return f'{name}.{d} ACC, {vf(fs)}, Q'
    if name.endswith('i'): return f'{name}.{d} ACC, {vf(fs)}, I'
    return f'{name}.{d} ACC, {vf(fs)}, {vf(ft)}'

def s11(v): return v - 2048 if v & 0x400 else v
def s15(v): return v - 32768 if v & 0x4000 else v

LO_SP = {0x30: 'IADD', 0x31: 'ISUB', 0x32: 'IADDI', 0x34: 'IAND', 0x35: 'IOR'}
LO_SP2 = {0x30: 'MOVE', 0x31: 'MR32', 0x34: 'LQI', 0x35: 'SQI', 0x36: 'LQD', 0x37: 'SQD', 0x38: 'DIV', 0x39: 'SQRT',
          0x3A: 'RSQRT', 0x3B: 'WAITQ', 0x3C: 'MTIR', 0x3D: 'MFIR', 0x3E: 'ILWR', 0x3F: 'ISWR', 0x40: 'RNEXT',
          0x41: 'RGET', 0x42: 'RINIT', 0x43: 'RXOR', 0x64: 'MFP', 0x68: 'XTOP', 0x69: 'XITOP', 0x6C: 'XGKICK',
          0x70: 'ESADD', 0x71: 'ERSADD', 0x72: 'ELENG', 0x73: 'ERLENG', 0x74: 'EATANxy', 0x75: 'EATANxz',
          0x76: 'ESUM', 0x78: 'ESQRT', 0x79: 'ERSQRT', 0x7A: 'ERCPR', 0x7B: 'WAITP', 0x7C: 'ESIN', 0x7D: 'EATAN',
          0x7E: 'EEXP'}
FLAG = {0x10: 'FCEQ', 0x11: 'FCSET', 0x12: 'FCAND', 0x13: 'FCOR', 0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND',
        0x17: 'FSOR', 0x18: 'FMEQ', 0x1A: 'FMAND', 0x1B: 'FMOR', 0x1C: 'FCGET'}
BR = {0x20: 'B', 0x21: 'BAL', 0x24: 'JR', 0x25: 'JALR', 0x28: 'IBEQ', 0x29: 'IBNE', 0x2C: 'IBLTZ', 0x2D: 'IBGTZ',
      0x2E: 'IBLEZ', 0x2F: 'IBGEZ'}

def dis_lower(w, pc, upper):
    if (upper >> 31) & 1:
        return f'LOI {struct.unpack("<f", struct.pack("<I", w))[0]:g} (0x{w:08x})'
    op = (w >> 25) & 0x7F
    it, is_, id_ = (w >> 16) & 31, (w >> 11) & 31, (w >> 6) & 31
    d = DEST(w)
    imm11 = s11(w & 0x7FF)
    if op == 0x00: return f'LQ.{d} {vf(it)}, {imm11}({vi(is_)})'
    if op == 0x01: return f'SQ.{d} {vf(is_)}, {imm11}({vi(it)})'
    if op == 0x04: return f'ILW.{d} {vi(it)}, {imm11}({vi(is_)})'
    if op == 0x05: return f'ISW.{d} {vi(it)}, {imm11}({vi(is_)})'
    if op == 0x08: return f'IADDIU {vi(it)}, {vi(is_)}, {(w & 0x7FF) | (((w >> 21) & 15) << 11)}'
    if op == 0x09: return f'ISUBIU {vi(it)}, {vi(is_)}, {(w & 0x7FF) | (((w >> 21) & 15) << 11)}'
    if op in FLAG:   # FCAND/FCOR/FCEQ write vi01; FSxx/FMxx take it; FCSET/FSSET are immediates only
        if op in (0x10, 0x12, 0x13): return f'{FLAG[op]} vi01, 0x{w & 0xFFFFFF:x}'
        if op in (0x11, 0x15): return f'{FLAG[op]} 0x{w & 0xFFFFFF:x}'
        if op == 0x1C: return f'{FLAG[op]} {vi(it)}'
        return f'{FLAG[op]} {vi(it)}, 0x{w & 0xFFF:x}'
    if op in BR:
        tgt = (pc + 8 + imm11 * 8) & 0x3FFF
        n = BR[op]
        if n == 'B': return f'B 0x{tgt:x}'
        if n == 'BAL': return f'BAL {vi(it)}, 0x{tgt:x}'
        if n == 'JR': return f'JR {vi(is_)}'
        if n == 'JALR': return f'JALR {vi(it)}, {vi(is_)}'
        if n in ('IBEQ', 'IBNE'): return f'{n} {vi(it)}, {vi(is_)}, 0x{tgt:x}'
        return f'{n} {vi(is_)}, 0x{tgt:x}'
    if op == 0x40:
        o = w & 0x3F
        if o in LO_SP:
            if o == 0x32: return f'IADDI {vi(it)}, {vi(is_)}, {((w >> 6) & 31) - (32 if (w >> 10) & 1 else 0)}'
            return f'{LO_SP[o]} {vi(id_)}, {vi(is_)}, {vi(it)}'
        o2 = (((w >> 6) & 31) << 2) | (w & 3)
        n = LO_SP2.get(o2, f'LO?{o2:02x}')
        if n == 'MOVE': return f'MOVE.{d} {vf(it)}, {vf(is_)}'
        if n == 'MR32': return f'MR32.{d} {vf(it)}, {vf(is_)}'
        if n == 'LQI': return f'LQI.{d} {vf(it)}, ({vi(is_)}++)'
        if n == 'SQI': return f'SQI.{d} {vf(is_)}, ({vi(it)}++)'
        if n == 'LQD': return f'LQD.{d} {vf(it)}, (--{vi(is_)})'
        if n == 'SQD': return f'SQD.{d} {vf(is_)}, (--{vi(it)})'
        if n in ('DIV', 'SQRT', 'RSQRT'):
            fsf, ftf = BC[(w >> 21) & 3], BC[(w >> 23) & 3]
            if n == 'SQRT': return f'SQRT Q, {vf(it)}{ftf}'
            return f'{n} Q, {vf(is_)}{fsf}, {vf(it)}{ftf}'
        if n == 'MTIR': return f'MTIR {vi(it)}, {vf(is_)}{BC[(w >> 21) & 3]}'
        if n == 'MFIR': return f'MFIR.{d} {vf(it)}, {vi(is_)}'
        if n == 'ILWR': return f'ILWR.{d} {vi(it)}, ({vi(is_)})'
        if n == 'ISWR': return f'ISWR.{d} {vi(it)}, ({vi(is_)})'
        if n in ('XTOP', 'XITOP'): return f'{n} {vi(it)}'
        if n == 'XGKICK': return f'XGKICK {vi(is_)}'
        if n in ('RGET', 'RINIT', 'RXOR', 'RNEXT'): return f'{n}.{d} {vf(it)}, R'
        if n == 'MFP': return f'MFP.{d} {vf(it)}, P'
        if n in ('WAITQ', 'WAITP'): return n
        return f'{n} P, {vf(is_)}{BC[(w >> 21) & 3]}'
    if w == 0: return 'NOP'
    return f'LO?op{op:02x} 0x{w:08x}'

def main():
    a = sys.argv[1:]
    pairs = []
    if a and a[0] == '--pairs':
        for line in open(a[1]):
            p = line.split()
            if len(p) >= 3: pairs.append((int(p[0], 16), int(p[1], 16), int(p[2], 16)))
        a = a[2:]
    else:
        img = open(a[0], 'rb').read(); a = a[1:]
        for pc in range(0, len(img) - 7, 8):
            lo, up = struct.unpack_from('<II', img, pc)
            pairs.append((pc, lo, up))
    start = int(a[0], 16) if a else 0
    end = int(a[1], 16) if len(a) > 1 else 0x4000
    for pc, lo, up in pairs:
        if pc < start or pc >= end: continue
        if lo == 0 and up == 0: continue
        flags = ''.join(c for c, b in zip('IEMDT', (31, 30, 29, 28, 27)) if (up >> b) & 1)
        print(f'{pc:04x}  {flags:5s} {dis_upper(up):40s} | {dis_lower(lo, pc, up)}')

if __name__ == '__main__':
    main()
