' EXPECT-LAZY: 1
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: starts an output-only cog (cog 1) on pin 18, sets the pin high through hub RAM while cog 1 runs lazily,
' then sets DIRA on its pin (OUTA low) at T + 8; cogs 2-5 read INA every 4 cycles from T + 0..3 (samples at $6000 + 128 * sampler)
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d40000
        wrlong  t, tadr
        mov     sk, wk
        add     sk, #32
        shl     sk, #2
        or      sk, #8
        mov     k, #4
        mov     a, smp
:st     mov     x, a
        shl     x, #16
        or      x, sk
        coginit x
        add     a, d128
        djnz    k, #:st
        mov     x, t
        sub     x, d2000
        waitcnt x, #0
        wrbyte  one, buf
        add     t, #8
        waitcnt t, #0
        or      dira, lbit
        mov     t, cnt
        add     t, d5000
        waitcnt t, #0
        cogstop one
        cogid   r
        cogstop r
one     long    1
d128    long    128
d2000   long    2000
d5000   long    5000
d40000  long    40000
parf    long    $62000000
buf     long    $6200
tadr    long    $6400
smp     long    $6000
sk      long    0
lbit    long    $00040000
a       long    0
k       long    0
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
        org     0
samp    mov     sa, par
        rdlong  st, tadr2
        mov     si, par
        shr     si, #7
        and     si, #3
        add     st, si
        waitcnt st, #0
        mov     s0, ina
        mov     s1, ina
        mov     s2, ina
        mov     s3, ina
        mov     s4, ina
        mov     s5, ina
        mov     s6, ina
        mov     s7, ina
        mov     s8, ina
        mov     s9, ina
        mov     s10, ina
        mov     s11, ina
        mov     s12, ina
        mov     s13, ina
        mov     s14, ina
        mov     s15, ina
        mov     s16, ina
        mov     s17, ina
        mov     s18, ina
        mov     s19, ina
        mov     s20, ina
        mov     s21, ina
        mov     s22, ina
        mov     s23, ina
        wrlong  s0, sa
        add     sa, #4
        wrlong  s1, sa
        add     sa, #4
        wrlong  s2, sa
        add     sa, #4
        wrlong  s3, sa
        add     sa, #4
        wrlong  s4, sa
        add     sa, #4
        wrlong  s5, sa
        add     sa, #4
        wrlong  s6, sa
        add     sa, #4
        wrlong  s7, sa
        add     sa, #4
        wrlong  s8, sa
        add     sa, #4
        wrlong  s9, sa
        add     sa, #4
        wrlong  s10, sa
        add     sa, #4
        wrlong  s11, sa
        add     sa, #4
        wrlong  s12, sa
        add     sa, #4
        wrlong  s13, sa
        add     sa, #4
        wrlong  s14, sa
        add     sa, #4
        wrlong  s15, sa
        add     sa, #4
        wrlong  s16, sa
        add     sa, #4
        wrlong  s17, sa
        add     sa, #4
        wrlong  s18, sa
        add     sa, #4
        wrlong  s19, sa
        add     sa, #4
        wrlong  s20, sa
        add     sa, #4
        wrlong  s21, sa
        add     sa, #4
        wrlong  s22, sa
        add     sa, #4
        wrlong  s23, sa
        add     sa, #4
        cogid   si
        cogstop si
tadr2   long    $6400
sa      long    0
si      long    0
st      long    0
s0      long    0
s1      long    0
s2      long    0
s3      long    0
s4      long    0
s5      long    0
s6      long    0
s7      long    0
s8      long    0
s9      long    0
s10     long    0
s11     long    0
s12     long    0
s13     long    0
s14     long    0
s15     long    0
s16     long    0
s17     long    0
s18     long    0
s19     long    0
s20     long    0
s21     long    0
s22     long    0
s23     long    0
        long    $C0DEE0D0
