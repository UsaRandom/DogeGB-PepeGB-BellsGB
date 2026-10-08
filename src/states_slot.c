

#include <gb/gb.h>
#include <stdio.h>
#include <string.h>
#include <menu.h>
#include <wallet.h>
#include "states.h"
#include "splash.h"
#include "draw.h"

#pragma bank 1

extern AppState current_state;
extern uint8_t current_slot;
extern char words[12][9];
extern uint8_t gen_type;
extern wallet current_wallet;
extern char temp_buffer[32];
extern uint8_t current_mode;

static const char* dogeGBTitle = "DogeGB";
static const char* bellsGBTitle = "BellsGB";
static const char* pepeGBTitle = "PepeGB";

void handle_slot_selection(void) {
    char slot_display[MAX_SLOTS][SLOT_DISPLAY_LEN];

    list_slots(current_mode, slot_display);

    
    char* gameTitle = "[BONK TIME!]\0";

    if(current_mode == PEPEGB){
        gameTitle = "[PEPE SAYS!]\0";
    }
    else if(current_mode == BELLSGB) {
        gameTitle = "[BELL RING!]\0";
    }
    
    const char* menu_options[MAX_SLOTS + 4] = {
        slot_display[0],
        slot_display[1],
        slot_display[2],
        slot_display[3],
        slot_display[4],
        slot_display[5],
        slot_display[6],
        slot_display[7],
        "",
        gameTitle,
        "",
        "[Set PIN]"
    };


    const char* title;

    switch(current_mode){
        case DOGEGB:
            title = dogeGBTitle;
            break;
        case BELLSGB:
            title = bellsGBTitle;
            break;
        case PEPEGB:
            title = pepeGBTitle;
            break;
        default:
            break;
    }
    

    int8_t slot = show_menu(title, menu_options, MAX_SLOTS + 4);
    if (slot >= 0 && slot < MAX_SLOTS) {
        current_slot = slot + 1;

        if(slot_display[slot][0] != 'E') {
            wallet result;
            get_wallet(current_slot, current_mode, &result);
            
            current_wallet = result;

            current_state = STATE_WALLET_MENU;
        } else {
            current_state = STATE_GEN_TYPE_SELECTION;
        }
    }

    if(slot == MAX_SLOTS + 1) {
        current_state = STATE_BONKTIME_GAME;
    }

    if(slot == MAX_SLOTS + 3) {
        current_state = STATE_SET_PIN;
    }

    if(slot == -2){
        const char* menu_options[3] = {
            "DogeGB",
            "BellsGB",
            "PepeGB"
        };

        int8_t modeSelection = 0;
        while((modeSelection = show_menu("Secret Menu", menu_options, 3)) < 0);

        
        current_mode = modeSelection;
        init_draw();
            
    }
}

void handle_gen_type_selection(void) BANKED {
    char* genTypes[] = {
        "New Wallet",
        "Load Wallet"
    };

    sprintf(temp_buffer, "Wallet Slot #%d", current_slot);
    int8_t selected_type = show_menu(temp_buffer, genTypes, 2);
    if (selected_type >= 0) {
        gen_type = selected_type;
        memset(words, 0, sizeof(words));
        if (gen_type == 0) {
            current_state = STATE_BONKTIME_ENTROPY;
        } else {
            current_state = STATE_SHOW_GENERATED_WORDS;
        }
    }
    else {
        current_state = STATE_SLOT_SELECTION;
    }
}
