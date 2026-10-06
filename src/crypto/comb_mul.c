#pragma bank 30
#include <stdint.h>
#include "progress.h"

/* Signed 13-bit schedule. 19 windows cover bits 0..246.
   A digit is in -4096..4095. The top 9 bits plus the carry
   are value * 2^247 * G, value in 0..512.
   19*13 + 9 = 256 progress ticks. */

void comb_load(uint8_t window, uint16_t mag, uint8_t *dst);
void comb_load_hi(uint16_t value, uint8_t *dst);
void secp_mix_add(uint8_t *xy, uint8_t neg) BANKED;

static uint16_t window_bits(const uint8_t *priv, uint16_t bit, uint8_t n) {
    uint16_t v = 0;
    uint8_t i;
    for (i = 0; i < n; i++) {
        uint16_t b = bit + i;
        uint8_t byte = 31 - (uint8_t)(b >> 3);
        if (priv[byte] & (uint8_t)(1u << (b & 7)))
            v |= (uint16_t)((uint16_t)1u << i);
    }
    return v;
}

void comb_schedule(const uint8_t *privkey) BANKED {
    static uint8_t gxy[64];
    static int16_t digits[19];
    uint8_t carry = 0;
    uint8_t w;
    uint8_t n;
    uint16_t top;

    for (w = 0; w < 19; w++) {
        uint16_t d = (uint16_t)(window_bits(privkey, (uint16_t)w * 13u, 13) + carry);
        if (d >= 4096u) {
            digits[w] = (int16_t)((int32_t)d - 8192);
            carry = 1;
        } else {
            digits[w] = (int16_t)d;
            carry = 0;
        }
    }
    top = (uint16_t)(window_bits(privkey, 247, 9) + carry);
    for (w = 0; w < 19; w++) {
        int16_t d = digits[w];
        if (d > 0) {
            comb_load(w, (uint16_t)d, gxy);
            secp_mix_add(gxy, 0);
        } else if (d < 0) {
            comb_load(w, (uint16_t)(-(int32_t)d), gxy);
            secp_mix_add(gxy, 1);
        }
        for (n = 0; n < 13; n++) add_progress(WEIGHT_SECP256k1);
    }
    if (top) {
        comb_load_hi(top, gxy);
        secp_mix_add(gxy, 0);
    }
    for (n = 0; n < 9; n++) add_progress(WEIGHT_SECP256k1);
}
