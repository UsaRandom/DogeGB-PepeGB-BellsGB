#pragma bank 6

#include <stdint.h>
#include "progress.h"

/*
 * The wallet draws a tile bar from add_progress. The test ROM keeps the
 * call so the crypto sources stay unchanged, and only counts the calls.
 */
extern volatile uint16_t ticks;

void add_progress(uint16_t weight) BANKED {
    (void)weight;
    ticks++;
}

void show_progress_page(void) BANKED {
}
