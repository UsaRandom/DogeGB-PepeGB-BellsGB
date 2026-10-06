#!/usr/bin/env python3
"""Generate ROM tables for the SM83 crypto path.

Writes:
  src/crypto/mul_tables.s   8x8 -> 16 product, banks 20-27, 128KB
  src/crypto/mul977_tab.s   byte * 977, three 256-byte tables in bank 4
  src/crypto/gpow_table.c   2^i * G affine points, bank 28, 16KB

The multiply table is packed so one outer byte stays in one MBC5 bank:
  bank   = 20 + (a >> 5)
  offset = (a & 31) * 512
  low 256 bytes are (a*b) & 0xFF for b = 0..255
  next 256 bytes are (a*b) >> 8
"""

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CRYPTO = ROOT / "src" / "crypto"

P = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
GX = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
GY = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
G2X = 0xC6047F9441ED7D6D3045406E95C07CD85C778E4B8CEF3CA7ABAC09B95C709EE5
G2Y = 0x1AE168FEA63DC339A3C58419466CEAEEF7F632653266D0E1236431A950CFE52A

MUL_BANK0 = 20
GPOW_BANK = 28


def inv(x):
    return pow(x, P - 2, P)


def point_add(p1, p2):
    if p1 is None:
        return p2
    if p2 is None:
        return p1
    x1, y1 = p1
    x2, y2 = p2
    if x1 == x2 and (y1 + y2) % P == 0:
        return None
    if p1 == p2:
        m = (3 * x1 * x1) * inv(2 * y1) % P
    else:
        m = (y2 - y1) * inv((x2 - x1) % P) % P
    x3 = (m * m - x1 - x2) % P
    y3 = (m * (x1 - x3) - y1) % P
    return x3, y3


def be32(n):
    return n.to_bytes(32, "big")


def mul977_tables():
    lo, mid, hi = [], [], []
    for b in range(256):
        v = b * 977
        lo.append(v & 0xFF)
        mid.append((v >> 8) & 0xFF)
        hi.append((v >> 16) & 0xFF)
    return lo, mid, hi


def check_mul977(lo, mid, hi):
    for b in range(256):
        for acc in (0, 1, 255):
            for carry in (0, 1, 255, 976, 2000):
                prod = lo[b] + (mid[b] << 8) + (hi[b] << 16)
                full = prod + acc + carry
                s = lo[b] + acc + (carry & 0xFF)
                byte = s & 0xFF
                nc = (s >> 8) + mid[b] + (carry >> 8) + (hi[b] << 8)
                if byte != (full & 0xFF) or nc != (full >> 8):
                    raise SystemExit(f"mul977 fold mismatch b={b} acc={acc} c={carry}")
                if nc > 0xFFFF:
                    raise SystemExit("mul977 carry does not fit in 16 bits")


def check_reduce(lo, mid, hi):
    import random

    def fold(prod):
        prod = list(prod)
        for _ in range(6):
            if all(b == 0 for b in prod[:32]):
                break
            hib = [prod[31 - i] for i in range(32)]
            acc = [0] * 40
            for i in range(32):
                acc[i] = prod[63 - i]
            carry = 0
            for i in range(32):
                carry += acc[i + 4] + hib[i]
                acc[i + 4] = carry & 0xFF
                carry >>= 8
            j = 36
            while carry:
                if j >= 40:
                    raise SystemExit("hi<<32 overflow")
                carry += acc[j]
                acc[j] = carry & 0xFF
                carry >>= 8
                j += 1
            carry = 0
            for i in range(32):
                b = hib[i]
                s = lo[b] + acc[i] + (carry & 0xFF)
                acc[i] = s & 0xFF
                carry = (s >> 8) + mid[b] + (carry >> 8) + (hi[b] << 8)
            j = 32
            while carry:
                if j >= 40:
                    raise SystemExit("mul977 overflow")
                carry += acc[j]
                acc[j] = carry & 0xFF
                carry >>= 8
                j += 1
            prod = [0] * 64
            for i in range(40):
                prod[63 - i] = acc[i]
        else:
            raise SystemExit("reduction did not finish")
        r = int.from_bytes(bytes(prod[32:]), "big")
        nsub = 0
        while r >= P:
            r -= P
            nsub += 1
            if nsub > 4:
                raise SystemExit("too many subtractions")
        return r

    samples = [
        0,
        1,
        P - 1,
        P,
        (1 << 512) - 1,
        GX * GX,
        (P - 1) * (P - 1),
    ]
    rng = random.Random(1)
    for _ in range(40):
        a = rng.randrange(1 << 256)
        b = rng.randrange(1 << 256)
        samples.append(a * b)
    for n in samples:
        got = fold(n.to_bytes(64, "big"))
        if got != n % P:
            raise SystemExit(f"reduce mismatch for {n:#x}")
    gx2 = fold((GX * GX).to_bytes(64, "big"))
    expect = 0x8550E7D238FCF3086BA9ADCF0FB52A9DE3652194D06CB5BB38D50229B854FC49
    if gx2 != expect:
        raise SystemExit(f"Gx^2 {gx2:#x}")


