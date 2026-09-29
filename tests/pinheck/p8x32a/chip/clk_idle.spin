' ARGS: -extat 30000 80
' EXPECT-SLEEPS: 1
' EXPECT-CLKSHIFT: 8000 22010
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
:a      test    p7, ina wz
  if_z  jmp     #:a
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p7      long    %10000000
t       long    0
x       long    0
y       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     w, cnt
        add     w, d5000
        waitcnt w, #0
        clkset  mode
        cogid   w
        cogstop w
mode    long    $6F
d5000   long    5000
w       long    0
        long    $C0DEE0D0
