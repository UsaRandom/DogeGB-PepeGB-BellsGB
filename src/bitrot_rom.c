#pragma bank 0
#include "bitrot_rom.h"
#include <gb/gb.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* sum_win borrows SP and adds little-endian words. patch_bitrot.py
   stores that sum. Call it only with interrupts off. */
extern volatile uint16_t boot_sum;
extern volatile uint16_t boot_ptr;
extern volatile uint8_t boot_passes;
void sum_win(void);

/* crc_win borrows SP. The 1 KB Sarwate table lives in SRAM at 0xAA00
   while the ROM window holds the bank being hashed. Call both with
   interrupts off and cartridge RAM enabled. crc_clear puts 0xFF back
   so the save image is unchanged. */
extern volatile uint8_t crc_state[4];
extern volatile uint16_t crc_ptr;
void crc_load(void);
void crc_win(void);
void crc_clear(void);

/* MBC5 8MB needs the high bank bit. SWITCH_ROM writes rROMB0 only,
   so the scan writes both ports and restores with SWITCH_ROM_MBC5,
   which clears rROMB1. Each bank is read with interrupts off so a
   handler cannot put a different bank back mid-checksum. */
static void rom_select(uint16_t bank) {
    rROMB1 = (uint8_t)(bank >> 8);
    rROMB0 = (uint8_t)bank;
}

/* Back to a code bank. SWITCH_ROM leaves rROMB1 alone, so a bank
   above 255 would stay mapped over the next banked call. */
static void rom_restore(uint8_t saved) {
    SWITCH_ROM_MBC5(saved);
}

static uint16_t rom_bank_count(void) {
    uint8_t rom_size_code = *(const uint8_t *)0x0148;
    return (uint16_t)(2u << rom_size_code);
}

// Trailer is 8 bytes at the end of the last bank:
// checksum u16, last_used u16, crc32 u32.
void read_trailer(uint16_t *last_used, uint16_t *checksum, uint32_t *crc) {
    uint8_t saved = _current_bank;
    uint16_t last_bank = (uint16_t)(rom_bank_count() - 1u);
    __critical {
        rom_select(last_bank);
        if (checksum) {
            *checksum = (uint16_t)(*(const uint8_t *)0x7FF8)
                      | ((uint16_t)(*(const uint8_t *)0x7FF9) << 8);
        }
        *last_used = (uint16_t)(*(const uint8_t *)0x7FFA)
                   | ((uint16_t)(*(const uint8_t *)0x7FFB) << 8);
        if (crc) memcpy(crc, (void *)0x7FFC, 4);
        rom_restore(saved);
    }
}

/* CRC32 of banks 0..last_used. Same polynomial, init, and final
   complement as patch_bitrot.py. The table is bank 11, copied into
   SRAM above the save block for the scan. */
bool rom_verify_integrity(void)
{
    uint8_t saved_bank = _current_bank;
    uint32_t stored_crc;
    uint32_t computed_crc;
    uint16_t last_used_bank;
    uint16_t b;

    read_trailer(&last_used_bank, 0, &stored_crc);

    ENABLE_RAM_MBC5;
    __critical {
        rom_select(11);
        crc_load();
        rom_restore(saved_bank);
    }

    crc_state[0] = 0xFF;
    crc_state[1] = 0xFF;
    crc_state[2] = 0xFF;
    crc_state[3] = 0xFF;

    __critical {
        rom_restore(0);
        crc_ptr = 0x0000;
        crc_win();
        rom_restore(saved_bank);
    }

    for (b = 1; b <= last_used_bank; b++) {
        __critical {
            rom_select(b);
            crc_ptr = 0x4000;
            crc_win();
            rom_restore(saved_bank);
        }
    }

    __critical {
        rom_select(11);
        crc_clear();
        rom_restore(saved_bank);
    }
    DISABLE_RAM_MBC5;

    computed_crc = (uint32_t)crc_state[0]
                 | ((uint32_t)crc_state[1] << 8)
                 | ((uint32_t)crc_state[2] << 16)
                 | ((uint32_t)crc_state[3] << 24);
    computed_crc = ~computed_crc;
    return computed_crc == stored_crc;
}

/* 128 passes is half a bank, about 10 ms. Short enough to sit in one
   frame of the logo fade. boot_passes 0 in the full check is 256. */
void rom_sum_slice(uint16_t bank, uint8_t first_half) {
    uint8_t saved = _current_bank;

    if (first_half) {
        boot_ptr = (bank == 0) ? 0x0000 : 0x4000;
    }
    boot_passes = 128;
    __critical {
        if (bank == 0) rom_restore(0);
        else rom_select(bank);
        sum_win();
        rom_restore(saved);
    }
}

// Boot integrity check. Same banks as the trailer coverage.
bool quick_rom_verify_integrity(void) {
    uint8_t saved_bank = _current_bank;
    uint16_t last_used_bank;
    uint16_t stored_checksum;
    uint16_t b;

    read_trailer(&last_used_bank, &stored_checksum, 0);
    boot_sum = 0;
    boot_passes = 0;

    __critical {
        rom_restore(0);
        boot_ptr = 0x0000;
        sum_win();
        rom_restore(saved_bank);
    }

    for (b = 1; b <= last_used_bank; b++) {
        __critical {
            rom_select(b);
            boot_ptr = 0x4000;
            sum_win();
            rom_restore(saved_bank);
        }
    }

    return boot_sum == stored_checksum;
}
