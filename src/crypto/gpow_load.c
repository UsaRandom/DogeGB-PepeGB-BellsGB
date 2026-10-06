#pragma bank 0
#include <stdint.h>
#include <string.h>
#include <gb/gb.h>

/* gpow_table fills bank 28 and is placed at 0x4000. */
#define GPOW_BANK 28

extern const uint8_t gpow_table[];

void gpow_load(uint8_t k, uint8_t *dst) {
    uint8_t saved = _current_bank;
    SWITCH_ROM(GPOW_BANK);
    memcpy(dst, gpow_table + ((uint16_t)k << 6), 64);
    SWITCH_ROM(saved);
}
