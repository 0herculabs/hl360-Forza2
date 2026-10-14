"""Minimal XDVDFS (Xbox 360 disc) reader: list or extract files from an ISO.

usage: xdvdfs.py ISO list
       xdvdfs.py ISO extract OUTDIR [name-substring ...]
"""
import os
import struct
import sys

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Known game-partition offsets: XGD2, XGD1, XGD3, plain XISO.
PARTITIONS = [0xFD90000, 0x18300000, 0x2080000, 0]


def find_partition(f):
    for off in PARTITIONS:
        f.seek(off + 32 * SECTOR)
        if f.read(20) == MAGIC:
            return off
    raise SystemExit("no XDVDFS volume descriptor found")


def walk(f, part, sector, size, prefix=""):
    f.seek(part + sector * SECTOR)
    data = f.read(size)
    stack, seen = [0], set()
    while stack:
        off = stack.pop()
        if off in seen or off + 14 > len(data):
            continue
        seen.add(off)
        left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", data, off)
        if left == 0xFFFF:
            continue
        name = data[off + 14: off + 14 + nlen].decode("latin-1")
        path = prefix + name
        if attr & 0x10:
            if length:
                yield from walk(f, part, start, length, path + "/")
        else:
            yield path, part + start * SECTOR, length
        if left:
            stack.append(left * 4)
        if right:
            stack.append(right * 4)


def main():
    iso, cmd = sys.argv[1], sys.argv[2]
    with open(iso, "rb") as f:
        part = find_partition(f)
        f.seek(part + 32 * SECTOR + 20)
        root_sector, root_size = struct.unpack("<II", f.read(8))
        files = sorted(walk(f, part, root_sector, root_size))
        if cmd == "list":
            print(f"partition offset 0x{part:X}, {len(files)} files")
            for p, _, n in files:
                print(f"{n:>12}  {p}")
            return
        out, wanted = sys.argv[3], sys.argv[4:]
        for p, off, n in files:
            if wanted and not any(w.lower() in p.lower() for w in wanted):
                continue
            dst = os.path.join(out, *p.split("/"))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            f.seek(off)
            with open(dst, "wb") as o:
                left = n
                while left:
                    chunk = f.read(min(left, 1 << 24))
                    o.write(chunk)
                    left -= len(chunk)
            print("extracted", p, n)


if __name__ == "__main__":
    main()
