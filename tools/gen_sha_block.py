#!/usr/bin/env python3
"""Generate src/crypto/sha_block.s.

SHA-512 compression for the SM83. Sigma output bytes are XORs of
page-aligned ROM tables (one per source byte of a circulant rotate),
plus a straight shift for the SHR term. The schedule keeps 16 words.
Each round renames a..h through eight fixed slots, so the working
state is not copied. 80 is a multiple of 8, so the slots line up
with the context again at the end.

The tables have to stay 256-byte aligned. Link this object first in
bank 10 so the area base (0x4000) provides that alignment.
"""

import hashlib
import os
import random
import re
import sys

MASK = (1 << 64) - 1
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONST_C = os.path.join(ROOT, "src", "crypto", "sha512_constants.c")
OUT_S = os.path.join(ROOT, "src", "crypto", "sha_block.s")

IV = (
    0x6A09E667F3BCC908,
    0xBB67AE8584CAA73B,
    0x3C6EF372FE94F82B,
    0xA54FF53A5F1D36F1,
    0x510E527FADE682D1,
    0x9B05688C2B3E6C1F,
    0x1F83D9ABFB41BD6B,
    0x5BE0CD19137E2179,
)


def load_k():
    text = open(CONST_C).read()
    words = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)ULL", text)]
    if len(words) != 80:
        raise SystemExit("expected 80 K constants, got %d" % len(words))
    return words


def rotr(x, n):
    n %= 64
    return ((x >> n) | (x << (64 - n))) & MASK


def big0(x):
    return rotr(x, 28) ^ rotr(x, 34) ^ rotr(x, 39)


def big1(x):
    return rotr(x, 14) ^ rotr(x, 18) ^ rotr(x, 41)


def s0_rot(x):
    return rotr(x, 1) ^ rotr(x, 8)


def s1_rot(x):
    return rotr(x, 19) ^ rotr(x, 61)


def sigma0(x):
    return s0_rot(x) ^ (x >> 7)


def sigma1(x):
    return s1_rot(x) ^ (x >> 6)


def word_of(bs):
    return int.from_bytes(bytes(bs), "little")


def bytes_of(x):
    return list(x.to_bytes(8, "little"))


def table_for(fn, offset):
    tab = []
    for v in range(256):
        src = [0] * 8
        src[offset] = v
        tab.append(bytes_of(fn(word_of(src)))[0])
    return tab


def live_offsets(fn):
    got = []
    for off in range(8):
        if any(table_for(fn, off)):
            got.append(off)
    return got


def shr_byte(src, i, bits):
    s = src[i]
    if i == 7:
        return s >> bits
    t = src[i + 1]
    if bits == 7:
        return (s >> 7) | ((t << 1) & 0xFE)
    if bits == 6:
        return (s >> 6) | ((t << 2) & 0xFC)
    raise ValueError(bits)


def sig_bytes(kind, src, specs, tables):
    offsets, shr = specs[kind]
    out = []
    for i in range(8):
        acc = 0
        for off in offsets:
            acc ^= tables[kind][off][src[(i + off) & 7]]
        if shr:
            acc ^= shr_byte(src, i, shr)
        out.append(acc)
    return out


def add_bytes(d, s):
    c = 0
    out = []
    for i in range(8):
        c += d[i] + s[i]
        out.append(c & 255)
        c >>= 8
    return out


def ch_bytes(e, f, g):
    return [g[i] ^ (e[i] & (f[i] ^ g[i])) for i in range(8)]


def maj_bytes(x, y, z):
    return [(x[i] & y[i]) | (z[i] & (x[i] ^ y[i])) for i in range(8)]


def k_bytes(k, i):
    return list(k[i].to_bytes(8, "little"))


