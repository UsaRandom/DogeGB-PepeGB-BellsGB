#pragma bank 4
#include "secp256k1.h"
#include <string.h>
#include "progress.h"

extern const uint8_t gpow_table[16384];

#ifdef __SDCC
extern uint8_t *bn_mul_pa;
extern uint8_t *bn_mul_pb;
extern uint8_t bn_mul_prod[64];
extern uint8_t bn_saved_bank;
extern const uint8_t mul977_lo[];
extern const uint8_t mul977_mid[];
extern const uint8_t mul977_hi[];
void bn_mul256_asm(void);
void bn_sqr256_asm(void);
void bn_inv_pub(uint8_t *r, const uint8_t *a) BANKED;
void gpow_load(uint8_t k, uint8_t *dst);
void comb_schedule(const uint8_t *privkey) BANKED;
#else
static void gpow_load(uint8_t k, uint8_t *dst) {
    memcpy(dst, gpow_table + ((unsigned)k << 6), 64);
}
#endif

// 256-bit big integer (32 bytes, big-endian)
typedef uint8_t bn256[32];

// secp256k1 prime p = 2^256 - 2^32 - 977
static const bn256 SECP256K1_P = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFE, 0xFF, 0xFF, 0xFC, 0x2F
};

// Generator point G.x
static const bn256 SECP256K1_GX = {
    0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
    0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
    0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
    0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
};

// Generator point G.y
static const bn256 SECP256K1_GY = {
    0x48, 0x3A, 0xDA, 0x77, 0x26, 0xA3, 0xC4, 0x65,
    0x5D, 0xA4, 0xFB, 0xFC, 0x0E, 0x11, 0x08, 0xA8,
    0xFD, 0x17, 0xB4, 0x48, 0xA6, 0x85, 0x54, 0x19,
    0x9C, 0x47, 0xD0, 0x8F, 0xFB, 0x10, 0xD4, 0xB8
};

// Static buffers for field arithmetic
static bn256 t1, t2, t3, t4;
static bn256 rx, ry, rz;  // Result point (Jacobian)
static bn256 px, py, pz;  // Temp point

// Compare a >= b
static int bn_cmp(const bn256 a, const bn256 b) {
    for (int i = 0; i < 32; i++) {
        if (a[i] > b[i]) return 1;
        if (a[i] < b[i]) return -1;
    }
    return 0;
}

// a = b
static void bn_copy(bn256 a, const bn256 b) {
    for (int i = 0; i < 32; i++) a[i] = b[i];
}

// a = 0
static void bn_zero(bn256 a) {
    for (int i = 0; i < 32; i++) a[i] = 0;
}

// a = a + b (mod p)
static void bn_add_mod(bn256 a, const bn256 b) {
    uint16_t carry = 0;
    for (int i = 31; i >= 0; i--) {
        carry += a[i] + b[i];
        a[i] = carry & 0xFF;
        carry >>= 8;
    }
    if (carry || bn_cmp(a, SECP256K1_P) >= 0) {
        // Subtract p
        uint16_t borrow = 0;
        for (int i = 31; i >= 0; i--) {
            int16_t diff = a[i] - SECP256K1_P[i] - borrow;
            if (diff < 0) { diff += 256; borrow = 1; }
            else borrow = 0;
            a[i] = diff;
        }
    }
}

// a = a - b (mod p)
static void bn_sub_mod(bn256 a, const bn256 b) {
    int16_t borrow = 0;
    for (int i = 31; i >= 0; i--) {
        int16_t diff = a[i] - b[i] - borrow;
        if (diff < 0) { diff += 256; borrow = 1; }
        else borrow = 0;
        a[i] = diff;
    }
    if (borrow) {
        // Add p back
        uint16_t carry = 0;
        for (int i = 31; i >= 0; i--) {
            carry += a[i] + SECP256K1_P[i];
            a[i] = carry & 0xFF;
            carry >>= 8;
        }
    }
}

