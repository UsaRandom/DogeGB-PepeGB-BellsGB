
#include "bitrot_save.h"
#include "sha256.h"
#include "progress.h"

#pragma bank 4

//Validates an address checksum. 
bool validate_checksum(char* address) BANKED {
    if (address[0] == '\0') return false;

    uint8_t decoded[25];
    for (int i = 0; i < 25; i++) decoded[i] = 0;

    static const char base58_alphabet[] =
        "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

    // Decode Base58 (reuses your exact alphabet and style)
    for (int i = 0; i < 36; i++) {
        char c = address[i];
        if (c == '\0') break;

        // Find digit value (manual loop to stay consistent with your no-stdlib style)
        int digit = -1;
        for (int k = 0; k < 58; k++) {
            if (base58_alphabet[k] == c) {
                digit = k;
                break;
            }
        }
        if (digit < 0) return 0;  // invalid character

        int carry = digit;
        for (int j = 24; j >= 0; j--) {
            carry += (int)decoded[j] * 58;
            decoded[j] = carry % 256;
            carry /= 256;
        }
        if (carry > 0) return 0;  // too large for 25-byte payload
    }

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