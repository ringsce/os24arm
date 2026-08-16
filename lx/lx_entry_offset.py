#!/usr/bin/env python3
"""lx/lx_entry_offset.py — print the byte offset of a symbol within an LX
application's flat image, for use as the LX header's eip field.

Programs are linked (lx/lx_app.ld) at a fixed base address (LX_LOAD_BASE,
also hardcoded in kernel/include/lx_loader.h), and objcopy -O binary then
extracts everything from that base address onward into one flat blob. The
symbol (normally "main") is not guaranteed to sit at byte 0 of that blob
once multiple .c files are linked together, so this looks its real address
up via llvm-nm and subtracts the known base to get the correct offset.

Usage: lx_entry_offset.py <elf_file> <symbol> <base_addr_hex> [llvm-nm path]
"""
import subprocess
import sys


def main():
    if len(sys.argv) < 4:
        print("usage: lx_entry_offset.py <elf> <symbol> <base_hex> [nm]", file=sys.stderr)
        return 1

    elf_file, symbol, base_hex = sys.argv[1], sys.argv[2], sys.argv[3]
    nm = sys.argv[4] if len(sys.argv) > 4 else "llvm-nm"
    base = int(base_hex, 16)

    out = subprocess.run([nm, elf_file], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[-1] == symbol:
            addr = int(parts[0], 16)
            print(addr - base)
            return 0

    print(f"lx_entry_offset.py: symbol '{symbol}' not found in {elf_file}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