/* p = 2^256 - 2^32 - 977, so x = hi*2^256 + lo ≡ lo + hi*2^32 + hi*977 (mod p). */
static void bn_reduce_secp256k1(uint8_t *prod, bn256 r) {
    static uint8_t acc[40];
    static uint8_t hi[32];
    int iter, i, j;
    uint16_t carry;

    for (iter = 0; iter < 6; iter++) {
        int high_zero = 1;
        for (i = 0; i < 32; i++) {
            if (prod[i]) { high_zero = 0; break; }
        }
        if (high_zero) break;

        for (i = 0; i < 32; i++) hi[i] = prod[31 - i];
        memset(acc, 0, 40);
        for (i = 0; i < 32; i++) acc[i] = prod[63 - i];

        carry = 0;
        for (i = 0; i < 32; i++) {
            carry += (uint16_t)acc[i + 4] + hi[i];
            acc[i + 4] = (uint8_t)carry;
            carry >>= 8;
        }
        for (j = 36; carry && j < 40; j++) {
            carry += acc[j];
            acc[j] = (uint8_t)carry;
            carry >>= 8;
        }

        carry = 0;
        for (i = 0; i < 32; i++) {
            uint8_t b = hi[i];
#ifdef __SDCC
            uint16_t s = (uint16_t)mul977_lo[b] + acc[i] + (uint8_t)carry;
            acc[i] = (uint8_t)s;
            carry = (uint16_t)((s >> 8) + mul977_mid[b] + (carry >> 8)
                               + ((uint16_t)mul977_hi[b] << 8));
#else
            uint32_t full = (uint32_t)b * 977u + acc[i] + carry;
            acc[i] = (uint8_t)full;
            carry = (uint16_t)(full >> 8);
#endif
        }
        for (j = 32; carry && j < 40; j++) {
            carry += acc[j];
            acc[j] = (uint8_t)carry;
            carry >>= 8;
        }

        memset(prod, 0, 64);
        for (i = 0; i < 40; i++) prod[63 - i] = acc[i];
    }

    for (i = 0; i < 32; i++) r[i] = prod[32 + i];

    for (i = 0; i < 4; i++) {
        if (bn_cmp(r, SECP256K1_P) >= 0) {
            uint16_t borrow = 0;
            for (j = 31; j >= 0; j--) {
                int16_t diff = (int16_t)r[j] - SECP256K1_P[j] - (int16_t)borrow;
                if (diff < 0) { diff += 256; borrow = 1; }
                else borrow = 0;
                r[j] = (uint8_t)diff;
            }
        } else {
            break;
        }
    }
}

/* r = a * b (mod p). The device multiply is the bank-0 table routine.
   Operands are copied to WRAM first: the table bank covers 0x4000-0x7FFF,
   so a const in that window cannot be read while the table is mapped. */
static void bn_mul_mod(bn256 r, const bn256 a, const bn256 b) {
#ifdef __SDCC
    static uint8_t mul_a[32], mul_b[32];
    memcpy(mul_a, a, 32);
    memcpy(mul_b, b, 32);
    bn_mul_pa = mul_a;
    bn_mul_pb = mul_b;
    bn_saved_bank = _current_bank;
    bn_mul256_asm();
    bn_reduce_secp256k1(bn_mul_prod, r);
#else
    static uint8_t prod[64];
    int i, j;
    uint32_t carry;

    memset(prod, 0, 64);
    for (i = 31; i >= 0; i--) {
        carry = 0;
        for (j = 31; j >= 0; j--) {
            uint32_t sum = prod[i + j + 1] + (uint32_t)a[i] * b[j] + carry;
            prod[i + j + 1] = (uint8_t)(sum & 0xFF);
            carry = sum >> 8;
        }
        {
            uint32_t sum = (uint32_t)prod[i] + carry;
            int k = i;
            while (k >= 0) {
                if (k != i) sum += prod[k];
                prod[k] = (uint8_t)sum;
                sum >>= 8;
                if (sum == 0) break;
                k--;
            }
        }
    }
    bn_reduce_secp256k1(prod, r);
#endif
}

// r = a^2 (mod p). The device square is triangular; the host uses the multiply.
static void bn_sqr_mod(bn256 r, const bn256 a) {
#ifdef __SDCC
    static uint8_t sqr_a[32];
    memcpy(sqr_a, a, 32);
    bn_mul_pa = sqr_a;
    bn_saved_bank = _current_bank;
    bn_sqr256_asm();
    bn_reduce_secp256k1(bn_mul_prod, r);
#else
    bn_mul_mod(r, a, a);
#endif
}

