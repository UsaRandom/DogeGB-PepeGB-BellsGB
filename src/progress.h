#include <gb/gb.h>
#include <stdbool.h>

void add_progress(uint16_t weight) BANKED;

void show_progress_page() BANKED;

void prepare_rom_check_bar(void) BANKED;
void update_progress(uint8_t progress) BANKED;
void use_coin_bar_color(void) BANKED;

/* Boot check: label on row 7, bar on rows 9-10. Address gen keeps BAR_Y. */
#define BOOT_BAR_Y 9
#define BOOT_CHECK_TEXT_Y 7

/* These two draw on the same bar as address generation. */
bool quick_rom_verify_integrity(void) BANKED;
bool rom_verify_integrity(void) BANKED;

/* ETA, and a coin on each side of the timer.
   address_page is 0 on the test page, which uses a higher row. */
void progress_ride_begin(uint8_t address_page) BANKED;
void progress_ride_tick(unsigned long done, unsigned long total) BANKED;
void progress_ride_end(void) BANKED;

/* Weights are GBC milliseconds.
   One PBKDF2 iteration is 263 ms (2048 of them).
   One comb tick is 47 ms (256 per key, three keys).
   One inverse field op is 50 ms (270 per key, three keys).
   Hash160 is 285 ms. Each address encoding is 586 ms.
   Each address check is 650 ms. Three of each.
   One CRC bank is 235 ms. One word-sum bank is 20 ms.
   The address page budgets the CRC plus the address ticks.
   The test page adds the word sum. The boot splash does not
   use these weights. */
#define WEIGHT_PBKDF2       263u
#define WEIGHT_SECP256k1     47u
#define WEIGHT_INV           50u
#define WEIGHT_HASH160      285u
#define WEIGHT_ADDR_ENCODE  586u
#define WEIGHT_ADDR_CHECK   650u
#define WEIGHT_CRC_BANK     235u
#define WEIGHT_SUM_BANK      20u
#define INVERSE_OPS         270u

#define ADDRESS_WORK_UL (2048UL * WEIGHT_PBKDF2 \
    + 3UL * 256UL * WEIGHT_SECP256k1 \
    + 3UL * INVERSE_OPS * WEIGHT_INV \
    + (unsigned long)WEIGHT_HASH160 \
    + 3UL * WEIGHT_ADDR_ENCODE \
    + 3UL * WEIGHT_ADDR_CHECK)
