#include <gb/gb.h>
#include <gb/cgb.h>
#include <stdio.h>
#include <gbdk/console.h>

#include "src/assets/offlineonly.h"
#include "src/assets/bork.h"
#include "src/assets/pepelogo.h"
#include "src/assets/bellslogo.h"

#include "bitrot_rom.h"
#include "draw.h"
#include "progress.h"


#pragma bank 7

static unsigned char blank_tile = 0;

static uint16_t scan_last;
static uint16_t scan_stored;
static uint16_t scan_bank;
static uint8_t scan_half;
static uint8_t scan_on;
extern volatile uint16_t boot_sum;

static void scan_begin(void) {
    if (scan_on) return;
    read_trailer(&scan_last, &scan_stored, 0);
    boot_sum = 0;
    scan_bank = 0;
    scan_half = 0;
    scan_on = 1;
}

static uint8_t scan_finished(void) {
    return scan_bank > scan_last;
}

static void scan_slice(void) {
    if (scan_finished()) return;
    rom_sum_slice(scan_bank, scan_half == 0);
    if (scan_half == 0) {
        scan_half = 1;
    } else {
        scan_half = 0;
        scan_bank++;
    }
}

static uint16_t scan_mark;
static uint16_t scan_span;
static uint16_t scan_acc;
static uint16_t scan_applied;
static uint8_t scan_px;

static uint16_t scan_done_slices(void) {
    return ((uint16_t)scan_bank << 1) + scan_half;
}

/* The splash screens already ran part of the sum. The bar covers only
   what is left, and it is drawn from empty. */
static void scan_bar_reset(void) {
    uint16_t total = ((uint16_t)scan_last + 1u) << 1;
    uint16_t done = scan_done_slices();

    scan_mark = done;
    scan_applied = 0;
    scan_acc = 0;
    scan_px = 0;
    scan_span = (done >= total) ? 0 : (uint16_t)(total - done);
}

static void scan_bar_sync(void) {
    uint16_t done;
    uint16_t advanced;

    if (scan_span == 0) {
        scan_px = 144;
        return;
    }
    done = scan_done_slices();
    if (done <= scan_mark) return;
    advanced = done - scan_mark;
    while (scan_applied < advanced) {
        scan_acc += 144;
        while (scan_acc >= scan_span && scan_px < 144) {
            scan_acc -= scan_span;
            scan_px++;
        }
        scan_applied++;
    }
}

static void scan_frame(void) {
    vsync();
    scan_slice();
}

static void show_rom_check_screen(void) {
    uint8_t n;

    init_draw();
    clear_screen();
    if (_cpu != CGB_TYPE) {
        BGP_REG = 0xE4;
        OBP0_REG = 0xE4;
        OBP1_REG = 0xE4;
    }
    prepare_rom_check_bar();
    gotoxy(4, BOOT_CHECK_TEXT_Y);
    printf("Checking ROM");
    scan_bar_reset();

    vsync();
    update_progress(0);

    while (!scan_finished()) {
        for (n = 0; n < 8 && !scan_finished(); n++) scan_slice();
        vsync();
        scan_bar_sync();
        update_progress(scan_px);
    }
    vsync();
    update_progress(144);

    if (boot_sum != scan_stored) {
        gotoxy(2, 12);
        printf("Corrupted ROM!");
        while (1) vsync();
    }
}

const palette_color_t white[4] = {
    RGB8(255,255,255), RGB8(255,255,255),
    RGB8(255,255,255), RGB8(255,255,255)
};


