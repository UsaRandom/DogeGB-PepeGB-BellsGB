#ifndef BITROT_ROM_H
#define BITROT_ROM_H

#include <stdbool.h>
#include <stdint.h>

bool rom_verify_integrity(void);
bool quick_rom_verify_integrity(void);

/* Trailer at the end of the last bank: checksum, last_used, crc.
   A null checksum or crc pointer skips that field. */
void read_trailer(uint16_t *last_used, uint16_t *checksum, uint32_t *crc);

/* Boot word sum, one half-bank per call. first_half reloads boot_ptr
   to the start of that bank. The splash calls this between frames. */
void rom_sum_slice(uint16_t bank, uint8_t first_half);

#endif