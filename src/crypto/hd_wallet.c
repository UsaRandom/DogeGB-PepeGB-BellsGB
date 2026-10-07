#pragma bank 4

#include <stdio.h>

#include "sha256.h"
#include "secp256k1.h"
#include "hmac.h"
#include "hd_wallet.h"
#include "ripemd160.h"

#include <gb/gb.h>
#include "progress.h"

static void my_memcpy(uint8_t *dest, const uint8_t *src, int len) {
    for (int i = 0; i < len; i++) {
        dest[i] = src[i];
    }
}

static const uint8_t SECP256K1_N[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE,
    0xBA, 0xAE, 0xDC, 0xE6, 0xAF, 0x48, 0xA0, 0x3B,
    0xBF, 0xD2, 0x5E, 0x8C, 0xD0, 0x36, 0x41, 0x41
};

void bn256_add_mod_n(uint8_t *result, const uint8_t *a, const uint8_t *b) BANKED {
    uint16_t carry = 0;
    int i;

    // Add a + b
    for (i = 31; i >= 0; i--) {
        carry += a[i] + b[i];
        result[i] = carry & 0xFF;
        carry >>= 8;
    }

    int cmp = 0;
    for (i = 0; i < 32; i++) {
        if (result[i] > SECP256K1_N[i]) { cmp = 1; break; }
        if (result[i] < SECP256K1_N[i]) { cmp = -1; break; }
    }

    if (carry || cmp >= 0) {
        uint16_t borrow = 0;
        for (i = 31; i >= 0; i--) {
            int16_t diff = result[i] - SECP256K1_N[i] - borrow;
            if (diff < 0) {
                diff += 256;
                borrow = 1;
            } else {
                borrow = 0;
            }
            result[i] = diff;
        }
    }
}

void seed_to_addresses(
    const uint8_t *seed,
    char *doge_out,
    char *pepe_out,
    char *bells_out,
    uint8_t privkey_out[32],
    uint8_t pubkey_out[33]
) BANKED {
    uint8_t privkey[32];
    uint8_t chaincode[32];
    uint8_t I[64];
    uint8_t data[37];
    uint8_t pubkey[33];
    uint8_t child_key[32];

    const uint32_t path[5] = {
        0x8000002C, 0x80000003, 0x80000000, 0, 0
    };

    hmac_sha512(I, (const uint8_t*)"Bitcoin seed", 12, seed, 64);
    my_memcpy(privkey, I, 32);
    my_memcpy(chaincode, I + 32, 32);

    for (int level = 0; level < 5; level++) {
        if (path[level] & 0x80000000u) {
            data[0] = 0x00;
            my_memcpy(data + 1, privkey, 32);
        } else {
            secp256k1_pubkey(privkey, pubkey);
            my_memcpy(data, pubkey, 33);
        }
        data[33] = (path[level] >> 24) & 0xFF;
        data[34] = (path[level] >> 16) & 0xFF;
        data[35] = (path[level] >> 8)  & 0xFF;
        data[36] = path[level]         & 0xFF;

        hmac_sha512(I, chaincode, 32, data, 37);

        uint8_t il_invalid = 0;
        for (int j = 0; j < 32; j++) {
            if (I[j] > SECP256K1_N[j])
            {
                il_invalid = 1;
                break;
            }
            if (I[j] < SECP256K1_N[j])
            {                
                break;
            }
        }
        if (il_invalid) {
            doge_out[0] = pepe_out[0] = bells_out[0] = '\0';
            return;
        }

        bn256_add_mod_n(child_key, I, privkey);

        uint8_t is_zero = 1;
        for (int j = 0; j < 32; j++) {
            if (child_key[j] != 0)
            {
                is_zero = 0;
                break;
            }
        }
        if (is_zero) {
            doge_out[0] = pepe_out[0] = bells_out[0] = '\0';
            return;
        }

        my_memcpy(privkey, child_key, 32);
        my_memcpy(chaincode, I + 32, 32);
    }

    uint8_t compressed_pub[33];
    secp256k1_pubkey(privkey, compressed_pub);

    my_memcpy(privkey_out, privkey, 32);
    my_memcpy(pubkey_out, compressed_pub, 33);

    SHA256_CTX ctx;
    uint8_t sha_hash[32];
    uint8_t hash160[20];

    sha256_init(&ctx);
    sha256_update(&ctx, compressed_pub, 33);
    sha256_final(&ctx, sha_hash);
    ripemd160(sha_hash, 32, hash160);
    add_progress(WEIGHT_HASH160);

    const struct {
        uint8_t version;
        char   *out_buffer;
    } address_configs[3] = {
        { 0x1E, doge_out  },   // Dogecoin → 'D...'
        { 0x38, pepe_out  },   // Pepecoin → 'P...'
        { 0x19, bells_out }    // Bellscoin → 'B...'
    };

    static const char base58_alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    for (int c = 0; c < 3; c++) {
        uint8_t versioned[21];
        versioned[0] = address_configs[c].version;
        my_memcpy(versioned + 1, hash160, 20);

        uint8_t checksum1[32];
        uint8_t checksum2[32];
        uint8_t checksum[4];
        uint8_t payload[25];

        sha256_init(&ctx);
        sha256_update(&ctx, versioned, 21);
        sha256_final(&ctx, checksum1);

        sha256_init(&ctx);
        sha256_update(&ctx, checksum1, 32);
        sha256_final(&ctx, checksum2);

        my_memcpy(checksum, checksum2, 4);
        my_memcpy(payload, versioned, 21);
        my_memcpy(payload + 21, checksum, 4);

        int zeros = 0;
        while (zeros < 25 && payload[zeros] == 0) zeros++;

        uint8_t b58[40] = {0};
        int b58_len = 0;

        for (int i = 0; i < 25; i++) {
            int carry = payload[i];
            for (int j = 0; j < b58_len; j++) {
                carry += (int)b58[j] << 8;
                b58[j] = carry % 58;
                carry /= 58;
            }
            while (carry > 0) {
                b58[b58_len++] = carry % 58;
                carry /= 58;
            }
        }

        char *out = address_configs[c].out_buffer;
        for (int i = 0; i < zeros; i++) *out++ = '1';
        for (int i = b58_len - 1; i >= 0; i--) *out++ = base58_alphabet[b58[i]];
        *out = '\0';
        add_progress(WEIGHT_ADDR_ENCODE);
    }
}