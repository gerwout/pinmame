' EXPECT-SLEEPS: 5
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     y0, #$61
        shl     y0, #24
        or      y0, x
        mov     y, y0
        coginit y wc, wr
        mov     t, cnt
        add     t, d20000
        waitcnt t, d1500
        wrlong  zero, flag
        waitcnt t, d1500
        wrbyte  one, flag3
        waitcnt t, d3000
        wrlong  zero, flag
        wrlong  zero, mark
        waitcnt t, d3000
        cogstop y
        waitcnt t, #500
        andn    y0, #%1111
        or      y0, y
        coginit y0
        waitcnt t, d12000
        waitcnt t, d12000
        coginit y0
        waitcnt t, d12000
        waitcnt t, d3000
        wrlong  two, flag
        waitcnt t, d3000
        cogid   t
        cogstop t
zero    long    0
d20000  long    20000
d12000  long    12000
d1500   long    1500
d3000   long    3000
one     long    1
two     long    2
flag    long    $6100
flag3   long    $6103
mark    long    $6108
t       long    0
x       long    0
y       long    0
y0      long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     a, par
        add     a, #4
        mov     m, par
        add     m, #8
:w1     rdlong  v, par wz
  if_z  jmp     #:w1
        mov     v, cnt
        wrlong  v, a
        add     a, #4
        add     a, #4
:w2     call    #chk
        wrlong  five, m
  if_nz jmp     #:w2
        mov     v, cnt
        wrlong  v, a
        cogid   v
        cogstop v
chk     rdlong  v, par
        cmp     v, #2 wz
chk_ret ret
a       long    0
m       long    0
v       long    0
five    long    5
        long    $C0DEE0D0
