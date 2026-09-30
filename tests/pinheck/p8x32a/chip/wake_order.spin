' EXPECT-SLEEPS: 8
' short pulses from a busy cog must wake a polling cog before the busy cog's next event
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
        mov     n, #8
:d      test    p5, ina wc
  if_nc jmp     #:d
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:e      test    p5, ina wc
  if_c  jmp     #:e
        djnz    n, #:d
        cogid   t
        cogstop t
p5      long    %100000
t       long    0
x       long    0
y       long    0
n       long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, pin
        mov     w, cnt
        add     w, d20000
        mov     k, #4
:p      waitcnt w, d3001
        or      outa, pin
        andn    outa, pin
        waitcnt w, d3002
        or      outa, pin
        nop
        andn    outa, pin
        waitcnt w, d3003
        or      outa, pin
        nop
        nop
        andn    outa, pin
        djnz    k, #:p
        waitcnt w, #0
        cogid   k
        cogstop k
pin     long    %100000
d3001   long    3001
d3002   long    3002
d3003   long    3003
d20000  long    20000
w       long    0
k       long    0
        long    $C0DEE0D0
