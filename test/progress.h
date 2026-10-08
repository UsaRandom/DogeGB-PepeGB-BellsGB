#ifndef PROGRESS_H
#define PROGRESS_H

#include <gb/gb.h>
#include <stdint.h>


void add_progress(uint16_t weight) BANKED;

void show_progress_page(char* title, char* message) BANKED;



// Tunable weights — easy to adjust during testing!
// These represent relative cost per iteration
// Base: 2048-loop iteration = 10 units
// 256-loop iteration  = 15 units (50% slower)
#define WEIGHT_PBKDF2  10u
#define WEIGHT_SECP256k1 10u
#define WEIGHT_INV 50u
#define WEIGHT_HASH160      285u
#define WEIGHT_ADDR_ENCODE  586u
#define WEIGHT_ADDR_CHECK   293u


#endif