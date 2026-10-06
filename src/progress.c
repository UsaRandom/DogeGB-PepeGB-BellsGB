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

#pragma bank 6


uint8_t total_progress = 0;

#define TILE_BASE 200           
#define BAR_X 1
#define BAR_Y 15
#define BAR_TOTAL_TILES 18      
#define BAR_TOTAL_PX (BAR_TOTAL_TILES * 8)  

const unsigned long TOTAL_WORK_UL = (unsigned long)2048UL * WEIGHT_PBKDF2 +
                                    3UL * 256UL * WEIGHT_SECP256k1; 

unsigned long progress_accum = 0UL;
unsigned long total_work = 0UL;

extern AppState current_state;



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
    if (total_progress >= BAR_TOTAL_PX) return;

    total_work += (unsigned long)weight;
    progress_accum += (unsigned long)weight * (unsigned long)BAR_TOTAL_PX;


    if (progress_accum >= TOTAL_WORK_UL) {
        progress_accum -= TOTAL_WORK_UL;
        total_progress++;

        if (total_progress > BAR_TOTAL_PX) {
            total_progress = BAR_TOTAL_PX;
        }


        if(total_work >= TOTAL_WORK_UL && current_state != STATE_TESTING){
            gotoxy(0,8);
            printf("     Just a bit    \n");
            printf("       longer      ");
        }

        update_progress(total_progress);
    }

}


uint8_t text_x_pos(const char* str) {
    uint8_t len = strlen(str);
    if (len > 18) len = 18;
    return (20 - len) / 2;
}


void show_progress_page() BANKED {
    vsync();
    clear_screen();
    total_work = 0UL;
    total_progress = 0;
    progress_accum = 0;
    
    set_bkg_palette(6, 1, progress_bar_palettes);

    set_bkg_data(TILE_BASE, progress_bar_TILE_COUNT, progress_bar_tiles);

    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(0, BAR_Y-1, 20, BAR_Y-3, 0); 
        fill_bkg_rect(BAR_X, BAR_Y, BAR_TOTAL_TILES, 2, 6); 
        fill_bkg_rect(0, BAR_Y + 2, 20, 7, 0); 
        VBK_REG = 0;
    }

    if(current_state != STATE_TESTING)
    {
        gotoxy(1,1);
        printf("   Address Gen.");

        gotoxy(0,8);
        printf("   This will take\n");
        printf("    a long time.");
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
