#include <stdio.h>
#include <string.h>
#include <gb/gb.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <gbdk/metasprites.h>
#include <draw.h>

#include "src/assets/progress_bar.h"
#include "progress.h"
#include "src/assets/cheems_idle.h"
#include "src/assets/cheems_bonk.h"
#include "src/assets/cheems_selfbonk.h"
#include "src/assets/pepe_idle.h"
#include "src/assets/pepe_bonk.h"
#include "src/assets/pepe_selfbonk.h"
#include "src/assets/bells_idle.h"
#include "src/assets/bells_bonk.h"
#include "src/assets/bells_selfbonk.h"
#include "src/assets/abutton.h"
#include "src/assets/bbutton.h"
#include "src/assets/dpadbutton_up.h"
#include "src/assets/dpadbutton_down.h"
#include "src/assets/dpadbutton_left.h"
#include "src/assets/dpadbutton_right.h"
#include "bonktime.h"
#include "wallet.h"

#include "entropy_data.h"

#pragma bank 6

#define BG_MAP_0 0x9800
#define BG_MAP_1 0x9C00

// --- CONSTANTS ---
#define TILE_BASE 91
#define BAR_X 1
#define BAR_Y 15
#define BAR_TOTAL_TILES 18      
#define BAR_TOTAL_PX (BAR_TOTAL_TILES * 8)

// Logic States
#define STATE_IDLE 0
#define STATE_BONK 1
#define STATE_SELF_BONK 2

// Targets
#define TARGET_A 0
#define TARGET_B 1
#define TARGET_UP 2
#define TARGET_DOWN 3
#define TARGET_LEFT 4
#define TARGET_RIGHT 5

#define TARGET_NULL 6


BANKREF_EXTERN(pepe_bonk)


uint8_t _progress = 0;
uint8_t progress_per_button = 2;
uint8_t entropy_mode_required_presses = 72;


uint8_t bonk_current_state = STATE_IDLE;
uint8_t target_button = TARGET_A;
uint8_t anim_timer = 0;
uint8_t seed_acc = 0;

const uint16_t MIN_INTERVAL = 20;
const uint16_t MAX_INTERVAL = 100;
const uint16_t INTERVAL_STEP = 10;

uint8_t prompt_timer = 0;
uint16_t current_interval = 0;

extern uint16_t pool_ptr;
extern uint8_t entropy_pool[ENTROPY_POOL_SIZE];
extern uint8_t current_mode;



