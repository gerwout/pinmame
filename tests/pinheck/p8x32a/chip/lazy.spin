' EXPECT-LAZY: 3
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: starts the scan cog (cog 1) on a buffer at $6200, then writes the buffer, reads INA whole and masked,
' reads INA eight times 777 cycles apart, waits for a rising edge of a scan pin, drives one high, and stops the scan cog; results at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d40000
        waitcnt t, #0
        wrbyte  b5, buf3
        wrbyte  b5, buf3
        mov     r, ina
        wrlong  r, res0
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        test    dmask, ina wc
        muxc    r2, #1
        test    omask, ina wz
        muxz    r2, #2
        wrlong  r2, res1
        mov     k, #8
        mov     a, res7
:rd     mov     t, cnt
        add     t, d777
        waitcnt t, #0
        mov     r, ina
        wrlong  r, a
        add     a, #4
        djnz    k, #:rd
        wrbyte  b9, buf7
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        waitpne lbit, lbit
        waitpeq lbit, lbit
        mov     r, cnt
        wrlong  r, res2
        mov     t, cnt
        add     t, d90000
        waitcnt t, #0
        mov     r, ina
        wrlong  r, res3
        or      outa, zbit
        or      dira, zbit
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        mov     r, ina
        wrlong  r, res4
        andn    dira, zbit
        andn    outa, zbit
        mov     t, cnt
        add     t, d90000
        waitcnt t, #0
        wrbyte  b9, buf3
        mov     r, ina
        wrlong  r, res5
        cogstop one
        mov     r, ina
        wrlong  r, res6
        cogid   r
        cogstop r
one     long    1
d3000   long    3000
d777    long    777
d40000  long    40000
d90000  long    90000
b5      long    $A5
b9      long    $5A
parf    long    $62000000
buf3    long    $6203
buf7    long    $6207
res0    long    $6000
res1    long    $6004
res2    long    $6008
res3    long    $600C
res4    long    $6010
res5    long    $6014
res6    long    $6018
res7    long    $6020
dmask   long    $00010000
omask   long    $00000F00
lbit    long    $00040000
zbit    long    $00080000
r       long    0
a       long    0
k       long    0
r2      long    0
t       long    0
x       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
' the scan cog: per byte two bits on P16 with a clock on P17, then P18 toggles, and P19 where CNT bit 4 is 0
worker  mov     dira, pins
:frame  mov     p2, par
        mov     n, #16
:byte   rdbyte  v, p2
        test    v, #1 wc
        muxc    outa, dbit
        or      outa, cbit
        andn    outa, cbit
        test    v, #2 wc
        muxc    outa, dbit
        or      outa, cbit
        andn    outa, cbit
        add     p2, #1
        djnz    n, #:byte
        xor     outa, lbit2
        mov     q, cnt
        test    q, #$10 wz
  if_z  xor     outa, zbit2
        jmp     #:frame
pins    long    $000F0000
dbit    long    $00010000
cbit    long    $00020000
lbit2   long    $00040000
zbit2   long    $00080000
p2      long    0
n       long    0
v       long    0
q       long    0
        long    $C0DEE0D0
