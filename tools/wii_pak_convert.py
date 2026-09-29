#!/usr/bin/env python3
"""Convert a Wii BT3 ".pak" into the PS2 ".pak" layout (BT3-Recomp / DragonNet research).

The two games ship the same container, only byte-swapped:

    [count]                      u32
    [offset[0] .. offset[count]] u32   (count+1 boundaries, count files)

On the PS2 those words are little-endian; on the Wii they are big-endian. The file payloads
are the same compressed blobs the PS2 stores in ".cpak" files:

    [uncompressedSize] u32
    [compressedSize]   u32
    <compressed stream>

so this tool only rewrites the headers (and, optionally, the stream words) -- it does not
touch the compression itself. The PS2 DragonNet_UP/US.pak in the retail image are 64-byte
stubs (same 3-file header, every file empty); pointing them at the Wii payload is the first
step of bringing the cut Dragon Net assets back.

Usage:
    wii_pak_convert.py <wii.pak> [--out <ps2.pak>] [--swap-stream] [--dry-run]
"""
import argparse
import struct
import sys
from pathlib import Path

HEADER_ALIGN = 0x40  # the stub's data start; a 252-file pak uses 0x400, i.e. align64


def align(value: int, to: int) -> int:
    return (value + to - 1) // to * to


def be32(buf: bytes, off: int) -> int:
    return struct.unpack_from(">I", buf, off)[0]


def parse_wii(buf: bytes):
    count = be32(buf, 0)
    if count == 0 or count > 4096:
        raise ValueError(f"implausible file count {count}")
    offsets = [be32(buf, 4 + 4 * i) for i in range(count + 1)]
    if offsets[0] != 4 * (count + 2):
        # the header itself is not always padded the same way; only warn
        print(f"  note: first offset 0x{offsets[0]:X} != header size 0x{4*(count+2):X}")
    files = []
    for i in range(count):
        start, end = offsets[i], offsets[i + 1]
        if not (start <= end <= len(buf)):
            raise ValueError(f"file {i}: bad bounds 0x{start:X}..0x{end:X}")
        blob = buf[start:end]
        if len(blob) < 8:
            files.append((0, 0, blob))  # empty stub entry
            continue
        files.append((be32(blob, 0), be32(blob, 4), blob[8:]))
    return count, files


def swap_words(buf: bytes) -> bytes:
    even = len(buf) & ~3
    out = bytearray(buf)
    for i in range(0, even, 4):
        out[i], out[i + 1], out[i + 2], out[i + 3] = out[i + 3], out[i + 2], out[i + 1], out[i]
    return bytes(out)


def build_ps2(count: int, files, swap_stream: bool) -> bytes:
    header_bytes = 4 * (count + 2)
    data_start = max(HEADER_ALIGN, align(header_bytes, HEADER_ALIGN))

    offsets, payload, cursor = [], bytearray(), data_start
    for uncomp, comp, stream in files:
        offsets.append(cursor)
        if uncomp == 0 and comp == 0:
            blob = b""
        else:
            blob = struct.pack("<II", uncomp, comp) + stream
        payload += blob
        cursor += len(blob)
    offsets.append(cursor)

    out = bytearray()
    out += struct.pack("<I", count)
    for off in offsets:
        out += struct.pack("<I", off)
    out += b"\x00" * (data_start - len(out))
    out += payload
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("wii_pak", type=Path)
    ap.add_argument("--out", type=Path, help="default: <wii_pak> with the PS2 layout")
    ap.add_argument("--swap-stream", action="store_true",
                    help="byte-swap the compressed stream words (try it if the engine chokes)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    src = args.wii_pak.read_bytes()
    count, files = parse_wii(src)
    print(f"{args.wii_pak.name}: {len(src):,} bytes, {count} file(s) [big-endian header]")
    for i, (uncomp, comp, stream) in enumerate(files):
        print(f"  file{i}: {uncomp:,} -> {comp:,} compressed ({len(stream):,} stream bytes)")

    if args.swap_stream:
        files = [(u, c, swap_words(s)) for (u, c, s) in files]
        print("  stream words byte-swapped")

    out_bytes = build_ps2(count, files, args.swap_stream)
    dst = args.out or args.wii_pak.with_suffix(".ps2.pak")
    print(f"-> {dst.name}: {len(out_bytes):,} bytes [little-endian header]")
    if args.dry_run:
        return 0
    dst.write_bytes(out_bytes)
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