def compress_ref(state, block, k):
    w = []
    for i in range(16):
        w.append(int.from_bytes(block[i * 8:(i + 1) * 8], "big"))
    for i in range(16, 80):
        w.append((sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16]) & MASK)
    a, b, c, d, e, f, g, h = state
    for i in range(80):
        t1 = (h + big1(e) + ((e & f) ^ ((~e & MASK) & g)) + k[i] + w[i]) & MASK
        t2 = (big0(a) + ((a & b) ^ (a & c) ^ (b & c))) & MASK
        h, g, f = g, f, e
        e = (d + t1) & MASK
        d, c, b = c, b, a
        a = (t1 + t2) & MASK
    vals = (a, b, c, d, e, f, g, h)
    return tuple((state[i] + vals[i]) & MASK for i in range(8))


def compress_slots(state_le, block, k, specs, tables):
    """Byte-level model of the asm, including the rolling schedule and slot rename."""
    w = []
    for i in range(16):
        w.append(list(reversed(block[i * 8:(i + 1) * 8])))
    slots = [list(state_le[i * 8:(i + 1) * 8]) for i in range(8)]
    for i in range(80):
        if i >= 16:
            # Expand in step with the round. A 16-word window cannot
            # hold W[0] once W[16] is written, and round i still needs W[i].
            t1 = sig_bytes("s1", w[(i - 2) & 15], specs, tables)
            t1 = add_bytes(t1, w[(i - 7) & 15])
            s0 = sig_bytes("s0", w[(i - 15) & 15], specs, tables)
            t1 = add_bytes(t1, s0)
            t1 = add_bytes(t1, w[(i - 16) & 15])
            w[i & 15] = t1
        v = i & 7

        def sl(r, v=v):
            return slots[(r - v) & 7]

        e, f, g, h = sl(4), sl(5), sl(6), sl(7)
        a, b, c, d = sl(0), sl(1), sl(2), sl(3)
        t1 = sig_bytes("b1", e, specs, tables)
        t1 = add_bytes(t1, ch_bytes(e, f, g))
        t1 = add_bytes(t1, h)
        t1 = add_bytes(t1, k_bytes(k, i))
        t1 = add_bytes(t1, w[i & 15])
        t2 = sig_bytes("b0", a, specs, tables)
        t2 = add_bytes(t2, maj_bytes(a, b, c))
        d[:] = add_bytes(d, t1)
        h[:] = add_bytes(t1, t2)
    out = []
    for i in range(8):
        out.extend(add_bytes(state_le[i * 8:(i + 1) * 8], slots[i]))
    return out


def state_le(state):
    out = []
    for word in state:
        out.extend(word.to_bytes(8, "little"))
    return out


def le_to_state(buf):
    return tuple(int.from_bytes(buf[i * 8:(i + 1) * 8], "little") for i in range(8))


