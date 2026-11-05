"""Scan gun_bio.exe .text for a raw byte pattern and show instructions around each hit."""
import sys
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

EXE = r"E:\Games\Biohazard Gun Survivor\gun_bio.exe"
pe = pefile.PE(EXE)
BASE = pe.OPTIONAL_HEADER.ImageBase
md = Cs(CS_ARCH_X86, CS_MODE_32)


def va_to_off(va):
    return pe.get_offset_from_rva(va - BASE)


def main():
    pattern = bytes.fromhex(sys.argv[1])
    before = int(sys.argv[2]) if len(sys.argv) > 2 else 12
    after = int(sys.argv[3]) if len(sys.argv) > 3 else 8
    text = None
    for s in pe.sections:
        if s.Name.rstrip(b"\0") == b".text":
            text = (BASE + s.VirtualAddress, s.PointerToRawData, s.SizeOfRawData, s.Misc_VirtualSize)
    start, raw, size, vsize = text
    data = pe.__data__[raw:raw + size]
    off = 0
    hits = 0
    while True:
        i = data.find(pattern, off)
        if i < 0:
            break
        off = i + 1
        hits += 1
        va = start + i
        lo = va - before
        hi = va + after
        blob = data[va - start - before:va - start + after]
        print("--- %08X" % va)
        for ins in md.disasm(blob, lo):
            print("    %08X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
            if ins.address >= va:
                break
    print("hits:", hits)


if __name__ == "__main__":
    main()
