' EXPECT-LAZY: 1
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: an output-only cog (cog 1) on pin 18 follows hub byte $6200; cog 0 waits with WAITPEQ for its pin
' (the lazy cog runs as the others from there) and records CNT and INA at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        or      dira, p0bit
        mov     t, cnt
        add     t, d40000
        waitcnt t, #0
        wrbyte  one, buf
        mov     t, cnt
        add     t, d1000
        waitcnt t, #0
        xor     outa, p0bit
        mov     t, cnt
        add     t, d1000
        waitcnt t, #0
        waitpeq lbit, lbit
        mov     r, cnt
        wrlong  r, res0
        mov     r, ina
        wrlong  r, res1
        cogstop one
        cogid   r
        cogstop r
one     long    1
d1000   long    1000
d40000  long    40000
parf    long    $62000000
buf     long    $6200
res0    long    $6000
res1    long    $6004
p0bit   long    $00000001
lbit    long    $00040000
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
        muxc    outa, lbit2
        jmp     #:loop
pins    long    $00040000
lbit2   long    $00040000
v       long    0
        long    $C0DEE0D0
