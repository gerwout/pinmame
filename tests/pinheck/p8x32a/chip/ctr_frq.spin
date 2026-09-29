' EXPECT-SLEEPS: 2
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     y, #$61
        shl     y, #24
        or      x, y
        coginit x
:a      test    p4, ina wz
  if_z  jmp     #:a
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:b      test    p4, ina wz
  if_nz jmp     #:b
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p4      long    %10000
t       long    0
x       long    0
y       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, p4w
        movi    ctra, #%00100_000
        movs    ctra, #4
        mov     frqa, #1
        mov     w, cnt
        add     w, d9000
        waitcnt w, d9000
        mov     frqa, fast
        waitcnt w, d9000
        mov     phsa, big
        waitcnt w, #0
        mov     frqa, #0
        cogid   w
        cogstop w
p4w     long    %10000
fast    long    $00100000
big     long    $7FFFFF00
d9000   long    9000
w       long    0
        long    $C0DEE0D0
