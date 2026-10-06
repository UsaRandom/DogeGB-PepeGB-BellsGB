        .module bitrot_sum
        .area _CODE

; Boot checksum inner loop. Borrow SP, pop 16-bit words, ADD HL, BC.
; 32 pops per pass. boot_passes is the pass count; 0 means 256,
; which is one 16KB bank. 128 is half a bank, so the splash can
; yield between halves. The advanced window pointer is written back
; to boot_ptr. Interrupts must already be off: DI is delayed one
; instruction, and an interrupt after SP moves would push into ROM.
; patch_bitrot.py stores this same little-endian word sum.

        .globl _sum_win
        .globl _boot_sum
        .globl _boot_ptr
        .globl _boot_passes

_sum_win::
        di
        push    bc
        push    de
        push    hl
        ld      (#_boot_sp), sp
        ld      a, (#_boot_ptr)
        ld      l, a
        ld      a, (#_boot_ptr + 1)
        ld      h, a
        ld      sp, hl
        ld      a, (#_boot_sum)
        ld      l, a
        ld      a, (#_boot_sum + 1)
        ld      h, a
        ld      a, (#_boot_passes)
001$:
        .rept 32
        pop     bc
        add     hl, bc
        .endm
        dec     a
        jr      nz, 001$
        ld      (#_boot_ptr), sp
        ld      a, l
        ld      (#_boot_sum), a
        ld      a, h
        ld      (#_boot_sum + 1), a
        ld      a, (#_boot_sp)
        ld      l, a
        ld      a, (#_boot_sp + 1)
        ld      h, a
        ld      sp, hl
        pop     hl
        pop     de
        pop     bc
        ret

        .area _BSS
_boot_sum::
        .ds 2
_boot_ptr::
        .ds 2
_boot_sp::
        .ds 2
_boot_passes::
        .ds 1
