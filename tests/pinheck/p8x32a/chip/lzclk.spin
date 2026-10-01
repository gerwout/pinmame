' EXPECT-LAZY: 1
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: an output-only cog (cog 1) on pin 18 follows hub byte $6200; cog 0 sets the byte, and 200 cycles later
' (cog 1's pin change not caught up yet) runs CLKSET: the pin change reaches the host before the CLKSET
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d40000
        waitcnt t, #0
        wrbyte  one, buf
        mov     t, cnt
        add     t, d200
        waitcnt t, #0
        clkset  mode
        mov     t, cnt
        add     t, d1000
        waitcnt t, #0
        cogstop one
        cogid   r
        cogstop r
one     long    1
d200    long    200
d1000   long    1000
d40000  long    40000
parf    long    $62000000
buf     long    $6200
mode    long    $6F
r       long    0
t       long    0
x       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pins
:loop   rdbyte  v, par
        test    v, #1 wc
        muxc    outa, pins
        jmp     #:loop
pins    long    $00040000
v       long    0
        long    $C0DEE0D0
