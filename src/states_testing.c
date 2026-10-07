

#include <gb/gb.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <gbdk/console.h>
#include <gbdk/font.h>
#include <draw.h>


#include "src/crypto/mnemonic.h"
#include "src/crypto/hd_wallet.h"
#include "bitrot_rom.h"
#include "bitrot_save.h"
#include "states.h"

#include "progress.h"

extern AppState current_state;

uint8_t test_address_generation(void) {

    char* test_mnemonic = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";

    char doge_addr[35] = "DBus3bamQjgJULBJtYXpEzDWQRwF5iwxgC";
    char bells_addr[35] = "BBDr846KrqMvPAUsmSsDpMraFueXWBWgih";
    char pepe_addr[35] = "PehYeRLFsRj5jboZXTC6rFHxmYdmV9RdfR";
    

    uint8_t local_seed[64];
    uint8_t local_privkey[32];
    uint8_t local_pubkey[33];
    char    local_dogeaddress[35];
    char    local_pepeaddress[35];
    char    local_bellsaddress[35];

    mnemonic_to_seed(test_mnemonic, local_seed);

    #ifndef TEST_MODE
    __asm__("di");
    #endif

    seed_to_addresses(local_seed, local_dogeaddress, local_pepeaddress, local_bellsaddress, local_privkey, local_pubkey);

    #ifndef TEST_MODE
    __asm__("ei");
    #endif

    return validate_checksum(local_dogeaddress) == 1
        && validate_checksum(local_pepeaddress) == 1
        && validate_checksum(local_bellsaddress) == 1
        && strcmp(local_dogeaddress, doge_addr) == 0
        && strcmp(local_pepeaddress, pepe_addr) == 0
        && strcmp(local_bellsaddress, bells_addr) == 0;
}


void handle_test_menu() BANKED {

    show_progress_page();


    draw_text(1, "Testing!", 6);

    draw_text(7, "ChkSum...", 1);

    bool failed = false;
    
    if(quick_rom_verify_integrity()) {
        draw_text(7, "ChkSum...OK", 1);
    } else {
        failed = true;
        draw_text(7, "ChkSum...FAIL", 1);
    }
    
    draw_text(8, "CRC32...", 1);
    
    if(!failed && rom_verify_integrity()) {
        draw_text(8, "CRC32...OK", 1);
    } else {
        failed = true;
        draw_text(8, "CRC32...FAIL", 1);
    }


    draw_text(9, "Addr Gen...", 1);
    
    if(!failed && test_address_generation()) {
        draw_text(9, "Addr Gen...OK", 1);
    } else {
        failed = true;
        draw_text(9, "Addr Gen...FAIL", 1);
    }

    if(failed){
        draw_text(11, "  ! Tests Failed !  ", 0);
    }
    else {
        draw_text(11, "Tests Pass", 5);
    }
    draw_text(13, "Press [Start]!", 3);

    while((joypad() & J_START) == 0) {
        stir_entropy();
    }
    while(joypad()){
        stir_entropy();
    }

    progress_ride_end();
    current_state = STATE_SLOT_SELECTION;
}