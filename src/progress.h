#include <gb/gb.h>
#include <stdbool.h>

void add_progress(uint16_t weight) BANKED;

void show_progress_page() BANKED;

void prepare_rom_check_bar(void) BANKED;
void update_progress(uint8_t progress) BANKED;

/* These two draw on the same bar as address generation. */
bool quick_rom_verify_integrity(void) BANKED;
bool rom_verify_integrity(void) BANKED;

/* Weights are GBC milliseconds, measured 2026-10-06.
   One PBKDF2 iteration is 263 ms (2048 of them).
   One comb tick is 47 ms (256 per key, three keys).
   One inverse field op is 50 ms (270 per key, three keys).
   One CRC bank is 235 ms. One word-sum bank is 20 ms.
   The address page budgets the CRC plus the address ticks.
   The test page adds the word sum. The boot splash does not
   use these weights. */
#define WEIGHT_PBKDF2       263u
#define WEIGHT_SECP256k1     47u
#define WEIGHT_INV           50u
#define WEIGHT_CRC_BANK     235u
#define WEIGHT_SUM_BANK      20u
#define INVERSE_OPS         270u

#define ADDRESS_WORK_UL (2048UL * WEIGHT_PBKDF2 \
    + 3UL * 256UL * WEIGHT_SECP256k1 \
    + 3UL * INVERSE_OPS * WEIGHT_INV)
