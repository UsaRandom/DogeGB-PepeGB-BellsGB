#ifdef __SDCC
#pragma bank 31
#endif
#include <stdint.h>
#include <gb/gb.h>
#include "progress.h"

/* secp256k1 a^(p-2) mod p. 255 squares and 15 multiplies.
   Each one ticks the progress bar. INVERSE_OPS in progress.h is 270.
   Checked against pow(a, p-2, p) for 0, 1, 2, p-1, Gx, and a random.
   Blocks of 1s in p-2 have lengths {1, 2, 22, 223}. */

void bn_mul_pub(uint8_t *r, const uint8_t *a, const uint8_t *b) BANKED;
void bn_sqr_pub(uint8_t *r, const uint8_t *a) BANKED;

static void field_mul(uint8_t *r, const uint8_t *a, const uint8_t *b) {
    bn_mul_pub(r, a, b);
    add_progress(WEIGHT_INV);
}

static void sqr_n(uint8_t *r, const uint8_t *a, uint8_t n) {
    bn_sqr_pub(r, a);
    add_progress(WEIGHT_INV);
    while (--n) {
        bn_sqr_pub(r, r);
        add_progress(WEIGHT_INV);
    }
}

void bn_inv_pub(uint8_t *r, const uint8_t *a) BANKED {
    static uint8_t x2[32], x3[32], x22[32], x44[32], acc[32], t[32];

    sqr_n(t, a, 1);
    field_mul(x2, t, a);

    sqr_n(t, x2, 1);
    field_mul(x3, t, a);

    sqr_n(x22, x3, 3);
    field_mul(x22, x22, x3);

    sqr_n(t, x22, 3);
    field_mul(x22, t, x3);

    sqr_n(t, x22, 2);
    field_mul(x22, t, x2);

    sqr_n(t, x22, 11);
    field_mul(x22, t, x22);

    sqr_n(t, x22, 22);
    field_mul(x44, t, x22);

    sqr_n(t, x44, 44);
    field_mul(acc, t, x44);

    sqr_n(t, acc, 88);
    field_mul(acc, t, acc);

    sqr_n(t, acc, 44);
    field_mul(acc, t, x44);

    sqr_n(t, acc, 3);
    field_mul(acc, t, x3);

    sqr_n(t, acc, 23);
    field_mul(acc, t, x22);

    sqr_n(t, acc, 5);
    field_mul(acc, t, a);

    sqr_n(t, acc, 3);
    field_mul(acc, t, x2);

    sqr_n(t, acc, 2);
    field_mul(r, t, a);
}
