#include "progress.h"
#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>
#include "src/assets/progress_bar.h"
#include <gbdk/metasprites.h>
#include "bitrot_rom.h"
#include "bitrot_save.h"
#include "mnemonic.h"
#include "hd_wallet.h"
#include <wallet.h>
#include <gb/cgb.h>

#pragma bank 6


uint8_t total_progress = 0;

#define TILE_BASE 200           
#define BAR_X 1
#define BAR_Y 15
#define BAR_TOTAL_TILES 18      
#define BAR_TOTAL_PX (BAR_TOTAL_TILES * 8)

static uint8_t bar_draw_y = BAR_Y;

unsigned long progress_accum = 0UL;
unsigned long total_work = 0UL;
static unsigned long work_total = 1UL;
uint8_t progress_on = 0;

extern volatile uint8_t crc_state[4];
extern uint8_t current_mode;

static palette_color_t bar_pal[4];

/* Fill is palette index 1. Gold, green, or bronze follows the coin. */
void use_coin_bar_color(void) BANKED {
    switch (current_mode) {
        case PEPEGB:
            bar_pal[1] = RGB8(73, 177, 55);
            break;
        case BELLSGB:
            bar_pal[1] = RGB8(214, 137, 50);
            break;
        default:
            bar_pal[1] = RGB8(247, 183, 22);
            break;
    }
    bar_pal[0] = RGB8(255, 255, 255);
    bar_pal[2] = RGB8(0, 0, 0);
    bar_pal[3] = RGB8(255, 255, 255);
    set_bkg_palette(6, 1, bar_pal);
}



void update_progress(uint8_t progress) BANKED {

    unsigned char top_row[BAR_TOTAL_TILES];
    unsigned char bot_row[BAR_TOTAL_TILES];

    for (uint8_t tile_pos = 0; tile_pos < BAR_TOTAL_TILES; tile_pos++) {
        uint16_t tile_start_px = (uint16_t)tile_pos * 8u;
        uint8_t local_filled_px = 0;

        if (progress > tile_start_px) {
            uint16_t remaining = progress - tile_start_px;
            local_filled_px = (remaining > 8) ? 8 : (uint8_t)remaining;
        }

        uint8_t level;
        uint8_t base_rel;

        if (tile_pos == 0) {
            level = (local_filled_px > 6) ? 6 : local_filled_px;
            base_rel = 0;
        }
        else if (tile_pos == BAR_TOTAL_TILES - 1) {
            level = (local_filled_px > 6) ? 6 : local_filled_px;
            base_rel = 7;
        }
        else {
            level = (local_filled_px > 8) ? 8 : local_filled_px;
            base_rel = 14;
        }

        top_row[tile_pos] = TILE_BASE + base_rel + level;
        bot_row[tile_pos] = TILE_BASE + (base_rel + 23) + level;
    }

    set_bkg_tiles(BAR_X, bar_draw_y,     BAR_TOTAL_TILES, 1, top_row);
    set_bkg_tiles(BAR_X, bar_draw_y + 1, BAR_TOTAL_TILES, 1, bot_row);


}


void add_progress(uint16_t weight) BANKED
{
    uint8_t drawn;

    if (!progress_on || total_progress >= BAR_TOTAL_PX || work_total == 0UL) return;

    total_work += (unsigned long)weight;
    progress_accum += (unsigned long)weight * (unsigned long)BAR_TOTAL_PX;

    drawn = total_progress;
    while (progress_accum >= work_total && total_progress < BAR_TOTAL_PX) {
        progress_accum -= work_total;
        total_progress++;
    }

    progress_ride_tick(total_work, work_total);

    if (drawn == total_progress) return;

    update_progress(total_progress);
}


uint8_t text_x_pos(const char* str) {
    uint8_t len = strlen(str);
    if (len > 18) len = 18;
    return (20 - len) / 2;
}

/* First byte after the save block. Slots end at 0xA92A and the CRC
   scratch table starts at 0xAA00. A raw byte, not a linker symbol,
   so it cannot shift the slots. 0xA5 means the known derivation
   matched. 0x00 and 0xFF are a fresh cartridge. */
#define ADDR_CHECK_ADDR 0xA92Au
#define ADDR_CHECK_DONE 0xA5u

static uint8_t addr_check_byte(void) {
    uint8_t v;

    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    v = *(volatile uint8_t *)ADDR_CHECK_ADDR;
    DISABLE_RAM_MBC5;
    return v;
}

static void mark_addr_check(void) {
    ENABLE_RAM_MBC5;
    SWITCH_RAM_MBC5(0);
    *(volatile uint8_t *)ADDR_CHECK_ADDR = ADDR_CHECK_DONE;
    DISABLE_RAM_MBC5;
}


