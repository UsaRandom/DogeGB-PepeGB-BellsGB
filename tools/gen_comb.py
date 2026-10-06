#!/usr/bin/env python3
"""Signed 13-bit fixed-base comb for secp256k1 pubkey generation.

19 windows cover bits 0..246. Digit d is in -4096..4095 and selects
|d| * 2^(13*window) * G, with Y negated when d is negative.
The top 9 bits plus the carry are value * 2^247 * G, value in 0..512.

Raw layout (tools/patch_comb.py places it after the link):
  banks 32..335   19 windows, 16 banks each, magnitudes 1..4096
  banks 336..338  513 points, values 0..512 of 2^247 * G
Banks 8 and 9 stay empty. Code stays below bank 32.
"""

import random
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_crypto_tables import G2X, G2Y, GX, GY, P, be32, point_add

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "src" / "crypto" / "comb_table.bin"

BASE = 32
WINDOWS = 19
WIDTH = 13
MAGS = 4096
TOP_BIT = WINDOWS * WIDTH  # 247
TOP_VALUES = 513  # 0..512
TOP_BANK = 336


def window_bits(priv, bit, n):
    """Low n bits of the scalar starting at `bit`. Same walk as the ROM."""
    v = 0
    for i in range(n):
        b = bit + i
        if b > 255:
            raise SystemExit(f"bit walk past 255: bit {bit} n {n}")
        byte = 31 - (b >> 3)
        if priv[byte] & (1 << (b & 7)):
            v |= 1 << i
    return v


def recode(k):
    priv = k.to_bytes(32, "big")
    carry = 0
    digits = []
    for w in range(WINDOWS):
        d = window_bits(priv, WIDTH * w, WIDTH) + carry
        if d >= MAGS:
            digits.append(d - 2 * MAGS)
            carry = 1
        else:
            digits.append(d)
            carry = 0
    top = window_bits(priv, TOP_BIT, 9) + carry
    if top > 512:
        raise SystemExit(f"top value {top}")
    return digits, top


def jac_double(p):
    if p is None:
        return None
    x, y, z = p
    if y == 0:
        return None
    a = x * x % P
    b = y * y % P
    c = b * b % P
    d = (2 * ((x + b) ** 2 - a - c)) % P
    e = (3 * a) % P
    f = e * e % P
    x3 = (f - 2 * d) % P
    y3 = (e * (d - x3) - 8 * c) % P
    z3 = (2 * y * z) % P
    return x3, y3, z3


def jac_add(p, q):
    if p is None:
        return q
    if q is None:
        return p
    x1, y1, z1 = p
    x2, y2, z2 = q
    z1z1 = z1 * z1 % P
    z2z2 = z2 * z2 % P
    u1 = x1 * z2z2 % P
    u2 = x2 * z1z1 % P
    s1 = y1 * z2 * z2z2 % P
    s2 = y2 * z1 * z1z1 % P
    h = (u2 - u1) % P
    r = (2 * (s2 - s1)) % P
    if h == 0:
        if r == 0:
            return jac_double(p)
        return None
    hh = h * h % P
    i = (4 * hh) % P
    j = h * i % P
    v = u1 * i % P
    x3 = (r * r - j - 2 * v) % P
    y3 = (r * (v - x3) - 2 * s1 * j) % P
    z3 = (((z1 + z2) ** 2 - z1z1 - z2z2) % P) * h % P
    return x3, y3, z3


def batch_affine(points):
    zs = [p[2] for p in points]
    prod = [1]
    for z in zs:
        prod.append(prod[-1] * z % P)
    inv = pow(prod[-1], P - 2, P)
    zinvs = [0] * len(zs)
    for i in range(len(zs) - 1, -1, -1):
        zinvs[i] = inv * prod[i] % P
        inv = inv * zs[i] % P
    out = []
    for (x, y, _z), zi in zip(points, zinvs):
        z2 = zi * zi % P
        z3 = z2 * zi % P
        out.append((x * z2 % P, y * z3 % P))
    return out


def check_jac():
    g = (GX, GY, 1)
    g2 = jac_double(g)
    got = batch_affine([g, g2, jac_add(g2, g)])
    exp3 = point_add((G2X, G2Y), (GX, GY))
    if got[0] != (GX, GY) or got[1] != (G2X, G2Y) or got[2] != exp3:
        raise SystemExit("jacobian mismatch")
    print("jac ok", flush=True)


def smul_base(base, k):
    r = None
    q = base
    while k:
        if k & 1:
            r = point_add(r, q)
        q = point_add(q, q)
        k >>= 1
    return r


