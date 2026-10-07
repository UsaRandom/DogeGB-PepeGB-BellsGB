#include <gb/gb.h>
#include <stdint.h>
#include <draw.h>
#include "menu.h"

/* The ETA and the two coins. Bank 12 so the home bank stays free. */
#pragma bank 12

extern uint8_t progress_on;

static uint8_t eta_row;
static uint8_t coin_y;
static uint8_t coin_frame;
static unsigned long coin_at;
static uint16_t eta_shown;

/* Same 8x8 spin as menu.c. Tile 0 holds, then this list. */
static const uint8_t coin_spin[12] = {1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0};
#define COIN_HOLD_MS 2000UL
#define COIN_FLIP_MS 167UL

/* Ten characters, centered: " 8:42 left" or "11:34 left". */
static void paint_eta(uint16_t secs) {
    uint8_t min = (uint8_t)(secs / 60);
    uint8_t sec = (uint8_t)(secs % 60);
    char t[11];

    t[0] = (min >= 10) ? (char)('0' + min / 10) : ' ';
    t[1] = (char)('0' + (min % 10));
    t[2] = ':';
    t[3] = (char)('0' + sec / 10);
    t[4] = (char)('0' + (sec % 10));
    t[5] = ' ';
    t[6] = 'l';
    t[7] = 'e';
    t[8] = 'f';
    t[9] = 't';
    t[10] = 0;
    draw_text(eta_row, t, 5);
}

/* Sprite edits sit in shadow OAM until VBlank. The long math leaves
   interrupts off, so copy them here. Mask IE only. Leave IME alone. */
static void copy_sprites(void) {
    uint8_t ie = IE_REG;
    IE_REG = 0;
    refresh_OAM();
    IE_REG = ie;
}

static void show_coins(uint8_t tile) {
    set_sprite_tile(0, tile);
    set_sprite_tile(1, tile);
    copy_sprites();
}

static void step_coins(unsigned long done) {
    while (done >= coin_at + ((coin_frame == 0) ? COIN_HOLD_MS : COIN_FLIP_MS)) {
        coin_at += (coin_frame == 0) ? COIN_HOLD_MS : COIN_FLIP_MS;
        coin_frame++;
        if (coin_frame > 12) coin_frame = 0;
        show_coins((coin_frame == 0) ? 0 : coin_spin[coin_frame - 1]);
    }
}

void progress_ride_begin(uint8_t address_page) BANKED {
    if (address_page) {
        eta_row = 9;
        coin_y = 88;
    } else {
        eta_row = 5;
        coin_y = 56;
    }
    eta_shown = 0xFFFF;
    coin_frame = 0;
    coin_at = 0;

    init_cursor_sprite();
    set_sprite_tile(0, 0);
    set_sprite_tile(1, 0);
    set_sprite_prop(0, 0);
    set_sprite_prop(1, 0);
    move_sprite(0, 40, coin_y);
    move_sprite(1, 128, coin_y);
    SHOW_SPRITES;
    copy_sprites();
}

void progress_ride_tick(unsigned long done, unsigned long total) BANKED {
    unsigned long left;
    unsigned long secs;
    uint16_t show;

    step_coins(done);

    left = (done >= total) ? 0UL : (total - done);
    secs = left / 1000UL;
    if (secs > 5999UL) secs = 5999UL;
    show = (uint16_t)secs;
    if (show != eta_shown) {
        eta_shown = show;
        paint_eta(show);
    }
}

void progress_ride_end(void) BANKED {
    progress_on = 0;
    move_sprite(0, 0, 0);
    move_sprite(1, 0, 0);
    copy_sprites();
}
