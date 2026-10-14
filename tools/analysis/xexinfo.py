"""Dump the XEX2 optional headers that matter for a recompilation bring-up."""
import struct
import sys

KEYS = {
    0x000002FF: "resource_info", 0x000003FF: "base_file_format",
    0x00010001: "original_base_address", 0x00010100: "entry_point",
    0x00010201: "image_base_address", 0x000103FF: "import_libraries",
    0x00018002: "checksum_timestamp", 0x00018102: "enabled_for_callcap",
    0x000183FF: "original_pe_name", 0x000200FF: "static_libraries",
    0x00020104: "tls_info", 0x00020200: "default_stack_size",
    0x00020301: "default_filesystem_cache_size", 0x00020401: "default_heap_size",
    0x00030000: "system_flags", 0x00040006: "execution_info",
    0x00040201: "title_workspace_size", 0x00040310: "game_ratings",
    0x00040404: "lan_key", 0x000405FF: "xbox360_logo",
    0x000406FF: "multidisc_media_ids", 0x000407FF: "alternate_title_ids",
    0x00040801: "additional_title_memory", 0x00E10402: "exports_by_name",
}


def main():
    d = open(sys.argv[1], "rb").read()
    magic, mflags, pe_off, _, sec_off, nhdr = struct.unpack_from(">4sIIIII", d, 0)
    print(f"magic={magic} module_flags=0x{mflags:X} pe_offset=0x{pe_off:X} headers={nhdr}")
    image_size, = struct.unpack_from(">I", d, sec_off + 4)
    load_addr, = struct.unpack_from(">I", d, sec_off + 0x110)
    print(f"image_size=0x{image_size:X} load_address=0x{load_addr:X}")
    for i in range(nhdr):
        key, val = struct.unpack_from(">II", d, 24 + i * 8)
        name = KEYS.get(key, f"0x{key:08X}")
        if key == 0x00040006:
            mid, ver, base, tid = struct.unpack_from(">IIII", d, val)
            print(f"  {name}: title_id={tid:08X} media_id={mid:08X} version=0x{ver:X}")
        elif key == 0x000003FF:
            enc, comp = struct.unpack_from(">HH", d, val + 4)
            print(f"  {name}: encryption={enc} compression={comp}  (1=basic,2=normal)")
        elif key == 0x000200FF:
            size, = struct.unpack_from(">I", d, val)
            print(f"  {name}:")
            for o in range(val + 4, val + size, 16):
                lib = d[o:o + 8].rstrip(b"\0").decode()
                maj, mnr, bld, qfe = struct.unpack_from(">HHHH", d, o + 8)
                print(f"      {lib:10} {maj}.{mnr}.{bld}.{qfe & 0xFF}")
        elif key == 0x000103FF:
            size, strsize, count = struct.unpack_from(">III", d, val)
            names = d[val + 12: val + 12 + strsize].split(b"\0")
            o = val + 12 + strsize
            print(f"  {name}: {count} libraries")
            for _ in range(count):
                lsize, = struct.unpack_from(">I", d, o)
                ver, minver, nidx, nrec = struct.unpack_from(">IIHH", d, o + 0x1C)
                v = f"{ver>>28}.{(ver>>24)&0xF}.{(ver>>8)&0xFFFF}.{ver&0xFF}"
                print(f"      {names[nidx].decode():14} v{v}  {nrec} import records")
                o += lsize
        elif key == 0x000183FF:
            n, = struct.unpack_from(">I", d, val)
            print(f"  {name}: {d[val+4:val+n].rstrip(b'\\0').decode()}")
        elif (key & 0xFF) in (0, 1):
            print(f"  {name}: 0x{val:08X}")
        else:
            print(f"  {name}: @0x{val:X}")


if __name__ == "__main__":
    main()