def build():
    check_jac()
    pow2 = [(GX, GY)]
    for i in range(255):
        pow2.append(point_add(pow2[-1], pow2[-1]))
        if pow2[-1] is None:
            raise SystemExit(f"2^{i+1} G is infinity")

    windows = []
    for w in range(WINDOWS):
        t0 = time.time()
        base = pow2[WIDTH * w]
        acc = (base[0], base[1], 1)
        jac = []
        for mag in range(1, MAGS + 1):
            jac.append(acc)
            acc = jac_add(acc, (base[0], base[1], 1))
            if acc is None:
                raise SystemExit(f"window {w} mag {mag + 1} is infinity")
        pts = batch_affine(jac)
        if pts[0] != base:
            raise SystemExit(f"window {w} mag 1")
        if pts[1] != point_add(base, base):
            raise SystemExit(f"window {w} mag 2")
        if pts[-1] != smul_base(base, MAGS):
            raise SystemExit(f"window {w} mag {MAGS}")
        windows.append(pts)
        print(f"window {w} {time.time() - t0:.1f}s", flush=True)

    base = pow2[TOP_BIT]
    acc = (base[0], base[1], 1)
    jac = []
    for v in range(1, TOP_VALUES):
        jac.append(acc)
        acc = jac_add(acc, (base[0], base[1], 1))
        if acc is None and v + 1 < TOP_VALUES:
            raise SystemExit(f"top {v + 1} is infinity")
    hi_pts = batch_affine(jac)
    hi = [None] + hi_pts
    if hi[1] != base or hi[16] != smul_base(base, 16):
        raise SystemExit("top table mismatch")
    if hi[512] != smul_base(base, 512):
        raise SystemExit("top 512 mismatch")
    if windows[0][0] != (GX, GY) or windows[0][1] != (G2X, G2Y):
        raise SystemExit("G or 2G mismatch")
    print("top ok", flush=True)
    return pow2, windows, hi


def neg(pt):
    x, y = pt
    return x, (P - y) % P


def scalar_mul(pow2, k):
    r = None
    i = 0
    while k:
        if k & 1:
            r = point_add(r, pow2[i])
        k >>= 1
        i += 1
    return r


def comb_mul(windows, hi, k):
    digits, top = recode(k)
    r = None
    adds = 0
    for w, d in enumerate(digits):
        if d > 0:
            r = point_add(r, windows[w][d - 1])
            adds += 1
        elif d < 0:
            r = point_add(r, neg(windows[w][-d - 1]))
            adds += 1
    if top:
        r = point_add(r, hi[top])
        adds += 1
    return r, adds


def check(pow2, windows, hi):
    samples = [
        0,
        1,
        2,
        4095,
        4096,
        4097,
        8191,
        8192,
        (1 << TOP_BIT) - 1,
        1 << TOP_BIT,
        1 << 255,
        (1 << 256) - 1,
    ]
    rng = random.Random(1)
    full = []
    for _ in range(16):
        k = rng.randrange(1 << 255, 1 << 256)
        samples.append(k)
        full.append(k)
    for _ in range(16):
        samples.append(rng.randrange(1 << 256))
    total = 0
    full_adds = 0
    for k in samples:
        got, adds = comb_mul(windows, hi, k)
        exp = scalar_mul(pow2, k)
        if got != exp:
            raise SystemExit(f"comb mismatch for {k:#x}")
        total += adds
        priv = k.to_bytes(32, "big")
        for bit, n in ((0, 13), (5, 13), (234, 13), (247, 9)):
            if window_bits(priv, bit, n) != ((k >> bit) & ((1 << n) - 1)):
                raise SystemExit(f"bit walk mismatch {k:#x} bit {bit}")
    for k in full:
        _r, adds = comb_mul(windows, hi, k)
        full_adds += adds
    print(
        f"self-check ok, {len(samples)} scalars, "
        f"avg adds {total / len(samples):.2f}, "
        f"random256 adds {full_adds / len(full):.2f}",
        flush=True,
    )


def point_bytes(pt):
    if pt is None:
        return bytes(64)
    return be32(pt[0]) + be32(pt[1])


def write(windows, hi):
    blob = bytearray()
    for pts in windows:
        if len(pts) != MAGS:
            raise SystemExit(f"window len {len(pts)}")
        for pt in pts:
            blob += point_bytes(pt)
    if len(hi) != TOP_VALUES:
        raise SystemExit(f"top len {len(hi)}")
    for pt in hi:
        blob += point_bytes(pt)
    expect = (WINDOWS * MAGS + TOP_VALUES) * 64
    if len(blob) != expect:
        raise SystemExit(f"blob {len(blob)} != {expect}")
    # Bank 338 holds only value 512. It must not look empty to bitrot.
    tail = blob[-64:]
    if tail.count(tail[0]) > 56:
        raise SystemExit("top point is too uniform for the trailer scan")
    OUT.write_bytes(blob)
    print(f"wrote {OUT} ({len(blob)} bytes)", flush=True)


def main():
    pow2, windows, hi = build()
    check(pow2, windows, hi)
    write(windows, hi)


if __name__ == "__main__":
    main()