def sha512_py(msg, k, specs, tables):
    state = IV
    bitlen = len(msg) * 8
    data = msg + b"\x80"
    while (len(data) % 128) != 112:
        data += b"\x00"
    data += bitlen.to_bytes(16, "big")
    for i in range(0, len(data), 128):
        block = data[i:i + 128]
        got = compress_slots(state_le(state), block, k, specs, tables)
        ref = compress_ref(state, block, k)
        if le_to_state(got) != ref:
            raise SystemExit("slot model diverged on block %d" % (i // 128))
        state = ref
    return b"".join(word.to_bytes(8, "big") for word in state)


def self_check(k, specs, tables):
    fns = {"b0": big0, "b1": big1, "s0": s0_rot, "s1": s1_rot}
    full = {"b0": big0, "b1": big1, "s0": sigma0, "s1": sigma1}
    rng = random.Random(1)
    words = [[0] * 8, [0xFF] * 8]
    for bit in range(64):
        words.append(bytes_of(1 << bit))
    for _ in range(40):
        words.append([rng.randrange(256) for _ in range(8)])
    for kind, fn in fns.items():
        offsets, _shr = specs[kind]
        for src in words:
            acc_bytes = [0] * 8
            for i in range(8):
                for off in offsets:
                    acc_bytes[i] ^= tables[kind][off][src[(i + off) & 7]]
            if acc_bytes != bytes_of(fn(word_of(src))):
                raise SystemExit("table mismatch for %s" % kind)
    for src in words:
        for kind in ("s0", "s1"):
            if sig_bytes(kind, src, specs, tables) != bytes_of(full[kind](word_of(src))):
                raise SystemExit("shr mismatch for %s" % kind)
        if sig_bytes("b0", src, specs, tables) != bytes_of(big0(word_of(src))):
            raise SystemExit("big0 mismatch")
        if sig_bytes("b1", src, specs, tables) != bytes_of(big1(word_of(src))):
            raise SystemExit("big1 mismatch")
    vectors = [b"", b"abc", b"a" * 64, b"a" * 200, os.urandom(300)]
    for msg in vectors:
        got = sha512_py(msg, k, specs, tables)
        exp = hashlib.sha512(msg).digest()
        if got != exp:
            raise SystemExit("hash mismatch for %r" % msg[:16])
    print("self-check ok: %d words, %d messages" % (len(words), len(vectors)))


def db_lines(data):
    lines = []
    for i in range(0, len(data), 16):
        chunk = ", ".join("0x%02X" % b for b in data[i:i + 16])
        lines.append("        .db " + chunk)
    return lines


def emit_sigma(kind, offsets, shr, dest, src="_sha_in", stage=True, label=None):
    """Source bytes stay in B/C/D/E across the eight output bytes.

    DE is not a pointer here, so those registers are free. A shared
    sigma copies _sha_src into _sha_in first. A round copy reads the
    state slot directly and skips that copy.
    """
    lines = []
    name = {"b0": "big0", "b1": "big1", "s0": "sigma0", "s1": "sigma1"}[kind]
    lines.append("%s:" % (label or ("sha_%s" % name)))
    if stage:
        lines.append("        call sha_stage")
    cache = {}
    reg_of = {}

    def want_list(i):
        want = []
        for off in offsets:
            idx = (i + off) & 7
            if idx not in want:
                want.append(idx)
        if shr:
            extra = [i] if i == 7 else [i, i + 1]
            for idx in extra:
                if idx not in want:
                    want.append(idx)
        return want

    for i in range(8):
        lines.append("        ; byte %d" % i)
        want = want_list(i)
        nxt = set(want_list(i + 1)) if i < 7 else set()
        ranked = sorted(want, key=lambda idx: (0 if idx in nxt else 1, want.index(idx)))
        chosen = ranked[:4]
        for reg, idx in list(reg_of.items()):
            if idx not in chosen:
                del cache[idx]
                del reg_of[reg]
        for idx in chosen:
            if idx in cache:
                continue
            free = [r for r in ("b", "c", "d", "e") if r not in reg_of]
            reg = free[0]
            lines.append("        ld a, (#%s+%d)" % (src, idx))
            lines.append("        ld %s, a" % reg)
            cache[idx] = reg
            reg_of[reg] = idx

        uncached = []
        cached_offs = []
        for off in offsets:
            idx = (i + off) & 7
            if idx in cache:
                cached_offs.append(off)
            else:
                uncached.append(off)
        if len(uncached) > 1:
            raise SystemExit("sigma %s byte %d has %d uncached terms" % (kind, i, len(uncached)))

        def emit_term(off, first):
            idx = (i + off) & 7
            tab = "_sha_t_%s_%d" % (kind, off)
            if idx in cache:
                lines.append("        ld l, %s" % cache[idx])
                lines.append("        ld h, #>(%s)" % tab)
                if first:
                    lines.append("        ld a, (hl)")
                else:
                    lines.append("        xor a, (hl)")
            else:
                if not first:
                    raise SystemExit("uncached term was not first")
                lines.append("        ld a, (#%s+%d)" % (src, idx))
                lines.append("        ld l, a")
                lines.append("        ld h, #>(%s)" % tab)
                lines.append("        ld a, (hl)")

        first = True
        for off in uncached + cached_offs:
            emit_term(off, first)
            first = False

        if shr:
            # Park the XOR in H. The shift uses A and L, and H is free
            # once the last table lookup of this byte has finished.
            lines.append("        ld h, a")
            if i in cache:
                lines.append("        ld a, %s" % cache[i])
            else:
                lines.append("        ld a, (#%s+%d)" % (src, i))
            lines.append("        rlca")
            if shr == 6:
                lines.append("        rlca")
                lines.append("        and a, #0x03")
            else:
                lines.append("        and a, #0x01")
            if i < 7:
                lines.append("        ld l, a")
                if (i + 1) in cache:
                    lines.append("        ld a, %s" % cache[i + 1])
                else:
                    lines.append("        ld a, (#%s+%d)" % (src, i + 1))
                lines.append("        add a, a")
                if shr == 6:
                    lines.append("        add a, a")
                lines.append("        or a, l")
            lines.append("        xor a, h")
        lines.append("        ld (#%s+%d), a" % (dest, i))
    lines.append("        ret")
    lines.append("")
    return lines


def slot_ref(v, logical):
    """WRAM address of logical register r at round variant v = i & 7."""
    return "_sha_s+%d" % (((logical - v) & 7) * 8)


def emit_set_src(addr):
    return [
        "        ld hl, #%s" % addr,
        "        ld a, l",
        "        ld (#_sha_src), a",
        "        ld a, h",
        "        ld (#_sha_src+1), a",
    ]


def emit_one_round(v):
    """Round body with slot addresses baked in for this v.

    sigma, ch, and maj stay shared calls. T1 is three two-operand
    adds. A single carry flag cannot add four bytes: 255*3 is 765.
    """
    lines = ["sha_round_%d:" % v]
    lines.append("        call sha_big1_%d" % v)
    lines.append("        ld hl, #%s" % slot_ref(v, 4))
    lines.append("        ld de, #_sha_be")
    lines.append("        call sha_copy8")
    lines.append("        ld hl, #%s" % slot_ref(v, 5))
    lines.append("        ld a, l")
    lines.append("        ld (#_sha_p1), a")
    lines.append("        ld a, h")
    lines.append("        ld (#_sha_p1+1), a")
    lines.append("        ld hl, #%s" % slot_ref(v, 6))
    lines.append("        ld a, (#_sha_p1)")
    lines.append("        ld e, a")
    lines.append("        ld a, (#_sha_p1+1)")
    lines.append("        ld d, a")
    lines.append("        call sha_ch")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        ld de, #_sha_s0")
    lines.append("        call sha_addm")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        ld de, #%s" % slot_ref(v, 7))
    lines.append("        call sha_addm")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        ld l, a")
    lines.append("        ld h, #0")
    lines.append("        add hl, hl")
    lines.append("        add hl, hl")
    lines.append("        add hl, hl")
    lines.append("        ld de, #_sha512_K")
    lines.append("        add hl, de")
    lines.append("        ld d, h")
    lines.append("        ld e, l")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_addm")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        call wptr")
    lines.append("        ld d, h")
    lines.append("        ld e, l")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_addm")
    lines.append("        call sha_big0_%d" % v)
    lines.append("        ld hl, #%s" % slot_ref(v, 2))
    lines.append("        ld de, #_sha_bz")
    lines.append("        call sha_copy8")
    lines.append("        ld hl, #%s" % slot_ref(v, 1))
    lines.append("        ld a, l")
    lines.append("        ld (#_sha_p1), a")
    lines.append("        ld a, h")
    lines.append("        ld (#_sha_p1+1), a")
    lines.append("        ld hl, #%s" % slot_ref(v, 0))
    lines.append("        ld a, (#_sha_p1)")
    lines.append("        ld e, a")
    lines.append("        ld a, (#_sha_p1+1)")
    lines.append("        ld d, a")
    lines.append("        call sha_maj")
    lines.append("        ld hl, #_sha_t2")
    lines.append("        ld de, #_sha_s0")
    lines.append("        call sha_addm")
    lines.append("        ld hl, #%s" % slot_ref(v, 3))
    lines.append("        ld de, #_sha_t1")
    lines.append("        call sha_addm")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        ld de, #_sha_t2")
    lines.append("        call sha_addm")
    lines.append("        ld de, #%s" % slot_ref(v, 7))
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_copy8")
    lines.append("        ret")
    lines.append("")
    return lines


def emit_rounds():
    lines = []
    lines.append("; Dispatch on i & 7. Each copy has immediate slot addresses.")
    lines.append("sha_round:")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        and a, #7")
    for v in range(8):
        if v:
            lines.append("        dec a")
        lines.append("        jp z, sha_round_%d" % v)
    lines.append("")
    for v in range(8):
        lines.extend(emit_one_round(v))
    return lines


def emit_repeat(body, n):
    lines = []
    for _ in range(n):
        lines.extend(body)
    return lines


def emit_asm(specs, tables):
    lines = []
    lines.append("        .module sha_block")
    lines.append("        .area _CODE_10")
    lines.append("")
    lines.append("; Generated by tools/gen_sha_block.py. Do not edit by hand.")
    lines.append("; Bank 10, same bank as sha512_transform and sha512_K.")
    lines.append("; Link this file before the other bank 10 objects so the")
    lines.append("; tables stay on 256-byte pages (area base is 0x4000).")
    lines.append("; A sigma byte is the XOR of one ROM table per source byte,")
    lines.append("; plus a straight SHR when the function is not a pure rotate.")
    lines.append("; The schedule is 16 words, expanded in step with the rounds.")
    lines.append("")
    lines.append("        .globl _sha512_block_asm")
    lines.append("        .globl _sha_ctx_p")
    lines.append("        .globl _sha_data_p")
    lines.append("        .globl _sha512_K")
    lines.append("")

    order = ("b0", "b1", "s0", "s1")
    for kind in order:
        offsets, _shr = specs[kind]
        for off in offsets:
            name = "_sha_t_%s_%d" % (kind, off)
            lines.append("        .bndry 256")
            lines.append("%s::" % name)
            lines.extend(db_lines(tables[kind][off]))
            lines.append("")

    lines.append("; HL = _sha_w + (A & 15)*8. Preserves BC.")
    lines.append("wptr:")
    lines.append("        and a, #0x0F")
    lines.append("        ld l, a")
    lines.append("        ld h, #0")
    lines.append("        add hl, hl")
    lines.append("        add hl, hl")
    lines.append("        add hl, hl")
    lines.append("        ld de, #_sha_w")
    lines.append("        add hl, de")
    lines.append("        ret")
    lines.append("")
    lines.append("set_src_w:")
    lines.append("        call wptr")
    lines.append("        ld a, l")
    lines.append("        ld (#_sha_src), a")
    lines.append("        ld a, h")
    lines.append("        ld (#_sha_src+1), a")
    lines.append("        ret")
    lines.append("")
    lines.append("; Copy the word at _sha_src into _sha_in. Clobbers A, HL, DE.")
    lines.append("sha_stage:")
    lines.append("        ld hl, #_sha_src")
    lines.append("        ld a, (hl+)")
    lines.append("        ld h, (hl)")
    lines.append("        ld l, a")
    lines.append("        ld de, #_sha_in")
    lines.extend(emit_repeat([
        "        ld a, (hl+)",
        "        ld (de), a",
        "        inc de",
    ], 8))
    lines.append("        ret")
    lines.append("")
    lines.append("; HL += DE, eight bytes, little-endian. Both pointers advance by 8.")
    lines.append("sha_addm:")
    lines.append("        xor a")
    for i in range(8):
        lines.append("        ld a, (de)")
        lines.append("        inc de")
        lines.append("        adc a, (hl)")
        lines.append("        ld (hl+), a")
    lines.append("        ret")
    lines.append("")
    lines.append("; Copy 8 bytes from HL to DE. Both advance by 8.")
    lines.append("sha_copy8:")
    lines.extend(emit_repeat([
        "        ld a, (hl+)",
        "        ld (de), a",
        "        inc de",
    ], 8))
    lines.append("        ret")
    lines.append("")

    # Ch: DE = f, HL = g, _sha_be = e. Result in _sha_s0.
    lines.append("; Ch(e,f,g) = g ^ (e & (f ^ g)). DE walks f, HL walks g.")
    lines.append("sha_ch:")
    for i in range(8):
        lines.append("        ld a, (de)")
        lines.append("        inc de")
        lines.append("        xor a, (hl)")
        lines.append("        ld b, a")
        lines.append("        ld a, (#_sha_be+%d)" % i)
        lines.append("        and a, b")
        lines.append("        ld b, a")
        lines.append("        ld a, (hl+)")
        lines.append("        xor a, b")
        lines.append("        ld (#_sha_s0+%d), a" % i)
    lines.append("        ret")
    lines.append("")
    # Maj: HL = x, DE = y, _sha_bz = z.
    lines.append("; Maj(x,y,z) = (x & y) | (z & (x ^ y)).")
    lines.append("sha_maj:")
    for i in range(8):
        lines.append("        ld a, (de)")
        lines.append("        xor a, (hl)")
        lines.append("        ld b, a")
        lines.append("        ld a, (#_sha_bz+%d)" % i)
        lines.append("        and a, b")
        lines.append("        ld b, a")
        lines.append("        ld a, (de)")
        lines.append("        inc de")
        lines.append("        and a, (hl)")
        lines.append("        inc hl")
        lines.append("        or a, b")
        lines.append("        ld (#_sha_s0+%d), a" % i)
    lines.append("        ret")
    lines.append("")

    lines.extend(emit_sigma("s0", specs["s0"][0], 7, "_sha_s0"))
    lines.extend(emit_sigma("s1", specs["s1"][0], 6, "_sha_t1"))
    for v in range(8):
        lines.extend(emit_sigma(
            "b1", specs["b1"][0], 0, "_sha_t1",
            src=slot_ref(v, 4), stage=False, label="sha_big1_%d" % v,
        ))
        lines.extend(emit_sigma(
            "b0", specs["b0"][0], 0, "_sha_t2",
            src=slot_ref(v, 0), stage=False, label="sha_big0_%d" % v,
        ))

    lines.extend(emit_rounds())

    lines.append("_sha512_block_asm::")
    lines.append("        ; W[0..15] from the message. Each word is byte-reversed.")
    lines.append("        ld hl, #_sha_data_p")
    lines.append("        ld a, (hl+)")
    lines.append("        ld h, (hl)")
    lines.append("        ld l, a")
    lines.append("        ld a, l")
    lines.append("        ld (#_sha_ptr), a")
    lines.append("        ld a, h")
    lines.append("        ld (#_sha_ptr+1), a")
    lines.append("        ld c, #0")
    lines.append("sha_ldw:")
    lines.append("        ld a, c")
    lines.append("        call wptr")
    lines.append("        ld a, l")
    lines.append("        add a, #7")
    lines.append("        ld l, a")
    lines.append("        ld a, h")
    lines.append("        adc a, #0")
    lines.append("        ld h, a")
    lines.append("        ld a, (#_sha_ptr)")
    lines.append("        ld e, a")
    lines.append("        ld a, (#_sha_ptr+1)")
    lines.append("        ld d, a")
    lines.append("        ld b, #8")
    lines.append("sha_ldwb:")
    lines.append("        ld a, (de)")
    lines.append("        inc de")
    lines.append("        ld (hl), a")
    lines.append("        dec hl")
    lines.append("        dec b")
    lines.append("        jr nz, sha_ldwb")
    lines.append("        ld a, e")
    lines.append("        ld (#_sha_ptr), a")
    lines.append("        ld a, d")
    lines.append("        ld (#_sha_ptr+1), a")
    lines.append("        inc c")
    lines.append("        ld a, c")
    lines.append("        cp a, #16")
    lines.append("        jp nz, sha_ldw")
    lines.append("")
    lines.append("        ld hl, #_sha_ctx_p")
    lines.append("        ld a, (hl+)")
    lines.append("        ld h, (hl)")
    lines.append("        ld l, a")
    lines.append("        ld de, #_sha_s")
    lines.append("        ld b, #64")
    lines.append("sha_cst:")
    lines.append("        ld a, (hl+)")
    lines.append("        ld (de), a")
    lines.append("        inc de")
    lines.append("        dec b")
    lines.append("        jr nz, sha_cst")
    lines.append("")
    lines.append("        ; Rounds 0..15 use the message words still sitting in W.")
    lines.append("        xor a")
    lines.append("        ld (#_sha_i), a")
    lines.append("sha_rloop0:")
    lines.append("        call sha_round")
    lines.append("        ld hl, #_sha_i")
    lines.append("        inc (hl)")
    lines.append("        ld a, (hl)")
    lines.append("        cp a, #16")
    lines.append("        jp c, sha_rloop0")
    lines.append("")
    lines.append("        ; W[i] = sigma1(W[i-2]) + W[i-7] + sigma0(W[i-15]) + W[i-16].")
    lines.append("        ; Expand just before round i. Read W[i-16] before the store:")
    lines.append("        ; that slot is the one this word replaces, and earlier rounds")
    lines.append("        ; have already consumed it.")
    lines.append("sha_sched:")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        sub a, #2")
    lines.append("        call set_src_w")
    lines.append("        call sha_sigma1")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        sub a, #15")
    lines.append("        call set_src_w")
    lines.append("        call sha_sigma0")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        ld de, #_sha_s0")
    lines.append("        call sha_addm")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        sub a, #7")
    lines.append("        call wptr")
    lines.append("        ld d, h")
    lines.append("        ld e, l")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_addm")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        sub a, #16")
    lines.append("        call wptr")
    lines.append("        ld d, h")
    lines.append("        ld e, l")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_addm")
    lines.append("        ld a, (#_sha_i)")
    lines.append("        call wptr")
    lines.append("        ld d, h")
    lines.append("        ld e, l")
    lines.append("        ld hl, #_sha_t1")
    lines.append("        call sha_copy8")
    lines.append("        call sha_round")
    lines.append("        ld hl, #_sha_i")
    lines.append("        inc (hl)")
    lines.append("        ld a, (hl)")
    lines.append("        cp a, #80")
    lines.append("        jp c, sha_sched")
    lines.append("")
    lines.append("        ; 80 mod 8 is 0, so slot r is logical register r again.")
    lines.append("        ld hl, #_sha_ctx_p")
    lines.append("        ld a, (hl+)")
    lines.append("        ld h, (hl)")
    lines.append("        ld l, a")
    lines.append("        ld de, #_sha_s")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        call sha_addm")
    lines.append("        ret")
    lines.append("")
    lines.append("        .area _BSS")
    lines.append("_sha_w:")
    lines.append("        .ds 128")
    lines.append("_sha_s:")
    lines.append("        .ds 64")
    lines.append("_sha_in:")
    lines.append("        .ds 8")
    lines.append("_sha_t1:")
    lines.append("        .ds 8")
    lines.append("_sha_t2:")
    lines.append("        .ds 8")
    lines.append("_sha_s0:")
    lines.append("        .ds 8")
    lines.append("_sha_be:")
    lines.append("        .ds 8")
    lines.append("_sha_bz:")
    lines.append("        .ds 8")
    lines.append("_sha_src:")
    lines.append("        .ds 2")
    lines.append("_sha_ptr:")
    lines.append("        .ds 2")
    lines.append("_sha_p1:")
    lines.append("        .ds 2")
    lines.append("_sha_i:")
    lines.append("        .ds 1")
    lines.append("_sha_v:")
    lines.append("        .ds 1")
    lines.append("_sha_tmp:")
    lines.append("        .ds 1")
    lines.append("_sha_tmp2:")
    lines.append("        .ds 1")
    lines.append("")
    return "\n".join(lines) + "\n"


def main():
    k = load_k()
    builders = {"b0": big0, "b1": big1, "s0": s0_rot, "s1": s1_rot}
    shrs = {"b0": 0, "b1": 0, "s0": 7, "s1": 6}
    specs = {}
    tables = {}
    total = 0
    for kind, fn in builders.items():
        offsets = live_offsets(fn)
        specs[kind] = (offsets, shrs[kind])
        tables[kind] = {off: table_for(fn, off) for off in offsets}
        total += 256 * len(offsets)
        print("%s offsets %s shr %d" % (kind, offsets, shrs[kind]))
    print("table bytes %d" % total)
    self_check(k, specs, tables)
    out = sys.argv[1] if len(sys.argv) > 1 else OUT_S
    with open(out, "w") as f:
        f.write(emit_asm(specs, tables))
    print("wrote %s" % out)


if __name__ == "__main__":
    main()
