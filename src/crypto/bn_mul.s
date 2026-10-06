        .module bn_mul
        .area _CODE

; 32x32 schoolbook multiply using the ROM product table in banks 20-27.
; One bank switch per outer byte. Callers leave interrupts off.
; Pointers and the 64-byte product live in BSS. The caller fills
; bn_mul_pa, bn_mul_pb, and bn_saved_bank (the bank to restore).

MUL_BASE        = 20

        .globl _bn_mul256_asm
        .globl _bn_sqr256_asm
        .globl _bn_mul_pa
        .globl _bn_mul_pb
        .globl _bn_mul_prod
        .globl _bn_saved_bank

_bn_mul256_asm::
        xor     a
        ld      (0x3000), a

        ld      hl, #_bn_mul_prod
        ld      b, #64
        xor     a
bn_mul_clr:
        ld      (hl+), a
        dec     b
        jr      nz, bn_mul_clr

        ld      a, #31
        ld      (#_bn_i), a

bn_mul_outer:
        ; ai = pa[i]
        ld      hl, #_bn_mul_pa
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (#_bn_i)
        ld      e, a
        ld      d, #0
        add     hl, de
        ld      a, (hl)
        ld      (#_bn_ai), a

        ; bank = 20 + (ai >> 5). Code is in bank 0, so the switch is safe.
        ld      a, (#_bn_ai)
        rlca
        rlca
        rlca
        and     #0x07
        add     a, #MUL_BASE
        ld      (0x2000), a

        ; H of the low page = 0x40 + ((ai & 31) << 1)
        ld      a, (#_bn_ai)
        and     #0x1F
        add     a, a
        add     a, #0x40
        ld      (#_bn_page), a

        ; DE = pb + 31
        ld      hl, #_bn_mul_pb
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      de, #31
        add     hl, de
        ld      d, h
        ld      e, l

        ; HL = prod + i + 32
        ld      hl, #_bn_mul_prod
        ld      a, (#_bn_i)
        add     a, #32
        ld      c, a
        ld      b, #0
        add     hl, bc

        ld      a, (#_bn_page)
        ld      c, a
        ld      b, #0
        ld      a, #32
        ld      (#_bn_n), a

bn_mul_inner:
        ; B = carry, C = page, DE = b pointer, HL = product column
        ld      a, (de)
        dec     de
        push    de
        push    hl
        ld      l, a
        ld      h, c
        ld      e, (hl)
        inc     h
        ld      d, (hl)
        pop     hl
        ld      a, (hl)
        add     a, e
        jr      nc, bn_mul_nohi
        inc     d
bn_mul_nohi:
        add     a, b
        ld      (hl), a
        ld      a, d
        adc     a, #0
        ld      b, a
        pop     de
        dec     hl
        ld      a, (#_bn_n)
        dec     a
        ld      (#_bn_n), a
        jr      nz, bn_mul_inner

        ; HL is prod[i]. Carry fits in B. Ripple if the byte overflows.
        ld      a, (hl)
        add     a, b
        ld      (hl), a
        jr      nc, bn_mul_row_done
bn_mul_ripple:
        ld      a, l
        cp      #<(_bn_mul_prod)
        jr      nz, bn_mul_ripple_go
        ld      a, h
        cp      #>(_bn_mul_prod)
        jr      z, bn_mul_row_done
bn_mul_ripple_go:
        dec     hl
        ld      a, (hl)
        inc     a
        ld      (hl), a
        jr      z, bn_mul_ripple
bn_mul_row_done:
        ld      a, (#_bn_i)
        or      a
        jr      z, bn_mul_finish
        dec     a
        ld      (#_bn_i), a
        jp      bn_mul_outer

bn_mul_finish:
        ld      a, (#_bn_saved_bank)
        ld      (0x2000), a
        ret

; 32x32 square. Diagonal terms once, cross terms doubled.
; Column carry is 16-bit: three bytes in one column can spill by 2,
; and a doubled product has a 9-bit high half.
;   spill = floor((acc + contrib_lo + cy_lo) / 256)
;   cy    = contrib_hi + spill + cy_hi

_bn_sqr256_asm::
        xor     a
        ld      (0x3000), a

        ld      hl, #_bn_mul_prod
        ld      b, #64
        xor     a
bn_sqr_clr:
        ld      (hl+), a
        dec     b
        jr      nz, bn_sqr_clr

        ld      a, #31
        ld      (#_bn_i), a

bn_sqr_outer:
        ld      hl, #_bn_mul_pa
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      a, (#_bn_i)
        ld      e, a
        ld      d, #0
        add     hl, de
        ld      a, (hl)
        ld      (#_bn_ai), a

        ld      a, (#_bn_ai)
        rlca
        rlca
        rlca
        and     #0x07
        add     a, #MUL_BASE
        ld      (0x2000), a

        ld      a, (#_bn_ai)
        and     #0x1F
        add     a, a
        add     a, #0x40
        ld      (#_bn_page), a
        ld      c, a

        ld      hl, #_bn_mul_pa
        ld      a, (hl+)
        ld      h, (hl)
        ld      l, a
        ld      de, #31
        add     hl, de
        ld      d, h
        ld      e, l

        ld      hl, #_bn_mul_prod
        ld      a, (#_bn_i)
        add     a, #32
        push    de
        ld      e, a
        ld      d, #0
        add     hl, de
        pop     de

        xor     a
        ld      (#_bn_cy), a
        ld      (#_bn_cy+1), a

        ld      a, (#_bn_i)
        ld      b, a
        ld      a, #31
        sub     a, b
        jr      z, bn_sqr_diag
        ld      c, a
        xor     a
        ld      b, a

; Cross terms. B is cy_lo, C is the count, page is _bn_page.
; Inlined: a call per product was slower than the schoolbook multiply.
bn_sqr_cross:
        ld      a, (de)
        dec     de
        push    de
        push    hl
        ld      l, a
        ld      a, (#_bn_page)
        ld      h, a
        ld      e, (hl)
        inc     h
        ld      d, (hl)
        ld      a, e
        add     a, a
        ld      e, a
        ld      a, d
        adc     a, a
        ld      d, a
        ld      a, #0
        adc     a, #0
        ld      (#_bn_extra), a
        pop     hl
        ld      a, (hl)
        add     a, e
        ld      e, a
        ld      a, #0
        adc     a, #0
        ld      (#_bn_spill), a
        ld      a, b
        add     a, e
        ld      (hl), a
        ld      a, (#_bn_spill)
        adc     a, #0
        add     a, d
        ld      e, a
        ld      a, (#_bn_extra)
        adc     a, #0
        ld      d, a
        ld      a, (#_bn_cy+1)
        add     a, e
        ld      b, a
        ld      a, d
        adc     a, #0
        ld      (#_bn_cy+1), a
        pop     de
        dec     hl
        dec     c
        jr      nz, bn_sqr_cross
        ld      a, b
        ld      (#_bn_cy), a
        ld      a, (#_bn_page)
        ld      c, a

bn_sqr_diag:
        ld      a, (#_bn_ai)
        call    bn_sqr_add
        dec     hl
        call    bn_sqr_ripple

        ld      a, (#_bn_i)
        or      a
        jr      z, bn_sqr_finish
        dec     a
        ld      (#_bn_i), a
        jp      bn_sqr_outer

bn_sqr_finish:
        ld      a, (#_bn_saved_bank)
        ld      (0x2000), a
        ret

; A = limb. Product once. Preserves the caller's DE, HL, and C.
bn_sqr_add:
        push    de
        call    bn_sqr_lookup
        xor     a
        ld      (#_bn_extra), a
        call    bn_sqr_apply
        pop     de
        ret

; A = limb. Product doubled. Preserves the caller's DE, HL, and C.
bn_sqr_dbladd:
        push    de
        call    bn_sqr_lookup
        ld      a, e
        add     a, a
        ld      e, a
        ld      a, d
        adc     a, a
        ld      d, a
        ld      a, #0
        adc     a, #0
        ld      (#_bn_extra), a
        call    bn_sqr_apply
        pop     de
        ret

; A = limb, C = page. E = low byte, D = high byte. Preserves C and HL.
bn_sqr_lookup:
        push    hl
        ld      l, a
        ld      h, c
        ld      e, (hl)
        inc     h
        ld      d, (hl)
        pop     hl
        ret

; (HL) += E + cy_lo. spill is that sum's high part (0..2).
; new cy = (extra<<8 | D) + spill + cy_hi.
; cy_hi is the old carry>>8 (0..2). It adds into the low byte.
; Adding it into the high byte skips a column.
; Preserves HL, DE, and C.
bn_sqr_apply:
        ld      a, (hl)
        add     a, e
        ld      b, a
        ld      a, #0
        adc     a, #0
        ld      (#_bn_spill), a
        ld      a, (#_bn_cy)
        add     a, b
        ld      (hl), a
        ld      a, (#_bn_spill)
        adc     a, #0
        add     a, d
        ld      b, a
        ld      a, (#_bn_extra)
        adc     a, #0
        ld      (#_bn_spill), a
        ld      a, (#_bn_cy+1)
        add     a, b
        ld      (#_bn_cy), a
        ld      a, (#_bn_spill)
        adc     a, #0
        ld      (#_bn_cy+1), a
        ret

; Add the 16-bit _bn_cy into (HL) and ripple toward prod[0].
bn_sqr_ripple:
        ld      a, (#_bn_cy)
        or      a
        jr      nz, bn_sqr_rip_add
        ld      a, (#_bn_cy+1)
        or      a
        ret     z
bn_sqr_rip_add:
        ld      a, (hl)
        ld      b, a
        ld      a, (#_bn_cy)
        add     a, b
        ld      (hl), a
        ld      a, (#_bn_cy+1)
        adc     a, #0
        or      a
        ret     z
        ld      (#_bn_cy), a
        xor     a
        ld      (#_bn_cy+1), a
        ld      a, l
        cp      #<(_bn_mul_prod)
        jr      nz, bn_sqr_rip_dec
        ld      a, h
        cp      #>(_bn_mul_prod)
        ret     z
bn_sqr_rip_dec:
        dec     hl
        jr      bn_sqr_ripple

        .area _BSS
_bn_mul_pa::
        .ds 2
_bn_mul_pb::
        .ds 2
_bn_mul_prod::
        .ds 64
_bn_saved_bank::
        .ds 1
_bn_i::
        .ds 1
_bn_ai::
        .ds 1
_bn_page::
        .ds 1
_bn_n::
        .ds 1
_bn_cy::
        .ds 2
_bn_spill::
        .ds 1
_bn_extra::
        .ds 1
