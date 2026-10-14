"""List code addresses that data points at but
that are not the start of a recompiled function.

usage: findptrs.py IMAGE.bin GENERATED_DIR
Prints one `0xADDR = {}` line per candidate (manifest [entrypoint.functions]
form), with the reason as a comment. Switch-table labels are excluded.
"""
import glob
import os
import re
import struct
import sys

BASE = 0x82000000
image = open(sys.argv[1], "rb").read()
gen = sys.argv[2]


def sections():
    pe = struct.unpack_from("<I", image, 0x3C)[0]
    n = struct.unpack_from("<H", image, pe + 6)[0]
    opt = struct.unpack_from("<H", image, pe + 20)[0]
    out = {}
    for i in range(n):
        name, vs, va = struct.unpack_from("<8sII", image, pe + 24 + opt + i * 40)
        out[name.rstrip(b"\0").decode()] = (BASE + va, BASE + va + vs)
    return out


secs = sections()
code_ranges = [secs[s] for s in (".text", "PAGELK", "BINK")]
in_code = lambda a: any(lo <= a < hi for lo, hi in code_ranges)
section_of = lambda a: next((n for n, (lo, hi) in secs.items() if lo <= a < hi), "?")

# Known function starts + every switch-case target the codegen already resolved.
starts = set()
for line in open(os.path.join(gen, "forza2_init.cpp"), encoding="utf-8"):
    m = re.match(r"\s*\{ 0x([0-9A-F]{8}),", line)
    if m:
        starts.add(int(m.group(1), 16))
switch_labels = set()
for path in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
    text = open(path, encoding="utf-8").read()
    for m in re.finditer(r"case \d+:\s*\n\s*goto loc_([0-9A-F]{8});", text):
        switch_labels.add(int(m.group(1), 16))

# Every aligned word in the data sections that points into code. (The .reloc
# section is not usable: the XEX packer drops it and reuses the range.)
cands = {}
for sec in (".rdata", ".data"):
    lo, hi = secs[sec]
    for where in range(lo, hi - 3, 4):
        target = struct.unpack_from(">I", image, where - BASE)[0]
        if in_code(target) and target % 4 == 0 and target not in starts                 and target not in switch_labels:
            cands.setdefault(target, set()).add(sec)

word = lambda a: struct.unpack_from(">I", image, a - BASE)[0]
for t in sorted(cands):
    prev = word(t - 4)
    why = []
    if word(t) == 0x7D8802A6:
        why.append("mflr r12 prologue")
    if prev in (0x4E800020, 0x4E800420, 0):
        why.append("after blr/bctr/pad")
    elif prev >> 26 == 18 and not prev & 1:
        why.append("after b")
    print(f"0x{t:08X} = {{}}  # from {','.join(sorted(cands[t]))}; {', '.join(why) or 'mid-code?'}")
print(f"# {len(cands)} candidates", file=sys.stderr)
