"""Find code entries that only a pointer can reach.

An address right after an unconditional terminator (blr, bctr, b) that no
direct branch anywhere in the binary targets, that is not a switch label and
not already a function start, can only be entered through a pointer: it is a
function the analysis merged into its predecessor. Also reports lis/addi pairs
in code that build a .text address (callbacks passed by value).

usage: findorphans.py IMAGE.bin GENERATED_DIR EXISTING_MANIFEST
"""
import glob
import os
import re
import struct
import sys

BASE = 0x82000000
image = open(sys.argv[1], "rb").read()
gen = sys.argv[2]
manifest = open(sys.argv[3], encoding="utf-8").read()

TEXT = (0x821D0000, 0x821D0000 + 0x77F338)
CODE = [TEXT, (0x8294F400, 0x8294F400 + 0x9D4), (0x8294FE00, 0x8294FE00 + 0x1074C)]
in_code = lambda a: any(lo <= a < hi for lo, hi in CODE)
word = lambda a: struct.unpack_from(">I", image, a - BASE)[0]

starts = set()
for line in open(os.path.join(gen, "forza2_init.cpp"), encoding="utf-8"):
    m = re.match(r"\s*\{ 0x([0-9A-F]{8}),", line)
    if m:
        starts.add(int(m.group(1), 16))
starts |= {int(a, 16) for a in re.findall(r"^0x([0-9A-Fa-f]{8}) =", manifest, re.M)}
switch_labels = set()
for path in glob.glob(os.path.join(gen, "*_recomp.*.cpp")):
    for m in re.finditer(r"case \d+:\s*\n\s*goto loc_([0-9A-F]{8});",
                         open(path, encoding="utf-8").read()):
        switch_labels.add(int(m.group(1), 16))


def sext(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


targets = set()
terminators = []
lis = {}
callbacks = set()
for lo, hi in CODE:
    for a in range(lo, hi, 4):
        w = word(a)
        op = w >> 26
        if op == 18:  # b / bl
            t = (a if not w & 2 else 0) + sext(w & 0x03FFFFFC, 26)
            targets.add(t & 0xFFFFFFFF)
            if not w & 1:
                terminators.append(a)
        elif op == 16:  # bc
            t = (a if not w & 2 else 0) + sext(w & 0xFFFC, 16)
            targets.add(t & 0xFFFFFFFF)
        elif w in (0x4E800020, 0x4E800420):  # blr, bctr
            terminators.append(a)
        if op == 15 and (w >> 16) & 31 == 0:  # lis rD,imm
            lis[(w >> 21) & 31] = (w & 0xFFFF) << 16
        elif op == 14 and (w >> 16) & 31 in lis:  # addi rD,rA,imm
            v = (lis[(w >> 16) & 31] + sext(w & 0xFFFF, 16)) & 0xFFFFFFFF
            if in_code(v) and v % 4 == 0:
                callbacks.add(v)
        if op == 18 or w in (0x4E800020, 0x4E800420):
            lis.clear()

frame = re.compile(rb"")


def looks_like_funclet(a):
    w0, w1 = word(a), word(a + 4)
    if 0x7D084378 in (w0, w1):  # mr r8,r8: EH funclet marker
        return True
    for w in (w0, w1):
        op = w >> 26
        ra, rs = (w >> 16) & 31, (w >> 21) & 31
        # D-form load/store/addi based on r28-r31 = parent frame access
        if op in (14, 32, 34, 36, 38, 40, 44, 48, 50, 52, 54, 58, 62) and ra >= 28:
            return True
        if op == 18 and w & 1:  # bl first: catch funclet body
            return True
    return False


found = {}
for t in terminators:
    a = t + 4
    while in_code(a) and word(a) == 0:
        a += 4
    if not in_code(a) or a in starts or a in targets or a in switch_labels:
        continue
    if looks_like_funclet(a):
        continue
    found[a] = "after terminator, no direct branch reaches it"
for v in callbacks:
    if v in starts or v in targets or v in switch_labels or v in found:
        continue
    prev = word(v - 4)
    if prev in (0x4E800020, 0x4E800420, 0) or (prev >> 26 == 18 and not prev & 1):
        if not looks_like_funclet(v):
            found[v] = "lis/addi callback after terminator"

for a in sorted(found):
    print(f"0x{a:08X} = {{}}  # {found[a]}")
print(f"# {len(found)} orphans ({len(callbacks)} lis/addi code pointers seen)", file=sys.stderr)
