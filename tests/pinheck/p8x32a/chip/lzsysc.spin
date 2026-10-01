' EXPECT-LAZY: 0
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 1 copies the C of RDBYTE WC (the hub's lock/cog result) to pin 18 while cog 0 sets and clears lock 0
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d40000
        waitcnt t, #0
        mov     k, #200
:lp     lockclr lk
        lockclr lk
        mov     t, cnt
        add     t, d100
        waitcnt t, #0
        lockset lk
        lockset lk
        mov     t, cnt
        add     t, d100
        waitcnt t, #0
        djnz    k, #:lp
        cogstop one
        cogid   r
        cogstop r
one     long    1
d100    long    100
d40000  long    40000
parf    long    $62000000
lk      long    0
r       long    0
t       long    0
x       long    0
k       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pins
:loop   rdbyte  v, par wc
        muxc    outa, pins
        jmp     #:loop
pins    long    $00040000
v       long    0
        long    $C0DEE0D0
