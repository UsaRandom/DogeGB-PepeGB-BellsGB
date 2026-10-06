#pragma bank 10
#include "sha512_transform.h"
#include <stdint.h>

/* SHA-512 compression on little-endian bytes.
   Rotates are a byte move plus a 0..7 bit rotate, not a per-bit loop.
   The device runs that schedule from sha_block.s. The host uses the
   same steps in C, and this machine is little-endian, matching SDCC. */

extern const uint64_t sha512_K[80];

#ifdef __SDCC

/* Device compression is sha_block.s. It reads these two pointers. */
uint8_t *sha_ctx_p;
uint8_t *sha_data_p;
void sha512_block_asm(void);

void sha512_transform(SHA512_CTX *ctx, const uint8_t *data) BANKED {
    sha_ctx_p = (uint8_t *)ctx;
    sha_data_p = (uint8_t *)data;
    sha512_block_asm();
}

#else
static void u_copy(uint8_t *d, const uint8_t *s) {
    uint8_t i;
    for (i = 0; i < 8; i++) d[i] = s[i];
}
static void u_add(uint8_t *d, const uint8_t *s) {
    uint16_t c = 0;
    uint8_t i;
    for (i = 0; i < 8; i++) {
        c = (uint16_t)(c + d[i] + s[i]);
        d[i] = (uint8_t)c;
        c >>= 8;
    }
}
static void u_xor(uint8_t *d, const uint8_t *s) {
    uint8_t i;
    for (i = 0; i < 8; i++) d[i] ^= s[i];
}
static void u_and(uint8_t *d, const uint8_t *s) {
    uint8_t i;
    for (i = 0; i < 8; i++) d[i] &= s[i];
}
static void u_or(uint8_t *d, const uint8_t *s) {
    uint8_t i;
    for (i = 0; i < 8; i++) d[i] |= s[i];
}
static void u_ror(uint8_t *d, const uint8_t *s, uint8_t n) {
    uint8_t tmp[8];
    uint8_t bytes = (uint8_t)(n >> 3);
    uint8_t bits = (uint8_t)(n & 7);
    uint8_t i;
    for (i = 0; i < 8; i++) tmp[i] = s[(i + bytes) & 7];
    while (bits--) {
        uint8_t wrap = (uint8_t)(tmp[0] & 1);
        for (i = 0; i < 7; i++)
            tmp[i] = (uint8_t)((tmp[i] >> 1) | (tmp[i + 1] << 7));
        tmp[7] = (uint8_t)((tmp[7] >> 1) | (wrap << 7));
    }
    for (i = 0; i < 8; i++) d[i] = tmp[i];
}
static void u_shr(uint8_t *d, const uint8_t *s, uint8_t n) {
    uint8_t tmp[8];
    uint8_t bytes = (uint8_t)(n >> 3);
    uint8_t bits = (uint8_t)(n & 7);
    uint8_t i;
    for (i = 0; i < 8; i++) {
        uint8_t from = (uint8_t)(i + bytes);
        tmp[i] = (from < 8) ? s[from] : 0;
    }
    while (bits--) {
        uint8_t next = 0;
        for (i = 7; i != 0xFF; i--) {
            uint8_t cur = tmp[i];
            tmp[i] = (uint8_t)((cur >> 1) | next);
            next = (uint8_t)((cur & 1) << 7);
        }
    }
    for (i = 0; i < 8; i++) d[i] = tmp[i];
}

static uint8_t W[80][8];
static uint8_t va[8], vb[8], vc[8], vd[8], ve[8], vf[8], vg[8], vh[8];
static uint8_t T1[8], T2[8], s0[8], s1[8];

static void sigma0(uint8_t *d, const uint8_t *x) {
    u_ror(d, x, 1);
    u_ror(s1, x, 8);
    u_xor(d, s1);
    u_shr(s1, x, 7);
    u_xor(d, s1);
}

static void sigma1(uint8_t *d, const uint8_t *x) {
    u_ror(d, x, 19);
    u_ror(s1, x, 61);
    u_xor(d, s1);
    u_shr(s1, x, 6);
    u_xor(d, s1);
}

static void big_sigma0(uint8_t *d, const uint8_t *x) {
    u_ror(d, x, 28);
    u_ror(s1, x, 34);
    u_xor(d, s1);
    u_ror(s1, x, 39);
    u_xor(d, s1);
}

static void big_sigma1(uint8_t *d, const uint8_t *x) {
    u_ror(d, x, 14);
    u_ror(s1, x, 18);
    u_xor(d, s1);
    u_ror(s1, x, 41);
    u_xor(d, s1);
}

static void ch(uint8_t *d, const uint8_t *x, const uint8_t *y, const uint8_t *z) {
    u_copy(d, y);
    u_xor(d, z);
    u_and(d, x);
    u_xor(d, z);
}

static void maj(uint8_t *d, const uint8_t *x, const uint8_t *y, const uint8_t *z) {
    u_copy(s1, x);
    u_xor(s1, y);
    u_and(s1, z);
    u_copy(d, x);
    u_and(d, y);
    u_or(d, s1);
}

void sha512_transform(SHA512_CTX *ctx, const uint8_t *data) BANKED {
    uint8_t *st = (uint8_t *)ctx->state;
    const uint8_t *K = (const uint8_t *)sha512_K;
    uint8_t i;

    for (i = 0; i < 16; i++) {
        const uint8_t *p = data + (uint16_t)i * 8;
        W[i][0] = p[7];
        W[i][1] = p[6];
        W[i][2] = p[5];
        W[i][3] = p[4];
        W[i][4] = p[3];
        W[i][5] = p[2];
        W[i][6] = p[1];
        W[i][7] = p[0];
    }
    for (i = 16; i < 80; i++) {
        sigma1(W[i], W[i - 2]);
        u_add(W[i], W[i - 7]);
        sigma0(s0, W[i - 15]);
        u_add(W[i], s0);
        u_add(W[i], W[i - 16]);
    }

    u_copy(va, st);
    u_copy(vb, st + 8);
    u_copy(vc, st + 16);
    u_copy(vd, st + 24);
    u_copy(ve, st + 32);
    u_copy(vf, st + 40);
    u_copy(vg, st + 48);
    u_copy(vh, st + 56);

    for (i = 0; i < 80; i++) {
        big_sigma1(T1, ve);
        ch(s0, ve, vf, vg);
        u_add(T1, s0);
        u_add(T1, vh);
        u_add(T1, K + (uint16_t)i * 8);
        u_add(T1, W[i]);
        big_sigma0(T2, va);
        maj(s0, va, vb, vc);
        u_add(T2, s0);

        u_copy(vh, vg);
        u_copy(vg, vf);
        u_copy(vf, ve);
        u_copy(ve, vd);
        u_add(ve, T1);
        u_copy(vd, vc);
        u_copy(vc, vb);
        u_copy(vb, va);
        u_copy(va, T1);
        u_add(va, T2);
    }

    u_add(st, va);
    u_add(st + 8, vb);
    u_add(st + 16, vc);
    u_add(st + 24, vd);
    u_add(st + 32, ve);
    u_add(st + 40, vf);
    u_add(st + 48, vg);
    u_add(st + 56, vh);
}

#endif
