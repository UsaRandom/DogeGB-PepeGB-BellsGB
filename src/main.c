
#include <gb/gb.h>
#include <gb/cgb.h>

#include <gbdk/platform.h>
#include <gbdk/font.h>
#include <gbdk/console.h>
#include <stdio.h>
#include <string.h>
#include <menu.h>

#include <splash.h>
#include <progress.h>
#include <draw.h>
#include <bonktime.h>
#include <wallet.h>
#include "wallet_sram.h"

#include "src/crypto/mnemonic.h"
#include "src/crypto/hd_wallet.h"
#include "src/crypto/secp256k1.h"
#include "src/crypto/bip39_wordlist.h"

#include "word_input.h"
#include "states.h"

#include "entropy_data.h"


unsigned char seed[64];
unsigned char privkey[32];
unsigned char pubkey[33];

char address[35];

AppState current_state;
uint8_t current_slot;
uint8_t gen_type;
char words[12][9];
uint8_t word_index;
uint8_t test_word_indices[2];
char temp_buffer[32];
wallet current_wallet;
uint8_t test_streak = 0;

uint16_t pool_ptr = 0;

uint8_t pin_set;
uint8_t pin_hash[32];
uint8_t pin_double_hash[32];

#ifndef DEFAULT_MODE
#define DEFAULT_MODE 0
#endif

uint8_t current_mode = DEFAULT_MODE;

char menu_strings[16][16];
const char* menu_options[16];


extern void handle_slot_selection(void);
extern void handle_gen_type_selection(void) BANKED;
extern void handle_bonk_game(void) BANKED;
extern void handle_word_entry(void) BANKED;
extern void handle_confirm_exit(void) BANKED;
extern void handle_random_word_test(void) BANKED;
extern void handle_show_generated_words(void) BANKED;
extern void handle_confirm_generated_words(void) BANKED;
extern void handle_generate_address(void) BANKED;
extern void handle_save_wallet(void) BANKED;
extern void handle_wallet_menu(void) BANKED;
extern void handle_bonktime_entropy(void) BANKED;
extern void handle_enter_pin(void) BANKED;
extern void handle_set_pin(void) BANKED;


extern uint8_t entropy_pool[ENTROPY_POOL_SIZE];

void stir_entropy(void){
    uint8_t sample = DIV_REG ^ LY_REG ^ STAT_REG ^ joypad();
    
    entropy_pool[pool_ptr] ^= sample;
    entropy_pool[(pool_ptr + 1) % ENTROPY_POOL_SIZE] += sample; 
    entropy_pool[(pool_ptr + 7) % ENTROPY_POOL_SIZE] ^= (sample << 1); 
    pool_ptr = (pool_ptr + 1) % ENTROPY_POOL_SIZE;
}

//custom vsync impl where instead of idle, we stir entropy.
void vsync_stir_entropy(void) {
    __critical {
        VBL_DONE = 0;
    }

    while (!VBL_DONE) {
        stir_entropy();
    }
}


void main(void)
{
    current_state = STATE_SPLASH;
    
    while (1) {
        switch (current_state) {

            case STATE_SPLASH:
                if(_cpu == CGB_TYPE) {
                    cpu_fast();
                }

                show_offline_warning();

                switch(current_mode) {
                    case DOGEGB:
                        show_doge_splash();
                        break;
                    case PEPEGB:
                        show_pepe_splash();
                        break;
                    case BELLSGB:
                        show_bells_splash();
                        break;
                    default:
                    break;
                }
                init_draw();
                SHOW_BKG;
                DISPLAY_ON;
                current_state = STATE_ENTER_PIN;
                current_slot = 0;
                gen_type = 0;
                word_index = 0;
                break;


            case STATE_ENTER_PIN:
                handle_enter_pin();
                break;
            case STATE_SET_PIN:
                handle_set_pin();
                break;

            case STATE_SLOT_SELECTION:
                handle_slot_selection();
                break;

            case STATE_BONKTIME_GAME:
                handle_bonk_game();
                break;

            case STATE_GEN_TYPE_SELECTION:
                handle_gen_type_selection();
                break;

            case STATE_BONKTIME_ENTROPY:
                handle_bonktime_entropy();
                break;

            case STATE_SHOW_GENERATED_WORDS:
                handle_show_generated_words();
                break;

            case STATE_CONFIRM_GENERATED_WORDS:
                handle_confirm_generated_words();
                break;

            case STATE_GENERATE_ADDRESS:
                handle_generate_address();
                break;

            case STATE_WALLET_MENU:
                handle_wallet_menu();
                break;

            case STATE_IDLE:
            default:
                vsync();
                break;
        }
    }
}