/* Banked entries so the addition-chain inverse can live outside bank 4.
   Same-bank field math keeps calling bn_mul_mod / bn_sqr_mod directly. */
void bn_mul_pub(uint8_t *r, const uint8_t *a, const uint8_t *b) BANKED {
    bn_mul_mod(r, a, b);
}

void bn_sqr_pub(uint8_t *r, const uint8_t *a) BANKED {
    bn_sqr_mod(r, a);
}

// r = a^(-1) (mod p). Chain is checked against a^(p-2) mod p.
void bn_inv_pub(uint8_t *r, const uint8_t *a) BANKED;
static void bn_inv_mod(bn256 r, const bn256 a) {
    bn_inv_pub(r, a);
}

// Check if point is at infinity (Z == 0)
static int is_infinity(void) {
    for (int i = 0; i < 32; i++) {
        if (pz[i] != 0) return 0;
    }
    return 1;
}

// Point doubling in Jacobian coordinates (a=0 for secp256k1)
// Using dbl-2009-l formula from https://hyperelliptic.org/EFD/g1p/auto-shortw-jacobian-0.html
// R = 2P where P = (px, py, pz)
static void point_double(void) {
    static bn256 A, B, C, D, E, F;
    
    if (is_infinity()) return; // Point at infinity
    
    // A = X1^2
    bn_sqr_mod(A, px);
    
    // B = Y1^2
    bn_sqr_mod(B, py);
    
    // C = B^2 = Y1^4
    bn_sqr_mod(C, B);
    
    // D = 2*((X1+B)^2 - A - C)
    bn_copy(D, px);
    bn_add_mod(D, B);           // D = X1 + B
    bn_sqr_mod(D, D);           // D = (X1 + B)^2
    bn_sub_mod(D, A);           // D = (X1 + B)^2 - A
    bn_sub_mod(D, C);           // D = (X1 + B)^2 - A - C
    bn_add_mod(D, D);           // D = 2*((X1+B)^2 - A - C)
    
    // E = 3*A = 3*X1^2
    bn_copy(E, A);
    bn_add_mod(E, A);
    bn_add_mod(E, A);           // E = 3*A
    
    // F = E^2
    bn_sqr_mod(F, E);
    
    // X3 = F - 2*D
    bn_copy(rx, F);
    bn_sub_mod(rx, D);
    bn_sub_mod(rx, D);          // X3 = F - 2*D
    
    // Y3 = E*(D - X3) - 8*C
    bn_copy(ry, D);
    bn_sub_mod(ry, rx);         // ry = D - X3
    bn_mul_mod(ry, E, ry);      // ry = E*(D - X3)
    bn_add_mod(C, C);           // C = 2*C
    bn_add_mod(C, C);           // C = 4*C
    bn_add_mod(C, C);           // C = 8*C
    bn_sub_mod(ry, C);          // Y3 = E*(D - X3) - 8*C
    
    // Z3 = 2*Y1*Z1
    bn_mul_mod(rz, py, pz);
    bn_add_mod(rz, rz);         // Z3 = 2*Y1*Z1
    
    bn_copy(px, rx);
    bn_copy(py, ry);
    bn_copy(pz, rz);
}

// Check if bn256 is zero
static int bn_is_zero(const bn256 a) {
    for (int i = 0; i < 32; i++) {
        if (a[i] != 0) return 0;
    }
    return 1;
}