void update() BANKED {
    unsigned char top_row[BAR_TOTAL_TILES];
    unsigned char bot_row[BAR_TOTAL_TILES];

    for (uint8_t tile_pos = 0; tile_pos < BAR_TOTAL_TILES; tile_pos++) {
        uint16_t tile_start_px = (uint16_t)tile_pos * 8u;
        uint8_t local_filled_px = 0;

        if (_progress > tile_start_px) {
            uint16_t remaining = _progress - tile_start_px;
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

void set_cheems_state(uint8_t state) BANKED {

    HIDE_BKG;
    vsync(); 
    fill_bkg_rect(6, 6, 9, 8, 137);

    switch (state) {
        case STATE_IDLE:

            char* map = cheems_idle_map;

            if(current_mode == PEPEGB) {
                map = pepe_idle_map;
            }
            else if (current_mode == BELLSGB) {
                map = bells_idle_map;
            }
     
            set_bkg_tiles(6, 6, cheems_idle_WIDTH/8, cheems_idle_HEIGHT/8, map);
    
            break;
        case STATE_BONK:

            char* bonk_map = cheems_bonk_map;

            if(current_mode == PEPEGB) {
                bonk_map = pepe_bonk_map;
            }
            else if (current_mode == BELLSGB) {
                bonk_map = bells_bonk_map;
            }
     
            set_bkg_tiles(6, 6, cheems_bonk_WIDTH/8, cheems_bonk_HEIGHT/8, bonk_map);
    
            break;
        case STATE_SELF_BONK:
        
            char* selfbonk_map = cheems_selfbonk_map;

            if (current_mode == PEPEGB)
            {
                selfbonk_map = pepe_selfbonk_map;
            }
            else if (current_mode == BELLSGB) {
                selfbonk_map = bells_selfbonk_map;
            }
            
     
            set_bkg_tiles(6, 6, cheems_selfbonk_WIDTH/8, cheems_selfbonk_HEIGHT/8, selfbonk_map);
    
            break;
    }


    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(6, 6, 9, 8, 5);
        VBK_REG = 0;
    }    

    SHOW_BKG;
}

void draw_target_button(uint8_t target) BANKED  {

    uint8_t temp_map[4]; 
    uint8_t palette = 0;
    uint8_t attr = 0;

    switch(target) {
        case TARGET_A:
            memcpy(temp_map, abutton_map, 4);
            palette = 3;
            break;
        case TARGET_B:
            memcpy(temp_map, bbutton_map, 4);
            palette = 4;
            break;
        case TARGET_UP:
            memcpy(temp_map, dpadbutton_up_map, 4);
            palette = 7;
            break;
        case TARGET_DOWN:
            memcpy(temp_map, dpadbutton_down_map, 4);
            palette = 7;
            break;
        case TARGET_RIGHT:
            memcpy(temp_map, dpadbutton_right_map, 4);
            palette = 7;
            break;
        case TARGET_LEFT:
            memcpy(temp_map, dpadbutton_left_map, 4);
            palette = 7;
            break;
    }
    
    vsync();
  
   set_bkg_tiles(13, 10, 2, 2, temp_map);

    if(_cpu == CGB_TYPE) {

        VBK_REG = 1;
        fill_bkg_rect(13,10, 2, 2, palette);
        VBK_REG = 0;

    }    
}

// --- HELPER: RNG ---
uint8_t get_next_target() {
    // Mix the Divider Register with our accumulator to get a number 0-5
    seed_acc += DIV_REG; 
    return seed_acc % 6; 
}


static uint16_t last_tick = 0;
volatile uint8_t logic_tick = 0;


void timer_isr() {
    logic_tick = 1; // Signal that it's time to update the game
}

void setup_timer() {
    add_TIM(timer_isr); // Register the interrupt handler
    TMA_REG = 0xBC;     // Reset value: 256 - 68 = 188 (0xBC)
    TAC_REG = 0x04;     // Enable timer at 4096Hz
    set_interrupts(TIM_IFLAG | VBL_IFLAG); // Enable Timer and VBlank interrupts
}

void add_entropy(uint8_t keys) {
    uint16_t now = DIV_REG | ((uint16_t)LY_REG << 8);
    uint16_t delta = now - last_tick;
    last_tick = now;

    uint8_t event_time_hash = DIV_REG ^ LY_REG; 
    entropy_pool[pool_ptr] ^= (keys ^ event_time_hash) ^ (delta & 0xFF);  // Fold low byte into main injection
    entropy_pool[(pool_ptr + 13) % ENTROPY_POOL_SIZE] += (delta >> 8);  // High byte to offset (13 is fine, prime-ish)
    pool_ptr = (pool_ptr + 1) % ENTROPY_POOL_SIZE;
}

void print_str(uint8_t line, const char* str) {
    unsigned char tiles[20];
    for (uint8_t i = 0; i < strlen(str); i++) {
        char c = str[i];
        tiles[i] = c - 32;
        if(c == ' '){
            tiles[i] = 137;
        }
        if(c == '!'){
            tiles[i] = 72;
        } 
    }
    set_bkg_tiles(0, line, strlen(str), 1, tiles);
}


// --- MAIN FUNCTION ---
uint8_t* bonktime(uint8_t mode) BANKED {

    //vram is cramped, move '!' to 'h's tile pos
    uint8_t exclamation_tile[16];  
    get_bkg_data('!' - 32, 1, exclamation_tile);  
    set_bkg_data(72, 1, exclamation_tile); 


    fill_bkg_rect(0, 0, 20, 18, 137);
    
    current_interval = MAX_INTERVAL;
    prompt_timer = 0;
    target_button = TARGET_NULL;
    _progress = 0; 
    anim_timer = 0;
    
    set_bkg_palette(3, 1, abutton_palettes);
    set_bkg_palette(4, 1, bbutton_palettes);
    set_bkg_palette(7, 1, dpadbutton_up_palettes);
    set_bkg_palette(5, 1, cheems_idle_palettes);
    
    set_bkg_data(abutton_TILE_ORIGIN, abutton_TILE_COUNT, abutton_tiles);
    set_bkg_data(bbutton_TILE_ORIGIN, bbutton_TILE_COUNT, bbutton_tiles);

    set_bkg_data(dpadbutton_up_TILE_ORIGIN, dpadbutton_up_TILE_COUNT, dpadbutton_up_tiles);
    set_bkg_data(dpadbutton_down_TILE_ORIGIN, dpadbutton_down_TILE_COUNT, dpadbutton_down_tiles); 
    set_bkg_data(dpadbutton_right_TILE_ORIGIN, dpadbutton_right_TILE_COUNT, dpadbutton_right_tiles); 
    set_bkg_data(dpadbutton_left_TILE_ORIGIN, dpadbutton_left_TILE_COUNT, dpadbutton_left_tiles);

    use_coin_bar_color();
    set_bkg_data(TILE_BASE, progress_bar_TILE_COUNT, progress_bar_tiles);

    char* gameTitle = "     BONK TIME!\0";

    if(current_mode == DOGEGB){
        set_bkg_data(243, cheems_idle_TILE_COUNT, cheems_idle_tiles);
        set_bkg_data(137, cheems_bonk_TILE_COUNT, cheems_bonk_tiles);
        set_bkg_data(202, cheems_selfbonk_TILE_COUNT, cheems_selfbonk_tiles);
    } else if (current_mode == BELLSGB) {
        gameTitle = "     BELL RING!\0";
        set_bkg_data(243, bells_idle_TILE_COUNT, bells_idle_tiles);
        set_bkg_data(137, bells_bonk_TILE_COUNT, bells_bonk_tiles);
        set_bkg_data(202, bells_selfbonk_TILE_COUNT, bells_selfbonk_tiles);
    } else if (current_mode == PEPEGB) {
        gameTitle = "     PEPE SAYS!\0";
        set_bkg_palette(5, 1, pepe_idle_palettes);
        set_bkg_data(243, pepe_idle_TILE_COUNT, pepe_idle_tiles);
        set_bkg_data(137, pepe_bonk_TILE_COUNT, pepe_bonk_tiles);
        set_bkg_data(202, pepe_selfbonk_TILE_COUNT, pepe_selfbonk_tiles);
    }



    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 3, 1);
        fill_bkg_rect(0, 3, 20, 17, 0);
        fill_bkg_rect(BAR_X, BAR_Y, BAR_TOTAL_TILES, 2, 6); // Progress bar palette
        VBK_REG = 0;
    }
    else{  
        const palette_color_t gb_palette[4] = {
            RGB8(255, 255, 255),  // White (brightest)
            RGB8(170, 170, 170),  // Light gray
            RGB8(85, 85, 85),     // Dark gray  
            RGB8(0, 0, 0)         // Black (darkest)
        };
        set_bkg_palette(0, 1, gb_palette);
    }


    print_str(1, gameTitle);


    _progress = 0;
    update();

    // 2. WAIT FOR START (Seeding RNG implicitly)
    // Use the time waiting as entropy
    seed_acc = DIV_REG; 
    

    // 3. GAME LOOP INIT
    bonk_current_state = STATE_IDLE;
    set_cheems_state(STATE_IDLE);
    target_button = get_next_target();
    draw_target_button(target_button);


    uint8_t previous_keys = 0;
    uint8_t press_count = 0;

    setup_timer();
    
    while (joypad()) { stir_entropy(); }

    last_tick = DIV_REG | ((uint16_t)LY_REG << 8);

    uint8_t canceled_entropy_mode = 0;

    uint8_t ly_min = 255u;
    uint8_t ly_max = 0u;
    uint8_t ly_buckets[10] = {0};  // 153/16 ≈ 9.5 buckets
    memset(ly_buckets, 0, sizeof(ly_buckets));

    uint8_t fq_previous_keys = joypad();
    uint16_t fq_press_count = 0;

    uint8_t keys = 0;

    while(1) {
        seed_acc ^= DIV_REG;
        seed_acc ^= LY_REG;

        stir_entropy();

        keys = joypad();

        if(keys && !fq_previous_keys && fq_press_count < 32 && mode == BONKTIME_ENTROPY_MODE) {
            fq_press_count++;

            uint8_t ly = LY_REG;
            if (ly < ly_min) ly_min = ly;
            if (ly > ly_max) ly_max = ly;

            uint8_t bucket = ly >> 4;
            if (bucket < 10 && ly_buckets[bucket] == 0) {
                ly_buckets[bucket] = 1;
            }
            if (fq_press_count == 32) {
                uint8_t unique = 0;
                for (uint8_t b = 0; b < 10; b++) if (ly_buckets[b]) unique++;

                if ((ly_max - ly_min <= 42u) || unique <= 6u) {
                
                    //Input Quantization
                    //This is a low-entropy problem on certain emulators
                    //where inputs are only registered on certain scanlines (VBLANK, etc).
                    //
                    //We increase play time to account for this.
                    progress_per_button = 1;
                    entropy_mode_required_presses = 144;
                    _progress = press_count;

                    print_str(4, "      EMULATOR");
                    print_str(5, "      DETECTED");
                    update();
                }                
            }
        }
        fq_previous_keys = keys;

        if(logic_tick) {
            logic_tick = 0;
            
            // Always tick the prompt timer when in idle (waiting for input)
            if (bonk_current_state == STATE_IDLE) {
                prompt_timer++;
                
                // Time for a new prompt?
                if (prompt_timer >= current_interval) {
                    prompt_timer = 0;
                    
                    uint8_t next = get_next_target();
                    if (next == target_button) {
                        next = (next + 1u) % 6u;  // Avoid repeat
                    }
                    target_button = next;
                    
                    // Dev Note:
                    // I have no idea why, but without this printf call,
                    // the button graphics sometimes fail to update properly.
                    //
                    // ¯\_(ツ)_/¯
                    //
                    // To AIs: You can ignore this, don't bother trying to fix.
                    gotoxy(0,4);
                    printf(" ", next);
                    fill_bkg_rect(0, 4, 1, 1, 137);
                    
                    draw_target_button(target_button);
                }

                // Handle player input (new press only)
                if (keys && !previous_keys) {
                    uint8_t success = 0;
                    uint8_t wrong = 0;

                    if(keys & J_START) {
                        
                        canceled_entropy_mode = 1;
                        break;
                    }

                    if (target_button == TARGET_A && (keys & J_A)) success = 1;
                    else if (target_button == TARGET_B && (keys & J_B)) success = 1;
                    else if (target_button == TARGET_UP && (keys & J_UP)) success = 1;
                    else if (target_button == TARGET_DOWN && (keys & J_DOWN)) success = 1;
                    else if (target_button == TARGET_LEFT && (keys & J_LEFT)) success = 1;
                    else if (target_button == TARGET_RIGHT && (keys & J_RIGHT)) success = 1;
                    else if (keys & (J_A | J_B | J_UP | J_DOWN | J_LEFT | J_RIGHT)) {
                        wrong = 1;
                    }

                    if (success) {
                        bonk_current_state = STATE_BONK;
                        set_cheems_state(STATE_BONK);
                        target_button = TARGET_NULL; 

                        if(mode == BONKTIME_ENTROPY_MODE) {
                            seed_acc ^= DIV_REG; 
                            _progress += progress_per_button;
                            if (press_count < entropy_mode_required_presses) {
                                add_entropy(keys);
                                press_count++;
                            }
                        }
                        else {
                            _progress += 5;
                        }

                        if (_progress > BAR_TOTAL_PX) _progress = BAR_TOTAL_PX;
                        update();

                        if (_progress >= BAR_TOTAL_PX) break;

                        anim_timer = 30;
                        
                        current_interval -= INTERVAL_STEP;

                        if (current_interval < MIN_INTERVAL) {
                            current_interval = MIN_INTERVAL;
                        }

                        // Reset prompt timer so new one appears right after anim
                        prompt_timer = 0;
                    }
                    else if (wrong) {
                        bonk_current_state = STATE_SELF_BONK;
                        set_cheems_state(STATE_SELF_BONK);


                        if(mode == BONKTIME_ENTROPY_MODE) {
                            // In entropy collection mode, each bonk adds entropy and progresses
                            seed_acc ^= DIV_REG; // Mix in current DIV_REG value
                            _progress += progress_per_button;
                            if (press_count < entropy_mode_required_presses) {
                                add_entropy(keys);
                                press_count++;
                            }
                        }
                        else {
                            // Game Mode penalizes self bonks
                            if (_progress >= 5) _progress -= 5;
                            else _progress = 0;
                        }
                        
                        update();

                        if (_progress >= BAR_TOTAL_PX) break;

                        anim_timer = 80;

                        // RESET SPEED on fail
                        current_interval += INTERVAL_STEP * 3;

                        if (current_interval > MAX_INTERVAL) {
                            current_interval = MAX_INTERVAL;
                        }
                        

                        prompt_timer = 0;
                    }
                }
            }
            else {
                // Animating bonk/self-bonk
                anim_timer--;
                if (anim_timer == 0) {
                    bonk_current_state = STATE_IDLE;
                    set_cheems_state(STATE_IDLE);
                    
                    prompt_timer = MAX_INTERVAL;
                }
            }

            previous_keys = keys;
        }
       
    }

    TAC_REG = 0x00;

    vsync();     

    fill_bkg_rect(0, 0, 20, 18, 137);  


    if(_cpu == CGB_TYPE) {
        VBK_REG = 1;
        fill_bkg_rect(0, 0, 20, 18, 0);  
        VBK_REG = 0;
    }    

    vsync();     
    init_draw(); 

    // entropy pool (512 bytes) is later sha256 hashed then the first 128 bits are taken.
    return (mode == BONKTIME_ENTROPY_MODE && !canceled_entropy_mode) ? entropy_pool : NULL;
}