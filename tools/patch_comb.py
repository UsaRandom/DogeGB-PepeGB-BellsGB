#!/usr/bin/env python3
"""Patch the signed 13-bit comb into an 8MB ROM.

tools/gen_comb.py writes src/crypto/comb_table.bin. Windows occupy
banks 32..335 and the 2^247 table occupies banks 336..338. Banks 8
and 9 must stay 0xFF. Run this after the link and before patch_bitrot.
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BIN = ROOT / "src" / "crypto" / "comb_table.bin"

BASE = 32
WINDOWS = 19
MAGS = 4096
TOP_BANK = 336
TOP_VALUES = 513
WIN_BYTES = MAGS * 64
TOP_BYTES = TOP_VALUES * 64
ROM_SIZE = 8 * 1024 * 1024
BANK = 0x4000


def main():
    if len(sys.argv) != 2:
        print("Usage: python3 tools/patch_comb.py <rom>")
        sys.exit(1)
    rom_path = Path(sys.argv[1])
    rom = bytearray(rom_path.read_bytes())
    if len(rom) != ROM_SIZE:
        raise SystemExit(f"{rom_path} is {len(rom)} bytes, need {ROM_SIZE}")
    blob = BIN.read_bytes()
    if len(blob) != WINDOWS * WIN_BYTES + TOP_BYTES:
        raise SystemExit(f"comb bin is {len(blob)} bytes")

    def place(dst, src, n, name):
        cur = rom[dst:dst + n]
        nxt = blob[src:src + n]
        if cur != nxt and cur != b"\xff" * n:
            raise SystemExit(f"{name} overlaps ROM that is not empty")
        rom[dst:dst + n] = nxt

    for w in range(WINDOWS):
        place((BASE + w * 16) * BANK, w * WIN_BYTES, WIN_BYTES, f"window {w}")
    place(TOP_BANK * BANK, WINDOWS * WIN_BYTES, TOP_BYTES, "top")

    for b in (8, 9):
        chunk = rom[b * BANK:(b + 1) * BANK]
        if chunk != b"\xff" * BANK:
            raise SystemExit(f"bank {b} is not empty")

    g = rom[BASE * BANK:BASE * BANK + 4]
    if g != bytes.fromhex("79be667e"):
        raise SystemExit(f"window 0 mag 1 is {g.hex()}, expected G")
    g2 = rom[BASE * BANK + 64:BASE * BANK + 68]
    if g2 != bytes.fromhex("c6047f94"):
        raise SystemExit(f"window 0 mag 2 is {g2.hex()}, expected 2G")

    rom_path.write_bytes(rom)
    print(f"patched comb into {rom_path} ({ROM_SIZE} bytes)")


if __name__ == "__main__":
    main()
