#ifdef __SDCC
#pragma bank 31
#endif
#include <stdint.h>
#include <gb/gb.h>

/* secp256k1 a^(p-2) mod p. 255 squares and 15 multiplies.
   Checked against pow(a, p-2, p) for 0, 1, 2, p-1, Gx, and a random.
   Blocks of 1s in p-2 have lengths {1, 2, 22, 223}. */

void bn_mul_pub(uint8_t *r, const uint8_t *a, const uint8_t *b) BANKED;
void bn_sqr_pub(uint8_t *r, const uint8_t *a) BANKED;

static void sqr_n(uint8_t *r, const uint8_t *a, uint8_t n) {
    bn_sqr_pub(r, a);
    while (--n) bn_sqr_pub(r, r);
}

void bn_inv_pub(uint8_t *r, const uint8_t *a) BANKED {
    static uint8_t x2[32], x3[32], x22[32], x44[32], acc[32], t[32];

    sqr_n(t, a, 1);
    bn_mul_pub(x2, t, a);

    sqr_n(t, x2, 1);
    bn_mul_pub(x3, t, a);

    sqr_n(x22, x3, 3);
    bn_mul_pub(x22, x22, x3);

    sqr_n(t, x22, 3);
    bn_mul_pub(x22, t, x3);

    sqr_n(t, x22, 2);
    bn_mul_pub(x22, t, x2);

    sqr_n(t, x22, 11);
    bn_mul_pub(x22, t, x22);

    sqr_n(t, x22, 22);
    bn_mul_pub(x44, t, x22);

    sqr_n(t, x44, 44);
    bn_mul_pub(acc, t, x44);

    sqr_n(t, acc, 88);
    bn_mul_pub(acc, t, acc);

    sqr_n(t, acc, 44);
    bn_mul_pub(acc, t, x44);

    sqr_n(t, acc, 3);
    bn_mul_pub(acc, t, x3);

    sqr_n(t, acc, 23);
    bn_mul_pub(acc, t, x22);

    sqr_n(t, acc, 5);
    bn_mul_pub(acc, t, a);

    sqr_n(t, acc, 3);
    bn_mul_pub(acc, t, x2);

    sqr_n(t, acc, 2);
    bn_mul_pub(r, t, a);
}
