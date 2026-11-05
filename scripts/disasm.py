"""Read-only disassembly helper for gun_bio.exe (Capstone).

Usage:
  python scripts/disasm.py func <hex_address> [size]   # disassemble a region
  python scripts/disasm.py xrefs <hex_address>         # find call/jmp references
  python scripts/disasm.py bytes <hex_address> <count> # raw bytes
"""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"E:\Games\Biohazard Gun Survivor\gun_bio.exe"

pe = pefile.PE(EXE)
BASE = pe.OPTIONAL_HEADER.ImageImageBase if hasattr(pe.OPTIONAL_HEADER, 'ImageImageBase') else pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)
md.detail = False


def va_to_off(va):
    rva = va - BASE
    return pe.get_offset_from_rva(rva)


def off_to_va(off):
    rva = pe.get_rva_from_offset(off)
    return BASE + rva


def read_va(va, n):
    off = va_to_off(va)
    return pe.__data__[off:off + n]


def executable_ranges():
    ranges = []
    for s in pe.sections:
        chars = s.Characteristics
        if chars & 0x20000000:  # IMAGE_SCN_MEM_EXECUTE
            start = BASE + s.VirtualAddress
            ranges.append((start, start + s.Misc_VirtualSize, s.Name.rstrip(b"\0").decode(errors="replace")))
    return ranges


def disasm(va, size=128, limit=1000000):
    data = read_va(va, size)
    out = []
    for i, ins in enumerate(md.disasm(data, va)):
        out.append("%08X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
        if len(out) >= limit:
            break
    return out


def xrefs(target):
    hits = []
    for start, end, name in executable_ranges():
        data = read_va(start, end - start)
        # E8/E9 rel32
        for i in range(len(data) - 4):
            b = data[i]
            if b in (0xE8, 0xE9):
                rel = int.from_bytes(data[i + 1:i + 5], "little", signed=True)
                dest = start + i + 5 + rel
                if dest == target:
                    hits.append((start + i, "call" if b == 0xE8 else "jmp", name))
            elif b == 0x68:  # push imm32
                imm = int.from_bytes(data[i + 1:i + 5], "little", signed=False)
                if imm == target:
                    hits.append((start + i, "push", name))
    return hits


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd = sys.argv[1]
    va = int(sys.argv[2], 16)
    if cmd == "func":
        size = int(sys.argv[3], 16) if len(sys.argv) > 3 else 256
        for line in disasm(va, size):
            print(line)
    elif cmd == "xrefs":
        for addr, kind, sec in xrefs(va):
            print("%08X  %-5s %s" % (addr, kind, sec))
    elif cmd == "bytes":
        n = int(sys.argv[3], 16) if len(sys.argv) > 3 else 64
        print(read_va(va, n).hex(" "))


if __name__ == "__main__":
    main()
