
#include "bitrot_save.h"
#include "sha256.h"
#include "progress.h"

#pragma bank 4

//Validates an address checksum. 
uint8_t b58_decode(const char *text, uint8_t *out);

bool validate_checksum(char* address) BANKED {
    if (address[0] == '\0') return false;

    uint8_t decoded[25];
    if (!b58_decode(address, decoded)) return false;

    // Double-SHA256 checksum verification (exact mirror of your encoder)
    SHA256_CTX ctx;
    uint8_t checksum1[32];
    uint8_t checksum2[32];

    sha256_init(&ctx);
    sha256_update(&ctx, decoded, 21);
    sha256_final(&ctx, checksum1);

    sha256_init(&ctx);
    sha256_update(&ctx, checksum1, 32);
    sha256_final(&ctx, checksum2);

    bool returnVal = true;

    // Manual compare (no memcmp needed)
    for (int i = 0; i < 4; i++) {
        if (decoded[21 + i] != checksum2[i]){    
            returnVal = false;
            break;
        }
    }

    add_progress(WEIGHT_ADDR_CHECK);
    return returnVal;
}