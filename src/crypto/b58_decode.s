        .module b58_decode
        .area   _CODE

; uint8_t b58_decode(const char *text, uint8_t *out)
; DE = text, BC = out. A = 1 when 25 bytes are filled, 0 on a
; bad character or a value that does not fit. Stops at NUL or
; 36 characters, whichever comes first. The low byte of
; byte*58+carry is stored directly. Leaves IME alone.

        .globl  _b58_decode

_b58_decode::
        ld      hl, #_b58_out
        ld      (hl), c
        inc     hl
        ld      (hl), b

        ld      h, b
        ld      l, c
        ld      c, #25
        xor     a
b58_clr:
        ld      (hl), a
        inc     hl
        dec     c
        jr      nz, b58_clr

        ld      a, #36
        ld      (#_b58_left), a

b58_char:
        ld      a, (#_b58_left)
        or      a
        jr      z, b58_ok
        dec     a
        ld      (#_b58_left), a

        ld      a, (de)
        or      a
        jr      z, b58_ok
        inc     de

        push    de
        ld      c, a
        ld      b, #0
        ld      hl, #_b58_alpha
b58_find:
        ld      a, (hl+)
        cp      c
        jr      z, b58_found
        inc     b
        ld      a, b
        cp      #58
        jr      nz, b58_find
        pop     de
        xor     a
        ret

b58_found:
        ld      c, b
        ld      b, #0
        ld      a, (#_b58_out)
        ld      l, a
        ld      a, (#_b58_out + 1)
        ld      h, a
        ld      de, #24
        add     hl, de
        ld      e, #25

b58_byte:
        push    hl
        ld      a, (hl)
        push    bc
        ld      c, a
        ld      b, #0
        ld      l, a
        ld      h, b
        add     hl, hl
        add     hl, bc
        add     hl, hl
        add     hl, bc
        add     hl, hl
        add     hl, hl
        add     hl, bc
        add     hl, hl
        pop     bc
        add     hl, bc
        ld      c, l
        ld      b, h
        pop     hl
        ld      (hl), c
        dec     hl
        ld      c, b
        ld      b, #0
        dec     e
        jr      nz, b58_byte

        ld      a, c
        or      a
        jr      z, b58_next
        pop     de
        xor     a
        ret

b58_next:
        pop     de
        jr      b58_char

b58_ok:
        ld      a, #1
        ret

_b58_alpha:
        .ascii  "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz"

        .area   _BSS
_b58_out:
        .ds     2
_b58_left:
        .ds     1
