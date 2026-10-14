"""Decrypt + decompress a retail XEX2 (basic compression) into its raw PE image,
then optionally disassemble around guest addresses.

usage: xexdump.py XEX OUT.bin
       xexdump.py XEX OUT.bin ADDR[:BEFORE:AFTER] ...
"""
import struct
import sys

from Crypto.Cipher import AES
import capstone

RETAIL_KEY = bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")


def load_image(path):
    d = open(path, "rb").read()
    _, _, pe_off, _, sec_off, nhdr = struct.unpack_from(">4sIIIII", d, 0)
    fmt_off = None
    for i in range(nhdr):
        key, val = struct.unpack_from(">II", d, 24 + i * 8)
        if key == 0x000003FF:
            fmt_off = val
    image_size, = struct.unpack_from(">I", d, sec_off + 4)
    load_addr, = struct.unpack_from(">I", d, sec_off + 0x110)
    fmt_size, enc, comp = struct.unpack_from(">IHH", d, fmt_off)
    payload = d[pe_off:]
    if enc == 1:
        file_key = d[sec_off + 0x150: sec_off + 0x160]
        session = AES.new(RETAIL_KEY, AES.MODE_ECB).decrypt(file_key)
        n = len(payload) & ~15
        payload = AES.new(session, AES.MODE_CBC, b"\0" * 16).decrypt(payload[:n])
    if comp == 1:
        out = bytearray()
        src = 0
        for o in range(fmt_off + 8, fmt_off + fmt_size, 8):
            dsize, zsize = struct.unpack_from(">II", d, o)
            out += payload[src:src + dsize] + b"\0" * zsize
            src += dsize
        image = bytes(out)
    elif comp == 0:
        image = payload
    else:
        raise SystemExit(f"compression {comp} not supported")
    image = image.ljust(image_size, b"\0")
    assert image[:2] == b"MZ", "decryption failed (no MZ header)"
    return image, load_addr


def main():
    image, base = load_image(sys.argv[1])
    open(sys.argv[2], "wb").write(image)
    print(f"image 0x{len(image):X} bytes at 0x{base:08X}")
    md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
    md.skipdata = True
    for spec in sys.argv[3:]:
        parts = spec.split(":")
        addr = int(parts[0], 16)
        before = int(parts[1]) if len(parts) > 1 else 16
        after = int(parts[2]) if len(parts) > 2 else 16
        start = addr - before * 4
        code = image[start - base: start - base + (before + after) * 4]
        print(f"\n--- around 0x{addr:08X} ---")
        for ins in md.disasm(code, start):
            mark = "=>" if ins.address == addr else "  "
            raw = int.from_bytes(ins.bytes, "big")
            print(f"{mark} {ins.address:08X}: {raw:08X}  {ins.mnemonic} {ins.op_str}")


if __name__ == "__main__":
    main()