void fade_palette(const palette_color_t* start_pal, const palette_color_t* target_pal, uint8_t scan) {

    if(_cpu == CGB_TYPE)
    {
        palette_color_t fade_pal[4];

        for (uint8_t step = 0; step <= 8; step++) {
            for (uint8_t i = 0; i < 4; i++) {
                uint8_t start_r = (start_pal[i] >> 0)  & 0x1F;
                uint8_t start_g = (start_pal[i] >> 5)  & 0x1F;
                uint8_t start_b = (start_pal[i] >> 10) & 0x1F;

                uint8_t target_r = (target_pal[i] >> 0)  & 0x1F;
                uint8_t target_g = (target_pal[i] >> 5)  & 0x1F;
                uint8_t target_b = (target_pal[i] >> 10) & 0x1F;

                uint8_t r = start_r + ((target_r - start_r) * step / 8);
                uint8_t g = start_g + ((target_g - start_g) * step / 8);
                uint8_t b = start_b + ((target_b - start_b) * step / 8);

                fade_pal[i] = r | (g << 5) | (b << 10);
            }
            set_bkg_palette(0, 1, fade_pal);
            for (uint8_t f = 0; f < 6; f++) {
                if (scan) scan_frame();
                else vsync();
            }
        }

        set_bkg_palette(0, 1, target_pal);
    }
    else {
        //OG gameboy has to fade from black otherwise it looks funky
        static const uint8_t black_to_image[] = {0xFF, 0xFE, 0xF9, 0xE4};
        static const uint8_t image_to_black[] = {0xE4, 0xF9, 0xFE, 0xFF};
        
        const uint8_t* steps = (start_pal == white) ? 
                               black_to_image : image_to_black;
        
        for (uint8_t i = 0; i < 4; i++) {
            BGP_REG  = steps[i];
            OBP0_REG = steps[i];
            OBP1_REG = steps[i];
            for (uint8_t f = 0; f < 12; f++) {
                if (scan) scan_frame();
                else vsync();
            }
        }
    }
   
}

void show_splash(
    uint16_t tile_origin,
    const uint8_t* tiles,
    uint16_t tile_count,
    const uint8_t* map,
    uint8_t map_width,
    uint8_t map_height,
    const palette_color_t* palette,
    uint8_t scan,
    uint8_t finish_check
)  {  
    DISPLAY_OFF;
    HIDE_SPRITES;
    SHOW_BKG;
    fill_bkg_rect(0, 0, 20, 18, blank_tile);
    VBK_REG = 1;
    fill_bkg_rect(0, 0, 20, 18, 0);
    VBK_REG = 0;

    set_bkg_palette(0, 1, white);

    set_bkg_data(tile_origin, tile_count, tiles);
    uint8_t start_x = (20 - map_width) / 2;
    uint8_t start_y = (18 - map_height) / 2;
    set_bkg_tiles(start_x, start_y, map_width, map_height, map);

    palette_color_t asset_palette[4];
    for (uint8_t i = 0; i < 4; i++) {
        asset_palette[i] = palette[i];
    }
    
    if (scan) scan_begin();

    DISPLAY_ON;
    fade_palette(white, asset_palette, scan);

    for (uint8_t i = 0; i < 90 && !joypad(); i++) {
        if (scan) scan_frame();
        else vsync();
    }

    fade_palette(asset_palette, white, scan);

    if (finish_check) {
        show_rom_check_screen();
        return;
    }

    for (uint8_t y = 0; y < 18; y++) {
        for (uint8_t x = 0; x < 20; x++) {
            set_bkg_tiles(x, y, 1, 1, &blank_tile);
        }
    }
    if (_cpu != CGB_TYPE) {
        BGP_REG = 0xE4;
        OBP0_REG = 0xE4;
        OBP1_REG = 0xE4;
    }
}

void show_pepe_splash(void) BANKED {
    show_splash(
        pepelogo_TILE_ORIGIN,
        pepelogo_tiles,
        pepelogo_TILE_COUNT,
        pepelogo_map,
        pepelogo_MAP_ATTRIBUTES_WIDTH,
        pepelogo_MAP_ATTRIBUTES_HEIGHT,
        pepelogo_palettes,
        1,
        1
    );
}

void show_doge_splash(void) BANKED {
    show_splash(
        bork_TILE_ORIGIN,
        bork_tiles,
        bork_TILE_COUNT,
        bork_map,
        bork_MAP_ATTRIBUTES_WIDTH,
        bork_MAP_ATTRIBUTES_HEIGHT,
        bork_palettes,
        1,
        1
    );
}

void show_bells_splash(void) BANKED {
    show_splash(
        bellslogo_TILE_ORIGIN,
        bellslogo_tiles,
        bellslogo_TILE_COUNT,
        bellslogo_map,
        bellslogo_MAP_ATTRIBUTES_WIDTH,
        bellslogo_MAP_ATTRIBUTES_HEIGHT,
        bellslogo_palettes,
        1,
        1
    );
}

void show_offline_warning(void) BANKED {
    show_splash(
        offlineonly_TILE_ORIGIN,
        offlineonly_tiles,
        offlineonly_TILE_COUNT,
        offlineonly_map,
        offlineonly_MAP_ATTRIBUTES_WIDTH,
        offlineonly_MAP_ATTRIBUTES_HEIGHT,
        offlineonly_palettes,
        1,
        0
    );

}