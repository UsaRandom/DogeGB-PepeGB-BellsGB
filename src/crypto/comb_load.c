#pragma bank 0
#include <stdint.h>
#include <string.h>
#include <gb/gb.h>

/* Signed 13-bit comb. tools/gen_comb.py and tools/patch_comb.py
   write the bytes after link. Banks 8 and 9 stay empty.
   Window w, magnitude 1..4096:
     bank = 32 + w*16 + ((mag-1) >> 8)
     offset = ((mag-1) & 255) << 6
   Top value 0..512 of 2^247*G starts at bank 336. */

#define COMB_BASE 32u
#define COMB_TOP  336u

static void map_bank(uint16_t bank) {
    rROMB1 = (uint8_t)(bank >> 8);
    rROMB0 = (uint8_t)bank;
}

void comb_load(uint8_t window, uint16_t mag, uint8_t *dst) {
    uint8_t saved = _current_bank;
    uint16_t slot = (uint16_t)(mag - 1u);
    uint16_t bank = COMB_BASE + (uint16_t)window * 16u + (slot >> 8);
    uint16_t off = (uint16_t)((slot & 255u) << 6);

    __critical {
        map_bank(bank);
        memcpy(dst, (const uint8_t *)(0x4000u + off), 64);
        /* SWITCH_ROM leaves rROMB1 set, so bank 30 would run as bank 286. */
        SWITCH_ROM_MBC5(saved);
    }
}

void comb_load_hi(uint16_t value, uint8_t *dst) {
    uint8_t saved = _current_bank;
    uint16_t bank = COMB_TOP + (value >> 8);
    uint16_t off = (uint16_t)((value & 255u) << 6);

    __critical {
        map_bank(bank);
        memcpy(dst, (const uint8_t *)(0x4000u + off), 64);
        /* SWITCH_ROM leaves rROMB1 set, so bank 30 would run as bank 286. */
        SWITCH_ROM_MBC5(saved);
    }
}
