PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     t, par
        call    #put
        mov     ina, #123
        cmp     ina, #123 wz
        muxz    f, #1
        add     cnt, #5
        mov     t, #0
        cmp     cnt, #5 wz
        muxz    f, #2
        mov     ctra, nco
        mov     frqa, #7
        mov     t, phsa
        nop
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     t, phsa
        call    #put
        add     phsa, #1
        mov     t, phsa
        call    #put
        mov     frqa, #3
        mov     t, phsa
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     ctra, duty
        mov     t, phsa
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        mov     ctra, #0
        mov     t, phsa
        nop
        mov     u, phsa
        sub     u, t
        mov     t, u
        call    #put
        movs    :next, #99
:next   mov     t, #1
        call    #put
        movs    :nx2, #77
        nop
:nx2    mov     t, #1
        call    #put
        rdlong  t, romw
        call    #put
        rdbyte  t, romb
        call    #put
        rdword  t, romw2
        call    #put
        mov     dira, #$F
        mov     outa, #3
        max     outa, #1 wc
        muxc    f, #4
        mov     t, outa
        call    #put
        mov     outa, #3
        max     outa, #5 wc
        muxc    f, #8
        mov     t, outa
        call    #put
        mov     t, #0
        wrlong  t, ptr
        add     ptr, #4
        wrlong  f, ptr
        cogid   t
        cogstop t
put     wrlong  t, ptr
        add     ptr, #4
put_ret ret
t       long    0
u       long    0
f       long    0
nco     long    %00100 << 26
duty    long    %00110 << 26
romw    long    $F004
romb    long    $8021
romw2   long    $E002
ptr     long    $6000
        long    $C0DEE0D0
