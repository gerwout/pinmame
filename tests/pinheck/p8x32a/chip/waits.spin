PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     dira, #%100000
        mov     t, cnt
        add     t, delay
        waitcnt t, #37
        or      outa, #%100000
        waitcnt t, #100
        andn    outa, #%100000
        waitcnt t, #0
        mov     t, cnt
        waitpeq pin6, pin6
        mov     u, cnt
        sub     u, t
        wrlong  u, ptr
        add     ptr, #4
        mov     t, cnt
        add     t, #9
        waitcnt t, #0
        mov     u, cnt
        sub     u, t
        wrlong  u, ptr
        cogstop x
        cogid   t
        cogstop t
x       long    0
t       long    0
u       long    0
pin6    long    %1000000
delay   long    10000
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%1000000
        waitpeq p5, p5
        mov     s, cnt
        waitpne p5, p5
        mov     e, cnt
        or      outa, #%1000000
        sub     e, s
        mov     a, #$60
        shl     a, #8
        add     a, #16
        wrlong  e, a
:spin   jmp     #:spin
p5      long    %100000
s       long    0
e       long    0
a       long    0
