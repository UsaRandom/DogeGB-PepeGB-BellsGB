#include "progress.h"
#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>
#include "src/assets/progress_bar.h"
#include <gbdk/metasprites.h>
#include "states.h"
#include "bitrot_rom.h"
#include <wallet.h>
#include <gb/cgb.h>

#pragma bank 6


uint8_t total_progress = 0;

#define TILE_BASE 200           
#define BAR_X 1
#define BAR_Y 15
#define BAR_TOTAL_TILES 18      
#define BAR_TOTAL_PX (BAR_TOTAL_TILES * 8)  

unsigned long progress_accum = 0UL;
unsigned long total_work = 0UL;
static unsigned long work_total = 1UL;
uint8_t progress_on = 0;

extern AppState current_state;
extern volatile uint8_t crc_state[4];
extern volatile uint16_t boot_sum;
extern uint8_t current_mode;

static palette_color_t bar_pal[4];

/* Fill is palette index 1. Gold, green, or bronze follows the coin. */
static void use_coin_bar_color(void) {
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

    set_bkg_tiles(BAR_X, BAR_Y,     BAR_TOTAL_TILES, 1, top_row);
    set_bkg_tiles(BAR_X, BAR_Y + 1, BAR_TOTAL_TILES, 1, bot_row);


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
    if (current_state == STATE_TESTING) {
        work_total += banks * (unsigned long)WEIGHT_SUM_BANK;
    }
    progress_on = 1;

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

    if (current_state != STATE_TESTING) {
        gotoxy(1, 1);
        printf("   Address Gen.");
    }

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

    progress_ride_begin(current_state != STATE_TESTING);
    progress_ride_tick(0UL, work_total);
}

/* Boot word-sum screen. Same bar as address gen, without that page's
   counters or copy. progress is 0..144 pixels. */
void prepare_rom_check_bar(void) BANKED {
    set_bkg_palette(6, 1, progress_bar_palettes);
    set_bkg_data(TILE_BASE, progress_bar_TILE_COUNT, progress_bar_tiles);

    if (_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(BAR_X, BAR_Y, BAR_TOTAL_TILES, 2, 6);
        VBK_REG = 0;
    }
    update_progress(0);
}

/* Word sum of banks 0..last_used. Two half-bank slices are one bank.
   Each bank moves the bar by the measured checksum time. */
bool quick_rom_verify_integrity(void) BANKED {
    uint16_t last = 0;
    uint16_t stored = 0;
    uint16_t b;

    read_trailer(&last, &stored, 0);
    boot_sum = 0;
    for (b = 0; b <= last; b++) {
        rom_sum_slice(b, 1);
        rom_sum_slice(b, 0);
        add_progress(WEIGHT_SUM_BANK);
    }
    return boot_sum == stored;
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
