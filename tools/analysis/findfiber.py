"""Find code that loads the current KTHREAD (lwz rX,0x100(r13)) and then touches
offset 0x164 (fiber_ptr) through it; print the containing function guess and
the calls it makes."""
import struct
import sys

import capstone

image = open(sys.argv[1], "rb").read()
names = {}
for line in open(sys.argv[2], encoding="utf-8"):  # forza2_init.cpp import table
    line = line.strip()
    if line.startswith("{ 0x") and "__imp__" in line:
        a, n = line.strip("{},").split(",")
        names[int(a, 16)] = n.strip()
BASE = 0x82000000
words = struct.unpack(f">{len(image)//4}I", image)


def d_form(w):
    return w >> 26, (w >> 21) & 31, (w >> 16) & 31, w & 0xFFFF


def func_start(i):
    # walk back to the instruction after the previous blr / zero padding
    j = i
    while j > 0 and words[j - 1] not in (0x4E800020, 0) and i - j < 2000:
        j -= 1
    return j


md = capstone.Cs(capstone.CS_ARCH_PPC, capstone.CS_MODE_32 | capstone.CS_MODE_BIG_ENDIAN)
hits = set()
for i, w in enumerate(words):
    op, rt, ra, imm = d_form(w)
    if op == 32 and ra == 13 and imm == 0x100:  # lwz rt,0x100(r13)
        for k in range(i + 1, min(i + 12, len(words))):
            op2, rt2, ra2, imm2 = d_form(words[k])
            if ra2 == rt and imm2 == 0x164 and op2 in (32, 36):  # lwz/stw
                hits.add(func_start(i))
                break
for s in sorted(hits):
    addr = BASE + s * 4
    end = s
    while end < len(words) and words[end] != 0x4E800020 and end - s < 400:
        end += 1
    calls = []
    for ins in md.disasm(image[s * 4:(end + 1) * 4], addr):
        if ins.mnemonic == "bl":
            t = int(ins.op_str, 16)
            calls.append(names.get(t, f"{t:08X}"))
    print(f"{addr:08X} len={(end - s + 1) * 4:#x} calls={calls}")