def gpow_bytes():
    pt = (GX, GY)
    out = bytearray()
    for i in range(256):
        if pt is None:
            raise SystemExit(f"point {i} is infinity")
        if i == 0 and pt != (GX, GY):
            raise SystemExit("G mismatch")
        if i == 1 and pt != (G2X, G2Y):
            raise SystemExit(f"2G mismatch {pt[0]:#x} {pt[1]:#x}")
        out += be32(pt[0]) + be32(pt[1])
        pt = point_add(pt, pt)
    if len(out) != 16384:
        raise SystemExit(f"gpow size {len(out)}")
    return out


def db_lines(data, per=16):
    lines = []
    for i in range(0, len(data), per):
        chunk = data[i:i + per]
        lines.append("    .db " + ", ".join(f"0x{b:02X}" for b in chunk))
    return lines


def write_mul_tables():
    parts = ["    .module mul_tables", "    ; Generated by tools/gen_crypto_tables.py", ""]
    for bank in range(8):
        blob = bytearray()
        base = bank * 32
        for slot in range(32):
            a = base + slot
            blob += bytes((a * b) & 0xFF for b in range(256))
            blob += bytes(((a * b) >> 8) & 0xFF for b in range(256))
        if len(blob) != 16384:
            raise SystemExit("mul bank size")
        # Spot-check the addressing the asm uses.
        a = base + 7
        off = 7 * 512
        if blob[off + 9] != ((a * 9) & 0xFF) or blob[off + 256 + 9] != ((a * 9) >> 8):
            raise SystemExit("mul layout")
        n = MUL_BANK0 + bank
        parts.append(f"    .area _CODE_{n}")
        parts.append(f"    _mul_tab_{n}::")
        parts.extend(db_lines(blob))
        parts.append("")
    path = CRYPTO / "mul_tables.s"
    path.write_text("\n".join(parts) + "\n")
    return path


def write_mul977(lo, mid, hi):
    parts = [
        "    .module mul977_tab",
        "    ; Generated by tools/gen_crypto_tables.py",
        "    ; byte * 977 as three 256-byte tables. Bank 4, with the field code.",
        "    .area _CODE_4",
        "    _mul977_lo::",
        *db_lines(lo),
        "    _mul977_mid::",
        *db_lines(mid),
        "    _mul977_hi::",
        *db_lines(hi),
        "",
    ]
    path = CRYPTO / "mul977_tab.s"
    path.write_text("\n".join(parts) + "\n")
    return path


def write_gpow(blob):
    lines = [
        "/* Generated by tools/gen_crypto_tables.py. Affine 2^i * G, X||Y, big-endian. */",
        "#include <stdint.h>",
        "#ifdef __SDCC",
        "#pragma bank 28",
        "#endif",
        "const uint8_t gpow_table[16384] = {",
    ]
    for i in range(0, len(blob), 16):
        chunk = blob[i:i + 16]
        comma = "," if i + 16 < len(blob) else ""
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in chunk) + comma)
    lines.append("};")
    lines.append("")
    path = CRYPTO / "gpow_table.c"
    path.write_text("\n".join(lines) + "\n")
    return path


def main():
    lo, mid, hi = mul977_tables()
    check_mul977(lo, mid, hi)
    check_reduce(lo, mid, hi)
    gpow = gpow_bytes()
    write_mul_tables()
    write_mul977(lo, mid, hi)
    write_gpow(gpow)
    print(f"tables ok, mul banks {MUL_BANK0}-{MUL_BANK0 + 7}, gpow bank {GPOW_BANK}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
