
#include <gb/gb.h>

void add_progress(uint16_t weight) BANKED;

void show_progress_page() BANKED;

void prepare_rom_check_bar(void) BANKED;
void update_progress(uint8_t progress) BANKED;



#define WEIGHT_PBKDF2  10UL
#define WEIGHT_SECP256k1 53UL


