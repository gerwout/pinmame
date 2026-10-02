PUB main
DAT
        long    $C0DE5EED
        org     0
' cog 6 stops cog 1 just as cog 1, waking from WAITCNT, starts a translated block that ends with a hub write whose
' slot comes after the stop: the write must not run. 24 trials, cog 1's start one cycle later each time, from 20 cycles
' before a cycle 4096 * k - 1 (where p8run's 4,096-cycle runs end). Each trial's writes from $6000 + 64 * trial
entry   mov     x, wk
        shl     x, #2
        mov     b, cnt
        add     b, k16k
        andn    b, k4095
        add     b, k4095
        mov     i, #0
:tr     mov     v, b
        sub     v, #20
        add     v, i
        wrlong  v, pp
        add     pp, #4
        mov     v, b
        sub     v, #10
        wrlong  v, pp
        add     pp, #4
        wrlong  out, pp
        sub     pp, #8
        mov     y, pp
        shl     y, #16
        or      y, x
        or      y, #1
        coginit y
        mov     y, pp
        shl     y, #16
        or      y, x
        or      y, #6
        coginit y
        mov     v, b
        add     v, #256
        waitcnt v, #0
        add     b, k12k
        add     pp, #16
        add     out, #64
        add     i, #1
        cmp     i, #24 wz
  if_nz jmp     #:tr
        cogid   y
        cogstop y
x       long    0
y       long    0
v       long    0
b       long    0
i       long    0
pp      long    $7000
out     long    $6000
k4095   long    4095
k12k    long    12288
k16k    long    16384
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  cogid   w
        cmp     w, #6 wz
  if_z  jmp     #stopper
        mov     p, par
        rdlong  w, p
        add     p, #8
        rdlong  a, p
        waitcnt w, #0
lp      wrlong  c, a
        add     a, #4
        add     c, #1
        jmp     #lp
stopper mov     p, par
        add     p, #4
        rdlong  w, p
        waitcnt w, #0
        cogstop one
        mov     w, cnt
        add     w, long1
        waitcnt w, #0
        cogid   w
        cogstop w
p       long    0
w       long    0
a       long    0
c       long    1
one     long    1
long1   long    100