void show_progress_page() BANKED {
    uint16_t last = 0;
    unsigned long banks;

    vsync();
    clear_screen();
    total_work = 0UL;
    total_progress = 0;
    progress_accum = 0;

    read_trailer(&last, 0, 0);
    banks = (unsigned long)last + 1UL;
    work_total = ADDRESS_WORK_UL + banks * (unsigned long)WEIGHT_CRC_BANK;
    if (addr_check_byte() != ADDR_CHECK_DONE) {
        /* Known wallet, then the user's. */
        work_total += ADDRESS_WORK_UL;
    }
    progress_on = 1;
    bar_draw_y = BAR_Y;

    use_coin_bar_color();

    set_bkg_data(TILE_BASE, progress_bar_TILE_COUNT, progress_bar_tiles);

    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(0, BAR_Y-1, 20, BAR_Y-3, 0); 
        fill_bkg_rect(BAR_X, BAR_Y, BAR_TOTAL_TILES, 2, 6); 
        fill_bkg_rect(0, BAR_Y + 2, 20, 7, 0); 
        VBK_REG = 0;
    }

    gotoxy(1, 1);
    printf("   Address Gen.");

    unsigned char top_empty[BAR_TOTAL_TILES];
    unsigned char bot_empty[BAR_TOTAL_TILES];

    top_empty[0] = TILE_BASE + 0;
    bot_empty[0] = TILE_BASE + 23;

    for (uint8_t i = 1; i < BAR_TOTAL_TILES - 1; i++) {
        top_empty[i] = TILE_BASE + 14;
        bot_empty[i] = TILE_BASE + 37;  
    }

    top_empty[BAR_TOTAL_TILES - 1] = TILE_BASE + 7;
    bot_empty[BAR_TOTAL_TILES - 1] = TILE_BASE + 30; 

    set_bkg_tiles(BAR_X, BAR_Y,     BAR_TOTAL_TILES, 1, top_empty);
    set_bkg_tiles(BAR_X, BAR_Y + 1, BAR_TOTAL_TILES, 1, bot_empty);

    progress_ride_begin();
    progress_ride_tick(0UL, work_total);
}

/* Known abandon wallet. The phrase and the three addresses live in
   this bank. The caller's buffers are the ones address generation
   already has. A match is recorded before the caller derives the
   user's mnemonic. A mismatch leaves the byte alone. */
uint8_t run_known_addr_check(uint8_t *seed, uint8_t *priv, uint8_t *pub,
                             char *doge, char *pepe, char *bells) BANKED {
    char phrase[109];

    if (addr_check_byte() == ADDR_CHECK_DONE) return 1;

    strcpy(phrase, "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about");
    mnemonic_to_seed(phrase, seed);

#ifndef TEST_MODE
    __asm__("di");
#endif

    seed_to_addresses(seed, doge, pepe, bells, priv, pub);

    if (strcmp(doge, "DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC") != 0
        || strcmp(pepe, "PehYeRLFsRj5jboZXTC6rFHxmYdmV9RdfR") != 0
        || strcmp(bells, "BBDr846KrqMvPAUsmSsDpMraFueXWBWgih") != 0
        || !validate_checksum(doge)
        || !validate_checksum(pepe)
        || !validate_checksum(bells)) {
#ifndef TEST_MODE
        __asm__("ei");
#endif
        return 0;
    }

#ifndef TEST_MODE
    __asm__("ei");
#endif
    mark_addr_check();
    return 1;
}

/* Boot word-sum screen. Same bar as address gen, drawn in the middle,
   in the coin color. progress is 0..144 pixels. */
void prepare_rom_check_bar(void) BANKED {
    bar_draw_y = BOOT_BAR_Y;
    use_coin_bar_color();
    set_bkg_data(TILE_BASE, progress_bar_TILE_COUNT, progress_bar_tiles);

    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(BAR_X, bar_draw_y, BAR_TOTAL_TILES, 2, 6);
        VBK_REG = 0;
    }
    update_progress(0);
}

/* CRC32 of the same banks. The complement matches patch_bitrot.py. */
bool rom_verify_integrity(void) BANKED {
    uint16_t last = 0;
    uint32_t stored = 0;
    uint16_t b;
    uint32_t computed;

    read_trailer(&last, 0, &stored);
    rom_crc_open();
    for (b = 0; b <= last; b++) {
        rom_crc_bank(b);
        add_progress(WEIGHT_CRC_BANK);
    }
    rom_crc_close();

    computed = (uint32_t)crc_state[0]
             | ((uint32_t)crc_state[1] << 8)
             | ((uint32_t)crc_state[2] << 16)
             | ((uint32_t)crc_state[3] << 24);
    return (~computed) == stored;
}