/* Mixed Jacobian + affine add. ax, ay stay in WRAM for the whole call. */
static void point_add_affine(const bn256 ax, const bn256 ay) {
    static bn256 U2, S2, H, HH, HHH, r_val, V, tmp;

    if (is_infinity()) {
        bn_copy(px, ax);
        bn_copy(py, ay);
        bn_zero(pz);
        pz[31] = 1;
        return;
    }

    bn_sqr_mod(tmp, pz);
    bn_mul_mod(U2, ax, tmp);

    bn_mul_mod(S2, tmp, pz);
    bn_mul_mod(S2, ay, S2);
    
    // H = U2 - U1 = U2 - px
    bn_copy(H, U2);
    bn_sub_mod(H, px);
    
    // r = S2 - S1 = S2 - py
    bn_copy(r_val, S2);
    bn_sub_mod(r_val, py);
    
    // If H == 0, points have same X coordinate
    if (bn_is_zero(H)) {
        if (bn_is_zero(r_val)) {
            // P == G, so P + G = 2P = 2G, use doubling
            point_double();
            return;
        } else {
            // P == -G, so P + G = infinity
            bn_zero(px);
            bn_zero(py);
            bn_zero(pz);
            return;
        }
    }
    
    // HH = H^2
    bn_sqr_mod(HH, H);
    
    // HHH = H^3
    bn_mul_mod(HHH, HH, H);
    
    // V = U1 * HH = px * HH
    bn_mul_mod(V, px, HH);
    
    // X3 = r^2 - HHH - 2*V
    bn_sqr_mod(rx, r_val);        // rx = r^2
    bn_sub_mod(rx, HHH);          // rx = r^2 - HHH
    bn_sub_mod(rx, V);            // rx = r^2 - HHH - V
    bn_sub_mod(rx, V);            // rx = r^2 - HHH - 2*V
    
    // Y3 = r*(V - X3) - S1*HHH = r*(V - X3) - py*HHH
    bn_copy(tmp, V);
    bn_sub_mod(tmp, rx);          // tmp = V - X3
    bn_mul_mod(ry, r_val, tmp);   // ry = r*(V - X3)
    bn_mul_mod(tmp, py, HHH);     // tmp = S1*HHH = py*HHH
    bn_sub_mod(ry, tmp);          // ry = r*(V - X3) - py*HHH
    
    // Z3 = Z1 * H
    bn_mul_mod(rz, pz, H);
    
    bn_copy(px, rx);
    bn_copy(py, ry);
    bn_copy(pz, rz);
}

// Convert Jacobian to affine: (X, Y, Z) -> (X/Z^2, Y/Z^3)
static void jacobian_to_affine(bn256 x, bn256 y) {
    bn_inv_mod(t1, pz);      // t1 = Z^(-1)
    bn_sqr_mod(t2, t1);      // t2 = Z^(-2)
    bn_mul_mod(x, px, t2);   // X = X * Z^(-2)
    bn_mul_mod(t3, t2, t1);  // t3 = Z^(-3)
    bn_mul_mod(y, py, t3);   // Y = Y * Z^(-3)
}

// Test function: compute Gx^2 mod p
void test_mul(uint8_t *result) BANKED {
    static bn256 gx_sq;
    bn_sqr_mod(gx_sq, SECP256K1_GX);
    for (int i = 0; i < 32; i++) result[i] = gx_sq[i];
}

#ifdef __SDCC
/* Y = P - Y. Affine Y is in 1..P-1, so this is the point negation. */
static void negate_y(uint8_t *y) {
    static bn256 tmp;
    bn_copy(tmp, SECP256K1_P);
    bn_sub_mod(tmp, y);
    bn_copy(y, tmp);
}

void secp_mix_add(uint8_t *xy, uint8_t neg) BANKED {
    if (neg) negate_y(xy + 32);
    point_add_affine(xy, xy + 32);
}
#endif

// Public key generation: pubkey = privkey * G
void secp256k1_pubkey(const uint8_t *privkey, uint8_t *pubkey) BANKED {
    bn_zero(px);
    bn_zero(py);
    bn_zero(pz);

#ifdef __SDCC
    /* Signed 7-bit comb. The schedule is in bank 30. Still 256 progress ticks. */
    comb_schedule(privkey);
#else
    /* Host reference: one add of 2^i * G per set bit. */
    static uint8_t gxy[64];
    for (int i = 0; i < 256; i++) {
        int byte_idx = i / 8;
        int bit_idx = 7 - (i % 8);
        if (privkey[byte_idx] & (1 << bit_idx)) {
            gpow_load((uint8_t)(255 - i), gxy);
            point_add_affine(gxy, gxy + 32);
        }
        add_progress(WEIGHT_SECP256k1);
    }
#endif
    
    // Convert to affine coordinates
    static bn256 ax, ay;
    jacobian_to_affine(ax, ay);
    
    // Output compressed pubkey
    pubkey[0] = (ay[31] & 1) ? 0x03 : 0x02;
    memcpy(pubkey + 1, ax, 32);
}
