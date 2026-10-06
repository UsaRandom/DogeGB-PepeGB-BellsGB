#include <gb/gb.h>
#include <stdint.h>
#include <string.h>

#include "hd_wallet.h"
#include "pbkdf2.h"
#include "sha512.h"

/* Banked field square already in secp256k1.c. One multiply, no scalar loop. */
void test_mul(uint8_t *out) BANKED;

/*
 * Mailbox the harness reads by symbol.
 * magic: 0 idle, 0x11 entered main, 0x22 test running, 0xA5 done.
 * test id is the SRAM byte the harness wrote before boot.
 */
volatile uint8_t result[256];
volatile uint16_t ticks;

/* BIP39 abandon x11 + about. 93 bytes, empty passphrase. */
static const char MNEMONIC[] =
    "abandon abandon abandon abandon abandon abandon abandon "
    "abandon abandon abandon abandon about";

static void finish(uint8_t test, const uint8_t *data, uint16_t len) {
    uint16_t i;

    if (len > 250) {
        len = 250;
    }
    result[1] = test;
    result[2] = (uint8_t)(len & 0xFF);
    result[3] = (uint8_t)(len >> 8);
    for (i = 0; i < len; i++) {
        result[4 + i] = data[i];
    }
    /* Written last so a frame boundary cannot observe a short buffer. */
    result[0] = 0xA5;
}

static uint8_t sram_at(uint16_t offset) {
    /* MBC5 RAM enable, then the battery RAM window. */
    *((volatile uint8_t *)0x0000) = 0x0A;
    return ((volatile uint8_t *)0xA000)[offset];
}

static void run_sha512(void) {
    static SHA512_CTX ctx;
    static uint8_t digest[64];
    static const uint8_t msg[3] = {'a', 'b', 'c'};

    sha512_init(&ctx);
    sha512_update(&ctx, msg, 3);
    sha512_final(&ctx, digest);
    finish(1, digest, 64);
}

static void run_mul(void) {
    static uint8_t out[32];

    test_mul(out);
    finish(2, out, 32);
}

static void run_pbkdf2(uint32_t iterations, uint8_t test) {
    static uint8_t seed[64];
    static const uint8_t salt[8] = {
        'm', 'n', 'e', 'm', 'o', 'n', 'i', 'c'
    };

    pbkdf2_hmac_sha512(seed, (const uint8_t *)MNEMONIC, 93, salt, 8, iterations);
    finish(test, seed, 64);
}

static void run_address(void) {
    static uint8_t seed[64];
    static uint8_t privkey[32];
    static uint8_t pubkey[33];
    static char doge[35];
    static char pepe[35];
    static char bells[35];
    static uint8_t packed[202];
    static const uint8_t salt[8] = {
        'm', 'n', 'e', 'm', 'o', 'n', 'i', 'c'
    };

    memset(doge, 0, 35);
    memset(pepe, 0, 35);
    memset(bells, 0, 35);
    memset(packed, 0, sizeof packed);

    pbkdf2_hmac_sha512(seed, (const uint8_t *)MNEMONIC, 93, salt, 8, 2048);
    seed_to_addresses(seed, doge, pepe, bells, privkey, pubkey);

    memcpy(packed, doge, 35);
    memcpy(packed + 35, pepe, 35);
    memcpy(packed + 70, bells, 35);
    memcpy(packed + 105, seed, 64);
    memcpy(packed + 169, pubkey, 33);
    finish(4, packed, 202);
}

void main(void) {
    uint8_t id;
    uint8_t mark;

    /* Crypto does not wait on vblank. Skip the interrupt tax. */
    __asm__("di");

    result[0] = 0x11;
    id = sram_at(0);
    mark = sram_at(1);
    result[1] = id;

    if (mark != 0x5A) {
        finish(0xEE, 0, 0);
        while (1) { }
    }

    result[0] = 0x22;

    if (id == 1) {
        run_sha512();
    } else if (id == 2) {
        run_mul();
    } else if (id == 3) {
        run_pbkdf2(1, 3);
    } else if (id == 4) {
        run_address();
    } else {
        finish(id, 0, 0);
    }

    while (1) { }
}
