# Analysis scripts

Used to find what `rexglue codegen` missed. All read your own game files; none
contain game data. Python 3 with `pycryptodome` and `capstone`
(`pip install pycryptodome capstone`).

| script | does |
|---|---|
| `xdvdfs.py ISO list\|extract OUT [names]` | list / extract an Xbox 360 disc image |
| `xexinfo.py default.xex` | XEX2 header: title ID, imports, static libraries |
| `xexdump.py default.xex image.bin [ADDR[:before:after] ...]` | decrypt + decompress to the raw image, disassemble around addresses |
| `findptrs.py image.bin generated/default` | code addresses data points at that are not function starts (vtables) |
| `findorphans.py image.bin generated/default forza2_manifest.toml` | entries after a terminator that no branch reaches - functions merged into their neighbour |
| `findfiber.py image.bin generated/default/forza2_init.cpp` | code touching KTHREAD+0x164 - the XAPI fiber functions |
