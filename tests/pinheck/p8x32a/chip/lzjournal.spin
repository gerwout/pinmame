' EXPECT-LAZY: 1
' EXPECT-JOURNAL: 1621 23
PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 0: starts the scan cog (cog 1) on 8 bytes at $6200, then rewrites them 400 times (bytes, words and longs, the
' same bytes again and again), reads INA, and stops the scan cog; results at $6000
entry   mov     x, wk
        shl     x, #2
        or      x, parf
        or      x, #1
        coginit x
        mov     t, cnt
        add     t, d20000
        waitcnt t, #0
        mov     k, d400
:burst  add     v, k
        wrbyte  v, b3
        rol     v, #3
        wrbyte  v, b5
        add     v, #77
        wrword  v, b2
        test    k, #7 wz
  if_z  wrlong  v, b4
        xor     v, k
        wrbyte  v, b3
        djnz    k, #:burst
        mov     r, ina
        wrlong  r, res0
        mov     t, cnt
        add     t, d3000
        waitcnt t, #0
        cogstop one
        mov     r, ina
        wrlong  r, res1
        cogid   r
        cogstop r
one     long    1
d400    long    400
d3000   long    3000
d20000  long    20000
parf    long    $62000000
b2      long    $6202
b3      long    $6203
b4      long    $6204
b5      long    $6205
res0    long    $6000
res1    long    $6004
r       long    0
k       long    0
t       long    0
v       long    $1234567
x       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
' the scan cog: each of the 8 bytes in turn on P8-P15, with P16 toggling
worker  mov     dira, pins
:frame  mov     p2, par
        mov     n, #8
:byte   rdbyte  v2, p2
        shl     v2, #8
        xor     v2, tbit
        mov     outa, v2
        add     p2, #1
        djnz    n, #:byte
        xor     tbit, tbit2
        jmp     #:frame
pins    long    $0001FF00
tbit    long    0
tbit2   long    $00010000
p2      long    0
n       long    0
v2      long    0
