' ARGS: -extat 30000 80 -extat 45001 0 -extat 60002 80
' EXPECT-SLEEPS: 5
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
        add     ptr, #4
:b      wrlong  zero, scratch
        test    p7, ina wz
  if_nz jmp     #:b
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:c      test    p7, ina wz
  if_z  jmp     #:c
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:d      test    p5, ina wc
  if_nc jmp     #:d
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p7      long    %10000000
p5      long    %100000
zero    long    0
scratch long    $6200
t       long    0
x       long    0
y       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pins
        movi    ctra, #%00100_000
        movs    ctra, #3
        mov     frqa, frq
        mov     w, cnt
        add     w, d20000
        mov     n, #5
:tog    waitcnt w, d700
        xor     outa, #%100
        djnz    n, #:tog
        wrlong  one, scr
        mov     n, #10
:tog2   waitcnt w, d700
        xor     outa, #%100
        djnz    n, #:tog2
        add     w, d20000
        waitcnt w, #0
        or      outa, #%100000
        mov     w, #200
        add     w, cnt
        waitcnt w, #0
        cogid   n
        cogstop n
pins    long    %100100
frq     long    $1000
d700    long    700
d20000  long    20000
one     long    1
scr     long    $6200
w       long    0
n       long    0
        long    $C0DEE0D0
